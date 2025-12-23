# Implementation Plan: Multimode Filter

**Branch**: `008-multimode-filter` | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/008-multimode-filter/spec.md`

## Summary

Layer 2 DSP Processor implementing a complete multimode filter by composing existing Layer 1 primitives (Biquad, OnePoleSmoother, Oversampler). Provides 8 filter types with selectable slopes (12-48 dB/oct for LP/HP/BP/Notch), coefficient smoothing for click-free modulation, and optional pre-filter drive with oversampled saturation.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Biquad (biquad.h), OnePoleSmoother (smoother.h), Oversampler (oversampler.h)
**Storage**: N/A (stateful DSP processor, no persistence)
**Testing**: Catch2 v3 (per Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single project - VST3 plugin DSP library
**Performance Goals**: < 0.5% CPU per instance (per Constitution Principle XI)
**Constraints**: Real-time safe (noexcept, no allocations in process), max 4 biquad stages
**Scale/Scope**: First Layer 2 processor, foundation for future filter-based effects

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

- [x] **Principle II (Real-Time Safety)**: All process methods will be noexcept with no allocations
- [x] **Principle III (Modern C++)**: C++20, RAII, no raw new/delete
- [x] **Principle IX (Layered Architecture)**: Layer 2 depends only on Layer 0/1
- [x] **Principle X (DSP Constraints)**: Oversampling for nonlinear drive, proper interpolation
- [x] **Principle XI (Performance Budget)**: < 0.5% CPU target for Layer 2 processor

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

### Post-Design Check

- [x] **Principle IX**: MultimodeFilter only uses Layer 0/1 components (verified in data-model.md)
- [x] **Principle XV (Honest Completion)**: Compliance table defined in spec.md for all FR/SC

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MultimodeFilter | `grep -r "MultimodeFilter" src/` | No | Create New |
| FilterSlope | `grep -r "FilterSlope" src/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| slopeToStages | `grep -r "slopeToStages" src/` | No | - | Create New |
| slopeTodBPerOctave | `grep -r "slopeTodBPerOctave" src/` | No | - | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FilterType | dsp/primitives/biquad.h | 1 | Reuse enum directly |
| Biquad | dsp/primitives/biquad.h | 1 | Single filter stage |
| BiquadCoefficients | dsp/primitives/biquad.h | 1 | Coefficient calculation |
| SmoothedBiquad | dsp/primitives/biquad.h | 1 | Future: per-coefficient smoothing (not used in initial implementation - OnePoleSmoother used for parameter targets instead) |
| butterworthQ() | dsp/primitives/biquad.h | 1 | Cascade Q calculation |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing |
| Oversampler<2,1> | dsp/primitives/oversampler.h | 1 | Drive saturation |
| dbToGain() | dsp/core/db_utils.h | 0 | Drive parameter conversion |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No MultimodeFilter or FilterSlope
- [x] `src/dsp/core/` - No conflicts
- [x] `src/dsp/primitives/` - Contains components to reuse
- [x] `src/dsp/processors/` - Directory does not exist (will create)
- [x] `ARCHITECTURE.md` - No Layer 2 processors yet

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (MultimodeFilter, FilterSlope) are unique and not found in codebase. Reusing existing Layer 1 components via composition, not duplication.

## Project Structure

### Documentation (this feature)

```text
specs/008-multimode-filter/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0: Research decisions
├── data-model.md        # Phase 1: Component structure
├── quickstart.md        # Phase 1: Usage examples
├── contracts/           # Phase 1: API contract
│   └── multimode_filter.h
└── tasks.md             # Phase 2: Implementation tasks (via /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── dsp/
│   ├── core/                    # Layer 0 (existing)
│   │   └── db_utils.h
│   ├── primitives/              # Layer 1 (existing)
│   │   ├── biquad.h
│   │   ├── smoother.h
│   │   └── oversampler.h
│   └── processors/              # Layer 2 (NEW - create directory)
│       └── multimode_filter.h   # New: MultimodeFilter class

tests/
├── unit/
│   └── processors/              # NEW - create directory
│       └── multimode_filter_test.cpp  # New: Filter tests
└── CMakeLists.txt               # Update: Add new test file
```

**Structure Decision**: Single source file in header-only style (following existing Layer 1 pattern). New `processors/` directory marks first Layer 2 component.

## Complexity Tracking

No constitution violations. All design decisions align with principles:

| Decision | Principle | Justification |
|----------|-----------|---------------|
| 4 pre-allocated biquad stages | II (RT Safety) | Avoids runtime allocation |
| Oversampler for drive only | X (DSP Constraints) | Nonlinear processing requires oversampling |
| Slope ignored for Shelf/Peak | - | Per research.md Decision 5 |

## Generated Artifacts

| Artifact | Status | Path |
|----------|--------|------|
| research.md | Complete | [research.md](research.md) |
| data-model.md | Complete | [data-model.md](data-model.md) |
| contracts/multimode_filter.h | Complete | [contracts/multimode_filter.h](contracts/multimode_filter.h) |
| quickstart.md | Complete | [quickstart.md](quickstart.md) |
| tasks.md | Pending | Run `/speckit.tasks` to generate |

## Next Steps

1. Run `/speckit.tasks` to generate implementation task list
2. Follow Test-First Development per Principle XII:
   - Verify TESTING-GUIDE.md in context
   - Write failing tests before implementation
   - Commit after each task group
3. Update ARCHITECTURE.md after implementation complete
