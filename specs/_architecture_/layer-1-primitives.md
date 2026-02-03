# Layer 1: DSP Primitives

[← Back to Architecture Index](README.md)

**Location**: `dsp/include/krate/dsp/primitives/` | **Dependencies**: Layer 0 only

---

## DelayLine
**Path:** [delay_line.h](../../dsp/include/krate/dsp/primitives/delay_line.h) | **Since:** 0.0.2

Circular buffer delay with multiple interpolation modes.

```cpp
enum class InterpolationType : uint8_t { None, Linear, Allpass, Cubic, Lagrange, Thiran };

class DelayLine {
    void prepare(size_t maxSamples) noexcept;
    void reset() noexcept;
    void push(float sample) noexcept;
    [[nodiscard]] float read(float delaySamples) const noexcept;
    void setInterpolation(InterpolationType type) noexcept;
    [[nodiscard]] size_t getMaxDelay() const noexcept;
};
```

| Interpolation | Use Case |
|---------------|----------|
| None | Integer delays only |
| Linear | Modulated delay (chorus) |
| Allpass | Fixed delay in feedback (dispersion-free) |
| Cubic/Lagrange | Pitch shifting, high-quality |
| Thiran | Fractional fixed delays |

---

## CrossfadingDelayLine
**Path:** [crossfading_delay_line.h](../../dsp/include/krate/dsp/primitives/crossfading_delay_line.h) | **Since:** 0.0.39

Click-free delay time changes using dual delay lines with crossfade.

```cpp
class CrossfadingDelayLine {
    void prepare(size_t maxSamples, float sampleRate, float crossfadeDurationMs = 50.0f) noexcept;
    void reset() noexcept;
    void push(float sample) noexcept;
    [[nodiscard]] float read(float delaySamples) noexcept;
    void setInterpolation(InterpolationType type) noexcept;
};
```

---

## LFO (Low-Frequency Oscillator)
**Path:** [lfo.h](../../dsp/include/krate/dsp/primitives/lfo.h) | **Since:** 0.0.3

```cpp
enum class Waveform : uint8_t { Sine, Triangle, Saw, Square, SampleAndHold, Random };
enum class TempoSyncMode : uint8_t { Free, Synced };

class LFO {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process() noexcept;                    // [-1, 1] bipolar
    [[nodiscard]] float processUnipolar() noexcept;            // [0, 1]
    void setWaveform(Waveform waveform) noexcept;
    void setFrequency(float hz) noexcept;                      // 0.001-100 Hz
    void setPhase(float normalizedPhase) noexcept;             // [0, 1]
    void syncToTempo(double bpm, NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept;
};
```

---

## OnePoleSmoother
**Path:** [smoother.h](../../dsp/include/krate/dsp/primitives/smoother.h) | **Since:** 0.0.4

Exponential parameter smoothing (RC filter).

```cpp
class OnePoleSmoother {
    void prepare(double sampleRate) noexcept;
    void setTime(float timeMs) noexcept;
    [[nodiscard]] float process(float target) noexcept;
    void snapTo(float value) noexcept;
    [[nodiscard]] bool isSmoothing() const noexcept;
};
```

---

## Biquad Filter
**Path:** [biquad.h](../../dsp/include/krate/dsp/primitives/biquad.h) | **Since:** 0.0.5

TDF2 biquad with coefficient smoothing and cascaded variants.

```cpp
enum class FilterType : uint8_t { Lowpass, Highpass, Bandpass, Notch, Allpass, LowShelf, HighShelf, Peak };

class Biquad {
    void configure(FilterType type, float freq, float Q, float gainDb, float sampleRate) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};

class Biquad24dB { /* 2-stage cascade (24dB/oct) */ };
class Biquad48dB { /* 4-stage cascade (48dB/oct) */ };
class SmoothedBiquad { /* Click-free coefficient changes */ };
```

---

## Oversampler
**Path:** [oversampler.h](../../dsp/include/krate/dsp/primitives/oversampler.h) | **Since:** 0.0.6

Anti-aliased processing wrapper.

```cpp
enum class OversamplingFactor : uint8_t { TwoX = 2, FourX = 4 };
enum class OversamplingQuality : uint8_t { Economy, Standard, High };
enum class OversamplingMode : uint8_t { ZeroLatency, LinearPhase };

template<size_t Factor = 2, size_t NumChannels = 2>
class Oversampler {
    void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality, OversamplingMode mode) noexcept;
    void process(float* left, float* right, size_t numSamples, const StereoCallback& callback) noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;
};

using Oversampler2x = Oversampler<2, 2>;
using Oversampler4x = Oversampler<4, 2>;
```

| Quality | Stopband | Latency | Use Case |
|---------|----------|---------|----------|
| Economy | ~48dB | 0 | Live, guitar amps |
| Standard | ~80dB | ~15 samp | General mixing |
| High | ~100dB | ~31 samp | Mastering |

---

## FFT
**Path:** [fft.h](../../dsp/include/krate/dsp/primitives/fft.h) | **Since:** 0.0.7

Radix-2 DIT FFT for real signals.

```cpp
struct Complex { float real, imag; /* arithmetic + polar methods */ };

class FFT {
    void prepare(size_t fftSize) noexcept;  // Power of 2, 256-8192
    void forward(const float* input, Complex* output) noexcept;   // N real → N/2+1 complex
    void inverse(const Complex* input, float* output) noexcept;   // N/2+1 complex → N real
    [[nodiscard]] size_t numBins() const noexcept;  // N/2+1
};
```

---

## STFT
**Path:** [stft.h](../../dsp/include/krate/dsp/primitives/stft.h) | **Since:** 0.0.8

Short-Time Fourier Transform with overlap-add.

```cpp
class STFT {
    void prepare(size_t fftSize, size_t hopSize, WindowType windowType) noexcept;
    void process(const float* input, size_t numSamples, STFTCallback callback) noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;  // fftSize samples
};
```

---

## DCBlocker
**Path:** [dc_blocker.h](../../dsp/include/krate/dsp/primitives/dc_blocker.h) | **Since:** 0.10.0

Lightweight first-order highpass filter for DC offset removal.

**Use when:**
- After asymmetric saturation/waveshaping (tube, diode) to remove introduced DC
- In feedback loops to prevent DC accumulation from quantization/round-off
- General signal conditioning before further processing

**Performance:** 3 arithmetic ops per sample (1 mul + 1 sub + 1 add) vs 9 ops for Biquad highpass.

```cpp
class DCBlocker {
    void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept;
    void reset() noexcept;
    void setCutoff(float cutoffHz) noexcept;  // Without state reset
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

| Cutoff | Use Case |
|--------|----------|
| 5 Hz | Feedback loops (minimal coloring) |
| 10 Hz | General DC blocking (default) |
| 20 Hz | Fast DC removal after aggressive processing |

**Note:** Replaces inline `DCBlocker` in `feedback_network.h`. Uses exact formula R = exp(-2π*fc/fs) for accurate cutoff matching.

---

## Waveshaper
**Path:** [waveshaper.h](../../dsp/include/krate/dsp/primitives/waveshaper.h) | **Since:** 0.10.0

Unified waveshaping primitive with selectable transfer function types.

**Use when:**
- Applying saturation/distortion effects with different harmonic characters
- Need a "drive knob" to control saturation intensity
- Want even harmonics (warmth) via asymmetry parameter
- Building saturation processors, tube stages, or fuzz effects

**Note:** Compose with DCBlocker when using non-zero asymmetry to remove DC offset.

```cpp
enum class WaveshapeType : uint8_t {
    Tanh,           // Warm, smooth saturation
    Atan,           // Slightly brighter than tanh
    Cubic,          // 3rd harmonic dominant
    Quintic,        // Smoother knee than cubic
    ReciprocalSqrt, // Fast tanh alternative
    Erf,            // Tape-like with spectral nulls
    HardClip,       // Harsh, all harmonics
    Diode,          // Subtle even harmonics (UNBOUNDED)
    Tube            // Warm even harmonics (bounded)
};

