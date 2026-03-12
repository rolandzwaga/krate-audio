# Implementation Plan: Ruinae Flanger Effect

**Branch**: `126-ruinae-flanger` | **Date**: 2026-03-12 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/126-ruinae-flanger/spec.md`

## Summary

Add a Flanger DSP processor (Layer 2) to the KrateDSP library and integrate it into the Ruinae synthesizer's effects chain as a modulation slot alternative to the Phaser. The Flanger uses a modulated short delay line (0.3-4.0ms) with feedback and LFO control to produce classic comb-filter sweep effects. The existing `phaserEnabled_` boolean is retired in favor of a three-way modulation type selector (None/Phaser/Flanger) with preset migration for backward compatibility.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang/Xcode 13+, GCC 10+)
**Primary Dependencies**: KrateDSP library (DelayLine, LFO, OnePoleSmoother, NoteValue), VST3 SDK 3.7.x+, VSTGUI 4.12+
**Storage**: VST3 binary state stream (IBStreamer)
**Testing**: Catch2 via CMake (dsp_tests, ruinae_tests) *(Constitution Principle XIII)*
**Target Platform**: Windows 10/11, macOS 11+, Linux (cross-platform required)
**Project Type**: Monorepo (shared DSP library + plugin)
**Performance Goals**: < 0.5% CPU for Flanger at 44.1 kHz stereo (Layer 2 budget); < 5% total plugin
**Constraints**: Zero allocations in audio thread; noexcept processing; real-time safe
**Scale/Scope**: ~1 new DSP class, ~1 new parameter helper file, modifications to effects chain + processor + controller

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; parameter changes via IParameterChanges
- [x] Processor functions without controller
- [x] State flows Host -> Processor -> Controller via setComponentState()

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process path (DelayLine pre-allocated in prepare())
- [x] No locks, mutexes, or blocking primitives
- [x] All buffers pre-allocated before setActive(true)

**Required Check - Principle IX (Layered Architecture):**
- [x] Flanger at Layer 2, depends only on Layer 0 (db_utils, note_value) and Layer 1 (DelayLine, LFO, OnePoleSmoother)
- [x] No circular dependencies

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code in Flanger DSP or parameter helpers
- [x] All VSTGUI cross-platform abstractions used for UI

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `Flanger`, `RuinaeFlangerParams`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `Flanger` | `grep -r "class Flanger" dsp/ plugins/` | No | Create New at `dsp/include/krate/dsp/processors/flanger.h` |
| `RuinaeFlangerParams` | `grep -r "struct RuinaeFlangerParams" dsp/ plugins/` | No | Create New at `plugins/ruinae/src/parameters/flanger_params.h` |

**Utility Functions to be created**: `handleFlangerParamChange`, `registerFlangerParams`, `formatFlangerParam`, `saveFlangerParams`, `loadFlangerParams`, `loadFlangerParamsToController`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `handleFlangerParamChange` | N/A | No | `flanger_params.h` | Create New (mirrors `handlePhaserParamChange`) |
| `registerFlangerParams` | N/A | No | `flanger_params.h` | Create New (mirrors `registerPhaserParams`) |
| `saveFlangerParams` | N/A | No | `flanger_params.h` | Create New (mirrors `savePhaserParams`) |
| `loadFlangerParams` | N/A | No | `flanger_params.h` | Create New (mirrors `loadPhaserParams`) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `DelayLine` | `dsp/include/krate/dsp/primitives/delay_line.h` | 1 | Core delay element; `prepare()`, `write()`, `readLinear()` |
| `LFO` | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | Modulation source; sine/triangle waveforms, tempo sync, phase offset |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Parameter smoothing for rate, depth, feedback, mix |
| `NoteValue` | `dsp/include/krate/dsp/core/note_value.h` | 0 | Tempo sync note value conversion |
| `flushDenormal()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal flushing in feedback path |
| `detail::isNaN()` / `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input safety checks |
| `Phaser` | `dsp/include/krate/dsp/processors/phaser.h` | 2 | Structural reference for interface pattern |
| `RuinaePhaserParams` | `plugins/ruinae/src/parameters/phaser_params.h` | Plugin | Template for parameter helpers |
| `RuinaeEffectsChain` | `plugins/ruinae/src/engine/ruinae_effects_chain.h` | Plugin | Integration point for modulation slot |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing Flanger)
- [x] `specs/_architecture_/layer-2-processors.md` - Component inventory
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID range 1910-1919 is free
- [x] `plugins/ruinae/src/parameters/` - No existing flanger_params.h

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The name `Flanger` does not exist anywhere in the codebase. All planned types and functions have unique names. The `RuinaeFlangerParams` struct is in the `Ruinae` namespace, distinct from the DSP `Krate::DSP::Flanger` class. No conflicts found.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `DelayLine` | `prepare` | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| `DelayLine` | `write` | `void write(float sample) noexcept` | Yes |
| `DelayLine` | `readLinear` | `float readLinear(float delaySamples) const noexcept` | Yes |
| `DelayLine` | `reset` | `void reset() noexcept` | Yes |
| `LFO` | `prepare` | `void prepare(double sampleRate) noexcept` | Yes |
| `LFO` | `process` | `float process() noexcept` (returns -1 to +1) | Yes |
| `LFO` | `setFrequency` | `void setFrequency(float freqHz) noexcept` | Yes |
| `LFO` | `setWaveform` | `void setWaveform(LFOWaveform wf) noexcept` | Yes |
| `LFO` | `setPhaseOffset` | `void setPhaseOffset(float degrees) noexcept` | Yes |
| `LFO` | `setTempoSync` | `void setTempoSync(bool enabled) noexcept` | Yes |
| `LFO` | `setTempo` | `void setTempo(double bpm) noexcept` | Yes |
| `LFO` | `setNoteValue` | `void setNoteValue(NoteValue nv, NoteModifier nm) noexcept` | Yes |
| `LFO` | `reset` | `void reset() noexcept` | Yes |
| `OnePoleSmoother` | `configure` | `void configure(float timeMs, float sampleRate) noexcept` | Yes |
| `OnePoleSmoother` | `process` | `float process() noexcept` | Yes |
| `OnePoleSmoother` | `setTarget` | `void setTarget(float target) noexcept` | Yes |
| `OnePoleSmoother` | `snapTo` | `void snapTo(float value) noexcept` | Yes |
| `detail::flushDenormal` | (free function) | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier
- [x] `dsp/include/krate/dsp/processors/phaser.h` - Phaser reference implementation
- [x] `plugins/ruinae/src/parameters/phaser_params.h` - Parameter helper pattern
- [x] `plugins/ruinae/src/engine/ruinae_effects_chain.h` - Effects chain integration
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID allocation

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `DelayLine::prepare` | Takes `maxDelaySeconds` (float), not ms | `delayLine.prepare(sampleRate, 0.010f)` for 10ms max |
| `DelayLine::readLinear` | Takes delay in **samples** (float), not ms | `readLinear(delayMs * sampleRateF * 0.001f)` |
| `LFO::process` | Returns bipolar [-1, +1], not unipolar [0, 1] | Map to delay: `delayMs = minMs + (lfoValue * 0.5f + 0.5f) * range` |
| `OnePoleSmoother` | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| `OnePoleSmoother::setTarget` | Only sets target, does not do smoothing | Call `process()` per-sample for smoothed value |
| `Phaser` | Uses additive mix: `dry + mix * wet` | Flanger uses true crossfade: `(1-mix)*dry + mix*wet` |
| `detail::flushDenormal` | Located in `db_utils.h`, not `dsp_utils.h` | `#include <krate/dsp/core/db_utils.h>` |
| State stream | `phaserEnabled_` is written **after** phaser params | Migration must read in same order |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `msToSamples(float ms)` | One-liner (`ms * sampleRate_ * 0.001f`); only used within Flanger; class stores sampleRate_ |
| Delay time mapping from LFO | Specific to flanger's 0.3-4.0ms range; not generalizable |

