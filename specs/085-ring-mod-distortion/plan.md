# Implementation Plan: Ring Modulator Distortion

**Branch**: `085-ring-mod-distortion` | **Date**: 2026-03-01 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/085-ring-mod-distortion/spec.md`

## Summary

Add a Ring Modulator as a new distortion type (value 6) in the Ruinae synthesizer plugin. The core DSP is a Layer 2 processor (`RingModulator`) that multiplies the voice signal by an internal carrier oscillator to produce sum/difference frequency sidebands. The carrier supports 5 waveforms: Sine (Gordon-Smith phasor), Triangle/Sawtooth/Square (PolyBLEP), and Noise (NoiseOscillator). Two frequency modes (Free Hz and Note Track with ratio) are supported. A one-pole smoother (~5ms) prevents zipper noise on frequency changes. The feature integrates into the existing `SelectableDistortion` infrastructure with 5 new parameters (IDs 560-564) and full state persistence with backward compatibility.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: KrateDSP (Layer 0-1 primitives: PolyBlepOscillator, NoiseOscillator, OnePoleSmoother, math_constants, db_utils), VST3 SDK, VSTGUI
**Storage**: VST3 binary state stream (IBStreamer) for preset persistence
**Testing**: Catch2 (dsp_tests target for DSP, ruinae_tests for plugin integration)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: <0.3% CPU per voice at 44.1 kHz (SC-002), <5% total plugin (Constitution XI)
**Constraints**: Zero allocations in audio thread (Constitution II), Layer 2 can only depend on Layer 0-1 (Constitution IX)
**Scale/Scope**: 1 new header-only DSP class, 1 new test file, ~6 modified plugin files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor handles audio-thread parameter storage (distortionParams_ atomics)
- [x] Controller handles UI parameter registration (distortion_params.h)
- [x] No cross-inclusion between processor and controller

**Principle II (Real-Time Audio Thread Safety):**
- [x] RingModulator::processBlock() is noexcept, zero-allocation after prepare()
- [x] All sub-components (PolyBlepOscillator, NoiseOscillator, OnePoleSmoother) are RT-safe
- [x] Gordon-Smith phasor is pure arithmetic (2 muls + 2 adds per sample)
- [x] No std::sin/cos in audio path (only in prepare/reset for initialization)

**Principle III (Modern C++):**
- [x] Header-only class with value semantics where possible
- [x] constexpr constants, [[nodiscard]] on queries
- [x] No raw new/delete in RingModulator (all value-type members)

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analyzed (NOT BENEFICIAL -- see section below)
- [x] Scalar-first workflow: no SIMD phase planned

**Principle VI (Cross-Platform):**
- [x] No platform-specific APIs
- [x] PolyBLEP and Gordon-Smith are pure C++ arithmetic

**Principle VIII (Testing Discipline):**
- [x] DSP unit tests for RingModulator class (all waveforms, modes, edge cases)
- [x] Plugin integration tests (state roundtrip, backward compat)
- [x] Tests before implementation (TDD)

**Principle IX (Layered Architecture):**
- [x] RingModulator at Layer 2, depends only on Layer 0 (math_constants, db_utils) and Layer 1 (polyblep_oscillator, noise_oscillator, smoother)
- [x] No upward layer violations

**Principle X (DSP Processing Constraints):**
- [x] No oversampling needed (ring modulation is linear multiplication, not waveshaping)
- [x] DC blocking already exists post-distortion in voice chain (dcBlocker_)

**Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Compliance table in spec.md will be filled with file paths, line numbers, test names, and measured values

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `RingModulator`, `RingModCarrierWaveform` (enum), `RingModFreqMode` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `RingModulator` | `grep -r "class RingModulator" dsp/ plugins/` | No (only in spec.md and archived freq-shifter references) | Create New |
| `RingModCarrierWaveform` | `grep -r "RingModCarrierWaveform" dsp/ plugins/` | No | Create New |
| `RingModFreqMode` | `grep -r "RingModFreqMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all functionality composed from existing primitives)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PolyBlepOscillator` | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | Carrier for Triangle/Sawtooth/Square waveforms |
| `NoiseOscillator` | `dsp/include/krate/dsp/primitives/noise_oscillator.h` | 1 | Carrier for Noise waveform |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | 5ms frequency smoothing |
| `OscWaveform` enum | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | Waveform mapping (Triangle/Saw/Square) |
| `NoiseColor` enum | `dsp/include/krate/dsp/core/pattern_freeze_types.h` | 0 | White noise for noise carrier |
| `kTwoPi`, `kPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Phase increment calculation |
| `detail::flushDenormal()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal flushing in process loop |
| `detail::isNaN()`, `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input validation |
| `createDropdownParameter()` | `plugins/ruinae/src/controller/parameter_helpers.h` | plugin | Dropdown parameter registration |
| `createDropdownParameterWithDefault()` | `plugins/ruinae/src/controller/parameter_helpers.h` | plugin | Dropdown with non-zero default |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no ring_modulator exists; ring_saturation.h is unrelated)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no ring_modulator exists; frequency_shifter.h uses similar Gordon-Smith pattern)
- [x] `specs/_architecture_/` - Component inventory (no RingModulator documented)
- [x] `plugins/ruinae/src/ruinae_types.h` - RuinaeDistortionType enum (will be extended)
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs 560-564 are unallocated

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (`RingModulator`, `RingModCarrierWaveform`, `RingModFreqMode`) are unique and not found anywhere in the codebase. The `ring_saturation.h` at Layer 1 is a completely different concept (ring-buffer-based saturation) with a different class name (`RingSaturation`). No namespace collision risk.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `PolyBlepOscillator` | `prepare` | `inline void prepare(double sampleRate) noexcept` | Yes |
| `PolyBlepOscillator` | `reset` | `inline void reset() noexcept` | Yes |
| `PolyBlepOscillator` | `setFrequency` | `inline void setFrequency(float hz) noexcept` | Yes |
| `PolyBlepOscillator` | `setWaveform` | `inline void setWaveform(OscWaveform waveform) noexcept` | Yes |
| `PolyBlepOscillator` | `process` | `[[nodiscard]] inline float process() noexcept` | Yes |
| `NoiseOscillator` | `prepare` | `void prepare(double sampleRate) noexcept` | Yes |
| `NoiseOscillator` | `reset` | `void reset() noexcept` | Yes |
| `NoiseOscillator` | `setColor` | `void setColor(NoiseColor color) noexcept` | Yes |
| `NoiseOscillator` | `process` | `[[nodiscard]] float process() noexcept` | Yes |
| `OnePoleSmoother` | `configure` | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| `OnePoleSmoother` | `setTarget` | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| `OnePoleSmoother` | `snapTo` | `void snapTo(float value) noexcept` | Yes |
| `OnePoleSmoother` | `process` | `[[nodiscard]] float process() noexcept` | Yes |
| `OnePoleSmoother` | `reset` | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator class
- [x] `dsp/include/krate/dsp/primitives/noise_oscillator.h` - NoiseOscillator class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi constants
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf utilities
- [x] `dsp/include/krate/dsp/processors/frequency_shifter.h` - Gordon-Smith oscillator pattern reference
- [x] `plugins/ruinae/src/ruinae_types.h` - RuinaeDistortionType enum
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID allocation
- [x] `plugins/ruinae/src/parameters/distortion_params.h` - Distortion parameter infrastructure
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - Dropdown constants
- [x] `plugins/ruinae/src/engine/ruinae_voice.h` - Voice distortion integration
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - Engine parameter forwarding
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `OnePoleSmoother` | `snapTo(value)` sets both current and target | Use `snapTo()` for init, `setTarget()` for smooth transition |
| `OnePoleSmoother` | `configure()` does NOT reset current value | Call `snapTo()` after `configure()` if init needed |
| `PolyBlepOscillator` | `prepare()` resets frequency to 440 Hz | Must call `setFrequency()` after `prepare()` |
| `PolyBlepOscillator` | `setWaveform()` to/from Triangle clears integrator | Waveform switches during processing are handled gracefully |
| `NoiseOscillator` | `setColor()` resets filter state but preserves PRNG | Calling `setColor(White)` after prepare is fine |
| `distortionParams_` | Type is stored as `int`, maps via `value * (count-1) + 0.5` | Follow existing pattern in `handleDistortionParamChange` |
| `kDistortionTypeCount` | Derived from `RuinaeDistortionType::NumTypes` | Changing enum automatically updates dropdown count |
| State save/load | Optional reads at end of stream fail silently | Ring mod fields at end, old presets get defaults |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| Gordon-Smith sine oscillator | Shared pattern with FrequencyShifter | `dsp/include/krate/dsp/core/gordon_smith_osc.h` | FrequencyShifter, RingModulator |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Gordon-Smith sine oscillator | Only 2 consumers (FrequencyShifter + RingModulator). Spec says extract at 3rd user. Inline implementation is ~10 lines, trivial to duplicate. |
| `computeEffectiveFrequency()` | Private helper, specific to ring mod freq mode logic |

**Decision**: No Layer 0 extraction. Gordon-Smith inline pattern duplicated (2 users; extract deferred to 3rd user per spec). All other utilities are class-specific.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Gordon-Smith phasor: s[n] depends on s[n-1], c[n-1]. Serial dependency per sample. |
| **Data parallelism width** | 1 (mono) or 2 (stereo) | Each voice is independent but only 1-2 channels. Cross-voice SIMD requires SoA layout redesign. |
| **Branch density in inner loop** | LOW | Waveform switch is outside the sample loop. Inner loop is pure arithmetic. |
| **Dominant operations** | Arithmetic (multiply, add) | Gordon-Smith: 2 muls + 2 adds. PolyBLEP: slightly more. Ring mod multiply: 1 mul. |
| **Current CPU budget vs expected usage** | 0.3% budget vs ~0.05% expected | Gordon-Smith + 1 multiply = ~7 ops/sample. At 44100 Hz, ~309K ops/sec. Well under budget. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The Gordon-Smith phasor creates a serial sample-to-sample dependency that prevents SIMD parallelization across samples. The parallelism width (1-2 channels) is too narrow for efficient SIMD lane utilization. Most importantly, the expected CPU usage (~0.05%) is far below the 0.3% budget, making optimization unnecessary.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Gordon-Smith phasor (already chosen) | ~30% vs std::sin() per sample | LOW | YES (already the design) |
| Skip processing when amplitude=0 | 100% savings when idle | LOW | YES |
| Branch-free waveform dispatch | Marginal (branch outside loop) | LOW | NO (switch outside loop is fine) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from spec Forward Reusability section):
- Potential Amplitude Modulation effect (AM = ring mod + DC bias on carrier)
- Potential Vocoder effect (ring modulation per frequency band)
- FrequencyShifter (shares Gordon-Smith pattern)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `RingModulator` class | HIGH | AM effect (add bias param), Vocoder bands | Keep local -- AM would need a `bias` parameter addition |
| `RingModCarrierWaveform` enum | MEDIUM | AM effect | Keep in ring_modulator.h for now |
| `RingModFreqMode` enum | MEDIUM | Any pitch-aware modulation effect | Keep local -- pattern can be copied |
| Gordon-Smith sine oscillator | HIGH | FrequencyShifter, potential AM, tremolo | Keep inline; extract to Layer 1 at 3rd user |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for distortion types | Distortion types have wildly different APIs; shared interface would be lowest-common-denominator |
| Keep Gordon-Smith inline | 2 users (FreqShifter + RingMod); extract threshold is 3 |
| Separate enum from OscWaveform | RingModCarrierWaveform excludes Pulse, adds Noise; different semantic from oscillator waveforms |

### Review Trigger

After implementing **Amplitude Modulation effect** (if it happens), review this section:
- [ ] Does AM need `RingModulator` with a bias parameter? Consider adding bias to ring_modulator.h
- [ ] Does AM use Gordon-Smith sine? If yes, extract to Layer 1 shared utility (3rd user threshold met)
- [ ] Any duplicated carrier oscillator code? Consider shared carrier abstraction

## Project Structure

### Documentation (this feature)

```text
specs/085-ring-mod-distortion/
+-- plan.md              # This file
+-- research.md          # Phase 0 research findings
+-- data-model.md        # Entity definitions
+-- quickstart.md        # Build/test quick reference
+-- contracts/           # API contracts
|   +-- ring_modulator_api.h
+-- spec.md              # Feature specification
+-- checklists/          # Implementation checklists
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/processors/
|   +-- ring_modulator.h            # NEW: RingModulator Layer 2 processor (header-only)
+-- tests/
    +-- unit/processors/
        +-- ring_modulator_test.cpp # NEW: Unit tests

