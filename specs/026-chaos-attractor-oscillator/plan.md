# Implementation Plan: Chaos Attractor Oscillator

**Branch**: `026-chaos-attractor-oscillator` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/026-chaos-attractor-oscillator/spec.md`

## Summary

Audio-rate chaos oscillator implementing 5 attractor types (Lorenz, Rossler, Chua, Duffing, VanDerPol) with RK4 adaptive substepping, per-attractor frequency scaling constants, additive external coupling, DC blocking, and tanh normalization. Unlike the existing control-rate `ChaosModSource`, this oscillator runs at full audio rate with approximate pitch tracking via dt scaling.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 (`fast_math.h`, `math_constants.h`, `db_utils.h`), Layer 1 (`dc_blocker.h`)
**Storage**: N/A (stateful DSP, in-memory only)
**Testing**: Catch2 (`dsp/tests/unit/processors/`)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: Monorepo DSP library
**Performance Goals**: < 1% CPU per instance @ 44.1kHz stereo (SC-007, Layer 2 budget)
**Constraints**: Zero allocations in audio thread, no exceptions, noexcept processing
**Scale/Scope**: Single DSP processor at Layer 2

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in process() methods
- [x] All processing methods marked noexcept
- [x] No exceptions, locks, or I/O in audio path
- [x] Buffers pre-allocated in prepare()

**Required Check - Principle IX (Layer Architecture):**
- [x] Component is Layer 2 (processors/)
- [x] Only depends on Layer 0 (core/) and Layer 1 (primitives/)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**:
- `ChaosOscillator` - main oscillator class
- `ChaosAttractor` - enum for attractor types

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ChaosOscillator | `grep -r "class ChaosOscillator" dsp/ plugins/` | No | Create New |
| ChaosAttractor | `grep -r "enum.*ChaosAttractor" dsp/ plugins/` | No | Create New (distinct from ChaosModel) |
| AttractorState | `grep -r "struct AttractorState" dsp/ plugins/` | Yes (private in ChaosModSource, ChaosWaveshaper) | Create New (internal struct, different scope) |

**Note on ChaosModel vs ChaosAttractor**: The existing `ChaosModel` enum in `chaos_waveshaper.h` has 4 types (Lorenz, Rossler, Chua, Henon). The new `ChaosAttractor` enum will have 5 types (Lorenz, Rossler, Chua, Duffing, VanDerPol). These are intentionally separate because:
1. Different attractor sets (Henon is discrete map not suitable for audio-rate, Duffing/VanDerPol are continuous and audio-suitable)
2. Different use cases (waveshaping vs oscillator)
3. The spec explicitly defines the new enum

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| rk4Step | `grep -r "rk4Step\|rungeKutta" dsp/ plugins/` | No | N/A | Create New (inline in class) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | DC blocking on output (FR-009) |
| fastTanh | `dsp/include/krate/dsp/core/fast_math.h` | 0 | Output normalization (FR-008) |
| kPi, kTwoPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Duffing cosine calculation |
| flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | State variable sanitization |
| isNaN, isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input/state validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (ChaosModSource exists)
- [x] `specs/_architecture_/` - Component inventory
- [x] `specs/OSC-ROADMAP.md` - Oscillator roadmap (Phase 12)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The planned `ChaosOscillator` class and `ChaosAttractor` enum are unique names not found in the codebase. The internal `AttractorState` struct is private to each class and in different compilation units, so no ODR conflict. The existing `ChaosModSource` and `ChaosWaveshaper` are separate components with different purposes.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| fastTanh | fastTanh | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Yes |
| detail::flushDenormal | flushDenormal | `inline float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | isInf | `constexpr bool isInf(float x) noexcept` | Yes |
| kPi | constant | `inline constexpr float kPi` | Yes |
| kTwoPi | constant | `inline constexpr float kTwoPi` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/core/fast_math.h` - fastTanh function
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DCBlocker | Must call `prepare()` before `process()` | `blocker_.prepare(sampleRate, 10.0f)` in `prepare()` |
| fastTanh | Located in `FastMath` namespace | `FastMath::fastTanh(x)` |
| flushDenormal | Located in `detail` namespace | `detail::flushDenormal(x)` |
| isNaN/isInf | Located in `detail` namespace | `detail::isNaN(x)`, `detail::isInf(x)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| RK4 integration | General numerical integration | Not recommended | ChaosOscillator only |

