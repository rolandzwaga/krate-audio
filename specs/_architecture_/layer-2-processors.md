# Layer 2: DSP Processors

[← Back to Architecture Index](README.md)

**Location**: `dsp/include/krate/dsp/processors/` | **Dependencies**: Layers 0-1

---

## EnvelopeFollower
**Path:** [envelope_follower.h](../../dsp/include/krate/dsp/processors/envelope_follower.h) | **Since:** 0.0.10

Level detection for dynamics and modulation.

```cpp
enum class EnvelopeDetectionMode : uint8_t { Peak, RMS, Hilbert };

class EnvelopeFollower {
    void prepare(double sampleRate) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void setAttackTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setDetectionMode(EnvelopeDetectionMode mode) noexcept;
};
```

---

## Saturation
**Path:** [saturation.h](../../dsp/include/krate/dsp/processors/saturation.h) | **Since:** 0.0.11

Multiple waveshaping algorithms with oversampling.

```cpp
enum class SaturationType : uint8_t { SoftClip, HardClip, Tape, Tube, Foldback };

class Saturation {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void setType(SaturationType type) noexcept;
    void setDrive(float dB) noexcept;
    void setOversamplingEnabled(bool enabled) noexcept;
};
```

---

## TubeStage
**Path:** [tube_stage.h](../../dsp/include/krate/dsp/processors/tube_stage.h) | **Since:** 0.10.0

Tube gain stage with configurable drive, bias, and saturation for warm, musical tube saturation.

```cpp
class TubeStage {
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
    void setInputGain(float dB) noexcept;     // [-24, +24] dB
    void setOutputGain(float dB) noexcept;    // [-24, +24] dB
    void setBias(float bias) noexcept;        // [-1, +1] asymmetry
    void setSaturationAmount(float amount) noexcept;  // [0, 1] wet/dry mix
    [[nodiscard]] float getInputGain() const noexcept;
    [[nodiscard]] float getOutputGain() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getSaturationAmount() const noexcept;
};
```

**Signal Chain:** Input -> [Input Gain] -> [Waveshaper (Tube)] -> [DC Blocker] -> [Output Gain] -> [Mix Blend]

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (waveshaper.h, dc_blocker.h, smoother.h)

---

## DiodeClipper
**Path:** [diode_clipper.h](../../dsp/include/krate/dsp/processors/diode_clipper.h) | **Since:** 0.10.0

Configurable diode clipping circuit modeling with four diode types and three topologies.

**Use when:**
- Emulating analog diode clipping circuits (overdrive pedals, guitar amps)
- Need selectable diode character (Silicon, Germanium, LED, Schottky)
- Want topology control (Symmetric for odd harmonics, Asymmetric for even+odd)
- Building distortion effects with authentic analog character

**Note:** No internal oversampling - compose with Oversampler for anti-aliasing when needed.

```cpp
enum class DiodeType : uint8_t { Silicon, Germanium, LED, Schottky };
enum class ClipperTopology : uint8_t { Symmetric, Asymmetric, SoftHard };

class DiodeClipper {
    static constexpr float kMinDriveDb = -24.0f;
    static constexpr float kMaxDriveDb = +48.0f;
    static constexpr float kMinOutputDb = -24.0f;
    static constexpr float kMaxOutputDb = +24.0f;
    static constexpr float kMinVoltage = 0.05f;
    static constexpr float kMaxVoltage = 5.0f;
    static constexpr float kMinKnee = 0.5f;
    static constexpr float kMaxKnee = 20.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
    [[nodiscard]] float processSample(float input) noexcept;

    void setDiodeType(DiodeType type) noexcept;          // Silicon, Germanium, LED, Schottky
    void setTopology(ClipperTopology topology) noexcept; // Symmetric, Asymmetric, SoftHard
    void setDrive(float dB) noexcept;                    // [-24, +48] dB input gain
    void setMix(float mix) noexcept;                     // [0, 1] dry/wet blend
    void setForwardVoltage(float voltage) noexcept;      // [0.05, 5.0] V threshold
    void setKneeSharpness(float knee) noexcept;          // [0.5, 20.0] transition sharpness
    void setOutputLevel(float dB) noexcept;              // [-24, +24] dB output gain

    [[nodiscard]] DiodeType getDiodeType() const noexcept;
    [[nodiscard]] ClipperTopology getTopology() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
    [[nodiscard]] float getForwardVoltage() const noexcept;
    [[nodiscard]] float getKneeSharpness() const noexcept;
    [[nodiscard]] float getOutputLevel() const noexcept;
    [[nodiscard]] constexpr size_t getLatency() const noexcept;  // Always 0
};
```

| DiodeType | Forward Voltage | Knee | Character |
|-----------|-----------------|------|-----------|
| Silicon | 0.6V | 5.0 | Classic overdrive |
| Germanium | 0.3V | 2.0 | Warm, vintage |
| LED | 1.8V | 15.0 | Aggressive, hard |
| Schottky | 0.2V | 1.5 | Subtle, early clipping |

| Topology | Harmonics | Use Case |
|----------|-----------|----------|
| Symmetric | Odd only | Classic distortion |
| Asymmetric | Even + Odd | Tube-like warmth |
| SoftHard | Even + Odd | Unique character |

**Signal Chain:** Input -> [Drive Gain] -> [Diode Clipping] -> [DC Blocker] -> [Output Gain] -> [Mix Blend]

**Dependencies:** Layer 0 (db_utils.h, sigmoid.h), Layer 1 (dc_blocker.h, smoother.h)

---

## WavefolderProcessor
**Path:** [wavefolder_processor.h](../../dsp/include/krate/dsp/processors/wavefolder_processor.h) | **Since:** 0.10.0

Full-featured wavefolding processor with four distinct models, symmetry control for even harmonics, DC blocking, and dry/wet mix. No internal oversampling (compose with Oversampler for anti-aliasing).

**Use when:**
- Creating wavefolding distortion effects (synthesizers, guitar effects)
- Need different harmonic flavors with single processor (triangle, sine, Buchla, Lockhart)
- Want even harmonic control via symmetry parameter (tube-like warmth)
- Building harmonic enhancement or exciter effects

**Note:** Model change is immediate (no smoothing), parameter changes (foldAmount, symmetry, mix) are smoothed over 5ms. Compose with Oversampler for anti-aliasing when needed.

```cpp
enum class WavefolderModel : uint8_t { Simple, Serge, Buchla259, Lockhart };
enum class BuchlaMode : uint8_t { Classic, Custom };

class WavefolderProcessor {
    static constexpr float kMinFoldAmount = 0.1f;
    static constexpr float kMaxFoldAmount = 10.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    void setModel(WavefolderModel model) noexcept;           // Simple, Serge, Buchla259, Lockhart
    void setBuchlaMode(BuchlaMode mode) noexcept;            // Classic (fixed), Custom (user-defined)
    void setBuchlaThresholds(const std::array<float, 5>& thresholds) noexcept;  // Custom mode
    void setBuchlaGains(const std::array<float, 5>& gains) noexcept;            // Custom mode
    void setFoldAmount(float amount) noexcept;               // [0.1, 10.0] intensity
    void setSymmetry(float symmetry) noexcept;               // [-1, +1] even harmonics
    void setMix(float mix) noexcept;                         // [0, 1] dry/wet (0 = bypass)

    [[nodiscard]] WavefolderModel getModel() const noexcept;
    [[nodiscard]] BuchlaMode getBuchlaMode() const noexcept;
    [[nodiscard]] float getFoldAmount() const noexcept;
    [[nodiscard]] float getSymmetry() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
};
```

| Model | Output Bounds | Harmonics | Character |
|-------|---------------|-----------|-----------|
| Simple | Bounded | Dense odd | Guitar distortion, general |
| Serge | [-1, 1] | Sparse FM-like | Serge modular emulation |
| Buchla259 | Bounded | Complex | 5-stage parallel, Buchla style |
| Lockhart | tanh bounded | Even + Odd | Soft saturation, nulls |

| Symmetry | Effect |
|----------|--------|
| 0.0 | Odd harmonics only (30dB+ 2nd harmonic rejection) |
| +/-0.5 | Measurable even harmonics (2nd within 20dB of 3rd) |
| +/-1.0 | Maximum asymmetry (strong even harmonics) |

**Signal Chain:** Input -> [Symmetry DC Offset] -> [Wavefolder (model)] -> [DC Blocker] -> [Mix Blend] -> Output

**Dependencies:** Layer 0 (wavefold_math.h), Layer 1 (wavefolder.h, dc_blocker.h, smoother.h)

---

## TapeSaturator
**Path:** [tape_saturator.h](../../dsp/include/krate/dsp/processors/tape_saturator.h) | **Since:** 0.10.0

Tape saturation with Simple (tanh + pre/de-emphasis) and Hysteresis (Jiles-Atherton magnetic) models.

**Use when:**
- Creating tape delay effects with authentic saturation (TapeDelay, BBDDelay)
- Need tape-style HF compression (pre-emphasis boosts HF before saturation)
- Want magnetic hysteresis memory effects for unique saturation character
- Building lo-fi or vintage tape emulation effects

**Note:** No internal oversampling - compose with Oversampler for anti-aliasing when needed. Model switching uses 10ms crossfade to prevent clicks.

