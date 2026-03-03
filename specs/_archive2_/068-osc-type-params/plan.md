# Implementation Plan: Oscillator Type-Specific Parameters

**Branch**: `068-osc-type-params` | **Date**: 2026-02-19 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/068-osc-type-params/spec.md`

## Summary

Extend the Ruinae synthesizer's `OscillatorSlot` virtual interface with a single `setParam(OscParam, float)` method to expose all oscillator type-specific parameters (32 unique DSP-parameter capabilities across 10 types, mapping to 30 new VST parameter IDs per oscillator because PolyBLEP and Wavetable share PM/FM IDs) through the voice architecture. Add 60 new VST parameter IDs (30 per oscillator x 2 oscillators), implement parameter routing from processor to voice to adapter, update UI templates with wired controls, and implement state persistence with backward compatibility.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (in-repo)
**Storage**: Binary state stream (IBStreamer), sequential field layout
**Testing**: Catch2 (via `dsp_tests` and `ruinae_tests` targets)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo (KrateDSP shared library + Ruinae plugin)
**Performance Goals**: < 0.5% CPU overhead for parameter routing at 44.1kHz, 8-voice polyphony
**Constraints**: Zero allocations in audio thread, noexcept, real-time safe
**Scale/Scope**: 60 new VST parameters, 10 adapter implementations, 20 UI templates

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Parameter changes flow Host -> Processor -> Engine -> Voice (correct direction) |
| II. Real-Time Safety | PASS | `setParam()` is noexcept, no allocation, no logging. All enum casts are compile-time safe |
| III. Modern C++ | PASS | C++20 `if constexpr`, `enum class`, `noexcept`, `constexpr` lookup tables |
| IV. SIMD & DSP Optimization | N/A | This feature is parameter routing, not DSP processing |
| V. VSTGUI Development | PASS | UIViewSwitchContainer with template-switch-control, control-tag bindings |
| VI. Cross-Platform | PASS | No platform-specific code; VSTGUI cross-platform abstractions only |
| VII. Build System | PASS | No new CMake targets needed; extends existing headers |
| VIII. Testing Discipline | PASS | Unit tests cover all adapter types; no existing tests broken |
| IX. Layered Architecture | PASS | OscParam at Layer 3, same as OscillatorSlot |
| X. DSP Processing | N/A | No new audio processing |
| XI. Performance Budgets | PASS | Virtual dispatch at block rate is ~5ns overhead |
| XII. Debugging Discipline | PASS | No framework pivots needed |
| XIII. Test-First | PASS | Failing tests first, then implementation |
| XIV. Living Architecture | PASS | Architecture docs updated after implementation |
| XV. ODR Prevention | PASS | OscParam enum is new (grep confirmed not found) |
| XVI. Honest Completion | PASS | Compliance table filled from actual code/test output |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**: OscParam (enum only, no class)

| Planned Type | Search Result | Action |
|--------------|---------------|--------|
| `OscParam` | Not found anywhere in codebase | Create New |

**Utility Functions to be created**: None (only extension of existing functions)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OscillatorSlot | `dsp/include/krate/dsp/systems/oscillator_slot.h` | 3 | Extended with `setParam()` |
| OscillatorAdapter<OscT> | `dsp/include/krate/dsp/systems/oscillator_adapters.h` | 3 | Implement `setParam()` override |
| SelectableOscillator | `dsp/include/krate/dsp/systems/selectable_oscillator.h` | 3 | Add `setParam()` forwarding |
| createDropdownParameter | `plugins/ruinae/src/controller/parameter_helpers.h` | Plugin | Reuse for all new dropdown registrations |
| createDropdownParameterWithDefault | `plugins/ruinae/src/controller/parameter_helpers.h` | Plugin | Reuse for dropdowns with non-zero defaults |
| OscAParams struct | `plugins/ruinae/src/parameters/osc_a_params.h` | Plugin | Extended with 30 new atomic fields |
| OscBParams struct | `plugins/ruinae/src/parameters/osc_b_params.h` | Plugin | Extended with 30 new atomic fields |
| dropdown_mappings.h | `plugins/ruinae/src/parameters/dropdown_mappings.h` | Plugin | Add new dropdown string arrays |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/systems/oscillator_types.h` - OscParam not found
- [x] `dsp/include/krate/dsp/systems/oscillator_slot.h` - No `setParam` exists
- [x] `dsp/include/krate/dsp/systems/oscillator_adapters.h` - No `setParam` exists
- [x] `dsp/include/krate/dsp/systems/selectable_oscillator.h` - No `setParam` exists
- [x] `plugins/ruinae/src/plugin_ids.h` - IDs 110-149 and 210-249 are free

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new type is `OscParam` (an enum, not a class). Grep confirms no existing `OscParam` in the codebase. All other changes extend existing types. No risk of duplicate definitions.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PolyBlepOscillator | setWaveform | `inline void setWaveform(OscWaveform waveform) noexcept` | Yes |
| PolyBlepOscillator | setPulseWidth | `inline void setPulseWidth(float width) noexcept` | Yes |
| PolyBlepOscillator | setPhaseModulation | `inline void setPhaseModulation(float radians) noexcept` | Yes |
| PolyBlepOscillator | setFrequencyModulation | `inline void setFrequencyModulation(float hz) noexcept` | Yes |
| PhaseDistortionOscillator | setWaveform | `void setWaveform(PDWaveform waveform) noexcept` | Yes |
| PhaseDistortionOscillator | setDistortion | `void setDistortion(float amount) noexcept` | Yes |
| SyncOscillator | setSlaveFrequency | `inline void setSlaveFrequency(float hz) noexcept` | Yes |
| SyncOscillator | setSlaveWaveform | `inline void setSlaveWaveform(OscWaveform waveform) noexcept` | Yes |
| SyncOscillator | setSyncMode | `inline void setSyncMode(SyncMode mode) noexcept` | Yes |
| SyncOscillator | setSyncAmount | `inline void setSyncAmount(float amount) noexcept` | Yes |
| SyncOscillator | setSlavePulseWidth | `inline void setSlavePulseWidth(float width) noexcept` | Yes |
| AdditiveOscillator | setNumPartials | `void setNumPartials(size_t count) noexcept` | Yes |
| AdditiveOscillator | setSpectralTilt | `void setSpectralTilt(float tiltDb) noexcept` | Yes |
| AdditiveOscillator | setInharmonicity | `void setInharmonicity(float B) noexcept` | Yes |
| ChaosOscillator | setAttractor | `void setAttractor(ChaosAttractor type) noexcept` | Yes |
| ChaosOscillator | setChaos | `void setChaos(float amount) noexcept` | Yes |
| ChaosOscillator | setCoupling | `void setCoupling(float amount) noexcept` | Yes |
| ChaosOscillator | setOutput | `void setOutput(size_t axis) noexcept` | Yes |
| ParticleOscillator | setFrequencyScatter | `void setFrequencyScatter(float semitones) noexcept` | Yes |
| ParticleOscillator | setDensity | `void setDensity(float particles) noexcept` | Yes |
| ParticleOscillator | setLifetime | `void setLifetime(float ms) noexcept` | Yes |
| ParticleOscillator | setSpawnMode | `void setSpawnMode(SpawnMode mode) noexcept` | Yes |
| ParticleOscillator | setEnvelopeType | `void setEnvelopeType(GrainEnvelopeType type) noexcept` | Yes |
| ParticleOscillator | setDriftAmount | `void setDriftAmount(float amount) noexcept` | Yes |
| FormantOscillator | setVowel | `void setVowel(Vowel vowel) noexcept` | Yes |
| FormantOscillator | setMorphPosition | `void setMorphPosition(float position) noexcept` | Yes |
| SpectralFreezeOscillator | setPitchShift | `void setPitchShift(float semitones) noexcept` | Yes |
| SpectralFreezeOscillator | setSpectralTilt | `void setSpectralTilt(float dbPerOctave) noexcept` | Yes |
| SpectralFreezeOscillator | setFormantShift | `void setFormantShift(float semitones) noexcept` | Yes |
| NoiseOscillator | setColor | `void setColor(NoiseColor color) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator API
- [x] `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` - WavetableOscillator API
- [x] `dsp/include/krate/dsp/primitives/noise_oscillator.h` - NoiseOscillator API
- [x] `dsp/include/krate/dsp/processors/phase_distortion_oscillator.h` - PD API + PDWaveform enum
- [x] `dsp/include/krate/dsp/processors/sync_oscillator.h` - Sync API + SyncMode enum
- [x] `dsp/include/krate/dsp/processors/additive_oscillator.h` - Additive API
- [x] `dsp/include/krate/dsp/processors/chaos_oscillator.h` - Chaos API + ChaosAttractor enum
- [x] `dsp/include/krate/dsp/processors/particle_oscillator.h` - Particle API + SpawnMode enum
- [x] `dsp/include/krate/dsp/processors/formant_oscillator.h` - Formant API
- [x] `dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h` - SpectralFreeze API
- [x] `dsp/include/krate/dsp/core/filter_tables.h` - Vowel enum
- [x] `dsp/include/krate/dsp/core/grain_envelope.h` - GrainEnvelopeType enum
- [x] `dsp/include/krate/dsp/core/pattern_freeze_types.h` - NoiseColor enum (8 values, expose 6)
- [x] `dsp/include/krate/dsp/systems/oscillator_slot.h` - OscillatorSlot interface
- [x] `dsp/include/krate/dsp/systems/oscillator_adapters.h` - OscillatorAdapter template
- [x] `dsp/include/krate/dsp/systems/selectable_oscillator.h` - SelectableOscillator
- [x] `dsp/include/krate/dsp/systems/oscillator_types.h` - OscType, PhaseMode enums
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs
- [x] `plugins/ruinae/src/parameters/osc_a_params.h` - OscAParams struct
- [x] `plugins/ruinae/src/parameters/osc_b_params.h` - OscBParams struct
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - Existing dropdown arrays
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter
- [x] `plugins/ruinae/src/engine/ruinae_voice.h` - RuinaeVoice class
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - RuinaeEngine class
- [x] `plugins/ruinae/src/processor/processor.cpp` - applyParamsToEngine pattern
- [x] `plugins/ruinae/resources/editor.uidesc` - UIViewSwitchContainer pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SyncOscillator | `setSlaveFrequency` takes absolute Hz, not ratio | Must multiply `masterFreq * ratio` |
| SyncOscillator | Adapter currently hardcodes `hz * 2.0f` in `setFrequency()` | Must change to `hz * slaveRatio_` |
| AdditiveOscillator | `setNumPartials` takes `size_t`, not `int` | Cast with `static_cast<size_t>(value)` |
| ChaosOscillator | `setOutput` takes `size_t`, not `int` | Cast with `static_cast<size_t>(value)` |
| NoiseColor enum | Has 8 values (includes Velvet, RadioStatic) | Clamp dropdown to 6 (White through Grey) |
| PolyBlepOscillator | `setPhaseModulation` parameter name is `radians` | Value range is -1.0 to +1.0 (maps to radians internally) |
| OscWaveform | 5 values: Sine=0, Sawtooth=1, Square=2, Pulse=3, Triangle=4 | Pulse is index 3, not 4 |
| State persistence | Fields read sequentially, old presets have fewer fields | `readFloat()` returns false for missing data |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| kParamIdToOscParam lookup table | Plugin-specific, only used by Ruinae parameter handling |

**Decision**: No new Layer 0 utilities needed. This feature is primarily parameter routing, not new DSP processing.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | No audio processing in this feature |
| **Data parallelism width** | N/A | Parameter routing is scalar |
| **Branch density in inner loop** | N/A | No inner loops |
| **Dominant operations** | Memory writes | Atomic stores + virtual dispatch |
| **Current CPU budget vs expected usage** | < 0.5% | Virtual dispatch ~5ns/call at block rate |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: This feature adds no new audio processing algorithms. It is purely parameter routing infrastructure (atomic reads, virtual dispatch, setter calls). SIMD has no applicability to parameter forwarding.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 (Systems) + Plugin Layer

### Sibling Features Analysis

**Related features at same layer**:
- Future wavetable import: would add new `OscParam` values for wavetable selection
- Future per-partial additive control: would extend `OscParam` with partial-level params
- Future SpectralFreeze live capture: would add freeze trigger through `OscParam`
- Future new oscillator types: would add new `OscParam` groups

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `OscParam` enum | HIGH | Any future oscillator parameter | Keep extensible with gaps |
| `setParam()` virtual method | HIGH | Any future oscillator parameter | Part of core interface |
| `kParamIdToOscParam` lookup | MEDIUM | Only if new osc params added | Keep in osc_a_params.h |
| UI template pattern | HIGH | New oscillator types | Follow same naming: `OscA_{Type}` |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Single `setParam()` method vs per-param virtual methods | Extensible without breaking ABI; one method handles all future params |
| Gaps of 10 in OscParam enum | Allows adding params per-type without renumbering |
| Lookup table vs switch for ID-to-OscParam | O(1) compile-time constant; cleaner than 30-case switch |

## Project Structure

### Documentation (this feature)

```text
specs/068-osc-type-params/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 research output
├── data-model.md        # Phase 1 data model
├── quickstart.md        # Phase 1 quickstart guide
├── contracts/           # Phase 1 API contracts
│   ├── oscillator-slot-interface.md
│   └── parameter-routing.md
└── tasks.md             # Phase 2 task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/systems/
├── oscillator_types.h        # MODIFIED: Add OscParam enum
├── oscillator_slot.h         # MODIFIED: Add virtual setParam()
├── oscillator_adapters.h     # MODIFIED: Implement setParam() override, fix Sync ratio
└── selectable_oscillator.h   # MODIFIED: Add setParam() forwarding

