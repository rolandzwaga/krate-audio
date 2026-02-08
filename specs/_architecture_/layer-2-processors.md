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

## FrequencyShifter
**Path:** [frequency_shifter.h](../../dsp/include/krate/dsp/processors/frequency_shifter.h) | **Since:** 0.13.0

Frequency shifter using Hilbert transform for single-sideband modulation. Shifts all frequencies by a constant Hz amount (not pitch shifting), creating inharmonic, metallic effects. Based on the Bode frequency shifter principle.

**Use when:**
- Creating inharmonic, metallic, bell-like textures
- Building Shepard tone / barber pole effects (with feedback)
- Need stereo widening with complementary frequency content
- Creating ring modulation effects (Both mode)
- Want animated modulation with LFO-controlled shift

**Unlike pitch shifting**, frequency shifting adds/subtracts a fixed Hz value:
- Pitch shift: 200Hz -> 400Hz, 400Hz -> 800Hz (preserves harmonics)
- Freq shift +100Hz: 200Hz -> 300Hz, 400Hz -> 500Hz (destroys harmonic relationship)

**Features:**
- Three direction modes: Up (upper sideband), Down (lower), Both (ring mod)
- LFO modulation of shift amount with configurable rate and depth
- Feedback path with tanh saturation for spiraling Shepard-tone effects
- Stereo mode: L=+shift, R=-shift for width
- Click-free parameter smoothing
- Quadrature oscillator with periodic renormalization (every 1024 samples)

```cpp
enum class ShiftDirection : uint8_t { Up, Down, Both };

class FrequencyShifter {
    static constexpr float kMaxShiftHz = 5000.0f;
    static constexpr float kMaxModDepthHz = 500.0f;
    static constexpr float kMaxFeedback = 0.99f;
    static constexpr float kMinModRate = 0.01f;
    static constexpr float kMaxModRate = 20.0f;
    static constexpr int kRenormInterval = 1024;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    void setShiftAmount(float hz) noexcept;         // [-5000, +5000] Hz
    void setDirection(ShiftDirection dir) noexcept;
    void setModRate(float hz) noexcept;             // [0.01, 20] Hz
    void setModDepth(float hz) noexcept;            // [0, 500] Hz
    void setFeedback(float amount) noexcept;        // [0.0, 0.99]
    void setMix(float dryWet) noexcept;             // [0.0, 1.0]

    [[nodiscard]] float process(float input) noexcept;
    void processStereo(float& left, float& right) noexcept;

    [[nodiscard]] float getShiftAmount() const noexcept;
    [[nodiscard]] ShiftDirection getDirection() const noexcept;
    [[nodiscard]] float getModRate() const noexcept;
    [[nodiscard]] float getModDepth() const noexcept;
    [[nodiscard]] float getFeedback() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
};
```

**SSB Formulas (I = in-phase, Q = quadrature from Hilbert, w = carrier freq):**
- Up: `output = I*cos(wt) - Q*sin(wt)` (upper sideband)
- Down: `output = I*cos(wt) + Q*sin(wt)` (lower sideband)
- Both: `output = I*cos(wt)` (ring modulation, Q terms cancel)

**Usage Example (Basic Frequency Shift):**
```cpp
FrequencyShifter shifter;
shifter.prepare(44100.0);
shifter.setShiftAmount(100.0f);        // +100Hz shift
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.0f);
shifter.setMix(1.0f);

for (size_t i = 0; i < numSamples; ++i) {
    output[i] = shifter.process(input[i]);
}
```

**Usage Example (Shepard Tone / Spiraling Effect):**
```cpp
FrequencyShifter shifter;
shifter.prepare(44100.0);
shifter.setShiftAmount(50.0f);         // Small shift for slow spiral
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.7f);             // High feedback for spiraling
shifter.setMix(1.0f);
```

**Usage Example (Stereo Widening):**
```cpp
FrequencyShifter shifter;
shifter.prepare(44100.0);
shifter.setShiftAmount(5.0f);          // Subtle shift for width
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.0f);
shifter.setMix(0.5f);                  // Blend with dry

// Process stereo: L=+5Hz, R=-5Hz
for (size_t i = 0; i < numSamples; ++i) {
    shifter.processStereo(left[i], right[i]);
}
```

**Usage Example (LFO Modulated Shift):**
```cpp
shifter.setShiftAmount(50.0f);         // Base shift
shifter.setModRate(0.5f);              // 0.5Hz LFO
shifter.setModDepth(30.0f);            // +/-30Hz variation
// Effective shift oscillates between 20Hz and 80Hz
```

**Gotchas:**
- 5-sample latency from Hilbert transform (not compensated)
- Aliasing at extreme shifts (>Nyquist/2) - documented limitation, no oversampling
- Feedback >99% is clamped to prevent infinite sustain
- NaN/Inf input resets state and returns 0.0f
- prepare() is NOT real-time safe (LFO allocates wavetables)

**Dependencies:** Layer 0 (math_constants.h, db_utils.h), Layer 1 (hilbert_transform.h, lfo.h, smoother.h)

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

## FormantDistortion
**Path:** [formant_distortion.h](../../dsp/include/krate/dsp/processors/formant_distortion.h) | **Since:** 0.14.0

Composite processor combining vocal-tract resonances with waveshaping saturation for "talking distortion" effects.

**Use when:**
- Creating vocal-style distortion with distinct vowel character
- Need dynamic formant effects that respond to playing dynamics
- Building expressive saturation with "talking" quality
- Want to combine formant filtering with selectable distortion types
- Creating alien or robot-voice textures with formant shifting

**Features:**
- Vowel selection (A, E, I, O, U) with discrete and blend modes
- Continuous vowel morphing (0-4 position) for animated "talking" effects
- Formant shifting (+/-24 semitones) for character adjustment
- 9 distortion types: Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Diode, Tube
- Envelope-controlled formant modulation for dynamic response
- DC blocking after asymmetric distortion
- Dry/wet mix control

```cpp
class FormantDistortion {
    static constexpr float kMinDrive = 0.5f;
    static constexpr float kMaxDrive = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinEnvModRange = 0.0f;
    static constexpr float kMaxEnvModRange = 24.0f;
    static constexpr float kDefaultEnvModRange = 12.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float sample) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Vowel selection
    void setVowel(Vowel vowel) noexcept;           // Discrete: A, E, I, O, U
    void setVowelBlend(float blend) noexcept;      // Continuous: [0, 4]

    // Formant modification
    void setFormantShift(float semitones) noexcept; // [-24, +24] semitones

    // Distortion
    void setDistortionType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;           // [0.5, 20.0]

    // Envelope following
    void setEnvelopeFollowAmount(float amount) noexcept;  // [0, 1]
    void setEnvelopeModRange(float semitones) noexcept;   // [0, 24]
    void setEnvelopeAttack(float ms) noexcept;
    void setEnvelopeRelease(float ms) noexcept;

    // Smoothing and mix
    void setSmoothingTime(float ms) noexcept;
    void setMix(float mix) noexcept;               // [0, 1]

    // Getters
    [[nodiscard]] Vowel getVowel() const noexcept;
    [[nodiscard]] float getVowelBlend() const noexcept;
    [[nodiscard]] float getFormantShift() const noexcept;
    [[nodiscard]] WaveshapeType getDistortionType() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float getEnvelopeFollowAmount() const noexcept;
    [[nodiscard]] float getEnvelopeModRange() const noexcept;
    [[nodiscard]] float getSmoothingTime() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
};
```

**Signal Flow:**
```
                     +----------------+
                     | EnvelopeFollow |<--- Raw Input (tracking)
                     +--------+-------+
                              |
                              v
Input ---> FormantFilter <----+-----> Waveshaper ---> DCBlocker --+---> Output
           (vowel shape)     (shift)  (saturation)    (cleanup)   |
                                                                  v
                                                              Mix Stage
                                                                  ^
                                                                  |
Input ------------------------------------------------------> Dry Signal
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| vowel | A | enum | Discrete vowel selection |
| vowelBlend | 0.0 | [0, 4] | Continuous position (0=A, 1=E, 2=I, 3=O, 4=U) |
| formantShift | 0 | [-24, +24] | Semitone shift applied to formants |
| drive | 1.0 | [0.5, 20] | Waveshaper drive multiplier |
| distortionType | Tanh | enum | Waveshaping algorithm |
| envelopeFollowAmount | 0.0 | [0, 1] | Envelope modulation depth |
| envelopeModRange | 12.0 | [0, 24] | Max envelope-driven shift (semitones) |
| mix | 1.0 | [0, 1] | Dry/wet blend |

**Envelope Modulation Formula:**
```
finalShift = staticShift + (envelope * modRange * amount)
```
Where envelope is [0, 1], modRange is semitones, and amount is [0, 1].

**Usage Example (Basic vowel distortion):**
```cpp
FormantDistortion fx;
fx.prepare(44100.0, 512);
fx.setVowel(Vowel::A);
fx.setDrive(3.0f);
fx.setDistortionType(WaveshapeType::Tanh);

// In audio callback
fx.process(buffer, numSamples);
```

**Usage Example (Dynamic wah-like effect):**
```cpp
FormantDistortion fx;
fx.prepare(44100.0, 512);
fx.setVowel(Vowel::A);
fx.setFormantShift(-6.0f);
fx.setEnvelopeFollowAmount(1.0f);
fx.setEnvelopeModRange(18.0f);
fx.setEnvelopeAttack(5.0f);
fx.setEnvelopeRelease(50.0f);

// Playing dynamics control formant sweep
fx.process(buffer, numSamples);
```

**Usage Example (Talking lead synth):**
```cpp
FormantDistortion fx;
fx.prepare(44100.0, 512);
fx.setDistortionType(WaveshapeType::Tube);
fx.setDrive(3.0f);
fx.setEnvelopeFollowAmount(0.5f);

// Automate vowelBlend from 0 to 4 for "aeiou" effect
for (size_t i = 0; i < numSamples; ++i) {
    fx.setVowelBlend(lfoValue);  // LFO sweeps 0-4
    output[i] = fx.process(input[i]);
}
```

**Gotchas:**
- setVowel() activates discrete mode, setVowelBlend() activates blend mode
- Both vowel values are stored independently; mode determines which is used
- Envelope tracks raw input (before any processing) for consistent response
- DC blocker removes offset from asymmetric distortion (Tube, Diode)
- reset() snaps mix smoother to target for immediate parameter response

**Performance:** < 0.5% CPU at 44.1kHz (Layer 2 processor budget)

**Dependencies:** Layer 0 (filter_tables.h), Layer 1 (waveshaper.h, dc_blocker.h, smoother.h), Layer 2 (formant_filter.h, envelope_follower.h)

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

## MultiStageEnvelopeFilter
**Path:** [multistage_env_filter.h](../../dsp/include/krate/dsp/processors/multistage_env_filter.h) | **Since:** 0.15.0

Complex envelope shapes beyond ADSR driving filter movement for evolving pads and textures. Supports up to 8 programmable stages with independent target frequency, transition time, and curve shape.

**Use when:**
- Creating filter sweeps with complex, evolving shapes (pads, ambient textures)
- Need rhythmic filter patterns via envelope looping
- Want expressive filter sweeps with velocity sensitivity
- Building filter animations with logarithmic/exponential curves
- Need programmatic multi-stage filter automation

**Differentiators from EnvelopeFilter:**
- EnvelopeFilter: Input amplitude-driven (auto-wah), single attack/release response
- MultiStageEnvelopeFilter: Programmable stages with independent targets, times, curves, looping

**Features:**
- Up to 8 programmable stages with target frequency, time (0-10000ms), and curve shape
- Three curve types per stage: logarithmic (curve=-1), linear (curve=0), exponential (curve=+1)
- Loopable envelope section (configurable loop start/end) for rhythmic patterns
- Velocity-sensitive modulation depth scaling
- Independent release time (not tied to stage times)
- Three filter types: Lowpass, Bandpass, Highpass (SVF 12dB/oct)
- Smooth transitions at loop points (no clicks)
- Real-time safe: all methods noexcept, zero allocations in process

```cpp
enum class EnvelopeState : uint8_t { Idle, Running, Releasing, Complete };

struct EnvelopeStage {
    float targetHz = 1000.0f;  // Target cutoff [1, sampleRate*0.45] Hz
    float timeMs = 100.0f;     // Transition time [0, 10000] ms
    float curve = 0.0f;        // Curve shape [-1 (log), 0 (linear), +1 (exp)]
};

class MultiStageEnvelopeFilter {
    static constexpr int kMaxStages = 8;
    static constexpr float kMinResonance = 0.1f;
    static constexpr float kMaxResonance = 30.0f;
    static constexpr float kMinFrequency = 1.0f;
    static constexpr float kMaxStageTimeMs = 10000.0f;
    static constexpr float kMaxReleaseTimeMs = 10000.0f;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Stage configuration
    void setNumStages(int stages) noexcept;              // [1, 8]
    void setStageTarget(int stage, float cutoffHz) noexcept;
    void setStageTime(int stage, float ms) noexcept;     // [0, 10000]
    void setStageCurve(int stage, float curve) noexcept; // [-1, +1]

    // Loop control
    void setLoop(bool enabled) noexcept;
    void setLoopStart(int stage) noexcept;
    void setLoopEnd(int stage) noexcept;

    // Filter settings
    void setResonance(float q) noexcept;                 // [0.1, 30.0]
    void setFilterType(SVFMode type) noexcept;           // Lowpass, Bandpass, Highpass
    void setBaseFrequency(float hz) noexcept;            // Starting frequency

    // Trigger & control
    void trigger() noexcept;                             // Start from stage 0
    void trigger(float velocity) noexcept;               // With velocity [0, 1]
    void release() noexcept;                             // Exit loop, decay to base
    void setReleaseTime(float ms) noexcept;              // [0, 10000]
    void setVelocitySensitivity(float amount) noexcept;  // [0, 1]

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // State monitoring
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] int getCurrentStage() const noexcept;
    [[nodiscard]] float getEnvelopeValue() const noexcept;  // [0, 1] within stage
    [[nodiscard]] bool isComplete() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| numStages | 1 | [1, 8] | Number of active stages |
| stageTarget | 1000 Hz | [1, sampleRate*0.45] | Target cutoff per stage |
| stageTime | 100 ms | [0, 10000] | Transition time per stage |
| stageCurve | 0.0 | [-1, +1] | Curve shape per stage |
| baseFrequency | 100 Hz | [1, sampleRate*0.45] | Starting/release frequency |
| resonance | 0.7071 | [0.1, 30] | Filter Q factor |
| releaseTime | 500 ms | [0, 10000] | Decay time after release() |
| velocitySensitivity | 0.0 | [0, 1] | How much velocity affects depth |

**Curve Shapes:**

| Curve | Value | Effect | Use Case |
|-------|-------|--------|----------|
| Logarithmic | -1.0 | Fast start, slow finish | Punchy attacks |
| Linear | 0.0 | Constant rate | Predictable sweeps |
| Exponential | +1.0 | Slow start, fast finish | Dramatic plunges |

**Usage Example (Basic 4-stage sweep):**
```cpp
MultiStageEnvelopeFilter filter;
filter.prepare(44100.0);

filter.setNumStages(4);
filter.setStageTarget(0, 200.0f);   // Stage 0: 200 Hz
filter.setStageTarget(1, 2000.0f);  // Stage 1: 2000 Hz
filter.setStageTarget(2, 500.0f);   // Stage 2: 500 Hz
filter.setStageTarget(3, 800.0f);   // Stage 3: 800 Hz
filter.setStageTime(0, 100.0f);
filter.setStageTime(1, 200.0f);
filter.setStageTime(2, 150.0f);
filter.setStageTime(3, 100.0f);
filter.setStageCurve(1, 1.0f);      // Exponential for stage 1

filter.trigger();

for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Rhythmic looping):**
```cpp
MultiStageEnvelopeFilter filter;
filter.prepare(44100.0);

filter.setNumStages(4);
filter.setStageTarget(0, 500.0f);
filter.setStageTarget(1, 2000.0f);
filter.setStageTarget(2, 800.0f);
filter.setStageTarget(3, 1500.0f);
filter.setLoop(true);
filter.setLoopStart(1);
filter.setLoopEnd(3);

filter.trigger();
// Loops stages 1-3 indefinitely until release() called
```

**Usage Example (Velocity-sensitive):**
```cpp
MultiStageEnvelopeFilter filter;
filter.prepare(44100.0);
filter.setVelocitySensitivity(1.0f);  // Full sensitivity

// On MIDI note-on
filter.trigger(midiVelocity / 127.0f);  // velocity 64 = 50% depth
```

**Topology:**
```
trigger() --> [Stage State Machine] --> Cutoff Frequency
                      |
                      v
              [Curve Shaping]
                      |
                      v
Input ---------> [SVF Filter] ---------> Output
                      ^
                      |
              Cutoff (smoothed), Q, Mode
