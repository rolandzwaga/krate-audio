# Implementation Plan: Tape Delay Mode

**Branch**: `024-tape-delay` | **Date**: 2025-12-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/024-tape-delay/spec.md`

## Summary

Implement a Layer 4 tape delay effect that emulates classic tape echo units (Roland RE-201, Echoplex, Watkins Copicat). The TapeDelay class composes existing Layer 3 components: TapManager for multi-head echo patterns, FeedbackNetwork for warm feedback with filtering/saturation, CharacterProcessor for tape character (wow/flutter, hiss, saturation), and ModulationMatrix for LFO routing. Key unique behaviors: motor inertia for smooth delay time changes with pitch artifacts, wow/flutter rate scaling with motor speed, and 3 playback heads at fixed timing ratios.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 3 (TapManager, FeedbackNetwork, CharacterProcessor, ModulationMatrix), Layer 1 (OnePoleSmoother, LFO)
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (via existing test infrastructure)
**Target Platform**: Windows/macOS/Linux VST3 hosts
**Project Type**: Single DSP library project
**Performance Goals**: <5% CPU at 44.1kHz stereo (SC-009), stable CPU under automation
**Constraints**: Real-time safe (noexcept, no allocations in process()), <2000ms max delay
**Scale/Scope**: Single Layer 4 user feature composed from 4+ Layer 3 systems

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All process() methods will be noexcept
- [x] No memory allocation after prepare()
- [x] Parameter changes smooth via OnePoleSmoother

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 4 feature composes only from Layer 0-3 components
- [x] No upward dependencies from lower layers

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Completed before creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: TapeDelay, TapeHead, MotorController

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TapeDelay | `grep -r "class TapeDelay" src/` | No | Create New |
| TapeHead | `grep -r "struct TapeHead" src/` | No | Create New |
| MotorController | `grep -r "class MotorController" src/` | No | Create New |
| WowFlutter | `grep -r "WowFlutter" src/` | No | Not needed (CharacterProcessor has it) |

**Utility Functions to be created**: None planned (all needed utilities exist in Layer 0-3)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| msToSamples | `grep -r "msToSamples" src/` | Yes | DelayEngine, FeedbackNetwork, TapManager | Keep as member |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| TapManager | src/dsp/systems/tap_manager.h | 3 | Multi-head echo pattern (3 heads at 1x, 1.5x, 2x motor speed) |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | 3 | Feedback path with filter + saturation |
| CharacterProcessor | src/dsp/systems/character_processor.h | 3 | Tape character (wow/flutter, hiss, saturation, rolloff) |
| ModulationMatrix | src/dsp/systems/modulation_matrix.h | 3 | LFO routing for wow/flutter modulation |
| OnePoleSmoother | src/dsp/primitives/smoother.h | 1 | Motor inertia smoothing |
| LFO | src/dsp/primitives/lfo.h | 1 | Additional modulation if needed |
| NoiseGenerator | src/dsp/processors/noise_generator.h | 2 | Tape hiss (used via CharacterProcessor) |
| dbToGain | src/dsp/core/db_utils.h | 0 | Level conversions |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No TapeDelay/TapeHead/MotorController
- [x] `src/dsp/core/` - No conflicts
- [x] `src/dsp/systems/` - No existing tape delay class
- [x] `src/dsp/features/` - Directory doesn't exist yet (first Layer 4 feature)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (TapeDelay, TapeHead, MotorController) do not exist in the codebase. This is the first Layer 4 feature, so src/dsp/features/ will be created fresh. Existing Layer 3 components will be composed, not duplicated.

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

*No new Layer 0 utilities identified. All needed utilities exist.*

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples | One-liner, already exists in composed components |
| calculateHeadRatio | Specific to tape head timing, only used here |

**Decision**: No Layer 0 extraction needed. Compose from existing components.

## Project Structure

### Documentation (this feature)

```text
specs/024-tape-delay/
├── spec.md              # Feature specification (created)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── tape_delay.h     # Public interface contract
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
├── dsp/
│   ├── features/           # NEW - Layer 4 User Features
│   │   └── tape_delay.h    # TapeDelay, TapeHead, MotorController
│   ├── systems/            # Layer 3 (existing)
│   │   ├── tap_manager.h
│   │   ├── feedback_network.h
│   │   ├── character_processor.h
│   │   └── modulation_matrix.h
│   ├── processors/         # Layer 2 (existing)
│   ├── primitives/         # Layer 1 (existing)
│   └── core/               # Layer 0 (existing)

