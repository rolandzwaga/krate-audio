# Krate Audio Architecture

Living inventory of components and APIs. Reference before writing specs to avoid duplication.

> **Constitution Principle XIII**: Every spec implementation MUST update this document.

**Last Updated**: 2026-01-12 | **Namespace**: `Krate::DSP` | **Include**: `<krate/dsp/...>`

## Repository Structure

```
dsp/                          # Shared KrateDSP library
├── include/krate/dsp/        # Public headers
│   ├── core/                 # Layer 0: Utilities
│   ├── primitives/           # Layer 1: DSP primitives
│   ├── processors/           # Layer 2: Processors
│   ├── systems/              # Layer 3: Systems
│   └── effects/              # Layer 4: Features
└── tests/                    # DSP tests

plugins/iterum/               # Iterum delay plugin
├── src/                      # Plugin source
├── tests/                    # Plugin tests
└── resources/                # UI, presets
```

## Layer Dependency Rules

```
Layer 4: User Features     ← composes layers 0-3
Layer 3: System Components ← composes layers 0-2
Layer 2: DSP Processors    ← uses layers 0-1
Layer 1: DSP Primitives    ← uses layer 0 only
Layer 0: Core Utilities    ← stdlib only, no DSP deps
```

---

## Layer 0: Core Utilities

### dB/Linear Conversion
**Path:** [db_utils.h](dsp/include/krate/dsp/core/db_utils.h) • **Since:** 0.0.1

```cpp
constexpr float kSilenceFloorDb = -144.0f;
[[nodiscard]] constexpr float dbToGain(float dB) noexcept;      // 0dB→1.0, -20dB→0.1, NaN→0.0
[[nodiscard]] constexpr float gainToDb(float gain) noexcept;    // 1.0→0dB, 0.0→-144dB
```

### Mathematical Constants
**Path:** [dsp_utils.h](dsp/include/krate/dsp/core/dsp_utils.h) • **Since:** 0.0.0

```cpp
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
```

### Window Functions
**Path:** [window_functions.h](dsp/include/krate/dsp/core/window_functions.h) • **Since:** 0.0.7

```cpp
enum class WindowType : uint8_t { Hann, Hamming, Blackman, Kaiser };

namespace Window {
    void generateHann(float* output, size_t size) noexcept;
    void generateHamming(float* output, size_t size) noexcept;
    void generateBlackman(float* output, size_t size) noexcept;
    void generateKaiser(float* output, size_t size, float beta) noexcept;
    [[nodiscard]] bool verifyCOLA(const float* window, size_t size, size_t hopSize, float tolerance = 1e-6f) noexcept;
    [[nodiscard]] std::vector<float> generate(WindowType type, size_t size, float kaiserBeta = 9.0f);
}
```

| Window | Sidelobes | COLA Overlap | Best For |
|--------|-----------|--------------|----------|
| Hann | -31dB | 50%/75% | General STFT |
| Hamming | -42dB | 50%/75% | Better sidelobe rejection |
| Blackman | -58dB | 75%+ | Low-leakage analysis |
| Kaiser (β=9) | -80dB | 90%+ | Precision analysis |

### Xorshift32 PRNG
**Path:** [random.h](dsp/include/krate/dsp/core/random.h) • **Since:** 0.0.14

Fast, deterministic RNG for audio noise. Period: 2³²-1.

```cpp
class Xorshift32 {
    explicit constexpr Xorshift32(uint32_t seed = kDefaultSeed) noexcept;
    [[nodiscard]] constexpr uint32_t next() noexcept;       // Raw 32-bit
    [[nodiscard]] constexpr float nextFloat() noexcept;     // [-1, 1] bipolar
    [[nodiscard]] constexpr float nextUnipolar() noexcept;  // [0, 1]
    constexpr void seed(uint32_t value) noexcept;
};
```

### NoteValue & Tempo Sync
**Path:** [note_value.h](dsp/include/krate/dsp/core/note_value.h) • **Since:** 0.0.17

