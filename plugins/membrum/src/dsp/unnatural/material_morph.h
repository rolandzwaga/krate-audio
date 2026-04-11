#pragma once

// ==============================================================================
// MaterialMorph -- Phase 2.E.5 (data-model.md §7, unnatural_zone_contract.md)
// ==============================================================================
// Per-voice time-varying envelope that morphs the Material parameter from
// startMaterial_ to endMaterial_ over durationMs_ milliseconds. Triggered on
// every note-on.
//
// Disabled state (FR-054): when enabled_ is false OR totalSamples_ == 0, the
// process() function returns the startMaterial_ value unchanged (acting as the
// "static Material" pass-through) so no morph processing happens.
//
// Defaults-off guarantee (FR-055): with enabled_ = false, MaterialMorph is a
// no-op; DrumVoice uses the DrumVoice's own material_ parameter instead of
// MaterialMorph::process().
//
// Real-time safety (FR-056): pure scalar math. No allocation. No RNG. No
// branching on block-size inputs.
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

class MaterialMorph
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        recomputeTotalSamples();
        reset();
    }

    void reset() noexcept
    {
        sampleCounter_ = 0.0f;
        currentValue_  = startMaterial_;
    }

    void setEnabled(bool on) noexcept { enabled_ = on; }

    void setStart(float material01) noexcept
    {
        startMaterial_ = std::clamp(material01, 0.0f, 1.0f);
    }

    void setEnd(float material01) noexcept
    {
        endMaterial_ = std::clamp(material01, 0.0f, 1.0f);
    }

    void setDurationMs(float ms) noexcept
    {
        // Clamp duration to minimum 1 sample to prevent divide-by-zero.
        durationMs_ = (ms < 0.0f) ? 0.0f : ms;
        recomputeTotalSamples();
    }

    void setCurve(bool exponential) noexcept { exponential_ = exponential; }

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// Trigger resets the envelope for a new note. Called from DrumVoice::noteOn().
    void trigger() noexcept
    {
        sampleCounter_ = 0.0f;
        currentValue_  = startMaterial_;
    }

    /// Process one sample: advance the envelope and return the current
    /// morph value in [0, 1]. Called once per audio sample from DrumVoice.
    [[nodiscard]] float process() noexcept
    {
        // FR-054 disabled: return a static value. DrumVoice should instead
        // read its own material_ parameter when enabled_ is false, but we
        // return startMaterial_ here for safety.
        if (!enabled_ || totalSamples_ <= 0.0f)
            return startMaterial_;

        // Completed: hold at end material.
        if (sampleCounter_ >= totalSamples_)
        {
            currentValue_ = endMaterial_;
            return currentValue_;
        }

        const float t = sampleCounter_ / totalSamples_;
        if (exponential_)
        {
            // Exponential morph: start * (end/start)^t.
            // Clamp start to a small positive to avoid divide-by-zero.
            const float startSafe = std::max(startMaterial_, 1e-6f);
            const float endSafe   = std::max(endMaterial_,   1e-6f);
            currentValue_ = startSafe * std::pow(endSafe / startSafe, t);
        }
        else
        {
            // Linear lerp: start + (end - start) * t.
            currentValue_ = startMaterial_ + (endMaterial_ - startMaterial_) * t;
        }

        sampleCounter_ += 1.0f;
        return currentValue_;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return enabled_ && totalSamples_ > 0.0f && sampleCounter_ < totalSamples_;
    }

    [[nodiscard]] float getCurrentValue() const noexcept { return currentValue_; }

private:
    void recomputeTotalSamples() noexcept
    {
        if (durationMs_ <= 0.0f || sampleRate_ <= 0.0)
        {
            totalSamples_ = 0.0f;
            return;
        }
        const float n = durationMs_ * static_cast<float>(sampleRate_) * 0.001f;
        // Clamp to minimum 1 sample to prevent divide-by-zero.
        totalSamples_ = (n < 1.0f) ? 1.0f : n;
    }

    bool   enabled_       = false;
    float  startMaterial_ = 1.0f;
    float  endMaterial_   = 0.0f;
    float  durationMs_    = 200.0f;
    bool   exponential_   = false;
    float  currentValue_  = 1.0f;
    float  sampleCounter_ = 0.0f;
    float  totalSamples_  = 0.0f;
    double sampleRate_    = 44100.0;
};

} // namespace Membrum
