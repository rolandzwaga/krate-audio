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
- Uses Direction enum from SequencerCore (L1)
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

---

## GranularFilter
**Path:** [granular_filter.h](../../dsp/include/krate/dsp/systems/granular_filter.h) | **Since:** 0.14.0

Granular synthesis engine with per-grain SVF filtering.

```cpp
class GranularFilter {
    // Lifecycle
    void prepare(double sampleRate, float maxDelaySeconds = 2.0f) noexcept;
    void reset() noexcept;
    void seed(uint32_t seedValue) noexcept;

    // Granular parameters
    void setGrainSize(float ms) noexcept;            // [10, 500]
    void setDensity(float grainsPerSecond) noexcept; // [1, 100]
    void setPitch(float semitones) noexcept;         // [-24, +24]
    void setPitchSpray(float amount) noexcept;       // [0, 1]
    void setPosition(float ms) noexcept;             // [0, 2000]
    void setPositionSpray(float amount) noexcept;    // [0, 1]
    void setReverseProbability(float prob) noexcept; // [0, 1]
    void setPanSpray(float amount) noexcept;         // [0, 1]
    void setJitter(float amount) noexcept;           // [0, 1]
    void setEnvelopeType(GrainEnvelopeType type) noexcept;
    void setPitchQuantMode(PitchQuantMode mode) noexcept;
    void setTexture(float amount) noexcept;          // [0, 1]
    void setFreeze(bool frozen) noexcept;

    // Filter parameters (per-grain)
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterCutoff(float hz) noexcept;         // [20, Nyquist*0.495]
    void setFilterResonance(float q) noexcept;       // [0.5, 20]
    void setFilterType(SVFMode mode) noexcept;
    void setCutoffRandomization(float octaves) noexcept;  // [0, 4]

    // Processing
    void process(float inputL, float inputR, float& outputL, float& outputR) noexcept;
    [[nodiscard]] size_t activeGrainCount() const noexcept;

    // Getters
    [[nodiscard]] float getTexture() const noexcept;
    [[nodiscard]] bool isFrozen() const noexcept;
    [[nodiscard]] PitchQuantMode getPitchQuantMode() const noexcept;
    [[nodiscard]] bool isFilterEnabled() const noexcept;
    [[nodiscard]] float getFilterCutoff() const noexcept;
    [[nodiscard]] float getFilterResonance() const noexcept;
    [[nodiscard]] SVFMode getFilterType() const noexcept;
    [[nodiscard]] float getCutoffRandomization() const noexcept;
};
```

**Signal Flow:**
```
Grain: Read position -> [Pitch shift] -> [Envelope] -> [Per-grain SVF filter] -> [Pan] -> Output mix
                                                               ^
                                                               |
                                               [Cutoff randomization: base +/- N octaves]
```

**Key Features:**
- Composes GrainPool (L1), GrainScheduler (L2), GrainProcessor (L2), DelayLine (L1), SVF (L1) x 128
- Per-grain filtering with independent filter state (64 slots x 2 channels)
- Cutoff randomization: 0-4 octaves around base frequency
- Filter type applies globally, cutoff can vary per grain
- Filter state reset on grain acquisition (no artifacts from previous grains)
- Signal flow: envelope BEFORE filter (no click at grain start)
- 1/sqrt(n) gain scaling prevents output explosion with high grain counts
- All existing granular parameters supported (pitch, position, spray, reverse, etc.)

**When to Use:**
- Spectral variations impossible with post-granular filtering
- Creating evolving textures with different spectral content per grain
- "Shimmer" and "sparkle" effects with high cutoff randomization
- Filtered granular delay effects

---

## TimeVaryingCombBank
**Path:** [timevar_comb_bank.h](../../dsp/include/krate/dsp/systems/timevar_comb_bank.h) | **Since:** 0.14.0

Bank of up to 8 comb filters with independently modulated delay times for evolving metallic and resonant textures.

```cpp
enum class Tuning : uint8_t { Harmonic, Inharmonic, Custom };

class TimeVaryingCombBank {
    // Lifecycle
    void prepare(double sampleRate, float maxDelayMs = 50.0f) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Comb configuration (1-8 combs)
    void setNumCombs(size_t count) noexcept;              // [1, 8]
    [[nodiscard]] size_t getNumCombs() const noexcept;
    void setCombDelay(size_t index, float ms) noexcept;   // Switches to Custom mode
    void setCombFeedback(size_t index, float amount) noexcept;  // [-0.9999, 0.9999]
    void setCombDamping(size_t index, float amount) noexcept;   // [0, 1] (0=bright)
    void setCombGain(size_t index, float dB) noexcept;

    // Tuning modes
    void setTuningMode(Tuning mode) noexcept;
    [[nodiscard]] Tuning getTuningMode() const noexcept;
    void setFundamental(float hz) noexcept;               // [20, 1000] Hz
    [[nodiscard]] float getFundamental() const noexcept;
    void setSpread(float amount) noexcept;                // [0, 1] (Inharmonic only)
    [[nodiscard]] float getSpread() const noexcept;

    // Modulation
    void setModRate(float hz) noexcept;                   // [0.01, 20] Hz
    [[nodiscard]] float getModRate() const noexcept;
    void setModDepth(float percent) noexcept;             // [0, 100]%
    [[nodiscard]] float getModDepth() const noexcept;
    void setModPhaseSpread(float degrees) noexcept;       // [0, 360)
    [[nodiscard]] float getModPhaseSpread() const noexcept;
    void setRandomModulation(float amount) noexcept;      // [0, 1]
    [[nodiscard]] float getRandomModulation() const noexcept;

    // Stereo
    void setStereoSpread(float amount) noexcept;          // [0, 1]
    [[nodiscard]] float getStereoSpread() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processStereo(float& left, float& right) noexcept;
};
```

**Signal Flow:**
```
Input -> [For each active comb]:
           +-> Comb[n] with modulated delay -> gain -> pan -> L/R sum
Output <- [L/R stereo output]
```

**Key Features:**
- Composes FeedbackComb x8 (L1), LFO x8 (L1), OnePoleSmoother x32 (L1), Xorshift32 x8 (L0)
- Three tuning modes:
  - Harmonic: f[n] = fundamental * (n+1) - musical harmonic series
  - Inharmonic: f[n] = fundamental * sqrt(1 + n*spread) - bell-like partials
  - Custom: manual per-comb delay times via setCombDelay()
- Per-comb modulation with independent LFO phase offsets
- Random drift modulation using Xorshift32 PRNG (deterministic on reset)
- Parameter smoothing: 20ms delay, 10ms feedback/damping, 5ms gain
- Stereo output with equal-power pan distribution
- Linear interpolation for modulated delays (not allpass)
- NaN/Inf detection per comb with auto-reset

