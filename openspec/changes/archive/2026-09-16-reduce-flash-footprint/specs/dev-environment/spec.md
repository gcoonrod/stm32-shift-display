## ADDED Requirements

### Requirement: Flash headroom is known and protected
The firmware's flash usage SHALL be reportable on demand, expressed against the device's fixed capacity. A change that materially increases it SHALL be a deliberate choice rather than something discovered when a build stops fitting.

#### Scenario: Usage is reportable
- **WHEN** the environment readiness check is run
- **THEN** it reports the current flash usage and the remaining headroom, without requiring a separate build step to be remembered

#### Scenario: A regression is attributable
- **WHEN** flash usage grows materially between two builds
- **THEN** the growth can be attributed to the change that caused it, because the figure is recorded per change rather than only observed at the end

#### Scenario: The budget is stated, not implied
- **WHEN** someone asks how much room is left
- **THEN** the answer comes from the documented capacity and current usage rather than from reading a build log

#### Scenario: Headroom survives a size-reduction effort
- **WHEN** space is reclaimed by a change whose purpose is reclaiming it
- **THEN** the new figure is recorded, so later work starts from a known baseline rather than from the last number anyone happened to notice
