# 03 - Interrupts, time and the keyboard

Until this phase the kernel only ran code it had started itself. Now
hardware can interrupt it: a timer 1000 times per second and the keyboard on
every key transition.

## Path of a device interrupt

```
device --line--> I/O APIC --vector--> local APIC --> CPU --> IDT stub
      --> trap_dispatch --> irq_dispatch --> registered handler
```

| Vectors | Use |
|---|---|
| `0x00-0x1F` | CPU exceptions |
| `0x20-0x2F` | legacy PIC, remapped and masked (only spurious interrupts) |
| `0x30` | local APIC timer |
| `0x31-0x40` | device IRQ 0-15 via the I/O APIC |
| `0xFF` | local APIC spurious interrupt |

## Finding the hardware: ACPI

The addresses of the APICs are not fixed; the firmware describes them in ACPI
tables. `arch/x86_64/acpi.c` starts from the RSDP (obtained through a Limine
request), follows it to the XSDT or RSDT, and reads two tables:

- **MADT** (`"APIC"`): local APIC address, I/O APIC address and base, and
  *interrupt source overrides*. An override says that a legacy IRQ is wired
  to a different I/O APIC input than its number suggests, possibly with
  different polarity or trigger mode. The classic one is IRQ 0 on input 2.
- **FADT** (`"FACP"`): the I/O port of the ACPI PM timer.

Tables are checksummed and live outside the direct map, so each is mapped
with the `map_device` function the kernel passes to `arch_irq_init`
(`vmm_map_physical`).

## Controllers

- **Legacy PIC** (`pic.c`): not used, but it must be silenced. It is first
  remapped to vectors `0x20-0x2F`, because by default it delivers on vectors
  8-15, where a spurious interrupt would look like a double fault. Then every
  line is masked.
- **Local APIC** (`lapic.c`): enabled through the `APIC_BASE` MSR and the
  spurious vector register. x2APIC mode is detected and rejected with a clear
  message, since its registers are not memory mapped.
- **I/O APIC** (`ioapic.c`): two visible registers, an index and a data
  window. All inputs start masked. `ioapic_route` writes a redirection entry:
  vector, polarity, trigger mode, destination CPU.

`arch_irq_register(irq, handler, context)` stores the handler first and
unmasks the line second, because the interrupt may fire immediately.

### End of interrupt

`irq_dispatch` signals end-of-interrupt to the local APIC *before* calling
the handler. Until the EOI the APIC withholds further interrupts of the same
or lower priority; forgetting it makes the system appear frozen. Doing it
first matters from Phase 4 on, when the timer handler may switch threads and
not return for a long time. The handler still cannot be re-entered:
interrupts are disabled on this CPU until `iretq`.

Spurious interrupts (vector `0xFF`, and the PIC range) are ignored without an
EOI, as the specification requires.

## Timer

The local APIC timer counts down from a programmed value at a rate derived
from the CPU's bus clock, which differs between machines. It has to be
measured against something with a known rate: the PIT, 1 193 182 Hz on every
PC.

`measure_timer_rate` lets both run, samples both counters twice, and divides
the differences. The result programs the timer in periodic mode for
`TIME_TICK_HZ` = 1000 interrupts per second. Three runs are made and the
median is used.

### A calibration bias, found and fixed

The first version waited for the PIT to expire and read the APIC count.
Under QEMU its result was 0.15% to 0.96% too high.

Varying the measurement length showed the error was inversely proportional
to it (about 0.8% at 5 ms, 0.3% at 20 ms, 0.13% at 49 ms). That signature
means a fixed delay, about 50 microseconds, not a rate error. The cause: the
"start" and "end" readings were taken by different code paths, so the
unavoidable gap between reading one counter and the other was not the same
at both ends.

The fix takes both samples with the same function (`sample_after`), so the
gap cancels in the subtraction. The measured clock is now within 0.1% of
QEMU's nominal 1 GHz.

## Time base

`kernel/core/time` counts ticks in an atomic 64-bit counter written only by
the timer interrupt. `time_now_ms()` is derived from it. `time_wait_ms`
sleeps with `arch_cpu_idle()` until the deadline; it blocks the whole kernel
and is a stop-gap until threads exist.

`arch_cpu_idle` is `sti; hlt`. `sti` takes effect after the next
instruction, so an interrupt cannot arrive between enabling and halting; if
it could, that wake-up would be lost.

## Keyboard

Three layers:

| Layer | File | Role |
|---|---|---|
| ports | `arch/x86_64/ps2_ports.c` | reach the controller's two registers |
| controller | `kernel/drivers/ps2/i8042.c` | configure the 8042, move bytes |
| keyboard | `kernel/drivers/keyboard/` | scancodes to key events, queue |

**Controller.** `i8042_init` disables both ports, flushes stale bytes (a key
pressed in the boot menu would otherwise be decoded as half of a later
scancode), and enables scancode translation to set 1. Attaching the keyboard
registers IRQ 1, then enables the interrupt in the controller and the port.
The mouse port stays disabled until Phase 7.

**Scancodes.** In set 1 a press sends one byte and the release the same byte
with bit 7 set; newer keys send the prefix `0xE0` first. `ps2_keyboard.c`
keeps that small state machine and calls `keyboard_report(key, pressed)`.

**Events.** `keyboard.c` is device independent. It tracks modifiers
(caps lock toggles on press; shift undoes caps lock for letters), looks up
the character for the US layout, and puts a `struct key_event` into a
64-entry ring buffer protected by a spinlock. When the buffer is full the
newest event is dropped. `keyboard_read_event` never blocks.

## After boot

With no scheduler yet, `kmain` ends in `idle_loop`: sleep until an
interrupt, log any key events, log an uptime line once per second.

## Checks

| Check | What it proves |
|---|---|
| `timer` | 1000 ticks take 1 s (within 1%) by the ACPI PM timer, a counter independent of both the APIC timer and the PIT used to calibrate it |
| `keyboard` | keys typed into the guest arrive as the right events: plain, shifted, enter, and an extended scancode |

The keyboard check needs input from outside. `tools/check-serial.sh` can run
a *sidecar* command next to QEMU; for this check it is
`tools/qemu-sendkeys.py`, which connects to QEMU's machine protocol (QMP),
waits for the kernel's "ready for input" line in the serial log, and sends
`a`, `shift-b`, `ret`, `up`.

## Known limits

- One CPU, one I/O APIC, xAPIC mode only.
- The tick is the only clock: resolution is 1 ms.
- Under software emulation (no KVM) the timing check varies by a few tenths
  of a percent from run to run.
- No test triggers an NMI.
