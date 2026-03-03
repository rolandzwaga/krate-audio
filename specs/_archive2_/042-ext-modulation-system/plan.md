# Implementation Plan: Extended Modulation System

**Branch**: `042-ext-modulation-system` | **Date**: 2026-02-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/042-ext-modulation-system/spec.md`

## Summary

Extend the Ruinae synthesizer's modulation system at two levels: (1) per-voice modulation by adding Aftertouch as a new VoiceModSource, OscALevel/OscBLevel as new VoiceModDest values, and updating VoiceModRouter.computeOffsets() and RuinaeVoice.processBlock() accordingly; (2) global modulation by composing the existing ModulationEngine into a test scaffold that registers global sources (LFO 1-2, Chaos, Rungler, EnvFollower, Macros 1-4, Pitch Bend, Mod Wheel) and destinations (Global Filter Cutoff/Resonance, Master Volume, Effect Mix, All Voice Filter Cutoff, All Voice Morph Position, Trance Gate Rate), with global-to-voice forwarding. The Rungler gains a ModulationSource interface. All changes must be real-time safe with zero allocations in the process path.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Steinberg VST3 SDK (not directly used in this DSP-only spec), KrateDSP (Layer 0-3), Catch2 (testing)
**Storage**: N/A (no persistence in this spec)
**Testing**: Catch2 (unit tests in `dsp/tests/unit/systems/` and `dsp/tests/unit/processors/`)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo -- shared DSP library at `dsp/`
**Performance Goals**: <0.5% CPU for 16 per-voice routes across 8 voices; <0.5% CPU for 32 global routings; zero heap allocations during processBlock()
**Constraints**: Hard real-time audio thread -- no allocations, locks, exceptions, or I/O in process path
**Scale/Scope**: 25 functional requirements, 8 success criteria, extension of existing components

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | This spec is DSP-only (Layer 2-3), no processor/controller changes |
| II. Real-Time Safety | PASS | All processing methods noexcept, fixed-size arrays, no allocations in process path |
| III. Modern C++ | PASS | C++20, scoped enums, std::array, constexpr, no raw new/delete in audio path |
| IV. SIMD & DSP Optimization | PASS | See SIMD analysis below -- not beneficial for this feature |
| V. VSTGUI Development | N/A | No UI in this spec |
| VI. Cross-Platform Compatibility | PASS | Pure C++ with no platform-specific code |
| VII. Project Structure & Build | PASS | All files in correct layer directories, CMake targets updated |
| VIII. Testing Discipline | PASS | Tests written before implementation per Principle XIII |
| IX. Layered Architecture | PASS | VoiceModRouter/RuinaeVoice at Layer 3; Rungler adapter at Layer 2; enums at Layer 3 |
| X. DSP Processing Constraints | PASS | No saturation/interpolation changes; existing constraints maintained |
| XI. Performance Budgets | PASS | SC-001 (<0.5% for per-voice), SC-002 (<0.5% for global) |
| XII. Test-First Development | PASS | All task groups start with failing tests |
| XIII. Test-First Development | PASS | Skills auto-load |
| XIV. Living Architecture Documentation | PASS | Architecture docs updated as final task |
| XV. ODR Prevention | PASS | See Codebase Research section -- all planned types verified unique |
| XVI. Honest Completion | ACKNOWLEDGED | Compliance table filled from actual code/test output only |

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: No new classes or structs. All changes are extensions to existing types (new enum values, new parameters to existing methods, new member functions).

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| VoiceModSource::Aftertouch | `grep -r "Aftertouch" dsp/ plugins/` | No | Add new enum value |
| VoiceModDest::OscALevel | `grep -r "OscALevel" dsp/ plugins/` | No | Add new enum value |
| VoiceModDest::OscBLevel | `grep -r "OscBLevel" dsp/ plugins/` | No | Add new enum value |
| RunglerModSource (wrapper) | `grep -r "RunglerModSource\|RunglerSource" dsp/ plugins/` | No | NOT creating -- spec says add interface directly to Rungler class |

**Utility Functions to be created**: None. All utility functions needed already exist.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| VoiceModRouter | `dsp/include/krate/dsp/systems/voice_mod_router.h` | 3 | EXTEND: add aftertouch parameter to computeOffsets() |
| VoiceModSource enum | `dsp/include/krate/dsp/systems/ruinae_types.h` | 3 | EXTEND: add Aftertouch value before NumSources |
| VoiceModDest enum | `dsp/include/krate/dsp/systems/ruinae_types.h` | 3 | EXTEND: add OscALevel, OscBLevel before NumDestinations |
| VoiceModRoute struct | `dsp/include/krate/dsp/systems/ruinae_types.h` | 3 | Reuse as-is |
| RuinaeVoice | `dsp/include/krate/dsp/systems/ruinae_voice.h` | 3 | EXTEND: apply OscA/BLevel offsets; add setAftertouch() |
| ModulationEngine | `dsp/include/krate/dsp/systems/modulation_engine.h` | 3 | Reuse as-is: compose into test scaffold |
| ModSource enum | `dsp/include/krate/dsp/core/modulation_types.h` | 0 | Reuse: LFO1, LFO2, EnvFollower, Chaos, Macro1-4 |
| ModRouting struct | `dsp/include/krate/dsp/core/modulation_types.h` | 0 | Reuse as-is |
| ChaosModSource | `dsp/include/krate/dsp/processors/chaos_mod_source.h` | 2 | Reuse as-is (already implements ModulationSource) |
| Rungler | `dsp/include/krate/dsp/processors/rungler.h` | 2 | EXTEND: add ModulationSource interface |
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | 2 | Reuse as-is (has getCurrentValue() but does NOT implement ModulationSource) |
| LFO | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | Reuse as-is (owned by ModulationEngine) |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Reuse as-is (used by ModulationEngine for 20ms smoothing) |
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | Reuse as-is (per-voice envelopes) |
| ModulationSource interface | `dsp/include/krate/dsp/core/modulation_source.h` | 0 | Rungler must implement this |
| BlockContext | `dsp/include/krate/dsp/core/block_context.h` | 0 | Used by ModulationEngine.process() |
| frequencyToMidiNote | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Used for key tracking computation |
| TranceGateParams | `dsp/include/krate/dsp/processors/trance_gate.h` | 2 | Contains rateHz field for trance gate rate destination |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no conflicts
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- no conflicts
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors -- Rungler will be extended
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems -- VoiceModRouter, RuinaeVoice, ruinae_types will be extended
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new classes or structs are being created. All changes are additions to existing enums (new values), new parameters to existing methods, and adding a base class to an existing class (Rungler : public ModulationSource). The Aftertouch, OscALevel, and OscBLevel names are unique in the codebase (verified by grep).

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| VoiceModRouter | computeOffsets | `void computeOffsets(float env1, float env2, float env3, float lfo, float gate, float velocity, float keyTrack) noexcept` | Yes |
| VoiceModRouter | getOffset | `[[nodiscard]] float getOffset(VoiceModDest dest) const noexcept` | Yes |
| VoiceModRouter | setRoute | `void setRoute(int index, VoiceModRoute route) noexcept` | Yes |
| RuinaeVoice | noteOn | `void noteOn(float frequency, float velocity) noexcept` | Yes |
| RuinaeVoice | processBlock | `void processBlock(float* output, size_t numSamples) noexcept` | Yes |
| ModulationEngine | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| ModulationEngine | process | `void process(const BlockContext& ctx, const float* inputL, const float* inputR, size_t numSamples) noexcept` | Yes |
| ModulationEngine | setRouting | `void setRouting(size_t index, const ModRouting& routing) noexcept` | Yes |
| ModulationEngine | getModulationOffset | `[[nodiscard]] float getModulationOffset(uint32_t destParamId) const noexcept` | Yes |
| ModulationEngine | setMacroValue | `void setMacroValue(size_t index, float value) noexcept` | Yes |
| Rungler | process | `[[nodiscard]] Output process() noexcept` | Yes |
| Rungler | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| Rungler::Output | rungler | `float rungler = 0.0f;  ///< Rungler CV (filtered DAC output) [0, +1]` | Yes |
| ModulationSource | getCurrentValue | `[[nodiscard]] virtual float getCurrentValue() const noexcept = 0` | Yes |
| ModulationSource | getSourceRange | `[[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0` | Yes |
| ChaosModSource | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept override` (returns normalizedOutput_) | Yes |
| ChaosModSource | checkAndResetIfDiverged | `void checkAndResetIfDiverged() noexcept` (checks 10x safeBound_) | Yes |
| TranceGateParams | rateHz | `float rateHz{4.0f}; ///< Free-run step rate in Hz [0.1, 100.0]` | Yes |
| TranceGate | setParams | `void setParams(const TranceGateParams& params) noexcept` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| frequencyToMidiNote | signature | `[[nodiscard]] inline float frequencyToMidiNote(float hz) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/voice_mod_router.h` - VoiceModRouter class
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - VoiceModSource/VoiceModDest enums, VoiceModRoute struct
- [x] `dsp/include/krate/dsp/systems/ruinae_voice.h` - RuinaeVoice class
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - ModulationEngine class
- [x] `dsp/include/krate/dsp/systems/modulation_matrix.h` - ModulationMatrix class (reference only)
- [x] `dsp/include/krate/dsp/processors/rungler.h` - Rungler class
- [x] `dsp/include/krate/dsp/processors/chaos_mod_source.h` - ChaosModSource class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/core/modulation_source.h` - ModulationSource interface
- [x] `dsp/include/krate/dsp/core/modulation_types.h` - ModSource enum, ModRouting struct
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - frequencyToMidiNote(), semitonesToRatio()
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/trance_gate.h` - TranceGateParams, TranceGate class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ModulationEngine | Uses uint32_t destParamId (not enum) for destinations | `engine.getModulationOffset(myDestId)` with arbitrary uint32_t IDs |
| ModulationEngine | Uses ModSource enum (not VoiceModSource) for global routing | `ModRouting{.source = ModSource::LFO1, ...}` |
| VoiceModRouter | computeOffsets takes 7 float parameters (not an array/struct) | Must be extended to 8 for Aftertouch |
| Rungler | Does NOT implement ModulationSource currently | Must add `: public ModulationSource` to class declaration |
| EnvelopeFollower | Does NOT implement ModulationSource (has getCurrentValue but not virtual) | ModulationEngine uses it internally, not through the interface |
| ChaosModSource | Lorenz safeBound_ = 50.0f, divergence check uses 10x (500) | Matches FR-025 |
| TranceGate | rateHz range is [0.1, 100.0] in TranceGateParams | Spec FR-020 limits to [0.1, 20.0] for modulation -- clamp in forwarding code |
| VoiceModDest | NumDestinations is the sentinel (currently 7) | After adding OscALevel, OscBLevel, it becomes 9 |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| setAftertouch(float) | RuinaeVoice-specific, one consumer |
| Global-to-voice forwarding | Engine-level orchestration, not reusable as standalone |

