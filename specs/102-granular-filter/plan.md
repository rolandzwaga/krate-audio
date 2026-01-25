# Implementation Plan: GranularFilter

**Branch**: `102-granular-filter` | **Date**: 2026-01-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/102-granular-filter/spec.md`

## Summary

A Layer 3 DSP system that extends granular synthesis with per-grain SVF filtering. Each of the 64 grains gets two independent SVF filter instances (one per stereo channel, total 128 filters) with randomizable cutoff frequencies. Filter processing occurs AFTER pitch shifting in the grain signal chain: read -> pitch -> envelope -> **filter** -> pan. This creates spectral variations impossible with post-granular filtering.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- GranularEngine (Layer 3) - foundation for granular processing
- SVF (Layer 1) - per-grain filter instances
- Xorshift32 (Layer 0) - cutoff randomization
- GrainPool, GrainProcessor, GrainScheduler (existing granular components)
**Storage**: N/A (in-memory only)
**Testing**: Catch2 (existing test framework)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component
**Performance Goals**: < 5% CPU on Intel i5-8400 with 64 simultaneous filtered grains at 48kHz
**Constraints**: Real-time safe (noexcept, no allocations in process path), zero latency
**Scale/Scope**: 64 grains max, 128 SVF instances (2 per grain for true stereo)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A - This is a DSP component, not plugin code.

**Principle II (Real-Time Audio Thread Safety)**:
- [x] All processing methods will be `noexcept`
- [x] No memory allocation in `process()` - all filters pre-allocated in `prepare()`
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions in audio path

**Principle III (Modern C++ Standards)**:
- [x] C++20 features (std::span, designated initializers)
- [x] RAII for resource management
- [x] `constexpr` and `const` used appropriately

**Principle IX (Layered DSP Architecture)**:
- [x] Layer 3 System - composes Layer 0-2 components
- [x] Will depend on: SVF (L1), DelayLine (L1), GrainPool (L1), GrainProcessor (L2), GrainScheduler (L2), Xorshift32 (L0)
- [x] No circular dependencies

**Principle X (DSP Processing Constraints)**:
- [x] SVF handles denormal flushing internally
- [x] Filter cutoff clamped to valid range (20Hz to sampleRate * 0.495)

**Principle XIII (Test-First Development)**:
- [x] Skills auto-load (testing-guide, vst-guide) - verified
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention)**:
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion)**:
- [x] All FR-xxx and SC-xxx requirements must be met before claiming complete

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: GranularFilter, FilteredGrain (extended Grain struct), GranularFilterParams

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| GranularFilter | `grep -r "class GranularFilter" dsp/ plugins/` | No | Create New |
| FilteredGrain | `grep -r "struct FilteredGrain" dsp/ plugins/` | No | Create New (extends Grain concept) |
| GranularFilterParams | `grep -r "GranularFilterParams" dsp/ plugins/` | No | Create New (extends GrainParams) |

**Utility Functions to be created**: None needed - using existing utilities

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| octavesToRatio | `grep -r "octavesToRatio" dsp/ plugins/` | No | Will use std::pow(2.0f, octaves) inline | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| GranularEngine | dsp/include/krate/dsp/systems/granular_engine.h | 3 | Reference architecture - will compose similar structure |
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | Per-grain filter - 2 instances per grain (L/R) |
| Grain | dsp/include/krate/dsp/primitives/grain_pool.h | 1 | Base grain state - extend with filter fields |
| GrainPool | dsp/include/krate/dsp/primitives/grain_pool.h | 1 | Grain lifecycle management |
| GrainProcessor | dsp/include/krate/dsp/processors/grain_processor.h | 2 | Grain processing with pitch/envelope |
| GrainScheduler | dsp/include/krate/dsp/processors/grain_scheduler.h | 2 | Grain triggering timing |
| GrainParams | dsp/include/krate/dsp/processors/grain_processor.h | 2 | Base grain parameters - extend with filter params |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Audio buffer for grains |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | Cutoff randomization |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing |
| LinearRamp | dsp/include/krate/dsp/primitives/smoother.h | 1 | Freeze crossfade |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems
- [x] `specs/_architecture_/` - Component inventory (README.md, layer-3-systems.md)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (GranularFilter, FilteredGrain, GranularFilterParams) are unique and not found in codebase. The design follows the established GranularEngine pattern with clear extension points.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF::kMaxCutoffRatio | constant | `static constexpr float kMaxCutoffRatio = 0.495f` | Yes |
| SVF::kMinQ | constant | `static constexpr float kMinQ = 0.1f` | Yes |
| SVF::kMaxQ | constant | `static constexpr float kMaxQ = 30.0f` | Yes |
| Grain | fields | `float readPosition, playbackRate, envelopePhase, envelopeIncrement, amplitude, panL, panR; bool active, reverse; size_t startSample` | Yes |
| GrainPool::kMaxGrains | constant | `static constexpr size_t kMaxGrains = 64` | Yes |
| GrainPool | acquireGrain | `[[nodiscard]] Grain* acquireGrain(size_t currentSample) noexcept` | Yes |
| GrainPool | releaseGrain | `void releaseGrain(Grain* grain) noexcept` | Yes |
| GrainPool | activeGrains | `[[nodiscard]] std::span<Grain* const> activeGrains() noexcept` | Yes |
| GrainProcessor | initializeGrain | `void initializeGrain(Grain& grain, const GrainParams& params) noexcept` | Yes |
| GrainProcessor | processGrain | `[[nodiscard]] std::pair<float, float> processGrain(Grain& grain, const DelayLine& delayBufferL, const DelayLine& delayBufferR) noexcept` | Yes |
| GrainProcessor | isGrainComplete | `[[nodiscard]] bool isGrainComplete(const Grain& grain) const noexcept` | Yes |
| GrainScheduler | process | `[[nodiscard]] bool process() noexcept` | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/grain_pool.h` - Grain struct, GrainPool class
- [x] `dsp/include/krate/dsp/processors/grain_processor.h` - GrainProcessor, GrainParams
- [x] `dsp/include/krate/dsp/processors/grain_scheduler.h` - GrainScheduler
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp
- [x] `dsp/include/krate/dsp/systems/granular_engine.h` - GranularEngine (reference)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SVF | Q range is 0.1-30, spec says 0.5-20 | Clamp to spec range (0.5-20) before passing to SVF |
| SVF | Cutoff max is sampleRate * 0.495 | Clamp after randomization |
| GrainProcessor::processGrain | Returns {L, R} with envelope and pan already applied | CANNOT use processGrain - must duplicate grain processing logic to intercept between envelope and pan |
| Grain struct | Pan gains (panL, panR) already computed in initializeGrain | Filter must process sampleL/sampleR directly AFTER envelope, BEFORE multiplying by panL/panR |

