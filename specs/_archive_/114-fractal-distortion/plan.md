# Implementation Plan: FractalDistortion

**Branch**: `114-fractal-distortion` | **Date**: 2026-01-27 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/114-fractal-distortion/spec.md`

## Summary

Recursive multi-scale distortion processor with self-similar harmonic structure. This Layer 2 processor implements five modes (Residual, Multiband, Harmonic, Cascade, Feedback) of fractal-inspired distortion where each iteration level contributes progressively smaller amplitude content, creating complex evolving harmonic structures. Uses existing Waveshaper, Crossover4Way, ChebyshevShaper, Biquad, DCBlocker, and OnePoleSmoother primitives.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- Layer 0: chebyshev.h, sigmoid.h, db_utils.h, math_constants.h
- Layer 1: waveshaper.h, biquad.h, dc_blocker.h, smoother.h, chebyshev_shaper.h
- Layer 2: crossover_filter.h (Crossover4Way)

**Storage**: N/A (stateless except for filter state and feedback delay)
**Testing**: Catch2 via dsp_tests target
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (Layer 2 Processor)
**Performance Goals**: < 0.5% CPU for 8 iterations at 44.1kHz per channel (SC-001)
**Constraints**: Real-time safe (noexcept, no allocations in process), aliasing accepted per DST-ROADMAP
**Scale/Scope**: Single-channel processor, composable for stereo

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Constitution Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| II: Real-Time Safety | PASS | Design uses noexcept, pre-allocated arrays, no allocations in process |
| III: Modern C++ | PASS | C++20, RAII, constexpr constants |
| IX: Layered Architecture | PASS | Layer 2 processor using Layers 0-1 |
| X: DSP Constraints | PASS | Aliasing accepted (Digital Destruction aesthetic per DST-ROADMAP); DC blocking after saturation |
| XI: Performance Budget | PASS | Target < 0.5% CPU, within Layer 2 budget |
| XII: Test-First | PASS | Tests will be written before implementation |
| XIV: ODR Prevention | PASS | No existing FractalDistortion found; all types unique |
| XVI: Honest Completion | ACKNOWLEDGED | Will verify all FR-xxx and SC-xxx at completion |

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

**Classes/Structs to be created**: FractalDistortion, FractalMode (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FractalDistortion | `grep -r "class FractalDistortion" dsp/ plugins/` | No | Create New |
| FractalMode | `grep -r "FractalMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all math reuses Layer 0/1)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Waveshaper | primitives/waveshaper.h | 1 | Per-iteration saturation with setType/setDrive |
| WaveshapeType | primitives/waveshaper.h | 1 | Cascade mode per-level algorithm selection |
| Crossover4Way | processors/crossover_filter.h | 2 | Multiband mode 4-way Linkwitz-Riley splitting |
| ChebyshevShaper | primitives/chebyshev_shaper.h | 1 | Harmonic mode odd/even extraction |
| Chebyshev::Tn | core/chebyshev.h | 0 | Harmonic mode polynomial evaluation |
| Biquad | primitives/biquad.h | 1 | Frequency decay highpass filtering per level |
| DCBlocker | primitives/dc_blocker.h | 1 | Post-processing DC removal |
| OnePoleSmoother | primitives/smoother.h | 1 | Drive and mix parameter smoothing |
| Sigmoid::tanh | core/sigmoid.h | 0 | Tanh saturation for Residual/Multiband/Feedback |
| flushDenormal | core/db_utils.h | 0 | Denormal prevention |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (FractalDistortion, FractalMode) are unique and not found in codebase. Implementation reuses existing primitives without modification.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Crossover4Way | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| Crossover4Way | reset | `void reset() noexcept` | Yes |
| Crossover4Way | process | `[[nodiscard]] Crossover4WayOutputs process(float input) noexcept` | Yes |
| Crossover4Way | setSubLowFrequency | `void setSubLowFrequency(float hz) noexcept` | Yes |
| Crossover4Way | setLowMidFrequency | `void setLowMidFrequency(float hz) noexcept` | Yes |
| Crossover4Way | setMidHighFrequency | `void setMidHighFrequency(float hz) noexcept` | Yes |
| ChebyshevShaper | setHarmonicLevel | `void setHarmonicLevel(int harmonic, float level) noexcept` | Yes |
| ChebyshevShaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| Sigmoid::tanh | (free function) | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |
| flushDenormal | (free function) | `inline float flushDenormal(float value) noexcept` | Yes |

