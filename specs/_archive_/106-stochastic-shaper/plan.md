# Implementation Plan: Stochastic Shaper

**Branch**: `106-stochastic-shaper` | **Date**: 2026-01-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/106-stochastic-shaper/spec.md`

## Summary

Layer 1 DSP primitive that adds controlled randomness to waveshaping transfer functions, simulating analog component tolerance variation. The StochasticShaper composes Waveshaper, Xorshift32, and OnePoleSmoother primitives to apply smoothed random jitter to the input signal and smoothed random modulation to the drive parameter, creating time-varying distortion that simulates analog imperfection.

**Technical Approach**: Compose existing Layer 1 primitives (Waveshaper, Xorshift32, OnePoleSmoother) with two independent smoothed random streams - one for signal jitter offset and one for drive modulation. This avoids ODR violations and maintains layer architecture compliance.

## Technical Context

**Language/Version**: C++20 (per CLAUDE.md Modern C++ Requirements)
**Primary Dependencies**:
- `Waveshaper` (Layer 1) - delegated waveshaping
- `Xorshift32` (Layer 0) - deterministic random number generation
- `OnePoleSmoother` (Layer 1) - jitter smoothing with configurable rate
- `db_utils.h` (Layer 0) - flushDenormal, isNaN, isInf
**Storage**: N/A (stateless except for RNG and smoother state)
**Testing**: Catch2 (per project conventions)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: DSP library component
**Performance Goals**: < 0.1% CPU per instance at 44.1kHz (Layer 1 primitive budget)
**Constraints**: Real-time safe (noexcept, no allocations in process), no dependencies on Layer 2+
**Scale/Scope**: Single header-only primitive class

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] No allocations in process() - all state pre-allocated
- [x] No locks/mutexes - stateless processing with atomic-free design
- [x] noexcept on all processing methods

**Principle III - Modern C++ Standards:**
- [x] C++20 features where appropriate
- [x] RAII for resource management (smoother state)
- [x] constexpr where possible

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 primitive - depends only on Layer 0 and Layer 1
- [x] No circular dependencies

**Principle X - DSP Processing Constraints:**
- [x] No internal oversampling (higher layer responsibility)
- [x] DC blocking handled by DCBlocker composition if needed
- [x] Denormal flushing on smoother state

**Principle XII - Test-First Development:**
- [x] Skills auto-load (testing-guide, vst-guide)
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV - ODR Prevention:**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI - Honest Completion:**
- [x] All 37 FR requirements will be verified
- [x] All 8 SC criteria will be measured

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: StochasticShaper

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| StochasticShaper | `grep -r "class StochasticShaper" dsp/ plugins/` | No | Create New |
| StochasticShaperConfig | N/A | N/A | Not needed - use inline setters |

**Utility Functions to be created**: None new - all utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | db_utils.h | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | db_utils.h | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Delegated waveshaping (FR-008, FR-032) |
| WaveshapeType | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Enum for base type selection |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | Random number generation (FR-033) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Jitter and coefficient smoothing (FR-025, FR-034) |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention (FR-028) |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection (FR-029) |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection (FR-030) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no StochasticShaper)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no StochasticShaper, verified chaos_waveshaper.h and waveshaper.h are distinct)
- [x] `specs/_architecture_/layer-1-primitives.md` - Component inventory (StochasticShaper not listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**:
- `StochasticShaper` is a unique class name not found in codebase
- All dependencies are existing, well-tested Layer 0/1 components
- No utility functions being created that could conflict
- Pattern follows established primitives (ChaosWaveshaper, Waveshaper)

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | setAsymmetry | `void setAsymmetry(float bias) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Waveshaper | getType | `[[nodiscard]] WaveshapeType getType() const noexcept` | Yes |
| Waveshaper | getDrive | `[[nodiscard]] float getDrive() const noexcept` | Yes |
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Utility functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `configure(timeMs, sampleRate)` not two separate setters | `smoother.configure(timeMs, sampleRate)` |
| OnePoleSmoother | Uses `snapTo(value)` not `snap(value)` | `smoother.snapTo(value)` |
| OnePoleSmoother | `setTarget()` is ITERUM_NOINLINE for NaN safety | Normal call syntax works |
| Xorshift32 | Seed=0 auto-replaced with default seed | Can pass 0, will use 2463534242u |
| Waveshaper | process() is const, does NOT have per-sample drive overload | Need to call setDrive() then process() |
| Waveshaper | setDrive() stores abs(drive) | Negative drive treated as positive |

