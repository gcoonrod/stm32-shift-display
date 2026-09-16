## ADDED Requirements

### Requirement: The display refreshes without CPU involvement
The display SHALL be refreshed continuously by a hardware peripheral replaying a precomputed waveform, rather than by the CPU clocking bits out on demand. The refresh SHALL continue at its full rate while the CPU is busy elsewhere, and SHALL NOT require the main loop to run at any particular rate.

Changing what is displayed SHALL be a matter of writing to the waveform buffer. No caller SHALL be required to shift or latch anything for the change to appear.

#### Scenario: The refresh survives a busy CPU
- **WHEN** the main loop is occupied for longer than one refresh period
- **THEN** the display continues refreshing at the same rate, with no flicker, dimming or corruption

#### Scenario: Writing the buffer is enough
- **WHEN** new content is written to the display
- **THEN** it appears without the caller shifting or latching, within one refresh period

#### Scenario: The refresh rate is above the flicker threshold
- **WHEN** the display is viewed at the edge of vision, or while the eyes are moving across it
- **THEN** no flicker is perceptible at any brightness

#### Scenario: Nothing is shown before the engine is configured
- **WHEN** the MCU has reset and the engine has not yet been started
- **THEN** the display is blank rather than showing whatever the shift registers powered up holding

### Requirement: Individual digits can be dimmed relative to the display
The display SHALL support dimming individual character positions relative to the rest of the display, by lighting a digit in only some of the refresh cycle's time slices rather than all of them.

A digit's relative brightness SHALL be proportional to the number of slices it is lit in, and SHALL be independent of which slices those are and of the phase relationship with the output-enable line.

A digit at full relative brightness SHALL be indistinguishable from the same digit on a display with no per-digit dimming in use.

#### Scenario: A dimmed digit is visibly dimmer
- **WHEN** one character position is set below full relative brightness while the others stay at full
- **THEN** that digit is visibly dimmer than the others, and remains legible

#### Scenario: Full relative brightness changes nothing
- **WHEN** every character position is at full relative brightness
- **THEN** the display is indistinguishable from the same content shown without per-digit dimming

#### Scenario: Relative brightness is proportional
- **WHEN** a digit's slice count is stepped from lowest to highest
- **THEN** its brightness increases monotonically, and equal slice counts on different character positions produce equal brightness

#### Scenario: A dimmed digit does not flicker
- **WHEN** a digit is held at a low relative brightness
- **THEN** it shows no flicker, beat or shimmer distinct from the rest of the display

#### Scenario: Zero slices blanks the digit
- **WHEN** a character position is set to zero slices
- **THEN** that position shows no lit segments, and the other positions are unaffected

### Requirement: The two brightness mechanisms compose without visible interaction
Global brightness on the output-enable line and per-digit brightness in the data path are independent modulations that multiply. Their periods SHALL be related such that every time slice contains a whole number of output-enable periods, so that each slice carries the same output-enable on-time regardless of the phase between them.

The timer constants that establish this relationship SHALL be derived at build time from the output-enable frequency and the core clock, rather than written as independent literals. The build SHALL fail if the relationship is not exact.

#### Scenario: No beat between the two rates
- **WHEN** the display is held at any combination of global brightness and per-digit brightness
- **THEN** no slow flutter, beat or breathing is visible that is not commanded

#### Scenario: Digits at equal slice counts are equally bright
- **WHEN** two character positions are set to the same slice count at any global brightness
- **THEN** they are equally bright, with no fixed difference attributable to where their slices fall

#### Scenario: Global brightness still covers everything
- **WHEN** the global display brightness is changed while per-digit levels are in use
- **THEN** every digit changes brightness together, keeping their relative proportions

#### Scenario: Retuning the output-enable frequency cannot silently break the lock
- **WHEN** the output-enable PWM frequency is changed and the slice period would no longer contain a whole number of output-enable periods
- **THEN** the build fails, rather than producing firmware with per-digit brightness errors

