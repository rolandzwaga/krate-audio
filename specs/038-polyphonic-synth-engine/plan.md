# Implementation Plan: Polyphonic Synth Engine

**Branch**: `038-polyphonic-synth-engine` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/038-polyphonic-synth-engine/spec.md`

## Summary

Implement a Layer 3 system `PolySynthEngine` class that composes existing components -- VoiceAllocator, SynthVoice (x16), MonoHandler, NoteProcessor, SVF (global filter), and Sigmoid::tanh (soft limiter) -- into a complete polyphonic synthesis engine with configurable polyphony (1-16), mono/poly mode switching, global filter, and master output with gain compensation and soft limiting. The engine is a pure orchestration layer that introduces no new DSP algorithms; it routes notes to voices, sums their output, and applies post-processing. A minor backwards-compatible addition (`setFrequency()`) is required on the existing SynthVoice class to support mono mode legato pitch changes.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: SynthVoice (L3), VoiceAllocator (L3), MonoHandler (L2), NoteProcessor (L2), SVF (L1), Sigmoid::tanh (L0), FastMath::fastTanh (L0), detail::isNaN/isInf (L0)
**Storage**: N/A (no persistent state)
**Testing**: Catch2 via dsp_tests target *(Constitution Principle VIII/XII)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo DSP library component
**Performance Goals**: <5% CPU @ 44.1kHz with 8 active voices (SC-001); <32KB memory footprint (SC-010)
**Constraints**: All methods except prepare() must be real-time safe (no alloc, no locks, no exceptions, no I/O). All methods noexcept. Header-only implementation per project convention.
**Scale/Scope**: 1 header file, 1 test file, 1 modified header (SynthVoice), 1 modified CMakeLists.txt

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP library component, no VST3 interaction |
| II. Real-Time Audio Thread Safety | PASS | All process methods noexcept, zero alloc after prepare() |
| III. Modern C++ Standards | PASS | C++20, RAII, std::array, no raw new/delete |
| IV. SIMD Optimization | PASS | Evaluated -- NOT BENEFICIAL (see analysis below) |
| V. VSTGUI Development | N/A | No UI component |
| VI. Cross-Platform Compatibility | PASS | Header-only, no platform-specific code |
| VII. Project Structure | PASS | Layer 3 in dsp/include/krate/dsp/systems/ |
| VIII. Testing Discipline | PASS | Catch2 unit tests, test-first workflow |
| IX. Layered Architecture | PASS | Layer 3, depends on L0/L1/L2/L3 only |
| X. DSP Processing Constraints | NOTE | Soft limiter uses tanh without oversampling -- justified as safety limiter, not creative distortion (see research.md RQ-3) |
| XI. Performance Budgets | PASS | Layer 3 budget <1%, full engine <5% |
| XII. Test-First Development | PASS | Tests written before implementation |
| XIII. Debugging Discipline | PASS | No framework work needed |
| XIV. Living Architecture | PASS | Will update architecture docs |
| XV. ODR Prevention | PASS | All names unique (see searches below) |
| XVI. Honest Completion | PASS | Compliance table verified at completion |

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

**Classes/Structs to be created**: PolySynthEngine, VoiceMode

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PolySynthEngine | `grep -r "class PolySynthEngine" dsp/ plugins/` | No | Create New |
| SynthEngine | `grep -r "class SynthEngine" dsp/ plugins/` | No | N/A (checked for conflicts) |
| VoiceMode | `grep -r "VoiceMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all logic is composition of existing APIs)

**Methods to add to existing classes**:

| Method | Target Class | Search | Existing? | Action |
|--------|-------------|--------|-----------|--------|
| `setFrequency(float)` | SynthVoice | Grep for setFrequency in synth_voice.h | No (only on oscillator subcomponents) | Add to SynthVoice |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SynthVoice | `dsp/include/krate/dsp/systems/synth_voice.h` | 3 | 16 instances in voice pool |
| VoiceAllocator | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | 1 instance for poly mode voice management |
| MonoHandler | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | 1 instance for mono mode |
| NoteProcessor | `dsp/include/krate/dsp/processors/note_processor.h` | 2 | 1 instance for pitch bend + velocity |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | 1 instance for global post-mix filter |
| Sigmoid::tanh() | `dsp/include/krate/dsp/core/sigmoid.h` | 0 | Soft limiter in master output |
| detail::isNaN/isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Parameter validation guards |
| VoiceEvent | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | Consumed from allocator |
| MonoNoteEvent | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | Consumed from mono handler |
| OscWaveform | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | Forwarded parameter type |
| SVFMode | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Filter mode config |
| EnvCurve | `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | Envelope curve config |
| AllocationMode | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | Forwarded config |
| StealMode | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | Forwarded config |
| MonoMode | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | Forwarded config |
| PortaMode | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | Forwarded config |
| VelocityCurve | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Forwarded config |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 DSP processors
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 system components
- [x] `dsp/include/krate/dsp/effects/` - Layer 4 effects (no conflict)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Both planned types (`PolySynthEngine`, `VoiceMode`) are unique and not found anywhere in the codebase. The only modification to existing code is adding a `setFrequency()` method to `SynthVoice`, which is a backwards-compatible addition that does not conflict with any existing method.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SynthVoice | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SynthVoice | reset | `void reset() noexcept` | Yes |
| SynthVoice | noteOn | `void noteOn(float frequency, float velocity) noexcept` | Yes |
| SynthVoice | noteOff | `void noteOff() noexcept` | Yes |
| SynthVoice | isActive | `[[nodiscard]] bool isActive() const noexcept` | Yes |
| SynthVoice | process | `[[nodiscard]] float process() noexcept` | Yes |
| SynthVoice | processBlock | `void processBlock(float* output, size_t numSamples) noexcept` | Yes |
| SynthVoice | setOsc1Waveform | `void setOsc1Waveform(OscWaveform waveform) noexcept` | Yes |
| SynthVoice | setOsc2Waveform | `void setOsc2Waveform(OscWaveform waveform) noexcept` | Yes |
| SynthVoice | setOscMix | `void setOscMix(float mix) noexcept` | Yes |
| SynthVoice | setOsc2Detune | `void setOsc2Detune(float cents) noexcept` | Yes |
| SynthVoice | setOsc2Octave | `void setOsc2Octave(int octave) noexcept` | Yes |
| SynthVoice | setFilterType | `void setFilterType(SVFMode type) noexcept` | Yes |
| SynthVoice | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` | Yes |
| SynthVoice | setFilterResonance | `void setFilterResonance(float q) noexcept` | Yes |
| SynthVoice | setFilterEnvAmount | `void setFilterEnvAmount(float semitones) noexcept` | Yes |
| SynthVoice | setFilterKeyTrack | `void setFilterKeyTrack(float amount) noexcept` | Yes |
| SynthVoice | setAmpAttack | `void setAmpAttack(float ms) noexcept` | Yes |
| SynthVoice | setAmpDecay | `void setAmpDecay(float ms) noexcept` | Yes |
| SynthVoice | setAmpSustain | `void setAmpSustain(float level) noexcept` | Yes |
| SynthVoice | setAmpRelease | `void setAmpRelease(float ms) noexcept` | Yes |
| SynthVoice | setAmpAttackCurve | `void setAmpAttackCurve(EnvCurve curve) noexcept` | Yes |
| SynthVoice | setAmpDecayCurve | `void setAmpDecayCurve(EnvCurve curve) noexcept` | Yes |
| SynthVoice | setAmpReleaseCurve | `void setAmpReleaseCurve(EnvCurve curve) noexcept` | Yes |
| SynthVoice | setFilterAttack | `void setFilterAttack(float ms) noexcept` | Yes |
| SynthVoice | setFilterDecay | `void setFilterDecay(float ms) noexcept` | Yes |
| SynthVoice | setFilterSustain | `void setFilterSustain(float level) noexcept` | Yes |
| SynthVoice | setFilterRelease | `void setFilterRelease(float ms) noexcept` | Yes |
| SynthVoice | setFilterAttackCurve | `void setFilterAttackCurve(EnvCurve curve) noexcept` | Yes |
| SynthVoice | setFilterDecayCurve | `void setFilterDecayCurve(EnvCurve curve) noexcept` | Yes |
| SynthVoice | setFilterReleaseCurve | `void setFilterReleaseCurve(EnvCurve curve) noexcept` | Yes |
| SynthVoice | setVelocityToFilterEnv | `void setVelocityToFilterEnv(float amount) noexcept` | Yes |
| VoiceAllocator | noteOn | `[[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| VoiceAllocator | noteOff | `[[nodiscard]] std::span<const VoiceEvent> noteOff(uint8_t note) noexcept` | Yes |
| VoiceAllocator | voiceFinished | `void voiceFinished(size_t voiceIndex) noexcept` | Yes |
| VoiceAllocator | setVoiceCount | `[[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept` | Yes |
| VoiceAllocator | setAllocationMode | `void setAllocationMode(AllocationMode mode) noexcept` | Yes |
| VoiceAllocator | setStealMode | `void setStealMode(StealMode mode) noexcept` | Yes |
| VoiceAllocator | setPitchBend | `void setPitchBend(float semitones) noexcept` | Yes |
| VoiceAllocator | getActiveVoiceCount | `[[nodiscard]] uint32_t getActiveVoiceCount() const noexcept` | Yes |
| VoiceAllocator | reset | `void reset() noexcept` | Yes |
| VoiceAllocator | getVoiceNote | `[[nodiscard]] int getVoiceNote(size_t voiceIndex) const noexcept` | Yes |
| MonoHandler | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| MonoHandler | reset | `void reset() noexcept` | Yes |
| MonoHandler | noteOn | `[[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept` | Yes |
| MonoHandler | noteOff | `[[nodiscard]] MonoNoteEvent noteOff(int note) noexcept` | Yes |
| MonoHandler | processPortamento | `[[nodiscard]] float processPortamento() noexcept` | Yes |
| MonoHandler | setMode | `void setMode(MonoMode mode) noexcept` | Yes |
| MonoHandler | setLegato | `void setLegato(bool enabled) noexcept` | Yes |
| MonoHandler | setPortamentoTime | `void setPortamentoTime(float ms) noexcept` | Yes |
| MonoHandler | setPortamentoMode | `void setPortamentoMode(PortaMode mode) noexcept` | Yes |
| MonoHandler | hasActiveNote | `[[nodiscard]] bool hasActiveNote() const noexcept` | Yes |
| NoteProcessor | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| NoteProcessor | reset | `void reset() noexcept` | Yes |
| NoteProcessor | setPitchBend | `void setPitchBend(float bipolar) noexcept` | Yes |
| NoteProcessor | processPitchBend | `[[nodiscard]] float processPitchBend() noexcept` | Yes |
| NoteProcessor | getFrequency | `[[nodiscard]] float getFrequency(uint8_t note) const noexcept` | Yes |
| NoteProcessor | mapVelocity | `[[nodiscard]] VelocityOutput mapVelocity(int velocity) const noexcept` | Yes |
| NoteProcessor | setPitchBendRange | `void setPitchBendRange(float semitones) noexcept` | Yes |
| NoteProcessor | setTuningReference | `void setTuningReference(float hz) noexcept` | Yes |
| NoteProcessor | setVelocityCurve | `void setVelocityCurve(VelocityCurve curve) noexcept` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| Sigmoid::tanh | tanh | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |
| detail::isNaN | isNaN | bit-pattern based, constexpr | Yes |
| detail::isInf | isInf | bit-pattern based, constexpr | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/synth_voice.h` - SynthVoice class
- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` - VoiceAllocator, VoiceEvent, VoiceState, AllocationMode, StealMode
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - MonoHandler, MonoNoteEvent, MonoMode, PortaMode
- [x] `dsp/include/krate/dsp/processors/note_processor.h` - NoteProcessor, VelocityOutput
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF, SVFMode, SVFOutputs
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanh
- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/core/midi_utils.h` - VelocityCurve, midiNoteToFrequency
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio, frequencyToMidiNote
- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - OscWaveform
- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - ADSREnvelope, ADSRStage
- [x] `dsp/include/krate/dsp/primitives/envelope_utils.h` - EnvCurve
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| MonoHandler | noteOn/noteOff take `int` not `uint8_t` | Cast: `monoHandler_.noteOn(static_cast<int>(note), static_cast<int>(velocity))` |
| NoteProcessor | mapVelocity takes `int` not `uint8_t` | Cast: `noteProcessor_.mapVelocity(static_cast<int>(velocity))` |
| VoiceAllocator | setVoiceCount returns events (excess voice NoteOff) | Must process returned events to noteOff excess voices |
| VoiceAllocator | setPitchBend takes semitones, not bipolar | Compute: `bipolar * pitchBendRange` before forwarding |
| SynthVoice | prepare takes `double` sampleRate | Use `double` not `float` |
| SynthVoice | noteOn takes `float frequency, float velocity` (not MIDI int) | Convert note to Hz, velocity 0-127 to 0.0-1.0 |
| SVF | kButterworthQ is `0.7071f` | Use `SVF::kButterworthQ` for default |
| SVF | processBlock modifies buffer in-place | Pass the output buffer directly |
| VoiceEvent::frequency | Exists for allocation decisions (e.g., highest-note stealing) | Do NOT use for voice pitch. Use `NoteProcessor::getFrequency(note)` instead -- it applies tuning reference + smoothed pitch bend. Update active voice frequencies per-block in processBlock. |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `updateGainCompensation()` | One-liner (1/sqrt(N)), only used internally |
| `dispatchPolyNoteOn()` | Engine-specific routing logic |
| `dispatchMonoNoteOn()` | Engine-specific routing logic |

**Decision**: No new Layer 0 utilities needed. All new code is orchestration/routing logic specific to the engine.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (per-voice) | Each SynthVoice has filter + envelope feedback loops |
| **Data parallelism width** | 1-16 voices | Independent voice processing, but each voice is serial internally |
| **Branch density in inner loop** | LOW | Main loop: `if (isActive()) processBlock()` |
| **Dominant operations** | Delegated to sub-components | Oscillator, filter, envelope processing inside SynthVoice |
| **Current CPU budget vs expected usage** | 5% budget, ~3-4% expected | Plenty of headroom for scalar |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The PolySynthEngine is an orchestration layer that delegates all compute-intensive work to SynthVoice::processBlock(). SIMD optimization at the engine level would only affect the voice output summation loop (adding float arrays), which is negligible compared to the per-voice processing. The per-voice processing cannot be SIMD-parallelized at this level because each SynthVoice has internal serial dependencies (filter state, envelope state). SIMD for individual sub-components (oscillator, filter) would be a separate spec targeting those Layer 1 primitives.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip inactive voices (FR-027) | ~50% when <half voices active | LOW | YES (already in spec) |
| Early-out when no voices active | ~100% when idle | LOW | YES |
| processBlock per voice (cache-friendly) | Already the pattern | N/A | Already done |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from synth-roadmap.md and known plans):
- FMVoice (spec 022): 4-operator FM synthesis voice
- UnisonEngine (spec 020): Supersaw unison oscillator system
- Future: Wavetable synth engine, additive synth engine

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| VoiceMode enum | MEDIUM | Future multi-mode synth engines | Keep local for now |
| Master output stage pattern (gain comp + soft limit) | LOW | Possibly other engines | Keep local |
| Parameter forwarding pattern | LOW | Pattern, not extractable code | Document pattern only |
| SynthVoice::setFrequency() | HIGH | Any system needing legato pitch | Already added to SynthVoice directly |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep VoiceMode in poly_synth_engine.h | Only one consumer; if a second engine needs it, extract then |
| No shared "EngineBase" class | Premature abstraction; FMVoice has fundamentally different voice structure |
| setFrequency() added to SynthVoice directly | Benefits any future consumer, backwards-compatible |

### Review Trigger

After implementing a **second synthesizer engine** (e.g., wavetable poly engine), review this section:
- [ ] Does the new engine need VoiceMode? If so, extract to a shared header
- [ ] Does the new engine use the same master output pattern? If so, extract MasterOutputStage
- [ ] Any duplicated note dispatch logic? Consider shared base or utility

## Project Structure

### Documentation (this feature)

```text
specs/038-polyphonic-synth-engine/
 ├── spec.md               # Feature specification
 ├── plan.md               # This file
 ├── research.md           # Phase 0 research findings
 ├── data-model.md         # Entity definitions and relationships
 ├── quickstart.md         # Build and usage guide
 ├── contracts/            # API contracts
 │   └── poly_synth_engine_api.h
 └── tasks.md              # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
 ├── include/krate/dsp/
 │   └── systems/
 │       ├── synth_voice.h          # MODIFIED: add setFrequency()
 │       └── poly_synth_engine.h    # NEW: main implementation
 └── tests/
     └── unit/systems/
         └── poly_synth_engine_test.cpp  # NEW: unit tests
```

**Structure Decision**: Header-only implementation in the KrateDSP shared library, following the existing pattern for Layer 3 system components. Tests added to the existing dsp_tests target.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Soft limiter without oversampling (Principle X) | Safety limiter on already-band-limited signal, not creative distortion | Oversampling would double CPU cost of entire output path for negligible quality improvement in a clipping prevention context |

## Implementation Task Outline

The following task groups will be detailed in `tasks.md` by `/speckit.tasks`:

### Task Group 0: SynthVoice Modification (Pre-requisite)
- Add `setFrequency(float hz)` to SynthVoice
- Add test for setFrequency (pitch change without envelope retrigger)
- Build and verify all existing SynthVoice tests still pass

### Task Group 1: Engine Construction and Defaults (FR-001 through FR-006)
- Write tests for construction, constants, prepare(), reset()
- Implement PolySynthEngine constructor and lifecycle methods
- Test default VoiceMode::Poly, default polyphony=8, default masterGain=1.0

### Task Group 2: Poly Mode Note Dispatch (FR-007, FR-008, FR-017, FR-026, FR-027)
- Write tests for noteOn/noteOff in poly mode (single note, chord, voice stealing)
- Implement poly mode dispatch through VoiceAllocator
- Write test for processBlock with active voices producing audio
- Write test for pitch bend affecting already-playing voices (per-block frequency update)
- Implement processBlock with voice summing and per-block pitch bend frequency updates

### Task Group 3: Voice Lifecycle (FR-028, FR-029, FR-030)
- Write tests for deferred voiceFinished notification
- Write test for getActiveVoiceCount
- Implement post-block lifecycle check

### Task Group 4: Polyphony Configuration (FR-012)
- Write tests for setPolyphony (increase, decrease, clamp)
- Implement setPolyphony with VoiceAllocator forwarding

### Task Group 5: Parameter Forwarding (FR-018)
- Write test: set waveform on engine, trigger note, verify sound character
- Write test: set filter cutoff, verify all 16 voices updated
- Implement all forwarding methods

### Task Group 6: Mono Mode (FR-009, FR-010, FR-011, FR-014)
- Write tests for mono noteOn/noteOff (retrigger, legato)
- Write test for portamento integration (per-sample frequency update)
- Implement mono mode dispatch through MonoHandler
- Implement processBlock mono portamento path

### Task Group 7: Mode Switching (FR-013)
- Write tests for poly->mono switch (most recent voice survives)
- Write tests for mono->poly switch
- Write test for same-mode no-op
- Implement setMode with voice transfer logic

### Task Group 8: Global Filter (FR-019, FR-020, FR-021)
- Write tests for filter enabled/disabled
- Write test for LP at 500Hz attenuating highs (SC-011)
- Implement global filter in processBlock

### Task Group 9: Master Output (FR-022, FR-023, FR-024, FR-025)
- Write tests for gain compensation (SC-005)
- Write tests for soft limiting (SC-003, SC-004)
- Implement master gain + compensation + soft limiter in processBlock

### Task Group 10: NoteProcessor Integration (FR-016, FR-017)
- Write tests for pitch bend (bipolar forwarding)
- Write tests for velocity curve mapping
- Implement NoteProcessor configuration forwarding

### Task Group 11: Voice Allocator Config (FR-015)
- Write tests for allocation mode and steal mode forwarding
- Implement forwarding methods

### Task Group 12: Edge Cases and Parameter Safety (FR-032, FR-033, FR-034)
- Write tests for NaN/Inf parameter rejection
- Write tests for velocity 0 as noteOff
- Write tests for prepare while playing, processBlock before prepare
- Write tests for all sample rates (SC-008)

### Task Group 13: Performance and Memory (SC-001, SC-010)
- Write CPU benchmark test (8 voices sawtooth at 44.1kHz)
- Write sizeof test for memory footprint
- Verify both meet spec thresholds

### Task Group 14: Finalization
- Run full test suite
- Update CMakeLists.txt with proper -fno-fast-math flags
- Run clang-tidy
- Update architecture documentation
