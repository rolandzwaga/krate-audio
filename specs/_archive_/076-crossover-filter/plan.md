# Implementation Plan: Crossover Filter (Linkwitz-Riley)

**Branch**: `076-crossover-filter` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/076-crossover-filter/spec.md`

## Summary

Implement Layer 2 DSP processors for Linkwitz-Riley crossover filters providing phase-coherent multiband signal splitting. Three classes will be implemented:

1. **CrossoverLR4** - 2-way LR4 (24dB/oct) crossover using 4 Biquad filters (2 LP + 2 HP cascaded)
2. **Crossover3Way** - 3-band split (Low/Mid/High) composing two CrossoverLR4 instances
3. **Crossover4Way** - 4-band split (Sub/Low/Mid/High) composing three CrossoverLR4 instances

Key technical approach:
- Reuse existing Biquad from `primitives/biquad.h` configured with Butterworth Q (0.7071)
- Reuse OnePoleSmoother from `primitives/smoother.h` for frequency parameter smoothing
- Lock-free atomic parameters for thread-safe UI/audio thread interaction
- Configurable TrackingMode for coefficient recalculation strategy

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Biquad (Layer 1), OnePoleSmoother (Layer 1), db_utils.h (Layer 0)
**Storage**: N/A (stateless parameters, filter state in members)
**Testing**: Catch2 (unit tests, frequency response verification)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library (Layer 2 processor)
**Performance Goals**: <100ns per sample for CrossoverLR4 (per SC-010)
**Constraints**: Zero allocations in process(), noexcept on all audio methods
**Scale/Scope**: Single header with three classes, ~500 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] N/A - This is a DSP library component, not plugin code

**Principle II (Real-Time Audio Thread Safety):**
- [x] All processing methods will be noexcept
- [x] No allocations after prepare() - filter arrays pre-sized
- [x] Lock-free atomic parameters for thread safety
- [x] No exceptions, mutexes, or I/O in audio path

**Principle III (Modern C++ Standards):**
- [x] C++20 features where beneficial (std::atomic, designated initializers)
- [x] RAII for all resource management
- [x] constexpr where applicable

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code
- [x] Uses existing cross-platform primitives (Biquad, OnePoleSmoother)

**Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor depending only on Layer 0-1
- [x] No circular dependencies

**Principle X (DSP Processing Constraints):**
- [x] Uses Butterworth Q for LR4 characteristic (not arbitrary Q)
- [x] Denormal prevention via Biquad's built-in flush-to-zero

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: CrossoverLR4, Crossover3Way, Crossover4Way, TrackingMode

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| CrossoverLR4 | `grep -r "class CrossoverLR4" dsp/ plugins/` | No | Create New |
| Crossover3Way | `grep -r "class Crossover3Way" dsp/ plugins/` | No | Create New |
| Crossover4Way | `grep -r "class Crossover4Way" dsp/ plugins/` | No | Create New |
| TrackingMode | `grep -r "TrackingMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all functionality provided by existing primitives)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Biquad | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | LP/HP filter stages for crossover |
| BiquadCoefficients | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Coefficient calculation |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Frequency parameter smoothing |
| FilterType | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | FilterType::Lowpass, FilterType::Highpass |
| kButterworthQ | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Q value (0.7071) for LR4 stages |
| kTwoPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Angular frequency calculation (tests) |
| flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal prevention (handled by Biquad) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (checked multimode_filter.h pattern)
- [x] `specs/_architecture_/` - Component inventory (no crossover listed)
- [x] `specs/FLT-ROADMAP.md` - Confirmed crossover is planned but not implemented

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (CrossoverLR4, Crossover3Way, Crossover4Way, TrackingMode) do not exist in the codebase. The search confirmed no existing crossover filter implementation. Only references found were in FLT-ROADMAP.md and linkwitzRileyQ() function documentation in biquad.h.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| kButterworthQ | constant | `inline constexpr float kButterworthQ = 0.7071067811865476f` | Yes |
| FilterType | enum | `enum class FilterType : uint8_t { Lowpass, Highpass, ... }` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class, FilterType enum, kButterworthQ constant
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/processors/multimode_filter.h` - Reference for Layer 2 API patterns

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Method is `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | Uses `configure()` not `prepare()` | `smoother.configure(timeMs, sampleRate)` |
| Biquad | Uses `configure()` for parameters | `biquad.configure(type, freq, Q, gain, sr)` |
| kButterworthQ | Is a constant, not function | `Q = kButterworthQ` (not `butterworthQ()` unless for cascaded stages) |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | All needed utilities exist in Layer 0/1 | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Frequency clamping | Simple min/max, uses sample rate from instance |
| Coefficient hysteresis check | TrackingMode-specific, internal implementation detail |

**Decision**: No new Layer 0 utilities needed. All functionality is covered by existing primitives.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- MultimodeFilter - Already exists, different purpose (single-band filtering)
- EnvelopeFilter - May use crossover for pre-filtering bands
- FormantFilter - Different topology, unlikely to share code

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| CrossoverLR4 | HIGH | Crossover3Way, Crossover4Way, MultibandProcessor (Layer 3) | Keep in this file, reused within |
| Crossover3Way | MEDIUM | MultibandCompressor, MultibandSaturation (Layer 3) | Keep in this file |
| TrackingMode | LOW | Potentially other filters with modulation | Keep in this file (enum) |

