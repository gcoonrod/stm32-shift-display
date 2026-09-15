## Context

`update_leds()` is four lines of `digitalWrite`, called once per loop from the
authoritative state. That structure is already right — a single writer owning all three
pins — and it is the reason this change is mostly a substitution rather than a rework.
What it writes is the problem: full on or full off, with the alarm's firing state picked
from `blink_on()`, the same square wave that drives the field-edit blink.

The pins were chosen for this. From the variant's `PinMap_TIM`:

```
D1  PB6  TIM4_CH1  ┐
D2  PB7  TIM4_CH2  ├─ one timer, three independent channels
D3  PB8  TIM4_CH3  ┘
```

The intent survives in the source as dead code: `HardwareTimer *AlrmLEDTim;` and the
commented-out `setPWM(botChannel, LED_BOT, 60, 10)` in `setup_user_leds()`. This change
is what that was reaching for.

Constraints worth stating up front. Flash is at **74.2% of 64 KB**, about 17 KB free —
comfortable three changes ago, not any more. There is one board and no test suite, so
anything not checkable over serial has to be checked by eye. And the display's own
brightness is a separate change on a separate timer; the two only share the shape of the
problem.

## Goals / Non-Goals

**Goals:**
- Indicator LEDs driven by hardware PWM, at a brightness the user chooses.
- A firing alarm that breathes instead of blinking.
- Keep the LED state observable from a host, because that is how the alarm was verified
  last time and will be again.

**Non-Goals:**
- Display brightness (`display-dimming`, TIM2, `~OE`).
- Per-LED brightness levels.
- Changing what any indicator means, or anything about alarm timing, arming or dismissal.
- Night scheduling.

## Decisions

### Use `analogWrite()` rather than driving `HardwareTimer` directly

The core resolves `analogWrite(pin, duty)` through `PinMap_TIM` to the right timer and
channel, so PB6/PB7/PB8 land on TIM4_CH1/CH2/CH3 without naming TIM4 anywhere. On core
3.0.0 the defaults are `PWM_RESOLUTION 8` and `PWM_FREQUENCY 1000` — 256 duty steps at
1 kHz, which for three indicator LEDs is ample. Flicker is not a concern at 1 kHz, and
256 steps is more than a gamma-mapped handful of user levels needs.

*As built*: 256 steps turned out to be ample for the **levels** and not for the
**breath**. Steady indicators only ever sit on one of eight gamma-mapped values, but a
fade interpolates between them, and at the dim end one 8-bit duty step is a large
fraction of the light output — duty 4 to 5 is a 25% jump. The first build staircased
visibly. Raising the indicators to 12 bits with `analogWriteResolution(12)` (the core caps
at `MAX_PWM_RESOLUTION 16`) took a level-6 breath from 95 distinct duty values to 439, and
the levels were recomputed on the same gamma curve so brightness is unchanged. The
assumption was right about what it was checked against and wrong about what came later.

Driving `HardwareTimer` directly would buy a configurable frequency and finer resolution,
at the cost of managing the timer object, the channel lookup via
`pinmap_peripheral`/`STM_PIN_CHANNEL`, and its lifetime. That is what the abandoned
`AlrmLEDTim` code was doing, and there is nothing here that needs it.

*Alternatives considered:* `HardwareTimer` with `setPWM` (more control, more machinery,
and the dead stub is evidence of how it goes); software PWM from the main loop (burns CPU
and jitters with whatever else the loop is doing).

### Gamma-map the user levels

Luminous output is linear in duty; perceived lightness is not, going roughly as
luminance^(1/2.2). A linear ramp of duty puts almost all of the apparent change in the
bottom few percent and makes the top half look flat.

So the user picks from a small number of levels — eight is a reasonable starting point —
and a constant lookup table maps each to a duty value on a curve. A table beats computing
`pow()` at runtime on a Cortex-M3 with no FPU, and at eight entries it costs eight bytes.

### Breathing is a waveform on the same duty path, not a separate mechanism

Breathing is computed in `update_leds()` from `millis()`: a phase across a period of
roughly two to three seconds, shaped into a smooth rise and fall, scaled so the peak is
the configured indicator brightness and the trough is near zero.

The fade needs the **same gamma correction as the levels**, which the first build missed:
the levels were mapped through the curve but the breath interpolated linearly in duty
between them, so it appeared to race through the dim end and crawl at the top. The
smoothstep is followed by a squaring step for the same reason the level table exists.

