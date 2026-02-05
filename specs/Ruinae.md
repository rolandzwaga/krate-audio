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
│         │   Trance Gate     │◄──── Pattern, Rate, Depth (mod)      │
│         │  (Rhythmic VCA)   │                                       │
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

### 7. Trance Gate (Layer 2 - Voice Processor)

A tempo-synchronized rhythmic amplitude modulator placed post-distortion and pre-VCA, used to impose macro-rhythmic structure on evolving or chaotic signals.

**Characteristics:**
- Acts as a shaped VCA (not a hard mute)
- Pattern-based (step sequencer or binary mask)
- Smooth ramps to avoid clicks
- Tempo-sync and free-run modes
- Can also function as a modulation source

**Primary Use Cases:**
- Rhythmic stabilization of chaotic oscillators
- Gated distortion textures
- Spectral motion accentuation
- Polyrhythmic amplitude articulation

**Design Note:**
The Trance Gate is intentionally placed after nonlinear processing stages to avoid destabilizing distortion and filter behavior. It imposes rhythmic structure without interfering with internal chaotic dynamics.

```cpp
namespace Krate::DSP {

enum class GatePattern : uint8_t {
    Binary16,       // 16-step on/off pattern
    Euclidean,      // Euclidean rhythm generator
    Probability,    // Per-step probability
    Custom          // User-defined pattern
};

struct TranceGateParams {
    GatePattern pattern{GatePattern::Binary16};
    uint16_t steps{0b1010101010101010};  // Binary pattern (16 bits)
    int numSteps{16};                     // Active steps (1-32)
    float rateHz{4.0f};                   // Free-run rate
    float depth{1.0f};                    // 0 = bypass, 1 = full gate
    float attackMs{1.0f};                 // Ramp up time per step
    float releaseMs{10.0f};               // Ramp down time per step
    float smoothing{0.5f};                // Edge smoothness (0 = hard, 1 = soft)
    bool tempoSync{true};                 // Sync to host tempo
    float noteValue{0.25f};               // Step length (1 = quarter note)

    // Euclidean parameters
    int euclideanHits{4};                 // Number of hits
    int euclideanRotation{0};             // Pattern rotation

    // Probability parameters
    float probability{0.8f};              // Per-step trigger probability
};

class TranceGate {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setParams(const TranceGateParams& params) noexcept;
    void setTempo(double bpm) noexcept;

    // Pattern control
    void setPattern(uint32_t pattern, int numSteps) noexcept;
    void setEuclidean(int hits, int steps, int rotation = 0) noexcept;
    void setStep(int stepIndex, bool on) noexcept;

    // Real-time parameter changes
    void setDepth(float depth) noexcept;
    void setRate(float rateHz) noexcept;
    void setNoteValue(float noteValue) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Stereo processing (applies same gate to both channels)
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // Modulation output (current gate envelope value)
    [[nodiscard]] float getGateValue() const noexcept;

    // State queries
    [[nodiscard]] int getCurrentStep() const noexcept;
    [[nodiscard]] bool isStepActive() const noexcept;

private:
    double sampleRate_{44100.0};
    double phase_{0.0};
    double phaseIncrement_{0.0};
    int currentStep_{0};
    float gateEnvelope_{0.0f};
    float attackCoeff_{0.0f};
    float releaseCoeff_{0.0f};

    TranceGateParams params_;
    uint32_t patternMask_{0xFFFF};

    // Euclidean pattern generation
    [[nodiscard]] static uint32_t generateEuclidean(int hits, int steps, int rotation) noexcept;
};

} // namespace Krate::DSP
```

**Implementation Notes:**
- Uses exponential coefficients for smooth attack/release ramps
- Euclidean pattern generated via Bjorklund algorithm
- Gate envelope output useful as modulation source for filter cutoff, etc.
- Probability mode uses internal PRNG for deterministic-per-seed randomness
- Tempo sync uses BlockContext for sample-accurate beat tracking

### 9. Delay Effect (Layer 4 - Effects)

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

### 10. Reverb (Layer 4 - Effects)

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
7. **TranceGate** - Rhythmic amplitude modulation, chaos stabilization

### Phase 3: Effects
8. **StereoDelay** - Extract from Iterum
9. **Reverb** - Essential for space/depth

### Phase 4: Advanced Features
10. **MPE Support** - Expressive control
11. **Microtuning** - Alternative scales
12. **Additional chaos sources** - Chua, double pendulum

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

