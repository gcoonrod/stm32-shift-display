## Context

The display is driven by six daisy-chained 74HC595s on a single 48-bit chain. Since
`bsrr-shift-out`, the CPU clocks all 48 bits out by hand — two stores per bit into
`GPIOA->BSRR`, roughly 100 stores per redraw — and it does this only when `time_dirty`
says something changed. Brightness comes from TIM2 PWM on `~OE` (PA0), which is one net
across all six registers, so it dims every digit together.

The pins are all on GPIOA:

```
  PA0  ~OE     TIM2_CH1, PWM brightness  -- NOT ours to write
  PA1  ~SRCLR  active low
  PA2  RCLK    latch
  PA3  SER     data
  PA4  SRCLK   bit clock
```

Two constraints follow from that map and shape everything below.

**There is no byte-oriented peripheral available.** SPI1 is SCK/MOSI on PA5/PA7; the
remap puts it on PB3/PB4/PB5, which are the three buttons. `SER` and `SRCLK` are on PA3
and PA4, which are SPI1_NSS and nothing. The board wires the shift registers to plain
GPIO, so the waveform has to be synthesized edge by edge. That is the single fact that
makes this change expensive rather than trivial — an SPI-wired board would need a
16-word buffer and one DMA channel.

**`~OE` shares the port.** Every word the DMA writes to `GPIOA->BSRR` is a word that
could reach across PA0 and blank the display or fight TIM2 for the pin. The existing
"address only the pins you own" rule now has to hold for words computed at build time
and replayed by a peripheral, where nothing in the instruction stream shows the mistake.

## Goals / Non-Goals

**Goals:**

- Refresh the display continuously from a peripheral, with the CPU uninvolved.
- Make per-digit brightness possible by modulating the data rather than `~OE`.
- Set the bit clock to an exact, chosen frequency rather than an emergent one.
- Leave global `~OE` brightness working exactly as it does today.

**Non-Goals:**

- Per-*segment* brightness. The mechanism allows it; nothing asks for it yet.
- Replacing `analogWrite` on `~OE`. TIM2 keeps the pin.
- Changing what the display shows. Only how brightly individual digits show it.
- Dimming the leading hour digit in 12-hour mode. The proposal named this as the first
  use; it has since been dropped. `blank-leading-hour-zero` shipped the blank, the blank
  is the better result, and it is now a `clock-ui` requirement. This change leaves it
  alone.

## Decisions

### Two DMA channels, not one

The obvious construction is one channel and one word per edge: 96 words per pass over
the chain. Half of those words are the same constant — `SRCLK` high — because the rising
edge carries no data.

So: two channels off the same timer, both writing `GPIOA->BSRR`.

| | Trigger | DMA1 ch | Memory increment | Writes |
|---|---|---|---|---|
| Data | TIM1_CH1 compare | 2 | **on** | `SER` set-or-clear \| `SRCLK` clear |
| Clock | TIM1_CH2 compare | 3 | **off** | `SRCLK` set (one constant word) |

The clock channel reads the same word forever, so it costs four bytes regardless of
buffer length. That halves the waveform to **48 words per slice**.

*Alternative considered:* one channel, 96 words per slice. Simpler to reason about and
one less peripheral to configure, but it doubles the buffer — the dominant cost of this
whole change — to save a channel that is otherwise idle. Rejected.

### Latch the previous slice, so latching costs nothing

`RCLK` has to pulse once per 48 bits. Appending latch words to the stream does not work:
the clock channel keeps firing during them, and extra clocks push data off the end of a
chain that is exactly as long as the data.

Instead the `RCLK` bits ride inside the data words. Word 0 of each slice carries
`RCLK` set alongside its data bit; word 1 carries `RCLK` clear. The rising edge at the
start of slice *k+1* latches what slice *k* shifted in.

```
   slice k        slice k+1      slice k+2
  [48 words]     [48 words]     [48 words]
   |              |              |
   |              RCLK^          RCLK^      <- latches the PREVIOUS slice
   |              shows slice k  shows k+1
```