```cpp
enum class NoteValue : uint8_t { DoubleWhole, Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond, SixtyFourth };
enum class NoteModifier : uint8_t { None, Dotted, Triplet };

[[nodiscard]] constexpr float getBeatsForNote(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
[[nodiscard]] constexpr float noteToDelayMs(NoteValue note, NoteModifier modifier, double tempoBPM) noexcept;
[[nodiscard]] constexpr float dropdownToDelayMs(int dropdownIndex, double tempoBPM) noexcept;
```

**Dropdown mapping (0-9):** 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1

### BlockContext
**Path:** [block_context.h](dsp/include/krate/dsp/core/block_context.h) • **Since:** 0.0.17

Per-block processing context for tempo-synced DSP.

```cpp
struct BlockContext {
    double sampleRate = 44100.0;
    size_t blockSize = 512;
    double tempoBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    bool isPlaying = false;
    int64_t transportPositionSamples = 0;

    [[nodiscard]] constexpr double tempoToSamples(NoteValue value, NoteModifier modifier = NoteModifier::None) const noexcept;
};
```

### FastMath
**Path:** [fast_math.h](dsp/include/krate/dsp/core/fast_math.h) • **Since:** 0.0.17

```cpp
namespace FastMath {
    [[nodiscard]] constexpr float fastTanh(float x) noexcept;  // Padé (5,4), ~3x faster than std::tanh
}
```

**Recommendations:** Use `fastTanh()` for saturation/waveshaping (3x speedup). Use `std::sin/cos/exp` for LFOs and filters (MSVC already optimized).

### Sigmoid Transfer Functions
**Path:** [sigmoid.h](dsp/include/krate/dsp/core/sigmoid.h) • **Since:** 0.10.0

Unified library of sigmoid (soft-clipping) transfer functions for audio saturation.

```cpp
namespace Sigmoid {
    // Symmetric functions (odd harmonics only)
    [[nodiscard]] constexpr float tanh(float x) noexcept;           // Wraps FastMath::fastTanh
    [[nodiscard]] constexpr float tanhVariable(float x, float drive) noexcept;  // Variable drive
    [[nodiscard]] inline float atan(float x) noexcept;              // (2/π)*atan(x)
    [[nodiscard]] inline float atanVariable(float x, float drive) noexcept;
    [[nodiscard]] inline float softClipCubic(float x) noexcept;     // 1.5x - 0.5x³
    [[nodiscard]] inline float softClipQuintic(float x) noexcept;   // 5th-order Legendre
    [[nodiscard]] inline float recipSqrt(float x) noexcept;         // x/sqrt(x²+1), ~5x faster
    [[nodiscard]] inline float erf(float x) noexcept;               // Wraps std::erf
    [[nodiscard]] inline float erfApprox(float x) noexcept;         // Abramowitz-Stegun, <0.1% error
    [[nodiscard]] inline constexpr float hardClip(float x, float threshold = 1.0f) noexcept;
}

namespace Asymmetric {
    // Asymmetric functions (even + odd harmonics)
    [[nodiscard]] inline float tube(float x) noexcept;              // x + 0.3x² - 0.15x³, tanh limited
    [[nodiscard]] inline float diode(float x) noexcept;             // Forward/reverse bias modeling
    template<typename Func>
    [[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept;  // DC bias (caller DC-blocks)
    [[nodiscard]] inline float dualCurve(float x, float posGain, float negGain) noexcept;  // Gains clamped >= 0
}
```

| Function | Speed vs std::tanh | Harmonics | Best For |
|----------|-------------------|-----------|----------|
| `Sigmoid::tanh` | 3x faster | Odd only | General saturation |
| `Sigmoid::recipSqrt` | 5x faster | Odd only | CPU-critical paths |
| `Sigmoid::softClipCubic` | 8x faster | Odd (3rd dominant) | Gentle limiting |
| `Sigmoid::erf` | 1x (wraps std) | Odd + nulls | Tape character |
| `Asymmetric::tube` | 3x faster | Even + Odd | Tube warmth |
| `Asymmetric::diode` | ~1x | Even + Odd | Subtle asymmetry |

**When to use:**
- **Variable drive:** Use `tanhVariable(x, drive)` for "drive knob" UI control
- **Performance critical:** Use `recipSqrt()` for 5x speedup over tanh
- **Tube warmth:** Use `Asymmetric::tube()` for 2nd harmonic richness
- **Tape character:** Use `erf()` for characteristic spectral nulls

