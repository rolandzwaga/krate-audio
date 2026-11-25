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
// T2.11 (MB-035, MB-212): noteOn writes its noise burst at the note's
// velocity (setNoteVelocity, handed in by DrumVoice before the body is
// configured), and the burst and the exciter's signal both enter the loop
// scaled by kStringLoopHeadroom, so a full-velocity strike keeps the loop
// junction below the waveguide's in-loop soft clip: the loop stays linear and
// the clip is a safety rail only. DrumVoice measures the strike peak and sets
// outputGain_ so the String body peaks at kBodyHeadroom like every modal body
// (MB-175, MB-221).
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

    /// T2.11 (MB-212): scale of everything fed into the waveguide loop (the
    /// noteOn burst and the external excitation). WaveguideString writes a
    /// burst of RMS velocity x excitationGain_ (about 1.4 at 80 Hz, peaks near
    /// 4) and soft-clips the loop junction at 1.0; at 0.25 that peak sat on
    /// the clip. The loop is linear, so the scale is exact and outputGain_
    /// (the strike normalisation) restores the level. Chosen so a
    /// velocity-1 Impulse strike at Size 1 (80 Hz, the hottest loop) stays
    /// linear within -40 dB ("String body: velocity scales the string and the
    /// loop stays linear").
    static constexpr float kStringLoopHeadroom = 0.25f;

    /// Velocity of the next noteOn's burst (DrumVoice sets it, clamped to
    /// [0, 1], before configureForNoteOn).
    float noteVelocity_ = 1.0f;

    /// Strike-normalisation gain on the body output (DrumVoice: kBodyHeadroom
    /// over the measured strike peak).
    float outputGain_ = 1.0f;

    void setNoteVelocity(float velocity) noexcept
    {
        noteVelocity_ = std::clamp(velocity, 0.0f, 1.0f);
    }

    void setOutputGain(float gain) noexcept { outputGain_ = gain; }

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

        // Initialize delay lines and loop state at the target frequency, with
        // the noise burst at the note's velocity inside the loop headroom;
        // the exciter then drives the waveguide through processSample.
        string_.noteOn(r.frequencyHz, noteVelocity_ * kStringLoopHeadroom);
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
        const float y = string_.process(excitation * kStringLoopHeadroom);
        quietRun_ = (string_.getControlEnergy() < kSilentEnergy)
                        ? std::min(quietRun_ + 1, silentRunSamples_)
                        : 0;
        return y * outputGain_;
    }
};

} // namespace Membrum
