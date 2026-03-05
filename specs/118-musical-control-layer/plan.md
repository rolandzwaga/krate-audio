# Implementation Plan: Innexus Milestone 4 -- Musical Control Layer (Freeze, Morph, Harmonic Filtering)

**Branch**: `118-musical-control-layer` | **Date**: 2026-03-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/118-musical-control-layer/spec.md`

## Summary

Add a Musical Control Layer to the Innexus harmonic analysis/synthesis plugin: manual Freeze (captures HarmonicFrame + ResidualFrame as a creative snapshot), Morph (linear interpolation between frozen State A and live State B for both harmonic partials and residual noise), Harmonic Filtering (5 preset per-partial amplitude masks: All-Pass, Odd Only, Even Only, Low Harmonics, High Harmonics), and Responsiveness (direct pass-through to the existing `HarmonicModelBuilder::setResponsiveness()` dual-timescale blend). Four new VST3 parameters (IDs 300-303) are registered, state persistence is bumped to version 4, and all new audio processing is real-time safe with pre-allocated storage.

## Technical Context

**Language/Version**: C++20, MSVC 2022 / Clang / GCC
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared library)
**Storage**: IBStream binary blob (VST3 state persistence, version 4)
**Testing**: Catch2 (dsp_tests, innexus_tests targets) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform VST3 plugin
**Project Type**: Monorepo with shared DSP library + plugin
**Performance Goals**: Combined freeze + morph + filter processing < 0.1% single-core CPU @ 44.1kHz (SC-007)
**Constraints**: Zero allocations on audio thread; all frozen frames, morph intermediates, and filter masks pre-allocated as member variables
**Scale/Scope**: 4 new parameters, 2 new utility functions (in shared DSP), processor/controller extensions; ~4 new test files, ~80 tasks across 8 phases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; no cross-includes
- [x] New parameters (kFreezeId, kMorphPositionId, kHarmonicFilterTypeId, kResponsivenessId) registered in Controller, handled via atomics in Processor
- [x] State version bumped to 4; backward compatible with v3

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] Frozen HarmonicFrame and ResidualFrame stored as member variables (no heap allocation)
- [x] Morph intermediate HarmonicFrame stored as member variable (no heap allocation)
- [x] Harmonic filter mask is a fixed-size `std::array<float, kMaxPartials>` member
- [x] No memory allocation, locks, exceptions, or I/O on audio thread
- [x] All buffers pre-allocated before setActive(true)

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see section below)
- [x] Scalar-first workflow planned

**Required Check - Principle IX (Layered Architecture):**
- [x] Frame interpolation utilities placed at Layer 2 (processors) alongside harmonic_types.h
- [x] Harmonic filter mask computation is a static utility function at Layer 2
- [x] No circular dependencies introduced

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with actual file paths, line numbers, test names, measured values
- [x] No thresholds will be relaxed from spec

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. No new classes or structs are needed.

| Planned Type | Search Pattern | Existing? | Action |
|--------------|----------------|-----------|--------|
| `lerpHarmonicFrame` | `lerpHarmonicFrame` | No | Create as free function in `harmonic_frame_utils.h` |
| `lerpResidualFrame` | `lerpResidualFrame` | No | Create as free function in `harmonic_frame_utils.h` |
| `computeHarmonicMask` | `computeHarmonicMask\|harmonicMask` | No | Create as free function in `harmonic_frame_utils.h` |
| `applyHarmonicMask` | `applyHarmonicMask` | No | Create as free function in `harmonic_frame_utils.h` |
| `HarmonicFilterType` (enum) | `HarmonicFilterType` | No | Create in `plugins/innexus/src/plugin_ids.h` |

**Utility Functions to be created**:

| Planned Function | Search Pattern | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `lerpHarmonicFrame` | `lerpHarmonic` | No | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | Create New |
| `lerpResidualFrame` | `lerpResidual` | No | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | Create New |
| `computeHarmonicMask` | `computeHarmonicMask` | No | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | Create New |
| `applyHarmonicMask` | `applyHarmonicMask` | No | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| HarmonicFrame | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Frozen state storage; morph source/destination; filter input |
| Partial | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Per-partial morph interpolation and filter mask application |
| kMaxPartials | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Array sizing for morph intermediates and filter masks |
| ResidualFrame | `dsp/include/krate/dsp/processors/residual_types.h` | 2 | Frozen residual state; morph interpolation of bandEnergies and totalEnergy |
| kResidualBands | `dsp/include/krate/dsp/processors/residual_types.h` | 2 | Residual morph loop bound |
| HarmonicModelBuilder | `dsp/include/krate/dsp/systems/harmonic_model_builder.h` | 3 | `setResponsiveness()` called with user parameter value |
| HarmonicOscillatorBank | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | 2 | Receives filtered/morphed frames; existing ~2ms amplitude smoothing handles transitions |
| ResidualSynthesizer | `dsp/include/krate/dsp/processors/residual_synthesizer.h` | 2 | Receives morphed/frozen residual frames; no changes needed |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Morph Position parameter smoothing (FR-017) |
| LiveAnalysisPipeline | `plugins/innexus/src/dsp/live_analysis_pipeline.h` | plugin | Access to `modelBuilder_` for `setResponsiveness()` |
| Processor | `plugins/innexus/src/processor/processor.h` | plugin | Extended with manual freeze state, morph logic, filter mask, responsiveness forwarding |
| Controller | `plugins/innexus/src/controller/controller.h` | plugin | Extended with 4 new parameter registrations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing frame utils, no conflicts)
- [x] `plugins/innexus/src/` - Plugin source (no existing freeze/morph/filter code beyond confidence-gated auto-freeze)
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs (300-399 range is free)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new functions are in a new header (`harmonic_frame_utils.h`) in the `Krate::DSP` namespace. The `HarmonicFilterType` enum is in the `Innexus` namespace (plugin-local). No naming conflicts detected. The utility functions are `inline` or `constexpr` in a header-only file, avoiding multiple definition issues.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| HarmonicFrame | partials | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| HarmonicFrame | numPartials | `int numPartials = 0` | Yes |
| HarmonicFrame | f0 | `float f0 = 0.0f` | Yes |
| HarmonicFrame | f0Confidence | `float f0Confidence = 0.0f` | Yes |
| HarmonicFrame | spectralCentroid | `float spectralCentroid = 0.0f` | Yes |
| HarmonicFrame | brightness | `float brightness = 0.0f` | Yes |
| HarmonicFrame | noisiness | `float noisiness = 0.0f` | Yes |
| HarmonicFrame | globalAmplitude | `float globalAmplitude = 0.0f` | Yes |
| Partial | amplitude | `float amplitude = 0.0f` | Yes |
| Partial | relativeFrequency | `float relativeFrequency = 0.0f` | Yes |
| Partial | harmonicIndex | `int harmonicIndex = 0` | Yes |
| Partial | phase | `float phase = 0.0f` | Yes |
| Partial | inharmonicDeviation | `float inharmonicDeviation = 0.0f` | Yes |
| Partial | frequency | `float frequency = 0.0f` | Yes |
| Partial | stability | `float stability = 0.0f` | Yes |
| Partial | age | `int age = 0` | Yes |
| ResidualFrame | bandEnergies | `std::array<float, kResidualBands> bandEnergies{}` | Yes |
| ResidualFrame | totalEnergy | `float totalEnergy = 0.0f` | Yes |
| ResidualFrame | transientFlag | `bool transientFlag = false` | Yes |
| HarmonicModelBuilder | setResponsiveness | `void setResponsiveness(float value) noexcept` | Yes |
| HarmonicOscillatorBank | loadFrame | `void loadFrame(const HarmonicFrame& frame, float targetPitch) noexcept` | Yes |
| ResidualSynthesizer | loadFrame | `void loadFrame(const ResidualFrame& frame, float brightness, float transientEmphasis)` | Yes |
| OnePoleSmoother | configure | `void configure(float timeMs, float sampleRate)` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float value) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| LiveAnalysisPipeline | modelBuilder_ | `Krate::DSP::HarmonicModelBuilder modelBuilder_` (private) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame, Partial, kMaxPartials
- [x] `dsp/include/krate/dsp/processors/residual_types.h` - ResidualFrame, kResidualBands
- [x] `dsp/include/krate/dsp/systems/harmonic_model_builder.h` - setResponsiveness(), responsiveness_
- [x] `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` - loadFrame(), kAmpSmoothTimeSec
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother
- [x] `plugins/innexus/src/processor/processor.h` - Processor class members
- [x] `plugins/innexus/src/processor/processor.cpp` - process() flow, state v3, confidence-gated freeze
- [x] `plugins/innexus/src/controller/controller.cpp` - Parameter registration, setComponentState
- [x] `plugins/innexus/src/plugin_ids.h` - ParameterIds enum
- [x] `plugins/innexus/src/dsp/live_analysis_pipeline.h` - LiveAnalysisPipeline class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| HarmonicModelBuilder | `setResponsiveness()` is on the private `modelBuilder_` inside `LiveAnalysisPipeline` | Need to add a public forwarding method `setResponsiveness(float)` to LiveAnalysisPipeline |
| Processor state | Current version is 3 (M3); must write 4 for M4 | `streamer.writeInt32(4)` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` or `setValue()` | `smoother.snapTo(value)` |
| OnePoleSmoother | Constructor default is NOT configured; must call `configure(timeMs, sampleRate)` | Call in `setupProcessing()` |
| StringListParameter | Entries are 0-indexed, maps to `ParamValue` via `stepCount` | 5 entries -> stepCount inferred, values 0.0/0.25/0.5/0.75/1.0 |
| Partial.harmonicIndex | 1-based (1 = fundamental) | Filter mask index = harmonicIndex, not array position |
| State persistence | M3 data (inputSource, latencyMode) is written AFTER residual frames block | M4 data must be appended AFTER M3 data |
| Confidence-gated freeze | Uses `isFrozen_` and `lastGoodFrame_` -- separate from manual freeze | Manual freeze uses `manualFreezeActive_`, `manualFrozenFrame_`, `manualFrozenResidualFrame_` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Morph position smoothing | Uses existing OnePoleSmoother as member variable |
| Freeze capture logic | Reads processor-local state, tightly coupled to process() flow |

