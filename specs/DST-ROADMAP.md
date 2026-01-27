# Distortion DSP Roadmap

A comprehensive roadmap for implementing distortion building blocks in the KrateDSP library. All components are designed as reusable primitives (Layers 0-3) that can be composed into plugin effects.

## Design Principles

### 1. Layer Architecture

| Layer | Purpose | Distortion Role |
|-------|---------|-----------------|
| **Layer 0 (core/)** | Math utilities, constants | Transfer functions, sigmoid math, Chebyshev polynomials |
| **Layer 1 (primitives/)** | Stateless/simple stateful DSP | Waveshapers, DC blockers, ADAA primitives |
| **Layer 2 (processors/)** | Configurable processors | Tube stage, diode clipper, wavefolder, tape saturator |
| **Layer 3 (systems/)** | Complete systems | Amp channel, tape machine, fuzz pedal |

### 2. Anti-Aliasing Strategy

**External Oversampling (Composable)**
- Distortion primitives are "pure" - no internal oversampling
- Use existing `Oversampler<Factor, NumChannels>` externally
- User/system chooses oversampling factor based on distortion intensity
- Chain multiple distortions with single oversample→process→downsample

**ADAA for Specific Algorithms**
- Provide ADAA variants for hard clipping and simple waveshapers
- Implemented as separate primitives (e.g., `HardClipADAA`)
- Alternative to oversampling when CPU is constrained

### 3. Code Reuse

**Existing Components to Leverage:**

| Component | Location | Reuse For |
|-----------|----------|-----------|
| `FastMath::fastTanh()` | `core/fast_math.h` | All tanh-based saturation |
| `softClip()` / `hardClip()` | `core/dsp_utils.h` | Basic clipping |
| `Oversampler<>` | `primitives/oversampler.h` | Anti-aliasing wrapper |
| `Biquad` | `primitives/biquad.h` | DC blocking, tone shaping |
| `OnePoleSmoother` | `primitives/smoother.h` | Parameter smoothing |
| `BitCrusher` | `primitives/bit_crusher.h` | Already complete |
| `SampleRateReducer` | `primitives/sample_rate_reducer.h` | Already complete |
| `SaturationProcessor` | `processors/saturation_processor.h` | Reference (has 5 algorithms) |
| `Xorshift32` | `core/random.h` | Noise, dither |

---

## Phase 1: Layer 0 - Transfer Function Library

Foundational math utilities for all distortion algorithms.

### 1.1 Sigmoid Functions (`core/sigmoid.h`)

A collection of soft-clipping transfer functions with consistent API.

```cpp
namespace Krate::DSP::Sigmoid {
    float tanh(float x);           // Use FastMath::fastTanh
    float tanhVariable(float x, float drive);
    float atan(float x);           // Normalized to [-1, 1]
    float atanVariable(float x, float drive);
    float softClipCubic(float x);  // f(x) = 1.5x - 0.5x³
    float softClipQuintic(float x);
    float recipSqrt(float x);      // x / sqrt(x² + 1) - fast alternative
    float erf(float x);            // Error function (tape character)
    float erfApprox(float x);      // Fast approximation
}
```

**Status:** ✅ IMPLEMENTED - `core/sigmoid.h`

### 1.2 Asymmetric Shaping (`core/asymmetric.h`)

Functions for creating even harmonics via asymmetry.

```cpp
namespace Krate::DSP::Asymmetric {
    // Apply DC bias before saturation (simple asymmetry)
    float biasedSaturate(float x, float bias, SigmoidFunc func);
    
    // Different curves for positive/negative
    float dualCurve(float x, float posGain, float negGain, SigmoidFunc func);
    
    // Diode-style asymmetric (existing in SaturationProcessor)
    float diode(float x);
    
    // Tube-style polynomial (existing in SaturationProcessor)
    float tubePolynomial(float x);
}
```

**Status:** Partially exists in `SaturationProcessor` (tube, diode)
**Work:** Extract to standalone functions in Layer 0

### 1.3 Chebyshev Polynomials (`core/chebyshev.h`)

For precise harmonic control.

```cpp
namespace Krate::DSP::Chebyshev {
    // Individual polynomials T_n(x)
    float T1(float x);  // = x (fundamental)
    float T2(float x);  // = 2x² - 1 (2nd harmonic)
    float T3(float x);  // = 4x³ - 3x (3rd harmonic)
    float T4(float x);  // = 8x⁴ - 8x² + 1
    float T5(float x);  // = 16x⁵ - 20x³ + 5x
    // ... up to T8
    
    // Recursive evaluation for arbitrary order
    float Tn(float x, int n);
    
    // Weighted sum of harmonics
    float harmonicMix(float x, const float* weights, int numHarmonics);
}
```

**Status:** ✅ IMPLEMENTED - `core/chebyshev.h`

### 1.4 Wavefolding Math (`core/wavefold_math.h`)

Mathematical primitives for wavefolding algorithms.

```cpp
namespace Krate::DSP::WavefoldMath {
    // Lambert W function (for Lockhart/Serge wavefolder)
    float lambertW(float x);
    float lambertWApprox(float x);  // Faster approximation
    
    // Triangle fold (simple)
    float triangleFold(float x, float threshold = 1.0f);
    
    // Sine fold (Serge-style)
    float sineFold(float x, float gain);
}
```

**Status:** ✅ IMPLEMENTED - `core/wavefold_math.h`

---

## Phase 2: Layer 1 - Distortion Primitives

Stateless or simple stateful building blocks.

### 2.1 Waveshaper Primitive (`primitives/waveshaper.h`)

Generic waveshaper with selectable transfer function.

