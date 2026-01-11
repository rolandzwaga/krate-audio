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

**Status:** Partially exists (`fastTanh`, `softClip` in dsp_utils)
**Work:** Create unified `sigmoid.h` with all variants

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Partially exists (scattered in `SaturationProcessor`)
**Work:** Create unified primitive with all curve types

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

**Status:** Uses `Biquad` in `SaturationProcessor` - works but heavier than needed
**Work:** Create lightweight dedicated primitive

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

**Status:** Does not exist
**Work:** New file - implement 1st and 2nd order ADAA

### 2.4 Tanh with ADAA (`primitives/tanh_adaa.h`)

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Does not exist (tube algorithm exists in `SaturationProcessor`)
**Work:** New file - extract and expand tube modeling

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

**Status:** Diode algorithm exists in `SaturationProcessor`
**Work:** New file - expand with topology options

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Simple tape exists in `SaturationProcessor`, hysteresis does not
**Work:** New file with Jiles-Atherton hysteresis option

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Primitives exist, processor wrapper does not
**Work:** New file - compose existing primitives

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Partially exists in `CharacterProcessor` (simplified)
**Work:** New file - comprehensive tape emulation

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

**Status:** Does not exist
**Work:** New file

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

**Status:** Does not exist
**Work:** New file

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

### New Files to Create

**Layer 0 (core/)**
- [ ] `sigmoid.h`
- [ ] `asymmetric.h`
- [ ] `chebyshev.h`
- [ ] `wavefold_math.h`

**Layer 1 (primitives/)**
- [ ] `dc_blocker.h`
- [ ] `waveshaper.h`
- [ ] `hard_clip_adaa.h`
- [ ] `tanh_adaa.h`
- [ ] `wavefolder.h`
- [ ] `chebyshev_shaper.h`

**Layer 2 (processors/)**
- [ ] `tube_stage.h`
- [ ] `diode_clipper.h`
- [ ] `wavefolder_processor.h`
- [ ] `tape_saturator.h`
- [ ] `fuzz_processor.h`
- [ ] `bitcrusher_processor.h`

**Layer 3 (systems/)**
- [ ] `amp_channel.h`
- [ ] `tape_machine.h`
- [ ] `fuzz_pedal.h`
- [ ] `distortion_rack.h`

**Tests**
- [ ] `tests/core/sigmoid_test.cpp`
- [ ] `tests/core/chebyshev_test.cpp`
- [ ] `tests/primitives/dc_blocker_test.cpp`
- [ ] `tests/primitives/waveshaper_test.cpp`
- [ ] `tests/primitives/adaa_test.cpp`
- [ ] `tests/primitives/wavefolder_test.cpp`
- [ ] `tests/processors/tube_stage_test.cpp`
- [ ] `tests/processors/diode_clipper_test.cpp`
- [ ] `tests/processors/tape_saturator_test.cpp`
- [ ] `tests/processors/fuzz_processor_test.cpp`
- [ ] `tests/systems/amp_channel_test.cpp`
- [ ] `tests/systems/tape_machine_test.cpp`

---

*Last updated: January 2026*