```

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (svf.h, smoother.h)

---

## SidechainFilter
**Path:** [sidechain_filter.h](../../dsp/include/krate/dsp/processors/sidechain_filter.h) | **Since:** 0.14.0

Dynamically controls filter cutoff based on sidechain signal envelope for ducking/pumping effects.

**Use when:**
- External signal controls filter dynamics (kick drum ducking bass)
- Creating rhythmic pumping/ducking effects in electronic music
- Need auto-wah with hold time and lookahead features
- Building sidechain filter effects where one signal controls another's tonal character

**Differences from EnvelopeFilter:**
- External sidechain input (vs self-analysis only)
- Hold time to prevent chattering on decaying transients
- Lookahead for anticipating transients before they occur
- State machine (Idle/Active/Holding) for precise timing control

```cpp
enum class SidechainFilterState : uint8_t { Idle, Active, Holding };
enum class SidechainDirection : uint8_t { Up, Down };
enum class SidechainFilterMode : uint8_t { Lowpass, Bandpass, Highpass };

class SidechainFilter {
    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kMinThresholdDb = -60.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kMinSensitivityDb = -24.0f;
    static constexpr float kMaxSensitivityDb = 24.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kMinLookaheadMs = 0.0f;
    static constexpr float kMaxLookaheadMs = 50.0f;
    static constexpr float kMinHoldMs = 0.0f;
    static constexpr float kMaxHoldMs = 1000.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;  // Equals lookahead samples

    // Processing - External sidechain
    [[nodiscard]] float processSample(float mainInput, float sidechainInput) noexcept;
    void process(const float* mainInput, const float* sidechainInput,
                 float* output, size_t numSamples) noexcept;
    void process(float* mainInOut, const float* sidechainInput, size_t numSamples) noexcept;

    // Processing - Self-sidechain
    [[nodiscard]] float processSample(float input) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Sidechain detection parameters
    void setAttackTime(float ms) noexcept;            // [0.1, 500]
    void setReleaseTime(float ms) noexcept;           // [1, 5000]
    void setThreshold(float dB) noexcept;             // [-60, 0] trigger threshold
    void setSensitivity(float dB) noexcept;           // [-24, +24] sidechain pre-gain

    // Filter response parameters
    void setDirection(SidechainDirection dir) noexcept;  // Up or Down
    void setMinCutoff(float hz) noexcept;             // [20, maxCutoff-1]
    void setMaxCutoff(float hz) noexcept;             // [minCutoff+1, Nyquist*0.45]
    void setResonance(float q) noexcept;              // [0.5, 20.0]
    void setFilterType(SidechainFilterMode type) noexcept;

    // Timing parameters
    void setLookahead(float ms) noexcept;             // [0, 50] adds latency
    void setHoldTime(float ms) noexcept;              // [0, 1000] delays release

    // Sidechain filter parameters
    void setSidechainFilterEnabled(bool enabled) noexcept;
    void setSidechainFilterCutoff(float hz) noexcept; // [20, 500] HP for sidechain

    // Monitoring
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentEnvelope() const noexcept;

    // Getters for all parameters...
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| attackTime | 10 ms | [0.1, 500] | Envelope attack speed |
| releaseTime | 100 ms | [1, 5000] | Envelope release speed |
| threshold | -30 dB | [-60, 0] | Trigger level (dB domain comparison) |
| sensitivity | 0 dB | [-24, +24] | Sidechain pre-gain |
| direction | Down | enum | Up (louder=higher cutoff), Down (louder=lower cutoff) |
| minCutoff | 200 Hz | [20, max-1] | Sweep range minimum |
| maxCutoff | 2000 Hz | [min+1, Nyquist*0.45] | Sweep range maximum |
| resonance | 8.0 | [0.5, 20.0] | Filter Q factor |
| filterType | Lowpass | enum | LP/BP/HP response |
| lookahead | 0 ms | [0, 50] | Anticipate transients (adds latency) |
| holdTime | 0 ms | [0, 1000] | Delay release phase |

**Usage Example (Kick drum ducking bass):**
```cpp
SidechainFilter filter;
filter.prepare(48000.0, 512);
filter.setDirection(SidechainDirection::Down);  // Louder = lower cutoff
filter.setThreshold(-30.0f);
filter.setMinCutoff(200.0f);
filter.setMaxCutoff(4000.0f);
filter.setAttackTime(5.0f);
filter.setReleaseTime(200.0f);
filter.setHoldTime(20.0f);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.processSample(bassInput[i], kickInput[i]);
}
```

**Usage Example (Self-sidechain auto-wah with lookahead):**
```cpp
SidechainFilter filter;
filter.prepare(48000.0, 512);
filter.setDirection(SidechainDirection::Up);
filter.setLookahead(5.0f);  // 5ms lookahead
filter.setHoldTime(50.0f);

// Self-sidechain: input controls its own filtering
filter.process(buffer, numSamples);
```

**State Machine:**
```
                    +------------------+
                    |      Idle        |  Filter at resting position
                    | (below threshold)|
                    +--------+---------+
                             |
                    envelope > threshold
                             |
                             v
                    +------------------+
                    |     Active       |  Filter follows envelope
                    | (above threshold)|
                    +--------+---------+
                             |
                    envelope <= threshold
                             |
                             v
                    +------------------+
                    |    Holding       |  Filter frozen at last value
                    | (hold timer > 0) |  Re-trigger resets timer
                    +--------+---------+
                             |
                    hold timer expires
                             |
                             v
                    (back to Idle, release begins)
```

**Topology:**
```
Sidechain Input --> [HP Filter (opt)] --> [Sensitivity Gain] --> EnvelopeFollower
                                                                        |
                                                                        v
                                                              [Threshold Compare]
                                                                        |
                                                                        v
                                                               [State Machine]
                                                                        |
                                                                        v
                                                              [Log-space Mapping]
                                                                        |
                                                                        v
Main Input ---------> [Lookahead Delay] ---------------------> SVF <--- Cutoff
                                                                |
                                                                v
                                                             Output
```

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (svf.h, delay_line.h, biquad.h, smoother.h), Layer 2 peer (envelope_follower.h)

---

## TransientAwareFilter
**Path:** [transient_filter.h](../../dsp/include/krate/dsp/processors/transient_filter.h) | **Since:** 0.14.0

Transient-triggered filter with dual envelope detection for dynamic tonal shaping of attacks.

**Use when:**
- Adding "snap" or "click" to drums/percussion by opening filter on transients
- Softening harsh synth attacks by closing filter on transients
- Creating rhythmic resonance "ping" effects on bass and plucked sounds
- Need attack-specific tonal shaping without affecting sustained portions

**Key Differentiator from EnvelopeFilter:**
- EnvelopeFilter: Responds to overall amplitude (auto-wah, touch-sensitive)
- TransientAwareFilter: Responds only to transients (attacks), sustained notes do NOT trigger

**Key Differentiator from SidechainFilter:**
- SidechainFilter: External sidechain input, lookahead, hold time
- TransientAwareFilter: Self-analysis only, level-independent detection

**Features:**
- Dual envelope transient detection (1ms fast, 50ms slow envelopes)
- Level-independent normalization (`diff / max(slowEnv, epsilon)`)
- Configurable sensitivity threshold (0.0-1.0)
- Bidirectional cutoff modulation (transient can be higher OR lower than idle)
- Resonance boost during transients with stability clamping (max Q=30)
- Exponential attack/decay response curves via OnePoleSmoother
- Log-space frequency interpolation for perceptual sweeps
- Three filter types: Lowpass, Bandpass, Highpass (SVF for modulation stability)

```cpp
enum class TransientFilterMode : uint8_t { Lowpass, Bandpass, Highpass };

class TransientAwareFilter {
    static constexpr float kFastEnvelopeAttackMs = 1.0f;
    static constexpr float kSlowEnvelopeAttackMs = 50.0f;
    static constexpr float kMinSensitivity = 0.0f;
    static constexpr float kMaxSensitivity = 1.0f;
    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 50.0f;
    static constexpr float kMinDecayMs = 1.0f;
    static constexpr float kMaxDecayMs = 1000.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kMaxTotalResonance = 30.0f;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;  // Always 0

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Transient detection parameters
    void setSensitivity(float sensitivity) noexcept;  // [0, 1] threshold control
    void setTransientAttack(float ms) noexcept;       // [0.1, 50] filter response speed
    void setTransientDecay(float ms) noexcept;        // [1, 1000] return to idle speed

    // Filter cutoff parameters
    void setIdleCutoff(float hz) noexcept;            // [20, Nyquist*0.45] at rest
    void setTransientCutoff(float hz) noexcept;       // [20, Nyquist*0.45] at peak

    // Filter resonance parameters
    void setIdleResonance(float q) noexcept;          // [0.5, 20.0] Q at rest
    void setTransientQBoost(float boost) noexcept;    // [0.0, 20.0] additional Q

    // Filter configuration
    void setFilterType(TransientFilterMode type) noexcept;

    // Monitoring (for UI)
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentResonance() const noexcept;
    [[nodiscard]] float getTransientLevel() const noexcept;  // [0, 1] detection level

    // Getters for all parameters...
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| sensitivity | 0.5 | [0, 1] | Detection threshold (higher = more sensitive) |
| transientAttack | 1.0 ms | [0.1, 50] | Filter response speed to transients |
| transientDecay | 50 ms | [1, 1000] | Return to idle speed |
| idleCutoff | 200 Hz | [20, Nyquist*0.45] | Cutoff at rest |
| transientCutoff | 4000 Hz | [20, Nyquist*0.45] | Cutoff at peak transient |
| idleResonance | 0.7071 | [0.5, 20] | Q at rest (Butterworth default) |
| transientQBoost | 0.0 | [0, 20] | Additional Q during transient |
| filterType | Lowpass | enum | LP/BP/HP response |

**Usage Example (Drum Attack Enhancement):**
```cpp
TransientAwareFilter filter;
filter.prepare(48000.0);
filter.setIdleCutoff(200.0f);         // Dark at rest
filter.setTransientCutoff(4000.0f);   // Bright on hits
filter.setSensitivity(0.5f);
filter.setTransientAttack(1.0f);
filter.setTransientDecay(50.0f);
filter.setFilterType(TransientFilterMode::Lowpass);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Synth Attack Softening):**
```cpp
TransientAwareFilter filter;
filter.prepare(48000.0);
filter.setIdleCutoff(8000.0f);        // Bright at rest
filter.setTransientCutoff(500.0f);    // Dark on attacks (inverse)
filter.setSensitivity(0.5f);
```

**Topology:**
```
                   +------------------+     +------------------+
Input ------------>| Fast Envelope    |---->|                  |
          |        | (1ms att/rel)    |     | Normalized Diff  |
          |        +------------------+     | = (fast-slow)    |
          |                                 |   / max(slow,e)  |
          |        +------------------+     |                  |
          +------->| Slow Envelope    |---->|                  |
          |        | (50ms att/rel)   |     +--------+---------+
          |        +------------------+              |
          |                                          v
          |                            +-------------------------+
          |                            | Threshold Compare       |
          |                            | (1.0 - sensitivity)     |
          |                            +-------------------------+
          |                                          |
          |                            +-------------------------+
          |                            | Response Smoother       |
          |                            | (attack/decay dynamic)  |
          |                            +-------------------------+
          |                                          |
          |                            +-------------------------+
          |                            | Log-space Freq Mapping  |
          |                            | + Linear Q Mapping      |
          |                            +-------------------------+
          |                                          |
          |        +------------------+              v
          +------->|       SVF        |<---- Cutoff + Resonance
                   | (LP/BP/HP)       |
                   +------------------+
                            |
                            v
                         Output
```

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (svf.h, smoother.h), Layer 2 peer (envelope_follower.h)

---

## PitchTrackingFilter
**Path:** [pitch_tracking_filter.h](../../dsp/include/krate/dsp/processors/pitch_tracking_filter.h) | **Since:** 0.14.0

Pitch-tracking dynamic filter for harmonic-aware filtering where cutoff follows detected pitch.

**Use when:**
- Filter cutoff should follow input pitch for harmonic emphasis/suppression
- Creating resonant filters that track a musical relationship to the input fundamental
- Need harmonic-aware filtering that maintains consistent tonal character across different notes
- Building pitch-following auto-filter effects for synths or monophonic sources

**Key Differentiator from EnvelopeFilter:**
- EnvelopeFilter: Cutoff follows amplitude (touch-sensitive, auto-wah)
- PitchTrackingFilter: Cutoff follows detected pitch (harmonic tracking)

**Key Differentiator from TransientAwareFilter:**
- TransientAwareFilter: Responds to transients/attacks only
- PitchTrackingFilter: Continuously tracks pitch content

**Features:**
- Autocorrelation-based pitch detection via PitchDetector (50-1000Hz range)
- Configurable harmonic ratio (0.125-16x) for cutoff = pitch * ratio
- Semitone offset (-48 to +48) for creative detuning effects
- Confidence threshold for reliable pitch tracking
- Configurable tracking speed (1-500ms smoothing)
- Fallback cutoff with smooth transitions for unpitched material
- Three filter types: Lowpass, Bandpass, Highpass (SVF for modulation stability)
- Monitoring outputs: currentCutoff, detectedPitch, pitchConfidence

```cpp
enum class PitchTrackingFilterMode : uint8_t { Lowpass, Bandpass, Highpass };

class PitchTrackingFilter {
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 30.0f;
    static constexpr float kMinTrackingMs = 1.0f;
    static constexpr float kMaxTrackingMs = 500.0f;
    static constexpr float kMinHarmonicRatio = 0.125f;
    static constexpr float kMaxHarmonicRatio = 16.0f;
    static constexpr float kMinSemitoneOffset = -48.0f;
    static constexpr float kMaxSemitoneOffset = 48.0f;
    static constexpr float kDefaultConfidenceThreshold = 0.5f;
    static constexpr float kDefaultTrackingMs = 50.0f;
    static constexpr float kDefaultHarmonicRatio = 1.0f;
    static constexpr float kDefaultFallbackCutoff = 1000.0f;
    static constexpr float kDefaultResonance = 0.7071067811865476f;  // Butterworth

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;  // ~256 samples (PitchDetector window)

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Pitch detection parameters
    void setDetectionRange(float minHz, float maxHz) noexcept;  // [50, 1000] Hz
    void setConfidenceThreshold(float threshold) noexcept;      // [0, 1]
    void setTrackingSpeed(float ms) noexcept;                   // [1, 500] ms

    // Filter-pitch relationship
    void setHarmonicRatio(float ratio) noexcept;                // [0.125, 16] cutoff multiplier
    void setSemitoneOffset(float semitones) noexcept;           // [-48, +48] semitones

    // Filter configuration
    void setResonance(float q) noexcept;                        // [0.5, 30]
    void setFilterType(PitchTrackingFilterMode type) noexcept;

    // Fallback behavior
    void setFallbackCutoff(float hz) noexcept;                  // [20, Nyquist*0.45]
    void setFallbackSmoothing(float ms) noexcept;               // [1, 500] ms

    // Monitoring
    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getDetectedPitch() const noexcept;
    [[nodiscard]] float getPitchConfidence() const noexcept;

    // Getters for all parameters...
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| confidenceThreshold | 0.5 | [0, 1] | Minimum pitch confidence for tracking |
| trackingSpeed | 50 ms | [1, 500] | Smoothing time for cutoff changes |
| harmonicRatio | 1.0 | [0.125, 16] | Cutoff = pitch * ratio |
| semitoneOffset | 0 | [-48, +48] | Additional offset in semitones |
| resonance | 0.707 | [0.5, 30] | Filter Q factor (Butterworth default) |
| filterType | Lowpass | enum | LP/BP/HP response |
| fallbackCutoff | 1000 Hz | [20, Nyquist*0.45] | Cutoff when pitch uncertain |
| fallbackSmoothing | 50 ms | [1, 500] | Transition speed to/from fallback |
| detectionRange | 50-1000 Hz | [50, 1000] | Valid pitch detection range |

**Usage Example (Harmonic Filter Tracking):**
```cpp
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);
filter.setHarmonicRatio(2.0f);       // Cutoff at 2nd harmonic (octave)
filter.setResonance(8.0f);           // Resonant peak
filter.setFilterType(PitchTrackingFilterMode::Lowpass);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Creative Semitone Offset):**
```cpp
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);
filter.setHarmonicRatio(1.0f);       // Follow fundamental
filter.setSemitoneOffset(7.0f);      // Fifth up from fundamental
filter.setResonance(4.0f);

// Play a melody - cutoff stays a fifth above each note
filter.processBlock(buffer, numSamples);
```

**Usage Example (Robust Fallback for Mixed Material):**
```cpp
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);
filter.setConfidenceThreshold(0.6f);  // Require good confidence
filter.setFallbackCutoff(1500.0f);    // Use 1.5kHz during uncertainty
filter.setFallbackSmoothing(100.0f);  // Smooth 100ms transitions

// Works with mixed pitched/unpitched material
filter.processBlock(buffer, numSamples);
```

