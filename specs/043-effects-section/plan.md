# Implementation Plan: Ruinae Effects Section

**Branch**: `043-effects-section` | **Date**: 2026-02-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/043-effects-section/spec.md`

## Summary

Implement `RuinaeEffectsChain`, a Layer 3 system component that composes five existing Layer 4 delay effects (Digital, Tape, PingPong, Granular, Spectral), one Layer 4 freeze effect (FreezeMode), and one Layer 4 reverb (Dattorro Reverb) into a fixed-order stereo processing chain: Freeze -> Delay -> Reverb -> Output. The system normalizes heterogeneous effect APIs, implements a linear crossfade state machine for click-free delay type switching (25-50ms), reports constant worst-case latency, and compensates non-spectral delays with internal padding. All runtime methods are real-time safe (noexcept, zero allocations).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP library (Layer 0-4 components), all existing delay/reverb/freeze effects
**Storage**: N/A (DSP library, no persistent storage)
**Testing**: Catch2 (via dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform DSP library
**Project Type**: Monorepo shared DSP library
**Performance Goals**: < 3.0% CPU overhead at 44.1kHz/512 samples for chain + Digital delay + reverb (SC-001)
**Constraints**: Zero heap allocations in processBlock; noexcept on all runtime methods; constant latency reporting
**Scale/Scope**: Single header-only class (~600-800 LOC) + enum addition to ruinae_types.h + comprehensive test file

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] processBlock() will be noexcept with zero allocations
- [x] All setter methods will be noexcept with zero allocations
- [x] All temporary buffers pre-allocated in prepare()
- [x] No mutexes, exceptions, or I/O in audio path

**Required Check - Principle IX (Layered Architecture):**
- [x] RuinaeEffectsChain at Layer 3 (systems/) -- EXCEPTION: composes Layer 4 effects
- [x] Layer exception documented: spec explicitly states "Layer 3 component that composes Layer 4 effects, consistent with the roadmap architecture"
- [x] Existing Layer 4 effects used as-is (composed, not extended)

**Required Check - Principle XII/XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle IV (SIMD Optimization):**
- [x] SIMD Optimization Analysis completed (see section below)

**Constitution Violations**: None. The Layer 3 -> Layer 4 composition is an accepted architectural exception for effects chain composition, documented in both the spec and roadmap.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: RuinaeEffectsChain, RuinaeDelayType (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| RuinaeEffectsChain | `grep -r "RuinaeEffectsChain" dsp/ plugins/` | No | Create New |
| RuinaeDelayType | `grep -r "RuinaeDelayType" dsp/ plugins/` | No | Create New in ruinae_types.h |
| EffectsChain | `grep -r "class EffectsChain" dsp/ plugins/` | No | Safe to use (but we use RuinaeEffectsChain to avoid ambiguity) |

**Utility Functions to be created**: None. All crossfade utilities already exist in `crossfade_utils.h`.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| crossfadeIncrement | `grep -r "crossfadeIncrement" dsp/` | Yes | `core/crossfade_utils.h` | Reuse |
| equalPowerGains | `grep -r "equalPowerGains" dsp/` | Yes | `core/crossfade_utils.h` | NOT USED (spec mandates linear, not equal-power) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DigitalDelay | `effects/digital_delay.h` | 4 | Composed: one of five delay types |
| TapeDelay | `effects/tape_delay.h` | 4 | Composed: one of five delay types |
| PingPongDelay | `effects/ping_pong_delay.h` | 4 | Composed: one of five delay types |
| GranularDelay | `effects/granular_delay.h` | 4 | Composed: one of five delay types |
| SpectralDelay | `effects/spectral_delay.h` | 4 | Composed: one of five delay types |
| Reverb | `effects/reverb.h` | 4 | Composed: Dattorro reverb in chain |
| ReverbParams | `effects/reverb.h` | 4 | Parameter forwarding struct |
| FreezeMode | `effects/freeze_mode.h` | 4 | Composed: spectral freeze slot |
| BlockContext | `core/block_context.h` | 0 | Tempo/transport context for delays |
| crossfadeIncrement | `core/crossfade_utils.h` | 0 | Calculate per-sample crossfade step |
| DelayLine | `primitives/delay_line.h` | 1 | Latency compensation padding delays |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (where new file lives)
- [x] `dsp/include/krate/dsp/effects/` - Layer 4 effects (dependencies)
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - Where RuinaeDelayType enum goes

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (RuinaeEffectsChain, RuinaeDelayType) are unique and not found anywhere in the codebase. The only modification to an existing file is adding an enum to `ruinae_types.h`, which currently has no `RuinaeDelayType` definition.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | blockSize | `size_t blockSize = 512` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| DigitalDelay | prepare | `void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept` | Yes |
| DigitalDelay | process | `void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept` | Yes |
| DigitalDelay | setTime | `void setTime(float ms) noexcept` | Yes |
| DigitalDelay | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| DigitalDelay | setMix | `void setMix(float amount) noexcept` | Yes |
| DigitalDelay | reset | `void reset() noexcept` | Yes |
| DigitalDelay | snapParameters | `void snapParameters() noexcept` | Yes |
| TapeDelay | prepare | `void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept` | Yes |
| TapeDelay | process (stereo) | `void process(float* left, float* right, size_t numSamples) noexcept` (NO BlockContext!) | Yes |
| TapeDelay | setMotorSpeed | `void setMotorSpeed(float ms) noexcept` | Yes |
| TapeDelay | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| TapeDelay | setMix | `void setMix(float amount) noexcept` | Yes |
| TapeDelay | reset | `void reset() noexcept` | Yes |
| PingPongDelay | prepare | `void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept` | Yes |
| PingPongDelay | process | `void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept` | Yes |
| PingPongDelay | setDelayTimeMs | `void setDelayTimeMs(float ms) noexcept` | Yes |
| PingPongDelay | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| PingPongDelay | setMix | `void setMix(float amount) noexcept` | Yes |
| PingPongDelay | reset | `void reset() noexcept` | Yes |
| PingPongDelay | snapParameters | `void snapParameters() noexcept` | Yes |
| GranularDelay | prepare | `void prepare(double sampleRate) noexcept` (only sampleRate!) | Yes |
| GranularDelay | process | `void process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, size_t numSamples, const BlockContext& ctx) noexcept` (separate in/out!) | Yes |
| GranularDelay | setDelayTime | `void setDelayTime(float ms) noexcept` | Yes |
| GranularDelay | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| GranularDelay | setDryWet | `void setDryWet(float mix) noexcept` | Yes |
| GranularDelay | reset | `void reset() noexcept` | Yes |
| GranularDelay | getLatencySamples | `[[nodiscard]] size_t getLatencySamples() const noexcept` returns 0 | Yes |
| SpectralDelay | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` | Yes |
| SpectralDelay | process | `void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx) noexcept` (in-place!) | Yes |
| SpectralDelay | setBaseDelayMs | `void setBaseDelayMs(float ms) noexcept` | Yes |
| SpectralDelay | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| SpectralDelay | setDryWetMix | `void setDryWetMix(float mix) noexcept` (0-1 normalized, refactored from 0-100) | Yes |
| SpectralDelay | getLatencySamples | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` returns fftSize_ (1024 default) | Yes |
| SpectralDelay | snapParameters | `void snapParameters() noexcept` | Yes |
| SpectralDelay | reset | `void reset() noexcept` | Yes |
| Reverb | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| Reverb | processBlock | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | Yes |
| Reverb | setParams | `void setParams(const ReverbParams& params) noexcept` | Yes |
| Reverb | reset | `void reset() noexcept` | Yes |
| FreezeMode | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize, float maxDelayMs) noexcept` | Yes |
| FreezeMode | process | `void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx) noexcept` | Yes |
| FreezeMode | setFreezeEnabled | `void setFreezeEnabled(bool enabled) noexcept` | Yes |
| FreezeMode | isFreezeEnabled | `[[nodiscard]] bool isFreezeEnabled() const noexcept` | Yes |
| FreezeMode | setPitchSemitones | `void setPitchSemitones(float semitones) noexcept` | Yes |
| FreezeMode | setShimmerMix | `void setShimmerMix(float mix) noexcept` (0-1 normalized, refactored from 0-100) | Yes |
| FreezeMode | setDecay | `void setDecay(float decay) noexcept` (0-1 normalized, refactored from 0-100) | Yes |
| FreezeMode | setDryWetMix | `void setDryWetMix(float mix) noexcept` (0-1 normalized, refactored from 0-100) | Yes |
| FreezeMode | reset | `void reset() noexcept` | Yes |
| FreezeMode | snapParameters | `void snapParameters() noexcept` | Yes |
| FreezeMode | getLatencySamples | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` | Yes |
| crossfadeIncrement | function | `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `float read(size_t delaySamples) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - crossfadeIncrement, equalPowerGains
- [x] `dsp/include/krate/dsp/effects/digital_delay.h` - DigitalDelay class
- [x] `dsp/include/krate/dsp/effects/tape_delay.h` - TapeDelay class
- [x] `dsp/include/krate/dsp/effects/ping_pong_delay.h` - PingPongDelay class
- [x] `dsp/include/krate/dsp/effects/granular_delay.h` - GranularDelay class
- [x] `dsp/include/krate/dsp/effects/spectral_delay.h` - SpectralDelay class
- [x] `dsp/include/krate/dsp/effects/reverb.h` - Reverb class + ReverbParams
- [x] `dsp/include/krate/dsp/effects/freeze_mode.h` - FreezeMode + FreezeFeedbackProcessor
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - Existing Ruinae enums
- [x] `dsp/include/krate/dsp/systems/delay_engine.h` - DelayEngine (reference)
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| TapeDelay | process() does NOT accept BlockContext | `tape_.process(left, right, numSamples)` -- no ctx |
| TapeDelay | setMotorSpeed() not setTime() for delay time | `tape_.setMotorSpeed(ms)` |
| GranularDelay | prepare() takes only sampleRate (no blockSize, no maxDelay) | `granular_.prepare(sampleRate)` |
| GranularDelay | process() uses separate in/out buffers, NOT in-place | Copy to temp, process, copy back |
| GranularDelay | setDryWet() not setMix() | `granular_.setDryWet(mix)` |
| GranularDelay | setDelayTime() not setBaseDelayMs() | `granular_.setDelayTime(ms)` |
| SpectralDelay | setDryWetMix() now uses 0-1 normalized (refactored from 0-100) | `spectral_.setDryWetMix(mix)` -- direct, no conversion |
| SpectralDelay | setBaseDelayMs() not setDelayTime() or setTime() | `spectral_.setBaseDelayMs(ms)` |
| SpectralDelay | getLatencySamples() returns fftSize_ (default 1024) | Chain reports this as worst-case latency |
| FreezeMode | setShimmerMix() now uses 0-1 normalized (refactored from 0-100) | `freeze_.setShimmerMix(mix)` -- direct, no conversion |
| FreezeMode | setDecay() now uses 0-1 normalized (refactored from 0-100) | `freeze_.setDecay(decay)` -- direct, no conversion |
| FreezeMode | setDryWetMix() now uses 0-1 normalized (refactored from 0-100) | When used as insert: set to 1.0 (full wet) |
| FreezeMode | prepare needs maxDelayMs parameter | `freeze_.prepare(sampleRate, maxBlockSize, 5000.0f)` |
| Reverb | prepare() takes only sampleRate | `reverb_.prepare(sampleRate)` |
| Reverb | processBlock() not process() for block | `reverb_.processBlock(left, right, numSamples)` |
| DigitalDelay | setTime() is the primary setter, also has setDelayTime() alias | Use `setTime(ms)` for consistency |
| PingPongDelay | setDelayTimeMs() not setTime() | `pingpong_.setDelayTimeMs(ms)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| processActiveDelay() | Internal dispatch, class-specific state |
| forwardDelayParams() | Per-type API normalization, not generalizable |

