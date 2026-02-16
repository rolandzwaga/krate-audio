# Implementation Plan: Additional Modulation Sources

**Branch**: `059-additional-mod-sources` | **Date**: 2026-02-16 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/059-additional-mod-sources/spec.md`

## Summary

Wire up all 5 remaining modulation sources (Env Follower, Sample & Hold, Random, Pitch Follower, Transient Detector) that already have full DSP implementations in the ModulationEngine but lack plugin-layer parameter exposure. This involves 18 new automatable parameters (IDs 2300-2799), 5 new parameter files, 15 new RuinaeEngine forwarding methods, processor wiring, state persistence (v14 -> v15), 18 control-tags, and populating 5 empty uidesc view templates. This follows the exact same integration pattern established by Spec 057 (Macros & Rungler).

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang, GCC)
**Primary Dependencies**: Steinberg VST3 SDK 3.7.x, VSTGUI 4.12+
**Storage**: Binary state stream (IBStreamer) for preset persistence
**Testing**: Catch2 (dsp_tests, ruinae_tests), pluginval strictness 5
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: Total plugin < 5% single core @ 44.1kHz stereo; All 5 sources only processed when actively routed (sourceActive_ pattern already in ModulationEngine)
**Constraints**: Zero allocations on audio thread; all buffers pre-allocated; no DSP changes needed
**Scale/Scope**: 18 new parameters, 5 new param files, 15 engine forwarding methods, 5 uidesc template populations, state version bump 14->15

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor handles param changes + engine wiring; Controller handles registration + display formatting. No cross-inclusion. |
| II. Real-Time Audio Thread Safety | PASS | All 5 DSP classes already implemented and RT-safe. All param stores use `std::atomic` with `memory_order_relaxed`. No allocations in new code. |
| III. Modern C++ Standards | PASS | Using `std::atomic`, `constexpr`, `std::clamp`, `static_cast`. No raw new/delete. |
| V. VSTGUI Development | PASS | Controls use UIDescription XML (ArcKnob, ToggleButton, COptionMenu, CTextLabel). All control-tags bound to registered VST parameters. Cross-platform only. |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. All UI via VSTGUI abstractions. `std::pow`, `std::log`, `std::clamp` for mappings. |
| VIII. Testing Discipline | PASS | Tests written before implementation. All existing tests must pass. |
| IX. Layered DSP Architecture | PASS | No new DSP classes created. All 5 sources exist at Layer 2 (processors), instantiated in ModulationEngine (Layer 3). No layer changes. |
| XII. Debugging Discipline | PASS | No framework pivots needed -- following established pattern from Spec 057. |
| XIII. Test-First Development | PASS | Failing tests first for each task group. |
| XIV. Living Architecture Documentation | PASS | Architecture docs updated as final task. |
| XV. Pre-Implementation Research (ODR) | PASS | See Codebase Research section below. |
| XVI. Honest Completion | PASS | Compliance table filled only after code/test verification. |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Result | Action |
|--------------|---------------|--------|
| `EnvFollowerParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/env_follower_params.h` |
| `SampleHoldParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/sample_hold_params.h` |
| `RandomParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/random_params.h` |
| `PitchFollowerParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/pitch_follower_params.h` |
| `TransientParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/transient_params.h` |

**Utility Functions to be created**:

| Planned Function | Search Result | Action |
|------------------|---------------|--------|
| `registerEnvFollowerParams()` | Not found | Create New in `env_follower_params.h` |
| `handleEnvFollowerParamChange()` | Not found | Create New in `env_follower_params.h` |
| `formatEnvFollowerParam()` | Not found | Create New in `env_follower_params.h` |
| `saveEnvFollowerParams()` | Not found | Create New in `env_follower_params.h` |
| `loadEnvFollowerParams()` | Not found | Create New in `env_follower_params.h` |
| `loadEnvFollowerParamsToController()` | Not found | Create New in `env_follower_params.h` |
| `registerSampleHoldParams()` | Not found | Create New in `sample_hold_params.h` |
| `handleSampleHoldParamChange()` | Not found | Create New in `sample_hold_params.h` |
| `formatSampleHoldParam()` | Not found | Create New in `sample_hold_params.h` |
| `saveSampleHoldParams()` | Not found | Create New in `sample_hold_params.h` |
| `loadSampleHoldParams()` | Not found | Create New in `sample_hold_params.h` |
| `loadSampleHoldParamsToController()` | Not found | Create New in `sample_hold_params.h` |
| `registerRandomParams()` | Not found | Create New in `random_params.h` |
| `handleRandomParamChange()` | Not found | Create New in `random_params.h` |
| `formatRandomParam()` | Not found | Create New in `random_params.h` |
| `saveRandomParams()` | Not found | Create New in `random_params.h` |
| `loadRandomParams()` | Not found | Create New in `random_params.h` |
| `loadRandomParamsToController()` | Not found | Create New in `random_params.h` |
| `registerPitchFollowerParams()` | Not found | Create New in `pitch_follower_params.h` |
| `handlePitchFollowerParamChange()` | Not found | Create New in `pitch_follower_params.h` |
| `formatPitchFollowerParam()` | Not found | Create New in `pitch_follower_params.h` |
| `savePitchFollowerParams()` | Not found | Create New in `pitch_follower_params.h` |
| `loadPitchFollowerParams()` | Not found | Create New in `pitch_follower_params.h` |
| `loadPitchFollowerParamsToController()` | Not found | Create New in `pitch_follower_params.h` |
| `registerTransientParams()` | Not found | Create New in `transient_params.h` |
| `handleTransientParamChange()` | Not found | Create New in `transient_params.h` |
| `formatTransientParam()` | Not found | Create New in `transient_params.h` |
| `saveTransientParams()` | Not found | Create New in `transient_params.h` |
| `loadTransientParams()` | Not found | Create New in `transient_params.h` |
| `loadTransientParamsToController()` | Not found | Create New in `transient_params.h` |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `ModulationEngine::setEnvFollowerAttack()` | `modulation_engine.h:431` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setEnvFollowerRelease()` | `modulation_engine.h:432` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setEnvFollowerSensitivity()` | `modulation_engine.h:433` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setSampleHoldRate()` | `modulation_engine.h:494` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setSampleHoldSlew()` | `modulation_engine.h:495` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setRandomRate()` | `modulation_engine.h:472` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setRandomSmoothness()` | `modulation_engine.h:473` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setRandomTempoSync()` | `modulation_engine.h:474` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setRandomTempo()` | `modulation_engine.h:475` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setPitchFollowerMinHz()` | `modulation_engine.h:502` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setPitchFollowerMaxHz()` | `modulation_engine.h:503` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setPitchFollowerConfidence()` | `modulation_engine.h:504` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setPitchFollowerTrackingSpeed()` | `modulation_engine.h:505` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setTransientSensitivity()` | `modulation_engine.h:511` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setTransientAttack()` | `modulation_engine.h:512` | 3 | Called via RuinaeEngine forwarding |
| `ModulationEngine::setTransientDecay()` | `modulation_engine.h:513` | 3 | Called via RuinaeEngine forwarding |
| `lfoRateFromNormalized()` / `lfoRateToNormalized()` | `lfo1_params.h:33-39` | Plugin | Reuse for S&H Rate and Random Rate parameter mapping (0.01-50 Hz log scale) |
| `createNoteValueDropdown()` | `controller/parameter_helpers.h:76` | Plugin | Reuse for S&H NoteValue and Random NoteValue dropdowns |
| `kNoteValueDropdownStrings` / `kNoteValueDropdownCount` | `note_value_ui.h:27-49` | Plugin | Reuse for S&H and Random NoteValue dropdown entries |
| `getNoteValueFromDropdown()` | `dsp/core/note_value.h:163` | 0 | Convert dropdown index to NoteValue+NoteModifier for Chaos/S&H/Random tempo sync rate calculation |
| `dropdownToDelayMs()` | `dsp/core/note_value.h:240` | 0 | Convert dropdown index + BPM to delay ms (then to Hz for S&H rate) |
| `RuinaeEngine::setMacroValue()` pattern | `ruinae_engine.h:416-418` | Plugin | Pattern reference for new forwarding methods |
| `RuinaeEngine::setRunglerOsc1Freq()` pattern | `ruinae_engine.h:421-426` | Plugin | Pattern reference for new forwarding methods |
| `MacroParams` pattern | `macro_params.h` | Plugin | Template for new param file structure |
| `RunglerParams` pattern | `rungler_params.h` | Plugin | Template for new param file structure (with log/discrete mappings) |
| `ChaosModParams` Sync/NoteValue pattern | `chaos_mod_params.h:17-23` | Plugin | Reference for S&H and Random Sync + NoteValue handling |
| `ModSource_Chaos` uidesc template | `editor.uidesc:1698-1749` | UI | Reference for Rate/NoteValue visibility switching pattern |
| `ModSource_Macros` uidesc template | `editor.uidesc:1751-1780` | UI | Reference for simple knob row layout |
| Empty mod source templates | `editor.uidesc:1820-1824` | UI | 5 empty CViewContainers to be populated |
| Control-tags section | `editor.uidesc:65-217` | UI | Add 18 new control-tags here |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class (no changes)
- [x] `dsp/include/krate/dsp/processors/sample_hold_source.h` - SampleHoldSource class (no changes)
- [x] `dsp/include/krate/dsp/processors/random_source.h` - RandomSource class (no changes)
- [x] `dsp/include/krate/dsp/processors/pitch_follower_source.h` - PitchFollowerSource class (no changes)
- [x] `dsp/include/krate/dsp/processors/transient_detector.h` - TransientDetector class (no changes)
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - All setter methods exist, no changes needed
- [x] `plugins/ruinae/src/plugin_ids.h` - Current kNumParameters = 2300, no conflicts in 2300-2799 range
- [x] `plugins/ruinae/src/parameters/` - 22 existing param files, none conflict
- [x] `plugins/ruinae/src/processor/processor.h` - kCurrentStateVersion = 14, fields up to settingsParams_
- [x] `plugins/ruinae/src/processor/processor.cpp` - processParameterChanges, applyParamsToEngine, getState, setState
- [x] `plugins/ruinae/src/controller/controller.cpp` - initialize, setComponentState, getParamStringByValue
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - Has Macros+Rungler forwarding, needs 5 new source groups
- [x] `plugins/ruinae/resources/editor.uidesc` - 5 empty templates at lines 1820-1824

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new DSP classes created. Five new parameter structs (`EnvFollowerParams`, `SampleHoldParams`, `RandomParams`, `PitchFollowerParams`, `TransientParams`) are unique names not found anywhere in the codebase. All new functions follow the established naming pattern with unique prefixes. No namespace conflicts.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `ModulationEngine` | `setEnvFollowerAttack` | `void setEnvFollowerAttack(float ms) noexcept` (line 431) | Yes |
| `ModulationEngine` | `setEnvFollowerRelease` | `void setEnvFollowerRelease(float ms) noexcept` (line 432) | Yes |
| `ModulationEngine` | `setEnvFollowerSensitivity` | `void setEnvFollowerSensitivity(float normalized) noexcept` (line 433) | Yes |
| `ModulationEngine` | `setSampleHoldRate` | `void setSampleHoldRate(float hz) noexcept` (line 494) | Yes |
| `ModulationEngine` | `setSampleHoldSlew` | `void setSampleHoldSlew(float ms) noexcept` (line 495) | Yes |
| `ModulationEngine` | `setRandomRate` | `void setRandomRate(float hz) noexcept` (line 472) | Yes |
| `ModulationEngine` | `setRandomSmoothness` | `void setRandomSmoothness(float normalized) noexcept` (line 473) | Yes |
| `ModulationEngine` | `setRandomTempoSync` | `void setRandomTempoSync(bool enabled) noexcept` (line 474) | Yes |
| `ModulationEngine` | `setRandomTempo` | `void setRandomTempo(float bpm) noexcept` (line 475) | Yes |
| `ModulationEngine` | `setPitchFollowerMinHz` | `void setPitchFollowerMinHz(float hz) noexcept` (line 502) | Yes |
| `ModulationEngine` | `setPitchFollowerMaxHz` | `void setPitchFollowerMaxHz(float hz) noexcept` (line 503) | Yes |
| `ModulationEngine` | `setPitchFollowerConfidence` | `void setPitchFollowerConfidence(float threshold) noexcept` (line 504) | Yes |
| `ModulationEngine` | `setPitchFollowerTrackingSpeed` | `void setPitchFollowerTrackingSpeed(float ms) noexcept` (line 505) | Yes |
| `ModulationEngine` | `setTransientSensitivity` | `void setTransientSensitivity(float sensitivity) noexcept` (line 511) | Yes |
| `ModulationEngine` | `setTransientAttack` | `void setTransientAttack(float ms) noexcept` (line 512) | Yes |
| `ModulationEngine` | `setTransientDecay` | `void setTransientDecay(float ms) noexcept` (line 513) | Yes |
| `lfoRateFromNormalized` | function | `inline float lfoRateFromNormalized(double value)` (lfo1_params.h:33) | Yes |
| `lfoRateToNormalized` | function | `inline double lfoRateToNormalized(float hz)` (lfo1_params.h:38) | Yes |
| `dropdownToDelayMs` | function | `[[nodiscard]] inline constexpr float dropdownToDelayMs(int dropdownIndex, float tempoBPM)` (note_value.h:240) | Yes |
| `getNoteValueFromDropdown` | function | `[[nodiscard]] inline constexpr NoteValueMapping getNoteValueFromDropdown(int dropdownIndex)` (note_value.h:163) | Yes |
| `kCurrentStateVersion` | constant | `constexpr Steinberg::int32 kCurrentStateVersion = 14;` (processor.h:71) | Yes |
| `RuinaeEngine::globalModEngine_` | field | `ModulationEngine globalModEngine_;` (ruinae_engine.h:1633) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - All setter methods for 5 sources (lines 431-513)
- [x] `dsp/include/krate/dsp/processors/sample_hold_source.h` - SampleHoldSource: setRate, setSlewTime, NO setTempoSync/setTempo
- [x] `dsp/include/krate/dsp/processors/random_source.h` - RandomSource: setRate, setSmoothness, setTempoSync, setTempo
- [x] `dsp/include/krate/dsp/core/note_value.h` - getNoteValueFromDropdown, dropdownToDelayMs, noteToDelayMs
- [x] `plugins/ruinae/src/plugin_ids.h` - Full ParameterIDs enum (lines 56-658)
- [x] `plugins/ruinae/src/processor/processor.h` - Processor class with all fields (lines 76-228)
- [x] `plugins/ruinae/src/processor/processor.cpp` - processParameterChanges, applyParamsToEngine, getState, setState
- [x] `plugins/ruinae/src/controller/controller.cpp` - initialize, setComponentState, getParamStringByValue
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - RuinaeEngine class with all methods (lines 92-1675)
- [x] `plugins/ruinae/src/parameters/macro_params.h` - MacroParams reference pattern
- [x] `plugins/ruinae/src/parameters/rungler_params.h` - RunglerParams reference pattern (with log mapping)
- [x] `plugins/ruinae/src/parameters/chaos_mod_params.h` - ChaosModParams with Sync/NoteValue pattern
- [x] `plugins/ruinae/src/parameters/lfo1_params.h` - LFO1Params with lfoRateFromNormalized/lfoRateToNormalized
- [x] `plugins/ruinae/src/parameters/note_value_ui.h` - Note value dropdown strings
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createNoteValueDropdown helper
- [x] `plugins/ruinae/resources/editor.uidesc` - Chaos template (Rate/NoteValue switching), empty templates

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `SampleHoldSource` | Has NO `setTempoSync()` or `setTempo()` methods | Must convert NoteValue+tempo to Hz at plugin level and call `setSampleHoldRate(hz)` |
| `RandomSource` | HAS `setTempoSync()` and `setTempo()` | Use directly, but NoteValue must still be converted at plugin level |
| `ModulationEngine` | No `setSampleHoldTempoSync()` or `setSampleHoldNoteValue()` exists | S&H sync must be handled entirely at processor level: `dropdownToDelayMs()` -> convert to Hz -> `setSampleHoldRate()` |
| `ModulationEngine` | No `setRandomNoteValue()` exists | Random NoteValue must be handled at processor level, but Random already handles tempo sync internally via `setRandomTempoSync(bool)` + `setRandomTempo(bpm)` |
| `EnvFollowerSensitivity` | Parameter is `envFollowerSensitivity_` in ModulationEngine, separate from the EnvelopeFollower class | `setEnvFollowerSensitivity(float)` stores it internally, applied during `getRawSourceValue()` |
| State version | Currently 14 | Bump to 15; new data appended AFTER v14 settings params |
| `lfoRateFromNormalized` | Maps [0,1] to [0.01, 50] Hz | Same range spec requires for S&H Rate (0.1-50 Hz) and Random Rate (0.1-50 Hz). Note: LFO min is 0.01 Hz but spec says 0.1 Hz -- use lfoRateFromNormalized anyway and clamp in the handle function to [0.1, 50] |
| `dropdownToDelayMs` | Returns milliseconds, S&H needs Hz | Convert: `rateHz = 1000.0f / dropdownToDelayMs(noteIdx, bpm)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Log frequency mappings for Pitch Follower | Simple inline formulas in param file, standard pattern |
| Sensitivity/Percentage formatting | One-liner `"%.0f%%"` pattern, repeated but trivial |

