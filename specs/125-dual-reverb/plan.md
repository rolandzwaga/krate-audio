# Implementation Plan: Dual Reverb System

**Branch**: `125-dual-reverb` | **Date**: 2026-03-11 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/125-dual-reverb/spec.md`

## Summary

Optimize the existing Dattorro plate reverb (Gordon-Smith LFO, block-rate smoothing, contiguous buffer, denormal cleanup) for 15%+ CPU reduction, create a new SIMD-optimized 8-channel FDN reverb using Google Highway (Hadamard diffuser, Householder feedback, SoA layout), and integrate both into Ruinae with a reverb type selector and equal-power crossfade switching.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, Google Highway 1.2.0 (SIMD), KrateDSP shared library
**Storage**: N/A (plugin state via IBStreamer)
**Testing**: Catch2 (dsp_tests, ruinae_tests), pluginval (VST3 validation)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: Dattorro: 15%+ CPU reduction (SC-001). FDN: <2% CPU for 512 samples at 44.1kHz (SC-002). Total plugin <5% single core.
**Constraints**: Real-time safe (zero allocations in process path), no locks/exceptions on audio thread, all sample rates 8kHz-192kHz
**Scale/Scope**: ~1 new class (FDNReverb), ~1 modified class (Reverb), ~5 modified plugin files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] No cross-includes between Processor and Controller
- [x] New parameter (kReverbTypeId) follows normalized 0-1 convention at VST boundary
- [x] State flows: Host -> Processor -> Controller (via setComponentState)

**Principle II (Real-Time Safety):**
- [x] No allocations in process path (all in prepare())
- [x] No locks, exceptions, or I/O on audio thread
- [x] Buffers pre-allocated in prepare()

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see below) -- verdict: BENEFICIAL
- [x] Scalar-first workflow: Phase 1 scalar, Phase 2 SIMD
- [x] Contiguous memory, SoA layout, minimal branching in inner loops

**Principle VI (Cross-Platform):**
- [x] Google Highway handles ISA dispatch (SSE2/AVX2/NEON)
- [x] No platform-specific UI code
- [x] No platform-specific APIs

**Principle IX (Layered Architecture):**
- [x] FDNReverb at Layer 4 (effects), composes Layer 0-1 only
- [x] No circular dependencies

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: FDNReverb

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FDNReverb | `grep -r "class FDNReverb" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (Householder/Hadamard are inline within FDNReverb)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ReverbParams | dsp/include/krate/dsp/effects/reverb.h | 4 | Shared parameter interface for both reverb types |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Pre-delay in FDN reverb |
| OnePoleLP | dsp/include/krate/dsp/primitives/one_pole.h | 1 | Bandwidth filter reference (FDN uses inline one-pole for SIMD) |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | Reference pattern (FDN uses inline DC blockers for SIMD) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing in both reverbs |
| equalPowerGains | dsp/include/krate/dsp/core/crossfade_utils.h | 0 | Reverb type crossfade |
| crossfadeIncrement | dsp/include/krate/dsp/core/crossfade_utils.h | 0 | Calculate crossfade rate |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal handling |
| isNaN / isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation |
| kPi / kTwoPi / kHalfPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Math constants |
| Gordon-Smith pattern | dsp/include/krate/dsp/processors/particle_oscillator.h | 2 | Proven LFO technique (pattern adapted, not code reused) |
| Delay crossfade pattern | plugins/ruinae/src/engine/ruinae_effects_chain.h | plugin | Crossfade switching pattern adapted for reverb |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/effects/` - Layer 4 effects (reverb.h, no fdn_reverb.h)
- [x] `plugins/ruinae/src/plugin_ids.h` - ID 1709 is available
- [x] `plugins/ruinae/src/engine/` - Effects chain structure

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: FDNReverb is a new class with a unique name not found anywhere in the codebase. All utility functions (Householder, Hadamard) are implemented as inline members or static functions within the class, avoiding namespace pollution. The existing Reverb class is modified in-place without renaming.

**Note on Dattorro contiguous buffer (FR-004)**: The contiguous delay buffer for the Dattorro reverb is implemented using a private `ContiguousDelayBuffer` helper class within the Reverb class — NOT by modifying the existing `DelayLine` Layer 1 API. Modifying `DelayLine` would be too invasive (see research.md R6). The helper manages the single allocation and provides per-section read/write access with power-of-2 per-section sizes for masking efficiency.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | advanceSamples | `void advanceSamples(size_t numSamples) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| DelayLine | read | `[[nodiscard]] float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| equalPowerGains | (pair version) | `[[nodiscard]] inline std::pair<float, float> equalPowerGains(float position) noexcept` | Yes |
| crossfadeIncrement | | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |
| ReverbParams | roomSize | `float roomSize = 0.5f` | Yes |
| ReverbParams | damping | `float damping = 0.5f` | Yes |
| ReverbParams | width | `float width = 1.0f` | Yes |
| ReverbParams | mix | `float mix = 0.3f` | Yes |
| ReverbParams | preDelayMs | `float preDelayMs = 0.0f` | Yes |
| ReverbParams | diffusion | `float diffusion = 0.7f` | Yes |
| ReverbParams | freeze | `bool freeze = false` | Yes |
| ReverbParams | modRate | `float modRate = 0.5f` | Yes |
| ReverbParams | modDepth | `float modDepth = 0.0f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains, crossfadeIncrement
- [x] `dsp/include/krate/dsp/effects/reverb.h` - ReverbParams struct, Reverb class
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs, state version
- [x] `plugins/ruinae/src/parameters/reverb_params.h` - RuinaeReverbParams
- [x] `plugins/ruinae/src/engine/ruinae_effects_chain.h` - Effects chain

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | `advanceSamples()` uses closed-form `pow(coeff, N)`, not loop | O(1) but calls std::pow |
| DelayLine | `prepare(sampleRate, maxDelaySeconds)` -- second arg is seconds, not samples | Convert ms to seconds: `ms * 0.001f` |
| OnePoleLP | `setCutoff(hz)` takes Hz, not normalized | Pass frequency in Hz directly |
| equalPowerGains | Does NOT clamp position -- caller must keep in [0, 1] | Clamp before calling |
| Reverb::processBlock | Simple loop calling process() -- no sub-block optimization | This is what we are optimizing |
| kCurrentStateVersion | Currently 4, must bump to 5 | Check version in load for backward compat |
| crossfadeIncrement | Crossfade duration is 30ms (not a range) | Use `crossfadeIncrement(30.0f, sampleRate_)` — matches research R9 decision |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Householder matrix apply | Specific to 8-channel FDN, tightly coupled to SoA layout |
| Hadamard butterfly (FWHT) | Specific to FDN diffuser, inline for performance |
| Gordon-Smith phasor advance | 3-line inline pattern, not worth extracting |
| Block-rate smoother update | Reverb-specific timing (16-sample sub-blocks) |