```cpp
enum class WaveshapeType : uint8_t {
    Tanh, TanhFast, Atan, Cubic, Quintic, ReciprocalSqrt, Erf,
    HardClip, Diode, Tube
};

class Waveshaper {
public:
    void setType(WaveshapeType type);
    void setDrive(float drive);      // Pre-gain
    void setAsymmetry(float bias);   // DC bias for even harmonics
    
    float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
};
```

**Status:** ✅ IMPLEMENTED - `primitives/waveshaper.h`

### 2.2 DC Blocker (`primitives/dc_blocker.h`)

Dedicated DC blocking filter (simpler than using Biquad).

```cpp
class DCBlocker {
public:
    void prepare(double sampleRate, float cutoffHz = 10.0f);
    void reset();
    
    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
    
private:
    float R_;      // Pole coefficient
    float x1_, y1_; // State
};
```

**Status:** ✅ IMPLEMENTED - `primitives/dc_blocker.h`

### 2.3 Hard Clip with ADAA (`primitives/hard_clip_adaa.h`)

Anti-aliased hard clipping without oversampling.

```cpp
class HardClipADAA {
public:
    enum class Order { First, Second };
    
    void setOrder(Order order);
    void setThreshold(float threshold);
    void reset();
    
    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
    
private:
    // Antiderivatives of hard clip
    static float F1(float x, float threshold);  // 1st antiderivative
    static float F2(float x, float threshold);  // 2nd antiderivative
    
    float x1_, x2_;  // Previous samples
    Order order_;
    float threshold_;
};
```

**Status:** ✅ IMPLEMENTED - `hard_clip_adaa.h`
**Work:** Complete - supports First and Second order ADAA

### 2.4 Hard Clip with polyBLAMP (`primitives/hard_clip_polyblamp.h`)

Anti-aliased hard clipping using polyBLAMP (Polynomial Bandlimited Ramp) correction.

```cpp
class HardClipPolyBLAMP {
public:
    void setThreshold(float threshold);
    void reset();

    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

private:
    float x1_;        // Previous input
    float y1_;        // Previous output
    float threshold_;
};
```

**Status:** ⚠️ PARTIAL - `hard_clip_polyblamp.h` exists but provides limited aliasing reduction
**Work:** Current basic 2-point implementation provides minimal aliasing benefit for arbitrary input signals. Full implementation would require:
- 4-point polyBLAMP kernel with Newton-Raphson iteration
- Polynomial fitting to corner boundaries per DAFx-16 paper
- For production use, **HardClipADAA is recommended** as it provides proven aliasing reduction.

### 2.5 Tanh with ADAA (`primitives/tanh_adaa.h`)

Anti-aliased tanh saturation.

```cpp
class TanhADAA {
public:
    void setDrive(float drive);
    void reset();
    
    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
    
private:
    // Antiderivative of tanh: F(x) = ln(cosh(x))
    static float F1(float x);
    
    float x1_;
    float drive_;
};
```

**Status:** ✅ IMPLEMENTED - `primitives/tanh_adaa.h`

### 2.5 Wavefolder Primitive (`primitives/wavefolder.h`)

Basic wavefolding without state (stateless transfer function).

```cpp
enum class WavefoldType : uint8_t {
    Triangle,   // Simple triangle fold
    Sine,       // sin(gain * x) - Serge-style
    Lockhart    // Lambert-W based
};

class Wavefolder {
public:
    void setType(WavefoldType type);
    void setFoldAmount(float amount);  // 1.0 = threshold, higher = more folds
    
    float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
};
```

**Status:** ✅ IMPLEMENTED - `primitives/wavefolder.h`

### 2.6 Chebyshev Waveshaper (`primitives/chebyshev_shaper.h`)

Harmonic control via Chebyshev polynomials.

```cpp
class ChebyshevShaper {
public:
    static constexpr int kMaxHarmonics = 8;
    
    void setHarmonicLevel(int harmonic, float level);  // 1-8
    void setAllHarmonics(const std::array<float, kMaxHarmonics>& levels);
    
    float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
    
private:
    std::array<float, kMaxHarmonics> harmonicLevels_;
};
```

**Status:** ✅ IMPLEMENTED - `primitives/chebyshev_shaper.h`

---

## Phase 3: Layer 2 - Distortion Processors

Configurable processors with gain staging, mixing, and optional features.

### 3.1 Refactor Existing `SaturationProcessor`

The current `SaturationProcessor` is good but has embedded oversampling. Options:
1. Keep as-is (backward compatible)
2. Create `SaturationProcessor2` without internal oversampling
3. Add flag to disable internal oversampling

**Recommendation:** Option 1 (keep) + create new modular processors below

### 3.2 Tube Stage Processor (`processors/tube_stage.h`)

Models a single triode gain stage.

```cpp
class TubeStage {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    // Parameters
    void setInputGain(float dB);     // Drive into stage
    void setOutputGain(float dB);    // Makeup gain
    void setBias(float bias);        // Tube bias point (affects asymmetry)
    void setSaturationAmount(float amount);  // 0-1
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    Waveshaper waveshaper_;
    DCBlocker dcBlocker_;
    OnePoleSmoother gainSmoother_;
    // ... parameters
};
```

**Status:** ✅ IMPLEMENTED - `processors/tube_stage.h`

### 3.3 Diode Clipper Processor (`processors/diode_clipper.h`)

Configurable diode clipping circuit model.

```cpp
enum class DiodeType : uint8_t {
    Silicon,    // Vf ~ 0.6-0.7V, sharp knee
    Germanium,  // Vf ~ 0.3V, soft knee
    LED,        // Vf ~ 1.8V, very hard
    Schottky    // Vf ~ 0.2V, softest
};

enum class ClipperTopology : uint8_t {
    Symmetric,      // Same diodes both polarities
    Asymmetric,     // Different thresholds
    SoftHard        // Soft one way, hard the other
};

class DiodeClipper {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    void setDiodeType(DiodeType type);
    void setTopology(ClipperTopology topology);
    void setDrive(float dB);
    void setMix(float mix);
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    // Uses Waveshaper internally with diode curves
    Waveshaper positivePath_;
    Waveshaper negativePath_;
    DCBlocker dcBlocker_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/diode_clipper.h`