tests/
└── unit/
    └── features/           # NEW
        └── tape_delay_test.cpp
```

**Structure Decision**: Create new `src/dsp/features/` directory for Layer 4 user features. TapeDelay is header-only, composing from Layer 3 systems.

## Architecture Overview

### TapeDelay Composition

```
┌─────────────────────────────────────────────────────────────────────┐
│                         TapeDelay (Layer 4)                         │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    MotorController                           │   │
│  │  - Delay time (Motor Speed): 20-2000ms                       │   │
│  │  - Inertia smoothing: 200-500ms transition                   │   │
│  │  - Pitch artifacts during transitions                        │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              ↓                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                      TapManager                              │   │
│  │  - 3 TapeHeads at fixed ratios (1x, 1.5x, 2x)               │   │
│  │  - Per-head enable, level, pan                               │   │
│  │  - Head timings scale with Motor Speed                       │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              ↓                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                   FeedbackNetwork                            │   │
│  │  - Feedback amount (0-100%+)                                 │   │
│  │  - Filter in path (LP for progressive darkening)             │   │
│  │  - Saturation for warmth and limiting                        │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              ↓                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                  CharacterProcessor                          │   │
│  │  - Mode: Tape                                                │   │
│  │  - Wear → wow/flutter depth + hiss level                     │   │
│  │  - Saturation → tape drive                                   │   │
│  │  - Age → EQ rolloff + noise + splice artifacts               │   │
│  │  - Wow rate scales with motor speed                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              ↓                                      │
│                        Output (L/R)                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Signal Flow

1. **Input** → Dry signal captured for mix
2. **MotorController** → Calculates smoothed delay time with inertia
3. **TapManager** → Reads from delay line at 3 head positions (scaled by motor speed)
4. **FeedbackNetwork** → Feedback path with LP filter + saturation applied to delayed signal
5. **CharacterProcessor (Tape mode)** → Adds wow/flutter, saturation, hiss, rolloff
6. **Mix** → Dry/wet blend at output

### User Control Mapping

| User Control | Target Component | Parameter |
|--------------|-----------------|-----------|
| Motor Speed | MotorController | targetDelayMs (smoothed with inertia) |
| Wear | CharacterProcessor | setTapeWowDepth, setTapeFlutterDepth, setTapeHissLevel |
| Saturation | CharacterProcessor | setTapeSaturation |
| Age | CharacterProcessor | setTapeRolloffFreq + hiss boost + splice artifacts |
| Echo Heads | TapManager | setTapEnabled, head timing ratios |
| Feedback | FeedbackNetwork | setFeedbackAmount |
| Mix | TapeDelay | dry/wet mix smoother |

### Motor Inertia Implementation

The MotorController provides realistic tape machine behavior:

```cpp
class MotorController {
    // Target delay set by user
    float targetDelayMs_;

    // Current delay (ramping toward target)
    float currentDelayMs_;

    // Inertia coefficient (200-500ms transition)
    OnePoleSmoother delaySmoother_;  // Long smoothing for inertia

    // Pitch artifact detection
    float lastDelayMs_;
    float pitchRatio_;  // > 1.0 = speeding up, < 1.0 = slowing down
};
```

When Motor Speed changes:
1. `targetDelayMs_` updates immediately
2. `currentDelayMs_` ramps slowly (200-500ms) via long smoothing time
3. Pitch ratio = `lastDelayMs_ / currentDelayMs_` creates tape speed-up/slow-down effect
4. Wow rate scales: `wowRate = baseWowRate * (baseDelayMs / currentDelayMs)`

### Echo Head Ratios (RE-201 Style)

