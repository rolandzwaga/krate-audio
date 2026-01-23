# Implementation Plan: Modal Resonator

**Branch**: `086-modal-resonator` | **Date**: 2026-01-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/086-modal-resonator/spec.md`

## Summary

The Modal Resonator models vibrating bodies as a sum of decaying sinusoidal modes for physically accurate resonance of complex bodies like bells, bars, and plates. It implements up to 32 parallel modes using the impulse-invariant transform of a two-pole complex resonator, with material presets (Wood, Metal, Glass, Ceramic, Nylon) providing frequency-dependent decay via the formula R_k = b_1 + b_3 * f_k^2.

**Technical approach**: Each mode is implemented as a decaying sinusoidal oscillator using the two-pole resonator difference equation:
```
y[n] = input * amplitude + 2*R*cos(theta)*y[n-1] - R^2*y[n-2]
```
where R is the pole radius (from T60 decay) and theta is the angular frequency.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Existing Layer 0/1 components from KrateDSP
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 framework (existing test infrastructure)
**Target Platform**: Windows, macOS, Linux (cross-platform via CMake)
**Project Type**: DSP library component (Layer 2 processor)
**Performance Goals**: 32 modes @ 192kHz within 1% CPU (~26.7 microseconds per 512-sample block)
**Constraints**: Real-time safe (no allocations in process), noexcept, no locks
**Scale/Scope**: Single mono processor; stereo via two instances

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process(), strike(), reset()
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions in audio path
- [x] Pre-allocate all buffers in prepare()

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor - depends only on Layer 0/1
- [x] Will be placed in `dsp/include/krate/dsp/processors/`
- [x] No circular dependencies

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

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ModalResonator | `grep -r "class ModalResonator" dsp/ plugins/` | No | Create New |
| ModalData | `grep -r "struct ModalData" dsp/ plugins/` | No | Create New |
| Material (enum) | `grep -r "enum.*Material" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| t60ToPoleRadius | `grep -r "t60ToPoleRadius\|T60ToPoleRadius" dsp/` | No | - | Create New |
| rt60ToQ | `grep -r "rt60ToQ" dsp/` | Yes | resonator_bank.h | Reference only (different use case) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing for frequency and amplitude (FR-030) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Amplitude conversion |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention (FR-029) |
| detail::isNaN, detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN/Inf detection (FR-032) |
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Angular frequency calculation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (ResonatorBank, KarplusStrong, WaveguideResonator)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ModalResonator, ModalData, Material) are unique and not found in codebase. Similar components (ResonatorBank) use different class names and different internal architecture (bandpass biquads vs two-pole sinusoidal oscillators).

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| kPi | - | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| kTwoPi | - | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Utility functions
- [x] `dsp/include/krate/dsp/core/math_constants.h` - Math constants

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| detail::isNaN | Requires -fno-fast-math for source file | Add compile flag in CMakeLists |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| t60ToPoleRadius | Audio algorithm, potentially reusable | Keep local initially | ModalResonator only currently |
| frequencyDependentDecay | Formula used in multiple physical models | Keep local initially | ModalResonator only |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| t60ToPoleRadius | Only one consumer (ModalResonator); may be inlined |
| updateModeCoefficients | Class-specific, uses internal state |

**Decision**: Keep all new utilities local to ModalResonator initially. Extract to Layer 0 only if a second consumer emerges.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from Phase 13 Physical Modeling Resonators):
- Phase 13.1: ResonatorBank (implemented) - uses bandpass biquads
- Phase 13.2: KarplusStrong (implemented) - delay-based plucked string
- Phase 13.3: WaveguideResonator (implemented) - waveguide tube model

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Material enum | MEDIUM | Potentially KarplusStrong, WaveguideResonator | Keep local; consider extraction if other resonators add material presets |
| Frequency-dependent decay formula | LOW | ResonatorBank already has different decay model | Keep local |
| Two-pole oscillator | LOW | Unique to modal synthesis | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared Material enum | First feature using material presets; patterns not established |
| Keep decay formula local | Different from ResonatorBank's rt60ToQ approach |

## Project Structure

### Documentation (this feature)