**Decision**: No new Layer 0 extractions. The new utility functions (`lerpHarmonicFrame`, `lerpResidualFrame`, `computeHarmonicMask`, `applyHarmonicMask`) belong at Layer 2 alongside `harmonic_types.h` and `residual_types.h` because they operate on Layer 2 data types.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Morph and filter are feed-forward per-frame operations |
| **Data parallelism width** | 48 partials + 16 bands | Morph iterates over up to 48 partials and 16 residual bands |
| **Branch density in inner loop** | LOW | Simple lerp and multiply per partial, minimal conditionals |
| **Dominant operations** | Per-partial lerp + multiply | Trivially cheap arithmetic |
| **Current CPU budget vs expected usage** | 0.1% budget vs ~0.001% expected | Per-frame operations on 48 partials = ~200 FLOPs per frame (~86 frames/sec) = negligible |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The morph interpolation (48 partials x 2 lerps) and harmonic filter (48 multiplies) happen once per analysis frame (~86 Hz at 44.1kHz/512 hop), not per sample. Total cost is approximately 300 floating-point operations per frame, which is completely negligible. SIMD optimization would add code complexity with zero measurable benefit. The oscillator bank (which IS per-sample) is already optimized.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip morph when morphPosition is 0.0 or 1.0 | Avoids 48 lerps per frame | LOW | YES |
| Skip filter when type is All-Pass | Avoids 48 multiplies per frame | LOW | YES |
| Pre-compute filter mask on type change (not per frame) | Avoids recomputation | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin-local (Processor extensions) + Layer 2 (utility functions)

