# Research: Arpeggiator Presets & Polish (082-presets-polish)

## Research Summary

All NEEDS CLARIFICATION items have been resolved through codebase investigation. No external library research was required as this feature uses only existing internal infrastructure.

---

## R1: Ruinae Processor State Serialization Sequence

**Decision**: The preset generator must replicate the exact binary serialization sequence from `Processor::getState()`.

**Findings**: The serialization sequence in `plugins/ruinae/src/processor/processor.cpp` (line 488-559) is:

1. `int32` state version (`kCurrentStateVersion = 1`)
2. `saveGlobalParams` (global synth params)
3. `saveOscAParams` (oscillator A)
4. `saveOscBParams` (oscillator B)
5. `saveMixerParams` (mixer)
6. `saveFilterParams` (filter)
7. `saveDistortionParams` (distortion)
8. `saveTranceGateParams` (trance gate)
9. `saveAmpEnvParams` (amp envelope)
10. `saveFilterEnvParams` (filter envelope)
11. `saveModEnvParams` (mod envelope)
12. `saveLFO1Params` (LFO 1)
13. `saveLFO2Params` (LFO 2)
14. `saveChaosModParams` (chaos mod)
15. `saveModMatrixParams` (mod matrix)
16. `saveGlobalFilterParams` (global filter)
17. `saveDelayParams` (delay)
18. `saveReverbParams` (reverb)
19. `saveMonoModeParams` (mono mode)
20. Voice routes: 16 x {int8 source, int8 destination, float amount, int8 curve, float smoothMs, int8 scale, int8 bypass, int8 active}
21. FX enable flags: int8 delayEnabled, int8 reverbEnabled
22. `savePhaserParams` + int8 phaserEnabled
23. `saveLFO1ExtendedParams`
24. `saveLFO2ExtendedParams`
25. `saveMacroParams`
26. `saveRunglerParams`
27. `saveSettingsParams`
28. `saveEnvFollowerParams`
29. `saveSampleHoldParams`
30. `saveRandomParams`
31. `savePitchFollowerParams`
32. `saveTransientParams`
33. `saveHarmonizerParams` + int8 harmonizerEnabled
34. `saveArpParams` (arpeggiator)

**Rationale**: The Ruinae preset generator must produce binary data matching this exact sequence. The approach established by `tools/disrumpo_preset_generator.cpp` uses a `BinaryWriter` class that writes `int32`, `float`, and `int8` values into a `std::vector<uint8_t>`, then wraps it in a `.vstpreset` envelope.

**Alternative considered**: Using a streaming approach with actual `IBStreamer` was rejected because the preset generator is a standalone tool that does not link against the VST3 SDK.

---

## R2: Preset Generator Pattern (Established by Iterum and Disrumpo)

**Decision**: Follow the `tools/disrumpo_preset_generator.cpp` pattern exactly.

**Findings**:
- `BinaryWriter` class with `writeInt32`, `writeFloat`, `writeInt8` methods (line 25-42)
- State struct with all parameter fields and a `serialize()` method that produces `std::vector<uint8_t>` (e.g., `DisrumpoPresetState::serialize()`, line 250-401)
- `writeVstPreset()` function that wraps binary data in `.vstpreset` file format (line 420-451): header (4 bytes "VST3", 4 bytes version=1, 32 bytes classId ASCII, 8 bytes listOffset), component data, chunk list ("List", count=1, "Comp", offset, size)
- `PresetDef` struct with name, category, and state
- `createAllPresets()` function returning `std::vector<PresetDef>`
- `main()` that iterates presets, creates category subdirectories, sanitizes filenames, and writes each preset

**Key detail -- classId ASCII**: For Disrumpo, the classId is a 32-char hex string derived from the FUID. For Ruinae, `kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)` must be converted to the ASCII hex representation used in the vstpreset header.

**Rationale**: Established pattern with proven correctness across two plugins.

---

## R3: RuinaePresetConfig Subcategory Extension

**Decision**: Add 6 new arp-specific subcategories after the existing 6 synth subcategories.

