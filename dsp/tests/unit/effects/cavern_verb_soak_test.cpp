// ==============================================================================
// Layer 4: Effect Tests - CavernVerb, 30-minute soak
//                                        (specs/vorago-phase9-cavern-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase9-cavern-space/spec.md   (SC-014, FR-028, FR-063)
//            specs/vorago-phase9-cavern-space/plan.md   (S10.1, S10.4, R-13)
//            specs/vorago-phase9-cavern-space/tasks.md  (T001 creates this TU;
//                                                        T015 fills it)
//
// SCOPE OF THIS TU: SC-014 only. Its real case carries the [long] tag, so it is
//   excluded from the per-push lane and runs nightly; the wiring scaffold below
//   is deliberately untagged so T001's case-count check sees it.
//
// NEVER include <allocation_operator_overrides.h> here: the global operator
//   new/delete replacement for this image already lives in
//   dsp/tests/unit/effects/aether_reverb_test.cpp; a second include is a
//   duplicate-symbol link error. Use <allocation_detector.h> only.
//
// NON-FINITE DETECTION: never std::isnan / std::isinf / std::isfinite. The
//   macOS leg builds with -ffast-math, under which those fold to `false`.
//   detail::isNaN / detail::isInf inspect the IEEE-754 exponent field behind an
//   opaque barrier instead (core/db_utils.h:99, :260).
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <reverb_metrics.h>  // G-2 as a STREAM (NoiseState / fillBandLimitedNoise)

#include <krate/dsp/core/db_utils.h>        // detail::isNaN / detail::isInf
#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/primitives/smoother.h>  // ITERUM_NOINLINE

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using Krate::DSP::CavernVerb;

namespace TestUtils = Krate::DSP::TestUtils;