The display runs one slice behind. The stream is circular and never ends, so "one slice
behind" is not a state anything can observe — at 500 Hz it is 2 ms of constant offset.

*Alternative considered:* a third DMA channel on TIM1_CH3 (DMA1 ch 6) pulsing `RCLK`.
More moving parts, another channel, another compare to phase correctly, for a problem
that two spare bits in an existing word already solve.

### The slice period is exactly one `~OE` PWM period

This is the decision that makes the two brightness mechanisms compose instead of fight,
and it is worth stating why the obvious alternatives are worse.

Data-path dimming and `~OE` dimming are two modulations multiplying. If their periods
are unrelated, the phase between them drifts, and any near-coincidence of harmonics
produces a slow beat — 4 kHz `~OE` against a 1001 Hz frame rate gives a visible 4 Hz
flutter. If they are locked but a slice spans only a fraction of an `~OE` period, each
digit's slices land at a fixed `~OE` phase and get a systematically wrong share of the
on-time — a per-digit brightness error that never averages out, which is worse than
flutter because it looks like a hardware fault.

Both problems disappear if **a slice contains a whole number of `~OE` periods**. Then
every slice carries exactly the same `~OE` on-time, whatever the phase, and a digit lit
in *k* of *N* slices is exactly *k/N* as bright. No phase term survives.

The arithmetic lands on one period exactly, which is a pleasant accident of the existing
constants:

```
  ~OE:  4 kHz from TIM2 at 72 MHz          -> 18000 counts per period
  TIM1: ARR+1 = 375, two compares per bit  ->   375 counts per bit
        48 bits per slice                  -> 18000 counts per slice
                                              ^^^^^ equal
```

So with **TIM1 ARR = 374** (375 counts), CH1 compare near 0 and CH2 near 187:

- bit clock **192 kHz** — far below the rate `bsrr-shift-out` ran at without failure
- slice **250 µs**, exactly one `~OE` period
- frame **500 Hz** at N = 8 slices, comfortably flicker-free

Both timers count from the same 72 MHz, so the relationship is exact and permanent
rather than nominal.

The consequence for the code: these numbers must be **derived from `PWM_FREQ_HZ` and the
core clock at build time, not written down**. Someone re-tuning `~OE` frequency for the
LEDs would otherwise silently break the lock, and the symptom would be a per-digit
brightness error nobody would connect to the edit.

### N = 8 slices

Buffer size is `N × 48 × 4` bytes.

| N | Buffer | Frame rate | Brightness steps |
|---|---|---|---|
| 4 | 768 B | 1000 Hz | 4 |
| **8** | **1536 B** | **500 Hz** | **8** |
| 16 | 3072 B | 250 Hz | 16 |

8 is the pick: 1536 B is 7.5% of RAM, taking the build from 4,920 to about 6,456 bytes
(24% → 32%), and 500 Hz is well clear of flicker. 16 doubles the cost and drops the frame
rate to where flicker starts being arguable at the edge of vision. 4 keeps the frame rate
high and the cost low, and is the fallback if 1536 B turns out to be wanted elsewhere; it
is not the default only because four levels is a thin range to demonstrate the mechanism
with.

Note this is a *linear* 8-step scale, not the gamma-mapped 8 the `~OE` levels use. Per-digit
levels will bunch at the top perceptually. That is acceptable for relative dimming of one
digit against the others; it is not a second user-facing brightness control.

### Timer choice: TIM1, with TIM3 as the fallback

TIM1 and TIM3 are both unused, and both can reach two DMA channels:

| | Data channel | Clock channel |
|---|---|---|
| TIM1 | CH1 → DMA1 ch 2 | CH2 → DMA1 ch 3 |
| TIM3 | CH3 → DMA1 ch 2 | CH4 → DMA1 ch 3 |

TIM1 first: it is an advanced-control timer whose compare events are free here (we use no
outputs, so `BDTR.MOE` never comes into it), and its APB2 clock needs no prescaler
reasoning. TIM3 is the drop-in fallback and reaches the same two channels.