**Decision**: No Layer 0 extractions needed. All utility calculations are trivial one-liners or domain-specific to the flanger. The existing Layer 0/1 primitives provide all necessary reusable functionality.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Delay output feeds back to input via feedback coefficient; 1-sample serial dependency |
| **Data parallelism width** | 2 channels | L/R stereo processing; only 2 independent streams |
| **Branch density in inner loop** | LOW | One NaN/Inf guard per channel; otherwise branchless |
| **Dominant operations** | Arithmetic + memory | Delay line read (memory), LFO (arithmetic), multiply-add (arithmetic) |
| **Current CPU budget vs expected usage** | 0.5% budget vs ~0.1-0.2% expected | Simple delay line + LFO is very cheap |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The flanger has a per-sample feedback loop creating serial dependencies. Only 2 channels of parallelism (stereo) means 50% lane waste with 4-wide SSE. The algorithm is already well under the 0.5% CPU budget at ~0.1-0.2% expected cost. SIMD would add complexity with negligible benefit.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when mix=0 | ~100% savings when bypassed | LOW | YES |
| Skip LFO update when depth=0 | ~30% savings for static comb filter | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (potential future additions):
- Chorus: Modulated delay without feedback, wider delay range
- Vibrato: Modulated delay, wet-only output
- Ensemble: Multi-voice chorus with detune

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `Flanger` class | MEDIUM | Chorus (similar delay modulation, different params) | Keep local; extract modulated-delay base after 2nd use |
| Modulation slot crossfade in RuinaeEffectsChain | MEDIUM | Future modulation effects (chorus, rotary) | Keep in effects chain; generalize if 3rd effect added |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared modulated-delay base class | Only one consumer (Flanger). Phaser uses allpass stages, not delay lines. Wait for chorus. |
| Keep crossfade logic in RuinaeEffectsChain | Crossfade is specific to the slot switching mechanism, not a reusable DSP primitive. |

### Review Trigger

After implementing a **Chorus effect** (if ever planned), review this section:
- [ ] Does Chorus need the same modulated delay line pattern? -> Extract shared base
- [ ] Does Chorus use the same stereo LFO offset pattern? -> Document shared pattern
- [ ] Any duplicated code between Flanger and Chorus? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/126-ruinae-flanger/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── flanger-api.h    # Flanger class interface contract
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
└── flanger.h                            # NEW: Flanger DSP class (Layer 2)

dsp/tests/unit/processors/
└── flanger_test.cpp                     # NEW: Flanger unit tests

plugins/ruinae/src/
├── plugin_ids.h                         # MODIFIED: Add kFlanger* IDs (1910-1919)
├── parameters/
│   └── flanger_params.h                 # NEW: Flanger parameter helpers
├── engine/
│   └── ruinae_effects_chain.h           # MODIFIED: Modulation slot selector + crossfade
├── processor/
│   └── processor.cpp                    # MODIFIED: Handle flanger params, state save/load, migration
└── controller/
    └── controller.cpp                   # MODIFIED: Register flanger params

plugins/ruinae/tests/
└── unit/processor/                      # Tests for state migration
```

**Structure Decision**: Follows existing monorepo layout. DSP class in shared library (Layer 2 processors). Plugin integration in Ruinae plugin source. Mirrors the existing Phaser implementation pattern exactly.

## Complexity Tracking

No constitution violations to justify. All design decisions follow established patterns.
