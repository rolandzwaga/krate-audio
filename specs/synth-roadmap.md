# VST Synth Building Blocks Roadmap

**Status**: Planning | **Last Updated**: 2026-02-05

This document tracks the missing DSP components needed to build a complete VST synthesizer using the existing KrateDSP library.

---

## Current State Summary

The KrateDSP library has strong foundations for synthesis:
- **Oscillators**: PolyBLEP, Wavetable, Noise, Phase Distortion, Additive, FM Operator, Sync, Sub
- **Filters**: Biquad, SVF, Ladder (Moog), Multimode, Formant, Self-Oscillating
- **Modulation**: LFO, ModulationMatrix, ModulationEngine, EnvelopeFollower, Chaos
- **Systems**: UnisonEngine (supersaw), FMVoice (4-op FM)

**Gap**: No envelope generators, voice management, or note handling infrastructure.

---

## Phase 1: Core Envelope (Critical Path)

### 1.1 ADSR Envelope Generator
**Layer**: 1 (Primitive) | **Priority**: P0 | **Estimate**: 1 spec

The fundamental missing piece. Required for amplitude gating, filter modulation, and general-purpose modulation.

**Requirements**:
- Standard ADSR stages with time-based parameters (ms)
- Exponential and linear curve options per stage
- Gate input (note on/off)
- Retrigger behavior (hard reset vs legato)
- Real-time safe parameter changes
- Optional velocity scaling of attack/decay/sustain

**API Sketch**:
```cpp
class ADSREnvelope {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Trigger control
    void gate(bool on) noexcept;           // Note on/off
    void retrigger() noexcept;             // Force restart from attack

    // Parameters (all times in ms)
    void setAttack(float ms) noexcept;     // 0.1 - 10000
    void setDecay(float ms) noexcept;      // 0.1 - 10000
    void setSustain(float level) noexcept; // 0.0 - 1.0
    void setRelease(float ms) noexcept;    // 0.1 - 10000

    // Curve shapes
    void setAttackCurve(EnvCurve curve) noexcept;
    void setDecayCurve(EnvCurve curve) noexcept;
    void setReleaseCurve(EnvCurve curve) noexcept;

    // Processing
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // State queries
    [[nodiscard]] EnvelopeStage getStage() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;  // Not idle
    [[nodiscard]] bool isReleasing() const noexcept;
};

enum class EnvelopeStage : uint8_t { Idle, Attack, Decay, Sustain, Release };
enum class EnvCurve : uint8_t { Linear, Exponential, Logarithmic };
```

**Dependencies**: Layer 0 only (math utilities)

**Unlocks**: Filter envelope, amplitude envelope, mod envelope, voice release detection

**status** Finished

---

### 1.2 Multi-Stage Envelope (Optional Enhancement)
**Layer**: 2 (Processor) | **Priority**: P2 | **Estimate**: 1 spec

Extended envelope for complex modulation (Korg MS-20 style, looping envelopes).

**Requirements**:
- Configurable number of stages (4-8)
- Loop points (for LFO-like behavior)
- Per-stage time and level
- Sustain point selection

**status** Finished

---

## Phase 2: Voice Management

### 2.1 Voice Allocator
**Layer**: 3 (System) | **Priority**: P0 | **Estimate**: 1 spec

Core polyphonic voice management without owning the actual voice DSP.

**Requirements**:
- Configurable voice count (1-32)
- Allocation modes: Round-robin, oldest, lowest-velocity, highest-note
- Voice stealing with release tail option
- Note tracking (which voice plays which note)
- Unison mode support (multiple voices per note)