plugins/ruinae/src/
├── plugin_ids.h              # MODIFIED: Add 60 new parameter IDs
├── parameters/
│   ├── dropdown_mappings.h   # MODIFIED: Add 9 new dropdown string arrays
│   ├── osc_a_params.h        # MODIFIED: Extend struct + handler + register + format + save/load
│   └── osc_b_params.h        # MODIFIED: Mirror of osc_a changes
├── engine/
│   ├── ruinae_voice.h        # MODIFIED: Add setOscAParam()/setOscBParam()
│   └── ruinae_engine.h       # MODIFIED: Add setOscAParam()/setOscBParam()
├── processor/
│   └── processor.cpp         # MODIFIED: Extend applyParamsToEngine()
└── controller/
    └── controller.cpp        # MODIFIED: PW disable sub-controller (FR-016)

plugins/ruinae/resources/
└── editor.uidesc             # MODIFIED: Replace placeholder templates + add control-tags

dsp/tests/unit/systems/
└── selectable_oscillator_test.cpp  # MODIFIED: Add setParam tests

plugins/ruinae/tests/
└── (new or existing test files for parameter routing)
```

**Structure Decision**: All changes are modifications to existing files. No new source files are created except possibly test files. This follows the monorepo structure with DSP at `dsp/` and plugin at `plugins/ruinae/`.

## Complexity Tracking

No constitution violations. No complexity exceptions needed.