### Interpolation Utilities
**Path:** [interpolation.h](dsp/include/krate/dsp/core/interpolation.h) • **Since:** 0.0.17

```cpp
namespace Interpolation {
    [[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept;
    [[nodiscard]] constexpr float cubicHermiteInterpolate(float y_m1, float y0, float y1, float y2, float t) noexcept;
    [[nodiscard]] constexpr float lagrangeInterpolate(float y_m1, float y0, float y1, float y2, float t) noexcept;
}
```

| Method | Points | Use Case |
|--------|--------|----------|
| Linear | 2 | Modulated delay (chorus/flanger) |
| Cubic Hermite | 4 | Pitch shifting, resampling |
| Lagrange | 4 | High-quality transposition |

### Stereo Cross-Blend
**Path:** [stereo_utils.h](dsp/include/krate/dsp/core/stereo_utils.h) • **Since:** 0.0.19

```cpp
constexpr void stereoCrossBlend(float inL, float inR, float crossAmount, float& outL, float& outR) noexcept;
// crossAmount: 0.0=normal, 0.5=mono, 1.0=swap (ping-pong)
```

### Pitch Utilities
**Path:** [pitch_utils.h](dsp/include/krate/dsp/core/pitch_utils.h) • **Since:** 0.0.35

```cpp
[[nodiscard]] constexpr float semitonesToRatio(float semitones) noexcept;  // 12→2.0, -12→0.5
[[nodiscard]] constexpr float ratioToSemitones(float ratio) noexcept;      // 2.0→12, 0.5→-12
```

### Grain Envelope
**Path:** [grain_envelope.h](dsp/include/krate/dsp/core/grain_envelope.h) • **Since:** 0.0.35

```cpp
enum class GrainEnvelopeType : uint8_t { Hann, Trapezoid, Sine, Blackman };

class GrainEnvelope {
    static void generate(float* buffer, size_t size, GrainEnvelopeType type) noexcept;
    static float lookup(const float* table, size_t tableSize, float phase) noexcept;
};
```

### Crossfade Utilities
**Path:** [crossfade_utils.h](dsp/include/krate/dsp/core/crossfade_utils.h) • **Since:** 0.0.41

```cpp
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept;
[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept;
// position: 0.0=full fadeOut, 0.5=equal blend, 1.0=full fadeIn
```

### Chebyshev Polynomials
**Path:** [chebyshev.h](dsp/include/krate/dsp/core/chebyshev.h) • **Since:** 0.10.0

Chebyshev polynomials of the first kind for harmonic control in waveshaping.
Key property: `T_n(cos(theta)) = cos(n*theta)` - produces pure nth harmonic from sine wave input.

```cpp
namespace Chebyshev {
    constexpr int kMaxHarmonics = 32;

    // Individual polynomials T1-T8 (Horner's method)
    [[nodiscard]] constexpr float T1(float x) noexcept;  // x (fundamental)
    [[nodiscard]] constexpr float T2(float x) noexcept;  // 2x^2 - 1 (2nd harmonic)
    [[nodiscard]] constexpr float T3(float x) noexcept;  // 4x^3 - 3x (3rd harmonic)
    [[nodiscard]] constexpr float T4(float x) noexcept;  // 8x^4 - 8x^2 + 1
    [[nodiscard]] constexpr float T5(float x) noexcept;  // 16x^5 - 20x^3 + 5x
    [[nodiscard]] constexpr float T6(float x) noexcept;  // 32x^6 - 48x^4 + 18x^2 - 1
    [[nodiscard]] constexpr float T7(float x) noexcept;  // 64x^7 - 112x^5 + 56x^3 - 7x
    [[nodiscard]] constexpr float T8(float x) noexcept;  // 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1

    // Arbitrary order (recurrence relation)
    [[nodiscard]] constexpr float Tn(float x, int n) noexcept;  // T_n = 2x*T_{n-1} - T_{n-2}

    // Weighted sum (Clenshaw algorithm)
    [[nodiscard]] inline float harmonicMix(float x, const float* weights, std::size_t numHarmonics) noexcept;
    // weights[0]=T1, weights[1]=T2, ..., weights[n-1]=Tn
}
```

