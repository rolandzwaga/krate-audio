// ==============================================================================
// Layer 2: DSP Processor - True-Peak Brickwall Limiter
// ==============================================================================
// A look-ahead brickwall limiter that guarantees the output stays at or below a
// configured true-peak ceiling. Detection runs on a 4x-oversampled copy of the
// signal (inter-sample / true-peak), so the ceiling holds for the reconstructed
// analog peak, not just the sample peak.
//
// Design (standard look-ahead brickwall):
//   - Audio is delayed by `lookahead` samples.
//   - For each incoming sample the required gain r = min(1, ceiling/truePeak) is
//     pushed into a window of length `lookahead`.
//   - The gain applied to the (delayed) output sample is the MINIMUM required
//     gain over the look-ahead window, so the reduction is fully in place by the
//     time the peak reaches the output -> no overshoot (brickwall).
//   - Gain attacks instantly (the look-ahead provides the time alignment) and
//     releases with a one-pole upward smoother to avoid pumping.
//
// True-peak detection uses a zero-latency IIR 4x oversampler, so the detector
// adds NO latency; the only reported latency is the look-ahead.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, all buffers allocated in prepare())
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0-1: Oversampler, db_utils)
// - Principle X: DSP Constraints (denormal-safe, bounded output)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/oversampler.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Look-ahead true-peak brickwall limiter (stereo-linked).
///
/// Guarantees |output| <= ceiling at the 4x-oversampled (true-peak) resolution.
/// Stereo channels share one gain so the stereo image is not pulled off-centre.
class TruePeakLimiter {
public:
    static constexpr float kDefaultCeilingDb   = -1.0f;  ///< EBU R128 / streaming true-peak ceiling
    static constexpr float kDefaultLookaheadMs = 1.0f;   ///< transient-preserving look-ahead
    static constexpr float kDefaultReleaseMs   = 80.0f;  ///< program-dependent recovery
    static constexpr float kMaxLookaheadMs     = 5.0f;   ///< caps the pre-allocated delay

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