**Decision**: No new Layer 0 utilities needed. All required functions already exist.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Modulation routing is a pure source*amount accumulation with no feedback |
| **Data parallelism width** | 16 routes (per-voice), 32 routes (global) | Independent routes could theoretically be SIMD-parallelized |
| **Branch density in inner loop** | LOW | Simple active check + multiply-accumulate per route |
| **Dominant operations** | Multiply-accumulate | sourceValue * amount, sum to destination |
| **Current CPU budget vs expected usage** | <0.5% budget vs ~0.1% expected | Well within budget without optimization |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The VoiceModRouter processes at most 16 routes per block (not per sample) with a simple multiply-accumulate loop. At 0.1% expected CPU, optimization is unnecessary. The 16-wide route iteration already fits in L1 cache. The global ModulationEngine already exists and processes 32 routes efficiently. SIMD would add complexity without meaningful benefit given the per-block (not per-sample) evaluation.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when routeCount_ == 0 | ~100% savings for unrouted voices | LOW | YES (already exists) |
| Skip inactive route slots | ~50% when <8 routes active | LOW | YES (already exists via active_[] flags) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 (Systems)

**Related features at same layer** (from roadmap):
- Phase 5: Effects Section (RuinaeEffectsChain at Layer 3/4)
- Phase 6: Ruinae Engine Composition (RuinaeEngine at Layer 3)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Global-to-voice forwarding pattern | HIGH | Phase 6 RuinaeEngine | Keep as implementation pattern; document in architecture docs |
| Rungler ModulationSource adapter | MEDIUM | Any future Layer 2 processor needing ModulationSource | Keep in Rungler class; pattern is simple to replicate |
| setAftertouch() dispatch pattern | HIGH | Phase 6 for MPE per-note pressure | Design with future MPE in mind (same API, different dispatch) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Rungler inherits ModulationSource directly | Spec clarification: avoids wrapper overhead; runglerCV_ is already maintained per-sample |
| No Rungler wrapper class | Spec explicitly says add interface directly to Rungler class |
| Per-voice route amounts NOT smoothed | Spec clarification: sources already smooth, minimal overhead design |
| Global amounts smoothed via existing ModulationEngine | Already uses OnePoleSmoother (20ms) per routing slot |

