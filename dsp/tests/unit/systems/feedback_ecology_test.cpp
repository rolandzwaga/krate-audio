// ==============================================================================
// Layer 3: System Tests - FeedbackEcology, behavioural cases
// ==============================================================================
// Vorago Phase 5 (specs/vorago-phase5-feedback-ecology): FeedbackEcology
// behavioural cases - SC-005, SC-006, SC-007, SC-008, SC-009, SC-010, SC-011,
// SC-013, SC-014, SC-015, SC-016, SC-017, SC-018, SC-022, SC-023, SC-024,
// SC-025 (plan S12.2, "the default suite").
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase5-feedback-ecology/spec.md
//            specs/vorago-phase5-feedback-ecology/plan.md
//            specs/vorago-phase5-feedback-ecology/tasks.md  (T001 creates this
//                                                            TU; later tasks
//                                                            land the cases)
//
// NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()/infinity()
//   here - this TU is deliberately NOT in dsp/tests/CMakeLists.txt's
//   -fno-fast-math block (tasks.md T001), so the FR-008/FR-009 guards are
//   proved in the /fp:fast + -ffast-math mode the header actually ships in.
//   SC-012 owns bit-pattern injection and lives in
//   feedback_ecology_nonfinite_test.cpp. Finiteness checks use
//   Krate::DSP::detail::isFinite / isNaN / isInf (core/db_utils.h), never
//   std::isnan / std::isinf / std::isfinite.
//
// ALLOCATION DETECTION: include <allocation_detector.h> ONLY. This TU must NOT
//   include <allocation_operator_overrides.h> - dsp_systems_tests already has
//   its single owner and a second include is a duplicate-symbol link error.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/feedback_ecology.h>

#include <krate/dsp/core/db_utils.h>         // detail::constexprLn, detail::kLn2, detail::isFinite
#include <krate/dsp/core/math_constants.h>   // kPi
#include <krate/dsp/core/random.h>           // Xorshift32, for the reference drive
#include <krate/dsp/primitives/crossfading_delay_line.h>  // kCrossfadeThresholdSamples - SC-014 (b2)
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/resonator_bank.h>  // kLn1000, kMaxResonatorQ, the clamp pair

#include <allocation_detector.h>  // AllocationScope / AllocationDetector - SC-007

#include "artifact_detection.h"  // ClickDetector/ClickDetectorConfig - SC-006 (e)
#include "audio_features.h"      // extractAudioFeatures - SC-011's RMS / centroid arm
#include "render_fingerprint.h"  // kSampleTolerance, kMetricTolerance, compareFingerprints

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

// Cases land here from T006 onward. This TU is registered in
// dsp/tests/CMakeLists.txt's dsp_systems_tests source list (the list is
// ENUMERATED, not globbed - an unregistered TU silently drops out of the build
// and its cases never run).

using Catch::Approx;
using Krate::DSP::FeedbackEcology;
using Krate::DSP::SVF;

namespace {

/// THE REFERENCE DRIVE (plan S12, tasks.md "The reference patch and the
/// reference drive"): white noise at -12 dBFS **RMS**, not peak. The two differ
/// by 10-12 dB for white noise and every governor assertion turns on which is
/// meant. Xorshift32::nextFloat() is uniform on [-1, +1], whose RMS is
/// 1/sqrt(3), so the amplitude that lands the RMS on 10^(-12/20) = 0.251189 is
/// 0.251189 * sqrt(3) = 0.435072.
constexpr float kReferenceDriveAmplitude = 0.435072f;
constexpr std::uint32_t kReferenceDriveSeed = 0x0D817Eu;

/// Fills both channels with the reference drive. Fixed seed, so every case that
/// uses it drives the component with the same signal.
void fillReferenceDrive(float* left, float* right, std::size_t numSamples,
                        std::uint32_t seed = kReferenceDriveSeed) {
    Krate::DSP::Xorshift32 rng(seed);
    for (std::size_t i = 0; i < numSamples; ++i) {
        left[i] = rng.nextFloat() * kReferenceDriveAmplitude;
        right[i] = rng.nextFloat() * kReferenceDriveAmplitude;
    }
}

}  // namespace

// ==============================================================================
// T006 - the S1.2 constants table
// ==============================================================================
// Every figure below is load-bearing somewhere else in the phase: the four
// default tables set the reference patch every later criterion renders, the
// ladder constants (kMaxLoopGain, kMaxCouplingPerPair, kMaxTotalLoopGain,
// kOutputClamp) are FR-041's rungs, and kConstructionSampleRate is what keeps
// the two cached clamp bounds ORDERED on an unprepared instance (R-8:
// std::clamp with hi < lo is UB that MSVC's _STL_VERIFY traps).
//
// STATIC_REQUIRE, not REQUIRE: these are compile-time constants, so a silent
// "tidy-up" that moves one must fail the BUILD rather than a run.
// ==============================================================================
TEST_CASE("FeedbackEcology_ConstantsTable", "[feedback_ecology]") {
    SECTION("topology and the control grid") {
        STATIC_REQUIRE(FeedbackEcology::kMaxLoops == std::size_t{6});
        STATIC_REQUIRE(FeedbackEcology::kControlChunkSamples == std::size_t{64});
        STATIC_REQUIRE(FeedbackEcology::kMaxLaneDecimation == std::size_t{17});
    }

    SECTION("the filter stage (FR-011, FR-012, FR-053)") {
        STATIC_REQUIRE(FeedbackEcology::kDefaultFilterQ == SVF::kButterworthQ);
        STATIC_REQUIRE(FeedbackEcology::kMaxFilterQ == SVF::kButterworthQ);
        STATIC_REQUIRE(FeedbackEcology::kMinFilterQ == SVF::kMinQ);
        STATIC_REQUIRE(FeedbackEcology::kMinCutoffHz == 20.0f);

        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopCutoffHz[0] == 2400.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopCutoffHz[1] == 1700.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopCutoffHz[2] == 1200.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopCutoffHz[3] == 850.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopCutoffHz[4] == 600.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopCutoffHz[5] == 420.0f);
    }

    SECTION("the delay stage (FR-020, FR-022)") {
        STATIC_REQUIRE(FeedbackEcology::kMinDelayMs == 10.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxDelayMs == 500.0f);
        STATIC_REQUIRE(FeedbackEcology::kDelayHeadroomMs == 20.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxDelaySeconds == 0.52f);
        STATIC_REQUIRE(FeedbackEcology::kCrossfadeMs == 20.0f);

        // FR-022: mutually prime, so no two loops' round trips lock into a
        // common period.
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopDelayMs[0] == 41.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopDelayMs[1] == 67.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopDelayMs[2] == 109.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopDelayMs[3] == 173.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopDelayMs[4] == 281.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopDelayMs[5] == 449.0f);
    }

    SECTION("the resonator stage (FR-013, FR-014)") {
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopResonanceHz[0] == 1200.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopResonanceHz[1] == 850.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopResonanceHz[2] == 600.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopResonanceHz[3] == 425.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopResonanceHz[4] == 300.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopResonanceHz[5] == 210.0f);

        STATIC_REQUIRE(FeedbackEcology::kDefaultResonanceRt60 == 1.0f);
        STATIC_REQUIRE(FeedbackEcology::kDcBlockerCutoffHz == 10.0f);
    }

    SECTION("gains and the FR-041 boundedness ladder") {
        STATIC_REQUIRE(FeedbackEcology::kMinLoopGain == 0.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxLoopGain == 0.90f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopGain == 0.72f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultLoopInputGain == 1.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxCouplingPerPair == 0.5f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultCoupling == 0.04f);
        STATIC_REQUIRE(FeedbackEcology::kMaxTotalLoopGain == 0.95f);
        STATIC_REQUIRE(FeedbackEcology::kOutputClamp == 4.0f);
    }

    SECTION("the governor (FR-043..FR-045)") {
        STATIC_REQUIRE(FeedbackEcology::kDefaultGovernorThresholdDb == -52.0f);
        STATIC_REQUIRE(FeedbackEcology::kMinGovernorThresholdDb == -72.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxGovernorThresholdDb == 0.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultGovernorRatio == 8.0f);
        STATIC_REQUIRE(FeedbackEcology::kMinGovernorRatio == 1.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxGovernorRatio == 20.0f);
        STATIC_REQUIRE(FeedbackEcology::kGovernorMinGain == 0.05f);
        STATIC_REQUIRE(FeedbackEcology::kGovernorAttackMs == 20.0f);
        STATIC_REQUIRE(FeedbackEcology::kGovernorReleaseMs == 800.0f);
    }

    SECTION("wander (FR-052, FR-053, FR-055)") {
        STATIC_REQUIRE(FeedbackEcology::kDefaultWanderRateHz == 0.03f);
        STATIC_REQUIRE(FeedbackEcology::kMinWanderRateHz == 0.002f);
        STATIC_REQUIRE(FeedbackEcology::kMaxWanderRateHz == 1.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxDelayWanderFraction == 0.5f);
        STATIC_REQUIRE(FeedbackEcology::kMaxCutoffWanderOctaves == 4.0f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultCutoffWanderOctaves == 0.5f);

        // FR-023: DERIVED per loop, not a flat figure - the header carries the
        // sigma_delta derivation table that produced these six.
        STATIC_REQUIRE(FeedbackEcology::kDefaultDelayWanderFraction[0] == 0.16f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultDelayWanderFraction[1] == 0.10f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultDelayWanderFraction[2] == 0.06f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultDelayWanderFraction[3] == 0.04f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultDelayWanderFraction[4] == 0.03f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultDelayWanderFraction[5] == 0.02f);
    }

    SECTION("ramps and the output stage (FR-072, FR-076)") {
        STATIC_REQUIRE(FeedbackEcology::kGainRampMs == 50.0f);
        STATIC_REQUIRE(FeedbackEcology::kMixRampMs == 20.0f);
        STATIC_REQUIRE(FeedbackEcology::kCouplingSmoothMs == 20.0f);
        STATIC_REQUIRE(FeedbackEcology::kGovernorRampMs == 20.0f);

        STATIC_REQUIRE(FeedbackEcology::kDefaultMix == 0.15f);
        STATIC_REQUIRE(FeedbackEcology::kDefaultWetGainDb == 0.0f);
        STATIC_REQUIRE(FeedbackEcology::kMinWetGainDb == -24.0f);
        STATIC_REQUIRE(FeedbackEcology::kMaxWetGainDb == 24.0f);
    }

    SECTION("life cycle and the rate floor (FR-060, FR-083, FR-002)") {
        STATIC_REQUIRE(FeedbackEcology::kWakeSilenceEpsilon == 1.0e-6f);
        STATIC_REQUIRE(FeedbackEcology::kMinUsableSampleRate == 8000.0);
        STATIC_REQUIRE(FeedbackEcology::kConstructionSampleRate == 48000.0);
    }

    SECTION("the nested types exist with the specified shape") {
        // APPEND ONLY - FilterMode becomes a persisted plugin parameter at
        // Phase 12, so the underlying values may never be renumbered.
        STATIC_REQUIRE(static_cast<std::uint8_t>(FeedbackEcology::FilterMode::Lowpass)
                       == std::uint8_t{0});
        STATIC_REQUIRE(static_cast<std::uint8_t>(FeedbackEcology::FilterMode::Bandpass)
                       == std::uint8_t{1});
        STATIC_REQUIRE(static_cast<std::uint8_t>(FeedbackEcology::FilterMode::Highpass)
                       == std::uint8_t{2});

        // Designated initialisers are mandatory (Clang errors on narrowing in a
        // positional brace init where MSVC does not).
        constexpr FeedbackEcology::PrepareConfig kDefaults{};
        STATIC_REQUIRE(kDefaults.maxBlockSamples == std::size_t{2048});
        STATIC_REQUIRE(kDefaults.numLoops == FeedbackEcology::kMaxLoops);

        constexpr FeedbackEcology::PrepareConfig kCustom{.maxBlockSamples = 512, .numLoops = 5};
        STATIC_REQUIRE(kCustom.maxBlockSamples == std::size_t{512});
        STATIC_REQUIRE(kCustom.numLoops == std::size_t{5});
    }
}

// ==============================================================================
// T007 - SC-017, the whole control surface
// ==============================================================================
// Every float setter driven to +/-10x its range; every index-taking method at
// kMaxLoops, kMaxLoops + 1 and SIZE_MAX; the coupling diagonal; the unprepared
// state (S9) including the SEEDED-BOUNDS arm that a 0.0f maxResonanceHz_ turns
// into std::clamp(1200.0f, 20.0f, 0.0f) - UB, trapped by MSVC's _STL_VERIFY, so
// this case must be run under an MSVC DEBUG build as well as under ASan; and the
// no-prepared_-gate arm (FR-006, FR-009 - there is no fourth rule).
//
// NON-FINITE REJECTION IS NOT HERE. It is proved in
// feedback_ecology_nonfinite_test.cpp (T016, SC-012), the only TU compiled
// -fno-fast-math. This TU must never name a non-finite value, which is also why
// setCouplingMatrix's "one non-finite entry leaves that pair's previous value
// standing" clause is asserted there and not below.
// ==============================================================================
TEST_CASE("FeedbackEcology_ControlSurfaceClamps", "[feedback_ecology]") {
    // The three out-of-range indices FR-009 names. The largest is spelled through
    // numeric_limits so the intent survives a reader.
    static constexpr std::size_t kBadIndices[] = {
        FeedbackEcology::kMaxLoops,
        FeedbackEcology::kMaxLoops + 1,
        std::numeric_limits<std::size_t>::max(),
    };

    SECTION("every float setter clamps at BOTH ends and its getter reports the clamp") {
        FeedbackEcology fe;

        // --- per-loop gains --------------------------------------------------
        fe.setLoopGain(0, 9.0f);
        REQUIRE(fe.getLoopGain(0) == FeedbackEcology::kMaxLoopGain);
        fe.setLoopGain(0, -5.0f);
        REQUIRE(fe.getLoopGain(0) == FeedbackEcology::kMinLoopGain);

        fe.setLoopInputGain(1, 10.0f);
        REQUIRE(fe.getLoopInputGain(1) == 1.0f);  // FR-074: [0, 1]
        fe.setLoopInputGain(1, -10.0f);
        REQUIRE(fe.getLoopInputGain(1) == 0.0f);

        // --- coupling ---------------------------------------------------------
        fe.setCoupling(0, 1, 5.0f);
        REQUIRE(fe.getCoupling(0, 1) == FeedbackEcology::kMaxCouplingPerPair);
        fe.setCoupling(0, 1, -5.0f);  // FR-032 / D-5: coupling is NON-NEGATIVE here
        REQUIRE(fe.getCoupling(0, 1) == 0.0f);

        // --- the filter stage -------------------------------------------------
        // FR-012's rung-1 ceiling: the SVF Lowpass and Highpass mixes peak AT Q,
        // so a Q above Butterworth would put gain > 1 inside a feedback loop.
        fe.setLoopFilterQ(0, 30.0f);
        REQUIRE(fe.getLoopFilterQ(0) == SVF::kButterworthQ);
        fe.setLoopFilterQ(0, -1.0f);
        REQUIRE(fe.getLoopFilterQ(0) == FeedbackEcology::kMinFilterQ);

        fe.setLoopCutoffHz(0, 1.0e6f);
        REQUIRE(fe.getLoopCutoffHz(0)
                == Approx(48000.0f * SVF::kMaxCutoffRatio).margin(1.0e-3));
        fe.setLoopCutoffHz(0, 0.5f);
        REQUIRE(fe.getLoopCutoffHz(0) == FeedbackEcology::kMinCutoffHz);

        // --- the delay stage ----------------------------------------------------
        fe.setLoopDelayMs(0, 5000.0f);
        REQUIRE(fe.getLoopDelayMs(0) == 500.0f);
        fe.setLoopDelayMs(0, 0.1f);
        REQUIRE(fe.getLoopDelayMs(0) == 10.0f);

        // --- the resonator stage ------------------------------------------------
        fe.setLoopResonanceHz(0, 1.0e6f);
        REQUIRE(fe.getLoopResonanceHz(0)
                == Approx(48000.0f * Krate::DSP::kMaxResonatorFrequencyRatio).margin(1.0e-3));
        fe.setLoopResonanceHz(0, 0.5f);
        REQUIRE(fe.getLoopResonanceHz(0) == Krate::DSP::kMinResonatorFrequency);

        // --- the output stage ----------------------------------------------------
        fe.setWetGain(100.0f);
        REQUIRE(fe.getWetGain() == 24.0f);
        fe.setWetGain(-100.0f);
        REQUIRE(fe.getWetGain() == -24.0f);

        fe.setMix(-1.0f);
        REQUIRE(fe.getMix() == 0.0f);
        fe.setMix(10.0f);
        REQUIRE(fe.getMix() == 1.0f);

        // --- the governor ---------------------------------------------------------
        fe.setGovernorRatio(1000.0f);
        REQUIRE(fe.getGovernorRatio() == 20.0f);
        fe.setGovernorRatio(-1000.0f);
        REQUIRE(fe.getGovernorRatio() == 1.0f);

        fe.setGovernorThresholdDb(10.0f);
        REQUIRE(fe.getGovernorThresholdDb() == 0.0f);
        fe.setGovernorThresholdDb(-360.0f);
        REQUIRE(fe.getGovernorThresholdDb() == -72.0f);

        // --- wander depths ---------------------------------------------------------
        fe.setLoopDelayWander(0, 5.0f);
        REQUIRE(fe.getLoopDelayWander(0) == 0.5f);
        fe.setLoopDelayWander(0, -5.0f);
        REQUIRE(fe.getLoopDelayWander(0) == 0.0f);

        fe.setLoopCutoffWander(0, 40.0f);
        REQUIRE(fe.getLoopCutoffWander(0) == 4.0f);
        fe.setLoopCutoffWander(0, -40.0f);
        REQUIRE(fe.getLoopCutoffWander(0) == 0.0f);

        // --- the loop count ---------------------------------------------------------
        fe.setNumLoops(0);
        REQUIRE(fe.getNumLoops() == std::size_t{1});
        REQUIRE(fe.getNormalisationLoopCount() == std::size_t{1});
        fe.setNumLoops(99);
        REQUIRE(fe.getNumLoops() == FeedbackEcology::kMaxLoops);
        REQUIRE(fe.getNormalisationLoopCount() == FeedbackEcology::kMaxLoops);
    }

    SECTION("an out-of-range index is a silent no-op, and its getter is neutral") {
        FeedbackEcology fe;

        // Snapshot the in-range state so a stray write is visible, not merely
        // absent.
        const float gain0 = fe.getLoopGain(0);
        const float delay0 = fe.getLoopDelayMs(0);
        const float cutoff0 = fe.getLoopCutoffHz(0);
        const float coupling01 = fe.getCoupling(0, 1);

        for (const std::size_t bad : kBadIndices) {
            // Every index-taking SETTER: a silent no-op.
            fe.setLoopFilterMode(bad, FeedbackEcology::FilterMode::Highpass);
            fe.setLoopCutoffHz(bad, 777.0f);
            fe.setLoopFilterQ(bad, 0.5f);
            fe.setLoopDelayMs(bad, 123.0f);
            fe.setLoopResonanceHz(bad, 333.0f);
            fe.setLoopResonanceRt60(bad, 2.0f);
            fe.setLoopGain(bad, 0.5f);
            fe.setLoopInputGain(bad, 0.25f);
            fe.setLoopDelayWander(bad, 0.3f);
            fe.setLoopCutoffWander(bad, 1.5f);
            fe.setCoupling(bad, 0, 0.3f);
            fe.setCoupling(0, bad, 0.3f);
            fe.setCoupling(bad, bad, 0.3f);

            // Every index-taking GETTER: the documented neutral, WITHOUT indexing
            // the array (this arm is the one to run under ASan).
            REQUIRE(fe.getLoopDelayMs(bad) == 0.0f);
            REQUIRE(fe.getLoopCutoffHz(bad) == 0.0f);
            REQUIRE(fe.getLoopFilterMode(bad) == FeedbackEcology::FilterMode::Lowpass);
            REQUIRE(fe.getLoopFilterQ(bad) == 0.0f);
            REQUIRE(fe.getLoopResonanceHz(bad) == 0.0f);
            REQUIRE(fe.getLoopResonanceRt60(bad) == 0.0f);
            REQUIRE(fe.getLoopGain(bad) == 0.0f);
            REQUIRE(fe.getLoopInputGain(bad) == 0.0f);
            REQUIRE(fe.getLoopDelayWander(bad) == 0.0f);
            REQUIRE(fe.getLoopCutoffWander(bad) == 0.0f);
            REQUIRE(fe.getCoupling(bad, 0) == 0.0f);
            REQUIRE(fe.getCoupling(0, bad) == 0.0f);
            REQUIRE(fe.getCoupling(bad, bad) == 0.0f);
            REQUIRE(fe.getLoopCurrentCutoffHz(bad) == 0.0f);
            REQUIRE(fe.getLoopAppliedCoupling(bad, 0) == 0.0f);
            REQUIRE(fe.getLoopAppliedCoupling(0, bad) == 0.0f);
        }

        REQUIRE(fe.getLoopGain(0) == gain0);
        REQUIRE(fe.getLoopDelayMs(0) == delay0);
        REQUIRE(fe.getLoopCutoffHz(0) == cutoff0);
        REQUIRE(fe.getCoupling(0, 1) == coupling01);
    }

    SECTION("the coupling diagonal is a no-op, and setCouplingMatrix obeys the same rules") {
        FeedbackEcology fe;

        fe.setCoupling(2, 2, 0.4f);
        REQUIRE(fe.getCoupling(2, 2) == 0.0f);
        REQUIRE(fe.getLoopAppliedCoupling(2, 2) == 0.0f);

        // Per-entry rule: each entry is clamped independently and the diagonal is
        // ignored. (The non-finite-entry clause is T016's - this TU must never
        // name a non-finite value.)
        std::array<std::array<float, FeedbackEcology::kMaxLoops>, FeedbackEcology::kMaxLoops> m{};
        for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                m[from][to] = (from == to) ? 0.9f : 0.25f;
            }
        }
        m[0][1] = 5.0f;   // above kMaxCouplingPerPair
        m[1][0] = -3.0f;  // below zero
        fe.setCouplingMatrix(m);

        REQUIRE(fe.getCoupling(0, 1) == FeedbackEcology::kMaxCouplingPerPair);
        REQUIRE(fe.getCoupling(1, 0) == 0.0f);
        REQUIRE(fe.getCoupling(2, 3) == 0.25f);
        REQUIRE(fe.getCoupling(5, 4) == 0.25f);
        for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
            REQUIRE(fe.getCoupling(i, i) == 0.0f);
        }
    }

    SECTION("an unprepared instance reports its post-construction defaults (S9)") {
        const FeedbackEcology fe;

        // The only two exceptions to the rule.
        REQUIRE(fe.getAllocatedBytes() == std::size_t{0});
        REQUIRE_FALSE(fe.isPrepared());

        // Everything else is the post-construction default, because FR-002 leaves
        // construction in the state prepare(48000.0, PrepareConfig{}) produces.
        REQUIRE(fe.getNumLoops() == FeedbackEcology::kMaxLoops);
        REQUIRE(fe.getMaxBlockSamples() == std::size_t{2048});
        REQUIRE(fe.getSampleRate() == FeedbackEcology::kConstructionSampleRate);
        REQUIRE(fe.getMix() == FeedbackEcology::kDefaultMix);
        REQUIRE(fe.getWetGain() == FeedbackEcology::kDefaultWetGainDb);
        REQUIRE(fe.getGovernorThresholdDb() == FeedbackEcology::kDefaultGovernorThresholdDb);
        REQUIRE(fe.getGovernorRatio() == FeedbackEcology::kDefaultGovernorRatio);
        REQUIRE(fe.getWanderRate() == FeedbackEcology::kDefaultWanderRateHz);
        REQUIRE(fe.isWanderEnabled());

        REQUIRE(fe.getLoopDelayMs(0) == 41.0f);
        REQUIRE(fe.getLoopCutoffHz(3) == 850.0f);
        for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
            INFO("loop " << i);
            REQUIRE(fe.getLoopDelayMs(i) == FeedbackEcology::kDefaultLoopDelayMs[i]);
            REQUIRE(fe.getLoopCutoffHz(i) == FeedbackEcology::kDefaultLoopCutoffHz[i]);
            REQUIRE(fe.getLoopResonanceHz(i) == FeedbackEcology::kDefaultLoopResonanceHz[i]);
            REQUIRE(fe.getLoopDelayWander(i) == FeedbackEcology::kDefaultDelayWanderFraction[i]);
            REQUIRE(fe.getLoopGain(i) == 0.72f);
            REQUIRE(fe.getLoopInputGain(i) == FeedbackEcology::kDefaultLoopInputGain);
            REQUIRE(fe.getLoopFilterQ(i) == FeedbackEcology::kDefaultFilterQ);
            REQUIRE(fe.getLoopCutoffWander(i) == FeedbackEcology::kDefaultCutoffWanderOctaves);
            REQUIRE(fe.getLoopFilterMode(i) == FeedbackEcology::FilterMode::Lowpass);
        }

        // FR-033's default ring: kDefaultCoupling to the two cyclic neighbours,
        // 0 elsewhere, nothing on the diagonal.
        for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                const bool neighbour =
                    (to == (from + 1) % FeedbackEcology::kMaxLoops)
                    || (to
                        == (from + FeedbackEcology::kMaxLoops - 1) % FeedbackEcology::kMaxLoops);
                const float expected =
                    (from != to && neighbour) ? FeedbackEcology::kDefaultCoupling : 0.0f;
                INFO("pair " << from << " -> " << to);
                REQUIRE(fe.getCoupling(from, to) == expected);
            }
        }
    }

    SECTION("the seeded-bounds arm: getLoopResonanceRt60 on a NEVER-PREPARED instance") {
        // R-8. getLoopResonanceRt60 clamps against maxResonanceHz_. With a 0.0f
        // initialiser this call is std::clamp(1200.0f, 20.0f, 0.0f): inverted
        // bounds, UB, and MSVC's <algorithm> traps it with _STL_VERIFY. The defect
        // this arm guards is a TRAP rather than a wrong value, so a Release run can
        // sail past it - run this case under an MSVC Debug build as well as under
        // ASan.
        //
        // The table is DERIVATION TABLE 2's realised column: rt60ToQ caps Q at
        // kMaxResonatorQ = 100, so the longest reachable ring at a centre f is
        // 100 * kLn1000 / (pi * f). The 1.0 s default is exactly reachable only at
        // the LOWEST default centre (210 Hz, where the request needs Q = 95.503)
        // and is reduced above it.
        const FeedbackEcology fe;
        static constexpr float kRealisedRt60[FeedbackEcology::kMaxLoops] = {
            0.1833f, 0.2587f, 0.3665f, 0.5174f, 0.7330f, 1.0000f,
        };
        for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
            INFO("loop " << i);
            REQUIRE(fe.getLoopResonanceRt60(i) == Approx(kRealisedRt60[i]).margin(1.0e-3));
        }
    }

    SECTION("there is no prepared_ gate: setters take effect before prepare() (FR-006)") {
        FeedbackEcology fe;
        REQUIRE_FALSE(fe.isPrepared());

        fe.setMix(0.42f);
        fe.setLoopGain(2, 0.5f);
        fe.setLoopDelayMs(3, 200.0f);
        fe.setCoupling(0, 1, 0.3f);

        REQUIRE(fe.getMix() == 0.42f);
        REQUIRE(fe.getLoopGain(2) == 0.5f);
        REQUIRE(fe.getLoopDelayMs(3) == 200.0f);
        REQUIRE(fe.getCoupling(0, 1) == 0.3f);
        REQUIRE_FALSE(fe.isPrepared());
        // The other half of FR-005 (c) - that a later prepare() DISCARDS all four -
        // is asserted in T008's lifecycle case, where prepare() exists.
    }

    SECTION("constexpr-log equivalence: the constexprLn series matches std::log2") {
        // S5.4: any constexpr log2 constant in the header must be
        // detail::constexprLn(x) / detail::kLn2 and NEVER a constexpr std::log2,
        // which is a GCC/MSVC builtin extension Clang rejects - the macOS and Linux
        // legs would break while Windows stayed green. applyDefaults() caches
        // baseLog2Cutoff through that series and setLoopCutoffHz through runtime
        // std::log2, so the two must agree or re-setting a default cutoff would
        // move the FR-053 wander centre.
        for (const float hz : FeedbackEcology::kDefaultLoopCutoffHz) {
            INFO("cutoff " << hz);
            const float series = Krate::DSP::detail::constexprLn(hz) / Krate::DSP::detail::kLn2;
            REQUIRE(series == Approx(std::log2(hz)).margin(1.0e-6));
        }
        for (const float hz : FeedbackEcology::kDefaultLoopResonanceHz) {
            INFO("resonance " << hz);
            const float series = Krate::DSP::detail::constexprLn(hz) / Krate::DSP::detail::kLn2;
            REQUIRE(series == Approx(std::log2(hz)).margin(1.0e-6));
        }
    }
}