### Denormal Flushing Strategy (FR-049)

Denormals are flushed using `flushDenormal()` from `core/db_utils.h` after each saturate() call in all process methods:

```cpp
// In processResidual(), processFeedback(), processMultiband(), etc.:
for (int i = 0; i < iterations_; ++i) {
    float saturated = Sigmoid::tanh(input * scalePower * smoothedDrive);
    saturated = flushDenormal(saturated);  // Prevent CPU spikes from denormals
    // ... rest of processing
}
```

This ensures denormal values (very small floating-point numbers that cause CPU slowdown) are flushed to zero immediately after saturation operations where they are most likely to occur.

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class, WaveshapeType enum
- [x] `dsp/include/krate/dsp/processors/crossover_filter.h` - Crossover4Way, Crossover4WayOutputs
- [x] `dsp/include/krate/dsp/primitives/chebyshev_shaper.h` - ChebyshevShaper class
- [x] `dsp/include/krate/dsp/core/chebyshev.h` - Chebyshev::Tn, harmonicMix
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad, FilterType enum
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanh
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Waveshaper | Takes absolute value of drive internally | Pass positive drive values |
| Crossover4Way | Needs prepare() before processing | Call prepare() in FractalDistortion::prepare() |
| ChebyshevShaper | Harmonic indices are 1-based | setHarmonicLevel(1, ...) for fundamental |
| Biquad | configure() uses FilterType enum | FilterType::Highpass for frequencyDecay |
| OnePoleSmoother | Uses ITERUM_NOINLINE on setTarget | Normal call syntax works |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateBandIterations() | Single consumer, specific to Multiband mode |
| msToSamples() | Simple inline, class stores sampleRate_ |

**Decision**: No new Layer 0 utilities needed. All math operations reuse existing Layer 0 functions.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from DST-ROADMAP):
- SpectralDistortion (spectral_distortion.h) - frequency-domain processing
- FormantDistortion (formant_distortion.h) - formant manipulation
- TemporalDistortion (temporal_distortion.h) - time-based effects
- FeedbackDistortion (feedback_distortion.h) - feedback-based effects

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Multi-iteration loop pattern | MEDIUM | Other recursive processors | Keep local - wait for 2nd use |
| Odd/even harmonic separation | LOW | Unique to Harmonic mode | Keep local |
| Band iteration distribution | LOW | Specific to Multiband mode | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Each distortion processor has unique algorithm; composition preferred over inheritance |
| Keep iteration loop local | Pattern may vary between processors; extract only after concrete 2nd use |

### Review Trigger

After implementing **SpectralDistortion** or **TemporalDistortion**, review this section:
- [ ] Does sibling need multi-iteration processing? -> Consider shared pattern
- [ ] Does sibling use frequency decay? -> Consider shared highpass per-level utility

## Project Structure

### Documentation (this feature)

