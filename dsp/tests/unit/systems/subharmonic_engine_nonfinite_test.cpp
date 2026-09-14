// ==============================================================================
// Layer 3: System Tests - SubharmonicEngine non-finite hygiene (SC-009)
//                              (specs/vorago-phase6-subharmonic)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase6-subharmonic/spec.md   (SC-009)
//            specs/vorago-phase6-subharmonic/plan.md   (S7.5, S7.6, S10.2)
//            specs/vorago-phase6-subharmonic/tasks.md  (T001 creates this TU,
//                                                       T019 lands the case and
//                                                       the probe definition)
//
// SCOPE OF THIS TU (plan S10.2): SC-009 ONLY. It is also the only definition of
//   Krate::DSP::detail::SubharmonicEngineNonFiniteProbe - the library declares
//   that struct (subharmonic_engine.h:132) and befriends it (:869), but never
//   defines it.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 6 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt. Non-finite values may be NAMED only here, and only
//   built from bit patterns through a volatile sink (makeNonFinite(bits);
//   patterns 0x7FC00000, 0x7F800000, 0xFF800000) - never
//   std::numeric_limits<float>::quiet_NaN()/infinity(), which fold to finite
//   garbage on the macOS/Linux -ffast-math legs. Finiteness is tested with
//   Krate::DSP::detail::isFinite (core/db_utils.h:118), never std::isnan /
//   std::isinf / std::isfinite (tools/lint-nonfinite-symbols.js gates this).
//
// NO BIT-EXACT FLOAT GOLDENS. Every `==` below is "this stored value was NOT
//   written" (arm (a)): the setter under test either stored its argument or
//   returned without touching the member, so exact equality IS the assertion.
//   Every aggregate comparison goes through render_fingerprint.h's measured
//   tolerances against a render of THIS build - never a number pinned in the
//   source (tools/lint-float-bit-goldens.js gates the real thing).
//
// ------------------------------------------------------------------------------
// THE THREE ARMS, AND WHAT EACH ONE WOULD CATCH
//
// (a) PER-SETTER REJECTION. std::clamp does NOT reject NaN: with v = NaN both
//     `v < lo` and `hi < v` are false and v is returned unchanged. FR-009's
//     remedy is REJECTION - `if (!detail::isFinite(v)) return;` as the first
//     statement - so the PREVIOUS value stands. The trace a clamp-only setter
//     would open is fatal and is quoted in the header: a NaN reaching
//     masterUnison_.increment (setFundamentalHz) poisons masterPhaseEstimate_,
//     which SubOscillator never guards.
//
// (b) THE S7.5 PROBE. FR-055's per-sample trap in renderChunk() step (6) is
//     UNREACHABLE through the public API, so the two stages that cannot
//     self-heal - DCBlocker2::process, which has no finiteness branch at all
//     (dc_blocker.h:328-341), and EnvelopeFollower::processSample, which
//     documents "Does NOT validate input" (envelope_follower.h:162) - are only
//     reachable through the declared friend.
//
// (c) THE 30 s ADVERSARIAL SWEEP with a non-finite INPUT buffer for the middle
//     10 s. This is the arm S7.6's follower guard exists to make reachable.
//     See the block above renderSweep() for the fixture reasoning and for the
//     falsification arithmetic.
// ==============================================================================

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "render_fingerprint.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/systems/subharmonic_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// ==============================================================================
// THE PLAN S7.5 FAULT-INJECTION PROBE - THE SOLE DEFINITION IN THE REPOSITORY
// ==============================================================================
// The library DECLARES this struct and befriends it; it never defines it, so a
// shipping build has no way to call it and it adds no public surface. Pattern:
// systems/feedback_ecology.h:172 (declaration) / :1525 (friend), defined once in
// dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp:131-150.
//
// ODR: swept this session -
//   grep -rn "SubharmonicEngineNonFiniteProbe" dsp/ plugins/ tools/
// returns the header's declaration + friend line and this TU only.
//
// EXACTLY TWO OPERATIONS (plan S7.5). A probe that can write any member is a
// second, untested API, so there is no generic setter here and no reset.
// ==============================================================================
namespace Krate::DSP::detail {

struct SubharmonicEngineNonFiniteProbe {
    /// What readChainHealth() reports: the two stages of the sub chain that
    /// cannot recover on their own.
    struct ChainHealth {
        float followerValue = 0.0f;
        float blockerY1     = 0.0f;
    };

