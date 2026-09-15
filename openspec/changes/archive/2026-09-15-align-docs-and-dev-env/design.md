## Context

The repository was cloned onto a Mac that is not the machine it was developed on, and the environment it assumes is largely absent. A survey of this machine found:

| Area | Repo assumes | This Mac has |
| --- | --- | --- |
| PlatformIO Core | `pio` on PATH; `~/.platformio/packages/toolchain-gccarmnoneeabi` (referenced by the generated `firmware/.vscode/launch.json`) | `~/.platformio` contains only `.cache/http`; no `pio` on PATH, no `penv` |
| PlatformIO IDE | `firmware/.vscode/extensions.json` recommends `platformio.platformio-ide` | 22 VS Code extensions installed, none of them PlatformIO or Cortex-Debug |
| Python | `software/.venv` built from Homebrew `python@3.12.4` | That interpreter is gone (no `/opt/homebrew/Cellar/python@3.12`); `python3` is pyenv 3.12.13; the venv's `bin/python*` symlinks are missing, so `.venv` cannot execute anything |
| Env manager | `pip install -r requirements.txt` | `uv` 0.11.12 available |
| KiCad | files written in KiCad 8 format (sch `20231120`, pcb `20240108`) | KiCad 10.0.3 |
| FreeCAD | `mechanical/desk-stand.FCStd` | not installed |
| Hardware | — | Board attached: VID `0x0483` PID `0x5740`, serial `49795F733055`, at `/dev/cu.usbmodem49795F7330551`. ST-Link attached: VID `0x0483` PID `0x3752`, serial `0673FF373841423043111349`, at `/dev/cu.usbmodem14102` |

`firmware/.pio/` still holds `idedata.json` and a checksum from the old machine, pointing at toolchain paths that do not exist — stale state that will mislead tooling and readers until it is cleared.

Separately, the committed documentation drifted from the design: `README.md` presents rev 1 as current while `hardware/stm32-shift-display.kicad_sch` is rev 2 (the `.kicad_pcb` title block still says rev 1, so the board files disagree with each other), and nothing documents the USB CDC command set, the rev 2 pin map, or the build and flash commands.

Constraints: the physical board is fabricated and working, and there is no second unit. The firmware is mid-development but flashable. No test suite exists, so "it builds, it flashes, it answers over CDC" is the only available regression signal.

## Goals / Non-Goals

**Goals:**
- Make `pio run`, `pio run -t upload`, and a CDC round-trip work from a plain terminal on this Mac, and prove each one against the attached hardware.
- Replace the dead Python environment with one that pins its interpreter and dependencies and is recreatable from committed files.
- Bring `README.md` and `CLAUDE.md` into agreement with the code, the schematic, and the toolchain that is actually installed.
- Leave behind a one-step readiness check, so the next machine's gap is a report rather than an archaeology session.

**Non-Goals:**
- Fixing firmware defects. The month off-by-one in `ST`/`GT`, `setup_rtc()` testing uninitialized globals, and `timesync.py`'s `abs(skew) > 1 & args.update` precedence bug are documented here and left for follow-up changes — mixing behavior fixes into an environment pass destroys the "unchanged firmware still flashes" regression signal.
- Building the menu FSM or display brightness control.
- Touching `hardware/` or `mechanical/` design files.
- Reconciling the `.kicad_pcb` title block's rev 1 against the schematic's rev 2. That requires opening the board in KiCad 10, which is exactly what this change declines to do; it is recorded as a known inconsistency.

## Decisions

### Install PlatformIO Core standalone with uv, not the VS Code extension

`uv tool install platformio` puts `pio` on PATH in its own isolated environment. PlatformIO is an ordinary Python package, so uv is a supported way to install it, and uv is already on this machine.

The decisive property is that the CLI works headlessly: builds, uploads, and monitors can be driven from a terminal or an agent session. The VS Code PlatformIO IDE extension — which `firmware/.vscode/extensions.json` recommends — buries `pio` in `~/.platformio/penv/bin`, which is not on PATH, so every terminal invocation needs path gymnastics and the IDE becomes a hard dependency of the build.

