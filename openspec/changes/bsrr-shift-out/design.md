## Context

`shiftOutByte` calls `digitalWrite` three times per bit. The disassembly of the shipped
image shows what each call costs: a bounds check, two table lookups, bitfield extraction to
recover the port and pin mask, and finally one `str` to `BSRR`. About 35 cycles to reach a
single-cycle store, and the answer is identical every time — the pins are fixed at
construction.

```
per bit   3 × bl digitalWrite  ~105 cycles
          3 × _delay_us checks   always zero, never taken
          loop overhead
                              ≈ 117 cycles
48 bits                       ≈ 5,600 cycles ≈ 78 µs   (~615 kHz shift clock)
```

Two facts shape the rewrite.

**The pins share a port.** `~OE` PA0, `~SRCLR` PA1, `RCLK` PA2, `SER` PA3, `SRCLK` PA4 are
all GPIOA. That is what makes one `BSRR` write able to present a data bit and drop the
clock together — and it is also why the driver must never write the port as a whole, since
`~OE` is in alternate-function mode under TIM2 and belongs to the brightness control.

**The parts are the limit, not the CPU.** Six 74HC595s on 3.3 V. The HC family is
characterised at 4.5 V, and for a daisy chain the binding constraint is not the rated
`fmax` but the serial propagation delay of each stage plus the next stage's setup time,
repeated six times. An unthrottled `BSRR` loop would clock at roughly 9–12 MHz, which is
plausibly at or past that edge.

And there is no way to check electrically: **`U15`'s `QH'` is unconnected**, so the far end
of the chain returns nothing. Correctness is confirmed by eye or not at all.

## Goals / Non-Goals

**Goals:**
- Replace the per-bit `digitalWrite` calls with direct `BSRR` writes.
- Establish the safe shift rate by measurement on this board, and record it.
- Leave everything the display shows exactly as it is.

**Non-Goals:**
- Timer + DMA, which is `dma-display-engine` and a different idea.
- Per-digit brightness, which needs that engine.
- Blanking the leading hour digit, which needs none of this.
- Hardware SPI, which cannot reach these pins.

## Decisions

### Resolve the port and masks once, in `begin()`

The pin numbers stay constructor parameters and the class API does not change. `begin()`
resolves each to its port and bit, and precomputes the `BSRR` words the inner loop needs:

```
_ser_set / _ser_clr        bit,  bit << 16
_clk_set / _clk_clr
_data1_clklow = _ser_set | _clk_clr     one write presents a 1 and drops the clock
_data0_clklow = _ser_clr | _clk_clr     …and a 0
```

The inner loop then becomes a select and two stores:

```
port->BSRR = (byte & mask) ? _data1_clklow : _data0_clklow;
port->BSRR = _clk_set;                  // rising edge clocks it in
```

Roughly 6–8 cycles per bit against 117.

### Combine data and clock only when they actually share a port

Merging the data and clock into one write assumes they are on the same port. They are on
this board, but the class takes them as parameters and should not silently misbehave if
they ever are not. `begin()` compares the resolved ports and picks the combined path when
they match, falling back to separate `BSRR` writes per pin when they do not. Both paths are
`BSRR`; the fallback is slower, not wrong.

*Alternatives considered:* assuming a shared port and documenting it (works until it
doesn't, and the failure is silent); keeping per-pin writes always (gives up half the win
for a case that does not exist on this hardware).

### Measure the rate with a runtime-tunable build, ship a compile-time constant

The sweep needs the delay to change without reflashing, or finding the edge means a
rebuild per step and a fresh judgement each time. Production wants no runtime variable in
the inner loop at all.

So: the inter-edge delay is a compile-time constant by default, and a build flag swaps in a
runtime-settable version plus a temporary serial command. One flash, then step the delay
down while watching the display, and the failure point is found in a single sitting. The
measured value is then baked in as the constant and the sweep build is not shipped.

*Alternatives considered:* a permanent runtime variable (costs a load and a loop in the hot
path forever, to re-measure something that changes only if the board does); rebuilding per
step (works, but turns one observation into eight round trips).

### Sweep with two patterns, because there are two failure modes

A marginal chain fails in two distinguishable ways, and no single pattern catches both:

- **Dropped or corrupted bits** show as wrong segments. `888888` lights every segment, so
  anything lost is visible immediately.
- **Lost or extra clock edges** shift the whole stream. `888888` would look perfect while
  every digit was in the wrong place. `012345` makes a shift obvious.

Alternating the two covers both. This matters more than usual precisely because there is no
readback: the pattern *is* the instrument.

### Keep the delay hook, drop the dead one

`_delay_us` is currently checked three times per bit and is always zero — it is dead weight
in the hot path, but the *idea* behind it is exactly right and is what this change makes
concrete. It moves out of the inner loop and becomes the deliberate inter-edge delay.
`_delay_ms` is referenced nowhere at all and goes.

## Risks / Trade-offs

- **The failure mode is intermittent and invisible.** A rate that is 95% safe produces a
  wrong segment occasionally, and the natural reaction is to blame something else. This is
  the whole reason the rate is measured rather than chosen, and why the margin is recorded
  rather than assumed.
- **No electrical verification exists.** `U15`'s `QH'` goes nowhere. Every check in this
  change ends at a human looking at six digits. Worth stating plainly rather than implying
  the tests are stronger than they are.
- **The win is invisible when it works.** 78 µs to a handful, once a second. Nothing a user
  could perceive, and the change should be judged on the code and the measured margin
  rather than on any observable improvement.
- **`clear()` and `latch()` share the port too.** They are smaller but have the same
  constraint; converting them keeps one idiom in the driver rather than two.

## Migration Plan

1. Resolve ports and masks in `begin()`; leave the writes as they are. Confirm nothing
   changes.
2. Convert `shiftOutByte`, then `latch()` and `clear()`, to `BSRR`. Confirm the display is
   correct at a deliberately slow rate.
3. Build the sweep variant; find the failure point with both patterns.
4. Choose the rate with margin, bake it in, rebuild without the sweep flag.
5. Exercise the full glyph set in every position at the chosen rate.
6. Record the measurement and the margin.

**Rollback:** confined to `firmware/lib/ShiftDisplay/`. Reverting restores the
`digitalWrite` loop, which is slow and unquestionably safe.

## Open Questions

- ~~What margin is right?~~ Moot, and worth recording as such: no failure was observed, so
  there is nothing to take a margin from. The decision was taken deliberately to ship at the
  unthrottled rate on the grounds that it is trivially changeable later — `SHIFT_EDGE_NOPS`
  is one build flag. The alternative considered was a small throttle as free insurance
  against the temperature and part spread that a single board at room temperature cannot
  test.
- ~~Does the sweep find an edge at all within the achievable range?~~ **No.** Both patterns
  ran clean at 0 NOPs — roughly 7 MHz, the fastest the loop produces — across repeated
  cycles with continuous re-shifting. The constant is zero. The sweep still earned its place:
  it establishes that the edge lies beyond what this implementation can reach, which is a
  measured fact rather than an assumption. What it cannot do is quantify the margin, since
  there was no failure point to measure back from.
- Should the sweep build stay in the repo behind its flag, or be removed once the number is
  known? Keeping it costs nothing at runtime and makes a board revision cheap to re-measure.