**Findings**:
- Current config (`plugins/ruinae/src/preset/ruinae_preset_config.h` line 16-25):
  ```cpp
  subcategoryNames = {"Pads", "Leads", "Bass", "Textures", "Rhythmic", "Experimental"}
  ```
- The `PresetManagerConfig::subcategoryNames` drives directory scanning (the `PresetManager` scans each named subdirectory under the factory/user preset root).
- Tab labels (`getRuinaeTabLabels()` line 28-33): `{"All", "Pads", "Leads", "Bass", "Textures", "Rhythmic", "Experimental"}`
- Adding 6 arp categories means 12 total subcategories and 13 tab labels (including "All").

**New subcategories**: "Arp Classic", "Arp Acid", "Arp Euclidean", "Arp Polymetric", "Arp Generative", "Arp Performance"

**Rationale**: Prefixing with "Arp " makes the categories visually distinct in the preset browser and avoids ambiguity with the synth subcategories. The preset files will be stored in subdirectories named with these exact strings.

---

## R4: Arp Parameter Serialization in saveArpParams

**Decision**: The preset generator must replicate the exact sequence in `saveArpParams()`.

**Findings** (`plugins/ruinae/src/parameters/arpeggiator_params.h` line 895-966):
1. Base params (11 values): enabled(int32), mode(int32), octaveRange(int32), octaveMode(int32), tempoSync(int32), noteValue(int32), freeRate(float), gateLength(float), swing(float), latchMode(int32), retrigger(int32)
2. Velocity lane: length(int32) + 32 steps(float)
3. Gate lane: length(int32) + 32 steps(float)
4. Pitch lane: length(int32) + 32 steps(int32)
5. Modifier lane: length(int32) + 32 steps(int32) + accentVelocity(int32) + slideTime(float)
6. Ratchet lane: length(int32) + 32 steps(int32)
7. Euclidean: enabled(int32) + hits(int32) + steps(int32) + rotation(int32)
8. Condition lane: length(int32) + 32 steps(int32) + fillToggle(int32)
9. Spice/Humanize: spice(float) + humanize(float)
10. Ratchet swing: ratchetSwing(float)

Total: ~232 individual values serialized for the arp section.

---

## R5: Note Value Index Mapping

**Decision**: Use the established dropdown indices for note values.

**Findings** (from `dsp/include/krate/dsp/core/note_value.h`):
- Index 0: 1/64T, 1: 1/64, 2: 1/64D
- Index 3: 1/32T, 4: 1/32, 5: 1/32D
- Index 6: 1/16T, 7: 1/16, 8: 1/16D
- Index 9: 1/8T, 10: 1/8 (default), 11: 1/8D
- Index 12: 1/4T, 13: 1/4, 14: 1/4D
- Index 15: 1/2T, 16: 1/2, 17: 1/2D
- Index 18: 1/1T, 19: 1/1, 20: 1/1D

Key indices for presets:
- 1/16 = index 7
- 1/8 = index 10
- 1/8T = index 9 (triplet)
- 1/4 = index 13
- 1/32 = index 4

---

## R6: Modifier Flag Values

**Decision**: Use the established `ArpStepFlags` enum values.

**Findings** (from `dsp/include/krate/dsp/processors/arpeggiator_core.h` line 42-47):
- `kStepActive = 0x01` -- note fires (Off = Rest)
- `kStepTie = 0x02` -- sustain previous note
- `kStepSlide = 0x04` -- legato portamento
- `kStepAccent = 0x08` -- velocity boost

Combinations:
- Active step: `0x01`
- Rest: `0x00`
- Active + Slide: `0x05`
- Active + Accent: `0x09`
- Active + Slide + Accent: `0x0D`
- Active + Tie: `0x03`

---

## R7: TrigCondition Index Mapping

**Decision**: Use the established `TrigCondition` enum values.

**Findings** (from `dsp/include/krate/dsp/processors/arpeggiator_core.h` line 74-94):
- 0: Always, 1: Prob10, 2: Prob25, 3: Prob50, 4: Prob75, 5: Prob90
- 6: 1:2, 7: 2:2, 8: 1:3, 9: 2:3, 10: 3:3
- 11: 1:4, 12: 2:4, 13: 3:4, 14: 4:4
- 15: First, 16: Fill, 17: !Fill

