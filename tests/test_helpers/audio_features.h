// ==============================================================================
// audio_features.h -- coarse perceptual feature extraction over a rendered signal
// ==============================================================================
// Shared by tools/krate-render and by golden/perceptual render tests so both
// measure audio the same way. Features: peak/RMS in dBFS, spectral centroid, and
// energy fractions in 5 bands (20-100, 100-500, 500-2k, 2k-8k, 8k-Nyquist).
//
// Deterministic text serialization (formatFeatures) uses coarse precision so the
// golden strings are stable across MSVC/Clang FP differences.
// ==============================================================================
#pragma once

#include <krate/dsp/primitives/fft.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Krate::Test {

struct AudioFeatures {
    double durationSec = 0.0;
    double peakDbfs = -160.0;
    double rmsDbfs = -160.0;
    double centroidHz = 0.0;
    // Energy fraction per band: [20-100, 100-500, 500-2k, 2k-8k, 8k-Nyquist]
    std::array<double, 5> band{{0, 0, 0, 0, 0}};
};

inline double linToDbfs(double lin) {
    return lin > 1e-9 ? 20.0 * std::log10(lin) : -160.0;
}

// Extract features from a mono signal at the given sample rate.
inline AudioFeatures extractAudioFeatures(const std::vector<float>& mono, double sr) {
    AudioFeatures f;
    const std::size_t n = mono.size();
    f.durationSec = static_cast<double>(n) / sr;

    double peak = 0.0, sumSq = 0.0;
    for (float x : mono) {
        double a = std::fabs(static_cast<double>(x));
        peak = std::max(peak, a);
        sumSq += static_cast<double>(x) * x;
    }
    f.peakDbfs = linToDbfs(peak);
    f.rmsDbfs = linToDbfs(std::sqrt(sumSq / static_cast<double>(std::max<std::size_t>(1, n))));

    constexpr std::size_t kFft = 2048;
    constexpr std::size_t kHop = 1024;
    Krate::DSP::FFT fft;
    fft.prepare(kFft);
    std::vector<float> win(kFft), frame(kFft);
    for (std::size_t i = 0; i < kFft; ++i)
        win[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * static_cast<float>(i) /
                                         static_cast<float>(kFft - 1)));
    std::vector<Krate::DSP::Complex> spec(kFft / 2 + 1);
    std::vector<double> magAvg(kFft / 2 + 1, 0.0);
    std::size_t frames = 0;
    for (std::size_t start = 0; start + kFft <= n || (start == 0 && n > 0); start += kHop) {
        for (std::size_t i = 0; i < kFft; ++i)
            frame[i] = ((start + i < n) ? mono[start + i] : 0.0f) * win[i];
        fft.forward(frame.data(), spec.data());
        for (std::size_t k = 0; k < spec.size(); ++k) magAvg[k] += spec[k].magnitude();
        ++frames;
        if (start + kFft >= n) break;
    }
    if (frames > 0)
        for (double& m : magAvg) m /= static_cast<double>(frames);

    const double binHz = sr / static_cast<double>(kFft);
    const double nyq = sr * 0.5;
    const double edges[6] = {20.0, 100.0, 500.0, 2000.0, 8000.0, nyq};
    double num = 0.0, den = 0.0, totalE = 0.0;
    std::array<double, 5> bandE{{0, 0, 0, 0, 0}};
    for (std::size_t k = 1; k < magAvg.size(); ++k) {  // skip DC
        double freq = static_cast<double>(k) * binHz;
        double mag = magAvg[k];
        double e = mag * mag;
        num += freq * mag;
        den += mag;
        totalE += e;
        for (int b = 0; b < 5; ++b)
            if (freq >= edges[b] && freq < edges[b + 1]) { bandE[static_cast<std::size_t>(b)] += e; break; }
    }
    f.centroidHz = den > 0.0 ? num / den : 0.0;
    for (int b = 0; b < 5; ++b)
        f.band[static_cast<std::size_t>(b)] = totalE > 0.0 ? bandE[static_cast<std::size_t>(b)] / totalE : 0.0;
    return f;
}

// Stable, diff-reviewable one-line serialization for golden comparison.
inline std::string formatFeatures(const AudioFeatures& f) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "peakDbfs=%.2f rmsDbfs=%.2f centroidHz=%.1f "
                  "b1=%.3f b2=%.3f b3=%.3f b4=%.3f b5=%.3f",
                  f.peakDbfs, f.rmsDbfs, f.centroidHz,
                  f.band[0], f.band[1], f.band[2], f.band[3], f.band[4]);
    return buf;
}

}  // namespace Krate::Test
