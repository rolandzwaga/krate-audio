# Implementation Plan: Moog Ladder Filter (LadderFilter)

**Branch**: `075-ladder-filter` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/075-ladder-filter/spec.md`

## Summary

Implement a Layer 1 DSP primitive that provides the classic Moog 24dB/octave resonant lowpass ladder filter with two processing models:
- **Linear model (Stilson/Smith)**: CPU-efficient 4-pole cascade without saturation
- **Nonlinear model (Huovilainen)**: Tanh saturation per stage for classic analog character with runtime-configurable oversampling (1x/2x/4x)

Key features: Variable slope (1-4 poles), resonance 0-4 with self-oscillation at ~3.9, drive parameter 0-24dB, resonance compensation, and internal per-sample exponential smoothing (~5ms) on cutoff and resonance to prevent zipper noise.

## Technical Context

**Language/Version**: C++20 (Modern C++ per Constitution Principle III)
**Primary Dependencies**:
- `krate/dsp/primitives/oversampler.h` (Layer 1) - 2x/4x oversampling for nonlinear model
- `krate/dsp/primitives/smoother.h` (Layer 1) - OnePoleSmoother for parameter smoothing
- `krate/dsp/core/math_constants.h` (Layer 0) - kPi, kTwoPi
- `krate/dsp/core/db_utils.h` (Layer 0) - dbToGain, flushDenormal, isNaN, isInf
- `krate/dsp/core/fast_math.h` (Layer 0) - fastTanh for Huovilainen saturation
**Storage**: N/A (in-memory filter state only)
**Testing**: Catch2 (dsp/tests/unit/primitives/ladder_filter_test.cpp)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (Layer 1 primitive)
**Performance Goals**:
- Linear model: <50ns/sample
- Nonlinear 2x oversampling: <150ns/sample
- Nonlinear 4x oversampling: <250ns/sample
**Constraints**: Real-time safe (noexcept, no allocations in process after prepare())
**Scale/Scope**: Single header class, ~500-700 lines header, ~800-1000 lines tests

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] No memory allocation in processing methods
- [x] No locks/mutexes in audio path
- [x] No exceptions in audio path (all methods noexcept)
- [x] No I/O operations
- [x] Denormal flushing for filter state variables

**Principle III - Modern C++ Standards:**
- [x] RAII resource management
- [x] Value semantics for filter state
- [x] constexpr where applicable
- [x] [[nodiscard]] on getter methods

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 component (primitives)
- [x] Only depends on Layer 0 (core utilities) and Oversampler (Layer 1)
- [x] No circular dependencies

**Principle X - DSP Processing Constraints:**
- [x] Oversampling for nonlinear tanh saturation (2x/4x configurable)
- [x] Feedback limiting via resonance capping

**Principle XII - Test-First Development:**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step
- [x] Artifact detection helpers for smoothing verification (SC-007)

**Principle XIV - ODR Prevention:**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: LadderFilter, LadderModel (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| LadderFilter | `grep -r "class LadderFilter" dsp/ plugins/` | No | Create New |
| LadderFilter | `grep -r "struct LadderFilter" dsp/ plugins/` | No | Create New |
| LadderModel | `grep -r "enum.*LadderModel" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - all utilities already exist in Layer 0/1.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Oversampler2xMono | dsp/include/krate/dsp/primitives/oversampler.h | 1 | 2x oversampling for nonlinear model |
| Oversampler4xMono | dsp/include/krate/dsp/primitives/oversampler.h | 1 | 4x oversampling for nonlinear model |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing for cutoff/resonance |
| kPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Coefficient calculation |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Angular frequency calculation |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Drive parameter conversion |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | State variable denormal flushing |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection for input validation |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection for input validation |
| FastMath::fastTanh | dsp/include/krate/dsp/core/fast_math.h | 0 | Saturation in Huovilainen model |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no ladder filter)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no ladder filter)
- [x] `specs/_architecture_/layer-1-primitives.md` - Component inventory (no ladder filter)
- [x] `dsp/include/krate/dsp/processors/multimode_filter.h` - Different filter topology, no conflict

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `LadderFilter` class is unique and not found anywhere in codebase. The new class serves a distinct purpose (Moog ladder topology) from existing filters:
- `Biquad` in biquad.h: Generic 2nd-order sections
- `MultimodeFilter` in multimode_filter.h: Layer 2 composed filter with mode selection
- `SVF` in svf.h: State variable filter topology
- `OnePoleLP/HP` in one_pole.h: Simple 1st-order filters

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Oversampler2xMono | prepare | `void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality, OversamplingMode mode) noexcept` | Y |
| Oversampler2xMono | process | `void process(float* buffer, size_t numSamples, const MonoCallback& callback) noexcept` | Y |
| Oversampler2xMono | getLatency | `[[nodiscard]] size_t getLatency() const noexcept` | Y |
| Oversampler2xMono | reset | `void reset() noexcept` | Y |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Y |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Y |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Y |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Y |
| OnePoleSmoother | reset | `void reset() noexcept` | Y |
| FastMath::fastTanh | - | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Y |
| dbToGain | - | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Y |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Y |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Y |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Y |
| kPi | - | `inline constexpr float kPi = 3.14159265358979323846f;` | Y |
| kTwoPi | - | `inline constexpr float kTwoPi = 2.0f * kPi;` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/oversampler.h` - Oversampler class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/fast_math.h` - fastTanh function
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Oversampler | Uses callback for processing at oversampled rate | `oversampler_.process(buffer, n, callback)` |
| Oversampler | Template instantiation: `Oversampler<2, 1>` for 2x mono | Use type alias `Oversampler2xMono` |
| OnePoleSmoother | Uses `configure()` not `prepare()` | `smoother.configure(5.0f, sampleRate)` |
| OnePoleSmoother | snapTo() sets both current and target | Use for initialization |
| detail::flushDenormal | In `detail::` namespace | `detail::flushDenormal(x)` |
| FastMath::fastTanh | In `FastMath::` namespace | `FastMath::fastTanh(x)` |

