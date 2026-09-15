# dev-environment Specification

## Purpose

Defines what a working development environment for this project must provide: PlatformIO Core invocable from a plain shell, a firmware build that succeeds from a clean tree, flashing over SWD via ST-Link, a board reachable over USB CDC, a reproducible Python environment for the host tooling in `software/timesync/`, explicit serial port selection, and a single documented readiness check.

## Requirements

### Requirement: PlatformIO Core is invocable from a shell
The development environment SHALL provide PlatformIO Core as a `pio` executable resolvable on `PATH` from a non-interactive shell, so that builds and flashes can be driven from a terminal or an agent session without an IDE.

#### Scenario: pio resolves in a fresh shell
- **WHEN** `pio --version` is run in a newly started shell
- **THEN** it prints a PlatformIO Core version and exits 0

#### Scenario: PlatformIO is not tied to an editor installation
- **WHEN** the `pio` executable's location is inspected
- **THEN** it resolves outside any VS Code extension directory, so terminal and IDE use do not depend on each other

### Requirement: Firmware builds from a clean tree
The repository SHALL build to completion with the toolchain installed on this machine, starting from a tree with no prior build artifacts.

#### Scenario: Clean build succeeds
- **WHEN** `firmware/.pio/` is removed and `pio run -d firmware` is run
- **THEN** PlatformIO fetches the `ststm32` platform, the ARM toolchain, and the four `lib_deps`, and the build exits 0

#### Scenario: Build produces a flashable image
- **WHEN** the build completes
- **THEN** `firmware/.pio/build/bluepill_f103c8_128k/firmware.elf` and `firmware.bin` exist and are newer than the build start

### Requirement: Firmware flashes over SWD
The environment SHALL flash the connected board through the attached ST-Link, since the board offers no DFU or bootloader upload path.

#### Scenario: Upload to the attached board
- **WHEN** the ST-Link is connected to the board's SWD header and `pio run -d firmware -t upload` is run
- **THEN** the upload completes successfully and the MCU restarts running the new image

#### Scenario: Missing debugger is reported clearly
- **WHEN** an upload is attempted with no ST-Link attached
- **THEN** the failure names the missing probe rather than presenting as a build error

### Requirement: The board answers over USB CDC
The environment SHALL be able to open the board's USB CDC port and exchange commands with the running firmware.

#### Scenario: Device reports its time
- **WHEN** `GT\r\n` is written to the board's CDC port
- **THEN** the device replies with a single integer Unix timestamp

#### Scenario: CDC port is discoverable
- **WHEN** the board is attached and serial devices are enumerated
- **THEN** the STM32 CDC device is identifiable by its USB descriptor rather than only by a machine-specific device-node name

### Requirement: Host tooling runs from a reproducible environment
The `software/timesync` tool SHALL run from an environment that pins its interpreter and dependencies and is recreatable from files committed to the repository, without depending on whichever interpreter happens to be first on `PATH`.

#### Scenario: Environment is recreated from a clean checkout
- **WHEN** the environment directory is absent and the documented setup command is run in `software/timesync/`
- **THEN** the environment is created with the pinned interpreter and `pyserial` and `ntplib` importable

#### Scenario: Tool runs against the device
- **WHEN** the board is attached and the timesync tool is run in read-only mode
- **THEN** it prints the NTP time, the device time, and the skew between them, and exits 0

#### Scenario: No dependence on an absent interpreter
- **WHEN** the environment is inspected for the interpreter it resolves to
- **THEN** that interpreter exists on this machine, and no committed file references a Homebrew `python@3.12.4` path

### Requirement: Serial port selection is explicit
The timesync tool SHALL NOT silently fall back to a device node hardcoded for one machine. It SHALL select a port that it discovered, or one the caller supplied, and SHALL fail with an actionable message when neither is available.

#### Scenario: Port supplied by the caller
- **WHEN** the tool is invoked with an explicit port argument
- **THEN** it uses that port and does not consult any default

#### Scenario: No device attached
- **WHEN** the tool is run with no port argument and no STM32 CDC device is present
- **THEN** it exits non-zero with a message naming the missing device and how to specify a port

### Requirement: Environment readiness is verifiable in one step
The repository SHALL provide a documented way to check the prerequisites for firmware and host-tool development, reporting each one's presence rather than failing at the first gap.

#### Scenario: Readiness check on a configured machine
- **WHEN** the documented check is run on a machine with the toolchain installed
- **THEN** it reports PlatformIO, the Python environment, the attached ST-Link, and the attached CDC device as present

#### Scenario: Readiness check on an unconfigured machine
- **WHEN** the check is run where PlatformIO is absent
- **THEN** it reports that specific prerequisite as missing and names the command that installs it
