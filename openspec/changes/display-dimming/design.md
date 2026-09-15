## Context

The display runs at one brightness because `~OE` is a GPIO that `ShiftDisplay::enable()`
drives low once at startup and never touches again. Everything needed to change that is
already in place:

```
        PA0 ──┬── R12 10k ── VCC        pull-up: blank through reset
   TIM2_CH1   │
              └──▶ ~OE on U6, U7, U10, U11, U14, U15   (one net, six registers)
```

Six 74HC595s drive 48 segment resistors statically — no multiplexing, so there is no
refresh scan for PWM to beat against. Duty is the only variable and segment current stays
at its design point at every brightness. `~OE` to output is tens of nanoseconds, four
orders of magnitude faster than the shortest duty step worth using.

Three things changed under this design's feet when `indicator-led-dimming` landed, and
they shape it:

- **The timer machinery is already linked.** `analogWrite` cost 4.4 KB there. Reusing it
  here is nearly free; the alternative of hand-rolled LL register code would now *save*
  nothing, because the 4.4 KB is already spent.
- **`analogWriteResolution` and `analogWriteFrequency` are global**, not per-pin. The
  indicators run at 12 bits. The display cannot pick its own resolution independently, so
  whatever is chosen has to suit both.
- **Flash is at 81.1%**, about 12.1 KB free. This change should be small, but the margin
  is no longer generous.

The glyph table has no `I` or `S`, so a `dISP` label cannot be rendered today.

## Goals / Non-Goals

**Goals:**
- Global display brightness on TIM2_CH1, adjustable, persisted, floored above illegibility.
- `~OE` under one owner, so nothing can silently stop the PWM.
- No change to what the display shows, only to how brightly.

**Non-Goals:**
- Indicator brightness (done, separate timer, separate level).
- Automatic dimming by time of day — a schedule is a policy that picks between two of
  these levels, and it needs this to exist first.
- Per-digit brightness, which the single `~OE` net makes impossible.

## Decisions

### `ShiftDisplay` owns `~OE`, including the inversion

The pin moves inside the class. `ShiftDisplay` gains a brightness setter, and
`enable()`/`disable()` become operations on it — `enable()` restores the configured
brightness, `disable()` sets zero — rather than `digitalWrite`s. Callers never see the pin.

This is where the active-low inversion lives, and it is worth stating plainly because it
is the single easiest thing to ship backwards: `~OE` **high** blanks the display, so the
duty written to the pin is the complement of the brightness. A caller asking for "20%
brightness" must not be the one remembering to write 80%.

It also puts the trap in one place. A stray `digitalWrite(OEB, …)` anywhere would
reconfigure PA0 from alternate-function back to plain output and stop TIM2 driving it —
the same failure the indicator change had to design around, with the difference that here
it blanks the entire display rather than one LED.

*Alternatives considered:* leaving the pin in `main.cpp` and having `ShiftDisplay` keep
its `digitalWrite` calls (guarantees two owners); a free function wrapping the pin (same
thing without the encapsulation).

### Reuse `analogWrite`, and raise the shared frequency to 4 kHz

Resolution and frequency are global to `analogWrite`, so the display and the indicators
share both. Twelve bits already suits the indicators and is ample here. Frequency is the
one that needs moving: the core's 1 kHz default is probably fine, but a bright
high-contrast source at low duty seen at the edge of vision is exactly where PWM flicker
gets noticed, and a clock is looked at sideways constantly.

4 kHz costs nothing. The timer's period register still has far more counts than the duty
resolution needs — at 72 MHz the reload is about 18,000 counts at 4 kHz against 4,096
duty steps — so resolution is untouched. Resolution only starts to suffer above roughly
17 kHz, where the reload would fall below 4,096. The indicators are unaffected by the
change; 4 kHz is as invisible to an LED as 1 kHz.

*Alternatives considered:* leaving it at 1 kHz (likely fine, no headroom, and a flicker
complaint would be expensive to diagnose); a separate `HardwareTimer` for TIM2 so the
display could set its own frequency (more machinery to sidestep a shared setting that
suits both).

### The alarm flash and the field blink both stay buffer-based

`~OE` could flash the display far more cheaply than rewriting the six-character buffer and
re-shifting 48 bits, and it would blank the decimal point too, which the buffer approach
does not.

It still should not. The field-edit blink **cannot** move — it blanks only the field being
edited, and `~OE` is all-or-nothing across all six digits. Moving just the alarm flash
would leave two blanking mechanisms for one concept, and the next person would have to
work out which applied where. The cost being avoided is a 48-bit shift three times a
second, which is not a cost. `~OE` means brightness and nothing else.

