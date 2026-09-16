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
RAM:   [==        ]  24.0% (used 4920 bytes from 20480 bytes)
Flash: [======    ]  56.8% (used 37220 bytes from 65536 bytes)
```

Uploading needs the ST-Link on the SWD header. The board's default upload protocol
is already `stlink`, so no `upload_protocol` setting is needed. A good flash ends
with `** Verified OK **` and `** Resetting Target **`.

> **Do not pipe `pio run` through `tee`.** The exit status you get back is `tee`'s,
> so a failed build looks like a success. Redirect instead: `pio run > build.log 2>&1`.

### The display shift-out

The driver writes the shift registers through `BSRR` rather than `digitalWrite`. `begin()`
resolves each pin to its port and bit once and precomputes the `BSRR` words; the inner loop
is then a select and two stores, about **9 instructions per bit** against roughly 117
before. A full 48-bit refresh went from **~117 µs to ~9 µs**, a shift clock of roughly
410 kHz to about **5.3 MHz**.

> These figures were first written as 78 µs → 6.7 µs and 615 kHz → 7 MHz. That was
> arithmetic done at an assumed 72 MHz; **this board runs at 48 MHz** (`SystemCoreClock`
> reports 48000000, and `WD` prints it). 117 instructions × 48 bits ÷ 72 MHz is 78 µs
> exactly, which is how the original numbers arose. The conclusion is unchanged — no
> failure was observed at the fastest rate the loop can produce — only the rate that
> describes it.

`~OE` shares GPIOA with the data, clock, latch and clear lines and belongs to the
brightness timer, so **every write names the pins it owns**. A port-wide write
(`GPIOA->ODR = …`) would reach across it however careful the intent.

**On the rate.** Six 74HC595s on 3.3 V: the family is characterised at 4.5 V, and for a
chain this deep the limit is each stage's serial propagation delay plus the next stage's
setup time, six times over. The far end of the chain is not wired back — `U15`'s `QH'` goes
nowhere — so there is no electrical readback and a marginal rate would show up only as the
occasional wrong segment.

The rate was therefore established by measurement. Two patterns were displayed
continuously, re-shifting every pass so a marginal rate had many chances to fail:
`888888` (every segment lit, so a dropped bit shows as a gap) and `012345` (six distinct
digits, so a shifted stream shows as wrong positions — which `888888` cannot reveal, every
digit being identical).

**Result: no failure at the fastest rate the loop can produce.** The throttle constant is
therefore zero. Note what that does and does not establish: it shows ~7 MHz works on this
board, at room temperature, with these parts. It does **not** quantify the margin, because
no failure point was found to measure back from. If wrong segments ever appear, the first
thing to try is a throttle.

```
-D SHIFT_EDGE_NOPS=n   insert n NOPs between clock edges (default 0)
-D SHIFT_SWEEP         runtime-settable rate plus the SN and TP commands,
                       for re-measuring; absent from the production build
```

### Flash budget

`./scripts/check-dev-env.sh` reports usage and headroom from the last build. Note it reads a
few hundred bytes higher than PlatformIO's build line, which counts only `.text`, `.data`
and `.rodata` and omits the vector table and the init arrays — those occupy flash too.

The `reduce-flash-footprint` change took usage from **53,924 bytes (82.3%)** to **36,832
(56.2%)** as PlatformIO counts it, without altering behaviour:

| | bytes saved |
| --- | --- |
| integer date arithmetic in place of `localtime`/`mktime` | 8,788 |
| `print()` in place of `Print::printf` | 3,632 |
| link-time optimisation | 4,672 |
| **total** | **17,092** |

The `bsrr-shift-out` change later added **364 bytes** back: resolving ports and masks once
costs more state than the removed `digitalWrite` call sites saved. That change buys speed,
not size.

