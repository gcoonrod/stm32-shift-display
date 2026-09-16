## Context

Flash sits at 82.3% of 64 KB, up 12.5 points across four changes, with nothing reporting
the figure between builds. The link map says where it went, and two chains stand out
because the firmware gets nothing for them.

`main.cpp` calls `localtime()`. The map's archive-inclusion table traces the consequence:

```
main.cpp.o            → lcltime.o        (localtime)
lcltime.o             → lcltime_r.o
lcltime_r.o           → tzset.o          (_tzset_unlocked)
tzset.o               → tzset_r.o
tzset_r.o             → sscanf.o         (siscanf)        ← timezone string parsing
sscanf.o              → nano-svfscanf.o  (__ssvfscanf_r)
nano-svfscanf.o       → nano-vfscanf_i.o (_scanf_chars)
```

The entire formatted-input engine is in the image so that `localtime` can parse a `TZ`
string this firmware never sets. Together with `mktime`, `localtime_r` and the 64-bit
division the time maths needs, that is about **5.2 KB**.

The second chain starts at `Print::printf`: `vdprintf` → `vasnprintf` → `svfprintf` →
`printf_i`, about **2.7 KB**, for eleven call sites that use `%d` and `%s` and nothing else.

Both are pure overhead. Neither is a trade-off anybody chose.

Build flags are otherwise sensible — `-Os`, `-ffunction-sections`, `-fdata-sections`,
`-fno-exceptions`, `-fno-rtti`, nano newlib — with `-flto` the one thing not enabled.

Separately, the month is written inconsistently by the two paths that set it: `ST` stores
`tm_mon` (0–11) while the menu editor stores 1–12. `ST` → `GT` round-trips because the same
offset is applied on both sides, which is exactly why zero skew from `timesync` has never
revealed it.

## Goals / Non-Goals

**Goals:**
- Remove the time and formatted-output library chains from the image.
- Leave every reply byte-identical, and prove it rather than assert it.
- Make the month consistent at 1–12 across both paths.
- Leave a way to see flash usage without remembering to read a build log.

**Non-Goals:**
- Replacing `analogWrite` with direct timer registers (~4.4 KB). Bank the safe space first.
- The display shift-out's 144 `digitalWrite` calls per refresh — a real speed win the clock
  does not need today.
- Any other item on the known-defects list.
- Making the firmware faster in any way a user could perceive. It is idle-dominated; the
  speed-ups here are incidental to removing code.

## Decisions

### Capture golden output before changing a line

The whole change is "alter the code, do not alter the behaviour", and a build that succeeds
proves none of that. So the first step is to capture every command's reply — including every
error path — from the **current** firmware into a file, and diff against it after each stage.

This is the one decision the change stands on. Without a byte-level baseline, "the replies
look right" is the strongest claim available, and it is not strong enough to justify
rewriting the date code.

### Integer civil-date arithmetic in place of `localtime`/`mktime`

Converting a Unix timestamp to a date and back is two well-known integer routines —
days-from-civil and civil-from-days — exact over the proleptic Gregorian calendar, with no
lookup tables, no locale, no timezone database and no 64-bit division. Roughly forty lines
against the 5.2 KB they displace.

The firmware's existing offset handling is unchanged: `ST` still adds the timezone offset
before converting, `GT` still subtracts it after. Only the conversion itself is replaced.

*Alternatives considered:* keeping `mktime` but avoiding `localtime` (the `tzset`/`scanf`
chain is reachable from both, so this saves little); `gmtime`/`timegm` instead (`timegm` is
not in newlib-nano, and `gmtime` still drags most of the chain).

### Month becomes 1–12 everywhere, converted only at the `tm` boundary

The RTC and the menu editor already use 1–12; only the serial path uses 0–11, and it does so
on both sides so the error hides itself. Making 1–12 canonical means one conversion in each
direction at the point a `tm`-shaped value is produced or consumed, and no offset anywhere
else.

`GT` output is therefore unchanged for a clock set by `ST`, and corrected for a clock set
from the menu. An RTC already holding a 0–11 month will read one month early until the date
is set again — a note in the docs, not a migration: nothing persists a month except the RTC
itself, and setting the date fixes it.

