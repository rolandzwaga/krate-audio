# Implementation Plan: Comb Filters (FeedforwardComb, FeedbackComb, SchroederAllpass)

**Branch**: `074-comb-filter` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/074-comb-filter/spec.md`

## Summary

Implement three Layer 1 DSP primitives for comb filtering:

1. **FeedforwardComb** (FIR): `y[n] = x[n] + g * x[n-D]` - Creates spectral notches for flanger/chorus effects
2. **FeedbackComb** (IIR): `y[n] = x[n] + g * y[n-D]` with optional damping - Creates resonant peaks for Karplus-Strong/reverb
3. **SchroederAllpass**: `y[n] = -g*x[n] + x[n-D] + g*y[n-D]` - Unity magnitude response for reverb diffusion

All three will reuse the existing `DelayLine` primitive and compose with Layer 0 utilities (`flushDenormal`, `isNaN`, `isInf`). Linear interpolation will be used for modulation support (not allpass interpolation, per spec clarification Q3).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: DelayLine (Layer 1), db_utils.h (Layer 0), math_constants.h (Layer 0)
**Storage**: N/A (in-memory delay buffers only)
**Testing**: Catch2 (dsp/tests/primitives/comb_filter_tests.cpp)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo DSP library
**Performance Goals**: <20ns per sample (SC-004), <64 bytes overhead per instance (SC-005)
**Constraints**: Real-time safe (no allocations in process), Layer 1 (depends only on Layer 0 + stdlib)
**Scale/Scope**: 3 filter classes, ~400 lines header, ~300 lines tests

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All processing methods marked `noexcept`
- [x] No memory allocation in `process()` or `processBlock()`
- [x] No locks, I/O, or exceptions in audio path
- [x] Denormal flushing for feedback state variables

**Required Check - Principle IX (Layer Architecture):**
- [x] Layer 1 primitive - depends only on Layer 0 and stdlib
- [x] Will NOT depend on any Layer 2+ components
- [x] Uses `<krate/dsp/...>` include pattern

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
| FeedforwardComb | `grep -r "class FeedforwardComb" dsp/ plugins/` | No | Create New |
| FeedbackComb | `grep -r "class FeedbackComb" dsp/ plugins/` | No | Create New |
| SchroederAllpass | `grep -r "class SchroederAllpass" dsp/ plugins/` | No | Create New |

**Note**: `AllpassStage` exists in `diffusion_network.h` (Layer 2) but is a different component:
- `AllpassStage`: Uses single-delay-line formulation with allpass interpolation for diffusion network
- `SchroederAllpass`: Standard two-state formulation with linear interpolation for modulation, reusable primitive

**No ODR conflict**: Different names, different formulations, different layers.

**Utility Functions to be created**: None - all utilities already exist in Layer 0.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Internal delay buffer for all three filters |
| detail::flushDenormal() | dsp/include/krate/dsp/core/db_utils.h | 0 | Flush denormals in feedback state variables |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection for FR-021 |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection for FR-021 |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | May be needed for frequency calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `specs/_architecture_/` - Component inventory (README.md for index, layer files for details)
- [x] `dsp/include/krate/dsp/processors/diffusion_network.h` - Contains AllpassStage (different component)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned classes (FeedforwardComb, FeedbackComb, SchroederAllpass) are unique names not found anywhere in the codebase. The existing `AllpassStage` in `diffusion_network.h` has a different name and serves a different purpose (composed Layer 2 processor vs. reusable Layer 1 primitive).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `[[nodiscard]] float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| DelayLine | sampleRate | `[[nodiscard]] double sampleRate() const noexcept` | Yes |
| DelayLine | maxDelaySamples | `[[nodiscard]] size_t maxDelaySamples() const noexcept` | Yes |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DelayLine | `readLinear()` for fractional delays, not `read()` | `delay_.readLinear(delaySamples)` |
| DelayLine | Read returns sample at `writeIndex - 1 - delaySamples` | Delay 0 = most recent sample |
| DelayLine | Must call `prepare()` before use | Check `sampleRate() > 0` for prepared state |
| detail::flushDenormal | In `detail::` namespace, not `Krate::DSP::` directly | `detail::flushDenormal(x)` |
| detail::isNaN | Uses bit manipulation, works with -ffast-math disabled | Requires -fno-fast-math on source file |