## Project Structure

### Documentation (this feature)

```text
specs/042-ext-modulation-system/
  plan.md              # This file
  research.md          # Phase 0 research output
  data-model.md        # Phase 1 entity model
  quickstart.md        # Phase 1 quickstart guide
  contracts/           # Phase 1 API contracts
  tasks.md             # Phase 2 task groups (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
  include/krate/dsp/
    core/
      modulation_source.h          # UNCHANGED: interface Rungler will implement
      modulation_types.h           # UNCHANGED: ModSource, ModRouting reused as-is
    processors/
      rungler.h                    # MODIFIED: add ModulationSource inheritance
    systems/
      ruinae_types.h               # MODIFIED: add Aftertouch, OscALevel, OscBLevel enums
      voice_mod_router.h           # MODIFIED: add aftertouch param to computeOffsets()
      ruinae_voice.h               # MODIFIED: apply OscA/BLevel offsets, add setAftertouch()
      modulation_engine.h          # UNCHANGED: composed as-is
  tests/
    unit/
      processors/
        rungler_test.cpp           # EXTENDED: add ModulationSource interface tests
      systems/
        voice_mod_router_test.cpp  # EXTENDED: add Aftertouch/OscALevel/OscBLevel tests
        ruinae_voice_test.cpp      # EXTENDED: add OscA/BLevel application tests
        ext_modulation_test.cpp    # NEW: global modulation composition + forwarding tests
```

