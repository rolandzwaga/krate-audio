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

## WavetableGenerator (Mipmapped Wavetable Generation)
**Path:** [wavetable_generator.h](../../dsp/include/krate/dsp/primitives/wavetable_generator.h) | **Since:** 0.15.0

Mipmapped wavetable generation via FFT/IFFT for standard waveforms, custom harmonic spectra, and raw waveform samples. Populates WavetableData with band-limited mipmap levels, each independently normalized with correct guard samples.

**Use when:**
- Generating mipmapped wavetables during plugin initialization
- Creating standard waveforms (sawtooth, square, triangle) with band-limiting
- Importing custom harmonic spectra or raw single-cycle waveforms
- Populating WavetableData for use with WavetableOscillator

**Note:** NOT real-time safe. All functions allocate temporary buffers and perform FFT. Call once during initialization, share the resulting WavetableData across oscillator instances via non-owning pointers.

```cpp
// Standard waveform generators
inline void generateMipmappedSaw(WavetableData& data);       // 1/n harmonic series
inline void generateMipmappedSquare(WavetableData& data);     // Odd harmonics, 1/n
inline void generateMipmappedTriangle(WavetableData& data);   // Odd harmonics, 1/n^2, alternating sign

// Custom spectrum generator
inline void generateMipmappedFromHarmonics(
    WavetableData& data,
    const float* harmonicAmplitudes,   // index 0 = fundamental (harmonic 1)
    size_t numHarmonics                // 0 = silence at all levels
);

// Raw sample generator (FFT analysis/resynthesis)
inline void generateMipmappedFromSamples(
    WavetableData& data,
    const float* samples,              // Single-cycle waveform
    size_t sampleCount                 // 0 = no modification
);
```

| Function | Spectrum | Use Case |
|----------|----------|----------|
| `generateMipmappedSaw` | All harmonics, 1/n | Bright, rich oscillator |
| `generateMipmappedSquare` | Odd harmonics, 1/n | Hollow, reedy tone |
| `generateMipmappedTriangle` | Odd harmonics, 1/n^2 | Soft, mellow tone |
| `generateMipmappedFromHarmonics` | User-defined | Custom timbres |
| `generateMipmappedFromSamples` | Analyzed via FFT | External wavetable import |

**Normalization:** Each mipmap level is independently normalized to ~0.96 peak amplitude.

**Guard Samples:** All generators set wraparound guard samples for branchless cubic Hermite interpolation (p[-1] = last sample, p[N] = first sample, etc.).

**Example Usage:**
```cpp
WavetableData sawTable;
generateMipmappedSaw(sawTable);
// sawTable.numLevels() == 11, each level band-limited for its frequency range

// Custom organ-like harmonic spectrum
float harmonics[] = {1.0f, 0.0f, 0.5f, 0.0f, 0.3f};  // 1st, 3rd, 5th only
WavetableData organTable;
generateMipmappedFromHarmonics(organTable, harmonics, 5);

// Import external single-cycle waveform
WavetableData importedTable;
generateMipmappedFromSamples(importedTable, wavSamples, wavSampleCount);
```

**Dependencies:** `core/wavetable_data.h`, `core/math_constants.h`, `primitives/fft.h`

---

## WavetableOscillator (Mipmapped Wavetable Playback)
**Path:** [wavetable_oscillator.h](../../dsp/include/krate/dsp/primitives/wavetable_oscillator.h) | **Since:** 0.15.0

Real-time wavetable playback with automatic mipmap selection, cubic Hermite interpolation, and mipmap crossfading. Follows the same interface pattern as PolyBlepOscillator for interchangeability in downstream components (FM Operator, PD Oscillator, Vector Mixer).

**Use when:**
- Building synthesizer voices with arbitrary wavetable timbres
- FM operators that need wavetable sources (instead of or alongside PolyBLEP)
- Phase distortion (PD) oscillators reading from custom waveforms
- Any application requiring alias-free playback of stored single-cycle waveforms

**Note:** Holds a non-owning pointer to WavetableData. The caller must ensure the WavetableData outlives the oscillator. Multiple oscillators can share a single WavetableData (~90 KB) for polyphonic voices.

```cpp
class WavetableOscillator {
    // Lifecycle
    void prepare(double sampleRate) noexcept;           // Init (NOT real-time safe)
    void reset() noexcept;                               // Clear state, preserve config

    // Parameters
    void setWavetable(const WavetableData* table) noexcept;  // Non-owning pointer
    void setFrequency(float hz) noexcept;                     // [0, sampleRate/2), NaN-safe

    // Processing (real-time safe, noexcept)
    [[nodiscard]] float process() noexcept;              // Single sample
    void processBlock(float* output, size_t numSamples) noexcept;
    void processBlock(float* output, const float* fmBuffer, size_t numSamples) noexcept;

    // Phase access (matches PolyBlepOscillator interface)
    [[nodiscard]] double phase() const noexcept;         // [0, 1)
    [[nodiscard]] bool phaseWrapped() const noexcept;    // True on cycle boundary
    void resetPhase(double newPhase = 0.0) noexcept;     // Hard sync

    // Modulation (per-sample, non-accumulating)
    void setPhaseModulation(float radians) noexcept;     // Yamaha-style PM
    void setFrequencyModulation(float hz) noexcept;      // Linear FM offset
};
```