**Related features at same layer** (from INNEXUS-ROADMAP.md):
- Milestone 5 (Harmonic Memory, Phases 15-16): Stores frozen snapshots as persistent presets; needs freeze capture and `lerpHarmonicFrame` for morphing between stored snapshots
- Priority 4 (Evolution Engine): Slowly drifts between stored spectra using `lerpHarmonicFrame`
- Priority 5 (Harmonic Modulators): LFO modulation of partial groups using per-partial amplitude masks (same mechanism as harmonic filter)
- Priority 6 (Multi-Source Blending): N-way blending generalizes `lerpHarmonicFrame`

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `lerpHarmonicFrame()` | HIGH | M5 snapshot morphing, P4 evolution, P6 multi-source | Place in shared DSP library (Layer 2) now |
| `lerpResidualFrame()` | HIGH | Same as above | Place in shared DSP library (Layer 2) now |
| `computeHarmonicMask()` | MEDIUM | P5 harmonic modulators, M7 custom curve UI | Place in shared DSP library (Layer 2) now |
| `applyHarmonicMask()` | MEDIUM | P5 harmonic modulators | Place in shared DSP library (Layer 2) now |

### Detailed Analysis (for HIGH potential items)

**`lerpHarmonicFrame()`** provides:
- Linear interpolation of L2-normalized amplitudes between two HarmonicFrame instances
- Linear interpolation of relativeFrequency between two frames
- Handling of unequal partial counts (missing partials treated as zero amplitude, relFreq defaults to harmonic index)
- Frame metadata interpolation (globalAmplitude, spectralCentroid, brightness, noisiness)
- Does NOT interpolate phase (oscillator bank is phase-continuous)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| M5 Harmonic Memory | YES | Morphs between stored snapshots |
| P4 Evolution Engine | YES | Core primitive for slow spectral drift |
| P6 Multi-Source Blending | YES | Generalizes to N-way (weighted sum of lerps) |

**Recommendation**: Place in `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` as a free inline function in the `Krate::DSP` namespace. This is the correct Layer 2 location alongside `harmonic_types.h`.

**`lerpResidualFrame()`** provides:
- Per-band linear interpolation of `bandEnergies[16]`
- Linear interpolation of `totalEnergy`
- Transient flag selection (dominant source based on morph position)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| M5 Harmonic Memory | YES | Snapshot includes residual data |
| P4 Evolution Engine | YES | Evolves residual character alongside harmonics |

