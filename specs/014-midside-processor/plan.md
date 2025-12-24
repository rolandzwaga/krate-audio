# Implementation Plan: MidSideProcessor

**Branch**: `014-midside-processor` | **Date**: 2025-12-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/014-midside-processor/spec.md`

## Summary

Implement a Layer 2 DSP Processor for stereo Mid/Side encoding, decoding, and manipulation. Core functionality includes:
- M/S matrix encoding: Mid = (L + R) / 2, Side = (L - R) / 2
- M/S matrix decoding: L = Mid + Side, R = Mid - Side
- Width control (0-200%) via Side channel scaling
- Independent Mid and Side gain controls (-96dB to +24dB)
- Solo modes for monitoring Mid or Side independently
- Click-free parameter transitions using OnePoleSmoother

This is a mathematically simple processor with no filtering requirements - primarily matrix math and gain control.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- Layer 0: `dbToGain()` from `dsp/core/db_utils.h`
- Layer 1: `OnePoleSmoother` from `dsp/primitives/smoother.h`
**Storage**: N/A (stateless except for smoothers)
**Testing**: Catch2 (following existing test patterns)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - all x64
**Project Type**: Single - VST3 plugin DSP processor
**Performance Goals**: < 0.1% CPU per instance at 44.1kHz stereo (extremely lightweight)
**Constraints**: Zero allocations in process(), noexcept
**Scale/Scope**: Single header, ~200 lines of code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Separation | N/A | Pure DSP processor, no VST dependencies |
| II. Real-Time Safety | ✅ PASS | No allocations, noexcept, pre-allocated smoothers |
| III. Modern C++ | ✅ PASS | C++20, constexpr, RAII, value semantics |
| IV. SIMD Optimization | N/A | Too simple for SIMD benefit |
| V. VSTGUI | N/A | No UI components |
| VI. Memory Architecture | ✅ PASS | Pre-allocated state only (5 smoothers) |
| VII. Project Structure | ✅ PASS | Standard Layer 2 processor location |
| VIII. Testing | ✅ PASS | Unit tests required per spec |
| IX. Layered Architecture | ✅ PASS | Layer 2, depends only on Layer 0-1 |
| X. DSP Constraints | ✅ PASS | No nonlinearities, no filtering needed |
| XI. Performance Budget | ✅ PASS | < 0.1% target easily achievable |
| XII. Test-First | ✅ PASS | Tests will be written before implementation |
| XIII. Architecture Docs | ✅ PASS | ARCHITECTURE.md update required |
| XIV. ODR Prevention | ✅ PASS | No existing MidSide/Stereo components found |
| XV. Honest Completion | ✅ PASS | Compliance table required at completion |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: MidSideProcessor

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MidSideProcessor | `grep -r "class MidSideProcessor\|struct MidSideProcessor" src/` | No | Create New |
| StereoProcessor | `grep -r "class.*Stereo\|struct.*Stereo" src/` | No | N/A (not creating) |

**Utility Functions to be created**: None (pure math inline)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| dbToGain() | dsp/core/db_utils.h | 0 | Convert dB gain values to linear |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Smooth midGain, sideGain, width, solo transitions |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No Mid/Side components
- [x] `src/dsp/core/` - No stereo processing utilities
- [x] `src/dsp/primitives/` - No stereo components
- [x] `src/dsp/processors/` - No Mid/Side processor

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing Mid/Side, Stereo, or width-related components exist in the codebase. This is a completely new processor with no naming conflicts.

## Project Structure

### Documentation (this feature)

```text
specs/014-midside-processor/
├── plan.md              # This file
├── spec.md              # Feature specification (complete)
├── research.md          # Not needed (trivial math, no unknowns)
├── data-model.md        # Entity definitions
├── quickstart.md        # Usage examples
├── contracts/           # API contract header
│   └── midside_processor.h
└── tasks.md             # Implementation tasks (generated by /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/
├── core/
│   └── db_utils.h           # Reuse: dbToGain()
├── primitives/
│   └── smoother.h           # Reuse: OnePoleSmoother
└── processors/
    └── midside_processor.h  # NEW: MidSideProcessor