### 3.4 Wavefolder Processor (`processors/wavefolder_processor.h`)

Full-featured wavefolder with anti-aliasing options.

```cpp
enum class WavefolderModel : uint8_t {
    Simple,     // Basic triangle fold
    Serge,      // Sine-based (sin(gain * x))
    Buchla259,  // 5-stage parallel architecture
    Lockhart    // Lambert-W based
};

class WavefolderProcessor {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    void setModel(WavefolderModel model);
    void setFoldAmount(float amount);   // 1.0 = just folding, higher = more folds
    void setSymmetry(float symmetry);   // -1 to +1 (asymmetric folding)
    void setMix(float mix);
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    Wavefolder wavefolder_;
    DCBlocker dcBlocker_;
    // For Buchla259: 5 parallel stages
    std::array<float, 5> stageThresholds_;
    std::array<float, 5> stageGains_;
};
```

**Status:** ✅ IMPLEMENTED - `processors/wavefolder_processor.h`

### 3.5 Tape Saturator Processor (`processors/tape_saturator.h`)

Tape-style saturation with hysteresis modeling.

```cpp
enum class TapeModel : uint8_t {
    Simple,         // tanh with pre/de-emphasis
    Hysteresis      // Jiles-Atherton model
};

enum class HysteresisSolver : uint8_t {
    RK2,    // Runge-Kutta 2nd order (fast)
    RK4,    // Runge-Kutta 4th order (balanced)
    NR4,    // Newton-Raphson 4 iterations (accurate)
    NR8     // Newton-Raphson 8 iterations (most accurate)
};

class TapeSaturator {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    void setModel(TapeModel model);
    void setSolver(HysteresisSolver solver);  // For Hysteresis model
    void setDrive(float dB);
    void setSaturation(float amount);
    void setBias(float bias);         // Tape bias
    void setMix(float mix);
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    TapeModel model_;
    // Simple model components
    Biquad preEmphasis_;
    Biquad deEmphasis_;
    Waveshaper saturator_;
    
    // Hysteresis model state
    float M_;       // Magnetization
    float H_prev_;  // Previous field
    // Jiles-Atherton parameters...
};
```

**Status:** ✅ IMPLEMENTED - `processors/tape_saturator.h`

### 3.6 Fuzz Processor (`processors/fuzz_processor.h`)

Fuzz Face style distortion.

```cpp
enum class FuzzType : uint8_t {
    Germanium,  // Warm, saggy, temperature-sensitive character
    Silicon     // Brighter, tighter, more aggressive
};

class FuzzProcessor {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    void setFuzzType(FuzzType type);
    void setFuzz(float amount);       // 0-1 (gain/saturation)
    void setVolume(float dB);         // Output level
    void setBias(float bias);         // Transistor bias (affects gating)
    void setTone(float tone);         // Simple tone control
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    Waveshaper saturator_;
    DCBlocker dcBlocker_;
    Biquad toneFilter_;
    float biasOffset_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/fuzz_processor.h`

### 3.7 Bitcrusher Processor (`processors/bitcrusher_processor.h`)

Combines existing BitCrusher + SampleRateReducer with gain staging.

```cpp
class BitcrusherProcessor {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    void setBitDepth(float bits);           // 4-16
    void setSampleRateReduction(float factor);  // 1-8
    void setDither(float amount);           // 0-1
    void setMix(float mix);
    void setPreGain(float dB);
    void setPostGain(float dB);
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    BitCrusher bitCrusher_;        // Existing primitive
    SampleRateReducer srReducer_;  // Existing primitive
    OnePoleSmoother gainSmoother_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/bitcrusher_processor.h`

---

## Phase 4: Layer 3 - Distortion Systems

Complete distortion systems that compose Layer 2 processors.

### 4.1 Amp Channel System (`systems/amp_channel.h`)

Complete guitar amp channel with multiple gain stages.

```cpp
class AmpChannel {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    // Gain staging
    void setInputGain(float dB);
    void setPreampGain(float dB);    // First stage drive
    void setPowerampGain(float dB);  // Second stage drive
    void setMasterVolume(float dB);
    
    // Character
    void setPreampType(TubeType type);   // 12AX7, 12AT7, etc.
    void setBrightCap(bool enabled);
    
    // Tone stack (pre or post distortion)
    void setToneStackPosition(ToneStackPosition pos);
    void setBass(float value);
    void setMid(float value);
    void setTreble(float value);
    void setPresence(float value);
    
    // Oversampling
    void setOversamplingFactor(int factor);  // 1, 2, or 4
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    TubeStage preampStage1_;
    TubeStage preampStage2_;
    TubeStage powerampStage_;
    MultimodeFilter toneStack_;  // Existing
    Oversampler<4, 1> oversampler_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `systems/amp_channel.h`

### 4.2 Tape Machine System (`systems/tape_machine.h`)

Complete tape machine emulation.

```cpp
class TapeMachine {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    // Tape parameters
    void setTapeSpeed(TapeSpeed speed);  // 7.5, 15, 30 ips
    void setTapeType(TapeType type);     // Different formulations
    void setInputLevel(float dB);
    void setOutputLevel(float dB);
    
    // Saturation
    void setBias(float bias);
    void setSaturation(float amount);
    void setHysteresisModel(HysteresisSolver solver);
    
