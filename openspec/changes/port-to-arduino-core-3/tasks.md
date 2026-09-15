## 1. Dependency changes

- [ ] 1.1 In `firmware/platformio.ini`, replace the `platform = ststm32@19.7.1` pin and its explanatory comment block with `platform = ststm32@^20.0.0`
- [ ] 1.2 Move `stm32duino/STM32duino RTC` from `@ ^1.4.0` to `@ ^2.0.0`
- [ ] 1.3 Remove the `stm32duino/STM32duino Low Power @ ^1.2.5` line; re-confirm with `grep -rn "LowPower\|STM32LowPower" firmware/src firmware/lib firmware/include` that nothing references it
- [ ] 1.4 Confirm `firmware/src/` and `firmware/lib/` are untouched — this change edits one file

## 2. Build

- [ ] 2.1 Remove `firmware/.pio/` and run `pio run -d firmware` (redirect to a log; do not pipe through `tee` or the exit status is lost)
- [ ] 2.2 Confirm the build exits 0 with no `error:` lines
- [ ] 2.3 Confirm the resolved versions: `ststm32@20.x`, `framework-arduinoststm32@4.30000.0` (Arduino core 3.0.0), `STM32duino RTC@2.x`, `AceButton@1.10.1`, `SerialCommands@2.2.0`, and no Low Power
- [ ] 2.4 Record the RAM/flash figures and compare against the pre-change 24.7% / 72.2%; the experiment predicts roughly 23.3% / 69.8%
- [ ] 2.5 Note any new warnings, in particular whether `SerialCommands`' deprecated `boolean` is still only a warning

## 3. Hardware verification

- [ ] 3.1 Record the device time with `GT` before flashing, so the clock can be restored if the port misbehaves
- [ ] 3.2 Flash with `pio run -d firmware -t upload`; confirm `** Verified OK **` and `** Resetting Target **`
- [ ] 3.3 Watch the display: it shows the time and the digits advance — this is the proof that the RTC seconds interrupt still fires and `time_dirty` still drives redraws
- [ ] 3.4 Confirm the board re-enumerates as USB `0483:5740` and `pio device list --serial` still shows it
- [ ] 3.5 CDC round-trip: `TEST` replies `TEST`, `GT` replies an integer timestamp, `GO` replies the offset
- [ ] 3.6 Set the clock with `ST <timestamp>`, wait at least a minute, read it back with `GT`, and confirm it advanced by the elapsed interval — proves the LSE source and RTC configuration survived
- [ ] 3.7 Press the SET button and confirm the menu branch still draws, so AceButton's dispatch is intact under the new core
- [ ] 3.8 If any of 3.3–3.7 fails, stop and revert rather than patching forward; the failure is the result this change exists to find

## 4. Documentation

- [ ] 4.1 Replace `docs/DEVELOPMENT.md`'s "Why the platform version is pinned" section with what is true afterward: what the constraint now is and why it is a range rather than a pin
- [ ] 4.2 Correct the "Arduino core 4.x" naming in `docs/DEVELOPMENT.md` — state both the PlatformIO package version and the Arduino core version it carries (`4.30000.0` = core 3.0.0, `4.21200.0` = core 2.12.0), since conflating them is what caused the original misdiagnosis
- [ ] 4.3 Update the resolved-versions block in `docs/DEVELOPMENT.md` to the versions recorded in 2.3
- [ ] 4.4 Remove the note that `STM32duino Low Power` is an unused `lib_deps` entry that can still break a build — it is no longer a dependency
- [ ] 4.5 Update `CLAUDE.md`: drop the "don't unpin it" trap, correct the core naming, and keep the `tee` warning
- [ ] 4.6 Update the platform-pin sentence in `openspec/config.yaml`'s context block
- [ ] 4.7 Record in `docs/DEVELOPMENT.md` that commit `109d621`'s message still says "core 4.x" and is left uncorrected because the history is merged

## 5. Close out

- [ ] 5.1 Confirm the final diff touches only `firmware/platformio.ini`, `docs/DEVELOPMENT.md`, `CLAUDE.md`, and `openspec/`
- [ ] 5.2 Run `./scripts/check-dev-env.sh` and confirm it still passes
- [ ] 5.3 Commit as a single unit so the change reverts atomically, and say in the message that rolling back requires reverting the whole commit, not just the platform line
- [ ] 5.4 Run `openspec validate port-to-arduino-core-3`
