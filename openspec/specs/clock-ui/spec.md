# clock-ui Specification

## Purpose

Defines what the device shows and how it is operated from the board itself: the idle time display and its RTC-driven redraw, hours presented in the selected 12/24-hour mode, the three user LEDs as indicators of mode and alarm state, a three-button menu that can be entered, scrolled, backed out of, and timed out of, field editors that show which field is under edit, and a glyph table that covers every character the interface renders.

## Requirements

### Requirement: The display shows the current time at rest
When not in a menu, the display SHALL show the current time of day and SHALL update as the time advances. It SHALL be shown at the configured display brightness.

"At rest" excludes the brief fade that runs when the display returns from the menu. During that fade the digits are deliberately unequal in brightness; once it completes, they SHALL be equal again.

#### Scenario: Time is displayed and advances
- **WHEN** the device is powered and not in a menu
- **THEN** the display shows hours, minutes, and seconds, and the seconds change once per second

#### Scenario: Redraw is driven by the RTC, not by polling
- **WHEN** the time has not changed since the last redraw
- **THEN** the displayed content is not recomputed, so the idle path stays as cheap as it is today

#### Scenario: Shown at the configured brightness
- **WHEN** the display is showing the time and no fade is running
- **THEN** every lit segment is at the configured display brightness, and all six digits are equally bright

#### Scenario: Brightness and content are independent
- **WHEN** the brightness changes
- **THEN** the displayed characters are unaffected — nothing is re-shifted, no digit flickers or glitches, and the decimal point keeps its state

### Requirement: Hours are presented in the selected 12/24-hour mode
The display SHALL present hours according to the selected mode, showing 0–23 in 24-hour mode and 1–12 in 12-hour mode. The RTC SHALL remain configured in 24-hour format regardless of the selected display mode, so that the conversion is a presentation concern only.

In 12-hour mode the leading zero SHALL be suppressed: an hour below ten SHALL show a blank in the tens position rather than a zero. In 24-hour mode the leading zero SHALL be kept.

#### Scenario: Midnight in 12-hour mode
- **WHEN** the RTC hour is 0 and 12-hour mode is selected
- **THEN** the display shows 12, not 0

#### Scenario: Noon and afternoon in 12-hour mode
- **WHEN** the RTC hour is 12 and then 13, with 12-hour mode selected
- **THEN** the display shows 12 and then 1

#### Scenario: 24-hour mode is unaffected
- **WHEN** 24-hour mode is selected and the RTC hour is 0 or 13
- **THEN** the display shows 00 or 13 respectively

#### Scenario: The RTC format is untouched
- **WHEN** the display mode is changed
- **THEN** the RTC's own hour format is not reconfigured, and time read back over serial is unchanged by the switch

#### Scenario: Single-digit hours lose the leading zero
- **WHEN** the displayed hour is between 1 and 9 in 12-hour mode
- **THEN** the tens position is blank, with no segments lit

#### Scenario: Two-digit hours keep both digits
- **WHEN** the displayed hour is 10, 11 or 12 in 12-hour mode
- **THEN** both digits are shown

#### Scenario: The blank is consistent wherever the time appears
- **WHEN** the time is shown while an alarm is firing
- **THEN** the leading zero is suppressed exactly as it is on the idle display

### Requirement: The LEDs indicate mode and alarm state
The three user LEDs SHALL be driven from the authoritative device state, and SHALL be updated whenever that state changes. All three are active high, and each SHALL be driven at the configured indicator brightness rather than simply on or off.

#### Scenario: AM/PM indicator
- **WHEN** 12-hour mode is selected and the time is before 12:00
- **THEN** the top LED (PB8, schematic D3) is lit, and it is unlit from 12:00 onward

#### Scenario: AM/PM is meaningless in 24-hour mode
- **WHEN** 24-hour mode is selected
- **THEN** the top LED is unlit regardless of the time

#### Scenario: Mode indicator
- **WHEN** 12-hour mode is selected
- **THEN** the middle LED (PB7, schematic D2) is lit, and it is unlit in 24-hour mode

