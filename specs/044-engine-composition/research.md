# Research: Ruinae Engine Composition

**Feature Branch**: `044-engine-composition`
**Date**: 2026-02-09

## Research Tasks

### R-001: Polyphonic Engine Composition Pattern

**Context**: The RuinaeEngine must compose 16 RuinaeVoice instances, a VoiceAllocator, MonoHandler, NoteProcessor, ModulationEngine, global filter, effects chain, and master output. The PolySynthEngine (spec 038) is the reference implementation.

**Decision**: Follow the PolySynthEngine pattern exactly, extending it with:
- Stereo output (PolySynthEngine is mono)
- RuinaeVoice instead of SynthVoice
- Global ModulationEngine integration
- RuinaeEffectsChain integration
- Stereo voice panning and width control
- Previous-output buffer for audio-reactive modulation

**Rationale**: The PolySynthEngine has been proven in production and establishes a clean composition pattern. The RuinaeEngine is a superset of this pattern. Reusing the same architecture minimizes risk and ensures consistency across engines.

**Alternatives Considered**:
- Inheriting from PolySynthEngine: Rejected because the engines compose different voice types (SynthVoice vs RuinaeVoice) and have fundamentally different processing pipelines (mono vs stereo, no effects vs effects chain). Inheritance would add complexity without benefit.
- Extracting a common base class: Premature abstraction with only 2 engines. Document the pattern instead.

### R-002: Stereo Voice Mixing with Equal-Power Pan Law

**Context**: Each RuinaeVoice outputs mono audio. The engine must pan each voice to a stereo position and sum into stereo mix buffers.

**Decision**: Use constant-power (equal-power) pan law: `L = cos(pan * pi/2)`, `R = sin(pan * pi/2)` where pan is in [0, 1] with 0.5 = center.

**Rationale**: Equal-power panning maintains consistent perceived loudness across pan positions. This is the industry standard for synthesizer voice panning. Linear panning would cause a -3 dB drop at center, which is unacceptable.

**Alternatives Considered**:
- Linear panning (`L = 1-pan, R = pan`): Rejected due to -3 dB center drop.
- -4.5 dB center (compromise): Rejected because equal-power is the standard for synth voice panning.

### R-003: Voice Pan Position Calculation

**Context**: Voice pan positions depend on voice index, polyphony count, and stereo spread parameter.

**Decision**: Formula: `panPosition = 0.5 + (voiceIndex / (polyphonyCount - 1) - 0.5) * spread` for polyphonyCount > 1. For polyphonyCount == 1, always 0.5 (center).

**Rationale**: This distributes voices evenly across the stereo field. At spread=0, all voices are centered. At spread=1, voices span the full stereo panorama. The formula is simple, cache-friendly (pre-computed per voice), and matches industry practice.

**Alternatives Considered**:
- Random pan positions: Rejected because they cause inconsistent stereo image.
- Modulation-driven panning: Deferred to future work; static positions per voice are sufficient for Phase 6.

### R-004: Global Modulation Engine Integration

**Context**: The global ModulationEngine must be processed once per block before voice processing. It receives audio input from the previous block's output.

**Decision**:
- Maintain `previousOutputL_` and `previousOutputR_` buffers (pre-allocated at `prepare()` time).
- Feed these to `ModulationEngine::process()` as audio input.
- After master output processing, copy current output to previous buffers.
- Use `ModulationEngine::getModulationOffset(destParamId)` to read offsets.
- Map `RuinaeModDest` enum values to `uint32_t` destParamIds starting at offset 64 (well above any voice-level mod destination IDs).

**Rationale**: One-block latency (~11ms at 512 samples/44.1kHz) is imperceptible for modulation. Pre-allocated buffers ensure zero allocations at runtime. Offset 64 avoids collision with voice-level modulation IDs.

**Alternatives Considered**:
- Zero-latency feedback: Would require processing modulation mid-voice-loop, breaking the clean separation between modulation and voice processing.
- Sidechain input: Over-engineered for self-modulation.

### R-005: RuinaeModDest Enum Design

**Context**: Internal enum for global modulation destinations that keeps the DSP library independent of plugin-level parameter IDs.

**Decision**: Define `enum class RuinaeModDest : uint32_t` starting at value 64:
- GlobalFilterCutoff = 64
- GlobalFilterResonance = 65
- MasterVolume = 66
- EffectMix = 67
- AllVoiceFilterCutoff = 68
- AllVoiceMorphPosition = 69
- AllVoiceTranceGateRate = 70

**Rationale**: Starting at 64 avoids collision with per-voice VoiceModDest values (which are 0-8). The ModulationEngine uses uint32_t destParamIds and supports up to 128 destinations (kMaxModDestinations).

**Alternatives Considered**:
- Starting at 0: Would collide with voice-level mod destinations if both engines use the same ModulationEngine.
- Starting at 1000: Unnecessary large gap; 64 provides sufficient separation.

### R-006: Master Output Stage Design

**Context**: The master output must apply gain compensation, soft limiting, and NaN/Inf flushing.

**Decision**:
- Gain compensation: `effectiveGain = masterGain * (1.0 / sqrt(polyphonyCount))`
- Soft limiting: `Sigmoid::tanh(x)` per sample when enabled
- NaN/Inf flush: Check with `detail::isNaN()`/`detail::isInf()` and replace with 0.0f

