# Implementation Plan: Hilbert Transform

**Branch**: `094-hilbert-transform` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/094-hilbert-transform/spec.md`

## Summary

Implement a Hilbert transform component for the Krate::DSP library using an allpass filter cascade approximation. This is a Layer 1 (Primitives) component that produces an analytic signal with 90-degree phase-shifted quadrature output. The implementation will reuse the existing `Allpass1Pole` class with Olli Niemitalo coefficients for wideband phase accuracy. Primary consumer is the FrequencyShifter component (Phase 16.3).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- `Allpass1Pole` (existing Layer 1 primitive)
- `math_constants.h` (Layer 0 - kPi)
- `db_utils.h` (Layer 0 - flushDenormal, isNaN, isInf)
**Storage**: N/A (stateless coefficients, all state in allpass filters)
**Testing**: Catch2 unit tests following existing primitives pattern
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: DSP library (monorepo)
**Performance Goals**: < 0.1% CPU per primitive, < 10ns/sample processing
**Constraints**: noexcept, zero allocations in process, real-time safe
**Scale/Scope**: Single header-only component, ~200 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP primitive, not plugin code |
| II. Real-Time Audio Thread Safety | PASS | All methods noexcept, no allocations |
| III. Modern C++ Standards | PASS | C++20, constexpr, RAII |
| IV. SIMD & DSP Optimization | PASS | Sequential processing, no branches in hot path |
| V. VSTGUI Development | N/A | No UI |
| VI. Cross-Platform Compatibility | PASS | Standard C++, no platform-specific code |
| VII. Project Structure | PASS | Header in `dsp/include/krate/dsp/primitives/` |
| VIII. Testing Discipline | PASS | Unit tests before implementation |
| IX. Layered DSP Architecture | PASS | Layer 1, depends only on Layer 0 and other Layer 1 |
| X. DSP Processing Constraints | PASS | Allpass filters, no saturation needing oversampling |
| XI. Performance Budgets | PASS | < 0.1% CPU target |
| XII. Debugging Discipline | PASS | Will follow test-first approach |
| XIII. Test-First Development | PASS | Tests written before implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-1-primitives.md |
| XV. Pre-Implementation Research (ODR) | PASS | See Codebase Research below |
| XVI. Honest Completion | PASS | Will verify all FR/SC requirements |
| XVII. Framework Knowledge | N/A | Not using VSTGUI/VST3 SDK directly |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture) - verified in context
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `HilbertTransform`, `HilbertOutput`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| HilbertTransform | `grep -r "class HilbertTransform" dsp/ plugins/` | No | Create New |
| HilbertOutput | `grep -r "struct HilbertOutput" dsp/ plugins/` | No | Create New |
| hilbert | `grep -r "hilbert" dsp/ plugins/` (case-insensitive) | No | Create New |
| analytic signal | `grep -r "analytic" dsp/ plugins/` (case-insensitive) | No | N/A |
| quadrature | `grep -r "quadrature" dsp/ plugins/` (case-insensitive) | No | N/A |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | db_utils.h | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | db_utils.h | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Allpass1Pole | dsp/include/krate/dsp/primitives/allpass_1pole.h | 1 | 8 instances for 2x4 allpass cascades |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing in process |
| isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection for safety reset |
| isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Inf detection for safety reset |
| kPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Not directly needed (allpass uses internally) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (30 files checked)
- [x] `specs/_architecture_/` - Component inventory (README.md, layer-1-primitives.md)
- [x] `dsp/include/krate/dsp/primitives/allpass_1pole.h` - Primary dependency

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (HilbertTransform, HilbertOutput) are unique and not found in codebase. No similar Hilbert, analytic signal, or quadrature implementations exist. The component reuses established Allpass1Pole pattern.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Allpass1Pole | prepare | `void prepare(double sampleRate) noexcept` | YES |
| Allpass1Pole | setCoefficient | `void setCoefficient(float a) noexcept` | YES |
| Allpass1Pole | process | `[[nodiscard]] float process(float input) noexcept` | YES |
| Allpass1Pole | reset | `void reset() noexcept` | YES |
| detail::flushDenormal | N/A | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | YES |
| detail::isNaN | N/A | `constexpr bool isNaN(float x) noexcept` | YES |
| detail::isInf | N/A | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | YES |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/allpass_1pole.h` - Allpass1Pole class (full API verified)
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf in detail namespace
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi constant (not directly needed)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Allpass1Pole | Must call `prepare()` before `setCoefficient()` | `ap.prepare(sr); ap.setCoefficient(coeff);` |
| Allpass1Pole | Uses `setCoefficient()` not `setFrequency()` for direct coefficient | Use `setCoefficient()` for Hilbert coefficients |
| detail::isNaN/isInf | In `detail` namespace | `detail::isNaN(x)` not `isNaN(x)` |
| detail::flushDenormal | In `detail` namespace | `detail::flushDenormal(x)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**N/A** - This is a Layer 1 primitive. No Layer 0 extraction candidates identified. All required utilities already exist in Layer 0.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from ROADMAP.md or known plans):
- N/A at Layer 1 (HilbertTransform is the primitive)

**Related consumers at higher layers:**
- FrequencyShifter (Layer 2 Processor) - Primary consumer, uses SSB modulation
- Future ring modulator effects
- Potential EnvelopeFollower Hilbert-based detection mode

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| HilbertTransform | HIGH | FrequencyShifter, ring modulators, envelope detection | This IS the reusable primitive |
| HilbertOutput struct | MEDIUM | Any dual-output processor | Keep in hilbert_transform.h, could be generalized later |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| HilbertOutput kept simple (i, q fields) | Matches spec API, FrequencyShifter needs exactly this |
| No shared base class for dual-output | First dual-output primitive - pattern not established |

## Project Structure

### Documentation (this feature)

```text
specs/094-hilbert-transform/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output (coefficients verified)
├── data-model.md        # Phase 1 output (class structure)
├── quickstart.md        # Phase 1 output (usage examples)
├── contracts/           # Phase 1 output (API header)
│   └── hilbert_transform.h
└── tasks.md             # Phase 2 output (implementation tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── hilbert_transform.h      # NEW - Header-only implementation
├── tests/
│   └── unit/primitives/
│       └── hilbert_transform_test.cpp  # NEW - Catch2 tests
└── CMakeLists.txt                   # UPDATE - Add test file

