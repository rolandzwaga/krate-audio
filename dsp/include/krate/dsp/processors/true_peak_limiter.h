// ==============================================================================
// Layer 2: DSP Processor - True-Peak Limiter (zero-latency)
// ==============================================================================
// A zero-latency brickwall-style peak limiter that keeps the output at or below
// a configured true-peak ceiling. Detection runs on a 4x-oversampled copy of
// the signal (inter-sample / true-peak) plus the raw sample, and the gain is
// applied to the CURRENT sample with an instantaneous attack and a smoothed
// release. No look-ahead delay, so it adds ZERO latency -- important for an
// instrument with multiple output buses (the main and aux buses stay sample-
// aligned; no host delay-compensation needed).
//
// Guarantee: because the detected peak tp >= |sample| and the gain applied to
// the sample is min(1, ceiling/tp), every output sample satisfies
// |out| <= ceiling exactly (the product is clamped to +/-ceiling so float
// rounding of ceiling/tp cannot push it one ulp over). Inter-sample (true) peaks are bounded to within the
// detector's accuracy; the -1 dBTP default ceiling leaves headroom for the
// residual inter-sample error. The trade-off vs a look-ahead limiter is a
// harder (instantaneous) attack, which for percussive material is acceptable.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, all buffers allocated in prepare())
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0-1: Oversampler)
// - Principle X: DSP Constraints (denormal-safe under FTZ, bounded output)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Zero-latency true-peak limiter (stereo-linked).
///
/// Bounds |output| <= ceiling at the 4x-oversampled (true-peak) resolution with
/// no added latency. Stereo channels share one gain so the image is not pulled
/// off-centre.
class TruePeakLimiter {
public:
    static constexpr float kDefaultCeilingDb = -1.0f;  ///< EBU R128 / streaming true-peak ceiling
    static constexpr float kDefaultReleaseMs = 80.0f;  ///< program-dependent recovery

    TruePeakLimiter() noexcept = default;

    // Non-copyable (holds oversampler filter state + buffers), movable.
    TruePeakLimiter(const TruePeakLimiter&) = delete;
    TruePeakLimiter& operator=(const TruePeakLimiter&) = delete;
    TruePeakLimiter(TruePeakLimiter&&) noexcept = default;
    TruePeakLimiter& operator=(TruePeakLimiter&&) noexcept = default;