**Decision**: No Layer 0 extractions needed. All new code is plugin-level parameter plumbing that follows established patterns. All DSP classes already exist.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | No new DSP algorithms -- only parameter wiring |
| **Data parallelism width** | N/A | This spec adds no DSP processing code |
| **Branch density in inner loop** | N/A | No inner loops added |
| **Dominant operations** | N/A | Only atomic load/store for parameter forwarding |
| **Current CPU budget vs expected usage** | N/A | All 5 DSP processors already integrated; this spec only connects parameters |

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec does not add any DSP algorithms. It only adds plugin-level parameter wiring (param structs, handle/register/format/save/load functions, engine forwarding methods, and uidesc controls). The DSP classes are fully implemented and already processing in the ModulationEngine. No SIMD analysis is needed.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin parameter layer (param files, processor wiring, uidesc controls)

**Related features at same layer**:
- Spec 057 (Macros & Rungler) -- already completed, established the pattern
- Future: Env Follower Source Type dropdown
- Future: S&H Input Type dropdown

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| S&H sync rate calculation pattern | MEDIUM | Future S&H enhancements | Keep in sample_hold_params.h |
| Env Follower param file pattern | LOW | Only Env Follower | Keep local |
| Random sync pattern | LOW | Only Random source | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for param files | Each param file is self-contained with inline functions; follows project convention |
| S&H sync handled at processor level | SampleHoldSource DSP class has no sync support; convert NoteValue+BPM to Hz in applyParamsToEngine |
| Random uses built-in sync + processor NoteValue | RandomSource has setTempoSync/setTempo but no NoteValue concept |

