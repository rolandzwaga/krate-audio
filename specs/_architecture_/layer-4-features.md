# Layer 4: User Features (Delay Modes)

[← Back to Architecture Index](README.md)

**Location**: `dsp/include/krate/dsp/effects/` | **Dependencies**: Layers 0-3

Layer 4 components are complete user-facing delay modes that compose layers 0-3.

---

## TapeDelay
**Path:** [tape_delay.h](../../dsp/include/krate/dsp/effects/tape_delay.h) | **Since:** 0.0.23

Vintage tape echo emulation (Space Echo, Echoplex style).

**Composes:** DelayEngine, FeedbackNetwork, CharacterProcessor (Tape mode), WowFlutter, NoiseGenerator (TapeHiss)

**Controls:** Time (20-2000ms), Feedback (0-120%), Motor Speed, Wear (wow/flutter), Saturation, Age, Multi-head enable/level/pan, Mix, Output Level

```cpp
class TapeDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setMotorSpeed(float bpm) noexcept;
    void setWear(float amount) noexcept;
    void setSaturation(float amount) noexcept;
    void setAge(float amount) noexcept;
    void setHeadEnabled(size_t head, bool enabled) noexcept;
    void setHeadLevel(size_t head, float dB) noexcept;
    void setFeedback(float amount) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## BBDDelay
**Path:** [bbd_delay.h](../../dsp/include/krate/dsp/effects/bbd_delay.h) | **Since:** 0.0.24

Bucket-brigade device emulation (Memory Man, DM-2 style).

**Composes:** DelayEngine, FeedbackNetwork, CharacterProcessor (BBD mode), LFO (Triangle)

**Unique behaviors:** Bandwidth tracks delay time (15kHz@20ms → 2.5kHz@1000ms), compander artifacts, clock noise

**Controls:** Time (20-1000ms), Feedback (0-120%), Modulation depth/rate, Age, Era (MN3005/MN3007/MN3205/SAD1024), Mix

```cpp
enum class BBDChipModel : uint8_t { MN3005, MN3007, MN3205, SAD1024 };

class BBDDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setTime(float ms) noexcept;
    void setFeedback(float amount) noexcept;
    void setModulation(float depth) noexcept;
    void setModulationRate(float hz) noexcept;
    void setAge(float amount) noexcept;
    void setEra(BBDChipModel model) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## DigitalDelay
**Path:** [digital_delay.h](../../dsp/include/krate/dsp/effects/digital_delay.h) | **Since:** 0.0.25

Clean digital delay with era presets.

**Composes:** DelayEngine, FeedbackNetwork (CrossfadingDelayLine), CharacterProcessor, DynamicsProcessor (limiter), LFO

**Controls:** Time (1-10000ms), Feedback (0-120%), Era (Pristine/EightiesDigital/LoFi), Age, Modulation, Limiter character, Mix

```cpp
enum class DigitalEra : uint8_t { Pristine, EightiesDigital, LoFi };
enum class LimiterCharacter : uint8_t { Soft, Medium, Hard };

class DigitalDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setTime(float ms) noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    void setNoteValue(NoteValue value) noexcept;
    void setFeedback(float amount) noexcept;
    void setEra(DigitalEra era) noexcept;
    void setAge(float amount) noexcept;
    void setModulationDepth(float depth) noexcept;
    void setModulationRate(float hz) noexcept;
    void setLimiterCharacter(LimiterCharacter character) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## PingPongDelay
**Path:** [ping_pong_delay.h](../../dsp/include/krate/dsp/effects/ping_pong_delay.h) | **Since:** 0.0.26

Stereo ping-pong with L/R timing ratios.

**Composes:** DelayLine ×2, LFO ×2 (90° offset), OnePoleSmoother ×7, DynamicsProcessor, stereoCrossBlend

**Controls:** Time (1-10000ms), L/R Ratio (7 presets), Cross-Feedback (0-100%), Stereo Width (0-200%), Feedback (0-120%), Modulation, Mix

```cpp
enum class LRRatio : uint8_t { OneToOne, TwoToOne, ThreeToTwo, FourToThree, OneToTwo, TwoToThree, ThreeToFour };

class PingPongDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setTime(float ms) noexcept;
    void setLRRatio(LRRatio ratio) noexcept;
    void setCrossFeedback(float amount) noexcept;
    void setStereoWidth(float amount) noexcept;
    void setFeedback(float amount) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## MultiTapDelay
**Path:** [multi_tap_delay.h](../../dsp/include/krate/dsp/effects/multi_tap_delay.h) | **Since:** 0.0.27

Multi-tap delay with rhythm patterns.

**Composes:** TapManager (8 taps), FeedbackNetwork, LFO

**Controls:** 8 taps (time/level/pan/filter each), Master Feedback, Pattern presets, Modulation, Mix

```cpp
class MultiTapDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setTapTime(size_t tap, float ms) noexcept;
    void setTapLevel(size_t tap, float dB) noexcept;
    void setTapPan(size_t tap, float pan) noexcept;
    void setTapEnabled(size_t tap, bool enabled) noexcept;
    void setMasterFeedback(float amount) noexcept;
    void loadPattern(TapPattern pattern) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## ReverseDelay
**Path:** [reverse_delay.h](../../dsp/include/krate/dsp/effects/reverse_delay.h) | **Since:** 0.0.28

Buffer-based reverse playback.

**Composes:** DelayLine (dual buffer), FeedbackNetwork, EnvelopeFollower, LFO

**Controls:** Window size (100-2000ms), Crossfade, Feedback (0-120%), Filter, Modulation, Mix

```cpp
class ReverseDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setWindowSize(float ms) noexcept;
    void setCrossfade(float amount) noexcept;
    void setFeedback(float amount) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## ShimmerDelay
