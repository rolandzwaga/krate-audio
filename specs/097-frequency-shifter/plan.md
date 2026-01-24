# Implementation Plan: Frequency Shifter

**Branch**: `097-frequency-shifter` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/097-frequency-shifter/spec.md`

## Summary

Implement `FrequencyShifter`, a Layer 2 processor that shifts all frequencies by a constant Hz amount using the Hilbert transform for single-sideband modulation. Unlike pitch shifting which preserves harmonic relationships, frequency shifting adds/subtracts a fixed Hz value creating inharmonic, metallic textures. The design uses:
- HilbertTransform (spec-094) for analytic signal generation (I/Q components)
- Direct phase increment recurrence relation with periodic renormalization (every 1024 samples) for the quadrature oscillator
- Three direction modes: Up (upper sideband), Down (lower sideband), Both (ring modulation)
- LFO-based modulation of shift amount
- Feedback path with tanh saturation for spiraling effects
- Stereo mode: left = +shift, right = -shift

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: HilbertTransform (Layer 1), LFO (Layer 1), OnePoleSmoother (Layer 1), db_utils.h, math_constants.h (Layer 0)
**Storage**: N/A (stateless configuration, filter state only)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform VST3
**Project Type**: DSP library component (header-only, Layer 2 processor)
**Performance Goals**: <0.5% CPU single core at 44.1kHz mono (Layer 2 budget per Constitution Principle XI)
**Constraints**: Real-time safe (no allocations in process), noexcept processing
**Scale/Scope**: Mono processor with stereo mode, shift range +/-5000Hz, feedback up to 99%

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

**Principle II (Real-Time Safety):**
- [x] All processing methods will be noexcept
- [x] No allocations in process() - HilbertTransform and LFO pre-allocate in prepare()
- [x] No locks, mutexes, or blocking operations in audio path
- [x] No exceptions, no I/O in process methods

**Principle IX (Layered Architecture):**
- [x] Layer 2 processor - depends only on Layer 0 (core) and Layer 1 (primitives)
- [x] Will use HilbertTransform from primitives/hilbert_transform.h (Layer 1)
- [x] Will use LFO from primitives/lfo.h (Layer 1)
- [x] Will use OnePoleSmoother from primitives/smoother.h (Layer 1)
- [x] Will use math_constants.h and db_utils.h from core/ (Layer 0)

**Principle X (DSP Constraints):**
- [x] Feedback >100% MUST include soft limiting (tanh on +/-1.0 range per spec clarification)
- [x] Denormal flushing after processing (using detail::flushDenormal)
- [x] No oversampling needed (linear SSB modulation, not nonlinear)

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

### Post-Design Re-Check

- [x] All decisions align with constitution
- [x] No violations requiring complexity tracking

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FrequencyShifter | `grep -r "class FrequencyShifter" dsp/ plugins/` | No | Create New |
| ShiftDirection | `grep -r "ShiftDirection" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | core/db_utils.h | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | core/db_utils.h | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | core/db_utils.h | Reuse |
| kPi, kTwoPi | `grep -r "kTwoPi" dsp/` | Yes | core/math_constants.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| HilbertTransform | primitives/hilbert_transform.h | 1 | Analytic signal generation (I/Q components) |
| HilbertOutput | primitives/hilbert_transform.h | 1 | Struct with i (in-phase) and q (quadrature) members |
| LFO | primitives/lfo.h | 1 | Modulation of shift amount |
| Waveform | primitives/lfo.h | 1 | LFO waveform selection (Sine, Triangle) |
| OnePoleSmoother | primitives/smoother.h | 1 | Parameter smoothing for shift, feedback, mix |
| kTwoPi | core/math_constants.h | 0 | Phase increment calculation |
| detail::flushDenormal | core/db_utils.h | 0 | Denormal flushing |
| detail::isNaN | core/db_utils.h | 0 | Input validation |
| detail::isInf | core/db_utils.h | 0 | Input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 primitives (HilbertTransform, LFO, OnePoleSmoother to reuse)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no FrequencyShifter exists)
- [x] `specs/_architecture_/` - Component inventory (FrequencyShifter not listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Searched for "FrequencyShifter", "frequency.*shift", and "ShiftDirection" patterns. No existing implementations found. The HilbertTransform primitive exists (spec-094) and will be composed, not duplicated. The ShiftDirection enum is unique (not to be confused with any existing direction/mode enums).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| HilbertTransform | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| HilbertTransform | reset | `void reset() noexcept` | Yes |
| HilbertTransform | process | `[[nodiscard]] HilbertOutput process(float input) noexcept` | Yes |
| HilbertOutput | i | `float i` (in-phase component) | Yes |
| HilbertOutput | q | `float q` (quadrature component) | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| kTwoPi | - | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/hilbert_transform.h` - HilbertTransform class, HilbertOutput struct
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class, Waveform enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| HilbertTransform | Returns {0, 0} on NaN/Inf input and resets state | Check for invalid output if needed |
| HilbertTransform | Has 5-sample latency | Document but don't compensate (per spec assumption) |
| HilbertOutput | Members are `i` and `q`, not `real` and `imag` | Use `result.i` and `result.q` |
| LFO | Non-copyable due to wavetable vectors | Use move semantics or create in-place |
| LFO | `setFrequency` clamps to [0.01, 20] Hz | LFO range is limited; for modulation |
| OnePoleSmoother | `setTarget` uses ITERUM_NOINLINE for NaN safety | Works correctly, no special handling |
| kTwoPi | Is `float` not `double` | Use `static_cast<double>` for phase calculations |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Quadrature oscillator update | Feature-specific, inline in process loop |
| SSB modulation formulas | Simple math tied to this component's algorithm |
| Phase renormalization | Simple check every 1024 samples |

**Decision**: No new Layer 0 utilities needed. All required utilities exist in db_utils.h and math_constants.h. The quadrature oscillator could potentially be extracted later if other components (RingModulator, BarberPoleFlanger) need it.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from spec.md Forward Reusability):
- RingModulator - could share quadrature oscillator pattern
- SingleSidebandModulator - essentially same component with different interface
- BarberPoleFlanger - uses similar frequency shifting principles

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Quadrature oscillator | MEDIUM | RingModulator, BarberPoleFlanger | Keep local, extract after 2nd use |
| SSB modulation pattern (Hilbert + carrier) | MEDIUM | SingleSidebandModulator | Keep local for now |
| Feedback with tanh saturation | LOW | Already common pattern | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep quadrature oscillator inline | First implementation, extract to shared primitive if RingModulator needs same pattern |
| No shared base class | FrequencyShifter is self-contained; other SSB components may have different interfaces |

### Review Trigger

After implementing **RingModulator** or **BarberPoleFlanger**, review this section:
- [ ] Does sibling need similar quadrature oscillator? -> Extract to shared quadrature_oscillator.h
- [ ] Does sibling use same Hilbert + carrier pattern? -> Document shared SSB pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/097-frequency-shifter/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output - research findings
├── data-model.md        # Phase 1 output - class/struct definitions
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contracts
│   └── frequency_shifter.h
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── frequency_shifter.h    # Header-only implementation
└── tests/
    └── unit/
        └── processors/
            └── frequency_shifter_test.cpp  # Catch2 tests
```

**Structure Decision**: Single header in processors/ following the established Layer 2 pattern (e.g., audio_rate_filter_fm.h, envelope_filter.h).

## Complexity Tracking

No constitution violations requiring justification.

---

## Phase 0 Complete - Research Findings

### Quadrature Oscillator Implementation

**Decision**: Direct phase increment recurrence relation with periodic renormalization every 1024 samples

**Rationale** (per spec clarification):
- More efficient than calling `std::sin`/`std::cos` every sample (4 multiplies + 2 adds vs. expensive trig calls)
- Renormalization every 1024 samples prevents numerical drift without significant overhead
- Standard technique used in DSP textbooks and commercial implementations

**Implementation Pattern**:
```cpp
// Initialize once when shift frequency changes:
void updateOscillator() noexcept {
    const double delta = kTwoPi * shiftHz_ / sampleRate_;
    cosDelta_ = static_cast<float>(std::cos(delta));
    sinDelta_ = static_cast<float>(std::sin(delta));
}

// Each sample:
void advanceOscillator() noexcept {
    const float cosNext = cosTheta_ * cosDelta_ - sinTheta_ * sinDelta_;
    const float sinNext = sinTheta_ * cosDelta_ + cosTheta_ * sinDelta_;
    cosTheta_ = cosNext;
    sinTheta_ = sinNext;

    // Every 1024 samples, renormalize to prevent drift
    if (++renormCounter_ >= 1024) {
        renormCounter_ = 0;
        const float r = std::sqrt(cosTheta_ * cosTheta_ + sinTheta_ * sinTheta_);
        if (r > 0.0f) {
            cosTheta_ /= r;
            sinTheta_ /= r;
        }
    }
}
```

### SSB Modulation Formulas

**Decision**: Use standard Hilbert-based SSB formulas (per spec FR-007, FR-008, FR-009)

**Formulas**:
- Upper sideband (Up): `output = I * cos(wt) - Q * sin(wt)`
- Lower sideband (Down): `output = I * cos(wt) + Q * sin(wt)`
- Both sidebands (ring mod): `output = 0.5 * (up + down) = I * cos(wt)`

Note: "Both" mode simplifies to just `I * cos(wt)` since the Q terms cancel.

### Feedback Saturation

**Decision**: Apply `tanh()` to feedback sample on +/-1.0 range (per spec clarification)

**Rationale**:
- Standard audio saturation range
- Prevents runaway oscillation while preserving signal character
- `tanh(x)` soft-clips gracefully, mapping +/-infinity to +/-1

**Implementation**:
```cpp
// In process():
const float feedbackIn = std::tanh(feedbackSample_) * smoothedFeedback;
const float inputWithFeedback = input + feedbackIn;
```

### Stereo Processing

**Decision**: Left channel = positive shift, Right channel = negative shift (per spec clarification)

**Rationale**:
- Standard stereo enhancement convention
- Creates complementary frequency content in each channel
- At +50Hz shift: L moves content up 50Hz, R moves content down 50Hz

**Implementation**:
```cpp
void processStereo(float& left, float& right) noexcept {
    // Process left with positive shift
    const float leftOut = processInternal(left, +1.0f);

    // Process right with negative shift (flip oscillator sign)
    const float rightOut = processInternal(right, -1.0f);

    left = leftOut;
    right = rightOut;
}
```

### Aliasing

**Decision**: Document only, no mitigation (per spec clarification)

**Rationale**:
- Keeps CPU budget within Layer 2 limits (<0.5%)
- Oversampling would significantly increase cost
- Aliasing only becomes audible with extreme shifts (>Nyquist/2)
- Document as limitation in header comments

### LFO Modulation

**Decision**: Use existing LFO primitive for shift modulation

**Implementation**:
```cpp
// In process():
const float lfoValue = modLFO_.process();  // Returns [-1, +1]
const float effectiveShift = shiftHz_ + (modDepth_ * lfoValue);
```

**Note**: LFO primitive already handles waveform selection, tempo sync, etc. The FrequencyShifter only exposes rate and depth controls for the modulation.

---

## Phase 1 Complete - Design Artifacts

### Enumerations

```cpp
/// Shift direction for single-sideband modulation
enum class ShiftDirection : uint8_t {
    Up = 0,    ///< Upper sideband only (input + shift)
    Down,      ///< Lower sideband only (input - shift)
    Both       ///< Both sidebands (ring modulation)
};
```

### Class Members

```cpp
class FrequencyShifter {
private:
    // Hilbert transform for analytic signal
    HilbertTransform hilbertL_;
    HilbertTransform hilbertR_;

    // Quadrature oscillator state (recurrence relation)
    float cosTheta_ = 1.0f;
    float sinTheta_ = 0.0f;
    float cosDelta_ = 1.0f;
    float sinDelta_ = 0.0f;
    int renormCounter_ = 0;

    // LFO for modulation
    LFO modLFO_;

    // Feedback state
    float feedbackSample_ = 0.0f;

    // Parameters (raw values)
    float shiftHz_ = 0.0f;
    float modDepth_ = 0.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;
    ShiftDirection direction_ = ShiftDirection::Up;

    // Smoothers for parameter changes
    OnePoleSmoother shiftSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};
```

### API Contract

```cpp
// Lifecycle
FrequencyShifter() noexcept = default;
void prepare(double sampleRate) noexcept;
void reset() noexcept;

// Shift Control
void setShiftAmount(float hz) noexcept;  // -5000 to +5000
void setDirection(ShiftDirection dir) noexcept;

// LFO Modulation
void setModRate(float hz) noexcept;   // 0.01 to 20
void setModDepth(float hz) noexcept;  // 0 to 500

// Feedback
void setFeedback(float amount) noexcept;  // 0.0 to 0.99

// Mix
void setMix(float dryWet) noexcept;  // 0.0 to 1.0

// Processing
[[nodiscard]] float process(float input) noexcept;
void processStereo(float& left, float& right) noexcept;

// Query
[[nodiscard]] bool isPrepared() const noexcept;
[[nodiscard]] float getShiftAmount() const noexcept;
[[nodiscard]] ShiftDirection getDirection() const noexcept;
[[nodiscard]] float getModRate() const noexcept;
[[nodiscard]] float getModDepth() const noexcept;
[[nodiscard]] float getFeedback() const noexcept;
[[nodiscard]] float getMix() const noexcept;
```

---

## Testing Strategy

### Test Categories

1. **Foundational Tests** (enum values, constants, lifecycle)
2. **Parameter Tests** (setters/getters, clamping, validation)
3. **SSB Algorithm Tests** (frequency verification via FFT)
4. **Direction Mode Tests** (Up, Down, Both sideband suppression)
5. **LFO Modulation Tests** (shift variation, waveforms)
6. **Feedback Tests** (spiraling effect, stability)
7. **Stereo Tests** (opposite shifts per channel)
8. **Mix Tests** (dry/wet blending)
9. **Edge Cases** (NaN/Inf, denormals, extreme parameters, zero shift)
10. **Performance Tests** (CPU budget verification)

### Key Test Cases

| Test | Requirement | Measurement |
|------|-------------|-------------|
| 440Hz + 100Hz shift = 540Hz | SC-001 | FFT peak detection, sideband suppression > 40dB |
| Direction Up = upper sideband only | SC-002 | Unwanted sideband < -40dB |
| Direction Down = lower sideband only | SC-003 | Unwanted sideband < -40dB |
| LFO modulates shift within +/- depth | SC-004 | Measure shift variance over time |
| Feedback creates comb spectrum | SC-005 | FFT shows peaks every shiftHz |
| Output bounded at 99% feedback | SC-006 | Peak < +6dBFS after 10 seconds |
| Zero shift = pass-through | SC-007 | Output matches input (accounting for Hilbert latency) |
| CPU < 0.5% at 44.1kHz | SC-008 | Benchmark timing |
| Parameter changes click-free | SC-009 | Audio inspection, no transients |
| Stereo has opposite shifts | SC-010 | FFT shows L shifted up, R shifted down |

---

## Implementation Order

### Phase 1: Enumerations and Class Skeleton
1. Define ShiftDirection enum
2. Define FrequencyShifter class with all members
3. Implement lifecycle (prepare, reset)
4. Implement getters/setters with clamping

### Phase 2: Quadrature Oscillator
1. Implement oscillator initialization (cosDelta_, sinDelta_)
2. Implement oscillator advance with renormalization
3. Test oscillator accuracy over long runs

### Phase 3: SSB Modulation (Mono)
1. Implement process() with Hilbert + oscillator
2. Implement all three direction modes
3. Test frequency shifting accuracy (SC-001, SC-002, SC-003)

### Phase 4: LFO Modulation
1. Integrate LFO for shift modulation
2. Implement setModRate, setModDepth
3. Test modulation behavior (SC-004)

### Phase 5: Feedback
1. Implement feedback path with tanh saturation
2. Test spiraling effect (SC-005)
3. Test stability (SC-006)

### Phase 6: Stereo and Mix
1. Implement processStereo() with opposite shifts
2. Implement mix blending
3. Test stereo (SC-010), mix, and zero shift (SC-007)

### Phase 7: Edge Cases and Performance
1. Implement NaN/Inf handling (FR-023)
2. Implement denormal flushing (FR-024)
3. Performance testing (SC-008)
4. Parameter smoothing verification (SC-009)

---

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/frequency_shifter.h` | Header-only implementation |
| `dsp/tests/unit/processors/frequency_shifter_test.cpp` | Catch2 test file |
| `specs/_architecture_/layer-2-processors.md` | Update with new component |

---

## Quick Reference

### Include Pattern
```cpp
#include <krate/dsp/processors/frequency_shifter.h>
```

### Usage Example
```cpp
FrequencyShifter shifter;
shifter.prepare(44100.0);
shifter.setShiftAmount(100.0f);   // +100Hz
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.0f);
shifter.setMix(1.0f);

// Process mono
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = shifter.process(input[i]);
}
```

### Stereo Example
```cpp
FrequencyShifter shifter;
shifter.prepare(44100.0);
shifter.setShiftAmount(50.0f);  // L = +50Hz, R = -50Hz

// Process stereo (in-place)
for (size_t i = 0; i < numSamples; ++i) {
    shifter.processStereo(left[i], right[i]);
}
```

### Ring Modulation Example
```cpp
shifter.setShiftAmount(200.0f);
shifter.setDirection(ShiftDirection::Both);  // Ring mod effect
```

### Modulated Shift Example
```cpp
shifter.setShiftAmount(50.0f);      // Base shift
shifter.setModRate(1.0f);           // 1Hz LFO
shifter.setModDepth(30.0f);         // +/-30Hz variation
// Effective shift oscillates between 20Hz and 80Hz
```

### Spiraling Feedback Example
```cpp
shifter.setShiftAmount(100.0f);
shifter.setFeedback(0.5f);  // 50% feedback
shifter.setDirection(ShiftDirection::Up);
// Creates Shepard-tone-like spiraling effect
```