## Layer 0 Candidate Analysis

**N/A** - This is a Layer 1 primitive. All needed utilities already exist in Layer 0.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateCoefficients | Filter-specific, involves ladder topology math |

**Decision**: All utility functions remain as private members of LadderFilter class.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from specs/_architecture_/layer-1-primitives.md):
- SVF (State Variable Filter) - Different topology, no shared code
- OnePoleLP/HP - Simpler 1st-order filters
- Biquad - Different topology (TDF2 biquad)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| LadderFilter | HIGH | Synth-style filtering in delay modes, future synthesizer features | Keep as Layer 1 primitive |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Single class with model selection | Simpler API than separate Linear/Nonlinear classes |
| Internal oversampling in processBlock | Clean API, transparent to caller |
| Use existing Oversampler | Reuse Layer 1 primitive, avoid duplication |

### Review Trigger

After implementing **future synth filter modes** or **Karplus-Strong**, review:
- [ ] Does new feature need LadderFilter? -> Already available
- [ ] Any API changes needed for new use cases?

## Project Structure

### Documentation (this feature)

```text
specs/075-ladder-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output - Huovilainen algorithm details
├── data-model.md        # Phase 1 output - class structure
├── quickstart.md        # Phase 1 output - usage examples
└── contracts/           # Phase 1 output - API contracts
    └── ladder_filter.h  # Public API header contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── ladder_filter.h    # Implementation (header-only)
└── tests/
    └── unit/
        └── primitives/
            └── ladder_filter_test.cpp  # Unit tests

specs/_architecture_/
└── layer-1-primitives.md    # Update with LadderFilter entry
```

**Structure Decision**: Standard Layer 1 primitive structure - header-only implementation in `dsp/include/krate/dsp/primitives/` with tests in `dsp/tests/unit/primitives/`.

## Constitution Re-Check (Post-Design)

*Re-evaluated after Phase 1 design completion.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] All processing methods marked `noexcept`
- [x] No memory allocation in `process()` or `processBlock()`
- [x] Oversampler buffers pre-allocated in `prepare()`
- [x] No locks/mutexes in any method
- [x] No I/O operations

**Principle III - Modern C++ Standards:**
- [x] `[[nodiscard]]` on getter methods and process()
- [x] Value semantics throughout
- [x] Enum class for model selection

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 component confirmed
- [x] Dependencies: oversampler.h (L1), smoother.h (L1), math_constants.h (L0), db_utils.h (L0), fast_math.h (L0)
- [x] No upward dependencies

**Principle X - DSP Processing Constraints:**
- [x] Oversampling integrated for nonlinear model
- [x] Denormal flushing in all filter stages

**Principle XII - Test-First Development:**
- [x] Test file location: `dsp/tests/unit/primitives/ladder_filter_test.cpp`
- [x] Test patterns established from `svf_test.cpp`, `biquad_test.cpp`
- [x] Artifact detection helpers for smoothing verification

**Principle XIV - ODR Prevention:**
- [x] `LadderFilter` class unique in codebase
- [x] Constants prefixed with `kLadder*`
- [x] No naming conflicts with existing components

**Gate Status**: PASS - All constitution checks satisfied.

---

## Phase 0: Research Summary

### Research Tasks Completed

1. **Huovilainen nonlinear ladder algorithm** - Researched via spec references (DAFX 2004)
2. **Stilson/Smith linear model** - Standard digital ladder implementation
3. **Resonance compensation formulas** - Linear compensation chosen per spec
4. **Parameter smoothing requirements** - Per-sample exponential, ~5ms time constant
5. **Oversampling integration patterns** - Reviewed existing Oversampler API