## MODIFIED Requirements

### Requirement: Display brightness is user-settable
The brightness of the seven-segment display SHALL be adjustable by the user, settable from the on-device menu and over the serial console, and SHALL persist across a power cycle. It is a single level covering the whole display, because the output-enable line is shared by all six digits.

Per-digit brightness, where it is used, SHALL multiply this level rather than replace it: the user's display brightness setting SHALL remain the control that dims every digit together, and no per-digit level SHALL make a digit brighter than that setting allows.

#### Scenario: Adjusting from the menu
- **WHEN** the display brightness is changed through the menu and committed
- **THEN** the display is visibly dimmer or brighter, and reading the level back reports the new value

#### Scenario: The display previews itself
- **WHEN** the level is being adjusted in the editor
- **THEN** the display tracks the value as it changes, so the choice is made by eye against the thing being adjusted

#### Scenario: Round-trips over serial
- **WHEN** the display level is set over serial and read back
- **THEN** the value read matches the value set, and a value outside the valid range is rejected without changing the level

#### Scenario: Survives a power cycle
- **WHEN** the level is set and power is removed and restored
- **THEN** the display returns at the level that was set

#### Scenario: Levels are perceptually even
- **WHEN** the level is stepped from lowest to highest
- **THEN** each step is a comparable change in apparent brightness, rather than the whole range appearing to happen at the bottom of the scale

#### Scenario: The dimmest level is still legible
- **WHEN** the level is at its minimum
- **THEN** the time can still be read in a dark room, so a dimmed clock is never mistaken for a failed one

#### Scenario: No visible flicker
- **WHEN** the display is viewed at any brightness, including at the edge of vision and while the eyes are moving
- **THEN** no flicker is perceptible

#### Scenario: Indicator brightness is separate
- **WHEN** the display brightness is changed
- **THEN** the indicator LEDs are unaffected, and vice versa

#### Scenario: A per-digit level cannot escape the global one
- **WHEN** the global display brightness is set to its minimum while a digit is at full relative brightness
- **THEN** that digit is no brighter than the global minimum allows

### Requirement: The output-enable line has a single owner
The shift registers' output-enable line SHALL be driven by exactly one mechanism. Any operation that blanks or restores the display SHALL go through that owner rather than writing the pin directly, because a direct write would reconfigure the pin away from its timer and silently stop the brightness control.

The output-enable line shares a GPIO port with the shift registers' data, clock, latch and clear lines. Code driving those lines SHALL address only the pins it owns, and SHALL NOT write the port as a whole, because a whole-port write reaches across the output-enable line whatever its intent.

This SHALL hold for writes performed by a peripheral as well as by the CPU. Where the port is driven from precomputed words replayed by hardware, those words SHALL be constructed so that they cannot address the output-enable pin, and that property SHALL be enforced at build time rather than left to inspection — nothing in the instruction stream shows such a mistake.

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

#### Scenario: Peripheral-written words cannot reach the output-enable pin
- **WHEN** a word destined for the port is constructed for replay by hardware
- **THEN** it addresses only the data, clock, latch and clear pins, and a word that would set or clear the output-enable pin fails the build

### Requirement: The shift-out stays within the shift registers' timing limits
The rate at which data is clocked into the shift registers SHALL be slow enough for the devices fitted, at the voltage they run at, with margin. The limit SHALL be established by measurement on the assembled board rather than by calculation alone, because the binding constraint is the serial propagation delay accumulated through a chain of six devices, and the parts are specified at a higher voltage than the board supplies.

Where the rate is set by a timer rather than by instruction timing, it SHALL be a chosen frequency derived from the core clock, not a consequence of what the compiler emitted. The chosen rate SHALL still be justified against the measured failure point.

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

#### Scenario: A timer-set rate is exact
- **WHEN** the shift clock is produced by a timer
- **THEN** its frequency follows from the core clock and the timer's reload value, and is stated as a number rather than estimated from instruction counts