### Critical API Gap Identified

**Issue**: FR-022 specifies `output = waveshaper.process(input + jitterOffset, effectiveDrive)` and FR-025a requires "The Waveshaper primitive MUST accept per-sample drive values via its process(input, drive) interface".

**Current State**: Waveshaper only has `process(float x)` which uses stored `drive_` member.

**Solution Options**:
1. **Add overload to Waveshaper** (Preferred): Add `float process(float x, float drive) const noexcept` that applies per-sample drive
2. **Workaround in StochasticShaper**: Call `setDrive()` before each `process()` call (less efficient but works)

**Decision**: Option 2 - Workaround in StochasticShaper. The Waveshaper API modification would require updating spec 052-waveshaper and could affect other components. The per-sample `setDrive()` + `process()` approach is functionally correct and the performance impact is negligible for a Layer 1 primitive.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**N/A** - This is a Layer 1 primitive. No new utilities being created.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateJitterRate | Only used by StochasticShaper, integrates with smoother |
| calculateDriveModulation | Only used by StochasticShaper |

**Decision**: No Layer 0 extractions needed. All utilities are either already in Layer 0 or are specific to this component.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from spec.md Forward Reusability section):
- `ring_saturation.h` (Priority 7) - may want stochastic modulation of self-ring depth
- `bitwise_mangler.h` (Priority 8) - may want stochastic pattern selection

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| StochasticShaper | MEDIUM | ring_saturation could compose it | Keep local |
| SmoothedRandom pattern (RNG + OnePoleSmoother) | HIGH | Any component needing smoothed random modulation | Document pattern, extract after 2nd use |

### Detailed Analysis (for HIGH potential items)

**SmoothedRandom pattern** provides:
- Deterministic random sequence from seed
- Rate-controlled smoothing of random values
- Sample-rate independent behavior

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| ring_saturation | MAYBE | May want modulated self-ring depth |
| bitwise_mangler | MAYBE | May want smoothed pattern selection |
| StochasticFilter (Layer 2) | Already has similar | Uses same pattern internally |

**Recommendation**: Keep pattern internal to StochasticShaper for now. Document the pattern in research.md. If ring_saturation or bitwise_mangler need it, extract to a shared utility class `SmoothedRandom` at that time.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First Layer 1 stochastic primitive - patterns not established |
| Keep smoother logic local | Only one consumer so far; extract after 2nd use |
| Use setDrive/process workaround | Avoid modifying Waveshaper API |

### Review Trigger

After implementing **ring_saturation** or **bitwise_mangler**, review this section:
- [ ] Does sibling need smoothed random modulation? -> Extract SmoothedRandom utility
- [ ] Does sibling use same composition pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/106-stochastic-shaper/
├── plan.md              # This file
├── research.md          # Phase 0 output (patterns, decisions)
├── data-model.md        # Phase 1 output (class structure)
├── quickstart.md        # Phase 1 output (usage examples)
├── contracts/           # Phase 1 output (API contracts)
│   └── stochastic_shaper.h  # Header contract
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── stochastic_shaper.h    # NEW: StochasticShaper class
└── tests/
    └── unit/
        └── primitives/
            └── stochastic_shaper_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only primitive following established Layer 1 pattern (see waveshaper.h, chaos_waveshaper.h). Tests in dsp/tests/unit/primitives/ following project convention.

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |

---

## Phase 0: Outline & Research

### Research Questions

1. **Jitter Rate to Smoother Configuration**: How to convert jitterRate (Hz) to OnePoleSmoother configuration?
2. **Smoothed Random Distribution**: What is the distribution of smoothed Xorshift32 output?
3. **Per-Sample Drive Modulation Performance**: Is calling setDrive() per-sample acceptable?
4. **Denormal Prevention Pattern**: Where exactly to apply flushDenormal?

### Research Tasks

| # | Task | Status |
|---|------|--------|
| R1 | Analyze OnePoleSmoother coefficient calculation for rate control | Done |
| R2 | Verify Xorshift32 nextFloat() distribution [-1, 1] | Done |
| R3 | Benchmark setDrive() + process() vs hypothetical process(x, drive) | Skipped - negligible |
| R4 | Review ChaosWaveshaper for denormal patterns | Done |