// ==============================================================================
// T015 helpers - the SC-023 ring measurement and SC-011's per-rate render
// ==============================================================================
// They live HERE, above FeedbackEcology_ResonatorRing and
// FeedbackEcology_SampleRate, because this file is ordered by CRITERION and both
// of those cases sit above the T009 helper block. Every anonymous namespace in a
// TU is the SAME namespace, so the two declarations below name the functions
// defined further down rather than introducing second copies of them - which is
// the point: makeReference()'s setter ORDER is load-bearing (prepare() restores
// kDefaultMix on every call, S3.2), and a second local copy of that fixture is
// exactly how the two would silently diverge.
// ==============================================================================
namespace {

/// Defined below with default arguments; declared here without them, so every
/// call in this region passes all four explicitly.
void makeReference(FeedbackEcology& fe, double fs, std::size_t loops, std::uint32_t seed);

/// Defined below. Renders `total` samples in fixed-size blocks.
void renderBlocks(FeedbackEcology& fe, const float* inL, const float* inR, float* outL,
                  float* outR, std::size_t total, std::size_t blockSize);

/// SC-023's decay estimate, taken from an already-rendered FR-073 tap.
struct RingMeasurement {
    double rt60Seconds = 0.0;
    double slopeDbPerSecond = 0.0;
    double initialPeak = 0.0;
    double initialPeakTime = 0.0;
    std::size_t pointsFitted = 0;
    bool valid = false;
};

/// SC-023's measurement, verbatim from the criterion: the peak of |tap| in each
/// successive ONE-CYCLE window (the envelope of a ringing bandpass is only
/// defined per cycle - a shorter window measures the carrier), converted to dB,
/// least-squares fitted over the span from -5 dB to -40 dB below the initial
/// peak, then extrapolated to -60 dB.
///
/// The walk starts at the peak window and STOPS at the first window under
/// -40 dB rather than filtering the whole array: a late window sitting on the
/// numerical floor would otherwise re-enter the fit and flatten the slope, which
/// inflates the reported RT60 - the direction that would hide the very defect
/// this criterion exists to catch.
[[nodiscard]] RingMeasurement measureRingRt60(const std::vector<float>& tap, double sampleRate,
                                              double centreHz) {
    RingMeasurement out;
    if (tap.empty() || sampleRate <= 0.0 || centreHz <= 0.0) return out;

    const auto window =
        static_cast<std::size_t>(std::max(1.0, std::round(sampleRate / centreHz)));
    const std::size_t windows = tap.size() / window;
    if (windows < 8) return out;

    std::vector<double> peakLin(windows, 0.0);
    std::vector<double> times(windows, 0.0);
    std::size_t peakIndex = 0;
    double peakValue = 0.0;
    for (std::size_t w = 0; w < windows; ++w) {
        double p = 0.0;
        for (std::size_t s = w * window; s < (w + 1) * window; ++s) {
            p = std::max(p, std::abs(static_cast<double>(tap[s])));
        }
        peakLin[w] = p;
        times[w] = (static_cast<double>(w) + 0.5) * static_cast<double>(window) / sampleRate;
        if (p > peakValue) {
            peakValue = p;
            peakIndex = w;
        }
    }
    if (peakValue <= 0.0) return out;
    out.initialPeak = peakValue;
    out.initialPeakTime = times[peakIndex];

    const double initialDb = 20.0 * std::log10(peakValue);

    double sumT = 0.0;
    double sumD = 0.0;
    double sumTT = 0.0;
    double sumTD = 0.0;
    std::size_t n = 0;
    for (std::size_t w = peakIndex; w < windows; ++w) {
        if (peakLin[w] <= 0.0) break;
        const double db = 20.0 * std::log10(peakLin[w]);
        if (db > initialDb - 5.0) continue;   // still inside the onset
        if (db < initialDb - 40.0) break;     // past the fit span
        sumT += times[w];
        sumD += db;
        sumTT += times[w] * times[w];
        sumTD += times[w] * db;
        ++n;
    }
    out.pointsFitted = n;
    if (n < 4) return out;

    const double dn = static_cast<double>(n);
    const double denom = dn * sumTT - sumT * sumT;
    if (!(denom > 0.0)) return out;
    const double slope = (dn * sumTD - sumT * sumD) / denom;  // dB per second
    out.slopeDbPerSecond = slope;
    if (!(slope < 0.0)) return out;

    out.rt60Seconds = -60.0 / slope;
    out.valid = true;
    return out;
}

/// One SC-023 arm's render, plus the bookkeeping figure read off THE SAME
/// INSTANCE - reading it from a second, freshly prepared object would compare
/// the measurement against a different instance's table and lose arm (c)'s
/// second half, which is the only half that can catch a regression to
/// Biquad::configure's kMaxQ = 30.
struct RingArmRender {
    std::vector<float> tap;
    double filedRt60Seconds = 0.0;
    double centreHz = 0.0;
    double cutoffHz = 0.0;
};

/// SC-023's fixture: NO feedback anywhere (own gain 0 on every loop, every
/// off-diagonal coupling 0), the SVF in front of the resonator made
/// near-transparent at the centre, wander off, the loop under test the only open
/// input tap, then `reset()` - so what the FR-073 tap carries is the loop
/// CHAIN's own impulse response and not a loop resonance.
[[nodiscard]] RingArmRender renderRingImpulseTap(std::size_t loop, double sampleRate,
                                                 double seconds) {
    constexpr std::size_t kLoopCount = FeedbackEcology::kMaxLoops;
    const auto total = static_cast<std::size_t>(seconds * sampleRate) + 1u;

    RingArmRender out;

    FeedbackEcology fe;
    fe.setSeed(0x5EEDu);
    fe.prepare(sampleRate,
               FeedbackEcology::PrepareConfig{.maxBlockSamples = 4096, .numLoops = kLoopCount});
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);
    fe.setWanderEnabled(false);

    for (std::size_t i = 0; i < kLoopCount; ++i) {
        fe.setLoopGain(i, 0.0f);
        fe.setLoopInputGain(i, i == loop ? 1.0f : 0.0f);
        for (std::size_t j = 0; j < kLoopCount; ++j) {
            fe.setCoupling(i, j, 0.0f);  // the diagonal is a documented no-op
        }
    }

    fe.setLoopFilterMode(loop, FeedbackEcology::FilterMode::Lowpass);
    fe.setLoopCutoffHz(loop, static_cast<float>(sampleRate) * SVF::kMaxCutoffRatio);

    // configure -> reset() -> render (tasks.md "Conventions"): reset() snaps
    // every ramp and smoother AND pushes the just-set cutoff into the SVF
    // through snapControlState(), so the patch is in force from sample 0.
    fe.reset();

    out.centreHz = static_cast<double>(fe.getLoopResonanceHz(loop));
    out.cutoffHz = static_cast<double>(fe.getLoopCutoffHz(loop));

    std::vector<float> inL(total, 0.0f);
    std::vector<float> inR(total, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;  // renderChunk step 0 forms monoIn = 0.5 * (L + R) = 1.0
    std::vector<float> outLBuf(total, 0.0f);
    std::vector<float> outRBuf(total, 0.0f);
    out.tap.assign(total, 0.0f);

    constexpr std::size_t kBlock = 4096;
    std::size_t done = 0;
    while (done < total) {
        const std::size_t n = std::min(kBlock, total - done);
        // loopTaps indexes as loopTaps[i][tapOffset + s] with tapOffset relative
        // to THIS call, so the pointer has to be re-based per block.
        std::array<float*, kLoopCount> blockTaps{};
        blockTaps[loop] = out.tap.data() + done;
        fe.processBlockTapped(inL.data() + done, inR.data() + done, outLBuf.data() + done,
                              outRBuf.data() + done, blockTaps.data(), n);
        done += n;
    }

    out.filedRt60Seconds = static_cast<double>(fe.getLoopResonanceRt60(loop));
    return out;
}

/// SC-011's per-rate figures, measured on the reference patch.
struct RateRenderMetrics {
    double rmsDbfs = 0.0;
    double centroidHz = 0.0;
    std::uint32_t clampEngagements = 0;
    std::uint32_t nonFiniteResets = 0;
};

/// Renders the reference patch at `fs` and measures the tail. The first
/// `measureFromSeconds` are DISCARDED: the delay lines open empty and the
/// longest loop is 449 ms, so the opening is a fill transient whose level says
/// nothing about the rate.
[[nodiscard]] RateRenderMetrics renderRateMetrics(double fs, double totalSeconds,
                                                  double measureFromSeconds) {
    RateRenderMetrics out;
    const auto total = static_cast<std::size_t>(totalSeconds * fs);
    const auto skip = static_cast<std::size_t>(measureFromSeconds * fs);

    std::vector<float> inL(total, 0.0f);
    std::vector<float> inR(total, 0.0f);
    fillReferenceDrive(inL.data(), inR.data(), total);

    std::vector<float> outL(total, 0.0f);
    std::vector<float> outR(total, 0.0f);

    FeedbackEcology fe;
    makeReference(fe, fs, FeedbackEcology::kMaxLoops, 0x5EEDu);
    renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), total, std::size_t{512});

    out.clampEngagements = fe.getClampEngagementCount();
    out.nonFiniteResets = fe.getNonFiniteResetCount();

    const std::vector<float> measured(outL.begin() + static_cast<std::ptrdiff_t>(skip),
                                      outL.end());
    const Krate::Test::AudioFeatures features = Krate::Test::extractAudioFeatures(measured, fs);
    out.rmsDbfs = features.rmsDbfs;
    out.centroidHz = features.centroidHz;
    return out;
}

}  // namespace

// ==============================================================================
// T007 - SC-023 arm (c), the table half
// ==============================================================================
// Arms (a) and (b) MEASURE the realised ring through the FR-073 tap and need the
// render path; T015 has since landed them in the SECTIONs below, using the
// measurement helpers in the T015 block above this banner.
//
// Arm (c) has two halves. This is the TABLE half: the getter reproduces
// DERIVATION TABLE 2 on a prepared instance. The half that ties the bookkeeping
// field to the coefficients ACTUALLY IN FORCE - the getter agreeing with the
// MEASURED ring - is T015's, and it is the only one that can catch a regression
// to Biquad::configure (kMaxQ = 30), because appliedResonanceQ is stored BEFORE
// the coefficients are written.
// ==============================================================================
TEST_CASE("FeedbackEcology_ResonatorRing", "[feedback_ecology]") {
    // T015 - arms (a) and (b), the MEASURED ring, and with them the second half
    // of arm (c): getLoopResonanceRt60(i) agreeing with the coefficients ACTUALLY
    // IN FORCE. appliedResonanceQ is stored BEFORE the coefficients are written,
    // so the table half above passes unchanged on a build that regressed to
    // Biquad::configure / BiquadCoefficients::calculate (biquad.h's kMaxQ = 30);
    // only the measurement can see it.
    constexpr double kRingFs = 48000.0;
    constexpr double kRingSeconds = 3.0;

    SECTION("(a) MEASURED ring at 210 Hz, the unclamped centre") {
        // rt60ToQ(210, 1.0) = 95.50 < kMaxResonatorQ = 100, so this is the one
        // default centre at which the 1.0 s request is delivered whole.
        const RingArmRender render = renderRingImpulseTap(5, kRingFs, kRingSeconds);
        const RingMeasurement m = measureRingRt60(render.tap, kRingFs, render.centreHz);

        WARN("SC-023 (a) loop 5: centre "
             << render.centreHz << " Hz, SVF cutoff " << render.cutoffHz
             << " Hz, measured RT60 = " << m.rt60Seconds << " s (slope " << m.slopeDbPerSecond
             << " dB/s over " << m.pointsFitted << " one-cycle windows, initial peak "
             << m.initialPeak << " at " << m.initialPeakTime
             << " s); getLoopResonanceRt60(5) = " << render.filedRt60Seconds
             << " s; the kMaxQ = 30 defect would measure 0.3141 s");

        REQUIRE(render.centreHz == Approx(210.0).margin(1.0e-3));
        // The clamp made the SVF near-transparent at the centre rather than
        // leaving the 420 Hz default one octave above it.
        REQUIRE(render.cutoffHz == Approx(kRingFs * SVF::kMaxCutoffRatio).margin(1.0));
        REQUIRE(m.valid);
        REQUIRE(m.rt60Seconds == Approx(1.000).epsilon(0.10));

        // *** THE FLOOR THAT MAY NEVER BE WEAKENED. *** If the +/-10 % band above
        // ever proves too tight for the measurement method it may be re-pinned
        // from a RECORDED measurement under FR-080's stop-and-surface rule; this
        // line may not move, and the response to a genuine miss is to fix the
        // coefficient path.
        REQUIRE(m.rt60Seconds > 0.5);

        // SC-023 (c), second half: the bookkeeping field against the filter in
        // force. THIS is what ties getLoopResonanceRt60 to the coefficients.
        REQUIRE(m.rt60Seconds == Approx(render.filedRt60Seconds).epsilon(0.10));
    }

    SECTION("(b) MEASURED ring at 1200 Hz, a Q-clamped centre") {
        // rt60ToQ(1200, 1.0) = 545.70, clamped to kMaxResonatorQ = 100, realised
        // 100 * kLn1000 / (pi * 1200) = 0.1833 s. The getter reports the realised
        // figure, and the ring must match IT and not the 1.0 s request.
        const RingArmRender render = renderRingImpulseTap(0, kRingFs, kRingSeconds);
        const RingMeasurement m = measureRingRt60(render.tap, kRingFs, render.centreHz);

        WARN("SC-023 (b) loop 0: centre "
             << render.centreHz << " Hz, SVF cutoff " << render.cutoffHz
             << " Hz, measured RT60 = " << m.rt60Seconds << " s (slope " << m.slopeDbPerSecond
             << " dB/s over " << m.pointsFitted << " one-cycle windows, initial peak "
             << m.initialPeak << " at " << m.initialPeakTime
             << " s); getLoopResonanceRt60(0) = " << render.filedRt60Seconds
             << " s; the kMaxQ = 30 defect would measure 0.0550 s");

        REQUIRE(render.centreHz == Approx(1200.0).margin(1.0e-3));
        REQUIRE(render.cutoffHz == Approx(kRingFs * SVF::kMaxCutoffRatio).margin(1.0));
        REQUIRE(m.valid);
        REQUIRE(m.rt60Seconds == Approx(0.1833).epsilon(0.10));

        // SC-023 (c), second half, at the clamped centre.
        REQUIRE(m.rt60Seconds == Approx(render.filedRt60Seconds).epsilon(0.10));
    }

    SECTION("(c) the getter reproduces DERIVATION TABLE 2 on a prepared instance") {
        FeedbackEcology fe;
        fe.prepare(48000.0, FeedbackEcology::PrepareConfig{});

        static constexpr float kRealisedRt60[FeedbackEcology::kMaxLoops] = {
            0.1833f, 0.2587f, 0.3665f, 0.5174f, 0.7330f, 1.0000f,
        };
        for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
            INFO("loop " << i);
            REQUIRE(fe.getLoopResonanceRt60(i) == Approx(kRealisedRt60[i]).margin(1.0e-3));
        }

        // A request ABOVE what the centre can deliver is silently clamped by
        // rt60ToQ's kMaxResonatorQ = 100 ceiling, and the getter reports the
        // REALISED figure rather than echoing the request.
        //
        // rt60ToQ(210, 2.0) = pi * 210 * 2 / kLn1000 = 191.01, clamped to 100.
        //
        // NOTE ON tasks.md T007: that section states the getter "still reports
        // 1.0000 s" here. That is an arithmetic slip in the prose, not a
        // requirement. 1.0000 s at 210 Hz corresponds to Q = 95.503, whereas the
        // clamp lands on Q = 100, so the ceiling at 210 Hz is
        //     kMaxResonatorQ * kLn1000 / (pi * 210) = 690.7755 / 659.7345
        //                                           = 1.04705 s.
        // A getter that reported 1.0000 s would be re-clamping its answer back into
        // the request's range - exactly the lie S4.2 says it must not tell. The
        // assertion below is written from the derivation so it cannot drift.
        const float ceilingAt210 =
            (Krate::DSP::kMaxResonatorQ * Krate::DSP::kLn1000) / (Krate::DSP::kPi * 210.0f);
        REQUIRE(ceilingAt210 == Approx(1.04705f).margin(1.0e-4));

        fe.setLoopResonanceRt60(5, 2.0f);
        REQUIRE(fe.getLoopResonanceRt60(5) == Approx(ceilingAt210).margin(1.0e-4));

        // Below the ceiling the request round-trips exactly - the clamp is per
        // centre, not a blanket cap.
        fe.setLoopResonanceRt60(5, 0.5f);
        REQUIRE(fe.getLoopResonanceRt60(5) == Approx(0.5f).margin(1.0e-4));

        // The same request at a HIGHER centre is clamped harder, which is the whole
        // content of DERIVATION TABLE 2.
        fe.setLoopResonanceRt60(0, 2.0f);
        const float ceilingAt1200 =
            (Krate::DSP::kMaxResonatorQ * Krate::DSP::kLn1000) / (Krate::DSP::kPi * 1200.0f);
        REQUIRE(fe.getLoopResonanceRt60(0) == Approx(ceilingAt1200).margin(1.0e-4));
        REQUIRE(ceilingAt1200 == Approx(0.1832f).margin(1.0e-3));
    }
}

// ==============================================================================
// T008 - SC-008, the allocation footprint
// ==============================================================================
// The figures are plan S10's table, TRANSCRIBED rather than re-derived: a test
// that recomputed nextPowerOf2(size_t(fs * kMaxDelaySeconds) + 1) * 6 * 4 would
// be the implementation restated, and would agree with a wrong implementation.
//
// 44 100 and 48 000 COINCIDE - both land in the same 32 768-sample power-of-two
// bucket - so the case asserts the EQUALITY. Writing an inequality there would
// encode a bug as a requirement.
// ==============================================================================
TEST_CASE("FeedbackEcology_Footprint", "[feedback_ecology]") {
    SECTION("0 before prepare() (S9's exception to the defaults rule)") {
        FeedbackEcology fe;
        REQUIRE(fe.getAllocatedBytes() == std::size_t{0});
        REQUIRE_FALSE(fe.isPrepared());
    }

    SECTION("plan S10's table, exactly") {
        struct Row {
            double rate;
            std::size_t bytes;
        };
        static const Row kRows[] = {
            {.rate = 44100.0, .bytes = std::size_t{786432}},
            {.rate = 48000.0, .bytes = std::size_t{786432}},
            {.rate = 96000.0, .bytes = std::size_t{1572864}},
            {.rate = 192000.0, .bytes = std::size_t{3145728}},
        };

        for (const Row& row : kRows) {
            INFO("sample rate " << row.rate);
            FeedbackEcology fe;
            fe.prepare(row.rate, FeedbackEcology::PrepareConfig{});
            REQUIRE(fe.getAllocatedBytes() == row.bytes);
        }

        // The coincidence, asserted as an EQUALITY on purpose.
        FeedbackEcology at441;
        FeedbackEcology at48;
        at441.prepare(44100.0, FeedbackEcology::PrepareConfig{});
        at48.prepare(48000.0, FeedbackEcology::PrepareConfig{});
        REQUIRE(at441.getAllocatedBytes() == at48.getAllocatedBytes());
    }

    SECTION("independent of maxBlockSamples") {
        static constexpr std::size_t kBlocks[] = {64, 512, 8192};
        for (std::size_t blockSamples : kBlocks) {
            INFO("maxBlockSamples " << blockSamples);
            FeedbackEcology fe;
            fe.prepare(48000.0, FeedbackEcology::PrepareConfig{.maxBlockSamples = blockSamples});
            REQUIRE(fe.getAllocatedBytes() == std::size_t{786432});
            REQUIRE(fe.getMaxBlockSamples() == blockSamples);
        }
    }

    SECTION("independent of numLoops - the multiplier is kMaxLoops, not numLoops") {
        // All six lines are prepared regardless of the count, because
        // setNumLoops must not allocate (FR-075, FR-081). A numLoops-scaled
        // figure would under-report the real footprint by 6x at numLoops = 1.
        static constexpr std::size_t kCounts[] = {1, FeedbackEcology::kMaxLoops};
        for (std::size_t count : kCounts) {
            INFO("numLoops " << count);
            FeedbackEcology fe;
            fe.prepare(48000.0, FeedbackEcology::PrepareConfig{.numLoops = count});
            REQUIRE(fe.getAllocatedBytes() == std::size_t{786432});
            REQUIRE(fe.getNumLoops() == count);
        }

        // ... and the figure does not move when the count changes on a prepared
        // object, because setNumLoops does not allocate.
        FeedbackEcology fe;
        fe.prepare(48000.0, FeedbackEcology::PrepareConfig{});
        const std::size_t before = fe.getAllocatedBytes();
        fe.setNumLoops(1);
        REQUIRE(fe.getAllocatedBytes() == before);
    }
}

// ==============================================================================
// T008 - SC-011, the two LIFECYCLE arms
// ==============================================================================
// The RENDER arms (44.1 / 48 / 96 / 192 kHz rendered comparisons, and the
// staircase-aware realised-delay bound) are T015's and have since landed in the
// last two SECTIONs below.
// ==============================================================================
TEST_CASE("FeedbackEcology_SampleRate", "[feedback_ecology]") {
    SECTION("(lifecycle) degenerate rates leave a usable object (FR-083)") {
        // 1 Hz, 0 and a negative rate. THE NON-FINITE RATE IS NOT HERE: this TU
        // is compiled WITH fast-math and the phase's conventions confine every
        // non-finite value to feedback_ecology_nonfinite_test.cpp, where they
        // are built from bit patterns behind a volatile sink. See the SECTION
        // below.
        static constexpr double kDegenerate[] = {1.0, 0.0, -48000.0};

        for (double requested : kDegenerate) {
            INFO("requested rate " << requested);
            FeedbackEcology fe;
            fe.prepare(requested, FeedbackEcology::PrepareConfig{});

            REQUIRE(fe.isPrepared());
            REQUIRE(fe.getSampleRate() >= FeedbackEcology::kMinUsableSampleRate);

            // The clamp pairs must still be ORDERED at the floored rate - a
            // getter that clamps against a rate-derived bound is UB otherwise
            // (R-8), and MSVC's <algorithm> traps it with _STL_VERIFY.
            for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
                INFO("loop " << i);
                REQUIRE(Krate::DSP::detail::isFinite(fe.getLoopResonanceRt60(i)));
                REQUIRE(fe.getLoopResonanceRt60(i) > 0.0f);
            }

            // 512 samples of the reference drive: finite on both channels.
            constexpr std::size_t kN = 512;
            std::array<float, kN> inL{};
            std::array<float, kN> inR{};
            std::array<float, kN> outL{};
            std::array<float, kN> outR{};
            fillReferenceDrive(inL.data(), inR.data(), kN);

            fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kN);
            for (std::size_t s = 0; s < kN; ++s) {
                INFO("sample " << s);
                REQUIRE(Krate::DSP::detail::isFinite(outL[s]));
                REQUIRE(Krate::DSP::detail::isFinite(outR[s]));
            }
        }
    }

    SECTION("(lifecycle) a non-finite rate - owned by the non-finite TU") {
        // prepare() substitutes 48 000 for a non-finite request (plan S2 step 1)
        // and NOT 1 Hz, because [kMinResonatorFrequency, 0.45 * fs] inverts
        // below ~44 Hz. Proving it needs a NaN double, which may not be named in
        // this TU (see the banner). T008 records the gap here; the arm belongs
        // beside SC-012's injections in feedback_ecology_nonfinite_test.cpp.
        SUCCEED("SC-011 non-finite rate: deferred to feedback_ecology_nonfinite_test.cpp.");
    }

    SECTION("(lifecycle) re-prepare() restores the defaults (FR-005 (c))") {
        FeedbackEcology fe;
        fe.prepare(48000.0, FeedbackEcology::PrepareConfig{});

        // Drive the configuration as far from the defaults as the control
        // surface allows.
        for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                if (from == to) continue;
                fe.setCoupling(from, to, FeedbackEcology::kMaxCouplingPerPair);
            }
            fe.setLoopGain(from, FeedbackEcology::kMinLoopGain);
        }
        REQUIRE(fe.getCoupling(0, 1) == Approx(FeedbackEcology::kMaxCouplingPerPair));
        REQUIRE(fe.getCoupling(0, 2) == Approx(FeedbackEcology::kMaxCouplingPerPair));
        REQUIRE(fe.getLoopGain(3) == Approx(FeedbackEcology::kMinLoopGain));

        fe.prepare(48000.0, FeedbackEcology::PrepareConfig{});

        // FR-033's default ring: kDefaultCoupling on the two cyclic neighbours,
        // 0 everywhere else.
        constexpr std::size_t kN = FeedbackEcology::kMaxLoops;
        for (std::size_t from = 0; from < kN; ++from) {
            for (std::size_t to = 0; to < kN; ++to) {
                if (from == to) continue;
                const bool neighbour = (to == (from + 1) % kN) || (to == (from + kN - 1) % kN);
                const float expected = neighbour ? FeedbackEcology::kDefaultCoupling : 0.0f;
                INFO("coupling " << from << " -> " << to);
                REQUIRE(fe.getCoupling(from, to) == Approx(expected).margin(1.0e-6));
            }
            INFO("loop " << from);
            REQUIRE(fe.getLoopGain(from) == Approx(FeedbackEcology::kDefaultLoopGain));
        }
    }

    SECTION("(lifecycle) reset() PRESERVES the configuration (FR-004) - the complement") {
        FeedbackEcology fe;
        fe.prepare(48000.0, FeedbackEcology::PrepareConfig{});

        for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                if (from == to) continue;
                fe.setCoupling(from, to, FeedbackEcology::kMaxCouplingPerPair);
            }
            fe.setLoopGain(from, FeedbackEcology::kMinLoopGain);
        }

        fe.reset();

        for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                if (from == to) continue;
                INFO("coupling " << from << " -> " << to);
                REQUIRE(fe.getCoupling(from, to)
                        == Approx(FeedbackEcology::kMaxCouplingPerPair));
            }
            INFO("loop " << from);
            REQUIRE(fe.getLoopGain(from) == Approx(FeedbackEcology::kMinLoopGain));
        }

        // reset() rewinds state, not configuration: the rate and the footprint
        // survive it too.
        REQUIRE(fe.getSampleRate() == Approx(48000.0));
        REQUIRE(fe.getAllocatedBytes() == std::size_t{786432});
    }

    // T015 - the two RENDER arms.
    static constexpr double kRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
    constexpr std::size_t kNumRates = 4;

    SECTION("(render) RMS, centroid, and no clamp or non-finite engagement at any rate") {
        // 10 s rendered, the first 2 s discarded: the delay lines open empty and
        // the longest loop is 449 ms, so the opening is a fill transient whose
        // level is a property of the render's start, not of the rate.
        constexpr double kTotalSeconds = 10.0;
        constexpr double kMeasureFromSeconds = 2.0;

        std::array<RateRenderMetrics, kNumRates> metrics{};
        for (std::size_t r = 0; r < kNumRates; ++r) {
            metrics[r] = renderRateMetrics(kRates[r], kTotalSeconds, kMeasureFromSeconds);
            WARN("SC-011 " << kRates[r] << " Hz: RMS = " << metrics[r].rmsDbfs
                           << " dBFS, centroid = " << metrics[r].centroidHz
                           << " Hz, clamp engagements = " << metrics[r].clampEngagements
                           << ", non-finite resets = " << metrics[r].nonFiniteResets);
        }

        // FR-046 / FR-047: the reference patch holds wetGain at 0 dB, which is
        // the scope in which the zero-engagement claim is made.
        for (std::size_t r = 0; r < kNumRates; ++r) {
            INFO("rate " << kRates[r]);
            REQUIRE(metrics[r].clampEngagements == std::uint32_t{0});
            REQUIRE(metrics[r].nonFiniteResets == std::uint32_t{0});
        }

        double rmsLo = metrics[0].rmsDbfs;
        double rmsHi = metrics[0].rmsDbfs;
        double centroidLo = metrics[0].centroidHz;
        double centroidHi = metrics[0].centroidHz;
        for (std::size_t r = 1; r < kNumRates; ++r) {
            rmsLo = std::min(rmsLo, metrics[r].rmsDbfs);
            rmsHi = std::max(rmsHi, metrics[r].rmsDbfs);
            centroidLo = std::min(centroidLo, metrics[r].centroidHz);
            centroidHi = std::max(centroidHi, metrics[r].centroidHz);
        }

        WARN("SC-011 across rates: RMS spread = " << (rmsHi - rmsLo) << " dB against 1 dB, centroid "
                                                  << centroidLo << " .. " << centroidHi
                                                  << " Hz, ratio = " << (centroidHi / centroidLo)
                                                  << " against 1.10");

        // IF THIS FAILS, READ THE MECHANISM BEFORE TOUCHING THE BOUND. The
        // reference drive is defined PER SAMPLE, so its one-sided PSD is
        // sigma^2 / (fs/2) and the noise power the loops' own passbands see falls
        // as 1/fs: 44.1 -> 192 kHz is 6.4 dB of INPUT difference before the
        // component does anything. What is supposed to absorb it is FR-044's
        // governor at kDefaultGovernorRatio = 8 (6.4 / 8 = 0.8 dB out), so a
        // spread near 6 dB means the governor is not engaged on this patch and a
        // spread near 1 dB means it is - the number diagnoses the governor, and
        // the response to a miss is never to widen the band.
        REQUIRE(rmsHi - rmsLo <= 1.0);
        // A silent render would satisfy "within 10 %" trivially, so the band is
        // pinned open at the bottom before the ratio is taken.
        REQUIRE(centroidLo > 1.0);
        REQUIRE(centroidHi / centroidLo <= 1.10);
    }

    SECTION("(render) the staircase-aware realised delay bound, wander OFF") {
        // setWanderEnabled(false) IMMEDIATELY after prepare(), so every mapped
        // delay is still the base value snapToDelayMs() put in force (FR-005,
        // FR-056/Q2: off zeroes the DEPTH term, it does not rewind a lane).
        //
        // THE BOUND IS STAIRCASE-AWARE AND THAT IS NOT A LICENCE TO DRIFT.
        // CrossfadingDelayLine moves only the inactive tap until the target has
        // drifted kCrossfadeThresholdSamples = 100 from the active one
        // (crossfading_delay_line.h:161-191, :306), so the realised value may
        // legitimately lag by up to one threshold - 2.268 ms at 44.1 kHz down to
        // 0.521 ms at 192 kHz. With wander off and the FR-005 snap the measured
        // error is expected to be EXACTLY ZERO, and it is reported at every rate
        // and every loop so a build that starts using the tolerance is visible.
        constexpr double kSeconds = 5.0;

        for (std::size_t r = 0; r < kNumRates; ++r) {
            const double fs = kRates[r];
            const auto total = static_cast<std::size_t>(kSeconds * fs);

            FeedbackEcology fe;
            fe.setSeed(0x5EEDu);
            fe.prepare(fs, FeedbackEcology::PrepareConfig{.maxBlockSamples = 4096,
                                                          .numLoops = FeedbackEcology::kMaxLoops});
            fe.setWanderEnabled(false);
            fe.setMix(1.0f);
            fe.setWetGain(0.0f);
            fe.reset();

            REQUIRE_FALSE(fe.isWanderEnabled());

            std::vector<float> inL(total, 0.0f);
            std::vector<float> inR(total, 0.0f);
            std::vector<float> outL(total, 0.0f);
            std::vector<float> outR(total, 0.0f);
            fillReferenceDrive(inL.data(), inR.data(), total);
            renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), total,
                         std::size_t{512});

            const double staircaseMs =
                static_cast<double>(
                    Krate::DSP::CrossfadingDelayLine::kCrossfadeThresholdSamples)
                * 1000.0 / fs;

            double worstError = 0.0;
            for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
                const double configured =
                    static_cast<double>(FeedbackEcology::kDefaultLoopDelayMs[i]);
                const double realised = static_cast<double>(fe.getLoopCurrentDelayMs(i));
                worstError = std::max(worstError, std::abs(realised - configured));
            }
            WARN("SC-011 " << fs << " Hz: staircase term = " << staircaseMs
                           << " ms, worst |realised - configured| over the six loops = "
                           << worstError << " ms");

            for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
                const double configured =
                    static_cast<double>(FeedbackEcology::kDefaultLoopDelayMs[i]);
                const double realised = static_cast<double>(fe.getLoopCurrentDelayMs(i));
                const double bound = std::max(0.01 * configured, staircaseMs);
                INFO("rate " << fs << ", loop " << i << ": configured " << configured
                             << " ms, realised " << realised << " ms, bound " << bound << " ms");
                REQUIRE(std::abs(realised - configured) <= bound);
            }
        }
    }
}

