# Implementation Plan: Ruinae Harmonizer Integration

**Branch**: `067-ruinae-harmonizer` | **Date**: 2026-02-19 | **Spec**: `specs/067-ruinae-harmonizer/spec.md`
**Input**: Feature specification from `specs/067-ruinae-harmonizer/spec.md`

## Summary

Integrate the existing `HarmonizerEngine` (Layer 3 DSP component) into the Ruinae synthesizer's effects chain, placed between the delay and reverb slots. This involves extending the `RuinaeEffectsChain` with a harmonizer processing slot, adding 28+ new parameters (8 global + 4 voices x 5 per-voice), extending the UI with a collapsible harmonizer panel in the effects section, and updating state serialization and latency reporting. No new DSP algorithms are needed -- only instantiation, parameter forwarding, and UI wiring.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared DSP library)
**Storage**: Binary state stream (IBStreamer) for preset serialization
**Testing**: Catch2 (unit + integration), pluginval (VST3 validation)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo with shared DSP library + plugin-specific code
**Performance Goals**: <2% CPU when disabled, <15% total at 44.1kHz with 4 voices (SC-003)
**Constraints**: Real-time safety (no allocations in audio thread), cross-platform VSTGUI only
**Scale/Scope**: ~28 new parameters, 1 new param struct file, modifications to ~8 existing files, ~1 new UI panel

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; no cross-includes
- [x] Parameter flow: Host -> Processor -> Controller via setComponentState()
- [x] All harmonizer params registered in Controller::initialize() and handled in Processor::processParameterChanges()

**Principle II (Real-Time Audio Thread Safety):**
- [x] HarmonizerEngine::prepare() called in setupProcessing() (pre-allocates all buffers)
- [x] HarmonizerEngine::process() is noexcept, zero allocations
- [x] Parameter forwarding uses atomic loads (memory_order_relaxed)
- [x] Stereo-to-mono summation uses stack/pre-allocated buffers

**Principle III (Modern C++):**
- [x] No raw new/delete for harmonizer
- [x] std::atomic for param struct fields
- [x] constexpr constants for parameter ranges

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis in dedicated section below
- [x] Scalar-first workflow (HarmonizerEngine already scalar + tested)

**Principle V (VSTGUI Development):**
- [x] UI panel defined in editor.uidesc XML
- [x] Expand/collapse follows existing chevron pattern
- [x] Voice row dimming via controller verifyView() + setParamNormalized()

**Principle VIII (Testing Discipline):**
- [x] Tests written before implementation (test-first)
- [x] Existing tests must continue passing
- [x] New tests for effects chain integration, parameter roundtrip, UI state

**Principle IX (Layered DSP Architecture):**
- [x] HarmonizerEngine is Layer 3, composed into RuinaeEffectsChain (also Layer 3)
- [x] No layer violations

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (Living Architecture Documentation):**
- [x] Architecture update task added (T082 in Phase 9): `specs/_architecture_/plugin-architecture.md` will be updated with the 2800-2899 parameter range row and the Harmonizer FX expand panel row

**Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Compliance table uses specific evidence (file paths, line numbers, measured values)

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `RuinaeHarmonizerParams`

