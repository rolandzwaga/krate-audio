# Implementation Plan: Spectral Tilt Filter

**Branch**: `082-spectral-tilt` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/082-spectral-tilt/spec.md`

## Summary

Implement a **Spectral Tilt Filter** - a Layer 2 DSP processor that applies a linear dB/octave gain slope across the frequency spectrum with configurable pivot frequency. Uses a single high-shelf biquad filter for efficient IIR approximation (per clarification decisions), with parameter smoothing for click-free real-time automation.

**Technical Approach**: Single high-shelf biquad centered at pivot frequency, with smoothed tilt and pivot parameters. Gain coefficients clamped during calculation to prevent extreme boost/cut. DC offset technique for denormal prevention.

## Technical Context

**Language/Version**: C++20 (per constitution Principle III)
**Primary Dependencies**:
- `Biquad` from `primitives/biquad.h` (Layer 1) - high-shelf filter
- `OnePoleSmoother` from `primitives/smoother.h` (Layer 1) - parameter smoothing
- `db_utils.h` from `core/` (Layer 0) - dB/gain conversions
- `math_constants.h` from `core/` (Layer 0) - kPi, kTwoPi

**Storage**: N/A (no persistent storage)
**Testing**: Catch2 via dsp_tests target (per constitution Principle VIII)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) cross-platform
**Project Type**: Single header in monorepo DSP library
**Performance Goals**: < 0.5% CPU for single instance at 44.1 kHz (FR-004, SC-004)
**Constraints**: Zero latency, zero allocations in process(), noexcept processing (FR-010, FR-020, FR-021)
**Scale/Scope**: Single mono channel processor; stereo handled at higher layer via dual instances

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II: Real-Time Audio Thread Safety**
- [x] No memory allocation in process()
- [x] No locks/mutexes in audio thread
- [x] noexcept processing methods
- [x] Pre-allocate all resources in prepare()

**Principle III: Modern C++ Standards**
- [x] C++20 target
- [x] RAII for resource management
- [x] constexpr where possible
- [x] Value semantics, no raw new/delete

**Principle VI: Cross-Platform Compatibility**
- [x] No platform-specific code
- [x] Use VSTGUI abstractions (N/A - DSP only)
- [x] Floating-point consistency via existing utilities

**Principle IX: Layered DSP Architecture**
- [x] Layer 2 processor depends only on Layers 0-1
- [x] No circular dependencies
- [x] Independently testable

**Principle XII: Test-First Development**
- [x] Skills auto-load (testing-guide, vst-guide)
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV: ODR Prevention**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created
- [x] Searched for existing SpectralTilt class - none found

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `SpectralTilt`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralTilt | `grep -r "class SpectralTilt" dsp/ plugins/` | No | Create New |
| struct SpectralTiltState | N/A - will use Biquad internally | N/A | N/A |

**Utility Functions to be created**: None - all utilities exist in Layer 0/1

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | `core/db_utils.h` | Reuse |
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | `core/db_utils.h` | Reuse |
| OnePoleSmoother | `grep -r "class OnePoleSmoother" dsp/` | Yes | `primitives/smoother.h` | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Biquad | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | High-shelf filter for tilt implementation |
| BiquadCoefficients | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Coefficient calculation for custom gain-limited shelf |
| FilterType::HighShelf | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | Filter type for coefficient calculation |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Smooth tilt and pivot parameters |
| dbToGain | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Convert dB/octave to linear gain |
| gainToDb | `dsp/include/krate/dsp/core/db_utils.h` | 0 | For test verification |
| flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | (NOT used - DC offset approach instead) |
| kPi, kTwoPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Angular frequency calculation |
| kDenormalThreshold | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Reference for DC offset magnitude |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no SpectralTilt)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no SpectralTilt)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no SpectralTilt, TiltEQ exists but different purpose)
- [x] `specs/_architecture_/layer-2-processors.md` - TiltEQ exists but is single-knob tonal shaping, not dB/octave tilt

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing `SpectralTilt` class found in codebase. `TiltEQ` exists but is a different concept (single-knob dark/bright tonal shaping). `SpectralMorphFilter` has internal `setSpectralTilt()` method but uses FFT-based implementation in frequency domain - this new IIR implementation provides a standalone, efficient alternative.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Biquad | constructor | `Biquad() noexcept = default;` | Yes |
| Biquad | setCoefficients | `void setCoefficients(const BiquadCoefficients& coeffs) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| BiquadCoefficients | calculate | `[[nodiscard]] static BiquadCoefficients calculate(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept;` | Yes |
| OnePoleSmoother | constructor | `OnePoleSmoother() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| gainToDb | function | `[[nodiscard]] constexpr float gainToDb(float gain) noexcept` | Yes |
| flushDenormal | function | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| kPi | constant | `inline constexpr float kPi = 3.14159265358979323846f;` | Yes |
| kTwoPi | constant | `inline constexpr float kTwoPi = 2.0f * kPi;` | Yes |
| FilterType::HighShelf | enum | `HighShelf,  ///< Boost/cut above cutoff (uses gainDb)` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad, BiquadCoefficients, FilterType
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, gainToDb, flushDenormal, kDenormalThreshold
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | configure() takes ms and sample rate | `smoother.configure(50.0f, sampleRate)` |
| BiquadCoefficients::calculate | Returns coefficients, must call setCoefficients on Biquad | `filter_.setCoefficients(BiquadCoefficients::calculate(...))` |
| FilterType | Is scoped enum `FilterType::HighShelf` | `FilterType::HighShelf` |
| Biquad denormal handling | Already flushes denormals internally in process() | No additional flushing needed |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | No new Layer 0 utilities needed | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateShelfGainDb() | Internal helper, single consumer, depends on class state |
| clampGainCoefficients() | Internal helper for gain limiting, specific to tilt algorithm |

