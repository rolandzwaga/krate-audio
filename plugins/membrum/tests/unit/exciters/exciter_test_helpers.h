#pragma once

// ==============================================================================
// Shared helpers for Phase 3 exciter contract tests.
// ==============================================================================
// NOTE: Header-only; each test TU #includes it. Functions are static/inline so
// ODR issues do not arise across multiple test compilation units.
// ==============================================================================

#include <cmath>
#include <cstdint>
#include <cstring>

namespace membrum_exciter_tests {

// Bit-manipulation NaN/Inf check. -ffast-math breaks std::isnan()/isfinite(),
// so we inspect the IEEE-754 exponent bits directly (see CLAUDE.md).
inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Discrete-Fourier spectral-centroid proxy on a LINEAR-spaced grid.
// Returns Σ(f * |X(f)|) / Σ(|X(f)|) in Hz. Avoids pulling a full FFT for
// per-test use. Linear spacing (not log) so bin density does not bias the
// centroid toward low frequencies. 128 bins between fLow and 0.45*Nyquist.
//
// No windowing — these tests analyze transient impulse bursts whose energy
// is concentrated at the start of the buffer; a Hann window would attenuate
// that content to near-zero. Leakage is acceptable because we compare
// centroid RATIOS between two signals of identical length.
inline float spectralCentroidDFT(const float* samples,
                                 int          numSamples,
                                 double       sampleRate,
                                 float        fLow  = 50.0f,
                                 float        fHigh = 0.0f) noexcept
{
    constexpr int kBins = 128;
    if (fHigh <= 0.0f)
        fHigh = 0.45f * static_cast<float>(sampleRate);
    const float df = (fHigh - fLow) / static_cast<float>(kBins - 1);
    float num = 0.0f, den = 0.0f;
    for (int b = 0; b < kBins; ++b)
    {
        const float f = fLow + df * static_cast<float>(b);
        const float w = 2.0f * 3.14159265358979f * f / static_cast<float>(sampleRate);
        float re = 0.0f, im = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float ang = w * static_cast<float>(n);
            re += samples[n] * std::cos(ang);
            im -= samples[n] * std::sin(ang);
        }
        const float mag = std::sqrt(re * re + im * im);
        num += f * mag;
        den += mag;
    }
    return den > 0.0f ? num / den : 0.0f;
}

// Trigger an exciter and collect N samples into `out`.
template <typename Exciter>
inline void runBurst(Exciter& e, float velocity, float* out, int numSamples,
                     float bodyFeedback = 0.0f) noexcept
{
    e.trigger(velocity);
    for (int i = 0; i < numSamples; ++i)
        out[i] = e.process(bodyFeedback);
}

} // namespace membrum_exciter_tests