**Recommendation**: Place in same `harmonic_frame_utils.h` file alongside `lerpHarmonicFrame()`.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| `lerpHarmonicFrame` + `lerpResidualFrame` in shared DSP Layer 2 | 3+ future consumers identified (M5, P4, P6); placing now avoids future refactoring |
| `computeHarmonicMask` + `applyHarmonicMask` in shared DSP Layer 2 | P5 modulators will need per-partial amplitude masks; same mechanism |
| `HarmonicFilterType` enum in plugin_ids.h (not shared) | Only Innexus uses preset names; P5 modulators use different mask generation |
| Manual freeze state separate from auto-freeze | Different purposes: auto-freeze = stability, manual = creative. Separate member variables prevent interference |
| `setResponsiveness()` forwarding via LiveAnalysisPipeline public method | modelBuilder_ is private; adding public forwarding is cleaner than friend access |

### Review Trigger

After implementing **M5 (Harmonic Memory, Phases 15-16)**, review this section:
- [ ] Does M5 use `lerpHarmonicFrame` for snapshot morphing? Confirm API is sufficient
- [ ] Does M5 need frame serialization beyond what `ResidualFrame`/`HarmonicFrame` provide?
- [ ] Does M5 extend freeze capture with naming/tagging? Consider shared snapshot struct

---

## Architecture Overview

### Signal Chain (Extended)

```
Analysis Source (Sample/Sidechain)
    |
    v
Analysis Pipeline (existing)
    |
    v
HarmonicFrame + ResidualFrame (live, State B)
    |
    v
[Manual Freeze Gate]  -----> if freeze ON: capture State A (once)
    |                         if freeze OFF + was frozen: 10ms crossfade
    v
[Confidence-Gated Auto-Freeze] (existing, lower priority than manual)
    |
    v
[Morph Interpolation]  <---- morphPosition (0=State A, 1=State B)
    |                         lerp amplitudes, relativeFreqs, residual bands
    v
[Harmonic Filter]       <---- filterType (All-Pass/Odd/Even/Low/High)
    |                         effectiveAmp_n = amp_n * mask(n)
    |                         residual passes through unmodified
    v
Oscillator Bank + Residual Synthesizer (existing)
    |
    v
Output
```

### Key Architecture Decisions

1. **Manual freeze captures BOTH harmonic and residual state** as separate member variables (`manualFrozenFrame_`, `manualFrozenResidualFrame_`), independent of the auto-freeze's `lastGoodFrame_`.

2. **Morph produces an intermediate HarmonicFrame** (`morphedFrame_`) that is then passed through the harmonic filter. The morphed frame is a member variable (pre-allocated, real-time safe).

3. **Harmonic filter mask is pre-computed** when the filter type changes, stored as `filterMask_[kMaxPartials]`. Applied per-partial as a simple multiply.

4. **The filtered frame is what gets loaded into the oscillator bank** via `loadFrame()`. The oscillator bank's existing per-partial amplitude smoothing (~2ms) handles transitions when the filter type or morph position changes.

5. **Responsiveness parameter** is a direct pass-through: `liveAnalysis_.setResponsiveness(value)` which forwards to `modelBuilder_.setResponsiveness(value)`.

---

## Component-by-Component Design

### 1. New Shared DSP Utility: `harmonic_frame_utils.h`

**Location**: `dsp/include/krate/dsp/processors/harmonic_frame_utils.h`
**Layer**: 2 (processors)
**Dependencies**: `harmonic_types.h`, `residual_types.h` (both Layer 2)

```cpp
namespace Krate::DSP {

/// Interpolate between two HarmonicFrames.
/// - Amplitudes: lerp of L2-normalized values
/// - RelativeFrequencies: lerp (missing partials default to harmonicIndex)
/// - Phase: NOT interpolated (oscillator bank is phase-continuous)
/// - Metadata: lerp of globalAmplitude, spectralCentroid, brightness, noisiness
/// - numPartials: max of both frames
inline HarmonicFrame lerpHarmonicFrame(
    const HarmonicFrame& a, const HarmonicFrame& b, float t) noexcept;

/// Interpolate between two ResidualFrames.
/// - bandEnergies: per-band lerp
/// - totalEnergy: lerp
/// - transientFlag: from dominant source (b when t > 0.5, a otherwise)
inline ResidualFrame lerpResidualFrame(
    const ResidualFrame& a, const ResidualFrame& b, float t) noexcept;

/// Compute harmonic filter mask for a given filter type index.
/// Writes mask values to the output array (1.0 = pass, 0.0 = attenuate).
/// Index is 0-based into the partials array; harmonicIndex is 1-based.
/// filterType: 0=AllPass, 1=OddOnly, 2=EvenOnly, 3=LowHarmonics, 4=HighHarmonics
/// Note: takes int (not HarmonicFilterType enum) to keep shared DSP free of plugin types.
/// Callers in processor.cpp must cast: static_cast<int>(filterTypeEnum) or use the
/// already-denormalized integer index directly.
inline void computeHarmonicMask(
    int filterType,
    const std::array<Partial, kMaxPartials>& partials,
    int numPartials,
    std::array<float, kMaxPartials>& mask) noexcept;

/// Apply a pre-computed harmonic mask to a frame's partial amplitudes in-place.
inline void applyHarmonicMask(
    HarmonicFrame& frame,
    const std::array<float, kMaxPartials>& mask) noexcept;

} // namespace Krate::DSP
```