The first figure is the one worth understanding. `localtime()` reaches `tzset`, which reaches
`sscanf` to parse a `TZ` string this firmware never sets, which drags in the whole
formatted-input engine — and `assert` → `fprintf` hung off the same chain. Roughly 8.8 KB for
a conversion that is forty lines of integer arithmetic.

`-flto` is enabled in `platformio.ini`. It was adopted on evidence rather than by default:
it saved 4,672 bytes and the board passed the same verification as every other stage,
including the RTC seconds interrupt, which is the sort of thing LTO can quietly break.

### Why the platform version is constrained

`firmware/platformio.ini` sets `platform = ststm32@^20.0.0` — a range, not a pin. The
range holds the major line this firmware has been verified against while still accepting
20.x updates; moving to a future platform 21 is then a deliberate change with its own
hardware verification, rather than a silent jump.

Note that PlatformIO's framework *package* version is not the Arduino core version it
carries. Conflating the two is easy and was the source of an earlier misdiagnosis here:

| PlatformIO package | Arduino core (`platform.txt`) |
| --- | --- |
| `framework-arduinoststm32@4.30000.0` | **3.0.0** — current, what this builds against |
| `framework-arduinoststm32@4.21200.0` | **2.12.0** — the previously pinned line |

This project was briefly pinned to `ststm32@19.7.1` (core 2.12.0) because the build
failed against core 3.0.0. The cause was library drift, not the core:
`STM32duino RTC@1.9.0` declared its own `voidFuncPtr` as `void (*)(void*)`, colliding
with the core's `api/Common.h` `typedef void (*voidFuncPtr)(void)`. `STM32duino RTC@2.0.0`
drops that typedef and takes `voidFuncPtrParam` (`void (*)(void*)`) for
`attachSecondsInterrupt`, which is the shape `main.cpp`'s `irq_rtc_seconds(void *data)`
already had — so the upgrade needed no source change. `STM32duino Low Power` also failed
to compile against core 3.0.0 and was referenced nowhere in `src/`, `lib/`, or
`include/`, so it was removed from `lib_deps` rather than ported.

Do not try to pin *backward* to a platform contemporary with the original 2024
development. 17.x and 18.x cannot be installed at all: their framework packages
(`~4.20801.0`, `~4.20900.0`) are no longer in the registry and resolution fails with
`UnknownPackageError`. That purge is why this project tracks a current range instead of
an exact old release.

Versions resolved by the verified build:

```
ststm32@20.0.0              framework-arduinoststm32@4.30000.0 (Arduino core 3.0.0)
STM32duino RTC@2.0.0        AceButton@1.10.1
SerialCommands@2.2.0
```

Two warnings are expected and benign: `SerialCommands` uses the deprecated `boolean`
type, and `STM32RTC` emits `#warning "only BCD mode is supported"`, which is simply true
of the STM32F1 RTC.

One stale reference remains on purpose: commit `109d621`, which introduced the original
pin, describes core 3.0.0 as "Arduino core 4.x". Its message is wrong but the history is
merged, so it is left alone rather than rewritten. This section is the correct account.

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
| `GM` | — | `12` or `24` | display hour format |
| `SM` | `12` or `24` | `OK` | display only; the RTC always runs in 24-hour format |
| `GA` | — | `hh mm armed` | alarm time and whether it is armed (`0`/`1`) |
| `SA` | `hh` `mm` | `OK` | 0–23 and 0–59; out of range is rejected and changes nothing |
| `AE` | `0` or `1` | `OK` | arm or disarm; `AE 0` also silences a firing alarm |
| `GL` | — | `top mid bot` | indicator **duty**, 0–4095 each (12-bit); lit means non-zero |
| `GI` | — | `1`–`8` | indicator brightness level |
| `SI` | `1`–`8` | `OK` | indicator brightness; out of range is rejected and changes nothing |
| `GD` | — | `level duty` | display brightness level and its duty |
| `SD` | `1`–`8` | `OK` | display brightness; out of range is rejected and changes nothing |
| `GB` | — | see below | raw settings storage, for diagnosing settings loss |

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

