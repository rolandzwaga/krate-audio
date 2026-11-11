// ==============================================================================
// Layer 3: System Tests - VoragoVoice spectral-target neutrality (FR-011 / B-2)
// ==============================================================================
// Split out of vorago_voice_test.cpp so that TU runs under fast-math in
// dsp_systems_tests while this one runs in dsp_ieee_tests.
//
// WHY IEEE: the case supplies the test's exp2 law as a spectral target and
// asserts the render is bit-identical to the cloud's untargeted path - two
// separately compiled computations compared bit for bit (CLAUDE.md, fast-math
// rules). Under the macOS repro's -ffast-math the two are reassociated apart
// (red at richness 0.6346, tilt -12 dB/oct; green with -fno-associative-math).
//
// Reference: specs/vorago-phase10-voice-engine/spec.md (FR-011, B-2)
//
// Helpers: vorago_voice_test_helpers.h, shared with vorago_voice_test.cpp.
// Allocation overrides: dsp_ieee_tests' owner is dsp_ieee_alloc_overrides.cpp;
// this TU includes neither allocation header.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/systems/vorago_voice.h>

#include <vorago_fixtures.h>

#include "vorago_voice_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace VF = Krate::DSP::TestUtils::Vorago;

namespace {

using Krate::DSP::BloomEngine;
using Krate::DSP::HarmonicCloud;
using Krate::DSP::VoragoVoice;
using VoragoVoiceTestHelpers::makeBloomDeterministic;
using VoragoVoiceTestHelpers::makeFastAttackVoice;
using VoragoVoiceTestHelpers::mutableBloom;
using VoragoVoiceTestHelpers::mutableCloud;
using VoragoVoiceTestHelpers::parentAmplitudeExp2;
using VoragoVoiceTestHelpers::renderAndDiscard;
using VoragoVoiceTestHelpers::richnessExponent;

/// FOUR RICHNESS VALUES CHOSEN SO p(r) IS EXACT IN BINARY FLOATING POINT.
/// p(r) = 3.0 + (-2.5) * r. At r in {0, 1/4, 1/2, 1} both the product and the
/// sum are exactly representable, so the value the test computes and the value
/// the cloud computes cannot diverge through FMA contraction - which they could
/// at, say, r = 0.7, where the two TUs may or may not fuse the multiply-add and
/// the bit-identity claim would become a toolchain lottery.
/// FR-034 (Phase 13c, T024): narrowed to points AT OR ABOVE
/// VoragoVoice::kCloudRichnessFloor (0.6346), where cloud richness == user
/// richness still holds and plain-vs-forced bit identity is the right claim.
/// The former below-floor points {0, 0.25, 0.5} moved to
/// VoragoVoice_CloudRichnessFloorNeutral; see
/// specs/vorago-phase13c-capability-audibility/artifacts/fr034_surfaced.md
/// entry 1. Note 0.6346f is NOT exact in binary (0.75f and 1.0f are), the
/// FMA risk recorded in that entry.
constexpr std::array<float, 3> kNeutralityRichness{0.6346f, 0.75f, 1.0f};
/// Spans the component's whole tilt domain (harmonic_cloud.h:194-195).
constexpr std::array<float, 4> kNeutralityTiltDb{-12.0f, -4.0f, 0.0f, 12.0f};
/// Includes BOTH the g == 0 identity branch and both endpoints (:478-488).
constexpr std::array<float, 4> kNeutralityGravity{-1.0f, 0.0f, 0.35f, 1.0f};
/// Includes the B == 0 identity and the component ceiling (:191).
constexpr std::array<float, 4> kNeutralityInharmonicity{0.0f, 0.001f, 0.015f, 0.1f};

}  // namespace

// -----------------------------------------------------------------------------
// FR-011 / B-2: the supplied parent law is bit-identical to the untargeted cloud
// -----------------------------------------------------------------------------