```text
specs/114-fractal-distortion/
├── plan.md              # This file
├── research.md          # Phase 0 output (below)
├── data-model.md        # Phase 1 output (below)
├── quickstart.md        # Phase 1 output (below)
├── contracts/           # Phase 1 output (N/A - no external API)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── fractal_distortion.h    # NEW: FractalDistortion class
└── tests/
    └── processors/
        └── fractal_distortion_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only processor in Layer 2 processors directory, matching existing patterns (granular_distortion.h, tube_stage.h, etc.).

## Complexity Tracking

> No Constitution violations. All design decisions follow established patterns.

---

# Phase 0: Research

## Research Tasks Completed

### 1. Existing Component Patterns (GranularDistortion Reference)

**Finding**: GranularDistortion provides an excellent template for FractalDistortion structure:
- `prepare(double sampleRate, size_t maxBlockSize)` pattern
- `reset()` for state clearing without reallocation
- Per-parameter setters with clamping and smoothing
- `process(float)` single-sample and `process(float*, size_t)` block variants
- Private helper methods for internal algorithms
- Constants for parameter ranges

**Decision**: Follow GranularDistortion's structural patterns for consistency.

### 2. Crossover4Way API for Multiband Mode

**Finding**: Crossover4Way provides:
- `setSubLowFrequency()`, `setLowMidFrequency()`, `setMidHighFrequency()` for band configuration
- Default frequencies: 80Hz, 300Hz, 3000Hz
- `process()` returns `Crossover4WayOutputs{sub, low, mid, high}`
- Uses Linkwitz-Riley 4th-order (LR4) crossovers for phase-coherent band splitting (flat-sum reconstruction)
- Needs `prepare(sampleRate)` before use

**Decision**: Use Crossover4Way with custom frequencies based on crossoverFrequency parameter. Set subLow=crossover/4, lowMid=crossover, midHigh=crossover*4 for pseudo-octave spacing (1:4:16 ratios between crossover points, optimized for bass/mid/high separation rather than true 1:2:4:8 octave spacing).

**Band indexing**: Band[0]=sub (lowest frequencies), Band[1]=low, Band[2]=mid, Band[3]=high (highest frequencies).

### 3. ChebyshevShaper for Harmonic Mode

**Finding**: ChebyshevShaper provides:
- `setHarmonicLevel(int harmonic, float level)` for individual control
- Harmonics 1-8 supported (1-based indexing)
- `process(float)` applies weighted polynomial mix
- Odd harmonics (1,3,5,7) vs Even harmonics (2,4,6,8) can be set independently

**Decision**: For Harmonic mode, use two ChebyshevShapers:
- One for odd harmonics (levels 1,3,5,7 set to 1.0)
- One for even harmonics (levels 2,4,6,8 set to 1.0)
- Apply different Waveshaper curves to each output

### 4. Frequency Decay Implementation

**Finding**: Per spec FR-025: level N highpassed at baseFrequency * (N+1) where baseFrequency = 200Hz.
- Level 0: no highpass (or 200Hz if frequencyDecay = 1.0)
- Level 1: 400Hz highpass
- Level 7: 1600Hz highpass

**Decision**: Use array of 8 Biquad filters configured as highpass. On prepare(), configure all with Q=0.707 (Butterworth). On process(), interpolate cutoff based on frequencyDecay parameter.

### 5. Feedback Mode State Buffer

**Finding**: FR-043-044 specify cross-level feedback where level[N-1] output feeds into level[N] computation.
- Need to store previous sample's iteration outputs
- Not a time-based delay - feedback is from previous sample's processing results

**Decision**: Use `std::array<float, kMaxIterations>` feedbackBuffer_ to store each level's output from the previous sample. On each new sample:
1. Read feedbackBuffer_[N-1] to get previous sample's level[N-1] output
2. Process current sample's level[N] using: `tanh((residual + feedbackBuffer_[N-1] * feedbackAmount) * scale * drive)`
3. Store current level[N] output back to feedbackBuffer_[N] for next sample

This creates sample-to-sample iteration coupling without needing DelayLine. The "cross-feeding" happens between iteration levels within consecutive samples.

### 6. Band Iteration Distribution Formula

**Finding**: FR-033 formula: `bandIterations[i] = max(1, round(baseIterations * bandIterationScale^(numBands - 1 - i)))`

With baseIterations=6, bandIterationScale=0.5:
- Band 3 (high): 6 * 0.5^0 = 6
- Band 2 (mid): 6 * 0.5^1 = 3
- Band 1 (low): 6 * 0.5^2 = 1.5 -> 2 (rounded)
- Band 0 (sub): 6 * 0.5^3 = 0.75 -> 1 (max 1)

**Decision**: Implementation verified against acceptance scenario. Formula is correct.

## Research Summary

All NEEDS CLARIFICATION items from spec have been resolved:
1. Crossover4Way API verified for Multiband mode
2. Frequency decay base frequency = 200Hz (from spec clarifications)
3. Harmonic mode uses ChebyshevShaper with separate odd/even processing
4. Multiband iteration distribution formula verified
5. Tanh waveshaper confirmed for base saturation

---

# Phase 1: Design

## Data Model

### FractalMode Enumeration

```cpp
enum class FractalMode : uint8_t {
    Residual = 0,   ///< Classic residual-based recursion (FR-005)
    Multiband = 1,  ///< Octave-band splitting with scaled iterations (FR-006)
    Harmonic = 2,   ///< Odd/even harmonic separation (FR-007)
    Cascade = 3,    ///< Different waveshaper per level (FR-008)
    Feedback = 4    ///< Cross-level feedback with delay (FR-009)
};
```

### FractalDistortion Class

```cpp
class FractalDistortion {
public:
    // Constants
    static constexpr int kMaxIterations = 8;
    static constexpr int kNumBands = 4;
    static constexpr float kMinIterations = 1;
    static constexpr float kMaxIterationsF = 8.0f;
    static constexpr float kMinScaleFactor = 0.3f;
    static constexpr float kMaxScaleFactor = 0.9f;
    static constexpr float kMinDrive = 1.0f;
    static constexpr float kMaxDrive = 20.0f;
    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;
    static constexpr float kMinFrequencyDecay = 0.0f;
    static constexpr float kMaxFrequencyDecay = 1.0f;
    static constexpr float kMinFeedbackAmount = 0.0f;
    static constexpr float kMaxFeedbackAmount = 0.5f;
    static constexpr float kDefaultCrossoverFrequency = 250.0f;
    static constexpr float kDefaultBandIterationScale = 0.5f;
    static constexpr float kBaseDecayFrequency = 200.0f;
    static constexpr float kSmoothingTimeMs = 10.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Mode Selection (FR-004 to FR-009)
    void setMode(FractalMode mode) noexcept;
    [[nodiscard]] FractalMode getMode() const noexcept;

