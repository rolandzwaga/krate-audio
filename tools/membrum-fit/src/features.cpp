#include "features.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace MembrumFit {

namespace {

// Simple in-place Cooley-Tukey radix-2 FFT on std::complex<float>. Used for
// attack-window feature extraction where pffft's min size (256) may be bigger
// than our windows.
void fftInPlace(std::vector<std::complex<float>>& x) {
    const std::size_t n = x.size();
    if (n < 2) return;
    // bit-reversal
    std::size_t j = 0;
    for (std::size_t i = 1; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::size_t nextPow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

std::vector<float> magnitudeSpectrum(std::span<const float> window, float& dcRemoved) {
    const std::size_t n = nextPow2(std::max<std::size_t>(window.size(), 64));
    std::vector<std::complex<float>> buf(n);
    for (std::size_t i = 0; i < window.size(); ++i) buf[i] = {window[i], 0.0f};
    fftInPlace(buf);
    std::vector<float> mag(n / 2);
    for (std::size_t k = 0; k < mag.size(); ++k) mag[k] = std::abs(buf[k]);
    dcRemoved = mag.empty() ? 0.0f : mag[0];
    if (!mag.empty()) mag[0] = 0.0f;
    return mag;
}

}  // namespace

float computeLogAttackTime(std::span<const float> w, double sr) {
    if (w.empty()) return -6.0f;
    float peak = 0.0f;
    for (float x : w) peak = std::max(peak, std::abs(x));
    if (peak <= 1e-9f) return -6.0f;
    const float thStart = peak * 0.02f;
    const float thEnd   = peak * 0.90f;
    std::size_t iStart = 0, iEnd = w.size() - 1;
    for (std::size_t i = 0; i < w.size(); ++i) if (std::abs(w[i]) >= thStart) { iStart = i; break; }
    for (std::size_t i = iStart; i < w.size(); ++i) if (std::abs(w[i]) >= thEnd)   { iEnd = i; break; }
    const float dt = (iEnd > iStart) ? static_cast<float>((iEnd - iStart) / sr) : 1e-6f;
    return std::log10(std::max(dt, 1e-6f));
}

float computeSpectralFlatness(std::span<const float> w, double) {
    float dc; const auto mag = magnitudeSpectrum(w, dc);
    if (mag.size() < 2) return 0.0f;
    double logSum = 0.0, arith = 0.0; std::size_t n = 0;
    for (std::size_t k = 1; k < mag.size(); ++k) {
        const float m = mag[k];
        if (m > 1e-12f) { logSum += std::log(m); arith += m; ++n; }
    }
    if (n == 0 || arith <= 0.0) return 0.0f;
    const double geomean = std::exp(logSum / static_cast<double>(n));
    const double arithmean = arith / static_cast<double>(n);
    return static_cast<float>(std::clamp(geomean / arithmean, 0.0, 1.0));
}

float computeSpectralCentroid(std::span<const float> w, double sr) {
    float dc; const auto mag = magnitudeSpectrum(w, dc);
    if (mag.empty()) return 0.0f;
    double num = 0.0, den = 0.0;
    const double binHz = sr / (2.0 * static_cast<double>(mag.size()));
    for (std::size_t k = 1; k < mag.size(); ++k) {
        num += mag[k] * k * binHz;
        den += mag[k];
    }
    return (den > 0.0) ? static_cast<float>(num / den) : 0.0f;
}

float computeAutocorrelationPeak(std::span<const float> s, std::size_t minLag, std::size_t maxLag) {
    if (s.size() <= maxLag) return 0.0f;
    float norm = 0.0f;
    for (float x : s) norm += x * x;
    if (norm <= 1e-9f) return 0.0f;
    float peak = 0.0f;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        float r = 0.0f;
        for (std::size_t i = 0; i + lag < s.size(); ++i) r += s[i] * s[i + lag];
        peak = std::max(peak, r / norm);
    }
    return std::clamp(peak, 0.0f, 1.0f);
}

float computeInharmonicity(std::span<const float> w, double sr, float f1) {
    if (f1 <= 0.0f) return 0.0f;
    float dc; const auto mag = magnitudeSpectrum(w, dc);
    if (mag.size() < 4) return 0.0f;
    const double binHz = sr / (2.0 * static_cast<double>(mag.size()));
    // Compare 2..8 th peaks to k*f1.
    double sumSq = 0.0; int N = 0;
    for (int k = 2; k <= 8; ++k) {
        const double target = k * f1;
        const std::size_t idx = static_cast<std::size_t>(std::round(target / binHz));
        if (idx >= mag.size()) break;
        // find local peak within +-3 bins
        std::size_t best = idx;
        for (std::size_t j = (idx > 3 ? idx - 3 : 0); j < std::min(mag.size(), idx + 3); ++j) {
            if (mag[j] > mag[best]) best = j;
        }
        const double measured = best * binHz;
        const double dev = (measured - target) / (k * f1);
        sumSq += dev * dev;
        ++N;
    }
    return (N > 0) ? static_cast<float>(sumSq / static_cast<double>(N)) : 0.0f;
}

float estimateFundamental(std::span<const float> w, double sr) {
    float dc; const auto mag = magnitudeSpectrum(w, dc);
    if (mag.size() < 4) return 0.0f;
    const double binHz = sr / (2.0 * static_cast<double>(mag.size()));
    std::size_t best = 0; float bestMag = 0.0f;
    const std::size_t maxBin = std::min(mag.size(), static_cast<std::size_t>(2000.0 / binHz));
    for (std::size_t k = 2; k < maxBin; ++k) {
        if (mag[k] > bestMag) { bestMag = mag[k]; best = k; }
    }
    if (best < 2 || best + 1 >= mag.size()) return static_cast<float>(best * binHz);
    // Parabolic interpolation
    const float a = mag[best - 1];
    const float b = mag[best];
    const float c = mag[best + 1];
    const float denom = (a - 2.0f * b + c);
    const float delta = (std::abs(denom) > 1e-9f) ? 0.5f * (a - c) / denom : 0.0f;
    return static_cast<float>((best + delta) * binHz);
}

AttackFeatures extractAttackFeatures(std::span<const float> s, const SegmentedSample& seg, double sr) {
    AttackFeatures f;
    if (seg.attackEndSample <= seg.onsetSample || seg.attackEndSample > s.size()) return f;
    const std::span<const float> attack = s.subspan(seg.onsetSample, seg.attackEndSample - seg.onsetSample);
    f.logAttackTime       = computeLogAttackTime(attack, sr);
    f.spectralFlatness    = computeSpectralFlatness(attack, sr);
    f.peakSpectralCentroid = computeSpectralCentroid(attack, sr);
    const std::size_t hops = attack.size() / 5;
    if (hops > 4) {
        for (int i = 0; i < 5; ++i) {
            const std::size_t off = i * hops;
            f.centroidTrajectory[i] = computeSpectralCentroid(attack.subspan(off, hops), sr);
        }
    }
    const std::size_t minLag = static_cast<std::size_t>(0.0005 * sr);
    const std::size_t maxLag = static_cast<std::size_t>(0.02   * sr);
    const std::size_t lookback = std::min<std::size_t>(seg.onsetSample, 2 * maxLag);
    if (lookback > maxLag) {
        f.preOnsetARPeak = computeAutocorrelationPeak(
            s.subspan(seg.onsetSample - lookback, lookback), minLag, maxLag);
    }
    const float f1 = estimateFundamental(attack, sr);
    f.inharmonicity = computeInharmonicity(attack, sr, f1);

    // Decay / total energy ratio.
    float total = 0.0f, decay = 0.0f;
    for (std::size_t i = 0; i < s.size(); ++i) total += s[i] * s[i];
    for (std::size_t i = seg.decayStartSample; i < seg.decayEndSample && i < s.size(); ++i)
        decay += s[i] * s[i];
    f.decayTailEnergyRatio = (total > 1e-9f) ? decay / total : 0.0f;

    float peak = 0.0f;
    for (float x : attack) peak = std::max(peak, std::abs(x));
    f.velocityEstimate = std::clamp(peak, 0.0f, 1.0f);
    return f;
}

}  // namespace MembrumFit
