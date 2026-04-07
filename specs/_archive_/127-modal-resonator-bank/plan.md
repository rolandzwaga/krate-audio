# Implementation Plan: Modal Resonator Bank for Physical Modelling

**Branch**: `127-modal-resonator-bank` | **Date**: 2026-03-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/127-modal-resonator-bank/spec.md`

## Summary

Add a modal resonator bank (Layer 2 processor) to the Innexus plugin that transforms the analyzed residual signal into physically resonant textures. The resonator uses the Gordon-Smith damped coupled-form topology with SoA/SIMD layout, Chaigne-Lambourg frequency-dependent damping, stiff-string/scatter inharmonicity warping, and transient emphasis excitation conditioning. A `PhysicalModelMixer` crossfades between the existing additive path (harmonic + residual) and the physical path (harmonic + modal resonator output), with mix=0 producing bit-exact backwards-compatible output.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang, GCC)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, Google Highway (SIMD), KrateDSP (shared DSP library)
**Storage**: VST3 state stream (IBStreamer flat format), presets
**Testing**: Catch2 (unit tests: dsp_tests, innexus_tests)
**Target Platform**: Windows 10/11, macOS 11+, Linux (cross-platform)
**Project Type**: Monorepo VST3 plugin (Innexus)
**Performance Goals**: 96 modes x 8 voices at 44.1 kHz < 5% single core (SC-002b); no XRuns at 128-sample buffer (SC-002a)
**Constraints**: Zero allocations in audio thread; no locks/exceptions in audio path; FTZ/DAZ assumed enabled
**Scale/Scope**: 2 new source files, 5 modified files, 5 new parameters, ~400 lines new DSP code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | New params follow existing pattern: atomic in processor, registered in controller |
| II. Real-Time Audio Thread Safety | PASS | No allocations in audio path. SoA arrays are fixed-size in struct. `prepare()` called in `setActive()`. |
| III. Modern C++ Standards | PASS | RAII, constexpr constants, std::array, no raw new/delete |
| IV. SIMD & DSP Optimization | PASS | SoA layout, 32-byte alignment, Highway for SIMD. Scalar-first per constitution (SIMD is Phase 2). |
| V. VSTGUI Development | N/A | No UI changes in this spec (parameters only, no custom views) |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. Highway handles ISA selection. |
| VII. Project Structure & Build System | PASS | Layer 2 processor in `dsp/processors/`, plugin DSP in `plugins/innexus/src/dsp/` |
| VIII. Testing Discipline | PASS | Test-first development. Unit tests for DSP, integration tests for voice pipeline. |
| IX. Layered DSP Architecture | PASS | `ModalResonatorBank` at Layer 2. Uses Layer 0 (dsp_utils.h softClip) and Layer 1 (smoother concepts). No upward deps. |
| X. DSP Processing Constraints | PASS | No saturation/oversampling needed. Output safety limiter via softClip. |
| XI. Performance Budgets | PASS | Target: < 5% single core. 96 modes x 8 voices = 768 resonators, each ~5 FLOPs/sample = ~3840 FLOPs/sample total. Well within budget at 44.1 kHz. |
| XIII. Test-First Development | PASS | Tests written before implementation per canonical todo list. |
| XIV. Living Architecture Documentation | PASS | Will update `specs/_architecture_/layer-2-processors.md` and `innexus-plugin.md`. |
| XV. Pre-Implementation Research (ODR) | PASS | See ODR section below. No conflicts. |
| XVI. Honest Completion | PASS | Compliance table in spec.md will be filled with actual evidence. |

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

**Classes/Structs to be created**: `ModalResonatorBank`, `PhysicalModelMixer`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `ModalResonatorBank` | `grep -r "class ModalResonatorBank\|struct ModalResonatorBank" dsp/ plugins/` | No | Create New (different from existing `ModalResonator`) |
| `PhysicalModelMixer` | `grep -r "class PhysicalModelMixer\|struct PhysicalModelMixer" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (reusing existing `softClip`)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `softClip` | `grep -r "softClip" dsp/` | Yes | `dsp/include/krate/dsp/core/dsp_utils.h:102` | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `softClip` | `dsp/include/krate/dsp/core/dsp_utils.h` | 0 | Output safety limiter (FR-010) |
| `HarmonicFrame` | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Read partial frequencies/amplitudes for mode tuning |
| `Partial` | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Per-partial data struct |
| `kMaxPartials` | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Constant (96) matching kMaxModes |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Reference for smoothing coefficient formula |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (found `ModalResonator`, `ResonatorBank` -- different from planned `ModalResonatorBank`)
- [x] `specs/_architecture_/` - Component inventory
- [x] `plugins/innexus/src/dsp/` - Innexus plugin-local DSP (no conflicts)
- [x] `plugins/innexus/src/processor/` - Voice struct, processor (will extend)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The new `ModalResonatorBank` class name is distinct from existing `ModalResonator` (32-mode biquad) and `ResonatorBank` (16 bandpass filters). Both existing classes have different topologies and APIs. `PhysicalModelMixer` is a new name with no conflicts. All planned types are unique.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `HarmonicFrame` | `partials` | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| `HarmonicFrame` | `numPartials` | `int numPartials = 0` | Yes |
| `Partial` | `frequency` | `float frequency = 0.0f` | Yes |
| `Partial` | `amplitude` | `float amplitude = 0.0f` | Yes |
| `softClip` | function | `inline float softClip(float sample) noexcept` | Yes |
| `OnePoleSmoother` | `configure` | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| `OnePoleSmoother` | `snapTo` | `void snapTo(float value) noexcept` | Yes |
| `ResidualSynthesizer` | `process` | `float process()` (returns single sample) | Yes |
| `InnexusVoice` | `prepare` | `void prepare(double sampleRate)` | Yes |
| `InnexusVoice` | `reset` | `void reset()` | Yes |
| `InnexusVoice` | `oscillatorBank` | `Krate::DSP::HarmonicOscillatorBank oscillatorBank` | Yes |
| `InnexusVoice` | `residualSynth` | `Krate::DSP::ResidualSynthesizer residualSynth` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame, Partial, kMaxPartials
- [x] `dsp/include/krate/dsp/core/dsp_utils.h` - softClip function
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/residual_synthesizer.h` - ResidualSynthesizer class
- [x] `plugins/innexus/src/processor/innexus_voice.h` - InnexusVoice struct
- [x] `plugins/innexus/src/processor/processor.h` - Processor class (fields, methods)
- [x] `plugins/innexus/src/processor/processor.cpp` - Voice render loop (L1577-1689)
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter ID allocation
- [x] `plugins/innexus/src/processor/processor_state.cpp` - State save/load pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `HarmonicFrame` | `numPartials` is `int`, not `size_t` | Cast when using as array index |
| `Partial.amplitude` | Linear amplitude, not dB | Compare vs `kAmplitudeThresholdLinear = 0.0001f` |
| `softClip` | Normalizes to [-1, 1] with saturation at +/-3 | Scale input by threshold, apply, scale back |
| `ResidualSynthesizer::process()` | Returns `float` (single sample) | Direct use as excitation input |
| State persistence | Uses flat format, no field names | New params appended at end, read with optional fallback |
| Parameter storage | All params stored as normalized `std::atomic<float>` | Denormalize in audio thread |
| `OnePoleSmoother` | Uses `kDefaultSmoothingTimeMs` by default | Must call `configure(2.0f, sampleRate)` for 2ms |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `applyTransientEmphasis` | Voicing-specific, uses class state (envelopeState_), only 1 consumer |
| `computeModeCoefficients` | Complex internal helper, tightly coupled to SoA layout |
| `flushSilentModes` | Operates on internal state arrays directly |

**Decision**: No new Layer 0 utilities needed. All new code is specific to the modal resonator bank or Innexus plugin.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability for all DSP features.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO (between modes) | Each mode is fully independent. Within a single mode, s_new depends on s,c -- serial per mode but parallelizable ACROSS modes. |
| **Data parallelism width** | 96 modes | 96 / 8 (AVX2) = 12 iterations. Excellent lane utilization. |
| **Branch density in inner loop** | LOW | Only active/inactive check, handled outside inner loop via mode culling. |
| **Dominant operations** | arithmetic (mul, add) | ~5 FLOPs per mode per sample (R*(s+eps*c)+gain*input, R*(c-eps*s_new)) |
| **Current CPU budget vs expected usage** | 5% budget vs ~2-3% expected | 768 resonators * 5 FLOPs * 44100 Hz = ~170M FLOPs/s. Modern CPU does ~50 GFLOPS. Headroom exists but SIMD ensures comfort. |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL

**Reasoning**: The modal resonator bank processes 96 independent modes with identical arithmetic per mode, no inter-mode dependencies, and branchless inner loops. This is a textbook SIMD workload. At 96 modes x 8 voices, SIMD provides ~4x throughput improvement on AVX2, reducing worst-case CPU from ~3% to ~0.8%. The existing `harmonic_oscillator_bank_simd.cpp` provides an exact template for the Highway integration.

### Implementation Workflow

| Phase | What | When | Deliverables |
|-------|------|------|-------------|
| **1. Scalar** | Full algorithm with scalar code | `/speckit.implement` | Working ModalResonatorBank + complete test suite + CPU baseline |
| **2. SIMD** | Highway-accelerated inner loop | Task group in tasks.md | SIMD kernel in `modal_resonator_bank_simd.cpp` + all tests pass + CPU improvement measured |

- Phase 2 will NOT change the public API
- Phase 2 will keep scalar path as fallback (tail loop)
- Phase 2 will follow exact pattern of `harmonic_oscillator_bank_simd.cpp`

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Mode culling (skip inactive modes) | ~20-50% when many modes culled | LOW | YES (in Phase 1) |
| Denormal flush per block (not per sample) | ~5% when modes are decaying | LOW | YES (in Phase 1) |
| Clock-divided coefficient updates (FR-021) | Marginal (optional) | LOW | DEFER (per spec: MAY, no obligation) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from physical modelling roadmap):
- Phase 2: Impact Exciter (Layer 2 processor)
- Phase 3: Waveguide String (Layer 2 processor)
- Phase 4: Sympathetic Resonance (post-voice global)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `ModalResonatorBank` | HIGH | Phase 2 (same resonator, different excitation), Phase 4 (sympathetic) | Keep in DSP library. API designed for any excitation source. |
| `PhysicalModelMixer` | HIGH | All subsequent phases (same mix logic) | Keep in Innexus plugin DSP. Phase-independent. |
| Chaigne-Lambourg damping | MEDIUM | Phase 3 waveguide might use similar damping | Keep inside ModalResonatorBank for now. Extract if Phase 3 needs it. |
| Transient emphasis | MEDIUM | Phase 2 Impact Exciter may use similar conditioning | Keep inside ModalResonatorBank for now. Extract if Phase 2 needs it. |
| SoA + SIMD pattern | HIGH | Any future SIMD-intensive processor | Pattern already established by HarmonicOscillatorBank. |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep `ModalResonatorBank` in shared DSP library | Phase 2 and Phase 4 will use same resonator with different excitation sources |
| Keep `PhysicalModelMixer` in plugin DSP | Innexus-specific; other plugins don't have this architecture |
| Keep damping model inside class | Only one consumer for now; extract after Phase 3 if needed |
| Keep transient emphasis inside class | Only one consumer for now; extract after Phase 2 if needed |

### Review Trigger

After implementing **Phase 2 (Impact Exciter)**, review:
- [ ] Does Phase 2 need the Chaigne-Lambourg damping model? -> Extract to shared utility
- [ ] Does Phase 2 need transient emphasis? -> Extract to shared utility
- [ ] Does `PhysicalModelMixer` need any API changes for exciter selection? -> Evaluate

## Project Structure

### Documentation (this feature)

```text
specs/127-modal-resonator-bank/
  plan.md              # This file
  spec.md              # Feature specification
  research.md          # Phase 0: research findings
  data-model.md        # Phase 1: entity definitions
  quickstart.md        # Phase 1: implementation guide
  contracts/
    modal_resonator_bank.h     # API contract for ModalResonatorBank
    physical_model_mixer.h     # API contract for PhysicalModelMixer