## Layer 0 Candidate Analysis

*For Layer 3 features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | No new utilities needed | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateRandomizedCutoff | Specific to this component, uses internal state |

**Decision**: No Layer 0 extraction needed. The octave-to-ratio calculation is a simple `std::pow(2.0f, octaves)` that doesn't warrant a utility function.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from spec.md):
- TimeVaryingCombBank (Layer 3) - may share per-voice filter patterns
- FilterFeedbackMatrix (Layer 3) - already exists, different architecture
- VowelSequencer (Layer 3) - different application (step-based)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Per-grain filter concept | MEDIUM | TimeVaryingCombBank | Keep local - different voice/grain lifecycle |
| Cutoff randomization in octaves | LOW | Unique to granular | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | GranularEngine composition is feature-specific |
| Keep filter state in custom struct | Grain struct extension pattern is feature-specific |

## Project Structure

### Documentation (this feature)

```text
specs/102-granular-filter/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── granular_filter.h    # New: GranularFilter class (Layer 3)
└── tests/
    └── unit/
        └── systems/
            └── granular_filter_test.cpp  # New: Unit tests
```

**Structure Decision**: Single header file in systems/ following GranularEngine pattern. No separate .cpp needed as this is a template-like header-only component.

## Complexity Tracking

No Constitution Check violations requiring justification.