**API Sketch**:
```cpp
struct VoiceEvent {
    enum Type : uint8_t { NoteOn, NoteOff, Steal };
    Type type;
    uint8_t voiceIndex;
    uint8_t note;
    uint8_t velocity;
    float frequency;  // Pre-computed from note + pitch bend
};

class VoiceAllocator {
    void setVoiceCount(size_t count) noexcept;
    void setAllocationMode(AllocationMode mode) noexcept;
    void setUnisonCount(size_t count) noexcept;

    // Returns events for voices that need action
    [[nodiscard]] std::span<VoiceEvent> noteOn(uint8_t note, uint8_t velocity) noexcept;
    [[nodiscard]] std::span<VoiceEvent> noteOff(uint8_t note) noexcept;

    // Query state
    [[nodiscard]] size_t getActiveVoiceCount() const noexcept;
    [[nodiscard]] bool isVoiceActive(size_t index) const noexcept;

    // Voice reports back when envelope completes
    void voiceFinished(size_t index) noexcept;
};

enum class AllocationMode : uint8_t { RoundRobin, Oldest, LowestVelocity, HighestNote };
```

**Dependencies**: Layer 0 only

**Unlocks**: Polyphonic synth capability

**status** Finished

---

### 2.2 Mono/Legato Handler
**Layer**: 2 (Processor) | **Priority**: P1 | **Estimate**: 1 spec

Monophonic note handling with legato and portamento.

**Requirements**:
- Last-note priority (standard mono behavior)
- Low-note / high-note priority options
- Legato mode (no retrigger on overlapping notes)
- Portamento with time control
- Note stack for release handling

**API Sketch**:
```cpp
struct MonoNoteEvent {
    float frequency;
    uint8_t velocity;
    bool retrigger;      // false in legato mode for tied notes
    bool isNoteOn;
};

class MonoHandler {
    void prepare(double sampleRate) noexcept;

    void setMode(MonoMode mode) noexcept;  // LastNote, LowNote, HighNote
    void setLegato(bool enabled) noexcept;
    void setPortamentoTime(float ms) noexcept;  // 0 = instant
    void setPortamentoMode(PortaMode mode) noexcept;  // Always, Legato

    [[nodiscard]] MonoNoteEvent noteOn(uint8_t note, uint8_t velocity) noexcept;
    [[nodiscard]] MonoNoteEvent noteOff(uint8_t note) noexcept;

    // Call per sample to get gliding frequency
    [[nodiscard]] float processPortamento() noexcept;
    [[nodiscard]] float getCurrentFrequency() const noexcept;
    [[nodiscard]] bool hasActiveNote() const noexcept;
};

enum class MonoMode : uint8_t { LastNote, LowNote, HighNote };
enum class PortaMode : uint8_t { Always, LegatoOnly };
```

**Dependencies**: Layer 0 (pitch_utils), Layer 1 (smoother)

---

### 2.3 Note Event Processor
**Layer**: 2 (Processor) | **Priority**: P1 | **Estimate**: 1 spec

MIDI note processing with pitch bend and velocity mapping.

**Requirements**:
- Note to frequency conversion (with tuning reference)
- Pitch bend range configuration (semitones)
- Pitch bend smoothing
- Velocity curves (linear, exponential, fixed)
- Velocity to multiple destinations (amp, filter, env time)

**API Sketch**:
```cpp
class NoteProcessor {
    void prepare(double sampleRate) noexcept;

    // Tuning
    void setTuningReference(float a4Hz) noexcept;  // Default 440
    void setPitchBendRange(float semitones) noexcept;  // Default ±2

    // Real-time pitch bend
    void setPitchBend(float bipolar) noexcept;  // -1 to +1
    [[nodiscard]] float getFrequency(uint8_t note) const noexcept;

    // Velocity mapping
    void setVelocityCurve(VelocityCurve curve) noexcept;
    [[nodiscard]] float mapVelocity(uint8_t velocity) const noexcept;
};

enum class VelocityCurve : uint8_t { Linear, Soft, Hard, Fixed };
```

**Dependencies**: Layer 0 (pitch_utils), Layer 1 (smoother)

---

## Phase 3: Synth Voice Composition

### 3.1 Basic Synth Voice
**Layer**: 3 (System) | **Priority**: P1 | **Estimate**: 1 spec

Complete single voice composing oscillator(s) + filter + envelopes.

**Requirements**:
- 2 oscillators with mix/detune
- 1 multimode filter
- 2 ADSR envelopes (amp + filter)
- Velocity → filter env amount
- Velocity → amplitude