# Trance Gate – Functional Specification

### Conceptual role

The trance gate is a **rhythmic energy shaper**, not a hard mute.
It operates as a **pattern-driven, shaped VCA** placed **post-distortion, pre-VCA**, imposing macro-rhythm while preserving internal chaos.

Key idea:

> *It should never sound like the signal is being chopped by a knife.*

---

## 1. Core Signal Model

The gate outputs a gain signal `g(t)` applied multiplicatively:

```
y(t) = x(t) * g(t)
```

Where:

* `x(t)` is the post-distortion audio signal
* `g(t)` ∈ [0, 1] (optionally >1 later for accenting)

No hard zeros unless explicitly requested.

---

## 2. Pattern Engine

### Pattern Representation

* Fixed-length step pattern (initially):

  * 8, 16, or 32 steps
* Each step stores:

  ```cpp
  struct GateStep {
      float level;   // 0.0 – 1.0 (not boolean)
  };
  ```

This immediately enables:

* Classic on/off gating (0 / 1)
* Accents (e.g. 0.7, 1.0)
* Ghost notes (0.2–0.4)
* Future probability weighting

Avoid boolean patterns — floats are strictly better.

---

### Step Timing

* Tempo-synced to host BPM
* Musical divisions:

  * 1/4, 1/8, 1/16, triplet, dotted
* Optional phase offset (pattern rotate)

Gate clock is **sample-accurate**, but pattern advances at step boundaries only.

---

## 3. Edge Shaping (Critical)

This is the most important part.

### Attack / Release Smoothing

Each step transition is smoothed using short ramps:

Parameters:

* `attackMs` (default ~1–5 ms)
* `releaseMs` (default ~5–20 ms)

Implementation:

* Per-sample one-pole or exponential ramp
* No discontinuities, ever

This ensures:

* No clicks
* Distortion tails breathe naturally
* Chaos oscillators aren’t “reset” perceptually

Hard gating should be **impossible by default**.

---

## 4. Depth Control

Depth determines how strongly the pattern affects the signal:

```
g_final(t) = lerp(1.0, g_pattern(t), depth)
```

Where:

* `depth = 0.0` → gate bypassed
* `depth = 1.0` → full pattern applied

This is crucial for subtle rhythmic motion instead of EDM pumping.

---

## 5. Interaction with Amp Envelope

This is where your design choice shines.

Final amplitude path:

```
audio
 → distortion
 → trance gate (rhythm)
 → amp envelope (note articulation)
 → effects
```

Important rule:

* **Gate does not affect voice lifetime**
* Amp envelope still controls silence / release
* Gate can be running even during release tails

This avoids:

* Choked releases
* Voice stealing artifacts
* “Machine-gun” note behavior

---

## 6. Modulation Support

The gate should expose **three modulation destinations** minimum:

1. **Depth**
2. **Pattern Offset / Phase**
3. **Rate / Division**

Optionally later:

* Step level modulation (wild but very “Ruinae”)

Additionally:

* Gate output itself should be a **modulation source**

  * Bipolar or unipolar
  * Useful for rhythmic chaos injection elsewhere

This turns the gate into a *rhythmic control signal*, not just audio processing.

---

## 7. Per-Voice vs Global (Strong Recommendation)

### Default: **Per-Voice**

* Each voice has its own gate instance
* Pattern phase resets on note-on (optionally)
* Preserves polyphonic articulation

### Optional Mode: Global

* Shared clock and phase
* For classic trance pumping

Make this a mode switch, not two implementations.

---

## 8. Explicit Non-Goals (important for discipline)

The trance gate should **not**:

* Do sidechain compression
* Auto-normalize
* Randomize unless explicitly told
* Modulate phase or frequency directly
* Replace an envelope generator

It is **not** a dynamics processor.
It is a rhythmic mask.

---

## Minimal Parameter Set (v1)

You can ship this confidently with:

* On/Off
* Pattern (8/16 steps)
* Rate (tempo-sync)
* Depth
* Attack
* Release
* Phase Offset
* Per-voice / Global

Everything else can come later.

---

## Blunt verdict

Adding this gate **elevates** the synth rather than cluttering it — *because* you put it post-distortion and spec’d it as a shaped VCA instead of a dumb chopper.

If you want next, we can:

* Turn this into a **formal spec block** for the markdown
* Design a **probabilistic / chaos-mutated pattern mode**
* Or sketch a **minimal RT-safe C++ class interface**


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