```cpp
enum class TapeModel : uint8_t { Simple, Hysteresis };
enum class HysteresisSolver : uint8_t { RK2, RK4, NR4, NR8 };

class TapeSaturator {
    static constexpr float kMinDriveDb = -24.0f;
    static constexpr float kMaxDriveDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kPreEmphasisFreqHz = 3000.0f;
    static constexpr float kPreEmphasisGainDb = 9.0f;
    static constexpr float kCrossfadeDurationMs = 10.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    void setModel(TapeModel model) noexcept;               // Simple, Hysteresis
    void setSolver(HysteresisSolver solver) noexcept;      // RK2, RK4, NR4, NR8
    void setDrive(float dB) noexcept;                      // [-24, +24] dB input gain
    void setSaturation(float amount) noexcept;             // [0, 1] saturation intensity
    void setBias(float bias) noexcept;                     // [-1, +1] asymmetry
    void setMix(float mix) noexcept;                       // [0, 1] dry/wet (0 = bypass)
    void setJAParams(float a, float alpha, float c,
                     float k, float Ms) noexcept;          // Expert J-A params

    [[nodiscard]] TapeModel getModel() const noexcept;
    [[nodiscard]] HysteresisSolver getSolver() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float getSaturation() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
    [[nodiscard]] float getJA_a() const noexcept;
    [[nodiscard]] float getJA_alpha() const noexcept;
    [[nodiscard]] float getJA_c() const noexcept;
    [[nodiscard]] float getJA_k() const noexcept;
    [[nodiscard]] float getJA_Ms() const noexcept;
};
```

| Model | CPU Budget | Saturation Type | Character |
|-------|------------|-----------------|-----------|
| Simple | < 0.3% | tanh + emphasis | Classic tape HF compression |
| Hysteresis/RK4 | < 1.5% | Jiles-Atherton | Magnetic memory effects |

| Solver | Speed | Accuracy | Use Case |
|--------|-------|----------|----------|
| RK2 | Fastest | Lower | Live performance |
| RK4 | Balanced | Good | Default, general use |
| NR4 | Slower | Higher | Mixing, mastering |
| NR8 | Slowest | Highest | Critical listening |

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| drive | 0 dB | [-24, +24] | Input gain before saturation |
| saturation | 0.5 | [0, 1] | Linear (0) to full distortion (1) |
| bias | 0.0 | [-1, +1] | Asymmetry, even harmonics |
| mix | 1.0 | [0, 1] | Dry/wet blend |

**Signal Chain:**
- Simple: Input -> [Drive] -> [Pre-emphasis +9dB @ 3kHz] -> [tanh blend] -> [De-emphasis -9dB @ 3kHz] -> [DC Blocker] -> [Mix]
- Hysteresis: Input -> [Drive + Bias] -> [J-A Hysteresis] -> [DC Blocker] -> [Mix]

**Dependencies:** Layer 0 (db_utils.h, sigmoid.h, crossfade_utils.h), Layer 1 (biquad.h, dc_blocker.h, smoother.h)

---

## FuzzProcessor
**Path:** [fuzz_processor.h](../../dsp/include/krate/dsp/processors/fuzz_processor.h) | **Since:** 0.10.0

Fuzz Face style distortion with Germanium and Silicon transistor types, bias control for "dying battery" effects, and optional octave-up mode.

**Use when:**
- Creating classic fuzz pedal effects (Fuzz Face, Tone Bender emulation)
- Need selectable transistor character (Germanium warm/saggy, Silicon bright/tight)
- Want bias control for gating/dying battery effects
- Building guitar or synth fuzz effects with octave-up capability

**Note:** No internal oversampling - compose with Oversampler for anti-aliasing when needed. Type switching uses 5ms crossfade to prevent clicks.

```cpp
enum class FuzzType : uint8_t { Germanium, Silicon };

class FuzzProcessor {
    static constexpr float kMinVolumeDb = -24.0f;
    static constexpr float kMaxVolumeDb = +24.0f;
    static constexpr float kToneMinHz = 400.0f;
    static constexpr float kToneMaxHz = 8000.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    void setFuzzType(FuzzType type) noexcept;      // Germanium, Silicon
    void setFuzz(float amount) noexcept;           // [0, 1] saturation intensity
    void setVolume(float dB) noexcept;             // [-24, +24] dB output
    void setBias(float bias) noexcept;             // [0, 1] 0=dying battery, 1=normal
    void setTone(float tone) noexcept;             // [0, 1] dark to bright (400-8000Hz LP)
    void setOctaveUp(bool enabled) noexcept;       // Self-modulation octave effect

    [[nodiscard]] FuzzType getFuzzType() const noexcept;
    [[nodiscard]] float getFuzz() const noexcept;
    [[nodiscard]] float getVolume() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getTone() const noexcept;
    [[nodiscard]] bool getOctaveUp() const noexcept;
};
```

| FuzzType | Harmonics | Clipping | Character |
|----------|-----------|----------|-----------|
| Germanium | Even + Odd | Soft (tube) | Warm, saggy, vintage |
| Silicon | Odd dominant | Hard (tanh) | Bright, tight, aggressive |

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| fuzz | 0.5 | [0, 1] | Drive/saturation amount |
| volume | 0 dB | [-24, +24] | Output level |
| bias | 0.7 | [0, 1] | Gating (0=dying battery, 1=normal) |
| tone | 0.5 | [0, 1] | LP filter freq (400-8000Hz) |
| octaveUp | false | bool | Self-modulation for octave-up |

**Signal Chain:** Input -> [Octave-Up (opt)] -> [Drive] -> [Type Saturation + Sag (Ge only)] -> [Bias Gate] -> [DC Blocker] -> [Tone LP] -> [Volume] -> Output

**Dependencies:** Layer 0 (db_utils.h, sigmoid.h, crossfade_utils.h), Layer 1 (biquad.h, dc_blocker.h, smoother.h)

---

## DynamicsProcessor
**Path:** [dynamics_processor.h](../../dsp/include/krate/dsp/processors/dynamics_processor.h) | **Since:** 0.0.12

Compressor/limiter with soft knee, sidechain, lookahead.

```cpp
class DynamicsProcessor {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
    void setThreshold(float dB) noexcept;
    void setRatio(float ratio) noexcept;
    void setKneeWidth(float dB) noexcept;
    void setAttackTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setLookahead(float ms) noexcept;
    void setAutoMakeup(bool enabled) noexcept;
    void setSidechainEnabled(bool enabled) noexcept;
    void setSidechainCutoff(float hz) noexcept;
    [[nodiscard]] float getCurrentGainReduction() const noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;
};
```

---

## DuckingProcessor
**Path:** [ducking_processor.h](../../dsp/include/krate/dsp/processors/ducking_processor.h) | **Since:** 0.0.13

Sidechain-triggered gain reduction.

```cpp
class DuckingProcessor {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    [[nodiscard]] float processSample(float main, float sidechain) noexcept;
    void setThreshold(float dB) noexcept;    // -60 to 0
    void setDepth(float dB) noexcept;        // -48 to 0
    void setAttackTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setHoldTime(float ms) noexcept;
    void setRange(float dB) noexcept;        // Max attenuation limit
    [[nodiscard]] float getCurrentGainReduction() const noexcept;
};
```

---

## NoiseGenerator
**Path:** [noise_generator.h](../../dsp/include/krate/dsp/processors/noise_generator.h) | **Since:** 0.0.14

Multi-type noise for analog character.

```cpp
enum class NoiseType : uint8_t { White, Pink, TapeHiss, VinylCrackle, Asperity };

class NoiseGenerator {
    void prepare(float sampleRate, size_t maxBlockSize) noexcept;
    void processMix(const float* input, float* output, size_t numSamples) noexcept;
    void setNoiseLevel(NoiseType type, float dB) noexcept;
    void setNoiseEnabled(NoiseType type, bool enabled) noexcept;
    void setCrackleDensity(float perSecond) noexcept;
    void setSensitivity(float amount) noexcept;  // Signal-dependent noise
};
```

---

## PitchShifter
**Path:** [pitch_shifter.h](../../dsp/include/krate/dsp/processors/pitch_shifter.h) | **Since:** 0.0.15

Phase-vocoder pitch shifting with formant preservation option.

```cpp
class PitchShifter {
    void prepare(double sampleRate, size_t maxBlockSize, size_t fftSize = 2048) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
    void setPitchShift(float semitones) noexcept;
    void setFormantPreserve(bool enabled) noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;
};
```

---

## Diffuser
**Path:** [diffuser.h](../../dsp/include/krate/dsp/processors/diffuser.h) | **Since:** 0.0.16

Multi-stage allpass network for reverb density.

```cpp
class Diffuser {
    void prepare(double sampleRate) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void setDiffusion(float amount) noexcept;  // 0-1
    void setDecay(float time) noexcept;
};
```

---

## WowFlutter
**Path:** [wow_flutter.h](../../dsp/include/krate/dsp/processors/wow_flutter.h) | **Since:** 0.0.23

Tape transport modulation effects.

```cpp
class WowFlutter {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
    void setWowDepth(float cents) noexcept;      // 0-50
    void setFlutterDepth(float cents) noexcept;  // 0-20
    void setWowRate(float hz) noexcept;          // 0.3-2
    void setFlutterRate(float hz) noexcept;      // 3-10
};
```

---

## TiltEQ
**Path:** [tilt_eq.h](../../dsp/include/krate/dsp/processors/tilt_eq.h) | **Since:** 0.0.40

Single-knob tonal shaping (bass/treble tilt).

```cpp
class TiltEQ {
    void prepare(double sampleRate) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setTilt(float amount) noexcept;           // -1 (dark) to +1 (bright)
    void setCenterFrequency(float hz) noexcept;    // Pivot point
};
```

---

## BitcrusherProcessor
**Path:** [bitcrusher_processor.h](../../dsp/include/krate/dsp/processors/bitcrusher_processor.h) | **Since:** 0.10.0

Bitcrusher effect composing bit depth reduction, sample rate decimation, gain staging, dither gating, and configurable processing order.

**Use when:**
- Creating lo-fi effects with bit depth reduction and sample rate decimation
- Need drive/makeup gain staging for creative control
- Want dither with automatic gating to prevent noise during silence
- Building retro game audio or vintage digital emulation effects

**Note:** Bit depth and sample rate factor changes are immediate (no smoothing). Gain and mix parameters use 5ms smoothing for click-free automation.