**Structure Decision**: All changes extend existing files. One new test file (`ext_modulation_test.cpp`) for global modulation integration tests that don't fit in existing test files. This file must be registered in `dsp/tests/CMakeLists.txt`.

## Complexity Tracking

No constitution violations. All changes follow existing patterns.

---

# Phase 0: Research

## Research Findings

### R-001: Rungler ModulationSource Adapter Pattern

**Decision**: Add `public ModulationSource` to the Rungler class declaration and implement `getCurrentValue()` and `getSourceRange()` as virtual overrides.

**Rationale**: The spec explicitly states the ModulationSource interface must be added directly to Rungler (not via a wrapper). The Rungler already maintains `runglerCV_` as a [0, +1] value updated per sample in `process()`. The `getCurrentValue()` override simply returns this value. The existing `ChaosModSource`, `RandomSource`, `TransientDetector`, `SampleHoldSource`, and `PitchFollowerSource` all follow this exact pattern (Layer 2 processor inheriting from Layer 0 ModulationSource interface).

**Alternatives Considered**:
- Wrapper class `RunglerModSource`: Rejected by spec clarification. Would add indirection overhead and an extra class.
- Passing Rungler output manually to ModulationEngine: Would break the ModulationSource abstraction and require special-case code in the engine.

**Implementation Detail**: The Rungler header already includes `<krate/dsp/core/db_utils.h>` (Layer 0). Adding `#include <krate/dsp/core/modulation_source.h>` is valid since Layer 2 can depend on Layer 0. The `process()` method already updates `runglerCV_`; `getCurrentValue()` returns it. `getSourceRange()` returns `{0.0f, 1.0f}`.

### R-002: VoiceModSource/VoiceModDest Enum Extension Strategy

**Decision**: Add `Aftertouch` before `NumSources` in VoiceModSource (making NumSources = 8). Add `OscALevel` and `OscBLevel` before `NumDestinations` in VoiceModDest (making NumDestinations = 9).

**Rationale**: The enums use `NumSources`/`NumDestinations` as sentinels for array sizing. Adding values before the sentinel automatically resizes all `std::array` instances that use `static_cast<size_t>(NumSources)` and `static_cast<size_t>(NumDestinations)`. This is the same pattern used when the original 7 sources/destinations were created.

**Backward Compatibility Impact**: Existing code that uses specific enum values (Env1=0 through KeyTrack=6, FilterCutoff=0 through OscBPitch=6) remains correct because the new values are appended after the existing ones but before the sentinel. The VoiceModRouter::computeOffsets() test file references enum values by name, not by numeric value, so tests remain valid.