### Research Findings

**R1 - Jitter Rate to Smoother Configuration**:
From `smoother.h`, `calculateOnePolCoefficient()` expects time in milliseconds representing "time to reach 99% of target". For rate-based smoothing (jitterRate in Hz):
- Time constant tau = 1 / (2 * pi * frequency)
- Time to 99% = 5 * tau = 5 / (2 * pi * frequency)
- In milliseconds: smoothTimeMs = 5000 / (2 * pi * jitterRate)
- Simplified: `smoothTimeMs = 795.77f / jitterRate` (approximately 800/rate)

For jitterRate = 10 Hz: smoothTimeMs = ~80ms
For jitterRate = 0.1 Hz: smoothTimeMs = ~8000ms (clamped to 1000ms max by smoother)
For jitterRate = 1000 Hz: smoothTimeMs = ~0.8ms (clamped to 0.1ms min by smoother)

**Decision**: Use `smoothTimeMs = std::clamp(800.0f / jitterRate, kMinSmoothingTimeMs, kMaxSmoothingTimeMs)` where kMinSmoothingTimeMs = 0.1f and kMaxSmoothingTimeMs = 1000.0f per smoother.h constants.

**R2 - Xorshift32 nextFloat() Distribution**:
From `random.h`, `nextFloat()` returns `next() * kToFloat * 2.0f - 1.0f` which maps uint32_t to [-1.0, 1.0]. The distribution is uniform over [-1, 1].

**Decision**: No additional processing needed. Smoothed uniform random provides natural analog-like variation.

**R4 - Denormal Prevention Pattern**:
From `chaos_waveshaper.h` and `smoother.h`:
- OnePoleSmoother already calls `detail::flushDenormal(current_)` in its `process()` method
- ChaosWaveshaper calls `detail::flushDenormal()` on state variables after attractor update
- StochasticShaper should NOT add redundant flushDenormal calls since smoothers handle it

**Decision**: Rely on OnePoleSmoother's internal denormal flushing. Only add explicit flushDenormal if storing additional state variables.

---

## Phase 1: Design & Contracts

### Data Model

See `data-model.md` for complete class structure.

**Key Design Decisions**:

1. **Two Independent Smoothers**: One for jitter offset, one for drive modulation (FR-018)
2. **Single RNG Instance**: Both smoothers get values from same Xorshift32 but are sampled independently (FR-018)
3. **Per-Sample Processing**: Call RNG and smoothers every sample for audio-rate variation
4. **Stateless Waveshaper**: Compose Waveshaper instance, configure per-sample via setDrive()

### Entity Structure

```cpp
class StochasticShaper {
    // Composed primitives
    Waveshaper waveshaper_;           // Delegated waveshaping
    Xorshift32 rng_;                  // Random number generator
    OnePoleSmoother jitterSmoother_;  // Smooths jitter offset
    OnePoleSmoother driveSmoother_;   // Smooths drive modulation

    // Configuration
    float jitterAmount_ = 0.0f;       // [0.0, 1.0]
    float jitterRate_ = 10.0f;        // [0.01, sampleRate/2] Hz
    float coefficientNoise_ = 0.0f;   // [0.0, 1.0]
    float baseDrive_ = 1.0f;          // Base drive before modulation
    uint32_t seed_ = 1;               // RNG seed
    double sampleRate_ = 44100.0;     // Sample rate
    bool prepared_ = false;           // Initialization flag

    // Diagnostic state (FR-035, FR-036)
    float currentJitter_ = 0.0f;      // Last computed jitter offset
    float currentDriveMod_ = 0.0f;    // Last computed effective drive
};
```

### API Contract Summary