// ==============================================================================
// T009 - the render path: FR-003's guard ladder, FR-007's ABSOLUTE control grid,
// renderChunk()'s six S6 steps and the output stage
// ==============================================================================
// Covers SC-024 (FeedbackEcology_EntryPointContract), SC-010
// (FeedbackEcology_BlockPartition), SC-016 (FeedbackEcology_TapEquivalence),
// SC-018 (FeedbackEcology_DryIdentity) and SC-013 (b)
// (FeedbackEcology_OutputClamp; T014 adds SC-013 (a)'s cross-arms).
//
// WHAT IS AND IS NOT LIVE UNDER THE HEADER AT THIS TASK. renderChunk reads
// appliedOwnFb_ and appliedCoupling_, and both are written by T011's
// normaliseRows() - so until T011/T012 land, every feedback and coupling
// coefficient is 0.0f and the six loops are a FEED-FORWARD bank
// (SVF -> delay -> resonator -> DC blocker -> tanh -> gate). Every criterion
// below discriminates the thing it names under BOTH the feed-forward and the
// fully-coupled network; where the coefficient state changes the NUMBERS rather
// than the claim, the case says so at the assertion.
//
// EXACT COMPARISONS. Four structural identities in this phase are exact by
// permission (tasks.md "Conventions"): SC-016's tapped/untapped bit-identity,
// SC-024 (b)/(c)/(d)'s same-code-path identities and SC-018 (a)'s mix == 0 dry
// pass-through. They go through floatBits() rather than `==`, because
// `-0.0f == +0.0f` is true and SC-018 (a) turns on exactly that pair. Long
// comparisons accumulate a mismatch COUNT and assert once: a per-sample REQUIRE
// over a 480 000-sample render is millions of Catch2 assertions.
//
// NO SECTIONs GUARD AN EXPENSIVE PROLOGUE. Catch2 re-runs a TEST_CASE body once
// per leaf SECTION, so a 60 s reference render placed above four SECTIONs would
// be rendered four times. The two long cases are therefore FLAT.
// ==============================================================================

namespace {

constexpr std::size_t kLoops = FeedbackEcology::kMaxLoops;

/// THE REFERENCE PATCH (tasks.md "The reference patch and the reference drive"):
/// numLoops = 6, all loops awake, the FR-013/FR-022/FR-033/FR-052/FR-053 default
/// tables, wander on at kDefaultWanderRateHz, the governor at its defaults,
/// mix = 1.0 (so a criterion measures the WET path and not the crossfade),
/// wetGain = 0 dB, a fixed seed.
///
/// It CONFIGURES IN PLACE instead of returning a FeedbackEcology by value. The
/// component owns six non-copyable CrossfadingDelayLines
/// (crossfading_delay_line.h:88-91), so a by-value return would rest on the
/// implicit move, whose BrownianDrift leg copy-constructs a polymorphic base
/// (ModulationSource declares a destructor, modulation_source.h:33) - a
/// -Wdeprecated-copy risk on the GCC leg for no gain. Callers write:
///     FeedbackEcology fe;  makeReference(fe);
///
/// THE ORDER IS LOAD-BEARING: setMix/setWetGain come AFTER prepare(), because
/// prepare() step 7's applyDefaults() restores kDefaultMix = 0.15f on EVERY call
/// (FR-005 (c)); reset() comes last because it SNAPS every ramp to the values
/// just set and re-derives all twelve lane streams from the seed, which is what
/// makes the patch exact from sample 0 (S3.2).
void makeReference(FeedbackEcology& fe, double fs = 48000.0, std::size_t loops = 6,
                   std::uint32_t seed = 0x5EEDu) {
    fe.setSeed(seed);
    fe.prepare(fs, FeedbackEcology::PrepareConfig{.maxBlockSamples = 4096, .numLoops = loops});
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);
    fe.reset();
}

/// The raw bits of a float. Used only for the four permitted structural
/// identities - never accumulated into a digest, which is what
/// tools/lint-float-bit-goldens.js forbids and rightly.
[[nodiscard]] std::uint32_t floatBits(float v) noexcept {
    std::uint32_t out = 0;
    std::memcpy(&out, &v, sizeof(out));
    return out;
}

/// The inverse. SC-018 (a)'s -0.0f fixture is BUILT FROM ITS BIT PATTERN rather
/// than written as the literal `-0.0f`: this TU compiles under /fp:fast and
/// -ffast-math (tasks.md T001), and -ffast-math implies -fno-signed-zeros, under
/// which a compiler is free to fold a negative-zero literal to +0.0f. The bit
/// pattern is not foldable, so the fixture reproduces the precondition on every
/// leg instead of only on Windows.
[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

/// Renders `total` samples through `fe` in fixed-size blocks (the tail block is
/// whatever is left).
void renderBlocks(FeedbackEcology& fe, const float* inL, const float* inR, float* outL,
                  float* outR, std::size_t total, std::size_t blockSize) {
    std::size_t done = 0;
    while (done < total) {
        const std::size_t n = std::min(blockSize, total - done);
        fe.processBlock(inL + done, inR + done, outL + done, outR + done, n);
        done += n;
    }
}

/// Counts the samples at which two buffers differ BIT FOR BIT.
[[nodiscard]] std::size_t countBitMismatches(const float* a, const float* b,
                                             std::size_t n) noexcept {
    std::size_t mismatches = 0;
    for (std::size_t s = 0; s < n; ++s) {
        if (floatBits(a[s]) != floatBits(b[s])) ++mismatches;
    }
    return mismatches;
}

}  // namespace

// ==============================================================================
// SC-024 - FR-003's entry-point contract: the guard ladder and both aliasings
// ==============================================================================
// No other criterion touches a null pointer, a zero length, an unprepared render
// or either aliasing. The ladder's three rungs are ORDERED and the order is
// itself under test: rung 1 (null) must fire before rung 3 (unprepared), or
// std::fill_n dereferences a null pointer on an unprepared instance.
// ==============================================================================
TEST_CASE("FeedbackEcology_EntryPointContract", "[feedback_ecology]") {
    constexpr float kSentinel = -7.5f;

    SECTION("(a) a null channel pointer writes nothing and advances nothing") {
        FeedbackEcology fe;
        makeReference(fe);

        // Settle first, so the recorded state is a real running state and not the
        // post-reset() snapshot every field would trivially still hold.
        constexpr std::size_t kWarm = 4096;
        std::vector<float> driveL(kWarm);
        std::vector<float> driveR(kWarm);
        std::vector<float> sinkL(kWarm, 0.0f);
        std::vector<float> sinkR(kWarm, 0.0f);
        fillReferenceDrive(driveL.data(), driveR.data(), kWarm);
        fe.processBlock(driveL.data(), driveR.data(), sinkL.data(), sinkR.data(), kWarm);

        std::array<float, kLoops> delayBefore{};
        std::array<float, kLoops> gateBefore{};
        std::array<std::uint32_t, kLoops> crossfadeBefore{};
        for (std::size_t i = 0; i < kLoops; ++i) {
            delayBefore[i] = fe.getLoopCurrentDelayMs(i);
            gateBefore[i] = fe.getLoopGate(i);
            crossfadeBefore[i] = fe.getLoopCrossfadeCount(i);
        }
        const float rmsBefore = fe.getGovernorRms();
        const std::uint32_t clampBefore = fe.getClampEngagementCount();
        const std::uint32_t nonFiniteBefore = fe.getNonFiniteResetCount();

        constexpr std::size_t kN = 256;
        std::vector<float> outL(kN, kSentinel);
        std::vector<float> outR(kN, kSentinel);

        // Five arms: each of the four pointers null in turn, then all four.
        for (int arm = 0; arm < 5; ++arm) {
            const float* aL = (arm == 0 || arm == 4) ? nullptr : driveL.data();
            const float* aR = (arm == 1 || arm == 4) ? nullptr : driveR.data();
            float* bL = (arm == 2 || arm == 4) ? nullptr : outL.data();
            float* bR = (arm == 3 || arm == 4) ? nullptr : outR.data();
            fe.processBlock(aL, aR, bL, bR, kN);
        }

        std::size_t written = 0;
        for (std::size_t s = 0; s < kN; ++s) {
            if (floatBits(outL[s]) != floatBits(kSentinel)) ++written;
            if (floatBits(outR[s]) != floatBits(kSentinel)) ++written;
        }
        REQUIRE(written == std::size_t{0});

        for (std::size_t i = 0; i < kLoops; ++i) {
            INFO("loop " << i);
            REQUIRE(floatBits(fe.getLoopCurrentDelayMs(i)) == floatBits(delayBefore[i]));
            REQUIRE(floatBits(fe.getLoopGate(i)) == floatBits(gateBefore[i]));
            REQUIRE(fe.getLoopCrossfadeCount(i) == crossfadeBefore[i]);
        }
        REQUIRE(floatBits(fe.getGovernorRms()) == floatBits(rmsBefore));
        REQUIRE(fe.getClampEngagementCount() == clampBefore);
        REQUIRE(fe.getNonFiniteResetCount() == nonFiniteBefore);
    }

    SECTION("(b) numSamples == 0 consumes no control step") {
        // THE ONLY WAY TO OBSERVE controlPhase_. A hundred zero-length calls
        // between two 512-sample halves must leave the concatenation identical to
        // one unbroken 1024-sample render; a guard placed AFTER the chunk loop
        // had run a control step would diverge across the second half.
        constexpr std::size_t kHalf = 512;
        constexpr std::size_t kTotal = 2 * kHalf;

        std::vector<float> inL(kTotal);
        std::vector<float> inR(kTotal);
        fillReferenceDrive(inL.data(), inR.data(), kTotal);

        FeedbackEcology split;
        FeedbackEcology whole;
        makeReference(split);
        makeReference(whole);

        std::vector<float> sL(kTotal, 0.0f);
        std::vector<float> sR(kTotal, 0.0f);
        std::vector<float> wL(kTotal, 0.0f);
        std::vector<float> wR(kTotal, 0.0f);

        split.processBlock(inL.data(), inR.data(), sL.data(), sR.data(), kHalf);
        for (int k = 0; k < 100; ++k) {
            split.processBlock(inL.data() + kHalf, inR.data() + kHalf, sL.data() + kHalf,
                               sR.data() + kHalf, 0);
        }
        split.processBlock(inL.data() + kHalf, inR.data() + kHalf, sL.data() + kHalf,
                           sR.data() + kHalf, kHalf);

        whole.processBlock(inL.data(), inR.data(), wL.data(), wR.data(), kTotal);

        REQUIRE(countBitMismatches(sL.data(), wL.data(), kTotal) == std::size_t{0});
        REQUIRE(countBitMismatches(sR.data(), wR.data(), kTotal) == std::size_t{0});
    }

    SECTION("(c) an unprepared instance writes exactly numSamples zeros") {
        FeedbackEcology fe;  // default-constructed: prepare() never called
        REQUIRE_FALSE(fe.isPrepared());

        constexpr std::size_t kN = 333;
        constexpr std::size_t kBuf = 512;
        std::vector<float> inL(kBuf, 0.25f);
        std::vector<float> inR(kBuf, -0.25f);
        std::vector<float> outL(kBuf, kSentinel);
        std::vector<float> outR(kBuf, kSentinel);

        std::array<float, kLoops> delayBefore{};
        std::array<float, kLoops> cutoffBefore{};
        std::array<float, kLoops> gateBefore{};
        std::array<std::uint32_t, kLoops> crossfadeBefore{};
        for (std::size_t i = 0; i < kLoops; ++i) {
            delayBefore[i] = fe.getLoopCurrentDelayMs(i);
            cutoffBefore[i] = fe.getLoopCurrentCutoffHz(i);
            gateBefore[i] = fe.getLoopGate(i);
            crossfadeBefore[i] = fe.getLoopCrossfadeCount(i);
        }
        const float rmsBefore = fe.getGovernorRms();

        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kN);

        std::size_t wrongZeros = 0;
        for (std::size_t s = 0; s < kN; ++s) {
            // std::fill_n writes +0.0f, so the bit pattern is exactly 0.
            if (floatBits(outL[s]) != std::uint32_t{0}) ++wrongZeros;
            if (floatBits(outR[s]) != std::uint32_t{0}) ++wrongZeros;
        }
        REQUIRE(wrongZeros == std::size_t{0});

        std::size_t overrun = 0;
        for (std::size_t s = kN; s < kBuf; ++s) {
            if (floatBits(outL[s]) != floatBits(kSentinel)) ++overrun;
            if (floatBits(outR[s]) != floatBits(kSentinel)) ++overrun;
        }
        REQUIRE(overrun == std::size_t{0});

        for (std::size_t i = 0; i < kLoops; ++i) {
            INFO("loop " << i);
            REQUIRE(floatBits(fe.getLoopCurrentDelayMs(i)) == floatBits(delayBefore[i]));
            REQUIRE(floatBits(fe.getLoopCurrentCutoffHz(i)) == floatBits(cutoffBefore[i]));
            REQUIRE(floatBits(fe.getLoopGate(i)) == floatBits(gateBefore[i]));
            REQUIRE(fe.getLoopCrossfadeCount(i) == crossfadeBefore[i]);
        }
        REQUIRE(floatBits(fe.getGovernorRms()) == floatBits(rmsBefore));
        REQUIRE(fe.getClampEngagementCount() == std::uint32_t{0});
        REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
    }

    SECTION("(d) both legal aliasings reproduce the non-aliased render exactly") {
        // mix = 0.5f DELIBERATELY, and NOT the reference patch's 1.0. At mix = 1
        // S6 step 5 does not read the dry at all, so an in-place-overwrite defect
        // - the ONE thing this arm exists to catch - would be structurally
        // invisible. At 0.5 the dry term is live and the two output channels
        // differ, so a crossed aliasing that clobbered an input before it was
        // read shows up on the first sample.
        constexpr std::size_t kN = 10 * 48000;

        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillReferenceDrive(inL.data(), inR.data(), kN);

        const auto configure = [](FeedbackEcology& fe) {
            makeReference(fe);
            fe.setMix(0.5f);
            fe.reset();
        };

        FeedbackEcology ref;
        configure(ref);
        std::vector<float> refL(kN, 0.0f);
        std::vector<float> refR(kN, 0.0f);
        ref.processBlock(inL.data(), inR.data(), refL.data(), refR.data(), kN);

        // Straight aliasing: inL == outL, inR == outR.
        FeedbackEcology direct;
        configure(direct);
        std::vector<float> aL = inL;
        std::vector<float> aR = inR;
        direct.processBlock(aL.data(), aR.data(), aL.data(), aR.data(), kN);
        REQUIRE(countBitMismatches(aL.data(), refL.data(), kN) == std::size_t{0});
        REQUIRE(countBitMismatches(aR.data(), refR.data(), kN) == std::size_t{0});

        // Crossed aliasing: inL == outR, inR == outL. After the call bufB holds
        // the LEFT output and bufA the RIGHT one.
        FeedbackEcology crossed;
        configure(crossed);
        std::vector<float> bufA = inL;
        std::vector<float> bufB = inR;
        crossed.processBlock(bufA.data(), bufB.data(), bufB.data(), bufA.data(), kN);
        REQUIRE(countBitMismatches(bufB.data(), refL.data(), kN) == std::size_t{0});
        REQUIRE(countBitMismatches(bufA.data(), refR.data(), kN) == std::size_t{0});
    }
}

// ==============================================================================
// SC-010 - block-partition invariance (FR-007's absolute control grid)
// ==============================================================================
// THE DEFECT THIS CATCHES: a BLOCK-RELATIVE control grid. Such a build runs two
// control steps for a 36 + 28 split where an unsplit 64 runs one, so its ramps
// and its lane advance become a function of the host's buffer size. The residue
// carried ACROSS calls is the only shape that survives all four partitions here.
// ==============================================================================
TEST_CASE("FeedbackEcology_BlockPartition", "[feedback_ecology]") {
    using Krate::DSP::TestUtils::compareFingerprints;
    using Krate::DSP::TestUtils::fingerprintRender;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kN = 60 * 48000;  // 60 s

    std::vector<float> inL(kN);
    std::vector<float> inR(kN);
    fillReferenceDrive(inL.data(), inR.data(), kN);

    std::vector<float> outL(kN, 0.0f);
    std::vector<float> outR(kN, 0.0f);

    // The 512-sample partition is the reference the others are compared against;
    // 512 is a whole number of 64-sample control chunks, i.e. the partition a
    // block-relative build would also get right.
    FeedbackEcology ref;
    makeReference(ref, kFs);
    renderBlocks(ref, inL.data(), inR.data(), outL.data(), outR.data(), kN, 512);
    const auto refFpL = fingerprintRender(outL);
    const auto refFpR = fingerprintRender(outR);

    const auto compareAgainstReference = [&](const char* label) {
        const auto cmpL = compareFingerprints(fingerprintRender(outL), refFpL);
        const auto cmpR = compareFingerprints(fingerprintRender(outR), refFpR);
        INFO(label << " left: " << cmpL.detail
                   << " worstSample=" << cmpL.worstSampleError);
        REQUIRE(cmpL.withinTolerance());
        INFO(label << " right: " << cmpR.detail
                   << " worstSample=" << cmpR.worstSampleError);
        REQUIRE(cmpR.withinTolerance());
    };

    // --- the three fixed partitions --------------------------------------------
    for (const std::size_t blockSize : {std::size_t{64}, std::size_t{7}, std::size_t{4096}}) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        FeedbackEcology fe;
        makeReference(fe, kFs);
        renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), kN, blockSize);
        INFO("block size " << blockSize);
        compareAgainstReference("fixed partition");
    }

    // --- an irregular pseudo-random partition -----------------------------------
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        FeedbackEcology fe;
        makeReference(fe, kFs);
        Krate::DSP::Xorshift32 rng(0xB10C51EEu);
        std::size_t done = 0;
        while (done < kN) {
            const std::size_t want = std::size_t{1} + static_cast<std::size_t>(rng.next() % 977u);
            const std::size_t n = std::min(want, kN - done);
            fe.processBlock(inL.data() + done, inR.data() + done, outL.data() + done,
                            outR.data() + done, n);
            done += n;
        }
        compareAgainstReference("irregular partition");
    }
}

// ==============================================================================
// SC-016 - the tap path changes nothing, and reconstructs the wet
// ==============================================================================
// processBlock FORWARDS to processBlockTapped with loopTaps = nullptr, so the two
// share ONE function body and the bit-identity below is structural. The second
// assertion is the stronger one: it reproduces S6 step 5's block IN ITS NORMATIVE
// ORDER from the taps alone, which pins FR-017's 1/sqrt(numLoops) divisor,
// FR-072's trim and FR-046's clamp as the output stage's real arithmetic rather
// than as a description of it.
// ==============================================================================
TEST_CASE("FeedbackEcology_TapEquivalence", "[feedback_ecology]") {
    constexpr std::size_t kN = 60 * 48000;  // 60 s
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kNumLoops = 6;

    std::vector<float> inL(kN);
    std::vector<float> inR(kN);
    fillReferenceDrive(inL.data(), inR.data(), kN);

    FeedbackEcology plain;
    FeedbackEcology tapped;
    makeReference(plain);
    makeReference(tapped);

    // Both ramps are SNAPPED by makeReference's reset(), so the two output-stage
    // constants are exact for the whole render.
    const float wetTrim = Krate::DSP::dbToGain(0.0f);
    const float normGain = 1.0f / std::sqrt(static_cast<float>(kNumLoops));

    std::vector<float> plainL(kBlock, 0.0f);
    std::vector<float> plainR(kBlock, 0.0f);
    std::vector<float> tapL(kBlock, 0.0f);
    std::vector<float> tapR(kBlock, 0.0f);

    std::array<std::vector<float>, kLoops> tapStore{};
    std::array<float*, kLoops> taps{};
    for (std::size_t i = 0; i < kLoops; ++i) {
        tapStore[i].assign(kBlock, 0.0f);
        taps[i] = tapStore[i].data();
    }

    std::size_t mismatches = 0;
    float worstReconstruction = 0.0f;
    std::size_t done = 0;
    while (done < kN) {
        const std::size_t n = std::min(kBlock, kN - done);
        plain.processBlock(inL.data() + done, inR.data() + done, plainL.data(), plainR.data(), n);
        tapped.processBlockTapped(inL.data() + done, inR.data() + done, tapL.data(), tapR.data(),
                                  taps.data(), n);

        mismatches += countBitMismatches(plainL.data(), tapL.data(), n);
        mismatches += countBitMismatches(plainR.data(), tapR.data(), n);

        for (std::size_t s = 0; s < n; ++s) {
            float sum = 0.0f;
            for (std::size_t i = 0; i < kLoops; ++i) {
                sum += tapStore[i][s];
            }
            // S6 step 5's block, IN THAT ORDER: trim the normalised sum, then
            // clamp. At mix = 1 the output channel IS the wet.
            float wet = wetTrim * (sum * normGain);
            wet = std::clamp(wet, -FeedbackEcology::kOutputClamp, FeedbackEcology::kOutputClamp);
            worstReconstruction = std::max(worstReconstruction, std::abs(wet - tapL[s]));
        }
        done += n;
    }

    REQUIRE(mismatches == std::size_t{0});
    INFO("worst reconstruction error = " << worstReconstruction);
    REQUIRE(worstReconstruction <= Krate::DSP::TestUtils::kSampleTolerance);
}

// ==============================================================================
// SC-018 - mix = 0 is BIT-EXACT dry, and the network is running underneath
// ==============================================================================
TEST_CASE("FeedbackEcology_DryIdentity", "[feedback_ecology]") {
    constexpr std::size_t kSeg = 10 * 48000;  // 10 s
    constexpr std::uint32_t kNegativeZeroBits = 0x80000000u;

    SECTION("(a) mix = 0 passes the dry through bit for bit, -0.0f included") {
        std::vector<float> inL(kSeg);
        std::vector<float> inR(kSeg);
        fillReferenceDrive(inL.data(), inR.data(), kSeg);

        // THE -0.0f SAMPLE IS THE WHOLE POINT of S6 step 5's explicit m == 0.0f
        // branch: (1-0)*dry + 0*wet is bit-exact for every finite dry EXCEPT
        // -0.0f, where -0.0f + 0.0f is +0.0f. Placed on both channels, off a
        // block boundary so no partition can hide it.
        constexpr std::size_t kNegZeroIndex = 1234;
        inL[kNegZeroIndex] = floatFromBits(kNegativeZeroBits);
        inR[kNegZeroIndex] = floatFromBits(kNegativeZeroBits);
        REQUIRE(floatBits(inL[kNegZeroIndex]) == kNegativeZeroBits);

        const std::vector<float> refL = inL;
        const std::vector<float> refR = inR;

        FeedbackEcology fe;
        makeReference(fe);
        fe.setMix(0.0f);
        fe.reset();  // SNAPS mixRamp_ to 0: "settled", not "gliding toward 0"
        REQUIRE(fe.getMix() == 0.0f);

        std::vector<float> outL(kSeg, 0.0f);
        std::vector<float> outR(kSeg, 0.0f);
        renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), kSeg, 512);

        REQUIRE(countBitMismatches(outL.data(), refL.data(), kSeg) == std::size_t{0});
        REQUIRE(countBitMismatches(outR.data(), refR.data(), kSeg) == std::size_t{0});
        REQUIRE(floatBits(outL[kNegZeroIndex]) == kNegativeZeroBits);
        REQUIRE(floatBits(outR[kNegZeroIndex]) == kNegativeZeroBits);
    }

    SECTION("(b1) the realised-state getters move while mix = 0 - T013 owns it") {
        // SC-018 (b)'s FIRST half needs three things this task does not ship: the
        // decimated lane advance (T010, without which getLoopCurrentDelayMs is
        // frozen at the base delay because no lane ever moves), the governor
        // (T012, without which getGovernorRms is a documented stub) and the gate
        // (T013, likewise).
        //
        // RECORDED DEFECT for the compliance pass. As written, the clause
        // "getLoopGate(i) ... change[s]" is UNSATISFIABLE on the reference patch
        // at ANY task: gateSteady(i) is 1.0f for every awake in-count loop and
        // prepare() step 12 / reset() step 4 SNAP the gate ramp to it, so a
        // steady awake render never moves a gate - only a life-cycle setter does,
        // which is SC-014's territory. T013 should assert that getLoopGate(i)
        // reports the live 1.0f rather than that it changes.
        SUCCEED("SC-018 (b) first half: needs T010's lane advance, T012's governor and "
                "T013's gate; T013 owns it. See the gate-clause defect note above.");
    }

    SECTION("(b2) the loops were charged, not asleep: mix 0 -> 1 matches a mix = 1 reference") {
        using Krate::DSP::TestUtils::compareFingerprints;
        using Krate::DSP::TestUtils::fingerprintRender;

        constexpr std::size_t kTotal = 2 * kSeg;
        std::vector<float> inL(kTotal);
        std::vector<float> inR(kTotal);
        fillReferenceDrive(inL.data(), inR.data(), kTotal);

        FeedbackEcology dry;
        makeReference(dry);
        dry.setMix(0.0f);
        dry.reset();

        FeedbackEcology wetRef;  // mix = 1 for the whole render
        makeReference(wetRef);

        std::vector<float> dryOutL(kTotal, 0.0f);
        std::vector<float> dryOutR(kTotal, 0.0f);
        std::vector<float> refOutL(kTotal, 0.0f);
        std::vector<float> refOutR(kTotal, 0.0f);

        renderBlocks(dry, inL.data(), inR.data(), dryOutL.data(), dryOutR.data(), kSeg, 512);
        renderBlocks(wetRef, inL.data(), inR.data(), refOutL.data(), refOutR.data(), kSeg, 512);

        dry.setMix(1.0f);
        renderBlocks(dry, inL.data() + kSeg, inR.data() + kSeg, dryOutL.data() + kSeg,
                     dryOutR.data() + kSeg, kSeg, 512);
        renderBlocks(wetRef, inL.data() + kSeg, inR.data() + kSeg, refOutL.data() + kSeg,
                     refOutR.data() + kSeg, kSeg, 512);

        // Skip 50 ms: kMixRampMs is 20 ms, so the crossfade is long settled.
        const std::size_t settle = static_cast<std::size_t>(0.05 * 48000.0);
        const std::size_t tailStart = kSeg + settle;
        const std::size_t tailLength = kTotal - tailStart;

        const auto dryTailL =
            fingerprintRender(std::span<const float>(dryOutL.data() + tailStart, tailLength));
        const auto refTailL =
            fingerprintRender(std::span<const float>(refOutL.data() + tailStart, tailLength));
        const auto cmpL = compareFingerprints(dryTailL, refTailL);
        INFO("left: " << cmpL.detail);
        REQUIRE(cmpL.withinTolerance());

        const auto dryTailR =
            fingerprintRender(std::span<const float>(dryOutR.data() + tailStart, tailLength));
        const auto refTailR =
            fingerprintRender(std::span<const float>(refOutR.data() + tailStart, tailLength));
        const auto cmpR = compareFingerprints(dryTailR, refTailR);
        INFO("right: " << cmpR.detail);
        REQUIRE(cmpR.withinTolerance());

        // ...and the wet is not silence, which is the actual "charged" claim: a
        // component that had been asleep at mix = 0 would match a reference that
        // was ALSO silent, and the two comparisons above would pass on nothing.
        INFO("wet rms after the crossfade = " << dryTailL.rms);
        REQUIRE(dryTailL.rms > 0.0);
    }
}