### LEDs

The firmware's positional names run **opposite** to the schematic's D-numbering, so
cross-reference with care. All three are active high (cathodes to GND).

| Firmware | Pin | Net | Schematic | Indicates |
| --- | --- | --- | --- | --- |
| `LED_TOP` | `PB8` | `/LED3` | **D3** | AM — lit before 12:00, and only in 12-hour mode |
| `LED_MID` | `PB7` | `/LED2` | **D2** | 12-hour mode is active |
| `LED_BOT` | `PB6` | `/LED1` | **D1** | alarm armed (steady) or firing (breathing) |

The indicators are PWM-driven, not switched: PB6/PB7/PB8 are TIM4_CH1/CH2/CH3, reached
through `analogWrite()` at **12-bit** resolution (`analogWriteResolution(12)`) rather than
the core's 8-bit default. Eight bits is plenty for steady indicators but not for fading
between them — at the dim end one duty step is a large fraction of the light, so a breath
built on 256 steps visibly staircases no matter how fast it updates. "Lit" therefore means a duty,
set by the brightness level and mapped through a gamma curve so the eight levels feel
evenly spaced. A firing alarm breathes rather than blinking, peaking at the configured
brightness rather than overriding it.

Every write to those pins goes through one path. A stray `digitalWrite` would reconfigure
its pin back to plain output and silently stop the timer driving it.

`GL` reports the duty of all three over serial, which is both faster than reading them off
the board and the only reliable way to do it — `digitalRead()` on a pin the timer is
driving samples the live PWM waveform at an arbitrary phase.

Note that the generic variant defines `TIMER_SERVO TIM4`. That only matters if the Servo
library is ever used; nothing here touches a timer otherwise.

### Display brightness

The shift registers' `~OE` line is PWM-driven from PA0 (TIM2_CH1), so brightness applies to
all six digits at once — it is a single net across all six registers, which is also why
per-digit dimming is not possible.

`~OE` belongs to `ShiftDisplay`, which owns the pin and the **active-low inversion**: `~OE`
high blanks the outputs, so the duty written to the pin is the complement of the
brightness. `enable()` and `disable()` are brightness operations, not pin writes, and
`enable()` restores the configured level rather than going to full. Nothing else may write
PA0 — a `digitalWrite` would reconfigure it away from the timer and blank the display.

`analogWrite`'s resolution and frequency are **global**, shared by the indicator LEDs on
TIM4 and the display on TIM2. Both are set once in `setup()`: 12-bit at 4 kHz. 1 kHz would
likely do, but a bright source at low duty seen at the edge of vision is where PWM flicker
gets noticed. 4 kHz costs nothing — the timer reload is about 18,000 counts against 4,096
duty steps, so resolution is untouched; it would only suffer above roughly 17 kHz.

The alarm flash and the field-edit blink stay buffer-based rather than moving to `~OE`.
The field blink has to blank one field and `~OE` is all-or-nothing, so moving only the
alarm would leave two blanking mechanisms for one idea. `~OE` means brightness and nothing
else.

### Menu

