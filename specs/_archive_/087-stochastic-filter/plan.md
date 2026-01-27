# Implementation Plan: Stochastic Filter

**Branch**: `087-stochastic-filter` | **Date**: 2026-01-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/087-stochastic-filter/spec.md`

## Summary

A Layer 2 DSP processor that implements a filter with stochastically varying parameters. The processor composes an SVF filter with multiple random modulation sources (Walk/Brownian, Jump, Lorenz chaotic attractor, Perlin noise) to create evolving, experimental filter effects. Randomizable parameters include cutoff frequency (octave range), resonance (Q) (range), and filter type (with parallel crossfade transitions). Control-rate updates (every 32-64 samples) balance CPU efficiency with temporal resolution.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- `primitives/svf.h` - TPT State Variable Filter (modulation-stable)
- `core/random.h` - Xorshift32 PRNG (real-time safe)
- `primitives/smoother.h` - OnePoleSmoother (parameter transitions)
**Storage**: N/A (no persistence)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library (Layer 2 processor)
**Performance Goals**: < 0.5% CPU per instance at 44.1kHz stereo (SC-006)
**Constraints**: Zero allocations in audio path, noexcept processing, linked stereo modulation
**Scale/Scope**: Single processor class, 4 random modes, 8 filter types

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] No allocations in process methods (pre-allocate in prepare())
- [x] No locks/mutexes in audio thread (lock-free parameter updates)
- [x] No exceptions in audio thread (noexcept on all processing)
- [x] No I/O in audio thread (Xorshift32 is deterministic, no system calls)

**Principle IX - Layered DSP Architecture:**
- [x] Layer 2 processor depends only on Layers 0-1
- [x] Uses existing SVF (Layer 1), Xorshift32 (Layer 0), OnePoleSmoother (Layer 1)
- [x] No circular dependencies

**Principle X - DSP Processing Constraints:**
- [x] TPT SVF handles modulation stability (no zipper artifacts)
- [x] Filter type transitions use parallel crossfade (FR-008)
- [x] Control-rate updates with smoothing between (FR-022)

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: StochasticFilter, RandomMode (enum), StochasticParams (internal struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| StochasticFilter | `grep -r "class StochasticFilter" dsp/ plugins/` | No | Create New |
| RandomMode | `grep -r "enum.*RandomMode" dsp/ plugins/` | No | Create New |
| LorenzAttractor | `grep -r "Lorenz\|lorenz" dsp/ plugins/` | No | Create New (internal) |
| PerlinNoise | `grep -r "Perlin\|perlin" dsp/ plugins/` | No | Create New (internal) |

**Utility Functions to be created**: None (all math utilities exist in Layer 0)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | Core filter - direct composition for modulation stability |
| SVFMode | dsp/include/krate/dsp/primitives/svf.h | 1 | Filter mode enumeration - reuse directly |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | PRNG for all random modes (Walk, Jump) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing for cutoff, Q, type crossfade |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Not needed (filter uses linear parameters) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no conflicts)
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (StochasticFilter, RandomMode, LorenzAttractor, PerlinNoise) are unique and not found in codebase. The Lorenz and Perlin implementations will be internal to the header (not exposed) or as nested classes/structs.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| SVFMode | enum values | `Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf` | Yes |
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | next | `[[nodiscard]] constexpr uint32_t next() noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` (returns [-1, 1]) | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` (returns [0, 1]) | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class, SVFMode enum
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SVF | Must call prepare() before process() | Check `isPrepared()` or document requirement |
| SVF | Q uses 1/Q internally (k_ = 1/Q) | Pass Q directly, SVF handles conversion |
| SVF | Mode sets mix coefficients, not filter type | setMode() changes output mixing |
| Xorshift32 | nextFloat() returns [-1, 1], not [0, 1] | Use nextUnipolar() for [0, 1] |
| Xorshift32 | seed(0) uses default seed | Avoid 0 or document behavior |
| OnePoleSmoother | snapTo() sets both current AND target | Use setTarget() for smooth transitions |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| LorenzAttractor | Could be reused for chaotic modulation | core/chaos.h | StochasticFilter, future chaotic LFO |
| PerlinNoise1D | Could be reused for coherent noise modulation | core/noise.h | StochasticFilter, future generative systems |