// ==============================================================================
// SC-013 - the output clamp never engages in-spec, and is proven WIRED
// ==============================================================================
// (b) IS WRITTEN FIRST BECAUSE (a) IS WORTHLESS UNTIL (b) PROVES THE COUNTER IS
// WIRED: a getter hard-wired to 0 passes (a) perfectly.
// ==============================================================================
TEST_CASE("FeedbackEcology_OutputClamp", "[feedback_ecology]") {
    SECTION("(b) the counter is wired: a hot drive at +24 dB trim engages the clamp") {
        // THE DRIVE, and why it is not the spec's bare 0 dBFS RMS.
        //
        // Reaching kOutputClamp = 4.0 through a +24 dB (x15.8489) trim needs a
        // NORMALISED wet sum of 4.0 / 15.8489 = 0.2524, i.e. a raw tap sum of
        // 0.2524 x sqrt(6) = 0.618 across the six loops. What limits that on the
        // reference patch is how little of a BROADBAND drive survives six NARROW
        // resonators: Q = 100 on loops 0-4 and 95.5 on loop 5 give equivalent
        // noise bandwidths of only 3.5-18.9 Hz out of 24 kHz each, so a
        // white-noise drive of RMS r produces a tap sum of roughly
        //     r x sqrt(0.94 x 56.5 Hz / 24000 Hz) = 0.047 r     (RMS)
        // and a peak near 5 sigma. The reference drive is -12 dBFS RMS per
        // channel (kReferenceDriveAmplitude, above), whose mono sum is
        // 0.5 x sqrt(2) x 0.2512 = 0.178 RMS - so the unscaled pre-clamp peak is
        // about 0.24 and even the spec's 0 dBFS RMS (x4) reaches only ~0.97.
        //
        // tasks.md names the remedy explicitly: "a louder drive or a longer
        // render - NEVER a lower kOutputClamp". kClampProbeDriveGain is that
        // louder drive.
        //
        // WHY x1000 AND NOT THE x100 THIS ARM CARRIED BEFORE T012. The taps this
        // arm sums are POST-governor: out_i = tanh(b_i * govGain) * gate_i. Once
        // T012 wired the governor and the SC-006 measurement re-based
        // kDefaultGovernorThresholdDb to -52 dB (DERIVATION TABLE 3), a drive
        // this hot pins the governor at its kGovernorMinGain = 0.05 floor, which
        // divides the argument of every tanh by 20: measured at x100 the
        // pre-clamp peak was 2.776, below the 4.0 ceiling, and the arm failed on
        // its own diagnostic assertion rather than on the counter.
        // At the floor the network is linear in the drive again (y_i grows with
        // b_i until tanh saturates), so the fix is purely more drive: x1000
        // leaves every loop deep in saturation and the pre-clamp peak several
        // times over the ceiling. The WARN below reports the realised margin, so
        // it is a measured number and not a claim.
        constexpr float kClampProbeDriveGain = 1000.0f;
        constexpr std::size_t kN = 10 * 48000;  // 10 s
        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kNumLoops = 6;

        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillReferenceDrive(inL.data(), inR.data(), kN);
        for (std::size_t s = 0; s < kN; ++s) {
            inL[s] *= kClampProbeDriveGain;
            inR[s] *= kClampProbeDriveGain;
        }

        FeedbackEcology fe;
        makeReference(fe);
        fe.setWetGain(24.0f);
        fe.reset();  // snaps wetGainRamp_ to x15.85 from sample 0
        REQUIRE(fe.getWetGain() == Approx(24.0f));

        const float wetTrim = Krate::DSP::dbToGain(24.0f);
        const float normGain = 1.0f / std::sqrt(static_cast<float>(kNumLoops));

        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);
        std::array<std::vector<float>, kLoops> tapStore{};
        std::array<float*, kLoops> taps{};
        for (std::size_t i = 0; i < kLoops; ++i) {
            tapStore[i].assign(kBlock, 0.0f);
            taps[i] = tapStore[i].data();
        }

        float preClampPeak = 0.0f;
        float outputPeak = 0.0f;
        std::size_t outOfRange = 0;
        std::size_t nonFinite = 0;
        std::size_t done = 0;
        while (done < kN) {
            const std::size_t n = std::min(kBlock, kN - done);
            fe.processBlockTapped(inL.data() + done, inR.data() + done, outL.data(), outR.data(),
                                  taps.data(), n);
            for (std::size_t s = 0; s < n; ++s) {
                float sum = 0.0f;
                for (std::size_t i = 0; i < kLoops; ++i) {
                    sum += tapStore[i][s];
                }
                preClampPeak = std::max(preClampPeak, std::abs(wetTrim * (sum * normGain)));

                const float sampleL = outL[s];
                const float sampleR = outR[s];
                if (!Krate::DSP::detail::isFinite(sampleL)
                    || !Krate::DSP::detail::isFinite(sampleR)) {
                    ++nonFinite;
                    continue;
                }
                outputPeak = std::max(outputPeak, std::max(std::abs(sampleL), std::abs(sampleR)));
                if (std::abs(sampleL) > FeedbackEcology::kOutputClamp
                    || std::abs(sampleR) > FeedbackEcology::kOutputClamp) {
                    ++outOfRange;
                }
            }
            done += n;
        }

        WARN("SC-013 (b) measured pre-clamp peak = "
             << preClampPeak << " (kOutputClamp = " << FeedbackEcology::kOutputClamp
             << "), output peak = " << outputPeak
             << ", clamp engagements = " << fe.getClampEngagementCount());

        // The diagnostic assertion FIRST: if the wet path never reached the
        // ceiling, the engagement count below would tell you nothing.
        REQUIRE(preClampPeak > FeedbackEcology::kOutputClamp);
        REQUIRE(fe.getClampEngagementCount() > std::uint32_t{0});
        REQUIRE(nonFinite == std::size_t{0});
        REQUIRE(outOfRange == std::size_t{0});
        REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
    }

    SECTION("(a) at wetGain <= 0 dB the clamp never engages - 60 s reference patch") {
        // STRUCTURAL, not a limiter: rung 2's per-loop tanh caps every tap at 1,
        // so the wet sum cannot exceed kMaxLoops, and rung 1's 1/sqrt(numLoops)
        // divisor caps the normalised sum at sqrt(6) = 2.4495 against a ceiling
        // of 4.0. This arm is therefore a DEFECT DETECTOR. T014 adds the
        // cross-arms over SC-002 / SC-003 / SC-006 / SC-011.
        constexpr std::size_t kN = 60 * 48000;

        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillReferenceDrive(inL.data(), inR.data(), kN);

        FeedbackEcology fe;
        makeReference(fe);  // wetGain = 0 dB
        std::vector<float> outL(kN, 0.0f);
        std::vector<float> outR(kN, 0.0f);
        renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), kN, 512);

        REQUIRE(fe.getClampEngagementCount() == std::uint32_t{0});
        REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
    }

    SECTION("(a) cross-arms - SC-002, SC-003, SC-006 and SC-011 all hold wetGain <= 0 dB") {
        // WHY THE FOUR CONFIGURATIONS ARE RE-RENDERED HERE (T014). SC-013 (a)
        // is worded over "every arm of SC-001, SC-002, SC-003, SC-006 and
        // SC-011", but three of those criteria do not live where the counter
        // can be read from this case: SC-002 (FeedbackEcology_CrossLoopTransfer)
        // and SC-003 (FeedbackEcology_DelayDriftClickFree) are [long] cases in
        // feedback_ecology_spectral_test.cpp, which the per-push CI filter
        // ~[long] excludes, and SC-013 is a per-push criterion. Each block below
        // is therefore ONE REPRESENTATIVE ARM of its criterion - the criterion's
        // own configuration, at the shortest render that still exercises the
        // mechanism it is about - which is exactly what tasks.md T014 asks for.
        // SC-001's arms stay with SC-001 (the [long] soak owns them).
        //
        // WHAT EACH ARM IS WORTH. With wetGain <= 0 dB the bound is STRUCTURAL:
        // FR-042's per-loop tanh caps every tap at 1, so the wet sum cannot
        // exceed kMaxLoops, and FR-017's 1/sqrt(numLoops) divisor caps the
        // normalised sum at sqrt(6) = 2.4495 against kOutputClamp = 4.0. These
        // are DEFECT DETECTORS for a build that loses one of those two rungs in
        // a configuration the plain reference patch does not reach - a single
        // driven loop with heavy coupling, a delay staircase firing at its
        // maximum rate, a 0 dBFS drive against the governor, a 192 kHz rate -
        // and they assert nothing at all until SC-013 (b) above proves the
        // counter increments. Each arm also asserts a NON-ZERO output peak, so
        // an arm that rendered silence cannot pass on an absence.
        constexpr std::size_t kBlock = 512;
        /// -12 dBFS as a LINEAR RMS level: 10^(-12/20).
        constexpr float kMinus12dBFSRms = 0.251189f;
        constexpr std::size_t kOneSecondAt48k = 48000u;
        constexpr std::size_t kTenSecondsAt48k = 10u * kOneSecondAt48k;

        std::array<float, kBlock> inL{};
        std::array<float, kBlock> inR{};
        std::array<float, kBlock> outL{};
        std::array<float, kBlock> outR{};

        // Streams `total` samples of uniform noise at `rmsLevel` (a LINEAR RMS
        // level, never a peak - the two differ by 10-12 dB for noise and every
        // governor-facing figure turns on which is meant) and returns the output
        // peak. Xorshift32::nextFloat() is uniform on [-1, +1], whose RMS is
        // 1/sqrt(3), so the amplitude that lands the RMS on rmsLevel is
        // rmsLevel * sqrt(3). Block by block from a stateful generator, so no
        // arm materialises its input.
        auto renderNoise = [&](FeedbackEcology& fe, Krate::DSP::Xorshift32& rng, float rmsLevel,
                               std::size_t total) {
            const float amplitude = rmsLevel * std::sqrt(3.0f);
            float peak = 0.0f;
            std::size_t done = 0;
            while (done < total) {
                const std::size_t n = std::min(kBlock, total - done);
                for (std::size_t s = 0; s < n; ++s) {
                    inL[s] = rng.nextFloat() * amplitude;
                    inR[s] = rng.nextFloat() * amplitude;
                }
                fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
                for (std::size_t s = 0; s < n; ++s) {
                    peak = std::max(peak, std::max(std::abs(outL[s]), std::abs(outR[s])));
                }
                done += n;
            }
            return peak;
        };

        // ---- SC-002's arm: single-loop excitation at the sweep's top coupling
        // point. Loop 0 driven, loops 1-5 fed only through the coupling matrix,
        // every off-diagonal pair at c = 0.20 - the hottest point of SC-002's
        // sweep, and the one where regeneration through the ring is largest.
        // configure -> reset() -> render, the phase's mandatory fixture rule.
        {
            FeedbackEcology fe;
            makeReference(fe);
            fe.setLoopInputGain(0, 1.0f);
            for (std::size_t i = 1; i < kLoops; ++i) {
                fe.setLoopInputGain(i, 0.0f);
            }
            for (std::size_t from = 0; from < kLoops; ++from) {
                for (std::size_t to = 0; to < kLoops; ++to) {
                    if (from == to) continue;
                    fe.setCoupling(from, to, 0.20f);
                }
            }
            fe.reset();

            Krate::DSP::Xorshift32 rng(kReferenceDriveSeed);
            const float peak = renderNoise(fe, rng, kMinus12dBFSRms, kTenSecondsAt48k);

            INFO("SC-002 arm (loop 0 driven, c = 0.20, 10 s): output peak "
                 << peak << ", clamps " << fe.getClampEngagementCount());
            REQUIRE(peak > 0.0f);
            REQUIRE(peak <= FeedbackEcology::kOutputClamp);
            REQUIRE(fe.getClampEngagementCount() == std::uint32_t{0});
            REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
        }

        // ---- SC-003's arm: the delay staircase firing as fast as the component
        // can make it fire - delayWanderFraction at kMaxDelayWanderFraction on
        // every loop and the wander rate at kMaxWanderRateHz - driven by the
        // criterion's own 110 Hz sine at -12 dBFS. The crossfade count is read
        // back so the arm cannot pass on a build whose delay never moved.
        {
            FeedbackEcology fe;
            makeReference(fe);
            for (std::size_t i = 0; i < kLoops; ++i) {
                fe.setLoopDelayWander(i, FeedbackEcology::kMaxDelayWanderFraction);
            }
            fe.setWanderRate(FeedbackEcology::kMaxWanderRateHz);
            fe.reset();

            constexpr double kFs = 48000.0;
            constexpr std::size_t kTotal = kTenSecondsAt48k;
            constexpr double kSineAmplitude = 0.251189;  // -12 dBFS, peak
            constexpr double kTwoPiD = 2.0 * static_cast<double>(Krate::DSP::kPi);
            const double phaseInc = kTwoPiD * 110.0 / kFs;

            double phase = 0.0;
            float peak = 0.0f;
            std::size_t done = 0;
            while (done < kTotal) {
                const std::size_t n = std::min(kBlock, kTotal - done);
                for (std::size_t s = 0; s < n; ++s) {
                    const auto v = static_cast<float>(kSineAmplitude * std::sin(phase));
                    inL[s] = v;
                    inR[s] = v;
                    phase += phaseInc;
                    if (phase >= kTwoPiD) phase -= kTwoPiD;
                }
                fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
                for (std::size_t s = 0; s < n; ++s) {
                    peak = std::max(peak, std::max(std::abs(outL[s]), std::abs(outR[s])));
                }
                done += n;
            }

            std::uint32_t crossfades = 0;
            for (std::size_t i = 0; i < kLoops; ++i) {
                crossfades += fe.getLoopCrossfadeCount(i);
            }

            INFO("SC-003 arm (110 Hz sine, max delay wander at max rate, 10 s): output peak "
                 << peak << ", crossfade onsets " << crossfades << ", clamps "
                 << fe.getClampEngagementCount());
            REQUIRE(peak > 0.0f);
            REQUIRE(crossfades > std::uint32_t{0});  // the staircase really fired
            REQUIRE(peak <= FeedbackEcology::kOutputClamp);
            REQUIRE(fe.getClampEngagementCount() == std::uint32_t{0});
            REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
        }

        // ---- SC-006's arm: the governor sweep, -40 dBFS RMS to 0 dBFS RMS in
        // 6 dB steps on ONE instance (the sweep is continuous - the criterion
        // reads the governor at the end of each step), 1 s per step. The top
        // step is the loudest in-spec drive anywhere in the phase, and it is
        // still inside the structural bound because the trim is at 0 dB.
        {
            FeedbackEcology fe;
            makeReference(fe);
            Krate::DSP::Xorshift32 rng(kReferenceDriveSeed);

            static constexpr std::array<float, 8> kSweepDb = {-40.0f, -34.0f, -28.0f, -22.0f,
                                                              -16.0f, -10.0f, -4.0f,  0.0f};
            float peak = 0.0f;
            for (const float db : kSweepDb) {
                peak = std::max(peak,
                                renderNoise(fe, rng, Krate::DSP::dbToGain(db), kOneSecondAt48k));
            }

            INFO("SC-006 arm (-40 to 0 dBFS RMS in 6 dB steps, 1 s each): output peak "
                 << peak << ", governor gain " << fe.getGovernorGain() << ", clamps "
                 << fe.getClampEngagementCount());
            REQUIRE(peak > 0.0f);
            REQUIRE(peak <= FeedbackEcology::kOutputClamp);
            REQUIRE(fe.getClampEngagementCount() == std::uint32_t{0});
            REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
        }

        // ---- SC-011's arm: the reference patch at all four rates. The clamp
        // pair and the resonator mapping are the two rate-dependent things in
        // the component, so a rate that pushed a loop's gain above unity would
        // show up here as an engagement rather than as a correct-looking render.
        {
            for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
                FeedbackEcology fe;
                makeReference(fe, fs);
                Krate::DSP::Xorshift32 rng(kReferenceDriveSeed);
                const auto total = static_cast<std::size_t>(2.0 * fs);
                const float peak = renderNoise(fe, rng, kMinus12dBFSRms, total);

                INFO("SC-011 arm at " << fs << " Hz (2 s): output peak " << peak << ", clamps "
                                      << fe.getClampEngagementCount());
                REQUIRE(peak > 0.0f);
                REQUIRE(peak <= FeedbackEcology::kOutputClamp);
                REQUIRE(fe.getClampEngagementCount() == std::uint32_t{0});
                REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
            }
        }
    }
}

// ==============================================================================
// T010 - the life-modulation lanes: FR-055's decimation and the two mappings
// ==============================================================================
// Covers SC-025 (FeedbackEcology_WanderRateMapping), SC-005
// (FeedbackEcology_DelayMotion) and SC-009 arm (d) alone
// (FeedbackEcology_SeedDeterminism; T015 lands arms (a)-(c) in the same case).
//
// WHAT IS AND IS NOT LIVE UNDER THE HEADER AT THIS TASK. appliedOwnFb_ and
// appliedCoupling_ are still written by T011/T012, so the six loops remain a
// FEED-FORWARD bank. Nothing below depends on circulating energy: every arm
// measures the CONTROL path (getLoopTargetDelayMs / getLoopTargetCutoffHz /
// getLoopCurrentDelayMs / getLoopCrossfadeCount / getLaneDecimation), which is
// driven entirely by the lanes and the two mappings. The one arm that compares
// RENDERS (SC-025 (c)'s rebase) compares two renders of the SAME build against
// each other, so the coefficient state cancels.
//
// STREAMING, NOT MATERIALISED. Two arms render 300 s and one renders 300 s at
// 192 kHz (57.6 M samples per channel = 230 MB materialised). Every arm but the
// rebase - which must fingerprint its output - drives the component block by
// block from a stateful drive and keeps only the per-block control readings.
// ==============================================================================

namespace {

/// The reference drive as a STATEFUL generator, so a 300 s render never
/// materialises its input. fillReferenceDrive() above constructs a fresh
/// Xorshift32 per call and would restart the noise on every block.
class ReferenceDrive {
public:
    explicit ReferenceDrive(std::uint32_t seed = kReferenceDriveSeed) noexcept : rng_(seed) {}

    void fill(float* left, float* right, std::size_t numSamples) noexcept {
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i] = rng_.nextFloat() * kReferenceDriveAmplitude;
            right[i] = rng_.nextFloat() * kReferenceDriveAmplitude;
        }
    }

private:
    Krate::DSP::Xorshift32 rng_;
};

/// Renders `totalSamples` through `fe` in `blockSize` blocks, discarding the
/// output and invoking `afterBlock(samplesRendered)` between blocks - which is
/// exactly where SC-005 requires its control readings to be taken.
template <typename AfterBlock>
void renderStreaming(FeedbackEcology& fe, std::size_t totalSamples, std::size_t blockSize,
                     ReferenceDrive& drive, const AfterBlock& afterBlock) {
    std::vector<float> inL(blockSize, 0.0f);
    std::vector<float> inR(blockSize, 0.0f);
    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);

    std::size_t done = 0;
    while (done < totalSamples) {
        const std::size_t n = std::min(blockSize, totalSamples - done);
        drive.fill(inL.data(), inR.data(), n);
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
        done += n;
        afterBlock(done);
    }
}

/// Pearson correlation of two equally long trajectories. Scale-invariant, which
/// is why the delay lanes (ms) and the cutoff lanes (Hz) can be compared pairwise
/// without normalisation.
[[nodiscard]] double pearson(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2) return 0.0;

    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        meanA += static_cast<double>(a[i]);
        meanB += static_cast<double>(b[i]);
    }
    meanA /= static_cast<double>(n);
    meanB /= static_cast<double>(n);

    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = static_cast<double>(a[i]) - meanA;
        const double db = static_cast<double>(b[i]) - meanB;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    const double denom = std::sqrt(saa * sbb);
    return denom > 0.0 ? sab / denom : 0.0;
}

}  // namespace

// ==============================================================================
// SC-025 - the FR-055 wander-rate mapping, its behavioural bite, and the rebase
// ==============================================================================
// THE DEFECT THIS CATCHES: a build with laneDecimation_ hard-wired to 1.
// BrownianDrift's tau saturates at kTauMax = 30 s, so WITHOUT the decimation the
// whole sub-range [0.002, 0.0333] Hz - INCLUDING this component's own 0.03 Hz
// default - is a dead zone in which every rate renders identically. Every
// criterion before this one exercised wander at either the default (where a
// correct build's effective 33.3 s is indistinguishable from a saturated 30 s)
// or at kMaxWanderRateHz (where the decimation is 1 in both builds), so the
// mechanism had no discriminating test at all.
// ==============================================================================
TEST_CASE("FeedbackEcology_WanderRateMapping", "[feedback_ecology]") {
    SECTION("(a) the mapping and both clamp ends, against plan S11's table") {
        FeedbackEcology fe;
        makeReference(fe);

        // prepare() step 9 is the first caller, at the default rate.
        REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kDefaultWanderRateHz));
        REQUIRE(fe.getLaneDecimation() == std::size_t{2});

        // 1 / 0.002 = 500 s; ceil(500 / 30) = 17.
        fe.setWanderRate(FeedbackEcology::kMinWanderRateHz);
        REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kMinWanderRateHz));
        REQUIRE(fe.getLaneDecimation() == std::size_t{17});
        REQUIRE(fe.getLaneDecimation() == FeedbackEcology::kMaxLaneDecimation);

        // 1 / 0.03 = 33.333 s; ceil(33.333 / 30) = 2.
        fe.setWanderRate(FeedbackEcology::kDefaultWanderRateHz);
        REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kDefaultWanderRateHz));
        REQUIRE(fe.getLaneDecimation() == std::size_t{2});

        // 1 / 1.0 = 1 s; ceil(1 / 30) = 1.
        fe.setWanderRate(FeedbackEcology::kMaxWanderRateHz);
        REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kMaxWanderRateHz));
        REQUIRE(fe.getLaneDecimation() == std::size_t{1});

        // Both clamp ends report the CLAMPED rate and the decimation that goes
        // with it - not the request, and not a stale decimation from before.
        fe.setWanderRate(0.0f);
        REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kMinWanderRateHz));
        REQUIRE(fe.getLaneDecimation() == std::size_t{17});

        fe.setWanderRate(1.0e6f);
        REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kMaxWanderRateHz));
        REQUIRE(fe.getLaneDecimation() == std::size_t{1});

        // A non-finite argument is a no-op; SC-012 asserts it in the
        // -fno-fast-math TU (T016), where a non-finite value can be BUILT.
    }

    SECTION("(b) the behavioural arm a hard-wired decimation fails") {
        // 0.0333 Hz IS THE TASK'S FAST ARM, and its decimation is 2, not 1:
        // 1 / 0.0333 = 30.030 s and ceil(30.030 / 30) = 2. Decimation 1 begins at
        // 1/30 = 0.033333... Hz. The arm does not turn on which side of that
        // boundary the fast rate lands - what it measures is that the SLOW rate
        // moves the delay targets far less - so the literal rate the task names is
        // kept and the decimation it really produces is asserted, rather than the
        // rate being quietly retuned to make a parenthetical true.
        constexpr float kSlowRateHz = 0.002f;   // decimation 17, effective tau 500 s
        constexpr float kFastRateHz = 0.0333f;  // decimation  2, effective tau 30.03 s
        constexpr double kFs = 48000.0;
        constexpr std::size_t kN = 300 * 48000;  // 300 s
        constexpr std::size_t kBlock = 512;

        struct RangeResult {
            std::array<float, kLoops> range{};
            std::size_t decimation = 0;
            float meanRange = 0.0f;
        };

        const auto measure = [&](float rateHz) {
            RangeResult out;
            FeedbackEcology fe;
            makeReference(fe, kFs);
            fe.setWanderRate(rateHz);
            fe.reset();  // same seed, same lane streams, decimation already in force
            out.decimation = fe.getLaneDecimation();

            std::array<float, kLoops> lo{};
            std::array<float, kLoops> hi{};
            for (std::size_t i = 0; i < kLoops; ++i) {
                lo[i] = fe.getLoopTargetDelayMs(i);
                hi[i] = lo[i];
            }

            ReferenceDrive drive;
            renderStreaming(fe, kN, kBlock, drive, [&](std::size_t) {
                for (std::size_t i = 0; i < kLoops; ++i) {
                    const float v = fe.getLoopTargetDelayMs(i);
                    lo[i] = std::min(lo[i], v);
                    hi[i] = std::max(hi[i], v);
                }
            });

            double sum = 0.0;
            for (std::size_t i = 0; i < kLoops; ++i) {
                out.range[i] = hi[i] - lo[i];
                sum += static_cast<double>(out.range[i]);
            }
            out.meanRange = static_cast<float>(sum / static_cast<double>(kLoops));
            return out;
        };

        const RangeResult slow = measure(kSlowRateHz);
        const RangeResult fast = measure(kFastRateHz);

        WARN("SC-025 (b) mean target-delay range over 300 s: slow ("
             << kSlowRateHz << " Hz, decimation " << slow.decimation << ") = " << slow.meanRange
             << " ms; fast (" << kFastRateHz << " Hz, decimation " << fast.decimation
             << ") = " << fast.meanRange << " ms; ratio slow/fast = "
             << (fast.meanRange > 0.0f ? slow.meanRange / fast.meanRange : -1.0f));

        REQUIRE(slow.decimation == std::size_t{17});
        REQUIRE(fast.decimation == std::size_t{2});

        // The fast arm must actually move, or the ratio below is 0/0.
        REQUIRE(fast.meanRange > 0.0f);

        // THE FACTOR OF 2 IS THE FLOOR (O-2): it may be re-pinned UPWARD from a
        // recorded measurement, never downward. A build with laneDecimation_
        // hard-wired to 1 renders both arms at the saturated tau = 30 s and
        // produces a ratio of 1.0.
        REQUIRE(slow.meanRange <= 0.5f * fast.meanRange);
    }

    SECTION("(c) a same-value setWanderRate REBASES the counter, it does not reset it") {
        // THE DEFECT: laneCounter_ = 0 in place of laneCounter_ %= newDecimation.
        // At the default rate the decimation is 2, so a reset-on-write build
        // advances the lanes on EVERY control step instead of every second one and
        // diverges grossly. 96 is deliberately NOT a multiple of the 64-sample
        // control chunk, so the writes land at every phase of the grid.
        constexpr double kFs = 48000.0;
        constexpr std::size_t kN = 60 * 48000;  // 60 s
        constexpr std::size_t kBlock = 96;

        using Krate::DSP::TestUtils::compareFingerprints;
        using Krate::DSP::TestUtils::fingerprintRender;

        std::vector<float> inL(kN);
        std::vector<float> inR(kN);
        fillReferenceDrive(inL.data(), inR.data(), kN);

        // BOTH renders use the SAME 96-sample partition, so the only difference
        // between them is the setWanderRate call. Partition invariance is
        // SC-010's criterion, not this one's.
        std::vector<float> refL(kN, 0.0f);
        std::vector<float> refR(kN, 0.0f);
        {
            FeedbackEcology fe;
            makeReference(fe, kFs);
            renderBlocks(fe, inL.data(), inR.data(), refL.data(), refR.data(), kN, kBlock);
        }

        std::vector<float> outL(kN, 0.0f);
        std::vector<float> outR(kN, 0.0f);
        {
            FeedbackEcology fe;
            makeReference(fe, kFs);
            std::size_t done = 0;
            while (done < kN) {
                const std::size_t n = std::min(kBlock, kN - done);
                fe.setWanderRate(fe.getWanderRate());  // the same value: nothing changes
                fe.processBlock(inL.data() + done, inR.data() + done, outL.data() + done,
                                outR.data() + done, n);
                done += n;
            }
            REQUIRE(fe.getLaneDecimation() == std::size_t{2});
            REQUIRE(fe.getWanderRate() == Approx(FeedbackEcology::kDefaultWanderRateHz));
        }

        const auto cmpL = compareFingerprints(fingerprintRender(outL), fingerprintRender(refL));
        const auto cmpR = compareFingerprints(fingerprintRender(outR), fingerprintRender(refR));
        INFO("rebase left: " << cmpL.detail << " worstSample=" << cmpL.worstSampleError
                             << " worstMetric=" << cmpL.worstMetricRelativeError);
        REQUIRE(cmpL.withinTolerance());
        INFO("rebase right: " << cmpR.detail << " worstSample=" << cmpR.worstSampleError
                              << " worstMetric=" << cmpR.worstMetricRelativeError);
        REQUIRE(cmpR.withinTolerance());
    }
}

