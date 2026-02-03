# Synthesizer Techniques & Architecture

This document outlines a synthesizer architecture that leverages the novel DSP components documented in this project, including chaos-based oscillators, spectral morphing, and experimental distortion techniques.

## Concept: "Ruinae" - Chaos/Spectral Hybrid Synthesizer

A synthesizer built around controlled chaos and spectral manipulation, designed for evolving textures, aggressive leads, and experimental sound design.

### Signal Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                         VOICE ARCHITECTURE                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────┐     ┌─────────────┐                               │
│  │   OSC A     │     │   OSC B     │                               │
│  │ (Chaos/     │     │ (Particle/  │                               │
│  │  Classic)   │     │  Formant)   │                               │
│  └──────┬──────┘     └──────┬──────┘                               │
│         │                   │                                       │
│         └────────┬──────────┘                                       │
│                  ▼                                                  │
│         ┌───────────────────┐                                       │
│         │ SpectralMorphFilter│◄──── Morph Position (mod)           │
│         │  (Dual-Input)      │◄──── Morph Mode                     │
│         └─────────┬─────────┘                                       │
│                   ▼                                                  │
│         ┌───────────────────┐                                       │
│         │  Filter Section   │◄──── Cutoff, Resonance (mod)         │
│         │  (Ladder/SVF/     │                                       │
│         │   Formant/Comb)   │                                       │
│         └─────────┬─────────┘                                       │
│                   ▼                                                  │
│         ┌───────────────────┐                                       │
│         │ Distortion Section│◄──── Drive, Character (mod)          │
│         │  (Chaos/Spectral/ │                                       │
│         │   Granular)       │                                       │
│         └─────────┬─────────┘                                       │
│                   ▼                                                  │
│         ┌───────────────────┐                                       │
│         │       VCA         │◄──── Amp Envelope                    │
│         └─────────┬─────────┘                                       │
│                   │                                                  │
└───────────────────┼─────────────────────────────────────────────────┘
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         EFFECTS SECTION                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐              │
│  │  Spectral   │──▶│   Delay     │──▶│   Reverb    │──▶ OUTPUT    │
│  │   Freeze    │   │  (Multi-    │   │             │              │
│  │             │   │   Mode)     │   │             │              │
│  └─────────────┘   └─────────────┘   └─────────────┘              │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                      MODULATION SOURCES                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  │
│  │  ENV 1  │  │  ENV 2  │  │  LFO 1  │  │  LFO 2  │  │  Chaos  │  │
│  │ (ADSR)  │  │ (ADSR)  │  │         │  │         │  │ (Lorenz)│  │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  │
│       │            │            │            │            │        │
│       └────────────┴────────────┴────────────┴────────────┘        │
│                              │                                      │
│                              ▼                                      │
│                    ┌───────────────────┐                           │
│                    │ Modulation Matrix │                           │
│                    │  (Any → Any)      │                           │
│                    └───────────────────┘                           │
│                                                                     │
│  Additional Sources: Rungler, Sample & Hold, Velocity, Aftertouch  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Available Components (Already Implemented)

From **DSP-OSCILLATOR-TECHNIQUES.md**:
- **Chaos Attractors**: Lorenz, Rössler, Chua systems for unpredictable modulation/audio
- **Particle Oscillators**: Swarm-based synthesis with emergent behavior
- **Formant Oscillators**: Vocal-like timbres without samples
- **Glitch Oscillators**: Bit-crushing, sample-rate reduction, buffer manipulation
- **Rungler**: Shift-register chaos generator

From **DST-ROADMAP.md**:
- **Chaos Wavshapers**: Lorenz/Rössler-driven waveshaping
- **Spectral Distortion**: FFT-domain harmonic manipulation
- **Granular Distortion**: Micro-grain based saturation
- **Temporal Distortion**: Time-domain manipulation

From **080-spectral-morph-filter**:
- **SpectralMorphFilter**: Dual-input spectral morphing with multiple blend modes

From **Existing DSP Library**:
- LadderFilter, SVF, FormantFilter, CombFilter, CrossoverFilter
- EnvelopeFilter (auto-wah)
- Various delay modes and effects

