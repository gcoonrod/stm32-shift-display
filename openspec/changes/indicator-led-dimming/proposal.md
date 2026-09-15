## Why

The three indicator LEDs are driven `HIGH` or `LOW` and nothing else. At 3.3 V through
220 Ω they sit at roughly 5 mA — fine on a desk in daylight, glaring in a dark bedroom,
which is where a clock actually lives. There is no way to turn them down.

The alarm's firing indication has the same bluntness: `update_leds()` picks the LED
state from `blink_on()`, a hard on/off square wave. It reads as an error light rather
than an alarm.

Board rev 2 remapped these pins specifically so this would be possible, and nothing has
ever used it. From the variant's `PinMap_TIM`:

```
D1  PB6  TIM4_CH1
D2  PB7  TIM4_CH2      one timer, three independent duty cycles
D3  PB8  TIM4_CH3
```

The intent is even still in the source as dead code — `AlrmLEDTim` and the commented-out
`setPWM(botChannel, LED_BOT, 60, 10)` in `setup_user_leds()` were an attempt at exactly
this, abandoned before it worked.

## What Changes

- Drive D1–D3 from TIM4 PWM instead of `digitalWrite`, replacing the on/off calls in
  `update_leds()` with duty cycles.
- **One brightness level for all three indicators**, adjustable from the menu and
  persisted alongside the existing settings. The indicators get their own level rather
  than following the display: a single LED and a seven-segment digit do not look equally
  bright at equal duty, so one control cannot serve both.
- Map the user-visible levels through a gamma curve. Perceived lightness goes roughly as
  luminance^(1/2.2), so evenly spaced duty values feel bunched at the bottom and flat at
  the top.
- **The alarm breathes while firing** — a smooth fade up and down replacing the current
  square-wave blink. Armed stays a steady lit D1: the two states remain unmistakably
  different, and a light that pulses all night beside a bed is its own problem.
- Serial get/set for the indicator level, consistent with the existing settings commands.
- Consume the dead `AlrmLEDTim` global and the commented-out PWM block in
  `setup_user_leds()`, which this change finally implements.

Deliberately unchanged: which LED means what. D3 stays AM-in-12-hour-mode, D2 stays the
12-hour indicator, D1 stays the alarm. Only how they are driven changes.

## Capabilities

### New Capabilities
_None._

### Modified Capabilities
- `clock-ui`: "The LEDs indicate mode and alarm state" gains brightness — the LEDs are
  no longer simply lit or unlit — and the capability gains a requirement that indicator
  brightness is user-settable and persisted.
- `alarm`: "Firing is signalled visibly and can be dismissed" changes its scenario for
  the LED from blinking to breathing. The requirement that firing stay visually distinct
  from armed is unchanged and is what the breathing has to preserve.

## Impact

- **Firmware**: `firmware/src/main.cpp` — `setup_user_leds()` (timer setup), `update_leds()`
  (duty instead of level), the settings struct and its backup-register layout, the menu
  tree and editors, and the serial commands.
- **Timer**: claims TIM4. The generic variant defines `TIMER_SERVO TIM4`, which matters
  only if the Servo library is ever used — it is not, and nothing else in this firmware
  touches a timer.
- **Backup domain**: one more value to persist. DR2, DR3 and DR5 are in use; DR8 and DR9
  are free. DR1, DR4, DR6, DR7 and DR10 remain off limits (core and RTC library).
- **Flash**: 74.2% of 64 KB used, leaving about 17 KB. A gamma table and a breathing
  curve are small, but the budget is no longer roomy and should be checked at each stage.
- **Coordination with `display-dimming`**: both changes add a brightness level, a menu
  entry, a persisted setting and a gamma mapping. They touch the same structures, so
  whichever lands second inherits the first one's shape. Landing this one first is the
  lower-risk order — it establishes the level-and-gamma pattern on three simple GPIOs
  before the same pattern is applied to the display's output-enable line.

## Not in scope

- Display brightness — that is `display-dimming`, on a different timer and a different
  pin.
- Per-LED brightness. One level covers all three; three separate levels would mean three
  more settings and three more menu entries for little gain.
- Any change to what the indicators mean, or to the alarm's timing, arming or dismissal.
