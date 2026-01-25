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