| Function | Use Case |
|----------|----------|
| `T1-T8` | Direct harmonic generation (compile-time) |
| `Tn(x, n)` | Dynamic order selection at runtime |
| `harmonicMix` | Custom harmonic spectra (tube amp, exciter) |

**When to use:**
- **Tube amp modeling:** Use `harmonicMix()` with emphasis on T2/T3 for even/odd harmonic character
- **Exciter/enhancer:** Blend higher harmonics (T5-T8) at low levels
- **Waveshaping design:** Combine with `Sigmoid::` functions for complex transfer curves

### Wavefolding Math Functions
**Path:** [wavefold_math.h](dsp/include/krate/dsp/core/wavefold_math.h) • **Since:** 0.10.0

Pure mathematical functions for wavefolding algorithms. Three fundamental approaches with different harmonic characters.

```cpp
namespace WavefoldMath {
    constexpr float kMinThreshold = 0.01f;                    // Triangle fold minimum
    constexpr float kLambertWDomainMin = -0.36787944117144233f;  // -1/e
    constexpr float kSineFoldGainEpsilon = 0.001f;            // Passthrough threshold

    // Lambert W function - Lockhart wavefolder foundation
    [[nodiscard]] inline float lambertW(float x) noexcept;        // Principal branch W0, 4 iterations
    [[nodiscard]] inline float lambertWApprox(float x) noexcept;  // Fast version, 1 iteration

    // Triangle fold - geometric wavefolding
    [[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept;

    // Sine fold - Serge synthesizer style
    [[nodiscard]] inline float sineFold(float x, float gain) noexcept;
}
```

| Function | Character | Use Case |
|----------|-----------|----------|
| `lambertW` | Rich even/odd harmonics, nulls | Lockhart wavefolder circuits |
| `lambertWApprox` | Same, ~3x faster | Real-time when precision not critical |
| `triangleFold` | Dense odd harmonics, smooth rolloff | Guitar effects, general distortion |
| `sineFold` | FM-like sparse spectrum, Bessel distribution | Serge wavefolder emulation |

**When to use:**
- **Lockhart wavefolder:** Use `lambertW()` for precise transfer function design
- **Serge modular emulation:** Use `sineFold()` with gain ~pi for characteristic sound
- **General distortion:** Use `triangleFold()` for predictable, symmetric clipping

---

## Layer 1: DSP Primitives

### DelayLine
**Path:** [delay_line.h](dsp/include/krate/dsp/primitives/delay_line.h) • **Since:** 0.0.2

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

### CrossfadingDelayLine
**Path:** [crossfading_delay_line.h](dsp/include/krate/dsp/primitives/crossfading_delay_line.h) • **Since:** 0.0.39

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

### LFO (Low-Frequency Oscillator)
**Path:** [lfo.h](dsp/include/krate/dsp/primitives/lfo.h) • **Since:** 0.0.3

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

### OnePoleSmoother
**Path:** [one_pole_smoother.h](dsp/include/krate/dsp/primitives/one_pole_smoother.h) • **Since:** 0.0.4

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

### Biquad Filter
**Path:** [biquad.h](dsp/include/krate/dsp/primitives/biquad.h) • **Since:** 0.0.5

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

### Oversampler
**Path:** [oversampler.h](dsp/include/krate/dsp/primitives/oversampler.h) • **Since:** 0.0.6

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

### FFT
**Path:** [fft.h](dsp/include/krate/dsp/primitives/fft.h) • **Since:** 0.0.7

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

### STFT
**Path:** [stft.h](dsp/include/krate/dsp/primitives/stft.h) • **Since:** 0.0.8

Short-Time Fourier Transform with overlap-add.

```cpp
class STFT {
    void prepare(size_t fftSize, size_t hopSize, WindowType windowType) noexcept;
    void process(const float* input, size_t numSamples, STFTCallback callback) noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;  // fftSize samples
};
```

### AllpassFilter
**Path:** [allpass_filter.h](dsp/include/krate/dsp/primitives/allpass_filter.h) • **Since:** 0.0.9

First-order allpass for diffusion and dispersion.