class Waveshaper {
    void setType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;        // Pre-gain (abs value stored)
    void setAsymmetry(float bias) noexcept;     // DC bias [-1, 1] for even harmonics
    [[nodiscard]] WaveshapeType getType() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float getAsymmetry() const noexcept;
    [[nodiscard]] float process(float x) const noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

| Type | Output Bounds | Harmonics | Character |
|------|---------------|-----------|-----------|
| Tanh | [-1, 1] | Odd only | Warm, smooth |
| Atan | [-1, 1] | Odd only | Brighter |
| Cubic | [-1, 1] | Odd (3rd dominant) | Gentle |
| Quintic | [-1, 1] | Odd (smooth) | Very gentle |
| ReciprocalSqrt | [-1, 1] | Odd only | Fast tanh alt |
| Erf | [-1, 1] | Odd + nulls | Tape-like |
| HardClip | [-1, 1] | All | Harsh, digital |
| Diode | Unbounded | Even + Odd | Subtle warmth |
| Tube | [-1, 1] | Even + Odd | Rich warmth |

**Important:** Only Diode is unbounded (can exceed [-1, 1]). All other types, including Tube, are bounded to [-1, 1].

---

## HardClipADAA
**Path:** [hard_clip_adaa.h](../../dsp/include/krate/dsp/primitives/hard_clip_adaa.h) | **Since:** 0.10.0

Anti-aliased hard clipping using Antiderivative Anti-Aliasing (ADAA). Provides 12-30dB aliasing reduction compared to naive hard clip without oversampling CPU cost.

**Use when:**
- Hard clipping is needed with minimal aliasing artifacts
- CPU budget doesn't allow for oversampling
- Building guitar/bass amp simulations or aggressive distortion
- Processing high-frequency content that would cause audible aliasing

**Note:** This is a stateful primitive - requires `reset()` between unrelated audio regions. Compose with DCBlocker if using in feedback loops.

```cpp
enum class Order : uint8_t { First, Second };  // Quality vs CPU tradeoff

class HardClipADAA {
    void setOrder(Order order) noexcept;           // First: ~6-8x, Second: ~12-15x vs naive
    void setThreshold(float threshold) noexcept;   // Clipping level (abs value stored)
    void reset() noexcept;                         // Clear state, preserves config
    [[nodiscard]] float process(float x) noexcept;                       // Single sample
    void processBlock(float* buffer, size_t n) noexcept;                 // Block processing
    [[nodiscard]] static float F1(float x, float threshold) noexcept;    // 1st antiderivative
    [[nodiscard]] static float F2(float x, float threshold) noexcept;    // 2nd antiderivative
};
```

| Order | Aliasing Reduction | CPU Cost vs Naive | Use Case |
|-------|-------------------|-------------------|----------|
| First | ~12-20 dB | ~6-8x | General use, real-time |
| Second | ~18-30 dB | ~12-15x | Quality-critical, may exceed 10x budget |

**Dependencies:** `core/sigmoid.h` (fallback), `core/db_utils.h` (NaN/Inf detection)

---

## TanhADAA
**Path:** [tanh_adaa.h](../../dsp/include/krate/dsp/primitives/tanh_adaa.h) | **Since:** 0.10.0

Anti-aliased tanh saturation using Antiderivative Anti-Aliasing (ADAA). Provides 3-12dB aliasing reduction compared to naive tanh without oversampling CPU cost.

**Use when:**
- Smooth saturation is needed with reduced aliasing artifacts
- Building tube amp simulations or warm distortion effects
- Processing high-frequency content through nonlinear waveshaping
- CPU budget doesn't allow for oversampling

**Note:** This is a stateful primitive - requires `reset()` between unrelated audio regions. Compose with DCBlocker if using asymmetric drive settings.

```cpp
class TanhADAA {
    void setDrive(float drive) noexcept;           // Pre-gain (abs value stored)
    void reset() noexcept;                         // Clear state, preserves config
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float process(float x) noexcept;                   // Single sample
    void processBlock(float* buffer, size_t n) noexcept;             // Block processing
    [[nodiscard]] static float F1(float x) noexcept;                 // 1st antiderivative: ln(cosh(x))
};
```

| Feature | Value |
|---------|-------|
| Aliasing Reduction | ~3-12 dB vs naive tanh |
| CPU Cost vs Naive | ~8-10x |
| Output Range | [-1, 1] (bounded) |
| Antiderivative | F1(x) = ln(cosh(x)) |

**ADAA Formula:**
```
y[n] = (F1(x[n]*drive) - F1(x[n-1]*drive)) / (drive * (x[n] - x[n-1]))
```

**Epsilon Fallback:** When |x[n] - x[n-1]| < 1e-5, uses `fastTanh(midpoint * drive)` to avoid division by near-zero.

**Dependencies:** `core/fast_math.h` (fastTanh, NaN/Inf detection)

---

## Wavefolder
**Path:** [wavefolder.h](../../dsp/include/krate/dsp/primitives/wavefolder.h) | **Since:** 0.10.0

Unified wavefolding primitive with three selectable algorithms for distinct harmonic characters.

**Use when:**
- Creating wavefolding distortion effects (synth-style or guitar)
- Need different harmonic flavors (dense odd harmonics, FM-like, circuit-derived)
- Building Serge-style wavefolder emulations
- Want soft saturation with Lockhart circuit character

**Note:** This is a stateless primitive - `process()` is const, no `reset()` needed. Compose with Oversampler for anti-aliasing and DCBlocker for asymmetric processing.

```cpp
enum class WavefoldType : uint8_t {
    Triangle,   // Dense odd harmonics, smooth rolloff (guitar effects)
    Sine,       // FM-like sparse spectrum (Serge style)
    Lockhart    // Rich even/odd harmonics with spectral nulls (circuit-derived)
};

class Wavefolder {
    void setType(WavefoldType type) noexcept;              // Algorithm selection
    void setFoldAmount(float amount) noexcept;             // Intensity [0.0, 10.0], abs() applied
    [[nodiscard]] WavefoldType getType() const noexcept;
    [[nodiscard]] float getFoldAmount() const noexcept;
    [[nodiscard]] float process(float x) const noexcept;   // Single sample (stateless)
    void processBlock(float* buffer, size_t n) const noexcept;  // Block processing
};
```

| Type | Output Bounds | Harmonics | Character |
|------|---------------|-----------|-----------|
| Triangle | [-1/foldAmount, 1/foldAmount] | Dense odd | Smooth, guitar-like |
| Sine | [-1, 1] | Sparse FM-like | Serge wavefolder |
| Lockhart | tanh bounded | Even + Odd | Soft saturation, nulls |

| foldAmount | Triangle | Sine | Lockhart |
|------------|----------|------|----------|
| 0.0 | Returns 0 | Passthrough | Returns ~0.514 |
| 1.0 | threshold=1.0 | gain=1.0 | moderate saturation |
| 10.0 | threshold=0.1 | gain=10.0 | heavy saturation |

**Performance:** Triangle ~5us, Sine ~5us, Lockhart ~50us for 512 samples.

**Dependencies:** `core/wavefold_math.h` (triangleFold, sineFold, lambertW), `core/fast_math.h` (fastTanh), `core/db_utils.h` (NaN/Inf detection)

---

## ChebyshevShaper
**Path:** [chebyshev_shaper.h](../../dsp/include/krate/dsp/primitives/chebyshev_shaper.h) | **Since:** 0.10.0

Harmonic control primitive using Chebyshev polynomial mixing. Unlike traditional waveshapers that add a fixed harmonic series, ChebyshevShaper allows independent control of each harmonic's level (1st through 8th), enabling precise timbral shaping.

**Use when:**
- Creating custom harmonic spectra (tube amp character, exciter effects)
- Need precise control over individual harmonics vs. generic saturation
- Building harmonic enhancers or exciters with specific harmonic emphasis
- Want phase-accurate harmonic addition (no aliasing from Chebyshev property)

**Note:** This is a stateless primitive - `process()` is const, no `reset()` needed. Compose with Oversampler for anti-aliasing at high drives and DCBlocker if even harmonics create DC offset.

```cpp
class ChebyshevShaper {
    static constexpr int kMaxHarmonics = 8;

    void setHarmonicLevel(int harmonic, float level) noexcept;  // harmonic 1-8, level unbounded
    void setAllHarmonics(const std::array<float, kMaxHarmonics>& levels) noexcept;
    [[nodiscard]] float getHarmonicLevel(int harmonic) const noexcept;
    [[nodiscard]] const std::array<float, kMaxHarmonics>& getHarmonicLevels() const noexcept;
    [[nodiscard]] float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) const noexcept;
};
```

| Harmonic Level | Effect |
|----------------|--------|
| 0.0 | Harmonic disabled |
| 1.0 | Full harmonic level |
| -1.0 | Phase-inverted harmonic |
| > 1.0 | Amplified harmonic |

**Typical Presets:**
```cpp
// Odd harmonics only (guitar distortion character)
shaper.setAllHarmonics({0.5f, 0.0f, 0.3f, 0.0f, 0.2f, 0.0f, 0.1f, 0.0f});

// Even harmonics (tube warmth)
shaper.setAllHarmonics({0.0f, 0.4f, 0.0f, 0.2f, 0.0f, 0.1f, 0.0f, 0.05f});

// Fundamental emphasis with light overtones
shaper.setAllHarmonics({1.0f, 0.1f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
```

**Performance:** ~1.6us for 512 samples (well under Layer 1 budget).

**Dependencies:** `core/chebyshev.h` (Chebyshev::harmonicMix)

---

## One-Pole Audio Filters
**Path:** [one_pole.h](../../dsp/include/krate/dsp/primitives/one_pole.h) | **Since:** 0.12.0

Simple first-order filters for audio signal processing. Three classes for different use cases:

**Note:** These are distinct from `OnePoleSmoother` (which is for parameter smoothing). These filters are optimized for audio signals with proper frequency response and DC blocking.

```cpp
// First-order lowpass (6dB/octave)
class OnePoleLP {
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};

// First-order highpass (6dB/octave)
class OnePoleHP {
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};

// Leaky integrator for envelope detection (sample-rate independent)
class LeakyIntegrator {
    explicit LeakyIntegrator(float leak = 0.999f) noexcept;
    void setLeak(float a) noexcept;      // [0, 1) - typically 0.99-0.9999
    [[nodiscard]] float getLeak() const noexcept;
    [[nodiscard]] float getState() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};
```

| Class | Formula | Use Case |
|-------|---------|----------|
| `OnePoleLP` | y[n] = (1-a)*x[n] + a*y[n-1] | Tone control, feedback damping |
| `OnePoleHP` | y[n] = ((1+a)/2)*(x[n]-x[n-1]) + a*y[n-1] | DC blocking, bass reduction |
| `LeakyIntegrator` | y[n] = x[n] + leak*y[n-1] | Envelope detection, smoothing |

**Comparison with OnePoleSmoother:**

| Feature | OnePoleLP/HP | OnePoleSmoother |
|---------|--------------|-----------------|
| Purpose | Audio filtering | Parameter smoothing |
| Frequency response | Accurate 6dB/oct | Approximation |
| NaN/Inf handling | Returns 0, resets | Undefined |
| DC blocking (HP) | Yes | No |
| Time constant | Frequency-based | Time-based |

**When to use:**
- **OnePoleLP:** Feedback loop damping, simple tone control, high-frequency rolloff
- **OnePoleHP:** DC blocking before effects, subsonic removal, crossover networks
- **LeakyIntegrator:** Envelope followers, level detection, smoothing rectified signals

**Dependencies:** `core/math_constants.h`, `core/db_utils.h` (flushDenormal, isNaN, isInf)

---

## State Variable Filter (SVF)
**Path:** [svf.h](../../dsp/include/krate/dsp/primitives/svf.h) | **Since:** 0.12.0

TPT (Topology-Preserving Transform) State Variable Filter using Cytomic's trapezoidal integration. Provides 8 filter modes with audio-rate modulation stability.

**Use when:**
- Building synth-style filters with LFO/envelope cutoff modulation
- Need simultaneous lowpass, highpass, bandpass, notch outputs (processMulti)
- Parametric EQ with peak/shelf modes
- Modulation stability is critical (biquad clicks on fast sweeps)

**Note:** SVF is preferred over Biquad when cutoff will be modulated at audio rate. For static filtering, Biquad is equally suitable.

```cpp
enum class SVFMode : uint8_t {
    Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf
};

struct SVFOutputs { float low, high, band, notch; };

class SVF {
    static constexpr float kButterworthQ = 0.7071067811865476f;

    void prepare(double sampleRate) noexcept;
    void setMode(SVFMode mode) noexcept;
    void setCutoff(float hz) noexcept;           // [1 Hz, sampleRate * 0.495]
    void setResonance(float q) noexcept;         // [0.1, 30.0]
    void setGain(float dB) noexcept;             // [-24, +24] for peak/shelf
    void reset() noexcept;

    [[nodiscard]] float process(float input) noexcept;       // Single mode output
    void processBlock(float* buffer, size_t n) noexcept;     // In-place block
    [[nodiscard]] SVFOutputs processMulti(float input) noexcept;  // All 4 outputs
};
```

| Mode | Use Case |
|------|----------|
| Lowpass | Synth filter, tone control |
| Highpass | Bass removal, sub filtering |
| Bandpass | Wah, formant emphasis |
| Notch | Frequency cancellation |
| Allpass | Phase manipulation, phaser |
| Peak | Parametric EQ band |
| LowShelf | Bass boost/cut |
| HighShelf | Treble boost/cut |

**Comparison with Biquad:**

| Feature | SVF | Biquad |
|---------|-----|--------|
| Modulation stability | Excellent | Poor (clicks on fast sweeps) |
| Multi-output | Yes (processMulti) | No |
| Parameter orthogonality | Cutoff/Q independent | Interdependent |
| CPU cost | Similar | Similar |
| Mode switching | Instant (m0/m1/m2 mix) | Requires coefficient recalc |

**TPT Topology:**
- Coefficients: `g = tan(π * fc / fs)`, `k = 1/Q`
- Mode mixing: `output = m0*high + m1*band + m2*low`
- State: Two integrators (ic1eq, ic2eq) with trapezoidal update

**Dependencies:** `core/math_constants.h`, `core/db_utils.h` (flushDenormal, isNaN, isInf, constexprPow10)

---

## SampleRateConverter
**Path:** [sample_rate_converter.h](../../dsp/include/krate/dsp/primitives/sample_rate_converter.h) | **Since:** 0.13.0

Variable-rate linear buffer playback with high-quality interpolation.

**Use when:**
- Playing back captured audio slices at different pitches (freeze mode)
- Simple pitch shifting of buffered audio
- Granular effect grain playback
- Building time-stretch effects

```cpp
enum class SRCInterpolationType : uint8_t { Linear, Cubic, Lagrange };

class SampleRateConverter {
    static constexpr float kMinRate = 0.25f;   // 2 octaves down
    static constexpr float kMaxRate = 4.0f;    // 2 octaves up
    static constexpr float kDefaultRate = 1.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setRate(float rate) noexcept;           // Clamped to [0.25, 4.0]
    void setInterpolation(SRCInterpolationType type) noexcept;
    void setPosition(float samples) noexcept;    // Set read position
    [[nodiscard]] float getPosition() const noexcept;
    [[nodiscard]] float process(const float* buffer, size_t bufferSize) noexcept;
    void processBlock(const float* src, size_t srcSize, float* dst, size_t dstSize) noexcept;
    [[nodiscard]] bool isComplete() const noexcept;
};
```

| Interpolation | Quality | Use Case |
|---------------|---------|----------|
| Linear | Low | Modulation, fast pitch sweeps |
| Cubic | Medium | General pitch shifting |
| Lagrange | High | Critical audio, offline rendering |

**Edge Handling:** For 4-point interpolation (Cubic, Lagrange) at buffer boundaries, edge clamping duplicates edge samples to ensure valid 4-sample windows.

**Example Usage:**
```cpp
SampleRateConverter converter;
converter.prepare(44100.0);
converter.setRate(2.0f);  // Octave up
converter.setInterpolation(SRCInterpolationType::Cubic);

// Play back captured slice
float output[512];
converter.processBlock(capturedSlice, sliceSize, output, 512);
```

**Dependencies:** `core/interpolation.h` (linearInterpolate, cubicHermiteInterpolate, lagrangeInterpolate)

---

## OnePoleAllpass (First-Order Allpass Filter)
**Path:** [one_pole_allpass.h](../../dsp/include/krate/dsp/primitives/one_pole_allpass.h) | **Since:** 0.13.0 | **Renamed:** 0.14.0 (was `Allpass1Pole` in `allpass_1pole.h`)

First-order allpass filter for frequency-dependent phase shifting. Provides unity magnitude response with phase shift from 0 degrees (DC) to -180 degrees (Nyquist).

**Use when:**
- Building phaser effects (cascade 2-12 stages with LFO modulation)
- Phase correction in crossover networks
- Any application requiring phase shift without magnitude change

> **Naming Convention:** Follows `one_pole.h` pattern (`OnePoleLP`, `OnePoleHP` → `OnePoleAllpass`)

```cpp
class OnePoleAllpass {
    void prepare(double sampleRate) noexcept;
    void setFrequency(float hz) noexcept;        // Break frequency (clamped to [1 Hz, Nyquist*0.99])
    void setCoefficient(float a) noexcept;       // Direct control (clamped to [-0.9999, +0.9999])
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] float getCoefficient() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;

    [[nodiscard]] static float coeffFromFrequency(float hz, double sampleRate) noexcept;
    [[nodiscard]] static float frequencyFromCoeff(float a, double sampleRate) noexcept;
};

// Backwards compatibility alias
using Allpass1Pole = OnePoleAllpass;
```

| Coefficient | Break Frequency | Phase at Break |
|-------------|-----------------|----------------|
| a -> +1.0 | Near DC | -90 deg near 0 Hz |
| a = 0.0 | fs/4 | -90 deg at quarter sample rate |
| a -> -1.0 | Near Nyquist | -90 deg near fs/2 |

**Difference Equation:** `y[n] = a*x[n] + x[n-1] - a*y[n-1]`

**Coefficient Formula:** `a = (1 - tan(pi*f/fs)) / (1 + tan(pi*f/fs))`

**Comparison with Biquad Allpass:**

| Feature | Allpass1Pole | Biquad (Allpass) |
|---------|--------------|------------------|
| Order | 1st order | 2nd order |
| Phase range | 0 to -180 deg | 0 to -360 deg |
| Memory | 20 bytes | ~24 bytes |
| Use case | Phasers, simple phase correction | Wider phase range, parametric |

**Dependencies:** `core/math_constants.h` (kPi), `core/db_utils.h` (flushDenormal, isNaN, isInf)

---

## Comb Filters (FeedforwardComb, FeedbackComb, SchroederAllpass)
**Path:** [comb_filter.h](../../dsp/include/krate/dsp/primitives/comb_filter.h) | **Since:** 0.13.0

Three comb filter primitives for modulation effects, physical modeling, and reverb diffusion.

**Use when:**
- Building flanger/chorus effects (FeedforwardComb - creates spectral notches)
- Karplus-Strong plucked string synthesis (FeedbackComb - creates resonant peaks)
- Reverb diffusion networks (SchroederAllpass - unity magnitude, phase dispersion)

**Note:** `SchroederAllpass` is distinct from `AllpassStage` in `diffusion_network.h` (Layer 2). `SchroederAllpass` is a reusable Layer 1 primitive with standard formulation and linear interpolation for modulation support, while `AllpassStage` is a composed processor using allpass interpolation.

```cpp
// Feedforward (FIR) comb: y[n] = x[n] + g * x[n-D]
class FeedforwardComb {
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;
    void setGain(float g) noexcept;           // [0.0, 1.0]
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};

// Feedback (IIR) comb with damping: y[n] = x[n] + g * LP(y[n-D])
class FeedbackComb {
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;
    void setFeedback(float g) noexcept;       // [-0.9999, 0.9999]
    void setDamping(float d) noexcept;        // [0.0, 1.0] (0=bright, 1=dark)
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};

// Schroeder allpass: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
class SchroederAllpass {
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;
    void setCoefficient(float g) noexcept;    // [-0.9999, 0.9999]
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

| Class | Difference Equation | Use Case |
|-------|---------------------|----------|
| `FeedforwardComb` | y[n] = x[n] + g*x[n-D] | Flanger, chorus (notches) |
| `FeedbackComb` | y[n] = x[n] + g*LP(y[n-D]) | Karplus-Strong, reverb (peaks) |
| `SchroederAllpass` | y[n] = -g*x[n] + x[n-D] + g*y[n-D] | Reverb diffusion (unity mag) |

**Damping (FeedbackComb):**
```cpp
// Damping = 0.0: No filtering (bright, full bandwidth)
// Damping = 1.0: Maximum lowpass (dark, heavily filtered)
comb.setDamping(0.3f);  // Light high-frequency rolloff
```

**Example Usage:**
```cpp
// Flanger effect
FeedforwardComb flanger;
flanger.prepare(44100.0, 0.02f);  // 20ms max
flanger.setGain(0.7f);
flanger.setDelayMs(5.0f);  // Modulate with LFO for sweep

// Karplus-Strong string
FeedbackComb string;
string.prepare(44100.0, 0.05f);   // 50ms max (low note ~20Hz)
string.setFeedback(0.995f);       // Long decay
string.setDamping(0.2f);          // Natural string damping

// Reverb diffusion
SchroederAllpass diffuser;
diffuser.prepare(44100.0, 0.1f);
diffuser.setCoefficient(0.7f);
diffuser.setDelayMs(30.0f);
```

**Dependencies:** `core/db_utils.h` (flushDenormal, isNaN, isInf), `primitives/delay_line.h`

---

## LadderFilter (Moog Ladder)
**Path:** [ladder_filter.h](../../dsp/include/krate/dsp/primitives/ladder_filter.h) | **Since:** 0.13.0

Moog-style 4-pole resonant lowpass ladder filter with two processing models: Linear (Stilson/Smith) for CPU efficiency and Nonlinear (Huovilainen) for analog-like saturation character.

**Use when:**
- Classic Moog filter sound for synth-style processing
- Variable slope filtering (6-24 dB/oct)
- Self-oscillating filter for sine generation
- Delay effects requiring resonant lowpass character

**Note:** Use oversampling (2x/4x) with Nonlinear model to reduce aliasing from tanh saturation. Parameter smoothing (~5ms) prevents zipper noise during modulation.

```cpp
enum class LadderModel : uint8_t { Linear, Nonlinear };

class LadderFilter {
    static constexpr float kMinCutoff = 20.0f;
    static constexpr float kMaxCutoffRatio = 0.45f;
    static constexpr float kMinResonance = 0.0f;
    static constexpr float kMaxResonance = 4.0f;  // Self-oscillation at ~3.9
    static constexpr float kMinDriveDb = 0.0f;
    static constexpr float kMaxDriveDb = 24.0f;

    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    void setModel(LadderModel model) noexcept;
    void setOversamplingFactor(int factor) noexcept;    // 1, 2, or 4
    void setResonanceCompensation(bool enabled) noexcept;
    void setSlope(int poles) noexcept;                  // 1-4