**Implementation Details**:

`lerpHarmonicFrame()`:
- `maxPartials = max(a.numPartials, b.numPartials)`
- For each partial index `i` from 0 to maxPartials-1:
  - If partial exists in both: `amplitude = lerp(a.partials[i].amplitude, b.partials[i].amplitude, t)`
  - If partial exists only in A (i >= b.numPartials): `amplitude = lerp(a.partials[i].amplitude, 0.0f, t)`
  - If partial exists only in B (i >= a.numPartials): `amplitude = lerp(0.0f, b.partials[i].amplitude, t)`
  - `relativeFrequency`: lerp between the two, with missing side defaulting to `float(harmonicIndex)` (ideal harmonic ratio)
  - Copy `harmonicIndex`, `inharmonicDeviation`, `frequency`, `stability`, `age` from the dominant source (B when t > 0.5, A otherwise)
- Frame metadata: `f0 = lerp(a.f0, b.f0, t)`, same for `spectralCentroid`, `brightness`, `noisiness`, `globalAmplitude`
- `f0Confidence = lerp(a.f0Confidence, b.f0Confidence, t)`
- `numPartials = maxPartials`

`lerpResidualFrame()`:
- For each band `i` from 0 to kResidualBands-1: `bandEnergies[i] = lerp(a.bandEnergies[i], b.bandEnergies[i], t)`
- `totalEnergy = lerp(a.totalEnergy, b.totalEnergy, t)`
- `transientFlag = (t > 0.5f) ? b.transientFlag : a.transientFlag`