// ==============================================================================
// SC-005 - the delay position really moves, and the reading is SETTLED
// ==============================================================================
// getCurrentDelaySamples() is the GAIN-WEIGHTED AVERAGE of the two taps
// (crossfading_delay_line.h:306-309), so a reading taken while a crossfade is in
// flight sits at an intermediate position that belongs to no settled step and
// would inflate every distinct-value count here. A reading therefore counts only
// when getLoopCrossfadeCount(i) is unchanged from the previous sampled block AND
// unchanged at the next - the two-sided test the task specifies.
//
// WHEN SETTLED, exactly one tap gain is 1.0f and the other 0.0f
// (crossfading_delay_line.h:257-259, :273-275), so the reported position IS the
// active tap and changes only when a crossfade completes. Two settled values
// therefore differ by at least kCrossfadeThresholdSamples = 100 samples
// (2.268 ms at 44.1 kHz, 0.521 ms at 192 kHz), which is why kDistinctToleranceMs
// below can be an order of magnitude smaller than the smallest real step and
// still only reject floating-point noise.
// ==============================================================================

namespace {

/// Well below the smallest real step at every rate this phase renders
/// (0.521 ms at 192 kHz), so it separates only FP noise from a genuine
/// crossfade step.
constexpr float kDistinctToleranceMs = 0.05f;

void addDistinct(std::vector<float>& values, float v) {
    for (const float existing : values) {
        if (std::abs(existing - v) <= kDistinctToleranceMs) return;
    }
    values.push_back(v);
}

/// SC-005's two-sided settled test, applied streaming to one loop's per-block
/// readings. Holds exactly three readings at a time.
class SettledDelayTracker {
public:
    void push(float delayMs, std::uint32_t crossfadeCount, double timeSeconds,
              double earlyWindowSeconds) {
        if (filled_ >= 2 && mid_.count == old_.count && crossfadeCount == mid_.count) {
            addDistinct(all_, mid_.delayMs);
            if (!anySettled_) {
                lo_ = mid_.delayMs;
                hi_ = mid_.delayMs;
                anySettled_ = true;
            }
            lo_ = std::min(lo_, mid_.delayMs);
            hi_ = std::max(hi_, mid_.delayMs);
            if (mid_.time <= earlyWindowSeconds) addDistinct(early_, mid_.delayMs);
        }
        old_ = mid_;
        mid_ = Reading{.delayMs = delayMs, .count = crossfadeCount, .time = timeSeconds};
        if (filled_ < 2) ++filled_;
    }

    [[nodiscard]] std::size_t distinctSettled() const noexcept { return all_.size(); }
    [[nodiscard]] std::size_t distinctSettledEarly() const noexcept { return early_.size(); }
    [[nodiscard]] float spreadMs() const noexcept { return anySettled_ ? hi_ - lo_ : 0.0f; }

private:
    struct Reading {
        float delayMs = 0.0f;
        std::uint32_t count = 0;
        double time = 0.0;
    };

    Reading old_{};
    Reading mid_{};
    int filled_ = 0;
    std::vector<float> all_;
    std::vector<float> early_;
    float lo_ = 0.0f;
    float hi_ = 0.0f;
    bool anySettled_ = false;
};

}  // namespace

TEST_CASE("FeedbackEcology_DelayMotion", "[feedback_ecology]") {
    // The two rates that carry arms (a) and (c). 44.1 kHz is the BINDING rate for
    // the FR-023 derivation - 100 samples is 2.268 ms there against 2.083 ms at
    // 48 kHz and 0.521 ms at 192 kHz - so a build that only satisfies SC-005 at
    // 48 kHz has not satisfied it.
    const auto runMotionArm = [](double fs, const char* label) {
        constexpr std::size_t kBlock = 512;
        constexpr double kTotalSeconds = 300.0;
        constexpr double kEarlySeconds = 120.0;
        const auto total = static_cast<std::size_t>(kTotalSeconds * fs);

        FeedbackEcology fe;
        makeReference(fe, fs);

        std::array<SettledDelayTracker, kLoops> trackers{};
        ReferenceDrive drive;
        renderStreaming(fe, total, kBlock, drive, [&](std::size_t rendered) {
            const double t = static_cast<double>(rendered) / fs;
            for (std::size_t i = 0; i < kLoops; ++i) {
                trackers[i].push(fe.getLoopCurrentDelayMs(i), fe.getLoopCrossfadeCount(i), t,
                                 kEarlySeconds);
            }
        });

        for (std::size_t i = 0; i < kLoops; ++i) {
            const float base = FeedbackEcology::kDefaultLoopDelayMs[i];
            WARN(label << " loop " << i << " (base " << base
                       << " ms): distinct settled = " << trackers[i].distinctSettled()
                       << ", distinct settled in the first 120 s = "
                       << trackers[i].distinctSettledEarly()
                       << ", spread = " << trackers[i].spreadMs() << " ms ("
                       << (100.0f * trackers[i].spreadMs() / base)
                       << " % of base), crossfade onsets = " << fe.getLoopCrossfadeCount(i));
        }

        for (std::size_t i = 0; i < kLoops; ++i) {
            const float base = FeedbackEcology::kDefaultLoopDelayMs[i];
            INFO(label << " loop " << i);

            // THE FLOOR THAT MAY NEVER BE WEAKENED: two distinct settled values -
            // one completed step - for EVERY loop. If a loop cannot clear it the
            // fix is FR-023's derivation (raise that loop's default wander
            // fraction), never a shorter assertion.
            REQUIRE(trackers[i].distinctSettled() >= std::size_t{2});

            // Provisional above the floor (O-2): re-pinnable UPWARD only, and
            // only from a recorded measurement.
            REQUIRE(trackers[i].distinctSettled() >= std::size_t{3});
            REQUIRE(trackers[i].spreadMs() >= 0.02f * base);

            if (base >= 109.0f) {
                REQUIRE(trackers[i].distinctSettledEarly() >= std::size_t{4});
            }
        }
    };

    SECTION("(a) 300 s at 44.1 kHz - the binding rate") {
        runMotionArm(44100.0, "SC-005 (a) 44.1 kHz");
    }

    SECTION("(b) an excursion below the crossfade threshold does not move the position") {
        // baseDelayMs = 10, delayWanderFraction = 0.05 - peak excursion 0.5 ms =
        // 22 samples at 44.1 kHz, below kCrossfadeThresholdSamples = 100. This is
        // the DOCUMENTED CONSEQUENCE of the shipped delay line, asserted so it is
        // a known property rather than a bug someone rediscovers.
        constexpr double kFs = 44100.0;
        constexpr std::size_t kBlock = 512;
        const auto total = static_cast<std::size_t>(120.0 * kFs);

        FeedbackEcology fe;
        makeReference(fe, kFs);
        fe.setLoopDelayMs(0, 10.0f);
        fe.setLoopDelayWander(0, 0.05f);
        fe.reset();  // snaps the delay position to the mapped target from sample 0

        const float initial = fe.getLoopCurrentDelayMs(0);
        float worstDeviation = 0.0f;
        float targetLo = fe.getLoopTargetDelayMs(0);
        float targetHi = targetLo;
        ReferenceDrive drive;
        renderStreaming(fe, total, kBlock, drive, [&](std::size_t) {
            worstDeviation =
                std::max(worstDeviation, std::abs(fe.getLoopCurrentDelayMs(0) - initial));
            const float target = fe.getLoopTargetDelayMs(0);
            targetLo = std::min(targetLo, target);
            targetHi = std::max(targetHi, target);
        });

        WARN("SC-005 (b) initial = " << initial << " ms, worst deviation over 120 s = "
                                     << worstDeviation << " ms, target range = ["
                                     << targetLo << ", " << targetHi
                                     << "] ms, crossfade onsets = " << fe.getLoopCrossfadeCount(0));

        // THE TARGET DOES MOVE - it is only the realised position that cannot.
        // Without this the arm would also pass on a build whose lanes are frozen,
        // which is the opposite defect. (The lower half of the excursion is
        // rectified by kMinDelayMs = 10, so the motion is one-sided here.)
        REQUIRE(targetHi - targetLo > 0.1f);
        REQUIRE(worstDeviation <= kDistinctToleranceMs);
        REQUIRE(fe.getLoopCrossfadeCount(0) == std::uint32_t{0});
    }

    SECTION("(c) 300 s at 192 kHz") {
        runMotionArm(192000.0, "SC-005 (c) 192 kHz");
    }
}

// ==============================================================================
// SC-009 arm (d) - the twelve lane streams are independent
// ==============================================================================
// T015 lands arms (a)-(c) (the reproducibility arms) in this same case. Arm (d)
// is here because it is what the FR-054 salt table and setSeed()'s per-lane
// derivation are FOR, and both land at T010's mapping.
//
// THE REALISED READINGS MUST NOT BE USED. getLoopCurrentDelayMs is a crossfade
// staircase with near-zero variance between steps, so its correlation is 0/0 or
// is dominated by two quantisation levels. The TARGET getters are the continuous
// lane trajectories, which is exactly why FR-062 exposes them.
//
// AND THE TARGET LEVELS MUST NOT BE CORRELATED DIRECTLY EITHER - the statistical
// arm runs on their PER-BLOCK INCREMENTS. That is an AMENDMENT to SC-009 (d),
// carried into spec.md and tasks.md in the same change, with the measurements
// that forced it written out at the assertion below. Short version: over 120 s
// these lanes supply about two independent samples, so the correlation of the
// LEVELS is ~0.7 for twelve provably independent streams and the criterion as
// first written could not be satisfied by any correct implementation.
// ==============================================================================
TEST_CASE("FeedbackEcology_SeedDeterminism", "[feedback_ecology]") {
    // -------------------------------------------------------------------------
    // T015 - arms (a), (b) and (c), the reproducibility arms.
    //
    // 30 s at 48 kHz, and the length is chosen FOR ARM (c). The FR-055 default
    // wander rate is 0.03 Hz, whose real-time decorrelation time is 33.3 s (the
    // derivation is in arm (d) below), so over a 10 s window two seeds have
    // barely separated and (c) would be measuring the opening transient - which
    // is seed-INDEPENDENT - rather than the seed. Arms (a) and (b) are exact at
    // any length; they use the same render so all three compare like with like.
    //
    // renderSeeded materialises its own copy of the drive on each call rather
    // than hoisting it to case scope: Catch2 re-runs a TEST_CASE body once per
    // leaf SECTION, so a hoisted 30 s buffer would be built four times over -
    // including for arm (d), which does not use it at all.
    // -------------------------------------------------------------------------
    constexpr std::size_t kReproSamples = 30u * 48000u;  // 30 s at 48 kHz

    const auto renderSeeded = [](std::uint32_t seed, std::vector<float>& outL,
                                 std::vector<float>& outR) {
        constexpr double kFsRepro = 48000.0;
        constexpr std::size_t kBlockRepro = 512;
        constexpr std::size_t kTotalRepro = 30u * 48000u;

        std::vector<float> inL(kTotalRepro, 0.0f);
        std::vector<float> inR(kTotalRepro, 0.0f);
        fillReferenceDrive(inL.data(), inR.data(), kTotalRepro);

        outL.assign(kTotalRepro, 0.0f);
        outR.assign(kTotalRepro, 0.0f);

        FeedbackEcology fe;
        makeReference(fe, kFsRepro, FeedbackEcology::kMaxLoops, seed);
        renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), kTotalRepro,
                     kBlockRepro);
    };

    SECTION("(a) two instances, the same seed, the same render") {
        using Krate::DSP::TestUtils::compareFingerprints;
        using Krate::DSP::TestUtils::fingerprintRender;

        std::vector<float> firstL;
        std::vector<float> firstR;
        std::vector<float> secondL;
        std::vector<float> secondR;
        renderSeeded(0x5EEDu, firstL, firstR);
        renderSeeded(0x5EEDu, secondL, secondR);

        const auto cmpL = compareFingerprints(fingerprintRender(secondL), fingerprintRender(firstL));
        const auto cmpR = compareFingerprints(fingerprintRender(secondR), fingerprintRender(firstR));

        WARN("SC-009 (a) worst metric relative error = "
             << std::max(cmpL.worstMetricRelativeError, cmpR.worstMetricRelativeError)
             << ", worst checkpoint error = "
             << std::max(cmpL.worstSampleError, cmpR.worstSampleError));

        INFO("left: " << cmpL.detail);
        REQUIRE(cmpL.withinTolerance());
        INFO("right: " << cmpR.detail);
        REQUIRE(cmpR.withinTolerance());
    }

    SECTION("(b) reset() replays the render on the SAME instance") {
        using Krate::DSP::TestUtils::compareFingerprints;
        using Krate::DSP::TestUtils::fingerprintRender;

        constexpr double kFsRepro = 48000.0;
        constexpr std::size_t kBlockRepro = 512;

        std::vector<float> inL(kReproSamples, 0.0f);
        std::vector<float> inR(kReproSamples, 0.0f);
        fillReferenceDrive(inL.data(), inR.data(), kReproSamples);

        std::vector<float> firstL(kReproSamples, 0.0f);
        std::vector<float> firstR(kReproSamples, 0.0f);
        std::vector<float> secondL(kReproSamples, 0.0f);
        std::vector<float> secondR(kReproSamples, 0.0f);

        FeedbackEcology fe;
        makeReference(fe, kFsRepro, FeedbackEcology::kMaxLoops, 0x5EEDu);
        renderBlocks(fe, inL.data(), inR.data(), firstL.data(), firstR.data(), kReproSamples,
                     kBlockRepro);

        // S3.2 step 6 is the one line this arm exists for: reset() re-derives
        // every lane stream from the STORED seed, so the second render starts
        // from the same twelve trajectories and not from where the first left
        // them.
        fe.reset();
        renderBlocks(fe, inL.data(), inR.data(), secondL.data(), secondR.data(), kReproSamples,
                     kBlockRepro);

        const auto cmpL = compareFingerprints(fingerprintRender(secondL), fingerprintRender(firstL));
        const auto cmpR = compareFingerprints(fingerprintRender(secondR), fingerprintRender(firstR));

        WARN("SC-009 (b) worst metric relative error = "
             << std::max(cmpL.worstMetricRelativeError, cmpR.worstMetricRelativeError)
             << ", worst checkpoint error = "
             << std::max(cmpL.worstSampleError, cmpR.worstSampleError));

        INFO("left: " << cmpL.detail);
        REQUIRE(cmpL.withinTolerance());
        INFO("right: " << cmpR.detail);
        REQUIRE(cmpR.withinTolerance());
    }

    SECTION("(c) two different seeds produce a DIFFERENT render") {
        using Krate::DSP::TestUtils::compareFingerprints;
        using Krate::DSP::TestUtils::fingerprintRender;
        using Krate::DSP::TestUtils::kSampleTolerance;

        std::vector<float> aL;
        std::vector<float> aR;
        std::vector<float> bL;
        std::vector<float> bR;
        renderSeeded(0x5EEDu, aL, aR);
        renderSeeded(0xA11CEu, bL, bR);

        const auto fpA = fingerprintRender(aL);
        const auto fpB = fingerprintRender(bL);
        const auto cmp = compareFingerprints(fpB, fpA);

        double sumAbsDiff = 0.0;
        for (std::size_t s = 0; s < kReproSamples; ++s) {
            sumAbsDiff += std::abs(static_cast<double>(aL[s]) - static_cast<double>(bL[s]));
        }
        const double meanAbsDiff = sumAbsDiff / static_cast<double>(kReproSamples);

        // =====================================================================
        // *** THE FLOOR IS AN AMENDMENT TO SC-009 (c), NOT A RELAXATION. ***
        //
        // The criterion first read "the mean absolute difference exceeds
        // 100 x kSampleTolerance" - 0.05 in absolute sample units. Measured at
        // T015's first build, that is UNSATISFIABLE BY ANY IMPLEMENTATION of
        // this spec, correct or broken, by a factor of 43:
        //
        //   30 s at 48 kHz, reference patch, seeds 0x5EED and 0xA11CE
        //     render a: rms 0.0014655 (-56.7 dBFS)  peak 0.0076627  meanAbs 0.0011695
        //     render b: rms 0.0014626 (-56.7 dBFS)  peak 0.0076667  meanAbs 0.0011673
        //     mean |a - b| = 0.0013306
        //
        // mean|a-b| <= mean|a| + mean|b| = 0.0023368 for ANY pair of renders of
        // this patch, so 0.05 is out of reach before the seed is even chosen.
        // The patch is quiet BY DESIGN and the header already records why
        // (DERIVATION TABLE 3): every default loop carries a Q ~ 100 resonator
        // whose equivalent noise bandwidth is 3.3-18.8 Hz out of 24 kHz, so a
        // broadband drive reaches the loops ~30 dB down - the same measurement
        // that moved kDefaultGovernorThresholdDb from -6 dB to -52 dB at T012.
        // No fixture rescues the old number either: the FR-044 governor turns a
        // 40 dB drive rise into 20.6 dB of output rise (SC-006 (b)), so even the
        // maximum wet trim (+24 dB, x15.85) over a 0 dBFS drive lands near 0.04
        // - and would then be measuring the trim rather than the seed.
        //
        // WHAT REPLACES IT, AND WHY IT STILL DISCRIMINATES. The difference is
        // floored against the RENDER'S OWN mean absolute level, which is the
        // quantity "audibly different" was always about:
        //   * two statistically independent renders of one process give
        //     mean|a-b| / mean|a| = sqrt(2) = 1.414 (Gaussian);
        //   * a build in which the seed never reaches the twelve lanes gives
        //     EXACTLY 0 - that build is what arm (a) above renders, and it
        //     reports 0.000000;
        //   * this build measures 1.138, i.e. 80 % of the independent limit.
        // The 0.5 bound therefore keeps a 2.28x margin under the measurement
        // while failing every build whose seed does not reach the lanes. The
        // second clause keeps an ABSOLUTE floor in the criterion - the
        // difference must also clear the comparator's own sample resolution,
        // kSampleTolerance, which it does by 2.66x.
        //
        // The intent, the render length, the seeds and the fingerprint clause
        // did NOT move. Only the quantity the floor is expressed against did.
        // =====================================================================
        const double referenceMeanAbs = fpA.meanAbs;
        const double relativeFloor = 0.5;
        const double relativeDiff = meanAbsDiff / referenceMeanAbs;

        WARN("SC-009 (c) mean |difference| = "
             << meanAbsDiff << " = " << relativeDiff << " x the render's own meanAbs ("
             << referenceMeanAbs << ") against the " << relativeFloor << " floor and the "
             << kSampleTolerance << " absolute floor; worst metric relative error = "
             << cmp.worstMetricRelativeError << " against " << cmp.metricTolerance
             << ", worst checkpoint error = " << cmp.worstSampleError << " against "
             << cmp.sampleTolerance);

        // BOTH clauses matter. The fingerprint failing on its own would also be
        // satisfied by a build whose two seeds differ only in the last bit of
        // one checkpoint; the mean-difference floor is what makes "different"
        // mean audibly different.
        INFO(cmp.detail);
        REQUIRE_FALSE(cmp.withinTolerance());
        REQUIRE(referenceMeanAbs > 0.0);
        REQUIRE(relativeDiff > relativeFloor);
        REQUIRE(meanAbsDiff > static_cast<double>(kSampleTolerance));
    }

    SECTION("(d) the twelve lane streams are distinct and uncorrelated") {
        // The DETERMINISTIC half first - it is the actual salt-collision guard,
        // it costs nothing, and if it fails the statistical half below is noise.
        // The salts are the S1.6 table transcribed: kSaltDelayLane = 0 + loop,
        // kSaltCutoffLane = 16 + loop. They are private to the component, so the
        // test recomputes them rather than reading them, which is what makes this
        // a guard against a RENUMBERING and not a tautology.
        constexpr std::uint32_t kSeed = 0x5EEDu;
        constexpr std::size_t kSaltDelayLane = 0;
        constexpr std::size_t kSaltCutoffLane = 16;

        std::array<std::uint32_t, 2 * kLoops> streamSeeds{};
        for (std::size_t i = 0; i < kLoops; ++i) {
            streamSeeds[i] = Krate::DSP::deriveStreamSeed(kSeed, kSaltDelayLane + i);
            streamSeeds[kLoops + i] = Krate::DSP::deriveStreamSeed(kSeed, kSaltCutoffLane + i);
        }
        for (std::size_t a = 0; a < streamSeeds.size(); ++a) {
            // deriveStreamSeed guarantees a non-zero result, and that is
            // load-bearing: Xorshift32::seed(0) silently substitutes its own
            // default (random.h:73-75), so two lanes hashing to 0 would COLLAPSE
            // ONTO ONE STREAM.
            REQUIRE(streamSeeds[a] != std::uint32_t{0});
            for (std::size_t b = a + 1; b < streamSeeds.size(); ++b) {
                INFO("stream seed collision between lanes " << a << " and " << b);
                REQUIRE(streamSeeds[a] != streamSeeds[b]);
            }
        }

        // ---------------------------------------------------------------------
        // THE STATISTICAL HALF - ON THE PER-BLOCK INCREMENTS OF THE TWELVE
        // TARGET TRAJECTORIES, NOT ON THEIR LEVELS.
        //
        // *** THIS IS AN AMENDMENT TO SC-009 (d), NOT A RELAXATION. *** The
        // criterion first read "the pairwise Pearson correlation between the
        // twelve lane target trajectories over a 120 s render is below 0.25 for
        // every pair". Measured, that is not a test of independence: it is
        // unsatisfiable by ANY correct implementation, because a Pearson estimate
        // needs independent samples and 120 s of these lanes carries about two.
        //
        //   Each lane is a BrownianDrift at the FR-055 default 0.03 Hz. From
        //   setWanderRate()'s own table: laneDecimation_ = 2 and per-advance
        //   tau = 16.667 s, so the REAL-TIME 1/e decorrelation time is 33.3 s.
        //   A 120 s window is 3.6 correlation times - N_eff ~ 2 - and the
        //   sampling standard deviation of r between two INDEPENDENT series that
        //   slow is ~0.5. The arm then draws 66 such pairs and keeps the WORST.
        //
        // MEASURED (standalone probe, twelve shipped BrownianDrift lanes driven
        // with this component's seeds, smoothness and decimation, no
        // FeedbackEcology code in the picture):
        //   * seed 0x5EED, 120 s: worst |r| on LEVELS = 0.6916, lanes 0 and 7 -
        //     the exact figure this component produced, so that number is a
        //     property of twelve independent OU streams, not of the component;
        //   * across 64 different base seeds: worst |r| on levels 0.5994..0.8894
        //     (mean 0.7566), above 0.25 for 64 seeds out of 64;
        //   * the level estimator converges only on absurd windows: 0.2970 at
        //     1800 s, 0.1579 at 7200 s, 0.0819 at 28800 s.
        //
        // SO THE ESTIMATOR MOVES; THE 0.25 BOUND AND THE 120 s RENDER DO NOT.
        // The per-block FIRST DIFFERENCE of a trajectory is driven by that lane's
        // own OU innovations, so its samples are effectively independent and the
        // estimate converges inside the window the criterion pins:
        //   * seed 0x5EED, 120 s: worst |r| on INCREMENTS = 0.0715;
        //   * across the same 64 seeds: worst 0.0922, mean 0.0628 - a 2.7x margin
        //     under the UNCHANGED 0.25 bound.
        // The detector keeps its teeth, and that is asserted here rather than
        // asserted about: the POSITIVE CONTROL at the end of this section makes
        // two lanes share one stream - the salt collision this arm exists to
        // catch - through the two DIFFERENT mappings, and the increment estimator
        // reports 0.9855 (levels 0.9977).
        //
        // The level figure is still computed and REPORTED so the historical
        // number stays visible and nobody re-derives this from scratch.
        // ---------------------------------------------------------------------
        constexpr double kFs = 48000.0;
        constexpr std::size_t kBlock = 512;
        constexpr double kMaxAbsCorrelation = 0.25;
        const auto total = static_cast<std::size_t>(120.0 * kFs);

        FeedbackEcology fe;
        makeReference(fe, kFs, std::size_t{6}, kSeed);

        std::array<std::vector<float>, 2 * kLoops> trajectories{};
        for (auto& t : trajectories) {
            t.reserve(total / kBlock + 2);
        }

        ReferenceDrive drive;
        renderStreaming(fe, total, kBlock, drive, [&](std::size_t) {
            for (std::size_t i = 0; i < kLoops; ++i) {
                trajectories[i].push_back(fe.getLoopTargetDelayMs(i));
                trajectories[kLoops + i].push_back(fe.getLoopTargetCutoffHz(i));
            }
        });

        std::array<std::vector<float>, 2 * kLoops> increments{};
        for (std::size_t a = 0; a < trajectories.size(); ++a) {
            increments[a].reserve(trajectories[a].size());
            for (std::size_t k = 1; k < trajectories[a].size(); ++k) {
                increments[a].push_back(trajectories[a][k] - trajectories[a][k - 1]);
            }
        }

        double worstAbs = 0.0;
        std::size_t worstA = 0;
        std::size_t worstB = 0;
        double worstLevelAbs = 0.0;
        std::size_t worstLevelA = 0;
        std::size_t worstLevelB = 0;
        for (std::size_t a = 0; a < trajectories.size(); ++a) {
            for (std::size_t b = a + 1; b < trajectories.size(); ++b) {
                const double r = std::abs(pearson(increments[a], increments[b]));
                if (r > worstAbs) {
                    worstAbs = r;
                    worstA = a;
                    worstB = b;
                }
                const double rl = std::abs(pearson(trajectories[a], trajectories[b]));
                if (rl > worstLevelAbs) {
                    worstLevelAbs = rl;
                    worstLevelA = a;
                    worstLevelB = b;
                }
            }
        }

        WARN("SC-009 (d) worst |pearson| on INCREMENTS = "
             << worstAbs << " between lane " << worstA << " and lane " << worstB
             << " (0-5 = delay, 6-11 = cutoff), over " << increments[0].size()
             << " increments; REPORTED ONLY, the level figure this criterion used to assert on = "
             << worstLevelAbs << " between lanes " << worstLevelA << " and " << worstLevelB);

        INFO("worst increment pair: lanes " << worstA << " and " << worstB);
        REQUIRE(worstAbs < kMaxAbsCorrelation);

        // --- THE POSITIVE CONTROL: the detector must still catch a collision ---
        // Two lanes on ONE stream, read through the two DIFFERENT mappings (the
        // hardest shape for the detector - a delay lane against a cutoff lane).
        // Without this the arm above proves only that the number is small, never
        // that a small number MEANS anything. Driven exactly as updateControl()
        // step 1 drives the real lanes, at the decimation the instance reports.
        {
            const std::size_t decimation = fe.getLaneDecimation();
            REQUIRE(decimation >= std::size_t{1});
            const float perAdvanceTau =
                (1.0f / FeedbackEcology::kDefaultWanderRateHz) / static_cast<float>(decimation);
            const float smoothness =
                std::clamp((perAdvanceTau - Krate::DSP::BrownianDrift::kTauMin)
                               / (Krate::DSP::BrownianDrift::kTauMax
                                  - Krate::DSP::BrownianDrift::kTauMin),
                           0.0f, 1.0f);

            std::array<Krate::DSP::BrownianDrift, 2> twinned{};
            for (auto& lane : twinned) {
                lane.prepare(kFs);
                lane.setSmoothness(smoothness);
                lane.setSeed(Krate::DSP::deriveStreamSeed(kSeed, kSaltDelayLane));
                lane.reset();
            }

            std::vector<float> asDelay;
            std::vector<float> asCutoff;
            asDelay.reserve(total / kBlock + 2);
            asCutoff.reserve(total / kBlock + 2);
            std::size_t laneCounter = 0;
            for (std::size_t done = 0; done < total; done += kBlock) {
                for (std::size_t s = 0; s < kBlock; s += FeedbackEcology::kControlChunkSamples) {
                    if (laneCounter == 0) {
                        for (auto& lane : twinned) {
                            lane.processBlock(FeedbackEcology::kControlChunkSamples);
                        }
                    }
                    laneCounter = (laneCounter + 1) % decimation;
                }
                asDelay.push_back(FeedbackEcology::kDefaultLoopDelayMs[0]
                                  * (1.0f
                                     + FeedbackEcology::kDefaultDelayWanderFraction[0]
                                           * twinned[0].getCurrentValue()));
                asCutoff.push_back(std::exp2(
                    std::log2(FeedbackEcology::kDefaultLoopCutoffHz[1])
                    + FeedbackEcology::kDefaultCutoffWanderOctaves * twinned[1].getCurrentValue()));
            }

            std::vector<float> dDelay;
            std::vector<float> dCutoff;
            dDelay.reserve(asDelay.size());
            dCutoff.reserve(asCutoff.size());
            for (std::size_t k = 1; k < asDelay.size(); ++k) {
                dDelay.push_back(asDelay[k] - asDelay[k - 1]);
                dCutoff.push_back(asCutoff[k] - asCutoff[k - 1]);
            }

            const double collidedIncrements = std::abs(pearson(dDelay, dCutoff));
            WARN("SC-009 (d) positive control - two lanes on ONE stream report |pearson| = "
                 << collidedIncrements << " on increments against the " << kMaxAbsCorrelation
                 << " bound the independent lanes clear at " << worstAbs);
            REQUIRE(collidedIncrements > 0.9);
        }
    }
}

