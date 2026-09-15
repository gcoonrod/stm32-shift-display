# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

"STM32 Shift Clock" — a desk clock on a custom STM32F103C8T6 board that drives a 6-character
7-segment display through six daisy-chained 74HC595 shift registers (no multiplexing). The repo
holds the KiCad hardware design (board **rev 2**), the FreeCAD stand, the PlatformIO/Arduino
firmware, and a Python host tool. The firmware is unfinished: timekeeping and the USB CDC command
console work; the on-device menu and brightness control do not.

**[`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) is the authority for commands** — prerequisites,
build, flash, the CDC command table, the pin map, known defects, and machine-specific facts. Read
it before running anything, and update it rather than duplicating its content here.

Quick orientation:

```bash
./scripts/check-dev-env.sh         # what's installed, what's attached, what's missing
pio run -d firmware                # build
pio run -d firmware -t upload      # flash over ST-Link (SWD only; no DFU path)
cd software/timesync && uv run python timesync.py -H
```

Two traps worth knowing before you touch anything:

- **`platformio.ini` pins `platform = ststm32@19.7.1` deliberately.** Unpinned it resolves to
  20.0.0 / Arduino core 4.x, where STM32duino RTC and Low Power fail to compile and this source's
  `void*`-style RTC seconds callback no longer matches. Don't "helpfully" unpin it.
- **Never pipe `pio run` through `tee`** — you get `tee`'s exit status and a failed build reads as
  success. Redirect instead.

## Verification

There is no test suite and no linter. The only regression signal is: it builds, it flashes, it
answers over CDC (`TEST` → `TEST`, `GT` → a Unix timestamp). `pio device monitor` needs an
interactive terminal and cannot be driven from an agent session — talk to the port with pyserial
instead.

`timesync.py -H` looks read-only but **writes the device clock** because of a known precedence bug.
See the known-defects list in `docs/DEVELOPMENT.md` before using it as a read-only probe.

## Firmware architecture

`firmware/src/main.cpp` is a cooperative super-loop; the three `firmware/lib/*` libraries are local
PlatformIO libs, not published ones.

**Display pipeline (`lib/ShiftDisplay`).** Owns a 6-char ASCII buffer plus a `_dp_state` bitmask for
decimal points, and subclasses `Print`. `writeDisplay(buf, dp)` updates the buffer and shifts it
out; `update()` re-shifts the existing buffer. **Shifting out does not make anything visible —
`latch()` must follow.** Bytes go out LSB-first and the buffer is shifted in reverse index order, so
`_buffer[0]` is the leftmost digit. `map_ascii()` only knows `0-9`, `A-H`, and space; anything else
renders as three horizontal bars, so adding glyphs means extending `segment_data[]` and the range
check together. `enable()`/`disable()` drive the 595 `OE` line — the hook intended for PWM
brightness.

**Timekeeping.** `STM32RTC` on LSE with a seconds interrupt (`irq_rtc_seconds`) that copies the
whole date/time into the global `DateTimeBuffer_t date_time_buf` (`firmware/include/header.h`) and
sets `time_dirty`. The main loop redraws only when `time_dirty`. RTC values survive reset via VBAT
(CR2032), so `setup_rtc()` only reinitializes when it detects the epoch default.

**State machine (`lib/ShiftDisplayFSM`).** Two-phase: `execute(action)` computes `nextState`,
`update()` commits it at the end of the loop. `State` has only `IDLE` and `MENU`; the `MenuState`
enum enumerates the intended menu tree but `currentMenuState` is never advanced and
`MENU_UP`/`MENU_DOWN` are empty. This is the main thing to build out for the on-device UI.

**Input.** Three active-low buttons via AceButton; `handleEvent()` only handles `kEventClicked` and
latches into `btnSetState`/`btnPlusState`/`btnMinusState`, which the loop consumes and resets to
`UNCHANGED` each pass. Long-press and repeat are enabled in `ButtonConfig` but not acted on.

**`lib/ShiftClock` is dead code** — a standalone software clock superseded by `STM32RTC`. Don't
extend it without deciding it's actually the path forward.

**`STM32duino Low Power` is an unused `lib_deps` entry** — nothing includes it, but it is still
compiled, so it can still break a build.

## Host tooling

`software/timesync/` is a uv project: `pyproject.toml` + `uv.lock` + `.python-version` (CPython
3.12.13). No `requirements.txt`, no activation step — use `uv run`. It finds the board by USB
descriptor `0483:5740`; `--com` overrides.

## Conventions

- Firmware sources carry a CC BY-NC-SA 4.0 header comment; match it in new files under `firmware/`.
- Work happens on feature branches off `main`.
- `hardware/` is **KiCad 8** format while KiCad 10 is what's installed — opening and saving migrates
  irreversibly. The files are deliberately left unmigrated; see `docs/DEVELOPMENT.md`.
- `openspec/` drives planning (`/opsx:propose`, `/opsx:apply`, …); `openspec/config.yaml` carries the
  project context that new changes inherit.
- Generated artifacts stay out of git: `firmware/.pio/`, `.venv/`, `__pycache__/`, `*.csv`,
  KiCad backups.