| Planned Type | Search Command | Existing? | Action |
|---|---|---|---|
| RuinaeHarmonizerParams | `grep -r "RuinaeHarmonizerParams" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: `handleHarmonizerParamChange`, `registerHarmonizerParams`, `formatHarmonizerParam`, `saveHarmonizerParams`, `loadHarmonizerParams`, `loadHarmonizerParamsToController`

| Planned Function | Search Command | Existing? | Location | Action |
|---|---|---|---|---|
| handleHarmonizerParamChange | `grep -r "handleHarmonizerParamChange" plugins/` | No | N/A | Create New |
| registerHarmonizerParams | `grep -r "registerHarmonizerParams" plugins/` | No | N/A | Create New |
| formatHarmonizerParam | `grep -r "formatHarmonizerParam" plugins/` | No | N/A | Create New |
| saveHarmonizerParams | `grep -r "saveHarmonizerParams" plugins/` | No | N/A | Create New |
| loadHarmonizerParams | `grep -r "loadHarmonizerParams" plugins/` | No | N/A | Create New |
| loadHarmonizerParamsToController | `grep -r "loadHarmonizerParamsToController" plugins/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| HarmonizerEngine | `dsp/include/krate/dsp/systems/harmonizer_engine.h` | 3 | Direct instantiation in RuinaeEffectsChain |
| RuinaePhaserParams | `plugins/ruinae/src/parameters/phaser_params.h` | Plugin | Pattern template for RuinaeHarmonizerParams |
| RuinaeEffectsChain | `plugins/ruinae/src/engine/ruinae_effects_chain.h` | 3 | Extend with harmonizer slot |
| RuinaeEngine | `plugins/ruinae/src/engine/ruinae_engine.h` | Plugin | Extend with harmonizer setters |
| dbToGain | `dsp/include/krate/dsp/core/db_utils.h` | 0 | dB-to-linear conversion in param handling |
| ScaleType | `dsp/include/krate/dsp/core/scale_harmonizer.h` | 0 | Dropdown enum for scale selection |
| HarmonyMode | `dsp/include/krate/dsp/systems/harmonizer_engine.h` | 3 | Dropdown enum for harmony mode |
| PitchMode | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | Dropdown enum for pitch shift mode |
| createDropdownParameter | `plugins/ruinae/src/controller/parameter_helpers.h` | Plugin | Dropdown registration helper |
| createDropdownParameterWithDefault | `plugins/ruinae/src/controller/parameter_helpers.h` | Plugin | Dropdown with default index |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/harmonizer_engine.h` - HarmonizerEngine API
- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - PitchMode enum, latency API
- [x] `dsp/include/krate/dsp/core/scale_harmonizer.h` - ScaleType enum, kNumScaleTypes
- [x] `plugins/ruinae/src/parameters/` - All existing param files
- [x] `plugins/ruinae/src/engine/ruinae_effects_chain.h` - Current effects chain
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - Current engine composition
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID ranges

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types (`RuinaeHarmonizerParams`) are unique and not found in the codebase. The new functions all follow the established `{action}{Effect}Params` naming pattern with the `Harmonizer` infix, which has no existing usage. The HarmonizerEngine itself is only used in DSP tests currently, not in any plugin.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| HarmonizerEngine | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` | Yes |
| HarmonizerEngine | reset | `void reset() noexcept` | Yes |
| HarmonizerEngine | process | `void process(const float* input, float* outputL, float* outputR, std::size_t numSamples) noexcept` | Yes |
| HarmonizerEngine | setHarmonyMode | `void setHarmonyMode(HarmonyMode mode) noexcept` | Yes |
| HarmonizerEngine | setNumVoices | `void setNumVoices(int count) noexcept` | Yes |
| HarmonizerEngine | setKey | `void setKey(int rootNote) noexcept` | Yes |
| HarmonizerEngine | setScale | `void setScale(ScaleType type) noexcept` | Yes |
| HarmonizerEngine | setPitchShiftMode | `void setPitchShiftMode(PitchMode mode) noexcept` | Yes |
| HarmonizerEngine | setFormantPreserve | `void setFormantPreserve(bool enable) noexcept` | Yes |
| HarmonizerEngine | setDryLevel | `void setDryLevel(float dB) noexcept` | Yes |
| HarmonizerEngine | setWetLevel | `void setWetLevel(float dB) noexcept` | Yes |
| HarmonizerEngine | setVoiceInterval | `void setVoiceInterval(int voiceIndex, int diatonicSteps) noexcept` | Yes |
| HarmonizerEngine | setVoiceLevel | `void setVoiceLevel(int voiceIndex, float dB) noexcept` | Yes |
| HarmonizerEngine | setVoicePan | `void setVoicePan(int voiceIndex, float pan) noexcept` | Yes |
| HarmonizerEngine | setVoiceDelay | `void setVoiceDelay(int voiceIndex, float ms) noexcept` | Yes |
| HarmonizerEngine | setVoiceDetune | `void setVoiceDetune(int voiceIndex, float cents) noexcept` | Yes |
| HarmonizerEngine | getLatencySamples | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` | Yes |
| PitchShiftProcessor | getPhaseVocoderFFTSize | `[[nodiscard]] static constexpr std::size_t getPhaseVocoderFFTSize() noexcept` (returns 4096) | Yes |
| PitchShiftProcessor | getPhaseVocoderHopSize | `[[nodiscard]] static constexpr std::size_t getPhaseVocoderHopSize() noexcept` (returns 1024) | Yes |
| ScaleType | enum values | Major=0, NaturalMinor=1, ..., Chromatic=8 (kNumScaleTypes=9) | Yes |
| HarmonyMode | enum values | Chromatic=0, Scalic=1 | Yes |
| PitchMode | enum values | Simple=0, Granular=1, PhaseVocoder=2, PitchSync=3 | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/harmonizer_engine.h` - HarmonizerEngine class (full API)
- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - PitchMode enum, getLatencySamples()
- [x] `dsp/include/krate/dsp/core/scale_harmonizer.h` - ScaleType enum, kNumScaleTypes
- [x] `plugins/ruinae/src/engine/ruinae_effects_chain.h` - RuinaeEffectsChain (full implementation)
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - RuinaeEngine (setter patterns)
- [x] `plugins/ruinae/src/parameters/phaser_params.h` - RuinaePhaserParams (pattern template)
- [x] `plugins/ruinae/src/parameters/delay_params.h` - registerFxEnableParams
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - All dropdown mappings
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter()
- [x] `plugins/ruinae/src/controller/controller.h` - Controller class (UI state)
- [x] `plugins/ruinae/src/controller/controller.cpp` - toggleFxDetail(), verifyView()
- [x] `plugins/ruinae/src/processor/processor.h` - Processor class (param packs)
- [x] `plugins/ruinae/src/processor/processor.cpp` - processParameterChanges(), getState/setState, applyParamsToEngine()
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID allocation

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| HarmonizerEngine | Takes MONO input, produces STEREO output | Sum L+R to mono before calling process() |
| HarmonizerEngine | Dry/wet is internal to the engine | Do NOT add external dry/wet mixing; use setDryLevel/setWetLevel |
| HarmonizerEngine | getLatencySamples() returns latency for CURRENT mode | Must set PhaseVocoder mode first to get worst-case, then set back |
| PitchShiftProcessor | PhaseVocoder latency = FFTSize + HopSize = 5120 samples | This is the worst-case value to add to spectral delay latency |
| RuinaeEffectsChain | targetLatencySamples_ is already spectral delay latency | Must ADD harmonizer latency to it, not replace |
| State serialization | Version must increment (currently v15 -> v16) | Add harmonizer params AFTER v15 mod source params in save/load order |
| FX enable params | Registered in registerFxEnableParams() in delay_params.h | Add kHarmonizerEnabledId there alongside existing enables |
| toggleFxDetail | Uses array of 3 panels (delay, reverb, phaser) | Must expand to 4 panels to include harmonizer |
| Controller panel tracking | expandedFxPanel_ uses indices 0=delay, 1=reverb, 2=phaser | Add 3=harmonizer |
| Control listener range guard | `controller.cpp` line ~792: `tag >= kActionTransformInvertTag && tag <= kActionFxExpandPhaserTag` registers action button listeners; kActionFxExpandHarmonizerTag (10022) is outside the current upper bound (10018) and will never be registered | Update upper bound from `kActionFxExpandPhaserTag` to `kActionFxExpandHarmonizerTag` when adding the harmonizer case |
| State serialization types | data-model.md E-004 previously listed harmonyMode/key/scale/pitchShiftMode/numVoices as [float]; codebase pattern (phaser, delay) uses writeInt32 for int fields | Use writeInt32/readInt32 for all std::atomic<int> fields; writeFloat/readFloat for all std::atomic<float> fields |
| Dropdown denormalization | `value * (count - 1) + 0.5` pattern for int conversion | Must use same pattern for HarmonyMode, Key, Scale, PitchMode |
| kNumParameters | Currently set to 2800 (start of harmonizer range) | Must update to 2900 after adding harmonizer params |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| Stereo-to-mono sum | One-line operation (L+R)*0.5, only used in effects chain |