### Replace `printf` with `print()`, plus two small helpers

`Print::print()` has its own integer and string output and does not touch `vfprintf`. The
call sites need only two things `print()` lacks: two-digit zero padding (`%02d`, used by
`GA`) and four-digit zero-padded hex (`%04lX`, used by `GB`). Both are a few lines each,
tens of bytes against the 2.7 KB they replace.

The risk is transcription: eleven call sites, each of which must produce the same bytes
including the ones with no trailing newline. The golden-output diff is what catches that,
which is why it comes first.

### Try LTO, and keep it only if the hardware agrees

`-flto` typically recovers a few percent, and costs one line in `platformio.ini`. It is also
the flag most likely to expose latent assumptions — interrupt vectors, weak symbols and
`volatile` usage across translation units are all places where a working build can become a
non-working binary.

So it is adopted only if the image builds *and* the board passes the same verification as
every other stage. If anything is off, it comes straight back out; the other savings stand
on their own.

*Decision as built*: **kept.** It saved 4,672 bytes — more than the other two stages
combined would have suggested for a compiler flag — and the board passed the full
verification: golden diff identical, `timesync` at zero skew, the RTC seconds interrupt
still firing, and the menu, display, alarm and both brightness controls unchanged on the
device. The interrupt check mattered most; weak ISR symbols are the classic way LTO turns a
clean build into a broken binary, and a serial diff would not have caught it.

### Report flash usage from the readiness script

The budget is fixed and the figure already exists in every build; what is missing is anyone
looking at it. `scripts/check-dev-env.sh` already reports the toolchain and the hardware, so
it is the natural place — it turns "how much room is left" from an archaeology exercise into
a line of output.

## Risks / Trade-offs

- **A silent behaviour change.** The failure mode is a reply that differs in a way nobody
  exercises — an error path, a boundary value, a missing newline. Mitigated by capturing
  every command including error paths, and by diffing rather than reading.
- **Date arithmetic that is subtly wrong.** Leap years, month lengths and the epoch are easy
  to get almost right. The check is a round-trip across a spread of dates — leap days, month
  ends, year boundaries — compared against known-good values computed on the host.
- **LTO changing behaviour rather than just size.** Handled by treating it as provisional
  until the board says otherwise, and by keeping it a separate step so it can be dropped
  without unpicking anything else.
- **The month fix touching more than intended.** It reaches the date editor, the RTC seconds
  interrupt and `setup_rtc`'s epoch check. Each needs looking at, not just the two obvious
  call sites.
- **Estimates are estimates.** 5.2 KB and 2.7 KB are symbol sizes from the map; the linker
  may keep some of it alive for another reason. The measurement after each stage is the
  answer, and a stage that saves nothing should be reverted rather than kept for tidiness.

## Migration Plan

1. Capture golden output from the current firmware — every command, every error path.
2. Replace the date conversion; diff against golden; measure.
3. Make the month canonical; diff, and verify both paths agree on the device.
4. Replace the `printf` call sites; diff against golden; measure.
5. Try LTO; diff, verify on hardware, keep or revert on the evidence.
6. Add flash reporting to the readiness script; record the final figure.

**Rollback:** each stage is independent and separately revertible. The firmware source is
the only thing changing, so any stage can come out without disturbing the others.

## Open Questions

- ~~Does removing `localtime` actually drop `__udivmoddi4`?~~ **No.** It stays, and correctly
  so: `time_t` is 64 bits in this toolchain, `GT` must still emit 64-bit values, and
  `println(long long)` divides by ten to print digits.
- ~~Does anything else pull the float `vfprintf` variant?~~ **No.** `assert` → `fprintf` hung
  off the time chain and went with it, which is why the date stage saved 8,788 bytes against
  an estimate of 5,200.
- ~~Is LTO worth keeping if it saves very little?~~ It saved 4,672 bytes, so the question did
  not arise. Kept.
- Still open: the `analogWrite` → LL timer rewrite (~4.4 KB) and the display shift-out's 144
  `digitalWrite` calls per refresh. Both were deliberately excluded here; there is now 28 KB
  of headroom, so neither is urgent.
