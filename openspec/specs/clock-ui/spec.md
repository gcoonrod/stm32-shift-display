# clock-ui Specification

## Purpose

Defines what the device shows and how it is operated from the board itself: the idle time display and its RTC-driven redraw, hours presented in the selected 12/24-hour mode, the three user LEDs as indicators of mode and alarm state, a three-button menu that can be entered, scrolled, backed out of, and timed out of, field editors that show which field is under edit, and a glyph table that covers every character the interface renders.

## Requirements

### Requirement: The display shows the current time at rest
When not in a menu, the display SHALL show the current time of day and SHALL update as the time advances.

#### Scenario: Time is displayed and advances
- **WHEN** the device is powered and not in a menu
- **THEN** the display shows hours, minutes, and seconds, and the seconds change once per second

#### Scenario: Redraw is driven by the RTC, not by polling
- **WHEN** the time has not changed since the last redraw
- **THEN** the displayed content is not recomputed, so the idle path stays as cheap as it is today

### Requirement: Hours are presented in the selected 12/24-hour mode
The display SHALL present hours according to the selected mode, showing 0–23 in 24-hour mode and 1–12 in 12-hour mode. The RTC SHALL remain configured in 24-hour format regardless of the selected display mode, so that the conversion is a presentation concern only.

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

### Requirement: The glyph table covers the characters the UI renders
The display driver SHALL render every character the user interface asks it to draw. The glyph table and the character-range validation SHALL be extended together, so that a character added to one is never rejected by the other.

#### Scenario: Menu labels render
- **WHEN** a menu label is written to the display
- **THEN** every character in it renders as its intended glyph, not as the invalid-character indicator

#### Scenario: Unsupported characters still fail visibly
- **WHEN** a character with no glyph is written
- **THEN** the invalid-character indicator is shown, rather than a blank or an arbitrary pattern
