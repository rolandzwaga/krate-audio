#pragma once

// =============================================================================
// Disrumpo audit metrics (test support, header-only)
// =============================================================================
// Spectral measurements the promoted audit probes share (fix plan Phase 6):
// readAliasing() reads aliased / non-harmonic / harmonic power of a rendered sine,
// responseDb() reads |H(f)| of an impulse response. Moved from
// integration/processor_audio_output_test.cpp. continuityAround() and clickCount()
// live in disrumpo_rig.h; the spectral centroid is Krate::Test::extractAudioFeatures
// (tests/test_helpers/audio_features.h).
// =============================================================================

#include "test_helpers/spectral_analysis.h"

#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace Disrumpo::TestSupport {

struct AliasReading {
    double aliasedDb = 0.0;      ///< power in the predicted fold-back bins (harmonics 2..200)
    double nonHarmonicDb = 0.0;  ///< power in every bin outside the harmonic main lobes
    double harmonicDb = 0.0;     ///< power in the harmonic main lobes (fundamental included)
    double fundamentalDb = 0.0;

    /// Non-harmonic power relative to the harmonic power (dB).
    [[nodiscard]] double nonHarmonicRelDb() const noexcept { return nonHarmonicDb - harmonicDb; }
};

/// Power-averaged over the last `frames` Hann-windowed `fftSize`-sample frames of `x`.
/// `toneHz` must sit exactly on a bin (e.g. 1031.25 Hz = bin 176 of 8192 at 48 kHz).
inline AliasReading readAliasing(const std::vector<float>& x, double sampleRate, float toneHz, size_t fftSize = 8192,
                                 size_t frames = 4) {
    using namespace Krate::DSP::TestUtils;
    AliasingTestConfig cfg;
    cfg.testFrequencyHz = toneHz;
    cfg.sampleRate = static_cast<float>(sampleRate);
    cfg.fftSize = fftSize;
    cfg.maxHarmonic = 200;
    auto aliased = getAliasedBins(cfg);
    std::sort(aliased.begin(), aliased.end());
    aliased.erase(std::unique(aliased.begin(), aliased.end()), aliased.end());
    const size_t fundBin = frequencyToBin(toneHz, cfg.sampleRate, fftSize);

    Krate::DSP::FFT fft;
    fft.prepare(fftSize);
    std::vector<float> window(fftSize);
    Krate::DSP::Window::generateHann(window.data(), fftSize);
    std::vector<float> frame(fftSize);
    std::vector<Krate::DSP::Complex> spectrum(fft.numBins());

    double aliasPower = 0.0;
    double nonHarmonicPower = 0.0;
    double harmonicPower = 0.0;
    double fundamentalPower = 0.0;
    const size_t start = x.size() - frames * fftSize;
    for (size_t f = 0; f < frames; ++f) {
        for (size_t i = 0; i < fftSize; ++i) {
            frame[i] = x[start + f * fftSize + i] * window[i];
        }
        fft.forward(frame.data(), spectrum.data());
        const double a = detail::sumBinPower(spectrum.data(), aliased);
        aliasPower += a * a;
        const double m = spectrum[fundBin].magnitude();
        fundamentalPower += m * m;
        for (size_t b = 3; b < spectrum.size(); ++b) {
            const size_t r = b % fundBin;
            const double mb = spectrum[b].magnitude();
            if (r <= 2 || r >= fundBin - 2) {  // harmonic main lobe (Hann: +-1 bin)
                harmonicPower += mb * mb;
            } else {
                nonHarmonicPower += mb * mb;
            }
        }
    }
    const auto db = [frames](double p) { return 10.0 * std::log10(p / static_cast<double>(frames) + 1e-30); };
    return {.aliasedDb = db(aliasPower),
            .nonHarmonicDb = db(nonHarmonicPower),
            .harmonicDb = db(harmonicPower),
            .fundamentalDb = db(fundamentalPower)};
}

/// |H(f)| in dB of an impulse response, by a direct DFT at f (double precision).
inline double responseDb(const std::vector<float>& h, double f, double sr) {
    const double w = 2.0 * std::numbers::pi * f / sr;
    const double cw = std::cos(w);
    const double sw = std::sin(w);
    double c = 1.0;
    double s = 0.0;
    double re = 0.0;
    double im = 0.0;
    for (const float v : h) {
        re += static_cast<double>(v) * c;
        im -= static_cast<double>(v) * s;
        const double cNext = c * cw - s * sw;
        s = s * cw + c * sw;
        c = cNext;
    }
    return 10.0 * std::log10(re * re + im * im + 1e-30);
}

/// Group delay (samples) of an impulse response at f: the phase slope over f +/- 2 Hz
/// (direct DFT in double precision; follow-up 81).
inline double groupDelaySamples(const std::vector<float>& h, double f, double sr) {
    const auto at = [&h, sr](double fq) {
        const double w = 2.0 * std::numbers::pi * fq / sr;
        const double cw = std::cos(w);
        const double sw = std::sin(w);
        double c = 1.0;
        double s = 0.0;
        double re = 0.0;
        double im = 0.0;
        for (const float v : h) {
            re += static_cast<double>(v) * c;
            im -= static_cast<double>(v) * s;
            const double cNext = c * cw - s * sw;
            s = s * cw + c * sw;
            c = cNext;
        }
        return std::complex<double>(re, im);
    };
    constexpr double kHalfSpan = 2.0;
    const double dphi = std::arg(at(f + kHalfSpan) / at(f - kHalfSpan));
    return -dphi / (2.0 * std::numbers::pi * 2.0 * kHalfSpan) * sr;
}

}  // namespace Disrumpo::TestSupport