```text
specs/086-modal-resonator/
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
│   └── processors/
│       └── modal_resonator.h    # NEW: ModalResonator class (Layer 2)
└── tests/
    └── unit/
        └── processors/
            └── modal_resonator_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only implementation following existing processor patterns (ResonatorBank, WaveguideResonator). Tests in mirrored directory structure.

---

## Phase 0: Research Output

### Research Question 1: Impulse-Invariant Transform for Two-Pole Resonator

**Decision**: Use the two-pole sinusoidal oscillator with direct coefficient computation

**Rationale**: The impulse-invariant transform maps continuous-time poles to discrete-time via z = e^(sT). For a decaying sinusoid, this simplifies to a direct formula where:
- Pole radius R = e^(-1/tau_samples) where tau_samples = T60 * fs / 6.91
- Pole angle theta = 2 * pi * frequency / sampleRate

**Alternatives considered**:
1. Biquad bandpass filters (ResonatorBank approach) - Less accurate for specified T60, different excitation response
2. State-variable filters - More complex, no clear advantage
3. Coupled form oscillator - Numerically less stable for long decays

**Implementation formula** (from CCRMA research):
```cpp
// Difference equation: y[n] = input*amp + 2*R*cos(theta)*y[n-1] - R^2*y[n-2]
float theta = kTwoPi * frequency / sampleRate;
float R = std::exp(-6.91f / (t60 * sampleRate));  // -60dB = 6.91 time constants
float a1 = 2.0f * R * std::cos(theta);
float a2 = R * R;
```

### Research Question 2: Frequency-Dependent Decay Formula

**Decision**: Use R_k = b_1 + b_3 * f_k^2 model from modal synthesis literature

**Rationale**: This is the standard model from physical acoustics, where:
- b_1 (Hz) controls global decay time (constant damping)
- b_3 (seconds) controls frequency-dependent damping (high frequencies decay faster)

**T60 from loss factor**: T60_k = 6.91 / R_k

**Material coefficients**: Must be empirically derived to match perceived material characteristics:
- Lower b_3 = more sustained highs (metallic)
- Higher b_3 = faster high-frequency decay (wooden/damped)

### Research Question 3: Material Preset Coefficient Values

**Decision**: Define empirically-tuned presets based on physical material properties

**Rationale**: Literature provides sparse specific values. Piano strings use b_1=0.5, b_3=1.58e-10. We derive material presets based on:
- Wood: Fast HF decay, moderate global decay (mallet percussion)
- Metal: Slow HF decay, long global decay (bells, vibraphones)
- Glass: Moderate HF decay, long global decay, bright character
- Ceramic: Fast HF decay, moderate global decay (like wood but brighter)
- Nylon: Very fast HF decay, short global decay (heavily damped)

**Derived preset values** (at 440Hz base frequency):

| Material | b_1 (Hz) | b_3 (s) | Character |
|----------|----------|---------|-----------|
| Wood | 2.0 | 1.0e-7 | Warm, quick HF decay (~0.5-2s T60) |
| Metal | 0.3 | 1.0e-9 | Bright, sustained (~2-8s T60) |
| Glass | 0.5 | 5.0e-8 | Bright, ringing (~1-4s T60) |
| Ceramic | 1.5 | 8.0e-8 | Warm/bright, medium decay |
| Nylon | 4.0 | 2.0e-7 | Dull, heavily damped (~0.2-0.8s T60) |

**Mode frequency ratios** (relative to base frequency):
- Harmonic modes: 1, 2, 3, 4, ... (simple for initial implementation)
- Can be overridden per-mode for inharmonic materials

### Research Question 4: SIMD Optimization for 32 Modes

**Decision**: Structure data for potential SIMD but implement scalar first

**Rationale**:
- 32 modes = 8 groups of 4 for SSE or 4 groups of 8 for AVX
- Current target is correctness and test coverage
- Profile before optimizing per Constitution Principle XI

**Data layout for future SIMD**:
```cpp
// Interleaved state for SIMD-friendly access
std::array<float, kMaxModes> y1_;      // y[n-1] for all modes
std::array<float, kMaxModes> y2_;      // y[n-2] for all modes
std::array<float, kMaxModes> a1_;      // 2*R*cos(theta) coefficients
std::array<float, kMaxModes> a2_;      // R^2 coefficients
std::array<float, kMaxModes> gains_;   // Mode amplitudes
```

**Scalar implementation first**, with aligned arrays for future SIMD upgrade.

---

## Phase 1: Design Output

### Data Model

See [data-model.md](data-model.md) for complete entity definitions.

**Key structures**:

```cpp
/// Mode data for bulk configuration
struct ModalData {
    float frequency;   // Mode frequency in Hz
    float t60;         // Decay time in seconds (RT60)
    float amplitude;   // Mode amplitude [0.0, 1.0]
};

/// Material presets
enum class Material : uint8_t {
    Wood,     // Warm, quick HF decay
    Metal,    // Bright, sustained
    Glass,    // Bright, ringing
    Ceramic,  // Warm/bright, medium
    Nylon     // Dull, heavily damped
};

