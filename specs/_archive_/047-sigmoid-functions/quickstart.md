# Quickstart: Sigmoid Transfer Function Library

## Include

```cpp
#include <krate/dsp/core/sigmoid.h>
```

## Basic Usage

### Symmetric Saturation (Odd Harmonics)

```cpp
using namespace Krate::DSP;

// Warm tanh saturation (fastest accurate sigmoid)
float out = Sigmoid::tanh(input);

// Variable drive control
float out = Sigmoid::tanhVariable(input, drive);  // drive: 0.1 (soft) to 10.0 (hard)

// Ultra-fast alternative (~10x faster than tanh)
float out = Sigmoid::recipSqrt(input);

// Tape-like character with unique spectral nulls
float out = Sigmoid::erf(input);
```

### Asymmetric Saturation (Even + Odd Harmonics)

```cpp
// Tube-style warmth (2nd harmonic emphasis)
float out = Asymmetric::tube(input);

// Diode-style clipping (soft forward, hard reverse)
float out = Asymmetric::diode(input);

// Custom asymmetry via different gains per polarity
float out = Asymmetric::dualCurve(input, posGain, negGain);

// Add asymmetry to any symmetric function via DC bias
float out = Asymmetric::withBias(input, 0.2f, Sigmoid::tanh);
// Note: Must DC-block output after using withBias!
```

## Function Selection Guide

| Need | Use | Why |
|------|-----|-----|
| Best quality saturation | `Sigmoid::tanh()` | Smooth, musical |
| Maximum CPU efficiency | `Sigmoid::recipSqrt()` | 10x faster |
| Tape emulation | `Sigmoid::erf()` | Unique spectrum |
| Tube warmth | `Asymmetric::tube()` | 2nd harmonic |
| Fuzz/overdrive | `Asymmetric::diode()` | Asymmetric clip |
| Simple polynomial | `Sigmoid::softClipCubic()` | No transcendentals |

## Real-Time Safety

All functions are:
- `noexcept` - No exceptions
- `constexpr` or `inline` - Compile-time or inlined
- Zero allocations - Safe for audio thread
- Branchless or predictable - Good cache behavior

## Edge Cases

```cpp
// All functions handle these correctly:
Sigmoid::tanh(NAN);      // Returns NAN
Sigmoid::tanh(INFINITY); // Returns 1.0f
Sigmoid::tanh(-INFINITY); // Returns -1.0f

// Variable functions handle zero/negative drive:
Sigmoid::tanhVariable(x, 0.0f);  // Returns 0.0f
Sigmoid::tanhVariable(x, -5.0f); // Treats as positive (5.0f)
```

## Anti-Aliasing

These are pure transfer functions. For anti-aliased distortion, wrap with `Oversampler`:

```cpp
#include <krate/dsp/primitives/oversampler.h>

Oversampler<2, 1> oversampler;  // 2x mono
oversampler.prepare(sampleRate, blockSize);

oversampler.process(buffer, numSamples, [](float* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        data[i] = Sigmoid::tanh(data[i] * drive);
    }
});
```

## DC Blocking

Asymmetric functions create DC offset. Always DC-block after:

```cpp
#include <krate/dsp/primitives/biquad.h>

Biquad dcBlocker;
dcBlocker.configure(FilterType::Highpass, 10.0f, 0.707f, 0.0f, sampleRate);

// Process
for (size_t i = 0; i < n; ++i) {
    buffer[i] = Asymmetric::tube(buffer[i]);
}
dcBlocker.processBlock(buffer, n);
```