**Topology:**
```
                   +------------------+
Input ------------>| PitchDetector    |---> Pitch (Hz) + Confidence
          |        | (autocorrelation)|
          |        +------------------+
          |                   |
          |                   v
          |        +------------------+     +-------------------+
          |        | Confidence Gate  |---->| Harmonic Calc     |
          |        | (threshold)      |     | cutoff = pitch *  |
          |        +------------------+     | ratio * 2^(s/12)  |
          |                   |             +-------------------+
          |                   |                       |
          |          (low confidence)                 v
          |                   |             +-------------------+
          |                   +------------>| Target Selection  |
          |                   |             | (tracking/fallback)|
          |                   v             +-------------------+
          |        +------------------+               |
          |        | Fallback Cutoff  |               v
          |        | (1000Hz default) |     +-------------------+
          |        +------------------+     | Cutoff Smoother   |
          |                                 +-------------------+
          |                                           |
          |        +------------------+               v
          +------->|       SVF        |<---- Cutoff Frequency
                   | (LP/BP/HP @ Q)   |
                   +------------------+
                            |
                            v
                         Output
```

**Dependencies:** Layer 0 (db_utils.h, pitch_utils.h), Layer 1 (pitch_detector.h, svf.h, smoother.h)

---

## NoteSelectiveFilter
**Path:** [note_selective_filter.h](../../dsp/include/krate/dsp/processors/note_selective_filter.h) | **Since:** 0.14.0

Note-selective dynamic filter that processes only audio matching specific note classes (C, C#, D, etc.), passing non-matching notes through dry.

**Use when:**
- Filtering only specific notes in a musical context (e.g., filter only root notes of a chord)
- Creating note-specific effects that apply processing to certain pitches while leaving others unchanged
- Building intelligent effects that respond to musical content rather than just amplitude/frequency

**Key Differentiator from PitchTrackingFilter:**
- PitchTrackingFilter: Cutoff follows detected pitch (filter moves with pitch)
- NoteSelectiveFilter: Applies filter only when detected pitch matches target notes (selective application)

**Features:**
- Note class selection via bitset (all 12 chromatic notes independently selectable)
- Configurable note tolerance (1-49 cents, default 49) for tuning variance
- Smooth crossfade transitions (0.5-50ms, default 5ms) for click-free state changes
- Filter always processes (stays hot) for instant response when note matches
- Block-rate pitch detection updates (~512 samples) for stability
- Three no-detection behaviors: Dry, Filtered, LastState
- Thread-safe parameter updates via atomics (FR-035)

```cpp
enum class NoDetectionMode : uint8_t { Dry, Filtered, LastState };

class NoteSelectiveFilter {
    static constexpr float kDefaultNoteTolerance = 49.0f;    // Cents
    static constexpr float kDefaultCrossfadeTimeMs = 5.0f;
    static constexpr float kMinCutoffHz = 20.0f;

    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;
    void reset() noexcept;

    // Note selection
    void setTargetNotes(std::bitset<12> notes) noexcept;    // 0=C, 1=C#, ..., 11=B
    void setTargetNote(int noteClass, bool enabled) noexcept;
    void clearAllNotes() noexcept;
    void setAllNotes() noexcept;

    // Pitch matching
    void setNoteTolerance(float cents) noexcept;            // [1, 49]

    // Crossfade
    void setCrossfadeTime(float ms) noexcept;               // [0.5, 50]

    // Filter configuration
    void setCutoff(float hz) noexcept;                      // [20, Nyquist*0.45]
    void setResonance(float q) noexcept;                    // [0.1, 30]
    void setFilterType(SVFMode type) noexcept;              // All 8 SVF modes

    // No-detection behavior
    void setNoDetectionBehavior(NoDetectionMode mode) noexcept;
    void setConfidenceThreshold(float threshold) noexcept;  // [0, 1]
    void setDetectionRange(float minHz, float maxHz) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, int numSamples) noexcept;

    // State query
    [[nodiscard]] int getDetectedNoteClass() const noexcept;    // -1 if no detection
    [[nodiscard]] bool isCurrentlyFiltering() const noexcept;
};
```

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| targetNotes | none | bitset<12> | Which note classes to filter |
| noteTolerance | 49 | [1, 49] | Cents tolerance for pitch matching |
| crossfadeTime | 5 ms | [0.5, 50] | Transition time (99% settling) |
| cutoff | 1000 Hz | [20, Nyquist] | Filter cutoff frequency |
| resonance | 0.7071 | [0.1, 30] | Filter Q (0.7071 = Butterworth) |
| filterType | Lowpass | SVFMode | LP/HP/BP/Notch/Allpass/Peak/Shelves |
| noDetectionMode | Dry | enum | Behavior when no pitch detected |
| confidenceThreshold | 0.3 | [0, 1] | Minimum pitch detection confidence |

**Usage Example (Filter Root Notes Only):**
```cpp
NoteSelectiveFilter filter;
filter.prepare(48000.0, 512);

// Enable filtering for C and G (root and fifth)
filter.setTargetNote(0, true);   // C
filter.setTargetNote(7, true);   // G

filter.setCutoff(500.0f);
filter.setResonance(4.0f);
filter.setFilterType(SVFMode::Lowpass);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Tight Tolerance for Well-Tuned Input):**
```cpp
NoteSelectiveFilter filter;
filter.prepare(48000.0, 512);
filter.setTargetNote(9, true);   // A notes only
filter.setNoteTolerance(10.0f);  // Only ±10 cents (strict tuning)
filter.setCrossfadeTime(2.0f);   // Fast transitions
```

**Usage Example (Handle Unpitched Material):**
```cpp
NoteSelectiveFilter filter;
filter.prepare(48000.0, 512);
filter.setAllNotes();            // Filter all pitched content
filter.setNoDetectionBehavior(NoDetectionMode::Dry);  // Pass drums dry
filter.setConfidenceThreshold(0.5f);  // Require moderate confidence
```

**Topology:**
```
                   +------------------+
Input ------------>| PitchDetector    |---> Pitch (Hz) + Confidence
          |        | (autocorrelation)|
          |        +------------------+
          |                   |
          |                   v
          |        +------------------+     +-------------------+
          |        | Note Matching    |---->| Target Notes      |
          |        | (frequencyTo-    |     | (bitset<12>)      |
          |        |  NoteClass)      |     +-------------------+
          |        +------------------+
          |                   |
          |    +--- Match ----+---- No Match ---+
          |    |                                |
          |    v                                v
          |  Target: 1.0 (Filtered)        Target: 0.0 (Dry)
          |                   |
          |                   v
          |        +------------------+
          |        | OnePoleSmoother  | (crossfade)
          |        +------------------+
          |                   |
          |                   v
          |        +------------------+
          +------->|       SVF        |----> Filtered signal
          |        | (always active)  |
          |        +------------------+
          |                   |
          |                   v
          +-------> Dry/Filtered crossfade mix -----> Output
```

**Dependencies:** Layer 0 (db_utils.h, pitch_utils.h), Layer 1 (pitch_detector.h, svf.h, smoother.h)

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

## SpectralDistortion
**Path:** [spectral_distortion.h](../../dsp/include/krate/dsp/processors/spectral_distortion.h) | **Since:** 0.14.0

Per-frequency-bin distortion in the spectral domain for "impossible" distortion effects.

**Use when:**
- Creating harmonic distortion on individual frequency bins
- Need frequency-selective saturation with independent band control
- Want lo-fi spectral magnitude quantization (spectral bitcrushing)
- Building effects that cannot exist in time-domain processing
- Need surgical phase-preserving distortion

**Features:**
- Four distortion modes: PerBinSaturate, MagnitudeOnly, BinSelective, SpectralBitcrush
- 9 waveshaping curves via Waveshaper primitive (Tanh, Atan, Cubic, etc.)
- Frequency-selective distortion with configurable low/mid/high bands
- DC/Nyquist bin exclusion by default (opt-in processing)
- Phase preservation mode for surgical processing
- Gap handling: Passthrough or UseGlobalDrive for unassigned bins
- Real-time safe (noexcept, allocation in prepare() only)

```cpp
enum class SpectralDistortionMode : uint8_t {
    PerBinSaturate,   // Per-bin waveshaping, natural phase evolution
    MagnitudeOnly,    // Per-bin waveshaping, exact phase preservation
    BinSelective,     // Per-band drive control with crossovers
    SpectralBitcrush  // Magnitude quantization, exact phase preservation
};

enum class GapBehavior : uint8_t {
    Passthrough,      // Unassigned bins pass through unmodified
    UseGlobalDrive    // Unassigned bins use global drive parameter
};

class SpectralDistortion {
    static constexpr size_t kMinFFTSize = 256;
    static constexpr size_t kMaxFFTSize = 8192;
    static constexpr size_t kDefaultFFTSize = 2048;
    static constexpr float kMinDrive = 0.0f;
    static constexpr float kMaxDrive = 10.0f;
    static constexpr float kMinBits = 1.0f;
    static constexpr float kMaxBits = 16.0f;

    void prepare(double sampleRate, size_t fftSize = kDefaultFFTSize) noexcept;
    void reset() noexcept;
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;

    // Mode selection
    void setMode(SpectralDistortionMode mode) noexcept;
    [[nodiscard]] SpectralDistortionMode getMode() const noexcept;

    // Global parameters
    void setDrive(float drive) noexcept;           // [0, 10], 0 = bypass
    void setSaturationCurve(WaveshapeType curve) noexcept;
    void setProcessDCNyquist(bool enabled) noexcept;

    // Bin-selective parameters
    void setLowBand(float freqHz, float drive) noexcept;
    void setMidBand(float lowHz, float highHz, float drive) noexcept;
    void setHighBand(float freqHz, float drive) noexcept;
    void setGapBehavior(GapBehavior mode) noexcept;

    // SpectralBitcrush parameters
    void setMagnitudeBits(float bits) noexcept;    // [1, 16]

    // Query
    [[nodiscard]] size_t latency() const noexcept;  // Returns fftSize
    [[nodiscard]] size_t getFftSize() const noexcept;
    [[nodiscard]] size_t getNumBins() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

**Parameters:**

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| mode | PerBinSaturate | enum | Processing mode |
| drive | 1.0 | [0, 10] | Global drive amount (0 = bypass) |
| saturationCurve | Tanh | WaveshapeType | Waveshaping algorithm |
| processDCNyquist | false | bool | Include DC/Nyquist bins |
| magnitudeBits | 16.0 | [1, 16] | Quantization depth for bitcrush |

**Usage Examples:**

```cpp
// Basic per-bin saturation
SpectralDistortion dist;
dist.prepare(44100.0, 2048);
dist.setMode(SpectralDistortionMode::PerBinSaturate);
dist.setDrive(3.0f);
dist.setSaturationCurve(WaveshapeType::Tanh);
dist.processBlock(input, output, numSamples);

// Frequency-selective distortion (bass crushing)
dist.setMode(SpectralDistortionMode::BinSelective);
dist.setLowBand(300.0f, 4.0f);        // Heavy saturation below 300Hz
dist.setMidBand(300.0f, 3000.0f, 2.0f); // Medium 300-3000Hz
dist.setHighBand(3000.0f, 1.0f);      // Clean above 3000Hz
dist.setGapBehavior(GapBehavior::Passthrough);

// Spectral bitcrushing (lo-fi effect)
dist.setMode(SpectralDistortionMode::SpectralBitcrush);
dist.setMagnitudeBits(4.0f);  // 16 quantization levels
```

**Signal Flow:**
```
Input -> [STFT Analysis] -> [Per-Bin Processing] -> [IFFT Synthesis] -> Output
                                   |
         +--------------+----------+-----------+--------------+
         |              |          |           |              |
    PerBinSaturate  MagnitudeOnly  BinSelective  SpectralBitcrush
```

**Dependencies:** Layer 0 (math_constants.h), Layer 1 (stft.h, spectral_buffer.h, spectral_utils.h, waveshaper.h)

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

---

## SelfOscillatingFilter
**Path:** [self_oscillating_filter.h](../../dsp/include/krate/dsp/processors/self_oscillating_filter.h) | **Since:** 0.13.0

Melodically playable self-oscillating filter using LadderFilter for sine-wave generation from filter resonance.

**Use when:**
- Creating oscillator-like effects from filter resonance (analog synth technique)
- Need MIDI-controlled melodic filter (noteOn/noteOff with velocity)
- Building filter ping effects (transient excitation of resonant filter)
- Want configurable attack/release envelope for shaped oscillation
- Need glide/portamento for pitch sweeps between notes

**Do NOT use when:**
- You just need a standard filter (use LadderFilter or SVF directly)
- You need polyphony (this is monophonic - use multiple instances)
- You need precise waveform shape (this produces sine-like, not pure sine)

**Features:**
- Self-oscillation at resonance >= 0.95 (normalized 0-1 maps to LadderFilter 0-4)
- MIDI control: noteOn(midiNote, velocity), noteOff() with 12-TET tuning
- Configurable attack (0-20ms) and release (10-2000ms) envelope
- Glide/portamento (0-5000ms) with linear frequency ramp
- External input mixing for filter ping effects
- Wave shaping via soft saturation (tanh, 1x-3x gain)
- Output level control (-60 to +6 dB)
- DC blocking for clean output

```cpp
class SelfOscillatingFilter {
    // Constants
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr float kMinAttackMs = 0.0f;
    static constexpr float kMaxAttackMs = 20.0f;
    static constexpr float kMinReleaseMs = 10.0f;
    static constexpr float kMaxReleaseMs = 2000.0f;
    static constexpr float kMinGlideMs = 0.0f;
    static constexpr float kMaxGlideMs = 5000.0f;
    static constexpr float kMinLevelDb = -60.0f;
    static constexpr float kMaxLevelDb = 6.0f;

    // Lifecycle
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    // Processing (real-time safe, noexcept)
    [[nodiscard]] float process(float externalInput) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // MIDI control
    void noteOn(int midiNote, int velocity) noexcept;  // velocity 0 = noteOff
    void noteOff() noexcept;

    // Parameters (smoothed for click-free changes)
    void setFrequency(float hz) noexcept;           // [20, 20000] or sampleRate*0.45
    void setResonance(float amount) noexcept;       // [0.0, 1.0], >= 0.95 for self-osc
    void setGlide(float ms) noexcept;               // [0, 5000] portamento time
    void setAttack(float ms) noexcept;              // [0, 20] envelope attack
    void setRelease(float ms) noexcept;             // [10, 2000] envelope release
    void setExternalMix(float mix) noexcept;        // [0, 1] 0=osc only, 1=ext only
    void setWaveShape(float amount) noexcept;       // [0, 1] soft saturation
    void setOscillationLevel(float dB) noexcept;    // [-60, +6] output level

    // Getters
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] float getGlide() const noexcept;
    [[nodiscard]] float getAttack() const noexcept;
    [[nodiscard]] float getRelease() const noexcept;
    [[nodiscard]] float getExternalMix() const noexcept;
    [[nodiscard]] float getWaveShape() const noexcept;
    [[nodiscard]] float getOscillationLevel() const noexcept;
    [[nodiscard]] bool isOscillating() const noexcept;  // UI feedback
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| frequency | 440 Hz | [20, 20000] | Oscillation/cutoff frequency |
| resonance | 1.0 | [0, 1] | >= 0.95 enables self-oscillation |
| glide | 0 ms | [0, 5000] | Portamento between notes |
| attack | 0 ms | [0, 20] | Envelope attack time |
| release | 500 ms | [10, 2000] | Envelope release time |
| externalMix | 0.0 | [0, 1] | Blend oscillation with external input |
| waveShape | 0.0 | [0, 1] | Soft saturation amount (tanh) |
| level | 0 dB | [-60, +6] | Output level |

**Usage Example (Pure oscillator):**
```cpp
SelfOscillatingFilter filter;
filter.prepare(44100.0, 512);
filter.setResonance(1.0f);  // Enable self-oscillation
filter.setFrequency(440.0f);

// Process as pure oscillator (no external input)
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(0.0f);
}
```

**Usage Example (MIDI-controlled melodic filter):**
```cpp
SelfOscillatingFilter filter;
filter.prepare(44100.0, 512);
filter.setResonance(1.0f);
filter.setGlide(50.0f);   // 50ms portamento
filter.setAttack(5.0f);   // 5ms attack
filter.setRelease(300.0f); // 300ms release

// On MIDI noteOn event
filter.noteOn(60, 100);  // C4, velocity 100

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(0.0f);
}

// On MIDI noteOff event
filter.noteOff();
```

**Usage Example (Filter ping):**
```cpp
SelfOscillatingFilter filter;
filter.prepare(44100.0, 512);
filter.setResonance(0.9f);    // High but below self-oscillation
filter.setFrequency(1000.0f);
filter.setExternalMix(1.0f);  // Process external input

// Process transient audio (drum hits, etc.)
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Topology:**
```
                    +-------------+
   External Input ->| Mix Control |--+
                    +-------------+  |
                                     v
                    +-------------+  |  +-------------+
   (self-osc) ----->|   Ladder    |--+->|  DC Block   |
                    |   Filter    |     +-------------+
                    +-------------+            |
                           ^                   v
                           |            +-------------+
                    Cutoff (from glide) | Wave Shape  |
                    Resonance (fixed    +-------------+
                     at ~3.95 for osc)         |
                                               v
                                        +-------------+
                                        |  Envelope   |
                                        |    * Gain   |
                                        +-------------+
                                               |
                                               v
                                        +-------------+
                                        | Level Gain  |
                                        +-------------+
                                               |
                                               v
                                            Output
```