**Decision**: Keep RK4 integration as member function within ChaosOscillator. RK4 is specific to ODE systems and the implementation is tightly coupled to the attractor derivative functions. No other DSP components currently need generic RK4, and premature extraction would add complexity without benefit. If a second consumer emerges (e.g., physical modeling), then consider extraction.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| rk4Step | Tightly coupled to attractor-specific derivative calculations |
| computeDerivatives | Per-attractor derivative equations, not general-purpose |
| dtMax constants | Per-attractor tuning, not reusable |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (processors/)

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 13: Formant Oscillator - FOF technique, no shared components
- Phase 14: Particle/Swarm Oscillator - Many lightweight sines, no chaos
- Phase 15: Rungler/Shift Register - Different chaos approach (digital, not ODE)
- Phase 16: Spectral Freeze Oscillator - FFT-based, no shared components

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ChaosOscillator | LOW | None identified | Keep local |
| ChaosAttractor enum | LOW | ChaosOscillator only | Keep local |
| RK4 integration | LOW | Possibly physical modeling (future) | Keep local, extract if 2nd use emerges |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared chaos base class | ChaosModSource and ChaosOscillator have fundamentally different designs (control-rate Euler vs audio-rate RK4) |
| Separate ChaosAttractor enum | Different attractor sets for different use cases |
| Keep RK4 local | Only one consumer, tightly coupled to attractor derivatives |

### Review Trigger