**Decision**: No Layer 0 extraction needed. All new code is plugin-level parameter handling and effects chain wiring. The HarmonizerEngine is already in the shared DSP library.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|---|---|---|
| **Feedback loops** | N/A | This feature is integration/wiring only, no new DSP algorithm |
| **Data parallelism width** | N/A | HarmonizerEngine already handles 4 voices internally |
| **Branch density in inner loop** | N/A | No new inner loops |
| **Dominant operations** | Parameter forwarding | All DSP operations are delegated to existing HarmonizerEngine |
| **Current CPU budget vs expected usage** | <15% total at 44.1kHz | HarmonizerEngine already benchmarked in spec 064 |

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature adds no new DSP processing code. It integrates an existing, already-tested HarmonizerEngine into the Ruinae plugin by wiring parameters and inserting it into the effects chain. The only new code is parameter registration, denormalization, state serialization, and UI layout -- none of which benefit from SIMD optimization. The HarmonizerEngine itself already had its SIMD analysis done in spec 064/065 (shared-analysis FFT optimization).

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|---|---|---|---|
| Skip harmonizer.process() when disabled | ~0% CPU when off (enable check only) | LOW | YES (FR-005) |
| Mono-to-stereo scratch buffer pre-allocation | Avoid per-block allocation | LOW | YES (already handled by pre-allocated buffers) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-level integration (wraps Layer 3 DSP)

**Related features at same layer** (from codebase):
- Iterum delay plugin could also benefit from harmonizer integration
- Future Krate plugins with effects chains

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| RuinaeHarmonizerParams pattern | MEDIUM | Iterum harmonizer integration | Keep local (plugin-specific param IDs) |
| Harmonizer dropdown mappings | MEDIUM | Iterum harmonizer integration | Keep in Ruinae dropdown_mappings.h for now |

### Decision Log

| Decision | Rationale |
|---|---|
| Keep RuinaeHarmonizerParams local to Ruinae | Plugin-specific parameter IDs and ranges; Iterum would have its own param struct with different IDs |
| Add harmonizer dropdowns to existing dropdown_mappings.h | Follows established pattern for all other dropdown enums |

### Review Trigger

After implementing harmonizer in Iterum (if planned), review:
- [ ] Does Iterum need identical dropdown mappings? -> Consider shared header
- [ ] Does Iterum use same param range pattern? -> Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/067-ruinae-harmonizer/
+-- spec.md              # Feature specification
+-- plan.md              # This file
+-- tasks.md             # Task breakdown (created by /speckit.tasks)
```

### Source Code (files to create or modify)

```text
# NEW FILES
plugins/ruinae/src/parameters/harmonizer_params.h     # RuinaeHarmonizerParams struct + all functions

# MODIFIED FILES
plugins/ruinae/src/plugin_ids.h                        # Add harmonizer param IDs (2800-2899, 1503)
plugins/ruinae/src/parameters/dropdown_mappings.h      # Add harmonizer dropdown constants
plugins/ruinae/src/parameters/delay_params.h           # Add kHarmonizerEnabledId to registerFxEnableParams
plugins/ruinae/src/engine/ruinae_effects_chain.h       # Add harmonizer_ member, setters, process slot
plugins/ruinae/src/engine/ruinae_engine.h              # Add harmonizer setter pass-throughs
plugins/ruinae/src/processor/processor.h               # Add harmonizer param pack + enabled flag
plugins/ruinae/src/processor/processor.cpp             # processParameterChanges, applyParamsToEngine, state save/load
plugins/ruinae/src/controller/controller.h             # Add fxDetailHarmonizer_ + chevron pointers
plugins/ruinae/src/controller/controller.cpp           # Register params, setComponentState, toggleFxDetail (4 panels), verifyView
plugins/ruinae/resources/editor.uidesc                 # Add harmonizer UI panel in effects section

# NEW TEST FILES
plugins/ruinae/tests/unit/harmonizer_params_test.cpp   # Parameter denormalization + roundtrip tests
```

**Structure Decision**: Plugin-local integration following established patterns. All harmonizer-specific code lives in `plugins/ruinae/` (parameters, processor wiring, controller, UI). The shared DSP engine at `dsp/include/krate/dsp/systems/harmonizer_engine.h` is used as-is.

---

## Detailed Design

### 1. Parameter ID Allocation (FR-006, FR-007, FR-010)

**File**: `plugins/ruinae/src/plugin_ids.h`

Add to the `ParameterIDs` enum:

```cpp
// ==========================================================================
// FX Enable Parameters (1500-1503)
// ==========================================================================
kDelayEnabledId = 1500,    // on/off (default: on)
kReverbEnabledId = 1501,   // on/off (default: on)
kPhaserEnabledId = 1502,   // on/off (default: on)
kHarmonizerEnabledId = 1503, // on/off (default: off)