**Gotchas:**
- Resonance mapping: User 0-1 maps to LadderFilter 0-4 with special handling above 0.95
- Self-oscillation frequency may differ slightly from cutoff due to ladder topology phase shift
- Per-sample cutoff updates required for accurate pitch during glide (FR-004)
- DC blocker required after filter to remove oscillation DC offset

**Performance:** < 0.5% CPU @ 44.1kHz stereo (2 instances) per Layer 2 budget

**Dependencies:** Layer 0 (midi_utils.h, db_utils.h, fast_math.h), Layer 1 (ladder_filter.h, dc_blocker.h, smoother.h)

---

## SampleHoldFilter
**Path:** [sample_hold_filter.h](../../dsp/include/krate/dsp/processors/sample_hold_filter.h) | **Since:** 0.13.0

Sample & Hold filter processor that samples and holds filter parameters at configurable intervals, creating stepped modulation effects.

**Use when:**
- Creating rhythmic, stepped filter patterns synchronized to a clock
- Building audio-reactive filter effects that respond to transients (drum hits, etc.)
- Need random/generative stepped modulation with probability control
- Want multi-dimensional spatial modulation (cutoff + Q + pan sampling)
- Creating distinctive "stepped" sound moving in time with music

**Do NOT use when:**
- You need continuous/smooth filter modulation (use StochasticFilter with Walk mode)
- You only need basic filter without modulation (use SVF directly)
- You need complex LFO shapes (use ModulationMatrix with LFO)

**Features:**
- Three trigger modes: Clock (regular intervals), Audio (transient detection), Random (probability-based)
- Four sample sources per parameter: LFO, Random, Envelope, External
- Per-parameter independent source selection (cutoff from LFO, Q from Random, pan from Envelope)
- Stereo processing with symmetric pan offset formula for spatial modulation
- Slew limiting for smooth transitions (0-500ms) applied only to sampled values
- Sample-accurate timing across buffer boundaries
- Deterministic seeding for reproducible random sequences
- Real-time safe: all process methods noexcept, zero allocations

```cpp
enum class TriggerSource : uint8_t { Clock, Audio, Random };
enum class SampleSource : uint8_t { LFO, Random, Envelope, External };

class SampleHoldFilter {
    // Constants
    static constexpr float kMinHoldTimeMs = 0.1f;
    static constexpr float kMaxHoldTimeMs = 10000.0f;
    static constexpr float kMinSlewTimeMs = 0.0f;
    static constexpr float kMaxSlewTimeMs = 500.0f;
    static constexpr float kMinLFORate = 0.01f;
    static constexpr float kMaxLFORate = 20.0f;
    static constexpr float kMinCutoffOctaves = 0.0f;
    static constexpr float kMaxCutoffOctaves = 8.0f;
    static constexpr float kMinQRange = 0.0f;
    static constexpr float kMaxQRange = 1.0f;
    static constexpr float kMinPanOctaveRange = 0.0f;
    static constexpr float kMaxPanOctaveRange = 4.0f;
    static constexpr float kDefaultBaseQ = 0.707f;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Processing (real-time safe, noexcept)
    [[nodiscard]] float process(float input) noexcept;
    void processStereo(float& left, float& right) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void processBlockStereo(float* left, float* right, size_t numSamples) noexcept;

    // Trigger configuration
    void setTriggerSource(TriggerSource source) noexcept;
    void setHoldTime(float ms) noexcept;              // [0.1, 10000]
    void setTransientThreshold(float threshold) noexcept;  // [0, 1] audio mode
    void setTriggerProbability(float probability) noexcept;  // [0, 1] random mode

    // Sample source configuration
    void setLFORate(float hz) noexcept;              // [0.01, 20]
    void setExternalValue(float value) noexcept;     // [0, 1] external source

    // Cutoff parameter
    void setCutoffSamplingEnabled(bool enabled) noexcept;
    void setCutoffSource(SampleSource source) noexcept;
    void setCutoffOctaveRange(float octaves) noexcept;  // [0, 8]

    // Q parameter
    void setQSamplingEnabled(bool enabled) noexcept;
    void setQSource(SampleSource source) noexcept;
    void setQRange(float range) noexcept;            // [0, 1]

    // Pan parameter (stereo)
    void setPanSamplingEnabled(bool enabled) noexcept;
    void setPanSource(SampleSource source) noexcept;
    void setPanOctaveRange(float octaves) noexcept;  // [0, 4]

    // Slew limiting
    void setSlewTime(float ms) noexcept;             // [0, 500]

    // Filter core
    void setFilterMode(SVFMode mode) noexcept;       // LP, HP, BP, Notch
    void setBaseCutoff(float hz) noexcept;           // [20, 20000]
    void setBaseQ(float q) noexcept;                 // [0.1, 30]

    // Reproducibility
    void setSeed(uint32_t seed) noexcept;

    // Getters for all parameters...
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
};
```

| TriggerSource | Description | Use Case |
|---------------|-------------|----------|
| Clock | Regular intervals based on hold time | Rhythmic stepped patterns |
| Audio | Transient detection from input | Drum-reactive effects |
| Random | Probability-based at hold intervals | Generative/chaotic patterns |

| SampleSource | Output | Use Case |
|--------------|--------|----------|
| LFO | [-1, 1] direct | Predictable modulation shape |
| Random | [-1, 1] Xorshift32 | Chaotic/generative effects |
| Envelope | [0,1] -> [-1,1] | Input-following modulation |
| External | [0,1] -> [-1,1] | Host automation / CV input |

**Usage Example (Basic stepped filter):**
```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);
filter.setTriggerSource(TriggerSource::Clock);
filter.setHoldTime(100.0f);  // 100ms intervals
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::LFO);
filter.setLFORate(2.0f);     // 2Hz LFO
filter.setCutoffOctaveRange(2.0f);  // +/- 2 octaves
filter.setBaseCutoff(1000.0f);

for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

**Usage Example (Audio-reactive stereo):**
```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);
filter.setTriggerSource(TriggerSource::Audio);
filter.setTransientThreshold(0.3f);  // Trigger on drum hits
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::Random);
filter.setPanSamplingEnabled(true);
filter.setPanSource(SampleSource::Random);
filter.setPanOctaveRange(1.0f);
filter.setSlewTime(10.0f);  // Smooth transitions
filter.setSeed(42);

filter.processBlockStereo(left, right, numSamples);
```

**Pan Formula:**
```
leftCutoff = baseCutoff * pow(2, -panValue * panOctaveRange)
rightCutoff = baseCutoff * pow(2, +panValue * panOctaveRange)
```
Where panValue is in [-1, 1] and panOctaveRange is [0, 4] octaves.

**Gotchas:**
- Hold time uses double precision for sample-accurate timing at high sample rates (192kHz)
- Audio mode uses EnvelopeFollower with DetectionMode::Peak, attack=0.1ms, release=50ms
- Slew applies ONLY to sampled modulation values; base parameter changes are instant
- Envelope and External sources output [0,1] which is converted to [-1,1] via `(value * 2) - 1`
- Random trigger mode evaluates probability at each hold interval using same Xorshift32 as sample source

**Performance:** < 0.5% CPU @ 44.1kHz stereo per Layer 2 budget

**Dependencies:** Layer 0 (random.h), Layer 1 (svf.h, lfo.h, smoother.h), Layer 2 (envelope_follower.h)

---

## AudioRateFilterFM
**Path:** [audio_rate_filter_fm.h](../../dsp/include/krate/dsp/processors/audio_rate_filter_fm.h) | **Since:** 0.13.0

Audio-rate filter frequency modulation processor that modulates SVF filter cutoff at audio rates (20Hz-20kHz) to create metallic, bell-like, ring modulation-style, and aggressive timbres.

**Use when:**
- Creating FM synthesis-style metallic or bell-like timbres from simple input signals
- Need sidechain-based timbral modulation between instruments
- Building chaotic/experimental feedback-based filtering effects
- Want precise control over modulation depth in musical octave units
- Need anti-aliased audio-rate filter modulation via configurable oversampling

**Features:**
- Three modulation sources: Internal wavetable oscillator, External (sidechain), Self (feedback)
- Four filter types: Lowpass, Highpass, Bandpass, Notch (12 dB/oct via SVF)
- Four internal oscillator waveforms: Sine, Triangle, Sawtooth, Square
- 2048-sample wavetables with linear interpolation for low THD
- FM depth in octaves (0-6) for intuitive musical control
- Per-sample cutoff updates for true audio-rate modulation
- Configurable oversampling (1x, 2x, 4x) for anti-aliasing
- Self-modulation with hard-clipping to [-1, +1] for bounded feedback
- NaN/Inf detection with automatic state reset

```cpp
class AudioRateFilterFM {
    static constexpr size_t kWavetableSize = 2048;
    static constexpr float kMinCutoff = 20.0f;
    static constexpr float kMinModFreq = 0.1f;
    static constexpr float kMaxModFreq = 20000.0f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMaxFMDepth = 6.0f;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Carrier filter
    void setCarrierCutoff(float hz) noexcept;       // [20, sr*0.495] Hz
    void setCarrierQ(float q) noexcept;             // [0.5, 20.0]
    void setFilterType(FMFilterType type) noexcept; // LP/HP/BP/Notch

    // Modulator
    void setModulatorSource(FMModSource source) noexcept;  // Internal/External/Self
    void setModulatorFrequency(float hz) noexcept;         // [0.1, 20000] Hz
    void setModulatorWaveform(FMWaveform waveform) noexcept; // Sine/Tri/Saw/Sqr

    // FM control
    void setFMDepth(float octaves) noexcept;        // [0.0, 6.0] octaves
    void setOversamplingFactor(int factor) noexcept; // 1, 2, or 4
    [[nodiscard]] size_t getLatency() const noexcept;

