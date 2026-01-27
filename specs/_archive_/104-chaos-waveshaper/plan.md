# Implementation Plan: Chaos Attractor Waveshaper

**Branch**: `104-chaos-waveshaper` | **Date**: 2026-01-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/104-chaos-waveshaper/spec.md`

## Summary

A Layer 1 primitive that provides time-varying waveshaping using chaos attractor dynamics. The attractor's normalized X component modulates the drive of a tanh-based soft-clipper, producing "living" distortion that evolves over time without external modulation. Supports four chaos models (Lorenz, Rossler, Chua, Henon) with input coupling for signal-reactive behavior.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- Layer 1: `primitives/oversampler.h` (Oversampler<2, 1> for anti-aliased waveshaping)
- Layer 0: `core/db_utils.h` (flushDenormal, isNaN, isInf)
- Layer 0: `core/sigmoid.h` (Sigmoid::tanh for waveshaping)
- Layer 0: `core/random.h` (Xorshift32 for reproducible initial conditions)
**Storage**: N/A
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single library (KrateDSP)
**Performance Goals**: < 0.1% CPU per instance at 44.1kHz (Layer 1 primitive budget)
**Constraints**: Zero allocations in process(), noexcept, real-time safe
**Scale/Scope**: Single class with ~400-500 lines, 4 attractor models

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A - Pure DSP primitive, no VST involvement.

**Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() - all state preallocated
- [x] No locks/mutexes - no shared state
- [x] No exceptions - all methods noexcept
- [x] No I/O - pure computation

**Principle III (Modern C++ Standards):**
- [x] C++20 targeting
- [x] RAII not needed (no resources to manage)
- [x] constexpr where applicable
- [x] Value semantics for state

**Principle IX (Layered DSP Architecture):**
- [x] Layer 1 primitive - depends only on Layer 0
- [x] No dependencies on Layers 2-4
- [x] Independently testable

**Principle X (DSP Processing Constraints):**
- [x] Internal 2x oversampling using `Oversampler<2, 1>` primitive (FR-034, FR-035)
- [x] No internal DC blocking (waveshaping is symmetric, compose with DCBlocker if needed)
- [x] Denormal flushing applied to attractor state

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ChaosWaveshaper, ChaosModel (enum), AttractorState (internal struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ChaosWaveshaper | `grep -r "ChaosWaveshaper" dsp/ plugins/` | No | Create New |
| ChaosModel | `grep -r "ChaosModel" dsp/ plugins/` | No | Create New |
| AttractorState | `grep -r "AttractorState" dsp/ plugins/` | No | Create New (internal) |

**Utility Functions to be created**: None - all utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | core/db_utils.h | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | core/db_utils.h | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | core/db_utils.h | Reuse |
| fastTanh | `grep -r "fastTanh" dsp/` | Yes | core/fast_math.h | Reuse (via Sigmoid::tanh) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Oversampler<2, 1>` | dsp/include/krate/dsp/primitives/oversampler.h | 1 | **REQUIRED** - 2x mono oversampling for anti-aliased waveshaping (FR-034, FR-035) |
| `detail::flushDenormal()` | dsp/include/krate/dsp/core/db_utils.h | 0 | Prevent denormals in attractor state |
| `detail::isNaN()` | dsp/include/krate/dsp/core/db_utils.h | 0 | Detect divergent attractor (NaN check) |
| `detail::isInf()` | dsp/include/krate/dsp/core/db_utils.h | 0 | Detect divergent attractor (Inf check) |
| `Sigmoid::tanh()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Base waveshaping function |
| `Sigmoid::tanhVariable()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Variable-drive waveshaping |
| `Xorshift32` | dsp/include/krate/dsp/core/random.h | 0 | Optional: reproducible initial conditions |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/stochastic_filter.h` - Has Lorenz implementation (inline, not reusable)
- [x] `specs/_architecture_/` - Component inventory (ChaosWaveshaper not listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ChaosWaveshaper, ChaosModel, AttractorState) are unique and not found anywhere in the codebase. The Lorenz attractor in StochasticFilter is implemented inline and uses different parameters/approach. No name collisions possible.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| oversampler.h | Oversampler<2,1>::prepare | `void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality = Economy, OversamplingMode mode = ZeroLatency) noexcept` | Yes |
| oversampler.h | Oversampler<2,1>::reset | `void reset() noexcept` | Yes |
| oversampler.h | Oversampler<2,1>::process | `void process(float* buffer, size_t numSamples, const MonoCallback& callback) noexcept` | Yes |
| oversampler.h | Oversampler<2,1>::getLatency | `[[nodiscard]] size_t getLatency() const noexcept` | Yes |
| db_utils.h | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| db_utils.h | isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils.h | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| sigmoid.h | Sigmoid::tanh | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |
| sigmoid.h | Sigmoid::tanhVariable | `[[nodiscard]] constexpr float tanhVariable(float x, float drive) noexcept` | Yes |
| random.h | Xorshift32 | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| random.h | Xorshift32::nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/oversampler.h` - Oversampler<2, 1> class and methods verified
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf signatures verified
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanh, Sigmoid::tanhVariable verified
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class verified

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| oversampler.h | Must call `prepare()` before `process()` | Check `isPrepared()` or always call in `prepare()` |
| oversampler.h | Non-copyable, holds filter state | Use member variable, not copies |
| oversampler.h | MonoCallback signature is `void(float*, size_t)` | Lambda must match exactly |
| oversampler.h | getLatency() returns 0 for Economy/ZeroLatency mode | No latency compensation needed for default settings |
| db_utils.h | `detail::` namespace prefix required | `detail::flushDenormal(x)` |
| db_utils.h | `detail::` namespace prefix required | `detail::isNaN(x)`, `detail::isInf(x)` |
| sigmoid.h | Uses FastMath::fastTanh internally | `Sigmoid::tanh(x)` wraps it cleanly |
| sigmoid.h | tanhVariable handles drive=0 returning 0 | Safe to use directly |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

