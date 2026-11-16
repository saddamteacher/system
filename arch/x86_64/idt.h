#ifndef ARCH_X86_64_IDT_H
#define ARCH_X86_64_IDT_H

#define IDT_VECTOR_COUNT 256

/* Builds and loads the IDT. Requires gdt_init() to have run. */
void idt_init(void);

#endif
