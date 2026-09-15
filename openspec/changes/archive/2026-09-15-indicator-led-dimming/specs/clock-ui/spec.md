## MODIFIED Requirements

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

## ADDED Requirements

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