N/A - This is a Layer 1 primitive. All needed utilities already exist in Layer 0.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

**Decision**: No extraction needed. All required utilities (denormal flushing, NaN/Inf detection, tanh waveshaping) already exist in Layer 0.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from ROADMAP.md or known plans):
- Future chaos-based LFO could reuse attractor implementations
- Stochastic delay modulation could use chaos sources
- Generative effects could use chaos state

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Lorenz integration kernel | MEDIUM | Chaos LFO, modulation sources | Keep local - wait for 2nd consumer |
| Rossler integration kernel | MEDIUM | Chaos LFO, modulation sources | Keep local - wait for 2nd consumer |
| Chua integration kernel | LOW | Rare - circuit-specific | Keep local |
| Henon map iteration | MEDIUM | Chaos LFO, percussion triggers | Keep local - wait for 2nd consumer |
| Bounded state pattern | HIGH | Any divergent system | Keep local - simple pattern |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep attractor kernels in ChaosWaveshaper | First consumer - no concrete evidence of reuse yet |
| No shared attractor base class | Over-engineering - each model has different state (2D vs 3D) |
| Inline Euler integration | Simple enough to duplicate if needed, avoid premature abstraction |

### Review Trigger

After implementing **chaos-based LFO or stochastic modulation**, review this section:
- [ ] Does sibling need Lorenz/Rossler/Henon attractors? -> Extract to `core/chaos_attractors.h`
- [ ] Does sibling use same bounded-state pattern? -> Document shared pattern
- [ ] Any duplicated integration code? -> Consider shared kernels

## Project Structure

### Documentation (this feature)