TEST_CASE("VoragoVoice_SpectralTargetIsNeutral", "[systems][vorago]") {
    // 8 chunks of warm-up (the 4 ms envelope walk plus the cloud's 50 ms attack
    // getting under way), then 8 chunks compared byte for byte.
    constexpr std::size_t kWarmUpChunks = 8;
    constexpr std::size_t kMeasuredSamples = 8u * VoragoVoice::kControlChunkSamples;

    auto plain = makeFastAttackVoice(0xB100Du);
    auto forced = makeFastAttackVoice(0xB100Du);

    double worstPeak = 0.0;
    int powDifferences = 0;

    for (const float richness : kNeutralityRichness) {
        const float p = richnessExponent(richness);
        // The discriminating half of B-2, and it is pure arithmetic: if
        // updateSpectrumTarget were written with std::pow the supplied parent
        // region would be a DIFFERENT float array from the cloud's own, and the
        // bit-identity asserted below could not hold.
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            const float viaPow = std::pow(static_cast<float>(i + 1), -p);
            if (viaPow != parentAmplitudeExp2(i, p)) {
                ++powDifferences;
            }
        }

        for (const float tiltDb : kNeutralityTiltDb) {
            for (const float gravity : kNeutralityGravity) {
                for (const float inharmonicity : kNeutralityInharmonicity) {
                    INFO("richness " << richness << ", tilt " << tiltDb << " dB/oct, gravity "
                                     << gravity << ", B " << inharmonicity);

                    for (VoragoVoice* v : {plain.get(), forced.get()}) {
                        v->reset();
                        v->setRichness(richness);
                        v->setSpectralTiltDb(tiltDb);
                        v->setInharmonicity(inharmonicity);
                        mutableCloud(*v).setSpectralGravity(gravity);
                        // The bloom held INERT, both arms: no depth, no wake, no
                        // clock. getLiveChildCount() therefore stays 0 and the
                        // voice's own handoff never supplies a target - which is
                        // what leaves the forced arm's target standing.
                        v->setBloomDepth(0.0f);
                        BloomEngine& b = mutableBloom(*v);
                        makeBloomDeterministic(b);
                        b.setWake(0.0f);
                        v->noteOn(55.0f, 1.0f);
                        renderAndDiscard(*v,
                                         kWarmUpChunks * VoragoVoice::kControlChunkSamples);
                    }

                    // Read the active count AFTER the warm-up: setRichness defers
                    // it to the cloud's next control update (:412-423).
                    const std::size_t active = plain->cloud().getActivePartialCount();
                    REQUIRE(active == forced->cloud().getActivePartialCount());
                    REQUIRE(active >= 1u);

                    std::array<float, HarmonicCloud::kMaxPartials> ratios{};
                    std::array<float, HarmonicCloud::kMaxPartials> amplitudes{};
                    for (std::size_t i = 0; i < active; ++i) {
                        ratios[i] = static_cast<float>(i + 1);
                        amplitudes[i] = parentAmplitudeExp2(i, p);
                    }

                    std::vector<float> plainOutL(kMeasuredSamples, 0.0f);
                    std::vector<float> plainOutR(kMeasuredSamples, 0.0f);
                    std::vector<float> forcedOutL(kMeasuredSamples, 0.0f);
                    std::vector<float> forcedOutR(kMeasuredSamples, 0.0f);

                    // BOTH arms are driven one 64-sample control chunk at a time,
                    // and both are marked fully dirty on every one of them. That
                    // is not decoration, it is what makes the comparison a
                    // comparison of VALUES rather than of RECOMPUTE SCHEDULES:
                    //
                    //  * FR-085 lever 1 (harmonic_cloud.h:776-812) makes a
                    //    REPEATED bit-identical setSpectralTarget a no-op, so
                    //    without the clear the forced arm would recompute on its
                    //    first chunk only;
                    //  * recalculateAmplitudes ends in FR-017's
                    //    normGain_.setTarget (:1500+), and a LinearRamp retarget
                    //    mid-ramp is not necessarily a no-op even at an unchanged
                    //    target. One arm recomputing on a chunk the other skips
                    //    could therefore move the normalizer's trajectory by
                    //    itself - a difference with nothing to do with B-2.
                    //
                    // clearSpectralTarget() marks every slot dirty on both halves
                    // (:862-866 -> :1252-1261), so the two arms run the SAME number
                    // of recomputes over the SAME slot set, and every remaining
                    // difference is the supplied law against the parametric one.
                    for (std::size_t off = 0; off < kMeasuredSamples;
                         off += VoragoVoice::kControlChunkSamples) {
                        mutableCloud(*plain).clearSpectralTarget();
                        plain->processStereoBlock(plainOutL.data() + off,
                                                  plainOutR.data() + off,
                                                  VoragoVoice::kControlChunkSamples);

                        mutableCloud(*forced).clearSpectralTarget();
                        // Installed BEFORE the chunk, so updateSpectrumTarget runs
                        // against it. It survives that call precisely because the
                        // clear is latched on targetActive_: the voice clears only
                        // a target IT set (vorago_voice.h, the `else if` arm).
                        mutableCloud(*forced).setSpectralTarget(ratios.data(),
                                                                amplitudes.data(), active);
                        forced->processStereoBlock(forcedOutL.data() + off,
                                                   forcedOutR.data() + off,
                                                   VoragoVoice::kControlChunkSamples);
                    }

                    // The FR-011 latch, both arms. The voice supplies nothing
                    // while the bloom is inert (so the plain arm's clear stands),
                    // and it does NOT clear a target it did not set (so the forced
                    // arm's target stands) - that `else if (targetActive_)` guard
                    // is what this pair of assertions pins.
                    REQUIRE_FALSE(plain->cloud().hasSpectralTarget());
                    REQUIRE(forced->cloud().hasSpectralTarget());

                    for (std::size_t i = 0; i < kMeasuredSamples; ++i) {
                        worstPeak =
                            std::max(worstPeak, static_cast<double>(std::abs(plainOutL[i])));
                    }

                    REQUIRE(VF::bitIdentical(plainOutL, forcedOutL));
                    REQUIRE(VF::bitIdentical(plainOutR, forcedOutR));
                }
            }
        }
    }

    // Non-vacuity: two silent buffers are bit-identical for the wrong reason.
    INFO("worst |left| over the sweep = " << worstPeak);
    REQUIRE(worstPeak > 1.0e-6);
    // Non-vacuity of the B-2 claim: the pow form really is a different float.
    INFO("slots where pow(n,-p) != exp2(-p*log2(n)): " << powDifferences);
    REQUIRE(powDifferences > 0);
}