    // Character
    void setHeadBump(float amount);      // Low frequency bump
    void setHighFreqRolloff(float freq); // Natural HF loss
    void setHiss(float amount);
    void setWowFlutter(float amount);
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    TapeSaturator saturator_;
    Biquad headBumpFilter_;
    Biquad hfRolloffFilter_;
    NoiseGenerator hissGenerator_;  // Existing
    LFO wowLFO_;                    // Existing
    LFO flutterLFO_;                // Existing
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `systems/tape_machine.h`

### 4.3 Fuzz Pedal System (`systems/fuzz_pedal.h`)

Complete fuzz pedal with input/output stages.

```cpp
class FuzzPedal {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    void setFuzzType(FuzzType type);
    void setFuzz(float amount);
    void setVolume(float dB);
    void setTone(float tone);
    void setBias(float bias);
    
    // Extras
    void setInputBuffer(bool enabled);   // Buffer before fuzz
    void setGateThreshold(float dB);     // Noise gate
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    FuzzProcessor fuzz_;
    Biquad inputBuffer_;
    // Simple gate
    float gateThreshold_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `systems/fuzz_pedal.h`

### 4.4 Distortion Rack System (`systems/distortion_rack.h`)

Chainable distortion system with configurable routing.

```cpp
class DistortionRack {
public:
    static constexpr int kMaxSlots = 4;
    
    enum class SlotType {
        None, Waveshaper, TubeStage, DiodeClipper, 
        Wavefolder, TapeSaturator, Fuzz, Bitcrusher
    };
    
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();
    
    // Slot configuration
    void setSlotType(int slot, SlotType type);
    void setSlotEnabled(int slot, bool enabled);
    void setSlotMix(int slot, float mix);
    
    // Access slot processors for parameter setting
    template<typename T>
    T* getSlotProcessor(int slot);
    
    // Global oversampling (applied once around entire chain)
    void setOversamplingFactor(int factor);
    
    // Global output
    void setOutputGain(float dB);
    
    void process(float* buffer, size_t n) noexcept;
    
private:
    std::array<SlotType, kMaxSlots> slotTypes_;
    std::array<bool, kMaxSlots> slotEnabled_;
    std::array<float, kMaxSlots> slotMix_;
    
    // Processor instances (use variant or type-erased wrapper)
    // ...
    
    Oversampler<4, 1> oversampler_;
};
```

**Status:** ✅ IMPLEMENTED - `systems/distortion_rack.h`

---

## Phase 5: Novel Distortion - Spectral & Chaos (Sound Design)

Creative distortion techniques targeting sound design and special effects.

### 5.1 Spectral Distortion Processor (`processors/spectral_distortion.h`)

Apply distortion algorithms per-frequency-bin in the spectral domain.

```cpp
enum class SpectralDistortionMode : uint8_t {
    PerBinSaturate,     // Apply saturation to each bin's magnitude
    MagnitudeOnly,      // Saturate magnitudes, preserve phase exactly
    BinSelective,       // Different distortion per frequency range
    SpectralBitcrush    // Quantize magnitudes per bin
};

class SpectralDistortion {
public:
    void prepare(double sampleRate, size_t fftSize = 2048);
    void reset();

    void setMode(SpectralDistortionMode mode);
    void setDrive(float drive);                    // Global drive
    void setSaturationCurve(WaveshapeType curve);  // Which waveshaper

    // Bin-selective mode
    void setLowBand(float freqHz, float drive);    // Below this freq
    void setMidBand(float lowHz, float highHz, float drive);
    void setHighBand(float freqHz, float drive);   // Above this freq

    // Spectral bitcrush
    void setMagnitudeBits(float bits);             // Quantize magnitudes

    void processBlock(const float* input, float* output, size_t n) noexcept;

    [[nodiscard]] size_t latency() const noexcept; // FFT size

private:
    STFT stft_;
    OverlapAdd overlapAdd_;
    SpectralBuffer spectrumA_;
    Waveshaper waveshaper_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/spectral_distortion.h`
**Reuses:** STFT, OverlapAdd, SpectralBuffer (Layer 1), Waveshaper
**Novel:** Impossible distortion characteristics - saturate magnitudes while preserving phase perfectly

### 5.2 Chaos Attractor Waveshaper (`primitives/chaos_waveshaper.h`)

Use chaotic mathematical systems for organic, unpredictable transfer functions.

```cpp
enum class ChaosModel : uint8_t {
    Lorenz,     // Classic Lorenz attractor - swirling, unpredictable
    Rossler,    // Smoother chaos, less harsh
    Chua,       // Electronic chaos circuit - analog character
    Henon       // 2D map - discrete, sharp transitions
};

class ChaosWaveshaper {
public:
    void prepare(double sampleRate);
    void reset();