    // Iteration Control (FR-010 to FR-012)
    void setIterations(int iterations) noexcept;
    [[nodiscard]] int getIterations() const noexcept;

    // Scale Factor (FR-013 to FR-015)
    void setScaleFactor(float scale) noexcept;
    [[nodiscard]] float getScaleFactor() const noexcept;

    // Drive Control (FR-016 to FR-018)
    void setDrive(float drive) noexcept;
    [[nodiscard]] float getDrive() const noexcept;

    // Mix Control (FR-019 to FR-022)
    void setMix(float mix) noexcept;
    [[nodiscard]] float getMix() const noexcept;

    // Frequency Decay (FR-023 to FR-025)
    void setFrequencyDecay(float decay) noexcept;
    [[nodiscard]] float getFrequencyDecay() const noexcept;

    // Multiband Mode (FR-030 to FR-033)
    void setCrossoverFrequency(float hz) noexcept;
    [[nodiscard]] float getCrossoverFrequency() const noexcept;
    void setBandIterationScale(float scale) noexcept;
    [[nodiscard]] float getBandIterationScale() const noexcept;

    // Harmonic Mode (FR-034 to FR-038)
    void setOddHarmonicCurve(WaveshapeType type) noexcept;
    void setEvenHarmonicCurve(WaveshapeType type) noexcept;
    [[nodiscard]] WaveshapeType getOddHarmonicCurve() const noexcept;
    [[nodiscard]] WaveshapeType getEvenHarmonicCurve() const noexcept;

    // Cascade Mode (FR-039 to FR-041)
    void setLevelWaveshaper(int level, WaveshapeType type) noexcept;
    [[nodiscard]] WaveshapeType getLevelWaveshaper(int level) const noexcept;

    // Feedback Mode (FR-042 to FR-045)
    void setFeedbackAmount(float amount) noexcept;
    [[nodiscard]] float getFeedbackAmount() const noexcept;

