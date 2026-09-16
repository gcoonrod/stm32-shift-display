# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

"STM32 Shift Clock" — a desk clock on a custom STM32F103C8T6 board that drives a 6-character
7-segment display through six daisy-chained 74HC595 shift registers (no multiplexing). The repo
holds the KiCad hardware design (board **rev 2**), the FreeCAD stand, the PlatformIO/Arduino
firmware, and a Python host tool. Timekeeping, the USB CDC command console, the on-device
menu, the three indicator LEDs, a daily alarm, global and per-digit display brightness, and the
DMA refresh engine all work.

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

- **`platformio.ini` constrains `platform = ststm32@^20.0.0` deliberately** — a range, not a
  pin. It holds the major line the firmware is verified against (Arduino core 3.0.0, carried by
  `framework-arduinoststm32@4.30000.0` — the package version and the core version differ, don't
  conflate them). Widening it to a future platform 21 is a deliberate change that needs hardware
  verification, not a cleanup.
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

**Display pipeline (`lib/ShiftDisplay`).** The display refreshes itself: TIM1 and two DMA
channels replay a precomputed waveform into `GPIOA->BSRR` at 500 Hz with the CPU uninvolved.
`SHIFT_ENGINE_DMA=0` falls back to the bit-banged shift loop, which stays compiled in so a bad
peripheral configuration is one rebuild from a working clock.

```
TIM1 update -> DMA1 ch5 -> GPIOA->BSRR   48 words/slice, incrementing
TIM1 CC1    -> DMA1 ch2 -> GPIOA->BSRR   one constant word (SRCLK high), no increment
```

Data rides the **update** event, not CH1, because CH1 would need `CCR1 = 0` — a compare
coinciding with the counter wrap. The rising clock edge carries no data, so it is one constant
word replayed with memory increment off: 48 words per slice, not 96. The latch rides in words 0
and 1 of each slice and presents the *previous* slice, costing no extra words and no third
channel; appending latch words would not work, as the clock channel keeps firing during them and
extra clocks push data off a chain exactly as long as its data.

**The board runs at 48 MHz, not 72.** `SHIFT_ENGINE_CORE_CLOCK_HZ` records that and
`shift_engine_clock_ok()` checks it at runtime, because `F_CPU` here expands to the runtime
`SystemCoreClock` and cannot be used in a constant expression. Everything else derives from it
and `PWM_FREQ_HZ`, with `static_assert`s that fail the build if a slice stops being a whole
number of `~OE` periods. **Never write those constants as literals** — see `ShiftDisplayEngine.h`.

Per-digit brightness comes from the data: a digit lit in *k* of 8 slices is *k*/8 as bright.
It multiplies with the global `~OE` level rather than replacing it. This is what the menu-exit
fade uses.

Never write GPIOA as a whole — `~OE` shares the port and belongs to the brightness timer. A
`static_assert` in `main.cpp` catches an aliased pin define and `oe_collides()` in `begin()`
catches two pins landing on the same port bit; the driver refuses to initialise if either does.

Owns a 6-char ASCII buffer plus a `_dp_state` bitmask for decimal points, and subclasses `Print`.
`setContent(buf, dp)` updates the buffer and rebuilds the waveform — under the engine that is all
a caller needs, and `latch()` is a no-op. Bytes go out LSB-first and the buffer is shifted in
reverse index order, so `_buffer[0]` is the leftmost digit. `map_ascii()` indexes one ASCII-keyed table, which is the single source of truth for what
renders: a character is drawable exactly when it has a non-zero entry. Add glyphs there and
nowhere else. `M` and `W` are absent deliberately — neither is legible on seven segments.
`enable()`/`disable()` are brightness operations on the 595 `~OE` line, which `ShiftDisplay`
owns along with its active-low inversion — `enable()` restores the configured brightness,
not full. Never `digitalWrite` PA0; it would reconfigure the pin away from TIM2 and blank
the display. `analogWrite` resolution and frequency are global and shared with the LEDs,
set once in `setup()`.

**Timekeeping.** `STM32RTC` on LSE with a seconds interrupt (`irq_rtc_seconds`) that copies the
whole date/time into the global `DateTimeBuffer_t date_time_buf` (`firmware/include/header.h`) and
sets `time_dirty`. Month is **1–12 everywhere**; converting to a `tm`-style 0–11 anywhere but at
the conversion boundary is the bug that used to make the serial and menu paths disagree.

**Don't reach for `<time.h>` or `Print::printf`.** `localtime`/`mktime` cost ~8.8 KB here — they
drag in `tzset`, `sscanf` and the whole formatted-input engine — and `printf` costs ~3.6 KB. Use
the `days_from_civil`/`civil_from_days` helpers and `print()` with the small `print2`/`print4hex`
helpers instead. `-flto` is on. The main loop redraws only when `time_dirty`. RTC values survive reset via VBAT
(CR2032), so `setup_rtc()` only reinitializes when it detects the epoch default.

**State machine (`lib/ShiftDisplayFSM`).** Two-phase: `execute(action)` computes the next
state, `update()` commits it at the end of the loop. `update()` must commit *every* pending
field — it originally committed only `currentState`, which silently stranded the whole menu
tree. States are `IDLE`, `MENU`, `EDIT`, `FIRING`; `MenuState` is the top-level entry and the
field index selects which value an editor is on.

**Input.** Three active-low buttons via AceButton. `handleEvent()` maps click, long-press and
repeat into `btnSetState`/`btnPlusState`/`btnMinusState`, which the loop consumes and resets each
pass. `kFeatureDoubleClick` is deliberately off (it delays every click);
`kFeatureSuppressAfterLongPress` is on so backing out of a level doesn't also emit a click.

**LEDs** are PWM, not on/off — PB6/PB7/PB8 are TIM4_CH1/CH2/CH3 via `analogWrite`, at a
gamma-mapped brightness level. All three are written through one path; a stray
`digitalWrite` reconfigures the pin away from its timer and stops it. `digitalRead` on them
is meaningless, so `GL` reports cached duty instead.

**Settings** persist in backup registers DR2/DR3/DR5/DR8. `GB` dumps them raw, including
what start-up read before `settings_load()` could overwrite it — settings have twice been
reported as resetting without reproducing, and that capture is what distinguishes storage
that failed to retain from firmware that clobbered good values. DR1, DR4 and DR10 belong to the
core and DR6/DR7 hold the RTC library's emulated date — writing those corrupts the clock.
See `docs/DEVELOPMENT.md`.

**`lib/ShiftClock` is dead code** — a standalone software clock superseded by `STM32RTC`. Don't
extend it without deciding it's actually the path forward.

**Dependencies are RTC, AceButton, and SerialCommands.** `STM32duino Low Power` was removed —
it was declared but referenced nowhere, and did not compile against core 3.0.0.

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
