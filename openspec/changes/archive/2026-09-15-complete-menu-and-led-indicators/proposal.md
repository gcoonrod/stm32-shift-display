## Why

The clock keeps accurate time and answers over USB CDC, but the device itself is
barely usable: the only way to set it is from a host running `timesync.py`. The
on-device UI was started and abandoned mid-way — `ShiftDisplayFSM` enumerates a whole
`MenuState` tree (`MENU_SET_TIME`, `MENU_SET_HOUR`, `MENU_SET_DAY`, …) that nothing
can ever reach, because `update()` commits `currentState` and never commits
`currentMenuState`. `handleEvent()` recognises only `kEventClicked`, so the long-press
and repeat features already enabled in `ButtonConfig` do nothing. The PLUS and MINUS
buttons are wired, debounced, and read every loop — and then discarded.

The three user LEDs are in the same state: initialised to off in `setup_user_leds()`
and never touched again. Board rev 2 deliberately remapped them to PWM-capable pins to
make this possible.

This change finishes the on-device UI so the clock can be set, configured, and used
without a computer attached.

## What Changes

**Menu and buttons**
- Commit `currentMenuState` in `ShiftDisplayFSM::update()` so the menu tree can
  actually advance. Without this nothing else in the menu works.
- Handle `kEventLongPressed` and `kEventRepeatPressed` alongside `kEventClicked`, and
  drop `kFeatureDoubleClick`, which only adds latency to every click.
- Top-level menu reached by clicking SET, scrolled with PLUS/MINUS, entered with SET,
  backed out by holding SET: `tINE`, `dATE`, `12-24`, `ALAr`.
- Field editors where the field being edited blinks, PLUS/MINUS adjust it with
  hold-to-repeat, and SET advances to the next field and finally commits.
- The menu returns to the clock by itself after inactivity, so the device is never
  left stranded in a submenu.

**Display**
- Extend the 7-segment glyph table beyond `0-9`, `A-H`. Menu labels need at least
  `L n o P r t U` and a blank-with-dash form; the table and `map_ascii()`'s range
  check must grow together or new glyphs render as the invalid-character bars.
- 12-hour display mode: hours shown as 1–12, with the RTC left in `HOUR_24` and the
  conversion applied at the display layer only.

**LEDs** (active high; cathodes to GND, confirmed from the netlist)
- `LED_TOP` / PB8 / **D3** — AM/PM indicator, lit for AM, only meaningful in 12-hour
  mode.
- `LED_MID` / PB7 / **D2** — lit when in 12-hour mode.
- `LED_BOT` / PB6 / **D1** — steady when an alarm is armed, blinking while it fires.

**Alarm**
- A daily alarm on RTC Alarm A, set from the menu, armed and disarmed from the menu or
  over serial. When it fires the display flashes and D1 blinks until any button
  dismisses it. There is no buzzer on the board, so the signal is visual.

**Persistence**
- 12/24 mode, alarm time, and armed state survive power loss in the F103's backup
  registers, which the CR2032 already holds up for the RTC.

**Serial**
- `TEST`, `GT`, `ST`, `SO`, `GO` keep their exact current behavior, arguments, and
  replies. New commands are added alongside them for the new state, so the clock stays
  controllable from the host and the new features are testable over CDC rather than
  only by hand.

**Defects fixed in passing** (both in code this change rewrites)
- `printClock()` and `printSetHourMenu()` each `sprintf` a 6-character string into
  `char buffer[6]`, overflowing the stack by the NUL terminator.

## Capabilities

### New Capabilities
- `clock-ui`: the on-device user interface — what the display shows at rest, how the
  three buttons navigate the menu and edit fields, what the LEDs indicate, and how
  12/24-hour mode is presented.
- `alarm`: setting, arming, firing, and dismissing a daily alarm, and the persistence
  of its configuration.
- `serial-command-console`: the USB CDC command surface — the existing commands'
  guaranteed behavior and the new ones covering the added state.

### Modified Capabilities
_None. `dev-environment` and `project-documentation` are unaffected; their existing
requirements (documented commands matching the firmware, hardware verification) apply
to this work unchanged._

## Impact

- **Firmware**: `firmware/src/main.cpp` (buttons, menu rendering, LEDs, alarm, serial
  commands, persistence), `firmware/lib/ShiftDisplayFSM/` (state commit, menu tree),
  `firmware/lib/ShiftDisplay/` (glyph table, blink support), `firmware/include/header.h`
  (settings struct).
- **Docs**: `docs/DEVELOPMENT.md` — the CDC command table, the LED map, the menu
  walkthrough, and the known-defects list, which loses the two buffer overflows and
  the "menu is unimplemented" entry.
- **Flash budget**: currently 69.8% of 64KB with 30.2% headroom. The glyph table and
  menu strings are small, but this is the first change to add real code volume, so
  the build must be watched rather than assumed to fit.
- **Risk**: this is the first change to modify `firmware/src/` since the environment
  work, so "unchanged firmware still flashes" is no longer available as a control.
  Every behavior here needs verifying on the device.

## Not in scope

- PWM brightness control. The `OE` line and `irq_timer_led()` remain unused; doing it
  here would tangle display timing with a menu rewrite.
- The known `tm_mon` off-by-one across `ST`/`GT`, `setup_rtc()` reading uninitialized
  globals, and the `timesync.py` defects. `ST`/`GT` behavior must not change in this
  change, and fixing the month bug would change it.
- Snooze and per-weekday alarm repeat.