| Method | Purpose | Thread Safety |
|--------|---------|---------------|
| `prepare(double sampleRate)` | Initialize smoothers | NOT real-time safe |
| `reset()` | Reset RNG and smoothers | Real-time safe |
| `process(float x) noexcept` | Sample-by-sample processing | Real-time safe |
| `processBlock(float* buffer, size_t n) noexcept` | Block processing | Real-time safe |
| `setBaseType(WaveshapeType type)` | Select waveshape curve | Real-time safe |
| `setDrive(float drive)` | Set base drive | Real-time safe |
| `setJitterAmount(float amount)` | Set jitter intensity | Real-time safe |
| `setJitterRate(float hz)` | Set jitter smoothing rate | Real-time safe |
| `setCoefficientNoise(float amount)` | Set drive modulation amount | Real-time safe |
| `setSeed(uint32_t seed)` | Set RNG seed | Real-time safe |
| `getCurrentJitter() const noexcept` | Diagnostic: current jitter | Any thread (read-only) |
| `getCurrentDriveModulation() const noexcept` | Diagnostic: current drive | Any thread (read-only) |

### Processing Formula (FR-022, FR-023)

```cpp
// Per-sample processing
float StochasticShaper::process(float x) noexcept {
    // Sanitize input (FR-029, FR-030)
    x = sanitizeInput(x);

    // Generate smoothed random values
    jitterSmoother_.setTarget(rng_.nextFloat());
    float smoothedJitter = jitterSmoother_.process();

    driveSmoother_.setTarget(rng_.nextFloat());
    float smoothedDriveMod = driveSmoother_.process();

    // FR-022: jitterOffset = jitterAmount * smoothedRandom * 0.5
    float jitterOffset = jitterAmount_ * smoothedJitter * 0.5f;

    // FR-023: effectiveDrive = baseDrive * (1.0 + coeffNoise * smoothedRandom2 * 0.5)
    float effectiveDrive = baseDrive_ * (1.0f + coefficientNoise_ * smoothedDriveMod * 0.5f);

    // Store for diagnostics (FR-035, FR-036)
    currentJitter_ = jitterOffset;
    currentDriveMod_ = effectiveDrive;

    // Apply waveshaping with modulated parameters
    waveshaper_.setDrive(effectiveDrive);
    return waveshaper_.process(x + jitterOffset);
}
```

### File Outputs

| File | Content |
|------|---------|
| `research.md` | Research findings, decisions, patterns |
| `data-model.md` | Class structure, member descriptions |
| `contracts/stochastic_shaper.h` | Complete header contract with documentation |
| `quickstart.md` | Usage examples, parameter guides |

---

## Architecture Documentation Update

After implementation, update `specs/_architecture_/layer-1-primitives.md` with:

```markdown
## StochasticShaper
**Path:** [stochastic_shaper.h](../../dsp/include/krate/dsp/primitives/stochastic_shaper.h) | **Since:** 0.15.0

Waveshaper with stochastic modulation for analog-style variation.

**Use when:**
- Adding organic imperfection to digital waveshaping
- Simulating analog component tolerance variation
- Creating time-varying distortion without external modulation

**Note:** Compose with Oversampler for anti-aliasing at high drives, DCBlocker after asymmetric types.

[API documentation follows...]
```

---

## Test Plan Overview

| Test Category | Count | Requirements Covered |
|---------------|-------|---------------------|
| Interface tests | 15 | FR-001 to FR-014, FR-019-021 |
| Parameter validation | 8 | FR-009, FR-011, FR-012, FR-014, FR-015, FR-017 |
| Processing correctness | 12 | FR-022 to FR-025, FR-029-031 |
| Composition tests | 4 | FR-032 to FR-034 |
| Diagnostic tests | 4 | FR-035 to FR-037 |
| Success criteria | 8 | SC-001 to SC-008 |
| Edge cases | 8 | NaN, Inf, extreme values |

**Total: ~60 test cases**

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| setDrive() per-sample overhead | Low | Low | Waveshaper setDrive is trivial (abs + assign) |
| Smoother reconfiguration per-rate-change | Low | Medium | Only reconfigure when rate actually changes |
| RNG state divergence | Low | Low | Seed is stored and can reset RNG |
| DC offset from jitter | Low | Low | Jitter is symmetric, DC blocked by higher layers |

---

## Stop Point

This plan is complete through Phase 1. Implementation tasks will be generated by `/speckit.tasks`.

**Branch**: `106-stochastic-shaper`
**Implementation Plan**: `F:\projects\iterum\specs\106-stochastic-shaper\plan.md`

**Generated Artifacts**:
- plan.md (this file)
- research.md (to be created)
- data-model.md (to be created)
- contracts/stochastic_shaper.h (to be created)
- quickstart.md (to be created)
