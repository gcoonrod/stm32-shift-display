## Why

This repo was developed on a different machine, and the toolchain it assumed is not present here: PlatformIO Core is gone (`~/.platformio` holds only a stale cache), `software/.venv` points at a Homebrew `python@3.12.4` that no longer exists, and no PlatformIO VS Code extension is installed. At the same time the committed documentation has drifted from the code and hardware — the README still describes board rev 1 while the schematic and the firmware pin map are rev 2. The board and an ST-Link are physically attached to this Mac right now, so the only thing standing between here and resuming firmware work is a corrective pass over the environment and the docs.

## What Changes

**Environment**
- Install PlatformIO Core standalone via `uv tool install platformio` so `pio` is on PATH and usable from a terminal or an agent session, not only from an IDE.
- Verify the restored toolchain end to end: `pio run` builds, `pio run -t upload` flashes over the attached ST-Link, and the board's CDC port enumerates and answers commands.
- **BREAKING (local only)**: delete and replace `software/.venv`, which is unrunnable, with a `uv`-managed environment for the timesync tool. `requirements.txt` gains a `pyproject.toml`/`uv.lock` companion so the interpreter and deps are pinned rather than inherited from whatever `python3` resolves to.
- Record the verified device identities — board CDC `/dev/cu.usbmodem49795F7330551` (STM32 `GENERIC_F103C8TX CDC in FS Mode`, serial `49795F733055`) and ST-Link (serial `0673FF373841423043111349`) — and stop `timesync.py` from silently depending on a hardcoded port.

**Documentation**
- Correct the README: it presents rev 1 as current while `hardware/stm32-shift-display.kicad_sch` is rev 2 and `hardware/assets/STM32ShiftDisplayRev2.pdf` exists. Preserve the rev 1 / PCBWay sponsorship write-up as history rather than deleting it.
- Document what actually exists today: the USB CDC command set (`TEST`, `GT`, `ST`, `SO`, `GO`), the rev 2 pin map, the build/flash/monitor commands, and the timesync workflow.
- State plainly which parts of the firmware are unfinished (the menu FSM, brightness/PWM) so the gap between the README's framing and the code is visible.
- Note that KiCad 10.0.3 is installed here while `hardware/` is KiCad 8 format (sch `20231120`, pcb `20240108`), and that opening-and-saving migrates those files irreversibly. **The hardware files are not opened or migrated by this change.**
- Keep `CLAUDE.md` consistent with the above, and fill in `openspec/config.yaml`'s empty `context:` block so future changes inherit the stack and conventions.
- Add the untracked local cruft (`.DS_Store`, editor/agent dirs) to `.gitignore` so the working tree is clean enough to see real changes.

Explicitly out of scope: fixing the firmware bugs the survey turned up (month off-by-one in `ST`/`GT`, `setup_rtc()` reading uninitialized globals, the `timesync.py` operator-precedence guard) and building the menu FSM. Those are documented here and belong to follow-up changes.

## Capabilities

### New Capabilities
- `dev-environment`: What a workstation must have, and how it is verified, to build, flash, and communicate with the STM32 Shift Clock from this repo — PlatformIO Core, the ARM toolchain, the Python host-tool environment, and the attached ST-Link/CDC devices.
- `project-documentation`: Requirements for the repo's committed documentation to describe the current hardware revision, firmware behavior, and toolchain, including how documented facts stay verifiable against the source.

### Modified Capabilities
_None — `openspec/specs/` is empty; this is the first change in the repo._

## Impact

- **Docs**: `README.md`, `CLAUDE.md`, `openspec/config.yaml`, and a new environment-setup document.
- **Host tooling**: `software/timesync/` — `.venv` replaced, `pyproject.toml`/`uv.lock` added, `timesync.py` port handling made explicit. Behavior of the `-H`/`-U`/`--csv` flags is unchanged.
- **Repo hygiene**: `.gitignore`.
- **Machine state (outside the repo)**: `~/.platformio` repopulates with `toolchain-gccarmnoneeabi` and the `ststm32` platform on the first build; `pio` installs into the uv tool dir.
- **Untouched**: `firmware/src/`, `firmware/lib/`, `firmware/platformio.ini`, all of `hardware/` and `mechanical/`. No firmware logic changes, so a successful flash of the current source is the regression test.