---

## Design Decisions

### 1. Composition vs Inheritance

**Decision**: Composition - create a new GranularFilter class that internally manages GranularEngine-like components with filter extensions.

**Rationale**:
- GranularEngine has private members and complex internal state
- Extending via composition gives full control over filter integration point
- Allows modifying the processGrain signal flow (filter after pitch, before pan)
- Cleaner than trying to inject filter processing into existing GrainProcessor

### 2. Filter State Storage

**Decision**: Create a parallel array of SVF pairs (not extend Grain struct).

**Rationale**:
- Grain struct is in primitives/ (Layer 1) - should not depend on SVF
- Arrays of SVF pairs indexed by grain slot avoid modifying existing components
- Reset filter when grain slot is acquired (matches grain lifecycle)
- Memory: 128 SVF instances * ~80 bytes each = ~10KB (acceptable)

### 3. Cutoff Randomization Timing

**Decision**: Randomize cutoff when grain is triggered (not continuously modulated).

**Rationale**:
- Matches spec FR-003 (cutoff randomization per grain)
- Simpler implementation
- More predictable behavior
- Continuous modulation would be a different feature (filter envelope)

### 4. Signal Flow Modification

**Decision**: Duplicate GrainProcessor::processGrain logic in GranularFilter to insert filter between envelope and pan.

**Rationale**:
- Current processGrain applies envelope AND pan atomically - cannot intercept
- Need filter to process individual L/R samples AFTER envelope, BEFORE pan (spec FR-012)
- GranularFilter manages complete per-grain processing: read → pitch → envelope → **filter sampleL/sampleR** → pan
- Filter processes the individual sample values (sampleL, sampleR), NOT the processGrain return value
- Can reuse existing GrainEnvelope::lookup() for envelope calculation

### 5. Filter Parameter Interface

**Decision**: Add filter parameters to GranularFilter class (not GrainParams).

**Rationale**:
- Base cutoff, Q, type, randomization amount are global parameters
- Only per-grain cutoff varies (via randomization)
- Keeps GrainParams unchanged (Layer 2 boundary)

---

## Implementation Approach

### Step 1: FilteredGrainState Structure

```cpp
struct FilteredGrainState {
    SVF filterL;           // Left channel filter
    SVF filterR;           // Right channel filter
    float cutoffHz;        // This grain's randomized cutoff
    bool filterEnabled;    // Whether this grain uses filtering
};
```

### Step 2: GranularFilter Class Structure

```cpp
class GranularFilter {
public:
    void prepare(double sampleRate, float maxDelaySeconds = 2.0f) noexcept;
    void reset() noexcept;

    // Existing GranularEngine interface
    void setGrainSize(float ms) noexcept;
    void setDensity(float grainsPerSecond) noexcept;
    void setPitch(float semitones) noexcept;
    // ... all other GranularEngine parameters ...

    // New filter parameters
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterType(SVFMode mode) noexcept;
    void setCutoffRandomization(float octaves) noexcept;

    void process(float inputL, float inputR, float& outputL, float& outputR) noexcept;

    void seed(uint32_t seedValue) noexcept;

private:
    // Granular components (same as GranularEngine)
    DelayLine delayL_, delayR_;
    GrainPool pool_;
    GrainScheduler scheduler_;
    GrainProcessor processor_;

    // Filter state per grain slot
    std::array<FilteredGrainState, GrainPool::kMaxGrains> filterStates_;

    // Global filter parameters
    bool filterEnabled_ = true;
    float baseCutoffHz_ = 1000.0f;
    float resonanceQ_ = SVF::kButterworthQ;
    SVFMode filterType_ = SVFMode::Lowpass;
    float cutoffRandomizationOctaves_ = 0.0f;

    // RNG for cutoff randomization
    Xorshift32 rng_{54321};
};
```

