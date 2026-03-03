# Data Model: Polyphonic Synth Engine

**Feature Branch**: `038-polyphonic-synth-engine` | **Date**: 2026-02-07

## Entities

### VoiceMode (FR-002)

```cpp
enum class VoiceMode : uint8_t {
    Poly = 0,   // Polyphonic: voices distributed via VoiceAllocator
    Mono = 1    // Monophonic: single voice via MonoHandler
};
```

**Location**: `dsp/include/krate/dsp/systems/poly_synth_engine.h`
**Namespace**: `Krate::DSP`
**Default**: `VoiceMode::Poly`

---

### PolySynthEngine (FR-001)

**Location**: `dsp/include/krate/dsp/systems/poly_synth_engine.h`
**Layer**: 3 (System)
**Namespace**: `Krate::DSP`

#### Constants (FR-003, FR-004)

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kMaxPolyphony` | `static constexpr size_t` | `16` | Maximum simultaneous voices |
| `kMinMasterGain` | `static constexpr float` | `0.0f` | Minimum master gain |
| `kMaxMasterGain` | `static constexpr float` | `2.0f` | Maximum master gain |

#### Sub-Components (Composition)

| Member | Type | Count | Purpose |
|--------|------|-------|---------|
| `voices_` | `std::array<SynthVoice, kMaxPolyphony>` | 16 | Voice pool (pre-allocated) |
| `allocator_` | `VoiceAllocator` | 1 | Polyphonic voice management |
| `monoHandler_` | `MonoHandler` | 1 | Monophonic mode handler |
| `noteProcessor_` | `NoteProcessor` | 1 | Pitch bend + velocity curves |
| `globalFilter_` | `SVF` | 1 | Post-mix filter |
| `scratchBuffer_` | `std::vector<float>` | 1 | Per-voice temp buffer |

#### State Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `mode_` | `VoiceMode` | `VoiceMode::Poly` | Current operation mode |
| `polyphonyCount_` | `size_t` | `8` | Configured polyphony (1-16) |
| `masterGain_` | `float` | `1.0f` | User master gain (0-2) |
| `gainCompensation_` | `float` | `0.354f` | Computed 1/sqrt(N) |
| `softLimitEnabled_` | `bool` | `true` | Enable tanh soft limiter |
| `globalFilterEnabled_` | `bool` | `false` | Enable post-mix filter |
| `sampleRate_` | `double` | `0.0` | Current sample rate |
| `prepared_` | `bool` | `false` | Has prepare() been called |
| `noteOnTimestamps_` | `std::array<uint64_t, kMaxPolyphony>` | `{0}` | Per-voice noteOn time |
| `timestampCounter_` | `uint64_t` | `0` | Monotonic counter |
| `monoVoiceNote_` | `int8_t` | `-1` | MIDI note of mono voice |

#### Lifecycle Methods

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| Constructor | `PolySynthEngine() noexcept` | N/A | Default init, no allocation |
| `prepare` | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | NO | Init all sub-components |
| `reset` | `void reset() noexcept` | YES | Clear all state to silent |

#### Note Dispatch Methods

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `noteOn` | `void noteOn(uint8_t note, uint8_t velocity) noexcept` | YES | Dispatch note-on |
| `noteOff` | `void noteOff(uint8_t note) noexcept` | YES | Dispatch note-off |
| `setPitchBend` | `void setPitchBend(float bipolar) noexcept` | YES | Forward pitch bend |

#### Configuration Methods

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `setPolyphony` | `void setPolyphony(size_t count) noexcept` | YES | Set voice count (1-16) |
| `setMode` | `void setMode(VoiceMode mode) noexcept` | YES | Switch poly/mono |
| `setMonoPriority` | `void setMonoPriority(MonoMode mode) noexcept` | YES | Forward to MonoHandler |
| `setLegato` | `void setLegato(bool enabled) noexcept` | YES | Forward to MonoHandler |
| `setPortamentoTime` | `void setPortamentoTime(float ms) noexcept` | YES | Forward to MonoHandler |
| `setPortamentoMode` | `void setPortamentoMode(PortaMode mode) noexcept` | YES | Forward to MonoHandler |
| `setAllocationMode` | `void setAllocationMode(AllocationMode mode) noexcept` | YES | Forward to VoiceAllocator |
| `setStealMode` | `void setStealMode(StealMode mode) noexcept` | YES | Forward to VoiceAllocator |
| `setPitchBendRange` | `void setPitchBendRange(float semitones) noexcept` | YES | Forward to NoteProcessor |
| `setTuningReference` | `void setTuningReference(float a4Hz) noexcept` | YES | Forward to NoteProcessor |
| `setVelocityCurve` | `void setVelocityCurve(VelocityCurve curve) noexcept` | YES | Forward to NoteProcessor |

#### Voice Parameter Forwarding Methods (FR-018)

> **Note**: The canonical API surface is defined in [contracts/poly_synth_engine_api.h](contracts/poly_synth_engine_api.h). The tables below are a summary for quick reference.

All forward to all 16 voices:

| Method | Forwards To |
|--------|-------------|
| `setOsc1Waveform(OscWaveform)` | `SynthVoice::setOsc1Waveform()` |
| `setOsc2Waveform(OscWaveform)` | `SynthVoice::setOsc2Waveform()` |
| `setOscMix(float)` | `SynthVoice::setOscMix()` |
| `setOsc2Detune(float)` | `SynthVoice::setOsc2Detune()` |
| `setOsc2Octave(int)` | `SynthVoice::setOsc2Octave()` |
| `setFilterType(SVFMode)` | `SynthVoice::setFilterType()` |
| `setFilterCutoff(float)` | `SynthVoice::setFilterCutoff()` |
| `setFilterResonance(float)` | `SynthVoice::setFilterResonance()` |
| `setFilterEnvAmount(float)` | `SynthVoice::setFilterEnvAmount()` |
| `setFilterKeyTrack(float)` | `SynthVoice::setFilterKeyTrack()` |
| `setAmpAttack(float)` | `SynthVoice::setAmpAttack()` |
| `setAmpDecay(float)` | `SynthVoice::setAmpDecay()` |
| `setAmpSustain(float)` | `SynthVoice::setAmpSustain()` |
| `setAmpRelease(float)` | `SynthVoice::setAmpRelease()` |
| `setAmpAttackCurve(EnvCurve)` | `SynthVoice::setAmpAttackCurve()` |
| `setAmpDecayCurve(EnvCurve)` | `SynthVoice::setAmpDecayCurve()` |
| `setAmpReleaseCurve(EnvCurve)` | `SynthVoice::setAmpReleaseCurve()` |
| `setFilterAttack(float)` | `SynthVoice::setFilterAttack()` |
| `setFilterDecay(float)` | `SynthVoice::setFilterDecay()` |
| `setFilterSustain(float)` | `SynthVoice::setFilterSustain()` |
| `setFilterRelease(float)` | `SynthVoice::setFilterRelease()` |
| `setFilterAttackCurve(EnvCurve)` | `SynthVoice::setFilterAttackCurve()` |
| `setFilterDecayCurve(EnvCurve)` | `SynthVoice::setFilterDecayCurve()` |
| `setFilterReleaseCurve(EnvCurve)` | `SynthVoice::setFilterReleaseCurve()` |
| `setVelocityToFilterEnv(float)` | `SynthVoice::setVelocityToFilterEnv()` |

#### Global Filter Methods (FR-019 through FR-021)

| Method | Signature | Description |
|--------|-----------|-------------|
| `setGlobalFilterEnabled` | `void setGlobalFilterEnabled(bool) noexcept` | Enable/disable |
| `setGlobalFilterCutoff` | `void setGlobalFilterCutoff(float hz) noexcept` | Set cutoff (20-20000 Hz) |
| `setGlobalFilterResonance` | `void setGlobalFilterResonance(float q) noexcept` | Set Q (0.1-30) |
| `setGlobalFilterType` | `void setGlobalFilterType(SVFMode mode) noexcept` | Set filter mode |

#### Master Output Methods (FR-022 through FR-025)

| Method | Signature | Description |
|--------|-----------|-------------|
| `setMasterGain` | `void setMasterGain(float gain) noexcept` | Set gain (0-2) |
| `setSoftLimitEnabled` | `void setSoftLimitEnabled(bool) noexcept` | Enable/disable limiter |

#### Processing Methods (FR-026 through FR-029)

| Method | Signature | Description |
|--------|-----------|-------------|
| `processBlock` | `void processBlock(float* output, size_t numSamples) noexcept` | Generate audio block |

#### Query Methods (FR-030, FR-031)

| Method | Signature | Description |
|--------|-----------|-------------|
| `getActiveVoiceCount` | `uint32_t getActiveVoiceCount() const noexcept` | Number of active voices |
| `getMode` | `VoiceMode getMode() const noexcept` | Current mode |

---

## Relationships

```
PolySynthEngine
  |
  |-- owns 16x SynthVoice (composition)
  |     |-- owns PolyBlepOscillator x2
  |     |-- owns SVF (per-voice filter)
  |     |-- owns ADSREnvelope x2
  |
  |-- owns 1x VoiceAllocator (composition)
  |     |-- produces VoiceEvent spans
  |
  |-- owns 1x MonoHandler (composition)
  |     |-- produces MonoNoteEvent
  |     |-- owns LinearRamp (portamento)
  |
  |-- owns 1x NoteProcessor (composition)
  |     |-- owns OnePoleSmoother (pitch bend)
  |
  |-- owns 1x SVF (global filter, composition)
  |
  |-- uses Sigmoid::tanh() (function call, no ownership)