    void setCutoff(float hz) noexcept;
    void setResonance(float amount) noexcept;           // 0-4
    void setDrive(float db) noexcept;                   // 0-24 dB

    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    [[nodiscard]] int getLatency() const noexcept;
};
```

| Model | CPU Budget | Oversampling | Character |
|-------|------------|--------------|-----------|
| Linear | <50ns/sample | None needed | Clean, CPU-efficient |
| Nonlinear | <150ns/sample (2x) | 2x recommended | Analog saturation |
| Nonlinear | <250ns/sample (4x) | 4x for HQ | Smooth harmonics |

| Slope | Attenuation at 1 Octave | Use Case |
|-------|------------------------|----------|
| 1 pole | -6 dB | Gentle tilt |
| 2 poles | -12 dB | Moderate rolloff |
| 3 poles | -18 dB | Steep rolloff |
| 4 poles | -24 dB | Classic Moog |

| Resonance | Behavior |
|-----------|----------|
| 0.0 | No resonance (pure lowpass) |
| 1.0-2.0 | Moderate emphasis at cutoff |
| 3.0-3.8 | Strong resonant peak |
| 3.9+ | Self-oscillation (sine output) |
| 4.0 | Maximum (capped for stability) |

**Example Usage:**
```cpp
LadderFilter filter;
filter.prepare(44100.0, 512);
filter.setModel(LadderModel::Nonlinear);
filter.setOversamplingFactor(2);
filter.setCutoff(1000.0f);
filter.setResonance(2.5f);
filter.setSlope(4);

float output = filter.process(input);
```

**Resonance Compensation:** Enable via `setResonanceCompensation(true)` to maintain consistent output level as resonance increases (uses formula `1.0 / (1.0 + resonance * 0.25)`).

**Dependencies:** `primitives/oversampler.h`, `primitives/smoother.h`, `core/fast_math.h`, `core/db_utils.h`, `core/math_constants.h`

---

## TwoPoleLP (Two-Pole Butterworth Lowpass)
**Path:** [two_pole_lp.h](../../dsp/include/krate/dsp/primitives/two_pole_lp.h) | **Since:** 0.14.0

12dB/octave Butterworth lowpass filter wrapper around Biquad. Provides maximally flat passband response for tone shaping applications.

**Use when:**
- Excitation filtering in physical models (Karplus-Strong brightness control)
- Simple tone shaping with smooth frequency response
- Any application requiring 2-pole lowpass with Butterworth characteristics

**Note:** This is a thin wrapper around `Biquad` configured as lowpass with Q = 0.7071 (Butterworth). For more filter types or variable Q, use `Biquad` directly.

```cpp
class TwoPoleLP {
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;           // [1 Hz, Nyquist*0.495]
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};
```

| Frequency | Response |
|-----------|----------|
| < cutoff/2 | Flat within 0.5 dB |
| At cutoff | -3 dB (Butterworth) |
| 1 octave above | -12 dB |
| 2 octaves above | -24 dB |

**Comparison with Biquad Lowpass:**

| Feature | TwoPoleLP | Biquad |
|---------|-----------|--------|
| Q control | Fixed (0.7071) | Variable |
| API | Simple (prepare/setCutoff) | Full (configure with all params) |
| Use case | Butterworth-only applications | General filtering |

**Example Usage:**
```cpp
TwoPoleLP brightnessFilter;
brightnessFilter.prepare(44100.0);
brightnessFilter.setCutoff(2000.0f);  // 2kHz cutoff

// Filter excitation noise
for (auto& sample : noiseBuffer) {
    sample = brightnessFilter.process(sample);
}
```

**Dependencies:** `primitives/biquad.h`

---

## HilbertTransform (Analytic Signal Generation)
**Path:** [hilbert_transform.h](../../dsp/include/krate/dsp/primitives/hilbert_transform.h) | **Since:** 0.14.0

Hilbert transform using Olli Niemitalo's allpass filter cascade approximation. Creates an analytic signal with 90-degree phase-shifted quadrature output for single-sideband modulation (frequency shifting).

**Use when:**
- Creating frequency shifter effects (single-sideband modulation)
- Ring modulation with precise frequency control
- Envelope detection via analytic signal magnitude
- Any application requiring a 90-degree phase-shifted signal pair

**Note:** Uses two parallel cascades of 4 first-order allpass filters with coefficients optimized for wideband 90-degree phase accuracy. Fixed 5-sample latency regardless of sample rate.

```cpp
struct HilbertOutput {
    float i;  ///< In-phase component (original signal, delayed)
    float q;  ///< Quadrature component (90 degrees phase-shifted)
};

class HilbertTransform {
    void prepare(double sampleRate) noexcept;       // 22050-192000 Hz (clamped)
    void reset() noexcept;                          // Clear all filter states
    [[nodiscard]] HilbertOutput process(float input) noexcept;
    void processBlock(const float* input, float* outI, float* outQ, int numSamples) noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] int getLatencySamples() const noexcept;  // Always returns 5
};
```

| Frequency Range | Phase Accuracy | Notes |
|-----------------|----------------|-------|
| 40Hz - 2kHz | Excellent (<1 deg) | Optimal range for frequency shifting |
| 2kHz - 5kHz | Good (<5 deg) | Acceptable for most audio |
| 5kHz - 10kHz | Degraded (<10 deg) | High frequency limitation |
| >0.9*Nyquist | Not guaranteed | Known limitation of allpass approximation |

**Frequency Shifting Pattern:**
```cpp
HilbertTransform hilbert;
LFO modulator;