```cpp
enum class ProcessingOrder : uint8_t { BitCrushFirst, SampleReduceFirst };

class BitcrusherProcessor {
    static constexpr float kMinBitDepth = 4.0f;
    static constexpr float kMaxBitDepth = 16.0f;
    static constexpr float kMinReductionFactor = 1.0f;
    static constexpr float kMaxReductionFactor = 8.0f;
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kDitherGateThresholdDb = -60.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    void setBitDepth(float bits) noexcept;                  // [4, 16] bits
    void setReductionFactor(float factor) noexcept;         // [1, 8] sample rate factor
    void setDitherAmount(float amount) noexcept;            // [0, 1] TPDF dither
    void setPreGain(float dB) noexcept;                     // [-24, +24] dB drive
    void setPostGain(float dB) noexcept;                    // [-24, +24] dB makeup
    void setMix(float mix) noexcept;                        // [0, 1] dry/wet
    void setProcessingOrder(ProcessingOrder order) noexcept;
    void setDitherGateEnabled(bool enabled) noexcept;

    [[nodiscard]] float getBitDepth() const noexcept;
    [[nodiscard]] float getReductionFactor() const noexcept;
    [[nodiscard]] float getDitherAmount() const noexcept;
    [[nodiscard]] float getPreGain() const noexcept;
    [[nodiscard]] float getPostGain() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
    [[nodiscard]] ProcessingOrder getProcessingOrder() const noexcept;
    [[nodiscard]] bool isDitherGateEnabled() const noexcept;
    [[nodiscard]] constexpr size_t getLatency() const noexcept;  // Always 0
};
```

| Parameter | Default | Range | Smoothed | Effect |
|-----------|---------|-------|----------|--------|
| bitDepth | 16 | [4, 16] | No | Quantization levels (2^N) |
| reductionFactor | 1 | [1, 8] | No | Sample rate decimation |
| ditherAmount | 0 | [0, 1] | No | TPDF dither intensity |
| preGain | 0 dB | [-24, +24] | Yes (5ms) | Drive before crushing |
| postGain | 0 dB | [-24, +24] | Yes (5ms) | Makeup after crushing |
| mix | 1.0 | [0, 1] | Yes (5ms) | Dry/wet blend |
| processingOrder | BitCrushFirst | enum | No | Effect chain order |
| ditherGateEnabled | true | bool | No | Gate dither during silence |

**Signal Chain (BitCrushFirst):** Input -> [Store Dry] -> [Pre-Gain] -> [BitCrusher + Dither Gate] -> [SampleRateReducer] -> [Post-Gain] -> [DC Blocker] -> [Mix Blend] -> Output

**Signal Chain (SampleReduceFirst):** Input -> [Store Dry] -> [Pre-Gain] -> [SampleRateReducer] -> [BitCrusher + Dither Gate] -> [Post-Gain] -> [DC Blocker] -> [Mix Blend] -> Output

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (bit_crusher.h, sample_rate_reducer.h, dc_blocker.h, smoother.h), Layer 2 peer (envelope_follower.h)

---

## CrossoverFilter
**Path:** [crossover_filter.h](../../dsp/include/krate/dsp/processors/crossover_filter.h) | **Since:** 0.12.0

Linkwitz-Riley crossover filters for phase-coherent multiband signal splitting. Outputs sum to perfectly flat frequency response.

**Use when:**
- Building multiband compressors, limiters, or dynamics processors
- Creating multiband saturation, distortion, or waveshaping effects
- Need bass management systems with sub/low/mid/high separation
- Want frequency-specific processing (e.g., high-frequency de-essing, low-frequency tightening)
- Implementing mastering-grade multiband processors

**Features:**
- Phase-coherent: all band outputs sum to flat response (within 0.1dB for 2/3-way, 1dB for 4-way)
- LR4 characteristic: -6dB at crossover frequency, 24dB/octave slopes
- Click-free automation: configurable smoothing (default 5ms)
- Tracking modes: Efficient (0.1Hz hysteresis) or HighAccuracy (per-sample coefficient updates)
- Thread-safe: lock-free atomic parameter updates for UI/audio thread safety

```cpp
enum class TrackingMode : uint8_t { Efficient, HighAccuracy };

struct CrossoverLR4Outputs { float low, high; };
struct Crossover3WayOutputs { float low, mid, high; };
struct Crossover4WayOutputs { float sub, low, mid, high; };

class CrossoverLR4 {
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequencyRatio = 0.45f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDefaultFrequency = 1000.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] CrossoverLR4Outputs process(float input) noexcept;
    void processBlock(const float* input, float* low, float* high, size_t numSamples) noexcept;

    void setCrossoverFrequency(float hz) noexcept;  // [20, sampleRate*0.45]
    void setSmoothingTime(float ms) noexcept;       // Default 5ms
    void setTrackingMode(TrackingMode mode) noexcept;

    [[nodiscard]] float getCrossoverFrequency() const noexcept;
    [[nodiscard]] float getSmoothingTime() const noexcept;
    [[nodiscard]] TrackingMode getTrackingMode() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};

class Crossover3Way {
    static constexpr float kDefaultLowMidFrequency = 300.0f;
    static constexpr float kDefaultMidHighFrequency = 3000.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] Crossover3WayOutputs process(float input) noexcept;
    void processBlock(const float* input, float* low, float* mid, float* high, size_t numSamples) noexcept;

    void setLowMidFrequency(float hz) noexcept;
    void setMidHighFrequency(float hz) noexcept;    // Auto-clamped to >= lowMid
    void setSmoothingTime(float ms) noexcept;
    void setTrackingMode(TrackingMode mode) noexcept;
};

class Crossover4Way {
    static constexpr float kDefaultSubLowFrequency = 80.0f;
    static constexpr float kDefaultLowMidFrequency = 300.0f;
    static constexpr float kDefaultMidHighFrequency = 3000.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] Crossover4WayOutputs process(float input) noexcept;
    void processBlock(const float* input, float* sub, float* low, float* mid, float* high, size_t numSamples) noexcept;

    void setSubLowFrequency(float hz) noexcept;
    void setLowMidFrequency(float hz) noexcept;     // Auto-clamped to [subLow, midHigh]
    void setMidHighFrequency(float hz) noexcept;    // Auto-clamped to >= lowMid
    void setSmoothingTime(float ms) noexcept;
    void setTrackingMode(TrackingMode mode) noexcept;
};
```

| Class | Bands | Typical Use Case | Performance |
|-------|-------|------------------|-------------|
| CrossoverLR4 | 2 (Low/High) | Simple multiband split | <100ns/sample |
| Crossover3Way | 3 (Low/Mid/High) | Standard multiband processing | ~200ns/sample |
| Crossover4Way | 4 (Sub/Low/Mid/High) | Bass management, advanced effects | ~300ns/sample |

| TrackingMode | Coefficient Updates | CPU Usage | Use Case |
|--------------|---------------------|-----------|----------|
| Efficient | When freq changes >= 0.1Hz | Lower | Most applications |
| HighAccuracy | Every sample during smoothing | Higher | Critical automation |

**Usage Example (2-way):**
```cpp
CrossoverLR4 crossover;
crossover.prepare(44100.0);
crossover.setCrossoverFrequency(1000.0f);

// In audio callback
auto [low, high] = crossover.process(inputSample);
// Process bands independently, then sum for flat output
float output = processedLow + processedHigh;
```

**Usage Example (3-way mastering):**
```cpp
Crossover3Way crossover;
crossover.prepare(44100.0);
crossover.setLowMidFrequency(300.0f);
crossover.setMidHighFrequency(3000.0f);

// Process block
crossover.processBlock(input, lowBand, midBand, highBand, blockSize);
// Apply different compression to each band, then sum
```

**Topology (LR4 2-way):**
```
Input --> [Butterworth LP Q=0.7071] --> [Butterworth LP Q=0.7071] --> Low (-6dB @ crossover)
      \
       -> [Butterworth HP Q=0.7071] --> [Butterworth HP Q=0.7071] --> High (-6dB @ crossover)

Low + High = Flat (0dB across spectrum)
```

**Dependencies:** Layer 1 (biquad.h, smoother.h)

---

## FormantFilter
**Path:** [formant_filter.h](../../dsp/include/krate/dsp/processors/formant_filter.h) | **Since:** 0.13.0

Vocal formant filtering using 3 parallel bandpass filters (F1, F2, F3) for creating "talking" effects on non-vocal audio sources.

**Use when:**
- Creating vocal/talking wah effects on synths, guitars, or pads
- Need discrete vowel selection (A, E, I, O, U) for distinct vocal characters
- Want smooth vowel morphing for animated "talking" effects
- Building formant synthesizers or vocoders
- Need pitch-independent formant shift for voice character adjustment

**Features:**
- 5 discrete vowels using formant data from Csound (bass male voice)
- Continuous vowel morphing (0-4 position, interpolates between adjacent vowels)
- Formant frequency shift (+/-24 semitones) for pitch-independent character changes
- Gender parameter (-1 male to +1 female) for quick character adjustment
- Click-free automation with configurable smoothing (default 5ms)
- Parallel bandpass topology preserves natural formant characteristics