**Key Change**: VoiceModRouter::computeOffsets() signature changes from 7 params to 8 (adding `float aftertouch`). This is a **breaking API change** -- all callers must be updated. There are exactly 2 callers:
1. `ruinae_voice.h` processBlock() (line 355)
2. `voice_mod_router_test.cpp` (every test case)

### R-003: RuinaeVoice OscALevel/OscBLevel Application Point

**Decision**: Apply OscALevel and OscBLevel modulation offsets in the mixing stage of processBlock(), after oscillator generation but before the mix computation.

**Rationale**: The spec says `effectiveLevel = clamp(baseLevel + offset, 0.0, 1.0)` where baseLevel = 1.0. The current signal flow generates oscABuffer_ and oscBBuffer_ independently, then mixes them. The most natural place to apply per-oscillator level modulation is to scale each buffer before mixing. This avoids modifying the oscillator code and keeps the modulation application centralized.

**Implementation**: After computing modulation offsets, before the mix loop:
```cpp
const float oscALevelOffset = modRouter_.getOffset(VoiceModDest::OscALevel);
const float oscBLevelOffset = modRouter_.getOffset(VoiceModDest::OscBLevel);
const float effectiveOscALevel = std::clamp(1.0f + oscALevelOffset, 0.0f, 1.0f);
const float effectiveOscBLevel = std::clamp(1.0f + oscBLevelOffset, 0.0f, 1.0f);
```
Then scale buffers by effective levels before mixing.

**Note**: Currently, modulation offsets are computed per-sample inside the inner loop (the modRouter_.computeOffsets() call is at line 355 in the per-sample loop). For OscALevel/OscBLevel, the modulation offset is applied per-block (once) since the oscillator buffers are generated as complete blocks. This matches the per-block computation design (FR-009).

**Refinement**: Since computeOffsets is currently called per-sample (inside the for loop), we need to either:
1. Move computeOffsets() to per-block (before the loop) -- matches spec FR-009 ("per-block")
2. Or extract OscALevel/OscBLevel from the first sample's computation.

Looking at the existing code more carefully: the spec from 041 says "per-block" but the actual implementation calls computeOffsets() per-sample. This was likely done because the filter cutoff modulation needs per-sample envelope tracking. The solution: compute the OscALevel/OscBLevel offsets from the FIRST call to computeOffsets() in the block, and apply them uniformly across the block. Or more practically, since envelopes change smoothly, use the values from the first sample's modulation computation.

**Final approach**: Restructure processBlock() to:
1. Compute a single "block-start" modulation offset for OscALevel/OscBLevel using the current envelope/LFO values at the start of the block
2. Apply these levels to oscABuffer_ and oscBBuffer_ before entering the per-sample loop
3. The per-sample loop continues to compute filter cutoff, morph position, etc. per sample as before

### R-004: Global Modulation Composition Architecture

**Decision**: For this spec, global modulation is tested via a minimal test scaffold (not a full RuinaeEngine, which is Phase 6). The test scaffold composes a ModulationEngine instance, registers sources and destinations, processes blocks, and verifies modulated values.

**Rationale**: The spec says "The Ruinae engine (Phase 6) does not yet exist; this spec defines how the ModulationEngine will be composed into it when it is built. For testing purposes, a minimal test scaffold suffices."

**Design**: The test scaffold is a test-only class in `ext_modulation_test.cpp` that:
- Owns a `ModulationEngine` instance
- Defines uint32_t constants for global destination IDs (e.g., `kGlobalFilterCutoffId = 0`)
- Registers ModRouting entries mapping ModSource values to destination IDs
- Calls `engine.process(ctx, inputL, inputR, numSamples)`
- Reads offsets via `engine.getModulationOffset(destId)`

**Global-to-Voice Forwarding**: The forwarding pattern is tested by:
1. Computing global offsets from ModulationEngine
2. Applying them to voice parameters following the two-stage clamping formula:
   `finalValue = clamp(clamp(baseValue + perVoiceOffset, min, max) + globalOffset, min, max)`

