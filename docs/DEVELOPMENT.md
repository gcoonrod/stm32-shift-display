# Development

How to build, flash, and talk to the STM32 Shift Clock. Every command here was run
on macOS (Darwin 25.6.0, Apple Silicon) on 2026-09-15; where something could not be
verified from a script, it says so.

## Prerequisites

| Tool | Why | Install |
| --- | --- | --- |
| [uv](https://docs.astral.sh/uv/) | installs PlatformIO and manages the Python tool environment | `curl -LsSf https://astral.sh/uv/install.sh \| sh` |
| PlatformIO Core | builds and flashes the firmware | `uv tool install platformio` |
| ST-Link probe | the only way to flash — the board has no DFU or serial bootloader path | hardware |
| KiCad 8 | *optional*, only to open `hardware/` — see [Hardware files](#hardware-files) first | [kicad.org](https://www.kicad.org/) |
| FreeCAD | *optional*, only to edit `mechanical/desk-stand.FCStd` | [freecad.org](https://www.freecad.org/) |

PlatformIO is installed as a standalone CLI rather than through the VS Code
PlatformIO IDE extension, so builds work from any terminal and from agent sessions.
`firmware/.vscode/extensions.json` still recommends the extension; installing it is
optional and independent. If you do, keep in mind it carries its own PlatformIO
copy in `~/.platformio/penv`, which can drift from the CLI one.

### Check the environment

```bash
./scripts/check-dev-env.sh
```

Reports PlatformIO, uv, the Python environment, the ST-Link, and the board's CDC
interface. It checks all of them before exiting, names the fix for anything
missing, and exits non-zero if there is a gap.

## Firmware

All PlatformIO commands work from the repo root with `-d firmware`, or from inside
`firmware/` without it.

```bash
pio run -d firmware                # build
pio run -d firmware -t upload      # build + flash over the ST-Link
pio run -d firmware -t clean       # drop build output
pio device list --serial           # find the board's CDC port
```

The first build downloads the platform, the ARM toolchain, and the libraries —
several hundred MB, a few minutes. Later builds take about 25 seconds. A successful
build reports roughly:

```
RAM:   [==        ]  24.7% (used 5052 bytes from 20480 bytes)
Flash: [=======   ]  72.2% (used 47348 bytes from 65536 bytes)
```

Uploading needs the ST-Link on the SWD header. The board's default upload protocol
is already `stlink`, so no `upload_protocol` setting is needed. A good flash ends
with `** Verified OK **` and `** Resetting Target **`.

> **Do not pipe `pio run` through `tee`.** The exit status you get back is `tee`'s,
> so a failed build looks like a success. Redirect instead: `pio run > build.log 2>&1`.

### Why the platform version is pinned

`firmware/platformio.ini` pins `platform = ststm32@19.7.1`. Unpinned, PlatformIO
resolves the latest platform, which ships Arduino core 4.x
(`framework-arduinoststm32@4.30000.0`). Against that core:

- STM32duino RTC and STM32duino Low Power fail to compile internally — `HardwareSerial`
  has no `_serial` or `configForLowPower` member, and `PinStatus` conversions fail.
- `STM32RTC.h` typedefs `voidFuncPtr` as `void (*)(void*)` while the core's
  `api/Common.h` typedefs it as `void (*)()`, a conflicting declaration.
- That conflict reaches this source: `rtc.attachSecondsInterrupt(irq_rtc_seconds)` in
  `main.cpp` passes a `void (*)(void*)`, which no longer matches.

19.7.1 is the newest platform still on the 2.x core line (`framework-arduinoststm32@4.21200.0`),
where the current library versions and this source compile unmodified. Platforms
contemporary with the original 2024 development (17.x, 18.x) cannot be used at all —
their framework packages (`~4.20801.0`, `~4.20900.0`) are no longer in the PlatformIO
registry, and installing them fails with `UnknownPackageError`.

Versions resolved by the verified build:

```
ststm32@19.7.1              framework-arduinoststm32@4.21200.0
STM32duino RTC@1.9.0        STM32duino Low Power@1.5.0
AceButton@1.10.1            SerialCommands@2.2.0
```

`STM32duino Low Power` is declared in `lib_deps` but is not included or called
anywhere in `src/` or `lib/`. It is still compiled, so it can still break a build.

## Talking to the board

The firmware exposes a command console over USB CDC. Commands are `\r\n`-terminated
with space-separated arguments, and the receive buffer is 32 bytes
(`serial_command_buffer_` in `main.cpp`).

| Command | Argument | Reply | Notes |
| --- | --- | --- | --- |
| `TEST` | — | `TEST` | liveness check |
| `GT` | — | Unix timestamp | reads the RTC, converts using the timezone offset |
| `ST` | Unix timestamp | `OK` | sets the RTC |
| `GO` | — | offset in hours | signed integer, defaults to `-6` |
| `SO` | offset in hours | `OK` | signed integer |

An unrecognized command replies `Unrecognized command [<cmd>]`.

`cmd_set_hour`, `cmd_set_minute`, and `cmd_set_second` are implemented in `main.cpp`
but never registered, so they are not reachable. Registering one needs a
`SerialCommand` object and an `AddCommand()` call in `setup()`.

### Interactively

```bash
pio device monitor -p /dev/cu.usbmodemXXXXXXXXXXXX -b 115200
```

This needs a real interactive terminal — it refuses to run with redirected input, so
it cannot be driven from a script or an agent session. The baud rate is ignored by
USB CDC but pyserial requires one.

### From a script

```python
import serial
with serial.Serial(port, 115200, timeout=2) as ser:
    ser.write(b"GT\r\n")
    print(ser.readline().decode().strip())
```

Verified round-trip on the attached board:

```
TEST -> 'TEST'
GT   -> '1789580671'
GO   -> '-6'
```

## Time sync

`software/timesync/` compares the board's RTC against NTP and can correct it.

```bash
cd software/timesync
uv sync                                   # create/refresh the environment
uv run python timesync.py -H              # human-readable NTP vs device vs skew
uv run python timesync.py -U              # push NTP time to the device
uv run python timesync.py --csv           # append a row to timesync.csv
uv run python timesync.py --com /dev/cu.usbmodemXXXXXXXXXXXX
```

The environment is uv-managed: `pyproject.toml` pins the dependencies, `uv.lock`
pins their resolution, and `.python-version` pins CPython 3.12.13. There is no
activation step and no `requirements.txt` — run `uv export --format requirements-txt`
if you need one for pip.

Without `--com`, the tool finds the board by USB descriptor (`0483:5740`). It exits
non-zero and names the problem when no board is attached, and when more than one is
attached it lists them and asks you to choose rather than guessing. `--com` bypasses
discovery entirely.

The timezone is hardcoded to `America/Chicago` and the script adds the DST offset
itself before sending — the firmware has no DST logic.

## Pin map

Board rev 2, from the `#define`s at the top of `firmware/src/main.cpp`. Those defines
and `hardware/stm32-shift-display.kicad_sch` are the authority; this table is a copy.

| Function | Pin | |
| --- | --- | --- |
| `SER` | `PA3` | 74HC595 serial data |
| `SRCLK` | `PA4` | 74HC595 shift clock |
| `SRCLRB` | `PA1` | 74HC595 clear, active low |
| `RCLK` | `PA2` | 74HC595 latch clock |
| `OEB` | `PA0` | 74HC595 output enable, active low |
| `LED_TOP` | `PB8` | user LED |
| `LED_MID` | `PB7` | user LED |
| `LED_BOT` | `PB6` | user LED |
| `BTN_SET` | `PB5` | button, `INPUT_PULLUP` |
| `BTN_PLUS` | `PB4` | button, `INPUT_PULLUP` |
| `BTN_MINUS` | `PB3` | button, `INPUT_PULLUP` |

The rev 2 remap put the LED and `OE` pins on PWM-capable outputs specifically to
allow brightness control. That is not implemented yet — see below.

## Hardware files

`hardware/` is in **KiCad 8** format: the schematic is `version 20231120` and the
board is `version 20240108`. Opening them in a newer KiCad and saving migrates them
irreversibly, with a large diff against a design that is already fabricated and
assembled.

These files are deliberately left unmigrated. Nothing in the firmware work needs
them edited. If a hardware change ever becomes necessary: branch first, migrate
deliberately, and commit the format migration on its own so it can be reviewed
separately from the design change.

The machine this was last set up on has KiCad 10.0.3 installed, which will migrate
on save.

**Known inconsistency:** the current board is rev 2, but the sources disagree about
that and all of them describe the same physical board — the schematic and
`STM32ShiftDisplayRev2.pdf` say rev 2, while `stm32-shift-display.kicad_pcb`'s title
block and the photo/model assets (`rev1.jpg`, `stm32-shift-display_rev1.step`) say
rev 1. The README has the full mapping. Correcting the `.kicad_pcb` field means
opening the board in KiCad, so it is left for whenever the hardware is next touched.

`mechanical/desk-stand.FCStd` needs FreeCAD, which is not installed on this machine.
`desk-stand-Body.step` and `desk-stand.xhtml` are exports that need no FreeCAD to view.

## Known defects

Verified in the current source and deliberately **not** fixed by the environment pass,
so that "the unchanged firmware still builds and flashes" stayed a usable signal:

- **Month off by one over the wire.** `cmd_set_time()` passes `tm_mon` (0–11) straight
  to `rtc.setMonth()` (1–12), and `cmd_get_time()` reads it back without the inverse
  correction. `ST` followed by `GT` does not round-trip the date.
- **`setup_rtc()` tests the wrong variables.** Its epoch check reads the uninitialized
  globals `day`, `month`, and `year` instead of the `date_time_buf` fields it just
  populated from the RTC.
- **`timesync.py` writes the clock without `-U`.** The guard is
  `abs(skew) > 1 & args.update`, which binds as `abs(skew) > (1 & args.update)`. With
  `-U` absent that is `abs(skew) > 0`, so any nonzero skew triggers a write. Running
  what looks like a read-only `-H` will set the device clock.
- **A bad `--com` port raises `UnboundLocalError`.** `get_device_time()` and
  `set_device_time()` reference `ser` in their `finally` blocks, which is unbound when
  `serial.Serial()` itself raises, masking the real `SerialException`.
- **No DST on the device.** The offset is applied by adding seconds around `mktime`.
  The host script compensates; anything else talking to the board does not.

## Unimplemented

- **The menu.** `lib/ShiftDisplayFSM` has only `IDLE` and `MENU` states. The
  `MenuState` enum lays out the intended tree, but `currentMenuState` is never
  advanced and the `MENU_UP`/`MENU_DOWN` actions are empty. `main.cpp`'s `MENU` branch
  only draws `printSetHourMenu()` and watches for a click to exit.
- **Brightness.** The `OE` line is on a PWM-capable pin and `irq_timer_led()` exists to
  toggle it, but the `HardwareTimer` setup in `setup_user_leds()` is commented out and
  nothing calls the ISR.
- **Long press and repeat.** Enabled in `ButtonConfig` but `handleEvent()` only handles
  `kEventClicked`.
- **`lib/ShiftClock`** is a standalone software clock, superseded by `STM32RTC` and
  referenced by nothing.

## This machine

Facts specific to the Mac this was set up on — they will differ elsewhere. Use
`pio device list --serial` or `./scripts/check-dev-env.sh` to find the equivalents.

| | |
| --- | --- |
| Board CDC | `/dev/cu.usbmodem49795F7330551` — USB `0483:5740`, serial `49795F733055`, "GENERIC_F103C8TX CDC in FS Mode" |
| ST-Link | `/dev/cu.usbmodem14102` — USB `0483:3752`, serial `0673FF373841423043111349` |
| PlatformIO | Core 6.2.0 at `~/.local/bin/pio` (uv tool install) |
| Python | CPython 3.12.13 via pyenv; uv 0.11.12 |
| KiCad | 10.0.3 |

The USB vendor id `0483` is shared by the board and the ST-Link; they differ by
product id, which is why port discovery matches on both.
