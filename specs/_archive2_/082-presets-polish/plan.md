# Implementation Plan: Arpeggiator Presets & Polish

**Branch**: `082-presets-polish` | **Date**: 2026-02-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/082-presets-polish/spec.md`

## Summary

Phase 12 (final phase) of the Ruinae arpeggiator. Creates a programmatic preset generator tool (`tools/ruinae_preset_generator.cpp`) that produces 12+ factory arp presets across 6 new categories, extends `RuinaePresetConfig` with arp-specific subcategories, verifies state round-trip fidelity, validates parameter display formatting, confirms transport integration, ensures preset change safety, and passes performance stress testing and pluginval level 5. All arp engine functionality was implemented in Phases 1-11c; this phase focuses on presets, verification, and polish.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+ on Windows, Clang on macOS, GCC on Linux)
**Primary Dependencies**: Steinberg VST3 SDK 3.7.x, VSTGUI 4.12+, Catch2 (testing)
**Storage**: Binary `.vstpreset` files (factory presets), IBStreamer binary state (plugin state)
**Testing**: Catch2 v3 (unit tests), pluginval (validation), ASan (memory safety)
**Target Platform**: Windows 10/11, macOS 11+, Linux (cross-platform)
**Project Type**: Monorepo -- VST3 plugin + shared DSP library + tools
**Performance Goals**: <0.1% CPU overhead for arpeggiator, zero heap allocations in audio thread
**Constraints**: No new arp parameters (all added in Phases 3-10), backward-compatible state format
**Scale/Scope**: 12+ factory presets, ~232 serialized arp values per preset, 1 new tool

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate -- no changes to architecture
- [x] Preset generator is a standalone tool, does not violate separation

**Principle II (Real-Time Audio Thread Safety):**
- [x] No new audio-thread code -- arpeggiator engine unchanged
- [x] Stress test verifies zero allocations in audio path (FR-017)

**Principle III (Modern C++ Standards):**
- [x] C++20 for preset generator tool
- [x] No raw new/delete -- BinaryWriter uses std::vector

**Principle V (VSTGUI Development):**
- [x] Preset browser uses existing VSTGUI infrastructure
- [x] No new custom views required

**Principle VI (Cross-Platform Compatibility):**
- [x] Preset generator is pure C++ with std::filesystem
- [x] No platform-specific code

**Principle VII (Project Structure & Build System):**
- [x] CMake target added following established pattern
- [x] Preset files in `plugins/ruinae/resources/presets/`

**Principle VIII (Testing Discipline):**
- [x] Tests written BEFORE implementation (test-first)
- [x] No existing tests broken
- [x] All new code tested

**Principle IX (Layered DSP Architecture):**
- [x] No DSP changes -- arpeggiator engine is unchanged
- [x] Preset generator does not depend on DSP layers

**Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with actual file paths, line numbers, and measured values
- [x] No requirements relaxed or quietly removed

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: RuinaePresetState, ArpPresetData, SynthPatchData (all in standalone tool, not in plugin namespace)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| RuinaePresetState | `grep -r "class RuinaePresetState\|struct RuinaePresetState" dsp/ plugins/ tools/` | No | Create New (in tool only) |
| ArpPresetData | `grep -r "class ArpPresetData\|struct ArpPresetData" dsp/ plugins/ tools/` | No | Create New (in tool only) |
| PresetDef (Ruinae) | `grep -r "struct PresetDef" tools/` | Yes (Iterum, Disrumpo) | Create New instance (tool-local, no ODR risk -- each tool is a separate compilation unit) |
| BinaryWriter (Ruinae) | `grep -r "class BinaryWriter" tools/` | Yes (Iterum, Disrumpo) | Create New instance (tool-local, no ODR risk) |

**Utility Functions to be created**: None globally. All functions are local to `tools/ruinae_preset_generator.cpp`.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| makeRuinaePresetConfig | plugins/ruinae/src/preset/ruinae_preset_config.h | Plugin | EXTEND: add 6 arp subcategories |
| getRuinaeTabLabels | plugins/ruinae/src/preset/ruinae_preset_config.h | Plugin | EXTEND: add 6 arp tab labels |
| saveArpParams | plugins/ruinae/src/parameters/arpeggiator_params.h | Plugin | REFERENCE: serialization order for generator |
| loadArpParams | plugins/ruinae/src/parameters/arpeggiator_params.h | Plugin | REFERENCE: deserialization for verification |
| formatArpParam | plugins/ruinae/src/parameters/arpeggiator_params.h | Plugin | VERIFY: display formatting correctness |
| registerArpParams | plugins/ruinae/src/parameters/arpeggiator_params.h | Plugin | VERIFY: parameter registration |
| Processor::getState | plugins/ruinae/src/processor/processor.cpp | Plugin | REFERENCE: complete serialization order |
| ArpeggiatorCore | dsp/include/krate/dsp/processors/arpeggiator_core.h | Layer 2 | REFERENCE: transport handling, note-off logic |
| PresetManager | plugins/shared/src/preset/preset_manager.h | Shared | REUSE: factory preset loading |
| disrumpo_preset_generator | tools/disrumpo_preset_generator.cpp | Tool | REFERENCE: pattern for BinaryWriter, writeVstPreset, PresetDef |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no conflicts
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- no conflicts
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors -- arpeggiator_core.h referenced only
- [x] `tools/` - Standalone tools -- BinaryWriter and PresetDef are tool-local (separate .cpp files)
- [x] `plugins/ruinae/src/preset/` - ruinae_preset_config.h to be extended

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All new types are either (a) in the standalone tool (`tools/ruinae_preset_generator.cpp`), which is a separate compilation unit with no shared headers, or (b) modifications to existing types in `ruinae_preset_config.h`. The `BinaryWriter` and `PresetDef` types in the tool file are local to that translation unit and do not conflict with the same-named types in `tools/preset_generator.cpp` or `tools/disrumpo_preset_generator.cpp`.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PresetManagerConfig | subcategoryNames | `std::vector<std::string> subcategoryNames` | Yes |
| PresetManagerConfig | processorUID | `Steinberg::FUID processorUID` | Yes |
| ArpeggiatorParams | enabled | `std::atomic<bool> enabled{false}` | Yes |
| ArpeggiatorParams | mode | `std::atomic<int> mode{0}` | Yes |
| ArpeggiatorCore | reset | `inline void reset() noexcept` | Yes |
| ArpeggiatorCore | processBlock | `inline size_t processBlock(const BlockContext& ctx, std::span<ArpEvent> outputEvents) noexcept` | Yes |
| ArpStepFlags | kStepActive | `kStepActive = 0x01` | Yes |
| ArpStepFlags | kStepSlide | `kStepSlide = 0x04` | Yes |
| ArpStepFlags | kStepAccent | `kStepAccent = 0x08` | Yes |
| TrigCondition | Fill | `Fill = 16` (index) | Yes |
| TrigCondition | Prob50 | `Prob50 = 3` (index) | Yes |
| kNoteValueDefaultIndex | value | `inline constexpr int kNoteValueDefaultIndex = 10` | Yes |
| kNoteValueDropdownCount | value | `inline constexpr int kNoteValueDropdownCount = 21` | Yes |
| kProcessorUID (Ruinae) | value | `static const Steinberg::FUID kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)` | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - ArpeggiatorParams struct, save/load/register/format functions
- [x] `plugins/ruinae/src/preset/ruinae_preset_config.h` - makeRuinaePresetConfig, getRuinaeTabLabels
- [x] `plugins/ruinae/src/processor/processor.cpp` - getState/setState serialization order
- [x] `plugins/ruinae/src/plugin_ids.h` - kProcessorUID, all kArp* parameter IDs
- [x] `plugins/shared/src/preset/preset_manager_config.h` - PresetManagerConfig struct
- [x] `plugins/shared/src/preset/preset_manager.h` - PresetManager class
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class, ArpStepFlags, TrigCondition
- [x] `dsp/include/krate/dsp/core/note_value.h` - Note value dropdown mapping
- [x] `tools/disrumpo_preset_generator.cpp` - BinaryWriter, writeVstPreset, PresetDef, serialize pattern
- [x] `tools/preset_generator.cpp` - Iterum generator pattern reference
- [x] `plugins/shared/src/ui/mod_matrix_types.h` - VoiceModRoute struct, kMaxVoiceRoutes

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| saveArpParams | Stores raw values (not normalized) | Generator writes raw float/int values matching atomic field ranges |
| saveArpParams | diceTrigger and overlays NOT serialized | Generator must NOT write these fields |
| NoteValue | Saved as int32 dropdown index, not as musical value | Use index 7 for 1/16, index 10 for 1/8, etc. |
| Modifier flags | Stored as int (not uint8_t) for lock-free guarantee | Generator writes int32 for modifier steps |
| kProcessorUID | Must be converted to 32-char ASCII hex for .vstpreset header | Convert each FUID component to 8 hex chars, uppercase |
| VoiceModRoute | 16 slots always written, each has 5 int8 + 2 float fields | Generator must write all 16 routes even if empty (default zeros) |
| State version | Written as first int32 in state, must be 1 | `w.writeInt32(1)` at start of serialize() |

## Layer 0 Candidate Analysis

No Layer 0 extraction needed. This feature introduces no new DSP utility functions. All code is either in the standalone preset generator tool or in plugin-level test files.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | No DSP algorithm -- this is preset generation and verification |
| **Data parallelism width** | N/A | Tool generates files sequentially |
| **Branch density in inner loop** | N/A | No inner loop |
| **Dominant operations** | I/O | File writing |
| **Current CPU budget vs expected usage** | N/A | One-time tool execution |

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature is a preset generator tool and verification tests. There is no DSP algorithm to optimize. The arpeggiator engine (implemented in Phases 1-11c) is unchanged.

## Higher-Layer Reusability Analysis

**This feature's layer**: Tools + Plugin preset config

### Sibling Features Analysis

**Related features at same layer** (from roadmap):
- Future Ruinae preset batches (additional synth presets)
- Arp-only preset save/load stretch goal

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| RuinaePresetState struct | HIGH | Any future Ruinae preset generator updates | Keep in tool (single file, easy to update) |
| Arp-specific preset categories | MEDIUM | Future arp preset batches | Already in RuinaePresetConfig |
| Parameter display verification test pattern | MEDIUM | Other plugin parameter audits | Keep local for now |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep RuinaePresetState in tool file | Single consumer; updating serialization format requires updating the tool anyway |
| Prefix arp categories with "Arp " | Visual distinction in preset browser; avoids collision with synth categories |

## Project Structure

### Documentation (this feature)

```text
specs/082-presets-polish/
  plan.md              # This file
  research.md          # Phase 0 research findings
  data-model.md        # Entity definitions
  quickstart.md        # Getting started guide
  contracts/           # API contracts
    preset-generator-api.md
  tasks.md             # Phase 2 task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