Click **SET** from the clock to open the menu. **PLUS**/**MINUS** scroll and wrap;
**SET** enters an entry; **holding SET** backs out one level without saving. Ten
seconds without a press returns to the clock, discarding anything uncommitted.

| Entry | Shown | Fields |
| --- | --- | --- |
| Clock | `CLOC` | hours, minutes, seconds |
| Date | `DAtE` | day, month, year |
| Hour format | `12-24` | `12Hr` / `24Hr` |
| Alarm | `ALArn` | hour, minute, `On`/`OFF` |
| Indicator brightness | `LEd` | level 1–8, previewed live on the LEDs |
| Display brightness | `dISP` | level 1–8, previewed live on the display itself |

Inside an editor the field being edited blinks. PLUS/MINUS adjust it and wrap at the
field's limits; holding either repeats. SET advances to the next field and commits
after the last, returning to the clock.

A firing alarm flashes the whole display and blinks D1 until any button is pressed —
or until `AE 0` disarms it from a host.

### Diagnosing settings loss

`GB` reports the backup registers twice over:

```
boot:C10C 0003 032F 0206 now:C10C 0003 032F 0206 expect_magic:C10C
      magic flags alarm bright
```

The `boot:` values are what `settings_load()` read at start-up, captured before anything
could overwrite them; `now:` is what the registers hold at the time of the query.

The distinction matters because the two ways settings can vanish look identical afterwards.
If the backup domain failed to retain, `boot:` shows a bad magic and zeroed registers. If
the firmware misread the magic and took the uninitialised branch, it rewrote defaults over
good values — and every later boot then reads a *valid* magic with default contents, hiding
the cause completely. Only the start-up capture separates them.

It exists because settings were twice reported as returning to defaults without either
instance reproducing. A controlled power cycle with distinctive values in all four settings,
both cables out, preserved everything including the clock — so the cause is still unknown,
and this is the instrument for catching it next time.

### Settings storage

12/24-hour mode, the alarm time, the armed flag and the indicator brightness live in
backup-domain registers on VBAT, so they survive a power cut exactly as the time does.
**DR2, DR3, DR5 and DR8** are used; a magic value in DR2 distinguishes configured backup
memory from a fresh coin cell. DR8 holds both brightness levels: the indicator level in
the low byte, stored as-is, and the display level in the high byte, stored **offset by
one** so that zero means "never written". The low byte shipped first with the high byte
zeroed, and storing the display level raw would make that indistinguishable from a
deliberate level 0 — the dimmest — so devices already in the field would come up looking
blank.

Most of the backup domain is already claimed and must not be reused: the core takes
**DR1** (`RTC_BKP_INDEX`), **DR4** (`HID_MAGIC_NUMBER_BKP_INDEX`) and **DR10** in
`backup.h`, and STM32RTC stores the F1's emulated date across **DR6 and DR7**
(`RTC_BKP_DATE`). Writing the date registers would surface as a clock bug rather than
a settings bug. Access goes through the core's `getBackupRegister`/`setBackupRegister`
from `backup.h`; STM32RTC's own helpers are internal to `rtc.c`.

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

- **`timesync.py` writes the clock without `-U`.** The guard is
  `abs(skew) > 1 & args.update`, which binds as `abs(skew) > (1 & args.update)`. With
  `-U` absent that is `abs(skew) > 0`, so any nonzero skew triggers a write. Running
  what looks like a read-only `-H` will set the device clock.
- **A bad `--com` port raises `UnboundLocalError`.** `get_device_time()` and
  `set_device_time()` reference `ser` in their `finally` blocks, which is unbound when
  `serial.Serial()` itself raises, masking the real `SerialException`.
- **No DST on the device.** The timezone offset is applied as a fixed number of seconds
  either side of the date conversion. The host script compensates; anything else talking
  to the board does not.

### Fixed, and what the old description got wrong

The month *was* recorded here as "off by one over the wire — `ST` followed by `GT` does not
round-trip the date". That description was wrong in an interesting way: `ST` → `GT` always
round-tripped, verified across ten timestamps including leap days and year boundaries. The
same offset was applied on both sides, so the serial path was self-consistent.

The actual fault was that the two paths which set the month disagreed with *each other* —
`ST` stored 0–11, the menu editor stored 1–12 — so a date set in one place read back a month
out in the other, while any test confined to a single path looked perfect. That is also why
`timesync` reporting zero skew never revealed it.

Month is now 1–12 everywhere, converted only where a `tm`-shaped value is produced.

## Unimplemented

- **Brightness.** The `OE` line is on a PWM-capable pin and `irq_timer_led()` exists to
  toggle it, but the `HardwareTimer` setup in `setup_user_leds()` is commented out and
  nothing calls the ISR.
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
