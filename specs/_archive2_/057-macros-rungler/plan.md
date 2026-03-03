# Implementation Plan: Macros & Rungler UI Exposure

**Branch**: `057-macros-rungler` | **Date**: 2026-02-15 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/057-macros-rungler/spec.md`

## Summary

Expose Macro 1-4 knobs and Rungler configuration as VST parameters, integrating the Rungler DSP class into the ModulationEngine, and populating the existing empty mod source dropdown views with functional UI controls. This involves 10 new automatable parameters (IDs 2000-2003 for macros, 2100-2105 for rungler), a ModSource enum renumbering to insert Rungler at position 10, preset migration for the enum change, two new parameter files (`macro_params.h`, `rungler_params.h`), engine integration, and uidesc template population. State version bumps from 12 to 13.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang, GCC)
**Primary Dependencies**: Steinberg VST3 SDK 3.7.x, VSTGUI 4.12+
**Storage**: Binary state stream (IBStreamer) for preset persistence
**Testing**: Catch2 (dsp_tests, ruinae_tests), pluginval strictness 5
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: Total plugin < 5% single core @ 44.1kHz stereo; Rungler only processed when actively routed (sourceActive_ pattern)
**Constraints**: Zero allocations on audio thread; all buffers pre-allocated
**Scale/Scope**: 10 new parameters, 2 new param files, DSP engine integration, 2 uidesc template populations

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor handles param changes + engine wiring; Controller handles registration + display formatting. No cross-inclusion. |
| II. Real-Time Audio Thread Safety | PASS | Rungler is already implemented with no allocations. It will be processed in `ModulationEngine::process()` (same pattern as Chaos, S&H). Macro values forwarded via atomic stores from `processParameterChanges()`. |
| III. Modern C++ Standards | PASS | Using `std::atomic`, `constexpr`, `std::clamp`. No raw new/delete in new code. |
| V. VSTGUI Development | PASS | Controls use UIDescription XML (ArcKnob, ToggleButton, CTextLabel). All control-tags bound to registered VST parameters. Cross-platform only. |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. All UI via VSTGUI abstractions. |
| VIII. Testing Discipline | PASS | Tests written before implementation. All existing tests must pass. |
| IX. Layered DSP Architecture | PASS | Rungler is Layer 2 (processors). ModulationEngine is Layer 3 (systems). Layer 3 can depend on Layer 2. No new DSP classes created. |
| XII. Debugging Discipline | PASS | No framework pivots needed. |
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
| `MacroParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/macro_params.h` |
| `RunglerParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/rungler_params.h` |

**Utility Functions to be created**:

| Planned Function | Search Result | Action |
|------------------|---------------|--------|
| `registerMacroParams()` | Not found | Create New in `macro_params.h` |
| `handleMacroParamChange()` | Not found | Create New in `macro_params.h` |
| `formatMacroParam()` | Not found | Create New in `macro_params.h` |
| `saveMacroParams()` | Not found | Create New in `macro_params.h` |
| `loadMacroParams()` | Not found | Create New in `macro_params.h` |
| `loadMacroParamsToController()` | Not found | Create New in `macro_params.h` |
| `registerRunglerParams()` | Not found | Create New in `rungler_params.h` |
| `handleRunglerParamChange()` | Not found | Create New in `rungler_params.h` |
| `formatRunglerParam()` | Not found | Create New in `rungler_params.h` |
| `saveRunglerParams()` | Not found | Create New in `rungler_params.h` |
| `loadRunglerParams()` | Not found | Create New in `rungler_params.h` |
| `loadRunglerParamsToController()` | Not found | Create New in `rungler_params.h` |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `MacroConfig` struct | `dsp/include/krate/dsp/core/modulation_types.h:117-122` | 0 | Macro values stored here; `setMacroValue()` already implemented |
| `ModulationEngine::setMacroValue()` | `modulation_engine.h:435-439` | 3 | Called from processor for macro value forwarding |
| `ModulationEngine::getMacroOutput()` | `modulation_engine.h:641-652` | 3 | Already returns processed macro value in `getRawSourceValue()` |
| `RuinaeEngine::setMacroValue()` | `ruinae_engine.h:416-418` | Plugin | Already forwards to `globalModEngine_.setMacroValue()` |
| `Rungler` class | `dsp/include/krate/dsp/processors/rungler.h:62-468` | 2 | Fully implemented DSP. Will be instantiated in ModulationEngine. |
| `Rungler::setOsc1Frequency()` | `rungler.h:166-171` | 2 | Called via new engine setters |
| `Rungler::setOsc2Frequency()` | `rungler.h:175-180` | 2 | Called via new engine setters |
| `Rungler::setRunglerDepth()` | `rungler.h:196-200` | 2 | Sets both osc depths simultaneously |
| `Rungler::setFilterAmount()` | `rungler.h:204-207` | 2 | CV smoothing amount |
| `Rungler::setRunglerBits()` | `rungler.h:211-217` | 2 | Shift register length |
| `Rungler::setLoopMode()` | `rungler.h:221-223` | 2 | Chaos vs loop mode |
| `Rungler::getCurrentValue()` | `rungler.h:237-239` | 2 | Returns filtered CV output [0,1] for mod routing |
| `Rungler::prepare()` | `rungler.h:105-131` | 2 | Prepare with sample rate |
| `Rungler::reset()` | `rungler.h:140-158` | 2 | Reset internal state |
| `Rungler::processBlock()` | `rungler.h:321-331` | 2 | Block-rate processing |
| `kModSourceStrings` | `dropdown_mappings.h:172-186` | Plugin | Insert "Rungler" at index 10 |
| `kModSourceCount` | `dropdown_mappings.h:170` | Plugin | Increase from 13 to 14 |
| `kNumGlobalSources` | `plugins/shared/src/ui/mod_matrix_types.h:31` | Shared | Increase from 12 to 13 |
| `ModSource_Macros` template | `editor.uidesc` | UI | Populate with 4 ArcKnob controls |
| `ModSource_Rungler` template | `editor.uidesc` | UI | Populate with 6 controls |
| `createDropdownParameter()` | `controller/parameter_helpers.h:22-40` | Plugin | Used for any dropdown params (not needed for this spec but available) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/modulation_types.h` - ModSource enum, MacroConfig, kModSourceCount
- [x] `dsp/include/krate/dsp/processors/rungler.h` - Rungler class, all constants and methods
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - ModulationEngine, all fields and methods
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum, kNumParameters
- [x] `plugins/ruinae/src/parameters/` - All existing *_params.h files (19 files)
- [x] `plugins/ruinae/src/processor/processor.h` - Processor fields, processParameterChanges
- [x] `plugins/ruinae/src/processor/processor.cpp` - getState/setState, applyParamsToEngine
- [x] `plugins/ruinae/src/controller/controller.cpp` - initialize, setComponentState, getParamStringByValue
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - All dropdown string arrays
- [x] `plugins/shared/src/ui/mod_matrix_types.h` - kNumGlobalSources

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new DSP classes created. Two new parameter structs (`MacroParams`, `RunglerParams`) are unique names not found anywhere in the codebase. The `Rungler` DSP class already exists and is only being instantiated (not redefined). All new functions follow the established naming pattern (`register*Params`, `handle*ParamChange`, etc.) with unique prefixes (`Macro`, `Rungler`).

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `Rungler` | `kDefaultOsc1Freq` | `static constexpr float kDefaultOsc1Freq = 200.0f` | Yes |
| `Rungler` | `kDefaultOsc2Freq` | `static constexpr float kDefaultOsc2Freq = 300.0f` | Yes |
| `Rungler` | `kDefaultBits` | `static constexpr size_t kDefaultBits = 8` | Yes |
| `Rungler` | `kMinBits` | `static constexpr size_t kMinBits = 4` (line 85) | Yes |
| `Rungler` | `kMaxBits` | `static constexpr size_t kMaxBits = 16` (line 86) | Yes |
| `Rungler` | `kMinFrequency` | `static constexpr float kMinFrequency = 0.1f` (line 81) | Yes |
| `Rungler` | `kMaxFrequency` | `static constexpr float kMaxFrequency = 20000.0f` (line 82) | Yes |
| `Rungler` | `setOsc1Frequency` | `void setOsc1Frequency(float hz) noexcept` (line 166) | Yes |
| `Rungler` | `setOsc2Frequency` | `void setOsc2Frequency(float hz) noexcept` (line 175) | Yes |
| `Rungler` | `setRunglerDepth` | `void setRunglerDepth(float depth) noexcept` (line 196) | Yes |
| `Rungler` | `setFilterAmount` | `void setFilterAmount(float amount) noexcept` (line 204) | Yes |
| `Rungler` | `setRunglerBits` | `void setRunglerBits(size_t bits) noexcept` (line 211) | Yes |
| `Rungler` | `setLoopMode` | `void setLoopMode(bool loop) noexcept` (line 221) | Yes |
| `Rungler` | `getCurrentValue` | `[[nodiscard]] float getCurrentValue() const noexcept override` (line 237) | Yes |
| `Rungler` | `prepare` | `void prepare(double sampleRate) noexcept` (line 105) | Yes |
| `Rungler` | `reset` | `void reset() noexcept` (line 140) | Yes |
| `Rungler` | `processBlock` | `void processBlock(size_t numSamples) noexcept` (line 321) | Yes |
| `ModulationEngine` | `setMacroValue` | `void setMacroValue(size_t index, float value) noexcept` (line 435) | Yes |
| `RuinaeEngine` | `setMacroValue` | `void setMacroValue(size_t index, float value) noexcept` (line 416) | Yes |
| `ModSource` | enum | `enum class ModSource : uint8_t { ..., Chaos = 9, SampleHold = 10, ... }` (line 35) | Yes |
| `kModSourceCount` | constant | `inline constexpr uint8_t kModSourceCount = 13;` (line 53) | Yes |
| `kNumGlobalSources` | constant | `inline constexpr int kNumGlobalSources = 12;` (mod_matrix_types.h:31) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/rungler.h` - Rungler class, all public methods and constants
- [x] `dsp/include/krate/dsp/core/modulation_types.h` - ModSource enum, MacroConfig, kModSourceCount
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - All methods (prepare, reset, process, getRawSourceValue, setMacroValue), all fields
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - setMacroValue, globalModEngine_ field
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum, kCurrentStateVersion = 12
- [x] `plugins/ruinae/src/processor/processor.h` - Processor class fields
- [x] `plugins/ruinae/src/processor/processor.cpp` - processParameterChanges, getState, setState, applyParamsToEngine
- [x] `plugins/ruinae/src/controller/controller.cpp` - initialize (parameter registration), setComponentState, getParamStringByValue
- [x] `plugins/ruinae/src/parameters/mono_mode_params.h` - Reference pattern for new param files
- [x] `plugins/ruinae/src/parameters/global_filter_params.h` - Reference pattern for new param files
- [x] `plugins/ruinae/src/parameters/chaos_mod_params.h` - Mod source view mode parameter registration
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - kModSourceStrings, kModSourceCount
- [x] `plugins/shared/src/ui/mod_matrix_types.h` - kNumGlobalSources
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter helper

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `Rungler::setRunglerDepth()` | Sets BOTH osc1 and osc2 depth simultaneously | Call `setRunglerDepth(depth)` once, NOT `setOsc1RunglerDepth` + `setOsc2RunglerDepth` separately |
| `Rungler::setRunglerBits()` | Takes `size_t`, not `int` | Cast from float: `static_cast<size_t>(bits)` |
| `ModSource` enum | Values are `uint8_t`, NOT `int` | Cast appropriately when loading from stream |
| `kModSourceCount` | Currently 13, includes None | After change: 14 (None + 13 sources) |
| `kNumGlobalSources` | Currently 12, excludes None | After change: 13 (sources only, no None) |
| State version | Currently 12 | Bump to 13; new data appended at END of stream |
| `Rungler::processBlock()` | Takes only `numSamples` (no audio input) | Unlike EnvFollower/Transient, Rungler is self-contained |
| Freq UI range vs DSP range | UI: 0.1-100 Hz, DSP: 0.1-20000 Hz | Use logarithmic mapping in UI, clamp to 100 Hz max |
| Default Osc freqs | DSP class defaults (200/300 Hz) are outside modulation UI range | **Decision**: Use 2.0 Hz and 3.0 Hz as UI defaults. Maintains the DSP's 2:3 frequency ratio within the modulation-focused 0.1-100 Hz range. This produces perceptible slow modulation (0.5 sec and 0.33 sec periods) with interesting chaotic patterns from the incommensurate frequencies. The DSP class defaults (200/300 Hz) are audio-rate defaults for standalone Rungler use but fall outside the modulation range. |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Log frequency mapping for Rungler | Simple inline formula in param file, only used by rungler_params.h |

**Decision**: No Layer 0 extractions needed. All new code is plugin-level parameter plumbing (param structs, handle/register/format/save/load functions) that follows established patterns. The DSP class (Rungler) already exists at Layer 2.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Rungler has cross-modulation feedback between oscillators and shift register feedback |
| **Data parallelism width** | 1 | Single Rungler instance; 2 oscillators but tightly coupled |
| **Branch density in inner loop** | HIGH | Direction reversal in triangle oscillators, conditional shift register clocking |
| **Dominant operations** | Bitwise + arithmetic | Shift register XOR, phase increments, filter updates |
| **Current CPU budget vs expected usage** | < 0.1% expected | Rungler processes at block rate, only when actively routed |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The Rungler is a single-instance processor with tightly coupled oscillator feedback, branch-heavy direction reversal logic, and bitwise shift register operations. Its CPU footprint is negligible (block-rate processing, only when routed). SIMD would provide zero benefit -- the algorithm is fundamentally serial and already extremely cheap.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| sourceActive_ early-out | Rungler only runs when routed | LOW | YES (already designed into ModulationEngine pattern) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin parameter layer (param files, processor wiring, uidesc controls)

**Related features at same layer** (from Roadmap Phase 6):
- Phase 6.1: Env Follower params + view
- Phase 6.2: Sample & Hold params + view
- Phase 6.3: Random params + view
- Phase 6.4: Pitch Follower params + view
- Phase 6.5: Transient Detector params + view

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `macro_params.h` pattern | HIGH | All Phase 6 param files | Keep local, use as template |
| `rungler_params.h` pattern | HIGH | All Phase 6 param files | Keep local, use as template |
| ModulationEngine integration pattern | HIGH | Phase 6 sources | Pattern documented, not extracted |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for param files | Each param file is self-contained with inline functions; parameterization would add complexity without benefit |
| Keep Rungler integration in ModulationEngine | Single class instance matches pattern of chaos_, sampleHold_, etc. |

### Review Trigger

After implementing **Phase 6 (Additional Mod Sources)**, review this section:
- [ ] Does Phase 6 need the same param file pattern? -> Copy template from macro_params.h / rungler_params.h
- [ ] Does Phase 6 use same ModulationEngine integration pattern? -> Follow Rungler integration as reference

## Project Structure

### Documentation (this feature)

```text
specs/057-macros-rungler/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── parameter-ids.md # Parameter ID definitions and mappings
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (files to create/modify)