        // Zero-latency IIR oversamplers for true-peak detection (no added latency).
        osL_.prepare(sampleRate, maxBlockSize_,
                     OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
        osR_.prepare(sampleRate, maxBlockSize_,
                     OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
        osBufL_.assign(maxBlockSize_ * 4, 0.0f);
        osBufR_.assign(maxBlockSize_ * 4, 0.0f);

        // Look-ahead delay length (>= 1 sample). The gain window and the audio
        // delay share this length.
        const std::size_t maxLook = static_cast<std::size_t>(
            std::ceil(kMaxLookaheadMs * 0.001f * sampleRate_)) + 1;
        delayL_.assign(maxLook, 0.0f);
        delayR_.assign(maxLook, 0.0f);
        grWindow_.assign(maxLook, 1.0f);

        setCeilingDb(ceilingDb_);
        setLookaheadMs(lookaheadMs_);
        setReleaseMs(releaseMs_);
        reset();
    }

    /// @brief Clear state (no reallocation).
    void reset() noexcept
    {
        osL_.reset();
        osR_.reset();
        std::fill(delayL_.begin(), delayL_.end(), 0.0f);
        std::fill(delayR_.begin(), delayR_.end(), 0.0f);
        std::fill(grWindow_.begin(), grWindow_.end(), 1.0f);
        writeIdx_     = 0;
        currentGain_  = 1.0f;
    }

    void setCeilingDb(float db) noexcept
    {
        ceilingDb_  = db;
        ceilingLin_ = std::pow(10.0f, db * 0.05f);
    }

    void setLookaheadMs(float ms) noexcept
    {
        lookaheadMs_ = std::clamp(ms, 0.0f, kMaxLookaheadMs);
        lookahead_   = static_cast<std::size_t>(
            std::lround(lookaheadMs_ * 0.001f * sampleRate_));
        // Window length = look-ahead samples (>=1 so there is always a delay slot).
        lookahead_ = std::max<std::size_t>(lookahead_, 1);
        if (lookahead_ > grWindow_.size())
            lookahead_ = grWindow_.size();
    }

    void setReleaseMs(float ms) noexcept
    {
        releaseMs_ = std::max(ms, 0.1f);
        const float tau = releaseMs_ * 0.001f * sampleRate_;
        releaseCoeff_ = (tau > 0.0f) ? (1.0f - std::exp(-1.0f / tau)) : 1.0f;
    }

    /// @brief Reported plugin latency in samples (== look-ahead). Detection adds none.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept { return lookahead_; }

    [[nodiscard]] float getCeilingLinear() const noexcept { return ceilingLin_; }

    /// @brief Process a stereo block in place. L/R are limited with a linked gain.
    void processBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;
        const std::size_t n = static_cast<std::size_t>(numSamples);

        // 4x oversample each channel for true-peak detection (zero added latency).
        osL_.upsample(left,  osBufL_.data(), n, 0);
        osR_.upsample(right, osBufR_.data(), n, 0);

        const std::size_t cap = lookahead_;
        for (std::size_t i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = right[i];

            // True-peak = max |.| across this sample's 4 oversampled phases,
            // linked across L/R. Include the raw samples too so tp >= |sample|
            // always holds (the IIR detector phases need not contain the raw
            // value); this makes the sample-level ceiling guarantee exact.
            float tp = std::max(std::fabs(inL), std::fabs(inR));
            for (std::size_t k = 0; k < 4; ++k)
            {
                tp = std::max(tp, std::fabs(osBufL_[i * 4 + k]));
                tp = std::max(tp, std::fabs(osBufR_[i * 4 + k]));
            }

            const float required = (tp > ceilingLin_ && tp > 0.0f)
                                       ? (ceilingLin_ / tp)
                                       : 1.0f;

            // Output sample = the one delayed by `cap` (oldest in the ring,
            // currently at writeIdx_ before we overwrite it).
            const float outL = delayL_[writeIdx_];
            const float outR = delayR_[writeIdx_];

            // Look-ahead window minimum over [k-cap .. k]: the `cap` stored
            // required-gains (which include the output sample's own) plus this
            // sample's `required`. This guarantees the gain is already reduced
            // by the time the loud sample reaches the output -> no overshoot.
            float target = required;
            for (std::size_t w = 0; w < cap; ++w)
                target = std::min(target, grWindow_[w]);

            // Instant attack (look-ahead aligns it), one-pole release upward.
            if (target < currentGain_)
                currentGain_ = target;
            else
                currentGain_ += (target - currentGain_) * releaseCoeff_;

            // Push the new INPUT sample + its required gain into the ring.
            delayL_[writeIdx_]   = inL;
            delayR_[writeIdx_]   = inR;
            grWindow_[writeIdx_] = required;
            writeIdx_ = (writeIdx_ + 1) % cap;

            // Emit the delayed sample with the look-ahead-aligned gain.
            left[i]  = outL * currentGain_;
            right[i] = outR * currentGain_;
        }
    }

private:
    float sampleRate_   = 44100.0f;
    std::size_t maxBlockSize_ = 512;

    float ceilingDb_  = kDefaultCeilingDb;
    float ceilingLin_ = 0.8912509f;
    float lookaheadMs_ = kDefaultLookaheadMs;
    std::size_t lookahead_ = 1;
    float releaseMs_ = kDefaultReleaseMs;
    float releaseCoeff_ = 0.0f;

    Oversampler<4, 1> osL_;
    Oversampler<4, 1> osR_;
    std::vector<float> osBufL_;
    std::vector<float> osBufR_;

    std::vector<float> delayL_;
    std::vector<float> delayR_;
    std::vector<float> grWindow_;
    std::size_t writeIdx_ = 0;
    float currentGain_ = 1.0f;
};

} // namespace DSP
} // namespace Krate
