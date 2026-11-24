#pragma once

// ==============================================================================
// BodyBank -- Phase 2 swap-in body dispatcher (data-model.md §3.8)
// ==============================================================================
// Owns the shared ModalResonatorBank plus one resident instance of each of the
// 6 body backends, all prepared in prepare() (setup thread). Deferred-swap
// pattern: setBodyModel() stores pendingType_ only; the swap happens inside
// configureForNoteOn() called at the start of DrumVoice::noteOn and only
// re-arms the selected instance, so it never allocates or frees on the audio
// thread (MB-002).
// ==============================================================================

#include "bodies/bell_body.h"
#include "bodies/membrane_body.h"
#include "bodies/noise_body.h"
#include "bodies/plate_body.h"
#include "bodies/shell_body.h"
#include "bodies/string_body.h"
#include "bodies/string_mapper.h"  // refreshStringMaterial() names Bodies::StringMapper
#include "body_model_type.h"
#include "voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>
#include <tuple>
#include <utility>

namespace Membrum {

class BodyBank
{
    // Declared ahead of the public members: a function with a deduced return
    // type must be defined before it is used, also inside the class.
    using Alternatives = std::tuple<
        MembraneBody,
        PlateBody,
        ShellBody,
        StringBody,
        BellBody,
        NoiseBody>;

    // One switch on currentType_ per call; currentType_ is never kCount
    // (applyPendingSwap maps it to Membrane).
    template <typename Fn>
    decltype(auto) dispatch(Fn&& fn) noexcept
    {
        auto& a = alternatives_;
        switch (currentType_)
        {
        case BodyModelType::Plate:     return std::forward<Fn>(fn)(std::get<PlateBody>(a));
        case BodyModelType::Shell:     return std::forward<Fn>(fn)(std::get<ShellBody>(a));
        case BodyModelType::String:    return std::forward<Fn>(fn)(std::get<StringBody>(a));
        case BodyModelType::Bell:      return std::forward<Fn>(fn)(std::get<BellBody>(a));
        case BodyModelType::NoiseBody: return std::forward<Fn>(fn)(std::get<NoiseBody>(a));
        case BodyModelType::Membrane:
        case BodyModelType::kCount:    break;
        }
        return std::forward<Fn>(fn)(std::get<MembraneBody>(a));
    }

public:
    BodyBank() noexcept = default;

    /// Setup thread only: prepares the shared bank and every body (String
    /// sizes its waveguide delay lines here).
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        voiceId_ = voiceId;
        sharedBank_.prepare(sampleRate);
        // Start every body from its constructed state, so each is prepared
        // exactly once, as a freshly swapped-in body used to be (a second
        // prepare() is not idempotent: NoiseBody's SVF keeps the mix targets
        // of the mode it had at its previous prepare(); see ExciterBank).
        alternatives_ = Alternatives{};
        std::apply([sampleRate, voiceId](auto&... b) noexcept {
            (b.prepare(sampleRate, voiceId), ...);
        }, alternatives_);
    }

    void reset() noexcept
    {
        sharedBank_.reset();
        clearSharedModes();
        dispatch([this](auto& b) noexcept { b.reset(sharedBank_); });
        lastOutput_ = 0.0f;
    }

    // Deferred: stores pendingType_ only. Swap happens at next configureForNoteOn().
    void setBodyModel(BodyModelType type) noexcept
    {
        pendingType_ = type;
    }

    void configureForNoteOn(const VoiceCommonParams& params,
                            float pitchEnvStartHz) noexcept
    {
        if (pendingType_ != currentType_)
            applyPendingSwap();

        dispatch([this, &params, pitchEnvStartHz](auto& b) noexcept {
            b.configureForNoteOn(sharedBank_, params, pitchEnvStartHz);
        });
    }

    [[nodiscard]] float processSample(float excitation) noexcept
    {
        const float out = dispatch([this, excitation](auto& b) noexcept {
            return b.processSample(sharedBank_, excitation);
        });
        lastOutput_ = out;
        return out;
    }

    // Block-level entry point (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    // Single dispatch per block. Each body implements
    // processBlock(sharedBank, excitation, out, numSamples) which routes the
    // modal work through ModalResonatorBank::processBlock — the SIMD-accelerated
    // fast path (Highway kernel processModalBankSampleSIMD). This is what makes
    // the per-voice CPU budget meetable at 40 NoiseBody modes.
    //
    //   out        : pointer to numSamples floats to overwrite
    //   excitation : pointer to numSamples input samples
    //   numSamples : block size
    void processBlock(float* out,
                      const float* excitation,
                      int numSamples) noexcept
    {
        dispatch([this, out, excitation, numSamples](auto& b) noexcept {
            b.processBlock(sharedBank_, excitation, out, numSamples);
            if (numSamples > 0)
                lastOutput_ = out[numSamples - 1];
        });
    }

