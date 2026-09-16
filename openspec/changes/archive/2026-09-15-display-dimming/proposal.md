## Why

The display runs at one brightness: full. Six digits are driven statically — six
74HC595s, 48 segment resistors, every lit segment conducting continuously — so a clock
showing `12 34 56` has upwards of twenty segments on at all times. On a desk that is
right. At 3am it is a lamp.

The hardware was built for this. `~OE` is on PA0, which is `TIM2_CH1`, and TIM2 is
entirely unclaimed (the variant reserves TIM3 for `tone()` and TIM4 for `Servo`). The
netlist shows `~OE` as a single net reaching all six registers:

```
        PA0 ──┬── R12 10k ── VCC        pull-up: blanked through reset
              │
              ├──▶ U6.13  ~OE  digit 1
              ├──▶ U7.13  ~OE  digit 2
              ├──▶ U10.13 ~OE  digit 3
              ├──▶ U11.13 ~OE  digit 4
              ├──▶ U14.13 ~OE  digit 5
              └──▶ U15.13 ~OE  digit 6
```

That topology makes PWM dimming close to an ideal case rather than a compromise. On a
multiplexed display, dimming fights the refresh scan — modulating a signal that is
already chopped, producing beats and uneven digits. Here there is no scan to fight.
Duty becomes the only variable and the instantaneous segment current stays at its design
point at every brightness, so the LEDs never drift toward the low-current region where
efficiency and colour go strange. `~OE` to output on an HC595 is tens of nanoseconds,
four orders of magnitude faster than the shortest duty step worth using, so the shift
registers are nowhere near the limit.

The dead `irq_timer_led()` in `main.cpp` is a software attempt at the same idea —
toggling `display.enable()`/`disable()` from a timer callback that nothing ever calls.
The hardware can do it properly.

## What Changes

- Drive `~OE` from TIM2_CH1 PWM, giving global display brightness across all six digits.
- **A brightness level in the menu**, persisted like the other settings, with the display
  itself as the live preview while adjusting — the one setting whose effect you can see
  as you change it.
- Gamma-mapped levels, for the same perceptual reason as the indicator LEDs.
- **A minimum brightness floor.** The lowest selectable level stays legible; a clock
  dimmed to nothing reads as a dead clock, and someone will conclude the board has
  failed.
- PWM frequency in the low kilohertz. Flicker, not resolution, is the constraint: at
  72 MHz even 10 kHz leaves 7,200 duty steps, far more than the handful of levels a user
  will ever see, whereas a few hundred hertz on a bright point source in a dark room is
  exactly where eye movement makes flicker visible.
- **`ShiftDisplay::enable()`/`disable()` become brightness operations.** They currently
  `digitalWrite` PA0, which once TIM2 owns the pin in alternate-function mode would
  reconfigure it back to plain GPIO and silently stop the PWM. Every caller has to go
  through the new path.
- Serial get/set for the brightness level.
- Consume the dead `irq_timer_led()` hook, which this change replaces with hardware.

## Capabilities

### New Capabilities
_None._

### Modified Capabilities
- `clock-ui`: "The display shows the current time at rest" gains brightness as a property
  of the display, and the capability gains a requirement that display brightness is
  user-settable, persisted, and floored above invisibility.
- `alarm`: only if the firing flash moves onto `~OE` — see below. If it stays
  buffer-based, `alarm` is untouched. *(Resolved in design: it stays buffer-based,
  so `alarm` is untouched.)*
- `serial-command-console`: *added during implementation.* Gains a requirement that
  the raw settings storage, including what start-up read, is reportable over serial —
  added after a second unreproduced report of settings returning to defaults, which
  cannot be diagnosed after the fact without it.

## Impact

- **Firmware**: `firmware/lib/ShiftDisplay/` (`enable()`/`disable()` semantics, timer
  ownership), `firmware/src/main.cpp` (timer setup, settings, menu, serial).
- **Timer**: claims TIM2. Note that PA0–PA3 are TIM2_CH1–CH4 and those are the `~OE`,
  `~SRCLR`, `RCLK` and `SER` lines — TIM2 is the shift-register timer by pin allocation,
  but only CH1 is wanted and the other channels staying unconfigured leaves those pins as
  ordinary GPIO.
- **Backup domain**: one more persisted value. DR8 and DR9 are free; DR1, DR4, DR6, DR7
  and DR10 are spoken for.
- **Flash**: about 17 KB free at 74.2% used.
- **Failsafe is already correct** and should stay that way: R12 pulls `~OE` high whenever
  PA0 is high-impedance, so the display is blank from power-on until firmware drives it,
  rather than showing whatever garbage the registers powered up with.

## Open question for the design

The alarm currently flashes by rewriting the six-character buffer and re-shifting 48
bits. With `~OE` as a brightness knob, flashing could instead be a duty change — cheaper,
and it would blank the decimal point too, which the buffer approach does not.

But the field-edit blink **cannot** move: it blanks only the field being edited, and
`~OE` is all-or-nothing across the whole display. So taking the cheap win for the alarm
means carrying two different blanking mechanisms. That tradeoff belongs in design.md, not
here.

## Not in scope

- Indicator LED brightness — that is `indicator-led-dimming`, on TIM4.
- Automatic dimming by time of day. A night schedule is a policy layered on top of this
  mechanism, and it needs this manual level to exist first: a schedule is just something
  that chooses between two of these values. Worth doing, worth doing separately.
- Per-digit or per-segment dimming, which the single `~OE` net makes impossible without
  cutting traces.