**Path:** [shimmer_delay.h](../../dsp/include/krate/dsp/effects/shimmer_delay.h) | **Since:** 0.0.29

Pitch-shifted feedback for ethereal textures.

**Composes:** DelayEngine, FeedbackNetwork, PitchShifter, Diffuser, LFO

**Controls:** Time, Feedback (0-120%), Pitch (±24 semitones), Shimmer blend, Diffusion, Modulation, Mix

```cpp
class ShimmerDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setTime(float ms) noexcept;
    void setFeedback(float amount) noexcept;
    void setPitch(float semitones) noexcept;
    void setShimmerBlend(float amount) noexcept;
    void setDiffusion(float amount) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## SpectralDelay
**Path:** [spectral_delay.h](../../dsp/include/krate/dsp/effects/spectral_delay.h) | **Since:** 0.0.30

Per-frequency-band delay times via FFT.

**Composes:** STFT, FFT, DelayLine (per bin), FeedbackNetwork

**Controls:** Time base, Time spread (low→high frequency delay curve), Feedback, Freeze, Spectral filtering, Mix

```cpp
class SpectralDelay {
    void prepare(double sampleRate, size_t maxBlockSize, size_t fftSize) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setTimeBase(float ms) noexcept;
    void setTimeSpread(float amount) noexcept;  // -1 to +1
    void setFeedback(float amount) noexcept;
    void setFreeze(bool enabled) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## FreezeDelay
**Path:** [freeze_delay.h](../../dsp/include/krate/dsp/effects/freeze_delay.h) | **Since:** 0.0.31

Infinite hold with smooth capture/release.

**Composes:** DelayLine (freeze buffer), CrossfadingDelayLine, EnvelopeFollower

**Controls:** Freeze toggle, Capture time, Release time, Decay, Filter, Mix

```cpp
class FreezeDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxFreezeMs) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setFreeze(bool enabled) noexcept;
    void setCaptureTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setDecay(float amount) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## DuckingDelay
**Path:** [ducking_delay.h](../../dsp/include/krate/dsp/effects/ducking_delay.h) | **Since:** 0.0.32

Auto-ducking delay that reduces during input.

**Composes:** DelayEngine, FeedbackNetwork, DuckingProcessor

**Controls:** Time, Feedback, Duck threshold, Duck depth, Attack/Release/Hold, Mix

```cpp
class DuckingDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setTime(float ms) noexcept;
    void setFeedback(float amount) noexcept;
    void setDuckThreshold(float dB) noexcept;
    void setDuckDepth(float dB) noexcept;
    void setDuckAttack(float ms) noexcept;
    void setDuckRelease(float ms) noexcept;
    void setMix(float amount) noexcept;
};
```

---

## Reverb
**Path:** [reverb.h](../../dsp/include/krate/dsp/effects/reverb.h) | **Since:** 0.0.40

Dattorro plate reverb algorithm for spatial processing.

**Composes:** DelayLine (pre-delay + 8 tank delays), OnePoleLP (bandwidth + 2 damping), DCBlocker x2, SchroederAllpass x4 (input diffusion), OnePoleSmoother x9

**Purpose:** Implements the Dattorro plate reverb (1997) with figure-eight tank topology, quadrature LFO modulation, freeze mode for infinite sustain, and multi-tap stereo output with mid-side width control. Provides spatial depth for synthesizer output.

**When to use:** Post-delay spatial processing, shared bus reverb effect, creative sound design with freeze mode.

**Controls:** Room Size (0-1, decay), Damping (0-1, HF absorption), Width (0-1, stereo decorrelation), Mix (0-1, dry/wet), Pre-delay (0-100ms), Diffusion (0-1), Freeze (on/off), Mod Rate (0-2 Hz), Mod Depth (0-1)

```cpp
struct ReverbParams {
    float roomSize = 0.5f;     // Decay control [0.0, 1.0]
    float damping = 0.5f;      // HF absorption [0.0, 1.0]
    float width = 1.0f;        // Stereo decorrelation [0.0, 1.0]
    float mix = 0.3f;          // Dry/wet blend [0.0, 1.0]
    float preDelayMs = 0.0f;   // Pre-delay in ms [0.0, 100.0]
    float diffusion = 0.7f;    // Input diffusion [0.0, 1.0]
    bool freeze = false;       // Infinite sustain mode
    float modRate = 0.5f;      // Tank LFO rate [0.0, 2.0] Hz
    float modDepth = 0.0f;     // Tank LFO depth [0.0, 1.0]
};

class Reverb {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(const ReverbParams& params) noexcept;
    void process(float& left, float& right) noexcept;
    void processBlock(float* left, float* right, size_t numSamples) noexcept;
    bool isPrepared() const noexcept;
};
```

---

## GranularDelay
**Path:** [granular_delay.h](../../dsp/include/krate/dsp/effects/granular_delay.h) | **Since:** 0.0.35

Granular texture generation from delay buffer.

**Composes:** GrainCloud, DelayLine (source buffer), SampleRateConverter, FeedbackNetwork

**Controls:** Grain size (10-500ms), Density (0.5-50 grains/sec), Pitch (±24 semi), Position spread, Pitch spread, Envelope type, Feedback, Freeze, Mix

```cpp
class GranularDelay {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setGrainSize(float ms) noexcept;
    void setGrainDensity(float grainsPerSecond) noexcept;
    void setGrainPitch(float semitones) noexcept;
    void setPositionSpread(float amount) noexcept;
    void setPitchSpread(float semitones) noexcept;
    void setEnvelopeType(GrainEnvelopeType type) noexcept;
    void setFeedback(float amount) noexcept;
    void setFreeze(bool enabled) noexcept;
    void setMix(float amount) noexcept;
};
```
