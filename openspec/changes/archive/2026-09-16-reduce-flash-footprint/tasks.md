## 1. Golden baseline, before touching anything

- [x] 1.1 Record the starting figures: flash bytes and percentage, RAM, and the sizes of the symbols this change expects to remove
- [x] 1.2 Capture every command's reply from the **current** firmware into a file — `TEST`, `GT`, `ST`, `SO`, `GO`, `GM`, `SM`, `GA`, `SA`, `AE`, `GL`, `GI`, `SI`, `GD`, `SD`, `GB` — including an unrecognized command
- [x] 1.3 Capture every error path too: missing argument and out-of-range argument for each command that takes one
- [x] 1.4 Capture a set of `ST` → `GT` round-trips across awkward dates: a leap day, the end of a 30- and a 31-day month, a year boundary, and a time either side of midnight
- [x] 1.5 Confirm the capture is reproducible — run it twice and diff, so that fields which legitimately change (the clock) are known and excluded from later comparisons

## 2. Integer date arithmetic

- [x] 2.1 Add days-from-civil and civil-from-days helpers, integer only, no tables
- [x] 2.2 Verify the helpers against known-good values computed on the host, across the same awkward dates as the golden capture, before wiring them into anything
- [x] 2.3 Replace `unixTimestampToTime`'s `localtime()` with the new conversion, preserving the existing timezone-offset handling exactly
- [x] 2.4 Replace `cmd_get_time`'s `mktime()` with the reverse conversion, preserving its offset handling exactly
- [x] 2.5 Confirm `localtime`, `mktime`, `tzset` and the `scanf` family are gone from the linked image
- [x] 2.6 Check whether `__udivmoddi4` went with them, and note what still needs 64-bit division if it did not
- [x] 2.7 Diff every reply against the golden capture; measure and record the flash delta

## 3. Month consistency

- [x] 3.1 Make 1–12 canonical in `date_time_buf` and the RTC, converting only where a `tm`-shaped value is produced or consumed
- [x] 3.2 Update the date editor path so the menu and serial agree
- [x] 3.3 Check `setup_rtc`'s epoch comparison against `RTC_MONTH_JANUARY` still means what it says under the canonical representation
- [x] 3.4 Check the RTC seconds interrupt, which reads the month back every second
- [x] 3.5 Verify on the device: set a date over serial, read it in the menu, set one in the menu, read it over serial — the month must agree in both directions
- [x] 3.6 Confirm `ST` → `GT` still round-trips to the same timestamp it did before
- [x] 3.7 Confirm the unmodified `timesync.py` still reports zero skew

## 4. Remove the formatted-output chain

- [x] 4.1 Add the two small helpers `print()` lacks: two-digit zero padding and four-digit zero-padded hex
- [x] 4.2 Replace all eleven `printf` call sites with `print()` sequences, preserving every byte — including the error replies that deliberately have no trailing newline
- [x] 4.3 Confirm `vdprintf`, `vasnprintf`, `svfprintf` and `printf_i` are gone from the linked image
- [x] 4.4 Check whether the float `vfprintf` variant survives via `assert`, and record what still reaches it
- [x] 4.5 Diff every reply against the golden capture, error paths included; measure and record the flash delta

## 5. Link-time optimisation

- [x] 5.1 Enable `-flto` and confirm the image builds
- [x] 5.2 Measure the delta; if it is negligible, note that and drop it rather than carrying the risk
- [x] 5.3 Flash and run the full verification — replies, display, menu, alarm, LEDs, brightness
- [x] 5.4 Decide on the evidence: keep it only if it both saves meaningfully and behaves identically, and record the decision either way

## 6. Flash budget reporting

- [x] 6.1 Report flash usage and remaining headroom from `scripts/check-dev-env.sh`
- [x] 6.2 Make it work without requiring a fresh build, or say plainly that it reflects the last build
- [x] 6.3 Record the change's own before and after figures in the docs

## 7. Verification

- [x] 7.1 Full golden diff: every command, every error path, every round-trip date, against the baseline from group 1
- [x] 7.2 Walk the menu on the device: every entry, wrap, long-press exit, timeout
- [x] 7.3 Confirm the display, the alarm breathing, both brightness controls and all three LEDs are unchanged
- [x] 7.4 Power-cycle and confirm settings and the clock survive, using `GB` to check the stored values
- [x] 7.5 Record the final flash and RAM against the 82.3% / 24.6% starting point
- [x] 7.6 Update `docs/DEVELOPMENT.md`: the size figures, the LTO decision, the flash reporting, and remove the month defect from the known-defects list
- [x] 7.7 Update `CLAUDE.md` where it describes the date handling or the known defects
- [x] 7.8 Run `openspec validate reduce-flash-footprint`
