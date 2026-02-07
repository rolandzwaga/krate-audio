# Layer 0: Core Utilities

[← Back to Architecture Index](README.md)

**Location**: `dsp/include/krate/dsp/core/` | **Dependencies**: stdlib only, no DSP deps

---

## StereoOutput
**Path:** [stereo_output.h](../../dsp/include/krate/dsp/core/stereo_output.h) | **Since:** 0.14.1

Lightweight stereo sample pair for returning stereo audio from `process()` methods. Aggregate type -- no user-declared constructors, supports brace initialization.

```cpp
struct StereoOutput {
    float left = 0.0f;   // Left channel sample
    float right = 0.0f;  // Right channel sample
};
```

**When to use:**
- Return type for stereo `process()` methods in Layer 3 systems (VectorMixer, UnisonEngine)
- Any component that needs to return a stereo pair from a single-sample process call

**Why Layer 0:** Prevents ODR violations when multiple Layer 3 systems (e.g., UnisonEngine, VectorMixer) independently define the same return type.

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
// Semitone/ratio conversion
[[nodiscard]] constexpr float semitonesToRatio(float semitones) noexcept;  // 12→2.0, -12→0.5
[[nodiscard]] constexpr float ratioToSemitones(float ratio) noexcept;      // 2.0→12, 0.5→-12

// Frequency-to-note conversion (spec 093)
[[nodiscard]] inline int frequencyToNoteClass(float hz) noexcept;          // 440→9 (A), 261.63→0 (C), 0→-1
[[nodiscard]] inline float frequencyToCentsDeviation(float hz) noexcept;   // 442.55→+10 cents, 437.47→-10 cents
```

| Function | Description | Use Case |
|----------|-------------|----------|
| `frequencyToNoteClass` | Maps Hz to note class (0-11, 0=C) | Note-selective filtering, pitch detection |
| `frequencyToCentsDeviation` | Cents deviation from nearest note (-50 to +50) | Pitch tolerance matching, tuning analysis |

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

---

## MIDI Utilities
**Path:** [midi_utils.h](../../dsp/include/krate/dsp/core/midi_utils.h) | **Since:** 0.13.0

MIDI note and velocity conversion utilities for melodic DSP components.

```cpp
// Constants
constexpr float kA4FrequencyHz = 440.0f;  // A4 reference frequency
constexpr int kA4MidiNote = 69;           // MIDI note for A4
constexpr int kMinMidiNote = 0;           // Minimum MIDI note
constexpr int kMaxMidiNote = 127;         // Maximum MIDI note
constexpr int kMinMidiVelocity = 0;       // Minimum MIDI velocity
constexpr int kMaxMidiVelocity = 127;     // Maximum MIDI velocity

// MIDI note to frequency (12-TET tuning)
[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = 440.0f) noexcept;

// MIDI velocity to linear gain
[[nodiscard]] constexpr float velocityToGain(int velocity) noexcept;

// Velocity curve types (since 0.17.0)
enum class VelocityCurve : uint8_t { Linear, Soft, Hard, Fixed };

