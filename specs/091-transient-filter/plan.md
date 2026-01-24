# Implementation Plan: TransientAwareFilter

**Branch**: `091-transient-filter` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/091-transient-filter/spec.md`

## Summary

The TransientAwareFilter is a Layer 2 DSP processor that detects transients using dual envelope follower comparison (fast vs slow) and modulates filter cutoff and resonance in response. Unlike EnvelopeFilter which follows overall amplitude, this responds only to sudden level changes (attacks), creating dynamic percussive tonal shaping.

**Technical Approach:**
- Dual EnvelopeFollower instances (1ms fast / 50ms slow) for transient detection
- Level-independent normalization: `diff / max(slowEnv, epsilon)`
- OnePoleSmoother for exponential attack/decay response curves
- SVF for modulation-stable filtering during rapid cutoff changes
- Exponential frequency mapping in log-space for perceptual linearity

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (EnvelopeFollower, SVF, OnePoleSmoother)
**Storage**: N/A (stateful DSP processor, no persistence)
**Testing**: Catch2 (Constitution Principle XIII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (monorepo structure)
**Performance Goals**: < 0.5% CPU at 48kHz mono (Layer 2 budget per Constitution XI)
**Constraints**: Zero allocations in process(), noexcept processing, real-time safe
**Scale/Scope**: Single-channel processor, composable for stereo

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in process() - EnvelopeFollowers/SVF are pre-allocated
- [x] All processing methods noexcept
- [x] No locks, mutexes, or blocking primitives

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 2 processor depends only on Layer 0/1 and peer Layer 2
- [x] Dependencies: EnvelopeFollower (L2), SVF (L1), OnePoleSmoother (L1), db_utils (L0)

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

**Classes/Structs to be created**: TransientAwareFilter, TransientFilterMode (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TransientAwareFilter | `grep -r "class.*Transient" dsp/` | No | Create New |
| TransientFilter | `grep -r "TransientFilter" dsp/` | No | Create New |
| TransientDetector | `grep -r "TransientDetector" dsp/` | No | Not needed - logic inline |

**Utility Functions to be created**: None (all utility functions exist in dependencies)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Two instances for fast (1ms) and slow (50ms) envelope tracking |
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | Main filter with modulation-stable TPT topology |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Response attack/decay smoothing for transient detection |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | No direct usage needed (EnvelopeFollower handles internally) |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN/Inf input validation |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN/Inf input validation |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal handling |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (EnvelopeFilter, SidechainFilter reference)
- [x] `specs/_architecture_/` - Component inventory (README.md for index)
- [x] `specs/_architecture_/layer-2-processors.md` - Existing L2 components

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (TransientAwareFilter, TransientFilterMode) are unique and not found in codebase. The feature follows established composition patterns from EnvelopeFilter and SidechainFilter. No new utility functions are being created.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins. Prevents compile-time API mismatch errors.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class and SVFMode enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/envelope_filter.h` - Reference for composition pattern
- [x] `dsp/include/krate/dsp/processors/sidechain_filter.h` - Reference for sibling pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| EnvelopeFollower | Attack/release times are symmetric (release = attack in spec) | `setAttackTime(1.0f); setReleaseTime(1.0f);` for fast envelope |
| OnePoleSmoother | Uses `snapTo()` not `snap()` for immediate value | `smoother.snapTo(value)` |
| SVF | Mode enum is `SVFMode` not `FilterMode` | `filter.setMode(SVFMode::Lowpass)` |
| SVF | Max Q is 30.0, need to clamp total Q | `std::min(idleQ + transientQBoost, 30.0f)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | All calculations use existing primitives | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateCutoff | Uses member state (idleCutoff_, transientCutoff_, direction_), only this class needs it |
| mapTransientToModulation | Specific to transient detection algorithm, class-specific |

**Decision**: No extraction to Layer 0 needed. All utility functions are class-specific and use member state. The exponential frequency mapping pattern is already established in EnvelopeFilter and SidechainFilter.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from spec.md):
- 090-sidechain-filter (Phase 15.1) - Already complete, shares composition pattern
- 092-pitch-tracking-filter (Phase 15.3) - Different detection mechanism (pitch vs transient)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Dual envelope transient detection | MEDIUM | Possibly TransientShaper, TransientDesigner | Keep local for now |
| Log-space frequency interpolation | LOW | Already duplicated in EnvelopeFilter, SidechainFilter | No action (pattern established) |

### Detailed Analysis (for MEDIUM potential items)

**Dual Envelope Transient Detection** provides:
- Fast/slow envelope comparison
- Level-independent normalization
- Threshold-based gating

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| 092-pitch-tracking-filter | NO | Uses pitch detection, not envelope comparison |
| Future TransientShaper | MAYBE | Might use same detection but different response |

**Recommendation**: Keep in TransientAwareFilter for now. If a second consumer (TransientShaper or similar) is added, consider extracting a TransientDetector primitive to Layer 1.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep transient detection inline | Only one consumer, extraction would be premature |
| Follow EnvelopeFilter composition pattern | Proven pattern, similar sibling at same layer |
| Define FilterType enum locally | Same pattern as SidechainFilter - scoped enum avoids conflicts |

### Review Trigger

After implementing **092-pitch-tracking-filter**, review this section:
- [ ] Does pitch-tracking need transient detection? -> Consider shared component
- [ ] Does pitch-tracking use same SVF composition? -> Document shared pattern
- [ ] Any duplicated frequency mapping code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/091-transient-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contract)
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── transient_filter.h    # TransientAwareFilter class
└── tests/
    └── unit/
        └── processors/
            └── transient_filter_test.cpp  # Catch2 tests
```