hilbert.prepare(44100.0);
modulator.prepare(44100.0);
modulator.setFrequency(5.0f);  // 5 Hz shift

for (auto& sample : buffer) {
    HilbertOutput h = hilbert.process(sample);
    float phase = modulator.getPhase() * 2.0f * kPi;
    // Upper sideband (frequency shift up)
    sample = h.i * std::cos(phase) - h.q * std::sin(phase);
    // Lower sideband (frequency shift down)
    // sample = h.i * std::cos(phase) + h.q * std::sin(phase);
    modulator.process();
}
```

**Implementation Notes:**
- Uses Olli Niemitalo coefficients (8 allpass sections total)
- Path 1: odd-indexed coefficients (a1, a3, a5, a7) with 1-sample delay
- Path 2: even-indexed coefficients (a0, a2, a4, a6)
- Niemitalo allpass form: y[n] = -a*x[n] + x[n-1] + a*y[n-1]
- Denormal flushing on all filter states and outputs

**Dependencies:** `core/db_utils.h` (isNaN, isInf)

---

## SequencerCore (Step Sequencer Timing Engine)
**Path:** [sequencer_core.h](../../dsp/include/krate/dsp/primitives/sequencer_core.h) | **Since:** 0.14.0

Reusable timing engine for tempo-synchronized step sequencers. Provides step timing, direction control, swing, gate length, and transport sync. Used by FilterStepSequencer and VowelSequencer.

**Use when:**
- Building any tempo-synced step sequencer effect
- Need timing with swing/groove control
- Require transport sync (PPQ position)
- Want gate-length control with crossfade

**Note:** This is a Layer 1 primitive that provides timing logic only. Parameter interpolation and actual audio processing must be handled by the composing system (Layer 3).

```cpp
enum class Direction : uint8_t { Forward, Backward, PingPong, Random };

