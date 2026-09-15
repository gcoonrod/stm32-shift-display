## Context

The on-device UI exists as scaffolding that cannot run. `ShiftDisplayFSM` declares a
nine-state `MenuState` tree, but `update()` is:

```cpp
void ShiftDisplayFSM::update() { currentState = nextState; }
```

`currentMenuState` is never assigned from `nextMenuState`, so every menu transition is
computed and then thrown away. `main.cpp`'s `handleEvent()` matches only
`kEventClicked`, so the `kFeatureLongPress` and `kFeatureRepeatPress` already enabled in
`ButtonConfig` produce events nothing reads. PLUS and MINUS are debounced and polled
every loop, then discarded. The LEDs are driven `LOW` once in `setup_user_leds()` and
never again.

Hardware facts confirmed from the exported netlist, not from the source's naming:

| Schematic | Net | MCU pin | `main.cpp` name |
| --- | --- | --- | --- |
| D3 | `/LED3` | PB8 | `LED_TOP` |
| D2 | `/LED2` | PB7 | `LED_MID` |
| D1 | `/LED1` | PB6 | `LED_BOT` |
| SW4 | `/BTN3` | PB5 | `BTN_SET` |
| SW3 | `/BTN2` | PB4 | `BTN_PLUS` |
| SW2 | `/BTN1` | PB3 | `BTN_MINUS` |

LED cathodes tie to GND, so all three are **active high**. Switch commons tie to GND, so
the buttons are active low, matching the existing `INPUT_PULLUP`. Note that the
firmware's `TOP`/`MID`/`BOT` names run opposite to the D-numbering — D1 is the bottom
LED. Code should use the positional names; this table is how they map back to the
schematic.

Constraints: 64KB flash with 30.2% free after the core-3 port, one physical board, no
test suite, and no sounder anywhere on the design.

## Goals / Non-Goals

**Goals:**
- Make the clock fully configurable from its own buttons.
- Drive the three LEDs from real state.
- Add a daily alarm that signals visually and survives a power cycle.
- Keep every existing serial command byte-for-byte compatible.

**Non-Goals:**
- PWM brightness on the `OE` line.
- Fixing the `tm_mon` off-by-one, which would change `ST`/`GT` behavior this change
  promises to preserve.
- Snooze, weekday repeat, or multiple alarms.

## Decisions

### Commit the menu state before anything else

`update()` gains `currentMenuState = nextMenuState;`. This is one line and it is the
prerequisite for every other menu behavior — without it, entering a submenu appears to
work once and then sticks. It lands first so the rest of the menu work is built on a
state machine that actually transitions.

### 12-hour mode is a display transform; the RTC stays in `HOUR_24`

The RTC is initialised `rtc.begin(false, STM32RTC::HOUR_24)` and stays that way. The
12-hour presentation is applied only where digits are formatted, and the AM/PM LED is
derived from the 24-hour value (`hour >= 12`).

This is what keeps the serial promise cheap to honour. `cmd_get_time` and `cmd_set_time`
read and write `date_time_buf` and the RTC in 24-hour terms; if the RTC were switched to
`HOUR_12`, every one of those paths would need an AM/PM parameter and the timestamp
arithmetic would have to account for it. Making the mode a presentation concern means
the serial code is untouched by the feature, which is far easier to guarantee than
"changed but equivalent".

*Alternatives considered:* switching the RTC to `HOUR_12` and threading `AM_PM` through
(touches exactly the code that must not change); storing a separate 12-hour shadow copy
of the time (two sources of truth for the same value).

### Replace the glyph range-check with a single ASCII lookup

`map_ascii()` currently validates with a hand-written range test and then indexes
`segment_data[]` by computed offset:

```cpp
if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'H') || (c == ' '))) return 0b10101000;
uint8_t index = (c <= '9') ? (c - '0') : (c - 'A' + 10);
```

Adding glyphs means editing the range test and the table in lockstep, and they will
drift — a character present in the table but missing from the range test renders as the
invalid-character bars, which looks like a hardware fault rather than a typo. Replacing
both with one sparse ASCII-indexed table makes the table the only source of truth, so a
glyph is renderable exactly when it is defined.

The set to add, all unambiguous on seven segments: `-`, `L`, `n`, `o`, `P`, `r`, `t`,
`U`. Deliberately not added: `M` and `W`, which cannot be rendered legibly and should
not be faked — menu labels are chosen from what the display can actually show.

### Poll the alarm in the existing 1 Hz tick rather than using RTC Alarm A

`irq_rtc_seconds` already fires every second and already refreshes `date_time_buf`.
A daily alarm is then: while armed, when `seconds == 0` and `hours:minutes` equal the
alarm time, raise a flag. Firing exactly once per day falls out of the seconds-zero
condition.

STM32RTC does expose Alarm A on the F1 with `MATCH_HHMMSS` ("every day"), but the F1's
RTC is a bare 32-bit counter with no calendar — the library emulates the calendar in
software and stores the date in backup registers. Daily re-arm behavior on that
emulation is exactly the kind of thing that works on an F4 and surprises on an F1, and
the only reason to prefer the peripheral would be waking from deep sleep, which this
firmware does not do since the Low Power dependency was removed. Polling a value the
firmware already computes each second is less code and has no peripheral quirk to
discover.

*Alternatives considered:* RTC Alarm A with `MATCH_HHMMSS` (fewer moving parts in
principle, but adds an ISR, a second source of time truth, and F1-specific risk);
comparing in the main loop rather than the tick (would fire repeatedly for a whole
second, needing its own edge tracking).

### Persist settings in backup registers DR2/DR3/DR5, never DR1, DR4, DR6, DR7 or DR10