namespace {

// -----------------------------------------------------------------------------
// Render geometry
// -----------------------------------------------------------------------------

constexpr double kSampleRate48 = 48000.0;

/// 480 samples, chosen so ONE SECOND is exactly 100 blocks and one minute exactly
/// 6000 blocks: every per-second energy sample and every per-minute RMS window
/// lands on a block boundary with no partial-block bookkeeping. 480 is not a
/// multiple of the 64-sample control chunk, so the render also exercises the
/// absolute-grid slicing (FR-007) rather than sitting on it.
constexpr std::size_t kBlockSamples = 480;
constexpr std::size_t kSecondSamples = 48000;
constexpr std::size_t kBlocksPerSecond = kSecondSamples / kBlockSamples;  // 100
constexpr std::size_t kSecondsPerMinute = 60;
constexpr std::size_t kSoakMinutes = 30;
constexpr std::size_t kSoakSeconds = kSoakMinutes * kSecondsPerMinute;  // 1800

static_assert(kBlocksPerSecond * kBlockSamples == kSecondSamples,
              "one second must be a whole number of blocks");

/// Independent G-2 streams for the two input channels.
constexpr std::uint32_t kNoiseSeedLeft = 0x51A0C0DEu;
constexpr std::uint32_t kNoiseSeedRight = 0x7E5730B1u;

/// SC-014 clause (a). Derivation (FR-028 + FR-063), so it can be CHECKED rather
/// than trusted: the ER stage is feed-forward, so its contribution is bounded by
/// Sum|g_i| = kEarlyGainSum = 1.865762 <= 2.0 times the input magnitude; the
/// owned engine's loop gain is <= 1.0 outside freeze (re-established for every
/// admissible damper offset by FR-045 / FR-048); and CavernVerb's equal-power
/// dry/wet law has unit maximum gain. With |input| <= 1.0 that is 2.0 for the ER
/// plus an order-unity late field, inside 4.0 with margin.
///
/// A MEASUREMENT ABOVE THIS IS A DEFECT TO FIX, NEVER AN OCCASION TO RE-DERIVE
/// THE BOUND (FR-082).
constexpr double kPeakBound = 4.0;
static_assert(static_cast<double>(CavernVerb::kEarlyGainSum) <= 2.0,
              "SC-014 (a)'s derivation assumes the ER gain sum is within 2.0");

/// SC-014 clause (b): minute 30 against minute 10.
constexpr double kDriftBoundDb = 3.0;

/// SC-014 clause (d) / SC-001 clause 1: frozen state-energy conservation.
constexpr double kFreezeConservationDb = 0.5;

/// Clause (d): the freeze is entered here, and the reference energy is taken one
/// further second later so the 50 ms latch (aether_reverb.h:1388,
/// kFreezeLatchMs) is long finished.
constexpr std::size_t kFreezeEntrySecond = 60;
constexpr std::size_t kFreezeSettleSeconds = 1;

// -----------------------------------------------------------------------------
// Small numeric helpers
// -----------------------------------------------------------------------------

/// @brief Finiteness without <cmath>'s classifiers (FR-071).
[[nodiscard]] ITERUM_NOINLINE bool soakFinite(float v) noexcept {
    return !Krate::DSP::detail::isNaN(v) && !Krate::DSP::detail::isInf(v);
}

/// @brief getStateEnergy() is a SUM OF SQUARES, so it converts on the 10*log10
///        law - the same form as aether_reverb_test.cpp:458-460.
[[nodiscard]] double energyToDb(float energy) {
    return 10.0 * std::log10(std::max(static_cast<double>(energy), 1e-300));
}

/// @brief Length of the longest strictly increasing OR strictly decreasing run.
///
/// Clause (b) asks for "no strictly monotone run of length 30" over the 30
/// per-minute figures, i.e. the sequence as a whole neither grows nor collapses
/// monotonically. Written as the law, so the returned figure can be reported.
[[nodiscard]] std::size_t longestStrictlyMonotoneRun(const std::vector<double>& v) {
    if (v.empty()) {
        return 0u;
    }
    std::size_t bestUp = 1u;
    std::size_t bestDown = 1u;
    std::size_t runUp = 1u;
    std::size_t runDown = 1u;
    for (std::size_t i = 1; i < v.size(); ++i) {
        runUp = (v[i] > v[i - 1u]) ? (runUp + 1u) : 1u;
        runDown = (v[i] < v[i - 1u]) ? (runDown + 1u) : 1u;
        bestUp = std::max(bestUp, runUp);
        bestDown = std::max(bestDown, runDown);
    }
    return std::max(bestUp, bestDown);
}

// -----------------------------------------------------------------------------
// SC-009 arm (b): the worst parameter combination (plan S10.4, SC-009 row)
// -----------------------------------------------------------------------------

/// @brief Arm (b)'s PREPARE-time fields: N = 16, spectral diffusion at 4096, and
///        maxEarlySeconds = 0.60 so setEarlySizeMs(600) is actually reachable
///        (under the 0.30 default it would clamp to 300 ms).
[[nodiscard]] CavernVerb::PrepareConfig armBConfig() {
    return CavernVerb::PrepareConfig{.numChannels = 16u,
                                     .maxBlockSamples = 512u,
                                     .maxEarlySeconds = 0.60f,
                                     .maxDelaySeconds = 0.50f,
                                     .spectralDiffusionEnabled = true,
                                     .diffusionFftSize = 4096u,
                                     .seed = 1u};
}

/// @brief Every one of the seventeen controls set EXPLICITLY: the FR-066 default
///        where arm (b) does not override it, arm (b)'s maximum where it does.
///
/// Spelling the defaults out (plan S10.3's rule for the shared fixtures) means a
/// future default change breaks this configuration loudly instead of silently
/// moving what the soak measures.
void applyArmBControls(CavernVerb& cav) {
    // arm (b) overrides
    cav.setSize(1.0f);
    cav.setFog(1.0f);
    cav.setBreath(1.0f);
    cav.setEarlyLevel(1.0f);
    cav.setEarlySend(1.0f);
    cav.setEarlySizeMs(CavernVerb::kEarlySizeMaxMs);
    cav.setDamperDepth(1.0f);
    cav.setDamperRate(1.0f);

    // FR-066 defaults, applied explicitly
    cav.setDarkness(CavernVerb::kDefaultDarkness);
    cav.setDecaySeconds(CavernVerb::kDefaultDecaySeconds);
    cav.setDensity(CavernVerb::kDefaultDensity);
    cav.setDimensionality(CavernVerb::kDefaultDimensionality);
    cav.setEarlyAbsorption(CavernVerb::kDefaultEarlyAbsorption);
    cav.setWidth(CavernVerb::kDefaultWidth);
    cav.setMix(CavernVerb::kDefaultMix);
    cav.setFreeze(false);
}

// -----------------------------------------------------------------------------
// G-2 as a continuously generated stream (P-3), never a looped buffer
// -----------------------------------------------------------------------------

/// @brief The two input streams plus the fixed gain that brings them up to the
///        unity peak SC-014's derivation assumes.
///
/// A STREAMING generator cannot be peak-normalised after the fact - it does not
/// know the peak of a render it has not produced yet - and normalising per block
/// would put a seam at every block boundary. So the gain is calibrated ONCE, on a
/// short pre-pass over the same seeds, and every sample is then hard-clamped into
/// [-1, 1]. The clamp is what makes "|x| <= 1.0" a guarantee rather than a hope:
/// a 30-minute stream reaches further into the Gaussian tail than any pre-pass,
/// and the peak bound of clause (a) is only meaningful if the input honours its
/// side of the derivation.
struct NoiseStream {
    TestUtils::NoiseState left;
    TestUtils::NoiseState right;
    std::uint64_t position = 0u;
    float gain = 1.0f;
};

/// Calibration pre-pass length. Long enough to see a representative peak, short
/// enough to be free next to a 30-minute render (no reverb runs here).
constexpr std::size_t kCalibrationSeconds = 10;

/// Target peak for the calibrated stream, just under unity so the clamp only has
/// to catch the tail beyond the pre-pass, not shave the bulk of the signal.
constexpr float kCalibrationTargetPeak = 0.98f;

/// Sanity cap on the calibration gain - a degenerate (near-silent) pre-pass must
/// not turn into an absurd multiplier.
constexpr float kCalibrationGainCap = 8.0f;

[[nodiscard]] float calibrateNoiseGain(double sampleRate) {
    TestUtils::NoiseState probeLeft;
    TestUtils::NoiseState probeRight;
    std::vector<float> a(kBlockSamples, 0.0f);
    std::vector<float> b(kBlockSamples, 0.0f);

    float peak = 0.0f;
    std::uint64_t pos = 0u;
    const std::size_t blocks = kCalibrationSeconds * kBlocksPerSecond;
    for (std::size_t n = 0; n < blocks; ++n) {
        TestUtils::fillBandLimitedNoise(std::span<float>(a), sampleRate, kNoiseSeedLeft, pos,
                                        probeLeft);
        TestUtils::fillBandLimitedNoise(std::span<float>(b), sampleRate, kNoiseSeedRight, pos,
                                        probeRight);
        for (std::size_t i = 0; i < kBlockSamples; ++i) {
            peak = std::max(peak, std::fabs(a[i]));
            peak = std::max(peak, std::fabs(b[i]));
        }
        pos += kBlockSamples;
    }

    if (!(peak > 1.0e-6f)) {
        return 1.0f;
    }
    return std::min(kCalibrationTargetPeak / peak, kCalibrationGainCap);
}

/// @brief Fill one block from the continuing stream, gained and clamped to unity.
void nextNoiseBlock(NoiseStream& stream, double sampleRate, std::vector<float>& outLeft,
                    std::vector<float>& outRight) {
    TestUtils::fillBandLimitedNoise(std::span<float>(outLeft), sampleRate, kNoiseSeedLeft,
                                    stream.position, stream.left);
    TestUtils::fillBandLimitedNoise(std::span<float>(outRight), sampleRate, kNoiseSeedRight,
                                    stream.position, stream.right);
    for (std::size_t i = 0; i < outLeft.size(); ++i) {
        outLeft[i] = std::clamp(outLeft[i] * stream.gain, -1.0f, 1.0f);
        outRight[i] = std::clamp(outRight[i] * stream.gain, -1.0f, 1.0f);
    }
    stream.position += outLeft.size();
}

}  // namespace

