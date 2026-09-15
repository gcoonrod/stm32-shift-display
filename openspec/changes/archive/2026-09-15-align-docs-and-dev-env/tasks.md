## 1. Firmware toolchain

- [x] 1.1 Install PlatformIO Core with `uv tool install platformio`; verify `pio --version` exits 0 in a fresh shell and `which pio` resolves outside any VS Code extension directory
- [x] 1.2 Remove the stale `firmware/.pio/` carried over from the previous machine (it references toolchain paths that do not exist here)
- [x] 1.3 Run `pio run -d firmware` to completion; expect a long first run while the `ststm32` platform, ARM toolchain, and the four `lib_deps` download
- [x] 1.4 If 1.3 fails on platform or Arduino-core drift, pin `platform = ststm32@<version>` in `firmware/platformio.ini` to a release contemporary with the last known-good 2024 build, re-run, and record the pinned version and the failure it resolved
- [x] 1.5 Confirm `firmware/.pio/build/bluepill_f103c8_128k/firmware.elf` and `firmware.bin` exist and post-date the build

## 2. Hardware in the loop

- [x] 2.1 Confirm the ST-Link (VID `0x0483`, PID `0x3752`, serial `0673FF373841423043111349`) and the board CDC (VID `0x0483`, PID `0x5740`, serial `49795F733055`) are both enumerated
- [x] 2.2 Flash the unchanged firmware with `pio run -d firmware -t upload`; confirm it completes and the display runs after reset
- [x] 2.3 Open the CDC port and send `TEST\r\n`; confirm the device replies `TEST`
- [x] 2.4 Send `GT\r\n`; confirm a single integer Unix timestamp comes back
- [x] 2.5 Record the exact working monitor invocation for the documentation

## 3. Python host environment

- [x] 3.1 Delete the dead `software/.venv` (untracked; its interpreter and `bin/python*` symlinks are gone)
- [x] 3.2 Create `software/timesync/pyproject.toml` with a pinned `requires-python` and `pyserial==3.5` / `ntplib==0.4.0`; generate `uv.lock`
- [x] 3.3 Remove `software/timesync/requirements.txt`, now fully represented by the lockfile; note the `uv export` fallback for pip users in the docs
- [x] 3.4 Verify from a clean state: remove the environment, run the documented setup command, and confirm `serial` and `ntplib` import
- [x] 3.5 Run `uv run python timesync.py -H` against the attached board; confirm it prints NTP time, device time, and skew and exits 0
- [x] 3.6 Update `software/timesync/.gitignore` if the uv layout changes what should be ignored

## 4. Serial port discovery

- [x] 4.1 Replace the hardcoded `/dev/cu.usbmodem49795F7330551` default with `serial.tools.list_ports` enumeration matching VID `0x0483` / PID `0x5740`
- [x] 4.2 Keep `--com` as an override that bypasses discovery entirely
- [x] 4.3 Exit non-zero with a message naming the missing device and how to pass a port when nothing matches; verify by unplugging the board
- [x] 4.4 Report the ambiguity rather than guessing when more than one matching device is present
- [x] 4.5 Re-run `-H` both with and without `--com` to confirm both paths still reach the device; leave `-U` and `--csv` behavior untouched

## 5. Readiness check

- [x] 5.1 Write `scripts/check-dev-env.sh` reporting on `pio`, the uv environment, the ST-Link, and the board CDC device
- [x] 5.2 Check every prerequisite before exiting, so one gap does not mask another; exit non-zero if any is missing
- [x] 5.3 Name the install command for each missing prerequisite in its failure line
- [x] 5.4 Verify it passes on this machine, and that it correctly reports a gap (e.g. with the board unplugged)

## 6. Documentation

- [x] 6.1 Write `docs/DEVELOPMENT.md` from the commands actually run in groups 1–5: prerequisites, install, build, flash, monitor, timesync workflow
- [x] 6.2 Add the CDC command table (`TEST`, `GT`, `ST <unix_ts>`, `SO <hours>`, `GO`) with framing — `\r\n` terminator, space-separated args, 32-byte buffer — and note that `cmd_set_hour`/`cmd_set_minute`/`cmd_set_second` exist but are unregistered
- [x] 6.3 Add the rev 2 pin map from `firmware/src/main.cpp`, naming the defines and the schematic as the authority
- [x] 6.4 Label machine-specific facts (device nodes, USB serial numbers) as observed on this Mac, with how to discover the equivalents elsewhere
- [x] 6.5 Document the KiCad 10.0.3 vs KiCad 8 file-format mismatch, that saving migrates irreversibly, that the files are deliberately unmigrated, and what to do if a hardware edit becomes necessary
- [x] 6.6 Record the known-but-unfixed defects: the `tm_mon` off-by-one across `ST`/`GT`, `setup_rtc()` testing uninitialized `day`/`month`/`year`, and `timesync.py`'s `abs(skew) > 1 & args.update` precedence bug
- [x] 6.7 Record that the menu FSM and display brightness/PWM are unimplemented, naming `lib/ShiftDisplayFSM` and the commented-out `HardwareTimer` setup
- [x] 6.8 Note that FreeCAD is not installed here and that `mechanical/` ships `.step`/`.xhtml` exports for anyone who does not need to edit the stand

## 7. README and agent context

- [x] 7.1 Correct `README.md` to lead with rev 2 as the current revision, linking `hardware/assets/STM32ShiftDisplayRev2.pdf`
- [x] 7.2 Retain the rev 1 design narrative and the PCBWay sponsorship acknowledgement as revision history rather than deleting them
- [x] 7.3 Note the `.kicad_pcb` title block's rev 1 vs the schematic's rev 2 as a known inconsistency left for a future hardware change
- [x] 7.4 Point the README at `docs/DEVELOPMENT.md` for setup instead of repeating them
- [x] 7.5 Trim `CLAUDE.md` to reference `docs/DEVELOPMENT.md` for commands, keeping architecture and conventions, so the two cannot drift
- [x] 7.6 Fill in `openspec/config.yaml`'s `context:` block with the firmware stack, host tooling, and branch/licensing conventions
- [x] 7.7 Add `.DS_Store` and local editor/agent artifacts to `.gitignore` so the working tree shows only real changes

## 8. Verification

- [x] 8.1 Walk `docs/DEVELOPMENT.md` top to bottom, running each command verbatim from the stated directory; fix anything that needs modification to work
- [x] 8.2 Diff the documented pin map against `main.cpp` defines and the documented commands against the registered `SerialCommand` objects
- [x] 8.3 Confirm `README.md`, `CLAUDE.md`, and `docs/DEVELOPMENT.md` agree on hardware revision, commands, and prerequisites
- [x] 8.4 Run `scripts/check-dev-env.sh` once more on the finished state
- [x] 8.5 Confirm `firmware/src/`, `firmware/lib/`, `hardware/`, and `mechanical/` are unchanged in the final diff (`platformio.ini` only if 1.4 pinned a version)
- [x] 8.6 Run `openspec validate align-docs-and-dev-env`