plugins/ruinae/
+-- src/
|   +-- ruinae_types.h              # MODIFIED: Add RingModulator enum value
|   +-- plugin_ids.h                # MODIFIED: Add 5 param IDs (560-564)
|   +-- parameters/
|   |   +-- distortion_params.h     # MODIFIED: Ring mod fields, handler, register, save/load, format
|   |   +-- dropdown_mappings.h     # MODIFIED: Add "Ring Mod" to distortion strings
|   +-- engine/
|   |   +-- ruinae_voice.h          # MODIFIED: Ring mod instance + distortion integration
|   |   +-- ruinae_engine.h         # MODIFIED: Ring mod parameter forwarding
|   +-- processor/
|       +-- processor.cpp           # MODIFIED: Ring mod parameter dispatching
+-- tests/
    +-- unit/
        +-- state_roundtrip_test.cpp # MODIFIED: Verify ring mod params round-trip

dsp/tests/CMakeLists.txt             # MODIFIED: Add ring_modulator_test.cpp
specs/_architecture_/layer-2-processors.md # MODIFIED: Document RingModulator
```

**Structure Decision**: Standard monorepo structure. New DSP class in `dsp/include/krate/dsp/processors/` (Layer 2). Plugin integration across existing Ruinae files following established patterns.

## Complexity Tracking

No constitution violations. All principles satisfied.