`computeHarmonicMask()`:
- **All-Pass (0)**: `mask[i] = 1.0f` for all
- **Odd Only (1)**: `mask[i] = (partials[i].harmonicIndex % 2 == 1) ? 1.0f : 0.0f`
- **Even Only (2)**: `mask[i] = (partials[i].harmonicIndex % 2 == 0) ? 1.0f : 0.0f`
- **Low Harmonics (3)**: `mask[i] = clamp(8.0f / float(partials[i].harmonicIndex), 0.0f, 1.0f)` -- meets FR-024 floor
- **High Harmonics (4)**: `mask[i] = clamp(float(partials[i].harmonicIndex) / 8.0f, 0.0f, 1.0f)` -- at index 1: 0.125 = -18dB (exceeds FR-025's -12dB floor); at index 8+: 1.0

### 2. Plugin ID Extensions: `plugin_ids.h`

Add to the existing `ParameterIds` enum in the 300-399 range:

```cpp
// Musical Control (300-399) -- M4
kFreezeId = 300,              // 0.0 = off, 1.0 = on (toggle)
kMorphPositionId = 301,       // 0.0 to 1.0, default 0.0
kHarmonicFilterTypeId = 302,  // StringListParameter: 5 presets
kResponsivenessId = 303,      // 0.0 to 1.0, default 0.5
```

Add a new enum:

```cpp
enum class HarmonicFilterType : int
{
    AllPass = 0,
    OddOnly = 1,
    EvenOnly = 2,
    LowHarmonics = 3,
    HighHarmonics = 4
};
```

### 3. Processor Extensions: `processor.h` / `processor.cpp`

**New member variables** (pre-allocated, real-time safe):

```cpp
// M4 Musical Control parameters (FR-001, FR-010, FR-019, FR-029)
std::atomic<float> freeze_{0.0f};              // 0.0 = off, 1.0 = on
std::atomic<float> morphPosition_{0.0f};       // 0.0 to 1.0
std::atomic<float> harmonicFilterType_{0.0f};  // normalized (0-4 mapped)
std::atomic<float> responsiveness_{0.5f};       // 0.0 to 1.0

// Manual Freeze State (FR-002, FR-003, FR-007)
bool manualFreezeActive_ = false;
Krate::DSP::HarmonicFrame manualFrozenFrame_{};
Krate::DSP::ResidualFrame manualFrozenResidualFrame_{};

// Morph intermediates (FR-011 to FR-018)
Krate::DSP::OnePoleSmoother morphPositionSmoother_;
Krate::DSP::HarmonicFrame morphedFrame_{};
Krate::DSP::ResidualFrame morphedResidualFrame_{};

// Harmonic Filter (FR-019 to FR-028)
std::array<float, Krate::DSP::kMaxPartials> filterMask_{};
int currentFilterType_ = 0;

// Freeze-to-live crossfade (FR-006)
int manualFreezeRecoverySamplesRemaining_ = 0;
int manualFreezeRecoveryLengthSamples_ = 0;
float manualFreezeRecoveryOldLevel_ = 0.0f;
static constexpr float kManualFreezeRecoveryTimeSec = 0.010f; // 10ms
```

**Process flow changes** (in `process()`):

The frame routing logic is restructured to insert freeze/morph/filter between the analysis output and the oscillator bank. The key change is that instead of loading frames directly into the oscillator bank, frames pass through a new processing chain:

1. **Determine active frame** (State B = current live or sample frame)
2. **Manual freeze gate**: If freeze just engaged, capture State A. If freeze disengaged, start 10ms crossfade.
3. **Morph**: If manual freeze is active, lerp between frozen State A and live State B using smoothed morphPosition. If no freeze, pass State B through unmodified.
4. **Harmonic filter**: Apply pre-computed mask to morphed frame's partial amplitudes.
5. **Load filtered frame** into oscillator bank and residual synth.

The confidence-gated auto-freeze remains as-is but is bypassed when manual freeze is active (FR-007).

**Parameter handling** (in `processParameterChanges()`):

```cpp
case kFreezeId:
    freeze_.store(static_cast<float>(value) > 0.5f ? 1.0f : 0.0f);
    break;
case kMorphPositionId:
    morphPosition_.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f));
    break;
case kHarmonicFilterTypeId:
    harmonicFilterType_.store(static_cast<float>(value));
    break;
case kResponsivenessId:
    responsiveness_.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f));
    break;
```

**Responsiveness forwarding** (in `process()`, early):

```cpp
float resp = responsiveness_.load(std::memory_order_relaxed);
liveAnalysis_.setResponsiveness(resp);
```

### 4. LiveAnalysisPipeline Extension

Add a public forwarding method:

```cpp
/// Set the responsiveness parameter on the internal HarmonicModelBuilder.
/// @param value Blend factor [0.0, 1.0] (0 = slow/stable, 1 = fast/responsive)
void setResponsiveness(float value) noexcept {
    modelBuilder_.setResponsiveness(value);
}
```

### 5. Controller Extensions: `controller.cpp`

Register 4 new parameters in `Controller::initialize()`:

```cpp
// M4 Musical Control parameters
parameters.addParameter(STR16("Freeze"), nullptr, 1, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate,
    kFreezeId);

auto* morphParam = new Steinberg::Vst::RangeParameter(
    STR16("Morph Position"), kMorphPositionId,
    STR16("%"), 0.0, 1.0, 0.0, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate);
parameters.addParameter(morphParam);

auto* filterParam = new Steinberg::Vst::StringListParameter(
    STR16("Harmonic Filter"), kHarmonicFilterTypeId, nullptr,
    Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
filterParam->appendString(STR16("All-Pass"));
filterParam->appendString(STR16("Odd Only"));
filterParam->appendString(STR16("Even Only"));
filterParam->appendString(STR16("Low Harmonics"));
filterParam->appendString(STR16("High Harmonics"));
parameters.addParameter(filterParam);

auto* respParam = new Steinberg::Vst::RangeParameter(
    STR16("Responsiveness"), kResponsivenessId,
    STR16("%"), 0.0, 1.0, 0.5, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate);
parameters.addParameter(respParam);
```

### 6. State Persistence: Version 4

**getState()** -- append after M3 data:

```cpp
// --- M4 parameters ---
streamer.writeInt8(freeze_.load(std::memory_order_relaxed) > 0.5f
    ? static_cast<Steinberg::int8>(1) : static_cast<Steinberg::int8>(0));
streamer.writeFloat(morphPosition_.load(std::memory_order_relaxed));
streamer.writeInt32(static_cast<Steinberg::int32>(
    std::round(harmonicFilterType_.load(std::memory_order_relaxed) * 4.0f)));
streamer.writeFloat(responsiveness_.load(std::memory_order_relaxed));
```

**setState()** -- read after M3 data (version >= 4):

```cpp
if (version >= 4)
{
    Steinberg::int8 freezeState = 0;
    if (streamer.readInt8(freezeState))
        freeze_.store(freezeState ? 1.0f : 0.0f);

    float morphPos = 0.0f;
    if (streamer.readFloat(morphPos))
        morphPosition_.store(std::clamp(morphPos, 0.0f, 1.0f));

    Steinberg::int32 filterType = 0;
    if (streamer.readInt32(filterType))
        harmonicFilterType_.store(
            std::clamp(static_cast<float>(filterType) / 4.0f, 0.0f, 1.0f));

    float resp = 0.5f;
    if (streamer.readFloat(resp))
        responsiveness_.store(std::clamp(resp, 0.0f, 1.0f));
}
```

**setComponentState()** (Controller) -- same read pattern, converting binary values back to normalized VST parameter values. When version >= 4, read M4 data in the same byte order (int8 freeze, float morphPos, int32 filterType, float responsiveness) and set each parameter's normalized value as follows:

- `kFreezeId`: `float(freezeState)` → 0.0 or 1.0 (already normalized for a step-1 toggle)
- `kMorphPositionId`: morphPos as-is (range [0,1] is already normalized)
- `kHarmonicFilterTypeId`: `std::clamp(float(filterType) / 4.0f, 0.0f, 1.0f)` — this is the exact inverse of the `round(value * 4.0f)` write formula; mapping int [0,4] → normalized [0.0, 0.25, 0.5, 0.75, 1.0]
- `kResponsivenessId`: resp as-is (range [0,1] is already normalized)

When version < 4, apply defaults: freeze=0.0, morph=0.0, filterNormalized=0.0 (All-Pass), responsiveness=0.5.

---

## Implementation Phases

### Phase 1: Shared DSP Utilities (harmonic_frame_utils.h)

1. Create `dsp/include/krate/dsp/processors/harmonic_frame_utils.h`
2. Implement `lerpHarmonicFrame()`, `lerpResidualFrame()`, `computeHarmonicMask()`, `applyHarmonicMask()`
3. Write unit tests: `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
   - Test lerp at t=0.0, t=0.5, t=1.0
   - Test unequal partial counts
   - Test residual band interpolation
   - Test transient flag selection at morph boundary (0.5)
   - Test all 5 filter mask presets (All-Pass, Odd, Even, Low, High)
   - Test mask application preserves other partial fields
   - Test FR-024: Low Harmonics floor `clamp(8/n, 0, 1)`
   - Test FR-025: High Harmonics attenuates index 1 by >= 12dB
4. Add to CMakeLists.txt (header-only, just add test file)
5. Build and verify all tests pass

### Phase 2: Parameter IDs and Plugin Registration

1. Add `kFreezeId`, `kMorphPositionId`, `kHarmonicFilterTypeId`, `kResponsivenessId` to `plugin_ids.h`
2. Add `HarmonicFilterType` enum to `plugin_ids.h`
3. Register parameters in `controller.cpp`
4. Write VST parameter registration tests in `plugins/innexus/tests/unit/vst/`
5. Build and verify pluginval passes

### Phase 3: Manual Freeze (Processor)

1. Add freeze member variables to `processor.h`
2. Add freeze parameter handling in `processParameterChanges()`
3. Implement freeze capture logic in `process()`:
   - Detect freeze engagement (transition from off to on)
   - Capture current HarmonicFrame + ResidualFrame as frozen state
   - While frozen, use frozen frames instead of live (bypassing auto-freeze)
4. Implement freeze disengage crossfade (10ms, FR-006)
5. Implement manual freeze priority over auto-freeze (FR-007)
6. Write freeze tests:
   - Freeze captures current frame (SC-002: output remains constant)
   - Freeze disengage crossfade completes in ~10ms (SC-001)
   - Freeze survives source switch (FR-008)
   - Manual freeze priority over auto-freeze (FR-007)
7. State persistence: version 4 for freeze state
8. Build, test, pluginval

### Phase 4: Morph (Processor)

1. Add morph member variables to `processor.h` (morphPositionSmoother_, morphedFrame_, morphedResidualFrame_)
2. Add morph parameter handling in `processParameterChanges()`
3. Configure morphPositionSmoother_ in `setupProcessing()` (~5-10ms time constant)
4. Implement morph logic in `process()`:
   - If freeze active: `morphedFrame_ = lerpHarmonicFrame(frozenFrame, liveFrame, smoothedMorphPos)`
   - If freeze inactive: pass-through live frame (FR-016)
   - Apply same morph to residual frames
5. Write morph tests:
   - Morph position 0.0 = frozen state output (SC-004)
   - Morph position 1.0 = live state output (SC-004)
   - Morph handles unequal partial counts (FR-015)
   - Morph has no effect when freeze is off (FR-016)
   - Morph position smoothing prevents zipper noise (FR-017)
   - Morph sweeps produce smooth transition (SC-003)
6. State persistence: version 4 for morph position
7. Build, test, pluginval

### Phase 5: Harmonic Filter (Processor)

1. Add filter member variables to `processor.h` (filterMask_, currentFilterType_)
2. Add filter parameter handling in `processParameterChanges()`
3. Implement filter logic in `process()`:
   - On filter type change: recompute filterMask_ via `computeHarmonicMask()`
   - Apply mask to morphed frame via `applyHarmonicMask()`
   - Residual passes through unmodified (FR-027)
4. Write filter tests:
   - All-Pass identity (FR-021)
   - Odd Only: even harmonics attenuated >= 60dB (SC-005)
   - Even Only: odd harmonics attenuated >= 60dB (SC-006)
   - Low Harmonics rolloff (FR-024)
   - High Harmonics rolloff (FR-025)
   - Filter applied after morph, before oscillator bank (FR-026)
   - Filter does not affect residual (FR-027)
   - Filter change is smooth (FR-028, handled by oscillator bank smoothing)
5. State persistence: version 4 for filter type
6. Build, test, pluginval

### Phase 6: Responsiveness Parameter

1. Add `setResponsiveness()` forwarding method to `LiveAnalysisPipeline`
2. Add responsiveness parameter handling in `processParameterChanges()`
3. Forward responsiveness value to `liveAnalysis_.setResponsiveness()` in `process()`
4. Write responsiveness tests:
   - Default 0.5 matches M1/M3 behavior (SC-008)
   - Value change takes effect within one frame (FR-031)
   - No effect in sample mode (FR-032)
5. State persistence: version 4 for responsiveness
6. Build, test, pluginval

### Phase 7: State Persistence v4

1. Update `getState()` to write version 4 with all 4 new parameters
2. Update `setState()` to read version 4 (with v3 backward compatibility)
3. Update `setComponentState()` in Controller for v4
4. Write state round-trip tests (SC-009):
   - Save with M4 parameters, reload, verify all restored
   - Load v3 state, verify M4 defaults applied
5. Build, test, pluginval

### Phase 8: Integration Testing and CPU Verification

1. Full integration tests combining freeze + morph + filter + responsiveness (including real-time safety verification under ASan — FR-035)
2. CPU measurement test (SC-007): verify < 0.1% overhead
3. SC-003 measurement: morph sweep spectral analysis — verify no impulsive frame-to-frame amplitude spikes
4. Pluginval at strictness level 5 (SC-010)
5. Update architecture documentation at `specs/_architecture_/` (Constitution Principle XIV — mandatory; task T122b): document `harmonic_frame_utils.h` in Layer 2 section and M4 Musical Control Layer extension pattern in the Innexus plugin section

---

## Test Strategy

### Unit Tests (DSP Layer)

**File**: `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- `lerpHarmonicFrame` at boundaries (t=0, t=1) and midpoint (t=0.5)
- `lerpHarmonicFrame` with unequal partial counts
- `lerpResidualFrame` at boundaries and midpoint
- `lerpResidualFrame` transient flag selection
- `computeHarmonicMask` for all 5 filter types
- `applyHarmonicMask` preserves non-amplitude fields
- FR-024/FR-025 attenuation floors

### Integration Tests (Plugin Layer)

**File**: `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- Freeze capture and release with crossfade
- Morph interpolation through full range
- Harmonic filter preset cycling
- Responsiveness parameter forwarding
- Combined freeze + morph + filter pipeline
- State persistence round-trip (v4)
- Backward compatibility (v3 state loads with defaults)
- CPU budget verification (SC-007)

### VST Tests

**File**: `plugins/innexus/tests/unit/vst/musical_control_vst_tests.cpp`
- Parameter registration for all 4 new params
- Parameter normalization ranges correct
- StringListParameter entries for harmonic filter

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Manual freeze interferes with auto-freeze | LOW | HIGH | Separate member variables; explicit priority check in process() |
| Morph produces clicks at frame boundaries | LOW | MEDIUM | Oscillator bank's ~2ms amplitude smoothing handles transitions |
| Filter mask recomputation on wrong thread | LOW | HIGH | Mask recomputed in process() (audio thread), stored as member array |
| State v4 incompatible with v3 | LOW | HIGH | Explicit version check; v3 loads with default values for M4 params |
| Responsiveness forwarding to private modelBuilder_ | LOW | LOW | Public forwarding method on LiveAnalysisPipeline |

---

## Project Structure

### Documentation (this feature)

```text
specs/118-musical-control-layer/
+-- plan.md              # This file
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
+-- harmonic_frame_utils.h       # NEW: lerpHarmonicFrame, lerpResidualFrame, computeHarmonicMask, applyHarmonicMask

dsp/tests/unit/processors/
+-- harmonic_frame_utils_tests.cpp  # NEW: Unit tests for frame utilities

plugins/innexus/src/
+-- plugin_ids.h                    # MODIFIED: Add kFreezeId(300), kMorphPositionId(301), kHarmonicFilterTypeId(302), kResponsivenessId(303), HarmonicFilterType enum
+-- dsp/
|   +-- live_analysis_pipeline.h    # MODIFIED: Add setResponsiveness() forwarding method
+-- processor/
|   +-- processor.h                 # MODIFIED: Add manual freeze state, morph members, filter mask, responsiveness atomic
|   +-- processor.cpp               # MODIFIED: Freeze/morph/filter logic in process(), state v4, parameter handling
+-- controller/
    +-- controller.cpp              # MODIFIED: Register 4 new parameters, setComponentState v4

plugins/innexus/tests/unit/processor/
+-- musical_control_tests.cpp       # NEW: Integration tests for freeze/morph/filter/responsiveness

plugins/innexus/tests/unit/vst/
+-- musical_control_vst_tests.cpp   # NEW: VST parameter registration tests
```

**Structure Decision**: Follows existing monorepo pattern. New shared utility functions in `dsp/include/krate/dsp/processors/` (Layer 2). Plugin integration extends existing files in `plugins/innexus/src/`. Tests split between DSP unit tests and plugin integration tests.

## Complexity Tracking

No constitution violations. All design decisions align with established patterns.