After implementing **Phase 15 (Rungler)**, review this section:
- [ ] Does Rungler need ODE integration? Unlikely (it's a shift register, not continuous system)
- [ ] Any duplicated chaos concepts? Check for attractor-like state management

## Project Structure

### Documentation (this feature)

```text
specs/026-chaos-attractor-oscillator/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (N/A for DSP - no external API)
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── chaos_oscillator.h     # Main implementation (FR-001 to FR-023)
└── tests/
    └── unit/
        └── processors/
            └── chaos_oscillator_test.cpp  # Unit tests (SC-001 to SC-008)
```

**Structure Decision**: Single header-only implementation at Layer 2 processors, following the established pattern of other oscillators (PhaseDistortionOscillator, AdditiveOscillator). Test file co-located with other processor tests.

## Complexity Tracking

> No Constitution Check violations requiring justification.

N/A - All constitution principles are satisfied by the design.

---

## Phase 0: Research

### Research Tasks

1. **RK4 Implementation Patterns for Audio DSP**
   - Best practices for fixed-timestep RK4 in audio applications
   - Memory layout for state vectors
   - Inline vs function call overhead

2. **Adaptive Substepping Without Heap Allocation**
   - Fixed maximum substeps approach (avoid dynamic allocation)
   - Compute `numSubsteps = ceil(dt / dtMax)` with integer arithmetic
   - Loop unrolling considerations

3. **Testing Strategies for Chaotic Systems**
   - Bounded output verification (primary - SC-001)
   - Divergence detection and recovery (SC-002)
   - Spectral analysis for timbre verification (SC-005, SC-006)
   - Approximate frequency verification (SC-008)

### Research Findings

See [research.md](research.md) for detailed findings.

**Key Decisions:**

| Decision | Rationale | Alternatives Rejected |
|----------|-----------|----------------------|
| Fixed-size state arrays | Real-time safe, no heap allocation | `std::vector` (heap allocation in audio thread) |
| Inline RK4 per attractor | Best performance, type-specific optimization | Generic templated RK4 (virtual call overhead) |
| Maximum 100 substeps cap | Prevents CPU spike at extreme low frequencies | Unlimited substepping (CPU unbounded) |
| DCBlocker at 10Hz | Standard DC blocking, matches existing usage | Higher cutoff (removes wanted frequencies) |

---

## Phase 1: Design

### Data Model

See [data-model.md](data-model.md) for detailed entity definitions.

**Key Entities:**

```cpp
// ChaosAttractor enum (FR-001 to FR-005)
enum class ChaosAttractor : uint8_t {
    Lorenz = 0,
    Rossler = 1,
    Chua = 2,
    Duffing = 3,
    VanDerPol = 4
};

// Internal state structure
struct AttractorState {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;  // Used as 'v' for 2D systems (Duffing, VanDerPol)
};

// Per-attractor constants
struct AttractorConstants {
    float dtMax;              // Stability threshold (FR-006)
    float baseDt;             // Base timestep for frequency scaling (FR-007)
    float referenceFrequency; // Reference frequency for scaling (FR-007)
    float safeBound;          // Divergence detection bound (FR-011)
    float xScale, yScale, zScale;  // Normalization scales (FR-008)
    float chaosMin, chaosMax, chaosDefault;  // Chaos parameter range (FR-019)
    AttractorState initialState;  // Reset state (FR-012)
};
```

### API Contracts

N/A - This is a DSP primitive with no external API contracts. The public interface is defined in the spec (FR-015 to FR-023).

### Quickstart

See [quickstart.md](quickstart.md) for usage examples.

```cpp
// Basic usage
ChaosOscillator osc;
osc.prepare(44100.0);
osc.setAttractor(ChaosAttractor::Lorenz);
osc.setFrequency(220.0f);
osc.setChaos(1.0f);

// Process single sample
float sample = osc.process();

// Process block
float buffer[512];
osc.processBlock(buffer, 512, nullptr);
```

---

## Implementation Phases

### Phase N-1.0: Quality Gate (Pre-Implementation)

**Tasks:**
- [ ] Run clang-tidy on related files
- [ ] Verify all existing tests pass
- [ ] Review spec one more time for completeness

### Phase N.0: Test Infrastructure

**Tasks:**
1. Create test file `dsp/tests/unit/processors/chaos_oscillator_test.cpp`
2. Add test file to CMakeLists.txt
3. Write test stubs for all FR-xxx and SC-xxx requirements
4. Verify test file compiles (tests should fail or skip)

### Phase N.1: Core Implementation

**Tasks (TDD cycle per attractor):**

1. **Lorenz Attractor (FR-001)**
   - Write test for Lorenz equations
   - Implement Lorenz derivative computation
   - Write test for bounded output
   - Implement RK4 integration

2. **Rossler Attractor (FR-002)**
   - Write test for Rossler equations
   - Implement Rossler derivative computation

3. **Chua Circuit (FR-003)**
   - Write test for Chua equations with h(x)
   - Implement Chua diode function
   - Implement Chua derivative computation

4. **Duffing Oscillator (FR-004)**
   - Write test for Duffing equations with driving term
   - Implement phase accumulator for cosine term
   - Implement Duffing derivative computation

5. **Van der Pol Oscillator (FR-005)**
   - Write test for Van der Pol equations
   - Implement Van der Pol derivative computation

### Phase N.2: Integration Features

**Tasks:**

1. **Adaptive Substepping (FR-006)**
   - Write test for substep count calculation
   - Implement adaptive substepping loop
   - Write test for stability at various frequencies

2. **Frequency Scaling (FR-007)**
   - Write test for dt calculation from frequency
   - Implement per-attractor frequency scaling
   - Write test for approximate pitch tracking (SC-008)

3. **Output Normalization (FR-008)**
   - Write test for bounded output [-1, 1]
   - Implement tanh soft-limiting
   - Implement per-axis normalization scales

4. **DC Blocking (FR-009)**
   - Write test for DC offset removal
   - Integrate DCBlocker primitive
   - Write test for settling time (SC-004)

### Phase N.3: Safety Features

**Tasks:**

1. **Divergence Detection (FR-011)**
   - Write test for state bound checking
   - Implement per-attractor safe bounds
   - Write test for NaN/Inf detection

2. **State Reset (FR-012)**
   - Write test for reset to initial conditions
   - Implement per-attractor initial states

3. **Reset Cooldown (FR-013)**
   - Write test for cooldown timing
   - Implement sample counter for cooldown

4. **NaN Input Handling (FR-014)**
   - Write test for NaN external input
   - Implement input sanitization

### Phase N.4: Interface and Polish

**Tasks:**

1. **Public Interface (FR-015 to FR-023)**
   - Implement all setters/getters
   - Implement process() and processBlock()
   - Write comprehensive tests for each method

2. **External Coupling (FR-020)**
   - Write test for coupling behavior
   - Implement additive forcing in x-derivative

3. **Documentation**
   - Add Doxygen comments
   - Update architecture docs

### Phase N+1.0: Quality Gate (Post-Implementation)

**Tasks:**
- [ ] All tests pass
- [ ] Run clang-tidy, fix all warnings
- [ ] Run pluginval (if plugin integration)
- [ ] Update `specs/_architecture_/layer-2-processors.md`
- [ ] Fill compliance table in spec.md

---

## Success Criteria Verification Plan

| SC | Verification Method | Test Name |
|----|---------------------|-----------|
| SC-001 | 10 second continuous processing, check bounds | `BoundedOutput10Seconds` |
| SC-002 | Force divergence, measure recovery time | `DivergenceRecoveryTime` |
| SC-003 | Process at 20Hz-2000Hz, verify no NaN/Inf | `NumericalStability` |
| SC-004 | Step response, measure DC after 100ms | `DCBlockerSettling` |
| SC-005 | Vary chaos 0-1, compute spectral centroid shift | `ChaosCentroidShift` |
| SC-006 | Compare spectral centroid across attractors | `AttractorSpectralDifference` |
| SC-007 | Benchmark CPU usage | `CPUBudget` |
| SC-008 | Set 440Hz, verify fundamental in 220-660Hz range | `FrequencyTracking` |

---

## Files to Create/Modify

### New Files

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/chaos_oscillator.h` | Main implementation |
| `dsp/tests/unit/processors/chaos_oscillator_test.cpp` | Unit tests |
| `specs/026-chaos-attractor-oscillator/research.md` | Research findings |
| `specs/026-chaos-attractor-oscillator/data-model.md` | Data model |
| `specs/026-chaos-attractor-oscillator/quickstart.md` | Usage guide |

### Modified Files

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add chaos_oscillator_test.cpp |
| `specs/_architecture_/layer-2-processors.md` | Add ChaosOscillator entry |
| `specs/OSC-ROADMAP.md` | Update Phase 12 status |
