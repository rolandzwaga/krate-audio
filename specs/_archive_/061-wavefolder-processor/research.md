# Research Notes: WavefolderProcessor

**Feature**: 061-wavefolder-processor
**Date**: 2026-01-14

## Research Questions

### 1. Buchla259 5-Stage Parallel Architecture

**Question**: How should the Buchla 259 Complex Waveform Generator's wavefolder be modeled?

**Decision**: Two sub-modes - Classic and Custom

**Classic Mode (Fixed Values)**:
- Thresholds: {0.2, 0.4, 0.6, 0.8, 1.0}
- Gains: {1.0, 0.8, 0.6, 0.4, 0.2}
- Thresholds scaled by 1/foldAmount to control intensity

**Custom Mode (User-Configurable)**:
- setBuchlaThresholds(array<float, 5>) for custom thresholds
- setBuchlaGains(array<float, 5>) for custom stage gains

**Rationale**: The classic Buchla 259 had fixed component values that created its distinctive timbre. Providing a Classic mode preserves authenticity, while Custom mode allows experimentation.

**Alternatives Considered**:
1. Single mode with fixed values - Rejected: limits creative flexibility
2. Per-stage exposed parameters - Rejected: too many parameters (10 total)
3. Interpolatable presets - Rejected: overcomplicates for minimal benefit

### 2. Symmetry Application

**Question**: How should symmetry (for even harmonic generation) be applied across different models?

**Decision**: Apply symmetry as DC offset BEFORE all wavefolding models (FR-025)

**Implementation**:
```cpp
float offsetInput = input + symmetry * (1.0f / foldAmount);
```

**Rationale**:
- Consistent behavior across all models (Simple, Serge, Buchla259, Lockhart)
- DC offset before nonlinear processing creates asymmetry -> even harmonics
- Scaling by 1/foldAmount keeps offset proportional to fold intensity

**Alternatives Considered**:
1. Fixed scale factor (e.g., 0.5) - Rejected: doesn't adapt to fold intensity
2. Per-model symmetry application - Rejected: inconsistent user experience
3. Post-fold asymmetric saturation - Rejected: different harmonic character

### 3. Performance Budget

**Question**: What is the acceptable CPU budget relative to existing processors?

**Decision**: Within 2x of TubeStage/DiodeClipper per mono instance (SC-005)

**Analysis**:
- TubeStage: 1 waveshaper + 1 DC blocker + 3 smoothers
- DiodeClipper: 1 diode function + 1 DC blocker + 5 smoothers
- WavefolderProcessor (non-Buchla): 1 wavefolder + 1 DC blocker + 3 smoothers
- WavefolderProcessor (Buchla259): 5 triangle folds + 1 DC blocker + 3 smoothers

**Rationale**: Buchla259 mode inherently requires ~5x the wavefold operations, but overall processing (including smoothers, DC blocker) brings total to ~2x baseline.

### 4. Infinity Handling

**Question**: How should +/- infinity input values be handled?

**Decision**: Infinity propagates through (same as NaN) - real-time safe, no branching

**Rationale**:
- Consistent with existing Wavefolder primitive behavior
- No additional branching in hot path
- "Garbage in, garbage out" philosophy for real-time safety
- DC blocker will also propagate (unbounded output acceptable)

**Alternatives Considered**:
1. Clamp infinity to large finite value - Rejected: adds branching
2. Return 0 for infinity - Rejected: inconsistent with NaN handling
3. Return NaN for infinity - Rejected: different from Wavefolder primitive

## Dependencies Verified

### Layer 0 (core/)

| Component | Header | Verified |
|-----------|--------|----------|
| WavefoldMath::triangleFold | `core/wavefold_math.h` | Yes |
| WavefoldMath::sineFold | `core/wavefold_math.h` | Yes |
| WavefoldMath::lambertW | `core/wavefold_math.h` | Yes |
| FastMath::fastTanh | `core/fast_math.h` | Yes |
| detail::isNaN | `core/db_utils.h` | Yes |
| detail::isInf | `core/db_utils.h` | Yes |

### Layer 1 (primitives/)

| Component | Header | Verified |
|-----------|--------|----------|
| Wavefolder | `primitives/wavefolder.h` | Yes |
| WavefoldType | `primitives/wavefolder.h` | Yes |
| DCBlocker | `primitives/dc_blocker.h` | Yes |
| OnePoleSmoother | `primitives/smoother.h` | Yes |

## Implementation Patterns from Reference Processors

### TubeStage Pattern (tube_stage.h)

```cpp
// Constants
static constexpr float kDefaultSmoothingMs = 5.0f;
static constexpr float kDCBlockerCutoffHz = 10.0f;

// Lifecycle
void prepare(double sampleRate, size_t maxBlockSize) noexcept;
void reset() noexcept;

// Processing
void process(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        // Advance smoothers
        // Check bypass
        // Apply processing chain
        // Mix blend
    }
}
```

### DiodeClipper Pattern (diode_clipper.h)

```cpp
// Multiple smoothers for all parameters
OnePoleSmoother driveSmoother_;
OnePoleSmoother mixSmoother_;
// ...

// Single sample processing extracted
[[nodiscard]] float processSample(float input) noexcept;

// Block processing calls per-sample
void process(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = processSample(buffer[i]);
    }
}
```

## Test Strategy (from tube_stage_test.cpp)

Test organization by User Story with Success Criteria tags:
- US1: Basic functionality [SC-xxx]
- US2-6: Parameter controls
- Edge cases: NaN, infinity, zero buffer, etc.

Key test patterns:
1. Default construction values
2. Harmonic content measurement via DFT
3. DC offset measurement
4. Parameter clamping verification
5. Bypass (mix=0) produces exact input
