## Context

`align-docs-and-dev-env` pinned `platform = ststm32@19.7.1` to get the firmware building
again on this machine. That pin bought time and cost nothing immediately, but it is a
dead end: the same registry purge that already stranded platforms 17.x and 18.x
(`UnknownPackageError: Could not find the package with 'platformio/framework-arduinoststm32
@ ~4.20801.0'`) will eventually reach 19.7.1's `~4.21200.0`, and the project would then
have no resolvable toolchain at all.

PlatformIO's framework package versions do not match the Arduino core versions they
carry, which is what made the original diagnosis wrong:

| PlatformIO package | `platform.txt` | Status |
| --- | --- | --- |
| `framework-arduinoststm32@4.21200.0` | Arduino core **2.12.0** | what the current pin uses |
| `framework-arduinoststm32@4.30000.0` | Arduino core **3.0.0** | current; what this change moves to |

The build failure attributed to "core 4.x" was really a library conflict:
`STM32duino RTC@1.9.0` defines its own `voidFuncPtr` as `void (*)(void*)` in
`STM32RTC.h:69`, colliding with core 3.0.0's `api/Common.h:80`
`typedef void (*voidFuncPtr)(void)`. `STM32duino RTC@2.0.0` stops declaring that typedef
and uses the core's `voidFuncPtrParam` (`api/Common.h:81`, `void (*)(void*)`) for
`attachSecondsInterrupt`, which is the shape `main.cpp`'s `irq_rtc_seconds(void *data)`
already has. The second failing library, `STM32duino Low Power@1.5.0`, genuinely does not
compile against core 3.0.0 — but `grep -rn "LowPower\|STM32LowPower" src/ lib/ include/`
returns nothing, so it is a dependency the project declares and never uses.

A scratch-copy experiment (platform unpinned, RTC `^2.0.0`, Low Power removed, no source
edits) built clean: 0 errors, RAM 23.3% and Flash 69.8%, down from 24.7% and 72.2%
because Low Power is no longer compiled in. That experiment was compile-only; nothing
has been run on the board.

Constraints: one physical board, no second unit, no test suite. The only runtime signal
is flashing and exercising the device.

## Goals / Non-Goals

**Goals:**
- Build against a currently published core, with constraints that keep resolving as the
  registry moves.
- Prove on hardware that the resulting image keeps time, drives the display, and answers
  over USB CDC.
- Leave the firmware source untouched, so any behavior change is attributable to the
  toolchain.
- Correct the "Arduino core 4.x" error in the documentation this change touches.

**Non-Goals:**
- Fixing the known defects (`tm_mon` off-by-one, `setup_rtc()` reading uninitialized
  globals, the `timesync.py` precedence bug, the `--com` `UnboundLocalError`).
- Implementing the menu FSM or PWM brightness. Brightness in particular wants
  `HardwareTimer`, whose semantics are among the things a core upgrade can move — better
  to land the port first and build brightness on the settled core.
- Migrating the KiCad files, which remain deliberately on KiCad 8.

## Decisions

### Upgrade STM32duino RTC to `^2.0.0` instead of changing the callback signature

The original proposal assumed `irq_rtc_seconds` had to become `void (*)()`. It does not:
RTC 2.0.0's `attachSecondsInterrupt(voidFuncPtrParam)` takes `void (*)(void*)`, matching
the existing function exactly. Upgrading the library is therefore strictly better than
editing the source — it fixes the conflict at its origin and keeps `firmware/src/`
untouched, which preserves the attribution of any runtime regression.

*Alternatives considered:* changing `irq_rtc_seconds` to drop its `void *data` parameter
(works, but edits source to compensate for a library bug that upstream already fixed, and
would have to be undone); pinning RTC to 1.9.0 and staying on core 2.12.0 (the status quo
this change exists to end).

### Remove `STM32duino Low Power` rather than upgrade it

Low Power 2.0.0 exists and presumably compiles against core 3.0.0, but the project
includes and calls nothing from it. Upgrading would keep compiling a library the firmware
does not use, for no benefit, and would keep a third-party breakage in the build path.
Deleting the `lib_deps` line removes ~1.5KB of flash and one failure mode.