// ==========================================================================
// Harmonizer Parameters (2800-2899)
// ==========================================================================
kHarmonizerBaseId = 2800,

// Global harmonizer params (2800-2807)
kHarmonizerHarmonyModeId = 2800,    // Chromatic/Scalic (dropdown, 2 entries)
kHarmonizerKeyId = 2801,             // C through B (dropdown, 12 entries)
kHarmonizerScaleId = 2802,           // 9 scale types (dropdown, 9 entries)
kHarmonizerPitchShiftModeId = 2803,  // Simple/Granular/PitchSync/PhaseVocoder (dropdown, 4 entries)
kHarmonizerFormantPreserveId = 2804, // on/off toggle
kHarmonizerNumVoicesId = 2805,       // 0-4 (dropdown, 5 entries)
kHarmonizerDryLevelId = 2806,        // -60 to +6 dB (default 0 dB = norm ~0.909)
kHarmonizerWetLevelId = 2807,        // -60 to +6 dB (default -6 dB = norm ~0.818)

// Per-voice params: Voice 1 (2810-2814)
kHarmonizerVoice1IntervalId = 2810,  // -24 to +24 diatonic steps (default 0)
kHarmonizerVoice1LevelId = 2811,     // -60 to +6 dB (default 0 dB)
kHarmonizerVoice1PanId = 2812,       // -1 to +1 (default 0 = center)
kHarmonizerVoice1DelayId = 2813,     // 0 to 50 ms (default 0)
kHarmonizerVoice1DetuneId = 2814,    // -50 to +50 cents (default 0)

// Per-voice params: Voice 2 (2820-2824)
kHarmonizerVoice2IntervalId = 2820,
kHarmonizerVoice2LevelId = 2821,
kHarmonizerVoice2PanId = 2822,
kHarmonizerVoice2DelayId = 2823,
kHarmonizerVoice2DetuneId = 2824,

// Per-voice params: Voice 3 (2830-2834)
kHarmonizerVoice3IntervalId = 2830,
kHarmonizerVoice3LevelId = 2831,
kHarmonizerVoice3PanId = 2832,
kHarmonizerVoice3DelayId = 2833,
kHarmonizerVoice3DetuneId = 2834,

// Per-voice params: Voice 4 (2840-2844)
kHarmonizerVoice4IntervalId = 2840,
kHarmonizerVoice4LevelId = 2841,
kHarmonizerVoice4PanId = 2842,
kHarmonizerVoice4DelayId = 2843,
kHarmonizerVoice4DetuneId = 2844,

kHarmonizerEndId = 2899,

// Update kNumParameters
kNumParameters = 2900,  // was 2800

// UI Action Tags - add harmonizer expand chevron
kActionFxExpandHarmonizerTag = 10022,  // Harmonizer FX expand/collapse
```

**Per-voice ID formula**: `kHarmonizerVoice{N}IntervalId = 2800 + 10 * N` where N is 1-4. Each voice occupies a 10-ID block (IDs ending in 0-4 used, 5-9 reserved).

### 2. Dropdown Mappings (FR-008)

**File**: `plugins/ruinae/src/parameters/dropdown_mappings.h`

Add harmonizer-specific dropdown constants:

```cpp
// =============================================================================
// Harmonizer: HarmonyMode dropdown (2 modes, stepCount = 1)
// =============================================================================
inline constexpr int kHarmonyModeCount = 2;
inline const Steinberg::Vst::TChar* const kHarmonyModeStrings[] = {
    STR16("Chromatic"),
    STR16("Scalic"),
};

// =============================================================================
// Harmonizer: Key dropdown (12 keys, stepCount = 11)
// =============================================================================
inline constexpr int kHarmonizerKeyCount = 12;
inline const Steinberg::Vst::TChar* const kHarmonizerKeyStrings[] = {
    STR16("C"), STR16("C#"), STR16("D"), STR16("Eb"),
    STR16("E"), STR16("F"), STR16("F#"), STR16("G"),
    STR16("Ab"), STR16("A"), STR16("Bb"), STR16("B"),
};

// =============================================================================
// Harmonizer: Scale dropdown (9 types, stepCount = 8)
// =============================================================================
inline constexpr int kHarmonizerScaleCount = 9; // == Krate::DSP::kNumScaleTypes
inline const Steinberg::Vst::TChar* const kHarmonizerScaleStrings[] = {
    STR16("Major"), STR16("Natural Minor"), STR16("Harmonic Minor"),
    STR16("Melodic Minor"), STR16("Dorian"), STR16("Mixolydian"),
    STR16("Phrygian"), STR16("Lydian"), STR16("Chromatic"),
};

// =============================================================================
// Harmonizer: PitchShiftMode dropdown (4 modes, stepCount = 3)
// =============================================================================
inline constexpr int kHarmonizerPitchModeCount = 4;
inline const Steinberg::Vst::TChar* const kHarmonizerPitchModeStrings[] = {
    STR16("Simple"), STR16("Granular"),
    STR16("Phase Vocoder"), STR16("Pitch Sync"),
};

// =============================================================================
// Harmonizer: NumVoices dropdown (5 options: 0-4, stepCount = 4)
// =============================================================================
inline constexpr int kHarmonizerNumVoicesCount = 5;
inline const Steinberg::Vst::TChar* const kHarmonizerNumVoicesStrings[] = {
    STR16("0"), STR16("1"), STR16("2"), STR16("3"), STR16("4"),
};

