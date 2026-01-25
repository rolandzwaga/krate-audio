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

---

## FuzzPedal
**Path:** [fuzz_pedal.h](../../dsp/include/krate/dsp/systems/fuzz_pedal.h) | **Since:** 0.0.67

Complete fuzz pedal system with input buffer, noise gate, and volume control.

```cpp
enum class GateType : uint8_t { SoftKnee, HardGate, LinearRamp };
enum class GateTiming : uint8_t { Fast, Normal, Slow };
enum class BufferCutoff : uint8_t { Hz5, Hz10, Hz20 };

class FuzzPedal {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // FuzzProcessor controls
    void setFuzzType(FuzzType type) noexcept;   // Germanium, Silicon
    void setFuzz(float amount) noexcept;        // [0, 1]
    void setTone(float tone) noexcept;          // [0, 1] (0=dark, 1=bright)
    void setBias(float bias) noexcept;          // [0, 1] (0=dying battery)

    // Output volume
    void setVolume(float dB) noexcept;          // [-24, +24] dB

    // Input buffer (DC blocking high-pass)
    void setInputBuffer(bool enabled) noexcept;
    void setBufferCutoff(BufferCutoff cutoff) noexcept;  // Hz5, Hz10, Hz20

    // Noise gate
    void setGateEnabled(bool enabled) noexcept;
    void setGateThreshold(float dB) noexcept;   // [-80, 0] dB
    void setGateType(GateType type) noexcept;
    void setGateTiming(GateTiming timing) noexcept;

    // Getters
    [[nodiscard]] FuzzType getFuzzType() const noexcept;
    [[nodiscard]] float getFuzz() const noexcept;
    [[nodiscard]] float getTone() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getVolume() const noexcept;
    [[nodiscard]] bool getInputBuffer() const noexcept;
    [[nodiscard]] BufferCutoff getBufferCutoff() const noexcept;
    [[nodiscard]] bool getGateEnabled() const noexcept;
    [[nodiscard]] float getGateThreshold() const noexcept;
    [[nodiscard]] GateType getGateType() const noexcept;
    [[nodiscard]] GateTiming getGateTiming() const noexcept;
};
```

**Signal Flow:**
```
Input -> [Input Buffer if enabled] -> [FuzzProcessor] ->
[Noise Gate if enabled] -> [Volume] -> Output
```

**Key Features:**
- Composes FuzzProcessor (Layer 2), Biquad (Layer 1), EnvelopeFollower (Layer 2), OnePoleSmoother (Layer 1)
- Input buffer: Butterworth high-pass at 5/10/20 Hz for DC blocking
- Three noise gate types: SoftKnee (musical), HardGate (binary), LinearRamp (gradual)
- Three gate timing presets: Fast (0.5ms/20ms), Normal (1ms/50ms), Slow (2ms/100ms)
- 5ms equal-power crossfade for gate type changes (click-free)
- 5ms parameter smoothing on volume control
- Default: Input buffer disabled, Gate disabled, Volume 0dB

---

## DistortionRack
**Path:** [distortion_rack.h](../../dsp/include/krate/dsp/systems/distortion_rack.h) | **Since:** 0.0.68

Multi-stage distortion rack with 4 configurable slots for chaining different distortion types.

```cpp
enum class SlotType : uint8_t {
    Empty, Waveshaper, TubeStage, DiodeClipper, Wavefolder, TapeSaturator, Fuzz, Bitcrusher
};

class DistortionRack {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;

    // Slot configuration
    void setSlotType(size_t slot, SlotType type) noexcept;
    void setSlotEnabled(size_t slot, bool enabled) noexcept;
    void setSlotMix(size_t slot, float mix) noexcept;      // [0, 1]
    void setSlotGain(size_t slot, float dB) noexcept;      // [-24, +24] dB

    // Processor access
    template<typename T>
    [[nodiscard]] T* getProcessor(size_t slot, size_t channel = 0) noexcept;

    // Global controls
    void setOversamplingFactor(int factor) noexcept;       // 1, 2, or 4
    void setOutputGain(float dB) noexcept;                 // [-24, +24] dB
    void setDCBlockingEnabled(bool enabled) noexcept;

    // Getters
    [[nodiscard]] SlotType getSlotType(size_t slot) const noexcept;
    [[nodiscard]] bool getSlotEnabled(size_t slot) const noexcept;
    [[nodiscard]] float getSlotMix(size_t slot) const noexcept;
    [[nodiscard]] float getSlotGain(size_t slot) const noexcept;
    [[nodiscard]] int getOversamplingFactor() const noexcept;
    [[nodiscard]] float getOutputGain() const noexcept;
    [[nodiscard]] bool getDCBlockingEnabled() const noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;
};
```