**When to Use:**
- Creating time-varying resonances and metallic textures
- Bell-like and gong sounds with inharmonic tuning
- Evolving ambient textures with modulated comb delays
- Karplus-Strong style physical modeling with multiple strings

**Example:**
```cpp
TimeVaryingCombBank bank;
bank.prepare(44100.0);

// Set up 4 harmonically-tuned combs
bank.setNumCombs(4);
bank.setTuningMode(Tuning::Harmonic);
bank.setFundamental(100.0f);  // 100 Hz fundamental

// Add modulation
bank.setModRate(1.0f);        // 1 Hz LFO rate
bank.setModDepth(10.0f);      // 10% modulation depth
bank.setModPhaseSpread(45.0f);// 45-degree phase offset per comb

// Stereo spread
bank.setStereoSpread(0.5f);

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    float left = input[i], right = input[i];
    bank.processStereo(left, right);
    outputL[i] = left;
    outputR[i] = right;
}
```

---

## FMVoice
**Path:** [fm_voice.h](../../dsp/include/krate/dsp/systems/fm_voice.h) | **Since:** 0.14.1

Complete 4-operator FM synthesis voice with algorithm routing (DX7-style).

```cpp
enum class Algorithm : uint8_t {
    Stacked2Op, Stacked4Op, Parallel2Plus2, Branched,
    Stacked3PlusCarrier, Parallel4, YBranch, DeepStack,
    kNumAlgorithms
};

enum class OperatorMode : uint8_t { Ratio, Fixed };

class FMVoice {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Algorithm selection
    void setAlgorithm(Algorithm algorithm) noexcept;
    [[nodiscard]] Algorithm getAlgorithm() const noexcept;

    // Voice control
    void setFrequency(float hz) noexcept;
    [[nodiscard]] float getFrequency() const noexcept;

    // Per-operator configuration
    void setOperatorRatio(size_t opIndex, float ratio) noexcept;       // [0, 16]
    void setOperatorLevel(size_t opIndex, float level) noexcept;       // [0, 1]
    void setOperatorMode(size_t opIndex, OperatorMode mode) noexcept;
    void setOperatorFixedFrequency(size_t opIndex, float hz) noexcept; // Fixed mode only
    void setFeedback(float amount) noexcept;                           // [0, 1]

    // Processing
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Key Features:**
- Composes 4 FMOperator (L2) instances + DCBlocker (L1)
- 8 selectable algorithm topologies (enum-indexed static constexpr tables)
- Per-operator ratio (frequency tracking) or fixed frequency modes
- Single feedback-enabled operator per algorithm (soft-limited via tanh)
- Carrier output normalization: `sum / carrierCount` for consistent amplitude
- DC blocking on output (20 Hz highpass)
- Phase preservation on algorithm switch (click-free real-time modulation)
- Compile-time topology validation via static_assert
- NaN/Inf sanitization on all outputs, clamped to [-2.0, 2.0]
- ~360 KB per instance (4 operators with wavetables)

**Algorithm Topologies:**
| Algorithm | Carriers | Description |
|-----------|----------|-------------|
| Stacked2Op | 1 | Simple 2->1 stack (bass, leads) |
| Stacked4Op | 1 | Full 4->3->2->1 chain (rich leads, brass) |
| Parallel2Plus2 | 2 | Two parallel 2-op stacks (organ, pads) |
| Branched | 1 | Multiple mods to single carrier (bells, metallic) |
| Stacked3PlusCarrier | 2 | 3-op stack + independent carrier (e-piano) |
| Parallel4 | 4 | All 4 as carriers (additive/organ) |
| YBranch | 1 | Mod feeding two parallel stacks (complex) |
| DeepStack | 1 | 4->3->2->1 chain, mid-chain feedback (aggressive) |

**When to Use:**
- FM/PM synthesis voices for bass, leads, bells, e-pianos
- DX7-style timbres requiring algorithm-based modulation routing
- Sound design requiring precise control over operator relationships
- Situations where per-operator ratio/fixed frequency modes are needed

**Example:**
```cpp
FMVoice voice;
voice.prepare(44100.0);

// Classic 2-op FM bass
voice.setAlgorithm(Algorithm::Stacked2Op);
voice.setFrequency(110.0f);  // A2
voice.setOperatorLevel(0, 1.0f);   // Carrier
voice.setOperatorLevel(1, 0.5f);   // Modulator
voice.setOperatorRatio(1, 2.0f);   // 2:1 ratio for second harmonic
voice.setFeedback(0.2f);           // Subtle feedback richness

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = voice.process();
}
```

---

## UnisonEngine
**Path:** [unison_engine.h](../../dsp/include/krate/dsp/systems/unison_engine.h) | **Since:** 0.14.0

Multi-voice detuned oscillator with stereo spread (supersaw/unison engine).

```cpp
struct StereoOutput {
    float left = 0.0f;
    float right = 0.0f;
};

class UnisonEngine {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Parameter setters
    void setNumVoices(size_t count) noexcept;        // [1, 16]
    void setDetune(float amount) noexcept;            // [0, 1]
    void setStereoSpread(float spread) noexcept;      // [0, 1]
    void setWaveform(OscWaveform waveform) noexcept;  // All 5 waveforms
    void setFrequency(float hz) noexcept;             // Base frequency
    void setBlend(float blend) noexcept;              // [0, 1] center/outer mix

    // Processing
    [[nodiscard]] StereoOutput process() noexcept;
    void processBlock(float* left, float* right, size_t numSamples) noexcept;
};
```

**Key Features:**
- Composes 16 PolyBlepOscillator (L1) instances pre-allocated as fixed-size array
- JP-8000 inspired non-linear detune curve (power exponent 1.7)
- Constant-power pan law: `cos((pan+1)*pi/4)`, `sin((pan+1)*pi/4)`
- Equal-power crossfade blend between center and outer voices with group-size normalization
- Gain compensation: `1/sqrt(numVoices)` for incoherent signal summation
- Deterministic random phase initialization using Xorshift32 (seed 0x5EEDBA5E)
- Output sanitization: NaN via bit_cast, clamp to [-2.0, 2.0]
- Zero heap allocation, total instance size < 2048 bytes
- Dependencies: Layer 0 (pitch_utils, crossfade_utils, random, math_constants, db_utils), Layer 1 (PolyBlepOscillator)

**When to Use:**
- Thick unison oscillator sounds (supersaw pads, detuned leads)
- Classic 7-voice supersaw (Roland JP-8000 style)
- Multi-waveform unison (square/triangle/sine unison stacks)
- Any scenario requiring multiple detuned oscillator voices with stereo spread

**Example:**
```cpp
UnisonEngine engine;
engine.prepare(44100.0);