```text
specs/104-chaos-waveshaper/
├── plan.md              # This file
├── research.md          # Phase 0 output (chaos attractor math)
├── data-model.md        # Phase 1 output (class design)
├── quickstart.md        # Phase 1 output (usage examples)
└── contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── chaos_waveshaper.h   # NEW: ChaosWaveshaper class
└── tests/
    └── primitives/
        └── test_chaos_waveshaper.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only implementation in Layer 1 primitives directory, matching existing patterns (waveshaper.h, wavefolder.h, etc.)

## Complexity Tracking

No constitution violations. Feature is straightforward Layer 1 primitive.

---

# Phase 0: Research

## Chaos Attractor Mathematics

### Lorenz Attractor (FR-013, FR-014)

The Lorenz system is a 3D continuous dynamical system:

```
dx/dt = sigma * (y - x)
dy/dt = x * (rho - z) - y
dz/dt = x * y - beta * z
```

**Standard parameters (chaotic regime):**
- sigma = 10.0
- rho = 28.0
- beta = 8/3 = 2.666...

**Typical state bounds:** X in [-20, 20], Y in [-30, 30], Z in [0, 50]
**Safe bounds (FR-018):** +/- 50 for all variables (per spec clarification)

**Integration timestep (FR-019):** dt = 0.005 at 44100 Hz
- Scaled by (44100.0 / sampleRate) for other rates
- Scaled by attractorSpeed parameter

**Perturbation scale (FR-026):** 0.1

### Rossler Attractor (FR-015)

The Rossler system is a simpler 3D continuous attractor:

```
dx/dt = -y - z
dy/dt = x + a * y
dz/dt = b + z * (x - c)
```

**Standard parameters (chaotic regime):**
- a = 0.2
- b = 0.2
- c = 5.7

**Typical state bounds:** X in [-10, 10], Y in [-10, 10], Z in [0, 20]
**Safe bounds (FR-018):** +/- 20 for all variables

**Integration timestep (FR-019):** dt = 0.02 at 44100 Hz
**Perturbation scale (FR-026):** 0.1

### Chua Attractor (FR-016)

The Chua circuit is a double-scroll electronic circuit attractor:

```
dx/dt = alpha * (y - x - h(x))
dy/dt = x - y + z
dz/dt = -beta * y
```

Where h(x) is the piecewise-linear Chua diode function:
```
h(x) = m1 * x + 0.5 * (m0 - m1) * (|x + 1| - |x - 1|)
```

**Standard parameters (clarification):**
- alpha = 15.6
- beta = 28.0
- m0 = -1.143
- m1 = -0.714

**Safe bounds (FR-018):** +/- 10 for all variables
**Integration timestep (FR-019):** dt = 0.01 at 44100 Hz
**Perturbation scale (FR-026):** 0.08

### Henon Map (FR-017)

The Henon map is a 2D discrete attractor (not continuous):

```
x[n+1] = 1 - a * x[n]^2 + y[n]
y[n+1] = b * x[n]
```

**Standard parameters:**
- a = 1.4
- b = 0.3

**Safe bounds (FR-018):** +/- 5 for both variables
**Integration timestep (FR-019):** dt = 1.0 (one iteration per control update)
**Perturbation scale (FR-026):** 0.05

**Interpolation:** For continuous output, interpolate between discrete map iterations using the fractional phase of the attractor speed.

## Waveshaping Transfer Function (FR-020, FR-021, FR-022)

The chaos modulates waveshaping via drive control:

```cpp
// Normalize attractor X to [-1, 1]
float normalizedX = std::clamp(attractorX / normalizationFactor, -1.0f, 1.0f);

// Map to drive range [minDrive, maxDrive]
// Using reasonable range: minDrive=0.5, maxDrive=4.0
float drive = minDrive + (normalizedX * 0.5f + 0.5f) * (maxDrive - minDrive);

// Apply waveshaping
float shaped = Sigmoid::tanhVariable(input, drive);

// Mix with dry
float output = std::lerp(input, shaped, chaosAmount);
```

**Normalization factors (to scale X to [-1, 1]):**
- Lorenz: 20.0 (typical X range is [-20, 20])
- Rossler: 10.0
- Chua: 5.0
- Henon: 1.5

## Sample Rate Compensation (FR-019)

```cpp
float compensatedDt = baseDt * (44100.0f / sampleRate_) * attractorSpeed_;
```

## Input Coupling (FR-025, FR-026, FR-027)

```cpp
// After integration, perturb state with input
if (inputCoupling_ > 0.0f) {
    float perturbation = inputCoupling_ * std::abs(input) * perturbationScale;
    attractorX_ += perturbation;
    attractorY_ += perturbation * 0.5f;  // Lesser perturbation on Y
    // Z typically unperturbed or minimal
}
```

## Numerical Stability (FR-031, FR-032, FR-033)

```cpp
// Input sanitization
float sanitizeInput(float x) noexcept {
    if (detail::isNaN(x)) return 0.0f;
    if (detail::isInf(x)) return std::clamp(x, -1.0f, 1.0f);  // Inf -> +/-1
    return x;
}

// State validation after integration
bool isStateValid() const noexcept {
    return !detail::isNaN(x_) && !detail::isInf(x_) &&
           !detail::isNaN(y_) && !detail::isInf(y_) &&
           !detail::isNaN(z_) && !detail::isInf(z_);
}

// Reset on divergence
void checkAndResetIfDiverged() noexcept {
    if (!isStateValid() || std::abs(x_) > safeBound_ ||
        std::abs(y_) > safeBound_ || std::abs(z_) > safeBound_) {
        resetToInitialConditions();
    }
}
```

---

# Phase 1: Design

## Data Model

See [data-model.md](data-model.md) for complete class design.

### Key Design Decisions

1. **Single class with enum-based model selection** - simpler than inheritance hierarchy for 4 models
2. **Internal state struct** - keeps x, y, z (or x, y for Henon) organized
3. **Control-rate attractor updates** - attractor doesn't need per-sample updates; update every 32 samples for efficiency
4. **Smooth parameter changes** - drive is naturally smooth from chaos evolution; no explicit smoother needed

### Class Interface Summary

```cpp
enum class ChaosModel : uint8_t { Lorenz, Rossler, Chua, Henon };

