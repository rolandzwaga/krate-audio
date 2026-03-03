# Implementation Plan: Ruinae Engine Composition

**Branch**: `044-engine-composition` | **Date**: 2026-02-09 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/044-engine-composition/spec.md`

## Summary

Compose all existing Ruinae DSP subsystems (16 RuinaeVoice instances, VoiceAllocator, MonoHandler, NoteProcessor, ModulationEngine, global SVF filter pair, RuinaeEffectsChain, and master output with gain compensation and soft limiting) into the complete `RuinaeEngine` class at Layer 3 (systems). This is an orchestration/composition task -- no new DSP algorithms are introduced. The engine follows the proven PolySynthEngine pattern (spec 038) extended with stereo voice panning, global modulation engine integration, effects chain integration, and stereo width control. The engine provides the single-class API that the Phase 7 plugin shell will instantiate.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (all layers), Catch2 v3 (testing)
**Storage**: N/A (in-memory DSP processing)
**Testing**: Catch2 v3 (unit tests + integration tests) *(Constitution Principle VIII/XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (monorepo)
**Performance Goals**: 8 voices at 44.1kHz < 10% single CPU core (SC-001)
**Constraints**: Zero heap allocations in processBlock(), all setter methods real-time safe, noexcept everywhere
**Scale/Scope**: Single header file (~800-1200 lines), 2 test files (~2000+ lines total)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check** (2026-02-09):

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in processBlock() or any setter method
- [x] No locks, mutexes, or blocking primitives in audio path
- [x] No exceptions (all methods noexcept)
- [x] No I/O operations in audio path
- [x] All buffers pre-allocated in prepare()

**Required Check - Principle III (Modern C++):**
- [x] C++20 target
- [x] RAII for all resources (std::vector for scratch buffers, std::array for voice pool)
- [x] No raw new/delete -- std::vector for dynamic buffers
- [x] constexpr for constants

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (see SIMD section below)

**Required Check - Principle IX (Layered Architecture):**
- [x] RuinaeEngine at Layer 3 (systems)
- [x] Depends on: Layer 0 (sigmoid, db_utils, pitch_utils, block_context, modulation_types), Layer 1 (SVF), Layer 2 (MonoHandler, NoteProcessor), Layer 3 (VoiceAllocator, RuinaeVoice, ModulationEngine, RuinaeEffectsChain)
- [x] Documented Layer 3 exception: composes other Layer 3 systems (same as RuinaeEffectsChain precedent)
- [x] Does NOT depend on Layer 4 directly

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] Plan includes compliance verification strategy for all 44 FR and 14 SC requirements

**Post-Design Re-Check** (completed during planning):
- [x] All design decisions comply with constitution
- [x] No violations requiring Complexity Tracking entries
- [x] Layer dependencies verified: L0 -> L1 -> L2 -> L3 only

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: RuinaeEngine, RuinaeModDest

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| RuinaeEngine | `grep -r "class RuinaeEngine" dsp/ plugins/` | No (only TODO comments in plugins/ruinae/) | Create New |
| RuinaeModDest | `grep -r "enum class RuinaeModDest" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None. All utility functions are already in Layer 0 (Sigmoid::tanh, detail::isNaN, detail::isInf, detail::flushDenormal, semitonesToRatio, midiNoteToFrequency, frequencyToMidiNote).

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| RuinaeVoice | `dsp/include/krate/dsp/systems/ruinae_voice.h` | 3 | 16 instances in voice pool |
| VoiceAllocator | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | 1 instance for poly voice management |
| MonoHandler | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | 1 instance for mono mode |
| NoteProcessor | `dsp/include/krate/dsp/processors/note_processor.h` | 2 | 1 instance for pitch bend/velocity |
| ModulationEngine | `dsp/include/krate/dsp/systems/modulation_engine.h` | 3 | 1 instance for global modulation |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | 2 instances for global stereo filter |
| RuinaeEffectsChain | `dsp/include/krate/dsp/systems/ruinae_effects_chain.h` | 3 | 1 instance for effects processing |
| Sigmoid::tanh | `dsp/include/krate/dsp/core/sigmoid.h` | 0 | Soft limiter |
| detail::isNaN/isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Parameter validation |
| detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal flushing in output |
| VoiceMode | `dsp/include/krate/dsp/systems/poly_synth_engine.h` | 3 | Reuse enum directly |
| BlockContext | `dsp/include/krate/dsp/core/block_context.h` | 0 | Tempo/transport context |
| ModSource | `dsp/include/krate/dsp/core/modulation_types.h` | 0 | Modulation source identifiers |
| ModRouting | `dsp/include/krate/dsp/core/modulation_types.h` | 0 | Modulation routing configuration |
| ReverbParams | `dsp/include/krate/dsp/effects/reverb.h` | 4 | Effects chain parameter forwarding |
| ruinae_types.h | `dsp/include/krate/dsp/systems/ruinae_types.h` | 3 | All Ruinae-specific enums |
| TranceGateParams | `dsp/include/krate/dsp/processors/trance_gate.h` | 2 | Voice parameter forwarding |
| math_constants.h | `dsp/include/krate/dsp/core/math_constants.h` | 0 | kPi for pan law calculation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems
- [x] `dsp/include/krate/dsp/effects/` - Layer 4 effects
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Only two new types (RuinaeEngine class, RuinaeModDest enum), both verified as absent from the codebase. The only reference to "RuinaeEngine" is in TODO comments in `plugins/ruinae/src/processor/processor.h`. The VoiceMode enum is reused from PolySynthEngine (no duplication).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| RuinaeVoice | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Y |
| RuinaeVoice | reset | `void reset() noexcept` | Y |
| RuinaeVoice | noteOn | `void noteOn(float frequency, float velocity) noexcept` | Y |
| RuinaeVoice | noteOff | `void noteOff() noexcept` | Y |
| RuinaeVoice | setFrequency | `void setFrequency(float hz) noexcept` | Y |
| RuinaeVoice | isActive | `[[nodiscard]] bool isActive() const noexcept` | Y |
| RuinaeVoice | processBlock | `void processBlock(float* output, size_t numSamples) noexcept` | Y |
| RuinaeVoice | setAftertouch | `void setAftertouch(float value) noexcept` | Y |
| RuinaeVoice | setOscAType | `void setOscAType(OscType type) noexcept` | Y |
| RuinaeVoice | setOscBType | `void setOscBType(OscType type) noexcept` | Y |
| RuinaeVoice | setMixMode | `void setMixMode(MixMode mode) noexcept` | Y |
| RuinaeVoice | setMixPosition | `void setMixPosition(float mix) noexcept` | Y |
| RuinaeVoice | setFilterType | `void setFilterType(RuinaeFilterType type) noexcept` | Y |
| RuinaeVoice | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` | Y |
| RuinaeVoice | setFilterResonance | `void setFilterResonance(float q) noexcept` | Y |
| RuinaeVoice | setFilterEnvAmount | `void setFilterEnvAmount(float semitones) noexcept` | Y |
| RuinaeVoice | setFilterKeyTrack | `void setFilterKeyTrack(float amount) noexcept` | Y |
| RuinaeVoice | setDistortionType | `void setDistortionType(RuinaeDistortionType type) noexcept` | Y |
| RuinaeVoice | setDistortionDrive | `void setDistortionDrive(float drive) noexcept` | Y |
| RuinaeVoice | setDistortionCharacter | `void setDistortionCharacter(float character) noexcept` | Y |
| RuinaeVoice | setTranceGateEnabled | `void setTranceGateEnabled(bool enabled) noexcept` | Y |
| RuinaeVoice | setTranceGateParams | `void setTranceGateParams(const TranceGateParams& params) noexcept` | Y |
| RuinaeVoice | setTranceGateStep | `void setTranceGateStep(int index, float level) noexcept` | Y |
| RuinaeVoice | setTranceGateTempo | `void setTranceGateTempo(double bpm) noexcept` | Y |
| RuinaeVoice | setModRoute | `void setModRoute(int index, VoiceModRoute route) noexcept` | Y |
| RuinaeVoice | setModRouteScale | `void setModRouteScale(VoiceModDest dest, float scale) noexcept` | Y |
| RuinaeVoice | getAmpEnvelope | `ADSREnvelope& getAmpEnvelope() noexcept` | Y |
| RuinaeVoice | getFilterEnvelope | `ADSREnvelope& getFilterEnvelope() noexcept` | Y |
| RuinaeVoice | getModEnvelope | `ADSREnvelope& getModEnvelope() noexcept` | Y |
| SelectableOscillator | setPhaseMode | `void setPhaseMode(PhaseMode mode) noexcept` | Y |
| VoiceAllocator | noteOn | `auto noteOn(uint8_t note, uint8_t velocity) noexcept` | Y |
| VoiceAllocator | noteOff | `auto noteOff(uint8_t note) noexcept` | Y |
| VoiceAllocator | voiceFinished | `void voiceFinished(size_t voiceIndex) noexcept` | Y |
| VoiceAllocator | setVoiceCount | `auto setVoiceCount(size_t count) noexcept` | Y |
| VoiceAllocator | getVoiceNote | `[[nodiscard]] int getVoiceNote(size_t voiceIndex) const noexcept` | Y |
| VoiceAllocator | getActiveVoiceCount | `[[nodiscard]] uint32_t getActiveVoiceCount() const noexcept` | Y |
| VoiceAllocator | reset | `void reset() noexcept` | Y |
| MonoHandler | noteOn | `MonoNoteEvent noteOn(int note, int velocity) noexcept` | Y |
| MonoHandler | noteOff | `MonoNoteEvent noteOff(int note) noexcept` | Y |
| MonoHandler | processPortamento | `float processPortamento() noexcept` | Y |
| MonoHandler | prepare | `void prepare(double sampleRate) noexcept` | Y |
| MonoHandler | reset | `void reset() noexcept` | Y |
| NoteProcessor | prepare | `void prepare(double sampleRate) noexcept` | Y |
| NoteProcessor | reset | `void reset() noexcept` | Y |
| NoteProcessor | setPitchBend | `void setPitchBend(float bipolar) noexcept` | Y |
| NoteProcessor | processPitchBend | `[[nodiscard]] float processPitchBend() noexcept` | Y |
| NoteProcessor | getFrequency | `[[nodiscard]] float getFrequency(uint8_t note) const noexcept` | Y |
| NoteProcessor | mapVelocity | `[[nodiscard]] VelocityOutput mapVelocity(int velocity) const noexcept` | Y |
| ModulationEngine | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Y |
| ModulationEngine | reset | `void reset() noexcept` | Y |
| ModulationEngine | process | `void process(const BlockContext& ctx, const float* inputL, const float* inputR, size_t numSamples) noexcept` | Y |
| ModulationEngine | setRouting | `void setRouting(size_t index, const ModRouting& routing) noexcept` | Y |
| ModulationEngine | clearRouting | `void clearRouting(size_t index) noexcept` | Y |
| ModulationEngine | getModulationOffset | `[[nodiscard]] float getModulationOffset(uint32_t destParamId) const noexcept` | Y |
| ModulationEngine | setMacroValue | `void setMacroValue(size_t index, float value) noexcept` | Y |
| ModulationEngine | setLFO1Rate | `void setLFO1Rate(float hz) noexcept` | Y |
| ModulationEngine | setLFO1Waveform | `void setLFO1Waveform(Waveform waveform) noexcept` | Y |
| ModulationEngine | setLFO2Rate | `void setLFO2Rate(float hz) noexcept` | Y |
| ModulationEngine | setLFO2Waveform | `void setLFO2Waveform(Waveform waveform) noexcept` | Y |
| ModulationEngine | setChaosSpeed | `void setChaosSpeed(float speed) noexcept` | Y |
| SVF | prepare | `void prepare(double sampleRate) noexcept` (inferred from PolySynthEngine usage) | Y |
| SVF | reset | `void reset() noexcept` | Y |
| SVF | process | `float process(float input) noexcept` | Y |
| SVF | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Y |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Y |
| SVF | setResonance | `void setResonance(float q) noexcept` | Y |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Y |
| SVF | kButterworthQ | `static constexpr float kButterworthQ` | Y |
| RuinaeEffectsChain | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Y |
| RuinaeEffectsChain | reset | `void reset() noexcept` | Y |
| RuinaeEffectsChain | processBlock | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | Y |
| RuinaeEffectsChain | setDelayType | `void setDelayType(RuinaeDelayType type) noexcept` | Y |
| RuinaeEffectsChain | setDelayTime | `void setDelayTime(float ms) noexcept` | Y |
| RuinaeEffectsChain | setDelayFeedback | `void setDelayFeedback(float amount) noexcept` | Y |
| RuinaeEffectsChain | setDelayMix | `void setDelayMix(float mix) noexcept` | Y |
| RuinaeEffectsChain | setDelayTempo | `void setDelayTempo(double bpm) noexcept` | Y |
| RuinaeEffectsChain | setFreezeEnabled | `void setFreezeEnabled(bool enabled) noexcept` | Y |
| RuinaeEffectsChain | setFreeze | `void setFreeze(bool frozen) noexcept` | Y |
| RuinaeEffectsChain | setFreezePitchSemitones | `void setFreezePitchSemitones(float semitones) noexcept` | Y |
| RuinaeEffectsChain | setFreezeShimmerMix | `void setFreezeShimmerMix(float mix) noexcept` | Y |
| RuinaeEffectsChain | setFreezeDecay | `void setFreezeDecay(float decay) noexcept` | Y |
| RuinaeEffectsChain | setReverbParams | `void setReverbParams(const ReverbParams& params) noexcept` | Y |
| RuinaeEffectsChain | getLatencySamples | `[[nodiscard]] size_t getLatencySamples() const noexcept` | Y |
| Sigmoid::tanh | tanh | `inline float tanh(float x) noexcept` (wraps FastMath::fastTanh) | Y |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Y |
| BlockContext | isPlaying | `bool isPlaying = false` | Y |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Y |
| BlockContext | blockSize | `size_t blockSize = 512` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/ruinae_voice.h` - RuinaeVoice class (full file)
- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` - VoiceAllocator class
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - MonoHandler class
- [x] `dsp/include/krate/dsp/processors/note_processor.h` - NoteProcessor class
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - ModulationEngine class (full file)
- [x] `dsp/include/krate/dsp/systems/ruinae_effects_chain.h` - RuinaeEffectsChain class (full file)
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanh
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct (full file)
- [x] `dsp/include/krate/dsp/core/modulation_types.h` - ModSource, ModRouting (full file)
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - All Ruinae enums (full file)
- [x] `dsp/include/krate/dsp/effects/reverb.h` - ReverbParams struct
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Waveform enum
- [x] `dsp/include/krate/dsp/systems/poly_synth_engine.h` - Reference pattern (full file)
- [x] `dsp/include/krate/dsp/systems/selectable_oscillator.h` - setPhaseMode signature

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| RuinaeVoice | prepare() takes 2 params (sampleRate, maxBlockSize) unlike SynthVoice which takes only sampleRate | `voice.prepare(sampleRate, maxBlockSize)` |
| RuinaeVoice | Non-copyable (move-only) due to SelectableOscillator with unique_ptr members | Use `std::array<RuinaeVoice, N>` (default-constructed), never copy |
| MonoHandler | noteOn/noteOff take `int` not `uint8_t` | `monoHandler_.noteOn(static_cast<int>(note), static_cast<int>(velocity))` |
| MonoHandler | Returns `MonoNoteEvent` struct, not `VoiceEvent` | Check `.retrigger`, `.isNoteOn`, `.frequency` fields |
| ModulationEngine | LFO shape setter is `setLFO1Waveform(Waveform)` not `setLFO1Shape(LFOShape)` | Use `Waveform` enum from lfo.h |
| ModulationEngine | Chaos rate setter is `setChaosSpeed(float)` not `setChaosModRate(float)` | `globalModEngine_.setChaosSpeed(rate)` |
| ModulationEngine | Macro setter is `setMacroValue(size_t, float)` not `setMacro(int, float)` | Cast index: `globalModEngine_.setMacroValue(static_cast<size_t>(index), value)` |
| RuinaeEffectsChain | Tempo forwarding uses `setDelayTempo(double)` | `effectsChain_.setDelayTempo(bpm)` |
| SelectableOscillator | Phase mode setter is on oscillator directly: `setPhaseMode(PhaseMode)` | Access via voice's internal oscA_/oscB_ -- but these are private. Need to add forwarding methods in RuinaeVoice or use existing method name convention |
| VoiceAllocator | `getActiveVoiceCount()` returns `uint32_t` (uses atomic internally) | Matches FR-040 return type |
| SVF | kButterworthQ is a static member, value is approximately 0.707f | `SVF::kButterworthQ` |

**Critical Gotcha - Phase Mode**: The spec requires `setOscAPhaseMode(PhaseMode)` and `setOscBPhaseMode(PhaseMode)` on the engine, but RuinaeVoice does NOT expose phase mode setters. The SelectableOscillator has `setPhaseMode()` but it is accessed only via `oscA_` and `oscB_` which are private members of RuinaeVoice. The implementation will need to either:
1. Add `setOscAPhaseMode(PhaseMode)` and `setOscBPhaseMode(PhaseMode)` forwarding methods to RuinaeVoice, OR
2. Skip phase mode forwarding and document the gap.

**Decision**: Add forwarding methods to RuinaeVoice as a minimal addition during implementation. This is a trivial 2-line method per oscillator.

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| Equal-power pan law | Audio panning algorithm, potentially reusable | Keep inline | Only RuinaeEngine currently |
| Mid/Side width encoding | Stereo processing pattern | Keep inline | Only RuinaeEngine currently |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `recalculatePanPositions()` | Engine-specific, depends on voice indices and spread parameter |
| `applystereoWidth()` | Simple inline loop, only used in processBlock |

**Decision**: No Layer 0 extractions needed. The stereo utilities are simple inline loops with only one consumer. If future engines need panning, extract at that time.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (per-voice) | Each voice has internal feedback (filter, delay, etc.) but voices are independent |
| **Data parallelism width** | 16 voices | 16 independent voice streams could be processed in parallel |
| **Branch density in inner loop** | MEDIUM | Active/inactive check per voice, mode switching per block |
| **Dominant operations** | Composition/routing | Most time spent in sub-component processBlock calls, not in engine-level code |
| **Current CPU budget vs expected usage** | 10% budget vs ~5-8% expected | Comfortable headroom |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The RuinaeEngine is a composition layer, not a DSP algorithm. Its hot path is calling sub-component processBlock() methods and summing their outputs. The sub-components (RuinaeVoice internals: oscillators, filters, distortion) are where CPU time is actually spent, and those already have their own optimization strategies. The voice-summing loop (16 voices * N samples) could theoretically benefit from SIMD, but the summing itself is trivially cheap compared to voice processing. Optimization effort should target individual voice components, not the engine shell.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip inactive voices (FR-033) | ~50%+ for typical patches (4-6 voices active) | LOW | YES (already in spec) |
| Pre-compute pan gains once per block | Negligible (already planned) | LOW | YES |
| Early-exit when no voices active | Negligible | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer**:
- PolySynthEngine (spec 038): Already exists, uses SynthVoice
- Future engines for other Krate Audio plugins

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Voice panning + summing loop pattern | MEDIUM | Future synth engines | Keep local; extract if 2nd engine needs it |
| Master output stage (gain comp + soft limit + NaN flush) | MEDIUM | Future synth engines | Keep local; pattern is simple to replicate |
| RuinaeModDest enum pattern | LOW | Specific to Ruinae | Keep in ruinae_engine.h |
| VoiceMode enum | HIGH | Already shared from PolySynthEngine | Reuse existing (no new code) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared engine base class | Only 2 engines exist; too few to generalize. PolySynthEngine is mono, RuinaeEngine is stereo -- fundamentally different processing. |
| Keep stereo panning inline | Simple trigonometric calculation, only used by RuinaeEngine |
| Reuse VoiceMode from PolySynthEngine | Already a standalone enum, no duplication needed |

### Review Trigger

After implementing **Phase 7 Plugin Shell**, review this section:
- [ ] Does plugin shell need generic engine interface? Likely not (single engine per plugin).
- [ ] Any shared parameter forwarding patterns? Document if observed.

## Project Structure

### Documentation (this feature)

```text
specs/044-engine-composition/
    plan.md              # This file
    research.md          # Phase 0 output
    data-model.md        # Phase 1 output
    quickstart.md        # Phase 1 output
    contracts/           # Phase 1 output
        ruinae-engine-api.md
    tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
    include/krate/dsp/systems/
        ruinae_engine.h          # NEW: RuinaeEngine class + RuinaeModDest enum
        ruinae_voice.h           # MODIFIED: Add setOscAPhaseMode/setOscBPhaseMode forwarding
    tests/
        unit/systems/
            ruinae_engine_test.cpp              # NEW: Unit tests (all FR/SC)
            ruinae_engine_integration_test.cpp  # NEW: End-to-end MIDI-to-output tests
    CMakeLists.txt               # MODIFIED: Add test source files