## Project Structure

### Documentation (this feature)

```text
specs/059-additional-mod-sources/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- parameter-ids.md # Parameter ID definitions and mappings
+-- tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (files to create/modify)

```text
# NEW FILES (5 parameter header files)
plugins/ruinae/src/parameters/env_follower_params.h       # EnvFollowerParams + register/handle/format/save/load
plugins/ruinae/src/parameters/sample_hold_params.h        # SampleHoldParams + register/handle/format/save/load
plugins/ruinae/src/parameters/random_params.h             # RandomParams + register/handle/format/save/load
plugins/ruinae/src/parameters/pitch_follower_params.h     # PitchFollowerParams + register/handle/format/save/load
plugins/ruinae/src/parameters/transient_params.h          # TransientParams + register/handle/format/save/load

# MODIFIED FILES - Plugin Layer
plugins/ruinae/src/plugin_ids.h                            # Add 5 param ranges (2300-2799), kNumParameters 2300->2800
plugins/ruinae/src/engine/ruinae_engine.h                  # Add 15 forwarding methods for 5 sources
plugins/ruinae/src/processor/processor.h                   # Add 5 param fields, bump kCurrentStateVersion 14->15
plugins/ruinae/src/processor/processor.cpp                 # processParameterChanges, applyParamsToEngine, getState, setState
plugins/ruinae/src/controller/controller.cpp               # initialize (register), setComponentState, getParamStringByValue