class SequencerCore {
    static constexpr size_t kMaxSteps = 16;
    static constexpr float kMinTempoBPM = 20.0f;
    static constexpr float kMaxTempoBPM = 300.0f;
    static constexpr float kGateCrossfadeMs = 5.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Step configuration
    void setNumSteps(size_t numSteps) noexcept;
    [[nodiscard]] size_t getNumSteps() const noexcept;

    // Timing configuration
    void setTempo(float bpm) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setSwing(float swing) noexcept;          // [0, 1] - 0.5 = 3:1 ratio
    void setGateLength(float gateLength) noexcept; // [0, 1] - fraction of step

    // Direction
    void setDirection(Direction direction) noexcept;
    [[nodiscard]] Direction getDirection() const noexcept;

    // Transport
    void sync(double ppqPosition) noexcept;       // Sync to DAW transport
    void trigger() noexcept;                       // Manual step advance
    [[nodiscard]] int getCurrentStep() const noexcept;

    // Processing (call once per sample)
    [[nodiscard]] bool tick() noexcept;           // Returns true on step change
    [[nodiscard]] bool isGateActive() const noexcept;
    [[nodiscard]] float getGateRampValue() noexcept;  // 5ms crossfade
};
```

| Direction | Pattern (4 steps) |
|-----------|-------------------|
| Forward | 0, 1, 2, 3, 0, 1, ... |
| Backward | 3, 2, 1, 0, 3, 2, ... |
| PingPong | 0, 1, 2, 3, 2, 1, 0, 1, ... (endpoints once per cycle) |
| Random | Random, no immediate repeat |

| Swing | Even:Odd Duration Ratio |
|-------|------------------------|
| 0.0 | 1:1 (no swing) |
| 0.5 | 3:1 (heavy swing) |
| 1.0 | Maximum |

**Example Usage:**
```cpp
SequencerCore core;
core.prepare(44100.0);
core.setNumSteps(4);
core.setTempo(120.0f);
core.setNoteValue(NoteValue::Eighth);
core.setDirection(Direction::Forward);
core.setSwing(0.3f);
core.setGateLength(0.75f);