    // Processing
    [[nodiscard]] float process(float input, float externalMod = 0.0f) noexcept;
    void processBlock(float* buffer, const float* modulator, size_t numSamples) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

**FM Formula:**
```
modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)
```
Where modulator is in [-1, +1] and fmDepth is in octaves [0, 6].

**Usage Example (Internal Oscillator FM):**
```cpp
AudioRateFilterFM fm;
fm.prepare(44100.0, 512);
fm.setModulatorSource(FMModSource::Internal);
fm.setModulatorFrequency(440.0f);      // 440 Hz modulator
fm.setModulatorWaveform(FMWaveform::Sine);
fm.setFMDepth(2.0f);                   // +/- 2 octaves
fm.setCarrierCutoff(1000.0f);
fm.setCarrierQ(8.0f);
fm.setFilterType(FMFilterType::Lowpass);
fm.setOversamplingFactor(2);           // 2x oversampling

fm.processBlock(buffer, numSamples);
```

**Usage Example (External Sidechain FM):**
```cpp
AudioRateFilterFM fm;
fm.prepare(44100.0, 512);
fm.setModulatorSource(FMModSource::External);
fm.setFMDepth(1.0f);                   // 1 octave depth
fm.setCarrierCutoff(2000.0f);
fm.setFilterType(FMFilterType::Bandpass);

// sidechainBuffer drives filter cutoff modulation
fm.processBlock(audioBuffer, sidechainBuffer, numSamples);
```

**Usage Example (Self-Modulation/Feedback FM):**
```cpp
AudioRateFilterFM fm;
fm.prepare(44100.0, 512);
fm.setModulatorSource(FMModSource::Self);
fm.setFMDepth(1.5f);                   // Moderate feedback depth
fm.setCarrierCutoff(1000.0f);
fm.setCarrierQ(12.0f);                 // High resonance for character
fm.setOversamplingFactor(4);           // 4x for chaotic stability

fm.processBlock(buffer, numSamples);
```

**Gotchas:**
- FM depth = 0 produces identical output to static SVF (useful for A/B testing)
- Self-modulation output is hard-clipped to [-1, +1] before use as modulator
- Oversampling factors other than 1, 2, 4 are clamped to nearest valid value
- External modulator nullptr is treated as 0.0 (no modulation)
- NaN/Inf input triggers state reset and returns 0.0f for that sample
- Phase continuity maintained when modulator frequency changes mid-stream
- Calling process() before prepare() returns input unchanged (safe bypass)

**Performance:** < 2ms per 512-sample block at 4x oversampling (SC-010)

**Dependencies:** Layer 0 (math_constants.h, db_utils.h), Layer 1 (svf.h, oversampler.h)

---

## TemporalDistortion
**Path:** [temporal_distortion.h](../../dsp/include/krate/dsp/processors/temporal_distortion.h) | **Since:** 0.14.0

Memory-based distortion processor where waveshaper drive changes based on signal history, creating dynamics-aware saturation that "feels alive" compared to static waveshaping.

**Use when:**
- Need dynamics-responsive saturation (guitar amp behavior where louder signals distort more)
- Want transient-reactive distortion (drums where attacks get sharper saturation)
- Creating expansion-style effects (quiet passages get more distortion)
- Adding analog-style hysteresis character (path-dependent processing)

```cpp
enum class TemporalMode : uint8_t {
    EnvelopeFollow,   // Drive increases with amplitude (tube amp behavior)
    InverseEnvelope,  // Drive increases as amplitude decreases (expansion)
    Derivative,       // Drive proportional to rate of change (transient emphasis)
    Hysteresis        // Drive depends on signal history (path-dependent)
};

class TemporalDistortion {
    static constexpr float kMinBaseDrive = 0.0f;
    static constexpr float kMaxBaseDrive = 10.0f;
    static constexpr float kReferenceLevel = 0.251189f;  // -12 dBFS RMS
    static constexpr float kMaxSafeDrive = 20.0f;        // InverseEnvelope cap

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] float processSample(float x) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Mode selection
    void setMode(TemporalMode mode) noexcept;
    [[nodiscard]] TemporalMode getMode() const noexcept;

    // Core parameters
    void setBaseDrive(float drive) noexcept;         // [0, 10]
    void setDriveModulation(float amount) noexcept;  // [0, 1] - 0 = static waveshaping
    void setAttackTime(float ms) noexcept;           // [0.1, 500]
    void setReleaseTime(float ms) noexcept;          // [1, 5000]
    void setWaveshapeType(WaveshapeType type) noexcept;  // All 9 waveshape types

    // Hysteresis-specific
    void setHysteresisDepth(float depth) noexcept;   // [0, 1]
    void setHysteresisDecay(float ms) noexcept;      // [1, 500]

    [[nodiscard]] constexpr size_t getLatency() const noexcept;  // Always 0
};
```

**Temporal Modes:**
- **EnvelopeFollow:** Louder = more drive. At reference level (-12 dBFS), drive equals base drive. Classic dynamics-responsive behavior.
- **InverseEnvelope:** Quieter = more drive. Capped at 20.0 to prevent instability on silence. Useful for expansion-style effects.
- **Derivative:** Drive proportional to amplitude rate of change. Uses 10Hz highpass on envelope. Transients get more distortion.
- **Hysteresis:** Drive depends on recent signal trajectory. Memory state decays exponentially. Creates path-dependent analog character.

**Usage Example (Guitar Amp Behavior):**
```cpp
TemporalDistortion distortion;
distortion.prepare(44100.0, 512);
distortion.setMode(TemporalMode::EnvelopeFollow);
distortion.setBaseDrive(2.0f);
distortion.setDriveModulation(0.7f);
distortion.setAttackTime(10.0f);
distortion.setReleaseTime(100.0f);
distortion.setWaveshapeType(WaveshapeType::Tanh);

distortion.processBlock(buffer, numSamples);
// Loud passages saturate more, quiet passages stay cleaner
```

**Usage Example (Transient Enhancement):**
```cpp
TemporalDistortion distortion;
distortion.prepare(44100.0, 512);
distortion.setMode(TemporalMode::Derivative);
distortion.setBaseDrive(3.0f);
distortion.setDriveModulation(1.0f);
distortion.setAttackTime(1.0f);
distortion.setReleaseTime(50.0f);

distortion.processBlock(drumBuffer, numSamples);
// Transients get sharper, sustained portions stay smoother
```

**Gotchas:**
- Zero drive modulation produces static waveshaping (equivalent to direct Waveshaper use)
- Zero base drive outputs silence
- InverseEnvelope mode caps drive at 20.0 to prevent instability on near-silence
- Envelope floor (0.001) prevents divide-by-zero in InverseEnvelope mode
- Mode switching is artifact-free due to 5ms drive smoothing
- NaN/Inf input resets state and returns 0.0f
- Processing before prepare() returns input unchanged (safe bypass)

**Performance:** < 0.5% CPU at 44.1kHz stereo (SC-010)

**Dependencies:** Layer 1 (waveshaper.h, one_pole.h, smoother.h), Layer 2 (envelope_follower.h)

---

## AllpassSaturator
**Path:** [allpass_saturator.h](../../dsp/include/krate/dsp/processors/allpass_saturator.h) | **Since:** 0.14.0

Resonant distortion processor using allpass filters with saturation in feedback loops. Creates pitched, self-oscillating resonances that can be excited by input audio. Supports four topologies for different timbral characteristics.

**Use when:**
- Creating pitched, resonant distortion effects (singing drums, resonant overtones)
- Building Karplus-Strong string synthesis with harmonic richness
- Generating metallic, bell-like inharmonic tones
- Creating self-sustaining drones and evolving textures
- Need resonant feedback that can self-oscillate with saturation

**Features:**
- Four topologies: SingleAllpass (pitched), AllpassChain (metallic), KarplusStrong (strings), FeedbackMatrix (drones)
- 9 saturation curves via Waveshaper (Tanh, TanhFast, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, SoftClip)
- Soft clipping at +/-2.0 in feedback path for bounded self-oscillation
- 10ms parameter smoothing for click-free automation
- DC blocking after saturation
- Zero latency processing

```cpp
enum class NetworkTopology : uint8_t {
    SingleAllpass,   // Single allpass + saturator feedback loop
    AllpassChain,    // 4 cascaded allpasses at prime frequency ratios
    KarplusStrong,   // Delay + lowpass + saturator (string synthesis)
    FeedbackMatrix   // 4x4 Householder matrix of cross-fed saturators
};

class AllpassSaturator {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;

    // Topology selection
    void setTopology(NetworkTopology topology) noexcept;
    [[nodiscard]] NetworkTopology getTopology() const noexcept;

    // Frequency control (resonant pitch)
    void setFrequency(float hz) noexcept;              // [20, sampleRate * 0.45]
    [[nodiscard]] float getFrequency() const noexcept;

    // Feedback control (resonance intensity)
    void setFeedback(float feedback) noexcept;         // [0.0, 0.999]
    [[nodiscard]] float getFeedback() const noexcept;

    // Saturation control
    void setSaturationCurve(WaveshapeType type) noexcept;
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;
    void setDrive(float drive) noexcept;               // [0.1, 10.0]
    [[nodiscard]] float getDrive() const noexcept;

    // KarplusStrong decay (string sustain)
    void setDecay(float seconds) noexcept;             // [0.001, 60.0]
    [[nodiscard]] float getDecay() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

| Topology | Resonance Type | Use Case |
|----------|----------------|----------|
| SingleAllpass | Pitched at frequency | Adding singing quality to drums, pitched distortion |
| AllpassChain | Inharmonic (f, 1.5f, 2.33f, 3.67f) | Metallic, bell-like, gamelan tones |
| KarplusStrong | Plucked string decay | Guitar plucks, harpsichord, physical modeling |
| FeedbackMatrix | Dense, evolving | Ambient drones, self-sustaining textures |

| Parameter | Default | Range | Smoothed | Effect |
|-----------|---------|-------|----------|--------|
| topology | SingleAllpass | enum | No (resets state) | Network configuration |
| frequency | 440 Hz | [20, Nyquist*0.45] | Yes (10ms) | Resonant pitch |
| feedback | 0.5 | [0, 0.999] | Yes (10ms) | Resonance intensity (>0.9 = self-oscillation) |
| drive | 1.0 | [0.1, 10.0] | Yes (10ms) | Saturation intensity |
| saturationCurve | Tanh | WaveshapeType | No | Saturation algorithm |
| decay | 1.0 s | [0.001, 60.0] | No | KarplusStrong only - RT60 decay time |

**Usage Example (Pitched Resonant Distortion):**
```cpp
AllpassSaturator processor;
processor.prepare(44100.0, 512);
processor.setTopology(NetworkTopology::SingleAllpass);
processor.setFrequency(440.0f);   // A4
processor.setFeedback(0.85f);     // Strong resonance
processor.setDrive(2.0f);         // Moderate saturation

for (size_t i = 0; i < numSamples; ++i) {
    output[i] = processor.process(input[i]);
}
```

**Usage Example (Karplus-Strong String):**
```cpp
AllpassSaturator processor;
processor.prepare(44100.0, 512);
processor.setTopology(NetworkTopology::KarplusStrong);
processor.setFrequency(220.0f);   // A3 pitch
processor.setDecay(1.5f);         // 1.5 second decay
processor.setDrive(2.5f);         // Added harmonics

// Trigger with impulse/noise burst
output[0] = processor.process(1.0f);
for (size_t i = 1; i < 44100 * 2; ++i) {
    output[i] = processor.process(0.0f);  // Resonance sustains
}
```

**Usage Example (FeedbackMatrix Drone):**
```cpp
AllpassSaturator processor;
processor.prepare(44100.0, 512);
processor.setTopology(NetworkTopology::FeedbackMatrix);
processor.setFrequency(100.0f);   // Low base frequency
processor.setFeedback(0.95f);     // Near self-oscillation
processor.setDrive(3.0f);         // Rich saturation

// Brief input excites sustained drone
for (int i = 0; i < 100; ++i) {
    output[i] = processor.process(0.5f);  // Brief excitation
}
for (size_t i = 100; i < numSamples; ++i) {
    output[i] = processor.process(0.0f);  // Self-sustains
}
```

**Topology Signal Flows:**

SingleAllpass:
```
input -> [+] -> [allpass] -> [saturator] -> [soft clip] -> output
          ^                                      |
          |_______ feedback * gain _____________|
```

KarplusStrong:
```
input -> [delay] -> [saturator] -> [1-pole LP] -> [soft clip] -> output
           ^                                           |
           |__________ feedback ______________________|
```

AllpassChain:
```
input -> [+] -> [AP1] -> [AP2] -> [AP3] -> [AP4] -> [saturator] -> [soft clip] -> output
          ^      f      1.5f     2.33f    3.67f                          |
          |_________________________ feedback ___________________________|
```

FeedbackMatrix:
```
input -> [Stage 1] -+                +-> sum -> output
         [Stage 2] -+-> Householder -+
         [Stage 3] -+    Matrix      +-> feedback to stages
         [Stage 4] -+                |
              ^                      |
              |_________feedback_____|
```

**Gotchas:**
- Topology changes reset all internal state (may cause brief silence)
- Frequency is clamped to [20Hz, sampleRate * 0.45] to prevent aliasing
- Feedback > 0.9 enables self-oscillation (input excites, resonance sustains indefinitely)
- Decay parameter only affects KarplusStrong topology
- NaN/Inf input resets state and returns 0.0f
- Processing before prepare() returns input unchanged (safe bypass)

**Performance:** < 0.5% CPU at 44.1kHz mono (SC-005)

**Dependencies:** Layer 0 (math_constants.h, db_utils.h, sigmoid.h), Layer 1 (biquad.h, delay_line.h, waveshaper.h, dc_blocker.h, smoother.h, one_pole.h)

---

## GranularDistortion
**Path:** [granular_distortion.h](../../dsp/include/krate/dsp/processors/granular_distortion.h) | **Since:** 0.14.0

Time-windowed granular distortion processor with per-grain variation. Applies distortion in overlapping micro-grains (5-100ms) for evolving, textured destruction effects impossible with static waveshaping.

**Use when:**
- Creating evolving, organic distortion textures that change over time
- Building "crushed" sounds with rhythmic, grain-based movement
- Designing experimental sound design with per-grain algorithm variation
- Need different distortion intensities on successive grains (drive variation)
- Want temporal smearing via position jitter for diffused transients

**Features:**
- 64 simultaneous grains with voice stealing
- 9 distortion algorithms (Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Diode, Tube)
- Per-grain drive randomization (0-100% variation)
- Per-grain algorithm randomization (optional)
- Position jitter for temporal smearing (0-50ms)
- Density control (1-8 overlapping grains)
- Click-free parameter automation via 10ms smoothing
- Hann window envelope for smooth grain onsets/offsets

```cpp
class GranularDistortion {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Grain parameters
    void setGrainSize(float ms) noexcept;            // [5, 100] ms
    void setGrainDensity(float density) noexcept;    // [1, 8] overlapping grains

    // Distortion parameters
    void setDistortionType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;             // [1, 20]
    void setDriveVariation(float amount) noexcept;   // [0, 1]
    void setAlgorithmVariation(bool enabled) noexcept;

    // Position control
    void setPositionJitter(float ms) noexcept;       // [0, 50] ms

    // Mix control
    void setMix(float mix) noexcept;                 // [0, 1]

    // Query
    [[nodiscard]] size_t getActiveGrainCount() const noexcept;
    [[nodiscard]] static constexpr size_t getMaxGrains() noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

    // Testing
    void seed(uint32_t seedValue) noexcept;
};
```

**Example Usage:**
```cpp
#include <krate/dsp/processors/granular_distortion.h>

GranularDistortion gd;
gd.prepare(44100.0, 512);

// Configure for evolving texture
gd.setGrainSize(50.0f);           // 50ms grains
gd.setGrainDensity(4.0f);         // ~4 overlapping grains
gd.setDrive(5.0f);                // Moderate drive
gd.setDriveVariation(0.5f);       // 50% drive variation
gd.setAlgorithmVariation(true);   // Random algorithms per grain
gd.setPositionJitter(10.0f);      // 10ms temporal smearing
gd.setMix(0.75f);                 // 75% wet

// Process audio
gd.process(buffer, numSamples);
```

**Signal Flow:**
```
input -> circular buffer -> grain trigger (scheduler)
                               |
                               v
              +---> grain N: read + envelope + waveshaper ---+
              |                                              |
              +---> grain N-1: read + envelope + waveshaper -+-> sum -> mix -> output
              |                                              |       (dry)
              +---> ...                                    --+
```

**Density Formula:**
```
grainsPerSecond = density * 1000 / grainSizeMs
```
With density=4 and grainSize=50ms: 80 grains/second

**Gotchas:**
- Mono-only design (FR-047). For stereo, use two instances with different seeds
- Position jitter is clamped to available buffer history at startup
- Algorithm variation cycles through all 9 waveshaper types randomly
- Diode algorithm is unbounded (can exceed [-1, 1]), others are bounded
- NaN/Inf input resets state and returns 0.0f
- Processing before prepare() may produce undefined output

**Performance:** < 0.5% CPU at 44.1kHz mono

**Memory:** ~143KB (32KB buffer + 64 waveshapers + 64 grain states + 8KB envelope table)

**Dependencies:** Layer 0 (grain_envelope.h, random.h, db_utils.h), Layer 1 (grain_pool.h, waveshaper.h, smoother.h), Layer 2 (grain_scheduler.h)

---

## FractalDistortion
**Path:** [fractal_distortion.h](../../dsp/include/krate/dsp/processors/fractal_distortion.h) | **Since:** 0.15.0

Recursive multi-scale distortion processor with self-similar harmonic structure. Applies fractal-inspired distortion where each iteration level contributes progressively smaller amplitude content, creating complex evolving harmonic structures impossible with single-stage saturation.

**Use when:**
- Creating evolving, layered distortion with self-similar harmonic structure
- Need frequency-aware distortion (Multiband mode: more harmonics on high frequencies)
- Want separate control over odd vs even harmonics (Harmonic mode)
- Building distortion that progressively changes character (Cascade mode)
- Creating chaotic, self-oscillating textures (Feedback mode)
- Need "Digital Destruction" aesthetic with intentional aliasing

**Features:**
- 5 processing modes (Residual, Multiband, Harmonic, Cascade, Feedback)
- 1-8 iteration levels with exponential amplitude scaling (scaleFactor^N)
- Per-level waveshaper selection (Cascade mode)
- Per-level highpass filtering (Frequency Decay)
- Cross-level feedback for chaotic textures (Feedback mode)
- Click-free parameter automation via 10ms smoothing
- DC blocking after asymmetric saturation
- NaN/Inf input handling (returns 0.0f and resets state)

```cpp
enum class FractalMode : uint8_t {
    Residual = 0,   // Classic residual-based recursion (default)
    Multiband = 1,  // Octave-band splitting with scaled iterations
    Harmonic = 2,   // Odd/even harmonic separation via Chebyshev
    Cascade = 3,    // Different waveshaper per level
    Feedback = 4    // Cross-level feedback for chaotic textures
};

class FractalDistortion {
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Mode selection
    void setMode(FractalMode mode) noexcept;
    [[nodiscard]] FractalMode getMode() const noexcept;

    // Core parameters
    void setIterations(int iterations) noexcept;     // [1, 8]
    void setScaleFactor(float scale) noexcept;       // [0.3, 0.9]
    void setDrive(float drive) noexcept;             // [1.0, 20.0]
    void setMix(float mix) noexcept;                 // [0.0, 1.0]
    void setFrequencyDecay(float decay) noexcept;    // [0.0, 1.0]

    // Multiband mode (FR-030 to FR-033)
    void setCrossoverFrequency(float hz) noexcept;
    void setBandIterationScale(float scale) noexcept;

    // Harmonic mode (FR-034 to FR-038)
    void setOddHarmonicCurve(WaveshapeType type) noexcept;
    void setEvenHarmonicCurve(WaveshapeType type) noexcept;

    // Cascade mode (FR-039 to FR-041)
    void setLevelWaveshaper(int level, WaveshapeType type) noexcept;
    [[nodiscard]] WaveshapeType getLevelWaveshaper(int level) const noexcept;

    // Feedback mode (FR-042 to FR-045)
    void setFeedbackAmount(float amount) noexcept;   // [0.0, 0.5]

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
};
```

**Example Usage:**
```cpp
#include <krate/dsp/processors/fractal_distortion.h>

FractalDistortion fractal;
fractal.prepare(44100.0, 512);

// Configure Residual mode (default)
fractal.setMode(FractalMode::Residual);
fractal.setIterations(4);      // 4 recursive levels
fractal.setScaleFactor(0.5f);  // Each level 50% of previous
fractal.setDrive(3.0f);        // Moderate drive
fractal.setMix(0.8f);          // 80% wet

// Optional: add brightness to deeper levels
fractal.setFrequencyDecay(0.5f);

// Process audio
fractal.process(buffer, numSamples);
```

**Residual Algorithm (default):**
```
level[N] = saturate((input - sum(level[0..N-1])) * scaleFactor^N * drive)
output = DC_block(sum(all_levels)) * mix + dry * (1-mix)
```

**Mode Characteristics:**
| Mode | Algorithm | Character |
|------|-----------|-----------|
| Residual | Iterative residual extraction | Complex, evolving harmonics |
| Multiband | 4-way crossover + per-band iterations | Frequency-aware distortion |
| Harmonic | Chebyshev odd/even separation | Independent odd/even control |
| Cascade | Per-level waveshaper selection | Progressive character change |
| Feedback | Previous output → current input | Chaotic, self-oscillating |

**Gotchas:**
- Mono-only design. For stereo, use two instances
- Aliasing is intentional ("Digital Destruction" aesthetic) - no internal oversampling
- Feedback mode capped at 0.5 to prevent runaway oscillation
- Multiband uses pseudo-octave spacing (1:4:16 ratios), not true octaves
- Frequency decay at level 8 with decay=1.0 creates highpass at 1600Hz (200Hz × 8)
- Processing before prepare() returns unchanged input

**Performance:** < 0.5% CPU at 44.1kHz mono (8 iterations)

**Memory:** ~4KB (8 waveshapers + 8 Biquads + DC blocker + Crossover4Way + smoothers)

**Dependencies:** Layer 0 (sigmoid.h, db_utils.h), Layer 1 (waveshaper.h, biquad.h, dc_blocker.h, smoother.h, chebyshev_shaper.h), Layer 2 (crossover_filter.h)

---

## SyncOscillator
**Path:** [sync_oscillator.h](../../dsp/include/krate/dsp/processors/sync_oscillator.h) | **Since:** 0.16.0

Band-limited synchronized oscillator with three sync modes. Composes a lightweight master PhaseAccumulator with a slave phase tracker and MinBlepTable::Residual for anti-aliased sync output.

**Use when:**
- Classic hard sync timbral control is needed (bright, vocal sync lead sounds)
- Reverse sync wave-folding effect (smooth, analog-style soft sync)
- Phase advance for subtle detuning, ensemble chorusing, and gentle phase entrainment
- Any oscillator synchronization requiring band-limited discontinuity correction

```cpp
enum class SyncMode : uint8_t { Hard = 0, Reverse = 1, PhaseAdvance = 2 };

class SyncOscillator {
    // Lifecycle
    explicit SyncOscillator(const MinBlepTable* table = nullptr) noexcept;
    void prepare(double sampleRate) noexcept;    // NOT real-time safe
    void reset() noexcept;

    // Parameter setters
    void setMasterFrequency(float hz) noexcept;  // [0, sampleRate/2)
    void setSlaveFrequency(float hz) noexcept;   // [0, sampleRate/2)
    void setSlaveWaveform(OscWaveform waveform) noexcept;
    void setSyncMode(SyncMode mode) noexcept;
    void setSyncAmount(float amount) noexcept;    // [0, 1]
    void setSlavePulseWidth(float width) noexcept; // [0.01, 0.99]

