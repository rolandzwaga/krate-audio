# Implementation Plan: Resonator Bank

**Branch**: `083-resonator-bank` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/083-resonator-bank/spec.md`

## Summary

Create a ResonatorBank processor (Layer 2) that provides 16 parallel tuned bandpass resonators for physical modeling applications. The component will support harmonic, inharmonic, and custom tuning modes with per-resonator control of frequency, decay, gain, and Q. Uses existing Biquad (Layer 1) for resonant filtering and OnePoleSmoother (Layer 1) for parameter smoothing.

## Technical Context

**Language/Version**: C++20 (per constitution)
**Primary Dependencies**: Biquad, OnePoleSmoother, dbToGain from KrateDSP
**Storage**: N/A (real-time processor)
**Testing**: Catch2 (existing test framework)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: DSP library component
**Performance Goals**: <1% CPU for 16 resonators at 192kHz (SC-001)
**Constraints**: Real-time safe (no allocations/locks/exceptions in process)
**Scale/Scope**: Single resonator bank instance, mono operation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() or processBlock()
- [x] No locks or blocking primitives
- [x] No exceptions in audio path
- [x] Pre-allocate all buffers at prepare() time

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] ResonatorBank belongs at Layer 2 (uses Layer 1 primitives)
- [x] Will depend only on Layer 0 (core) and Layer 1 (primitives)
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

**Classes/Structs to be created**: ResonatorBank, ResonatorParams (internal struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ResonatorBank | `grep -r "class ResonatorBank" dsp/ plugins/` | No | Create New |
| Resonator | `grep -r "class Resonator" dsp/ plugins/` | No | N/A - will use Biquad directly |

**Utility Functions to be created**: rt60ToQ

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| rt60ToQ | `grep -r "rt60ToQ\|RT60\|decayToQ" dsp/ plugins/` | No | N/A | Create New (inline in header) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Bandpass filter for each resonator |
| BiquadCoefficients::calculate() | dsp/include/krate/dsp/primitives/biquad.h | 1 | Calculate bandpass coefficients |
| FilterType::Bandpass | dsp/include/krate/dsp/primitives/biquad.h | 1 | Filter type for resonators |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (20ms) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Convert per-resonator dB gain to linear |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Prevent denormal CPU spikes |
| kPi | dsp/include/krate/dsp/core/math_constants.h | 0 | RT60-to-Q formula |
| kMinFilterFrequency | dsp/include/krate/dsp/primitives/biquad.h | 1 | Frequency clamping (1.0f) |
| kMaxQ | dsp/include/krate/dsp/primitives/biquad.h | 1 | Q clamping reference (30.0f) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing ResonatorBank or similar class found. All utility functions will be inline constexpr in the header to avoid ODR issues. The planned rt60ToQ formula is unique and not duplicated elsewhere.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| BiquadCoefficients | calculate | `[[nodiscard]] static BiquadCoefficients calculate(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| dbToGain | (function) | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| detail::flushDenormal | (function) | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| kPi | (constant) | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| FilterType::Bandpass | (enum) | `Bandpass, ///< Constant 0 dB peak gain` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class and FilterType enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Biquad | Bandpass has 0dB peak gain | No gainDb adjustment needed for bandpass |
| OnePoleSmoother | Uses configure() not setTime() | `smoother.configure(20.0f, sampleRate)` |
| OnePoleSmoother | Uses process() not getValue() | `float smoothed = smoother.process()` |
| Biquad | kMaxQ is 30.0f not 100.0f | Spec says max Q=100, need to define our own |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| rt60ToQ | May be useful for Karplus-Strong, Modal Resonator | resonance_utils.h (new) | ResonatorBank, future physical modeling |

**Decision**: Keep rt60ToQ as an inline constexpr in the ResonatorBank header for now. If Phase 13.2-13.4 (Karplus-Strong, Waveguide, Modal) also need this formula, extract to Layer 0 at that time. Premature extraction would violate YAGNI.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| applySpectralTilt | Specific to ResonatorBank's per-resonator gain approach |
| calculateInharmonicFrequency | Could be shared but wait for 2nd consumer |

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- Phase 13.2: Karplus-Strong String
- Phase 13.3: Waveguide Resonator
- Phase 13.4: Modal Resonator

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| rt60ToQ formula | HIGH | Karplus-Strong, Modal | Keep local, extract after 2nd use |
| Inharmonicity formula | MEDIUM | Waveguide (possibly) | Keep local |
| Spectral tilt | LOW | None identified | Keep as member function |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First physical modeling component - patterns not established |
| Keep rt60ToQ inline | Only one consumer so far, YAGNI |
| Keep inharmonicity inline | Specific formula for strings/bells, not generic |

### Review Trigger

After implementing **Phase 13.2 Karplus-Strong**, review this section:
- [ ] Does Karplus-Strong need rt60ToQ? -> Extract to resonance_utils.h
- [ ] Does Karplus-Strong use inharmonicity? -> Extract if formula matches
- [ ] Any duplicated damping code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/083-resonator-bank/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API header contract)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── resonator_bank.h    # New: ResonatorBank class (header-only)
└── tests/
    └── processors/
        └── resonator_bank_tests.cpp  # New: Unit tests