```text
# NEW FILES
plugins/ruinae/src/parameters/macro_params.h          # MacroParams struct + register/handle/format/save/load
plugins/ruinae/src/parameters/rungler_params.h         # RunglerParams struct + register/handle/format/save/load

# MODIFIED FILES - DSP Layer
dsp/include/krate/dsp/core/modulation_types.h          # ModSource enum: insert Rungler=10, renumber, kModSourceCount 13->14
dsp/include/krate/dsp/systems/modulation_engine.h      # Add Rungler field, prepare/reset/process/getRawSourceValue integration

# MODIFIED FILES - Plugin Layer
plugins/ruinae/src/plugin_ids.h                         # Add macro/rungler param IDs, kNumParameters 2000->2200
plugins/ruinae/src/parameters/dropdown_mappings.h       # kModSourceStrings: insert "Rungler", kModSourceCount update
plugins/ruinae/src/engine/ruinae_engine.h               # Add rungler setter methods forwarding to globalModEngine_
plugins/ruinae/src/processor/processor.h                # Add MacroParams + RunglerParams fields
plugins/ruinae/src/processor/processor.cpp              # processParameterChanges, getState, setState, applyParamsToEngine
plugins/ruinae/src/controller/controller.cpp            # initialize (register), setComponentState, getParamStringByValue

# MODIFIED FILES - Shared UI
plugins/shared/src/ui/mod_matrix_types.h                # kNumGlobalSources 12->13

# MODIFIED FILES - UI
plugins/ruinae/resources/editor.uidesc                  # Control-tags + ModSource_Macros/Rungler template contents

# MODIFIED FILES - Tests
plugins/ruinae/tests/integration/mod_matrix_grid_test.cpp  # Update kNumGlobalSources assertion (12->13)
```