// Map velocity through a curve (since 0.17.0)
[[nodiscard]] inline float mapVelocity(int velocity, VelocityCurve curve) noexcept;
```

| Function | Formula | Examples |
|----------|---------|----------|
| `midiNoteToFrequency` | `a4 * 2^((note - 69) / 12)` | 69 -> 440 Hz, 60 -> 261.63 Hz |
| `velocityToGain` | `velocity / 127` | 127 -> 1.0, 64 -> ~0.5 (-6 dB) |
| `mapVelocity` (Linear) | `velocity / 127` | 64 -> ~0.504 |
| `mapVelocity` (Soft) | `sqrt(velocity / 127)` | 64 -> ~0.710 |
| `mapVelocity` (Hard) | `(velocity / 127)^2` | 64 -> ~0.254 |
| `mapVelocity` (Fixed) | `1.0` if vel > 0 | 1 -> 1.0, 0 -> 0.0 |

**When to use:**
- **Melodic DSP components:** Convert MIDI notes to oscillator/filter frequencies
- **MIDI-controlled effects:** Map velocity to amplitude or modulation depth
- **Custom tuning:** Pass alternate A4 frequency for non-standard tuning (e.g., 432 Hz)
- **Velocity curves:** Use `mapVelocity()` with `VelocityCurve` for non-linear velocity response
- **NoteProcessor, VoiceAllocator, MonoHandler:** All can use `mapVelocity()` for velocity mapping

**Do NOT use when:**
- You need microtonal tuning (this is strictly 12-TET)

---

## Sweep-Morph Link Curves
**Path:** [sweep_morph_link.h](../../specs/007-sweep-system/contracts/sweep_morph_link.h) | **Since:** 0.15.0 (Disrumpo plugin)

Pure mathematical curve functions for mapping sweep frequency position (0-1) to morph position (0-1). Part of the Disrumpo sweep system.

**Note:** These functions are defined in the Disrumpo plugin contracts directory, not the shared DSP library, as they are specific to Disrumpo's sweep-morph linking feature.

```cpp
namespace Disrumpo::SweepMorphLink {
    [[nodiscard]] inline float linear(float x) noexcept;         // y = x
    [[nodiscard]] inline float inverse(float x) noexcept;        // y = 1 - x
    [[nodiscard]] inline float easeIn(float x) noexcept;         // y = x^3 (slow start)
    [[nodiscard]] inline float easeOut(float x) noexcept;        // y = 1 - (1-x)^3 (slow end)
    [[nodiscard]] inline float holdRise(float x) noexcept;       // 0 until 0.6, then rises
    [[nodiscard]] inline float stepped(float x) noexcept;        // Quantized to 0, 0.33, 0.67, 1.0
    [[nodiscard]] float applyMorphLinkCurve(MorphLinkMode mode, float normalizedSweepFreq) noexcept;
}
```

| Curve | Formula | Character |
|-------|---------|-----------|
| Linear | `y = x` | Direct 1:1 mapping |
| Inverse | `y = 1 - x` | Reversed mapping |
| EaseIn | `y = x^3` | Slow start, fast end |
| EaseOut | `y = 1 - (1-x)^3` | Fast start, slow end |
| HoldRise | `x < 0.6 ? 0 : (x-0.6)/0.4` | Hold then rise |
| Stepped | Quantize to 4 levels | Discrete jumps |

**When to use:**
- Mapping sweep frequency position to morph position in Disrumpo
- Any feature needing normalized [0,1] position curve transformations
- Building custom automation curves with predictable mathematical properties

---

## PolyBLEP/PolyBLAMP Correction Functions
**Path:** [polyblep.h](../../dsp/include/krate/dsp/core/polyblep.h) | **Since:** 0.15.0

Polynomial band-limited step (BLEP) and ramp (BLAMP) correction functions for anti-aliased waveform generation. Pure constexpr math with no state, no allocations.

```cpp
// 2-point variants (C1 continuity, correction region: dt)
[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept;   // Step correction
[[nodiscard]] constexpr float polyBlamp(float t, float dt) noexcept;  // Ramp correction

// 4-point variants (C3 continuity, correction region: 2*dt)
[[nodiscard]] constexpr float polyBlep4(float t, float dt) noexcept;  // Higher-quality step correction
[[nodiscard]] constexpr float polyBlamp4(float t, float dt) noexcept; // Higher-quality ramp correction
```

| Function | Polynomial Degree | Kernel Width | Continuity | Use Case |
|----------|------------------|--------------|------------|----------|
| `polyBlep` | 2nd | 2 samples | C1 | Sawtooth/square anti-aliasing |
| `polyBlep4` | 4th | 4 samples | C3 | High-quality step correction (hard sync, fast FM) |
| `polyBlamp` | 3rd | 2 samples | C1 | Triangle peak smoothing |
| `polyBlamp4` | 5th | 4 samples | C3 | High-quality ramp correction |

**Parameters:**
- `t`: Normalized phase [0, 1)
- `dt`: Normalized frequency = frequency / sampleRate. Precondition: 0 < dt < 0.5

**When to use:**
- Building any PolyBLEP-based oscillator (sawtooth, square, triangle, pulse)
- Correcting step discontinuities: `saw -= polyBlep(t, dt)`
- Correcting ramp discontinuities: `tri += slopeChange * dt * polyBlamp(t, dt)`
- Use 4-point variants when higher alias suppression is needed (hard sync, fast FM)

**Do NOT use when:**
- dt >= 0.5 (frequency at or above Nyquist) -- behavior undefined
- Building wavetable oscillators (use bandlimited wavetables instead)

---

## Wavetable Data & Mipmap Level Selection
**Path:** [wavetable_data.h](../../dsp/include/krate/dsp/core/wavetable_data.h) | **Since:** 0.15.0

Mipmapped wavetable storage and mipmap level selection for alias-free wavetable oscillator playback. Each mipmap level contains a band-limited version of a single waveform cycle with guard samples enabling branchless cubic Hermite interpolation.

```cpp
// Constants
inline constexpr size_t kDefaultTableSize = 2048;   // Samples per level (excluding guards)
inline constexpr size_t kMaxMipmapLevels = 11;       // ~11 octaves of coverage
inline constexpr size_t kGuardSamples = 4;            // 1 prepend + 3 append

// Storage (~90 KB per instance)
struct WavetableData {
    [[nodiscard]] const float* getLevel(size_t level) const noexcept;   // Returns nullptr if invalid
    float* getMutableLevel(size_t level) noexcept;                       // For generator use
    [[nodiscard]] size_t tableSize() const noexcept;                     // Always 2048
    [[nodiscard]] size_t numLevels() const noexcept;
    void setNumLevels(size_t n) noexcept;                                // Clamped to [0, 11]
};

// Level selection functions
[[nodiscard]] constexpr size_t selectMipmapLevel(float frequency, float sampleRate, size_t tableSize) noexcept;
[[nodiscard]] inline float selectMipmapLevelFractional(float frequency, float sampleRate, size_t tableSize) noexcept;
```

**Memory Layout per Level:**
```
Physical: [prepend_guard][data_0..data_{N-1}][append_0][append_1][append_2]
getLevel() returns pointer to data_0 (physical offset 1)
p[-1] = data[N-1]   (prepend guard)
p[N]  = data[0]     (first append guard)
p[N+1] = data[1]    (second append guard)
p[N+2] = data[2]    (third append guard)
```

**Level Selection Formula:**
```
level = max(0, ceil(log2(frequency * tableSize / sampleRate)))
```

| Frequency (at 44.1 kHz) | Integer Level | Fractional Level |
|--------------------------|---------------|------------------|
| 20 Hz | 0 | ~0.0 |
| 440 Hz | 5 | ~4.4 |
| 10,000 Hz | 9 | ~8.9 |
| 22,050 Hz (Nyquist) | 10 | 10.0 |

**When to use:**
- Any wavetable-based synthesis component (oscillator, FM operator, PD oscillator)
- Storing mipmapped single-cycle waveform data shared across polyphonic voices
- Selecting appropriate mipmap level for alias-free playback at a given frequency

**Do NOT use when:**
- You need PolyBLEP oscillator (use `polyblep_oscillator.h` instead)
- You need multi-cycle or streaming audio storage (use DelayLine or buffer)

**Example Usage:**
```cpp
WavetableData sawTable;
generateMipmappedSaw(sawTable);  // From wavetable_generator.h

// Read from level 0 using guard samples for cubic Hermite
const float* level0 = sawTable.getLevel(0);
// Safe reads: level0[-1], level0[0], ..., level0[2047], level0[2048], level0[2049], level0[2050]

// Select level for 440 Hz at 44100 Hz
size_t level = selectMipmapLevel(440.0f, 44100.0f, sawTable.tableSize());
float fracLevel = selectMipmapLevelFractional(440.0f, 44100.0f, sawTable.tableSize());
```

---

## Phase Accumulator & Utilities
**Path:** [phase_utils.h](../../dsp/include/krate/dsp/core/phase_utils.h) | **Since:** 0.15.0

Centralized phase accumulator and utility functions for oscillator infrastructure. Replaces duplicated phase logic in lfo.h, audio_rate_filter_fm.h, and frequency_shifter.h.

```cpp
// Standalone utility functions
[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept;
[[nodiscard]] constexpr double wrapPhase(double phase) noexcept;       // Wraps to [0, 1) via subtraction
[[nodiscard]] constexpr bool detectPhaseWrap(double currentPhase, double previousPhase) noexcept;
[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept;

// Phase accumulator struct
struct PhaseAccumulator {
    double phase = 0.0;       // Current phase [0, 1)
    double increment = 0.0;   // Phase advance per sample

    [[nodiscard]] bool advance() noexcept;                         // Returns true on wrap
    void reset() noexcept;                                          // Phase -> 0, keeps increment
    void setFrequency(float frequency, float sampleRate) noexcept; // Sets increment
};
```

**Basic PhaseAccumulator pattern:**
```cpp
PhaseAccumulator acc;
acc.setFrequency(440.0f, 44100.0f);

for (int i = 0; i < numSamples; ++i) {
    float saw = 2.0f * static_cast<float>(acc.phase) - 1.0f;
    float t = static_cast<float>(acc.phase);
    float dt = static_cast<float>(acc.increment);
    saw -= polyBlep(t, dt);

    bool wrapped = acc.advance();
    if (wrapped) {
        double offset = subsamplePhaseWrapOffset(acc.phase, acc.increment);
        // Use offset for sub-sample-accurate BLEP placement
    }
    output[i] = saw;
}
```

**Design decisions:**
- `double` precision for phase/increment prevents accumulated rounding over long playback
- Wrapping uses subtraction (not `std::fmod`) matching existing codebase pattern
- `wrapPhase(double)` wraps to [0, 1) for oscillator use; distinct from `spectral_utils.h::wrapPhase(float)` which wraps to [-pi, pi]

**When to use:**
- Any oscillator or modulator needing phase management
- Calculating phase increments from frequency/sample rate
- Detecting and timing phase wraps for PolyBLEP correction
- Replacing duplicated phase logic in existing components

**Do NOT use when:**
- You need spectral phase wrapping to [-pi, pi] (use `spectral_utils.h` instead)
- You need quadrature oscillator (sin/cos rotation) pattern (use direct computation)
