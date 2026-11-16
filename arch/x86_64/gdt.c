/*
 * Global Descriptor Table and Task State Segment.
 *
 * In 64-bit mode segmentation no longer translates addresses, but the CPU
 * still needs a GDT for two things: code segment descriptors decide the
 * privilege level (ring 0 or 3) and 64-bit mode, and a TSS descriptor points
 * at the TSS, which holds the stacks the CPU switches to on interrupts.
 *
 * Limine leaves its own GDT loaded. We replace it because the kernel must
 * own every table the CPU consults, and we need user segments and a TSS.
 *
 * State: `gdt`, the IST stacks and tss.ist are written once here, during
 * single-threaded boot with interrupts off. tss.rsp[0] is rewritten by
 * arch_set_kernel_stack() on every thread switch, with the scheduler lock
 * held.
 */
#include "gdt.h"

#include <stdint.h>

#include <arch/cpu.h>

#include "syscall.h"

/* Access byte: present, descriptor type (code/data), and for code: readable;
 * for data: writable. DPL (bits 5-6) is the privilege level. */
#define ACCESS_PRESENT   (1ULL << 47)
#define ACCESS_CODE_DATA (1ULL << 44)
#define ACCESS_EXEC      (1ULL << 43)
#define ACCESS_RW        (1ULL << 41)
#define ACCESS_DPL(ring) ((uint64_t)(ring) << 45)
/* L bit: this code segment runs in 64-bit mode. */
#define FLAG_LONG_MODE   (1ULL << 53)

#define SEGMENT_CODE(ring) \
    (ACCESS_PRESENT | ACCESS_CODE_DATA | ACCESS_EXEC | ACCESS_RW | ACCESS_DPL(ring) | FLAG_LONG_MODE)
#define SEGMENT_DATA(ring) \
    (ACCESS_PRESENT | ACCESS_CODE_DATA | ACCESS_RW | ACCESS_DPL(ring))

/* System descriptor type 9: available 64-bit TSS. */
#define TSS_TYPE_AVAILABLE 0x9ULL

/* Layout fixed by the CPU (Intel SDM vol. 3, "64-bit TSS"). */
struct tss {
    uint32_t reserved0;
    uint64_t rsp[3]; /* stacks for entering ring 0..2 from a lower ring */
    uint64_t reserved1;
    uint64_t ist[7]; /* Interrupt Stack Table */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_bitmap_offset;
} __attribute__((packed));

struct descriptor_table_pointer {
    uint16_t limit; /* size in bytes minus one */
    uint64_t base;
} __attribute__((packed));

#define IST_STACK_SIZE 8192

/* null, kernel code, kernel data, user data, user code, TSS (two slots:
 * a 64-bit base does not fit in one 8-byte descriptor). */
static uint64_t gdt[7];
static struct tss tss;
static uint8_t double_fault_stack[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t nmi_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

/* Implemented in lowlevel.asm. */
void gdt_load(const struct descriptor_table_pointer *pointer, uint16_t code_selector, uint16_t data_selector);

void arch_set_kernel_stack(void *top)
{
    /* Two mechanisms enter the kernel from user mode and each has its own
     * idea of where the stack is: interrupts and exceptions read it from
     * the TSS, the syscall instruction does not switch stacks at all and
     * our entry code loads it from a variable. */
    tss.rsp[0] = (uint64_t)(uintptr_t)top;
    syscall_kernel_stack = (uint64_t)(uintptr_t)top;
}

static uint64_t stack_top(uint8_t *stack)
{
    /* Stacks grow downwards, so the initial stack pointer is the end. */
    return (uint64_t)(uintptr_t)stack + IST_STACK_SIZE;
}

static void tss_init(void)
{
    tss.ist[IST_DOUBLE_FAULT - 1] = stack_top(double_fault_stack);
    tss.ist[IST_NMI - 1] = stack_top(nmi_stack);
    /* An offset beyond the TSS limit means "no I/O permission bitmap". */
    tss.io_bitmap_offset = sizeof(tss);
}

static void gdt_set_tss(unsigned index)
{
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;

    /* The base address is scattered over the descriptor for historical
     * reasons: bits 0-23, then 24-31, then 32-63 in the second slot. */
    gdt[index] = (limit & 0xFFFF)
               | ((base & 0xFFFFFF) << 16)
               | (TSS_TYPE_AVAILABLE << 40)
               | ACCESS_PRESENT
               | (((limit >> 16) & 0xF) << 48)
               | (((base >> 24) & 0xFF) << 56);
    gdt[index + 1] = base >> 32;
}

void gdt_init(void)
{
    tss_init();

    gdt[0] = 0; /* the CPU requires selector 0 to be an unusable null entry */
    gdt[GDT_KERNEL_CODE / 8] = SEGMENT_CODE(0);
    gdt[GDT_KERNEL_DATA / 8] = SEGMENT_DATA(0);
    gdt[GDT_USER_DATA / 8] = SEGMENT_DATA(3);
    gdt[GDT_USER_CODE / 8] = SEGMENT_CODE(3);
    gdt_set_tss(GDT_TSS / 8);

    struct descriptor_table_pointer pointer = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt,
    };
    gdt_load(&pointer, GDT_KERNEL_CODE, GDT_KERNEL_DATA);

    /* Tell the CPU which GDT entry describes the TSS. */
    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS));
}
