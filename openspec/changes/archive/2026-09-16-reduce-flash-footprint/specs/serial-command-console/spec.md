## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: Command replies do not depend on formatted-output machinery
Command replies SHALL be produced without pulling a general-purpose formatted-output implementation into the image. The text of every reply is fixed by "The existing command set is preserved exactly"; how it is assembled is not, and the assembly method SHALL be chosen so that the cost of the whole `printf` family is not paid for `%d` and `%s`.

#### Scenario: The formatted-output engine is absent
- **WHEN** the linked image is inspected for the general-purpose formatted-output implementation
- **THEN** it is not present

#### Scenario: Replies are unchanged by the substitution
- **WHEN** every command that previously used formatted output is exercised, including its error paths
- **THEN** each reply is byte-identical to the previous firmware's
