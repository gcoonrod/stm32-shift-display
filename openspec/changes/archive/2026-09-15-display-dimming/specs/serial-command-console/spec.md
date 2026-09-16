## ADDED Requirements

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