    // Processing (real-time safe)
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Sync Modes:**

| Mode | Behavior | Correction | Sound Character |
|------|----------|------------|-----------------|
| Hard | Reset slave phase on master wrap | minBLEP (step) | Bright, vocal, classic sync lead |
| Reverse | Reverse slave direction on master wrap | minBLAMP (derivative) | Warm, wavefolder-like, analog soft sync |
| PhaseAdvance | Nudge slave phase toward alignment | minBLEP (proportional) | Subtle detuning, ensemble, chorusing |

**SyncAmount (0.0 to 1.0):**
- 0.0 = No sync (slave runs freely)
- 1.0 = Full sync (hard reset / full reversal / complete phase advance)
- Intermediate = Smooth crossfade between free-running and synced behavior

**Architecture Notes:**
- The slave uses a PhaseAccumulator (not PolyBlepOscillator). The naive waveform is evaluated directly at each sample, and ALL discontinuity corrections (sync-induced and natural wraps) go through the MinBLEP residual. This avoids PolyBLEP/minBLEP double-correction at slave phase wrap boundaries.
- Master oscillator is a lightweight PhaseAccumulator (no waveform output). Its sole purpose is providing timing for sync events.
- At integer frequency ratios (e.g., 1:1, 2:1), the slave naturally reaches the correct phase, making sync a no-op (clean pass-through).
- Master frequency clamped to sampleRate/2, ensuring at most one sync event per sample.

**Gotchas:**
- Mono-only design. For stereo, use two instances
- SyncAmount is NOT internally smoothed. Caller must smooth to avoid clicks
- Reverse sync at 1:1 ratio still toggles direction (not a pass-through)
- Constructor takes `const MinBlepTable*` (caller owns lifetime). Table must be prepared before `SyncOscillator::prepare()`
- NaN/Inf inputs sanitized to safe defaults (0.0 for frequencies)
- Output clamped to [-2.0, 2.0] with NaN replaced by 0.0

**Performance:** ~100-150 cycles/sample per voice (measured in Release build)

**Memory:** MinBlepTable shared across voices, plus one Residual buffer (16 floats) per instance

**Dependencies:** Layer 0 (phase_utils.h, math_constants.h, db_utils.h), Layer 1 (polyblep_oscillator.h for OscWaveform enum, minblep_table.h)

---

## SubOscillator
**Path:** [sub_oscillator.h](../../dsp/include/krate/dsp/processors/sub_oscillator.h) | **Since:** 0.17.0

Frequency-divided sub-oscillator that tracks a master oscillator via flip-flop division, replicating the classic analog sub-oscillator behavior found in Moog, Sequential, and Oberheim hardware synthesizers. Supports square (with minBLEP), sine, and triangle waveforms at one-octave (divide-by-2) or two-octave (divide-by-4) depths, with an equal-power mix control.

**Use when:**
- Adding analog-style sub-bass beneath a main oscillator (synth voice architecture)
- Classic sub-oscillator behavior with flip-flop frequency division
- Combining sub-bass with the main oscillator using equal-power crossfade
- Polyphonic sub-oscillators (up to 128 voices sharing one MinBlepTable)

```cpp
enum class SubOctave : uint8_t { OneOctave = 0, TwoOctaves = 1 };
enum class SubWaveform : uint8_t { Square = 0, Sine = 1, Triangle = 2 };

class SubOscillator {
    // Lifecycle
    explicit SubOscillator(const MinBlepTable* table = nullptr) noexcept;
    void prepare(double sampleRate) noexcept;    // NOT real-time safe
    void reset() noexcept;

    // Parameter setters
    void setOctave(SubOctave octave) noexcept;
    void setWaveform(SubWaveform waveform) noexcept;
    void setMix(float mix) noexcept;              // [0, 1], NaN/Inf ignored

    // Processing (real-time safe)
    [[nodiscard]] float process(bool masterPhaseWrapped,
                                float masterPhaseIncrement) noexcept;
    [[nodiscard]] float processMixed(float mainOutput,
                                     bool masterPhaseWrapped,
                                     float masterPhaseIncrement) noexcept;
};
```

**Integration with PolyBlepOscillator:**

```cpp
MinBlepTable table;
table.prepare(64, 8);

PolyBlepOscillator master;
master.prepare(44100.0);
master.setFrequency(440.0f);
master.setWaveform(OscWaveform::Sawtooth);

SubOscillator sub(&table);
sub.prepare(44100.0);
sub.setOctave(SubOctave::OneOctave);
sub.setWaveform(SubWaveform::Square);
sub.setMix(0.5f);

float phaseInc = 440.0f / 44100.0f;
for (int i = 0; i < numSamples; ++i) {
    float mainOut = master.process();
    output[i] = sub.processMixed(mainOut, master.phaseWrapped(), phaseInc);
}
```

**Waveforms:**

| Waveform | Generation Method | Band-Limiting | Sound Character |
|----------|-------------------|---------------|-----------------|
| Square | Flip-flop state (+1/-1) | minBLEP at toggle points | Classic analog sub, punchy |
| Sine | Phase accumulator + sin() | Inherently band-limited | Smooth, pure sub-bass |
| Triangle | Phase accumulator + piecewise linear | Inherently band-limited | Warm, rounded sub |

**Octave Division:**

| Mode | Division | Mechanism | Example (440 Hz master) |
|------|----------|-----------|-------------------------|
| OneOctave | /2 | Single flip-flop | 220 Hz output |
| TwoOctaves | /4 | Two-stage flip-flop chain | 110 Hz output |

**Architecture Notes:**
- The SubOscillator does NOT own a PolyBlepOscillator. It receives `masterPhaseWrapped` and `masterPhaseIncrement` as parameters to `process()`.
- The flip-flop toggle drives both the square waveform output AND the sine/triangle phase resynchronization (phase reset to 0 on rising edge).
- MinBLEP correction only applies to the Square waveform. Sine and triangle are smooth and do not need discontinuity correction.
- Mix parameter gains are cached at `setMix()` time using `equalPowerGains()`, not computed per-sample.
- Delta-phase tracking: sine/triangle frequency = `masterPhaseIncrement / octaveFactor`, providing zero-latency response to FM and pitch modulation.
- All flip-flop states initialized to false in constructor, `prepare()`, and `reset()` for deterministic rendering (DAW bounce consistency).

**Gotchas:**
- Mono-only design. For stereo, use two instances
- Mix parameter is NOT internally smoothed. Caller must smooth to avoid zipper noise
- Constructor takes `const MinBlepTable*` (caller owns lifetime). Table must be prepared before `SubOscillator::prepare()`
- NaN/Inf inputs to `setMix()` are ignored (previous value retained)
- Output clamped to [-2.0, 2.0] with NaN replaced by 0.0
- `phaseIncrement` parameter is a float (matching PolyBlepOscillator's `dt_` precision)

**Performance:** < 50 cycles/sample per voice (measured in Release build)

**Memory:** ~112 bytes per instance (standard table config: length=16). 128 instances = ~14 KB (fits in L1 cache).

**Dependencies:** Layer 0 (phase_utils.h, math_constants.h, crossfade_utils.h, db_utils.h), Layer 1 (minblep_table.h)

---

## FMOperator
**Path:** [fm_operator.h](../../dsp/include/krate/dsp/processors/fm_operator.h) | **Since:** 0.11.0

Single FM synthesis operator (oscillator + ratio + feedback + level), the fundamental building block for FM/PM synthesis. Uses phase modulation (Yamaha DX7-style) where the modulator output is added to the carrier's phase, not frequency.

**Use when:**
- Building FM/PM synthesis voices
- Need frequency-controllable sine oscillator with ratio multiplier
- Want self-modulation feedback for harmonic richness
- Chaining operators for FM algorithm topologies

```cpp
class FMOperator {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept;
    void setFrequency(float hz) noexcept;
    void setRatio(float ratio) noexcept;      // [0, 16.0]
    void setFeedback(float amount) noexcept;  // [0, 1.0]
    void setLevel(float level) noexcept;      // [0, 1.0]
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] float getRatio() const noexcept;
    [[nodiscard]] float getFeedback() const noexcept;
    [[nodiscard]] float getLevel() const noexcept;
    [[nodiscard]] float lastRawOutput() const noexcept;
};
```

**Usage Example: Two-Operator FM Chain (Modulator -> Carrier)**

```cpp
// Create operators
FMOperator modulator;
FMOperator carrier;

// Initialize
modulator.prepare(44100.0);
carrier.prepare(44100.0);

// Configure modulator (2:1 ratio = one octave above base frequency)
modulator.setFrequency(440.0f);
modulator.setRatio(2.0f);       // 880 Hz
modulator.setFeedback(0.0f);    // No self-modulation
modulator.setLevel(0.5f);       // Modulation depth

// Configure carrier (1:1 ratio = base frequency)
carrier.setFrequency(440.0f);
carrier.setRatio(1.0f);         // 440 Hz
carrier.setFeedback(0.0f);      // No self-modulation
carrier.setLevel(1.0f);         // Full output

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    modulator.process();        // Advance modulator
    float pm = modulator.lastRawOutput() * modulator.getLevel();
    output[i] = carrier.process(pm);  // Apply modulation
}
```

**Signal Flow:**
1. effectiveFreq = frequency * ratio (Nyquist-clamped)
2. feedbackPM = tanh(previousRawOutput * feedbackAmount)
3. totalPM = phaseModInput + feedbackPM
4. rawOutput = sin(phase + totalPM)
5. output = rawOutput * level
6. return sanitize(output)

**Parameter Ranges:**

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| frequency | [0, Nyquist) | 0 Hz | Base frequency in Hz |
| ratio | [0, 16.0] | 1.0 | Frequency multiplier |
| feedback | [0, 1.0] | 0.0 | Self-modulation intensity |
| level | [0, 1.0] | 0.0 | Output amplitude |

**Feedback Behavior:**

| Feedback | Character | THD |
|----------|-----------|-----|
| 0.0 | Pure sine | < 0.1% |
| 0.3-0.5 | Saw-like, added harmonics | 5-20% |
| 1.0 | Maximum richness, sawtooth-like | > 30% |

**Gotchas:**
- `prepare()` is NOT real-time safe (generates wavetable internally)
- `process()` returns 0.0 if `prepare()` has not been called
- NaN/Inf inputs to parameters are sanitized (frequency -> 0, others -> preserve previous)
- Output clamped to [-2.0, 2.0] with NaN replaced by 0.0
- Each instance owns ~90 KB for internal sine wavetable

**Performance:** < 0.5% CPU for 1 second of audio at 44.1 kHz (Layer 2 budget)

**Memory:** ~90 KB per instance (dominated by mipmapped sine wavetable)

**Dependencies:** Layer 0 (fast_math.h, db_utils.h, math_constants.h), Layer 1 (wavetable_oscillator.h, wavetable_generator.h)

---

## PhaseDistortionOscillator
**Path:** [phase_distortion_oscillator.h](../../dsp/include/krate/dsp/processors/phase_distortion_oscillator.h) | **Since:** 0.11.0

Casio CZ-style Phase Distortion oscillator implementing 8 waveform types with DCW (Digitally Controlled Wave) morphing. At distortion=0.0, all waveforms produce pure sine. At distortion=1.0, each produces its characteristic shape.

**Use when:**
- Need CZ-style phase distortion synthesis
- Want smooth morphing from sine to characteristic waveforms
- Building hybrid synthesis (PD oscillator as FM carrier)
- Need resonant filter-like timbres without actual filters

```cpp
enum class PDWaveform : uint8_t {
    Saw, Square, Pulse, DoubleSine, HalfSine,
    ResonantSaw, ResonantTriangle, ResonantTrapezoid
};

class PhaseDistortionOscillator {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
    void setFrequency(float hz) noexcept;
    void setWaveform(PDWaveform waveform) noexcept;
    void setDistortion(float amount) noexcept;  // [0, 1]
    void setMaxResonanceFactor(float factor) noexcept;  // [0, 16]
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] PDWaveform getWaveform() const noexcept;
    [[nodiscard]] float getDistortion() const noexcept;
    [[nodiscard]] double phase() const noexcept;
    [[nodiscard]] bool phaseWrapped() const noexcept;
    void resetPhase(double newPhase = 0.0) noexcept;
};
```

**Usage Example: Basic PD Waveform Generation**

```cpp
PhaseDistortionOscillator osc;
osc.prepare(44100.0);
osc.setFrequency(440.0f);
osc.setWaveform(PDWaveform::Saw);
osc.setDistortion(0.7f);  // 70% distortion

// Generate audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = osc.process();
}
```

**Usage Example: PD Oscillator as FM Carrier**

```cpp
// Modulator: standard FM operator
FMOperator modulator;
modulator.prepare(44100.0);
modulator.setFrequency(440.0f);
modulator.setRatio(2.0f);
modulator.setLevel(0.3f);

// Carrier: PD oscillator for unique timbre
PhaseDistortionOscillator carrier;
carrier.prepare(44100.0);
carrier.setFrequency(440.0f);
carrier.setWaveform(PDWaveform::ResonantSaw);
carrier.setDistortion(0.5f);

// Process with phase modulation
for (size_t i = 0; i < numSamples; ++i) {
    modulator.process();
    float pm = modulator.lastRawOutput() * modulator.getLevel();
    output[i] = carrier.process(pm);
}
```

**Waveform Types:**

| Waveform | Technique | Character |
|----------|-----------|-----------|
| Saw | Two-segment phase transfer | Bright, harmonically rich |
| Square | Four-segment phase transfer | Hollow, odd harmonics |
| Pulse | Asymmetric duty cycle | Nasal, bright |
| DoubleSine | Phase doubling | Octave-doubled tone |
| HalfSine | Phase reflection | Half-wave rectified |
| ResonantSaw | Falling sawtooth window | Filter-like resonance |
| ResonantTriangle | Triangle window | Softer resonance |
| ResonantTrapezoid | Trapezoid window | Wide resonance |

**Parameter Ranges:**

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| frequency | [0, Nyquist) | 440 Hz | Fundamental frequency |
| distortion | [0, 1] | 0.0 | DCW amount (0=sine) |
| waveform | enum | Saw | Waveform type |
| maxResonanceFactor | [0, 16] | 8.0 | Resonant peak multiplier |

**Key Behaviors:**
- distortion=0.0: All waveforms produce pure sine (THD < 0.5%)
- distortion=1.0: Full characteristic waveform shape
- Resonant waveforms: Peak frequency = fundamental * (1 + distortion * maxResonanceFactor)

**Gotchas:**
- `prepare()` is NOT real-time safe (generates cosine wavetable)
- `process()` returns 0.0 if `prepare()` has not been called
- NaN/Inf inputs to setFrequency() are sanitized to 0.0
- NaN/Inf inputs to setDistortion() preserve previous value
- Output clamped to [-2.0, 2.0] with NaN replaced by 0.0
- Each instance owns ~90 KB for internal cosine wavetable

**Performance:** < 0.5 ms for 1 second of audio at 44.1 kHz (Layer 2 budget)

**Memory:** ~90 KB per instance (dominated by mipmapped cosine wavetable)

**Dependencies:** Layer 0 (phase_utils.h, math_constants.h, db_utils.h, interpolation.h, wavetable_data.h), Layer 1 (wavetable_oscillator.h, wavetable_generator.h)

---

## AdditiveOscillator
**Path:** [additive_oscillator.h](../../dsp/include/krate/dsp/processors/additive_oscillator.h) | **Since:** 0.12.0

IFFT-based additive synthesis oscillator with up to 128 partials. Uses overlap-add resynthesis with Hann windowing at 75% overlap for efficient O(N log N) synthesis independent of partial count.

**Use when:**
- Building organ-like timbres with explicit harmonic control
- Creating bell/piano sounds with inharmonicity
- Need spectral morphing or resynthesis applications
- Want per-partial amplitude, frequency ratio, and phase control

```cpp
class AdditiveOscillator {
    static constexpr size_t kMaxPartials = 128;
    static constexpr size_t kMinFFTSize = 512;
    static constexpr size_t kMaxFFTSize = 4096;
    static constexpr size_t kDefaultFFTSize = 2048;

    void prepare(double sampleRate, size_t fftSize = kDefaultFFTSize) noexcept;
    void reset() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Fundamental frequency
    void setFundamental(float hz) noexcept;

    // Per-partial control (1-based indexing)
    void setPartialAmplitude(size_t partialNumber, float amplitude) noexcept;
    void setPartialFrequencyRatio(size_t partialNumber, float ratio) noexcept;
    void setPartialPhase(size_t partialNumber, float phase) noexcept;

    // Macro controls
    void setNumPartials(size_t count) noexcept;
    void setSpectralTilt(float tiltDb) noexcept;      // [-24, +12] dB/octave
    void setInharmonicity(float B) noexcept;          // [0, 0.1] bell-like stretching

    // Query
    [[nodiscard]] size_t latency() const noexcept;    // Returns FFT size
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] size_t fftSize() const noexcept;
    [[nodiscard]] float fundamental() const noexcept;
    [[nodiscard]] size_t numPartials() const noexcept;
};
```

**Usage Example: Organ-like Harmonic Series**

```cpp
AdditiveOscillator osc;
osc.prepare(44100.0, 2048);
osc.setFundamental(261.63f);  // Middle C
osc.setNumPartials(8);

// Organ drawbar-like amplitudes
osc.setPartialAmplitude(1, 1.0f);   // 8'
osc.setPartialAmplitude(2, 0.5f);   // 4'
osc.setPartialAmplitude(3, 0.25f);  // 2 2/3'
osc.setPartialAmplitude(4, 0.125f); // 2'

osc.processBlock(output, 512);
```

**Usage Example: Bell-like Inharmonicity**

```cpp
AdditiveOscillator bell;
bell.prepare(44100.0);
bell.setFundamental(440.0f);
bell.setNumPartials(16);
bell.setInharmonicity(0.01f);  // Bell-like stretching
bell.setSpectralTilt(-6.0f);   // Natural high-frequency rolloff

// f_n = n * f1 * sqrt(1 + B * n^2)
// Partial 10 at B=0.01: 440 * 10 * sqrt(1.01) = 4622 Hz (vs 4400 harmonic)

bell.processBlock(output, 512);
```

**Parameter Ranges:**

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| fundamental | [0, Nyquist) | 440 Hz | 0 produces silence |
| partialNumber | [1, 128] | - | 1-based indexing |
| amplitude | [0, 1] | 1.0 (partial 1) | Per-partial amplitude |
| ratio | (0, 64] | N (partial number) | Frequency multiplier |
| phase | [0, 1) | 0.0 | Applied at reset() |
| numPartials | [1, 128] | 1 | Active partial count |
| spectralTilt | [-24, +12] | 0 dB/oct | Brightness control |
| inharmonicity | [0, 0.1] | 0.0 | Piano-string formula |
| fftSize | 512, 1024, 2048, 4096 | 2048 | Set at prepare() |

**Key Behaviors:**
- Latency = FFT size (e.g., 2048 samples at default)
- Phase changes take effect at next reset(), not mid-playback
- Partials above Nyquist are automatically excluded
- Output sanitized to [-2, +2] with NaN/Inf replaced by 0
- Spectral tilt formula: amplitude *= pow(10, tiltDb * log2(n) / 20)
- Inharmonicity formula: f_n = n * f1 * sqrt(1 + B * n^2)

**Gotchas:**
- `prepare()` is NOT real-time safe (allocates FFT buffers, ~80 KB at FFT 2048)
- `processBlock()` outputs zeros if `prepare()` has not been called
- Fundamental = 0 produces silence (use for note-off)
- Out-of-range partial numbers (0, >128) are silently ignored
- NaN/Inf inputs to setters are sanitized to safe defaults

**Performance:** O(N log N) per FFT frame, independent of partial count

**Memory:** ~80 KB per instance at FFT size 2048 (FFT twiddles, spectrum buffer, output buffer)

**Dependencies:** Layer 0 (window_functions.h, phase_utils.h, math_constants.h, db_utils.h), Layer 1 (fft.h)

---

## ChaosOscillator
**Path:** [chaos_oscillator.h](../../dsp/include/krate/dsp/processors/chaos_oscillator.h) | **Since:** 0.11.0

Audio-rate chaos oscillator implementing 5 attractor types with RK4 adaptive substepping for numerical stability.

**Use when:**
- Need evolving, non-repetitive textures for experimental synthesis
- Want organic, unpredictable timbral movement
- Building chaos-based modulation or audio sources
- Require smooth, controllable chaotic behavior with approximate pitch tracking

**Attractor Types:**
- **Lorenz**: Smooth, flowing, three-lobe butterfly pattern (classic chaos)
- **Rossler**: Asymmetric single spiral, buzzy character
- **Chua**: Harsh double-scroll with abrupt transitions
- **Duffing**: Driven nonlinear oscillator, harmonically rich
- **VanDerPol**: Relaxation oscillations, pulse-like waveform

```cpp
enum class ChaosAttractor : uint8_t { Lorenz, Rossler, Chua, Duffing, VanDerPol };

class ChaosOscillator {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Parameter Setters
    void setAttractor(ChaosAttractor type) noexcept;  // Attractor model
    void setFrequency(float hz) noexcept;             // [0.1, 20000] Hz
    void setChaos(float amount) noexcept;             // [0, 1] normalized
    void setCoupling(float amount) noexcept;          // [0, 1] external coupling
    void setOutput(size_t axis) noexcept;             // 0=x, 1=y, 2=z

    // Processing
    [[nodiscard]] float process(float externalInput = 0.0f) noexcept;
    void processBlock(float* output, size_t numSamples, const float* extInput = nullptr) noexcept;

    // Getters
    [[nodiscard]] ChaosAttractor getAttractor() const noexcept;
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] float getChaos() const noexcept;
    [[nodiscard]] float getCoupling() const noexcept;
    [[nodiscard]] size_t getOutput() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

**Basic Usage:**
```cpp
ChaosOscillator osc;
osc.prepare(44100.0);
osc.setAttractor(ChaosAttractor::Lorenz);
osc.setFrequency(220.0f);
osc.setChaos(1.0f);  // Maximum chaos (rho=28 for Lorenz)

// Process single sample
float sample = osc.process();

// Process block
float buffer[512];
osc.processBlock(buffer, 512);

// With external coupling (modulation/synchronization)
osc.setCoupling(0.3f);
float extSignal[512] = { /* ... */ };
osc.processBlock(buffer, 512, extSignal);
```

**Parameter Ranges:**

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| frequency | [0.1, 20000] Hz | 220 Hz | Approximate pitch tracking |
| chaos | [0, 1] | 1.0 | Maps to per-attractor range |
| coupling | [0, 1] | 0.0 | External input strength |
| output axis | [0, 2] | 0 (x) | Which state variable to output |

**Chaos Parameter Mapping:**

| Attractor | Parameter | Range at chaos=[0,1] |
|-----------|-----------|---------------------|
| Lorenz | rho | [20, 28] |
| Rossler | c | [4, 8] |
| Chua | alpha | [12, 18] |
| Duffing | A (drive) | [0.2, 0.5] |
| VanDerPol | mu | [0.5, 5.0] |

**Key Behaviors:**
- Output bounded to [-1, +1] via tanh soft-limiting
- DC blocked at 10 Hz to remove offset
- Automatic divergence detection and reset (within 1ms)
- RK4 integration with adaptive substepping for stability
- Each attractor has distinct spectral character (>20% centroid difference)
- External coupling adds to x-derivative for synchronization

**Distinction from ChaosModSource:**
- **ChaosOscillator**: Audio-rate, RK4 integration, 5 attractors, frequency control
- **ChaosModSource**: Control-rate, Euler integration, 4 attractors, rate control

**Gotchas:**
- `prepare()` must be called before processing
- Frequency is approximate due to chaotic nature (within 0.5-1.5x target)
- Attractor changes reset state to initial conditions
- Output axis for 2D systems (Duffing, VanDerPol): z outputs 0

**Performance:** <1% CPU per instance at 44.1kHz stereo

**Memory:** ~200 bytes per instance (no allocations, embedded DCBlocker)

**Dependencies:** Layer 0 (fast_math.h, db_utils.h, math_constants.h), Layer 1 (dc_blocker.h)

---

## FormantOscillator
**Path:** [formant_oscillator.h](../../dsp/include/krate/dsp/processors/formant_oscillator.h) | **Since:** 0.14.2

FOF (Fonction d'Onde Formantique) synthesis oscillator for vowel-like sound generation.

**Use when:**
- Generating vowel-like sounds directly (no input signal required)
- Creating vocal synthesis without external excitation
- Need smooth vowel morphing for evolving timbres
- Precise per-formant control for custom vocal sounds

**Distinction from FormantFilter:**
- **FormantOscillator**: Generates audio via FOF grain synthesis, no input needed
- **FormantFilter**: Applies 3 bandpass resonances to an existing signal

```cpp
class FormantOscillator {
    static constexpr size_t kNumFormants = 5;
    static constexpr size_t kGrainsPerFormant = 8;
    static constexpr float kMasterGain = 0.4f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Fundamental control
    void setFundamental(float hz) noexcept;  // [20, 2000] Hz

    // Vowel selection
    void setVowel(Vowel vowel) noexcept;     // A, E, I, O, U
    void morphVowels(Vowel from, Vowel to, float mix) noexcept;
    void setMorphPosition(float position) noexcept;  // [0, 4]

    // Per-formant control
    void setFormantFrequency(size_t index, float hz) noexcept;
    void setFormantBandwidth(size_t index, float hz) noexcept;
    void setFormantAmplitude(size_t index, float amp) noexcept;
};
```

**Vowel Presets (Bass Male Voice):**

| Vowel | F1 (Hz) | F2 (Hz) | F3 (Hz) | F4 (Hz) | F5 (Hz) |
|-------|---------|---------|---------|---------|---------|
| A /a/ | 600 | 1040 | 2250 | 2450 | 2750 |
| E /e/ | 400 | 1620 | 2400 | 2800 | 3100 |
| I /i/ | 250 | 1750 | 2600 | 3050 | 3340 |
| O /o/ | 400 | 750 | 2400 | 2600 | 2900 |
| U /u/ | 350 | 600 | 2400 | 2675 | 2950 |

**FOF Grain Architecture:**
- 5 parallel formant generators (F1-F5)
- 8-grain fixed-size pool per formant (40 grains total)
- 3ms attack (half-cycle raised cosine)
- 20ms grain duration
- Exponential decay controlled by bandwidth

**Gotchas:**
- `prepare()` must be called before processing
- Output may briefly exceed 1.0 during peak grain alignment (max ~1.12)
- Spectral peaks are at harmonics nearest to formant frequencies
- Setting all formant amplitudes to 0 produces silence

**Performance:** <0.5% CPU per instance at 44.1kHz mono

**Memory:** ~2.5KB per instance (no allocations, fixed grain pools)

**Dependencies:** Layer 0 (phase_utils.h, filter_tables.h, math_constants.h)

---

## ParticleOscillator
**Path:** [particle_oscillator.h](../../dsp/include/krate/dsp/processors/particle_oscillator.h) | **Since:** 0.14.2

Particle/swarm synthesis oscillator generating complex textural timbres from up to 64 lightweight sine oscillators ("particles") with individual frequency scatter, drift, lifetime, and spawn behavior.

**Use when:**
- Generating dense granular cloud textures from overlapping sine grains
- Creating organic, living tones with evolving spectral content
- Need multi-voice oscillation with per-particle frequency drift
- Building particle/swarm synthesis with controllable spawn patterns
- Want textured timbres that evolve continuously (not static additive)

**Distinction from AdditiveOscillator:**
- **ParticleOscillator**: Stochastic, short-lived sine grains with drift and spawn scheduling
- **AdditiveOscillator**: Deterministic harmonic partials with IFFT-based resynthesis

```cpp
enum class SpawnMode : uint8_t { Regular, Random, Burst };

class ParticleOscillator {
    static constexpr size_t kMaxParticles = 64;
    static constexpr size_t kEnvTableSize = 256;
    static constexpr float kOutputClamp = 1.5f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Frequency control
    void setFrequency(float centerHz) noexcept;           // [1, Nyquist) Hz
    void setFrequencyScatter(float semitones) noexcept;   // [0, 48] semitones

    // Population control
    void setDensity(float particles) noexcept;             // [1, 64] particles
    void setLifetime(float ms) noexcept;                   // [1, 10000] ms

    // Spawn behavior
    void setSpawnMode(SpawnMode mode) noexcept;            // Regular/Random/Burst
    void triggerBurst() noexcept;                          // Burst mode only

    // Envelope and drift
    void setEnvelopeType(GrainEnvelopeType type) noexcept; // 6 types
    void setDriftAmount(float amount) noexcept;            // [0, 1]

    // Seeding and query
    void seed(uint32_t seedValue) noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] size_t activeParticleCount() const noexcept;
};
```

**Spawn Modes:**

| Mode | Behavior | Use Case |
|------|----------|----------|
| Regular | Evenly spaced intervals (lifetime/density) | Steady, predictable textures |
| Random | Poisson-distributed stochastic timing | Natural, organic clouds |
| Burst | Manual trigger via triggerBurst() | Impact transients, unison swells |

**Envelope Types:** All 6 GrainEnvelopeType shapes (Hann, Trapezoid, Sine, Blackman, Linear, Exponential) are precomputed during prepare(). Switching is a zero-cost index swap.

**Normalization:** Output is normalized by 1/sqrt(density) for stable perceived loudness across all density settings. Hard clamp at [-1.5, +1.5] prevents downstream damage.

**Gotchas:**
- `prepare()` must be called before processing (outputs silence otherwise)
- `triggerBurst()` is a no-op in Regular and Random modes
- Scatter offsets are assigned at spawn time; changing scatter mid-stream affects only new particles
- Normalization uses target density (not active count) to avoid amplitude pumping
- Sine wavetable (2048 entries) is used instead of std::sin for performance

**Performance:** <2% CPU per instance at 44.1kHz mono with 64 particles (SC-003 target <0.5% on reference hardware)

**Memory:** ~10KB per instance (64 particles + 6 envelope tables + sine wavetable, no allocations)

**Dependencies:** Layer 0 (random.h, grain_envelope.h, pitch_utils.h, math_constants.h, db_utils.h)

---

## Rungler
**Path:** [rungler.h](../../dsp/include/krate/dsp/processors/rungler.h) | **Since:** 0.14.3

Benjolin-inspired chaotic stepped-voltage generator. Two cross-modulating triangle oscillators drive an N-bit shift register with XOR feedback, producing evolving stepped sequences via a 3-bit DAC. Five simultaneous outputs: osc1 triangle, osc2 triangle, rungler CV, PWM comparator, and mixed.

**Use when:**
- Generating chaotic, evolving stepped sequences for modulation
- Need a self-modulating oscillator system with edge-of-chaos behavior
- Want CV-like stepped waveforms with configurable smoothing
- Building generative patches that evolve over time without external modulation
- Need multiple correlated-but-different output signals from one source

**Distinction from ChaosOscillator:**
- **Rungler**: Shift-register based, produces stepped/quantized output, two triangle oscillators with cross-modulation, Benjolin-inspired
- **ChaosOscillator**: Continuous attractor-based (Lorenz, Rossler, etc.), smooth output, single oscillator with RK4 integration

```cpp
class Rungler {
    struct Output {
        float osc1;    // Oscillator 1 triangle [-1, +1]
        float osc2;    // Oscillator 2 triangle [-1, +1]
        float rungler; // Rungler CV (filtered DAC) [0, +1]
        float pwm;     // PWM comparator [-1, +1]
        float mixed;   // (osc1 + osc2) * 0.5 [-1, +1]
    };