# MODIFIED FILES - UI
plugins/ruinae/resources/editor.uidesc                     # 18 control-tags + 5 template populations

# NO DSP CHANGES - All 5 DSP classes and ModulationEngine are unmodified
```

**Structure Decision**: Monorepo structure -- all changes in `plugins/ruinae/` (source, parameters, engine, resources). No DSP layer changes. No shared UI changes.

## Detailed Implementation Design

### Task Group 1: Parameter IDs (FR-001 through FR-006)

**File modified**: `plugins/ruinae/src/plugin_ids.h`

Insert after `kSettingsEndId = 2299` and before `kNumParameters`:

```cpp
// ==========================================================================
// Env Follower Parameters (2300-2399)
// ==========================================================================
kEnvFollowerBaseId = 2300,
kEnvFollowerSensitivityId = 2300, // Sensitivity [0, 1] (default 0.5)
kEnvFollowerAttackId = 2301,      // Attack time [0.1, 500] ms (default 10 ms)
kEnvFollowerReleaseId = 2302,     // Release time [1, 5000] ms (default 100 ms)
kEnvFollowerEndId = 2399,

// ==========================================================================
// Sample & Hold Parameters (2400-2499)
// ==========================================================================
kSampleHoldBaseId = 2400,
kSampleHoldRateId = 2400,         // Rate [0.1, 50] Hz (default 4 Hz)
kSampleHoldSyncId = 2401,         // Tempo sync on/off (default off)
kSampleHoldNoteValueId = 2402,    // Note value dropdown (default 1/8)
kSampleHoldSlewId = 2403,         // Slew time [0, 500] ms (default 0 ms)
kSampleHoldEndId = 2499,

// ==========================================================================
// Random Parameters (2500-2599)
// ==========================================================================
kRandomBaseId = 2500,
kRandomRateId = 2500,             // Rate [0.1, 50] Hz (default 4 Hz)
kRandomSyncId = 2501,             // Tempo sync on/off (default off)
kRandomNoteValueId = 2502,        // Note value dropdown (default 1/8)
kRandomSmoothnessId = 2503,       // Smoothness [0, 1] (default 0)
kRandomEndId = 2599,

// ==========================================================================
// Pitch Follower Parameters (2600-2699)
// ==========================================================================
kPitchFollowerBaseId = 2600,
kPitchFollowerMinHzId = 2600,     // Min frequency [20, 500] Hz (default 80 Hz)
kPitchFollowerMaxHzId = 2601,     // Max frequency [200, 5000] Hz (default 2000 Hz)
kPitchFollowerConfidenceId = 2602, // Confidence [0, 1] (default 0.5)
kPitchFollowerSpeedId = 2603,     // Tracking speed [10, 300] ms (default 50 ms)
kPitchFollowerEndId = 2699,

// ==========================================================================
// Transient Detector Parameters (2700-2799)
// ==========================================================================
kTransientBaseId = 2700,
kTransientSensitivityId = 2700,   // Sensitivity [0, 1] (default 0.5)
kTransientAttackId = 2701,        // Attack time [0.5, 10] ms (default 2 ms)
kTransientDecayId = 2702,         // Decay time [20, 200] ms (default 50 ms)
kTransientEndId = 2799,