**Structure Decision**: Monorepo structure -- DSP changes in `dsp/`, plugin changes in `plugins/ruinae/src/`, shared UI in `plugins/shared/`, UI resources in `plugins/ruinae/resources/`.

## Detailed Implementation Design

### Task Group 1: Parameter IDs and Enum Changes (FR-001, FR-002, FR-003, FR-009, FR-015)

**Files modified**: `plugin_ids.h`, `modulation_types.h`, `dropdown_mappings.h`, `mod_matrix_types.h`

#### 1a. Add Macro and Rungler parameter IDs to `plugin_ids.h`

Insert after `kPhaserEndId = 1999` and before `kNumParameters`:

```cpp
// ==========================================================================
// Macro Parameters (2000-2099)
// ==========================================================================
kMacroBaseId = 2000,
kMacro1ValueId = 2000,     // Macro 1 knob value [0, 1] (default 0)
kMacro2ValueId = 2001,     // Macro 2 knob value [0, 1] (default 0)
kMacro3ValueId = 2002,     // Macro 3 knob value [0, 1] (default 0)
kMacro4ValueId = 2003,     // Macro 4 knob value [0, 1] (default 0)
kMacroEndId = 2099,

// ==========================================================================
// Rungler Parameters (2100-2199)
// ==========================================================================
kRunglerBaseId = 2100,
kRunglerOsc1FreqId = 2100, // Osc1 frequency [0.1, 100] Hz (default 2.0 Hz)
kRunglerOsc2FreqId = 2101, // Osc2 frequency [0.1, 100] Hz (default 3.0 Hz)
kRunglerDepthId = 2102,    // Cross-mod depth [0, 1] (default 0)
kRunglerFilterId = 2103,   // CV filter amount [0, 1] (default 0)
kRunglerBitsId = 2104,     // Shift register bits [4, 16] (default 8)
kRunglerLoopModeId = 2105, // Loop mode on/off (default off = chaos)
kRunglerEndId = 2199,

// ==========================================================================
kNumParameters = 2200,
```