Two properties matter. It needs no interrupt — the main loop already runs fast enough to
update a duty value smoothly, and unlike the RTC tick there is nothing to synchronise to.
And it **scales to the configured brightness rather than overriding it**: an alarm that
breathes to full brightness when the user has set the indicators dim would undo the
setting exactly when it is least welcome.

The armed indicator stays steady. Steady-versus-breathing is what distinguishes the two
states, and a pulsing light beside a bed all night is the thing people take the batteries
out over.

*Alternatives considered:* a timer-driven fade (an ISR for something the loop can do); a
triangle wave (cheaper, but the linear ramp reads as mechanical next to a sine-like
curve); breathing the armed state too (rejected above).

### `GL` reports duty, not a logical level

`cmd_get_leds()` currently returns `digitalRead()` of each pin. Once a pin is in
alternate-function mode driven by a timer, `digitalRead` samples the live PWM waveform at
whatever phase the call lands on — it would return noise that looks like a flickering
indicator.

`GL` therefore reports each LED's **duty value** instead. "Lit" becomes "non-zero", which
keeps the existing `serial-command-console` scenarios true, and it preserves something
this change would otherwise destroy: the alarm's firing state stayed observable from a
host last time precisely because `GL` changed while the LED blinked. A breathing LED read
repeatedly shows a varying duty, so that verification path survives.

`GL` is not one of the five commands the `serial-command-console` spec pins as
byte-for-byte preserved, so changing its reply is permitted — but it is a documented
contract and `docs/DEVELOPMENT.md` has to change with it.

### Persist the level in DR8, packed for the display change to share

DR2, DR3 and DR5 hold the magic, flags and alarm. DR1, DR4, DR6, DR7 and DR10 belong to
the core and the RTC library and are off limits. DR8 and DR9 are free.

The indicator level goes in the **low byte of DR8**, leaving the high byte for
`display-dimming` to take without needing another register or a migration. Both are
brightness levels; one register for both is the layout they would have been given had the
two changes been designed together.

## Risks / Trade-offs

- **`digitalWrite` silently kills PWM.** Writing a PWM'd pin with `digitalWrite`
  reconfigures it back to plain output and the timer stops driving it. `update_leds()`
  being the only writer is the protection, and it should stay the only writer — including
  `setup_user_leds()`, which currently does `pinMode` plus `digitalWrite(LOW)` and should
  establish the off state through the same path.
- **Flash budget.** 17 KB free, and `analogWrite` pulls in PWM machinery this firmware has
  not linked before. Check the size after the first build rather than at the end, so an
  unpleasant number is attributable.

  *As built*: the substitution alone cost **3,892 bytes**, taking flash from 74.2% to
  80.1%. `analogWrite` reaches `pwm_start` which drags in the whole `HardwareTimer` class
  and its HAL support — roughly 4.4 KB including input capture, complementary outputs and
  the timer IRQ handler, none of which this firmware uses. It is one-time infrastructure:
  `display-dimming` gets PWM on TIM2 for almost nothing on top. If flash ever gets tight,
  configuring TIM4 and TIM2 through LL registers directly would recover most of that
  4.4 KB, at the cost of hand-rolled register code — the escape hatch, not the starting
  point.
- **TIM4 is `TIMER_SERVO`** in the generic variant. Harmless — the Servo library is not
  used and nothing else here touches a timer — but it is the kind of thing that surprises
  someone later, so it belongs in the docs.
- **Breathing could mask the armed state.** If the breath's trough sits at zero and the
  period is long, a glance at the wrong moment shows a dark D1 during an alarm. Keeping
  the trough slightly above zero avoids the display reading as "not armed" mid-breath.
- **Eight levels may be the wrong number.** It is a guess. The table makes it a one-line
  change, and the right number will be obvious within a minute of using it.

## Migration Plan

1. Move `update_leds()` to `analogWrite` at a fixed full duty; confirm no visible change.
2. Add the level setting, its persistence and the gamma table; confirm over serial and by
   eye.
3. Add the menu entry with live preview.
4. Replace the firing blink with the breathing waveform.
5. Change `GL` to report duty; update the docs.

**Rollback:** the change is confined to `main.cpp` and documentation. Reverting restores
`digitalWrite` indicators; re-flash over SWD.

## Open Questions

- How many levels, and what gamma exponent? Eight and 2.2 are the starting point; both
  are one-line changes once they have been looked at.
- What breath period reads as calm rather than urgent? Two to three seconds is the
  proposal. An alarm arguably wants urgency, which would argue shorter — worth trying
  before fixing.
- Should the indicators dim while the menu is open, so the display is easier to read? A
  small touch, easy to add later, and not worth complicating this change for.