#### Scenario: Alarm indicator
- **WHEN** an alarm is armed
- **THEN** the bottom LED (PB6, schematic D1) is lit steadily

#### Scenario: Lit means the configured brightness
- **WHEN** any indicator is lit
- **THEN** it is driven at the configured indicator brightness, and an indicator that is unlit draws no current

#### Scenario: One writer owns the pins
- **WHEN** an indicator's state changes
- **THEN** it is changed through the single path that owns the LED pins, so no code path can reconfigure a pin away from its timer and silently stop the others

### Requirement: Indicator brightness is user-settable
The brightness of the three indicator LEDs SHALL be adjustable by the user as a single level covering all three, settable from the on-device menu and over the serial console, and SHALL persist across a power cycle.

#### Scenario: Adjusting from the menu
- **WHEN** the indicator brightness is changed through the menu and committed
- **THEN** the indicators are visibly dimmer or brighter, and reading the level back reports the new value

#### Scenario: Live preview while adjusting
- **WHEN** the level is being adjusted in the editor
- **THEN** the indicators track the value as it changes, so the choice is made by eye rather than by number

#### Scenario: Round-trips over serial
- **WHEN** the indicator level is set over serial and read back
- **THEN** the value read matches the value set, and a value outside the valid range is rejected without changing the level

#### Scenario: Survives a power cycle
- **WHEN** the level is set and power is removed and restored
- **THEN** the indicators return at the level that was set

#### Scenario: Levels are perceptually even
- **WHEN** the level is stepped from lowest to highest
- **THEN** each step is a comparable change in apparent brightness, rather than the whole range appearing to happen at the bottom of the scale

#### Scenario: The dimmest level is still visible
- **WHEN** the level is at its minimum
- **THEN** each lit indicator remains visible in a dark room, so the setting cannot be mistaken for a failed LED

### Requirement: Display brightness is user-settable
The brightness of the seven-segment display SHALL be adjustable by the user, settable from the on-device menu and over the serial console, and SHALL persist across a power cycle. It is a single level covering the whole display, because the output-enable line is shared by all six digits.

Per-digit brightness, where it is used, SHALL multiply this level rather than replace it: the user's display brightness setting SHALL remain the control that dims every digit together, and no per-digit level SHALL make a digit brighter than that setting allows.

#### Scenario: Adjusting from the menu
- **WHEN** the display brightness is changed through the menu and committed
- **THEN** the display is visibly dimmer or brighter, and reading the level back reports the new value

#### Scenario: The display previews itself
- **WHEN** the level is being adjusted in the editor
- **THEN** the display tracks the value as it changes, so the choice is made by eye against the thing being adjusted

#### Scenario: Round-trips over serial
- **WHEN** the display level is set over serial and read back
- **THEN** the value read matches the value set, and a value outside the valid range is rejected without changing the level

#### Scenario: Survives a power cycle
- **WHEN** the level is set and power is removed and restored
- **THEN** the display returns at the level that was set

#### Scenario: Levels are perceptually even
- **WHEN** the level is stepped from lowest to highest
- **THEN** each step is a comparable change in apparent brightness, rather than the whole range appearing to happen at the bottom of the scale

#### Scenario: The dimmest level is still legible
- **WHEN** the level is at its minimum
- **THEN** the time can still be read in a dark room, so a dimmed clock is never mistaken for a failed one

#### Scenario: No visible flicker
- **WHEN** the display is viewed at any brightness, including at the edge of vision and while the eyes are moving
- **THEN** no flicker is perceptible

#### Scenario: Indicator brightness is separate
- **WHEN** the display brightness is changed
- **THEN** the indicator LEDs are unaffected, and vice versa

#### Scenario: A per-digit level cannot escape the global one
- **WHEN** the global display brightness is set to its minimum while a digit is at full relative brightness
- **THEN** that digit is no brighter than the global minimum allows

### Requirement: Individual digits can be dimmed relative to the display
The display SHALL support dimming individual character positions relative to the rest of the display, by lighting a digit in only some of the refresh cycle's time slices rather than all of them.

A digit's relative brightness SHALL be proportional to the number of slices it is lit in, and SHALL be independent of which slices those are and of the phase relationship with the output-enable line.