---

## Missing Components

The following fundamental synthesizer components are not yet implemented and would be required:

### 1. ADSR Envelope Generator (Layer 1 - Primitives)

```cpp
namespace Krate::DSP {

struct ADSRParams {
    float attackMs{10.0f};      // 0.1 - 10000 ms
    float decayMs{100.0f};      // 0.1 - 10000 ms
    float sustainLevel{0.7f};   // 0.0 - 1.0
    float releaseMs{200.0f};    // 0.1 - 10000 ms
    float attackCurve{0.0f};    // -1 (log) to +1 (exp), 0 = linear
    float decayCurve{0.0f};
    float releaseCurve{0.0f};
};

class ADSREnvelope {
public:
    void prepare(double sampleRate);
    void setParams(const ADSRParams& params);

    void noteOn();              // Trigger attack
    void noteOff();             // Trigger release
    void reset();               // Immediate reset to idle

    float process();            // Returns envelope value [0, 1]

    bool isActive() const;      // True if not in idle state

    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage getStage() const;

private:
    Stage stage_{Stage::Idle};
    float currentValue_{0.0f};
    float attackCoeff_, decayCoeff_, releaseCoeff_;
    // ...
};

} // namespace Krate::DSP
```

**Implementation Notes:**
- Use exponential coefficients for smooth curves
- Support retriggering (restart attack from current value vs. from zero)
- Consider adding velocity scaling option

### 2. LFO (Layer 1 - Primitives)

```cpp
namespace Krate::DSP {

enum class LFOShape {
    Sine,
    Triangle,
    Saw,
    Square,
    SampleAndHold,
    Chaos       // Uses Lorenz attractor output
};

struct LFOParams {
    float rateHz{1.0f};         // 0.01 - 50 Hz (or tempo-synced)
    LFOShape shape{LFOShape::Sine};
    float depth{1.0f};          // Output scaling
    float phase{0.0f};          // Initial phase offset
    bool bipolar{true};         // -1..+1 vs 0..+1
    bool tempoSync{false};
    float noteDivision{1.0f};   // For tempo sync (1 = quarter note)
};

class LFO {
public:
    void prepare(double sampleRate);
    void setParams(const LFOParams& params);
    void setTempo(double bpm);

    float process();            // Returns LFO value
    void reset();               // Reset phase to initial
    void sync();                // Reset phase (for MIDI sync)

private:
    double phase_{0.0};
    double phaseIncrement_;
    // ...
};

} // namespace Krate::DSP
```

**Implementation Notes:**
- Band-limited for higher rates to avoid aliasing
- Support tempo sync with musical divisions (1/4, 1/8, dotted, triplet)
- Chaos mode pulls from Lorenz attractor for organic modulation

### 3. Voice Manager (Layer 3 - Systems)

```cpp
namespace Krate::DSP {

template<typename VoiceType, size_t MaxVoices = 16>
class VoiceManager {
public:
    void prepare(double sampleRate, int maxBlockSize);

    // Voice allocation
    void noteOn(int noteNumber, float velocity, int channel = 0);
    void noteOff(int noteNumber, int channel = 0);
    void allNotesOff();

    // Polyphony modes
    enum class Mode { Poly, Mono, Legato, Unison };
    void setMode(Mode mode);
    void setUnisonVoices(int count);    // For unison mode
    void setUnisonDetune(float cents);

    // Processing
    void process(float* left, float* right, int numSamples);

    // Voice stealing
    enum class StealMode { Oldest, Lowest, Highest, Quietest };
    void setStealMode(StealMode mode);

    // Access individual voices for parameter updates
    void forEachActiveVoice(std::function<void(VoiceType&)> fn);

private:
    std::array<VoiceType, MaxVoices> voices_;
    std::array<bool, MaxVoices> voiceActive_;
    // Voice allocation tracking...
};

} // namespace Krate::DSP
```

**Implementation Notes:**
- Template-based to work with any voice type
- Efficient voice stealing with multiple strategies
- Support mono/legato modes with portamento
- Unison mode with stereo spread

### 4. Modulation Matrix (Layer 3 - Systems)