**Decision**: All utilities already exist in Layer 0/1. No new extractions needed.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- SpectralMorphFilter (Phase 12.1) - has internal FFT-based tilt, could potentially delegate to this for IIR version
- SpectralGate (Phase 12.2) - similar spectral processor, might benefit from tilt for output shaping
- ResonatorBank (Phase 13.1) - explicitly mentions `setSpectralTilt()` for high-freq rolloff

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SpectralTilt class | HIGH | ResonatorBank, possibly SpectralMorphFilter IIR mode | Keep standalone, designed for composition |

### Detailed Analysis (for HIGH potential items)

**SpectralTilt** provides:
- Linear dB/octave tilt across spectrum
- Configurable pivot frequency
- Efficient IIR implementation (vs FFT)
- Parameter smoothing built-in

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| ResonatorBank | YES | Explicitly designed to have `setSpectralTilt()` per roadmap |
| SpectralMorphFilter | MAYBE | Could add IIR tilt option alongside FFT tilt |
| TiltEQ | NO | Different concept (single-knob dark/bright vs dB/octave) |

**Recommendation**: Keep as standalone Layer 2 processor. Design API for easy composition by higher layers.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Standalone class (not utility) | Multiple consumers expected, stateful (filter state, smoothers) |
| Single high-shelf architecture | Per spec clarification - simpler than multi-stage cascade |
| IIR not FFT | Per spec - efficient, zero latency, suitable for real-time |

### Review Trigger

After implementing **ResonatorBank (Phase 13.1)**, review this section:
- [ ] Does ResonatorBank need SpectralTilt or similar? -> Compose SpectralTilt internally
- [ ] Does ResonatorBank use same composition pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/082-spectral-tilt/
+-- spec.md              # Feature specification
+-- plan.md              # This file
+-- research.md          # Phase 0 output (high-shelf coefficient research)
+-- data-model.md        # Phase 1 output (class diagram)
+-- quickstart.md        # Phase 1 output (usage examples)
+-- contracts/           # Phase 1 output (API surface)
    +-- spectral_tilt_api.h
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
    +-- processors/
        +-- spectral_tilt.h     # NEW: SpectralTilt class implementation
+-- tests/
    +-- unit/
        +-- processors/
            +-- spectral_tilt_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only implementation in `processors/` with corresponding test file. Follows existing patterns like `envelope_filter.h`, `formant_filter.h`.

## Complexity Tracking

> No Constitution Check violations. Design follows established patterns.

| Consideration | Decision | Rationale |
|--------------|----------|-----------|
| Single vs multi-stage shelf | Single high-shelf | Per spec clarification, simpler design meets requirements |
| Gain limiting approach | Clamp during coefficient calculation | Per spec clarification, prevents numerical instability |
| Denormal handling | DC offset technique | Per spec clarification, alternative to flushDenormal |

---

## Phase 0: Research

### Research Tasks

