## Context

`render()` builds the six-character buffer through `put2()`, which always writes two digits.
The hours it is given already come from `display_hours()`, which returns 1–12 in 12-hour
mode and leaves the RTC in 24-hour format — so the conversion exists and only the leading
zero is left.

The time is rendered in two places: the idle clock, and the flash while an alarm is firing.

## Goals / Non-Goals

**Goals:**
- Suppress the leading zero on the hour in 12-hour mode, everywhere the time is shown.

**Non-Goals:**
- 24-hour mode, which keeps its leading zero.
- The time and alarm editors.
- Dimming rather than blanking, which needs the display engine.

## Decisions

### Blank in both render sites, through one helper

The idle path and the firing path each format the same three fields. Adding the blanking
inline would put the same conditional in two places, and the fault that follows is the
usual one: someone later changes one and not the other, and the alarm flash quietly
disagrees with the clock it is flashing.

A small helper that writes the hour — converted and blanked — is used by both.

### Leave the editors alone, deliberately

The time editor renders `edit_field[0]`, the raw 0–23 RTC hour, not `display_hours()`. It
does not follow 12-hour mode at all today, so blanking there would be applying half of a
presentation it does not otherwise use. It would also be wrong on its own terms: a field
being edited should show both of its digits, because the user is about to change them.

Whether the editors ought to present 12-hour time is a real question and a different one.
Left alone rather than half-answered here.

### Blank means blank

The glyph table maps space to all-segments-off before the lookup, so a space renders as a
dark digit rather than the invalid-character indicator. Worth confirming on the board
rather than assuming, since the invalid indicator is also a sparse pattern and a careless
glance would accept either.

## Risks / Trade-offs

- **Blanking where a `1` belongs.** Ten, eleven and twelve o'clock are the cases that
  distinguish a correct implementation from one testing the wrong threshold, and they are
  only reachable for three hours in twelve.
- **Blanking in 24-hour mode.** The other direction of the same mistake, and the existing
  scenario already pins it.
- Both faults are obvious the moment they are looked at, and invisible for most of the day
  if they are not. The checks are worth doing against specific set times rather than by
  waiting for the clock to reach them.

## Migration Plan

1. Add the helper and use it in both render sites.
2. Set the clock to each interesting hour over serial and check the display: a single-digit
   hour, ten, twelve, and one in the afternoon.
3. Switch to 24-hour mode and confirm the leading zero returns.

**Rollback:** two lines and a helper, in one file.

## Open Questions

- Should the seconds and minutes ever blank similarly? No — `9 3 0` is not a clock. The
  suppression is specific to the leading hour digit and should stay that way.