**Rationale**: 1/sqrt(N) compensation is scientifically grounded (uncorrelated signals sum in power). tanh is the industry standard for soft limiting. Post-limiter NaN/Inf flush is a safety net.

**Alternatives Considered**:
- 1/N compensation: Too aggressive; causes quiet output with high polyphony.
- Clipper (hard limit): Harsh; tanh provides musical saturation.

### R-007: Mode Switching Safety

**Context**: Switching between Poly and Mono modes must not produce audio discontinuities.

**Decision**: Follow the PolySynthEngine pattern:
- Poly-to-Mono: Find most recently triggered voice (via timestamps), release all others. If not at index 0, release all and restart voice 0 with the most recent note. Initialize MonoHandler with surviving note.
- Mono-to-Poly: Voice 0 continues unchanged. Reset MonoHandler. Subsequent notes go through VoiceAllocator.

**Rationale**: This is the proven pattern from PolySynthEngine. The "voice 0 continues" approach for Mono-to-Poly minimizes discontinuities because the currently sounding voice is never interrupted.

**Alternatives Considered**:
- Crossfading during mode switch: Over-engineered; the current pattern is already glitch-free (SC-007 < -40 dBFS).

### R-008: RuinaeVoice API Compatibility

**Context**: The RuinaeEngine must compose 16 RuinaeVoice instances. Need to verify the API is compatible.

**Decision**: RuinaeVoice provides the exact API needed:
- `prepare(double sampleRate, size_t maxBlockSize)` -- note: takes 2 parameters (unlike SynthVoice which takes only sampleRate)
- `reset()`
- `noteOn(float frequency, float velocity)`
- `noteOff()`
- `setFrequency(float hz)`
- `isActive()` -- based on ampEnv_.isActive()
- `processBlock(float* output, size_t numSamples)` -- mono output
- All parameter setters (osc types, filter, distortion, trance gate, envelopes, etc.)
- `setAftertouch(float value)` -- for per-voice aftertouch forwarding
- Non-copyable, movable (due to SelectableOscillator)

**Rationale**: Direct verification against the header file confirms full API compatibility. The 2-parameter prepare() requires the engine to pass maxBlockSize to each voice.

### R-009: RuinaeEffectsChain API Compatibility

**Context**: The engine composes 1 RuinaeEffectsChain instance for stereo effects.

**Decision**: RuinaeEffectsChain provides:
- `prepare(double sampleRate, size_t maxBlockSize)`
- `reset()`
- `processBlock(float* left, float* right, size_t numSamples)` -- stereo in-place
- `setDelayType(RuinaeDelayType)`, `setDelayTime(float ms)`, `setDelayFeedback(float)`, `setDelayMix(float)`
- `setFreezeEnabled(bool)`, `setFreeze(bool)`, `setFreezePitchSemitones(float)`, `setFreezeShimmerMix(float)`, `setFreezeDecay(float)`
- `setReverbParams(const ReverbParams&)`
- `getLatencySamples()` -- returns worst-case latency
- Note: `setDelayTempo(double bpm)` exists for tempo forwarding

**Rationale**: Direct verification against the header confirms all pass-through methods are available.

### R-010: Integration Test Strategy (MIDI-to-Output)

**Context**: User requires extensive integration tests covering the full MIDI-to-output signal path.

**Decision**: Structure integration tests as end-to-end signal path tests:

1. **MIDI Note -> Voice Output**: Send noteOn, process block, verify non-zero stereo output
2. **MIDI Chord -> Polyphonic Mix**: Send multiple noteOn, verify stereo content from all notes
3. **MIDI NoteOff -> Release -> Silence**: Send noteOff, process until silence
4. **MIDI -> Mono Mode -> Legato**: Test mono mode with overlapping notes
5. **MIDI -> Portamento -> Frequency Glide**: Verify smooth pitch transition
6. **MIDI -> Pitch Bend -> Frequency Shift**: Verify pitch bend affects all voices
7. **MIDI -> Aftertouch -> Voice Modulation**: Verify aftertouch forwarding
8. **Full Chain**: noteOn -> voice -> stereo pan -> width -> global filter -> effects -> master -> output
9. **Mode Switching Under Load**: Switch poly/mono with active voices
10. **Tempo Sync**: Verify tempo propagation to all components
11. **Multiple Sample Rates**: Verify at 44100, 48000, 96000, 192000

**Rationale**: Integration tests catch composition errors that unit tests miss. Testing the full signal path from MIDI event to stereo output ensures all components are correctly wired together.

### R-011: RuinaeVoice Array Storage

**Context**: RuinaeVoice is non-copyable and movable due to SelectableOscillator internals (unique_ptr members). Need to store 16 instances.

**Decision**: Use a helper approach for the array. Since RuinaeVoice is default-constructible and move-assignable, `std::array<RuinaeVoice, 16>` works because std::array value-initializes elements which calls the default constructor. No explicit initialization loop needed for construction.

**Rationale**: Verified that `RuinaeVoice() noexcept = default` is declared. Default constructor is noexcept. std::array of 16 default-constructed RuinaeVoice instances is valid.

**Alternatives Considered**:
- unique_ptr array: Adds indirection and heap allocation per voice. Rejected.
- std::vector: Would require move-constructing elements. Rejected for fixed-size data.
