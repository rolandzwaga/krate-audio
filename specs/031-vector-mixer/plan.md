# Implementation Plan: Vector Mixer

**Branch**: `031-vector-mixer` | **Date**: 2026-02-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/031-vector-mixer/spec.md`

## Summary

Implement a Layer 3 system component (`VectorMixer`) that performs XY vector mixing between 4 audio sources with selectable topologies (square bilinear and diamond/Prophet VS), three mixing laws (linear, equal-power, square-root), per-axis one-pole exponential smoothing, and both mono and stereo processing. The component is a lightweight, stateless (aside from smoothing) utility that computes weights from an XY position and applies them as a weighted sum of four input signals. Thread-safe atomic setters for modulation parameters (X, Y, smoothing time) allow real-time automation from any thread while processBlock() runs on the audio thread.

## Technical Context

**Language/Version**: C++20 (header-only in `Krate::DSP` namespace)
**Primary Dependencies**: Layer 0 (`math_constants.h` for `kTwoPi`, `db_utils.h` for `detail::constexprExp`, `detail::isNaN`, `detail::isInf`), `<atomic>`, `<algorithm>`, `<cmath>`, `<cstddef>`, `<cassert>`
**Storage**: N/A (no persistent state, no buffers)
**Testing**: Catch2 via `dsp_tests` target *(Constitution Principle VIII: Testing Discipline)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library component (header-only, `dsp/include/krate/dsp/systems/`)
**Performance Goals**: <0.05% CPU for 512 samples mono at 44.1 kHz (SC-003); <0.8% CPU for 8192 samples (SC-008)
**Constraints**: Zero allocation in audio path, noexcept everywhere, no locks, no I/O, no exceptions
**Scale/Scope**: Single header file (~400-500 lines), single test file (~800-1000 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A -- this is a pure DSP component, not a plugin component.

**Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process path -- only arithmetic on stack variables
- [x] No locks -- `std::atomic<float>` with `memory_order_relaxed` for XY position
- [x] No exceptions -- all methods noexcept
- [x] No I/O -- pure computation
- [x] Pre-allocate: Nothing to pre-allocate (no buffers, no delay lines)

**Principle III (Modern C++ Standards):**
- [x] C++20 features: `std::atomic<float>`, `[[nodiscard]]`, designated initializers
- [x] No raw new/delete -- no heap allocation at all
- [x] constexpr where applicable (enums, weight computation functions)

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see section below) -- verdict: NOT BENEFICIAL
- [x] Scalar-first workflow applies (only scalar needed)

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code
- [x] `std::atomic<float>::is_lock_free()` -- verified on all target platforms for float
- [x] `std::sqrt` used (not `sqrtf`) -- consistent across compilers

**Principle VIII (Testing Discipline):**
- [x] Tests will be written BEFORE implementation code
- [x] Unit tests with known input/output pairs for all topologies and mixing laws

**Principle IX (Layered Architecture):**
- [x] Layer 3 component -- depends only on Layer 0 (math_constants.h, db_utils.h)
- [x] No Layer 1/2 dependencies needed (smoothing inlined, not composed)

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with actual evidence, not assumptions

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: VectorMixer, Topology (enum), MixingLaw (enum), Weights (struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| VectorMixer | `grep -r "class VectorMixer" dsp/ plugins/` | No | Create New |
| VectorMixer | `grep -r "VectorMixer" dsp/ plugins/` | No | Create New |
| Topology (enum) | `grep -r "enum.*Topology" dsp/ plugins/` | No (AlgorithmTopology is a struct, not a conflicting enum) | Create New |
| MixingLaw (enum) | `grep -r "enum.*MixingLaw" dsp/ plugins/` | No | Create New |
| Weights (struct) | `grep -r "struct Weights" dsp/ plugins/` | No | Create New |
| StereoOutput (struct) | `grep -r "struct StereoOutput" dsp/ plugins/` | **Yes** -- `dsp/include/krate/dsp/systems/unison_engine.h:47` | **Reuse existing** |

**Critical finding:** `StereoOutput` already exists in `Krate::DSP` namespace in `unison_engine.h`. Defining another `StereoOutput` in the same namespace would be an ODR violation. The VectorMixer must either:
1. Include `unison_engine.h` (bad -- pulls in heavy oscillator dependencies)
2. Forward-declare and use the existing struct (impossible for aggregate types)
3. **Extract `StereoOutput` to a shared location** (best approach)

**Decision:** Extract `StereoOutput` to `dsp/include/krate/dsp/core/stereo_output.h` (Layer 0) since it is a simple POD struct with no dependencies. Update `unison_engine.h` to include from the new location. This also benefits any future stereo-output components. Alternatively, the VectorMixer can define its own return type with a different name (e.g., `VectorStereoOutput`), but extracting is cleaner and prevents future ODR issues if other systems also define `StereoOutput`.

**Recommended approach:** Extract `StereoOutput` to Layer 0. This is a one-line struct with zero dependencies -- it belongs in core. The change to `unison_engine.h` is backward-compatible (consumers already use `Krate::DSP::StereoOutput`).

**Utility Functions to be created**: None that are standalone -- all weight computation is internal to VectorMixer.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none planned as free functions) | -- | -- | -- | -- |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `kTwoPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Smoothing coefficient formula: `exp(-kTwoPi / (timeMs * 0.001f * sampleRate))` |
| `detail::constexprExp` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Computing one-pole smoothing coefficient at prepare-time |
| `detail::isNaN` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Debug assertions for invalid input detection (FR-025) |
| `detail::isInf` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Debug assertions for invalid input detection (FR-025) |
| `StereoOutput` | `dsp/include/krate/dsp/systems/unison_engine.h` (to be extracted to `core/stereo_output.h`) | 0 (after extraction) | Return type for stereo process() methods (FR-015) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no conflicts
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother reference pattern
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 siblings -- no VectorMixer or Mixer class exists
- [x] `specs/_architecture_/layer-3-systems.md` - Component inventory checked
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - Reference for equal-power math (2-source only, VectorMixer needs 4-source)