specs/_architecture_/
└── layer-1-primitives.md            # UPDATE - Add HilbertTransform section
```

**Structure Decision**: Header-only implementation in `primitives/` following existing pattern (allpass_1pole.h). Single test file in `unit/primitives/`.

## Complexity Tracking

> **No violations requiring justification.**

The implementation follows all constitution principles without deviation.

---

## Research Summary (Phase 0)

### Olli Niemitalo Allpass Coefficients

The Hilbert transform implementation uses two parallel cascades of 4 `Allpass1Pole` instances. The coefficients are optimized by Olli Niemitalo for wideband 90-degree phase accuracy.

**Path 1 (In-phase path)** - 4 `Allpass1Pole` instances + 1-sample delay:
```cpp
constexpr float kPath1Coeffs[4] = {
    0.6923878f,
    0.9360654322959f,
    0.9882295226860f,
    0.9987488452737f
};
```

**Path 2 (Quadrature path)** - 4 `Allpass1Pole` instances:
```cpp
constexpr float kPath2Coeffs[4] = {
    0.4021921162426f,
    0.8561710882420f,
    0.9722909545651f,
    0.9952884791278f
};
```

**Reference**: [Olli Niemitalo - Hilbert Transform](https://yehar.com/blog/?p=368)

### Design Decisions from Specification

1. **Latency**: 5 samples fixed (1 sample explicit delay + ~4 samples group delay from allpass cascade)
2. **Implementation**: Reuse existing `Allpass1Pole` class
3. **Settling time**: 5 samples after reset()
4. **Sample rate validation**: Clamp to range (22050-192000Hz) silently
5. **Coefficients**: Compile-time constexpr constants

### Phase Accuracy

The allpass cascade approximation achieves:
- +/- 0.7 degree phase accuracy over 0.002 to 0.998 of Nyquist
- At 44.1kHz: effective bandwidth ~40Hz to ~20kHz with +/-1 degree accuracy
- Bandwidth scales proportionally with sample rate

---

## Data Model (Phase 1)

### HilbertOutput Struct

```cpp
/// Output structure containing both components of the analytic signal
struct HilbertOutput {
    float i;  ///< In-phase component (original signal, delayed)
    float q;  ///< Quadrature component (90 degrees phase-shifted)
};
```

### HilbertTransform Class

```cpp
class HilbertTransform {
public:
    // Lifecycle
    HilbertTransform() noexcept = default;

    // Configuration
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Processing
    [[nodiscard]] HilbertOutput process(float input) noexcept;
    void processBlock(const float* input, float* outI, float* outQ,
                      int numSamples) noexcept;

    // State Query
    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] int getLatencySamples() const noexcept;

