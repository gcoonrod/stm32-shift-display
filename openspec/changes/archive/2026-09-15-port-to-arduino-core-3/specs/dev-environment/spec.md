## MODIFIED Requirements

### Requirement: Firmware builds from a clean tree
The repository SHALL build to completion with the toolchain installed on this machine, starting from a tree with no prior build artifacts.

#### Scenario: Clean build succeeds
- **WHEN** `firmware/.pio/` is removed and `pio run -d firmware` is run
- **THEN** PlatformIO fetches the `ststm32` platform, the ARM toolchain, and the three `lib_deps`, and the build exits 0

#### Scenario: Build produces a flashable image
- **WHEN** the build completes
- **THEN** `firmware/.pio/build/bluepill_f103c8_128k/firmware.elf` and `firmware.bin` exist and are newer than the build start

## ADDED Requirements

### Requirement: The toolchain is not pinned to a superseded core line
The firmware SHALL build against a currently published Arduino STM32 core, and SHALL NOT depend on a platform version whose framework package the PlatformIO registry no longer serves. Where a version constraint is kept, it SHALL be a floor or a range that continues to resolve, not a pin to a single superseded release.

#### Scenario: Build resolves without a platform pin
- **WHEN** `firmware/platformio.ini` names no exact platform version and `pio run -d firmware` is run
- **THEN** PlatformIO resolves the current `ststm32` platform and the build exits 0

#### Scenario: Library constraints admit the versions that work
- **WHEN** the `lib_deps` constraints are resolved against the registry
- **THEN** each one resolves to a version that compiles against the core in use, and no dependency is listed that the project neither includes nor calls

#### Scenario: Registry purge does not strand the build
- **WHEN** a framework package older than the one in use is purged from the registry
- **THEN** the project's own constraints still resolve, because they do not name it

### Requirement: A toolchain change is verified on hardware
A change that alters the compiler, core, or library versions the firmware is built against SHALL be verified by running the resulting image on the board, not by compilation alone. Compiling proves the source is accepted; it does not prove the peripherals still behave.

#### Scenario: The image runs after a toolchain change
- **WHEN** firmware built against the new toolchain is flashed to the board
- **THEN** the display shows the time and the digits advance, confirming the RTC seconds interrupt still fires

#### Scenario: The command console survives the change
- **WHEN** the board is addressed over USB CDC after the change
- **THEN** it enumerates with USB descriptor `0483:5740` and answers `TEST` with `TEST` and `GT` with an integer Unix timestamp

#### Scenario: Timekeeping survives the change
- **WHEN** the clock is set over CDC and read back after at least a minute
- **THEN** the time has advanced by the elapsed interval, confirming the LSE clock source and RTC configuration still work

#### Scenario: A regression is attributable
- **WHEN** behavior differs from before the toolchain change
- **THEN** the change under test contains no unrelated source edits, so the difference is attributable to the toolchain