**This needs verifying, not assuming.** Some Arduino STM32 core configurations claim TIM1
for the HAL timebase, tone, or servo. A build-time check that the core has not already
taken the timer is a task, not an assumption — and the fallback exists precisely because
the answer might be yes.

### The bit-banged path stays compiled-in behind a flag

DMA faults do not announce themselves. A misconfigured channel produces a blank display
or garbage, with no stack trace and — because `U15`'s `QH'` goes nowhere — no electrical
readback to interrogate. Keeping `shiftOutByte()` and its callers behind a build flag
means a bad configuration is one rebuild from a working clock, and means the two can be
run against the same content and compared by eye.

It also costs almost nothing: the old path is a few hundred bytes of flash against the
28 KB currently free.

## Risks / Trade-offs

**The display is confirmed only by eye** → No readback exists. Verification uses a pattern
where corruption is unmistakable (`888888` with all decimal points, and a per-digit
brightness ramp `1 2 3 4 5 6` where a wrong slice count is obvious), not the time, where a
wrong segment can look like a plausible digit.

**A buffer rewrite can tear a frame** → Updating 48 words while the DMA replays them can
show old data in some slices and new data in others. That lasts one frame — 2 ms — and is
invisible. Double-buffering would cost another 1536 B to fix something nobody can see;
rejected deliberately rather than overlooked.

**`clear()` changes meaning** → Today it pulses `~SRCLR` and latches, and the registers stay
cleared until something writes them. With the engine running, the next frame overwrites the
clear 2 ms later. `clear()` has to become "write spaces into the buffer", and any caller
relying on the old semantics has to be found.

**Someone re-tunes `PWM_FREQ_HZ` for the LEDs and silently breaks the slice lock** → The
timer constants are derived from it rather than written down, and a static assertion fails
the build if the division is not exact.

**A precomputed word reaches PA0** → Every word is built by one helper from the four pin
masks, and a static assertion rejects any word with bit 0 or bit 16 set. The failure this
prevents is a display that goes dark for reasons invisible in the source.

**This replaces a working driver with a peripheral configuration** → It is the largest
single change contemplated for this firmware, and the reason the old path stays behind a
flag. If per-digit brightness turns out not to be worth 1536 B and this complexity, the
revert is a build flag rather than a git operation.

**DMA steals bus cycles from the CPU** → 384k transfers/s against a 72 MHz AHB is on the
order of 5% of bandwidth. The super-loop has no deadline tighter than a one-second RTC
interrupt, so this is noted for completeness rather than as a concern.

## Migration Plan

1. Build the waveform and verify it with the engine stopped — shift one slice out by hand
   through the existing path and confirm the display is identical. This separates "the
   words are right" from "the peripherals are right", which is the hard part of debugging
   DMA by eye.
2. Start the engine with all digits at full brightness. The display should be
   indistinguishable from today.
3. Introduce per-digit levels and exercise them from a test pattern.

Nothing in the clock's normal appearance changes at any step. Rollback is the build flag.

## Open Questions

- **Does the Arduino core already claim TIM1?** Resolved by inspection before any code is
  written; TIM3 is the fallback and needs no other change.
- **Is 8 slices enough to dim a digit attractively?** A level that reads as "dimmer" and
  not as "failing" may want the gamma floor the `~OE` levels use. Answerable only by eye,
  after step 3.
- **Should per-digit brightness persist?** The alarm and mode settings live in backup
  registers. Whether a per-digit level is a user setting or a fixed property of a character
  position is undecided, and deliberately left out of the specs until the feature exists to
  judge.

- **What consumes this?** With the hour digit dropped, the engine has no named user-visible
  feature behind it — its case rests on CPU-free refresh, an exactly specified bit clock,
  and per-digit brightness as a capability. That is a legitimate case, but it should be made
  knowingly rather than inherited from a use that no longer applies.