```cpp
class AllpassFilter {
    void prepare(size_t maxDelaySamples) noexcept;
    void setDelay(size_t samples) noexcept;
    void setCoefficient(float g) noexcept;  // [-1, 1]
    [[nodiscard]] float process(float input) noexcept;
};
```

### Comb Filters
**Path:** [comb_filter.h](dsp/include/krate/dsp/primitives/comb_filter.h) • **Since:** 0.0.9

```cpp
class FeedbackCombFilter {
    void prepare(size_t maxDelaySamples) noexcept;
    void setDelay(size_t samples) noexcept;
    void setFeedback(float g) noexcept;
    [[nodiscard]] float process(float input) noexcept;
};

class FeedforwardCombFilter { /* Same API, feedforward topology */ };
```

### Sample Rate Converter
**Path:** [sample_rate_converter.h](dsp/include/krate/dsp/primitives/sample_rate_converter.h) • **Since:** 0.0.35

Variable-rate playback for granular synthesis.

```cpp
class SampleRateConverter {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    [[nodiscard]] float process(float* buffer, size_t bufferSize, float& readPosition, float rate) noexcept;
    void setInterpolationQuality(InterpolationQuality quality) noexcept;
};
```

### DCBlocker
**Path:** [dc_blocker.h](dsp/include/krate/dsp/primitives/dc_blocker.h) • **Since:** 0.10.0

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

## Layer 2: DSP Processors

### EnvelopeFollower
**Path:** [envelope_follower.h](dsp/include/krate/dsp/processors/envelope_follower.h) • **Since:** 0.0.10

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

### Saturation
**Path:** [saturation.h](dsp/include/krate/dsp/processors/saturation.h) • **Since:** 0.0.11

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

### DynamicsProcessor
**Path:** [dynamics_processor.h](dsp/include/krate/dsp/processors/dynamics_processor.h) • **Since:** 0.0.12

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

### DuckingProcessor
**Path:** [ducking_processor.h](dsp/include/krate/dsp/processors/ducking_processor.h) • **Since:** 0.0.13

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

### NoiseGenerator
**Path:** [noise_generator.h](dsp/include/krate/dsp/processors/noise_generator.h) • **Since:** 0.0.14

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

### PitchShifter
**Path:** [pitch_shifter.h](dsp/include/krate/dsp/processors/pitch_shifter.h) • **Since:** 0.0.15

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

### Diffuser
**Path:** [diffuser.h](dsp/include/krate/dsp/processors/diffuser.h) • **Since:** 0.0.16

Multi-stage allpass network for reverb density.

```cpp
class Diffuser {
    void prepare(double sampleRate) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void setDiffusion(float amount) noexcept;  // 0-1
    void setDecay(float time) noexcept;
};
```

### WowFlutter
**Path:** [wow_flutter.h](dsp/include/krate/dsp/processors/wow_flutter.h) • **Since:** 0.0.23

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

### TiltEQ
**Path:** [tilt_eq.h](dsp/include/krate/dsp/processors/tilt_eq.h) • **Since:** 0.0.40

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

## Layer 3: System Components

### DelayEngine
**Path:** [delay_engine.h](dsp/include/krate/dsp/systems/delay_engine.h) • **Since:** 0.0.18

Core delay system with tempo sync and modulation routing.

```cpp
class DelayEngine {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void setDelayTime(float ms) noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setModulationSource(ModulationSource source) noexcept;
    void setModulationDepth(float depth) noexcept;
};
```

### FeedbackNetwork
**Path:** [feedback_network.h](dsp/include/krate/dsp/systems/feedback_network.h) • **Since:** 0.0.19

Stereo feedback routing with filtering and saturation.

```cpp
class FeedbackNetwork {
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void process(float& left, float& right, float inputL, float inputR) noexcept;
    void pushToDelay(float left, float right) noexcept;
    void setFeedback(float amount) noexcept;       // 0-1.2
    void setCrossAmount(float amount) noexcept;    // 0=dual mono, 1=ping-pong
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterType(FilterType type) noexcept;
    void setSaturationEnabled(bool enabled) noexcept;
    void setSaturationDrive(float dB) noexcept;
};
```

### ModulationMatrix
**Path:** [modulation_matrix.h](dsp/include/krate/dsp/systems/modulation_matrix.h) • **Since:** 0.0.20

