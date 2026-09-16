# serial-command-console Specification

## Purpose

Defines the USB CDC command console: the existing `TEST`, `GT`, `ST`, `SO`, and `GO` commands preserved exactly so host tooling keeps working, serial access to every setting the on-device menu can change, and argument validation that rejects missing or out-of-range values instead of applying partial or clamped ones.

## Requirements

### Requirement: The existing command set is preserved exactly
The commands `TEST`, `GT`, `ST`, `SO`, and `GO` SHALL keep their current names, arguments, replies, and framing. Host tooling written against them SHALL continue to work without modification. Their internal implementation may change freely, provided the bytes on the wire do not.

The one permitted exception is the calendar month, which SHALL be represented consistently as 1–12 by every path that reads or writes it. A date set over serial and a date set from the on-device menu SHALL agree with each other and with the RTC.

#### Scenario: Existing commands answer as before
- **WHEN** `TEST`, `GT`, and `GO` are sent
- **THEN** they reply `TEST`, a Unix timestamp, and the timezone offset respectively, as they did before this change

#### Scenario: Setting the time still works
- **WHEN** `ST` is sent with a Unix timestamp
- **THEN** it replies `OK` and the clock is set, and reading the time straight back with `GT` returns the timestamp that was set

#### Scenario: The host tool is unaffected
- **WHEN** the existing timesync tool is run against the device
- **THEN** it reports the device time and corrects it exactly as it did before this change, with no edit to the tool

#### Scenario: Framing is unchanged
- **WHEN** commands are sent terminated by carriage return and newline with space-separated arguments
- **THEN** they are parsed as before, within the existing receive buffer size

#### Scenario: The two ways of setting the date agree
- **WHEN** a date is set from the on-device menu and then read over serial
- **THEN** the month reported matches the month that was entered, and the same holds in reverse for a date set over serial and then viewed in the menu

#### Scenario: Replies are byte-identical
- **WHEN** any command's reply is compared against the same command's reply from the previous firmware
- **THEN** the bytes match exactly, including error messages, separators and line endings

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

### Requirement: Persisted settings are diagnosable from a host
The device SHALL report the raw contents of its settings storage over the serial console, including the values read at start-up before any code could overwrite them. Settings loss that cannot be reproduced on demand is otherwise indistinguishable from settings that were never saved, and the two have opposite causes.

#### Scenario: Start-up values are reported as they were read
- **WHEN** the raw settings storage is read over serial
- **THEN** the report includes what the start-up read returned, distinct from what the storage holds at the time of the query

#### Scenario: A self-inflicted overwrite is distinguishable from lost storage
- **WHEN** settings have unexpectedly returned to their defaults
- **THEN** the report shows whether start-up saw valid stored settings, which separates storage that failed to retain from firmware that overwrote good values with defaults

#### Scenario: The expected validity marker is reported alongside
- **WHEN** the report is read
- **THEN** it includes the value the firmware treats as marking initialised storage, so a mismatch is visible without consulting the source

### Requirement: Command replies do not depend on formatted-output machinery
Command replies SHALL be produced without pulling a general-purpose formatted-output implementation into the image. The text of every reply is fixed by "The existing command set is preserved exactly"; how it is assembled is not, and the assembly method SHALL be chosen so that the cost of the whole `printf` family is not paid for `%d` and `%s`.

#### Scenario: The formatted-output engine is absent
- **WHEN** the linked image is inspected for the general-purpose formatted-output implementation
- **THEN** it is not present

#### Scenario: Replies are unchanged by the substitution
- **WHEN** every command that previously used formatted output is exercised, including its error paths
- **THEN** each reply is byte-identical to the previous firmware's