**Decision**: Keep Lorenz and Perlin as internal implementations for now. Extract to Layer 0 if a second consumer emerges (e.g., chaotic LFO, generative delay time).

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| updateModulation() | Per-processor control rate logic |
| calculateWalkStep() | Internal to Walk mode |
| calculateJumpValue() | Internal to Jump mode |

**Decision**: All random mode calculations stay internal. Lorenz/Perlin could be extracted later if reuse emerges.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- Stochastic delay time modulation (future)
- Stochastic panning processor (future)
- Generative LFO modes (future, may be Layer 1)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| LorenzAttractor | HIGH | Chaotic LFO, stochastic delay | Keep internal, extract after 2nd use |
| PerlinNoise1D | HIGH | Generative modulation | Keep internal, extract after 2nd use |
| RandomMode enum | MEDIUM | Other stochastic processors | Keep in this header, export if needed |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep Lorenz/Perlin internal | First feature using them - patterns not established |
| Linked stereo modulation | Preserves stereo image, halves CPU (FR-018) |
| Control-rate updates | 32-64 samples balances CPU vs. temporal resolution (FR-022) |

### Review Trigger

After implementing **stochastic delay time modulation**, review this section:
- [ ] Does stochastic delay need Lorenz/Perlin? -> Extract to Layer 0
- [ ] Does stochastic delay use same control-rate pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/087-stochastic-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output - Lorenz/Perlin algorithms
├── data-model.md        # Phase 1 output - class structure
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contracts
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── stochastic_filter.h    # Main implementation (Layer 2)
└── tests/
    └── unit/processors/
        └── stochastic_filter_test.cpp  # Unit tests

# No plugin integration in this spec (DSP-only)
```

**Structure Decision**: Single header in Layer 2 processors. Internal Lorenz/Perlin implementations as private nested classes or inline helpers.

## Complexity Tracking

No constitution violations. All design decisions align with established patterns.

| Aspect | Choice | Justification |
|--------|--------|---------------|
| Random modes | 4 (Walk, Jump, Lorenz, Perlin) | Spec requirement (FR-001) |
| Filter type crossfade | Parallel processing | Prevents clicks (FR-008) |
| Update rate | Control-rate (32-64 samples) | CPU efficiency (FR-022) |
| Stereo | Linked modulation | Preserves image, halves CPU (FR-018) |

---

## Post-Design Constitution Re-Check

*Completed after Phase 1 design artifacts generated.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] API design uses noexcept on all processing methods
- [x] No std::vector or dynamic allocation in process path
- [x] Xorshift32 PRNG is lock-free and deterministic
- [x] Filter type transitions use pre-instantiated SVF pairs

**Principle IX - Layered DSP Architecture:**
- [x] StochasticFilter (Layer 2) depends only on SVF, Xorshift32, OnePoleSmoother (Layers 0-1)
- [x] No Layer 3/4 dependencies introduced
- [x] Internal Lorenz/Perlin helpers do not create Layer 0 dependencies

**Principle X - DSP Processing Constraints:**
- [x] TPT SVF handles audio-rate modulation stability
- [x] Parallel crossfade for type transitions documented in research.md
- [x] Control-rate update strategy documented (32 samples)

**Principle XIV - ODR Prevention:**
- [x] All new types unique in codebase
- [x] Dependency API signatures verified from source headers
- [x] No conflicts with existing components

**Status**: All constitution principles satisfied. Ready for Phase 2 (tasks.md generation via /speckit.tasks).

---

## Generated Artifacts

| Artifact | Path | Description |
|----------|------|-------------|
| plan.md | `specs/087-stochastic-filter/plan.md` | This implementation plan |
| research.md | `specs/087-stochastic-filter/research.md` | Algorithm research (Lorenz, Perlin, etc.) |
| data-model.md | `specs/087-stochastic-filter/data-model.md` | Class structure and state |
| quickstart.md | `specs/087-stochastic-filter/quickstart.md` | Usage examples |
| API contract | `specs/087-stochastic-filter/contracts/stochastic_filter_api.h` | Public API specification |