**Destination-specific ranges and scaling**:
| Forwarded Destination | Min | Max | Scaling | Rationale |
|----------------------|-----|-----|---------|-----------|
| All Voice Filter Cutoff | -96.0 | +96.0 (semitones) | offset * 48 | 4 octaves matches standard modulation range in professional synthesizers (Surge XT, Vital) |
| All Voice Morph Position | 0.0 | 1.0 (normalized) | direct | Linear blend position, no scaling needed |
| Trance Gate Rate | 0.1 | 20.0 (Hz) | offset * 19.9 | Covers slow rhythmic pulses to fast tremolo |

### R-005: Pitch Bend and Mod Wheel Source Integration

**Decision**: Pitch Bend and Mod Wheel are not existing ModSource enum values. The ModulationEngine does not have built-in pitch bend or mod wheel sources. They must be integrated as macro-like external values injected into the engine.

**Rationale**: Looking at ModSource enum: `{None, LFO1, LFO2, EnvFollower, Random, Macro1-4, Chaos, SampleHold, PitchFollower, Transient}`. There is no PitchBend or Mod Wheel source. The simplest approach consistent with the existing architecture:

**Option A**: Use Macro slots for Pitch Bend and Mod Wheel. Macro1-4 already exist as adjustable [0,1] values. We could reserve Macro1 = Pitch Bend, Macro2 = Mod Wheel, leaving Macro3-4 for user macros.
- Pro: No enum changes, uses existing infrastructure; does not modify the Layer 0 ModSource enum (which would affect the 008-modulation-system spec and all existing tests)
- Con: Reduces available macros from 4 to 2

**Option B**: Add PitchBend and Mod Wheel to the ModSource enum in modulation_types.h.
- Pro: Clean, dedicated source slots
- Con: Requires modifying Layer 0 enum and ModulationEngine source handling

**Decision**: Option A -- Use Macros as the vehicle for Pitch Bend and Mod Wheel values. The spec says "Macros 1-4" are among the global sources, implying all 4 are available alongside Pitch Bend and Mod Wheel. However, re-reading FR-012 carefully: "The engine MUST register the following global modulation sources at prepare() time: LFO 1, LFO 2, Chaos Mod, Rungler, Envelope Follower, Macros 1-4, Pitch Bend, and Mod Wheel." This lists 12 sources total.

The ModulationEngine already has 12 ModSource values (None + 12 = 13 with None). But Pitch Bend and Mod Wheel are not among them. We need a way to inject these values.

**Revised Decision**: Since the Ruinae Engine does not yet exist, and we are building a test scaffold, we can:
1. Use ModulationEngine macros for Pitch Bend and Mod Wheel in the test scaffold (Macro1 = Pitch Bend, Macro2 = Mod Wheel, Macro3-4 = user macros), with normalization applied before setting the macro value
2. Document that Phase 6 (Ruinae Engine) may add dedicated PitchBend/Mod Wheel ModSource enum values

This approach:
- Validates the routing and forwarding logic completely
- Does not require modifying the Layer 0 ModSource enum (which would affect all existing tests)
- Provides a clear migration path for Phase 6

For the test scaffold:
- Pitch Bend: Normalize MIDI 14-bit to [-1, +1] then map to [0, 1] for macro: `macroValue = (pitchBend + 1.0) * 0.5`
- Mod Wheel: Normalize CC#1 (0-127) to [0, 1] directly as macro value

**Alternative**: We could create a simple `MidiControlSource` class that implements ModulationSource and holds a float value set from MIDI events. This would be registered with the ModulationMatrix (not ModulationEngine). However, since we're using ModulationEngine (which owns its sources internally), the macro approach is simpler.

### R-006: NaN/Infinity/Denormal Handling in Modulation Path

**Decision**: Add a sanitization step after computing each destination offset in VoiceModRouter that replaces NaN/Inf with 0.0f and flushes denormals.

**Rationale**: FR-024 requires handling NaN, infinity, and denormals. The existing `detail::isNaN()`, `detail::isInf()`, and `detail::flushDenormal()` functions in `db_utils.h` provide the necessary utilities. The sanitization point should be at the output of the router (after accumulation), not at each route, for efficiency.

**Implementation**: After the accumulation loop in computeOffsets(), add a pass over offsets_:
```cpp
for (auto& offset : offsets_) {
    if (detail::isNaN(offset) || detail::isInf(offset)) {
        offset = 0.0f;
    }
    offset = detail::flushDenormal(offset);
}
```

### R-007: Existing Test Patterns

