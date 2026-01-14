# Layer 3: System Components

[← Back to Architecture Index](README.md)

**Location**: `dsp/include/krate/dsp/systems/` | **Dependencies**: Layers 0-2

---

## DelayEngine
**Path:** [delay_engine.h](../../dsp/include/krate/dsp/systems/delay_engine.h) | **Since:** 0.0.18

Core delay system with tempo sync and modulation routing.

```cpp
class DelayEngine {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setDelayTime(float ms) noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setModulationSource(ModulationSource source) noexcept;
    void setModulationDepth(float depth) noexcept;
};
```

---

## FeedbackNetwork
**Path:** [feedback_network.h](../../dsp/include/krate/dsp/systems/feedback_network.h) | **Since:** 0.0.19

Stereo feedback routing with filtering and saturation.

```cpp
class FeedbackNetwork {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float& left, float& right, float inputL, float inputR) noexcept;
    void pushToDelay(float left, float right) noexcept;
    void setFeedback(float amount) noexcept;       // 0-1.2
    void setCrossAmount(float amount) noexcept;    // 0=dual mono, 1=ping-pong
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterType(FilterType type) noexcept;
    void setSaturationEnabled(bool enabled) noexcept;
    void setSaturationDrive(float dB) noexcept;
};
```

---

## ModulationMatrix
**Path:** [modulation_matrix.h](../../dsp/include/krate/dsp/systems/modulation_matrix.h) | **Since:** 0.0.20

Flexible modulation source → destination routing.

```cpp
class ModulationMatrix {
    void prepare(double sampleRate) noexcept;
    void setConnection(ModSource source, ModDest dest, float depth) noexcept;
    [[nodiscard]] float getModulation(ModDest dest) const noexcept;
    void process() noexcept;  // Update all sources, compute destinations
};
```

---

## CharacterProcessor
**Path:** [character_processor.h](../../dsp/include/krate/dsp/systems/character_processor.h) | **Since:** 0.0.21

Analog character coloration.

```cpp
enum class CharacterMode : uint8_t { Clean, Tape, BBD, DigitalVintage };

class CharacterProcessor {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setMode(CharacterMode mode) noexcept;
    void setAge(float amount) noexcept;  // 0-1 degradation
};
```

---

## TapManager
**Path:** [tap_manager.h](../../dsp/include/krate/dsp/systems/tap_manager.h) | **Since:** 0.0.22

Multi-tap delay management with filtering and panning.

```cpp
class TapManager {
    void prepare(double sampleRate, size_t numTaps, float maxDelayMs) noexcept;
    void setTapTime(size_t tap, float ms) noexcept;
    void setTapLevel(size_t tap, float dB) noexcept;
    void setTapPan(size_t tap, float pan) noexcept;  // -1 to +1
    void setTapFilterCutoff(size_t tap, float hz) noexcept;
    void setTapEnabled(size_t tap, bool enabled) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
};
```

---

## GrainCloud
**Path:** [grain_cloud.h](../../dsp/include/krate/dsp/systems/grain_cloud.h) | **Since:** 0.0.35

Polyphonic grain management for granular synthesis.

```cpp
class GrainCloud {
    void prepare(double sampleRate, size_t maxGrains, size_t maxGrainSamples) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setGrainSize(float ms) noexcept;
    void setGrainDensity(float grainsPerSecond) noexcept;
    void setGrainPitch(float semitones) noexcept;
    void setPositionSpread(float amount) noexcept;
    void setPitchSpread(float semitones) noexcept;
    void setEnvelopeType(GrainEnvelopeType type) noexcept;
};
```

---

## AmpChannel
**Path:** [amp_channel.h](../../dsp/include/krate/dsp/systems/amp_channel.h) | **Since:** 0.0.65

Guitar amp channel with multi-stage gain, Baxandall tone stack, and optional oversampling.