```

---

## Signal Flow

```
noteOn/noteOff (from caller)
       |
       v
+-- VoiceMode check ---+
|                       |
v                       v
VoiceAllocator     MonoHandler
(poly mode)        (mono mode)
|                       |
v                       v
SynthVoice[0..N-1]  SynthVoice[0]
(each: osc->filter->ampEnv)
|                       |
+-------+-------+-------+
        |
        v
    Sum all active voice outputs
        |
        v
    Global Filter (if enabled)
        |
        v
    Master Gain * (1/sqrt(N))
        |
        v
    Soft Limiter (tanh, if enabled)
        |
        v
    Output buffer
```

---

## State Transitions

### VoiceMode

```
Poly <--setMode(Mono)--> Mono
  |                        |
  |  (default at start)    |
```

### Voice Lifecycle (within allocator)

```
Idle --[noteOn]--> Active --[noteOff]--> Releasing --[voiceFinished]--> Idle
                     |                       ^
                     +--[steal]--+-----------+
                                 |
                                 v
                              Active (new note)
```

---

## Validation Rules

| Parameter | Range | Clamp Behavior |
|-----------|-------|----------------|
| polyphony | [1, 16] | Clamp to bounds |
| masterGain | [0.0, 2.0] | Clamp to bounds |
| globalFilterCutoff | [20.0, 20000.0] | Clamp to bounds |
| globalFilterResonance | [0.1, 30.0] | Clamp to bounds |
| pitchBend (bipolar) | [-1.0, 1.0] | Forward to NoteProcessor (handles internally) |
| NaN/Inf inputs | Any setter | Silently ignored (parameter unchanged) |

---

## SynthVoice Modification Required

A new method must be added to `SynthVoice` (backwards-compatible):

```cpp
/// @brief Update oscillator frequencies without retriggering envelopes.
/// Used by PolySynthEngine for mono mode legato pitch changes.
/// @param hz New frequency in Hz
void setFrequency(float hz) noexcept {
    if (detail::isNaN(hz) || detail::isInf(hz)) return;
    noteFrequency_ = (hz < 0.0f) ? 0.0f : hz;
    osc1_.setFrequency(noteFrequency_);
    updateOsc2Frequency();
}
```

This method is distinct from `noteOn()` because it does NOT:
- Update velocity
- Gate envelopes (no `ampEnv_.gate(true)`)
- Reset any state

It ONLY updates the oscillator frequencies, preserving envelope continuity for legato transitions.