```cpp
class FormantFilter {
    static constexpr int kNumFormants = 3;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequencyRatio = 0.45f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinGender = -1.0f;
    static constexpr float kMaxGender = 1.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    void setVowel(Vowel vowel) noexcept;              // Discrete: A, E, I, O, U
    void setVowelMorph(float position) noexcept;       // Continuous: 0-4
    void setFormantShift(float semitones) noexcept;    // [-24, +24] semitones
    void setGender(float amount) noexcept;             // [-1, +1] male/female
    void setSmoothingTime(float ms) noexcept;          // Default 5ms

    [[nodiscard]] Vowel getVowel() const noexcept;
    [[nodiscard]] float getVowelMorph() const noexcept;
    [[nodiscard]] float getFormantShift() const noexcept;
    [[nodiscard]] float getGender() const noexcept;
    [[nodiscard]] float getSmoothingTime() const noexcept;
    [[nodiscard]] bool isInMorphMode() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Vowel | F1 (Hz) | F2 (Hz) | F3 (Hz) | Character |
|-------|---------|---------|---------|-----------|
| A | 600 | 1040 | 2250 | Open, "ah" as in father |
| E | 400 | 1620 | 2400 | Mid, "eh" as in bed |
| I | 250 | 1750 | 2600 | Close, "ee" as in see |
| O | 400 | 750 | 2400 | Back rounded, "oh" as in go |
| U | 350 | 600 | 2400 | Close back, "oo" as in boot |

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| vowel | A | enum | Discrete vowel selection |
| vowelMorph | 0.0 | [0, 4] | Continuous position (0=A, 1=E, 2=I, 3=O, 4=U) |
| formantShift | 0 | [-24, +24] | Semitone shift (pow(2, semitones/12)) |
| gender | 0.0 | [-1, +1] | Scale factor (pow(2, gender*0.25)) |
| smoothingTime | 5ms | [0.1, 1000] | Parameter transition time |

**Usage Example (Basic vowel selection):**
```cpp
FormantFilter filter;
filter.prepare(44100.0);
filter.setVowel(Vowel::A);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Animated talking wah):**
```cpp
FormantFilter filter;
filter.prepare(44100.0);

// Modulate morph position with LFO for talking effect
float lfoPhase = 0.0f;
for (size_t i = 0; i < numSamples; ++i) {
    float morphPos = 2.0f + 2.0f * std::sin(lfoPhase);  // Sweep 0-4
    filter.setVowelMorph(morphPos);
    output[i] = filter.process(input[i]);
    lfoPhase += 2.0f * kPi * 0.5f / sampleRate;  // 0.5 Hz LFO
}
```

**Usage Example (Gender adjustment):**
```cpp
FormantFilter filter;
filter.prepare(44100.0);
filter.setVowel(Vowel::A);
filter.setGender(1.0f);   // Female character (+19% formant shift)
// or
filter.setGender(-1.0f);  // Male character (-17% formant shift)
```

**Topology:**
```
                 +-----------------+
                 | Bandpass F1     |
Input -------+-->| (600Hz, Q=10)   |---+
             |   +-----------------+   |
             |                         |
             |   +-----------------+   |
             +-->| Bandpass F2     |---+--> Sum --> Output
             |   | (1040Hz, Q=15)  |   |
             |   +-----------------+   |
             |                         |
             |   +-----------------+   |
             +-->| Bandpass F3     |---+
                 | (2250Hz, Q=20)  |
                 +-----------------+
```

**Dependencies:** Layer 0 (filter_tables.h), Layer 1 (biquad.h, smoother.h)

---

## EnvelopeFilter
**Path:** [envelope_filter.h](../../dsp/include/krate/dsp/processors/envelope_filter.h) | **Since:** 0.13.0

Envelope-controlled filter (auto-wah) combining EnvelopeFollower with SVF for touch-sensitive filter effects.

**Use when:**
- Creating classic auto-wah effects for guitar or synth
- Need touch-sensitive filter sweeps that respond to playing dynamics
- Want multiple filter types (lowpass, bandpass, highpass) with envelope control
- Building funk-style touch wah or bass filter effects
- Need inverse wah (Down direction) for unique tonal effects

**Features:**
- Three filter types: Lowpass, Bandpass, Highpass (12dB/oct SVF)
- Two direction modes: Up (classic wah) and Down (inverse wah)
- Exponential frequency mapping for perceptually linear sweeps
- Sensitivity control for input level matching (-24 to +24 dB)
- Configurable attack/release for envelope response (0.1ms to 5000ms)
- Depth control for modulation amount
- Dry/wet mix for parallel filtering

```cpp
enum class Direction : uint8_t { Up, Down };
enum class FilterType : uint8_t { Lowpass, Bandpass, Highpass };

class EnvelopeFilter {
    static constexpr float kMinSensitivity = -24.0f;
    static constexpr float kMaxSensitivity = 24.0f;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kDefaultMinFrequency = 200.0f;
    static constexpr float kDefaultMaxFrequency = 2000.0f;
    static constexpr float kDefaultResonance = 8.0f;
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Envelope parameters
    void setSensitivity(float dB) noexcept;        // [-24, +24] pre-gain for envelope
    void setAttack(float ms) noexcept;             // [0.1, 500] envelope attack
    void setRelease(float ms) noexcept;            // [1, 5000] envelope release
    void setDirection(Direction dir) noexcept;     // Up or Down

    // Filter parameters
    void setFilterType(FilterType type) noexcept;  // Lowpass, Bandpass, Highpass
    void setMinFrequency(float hz) noexcept;       // [20, maxFreq-1] sweep start
    void setMaxFrequency(float hz) noexcept;       // [minFreq+1, Nyquist*0.45] sweep end
    void setResonance(float q) noexcept;           // [0.5, 20.0] filter Q
    void setDepth(float amount) noexcept;          // [0, 1] modulation depth

    // Output
    void setMix(float dryWet) noexcept;            // [0, 1] dry/wet

    // Monitoring
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentEnvelope() const noexcept;

    // Getters for all parameters
    [[nodiscard]] float getSensitivity() const noexcept;
    [[nodiscard]] float getAttack() const noexcept;
    [[nodiscard]] float getRelease() const noexcept;
    [[nodiscard]] Direction getDirection() const noexcept;
    [[nodiscard]] FilterType getFilterType() const noexcept;
    [[nodiscard]] float getMinFrequency() const noexcept;
    [[nodiscard]] float getMaxFrequency() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] float getDepth() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| sensitivity | 0 dB | [-24, +24] | Pre-gain for envelope detection only |
| attack | 10 ms | [0.1, 500] | Envelope attack time |
| release | 100 ms | [1, 5000] | Envelope release time |
| direction | Up | enum | Higher envelope = higher/lower cutoff |
| filterType | Lowpass | enum | Filter response type |
| minFrequency | 200 Hz | [20, maxFreq-1] | Sweep range minimum |
| maxFrequency | 2000 Hz | [minFreq+1, Nyquist*0.45] | Sweep range maximum |
| resonance | 8.0 | [0.5, 20.0] | Filter Q factor |
| depth | 1.0 | [0, 1] | Modulation amount |
| mix | 1.0 | [0, 1] | Dry/wet blend |

**Usage Example (Classic auto-wah):**
```cpp
EnvelopeFilter filter;
filter.prepare(44100.0);
filter.setFilterType(EnvelopeFilter::FilterType::Bandpass);
filter.setMinFrequency(200.0f);
filter.setMaxFrequency(2000.0f);
filter.setResonance(8.0f);
filter.setAttack(10.0f);
filter.setRelease(100.0f);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Inverse wah for bass):**
```cpp
EnvelopeFilter filter;
filter.prepare(44100.0);
filter.setDirection(EnvelopeFilter::Direction::Down);
filter.setFilterType(EnvelopeFilter::FilterType::Lowpass);
filter.setMinFrequency(100.0f);
filter.setMaxFrequency(1000.0f);
filter.setResonance(4.0f);
```

**Topology:**
```
                   +------------------+
Input --> +------->| Sensitivity Gain |---> EnvelopeFollower
          |        +------------------+              |
          |                                          v
          |                            +-----------------------+
          |                            | Exponential Mapping   |
          |                            | (minFreq to maxFreq)  |
          |                            +-----------------------+
          |                                          |
          |        +------------------+              v
          +------->|       SVF        |<--- Cutoff Frequency
                   | (LP/BP/HP @Q)    |
                   +------------------+
                            |
                            v
                   +------------------+
                   | Dry/Wet Mix      |---> Output
                   +------------------+
```

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (svf.h), Layer 2 peer (envelope_follower.h)

---

## Phaser
**Path:** [phaser.h](../../dsp/include/krate/dsp/processors/phaser.h) | **Since:** 0.13.0

Classic phaser effect with cascaded first-order allpass filters and LFO modulation for creating sweeping notches and jets.

**Use when:**
- Creating classic phaser modulation effects on synths, guitars, or pads
- Need variable stage count (2-12) for subtle to intense phasing
- Want stereo processing with configurable LFO phase offset for width
- Building vintage phaser pedal emulations with feedback resonance
- Need tempo-synchronized modulation for rhythmic effects

**Features:**
- 2-12 cascaded allpass stages (even numbers only, N stages = N/2 notches)
- LFO waveform selection (Sine, Triangle, Square, Sawtooth)
- Exponential frequency mapping for perceptually even sweeps
- Bipolar feedback (-1 to +1) with tanh soft-clipping for resonance
- Stereo processing with 0-360 degree LFO phase offset
- Tempo sync with note value and modifier support
- Mix-before-feedback topology for classic phaser sound
- Parameter smoothing (5ms) for click-free automation

