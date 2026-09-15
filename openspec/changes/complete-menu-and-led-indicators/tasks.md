## 1. State machine foundation

- [x] 1.1 Add `currentMenuState = nextMenuState;` to `ShiftDisplayFSM::update()` — without this every menu transition is computed and discarded
- [x] 1.2 Add a `getMenuState()` accessor so `main.cpp` can render from the committed menu state
- [x] 1.3 Fill in the `MENU_UP` and `MENU_DOWN` actions, which are currently empty cases, with wrapping movement across the top-level entries
- [x] 1.4 Extend `execute()` so `MENU_SELECT` descends from each top-level entry into its editor, and `MENU_EXIT` ascends one level rather than always returning to `IDLE`
- [x] 1.5 Build and confirm the FSM still compiles into the existing main loop with no behavior change yet visible

## 2. Display glyphs

- [x] 2.1 Replace `segment_data[]` plus the hand-written range check in `map_ascii()` with a single ASCII-indexed lookup, so a glyph is renderable exactly when it is defined
- [x] 2.2 Add glyphs for `-`, `L`, `n`, `o`, `P`, `r`, `t`, `U`, keeping the existing `0-9`, `A-H` and space unchanged
- [x] 2.3 Keep the invalid-character indicator for anything with no glyph, so an unrenderable character still fails visibly
- [x] 2.4 Fix the `char buffer[6]` stack overflows at `main.cpp:248` and `:324` — both `sprintf` six characters plus a NUL into six bytes
- [x] 2.5 Flash a scratch build that writes each new glyph and confirm on the display that every one renders as intended, not as the invalid-character bars

## 3. Settings storage and LEDs

- [x] 3.1 Add a settings struct holding 12/24-hour mode, alarm hour, alarm minute, and armed state
- [x] 3.2 Implement load/save against backup registers DR2 (magic), DR3 (flags), DR4 (packed alarm time) via `LL_RTC_BAK_SetRegister`/`GetRegister`; the library's own helpers are internal to `rtc.c`
- [x] 3.3 Confirm in `rtc.h` that `RTC_BKP_DATE` is still `LL_RTC_BKP_DR6` and that nothing in this change writes DR6 or DR7 — those hold the emulated date on the F1
- [x] 3.4 Apply a magic value on load so uninitialized backup memory yields defaults rather than arbitrary settings
- [x] 3.5 Implement `update_leds()` driven from state: top/PB8/D3 = PM in 12-hour mode, mid/PB7/D2 = 12-hour mode active, bottom/PB6/D1 = alarm armed. All active high
- [x] 3.6 Call `update_leds()` once per loop rather than writing LEDs from individual handlers
- [x] 3.7 Verify persistence on hardware: set mode and alarm, pull USB power, reconnect, and confirm the settings and the date both survived

## 4. 12/24-hour display

- [x] 4.1 Apply the 12-hour conversion at the point digits are formatted, leaving the RTC in `HOUR_24`
- [x] 4.2 Handle the two boundaries explicitly: RTC hour 0 displays as 12, RTC hour 12 displays as 12, RTC hour 13 displays as 1
- [x] 4.3 Confirm `rtc.begin()` is still called with `STM32RTC::HOUR_24` and that no code path reconfigures the RTC's hour format
- [x] 4.4 Verify on hardware across a mode switch that `GT` returns the same timestamp before and after

## 5. Buttons and menu

- [x] 5.1 Remove `kFeatureDoubleClick` from `ButtonConfig` — nothing uses it and it delays every click by the double-click window
- [x] 5.2 Handle `kEventLongPressed` and `kEventRepeatPressed` in `handleEvent()` alongside `kEventClicked`, latching them the way click events are latched today
- [x] 5.3 Implement the top-level menu: SET enters, PLUS/MINUS scroll with wrap, SET selects, long-press SET backs out
- [x] 5.4 Choose menu labels renderable with the extended glyph set and confirm each on the display
- [x] 5.5 Implement the time editor: active field blinks, PLUS/MINUS adjust with wrap, repeat-press drives hold-to-adjust, SET advances field and commits after the last
- [x] 5.6 Implement the date editor on the same pattern
- [x] 5.7 Implement the 12/24 toggle entry, writing through to settings and refreshing the LEDs
- [x] 5.8 Drive blinking from a single `millis()`-derived phase shared with the alarm blink, and re-render only on content or phase change so the idle path does not re-shift every loop
- [x] 5.9 Add the inactivity timeout that returns to the clock without committing a pending value
- [x] 5.10 Confirm backing out of an edited-but-uncommitted field discards the change

## 6. Alarm

- [x] 6.1 Add the alarm entry to the menu: set hour and minute, then arm or disarm
- [x] 6.2 Raise the firing flag from `irq_rtc_seconds` when armed and `seconds == 0` and hour/minute match, so it fires once per day with no re-arming
- [x] 6.3 Implement the firing signal: display flashes and the bottom LED blinks, visibly distinct from the steady armed indication
- [x] 6.4 Dismiss on any button, and consume that press so it does not also act on the menu
- [x] 6.5 Confirm the alarm stays armed after dismissal and fires again the next day
- [x] 6.6 Confirm the clock keeps time while the alarm is left signalling, and that `GT` is still correct
- [x] 6.7 Verify a real firing on hardware by setting the alarm a minute or two ahead and watching it

## 7. Serial commands

- [x] 7.1 Add get/set commands for 12/24-hour mode
- [x] 7.2 Add get/set commands for the alarm time, rejecting hours outside 0–23 and minutes outside 0–59 without changing state
- [x] 7.3 Add an arm/disarm command and a way to read the armed state
- [x] 7.4 Add a command that reports LED state, so the indicators can be checked from the host rather than only by eye
- [x] 7.5 Register every new command with `AddCommand()` — note the existing `cmd_set_hour`/`cmd_set_minute`/`cmd_set_second` are implemented but never registered, which is the trap to avoid repeating
- [x] 7.6 Confirm the receive buffer still holds the longest new command with its arguments
- [x] 7.7 Leave `TEST`, `GT`, `ST`, `SO`, `GO` handlers untouched; diff them against the previous revision to prove it

## 8. Verification

- [x] 8.1 Run the unmodified `timesync.py -H` against the device as the serial regression test — it must report the time and skew exactly as before, with no edit to the tool
- [x] 8.2 Round-trip every new setting over serial, and confirm a setting changed with the buttons reads back correctly over serial
- [x] 8.3 Walk the whole menu on the device: every entry reachable, wrap works at both ends, long-press backs out, timeout returns to the clock
- [x] 8.4 Confirm all three LEDs against their table: PM in 12-hour mode only, mode indicator, armed indicator
- [x] 8.5 Power-cycle and confirm mode, alarm time, armed state, and the date all survive
- [x] 8.6 Record final flash and RAM against the 69.8% / 23.3% starting point
- [x] 8.7 Update `docs/DEVELOPMENT.md`: CDC command table, LED map with the D-number mapping, menu walkthrough, and remove the two buffer overflows and the "menu unimplemented" entry from known defects
- [x] 8.8 Update `CLAUDE.md` where it describes the FSM as having an unreachable menu tree and the LEDs as unused
- [x] 8.9 Run `openspec validate complete-menu-and-led-indicators`