// In process loop:
if (core.tick()) {
    int step = core.getCurrentStep();
    applyStepParameters(step);  // Your parameter logic
}
bool gateOn = core.isGateActive();
float gateValue = core.getGateRampValue();
```

**Dependencies:** `core/note_value.h`, `primitives/smoother.h` (LinearRamp)

---

## ChaosWaveshaper (Chaos Attractor Waveshaper)
**Path:** [chaos_waveshaper.h](../../dsp/include/krate/dsp/primitives/chaos_waveshaper.h) | **Since:** 0.14.0

Time-varying waveshaping using chaos attractor dynamics. The attractor's normalized X component modulates the drive of a tanh-based soft-clipper, producing distortion that evolves over time without external modulation.

**Use when:**
- Creating evolving distortion effects that change character over time
- Building unique delay feedback processing that mutates autonomously
- Need distortion that responds to input dynamics (input coupling)
- Want analog-style unpredictability in digital processing

**Note:** Uses internal 2x oversampling for anti-aliased waveshaping. Compose with DCBlocker when using in feedback loops.

```cpp
enum class ChaosModel : uint8_t {
    Lorenz,   // Swirling, unpredictable (sigma=10, rho=28, beta=8/3)
    Rossler,  // Smoother spiraling patterns (a=0.2, b=0.2, c=5.7)
    Chua,     // Double-scroll bi-modal jumps (alpha=15.6, beta=28)
    Henon     // Sharp rhythmic transitions (a=1.4, b=0.3)
};

class ChaosWaveshaper {
    static constexpr size_t kControlRateInterval = 32;  // Control-rate updates

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    void setModel(ChaosModel model) noexcept;           // Select chaos algorithm
    void setChaosAmount(float amount) noexcept;         // [0, 1] dry/wet mix
    void setAttractorSpeed(float speed) noexcept;       // [0.01, 100] evolution rate
    void setInputCoupling(float coupling) noexcept;     // [0, 1] input reactivity

    [[nodiscard]] ChaosModel getModel() const noexcept;
    [[nodiscard]] float getChaosAmount() const noexcept;
    [[nodiscard]] float getAttractorSpeed() const noexcept;
    [[nodiscard]] float getInputCoupling() const noexcept;
    [[nodiscard]] size_t latency() const noexcept;      // 0 for default mode

    [[nodiscard]] float process(float input) noexcept;  // Single sample (no oversampling)
    void processBlock(float* buffer, size_t numSamples) noexcept;  // Block with oversampling
};
```

| Model | Character | Use Case |
|-------|-----------|----------|
| Lorenz | Swirling, unpredictable | Classic chaos, ambient textures |
| Rossler | Smooth spiraling | Gentler modulation, synth pads |
| Chua | Bi-modal jumps | Circuit-derived, edgy transitions |
| Henon | Sharp rhythmic | Discrete map, rhythmic artifacts |

| Parameter | Range | Description |
|-----------|-------|-------------|
| ChaosAmount | [0, 1] | 0 = bypass, 1 = full chaos processing |
| AttractorSpeed | [0.01, 100] | Evolution rate multiplier |
| InputCoupling | [0, 1] | How much input amplitude perturbs attractor |

**Example Usage:**
```cpp
ChaosWaveshaper shaper;
shaper.prepare(44100.0, 512);
shaper.setModel(ChaosModel::Lorenz);
shaper.setChaosAmount(0.5f);       // 50% wet
shaper.setAttractorSpeed(1.0f);   // Nominal rate
shaper.setInputCoupling(0.3f);    // Moderate input reactivity

// Block processing (preferred - uses oversampling)
shaper.processBlock(buffer, numSamples);
```

**Implementation Notes:**
- Control-rate attractor updates every 32 samples for efficiency
- Internal 2x oversampling via `Oversampler<2, 1>` for anti-aliasing
- Automatic state reset on divergence (NaN/Inf/bounds exceeded)
- Sample-rate compensated integration for consistent behavior across rates
- Input coupling accumulates envelope over control interval before perturbation

**Dependencies:** `primitives/oversampler.h`, `core/sigmoid.h` (tanhVariable), `core/db_utils.h` (flushDenormal, isNaN, isInf)

---

## StochasticShaper (Analog-Style Random Waveshaper)
**Path:** [stochastic_shaper.h](../../dsp/include/krate/dsp/primitives/stochastic_shaper.h) | **Since:** 0.14.0

Waveshaper with stochastic modulation for analog-style variation. Adds controlled randomness to waveshaping transfer functions, simulating analog component tolerance variation where each sample passes through a slightly different curve.

**Use when:**
- Adding subtle analog imperfection to digital distortion
- Creating warmth and "life" that distinguishes from clinical digital saturation
- Building evolving distortion effects without external modulation
- Simulating component drift and tolerance variations

**Note:** This is a composition of Waveshaper, Xorshift32 RNG, and OnePoleSmoother. No internal oversampling - compose with Oversampler if needed. Compose with DCBlocker for asymmetric waveshape types.

```cpp
class StochasticShaper {
    static constexpr float kDefaultJitterRate = 10.0f;   // Hz
    static constexpr float kMinJitterRate = 0.01f;       // Hz
    static constexpr float kMaxJitterOffset = 0.5f;      // At amount=1.0
    static constexpr float kDriveModulationRange = 0.5f; // +/- 50% at coeffNoise=1.0
    static constexpr float kDefaultDrive = 1.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Base waveshaper configuration
    void setBaseType(WaveshapeType type) noexcept;       // Tanh, Atan, Tube, etc.
    void setDrive(float drive) noexcept;                 // Saturation intensity

    // Jitter parameters (random signal offset)
    void setJitterAmount(float amount) noexcept;         // [0, 1] intensity
    void setJitterRate(float hz) noexcept;               // [0.01, Nyquist/2] Hz

    // Coefficient noise (random drive modulation)
    void setCoefficientNoise(float amount) noexcept;     // [0, 1] drive variation

    // Reproducibility
    void setSeed(uint32_t seed) noexcept;                // Deterministic output

