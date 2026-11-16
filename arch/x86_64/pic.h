#ifndef ARCH_X86_64_PIC_H
#define ARCH_X86_64_PIC_H

/* Moves the legacy 8259 PICs out of the exception vectors and masks every
 * line, so that the APICs are the only source of interrupts. */
void pic_disable(void);

#endif