```

### Source Code (repository root)

```text
dsp/
  include/krate/dsp/processors/
    modal_resonator_bank.h           # NEW: Layer 2 processor (header-only, scalar)
    modal_resonator_bank_simd.cpp    # NEW: SIMD kernel (Phase 2)
  tests/unit/processors/
    test_modal_resonator_bank.cpp    # NEW: Unit tests

plugins/innexus/
  src/
    plugin_ids.h                     # MODIFY: Add 5 param IDs (800-804)
    dsp/
      physical_model_mixer.h         # NEW: Stateless mixer utility
    processor/
      innexus_voice.h                # MODIFY: Add modalResonator field
      processor.h                    # MODIFY: Add atomics + smoothers
      processor.cpp                  # MODIFY: Voice render loop
      processor_params.cpp           # MODIFY: Handle new param changes
      processor_state.cpp            # MODIFY: Save/load new params
    controller/
      controller.cpp                 # MODIFY: Register new params
  tests/
    unit/processor/
      test_physical_model.cpp        # NEW: Integration tests
```

**Structure Decision**: Standard monorepo structure. New DSP component in shared library (Layer 2). Plugin-local mixer in Innexus DSP directory. Tests mirror source locations.

## Complexity Tracking

No constitution violations. All design decisions comply with the constitution.
