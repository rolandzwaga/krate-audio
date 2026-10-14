// ==============================================================================
// Layer 1: DSP Primitive - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// RetriggerDeclick: the click-free Reset-restart gain (D-8, FR-041-FR-045).
// Includes Layer 0 only.
//   Spec:  specs/profundum-phase4-drop-note-behaviour/spec.md
//   Plan:  specs/profundum-phase4-drop-note-behaviour/plan.md (S7, S11.1)
//   Carries: SC-011 (RetriggerDeclick_*), SC-017, SC-019.
//
// Gain law (plan S1 C-15): after arm(), sample k = 0 .. L_d - 1 carries
//   g[k] = 0.5 (1 + cos(pi (k + 1) / L_d)),  L_d = ceil(kDeclickMs * fs / 1000),
// with the switch sample k = L_d - 1 written exactly 0.0f; the gain returns to
// 1.0f on the following sample. The cosine comes from a double rotation
// recurrence seeded at arm(). A dip never restarts (FR-043), so g0 = 1.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace Krate::DSP {

class RetriggerDeclick {
public:
    static constexpr float kDeclickMs = 1.5f;
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;

    /// L_d = ceil(kDeclickMs * fs / 1000) with fs clamped to [22050, 192000],
    /// then reset(). A non-finite rate is a no-op (tasks.md deviation 7).
    void prepare(double sampleRate) noexcept {
        if (!detail::isFinite(sampleRate))
            return;
        const double fs = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
        length_ = static_cast<std::size_t>(
            std::ceil(static_cast<double>(kDeclickMs) * fs / 1000.0));
        const double step = std::numbers::pi / static_cast<double>(length_);
        rotCos_ = std::cos(step);
        rotSin_ = std::sin(step);
        prepared_ = true;
        reset();
    }

    /// Gain 1, idle.
    void reset() noexcept {
        active_ = false;
        remaining_ = 0;
        cos_ = 1.0;
        sin_ = 0.0;
    }

    /// Start the dip. No-op before prepare() and while a dip runs (FR-043).
    void arm() noexcept {
        if (!prepared_ || active_)
            return;
        active_ = true;
        remaining_ = length_ - 1;
        cos_ = rotCos_; // cos(pi * 1 / L_d): the angle of sample k = 0
        sin_ = rotSin_;
    }

    /// Samples to render before the switch sample; 0 at the switch and when idle.
    [[nodiscard]] std::size_t samplesUntilSwitch() const noexcept {
        return active_ ? remaining_ : 0;
    }

    /// Gain for this sample (C-15). Exactly 1.0f when idle.
    [[nodiscard]] float process() noexcept {
        if (!active_)
            return 1.0f;
        if (remaining_ == 0) {
            active_ = false;
            return 0.0f;
        }
        const float g = static_cast<float>(0.5 * (1.0 + cos_));
        const double c = cos_ * rotCos_ - sin_ * rotSin_;
        sin_ = sin_ * rotCos_ + cos_ * rotSin_;
        cos_ = c;
        --remaining_;
        return g;
    }

    /// Writes n gain samples; (nullptr, n) and (p, 0) are no-ops.
    void processBlock(float* gain, std::size_t n) noexcept {
        if (gain == nullptr)
            return;
        for (std::size_t i = 0; i < n; ++i)
            gain[i] = process();
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    double rotCos_ = 1.0;
    double rotSin_ = 0.0;
    double cos_ = 1.0;
    double sin_ = 0.0;
    std::size_t length_ = 0;
    std::size_t remaining_ = 0;
    bool prepared_ = false;
    bool active_ = false;
};

} // namespace Krate::DSP