    void setModel(ChaosModel model);
    void setChaosAmount(float amount);      // Blend with input (0=bypass, 1=full chaos)
    void setAttractorSpeed(float speed);    // How fast attractor evolves
    void setInputCoupling(float coupling);  // How much input affects attractor state

    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

private:
    ChaosModel model_;
    // Lorenz state: x, y, z
    float lx_, ly_, lz_;
    // Attractor parameters (sigma, rho, beta for Lorenz)
    float sigma_, rho_, beta_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `primitives/chaos_waveshaper.h`
**Novel:** Living, breathing distortion that evolves over time. Input drives attractor state for signal-reactive chaos.

### 5.3 Formant Distortion Processor (`processors/formant_distortion.h`)

Distort through vocal-tract-like resonances (leverages FormantFilter from filter roadmap).

```cpp
class FormantDistortion {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setVowel(FormantVowel vowel);         // A, E, I, O, U
    void setVowelBlend(float blend);           // Morph between vowels
    void setFormantShift(float semitones);     // Shift formants up/down
    void setDistortionType(WaveshapeType type);
    void setDrive(float drive);
    void setEnvelopeFollow(float amount);      // Modulate formants by input level

    void process(float* buffer, size_t n) noexcept;

private:
    FormantFilter formantPre_;   // Pre-distortion formant shaping
    Waveshaper waveshaper_;
    FormantFilter formantPost_;  // Post-distortion (optional)
    OnePoleSmoother envFollower_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/formant_distortion.h`
**Reuses:** FormantFilter (Phase 11.1), Waveshaper, OnePoleSmoother
**Novel:** "Talking distortion" - vowel shapes combined with saturation for alien textures

---

## Phase 6: Novel Distortion - Stochastic & Temporal (Analog Character)

Distortion techniques that add analog-like variation and input-reactive behavior.

### 6.1 Stochastic Waveshaper (`primitives/stochastic_shaper.h`)

Add controlled randomness to the transfer function itself.

```cpp
class StochasticShaper {
public:
    void prepare(double sampleRate);
    void reset();

    void setBaseType(WaveshapeType type);      // Underlying curve
    void setJitterAmount(float amount);        // Random offset per sample
    void setJitterRate(float hz);              // How fast jitter changes
    void setCoefficientNoise(float amount);    // Randomize curve shape
    void setSeed(uint32_t seed);               // For reproducible randomness

    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

private:
    Waveshaper baseShaper_;
    Xorshift32 rng_;
    OnePoleSmoother jitterSmoother_;
    float jitterAmount_;
    float coeffNoise_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `primitives/stochastic_shaper.h`
**Reuses:** Waveshaper, Xorshift32, OnePoleSmoother, **Oversampler<2, 1>** (optional, for anti-aliased waveshaping - see ChaosWaveshaper pattern)
**Novel:** Simulates analog component tolerance variation - each sample gets slightly different curve

### 6.2 Temporal Distortion Processor (`processors/temporal_distortion.h`)

Transfer function changes based on signal history (memory-based distortion).

```cpp
enum class TemporalMode : uint8_t {
    EnvelopeFollow,     // Louder = more distortion
    InverseEnvelope,    // Quiet = more distortion (expansion)
    Derivative,         // Transients get different curve
    Hysteresis          // Deep memory of recent samples
};

class TemporalDistortion {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setMode(TemporalMode mode);
    void setBaseDrive(float drive);            // Drive at reference level
    void setDriveModulation(float amount);     // How much envelope affects drive
    void setAttackTime(float ms);              // Envelope follower attack
    void setReleaseTime(float ms);             // Envelope follower release
    void setSaturationCurve(WaveshapeType type);

    // For Hysteresis mode
    void setHysteresisDepth(float depth);      // How much history matters
    void setHysteresisDecay(float ms);         // How fast memory fades

    void process(float* buffer, size_t n) noexcept;

private:
    TemporalMode mode_;
    Waveshaper waveshaper_;
    OnePoleSmoother envFollower_;
    OnePoleSmoother derivativeFilter_;
    DCBlocker dcBlocker_;
    // Hysteresis state
    float memoryState_;
    float hysteresisDecay_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/temporal_distortion.h`
**Reuses:** Waveshaper, **EnvelopeFollower** (for EnvelopeFollow/InverseEnvelope/Peak modes), OnePoleSmoother, OnePoleLP/OnePoleHP (for derivative filter), DCBlocker
**Novel:** Compressive distortion (loud=more), expansion distortion (quiet=more), transient-reactive curves

---

## Phase 7: Novel Distortion - Hybrid & Network (Complex Routing)

Distortion through complex signal routing and self-interaction.

### 7.1 Ring-Saturation Hybrid (`primitives/ring_saturation.h`)

Self-modulation: multiply signal with saturated version of itself.

```cpp
class RingSaturation {
public:
    void prepare(double sampleRate);
    void reset();

    void setSaturationCurve(WaveshapeType type);
    void setDrive(float drive);          // Drive into saturation
    void setModulationDepth(float depth); // Blend of ring mod (0=none, 1=full)
    void setStages(int stages);           // Stack multiple self-mods (1-4)

    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

private:
    Waveshaper saturator_;
    int stages_;
    float modDepth_;
    DCBlocker dcBlocker_;
};
```

**Status:** ✅ IMPLEMENTED - `primitives/ring_saturation.h`
**Formula:** `output = input * saturate(input * drive) * depth + input * (1-depth)`
**Novel:** Creates inharmonic sidebands like ring mod, but signal-coherent. Metallic, bell-like character.

### 7.2 Allpass-Saturator Network (`processors/allpass_saturator.h`)

Place saturation inside allpass filter feedback loops for resonant distortion.

```cpp
enum class NetworkTopology : uint8_t {
    SingleAllpass,      // One allpass with saturation in feedback
    AllpassChain,       // Series of allpass filters with saturation
    KarplusStrong,      // Delay + saturator + feedback (plucked string)
    FeedbackMatrix      // 4x4 matrix of cross-fed saturators
};

class AllpassSaturator {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setTopology(NetworkTopology topology);
    void setFrequency(float hz);           // Resonant frequency / pitch
    void setFeedback(float feedback);      // 0-1 (>0.9 can self-oscillate)
    void setSaturationCurve(WaveshapeType type);
    void setDrive(float drive);            // Saturation intensity
    void setDecay(float seconds);          // For Karplus-Strong

    void process(float* buffer, size_t n) noexcept;

private:
    NetworkTopology topology_;
    std::array<Biquad, 4> allpassFilters_;  // For chain
    DelayLine delay_;                        // For Karplus-Strong
    Waveshaper saturator_;
    DCBlocker dcBlocker_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/allpass_saturator.h`
**Reuses:** Biquad (allpass mode), DelayLine, Waveshaper, DCBlocker, **OnePoleAllpass** (for dispersion), **KarplusStrong** (architectural reference for NetworkTopology::KarplusStrong), **DiffusionNetwork** (architectural reference for allpass chains)
**Novel:** Pitched/resonant distortion that can self-oscillate. Input "excites" resonance.

### 7.3 Feedback Distortion Processor (`processors/feedback_distortion.h`)

Controlled feedback runaway with limiting.

```cpp
class FeedbackDistortion {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setDelayTime(float ms);           // Feedback delay (1-100ms)
    void setFeedback(float amount);        // 0-1.5 (>1 = runaway with limiter)
    void setSaturationCurve(WaveshapeType type);
    void setDrive(float drive);
    void setLimiterThreshold(float dB);    // Catches runaway
    void setToneFrequency(float hz);       // Filter in feedback path

