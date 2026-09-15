# project-documentation Specification

## Purpose

Defines what the repository's committed documentation must assert and keep true: the current hardware revision, the MCU pin map, the USB CDC command set, commands that run as written, honest labeling of unfinished firmware behavior and toolchain version drift, clear marking of machine-specific facts, and agent-facing context that reflects the project.

## Requirements

### Requirement: Documentation states the current hardware revision
The committed documentation SHALL identify the current board revision as the one the KiCad source describes, and SHALL present superseded revisions as history rather than as the current state.

#### Scenario: Current revision matches the design files
- **WHEN** the README's stated current revision is compared with the `rev` field in `hardware/stm32-shift-display.kicad_sch`
- **THEN** they agree

#### Scenario: Earlier revision history is preserved
- **WHEN** the README is read after the correction
- **THEN** the rev 1 design narrative and the PCBWay sponsorship acknowledgement are still present, marked as prior-revision history

#### Scenario: Revision assets are reachable
- **WHEN** a reader looks for the current schematic
- **THEN** the documentation links the rev 2 schematic asset rather than only rev 1 renders

### Requirement: Documented pin map matches the firmware
Documentation of the MCU-to-peripheral pin assignments SHALL match the pin definitions in the firmware source, and SHALL name the source file as the authority.

#### Scenario: Pin map agrees with source
- **WHEN** each documented pin assignment is compared with the corresponding `#define` in `firmware/src/main.cpp`
- **THEN** every assignment agrees

#### Scenario: Authority is named
- **WHEN** a reader needs to confirm a pin assignment
- **THEN** the documentation points at the firmware defines and the KiCad schematic as the source of truth

### Requirement: Documented serial protocol matches the firmware
The documented USB CDC command set SHALL list exactly the commands the firmware registers, with their arguments, replies, and framing.

#### Scenario: Command list is complete and accurate
- **WHEN** the documented commands are compared with the `SerialCommand` objects registered in `setup()`
- **THEN** the sets match, and each documented command's argument and reply match its handler

#### Scenario: Framing is documented
- **WHEN** a reader wants to talk to the board by hand
- **THEN** the documentation states the `\r\n` terminator, the space-separated argument format, and the command buffer size

#### Scenario: Unregistered handlers are not presented as available
- **WHEN** handlers exist in the source but are never registered
- **THEN** the documentation does not list them as usable commands

### Requirement: Documented commands are executable as written
Every build, flash, monitor, and host-tool command in the documentation SHALL run as written on a machine configured per the environment setup, with no undocumented prerequisite.

#### Scenario: Commands run verbatim
- **WHEN** each documented command is copied and run from the stated directory on a configured machine
- **THEN** it executes without modification

#### Scenario: Missing prerequisites are stated, not assumed
- **WHEN** a documented command depends on a tool that is not installed by default
- **THEN** the documentation names that prerequisite and how to install it

### Requirement: Unfinished firmware behavior is labeled
The documentation SHALL distinguish firmware behavior that works today from behavior that is incomplete, so a reader is not misled by aspirational description.

#### Scenario: Incomplete areas are called out
- **WHEN** a reader looks for the state of the on-device menu and display brightness control
- **THEN** the documentation identifies them as unimplemented and names the code that would carry them

#### Scenario: Known defects are recorded
- **WHEN** a defect has been identified in committed code but not yet fixed
- **THEN** the documentation records it with enough detail to locate it in the source

### Requirement: Toolchain version drift is recorded
The documentation SHALL record where an installed tool's version differs from the format of the committed files it opens, together with the consequence of using it.

#### Scenario: KiCad version mismatch is documented
- **WHEN** a reader opens the hardware design
- **THEN** the documentation states the KiCad file-format version of the committed files, the version installed on this machine, and that saving migrates the files irreversibly

#### Scenario: Stance on migration is explicit
- **WHEN** a reader decides whether to open the hardware files
- **THEN** the documentation states that the files are deliberately left unmigrated and what to do if migration becomes necessary

### Requirement: Machine-specific facts are marked as such
Documentation SHALL distinguish facts about this particular workstation from facts about the project, so a reader on another machine knows what will not transfer.

#### Scenario: Device identifiers are attributed
- **WHEN** a specific serial device node or USB serial number appears in the documentation
- **THEN** it is identified as observed on this machine, alongside how to discover the equivalent elsewhere

### Requirement: Agent and planning context reflect the project
The repository's agent-facing context SHALL describe the project's stack, workflow, and conventions, so generated artifacts inherit them instead of re-deriving them.

#### Scenario: OpenSpec context is populated
- **WHEN** `openspec/config.yaml` is read
- **THEN** its `context` block describes the firmware stack, the host tooling, and the repository's branch and licensing conventions

#### Scenario: Agent guidance agrees with the corrected documentation
- **WHEN** `CLAUDE.md` is compared with the README and the environment setup documentation
- **THEN** they state the same hardware revision, commands, and prerequisites
