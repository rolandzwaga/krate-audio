#pragma once

// ==============================================================================
// ExciterBank -- Phase 2 swap-in exciter dispatcher (data-model.md §2.7)
// ==============================================================================
// Holds one resident instance of each of the 7 exciter alternatives, all
// prepared in prepare() (setup thread), and dispatches to the current one with
// a single switch per call. Deferred-swap pattern: setExciterType writes
// pendingType_ only; the swap happens inside trigger() if pendingType_ differs
// from currentType_. A swap only re-arms the selected instance to its
// post-prepare state, so it never allocates, frees or builds a wavetable on
// the audio thread (MB-002 / MB-020).
// ==============================================================================

#include "exciter_type.h"
#include "exciters/clap_exciter.h"
#include "exciters/feedback_exciter.h"
#include "exciters/fm_impulse_exciter.h"
#include "exciters/friction_exciter.h"
#include "exciters/impulse_exciter.h"
#include "exciters/mallet_exciter.h"
#include "exciters/noise_burst_exciter.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>

namespace Membrum {

class ExciterBank
{
    // Declared ahead of the public members: a function with a deduced return
    // type must be defined before it is used, also inside the class.
    using Alternatives = std::tuple<
        ImpulseExciter,
        MalletExciter,
        NoiseBurstExciter,
        FrictionExciter,
        FMImpulseExciter,
        FeedbackExciter,
        ClapExciter>;

    // One switch on currentType_ per call; currentType_ is never kCount
    // (applyPendingSwap maps it to Impulse).
    template <typename Self, typename Fn>
    static decltype(auto) dispatchOn(Self& self, Fn&& fn) noexcept
    {
        auto& a = self.alternatives_;
        switch (self.currentType_)
        {
        case ExciterType::Mallet:     return std::forward<Fn>(fn)(std::get<MalletExciter>(a));
        case ExciterType::NoiseBurst: return std::forward<Fn>(fn)(std::get<NoiseBurstExciter>(a));
        case ExciterType::Friction:   return std::forward<Fn>(fn)(std::get<FrictionExciter>(a));
        case ExciterType::FMImpulse:  return std::forward<Fn>(fn)(std::get<FMImpulseExciter>(a));
        case ExciterType::Feedback:   return std::forward<Fn>(fn)(std::get<FeedbackExciter>(a));
        case ExciterType::Clap:       return std::forward<Fn>(fn)(std::get<ClapExciter>(a));
        case ExciterType::Impulse:
        case ExciterType::kCount:     break;
        }
        return std::forward<Fn>(fn)(std::get<ImpulseExciter>(a));
    }

    template <typename Fn>
    decltype(auto) dispatch(Fn&& fn) noexcept { return dispatchOn(*this, std::forward<Fn>(fn)); }

    template <typename Fn>
    decltype(auto) dispatch(Fn&& fn) const noexcept { return dispatchOn(*this, std::forward<Fn>(fn)); }

public:
    ExciterBank() noexcept = default;

    /// Setup thread only: prepares every alternative (FMImpulse builds its
    /// operator wavetables here, Impulse / Mallet size their comb delay).
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;
        // Start every alternative from its constructed state, so each is
        // prepared exactly once, as a freshly swapped-in alternative used to
        // be. A second prepare() is not idempotent for every alternative
        // (Krate::DSP::SVF keeps the mix targets of the mode it had at its
        // previous prepare()), and the host prepares at least twice
        // (initialize, setupProcessing, setActive). The fresh tuple lives on
        // the heap: at ~186 KB it is too large for a host thread's stack.
        const auto fresh = std::make_unique<Alternatives>();
        alternatives_ = std::move(*fresh);
        std::apply([sampleRate, voiceId](auto&... e) noexcept {
            (e.prepare(sampleRate, voiceId), ...);
        }, alternatives_);
        replaySecondaryParams();
    }

    void reset() noexcept
    {
        dispatch([](auto& e) noexcept { e.reset(); });
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
        dispatch([velocity](auto& e) noexcept { e.trigger(velocity); });
    }

    void release() noexcept
    {
        dispatch([](auto& e) noexcept { e.release(); });
    }

    [[nodiscard]] float process(float bodyFeedback) noexcept
    {
        return dispatch(
            [bodyFeedback](auto& e) noexcept { return e.process(bodyFeedback); });
    }

    // Block-level entry point (research.md §1 / T043 / FR-001, FR-002):
    // performs exactly ONE dispatch per block, with the inner sample loop
    // running inside the visitor lambda. This is the hot path the audio
    // thread must use; the per-sample process() above is preserved for unit
    // tests that drive exciters sample-by-sample.
    //
    //   out            : pointer to numSamples floats to overwrite
    //   bodyFeedback   : pointer to numSamples floats (one feedback sample per
    //                    output sample); may be nullptr, in which case 0 is
    //                    used for all samples
    //   numSamples     : block size
    void processBlock(float* out,
                      const float* bodyFeedback,
                      int numSamples) noexcept
    {
        dispatch([out, bodyFeedback, numSamples](auto& e) noexcept {
            if (bodyFeedback != nullptr)
            {
                for (int i = 0; i < numSamples; ++i)
                    out[i] = e.process(bodyFeedback[i]);
            }
            else
            {
                for (int i = 0; i < numSamples; ++i)
                    out[i] = e.process(0.0f);
            }
        });
    }

