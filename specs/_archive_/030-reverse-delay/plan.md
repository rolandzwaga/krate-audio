# Implementation Plan: Reverse Delay Mode

**Branch**: `030-reverse-delay` | **Date**: 2025-12-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/030-reverse-delay/spec.md`

## Summary

Implement a reverse delay effect that captures audio chunks and plays them back in reverse, creating backwards echo effects. The effect supports three playback modes (Full Reverse, Alternating, Random), crossfade control for smooth chunk transitions, and feedback with optional filtering for cascading reversed repeats.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 13+, GCC 10+)
**Primary Dependencies**: Iterum DSP library (Layer 0-3 components)
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 (via TESTING-GUIDE.md patterns)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - VST3 plugin
**Project Type**: Single (DSP library extension)
**Performance Goals**: < 1% CPU per instance at 44.1kHz stereo (Layer 4 budget)
**Constraints**: Real-time safe (no allocations in process), max 2000ms chunk size

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle IX (Layered Architecture):**
- [x] ReverseBuffer placed in Layer 1 (primitives) - depends only on Layer 0
- [x] ReverseFeedbackProcessor placed in Layer 2 (processors) - implements IFeedbackProcessor, wraps ReverseBuffer
- [x] ReverseDelay placed in Layer 4 (features) - uses FlexibleFeedbackNetwork with injected processor

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ReverseBuffer | `grep -r "class ReverseBuffer" src/` | No | Create New (Layer 1) |
| ReverseFeedbackProcessor | `grep -r "class ReverseFeedbackProcessor" src/` | No | Create New (Layer 2, implements IFeedbackProcessor) |
| ReverseDelay | `grep -r "class ReverseDelay" src/` | No | Create New (Layer 4) |
| PlaybackMode | `grep -r "enum.*PlaybackMode" src/` | No | Create New (scoped enum) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| equalPowerCrossfade | `grep -r "equalPowerCrossfade" src/` | No | - | Create in ReverseBuffer |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FlexibleFeedbackNetwork | dsp/systems/flexible_feedback_network.h | 3 | Feedback management with processor injection (FR-015) |
| IFeedbackProcessor | dsp/systems/i_feedback_processor.h | 3 | Interface for ReverseFeedbackProcessor |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing for chunk size, crossfade |
| BlockContext | dsp/core/block_context.h | 0 | Tempo sync for chunk sizes |
| dbToGain | dsp/core/db_utils.h | 0 | Output gain conversion |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No reverse-related utilities
- [x] `src/dsp/core/` - Layer 0 utilities, no conflicts
- [x] `src/dsp/primitives/delay_line.h` - Reference for buffer patterns
- [x] `src/dsp/systems/flexible_feedback_network.h` - Reference for feedback patterns

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ReverseBuffer, ReverseDelay, PlaybackMode) are unique and not found in codebase. The equalPowerCrossfade function will be defined in ReverseBuffer's translation unit only.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | tempoToSamples | `[[nodiscard]] constexpr size_t tempoToSamples(NoteValue note, NoteModifier modifier = NoteModifier::None) const noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| MultimodeFilter | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| MultimodeFilter | process | `void process(float* buffer, size_t numSamples) noexcept` | Yes |
| MultimodeFilter | setType | `void setType(FilterType type) noexcept` | Yes |
| MultimodeFilter | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| IFeedbackProcessor | prepare | `virtual void prepare(double sampleRate, std::size_t maxBlockSize) noexcept = 0` | Yes |
| IFeedbackProcessor | process | `virtual void process(float* left, float* right, std::size_t numSamples) noexcept = 0` | Yes |
| IFeedbackProcessor | reset | `virtual void reset() noexcept = 0` | Yes |
| IFeedbackProcessor | getLatencySamples | `[[nodiscard]] virtual std::size_t getLatencySamples() const noexcept = 0` | Yes |
| FlexibleFeedbackNetwork | setProcessor | `void setProcessor(IFeedbackProcessor* processor, float crossfadeMs = 50.0f) noexcept` | Yes |
| FlexibleFeedbackNetwork | setDelayTimeMs | `void setDelayTimeMs(float ms) noexcept` | Yes |
| FlexibleFeedbackNetwork | setFeedbackAmount | `void setFeedbackAmount(float amount) noexcept` | Yes |
| FlexibleFeedbackNetwork | setProcessorMix | `void setProcessorMix(float mix) noexcept` | Yes |

### Header Files Read

