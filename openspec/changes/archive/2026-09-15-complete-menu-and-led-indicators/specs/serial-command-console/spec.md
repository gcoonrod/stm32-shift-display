## ADDED Requirements

### Requirement: The existing command set is preserved exactly
The commands `TEST`, `GT`, `ST`, `SO`, and `GO` SHALL keep their current names, arguments, replies, and semantics. Host tooling written against them SHALL continue to work without modification.

#### Scenario: Existing commands answer as before
- **WHEN** `TEST`, `GT`, and `GO` are sent
- **THEN** they reply `TEST`, a Unix timestamp, and the timezone offset respectively, as they did before this change

#### Scenario: Setting the time still works
- **WHEN** `ST` is sent with a Unix timestamp
- **THEN** it replies `OK` and the clock is set, with the same interpretation of the timestamp as before

#### Scenario: The host tool is unaffected
- **WHEN** the existing timesync tool is run against the device
- **THEN** it reports the device time and corrects it exactly as it did before this change, with no edit to the tool

#### Scenario: Framing is unchanged
- **WHEN** commands are sent terminated by carriage return and newline with space-separated arguments
- **THEN** they are parsed as before, within the existing receive buffer size

### Requirement: The new device state is reachable over serial
Every setting the on-device menu can change SHALL also be readable and writable over the serial console, so the device can be configured and tested from a host.

#### Scenario: Display mode round-trips
- **WHEN** the 12/24-hour mode is set over serial and read back
- **THEN** the value read matches the value set, and the display and mode LED reflect it

#### Scenario: Alarm time round-trips
- **WHEN** the alarm time is set over serial and read back
- **THEN** the value read matches the value set

#### Scenario: Armed state round-trips
- **WHEN** the alarm is armed over serial and the armed state is read back
- **THEN** it reports armed, and the bottom LED is lit

#### Scenario: Menu changes are visible to the host
- **WHEN** a setting is changed using the buttons and then read over serial
- **THEN** the value read reflects the change made on the device

### Requirement: Commands validate their arguments
A command given a missing or out-of-range argument SHALL report an error and leave device state unchanged, rather than applying a partial or clamped value.

#### Scenario: Missing argument
- **WHEN** a command requiring an argument is sent without one
- **THEN** it replies with an error and no state changes

#### Scenario: Out-of-range argument
- **WHEN** a command is sent a value outside the valid range for its field
- **THEN** it replies with an error naming the problem and no state changes

#### Scenario: Unrecognized commands are still reported
- **WHEN** an unknown command is sent
- **THEN** the existing unrecognized-command reply is produced
