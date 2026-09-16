## Why

Brightness today is global because the hardware makes it so: `~OE` is a single net across
all six shift registers, so TIM2's PWM dims every digit together. There is no wire that
would let one digit be dimmer than another.

But brightness does not have to come from `~OE`. If the segment data is re-shifted fast
enough, a digit lit in three refreshes out of eight is three-eighths as bright as one lit
in all eight — brightness as a property of the *data* rather than the output-enable line.
That gives per-digit and per-segment control that the wiring cannot.

The obstacle is cost. Data-path dimming needs the display re-shifted continuously at
kilohertz rates, and doing that from the CPU means spending the loop on it forever.

DMA removes the obstacle entirely. `BSRR` takes a 32-bit word whose low half sets pins and
whose high half clears them, so an entire shift waveform can be precomputed as an array of
words and replayed by a DMA channel on a timer's tick, with the CPU uninvolved:

```
RAM buffer, one BSRR word per edge:

  [ SER set,   SRCLK clear ]   present the bit
  [ SRCLK set              ]   rising edge clocks it in
  [ SER clear, SRCLK clear ]
  [ SRCLK set              ]
  ... 96 words for 48 bits ...
  [ RCLK set ] [ RCLK clear ]  latch

  TIM1 update ──▶ DMA1 Ch5 ──▶ GPIOA->BSRR
    every N ticks   one word per event
```

In **circular** mode the buffer replays forever. The display stops being something the CPU
draws and becomes something that refreshes itself.

All seven DMA1 channels are free — the only DMA symbols in the current image are two-byte
default interrupt stubs. TIM1 and TIM3 are unused; TIM2 and TIM4 belong to display
brightness and the indicator LEDs and stay untouched.

## What Changes

- A timer-driven, circular DMA channel replays a precomputed `BSRR` waveform to GPIOA,
  refreshing the display continuously with no CPU involvement.
- The waveform holds **N time-slices**. A digit's brightness is how many slices it appears
  lit in, giving per-digit — and in principle per-segment — brightness.
- The timer period sets the shift clock **exactly**, which also settles the 74HC595-at-3.3 V
  timing question far better than instruction counting can: the rate is a number you choose
  rather than a consequence of what the compiler emitted.
- Global `~OE` brightness stays as it is. Per-digit brightness multiplies with it rather
  than replacing it.
- The first use is the one that prompted this: **dimming the leading hour digit in 12-hour
  mode** rather than blanking it.

## What this costs

The numbers are the argument, in both directions.

For a flicker-free cycle the whole slice set must repeat well above the eye's threshold.
At 8 slices and roughly 100 words per full shift, a 1 kHz cycle rate needs

```
8 slices × 100 words × 1000 Hz  =  800,000 words/second
                                =  ~400 kHz shift clock   (comfortably within the parts)
                                =  3,200 bytes of RAM      (8 × 100 × 4)
```

3.2 KB against 20 KB total, on top of the 4.9 KB already used — call it 39% of RAM. Four
slices halves it at the cost of coarser steps.

Against that: the CPU cost of refreshing the display drops to zero, and stays zero no
matter how fast it refreshes.

## Capabilities

### New Capabilities
_None._

### Modified Capabilities
- `clock-ui`: "Display brightness is user-settable" currently states that brightness is a
  single level for the whole display because the output-enable line is shared. That
  reasoning stops being the whole story once brightness can also come from the data path,
  so the requirement needs to say how the two interact. The capability also gains per-digit
  brightness, and a requirement that the display refreshes without CPU involvement.

## Impact

- **Firmware**: `firmware/lib/ShiftDisplay/` substantially — the driver stops shifting bits
  and starts maintaining a waveform buffer. `firmware/src/main.cpp` where brightness and
  rendering meet.
- **Peripherals**: claims a timer (TIM1 or TIM3) and a DMA1 channel. Neither is used today,
  and TIM2/TIM4 are deliberately left alone.
- **RAM**: 1.6–3.2 KB depending on slice count, against 15.6 KB free.
- **Risk**: this is the largest single change contemplated for this firmware. It replaces a
  working display driver with a peripheral configuration, and DMA faults are harder to
  diagnose than a wrong `digitalWrite`. There is also **no readback**: `U15`'s `QH'` is
  unconnected, so correctness is confirmed by eye.
- **Two PWMs multiplying**: data-path slices and `~OE` gating interact. Beat frequencies
  between the slice rate and the `~OE` rate are the thing to watch, and the reason the two
  rates should not be close or harmonically related.

## Ordering

`bsrr-shift-out` should land first. It establishes the cached port, the cached masks and
the explicit `BSRR` discipline that this engine builds on, and it produces the measured
safe clock rate that this change needs in order to choose a timer period. Doing this one
first would mean guessing that number twice.

## Not in scope

- **Blanking** the leading hour digit, which needs no engine at all — writing a space
  instead of a zero is a few lines in the render path and should not wait for this.
- Any change to what the display shows, beyond the brightness of individual digits.
- Replacing `analogWrite` on `~OE`; global brightness stays on TIM2 exactly as it is.