// ==============================================================================
// T011 helpers - the two row sums SC-015 compares against each other
// ==============================================================================
namespace {

/// FR-035's row sum RECONSTRUCTED FROM THE COEFFICIENTS ACTUALLY IN FORCE.
///
/// This, and not getLoopAppliedTotalGain(i), is what makes SC-015 (a) a
/// measurement. getLoopAppliedTotalGain is std::min(g, kMaxTotalLoopGain) BY
/// CONSTRUCTION (plan S5.3), so on its own it satisfies both of SC-015 (a)'s
/// clauses whatever coefficients the per-sample path is actually multiplying by
/// - a build that dropped the `* scale` from the coefficient writes would pass.
/// getLoopAppliedCoupling is the per-pair evidence that closes that hole.
///
/// Accumulated in double so the tolerance being asserted is the component's, not
/// the test's.
[[nodiscard]] double appliedRowSum(const FeedbackEcology& fe, std::size_t i) {
    double sum = static_cast<double>(fe.getLoopAppliedOwnFeedback(i));
    for (std::size_t j = 0; j < kLoops; ++j) {
        if (j == i) continue;
        sum += static_cast<double>(fe.getLoopAppliedCoupling(j, i));
    }
    return sum;
}

/// The RAW row sum the caller configured, read back through the CONFIGURATION
/// getters. Q3: `j` ranges over `j < numLoops` excluding `i`, ignoring
/// wake/dormancy - so this is a pure function of the configuration.
[[nodiscard]] double configuredRowSum(const FeedbackEcology& fe, std::size_t i) {
    double sum = static_cast<double>(fe.getLoopGain(i));
    for (std::size_t j = 0; j < fe.getNumLoops(); ++j) {
        if (j == i) continue;
        sum += static_cast<double>(fe.getCoupling(j, i));
    }
    return sum;
}

}  // namespace

// ==============================================================================
// SC-015 - row-sum normalisation is enforced and VISIBLE (FR-035, FR-075)
// ==============================================================================
// EVERY ARM RENDERS >= 100 ms AFTER WRITING ITS CONFIGURATION and before reading
// the applied getters. FR-035 normalises the SMOOTHED values, and 100 ms is
// 5 x the kCouplingSmoothMs = 20 ms one-pole time-to-99 % plus the
// kCompletionThreshold snap (smoother.h:199-202), after which every control-rate
// smoother is BIT-EXACTLY at its target.
//
// The last arm ("R-1") is not in the spec's SC-015 text: it is the plan's
// targeted assertion for risk R-1, the single most likely implementer trap in
// this phase (plan S5.0). calculateOnePolCoefficient takes a PER-SAMPLE rate, so
// the 42 smoothers advanced once per 64-sample control step MUST be configured
// with fs / kControlChunkSamples. Configured with fs the realised constant is
// 64 x 20 ms = 1.28 s, no other criterion in the phase would notice, and the bug
// would ship.
// ==============================================================================
TEST_CASE("FeedbackEcology_GainNormalisation", "[feedback_ecology]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kChunk = FeedbackEcology::kControlChunkSamples;  // 64
    // 80 x 64 = 5 120 samples = 106.7 ms at 48 kHz.
    constexpr std::size_t kSettleChunks = 80;
    constexpr float kMaxTotal = FeedbackEcology::kMaxTotalLoopGain;   // 0.95f
    constexpr float kMaxPair = FeedbackEcology::kMaxCouplingPerPair;  // 0.5f
    constexpr float kMaxOwn = FeedbackEcology::kMaxLoopGain;          // 0.90f

    std::vector<float> driveL(kChunk, 0.0f);
    std::vector<float> driveR(kChunk, 0.0f);
    fillReferenceDrive(driveL.data(), driveR.data(), kChunk);
    std::vector<float> outL(kChunk, 0.0f);
    std::vector<float> outR(kChunk, 0.0f);

    auto renderChunks = [&](FeedbackEcology& fe, std::size_t chunks) {
        for (std::size_t c = 0; c < chunks; ++c) {
            fe.processBlock(driveL.data(), driveR.data(), outL.data(), outR.data(), kChunk);
        }
    };

    SECTION("(a) the coefficients in force, over 1 000 random configurations") {
        FeedbackEcology fe;
        makeReference(fe, kFs);

        Krate::DSP::Xorshift32 rng(0xC0FFEEu);
        // Xorshift32::nextFloat() is bipolar [-1, +1] (random.h:59-63).
        auto unit = [&rng]() { return 0.5f * (rng.nextFloat() + 1.0f); };

        constexpr std::size_t kConfigurations = 1000;
        double worstOverBound = 0.0;       // max over i of (S_i - kMaxTotalLoopGain)
        double worstAgreement = 0.0;       // max over i of |total_i - S_i|
        double worstGlideOverBound = 0.0;  // the same bound, sampled DURING the glide
        double worstGlideAgreement = 0.0;
        double worstSaturatedError = 0.0;  // max |S_i - kMaxTotalLoopGain| when raw > bound
        std::size_t saturatedRows = 0;
        std::size_t unsaturatedRows = 0;

        for (std::size_t k = 0; k < kConfigurations; ++k) {
            // A per-configuration ceiling on both quantities, so the 1 000 draws
            // straddle kMaxTotalLoopGain instead of all landing far above it -
            // both branches of `scale` have to be exercised, and the arm asserts
            // below that both were.
            const float ownCeil = unit() * kMaxOwn;
            const float pairCeil = unit() * kMaxPair;
            for (std::size_t i = 0; i < kLoops; ++i) {
                fe.setLoopGain(i, unit() * ownCeil);
                for (std::size_t j = 0; j < kLoops; ++j) {
                    if (i == j) continue;
                    fe.setCoupling(i, j, unit() * pairCeil);
                }
            }

            // THE IN-GLIDE SAMPLING. The bound is a claim about every instant,
            // not only about the settled end points, so it is read once per
            // control chunk while the 42 smoothers are still moving.
            for (std::size_t c = 0; c < kSettleChunks; ++c) {
                renderChunks(fe, 1);
                for (std::size_t i = 0; i < kLoops; ++i) {
                    const double s = appliedRowSum(fe, i);
                    worstGlideOverBound =
                        std::max(worstGlideOverBound, s - static_cast<double>(kMaxTotal));
                    worstGlideAgreement = std::max(
                        worstGlideAgreement,
                        std::abs(static_cast<double>(fe.getLoopAppliedTotalGain(i)) - s));
                }
            }

            for (std::size_t i = 0; i < kLoops; ++i) {
                const double s = appliedRowSum(fe, i);
                const double raw = configuredRowSum(fe, i);
                worstOverBound = std::max(worstOverBound, s - static_cast<double>(kMaxTotal));
                worstAgreement =
                    std::max(worstAgreement,
                             std::abs(static_cast<double>(fe.getLoopAppliedTotalGain(i)) - s));
                // The guard band keeps a draw that lands ON the bound out of
                // both populations: there the two branches of `scale` agree to
                // within float rounding and neither classification is wrong.
                if (raw > static_cast<double>(kMaxTotal) + 1.0e-4) {
                    ++saturatedRows;
                    worstSaturatedError = std::max(
                        worstSaturatedError, std::abs(s - static_cast<double>(kMaxTotal)));
                } else if (raw < static_cast<double>(kMaxTotal) - 1.0e-4) {
                    ++unsaturatedRows;
                }
            }
        }

        WARN("SC-015 (a): " << kConfigurations << " configurations, " << saturatedRows
                            << " saturated rows / " << unsaturatedRows
                            << " unsaturated; worst settled excess over kMaxTotalLoopGain = "
                            << worstOverBound
                            << ", worst in-glide excess = " << worstGlideOverBound
                            << ", worst total/row-sum disagreement = "
                            << std::max(worstAgreement, worstGlideAgreement)
                            << ", worst saturated error = " << worstSaturatedError);

        // Both branches of `scale` must actually have been taken, or the arm
        // proves half of what it claims.
        REQUIRE(saturatedRows > 0u);
        REQUIRE(unsaturatedRows > 0u);

        REQUIRE(worstOverBound <= 1.0e-6);
        REQUIRE(worstGlideOverBound <= 1.0e-6);
        REQUIRE(worstAgreement <= 1.0e-6);
        REQUIRE(worstGlideAgreement <= 1.0e-6);
        REQUIRE(worstSaturatedError <= 1.0e-5);
    }

    SECTION("(b) the configuration getters are untouched by the normalisation") {
        FeedbackEcology fe;
        makeReference(fe, kFs);

        // A deliberately saturating configuration: every row's raw sum is 3.40.
        for (std::size_t i = 0; i < kLoops; ++i) {
            fe.setLoopGain(i, kMaxOwn);
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) continue;
                fe.setCoupling(i, j, kMaxPair);
            }
        }
        renderChunks(fe, kSettleChunks);

        for (std::size_t i = 0; i < kLoops; ++i) {
            // The stored TARGET survives; only the APPLIED value is scaled.
            REQUIRE(fe.getLoopGain(i) == Approx(kMaxOwn).margin(1.0e-6));
            REQUIRE(fe.getLoopAppliedOwnFeedback(i) < kMaxOwn);
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) {
                    REQUIRE(fe.getCoupling(i, j) == 0.0f);  // FR-030: the diagonal
                    continue;
                }
                REQUIRE(fe.getCoupling(i, j) == Approx(kMaxPair).margin(1.0e-6));
            }
        }
    }

    SECTION("(c) the worst case: raw 3.40 scaled to exactly kMaxTotalLoopGain") {
        FeedbackEcology fe;
        makeReference(fe, kFs);

        for (std::size_t i = 0; i < kLoops; ++i) {
            fe.setLoopGain(i, kMaxOwn);
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) continue;
                fe.setCoupling(i, j, kMaxPair);
            }
        }
        renderChunks(fe, kSettleChunks);

        // 0.90 + 5 x 0.5 = 3.40; scale = 0.95 / 3.40 = 0.279412;
        // applied own feedback = 0.251471; applied per-pair coupling = 0.139706.
        for (std::size_t i = 0; i < kLoops; ++i) {
            REQUIRE(configuredRowSum(fe, i) == Approx(3.40).margin(1.0e-5));
            REQUIRE(fe.getLoopAppliedOwnFeedback(i) == Approx(0.251471f).margin(1.0e-5));
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) continue;
                REQUIRE(fe.getLoopAppliedCoupling(j, i) == Approx(0.139706f).margin(1.0e-5));
            }
            REQUIRE(fe.getLoopAppliedTotalGain(i) == Approx(kMaxTotal).margin(1.0e-6));
            REQUIRE(appliedRowSum(fe, i)
                    == Approx(static_cast<double>(kMaxTotal)).margin(1.0e-6));
        }
    }

    SECTION("(d1) numLoops = 1 is COUNT-inert whatever the unused slots hold") {
        FeedbackEcology fe;
        // PrepareConfig{.numLoops = 1} SNAPS the count mask (prepare() step 11),
        // so slots 1..5 hold cm == 0.0f BIT-EXACTLY rather than gliding - which
        // is what makes the `== 0.0f` below an exact comparison and not a
        // tolerance.
        makeReference(fe, kFs, 1);
        REQUIRE(fe.getNumLoops() == 1u);

        // EVERY pair, including every pair AMONG the unused slots 1..5.
        for (std::size_t i = 0; i < kLoops; ++i) {
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) continue;
                fe.setCoupling(i, j, kMaxPair);
            }
        }
        renderChunks(fe, kSettleChunks);

        REQUIRE(fe.getLoopAppliedTotalGain(0) == Approx(fe.getLoopGain(0)).margin(1.0e-6));
        for (std::size_t j = 0; j < kLoops; ++j) {
            REQUIRE(fe.getLoopAppliedCoupling(j, 0) == 0.0f);
        }
    }

    SECTION("(d2) dormancy is inert - it never touches the count mask") {
        FeedbackEcology fe;
        makeReference(fe, kFs);

        for (std::size_t i = 0; i < kLoops; ++i) {
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) continue;
                fe.setCoupling(i, j, kMaxPair);
            }
        }
        renderChunks(fe, kSettleChunks);
        const float allAwake = fe.getLoopAppliedTotalGain(0);

        for (std::size_t sleeper = 1; sleeper < kLoops; ++sleeper) {
            fe.setLoopDormant(sleeper, true);
            renderChunks(fe, kSettleChunks);
            REQUIRE(fe.getLoopAppliedTotalGain(0) == Approx(allAwake).margin(1.0e-6));
        }
    }

    SECTION("R-1: the coupling smoothers run on the CONTROL clock, not on fs") {
        FeedbackEcology fe;
        makeReference(fe, kFs);

        // A fixture whose row sums stay well under kMaxTotalLoopGain, so the
        // applied coupling equals the configured one and the arm measures the
        // TIME CONSTANT and nothing else: row 1's raw sum is 0.1 + 0.3 = 0.4.
        for (std::size_t i = 0; i < kLoops; ++i) {
            fe.setLoopGain(i, 0.1f);
            for (std::size_t j = 0; j < kLoops; ++j) {
                if (i == j) continue;
                fe.setCoupling(i, j, 0.0f);
            }
        }
        renderChunks(fe, kSettleChunks);
        REQUIRE(fe.getLoopAppliedCoupling(0, 1) == Approx(0.0f).margin(1.0e-6));

        fe.setCoupling(0, 1, 0.3f);
        // ~24 ms of rendered time = 1 152 samples at 48 kHz. Configured at fs
        // instead of fs / kControlChunkSamples the realised constant is
        // 64 x 20 ms = 1.28 s, which reaches ~9 % of the step in this window.
        constexpr std::size_t kShortGlideChunks = 18;  // 18 x 64 = 1 152 samples
        renderChunks(fe, kShortGlideChunks);

        const float reached = fe.getLoopAppliedCoupling(0, 1);
        WARN("R-1: getLoopAppliedCoupling(0, 1) reached "
             << reached << " of its 0.3 target after " << (kShortGlideChunks * kChunk)
             << " samples (~24 ms); the fs-configured bug reaches ~0.028");
        REQUIRE(reached == Approx(0.3f).margin(0.01f));
    }
}

// ==============================================================================
// SC-006 - the energy governor (T012; FR-043, FR-044, FR-045)
// ==============================================================================
// Six arms in ONE linear body, deliberately NOT six Catch2 SECTIONs: a SECTION
// re-runs the whole TEST_CASE body, and the three level sweeps below render
// 3 x 8 x 20 s = 480 s of audio between them. Under SECTIONs that becomes
// 480 s PER SECTION. The arms are delimited by comment banners instead and each
// one carries its own INFO/WARN so a failure still names itself.
//
// THE THRESHOLD CROSSING IS MEASURED, NOT ASSUMED. The governor reads
// normGain * sum_i b_i - the loops' post-DC-blocker sum, which the network
// AMPLIFIES - and not the input, so no input level can be asserted to sit on a
// particular side of dbToGain(getGovernorThresholdDb()) a priori. Sweep 1 runs
// at ratio = 1 (the governor off, FR-044) purely to RECORD getGovernorRms() per
// step; every level-dependent assertion in arm (a) is then written against those
// recordings. If the recorded set does not STRADDLE the threshold the response
// is to re-measure and record kDefaultGovernorThresholdDb the Phase-3 way -
// never to move an assertion, which is why the straddle is asserted first.
// ==============================================================================
namespace {

constexpr double kGovFs = 48000.0;

/// -40 -> 0 dBFS RMS in 6 dB steps. 40 is not a multiple of 6, so the LAST step
/// is 4 dB: both endpoints the criterion names are actually rendered, which is
/// what arm (b)'s "40 dB input increase" arithmetic assumes.
constexpr std::array<float, 8> kGovernorSweepDb = {-40.0f, -34.0f, -28.0f, -22.0f,
                                                   -16.0f, -10.0f, -4.0f,  0.0f};

/// Xorshift32::nextFloat() is uniform on [-1, +1], whose RMS is 1/sqrt(3), so
/// the amplitude that lands the drive's RMS on dbToGain(db) is
/// dbToGain(db) * sqrt(3). Same construction as kReferenceDriveAmplitude, which
/// is this expression evaluated at -12 dBFS.
[[nodiscard]] float sweepAmplitude(float db) {
    return Krate::DSP::dbToGain(db) * 1.7320508f;
}

struct GovernorStepReading {
    float inputDb = 0.0f;  ///< the step's drive level, dBFS RMS
    float rms = 0.0f;      ///< getGovernorRms() at the END of the step
    float gain = 1.0f;     ///< getGovernorGain() at the END of the step
    float minGain = 1.0f;  ///< the smallest getGovernorGain() seen DURING it
    double outRms = 0.0;   ///< output RMS over the step's last `tailSeconds`
};

/// Renders the sweep through `fe`, STREAMING (tasks.md's mandatory rule for any
/// multi-second render): 8 x 20 s x 48 kHz is 7.68 M samples per channel, so the
/// statistics are accumulated block by block and at most one 4096-sample block
/// is ever in memory. The noise generator runs CONTINUOUSLY across the steps -
/// only its amplitude changes - so no step boundary injects a phase break.
[[nodiscard]] std::vector<GovernorStepReading> runGovernorSweep(
    FeedbackEcology& fe, double fs, double stepSeconds = 20.0, double tailSeconds = 5.0,
    std::uint32_t seed = kReferenceDriveSeed) {
    constexpr std::size_t kBlock = 4096;
    Krate::DSP::Xorshift32 rng(seed);
    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    const auto stepSamples = static_cast<std::size_t>(stepSeconds * fs);
    const auto tailSamples = static_cast<std::size_t>(tailSeconds * fs);
    const std::size_t tailStart = (stepSamples > tailSamples) ? (stepSamples - tailSamples) : 0;

    std::vector<GovernorStepReading> readings;
    readings.reserve(kGovernorSweepDb.size());

    for (const float db : kGovernorSweepDb) {
        const float amp = sweepAmplitude(db);
        double sumSq = 0.0;
        std::size_t counted = 0;
        float minGain = 1.0f;
        std::size_t done = 0;
        while (done < stepSamples) {
            const std::size_t n = std::min(kBlock, stepSamples - done);
            for (std::size_t s = 0; s < n; ++s) {
                inL[s] = rng.nextFloat() * amp;
                inR[s] = rng.nextFloat() * amp;
            }
            fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
            for (std::size_t s = 0; s < n; ++s) {
                if (done + s >= tailStart) {
                    const double mono =
                        0.5 * (static_cast<double>(outL[s]) + static_cast<double>(outR[s]));
                    sumSq += mono * mono;
                    ++counted;
                }
            }
            minGain = std::min(minGain, fe.getGovernorGain());
            done += n;
        }

        GovernorStepReading r;
        r.inputDb = db;
        r.rms = fe.getGovernorRms();
        r.gain = fe.getGovernorGain();
        r.minGain = minGain;
        r.outRms = (counted > 0) ? std::sqrt(sumSq / static_cast<double>(counted)) : 0.0;
        readings.push_back(r);
    }
    return readings;
}

/// The sweep's threshold-crossing level: the input dBFS of the FIRST step whose
/// end-of-step governor gain has dropped below unity. Returns +1.0f when the
/// sweep never crosses, which every caller treats as a failure.
[[nodiscard]] float crossingLevelDb(const std::vector<GovernorStepReading>& readings) {
    for (const GovernorStepReading& r : readings) {
        if (r.gain < 1.0f) return r.inputDb;
    }
    return 1.0f;
}

}  // namespace

TEST_CASE("FeedbackEcology_Governor", "[feedback_ecology]") {
    using Krate::DSP::TestUtils::ClickDetection;
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;

    const float threshold = Krate::DSP::dbToGain(FeedbackEcology::kDefaultGovernorThresholdDb);

    // -------------------------------------------------------------------------
    // Sweep 1 - ratio = 1, THE GOVERNOR OFF. Two jobs: it records the RMS every
    // level-dependent assertion in (a) is written against, and it IS arm (d).
    // -------------------------------------------------------------------------
    std::vector<GovernorStepReading> off;
    {
        FeedbackEcology fe;
        makeReference(fe, kGovFs);
        fe.setGovernorRatio(1.0f);
        fe.reset();
        REQUIRE(fe.getGovernorRatio() == 1.0f);
        REQUIRE(fe.getGovernorThresholdDb() == FeedbackEcology::kDefaultGovernorThresholdDb);
        off = runGovernorSweep(fe, kGovFs);
    }
    REQUIRE(off.size() == kGovernorSweepDb.size());

    // --- (d) at ratio = 1 the gain is EXACTLY 1.0f at every input level -------
    // Reachable as an exact identity, not a tolerance: the exponent
    // 1/ratio - 1 is exactly 0.0f and std::pow(x, 0.0f) is exactly 1.0f for
    // every finite positive x, so the law's target is the literal 1.0f the ramp
    // was snapped to by prepare() and process() early-outs on == target.
    for (const GovernorStepReading& r : off) {
        INFO("(d) ratio = 1, step " << r.inputDb << " dBFS, rms " << r.rms);
        REQUIRE(r.gain == 1.0f);
        REQUIRE(r.minGain == 1.0f);
        REQUIRE(Krate::DSP::detail::isFinite(r.rms));
    }

    // --- (a) the straddle, asserted BEFORE anything is read from it -----------
    bool anyBelow = false;
    bool anyAbove = false;
    for (const GovernorStepReading& r : off) {
        if (r.rms < threshold) anyBelow = true;
        if (r.rms > threshold) anyAbove = true;
    }
    WARN("SC-006 (a): threshold "
         << threshold << " (" << FeedbackEcology::kDefaultGovernorThresholdDb
         << " dB); governor-off tracker RMS per step: " << off[0].rms << ", " << off[1].rms << ", "
         << off[2].rms << ", " << off[3].rms << ", " << off[4].rms << ", " << off[5].rms << ", "
         << off[6].rms << ", " << off[7].rms);
    INFO("the -40..0 dBFS sweep must STRADDLE dbToGain(kDefaultGovernorThresholdDb); "
         "if it does not, re-measure and record the default - never move an assertion");
    REQUIRE(anyBelow);
    REQUIRE(anyAbove);

    // -------------------------------------------------------------------------
    // Sweep 2 - the reference patch at its governor defaults (ratio = 8).
    // -------------------------------------------------------------------------
    std::vector<GovernorStepReading> on;
    {
        FeedbackEcology fe;
        makeReference(fe, kGovFs);
        REQUIRE(fe.getGovernorRatio() == FeedbackEcology::kDefaultGovernorRatio);
        on = runGovernorSweep(fe, kGovFs);
    }
    REQUIRE(on.size() == off.size());

    WARN("SC-006 (a): governed gain per step: " << on[0].gain << ", " << on[1].gain << ", "
                                                << on[2].gain << ", " << on[3].gain << ", "
                                                << on[4].gain << ", " << on[5].gain << ", "
                                                << on[6].gain << ", " << on[7].gain);

    // --- (a) non-increasing, unity below the threshold, sub-unity above -------
    for (std::size_t k = 1; k < on.size(); ++k) {
        INFO("(a) non-increasing across steps " << on[k - 1].inputDb << " -> " << on[k].inputDb);
        REQUIRE(on[k].gain <= on[k - 1].gain);
    }
    for (std::size_t k = 0; k < on.size(); ++k) {
        INFO("(a) step " << on[k].inputDb << " dBFS: governor-off RMS " << off[k].rms
                         << " vs threshold " << threshold << ", governed gain " << on[k].gain);
        if (off[k].rms < threshold) {
            REQUIRE(on[k].gain == 1.0f);
        } else if (off[k].rms > threshold) {
            REQUIRE(on[k].gain < 1.0f);
        }
        REQUIRE(on[k].gain >= FeedbackEcology::kGovernorMinGain);
        REQUIRE(on[k].minGain >= FeedbackEcology::kGovernorMinGain);
    }
    INFO("(a) 0 dBFS gain " << on.back().gain);
    REQUIRE(on.back().gain < 0.6f);

    // --- (b) the output rises SUB-LINEARLY ------------------------------------
    // 40 dB of input increase must buy at most 28 dB of output increase.
    REQUIRE(on.front().outRms > 0.0);
    REQUIRE(on.back().outRms > 0.0);
    const double outRiseDb = 20.0 * std::log10(on.back().outRms / on.front().outRms);
    WARN("SC-006 (b): output RMS rose " << outRiseDb << " dB across a 40 dB input rise");
    INFO("(b) output rise " << outRiseDb << " dB must be at least 12 dB under the 40 dB input rise");
    REQUIRE(outRiseDb <= 28.0);

    // --- (f) loop-count invariance --------------------------------------------
    // The arm a RAW, unscaled sum_i b_i tracker fails: without FR-017's
    // 1/sqrt(numLoops) applied to the tracker's input as well as to the wet sum,
    // the same threshold names levels 10*log10(6) = 7.8 dB apart at
    // numLoops = 1 versus 6 - more than one 6 dB step of this sweep.
    std::vector<GovernorStepReading> oneLoop;
    {
        FeedbackEcology fe;
        makeReference(fe, kGovFs, 1);
        REQUIRE(fe.getNumLoops() == 1);
        oneLoop = runGovernorSweep(fe, kGovFs);
    }
    const float cross6 = crossingLevelDb(on);
    const float cross1 = crossingLevelDb(oneLoop);
    WARN("SC-006 (f): crossing level at numLoops = 6 is " << cross6 << " dBFS, at numLoops = 1 is "
                                                          << cross1 << " dBFS");
    INFO("(f) the sweep must actually cross at BOTH counts");
    REQUIRE(cross6 <= 0.0f);
    REQUIRE(cross1 <= 0.0f);
    INFO("(f) crossing levels " << cross6 << " and " << cross1 << " dBFS must agree within 1 dB");
    REQUIRE(std::abs(cross6 - cross1) <= 1.0f);

    // --- (c) the floor holds on a representative subset of SC-001's configs ----
    // SC-001 (c) draws every scalar uniformly from its clamped range; this is
    // eight of those draws plus the worst-case corner, each rendered 5 s under
    // the reference drive with getGovernorGain() sampled every block. The full
    // 256-configuration sweep lives in SC-001; what is asserted HERE is only
    // that the law's own floor, kGovernorMinGain, is never breached.
    {
        constexpr std::size_t kConfigs = 8;
        constexpr std::size_t kBlock = 512;
        const auto renderSamples = static_cast<std::size_t>(5.0 * kGovFs);
        std::vector<float> inL(kBlock, 0.0f);
        std::vector<float> inR(kBlock, 0.0f);
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);

        for (std::size_t cfg = 0; cfg <= kConfigs; ++cfg) {
            FeedbackEcology fe;
            makeReference(fe, kGovFs);

            if (cfg == 0) {
                // The worst-case corner (SC-001's definition), governor at its
                // defaults: the configuration that drives the tracker hardest.
                for (std::size_t i = 0; i < kLoops; ++i) {
                    fe.setLoopGain(i, FeedbackEcology::kMaxLoopGain);
                    fe.setLoopFilterQ(i, FeedbackEcology::kMaxFilterQ);
                    fe.setLoopResonanceRt60(i, 30.0f);
                    fe.setLoopDelayWander(i, FeedbackEcology::kMaxDelayWanderFraction);
                    fe.setLoopCutoffWander(i, FeedbackEcology::kMaxCutoffWanderOctaves);
                    for (std::size_t j = 0; j < kLoops; ++j) {
                        if (i == j) continue;
                        fe.setCoupling(i, j, FeedbackEcology::kMaxCouplingPerPair);
                    }
                }
                fe.setWanderRate(FeedbackEcology::kMaxWanderRateHz);
            } else {
                Krate::DSP::Xorshift32 cfgRng(0x00C0FFEEu + static_cast<std::uint32_t>(cfg));
                const auto uniform = [&cfgRng](float lo, float hi) {
                    return lo + (hi - lo) * cfgRng.nextUnipolar();
                };
                for (std::size_t i = 0; i < kLoops; ++i) {
                    fe.setLoopGain(
                        i, uniform(FeedbackEcology::kMinLoopGain, FeedbackEcology::kMaxLoopGain));
                    fe.setLoopFilterQ(
                        i, uniform(FeedbackEcology::kMinFilterQ, FeedbackEcology::kMaxFilterQ));
                    fe.setLoopDelayMs(
                        i, uniform(FeedbackEcology::kMinDelayMs, FeedbackEcology::kMaxDelayMs));
                    fe.setLoopResonanceRt60(i, uniform(0.1f, 30.0f));
                    fe.setLoopDelayWander(
                        i, uniform(0.0f, FeedbackEcology::kMaxDelayWanderFraction));
                    fe.setLoopCutoffWander(
                        i, uniform(0.0f, FeedbackEcology::kMaxCutoffWanderOctaves));
                    for (std::size_t j = 0; j < kLoops; ++j) {
                        if (i == j) continue;
                        fe.setCoupling(i, j, uniform(0.0f, FeedbackEcology::kMaxCouplingPerPair));
                    }
                }
                fe.setWanderRate(uniform(FeedbackEcology::kMinWanderRateHz,
                                         FeedbackEcology::kMaxWanderRateHz));
                fe.setGovernorThresholdDb(uniform(FeedbackEcology::kMinGovernorThresholdDb,
                                                  FeedbackEcology::kMaxGovernorThresholdDb));
                fe.setGovernorRatio(
                    uniform(FeedbackEcology::kMinGovernorRatio, FeedbackEcology::kMaxGovernorRatio));
            }
            fe.reset();

            Krate::DSP::Xorshift32 rng(kReferenceDriveSeed + static_cast<std::uint32_t>(cfg));
            float worst = 1.0f;
            std::size_t done = 0;
            while (done < renderSamples) {
                const std::size_t n = std::min(kBlock, renderSamples - done);
                for (std::size_t s = 0; s < n; ++s) {
                    inL[s] = rng.nextFloat() * kReferenceDriveAmplitude;
                    inR[s] = rng.nextFloat() * kReferenceDriveAmplitude;
                }
                fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
                worst = std::min(worst, fe.getGovernorGain());
                done += n;
            }
            INFO("(c) configuration " << cfg << ": worst governor gain " << worst);
            REQUIRE(Krate::DSP::detail::isFinite(worst));
            REQUIRE(worst >= FeedbackEcology::kGovernorMinGain);
            REQUIRE(worst <= 1.0f);
        }
    }

    // --- (e) no zipper on a 20 dB input step -----------------------------------
    // The drive is MONO-IDENTICAL for this arm (left == right) so that ONE zero
    // crossing serves both channels: the step is applied at a zero crossing of
    // the generator's output, which is what keeps the fixture's own
    // discontinuity out of the measurement. The analysis window still begins ONE
    // SAMPLE AFTER the step sample - at mix = 1 the reference patch passes the
    // drive straight into the wet path, so a window that includes the step
    // sample fires the detector on the fixture rather than on the governor.
    {
        const auto preSamples = static_cast<std::size_t>(3.0 * kGovFs);
        const auto postSamples = static_cast<std::size_t>(1.0 * kGovFs);
        const std::size_t total = preSamples + postSamples;

        std::vector<float> drive(total, 0.0f);
        Krate::DSP::Xorshift32 rng(kReferenceDriveSeed);
        for (std::size_t s = 0; s < total; ++s) drive[s] = rng.nextFloat();

        // The step: -30 dBFS RMS -> -10 dBFS RMS, exactly 20 dB.
        const float loAmp = sweepAmplitude(-30.0f);
        const float hiAmp = sweepAmplitude(-10.0f);

        // The zero crossing nearest the nominal step index: searched forward from
        // it to the first sign change. Uniform noise always has one within a
        // handful of samples, so the realised offset is far inside the 200 ms
        // window that follows.
        std::size_t stepIdx = preSamples;
        for (std::size_t k = 0; k < 64; ++k) {
            const std::size_t cand = preSamples + k;
            if (cand == 0 || cand + 1 >= total) break;
            const bool crossesUp = (drive[cand - 1] <= 0.0f && drive[cand] >= 0.0f);
            const bool crossesDown = (drive[cand - 1] >= 0.0f && drive[cand] <= 0.0f);
            if (crossesUp || crossesDown) {
                stepIdx = cand;
                break;
            }
        }

        std::vector<float> inL(total, 0.0f);
        std::vector<float> inR(total, 0.0f);
        for (std::size_t s = 0; s < total; ++s) {
            inL[s] = drive[s] * ((s < stepIdx) ? loAmp : hiAmp);
            inR[s] = inL[s];
        }
        std::vector<float> outL(total, 0.0f);
        std::vector<float> outR(total, 0.0f);

        FeedbackEcology fe;
        makeReference(fe, kGovFs);
        renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), total, 512);

        // The fixture must actually engage the governor, or the arm proves
        // nothing about zipper. Like the straddle above, this is a MEASURED
        // precondition: if it fails, the response is to re-measure the step's
        // levels against the recorded sweep - never to drop the arm.
        INFO("(e) governor gain after the step: " << fe.getGovernorGain());
        REQUIRE(fe.getGovernorGain() < 1.0f);

        ClickDetectorConfig cfg;
        cfg.sampleRate = static_cast<float>(kGovFs);  // the struct default is 44100
        ClickDetector detector(cfg);
        detector.prepare();

        const std::size_t windowStart = stepIdx + 1;
        const auto windowLen = static_cast<std::size_t>(0.2 * kGovFs);
        REQUIRE(windowStart + windowLen <= total);
        const std::vector<ClickDetection> hits =
            detector.detect(outL.data() + windowStart, windowLen);
        INFO("(e) " << hits.size() << " click detections in the 200 ms window from sample "
                    << windowStart << " (step at " << stepIdx << ")");
        REQUIRE(hits.empty());
    }
}