#### 1b. Insert `ModSource::Rungler = 10` in the enum

In `modulation_types.h`, change:
```cpp
Chaos = 9,
Rungler = 10,       ///< Rungler (Benjolin chaotic oscillator)
SampleHold = 11,    ///< Sample & Hold (renumbered from 10)
PitchFollower = 12, ///< Pitch Follower (renumbered from 11)
Transient = 13      ///< Transient Detector (renumbered from 12)
```
And update: `inline constexpr uint8_t kModSourceCount = 14;`

#### 1c. Update `kModSourceStrings` in `dropdown_mappings.h`

Insert `STR16("Rungler")` at index 10 (after "Chaos", before "Sample & Hold"). Update comment to reflect 14 sources.

#### 1d. Update `kNumGlobalSources` in `mod_matrix_types.h`

Change from 12 to 13. Update comments accordingly.

#### 1e. Update `mod_matrix_grid_test.cpp`

Change `REQUIRE(kNumGlobalSources == 12)` to `REQUIRE(kNumGlobalSources == 13)`.

### Task Group 2: ModulationEngine Rungler Integration (FR-008)

**File modified**: `dsp/include/krate/dsp/systems/modulation_engine.h`

#### 2a. Add Rungler field

Add member field alongside existing sources:
```cpp
Rungler rungler_;     // After transient_ field
```

