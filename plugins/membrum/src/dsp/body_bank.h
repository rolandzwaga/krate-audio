#pragma once

// ==============================================================================
// BodyBank -- Phase 2 swap-in body dispatcher (data-model.md §3.8)
// ==============================================================================
// Owns the shared ModalResonatorBank plus a std::variant of 6 body backends.
// Deferred-swap pattern: setBodyModel() stores pendingType_ only; the swap
// happens inside configureForNoteOn() called at the start of DrumVoice::noteOn.
// ==============================================================================

#include "bodies/bell_body.h"
#include "bodies/membrane_body.h"
#include "bodies/noise_body.h"
#include "bodies/plate_body.h"
#include "bodies/shell_body.h"
#include "bodies/string_body.h"
#include "body_model_type.h"
#include "voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <cstdint>
#include <variant>

namespace Membrum {

class BodyBank
{
public:
    BodyBank() noexcept : active_(std::in_place_type<MembraneBody>) {}

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;
        sharedBank_.prepare(sampleRate);
        std::visit([sampleRate, voiceId](auto& b) noexcept {
            b.prepare(sampleRate, voiceId);
        }, active_);
    }

    void reset() noexcept
    {
        sharedBank_.reset();
        std::visit([this](auto& b) noexcept { b.reset(sharedBank_); }, active_);
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

        std::visit(
            [this, &params, pitchEnvStartHz](auto& b) noexcept {
                b.configureForNoteOn(sharedBank_, params, pitchEnvStartHz);
            },
            active_);
    }

    [[nodiscard]] float processSample(float excitation) noexcept
    {
        const float out = std::visit(
            [this, excitation](auto& b) noexcept {
                return b.processSample(sharedBank_, excitation);
            },
            active_);
        lastOutput_ = out;
        return out;
    }

    // Block-level entry point (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    // Single std::visit dispatch per block. Each body variant implements
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
        std::visit(
            [this, out, excitation, numSamples](auto& b) noexcept {
                b.processBlock(sharedBank_, excitation, out, numSamples);
                if (numSamples > 0)
                    lastOutput_ = out[numSamples - 1];
            },
            active_);
    }

    // Single-visit helper: invokes `fn` with a reference to the currently
    // active body alternative and the shared ModalResonatorBank. Callers can
    // then drive the body per-sample using direct typed calls, so the
    // std::variant dispatch happens exactly ONCE per block per voice
    // (research.md §1 / T043 / FR-001, FR-002). Used by DrumVoice::processBlock
    // to run the exciter×body inner sample loop without a per-sample visit.
    template <typename Fn>
    void withActive(Fn&& fn) noexcept
    {
        std::visit(
            [this, &fn](auto& b) noexcept { fn(b, sharedBank_); },
            active_);
    }

    void setLastOutput(float y) noexcept { lastOutput_ = y; }

    [[nodiscard]] float getLastOutput() const noexcept { return lastOutput_; }

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
    using Variant = std::variant<
        MembraneBody,
        PlateBody,
        ShellBody,
        StringBody,
        BellBody,
        NoiseBody>;

    void applyPendingSwap() noexcept
    {
        // Clear shared-bank state before installing the new body so the tail
        // of the previous body doesn't bleed into the new one.
        sharedBank_.reset();

        switch (pendingType_)
        {
        case BodyModelType::Membrane:
            active_.emplace<MembraneBody>();
            break;
        case BodyModelType::Plate:
            active_.emplace<PlateBody>();
            break;
        case BodyModelType::Shell:
            active_.emplace<ShellBody>();
            break;
        case BodyModelType::String:
            active_.emplace<StringBody>();
            break;
        case BodyModelType::Bell:
            active_.emplace<BellBody>();
            break;
        case BodyModelType::NoiseBody:
            active_.emplace<NoiseBody>();
            break;
        case BodyModelType::kCount:
            active_.emplace<MembraneBody>();
            pendingType_ = BodyModelType::Membrane;
            break;
        }
        currentType_ = pendingType_;

        const double sr        = sampleRate_;
        const std::uint32_t vid = voiceId_;
        std::visit([sr, vid](auto& b) noexcept { b.prepare(sr, vid); }, active_);
    }

    Variant                          active_;
    Krate::DSP::ModalResonatorBank   sharedBank_;
    BodyModelType                    currentType_ = BodyModelType::Membrane;
    BodyModelType                    pendingType_ = BodyModelType::Membrane;
    float                            lastOutput_  = 0.0f;
    double                           sampleRate_  = 44100.0;
    std::uint32_t                    voiceId_     = 0;
};

} // namespace Membrum
