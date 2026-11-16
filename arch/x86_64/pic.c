/*
 * Legacy 8259 Programmable Interrupt Controllers (a master/slave pair).
 *
 * We use the APICs instead, but the PICs still exist and must be silenced.
 * Masking alone is not enough: after reset they deliver IRQ 0-7 on vectors
 * 8-15, which are CPU exceptions (8 is the double fault). A spurious PIC
 * interrupt would then look like a crash, so they are first remapped to
 * vectors of their own.
 */
#include "pic.h"

#include "io.h"
#include "vectors.h"

#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA    0x21
#define PIC_SLAVE_COMMAND  0xA0
#define PIC_SLAVE_DATA     0xA1

/* Initialisation is a fixed four-byte sequence ("ICW1-4"). */
#define ICW1_INIT_WITH_ICW4 0x11
#define ICW3_MASTER_SLAVE_ON_IRQ2 0x04
#define ICW3_SLAVE_IDENTITY 0x02
#define ICW4_8086_MODE 0x01

#define MASK_ALL 0xFF

void pic_disable(void)
{
    io_out8(PIC_MASTER_COMMAND, ICW1_INIT_WITH_ICW4);
    io_out8(PIC_SLAVE_COMMAND, ICW1_INIT_WITH_ICW4);
    io_out8(PIC_MASTER_DATA, VECTOR_PIC_BASE);
    io_out8(PIC_SLAVE_DATA, VECTOR_PIC_BASE + 8);
    io_out8(PIC_MASTER_DATA, ICW3_MASTER_SLAVE_ON_IRQ2);
    io_out8(PIC_SLAVE_DATA, ICW3_SLAVE_IDENTITY);
    io_out8(PIC_MASTER_DATA, ICW4_8086_MODE);
    io_out8(PIC_SLAVE_DATA, ICW4_8086_MODE);

    io_out8(PIC_MASTER_DATA, MASK_ALL);
    io_out8(PIC_SLAVE_DATA, MASK_ALL);
}