```cpp
class Phaser {
    static constexpr int kMaxStages = 12;
    static constexpr int kMinStages = 2;
    static constexpr int kDefaultStages = 4;
    static constexpr float kMinRate = 0.01f;
    static constexpr float kMaxRate = 20.0f;
    static constexpr float kMinCenterFreq = 100.0f;
    static constexpr float kMaxCenterFreq = 10000.0f;
    static constexpr float kSmoothingTimeMs = 5.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void processStereo(float* left, float* right, size_t numSamples) noexcept;

    // Stage control
    void setNumStages(int stages) noexcept;         // [2, 12] even numbers
    [[nodiscard]] int getNumStages() const noexcept;

    // LFO parameters
    void setRate(float hz) noexcept;                // [0.01, 20] Hz
    void setDepth(float amount) noexcept;           // [0, 1] sweep range
    void setWaveform(Waveform waveform) noexcept;   // Sine, Triangle, Square, Sawtooth

    // Frequency control
    void setCenterFrequency(float hz) noexcept;     // [100, 10000] Hz

    // Feedback and mix
    void setFeedback(float amount) noexcept;        // [-1, +1] bipolar resonance
    void setMix(float dryWet) noexcept;             // [0, 1] dry/wet

    // Stereo
    void setStereoSpread(float degrees) noexcept;   // [0, 360] phase offset

    // Tempo sync
    void setTempoSync(bool enabled) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setTempo(float bpm) noexcept;

    // Getters for all parameters...
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| numStages | 4 | [2, 12] even | Number of notches = stages/2 |
| rate | 0.5 Hz | [0.01, 20] | LFO frequency (free-running) |
| depth | 0.5 | [0, 1] | Sweep range around center |
| centerFrequency | 1000 Hz | [100, 10000] | Sweep midpoint |
| feedback | 0.0 | [-1, +1] | Resonance/notch emphasis |
| mix | 0.5 | [0, 1] | Dry/wet blend |
| stereoSpread | 0 deg | [0, 360] | LFO phase offset L/R |
| waveform | Sine | enum | LFO shape |

**Usage Example (Basic phaser):**
```cpp
Phaser phaser;
phaser.prepare(44100.0);
phaser.setNumStages(4);       // Classic 4-stage phaser
phaser.setRate(0.5f);         // 0.5 Hz sweep
phaser.setDepth(0.8f);        // 80% depth
phaser.setFeedback(0.5f);     // 50% feedback for resonance
phaser.setMix(0.5f);          // 50/50 dry/wet

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = phaser.process(input[i]);
}
```

**Usage Example (Stereo with spread):**
```cpp
Phaser phaser;
phaser.prepare(44100.0);
phaser.setStereoSpread(180.0f);  // Inverted L/R modulation
phaser.setRate(1.0f);
phaser.setDepth(1.0f);

// Process stereo in-place
phaser.processStereo(left, right, numSamples);
```

**Usage Example (Tempo-synced):**
```cpp
Phaser phaser;
phaser.prepare(44100.0);
phaser.setTempoSync(true);
phaser.setTempo(120.0f);                          // 120 BPM
phaser.setNoteValue(NoteValue::Quarter);          // Quarter note = 2 Hz at 120 BPM
```

**Topology (mix-before-feedback):**
```
Input
  |
  +-- feedbackState * feedback (tanh soft-clipped) --->+
  |                                                    |
  v                                                    |
[Allpass Cascade (N stages)] ---> wet                  |
  |                                                    |
  v                                                    |
[Mix: dry * (1-mix) + wet * mix] ---> output           |
  |                                                    |
  +---------------------------------------------------+
  (feedbackState = output for next sample)
```

**Dependencies:** Layer 0 (db_utils.h, note_value.h), Layer 1 (allpass_1pole.h, lfo.h, smoother.h)

---

## SpectralMorphFilter
**Path:** [spectral_morph_filter.h](../../dsp/include/krate/dsp/processors/spectral_morph_filter.h) | **Since:** 0.14.0

Spectral morphing processor that blends two audio signals by interpolating their magnitude spectra while providing phase source selection, spectral pitch shifting, and spectral tilt control.

**Use when:**
- Creating cross-synthesis effects between two audio sources (vocals/instruments)
- Need spectral "freeze" effects using snapshot mode for static textures
- Want vocoder-like spectral morphing with smooth parameter transitions
- Building hybrid/crossfade effects that blend timbres rather than waveforms
- Creating formant-shifted or "chipmunk/monster" effects via spectral shift

**Features:**
- Dual-input spectral morphing (FR-002) with magnitude interpolation
- Single-input snapshot mode (FR-003) for morphing with captured spectrum
- Phase source selection: A, B, or Blend (complex vector interpolation)
- Spectral pitch shift (+/-24 semitones) via bin rotation
- Spectral tilt (+/-12 dB/octave) with 1 kHz pivot for brightness control
- COLA-compliant overlap-add synthesis (50% overlap with Hann window)
- Parameter smoothing (50ms) for click-free automation
- Real-time safe processing (noexcept, pre-allocated buffers)

```cpp
enum class PhaseSource : uint8_t { A, B, Blend };

class SpectralMorphFilter {
    static constexpr std::size_t kMinFFTSize = 256;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 2048;
    static constexpr float kMinMorphAmount = 0.0f;
    static constexpr float kMaxMorphAmount = 1.0f;
    static constexpr float kMinSpectralShift = -24.0f;
    static constexpr float kMaxSpectralShift = +24.0f;
    static constexpr float kMinSpectralTilt = -12.0f;
    static constexpr float kMaxSpectralTilt = +12.0f;
    static constexpr float kTiltPivotHz = 1000.0f;
    static constexpr float kSmoothingTimeMs = 50.0f;

    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept;
    void reset() noexcept;

    // Dual-input processing (cross-synthesis)
    void processBlock(const float* inputA, const float* inputB,
                      float* output, std::size_t numSamples) noexcept;

    // Single-input processing (snapshot mode)
    [[nodiscard]] float process(float input) noexcept;

    // Snapshot capture
    void captureSnapshot() noexcept;
    void setSnapshotFrameCount(std::size_t frames) noexcept;

    // Morphing parameters
    void setMorphAmount(float amount) noexcept;        // [0, 1]
    void setPhaseSource(PhaseSource source) noexcept;  // A, B, Blend

    // Spectral effects
    void setSpectralShift(float semitones) noexcept;   // [-24, +24]
    void setSpectralTilt(float dBPerOctave) noexcept;  // [-12, +12]

    // Query
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;
    [[nodiscard]] std::size_t getFftSize() const noexcept;
    [[nodiscard]] float getMorphAmount() const noexcept;
    [[nodiscard]] PhaseSource getPhaseSource() const noexcept;
    [[nodiscard]] float getSpectralShift() const noexcept;
    [[nodiscard]] float getSpectralTilt() const noexcept;
    [[nodiscard]] bool hasSnapshot() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| morphAmount | 0.0 | [0, 1] | Magnitude interpolation (0=A, 1=B) |
| phaseSource | A | enum | Phase from A, B, or complex blend |
| spectralShift | 0 | [-24, +24] | Semitone shift via bin rotation |
| spectralTilt | 0 | [-12, +12] | dB/octave with 1kHz pivot |
| snapshotFrameCount | 4 | [1, 16] | Frames to average for snapshot |

**Usage Example (Dual-input morphing):**
```cpp
SpectralMorphFilter filter;
filter.prepare(44100.0, 2048);
filter.setMorphAmount(0.5f);
filter.setPhaseSource(PhaseSource::A);

// In audio callback
filter.processBlock(inputA, inputB, output, blockSize);
```

**Usage Example (Snapshot mode):**
```cpp
SpectralMorphFilter filter;
filter.prepare(44100.0, 2048);

// Capture spectral fingerprint from current input
filter.captureSnapshot();
for (size_t i = 0; i < warmupSamples; ++i) {
    (void)filter.process(input[i]);  // Feed samples for capture
}

// Now morph live input against snapshot
filter.setMorphAmount(0.7f);
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(liveInput[i]);
}
```

**Usage Example (Spectral effects):**
```cpp
SpectralMorphFilter filter;
filter.prepare(44100.0, 2048);
filter.setMorphAmount(0.0f);          // Pure source A
filter.setSpectralShift(12.0f);       // +1 octave shift
filter.setSpectralTilt(6.0f);         // Boost highs +6dB/octave

// Creates "chipmunk" effect with brightness boost
filter.processBlock(input, nullptr, output, blockSize);
```

**Signal Flow:**
```
Input A ---> STFT Analyze ---+
                             |
                             v
                     +-----------------+
                     | Magnitude       |
                     | Interpolation   |<--- morphAmount
                     +-----------------+
                             |
Input B ---> STFT Analyze ---+
                             |
                             v
                     +-----------------+
                     | Phase Selection |<--- phaseSource
                     +-----------------+
                             |
                             v
                     +-----------------+
                     | Spectral Shift  |<--- spectralShift (semitones)
                     +-----------------+
                             |
                             v
                     +-----------------+
                     | Spectral Tilt   |<--- spectralTilt (dB/octave)
                     +-----------------+
                             |
                             v
                     +-----------------+
                     | Overlap-Add     |---> Output
                     | Synthesis       |
                     +-----------------+
```

**Performance:** < 50ms for two 1-second mono buffers at 44.1kHz (SC-001)

**Dependencies:** Layer 0 (math_constants.h, window_functions.h), Layer 1 (fft.h, stft.h, spectral_buffer.h, smoother.h)

---

## SpectralTilt
**Path:** [spectral_tilt.h](../../dsp/include/krate/dsp/processors/spectral_tilt.h) | **Since:** 0.14.0

IIR spectral tilt filter using a single high-shelf biquad for efficient brightness/darkness control with configurable pivot frequency.

**Use when:**
- Creating tonal shaping effects (brightness or darkness control)
- Need efficient spectral slope adjustment in feedback paths
- Want real-time parameter automation without clicks
- Building pre/post-emphasis filters
- Need lightweight alternative to FFT-based spectral tilt (SpectralMorphFilter)

**Features:**
- Configurable tilt amount (-12 to +12 dB/octave)
- Configurable pivot frequency (20 Hz to 20 kHz)
- Parameter smoothing (default 50ms) for click-free automation
- Gain limiting (+24 dB max, -48 dB min) for stability
- Zero latency (pure IIR implementation)
- Passthrough when not prepared

**Note:** This is a single high-shelf approximation that provides good accuracy (~1 dB) within 2-3 octaves of the pivot frequency. For precise frequency-domain tilt, use SpectralMorphFilter with its setSpectralTilt() method.

```cpp
class SpectralTilt {
    static constexpr float kMinTilt = -12.0f;
    static constexpr float kMaxTilt = +12.0f;
    static constexpr float kMinPivot = 20.0f;
    static constexpr float kMaxPivot = 20000.0f;
    static constexpr float kMinSmoothing = 1.0f;
    static constexpr float kMaxSmoothing = 500.0f;
    static constexpr float kDefaultSmoothing = 50.0f;
    static constexpr float kDefaultPivot = 1000.0f;
    static constexpr float kDefaultTilt = 0.0f;
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kMinGainDb = -48.0f;

    void prepare(double sampleRate);
    void reset() noexcept;

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, int numSamples) noexcept;

    void setTilt(float dBPerOctave);          // [-12, +12]
    void setPivotFrequency(float hz);          // [20, 20000]
    void setSmoothing(float ms);               // [1, 500]

    [[nodiscard]] float getTilt() const noexcept;
    [[nodiscard]] float getPivotFrequency() const noexcept;
    [[nodiscard]] float getSmoothing() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| tilt | 0 | [-12, +12] dB/oct | Positive = brighter, Negative = darker |
| pivotFrequency | 1000 Hz | [20, 20000] | Pivot point (0 dB transition) |
| smoothing | 50 ms | [1, 500] | Parameter transition time |

**Usage Example (Brightness control):**
```cpp
SpectralTilt tilt;
tilt.prepare(44100.0);
tilt.setTilt(6.0f);              // +6 dB/octave brightness
tilt.setPivotFrequency(1000.0f); // Pivot at 1 kHz

// In audio callback
for (int i = 0; i < numSamples; ++i) {
    output[i] = tilt.process(input[i]);
}
```

**Usage Example (Block processing):**
```cpp
SpectralTilt tilt;
tilt.prepare(44100.0);
tilt.setTilt(-3.0f);  // Slight darkness
tilt.processBlock(buffer, blockSize);
```

**Topology:**
```
Input --> [OnePoleSmoother (tilt)] --+
      --> [OnePoleSmoother (pivot)] -+--> [Coefficient Calc] --> [High-Shelf Biquad] --> Output
```

**Dependencies:** Layer 0 (db_utils.h, math_constants.h), Layer 1 (biquad.h, smoother.h)

---

## ResonatorBank
**Path:** [resonator_bank.h](../../dsp/include/krate/dsp/processors/resonator_bank.h) | **Since:** 0.14.0

Bank of 16 tuned resonant bandpass filters for physical modeling applications. Supports harmonic, inharmonic, and custom tuning modes with per-resonator control of frequency, decay, gain, and Q.

**Use when:**
- Creating physical modeling effects (marimba bars, bells, strings)
- Need tuned resonance added to percussive or noise sources
- Want harmonic series tuning (strings, flutes) or inharmonic (bells, bars)
- Building pitched percussion synthesizers
- Need spectral shaping via global tilt and damping controls

**Features:**
- 16 parallel bandpass resonators using Biquad filters
- Three tuning modes: Harmonic, Inharmonic, Custom frequencies
- Per-resonator control: frequency, decay (RT60), gain (dB), Q factor
- Global damping to scale all decay times proportionally
- Global spectral tilt for brightness/darkness control (-12 to +12 dB/octave)
- Exciter mix for dry/wet blend
- Trigger function for percussive excitation
- Parameter smoothing (20ms) for click-free automation
- Real-time safe (noexcept, no allocations in process)

```cpp
enum class TuningMode : uint8_t { Harmonic, Inharmonic, Custom };

class ResonatorBank {
    static constexpr size_t kMaxResonators = 16;
    static constexpr float kMinResonatorFrequency = 20.0f;
    static constexpr float kMaxResonatorFrequencyRatio = 0.45f;
    static constexpr float kMinResonatorQ = 0.1f;
    static constexpr float kMaxResonatorQ = 100.0f;
    static constexpr float kMinDecayTime = 0.001f;
    static constexpr float kMaxDecayTime = 30.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Tuning configuration
    void setHarmonicSeries(float fundamentalHz, int numPartials) noexcept;
    void setInharmonicSeries(float baseHz, float inharmonicity) noexcept;
    void setCustomFrequencies(const float* frequencies, size_t count) noexcept;
    [[nodiscard]] TuningMode getTuningMode() const noexcept;
    [[nodiscard]] size_t getNumActiveResonators() const noexcept;