    /// @brief Allocate buffers and configure for a sample rate / max block size.
    /// @note NOT real-time safe (allocates). Call before processing.
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept
    {
        sampleRate_   = static_cast<float>(sampleRate);
        maxBlockSize_ = std::max<std::size_t>(maxBlockSize, 1);

        // Zero-latency IIR oversamplers for true-peak detection.
        osL_.prepare(sampleRate, maxBlockSize_,
                     OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
        osR_.prepare(sampleRate, maxBlockSize_,
                     OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
        osBufL_.assign(maxBlockSize_ * 4, 0.0f);
        osBufR_.assign(maxBlockSize_ * 4, 0.0f);

        // G14-1 look-ahead (opt-in): the delay line, the required-gain window (N + 1) and the min window (N).
        lookahead_ = lookaheadRequested_;
        laDelayL_.assign(lookahead_, 0.0f);
        laDelayR_.assign(lookahead_, 0.0f);
        laReq_.assign(lookahead_ > 0 ? lookahead_ + 1 : 0, 1.0f);
        laMin_.assign(lookahead_, 1.0f);

        setCeilingDb(ceilingDb_);
        setReleaseMs(releaseMs_);
        reset();
    }

    /// @brief Clear state (no reallocation).
    void reset() noexcept
    {
        osL_.reset();
        osR_.reset();
        hbL_.reset();
        hbR_.reset();
        currentGain_ = 1.0f;
        std::fill(laDelayL_.begin(), laDelayL_.end(), 0.0f);
        std::fill(laDelayR_.begin(), laDelayR_.end(), 0.0f);
        std::fill(laReq_.begin(), laReq_.end(), 1.0f);
        std::fill(laMin_.begin(), laMin_.end(), 1.0f);
        laPos_ = 0;
        laReqPos_ = 0;
        laMinPos_ = 0;
        laMinSum_ = static_cast<double>(lookahead_);
    }

    /// Profundum Phase 9 T029 (append-only, off by default). When enabled, the true-peak detector
    /// interpolates each channel 4x with the polyphase halfband allpass pair
    /// (primitives/polyphase_halfband_oversampler.h, the same primitive PeriodPeakDetector::truePeak
    /// uses) instead of the IIR Oversampler, at about a fifth of the cost. The sample-level ceiling
    /// guarantee is unchanged (tp >= |sample| still holds; the product is still clamped); the
    /// inter-sample estimate differs within the detector's accuracy. Takes effect at the next sample.
    void setHalfbandDetector(bool enabled) noexcept { halfbandDetector_ = enabled; }
    [[nodiscard]] bool halfbandDetector() const noexcept { return halfbandDetector_; }

    /// Profundum Phase 9 G14-1 (append-only, off by default; user ruling 2026-10-21). A look-ahead of
    /// N samples (0 = off: the zero-latency path above, bit for bit). The audio is delayed by N samples
    /// and the gain applied to each delayed sample is the mean over the last N samples of the running
    /// minimum over the last N + 1 samples of the per-sample required gain min(1, ceiling / tp). Every
    /// term of that mean covers the delayed sample's own requirement, so |out| <= ceiling still holds
    /// sample by sample (the clamp stays), and the gain now ramps over N samples instead of stepping:
    /// the band-limited reconstruction of the output no longer overshoots the ceiling between samples
    /// (the zero-look-ahead path read +1.02 dBTP on a hot asymmetric Fold-100 note, the step being a
    /// clipper). The one-pole release still bounds how fast the gain rises. Configuration-time: takes
    /// effect at the next prepare(), which allocates the delay; clamped to [0, kMaxLookaheadSamples].
    /// Adds N samples of latency: the owner reports lookaheadSamples() to its host.
    static constexpr std::size_t kMaxLookaheadSamples = 1024;
    void setLookaheadSamples(std::size_t samples) noexcept
    {
        lookaheadRequested_ = std::min(samples, kMaxLookaheadSamples);
    }
    /// The look-ahead engaged by the last prepare() (0 when off).
    [[nodiscard]] std::size_t lookaheadSamples() const noexcept { return lookahead_; }

    void setCeilingDb(float db) noexcept
    {
        ceilingDb_  = db;
        ceilingLin_ = std::pow(10.0f, db * 0.05f);
    }

    void setReleaseMs(float ms) noexcept
    {
        releaseMs_ = std::max(ms, 0.1f);
        const float tau = releaseMs_ * 0.001f * sampleRate_;
        releaseCoeff_ = (tau > 0.0f) ? (1.0f - std::exp(-1.0f / tau)) : 1.0f;
    }

    [[nodiscard]] float getCeilingLinear() const noexcept { return ceilingLin_; }

    /// @brief Process a stereo block in place. L/R are limited with a linked,
    /// zero-latency gain. Output appears at the same sample index as input.
    /// Blocks larger than the prepared maxBlockSize are processed in chunks so
    /// the oversampling scratch buffers can never overflow (S1).
    void processBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;
        const int maxChunk = static_cast<int>(maxBlockSize_);
        int offset = 0;
        int remaining = numSamples;
        while (remaining > 0)
        {
            const int chunk = remaining < maxChunk ? remaining : maxChunk;
            processChunk(left + offset, right + offset, chunk);
            offset    += chunk;
            remaining -= chunk;
        }
    }

private:
    void processChunk(float* left, float* right, int numSamples) noexcept
    {
        const std::size_t n = static_cast<std::size_t>(numSamples);

        // 4x oversample each channel for true-peak detection (zero added latency).
        // T029: with the halfband detector the interpolation runs per sample below.
        if (!halfbandDetector_)
        {
            osL_.upsample(left,  osBufL_.data(), n, 0);
            osR_.upsample(right, osBufR_.data(), n, 0);
        }

        for (std::size_t i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = right[i];

            // True-peak = max |.| across this sample's 4 oversampled phases,
            // linked across L/R. Fold in the raw samples so tp >= |sample|
            // always holds, making the sample-level ceiling guarantee exact.
            // (The bus input is already finite -- the per-voice hardClip bounds
            // every voice to [-1, 1] and the coupling return is limited -- so no
            // NaN/Inf sanitisation is needed here; it would also be unreliable
            // under -ffast-math, where the compiler assumes finite values.)
            float tp = std::max(std::fabs(inL), std::fabs(inR));
            if (halfbandDetector_)
            {
                std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> hb{};
                hbL_.upsample(inL, hb.data());
                for (const float v : hb)
                    tp = std::max(tp, std::fabs(v));
                hbR_.upsample(inR, hb.data());
                for (const float v : hb)
                    tp = std::max(tp, std::fabs(v));
            }
            else
            {
                for (std::size_t k = 0; k < 4; ++k)
                {
                    tp = std::max(tp, std::fabs(osBufL_[i * 4 + k]));
                    tp = std::max(tp, std::fabs(osBufR_[i * 4 + k]));
                }
            }

            const float required = (tp > ceilingLin_ && tp > 0.0f)
                                       ? (ceilingLin_ / tp)
                                       : 1.0f;

            if (lookahead_ > 0)
            {
                // G14-1: running minimum of the required gain over the last N + 1 samples (this one
                // included), then its mean over the last N samples; the gain follows that mean down and
                // releases upward through the one-pole. Applied to the sample delayed by N.
                laReq_[laReqPos_] = required;
                laReqPos_ = (laReqPos_ + 1 == laReq_.size()) ? 0 : laReqPos_ + 1;
                float m = 1.0f;
                for (const float v : laReq_)
                    m = std::min(m, v);
                laMinSum_ += static_cast<double>(m) - static_cast<double>(laMin_[laMinPos_]);
                laMin_[laMinPos_] = m;
                laMinPos_ = (laMinPos_ + 1 == lookahead_) ? 0 : laMinPos_ + 1;
                const float avg = static_cast<float>(laMinSum_ / static_cast<double>(lookahead_));
                if (avg < currentGain_)
                    currentGain_ = avg;
                else
                    currentGain_ += (avg - currentGain_) * releaseCoeff_;
                if (currentGain_ > avg)   // the release can not lift the gain above the window's own bound
                    currentGain_ = avg;
                const float dl = laDelayL_[laPos_];
                const float dr = laDelayR_[laPos_];
                laDelayL_[laPos_] = inL;
                laDelayR_[laPos_] = inR;
                laPos_ = (laPos_ + 1 == lookahead_) ? 0 : laPos_ + 1;
                left[i]  = std::clamp(dl * currentGain_, -ceilingLin_, ceilingLin_);
                right[i] = std::clamp(dr * currentGain_, -ceilingLin_, ceilingLin_);
                continue;
            }

            // Instantaneous attack (no look-ahead -> the reduction must be in
            // place on THIS sample to avoid overshoot), one-pole release upward.
            if (required < currentGain_)
                currentGain_ = required;
            else
                currentGain_ += (required - currentGain_) * releaseCoeff_;

            // Clamp: ceilingLin_ / tp * |sample| can round one ulp above the ceiling in
            // float; the clamp keeps the |out| <= ceiling guarantee exact. It is a no-op
            // whenever the gain is 1 (then |sample| <= tp <= ceiling).
            left[i]  = std::clamp(inL * currentGain_, -ceilingLin_, ceilingLin_);
            right[i] = std::clamp(inR * currentGain_, -ceilingLin_, ceilingLin_);
        }
    }

    float sampleRate_   = 44100.0f;
    std::size_t maxBlockSize_ = 512;

    float ceilingDb_  = kDefaultCeilingDb;
    float ceilingLin_ = 0.8912509f;
    float releaseMs_  = kDefaultReleaseMs;
    float releaseCoeff_ = 0.0f;

    Oversampler<4, 1> osL_;
    Oversampler<4, 1> osR_;
    std::vector<float> osBufL_;
    std::vector<float> osBufR_;
    PolyphaseHalfbandOversampler hbL_;   // T029 opt-in detector (4x, heap-free)
    PolyphaseHalfbandOversampler hbR_;
    bool halfbandDetector_ = false;

    // G14-1 opt-in look-ahead (all sized at prepare(); empty when off)
    std::size_t lookaheadRequested_ = 0;
    std::size_t lookahead_ = 0;
    std::vector<float> laDelayL_, laDelayR_;   // the N-sample audio delay
    std::vector<float> laReq_;                 // required gain, last N + 1 samples
    std::vector<float> laMin_;                 // running minimum, last N samples
    std::size_t laPos_ = 0, laReqPos_ = 0, laMinPos_ = 0;
    double laMinSum_ = 0.0;

    float currentGain_ = 1.0f;
};

} // namespace DSP
} // namespace Krate