**Decision**: No new Layer 0 extractions. All utilities are tightly coupled to their specific reverb implementations and don't have 3+ consumers. The Householder/Hadamard utilities could be extracted if a second FDN-based effect is added later.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | 8 delay lines feed back through Householder matrix. Serial across samples but parallel across channels at each sample step. |
| **Data parallelism width** | 8 channels | Perfect for 8-wide AVX or 2x 4-wide SSE. Highway handles dispatch. |
| **Branch density in inner loop** | LOW | No conditionals in inner loop. Freeze handled by coefficient choice before loop. |
| **Dominant operations** | Arithmetic (add, mul) + one-pole filter | Filter bank, matrix multiply, gain application. All SIMD-friendly. |
| **Current CPU budget vs expected usage** | <2% budget, expected ~1-2% scalar | FDN has more work than Dattorro (8 channels vs 2 tanks). SIMD worthwhile. |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL

**Reasoning**: The FDN reverb processes 8 independent channels at each sample step. The Householder matrix (O(N): N additions + 1 multiply + N subtractions = 17 ops for N=8), one-pole filter bank, and gain application are all element-wise or reduce-broadcast operations that map directly to SIMD lanes. The data is naturally arranged as SoA (8-element arrays). With Highway's ScalableTag, a single code path handles SSE (2x 4-wide) and AVX (1x 8-wide) transparently. (Full SIMD viability analysis: see research.md R7.)

### Implementation Workflow

| Phase | What | When | Deliverables |
|-------|------|------|-------------|
| **1. Scalar** | Full FDN algorithm with scalar code | `/speckit.implement` tasks | Working FDNReverb + complete test suite + CPU baseline |
| **2. SIMD** | Highway SIMD kernels for filter bank, Householder, Hadamard | `/speckit.implement` final task group | SIMD implementation + all tests pass + CPU improvement measured |