**Structure Decision**: Standard KrateDSP monorepo structure. Single header file in Layer 2 processors directory, corresponding test file in unit tests.

## Algorithm Design

### Transient Detection Algorithm

```
Input: audio sample x[n]

1. Fast Envelope (1ms attack/release):
   fastEnv = EnvelopeFollower(x[n], attack=1ms, release=1ms)

2. Slow Envelope (50ms attack/release):
   slowEnv = EnvelopeFollower(x[n], attack=50ms, release=50ms)

3. Transient Difference:
   diff = max(0, fastEnv - slowEnv)

4. Level-Independent Normalization:
   epsilon = 1e-6
   normalized = diff / max(slowEnv, epsilon)

5. Threshold Comparison:
   threshold = 1.0 - sensitivity  (sensitivity in [0,1])
   transientLevel = (normalized > threshold) ? normalized : 0

6. Response Smoothing (exponential, single OnePoleSmoother with dynamic reconfiguration):
   - If transientLevel > smoothed: reconfigure smoother for attack time, setTarget(transientLevel)
   - If transientLevel < smoothed: reconfigure smoother for decay time, setTarget(transientLevel)
   smoothedLevel = OnePoleSmoother.process()
   Note: Using single smoother avoids state management complexity of dual smoothers
```

### Cutoff Modulation

```
Input: smoothedLevel (0 to 1)

1. Log-space interpolation:
   logIdle = log(idleCutoff)
   logTransient = log(transientCutoff)

2. Blend:
   logCutoff = logIdle + smoothedLevel * (logTransient - logIdle)
   cutoff = exp(logCutoff)

3. Apply to SVF:
   svf.setCutoff(cutoff)
```

### Resonance Modulation

```
Input: smoothedLevel (0 to 1)

1. Linear interpolation:
   totalQ = idleQ + smoothedLevel * transientQBoost

2. Clamp to safe range:
   totalQ = clamp(totalQ, 0.5, 30.0)

3. Apply to SVF:
   svf.setResonance(totalQ)
```

## Component Design

### TransientFilterMode Enum

```cpp
enum class TransientFilterMode : uint8_t {
    Lowpass = 0,
    Bandpass = 1,
    Highpass = 2
};
```

### TransientAwareFilter Class