    // Single-dispatch helper: invokes `fn` with a reference to the currently
    // active exciter alternative. Callers can then process an entire block
    // (or any other multi-sample operation) using direct typed calls, so the
    // type dispatch happens exactly ONCE per block per voice (research.md §1 /
    // T043 / FR-001, FR-002). Used by DrumVoice::processBlock to run the
    // exciter×body inner sample loop without a per-sample dispatch.
    template <typename Fn>
    void withActive(Fn&& fn) noexcept
    {
        dispatch([&fn](auto& e) noexcept { std::forward<Fn>(fn)(e); });
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return dispatch([](const auto& e) noexcept { return e.isActive(); });
    }

    [[nodiscard]] ExciterType getCurrentType() const noexcept { return currentType_; }
    [[nodiscard]] ExciterType getPendingType() const noexcept { return pendingType_; }

    /// Cached normalized NoiseBurst contact-time. Exposed so the DrumVoice
    /// strike-normalisation probe can build a local exciter whose burst length
    /// matches the note that will actually play.
    [[nodiscard]] float getPendingNoiseBurstContactNorm() const noexcept
    {
        return pendingNoiseBurstContactNorm_;
    }

    /// Phase 7: forward the normalized noise-burst contact-time parameter to
    /// the NoiseBurst exciter. Cached so a later swap re-applies it.
    void setNoiseBurstContactMs(float normalized) noexcept
    {
        pendingNoiseBurstContactNorm_ = normalized;
        std::get<NoiseBurstExciter>(alternatives_).setContactMs(normalized);
    }

    /// Phase 4 (audit finding 3): forward the normalized FM Ratio parameter to
    /// the FMImpulse exciter. Cached so a later swap re-applies the host's value.
    void setFMRatio(float normalized) noexcept
    {
        pendingFMRatioNorm_ = normalized;
        std::get<FMImpulseExciter>(alternatives_).setModulatorRatio(normalized);
    }

    /// Phase 4 (audit finding 4): forward the normalized Feedback Amount
    /// parameter to the Feedback exciter.
    void setFeedbackAmount(float normalized) noexcept
    {
        pendingFeedbackNorm_ = normalized;
        std::get<FeedbackExciter>(alternatives_).setFeedbackAmount(normalized);
    }

    /// Phase 4 (audit finding 5): forward the normalized Friction Pressure
    /// parameter to the Friction exciter.
    void setFrictionPressure(float normalized) noexcept
    {
        pendingFrictionNorm_ = normalized;
        std::get<FrictionExciter>(alternatives_).setPressure(normalized);
    }

    /// D4 (06-orchestralKit-fix-plan.md): forward the body-size contact hint
    /// to the Mallet exciter. Cached so a later swap re-applies it.
    void setMalletBodySizeHint(float sizeNorm) noexcept
    {
        pendingMalletSizeHint_ = sizeNorm;
        std::get<MalletExciter>(alternatives_).setBodySizeHint(sizeNorm);
    }

private:
    void replaySecondaryParams() noexcept
    {
        std::get<NoiseBurstExciter>(alternatives_).setContactMs(pendingNoiseBurstContactNorm_);
        std::get<FMImpulseExciter>(alternatives_).setModulatorRatio(pendingFMRatioNorm_);
        std::get<FeedbackExciter>(alternatives_).setFeedbackAmount(pendingFeedbackNorm_);
        std::get<FrictionExciter>(alternatives_).setPressure(pendingFrictionNorm_);
        std::get<MalletExciter>(alternatives_).setBodySizeHint(pendingMalletSizeHint_);
    }

    void applyPendingSwap() noexcept
    {
        if (pendingType_ == ExciterType::kCount)
            pendingType_ = ExciterType::Impulse;  // Invalid -- clamp back to Impulse.
        currentType_ = pendingType_;

        // Re-arm the resident alternative to its post-prepare state without
        // allocating: an alternative with restart() re-seeds what its reset()
        // keeps (Impulse / Mallet: the ImpactExciter RNG); every other
        // alternative's reset() already restores its full post-prepare state.
        const double sr        = sampleRate_;
        const std::uint32_t vid = voiceId_;
        dispatch([sr, vid](auto& e) noexcept {
            if constexpr (requires { e.restart(sr, vid); })
                e.restart(sr, vid);
            else
                e.reset();
        });

        // Replay the secondary exciter parameters so their host automation
        // values survive a type swap.
        replaySecondaryParams();
    }

    Alternatives alternatives_{};
    ExciterType  currentType_ = ExciterType::Impulse;
    ExciterType  pendingType_ = ExciterType::Impulse;
    double       sampleRate_  = 44100.0;
    float        pendingNoiseBurstContactNorm_ = 0.5f;  // Phase 7: cached across swaps.
    // Secondary exciter params, cached across swaps. Defaults reproduce the
    // historical hardcoded values: FM ratio 1.4 (norm 0.13333), feedback and
    // friction default to legacy velocity-only behaviour (norm 0).
    float        pendingFMRatioNorm_  = (1.4f - 1.0f) / 3.0f;
    float        pendingFeedbackNorm_ = 0.0f;
    float        pendingFrictionNorm_ = 0.0f;
    float        pendingMalletSizeHint_ = 1.0f;  // D4: neutral = legacy mass
    std::uint32_t voiceId_    = 0;
};

} // namespace Membrum