Flexible modulation source → destination routing.

```cpp
class ModulationMatrix {
    void prepare(double sampleRate) noexcept;
    void setConnection(ModSource source, ModDest dest, float depth) noexcept;
    [[nodiscard]] float getModulation(ModDest dest) const noexcept;
    void process() noexcept;  // Update all sources, compute destinations
};
```

### CharacterProcessor
**Path:** [character_processor.h](dsp/include/krate/dsp/systems/character_processor.h) • **Since:** 0.0.21

Analog character coloration.

```cpp
enum class CharacterMode : uint8_t { Clean, Tape, BBD, DigitalVintage };

class CharacterProcessor {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setMode(CharacterMode mode) noexcept;
    void setAge(float amount) noexcept;  // 0-1 degradation
};
```

### TapManager
**Path:** [tap_manager.h](dsp/include/krate/dsp/systems/tap_manager.h) • **Since:** 0.0.22

Multi-tap delay management with filtering and panning.

```cpp
class TapManager {
    void prepare(double sampleRate, size_t numTaps, float maxDelayMs) noexcept;
    void setTapTime(size_t tap, float ms) noexcept;
    void setTapLevel(size_t tap, float dB) noexcept;
    void setTapPan(size_t tap, float pan) noexcept;  // -1 to +1
    void setTapFilterCutoff(size_t tap, float hz) noexcept;
    void setTapEnabled(size_t tap, bool enabled) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
};
```

### GrainCloud
**Path:** [grain_cloud.h](dsp/include/krate/dsp/systems/grain_cloud.h) • **Since:** 0.0.35

Polyphonic grain management for granular synthesis.

```cpp
class GrainCloud {
    void prepare(double sampleRate, size_t maxGrains, size_t maxGrainSamples) noexcept;
    void process(float* left, float* right, size_t numSamples) noexcept;
    void setGrainSize(float ms) noexcept;
    void setGrainDensity(float grainsPerSecond) noexcept;
    void setGrainPitch(float semitones) noexcept;
    void setPositionSpread(float amount) noexcept;
    void setPitchSpread(float semitones) noexcept;
    void setEnvelopeType(GrainEnvelopeType type) noexcept;
};
```

---

## Layer 4: User Features (Delay Modes)

Layer 4 components are complete user-facing delay modes that compose layers 0-3.

### TapeDelay
**Path:** [tape_delay.h](dsp/include/krate/dsp/effects/tape_delay.h) • **Since:** 0.0.23

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

### BBDDelay
**Path:** [bbd_delay.h](dsp/include/krate/dsp/effects/bbd_delay.h) • **Since:** 0.0.24

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

### DigitalDelay
**Path:** [digital_delay.h](dsp/include/krate/dsp/effects/digital_delay.h) • **Since:** 0.0.25

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

### PingPongDelay
**Path:** [ping_pong_delay.h](dsp/include/krate/dsp/effects/ping_pong_delay.h) • **Since:** 0.0.26

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

### MultiTapDelay
**Path:** [multi_tap_delay.h](dsp/include/krate/dsp/effects/multi_tap_delay.h) • **Since:** 0.0.27

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

### ReverseDelay
**Path:** [reverse_delay.h](dsp/include/krate/dsp/effects/reverse_delay.h) • **Since:** 0.0.28

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

### ShimmerDelay
**Path:** [shimmer_delay.h](dsp/include/krate/dsp/effects/shimmer_delay.h) • **Since:** 0.0.29

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

### SpectralDelay
**Path:** [spectral_delay.h](dsp/include/krate/dsp/effects/spectral_delay.h) • **Since:** 0.0.30

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

### FreezeDelay
**Path:** [freeze_delay.h](dsp/include/krate/dsp/effects/freeze_delay.h) • **Since:** 0.0.31

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

### DuckingDelay
**Path:** [ducking_delay.h](dsp/include/krate/dsp/effects/ducking_delay.h) • **Since:** 0.0.32

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

### GranularDelay
**Path:** [granular_delay.h](dsp/include/krate/dsp/effects/granular_delay.h) • **Since:** 0.0.35

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

---

## Plugin Architecture

### VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/iterum/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/iterum/src/controller/` | UI state management |
| Entry | `plugins/iterum/src/entry.cpp` | Factory registration |
| IDs | `plugins/iterum/src/plugin_ids.h` | Parameter and component IDs |

### Parameter Flow

```
Host → Processor.processParameterChanges() → atomics → process()
                                                    ↓
Host ← Controller.setParamNormalized() ← IMessage ←┘
```

### State Flow

```
Save: Processor.getState() → IBStream (parameters + version)
Load: IBStream → Processor.setState() → Controller.setComponentState()
```

### UI Components

| Component | Path | Purpose |
|-----------|------|---------|
| TapPatternEditor | `plugins/iterum/src/ui/tap_pattern_editor.h` | Custom tap pattern visual editor |
| CopyPatternButton | `plugins/iterum/src/controller/controller.cpp` | Copy current pattern to custom |
| ResetPatternButton | `plugins/iterum/src/controller/controller.cpp` | Reset pattern to linear spread |

### TapPatternEditor (Spec 046)
**Path:** [tap_pattern_editor.h](plugins/iterum/src/ui/tap_pattern_editor.h) • **Since:** 0.9.6

Visual editor for creating custom delay tap patterns. Extends `VSTGUI::CControl`.

```cpp
class TapPatternEditor : public VSTGUI::CControl {
    void setTapTimeRatio(size_t tapIndex, float ratio);  // [0, 1]
    void setTapLevel(size_t tapIndex, float level);      // [0, 1]
    void setActiveTapCount(size_t count);                // 1-16
    void setSnapDivision(SnapDivision division);         // Grid snapping
    void resetToDefault();                                // Linear spread, full levels
    void onPatternChanged(int patternIndex);             // Cancel drag if not Custom
    void setParameterCallback(ParameterCallback cb);     // Notify on user drag
};
```

**Features:**
- Horizontal drag adjusts tap timing (X axis = time ratio 0-1)
- Vertical drag adjusts tap level (Y axis = level 0-1)
- Grid snapping: Off, 1/4, 1/8, 1/16, 1/32, Triplet
- Only editable when pattern == Custom (index 19)
- Copy from any pattern to use as starting point

**Parameter IDs (Custom Pattern):**
- Time ratios: `kMultiTapCustomTime0Id` - `kMultiTapCustomTime15Id` (950-965)
- Levels: `kMultiTapCustomLevel0Id` - `kMultiTapCustomLevel15Id` (966-981)
- UI tags: `kMultiTapPatternEditorTagId` (920), `kMultiTapCopyPatternButtonTagId` (921), `kMultiTapResetPatternButtonTagId` (923)

**Visibility:** Controlled by `patternEditorVisibilityController_` using IDependent pattern. Visible only when `kMultiTapTimingPatternId` == 19 (Custom).

---

## Testing Layers

| Layer | Test Location | Focus |
|-------|---------------|-------|
| 0 | `dsp/tests/unit/core/` | Pure functions, edge cases |
| 1 | `dsp/tests/unit/primitives/` | Single primitives |
| 2 | `dsp/tests/unit/processors/` | Processor behavior |
| 3 | `dsp/tests/unit/systems/` | System integration |
| 4 | `dsp/tests/unit/effects/` | Feature behavior |
| Plugin | `plugins/iterum/tests/` | End-to-end, approval tests |

---

## Quick Reference

### Layer Inclusion Rules

| Your Code In | Can Include |
|--------------|-------------|
| Layer 0 | stdlib only |
| Layer 1 | Layer 0 |
| Layer 2 | Layers 0-1 |
| Layer 3 | Layers 0-2 |
| Layer 4 | Layers 0-3 |
| Plugin | All DSP layers |

### Common Include Patterns

```cpp
#include <krate/dsp/core/db_utils.h>           // Layer 0
#include <krate/dsp/primitives/delay_line.h>   // Layer 1
#include <krate/dsp/processors/saturation.h>   // Layer 2
#include <krate/dsp/systems/delay_engine.h>    // Layer 3
#include <krate/dsp/effects/tape_delay.h>      // Layer 4
```

### Before Creating New Components

```bash
# Search for existing implementations (ODR prevention)
grep -r "class YourClassName" dsp/ plugins/
grep -r "struct YourStructName" dsp/ plugins/
```