### Key Decisions from Research

| Decision | Rationale | Alternatives Considered |
|----------|-----------|------------------------|
| Huovilainen for nonlinear | Best balance of analog character vs. CPU | Stilson-Smith with saturation, Zavalishin |
| Stilson/Smith for linear | Simple, efficient, well-documented | Cascaded biquads |
| fastTanh for saturation | 3x faster than std::tanh, sufficient accuracy | std::tanh, polynomial approximation |
| Runtime oversampling factor | User can balance quality vs CPU | Compile-time template, fixed 2x |
| Internal oversampling | Clean API, transparent to caller | External oversampling by caller |
| OnePoleSmoother for params | Existing tested primitive, correct time constant | Custom smoothing, per-block |
| Linear resonance compensation | Simple formula, per spec: `1.0 / (1.0 + resonance * 0.25)` | Exponential curves |

### Algorithm Details

#### Linear Model (Stilson/Smith)

4 cascaded one-pole lowpass stages with feedback from output to input:

```
g = tan(pi * cutoff / sampleRate)
k = resonance

For each sample:
  feedback = state[3] * k
  input_with_fb = input - feedback

  For each stage i = 0..3:
    v = g * (input_i - state[i])
    output_i = v + state[i]
    state[i] = output_i + v
    input_{i+1} = output_i

  output = output based on slope selection (1-4 poles)
```

#### Nonlinear Model (Huovilainen)

Same topology but with tanh saturation in feedback and per-stage:

```
For each sample (at oversampled rate):
  feedback = tanh(state[3] * k * thermal)
  input_with_fb = input - feedback

  For each stage i = 0..3:
    stage_input = i == 0 ? input_with_fb : state[i-1]
    v = g * (tanh(stage_input * thermal) - tanh_state[i])
    output_i = v + tanh_state[i]
    state[i] = output_i
    tanh_state[i] = tanh(output_i * thermal)
```

Where `thermal = 1.22` (controls saturation character).

### Reference Material