| Feature | Detail |
|---------|--------|
| Interpolation | Cubic Hermite (4-point, branchless via guard samples) |
| Mipmap selection | Automatic fractional level from frequency |
| Crossfading | Dual lookup with linear blend when frac in [0.05, 0.95] |
| Output range | Sanitized to [-2.0, 2.0], NaN replaced with 0.0 |
| Phase precision | double (prevents accumulated rounding over long playback) |
| FM/PM | Per-sample, non-accumulating, reset after each process() call |

**WavetableOscillator vs PolyBlepOscillator:**

| Feature | WavetableOscillator | PolyBlepOscillator |
|---------|---------------------|--------------------|
| Timbres | Arbitrary (any wavetable) | 5 standard waveforms |
| Anti-aliasing | Mipmap band-limiting (no aliasing) | PolyBLEP correction (~40 dB) |
| Memory | ~90 KB per WavetableData (shared) | ~64 bytes per instance |
| CPU | Table read + interpolation | Polynomial computation |
| Best for | Custom timbres, wavetable synths | Simple waveforms, minimal memory |
| Interface | Identical (interchangeable) | Identical (interchangeable) |

**Complementary Roles:** These oscillators serve different use cases but share the same interface. Use PolyBlepOscillator for standard waveforms with minimal memory. Use WavetableOscillator when arbitrary timbres or zero-aliasing playback is needed. Downstream components (FM Operator, PD Oscillator, Vector Mixer) can accept either through their shared interface.

**Example Usage:**
```cpp
// Initialize (once, at startup)
WavetableData sawTable;
generateMipmappedSaw(sawTable);

// Create oscillator
WavetableOscillator osc;
osc.prepare(44100.0);
osc.setWavetable(&sawTable);
osc.setFrequency(440.0f);

// Generate audio (real-time safe)
float output[512];
osc.processBlock(output, 512);

// FM synthesis (per-sample modulation)
float fmBuffer[512];  // Filled by modulator
osc.processBlock(output, fmBuffer, 512);

// Phase modulation
osc.setPhaseModulation(modSignal * 3.14159f);
float sample = osc.process();

// Polyphonic sharing (multiple voices, one table)
WavetableOscillator voice1, voice2;
voice1.prepare(44100.0);  voice1.setWavetable(&sawTable);
voice2.prepare(44100.0);  voice2.setWavetable(&sawTable);  // Same table pointer
```

**Dependencies:** `core/wavetable_data.h`, `core/interpolation.h`, `core/phase_utils.h`, `core/math_constants.h`, `core/db_utils.h`

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

---

## MinBlepTable (Minimum-Phase Band-Limited Step Function)
**Path:** [minblep_table.h](../../dsp/include/krate/dsp/primitives/minblep_table.h) | **Since:** 0.15.0

Precomputed minimum-phase band-limited step function (minBLEP) table for high-quality discontinuity correction in sync oscillators, hard sync, and any waveform with instantaneous transitions. Generates the table once during initialization, then provides real-time sub-sample-accurate lookup.

**Use when:**
- Hard sync oscillators need discontinuity correction at the slave reset point
- Sub-oscillator sync requires band-limited transitions
- Any waveform generator produces hard discontinuities that cause aliasing
- Higher quality than PolyBLEP is needed (>50 dB alias rejection vs ~40 dB)

**Note:** MinBLEP is complementary to PolyBLEP. PolyBLEP is cheaper (no table lookup, ~40 dB rejection) and suitable for standard waveforms. MinBLEP provides higher quality (>50 dB with 32 zero crossings) at the cost of table memory and per-discontinuity ring buffer stamping. Use MinBLEP when the extra quality matters (sync oscillators, sub-oscillators, FM carrier resets).

```cpp
class MinBlepTable {
    // Lifecycle (NOT real-time safe)
    void prepare(size_t oversamplingFactor = 64, size_t zeroCrossings = 8);

    // Table query (real-time safe, noexcept)
    [[nodiscard]] float sample(float subsampleOffset, size_t index) const noexcept;
    [[nodiscard]] size_t length() const noexcept;      // zeroCrossings * 2
    [[nodiscard]] bool isPrepared() const noexcept;

    // Nested correction buffer
    struct Residual {
        explicit Residual(const MinBlepTable& table);  // NOT real-time safe
        void addBlep(float subsampleOffset, float amplitude) noexcept;  // RT-safe
        [[nodiscard]] float consume() noexcept;                          // RT-safe
        void reset() noexcept;                                           // RT-safe
    };
};
```

| Parameter | Default | Effect |
|-----------|---------|--------|
| oversamplingFactor | 64 | Sub-sample resolution (higher = smoother interpolation) |
| zeroCrossings | 8 | Sinc lobes per side (higher = sharper cutoff, more memory) |

| Zero Crossings | Table Length | Alias Rejection | Memory |
|----------------|--------------|-----------------|--------|
| 8 | 16 samples | ~35 dB | ~4 KB |
| 16 | 32 samples | ~50 dB | ~8 KB |
| 32 | 64 samples | >50 dB | ~16 KB |