### Detailed Analysis (for HIGH potential items)

**CrossoverLR4** provides:
- Phase-coherent 2-way band splitting
- Smoothed frequency parameter with configurable tracking
- Thread-safe atomic parameter updates

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Crossover3Way | YES | Directly composes two CrossoverLR4 instances |
| Crossover4Way | YES | Directly composes three CrossoverLR4 instances |
| EnvelopeFilter | MAYBE | Could use for pre-filtering if multiband |

**Recommendation**: Keep CrossoverLR4 in the crossover_filter.h file alongside 3-way and 4-way variants. This is the natural organization since they are tightly coupled.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Single header file | All three classes are tightly coupled and typically used together |
| TrackingMode enum in same file | Only used by crossover classes, no other consumers |

### Review Trigger

After implementing **MultibandProcessor (Layer 3)**, review this section:
- [ ] Does MultibandProcessor need CrossoverLR4/3Way/4Way? -> Already designed for this
- [ ] Any additional tracking modes needed? -> Evaluate based on modulation requirements
- [ ] Any duplicated code? -> Unlikely given clean composition

## Project Structure

### Documentation (this feature)

```text
specs/076-crossover-filter/
├── spec.md              # Feature specification (exists)
├── plan.md              # This file
├── research.md          # Phase 0 output (to be created)
├── data-model.md        # Phase 1 output (to be created)
├── quickstart.md        # Phase 1 output (to be created)
├── contracts/           # Phase 1 output (to be created)
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── crossover_filter.h    # New: CrossoverLR4, Crossover3Way, Crossover4Way
└── tests/
    └── unit/
        └── processors/
            └── crossover_filter_test.cpp  # New: Test file
```

**Structure Decision**: Single header in processors/ following the MultimodeFilter pattern. Tests in corresponding test directory.

## Complexity Tracking

> **No violations identified** - All Constitution checks pass.

---

## Phase 0: Research Summary

### Research Tasks Completed

Based on the spec and codebase analysis, all technical decisions are resolved:

#### R1: LR4 Filter Topology
- **Decision**: Two cascaded Butterworth biquads per path (LP or HP)
- **Rationale**: LR4 = squared Butterworth response. Two stages with Q=0.7071 each produces the -6dB crossover point and 24dB/oct slope
- **Alternatives considered**: Single LR filter formulas exist but cascaded Butterworth is simpler and uses existing Biquad primitive

#### R2: Coefficient Recalculation Strategy
- **Decision**: TrackingMode enum with Efficient (0.1Hz hysteresis) and HighAccuracy (per-sample) options
- **Rationale**: Efficient mode reduces CPU when frequency is stable; HighAccuracy mode ensures bit-perfect modulation tracking
- **Alternatives considered**: Always per-sample (too expensive), always hysteresis (not accurate enough for critical modulation)

#### R3: Thread Safety Approach
- **Decision**: std::atomic<float> for frequency parameters, std::atomic<int> for TrackingMode enum
- **Rationale**: Lock-free atomics allow UI thread to update parameters while audio thread reads without blocking
- **Alternatives considered**: std::mutex (violates real-time constraints), single-writer assumption (not safe with DAW automation)

#### R4: 3-Way/4-Way Topology
- **Decision**: Serial cascaded CrossoverLR4 instances
- **Rationale**:
  - 3-Way: Input -> CrossoverLR4#1 (lowMid) -> Low + High#1 -> High#1 -> CrossoverLR4#2 (midHigh) -> Mid + High
  - 4-Way: Same pattern extended with third crossover