// ==============================================================================
// T013 - the loop life cycle: SC-014 (Dormancy) and SC-022 (loop count)
// ==============================================================================
// Both cases render minutes of audio TIME, so both stream: a small scratch block
// is reused and only the statistics survive. The one exception is SC-014 (c),
// which needs the six tap waveforms in memory to run ClickDetector over two
// 510 ms windows - 2.5 s x 6 taps = 2.9 MB, and the windows are cut out of that.
//
// WHY THE TWO HELPERS BELOW EXIST RATHER THAN renderBlocks(): renderBlocks needs
// the whole input AND the whole output materialised. A 90 s arm would be 17 MB
// per buffer, four of them, for samples that are never read.
// ==============================================================================

namespace {

/// Streams `total` samples of SILENCE through `fe`, discarding the output.
void renderSilenceBlocks(FeedbackEcology& fe, std::size_t total, std::size_t blockSize = 512) {
    std::vector<float> zeros(blockSize, 0.0f);
    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);
    std::size_t done = 0;
    while (done < total) {
        const std::size_t n = std::min(blockSize, total - done);
        fe.processBlock(zeros.data(), zeros.data(), outL.data(), outR.data(), n);
        done += n;
    }
}

/// Streams `total` samples of the reference drive through `fe`, discarding the
/// output. The drive is generated one sample at a time from a fresh Xorshift32,
/// so it is a pure function of `seed` and independent of `blockSize`.
void renderDriveBlocks(FeedbackEcology& fe, std::size_t total, std::size_t blockSize = 512,
                       std::uint32_t seed = kReferenceDriveSeed) {
    Krate::DSP::Xorshift32 rng(seed);
    std::vector<float> inL(blockSize, 0.0f);
    std::vector<float> inR(blockSize, 0.0f);
    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);
    std::size_t done = 0;
    while (done < total) {
        const std::size_t n = std::min(blockSize, total - done);
        for (std::size_t s = 0; s < n; ++s) {
            inL[s] = rng.nextFloat() * kReferenceDriveAmplitude;
            inR[s] = rng.nextFloat() * kReferenceDriveAmplitude;
        }
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
        done += n;
    }
}

}  // namespace

// ==============================================================================
// SC-014 - Dormancy behaves exactly as the cross-cutting rule requires
// ==============================================================================
// FLAT, no SECTIONs: Catch2 re-runs a TEST_CASE body once per leaf SECTION, and
// arm (d) alone renders 90.5 s of audio.
//
// The arms are ordered so the cheap structural ones fail first: (a) the two
// setters fold into one value, (e) the epsilon snap, (b) what a settled-dormant
// loop reports, (b2) that the lanes really advanced underneath it, (c) the wake
// ramp and both edges' click-freedom, (d) the FR-063 clear.
// ==============================================================================
TEST_CASE("FeedbackEcology_Dormancy", "[feedback_ecology]") {
    using Krate::DSP::TestUtils::ClickDetection;
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;
    using Krate::DSP::TestUtils::compareFingerprints;
    using Krate::DSP::TestUtils::fingerprintRender;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kFsI = 48000;
    constexpr std::size_t kSleeper = 2;  // 109 ms base delay, 600 Hz resonator centre

    // -------------------------------------------------------------------------
    // (a) setLoopDormant(i, true) and setLoopWake(i, 0.0f) produce THE SAME
    //     render - the Dormancy rule's core claim.
    //
    //     The configure -> reset() -> render order is mandatory: prepare() snaps
    //     the gate to gateSteady(), so a setter called after it GLIDES for 50 ms
    //     and the energy injected during those 50 ms then circulates for seconds.
    //     reset() snaps the gate and clears the audio in one call.
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kN = 5 * kFsI;  // 5 s
        std::vector<float> inL(kN, 0.0f);
        std::vector<float> inR(kN, 0.0f);
        fillReferenceDrive(inL.data(), inR.data(), kN);

        std::vector<float> dormL(kN, 0.0f);
        std::vector<float> dormR(kN, 0.0f);
        std::vector<float> wakeL(kN, 0.0f);
        std::vector<float> wakeR(kN, 0.0f);

        FeedbackEcology dormantFe;
        makeReference(dormantFe, kFs);
        dormantFe.setLoopDormant(kSleeper, true);
        dormantFe.reset();
        renderBlocks(dormantFe, inL.data(), inR.data(), dormL.data(), dormR.data(), kN, 512);

        FeedbackEcology wakeFe;
        makeReference(wakeFe, kFs);
        wakeFe.setLoopWake(kSleeper, 0.0f);
        wakeFe.reset();
        renderBlocks(wakeFe, inL.data(), inR.data(), wakeL.data(), wakeR.data(), kN, 512);

        // The two routes really are different configurations - only the
        // steady-state gate value they fold into is shared.
        REQUIRE(dormantFe.isLoopDormant(kSleeper));
        REQUIRE(dormantFe.getLoopWakeAmount(kSleeper) == 1.0f);
        REQUIRE_FALSE(wakeFe.isLoopDormant(kSleeper));
        REQUIRE(wakeFe.getLoopWakeAmount(kSleeper) == 0.0f);

        const auto cmpL = compareFingerprints(fingerprintRender(std::span<const float>(wakeL)),
                                              fingerprintRender(std::span<const float>(dormL)));
        INFO("(a) L: " << cmpL.detail << " worstMetric=" << cmpL.worstMetricRelativeError
                       << " worstSample=" << cmpL.worstSampleError);
        REQUIRE(cmpL.withinTolerance());
        const auto cmpR = compareFingerprints(fingerprintRender(std::span<const float>(wakeR)),
                                              fingerprintRender(std::span<const float>(dormR)));
        INFO("(a) R: " << cmpR.detail << " worstMetric=" << cmpR.worstMetricRelativeError
                       << " worstSample=" << cmpR.worstSampleError);
        REQUIRE(cmpR.withinTolerance());

        // STRONGER THAN THE CRITERION ASKS, and structurally so: gateSteady()
        // returns a literal 0.0f on both routes and nothing else in the object
        // reads `dormant` or `wakeAmount`, so the two renders are the SAME CODE
        // PATH from the same seed. Any difference at all is a real divergence,
        // not toolchain spread - which is why this one is exact.
        REQUIRE(countBitMismatches(dormL.data(), wakeL.data(), kN) == std::size_t{0});
        REQUIRE(countBitMismatches(dormR.data(), wakeR.data(), kN) == std::size_t{0});
    }

    // -------------------------------------------------------------------------
    // (e) setLoopWake(i, 1e-9f) snaps the GATE TARGET to exactly 0.0f while the
    //     getter still reports the caller's own 1e-9f. Without the
    //     kWakeSilenceEpsilon snap inside gateSteady() a Phase-8 release tail
    //     that stops at 1e-8 leaves a loop burning forever.
    // -------------------------------------------------------------------------
    {
        FeedbackEcology fe;
        makeReference(fe, kFs);
        fe.setLoopWake(kSleeper, 1.0e-9f);
        REQUIRE(fe.getLoopWakeAmount(kSleeper) == 1.0e-9f);  // stored VERBATIM
        REQUIRE(fe.isLoopEngineActive(kSleeper));            // still awake: the ramp is falling

        // 100 ms - the 50 ms gate fade plus the control step that runs the sleep
        // edge once the ramp has landed.
        renderDriveBlocks(fe, kFsI / 10);
        REQUIRE(fe.getLoopGate(kSleeper) == 0.0f);
        REQUIRE_FALSE(fe.isLoopEngineActive(kSleeper));
    }

    // -------------------------------------------------------------------------
    // (b) A settled-dormant loop: engine off and gate exactly zero, TARGETS
    //     still moving, REALISED values frozen. Two distinct claims, and the
    //     freeze has two distinct mechanisms - the delay tap cannot advance
    //     because read() is never called, and the cutoff cannot change because
    //     setCutoff() is never called.
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kAwake = 0;  // the control index: loop 0 stays awake
        FeedbackEcology fe;
        makeReference(fe, kFs);
        fe.setLoopDormant(kSleeper, true);
        fe.reset();

        REQUIRE_FALSE(fe.isLoopEngineActive(kSleeper));
        REQUIRE(fe.getLoopGate(kSleeper) == 0.0f);

        renderDriveBlocks(fe, kFsI);  // 1 s

        const float targetDelay0 = fe.getLoopTargetDelayMs(kSleeper);
        const float targetCutoff0 = fe.getLoopTargetCutoffHz(kSleeper);
        const float currentDelay0 = fe.getLoopCurrentDelayMs(kSleeper);
        const float currentCutoff0 = fe.getLoopCurrentCutoffHz(kSleeper);
        const float awakeCutoff0 = fe.getLoopCurrentCutoffHz(kAwake);

        renderDriveBlocks(fe, 20 * kFsI);  // 20 s more

        REQUIRE_FALSE(fe.isLoopEngineActive(kSleeper));
        REQUIRE(fe.getLoopGate(kSleeper) == 0.0f);

        // The lanes are alive, and the target getters are their only observable.
        INFO("(b) target delay " << targetDelay0 << " -> " << fe.getLoopTargetDelayMs(kSleeper)
                                 << " ms, target cutoff " << targetCutoff0 << " -> "
                                 << fe.getLoopTargetCutoffHz(kSleeper) << " Hz");
        REQUIRE(fe.getLoopTargetDelayMs(kSleeper) != targetDelay0);
        REQUIRE(fe.getLoopTargetCutoffHz(kSleeper) != targetCutoff0);

        // ...and the realised pair is frozen, BIT for bit.
        REQUIRE(floatBits(fe.getLoopCurrentDelayMs(kSleeper)) == floatBits(currentDelay0));
        REQUIRE(floatBits(fe.getLoopCurrentCutoffHz(kSleeper)) == floatBits(currentCutoff0));

        // THE CONTROL ARM. Without it "frozen" is indistinguishable from a dead
        // getter: an AWAKE loop's commanded cutoff is rewritten on every control
        // step and must have moved over the same 20 s.
        INFO("(b) awake control loop cutoff " << awakeCutoff0 << " -> "
                                              << fe.getLoopCurrentCutoffHz(kAwake) << " Hz");
        REQUIRE(fe.getLoopCurrentCutoffHz(kAwake) != awakeCutoff0);
    }

    // -------------------------------------------------------------------------
    // (b2) The lane advance is real, OBSERVATIONALLY: sleep a loop, hold 60 s of
    //      silence, wake it, and read the values it wakes with AFTER the FR-064
    //      snap. This arm depends on no getter being live while dormant.
    //
    //      THE FIXTURE USES MAXIMUM WANDER DEPTH, DELIBERATELY. The criterion is
    //      that the LANES ADVANCE while the loop sleeps; whether the DEFAULT
    //      depth table moves a delay far enough in 60 s is SC-005's question,
    //      and mixing the two would let a frozen-lane regression hide behind a
    //      shallow default. At maximum depth a frozen lane is the only way this
    //      arm can fail. Loop 5 carries the longest base delay (449 ms), so one
    //      FR-023 step is the smallest fraction of its mapped range.
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kTest = 5;
        FeedbackEcology fe;
        makeReference(fe, kFs);
        fe.setLoopDelayWander(kTest, FeedbackEcology::kMaxDelayWanderFraction);
        fe.setLoopCutoffWander(kTest, FeedbackEcology::kMaxCutoffWanderOctaves);
        fe.reset();

        renderDriveBlocks(fe, kFsI / 2);  // 0.5 s awake
        fe.setLoopDormant(kTest, true);
        renderDriveBlocks(fe, kFsI / 10);  // 100 ms: the fade, then the sleep edge
        REQUIRE_FALSE(fe.isLoopEngineActive(kTest));

        // Read AFTER the sleep edge, so these are the frozen values the loop
        // actually sleeps with rather than a reading taken mid-fade.
        const float sleptDelayMs = fe.getLoopCurrentDelayMs(kTest);
        const float sleptCutoffHz = fe.getLoopCurrentCutoffHz(kTest);
        REQUIRE(sleptCutoffHz > 0.0f);

        renderSilenceBlocks(fe, 60 * kFsI);  // 60 s asleep, input silent

        fe.setLoopDormant(kTest, false);  // the FR-064 snap happens IN THE SETTER
        REQUIRE(fe.isLoopEngineActive(kTest));
        const float wokeDelayMs = fe.getLoopCurrentDelayMs(kTest);
        const float wokeCutoffHz = fe.getLoopCurrentCutoffHz(kTest);

        // One FR-023 step is kCrossfadeThresholdSamples
        // (crossfading_delay_line.h:78) expressed in ms at the render rate -
        // 2.0833 ms at 48 kHz.
        const float oneStepMs =
            Krate::DSP::CrossfadingDelayLine::kCrossfadeThresholdSamples * 1000.0f
            / static_cast<float>(kFs);
        const float delayMoveMs = std::abs(wokeDelayMs - sleptDelayMs);
        const float cutoffMoveRel = std::abs(wokeCutoffHz - sleptCutoffHz) / sleptCutoffHz;
        WARN("SC-014 (b2): 60 s asleep moved loop 5's delay by "
             << delayMoveMs << " ms (one FR-023 step = " << oneStepMs << " ms) and its cutoff by "
             << (100.0f * cutoffMoveRel) << " % (" << sleptCutoffHz << " -> " << wokeCutoffHz
             << " Hz)");
        REQUIRE(delayMoveMs > oneStepMs);
        REQUIRE(cutoffMoveRel > 0.01f);
    }

    // -------------------------------------------------------------------------
    // (c) Wake re-entry is a 50 ms ramp, and BOTH edges are click-free WITH
    //     EVERY OFF-DIAGONAL COUPLING AT kMaxCouplingPerPair = 0.5. That fixture
    //     is the point: under the rejected pre-gate coupling reading a neighbour
    //     would see a 0.5-weighted step the instant the sleeping loop's gate
    //     settled at zero. The detector therefore runs on every loop's TAP,
    //     neighbours included, and not on the mixed output where six loops
    //     average one loop's discontinuity away.
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kSettle = kFsI;         // 1 s awake
        constexpr std::size_t kAsleep = kFsI;         // 1 s dormant
        constexpr std::size_t kAfterWake = kFsI / 2;  // 500 ms after the wake
        constexpr std::size_t kTotal = kSettle + kAsleep + kAfterWake;
        constexpr std::size_t kBlock = 64;            // resolves the 2 400-sample ramp
        constexpr std::size_t kPre = kFsI / 100;      // 10 ms of lead-in per window

        FeedbackEcology fe;
        makeReference(fe, kFs);
        for (std::size_t from = 0; from < kLoops; ++from) {
            for (std::size_t to = 0; to < kLoops; ++to) {
                if (from == to) continue;
                fe.setCoupling(from, to, FeedbackEcology::kMaxCouplingPerPair);
            }
        }
        fe.reset();

        std::vector<float> inL(kTotal, 0.0f);
        std::vector<float> inR(kTotal, 0.0f);
        fillReferenceDrive(inL.data(), inR.data(), kTotal);
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);

        std::array<std::vector<float>, kLoops> tapStore{};
        for (std::size_t i = 0; i < kLoops; ++i) {
            tapStore[i].assign(kTotal, 0.0f);
        }

        std::vector<float> gateHistory;
        gateHistory.reserve(kAfterWake / kBlock + 2);

        std::size_t done = 0;
        const auto renderTo = [&](std::size_t limit, bool recordGate) {
            while (done < limit) {
                const std::size_t n = std::min(kBlock, limit - done);
                std::array<float*, kLoops> taps{};
                for (std::size_t i = 0; i < kLoops; ++i) {
                    taps[i] = tapStore[i].data() + done;
                }
                fe.processBlockTapped(inL.data() + done, inR.data() + done, outL.data(),
                                      outR.data(), taps.data(), n);
                done += n;
                if (recordGate) gateHistory.push_back(fe.getLoopGate(kSleeper));
            }
        };

        renderTo(kSettle, false);
        REQUIRE(fe.getLoopGate(kSleeper) == 1.0f);
        REQUIRE(fe.isLoopEngineActive(kSleeper));

        fe.setLoopDormant(kSleeper, true);
        renderTo(kSettle + kAsleep, false);
        REQUIRE(fe.getLoopGate(kSleeper) == 0.0f);
        REQUIRE_FALSE(fe.isLoopEngineActive(kSleeper));

        fe.setLoopDormant(kSleeper, false);
        // FR-064: the wake edge is IN THE SETTER. A build that deferred it to
        // the next control step would leave the loop silent for up to 63 samples
        // while its gate was already rising - a <= 1.3 ms attack notch.
        REQUIRE(fe.isLoopEngineActive(kSleeper));
        renderTo(kTotal, true);

        // --- the ramp: monotone, and complete at 50 ms +/- one control chunk ---
        REQUIRE(gateHistory.size() > 1);
        for (std::size_t k = 1; k < gateHistory.size(); ++k) {
            INFO("(c) gate at sample " << (k * kBlock) << ": " << gateHistory[k - 1] << " -> "
                                       << gateHistory[k]);
            REQUIRE(gateHistory[k] >= gateHistory[k - 1]);
        }
        std::size_t completeAt = 0;
        for (std::size_t k = 0; k < gateHistory.size(); ++k) {
            if (gateHistory[k] == 1.0f) {
                completeAt = (k + 1) * kBlock;
                break;
            }
        }
        const auto expectedAt = static_cast<std::size_t>(
            static_cast<double>(FeedbackEcology::kGainRampMs) * 0.001 * kFs);  // 2 400
        INFO("(c) gate reached 1.0 after " << completeAt << " samples, expected " << expectedAt
                                           << " +/- " << FeedbackEcology::kControlChunkSamples);
        REQUIRE(completeAt != 0);
        REQUIRE(completeAt >= expectedAt - FeedbackEcology::kControlChunkSamples);
        REQUIRE(completeAt <= expectedAt + FeedbackEcology::kControlChunkSamples);

        // --- both edges, every tap --------------------------------------------
        ClickDetectorConfig cfg;
        cfg.sampleRate = static_cast<float>(kFs);  // the struct default is 44100
        ClickDetector detector(cfg);
        detector.prepare();

        struct EdgeWindow {
            const char* name;
            std::size_t start;
            std::size_t length;
        };
        const std::array<EdgeWindow, 2> windows{
            EdgeWindow{.name = "sleep", .start = kSettle - kPre, .length = kPre + kAfterWake},
            EdgeWindow{
                .name = "wake", .start = kSettle + kAsleep - kPre, .length = kPre + kAfterWake}};

        for (const EdgeWindow& w : windows) {
            REQUIRE(w.start + w.length <= kTotal);
            for (std::size_t i = 0; i < kLoops; ++i) {
                const std::vector<ClickDetection> hits =
                    detector.detect(tapStore[i].data() + w.start, w.length);
                INFO("(c) " << hits.size() << " detections on loop " << i << "'s tap across the "
                            << w.name << " edge (window from sample " << w.start << ")");
                REQUIRE(hits.empty());
            }
        }
    }

    // -------------------------------------------------------------------------
    // (d) THE SLEEP EDGE CLEARS THE LOOP (FR-063). Charge the loop with 30 s of
    //     drive, sleep it, hold 60 s of silence, then wake it with the input
    //     still silent. A build that only SKIPS the chain has a fully charged
    //     109 ms line and a ringing resonator waiting behind the gate and
    //     replays them here at full amplitude - failing by 60 dB or more.
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kTest = kSleeper;
        constexpr std::size_t kWindow = kFsI / 2;  // 500 ms
        constexpr std::size_t kBlock = 512;

        FeedbackEcology fe;
        makeReference(fe, kFs);
        renderDriveBlocks(fe, 30 * kFsI);  // charge

        fe.setLoopDormant(kTest, true);
        renderSilenceBlocks(fe, 60 * kFsI);
        REQUIRE_FALSE(fe.isLoopEngineActive(kTest));

        fe.setLoopDormant(kTest, false);  // wake, input STILL silent
        REQUIRE(fe.isLoopEngineActive(kTest));

        std::vector<float> zeros(kBlock, 0.0f);
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);
        std::vector<float> tap(kBlock, 0.0f);
        std::array<float*, kLoops> taps{};
        taps.fill(nullptr);
        taps[kTest] = tap.data();

        float peak = 0.0f;
        std::size_t nonFiniteSamples = 0;
        std::size_t done = 0;
        while (done < kWindow) {
            const std::size_t n = std::min(kBlock, kWindow - done);
            fe.processBlockTapped(zeros.data(), zeros.data(), outL.data(), outR.data(),
                                  taps.data(), n);
            for (std::size_t s = 0; s < n; ++s) {
                if (!Krate::DSP::detail::isFinite(tap[s])) {
                    ++nonFiniteSamples;
                    continue;
                }
                peak = std::max(peak, std::abs(tap[s]));
            }
            done += n;
        }

        const double peakDb = 20.0 * std::log10(static_cast<double>(peak) + 1.0e-12);
        WARN("SC-014 (d): the woken loop's tap peaked at "
             << peakDb << " dBFS over the first 500 ms (linear " << peak << ")");
        REQUIRE(nonFiniteSamples == std::size_t{0});
        REQUIRE(peakDb < -80.0);
    }
}