A digit at full relative brightness SHALL be indistinguishable from the same digit on a display with no per-digit dimming in use.

#### Scenario: A dimmed digit is visibly dimmer
- **WHEN** one character position is set below full relative brightness while the others stay at full
- **THEN** that digit is visibly dimmer than the others, and remains legible

#### Scenario: Full relative brightness changes nothing
- **WHEN** every character position is at full relative brightness
- **THEN** the display is indistinguishable from the same content shown without per-digit dimming

#### Scenario: Relative brightness is proportional
- **WHEN** a digit's slice count is stepped from lowest to highest
- **THEN** its brightness increases monotonically, and equal slice counts on different character positions produce equal brightness

#### Scenario: A dimmed digit does not flicker
- **WHEN** a digit is held at a low relative brightness
- **THEN** it shows no flicker, beat or shimmer distinct from the rest of the display

#### Scenario: Zero slices blanks the digit
- **WHEN** a character position is set to zero slices
- **THEN** that position shows no lit segments, and the other positions are unaffected

### Requirement: The two brightness mechanisms compose without visible interaction
Global brightness on the output-enable line and per-digit brightness in the data path are independent modulations that multiply. Their periods SHALL be related such that every time slice contains a whole number of output-enable periods, so that each slice carries the same output-enable on-time regardless of the phase between them.

The timer constants that establish this relationship SHALL be derived at build time from the output-enable frequency and the core clock, rather than written as independent literals. The build SHALL fail if the relationship is not exact.

#### Scenario: No beat between the two rates
- **WHEN** the display is held at any combination of global brightness and per-digit brightness
- **THEN** no slow flutter, beat or breathing is visible that is not commanded

#### Scenario: Digits at equal slice counts are equally bright
- **WHEN** two character positions are set to the same slice count at any global brightness
- **THEN** they are equally bright, with no fixed difference attributable to where their slices fall

#### Scenario: Global brightness still covers everything
- **WHEN** the global display brightness is changed while per-digit levels are in use
- **THEN** every digit changes brightness together, keeping their relative proportions

#### Scenario: Retuning the output-enable frequency cannot silently break the lock
- **WHEN** the output-enable PWM frequency is changed and the slice period would no longer contain a whole number of output-enable periods
- **THEN** the build fails, rather than producing firmware with per-digit brightness errors

### Requirement: The output-enable line has a single owner
The shift registers' output-enable line SHALL be driven by exactly one mechanism. Any operation that blanks or restores the display SHALL go through that owner rather than writing the pin directly, because a direct write would reconfigure the pin away from its timer and silently stop the brightness control.

The output-enable line shares a GPIO port with the shift registers' data, clock, latch and clear lines. Code driving those lines SHALL address only the pins it owns, and SHALL NOT write the port as a whole, because a whole-port write reaches across the output-enable line whatever its intent.

This SHALL hold for writes performed by a peripheral as well as by the CPU. Where the port is driven from precomputed words replayed by hardware, those words SHALL be constructed so that they cannot address the output-enable pin, and that property SHALL be enforced at build time rather than left to inspection — nothing in the instruction stream shows such a mistake.

#### Scenario: Blanking and restoring go through the owner
- **WHEN** code blanks or re-enables the display
- **THEN** it does so through the brightness owner, and the pin is never written directly

#### Scenario: Restoring returns to the configured brightness
- **WHEN** the display is blanked and then re-enabled
- **THEN** it returns to the configured brightness rather than to full

#### Scenario: The display is blank through reset
- **WHEN** the MCU is in reset or has not yet configured the pin
- **THEN** the display is blank rather than showing whatever the shift registers powered up holding

#### Scenario: Shifting data does not disturb brightness
- **WHEN** data is shifted out, latched, or the registers are cleared
- **THEN** the output-enable line is unaffected and the display's brightness does not change

#### Scenario: Port-wide writes are not used
- **WHEN** the display driver drives its pins
- **THEN** it addresses them individually rather than writing the whole port, so no operation can reach a pin it does not own