*Alternatives considered:* moving the alarm flash to `~OE` (cheaper, but splits blanking
in two); moving both (impossible — partial blanking has no `~OE` equivalent).

### A firing alarm flashes at the configured brightness

Consistent with the decision already made for the breathing indicator: the alarm does not
override brightness to full. Someone who dimmed the clock did so for the hours the alarm
fires in, and an alarm that undoes that setting at 3am undoes it at precisely the wrong
moment. The flash remains unmissable because it is the whole display going on and off,
not because it is bright.

### Add `I` and `S` so the menu can say `dISP`

The label needs to distinguish display brightness from the indicators' `LEd` entry, and
the current vocabulary — `0-9 A-H L n o P r t U d -` — cannot spell anything that reads as
"display". Both letters render acceptably on seven segments: `I` as the two left verticals,
`S` as the same shape as `5`, which is a universal seven-segment compromise and reads
correctly in a word.

They cost nothing: the table already reserves zeroed slots across the whole ASCII range,
so defining them changes no sizes. The label-audit script written during the last change
should be run over the label set afterwards — that is how the missing `d` would have been
caught before it reached the board.

*Alternatives considered:* a label built from existing glyphs (`dULL`, `LEUEL`) — all of
them either misleading or ambiguous with the indicator entry.

*Folded in during implementation*: `I` was first written as `0b00011000`, which is D|E, not
the E|F the comment claimed. That made three glyph bugs written as binary literals — a
missing entry, a stray decimal point, and one built from the wrong two bits — so the table
was rewritten in terms of named `SEG_A`..`SEG_G` constants. `SEG_E | SEG_F` is either right
or obviously wrong; `0b00011000` is neither, and the compiler accepts any eight bits it is
given. `'B'` was corrected at the same time: it was missing segment C and was byte-identical
to `'T'`. The table was regenerated from the verified decode rather than retyped, and diffed
byte-for-byte against the previous values to confirm exactly one intended change. Flash is
unchanged, as the constants fold.

### Persist in DR8's high byte, on the same gamma curve with its own floor

The low byte holds the indicator level; the high byte was left for exactly this, so no
migration is needed. The gamma curve is the same — the perceptual maths does not care what
is being dimmed — but the **floor differs**: the indicators' lowest level only has to stay
visible, while the display's lowest has to stay *legible*, which is a higher bar and is
settled by eye on the board, not by arithmetic.

## Risks / Trade-offs

- **Shipping the inversion backwards.** Brightness 1 would produce a nearly-full display
  and brightness 8 a nearly-blank one, and at the extremes it looks plausible enough to
  miss. The serial read-back makes this checkable from a host before anyone squints at it.
- **A brightness of zero looks like a dead clock.** The floor is a requirement, not a nicety,
  and it has to be verified on the board in a dark room rather than reasoned about.
- **Flash.** 12.1 KB free. This change adds a setting, a menu entry, two commands and a
  handful of glyph bytes on top of machinery that is already linked, so it should be a few
  hundred bytes — but that is a prediction, and the first build should be measured against
  it.
- **The shared `analogWrite` frequency couples two features.** Changing it for the display
  changes it for the indicators. Harmless now, and worth a comment so it is not discovered
  by someone tuning one and puzzled by the other.
- **Nothing verifies brightness end-to-end but an eye.** Serial can confirm the duty that
  was requested; only a person can confirm the display is legible at level 1 and not
  painful at level 8.

## Migration Plan

1. Move `~OE` into `ShiftDisplay` as a brightness value with the inversion inside, keeping
   the current behaviour at full brightness; confirm nothing changes on the board.
2. Raise the shared PWM frequency; confirm the indicators still behave.
3. Add the level setting, its persistence and the gamma mapping; confirm over serial.
4. Add the `I` and `S` glyphs and the menu entry, with the display previewing itself.
5. Add the serial commands.
6. Verify the floor by eye, then the flicker, then a power cycle.

**Rollback:** confined to `ShiftDisplay`, `main.cpp` and documentation. Reverting restores
a permanently-on `~OE`; re-flash over SWD.

## Open Questions

- How many levels, and what floor? Eight matches the indicators, and the floor is whatever
  is still legible in the dark — both settled on the board.
- Should the menu dim the indicators while it is open, so the display is easier to read
  against them? Deferred once already; still not worth complicating either change.
- Does 4 kHz actually buy anything over 1 kHz here? Unfalsifiable without someone who can
  see flicker at 1 kHz. It costs nothing, so it is taken as insurance rather than as a
  fix for an observed problem.