    /// @brief Poison the FR-042 DC blocker's recursive state.
    ///
    /// The poison is delivered by PROCESSING one non-finite sample through the
    /// blocker, not by writing a member: DCBlocker2 stores
    /// `y1_ = detail::flushDenormal(y)` (dc_blocker.h:340) and flushDenormal
    /// returns a NaN unchanged because both of its ordered comparisons are false
    /// (db_utils.h:245-247). That is the documented propagation the FR-055 trap
    /// exists to catch, so injecting through it is the real fault and not a
    /// synthesised one.
    ///
    /// The engine must be prepared: DCBlocker2::process returns its input
    /// unchanged and stores NOTHING while `prepared_` is false
    /// (dc_blocker.h:329-331), which would make the injection a silent no-op.
    /// The caller asserts on readChainHealth() immediately afterwards, so that
    /// precondition cannot be violated undetected.
    static void injectChainNonFinite(SubharmonicEngine& engine, float nonFinite) noexcept {
        static_cast<void>(engine.blocker_.process(nonFinite));
    }

    /// @brief Read the follower's current value and the blocker's y1_.
    ///
    /// DELIBERATE DEVIATION, stated rather than hidden: `DCBlocker2::y1_` is
    /// PRIVATE (dc_blocker.h:388) and has no accessor, and friendship with
    /// SubharmonicEngine reaches `blocker_` the member, not the member's own
    /// privates. So y1_ is observed through a COPY of the blocker fed one zero
    /// sample: DCBlocker2 is copyable (:285-288) and the copy's
    /// `process(0.0f)` evaluates `-a1_*y1_ - a2_*y2_` with finite, non-zero
    /// coefficients (a1_ = -2cos(w0)/a0 ~= -2 at an 18 Hz corner), which is
    /// non-finite if and only if the blocker's output history is. The ENGINE'S
    /// blocker is not advanced by this read - the copy absorbs the state
    /// change - so readChainHealth() is observation-only, as its name promises.
    [[nodiscard]] static ChainHealth readChainHealth(const SubharmonicEngine& engine) noexcept {
        DCBlocker2 mirror = engine.blocker_;
        return ChainHealth{.followerValue = engine.follower_.getCurrentValue(),
                           .blockerY1     = mirror.process(0.0f)};
    }
};

}  // namespace Krate::DSP::detail

