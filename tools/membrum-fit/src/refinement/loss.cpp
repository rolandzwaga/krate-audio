#include "loss.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace MembrumFit {

namespace {

void fftInPlace(std::vector<std::complex<float>>& x) {
    const std::size_t n = x.size();
    if (n < 2) return;
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

std::vector<float> hannWindow(std::size_t n) {
    std::vector<float> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f * i / (n - 1)));
    return w;
}

float singleScaleSTFT(std::span<const float> a, std::span<const float> b, std::size_t fftN) {
    const std::size_t hop = fftN / 4;
    const auto w = hannWindow(fftN);
    const std::size_t len = std::min(a.size(), b.size());
    if (len < fftN) return 0.0f;
    std::vector<std::complex<float>> bufA(fftN), bufB(fftN);
    double sum = 0.0; std::size_t frames = 0;
    for (std::size_t off = 0; off + fftN <= len; off += hop) {
        for (std::size_t i = 0; i < fftN; ++i) {
            bufA[i] = { a[off + i] * w[i], 0.0f };
            bufB[i] = { b[off + i] * w[i], 0.0f };
        }
        fftInPlace(bufA);
        fftInPlace(bufB);
        for (std::size_t k = 0; k < fftN / 2; ++k) {
            const float ma = std::log1p(std::abs(bufA[k]));
            const float mb = std::log1p(std::abs(bufB[k]));
            sum += std::abs(ma - mb);
        }
        ++frames;
    }
    return (frames > 0) ? static_cast<float>(sum / static_cast<double>(frames * (fftN / 2))) : 0.0f;
}

}  // namespace

float computeMSSLoss(std::span<const float> target, std::span<const float> candidate) {
    const std::array<std::size_t, 6> sizes = { 64, 128, 256, 512, 1024, 2048 };
    double total = 0.0;
    for (auto n : sizes) total += singleScaleSTFT(target, candidate, n);
    return static_cast<float>(total / static_cast<double>(sizes.size()));
}

std::vector<float> computeMFCC(std::span<const float> signal, double sampleRate) {
    // Simple mel-filterbank-based MFCC (single-frame, full-signal). Used only
    // to get a coarse timbral fingerprint for the loss; not full Slaney MFCC.
    if (signal.empty()) return {};
    constexpr int    kNumMel  = 26;
    constexpr float  kMelLow  = 0.0f;
    constexpr std::size_t kFFTN = 2048;
    std::vector<std::complex<float>> buf(kFFTN, {0.0f, 0.0f});
    const std::size_t take = std::min<std::size_t>(signal.size(), kFFTN);
    const auto w = hannWindow(take);
    for (std::size_t i = 0; i < take; ++i) buf[i] = { signal[i] * w[i], 0.0f };
    fftInPlace(buf);
    std::vector<float> mag(kFFTN / 2);
    for (std::size_t k = 0; k < mag.size(); ++k) mag[k] = std::abs(buf[k]);

    auto hz2mel = [](float hz){ return 2595.0f * std::log10(1.0f + hz / 700.0f); };
    auto mel2hz = [](float m) { return 700.0f * (std::pow(10.0f, m / 2595.0f) - 1.0f); };

    const float melHigh = hz2mel(static_cast<float>(sampleRate * 0.5));
    const float melStep = (melHigh - kMelLow) / static_cast<float>(kNumMel + 1);
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(kFFTN);

    std::vector<float> melEnergies(kNumMel, 0.0f);
    for (int m = 0; m < kNumMel; ++m) {
        const float mLo  = mel2hz(kMelLow + m * melStep);
        const float mMid = mel2hz(kMelLow + (m + 1) * melStep);
        const float mHi  = mel2hz(kMelLow + (m + 2) * melStep);
        for (std::size_t k = 0; k < mag.size(); ++k) {
            const float f = k * binHz;
            float w = 0.0f;
            if (f >= mLo && f <= mMid) w = (f - mLo) / std::max(mMid - mLo, 1e-6f);
            else if (f > mMid && f <= mHi) w = (mHi - f) / std::max(mHi - mMid, 1e-6f);
            melEnergies[m] += w * mag[k];
        }
        melEnergies[m] = std::log(std::max(melEnergies[m], 1e-9f));
    }
    // DCT-II
    std::vector<float> mfcc(kMfccCoeffCount, 0.0f);
    for (int n = 0; n < kMfccCoeffCount; ++n) {
        float s = 0.0f;
        for (int m = 0; m < kNumMel; ++m) {
            s += melEnergies[m]
               * std::cos(3.14159265358979323846f * n * (2 * m + 1) / (2 * kNumMel));
        }
        mfcc[n] = s;
    }
    return mfcc;
}

float computeMFCCL1(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) s += std::abs(a[i] - b[i]);
    return static_cast<float>(s / static_cast<double>(a.size()));
}

std::vector<float> computeLogEnvelope(std::span<const float> signal, double sampleRate) {
    // Coarse rectified-and-low-passed envelope sampled at 200 Hz.
    if (signal.empty() || sampleRate <= 0.0) return {};
    const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(sampleRate / 200.0));
    std::vector<float> env;
    env.reserve(signal.size() / hop + 1);
    for (std::size_t i = 0; i < signal.size(); i += hop) {
        const std::size_t end = std::min(signal.size(), i + hop);
        float e = 0.0f;
        for (std::size_t j = i; j < end; ++j) e += signal[j] * signal[j];
        env.push_back(std::log(std::max(std::sqrt(e / static_cast<float>(end - i)), 1e-9f)));
    }
    return env;
}

float computeEnvelopeL1(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) return 0.0f;
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += std::abs(a[i] - b[i]);
    return static_cast<float>(s / static_cast<double>(n));
}

float totalLoss(std::span<const float> target,
                const std::vector<float>& tMfcc,
                const std::vector<float>& tEnv,
                std::span<const float> candidate,
                double sampleRate,
                LossWeights w) {
    const float lStft = computeMSSLoss(target, candidate);
    const auto cMfcc  = computeMFCC(candidate, sampleRate);
    const auto cEnv   = computeLogEnvelope(candidate, sampleRate);
    const float lMfcc = tMfcc.empty()
        ? computeMFCCL1(computeMFCC(target, sampleRate), cMfcc)
        : computeMFCCL1(tMfcc, cMfcc);
    const float lEnv  = tEnv.empty()
        ? computeEnvelopeL1(computeLogEnvelope(target, sampleRate), cEnv)
        : computeEnvelopeL1(tEnv, cEnv);
    return w.stft * lStft + w.mfcc * lMfcc + w.env * lEnv;
}

}  // namespace MembrumFit