**Signal Flow:**
```
Input -> [Oversample Up] -> Slot 0 (process -> mix -> gain -> DC block) ->
Slot 1 -> Slot 2 -> Slot 3 -> [Oversample Down] -> [Output Gain] -> Output
```

**Key Features:**
- 4 configurable slots with 8 processor types (Empty + 7 distortions)
- Composes Waveshaper (L1), TubeStage, DiodeClipper, WavefolderProcessor, TapeSaturator, FuzzProcessor, BitcrusherProcessor (L2)
- Per-slot enable, mix [0-1], and gain [-24,+24 dB] with 5ms smoothing
- Per-slot DC blocking (10Hz cutoff) active when slot enabled
- Global oversampling (1x/2x/4x) applied once around entire chain
- Type-safe processor access via getProcessor<T>() template
- std::variant with compile-time dispatch (zero virtual overhead)
- Stereo processing with independent L/R processor instances

---

## FilterFeedbackMatrix
**Path:** [filter_feedback_matrix.h](../../dsp/include/krate/dsp/systems/filter_feedback_matrix.h) | **Since:** 0.0.96

Matrix of SVF filters with configurable cross-feedback routing for complex resonant textures.

```cpp
template <size_t N>  // N = 2, 3, or 4
class FilterFeedbackMatrix {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processStereo(float& left, float& right) noexcept;

    // Filter configuration
    void setActiveFilters(size_t count) noexcept;          // [1, N]
    void setFilterMode(size_t i, SVFMode mode) noexcept;
    void setFilterCutoff(size_t i, float hz) noexcept;     // [20, 20000]
    void setFilterResonance(size_t i, float q) noexcept;   // [0.5, 30]

    // Feedback routing
    void setFeedbackAmount(size_t from, size_t to, float amount) noexcept;  // [-1, 1]
    void setFeedbackMatrix(const std::array<std::array<float, N>, N>& matrix) noexcept;
    void setFeedbackDelay(size_t from, size_t to, float ms) noexcept;       // [0, 100]

    // Input/Output routing
    void setInputGain(size_t i, float gain) noexcept;      // [0, 1]
    void setOutputGain(size_t i, float gain) noexcept;     // [0, 1]
    void setInputGains(const std::array<float, N>& gains) noexcept;
    void setOutputGains(const std::array<float, N>& gains) noexcept;

    // Global control
    void setGlobalFeedback(float amount) noexcept;         // [0, 1]
    [[nodiscard]] float getGlobalFeedback() const noexcept;
};
```

**Signal Flow:**
```
Input -> [inputGains] -> Filters -> [tanh] -> [feedback matrix with delays]
                              |                         |
                              v                         v
                         [outputGains] <----- [dcBlocker] <---- [from other filters]
                              |
                              v
                           Output
```

**Key Features:**
- Template parameter N (2-4) sets compile-time array sizes for efficiency
- Runtime setActiveFilters() for CPU optimization when fewer filters needed
- Per-filter soft clipping (tanh) before feedback routing prevents instability
- Per-feedback-path DC blocking (10Hz) prevents DC accumulation
- Linear interpolation (readLinear()) for feedback path delays
- Dual-mono stereo: independent filter networks per channel
- 20ms parameter smoothing for click-free operation
- Composes SVF (L1), DelayLine (L1), DCBlocker (L1), OnePoleSmoother (L1)