- Phase 2 MUST NOT change the public API
- Phase 2 MUST keep scalar as fallback
- Phase 2 MUST re-run full test suite
- FR-016 (16-sample sub-blocks) applies to the SIMD `processBlock` path introduced in Phase 2. The scalar Phase 1 `processBlock` calls `process()` per sample without sub-blocks; this is intentional and correct.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Gordon-Smith phasor (Dattorro LFO) | ~10-15% in modulated path | LOW | YES (FR-001) |
| Block-rate smoothing (Dattorro) | ~5-10% reduced smoother/filter updates | LOW | YES (FR-002, FR-003) |
| Contiguous buffer (Dattorro) | ~2-5% better cache locality | MEDIUM | YES (FR-004) |
| FTZ/DAZ reliance (Dattorro) | ~1-2% removed flushDenormal calls | LOW | YES (FR-005) |
| SoA layout for FDN | Enables SIMD, better cache | MEDIUM | YES (FR-014) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer**:
- Iterum delay modes (could add post-delay reverb)
- Future Disrumpo reverb send
- Future reverb-based effects (shimmer, freeze reverb)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FDNReverb | HIGH | Iterum (reverb after delay), future plugins | Keep at Layer 4 in KrateDSP -- already shared |
| Hadamard/Householder utilities | MEDIUM | Other FDN-based algorithms | Keep inline in FDNReverb, extract after 2nd consumer |
| Block-rate smoother pattern | MEDIUM | Any DSP component needing sub-block updates | Keep as reverb-specific, document pattern |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep Householder/Hadamard in FDNReverb | Only one consumer, premature to extract |
| FDNReverb in KrateDSP shared library | Available to all plugins via Layer 4 |
| No abstract Reverb base class | Only 2 reverb types, both accept ReverbParams. Polymorphism via template or if/else in effects chain is simpler than vtable. |

### Review Trigger

After implementing the next reverb-related feature, review:
- [ ] Does the new feature need Householder/Hadamard utilities? -> Extract to Layer 0/2
- [ ] Does the new feature use the same crossfade switching pattern? -> Extract to shared
- [ ] Any duplicated FDN infrastructure? -> Consider shared FDN base

## Project Structure

### Documentation (this feature)

```text
specs/125-dual-reverb/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- fdn_reverb_api.h
|   +-- reverb_type_integration.h
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- effects/
|       +-- reverb.h              # MODIFIED: Dattorro optimization
|       +-- fdn_reverb.h          # NEW: FDN reverb class
|       +-- fdn_reverb_simd.cpp   # NEW: Highway SIMD kernels
+-- tests/unit/
|   +-- effects/
|       +-- reverb_test.cpp       # MODIFIED: Tests for optimized Dattorro
|       +-- fdn_reverb_test.cpp   # NEW: FDN reverb tests
+-- CMakeLists.txt                # MODIFIED: Add fdn_reverb_simd.cpp

plugins/ruinae/
+-- src/
|   +-- plugin_ids.h              # MODIFIED: kReverbTypeId, state version 5
|   +-- parameters/
|   |   +-- reverb_params.h       # MODIFIED: reverbType field
|   +-- engine/
|   |   +-- ruinae_effects_chain.h # MODIFIED: Dual reverb + crossfade
|   +-- processor/
|   |   +-- processor.h           # MODIFIED: reverbType atomic
|   |   +-- processor.cpp         # MODIFIED: State save/load, param handling
|   +-- controller/
|       +-- controller.cpp        # MODIFIED: Register reverb type param
+-- tests/
    +-- unit/
        +-- processor/
            +-- reverb_type_test.cpp # NEW: Reverb type switching tests
```

**Structure Decision**: Monorepo structure unchanged. New FDN reverb is a shared DSP component at Layer 4 (available to all plugins). Integration code lives in the Ruinae plugin directory.

## Complexity Tracking

No constitution violations. All design decisions comply with project principles.

## Post-Design Constitution Re-Check

*Re-validated after Phase 1 design completion.*

- [x] Principle I: kReverbTypeId (1709) registered as StringListParameter, normalized at VST boundary
- [x] Principle II: All allocations in prepare(), process path is allocation-free
- [x] Principle IV: SIMD analysis complete, scalar-first workflow planned
- [x] Principle VI: Google Highway for cross-platform SIMD, no native APIs
- [x] Principle VIII: Tests planned for all new code, existing tests preserved
- [x] Principle IX: FDNReverb at Layer 4, depends on Layer 0-1 only
- [x] Principle XIV: ODR search complete, no conflicts found
- [x] Principle XVI: SC thresholds will be measured with real benchmarks (no relaxation)

No gate failures. Design is approved for implementation.
