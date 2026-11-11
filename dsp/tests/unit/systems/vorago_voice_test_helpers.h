// ==============================================================================
// VoragoVoice test helpers shared by two TUs in two executables
// ==============================================================================
// vorago_voice_test.cpp (dsp_systems_tests, fast-math) and
// vorago_voice_spectral_target_neutral_test.cpp (dsp_ieee_tests) both drive a
// VoragoVoice through these. The split exists because
// VoragoVoice_SpectralTargetIsNeutral asserts bit-identity between the test's
// exp2 law and the cloud's own, which only holds under IEEE flags; every other
// case in the voice TU runs under fast-math.
//
// Header-only and `inline`, in a named namespace, so the two TUs that include it
// share one definition (no anonymous-namespace copies, no ODR hazard if both ever
// land in one image).
// ==============================================================================

#pragma once

#include <krate/dsp/systems/vorago_voice.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace VoragoVoiceTestHelpers {

inline constexpr double kHelperSampleRate = 48000.0;

/// @brief Render @p n samples and throw the audio away.
inline void renderAndDiscard(Krate::DSP::VoragoVoice& voice, std::size_t n) {
    std::vector<float> l(n, 0.0f);
    std::vector<float> r(n, 0.0f);
    voice.processStereoBlock(l.data(), r.data(), n);
}

// -----------------------------------------------------------------------------
// The two test-side doors through the voice's const sub-component accessors.
//
// VoragoVoice::bloom() and ::cloud() are const BY DESIGN - the voice owns every
// write path onto its components. Two things these cases need are on the far
// side of that const and are NOT on the voice's own surface at T011:
//
//   (a) BloomEngine::triggerBloom() (bloom_engine.h:663). The voice-level
//       trigger is FR-022's BloomTrigger family, routed by publishIdentity(),
//       which is still T013's empty hook - and the ONLY other source is the
//       internal spawn clock, whose ceiling is kMaxSpawnRateHz = 0.05 Hz
//       (bloom_engine.h:280), i.e. one expected event per 20 s of RENDERED
//       audio. SC-018a's two edges have to be forced, not waited for.
//   (b) HarmonicCloud::setSpectralGravity() / setSpectralTarget(). Gravity is
//       deliberately NOT on the voice's macro surface (Q4, vorago_voice.h:466)
//       and setSpectralTarget is the voice's own private business.
//
// The referenced components are non-const members of a non-const heap-allocated
// VoragoVoice, so removing const is well defined - the same idiom, and the same
// justification, as seraphis_macro_test.cpp:420-435.
// -----------------------------------------------------------------------------

inline Krate::DSP::BloomEngine& mutableBloom(Krate::DSP::VoragoVoice& v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<Krate::DSP::BloomEngine&>(v.bloom());
}

inline Krate::DSP::HarmonicCloud& mutableCloud(Krate::DSP::VoragoVoice& v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<Krate::DSP::HarmonicCloud&>(v.cloud());
}

/// @brief A prepared voice whose pre-sustain envelope walk is 1 ms per stage.
///
/// FR-014's SHIPPED walk is 20 s + 30 s + 45 s + 60 s (vorago_voice.h:266-268),
/// which leaves any render short enough to run 256 times essentially at zero -
/// and a bit-identity assertion over two silent buffers passes for the wrong
/// reason. Compressing the walk changes nothing these cases measure: the
/// envelope multiplies the excitation bus by ONE scalar per sample (AR-5), the
/// same scalar in every arm.
[[nodiscard]] inline std::unique_ptr<Krate::DSP::VoragoVoice> makeFastAttackVoice(
    std::uint32_t seed) {
    auto v = std::make_unique<Krate::DSP::VoragoVoice>();
    v->setSeed(seed);
    v->prepare(kHelperSampleRate, Krate::DSP::VoragoVoiceConfig{});
    for (int st = 0; st < Krate::DSP::VoragoVoice::kEnvelopeSustainPoint; ++st) {
        v->setEnvelopeStageTimeMs(st, 1.0f);
    }
    return v;
}

/// @brief Put the bloom under the test's control: no internal spawn clock, one
///        child per event, no hold jitter, the shortest legal child life.
inline void makeBloomDeterministic(Krate::DSP::BloomEngine& b) {
    using Krate::DSP::BloomEngine;
    b.setSpawnRateHz(0.0f);  // bloom_engine.h:540-548 - triggerBloom() is then the ONLY source
    b.setChildrenPerEvent(1);
    b.setHoldJitterFraction(0.0f);  // :593-600 - every latched hold is exactly getHoldSeconds()
    b.setFadeInSeconds(BloomEngine::kMinFadeInSeconds);    // 1 s
    b.setHoldSeconds(BloomEngine::kMinHoldSeconds);        // 0 s
    b.setFadeOutSeconds(BloomEngine::kMinFadeOutSeconds);  // 1 s
}

/// @brief The cloud's OWN untargeted amplitude law, written exactly as
///        harmonic_cloud.h:1467-1468 and :1492-1494 write it.
///
/// `std::exp2(-p * kHarmonicCloudLog2N[i])`, never `std::pow(n, -p)` (B-2). The
/// neutrality case (vorago_voice_spectral_target_neutral_test.cpp) supplies THIS
/// as a spectral target and asserts the render is bit-identical to the
/// untargeted one; the pow form is asserted to be a DIFFERENT float, which is
/// what makes that bit-identity a discriminating test rather than a tautology.
[[nodiscard]] inline float parentAmplitudeExp2(std::size_t i, float p) {
    return std::exp2(-p * Krate::DSP::detail::kHarmonicCloudLog2N[i]);
}

/// @brief FR-041(b)'s p(r), evaluated exactly as harmonic_cloud.h:1467-1468 does.
[[nodiscard]] inline float richnessExponent(float r) {
    using Krate::DSP::HarmonicCloud;
    return HarmonicCloud::kRichnessMinExponent
           + (HarmonicCloud::kRichnessMaxExponent - HarmonicCloud::kRichnessMinExponent) * r;
}

}  // namespace VoragoVoiceTestHelpers
