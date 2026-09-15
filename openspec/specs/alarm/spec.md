# alarm Specification

## Purpose

Defines the device's single daily alarm: an alarm time settable from the on-device menu and over the serial console, an armed state independent of that time, firing at the set time each day without re-arming, a visual signal that any button dismisses, and persistence of the alarm and 12/24-hour settings across power loss in the backup domain.

## Requirements

### Requirement: A daily alarm time can be set
The device SHALL hold a single alarm time expressed as an hour and a minute, settable from the on-device menu and over the serial console.

#### Scenario: Setting from the menu
- **WHEN** the alarm time is set through the menu and committed
- **THEN** reading the alarm back reports the hour and minute that were set

#### Scenario: Setting over serial
- **WHEN** the alarm time is set over the serial console
- **THEN** the on-device menu shows the same value

#### Scenario: Out-of-range values are rejected
- **WHEN** an hour outside 0–23 or a minute outside 0–59 is supplied over serial
- **THEN** the command reports an error and the stored alarm time is unchanged

### Requirement: The alarm can be armed and disarmed
Arming SHALL be independent of the alarm time, so the time can be set without the alarm sounding and the alarm can be disarmed without losing the time. The armed state SHALL be reflected by the bottom LED.

#### Scenario: Arming lights the indicator
- **WHEN** the alarm is armed
- **THEN** the bottom LED is lit steadily

#### Scenario: Disarming clears the indicator
- **WHEN** the alarm is disarmed
- **THEN** the bottom LED is unlit and the alarm time is retained

#### Scenario: Changing the time while armed
- **WHEN** the alarm time is changed while armed
- **THEN** the alarm remains armed and will next fire at the new time

### Requirement: The alarm fires at the set time each day
While armed, the device SHALL signal when the time of day reaches the alarm time, and SHALL do so again on subsequent days without needing to be re-armed.

#### Scenario: Firing
- **WHEN** the time of day reaches the alarm time while armed
- **THEN** the device enters the firing state

#### Scenario: Firing repeats daily
- **WHEN** an alarm has fired and been dismissed, and twenty-four hours pass
- **THEN** the alarm fires again without the user re-arming it

#### Scenario: Disarmed alarms do not fire
- **WHEN** the time of day reaches the alarm time while disarmed
- **THEN** nothing happens

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

### Requirement: Alarm and mode settings survive power loss
The alarm time, the armed state, and the 12/24-hour mode SHALL persist across a power cycle, stored in backup domain registers held up by the coin cell. Storage SHALL NOT use the registers the RTC library reserves for its own date storage.

#### Scenario: Settings survive a power cycle
- **WHEN** the alarm is armed at a set time in 12-hour mode and power is removed and restored
- **THEN** the alarm is still armed at the same time and the display is still in 12-hour mode

#### Scenario: The date is not corrupted
- **WHEN** settings are written and the device is power cycled
- **THEN** the date read back is the date that was set, confirming the settings did not overwrite the library's reserved date registers

#### Scenario: Uninitialized backup memory is detected
- **WHEN** the device starts with backup memory that has never held settings
- **THEN** defaults are applied rather than arbitrary values being treated as settings
