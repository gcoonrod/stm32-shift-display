## Why

The display driver shifts 48 bits by calling `digitalWrite` 144 times. From the
disassembly of the shipped image, each call spends about 35 cycles turning a pin number
into a port and bit mask — bounds check, two table lookups, field extraction — before
reaching the single store to `BSRR` that does the actual work:

```
per bit:  3 × bl digitalWrite        ~105 cycles
          3 × _delay_us checks       always zero, never taken
          loop overhead
                                   ≈ 117 cycles
48 bits                            ≈ 5,600 cycles ≈ 78 µs   (~615 kHz shift clock)
```

Every one of those lookups resolves to the same answer every time. The pins are fixed at
construction and never change.

**All five control pins are on GPIOA** — `~OE` PA0, `~SRCLR` PA1, `RCLK` PA2, `SER` PA3,
`SRCLK` PA4 — so a single `BSRR` write can present a data bit and drop the clock together,
atomically, touching only the pins named in its mask.

This buys nothing a user can see. A full refresh happens about once a second, so 78 µs is
roughly 0.02% of the CPU, and nothing is waiting on it. The reason to do it is that it is
thirty lines, it makes the driver honest about what it is doing, and the same groundwork —
cached port, cached masks, explicit `BSRR` — is what a future display engine would need
anyway.

## What Changes

- Cache the GPIO port pointer and the pin bit masks in `begin()`, where they are already
  known, instead of re-deriving them 144 times per refresh.
- Replace the per-bit `digitalWrite` triple with `BSRR` writes.
- Move the `_delay_us` checks out of the inner loop. They are dead in the hot path today
  and they are checked three times per bit.
- **Use explicit `BSRR` masks, never a whole-port write.** `~OE` shares GPIOA and is under
  TIM2's control; a `GPIOA->ODR = ...` style optimisation would reach across it. The
  driver must name the pins it owns.
- **Determine the safe clock rate by measurement rather than by datasheet reading.** Make
  the inter-edge delay a build-time constant, write a known test pattern, and sweep the
  rate until the display garbles. Back off from the measured edge, rather than guessing a
  margin.

## Why the sweep matters

These are plain **74HC595** parts on a **3.3 V** rail. The HC family is characterised at
4.5 V; at 3.3 V both propagation delay and setup time stretch. For a six-deep daisy chain
the binding constraint is not the rated `fmax` but

```
t_PHL(SRCLK → QH') + t_su(next stage SER)  <  clock period
```

because each chip's serial output must settle before the next latches it, six times over.
An unthrottled `BSRR` loop would clock at roughly 9–12 MHz, which is plausibly at or past
that edge.

The failure mode is not a crash. It is *occasional* wrong segments — and
**`U15`'s `QH'` is unconnected**, so there is no electrical readback and no way to detect
corruption except by looking at the display. A fault that shows up rarely and can only be
seen by eye is one that gets blamed on something else.

Sweeping to the failure point costs the same effort as guessing and produces a number
instead of a hope.

## Capabilities

### New Capabilities
_None._

### Modified Capabilities
- `clock-ui`: gains a requirement that the shift-out stays within the shift registers'
  timing limits with a margin established by measurement, and that the display driver
  manipulates only the pins it owns — `~OE` shares its port and belongs to the brightness
  control.

## Impact

- **Firmware**: `firmware/lib/ShiftDisplay/` — `begin()`, `shiftOutByte()`, `latch()`,
  `clear()`. No change to any caller, and no change to what appears on the display.
- **Flash**: expected to shrink slightly; `digitalWrite` stays linked for the buttons and
  LEDs, so this removes call sites rather than code.
- **Risk**: the whole change is invisible when it works and intermittent when it does not.
  A test pattern that makes corruption obvious is part of the work, not an optional extra.

## Not in scope

- Hardware SPI, which **cannot** drive these lines: `SER` is PA3 and `SRCLK` is PA4, while
  SPI1 wants MOSI on PA7 and SCK on PA5, and its remap lands on PB3/PB4/PB5 — the three
  buttons.
- Timer + DMA, which is a different idea with a different purpose; see `dma-display-engine`.
- Per-digit brightness, which needs that engine.
- Blanking the leading hour digit in 12-hour mode, which needs none of this and should be
  done on its own.