```

**Structure Decision**: Single header-only implementation following existing Layer 2 processor pattern. Tests in matching directory structure.

## Complexity Tracking

No Constitution violations requiring justification. Design follows all principles.

---

## Post-Design Constitution Re-Check

*Re-evaluated after Phase 1 design completion.*

### Principle II (Real-Time Audio Thread Safety) - PASS

| Check | Status | Evidence |
|-------|--------|----------|
| No allocations in process() | PASS | Uses `std::array` (stack-allocated), no dynamic allocation |
| No locks | PASS | No mutex, lock_guard, or blocking primitives |
| No exceptions | PASS | All methods marked `noexcept` |
| Pre-allocate at prepare() | PASS | All 16 Biquads and smoothers initialized at construction |

### Principle III (Modern C++ Standards) - PASS

| Check | Status | Evidence |
|-------|--------|----------|
| C++20 features | PASS | Uses `std::array`, `constexpr`, `[[nodiscard]]` |
| RAII | PASS | No manual memory management |
| Value semantics | PASS | No raw pointers, copies are safe |
| `const`/`constexpr` | PASS | Constants are `inline constexpr`, utility functions `constexpr` |

### Principle IX (Layered DSP Architecture) - PASS

| Check | Status | Evidence |
|-------|--------|----------|
| Correct layer | PASS | Layer 2 (Processor) - composes Layer 1 primitives |
| Dependencies | PASS | Only depends on Layer 0 (db_utils, math_constants) and Layer 1 (biquad, smoother) |
| No circular deps | PASS | No upward dependencies to Layer 3/4 |

### Principle XIV (ODR Prevention) - PASS

| Check | Status | Evidence |
|-------|--------|----------|
| Searched for conflicts | PASS | `grep -r "ResonatorBank\|resonator" dsp/` returned no results |
| New types unique | PASS | ResonatorBank, TuningMode not found elsewhere |
| Utility functions inline | PASS | rt60ToQ, calculateTiltGain are `inline constexpr` |

### Design Validation Summary

All constitution principles are satisfied. The design:

1. **Is real-time safe**: Fixed-size arrays, no allocations, all noexcept
2. **Follows modern C++**: Uses standard library containers, constexpr utilities
3. **Respects layer boundaries**: Only uses Layer 0-1 components
4. **Avoids ODR violations**: All new types are unique, inline utilities

**Ready for Phase 2 task generation.**

---

## Generated Artifacts

| Artifact | Path | Description |
|----------|------|-------------|
| Implementation Plan | `F:\projects\iterum\specs\083-resonator-bank\plan.md` | This file |
| Research | `F:\projects\iterum\specs\083-resonator-bank\research.md` | Technical decisions and formulas |
| Data Model | `F:\projects\iterum\specs\083-resonator-bank\data-model.md` | Entity definitions and state |
| API Contract | `F:\projects\iterum\specs\083-resonator-bank\contracts\resonator_bank.h` | Header interface |
| Quickstart | `F:\projects\iterum\specs\083-resonator-bank\quickstart.md` | Usage examples |
