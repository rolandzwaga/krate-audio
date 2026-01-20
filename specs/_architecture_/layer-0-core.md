# Layer 0: Core Utilities

[← Back to Architecture Index](README.md)

**Location**: `dsp/include/krate/dsp/core/` | **Dependencies**: stdlib only, no DSP deps

---

## dB/Linear Conversion
**Path:** [db_utils.h](../../dsp/include/krate/dsp/core/db_utils.h) | **Since:** 0.0.1

```cpp
constexpr float kSilenceFloorDb = -144.0f;
[[nodiscard]] constexpr float dbToGain(float dB) noexcept;      // 0dB→1.0, -20dB→0.1, NaN→0.0
[[nodiscard]] constexpr float gainToDb(float gain) noexcept;    // 1.0→0dB, 0.0→-144dB
```

---

## Mathematical Constants
**Path:** [dsp_utils.h](../../dsp/include/krate/dsp/core/dsp_utils.h) | **Since:** 0.0.0

```cpp
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
```

---

## Window Functions
**Path:** [window_functions.h](../../dsp/include/krate/dsp/core/window_functions.h) | **Since:** 0.0.7

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

---

## Xorshift32 PRNG
**Path:** [random.h](../../dsp/include/krate/dsp/core/random.h) | **Since:** 0.0.14

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

---

## NoteValue & Tempo Sync
**Path:** [note_value.h](../../dsp/include/krate/dsp/core/note_value.h) | **Since:** 0.0.17

```cpp
enum class NoteValue : uint8_t { DoubleWhole, Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond, SixtyFourth };
enum class NoteModifier : uint8_t { None, Dotted, Triplet };

[[nodiscard]] constexpr float getBeatsForNote(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
[[nodiscard]] constexpr float noteToDelayMs(NoteValue note, NoteModifier modifier, double tempoBPM) noexcept;
[[nodiscard]] constexpr float dropdownToDelayMs(int dropdownIndex, double tempoBPM) noexcept;
```

**Dropdown mapping (0-9):** 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1

---

## BlockContext
**Path:** [block_context.h](../../dsp/include/krate/dsp/core/block_context.h) | **Since:** 0.0.17

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

---

## FastMath
**Path:** [fast_math.h](../../dsp/include/krate/dsp/core/fast_math.h) | **Since:** 0.0.17

```cpp
namespace FastMath {
    [[nodiscard]] constexpr float fastTanh(float x) noexcept;  // Padé (5,4), ~3x faster than std::tanh
}
```

**Recommendations:** Use `fastTanh()` for saturation/waveshaping (3x speedup). Use `std::sin/cos/exp` for LFOs and filters (MSVC already optimized).

---

## Sigmoid Transfer Functions
**Path:** [sigmoid.h](../../dsp/include/krate/dsp/core/sigmoid.h) | **Since:** 0.10.0

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

---

## Interpolation Utilities
**Path:** [interpolation.h](../../dsp/include/krate/dsp/core/interpolation.h) | **Since:** 0.0.17

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

---

## Stereo Cross-Blend
**Path:** [stereo_utils.h](../../dsp/include/krate/dsp/core/stereo_utils.h) | **Since:** 0.0.19

```cpp
constexpr void stereoCrossBlend(float inL, float inR, float crossAmount, float& outL, float& outR) noexcept;
// crossAmount: 0.0=normal, 0.5=mono, 1.0=swap (ping-pong)
```

---

## Pitch Utilities
**Path:** [pitch_utils.h](../../dsp/include/krate/dsp/core/pitch_utils.h) | **Since:** 0.0.35

```cpp
[[nodiscard]] constexpr float semitonesToRatio(float semitones) noexcept;  // 12→2.0, -12→0.5
[[nodiscard]] constexpr float ratioToSemitones(float ratio) noexcept;      // 2.0→12, 0.5→-12
```

---

## Grain Envelope
**Path:** [grain_envelope.h](../../dsp/include/krate/dsp/core/grain_envelope.h) | **Since:** 0.0.35

```cpp
enum class GrainEnvelopeType : uint8_t { Hann, Trapezoid, Sine, Blackman };

class GrainEnvelope {
    static void generate(float* buffer, size_t size, GrainEnvelopeType type) noexcept;
    static float lookup(const float* table, size_t tableSize, float phase) noexcept;
};
```

---

