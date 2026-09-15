## Why

`firmware/platformio.ini` pins `platform = ststm32@19.7.1` because the current source
does not compile against anything newer. That pin is a holding action, not a
resolution: it freezes the project on the Arduino STM32 2.x core line while upstream
has moved to 4.x. The longer it stands, the more the gap costs — 19.7.1's framework
package will eventually be purged from the PlatformIO registry the same way the
17.x/18.x ones already were, at which point the firmware stops building anywhere,
on any machine, with no path back.

Evidence gathered during `align-docs-and-dev-env` (2026-09-15), building unpinned
against `ststm32@20.0.0` / `framework-arduinoststm32@4.30000.0`:

- `STM32RTC.h:69` typedefs `voidFuncPtr` as `void (*)(void*)`; the core's
  `api/Common.h:80` typedefs it as `void (*)()` — conflicting declaration.
- `main.cpp:238` `rtc.attachSecondsInterrupt(irq_rtc_seconds)` fails: `irq_rtc_seconds`
  is `void (*)(void*)`, the core-4 API wants `void (*)()`.
- `STM32LowPower.cpp` fails internally: no matching `attachInterrupt` overload,
  `uint32_t`→`PinStatus` conversions, and `HardwareSerial` has no `_serial` or
  `configForLowPower` member.

Attempting to pin *older* (17.6.0, contemporary with the original development) fails
outright with `UnknownPackageError: Could not find the package with
'platformio/framework-arduinoststm32 @ ~4.20801.0'`. The registry only retains
`4.30000.0`, `4.21200.0`, `4.20600.231001`, and `4.10900.200819`. The escape hatch is
forward, not backward.

## What Changes

- Change the RTC seconds-interrupt callback to the core-4 signature: `irq_rtc_seconds`
  becomes `void (*)()`, dropping the unused `void *data` parameter and its `UNUSED(data)`.
- Remove `stm32duino/STM32duino Low Power` from `lib_deps`. Nothing in `src/` or `lib/`
  includes or calls it, but it is still compiled and is a hard build failure on core 4.
  Removing the dependency is the fix; there is nothing to port.
- Move `STM32duino RTC` to a version that works against core 4 (2.0.0 or later), and
  adjust any other API drift the upgrade surfaces.
- Remove the `platform` pin, or move it forward to a current release with a comment
  explaining what it now guarantees.
- Re-verify on hardware: build, flash over ST-Link, and confirm the clock keeps time
  and answers `TEST`/`GT`/`ST`/`SO`/`GO` over USB CDC.
- Update `docs/DEVELOPMENT.md`'s "Why the platform version is pinned" section to
  describe whatever is true afterward.

**BREAKING**: the firmware will no longer build on the 2.x core line after this change.
That is the point, but it means the pin cannot simply be restored to recover.

## Capabilities

### New Capabilities
_None — this changes how existing behavior is built, not what the system does._

### Modified Capabilities
- `dev-environment`: the requirement that the firmware builds from a clean tree stands,
  but the toolchain it must build against changes from the pinned 2.x core line to a
  current one. The clean-build and flash scenarios need re-verification against the new
  platform; the requirement text itself may not need to change.

## Impact

- **Firmware**: `firmware/src/main.cpp` (the callback signature), `firmware/platformio.ini`
  (the pin and `lib_deps`). Possibly `firmware/lib/*` if the upgrade surfaces further drift.
- **Docs**: `docs/DEVELOPMENT.md` (the pinning rationale and the resolved-versions list),
  `CLAUDE.md` (the "don't unpin it" warning), `openspec/config.yaml` (the pin note).
- **Verification**: this is the first change to modify `firmware/src/`, so the
  "unchanged firmware still flashes" signal that `align-docs-and-dev-env` relied on is
  spent. Flashing and exercising the board over CDC becomes the only regression test,
  which makes hardware verification mandatory rather than merely advisable.
- **Risk**: core 4.x may change behavior beyond compilation — RTC init, USB CDC
  enumeration, and `HardwareTimer` are the areas to watch. The board is recoverable over
  SWD, so a bad flash is not fatal.

## Not in scope

The known defects recorded in `docs/DEVELOPMENT.md` — the `tm_mon` off-by-one across
`ST`/`GT`, `setup_rtc()` reading uninitialized globals, `timesync.py`'s
`abs(skew) > 1 & args.update` precedence bug, and the `UnboundLocalError` on a bad
`--com` port. Keep this change to the core port so a behavior regression is
attributable to the port and nothing else.
