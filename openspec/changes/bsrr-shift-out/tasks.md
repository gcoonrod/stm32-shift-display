## 1. Baseline

- [x] 1.1 Record the starting flash and RAM figures, and the current shift-out cost from the disassembly, so the change can be judged against something
- [x] 1.2 Capture the golden command output as before, so it can be confirmed that nothing outside the display driver moved
- [x] 1.3 Note the current shift clock rate implied by the instruction count, as the figure the measurement will replace

## 2. Resolve ports and masks, without changing the writes

- [x] 2.1 Resolve each control pin to its GPIO port and bit in `begin()`, alongside the existing `pinMode` calls
- [x] 2.2 Precompute the `BSRR` words the inner loop will need, including the combined data-and-clock-low pair
- [x] 2.3 Detect whether the data and clock pins share a port, and record which path applies
- [x] 2.4 Build and flash with the writes still going through `digitalWrite`; confirm the display is unchanged

## 3. Convert the writes

- [x] 3.1 Replace the per-bit `digitalWrite` triple in `shiftOutByte` with `BSRR` writes, using the combined path when the ports match and per-pin writes when they do not
- [x] 3.2 Convert `latch()` to `BSRR`
- [x] 3.3 Convert `clear()` to `BSRR`
- [x] 3.4 Move the inter-edge delay out of the inner loop's conditional and make it the deliberate throttle; remove the unused `_delay_ms`
- [x] 3.5 Confirm no write addresses the port as a whole — every store names only the driver's own pins, so `~OE` is untouched
- [x] 3.6 Build and flash at a deliberately slow rate; confirm all six digits are correct before going near the limit

## 4. Measure the safe rate

- [x] 4.1 Add the sweep build: a runtime-settable inter-edge delay and a temporary command to change it, both behind a build flag so production carries neither
- [x] 4.2 Add the two test patterns — all-segments-lit to expose dropped bits, and six distinct digits to expose a shifted stream
- [x] 4.3 Flash the sweep build and step the delay down while watching the display, alternating patterns, until corruption appears
- [x] 4.4 Record the failure point, and note whether it was a segment fault or a shift fault — they implicate different timings
- [x] 4.5 If no failure appears at the fastest achievable rate, record that instead; the constant is then zero and the margin is whatever the loop can do
- [x] 4.6 Choose the shipping rate with margin over the measured edge, and record the reasoning

## 5. Settle the production build

- [x] 5.1 Bake the chosen delay in as a compile-time constant and rebuild without the sweep flag
- [x] 5.2 Confirm the sweep command and the runtime variable are absent from the shipped image
- [x] 5.3 Exercise the full glyph set in all six positions at the chosen rate, watching for intermittent wrong segments
- [ ] 5.4 Leave the display running for a spell and check it again, since the failure this guards against is occasional rather than immediate. *Open by nature: the display has run continuously through the pattern sweep and the device checks with nothing observed, but that is minutes, not days. Satisfied by using the clock normally.*

## 6. Verification

- [x] 6.1 Diff the command output against the golden capture; nothing outside the display driver should have moved
- [x] 6.2 Confirm brightness still works across its full range, and that changing it mid-refresh disturbs nothing
- [x] 6.3 Confirm the alarm flash and the field-edit blink still render correctly, since both drive the display harder than the idle path
- [x] 6.4 Walk the menu and confirm every label still renders
- [x] 6.5 Record the final flash and RAM figures, and the measured shift rate against the 615 kHz starting point
- [x] 6.6 Update `docs/DEVELOPMENT.md` with the measured failure point, the chosen rate and the margin, so the constant is not an unexplained number
- [x] 6.7 Update `CLAUDE.md` where it describes the display driver
- [x] 6.8 Run `openspec validate bsrr-shift-out`