*Alternatives considered:* the IDE extension alone (rejected: not headless; also note the same file marks `ms-vscode.cpptools-extension-pack` as unwanted, so the IDE path brings its own conventions); PlatformIO's official `get-platformio.py` installer (equivalent outcome, but manages its own virtualenv outside any tool manager); Homebrew (no first-party formula). The extension remains installable later for step-debugging — the standalone CLI does not preclude it, though the two installs must not be allowed to drift.

*Contingency:* if `uv tool install platformio` fails, fall back to `get-platformio.py` and symlink `pio` onto PATH. The spec requires `pio` to resolve in a fresh shell, not a particular installer.

### Delete `software/.venv` and manage the Python tool with uv

The existing venv is unrecoverable: its `pyvenv.cfg` names an interpreter that no longer exists and its `bin/python*` symlinks are gone. It is untracked, so deleting it costs nothing and loses no committed state.

Replacing it with a `uv`-managed project (`pyproject.toml` + `uv.lock` in `software/timesync/`) pins both the dependency versions and the Python version, which is precisely the failure mode that broke it — the old venv inherited a Homebrew interpreter by path and died when that path moved. `uv run python timesync.py` then works without an activation step, which also removes a documentation footgun.

`requirements.txt` (two pinned lines) is fully represented by `pyproject.toml` + `uv.lock` and is removed rather than left as a second, drift-prone source of truth. `uv export` can regenerate it if something ever needs pip.