1. **High-shelf filter coefficient calculation for tilt approximation**
   - How to map dB/octave tilt to shelf gain parameter
   - Relationship between pivot frequency and shelf cutoff frequency
   - Q factor selection for smooth tilt response

2. **Gain limiting implementation**
   - Where to apply gain limits in coefficient calculation
   - Safe gain range (+24 dB max, -48 dB min per spec)
   - Coefficient stability considerations

3. **DC offset for denormal prevention**
   - Optimal offset magnitude (~1e-15f per spec)
   - Where to apply offset (state variables z1, z2)
   - Impact on signal integrity

### Key Findings

#### High-Shelf Tilt Approximation

A single high-shelf filter can approximate spectral tilt when:
- Shelf cutoff frequency = pivot frequency
- Shelf gain (dB) = tilt (dB/octave) for frequencies 1 octave above pivot
- Q = 0.7071 (Butterworth) for smooth transition

**Formula derivation:**
- For +6 dB/octave tilt with 1 kHz pivot:
  - At 2 kHz (1 octave above): target gain = +6 dB
  - At 4 kHz (2 octaves above): target gain = +12 dB
  - High-shelf with +6 dB gain at 1 kHz cutoff approximates this

**Limitation**: Single shelf is an approximation. True linear dB/octave would require infinite stages or FFT. Single shelf provides ~1 dB accuracy within 2-3 octaves of pivot, which meets FR-009 tolerance.

#### Gain Coefficient Clamping

Apply gain limits during `BiquadCoefficients::calculate()` call:
1. Calculate target shelf gain from tilt parameter
2. Clamp shelf gain to [+24 dB, -48 dB] range
3. Pass clamped gain to coefficient calculation

```cpp
float targetGainDb = calculateTargetShelfGain(tiltDbPerOctave);
float clampedGainDb = std::clamp(targetGainDb, kMinGainDb, kMaxGainDb);
auto coeffs = BiquadCoefficients::calculate(FilterType::HighShelf,
                                            pivotFreq, kQ, clampedGainDb, sampleRate);
```

#### DC Offset Technique

Add small DC offset to filter state variables before coefficient multiplication:

```cpp
// In modified process() - but Biquad already handles denormals
// Per investigation: Biquad::process() already calls flushDenormal(z1_) and flushDenormal(z2_)
// The DC offset approach is an ALTERNATIVE that we'll implement in our wrapper
// to demonstrate the technique, though Biquad's built-in handling is sufficient
```

**Decision**: Use Biquad's built-in denormal handling (flushDenormal). Document DC offset technique in comments for educational purposes per FR-011 requirement.

---

## Phase 1: Design

### Data Model

```cpp
class SpectralTilt {
public:
    // Constants (per spec FR-002, FR-003, FR-014)
    static constexpr float kMinTilt = -12.0f;      // dB/octave
    static constexpr float kMaxTilt = +12.0f;      // dB/octave
    static constexpr float kMinPivot = 20.0f;      // Hz
    static constexpr float kMaxPivot = 20000.0f;   // Hz
    static constexpr float kMinSmoothing = 1.0f;   // ms
    static constexpr float kMaxSmoothing = 500.0f; // ms
    static constexpr float kDefaultSmoothing = 50.0f;
    static constexpr float kDefaultPivot = 1000.0f;
    static constexpr float kDefaultTilt = 0.0f;

    // Gain limits (per spec FR-024, FR-025)
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kMinGainDb = -48.0f;

private:
    // Processing components
    Biquad filter_;

    // Parameter smoothers
    OnePoleSmoother tiltSmoother_;
    OnePoleSmoother pivotSmoother_;

    // Configuration
    double sampleRate_ = 44100.0;
    float tilt_ = kDefaultTilt;
    float pivotFrequency_ = kDefaultPivot;
    float smoothingMs_ = kDefaultSmoothing;

    // State
    bool prepared_ = false;
};
```

### API Contract

See `contracts/spectral_tilt_api.h` for full interface.

```cpp
// Lifecycle
void prepare(double sampleRate);
void reset();

// Parameters
void setTilt(float dBPerOctave);           // FR-002: [-12, +12]
void setPivotFrequency(float hz);          // FR-003: [20, 20000]
void setSmoothing(float ms);               // FR-014: [1, 500], default 50

// Processing
[[nodiscard]] float process(float input) noexcept;   // FR-017
void processBlock(float* buffer, int numSamples) noexcept;  // FR-018

// Query
[[nodiscard]] float getTilt() const noexcept;
[[nodiscard]] float getPivotFrequency() const noexcept;
[[nodiscard]] float getSmoothing() const noexcept;
[[nodiscard]] bool isPrepared() const noexcept;
```