## Crossfade Utilities
**Path:** [crossfade_utils.h](../../dsp/include/krate/dsp/core/crossfade_utils.h) | **Since:** 0.0.41

```cpp
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept;
[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept;
// position: 0.0=full fadeOut, 0.5=equal blend, 1.0=full fadeIn
```

---

## Chebyshev Polynomials
**Path:** [chebyshev.h](../../dsp/include/krate/dsp/core/chebyshev.h) | **Since:** 0.10.0

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

---

## Wavefolding Math Functions
**Path:** [wavefold_math.h](../../dsp/include/krate/dsp/core/wavefold_math.h) | **Since:** 0.10.0

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

## Formant Tables
**Path:** [filter_tables.h](../../dsp/include/krate/dsp/core/filter_tables.h) | **Since:** 0.12.0

Constexpr formant frequency and bandwidth data for vowel synthesis and formant filtering.

```cpp
enum class Vowel : uint8_t { A = 0, E = 1, I = 2, O = 3, U = 4 };

struct FormantData {
    float f1;   // First formant frequency (Hz)
    float f2;   // Second formant frequency (Hz)
    float f3;   // Third formant frequency (Hz)
    float bw1;  // First formant bandwidth (Hz)
    float bw2;  // Second formant bandwidth (Hz)
    float bw3;  // Third formant bandwidth (Hz)
};

inline constexpr std::array<FormantData, 5> kVowelFormants;  // Bass male voice data
[[nodiscard]] inline constexpr const FormantData& getFormant(Vowel v) noexcept;
```

| Vowel | F1 (Hz) | F2 (Hz) | F3 (Hz) | BW1 (Hz) | BW2 (Hz) | BW3 (Hz) |
|-------|---------|---------|---------|----------|----------|----------|
| A | 600 | 1040 | 2250 | 60 | 70 | 110 |
| E | 400 | 1620 | 2400 | 40 | 80 | 100 |
| I | 250 | 1750 | 2600 | 60 | 90 | 100 |
| O | 400 | 750 | 2400 | 40 | 80 | 100 |
| U | 350 | 600 | 2400 | 40 | 80 | 100 |

**When to use:**
- **Formant filter design:** Configure parallel bandpass filters using F1/F2/F3 frequencies
- **Vowel morphing:** Interpolate between formant data for different vowels
- **Vocal synthesis:** Build formant-based voice effects (talker, vocoder)

---

## Filter Design Utilities
**Path:** [filter_design.h](../../dsp/include/krate/dsp/core/filter_design.h) | **Since:** 0.12.0

Filter design utility functions for bilinear transform, RT60 calculation, and Q values.

```cpp
namespace FilterDesign {
    // Frequency prewarping for bilinear transform
    [[nodiscard]] inline float prewarpFrequency(float freq, double sampleRate) noexcept;

    // Comb filter feedback for desired reverb decay time
    [[nodiscard]] inline float combFeedbackForRT60(float delayMs, float rt60Seconds) noexcept;

    // Chebyshev Type I Q values for cascade stages
    [[nodiscard]] inline float chebyshevQ(size_t stage, size_t numStages, float rippleDb) noexcept;

    // Bessel filter Q values (lookup table, constexpr)
    [[nodiscard]] constexpr float besselQ(size_t stage, size_t numStages) noexcept;

    // Butterworth pole angles (constexpr)
    [[nodiscard]] constexpr float butterworthPoleAngle(size_t k, size_t N) noexcept;
}
```

| Function | Purpose | Use Case |
|----------|---------|----------|
| `prewarpFrequency` | Compensate bilinear transform warping | SVF, Ladder, any bilinear-derived filter |
| `combFeedbackForRT60` | Calculate feedback for reverb decay | Comb filters, Schroeder reverb |
| `chebyshevQ` | Chebyshev Type I cascade Q values | Steeper rolloff filters with ripple |
| `besselQ` | Bessel cascade Q values (orders 2-8) | Maximally flat group delay filters |
| `butterworthPoleAngle` | Butterworth pole positions | Reference for filter design |

**When to use:**
- **Bilinear transform filters:** Call `prewarpFrequency()` before calculating analog prototype
- **Reverb design:** Use `combFeedbackForRT60()` for Schroeder comb filters
- **Advanced cascades:** Use `chebyshevQ()`/`besselQ()` for multi-stage biquad cascades
