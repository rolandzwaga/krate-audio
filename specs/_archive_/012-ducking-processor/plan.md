# Implementation Plan: Ducking Processor

**Branch**: `012-ducking-processor` | **Date**: 2025-12-23 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/012-ducking-processor/spec.md`

## Summary

Implement a Layer 2 DSP processor that performs sidechain-triggered gain reduction. The DuckingProcessor attenuates a main audio signal based on the level of an external sidechain signal, using threshold-triggered depth with configurable attack/release timing, hold time, range limiting, and optional sidechain highpass filtering. Reuses EnvelopeFollower for level detection, OnePoleSmoother for gain smoothing, and Biquad for sidechain filtering.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang 15+, GCC 12+)
**Primary Dependencies**: EnvelopeFollower (Layer 2), OnePoleSmoother (Layer 1), Biquad (Layer 1), db_utils (Layer 0)
**Storage**: N/A (stateless except internal processing state)
**Testing**: Catch2 (per project standard, see specs/TESTING-GUIDE.md)
**Target Platform**: Windows 10/11 (x64), macOS 11+ (x64/ARM64), Linux (x64)
**Project Type**: Single DSP component within VST3 plugin architecture
**Performance Goals**: < 0.5% CPU per instance at 44.1kHz stereo (Layer 2 budget)
**Constraints**: Zero latency (no lookahead), noexcept processing, no allocations in process()
**Scale/Scope**: Single mono processor class with dual-input processing API

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All processing functions will be noexcept
- [x] No memory allocation in process() - all state pre-allocated in prepare()
- [x] No blocking operations - pure computation

**Required Check - Principle III (Modern C++):**
- [x] Using C++20 features (constexpr, [[nodiscard]])
- [x] RAII for all resource management (composed components handle their own state)

**Required Check - Principle IX (Layer Architecture):**
- [x] DuckingProcessor is Layer 2 (DSP Processor)
- [x] Depends only on Layer 0 (db_utils) and Layer 1 (Biquad, OnePoleSmoother)
- [x] EnvelopeFollower is peer Layer 2 - cross-Layer-2 composition is allowed

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XV (Honest Completion):**
- [x] All FRs and SCs from spec are testable
- [x] No placeholder implementations planned
- [x] Success criteria thresholds will not be relaxed

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: DuckingProcessor

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DuckingProcessor | `grep -r "class DuckingProcessor\|struct Ducker" src/` | No | Create New |

**Utility Functions to be created**: None (all utilities exist in db_utils.h)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" src/` | Yes | dsp/core/db_utils.h | Reuse |
| gainToDb | `grep -r "gainToDb" src/` | Yes | dsp/core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| dbToGain | dsp/core/db_utils.h | 0 | Convert depth/range/threshold dB to linear |
| gainToDb | dsp/core/db_utils.h | 0 | Convert envelope level to dB for comparison |
| detail::isNaN | dsp/core/db_utils.h | 0 | NaN input sanitization |
| detail::isInf | dsp/core/db_utils.h | 0 | Infinity input sanitization |
| detail::flushDenormal | dsp/core/db_utils.h | 0 | Denormal prevention in state variables |
| detail::constexprExp | dsp/core/db_utils.h | 0 | Coefficient calculation |
| Biquad | dsp/primitives/biquad.h | 1 | Sidechain highpass filter |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Gain reduction smoothing for click-free transitions |
| EnvelopeFollower | dsp/processors/envelope_follower.h | 2 | Sidechain level detection |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - Legacy utilities (no Ducking-related code)
- [x] `src/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `src/dsp/processors/dynamics_processor.h` - Reference for gain reduction patterns
- [x] `src/dsp/processors/envelope_follower.h` - Will be composed, not duplicated

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: DuckingProcessor is a new class name with no existing implementations. All planned functionality is either new (hold time state machine) or uses established patterns from DynamicsProcessor. All utility functions already exist in Layer 0 and will be reused directly.

## Project Structure

### Documentation (this feature)

```text
specs/012-ducking-processor/
├── plan.md              # This file
├── research.md          # Phase 0 output - design decisions
├── data-model.md        # Phase 1 output - class API
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - header contract
│   └── ducking_processor.h
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
└── dsp/
    ├── core/
    │   └── db_utils.h         # Layer 0 - dB conversions (reuse)
    ├── primitives/
    │   ├── biquad.h           # Layer 1 - filter (reuse)
    │   └── smoother.h         # Layer 1 - smoothing (reuse)
    └── processors/
        ├── envelope_follower.h    # Layer 2 - level detection (reuse)
        ├── dynamics_processor.h   # Layer 2 - reference pattern
        └── ducking_processor.h    # NEW - DuckingProcessor implementation

tests/
└── unit/
    └── processors/
        └── ducking_processor_test.cpp  # NEW - unit tests
```

**Structure Decision**: Single header implementation in `src/dsp/processors/` following existing Layer 2 processor patterns (EnvelopeFollower, DynamicsProcessor). All implementation inline in header per project convention.

## Complexity Tracking

No constitution violations require justification. The design follows established patterns exactly.

## Algorithm Design

### Hold Time State Machine

The hold time feature requires a simple state machine not present in DynamicsProcessor:

```
States:
  IDLE: Sidechain below threshold, no gain reduction
  DUCKING: Sidechain above threshold, applying gain reduction
  HOLDING: Sidechain dropped below threshold, but still holding before release

Transitions:
  IDLE → DUCKING: When envelope > threshold
  DUCKING → HOLDING: When envelope < threshold, start hold timer
  HOLDING → DUCKING: When envelope > threshold (re-trigger during hold)
  HOLDING → IDLE: When hold timer expires, release can begin
  DUCKING → IDLE: (via HOLDING state, or directly if holdTime = 0)
```

### Gain Reduction Calculation

Unlike DynamicsProcessor's ratio-based compression, ducking uses fixed depth:

```cpp
// DynamicsProcessor (ratio-based):
gainReduction_dB = (inputLevel - threshold) * (1 - 1/ratio)

// DuckingProcessor (depth-based):
if (sidechainLevel > threshold) {
    // Interpolate from 0 to depth based on how far above threshold
    overshoot = sidechainLevel - threshold;
    factor = min(1.0, overshoot / 10.0);  // 10dB for full depth
    targetGainReduction = depth * factor;

    // Clamp to range limit
    actualGainReduction = max(targetGainReduction, range);  // range is negative
}
```

### Processing Flow

```
1. Input: main sample, sidechain sample
2. Apply HPF to sidechain (if enabled)
3. Get envelope from sidechain via EnvelopeFollower
4. Convert envelope to dB
5. Compare to threshold → update state machine
6. Calculate target gain reduction (depth * factor)
7. Apply range limit (clamp)
8. Smooth gain reduction via OnePoleSmoother
9. Apply gain reduction to main signal
10. Store GR for metering
11. Output: processed main sample
```
