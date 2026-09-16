## 1. Settle the unknowns before writing any code

- [x] 1.1 Determine whether the Arduino core claims TIM1 for the HAL timebase, tone or servo; if it does, switch the design to TIM3 (CH3 → DMA1 ch 2, CH4 → DMA1 ch 3) and record which timer was chosen and why. *It does not — every TIM1 reference is generic dispatch. The variant names TIM3 `TIMER_TONE` and TIM4 `TIMER_SERVO`, so TIM1 stays the choice and TIM3 is a costlier fallback than assumed. Recorded in design.md.*
- [x] 1.2 Confirm DMA1 channels 2 and 3 are unclaimed in the current image, the same way the proposal confirmed all seven were — by symbol, not by assumption. *Both DMA1 literals in the image are in `HAL_UART_IRQHandler`; the console is USB CDC, so no channel is configured.*
- [x] 1.3 Confirm nothing else in the build calls `analogWriteFrequency` after `setup()`, since the slice lock depends on the output-enable frequency not moving. *Called exactly once, `main.cpp:263`, before `display.begin()`.*
- [x] 1.4 Record the baseline flash and RAM figures, and correct the stale block in `docs/DEVELOPMENT.md` that still quotes the pre-flash-reduction numbers. *Baseline RAM 4,920 / 20,480 and Flash 37,220 / 65,536; docs corrected from 5052 / 47348.*

## 2. Derive the constants

- [x] 2.1 Express the timer reload, the compare values and the slice length as expressions over `PWM_FREQ_HZ` and the core clock, so none of them is an independent literal. *`ShiftDisplayEngine.h`. `F_CPU` turned out to be the runtime `SystemCoreClock`, so the core clock is a declared constant checked at runtime by `shift_engine_clock_ok()`; everything else derives.*
- [x] 2.2 Add a static assertion that the slice period is an exact whole number of output-enable periods, so retuning the PWM frequency fails the build rather than producing per-digit brightness errors. *Verified to bite: `PWM_FREQ_HZ` 7000 fails the divides-the-clock assert, 3600 fails the divides-into-48 assert, 32 slices fails the 200 Hz flicker floor, 12 slices fails the power-of-two check.*
- [x] 2.3 Add a static assertion that no constructed word can set or clear bit 0 or bit 16 of the port, so nothing replayed by the DMA can reach `~OE`. *Two layers, because the masks resolve at runtime: a `static_assert` in main.cpp that the five pin defines are distinct (verified to fail when OEB is aliased onto SER), and `ShiftDisplay::oe_collides()` in `begin()` proving no driven pin shares a port bit with `~OE` — the driver refuses to initialise if one does. Checking the four masks once is equivalent to checking every word, since every word is composed from them.*
- [x] 2.4 Confirm the derived bit clock against the rate measured in `bsrr-shift-out`, and record the margin. *192 kHz against ~7 MHz: a 36x margin, and the 7 MHz is a floor rather than a measured limit. A `static_assert` caps the derived bit clock at 1 MHz.*

## 3. Build the waveform, with the engine stopped

- [x] 3.1 Add the buffer and the word builder: one data word per bit, `SER` set-or-clear with `SRCLK` low, and the `RCLK` set/clear bits in words 0 and 1 of each slice. *`build_waveform()`. Bit order matches `update_display()`/`shiftOutByte()` exactly — bytes `_buffer[5]`..`_buffer[0]`, LSB first, DP in bit 0. Verified from the device: `w0=00180004` (clear SER|SRCLK, set RCLK), `w1` releases RCLK, `w2+` carry no latch bits, and no word sets bit 0 or 16.*
- [x] 3.2 Add the single constant clock-high word the second channel will replay. *`_clk_high_word`, a member so the DMA channel has a source address.*
- [x] 3.3 Render the current display buffer into one slice and shift that slice out through the *existing* bit-banged path; confirm the display is identical to today. *Confirmed on hardware at 12:34:56 in 24-hour mode — correct digits, nothing garbled.*
- [x] 3.4 Confirm the latch-the-previous-slice arrangement produces the right content when slices are shifted back to back, still by hand. *Confirmed — content in the right positions, not shifted by one. Per-digit dimming also works: a single digit steps visibly dimmer, a 1-2-3-4-6-8 gradient reads as a ramp, and a hand-driven fade read as a flourish rather than as steps.*

## 4. Start the engine

- [x] 4.1 Put the existing bit-banged path behind a build flag, so a bad configuration is one rebuild from a working clock. *`SHIFT_ENGINE_DMA`, default 1; `-D SHIFT_ENGINE_DMA=0` restores the bit-banged shift-out. `WE 0`/`WE 1` also swap them at runtime in the verify build.*
- [x] 4.2 Configure the timer: reload, the two compare channels, and DMA requests enabled on both. *Changed from the design: data rides the **update** event rather than CH1, because CH1 would need `CCR1 = 0` — a compare coinciding with the counter wrap, an edge case not worth betting a hard-to-debug peripheral on. Update *is* the wrap. So TIM1 update → DMA1 ch5 and TIM1 CC1 (at mid-period) → DMA1 ch2; both channels equally free.*
- [x] 4.3 Configure the data channel — memory-to-peripheral, 32-bit both ends, memory increment on, circular, destination `GPIOA->BSRR`. *DMA1 ch5. Verified live: CNDTR counts down and reloads.*
- [x] 4.4 Configure the clock channel — same, but memory increment off, pointed at the single constant word. *DMA1 ch2, source `_clk_high_word`.*
- [x] 4.5 Start with every digit at full brightness in every slice; confirm the display is indistinguishable from the bit-banged build. *Confirmed on hardware — "it looks correct". Measured frame rate 500 Hz exactly across three runs, counted from CNDTR reloads, matching the derived target.*
- [x] 4.6 Confirm the CPU is genuinely out of it: hold the main loop busy for longer than a frame and confirm the display does not flicker, dim or corrupt. *Confirmed: a 1-2-3-4-6-8 per-digit gradient held steady through 5 s with the main loop unable to execute an instruction. The first version of this test was weak — the display is non-multiplexed, so the 595s hold their last latch and a blocked loop leaves any build lit and frozen. A **dimmed** digit is the discriminating case, since it exists only while something re-shifts continuously.*
- [ ] 4.7 Confirm the display is blank before the engine starts and through reset, rather than showing whatever the registers powered up holding