```cpp
class TransientAwareFilter {
public:
    // Constants (from spec FR-xxx)
    static constexpr float kFastEnvelopeAttackMs = 1.0f;
    static constexpr float kFastEnvelopeReleaseMs = 1.0f;
    static constexpr float kSlowEnvelopeAttackMs = 50.0f;
    static constexpr float kSlowEnvelopeReleaseMs = 50.0f;
    static constexpr float kMinSensitivity = 0.0f;
    static constexpr float kMaxSensitivity = 1.0f;
    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 50.0f;
    static constexpr float kMinDecayMs = 1.0f;
    static constexpr float kMaxDecayMs = 1000.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kMaxTotalResonance = 30.0f;
    static constexpr float kMaxQBoost = 20.0f;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] size_t getLatency() const noexcept { return 0; }

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Transient Detection Parameters
    void setSensitivity(float sensitivity) noexcept;
    void setTransientAttack(float ms) noexcept;
    void setTransientDecay(float ms) noexcept;

    // Filter Cutoff Parameters
    void setIdleCutoff(float hz) noexcept;
    void setTransientCutoff(float hz) noexcept;

    // Filter Resonance Parameters
    void setIdleResonance(float q) noexcept;
    void setTransientQBoost(float boost) noexcept;

    // Filter Configuration
    void setFilterType(TransientFilterMode type) noexcept;

    // Monitoring (for UI)
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentResonance() const noexcept;
    [[nodiscard]] float getTransientLevel() const noexcept;

private:
    // Composed components
    EnvelopeFollower fastEnvelope_;
    EnvelopeFollower slowEnvelope_;
    OnePoleSmoother responseSmoother_;
    SVF filter_;

    // Configuration
    double sampleRate_ = 44100.0;
    float sensitivity_ = 0.5f;
    float idleCutoff_ = 200.0f;
    float transientCutoff_ = 4000.0f;
    float idleResonance_ = 0.7071f;
    float transientQBoost_ = 0.0f;
    TransientFilterMode filterType_ = TransientFilterMode::Lowpass;

    // Monitoring state
    float currentCutoff_ = 200.0f;
    float currentResonance_ = 0.7071f;
    float transientLevel_ = 0.0f;

    bool prepared_ = false;

    // Internal helpers
    [[nodiscard]] float calculateCutoff(float transientAmount) const noexcept;
    [[nodiscard]] float calculateResonance(float transientAmount) const noexcept;
    [[nodiscard]] SVFMode mapFilterType(TransientFilterMode type) const noexcept;
};
```

## Test Strategy

### Test Categories

1. **Foundational Tests** (Phase 2)
   - Enum values
   - Constants
   - Prepare/reset lifecycle
   - Default parameter values

2. **User Story 1 Tests** (Phase 3) - Drum Attack Enhancement
   - Transient detection on impulse
   - Filter opens on kick drum hits
   - Filter returns to idle between hits
   - Sensitivity affects threshold

3. **User Story 2 Tests** (Phase 3) - Synth Transient Softening
   - Inverse direction (transient cutoff < idle cutoff)
   - Sustained notes stay at idle
   - Attacks briefly close filter

4. **User Story 3 Tests** (Phase 3) - Resonance Boost
   - Q increases on transients
   - Q boost of 0 means no resonance modulation
   - Total Q clamped to 30.0

5. **Edge Case Tests** (Phase 4)
   - NaN/Inf input handling
   - Rapid transients (16th notes at 180 BPM)
   - Sustained input (no false triggers)
   - Equal idle/transient cutoffs
   - Sensitivity extremes (0 and 1)

6. **Performance Tests** (Phase 4)
   - CPU usage < 0.5% at 48kHz
   - No allocations during process()

### Key Test Patterns (from sidechain_filter_test.cpp)

```cpp
// Test impulse response timing
void generateKickTransient(float* buffer, size_t size, float sampleRate,
                           float attackMs, float decayMs, float amplitude);

// Test level-independence
// Same transient shape at different levels should trigger equally

// Test timing accuracy
size_t msToSamples(float ms, double sampleRate);
```

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| EnvelopeFollower API mismatch | Low | Medium | API signatures verified above |
| Transient detection too sensitive | Medium | Low | Sensitivity parameter with threshold tuning |
| CPU usage exceeds budget | Low | Medium | Simple algorithm, no FFT or heavy processing |
| Rapid modulation artifacts | Medium | Medium | SVF chosen for modulation stability |
| Level-dependent behavior | Low | High | Normalization algorithm verified in spec |

## Complexity Tracking

No constitution violations requiring justification.

## Files to Create

1. **Header**: `dsp/include/krate/dsp/processors/transient_filter.h`
2. **Tests**: `dsp/tests/unit/processors/transient_filter_test.cpp`

## Files to Update (Post-Implementation)

1. `specs/_architecture_/layer-2-processors.md` - Add TransientAwareFilter entry
2. `dsp/tests/CMakeLists.txt` - Add test file to build (if not auto-discovered)
