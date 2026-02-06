# Implementation Plan: Particle / Swarm Oscillator

**Branch**: `028-particle-oscillator` | **Date**: 2026-02-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/028-particle-oscillator/spec.md`

## Summary

Implement a ParticleOscillator as a Layer 2 processor in KrateDSP that generates complex textural timbres from a pool of up to 64 lightweight sine oscillators ("particles"). Each particle has individual frequency offset (scatter), drift (low-pass filtered random walk), lifetime, and grain envelope shaping. Three spawn modes control temporal pattern: Regular (evenly spaced), Random (stochastic), and Burst (manual trigger). Output is normalized by `1/sqrt(density)` for stable perceived loudness. All memory is pre-allocated; all processing is real-time safe.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 only -- random.h (Xorshift32), grain_envelope.h (GrainEnvelope, GrainEnvelopeType), pitch_utils.h (semitonesToRatio), math_constants.h (kTwoPi), db_utils.h (isNaN, isInf, flushDenormal)
**Storage**: N/A (all state in fixed-size arrays)
**Testing**: Catch2 via dsp_tests target *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform header-only
**Project Type**: KrateDSP library component (header-only, single file)
**Performance Goals**: < 0.5% single core at 64 particles, 44.1kHz (Layer 2 budget)
**Constraints**: Zero heap allocation in audio path; all noexcept; fixed 64-particle pool
**Scale/Scope**: Single header file (~400-500 lines), single test file (~600-800 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**:

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process/setters (fixed std::array<Particle, 64>)
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions (all noexcept)
- [x] No I/O in processing path
- [x] Pre-allocated buffers (envelope tables, particle pool)

**Required Check - Principle IX (Layered Architecture):**
- [x] Component is Layer 2 (processors/)
- [x] Only depends on Layer 0 (core/) -- no Layer 1+ dependencies
- [x] Include pattern: `<krate/dsp/core/...>` only

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific code
- [x] Uses std::sin, std::sqrt, std::clamp -- all portable
- [x] Header-only implementation

**Post-Design Re-Check (PASSED)**:
- [x] All design decisions comply with constitution
- [x] No real-time safety violations in API contract
- [x] Layer dependency rules respected (Layer 2 using only Layer 0)
- [x] No ODR conflicts found

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ParticleOscillator, Particle (internal struct), SpawnMode (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ParticleOscillator | `grep -r "ParticleOscillator" dsp/ plugins/` | No | Create New |
| Particle | `grep -r "class Particle[^O]" dsp/ plugins/` | No | Create New (detail namespace) |
| SpawnMode | `grep -r "enum class SpawnMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all new logic is within the class)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| sanitizeOutput | `grep -r "sanitizeOutput" dsp/` | Yes | AdditiveOscillator | Same pattern, implement as private static method |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | PRNG for scatter offsets, spawn timing, drift noise |
| GrainEnvelope::generate() | dsp/include/krate/dsp/core/grain_envelope.h | 0 | Pre-compute envelope lookup tables in prepare() |
| GrainEnvelope::lookup() | dsp/include/krate/dsp/core/grain_envelope.h | 0 | Per-sample envelope amplitude lookup with interpolation |
| GrainEnvelopeType | dsp/include/krate/dsp/core/grain_envelope.h | 0 | Envelope type enumeration (Hann, Trapezoid, etc.) |
| semitonesToRatio() | dsp/include/krate/dsp/core/pitch_utils.h | 0 | Convert scatter semitones to frequency ratio |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Phase accumulator: sin(kTwoPi * phase) |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection in output sanitization and input validation |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection in output sanitization |
| detail::flushDenormal() | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing in drift filter |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing particle_oscillator.h)
- [x] `specs/_architecture_/layer-2-processors.md` - No ParticleOscillator listed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned types (ParticleOscillator, Particle, SpawnMode) are confirmed unique in the codebase via grep search. The Particle struct is in a detail namespace or private to avoid any future conflict. The SpawnMode enum name is unique (existing scheduling uses SchedulingMode).

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` (returns [-1, 1]) | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` (returns [0, 1]) | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| GrainEnvelope | generate | `inline void generate(float* output, size_t size, GrainEnvelopeType type, float attackRatio = 0.1f, float releaseRatio = 0.1f) noexcept` | Yes |
| GrainEnvelope | lookup | `[[nodiscard]] inline float lookup(const float* table, size_t tableSize, float phase) noexcept` | Yes |
| semitonesToRatio | function | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| kTwoPi | constant | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |
| detail::isNaN | function | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | function | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| detail::flushDenormal | function | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/core/grain_envelope.h` - GrainEnvelope namespace, GrainEnvelopeType enum
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio function
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kTwoPi constant
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf, detail::flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 | `nextFloat()` returns [-1, 1] (bipolar), `nextUnipolar()` returns [0, 1] | Use `nextFloat()` for scatter offsets, `nextUnipolar()` for spawn timing |
| GrainEnvelope::generate | Has optional attackRatio/releaseRatio params (default 0.1) | Only relevant for Trapezoid/Linear/Exponential types |
| GrainEnvelope::lookup | Phase param is [0, 1], not [0, tableSize] | Pass envelope progress directly as phase |
| semitonesToRatio | Returns ratio (e.g., 2.0 for +12 semitones), not Hz | Multiply: `centerHz * semitonesToRatio(offset)` |
| detail::isNaN/isInf | Source file must compile with `-fno-fast-math` | Add test file to CMake `set_source_files_properties` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| sanitizeOutput | Static helper, same pattern as AdditiveOscillator but not worth extracting to shared location (only 3 lines, two consumers) |
| spawnParticle | Class-specific logic using private state |
| processParticle | Class-specific logic using Particle struct |

**Decision**: No new Layer 0 utilities needed. All new code is specific to ParticleOscillator.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (existing oscillators):
- AdditiveOscillator: IFFT-based additive synthesis (spec 025)
- ChaosOscillator: Chaos attractor audio-rate oscillation (spec 026)
- FormantOscillator: FOF formant synthesis (spec 027)
- PhaseDistortionOscillator: CZ-style PD synthesis (spec 024)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Particle struct | LOW | No other oscillator uses per-voice sine+drift | Keep local (private/detail) |
| SpawnMode enum | MEDIUM | Could be used by future swarm/cloud processors | Keep local, extract after 2nd use |
| sanitizeOutput pattern | HIGH | Already duplicated in AdditiveOscillator | Keep local (3-line function, extraction overhead > benefit) |
| 1/sqrt(N) normalization | MEDIUM | Any multi-voice summing component | Keep local (one-liner math) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared oscillator base class | Each oscillator has fundamentally different processing (IFFT vs chaos integration vs FOF vs particle pool) |
| Keep SpawnMode local | First and only consumer; premature to extract |
| Inline drift filter (not OnePoleSmoother) | OnePoleSmoother has unnecessary overhead for 64 instances (NaN checks, completion detection, configurable time) |
| No GrainScheduler refactoring | Different timing model; would add complexity without benefit to existing consumer |

### Review Trigger

After implementing the **next multi-voice/swarm oscillator**, review this section:
- [ ] Does it need SpawnMode or similar? -> Extract to shared location
- [ ] Does it use 1/sqrt(N) normalization? -> Consider shared utility
- [ ] Does it need per-voice drift? -> Consider extracting DriftGenerator primitive

## Project Structure

### Documentation (this feature)

```text
specs/028-particle-oscillator/
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Entity definitions and relationships
├── quickstart.md        # Build/test/implementation quick reference
├── contracts/           # API contract definition
│   └── particle_oscillator_api.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── particle_oscillator.h    # NEW: Main header (Layer 2)
└── tests/
    └── unit/
        └── processors/
            └── particle_oscillator_test.cpp  # NEW: Tests
```

**Structure Decision**: Single header-only implementation file at Layer 2 (processors/), following the exact pattern of ChaosOscillator, FormantOscillator, and AdditiveOscillator. Test file in the standard location under `dsp/tests/unit/processors/`. Both files must be registered in `dsp/tests/CMakeLists.txt`.

## Complexity Tracking

No constitution violations. All gates passed.