    // Processing
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Diagnostics (for testing/validation)
    [[nodiscard]] float getCurrentJitter() const noexcept;
    [[nodiscard]] float getCurrentDriveModulation() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

| Parameter | Range | Description |
|-----------|-------|-------------|
| JitterAmount | [0, 1] | 0 = no offset, 1 = +/-0.5 offset range |
| JitterRate | [0.01, Nyquist/2] Hz | Low = slow drift, high = rapid texture |
| CoefficientNoise | [0, 1] | 0 = constant drive, 1 = +/-50% drive modulation |
| Seed | uint32_t | Same seed = identical output |

**Jitter Rate Character:**

| Rate | Character | Use Case |
|------|-----------|----------|
| 0.1 Hz | Slow component drift | Subtle analog warmth |
| 10 Hz | Moderate variation | General use (default) |
| 100+ Hz | Rapid texture | Gritty, noisy character |

**Example Usage:**
```cpp
StochasticShaper shaper;
shaper.prepare(44100.0);
shaper.setBaseType(WaveshapeType::Tanh);
shaper.setDrive(2.0f);
shaper.setJitterAmount(0.3f);     // Subtle random offset
shaper.setJitterRate(10.0f);      // Moderate variation rate
shaper.setCoefficientNoise(0.2f); // Subtle drive variation
shaper.setSeed(42);               // Reproducible output

// Process
float output = shaper.process(input);
```

**Key Behaviors:**
- `jitterAmount=0 AND coefficientNoise=0` equals standard Waveshaper (bypass mode)
- Same seed with same parameters produces identical output (deterministic)
- Uses two independent OnePoleSmoother instances for uncorrelated jitter and drive variation
- NaN input treated as 0.0, Infinity clamped to [-1, 1]

**Dependencies:** `primitives/waveshaper.h`, `primitives/smoother.h`, `core/random.h`, `core/db_utils.h`

---

## RingSaturation (Self-Modulation Distortion)
**Path:** [ring_saturation.h](../../dsp/include/krate/dsp/primitives/ring_saturation.h) | **Since:** 0.14.0

Self-modulation distortion primitive that creates metallic, bell-like character through signal-coherent inharmonic sidebands. Unlike traditional ring modulation with external carriers, RingSaturation uses the signal's own saturated version to modulate itself.

**Use when:**
- Creating metallic, bell-like distortion tones
- Need inharmonic sidebands that track the input frequency
- Building complex, evolving distortion character
- Want distortion with spectral content beyond simple harmonics

**Note:** Multi-stage processing (1-4 stages) progressively increases spectral complexity. Compose with Oversampler for extreme drive settings at high frequencies. Built-in 10Hz DC blocker handles asymmetric saturation.

```cpp
class RingSaturation {
    static constexpr int kMinStages = 1;
    static constexpr int kMaxStages = 4;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kCrossfadeTimeMs = 10.0f;
    static constexpr float kSoftLimitScale = 2.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Saturation curve selection (click-free 10ms crossfade)
    void setSaturationCurve(WaveshapeType type) noexcept;
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;

    // Drive control (pre-gain into saturation)
    void setDrive(float drive) noexcept;            // [0, unbounded), negative clamped to 0
    [[nodiscard]] float getDrive() const noexcept;

    // Modulation depth (ring modulation mix)
    void setModulationDepth(float depth) noexcept;  // [0, 1] clamped
    [[nodiscard]] float getModulationDepth() const noexcept;

    // Multi-stage processing
    void setStages(int stages) noexcept;            // [1, 4] clamped
    [[nodiscard]] int getStages() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

**Core Formula:**
```
output = input + (input * saturate(input * drive) - input) * depth
```

This formula produces inharmonic sidebands because the saturated signal contains harmonics that, when multiplied with the input, create sum and difference frequencies.

| Parameter | Range | Description |
|-----------|-------|-------------|
| Drive | [0, unbounded) | Saturation intensity (typical: 0.5-10) |
| ModulationDepth | [0, 1] | 0 = dry signal, 1 = full ring modulation |
| Stages | [1, 4] | Each stage feeds output to next |
| SaturationCurve | WaveshapeType | Tanh, Tube, HardClip, etc. |

| Stages | Spectral Complexity | Use Case |
|--------|---------------------|----------|
| 1 | Moderate sidebands | Subtle metallic coloring |
| 2 | More complex spectrum | Medium distortion |
| 3 | High complexity | Heavy processing |
| 4 | Maximum entropy | Extreme textures |

**Example Usage:**
```cpp
RingSaturation ringSat;
ringSat.prepare(44100.0);
ringSat.setDrive(2.5f);              // Moderate saturation
ringSat.setModulationDepth(0.8f);    // Strong ring effect
ringSat.setStages(2);                // Medium complexity
ringSat.setSaturationCurve(WaveshapeType::Tube);  // Warm character

// Process block
ringSat.processBlock(buffer, numSamples);

// Single sample
float output = ringSat.process(input);
```

**Key Behaviors:**
- `depth=0` returns input unchanged (true bypass)
- `drive=0` with `depth=0.5` returns `input * 0.5` (attenuation only)
- Output soft-limited to approach +/-2.0 asymptotically (SC-005)
- DC blocker removes offset from asymmetric saturation (10Hz cutoff)
- Curve switching crossfades over 10ms (click-free)
- NaN input returns NaN without corrupting state

**Related Primitives:** Waveshaper (underlying saturation), DCBlocker (DC removal), ChaosWaveshaper (time-varying), StochasticShaper (random variation)

**Dependencies:** `primitives/waveshaper.h`, `primitives/dc_blocker.h`, `primitives/smoother.h`, `core/sigmoid.h`, `core/db_utils.h`

---

## BitwiseMangler (Bit Manipulation Distortion)
**Path:** [bitwise_mangler.h](../../dsp/include/krate/dsp/primitives/bitwise_mangler.h) | **Since:** 0.15.0

Bit manipulation distortion with six operation modes for wild tonal shifts. Converts audio samples to 24-bit integer representation, applies bitwise operations, and converts back to float.

**Use when:**
- Creating unconventional digital distortion effects
- Need signal-dependent distortion (XorPrevious) that responds to transients
- Want deterministic chaos (BitShuffle) with reproducible output
- Simulating integer overflow artifacts (OverflowWrap)
- Building glitch/destruction effects with precise control

**Note:** XorPrevious and BitAverage maintain previous sample state. DC blocking is enabled by default to remove DC offset introduced by these stateful operations. Disable DC blocking with `setDCBlockEnabled(false)` for "utter destruction" mode. Compose with Oversampler if aliasing artifacts are undesired. Output may exceed [-1, 1] in OverflowWrap mode.

```cpp
enum class BitwiseOperation : uint8_t {
    XorPattern,   // XOR with configurable 32-bit pattern
    XorPrevious,  // XOR current sample with previous sample
    BitRotate,    // Circular bit rotation left/right
    BitShuffle,   // Deterministic bit permutation from seed
    BitAverage,   // Bitwise AND with previous sample
    OverflowWrap  // Integer overflow wrap behavior
};

class BitwiseMangler {
    static constexpr float kDefaultIntensity = 1.0f;
    static constexpr uint32_t kDefaultPattern = 0xAAAAAAAAu;
    static constexpr int kMinRotateAmount = -16;
    static constexpr int kMaxRotateAmount = 16;
    static constexpr uint32_t kDefaultSeed = 12345u;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setOperation(BitwiseOperation op) noexcept;
    [[nodiscard]] BitwiseOperation getOperation() const noexcept;

    void setIntensity(float intensity) noexcept;      // [0, 1] wet/dry mix
    [[nodiscard]] float getIntensity() const noexcept;

    void setPattern(uint32_t pattern) noexcept;       // XorPattern mode
    [[nodiscard]] uint32_t getPattern() const noexcept;

    void setRotateAmount(int bits) noexcept;          // [-16, +16], BitRotate mode
    [[nodiscard]] int getRotateAmount() const noexcept;

    void setSeed(uint32_t seed) noexcept;             // BitShuffle mode
    [[nodiscard]] uint32_t getSeed() const noexcept;

    void setDCBlockEnabled(bool enabled) noexcept;   // Default: true
    [[nodiscard]] bool isDCBlockEnabled() const noexcept;

    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
    [[nodiscard]] static constexpr size_t getLatency() noexcept;  // Always 0
};
```

| Mode | Character | Use Case |
|------|-----------|----------|
| XorPattern | Metallic harmonics | Aggressive digital distortion |
| XorPrevious | Transient-responsive | Dynamic distortion |
| BitRotate | Pseudo-pitch shift | Unusual frequency effects |
| BitShuffle | Chaotic destruction | Extreme sound design |
| BitAverage | Smoothing/thinning | Subtle bit-level processing |
| OverflowWrap | Hard digital artifacts | Integer overflow simulation |

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Intensity | [0, 1] | 1.0 | 0 = bypass, 1 = full effect |
| Pattern | 32-bit | 0xAAAAAAAA | XOR mask (alternating bits) |
| RotateAmount | [-16, +16] | 0 | Positive = left, negative = right |
| Seed | Non-zero | 12345 | Permutation seed for BitShuffle |
| DCBlockEnabled | bool | true | Remove DC offset from output |

**Example Usage:**
```cpp
BitwiseMangler mangler;
mangler.prepare(44100.0);
mangler.setOperation(BitwiseOperation::XorPattern);
mangler.setPattern(0xAAAAAAAAu);  // Alternating bits
mangler.setIntensity(0.5f);       // 50% wet

// Process
float output = mangler.process(input);

// Signal-dependent distortion
mangler.setOperation(BitwiseOperation::XorPrevious);
// High frequencies produce more dramatic changes than low frequencies

// Deterministic chaos
mangler.setOperation(BitwiseOperation::BitShuffle);
mangler.setSeed(42);              // Same seed = same output

// "Utter destruction" mode - disable DC blocking
mangler.setDCBlockEnabled(false);
```

**Key Behaviors:**
- `intensity=0` produces bit-exact passthrough (SC-009)
- DC blocking enabled by default removes offset from XorPrevious/BitAverage (SC-010)
- XorPattern with pattern `0x00000000` = bypass
- XorPattern with pattern `0xFFFFFFFF` = invert all bits
- BitRotate with amount `0` = passthrough
- BitShuffle same seed after reset = identical output (SC-004)
- OverflowWrap wraps values exceeding 24-bit integer range
- Zero latency (SC-007)
- NaN/Inf input returns 0.0 (FR-022)

**Related Primitives:** BitCrusher (quantization), Waveshaper (smooth saturation)

**Dependencies:** `core/random.h` (Xorshift32), `core/db_utils.h` (isNaN, isInf, flushDenormal)

---

## SweepPositionBuffer (Lock-Free Audio-UI Sync)
**Path:** [sweep_position_buffer.h](../../dsp/include/krate/dsp/primitives/sweep_position_buffer.h) | **Since:** 0.15.0

Lock-free Single-Producer Single-Consumer (SPSC) ring buffer for communicating sweep position data from audio thread to UI thread.

**Use when:**
- Audio thread needs to send visualization data to UI (sweep position, waveform data)
- Real-time audio data needs UI display without blocking
- Building audio-visual synchronized effects (spectrum analyzers, sweep indicators)

**Note:** This is a thread-safe communication primitive. Producer (audio thread) calls `push()`, consumer (UI thread) calls `pop()`/`getLatest()`. No locks or allocations after construction.

```cpp
struct SweepPositionData {
    float centerFreqHz = 1000.0f;   // Current sweep center frequency in Hz
    float widthOctaves = 1.5f;      // Sweep width in octaves
    float intensity = 0.5f;         // Intensity multiplier [0.0, 2.0]
    uint64_t samplePosition = 0;    // Sample count for timing sync
    bool enabled = false;           // Sweep on/off state
    uint8_t falloff = 1;            // Falloff mode (0=Sharp, 1=Smooth)
};

class SweepPositionBuffer {
    static constexpr int kSweepBufferSize = 8;  // ~100ms at typical block sizes

