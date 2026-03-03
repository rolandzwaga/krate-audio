# Data Model: Ruinae Voice Architecture

**Date**: 2026-02-08 | **Spec**: 041-ruinae-voice-architecture

## Entities

### Enumerations (in `ruinae_types.h`)

```cpp
namespace Krate::DSP {

// Oscillator type selection (FR-001)
enum class OscType : uint8_t {
    PolyBLEP = 0,
    Wavetable,
    PhaseDistortion,
    Sync,
    Additive,
    Chaos,
    Particle,
    Formant,
    SpectralFreeze,
    Noise,
    NumTypes  // = 10
};

// Mixer mode selection (FR-006)
enum class MixMode : uint8_t {
    CrossfadeMix = 0,
    SpectralMorph
};

// Oscillator phase behavior on type switch (FR-005)
enum class PhaseMode : uint8_t {
    Reset = 0,     // Reset phase to 0 on type switch (default)
    Continuous     // Attempt to preserve phase across type switch
};

// Voice filter type (FR-010)
enum class RuinaeFilterType : uint8_t {
    SVF_LP = 0,
    SVF_HP,
    SVF_BP,
    SVF_Notch,
    Ladder,
    Formant,
    Comb
};

// Voice distortion type (FR-013)
enum class RuinaeDistortionType : uint8_t {
    Clean = 0,
    ChaosWaveshaper,
    SpectralDistortion,
    GranularDistortion,
    Wavefolder,
    TapeSaturator
};

// Modulation sources (FR-025)
enum class VoiceModSource : uint8_t {
    Env1 = 0,       // Amplitude envelope
    Env2,           // Filter envelope
    Env3,           // General modulation envelope
    VoiceLFO,       // Per-voice LFO
    GateOutput,     // TranceGate envelope value
    Velocity,       // Note velocity (constant per note)
    KeyTrack,       // Key tracking (note relative to C4)
    NumSources
};

// Modulation destinations (FR-026)
enum class VoiceModDest : uint8_t {
    FilterCutoff = 0,   // Semitone offset
    FilterResonance,    // Linear offset
    MorphPosition,      // Linear offset
    DistortionDrive,    // Linear offset
    TranceGateDepth,    // Linear offset
    OscAPitch,          // Semitone offset
    OscBPitch,          // Semitone offset
    NumDestinations
};

} // namespace Krate::DSP
```

### VoiceModRoute (FR-024)

```cpp
struct VoiceModRoute {
    VoiceModSource source{VoiceModSource::Env1};
    VoiceModDest destination{VoiceModDest::FilterCutoff};
    float amount{0.0f};  // Bipolar: [-1.0, +1.0]
};
```

**Validation rules**: `amount` clamped to [-1.0, +1.0].

### VoiceModRouter (FR-024, FR-027)

```cpp
class VoiceModRouter {
    static constexpr int kMaxRoutes = 16;
    std::array<VoiceModRoute, kMaxRoutes> routes_;
    int routeCount_{0};
    std::array<float, static_cast<size_t>(VoiceModDest::NumDestinations)> offsets_;

public:
    void setRoute(int index, VoiceModRoute route) noexcept;
    void clearRoute(int index) noexcept;
    void computeOffsets(float env1, float env2, float env3,
                        float lfo, float gate,
                        float velocity, float keyTrack) noexcept;
    float getOffset(VoiceModDest dest) const noexcept;
};
```

**State transitions**: Offsets recomputed at start of each processBlock. Routes are static until user changes them.

### SelectableOscillator (FR-001 through FR-005)

```cpp
using OscillatorVariant = std::variant<
    std::monostate,           // Unprepared/empty
    PolyBlepOscillator,
    WavetableOscillator,
    PhaseDistortionOscillator,
    SyncOscillator,
    AdditiveOscillator,
    ChaosOscillator,
    ParticleOscillator,
    FormantOscillator,
    SpectralFreezeOscillator,
    NoiseOscillator
>;

class SelectableOscillator {
    OscillatorVariant oscillator_;
    OscType activeType_{OscType::PolyBLEP};
    PhaseMode phaseMode_{PhaseMode::Reset};
    float currentFrequency_{440.0f};
    double sampleRate_{44100.0};
    size_t maxBlockSize_{512};
    bool prepared_{false};

public:
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void setType(OscType type) noexcept;
    void setFrequency(float hz) noexcept;
    void setPhaseMode(PhaseMode mode) noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
    OscType getActiveType() const noexcept;
};
```

**State transitions**:
- `prepare()` -> constructs default type (PolyBLEP), calls prepare on it
- `setType(newType)` -> if same type, no-op; otherwise, destroy current via `emplace<monostate>()`, construct new via `emplace<NewType>()`, call prepare on new type, optionally preserve frequency
- `processBlock()` -> dispatches via visitor to active type's processBlock

### RuinaeVoice (FR-028 through FR-036)