    // Processing (FR-046 to FR-050)
    [[nodiscard]] float process(float input) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Query
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Mode-specific processing
    [[nodiscard]] float processResidual(float input, float smoothedDrive) noexcept;
    [[nodiscard]] float processMultiband(float input, float smoothedDrive) noexcept;
    [[nodiscard]] float processHarmonic(float input, float smoothedDrive) noexcept;
    [[nodiscard]] float processCascade(float input, float smoothedDrive) noexcept;
    [[nodiscard]] float processFeedback(float input, float smoothedDrive) noexcept;

    // Helpers
    void updateCrossoverFrequencies() noexcept;
    void updateDecayFilters() noexcept;
    [[nodiscard]] int calculateBandIterations(int bandIndex) const noexcept;

    // Components
    std::array<Waveshaper, kMaxIterations> waveshapers_;       // Per-level waveshapers
    std::array<Biquad, kMaxIterations> decayFilters_;          // Per-level highpass for frequencyDecay
    Crossover4Way crossover_;                                   // Multiband mode splitting
    ChebyshevShaper oddHarmonicShaper_;                        // Harmonic mode odd extraction
    ChebyshevShaper evenHarmonicShaper_;                       // Harmonic mode even extraction
    Waveshaper oddCurveShaper_;                                // Harmonic mode odd saturation
    Waveshaper evenCurveShaper_;                               // Harmonic mode even saturation
    DCBlocker dcBlocker_;                                      // Post-processing DC removal
    OnePoleSmoother driveSmoother_;                            // Drive parameter smoothing
    OnePoleSmoother mixSmoother_;                              // Mix parameter smoothing

    // Feedback mode state
    std::array<float, kMaxIterations> feedbackBuffer_{};       // Stores previous sample's level outputs for cross-feeding

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Parameters
    FractalMode mode_ = FractalMode::Residual;
    int iterations_ = 4;
    float scaleFactor_ = 0.5f;
    float drive_ = 2.0f;
    float mix_ = 1.0f;
    float frequencyDecay_ = 0.0f;
    float crossoverFrequency_ = kDefaultCrossoverFrequency;
    float bandIterationScale_ = kDefaultBandIterationScale;
    WaveshapeType oddHarmonicCurve_ = WaveshapeType::Tanh;
    WaveshapeType evenHarmonicCurve_ = WaveshapeType::Tube;
    std::array<WaveshapeType, kMaxIterations> levelWaveshapers_;
    float feedbackAmount_ = 0.0f;
};
```

### Processing Algorithms

#### Residual Mode (FR-027 to FR-029)

```
level[0] = tanh(input * drive)
for N = 1 to iterations-1:
    residual = input - sum(level[0..N-1])
    level[N] = tanh(residual * scaleFactor^N * drive)
output = sum(all levels)
```

#### Multiband Mode (FR-030 to FR-033)

```
bands = crossover.process(input)  // {sub, low, mid, high}
bandIterations = [1, 2, 3, 6]     // calculated from formula
for each band:
    apply Residual algorithm with bandIterations[i] iterations
output = sum(processed bands)
```

#### Harmonic Mode (FR-034 to FR-038)

```
oddComponent = oddHarmonicShaper.process(input)   // T1, T3, T5, T7
evenComponent = evenHarmonicShaper.process(input) // T2, T4, T6, T8
processedOdd = oddCurveShaper.process(oddComponent * drive)
processedEven = evenCurveShaper.process(evenComponent * drive)
output = processedOdd + processedEven
```

#### Cascade Mode (FR-039 to FR-041)

```
level[0] = waveshapers_[0].process(input * drive)
for N = 1 to iterations-1:
    residual = input - sum(level[0..N-1])
    level[N] = waveshapers_[N].process(residual * scaleFactor^N * drive)
output = sum(all levels)
```

#### Feedback Mode (FR-042 to FR-045)

```
// feedbackBuffer_ stores previous sample's level outputs
level[0] = tanh(input * drive)
for N = 1 to iterations-1:
    residual = input - sum(level[0..N-1])
    feedbackIn = feedbackBuffer_[N-1] * feedbackAmount  // previous sample's level[N-1]
    level[N] = tanh((residual + feedbackIn) * scaleFactor^N * drive)