```cpp
namespace Krate::DSP {

class ModulationMatrix {
public:
    static constexpr int MaxSources = 32;
    static constexpr int MaxDestinations = 64;
    static constexpr int MaxSlots = 32;

    // Source/destination registration
    int registerSource(const std::string& name);
    int registerDestination(const std::string& name, float minValue, float maxValue);

    // Modulation routing
    struct ModSlot {
        int sourceId{-1};
        int destinationId{-1};
        float amount{0.0f};     // Bipolar: -1 to +1
        int viaSourceId{-1};    // Optional: modulate the amount
    };

    void setSlot(int slotIndex, const ModSlot& slot);
    void clearSlot(int slotIndex);

    // Per-sample processing
    void updateSourceValue(int sourceId, float value);
    float getModulatedValue(int destinationId, float baseValue);

    // Block processing (more efficient)
    void processBlock(int numSamples);
    const float* getModulationBuffer(int destinationId);

private:
    // Optimized for real-time: no allocations, fixed-size buffers
    std::array<float, MaxSources> sourceValues_;
    std::array<ModSlot, MaxSlots> slots_;
    // ...
};

} // namespace Krate::DSP
```

**Implementation Notes:**
- Fixed-size, allocation-free for real-time safety
- Support "via" routing (modulation of modulation depth)
- Pre-calculate modulation per block for efficiency

### 5. MIDI Handler (Layer 3 - Systems)

```cpp
namespace Krate::DSP {

struct MIDIEvent {
    enum class Type { NoteOn, NoteOff, CC, PitchBend, Aftertouch, PolyAftertouch };
    Type type;
    int channel;
    int data1;      // Note number or CC number
    int data2;      // Velocity or CC value
    int sampleOffset;  // Sample-accurate timing within block
};

class MIDIHandler {
public:
    // Event processing
    void addEvent(const MIDIEvent& event);
    void processEvents(int blockSize);

    // Callbacks
    std::function<void(int note, float velocity)> onNoteOn;
    std::function<void(int note)> onNoteOff;
    std::function<void(int cc, float value)> onCC;
    std::function<void(float bend)> onPitchBend;  // -1 to +1
    std::function<void(float pressure)> onAftertouch;

    // State queries
    float getPitchBend() const;
    float getCC(int ccNumber) const;
    float getAftertouch() const;

    // MPE support
    void setMPEEnabled(bool enabled);

private:
    std::vector<MIDIEvent> eventQueue_;
    std::array<float, 128> ccValues_;
    float pitchBend_{0.0f};
    // ...
};

} // namespace Krate::DSP
```

### 6. Sample & Hold (Layer 1 - Primitives)

```cpp
namespace Krate::DSP {

class SampleAndHold {
public:
    void prepare(double sampleRate);

    // Trigger modes
    enum class TriggerMode {
        Clock,      // Regular interval
        Random,     // Random timing (controlled chaos)
        External    // External trigger input
    };
    void setMode(TriggerMode mode);
    void setRate(float rateHz);

    // Processing
    float process(float input);
    float process(float input, bool trigger);  // External trigger

    // Smoothing (for less stepped output)
    void setSlew(float slewMs);

private:
    float heldValue_{0.0f};
    float slewCoeff_;
    // ...
};

} // namespace Krate::DSP
```

### 7. Delay Effect (Layer 4 - Effects)

Already have delay infrastructure in Iterum plugin. Would need to extract/generalize for synth use:

```cpp
namespace Krate::DSP {

struct DelayParams {
    float timeMs{250.0f};
    float feedback{0.3f};
    float mix{0.5f};
    float stereoSpread{0.0f};
    bool tempoSync{false};
    float noteValue{1.0f};
    bool pingPong{false};
};

class StereoDelay {
public:
    void prepare(double sampleRate, int maxDelayMs = 2000);
    void setParams(const DelayParams& params);
    void setTempo(double bpm);

    void process(float& left, float& right);
    void processBlock(float* left, float* right, int numSamples);

private:
    // Uses existing DelayLine primitive
};

} // namespace Krate::DSP
```

### 8. Reverb (Layer 4 - Effects)

