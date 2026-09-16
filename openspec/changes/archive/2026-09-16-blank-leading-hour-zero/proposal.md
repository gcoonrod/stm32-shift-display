## Why

In 12-hour mode the clock shows `09 30 00` at half past nine. Every domestic clock shows
` 9 30 00`, because a leading zero on a 12-hour display is something only digital watches
from 1978 do.

The hours are already converted for display — `display_hours()` returns 1–12 in 12-hour
mode and the RTC stays in 24-hour format regardless — so the only thing missing is
suppressing the tens digit when it is zero.

This was noticed while exploring per-digit dimming, where it looked like it might need a
DMA display engine to dim that digit. It does not. Blanking is not dimming, and it needs
nothing but a space instead of a zero.

## What Changes

- In 12-hour mode, when the displayed hour is below 10, the tens position shows blank
  rather than `0`.
- Applies to the idle clock and to the display while an alarm is firing, which shows the
  same time and would otherwise disagree with it.
- 24-hour mode is untouched: `09 30 00` is correct there and stays.

## Capabilities

### New Capabilities
_None._

### Modified Capabilities
- `clock-ui`: "Hours are presented in the selected 12/24-hour mode" gains the leading-zero
  suppression. Its existing scenario pinning 24-hour mode to `00` and `13` already says the
  other half of this and does not change.

## Impact

- **Firmware**: `firmware/src/main.cpp`, the two places the time is rendered.
- **Flash**: negligible.
- **Risk**: low, and visible immediately if wrong — the failure modes are a stray blank in
  24-hour mode or a blank where `1` belongs at ten, eleven and twelve o'clock.

## Not in scope

- **The time and alarm editors.** Both show the raw 0–23 hour rather than the converted
  one, so they do not follow 12-hour mode at all today. Blanking there would also be wrong
  on its own terms: you need to see both digits of a field you are editing. Whether the
  editors should present 12-hour time is a separate question, deliberately left alone.
- Dimming that digit rather than blanking it, which does need the display engine and is
  `dma-display-engine`.