// ==========================================================================
kNumParameters = 2800,
```

Also update the ID range allocation comment block at the top of the enum to include:
```
//   2300-2399: Env Follower
//   2400-2499: Sample & Hold
//   2500-2599: Random
//   2600-2699: Pitch Follower
//   2700-2799: Transient Detector
```

### Task Group 2: Parameter Files (FR-007 through FR-011)

#### 2a. Create `env_follower_params.h`

New file at `plugins/ruinae/src/parameters/env_follower_params.h` following `rungler_params.h` pattern:

- **`EnvFollowerParams` struct**:
  - `std::atomic<float> sensitivity{0.5f}` -- [0, 1]
  - `std::atomic<float> attackMs{10.0f}` -- [0.1, 500] ms
  - `std::atomic<float> releaseMs{100.0f}` -- [1, 5000] ms

- **Logarithmic attack mapping**: `ms = 0.1 * pow(5000.0, normalized)` maps [0,1] to [0.1, 500] ms. Default 10 ms: `norm = log(10/0.1) / log(5000) = log(100) / log(5000) = 2.0/3.699 = 0.5406`

- **Logarithmic release mapping**: `ms = 1.0 * pow(5000.0, normalized)` maps [0,1] to [1, 5000] ms. Default 100 ms: `norm = log(100/1) / log(5000) = 2.0/3.699 = 0.5406`

- **`handleEnvFollowerParamChange()`**: Sensitivity = direct [0,1], Attack/Release = log denormalization
- **`registerEnvFollowerParams()`**: 3 params with `kCanAutomate`
- **`formatEnvFollowerParam()`**: Sensitivity: "XX%", Attack: "X.X ms" / "XXX ms", Release: "X ms" / "XXXX ms"
- **`saveEnvFollowerParams()`**: Write 3 floats (sensitivity, attackMs, releaseMs)
- **`loadEnvFollowerParams()`**: Read 3 floats, return false on EOF
- **`loadEnvFollowerParamsToController()`**: Read and convert back to normalized

#### 2b. Create `sample_hold_params.h`

New file at `plugins/ruinae/src/parameters/sample_hold_params.h` following `chaos_mod_params.h` pattern:

- **`SampleHoldParams` struct**:
  - `std::atomic<float> rateHz{4.0f}` -- [0.1, 50] Hz (uses lfoRateFromNormalized)
  - `std::atomic<bool> sync{false}` -- tempo sync on/off
  - `std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex}` -- default 1/8
  - `std::atomic<float> slewMs{0.0f}` -- [0, 500] ms linear

- **`handleSampleHoldParamChange()`**: Rate uses `lfoRateFromNormalized()`, Sync is bool, NoteValue is dropdown index, Slew is linear 0-500 ms
- **`registerSampleHoldParams()`**: Rate continuous, Sync boolean stepCount=1, NoteValue via `createNoteValueDropdown()`, Slew continuous
- **`formatSampleHoldParam()`**: Rate: "X.XX Hz", Slew: "X ms"
- **Save/Load**: Write rateHz (float), sync (int32), noteValue (int32), slewMs (float)

**Slew linear mapping**: `ms = normalized * 500.0f`. Default 0 ms = normalized 0.0.

#### 2c. Create `random_params.h`

New file at `plugins/ruinae/src/parameters/random_params.h` following `chaos_mod_params.h` pattern:

- **`RandomParams` struct**:
  - `std::atomic<float> rateHz{4.0f}` -- [0.1, 50] Hz (uses lfoRateFromNormalized)
  - `std::atomic<bool> sync{false}` -- tempo sync on/off
  - `std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex}` -- default 1/8
  - `std::atomic<float> smoothness{0.0f}` -- [0, 1]

- **`handleRandomParamChange()`**: Rate uses `lfoRateFromNormalized()`, Sync is bool, NoteValue is dropdown index, Smoothness is direct [0,1]
- **`registerRandomParams()`**: Rate continuous, Sync boolean stepCount=1, NoteValue via `createNoteValueDropdown()`, Smoothness continuous
- **`formatRandomParam()`**: Rate: "X.XX Hz", Smoothness: "XX%"
- **Save/Load**: Write rateHz (float), sync (int32), noteValue (int32), smoothness (float)

#### 2d. Create `pitch_follower_params.h`

New file at `plugins/ruinae/src/parameters/pitch_follower_params.h`:

- **`PitchFollowerParams` struct**:
  - `std::atomic<float> minHz{80.0f}` -- [20, 500] Hz log
  - `std::atomic<float> maxHz{2000.0f}` -- [200, 5000] Hz log
  - `std::atomic<float> confidence{0.5f}` -- [0, 1]
  - `std::atomic<float> speedMs{50.0f}` -- [10, 300] ms linear

- **Logarithmic min Hz mapping**: `hz = 20 * pow(25.0, normalized)` maps [0,1] to [20, 500] Hz. Default 80 Hz: `norm = log(80/20) / log(25) = log(4) / log(25) = 0.602/1.398 = 0.4307`

- **Logarithmic max Hz mapping**: `hz = 200 * pow(25.0, normalized)` maps [0,1] to [200, 5000] Hz. Default 2000 Hz: `norm = log(2000/200) / log(25) = log(10) / log(25) = 1.0/1.398 = 0.7153`

- **Linear speed mapping**: `ms = 10 + normalized * 290`. Default 50 ms: `norm = (50 - 10) / 290 = 0.1379`

- **`handlePitchFollowerParamChange()`**: MinHz/MaxHz = log denorm, Confidence = direct [0,1], Speed = linear 10-300 ms
- **`registerPitchFollowerParams()`**: 4 params with `kCanAutomate`
- **`formatPitchFollowerParam()`**: MinHz/MaxHz: "XXX Hz" or "XXXX Hz", Confidence: "XX%", Speed: "XX ms"
- **Save/Load**: Write minHz, maxHz, confidence, speedMs (4 floats)

#### 2e. Create `transient_params.h`

New file at `plugins/ruinae/src/parameters/transient_params.h`:

- **`TransientParams` struct**:
  - `std::atomic<float> sensitivity{0.5f}` -- [0, 1]
  - `std::atomic<float> attackMs{2.0f}` -- [0.5, 10] ms linear
  - `std::atomic<float> decayMs{50.0f}` -- [20, 200] ms linear

- **Linear attack mapping**: `ms = 0.5 + normalized * 9.5`. Default 2 ms: `norm = (2 - 0.5) / 9.5 = 0.1579`

- **Linear decay mapping**: `ms = 20 + normalized * 180`. Default 50 ms: `norm = (50 - 20) / 180 = 0.1667`

- **`handleTransientParamChange()`**: Sensitivity = direct [0,1], Attack = linear 0.5-10 ms, Decay = linear 20-200 ms
- **`registerTransientParams()`**: 3 params with `kCanAutomate`
- **`formatTransientParam()`**: Sensitivity: "XX%", Attack: "X.X ms", Decay: "XXX ms"
- **Save/Load**: Write sensitivity, attackMs, decayMs (3 floats)

### Task Group 3: RuinaeEngine Forwarding Methods (FR-012)

**File modified**: `plugins/ruinae/src/engine/ruinae_engine.h`

Add after the Rungler forwarding methods (line ~426):

```cpp
// Env Follower source setters
void setEnvFollowerSensitivity(float v) noexcept { globalModEngine_.setEnvFollowerSensitivity(v); }
void setEnvFollowerAttack(float ms) noexcept { globalModEngine_.setEnvFollowerAttack(ms); }
void setEnvFollowerRelease(float ms) noexcept { globalModEngine_.setEnvFollowerRelease(ms); }

// Sample & Hold source setters
void setSampleHoldRate(float hz) noexcept { globalModEngine_.setSampleHoldRate(hz); }
void setSampleHoldSlew(float ms) noexcept { globalModEngine_.setSampleHoldSlew(ms); }

// Random source setters
void setRandomRate(float hz) noexcept { globalModEngine_.setRandomRate(hz); }
void setRandomSmoothness(float v) noexcept { globalModEngine_.setRandomSmoothness(v); }
void setRandomTempoSync(bool enabled) noexcept { globalModEngine_.setRandomTempoSync(enabled); }
void setRandomTempo(float bpm) noexcept { globalModEngine_.setRandomTempo(bpm); }

// Pitch Follower source setters
void setPitchFollowerMinHz(float hz) noexcept { globalModEngine_.setPitchFollowerMinHz(hz); }
void setPitchFollowerMaxHz(float hz) noexcept { globalModEngine_.setPitchFollowerMaxHz(hz); }
void setPitchFollowerConfidence(float v) noexcept { globalModEngine_.setPitchFollowerConfidence(v); }
void setPitchFollowerTrackingSpeed(float ms) noexcept { globalModEngine_.setPitchFollowerTrackingSpeed(ms); }