*Alternatives considered:* rebuild the same venv from pyenv 3.12.13 (rejected: reproduces the original fragility — a venv bound to one machine's interpreter path); document-only (rejected: leaves the tool unrunnable, and this change exists to make the machine work).

### Discover the serial port instead of hardcoding it

`timesync.py` currently defaults to `/dev/cu.usbmodem49795F7330551`, a device node specific to one board on one Mac. It will be changed to enumerate ports via `serial.tools.list_ports` and select the device matching VID `0x0483` / PID `0x5740` — verified above as this board's descriptor — with `--com` still overriding, and a clear non-zero exit naming the missing device when nothing matches.

This is the one behavior change to committed code in this change, and it is confined to port selection; the `-H`, `-U`, and `--csv` paths are untouched. The hardcoded node stays in the docs as an observed example, labeled machine-specific.

*Alternatives considered:* an environment variable or a config file (more machinery than a single-board project needs); matching on the product string (brittle across core versions compared with VID/PID).

### Leave the hardware files in KiCad 8 format

KiCad 10.0.3 will migrate `hardware/*.kicad_sch` and `*.kicad_pcb` on save, irreversibly and with a large diff against a design that has already been fabricated and assembled. Nothing in finishing the firmware requires editing the board, so the files stay untouched and the mismatch is documented instead — including what to do if a hardware edit ever becomes necessary (branch, migrate deliberately, commit the migration on its own).

*Alternatives considered:* migrate now (rejected: an unforced, one-way change to working design files, with no rev 3 in prospect); open read-only to confirm they load (rejected for this change: KiCad's autosave and project-file rewrites make "read-only" hard to guarantee, and the files' format version is already known from their headers).

### Documentation shape: three files with distinct jobs, and a script for the check

- `README.md` — the project's public face. Corrected to lead with rev 2 as current; the rev 1 / PCBWay narrative is retained below as revision history, not deleted. It is the only part of the repo written for an outside reader, and the sponsorship acknowledgement has standing.
- `docs/DEVELOPMENT.md` (new) — the working document: prerequisites, install commands, build/flash/monitor, the CDC command table, the rev 2 pin map, the timesync workflow, and the machine-specific observations. This keeps setup detail out of the README without burying it in `CLAUDE.md`.
- `CLAUDE.md` — agent-facing guidance, already written; it is trimmed to point at `docs/DEVELOPMENT.md` for commands rather than duplicating them, so the two cannot drift.
- `scripts/check-dev-env.sh` (new) — the one-step readiness check the spec requires. A script rather than a prose checklist, because a checklist cannot report *which* prerequisite is missing, and because it is the artifact that makes the next machine's setup cheap. It reports on `pio`, the uv environment, the ST-Link, and the CDC device, checking all of them before exiting non-zero on any gap.

`openspec/config.yaml`'s empty `context:` block is filled in with the stack and conventions so future proposals inherit them.

### Order: environment first, documentation second

The environment work is done and verified against the hardware before the documentation is written, so that every documented command is one that has actually been run on this machine. The spec requires documented commands to be executable as written; writing them first and verifying later inverts that.

## Risks / Trade-offs

- **The first clean build pulls a newer `ststm32` platform than the 2024 build used, and the newer Arduino STM32 core breaks compilation** → `platformio.ini` pins no platform version, so it resolves to latest. This is the most likely failure in the whole change. Mitigation: treat a build break as expected-and-handled, not as a blocker — pin `platform = ststm32@<version>` to a release contemporary with the last known-good build. This is the one edit to `platformio.ini` the change permits, and it is recorded if used.
- **The first build downloads several hundred MB** (toolchain, framework, platform) → unavoidable; it happens once and is why the clean-build verification is a distinct, separately-timed task.
- **Flashing could leave the board in a non-working state** → low impact: SWD upload is always recoverable with the ST-Link, and the image being flashed is the current committed source, unchanged.
- **The `.pio` stale-state cleanup deletes the generated `launch.json`'s referenced paths** → `firmware/.vscode/launch.json` and `c_cpp_properties.json` are PlatformIO-generated and gitignored; they regenerate. `c_cpp_properties.json` is already malformed on this machine (it fails to parse as JSON), which confirms it is disposable.
- **Port auto-detection picks the wrong device if another STM32 CDC device is attached** → the ST-Link shares the VID but differs in PID, so the two do not collide; if multiple boards ever appear, the tool reports the ambiguity and `--com` resolves it.
- **Removing `requirements.txt` breaks a pip-based workflow someone still uses** → trivially reversible via `uv export`, and documented in `docs/DEVELOPMENT.md`.
- **Documentation accuracy decays as soon as the firmware changes again** → the specs tie documented facts to their source (pin defines, registered commands), so drift is checkable rather than merely regrettable; the follow-up firmware changes are expected to update the docs alongside.

## Migration Plan

1. Install `pio`; confirm it resolves in a fresh shell.
2. Clear stale `firmware/.pio/`; run a clean build. If it fails on platform/core drift, pin the platform version and re-run.
3. Flash the unchanged firmware over the ST-Link; confirm the display runs.
4. Rebuild the Python environment with uv; confirm `pyserial`/`ntplib` import.
5. Exercise the CDC round-trip (`GT`) and the timesync tool against the board.
6. Land the port-discovery change; re-verify with and without `--com`.
7. Write the readiness script; run it here, and confirm it reports a gap correctly.
8. Write the documentation from the verified commands; correct the README's revision framing.

**Rollback:** every step is independently reversible. The environment work lives outside the repo (`uv tool uninstall platformio`, delete `software/timesync/.venv`). The committed changes are documentation plus one contained function in `timesync.py`; `git revert` restores the prior state, and the firmware is untouched throughout.

## Open Questions

- Does the current `ststm32` platform still build this source unmodified? Resolved by executing step 2; the answer determines whether `platformio.ini` gains a version pin.
- Should `mechanical/` note that FreeCAD is absent here, or is the `.step`/`.xhtml` export sufficient for anyone who does not intend to edit the stand? Leaning toward a one-line note.
- The `.kicad_pcb` title block says rev 1 while the schematic says rev 2. Documented as a known inconsistency in this change; fixing it requires a deliberate KiCad 10 session and belongs to a hardware change if one ever happens.