```cpp
namespace Krate::DSP {

struct ReverbParams {
    float roomSize{0.5f};       // 0-1
    float damping{0.5f};        // High frequency absorption
    float width{1.0f};          // Stereo width
    float mix{0.3f};            // Dry/wet
    float preDelay{20.0f};      // ms
    bool freeze{false};         // Infinite decay
};

class Reverb {
public:
    void prepare(double sampleRate);
    void setParams(const ReverbParams& params);

    void process(float& left, float& right);
    void processBlock(float* left, float* right, int numSamples);

private:
    // Freeverb-style or algorithmic implementation
    // Consider: FDN, Schroeder, Dattorro
};

} // namespace Krate::DSP
```

---

## Alternative Synthesizer Concepts

### 1. Particle Synthesizer
Focus on swarm-based oscillators with emergent behavior:
- Multiple particle oscillators with interaction forces
- Particle position → spectral centroid mapping
- Collision events trigger transients
- Gravity/attraction parameters for macro control

### 2. Formant Synthesizer
Speech/vocal synthesis focus:
- Formant oscillators as main sound source
- Vowel morphing via formant interpolation
- Consonant injection via noise + filtering
- Integration with SpectralMorphFilter for vowel blending

### 3. Glitch Synthesizer
Intentional digital artifacts as features:
- Glitch oscillator with controllable corruption
- Buffer-based effects (stutter, reverse, slice)
- Bit-depth and sample-rate as playable parameters
- "Stability" parameter (clean ↔ chaos)

### 4. Chaos Controller
Minimal synthesis, maximum modulation:
- Simple oscillator core (saw/square)
- Extensive chaos-based modulation (Lorenz, Rössler, Chua)
- Rungler-style shift register routing
- Every parameter modulatable by chaos

---

## Implementation Priority

### Phase 1: Core Synthesis (Essential)
1. **ADSREnvelope** - Required for any playable synth
2. **LFO** - Basic modulation capability
3. **VoiceManager** - Polyphony support

### Phase 2: Modulation & Control
4. **MIDIHandler** - VST3 MIDI event processing
5. **ModulationMatrix** - Flexible routing
6. **SampleAndHold** - Additional mod source

### Phase 3: Effects
7. **StereoDelay** - Extract from Iterum
8. **Reverb** - Essential for space/depth

### Phase 4: Advanced Features
9. **MPE Support** - Expressive control
10. **Microtuning** - Alternative scales
11. **Additional chaos sources** - Chua, double pendulum

---

## Integration with Existing Components

### Using SpectralMorphFilter in Synth Context

```cpp
// In synth voice
void SynthVoice::process(float* buffer, int numSamples) {
    // Generate both oscillators
    oscA_.processBlock(bufferA_, numSamples);
    oscB_.processBlock(bufferB_, numSamples);

    // Spectral morph between them
    spectralMorph_.setMorphPosition(morphEnv_.process());
    spectralMorph_.process(bufferA_, bufferB_, buffer, numSamples);

    // Continue through filter, distortion, VCA...
}
```

### Chaos Modulation Source

```cpp
// Lorenz attractor as modulation source
class ChaosMod {
    LorenzAttractor lorenz_;

    float process() {
        auto [x, y, z] = lorenz_.process();
        // Normalize and scale
        return (x / 30.0f);  // Lorenz x typically -20..+20
    }
};
```

---

## Design Principles

1. **Real-time Safe**: No allocations, locks, or exceptions in audio path
2. **Modular**: Each component usable standalone or composed
3. **Stateless Configuration**: Parameters via struct, state internal
4. **Block Processing**: Support both sample-by-sample and block modes
5. **Cross-Platform**: Pure C++, no platform-specific code in DSP
6. **Testable**: Every component independently testable

---

## References

- [DSP-OSCILLATOR-TECHNIQUES.md](DSP-OSCILLATOR-TECHNIQUES.md) - Novel oscillator implementations
- [DST-ROADMAP.md](DST-ROADMAP.md) - Distortion technique roadmap
- [080-spectral-morph-filter/spec.md](_archive_/080-spectral-morph-filter/spec.md) - Spectral morphing specification
- Project Constitution: `.specify/memory/constitution.md`