namespace {

using Probe = Krate::DSP::detail::SubharmonicEngineNonFiniteProbe;
using Krate::DSP::SubharmonicEngine;
using Krate::DSP::SubWaveform;
using Krate::DSP::detail::isFinite;

namespace fp = Krate::DSP::TestUtils;

// =============================================================================
// Non-finite construction (never std::numeric_limits)
// =============================================================================

struct NonFinitePattern {
    const char*   name;
    std::uint32_t bits;
};

constexpr std::array<NonFinitePattern, 3> kPatterns{{
    {"quiet NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time, which is how a -ffast-math build
/// turns an "infinity" literal into a finite number. Idiom transcribed from
/// dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink         = bits;
    const std::uint32_t    materialized = sink;
    float                  out          = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

// =============================================================================
// Shared fixture constants
// =============================================================================

constexpr double      kFs    = 48000.0;
constexpr std::size_t kBlock = 512;

constexpr double kTwoPi = 6.283185307179586;

/// A steady body, deterministic from the ABSOLUTE sample index so a render
/// split into 1 + 511 samples carries the same signal as an unsplit 512.
constexpr double kBodyHz        = 55.0;
constexpr double kBodyAmplitude = 0.25;

[[nodiscard]] float bodySample(std::size_t index) {
    const double omega = kTwoPi * kBodyHz / kFs;
    return static_cast<float>(kBodyAmplitude * std::sin(omega * static_cast<double>(index)));
}

/// FR-032's reference level is pinned at the TOP of its range in both fixtures
/// below, and that is load-bearing rather than incidental. At the default
/// -18 dB reference, `envNorm = clamp(env / 0.1259, 0, 1)` saturates at 1.0 for
/// a 0.25-amplitude body (RMS 0.1768) and the tracking law goes FLAT - a
/// poisoned or reset follower would then be invisible in the audio and both
/// arm (b) and arm (c2) would pass on a broken build. At 0 dB the reference RMS
/// is 1.0, envNorm sits at ~0.177, and every movement of the sensor reaches the
/// output.
constexpr float kUnclampedTrackReferenceDb = SubharmonicEngine::kMaxTrackReferenceDb;

static_assert(kUnclampedTrackReferenceDb == 0.0f,
              "the fixture reasoning above assumes a reference RMS of 1.0");

// =============================================================================
// Arm (b): the S7.5 probe and one-sample recovery
// =============================================================================
// 15.0 s at 48 kHz in 512-sample blocks. The injection lands at block 188
// (t = 2.005 s), and the tail measured against the un-injected reference starts
// at block 1126 (t = 12.01 s) - TEN SECONDS of settle after the fault.
//
// Ten seconds is not padding, it is arithmetic. recoverNonFinite() resets the
// follower (subharmonic_engine.h:1314-1319), so the injected instance re-attacks
// its envelope from zero while the reference sits at its steady state; the
// residual difference decays with the SLOWER of the two follower coefficients,
// the 800 ms release. Ten seconds is 12.5 time constants, e^-12.5 ~= 3.7e-6 of
// the initial difference - three orders below render_fingerprint.h's 2.5e-4
// metric tolerance. The other three stages recover far faster: lowpass_ and
// blocker_ are LTI with an identical input from that sample on (the 18 Hz
// corner's ~9 ms memory), and saturator_.reset() snaps its smoothers TO THEIR
// TARGETS (saturation_processor.h:150-153), so it contributes no transient at
// all.
// =============================================================================

constexpr std::size_t kRecoveryBlocks      = 1408;  // 720 896 samples, 15.02 s
constexpr std::size_t kInjectBlock         = 188;   //  96 256 samples,  2.01 s
constexpr std::size_t kRecoveryTailBlock   = 1126;  // 576 512 samples, 12.01 s
constexpr std::size_t kRecoveryTailSamples = (kRecoveryBlocks - kRecoveryTailBlock) * kBlock;

static_assert(kInjectBlock < kRecoveryTailBlock && kRecoveryTailBlock < kRecoveryBlocks,
              "the fault must land before the settle, and the settle before the tail");

constexpr std::size_t kNoInjection = static_cast<std::size_t>(-1);

struct RecoveryRender {
    std::vector<float> tailTap;
    bool               allFinite          = true;
    bool               healthBeforeFinite = false;
    bool               healthPoisoned     = false;
    bool               oneSampleFinite    = false;
    bool               healthHealed       = false;
    std::uint32_t      clampCount         = 0;
};

void configureSteadyFixture(SubharmonicEngine& engine) {
    // Everything else stays on the FR-003 defaults: Sine tones at -18/-24/-30 dB,
    // tracking amount 1.0, 120/800 ms follower, 120 Hz low-pass, +3 dB drive,
    // 0 dB wet.
    engine.setSeed(0x5EED0019u);
    engine.setTrackReferenceDb(kUnclampedTrackReferenceDb);
    engine.setSubToMainEnabled(true);
}

/// @brief Render kRecoveryBlocks blocks of the steady body, optionally injecting
///        the S7.5 fault at the start of block `injectAtBlock`.
[[nodiscard]] RecoveryRender renderRecovery(std::size_t injectAtBlock) {
    SubharmonicEngine engine;
    engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = kBlock});
    configureSteadyFixture(engine);

    RecoveryRender result;
    result.tailTap.reserve(kRecoveryTailSamples);

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    std::vector<float> tap(kBlock, 0.0f);

    std::size_t index = 0;

    // Renders `count` samples starting at the running absolute index and returns
    // whether every rendered sample of every output was finite.
    const auto renderRun = [&](std::size_t count) -> bool {
        for (std::size_t i = 0; i < count; ++i) {
            inL[i] = bodySample(index + i);
            inR[i] = inL[i];
        }
        engine.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(), tap.data(),
                                  count);
        bool runFinite = true;
        for (std::size_t i = 0; i < count; ++i) {
            if (!isFinite(outL[i]) || !isFinite(outR[i]) || !isFinite(tap[i])) {
                runFinite = false;
            }
            if (index + i >= kRecoveryTailBlock * kBlock) {
                result.tailTap.push_back(tap[i]);
            }
        }
        index += count;
        return runFinite;
    };

    for (std::size_t block = 0; block < kRecoveryBlocks; ++block) {
        if (block == injectAtBlock) {
            result.healthBeforeFinite = isFinite(Probe::readChainHealth(engine).blockerY1);
            Probe::injectChainNonFinite(engine, makeNonFinite(kPatterns[0].bits));
            result.healthPoisoned = !isFinite(Probe::readChainHealth(engine).blockerY1);

            // ONE sample. FR-055's trap in renderChunk() step (6) fires on this
            // very sample: the blocker's poisoned y1_ makes `y` non-finite, the
            // trap substitutes 0.0f and calls recoverNonFinite().
            result.oneSampleFinite = renderRun(1);

            const auto healed   = Probe::readChainHealth(engine);
            result.healthHealed = isFinite(healed.blockerY1) && isFinite(healed.followerValue);
            result.allFinite    = result.allFinite && result.oneSampleFinite;

            const bool rest  = renderRun(kBlock - 1);
            result.allFinite = result.allFinite && rest;
        } else {
            const bool ok    = renderRun(kBlock);
            result.allFinite = result.allFinite && ok;
        }
    }

    result.clampCount = engine.getClampEngagementCount();
    return result;
}

// =============================================================================
// Arm (c): the 30 s adversarial sweep with a non-finite input window
// =============================================================================
// THE REFERENCE RENDER IS THE SAME 30 s WITH THE WINDOW REPLACED BY DIGITAL
// SILENCE, and that choice is the whole design of this arm. Reasoning, from the
// shipped code:
//
//  * The sub chain is GENERATOR-DRIVEN. renderChunk() reads the input for
//    exactly two purposes (subharmonic_engine.h:1186-1223): the dry samples it
//    adds to, and `mono`, which feeds the follower. Nothing else in the chain
//    can see the host's audio.
//  * S7.6's guard feeds the follower `isFinite(mono) ? mono : 0.0f`
//    (:1222-1223). During the poisoned window the sensor therefore receives
//    EXACTLY the sequence a silent input would produce - so a correct
//    implementation makes the two renders agree sample for sample, and the
//    comparison is sharp at render_fingerprint.h's default tolerances rather
//    than at a hand-loosened bound.
//  * A reference rendered with the BODY running through the window would be the
//    wrong control: a correct implementation's follower decays during the window
//    while such a reference's tracks the body, so the two would diverge at
//    t = 20 s for a reason that has nothing to do with the guard.
//
// TWO FIXTURE VALUES ARE HELD FIXED SO THE FALSIFICATION HAS TEETH. The stated
// falsification is to comment out the per-sample isFinite(mono) guard in
// renderChunk step (3). The SECOND guard (updateControl step (4),
// subharmonic_engine.h:1104-1108) then still catches the poisoned envelope and
// resets the follower once per control chunk, so the mutant does not mute - it
// SNAPS the envelope to zero at the first control step of the window, where a
// correct build's decays smoothly. For that difference to survive to the
// measured tail:
//
//   * the follower RELEASE is pinned at kMaxFollowerReleaseMs (5000 ms), so
//     across the 10 s window a correct build's envelope decays only to
//     e^-2 = 13.5 % rather than to nothing. At t = 20 s the mutant's envNorm is
//     0 and the correct build's is ~0.135 x 0.177;
//   * the follower ATTACK stays at the 120 ms default, so both re-converge in
//     under a second - the divergence is confined to the first ~0.9 s of the
//     10 s tail, which the aggregate metrics carry (a ~0.6 % rms shift against a
//     2.5e-4 tolerance) even when checkpoint[0] happens to land near a zero
//     crossing of the sub.
//
// Everything else IS swept, every 512 samples (10.7 ms), across its full FR
// range - with three deliberate floors, each of which would otherwise make the
// criterion vacuous rather than adversarial:
//   * tone 0's level never reaches the fader bottom, so allTonesDormant() stays
//     false and the chain the arm measures is actually running;
//   * the wet trim never goes below -12 dB, so the sub reaches the main output;
//   * subToMainEnabled stays true, for the same reason.
// =============================================================================

constexpr std::size_t kSweepSamples = 30 * 48000;  // 1 440 000
constexpr std::size_t kWindowStart  = 10 * 48000;  //   480 000
constexpr std::size_t kWindowEnd    = 20 * 48000;  //   960 000
constexpr std::size_t kTailSamples  = kSweepSamples - kWindowEnd;

static_assert(kWindowStart < kWindowEnd && kWindowEnd < kSweepSamples,
              "the poisoned window must be strictly inside the render");

/// Triangle walk on [0, 1], a pure function of the block index.
[[nodiscard]] float ramp01(std::size_t block, std::size_t period) {
    const std::size_t p  = block % (2 * period);
    const std::size_t up = (p < period) ? p : (2 * period - p);
    return static_cast<float>(up) / static_cast<float>(period);
}

[[nodiscard]] float lerpf(float lo, float hi, float u) { return lo + (hi - lo) * u; }

void applySweep(SubharmonicEngine& engine, std::size_t block) {
    engine.setFundamentalHz(lerpf(30.0f, 90.0f, ramp01(block, 37)));

    // Tone 0 never reaches the fader bottom (see the block comment above);
    // tones 1 and 2 are free to go dormant individually.
    engine.setToneLevelDb(0, lerpf(-24.0f, -6.0f, ramp01(block, 13)));
    engine.setToneLevelDb(1, lerpf(SubharmonicEngine::kMinToneLevelDb, -9.0f, ramp01(block, 17)));
    engine.setToneLevelDb(2, lerpf(SubharmonicEngine::kMinToneLevelDb, -9.0f, ramp01(block, 19)));

    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        engine.setToneBreathRate(t, lerpf(SubharmonicEngine::kMinBreathRateHz,
                                          SubharmonicEngine::kMaxBreathRateHz,
                                          ramp01(block + 5 * t, 23)));
        engine.setToneBreathDepth(t, ramp01(block + 7 * t, 29));
        engine.setToneWaveform(
            t, static_cast<SubWaveform>(static_cast<std::uint8_t>((block / 97 + t) % 3)));
    }

