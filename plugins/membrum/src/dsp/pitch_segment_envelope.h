#pragma once

// ==============================================================================
// PitchSegmentEnvelope -- Membrum-local 1- or 2-segment pitch envelope
// ==============================================================================
// Replaces the single-segment Krate::DSP::MultiStageEnvelope previously used
// by ToneShaper for pitch sweeps. The new shape supports an optional middle
// breakpoint with per-segment continuous curve shaping ([-1, +1], 0 = linear,
// +1 = strongly exponential, -1 = strongly logarithmic), reusing the Layer-0
// power-curve table used by the editor for visual <-> audible parity.
//
// Output is in Hz, ready to feed straight into the modal body's f0 update.
//
// Real-time safety:
//   - process() does only LUT lookup + linear interpolation (no transcendentals).
//   - configure*() rebuilds the LUTs via generatePowerCurveTable() at user-edit
//     time, never per-sample.
// ==============================================================================

#include <krate/dsp/core/curve_table.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace Membrum {

class PitchSegmentEnvelope
{
public:
    void prepare(float sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 44100.0f;
        // Default to a do-nothing single segment (both endpoints equal) so that
        // process() returns a stable value before configure*() is called.
        configureSingle(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void reset() noexcept
    {
        phaseSamples_ = 0;
        active_       = false;
    }

    void noteOn() noexcept
    {
        phaseSamples_ = 0;
        active_       = totalSamples_ > 0;
    }

    void noteOff() noexcept
    {
        // Drum-style: no sustain phase. noteOff is a no-op; the envelope keeps
        // running through its full duration regardless of gate.
    }

    /// Configure a single segment Start -> End over totalMs with the given
    /// continuous curve amount in [-1, +1] (0 = linear). Used in legacy /
    /// knee-disabled mode.
    void configureSingle(float startHz,
                         float endHz,
                         float totalMs,
                         float curveAmount) noexcept
    {
        const int totalN = msToSamples(totalMs);
        seg1Samples_     = totalN;
        seg2Samples_     = 0;
        totalSamples_    = totalN;
        endValue_        = endHz;

        Krate::DSP::generatePowerCurveTable(
            seg1Table_, std::clamp(curveAmount, -1.0f, 1.0f), startHz, endHz);
    }

    /// Configure a 2-segment Start -> Mid -> End envelope.
    /// midFraction in [0, 1] is the portion of totalMs spent in segment 1.
    /// Each segment carries its own continuous curve amount in [-1, +1].
    void configureThreePoint(float startHz,
                             float midHz,
                             float endHz,
                             float totalMs,
                             float midFraction,
                             float curve1Amount,
                             float curve2Amount) noexcept
    {
        const int totalN = msToSamples(totalMs);
        const float frac = std::clamp(midFraction, 0.0f, 1.0f);

        seg1Samples_  = static_cast<int>(static_cast<float>(totalN) * frac);
        seg2Samples_  = totalN - seg1Samples_;
        totalSamples_ = totalN;
        endValue_     = endHz;

        Krate::DSP::generatePowerCurveTable(
            seg1Table_, std::clamp(curve1Amount, -1.0f, 1.0f), startHz, midHz);
        Krate::DSP::generatePowerCurveTable(
            seg2Table_, std::clamp(curve2Amount, -1.0f, 1.0f), midHz,   endHz);
    }

    /// Return the current envelope value (in Hz). When the envelope has fully
    /// elapsed, sticks at endValue_. Safe to call before noteOn() -- returns
    /// endValue_ in that case (active_ is false).
    [[nodiscard]] float process() noexcept
    {
        if (!active_ || totalSamples_ <= 0)
            return endValue_;

        const int p = phaseSamples_;
        float out;
        if (p < seg1Samples_)
        {
            const float phase = (seg1Samples_ > 0)
                ? static_cast<float>(p) / static_cast<float>(seg1Samples_)
                : 0.0f;
            out = Krate::DSP::lookupCurveTable(seg1Table_, phase);
        }
        else if (p < totalSamples_ && seg2Samples_ > 0)
        {
            const float phase =
                static_cast<float>(p - seg1Samples_)
                / static_cast<float>(seg2Samples_);
            out = Krate::DSP::lookupCurveTable(seg2Table_, phase);
        }
        else
        {
            out      = endValue_;
            active_  = false;
        }

        if (active_)
            ++phaseSamples_;

        return out;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] int  totalSamples() const noexcept { return totalSamples_; }
    [[nodiscard]] int  seg1Samples()  const noexcept { return seg1Samples_; }
    [[nodiscard]] int  seg2Samples()  const noexcept { return seg2Samples_; }

private:
    [[nodiscard]] int msToSamples(float ms) const noexcept
    {
        if (ms <= 0.0f) return 0;
        return static_cast<int>(ms * sampleRate_ * 0.001f + 0.5f);
    }

    float sampleRate_ = 44100.0f;

    // Power-curve LUTs covering each segment's Hz trajectory. generatePowerCurveTable
    // bakes startLevel/endLevel into the table so lookup is a direct Hz read.
    std::array<float, Krate::DSP::kCurveTableSize> seg1Table_{};
    std::array<float, Krate::DSP::kCurveTableSize> seg2Table_{};

    int   seg1Samples_  = 0;
    int   seg2Samples_  = 0;
    int   totalSamples_ = 0;
    int   phaseSamples_ = 0;
    float endValue_     = 0.0f;
    bool  active_       = false;
};

} // namespace Membrum