private:
    // Path 1: 4 Allpass1Pole instances + 1 sample delay -> in-phase output
    Allpass1Pole ap1_[4];
    float delay1_ = 0.0f;

    // Path 2: 4 Allpass1Pole instances -> quadrature output
    Allpass1Pole ap2_[4];

    double sampleRate_ = 44100.0;
};
```

### Processing Algorithm

```
Input signal x[n]
       │
       ├──────────────────────────────────────────┐
       │                                          │
       ▼                                          ▼
   Path 1 (I)                                 Path 2 (Q)
       │                                          │
       ▼                                          ▼
   AP(a1) -> AP(a2) -> AP(a3) -> AP(a4)       AP(b1) -> AP(b2) -> AP(b3) -> AP(b4)
       │                                          │
       ▼                                          │
   z^-1 (1-sample delay)                         │
       │                                          │
       ▼                                          ▼
   Output I (in-phase)                        Output Q (quadrature)
```

---

## Implementation Tasks Overview

The implementation will follow test-first development with these task groups:

### Task Group 1: Preparation & Reset (FR-001 to FR-003)
- Write tests for prepare() sample rate handling
- Write tests for reset() clearing state
- Write tests for sample rate clamping
- Implement HilbertOutput struct
- Implement HilbertTransform class skeleton
- Implement prepare() with coefficient initialization
- Implement reset()

### Task Group 2: Single Sample Processing (FR-004, FR-006, FR-007)
- Write tests for process() returning HilbertOutput
- Write tests for phase difference verification
- Implement process() with dual allpass paths
- Verify 90-degree phase relationship

### Task Group 3: Block Processing (FR-005)
- Write tests for processBlock() matching process()
- Write tests for block processing bit-exactness
- Implement processBlock()

### Task Group 4: Phase Accuracy (FR-008 to FR-010)
- Write tests for phase accuracy at 100Hz, 1kHz, 5kHz, 10kHz
- Write tests for magnitude unity within 0.1dB
- Verify phase accuracy meets +/-1 degree spec

### Task Group 5: State Query & Real-Time Safety (FR-015 to FR-019)
- Write tests for getSampleRate()
- Write tests for getLatencySamples() returning 5
- Write tests for NaN/Inf handling
- Write tests for noexcept guarantees
- Implement state query methods
- Implement NaN/Inf handling with reset

### Task Group 6: Success Criteria Verification (SC-001 to SC-010)
- Systematic verification of all SC requirements
- Performance testing (<10ms for 1 second at 44.1kHz)
- Settling time verification (5 samples)
- Sample rate clamping edge cases

### Task Group 7: Documentation & Integration
- Update layer-1-primitives.md
- Update dsp/CMakeLists.txt to include test file
- Update dsp/tests/CMakeLists.txt

---

## Post-Design Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| IX. Layered DSP Architecture | PASS | Layer 1, depends only on Layer 0 + Layer 1 (Allpass1Pole) |
| X. DSP Processing Constraints | PASS | No saturation, allpass filters only |
| XIII. Test-First Development | PASS | All task groups start with test writing |
| XV. Pre-Implementation Research | PASS | ODR prevention verified, no conflicts |
| XVI. Honest Completion | PASS | All FR/SC will be verified systematically |

---

## Quick Reference

### Key Files
- **Implementation**: `dsp/include/krate/dsp/primitives/hilbert_transform.h`
- **Tests**: `dsp/tests/unit/primitives/hilbert_transform_test.cpp`
- **Specification**: `specs/094-hilbert-transform/spec.md`

### Build Commands
```bash
# Configure
cmake --preset windows-x64-release

# Build DSP tests
cmake --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[hilbert]"
```

### Success Criteria Summary
| SC | Requirement | Verification Method |
|----|-------------|---------------------|
| SC-001 | Phase 90 +/-1 degree at 100Hz, 1kHz, 5kHz, 10kHz | Cross-correlation phase measurement |
| SC-002 | Magnitude difference < 0.1dB | RMS comparison |
| SC-003 | 1 second @ 44.1kHz < 10ms | Chrono timing |
| SC-004 | No allocations in process | noexcept + manual review |
| SC-005 | Block == sample-by-sample | Bit-exact comparison |
| SC-006 | Deterministic after reset | Repeated reset verification |
| SC-007 | All sample rates work | Test at 44.1, 48, 96, 192kHz |
| SC-008 | Phase accuracy after 5 samples settling | Settling time test |
| SC-009 | getLatencySamples() returns 5 | Direct assertion |
| SC-010 | Sample rate clamping | Edge case tests |
