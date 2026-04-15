#include "tone_shaper_fit.h"
#include "features.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace MembrumFit {

std::vector<PitchTrackPoint> trackPitch(std::span<const float> s,
                                        const SegmentedSample& seg,
                                        double sr) {
    std::vector<PitchTrackPoint> out;
    const std::size_t hop  = static_cast<std::size_t>(0.005 * sr);
    const std::size_t win  = static_cast<std::size_t>(0.020 * sr);
    const std::size_t end  = std::min(s.size(), seg.onsetSample + static_cast<std::size_t>(0.100 * sr));
    for (std::size_t i = seg.onsetSample; i + win < end; i += hop) {
        const float f = estimateFundamental(s.subspan(i, win), sr);
        out.push_back({ static_cast<float>((i - seg.onsetSample) / sr), f });
    }
    return out;
}

bool detectPitchEnvelope(const std::vector<PitchTrackPoint>& t,
                         float& startHz, float& endHz, float& durSec) {
    if (t.size() < 2) return false;
    startHz = t.front().freqHz;
    endHz   = t.front().freqHz;
    float minHz = startHz, maxHz = startHz;
    float minT = 0.0f;
    for (const auto& p : t) {
        if (p.freqHz < minHz) { minHz = p.freqHz; minT = p.timeSec; }
        if (p.freqHz > maxHz) { maxHz = p.freqHz; }
    }
    if (startHz > 0.0f && minHz < startHz * 0.8f && minT < 0.2f) {
        endHz  = minHz;
        durSec = minT;
        return true;
    }
    return false;
}