// =============================================================================
// Harmonizer: Interval dropdown (49 options: -24 to +24, stepCount = 48)
// =============================================================================
inline constexpr int kHarmonizerIntervalCount = 49; // -24 to +24 inclusive
// Helper: convert dropdown index (0..48) to diatonic step value (-24..+24)
inline int harmonizerIntervalFromIndex(int index) {
    return std::clamp(index - 24, -24, 24);
}
// Helper: convert diatonic step value to dropdown index
inline int harmonizerIntervalToIndex(int interval) {
    return std::clamp(interval + 24, 0, 48);
}
```

### 3. RuinaeHarmonizerParams Struct (FR-011)

**File**: `plugins/ruinae/src/parameters/harmonizer_params.h` (NEW)

Following the exact pattern of `phaser_params.h`, this file contains:

1. **`RuinaeHarmonizerParams` struct** with `std::atomic<>` fields for all 28 parameters:
   - Global: harmonyMode (int, 0-1), key (int, 0-11), scale (int, 0-8), pitchShiftMode (int, 0-3), formantPreserve (bool), numVoices (int, 0-4), dryLevelDb (float, default 0.0), wetLevelDb (float, default -6.0)
   - Per-voice (x4): interval (int, -24 to +24), levelDb (float, -60 to +6), pan (float, -1 to +1), delayMs (float, 0-50), detuneCents (float, -50 to +50)

2. **`handleHarmonizerParamChange()`** -- denormalization from 0-1 normalized to plain values:
   - Dropdowns: `value * (count - 1) + 0.5` truncated to int (matches existing pattern)
   - dB ranges: `value * 66.0 - 60.0` for [-60, +6] dB range
   - Interval: `value * 48.0 + 0.5` -> index, then `index - 24` for [-24, +24]
   - Pan: `value * 2.0 - 1.0` for [-1, +1]
   - Delay: `value * 50.0` for [0, 50] ms
   - Detune: `value * 100.0 - 50.0` for [-50, +50] cents
   - Uses per-voice ID blocks: `if (paramId >= kHarmonizerVoice1IntervalId && paramId <= kHarmonizerVoice1DetuneId)` etc.

3. **`registerHarmonizerParams()`** -- parameter registration with correct types:
   - Dropdowns use `createDropdownParameter()` / `createDropdownParameterWithDefault()`
   - Toggle uses stepCount=1
   - Continuous params use `addParameter()` with appropriate defaults

4. **`formatHarmonizerParam()`** -- display formatting for host automation lanes

5. **`saveHarmonizerParams()` / `loadHarmonizerParams()`** -- binary state serialization

6. **`loadHarmonizerParamsToController()`** -- controller state restore (normalized values)

**Default values (normalized):**
- harmonyMode: 0.0 (Chromatic)
- key: 0.0 (C)
- scale: 0.0 (Major)
- pitchShiftMode: 0.0 (Simple)
- formantPreserve: 0.0 (off)
- numVoices: 0.0 (0 voices = silent default)
- dryLevel: ~0.909 (0 dB in [-60, +6] range: (0 - (-60)) / 66 = 60/66)
- wetLevel: ~0.818 (-6 dB in [-60, +6] range: (-6 - (-60)) / 66 = 54/66)
- voice intervals: 0.5 (0 steps in [-24, +24] range)
- voice levels: ~0.909 (0 dB)
- voice pans: 0.5 (center)
- voice delays: 0.0 (0 ms)
- voice detunes: 0.5 (0 cents)

### 4. Effects Chain Integration (FR-001, FR-002, FR-003, FR-004, FR-005, FR-019, FR-020, FR-021)

**File**: `plugins/ruinae/src/engine/ruinae_effects_chain.h`

**Changes:**

a) **Add include**: `#include <krate/dsp/systems/harmonizer_engine.h>`

b) **Add member**: `HarmonizerEngine harmonizer_;`

c) **Add enable flag**: `bool harmonizerEnabled_ = false;`

d) **Add mono scratch buffer**: `std::vector<float> harmonizerMonoScratch_;` (pre-allocated in prepare)

e) **Modify `prepare()`**:
```cpp
// After reverb prepare, before targetLatencySamples_ calculation:

// Prepare harmonizer
harmonizer_.prepare(sampleRate, maxBlockSize);

// Query worst-case latency: set PhaseVocoder mode, get latency, reset mode
harmonizer_.setPitchShiftMode(PitchMode::PhaseVocoder);
size_t harmonizerLatency = harmonizer_.getLatencySamples();
harmonizer_.setPitchShiftMode(PitchMode::Simple); // Reset to default

// Combined worst-case latency: spectral delay + harmonizer PhaseVocoder
targetLatencySamples_ = spectralDelay_.getLatencySamples() + harmonizerLatency;

// Pre-allocate harmonizer mono scratch
harmonizerMonoScratch_.resize(maxBlockSize, 0.0f);
```

f) **Modify `reset()`**: Add `harmonizer_.reset();`

g) **Add harmonizer setters** (following the phaser/reverb pattern):
```cpp
void setHarmonizerEnabled(bool enabled) noexcept { harmonizerEnabled_ = enabled; }

// Global
void setHarmonizerHarmonyMode(int mode) noexcept {
    harmonizer_.setHarmonyMode(static_cast<HarmonyMode>(std::clamp(mode, 0, 1)));
}
void setHarmonizerKey(int key) noexcept {
    harmonizer_.setKey(std::clamp(key, 0, 11));
}
void setHarmonizerScale(int scale) noexcept {
    harmonizer_.setScale(static_cast<ScaleType>(std::clamp(scale, 0, 8)));
}
void setHarmonizerPitchShiftMode(int mode) noexcept {
    harmonizer_.setPitchShiftMode(static_cast<PitchMode>(std::clamp(mode, 0, 3)));
}
void setHarmonizerFormantPreserve(bool enable) noexcept {
    harmonizer_.setFormantPreserve(enable);
}
void setHarmonizerNumVoices(int count) noexcept {
    harmonizer_.setNumVoices(count);
}
void setHarmonizerDryLevel(float dB) noexcept {
    harmonizer_.setDryLevel(dB);
}
void setHarmonizerWetLevel(float dB) noexcept {
    harmonizer_.setWetLevel(dB);
}

// Per-voice
void setHarmonizerVoiceInterval(int voice, int steps) noexcept {
    harmonizer_.setVoiceInterval(voice, steps);
}
void setHarmonizerVoiceLevel(int voice, float dB) noexcept {
    harmonizer_.setVoiceLevel(voice, dB);
}
void setHarmonizerVoicePan(int voice, float pan) noexcept {
    harmonizer_.setVoicePan(voice, pan);
}
void setHarmonizerVoiceDelay(int voice, float ms) noexcept {
    harmonizer_.setVoiceDelay(voice, ms);
}
void setHarmonizerVoiceDetune(int voice, float cents) noexcept {
    harmonizer_.setVoiceDetune(voice, cents);
}
```