// ==============================================================================
// SC-014 - bounded under soak, at the worst parameter combination
//
// Two 30-minute renders at SC-009 arm (b)'s configuration:
//   (a)(b)(c) an UNFROZEN render driven by continuously generated G-2 for the
//             full 30 minutes - peak bound, no drift, nothing non-finite;
//   (d)       a FROZEN render, latched at 60 s with G-3 (digital silence)
//             thereafter - +/-0.5 dB state-energy conservation over the
//             remaining 29 minutes.
//
// Clause (d) is where "neither dies nor explodes overnight" is actually tested.
// Clause (b) cannot carry it: unfrozen, with decay clamped to <= 60 s
// (aether_reverb.h:2735-2736, FR-014), a silent tail decays to zero BY
// CONSTRUCTION, which is why the "G-2 for 60 s then silence" form of SC-014 was
// unsatisfiable and was replaced by continuous excitation plus this second
// render.                                                           (tasks.md T015)
// ==============================================================================
TEST_CASE("CavernVerb_Soak", "[effects][cavern][long]") {
    const CavernVerb::PrepareConfig config = armBConfig();
    const float noiseGain = calibrateNoiseGain(kSampleRate48);
    WARN("G-2 calibration gain " << noiseGain);

    std::vector<float> inLeft(kBlockSamples, 0.0f);
    std::vector<float> inRight(kBlockSamples, 0.0f);
    std::vector<float> outLeft(kBlockSamples, 0.0f);
    std::vector<float> outRight(kBlockSamples, 0.0f);
    const std::vector<float> silence(kBlockSamples, 0.0f);

    // -------------------------------------------------------------------------
    // Render 1 - unfrozen, 30 minutes of continuously generated G-2.
    // Clauses (a), (b) and (c).
    // -------------------------------------------------------------------------
    {
        CavernVerb cav;
        cav.prepare(kSampleRate48, config);
        applyArmBControls(cav);
        REQUIRE(cav.isPrepared());
        REQUIRE_FALSE(cav.isFrozen());

        NoiseStream stream;
        stream.gain = noiseGain;

        double outputPeak = 0.0;
        double inputPeak = 0.0;
        std::size_t nonFiniteOutputs = 0;
        std::vector<double> minuteRms;
        minuteRms.reserve(kSoakMinutes);

        for (std::size_t minute = 0; minute < kSoakMinutes; ++minute) {
            double sumSquares = 0.0;
            const std::size_t blocksThisMinute = kSecondsPerMinute * kBlocksPerSecond;
            for (std::size_t n = 0; n < blocksThisMinute; ++n) {
                nextNoiseBlock(stream, kSampleRate48, inLeft, inRight);

                cav.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                       outRight.data(), kBlockSamples);

                for (std::size_t i = 0; i < kBlockSamples; ++i) {
                    const float l = outLeft[i];
                    const float r = outRight[i];
                    if (!soakFinite(l) || !soakFinite(r)) {
                        ++nonFiniteOutputs;
                        continue;  // a non-finite sample poisons peak / RMS
                    }
                    inputPeak = std::max(inputPeak, static_cast<double>(std::fabs(inLeft[i])));
                    inputPeak = std::max(inputPeak, static_cast<double>(std::fabs(inRight[i])));
                    outputPeak = std::max(outputPeak, static_cast<double>(std::fabs(l)));
                    outputPeak = std::max(outputPeak, static_cast<double>(std::fabs(r)));
                    sumSquares += (static_cast<double>(l) * static_cast<double>(l)) +
                                  (static_cast<double>(r) * static_cast<double>(r));
                }
            }
            const auto count =
                static_cast<double>(blocksThisMinute) * static_cast<double>(kBlockSamples) * 2.0;
            minuteRms.push_back(std::sqrt(sumSquares / count));
        }

        REQUIRE(minuteRms.size() == kSoakMinutes);

        // --- the input honoured its side of clause (a)'s derivation -----------
        WARN("SC-014 input peak " << inputPeak);
        REQUIRE(inputPeak <= 1.0);

        // --- (a) peak ---------------------------------------------------------
        WARN("SC-014 (a) measured peak " << outputPeak << " against the bound " << kPeakBound);
        INFO("unfrozen 30-minute peak " << outputPeak);
        REQUIRE(outputPeak <= kPeakBound);
        REQUIRE(outputPeak > 1.0e-3);  // the component is ALIVE, not merely bounded

        // --- (c) nothing non-finite, and no recovery ever fired ---------------
        INFO("non-finite output samples " << nonFiniteOutputs);
        REQUIRE(nonFiniteOutputs == 0u);
        REQUIRE(cav.getNonFiniteRecoveryCount() == 0u);
        REQUIRE_FALSE(cav.isRecovering());

        // --- (b) no drift -----------------------------------------------------
        const std::size_t monotoneRun = longestStrictlyMonotoneRun(minuteRms);
        WARN("SC-014 (b) longest strictly monotone run " << monotoneRun << " of " << kSoakMinutes);
        INFO("minute 1 RMS " << minuteRms.front() << ", minute 10 RMS " << minuteRms[9]
                             << ", minute 30 RMS " << minuteRms.back());
        REQUIRE(monotoneRun < kSoakMinutes);

        REQUIRE(minuteRms[9] > 0.0);
        const double driftDb =
            20.0 * std::log10(std::max(minuteRms.back(), 1e-300) / minuteRms[9]);
        WARN("SC-014 (b) minute 30 vs minute 10: " << driftDb << " dB");
        REQUIRE(std::fabs(driftDb) <= kDriftBoundDb);
    }

    // -------------------------------------------------------------------------
    // Render 2 - clause (d): frozen at 60 s, digital silence (G-3) thereafter,
    // +/-0.5 dB state-energy conservation over the remaining 29 minutes.
    // -------------------------------------------------------------------------
    {
        CavernVerb cav;
        cav.prepare(kSampleRate48, config);
        applyArmBControls(cav);
        REQUIRE(cav.isPrepared());

        NoiseStream stream;
        stream.gain = noiseGain;

        // 60 s of G-2 to charge the late field.
        for (std::size_t second = 0; second < kFreezeEntrySecond; ++second) {
            for (std::size_t n = 0; n < kBlocksPerSecond; ++n) {
                nextNoiseBlock(stream, kSampleRate48, inLeft, inRight);
                cav.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                       outRight.data(), kBlockSamples);
            }
        }

        cav.setFreeze(true);

        // One settle second of G-3: the latch is a 50 ms per-sample crossfade
        // (kFreezeLatchMs, aether_reverb.h:1388), so isFrozen() must be true well
        // before the reference energy is taken.
        for (std::size_t n = 0; n < kBlocksPerSecond; ++n) {
            cav.processStereoBlock(silence.data(), silence.data(), outLeft.data(), outRight.data(),
                                   kBlockSamples);
        }
        REQUIRE(cav.isFrozen());

        // R-13: getStateEnergy() is O(sum m_i). It is sampled BETWEEN blocks,
        // once per second, never from inside a render.
        const float referenceEnergy = cav.getStateEnergy();
        INFO("frozen reference state energy " << referenceEnergy);
        REQUIRE(soakFinite(referenceEnergy));
        REQUIRE(referenceEnergy > 0.0f);
        const double referenceDb = energyToDb(referenceEnergy);

        const std::size_t measuredSeconds =
            kSoakSeconds - kFreezeEntrySecond - kFreezeSettleSeconds;  // 1739
        double worstDeviationDb = 0.0;
        std::size_t worstSecond = 0;
        std::size_t nonFiniteEnergies = 0;
        std::size_t nonFiniteOutputs = 0;

        for (std::size_t second = 0; second < measuredSeconds; ++second) {
            for (std::size_t n = 0; n < kBlocksPerSecond; ++n) {
                cav.processStereoBlock(silence.data(), silence.data(), outLeft.data(),
                                       outRight.data(), kBlockSamples);
                for (std::size_t i = 0; i < kBlockSamples; ++i) {
                    if (!soakFinite(outLeft[i]) || !soakFinite(outRight[i])) {
                        ++nonFiniteOutputs;
                    }
                }
            }

            const float energy = cav.getStateEnergy();
            if (!soakFinite(energy)) {
                ++nonFiniteEnergies;
                continue;
            }
            const double deviationDb = std::fabs(energyToDb(energy) - referenceDb);
            if (deviationDb > worstDeviationDb) {
                worstDeviationDb = deviationDb;
                worstSecond = second;
            }
        }

        WARN("SC-014 (d) worst frozen energy deviation " << worstDeviationDb << " dB at +"
                                                         << worstSecond << " s of "
                                                         << measuredSeconds);
        INFO("reference " << referenceDb << " dB, worst deviation " << worstDeviationDb
                          << " dB at second " << worstSecond);
        REQUIRE(nonFiniteEnergies == 0u);
        REQUIRE(nonFiniteOutputs == 0u);
        REQUIRE(cav.getNonFiniteRecoveryCount() == 0u);
        REQUIRE(worstDeviationDb <= kFreezeConservationDb);
    }
}
