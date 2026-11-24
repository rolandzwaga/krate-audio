#pragma once

// ==============================================================================
// StringBody -- Phase 2 (data-model.md §3.6)
// ==============================================================================
// Owns a Krate::DSP::WaveguideString. Ignores the shared ModalResonatorBank
// entirely (FR-023, first body model to break out of the shared bank).
//
// The waveguide lifecycle used here is:
//   configureForNoteOn()
//     -> setFrequency / setDecay / setBrightness / setPickPosition
//     -> noteOn(f0, velocity)  (initializes delay-line lengths + loop state)
// Phase 2 uses a fixed velocity=1.0 inside noteOn; the audible output is
// driven by the exciter through processSample(excitation) which calls
// waveguide_.process(excitation). The exciter already embeds user velocity.
// ==============================================================================

#include "../voice_common_params.h"
#include "string_mapper.h"

#include <krate/dsp/processors/modal_resonator_bank.h>
#include <krate/dsp/processors/waveguide_string.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Membrum {

struct StringBody
{
    Krate::DSP::WaveguideString string_;

    /// WaveguideString::getControlEnergy() is a one-pole (5 ms) average of the
    /// squared output, i.e. a mean square. -100 dBFS RMS is an amplitude of
    /// 10^(-100/20) = 1e-5, whose square is 1e-10.
    static constexpr float kSilentEnergy = 1.0e-10f;

    /// How long the control energy must stay below kSilentEnergy before the
    /// body counts as silent: longer than the follower's ripple (half a period
    /// of the string, 25 ms at the 20 Hz floor of StringMapper, 6.25 ms at the
    /// 80 Hz a Size of 1 maps to), so a ripple trough never reads as silence.
    static constexpr float kSilentRunMs = 20.0f;

    /// Consecutive samples whose control energy stayed below kSilentEnergy,
    /// capped at silentRunSamples_. Voice retirement reads it once per host
    /// block (MB-106, MB-160, T1.13 / MB-052): counted per sample, the run
    /// starts at the same sample whatever the block size, so the silence
    /// decision is reached at the first block end past one fixed voice time.
    /// (A per-block maximum needed a whole silent block, which put block-64
    /// and block-1024 retirement up to two 1024-sample blocks apart.)
    int quietRun_         = 0;
    int silentRunSamples_ = 960;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        string_.prepare(sampleRate);
        string_.prepareVoice(voiceId);
        silentRunSamples_ = static_cast<int>(
            std::lround(static_cast<double>(kSilentRunMs) * 0.001 * sampleRate));
        quietRun_ = 0;
    }

    void reset(Krate::DSP::ModalResonatorBank& /*sharedBank*/) noexcept
    {
        string_.silence();
        quietRun_ = 0;
    }

    /// True once the control energy has stayed below kSilentEnergy for
    /// kSilentRunMs.
    [[nodiscard]] bool isSilent() const noexcept
    {
        return quietRun_ >= silentRunSamples_;
    }

    /// Re-arm to the post-prepare state when BodyBank selects this body again
    /// (audio thread, no allocation): silence the loop and re-seed the
    /// waveguide RNG as prepareVoice() did. The smoother configuration and
    /// energy-follower coefficients prepare() set are never changed, and
    /// noteOn() snaps the smoothers.
    void restart(std::uint32_t voiceId) noexcept
    {
        string_.silence();
        string_.prepareVoice(voiceId);
        quietRun_ = 0;
    }

    void configureForNoteOn(Krate::DSP::ModalResonatorBank& /*sharedBank*/,
                            const VoiceCommonParams& params,
                            float pitchHz) noexcept
    {
        const auto r = Bodies::StringMapper::map(params, pitchHz);

        string_.setFrequency(r.frequencyHz);
        string_.setDecay(r.decayTime);
        string_.setBrightness(r.brightness);
        string_.setPickPosition(r.pickPosition);

        // Initialize delay lines and loop state at the target frequency.
        // The excitation noise burst it writes is fine; once the exciter
        // starts feeding samples through processSample, that transient
        // plus the external excitation drives the waveguide.
        string_.noteOn(r.frequencyHz, 1.0f);
        quietRun_ = 0;   // a new note is not silent until its own run is
    }

    // IMPORTANT: IGNORES sharedBank (FR-023, shared-bank isolation contract).
    [[nodiscard]] float processSample(
        Krate::DSP::ModalResonatorBank& /*sharedBank*/,
        float excitation) noexcept
    {
        return processTracked(excitation);
    }

    /// String body doesn't use the modal bank, so the no-smooth variant is
    /// just the same per-sample call. Symmetric API for the slow-path caller.
    [[nodiscard]] float processSampleNoSmooth(
        Krate::DSP::ModalResonatorBank& /*sharedBank*/,
        float excitation) noexcept
    {
        return processTracked(excitation);
    }

    // Block-rate entry point (Phase 9 SIMD emergency fallback / plan.md §SIMD).
    // WaveguideString has no SIMD block entry, so we loop per-sample. The
    // real win here is that the outer per-sample chain (unnatural + tone
    // shaper + env) can still be block-free of per-sample type dispatch.
    void processBlock(Krate::DSP::ModalResonatorBank& /*sharedBank*/,
                      const float* excitation,
                      float* out,
                      int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processTracked(excitation[i]);
    }

private:
    [[nodiscard]] float processTracked(float excitation) noexcept
    {
        const float y = string_.process(excitation);
        quietRun_ = (string_.getControlEnergy() < kSilentEnergy)
                        ? std::min(quietRun_ + 1, silentRunSamples_)
                        : 0;
        return y;
    }
};

} // namespace Membrum