namespace {

// Centroid trajectory across the whole sound (hop 10 ms). Returns pairs
// (time in sec, centroid in Hz).
struct CentroidTick { float t; float hz; };
std::vector<CentroidTick> centroidTrajectory(std::span<const float> s,
                                             const SegmentedSample& seg,
                                             double sr) {
    std::vector<CentroidTick> out;
    const std::size_t hop = static_cast<std::size_t>(0.010 * sr);
    const std::size_t win = static_cast<std::size_t>(0.040 * sr);
    const std::size_t end = std::min(s.size(), seg.decayEndSample);
    for (std::size_t i = seg.onsetSample; i + win < end; i += hop) {
        const float c = computeSpectralCentroid(s.subspan(i, win), sr);
        out.push_back({ static_cast<float>((i - seg.onsetSample) / sr), c });
    }
    return out;
}

// Fit tau such that centroid(t) = endHz + (startHz - endHz) * exp(-t/tau).
// Returns tau in seconds. Uses coarse log-linear regression over the first
// 0.4 s. Returns a negative value if the trajectory is not monotonically
// decaying (no filter envelope needed).
float fitFilterEnvelopeTauSec(const std::vector<CentroidTick>& traj) {
    if (traj.size() < 3) return -1.0f;
    const float c0 = traj.front().hz;
    float cMin = c0;
    for (const auto& p : traj) cMin = std::min(cMin, p.hz);
    const float thresh = c0 * 0.6f;  // require substantial decay
    if (cMin > thresh) return -1.0f;

    // Find first time the trajectory hits c0/e (natural log decay).
    const float target = cMin + (c0 - cMin) / static_cast<float>(2.71828);
    for (const auto& p : traj) {
        if (p.hz <= target) return std::max(p.t, 1e-3f);
    }
    return -1.0f;
}

// Rough THD: total |spectrum| minus energy at the first 3 harmonics of f1.
float computeTHD(std::span<const float> window, double sr, float f1) {
    if (f1 <= 0.0f || window.empty()) return 0.0f;
    // Reuse estimateFundamental's FFT magnitudes via a local clone.
    const std::size_t n = 2048;
    if (window.size() < n) return 0.0f;
    std::vector<std::complex<float>> buf(n, {0.0f, 0.0f});
    for (std::size_t i = 0; i < n; ++i) buf[i] = { window[i], 0.0f };
    // Simple in-place FFT using stdlib complex; duplicate of features' helper
    // for locality.
    auto fft = [](std::vector<std::complex<float>>& a) {
        const std::size_t M = a.size();
        std::size_t j = 0;
        for (std::size_t i = 1; i < M; ++i) {
            std::size_t bit = M >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (std::size_t len = 2; len <= M; len <<= 1) {
            const float ang = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
            const std::complex<float> wlen(std::cos(ang), std::sin(ang));
            for (std::size_t i = 0; i < M; i += len) {
                std::complex<float> w(1.0f, 0.0f);
                for (std::size_t k = 0; k < len / 2; ++k) {
                    const auto u = a[i + k];
                    const auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    };
    fft(buf);
    const float binHz = static_cast<float>(sr) / static_cast<float>(n);
    float fund = 0.0f, rest = 0.0f;
    for (std::size_t k = 1; k < n / 2; ++k) {
        const float hz = k * binHz;
        const float mag = std::abs(buf[k]);
        const int   harmonic = static_cast<int>(std::round(hz / f1));
        if (harmonic == 1)      fund += mag * mag;
        else if (harmonic >= 2) rest += mag * mag;
    }
    return (fund > 1e-9f) ? std::sqrt(rest) / std::sqrt(fund) : 0.0f;
}

}  // namespace

void fitToneShaper(std::span<const float> samples,
                   const SegmentedSample& seg,
                   double sr,
                   const ModalDecomposition& modes,
                   Membrum::PadConfig& config) {
    // --- Pitch envelope ---
    const auto track = trackPitch(samples, seg, sr);
    float start = 0.0f, end = 0.0f, durSec = 0.0f;
    if (detectPitchEnvelope(track, start, end, durSec)) {
        auto toNorm = [](float hz){
            return static_cast<float>(std::log(std::max(hz, 20.0f) / 20.0f) / std::log(100.0));
        };
        config.tsPitchEnvStart = toNorm(start);
        config.tsPitchEnvEnd   = toNorm(end);
        config.tsPitchEnvTime  = std::clamp(durSec / 0.5f, 0.0f, 1.0f);
        config.tsPitchEnvCurve = 0.0f;  // Exponential
    }

    // --- Filter envelope (closed-form exponential decay fit) ---
    const auto traj = centroidTrajectory(samples, seg, sr);
    const float tauSec = fitFilterEnvelopeTauSec(traj);
    if (tauSec > 0.0f && !traj.empty()) {
        // Enable filter envelope with negative Amount (downward sweep).
        // normalise tau to [0, 1] against 5000 ms.
        const float decNorm = std::clamp(tauSec / 5.0f, 0.0f, 1.0f);
        config.tsFilterEnvAmount = 0.25f;      // -0.5 denormalised (cutoff DROPS)
        config.tsFilterEnvAttack = 0.0f;
        config.tsFilterEnvDecay  = decNorm;
        config.tsFilterEnvSustain = 0.0f;
        config.tsFilterEnvRelease = decNorm;
        // Set starting cutoff from the peak centroid.
        const float peakCentroid = traj.front().hz;
        const float cutoffNorm = std::clamp(std::log(std::max(peakCentroid, 20.0f) / 20.0f) / std::log(1000.0f), 0.0f, 1.0f);
        config.tsFilterCutoff = static_cast<float>(cutoffNorm);
    }

    // --- Drive / fold detection ---
    // Take a 50 ms window 10 ms after onset (past the attack transient).
    const std::size_t anchor = std::min(samples.size() - 1,
        seg.onsetSample + static_cast<std::size_t>(0.010 * sr));
    const std::size_t winLen = std::min<std::size_t>(samples.size() - anchor,
        static_cast<std::size_t>(0.050 * sr));
    const std::span<const float> drvWin(samples.data() + anchor, winLen);
    const float f1 = modes.modes.empty() ? estimateFundamental(drvWin, sr) : modes.modes.front().freqHz;
    const float thd = computeTHD(drvWin, sr, f1);
    if (thd > 0.05f) {
        config.tsDriveAmount = std::clamp((thd - 0.05f) / 0.5f, 0.0f, 1.0f);
    }
    // Fold: detectable when the 3rd-5th harmonics collectively exceed the
    // fundamental. We reuse the same FFT slice.
    {
        const std::size_t n = 2048;
        if (drvWin.size() >= n) {
            std::vector<std::complex<float>> buf(n, {0.0f, 0.0f});
            for (std::size_t i = 0; i < n; ++i) buf[i] = { drvWin[i], 0.0f };
            auto fft = [](std::vector<std::complex<float>>& a) {
                const std::size_t M = a.size();
                std::size_t j = 0;
                for (std::size_t i = 1; i < M; ++i) {
                    std::size_t bit = M >> 1;
                    for (; j & bit; bit >>= 1) j ^= bit;
                    j ^= bit;
                    if (i < j) std::swap(a[i], a[j]);
                }
                for (std::size_t len = 2; len <= M; len <<= 1) {
                    const float ang = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
                    const std::complex<float> wlen(std::cos(ang), std::sin(ang));
                    for (std::size_t i = 0; i < M; i += len) {
                        std::complex<float> w(1.0f, 0.0f);
                        for (std::size_t k = 0; k < len / 2; ++k) {
                            const auto u = a[i + k];
                            const auto v = a[i + k + len / 2] * w;
                            a[i + k] = u + v;
                            a[i + k + len / 2] = u - v;
                            w *= wlen;
                        }
                    }
                }
            };
            fft(buf);
            const float binHz = static_cast<float>(sr) / static_cast<float>(n);
            float fundMag = 0.0f, highHarmMag = 0.0f;
            for (std::size_t k = 1; k < n / 2; ++k) {
                const float hz = k * binHz;
                const float mag = std::abs(buf[k]);
                const int harmonic = (f1 > 0.0f) ? static_cast<int>(std::round(hz / f1)) : 0;
                if (harmonic == 1)       fundMag      += mag;
                else if (harmonic >= 3 && harmonic <= 5) highHarmMag += mag;
            }
            if (fundMag > 1e-9f && highHarmMag > fundMag) {
                config.tsFoldAmount = std::clamp((highHarmMag - fundMag) / fundMag, 0.0f, 1.0f);
            }
        }
    }
}

}  // namespace MembrumFit
