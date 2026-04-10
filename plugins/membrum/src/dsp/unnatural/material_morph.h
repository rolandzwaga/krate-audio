#pragma once

// ==============================================================================
// MaterialMorph -- Phase 2 no-op stub (data-model.md §7)
// ==============================================================================

#include <cstdint>

namespace Membrum {

class MaterialMorph
{
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void reset() noexcept { currentValue_ = 0.0f; }

    void setEnabled(bool on) noexcept { enabled_ = on; }
    void setStart(float material01) noexcept { startMaterial_ = material01; }
    void setEnd(float material01) noexcept { endMaterial_ = material01; }
    void setDurationMs(float ms) noexcept { durationMs_ = ms; }
    void setCurve(bool exponential) noexcept { exponential_ = exponential; }

    void trigger() noexcept {}
    [[nodiscard]] float process() noexcept { return currentValue_; }
    [[nodiscard]] bool isActive() const noexcept { return false; }

private:
    bool   enabled_       = false;
    float  startMaterial_ = 1.0f;
    float  endMaterial_   = 0.0f;
    float  durationMs_    = 200.0f;
    bool   exponential_   = false;
    float  currentValue_  = 0.0f;
    double sampleRate_    = 44100.0;
};

} // namespace Membrum
