// ==============================================================================
// SpectralDecayEnvelope
// ==============================================================================
// Per-partial spectral decay for natural fade-out when the sidechain confidence
// gate freezes a harmonic frame. Higher partials decay faster than lower ones,
// mimicking acoustic instrument behavior where high-frequency energy dissipates
// first and the fundamental lingers longest.
//
// Usage:
//   prepare(sampleRate, blockSize)  -- precompute decay coefficients
//   activate(frozenFrame)           -- start decaying from given frame
//   processBlock(frame)             -- apply one block of decay in-place
//   isFullyDecayed()                -- true when all partials are below threshold
//   deactivate()                    -- stop and reset
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Innexus {

class SpectralDecayEnvelope
{
public:
    void prepare(double sampleRate, size_t blockSize) noexcept
    {
        blockDurationSec_ = static_cast<float>(blockSize) /
                            static_cast<float>(sampleRate);

        // Precompute per-partial decay coefficients.
        // tau_k = kBaseDecayTimeSec / (1.0 + k * kHighFreqDecayRate)
        // coeff_k = exp(-blockDuration / tau_k)
        for (size_t k = 0; k < Krate::DSP::kMaxPartials; ++k)
        {
            float tau = kBaseDecayTimeSec /
                        (1.0f + static_cast<float>(k) * kHighFreqDecayRate);
            decayCoeffs_[k] = std::exp(-blockDurationSec_ / tau);
        }
    }

    void activate(const Krate::DSP::HarmonicFrame& frame) noexcept
    {
        active_ = true;
        fullyDecayed_ = false;
        numPartials_ = frame.numPartials;
        initialGlobalAmplitude_ = frame.globalAmplitude;

        // Initialize per-partial gains to 1.0 (full amplitude)
        partialGains_.fill(1.0f);

        // Zero-partial frame is immediately fully decayed
        if (numPartials_ <= 0)
            fullyDecayed_ = true;
    }

    void deactivate() noexcept
    {
        active_ = false;
        fullyDecayed_ = false;
    }

    void processBlock(Krate::DSP::HarmonicFrame& frame) noexcept
    {
        if (!active_ || fullyDecayed_)
            return;

        bool allBelowThreshold = true;
        float weightedFreqSum = 0.0f;
        float ampSum = 0.0f;

        for (int p = 0; p < numPartials_; ++p)
        {
            auto idx = static_cast<size_t>(p);

            // Apply per-block exponential decay to partial amplitude
            frame.partials[idx].amplitude *= decayCoeffs_[idx];

            // Track cumulative gain separately for the silence check.
            // On ARM NEON with FTZ (flush-to-zero), small frame amplitudes
            // can be flushed to 0.0f prematurely when the multiply result
            // hits the denormal range. The gain envelope starts at 1.0 and
            // stays in normal float range much longer, giving an accurate
            // picture of whether we've actually reached -80 dBFS.
            partialGains_[idx] *= decayCoeffs_[idx];

            if (partialGains_[idx] >= kSilenceThreshold)
                allBelowThreshold = false;

            float amp = frame.partials[idx].amplitude;

            // Accumulate for centroid calculation
            weightedFreqSum += frame.partials[idx].frequency * amp;
            ampSum += amp;
        }

        // Update globalAmplitude as RMS of decayed partials
        float sumSq = 0.0f;
        for (int p = 0; p < numPartials_; ++p)
        {
            float a = frame.partials[static_cast<size_t>(p)].amplitude;
            sumSq += a * a;
        }
        frame.globalAmplitude = std::sqrt(
            sumSq / std::max(static_cast<float>(numPartials_), 1.0f));

        // Update spectral centroid (amplitude-weighted mean frequency)
        if (ampSum > 1e-10f)
            frame.spectralCentroid = weightedFreqSum / ampSum;

        if (allBelowThreshold)
        {
            fullyDecayed_ = true;
            // Zero out everything cleanly
            for (int p = 0; p < numPartials_; ++p)
                frame.partials[static_cast<size_t>(p)].amplitude = 0.0f;
            frame.globalAmplitude = 0.0f;
        }
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] bool isFullyDecayed() const noexcept { return fullyDecayed_; }

    /// Returns the globalAmplitude of the frame at the time activate() was called.
    /// Used for amplitude-gated recovery during decay.
    [[nodiscard]] float initialAmplitude() const noexcept
    {
        return initialGlobalAmplitude_;
    }

private:
    static constexpr float kSilenceThreshold = 1e-4f; // -80 dBFS

    // Base decay time for the fundamental (seconds)
    static constexpr float kBaseDecayTimeSec = 0.6f;

    // How much faster higher partials decay relative to fundamental.
    // tau_k = kBaseDecayTimeSec / (1.0 + k * kHighFreqDecayRate)
    static constexpr float kHighFreqDecayRate = 0.25f;

    bool active_ = false;
    bool fullyDecayed_ = false;

    // Per-partial decay coefficient (applied per block)
    std::array<float, Krate::DSP::kMaxPartials> decayCoeffs_{};

    // Current per-partial gain (starts at 1.0, decays toward 0)
    std::array<float, Krate::DSP::kMaxPartials> partialGains_{};

    int numPartials_ = 0;
    float blockDurationSec_ = 0.0f;
    float initialGlobalAmplitude_ = 0.0f;
};

} // namespace Innexus