    // Single-dispatch helper: invokes `fn` with a reference to the currently
    // active body alternative and the shared ModalResonatorBank. Callers can
    // then drive the body per-sample using direct typed calls, so the type
    // dispatch happens exactly ONCE per block per voice (research.md §1 /
    // T043 / FR-001, FR-002). Used by DrumVoice::processBlock to run the
    // exciter×body inner sample loop without a per-sample dispatch.
    template <typename Fn>
    void withActive(Fn&& fn) noexcept
    {
        dispatch([this, &fn](auto& b) noexcept { std::forward<Fn>(fn)(b, sharedBank_); });
    }

    void setLastOutput(float y) noexcept { lastOutput_ = y; }

    // Audit H-3: retune the active String waveguide to an absolute pitch (Hz).
    // The String body owns a WaveguideString instead of the shared modal bank,
    // so the pitch-envelope path can't reach it through updateModes(); this
    // routes the per-sample/block pitch into the waveguide's frequency smoother.
    // No-op for every modal body (their pitch is handled via the shared bank).
    void setStringFrequency(float pitchHz) noexcept
    {
        if (currentType_ == BodyModelType::String)
            std::get<StringBody>(alternatives_).string_.setFrequency(pitchHz);
    }

    // Audit M-1: re-apply the String mapper's material-derived settings
    // (brightness / decay) for a live Material Morph WITHOUT re-triggering the
    // note. Frequency and pick position are intentionally left untouched so the
    // morph doesn't fight the pitch-envelope glide. No-op for modal bodies.
    void refreshStringMaterial(const VoiceCommonParams& params) noexcept
    {
        if (currentType_ == BodyModelType::String)
        {
            auto& s = std::get<StringBody>(alternatives_);
            const auto r = Bodies::StringMapper::map(params, 0.0f);
            s.string_.setBrightness(r.brightness);
            s.string_.setDecay(r.decayTime);
        }
    }

    [[nodiscard]] float getLastOutput() const noexcept { return lastOutput_; }

    /// Body-aware silence test for voice retirement (MB-106, MB-160), called
    /// once per rendered block. The String body runs its WaveguideString and
    /// never the shared bank, so the bank's mode count says nothing about it:
    /// it is silent once the waveguide's control energy has stayed below
    /// -100 dBFS RMS for StringBody::kSilentRunMs, counted per sample
    /// (StringBody::isSilent). Every other body is silent once the bank's
    /// flushSilentModes() has retired every mode.
    [[nodiscard]] bool isBodySilent() noexcept
    {
        if (currentType_ == BodyModelType::String)
            return std::get<StringBody>(alternatives_).isSilent();
        return sharedBank_.getNumActiveModes() == 0;
    }

    [[nodiscard]] BodyModelType getCurrentType() const noexcept { return currentType_; }
    [[nodiscard]] BodyModelType getPendingType() const noexcept { return pendingType_; }

    [[nodiscard]] Krate::DSP::ModalResonatorBank& getSharedBank() noexcept
    {
        return sharedBank_;
    }

    [[nodiscard]] const Krate::DSP::ModalResonatorBank& getSharedBank() const noexcept
    {
        return sharedBank_;
    }

private:
    /// Empty the shared bank's mode set (MB-106, MB-039). reset() only clears
    /// the oscillator states, so the previous body's mode count, active flags
    /// and frequencies survived a swap or a kit switch: a String voice on that
    /// slot never retired and offered the old partials to coupling. setModes()
    /// with 0 partials sets numModes_ and numActiveModes_ to 0 and deactivates
    /// every mode. Modal bodies call setModes() again in configureForNoteOn().
    void clearSharedModes() noexcept
    {
        static constexpr float kNone[1] = {0.0f};
        sharedBank_.setModes(kNone, kNone, 0, 1.0f, 0.5f, 0.0f, 0.0f);
    }

    void applyPendingSwap() noexcept
    {
        // Clear shared-bank state before installing the new body so the tail
        // of the previous body doesn't bleed into the new one.
        sharedBank_.reset();
        clearSharedModes();

        if (pendingType_ == BodyModelType::kCount)
            pendingType_ = BodyModelType::Membrane;
        currentType_ = pendingType_;

        // Re-arm the resident body to its post-prepare state without
        // allocating: String re-seeds its waveguide RNG in restart(); every
        // other body's reset() already restores its post-prepare state.
        const std::uint32_t vid = voiceId_;
        dispatch([this, vid](auto& b) noexcept {
            if constexpr (requires { b.restart(vid); })
                b.restart(vid);
            else
                b.reset(sharedBank_);
        });
    }

    Alternatives                     alternatives_{};
    Krate::DSP::ModalResonatorBank   sharedBank_;
    BodyModelType                    currentType_ = BodyModelType::Membrane;
    BodyModelType                    pendingType_ = BodyModelType::Membrane;
    float                            lastOutput_  = 0.0f;
    std::uint32_t                    voiceId_     = 0;
};

} // namespace Membrum