    // Per-resonator control
    void setFrequency(size_t index, float hz) noexcept;
    void setDecay(size_t index, float seconds) noexcept;
    void setGain(size_t index, float dB) noexcept;
    void setQ(size_t index, float q) noexcept;
    void setEnabled(size_t index, bool enabled) noexcept;
    [[nodiscard]] float getFrequency(size_t index) const noexcept;
    [[nodiscard]] float getDecay(size_t index) const noexcept;
    [[nodiscard]] float getGain(size_t index) const noexcept;
    [[nodiscard]] float getQ(size_t index) const noexcept;
    [[nodiscard]] bool isEnabled(size_t index) const noexcept;

    // Global controls
    void setDamping(float amount) noexcept;          // [0, 1] 0=full decay, 1=silent
    void setExciterMix(float amount) noexcept;       // [0, 1] 0=wet, 1=dry
    void setSpectralTilt(float dBPerOctave) noexcept; // [-12, +12]
    [[nodiscard]] float getDamping() const noexcept;
    [[nodiscard]] float getExciterMix() const noexcept;
    [[nodiscard]] float getSpectralTilt() const noexcept;

    // Trigger
    void trigger(float velocity = 1.0f) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| frequency | 440 Hz | [20, sampleRate*0.45] | Center frequency per resonator |
| decay | 1.0 s | [0.001, 30] | RT60 decay time per resonator |
| gain | 0 dB | - | Output gain per resonator |
| Q | 10.0 | [0.1, 100] | Quality factor per resonator |
| damping | 0.0 | [0, 1] | Global decay scaling |
| exciterMix | 0.0 | [0, 1] | Dry/wet mix |
| spectralTilt | 0.0 | [-12, +12] | dB/octave with 1kHz pivot |

**Tuning Modes:**

| Mode | Formula | Use Case |
|------|---------|----------|
| Harmonic | f_n = f_0 * n | Strings, flutes, ideal resonators |
| Inharmonic | f_n = f_0 * n * sqrt(1 + B*n^2) | Bells, marimba bars, stiff strings |
| Custom | User-specified frequencies | Experimental tunings, specific instruments |

**Usage Example (Harmonic series):**
```cpp
ResonatorBank bank;
bank.prepare(44100.0);
bank.setHarmonicSeries(440.0f, 8);  // A4 with 8 partials
bank.setDamping(0.2f);               // Light damping

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = bank.process(input[i]);
}
```

**Usage Example (Percussive trigger):**
```cpp
ResonatorBank bank;
bank.prepare(44100.0);
bank.setInharmonicSeries(110.0f, 0.01f);  // Bell-like partials

// On MIDI note-on
bank.trigger(velocity / 127.0f);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = bank.process(0.0f);  // No audio input needed
}
```

**Usage Example (Custom tuning):**
```cpp
ResonatorBank bank;
bank.prepare(44100.0);

// Gamelan-style tuning
std::array<float, 5> pelog = {293.0f, 329.0f, 349.0f, 415.0f, 466.0f};
bank.setCustomFrequencies(pelog.data(), pelog.size());
```

**Utility Functions:**

```cpp
// Convert RT60 decay time to filter Q
[[nodiscard]] float rt60ToQ(float frequency, float rt60Seconds) noexcept;

// Calculate inharmonic partial frequency
[[nodiscard]] float calculateInharmonicFrequency(
    float fundamental, int partial, float inharmonicity) noexcept;

// Calculate spectral tilt gain for a frequency
[[nodiscard]] float calculateTiltGain(
    float frequency, float tiltDbPerOctave) noexcept;
```

**Topology:**
```
                   +------------------------+
                   | Bandpass Resonator 0   |---+
                   +------------------------+   |
                                                |
Input ----+------->| Bandpass Resonator 1   |---+----> [Spectral Tilt] --> [Exciter Mix] --> Output
          |        +------------------------+   |
          |                   ...               |
          |        +------------------------+   |
          +------->| Bandpass Resonator 15  |---+
                   +------------------------+

Each resonator: Input -> [Biquad Bandpass @ freq, Q] -> [Gain] -> [Damping Scale] -> [Tilt Gain]
```

**Dependencies:** Layer 0 (db_utils.h, math_constants.h), Layer 1 (biquad.h, smoother.h)

---

## KarplusStrong
**Path:** [karplus_strong.h](../../dsp/include/krate/dsp/processors/karplus_strong.h) | **Since:** 0.14.0

Karplus-Strong plucked string synthesizer for realistic plucked and bowed string sounds. Implements the classic algorithm with extensions for tone shaping, inharmonicity, and continuous excitation.

**Use when:**
- Creating realistic plucked string sounds (guitar, harp, harpsichord, koto)
- Need sustained/bowed string sounds with infinite sustain capability
- Want piano-like inharmonicity for bell-like or metallic timbres
- Building physical modeling instruments or effects
- Need resonant string response to external audio (sympathetic resonance)

**Features:**
- Allpass fractional delay interpolation for pitch accuracy within 20 cents
- Noise burst excitation with configurable brightness filtering
- Pick position simulation via comb filtering
- Damping control for high-frequency loss (tone darkening)
- Decay time control (RT60) with feedback coefficient limiting
- Inharmonicity (stretch) via allpass dispersion for piano/bell character
- Continuous bowing mode for sustained tones
- Custom excitation signal injection for hybrid sounds
- DC blocking in feedback path
- External audio input for sympathetic resonance
- All methods noexcept for real-time safety

```cpp
class KarplusStrong {
    void prepare(double sampleRate, float minFrequency = 20.0f) noexcept;
    void reset() noexcept;

    // Pitch control
    void setFrequency(float hz) noexcept;           // [minFrequency, Nyquist*0.5]
    void setDecay(float seconds) noexcept;          // RT60 decay time
    void setDamping(float amount) noexcept;         // [0, 1] HF loss
    void setBrightness(float amount) noexcept;      // [0, 1] excitation spectrum
    void setPickPosition(float position) noexcept;  // [0, 1] comb filtering
    void setStretch(float amount) noexcept;         // [0, 1] inharmonicity

    // Excitation
    void pluck(float velocity = 1.0f) noexcept;     // Noise burst
    void bow(float pressure) noexcept;              // Continuous excitation
    void excite(const float* signal, size_t length) noexcept;  // Custom

    // Processing
    [[nodiscard]] float process(float input = 0.0f) noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| frequency | 440 Hz | [minFreq, Nyquist*0.5] | Fundamental pitch |
| decay | 1.0 s | [0.001, 60] | RT60 decay time |
| damping | 0.3 | [0, 1] | 0=bright, 1=dark |
| brightness | 1.0 | [0, 1] | Excitation HF content |
| pickPosition | 0.0 | [0, 1] | 0=bridge, 0.5=middle, 1=nut |
| stretch | 0.0 | [0, 1] | 0=harmonic, 1=bell-like |

**Excitation Modes:**

| Mode | Method | Use Case |
|------|--------|----------|
| Pluck | pluck(velocity) | Guitar, harp, harpsichord |
| Bow | bow(pressure) | Sustained strings, drones |
| Custom | excite(signal, length) | Hybrid synthesis, special effects |

**Usage Example (Basic pluck):**
```cpp
KarplusStrong ks;
ks.prepare(44100.0, 20.0f);
ks.setFrequency(440.0f);
ks.setDecay(2.0f);
ks.setDamping(0.3f);

ks.pluck(1.0f);  // Full velocity pluck

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = ks.process();
}
```

**Usage Example (Sustained bowing):**
```cpp
KarplusStrong ks;
ks.prepare(44100.0, 20.0f);
ks.setFrequency(220.0f);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    ks.bow(0.5f);  // Continuous pressure
    output[i] = ks.process();
}
```

**Usage Example (Piano-like inharmonicity):**
```cpp
KarplusStrong ks;
ks.prepare(44100.0, 20.0f);
ks.setFrequency(440.0f);
ks.setStretch(0.3f);  // Moderate inharmonicity
ks.pluck(1.0f);
```

**Topology:**
```
Excitation (pluck/bow/excite) --> [TwoPoleLP brightness]
                                        |
                                        v
                                  (fills delay line with pick position comb)