**Decision**: No new Layer 0 utilities needed. The crossfade math already exists in `crossfade_utils.h`.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Each delay type has internal feedback |
| **Data parallelism width** | 2 channels | Stereo only, no multi-voice |
| **Branch density in inner loop** | MEDIUM | Crossfade state check + delay type dispatch |
| **Dominant operations** | Memory (buffer copy) + delegated processing | Most work done by composed effects |
| **Current CPU budget vs expected usage** | < 3% vs well within | Effects chain is thin orchestration layer |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The RuinaeEffectsChain is a thin orchestration layer that delegates all actual DSP to composed effects. Its own per-sample work is limited to crossfade blending (2-channel linear ramp) and buffer copying. The parallelism width (2 channels) wastes 50-75% of SIMD lanes. The dominant cost is the composed effects themselves, which already have their own optimization. Chain overhead is trivially small.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip inactive effects (mix=0 bypass) | High for bypassed slots | LOW | YES |
| Early-out for zero-sample blocks | Trivial gain | LOW | YES |
| Quiesce outgoing delay after crossfade | Saves CPU of idle delay | LOW | YES (FR-013) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 (Systems)

**Related features at same layer** (from roadmap):
- Phase 6: Ruinae Engine Composition (will own RuinaeEffectsChain)
- Future synth effects chains

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| RuinaeEffectsChain | LOW (Ruinae-specific) | Phase 6 engine only | Keep local |
| Crossfade state machine pattern | MEDIUM | Any multi-mode effect | Document pattern, extract after 2nd use |
| Latency compensation pattern | MEDIUM | Any variable-latency chain | Document, keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for effects | Effects have heterogeneous APIs; uniform interface would be artificial |
| Linear crossfade (not equal-power) | Spec requirement: predictable with feedback delays, no swelling |
| FreezeMode (not FreezeFeedbackProcessor) | FreezeMode provides full API including dry/wet, delay time, smoothing |

