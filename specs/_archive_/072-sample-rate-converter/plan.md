# Implementation Plan: Sample Rate Converter

**Branch**: `072-sample-rate-converter` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/072-sample-rate-converter/spec.md`

## Summary

Layer 1 DSP primitive for variable-rate playback from linear buffers with high-quality interpolation. The `SampleRateConverter` fills a gap in the codebase by providing fractional position tracking, multiple interpolation modes (Linear, Cubic Hermite, Lagrange), and end-of-buffer detection for one-shot playback scenarios such as freeze mode slice playback and pitch-shifted buffer manipulation.

**Key Technical Decisions:**
- Reuse existing `Interpolation::*` functions from `core/interpolation.h` (no new interpolation code needed)
- Edge reflection for 4-point interpolation at boundaries
- Rate clamping to [0.25, 4.0] range (2 octaves up/down)
- Completion boundary at position >= (bufferSize - 1)
- Block processing with constant rate (captured at block start)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: `core/interpolation.h` (Layer 0), standard library only
**Storage**: N/A (stateless buffer playback)
**Testing**: Catch2 (Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo - DSP library component
**Performance Goals**: Layer 1 primitive < 0.1% CPU per instance
**Constraints**: Real-time safe (noexcept, no allocations in process())
**Scale/Scope**: Single header-only class, ~200 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] No allocations in processing methods
- [x] All processing methods are noexcept
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions, I/O, or system calls

**Principle III - Modern C++ Standards:**
- [x] C++20 features (constexpr, [[nodiscard]])
- [x] RAII not applicable (header-only, no resources)
- [x] No raw new/delete

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 primitive depends only on Layer 0
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Completed. No conflicts found.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `SampleRateConverter`, `InterpolationType` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SampleRateConverter | `grep -r "SampleRateConverter" dsp/ plugins/` | No | Create New |
| InterpolationType | `grep -r "enum.*InterpolationType" dsp/ plugins/` | No (different context in DelayLine) | Create New (scoped to this class) |

**Utility Functions to be created**: None - all interpolation functions already exist

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| linearInterpolate | `grep -r "linearInterpolate" dsp/` | Yes | core/interpolation.h | REUSE |
| cubicHermiteInterpolate | `grep -r "cubicHermiteInterpolate" dsp/` | Yes | core/interpolation.h | REUSE |
| lagrangeInterpolate | `grep -r "lagrangeInterpolate" dsp/` | Yes | core/interpolation.h | REUSE |
| semitonesToRatio | `grep -r "semitonesToRatio" dsp/` | Yes | core/pitch_utils.h | MAY REUSE (optional API) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| linearInterpolate | dsp/include/krate/dsp/core/interpolation.h | 0 | Linear interpolation mode |
| cubicHermiteInterpolate | dsp/include/krate/dsp/core/interpolation.h | 0 | Cubic interpolation mode |
| lagrangeInterpolate | dsp/include/krate/dsp/core/interpolation.h | 0 | Lagrange interpolation mode |
| semitonesToRatio | dsp/include/krate/dsp/core/pitch_utils.h | 0 | Optional semitone-based rate API |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `specs/_architecture_/` - Component inventory
- [x] `dsp/include/krate/dsp/core/interpolation.h` - Verified all three interpolation functions exist

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `SampleRateConverter` is a unique class name not found in codebase. The `InterpolationType` enum will be scoped within the header file. All interpolation functions already exist and will be reused, not duplicated.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Complete. All signatures verified from source.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Interpolation | linearInterpolate | `[[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept` | Yes |
| Interpolation | cubicHermiteInterpolate | `[[nodiscard]] constexpr float cubicHermiteInterpolate(float ym1, float y0, float y1, float y2, float t) noexcept` | Yes |
| Interpolation | lagrangeInterpolate | `[[nodiscard]] constexpr float lagrangeInterpolate(float ym1, float y0, float y1, float y2, float t) noexcept` | Yes |
| pitch_utils | semitonesToRatio | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/interpolation.h` - All three interpolation functions
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| cubicHermiteInterpolate | Parameter order is ym1, y0, y1, y2, t (y0 is center) | Pass 4 samples centered at integer position |
| lagrangeInterpolate | Same parameter order as Hermite | Consistent with Hermite API |
| Interpolation t parameter | t must be in [0, 1] for interpolation | Position fractional part only |

## Layer 0 Candidate Analysis

*This is a Layer 1 feature. Evaluated potential Layer 0 extraction candidates per Constitution Principle IX.*

### Utilities Evaluated for Layer 0 Extraction

| Candidate Function | Extract? | Rationale |
|--------------------|----------|-----------|
| `getSampleClamped(buffer, size, idx)` | NO | Only used by SampleRateConverter. If future components need edge clamping, can extract then. Inline helper is simpler. |

### Utilities to Keep as Member/Inline Functions

| Function | Why Keep? |
|----------|-----------|
| Edge clamping helper | Internal implementation detail, only used by this class. If reuse emerges (e.g., in future resampling components), revisit for Layer 0 extraction to `Interpolation::getClampedSample()`. |
| Position bounds check | Simple inline check, class-specific |

**Decision**: All new code stays in SampleRateConverter. Interpolation functions already exist in Layer 0. Edge clamping is a private implementation detail with no current reuse need.

**Review Trigger**: If a second component needs edge clamping, extract to `core/interpolation.h` as `Interpolation::getClampedSample(const float* buffer, size_t size, int index)`.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from architecture docs):
- DelayLine: Circular buffer delay (distinct purpose)
- RollingCaptureBuffer: Circular capture for freeze (complementary - this plays extracted slices)
- SampleRateReducer: Lo-fi sample-and-hold (distinct purpose)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SampleRateConverter | HIGH | Pattern freeze playback, granular processors, time-stretch effects | Keep as standalone primitive |

### Detailed Analysis (for HIGH potential items)

**SampleRateConverter** provides:
- Variable-rate linear buffer playback
- High-quality interpolation (3 modes)
- End-of-buffer detection
- Rate clamping with musical limits

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Pattern Freeze | YES | Play extracted slices at different pitches |
| Granular Engine | YES | Grain playback at variable pitch |
| Future time-stretch | YES | Building block for WSOLA/phase vocoder |

**Recommendation**: Keep as standalone Layer 1 primitive. Already designed for composition.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Header-only implementation | Consistent with Layer 1 primitives (DelayLine, SampleRateReducer) |
| Separate InterpolationType enum | Avoid coupling to DelayLine's enum |
| No semitone API in initial version | Keep scope minimal; users can use pitch_utils.h directly |

### Review Trigger

After implementing **pattern freeze playback**, review this section:
- [ ] Does freeze use SampleRateConverter for slice playback? Verify API is sufficient
- [ ] Any missing features discovered during integration?

## Project Structure

### Documentation (this feature)

```text
specs/072-sample-rate-converter/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── sample_rate_converter_api.h  # API contract
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── interpolation.h          # EXISTING - will be reused
│   └── primitives/
│       └── sample_rate_converter.h  # NEW - this feature
└── tests/
    └── unit/
        └── primitives/
            └── sample_rate_converter_test.cpp  # NEW - this feature
```

**Structure Decision**: Standard Layer 1 primitive pattern - single header-only file in primitives/, corresponding test file in tests/unit/primitives/.

## Complexity Tracking

> No constitution violations. Standard Layer 1 primitive implementation.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |

---

## Phase 0: Research Findings

See [research.md](research.md) for detailed findings.

### Key Research Outcomes

1. **Interpolation Functions**: All three required interpolation functions already exist in `core/interpolation.h`:
   - `linearInterpolate(y0, y1, t)` - 2-point linear
   - `cubicHermiteInterpolate(ym1, y0, y1, y2, t)` - 4-point Catmull-Rom
   - `lagrangeInterpolate(ym1, y0, y1, y2, t)` - 4-point Lagrange polynomial

2. **Edge Reflection Pattern**: For 4-point interpolation at boundaries, use reflection:
   - Position 0.5: samples `[buffer[0], buffer[0], buffer[1], buffer[2]]` (reflect left edge)
   - Position N-1.5: samples `[buffer[N-3], buffer[N-2], buffer[N-1], buffer[N-1]]` (reflect right edge)

3. **THD+N Measurement**: Cubic interpolation provides 20-30dB better THD+N than linear for sine wave test at non-integer rates (verified by existing interpolation tests showing cubic is closer to true sine values).

4. **Rate Clamping**: [0.25, 4.0] corresponds to +/- 2 octaves, matching common pitch shifter ranges.

5. **API Pattern**: Follow `SampleRateReducer` pattern (prepare/reset/process/processBlock).

---

## Phase 1: Design Outputs

### Data Model

See [data-model.md](data-model.md) for complete entity definitions.

**Key Entities:**
- `InterpolationType` enum: Linear, Cubic, Lagrange
- `SampleRateConverter` class with:
  - Configuration: rate_, interpolationType_, sampleRate_
  - State: position_, isComplete_
  - Constants: kMinRate, kMaxRate, kDefaultRate

### API Contracts

See [contracts/](contracts/) for OpenAPI/header specifications.

**Core API:**
```cpp
class SampleRateConverter {
public:
    static constexpr float kMinRate = 0.25f;
    static constexpr float kMaxRate = 4.0f;
    static constexpr float kDefaultRate = 1.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setRate(float rate) noexcept;
    void setInterpolation(InterpolationType type) noexcept;
    void setPosition(float samples) noexcept;
    [[nodiscard]] float getPosition() const noexcept;
    [[nodiscard]] float process(const float* buffer, size_t bufferSize) noexcept;
    void processBlock(const float* src, size_t srcSize, float* dst, size_t dstSize) noexcept;
    [[nodiscard]] bool isComplete() const noexcept;
};
```

### Quickstart Guide

See [quickstart.md](quickstart.md) for implementation sequence.

**Implementation Order:**
1. Write foundational tests (constants, rate clamping)
2. Implement rate 1.0 passthrough
3. Add linear interpolation
4. Add cubic/Lagrange interpolation with edge reflection
5. Add end-of-buffer detection
6. Add block processing
7. Add THD+N verification test

---

## Constitution Re-Check (Post-Design)

**Principle II - Real-Time Safety:**
- [x] All methods marked noexcept
- [x] No allocations in process/processBlock
- [x] No locks or blocking

**Principle IX - Layer Architecture:**
- [x] Only depends on Layer 0 (interpolation.h)
- [x] No circular dependencies

**Principle XII - Test-First:**
- [x] Test structure defined before implementation
- [x] Commit points after each task group

**Status**: All gates pass. Ready for Phase 2 task generation.