Feedback loop:
DelayLine --> OnePoleLP --> Allpass1Pole --> DCBlocker2 --> * feedback --> DelayLine
(allpass)    (damping)     (stretch)        (DC block)
                                        |
                                        v
                                     Output
```

**Dependencies:** Layer 0 (random.h, db_utils.h), Layer 1 (delay_line.h, one_pole.h, two_pole_lp.h, allpass_1pole.h, dc_blocker.h)

---

### WaveguideResonator

**File:** `dsp/include/krate/dsp/processors/waveguide_resonator.h`

**Purpose:** Digital waveguide resonator for flute/pipe-like resonances with configurable end reflections, frequency-dependent loss, dispersion for inharmonicity, and excitation point control.

**When to use this:**
- For modeling blown/sustained pipe and flute resonances
- When you need configurable end reflections (open-open, closed-closed, open-closed)
- For resonant effects with frequency-dependent damping
- When inharmonicity via dispersion is desired (bell-like tones)
- Compare to KarplusStrong for plucked string synthesis

**Public API:**
```cpp
class WaveguideResonator {
public:
    // Lifecycle
    void prepare(double sampleRate, float maxDelaySeconds = 0.1f) noexcept;
    void reset() noexcept;

    // Frequency control (FR-001, FR-002)
    void setFrequency(float hz) noexcept;
    [[nodiscard]] float getFrequency() const noexcept;

    // End reflection control (FR-003, FR-004, FR-005)
    void setEndReflection(float left, float right) noexcept;
    [[nodiscard]] float getLeftReflection() const noexcept;
    [[nodiscard]] float getRightReflection() const noexcept;

    // Loss/damping control (FR-008, FR-009, FR-010)
    void setLoss(float amount) noexcept;
    [[nodiscard]] float getLoss() const noexcept;

    // Dispersion/inharmonicity control (FR-011, FR-012, FR-013)
    void setDispersion(float amount) noexcept;
    [[nodiscard]] float getDispersion() const noexcept;

    // Excitation point control (FR-014, FR-015, FR-016)
    void setExcitationPoint(float position) noexcept;
    [[nodiscard]] float getExcitationPoint() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;
};
```

**Key Features:**
- **End Reflections:** Configurable reflection coefficients for left and right ends (-1 to +1)
  - Open-Open (-1, -1): All harmonics, flute-like
  - Closed-Closed (+1, +1): All harmonics, organ pipe
  - Open-Closed (-1, +1): Odd harmonics only, clarinet-like
- **Frequency-Dependent Loss:** OnePoleLP filters simulate air absorption (highs decay faster)
- **Dispersion:** Allpass filter introduces inharmonicity for bell-like tones
- **Excitation Point:** Position affects harmonic content via comb filtering effect

**Usage Example (Basic flute-like resonance):**
```cpp
WaveguideResonator waveguide;
waveguide.prepare(44100.0);

waveguide.setFrequency(440.0f);           // A4 pitch
waveguide.setEndReflection(-1.0f, -1.0f); // Open-open (flute)
waveguide.setLoss(0.1f);                  // Light damping
waveguide.setDispersion(0.0f);            // Pure harmonics
waveguide.setExcitationPoint(0.5f);       // Center excitation

// Excite with noise or impulse
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = waveguide.process(input[i]);
}
```

**Usage Example (Clarinet-like odd harmonics):**
```cpp
WaveguideResonator waveguide;
waveguide.prepare(44100.0);

waveguide.setFrequency(440.0f);           // Set to 2x desired fundamental
waveguide.setEndReflection(-1.0f, +1.0f); // Open-closed (clarinet)
waveguide.setLoss(0.15f);
// Produces odd harmonics only (fundamental at 220 Hz)
```

**Usage Example (Bell-like inharmonic tone):**
```cpp
WaveguideResonator waveguide;
waveguide.prepare(44100.0);

waveguide.setFrequency(440.0f);
waveguide.setEndReflection(-1.0f, -1.0f);
waveguide.setDispersion(0.5f);  // Shifts partials away from harmonics
waveguide.setLoss(0.2f);
```

**Topology:**
```
Input --> [Excitation Point Distribution]
                    |
                    v
              [DelayLine]
                    |
                    v
          [OnePoleLP loss] --> [Allpass1Pole dispersion]
                    |
                    v
      * leftReflection_ * rightReflection_ (combined reflection)
                    |
                    v
          [Feedback to DelayLine]
                    |
                    v
    [Excitation Point Output Tap] --> Output
```

**Dependencies:** Layer 0 (none), Layer 1 (delay_line.h, one_pole.h, allpass_1pole.h, one_pole_smoother.h)

---

## ModalResonator
**Path:** [modal_resonator.h](../../dsp/include/krate/dsp/processors/modal_resonator.h) | **Since:** 0.14.0

Modal resonator modeling vibrating bodies as a sum of decaying sinusoidal modes for physically accurate resonance of complex bodies like bells, bars, and plates.

**Use when:**
- Creating physically accurate resonant bodies (bells, marimba bars, gongs, plates)
- Need independent T60 decay control per mode (not Q-based like ResonatorBank)
- Want material presets for quick timbre selection (Wood, Metal, Glass, Ceramic, Nylon)
- Building percussion synthesizers that respond to velocity
- Need strike excitation with energy accumulation for realistic percussion
- Require precise T60 decay timing (within 10%) for physical modeling

**Differentiators from ResonatorBank:**
- **Oscillator topology:** Two-pole decaying sinusoidal oscillators vs bandpass biquad filters
- **Decay accuracy:** T60 decay time accurate within 10% vs Q-based approximation
- **Strike behavior:** Energy accumulation (multiple strikes add energy) vs trigger impulse
- **Mode count:** 32 modes vs 16 resonators
- **Material presets:** Frequency-dependent decay based on physical material properties

**Features:**
- 32 parallel modes using impulse-invariant two-pole oscillators
- Per-mode control: frequency, T60 decay time, amplitude
- Material presets with frequency-dependent decay (R_k = b1 + b3*f^2)
- Size scaling (inverse frequency relationship for larger/smaller objects)
- Global damping (scales all decay times proportionally)
- Strike excitation with velocity scaling and energy accumulation
- Parameter smoothing (default 20ms) for click-free automation
- Real-time safe (noexcept, zero allocations in process)

```cpp
struct ModalData {
    float frequency;   // Mode frequency in Hz [20, sampleRate * 0.45]
    float t60;         // Decay time in seconds (RT60) [0.001, 30.0]
    float amplitude;   // Mode amplitude [0.0, 1.0]
};

enum class Material : uint8_t { Wood, Metal, Glass, Ceramic, Nylon };

class ModalResonator {
    static constexpr size_t kMaxModes = 32;
    static constexpr float kMinModeFrequency = 20.0f;
    static constexpr float kMaxModeFrequencyRatio = 0.45f;
    static constexpr float kMinModeDecay = 0.001f;
    static constexpr float kMaxModeDecay = 30.0f;
    static constexpr float kMinSizeScale = 0.1f;
    static constexpr float kMaxSizeScale = 10.0f;

    explicit ModalResonator(float smoothingTimeMs = 20.0f) noexcept;
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Per-mode control
    void setModeFrequency(int index, float hz) noexcept;
    void setModeDecay(int index, float t60Seconds) noexcept;
    void setModeAmplitude(int index, float amplitude) noexcept;
    void setModes(const ModalData* modes, int count) noexcept;

