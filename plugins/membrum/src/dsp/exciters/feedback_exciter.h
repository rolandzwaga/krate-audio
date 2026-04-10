#pragma once

// ==============================================================================
// FeedbackExciter -- Phase 2 stub (data-model.md §2.6)
// ==============================================================================
// Structurally-complete stub. Phase 3 (T042) replaces this with the custom
// per-voice energy-limited feedback topology.
// ==============================================================================

#include <cstdint>

namespace Membrum {

struct FeedbackExciter
{
    void prepare(double /*sampleRate*/, std::uint32_t /*voiceId*/) noexcept {}
    void reset() noexcept { active_ = false; }
    void trigger(float /*velocity*/) noexcept { active_ = false; }
    void release() noexcept {}
    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept { return 0.0f; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    bool active_ = false;
};

} // namespace Membrum