## 5. Rework the driver's entry points

- [ ] 5.1 Make `writeDisplay()` and the `Print` path write the waveform buffer rather than shift; confirm content appears within one frame with no caller latching anything
- [ ] 5.2 Decide what `latch()` means now that latching is continuous, and either keep it as a no-op with a comment or remove it, having checked every caller
- [ ] 5.3 Rework `clear()` — the `~SRCLR` pulse no longer persists, since the next frame overwrites it — and find every caller relying on the old semantics
- [ ] 5.4 Confirm `enable()`/`disable()` still work, since they are `~OE` operations and should be untouched by any of this
- [ ] 5.5 Confirm a mid-frame buffer rewrite produces at most one torn frame and nothing worse

## 6. Per-digit brightness

- [ ] 6.1 Add a per-position slice count, defaulting to full, and render each slice according to it
- [ ] 6.2 Confirm a single dimmed digit is visibly dimmer and still legible, with the others unaffected
- [ ] 6.3 Confirm zero slices blanks a position and leaves the others alone
- [ ] 6.4 Confirm equal slice counts on different positions look equally bright — this is the check that the slice/output-enable lock actually holds. **Must be done under the DMA engine, not the CPU replay.** Six digits at 4/8 already look uniform under replay, but that proves nothing about the lock: the replay free-runs, so its phase against `~OE` drifts and the error averages away. The engine is locked instead, and only a locked run tests locking
- [ ] 6.5 Watch a dimmed digit for a spell at several global brightness levels, looking for beat or shimmer that is not commanded
- [ ] 6.6 Confirm the global brightness setting still dims everything together and keeps the relative proportions, and that a digit at full relative brightness is no brighter than the global minimum allows

## 7. The menu-exit fade

- [ ] 7.1 Add the transition detection: compare the state either side of `stateMachine.update()` and recognise `MENU`/`EDIT` → `IDLE`, so backing out, committing and timing out all trigger alike
- [ ] 7.2 Confirm `FIRING` → `IDLE` does not trigger it — dismissing an alarm shows the time at once
- [ ] 7.3 Add the fade stepper as its own call in the loop tail alongside `update_leds()`, driven from `millis()` against a start stamp rather than by counting steps
- [ ] 7.4 Confirm `render()` is untouched and nothing is re-shifted for the fade; the `time_dirty` path must stay exactly as cheap as it is
- [ ] 7.5 Wire the per-position ramp: position *p* starts at `p × STAGGER` and reaches full over `FADE`
- [ ] 7.6 Add the abort: any button press, menu re-entry or alarm fire snaps every position to full immediately
- [ ] 7.7 Add a build-flagged serial command to trigger the fade without walking the menu, so it can be watched repeatedly; confirm it is absent from the production image
- [ ] 7.8 Watch the fade and judge whether 8 levels reads as a flourish or as steps. If it steps, work the remedies in the design's order — the global `~OE` ramp first, since it costs no RAM and buys more than more slices do — and record which was needed and why
- [ ] 7.9 Confirm the fade ends at the configured brightness at every level including the minimum, and that a digit is never left dim by any exit path
- [ ] 7.10 Confirm the blanked leading position stays blank throughout in 12-hour mode, and that the remaining digits keep their timing
- [ ] 7.11 Hold the loop busy mid-fade and confirm the fade shortens rather than stretching
- [ ] 7.12 Tune `STAGGER` and `FADE` by eye from the 70 ms / 180 ms starting point, and record the values chosen

## 8. Verification

- [ ] 8.1 Diff the serial command output against a golden capture taken before the change; nothing outside the display should have moved
- [ ] 8.2 Exercise the full glyph set in all six positions and confirm no intermittent wrong segments, using the all-segments and six-distinct-digits patterns rather than the time
- [ ] 8.3 Confirm the alarm flash and the field-edit blink still render, since both drive the display harder than the idle path
- [ ] 8.4 Walk the menu and confirm every label renders
- [ ] 8.5 Confirm the leading hour zero is still blanked in 12-hour mode and still present in 24-hour mode — this change must not disturb behaviour that already shipped
- [ ] 8.6 Record final flash and RAM against the baseline, and state the buffer's share of RAM
- [ ] 8.7 Update `CLAUDE.md` where it describes the display pipeline, which currently says the shift loop is two stores per bit driven by the CPU
- [ ] 8.8 Update `docs/DEVELOPMENT.md` with the timer, the two DMA channels, the derived constants, the reason the slice period is locked to the output-enable period, and the fade's tuned timings
- [ ] 8.9 Run `openspec validate dma-display-engine`

## 9. Deferred, deliberately

- [ ] 9.1 Decide whether per-digit levels should persist in backup registers, once there is something to judge by eye