## Layer 0 Candidate Analysis

*This is a Layer 1 feature, so no Layer 0 extraction analysis needed.*

**Decision**: No new Layer 0 utilities required. All needed utilities already exist.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from specs/_architecture_/layer-1-primitives.md):
- Allpass1Pole (spec 073) - First-order allpass for phasers (different from SchroederAllpass)
- Future: Additional filter primitives

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FeedbackComb | HIGH | Karplus-Strong synthesis, reverb comb banks, physical modeling | Keep as Layer 1 primitive |
| SchroederAllpass | HIGH | Reverb diffusion networks, can complement/replace AllpassStage | Keep as Layer 1 primitive |
| FeedforwardComb | MEDIUM | Flanger, chorus effects, comb EQ | Keep as Layer 1 primitive |

### Detailed Analysis

**FeedbackComb** provides:
- Resonant comb filtering with configurable decay
- Optional damping for natural high-frequency rolloff
- Variable delay for modulation effects

| Future Feature | Would Reuse? | Notes |
|----------------|--------------|-------|
| Karplus-Strong synth | YES | Core component for plucked string synthesis |
| Reverb (Layer 3/4) | YES | Comb bank in Schroeder/Freeverb algorithms |
| Physical modeling | YES | Resonator for drums, strings, etc. |

**SchroederAllpass** provides:
- Unity magnitude filtering (flat frequency response)
- Impulse diffusion/spreading
- Modulation-safe linear interpolation

| Future Feature | Would Reuse? | Notes |
|----------------|--------------|-------|
| Enhanced DiffusionNetwork | MAYBE | Could replace/complement AllpassStage in Layer 2 |
| New reverb algorithms | YES | Standard building block for diffusion |
| Phase dispersion effects | YES | Decorrelation, spatial processing |

**Recommendation**: Keep all three as Layer 1 primitives. They are fundamental building blocks that will be composed by Layer 2+ components.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Use linear interpolation not allpass | Per spec clarification Q3 - supports modulation without artifacts |
| Three separate classes not one templated | Different equations, different parameters, clearer API |
| Damping as one-pole LP coefficient | Simpler than cutoff frequency, direct control, per spec clarification Q1 |

### Review Trigger

After implementing **Karplus-Strong synthesis** or **enhanced reverb**, review:
- [ ] Does new feature need FeedbackComb/SchroederAllpass? -> Already available as primitives
- [ ] Any API changes needed? -> Consider backwards compatibility

## Project Structure

### Documentation (this feature)

```text
specs/074-comb-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output - design decisions
├── data-model.md        # Phase 1 output - class structure
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contracts
│   └── comb_filter.h    # Public API header contract
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── comb_filter.h    # All three comb filter classes
└── tests/
    └── primitives/
        └── comb_filter_tests.cpp  # Catch2 test suite

specs/_architecture_/
└── layer-1-primitives.md    # Update with new components
```

**Structure Decision**: Single header file containing all three comb filter classes (FeedforwardComb, FeedbackComb, SchroederAllpass) following the pattern of other Layer 1 primitives. Tests in corresponding primitives test directory.

## Complexity Tracking

No Constitution violations to justify. This is a straightforward Layer 1 primitive implementation following established patterns.

---

## Phase 0: Research Summary

### Research Tasks Completed

1. **Comb filter stability requirements** - Researched via web search
2. **Schroeder allpass formulation** - Confirmed from Stanford CCRMA and Valhalla DSP
3. **Damping filter implementation** - Confirmed from Freeverb/Moorer designs
4. **Existing codebase analysis** - Verified no ODR conflicts

### Key Decisions from Research

| Decision | Rationale | Alternatives Considered |
|----------|-----------|------------------------|
| Standard two-state Schroeder formulation | Cleaner API, better for modulation | Single-delay-line (used by AllpassStage, requires allpass interpolation) |
| Linear interpolation for all filters | Supports smooth modulation without artifacts | Allpass interpolation (causes issues with modulated delays) |
| Feedback clamped to 0.9999f | Ensures DC stability while allowing long decay | 0.999f (too aggressive), 1.0f (unstable) |
| One-pole LP damping with coefficient range [0,1] | Direct control, simple implementation | Cutoff frequency (requires coefficient calculation each sample) |
| Denormal flushing on feedback state only | Per spec clarification Q2, DelayLine handles its own buffer | Flush entire buffer (unnecessary overhead) |

