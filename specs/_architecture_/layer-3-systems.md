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