- [x] `src/dsp/core/block_context.h` - BlockContext struct
- [x] `src/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `src/dsp/processors/multimode_filter.h` - MultimodeFilter class
- [x] `src/dsp/systems/i_feedback_processor.h` - IFeedbackProcessor interface
- [x] `src/dsp/systems/flexible_feedback_network.h` - FlexibleFeedbackNetwork class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| MultimodeFilter | Uses `FilterType::` enum | `FilterType::Lowpass` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| equalPowerCrossfade | Generic audio crossfade | stereo_utils.h (future) | ReverseBuffer, possible granular delay |

**Decision**: Keep `equalPowerCrossfade` local for now. Extract to Layer 0 if Granular Delay (4.8) needs it.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples | One-liner, class stores sampleRate_ |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- Granular Delay (4.8) - may share chunk/grain concepts
- Freeze Mode (4.11) - uses FlexibleFeedbackNetwork pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ReverseBuffer | HIGH | Granular Delay (4.8), any chunk-based effect | Layer 1 primitive, reusable |
| ReverseFeedbackProcessor | HIGH | Any mode needing reverse in feedback | Layer 2, implements IFeedbackProcessor |
| Crossfade logic | MEDIUM | Granular Delay (4.8), any chunk-based effect | Keep in ReverseBuffer |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Use FlexibleFeedbackNetwork | Follows ShimmerDelay pattern, enables component reuse |
| ReverseFeedbackProcessor as Layer 2 | Implements IFeedbackProcessor, injectable into any FlexibleFeedbackNetwork |
| ReverseBuffer as Layer 1 | Pure buffer primitive, no dependencies on higher layers |
| Keep crossfade in ReverseBuffer | Part of the buffer's core functionality |

## Project Structure

### Documentation (this feature)

```text
specs/030-reverse-delay/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 research output
├── data-model.md        # Phase 1 component design
├── quickstart.md        # Quick verification tests
└── checklists/
    └── requirements.md  # Specification checklist
```

### Source Code (repository root)

```text
src/dsp/
├── primitives/
│   └── reverse_buffer.h           # NEW: Layer 1 - Capture + reverse playback
├── processors/
│   └── reverse_feedback_processor.h  # NEW: Layer 2 - IFeedbackProcessor for reverse
└── features/
    └── reverse_delay.h            # NEW: Layer 4 - Complete reverse delay mode

tests/unit/
├── primitives/
│   └── reverse_buffer_test.cpp    # NEW: ReverseBuffer unit tests
├── processors/
│   └── reverse_feedback_processor_test.cpp  # NEW: ReverseFeedbackProcessor tests
└── features/
    └── reverse_delay_test.cpp     # NEW: ReverseDelay integration tests
```

**Structure Decision**: Follows ShimmerDelay pattern (shimmer_feedback_processor.h + shimmer_delay.h)

## Complexity Tracking

> No constitution violations.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Design Decisions

### FR-015: FlexibleFeedbackNetwork with ReverseFeedbackProcessor

**Requirement**: FR-015 specifies "System MUST use FlexibleFeedbackNetwork (Layer 3) for feedback management"

**Implementation**: ReverseDelay uses FlexibleFeedbackNetwork with an injected ReverseFeedbackProcessor:

- **FlexibleFeedbackNetwork** - Manages delay line (set to minimum), feedback loop, filter, and limiter
- **ReverseFeedbackProcessor** - Implements IFeedbackProcessor, wraps stereo ReverseBuffer pair

**Signal Flow**:
```
Input → FlexibleFeedbackNetwork:
         ├── Delay Line (minimal, just for feedback timing)
         ├── ReverseFeedbackProcessor (injected):
         │   ├── ReverseBuffer L (capture + reverse playback)
         │   ├── ReverseBuffer R (capture + reverse playback)
         │   └── Crossfade between chunks
         ├── MultimodeFilter (optional, in feedback path)
         └── DynamicsProcessor + tanh soft limiting
→ Output
```

**Compliance Status**: FR-015 is **FULLY MET** - uses FlexibleFeedbackNetwork with processor injection, following the established ShimmerDelay pattern.

### ReverseBuffer Double-Buffer Design

The ReverseBuffer uses a double-buffer (A/B) strategy:
1. While buffer A captures incoming audio, buffer B plays back (reversed)
2. When capture is complete, swap roles: A plays back, B captures
3. Crossfade during swap prevents clicks

```
Time →
Buffer A: [Capture.........][Play (reverse)....][Capture........][Play (reverse)...]
Buffer B: [Play (reverse)...][Capture.........][Play (reverse)...][Capture........]
                           ↑                    ↑
                         Swap                 Swap
```

### Playback Mode Implementation

| Mode | Behavior |
|------|----------|
| FullReverse | Every chunk plays reversed |
| Alternating | Chunk 1: reverse, Chunk 2: forward, Chunk 3: reverse... |
| Random | Each chunk independently decides forward/reverse (50/50) |

Mode stored as state in ReverseBuffer; checked at each chunk boundary.