tools/
  ruinae_preset_generator.cpp    # NEW: Factory preset generator

plugins/ruinae/
  src/
    preset/
      ruinae_preset_config.h     # MODIFY: Add 6 arp subcategories
  resources/
    presets/                     # NEW: Factory preset directory
      Arp Classic/               # NEW: 3+ .vstpreset files
      Arp Acid/                  # NEW: 2+ .vstpreset files
      Arp Euclidean/             # NEW: 3+ .vstpreset files
      Arp Polymetric/            # NEW: 2+ .vstpreset files
      Arp Generative/            # NEW: 2+ .vstpreset files
      Arp Performance/           # NEW: 2+ .vstpreset files
  tests/unit/
    parameters/
      arpeggiator_params_test.cpp  # EXTEND: Display name & formatter verification
    processor/
      arp_integration_test.cpp     # EXTEND: Transport & preset change tests
    state_roundtrip_test.cpp       # EXTEND: Arp state round-trip fidelity
    arp_preset_e2e_test.cpp        # NEW: End-to-end preset load + playback test
    arp_performance_test.cpp       # NEW: CPU overhead & stress test

CMakeLists.txt                     # MODIFY: Add ruinae_preset_generator target
```

**Structure Decision**: Standard monorepo layout. New tool follows the established pattern at `tools/`. New tests extend existing test files where possible and create new files for distinct test categories (e2e, performance).

## Complexity Tracking

No constitution violations. All design decisions comply with the constitution.

---

## Implementation Task Groups

### Task Group 0: Branch Setup & Preset Config Extension

**Files**: `plugins/ruinae/src/preset/ruinae_preset_config.h`

**Tasks**:
1. Create branch `082-presets-polish` from `main`
2. Extend `makeRuinaePresetConfig()` to add 6 arp subcategories: `"Arp Classic"`, `"Arp Acid"`, `"Arp Euclidean"`, `"Arp Polymetric"`, `"Arp Generative"`, `"Arp Performance"`
3. Extend `getRuinaeTabLabels()` to add the same 6 labels after the existing synth labels
4. Build and verify compilation

**Covers**: FR-009 (preset config extension)

### Task Group 1: Preset Generator Tool -- Scaffolding & Build

**Files**: `tools/ruinae_preset_generator.cpp`, `CMakeLists.txt`

**Tasks**:
1. Create `tools/ruinae_preset_generator.cpp` with:
   - `BinaryWriter` class (same pattern as disrumpo_preset_generator.cpp)
   - `writeLE32()`, `writeLE64()`, `writeVstPreset()` functions
   - `kClassIdAscii` constant derived from Ruinae's `kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)`
   - `RuinaePresetState` struct with default values matching all Ruinae parameter defaults
   - `serialize()` method that writes ALL parameters in `getState()` order
   - `PresetDef` struct with name, category, state
2. Add CMake targets in root `CMakeLists.txt`:
   ```cmake
   add_executable(ruinae_preset_generator tools/ruinae_preset_generator.cpp)
   target_compile_features(ruinae_preset_generator PRIVATE cxx_std_20)
   set_target_properties(ruinae_preset_generator PROPERTIES
       RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

   add_custom_target(generate_ruinae_presets
       COMMAND ruinae_preset_generator "${CMAKE_SOURCE_DIR}/plugins/ruinae/resources/presets"
       DEPENDS ruinae_preset_generator
       COMMENT "Generating Ruinae factory arp presets")
   ```
3. Build the generator tool and verify it compiles

**Implementation detail for `serialize()`**: Must read EVERY `save*Params()` function in `plugins/ruinae/src/parameters/` to determine the exact field count, type, and order. The `RuinaePresetState` struct must have default values matching each corresponding `*Params` struct constructor defaults. This is the most complex and error-prone part of the implementation -- byte-level fidelity is required.

**Covers**: FR-009 (generator tool)

### Task Group 2: Preset Generator -- Synth Parameter Serialization

**Files**: `tools/ruinae_preset_generator.cpp`

**Tasks**:
1. Read each of the parameter save functions to extract exact serialization sequences -- there are 30 individual header files in `plugins/ruinae/src/parameters/` plus 2 inline sections in `processor.cpp`: voice routes (16 x {i8 source, i8 dest, f32 amount, i8 curve, f32 smoothMs, i8 scale, i8 bypass, i8 active}) and FX enable flags (i8 delayEnabled, i8 reverbEnabled). All 32 sources must be read; missing either inline section will produce a corrupt serialization.
2. Implement the `serialize()` method section by section:
   - State version (int32 = 1)
   - Global params (from `saveGlobalParams`)
   - Osc A params (from `saveOscAParams`)
   - Osc B params (from `saveOscBParams`)
   - Mixer params (from `saveMixerParams`)
   - Filter params (from `saveFilterParams`)
   - Distortion params (from `saveDistortionParams`)
   - Trance Gate params (from `saveTranceGateParams`)
   - Amp Env params (from `saveAmpEnvParams`)
   - Filter Env params (from `saveFilterEnvParams`)
   - Mod Env params (from `saveModEnvParams`)
   - LFO 1 params (from `saveLFO1Params`)
   - LFO 2 params (from `saveLFO2Params`)
   - Chaos Mod params (from `saveChaosModParams`)
   - Mod Matrix params (from `saveModMatrixParams`)
   - Global Filter params (from `saveGlobalFilterParams`)
   - Delay params (from `saveDelayParams`)
   - Reverb params (from `saveReverbParams`)
   - Mono Mode params (from `saveMonoModeParams`)
   - Voice routes (16 x {i8 source, i8 dest, f32 amount, i8 curve, f32 smoothMs, i8 scale, i8 bypass, i8 active})
   - FX enable flags (i8 delayEnabled, i8 reverbEnabled)
   - Phaser params (from `savePhaserParams`) + i8 phaserEnabled
   - LFO 1 Extended params (from `saveLFO1ExtendedParams`)
   - LFO 2 Extended params (from `saveLFO2ExtendedParams`)
   - Macro params (from `saveMacroParams`)
   - Rungler params (from `saveRunglerParams`)
   - Settings params (from `saveSettingsParams`)
   - Env Follower params (from `saveEnvFollowerParams`)
   - Sample Hold params (from `saveSampleHoldParams`)
   - Random params (from `saveRandomParams`)
   - Pitch Follower params (from `savePitchFollowerParams`)
   - Transient params (from `saveTransientParams`)
   - Harmonizer params (from `saveHarmonizerParams`) + i8 harmonizerEnabled
   - Arp params (matching `saveArpParams` exactly)
3. Verify: Generate a default preset, load it in the plugin, confirm no crash and all defaults correct
4. Build and run

**Covers**: FR-009, FR-010 (partial -- serialization infrastructure)

### Task Group 3: Preset Generator -- Arp Preset Definitions

**Files**: `tools/ruinae_preset_generator.cpp`

**Tasks**:
1. Implement helper functions to set arp pattern data on `RuinaePresetState`:
   - `setArpEnabled(state, true)` -- enables arp
   - `setArpMode(state, mode)` -- sets arp mode
   - `setArpRate(state, noteValueIndex)` -- sets tempo-synced rate
   - `setVelocityLane(state, length, values[])` -- sets velocity pattern
   - `setGateLane(state, length, values[])` -- sets gate pattern
   - `setPitchLane(state, length, values[])` -- sets pitch offsets
   - `setModifierLane(state, length, values[])` -- sets step modifier bitmasks
   - `setRatchetLane(state, length, values[])` -- sets ratchet counts
   - `setConditionLane(state, length, values[])` -- sets trigger conditions
   - `setEuclidean(state, enabled, hits, steps, rotation)` -- configures Euclidean
   - `setSpice(state, amount)` -- sets spice
   - `setHumanize(state, amount)` -- sets humanize
2. Implement helper functions for synth patches:
   - `setSynthPad(state)` -- warm pad oscillator settings
   - `setSynthBass(state)` -- punchy bass settings
   - `setSynthLead(state)` -- bright lead settings
   - `setSynthAcid(state)` -- squelchy acid settings (for Acid presets)
3. Implement `createAllPresets()` with minimum 12 presets:

   **Classic (3 presets)**:
   - "Basic Up 1/16" -- Mode=Up, Rate=1/16 (index 7), velocity uniform, gate 80%, no modifiers
   - "Down 1/8" -- Mode=Down, Rate=1/8 (index 10), velocity uniform, gate 90%
   - "UpDown 1/8T" -- Mode=UpDown, Rate=1/8T (index 9), velocity accent pattern

   **Acid (2 presets)**:
   - "Acid Line 303" -- Mode=Up, Rate=1/16 (index 7), slide on steps 3,7,11,15, accent on steps 1,5,9,13, acid synth patch, 8-step pattern
   - "Acid Stab" -- Mode=AsPlayed, Rate=1/16, accent all steps, gate 40%, staccato acid patch

   **Euclidean World (3 presets)**:
   - "Tresillo E(3,8)" -- Euclidean enabled, hits=3, steps=8, rotation=0 (classic 3-3-2)
   - "Bossa E(5,16)" -- Euclidean enabled, hits=5, steps=16, rotation=0
   - "Samba E(7,16)" -- Euclidean enabled, hits=7, steps=16, rotation=0

   **Polymetric (2 presets)**:
   - "3x5x7 Evolving" -- Velocity lane length=3, gate lane length=5, pitch lane length=7 (LCM=105)
   - "4x5 Shifting" -- Ratchet lane length=4, velocity lane length=5, gate lane length=6 (LCM=60, 3 differentially-lengthed lanes satisfying FR-006)

   **Generative (2 presets)**:
   - "Spice Evolver" -- Spice=0.7, humanize=0.3, varied conditions (Prob50 on some steps)
   - "Chaos Garden" -- Spice=0.9, all condition lanes set to various probabilities

   **Performance (2 presets)**:
   - "Fill Cascade" -- Fill conditions on steps 5,6,7,8,13,14,15,16 (denser when Fill active)
   - "Probability Waves" -- Prob75 on even steps, Prob25 on odd steps, ratchet lane length=8 (integer values 1-4, e.g. steps: 1,2,1,2,1,2,1,2)

4. Generate all presets and verify file count/structure
5. Build and run

**Covers**: FR-001 through FR-010, SC-001

### Task Group 4: State Round-Trip Verification Tests

**Files**: `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`

**Tasks**:
1. Write test "Arp state round-trip preserves all lane values":
   - Set all 6 lanes to non-default values with non-default lengths
   - Call `getState()` then `setState()` on a fresh processor
   - Compare every atomic field with exact equality
2. Write test "Arp state round-trip preserves Euclidean settings":
   - Set euclidean enabled=true, hits=5, steps=13, rotation=3
   - Round-trip and verify exact match
3. Write test "Arp state round-trip preserves condition values":
   - Set various TrigCondition values across all 32 steps
   - Round-trip and verify exact match
4. Write test "Arp state round-trip preserves modifier bitmasks":
   - Set combinations of Slide, Accent, Tie, Rest across steps
   - Round-trip and verify exact match
5. Write test "Arp state round-trip preserves float values bit-identically":
   - Set spice=0.73, humanize=0.42, ratchetSwing=62.0
   - Round-trip and verify bit-identical (memcmp or reinterpret_cast)
6. Write test "Pre-arp preset loads with arp disabled":
   - Create a state blob without arp data (truncated after harmonizer)
   - Load and verify arp defaults
7. Build and run tests

**Covers**: FR-011 through FR-015, SC-004

### Task Group 5: Parameter Display Verification Tests

**Files**: `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`

**Tasks**:
1. Write test "All arp parameters have Arp prefix in display name":
   - Iterate all kArp* parameter IDs (excluding playhead params)
   - For each, get the parameter info and verify name starts with "Arp"
2. Write test "Arp step parameters have non-padded numbering":
   - Verify "Arp Vel Step 1" (not "Arp Vel Step 01")
   - Verify "Arp Vel Step 16" (not "Arp Vel Step 016")
3. Write test "formatArpParam produces readable output for all parameter types":
   - Mode at value 0.0 -> "Up"
   - Note value at index 7 -> "1/16"
   - Gate length at 0.3970 -> "80%"
   - Swing at 0.333 -> "25%"
   - Pitch step at 0.75 -> "+12 st"
   - Pitch step at 0.25 -> "-12 st"
   - Ratchet step at 0.333 -> "2x"
   - Condition step at index 3 -> "50%"
   - Spice at 0.73 -> "73%"
   - Ratchet swing at 0.48 -> "62%"
   - Modifier step at kStepActive|kStepSlide|kStepAccent (0x0D) -> "SL AC" (per FR-022: human-readable flag abbreviations, not hex literals)
4. Build and run tests

**Covers**: FR-020 through FR-022, SC-005, SC-006

### Task Group 6: Transport Integration Verification Tests

**Files**: `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`

**Tasks**:
1. Write test "Transport start resets arp to step 1":
   - Configure arp, play some steps, stop transport
   - Start transport again, verify first event is from step 1
2. Write test "Transport stop sends all notes off":
   - Start arp playing, accumulate some note-on events
   - Stop transport, verify matching note-off events emitted
3. Write test "Rapid transport start/stop cycles":
   - Start/stop transport within <100ms equivalent blocks
   - Verify no duplicate notes, no missed first steps
4. Write test "Preset change during playback flushes notes":
   - Start arp with preset A
   - Simulate state change mid-playback
   - Verify all preset-A notes get note-off before preset-B events
5. Build and run tests

**Covers**: FR-023 through FR-030, SC-007, SC-008

### Task Group 7: End-to-End Preset Load & Playback Test

**Files**: `plugins/ruinae/tests/unit/arp_preset_e2e_test.cpp` (new)

**Tasks**:
1. Write test "End-to-end: Load Basic Up 1/16 preset, play C-E-G, verify note sequence":
   - Generate the "Basic Up 1/16" preset state (same parameters as the factory preset)
   - Load via `setState()`
   - Feed MIDI note-on for C4 (60), E4 (64), G4 (67)
   - Run `process()` for enough blocks to cover several arp cycles
   - Collect emitted note events
   - Verify: notes are C4, E4, G4 in ascending order, repeating
   - Verify: velocities match the uniform velocity pattern
   - Verify: timing offsets are consistent with 1/16 note rate at the test tempo
2. Build and run test

**Covers**: SC-011

### Task Group 8: Performance & Stress Tests

**Files**: `plugins/ruinae/tests/unit/arp_performance_test.cpp` (new)

**Tasks**:
1. Write test "CPU overhead of arp is less than 0.1%":
   - Measure CPU time for N blocks with arp disabled
   - Measure CPU time for N blocks with arp enabled (moderate pattern)
   - Calculate delta as percentage of total
   - Assert < 0.1%
2. Write test "Stress test: 10 notes, ratchet=4, all lanes, spice=100%, 200 BPM, 1/32":
   - Configure worst-case scenario
   - Run for 10 seconds worth of blocks
   - Verify no crashes, no assertion failures
   - Verify all note-on events have matching note-off events
3. Write test "Zero memory growth during stress test":
   - Run stress scenario
   - Verify no std::bad_alloc or unexpected memory patterns
   - (Full ASan verification is a build-time check, not an in-test assertion)
4. Build and run tests

**Covers**: FR-016 through FR-019, SC-002, SC-003

### Task Group 9: Pluginval & Regression Verification

**Tasks**:
1. Build full plugin: `cmake --build build/windows-x64-release --config Release`
2. Run all existing tests: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
3. Verify zero regressions (all previously-passing tests still pass)
4. Run pluginval level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
5. Verify pass

**Covers**: FR-031, FR-032, SC-009, SC-010

### Task Group 10: Final Polish & Compliance

**Tasks**:
1. Run clang-tidy on all changed files
2. Fix any warnings or style issues
3. Verify all preset files are generated correctly
4. Fill compliance table in spec.md with actual evidence
5. Update architecture docs at `specs/_architecture_/` if needed
6. Final commit

---

## Key Implementation Details

### Ruinae Processor FUID to ASCII Hex

The `kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)` converts to the ASCII hex string needed for the `.vstpreset` header. The FUID stores 4 uint32 values; each is written as 8 uppercase hex chars:

```
A3B7C1D5 2E4F6A8B 9C0D1E2F 3A4B5C6D
-> "A3B7C1D52E4F6A8B9C0D1E2F3A4B5C6D"
```

### Serialization Validation Strategy

After implementing the generator, the most critical validation is:
1. Generate a preset with all defaults
2. Create a Ruinae processor instance
3. Call `getState()` to get the "canonical" default state
4. Compare the generator's output byte-for-byte with the canonical state
5. Any mismatch indicates a serialization error in the generator

This validation can be a unit test that links against the plugin library.

### Preset Category Naming

Using "Arp " prefix for all 6 new categories:
- "Arp Classic" (not just "Classic" -- avoids ambiguity)
- "Arp Acid"
- "Arp Euclidean" (shortened from "Euclidean World" for UI space)
- "Arp Polymetric"
- "Arp Generative"
- "Arp Performance"

These names appear as directory names in the preset file system and as tab labels in the preset browser.