### Step 3: Modified Grain Processing

**CRITICAL**: Filter processes individual sample values (sampleL, sampleR) AFTER envelope application, BEFORE pan multiplication. This is NOT using processGrain's return value.

```cpp
// In process():
for (size_t i = 0; i < pool_.activeCount(); ++i) {
    Grain* grain = activeGrains[i];
    size_t slotIndex = getGrainSlotIndex(grain);  // Pointer arithmetic to find slot

    // 1. Read from delay buffer with pitch shifting
    float sampleL = delayL_.readLinear(grain->readPosition);
    float sampleR = delayR_.readLinear(grain->readPosition);

    // 2. Apply envelope (envelope * amplitude to each sample)
    float envelope = GrainEnvelope::lookup(grain->envelopePhase, envelopeType_);
    sampleL *= envelope * grain->amplitude;
    sampleR *= envelope * grain->amplitude;

    // 3. Apply filter to individual samples (NEW - AFTER envelope, BEFORE pan)
    //    Filter processes sampleL and sampleR independently (true stereo)
    if (filterEnabled_ && filterStates_[slotIndex].filterEnabled) {
        sampleL = filterStates_[slotIndex].filterL.process(sampleL);
        sampleR = filterStates_[slotIndex].filterR.process(sampleR);
    }

    // 4. Apply panning (pan gains applied AFTER filtering)
    sumL += sampleL * grain->panL;
    sumR += sampleR * grain->panR;

    // Advance grain state (readPosition, envelopePhase)...
}
```

---

## Test Strategy

### Unit Tests

1. **Per-grain filter independence**: Verify grains with different cutoffs produce different spectra
2. **Cutoff randomization distribution**: Verify uniform distribution over octave range
3. **Filter type selection**: Verify LP/HP/BP/Notch produce correct frequency responses
4. **Q clamping**: Verify Q is clamped to 0.5-20 range
5. **Cutoff clamping**: Verify cutoff clamped to 20Hz-Nyquist*0.495 after randomization
6. **Deterministic seeding**: Verify same seed produces identical output
7. **Filter bypass**: Verify bypass mode matches GranularEngine output (bit-identical when seeded)
8. **Filter state reset**: Verify no artifacts from previous grain's state
9. **Existing parameter compatibility**: All GranularEngine parameters work correctly

### Performance Tests

1. **64 grains @ 48kHz**: Measure CPU usage, verify < 5% on reference system

### Spectral Tests

1. **FFT verification**: Process white noise through filter, verify frequency response
2. **Per-grain spectral difference**: Verify grains with different cutoffs have measurably different spectra

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| CPU budget exceeded with 128 SVF instances | Low | High | SVF is very efficient (~10 muls/sample). Budget allows ~1000 SVF/sample at 48kHz |
| Memory pressure from filter state | Low | Low | ~10KB total, well within budget |
| Filter instability at extreme settings | Low | Medium | SVF internally clamps parameters, Q range limited to 0.5-20 |
| Grain slot indexing complexity | Medium | Medium | Use parallel array indexed by grain position in pool array |

---

## Post-Design Constitution Re-Check

*Re-verified after Phase 1 design*

**Principle II (Real-Time Safety)**:
- [x] All allocations in prepare()
- [x] Process path is noexcept
- [x] No dynamic memory in process()
- [x] SVF::process() is noexcept and real-time safe

**Principle IX (Layer Architecture)**:
- [x] GranularFilter in systems/ (Layer 3)
- [x] Depends only on Layers 0-2
- [x] No circular dependencies

**Principle XIV (ODR Prevention)**:
- [x] All new types are unique
- [x] No conflicts with existing components

All gates pass. Implementation can proceed.