// Classic 7-voice supersaw
engine.setNumVoices(7);
engine.setWaveform(OscWaveform::Sawtooth);
engine.setFrequency(440.0f);
engine.setDetune(0.5f);
engine.setStereoSpread(0.8f);
engine.setBlend(0.5f);

// Per-sample processing
StereoOutput out = engine.process();

// Block processing (bit-identical to per-sample)
std::array<float, 512> left{}, right{};
engine.processBlock(left.data(), right.data(), 512);
```

---

## VectorMixer
**Path:** [vector_mixer.h](../../dsp/include/krate/dsp/systems/vector_mixer.h) | **Since:** 0.14.1

XY vector mixer for 4 audio sources with selectable topologies and mixing laws.

```cpp
enum class Topology : uint8_t { Square, Diamond };
enum class MixingLaw : uint8_t { Linear, EqualPower, SquareRoot };

struct Weights { float a, b, c, d; };

class VectorMixer {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // XY Position (thread-safe atomic stores)
    void setVectorX(float x) noexcept;         // [-1, 1], clamped
    void setVectorY(float y) noexcept;         // [-1, 1], clamped
    void setVectorPosition(float x, float y) noexcept;

    // Configuration (NOT thread-safe)
    void setTopology(Topology topo) noexcept;
    void setMixingLaw(MixingLaw law) noexcept;
    void setSmoothingTimeMs(float ms) noexcept;  // 0=instant, default 5ms

    // Processing - Mono
    [[nodiscard]] float process(float a, float b, float c, float d) noexcept;
    void processBlock(const float* a, const float* b, const float* c, const float* d,
                      float* output, size_t numSamples) noexcept;

    // Processing - Stereo
    [[nodiscard]] StereoOutput process(float aL, float aR, float bL, float bR,
                                        float cL, float cR, float dL, float dR) noexcept;
    void processBlock(const float* aL, const float* aR, const float* bL, const float* bR,
                      const float* cL, const float* cR, const float* dL, const float* dR,
                      float* outL, float* outR, size_t numSamples) noexcept;

    // Weight Query
    [[nodiscard]] Weights getWeights() const noexcept;
};
```

**Key Features:**
- Two topologies: Square (bilinear interpolation, corners) and Diamond (Prophet VS-style, cardinal points)
- Three mixing laws: Linear (amplitude-preserving), EqualPower/SquareRoot (power-preserving via sqrt)
- Per-axis one-pole exponential smoothing (configurable 0-inf ms)
- Thread-safe X/Y/smoothingTime setters (std::atomic with relaxed ordering)
- Header-only, ~52 bytes per instance, no heap allocation
- Dependencies: Layer 0 only (math_constants.h, db_utils.h, stereo_output.h)
- All methods noexcept, real-time safe

**Topologies:**
| Topology | Source Layout | At Center | Use Case |
|----------|--------------|-----------|----------|
| Square | A=TL, B=TR, C=BL, D=BR | All 0.25 | Standard XY pad |
| Diamond | A=Left, B=Right, C=Top, D=Bottom | All 0.25 | Prophet VS-style |

**When to Use:**
- Blending 4 oscillator waveforms with XY joystick control
- Vector synthesis (Prophet VS, Korg Wavestation style)
- Any 4-source crossfade driven by 2D position
- Stereo source blending with identical per-channel weights

**Example:**
```cpp
VectorMixer mixer;
mixer.setSmoothingTimeMs(5.0f);
mixer.setTopology(Topology::Square);
mixer.setMixingLaw(MixingLaw::EqualPower);
mixer.prepare(44100.0);

mixer.setVectorPosition(0.3f, -0.5f);  // Thread-safe

// Mono processing
float out = mixer.process(osc1, osc2, osc3, osc4);

// Stereo processing
StereoOutput stereo = mixer.process(aL, aR, bL, bR, cL, cR, dL, dR);

// Weight query for UI
Weights w = mixer.getWeights();
```

---

## VowelSequencer
**Path:** [vowel_sequencer.h](../../dsp/include/krate/dsp/systems/vowel_sequencer.h) | **Since:** 0.14.0

16-step vowel formant sequencer synchronized to tempo for rhythmic "talking" effects.

```cpp
struct VowelStep {
    Vowel vowel = Vowel::A;     // Discrete vowel (A, E, I, O, U)
    float morph = 0.0f;         // Continuous morph position [0, 4]
};

class VowelSequencer {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Step configuration (16 steps max)
    void setNumSteps(size_t numSteps) noexcept;          // [1, 16]
    [[nodiscard]] size_t getNumSteps() const noexcept;
    void setStep(size_t stepIndex, const VowelStep& step) noexcept;
    [[nodiscard]] const VowelStep& getStep(size_t stepIndex) const noexcept;
    void setStepVowel(size_t stepIndex, Vowel vowel) noexcept;
    void setStepMorph(size_t stepIndex, float morph) noexcept;

    // Mode
    void setMorphMode(bool enabled) noexcept;            // Discrete vs continuous
    [[nodiscard]] bool isMorphMode() const noexcept;

    // Global formant modification
    void setFormantShift(float semitones) noexcept;      // [-24, +24]
    void setGender(float amount) noexcept;               // [-1, +1]

    // Timing (delegated to SequencerCore)
    void setTempo(float bpm) noexcept;                   // [20, 300]
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setSwing(float swing) noexcept;                 // [0, 1]
    void setGlideTime(float ms) noexcept;                // [0, 500]
    void setGateLength(float gateLength) noexcept;       // [0, 1]

    // Playback (delegated to SequencerCore)
    void setDirection(Direction direction) noexcept;
    [[nodiscard]] Direction getDirection() const noexcept;