// ==============================================================================
// SC-022 - both loop counts the roadmap names are real
// ==============================================================================
// FLAT for the same reason as SC-014: the (a)/(b) pair renders 60 s per count.
//
// (b) IS THE ARM WITH TEETH. The wet RMS RATIO between five and six loops is
// REPORTED AND NEVER GATED - with six partially correlated loops the correct
// ratio lies anywhere between 0 dB (incoherent summing, where 1/sqrt(n) exactly
// cancels) and +0.79 dB (fully coherent), so any fixed band on it either misses
// a missing divisor or fails a correct build. The EXACT identity below has
// neither problem: a build that omits FR-017, divides by the awake count, or
// divides by kMaxLoops misses it by orders of magnitude more than
// kSampleTolerance.
// ==============================================================================
TEST_CASE("FeedbackEcology_LoopCount", "[feedback_ecology]") {
    using Krate::DSP::TestUtils::ClickDetection;
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kFsI = 48000;

    // -------------------------------------------------------------------------
    // (a) bounded at both counts, and (b) the FR-017 divisor, exactly.
    // -------------------------------------------------------------------------
    std::array<double, 2> wetRms{};
    const std::array<std::size_t, 2> counts{std::size_t{5}, std::size_t{6}};

    for (std::size_t k = 0; k < counts.size(); ++k) {
        const std::size_t loops = counts[k];

        FeedbackEcology fe;
        makeReference(fe, kFs, loops);
        REQUIRE(fe.getNumLoops() == loops);
        REQUIRE(fe.getNormalisationLoopCount() == loops);

        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kTotal = 60 * kFsI;  // 60 s corner render

        // makeReference's reset() SNAPPED both output-stage ramps, so these two
        // constants are exact for the whole render.
        const float wetTrim = Krate::DSP::dbToGain(0.0f);
        const float normGain = 1.0f / std::sqrt(static_cast<float>(loops));

        std::vector<float> inL(kBlock, 0.0f);
        std::vector<float> inR(kBlock, 0.0f);
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);
        std::array<std::vector<float>, kLoops> tapStore{};
        std::array<float*, kLoops> taps{};
        for (std::size_t i = 0; i < kLoops; ++i) {
            tapStore[i].assign(kBlock, 0.0f);
            taps[i] = tapStore[i].data();
        }

        Krate::DSP::Xorshift32 rng(kReferenceDriveSeed);
        double sumSquares = 0.0;
        double peak = 0.0;
        std::size_t nonFiniteSamples = 0;
        float worstReconstruction = 0.0f;
        std::size_t done = 0;
        while (done < kTotal) {
            const std::size_t n = std::min(kBlock, kTotal - done);
            for (std::size_t s = 0; s < n; ++s) {
                inL[s] = rng.nextFloat() * kReferenceDriveAmplitude;
                inR[s] = rng.nextFloat() * kReferenceDriveAmplitude;
            }
            fe.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(), taps.data(),
                                  n);
            for (std::size_t s = 0; s < n; ++s) {
                const float v = outL[s];
                if (!Krate::DSP::detail::isFinite(v) || !Krate::DSP::detail::isFinite(outR[s])) {
                    ++nonFiniteSamples;
                    continue;
                }
                const double d = static_cast<double>(v);
                sumSquares += d * d;
                peak = std::max(peak, std::abs(d));

                // SC-016's identity, evaluated at THIS count. At mix = 1 the
                // output channel IS the wet, and S6 step 5's order is normative:
                // trim the normalised sum, then clamp.
                float sum = 0.0f;
                for (std::size_t i = 0; i < kLoops; ++i) {
                    sum += tapStore[i][s];
                }
                float wet = wetTrim * (sum * normGain);
                wet = std::clamp(wet, -FeedbackEcology::kOutputClamp,
                                 FeedbackEcology::kOutputClamp);
                worstReconstruction = std::max(worstReconstruction, std::abs(wet - v));
            }
            done += n;
        }

        const double rms = std::sqrt(sumSquares / static_cast<double>(kTotal));
        wetRms[k] = rms;

        INFO("(a) numLoops = " << loops << ": peak " << peak << ", rms " << rms << ", clamps "
                               << fe.getClampEngagementCount() << ", non-finite resets "
                               << fe.getNonFiniteResetCount());
        REQUIRE(nonFiniteSamples == std::size_t{0});
        REQUIRE(peak < static_cast<double>(FeedbackEcology::kOutputClamp));
        REQUIRE(fe.getClampEngagementCount() == 0u);
        REQUIRE(fe.getNonFiniteResetCount() == 0u);
        REQUIRE(rms > 0.0);  // non-silent

        INFO("(b) numLoops = " << loops << ": worst |wet - out| = " << worstReconstruction);
        REQUIRE(worstReconstruction <= Krate::DSP::TestUtils::kSampleTolerance);
    }

    WARN("SC-022 (b): wet RMS 5 loops = "
         << wetRms[0] << ", 6 loops = " << wetRms[1]
         << ", ratio = " << (20.0 * std::log10(wetRms[1] / wetRms[0]))
         << " dB - REPORTED, NEVER GATED (the correct value lies anywhere in [0, +0.79] dB)");

    // -------------------------------------------------------------------------
    // (c) The mid-render count change is click-free. RUN TWICE: once on the
    //     default 0.04 ring and once with every off-diagonal pair pre-seeded at
    //     kMaxCouplingPerPair. The second fixture is the one with teeth - at
    //     0.04 an instantly-deleted coupling coefficient is a -28 dB event the
    //     detector can miss.
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kBefore = 2 * kFsI;
        constexpr std::size_t kAfter = kFsI;      // 1 s, of which 500 ms is asserted
        constexpr std::size_t kTotal = kBefore + kAfter;
        constexpr std::size_t kPre = kFsI / 100;  // 10 ms of lead-in
        constexpr std::size_t kDropped = 5;       // the loop numLoops = 5 excludes

        std::vector<float> inL(kTotal, 0.0f);
        std::vector<float> inR(kTotal, 0.0f);
        fillReferenceDrive(inL.data(), inR.data(), kTotal);

        for (int fixture = 0; fixture < 2; ++fixture) {
            const bool maxCoupling = (fixture == 1);

            FeedbackEcology fe;
            makeReference(fe, kFs);
            if (maxCoupling) {
                for (std::size_t from = 0; from < kLoops; ++from) {
                    for (std::size_t to = 0; to < kLoops; ++to) {
                        if (from == to) continue;
                        fe.setCoupling(from, to, FeedbackEcology::kMaxCouplingPerPair);
                    }
                }
            }
            fe.reset();

            std::vector<float> outL(kTotal, 0.0f);
            std::vector<float> outR(kTotal, 0.0f);
            renderBlocks(fe, inL.data(), inR.data(), outL.data(), outR.data(), kBefore, 512);
            REQUIRE(fe.getLoopGate(kDropped) == 1.0f);

            fe.setNumLoops(5);
            REQUIRE(fe.getNumLoops() == std::size_t{5});
            REQUIRE(fe.getNormalisationLoopCount() == std::size_t{5});

            renderBlocks(fe, inL.data() + kBefore, inR.data() + kBefore, outL.data() + kBefore,
                         outR.data() + kBefore, kAfter, 512);

            // FR-075: the dropped loop LEFT THROUGH THE GATE, exactly the way a
            // sleep is done - it was not cut out of the sum.
            REQUIRE(fe.getLoopGate(kDropped) == 0.0f);
            REQUIRE_FALSE(fe.isLoopEngineActive(kDropped));

            ClickDetectorConfig cfg;
            cfg.sampleRate = static_cast<float>(kFs);  // the struct default is 44100
            ClickDetector detector(cfg);
            detector.prepare();

            const std::size_t windowStart = kBefore - kPre;
            const std::size_t windowLen = kPre + kFsI / 2;
            REQUIRE(windowStart + windowLen <= kTotal);
            const std::vector<ClickDetection> hits =
                detector.detect(outL.data() + windowStart, windowLen);
            INFO("(c) " << (maxCoupling ? "max-coupling" : "default-ring") << " fixture: "
                        << hits.size()
                        << " detections across setNumLoops(6 -> 5) and the 500 ms after it");
            REQUIRE(hits.empty());
        }
    }
}

// ==============================================================================
// SC-007 - zero allocation after prepare(), across the WHOLE surface
// ==============================================================================
// prepare() is the component's ONLY allocating method (FR-005) and its footprint
// is six CrossfadingDelayLine ring buffers and nothing else (FR-081, FR-082).
// Every other public call - render, setter, reset(), setSeed(), setNumLoops(),
// the dormancy edges that fire clearLoopAudio() on the audio path,
// setCouplingMatrix(), and the tapped entry point - must leave the heap
// untouched. This case is the gate on that, and it is the only criterion in the
// phase that walks the entire setter surface.
//
// The clauses are not decoration: each names a place where a later change could
// reach for the heap and no other criterion would notice - a setter that grew a
// container, a mid-render count change that built a scratch table, a sleep edge
// re-applying a configuration through a temporary, a render whose block is 128x
// larger than the maxBlockSamples prepare() was given.
//
// FALSIFICATION: the counters below make every clause NON-VACUOUS. A walk that
// never moved the count, never changed the lane decimation, never fired a sleep
// or wake edge, never triggered a crossfade or never called reset() would still
// read zero allocations, and would prove nothing.
//
// NOTHING INSIDE THE SCOPE MAY ALLOCATE FOR REASONS OF ITS OWN, which rules out
// CAPTURE, INFO, REQUIRE and any container growth: every buffer is sized before
// the scope opens and every assertion is made after it closes.
//
// HOW THE COUNT IS READ. AllocationScope latches its count in its DESTRUCTOR
// (tests/test_helpers/allocation_detector.h:111-127), so its own accessors read
// 0 while the scope is open and are gone once it has closed. Both figures
// SC-007 names are therefore taken from the singleton the scope reports -
// AllocationDetector::instance().getAllocationCount() - while the scope is
// still open. That is the tree's idiom (resonance_drift_network_test.cpp:466-472,
// atmosphere_engine_test.cpp:2280).
//
// FREES. The shipped detector counts allocations only: the replaced operator
// delete is a bare free() with no counter
// (tests/test_helpers/allocation_operator_overrides.h:96-119), so "zero frees"
// has no direct observable. The proxy asserted here is getAllocatedBytes() -
// the six buffers' ownership, read every block: a free that released one of
// them would either be followed by an allocation (counted) or leave the
// reported footprint changed (asserted).
// ==============================================================================
TEST_CASE("FeedbackEcology_NoAllocation", "[feedback_ecology]") {
    using FilterMode = FeedbackEcology::FilterMode;

    constexpr std::size_t kBlocks = 1000;
    // The irregular sizes SC-007 names: below one control chunk, a prime that
    // never divides it, exactly one chunk, one short of a power of two, a power
    // of two, and two sizes far above the maxBlockSamples prepare() is given.
    constexpr std::array<std::size_t, 7> kSizes{1u, 7u, 64u, 511u, 512u, 4096u, 8193u};
    constexpr std::size_t kMaxSize = 8193;
    static_assert(kSizes[6] == kMaxSize, "the buffers below must hold the largest block");

    // ---- the setter surface at its extremes ---------------------------------
    // Each table is under its setter's minimum, at the minimum, far over the
    // maximum, and back at a sane interior value, so every clamp is crossed in
    // both directions many times over the walk. Every value is FINITE: this TU
    // compiles under /fp:fast and -ffast-math and the phase's conventions
    // confine non-finite arguments to feedback_ecology_nonfinite_test.cpp.
    constexpr std::array<FilterMode, 4> kModes{FilterMode::Lowpass, FilterMode::Bandpass,
                                               FilterMode::Highpass,
                                               // FR-011: an enumerator outside the three named
                                               // values is a silent no-op. The underlying type is
                                               // std::uint8_t, so the cast is well defined.
                                               static_cast<FilterMode>(7)};
    constexpr std::array<float, 4> kCutoffsHz{0.0f, FeedbackEcology::kMinCutoffHz, 1.0e9f, 1000.0f};
    constexpr std::array<float, 4> kFilterQs{0.0f, FeedbackEcology::kMinFilterQ, 1.0e9f, 0.5f};
    constexpr std::array<float, 4> kDelaysMs{0.0f, FeedbackEcology::kMinDelayMs, 1.0e9f, 250.0f};
    constexpr std::array<float, 4> kResonanceHz{0.0f, 1.0e9f, 55.0f, 4000.0f};
    constexpr std::array<float, 4> kRt60s{0.0f, 1.0e9f, 0.5f, 30.0f};
    constexpr std::array<float, 4> kLoopGains{-1.0f, FeedbackEcology::kMinLoopGain, 1.0e9f,
                                              FeedbackEcology::kMaxLoopGain};
    constexpr std::array<float, 4> kInputGains{-1.0f, 0.0f, 1.0e9f, 1.0f};
    constexpr std::array<float, 4> kCouplings{-1.0f, 0.0f, 1.0e9f,
                                              FeedbackEcology::kMaxCouplingPerPair};
    constexpr std::array<float, 4> kThresholdsDb{-1.0e9f,
                                                 FeedbackEcology::kMinGovernorThresholdDb, 1.0e9f,
                                                 FeedbackEcology::kDefaultGovernorThresholdDb};
    constexpr std::array<float, 4> kRatios{-1.0e9f, FeedbackEcology::kMinGovernorRatio, 1.0e9f,
                                           FeedbackEcology::kDefaultGovernorRatio};
    constexpr std::array<float, 4> kDelayWanders{-1.0f, 0.0f, 1.0e9f,
                                                 FeedbackEcology::kMaxDelayWanderFraction};
    constexpr std::array<float, 4> kCutoffWanders{-1.0f, 0.0f, 1.0e9f,
                                                  FeedbackEcology::kMaxCutoffWanderOctaves};
    // FR-055's decimation is ceil((1/rate) / BrownianDrift::kTauMax) clamped to
    // [1, kMaxLaneDecimation], so these rates span the mapping end to end and
    // the outer two are out of range on purpose, to cross the clamp.
    constexpr std::array<float, 6> kWanderRates{0.0f,
                                                FeedbackEcology::kMinWanderRateHz,
                                                FeedbackEcology::kDefaultWanderRateHz,
                                                0.5f,
                                                FeedbackEcology::kMaxWanderRateHz,
                                                1.0e9f};
    constexpr std::array<float, 4> kWakes{-1.0f, 0.0f, 1.0e9f, 1.0f};
    constexpr std::array<float, 4> kMixes{-1.0f, 0.0f, 1.0e9f, 1.0f};
    constexpr std::array<float, 4> kWetGainsDb{-1.0e9f, FeedbackEcology::kMinWetGainDb, 1.0e9f,
                                               FeedbackEcology::kMaxWetGainDb};
    // setNumLoops across [1, 6] as SC-007 names it, plus both ends of its clamp.
    constexpr std::array<std::size_t, 8> kLoopCounts{6u, 1u, 3u, 6u, 2u, 5u, 0u, 99u};

    // Both matrices are built BEFORE the scope: setCouplingMatrix takes a
    // reference, and hoisting them keeps the walk free of any construction.
    using CouplingMatrix =
        std::array<std::array<float, FeedbackEcology::kMaxLoops>, FeedbackEcology::kMaxLoops>;
    CouplingMatrix zeroMatrix{};
    CouplingMatrix maxMatrix{};
    for (auto& row : maxMatrix) {
        row.fill(FeedbackEcology::kMaxCouplingPerPair);
    }

    // ---- buffers, all sized before the scope opens --------------------------
    std::vector<float> inL(kMaxSize, 0.0f);
    std::vector<float> inR(kMaxSize, 0.0f);
    std::vector<float> outL(kMaxSize, 0.0f);
    std::vector<float> outR(kMaxSize, 0.0f);
    fillReferenceDrive(inL.data(), inR.data(), kMaxSize);

    std::array<std::vector<float>, kLoops> tapStore{};
    std::array<float*, kLoops> taps{};
    for (std::size_t i = 0; i < kLoops; ++i) {
        tapStore[i].assign(kMaxSize, 0.0f);
        taps[i] = tapStore[i].data();
    }

    // ---- prepare() OUTSIDE the scope ----------------------------------------
    // maxBlockSamples deliberately at its FLOOR while the walk renders blocks a
    // hundred and twenty-eight times larger: FR-085 says the field sizes
    // nothing, and a render that quietly grew a scratch buffer to fit 8 193
    // samples would show up here as an allocation rather than as a
    // correct-looking render.
    FeedbackEcology fe;
    fe.setSeed(0x5EEDu);
    fe.prepare(48000.0, FeedbackEcology::PrepareConfig{
                            .maxBlockSamples = 64, .numLoops = FeedbackEcology::kMaxLoops});
    REQUIRE(fe.getMaxBlockSamples() == std::size_t{64});
    const std::size_t bytesAfterPrepare = fe.getAllocatedBytes();
    REQUIRE(bytesAfterPrepare > std::size_t{0});

    // ---- evidence, all collected without allocating -------------------------
    std::size_t allocations = 0;
    std::size_t loopCountMoves = 0;
    std::size_t decimationChanges = 0;
    std::size_t widestDecimation = 0;
    std::size_t narrowestDecimation = 1000u;
    std::size_t sleepEdges = 0;
    std::size_t wakeEdges = 0;
    std::array<bool, kLoops> sawSleepEdge{};
    std::array<bool, kLoops> sawWakeEdge{};
    std::array<bool, kLoops> engineWas{};
    std::size_t resets = 0;
    std::size_t seedCalls = 0;
    std::size_t tappedCalls = 0;
    std::size_t plainCalls = 0;
    std::uint32_t maxCrossfadeSum = 0;
    bool renderedFinite = true;
    bool footprintStable = true;

    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        std::size_t previousLoopCount = fe.getNumLoops();
        std::size_t previousDecimation = fe.getLaneDecimation();
        for (std::size_t i = 0; i < kLoops; ++i) {
            engineWas[i] = fe.isLoopEngineActive(i);
        }

        for (std::size_t b = 0; b < kBlocks; ++b) {
            const std::size_t n = kSizes[b % kSizes.size()];

            // ---- the setter surface, in DECLARATION ORDER -------------------
            fe.setNumLoops(kLoopCounts[b % kLoopCounts.size()]);
            for (std::size_t i = 0; i < kLoops; ++i) {
                fe.setLoopFilterMode(i, kModes[(b + i) % kModes.size()]);
                fe.setLoopCutoffHz(i, kCutoffsHz[(b + i) % kCutoffsHz.size()]);
                fe.setLoopFilterQ(i, kFilterQs[(b + i) % kFilterQs.size()]);
                fe.setLoopDelayMs(i, kDelaysMs[(b + i) % kDelaysMs.size()]);
                fe.setLoopResonanceHz(i, kResonanceHz[(b + i) % kResonanceHz.size()]);
                fe.setLoopResonanceRt60(i, kRt60s[(b + i) % kRt60s.size()]);
                fe.setLoopGain(i, kLoopGains[(b + i) % kLoopGains.size()]);
                fe.setLoopInputGain(i, kInputGains[(b + i) % kInputGains.size()]);
            }
            for (std::size_t from = 0; from < kLoops; ++from) {
                for (std::size_t to = 0; to < kLoops; ++to) {
                    fe.setCoupling(from, to, kCouplings[(b + from + to) % kCouplings.size()]);
                }
            }
            // The whole-matrix setter, alternating between the two extremes so
            // it is not merely re-writing what setCoupling just wrote.
            fe.setCouplingMatrix((b % 2u == 0u) ? zeroMatrix : maxMatrix);
            fe.setGovernorThresholdDb(kThresholdsDb[b % kThresholdsDb.size()]);
            fe.setGovernorRatio(kRatios[b % kRatios.size()]);
            for (std::size_t i = 0; i < kLoops; ++i) {
                fe.setLoopDelayWander(i, kDelayWanders[(b + i) % kDelayWanders.size()]);
                fe.setLoopCutoffWander(i, kCutoffWanders[(b + i) % kCutoffWanders.size()]);
            }
            fe.setWanderRate(kWanderRates[b % kWanderRates.size()]);
            fe.setWanderEnabled((b % 3u) != 0u);

            // ---- FR-063/FR-064's dormancy edges, walked across all six loops -
            // One loop per block, its state flipped every sixth block, so each
            // loop sees both edge kinds many times and the sleep-edge
            // clearLoopAudio() runs on every slot. Six blocks average ~11 000
            // samples here, comfortably past the kGainRampMs = 50 ms gate ramp,
            // so the edges actually settle rather than being re-targeted
            // mid-fade forever.
            const std::size_t edgeLoop = b % kLoops;
            const std::size_t dormancyCycle = b / kLoops;  // six blocks long
            const bool dormant = (dormancyCycle % 2u) == 0u;
            // THE WAKE VALUE IS INDEXED BY THE DORMANCY CYCLE AND THE LOOP, NOT
            // BY THE RAW BLOCK INDEX, and that is load-bearing rather than
            // taste. Loop i leaves dormancy at b = 6k + i with k odd, i.e. at
            // b congruent to (6 + i) mod 12, so kWakes[b % kWakes.size()] hands
            // loop i the SAME entry, (2 + i) mod 4, on every one of its wake
            // blocks for the whole walk. For loops 2 and 3 that entry is -1.0f
            // or 0.0f, and setLoopWake clamps both to a wake amount of zero -
            // which IS dormancy (FR-060; gateSteady() snaps anything at or
            // under kWakeSilenceEpsilon to a literal 0.0f). Those two slots
            // could therefore never satisfy refreshGates() target != 0.0f test,
            // never woke, and sawWakeEdge[2] / sawWakeEdge[3] below were
            // unreachable. A table of period 4 against a dormancy period of 12
            // is the aliasing; folding in the cycle counter and the loop index
            // breaks it, and every slot now sees all four entries, both clamp
            // ends included, and wakes tens of times across the walk.
            fe.setLoopWake(edgeLoop, dormant
                                         ? 0.0f
                                         : kWakes[(dormancyCycle + edgeLoop) % kWakes.size()]);
            fe.setLoopDormant(edgeLoop, dormant);

            fe.setMix(kMixes[b % kMixes.size()]);
            fe.setWetGain(kWetGainsDb[b % kWetGainsDb.size()]);

            // ---- the lifecycle calls SC-007 names ---------------------------
            if ((b % 397u) == 396u) {
                fe.reset();
                ++resets;
            }
            if ((b % 251u) == 250u) {
                fe.setSeed(0x5EEDu + static_cast<std::uint32_t>(b));
                ++seedCalls;
            }

            // ---- both entry points. processBlock FORWARDS to
            // processBlockTapped with loopTaps = nullptr (FR-073, one function
            // body), so the 1 000 render calls below are 1 000 processBlock
            // calls in every sense that matters, and half of them additionally
            // exercise the six tap writes.
            if ((b % 2u) == 0u) {
                fe.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(),
                                      taps.data(), n);
                ++tappedCalls;
            } else {
                fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
                ++plainCalls;
            }

            for (std::size_t s = 0; s < n; ++s) {
                if (!Krate::DSP::detail::isFinite(outL[s])
                    || !Krate::DSP::detail::isFinite(outR[s])) {
                    renderedFinite = false;
                }
            }

            // ---- the evidence, read back through the public surface ---------
            if (fe.getAllocatedBytes() != bytesAfterPrepare) footprintStable = false;

            const std::size_t loopCount = fe.getNumLoops();
            if (loopCount != previousLoopCount) {
                ++loopCountMoves;
                previousLoopCount = loopCount;
            }
            const std::size_t decimation = fe.getLaneDecimation();
            if (decimation != previousDecimation) {
                ++decimationChanges;
                previousDecimation = decimation;
            }
            widestDecimation = std::max(widestDecimation, decimation);
            narrowestDecimation = std::min(narrowestDecimation, decimation);

            for (std::size_t i = 0; i < kLoops; ++i) {
                const bool active = fe.isLoopEngineActive(i);
                if (active != engineWas[i]) {
                    if (active) {
                        ++wakeEdges;
                        sawWakeEdge[i] = true;
                    } else {
                        ++sleepEdges;
                        sawSleepEdge[i] = true;
                    }
                    engineWas[i] = active;
                }
            }

            // reset() zeroes the per-loop crossfade counters, so the evidence is
            // the largest sum SEEN, not the final reading.
            std::uint32_t crossfadeSum = 0;
            for (std::size_t i = 0; i < kLoops; ++i) {
                crossfadeSum += fe.getLoopCrossfadeCount(i);
            }
            maxCrossfadeSum = std::max(maxCrossfadeSum, crossfadeSum);
        }

        // Read while the scope is still OPEN: AllocationScope latches its own
        // count in its destructor (allocation_detector.h:111-127).
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    // ---- the criterion ------------------------------------------------------
    const bool hadAllocations = allocations > 0u;  // AllocationScope::hadAllocations()
    CAPTURE(allocations, resets, seedCalls, tappedCalls, plainCalls, loopCountMoves,
            decimationChanges, widestDecimation, narrowestDecimation, sleepEdges, wakeEdges,
            maxCrossfadeSum);
    REQUIRE_FALSE(hadAllocations);
    REQUIRE(allocations == std::size_t{0});

    // The "zero frees" half, through the only observable there is: the six
    // buffers were never released and re-taken.
    REQUIRE(footprintStable);
    REQUIRE(fe.getAllocatedBytes() == bytesAfterPrepare);

    // ---- non-vacuity: the walk really did every clause ----------------------
    REQUIRE(renderedFinite);
    REQUIRE(tappedCalls + plainCalls == kBlocks);
    REQUIRE(tappedCalls > std::size_t{0});
    REQUIRE(plainCalls > std::size_t{0});
    REQUIRE(resets > std::size_t{0});
    REQUIRE(seedCalls > std::size_t{0});
    REQUIRE(loopCountMoves > std::size_t{0});
    REQUIRE(decimationChanges > std::size_t{0});
    REQUIRE(widestDecimation == FeedbackEcology::kMaxLaneDecimation);
    REQUIRE(narrowestDecimation == std::size_t{1});
    REQUIRE(sleepEdges > std::size_t{0});
    REQUIRE(wakeEdges > std::size_t{0});
    REQUIRE(maxCrossfadeSum > std::uint32_t{0});
    for (std::size_t i = 0; i < kLoops; ++i) {
        INFO("loop " << i);
        REQUIRE(sawSleepEdge[i]);
        REQUIRE(sawWakeEdge[i]);
    }
}

// =============================================================================
// FR-042 rung 2 under OQ-2 LEVER 1 - fastTanh preserves the bound
// =============================================================================
// FR-080 pre-authorises "replace std::tanh with the shipped FastMath
// alternative UNDER ITS OWN ERROR-BOUND TEST". This is that test, and it is
// written against what rung 2 actually OWES the boundedness ladder rather than
// against a similarity-to-std::tanh feeling:
//
//   (a) THE BOUND. FR-042 says the clip bounds |y_i| <= 1 UNCONDITIONALLY,
//       "whatever a future coefficient change, sample-rate extreme or
//       arithmetic surprise does to rung 1". That is the only property rungs 3
//       and 4 are sized against (DERIVATION TABLE 4 static_asserts
//       kMaxLoops/sqrt(kMaxLoops) < kOutputClamp on exactly this bound), so it
//       is asserted over the whole finite float range, extremes included.
//   (b) MONOTONE, ODD, AND ZERO-PRESERVING. A wave-folder would also satisfy
//       (a) and would turn rung 2 into a distortion generator; fastTanh(0)
//       must be exactly 0.0f or a silent loop would not stay silent.
//   (c) THE ERROR. Measured against std::tanh over the range the loop can
//       reach, and pinned at a MEASURED figure - printed, so a later reader
//       re-derives it instead of trusting this comment.
//
// FR-042 also says the clip "is a bound, not a tone control: at the levels
// FR-018 default gain produces, tanh is within 2 % of linear" - so the arm that
// matters most for tone is the error at SMALL |x|, which is where the Pade
// quotient is exact to several digits.
// =============================================================================
TEST_CASE("FeedbackEcology_FastTanhErrorBound", "[feedback_ecology]") {
    // The measured ceilings. Both are RECORDED MEASUREMENTS printed by this
    // case, not guesses: the absolute error peaks where fastTanh saturates to
    // exactly 1 at |x| = 3.5 while std::tanh is still 0.9981779, i.e. 1.822e-3,
    // and the relative error inside the loop working range is far smaller.
    constexpr float kMaxAbsError            = 2.0e-3f;
    constexpr float kMaxRelErrorSmallSignal = 1.0e-6f;
    /// |b * govGain| at kOutputClamp is 4; the sweep goes far past it.
    constexpr float kSweepLimit = 64.0f;
    constexpr int   kSweepSteps = 400000;

    float maxAbsError    = 0.0f;
    float maxAbsErrorAt  = 0.0f;
    float maxRelSmall    = 0.0f;
    float maxRelSmallAt  = 0.0f;
    float maxMagnitude   = 0.0f;
    bool  monotone       = true;
    bool  oddSymmetric   = true;
    float prev           = -2.0f;

    for (int k = 0; k <= kSweepSteps; ++k) {
        const float x =
            -kSweepLimit + 2.0f * kSweepLimit * (static_cast<float>(k) / static_cast<float>(kSweepSteps));
        const float y = Krate::DSP::FastMath::fastTanh(x);

        // (a) the bound, on every sample of the sweep
        maxMagnitude = std::max(maxMagnitude, std::abs(y));

        // (b) monotone non-decreasing and odd
        if (y < prev) monotone = false;
        prev = y;
        const float yNeg = Krate::DSP::FastMath::fastTanh(-x);
        if (std::abs(yNeg + y) > 1.0e-7f) oddSymmetric = false;

        // (c) the error against the reference
        const float ref = std::tanh(x);
        const float err = std::abs(y - ref);
        if (err > maxAbsError) {
            maxAbsError   = err;
            maxAbsErrorAt = x;
        }
        if (std::abs(x) <= 1.0f && std::abs(ref) > 1.0e-6f) {
            const float rel = err / std::abs(ref);
            if (rel > maxRelSmall) {
                maxRelSmall   = rel;
                maxRelSmallAt = x;
            }
        }
    }

    WARN("FR-042 / OQ-2 lever 1: fastTanh over [-"
         << kSweepLimit << ", " << kSweepLimit << "] in " << kSweepSteps << " steps\n"
         << "  max |fastTanh| = " << maxMagnitude << " (bound "
         << FeedbackEcology::kSoftClipCeiling << ")\n"
         << "  max |fastTanh - std::tanh| = " << maxAbsError << " at x = " << maxAbsErrorAt
         << " (ceiling " << kMaxAbsError << ")\n"
         << "  max relative error for |x| <= 1 = " << maxRelSmall << " at x = " << maxRelSmallAt
         << " (ceiling " << kMaxRelErrorSmallSignal << ")");

    // --- (a) THE BOUND, including the extremes the sweep cannot reach --------
    INFO("(a) max |fastTanh| over the sweep = " << maxMagnitude);
    REQUIRE(maxMagnitude <= FeedbackEcology::kSoftClipCeiling);
    REQUIRE(std::abs(Krate::DSP::FastMath::fastTanh(3.4999f)) <= FeedbackEcology::kSoftClipCeiling);
    REQUIRE(std::abs(Krate::DSP::FastMath::fastTanh(3.5f)) <= FeedbackEcology::kSoftClipCeiling);
    REQUIRE(std::abs(Krate::DSP::FastMath::fastTanh(1.0e30f)) <= FeedbackEcology::kSoftClipCeiling);
    REQUIRE(std::abs(Krate::DSP::FastMath::fastTanh(-1.0e30f)) <= FeedbackEcology::kSoftClipCeiling);
    REQUIRE(std::abs(Krate::DSP::FastMath::fastTanh(std::numeric_limits<float>::max()))
            <= FeedbackEcology::kSoftClipCeiling);

    // --- (b) shape ------------------------------------------------------------
    INFO("(b) fastTanh must be monotone non-decreasing - a fold would make rung 2 a distortion "
         "generator rather than a limiter");
    REQUIRE(monotone);
    REQUIRE(oddSymmetric);
    REQUIRE(Krate::DSP::FastMath::fastTanh(0.0f) == 0.0f);

    // --- (c) the measured error ------------------------------------------------
    INFO("(c) max |fastTanh - std::tanh| = " << maxAbsError << " at x = " << maxAbsErrorAt);
    REQUIRE(maxAbsError <= kMaxAbsError);
    INFO("(c) max relative error for |x| <= 1 = " << maxRelSmall);
    REQUIRE(maxRelSmall <= kMaxRelErrorSmallSignal);

    // --- the CONSEQUENCE for the component, not only for the function --------
    // FR-042 bounds the wet sum at sqrt(numLoops) BEFORE the FR-072 trim, and
    // DERIVATION TABLE 4 static_asserts that this sits under kOutputClamp. The
    // arithmetic is worth restating as a runtime check so a change to either
    // constant is caught here as well as at compile time.
    REQUIRE(static_cast<float>(FeedbackEcology::kMaxLoops) * FeedbackEcology::kSoftClipCeiling
                / FeedbackEcology::kSqrtMaxLoops
            < FeedbackEcology::kOutputClamp);
}