*Alternatives considered:* upgrade to `^2.0.0` (carries dead weight); keep it pinned at
1.5.0 (does not compile); keep it and set `lib_ldf_mode` to exclude it (more configuration
to express "we don't use this" than simply not listing it).

### Constrain the platform to `^20.0.0`, not to nothing

Removing the pin entirely would let the project follow the platform across a future major
version — exactly the kind of silent jump that produced this situation. A caret range
holds the verified major line while still accepting 20.x updates, and satisfies the spec's
requirement that a constraint be a range that continues to resolve rather than a pin to a
single release. When platform 21 appears, moving to it becomes a deliberate change with
its own hardware verification, which is the process this change is establishing.

*Alternatives considered:* fully unpinned (invites the next surprise); pinning `@20.0.0`
exactly (recreates the present problem one purge later).

### Verify in a fixed order, and treat the display as the RTC's witness

Compilation says nothing about whether the RTC seconds interrupt still fires or whether
USB CDC still enumerates. The verification order is: flash → watch the display advance →
CDC round-trip → set and re-read the clock across a minute. The display advancing is the
cheapest proof that `irq_rtc_seconds` is being called, since that interrupt is what sets
`time_dirty` and drives every redraw; if the digits freeze, the callback binding is wrong
regardless of what compiled.

### Correct the core-version naming wherever it appears

`docs/DEVELOPMENT.md`, `CLAUDE.md`, and commit `109d621` all say "Arduino core 4.x". The
commit message cannot be corrected without rewriting merged history, which is not worth
it; the documentation can and is. The corrected docs state both numbers — the PlatformIO
package version and the core version it carries — since the mismatch between them is what
caused the error in the first place.

## Risks / Trade-offs

- **The image compiles but misbehaves on the board** → the central risk, and the reason
  hardware verification is a spec requirement rather than a task nicety. Watch RTC/LSE
  init, USB CDC enumeration (`setup()` calls `Serial.dtr(true)` before `Serial.begin()`,
  which is unusual and core-version-sensitive), and the seconds interrupt. Recovery is a
  revert plus a re-flash over SWD; the board cannot be bricked this way.
- **`SerialCommands@2.2.0` is unmaintained and already warns** (`'boolean' is deprecated`)
  → a warning today, potentially an error on a future core. Out of scope here, but it is
  the next dependency likely to force a change.
- **Rolling back needs the whole commit, not just the pin** → restoring
  `platform = ststm32@19.7.1` without also restoring `RTC @ ^1.4.0` and the Low Power
  line yields a build that fails differently. Revert as a unit; the change is one file,
  so this is easy if done deliberately.
- **The flash headroom gain is not a goal** → 69.8% vs 72.2% is a pleasant side effect of
  dropping dead weight, not a reason to do this. Don't let it justify skipping
  verification.

## Migration Plan

1. Edit `firmware/platformio.ini`: platform to `^20.0.0`, RTC to `^2.0.0`, drop Low Power,
   remove the pin's comment block.
2. Clean build; confirm 0 errors and the expected resolved versions.
3. Flash over ST-Link; confirm the display runs and the digits advance.
4. CDC round-trip, then set/read the clock across a minute.
5. Update the docs that describe the pin, including the core-version correction.
6. Commit as a single unit so it reverts as one.

**Rollback:** `git revert` the commit and re-flash. The firmware source is untouched
throughout, so the pre-change image is exactly reproducible from the previous commit.

## Open Questions

- Does core 3.0.0 change USB CDC enumeration enough to disturb the `Serial.dtr(true)` /
  `Serial.begin()` ordering in `setup()`? Answered by step 4; if the port enumerates but
  never answers, that ordering is the first suspect.
- Should `board = genericSTM32F103C8` be revisited while the platform moves? The commented
  out `bluepill_f103c8_128k` line suggests past uncertainty. Out of scope unless step 2
  surfaces a board-definition problem.