h) **Modify `processChunk()`** -- Insert harmonizer slot between delay and reverb:

```cpp
// After Slot 1 (Delay) processing and before Slot 2 (Reverb):

// ---------------------------------------------------------------
// Slot 1.5: Harmonizer (FR-001, FR-002, FR-021)
// ---------------------------------------------------------------
if (harmonizerEnabled_) {
    // FR-021: Sum stereo delay output to mono
    for (size_t i = 0; i < numSamples; ++i) {
        harmonizerMonoScratch_[i] = (left[i] + right[i]) * 0.5f;
    }
    // Process mono->stereo (engine handles dry/wet internally)
    harmonizer_.process(harmonizerMonoScratch_.data(), left, right, numSamples);
}
```

**Important**: The compensation delay logic remains BEFORE the harmonizer slot. The harmonizer processes the already-compensated delay output.

### 5. RuinaeEngine Pass-Through Setters (FR-013)

**File**: `plugins/ruinae/src/engine/ruinae_engine.h`

Add harmonizer setter pass-throughs following the phaser pattern:

```cpp
// Harmonizer
void setHarmonizerEnabled(bool enabled) noexcept { effectsChain_.setHarmonizerEnabled(enabled); }
void setHarmonizerHarmonyMode(int mode) noexcept { effectsChain_.setHarmonizerHarmonyMode(mode); }
void setHarmonizerKey(int key) noexcept { effectsChain_.setHarmonizerKey(key); }
void setHarmonizerScale(int scale) noexcept { effectsChain_.setHarmonizerScale(scale); }
void setHarmonizerPitchShiftMode(int mode) noexcept { effectsChain_.setHarmonizerPitchShiftMode(mode); }
void setHarmonizerFormantPreserve(bool enable) noexcept { effectsChain_.setHarmonizerFormantPreserve(enable); }
void setHarmonizerNumVoices(int count) noexcept { effectsChain_.setHarmonizerNumVoices(count); }
void setHarmonizerDryLevel(float dB) noexcept { effectsChain_.setHarmonizerDryLevel(dB); }
void setHarmonizerWetLevel(float dB) noexcept { effectsChain_.setHarmonizerWetLevel(dB); }
void setHarmonizerVoiceInterval(int voice, int steps) noexcept { effectsChain_.setHarmonizerVoiceInterval(voice, steps); }
void setHarmonizerVoiceLevel(int voice, float dB) noexcept { effectsChain_.setHarmonizerVoiceLevel(voice, dB); }
void setHarmonizerVoicePan(int voice, float pan) noexcept { effectsChain_.setHarmonizerVoicePan(voice, pan); }
void setHarmonizerVoiceDelay(int voice, float ms) noexcept { effectsChain_.setHarmonizerVoiceDelay(voice, ms); }
void setHarmonizerVoiceDetune(int voice, float cents) noexcept { effectsChain_.setHarmonizerVoiceDetune(voice, cents); }
```

### 6. Processor Integration (FR-012, FR-014, FR-015)

**File**: `plugins/ruinae/src/processor/processor.h`

```cpp
// Add include
#include "parameters/harmonizer_params.h"

// Add to parameter packs section:
std::atomic<bool> harmonizerEnabled_{false};
RuinaeHarmonizerParams harmonizerParams_;
```

**File**: `plugins/ruinae/src/processor/processor.cpp`

a) **processParameterChanges()** -- add harmonizer parameter handling:
```cpp
} else if (paramId == kHarmonizerEnabledId) {
    harmonizerEnabled_.store(value >= 0.5, std::memory_order_relaxed);
} else if (paramId >= kHarmonizerBaseId && paramId <= kHarmonizerEndId) {
    handleHarmonizerParamChange(harmonizerParams_, paramId, value);
}
```

b) **applyParamsToEngine()** -- add harmonizer parameter forwarding:
```cpp
// --- Harmonizer ---
engine_.setHarmonizerEnabled(harmonizerEnabled_.load(std::memory_order_relaxed));
engine_.setHarmonizerHarmonyMode(harmonizerParams_.harmonyMode.load(std::memory_order_relaxed));
engine_.setHarmonizerKey(harmonizerParams_.key.load(std::memory_order_relaxed));
engine_.setHarmonizerScale(harmonizerParams_.scale.load(std::memory_order_relaxed));
engine_.setHarmonizerPitchShiftMode(harmonizerParams_.pitchShiftMode.load(std::memory_order_relaxed));
engine_.setHarmonizerFormantPreserve(harmonizerParams_.formantPreserve.load(std::memory_order_relaxed));
engine_.setHarmonizerNumVoices(harmonizerParams_.numVoices.load(std::memory_order_relaxed));
engine_.setHarmonizerDryLevel(harmonizerParams_.dryLevelDb.load(std::memory_order_relaxed));
engine_.setHarmonizerWetLevel(harmonizerParams_.wetLevelDb.load(std::memory_order_relaxed));

for (int v = 0; v < 4; ++v) {
    engine_.setHarmonizerVoiceInterval(v, harmonizerParams_.voiceInterval[v].load(std::memory_order_relaxed));
    engine_.setHarmonizerVoiceLevel(v, harmonizerParams_.voiceLevelDb[v].load(std::memory_order_relaxed));
    engine_.setHarmonizerVoicePan(v, harmonizerParams_.voicePan[v].load(std::memory_order_relaxed));
    engine_.setHarmonizerVoiceDelay(v, harmonizerParams_.voiceDelayMs[v].load(std::memory_order_relaxed));
    engine_.setHarmonizerVoiceDetune(v, harmonizerParams_.voiceDetuneCents[v].load(std::memory_order_relaxed));
}
```