`rtc.h` defines `RTC_BKP_DATE` as `LL_RTC_BKP_DR6` on the F1, and the library stores the
emulated date across **DR6 and DR7**. Writing settings there would silently corrupt the
date — the failure would look like a clock bug, not a settings bug.

DR6/DR7 are not the only claimed registers. `backup.h` also reserves **DR1**
(`RTC_BKP_INDEX`) and **DR4** (`HID_MAGIC_NUMBER_BKP_INDEX`, the HID bootloader magic),
plus **DR10** (`HID_OLD_MAGIC_NUMBER_BKP_INDEX`). An earlier draft of this design put the
alarm in DR4, which would have clobbered the bootloader magic — the same class of silent
corruption as the date, found only by reading `backup.h` before writing to it.

Settings therefore use DR2 (magic), DR3 (flags: 12-hour mode, armed) and DR5 (alarm hour
and minute packed). A magic value distinguishes initialised backup memory from a fresh
coin cell; without it, arbitrary contents read as a valid configuration.

Access goes through the core's own `getBackupRegister`/`setBackupRegister` from
`backup.h`, which are public `static inline` wrappers reachable via `Arduino.h`. (The F1
LL driver has no `LL_RTC_BAK_*` accessors at all — it defines only the `LL_RTC_BKP_DRn`
index constants — and STM32RTC's same-named helpers are private to `rtc.c`.)
`enableBackupDomain()` from the same header unlocks the domain and enables the BKP clock;
it resets nothing.

*Alternatives considered:* overriding `RTC_BKP_DATE` with a build flag to relocate the
library's date storage (moves someone else's data to make room for ours); flash
emulation (wear, and the coin cell is right there).

*As built*: verified across four reset cycles with distinctive values, all preserved
alongside the date. One earlier boot came back showing defaults and did not reproduce;
it is recorded here rather than explained away.

### One LED update path, derived from state

LEDs are written by a single `update_leds()` called once per loop from the authoritative
state — mode, armed, firing, current hour — rather than by `digitalWrite` calls
scattered through menu handlers. With three indicators, two of which depend on state that
several code paths can change, the scattered form guarantees a path that forgets to
update one. Blink phases come from a single `millis()`-derived counter shared by the
editing-field blink and the alarm blink, so they cannot drift apart visually.

### Give SET its own ButtonConfig; drop double-click

`ButtonConfig` currently enables `kFeatureDoubleClick`, which forces AceButton to delay
every click event until the double-click window expires. Nothing uses double-click, and
the delay is felt on every single menu press. Removing it makes clicks immediate.
`kEventLongPressed` drives "back out a level" and `kEventRepeatPressed` drives
hold-to-adjust.

The two features cannot share a `ButtonConfig`. `AceButton::check()` evaluates
`checkLongPress()` and `checkRepeatPress()` in the same pass, and `kLongPressDelay` and
`kRepeatPressDelay` are both 1000 ms by default, so a held button emits
`kEventLongPressed` immediately followed by `kEventRepeatPressed`. With one latched
button state per button, the second silently overwrote the first and holding SET did
nothing at all. SET therefore gets its own config with click and long-press only, while
PLUS and MINUS keep click and repeat and drop long-press, which they never used. The
latch additionally refuses to downgrade a `LONG_PRESSED` to anything arriving after it in
the same pass.

### Dismissal consumes the button press

When the alarm is firing, the press that dismisses it must not also act on the menu —
otherwise silencing the alarm drops the user into a submenu. The firing state is checked
before menu dispatch and consumes the event.

## Risks / Trade-offs

- **Flash budget.** 69.8% used, so roughly 19KB free. Menu strings, the glyph table, and
  editor logic are small, but this is the first change to add real code volume; the
  build output is checked at each stage rather than at the end, so an overrun is caught
  while it is still attributable.
- **Regressing the serial commands.** The spec requires byte-identical behavior, and the
  strongest available check is that the unmodified `timesync.py` still works. Running it
  is the regression test, not a code read.
- **Blinking interacts with the display refresh.** The idle path only redraws when
  `time_dirty` is set; blinking needs a periodic redraw that must not reintroduce a
  full re-shift every loop. Render on content change or blink-phase change, and latch
  only then.
- **Buffer overflows already present.** `printClock()` and `printSetHourMenu()` both
  `sprintf` six characters into `char buffer[6]`. This change rewrites both, so the
  sizing is fixed as part of that work rather than left as a trap for the new code to
  inherit.
- **No sounder.** A visual-only alarm will not wake anyone. That is a property of the
  board, not of this change; if it matters, a later revision adds a buzzer.

## Migration Plan

1. Commit the FSM menu state; confirm transitions advance.
2. Extend the glyph table and switch `map_ascii()` to the lookup; confirm on hardware
   that new glyphs render.
3. Add settings storage and `update_leds()`; confirm the LEDs follow serial-set state
   before any menu exists to drive them.
4. Add button events and the menu tree, then the field editors.
5. Add the alarm tick, firing, and dismissal.
6. Add the new serial commands; re-run `timesync.py` as the regression check.
7. Update the docs and the known-defects list.

**Rollback:** the firmware source is otherwise stable, so reverting the change restores
the current working clock; re-flash over SWD.

## Open Questions

- What inactivity period should return the menu to the clock? Ten seconds is the
  starting value; it is a one-constant change once it has been lived with.
- Should setting the time zero the seconds, as most clocks do, or preserve them? Zeroing
  is the convention and makes the clock settable against a time signal, but it is a
  visible behavior choice worth confirming during implementation.
- Does the alarm need to survive being set for a time that is skipped when the clock is
  changed forward past it? Current design says no: it fires only on an exact
  seconds-zero match, so a skipped minute is a missed alarm that day.