```cpp
enum class ToneStackPosition : uint8_t { Pre, Post };

class AmpChannel {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Gain staging
    void setInputGain(float dB) noexcept;      // [-24, +24] dB
    void setPreampGain(float dB) noexcept;     // [-24, +24] dB
    void setPowerampGain(float dB) noexcept;   // [-24, +24] dB
    void setMasterVolume(float dB) noexcept;   // [-60, +6] dB

    // Preamp configuration
    void setPreampStages(int count) noexcept;  // [1, 3]

    // Tone stack (Baxandall-style)
    void setToneStackPosition(ToneStackPosition pos) noexcept;
    void setBass(float value) noexcept;        // [0, 1] -> +/-12dB
    void setMid(float value) noexcept;         // [0, 1] -> +/-12dB
    void setTreble(float value) noexcept;      // [0, 1] -> +/-12dB
    void setPresence(float value) noexcept;    // [0, 1] -> +/-6dB

    // Character controls
    void setBrightCap(bool enabled) noexcept;  // Gain-dependent HF boost

    // Oversampling
    void setOversamplingFactor(int factor) noexcept;  // 1, 2, or 4
    [[nodiscard]] size_t getLatency() const noexcept;
};
```

**Signal Flow:**
```
Input -> [Input Gain] -> [Bright Cap] -> [Tone Stack Pre?] ->
[Preamp 1-3] -> [Tone Stack Post?] -> [Poweramp] -> [Master] -> Output
```

**Key Features:**
- Composes 3 TubeStage processors for preamp + 1 for poweramp
- Baxandall tone stack with independent bass/treble (100Hz/800Hz/3kHz/5kHz)
- Bright cap: +6dB at -24dB input, linear attenuation to 0dB at +12dB
- Deferred oversampling: factor change applies on reset()/prepare()
- 5ms parameter smoothing for click-free operation

---

## TapeMachine
**Path:** [tape_machine.h](../../dsp/include/krate/dsp/systems/tape_machine.h) | **Since:** 0.0.66

Complete tape machine emulation composing saturation, filters, and modulation.

```cpp
enum class MachineModel : uint8_t { Studer, Ampex };
enum class TapeSpeed : uint8_t { IPS_7_5, IPS_15, IPS_30 };
enum class TapeType : uint8_t { Type456, Type900, TypeGP9 };

class TapeMachine {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Machine configuration
    void setMachineModel(MachineModel model) noexcept;
    void setTapeSpeed(TapeSpeed speed) noexcept;
    void setTapeType(TapeType type) noexcept;

    // Gain staging
    void setInputLevel(float dB) noexcept;
    void setOutputLevel(float dB) noexcept;

    // Saturation
    void setBias(float bias) noexcept;
    void setSaturation(float amount) noexcept;
    void setHysteresisModel(HysteresisSolver solver) noexcept;

    // Frequency shaping
    void setHeadBumpAmount(float amount) noexcept;
    void setHeadBumpFrequency(float hz) noexcept;
    void setHighFreqRolloffAmount(float amount) noexcept;
    void setHighFreqRolloffFrequency(float hz) noexcept;

    // Modulation
    void setHiss(float amount) noexcept;
    void setWow(float amount) noexcept;
    void setFlutter(float amount) noexcept;
    void setWowRate(float hz) noexcept;
    void setFlutterRate(float hz) noexcept;
    void setWowDepth(float cents) noexcept;
    void setFlutterDepth(float cents) noexcept;
};
```

**Signal Flow:**
```
Input -> [Input Gain] -> [TapeSaturator] -> [Head Bump] ->
[HF Rolloff] -> [Wow/Flutter] -> [Hiss] -> [Output Gain] -> Output
```

**Key Features:**
- Composes TapeSaturator (Layer 2), NoiseGenerator (Layer 2), LFO x2 (Layer 1), Biquad x2 (Layer 1)
- Machine models: Studer (transparent), Ampex (warm)
- Tape speeds: 7.5/15/30 ips with speed-dependent frequency characteristics
- Tape types: Type456/Type900/TypeGP9 affecting saturation character
- Full manual override for all preset defaults
- 5ms parameter smoothing for click-free operation