// Transient Detector source setters
void setTransientSensitivity(float v) noexcept { globalModEngine_.setTransientSensitivity(v); }
void setTransientAttack(float ms) noexcept { globalModEngine_.setTransientAttack(ms); }
void setTransientDecay(float ms) noexcept { globalModEngine_.setTransientDecay(ms); }
```

**Total**: 18 forwarding methods (3 + 2 + 4 + 4 + 3 = 16 unique engine calls, but S&H has no sync forwarding and Random has tempo forwarding).

**Note on S&H Sync**: Unlike other sources, S&H sync is handled entirely at the processor level. The `applyParamsToEngine()` method converts NoteValue + BPM to Hz when Sync is on and calls `setSampleHoldRate(hz)`. There is no `setSampleHoldTempoSync()` forwarding needed.

### Task Group 4: Processor Wiring (FR-013 through FR-017)

**Files modified**: `processor.h`, `processor.cpp`

#### 4a. Add param fields to `Processor` (processor.h)

Add after `settingsParams_` (line 175):

```cpp
EnvFollowerParams envFollowerParams_;
SampleHoldParams sampleHoldParams_;
RandomParams randomParams_;
PitchFollowerParams pitchFollowerParams_;
TransientParams transientParams_;
```

Add includes for all 5 new param files.

Bump `kCurrentStateVersion` from 14 to 15.

#### 4b. Extend `processParameterChanges()` (processor.cpp)

Add after the settings params block (line ~671):

```cpp
} else if (paramId >= kEnvFollowerBaseId && paramId <= kEnvFollowerEndId) {
    handleEnvFollowerParamChange(envFollowerParams_, paramId, value);
} else if (paramId >= kSampleHoldBaseId && paramId <= kSampleHoldEndId) {
    handleSampleHoldParamChange(sampleHoldParams_, paramId, value);
} else if (paramId >= kRandomBaseId && paramId <= kRandomEndId) {
    handleRandomParamChange(randomParams_, paramId, value);
} else if (paramId >= kPitchFollowerBaseId && paramId <= kPitchFollowerEndId) {
    handlePitchFollowerParamChange(pitchFollowerParams_, paramId, value);
} else if (paramId >= kTransientBaseId && paramId <= kTransientEndId) {
    handleTransientParamChange(transientParams_, paramId, value);
}
```

#### 4c. Extend `applyParamsToEngine()` (processor.cpp)

Add after the Settings section (line ~1024):

```cpp
// --- Env Follower ---
engine_.setEnvFollowerSensitivity(envFollowerParams_.sensitivity.load(std::memory_order_relaxed));
engine_.setEnvFollowerAttack(envFollowerParams_.attackMs.load(std::memory_order_relaxed));
engine_.setEnvFollowerRelease(envFollowerParams_.releaseMs.load(std::memory_order_relaxed));

// --- Sample & Hold ---
if (sampleHoldParams_.sync.load(std::memory_order_relaxed)) {
    // When synced, convert NoteValue + tempo to rate in Hz
    int noteIdx = sampleHoldParams_.noteValue.load(std::memory_order_relaxed);
    float delayMs = dropdownToDelayMs(noteIdx, tempoBPM_);
    float rateHz = (delayMs > 0.0f) ? (1000.0f / delayMs) : 4.0f;
    engine_.setSampleHoldRate(rateHz);
} else {
    engine_.setSampleHoldRate(sampleHoldParams_.rateHz.load(std::memory_order_relaxed));
}
engine_.setSampleHoldSlew(sampleHoldParams_.slewMs.load(std::memory_order_relaxed));

// --- Random ---
engine_.setRandomRate(randomParams_.rateHz.load(std::memory_order_relaxed));
engine_.setRandomSmoothness(randomParams_.smoothness.load(std::memory_order_relaxed));
engine_.setRandomTempoSync(randomParams_.sync.load(std::memory_order_relaxed));
engine_.setRandomTempo(tempoBPM_);
// Note: When Random sync is on, RandomSource internally uses tempo to compute rate.
// NoteValue is not directly used by RandomSource -- it uses the rate/tempo internally.
// However, we need to convert NoteValue to rate when sync is on, similar to S&H:
if (randomParams_.sync.load(std::memory_order_relaxed)) {
    int noteIdx = randomParams_.noteValue.load(std::memory_order_relaxed);
    float delayMs = dropdownToDelayMs(noteIdx, tempoBPM_);
    float rateHz = (delayMs > 0.0f) ? (1000.0f / delayMs) : 4.0f;
    engine_.setRandomRate(rateHz);
}

// --- Pitch Follower ---
engine_.setPitchFollowerMinHz(pitchFollowerParams_.minHz.load(std::memory_order_relaxed));
engine_.setPitchFollowerMaxHz(pitchFollowerParams_.maxHz.load(std::memory_order_relaxed));
engine_.setPitchFollowerConfidence(pitchFollowerParams_.confidence.load(std::memory_order_relaxed));
engine_.setPitchFollowerTrackingSpeed(pitchFollowerParams_.speedMs.load(std::memory_order_relaxed));