```cpp
class RuinaeVoice {
    // Oscillators
    SelectableOscillator oscA_;
    SelectableOscillator oscB_;

    // Scratch buffers (allocated in prepare)
    std::vector<float> oscABuffer_;
    std::vector<float> oscBBuffer_;
    std::vector<float> mixBuffer_;

    // Mixer
    MixMode mixMode_{MixMode::CrossfadeMix};
    float mixPosition_{0.5f};
    SpectralMorphFilter spectralMorph_;

    // Filter (variant-based selection)
    using FilterVariant = std::variant<SVF, LadderFilter, FormantFilter, FeedbackComb>;
    FilterVariant filter_;
    RuinaeFilterType filterType_{RuinaeFilterType::SVF_LP};
    float filterCutoffHz_{1000.0f};
    float filterResonance_{0.707f};
    float filterEnvAmount_{0.0f};     // Semitones, bipolar
    float filterKeyTrack_{0.0f};      // 0.0 to 1.0

    // Distortion (variant-based selection)
    using DistortionVariant = std::variant<
        std::monostate,        // Clean bypass
        ChaosWaveshaper,
        SpectralDistortion,
        GranularDistortion,
        Wavefolder,
        TapeSaturator
    >;
    DistortionVariant distortion_;
    RuinaeDistortionType distortionType_{RuinaeDistortionType::Clean};
    float distortionDrive_{0.0f};
    float distortionCharacter_{0.5f};

    // TranceGate
    TranceGate tranceGate_;
    bool tranceGateEnabled_{false};

    // Envelopes
    ADSREnvelope ampEnv_;       // ENV 1
    ADSREnvelope filterEnv_;    // ENV 2
    ADSREnvelope modEnv_;       // ENV 3

    // Per-voice LFO
    LFO voiceLfo_;

    // DC Blocker (post-distortion)
    DCBlocker dcBlocker_;

    // Modulation
    VoiceModRouter modRouter_;

    // Voice state
    float noteFrequency_{0.0f};
    float velocity_{0.0f};
    double sampleRate_{0.0};
    size_t maxBlockSize_{0};
    bool prepared_{false};

public:
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    void setFrequency(float hz) noexcept;
    bool isActive() const noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Section setters (all noexcept, real-time safe)
    void setOscAType(OscType type) noexcept;
    void setOscBType(OscType type) noexcept;
    void setMixMode(MixMode mode) noexcept;
    void setMixPosition(float mix) noexcept;
    void setFilterType(RuinaeFilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;
    void setDistortionType(RuinaeDistortionType type) noexcept;
    void setDistortionDrive(float drive) noexcept;
    void setDistortionCharacter(float character) noexcept;
    void setTranceGateEnabled(bool enabled) noexcept;
    void setTranceGateParams(const TranceGateParams& params) noexcept;
    void setTranceGateTempo(double bpm) noexcept;
    float getGateValue() const noexcept;

    // Modulation routing
    void setModRoute(int index, VoiceModRoute route) noexcept;

    // Envelope accessors
    ADSREnvelope& getAmpEnvelope() noexcept;
    ADSREnvelope& getFilterEnvelope() noexcept;
    ADSREnvelope& getModEnvelope() noexcept;
    LFO& getVoiceLFO() noexcept;
};
```

**Signal flow (processBlock)**:
```
1. Compute modulation offsets (VoiceModRouter)
2. Generate OSC A -> oscABuffer_
3. Generate OSC B -> oscBBuffer_
4. Mix (CrossfadeMix or SpectralMorph) -> mixBuffer_
5. Filter (with modulated cutoff) -> mixBuffer_ (in-place)
6. Distortion -> mixBuffer_ (in-place)
7. DC Block -> mixBuffer_ (in-place)
8. TranceGate (if enabled) -> mixBuffer_ (in-place)
9. VCA (amplitude envelope) -> output
10. NaN/Inf flush -> output
```

## Relationships

```
RuinaeVoice 1--2 SelectableOscillator  (OSC A, OSC B)
RuinaeVoice 1--1 SpectralMorphFilter   (for SpectralMorph mix mode)
RuinaeVoice 1--1 FilterVariant         (selectable filter)
RuinaeVoice 1--1 DistortionVariant     (selectable distortion)
RuinaeVoice 1--1 TranceGate           (optional rhythmic gate)
RuinaeVoice 1--3 ADSREnvelope         (ENV 1, 2, 3)
RuinaeVoice 1--1 LFO                  (per-voice modulation)
RuinaeVoice 1--1 VoiceModRouter       (modulation routing)
RuinaeVoice 1--1 DCBlocker            (post-distortion cleanup)
SelectableOscillator 1--1 OscillatorVariant  (holds active osc type)
VoiceModRouter 1--16 VoiceModRoute    (fixed-size route array)
```

## Memory Estimates (per voice, lazy init)

| Component | Estimated Size | Notes |
|-----------|---------------|-------|
| SelectableOscillator (OSC A) | ~2-17 KB | Depends on active type; Particle is largest |
| SelectableOscillator (OSC B) | ~2-17 KB | Same |
| SpectralMorphFilter | ~16 KB | FFT buffers (1024-point), only used in SpectralMorph mode |
| FilterVariant | ~1 KB | Largest is FeedbackComb with DelayLine |
| DistortionVariant | ~8 KB | SpectralDistortion has FFT buffers |
| TranceGate | ~0.5 KB | Pattern + smoothers |
| 3 ADSREnvelopes | ~0.1 KB | Very lightweight |
| LFO | ~8 KB | Wavetable (2048 * 4 bytes) |
| Scratch buffers (3) | ~12 KB | 3 * 4096 * sizeof(float) |
| VoiceModRouter | ~0.3 KB | 16 routes + offset array |
| DCBlocker | ~0.1 KB | |
| **Total (typical)** | **~20-35 KB** | With PolyBLEP + Particle active |
| **Total (worst case)** | **~64 KB** | With SpectralFreeze + Particle + SpectralMorph |