### ODR Risk Assessment

**Risk Level**: Medium

**Justification**: The `StereoOutput` struct already exists in `unison_engine.h` within `Krate::DSP`. If we define another `StereoOutput` in `vector_mixer.h`, any translation unit including both headers would have an ODR violation. Mitigation: extract `StereoOutput` to `core/stereo_output.h` as a prerequisite task. After extraction, risk drops to Low (all planned types are unique).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `math_constants.h` | `kTwoPi` | `inline constexpr float kTwoPi = 2.0f * kPi;` | Yes |
| `db_utils.h` | `detail::constexprExp` | `constexpr float constexprExp(float x) noexcept` | Yes |
| `db_utils.h` | `detail::isNaN` | `constexpr bool isNaN(float x) noexcept` | Yes |
| `db_utils.h` | `detail::isInf` | `constexpr bool isInf(float x) noexcept` | Yes |
| `StereoOutput` | `left` | `float left = 0.0f;` | Yes |
| `StereoOutput` | `right` | `float right = 0.0f;` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi constants
- [x] `dsp/include/krate/dsp/core/db_utils.h` - constexprExp, isNaN, isInf, flushDenormal
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains reference
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother pattern reference
- [x] `dsp/include/krate/dsp/systems/unison_engine.h` - StereoOutput struct, API patterns
- [x] `dsp/include/krate/dsp/systems/fm_voice.h` - FMVoice API patterns (prepare/reset/process)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `OnePoleSmoother` | Has completion threshold `kCompletionThreshold = 0.0001f` and snaps to target when within threshold | VectorMixer smoothing will NOT use this threshold -- pure one-pole without snap, matching FR-019 formula exactly |
| `OnePoleSmoother` | Uses `configure(smoothTimeMs, sampleRate)` not `prepare()` | VectorMixer will compute coefficient inline using `exp(-kTwoPi / (timeMs * 0.001f * sampleRate))` per FR-019 |
| `OnePoleSmoother` | Formula uses `exp(-5000.0f / (time * sr))` (5-tau convention) | VectorMixer uses `exp(-kTwoPi / (time * 0.001 * sr))` per FR-019 -- DIFFERENT formula, do not copy from OnePoleSmoother |
| `StereoOutput` | Defined in `unison_engine.h` inside `Krate::DSP` namespace | Must extract to `core/stereo_output.h` before VectorMixer can use it |
| `std::atomic<float>` | `memory_order_relaxed` is sufficient for single-writer position updates | Do NOT use `memory_order_seq_cst` -- unnecessary overhead |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `StereoOutput` struct | Already exists in Layer 3 (unison_engine.h), needed by VectorMixer too; belongs in Layer 0 as a POD type | `core/stereo_output.h` | UnisonEngine, VectorMixer, future stereo systems |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `computeSquareWeights()` | Internal to VectorMixer topology logic, one consumer |
| `computeDiamondWeights()` | Internal to VectorMixer topology logic, one consumer |
| `applyMixingLaw()` | Internal to VectorMixer, mixing law logic tightly coupled to topology weights |
| Smoothing coefficient calculation | One-liner in prepare(), class stores sampleRate_ |