## Project Structure

### Documentation (this feature)

```text
specs/043-effects-section/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- ruinae_effects_chain_api.h  # Public API contract
+-- tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- systems/
|   |   +-- ruinae_types.h              # MODIFIED: add RuinaeDelayType enum
|   |   +-- ruinae_effects_chain.h      # NEW: RuinaeEffectsChain class
+-- tests/
    +-- unit/systems/
        +-- ruinae_effects_chain_test.cpp  # NEW: comprehensive tests
```

**Structure Decision**: Single new header in `systems/` layer, enum addition to existing `ruinae_types.h`, one new test file in `tests/unit/systems/`.

## Post-Design Constitution Re-Check

*Re-evaluated after Phase 1 design completion (research.md, data-model.md, contracts/, quickstart.md).*

**Principle II (Real-Time Safety):**
- [x] API contract (contracts/ruinae_effects_chain_api.h) confirms all runtime methods are `noexcept`
- [x] data-model.md confirms all temporary buffers (tempL_, tempR_, crossfadeOutL_, crossfadeOutR_) are pre-allocated in prepare()
- [x] research.md R-005 confirms GranularDelay buffer normalization uses pre-allocated temp buffers, not runtime allocations
- [x] No mutexes, exceptions, or I/O in any designed processing path
- [x] Crossfade state machine (R-002) uses simple arithmetic (alpha increment per sample), no allocations