Add `#include <krate/dsp/processors/rungler.h>` at the top.

#### 2b. Add Rungler setter methods

Add public setter methods following the pattern of chaos/sampleHold setters:
```cpp
// Rungler source setters
void setRunglerOsc1Freq(float hz) noexcept { rungler_.setOsc1Frequency(hz); }
void setRunglerOsc2Freq(float hz) noexcept { rungler_.setOsc2Frequency(hz); }
void setRunglerDepth(float depth) noexcept { rungler_.setRunglerDepth(depth); }
void setRunglerFilter(float amount) noexcept { rungler_.setFilterAmount(amount); }
void setRunglerBits(size_t bits) noexcept { rungler_.setRunglerBits(bits); }
void setRunglerLoopMode(bool loop) noexcept { rungler_.setLoopMode(loop); }
```

#### 2c. Integrate Rungler in `prepare()`

Add after `transient_.prepare(sampleRate);`:
```cpp
rungler_.prepare(sampleRate);
```

#### 2d. Integrate Rungler in `reset()`

Add after `transient_.reset();`:
```cpp
rungler_.reset();
```

#### 2e. Integrate Rungler in `process()`

In the per-block sources section, add:
```cpp
if (sourceActive_[static_cast<size_t>(ModSource::Rungler)]) {
    rungler_.processBlock(safeSamples);
}
```

#### 2f. Add Rungler case in `getRawSourceValue()`

Insert before `case ModSource::SampleHold:`:
```cpp
case ModSource::Rungler:
    return rungler_.getCurrentValue();
```

### Task Group 3: RuinaeEngine Rungler Forwarding (FR-010)

**File modified**: `plugins/ruinae/src/engine/ruinae_engine.h`