class ChaosWaveshaper {
public:
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;  // maxBlockSize for oversampler
    void reset() noexcept;

    void setModel(ChaosModel model) noexcept;         // Defaults to Lorenz for invalid values (FR-036)
    void setChaosAmount(float amount) noexcept;       // [0, 1]
    void setAttractorSpeed(float speed) noexcept;     // [0.01, 100]
    void setInputCoupling(float coupling) noexcept;   // [0, 1]

    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    [[nodiscard]] size_t latency() const noexcept;    // Returns oversampler latency (0 for default settings)

private:
    Oversampler<2, 1> oversampler_;  // 2x mono oversampling (FR-034, FR-035)
};
```

## API Contracts

See [contracts/chaos_waveshaper.h](contracts/chaos_waveshaper.h) for complete header.

## Implementation Approach

### File Structure

Single header: `dsp/include/krate/dsp/primitives/chaos_waveshaper.h`

### Control Rate Processing

```cpp
// Note: kControlRateInterval defined in implementation (T005)
static constexpr size_t kControlRateInterval = 32;  // Update attractor every 32 samples

// Single-sample process (for per-sample use within oversampler callback)
float processInternal(float input) noexcept {
    input = sanitizeInput(input);

    if (--samplesUntilUpdate_ <= 0) {
        updateAttractor();
        samplesUntilUpdate_ = kControlRateInterval;
    }

    if (chaosAmount_ <= 0.0f) return input;  // FR-023: bypass

    float shaped = applyWaveshaping(input);
    return std::lerp(input, shaped, chaosAmount_);  // FR-021
}

// Block process with oversampling (FR-034, FR-035)
void processBlock(float* buffer, size_t numSamples) noexcept {
    if (chaosAmount_ <= 0.0f) return;  // Skip oversampling for bypass

    oversampler_.process(buffer, numSamples, [this](float* data, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            data[i] = processInternal(data[i]);
        }
    });
}
```

### Attractor Update

```cpp
void updateAttractor() noexcept {
    switch (model_) {
        case ChaosModel::Lorenz:  updateLorenz();  break;
        case ChaosModel::Rossler: updateRossler(); break;
        case ChaosModel::Chua:    updateChua();    break;
        case ChaosModel::Henon:   updateHenon();   break;
    }

    // Flush denormals
    state_.x = detail::flushDenormal(state_.x);
    state_.y = detail::flushDenormal(state_.y);
    state_.z = detail::flushDenormal(state_.z);

    // Check bounds and reset if diverged
    checkAndResetIfDiverged();

    // Update normalized output
    normalizedX_ = std::clamp(state_.x / normalizationFactor_, -1.0f, 1.0f);
}
```

## Testing Strategy

### Test Categories

1. **Bypass tests (SC-002):** chaosAmount=0.0 produces identical output
2. **Silence tests (SC-003):** Zero input produces zero output
3. **Time-variance tests (SC-001):** Output varies over time with constant input
4. **Bounded state tests (SC-005):** State stays bounded for extended processing
5. **Model distinctness tests (SC-006):** Different models produce different output
6. **Speed scaling tests (SC-007):** Speed parameter affects evolution rate
7. **Input coupling tests (SC-008):** Coupling creates input-correlated variation
8. **NaN/Inf handling tests:** Sanitization works correctly

### Performance Validation

- Process 1 million samples, measure CPU time
- Target: < 0.1% of buffer time at 44.1kHz

## Risk Assessment

### Low Risk
- Basic attractor math is well-documented
- Tanh waveshaping is proven
- Pattern follows existing primitives (Waveshaper, Wavefolder)

### Medium Risk
- Henon interpolation may need tuning for smooth output
- Chua parameters may need adjustment for musical results

### Mitigations
- Use proven parameters from literature
- Add parameter getters for experimentation
- Comprehensive test coverage catches regressions

---

## Architecture Documentation Update

After implementation, update `specs/_architecture_/layer-1-primitives.md` with:

```markdown
## ChaosWaveshaper
**Path:** [chaos_waveshaper.h](../../dsp/include/krate/dsp/primitives/chaos_waveshaper.h) | **Since:** 0.15.0

Time-varying waveshaping using chaos attractor dynamics.

**Use when:**
- Creating distortion that "breathes" and evolves
- Signal-reactive nonlinear processing
- Generative/experimental sound design
```