**Algorithm (prepare):**
1. Generate Blackman-windowed sinc (BLIT)
2. Minimum-phase transform via cepstral method (Oppenheim & Schafer)
3. Integrate to produce minBLEP (cumulative sum)
4. Normalize (scale last to 1.0, clamp first to 0.0)
5. Store as flat polyphase table

**Residual Usage Pattern:**
```cpp
// Initialize (once, at startup)
MinBlepTable table;
table.prepare(64, 8);
MinBlepTable::Residual residual(table);

// In audio processing loop:
for (size_t n = 0; n < numSamples; ++n) {
    float prevPhase = phase;
    phase += phaseInc;

    if (phase >= 1.0f) {
        phase -= 1.0f;
        // subsampleOffset = fractional samples past the discontinuity
        float subsampleOffset = phase / phaseInc;
        residual.addBlep(subsampleOffset, discontinuityAmplitude);
    }

    float naive = computeNaiveWaveform(phase);
    output[n] = naive + residual.consume();
}
```

**Key Behaviors:**
- `sample(0.0, 0)` returns exactly 0.0 (step hasn't started)
- `sample(0.0, length()-1)` returns exactly 1.0 (step complete)
- `sample(offset, index >= length())` returns 1.0 (settled value)
- `sample()` on unprepared table returns 0.0
- `addBlep()` with NaN/Inf amplitude is safely ignored (FR-037)
- `addBlamp()` with NaN/Inf amplitude is safely ignored
- Multiple Residual instances can share one MinBlepTable (read-only after prepare)
- `consume()` on empty buffer returns 0.0

**MinBLAMP Extension (since 0.16.0):**

MinBLAMP (band-limited ramp) corrects derivative discontinuities (kinks, direction reversals) rather than step discontinuities. The BLAMP table is generated automatically during `prepare()` by integrating the minBLEP residual.

```cpp
// Additional table query
[[nodiscard]] float sampleBlamp(float subsampleOffset, size_t index) const noexcept;

// Additional Residual method
void addBlamp(float subsampleOffset, float amplitude) noexcept;  // RT-safe
```

**When to use minBLAMP vs minBLEP:**
- **minBLEP** (`addBlep`): Step discontinuities -- waveform value jumps (hard sync reset, sawtooth wrap)
- **minBLAMP** (`addBlamp`): Derivative discontinuities -- slope changes sign (reverse sync direction reversal, triangle wave kinks)

**Subsample Offset Convention:**
- `subsampleOffset = 0.0`: Discontinuity at the exact sample boundary
- `subsampleOffset = 0.5`: Discontinuity occurred half a sample ago
- Computed as `phase / phaseInc` after wrap (fractional samples past the transition)

**Comparison with PolyBLEP:**

| Feature | MinBLEP | PolyBLEP |
|---------|---------|----------|
| Alias rejection | >50 dB (32 ZC) | ~40 dB |
| Memory | ~4-16 KB table | None (polynomial) |
| Per-discontinuity cost | Ring buffer stamp (N samples) | 2-4 sample correction |
| Minimum phase | Yes (energy front-loaded) | No (symmetric) |
| Best for | Sync, hard transitions | Standard waveforms |

**Related Primitives:** PolyBlepOscillator (complementary, lower quality/cost), FFT (used in table generation), WavetableOscillator (similar table lookup pattern)

**Dependencies:** `core/interpolation.h` (linearInterpolate), `core/math_constants.h` (kPi), `core/window_functions.h` (generateBlackman), `core/db_utils.h` (isNaN, isInf), `primitives/fft.h` (cepstral transform)

---

## PinkNoiseFilter
**Path:** [pink_noise_filter.h](../../dsp/include/krate/dsp/primitives/pink_noise_filter.h) | **Since:** 0.14.2

Paul Kellet's pink noise filter for converting white noise to pink noise (-3dB/octave).

```cpp
class PinkNoiseFilter {
    [[nodiscard]] float process(float white) noexcept;  // White in, pink out [-1, 1]
    void reset() noexcept;                              // Clear filter state
};
```

**Algorithm:** 7-state recursive filter with fixed coefficients. Accuracy: +/-0.05dB from 9.2Hz to Nyquist at 44.1kHz. Sample-rate independent across 44.1kHz-192kHz.

**When to use:**
- Pink noise generation in synthesis (excitation, modulation)
- Shared primitive used by both NoiseOscillator (Layer 1) and NoiseGenerator (Layer 2)

**Reference:** [Paul Kellet's Pink Noise Algorithm](https://www.firstpr.com.au/dsp/pink-noise/)

**Dependencies:** None (pure algorithm)

---

## NoiseOscillator
**Path:** [noise_oscillator.h](../../dsp/include/krate/dsp/primitives/noise_oscillator.h) | **Since:** 0.14.2

Lightweight noise oscillator providing six noise colors for synthesis-level composition.

```cpp
class NoiseOscillator {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;                              // Restart from seed
    void setColor(NoiseColor color) noexcept;           // White, Pink, Brown, Blue, Violet, Grey
    void setSeed(uint32_t seed) noexcept;               // Deterministic sequences
    [[nodiscard]] float process() noexcept;             // Single sample [-1, 1]
    void processBlock(float* output, size_t numSamples) noexcept;

    // Queries
    [[nodiscard]] NoiseColor color() const noexcept;
    [[nodiscard]] uint32_t seed() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
};
```

| Color | Slope | Algorithm |
|-------|-------|-----------|
| White | 0 dB/oct | Direct PRNG |
| Pink | -3 dB/oct | Paul Kellet filter |
| Brown | -6 dB/oct | Leaky integrator (leak=0.99) |
| Blue | +3 dB/oct | Differentiated pink |
| Violet | +6 dB/oct | Differentiated white |
| Grey | Inverse A-weight | Dual biquad shelf cascade |

**When to use:**
- Karplus-Strong string excitation
- LFO modulation source (noise-based randomness)
- Audio testing and calibration (grey noise)
- Synthesis-level noise generation

**NoiseOscillator vs NoiseGenerator:**
- **NoiseOscillator** (Layer 1): Lightweight primitive for oscillator composition, 6 colors
- **NoiseGenerator** (Layer 2): Effects-oriented with level control, smoothing, signal-dependent modulation, 13 noise types including tape hiss, vinyl crackle

**Dependencies:** `core/random.h` (Xorshift32), `core/pattern_freeze_types.h` (NoiseColor), `primitives/pink_noise_filter.h`, `primitives/biquad.h`

---

## ADSREnvelope (ADSR Envelope Generator)
**Path:** [adsr_envelope.h](../../dsp/include/krate/dsp/primitives/adsr_envelope.h) | **Since:** 0.15.0 | **Modified:** 0.18.0

Five-state ADSR envelope generator. Supports both discrete `EnvCurve` enum (backward compat) and continuous float curve amounts with 256-entry lookup tables (Spec 048).

```cpp
enum class ADSRStage : uint8_t { Idle, Attack, Decay, Sustain, Release };
enum class EnvCurve : uint8_t { Exponential, Linear, Logarithmic };
enum class RetriggerMode : uint8_t { Hard, Legato };

class ADSREnvelope {
    void prepare(float sampleRate) noexcept;
    void reset() noexcept;
    void gate(bool on) noexcept;

    // Parameter setters (NaN-safe, real-time safe)
    void setAttack(float ms) noexcept;       // 0.1 - 10000 ms
    void setDecay(float ms) noexcept;        // 0.1 - 10000 ms
    void setSustain(float level) noexcept;   // 0.0 - 1.0
    void setRelease(float ms) noexcept;      // 0.1 - 10000 ms

    // Curve setters (two overloads each)
    void setAttackCurve(EnvCurve curve) noexcept;      // Discrete enum (backward compat)
    void setAttackCurve(float curveAmount) noexcept;    // Continuous [-1, +1] (Spec 048)
    void setDecayCurve(EnvCurve curve) noexcept;
    void setDecayCurve(float curveAmount) noexcept;
    void setReleaseCurve(EnvCurve curve) noexcept;
    void setReleaseCurve(float curveAmount) noexcept;

    void setRetriggerMode(RetriggerMode mode) noexcept;
    void setVelocityScaling(bool enabled) noexcept;
    void setVelocity(float velocity) noexcept;

    // Processing
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Queries
    [[nodiscard]] ADSRStage getStage() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool isReleasing() const noexcept;
    [[nodiscard]] float getOutput() const noexcept;
};
```

**Curve Shaping (since 0.18.0):**

Each stage (attack, decay, release) has a 256-entry lookup table generated via `curve_table.h`. The `float` overload of `setAttackCurve()`/`setDecayCurve()`/`setReleaseCurve()` regenerates the table using `generatePowerCurveTable()`. The `EnvCurve` overload maps to float via `envCurveToCurveAmount()` for backward compatibility.

| curveAmount | Shape | Legacy Equivalent |
|-------------|-------|-------------------|
| -1.0 to -0.5 | Logarithmic | `EnvCurve::Logarithmic` (-0.7) |
| ~0.0 | Linear | `EnvCurve::Linear` (0.0) |
| +0.5 to +1.0 | Exponential | `EnvCurve::Exponential` (+0.7) |

**Per-sample cost:** 1 table lookup + 1 linear interpolation (1 multiply + 1 add). Same cost regardless of curve shape.

**When to use:**
- Synthesizer amplitude envelope (per-voice)
- Filter modulation source
- General-purpose envelope modulation

**Dependencies:** `core/curve_table.h` (table generation), `primitives/envelope_utils.h`, `core/db_utils.h` (`detail::isNaN()` for NaN-safe setters)

---

## Envelope Utilities
**Path:** [envelope_utils.h](../../dsp/include/krate/dsp/primitives/envelope_utils.h) | **Since:** 0.16.0

Shared envelope coefficient calculation utilities extracted from ADSREnvelope. Used by both ADSREnvelope and MultiStageEnvelope (Layer 2).

```cpp
// Constants
constexpr float kEnvelopeIdleThreshold = 1e-4f;
constexpr float kMinEnvelopeTimeMs = 0.1f;
constexpr float kMaxEnvelopeTimeMs = 10000.0f;
constexpr float kDefaultTargetRatioA = 0.3f;
constexpr float kDefaultTargetRatioDR = 0.0001f;
constexpr float kLinearTargetRatio = 100.0f;
constexpr float kSustainSmoothTimeMs = 5.0f;

// Enums (shared between all envelope types)
enum class EnvCurve : uint8_t { Exponential, Linear, Logarithmic };
enum class RetriggerMode : uint8_t { Hard, Legato };

// One-pole envelope coefficient computation (EarLevel Engineering method)
struct StageCoefficients { float coef; float base; };
[[nodiscard]] StageCoefficients calcEnvCoefficients(
    float timeMs, float sampleRate, float targetLevel,
    float targetRatio, bool rising) noexcept;

// Target ratio helpers
[[nodiscard]] float getAttackTargetRatio(EnvCurve curve) noexcept;
[[nodiscard]] float getDecayTargetRatio(EnvCurve curve) noexcept;
```

**When to use:**
- Building any envelope generator that uses one-pole coefficient calculation
- Sharing curve types and constants across envelope implementations
- Avoid duplicating the EarLevel Engineering coefficient formula

**Dependencies:** `core/db_utils.h` (for `ITERUM_NOINLINE`, `detail::flushDenormal()`)

---

## SpectralTransientDetector
**Path:** [spectral_transient_detector.h](../../dsp/include/krate/dsp/primitives/spectral_transient_detector.h) | **Since:** 0.18.0 | **Spec:** [062-spectral-transient-detector](../062-spectral-transient-detector/spec.md)

Spectral flux-based onset detection on magnitude spectra. Computes half-wave rectified spectral flux per frame (sum of positive magnitude differences between consecutive frames), compares against an adaptive threshold derived from an exponential moving average (EMA), and flags transient onsets. Used by `PhaseVocoderPitchShifter` for transient-aware phase reset.

**Use when:**
- Any spectral processor that needs onset/transient detection without phase information
- Triggering phase reset in phase vocoders to preserve transient sharpness
- Onset-aligned granular processing or spectral freeze effects
- Detecting drum hits, consonant attacks, or plucked string onsets in spectral domain

**Note:** The `detect()` path is real-time safe (no allocations, no exceptions, no locks, no I/O). All memory is allocated in `prepare()`. First `detect()` call after `prepare()` or `reset()` always returns `false` (first-frame suppression) but still seeds the running average.

```cpp
class SpectralTransientDetector {
    // Lifecycle
    void prepare(std::size_t numBins) noexcept;   // Allocate and reset (reallocates if bin count changes)
    void reset() noexcept;                         // Clear state, preserve threshold/smoothingCoeff

    // Detection (real-time safe)
    [[nodiscard]] bool detect(const float* magnitudes, std::size_t numBins) noexcept;

    // Configuration
    void setThreshold(float multiplier) noexcept;      // [1.0, 5.0] (default: 1.5)
    void setSmoothingCoeff(float coeff) noexcept;       // [0.8, 0.99] (default: 0.95)

    // Getters (values from most recent detect() call)
    [[nodiscard]] float getSpectralFlux() const noexcept;
    [[nodiscard]] float getRunningAverage() const noexcept;
    [[nodiscard]] bool isTransient() const noexcept;
};
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| threshold | 1.5 | [1.0, 5.0] | Multiplier on running average for detection. Lower = more sensitive. |
| smoothingCoeff | 0.95 | [0.8, 0.99] | EMA alpha. Higher = slower-moving average (more historical context). |

**Algorithm:**
1. Compute half-wave rectified spectral flux: `SF(n) = sum(max(0, mag[k] - prevMag[k]))` for all bins
2. Update EMA: `runningAvg = alpha * runningAvg + (1 - alpha) * SF`
3. Enforce floor of 1e-10 on running average (prevents instability after prolonged silence)
4. Detect transient: `SF > threshold * runningAvg` (suppressed on first frame)
5. Store current magnitudes as previous for next frame

**Supported bin counts:** 257 (512-point FFT), 513 (1024-point FFT), 1025 (2048-point FFT), 2049 (4096-point FFT), 4097 (8192-point FFT).

**Performance:** O(numBins) per frame with 3 arithmetic operations per bin (subtract, max, add). No transcendental math. Negligible overhead (< 0.01% CPU at 44.1kHz/4096-point FFT).

**Dependencies:** Standard library only (`<vector>`, `<cstddef>`, `<cmath>`, `<algorithm>`, `<cassert>`)

---

## PitchTracker
**Path:** [pitch_tracker.h](../../dsp/include/krate/dsp/primitives/pitch_tracker.h) | **Since:** 0.19.0 | **Spec:** [063-pitch-tracker](../063-pitch-tracker/spec.md)

5-stage post-processing wrapper around PitchDetector for stable harmonizer pitch input. Transforms raw, jittery pitch detection into stable MIDI note decisions by applying confidence gating, median filtering, hysteresis, minimum note duration, and frequency smoothing in a fixed pipeline order. Designed as the primary pitch input for the Phase 4 HarmonizerEngine.

**Processing pipeline (per internal analysis hop):**
```
pushBlock() -> [internal detect() per hop]
    [1] Confidence Gate (reject unreliable frames)
    [2] Median Filter (reject outliers from confident frames)
    [3] Hysteresis (prevent oscillation at note boundaries)
    [4] Min Note Duration (prevent rapid note-switching)
    [5] Frequency Smoother (smooth Hz output for pitch shifting)
```

```cpp
class PitchTracker {
    // Constants
    static constexpr std::size_t kDefaultWindowSize           = 256;
    static constexpr std::size_t kMaxMedianSize               = 11;
    static constexpr float       kDefaultHysteresisThreshold  = 50.0f;   // cents
    static constexpr float       kDefaultConfidenceThreshold  = 0.5f;
    static constexpr float       kDefaultMinNoteDurationMs    = 50.0f;
    static constexpr float       kDefaultFrequencySmoothingMs = 25.0f;

    // Lifecycle
    void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept;
    void reset() noexcept;

    // Processing (real-time safe, zero allocations)
    void pushBlock(const float* samples, std::size_t numSamples) noexcept;

    // Output queries
    [[nodiscard]] float getFrequency()  const noexcept;  // Stage 5: smoothed Hz
    [[nodiscard]] int   getMidiNote()   const noexcept;  // Stage 4: committed note (-1 = none)
    [[nodiscard]] float getConfidence() const noexcept;  // Raw from PitchDetector
    [[nodiscard]] bool  isPitchValid()  const noexcept;  // true iff last frame passed gate

    // Configuration (take effect on next hop)
    void setMedianFilterSize(std::size_t size) noexcept;     // [1, 11], default 5
    void setHysteresisThreshold(float cents) noexcept;       // >= 0, default 50
    void setConfidenceThreshold(float threshold) noexcept;   // [0, 1], default 0.5
    void setMinNoteDuration(float ms) noexcept;              // >= 0, default 50
};
```

**Use when:**
- Driving a diatonic harmonizer engine (Phase 4 HarmonizerEngine integration) that needs stable, non-warbling MIDI note input
- Any downstream processor that needs a stable integer MIDI note from monophonic audio
- Pitch-following modulation (e.g., pitch-to-filter-cutoff) where raw detector jitter would cause audible artifacts
- Pitch correction or display where single-frame outliers and boundary oscillation must be suppressed

**Key design decisions:**
- `getMidiNote()` returns the committed note from stage 4 directly; it is NOT derived from the smoothed frequency of stage 5
- First detection (no committed note) bypasses both hysteresis and minimum note duration for immediate musical responsiveness
- Low-confidence frames never enter the median ring buffer, preventing noise from contaminating the filter
- Median filter uses insertion sort on a fixed-size `std::array` scratch buffer (no heap allocation)
- `pushBlock()` internally triggers 0..N pipeline executions per call, matching PitchDetector's hop rhythm (`windowSize/4`)

**Usage example:**
```cpp
#include <krate/dsp/primitives/pitch_tracker.h>

Krate::DSP::PitchTracker tracker;

// In setupProcessing():
tracker.prepare(sampleRate, 256);

// Optional configuration:
tracker.setConfidenceThreshold(0.5f);
tracker.setHysteresisThreshold(50.0f);
tracker.setMinNoteDuration(50.0f);
tracker.setMedianFilterSize(5);

// In process() audio callback:
tracker.pushBlock(inputSamples, numSamples);

if (tracker.isPitchValid()) {
    int midiNote = tracker.getMidiNote();    // Committed, stable note
    float freq = tracker.getFrequency();      // Smoothed Hz for pitch shift
    float conf = tracker.getConfidence();     // Raw detector confidence
}
```

| Parameter | Default | Range | Effect |
|-----------|---------|-------|--------|
| medianFilterSize | 5 | [1, 11] | Larger = more outlier rejection, slightly more latency |
| hysteresisThreshold | 50 cents | [0, +inf) | Larger = more stable notes, less responsive to small changes |
| confidenceThreshold | 0.5 | [0, 1] | Higher = fewer accepted frames, more conservative |
| minNoteDuration | 50 ms | [0, +inf) | Longer = fewer note switches, less warbling |
| frequencySmoothing | 25 ms | (internal) | OnePoleSmoother time constant for Hz output |

**Performance:** Pipeline stages are pure arithmetic (no transcendental math except `std::round` and `std::abs`). Incremental CPU overhead beyond PitchDetector is < 0.1% at 44.1kHz (Layer 1 budget).

**Dependencies:** `PitchDetector` (L1), `OnePoleSmoother` (L1), `pitch_utils.h` (L0, `frequencyToMidiNote()`), `midi_utils.h` (L0, `midiNoteToFrequency()`)

---

## HeldNoteBuffer (Arpeggiator Note Tracking)
**Path:** [held_note_buffer.h](../../dsp/include/krate/dsp/primitives/held_note_buffer.h) | **Since:** 0.19.0 | **Spec:** [069-held-note-buffer](../069-held-note-buffer/spec.md)

Fixed-capacity (32-note), heap-free buffer tracking currently held MIDI notes for arpeggiator pattern generation. Maintains two parallel views: pitch-sorted (ascending MIDI note number) for directional arp modes, and insertion-ordered (chronological noteOn order) for AsPlayed mode. Uses a monotonically increasing insertion counter for chronological ordering.

**Use when:**
- Building an arpeggiator that needs to track held MIDI notes with multiple sort orders
- Phase 2 `ArpeggiatorCore` (Layer 3) will compose `HeldNoteBuffer` + `NoteSelector` + `SequencerCore`
- Ruinae Processor (Phase 3) will own a `HeldNoteBuffer` and feed it MIDI events from the host

**Note on ODR safety:** The names `HeldNote`, `HeldNoteBuffer`, `ArpMode`, `OctaveMode`, `ArpNoteResult`, and `NoteSelector` are unique within the `Krate::DSP` namespace. Before creating any new class with a similar name, search the codebase to avoid ODR violations.

**Types provided:**

```cpp
/// Single held MIDI note with insertion-order tracking.
struct HeldNote {
    uint8_t note{0};          // MIDI note number (0-127)
    uint8_t velocity{0};      // MIDI velocity (1-127; 0 never stored)
    uint16_t insertOrder{0};  // Monotonically increasing counter
};

/// Arpeggiator pattern mode (10 modes).
enum class ArpMode : uint8_t {
    Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord
};

/// Octave expansion ordering mode.
enum class OctaveMode : uint8_t {
    Sequential,   // Complete pattern at each octave before advancing
    Interleaved   // Each note at all octave transpositions before next note
};

/// Result of NoteSelector::advance(). Fixed-capacity, no heap allocation.
struct ArpNoteResult {
    std::array<uint8_t, 32> notes{};       // MIDI note numbers (with octave offset applied)
    std::array<uint8_t, 32> velocities{};  // Corresponding velocities
    size_t count{0};                        // Number of valid entries (0 = empty, 1 = single, N = chord)
};
```

**HeldNoteBuffer public API:**

```cpp
class HeldNoteBuffer {
    static constexpr size_t kMaxNotes = 32;

    void noteOn(uint8_t note, uint8_t velocity) noexcept;  // Add/update note
    void noteOff(uint8_t note) noexcept;                    // Remove note (silently ignores unknown)
    void clear() noexcept;                                   // Remove all notes, reset insert counter
    [[nodiscard]] size_t size() const noexcept;              // Number of held notes
    [[nodiscard]] bool empty() const noexcept;               // True if no notes held
    [[nodiscard]] std::span<const HeldNote> byPitch() const noexcept;        // Pitch-ascending view
    [[nodiscard]] std::span<const HeldNote> byInsertOrder() const noexcept;  // Chronological view
};
```

| Operation | Complexity | Description |
|-----------|------------|-------------|
| `noteOn` (new) | O(N) | Linear scan for duplicate check + insertion sort into pitch-sorted array |
| `noteOn` (update) | O(N) | Linear scan to find + update velocity in both arrays |
| `noteOff` | O(N) | Linear scan + shift-left removal from both arrays |
| `clear` | O(1) | Reset size and counter |
| `byPitch` / `byInsertOrder` | O(1) | Return `std::span` view over internal array |

**Key behaviors:**
- Duplicate `noteOn` updates velocity without creating a second entry
- `noteOn` when full (32 notes) silently ignores the new note
- `noteOff` for a note not in the buffer is silently ignored
- `clear()` resets the insertion order counter to 0
- Zero heap allocation (SC-003) -- all storage is `std::array`

**Dependencies:** `<array>`, `<cstdint>`, `<cstddef>`, `<span>`, `<algorithm>`

---

## NoteSelector (Arpeggiator Note Selection Engine)
**Path:** [held_note_buffer.h](../../dsp/include/krate/dsp/primitives/held_note_buffer.h) | **Since:** 0.19.0 | **Spec:** [069-held-note-buffer](../069-held-note-buffer/spec.md)

Stateful traversal engine for arpeggiator note selection. Implements all 10 `ArpMode` patterns with octave range expansion (1-4 octaves) in both Sequential and Interleaved ordering. Receives a `const HeldNoteBuffer&` on each `advance()` call and produces the next note(s) to play. Holds no internal reference to any buffer.

**Use when:**
- Phase 2 `ArpeggiatorCore` (Layer 3) will call `advance()` once per arp step tick
- Any arpeggiator that needs configurable pattern traversal with octave expansion
- Testing arp patterns independently of timing/sequencing logic

**Note on ODR safety:** `NoteSelector` is defined in the same header as `HeldNoteBuffer`. Both are unique names within `Krate::DSP`.

```cpp
class NoteSelector {
    // Construction
    explicit NoteSelector(uint32_t seed = 1) noexcept;  // PRNG seed for Random/Walk

    // Configuration
    void setMode(ArpMode mode) noexcept;                // Sets mode and calls reset()
    void setOctaveRange(int octaves) noexcept;           // [1, 4], clamped
    void setOctaveMode(OctaveMode mode) noexcept;        // Sequential or Interleaved

    // Pattern traversal
    [[nodiscard]] ArpNoteResult advance(const HeldNoteBuffer& held) noexcept;

    // Reset to beginning of pattern (all mode state cleared)
    void reset() noexcept;
};
```

| Mode | Pattern (notes C3, E3, G3) | Description |
|------|---------------------------|-------------|
| Up | 60, 64, 67, 60, 64, 67, ... | Ascending pitch, wrap at top |
| Down | 67, 64, 60, 67, 64, 60, ... | Descending pitch, wrap at bottom |
| UpDown | 60, 64, 67, 64, 60, 64, ... | Ping-pong, no endpoint repeat |
| DownUp | 67, 64, 60, 64, 67, 64, ... | Ping-pong descending first |
| Converge | 60, 67, 64, 60, 67, 64, ... | Outside-in alternation |
| Diverge | 64, 60, 67, 64, 60, 67, ... | Center outward (odd count) |
| Random | uniform random selection | Xorshift32 PRNG, seeded |
| Walk | random +/-1 step, clamped | Xorshift32 PRNG, bounded |
| AsPlayed | insertion order (chronological) | Uses `byInsertOrder()` view |
| Chord | all notes simultaneously | `count = N`, no octave transposition |

| Octave Mode | Example (Up, [C3, E3], range 3) | Description |
|-------------|--------------------------------|-------------|
| Sequential | 60, 64, 72, 76, 84, 88 | Complete pattern at each octave |
| Interleaved | 60, 72, 84, 64, 76, 88 | Each note at all octaves before next |

**Key behaviors:**
- Empty buffer returns `ArpNoteResult` with `count == 0` for all modes
- Index clamping on buffer mutation: if notes are removed mid-pattern, the internal index is clamped to `[0, size-1]` before the next access
- `reset()` clears all internal state: `noteIndex_`, `pingPongPos_`, `octaveOffset_`, `walkIndex_`, `convergeStep_`, `direction_`
- MIDI note clamping (FR-028): octave transposition clamps result to 127 maximum
- Chord mode ignores octave range (FR-020)
- Single note held: all modes return that note (except Chord which returns `count == 1`)
- Zero heap allocation (SC-003) -- `ArpNoteResult` uses `std::array<uint8_t, 32>`

**Layer 0 dependency:** Uses `Xorshift32` from `core/random.h` for deterministic random selection in Random and Walk modes.

**Dependencies:** `core/random.h` (Xorshift32), `<algorithm>` (std::min, std::clamp), `<span>`

---

## ArpLane<T, MaxSteps>
**Path:** [arp_lane.h](../../dsp/include/krate/dsp/primitives/arp_lane.h) | **Since:** 0.20.0 | **Spec:** [072-independent-lanes](../072-independent-lanes/spec.md)

Fixed-capacity step lane for arpeggiator polymetric patterns. Stores up to `MaxSteps` values of type `T` with independent position tracking. Each lane advances its own counter on every arp step, enabling polymetric rhythms when lanes have coprime lengths (e.g., velocity=3, gate=5, pitch=7 produces a combined pattern that repeats every LCM=105 steps).

**Use when:**
- You need a fixed-capacity, zero-allocation, step-advancing container with independent cycling for arpeggiator lane patterns
- Building polymetric step sequencer lanes that cycle at different lengths
- Any per-step modulation source that must advance independently of the main arp pattern

```cpp
template <typename T, size_t MaxSteps = 32>
class ArpLane {
    // Length control
    void setLength(size_t len) noexcept;           // Clamp to [1, MaxSteps]; wraps position to 0 if >= new length
    [[nodiscard]] size_t length() const noexcept;

    // Step value access
    void setStep(size_t index, T value) noexcept;  // Index clamped to [0, length_-1]
    [[nodiscard]] T getStep(size_t index) const noexcept;  // Returns T{} if index >= length_

    // Playback
    [[nodiscard]] T advance() noexcept;            // Returns current step value, then advances position
    void reset() noexcept;                          // Resets position to 0
    [[nodiscard]] size_t currentStep() const noexcept;
};
```

**Supported types:**
| Type | Lane | Value Range | Default | Identity Value |
|------|------|-------------|---------|----------------|
| `float` | Velocity | 0.0-1.0 | 1.0 | 1.0 (no scaling) |
| `float` | Gate | 0.01-2.0 | 1.0 | 1.0 (no multiplier) |
| `int8_t` | Pitch | -24 to +24 | 0 | 0 (no offset) |
| `uint8_t` | (planned) | varies | 0 | Phases 5-8: modifiers, ratchet, conditions |

**Key behaviors:**
- `std::array<T, MaxSteps>` backing store -- zero heap allocation, real-time safe
- Default construction: `length_ = 1`, `position_ = 0`, all steps value-initialized to `T{}`
- `setLength()` clamps to `[1, MaxSteps]`; if current position >= new length, position wraps to 0
- `advance()` returns `steps_[position_]` then increments position modulo length
- `setStep()` clamps index to `[0, length_-1]` (writes to highest valid index if out of range)
- `getStep()` returns `T{}` if the original index >= length (safe default for unset steps)
- All methods are `noexcept` -- no exceptions, no allocations, no I/O

**Memory:** `sizeof(std::array<T, MaxSteps>) + 2 * sizeof(size_t)` per instance (~34-48 bytes for `ArpLane<float, 32>` with padding). Header-only, real-time safe, single-threaded.

**Dependencies:** `<array>`, `<cstddef>`, `<cstdint>`, `<algorithm>` (std::min, std::clamp)