// --- Transient ---
engine_.setTransientSensitivity(transientParams_.sensitivity.load(std::memory_order_relaxed));
engine_.setTransientAttack(transientParams_.attackMs.load(std::memory_order_relaxed));
engine_.setTransientDecay(transientParams_.decayMs.load(std::memory_order_relaxed));
```

**Critical design decision for S&H and Random sync**:

The `SampleHoldSource` DSP class has NO tempo sync support. When the user enables S&H Sync, we handle it entirely at the processor level:
1. Read the NoteValue dropdown index
2. Convert to delay in ms using `dropdownToDelayMs(noteIdx, tempoBPM_)`
3. Convert ms to Hz: `rateHz = 1000.0f / delayMs`
4. Call `engine_.setSampleHoldRate(rateHz)` with the computed rate
5. When Sync is OFF, use the Rate knob value directly

The `RandomSource` DSP class HAS `setTempoSync(bool)` and `setTempo(bpm)`, but internally it just converts tempo to a rate. Since we want NoteValue-based sync (matching the Chaos/LFO/S&H UX pattern), we:
1. Always pass `setRandomTempoSync(false)` -- we handle sync ourselves
2. When Sync is ON, convert NoteValue + BPM to Hz (same as S&H)
3. Call `setRandomRate(hz)` with the computed rate
4. When Sync is OFF, use the Rate knob value directly

This gives consistent behavior across all 4 syncable sources (LFO, Chaos, S&H, Random).

#### 4d. Extend `getState()` (state version 15)

Append after v14 settings params:

```cpp
// v15: Mod source params (Env Follower, S&H, Random, Pitch Follower, Transient)
saveEnvFollowerParams(envFollowerParams_, streamer);
saveSampleHoldParams(sampleHoldParams_, streamer);
saveRandomParams(randomParams_, streamer);
savePitchFollowerParams(pitchFollowerParams_, streamer);
saveTransientParams(transientParams_, streamer);
```

#### 4e. Extend `setState()` with backward compatibility

After the v14 settings loading block:

```cpp
// v15: Mod source params
if (version >= 15) {
    loadEnvFollowerParams(envFollowerParams_, streamer);
    loadSampleHoldParams(sampleHoldParams_, streamer);
    loadRandomParams(randomParams_, streamer);
    loadPitchFollowerParams(pitchFollowerParams_, streamer);
    loadTransientParams(transientParams_, streamer);
}
// Note: For version < 15, all mod source params keep their default values:
// Env Follower: Sensitivity=0.5, Attack=10ms, Release=100ms
// S&H: Rate=4Hz, Sync=off, NoteValue=1/8, Slew=0ms
// Random: Rate=4Hz, Sync=off, NoteValue=1/8, Smoothness=0
// Pitch Follower: MinHz=80, MaxHz=2000, Confidence=0.5, Speed=50ms
// Transient: Sensitivity=0.5, Attack=2ms, Decay=50ms
```

### Task Group 5: Controller Registration and Display (FR-019)

**File modified**: `plugins/ruinae/src/controller/controller.cpp`

#### 5a. Register params in `Controller::initialize()`

After `registerSettingsParams(parameters);` (line 119):

```cpp
registerEnvFollowerParams(parameters);
registerSampleHoldParams(parameters);
registerRandomParams(parameters);
registerPitchFollowerParams(parameters);
registerTransientParams(parameters);
```

Add includes for all 5 param files.

#### 5b. Extend `setComponentState()`

After v14 settings loading (line ~274):

```cpp
// v15: Mod source params
if (version >= 15) {
    loadEnvFollowerParamsToController(streamer, setParam);
    loadSampleHoldParamsToController(streamer, setParam);
    loadRandomParamsToController(streamer, setParam);
    loadPitchFollowerParamsToController(streamer, setParam);
    loadTransientParamsToController(streamer, setParam);
}
```

#### 5c. Extend `getParamStringByValue()`

After the settings formatting block (line ~386):

```cpp
} else if (id >= kEnvFollowerBaseId && id <= kEnvFollowerEndId) {
    result = formatEnvFollowerParam(id, valueNormalized, string);
} else if (id >= kSampleHoldBaseId && id <= kSampleHoldEndId) {
    result = formatSampleHoldParam(id, valueNormalized, string);
} else if (id >= kRandomBaseId && id <= kRandomEndId) {
    result = formatRandomParam(id, valueNormalized, string);
} else if (id >= kPitchFollowerBaseId && id <= kPitchFollowerEndId) {
    result = formatPitchFollowerParam(id, valueNormalized, string);
} else if (id >= kTransientBaseId && id <= kTransientEndId) {
    result = formatTransientParam(id, valueNormalized, string);
}
```

### Task Group 6: Control-Tags and UI Templates (FR-019 through FR-024)

**File modified**: `plugins/ruinae/resources/editor.uidesc`

#### 6a. Add control-tags (18 tags)

In the control-tags section:

```xml
<control-tag name="EnvFollowerSensitivity" tag="2300"/>
<control-tag name="EnvFollowerAttack" tag="2301"/>
<control-tag name="EnvFollowerRelease" tag="2302"/>
<control-tag name="SampleHoldRate" tag="2400"/>
<control-tag name="SampleHoldSync" tag="2401"/>
<control-tag name="SampleHoldNoteValue" tag="2402"/>
<control-tag name="SampleHoldSlew" tag="2403"/>
<control-tag name="RandomRate" tag="2500"/>
<control-tag name="RandomSync" tag="2501"/>
<control-tag name="RandomNoteValue" tag="2502"/>
<control-tag name="RandomSmoothness" tag="2503"/>
<control-tag name="PitchFollowerMinHz" tag="2600"/>
<control-tag name="PitchFollowerMaxHz" tag="2601"/>
<control-tag name="PitchFollowerConfidence" tag="2602"/>
<control-tag name="PitchFollowerSpeed" tag="2603"/>
<control-tag name="TransientSensitivity" tag="2700"/>
<control-tag name="TransientAttack" tag="2701"/>
<control-tag name="TransientDecay" tag="2702"/>
```

#### 6b. Populate `ModSource_EnvFollower` template (FR-020)

Replace the empty self-closing tag with:

```xml
<template name="ModSource_EnvFollower" size="158, 120" class="CViewContainer" transparent="true">
    <!-- Sensitivity -->
    <view class="ArcKnob" origin="4, 0" size="28, 28"
          control-tag="EnvFollowerSensitivity" default-value="0.5"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="0, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Sens"/>
    <!-- Attack -->
    <view class="ArcKnob" origin="48, 0" size="28, 28"
          control-tag="EnvFollowerAttack" default-value="0.5406"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="44, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Atk"/>
    <!-- Release -->
    <view class="ArcKnob" origin="92, 0" size="28, 28"
          control-tag="EnvFollowerRelease" default-value="0.5406"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="88, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Rel"/>
</template>
```

#### 6c. Populate `ModSource_SampleHold` template (FR-021)

Following the Chaos template Rate/NoteValue visibility switching pattern:

```xml
<template name="ModSource_SampleHold" size="158, 120" class="CViewContainer" transparent="true">
    <!-- Row 1: Rate/NoteValue + Slew -->
    <!-- Rate (hidden when sync active) -->
    <view class="CViewContainer" origin="0, 0" size="36, 38"
          custom-view-name="SHRateGroup" transparent="true">
        <view class="ArcKnob" origin="4, 0" size="28, 28"
              control-tag="SampleHoldRate" default-value="0.702"
              arc-color="modulation" guide-color="knob-guide"/>
        <view class="CTextLabel" origin="0, 28" size="36, 10"
              font="~ NormalFontSmaller" font-color="text-secondary"
              text-alignment="center" transparent="true" title="Rate"/>
    </view>
    <!-- Note Value (visible when sync active) -->
    <view class="CViewContainer" origin="0, 0" size="36, 38"
          custom-view-name="SHNoteValueGroup" transparent="true"
          visible="false">
        <view class="COptionMenu" origin="2, 6" size="32, 16"
              control-tag="SampleHoldNoteValue"
              font="~ NormalFontSmaller" font-color="text-primary"
              back-color="bg-dropdown" frame-color="frame-dropdown"
              round-rect-radius="2" frame-width="1"
              min-value="0" max-value="1"
              default-value="0.5"/>
        <view class="CTextLabel" origin="0, 28" size="36, 10"
              font="~ NormalFontSmaller" font-color="text-secondary"
              text-alignment="center" transparent="true" title="Note"/>
    </view>
    <!-- Slew -->
    <view class="ArcKnob" origin="80, 0" size="28, 28"
          control-tag="SampleHoldSlew" default-value="0.0"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="76, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Slew"/>

    <!-- Row 2: Sync toggle -->
    <view class="ToggleButton" origin="2, 42" size="36, 12"
          control-tag="SampleHoldSync" default-value="0.0"
          title="Sync"
          on-color="modulation" off-color="toggle-off"
          font-size="9"/>
