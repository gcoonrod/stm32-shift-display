## Why

Flash has gone 69.8% → 74.2% → 81.1% → 82.3% over four changes, with about 11.6 KB left
of 64 KB and no tripwire. Nothing has been wasteful exactly, but nothing has looked back
either, and the next feature will be the one that runs out.

Measuring where it actually went turns up two things that cost thousands of bytes for no
benefit at all, both traceable through the link map rather than guessed at.

**The entire `scanf` machinery is linked because of timezone parsing.** `main.cpp` calls
`localtime()`, which pulls `lcltime_r` → `tzset` → `tzset_r` → `sscanf` → the formatted-input
engine. Nothing in this firmware reads formatted input:

```
mktime                    812
localtime_r               476
_tzset_unlocked_r       1,096
__ssvfscanf_r             748
__ssvfiscanf_r            748
_scanf_i                  508
sscanf                     84
__udivmoddi4              694   (64-bit division, from the time maths)
                        -----
                       ~5,166 bytes
```

The firmware needs none of it. It converts between a Unix timestamp and a date, which is
forty lines of integer arithmetic.

**`Print::printf` pulls the whole `vfprintf` chain.** Every command reply that uses it
drags in `vdprintf` → `vasnprintf` → `svfprintf` → `printf_i`, roughly **2,728 bytes**,
and all eleven call sites use nothing but `%d` and `%s`.

Link-time optimisation is also off: `-Os`, `-ffunction-sections` and `-fdata-sections` are
all enabled, `-flto` is not.

## What Changes

- Replace `localtime()` and `mktime()` with integer civil-date arithmetic, removing the
  time, timezone and formatted-input machinery from the image.
- Replace the eleven `printf` call sites with `print()`/`println()` sequences producing
  **byte-identical output**, removing the formatted-output machinery.
- Enable link-time optimisation if it builds and runs correctly.
- Record flash usage against its budget, and give `scripts/check-dev-env.sh` a way to
  report it, so the reclaimed space is not quietly re-spent.

**One deliberate behaviour change, requested rather than incidental.** The month is
currently handled inconsistently by the two paths that set it:

| Path | writes month as |
| --- | --- |
| `ST` over serial | `tm_mon`, **0–11** |
| Menu date editor | `edit_field`, **1–12** |

`ST` → `GT` round-trips correctly because the same offset is applied on both sides, which
is why `timesync` reports zero skew and the fault has stayed invisible. But a date set from
the menu reads back a month too high over serial, and a date set by `ST` displays a month
too low. Since the date code is being rewritten anyway, this change makes **1–12 canonical**
everywhere — matching the RTC and the menu — and converts only at the `tm` boundary.

After the fix, `GT` is unchanged for clocks set by `ST` and *corrected* for clocks set from
the menu.

Everything else is required to be indistinguishable. The display, the menu, the alarm, the
LEDs, the brightness controls and every other command must behave exactly as they do now.

## Capabilities

### New Capabilities
_None._

### Modified Capabilities
- `serial-command-console`: "The existing command set is preserved exactly" — `ST` and
  `GT` keep their names, arguments, replies and framing, but the month is no longer written
  inconsistently, so the requirement's claim that the timestamp is interpreted exactly as
  before needs to say what actually changed and what did not.
- `dev-environment`: gains a requirement that flash usage is known and protected. Four
  changes have consumed 12.5 percentage points of a fixed budget with nothing reporting it.

## Impact

- **Firmware**: `firmware/src/main.cpp` — the date conversion helpers, `cmd_set_time`,
  `cmd_get_time`, the eleven `printf` call sites, and the month handling in the date editor
  and the RTC seconds interrupt. `firmware/platformio.ini` if LTO is adopted.
- **Tooling**: `scripts/check-dev-env.sh` reports flash usage.
- **Docs**: `docs/DEVELOPMENT.md` — the size figures, the LTO decision, and the removal of
  the month defect from the known-defects list.
- **Risk**: this is the first change whose entire purpose is to alter code without altering
  behaviour, so "it still builds" proves very little. Every reply has to be compared against
  the current firmware's output rather than merely inspected.
- **Existing devices**: an RTC set by `ST` before this change holds a 0–11 month and will
  display one month early until the date is set again. Worth a note, not a migration.

## Not in scope

- **Replacing `analogWrite` with direct timer registers** (~4.4 KB). The largest remaining
  item, and the one that would replace working brightness and breathing code with register
  writes only hardware testing could validate. The reclaimed space should be banked before
  spending risk on it.
- **The display shift-out**, which uses 144 `digitalWrite` calls per refresh and is the only
  genuinely hot path. A real speed win, but a speed win the clock does not currently need —
  and this change is already handling enough at once.
- Any other defect on the known list.