Add setter methods forwarding to `globalModEngine_`:
```cpp
void setRunglerOsc1Freq(float hz) noexcept { globalModEngine_.setRunglerOsc1Freq(hz); }
void setRunglerOsc2Freq(float hz) noexcept { globalModEngine_.setRunglerOsc2Freq(hz); }
void setRunglerDepth(float depth) noexcept { globalModEngine_.setRunglerDepth(depth); }
void setRunglerFilter(float amount) noexcept { globalModEngine_.setRunglerFilter(amount); }
void setRunglerBits(size_t bits) noexcept { globalModEngine_.setRunglerBits(bits); }
void setRunglerLoopMode(bool loop) noexcept { globalModEngine_.setRunglerLoopMode(loop); }
```

### Task Group 4: Parameter Files (FR-004, FR-005)

#### 4a. Create `macro_params.h`

New file at `plugins/ruinae/src/parameters/macro_params.h` following `mono_mode_params.h` pattern:

- **`MacroParams` struct**: 4x `std::atomic<float>` for macro values (default 0.0f)
- **`handleMacroParamChange()`**: Switch on IDs 2000-2003, store value [0, 1]
- **`registerMacroParams()`**: Register 4 continuous parameters with `kCanAutomate`, unit "%", titles "Macro 1".."Macro 4", default 0.0
- **`formatMacroParam()`**: Display as "XX%" (0 decimals) for all 4 macros
- **`saveMacroParams()`**: Write 4 floats
- **`loadMacroParams()`**: Read 4 floats, return false on EOF
- **`loadMacroParamsToController()`**: Read 4 floats, call `setParam(kMacroNValueId, value)` (value is already normalized)

#### 4b. Create `rungler_params.h`

New file at `plugins/ruinae/src/parameters/rungler_params.h`:

- **`RunglerParams` struct**:
  - `std::atomic<float> osc1FreqHz{2.0f}` -- UI default within 0.1-100 Hz range
  - `std::atomic<float> osc2FreqHz{3.0f}` -- UI default within 0.1-100 Hz range
  - `std::atomic<float> depth{0.0f}` -- [0, 1]
  - `std::atomic<float> filter{0.0f}` -- [0, 1]
  - `std::atomic<int> bits{8}` -- [4, 16]
  - `std::atomic<bool> loopMode{false}` -- chaos mode default

- **`handleRunglerParamChange()`**: Logarithmic denormalization for freq params (0.1-100 Hz), linear for depth/filter, discrete for bits (stepCount=12 -> 4-16), boolean for loop mode

- **`registerRunglerParams()`**: Register 6 parameters:
  - Osc1 Freq: continuous, "Hz", log mapping default for 2.0 Hz
  - Osc2 Freq: continuous, "Hz", log mapping default for 3.0 Hz
  - Depth: continuous, "%", default 0.0
  - Filter: continuous, "%", default 0.0
  - Bits: stepCount=12 (13 values: 4,5,6,...,16), default for 8
  - Loop Mode: stepCount=1, default 0.0

- **`formatRunglerParam()`**: Freq: "X.XX Hz" (2 decimals), Depth/Filter: "XX%" (0 decimals), Bits: integer "X", Loop: handled by framework (on/off)

- **`saveRunglerParams()`**: Write osc1FreqHz (float), osc2FreqHz (float), depth (float), filter (float), bits (int32), loopMode (int32)

- **`loadRunglerParams()`**: Read in same order, return false on EOF

- **`loadRunglerParamsToController()`**: Inverse mappings for controller sync

**Logarithmic frequency mapping**: `hz = 0.1 * pow(1000.0, normalized)` maps [0,1] to [0.1, 100] Hz. Inverse: `normalized = log(hz/0.1) / log(1000.0)`.

Default normalized values:
- Osc1 (2.0 Hz): `log(2.0/0.1) / log(1000) = log(20) / log(1000) = 1.301/3.0 = 0.4337`
- Osc2 (3.0 Hz): `log(3.0/0.1) / log(1000) = log(30) / log(1000) = 1.477/3.0 = 0.4924`
- Bits (8): default index = (8-4) / 12 = 0.3333 (stepCount=12, 4 + round(norm * 12) = bits)

### Task Group 5: Processor Wiring (FR-006, FR-007, FR-011)

**Files modified**: `processor.h`, `processor.cpp`

#### 5a. Add param fields to `Processor`

Add after `monoModeParams_`:
```cpp
MacroParams macroParams_;
RunglerParams runglerParams_;
```