    // Producer (audio thread)
    bool push(const SweepPositionData& data) noexcept;

    // Consumer (UI thread)
    bool pop(SweepPositionData& data) noexcept;
    [[nodiscard]] bool getLatest(SweepPositionData& data) const noexcept;
    bool drainToLatest(SweepPositionData& data) noexcept;
    [[nodiscard]] SweepPositionData getInterpolatedPosition(uint64_t targetSample) const noexcept;

    // Utility
    void clear() noexcept;
    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] int count() const noexcept;
};
```

| Method | Thread | Description |
|--------|--------|-------------|
| `push()` | Audio only | Add new position data (returns false if full) |
| `pop()` | UI only | Remove oldest entry (FIFO order) |
| `getLatest()` | UI only | Peek newest entry without removing |
| `drainToLatest()` | UI only | Clear buffer, return newest |
| `getInterpolatedPosition()` | UI only | Interpolate for smooth 60fps display |

**Example Usage (Processor side):**
```cpp
// In Processor::process()
if (sweepProcessor_.isEnabled()) {
    SweepPositionData data;
    data.centerFreqHz = sweepProcessor_.getCenterFrequency();
    data.widthOctaves = sweepProcessor_.getWidth();
    data.intensity = sweepProcessor_.getIntensity();
    data.samplePosition = currentSamplePosition;
    data.enabled = true;
    sweepPositionBuffer_.push(data);
}
```

**Example Usage (UI side):**
```cpp
// In Controller::idle() or timer callback
SweepPositionData data;
if (sweepPositionBuffer_->getLatest(data)) {
    sweepIndicator_->setPosition(data.centerFreqHz, data.widthOctaves, data.intensity);
    sweepIndicator_->setEnabled(data.enabled);
}
```

**Dependencies:** Standard library only (std::atomic, std::array)

---

## PolyBlepOscillator (Band-Limited Audio-Rate Oscillator)
**Path:** [polyblep_oscillator.h](../../dsp/include/krate/dsp/primitives/polyblep_oscillator.h) | **Since:** 0.15.0

Band-limited audio-rate oscillator using polynomial band-limited step (PolyBLEP) correction. Generates sine, sawtooth, square, pulse (variable width), and triangle waveforms with anti-aliased discontinuity handling. Triangle is generated via leaky integration of a PolyBLEP-corrected square wave.

**Use when:**
- Building synthesizer voices (subtractive, FM, additive)
- Audio-rate modulation sources (FM operators, ring mod carriers)
- Generating test signals with controlled spectral content
- Any application requiring band-limited oscillation at audio frequencies

**Note:** This is a scalar-only implementation designed for future SIMD optimization. Uses value semantics (copyable/movable) for efficient composition into multi-voice contexts (unison, supersaw). Does not perform internal oversampling -- compose with `Oversampler` if needed. Assumes FTZ/DAZ CPU flags are set at the processor level.

```cpp
enum class OscWaveform : uint8_t {
    Sine = 0,       // Pure sine (no PolyBLEP)
    Sawtooth = 1,   // PolyBLEP at wrap discontinuity
    Square = 2,     // PolyBLEP at rising + falling edges
    Pulse = 3,      // PolyBLEP at variable-position edges
    Triangle = 4    // Leaky-integrated PolyBLEP square
};

class PolyBlepOscillator {
    // Lifecycle
    void prepare(double sampleRate) noexcept;          // Init (NOT real-time safe)
    void reset() noexcept;                              // Clear state, preserve config

    // Parameters
    void setFrequency(float hz) noexcept;               // [0, sampleRate/2), NaN-safe
    void setWaveform(OscWaveform waveform) noexcept;    // Clears integrator on Triangle switch
    void setPulseWidth(float width) noexcept;            // [0.01, 0.99], Pulse only

    // Processing (real-time safe, noexcept)
    [[nodiscard]] float process() noexcept;              // Single sample
    void processBlock(float* output, size_t numSamples) noexcept;

    // Phase access (for sync/sub-oscillator integration)
    [[nodiscard]] double phase() const noexcept;         // [0, 1)
    [[nodiscard]] bool phaseWrapped() const noexcept;    // True on cycle boundary
    void resetPhase(double newPhase = 0.0) noexcept;    // Hard sync, preserves integrator

    // Modulation (per-sample, non-accumulating)
    void setPhaseModulation(float radians) noexcept;     // Yamaha-style PM
    void setFrequencyModulation(float hz) noexcept;      // Linear FM offset
};
```

| Waveform | Anti-Aliasing | Character |
|----------|---------------|-----------|
| Sine | None needed | Pure fundamental |
| Sawtooth | 4-point PolyBLEP at wrap | Rich harmonics, bright |
| Square | 4-point PolyBLEP at both edges | Hollow, odd harmonics |
| Pulse | 4-point PolyBLEP at variable edges | PWM, duty cycle control |
| Triangle | Leaky-integrated PolyBLEP square | Soft, odd harmonics |

| Feature | Detail |
|---------|--------|
| Alias suppression | >= 40 dB below fundamental (SC-001 through SC-003) |
| Output range | [-1.1, 1.1] nominal, sanitized to [-2, 2] |
| Performance | ~6-9 ns/sample (~17-28 cycles at 3 GHz) |
| NaN/Inf safety | Branchless sanitization, NaN-safe parameter setters |
| Phase interface | phase(), phaseWrapped(), resetPhase() for sync |
| FM/PM | Per-sample linear FM + Yamaha-style PM |

**Reusability (downstream oscillator ecosystem):**
- `OscWaveform` enum shared by SyncOscillator (Phase 5), SubOscillator (Phase 6), UnisonEngine (Phase 7)
- `PolyBlepOscillator` composed into SyncOscillator (slave), UnisonEngine (per-voice), Rungler (Phase 15)
- `phase()` / `phaseWrapped()` interface designed for master-slave sync topology
- Value semantics enables direct array storage in multi-voice contexts

**Example Usage:**
```cpp
PolyBlepOscillator osc;
osc.prepare(44100.0);
osc.setFrequency(440.0f);
osc.setWaveform(OscWaveform::Sawtooth);

// Single-sample processing
for (int i = 0; i < numSamples; ++i) {
    output[i] = osc.process();
}

// Block processing
osc.processBlock(output, numSamples);

// FM synthesis (per-sample modulation)
for (int i = 0; i < numSamples; ++i) {
    osc.setFrequencyModulation(modSignal[i] * 200.0f);
    output[i] = osc.process();
}

// Sync detection (for master oscillator)
for (int i = 0; i < numSamples; ++i) {
    output[i] = osc.process();
    if (osc.phaseWrapped()) {
        slaveOsc.resetPhase(0.0);
    }
}
```

**Dependencies:** `core/polyblep.h` (polyBlep4), `core/phase_utils.h` (PhaseAccumulator, wrapPhase), `core/math_constants.h` (kTwoPi), `core/db_utils.h` (detail::isNaN, detail::isInf)