**When to Use:**
- Creating complex resonant textures and filter networks
- Building FDN-style (Feedback Delay Network) effects with filters instead of pure delays
- Designing custom cross-modulating filter topologies (serial, parallel, hybrid)

---

## FilterStepSequencer
**Path:** [filter_step_sequencer.h](../../dsp/include/krate/dsp/systems/filter_step_sequencer.h) | **Since:** 0.0.98

16-step filter parameter sequencer synchronized to tempo for rhythmic filter effects.

```cpp
enum class Direction : uint8_t { Forward, Backward, PingPong, Random };

struct SequencerStep {
    float cutoffHz = 1000.0f;          // [20, 20000] Hz
    float q = 0.707f;                  // [0.5, 20.0]
    SVFMode type = SVFMode::Lowpass;
    float gainDb = 0.0f;               // [-24, +12] dB
};

class FilterStepSequencer {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Step configuration (16 steps max)
    void setNumSteps(size_t numSteps) noexcept;          // [1, 16]
    [[nodiscard]] size_t getNumSteps() const noexcept;
    void setStep(size_t stepIndex, const SequencerStep& step) noexcept;
    [[nodiscard]] const SequencerStep& getStep(size_t stepIndex) const noexcept;
    void setStepCutoff(size_t stepIndex, float hz) noexcept;
    void setStepQ(size_t stepIndex, float q) noexcept;
    void setStepType(size_t stepIndex, SVFMode type) noexcept;
    void setStepGain(size_t stepIndex, float dB) noexcept;

    // Timing
    void setTempo(float bpm) noexcept;                   // [20, 300]
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setSwing(float swing) noexcept;                 // [0, 1] (0.5 = 3:1 ratio)
    void setGlideTime(float ms) noexcept;                // [0, 500]
    void setGateLength(float gateLength) noexcept;       // [0, 1]

    // Playback
    void setDirection(Direction direction) noexcept;
    [[nodiscard]] Direction getDirection() const noexcept;

    // Transport
    void sync(double ppqPosition) noexcept;
    void trigger() noexcept;
    [[nodiscard]] int getCurrentStep() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples, const BlockContext* ctx = nullptr) noexcept;
};
```

**Signal Flow:**
```
Input -> [SVF Filter with per-step cutoff/Q/type] -> [Per-step gain] -> [Gate crossfade] -> Output
                ^
                |
          [Timing engine: tempo/swing/glide]
```

**Key Features:**
- Composes SVF (L1), LinearRamp x4 (L1) for smooth parameter transitions
- 16 programmable steps with cutoff, Q, filter type, and gain per step
- Tempo sync via NoteValue enum (1/1 to 1/32, triplet/dotted variants)
- Swing timing: 0-100%, 50% produces 3:1 ratio
- Glide/portamento: 0-500ms, auto-truncates when step duration < glide time
- Gate length: rhythmic pumping with 5ms crossfade for click-free transitions
- Four direction modes: Forward, Backward, PingPong (endpoints once), Random (no repeats)
- PPQ sync for DAW transport lock
- Filter type changes instantly at step boundaries, cutoff/Q/gain glide
- Zero allocations in process() for real-time safety

**When to Use:**
- Classic trance-gate filter effects
- Dubstep wobble bass
- Rhythmic filter modulation synchronized to tempo
- Evolving textures with per-step filter type changes
- Groovy filter movement with swing timing

**Example:**
```cpp
FilterStepSequencer seq;
seq.prepare(44100.0);

// Set up 4 steps with ascending cutoff
seq.setNumSteps(4);
seq.setStepCutoff(0, 200.0f);   // Dark
seq.setStepCutoff(1, 800.0f);   // Warm
seq.setStepCutoff(2, 2000.0f);  // Present
seq.setStepCutoff(3, 5000.0f);  // Bright

// Configure timing
seq.setTempo(120.0f);
seq.setNoteValue(NoteValue::Quarter);
seq.setGlideTime(50.0f);  // Smooth 50ms transitions

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = seq.process(buffer[i]);
}
```