- [Huovilainen DAFX 2004 Paper](https://dafx.de/paper-archive/2004/P_061.PDF) - Primary reference for nonlinear model
- [MoogLadders GitHub Collection](https://github.com/ddiakopoulos/MoogLadders) - Reference implementations
- [Valimaki Improvements](https://www.researchgate.net/publication/220386519_Oscillator_and_Filter_Algorithms_for_Virtual_Analog_Synthesis) - Optimization techniques

---

## Phase 1: Design Summary

### Class Design

```cpp
namespace Krate::DSP {

/// Processing model selection
enum class LadderModel : uint8_t {
    Linear,     ///< CPU-efficient, no saturation
    Nonlinear   ///< Tanh saturation per stage (Huovilainen)
};

/// Moog ladder filter primitive
class LadderFilter {
public:
    // Constants
    static constexpr float kMinCutoff = 20.0f;
    static constexpr float kMaxCutoffRatio = 0.45f;  // * sampleRate
    static constexpr float kMinResonance = 0.0f;
    static constexpr float kMaxResonance = 4.0f;
    static constexpr float kMinDriveDb = 0.0f;
    static constexpr float kMaxDriveDb = 24.0f;
    static constexpr int kMinSlope = 1;  // 6dB/oct
    static constexpr int kMaxSlope = 4;  // 24dB/oct
    static constexpr float kDefaultSmoothingTimeMs = 5.0f;

    // Lifecycle
    LadderFilter() noexcept = default;
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    // Configuration
    void setModel(LadderModel model) noexcept;
    void setOversamplingFactor(int factor) noexcept;  // 1, 2, or 4
    void setResonanceCompensation(bool enabled) noexcept;
    void setSlope(int poles) noexcept;  // 1-4

    // Parameters
    void setCutoff(float hz) noexcept;
    void setResonance(float amount) noexcept;  // 0-4
    void setDrive(float db) noexcept;  // 0-24 dB

    // Getters
    [[nodiscard]] LadderModel getModel() const noexcept;
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] int getSlope() const noexcept;
    [[nodiscard]] int getOversamplingFactor() const noexcept;
    [[nodiscard]] bool isResonanceCompensationEnabled() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Latency
    [[nodiscard]] int getLatency() const noexcept;

private:
    // State
    std::array<float, 4> state_{};      // 4 one-pole stages
    std::array<float, 4> tanhState_{};  // Cached tanh for Huovilainen

    // Smoothers for zipper-free modulation
    OnePoleSmoother cutoffSmoother_;
    OnePoleSmoother resonanceSmoother_;

    // Oversampler (for nonlinear model)
    Oversampler2xMono oversampler2x_;
    Oversampler4xMono oversampler4x_;

    // Configuration
    double sampleRate_ = 44100.0;
    double oversampledRate_ = 44100.0;
    LadderModel model_ = LadderModel::Linear;
    int oversamplingFactor_ = 2;
    int slope_ = 4;
    bool resonanceCompensation_ = false;
    bool prepared_ = false;

    // Cached parameters
    float targetCutoff_ = 1000.0f;
    float targetResonance_ = 0.0f;
    float driveGain_ = 1.0f;

    // Internal
    void calculateCoefficients(float cutoff, float resonance) noexcept;
    [[nodiscard]] float processLinear(float input, float cutoff, float resonance) noexcept;
    [[nodiscard]] float processNonlinear(float input, float cutoff, float resonance) noexcept;
    [[nodiscard]] float selectOutput() const noexcept;
};

} // namespace Krate::DSP
```

### Test Strategy

Following Constitution Principle XII (Test-First Development):

1. **Unit Tests** (in `ladder_filter_test.cpp`):
   - Frequency response verification (slope accuracy)
   - Self-oscillation test (resonance >= 3.9)
   - Parameter smoothing artifact detection using test helpers
   - Edge cases: NaN/Inf input, extreme parameters
   - Block vs sample-by-sample equivalence
   - Model switching mid-stream (click-free)
   - Stability over 1M samples

2. **Test Categories**:
   - `[ladder][linear]` - Linear model tests
   - `[ladder][nonlinear]` - Nonlinear model tests
   - `[ladder][smoothing]` - Parameter smoothing tests
   - `[ladder][oversampling]` - Oversampling tests
   - `[ladder][self-oscillation]` - Resonance self-oscillation
   - `[ladder][slope]` - Variable slope tests
   - `[ladder][stability]` - Long-running stability tests
   - `[ladder][edge-cases]` - Edge case handling

3. **Key Test Cases** (from spec acceptance scenarios):
   - AS-1.1: White noise through 1kHz cutoff shows -24dB at 2kHz (4-pole)
   - AS-1.2: Self-oscillation at resonance 3.9 produces sine at cutoff
   - AS-2.1: 1-pole mode shows -6dB at one octave above cutoff
   - AS-2.2: 2-pole mode shows -12dB at one octave above cutoff
   - AS-3.1: Aliasing products at least 60dB below fundamental (10kHz, nonlinear)
   - AS-3.2: Model switching mid-stream produces no clicks
   - AS-4.1: Drive 0dB produces <0.1% THD
   - AS-4.2: Drive 12dB produces visible odd harmonics
   - AS-5.1: Resonance compensation maintains level within 3dB

4. **Artifact Detection Tests** (SC-007):
   - Use `ClickDetector` from test_helpers for smoothing verification
   - Parameter sweep tests with rapid cutoff/resonance changes
   - Verify no clicks detected during 100Hz-10kHz sweep in 100 samples

### Success Criteria Verification Plan

| SC | Verification Method |
|----|---------------------|
| SC-001 | Measure -24dB (+/-1dB) at one octave above cutoff for 4-pole mode |
| SC-002 | Process silence with resonance 3.9, verify stable sine output |
| SC-003 | FFT analysis of 10kHz through nonlinear, verify aliasing < -60dB |
| SC-004 | Benchmark timing per sample, verify Linear <50ns, Nonlinear 2x <150ns, 4x <250ns |
| SC-005 | Process extreme parameters for 1M samples, verify no NaN/Inf/runaway |
| SC-006 | Measure -6dB, -12dB, -18dB at 1 octave for 1, 2, 3-pole modes |
| SC-007 | Use ClickDetector during parameter sweeps, verify zero detections |
| SC-008 | Run tests on all three platforms via CI |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Aliasing in nonlinear model | Medium | High | Enforce 2x minimum oversampling, quality testing |
| Self-oscillation instability | Low | Medium | Resonance capping at 4.0, soft limiting |
| Parameter smoothing insufficient | Low | Medium | Use proven OnePoleSmoother, artifact detection tests |
| CPU budget exceeded | Medium | Medium | Benchmark early, optimize if needed (SIMD, coefficient caching) |
| Cross-platform precision differences | Low | Low | Use Approx() with margin in tests, 6 decimal precision |

---

## Artifacts Generated

- `f:\projects\iterum\specs\075-ladder-filter\plan.md` (this file)

## Next Steps

1. Create `research.md` with detailed Huovilainen algorithm documentation
2. Create `data-model.md` with complete class structure
3. Create `quickstart.md` with usage examples
4. Create `contracts/ladder_filter.h` with API contract
5. Run Phase 2 (`/speckit.tasks`) to generate detailed implementation tasks
6. Begin test-first implementation per Constitution Principle XII

---

## Complexity Tracking

No Constitution violations to justify. This is a straightforward Layer 1 primitive implementation following established patterns from SVF and Biquad filters.