- **Alternatives considered**: Parallel topology (doesn't sum flat), Butterworth crossover (not -6dB at crossover)

#### R5: Denormal Handling
- **Decision**: Rely on Biquad's built-in denormal prevention (flushDenormal in state updates)
- **Rationale**: Biquad already calls flushDenormal() on z1_ and z2_ state variables, no additional handling needed
- **Alternatives considered**: Additional flush calls (redundant CPU cost)

#### R6: Sample Rate Change Handling
- **Decision**: Reset all filter states and reinitialize coefficients on prepare()
- **Rationale**: Clean slate prevents artifacts from stale state at different sample rates
- **Alternatives considered**: State preservation (mathematically incorrect when sample rate changes)

### Existing Component Analysis

| Component | Status | Notes |
|-----------|--------|-------|
| Biquad | Ready to use | Provides configure(), process(), processBlock(), reset() |
| OnePoleSmoother | Ready to use | Provides configure(), setTarget(), process(), snapTo() |
| kButterworthQ | Ready to use | 0.7071067811865476f constant |
| FilterType | Ready to use | FilterType::Lowpass, FilterType::Highpass |

### Performance Estimates

| Operation | Estimated Cost | Budget |
|-----------|----------------|--------|
| CrossoverLR4::process() | ~40ns (4 biquads @ ~10ns each) | <100ns (SC-010) |
| Crossover3Way::process() | ~80ns (2 CrossoverLR4) | N/A |
| Crossover4Way::process() | ~120ns (3 CrossoverLR4) | N/A |
| Coefficient recalc | ~200ns | Once per frequency change (Efficient mode) |

---

## Phase 1: Design Artifacts

### Data Model

See [data-model.md](data-model.md) for detailed entity specifications.

**Key Entities:**

```cpp
// Coefficient recalculation strategy
enum class TrackingMode : uint8_t {
    Efficient,      // 0.1Hz hysteresis (default)
    HighAccuracy    // Per-sample during smoothing
};

// Output structures
struct CrossoverLR4Outputs { float low; float high; };
struct Crossover3WayOutputs { float low; float mid; float high; };
struct Crossover4WayOutputs { float sub; float low; float mid; float high; };

// Main classes
class CrossoverLR4;    // 2-way LR4 crossover
class Crossover3Way;   // 3-band splitter
class Crossover4Way;   // 4-band splitter
```

### API Contracts

See [contracts/](contracts/) directory for detailed API specifications.

**CrossoverLR4 API Summary:**
```cpp
class CrossoverLR4 {
    void prepare(double sampleRate);
    void setCrossoverFrequency(float hz);
    void setSmoothingTime(float ms);
    void setTrackingMode(TrackingMode mode);
    CrossoverLR4Outputs process(float input) noexcept;
    void processBlock(const float* input, float* low, float* high, size_t numSamples) noexcept;
    void reset() noexcept;
};
```

### Quickstart

See [quickstart.md](quickstart.md) for usage examples.

---

## Implementation Checklist (Phase 2 - Tasks)

The following task groups will be generated in `tasks.md` by `/speckit.tasks`:

1. **Task Group 1: TrackingMode Enum and Output Structs**
   - Define TrackingMode enum
   - Define CrossoverLR4Outputs, Crossover3WayOutputs, Crossover4WayOutputs structs
   - Write unit tests for struct default values

2. **Task Group 2: CrossoverLR4 Core Implementation**
   - Implement CrossoverLR4 class with 4 Biquad members
   - Implement prepare() with sample rate initialization
   - Implement process() with LR4 filtering (2 LP + 2 HP cascaded)
   - Write frequency response tests (flat sum, -6dB at crossover, 24dB/oct slope)

3. **Task Group 3: CrossoverLR4 Parameter Smoothing**
   - Add OnePoleSmoother for frequency parameter
   - Implement setCrossoverFrequency() with atomic write
   - Implement setSmoothingTime()
   - Implement TrackingMode coefficient recalculation
   - Write click-free modulation tests

4. **Task Group 4: CrossoverLR4 Block Processing and Thread Safety**
   - Implement processBlock() for buffer processing
   - Implement reset() for state clearing
   - Add std::atomic members for thread safety
   - Write thread safety tests

5. **Task Group 5: Crossover3Way Implementation**
   - Implement Crossover3Way composing two CrossoverLR4 instances
   - Implement frequency ordering auto-clamp
   - Write flat sum tests for 3-band output

6. **Task Group 6: Crossover4Way Implementation**
   - Implement Crossover4Way composing three CrossoverLR4 instances
   - Implement frequency ordering auto-clamp
   - Write flat sum tests for 4-band output

7. **Task Group 7: Edge Cases and Stability**
   - Test frequency clamping (20Hz min, Nyquist/2 max)
   - Test overlapping frequencies in multi-way crossovers
   - Test sample rate change handling
   - Test stability under extreme parameters

8. **Task Group 8: Performance Validation**
   - Benchmark CrossoverLR4 per-sample cost (<100ns target)
   - Verify TrackingMode::Efficient reduces coefficient updates
   - Verify zero allocations in process path

9. **Task Group 9: Documentation and Architecture Update**
   - Add crossover_filter.h to layer-2-processors.md
   - Update README.md in architecture
   - Final code review and cleanup

---

## Constitution Re-Check (Post-Design)

**Principle II (Real-Time Audio Thread Safety):**
- [x] All processing methods are noexcept and allocation-free
- [x] Lock-free atomics for parameter updates
- [x] No exceptions, mutexes, or I/O in audio path

**Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor depending only on Layer 0-1 (Biquad, OnePoleSmoother)
- [x] No circular dependencies

**Principle X (DSP Processing Constraints):**
- [x] Uses Butterworth Q (0.7071) for LR4 stages
- [x] Denormal prevention via Biquad's built-in handling

**All gates pass. Ready for Phase 2 task generation.**

---

## Artifacts Generated

| Artifact | Path | Status |
|----------|------|--------|
| Implementation Plan | `specs/076-crossover-filter/plan.md` | Complete |
| Research Summary | (inline in plan.md) | Complete |
| Data Model | `specs/076-crossover-filter/data-model.md` | To be created |
| API Contracts | `specs/076-crossover-filter/contracts/` | To be created |
| Quickstart | `specs/076-crossover-filter/quickstart.md` | To be created |
| Tasks | `specs/076-crossover-filter/tasks.md` | To be created by /speckit.tasks |