    engine.setTrackingAmount(lerpf(0.7f, 1.0f, ramp01(block, 31)));
    engine.setLowpassCutoffHz(lerpf(SubharmonicEngine::kMinLowpassHz,
                                    SubharmonicEngine::kMaxLowpassHz, ramp01(block, 41)));
    engine.setDriveDb(lerpf(SubharmonicEngine::kMinDriveDb, SubharmonicEngine::kMaxDriveDb,
                            ramp01(block, 43)));
    engine.setWetGainDb(lerpf(-12.0f, SubharmonicEngine::kMaxWetGainDb, ramp01(block, 47)));
}

struct SweepRender {
    std::vector<float> tailMain;                        ///< outL over [kWindowEnd, end)
    std::vector<float> tailTap;                         ///< subTap over the same span
    bool               tapAllFinite        = true;      ///< (c1), the WHOLE render
    bool               outsideWindowFinite = true;      ///< (c2), everything but the window
    std::size_t        firstBadTapIndex    = kSweepSamples;
    std::size_t        firstBadOutIndex    = kSweepSamples;
    std::uint32_t      clampCount          = 0;
};

/// @brief 30 s of the swept fixture. `poisonWindow` selects the non-finite input
///        window (the arm under test) or digital silence (the reference).
[[nodiscard]] SweepRender renderSweep(bool poisonWindow) {
    SubharmonicEngine engine;
    engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = kBlock});
    engine.setSeed(0x5EED0119u);
    engine.setFollowerAttackMs(SubharmonicEngine::kDefaultFollowerAttackMs);
    engine.setFollowerReleaseMs(SubharmonicEngine::kMaxFollowerReleaseMs);
    engine.setTrackReferenceDb(kUnclampedTrackReferenceDb);
    engine.setSubToMainEnabled(true);

    SweepRender result;
    result.tailMain.reserve(kTailSamples);
    result.tailTap.reserve(kTailSamples);

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    std::vector<float> tap(kBlock, 0.0f);

    std::size_t done  = 0;
    std::size_t block = 0;
    while (done < kSweepSamples) {
        const std::size_t n = std::min(kBlock, kSweepSamples - done);
        applySweep(engine, block);

        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx      = done + i;
            const bool        inWindow = (idx >= kWindowStart && idx < kWindowEnd);
            if (!inWindow) {
                inL[i] = bodySample(idx);
            } else if (poisonWindow) {
                inL[i] = makeNonFinite(kPatterns[idx % kPatterns.size()].bits);
            } else {
                inL[i] = 0.0f;
            }
            inR[i] = inL[i];
        }

        engine.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(), tap.data(), n);

        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx      = done + i;
            const bool        inWindow = (idx >= kWindowStart && idx < kWindowEnd);

            if (!isFinite(tap[i])) {
                if (result.tapAllFinite) {
                    result.firstBadTapIndex = idx;
                }
                result.tapAllFinite = false;
            }
            if (!inWindow && (!isFinite(outL[i]) || !isFinite(outR[i]))) {
                if (result.outsideWindowFinite) {
                    result.firstBadOutIndex = idx;
                }
                result.outsideWindowFinite = false;
            }
            if (idx >= kWindowEnd) {
                result.tailMain.push_back(outL[i]);
                result.tailTap.push_back(tap[i]);
            }
        }

        done += n;
        ++block;
    }

    result.clampCount = engine.getClampEngagementCount();
    return result;
}