**Decision**: Follow the test patterns established in `voice_mod_router_test.cpp` and `ruinae_voice_test.cpp`.

**Key patterns observed**:
- Use `Catch::Approx` for float comparisons
- Use `.margin()` for loose comparisons where needed
- Helper functions: `createPreparedVoice()`, `processNSamples()`, `computeRMS()`
- Test naming: descriptive with `[tag]` markers
- Test structure: REQUIRE for exact checks, SECTION for variants

---

# Phase 1: Design & Contracts

## Data Model

See `data-model.md` for the complete entity model.

### Entity Changes Summary

**VoiceModSource** (extended from 7 to 8 values):
```
Env1=0, Env2=1, Env3=2, VoiceLFO=3, GateOutput=4, Velocity=5, KeyTrack=6, Aftertouch=7, NumSources=8
```

**VoiceModDest** (extended from 7 to 9 values):
```
FilterCutoff=0, FilterResonance=1, MorphPosition=2, DistortionDrive=3, TranceGateDepth=4,
OscAPitch=5, OscBPitch=6, OscALevel=7, OscBLevel=8, NumDestinations=9
```

**VoiceModRouter.computeOffsets()** (extended from 7 to 8 parameters):
```cpp
void computeOffsets(float env1, float env2, float env3,
                    float lfo, float gate,
                    float velocity, float keyTrack,
                    float aftertouch) noexcept;
```

**Rungler** (extended with ModulationSource interface):
```cpp
class Rungler : public ModulationSource {
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;
};
```

**RuinaeVoice** (extended with aftertouch storage and OscA/BLevel application):
```cpp
class RuinaeVoice {
    void setAftertouch(float value) noexcept;
    // processBlock() modified to apply OscALevel/OscBLevel offsets
private:
    float aftertouch_{0.0f};
};
```

### Global Modulation Destination IDs (for test scaffold)

```cpp
// Arbitrary uint32_t IDs for ModulationEngine destination registration
constexpr uint32_t kGlobalFilterCutoffDestId = 0;
constexpr uint32_t kGlobalFilterResonanceDestId = 1;
constexpr uint32_t kMasterVolumeDestId = 2;
constexpr uint32_t kEffectMixDestId = 3;
constexpr uint32_t kAllVoiceFilterCutoffDestId = 4;
constexpr uint32_t kAllVoiceMorphPositionDestId = 5;
constexpr uint32_t kTranceGateRateDestId = 6;
```

## API Contracts

### Contract 1: VoiceModRouter Extended API

```cpp
// ruinae_types.h - Extended enums
enum class VoiceModSource : uint8_t {
    Env1 = 0, Env2, Env3, VoiceLFO, GateOutput, Velocity, KeyTrack,
    Aftertouch,     // NEW (FR-001)
    NumSources      // = 8
};

enum class VoiceModDest : uint8_t {
    FilterCutoff = 0, FilterResonance, MorphPosition, DistortionDrive,
    TranceGateDepth, OscAPitch, OscBPitch,
    OscALevel,      // NEW (FR-002)
    OscBLevel,      // NEW (FR-002)
    NumDestinations // = 9
};

// voice_mod_router.h - Extended computeOffsets
void computeOffsets(float env1, float env2, float env3,
                    float lfo, float gate,
                    float velocity, float keyTrack,
                    float aftertouch) noexcept;
```

**Preconditions**: aftertouch in [0, 1]
**Postconditions**: getOffset() returns summed contributions for all 9 destinations
**Error Handling**: NaN/Inf in source values replaced with 0.0f

### Contract 2: Rungler ModulationSource Interface

```cpp
// rungler.h - Extended class declaration
class Rungler : public ModulationSource {
public:
    // Existing API unchanged...

    // ModulationSource interface (FR-017)
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return runglerCV_;  // [0, +1]
    }
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {0.0f, 1.0f};
    }
};
```

**Preconditions**: prepare() called, process() being called to update runglerCV_
**Postconditions**: getCurrentValue() returns filtered DAC output in [0, +1]
**Error Handling**: Returns 0.0f if not prepared

### Contract 3: RuinaeVoice Aftertouch + OscLevel API