tests/unit/processors/
└── midside_processor_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Standard Layer 2 processor in `src/dsp/processors/` following existing patterns (e.g., `noise_generator.h`, `ducking_processor.h`).

## Complexity Tracking

> No constitution violations requiring justification. All principles satisfied.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| None | N/A | N/A |

## Phase 0: Research

**Status**: SKIPPED - No research needed

This processor requires no research because:
1. Mid/Side math is well-defined (textbook formulas in spec)
2. No filtering, FFT, or complex algorithms
3. All required primitives (OnePoleSmoother, dbToGain) already exist
4. No unknowns or NEEDS CLARIFICATION markers in spec

## Phase 1: Design & Contracts

### Dependencies

```
Layer 0: db_utils.h (dbToGain)
    │
    └─> Layer 1: smoother.h (OnePoleSmoother)
            │
            └─> Layer 2: midside_processor.h (MidSideProcessor)
```

### Processing Flow

```
Input L/R
    │
    ▼
┌─────────────────┐
│  Encode to M/S  │  Mid = (L + R) / 2
│                 │  Side = (L - R) / 2
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Apply Gains    │  Mid *= smoothedMidGain
│                 │  Side *= smoothedSideGain * smoothedWidth
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Solo Logic     │  if soloMid: Side = 0
│                 │  if soloSide: Mid = 0
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Decode to L/R  │  L = Mid + Side
│                 │  R = Mid - Side
└────────┬────────┘
         │
         ▼
Output L/R
```

### State Variables

| Variable | Type | Purpose |
|----------|------|---------|
| sampleRate_ | float | Current sample rate for smoother init |
| midGainDb_ | float | Target mid gain in dB |
| sideGainDb_ | float | Target side gain in dB |
| width_ | float | Target width [0.0 - 2.0] |
| soloMid_ | bool | Solo mid channel flag |
| soloSide_ | bool | Solo side channel flag |
| midGainSmoother_ | OnePoleSmoother | Smoothed mid gain |
| sideGainSmoother_ | OnePoleSmoother | Smoothed side gain |
| widthSmoother_ | OnePoleSmoother | Smoothed width |
| soloMidSmoother_ | OnePoleSmoother | Smooth solo transitions |
| soloSideSmoother_ | OnePoleSmoother | Smooth solo transitions |

### API Surface

```cpp
class MidSideProcessor {
public:
    // Lifecycle
    void prepare(float sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Configuration
    void setWidth(float widthPercent) noexcept;           // [0%, 200%]
    void setMidGain(float gainDb) noexcept;               // [-96dB, +24dB]
    void setSideGain(float gainDb) noexcept;              // [-96dB, +24dB]
    void setSoloMid(bool enabled) noexcept;
    void setSoloSide(bool enabled) noexcept;

    // Processing
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

    // Queries
    [[nodiscard]] float getWidth() const noexcept;
    [[nodiscard]] float getMidGain() const noexcept;
    [[nodiscard]] float getSideGain() const noexcept;
    [[nodiscard]] bool isSoloMidEnabled() const noexcept;
    [[nodiscard]] bool isSoloSideEnabled() const noexcept;
};
```

## Artifacts Generated

| Artifact | Status |
|----------|--------|
| plan.md | ✅ Complete (this file) |
| research.md | ⏭️ Skipped (not needed) |
| data-model.md | ✅ Complete |
| contracts/midside_processor.h | ✅ Complete |
| quickstart.md | ✅ Complete |

## Next Steps

1. Run `/speckit.tasks` to generate implementation tasks
2. Implement following test-first methodology
3. Update ARCHITECTURE.md after completion
4. Fill Implementation Verification section in spec.md
