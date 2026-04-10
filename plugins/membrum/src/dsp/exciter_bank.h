#pragma once

// ==============================================================================
// ExciterBank -- Phase 2 swap-in exciter dispatcher (data-model.md §2.7)
// ==============================================================================
// Holds all 6 exciter alternatives in a std::variant, dispatches via
// std::visit, and implements the deferred-swap pattern (setExciterType writes
// pendingType_ only; the swap happens inside trigger() if pendingType_ differs
// from currentType_).
// ==============================================================================

#include "exciter_type.h"
#include "exciters/feedback_exciter.h"
#include "exciters/fm_impulse_exciter.h"
#include "exciters/friction_exciter.h"
#include "exciters/impulse_exciter.h"
#include "exciters/mallet_exciter.h"
#include "exciters/noise_burst_exciter.h"

#include <cstdint>
#include <variant>

namespace Membrum {

class ExciterBank
{
public:
    ExciterBank() noexcept : active_(std::in_place_type<ImpulseExciter>) {}

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;
        std::visit([sampleRate, voiceId](auto& e) noexcept {
            e.prepare(sampleRate, voiceId);
        }, active_);
    }

    void reset() noexcept
    {
        std::visit([](auto& e) noexcept { e.reset(); }, active_);
    }

    // Deferred: stores pendingType_ only. Swap happens at next trigger().
    void setExciterType(ExciterType type) noexcept
    {
        pendingType_ = type;
    }

    void trigger(float velocity) noexcept
    {
        if (pendingType_ != currentType_)
            applyPendingSwap();
        std::visit([velocity](auto& e) noexcept { e.trigger(velocity); }, active_);
    }

    void release() noexcept
    {
        std::visit([](auto& e) noexcept { e.release(); }, active_);
    }

    [[nodiscard]] float process(float bodyFeedback) noexcept
    {
        return std::visit(
            [bodyFeedback](auto& e) noexcept { return e.process(bodyFeedback); },
            active_);
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return std::visit([](const auto& e) noexcept { return e.isActive(); },
                          active_);
    }

    [[nodiscard]] ExciterType getCurrentType() const noexcept { return currentType_; }
    [[nodiscard]] ExciterType getPendingType() const noexcept { return pendingType_; }

private:
    using Variant = std::variant<
        ImpulseExciter,
        MalletExciter,
        NoiseBurstExciter,
        FrictionExciter,
        FMImpulseExciter,
        FeedbackExciter>;

    void applyPendingSwap() noexcept
    {
        // Emplace the new alternative in-place (no heap), then prepare it so
        // per-voice state and sample rate are valid before trigger().
        switch (pendingType_)
        {
        case ExciterType::Impulse:
            active_.emplace<ImpulseExciter>();
            break;
        case ExciterType::Mallet:
            active_.emplace<MalletExciter>();
            break;
        case ExciterType::NoiseBurst:
            active_.emplace<NoiseBurstExciter>();
            break;
        case ExciterType::Friction:
            active_.emplace<FrictionExciter>();
            break;
        case ExciterType::FMImpulse:
            active_.emplace<FMImpulseExciter>();
            break;
        case ExciterType::Feedback:
            active_.emplace<FeedbackExciter>();
            break;
        case ExciterType::kCount:
            // Invalid — clamp back to Impulse.
            active_.emplace<ImpulseExciter>();
            pendingType_ = ExciterType::Impulse;
            break;
        }
        currentType_ = pendingType_;

        const double sr        = sampleRate_;
        const std::uint32_t vid = voiceId_;
        std::visit([sr, vid](auto& e) noexcept { e.prepare(sr, vid); }, active_);
    }

    Variant      active_;
    ExciterType  currentType_ = ExciterType::Impulse;
    ExciterType  pendingType_ = ExciterType::Impulse;
    double       sampleRate_  = 44100.0;
    std::uint32_t voiceId_    = 0;
};

} // namespace Membrum