    // Transport (delegated to SequencerCore)
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
Input -> [FormantFilter with per-step vowel/morph] -> [Gate crossfade to dry] -> Output
                ^
                |
          [SequencerCore: tempo/swing/gate/direction]
```

**Key Features:**
- Composes SequencerCore (L1) for timing, FormantFilter (L2) for sound
- 16 programmable steps with discrete vowel or continuous morph per step
- Discrete mode: snap to exact A/E/I/O/U formants
- Morph mode: interpolate between vowels (0.0=A, 1.0=E, 2.0=I, 3.0=O, 4.0=U)
- Global formant shift (+/- 24 semitones) and gender (-1=male, +1=female)
- Gate behavior: output = wet * gateRamp + input * (1 - gateRamp)
- When gate is off, returns to dry input (not silence)
- Direction modes: Forward, Backward, PingPong, Random (via SequencerCore)
- PPQ sync for DAW transport lock (via SequencerCore)
- Zero allocations in process() for real-time safety

**When to Use:**
- Creating "talking" or "vowel" filter effects
- Rhythmic vocal-like sweeps on synth pads or bass
- Electronic music production with tempo-synced formant modulation
- Sound design requiring vowel sequences

**Example:**
```cpp
VowelSequencer seq;
seq.prepare(44100.0);

// Set up 5 vowels A-E-I-O-U
seq.setNumSteps(5);
seq.setStepVowel(0, Vowel::A);
seq.setStepVowel(1, Vowel::E);
seq.setStepVowel(2, Vowel::I);
seq.setStepVowel(3, Vowel::O);
seq.setStepVowel(4, Vowel::U);

// Configure timing
seq.setTempo(120.0f);
seq.setNoteValue(NoteValue::Eighth);
seq.setGateLength(0.75f);  // 75% gate for rhythmic effect

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = seq.process(buffer[i]);
}
```

---

## VoiceAllocator
**Path:** [voice_allocator.h](../../dsp/include/krate/dsp/systems/voice_allocator.h) | **Since:** 0.14.2

Polyphonic voice management with configurable allocation strategies and voice stealing.

```cpp
enum class VoiceState : uint8_t { Idle, Active, Releasing };
enum class AllocationMode : uint8_t { RoundRobin, Oldest, LowestVelocity, HighestNote };
enum class StealMode : uint8_t { Hard, Soft };

struct VoiceEvent {
    enum class Type : uint8_t { NoteOn, NoteOff, Steal };
    Type type;
    uint8_t voiceIndex;
    uint8_t note;
    uint8_t velocity;
    float frequency;
};

class VoiceAllocator {
    static constexpr size_t kMaxVoices = 32;
    static constexpr size_t kMaxUnisonCount = 8;
    static constexpr size_t kMaxEvents = 64;

