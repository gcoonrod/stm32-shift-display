## MODIFIED Requirements

### Requirement: The display shows the current time at rest
When not in a menu, the display SHALL show the current time of day and SHALL update as the time advances. It SHALL be shown at the configured display brightness.

#### Scenario: Time is displayed and advances
- **WHEN** the device is powered and not in a menu
- **THEN** the display shows hours, minutes, and seconds, and the seconds change once per second

#### Scenario: Redraw is driven by the RTC, not by polling
- **WHEN** the time has not changed since the last redraw
- **THEN** the displayed content is not recomputed, so the idle path stays as cheap as it is today

#### Scenario: Shown at the configured brightness
- **WHEN** the display is showing the time
- **THEN** every lit segment is at the configured display brightness, and all six digits are equally bright

#### Scenario: Brightness and content are independent
- **WHEN** the brightness changes
- **THEN** the displayed characters are unaffected — nothing is re-shifted, no digit flickers or glitches, and the decimal point keeps its state

## ADDED Requirements

### Requirement: Display brightness is user-settable
The brightness of the seven-segment display SHALL be adjustable by the user, settable from the on-device menu and over the serial console, and SHALL persist across a power cycle. It is a single level covering the whole display; the output-enable line is shared by all six digits, so per-digit brightness is not offered.

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

### Requirement: The output-enable line has a single owner
The shift registers' output-enable line SHALL be driven by exactly one mechanism. Any operation that blanks or restores the display SHALL go through that owner rather than writing the pin directly, because a direct write would reconfigure the pin away from its timer and silently stop the brightness control.

#### Scenario: Blanking and restoring go through the owner
- **WHEN** code blanks or re-enables the display
- **THEN** it does so through the brightness owner, and the pin is never written directly

#### Scenario: Restoring returns to the configured brightness
- **WHEN** the display is blanked and then re-enabled
- **THEN** it returns to the configured brightness rather than to full

#### Scenario: The display is blank through reset
- **WHEN** the MCU is in reset or has not yet configured the pin
- **THEN** the display is blank rather than showing whatever the shift registers powered up holding