    static constexpr float kMinFrequency = 0.1f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr size_t kMinBits = 4;
    static constexpr size_t kMaxBits = 16;
    static constexpr size_t kDefaultBits = 8;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void seed(uint32_t seedValue) noexcept;
    [[nodiscard]] Output process() noexcept;
    void processBlock(Output* output, size_t numSamples) noexcept;
    void processBlockMixed(float* output, size_t numSamples) noexcept;
    void processBlockRungler(float* output, size_t numSamples) noexcept;

    // Parameter setters
    void setOsc1Frequency(float hz) noexcept;        // [0.1, 20000] Hz
    void setOsc2Frequency(float hz) noexcept;        // [0.1, 20000] Hz
    void setOsc1RunglerDepth(float depth) noexcept;  // [0, 1]
    void setOsc2RunglerDepth(float depth) noexcept;  // [0, 1]
    void setRunglerDepth(float depth) noexcept;      // Sets both [0, 1]
    void setFilterAmount(float amount) noexcept;     // [0, 1] CV smoothing
    void setRunglerBits(size_t bits) noexcept;       // [4, 16]
    void setLoopMode(bool loop) noexcept;            // Chaos vs loop
};
```

**Modes:**

| Mode | Data Input | Behavior |
|------|-----------|----------|
| Chaos (default) | Osc1 pulse XOR last bit | Evolving, non-repeating patterns |
| Loop | Last bit recycled | Fixed repeating pattern |

**Usage Example:**
```cpp
Rungler rungler;
rungler.prepare(44100.0);
rungler.setOsc1Frequency(200.0f);
rungler.setOsc2Frequency(300.0f);
rungler.setRunglerDepth(0.5f);        // Moderate cross-modulation
rungler.setFilterAmount(0.0f);        // Raw stepped output

auto out = rungler.process();
// out.osc1    -> triangle wave [-1, +1]
// out.rungler -> 8-level stepped CV [0, +1]
// out.mixed   -> (osc1 + osc2) * 0.5

// Deterministic output for tests:
rungler.seed(12345);
rungler.reset();
```

**Gotchas:**
- `prepare()` must be called before processing (outputs silence otherwise)
- Triangle oscillator increment is `4 * freq / sampleRate` (bipolar [-1, +1] traverses 4 units per cycle)
- NaN/Inf inputs to frequency setters are sanitized to defaults (200 Hz for osc1, 300 Hz for osc2)
- In loop mode, an all-zero register stays at zero DAC output (documented limitation)
- Shift register is seeded randomly on `prepare()`; call `seed()` + `reset()` for deterministic output
- Register length changes (`setRunglerBits()`) truncate the register immediately

**Performance:** <0.1% CPU per instance at 44.1kHz (well within 0.5% Layer 2 budget). Only simple arithmetic, no trig/FFT.

**Memory:** ~100 bytes per instance (no allocations, OnePoleLP filter state + oscillator phases + register)

**Dependencies:** Layer 0 (random.h, db_utils.h), Layer 1 (one_pole.h)

---

## FormantPreserver
**Path:** [formant_preserver.h](../../dsp/include/krate/dsp/processors/formant_preserver.h) | **Since:** 0.14.4

Cepstral spectral envelope extraction and reapplication for formant-preserving transformations. Separates the slowly-varying resonance structure (formants) from the fine harmonic structure using the cepstral method: log-magnitude -> IFFT -> low-pass lifter (Hann-windowed) -> FFT -> smoothed envelope.

**Use when:**
- Implementing formant-preserving pitch shift (keep vocal character while changing pitch)
- Shifting formant structure independently of pitch (vowel morphing, gender effects)
- Extracting a smooth spectral envelope from arbitrary magnitude spectra
- Any spectral transformation that needs to separate fine vs. coarse spectral structure

```cpp
class FormantPreserver {
    static constexpr float kMinMagnitude = 1e-10f;

