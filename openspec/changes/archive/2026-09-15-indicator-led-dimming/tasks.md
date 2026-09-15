## 1. PWM drive, no behaviour change yet

- [x] 1.1 Replace the three `digitalWrite` calls in `update_leds()` with `analogWrite`, lit meaning full duty, so the change is invisible on the board at this stage
- [x] 1.2 Change `setup_user_leds()` to establish the off state through the same path rather than `pinMode` plus `digitalWrite(LOW)`, so nothing reconfigures a PWM pin back to plain output
- [x] 1.3 Delete the `AlrmLEDTim` global and the commented-out `setPWM` block, which this change supersedes
- [x] 1.4 Build and record the flash delta from the 74.2% starting point — `analogWrite` links PWM machinery this firmware has not used before
- [x] 1.5 Flash and confirm all three indicators still behave exactly as before

## 2. Brightness level

- [x] 2.1 Add an indicator level to the settings struct, with a defined default
- [x] 2.2 Persist it in the low byte of DR8, leaving the high byte free for `display-dimming`; re-confirm DR1, DR4, DR6, DR7 and DR10 are still untouched
- [x] 2.3 Add the gamma lookup table mapping user levels to duty values
- [x] 2.4 Apply the level in `update_leds()` so "lit" means the configured duty
- [x] 2.5 Confirm the dimmest level is still clearly visible in a dark room, and raise the floor if it is not

## 3. Serial

- [x] 3.1 Add get/set commands for the indicator level, rejecting out-of-range values without changing state
- [x] 3.2 Change `GL` to report each LED's duty instead of `digitalRead`, which samples the live PWM waveform and returns noise on an alternate-function pin
- [x] 3.3 Confirm the existing `serial-command-console` scenarios still hold with duty values — "lit" now means non-zero
- [x] 3.4 Verify `TEST`, `GT`, `ST`, `SO` and `GO` are untouched, and that the unmodified `timesync.py` still reports skew 0

## 4. Menu

- [x] 4.1 Add the brightness entry to the menu tree, with a label renderable from the existing glyph set
- [x] 4.2 Add its editor, with the level as a single adjustable field
- [x] 4.3 Make the indicators track the value live while it is being adjusted, so the choice is made by eye
- [x] 4.4 Confirm backing out with a long press discards an uncommitted level change and restores the previous brightness
- [x] 4.5 Confirm the inactivity timeout also restores the previous brightness rather than leaving the preview applied

## 5. Breathing

- [x] 5.1 Replace the `blink_on()` source for the firing indicator with a breathing waveform computed from `millis()`
- [x] 5.2 Scale the breath so its peak is the configured indicator brightness rather than full duty
- [x] 5.3 Keep the trough slightly above zero so a glance mid-breath does not read as "not armed"
- [x] 5.4 Confirm the armed indicator still sits steady, visibly distinct from breathing
- [x] 5.5 Confirm the fade reads as continuous rather than stepped at both the highest and lowest brightness levels
- [x] 5.6 Verify a real firing on the board, and confirm `GL` shows the bottom LED's duty varying so firing stays detectable from a host

## 6. Verification and documentation

- [x] 6.1 Power-cycle and confirm the indicator level survives alongside the existing settings and the date
- [x] 6.2 Walk the full menu and confirm the new entry has not disturbed scrolling, wrap, long-press exit or the timeout
- [x] 6.3 Confirm all three indicators still show the right thing: AM only in 12-hour mode, mode indicator, alarm armed
- [x] 6.4 Record the final flash and RAM figures
- [x] 6.5 Update `docs/DEVELOPMENT.md`: the `GL` reply format, the new commands, the menu entry, the LED table, and a note that TIM4 is nominally `TIMER_SERVO`
- [x] 6.6 Update `CLAUDE.md` where it describes the LEDs as driven on or off
- [x] 6.7 Run `openspec validate indicator-led-dimming`