// Store current level outputs for next sample's feedback
for N = 0 to iterations-1:
    feedbackBuffer_[N] = level[N]
output = tanh(sum(all levels))  // soft-limit for bounded output (FR-045)
```

Note: feedbackBuffer_ is a `std::array<float, kMaxIterations>` that stores each level's output from the previous sample. This creates sample-to-sample iteration coupling - not a time-based delay.

## Contracts

N/A - This is an internal DSP component, not an external API. No OpenAPI/GraphQL contracts needed.

## Quickstart

### Basic Usage

```cpp
#include <krate/dsp/processors/fractal_distortion.h>

using namespace Krate::DSP;

// Create and prepare
FractalDistortion fractal;
fractal.prepare(44100.0, 512);

// Configure for basic fractal saturation
fractal.setMode(FractalMode::Residual);
fractal.setIterations(4);
fractal.setScaleFactor(0.5f);
fractal.setDrive(2.0f);
fractal.setMix(0.75f);

// Process audio
float output = fractal.process(inputSample);

// Or block processing
fractal.process(buffer, numSamples);
```

### Multiband Mode

```cpp
fractal.setMode(FractalMode::Multiband);
fractal.setIterations(6);
fractal.setCrossoverFrequency(250.0f);
fractal.setBandIterationScale(0.5f);
// Results in: sub=1, low=2, mid=3, high=6 iterations
```

### Harmonic Mode

```cpp
fractal.setMode(FractalMode::Harmonic);
fractal.setOddHarmonicCurve(WaveshapeType::Tanh);   // 3rd, 5th, 7th harmonics
fractal.setEvenHarmonicCurve(WaveshapeType::Tube);  // 2nd, 4th, 6th harmonics
```

### Cascade Mode

```cpp
fractal.setMode(FractalMode::Cascade);
fractal.setIterations(3);
fractal.setLevelWaveshaper(0, WaveshapeType::Tube);
fractal.setLevelWaveshaper(1, WaveshapeType::Tanh);
fractal.setLevelWaveshaper(2, WaveshapeType::HardClip);
```

### Feedback Mode

```cpp
fractal.setMode(FractalMode::Feedback);
fractal.setFeedbackAmount(0.3f);
fractal.setFeedbackDelay(10);  // samples
```

### Frequency Decay

```cpp
// Add high-frequency emphasis at deeper levels (all modes)
fractal.setFrequencyDecay(0.5f);
// Level 4 will be highpassed at 800Hz (200 * 4)
```

---

## Post-Design Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| II: Real-Time Safety | PASS | All processing noexcept, no allocations, pre-allocated arrays |
| III: Modern C++ | PASS | C++20, RAII, constexpr constants, std::array |
| IX: Layered Architecture | PASS | Layer 2 using only Layers 0-1 (except Crossover4Way from Layer 2) |
| X: DSP Constraints | PASS | DC blocking via DCBlocker, aliasing accepted per DST-ROADMAP |
| XI: Performance Budget | PASS | 8 iterations ~8 waveshaper calls + filters, within 0.5% budget |
| XII: Test-First | ACKNOWLEDGED | Tests defined in tasks.md |
| XIV: ODR Prevention | PASS | All types unique, verified via searches |
| XVI: Honest Completion | ACKNOWLEDGED | Implementation must satisfy all FR-xxx/SC-xxx |

**Gate Status**: PASSED - Ready for Phase 2 (task breakdown)

---

## Artifacts Generated

| Artifact | Status | Location |
|----------|--------|----------|
| plan.md | Complete | specs/114-fractal-distortion/plan.md |
| research.md | Inline | Included in plan.md Phase 0 section |
| data-model.md | Inline | Included in plan.md Phase 1 section |
| quickstart.md | Inline | Included in plan.md Phase 1 section |
| contracts/ | N/A | Internal DSP component, no external API |

## Next Steps

Run `/speckit.tasks` to generate the task breakdown in `specs/114-fractal-distortion/tasks.md`.