**API Sketch**:
```cpp
class SynthVoice {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note control
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    [[nodiscard]] bool isActive() const noexcept;

    // Oscillators
    void setOsc1Waveform(OscWaveform wf) noexcept;
    void setOsc2Waveform(OscWaveform wf) noexcept;
    void setOscMix(float mix) noexcept;  // 0 = osc1, 1 = osc2
    void setOsc2Detune(float cents) noexcept;
    void setOsc2Octave(int octave) noexcept;  // -2 to +2

    // Filter
    void setFilterType(SVFMode type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float amount) noexcept;  // bipolar
    void setFilterKeyTrack(float amount) noexcept;

    // Envelopes
    ADSREnvelope& getAmpEnvelope() noexcept;
    ADSREnvelope& getFilterEnvelope() noexcept;

    // Processing
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Dependencies**: Layer 1 (PolyBlepOscillator, ADSREnvelope, SVF), Layer 2 (MultimodeFilter)

---

### 3.2 Polyphonic Synth Engine
**Layer**: 3 (System) | **Priority**: P2 | **Estimate**: 1 spec

Complete polyphonic engine composing VoiceAllocator + SynthVoice pool.

**Requirements**:
- Configurable polyphony (1-16 voices)
- Mono/poly mode switching
- Global filter option
- Master output with soft limiting

---

## Phase 4: Extended Features (Future)

### 4.1 MPE Support
**Layer**: 2 | **Priority**: P3

Per-note expression (pitch bend, pressure, slide).

### 4.2 Arpeggiator
**Layer**: 3 | **Priority**: P3

Pattern-based note generation with sync.

### 4.3 Chord Memory
**Layer**: 2 | **Priority**: P3

Transposable chord patterns.

### 4.4 Microtuning
**Layer**: 0 | **Priority**: P3

Scala file support, custom tuning tables.

---

## Dependency Graph

```
                    ┌─────────────────────┐
                    │  Polyphonic Synth   │  (Phase 3.2)
                    │       Engine        │
                    └──────────┬──────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              ▼                ▼                ▼
    ┌─────────────────┐ ┌─────────────┐ ┌─────────────────┐
    │  Voice          │ │ Synth Voice │ │  Mono/Legato    │
    │  Allocator      │ │  (Phase 3.1)│ │  Handler        │
    │  (Phase 2.1)    │ └──────┬──────┘ │  (Phase 2.2)    │
    └─────────────────┘        │        └────────┬────────┘
                               │                 │
         ┌─────────────────────┼─────────────────┤
         │                     │                 │
         ▼                     ▼                 ▼
    ┌──────────┐      ┌────────────────┐  ┌────────────┐
    │ Note     │      │ ADSR Envelope  │  │ Portamento │
    │ Processor│      │  (Phase 1.1)   │  │ (smoother) │
    │ (2.3)    │      └────────────────┘  └────────────┘
    └──────────┘              │
         │                    │
         ▼                    ▼
    ┌──────────────────────────────────────────────────┐
    │              Existing Components                 │
    │  PolyBlepOsc, SVF, Ladder, LFO, ModMatrix, etc. │
    └──────────────────────────────────────────────────┘
```

---

## Implementation Order

| Order | Component | Layer | Blocks | Est. Complexity |
|-------|-----------|-------|--------|-----------------|
| 1 | ADSR Envelope | L1 | — | Medium |
| 2 | Voice Allocator | L3 | — | Medium |
| 3 | Mono/Legato Handler | L2 | — | Low |
| 4 | Note Event Processor | L2 | — | Low |
| 5 | Basic Synth Voice | L3 | 1 | Medium |
| 6 | Polyphonic Synth Engine | L3 | 2, 5 | High |

**Critical path**: ADSR → Voice Allocator → Synth Voice → Poly Engine

---

## References

- Existing specs: `specs/015-polyblep-oscillator/`, `specs/020-supersaw-unison-engine/`, `specs/022-fm-voice-system/`
- Architecture: `specs/_architecture_/layer-1-primitives.md`, `layer-3-systems.md`
- Constitution: `.specify/memory/constitution.md`
