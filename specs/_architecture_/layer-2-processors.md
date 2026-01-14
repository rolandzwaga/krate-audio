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