#### Scenario: Peripheral-written words cannot reach the output-enable pin
- **WHEN** a word destined for the port is constructed for replay by hardware
- **THEN** it addresses only the data, clock, latch and clear pins, and a word that would set or clear the output-enable pin fails the build

### Requirement: The menu is reachable and navigable with three buttons
Clicking SET from the idle display SHALL open a top-level menu. PLUS and MINUS SHALL move between menu entries, wrapping at the ends. Clicking SET SHALL enter the highlighted entry. Holding SET SHALL leave the current level without committing a change.

#### Scenario: Entering the menu
- **WHEN** SET is clicked while the clock is displayed
- **THEN** the first menu entry is shown

#### Scenario: Scrolling wraps
- **WHEN** MINUS is pressed on the first menu entry
- **THEN** the last entry is shown

#### Scenario: Backing out discards
- **WHEN** SET is held while inside an entry whose value has been altered but not committed
- **THEN** the previous level is shown and the altered value is not saved

#### Scenario: Menu state actually advances
- **WHEN** a menu transition is requested
- **THEN** the state machine commits the pending menu state, so successive transitions move through the tree rather than repeating the same state

### Requirement: Field editors show which field is being edited
Inside an editor, the field under edit SHALL be visually distinguished by blinking. PLUS and MINUS SHALL adjust the field, wrapping at its limits, and SHALL repeat while held. Clicking SET SHALL advance to the next field, and SHALL commit the value after the last field.

#### Scenario: The edited field blinks
- **WHEN** the hour field is being edited
- **THEN** the hour digits alternate between visible and blank while the remaining digits stay lit

#### Scenario: Values wrap at their limits
- **WHEN** PLUS is pressed with the minute field at 59
- **THEN** the minute field becomes 0

#### Scenario: Holding repeats
- **WHEN** PLUS is held
- **THEN** the field advances repeatedly without further presses

#### Scenario: Committing writes through
- **WHEN** SET is clicked on the last field of the time editor
- **THEN** the new time is written to the RTC and the display returns to showing it

### Requirement: The menu does not strand the device
The menu SHALL return to the idle time display after a period with no button activity, so the device cannot be left showing a menu indefinitely.

#### Scenario: Inactivity returns to the clock
- **WHEN** no button is pressed for the inactivity period while a menu or editor is open
- **THEN** the display returns to the time, with no uncommitted value saved

### Requirement: Returning to the clock from the menu fades the digits in
When the display returns to the idle time display from the menu or an editor, the digits SHALL fade in one at a time, left to right, rather than appearing at once. This SHALL apply whether the return was by backing out, by committing an edit, or by the inactivity timeout.

The fade SHALL be driven by elapsed time rather than by counting steps, so that a delayed pass through the main loop shortens the fade rather than stretching it.

The fade SHALL change brightness only. The displayed content SHALL NOT be recomputed or re-shifted on its account.

The fade SHALL always end with every character position at full relative brightness and the display at the configured global brightness. If anything interrupts it — a button press, a re-entry to the menu, an alarm firing — it SHALL be abandoned and every position returned to full immediately, so that no path can leave a digit dim.

#### Scenario: Backing out of the menu fades in
- **WHEN** the menu is backed out of and the display returns to the time
- **THEN** the digits appear one at a time from left to right, and all six are at full brightness when the fade completes

#### Scenario: Committing an edit fades in
- **WHEN** an editor's last field is advanced past and the value is committed
- **THEN** the display returns to the time with the same fade

#### Scenario: Timing out fades in
- **WHEN** the inactivity timeout returns the display to the time
- **THEN** the display returns with the same fade

#### Scenario: Dismissing an alarm does not fade
- **WHEN** a firing alarm is dismissed and the display returns to the time
- **THEN** the time is shown at once, at full brightness, with no fade

#### Scenario: Interrupting the fade does not strand a dim digit
- **WHEN** a button is pressed, the menu is re-entered, or an alarm fires while the fade is running
- **THEN** the fade is abandoned and every character position is at full relative brightness immediately

#### Scenario: The fade ends at the user's brightness, not above or below it
- **WHEN** the fade completes at any configured display brightness, including the minimum
- **THEN** the display is at exactly the brightness it would have been had there been no fade

