## MODIFIED Requirements

### Requirement: Firing is signalled visibly and can be dismissed
The board has no sounder, so the alarm SHALL signal visually: the display flashes and the bottom LED breathes, fading smoothly up and down rather than switching on and off. Any button press SHALL dismiss it.

#### Scenario: Visual signal
- **WHEN** the alarm is firing
- **THEN** the display flashes and the bottom LED breathes, distinguishing it from the steady armed indication

#### Scenario: Breathing is smooth
- **WHEN** the bottom LED is breathing
- **THEN** its brightness changes in steps fine enough that the fade reads as continuous rather than as visible stepping

#### Scenario: Breathing respects the configured brightness
- **WHEN** the alarm breathes while the indicator brightness is set low
- **THEN** the breath peaks at the configured brightness rather than overriding it to full

#### Scenario: Any button dismisses
- **WHEN** any of the three buttons is pressed while the alarm is firing
- **THEN** the signalling stops, the display returns to showing the time, and that button press does not also act on the menu

#### Scenario: The alarm stays armed after dismissal
- **WHEN** a firing alarm is dismissed
- **THEN** the alarm remains armed for the following day

#### Scenario: Timekeeping is unaffected
- **WHEN** the alarm fires and is left signalling
- **THEN** the clock continues to keep time, and the time read back over serial is correct

#### Scenario: Firing is observable from a host
- **WHEN** the alarm is firing and the LED state is read over serial repeatedly
- **THEN** the reported bottom-LED value varies as the LED breathes, so a firing alarm can be detected without looking at the board
