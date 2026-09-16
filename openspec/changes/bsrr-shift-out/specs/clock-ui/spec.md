## MODIFIED Requirements

### Requirement: The output-enable line has a single owner
The shift registers' output-enable line SHALL be driven by exactly one mechanism. Any operation that blanks or restores the display SHALL go through that owner rather than writing the pin directly, because a direct write would reconfigure the pin away from its timer and silently stop the brightness control.

The output-enable line shares a GPIO port with the shift registers' data, clock, latch and clear lines. Code driving those lines SHALL address only the pins it owns, and SHALL NOT write the port as a whole, because a whole-port write reaches across the output-enable line whatever its intent.

#### Scenario: Blanking and restoring go through the owner
- **WHEN** code blanks or re-enables the display
- **THEN** it does so through the brightness owner, and the pin is never written directly

#### Scenario: Restoring returns to the configured brightness
- **WHEN** the display is blanked and then re-enabled
- **THEN** it returns to the configured brightness rather than to full

#### Scenario: The display is blank through reset
- **WHEN** the MCU is in reset or has not yet configured the pin
- **THEN** the display is blank rather than showing whatever the shift registers powered up holding

#### Scenario: Shifting data does not disturb brightness
- **WHEN** data is shifted out, latched, or the registers are cleared
- **THEN** the output-enable line is unaffected and the display's brightness does not change

#### Scenario: Port-wide writes are not used
- **WHEN** the display driver drives its pins
- **THEN** it addresses them individually rather than writing the whole port, so no operation can reach a pin it does not own

## ADDED Requirements

### Requirement: The shift-out stays within the shift registers' timing limits
The rate at which data is clocked into the shift registers SHALL be slow enough for the devices fitted, at the voltage they run at, with margin. The limit SHALL be established by measurement on the assembled board rather than by calculation alone, because the binding constraint is the serial propagation delay accumulated through a chain of six devices, and the parts are specified at a higher voltage than the board supplies.

#### Scenario: The safe rate is measured, not assumed
- **WHEN** the shift rate is chosen
- **THEN** it is derived from an observed failure point on the assembled hardware, and the margin between the two is recorded

#### Scenario: Corruption is made visible during measurement
- **WHEN** the shift rate is swept to find the failure point
- **THEN** the pattern on the display is one where corruption is unmistakable, since the end of the chain is not wired back and no electrical readback exists

#### Scenario: The display is correct at the chosen rate
- **WHEN** the display is exercised at the chosen rate across all six digits and the full glyph set
- **THEN** every character renders correctly, with no intermittent wrong segments

#### Scenario: The rate is stated where it can be found
- **WHEN** someone asks why the shift runs at the rate it does
- **THEN** the measured failure point, the chosen rate and the margin between them are recorded, rather than left as an unexplained constant
