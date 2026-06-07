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
// |out| <= ceiling exactly. Inter-sample (true) peaks are bounded to within the
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

#include <algorithm>
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

        setCeilingDb(ceilingDb_);
        setReleaseMs(releaseMs_);
        reset();
    }

    /// @brief Clear state (no reallocation).
    void reset() noexcept
    {
        osL_.reset();
        osR_.reset();
        currentGain_ = 1.0f;
    }

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
    void processBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;
        const std::size_t n = static_cast<std::size_t>(numSamples);

        // 4x oversample each channel for true-peak detection (zero added latency).
        osL_.upsample(left,  osBufL_.data(), n, 0);
        osR_.upsample(right, osBufR_.data(), n, 0);

        for (std::size_t i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = right[i];

            // True-peak = max |.| across this sample's 4 oversampled phases,
            // linked across L/R. Fold in the raw samples so tp >= |sample|
            // always holds, making the sample-level ceiling guarantee exact.
            float tp = std::max(std::fabs(inL), std::fabs(inR));
            for (std::size_t k = 0; k < 4; ++k)
            {
                tp = std::max(tp, std::fabs(osBufL_[i * 4 + k]));
                tp = std::max(tp, std::fabs(osBufR_[i * 4 + k]));
            }

            const float required = (tp > ceilingLin_ && tp > 0.0f)
                                       ? (ceilingLin_ / tp)
                                       : 1.0f;

            // Instantaneous attack (no look-ahead -> the reduction must be in
            // place on THIS sample to avoid overshoot), one-pole release upward.
            if (required < currentGain_)
                currentGain_ = required;
            else
                currentGain_ += (required - currentGain_) * releaseCoeff_;

            left[i]  = inL * currentGain_;
            right[i] = inR * currentGain_;
        }
    }

private:
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

    float currentGain_ = 1.0f;
};

} // namespace DSP
} // namespace Krate