    void process(float* buffer, size_t n) noexcept;

private:
    DelayLine delay_;
    Waveshaper saturator_;
    Biquad toneFilter_;
    float limiterThreshold_;
    DCBlocker dcBlocker_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/feedback_distortion.h`
**Reuses:** DelayLine, Waveshaper, Biquad, DCBlocker
**Novel:** Creates sustained, singing distortion. Near-oscillation with limiter for controlled chaos.

---

## Phase 8: Novel Distortion - Digital Destruction (Lo-Fi & Granular)

Intentional digital artifacts and granular processing for creative destruction.

### 8.1 Bitwise Mangler (`primitives/bitwise_mangler.h`)

Operations on the bit representation of samples (beyond bitcrushing).

```cpp
enum class BitwiseOperation : uint8_t {
    XorPattern,     // XOR with repeating pattern
    XorPrevious,    // XOR with previous sample
    BitRotate,      // Rotate bits left/right
    BitShuffle,     // Reorder bits within sample
    BitAverage,     // AND/OR with adjacent samples
    OverflowWrap    // Let values wrap instead of clip
};

class BitwiseMangler {
public:
    void prepare(double sampleRate);
    void reset();

    void setOperation(BitwiseOperation op);
    void setIntensity(float intensity);    // Blend with original
    void setPattern(uint32_t pattern);     // For XorPattern
    void setRotateAmount(int bits);        // For BitRotate (-16 to +16)
    void setSeed(uint32_t seed);           // For random operations

    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

private:
    BitwiseOperation operation_;
    uint32_t pattern_;
    int rotateAmount_;
    Xorshift32 rng_;
    float prevSample_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `primitives/bitwise_mangler.h`
**Reuses:** Xorshift32
**Novel:** Wild tonal shifts from bit manipulation. XOR creates harmonically complex results.

### 8.2 Aliasing Effect Processor (`processors/aliasing_effect.h`)

Intentional aliasing as a creative effect (anti-anti-aliasing).

```cpp
class AliasingEffect {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setDownsampleFactor(float factor);  // 2-32 (no AA filter)
    void setFrequencyShift(float hz);        // Shift before downsample
    void setAliasingBand(float lowHz, float highHz);  // Only alias this band
    void setMix(float mix);

    void process(float* buffer, size_t n) noexcept;

private:
    float downsampleFactor_;
    float phaseIncrement_;       // For frequency shifter
    float phase_;
    Biquad bandFilter_;          // Isolate aliasing band
    SampleRateReducer reducer_;  // Without AA
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/aliasing_effect.h`
**Reuses:** SampleRateReducer (with AA disabled), Biquad, **FrequencyShifter** (for setFrequencyShift feature - SSB modulation via Hilbert transform)
**Novel:** Digital grunge aesthetic - fold high frequencies back intentionally

### 8.3 Granular Distortion Processor (`processors/granular_distortion.h`)

Distort audio in time-windowed micro-grains.

```cpp
class GranularDistortion {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setGrainSize(float ms);             // 5-100ms
    void setGrainDensity(float density);     // Overlapping grains (1-8)
    void setDistortionType(WaveshapeType type);
    void setDriveVariation(float amount);    // Random drive per grain
    void setAlgorithmVariation(bool enabled); // Random algorithm per grain
    void setPositionJitter(float ms);        // Random grain start offset

    void process(float* buffer, size_t n) noexcept;

private:
    // Simple grain engine
    std::array<float, 8192> grainBuffer_;    // Circular buffer
    size_t writePos_;
    float grainSizeMs_;
    int grainDensity_;

    // Per-grain state
    struct Grain {
        size_t startPos;
        size_t length;
        float drive;
        WaveshapeType algorithm;
        float windowPos;
    };
    std::array<Grain, 8> activeGrains_;

    Waveshaper waveshaper_;
    Xorshift32 rng_;
    // ...
};
```

**Status:** ✅ IMPLEMENTED - `processors/granular_distortion.h`
**Reuses:** **GrainPool** (64-grain management with voice stealing), **GrainProcessor** (grain reading/envelope), **GrainScheduler** (trigger scheduling with density/jitter), **GrainEnvelope** (window functions), Waveshaper, Xorshift32
**Novel:** Each micro-grain gets different distortion. Creates evolving, textured destruction.

### 8.4 Fractal/Recursive Distortion (`processors/fractal_distortion.h`)

Apply distortion at multiple frequency scales with self-similar processing, creating harmonic structure that reveals new detail at every "zoom level."

```cpp
enum class FractalMode : uint8_t {
    Residual,       // Classic: distort progressively smaller residuals
    Multiband,      // Split into octave bands, recurse each with depth scaling
    Harmonic,       // Separate odd/even harmonics, different curves per level
    Cascade,        // Different waveshaper at each iteration level
    Feedback        // Cross-feed between iteration levels (chaotic)
};

class FractalDistortion {
public:
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    void setMode(FractalMode mode);
    void setIterations(int iterations);           // 1-8 recursive levels
    void setScaleFactor(float scale);             // Amplitude reduction per level (0.3-0.9)
    void setFrequencyDecay(float decay);          // High-freq emphasis at deeper levels (0-1)
    void setDrive(float drive);                   // Base drive level
    void setMix(float mix);                       // Dry/wet

    // Per-level waveshaper (Cascade mode)
    void setLevelWaveshaper(int level, WaveshapeType type);

    // Multiband mode
    void setCrossoverFrequency(float hz);         // Base octave split frequency
    void setBandIterationScale(float scale);      // Fewer iterations at lower bands

    // Feedback mode
    void setFeedbackAmount(float amount);         // Cross-level feedback (0-0.5)
    void setFeedbackDelay(int samples);           // Delay between feedback taps

    void process(float* buffer, size_t n) noexcept;

private:
    FractalMode mode_;
    int iterations_;
    float scaleFactor_;
    float freqDecay_;
    std::array<Waveshaper, 8> levelWaveshapers_;
    std::array<Biquad, 4> bandSplitters_;         // For multiband
    DCBlocker dcBlocker_;
    // ...
};
```

**Algorithms by Mode:**

**Residual (original):**
```
level[0] = saturate(input * drive)
level[n] = saturate((input - sum(level[0..n-1])) * scale^n)
output = sum(level[0..N])
```

**Multiband (true frequency-domain fractal):**
```
Split input into octave bands (e.g., 4 bands: <250Hz, 250-1k, 1k-4k, >4k)
For each band:
  iterations = baseIterations * (bandIndex / numBands)  // More iterations for higher bands
  Apply Residual algorithm with scaled iterations
Recombine bands
```
*Sonic result:* High frequencies get progressively more "detailed" distortion, like zooming into a fractal reveals more structure.

**Harmonic (odd/even separation):**
```
For each iteration:
  oddHarmonics = chebyshevOddExtract(input)
  evenHarmonics = chebyshevEvenExtract(input)
  level[n] = saturate(oddHarmonics, curveA) + saturate(evenHarmonics, curveB)
  input = input - level[n] * scale
```
*Sonic result:* Creates complex intermodulation between harmonic series.

**Cascade (different waveshaper per level):**
```
level[0] = waveshaper[0].process(input * drive)
level[n] = waveshaper[n].process((input - sum(level[0..n-1])) * scale^n)
```
*Sonic result:* Each iteration has unique tonal character (e.g., soft->hard->fold->clip).

**Feedback (chaotic):**
```
level[0] = saturate(input + feedbackBuffer * feedbackAmount)
level[n] = saturate((input - sum(level[0..n-1]) + level[n-1] * feedback) * scale^n)
feedbackBuffer = delay(level[N], feedbackDelay)
```
*Sonic result:* Self-modulating chaos with controllable instability.

**Frequency Decay Parameter:**
When `freqDecay > 0`, higher iterations are highpass-filtered, emphasizing that "detail emerges at smaller scales":
```
level[n] = highpass(saturate(...), baseFreq * (n+1))
```

**Status:** ✅ IMPLEMENTED - `processors/fractal_distortion.h`
**Reuses:** Waveshaper, Biquad (for band splitting and highpass), DCBlocker, Chebyshev (for Harmonic mode)
**Novel:** True frequency-domain self-similarity. Multiband mode creates "zoom into detail" effect. Feedback mode adds controlled chaos. Cascade mode allows timbral evolution across iterations.

**Sonic Applications:**
- **Multiband:** Add "grit" to high frequencies while keeping lows clean, with natural-sounding harmonic stacking
- **Cascade:** Design specific harmonic evolution (warm->bright->harsh)
- **Feedback:** Self-oscillating textures, drones, glitch effects
- **Harmonic:** Complex intermodulation for bell-like or metallic tones

---

## Implementation Order

### Priority 1: Foundation (Layer 0)
1. `core/sigmoid.h` - All sigmoid functions
2. `core/asymmetric.h` - Asymmetry utilities
3. `core/chebyshev.h` - Chebyshev polynomials
4. `core/wavefold_math.h` - Wavefolding math

### Priority 2: Core Primitives (Layer 1)
5. `primitives/dc_blocker.h` - Lightweight DC blocker
6. `primitives/waveshaper.h` - Unified waveshaper
7. `primitives/hard_clip_adaa.h` - ADAA hard clipping
8. `primitives/tanh_adaa.h` - ADAA tanh
9. `primitives/wavefolder.h` - Basic wavefolder
10. `primitives/chebyshev_shaper.h` - Chebyshev waveshaper

### Priority 3: Processors (Layer 2)
11. `processors/tube_stage.h` - Tube gain stage
12. `processors/diode_clipper.h` - Diode clipping
13. `processors/wavefolder_processor.h` - Full wavefolder
14. `processors/tape_saturator.h` - Tape with hysteresis
15. `processors/fuzz_processor.h` - Fuzz distortion
16. `processors/bitcrusher_processor.h` - Compose existing primitives

### Priority 4: Systems (Layer 3)
17. `systems/amp_channel.h` - Amp modeling
18. `systems/tape_machine.h` - Tape machine
19. `systems/fuzz_pedal.h` - Fuzz pedal
20. `systems/distortion_rack.h` - Chainable rack

### Priority 5: Spectral & Chaos (Layer 1-2) - Sound Design
21. `primitives/chaos_waveshaper.h` - Lorenz/Rössler attractors
22. `processors/spectral_distortion.h` - Per-bin FFT distortion
23. `processors/formant_distortion.h` - Vocal tract + saturation

### Priority 6: Stochastic & Temporal (Layer 1-2) - Analog Character
24. `primitives/stochastic_shaper.h` - Randomized transfer function
25. `processors/temporal_distortion.h` - Envelope-reactive distortion

### Priority 7: Hybrid & Network (Layer 1-2) - Complex Routing
26. `primitives/ring_saturation.h` - Self-modulation distortion
27. `processors/allpass_saturator.h` - Resonant distortion networks
28. `processors/feedback_distortion.h` - Controlled feedback runaway

### Priority 8: Digital Destruction (Layer 1-2) - Lo-Fi & Granular
29. `primitives/bitwise_mangler.h` - XOR, bit rotation, shuffle
30. `processors/aliasing_effect.h` - Intentional aliasing
31. `processors/granular_distortion.h` - Per-grain distortion
32. `processors/fractal_distortion.h` - Recursive multi-scale

---

## Deferred: Neural Network Amp Modeling

Neural network-based amp modeling requires significant research:
- Training infrastructure
- Model format (LSTM, GRU, WaveNet)
- Real-time inference engine
- Model management

**Recommendation:** Separate research project after core DSP is complete.

---

## Testing Strategy

Each component requires:

1. **Unit tests** - Pure function behavior, edge cases
2. **Harmonic analysis** - Verify expected harmonic content (FFT)
3. **Aliasing tests** - Compare oversampled vs non-oversampled output
4. **DC offset tests** - Verify DC blocker effectiveness
5. **Performance benchmarks** - CPU usage per sample

Use existing test infrastructure in `dsp/tests/`.

---

## Existing Code Audit

### Components to Keep As-Is
- `BitCrusher` - Complete, well-tested
- `SampleRateReducer` - Complete, well-tested
- `Oversampler` - Comprehensive, reuse for all anti-aliasing
- `FastMath::fastTanh` - Optimized, use in Sigmoid
- `Biquad` - Use for tone shaping, pre/de-emphasis
- `OnePoleSmoother` - Use for parameter smoothing

### Components to Extract/Refactor
- `SaturationProcessor` saturation functions → `core/sigmoid.h`, `core/asymmetric.h`
- `dsp_utils.h` `softClip`/`hardClip` → keep but also add to `core/sigmoid.h`

### Components That Need Extension
- `NoiseGenerator` - Could add tape hiss modulation for TapeMachine

---

## File Checklist

### Implemented Files

**Layer 0 (core/)**
- [x] `sigmoid.h`
- [ ] `asymmetric.h` - Functions embedded in processors, standalone file not created
- [x] `chebyshev.h`
- [x] `wavefold_math.h`

**Layer 1 (primitives/)**
- [x] `dc_blocker.h`
- [x] `waveshaper.h`
- [x] `hard_clip_adaa.h`
- [x] `tanh_adaa.h`
- [x] `wavefolder.h`
- [x] `chebyshev_shaper.h`

**Layer 2 (processors/)**
- [x] `tube_stage.h`
- [x] `diode_clipper.h`
- [x] `wavefolder_processor.h`
- [x] `tape_saturator.h`
- [x] `fuzz_processor.h`
- [x] `bitcrusher_processor.h`

**Layer 3 (systems/)**
- [x] `amp_channel.h`
- [x] `tape_machine.h`
- [x] `fuzz_pedal.h`
- [x] `distortion_rack.h`

**Novel Distortion - Layer 1 (primitives/)**
- [x] `chaos_waveshaper.h` - Lorenz/Rössler/Chua attractor transfer functions
- [x] `stochastic_shaper.h` - Randomized transfer function (analog tolerance sim)
- [x] `ring_saturation.h` - Self-modulation (signal × saturated signal)
- [x] `bitwise_mangler.h` - XOR, bit rotation, shuffle, overflow wrap

**Novel Distortion - Layer 2 (processors/)**
- [x] `spectral_distortion.h` - Per-bin FFT domain saturation
- [x] `formant_distortion.h` - Vowel shaping + saturation
- [x] `temporal_distortion.h` - Envelope-following drive modulation
- [x] `allpass_saturator.h` - Resonant distortion networks
- [x] `feedback_distortion.h` - Controlled feedback with limiting
- [x] `aliasing_effect.h` - Intentional aliasing (anti-AA)
- [x] `granular_distortion.h` - Per-grain variable distortion
- [x] `fractal_distortion.h` - Recursive multi-scale distortion

**Tests**
- [x] `tests/core/sigmoid_test.cpp`
- [x] `tests/core/chebyshev_test.cpp`
- [x] `tests/primitives/dc_blocker_test.cpp`
- [x] `tests/primitives/waveshaper_test.cpp`
- [x] `tests/primitives/hard_clip_adaa_test.cpp`
- [x] `tests/primitives/tanh_adaa_test.cpp`
- [x] `tests/primitives/wavefolder_test.cpp`
- [x] `tests/processors/tube_stage_test.cpp`
- [x] `tests/processors/diode_clipper_test.cpp`
- [x] `tests/processors/tape_saturator_test.cpp`
- [x] `tests/processors/fuzz_processor_test.cpp`
- [x] `tests/systems/amp_channel_test.cpp`
- [x] `tests/systems/tape_machine_test.cpp`
- [x] `tests/systems/fuzz_pedal_test.cpp`
- [x] `tests/systems/distortion_rack_tests.cpp`

**Novel Distortion Tests**
- [x] `tests/primitives/chaos_waveshaper_test.cpp`
- [x] `tests/primitives/stochastic_shaper_test.cpp`
- [x] `tests/primitives/ring_saturation_test.cpp`
- [x] `tests/primitives/bitwise_mangler_test.cpp`
- [x] `tests/processors/spectral_distortion_test.cpp`
- [x] `tests/processors/formant_distortion_test.cpp`
- [x] `tests/processors/temporal_distortion_test.cpp`
- [x] `tests/processors/allpass_saturator_tests.cpp`
- [x] `tests/processors/feedback_distortion_test.cpp`
- [x] `tests/processors/aliasing_effect_test.cpp`
- [x] `tests/processors/granular_distortion_test.cpp`
- [x] `tests/processors/fractal_distortion_test.cpp`

---

*Last updated: January 2026 (Implementation status verified against codebase)*