c) **State version**: Increment `kCurrentStateVersion` from 15 to 16.

**Serialization type rule** (matches codebase pattern in phaser_params.h, delay_params.h): use `writeInt32`/`readInt32` for all `std::atomic<int>` and `std::atomic<bool>` fields (harmonyMode, key, scale, pitchShiftMode, numVoices, formantPreserve, voiceIntervals); use `writeFloat`/`readFloat` for all `std::atomic<float>` fields (dryLevelDb, wetLevelDb, voice level/pan/delay/detune). Use `writeInt8`/`readInt8` only for the standalone harmonizerEnabled flag (matching existing FX enable pattern). See data-model.md E-004 for the corrected layout.

d) **getState()** -- add after v15 mod source params:
```cpp
// v16: Harmonizer params + enable flag
saveHarmonizerParams(harmonizerParams_, streamer);
streamer.writeInt8(harmonizerEnabled_.load(std::memory_order_relaxed) ? 1 : 0);
```

e) **setState()** -- add version-gated load:
```cpp
// v16: Harmonizer params + enable flag
if (version >= 16) {
    loadHarmonizerParams(harmonizerParams_, streamer);
    Steinberg::int8 i8 = 0;
    if (streamer.readInt8(i8))
        harmonizerEnabled_.store(i8 != 0, std::memory_order_relaxed);
}
```

### 7. Controller Integration (FR-016, FR-017, FR-018, FR-022, FR-023)

**File**: `plugins/ruinae/src/controller/controller.h`

Add to the UI state section:
```cpp
VSTGUI::CViewContainer* fxDetailHarmonizer_ = nullptr;
VSTGUI::CControl* fxExpandHarmonizerChevron_ = nullptr;

// Harmonizer voice row containers (for dimming based on NumVoices)
std::array<VSTGUI::CViewContainer*, 4> harmonizerVoiceRows_{};
```

**File**: `plugins/ruinae/src/controller/controller.cpp`

a) **initialize()** -- add parameter registration:
```cpp
registerHarmonizerParams(parameters);
```
And add `kHarmonizerEnabledId` to `registerFxEnableParams()` in `delay_params.h`:
```cpp
parameters.addParameter(STR16("Harmonizer Enabled"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kHarmonizerEnabledId);
```

b) **getParamStringByValue()** -- add harmonizer formatting:
```cpp
if (id >= kHarmonizerBaseId && id <= kHarmonizerEndId) {
    return formatHarmonizerParam(id, valueNormalized, string);
}
```

c) **setComponentState()** -- add v16 harmonizer state restore:
```cpp
// v16: Harmonizer params + enable flag
if (version >= 16) {
    loadHarmonizerParamsToController(streamer, setParam);
    Steinberg::int8 i8 = 0;
    if (streamer.readInt8(i8))
        setParam(kHarmonizerEnabledId, i8 != 0 ? 1.0 : 0.0);
}
```

d) **setParamNormalized()** -- add voice row dimming logic:
When `kHarmonizerNumVoicesId` changes, update visibility/enabled state of voice rows:
```cpp
if (tag == kHarmonizerNumVoicesId) {
    int numVoices = static_cast<int>(value * (kHarmonizerNumVoicesCount - 1) + 0.5);
    for (int i = 0; i < 4; ++i) {
        if (harmonizerVoiceRows_[i]) {
            harmonizerVoiceRows_[i]->setAlphaValue(i < numVoices ? 1.0f : 0.3f);
            // Disable/enable controls within the row
        }
    }
}
```

e) **verifyView()** -- wire harmonizer UI elements:
```cpp
// FX detail panel: HarmonizerDetail
if (*name == "HarmonizerDetail") {
    fxDetailHarmonizer_ = container;
    container->setVisible(false); // FR-023: collapsed by default
}
// Voice rows: HarmonizerVoice1, HarmonizerVoice2, etc.
if (*name == "HarmonizerVoice1") harmonizerVoiceRows_[0] = container;
if (*name == "HarmonizerVoice2") harmonizerVoiceRows_[1] = container;
if (*name == "HarmonizerVoice3") harmonizerVoiceRows_[2] = container;
if (*name == "HarmonizerVoice4") harmonizerVoiceRows_[3] = container;

// Capture harmonizer expand chevron
if (tag == kActionFxExpandHarmonizerTag) fxExpandHarmonizerChevron_ = ctrl;
```

f) **valueChanged()** -- handle harmonizer expand chevron:
```cpp
case kActionFxExpandHarmonizerTag: toggleFxDetail(3); return;
```

g) **toggleFxDetail()** -- expand panels array from 3 to 4:
```cpp
VSTGUI::CViewContainer* panels[] = {fxDetailDelay_, fxDetailReverb_, fxDetailPhaser_, fxDetailHarmonizer_};
for (int i = 0; i < 4; ++i) {
    if (panels[i]) {
        panels[i]->setVisible(i == panelIndex && opening);
    }
}
```