| Head | Ratio | Example at 500ms Motor Speed |
|------|-------|------------------------------|
| 1 | 1.0x | 500ms |
| 2 | 1.5x | 750ms |
| 3 | 2.0x | 1000ms |

All heads scale proportionally when Motor Speed changes.

## Layer 4 Reusability Analysis

*Forward-looking analysis: What code from this feature could be reused by other Layer 4 features?*

This is the **first Layer 4 feature**. Code created here may establish patterns for:
- 025-bbd-delay (BBD/bucket-brigade emulation)
- 026-digital-delay (Clean digital delay)
- 027-ping-pong (Stereo ping-pong)
- 028-multi-tap (User-configurable multi-tap)
- 029-shimmer (Pitch-shifted feedback)
- 030-reverse (Reverse delay)
- 031-granular (Granular delay)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| **MotorController** | HIGH | BBD (clock speed), All modes (smooth delay changes) | Consider extracting to `src/dsp/features/common/` after 2nd use |
| **TapeHead struct** | MEDIUM | Multi-Tap (generalize to DelayHead) | Keep tape-specific for now, generalize if Multi-Tap needs it |
| **Composition pattern** | HIGH | All Layer 4 modes | Document pattern, don't abstract prematurely |
| **Parameter macro mapping** | MEDIUM | All modes with "character" controls | Keep inline, extract if 3+ modes duplicate |

### MotorController Reuse Analysis

The MotorController provides:
- Target + current delay with configurable smoothing
- Pitch ratio calculation during transitions
- Configurable inertia time

**Other modes that could use this:**

| Mode | Inertia Behavior | Reuse MotorController? |
|------|------------------|------------------------|
| BBD | Clock speed changes = pitch sweep | YES - same concept, different time constant |
| Digital | Instant changes (no pitch) | NO - use simple smoother |
| Ping-Pong | Same as source mode | INHERIT from source |
| Shimmer | Pitch artifacts intentional | MAYBE - depends on design |
| Reverse | Buffer position sweep | DIFFERENT - needs reverse-specific logic |

**Recommendation**: Implement MotorController as a standalone class in `tape_delay.h` for now. When BBD mode is specified, evaluate whether to:
1. Extract to `src/dsp/features/common/motor_controller.h`
2. Generalize with template parameters for inertia behavior
3. Keep separate implementations if behaviors diverge

### Composition Pattern

All Layer 4 features will likely compose:
- Some form of delay management (TapManager or DelayEngine)
- FeedbackNetwork for repeats
- CharacterProcessor for coloration (mode-specific)
- Dry/wet mixing

**Pattern to document (not abstract):**

```cpp
class DelayMode {
protected:
    // Common composition
    TapManager taps_;           // or DelayEngine for single-tap
    FeedbackNetwork feedback_;
    CharacterProcessor character_;
    OnePoleSmoother mixSmoother_;

    // Mode-specific
    // ... (MotorController for tape, ClockController for BBD, etc.)
};
```

**Recommendation**: Do NOT create a base class yet. Let patterns emerge from 2-3 implementations, then refactor if beneficial. Premature abstraction is worse than duplication.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared Layer 4 base class | First feature - patterns not yet established |
| MotorController stays in tape_delay.h | Only one consumer so far |
| TapeHead not generalized | Tape-specific ratios (1x, 1.5x, 2x) |
| Document patterns, don't abstract | YAGNI - wait for concrete need |

### Review Trigger

After implementing **025-bbd-delay**, review this section:
- [ ] Does BBD need MotorController or similar? → Extract to common/
- [ ] Does BBD use same composition pattern? → Document shared pattern
- [ ] Any duplicated parameter mapping? → Consider shared utilities

## Complexity Tracking

No constitution violations. All complexity is justified:

| Design Decision | Justification |
|-----------------|---------------|
| Compose 4 Layer 3 systems | Required for full tape delay feature (spec FR-001 to FR-036) |
| MotorController class | Encapsulates inertia logic cleanly (FR-003, FR-004) |
| TapeHead struct | Simple data structure for head configuration |