---

## R8: Transport Handling in ArpeggiatorCore

**Decision**: Transport start/stop handling is already implemented. Verification tests needed.

**Findings**:
- Transport stop (`processBlock`, line 620-633): When `ctx.isPlaying` transitions from true to false (`wasPlaying_` guard), emits NoteOff for all `currentArpNotes_`, clears pending note-offs, resets ratchet state, resets lanes.
- Transport start: On first block with `ctx.isPlaying == true` and `wasPlaying_ == false`, the arp fires step 1 (`firstStepPending_ = true` from `reset()`).
- `reset()` (line 206-221): Clears all counters, note tracking, and lane positions. Called on `setEnabled(true)` and other state transitions.

**No new code needed**: Only verification tests.

---

## R9: Preset Change Safety

**Decision**: The `setState()` mechanism inherently handles preset changes atomically within a single `process()` call.

**Findings**:
- In VST3, `setState()` is called from the host thread, setting atomic parameter values.
- `processParameterChanges()` in the processor reads changed parameters at the start of each `process()` call.
- For the arpeggiator, parameter changes are applied via atomic loads in `applyParamsToEngine()`.
- The `ArpeggiatorCore::setEnabled()` method (line 287-299) includes a `needsDisableNoteOff_ = true` flag that triggers note-off emission on the next `processBlock()` call when disabling.
- When a new preset with different arp parameters is loaded, the parameter changes propagate atomically. Lane positions may need clamping when lane lengths change (this is handled by the `ArpLane` class which wraps positions modulo length).

**Important**: If a preset changes the arp-enabled state from true to false (or the arp mode/pattern changes dramatically), the note-off flush happens via the `needsDisableNoteOff_` mechanism in `processBlock()`. For pattern-only changes (same enabled state), the lanes update atomically via atomic loads and the step position wraps naturally.

---

## R10: Synth Parameter Save Functions (for Preset Generator)

**Decision**: The preset generator must serialize ALL synth parameter packs in the exact same order as `getState()`. Each save function writes a specific sequence of int32/float/int8 values. The generator must replicate this byte-for-byte.

**Findings**: There are 32 individual parameter save function calls plus voice routes and enable flags. Each parameter file in `plugins/ruinae/src/parameters/` defines both `save*Params()` and `load*Params()` inline functions. The preset generator must read each save function to determine the exact field count, types, and order.

**Approach**: Rather than trying to replicate all 32 save functions (which would be extremely error-prone and fragile), the preset generator should define a `RuinaePresetState` struct that mirrors the complete state, with a `serialize()` method that writes all fields in the same order as `getState()`. Default values should match the defaults in each `*Params` struct.

---

## R11: Existing Arp Tests

**Decision**: Extend existing test files rather than creating new ones where appropriate.

**Findings**:
- `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` -- 70 test cases covering parameter registration, denormalization, formatting, and display names
- `plugins/ruinae/tests/unit/state_roundtrip_test.cpp` -- state save/load round-trip tests (uses TestableProcessor pattern)
- `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` -- arp integration tests
- `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp` -- arp modifier integration tests

New tests for this phase should:
- Add round-trip tests for arp state to `state_roundtrip_test.cpp`
- Add performance/stress tests as new test file
- Add parameter display verification tests to `arpeggiator_params_test.cpp`
- Add preset load/playback end-to-end test as new test file

---

## R12: Factory Preset Directory Structure

**Decision**: Factory presets go in `plugins/ruinae/resources/presets/{subcategory}/`.

**Findings**:
- Disrumpo pattern: `plugins/disrumpo/resources/presets/{category}/{Preset_Name}.vstpreset`
- Ruinae currently has NO `resources/presets/` directory (it was not needed before factory presets)
- The `PresetManager` scans `{factoryDir}/{subcategoryName}/` for `.vstpreset` files
- The preset generator creates the directory structure and files

**Action**: The CMakeLists.txt for Ruinae needs to be updated to install/copy the preset files, similar to how Disrumpo handles it. The `generate_ruinae_presets` custom target should be added to the root CMakeLists.txt.
