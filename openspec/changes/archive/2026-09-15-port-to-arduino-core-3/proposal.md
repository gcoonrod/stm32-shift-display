## Why

`firmware/platformio.ini` pins `platform = ststm32@19.7.1` because the source did not
compile against anything newer. That pin is a holding action, not a resolution: it
freezes the project on Arduino STM32 core 2.12.0 while upstream has moved to 3.0.0.
The longer it stands, the more it costs — 19.7.1's framework package will eventually
be purged from the PlatformIO registry the way the 17.x/18.x ones already were, at
which point the firmware stops building anywhere, on any machine, with no path back.

It turns out the pin can be lifted without touching a line of firmware source. The
build failure that forced it was never really about the core: it was
`STM32duino RTC@1.9.0` declaring its own `voidFuncPtr` as `void (*)(void*)`, which
collides with the core's `void (*)()` in `api/Common.h`. `STM32duino RTC@2.0.0` drops
that typedef and takes `voidFuncPtrParam` (`void (*)(void*)`) for
`attachSecondsInterrupt` — exactly the shape of `main.cpp`'s existing
`irq_rtc_seconds(void *data)`. The other failing library, `STM32duino Low Power`, is
declared in `lib_deps` but included and called by nothing; it is compiled only because
it is listed, and deleting the line deletes the problem.

Verified by experiment on 2026-09-15 in a scratch copy of `firmware/`, with the
platform unpinned, `STM32duino RTC @ ^2.0.0`, and Low Power removed:

```
ststm32@20.0.0, framework-arduinoststm32@4.30000.0 (Arduino core 3.0.0)
STM32duino RTC@2.0.0, AceButton@1.10.1, SerialCommands@2.2.0
SUCCESS, 0 errors
RAM:   23.3% (4772 / 20480)   was 24.7% (5052)
Flash: 69.8% (45768 / 65536)  was 72.2% (47348)
```

The image gets smaller, because the unused Low Power library is no longer compiled in.

## What Changes

The whole change is `firmware/platformio.ini`:

- Remove the `platform = ststm32@19.7.1` pin and the comment block explaining it.
- Move `stm32duino/STM32duino RTC` from `^1.4.0` to `^2.0.0`.
- Remove `stm32duino/STM32duino Low Power` from `lib_deps`. Nothing includes or calls
  it, and it does not compile against core 3.0.0. There is nothing to port.
- Verify on hardware, which is the part the compile experiment could not cover.
- Update the documentation that currently explains why the pin exists.

**No changes to `firmware/src/` or `firmware/lib/`.** The `irq_rtc_seconds(void *data)`
signature that appeared to need porting is correct as written under RTC 2.0.0.

**Correction carried by this change**: `docs/DEVELOPMENT.md`, `CLAUDE.md`, and the
commit message on `109d621` describe the newer toolchain as "Arduino core 4.x". That
is wrong — `4.30000.0` is PlatformIO's *package* version for the framework; the
Arduino core inside it is **3.0.0** (`platform.txt: version=3.0.0`), just as
`4.21200.0` is core **2.12.0**. The docs are corrected here.

**BREAKING**: after this change the firmware no longer builds on the 2.x core line,
so restoring the old pin will not recover a working build without also reverting the
`lib_deps` changes. Revert the commit as a unit.

## Capabilities

### New Capabilities
_None — this changes what the system is built against, not what it does._

### Modified Capabilities
- `dev-environment`: the clean-build requirement's scenario names "the four `lib_deps`",
  which becomes three. The same capability gains a requirement that the toolchain not be
  pinned to a superseded core line, and a requirement that a toolchain change be
  verified on hardware rather than by compilation alone.

## Impact

- **Firmware**: `firmware/platformio.ini` only.
- **Docs**: `docs/DEVELOPMENT.md` ("Why the platform version is pinned", the resolved
  versions list, and the core-version naming), `CLAUDE.md` (the "don't unpin it"
  warning), `openspec/config.yaml` (the pin note).
- **Verification**: this is the first change whose output is a *different binary* on the
  same source, so "it still builds" proves less than usual. Flashing and exercising the
  board is mandatory, not advisory — see the hardware-verification requirement in the
  spec delta.
- **Risk**: core 3.0.0 may change runtime behavior that compiles cleanly either way —
  RTC init and the LSE clock source, USB CDC enumeration and the `Serial.dtr()` dance in
  `setup()`, and `HardwareTimer` semantics are the places to look. A bad flash is
  recoverable over SWD.

## Not in scope

The known defects recorded in `docs/DEVELOPMENT.md` — the `tm_mon` off-by-one across
`ST`/`GT`, `setup_rtc()` reading uninitialized globals, `timesync.py`'s
`abs(skew) > 1 & args.update` precedence bug, and the `UnboundLocalError` on a bad
`--com` port. Keeping them out means any behavior change after this lands is
attributable to the core upgrade and nothing else.