```cpp
// ruinae_voice.h - New methods
void setAftertouch(float value) noexcept {
    aftertouch_ = std::clamp(value, 0.0f, 1.0f);
}

// Modified processBlock() behavior:
// 1. OscALevel offset applied: effectiveOscALevel = clamp(1.0 + offset, 0.0, 1.0)
// 2. OscBLevel offset applied: effectiveOscBLevel = clamp(1.0 + offset, 0.0, 1.0)
// 3. oscABuffer_[i] *= effectiveOscALevel (before mixing)
// 4. oscBBuffer_[i] *= effectiveOscBLevel (before mixing)
// 5. aftertouch_ passed to computeOffsets() as 8th parameter
```

### Contract 4: Global-to-Voice Forwarding Formula

```cpp
// Two-stage clamping (FR-021):
// Step 1: Per-voice result
float perVoiceResult = std::clamp(baseValue + perVoiceOffset, min, max);
// Step 2: Apply global offset
float finalValue = std::clamp(perVoiceResult + globalOffset, min, max);
```

Applied destinations:
- All Voice Filter Cutoff: forwarded to each voice's filter cutoff
- All Voice Morph Position: forwarded to each voice's mix position
- Trance Gate Rate: forwarded to each voice's trance gate rate (Hz, [0.1, 20.0])

## Task Group Overview

The implementation is organized into 5 task groups matching the 3 priority levels plus smoothing/safety:

### TG-1: Per-Voice Modulation Extensions (P1 -- US1, US2, US3)
- Extend VoiceModSource enum with Aftertouch
- Extend VoiceModDest enum with OscALevel, OscBLevel
- Update VoiceModRouter.computeOffsets() signature (8 params)
- Add NaN/Inf sanitization to computeOffsets()
- Add setAftertouch() to RuinaeVoice
- Apply OscALevel/OscBLevel in RuinaeVoice.processBlock()
- Update all callers of computeOffsets()
- Extend voice_mod_router_test.cpp
- Extend ruinae_voice_test.cpp

### TG-2: Rungler ModulationSource Adapter (P2 -- US6)
- Add ModulationSource inheritance to Rungler
- Implement getCurrentValue() and getSourceRange()
- Add Rungler ModulationSource tests to rungler_test.cpp

### TG-3: Global Modulation Composition (P2 -- US4, US5, US6)
- Create ext_modulation_test.cpp with test scaffold
- Test global source registration (LFO, Chaos, Rungler, Macros, PitchBend/Mod Wheel via macros)
- Test global destination routing
- Test global-to-voice forwarding (All Voice Filter Cutoff, All Voice Morph Position, Trance Gate Rate)
- Test Pitch Bend normalization (14-bit to [-1, +1])
- Test Mod Wheel normalization (CC#1 to [0, 1])
- Register new test file in CMakeLists.txt

### TG-4: Real-Time Safety and Smoothing (P3 -- US7)
- Verify zero allocations in all new code paths
- Test NaN/Inf/denormal handling
- Test parameter transition smoothness
- Performance benchmarks for SC-001 and SC-002

### TG-5: Architecture Documentation and Backward Compatibility (Final)
- Verify all existing 041 tests pass without modification (SC-005)
- Update specs/_architecture_/ documentation
- Final compliance verification

## File Change Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `dsp/include/krate/dsp/systems/ruinae_types.h` | MODIFY | Add Aftertouch to VoiceModSource, OscALevel/OscBLevel to VoiceModDest |
| `dsp/include/krate/dsp/systems/voice_mod_router.h` | MODIFY | Add aftertouch param to computeOffsets(), add NaN/Inf sanitization |
| `dsp/include/krate/dsp/systems/ruinae_voice.h` | MODIFY | Add setAftertouch(), apply OscA/BLevel offsets in processBlock() |
| `dsp/include/krate/dsp/processors/rungler.h` | MODIFY | Add ModulationSource inheritance + overrides |
| `dsp/tests/unit/systems/voice_mod_router_test.cpp` | MODIFY | Add Aftertouch/OscALevel/OscBLevel tests, update all computeOffsets calls |
| `dsp/tests/unit/systems/ruinae_voice_test.cpp` | MODIFY | Add OscA/BLevel application tests, aftertouch tests |
| `dsp/tests/unit/processors/rungler_test.cpp` | MODIFY | Add ModulationSource interface tests |
| `dsp/tests/unit/systems/ext_modulation_test.cpp` | NEW | Global modulation composition + forwarding tests |
| `dsp/tests/CMakeLists.txt` | MODIFY | Add ext_modulation_test.cpp to test list |