/// Material coefficients for frequency-dependent decay
struct MaterialCoefficients {
    float b1;              // Global decay (Hz)
    float b3;              // Frequency-dependent decay (s)
    std::array<float, 8> frequencyRatios;  // Mode frequency multipliers
};
```

### API Contracts

See [contracts/modal_resonator_api.h](contracts/modal_resonator_api.h) for complete API.

**Public interface summary**:

```cpp
class ModalResonator {
public:
    // Lifecycle
    explicit ModalResonator(float smoothingTimeMs = 20.0f) noexcept;
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Per-mode control
    void setModeFrequency(int index, float hz) noexcept;
    void setModeDecay(int index, float t60Seconds) noexcept;
    void setModeAmplitude(int index, float amplitude) noexcept;
    void setModes(const ModalData* modes, int count) noexcept;

    // Material presets
    void setMaterial(Material mat) noexcept;

    // Global controls
    void setSize(float scale) noexcept;   // [0.1, 10.0]
    void setDamping(float amount) noexcept; // [0.0, 1.0]

    // Excitation
    void strike(float velocity = 1.0f) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, int numSamples) noexcept;

    // Query
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] int getNumActiveModes() const noexcept;
};
```

### Implementation Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ModalResonator (Layer 2)                 │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Mode State Arrays (x32)                │    │
│  │  ┌─────────┬─────────┬─────────┬─────────┐         │    │
│  │  │  y1[k]  │  y2[k]  │  a1[k]  │  a2[k]  │ ...     │    │
│  │  │ (state) │ (state) │ (coeff) │ (coeff) │         │    │
│  │  └─────────┴─────────┴─────────┴─────────┘         │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │           Parameter Smoothers (Layer 1)             │    │
│  │  ┌──────────────────┬──────────────────┐            │    │
│  │  │ frequencySmoother│ amplitudeSmoother│ (x32)      │    │
│  │  │ (OnePoleSmoother)│ (OnePoleSmoother)│            │    │
│  │  └──────────────────┴──────────────────┘            │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Material Preset Data                    │    │
│  │  ┌───────┬───────┬───────┬─────────┬───────┐        │    │
│  │  │ Wood  │ Metal │ Glass │ Ceramic │ Nylon │        │    │
│  │  │ b1,b3 │ b1,b3 │ b1,b3 │  b1,b3  │ b1,b3 │        │    │
│  │  └───────┴───────┴───────┴─────────┴───────┘        │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘

Processing Flow:
                    ┌───────────┐
    input ────────►│  strike?  │
                    │  (add to  │
                    │   modes)  │
                    └─────┬─────┘
                          │
                          ▼
              ┌───────────────────────┐
              │   For each mode k:    │
              │                       │
              │ y[n] = input*amp[k]   │
              │   + a1[k]*y1[k]       │
              │   - a2[k]*y2[k]       │
              │                       │
              │ y2[k] = y1[k]         │
              │ y1[k] = y[n]          │
              │                       │
              │ output += y[n]        │
              └───────────────────────┘
                          │
                          ▼
              ┌───────────────────────┐
              │   flushDenormal()     │
              └───────────────────────┘
                          │
                          ▼
                      output
```

---

## Test Strategy

### Test Categories

1. **Lifecycle Tests** (T001-T010)
   - Construction, prepare(), reset()
   - Sample rate handling
   - State initialization

2. **Per-Mode Control Tests** (T011-T030)
   - setModeFrequency accuracy (SC-002: within 5 cents)
   - setModeDecay accuracy (SC-003: within 10%)
   - setModeAmplitude scaling
   - setModes bulk configuration
   - Parameter clamping

3. **Material Preset Tests** (T031-T050)
   - setMaterial applies correct coefficients
   - Metal vs Wood decay comparison (SC-010)
   - Frequency ratios correct
   - Presets modifiable after selection

4. **Global Control Tests** (T051-T070)
   - setSize frequency scaling (SC-009)
   - setDamping decay reduction
   - Parameter ranges

5. **Strike/Excitation Tests** (T071-T090)
   - strike() produces output (SC-004: within 1 sample)
   - Velocity scaling
   - Energy accumulation
   - Strike during resonance

6. **Processing Tests** (T091-T110)
   - process() single sample
   - processBlock() consistency
   - Input excitation
   - Silent output without excitation

7. **Stability Tests** (T111-T130)
   - 32 modes @ 192kHz (SC-001, SC-007)
   - NaN/Inf handling (FR-032)
   - Long-term stability (30 seconds)
   - Denormal prevention

8. **Parameter Smoothing Tests** (T131-T140)
   - No clicks on frequency change (SC-005)
   - No clicks on amplitude change
   - Smoothing time configuration

### Key Test Implementations

