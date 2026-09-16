## MODIFIED Requirements

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