    // Core note events
    [[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note, uint8_t velocity) noexcept;
    [[nodiscard]] std::span<const VoiceEvent> noteOff(uint8_t note) noexcept;
    void voiceFinished(size_t voiceIndex) noexcept;

    // Configuration
    void setAllocationMode(AllocationMode mode) noexcept;
    void setStealMode(StealMode mode) noexcept;
    [[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept;
    void setUnisonCount(size_t count) noexcept;
    void setUnisonDetune(float amount) noexcept;
    void setPitchBend(float semitones) noexcept;
    void setTuningReference(float a4Hz) noexcept;

    // Thread-safe state queries
    [[nodiscard]] int getVoiceNote(size_t voiceIndex) const noexcept;
    [[nodiscard]] VoiceState getVoiceState(size_t voiceIndex) const noexcept;
    [[nodiscard]] bool isVoiceActive(size_t voiceIndex) const noexcept;
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept;

    void reset() noexcept;
};
```

**Key Features:**
- Pure routing engine: produces VoiceEvent instructions, does NOT contain DSP
- Four allocation strategies: RoundRobin, Oldest (default), LowestVelocity, HighestNote
- Voice stealing: Hard (immediate Steal event) and Soft (NoteOff + NoteOn)
- Releasing-voice preference: steals releasing voices before active voices
- Same-note retrigger: reuses existing voice instead of consuming a new one
- Unison mode: 1-8 voices per note with symmetric linear detune (+/-50 cents max)
- Configurable voice count: 1-32 voices, runtime adjustable
- Global pitch bend and custom A4 tuning reference
- Thread-safe queries: getVoiceNote(), getVoiceState(), getActiveVoiceCount() use atomics
- Zero heap allocation after construction, all arrays pre-allocated
- Header-only, ~1200 bytes per instance, all methods noexcept
- Dependencies: Layer 0 only (midi_utils.h, pitch_utils.h, db_utils.h)

**When to Use:**
- Implementing polyphonic synthesizers that need note-to-voice routing
- Any scenario requiring voice stealing, allocation modes, and unison support
- As the voice management layer in a PolyphonicSynthEngine (Phase 3.2)

**Related Components:**
- FMVoice (Layer 3): a single voice DSP system that would be managed by VoiceAllocator
- UnisonEngine (Layer 3): complementary per-oscillator detuning (VoiceAllocator handles voice-level, UnisonEngine handles oscillator-level)
- ADSREnvelope (Layer 1): voice lifecycle mirrors envelope lifecycle (Active=gate-on, Releasing=gate-off, Idle=envelope-complete)

**Example:**
```cpp
Krate::DSP::VoiceAllocator allocator;
allocator.setAllocationMode(AllocationMode::Oldest);
allocator.setStealMode(StealMode::Hard);

// Process note-on
auto events = allocator.noteOn(60, 100); // Middle C, velocity 100
for (const auto& e : events) {
    switch (e.type) {
        case VoiceEvent::Type::NoteOn:
            voices[e.voiceIndex].start(e.frequency, e.velocity);
            break;
        case VoiceEvent::Type::Steal:
            voices[e.voiceIndex].hardStop();
            break;
        case VoiceEvent::Type::NoteOff:
            voices[e.voiceIndex].release();
            break;
    }
}

// When voice envelope finishes
allocator.voiceFinished(voiceIndex);

// Unison mode
allocator.setUnisonCount(3);
allocator.setUnisonDetune(0.5f);
auto unisonEvents = allocator.noteOn(60, 100);
// unisonEvents has 3 NoteOn events with different voice indices and detuned frequencies
```

---

## SynthVoice
**Path:** [synth_voice.h](../../dsp/include/krate/dsp/systems/synth_voice.h) | **Since:** 0.14.2

Complete single-voice subtractive synthesis unit with dual oscillators, SVF filter, and ADSR envelopes.

```cpp
class SynthVoice {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Note control
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    [[nodiscard]] bool isActive() const noexcept;

    // Oscillator parameters
    void setOsc1Waveform(OscWaveform waveform) noexcept;
    void setOsc2Waveform(OscWaveform waveform) noexcept;
    void setOscMix(float mix) noexcept;           // [0, 1] (0=osc1, 1=osc2)
    void setOsc2Detune(float cents) noexcept;      // [-100, +100]
    void setOsc2Octave(int octave) noexcept;       // [-2, +2]

    // Filter parameters
    void setFilterType(SVFMode type) noexcept;     // LP/HP/BP/Notch
    void setFilterCutoff(float hz) noexcept;       // [20, 20000]
    void setFilterResonance(float q) noexcept;     // [0.1, 30]
    void setFilterEnvAmount(float semitones) noexcept;  // [-96, +96]
    void setFilterKeyTrack(float amount) noexcept; // [0, 1]

    // Amplitude envelope (ADSR)
    void setAmpAttack(float ms) noexcept;
    void setAmpDecay(float ms) noexcept;
    void setAmpSustain(float level) noexcept;
    void setAmpRelease(float ms) noexcept;

    // Filter envelope (ADSR)
    void setFilterAttack(float ms) noexcept;
    void setFilterDecay(float ms) noexcept;
    void setFilterSustain(float level) noexcept;
    void setFilterRelease(float ms) noexcept;

    // Velocity mapping
    void setVelocityToFilterEnv(float amount) noexcept;  // [0, 1]

    // Processing
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Signal Flow:**
```
Osc1 --+
       |-- Mix --> SVF Filter --> Amp Envelope --> Output
Osc2 --+              ^
                       |
              [Filter Env * Velocity] + [Key Tracking]
```

**Key Features:**
- Composes 2 PolyBlepOscillator (L1), 1 SVF (L1), 2 ADSREnvelope (L1)
- Per-sample filter cutoff modulation from envelope (no zipper artifacts)
- Filter key tracking: cutoff follows pitch relative to C4 (MIDI 60)
- Velocity-to-filter-envelope scaling: `effectiveEnvAmount = envAmount * (1 - velToFilterEnv + velToFilterEnv * velocity)`
- Hard retrigger: envelopes attack from current level, oscillator phase preserved
- NaN/Inf guard on all setters and noteOn() inputs (FR-032)
- processBlock() is bit-identical to process() loop (SC-004)
- < 1% CPU at 44.1 kHz with both oscillators and filter modulation active
- Header-only, zero heap allocation, all methods noexcept

**When to Use:**
- Single-voice subtractive synthesis for polyphonic engine
- Classic analog-style synth patches (bass, leads, pads)
- Any scenario requiring oscillator + filter + envelope signal chain

**Related Components:**
- FMVoice (Layer 3): FM synthesis voice (complementary synthesis method)
- VoiceAllocator (Layer 3): manages note-to-voice routing for polyphonic playback
- ADSREnvelope (Layer 1): the envelope generators used internally
- PolyBlepOscillator (Layer 1): the anti-aliased oscillators used internally

**Example:**
```cpp
SynthVoice voice;
voice.prepare(44100.0);

// Classic subtractive bass
voice.setOsc1Waveform(OscWaveform::Sawtooth);
voice.setOsc2Waveform(OscWaveform::Square);
voice.setOscMix(0.5f);
voice.setOsc2Detune(7.0f);          // +7 cents for thickness
voice.setFilterCutoff(500.0f);
voice.setFilterEnvAmount(48.0f);     // +4 octave sweep
voice.setFilterAttack(0.1f);
voice.setFilterDecay(300.0f);
voice.setFilterSustain(0.2f);
voice.setFilterKeyTrack(0.5f);

voice.noteOn(110.0f, 0.8f);          // A2, velocity 0.8
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = voice.process();
}
voice.noteOff();
// Continue processing until voice.isActive() returns false
```

---

## PolySynthEngine
**Path:** [poly_synth_engine.h](../../dsp/include/krate/dsp/systems/poly_synth_engine.h) | **Since:** 0.14.2

Complete polyphonic synthesis engine composing VoiceAllocator, SynthVoice pool, MonoHandler, NoteProcessor, and global SVF filter into a configurable engine.

```cpp
enum class VoiceMode : uint8_t { Poly, Mono };

class PolySynthEngine {
    static constexpr size_t kMaxPolyphony = 16;
    static constexpr float kMinMasterGain = 0.0f;
    static constexpr float kMaxMasterGain = 2.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note dispatch
    void noteOn(uint8_t note, uint8_t velocity) noexcept;
    void noteOff(uint8_t note) noexcept;

    // Configuration
    void setPolyphony(size_t count) noexcept;        // [1, 16]
    void setMode(VoiceMode mode) noexcept;           // Poly/Mono switching
    void setMasterGain(float gain) noexcept;         // [0, 2]
    void setSoftLimitEnabled(bool enabled) noexcept;

    // Global filter
    void setGlobalFilterEnabled(bool enabled) noexcept;
    void setGlobalFilterCutoff(float hz) noexcept;   // [20, 20000]
    void setGlobalFilterResonance(float q) noexcept; // [0.1, 30]
    void setGlobalFilterType(SVFMode mode) noexcept;

    // Mono mode config
    void setMonoPriority(MonoMode mode) noexcept;
    void setLegato(bool enabled) noexcept;
    void setPortamentoTime(float ms) noexcept;
    void setPortamentoMode(PortaMode mode) noexcept;

    // Voice parameter forwarding (all 16 voices)
    void setOsc1Waveform(OscWaveform waveform) noexcept;
    void setOsc2Waveform(OscWaveform waveform) noexcept;
    void setOscMix(float mix) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setAmpAttack(float ms) noexcept;
    void setAmpRelease(float ms) noexcept;
    // ... (30+ parameter forwarding methods)

    // NoteProcessor config
    void setPitchBend(float bipolar) noexcept;
    void setPitchBendRange(float semitones) noexcept;
    void setTuningReference(float a4Hz) noexcept;
    void setVelocityCurve(VelocityCurve curve) noexcept;

    // VoiceAllocator config
    void setAllocationMode(AllocationMode mode) noexcept;
    void setStealMode(StealMode mode) noexcept;

    // Processing
    void processBlock(float* output, size_t numSamples) noexcept;

    // State queries
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept;
    [[nodiscard]] VoiceMode getMode() const noexcept;
};
```

**Signal Flow:**
```
Poly mode:
  noteOn/Off -> VoiceAllocator -> SynthVoice[0..N-1]
  -> Sum -> Global Filter (optional) -> Master Gain * 1/sqrt(N) -> Soft Limit (tanh)

Mono mode:
  noteOn/Off -> MonoHandler -> SynthVoice[0] (with legato/portamento)
  -> Global Filter (optional) -> Master Gain * 1/sqrt(N) -> Soft Limit (tanh)
```

**Key Features:**
- Composes 16 SynthVoice (L3), VoiceAllocator (L3), MonoHandler (L2), NoteProcessor (L2), SVF (L1)
- Poly mode: full polyphonic playback with configurable voice count (1-16)
- Mono mode: single-voice with legato, portamento, and note priority
- Poly->Mono switch: most recent voice survives, others released
- Gain compensation: `1/sqrt(polyphonyCount)` based on configured (not active) voice count
- Soft limiting: `Sigmoid::tanh()` (Pade 5,4 approximant) prevents output exceeding [-1, +1]
- Global post-mix SVF filter with enable/disable bypass
- Unified parameter forwarding: set once on engine, applied to all 16 voices
- NoteProcessor is source of truth for audio frequencies (applies tuning + pitch bend)
- Deferred voiceFinished() notification (after processBlock, not mid-block)
- NaN/Inf guard on all float setters
- Per-sample portamento in mono mode for smooth frequency glides
- Header-only, zero allocations in processBlock(), all methods noexcept
- < 5% CPU with 8 voices at 44.1 kHz, < 32 KB static footprint

**When to Use:**
- Complete polyphonic synthesis engine for plugin integration
- Any scenario requiring managed multi-voice synthesis with mode switching
- Plugin processor that needs a single-call interface for synth functionality

**Related Components:**
- SynthVoice (Layer 3): individual voice DSP managed by this engine
- VoiceAllocator (Layer 3): polyphonic voice routing managed by this engine
- MonoHandler (Layer 2): monophonic note handling managed by this engine
- NoteProcessor (Layer 2): pitch/velocity processing managed by this engine
- FMVoice (Layer 3): alternative voice type for future multi-engine synth

**Example:**
```cpp
PolySynthEngine engine;
engine.prepare(44100.0, 512);

// Configure sound
engine.setOsc1Waveform(OscWaveform::Sawtooth);
engine.setFilterCutoff(2000.0f);
engine.setAmpRelease(500.0f);
engine.setPolyphony(8);

// Play a chord
engine.noteOn(60, 100);  // C4
engine.noteOn(64, 100);  // E4
engine.noteOn(67, 100);  // G4

// Process audio
std::array<float, 512> output{};
engine.processBlock(output.data(), output.size());

// Release
engine.noteOff(60);
engine.noteOff(64);
engine.noteOff(67);
```

---

## SelectableOscillator
**Path:** [selectable_oscillator.h](../../dsp/include/krate/dsp/systems/selectable_oscillator.h) | **Since:** 0.15.0

Variant-based oscillator wrapper supporting all 10 oscillator types with lazy initialization and real-time safe type switching.

```cpp
class SelectableOscillator {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Type switching (FR-001 through FR-005)
    void setType(OscType type, PhaseMode phase = PhaseMode::Reset) noexcept;
    [[nodiscard]] OscType getType() const noexcept;

    // Parameters
    void setFrequency(float hz) noexcept;

    // Processing
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Key Features:**
- OscillatorVariant: `std::variant<std::monostate, PolyBlepOscillator, WavetableOscillator, PhaseDistortionOscillator, SyncOscillator, AdditiveOscillator, ChaosOscillator, ParticleOscillator, FormantOscillator, SpectralFreezeOscillator, NoiseOscillator>`
- Lazy initialization: starts with std::monostate, constructs active type on setType()
- Type switch same-type is no-op (allocation-free)
- Phase mode: Reset (default) or Continuous across type switches
- processBlock before prepare produces silence
- NaN/Inf frequency inputs silently ignored
- All 10 types produce non-zero output at 440 Hz (RMS > -60 dBFS)

**When to Use:**
- Any voice architecture that needs runtime-selectable oscillator types
- As the oscillator building block in RuinaeVoice (dual-oscillator setup)
- When a single oscillator slot must support multiple synthesis methods

---

## VoiceModRouter
**Path:** [voice_mod_router.h](../../dsp/include/krate/dsp/systems/voice_mod_router.h) | **Since:** 0.15.0

Per-voice modulation router with fixed-size storage for up to 16 routes.

```cpp
class VoiceModRouter {
    static constexpr int kMaxRoutes = 16;

    // Route management
    void setRoute(int index, VoiceModRoute route) noexcept;
    void clearRoute(int index) noexcept;
    void clearAllRoutes() noexcept;
    [[nodiscard]] int getRouteCount() const noexcept;

    // Per-block computation (042-ext-modulation-system: 8 parameters with aftertouch)
    void computeOffsets(float env1, float env2, float env3,
                        float lfo, float gate,
                        float velocity, float keyTrack,
                        float aftertouch) noexcept;

    // Offset retrieval
    [[nodiscard]] float getOffset(VoiceModDest dest) const noexcept;
};
```

**Key Features:**
- Fixed `std::array<VoiceModRoute, 16>` storage (zero allocation)
- Route amount clamped to [-1.0, +1.0] on setRoute()
- computeOffsets() iterates active routes, reads source value, multiplies by amount, accumulates to destination
- Multiple routes to same destination are summed (FR-027)
- NaN/Inf/denormal sanitization on all computed offsets (042-ext-modulation-system FR-024)
- Source values: Env1/2/3 [0,1], LFO [-1,+1], Gate [0,1], Velocity [0,1], KeyTrack [-1,+1], Aftertouch [0,1]
- 8 modulation sources (VoiceModSource): Env1, Env2, Env3, VoiceLFO, GateOutput, Velocity, KeyTrack, Aftertouch
- 9 modulation destinations (VoiceModDest): FilterCutoff, FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, OscAPitch, OscBPitch, OscALevel, OscBLevel

**When to Use:**
- Per-voice modulation routing in synthesizer voices
- Any scenario requiring configurable source-to-destination modulation mapping
- As the modulation component in RuinaeVoice

---

## RuinaeVoice
**Path:** [ruinae_voice.h](../../dsp/include/krate/dsp/systems/ruinae_voice.h) | **Since:** 0.15.0

Complete per-voice processing unit for the Ruinae chaos/spectral hybrid synthesizer.

```cpp
class RuinaeVoice {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note control
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    void setFrequency(float hz) noexcept;
    [[nodiscard]] bool isActive() const noexcept;

    // Processing
    void processBlock(float* output, size_t numSamples) noexcept;

    // Oscillator configuration
    void setOscAType(OscType type) noexcept;
    void setOscBType(OscType type) noexcept;

    // Mixer configuration
    void setMixMode(MixMode mode) noexcept;       // CrossfadeMix or SpectralMorph
    void setMixPosition(float mix) noexcept;       // [0, 1]

    // Filter configuration
    void setFilterType(RuinaeFilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // Distortion configuration
    void setDistortionType(RuinaeDistortionType type) noexcept;
    void setDistortionDrive(float drive) noexcept;

    // TranceGate configuration
    void setTranceGateEnabled(bool enabled) noexcept;
    void setTranceGateParams(const TranceGateParams& params) noexcept;

    // Modulation routing
    void setModRoute(int index, VoiceModRoute route) noexcept;
    void setModRouteScale(VoiceModDest dest, float scale) noexcept;
    void setAftertouch(float value) noexcept;  // [0, 1] channel pressure (042-ext-modulation-system)

    // Envelope/LFO access
    ADSREnvelope& getAmpEnvelope() noexcept;
    ADSREnvelope& getFilterEnvelope() noexcept;
    ADSREnvelope& getModEnvelope() noexcept;
    LFO& getVoiceLFO() noexcept;
};
```

**Signal Flow:**
```
OSC A --+
        |-- Mixer (CrossfadeMix or SpectralMorph) --> Filter --> Distortion
OSC B --+                                                          |
                                                              DC Blocker
                                                                   |
                                                             TranceGate
                                                                   |
                                                              VCA (Amp Env)
                                                                   |
                                                                Output
```

**Key Features:**
- 2x SelectableOscillator with 10 types each
- Dual-mode mixer: CrossfadeMix (linear) or SpectralMorph (FFT-based, lazily allocated)
- FilterVariant: `std::variant<SVF, LadderFilter, FormantFilter, FeedbackComb>` (7 modes)
- DistortionVariant: `std::variant<std::monostate, ChaosWaveshaper, SpectralDistortion, GranularDistortion, Wavefolder, TapeSaturator>` (6 types)
- TranceGate: post-distortion rhythmic gating with configurable pattern
- 3x ADSR envelopes (amp, filter, modulation)
- Per-voice LFO
- VoiceModRouter with 8 sources and 9 destinations (042-ext-modulation-system)
- Aftertouch (channel pressure) as modulation source via setAftertouch() (042-ext-modulation-system FR-010)
- OscALevel/OscBLevel modulation destinations applied per-sample before mixing (042-ext-modulation-system FR-004)
- Per-sample filter cutoff modulation (no zipper artifacts)
- Per-sample modulation routing (sample-accurate)
- NaN/Inf flush on all outputs
- DC blocking after distortion
- SpectralMorphFilter lazily allocated (only when SpectralMorph mode selected)
- Voice lifetime determined solely by amp envelope

**When to Use:**
- As the per-voice unit in a Ruinae polyphonic synthesizer engine
- Any synthesis scenario requiring a full signal chain with selectable components
- Chaos/spectral hybrid synthesis with dual oscillators and modulation routing

**Related Components:**
- SelectableOscillator (Layer 3): oscillator slots
- VoiceModRouter (Layer 3): modulation routing
- SpectralMorphFilter (Layer 2): spectral morphing mixer mode
- VoiceAllocator (Layer 3): for future polyphonic voice management

---

## RuinaeEffectsChain
**Path:** [ruinae_effects_chain.h](../../dsp/include/krate/dsp/systems/ruinae_effects_chain.h) | **Since:** 0.15.0

Stereo effects chain for the Ruinae synthesizer composing existing Layer 4 effects into a fixed-order processing chain: Freeze -> Delay -> Reverb -> Output.

```cpp
enum class RuinaeDelayType : uint8_t {
    Digital = 0, Tape = 1, PingPong = 2, Granular = 3, Spectral = 4, NumTypes = 5
};

class RuinaeEffectsChain {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Processing (FR-004, FR-005, FR-028)
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // Delay type selection (FR-009 through FR-014)
    void setDelayType(RuinaeDelayType type) noexcept;
    [[nodiscard]] RuinaeDelayType getActiveDelayType() const noexcept;

    // Delay parameter forwarding (FR-015 through FR-017)
    void setDelayTime(float ms) noexcept;
    void setDelayFeedback(float amount) noexcept;
    void setDelayMix(float mix) noexcept;
    void setDelayTempo(double bpm) noexcept;

    // Freeze control (FR-018 through FR-020)
    void setFreezeEnabled(bool enabled) noexcept;
    void setFreeze(bool frozen) noexcept;
    void setFreezePitchSemitones(float semitones) noexcept;
    void setFreezeShimmerMix(float mix) noexcept;
    void setFreezeDecay(float decay) noexcept;

    // Reverb control (FR-021 through FR-023)
    void setReverbParams(const ReverbParams& params) noexcept;

    // Latency (FR-026, FR-027)
    [[nodiscard]] size_t getLatencySamples() const noexcept;
};
```

**Signal Flow:**
```
Voice Sum -> [Freeze (if enabled)] -> [Active Delay Type] -> [Reverb] -> Output
                                           |
                                    [Crossfade partner during type switch]
                                           |
                                    [Latency Compensation (non-spectral types)]
```

**Key Features:**
- Composes 5 Layer 4 delay effects (DigitalDelay, TapeDelay, PingPongDelay, GranularDelay, SpectralDelay)
- FreezeMode (Layer 4) as insert effect with pitch shifting, shimmer, and decay
- Dattorro Reverb (Layer 4) with independent freeze control
- Click-free delay type switching via 30ms linear crossfade (within 25-50ms spec)
- Fast-track crossfade: snap-to-completion when new type switch requested mid-fade
- Constant worst-case latency reporting (spectral delay FFT size, typically 1024 samples)
- Per-delay latency compensation using DelayLine pairs (4 pairs for non-spectral types)
- Unified parameter forwarding API normalizes different delay type setter names
- Chunk processing: processBlock splits large blocks into maxBlockSize chunks
- All runtime methods noexcept, zero allocations in processBlock
- Header-only, Layer 3 (documented exception: composes Layer 4 effects)

**When to Use:**
- Ruinae engine composition (Phase 6) as the effects section after voice summing
- Any multi-effect chain requiring delay type selection with constant latency

**Related Components:**
- RuinaeVoice (Layer 3): per-voice synthesis, feeds into this effects chain
- DigitalDelay, TapeDelay, PingPongDelay, GranularDelay, SpectralDelay (Layer 4): composed delay types
- FreezeMode (Layer 4): spectral freeze effect
- Reverb (Layer 4): Dattorro plate reverb
- DelayLine (Layer 1): used for per-delay latency compensation
- crossfadeIncrement (Layer 0): calculates per-sample crossfade alpha increment

---

## RuinaeEngine
**Path:** [ruinae_engine.h](../../dsp/include/krate/dsp/systems/ruinae_engine.h) | **Since:** 0.15.0

Complete polyphonic Ruinae synthesizer engine composing 16 RuinaeVoice instances, VoiceAllocator, MonoHandler, NoteProcessor, ModulationEngine, global stereo SVF filter, RuinaeEffectsChain, and master output.

```cpp
enum class RuinaeModDest : uint32_t {
    GlobalFilterCutoff = 64, GlobalFilterResonance = 65,
    MasterVolume = 66, EffectMix = 67,
    AllVoiceFilterCutoff = 68, AllVoiceMorphPosition = 69, AllVoiceTranceGateRate = 70
};

class RuinaeEngine {
    static constexpr size_t kMaxPolyphony = 16;
    static constexpr float kMinMasterGain = 0.0f;
    static constexpr float kMaxMasterGain = 2.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note dispatch
    void noteOn(uint8_t note, uint8_t velocity) noexcept;
    void noteOff(uint8_t note) noexcept;

    // Configuration
    void setPolyphony(size_t count) noexcept;        // [1, 16]
    void setMode(VoiceMode mode) noexcept;           // Poly/Mono switching
    void setMasterGain(float gain) noexcept;         // [0, 2]
    void setSoftLimitEnabled(bool enabled) noexcept;

    // Stereo mixing
    void setStereoSpread(float spread) noexcept;     // [0, 1]
    void setStereoWidth(float width) noexcept;       // [0, 2]

    // Global filter
    void setGlobalFilterEnabled(bool enabled) noexcept;
    void setGlobalFilterCutoff(float hz) noexcept;
    void setGlobalFilterResonance(float q) noexcept;
    void setGlobalFilterType(SVFMode mode) noexcept;

    // Global modulation
    void setGlobalModRoute(int slot, ModSource source, RuinaeModDest dest, float amount) noexcept;
    void clearGlobalModRoute(int slot) noexcept;
    void setGlobalLFO1Rate(float hz) noexcept;
    void setGlobalLFO1Waveform(Waveform shape) noexcept;
    // ... (LFO2, Chaos, Macro setters)

    // Performance controllers
    void setPitchBend(float bipolar) noexcept;
    void setAftertouch(float value) noexcept;
    void setModWheel(float value) noexcept;

    // Effects chain forwarding
    void setDelayType(RuinaeDelayType type) noexcept;
    void setDelayTime(float ms) noexcept;
    void setDelayFeedback(float amount) noexcept;
    void setDelayMix(float mix) noexcept;
    void setReverbParams(const ReverbParams& params) noexcept;
    void setFreezeEnabled(bool enabled) noexcept;
    [[nodiscard]] size_t getLatencySamples() const noexcept;

    // Voice parameter forwarding (all 16 voices)
    void setOscAType(OscType type) noexcept;
    void setOscBType(OscType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setAmpAttack(float ms) noexcept;
    void setAmpRelease(float ms) noexcept;
    // ... (50+ parameter forwarding methods for all voice subsystems)

    // Mono mode config
    void setMonoPriority(MonoMode mode) noexcept;
    void setLegato(bool enabled) noexcept;
    void setPortamentoTime(float ms) noexcept;

    // NoteProcessor config
    void setPitchBendRange(float semitones) noexcept;
    void setTuningReference(float a4Hz) noexcept;
    void setVelocityCurve(VelocityCurve curve) noexcept;

    // Tempo/Transport
    void setTempo(double bpm) noexcept;
    void setBlockContext(const BlockContext& ctx) noexcept;

    // Processing (stereo output)
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // State queries
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept;
    [[nodiscard]] VoiceMode getMode() const noexcept;
};
```

**Signal Flow:**
```
Poly mode:
  noteOn/Off -> VoiceAllocator -> RuinaeVoice[0..N-1]
  -> Equal-power stereo pan + Sum -> Stereo Width (Mid/Side)
  -> Global Filter (optional) -> Effects Chain (Freeze/Delay/Reverb)
  -> Master Gain * 1/sqrt(N) -> Soft Limit (tanh) -> NaN/Inf Flush -> Output

Mono mode:
  noteOn/Off -> MonoHandler -> RuinaeVoice[0] (with legato/per-sample portamento)
  -> Equal-power stereo pan -> Stereo Width -> Global Filter -> Effects Chain
  -> Master Gain * 1/sqrt(N) -> Soft Limit (tanh) -> NaN/Inf Flush -> Output
```

**Key Features:**
- Composes 16 RuinaeVoice (L3), VoiceAllocator (L3), MonoHandler (L2), NoteProcessor (L2), ModulationEngine (L3), 2x SVF (L1), RuinaeEffectsChain (L3)
- Poly mode: full polyphonic playback with configurable voice count (1-16)
- Mono mode: single-voice with legato, portamento, and note priority
- Stereo output with equal-power pan law: `leftGain = cos(pan * pi/2)`, `rightGain = sin(pan * pi/2)`
- Stereo spread distributes voice pan positions evenly across field
- Stereo width via Mid/Side encoding
- Global ModulationEngine with 7 engine-level destinations (RuinaeModDest enum, values 64-70)
- Previous block's output fed back as audio input for global modulation
- Gain compensation: `1/sqrt(polyphonyCount)` based on configured voice count
- Soft limiting: `Sigmoid::tanh()` prevents output exceeding [-1, +1]
- NaN/Inf flush on all output samples
- Deferred voiceFinished() notification (after processBlock, not mid-block)
- NaN/Inf guard on all float setters (silently ignored)
- Per-sample portamento in mono mode for smooth frequency glides
- Effects chain latency reported via getLatencySamples() (typically 1024 samples from spectral delay FFT)
- Header-only, zero allocations in processBlock(), all methods noexcept
- ~3.4% CPU with 8 voices at 44.1 kHz (benchmark verified)

**When to Use:**
- Top-level DSP system for the Ruinae synthesizer plugin
- Plugin processor instantiates this engine and forwards parameters
- Phase 7 plugin shell will compose this as its audio processing core

**Related Components:**
- RuinaeVoice (Layer 3): per-voice synthesis unit managed by this engine
- RuinaeEffectsChain (Layer 3): effects section after voice summing
- VoiceAllocator (Layer 3): polyphonic voice routing
- ModulationEngine (Layer 3): global modulation (LFOs, Chaos, Rungler, Macros)
- MonoHandler (Layer 2): monophonic note handling
- NoteProcessor (Layer 2): pitch/velocity processing
- PolySynthEngine (Layer 3): reference pattern (this engine follows same architecture)

**Example:**
```cpp
RuinaeEngine engine;
engine.prepare(44100.0, 512);

// Configure sound
engine.setOscAType(OscType::PolyBLEP);
engine.setOscBType(OscType::Wavetable);
engine.setFilterCutoff(2000.0f);
engine.setAmpRelease(500.0f);
engine.setPolyphony(8);
engine.setStereoSpread(0.5f);

// Configure effects
engine.setDelayType(RuinaeDelayType::Digital);
engine.setDelayTime(300.0f);
engine.setDelayMix(0.3f);
engine.setReverbParams({.roomSize = 0.6f, .mix = 0.2f});

// Play a chord
engine.noteOn(60, 100);  // C4
engine.noteOn(64, 100);  // E4
engine.noteOn(67, 100);  // G4

// Process stereo audio
std::array<float, 512> left{}, right{};
engine.processBlock(left.data(), right.data(), left.size());

// Release
engine.noteOff(60);
engine.noteOff(64);
engine.noteOff(67);
```