Add includes for `macro_params.h` and `rungler_params.h`.

#### 5b. Extend `processParameterChanges()`

Add after the mono mode section:
```cpp
} else if (paramId >= kMacroBaseId && paramId <= kMacroEndId) {
    handleMacroParamChange(macroParams_, paramId, value);
} else if (paramId >= kRunglerBaseId && paramId <= kRunglerEndId) {
    handleRunglerParamChange(runglerParams_, paramId, value);
}
```

#### 5c. Extend `applyParamsToEngine()`

Add after mono mode section:
```cpp
// --- Macros ---
for (int i = 0; i < 4; ++i) {
    engine_.setMacroValue(static_cast<size_t>(i),
        macroParams_.values[i].load(std::memory_order_relaxed));
}

// --- Rungler ---
engine_.setRunglerOsc1Freq(runglerParams_.osc1FreqHz.load(std::memory_order_relaxed));
engine_.setRunglerOsc2Freq(runglerParams_.osc2FreqHz.load(std::memory_order_relaxed));
engine_.setRunglerDepth(runglerParams_.depth.load(std::memory_order_relaxed));
engine_.setRunglerFilter(runglerParams_.filter.load(std::memory_order_relaxed));
engine_.setRunglerBits(static_cast<size_t>(runglerParams_.bits.load(std::memory_order_relaxed)));
engine_.setRunglerLoopMode(runglerParams_.loopMode.load(std::memory_order_relaxed));
```

#### 5d. Extend `getState()` (state version 13)

Bump `kCurrentStateVersion` from 12 to 13. Append after v12 extended LFO params:
```cpp
// v13: Macro and Rungler params
saveMacroParams(macroParams_, streamer);
saveRunglerParams(runglerParams_, streamer);
```

#### 5e. Extend `setState()` with backward compatibility

In the `version >= 3` branch, add after v12 LFO extended loading:
```cpp
// v13: Macro and Rungler params
if (version >= 13) {
    loadMacroParams(macroParams_, streamer);
    loadRunglerParams(runglerParams_, streamer);
}
// Note: For version < 13, macro/rungler params keep their default values
// (macros = 0.0, rungler = DSP defaults)
```

### Task Group 6: Preset Migration for ModSource Renumbering (FR-009a)

**File modified**: `processor.cpp` (setState), `mod_matrix_params.h` (loadModMatrixParams)

When loading presets with version < 13, the mod matrix source values need migration:
- Old SampleHold (10) -> New SampleHold (11)
- Old PitchFollower (11) -> New PitchFollower (12)
- Old Transient (12) -> New Transient (13)
- Values 0-9 remain unchanged
- New Rungler (10) only appears in v13+ presets

**Implementation approach**: Add a migration step in `applyParamsToEngine()` or in `loadModMatrixParams()` that checks the version. The cleanest approach is to apply migration during state loading when `version < 13`:

**Detailed Migration Algorithm:**

In `Processor::setState()`, after loading mod matrix params but before the load completes, if `version < 13`:
```cpp
// Migrate mod source enum values: Rungler (10) inserted before SampleHold
if (version < 13) {
    // Migrate global mod matrix slots
    for (auto& slot : modMatrixParams_.slots) {
        int src = slot.source.load(std::memory_order_relaxed);
        if (src >= 10) { // Old SampleHold=10, PitchFollower=11, Transient=12
            slot.source.store(src + 1, std::memory_order_relaxed);
        }
    }

    // Migrate voice route sources (loaded in v3+ branch)
    // Voice routes store source as int8_t in the route struct
    for (size_t voiceIdx = 0; voiceIdx < kNumVoices; ++voiceIdx) {
        for (auto& route : voiceRouteParams_[voiceIdx]) {
            if (route.source >= 10) {
                route.source = static_cast<int8_t>(route.source + 1);
            }
        }
    }
}
```

**Migration scope**: Two locations need migration:
1. **Mod matrix params** (global slots): `modMatrixParams_.slots[i].source` — stored as atomic int
2. **Voice routes**: Voice route source fields — stored as int8_t in route struct

**Same migration logic** must be applied in `Controller::setComponentState()` for controller-side mod matrix loading to keep UI sync correct.

### Task Group 7: Controller Registration and Display (FR-004, FR-005, FR-012)

**File modified**: `controller.cpp`

#### 7a. Register params in `Controller::initialize()`

After `registerMonoModeParams(parameters);`:
```cpp
registerMacroParams(parameters);
registerRunglerParams(parameters);
```