#### Scenario: A blanked position stays blank through the fade
- **WHEN** the leading hour position is blank in 12-hour mode and the fade runs
- **THEN** nothing appears at that position at any point in the fade, and the remaining digits keep their timing

#### Scenario: The fade does not recompute content
- **WHEN** the fade is running and the time has not changed
- **THEN** the displayed characters are not recomputed or re-shifted, so the idle path stays as cheap as it is today

#### Scenario: A slow loop shortens the fade rather than stretching it
- **WHEN** the main loop is delayed during a fade
- **THEN** the fade reaches full brightness at the time it would have anyway, skipping intermediate levels rather than running long

### Requirement: The glyph table covers the characters the UI renders
The display driver SHALL render every character the user interface asks it to draw. The glyph table and the character-range validation SHALL be extended together, so that a character added to one is never rejected by the other.

#### Scenario: Menu labels render
- **WHEN** a menu label is written to the display
- **THEN** every character in it renders as its intended glyph, not as the invalid-character indicator

#### Scenario: Unsupported characters still fail visibly
- **WHEN** a character with no glyph is written
- **THEN** the invalid-character indicator is shown, rather than a blank or an arbitrary pattern

### Requirement: The display refreshes without CPU involvement
The display SHALL be refreshed continuously by a hardware peripheral replaying a precomputed waveform, rather than by the CPU clocking bits out on demand. The refresh SHALL continue at its full rate while the CPU is busy elsewhere, and SHALL NOT require the main loop to run at any particular rate.

Changing what is displayed SHALL be a matter of writing to the waveform buffer. No caller SHALL be required to shift or latch anything for the change to appear.

#### Scenario: The refresh survives a busy CPU
- **WHEN** the main loop is occupied for longer than one refresh period
- **THEN** the display continues refreshing at the same rate, with no flicker, dimming or corruption

#### Scenario: Writing the buffer is enough
- **WHEN** new content is written to the display
- **THEN** it appears without the caller shifting or latching, within one refresh period

#### Scenario: The refresh rate is above the flicker threshold
- **WHEN** the display is viewed at the edge of vision, or while the eyes are moving across it
- **THEN** no flicker is perceptible at any brightness

#### Scenario: Nothing is shown before the engine is configured
- **WHEN** the MCU has reset and the engine has not yet been started
- **THEN** the display is blank rather than showing whatever the shift registers powered up holding

### Requirement: The shift-out stays within the shift registers' timing limits
The rate at which data is clocked into the shift registers SHALL be slow enough for the devices fitted, at the voltage they run at, with margin. The limit SHALL be established by measurement on the assembled board rather than by calculation alone, because the binding constraint is the serial propagation delay accumulated through a chain of six devices, and the parts are specified at a higher voltage than the board supplies.

Where the rate is set by a timer rather than by instruction timing, it SHALL be a chosen frequency derived from the core clock, not a consequence of what the compiler emitted. The chosen rate SHALL still be justified against the measured failure point.

#### Scenario: The safe rate is measured, not assumed
- **WHEN** the shift rate is chosen
- **THEN** it is derived from an observed failure point on the assembled hardware, and the margin between the two is recorded

#### Scenario: Corruption is made visible during measurement
- **WHEN** the shift rate is swept to find the failure point
- **THEN** the pattern on the display is one where corruption is unmistakable, since the end of the chain is not wired back and no electrical readback exists

#### Scenario: The display is correct at the chosen rate
- **WHEN** the display is exercised at the chosen rate across all six digits and the full glyph set
- **THEN** every character renders correctly, with no intermittent wrong segments

#### Scenario: The rate is stated where it can be found
- **WHEN** someone asks why the shift runs at the rate it does
- **THEN** the measured failure point, the chosen rate and the margin between them are recorded, rather than left as an unexplained constant

#### Scenario: A timer-set rate is exact
- **WHEN** the shift clock is produced by a timer
- **THEN** its frequency follows from the core clock and the timer's reload value, and is stated as a number rather than estimated from instruction counts