**Principle III (Modern C++):**
- [x] C++20 scoped enum (`RuinaeDelayType : uint8_t`)
- [x] RAII for all composed effects (owned by value, not raw pointers)
- [x] Non-copyable, movable design (delete copy, default move)
- [x] `std::vector<float>` for temp buffers (RAII, pre-allocated in prepare)
- [x] `[[nodiscard]]` on getActiveDelayType() and getLatencySamples()

**Principle IV (SIMD Optimization):**
- [x] SIMD analysis completed in plan.md: verdict NOT BENEFICIAL (thin orchestration layer, 2-channel parallelism wastes lanes)
- [x] Alternative optimizations documented (skip inactive effects, early-out for zero blocks, quiesce after crossfade)

**Principle IX (Layered Architecture):**
- [x] RuinaeEffectsChain at Layer 3 (systems/) -- DOCUMENTED EXCEPTION: composes Layer 4 effects
- [x] Exception documented in API contract Doxygen, plan.md, and spec.md
- [x] All composed effects are used as-is (not extended or modified)
- [x] Layer dependencies: Layer 0 (BlockContext, crossfadeIncrement), Layer 1 (DelayLine), Layer 4 (all effects)

**Principle XIV (ODR Prevention):**
- [x] Both new types (RuinaeEffectsChain, RuinaeDelayType) verified unique via codebase search
- [x] RuinaeDelayType enum added to existing ruinae_types.h (no new file for enum)
- [x] No naming conflicts with any existing component

**Principle XVI (Honest Completion):**
- [x] Compliance table in spec.md exists with all 29 FR + 8 SC rows (empty, to be filled during implementation)
- [x] Completion checklist exists with explicit verification steps

**Post-Design Violations Found**: NONE

All design artifacts are consistent with the project constitution. No exceptions beyond the already-documented Layer 3 -> Layer 4 composition, which is an accepted architectural pattern for effects chain systems.

## Complexity Tracking

No constitution violations to justify.