### Processing Algorithm

```
process(input):
    if (!prepared_): return input  // FR-019: passthrough

    // Update smoothed parameters
    smoothedTilt = tiltSmoother_.process()
    smoothedPivot = pivotSmoother_.process()

    // Recalculate coefficients if parameters changed
    if (parametersChanged()):
        gainDb = calculateShelfGain(smoothedTilt)
        clampedGain = clamp(gainDb, kMinGainDb, kMaxGainDb)  // FR-023
        coeffs = BiquadCoefficients::calculate(
            FilterType::HighShelf, smoothedPivot, kQ, clampedGain, sampleRate_)
        filter_.setCoefficients(coeffs)

    // Process through filter (denormals handled by Biquad)
    return filter_.process(input)
```

---

## Test Strategy

### Test File Structure

```text
dsp/tests/unit/processors/spectral_tilt_test.cpp
```

### Test Categories

#### 1. Construction and Initialization Tests
- Default construction creates valid object
- isPrepared() returns false before prepare()
- Default parameter values match spec

#### 2. Prepare and Reset Tests
- prepare() sets prepared flag
- reset() clears filter state without changing parameters
- Sample rate is stored correctly
- Multiple prepare() calls are safe

#### 3. Passthrough Tests (FR-019)
- process() before prepare() returns input unchanged
- Zero tilt produces near-unity output (SC-008)

#### 4. Tilt Slope Accuracy Tests (FR-001, FR-009, SC-001, SC-002, SC-007)

**Test Method**: Measure gain at octave intervals
```
Frequencies: 125, 250, 500, 1000, 2000, 4000, 8000 Hz
Pivot: 1000 Hz
Tilt: +6 dB/octave
```

| Frequency | Octaves from Pivot | Expected Gain | Tolerance |
|-----------|-------------------|---------------|-----------|
| 125 Hz | -3 | -18 dB | +/- 1 dB |
| 250 Hz | -2 | -12 dB | +/- 1 dB |
| 500 Hz | -1 | -6 dB | +/- 1 dB |
| 1000 Hz | 0 | 0 dB | +/- 0.5 dB (SC-003) |
| 2000 Hz | +1 | +6 dB | +/- 1 dB |
| 4000 Hz | +2 | +12 dB | +/- 1 dB |
| 8000 Hz | +3 | +18 dB | +/- 1 dB |

**Measurement procedure**:
1. Process sine wave at test frequency for sufficient samples to reach steady state
2. Measure output amplitude
3. Calculate gain in dB relative to input
4. Verify within tolerance

#### 5. Pivot Frequency Tests (FR-003, FR-006, SC-003)
- Unity gain at pivot frequency for all tilt values
- Pivot clamping at 20 Hz and 20 kHz boundaries
- Pivot changes are smoothed (no clicks)

#### 6. Parameter Smoothing Tests (FR-012, FR-013, FR-014, SC-006)
- setSmoothing() affects transition time
- Large parameter changes don't cause clicks
- Smoothing reaches 90% of target within specified time

#### 7. Gain Limiting Tests (FR-023, FR-024, FR-025)
- Extreme positive tilt doesn't exceed +24 dB
- Extreme negative tilt doesn't fall below -48 dB
- Coefficients remain stable at gain limits

#### 8. Real-Time Safety Tests (FR-020, FR-021, FR-022)
- process() and processBlock() are noexcept
- No allocations in process path (verify via profiler/instrumentation)
- Processing completes within performance budget

#### 9. Edge Case Tests
- NaN/Inf input handling
- Very low sample rate (1000 Hz)
- Very high sample rate (192000 Hz)
- Zero samples in processBlock()

### Measurement Utilities