h) **willClose()** -- cleanup new pointers:
```cpp
fxDetailHarmonizer_ = nullptr;
fxExpandHarmonizerChevron_ = nullptr;
harmonizerVoiceRows_.fill(nullptr);
```

### 8. UI Layout (FR-016, FR-017, FR-022, FR-023)

**File**: `plugins/ruinae/resources/editor.uidesc`

Add a Harmonizer panel in the effects section, structured as:

```
Harmonizer Panel (CViewContainer, custom-view-name="HarmonizerPanel")
+-- Header Row
|   +-- Enable Toggle (COnOffButton, control-tag="Harmonizer Enabled")
|   +-- "HARMONIZER" Label (CTextLabel)
|   +-- Expand/Collapse Chevron (CControl, control-tag=kActionFxExpandHarmonizerTag)
+-- Detail Panel (CViewContainer, custom-view-name="HarmonizerDetail", initially hidden)
    +-- Global Controls Row
    |   +-- Harmony Mode dropdown (COptionMenu, control-tag=kHarmonizerHarmonyModeId)
    |   +-- Key dropdown (COptionMenu, control-tag=kHarmonizerKeyId)
    |   +-- Scale dropdown (COptionMenu, control-tag=kHarmonizerScaleId)
    |   +-- Pitch Mode dropdown (COptionMenu, control-tag=kHarmonizerPitchShiftModeId)
    |   +-- Formant Preserve toggle (COnOffButton, control-tag=kHarmonizerFormantPreserveId)
    |   +-- Num Voices dropdown (COptionMenu, control-tag=kHarmonizerNumVoicesId)
    |   +-- Dry Level knob (CKnobBase, control-tag=kHarmonizerDryLevelId)
    |   +-- Wet Level knob (CKnobBase, control-tag=kHarmonizerWetLevelId)
    +-- Voice 1 Row (CViewContainer, custom-view-name="HarmonizerVoice1")
    |   +-- "V1" label + Interval, Level, Pan, Delay, Detune controls
    +-- Voice 2 Row (CViewContainer, custom-view-name="HarmonizerVoice2")
    +-- Voice 3 Row (CViewContainer, custom-view-name="HarmonizerVoice3")
    +-- Voice 4 Row (CViewContainer, custom-view-name="HarmonizerVoice4")
```

**Voice row layout**: Each row contains 5 controls in a horizontal layout:
- Interval: COptionMenu (49-entry dropdown for -24 to +24) or a knob with label
- Level: CKnob/CSlider (-60 to +6 dB)
- Pan: CKnob (-1 to +1)
- Delay: CKnob (0-50 ms)
- Detune: CKnob (-50 to +50 cents)

**Panel position**: After the Phaser panel and before the Reverb panel in the vertical effects section layout. The exact coordinates depend on the current editor.uidesc layout and will be determined during implementation.

### 9. Latency Reporting (FR-019, FR-020)

The latency calculation happens in `RuinaeEffectsChain::prepare()`:

```
targetLatencySamples_ = spectralDelay_.getLatencySamples() + harmonizer PhaseVocoder latency
```

At 44.1kHz with default spectral delay FFT size 4096:
- Spectral delay latency: 4096 samples (the spectral delay reports this via `getLatencySamples()`)
- Harmonizer PhaseVocoder latency: 4096 + 1024 = 5120 samples
- Combined worst-case: 4096 + 5120 = 9216 samples

This combined value is held constant for the session (FR-020). The compensation delay lines need their capacity increased to accommodate the larger combined latency.

**Note**: Currently the Ruinae processor does NOT call `AudioEffect::setInitialDelay()` to report latency to the host. The latency is only used internally for per-delay-type compensation. This is existing behavior and is not changed by this spec. If host latency reporting is desired, `setInitialDelay(engine_.getLatencySamples())` should be called in `setupProcessing()`, but that is a separate concern.

### 10. State Version Migration

**Current**: v15 (mod source params)
**New**: v16 (harmonizer params + enable flag)

Old presets (v1-v15) load without harmonizer data; all harmonizer params keep their registration defaults (disabled, Chromatic mode, 0 voices, 0 dB dry, -6 dB wet). This is safe because:
- Harmonizer starts disabled (kHarmonizerEnabledId default = 0.0)
- No voices active by default (numVoices = 0)
- No audible change to existing presets

## Test Strategy

### Unit Tests (harmonizer_params_test.cpp)

1. **Parameter denormalization accuracy**: Verify `handleHarmonizerParamChange()` correctly converts normalized 0-1 values to plain values for all parameter types (dropdowns, dB ranges, bipolar ranges, etc.)
2. **State roundtrip**: Save and load all harmonizer params; verify exact value preservation
3. **Edge values**: Test min/max/center for all parameter ranges
4. **Per-voice ID routing**: Verify correct voice index mapping for all 4 voices

### Integration Tests (in existing test files)

1. **Effects chain signal flow**: Verify harmonizer slot processes audio between delay and reverb
2. **Enable/disable bypass**: Verify disabled harmonizer passes signal unchanged
3. **Stereo-to-mono summation**: Verify L+R summing before harmonizer input
4. **Latency reporting**: Verify combined worst-case latency value is correct
5. **State migration**: Verify old presets (v15) load cleanly with harmonizer defaults

### Manual/Pluginval Testing

1. Run pluginval at strictness level 5
2. Verify harmonizer UI panel appears correctly
3. Verify voice row dimming with NumVoices changes
4. Verify expand/collapse chevron behavior
5. Verify no clicks on enable/disable

## Complexity Tracking

No constitution violations identified. All design decisions follow established patterns.