    void prepare(std::size_t fftSize, double sampleRate) noexcept;
    void reset() noexcept;
    void extractEnvelope(const float* magnitudes, float* outputEnvelope) noexcept;
    void applyFormantPreservation(
        const float* shiftedMagnitudes,
        const float* originalEnvelope,
        const float* shiftedEnvelope,
        float* outputMagnitudes,
        std::size_t numBins) const noexcept;
};
```

**Gotchas:**
- `prepare()` takes `double sampleRate` (not float)
- Lifter cutoff is fixed at prepare-time: `lifterBins = 0.0015 * sampleRate` (~66 bins at 44.1kHz)
- Internal FFT adds ~16 KB memory overhead
- Envelopes may be unreliable for fundamentals below ~80 Hz (quefrency cutoff limitation)

**Performance:** Negligible when called once per STFT hop (only at frame boundaries).

**Memory:** ~70 KB at fftSize=2048 (internal FFT + work buffers + lifter window)

**Dependencies:** Layer 0 (math_constants.h, window_functions.h), Layer 1 (fft.h)

---

## SpectralFreezeOscillator
**Path:** [spectral_freeze_oscillator.h](../../dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h) | **Since:** 0.14.4

Captures a single FFT frame from audio input and continuously resynthesizes it as a frozen spectral drone. Uses coherent per-bin phase advancement with IFFT overlap-add resynthesis (Hann synthesis window, 75% overlap). Supports three spectral manipulations: pitch shift via bin remapping, spectral tilt (brightness control), and formant shift via cepstral envelope.

**Use when:**
- Turning transient audio events into infinite sustain/drone textures
- Freezing and manipulating spectral snapshots for sound design
- Building pad/drone synthesizers from arbitrary audio input
- Need spectral manipulation (pitch, tilt, formant) on a frozen spectrum

```cpp
class SpectralFreezeOscillator {
    // Lifecycle
    void prepare(double sampleRate, size_t fftSize = 2048) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Freeze/Unfreeze
    void freeze(const float* inputBlock, size_t blockSize) noexcept;
    void unfreeze() noexcept;
    [[nodiscard]] bool isFrozen() const noexcept;

    // Processing
    void processBlock(float* output, size_t numSamples) noexcept;

    // Parameters
    void setPitchShift(float semitones) noexcept;     // [-24, +24]
    void setSpectralTilt(float dbPerOctave) noexcept;  // [-24, +24]
    void setFormantShift(float semitones) noexcept;    // [-24, +24]
    [[nodiscard]] float getPitchShift() const noexcept;
    [[nodiscard]] float getSpectralTilt() const noexcept;
    [[nodiscard]] float getFormantShift() const noexcept;

    // Query
    [[nodiscard]] size_t getLatencySamples() const noexcept;
    [[nodiscard]] size_t getFftSize() const noexcept;
    [[nodiscard]] size_t getHopSize() const noexcept;
};
```

**Gotchas:**
- `freeze()` does NOT apply an analysis window (raw FFT capture prevents sidelobe beating)
- Output is quantized to FFT bin center frequencies (bin spacing = sampleRate / fftSize)
- Parameter changes take effect on synthesis frame boundaries (every hopSize samples)
- FFT size clamped to power-of-2 in [256, 8192]
- `processBlock()` outputs silence when not frozen or not prepared
- Unfreeze crossfades to silence over one hop duration (fftSize/4 samples)

**Performance:** <0.5% CPU single core @ 44.1kHz, 512 samples, 2048 FFT (SC-003).

**Memory:** ~90 KB without formant, ~170 KB with formant shift (for fftSize=2048).

**Dependencies:** Layer 0 (math_constants.h, window_functions.h, pitch_utils.h, db_utils.h), Layer 1 (fft.h, spectral_buffer.h, spectral_utils.h), Layer 2 (formant_preserver.h)

---

## MultiStageEnvelope (Multi-Stage Envelope Generator)
**Path:** [multi_stage_envelope.h](../../dsp/include/krate/dsp/processors/multi_stage_envelope.h) | **Since:** 0.16.0

Configurable multi-stage envelope generator (4-8 stages) with per-stage time/level/curve, sustain point selection, loop points for LFO-like behavior, and retrigger modes.

```cpp
struct EnvStageConfig {
    float targetLevel = 0.0f;     // [0.0, 1.0]
    float timeMs = 100.0f;       // [0.0, 10000.0]
    EnvCurve curve = EnvCurve::Exponential;
};

enum class MultiStageEnvState : uint8_t { Idle, Running, Sustaining, Releasing };

class MultiStageEnvelope {
    static constexpr int kMinStages = 4;
    static constexpr int kMaxStages = 8;
    static constexpr float kMaxStageTimeMs = 10000.0f;

    // Lifecycle
    void prepare(float sampleRate) noexcept;
    void reset() noexcept;
    void gate(bool on) noexcept;

    // Stage configuration
    void setNumStages(int count) noexcept;           // [4, 8]
    void setStageLevel(int stage, float level) noexcept;
    void setStageTime(int stage, float ms) noexcept;
    void setStageCurve(int stage, EnvCurve curve) noexcept;
    void setStage(int stage, float level, float ms, EnvCurve curve) noexcept;

    // Sustain and loop
    void setSustainPoint(int stage) noexcept;
    void setLoopEnabled(bool enabled) noexcept;
    void setLoopStart(int stage) noexcept;
    void setLoopEnd(int stage) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setRetriggerMode(RetriggerMode mode) noexcept;

    // Processing
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Queries
    [[nodiscard]] MultiStageEnvState getState() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool isReleasing() const noexcept;
    [[nodiscard]] float getOutput() const noexcept;
    [[nodiscard]] int getCurrentStage() const noexcept;
    [[nodiscard]] int getNumStages() const noexcept;
    [[nodiscard]] int getSustainPoint() const noexcept;
    [[nodiscard]] bool getLoopEnabled() const noexcept;
    [[nodiscard]] int getLoopStart() const noexcept;
    [[nodiscard]] int getLoopEnd() const noexcept;
};
```

**Curve algorithms per stage:**

| Curve | Algorithm | Per-sample cost |
|-------|-----------|----------------|
| Exponential | Normalized one-pole (EarLevel) mapped to actual range | 2 mul + 2 add |
| Linear | Phase-based interpolation | 1 mul + 1 add |
| Logarithmic | Quadratic phase mapping (phase^2 rising, 1-(1-phase)^2 falling) | 2 mul + 1 add |

**When to use:**
- Complex amplitude envelopes beyond ADSR (spit brass, dual-decay, delayed-attack)
- LFO-like cyclic modulation with complex waveshapes (via loop mode)
- MSEG-style modulation sources
- Korg Poly-800 DEG, Yamaha DX7, or Buchla 281 inspired envelope shapes

**When to use ADSREnvelope instead:**
- Standard ADSR envelope is sufficient
- Need velocity scaling (not included in MultiStageEnvelope)
- Per-stage curve control is not needed

**Performance:** ~2.35 ns/sample, 0.01% CPU at 44.1kHz (SC-003 target: <0.05%).

**Dependencies:** Layer 0 (db_utils.h), Layer 1 (envelope_utils.h)

---

## MonoHandler
**Path:** [mono_handler.h](../../dsp/include/krate/dsp/processors/mono_handler.h) | **Since:** 0.16.0

Monophonic note management with legato and portamento. Manages a 16-entry fixed-capacity note stack, implements three note priority algorithms (LastNote, LowNote, HighNote), provides legato mode for envelope retrigger suppression, and offers constant-time portamento that operates linearly in pitch space (semitones). Does not produce audio -- returns `MonoNoteEvent` instructions for the caller (synth voice) to act on.

```cpp
// Event descriptor returned by noteOn/noteOff
struct MonoNoteEvent {
    float frequency;    // Hz (12-TET, A4=440Hz)
    uint8_t velocity;   // 0-127
    bool retrigger;     // true = restart envelopes
    bool isNoteOn;      // true = note active
};

enum class MonoMode : uint8_t { LastNote, LowNote, HighNote };
enum class PortaMode : uint8_t { Always, LegatoOnly };

class MonoHandler {
    // Initialization
    void prepare(double sampleRate) noexcept;

    // Note events -- return MonoNoteEvent instructions
    [[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept;
    [[nodiscard]] MonoNoteEvent noteOff(int note) noexcept;

    // Portamento -- call once per sample
    [[nodiscard]] float processPortamento() noexcept;
    [[nodiscard]] float getCurrentFrequency() const noexcept;

    // Configuration
    void setMode(MonoMode mode) noexcept;
    void setLegato(bool enabled) noexcept;
    void setPortamentoTime(float ms) noexcept;   // 0-10000 ms
    void setPortamentoMode(PortaMode mode) noexcept;

    // State
    [[nodiscard]] bool hasActiveNote() const noexcept;
    void reset() noexcept;
};
```

**Usage example:**

```cpp
MonoHandler mono;
mono.prepare(44100.0);
mono.setPortamentoTime(50.0f);
mono.setLegato(true);

// On MIDI note-on
auto event = mono.noteOn(60, 100);
if (event.retrigger) {
    envelope.trigger();
}

// Per-sample in audio callback
for (int i = 0; i < blockSize; ++i) {
    float freq = mono.processPortamento();
    oscillator.setFrequency(freq);
    output[i] = oscillator.process() * envelope.process();
}

// On MIDI note-off
auto offEvent = mono.noteOff(60);
if (!offEvent.isNoteOn) {
    envelope.release();
}
```

**When to use:**
- Mono synth voice where only one note sounds at a time
- Mono mode in a polyphonic synth engine (complementary to VoiceAllocator)
- Lead and bass synth patches requiring legato phrasing and portamento
- Classic mono synth emulation (Minimoog low-note, Korg MS-20 high-note, etc.)

**When to use VoiceAllocator instead:**
- Polyphonic voice management (multiple simultaneous notes)
- Voice stealing, unison, round-robin allocation

**Memory:** ~72 bytes per instance (16-entry note stack + portamento state). Real-time safe, single-threaded.

**Dependencies:** Layer 0 (midi_utils.h, pitch_utils.h, db_utils.h), Layer 1 (LinearRamp from smoother.h)

---

## NoteProcessor
**Path:** [note_processor.h](../../dsp/include/krate/dsp/processors/note_processor.h) | **Since:** 0.17.0

MIDI note processing with pitch bend smoothing and velocity curve mapping. Converts MIDI note numbers to frequencies using 12-TET with configurable A4 tuning (400-480 Hz), applies smoothed pitch bend with configurable range (0-24 semitones), and maps velocity through four curve types (Linear, Soft, Hard, Fixed) with multi-destination depth scaling (amplitude, filter, envelope time).

```cpp
struct VelocityOutput {
    float amplitude    = 0.0f;  // Velocity scaled for amplitude
    float filter       = 0.0f;  // Velocity scaled for filter cutoff
    float envelopeTime = 0.0f;  // Velocity scaled for envelope time
};

class NoteProcessor {
    // Initialization
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Pitch bend
    void setPitchBend(float bipolar) noexcept;
    [[nodiscard]] float processPitchBend() noexcept;
    [[nodiscard]] float getFrequency(uint8_t note) const noexcept;

    // Pitch bend configuration
    void setPitchBendRange(float semitones) noexcept;   // [0, 24], default 2
    void setSmoothingTime(float ms) noexcept;            // default 5ms

    // Tuning
    void setTuningReference(float hz) noexcept;          // [400, 480], default 440
    [[nodiscard]] float getTuningReference() const noexcept;

    // Velocity
    void setVelocityCurve(VelocityCurve curve) noexcept;
    [[nodiscard]] VelocityOutput mapVelocity(int velocity) const noexcept;
    void setAmplitudeVelocityDepth(float depth) noexcept;   // [0, 1], default 1.0
    void setFilterVelocityDepth(float depth) noexcept;      // [0, 1], default 0.0
    void setEnvelopeTimeVelocityDepth(float depth) noexcept; // [0, 1], default 0.0
};
```

**Usage example:**

```cpp
NoteProcessor noteProc;
noteProc.prepare(44100.0);
noteProc.setTuningReference(442.0f);
noteProc.setVelocityCurve(VelocityCurve::Soft);
noteProc.setAmplitudeVelocityDepth(1.0f);
noteProc.setFilterVelocityDepth(0.5f);

// Per note-on
VelocityOutput vel = noteProc.mapVelocity(100);

// Per audio block (once, shared by all voices)
noteProc.setPitchBend(0.5f);
noteProc.processPitchBend();

// Per voice
float freq = noteProc.getFrequency(69);
```

**When to use:**
- Polyphonic/monophonic synthesizers needing note-to-frequency conversion
- Any voice engine requiring smoothed pitch bend
- Instruments needing configurable velocity response curves
- Multi-destination velocity routing (amplitude + filter + envelope)

**Usage pattern:** prepare once, processPitchBend once per block, getFrequency per voice, mapVelocity per note-on.

**Memory:** ~60 bytes per instance (OnePoleSmoother + cached state). Real-time safe, single-threaded.

**Dependencies:** Layer 0 (midi_utils.h, pitch_utils.h, db_utils.h), Layer 1 (OnePoleSmoother from smoother.h)

---

## TranceGate
**Path:** [trance_gate.h](../../dsp/include/krate/dsp/processors/trance_gate.h) | **Since:** 0.10.0

Rhythmic energy shaper -- pattern-driven VCA that applies a repeating step pattern as multiplicative gain to audio. Provides click-free transitions via asymmetric one-pole smoothing, Euclidean pattern generation, depth-controlled mixing, tempo-synced and free-running modes, and per-voice/global clock modes. Designed for placement post-distortion, pre-VCA in the Ruinae voice chain.

```cpp
struct GateStep {
    float level{1.0f};  // 0.0 = silence, 1.0 = full volume
};

struct TranceGateParams {
    int numSteps{16};                                // [2, 32]
    float rateHz{4.0f};                              // Free-run Hz [0.1, 100.0]
    float depth{1.0f};                               // [0.0, 1.0]: 0 = bypass, 1 = full
    float attackMs{2.0f};                            // [1.0, 20.0] ms
    float releaseMs{10.0f};                          // [1.0, 50.0] ms
    float phaseOffset{0.0f};                         // [0.0, 1.0]
    bool tempoSync{true};                            // true = tempo, false = free-run
    NoteValue noteValue{NoteValue::Sixteenth};       // Step note value
    NoteModifier noteModifier{NoteModifier::None};   // Dotted/Triplet
    bool perVoice{true};                             // true = reset on noteOn
};

class TranceGate {
    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Configuration
    void setParams(const TranceGateParams& params) noexcept;
    void setTempo(double bpm) noexcept;

    // Pattern Control
    void setStep(int index, float level) noexcept;
    void setPattern(const std::array<float, 32>& pattern, int numSteps) noexcept;
    void setEuclidean(int hits, int steps, int rotation = 0) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;      // mono
    void processBlock(float* left, float* right, size_t numSamples) noexcept; // stereo

    // Queries
    [[nodiscard]] float getGateValue() const noexcept;
    [[nodiscard]] int getCurrentStep() const noexcept;
};
```

**When to use:**
- Rhythmic amplitude gating (trance gate, sidechain-style pumping)
- Pattern-driven volume modulation with float-level steps (ghost notes, accents)
- Euclidean rhythm generation for polyrhythmic effects
- Any processor needing tempo-synced or free-running step patterns with click-free transitions
- Post-distortion, pre-VCA position in synthesizer voice chains

**Usage pattern:** prepare once, setParams/setTempo per block, process per sample or processBlock per block. Per-voice mode: call reset() on note-on. Global mode: continuous clock.

**Memory:** ~200 bytes per instance (32-float pattern array + 2 smoothers + timing state). Header-only, real-time safe, single-threaded.

**Dependencies:** Layer 0 (euclidean_pattern.h, note_value.h), Layer 1 (OnePoleSmoother from smoother.h)