Add includes for `macro_params.h` and `rungler_params.h`.

#### 7b. Extend `setComponentState()`

In the `version >= 2` branch, after v12 LFO extended loading:
```cpp
// v13: Macro and Rungler params
if (version >= 13) {
    loadMacroParamsToController(streamer, setParam);
    loadRunglerParamsToController(streamer, setParam);
}
```

Apply the same mod source migration for controller-side mod matrix loading when `version < 13`.

#### 7c. Extend `getParamStringByValue()`

Add after the mono mode formatting block:
```cpp
} else if (id >= kMacroBaseId && id <= kMacroEndId) {
    result = formatMacroParam(id, valueNormalized, string);
} else if (id >= kRunglerBaseId && id <= kRunglerEndId) {
    result = formatRunglerParam(id, valueNormalized, string);
}
```

### Task Group 8: Control-Tags and UI Templates (FR-012, FR-013, FR-014)

**File modified**: `editor.uidesc`

#### 8a. Add control-tags

In the control-tags section, add:
```xml
<control-tag name="Macro1Value" tag="2000"/>
<control-tag name="Macro2Value" tag="2001"/>
<control-tag name="Macro3Value" tag="2002"/>
<control-tag name="Macro4Value" tag="2003"/>
<control-tag name="RunglerOsc1Freq" tag="2100"/>
<control-tag name="RunglerOsc2Freq" tag="2101"/>
<control-tag name="RunglerDepth" tag="2102"/>
<control-tag name="RunglerFilter" tag="2103"/>
<control-tag name="RunglerBits" tag="2104"/>
<control-tag name="RunglerLoopMode" tag="2105"/>
```

#### 8b. Populate `ModSource_Macros` template

Four 28x28 ArcKnobs in horizontal row with labels:
- M1 knob at (4, 0), label "M1" at (4, 28)
- M2 knob at (42, 0), label "M2" at (42, 28)
- M3 knob at (80, 0), label "M3" at (80, 28)
- M4 knob at (118, 0), label "M4" at (118, 28)
- All knobs: `arc-color="modulation"`, `guide-color="knob-guide"`, default "0.0"

#### 8c. Populate `ModSource_Rungler` template

Row 1 (y=0): Four 28x28 ArcKnobs:
- Osc1 Freq at (4, 0), label "Osc1" at (4, 28)
- Osc2 Freq at (42, 0), label "Osc2" at (42, 28)
- Depth at (80, 0), label "Depth" at (80, 28)
- Filter at (118, 0), label "Filter" at (118, 28)

Row 2 (y=50): Bits knob + Loop toggle:
- Bits knob (28x28) at (4, 50), label "Bits" at (4, 78)
- Loop Mode ToggleButton at (50, 54), label "Loop" at (50, 78)
- All knobs: `arc-color="modulation"`, `guide-color="knob-guide"`
- Toggle: `on-color="modulation"`, `off-color="toggle-off"`, title "Loop"

### Task Group 9: Architecture Documentation Update

Update `specs/_architecture_/` to document:
- Macro parameter exposure (linking param IDs to MacroConfig/ModulationEngine)
- Rungler integration into ModulationEngine (new source, prepare/reset/process pattern)
- State version 13 format (appended macro + rungler param packs)

## Complexity Tracking

No constitution violations to justify. All design decisions follow established patterns exactly.

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor owns MacroParams/RunglerParams. Controller registers/formats. No cross-dependency. |
| II. Real-Time Thread Safety | PASS | All param stores use `std::atomic` with `memory_order_relaxed`. Rungler already RT-safe. No allocations in any new code. |
| III. Modern C++ | PASS | `std::atomic`, `constexpr`, `std::clamp`, `static_cast`. No raw new/delete. |
| V. VSTGUI | PASS | All controls via UIDescription XML. ArcKnob, CTextLabel, ToggleButton -- all cross-platform. |
| VI. Cross-Platform | PASS | No platform-specific code. `std::pow`, `std::log`, `std::clamp` used for mappings. |
| VIII. Testing | PASS | Tests for param handling, state persistence, enum migration, engine integration. |
| IX. Layered DSP | PASS | Rungler (Layer 2) instantiated in ModulationEngine (Layer 3). Correct dependency direction. |
| XIII. Test-First | PASS | Tests designed before implementation for each task group. |
| XV. ODR Prevention | PASS | No duplicate types. All new names verified unique. |
| XVI. Honest Completion | PASS | Compliance table will be filled from actual code/test verification only. |