```

### Test Architecture

**Unit Tests** (`ruinae_engine_test.cpp`):
- Tag: `[ruinae-engine]`
- Organized by user story and functional requirement
- Tests individual engine features in isolation

**Integration Tests** (`ruinae_engine_integration_test.cpp`):
- Tag: `[ruinae-engine-integration]`
- Tests the complete MIDI-to-stereo-output signal path
- Verifies all sub-components are correctly wired together
- Includes multi-sample-rate validation
- Includes CPU performance benchmarks

**Integration Test Categories** (per user request for extensive signal path coverage):

1. **T-INT-001: MIDI NoteOn -> Stereo Audio Output**
   - Send noteOn(60, 100), process 512 samples
   - Verify both L and R channels contain non-zero audio
   - Verify output contains frequency content near 261.6 Hz (C4)

2. **T-INT-002: MIDI Chord -> Polyphonic Stereo Mix**
   - Send noteOn for C major chord (60, 64, 67)
   - Process multiple blocks
   - Verify 3 active voices
   - Verify stereo content from all three notes

3. **T-INT-003: Full Signal Chain (NoteOn -> Osc -> Filter -> Distortion -> TranceGate -> VCA -> Pan -> Width -> GlobalFilter -> Effects -> Master)**
   - Configure non-default settings for every stage
   - Send noteOn, process blocks
   - Verify output differs from default configuration (effects are audible)

4. **T-INT-004: NoteOff -> Release -> Silence**
   - Send noteOn, process blocks, send noteOff
   - Process until output is silence (all samples < threshold)
   - Verify voice count returns to 0

5. **T-INT-005: Mono Legato Signal Path**
   - Set mono mode with legato enabled
   - Play overlapping notes
   - Verify single voice throughout
   - Verify no envelope retrigger on legato transitions

6. **T-INT-006: Portamento Pitch Glide**
   - Set mono mode with portamento = 100ms
   - Play note 60, then note 72
   - Measure frequency at midpoint
   - Verify it corresponds to approximately note 66 (~370 Hz)

7. **T-INT-007: Pitch Bend Through Full Chain**
   - Play a note, apply pitch bend +1.0
   - Verify output frequency shifts by pitch bend range (2 semitones)

8. **T-INT-008: Aftertouch -> Voice Modulation**
   - Configure per-voice route: Aftertouch -> FilterCutoff
   - Play note, set aftertouch = 0.8
   - Verify filter cutoff is modulated (output spectral content changes)

9. **T-INT-009: Global Modulation -> Filter Cutoff**
   - Set global route: LFO1 -> GlobalFilterCutoff
   - Enable global filter, play note
   - Process multiple blocks
   - Verify filter cutoff varies over blocks

10. **T-INT-010: Effects Chain Integration (Reverb Tail)**
    - Set reverb mix = 0.5, room size = 0.7
    - Play short note (noteOn + quick noteOff)
    - Process many blocks after release
    - Verify reverberant tail extends beyond voice release

11. **T-INT-011: Effects Chain Integration (Delay Echoes)**
    - Set delay type = Digital, delay mix = 0.5, delay time = 200ms
    - Play impulse-like note
    - Verify delayed echoes appear at expected time offset

12. **T-INT-012: Mode Switching Under Load**
    - Play chord in poly mode
    - Switch to mono mode
    - Verify most recent note continues, others released
    - Switch back to poly
    - Play new chord
    - Verify polyphonic behavior restored

13. **T-INT-013: Multi-Sample-Rate Validation**
    - For each of {44100, 48000, 88200, 96000, 176400, 192000}:
    - Prepare engine, play note, process block
    - Verify non-zero output

14. **T-INT-014: Soft Limiter Under Full Load**
    - Configure 16 voices, sawtooth oscillators, full velocity
    - Play 16 simultaneous notes
    - Verify all output samples in [-1.0, +1.0]

15. **T-INT-015: Voice Stealing Signal Path**
    - Configure 4 voices
    - Play 5 notes
    - Verify all 5 notes eventually sound
    - Verify voice stealing produces audio within same block

16. **T-INT-016: Gain Compensation Scaling**
    - Play same note at different polyphony counts (1, 2, 4, 8)
    - Measure RMS output
    - Verify RMS scales approximately as sqrt(N) relative to single voice

17. **T-INT-017: Tempo Sync Through Chain**
    - Set tempo 120 BPM
    - Enable tempo-synced LFO, trance gate, delay
    - Verify all components respond to tempo change to 140 BPM

18. **T-INT-018: CPU Performance Benchmark**
    - Configure 8 voices with full processing chain
    - Process 1 second of audio at 44100 Hz
    - Measure wall-clock time
    - Verify < 10% of single core

## Complexity Tracking

No constitution violations. No entries needed.