</template>
```

Default Rate value: `lfoRateToNormalized(4.0f) = log(4.0 / 0.01) / log(5000) = log(400) / log(5000) = 2.602/3.699 = 0.7033` (approx 0.702).

#### 6d. Populate `ModSource_Random` template (FR-022)

Same pattern as S&H but with Smoothness instead of Slew:

```xml
<template name="ModSource_Random" size="158, 120" class="CViewContainer" transparent="true">
    <!-- Row 1: Rate/NoteValue + Smoothness -->
    <!-- Rate (hidden when sync active) -->
    <view class="CViewContainer" origin="0, 0" size="36, 38"
          custom-view-name="RandomRateGroup" transparent="true">
        <view class="ArcKnob" origin="4, 0" size="28, 28"
              control-tag="RandomRate" default-value="0.702"
              arc-color="modulation" guide-color="knob-guide"/>
        <view class="CTextLabel" origin="0, 28" size="36, 10"
              font="~ NormalFontSmaller" font-color="text-secondary"
              text-alignment="center" transparent="true" title="Rate"/>
    </view>
    <!-- Note Value (visible when sync active) -->
    <view class="CViewContainer" origin="0, 0" size="36, 38"
          custom-view-name="RandomNoteValueGroup" transparent="true"
          visible="false">
        <view class="COptionMenu" origin="2, 6" size="32, 16"
              control-tag="RandomNoteValue"
              font="~ NormalFontSmaller" font-color="text-primary"
              back-color="bg-dropdown" frame-color="frame-dropdown"
              round-rect-radius="2" frame-width="1"
              min-value="0" max-value="1"
              default-value="0.5"/>
        <view class="CTextLabel" origin="0, 28" size="36, 10"
              font="~ NormalFontSmaller" font-color="text-secondary"
              text-alignment="center" transparent="true" title="Note"/>
    </view>
    <!-- Smoothness -->
    <view class="ArcKnob" origin="80, 0" size="28, 28"
          control-tag="RandomSmoothness" default-value="0.0"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="76, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Smooth"/>

    <!-- Row 2: Sync toggle -->
    <view class="ToggleButton" origin="2, 42" size="36, 12"
          control-tag="RandomSync" default-value="0.0"
          title="Sync"
          on-color="modulation" off-color="toggle-off"
          font-size="9"/>
</template>
```

#### 6e. Populate `ModSource_PitchFollower` template (FR-023)

Four knobs in a single row:

```xml
<template name="ModSource_PitchFollower" size="158, 120" class="CViewContainer" transparent="true">
    <!-- Min Hz -->
    <view class="ArcKnob" origin="4, 0" size="28, 28"
          control-tag="PitchFollowerMinHz" default-value="0.4307"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="0, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Min"/>
    <!-- Max Hz -->
    <view class="ArcKnob" origin="42, 0" size="28, 28"
          control-tag="PitchFollowerMaxHz" default-value="0.7153"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="38, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Max"/>
    <!-- Confidence -->
    <view class="ArcKnob" origin="80, 0" size="28, 28"
          control-tag="PitchFollowerConfidence" default-value="0.5"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="76, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Conf"/>
    <!-- Tracking Speed -->
    <view class="ArcKnob" origin="118, 0" size="28, 28"
          control-tag="PitchFollowerSpeed" default-value="0.1379"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="114, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Speed"/>
</template>
```

#### 6f. Populate `ModSource_Transient` template (FR-024)

Three knobs in a single row:

```xml
<template name="ModSource_Transient" size="158, 120" class="CViewContainer" transparent="true">
    <!-- Sensitivity -->
    <view class="ArcKnob" origin="4, 0" size="28, 28"
          control-tag="TransientSensitivity" default-value="0.5"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="0, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Sens"/>
    <!-- Attack -->
    <view class="ArcKnob" origin="48, 0" size="28, 28"
          control-tag="TransientAttack" default-value="0.1579"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="44, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Atk"/>
    <!-- Decay -->
    <view class="ArcKnob" origin="92, 0" size="28, 28"
          control-tag="TransientDecay" default-value="0.1667"
          arc-color="modulation" guide-color="knob-guide"/>
    <view class="CTextLabel" origin="88, 28" size="36, 10"
          font="~ NormalFontSmaller" font-color="text-secondary"
          text-alignment="center" transparent="true" title="Decay"/>
</template>
```

### Task Group 7: S&H and Random Sync Visibility Switching

**File modified**: `plugins/ruinae/src/controller/controller.cpp` (or sub-controller)

The S&H and Random templates use `custom-view-name` groups (`SHRateGroup`/`SHNoteValueGroup` and `RandomRateGroup`/`RandomNoteValueGroup`) that need visibility switching when the Sync toggle changes -- same pattern as `ChaosRateGroup`/`ChaosNoteValueGroup`.

This requires the sub-controller/delegate to handle `SampleHoldSync` and `RandomSync` parameter changes by toggling visibility of the respective groups:
- When Sync = OFF: RateGroup visible, NoteValueGroup hidden
- When Sync = ON: RateGroup hidden, NoteValueGroup visible

The Chaos visibility switching mechanism already exists in the controller. The new groups follow the exact same `custom-view-name` based visibility pattern. The implementation should extend the existing switch/case (or if/else chain) that handles `ChaosSync` -> `ChaosRateGroup`/`ChaosNoteValueGroup` to also handle:
- `SampleHoldSync` -> `SHRateGroup`/`SHNoteValueGroup`
- `RandomSync` -> `RandomRateGroup`/`RandomNoteValueGroup`

### Task Group 8: Architecture Documentation Update

Update `specs/_architecture_/` to document:
- Env Follower, S&H, Random, Pitch Follower, and Transient parameter exposure
- State version 15 format (appended after v14)
- S&H sync handling pattern (plugin-level NoteValue to Hz conversion)

## Complexity Tracking

No constitution violations to justify. All design decisions follow established patterns exactly.

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor owns all param structs. Controller registers/formats. No cross-dependency. |
| II. Real-Time Thread Safety | PASS | All param stores use `std::atomic` with `memory_order_relaxed`. No allocations in any new code. S&H sync conversion (dropdownToDelayMs) is constexpr/inline. |
| III. Modern C++ | PASS | `std::atomic`, `constexpr`, `std::clamp`, `static_cast`. No raw new/delete. |
| V. VSTGUI | PASS | All controls via UIDescription XML. ArcKnob, ToggleButton, COptionMenu, CTextLabel -- all cross-platform. |
| VI. Cross-Platform | PASS | No platform-specific code. `std::pow`, `std::log`, `std::clamp` used for mappings. |
| VIII. Testing | PASS | Tests for param handling, state persistence, engine integration. All existing tests must pass. |
| IX. Layered DSP | PASS | No DSP layer changes. All 5 sources exist at Layer 2, instantiated in ModulationEngine (Layer 3). |
| XIII. Test-First | PASS | Tests designed before implementation for each task group. |
| XV. ODR Prevention | PASS | No duplicate types. All new names verified unique. |
| XVI. Honest Completion | PASS | Compliance table will be filled from actual code/test verification only. |