    // Material presets
    void setMaterial(Material mat) noexcept;

    // Global controls
    void setSize(float scale) noexcept;       // [0.1, 10.0] inverse freq scaling
    void setDamping(float amount) noexcept;   // [0.0, 1.0] decay reduction

    // Excitation
    void strike(float velocity = 1.0f) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, int numSamples) noexcept;

    // Query
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] int getNumActiveModes() const noexcept;
    [[nodiscard]] float getModeFrequency(int index) const noexcept;
    [[nodiscard]] float getModeDecay(int index) const noexcept;
    [[nodiscard]] float getModeAmplitude(int index) const noexcept;
    [[nodiscard]] bool isModeEnabled(int index) const noexcept;
    [[nodiscard]] float getSize() const noexcept;
    [[nodiscard]] float getDamping() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| frequency | 440 Hz | [20, sampleRate*0.45] | Mode center frequency |
| t60 | 1.0 s | [0.001, 30] | Time to decay by 60 dB |
| amplitude | 0.0 | [0, 1] | Mode output level |
| size | 1.0 | [0.1, 10] | Frequency scaling (2.0 = octave down) |
| damping | 0.0 | [0, 1] | Decay reduction (1.0 = instant silence) |

**Material Presets:**

| Material | b1 (Hz) | b3 (s) | Character |
|----------|---------|--------|-----------|
| Wood | 2.0 | 1e-7 | Warm, quick HF decay (marimba-like) |
| Metal | 0.3 | 1e-9 | Bright, sustained (bell-like) |
| Glass | 0.5 | 5e-8 | Bright, ringing (glass bowl-like) |
| Ceramic | 1.5 | 8e-8 | Warm/bright, medium (tile-like) |
| Nylon | 4.0 | 2e-7 | Dull, heavily damped (string-like) |

**Usage Example (Basic percussion):**
```cpp
ModalResonator resonator;
resonator.prepare(44100.0);
resonator.setMaterial(Material::Metal);

// Strike with velocity
resonator.strike(1.0f);

// In audio callback
for (int i = 0; i < numSamples; ++i) {
    output[i] = resonator.process(0.0f);
}
```

**Usage Example (Custom mode configuration):**
```cpp
ModalResonator resonator;
resonator.prepare(44100.0);

// Configure bell-like inharmonic partials
std::array<ModalData, 6> modes = {{
    {440.0f, 3.0f, 1.0f},      // Fundamental
    {687.0f, 2.5f, 0.8f},      // 1.56x (inharmonic)
    {1023.0f, 2.0f, 0.6f},     // 2.33x
    {1479.0f, 1.5f, 0.4f},     // 3.36x
    {2012.0f, 1.0f, 0.3f},     // 4.57x
    {2631.0f, 0.8f, 0.2f}      // 5.98x
}};
resonator.setModes(modes.data(), modes.size());

resonator.strike(0.8f);
```

**Usage Example (Size and damping control):**
```cpp
ModalResonator resonator;
resonator.prepare(44100.0);
resonator.setMaterial(Material::Wood);

// Make it sound like a larger instrument (lower pitch)
resonator.setSize(2.0f);  // Frequencies halved

// Add some dampening (like a muted marimba)
resonator.setDamping(0.3f);  // 30% decay reduction

resonator.strike(1.0f);
```

**Usage Example (Resonant filter effect):**
```cpp
ModalResonator resonator;
resonator.prepare(44100.0);

// Configure resonant modes
resonator.setModeFrequency(0, 440.0f);
resonator.setModeDecay(0, 0.5f);
resonator.setModeAmplitude(0, 1.0f);

// Process audio through the resonator (acts as filter)
for (int i = 0; i < numSamples; ++i) {
    output[i] = resonator.process(input[i]);
}
```

**Topology:**
```
                   +---------------------------+
strike(vel) ---+-->| Mode 0: y[n] = in*amp +   |---+
               |   | a1*y[n-1] - a2*y[n-2]     |   |
               |   +---------------------------+   |
               |                                   |
Input -----+---+-->| Mode 1: two-pole oscillator|---+---> Sum ---> Output
           |   |   +---------------------------+   |
           |   |              ...                  |
           |   |   +---------------------------+   |
           +---+-->| Mode 31: two-pole oscillator|--+
                   +---------------------------+

Each mode: y[n] = input * amplitude + 2*R*cos(theta)*y[n-1] - R^2*y[n-2]
where: R = exp(-6.91 / (T60 * sampleRate)), theta = 2*pi*freq/sampleRate
```

**Performance:** < 1% CPU @ 192kHz for 32 modes (target: 26.7 microseconds per 512-sample block)

**Dependencies:** Layer 0 (db_utils.h, math_constants.h), Layer 1 (smoother.h)

---

## StochasticFilter
**Path:** [stochastic_filter.h](../../dsp/include/krate/dsp/processors/stochastic_filter.h) | **Since:** 0.15.0

Filter with stochastically varying parameters (cutoff, Q, type) for experimental sound design and evolving textures.

**Use when:**
- Creating evolving pad textures with organic filter movement (Walk mode)
- Building glitch effects with sudden random filter changes (Jump mode)
- Need complex, non-repeating modulation patterns (Lorenz chaotic attractor)
- Want smooth, coherent random modulation (Perlin noise)
- Experimenting with random filter type switching

**Features:**
- Four random modulation modes: Walk (Brownian), Jump (discrete), Lorenz (chaotic), Perlin (coherent noise)
- Randomizable cutoff (octave-based range), resonance (Q), and filter type
- Independent enable/disable for each randomizable parameter
- Deterministic seeding for reproducible behavior
- Click-free transitions via parameter smoothing and parallel crossfade (type switching)
- Control-rate updates (32 samples) for CPU efficiency
- Uses TPT SVF for modulation stability

```cpp
enum class RandomMode : uint8_t { Walk, Jump, Lorenz, Perlin };

namespace FilterTypeMask {
    constexpr uint8_t Lowpass = 0x01, Highpass = 0x02, Bandpass = 0x04;
    constexpr uint8_t Notch = 0x08, Allpass = 0x10, Peak = 0x20;
    constexpr uint8_t LowShelf = 0x40, HighShelf = 0x80, All = 0xFF;
}

class StochasticFilter {
    static constexpr float kMinChangeRate = 0.01f;
    static constexpr float kMaxChangeRate = 100.0f;
    static constexpr float kDefaultChangeRate = 1.0f;
    static constexpr float kMinSmoothing = 0.0f;
    static constexpr float kMaxSmoothing = 1000.0f;
    static constexpr float kDefaultSmoothing = 50.0f;
    static constexpr float kMinOctaveRange = 0.0f;
    static constexpr float kMaxOctaveRange = 8.0f;
    static constexpr size_t kControlRateInterval = 32;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Mode selection
    void setMode(RandomMode mode) noexcept;
    [[nodiscard]] RandomMode getMode() const noexcept;

    // Randomization enable
    void setCutoffRandomEnabled(bool enabled) noexcept;
    void setResonanceRandomEnabled(bool enabled) noexcept;
    void setTypeRandomEnabled(bool enabled) noexcept;

    // Base parameters
    void setBaseCutoff(float hz) noexcept;
    void setBaseResonance(float q) noexcept;
    void setBaseFilterType(SVFMode type) noexcept;

    // Ranges
    void setCutoffOctaveRange(float octaves) noexcept;  // [0, 8]
    void setResonanceRange(float range) noexcept;       // [0, 1]
    void setEnabledFilterTypes(uint8_t mask) noexcept;  // FilterTypeMask bitmask

    // Control
    void setChangeRate(float hz) noexcept;              // [0.01, 100]
    void setSmoothingTime(float ms) noexcept;           // [0, 1000]
    void setSeed(uint32_t seed) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
};
```

| Mode | Character | Use Case |
|------|-----------|----------|
| Walk | Smooth drift, bounded | Evolving pad textures |
| Jump | Discrete, sudden | Glitch effects, rhythmic chaos |
| Lorenz | Chaotic, non-repeating | Complex evolving modulation |
| Perlin | Smooth, band-limited | Organic coherent movement |

**Usage Example (Evolving pad with Walk mode):**
```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);
filter.setMode(RandomMode::Walk);
filter.setCutoffRandomEnabled(true);
filter.setBaseCutoff(1000.0f);
filter.setCutoffOctaveRange(2.0f);  // +/- 2 octaves
filter.setChangeRate(1.0f);         // 1 Hz drift rate
filter.setSeed(42);                 // Reproducible

// In audio callback
filter.processBlock(buffer, numSamples);
```

**Usage Example (Glitchy rhythm with Jump mode):**
```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);
filter.setMode(RandomMode::Jump);
filter.setCutoffRandomEnabled(true);
filter.setBaseCutoff(2000.0f);
filter.setCutoffOctaveRange(4.0f);
filter.setChangeRate(8.0f);         // 8 jumps per second
filter.setSmoothingTime(20.0f);     // Click-free

filter.processBlock(buffer, numSamples);
```

**Topology:**
```
                    +-------------------+
                    | Random Generator  |
                    | (Walk/Jump/Lorenz/|
                    |  Perlin based on  |
                    |  mode + seed)     |
                    +-------------------+
                            |
                    +-------v-------+
                    | Modulation    |
                    | Scaling       |
                    | (octave/range)|
                    +---------------+
                            |
                    +-------v-------+       +-------+
                    | Parameter     |<------| One-  |
                    | Smoother      |       | Pole  |
                    +---------------+       +-------+
                            |
                            v
Input -----> [FilterA (SVF)] --+---> Output
                               |
                  (when transitioning types)
                               |
             [FilterB (SVF)] --+
                  (crossfade)
```

**Performance:** < 0.5% CPU @ 44.1kHz stereo per instance (control-rate updates every 32 samples)

**Dependencies:** Layer 0 (random.h), Layer 1 (svf.h, smoother.h)