// =============================================================================
// Arm (a): the per-setter rejection helper
// =============================================================================

template <typename Setter, typename Getter>
void requireRejectsNonFinite(const std::string& name, const Setter& setter, const Getter& getter) {
    const float before = getter();
    INFO(name + ": the standing value must itself be finite for this probe to mean anything");
    REQUIRE(isFinite(before));

    for (const NonFinitePattern& pattern : kPatterns) {
        setter(makeNonFinite(pattern.bits));
        INFO(name + " <- " + pattern.name + "; the previous value " + std::to_string(before) +
             " must still stand, and the getter reports " + std::to_string(getter()));
        REQUIRE(getter() == before);
    }
}

}  // namespace

// ==============================================================================
// SC-009
// ==============================================================================

TEST_CASE("SubharmonicEngine_NonFinite", "[subharmonic_engine]") {
    // Self-check FIRST: on a leg where the bit patterns folded to finite garbage
    // every arm below would pass while injecting nothing at all.
    for (const NonFinitePattern& pattern : kPatterns) {
        INFO(std::string("makeNonFinite folded ") + pattern.name + " to a finite value");
        REQUIRE_FALSE(isFinite(makeNonFinite(pattern.bits)));
    }

    SECTION("(a) every float setter rejects NaN and +/-Inf, the previous value standing") {
        SubharmonicEngine engine;
        engine.prepare(kFs, SubharmonicEngine::PrepareConfig{.maxBlockSamples = kBlock});

        // Push a distinctive value into every setter first. Comparing against a
        // NON-DEFAULT standing value is what makes this "the previous value
        // stands" rather than the weaker "the getter still reports a default".
        engine.setFundamentalHz(70.0f);
        engine.setTrackingAmount(0.75f);
        engine.setTrackReferenceDb(-12.0f);
        engine.setFollowerAttackMs(150.0f);
        engine.setFollowerReleaseMs(900.0f);
        engine.setLowpassCutoffHz(300.0f);
        engine.setDriveDb(6.0f);
        engine.setWetGainDb(-3.0f);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            engine.setToneLevelDb(t, -15.0f);
            engine.setToneBreathRate(t, 0.2f);
            engine.setToneBreathDepth(t, 0.6f);
        }

        requireRejectsNonFinite(
            "setFundamentalHz", [&engine](float v) { engine.setFundamentalHz(v); },
            [&engine] { return engine.getFundamentalHz(); });
        requireRejectsNonFinite(
            "setTrackingAmount", [&engine](float v) { engine.setTrackingAmount(v); },
            [&engine] { return engine.getTrackingAmount(); });
        requireRejectsNonFinite(
            "setTrackReferenceDb", [&engine](float v) { engine.setTrackReferenceDb(v); },
            [&engine] { return engine.getTrackReferenceDb(); });
        requireRejectsNonFinite(
            "setFollowerAttackMs", [&engine](float v) { engine.setFollowerAttackMs(v); },
            [&engine] { return engine.getFollowerAttackMs(); });
        requireRejectsNonFinite(
            "setFollowerReleaseMs", [&engine](float v) { engine.setFollowerReleaseMs(v); },
            [&engine] { return engine.getFollowerReleaseMs(); });
        requireRejectsNonFinite(
            "setLowpassCutoffHz", [&engine](float v) { engine.setLowpassCutoffHz(v); },
            [&engine] { return engine.getLowpassCutoffHz(); });
        requireRejectsNonFinite(
            "setDriveDb", [&engine](float v) { engine.setDriveDb(v); },
            [&engine] { return engine.getDriveDb(); });
        requireRejectsNonFinite(
            "setWetGainDb", [&engine](float v) { engine.setWetGainDb(v); },
            [&engine] { return engine.getWetGainDb(); });

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            const std::string suffix = "(tone " + std::to_string(t) + ")";
            requireRejectsNonFinite(
                "setToneLevelDb " + suffix, [&engine, t](float v) { engine.setToneLevelDb(t, v); },
                [&engine, t] { return engine.getToneLevelDb(t); });
            requireRejectsNonFinite(
                "setToneBreathRate " + suffix,
                [&engine, t](float v) { engine.setToneBreathRate(t, v); },
                [&engine, t] { return engine.getToneBreathRate(t); });
            requireRejectsNonFinite(
                "setToneBreathDepth " + suffix,
                [&engine, t](float v) { engine.setToneBreathDepth(t, v); },
                [&engine, t] { return engine.getToneBreathDepth(t); });
        }

        // A rejected setter must also leave the DERIVED state alone: the tone
        // frequencies are still the 70 Hz fundamental's, not a NaN's. This is
        // the assertion that would catch a setter which rejected the argument
        // but had already written masterUnison_.increment.
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " + std::to_string(t) + " frequency after the rejected writes");
            REQUIRE(isFinite(engine.getToneFrequencyHz(t)));
            REQUIRE(engine.getToneFrequencyHz(t) > 0.0f);
        }
    }

    SECTION("(b) the S7.5 probe: poisoned blocker, finite within one sample, correct audio after") {
        const RecoveryRender injected  = renderRecovery(kInjectBlock);
        const RecoveryRender reference = renderRecovery(kNoInjection);

        INFO("the blocker must be clean before the injection - otherwise the arm proves nothing");
        REQUIRE(injected.healthBeforeFinite);

        INFO("injectChainNonFinite() did not reach blocker_ (is the engine prepared?)");
        REQUIRE(injected.healthPoisoned);

        INFO("FR-055 rung 4: the first sample rendered after the injection must be finite");
        REQUIRE(injected.oneSampleFinite);

        INFO("recoverNonFinite() must leave BOTH unguarded stages clean");
        REQUIRE(injected.healthHealed);

        INFO("no sample of the 15 s render may be non-finite");
        REQUIRE(injected.allFinite);
        REQUIRE(reference.allFinite);

        REQUIRE(injected.tailTap.size() == kRecoveryTailSamples);
        REQUIRE(reference.tailTap.size() == kRecoveryTailSamples);

        const auto referenceFp = fp::fingerprintRender(std::span<const float>(reference.tailTap));

        // Fixture self-check: a silent reference would make the comparison below
        // pass for a permanently dead engine.
        INFO("the un-injected reference sub must actually be audible, rms = " +
             std::to_string(referenceFp.rms));
        REQUIRE(referenceFp.rms > 1.0e-4);

        const auto injectedFp = fp::fingerprintRender(std::span<const float>(injected.tailTap));
        const auto comparison = fp::compareFingerprints(injectedFp, referenceFp);

        INFO("the engine must render correct audio 10 s after the fault: " + comparison.detail);
        REQUIRE(comparison.withinTolerance());

        INFO("the FR-054 clamp must not engage on either render");
        REQUIRE(injected.clampCount == 0u);
        REQUIRE(reference.clampCount == 0u);
    }

    SECTION("(c) a 30 s adversarial sweep with a non-finite input for the middle 10 s") {
        const SweepRender poisoned  = renderSweep(true);
        const SweepRender reference = renderSweep(false);

        // ---- (c1) the tap is finite at EVERY sample of the whole render ------
        // The sub chain is generator-driven, so the host's audio must never
        // reach it. A build that routed the input into the chain fails here.
        INFO("subTap went non-finite at sample " + std::to_string(poisoned.firstBadTapIndex) +
             " of " + std::to_string(kSweepSamples));
        REQUIRE(poisoned.tapAllFinite);
        REQUIRE(reference.tapAllFinite);

        // ---- (c2) the main output outside the poisoned window ----------------
        // Nothing is asserted INSIDE the window: FR-050 is an add, and
        // sanitising a host's audio behind its back hides the host's bug.
        INFO("main output went non-finite outside the window at sample " +
             std::to_string(poisoned.firstBadOutIndex));
        REQUIRE(poisoned.outsideWindowFinite);
        REQUIRE(reference.outsideWindowFinite);

        REQUIRE(poisoned.tailMain.size() == kTailSamples);
        REQUIRE(reference.tailMain.size() == kTailSamples);

        const auto referenceTapFp = fp::fingerprintRender(std::span<const float>(reference.tailTap));

        INFO("the reference sub must be audible over the final 10 s, rms = " +
             std::to_string(referenceTapFp.rms));
        REQUIRE(referenceTapFp.rms > 1.0e-4);

        // The tap comparison is the sharp one: the main output's fingerprint is
        // dominated by the untouched dry body, which would mask a muted sub.
        const auto poisonedTapFp = fp::fingerprintRender(std::span<const float>(poisoned.tailTap));
        const auto tapComparison = fp::compareFingerprints(poisonedTapFp, referenceTapFp);
        INFO("final-10 s sub tap vs the finite-input reference: " + tapComparison.detail);
        REQUIRE(tapComparison.withinTolerance());

        const auto poisonedMainFp = fp::fingerprintRender(std::span<const float>(poisoned.tailMain));
        const auto referenceMainFp =
            fp::fingerprintRender(std::span<const float>(reference.tailMain));
        const auto mainComparison = fp::compareFingerprints(poisonedMainFp, referenceMainFp);
        INFO("final-10 s main output vs the finite-input reference: " + mainComparison.detail);
        REQUIRE(mainComparison.withinTolerance());

        // ---- (c3) ------------------------------------------------------------
        INFO("clamp engagements: poisoned = " + std::to_string(poisoned.clampCount) +
             ", reference = " + std::to_string(reference.clampCount));
        REQUIRE(poisoned.clampCount == 0u);
        REQUIRE(reference.clampCount == 0u);
    }
}
