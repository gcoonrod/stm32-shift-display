## 1. Move ~OE into ShiftDisplay

- [x] 1.1 Give `ShiftDisplay` a brightness value and a setter, with the active-low inversion handled inside the class so callers never write the complement themselves
- [x] 1.2 Drive `~OE` with `analogWrite` on PA0 (TIM2_CH1) in `begin()`, replacing the `pinMode`/`digitalWrite` setup
- [x] 1.3 Reimplement `enable()` and `disable()` as brightness operations — `enable()` restores the configured brightness, not full
- [x] 1.4 Confirm no `digitalWrite` to the `~OE` pin survives anywhere in `src/` or `lib/`
- [x] 1.5 Confirm the display still shows the same content after ~OE moved into ShiftDisplay. *Done via the end state rather than the planned fixed-full-brightness intermediate build, which was skipped: the intermediate was not separately observable, and the configured-level behaviour is.*

## 2. Shared PWM settings

- [x] 2.1 Raise the global `analogWriteFrequency` to 4 kHz, with a comment that resolution and frequency are shared with the indicator LEDs
- [x] 2.2 Confirm the timer reload still exceeds the 12-bit duty range at 4 kHz, so resolution is not silently reduced
- [x] 2.3 Flash and confirm the indicator LEDs are unaffected — brightness levels and alarm breathing unchanged

## 3. Brightness level

- [x] 3.1 Add a display level to the settings struct, with a defined default
- [x] 3.2 Persist it in the **high** byte of DR8, preserving the indicator level in the low byte; confirm DR1, DR4, DR6, DR7 and DR10 remain untouched
- [x] 3.3 Map levels to duty on the same gamma curve as the indicators, with a floor chosen for legibility rather than mere visibility
- [x] 3.4 Apply the level so the display shows at the configured brightness, and confirm all six digits are equally bright
- [x] 3.5 Confirm changing brightness does not disturb content — nothing re-shifts, no digit glitches, the decimal point holds

## 4. Glyphs and menu

- [x] 4.1 Define `I` and `S` in the glyph table, filling slots that are already zeroed
- [x] 4.2 Re-run the label audit from the previous change over every label string, so a missing glyph is caught before it reaches the board
- [x] 4.3 Add the `dISP` menu entry and its editor, with the level as a single adjustable field
- [x] 4.4 Make the display preview itself live while the level is being adjusted
- [x] 4.5 Confirm backing out with a long press discards the change and restores the previous brightness
- [x] 4.6 Confirm the inactivity timeout also restores the previous brightness rather than leaving the preview applied

## 4b. Glyph table hardening (folded in)

- [x] 4b.1 Replace the raw binary literals in `segment_data[]` with named `SEG_A`..`SEG_G` constants, so each glyph reads as the segments it lights
- [x] 4b.2 Fix `'B'`, which was missing segment C and was byte-identical to `'T'` — the last of three glyph bugs that literals made easy to write and hard to see
- [x] 4b.3 Regenerate the table from the verified decode rather than retyping it, and diff the result byte-for-byte against the previous values
- [x] 4b.4 Confirm exactly one byte changed (`'B'`) and that flash is unchanged, since the compiler folds the constants

## 5. Serial

- [x] 5.1 Add get/set commands for the display level, rejecting out-of-range values without changing state
- [x] 5.2 Use the read-back to confirm the inversion is the right way round before judging it by eye — level 1 must be dim and level 8 bright
- [x] 5.3 Verify `TEST`, `GT`, `ST`, `SO` and `GO` are untouched, and that the unmodified `timesync.py` still reports skew 0

## 6. Verification

- [x] 6.1 Confirm the dimmest level is still legible in a dark room; raise the floor if it is not
- [x] 6.2 Confirm the brightest level is not unpleasant in a lit room
- [x] 6.3 Check for flicker at several levels, including at the edge of vision and while moving the eyes
- [x] 6.4 Confirm the alarm still flashes the display, at the configured brightness rather than overriding to full
- [x] 6.5 Confirm the field-edit blink still blanks only the field being edited
- [x] 6.6 Power-cycle and confirm both brightness levels survive alongside the other settings and the date
- [x] 6.7 Walk the full menu and confirm the new entry has not disturbed scrolling, wrap, long-press exit or the timeout
- [x] 6.8 Record the final flash and RAM against the 81.1% / 24.3% starting point
- [x] 6.9 Update `docs/DEVELOPMENT.md`: the new commands, the menu entry, the brightness section, and the shared PWM frequency
- [x] 6.10 Update `CLAUDE.md` where it describes the display driver and `enable()`/`disable()`
- [x] 6.11 Run `openspec validate display-dimming`