**Decision**: Extract only `StereoOutput` to Layer 0. All weight computation and mixing law logic stays inside `VectorMixer` as private methods -- there is only one consumer and the logic is tightly coupled to the class state.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability for all DSP features.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | VectorMixer is stateless aside from one-pole smoothers for X/Y. No sample-to-sample feedback in the mixing path. |
| **Data parallelism width** | 4 (sources) | Four weights multiplied by four inputs -- exactly matches SSE 128-bit (4 floats). However, the operation is trivially cheap. |
| **Branch density in inner loop** | LOW | One branch: mixing law selection (topology is constant within a block). Could be eliminated with function pointers. |
| **Dominant operations** | Arithmetic (multiply-add) | Per sample: 2 smoothing updates (2 muls + 2 adds each), 4-6 multiplies for weights, 4 multiply-adds for mixing = ~20 FLOPs total. |
| **Current CPU budget vs expected usage** | <1% budget vs ~0.01% expected | The algorithm is ~20 FLOPs per sample. At 44.1 kHz that is ~882K FLOPs/sec. A modern CPU does ~10 GFLOPS single-threaded. Usage is 0.009% -- well under the 1% Layer 3 budget. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The VectorMixer performs approximately 20 floating-point operations per sample (2 smoother updates + weight computation + 4 multiply-adds). This is far below any measurable CPU budget (<0.01% CPU). The SIMD setup overhead (loading 4 values into a register, computing weights, storing back) would likely be comparable to the scalar cost for such a trivial workload. Additionally, the smoothing state update is sequential (depends on previous sample), limiting parallelization across samples.

### Implementation Workflow

**Verdict is NOT BENEFICIAL:** Skip SIMD Phase 2. Scalar implementation only.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Pre-compute `smoothCoeff_` once in `prepare()` / `setSmoothingTimeMs()` | Avoids repeated exp() per parameter change | LOW | YES (already planned) |
| Skip smoothing when `smoothTimeMs_ == 0` | Eliminates 2 multiply-adds per sample when unused | LOW | YES |
| Branch-free mixing law via function pointer or switch-outside-loop | Avoids per-sample branch on mixing law enum | LOW | DEFER (branch predictor handles well for constant law) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from architecture docs and spec):
- UnisonEngine: Could use VectorMixer for blending multiple unison configurations
- FMVoice: Could use VectorMixer for blending FM algorithm outputs
- Future "vector envelope" or "vector sequencer" systems would drive VectorMixer positions
- Future wavetable oscillator could use VectorMixer for 4-corner wavetable morphing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `VectorMixer` class | HIGH | Synth voice (wavetable morphing), vector envelope system, any 4-source crossfade | Keep as self-contained Layer 3 component |
| `Weights` struct | MEDIUM | Any component needing quad-weight distribution | Keep in `vector_mixer.h` -- extract if 2nd consumer appears |
| `Topology` / `MixingLaw` enums | LOW | Only VectorMixer-specific | Keep in `vector_mixer.h` |
| `StereoOutput` struct | HIGH | UnisonEngine, VectorMixer, future stereo systems | Extract to `core/stereo_output.h` (prerequisite task) |

### Detailed Analysis (for HIGH potential items)

**VectorMixer** provides:
- XY-to-4-weight computation with selectable topology (square/diamond)
- Selectable mixing law (linear, equal-power, square-root)
- Per-axis exponential smoothing
- Mono and stereo weighted-sum processing

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| UnisonEngine | MAYBE | Could drive blend parameter via VectorMixer, but UnisonEngine already has its own blend mechanism |
| FMVoice | MAYBE | Could blend 4 FM algorithm outputs, but FMVoice has discrete algorithm switching |
| Future wavetable synth | YES | 4-corner wavetable morphing is exactly XY vector mixing |

**Recommendation**: Keep VectorMixer as a self-contained component. It is already designed as a reusable utility. No extraction needed -- consumers will compose it directly.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Inline smoothing instead of composing OnePoleSmoother | FR-019 specifies a different formula than OnePoleSmoother uses; also avoids Layer 1 dependency (though Layer 3 can depend on Layer 1, inlining is simpler for 2 smoothers) |
| Extract StereoOutput to Layer 0 | Prevents ODR violation with UnisonEngine; simple POD type belongs in core |
| Use `std::atomic<float>` directly, not through a wrapper | Only 3 atomic members (X, Y, smoothingTimeMs); wrapper would be over-engineering |
| Keep Weights/Topology/MixingLaw inside vector_mixer.h | Single consumer, no demonstrated need for separate files |

### Review Trigger

After implementing the next synth-related feature, review this section:
- [ ] Does the synth voice need VectorMixer for wavetable morphing? If so, verify API compatibility
- [ ] Does any future component define its own `Weights` struct? If so, consider extracting to shared location
- [ ] Any duplicated 4-source crossfade logic? Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/031-vector-mixer/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- vector_mixer_api.h   # C++ header contract
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- stereo_output.h          # NEW: Extracted StereoOutput struct (Layer 0)
|   +-- systems/
|       +-- vector_mixer.h           # NEW: VectorMixer class (Layer 3)
+-- tests/
    +-- unit/systems/
        +-- vector_mixer_tests.cpp   # NEW: Unit tests
```

**Structure Decision**: Standard monorepo layout. Single header-only implementation in `systems/`, single test file in `tests/unit/systems/`. One prerequisite extraction of `StereoOutput` to `core/stereo_output.h`.

## Complexity Tracking

No constitution violations. All design decisions comply with all principles.
