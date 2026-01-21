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

## Allpass1Pole (First-Order Allpass Filter)
**Path:** [allpass_1pole.h](../../dsp/include/krate/dsp/primitives/allpass_1pole.h) | **Since:** 0.13.0

First-order allpass filter for frequency-dependent phase shifting. Provides unity magnitude response with phase shift from 0 degrees (DC) to -180 degrees (Nyquist).

**Use when:**
- Building phaser effects (cascade 2-12 stages with LFO modulation)
- Phase correction in crossover networks
- Any application requiring phase shift without magnitude change

```cpp
class Allpass1Pole {
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