```cpp
// SC-002: Frequency accuracy within 5 cents
TEST_CASE("Mode frequency accurate within 5 cents", "[modal][SC-002]") {
    ModalResonator resonator;
    resonator.prepare(44100.0);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 2.0f);
    resonator.setModeAmplitude(0, 1.0f);

    // Process impulse and measure frequency via DFT
    std::vector<float> output(8192);
    resonator.strike(1.0f);
    for (auto& sample : output) sample = resonator.process(0.0f);

    float measuredFreq = findPeakFrequency(output.data(), output.size(), 44100.0f);
    float centsError = 1200.0f * std::log2(measuredFreq / 440.0f);
    REQUIRE(std::abs(centsError) < 5.0f);
}

// SC-003: Decay time within 10%
TEST_CASE("Mode decay time accurate within 10%", "[modal][SC-003]") {
    ModalResonator resonator;
    resonator.prepare(44100.0);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);  // 1 second T60
    resonator.setModeAmplitude(0, 1.0f);

    std::vector<float> output(88200);  // 2 seconds
    resonator.strike(1.0f);
    for (auto& sample : output) sample = resonator.process(0.0f);

    float measuredT60 = measureRT60(output.data(), output.size(), 44100.0f);
    REQUIRE(measuredT60 == Approx(1.0f).margin(0.1f));
}

// SC-004: Strike latency within 1 sample
TEST_CASE("Strike produces output within 1 sample", "[modal][SC-004]") {
    ModalResonator resonator;
    resonator.prepare(44100.0);
    resonator.setModeFrequency(0, 440.0f);
    resonator.setModeDecay(0, 1.0f);
    resonator.setModeAmplitude(0, 1.0f);

    resonator.strike(1.0f);
    float firstSample = resonator.process(0.0f);

    REQUIRE(std::abs(firstSample) > 0.001f);
}
```

---

## Risk Analysis

### High Risk Items

| Risk | Mitigation |
|------|------------|
| Numerical instability with 32 modes and long decays | Denormal flushing, stability tests at 192kHz for 30s |
| Frequency accuracy with float precision | Double precision for coefficient calculation, verify with DFT |
| Performance not meeting 1% CPU target | Profile early, structure data for SIMD upgrade path |

### Medium Risk Items

| Risk | Mitigation |
|------|------------|
| Material presets not sounding realistic | Empirical tuning with audio tests, user-adjustable |
| Parameter smoothing causing audible artifacts | Test with abrupt changes, verify click-free |
| Strike accumulation causing clipping | Normalize output or soft-clip, document behavior |

### Low Risk Items

| Risk | Mitigation |
|------|------------|
| ODR violations | Thorough codebase search completed |
| API inconsistency with other resonators | Follow ResonatorBank patterns |

---

## Implementation Phases

### Phase 2: Implementation Tasks (to be generated by /speckit.tasks)

**Task Group 1**: Core Two-Pole Oscillator
- Write failing tests for single mode oscillation
- Implement Mode struct with state and coefficients
- Implement coefficient calculation (R, theta from frequency/T60)
- Verify frequency and decay accuracy

**Task Group 2**: Multi-Mode Processing
- Write failing tests for 32-mode parallel processing
- Implement ModalResonator class shell
- Implement prepare(), reset()
- Implement process(), processBlock()

**Task Group 3**: Per-Mode Control
- Write failing tests for setModeFrequency/Decay/Amplitude
- Implement per-mode setters
- Implement setModes() bulk configuration
- Implement parameter clamping

**Task Group 4**: Material Presets
- Write failing tests for material presets
- Implement Material enum and coefficients
- Implement setMaterial()
- Verify frequency-dependent decay

**Task Group 5**: Global Controls
- Write failing tests for size/damping
- Implement setSize()
- Implement setDamping()
- Verify scaling behavior

**Task Group 6**: Strike/Excitation
- Write failing tests for strike()
- Implement strike() with velocity scaling
- Implement energy accumulation
- Verify latency (SC-004)

**Task Group 7**: Parameter Smoothing
- Write failing tests for click-free changes
- Add OnePoleSmoother for frequency/amplitude
- Implement smoothed coefficient updates
- Verify no clicks (SC-005)

**Task Group 8**: Stability and Performance
- Write stability tests (SC-007)
- Implement NaN/Inf handling (FR-032)
- Performance profiling (SC-001)
- Final validation

---

## Complexity Tracking

> No Constitution violations identified. All design decisions comply with principles.

---

## References

Research sources consulted:
- [CCRMA: Impulse Invariant Method](https://ccrma.stanford.edu/~jos/pasp/Impulse_Invariant_Method.html)
- [CCRMA: Two-Pole Filters](https://ccrma.stanford.edu/~jos/fp/Two_Pole.html)
- [Nathan Ho: Exploring Modal Synthesis](https://nathan.ho.name/posts/exploring-modal-synthesis/)
- [DSPRelated: Resonator Bandwidth](https://www.dsprelated.com/freebooks/filters/Resonator_Bandwidth_Terms_Pole.html)
- [Mutable Instruments Rings Manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/rings/manual/)
- [Wikipedia: Impulse Invariance](https://en.wikipedia.org/wiki/Impulse_invariance)