### Reference Material

- [Stanford CCRMA - Schroeder Allpass Sections](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html)
- [Valhalla DSP - Reverb Diffusion](https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/)
- [DSPRelated - Lowpass Feedback Comb Filter](https://www.dsprelated.com/freebooks/pasp/Lowpass_Feedback_Comb_Filter.html)
- [Number Analytics - Comb Filters Theory](https://www.numberanalytics.com/blog/comb-filters-theory-and-practice)

---

## Phase 1: Design Summary

### Class Design

#### FeedforwardComb (FIR Comb Filter)

```cpp
class FeedforwardComb {
public:
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;

    void setGain(float g) noexcept;           // [0.0, 1.0]
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    DelayLine delay_;
    float gain_ = 0.5f;
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};
```

**Equation**: `y[n] = x[n] + g * x[n-D]`

#### FeedbackComb (IIR Comb Filter)

```cpp
class FeedbackComb {
public:
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;

    void setFeedback(float g) noexcept;       // [-0.9999, 0.9999]
    void setDamping(float d) noexcept;        // [0.0, 1.0] - 0=bright, 1=dark
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    DelayLine delay_;
    float feedback_ = 0.5f;
    float damping_ = 0.0f;
    float dampingState_ = 0.0f;  // One-pole LP state (flushed for denormals)
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};
```

**Equation**: `y[n] = x[n] + g * LP(y[n-D])` where `LP(x) = (1-d)*x + d*LP_prev`

#### SchroederAllpass

```cpp
class SchroederAllpass {
public:
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;

    void setCoefficient(float g) noexcept;    // [-0.9999, 0.9999]
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    DelayLine delay_;
    float coefficient_ = 0.7f;
    float feedbackState_ = 0.0f;  // Flushed for denormals
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};
```

**Equation**: `y[n] = -g*x[n] + x[n-D] + g*y[n-D]`

### Test Strategy

Following Constitution Principle XII (Test-First Development):

1. **Unit Tests** (in `comb_filter_tests.cpp`):
   - Impulse response verification for all three types
   - Frequency response measurements (notch depth, peak height, allpass flatness)
   - Edge cases: NaN/Inf input, zero delay, max delay, gain limits
   - Block vs sample-by-sample equivalence (SC-006)
   - Variable delay sweep (click-free modulation)

2. **Test Categories**:
   - `[feedforward]` - FeedforwardComb tests
   - `[feedback]` - FeedbackComb tests
   - `[schroeder]` - SchroederAllpass tests
   - `[edge-cases]` - Common edge case handling
   - `[performance]` - Performance benchmarks

3. **Key Test Cases** (from spec acceptance scenarios):
   - AS-1.1: Feedforward impulse at D samples with amplitude g
   - AS-1.2: Feedforward notches at f = (2k-1)/(2D*T)
   - AS-2.1: Feedback decaying echoes at D, 2D, 3D...
   - AS-2.2: Feedback peaks at f = k/(D*T)
   - AS-2.3: Feedback stability with g=0.99+
   - AS-3.1: Schroeder unity magnitude (<0.01 dB)
   - AS-3.2: Schroeder impulse spreading
   - AS-4.1/4.2: Variable delay click-free sweep

### Success Criteria Verification Plan

| SC | Verification Method |
|----|---------------------|
| SC-001 | Measure notch depth at theoretical frequencies, verify >= -40 dB |
| SC-002 | Measure peak height at theoretical frequencies, verify >= +20 dB |
| SC-003 | FFT magnitude response sweep, verify max deviation < 0.01 dB |
| SC-004 | Benchmark process() timing, verify < 20 ns/sample |
| SC-005 | sizeof() check, verify < 64 bytes overhead |
| SC-006 | Compare block vs sample-by-sample output, verify bit-identical |
| SC-007 | Feed NaN/Inf/denormal inputs, verify no crash/invalid output |
| SC-008 | Sweep delay at 10Hz, verify no clicks in output |

---

## Artifacts Generated

- `f:\projects\iterum\specs\074-comb-filter\plan.md` (this file)

## Next Steps

1. Run Phase 2 (`/speckit.tasks`) to generate detailed implementation tasks
2. Create feature branch if not already on `074-comb-filter`
3. Begin test-first implementation per Constitution Principle XII