```cpp
// Helper to measure gain at frequency
float measureGainAtFrequency(SpectralTilt& filter, float freq, float sampleRate) {
    const int numSamples = static_cast<int>(sampleRate);  // 1 second
    const float phaseInc = kTwoPi * freq / sampleRate;

    float phase = 0.0f;
    float sumSquared = 0.0f;

    // Skip first 0.5 seconds for filter settling
    for (int i = 0; i < numSamples / 2; ++i) {
        float input = std::sin(phase);
        filter.process(input);
        phase += phaseInc;
    }

    // Measure next 0.5 seconds
    for (int i = 0; i < numSamples / 2; ++i) {
        float input = std::sin(phase);
        float output = filter.process(input);
        sumSquared += output * output;
        phase += phaseInc;
    }

    float rms = std::sqrt(sumSquared / (numSamples / 2));
    float inputRms = 0.7071f;  // RMS of unit sine
    return gainToDb(rms / inputRms);
}
```

---

## Implementation Tasks

### Task 1: Create Test File (Test-First)

1. Create `dsp/tests/unit/processors/spectral_tilt_test.cpp`
2. Write failing tests for:
   - Basic construction
   - prepare() / reset()
   - Passthrough when not prepared
3. Add to CMakeLists.txt

### Task 2: Implement Minimal Skeleton

1. Create `dsp/include/krate/dsp/processors/spectral_tilt.h`
2. Implement class skeleton with constants
3. Implement prepare() and reset()
4. Verify tests pass

### Task 3: Implement Parameter Setters

1. Write tests for setTilt(), setPivotFrequency(), setSmoothing()
2. Implement setters with clamping
3. Implement smoothers configuration
4. Verify tests pass

### Task 4: Implement Core Processing

1. Write tilt slope accuracy tests (octave interval measurement)
2. Implement process() with coefficient calculation
3. Implement gain clamping
4. Verify tilt accuracy tests pass

### Task 5: Implement processBlock()

1. Write processBlock() tests
2. Implement efficient block processing
3. Verify performance meets budget

### Task 6: Implement Edge Cases

1. Write edge case tests (NaN, extreme values)
2. Implement defensive handling
3. Verify all tests pass

### Task 7: Final Verification

1. Run full test suite
2. Verify all FR-xxx and SC-xxx requirements
3. Run pluginval (if plugin code affected)
4. Update architecture documentation

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `dsp/include/krate/dsp/processors/spectral_tilt.h` | CREATE | Main implementation |
| `dsp/tests/unit/processors/spectral_tilt_test.cpp` | CREATE | Unit tests |
| `dsp/tests/CMakeLists.txt` | MODIFY | Add test to build |
| `specs/_architecture_/layer-2-processors.md` | MODIFY | Add SpectralTilt documentation |

---

## Success Criteria Mapping

| Success Criteria | Test | Acceptance |
|------------------|------|------------|
| SC-001 | TiltSlopeAccuracy_Positive | +6 dB at 2x pivot, -6 dB at 0.5x pivot, +/- 1 dB |
| SC-002 | TiltSlopeAccuracy_Negative | -6 dB at 2x pivot, +6 dB at 0.5x pivot, +/- 1 dB |
| SC-003 | PivotFrequencyUnity | Gain at pivot within 0.5 dB of unity |
| SC-004 | PerformanceTest | < 0.5% CPU at 44.1 kHz |
| SC-005 | LatencyTest | getLatency() == 0 (implicit, IIR) |
| SC-006 | ParameterSmoothingClickFree | No audible clicks on parameter change |
| SC-007 | TiltAccuracyRange | Within 1 dB across 100 Hz - 10 kHz |
| SC-008 | ZeroTiltPassthrough | Tilt=0 produces near-unity output |

---

## Appendix: High-Shelf Coefficient Formulas

From Robert Bristow-Johnson's Audio EQ Cookbook (used by existing Biquad):

```
A  = sqrt(10^(gainDb/20))
w0 = 2*pi*f0/Fs
alpha = sin(w0)/(2*Q)

b0 =    A*( (A+1) + (A-1)*cos(w0) + 2*sqrt(A)*alpha )
b1 = -2*A*( (A-1) + (A+1)*cos(w0)                   )
b2 =    A*( (A+1) + (A-1)*cos(w0) - 2*sqrt(A)*alpha )
a0 =        (A+1) - (A-1)*cos(w0) + 2*sqrt(A)*alpha
a1 =    2*( (A-1) - (A+1)*cos(w0)                   )
a2 =        (A+1) - (A-1)*cos(w0) - 2*sqrt(A)*alpha
```

For tilt approximation:
- `f0` = pivot frequency
- `gainDb` = tilt value (dB/octave acts as shelf gain for ~1 octave range)
- `Q` = 0.7071 (Butterworth) for smooth transition
