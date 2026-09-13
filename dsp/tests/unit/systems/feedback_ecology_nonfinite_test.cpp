// ==============================================================================
// Layer 3: System Tests - FeedbackEcology non-finite hygiene (SC-012)
//                              (specs/vorago-phase5-feedback-ecology)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase5-feedback-ecology/spec.md   (SC-012)
//            specs/vorago-phase5-feedback-ecology/plan.md   (S8.5, S12.2, S12.3)
//            specs/vorago-phase5-feedback-ecology/tasks.md  (T001 creates this
//                                                            TU, T016 lands the
//                                                            case and the probe
//                                                            definition)
//
// SCOPE OF THIS TU: SC-012 ONLY. It is also the only definition of
//   Krate::DSP::detail::FeedbackEcologyNonFiniteProbe - the library declares
//   that struct (feedback_ecology.h:172) and befriends it (:1381), but never
//   defines it.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 5 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt:876. The other three must NOT be added to that
//   block: feedback_ecology_test.cpp and feedback_ecology_spectral_test.cpp stay
//   out so the FR-008/FR-009 guards are also exercised in the /fp:fast +
//   -ffast-math mode the header actually ships in, and
//   feedback_ecology_perf_test.cpp stays out because -fno-fast-math would move
//   the figures its baselines are pinned to.
//
// NON-FINITE VALUES ARE BUILT FROM BIT PATTERNS through a volatile sink -
//   never std::numeric_limits<float>::quiet_NaN()/infinity(), which fold to
//   finite garbage on the macOS/Linux -ffast-math legs. Transcribable idiom:
//   dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155
//   (makeNonFinite(bits); patterns 0x7FC00000, 0x7F800000, 0xFF800000).
//   Finiteness is asserted with Krate::DSP::detail::isFinite (core/db_utils.h),
//   never std::isnan / std::isinf / std::isfinite
//   (tools/lint-nonfinite-symbols.js gates this).
//
// NO BIT-EXACT FLOAT GOLDENS. Every `==` below is either "this stored value was
//   NOT written" (arm (a)), "this sample is the structural zero the read-mute
//   window produces" (arm (c1)), or "these two RENDERS of the same build agree
//   sample for sample" (the isolation arms) - never "this computation
//   reproduced a number pinned in the source", which is what
//   tools/lint-float-bit-goldens.js forbids. Aggregate comparisons go through
//   render_fingerprint.h's measured tolerances.
//
// ------------------------------------------------------------------------------
// THREE FIXTURE DECISIONS THAT ARE LOAD-BEARING, AND WHY - read before changing
// a number here. All three come from reading the shipped header this session;
// none of them weakens an assertion's SHAPE.
//
// 1. ARM (c) ZEROES EVERY OFF-DIAGONAL COUPLING. The default patch carries
//    FR-033's neighbour ring (kDefaultCoupling = 0.04 on i +/- 1), and the
//    per-sample path forms loop j's input from `appliedCoupling_[i][j] *
//    prevOut_[i]` (feedback_ecology.h:1956-1960). Clearing loop 2 therefore
//    changes loops 1 and 3 on the VERY NEXT SAMPLE, for a reason that has
//    nothing to do with whether the FR-047 trap is scoped correctly. With the
//    ring in force the "other five loops are bit-identical" claim is false on a
//    CORRECT build, so the fixture removes the structural channel and leaves
//    exactly the two the arm is about: the trap's per-loop clearLoopAudio(i) and
//    the trap's GLOBAL follower_.reset().
//
// 2. ARM (c1)'s CONVERGENCE IS MEASURED AT 3.75-4.00 s, NOT AT
//    kGovernorRampMs + kGovernorReleaseMs (820 ms). tasks.md T016 names 820 ms;
//    that figure accounts for the governor's own recovery and NOT for the
//    cleared loop's memory, which is the slower of the two. Loop 2 circulates
//    once per 109 ms at an applied own-feedback of kDefaultLoopGain = 0.72
//    (feedback_ecology.h:1953), so the transient the clear injects decays by
//    ~0.72 per circulation: at 820 ms that is 0.72^7.5 ~ 9e-2 of its initial
//    size - two orders of magnitude ABOVE kSampleTolerance = 5e-4 - and at
//    3.75 s it is 0.72^34 ~ 1e-5. The TOLERANCE is untouched and the assertion
//    shape is untouched; only the instant at which the network is asked to have
//    forgotten the perturbation moves, onto the time constant that actually
//    governs it. The 820 ms figure is still MEASURED AND REPORTED below so the
//    record carries it. STOP-AND-SURFACE: this is a deviation from the task
//    text and is flagged as such.
//
// 3. ARM (c2) DRIVES QUIETLY ON PURPOSE. Its clause "getGovernorGain() returns
//    to 1.0f within kGovernorRampMs + one control chunk" is only a statement
//    about the guard if the governor is IDLE - with the governor engaged the
//    S5.6 law re-targets below 1.0 at the next control step (the follower's
//    20 ms attack refills faster than the 20 ms ramp climbs), and the clause
//    would be unsatisfiable on a correct build. DERIVATION TABLE 3
//    (feedback_ecology.h:270-283) records the tracker at -57.0 dB for a
//    -28 dBFS drive against a -52 dB threshold, so a -30 dBFS drive holds the
//    governor exactly idle. The arm ASSERTS that precondition rather than
//    assuming it.
// ==============================================================================

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/feedback_ecology.h>

#include <krate/dsp/core/db_utils.h>  // detail::isFinite - never std::isnan
#include <krate/dsp/core/random.h>    // Xorshift32, for the deterministic drive

#include "artifact_detection.h"  // ClickDetector/ClickDetectorConfig - arm (b)
#include "render_fingerprint.h"  // kSampleTolerance, compareFingerprints - arms (c), (d)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using Krate::DSP::FeedbackEcology;
using Krate::DSP::Xorshift32;
using Krate::DSP::detail::isFinite;

// ==============================================================================
// FR-048: THE FAULT-INJECTION PROBE - declared by the library, DEFINED HERE AND
// NOWHERE ELSE (plan S8.5)
// ==============================================================================
// feedback_ecology.h:170-172 forward-declares this struct and :1381 befriends
// it; the library never defines it, so a shipping build has no way to call it
// and it adds no public surface. SC-012 (c) is its only consumer: no shipping
// code path, no plugin and no other test may name it. Sweep:
//   grep -rn "FeedbackEcologyNonFiniteProbe" dsp/ plugins/
// must return the header's declaration + friend line and this TU only.
//
// The two members poison the two stages FR-047 names and NOTHING else. Neither
// reaches into private state directly: DCBlocker's y1_ and EnvelopeFollower's
// squaredEnvelope_ are private, so each is poisoned by PROCESSING a non-finite
// sample through that one object - the documented propagation
// (dc_blocker.h:188 "NaN inputs are propagated"; envelope_follower.h:163 "Does
// NOT validate input") that makes the trap necessary in the first place.
// ==============================================================================
namespace Krate::DSP::detail {

struct FeedbackEcologyNonFiniteProbe {
    /// Sticks the poison in that loop's DCBlocker y1_ (dc_blocker.h:190-205:
    /// y = x - x1_ + R_ * y1_, then y1_ = flushDenormal(y), and flushDenormal
    /// returns a NaN unchanged because both of its ordered comparisons are
    /// false - db_utils.h:245-247).
    static void poisonDcBlocker(FeedbackEcology& fe, std::size_t loop, float nonFinite) noexcept {
        static_cast<void>(fe.loops_[loop].dcBlocker.process(nonFinite));
    }

    /// Sticks the poison in the governor tracker's squaredEnvelope_
    /// (envelope_follower.h:164-188, RMS mode: the release branch recomputes
    /// squared + releaseCoeff_ * (NaN - squared) = NaN forever).
    static void poisonFollower(FeedbackEcology& fe, float nonFinite) noexcept {
        static_cast<void>(fe.follower_.processSample(nonFinite));
    }
};

}  // namespace Krate::DSP::detail

namespace {

using Probe = Krate::DSP::detail::FeedbackEcologyNonFiniteProbe;

// =============================================================================
// Fixture constants
// =============================================================================

constexpr double kFs = 48000.0;

/// Every render below is issued in control-chunk-sized blocks, so a probe call
/// made between two blocks lands exactly on a control-step boundary and
/// "the next control step" is unambiguous (feedback_ecology.h:820-828: the
/// control step runs when controlPhase_ == 0, i.e. at the first sample of the
/// next call).
constexpr std::size_t kBlock = FeedbackEcology::kControlChunkSamples;  // 64

/// The loop the DCBlocker probe poisons. 2 is the plan's choice (S12.2); its
/// 109 ms default delay (kDefaultLoopDelayMs[2]) makes the read-mute window
/// long enough to measure without dominating the render.
constexpr std::size_t kProbeLoop = 2;

/// Xorshift32::nextFloat() is uniform on [-1, +1] (core/random.h:39), so its
/// RMS is 1/sqrt(3) = 0.57735. These two scales put the drive at the two levels
/// DERIVATION TABLE 3 (feedback_ecology.h:270-283) was measured on.
constexpr float kDriveScaleMinus12dBFS = 0.43503f;  // 0.25119 / 0.57735
constexpr float kDriveScaleMinus30dBFS = 0.05477f;  // 0.031623 / 0.57735

constexpr std::uint32_t kFixtureSeed = 0x5EEDu;
constexpr std::uint32_t kDriveSeed = 0x0D817Eu;

// =============================================================================
// Non-finite construction (never std::numeric_limits)
// =============================================================================

struct NonFinitePattern {
    const char* name;
    std::uint32_t bits;
};

constexpr std::array<NonFinitePattern, 3> kPatterns{{
    {"quiet NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant being folded
/// back into the memcpy at compile time, which is how a -ffast-math build turns
/// an "infinity" literal into a finite number.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

[[nodiscard]] float bitNaN() noexcept { return makeNonFinite(0x7FC00000u); }

/// The raw bits of a float. Used ONLY to express exact identity between two
/// values produced by the same build - never accumulated into a digest.
[[nodiscard]] std::uint32_t floatBits(float v) noexcept {
    std::uint32_t out = 0;
    std::memcpy(&out, &v, sizeof(out));
    return out;
}

// =============================================================================
// Drive and render helpers
// =============================================================================

struct StereoBuffer {
    std::vector<float> l;
    std::vector<float> r;
};

/// Deterministic bipolar noise, identical for a given seed. Both channels are
/// drawn from one stream, so the FR-016 mono sum is not a correlated special
/// case.
[[nodiscard]] StereoBuffer makeDrive(std::size_t n, std::uint32_t seed, float scale) {
    Xorshift32 rng{seed};
    StereoBuffer buf{.l = std::vector<float>(n, 0.0f), .r = std::vector<float>(n, 0.0f)};
    for (std::size_t i = 0; i < n; ++i) {
        buf.l[i] = rng.nextFloat() * scale;
        buf.r[i] = rng.nextFloat() * scale;
    }
    return buf;
}

/// The output pair plus all six FR-073 taps for one render.
struct Render {
    std::vector<float> outL;
    std::vector<float> outR;
    std::array<std::vector<float>, FeedbackEcology::kMaxLoops> taps;

    explicit Render(std::size_t n) : outL(n, 0.0f), outR(n, 0.0f) {
        for (auto& t : taps) {
            t.assign(n, 0.0f);
        }
    }
};

/// One reading per control step: the two FR-070 governor getters are only
/// observable between blocks, and kBlock == kControlChunkSamples, so one
/// reading per block IS one reading per control step.
struct GovernorTrace {
    std::vector<float> gain;
    std::vector<float> rms;
};

/// Renders `count` samples starting at `offset` of `drive` into `dst`, in
/// control-chunk blocks. When `trace` is non-null it records both governor
/// getters after every block.
void renderSpan(FeedbackEcology& fe, const StereoBuffer& drive, Render& dst, std::size_t offset,
                std::size_t count, GovernorTrace* trace = nullptr) {
    std::size_t done = 0;
    while (done < count) {
        const std::size_t n = std::min(kBlock, count - done);
        const std::size_t at = offset + done;
        std::array<float*, FeedbackEcology::kMaxLoops> ptrs{};
        for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
            ptrs[i] = dst.taps[i].data() + at;
        }
        fe.processBlockTapped(drive.l.data() + at, drive.r.data() + at, dst.outL.data() + at,
                              dst.outR.data() + at, ptrs.data(), n);
        if (trace != nullptr) {
            trace->gain.push_back(fe.getGovernorGain());
            trace->rms.push_back(fe.getGovernorRms());
        }
        done += n;
    }
}

/// The Phase-5 reference patch: every default table, wander on at
/// kDefaultWanderRateHz, the governor at its defaults, mix = 1 (so a criterion
/// measures the WET path and not the crossfade), wetGain = 0 dB, a fixed seed.
///
/// THE ORDER IS LOAD-BEARING and is the main TU's makeReference verbatim
/// (feedback_ecology_test.cpp:1282-1289): setMix/setWetGain come AFTER
/// prepare(), because prepare() step 7's applyDefaults() restores
/// kDefaultMix = 0.15f on EVERY call (FR-005 (c)); reset() comes last because it
/// SNAPS every ramp to the values just set and re-derives all twelve lane
/// streams from the seed.
void makeReference(FeedbackEcology& fe, double fs = kFs) {
    fe.setSeed(kFixtureSeed);
    fe.prepare(fs, FeedbackEcology::PrepareConfig{.maxBlockSamples = 4096, .numLoops = 6});
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);
    fe.reset();
}

/// The reference patch with the two structural cross-loop channels removed:
/// wander off (so the delay staircase is stationary and the read-mute window is
/// exactly computable) and EVERY off-diagonal coupling at zero (fixture decision
/// 1 in the header comment). `ratio` selects the governor arm: the default
/// kDefaultGovernorRatio for the governor-coupled run, 1.0f for the clean
/// isolation repeat (S5.6's exponent identity makes govGain exactly 1.0f there).
void makeIsolated(FeedbackEcology& fe, float ratio, double fs = kFs) {
    fe.setSeed(kFixtureSeed);
    fe.prepare(fs, FeedbackEcology::PrepareConfig{.maxBlockSamples = 4096, .numLoops = 6});
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);
    fe.setWanderEnabled(false);
    for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
        for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
            if (from != to) fe.setCoupling(from, to, 0.0f);
        }
    }
    fe.setGovernorRatio(ratio);
    fe.reset();
}

// =============================================================================
// Measurement helpers
// =============================================================================

[[nodiscard]] double rmsOf(const float* x, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, n)));
}

[[nodiscard]] double toDb(double v) { return 20.0 * std::log10(std::max(v, 1.0e-12)); }

/// Every sample of both channels finite AND inside the FR-046 clamp, over
/// [from, to).
struct BoundedResult {
    bool allFinite = true;
    double peak = 0.0;
    std::size_t firstBadIndex = 0;
};

[[nodiscard]] BoundedResult checkBounded(const Render& r, std::size_t from, std::size_t to) {
    BoundedResult out;
    for (std::size_t s = from; s < to; ++s) {
        const float a = r.outL[s];
        const float b = r.outR[s];
        if (!isFinite(a) || !isFinite(b)) {
            if (out.allFinite) out.firstBadIndex = s;
            out.allFinite = false;
            continue;
        }
        out.peak = std::max(out.peak, static_cast<double>(std::max(std::abs(a), std::abs(b))));
    }
    return out;
}

/// Worst absolute per-sample difference between two taps over [from, to).
[[nodiscard]] double worstTapError(const Render& a, const Render& b, std::size_t loop,
                                   std::size_t from, std::size_t to) {
    double worst = 0.0;
    for (std::size_t s = from; s < to; ++s) {
        worst = std::max(worst, std::abs(static_cast<double>(a.taps[loop][s])
                                         - static_cast<double>(b.taps[loop][s])));
    }
    return worst;
}

/// Number of samples in [from, to) at which two taps differ BIT FOR BIT.
[[nodiscard]] std::size_t countTapBitMismatches(const Render& a, const Render& b, std::size_t loop,
                                                std::size_t from, std::size_t to) {
    std::size_t mismatches = 0;
    for (std::size_t s = from; s < to; ++s) {
        if (floatBits(a.taps[loop][s]) != floatBits(b.taps[loop][s])) ++mismatches;
    }
    return mismatches;
}

/// SC-003 (b)'s SECOND, self-calibrating clause: the median of the largest `k`
/// absolute first differences of a settled span. It measures the render's own
/// slope population, so it is valid whatever the render's bandwidth - unlike a
/// fixed fraction of the peak, which a 1200 Hz-centred resonator bank can
/// legitimately exceed at 48 kHz.
[[nodiscard]] double medianOfLargestFirstDifferences(const float* x, std::size_t from,
                                                     std::size_t to, std::size_t k) {
    std::vector<double> diffs;
    if (to <= from + 1) return 0.0;
    diffs.reserve(to - from - 1);
    for (std::size_t s = from + 1; s < to; ++s) {
        diffs.push_back(std::abs(static_cast<double>(x[s]) - static_cast<double>(x[s - 1])));
    }
    k = std::min(k, diffs.size());
    if (k == 0) return 0.0;
    std::partial_sort(diffs.begin(), diffs.begin() + static_cast<std::ptrdiff_t>(k), diffs.end(),
                      [](double lhs, double rhs) { return lhs > rhs; });
    return diffs[k / 2];
}

[[nodiscard]] double maxFirstDifference(const float* x, std::size_t from, std::size_t to) {
    double worst = 0.0;
    for (std::size_t s = from + 1; s < to; ++s) {
        worst = std::max(worst, std::abs(static_cast<double>(x[s]) - static_cast<double>(x[s - 1])));
    }
    return worst;
}

// =============================================================================
// Arm (a): the float-setter table, in the header's DECLARATION ORDER
// =============================================================================
// Every single-argument float setter FeedbackEcology declares, in the order the
// header declares them (feedback_ecology.h:883-1153), plus setCoupling (three
// arguments, one of them the float under test). setCouplingMatrix takes a
// matrix and is swept separately below the table; setLoopFilterMode,
// setNumLoops, setWanderEnabled, setLoopDormant and setSeed take no float.
//
// Each row writes a LEGAL value first and asserts the getter MOVED. Without
// that step the "previous value stands" claim is satisfiable by a setter that
// stores nothing at all.

struct FloatSetterProbe {
    const char* name;
    void (*set)(FeedbackEcology&, float);
    float (*get)(const FeedbackEcology&);
    float sane;
};

}  // namespace

// ==============================================================================
// SC-012 - non-finite hygiene: the four arms, in the plan's order
// ==============================================================================
TEST_CASE("FeedbackEcology_NonFinite", "[feedback_ecology]") {
    using Krate::DSP::TestUtils::ClickDetection;
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;
    using Krate::DSP::TestUtils::compareFingerprints;
    using Krate::DSP::TestUtils::fingerprintRender;
    using Krate::DSP::TestUtils::kSampleTolerance;

    // ==========================================================================
    // (a) FR-009: every float setter rejects NaN and +/-Inf as a NO-OP, and the
    //     matching getter still reports the previous value.
    // ==========================================================================
    // std::clamp does NOT reject NaN - with v = NaN both `v < lo` and `hi < v`
    // are false, so v is returned unchanged. A clamp-only setter therefore
    // admits NaN into configuration state. FR-009's remedy is REJECTION:
    // `if (!detail::isFinite(v)) return;` as the first statement, so the
    // PREVIOUS value stands (feedback_ecology.h:884, :899, :912, ...).
    {
        const std::array<FloatSetterProbe, 16> probes{{
            {"setLoopCutoffHz", [](FeedbackEcology& fe, float v) { fe.setLoopCutoffHz(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopCutoffHz(kProbeLoop); }, 900.0f},
            {"setLoopFilterQ", [](FeedbackEcology& fe, float v) { fe.setLoopFilterQ(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopFilterQ(kProbeLoop); }, 0.3f},
            {"setLoopDelayMs", [](FeedbackEcology& fe, float v) { fe.setLoopDelayMs(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopDelayMs(kProbeLoop); }, 150.0f},
            // MUST precede the RT60 row: getLoopResonanceRt60 reports the
            // REALISED figure derived from the applied Q at the CURRENT centre
            // (DERIVATION TABLE 2), so moving the centre moves that reading.
            {"setLoopResonanceHz",
             [](FeedbackEcology& fe, float v) { fe.setLoopResonanceHz(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopResonanceHz(kProbeLoop); }, 500.0f},
            // 0.2 s at 500 Hz is Q = 9.1, well under kMaxResonatorQ, so the
            // realised figure MOVES (a 4 s request would clamp back to the same
            // realised value and the non-vacuity check below would fail).
            {"setLoopResonanceRt60",
             [](FeedbackEcology& fe, float v) { fe.setLoopResonanceRt60(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopResonanceRt60(kProbeLoop); }, 0.2f},
            {"setLoopGain", [](FeedbackEcology& fe, float v) { fe.setLoopGain(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopGain(kProbeLoop); }, 0.5f},
            {"setLoopInputGain",
             [](FeedbackEcology& fe, float v) { fe.setLoopInputGain(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopInputGain(kProbeLoop); }, 0.25f},
            {"setCoupling(1,2)", [](FeedbackEcology& fe, float v) { fe.setCoupling(1, 2, v); },
             [](const FeedbackEcology& fe) { return fe.getCoupling(1, 2); }, 0.25f},
            {"setGovernorThresholdDb",
             [](FeedbackEcology& fe, float v) { fe.setGovernorThresholdDb(v); },
             [](const FeedbackEcology& fe) { return fe.getGovernorThresholdDb(); }, -30.0f},
            {"setGovernorRatio", [](FeedbackEcology& fe, float v) { fe.setGovernorRatio(v); },
             [](const FeedbackEcology& fe) { return fe.getGovernorRatio(); }, 4.0f},
            {"setLoopDelayWander",
             [](FeedbackEcology& fe, float v) { fe.setLoopDelayWander(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopDelayWander(kProbeLoop); }, 0.3f},
            {"setLoopCutoffWander",
             [](FeedbackEcology& fe, float v) { fe.setLoopCutoffWander(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopCutoffWander(kProbeLoop); }, 2.0f},
            {"setWanderRate", [](FeedbackEcology& fe, float v) { fe.setWanderRate(v); },
             [](const FeedbackEcology& fe) { return fe.getWanderRate(); }, 0.5f},
            {"setLoopWake", [](FeedbackEcology& fe, float v) { fe.setLoopWake(kProbeLoop, v); },
             [](const FeedbackEcology& fe) { return fe.getLoopWakeAmount(kProbeLoop); }, 0.4f},
            {"setMix", [](FeedbackEcology& fe, float v) { fe.setMix(v); },
             [](const FeedbackEcology& fe) { return fe.getMix(); }, 0.6f},
            {"setWetGain", [](FeedbackEcology& fe, float v) { fe.setWetGain(v); },
             [](const FeedbackEcology& fe) { return fe.getWetGain(); }, -6.0f},
        }};

        FeedbackEcology fe;
        makeReference(fe);

        for (const FloatSetterProbe& p : probes) {
            INFO("(a) setter " << p.name);
            const float pristine = p.get(fe);
            p.set(fe, p.sane);
            const float before = p.get(fe);
            // NON-VACUITY: the setter demonstrably works before we ask whether
            // it refuses.
            REQUIRE(floatBits(before) != floatBits(pristine));

            for (const NonFinitePattern& pattern : kPatterns) {
                INFO("(a) pattern " << pattern.name);
                p.set(fe, makeNonFinite(pattern.bits));
                REQUIRE(floatBits(p.get(fe)) == floatBits(before));
            }
        }

        // setCouplingMatrix: the SAME per-entry rule applied element by element
        // (feedback_ecology.h:981-990). One poisoned entry leaves ONLY that
        // pair's previous value standing; every other entry - including a NEW
        // legal one written in the same call - lands.
        for (const NonFinitePattern& pattern : kPatterns) {
            INFO("(a) setCouplingMatrix with a poisoned entry, pattern " << pattern.name);

            std::array<std::array<float, FeedbackEcology::kMaxLoops>, FeedbackEcology::kMaxLoops> m{};
            for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
                for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                    m[from][to] = fe.getCoupling(from, to);
                }
            }
            const float poisonedPairBefore = fe.getCoupling(1, 2);
            const float goodPairBefore = fe.getCoupling(3, 4);
            const float goodPairNew = 0.31f;
            REQUIRE(floatBits(goodPairBefore) != floatBits(goodPairNew));

            m[1][2] = makeNonFinite(pattern.bits);
            m[3][4] = goodPairNew;
            fe.setCouplingMatrix(m);

            REQUIRE(floatBits(fe.getCoupling(1, 2)) == floatBits(poisonedPairBefore));
            REQUIRE(floatBits(fe.getCoupling(3, 4)) == floatBits(goodPairNew));
            // Restore, so the next pattern's non-vacuity check has room.
            fe.setCoupling(3, 4, goodPairBefore);
        }
    }

    // ==========================================================================
    // (b) THE PUBLIC PATH: one non-finite INPUT sample into a settled render.
    // ==========================================================================
    // FR-047 correctly does NOT fire here, and that is the finding rather than a
    // gap: the dry is sanitised PER CHANNEL before the mono sum
    // (feedback_ecology.h:1926-1928), one stage earlier even than the SVF's own
    // non-finite reset. The poison therefore never reaches b_i, so the counter
    // must read 0 - an "increments by exactly 1" assertion here would be
    // unsatisfiable on a correct build (spec S16 C-2).
    {
        constexpr std::size_t kSettle = static_cast<std::size_t>(2.0 * kFs);  // 96 000
        constexpr std::size_t kWindow = static_cast<std::size_t>(0.2 * kFs);  // 9 600
        constexpr std::size_t kTotal = kSettle + 2 * kWindow;
        static_assert(kSettle % kBlock == 0, "the injection must land on a control boundary");

        StereoBuffer drive = makeDrive(kTotal, kDriveSeed, kDriveScaleMinus12dBFS);
        const float cleanL = drive.l[kSettle];
        const float cleanR = drive.r[kSettle];
        Render render(kTotal);

        for (const NonFinitePattern& pattern : kPatterns) {
            INFO("(b) input-sample injection, pattern " << pattern.name);

            FeedbackEcology inst;
            makeReference(inst);

            // ONE sample, both channels, at the settled boundary.
            const float bad = makeNonFinite(pattern.bits);
            drive.l[kSettle] = bad;
            drive.r[kSettle] = bad;

            renderSpan(inst, drive, render, 0, kTotal);

            // Restore the drive so the next pattern starts from clean content.
            drive.l[kSettle] = cleanL;
            drive.r[kSettle] = cleanR;

            const BoundedResult bounded = checkBounded(render, kSettle, kTotal);
            INFO("(b) peak from the injection sample onward = " << bounded.peak);
            REQUIRE(bounded.allFinite);
            REQUIRE(bounded.peak < static_cast<double>(FeedbackEcology::kOutputClamp));
            // ANTI-VACUITY: a silent render passes every clause above.
            REQUIRE(bounded.peak > 1.0e-3);

            REQUIRE(inst.getNonFiniteResetCount() == 0u);
            REQUIRE(inst.getClampEngagementCount() == 0u);

            // The recovery edge is click-bounded.
            ClickDetectorConfig cfg;
            cfg.sampleRate = static_cast<float>(kFs);  // the struct default is 44 100
            ClickDetector detector(cfg);
            detector.prepare();
            const std::vector<ClickDetection> hits =
                detector.detect(render.outL.data() + kSettle, kWindow);
            INFO("(b) " << hits.size() << " click detections in the 200 ms after the injection");
            REQUIRE(hits.size() <= 1u);

            // SC-003 (b)'s self-calibrating shape clause, with the slope
            // population taken from the SETTLED span before the injection.
            const double reference =
                medianOfLargestFirstDifferences(render.outL.data(), kSettle / 2, kSettle, 1000);
            // kSettle - 1 as the start so the very first difference the arm is
            // about - the one ACROSS the injection sample - is inside the span.
            const double worst =
                maxFirstDifference(render.outL.data(), kSettle - 1, kSettle + kWindow);
            INFO("(b) max |first difference| after the injection = "
                 << worst << ", median of the largest 1 000 before it = " << reference
                 << ", bound = " << (8.0 * reference));
            REQUIRE(reference > 0.0);
            REQUIRE(worst <= 8.0 * reference);
        }
    }

    // ==========================================================================
    // (c1) THE IN-LOOP DCBlocker, reaching the per-sample rung-5 trap
    //      (feedback_ecology.h:1986-1992).
    // ==========================================================================
    {
        constexpr std::size_t kSettle = static_cast<std::size_t>(2.0 * kFs);  // 96 000
        static_assert(kSettle % kBlock == 0, "the probe must land on a control boundary");

        struct Arm {
            const char* name;
            float ratio;
            std::size_t post;
            bool expectWholeSpanBitIdentity;
        };
        const std::array<Arm, 2> arms{{
            // The governor-coupled run. follower_.reset() zeroes the tracker
            // GLOBALLY, so bit-identity can only be claimed up to the first
            // control step after the trap; convergence is the claim thereafter.
            {"governor engaged (kDefaultGovernorRatio)", FeedbackEcology::kDefaultGovernorRatio,
             static_cast<std::size_t>(4.0 * kFs), false},
            // The clean isolation proof. At ratio = 1 the S5.6 exponent identity
            // makes govGain exactly 1.0f at every level, so follower_.reset() is
            // unobservable and the other five loops are bit-identical for the
            // WHOLE render.
            {"governor off (ratio = 1)", 1.0f, static_cast<std::size_t>(1.0 * kFs), true},
        }};

        for (const Arm& arm : arms) {
            INFO("(c1) " << arm.name);
            const std::size_t total = kSettle + arm.post;

            const StereoBuffer drive = makeDrive(total, kDriveSeed, kDriveScaleMinus12dBFS);

            // The never-poisoned twin, rendered end to end with the same drive.
            FeedbackEcology ref;
            makeIsolated(ref, arm.ratio);
            Render refRender(total);
            renderSpan(ref, drive, refRender, 0, total);
            REQUIRE(ref.getNonFiniteResetCount() == 0u);

            FeedbackEcology fe;
            makeIsolated(fe, arm.ratio);
            Render feRender(total);
            renderSpan(fe, drive, feRender, 0, kSettle);

            REQUIRE(fe.getNonFiniteResetCount() == 0u);
            const float preRms = fe.getGovernorRms();
            const float preGain = fe.getGovernorGain();
            REQUIRE(isFinite(preRms));
            REQUIRE(isFinite(preGain));
            INFO("(c1) pre-poison governor rms = " << toDb(static_cast<double>(preRms))
                                                   << " dB, gain = " << preGain);
            // ANTI-VACUITY for the two arms' distinct premises: the engaged arm
            // must actually be engaged, the ratio = 1 arm must be exactly unity.
            if (arm.expectWholeSpanBitIdentity) {
                REQUIRE(floatBits(preGain) == floatBits(1.0f));
            } else {
                REQUIRE(preGain < 1.0f);
            }

            // The read-mute window clearLoopAudio() will install is
            // ceil(currentDelaySamples) + 1 (feedback_ecology.h:2219-2221). With
            // wander off the delay is stationary, so this reading taken now is
            // the delay in force at the trap; floor() keeps the assertion
            // conservative in the only direction that matters.
            const double delaySamples = static_cast<double>(fe.getLoopCurrentDelayMs(kProbeLoop))
                                        * kFs / 1000.0;
            const auto muteFloor = static_cast<std::size_t>(std::floor(delaySamples));
            REQUIRE(muteFloor > 0u);

            // *** THE INJECTION ***
            Probe::poisonDcBlocker(fe, kProbeLoop, bitNaN());
            REQUIRE(fe.getNonFiniteResetCount() == 0u);  // the probe itself counts nothing

            GovernorTrace trace;
            renderSpan(fe, drive, feRender, kSettle, arm.post, &trace);

            // --- the counter: EXACTLY one trap, and no second one ------------
            REQUIRE(fe.getNonFiniteResetCount() == 1u);

            // --- clearLoopAudio(kProbeLoop) ran ------------------------------
            // The trap sample is the first sample of the post span: b is forced
            // to 0.0f, so y = tanh(0) = 0 and the tap is the structural zero.
            // It stays exactly zero for the whole read-mute window, during which
            // the loop WRITES the delay line but does not READ it.
            for (std::size_t s = kSettle; s < kSettle + muteFloor; ++s) {
                if (floatBits(feRender.taps[kProbeLoop][s]) != floatBits(0.0f)) {
                    INFO("(c1) loop " << kProbeLoop << " tap non-zero at offset " << (s - kSettle)
                                      << " of the " << muteFloor << "-sample read-mute window: "
                                      << feRender.taps[kProbeLoop][s]);
                    REQUIRE(false);
                }
            }
            // ...and then the loop REFILLS from its input tap rather than
            // resuming its ring.
            const std::size_t refillSearchEnd =
                std::min(total, kSettle + muteFloor + static_cast<std::size_t>(0.5 * kFs));
            std::size_t refillAt = refillSearchEnd;
            for (std::size_t s = kSettle + muteFloor; s < refillSearchEnd; ++s) {
                if (feRender.taps[kProbeLoop][s] != 0.0f) {
                    refillAt = s;
                    break;
                }
            }
            INFO("(c1) loop " << kProbeLoop << " refilled " << (refillAt - kSettle)
                              << " samples after the trap (read-mute floor " << muteFloor << ")");
            REQUIRE(refillAt < refillSearchEnd);

            // --- the governor's follower was reset ---------------------------
            for (std::size_t k = 0; k < trace.gain.size(); ++k) {
                if (!isFinite(trace.gain[k]) || !isFinite(trace.rms[k])) {
                    INFO("(c1) non-finite governor reading at control step " << k);
                    REQUIRE(false);
                }
            }
            // "back within 1 dB of its pre-poison value within 2 s": the closest
            // approach over the first 2 s of control steps.
            const std::size_t stepsIn2s = static_cast<std::size_t>(2.0 * kFs) / kBlock;
            double bestRmsErrorDb = 1.0e9;
            for (std::size_t k = 0; k < std::min(stepsIn2s, trace.rms.size()); ++k) {
                bestRmsErrorDb = std::min(bestRmsErrorDb,
                                          std::abs(toDb(static_cast<double>(trace.rms[k]))
                                                   - toDb(static_cast<double>(preRms))));
            }
            INFO("(c1) governor rms returned to within " << bestRmsErrorDb
                                                         << " dB of its pre-poison value");
            REQUIRE(bestRmsErrorDb <= 1.0);

            // --- the main output is finite and bounded from the trap onward ---
            const BoundedResult bounded = checkBounded(feRender, kSettle, total);
            INFO("(c1) peak from the trap sample onward = " << bounded.peak);
            REQUIRE(bounded.allFinite);
            REQUIRE(bounded.peak < static_cast<double>(FeedbackEcology::kOutputClamp));
            REQUIRE(bounded.peak > 1.0e-3);  // anti-vacuity
            REQUIRE(fe.getClampEngagementCount() == 0u);

            // --- the other five loops, scoped correctly ----------------------
            for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
                if (i == kProbeLoop) continue;
                INFO("(c1) unpoisoned loop " << i);

                // Bit-identical for every sample BEFORE the first control step
                // after the injection. (The trap runs inside renderChunk, i.e.
                // AFTER the control step that opened the post span, so the next
                // control step is kBlock samples later.)
                REQUIRE(countTapBitMismatches(feRender, refRender, i, kSettle, kSettle + kBlock)
                        == 0u);

                if (arm.expectWholeSpanBitIdentity) {
                    // ratio = 1: nothing couples these loops to loop 2 at all.
                    REQUIRE(countTapBitMismatches(feRender, refRender, i, kSettle, total) == 0u);
                    continue;
                }

                // Reported for the record: the error at the instant
                // kGovernorRampMs + kGovernorReleaseMs names (820 ms). See
                // fixture decision 2 in this file's header comment for why the
                // ASSERTION is taken later - the cleared loop's own 109 ms /
                // 0.72-per-circulation memory, not the governor's ramp, is the
                // slow term.
                const std::size_t at820ms =
                    kSettle + static_cast<std::size_t>(0.820 * kFs);
                const double err820 =
                    worstTapError(feRender, refRender, i, at820ms, at820ms + kBlock);
                const std::size_t convergeFrom = total - static_cast<std::size_t>(0.25 * kFs);
                const double errFinal = worstTapError(feRender, refRender, i, convergeFrom, total);
                INFO("(c1) worst tap error: " << err820 << " at 820 ms, " << errFinal
                                              << " over the final 250 ms (tolerance "
                                              << kSampleTolerance << ")");
                REQUIRE(errFinal <= static_cast<double>(kSampleTolerance));
            }
        }
    }

    // ==========================================================================
    // (c2) THE GOVERNOR'S EnvelopeFollower, reaching the S5.6 control-step guard
    //      (feedback_ecology.h:1849-1855).
    // ==========================================================================
    // This path CANNOT reach the per-sample b_i trap. Without the guard the
    // component sits permanently muted with both health counters reading clean:
    // a NaN tracker makes the law's target NaN, LinearRamp::setTarget converts a
    // NaN target into an unramped step to ZERO (smoother.h:342-348), govGain
    // becomes 0.0f, every y_i becomes 0 and every b_i stays finite forever.
    {
        constexpr std::size_t kSettle = static_cast<std::size_t>(1.0 * kFs);   // 48 000
        constexpr std::size_t kWindow = static_cast<std::size_t>(2.0 * kFs);   // 96 000
        constexpr std::size_t kTotal = kSettle + 2 * kWindow;
        static_assert((kSettle + kWindow) % kBlock == 0,
                      "the probe must land on a control boundary");

        const std::size_t inject = kSettle + kWindow;

        const StereoBuffer drive = makeDrive(kTotal, kDriveSeed, kDriveScaleMinus30dBFS);

        FeedbackEcology fe;
        makeReference(fe);
        fe.setWanderEnabled(false);
        fe.reset();

        Render render(kTotal);
        renderSpan(fe, drive, render, 0, inject);

        const float preRms = fe.getGovernorRms();
        const float preGain = fe.getGovernorGain();
        INFO("(c2) pre-poison governor rms = " << toDb(static_cast<double>(preRms))
                                               << " dB, gain = " << preGain);
        REQUIRE(isFinite(preRms));
        REQUIRE(preRms > 0.0f);
        // THE ARM'S PRECONDITION, ASSERTED RATHER THAN ASSUMED (fixture decision
        // 3): the -30 dBFS drive must leave the governor exactly idle, or the
        // "returns to 1.0f" clause below is a statement about the law rather
        // than about the guard.
        REQUIRE(floatBits(preGain) == floatBits(1.0f));
        REQUIRE(fe.getNonFiniteResetCount() == 0u);

        // *** THE INJECTION ***
        Probe::poisonFollower(fe, bitNaN());
        REQUIRE(fe.getNonFiniteResetCount() == 0u);  // the probe itself counts nothing
        // The poison really is in the tracker's state - otherwise the guard
        // below would have nothing to catch.
        REQUIRE(!isFinite(fe.getGovernorRms()));

        // ONE control chunk: the guard fires at the control step that opens it,
        // i.e. within kBlock samples of the injection.
        GovernorTrace trace;
        renderSpan(fe, drive, render, inject, kBlock, &trace);
        REQUIRE(fe.getNonFiniteResetCount() == 1u);

        // The rest of the post window.
        renderSpan(fe, drive, render, inject + kBlock, kWindow - kBlock, &trace);
        REQUIRE(fe.getNonFiniteResetCount() == 1u);  // exactly one, never a second

        for (std::size_t k = 0; k < trace.gain.size(); ++k) {
            if (!isFinite(trace.gain[k]) || !isFinite(trace.rms[k])) {
                INFO("(c2) non-finite governor reading at control step " << k
                                                                        << " after the injection");
                REQUIRE(false);
            }
            // The guard re-targets the ramp to unity, and the fixture holds the
            // law at unity too, so the gain never leaves 1.0f. A build that let
            // the NaN reach LinearRamp::setTarget would read exactly 0.0f here.
            if (floatBits(trace.gain[k]) != floatBits(1.0f)) {
                INFO("(c2) governor gain left unity at control step " << k << ": " << trace.gain[k]);
                REQUIRE(false);
            }
        }

        // The tracker recovers toward its pre-poison reading. Strict per-step
        // monotonicity is NOT asserted: the drive is stochastic and an RMS
        // follower with a kGovernorReleaseMs = 800 ms release still dips on a
        // quiet passage, so a monotone gate would measure the noise, not the
        // recovery. What is asserted is that it gets back within 1 dB inside 2 s.
        double bestRmsErrorDb = 1.0e9;
        for (const float v : trace.rms) {
            bestRmsErrorDb = std::min(bestRmsErrorDb, std::abs(toDb(static_cast<double>(v))
                                                               - toDb(static_cast<double>(preRms))));
        }
        INFO("(c2) governor rms returned to within " << bestRmsErrorDb
                                                     << " dB of its pre-poison value");
        REQUIRE(bestRmsErrorDb <= 1.0);

        // The render stays finite and bounded from the injection onward.
        const BoundedResult bounded = checkBounded(render, inject, kTotal);
        INFO("(c2) peak from the injection onward = " << bounded.peak);
        REQUIRE(bounded.allFinite);
        REQUIRE(bounded.peak < static_cast<double>(FeedbackEcology::kOutputClamp));
        REQUIRE(bounded.peak > 1.0e-5);  // anti-vacuity at this quiet fixture

        // *** THE ANTI-MUTE CLAUSE - THE WHOLE POINT OF THE ARM ***
        // A build that merely muted itself fails here by 60 dB or more while
        // passing everything else.
        const double beforeDb = toDb(rmsOf(render.outL.data() + kSettle, kWindow));
        const double afterDb = toDb(rmsOf(render.outL.data() + inject, kWindow));
        INFO("(c2) output RMS " << beforeDb << " dB before vs " << afterDb << " dB after");
        REQUIRE(std::abs(afterDb - beforeDb) <= 1.0);
    }

    // ==========================================================================
    // (d) reset() recovers determinism after a probe injection.
    // ==========================================================================
    {
        constexpr std::size_t kPre = static_cast<std::size_t>(1.0 * kFs);
        constexpr std::size_t kPoisoned = static_cast<std::size_t>(0.5 * kFs);
        constexpr std::size_t kAfter = static_cast<std::size_t>(2.0 * kFs);
        constexpr std::size_t kTotal = kPre + kPoisoned;
        static_assert(kPre % kBlock == 0, "the probe must land on a control boundary");

        const StereoBuffer drive = makeDrive(std::max(kTotal, kAfter), kDriveSeed,
                                             kDriveScaleMinus12dBFS);

        // The poisoned instance: render, poison, let the trap fire, reset.
        FeedbackEcology fe;
        makeReference(fe);
        {
            Render scratch(kTotal);
            renderSpan(fe, drive, scratch, 0, kPre);
            Probe::poisonDcBlocker(fe, kProbeLoop, bitNaN());
            renderSpan(fe, drive, scratch, kPre, kPoisoned);
            REQUIRE(fe.getNonFiniteResetCount() == 1u);
        }
        fe.reset();
        // FR-047's counter is cleared by reset() (feedback_ecology.h:720).
        REQUIRE(fe.getNonFiniteResetCount() == 0u);

        Render recovered(kAfter);
        renderSpan(fe, drive, recovered, 0, kAfter);

        // The never-poisoned twin, from the same patch and the same reset().
        FeedbackEcology clean;
        makeReference(clean);
        clean.reset();
        Render pristine(kAfter);
        renderSpan(clean, drive, pristine, 0, kAfter);

        const auto actual =
            fingerprintRender(std::span<const float>(recovered.outL.data(), kAfter));
        const auto reference =
            fingerprintRender(std::span<const float>(pristine.outL.data(), kAfter));
        const auto comparison = compareFingerprints(actual, reference);
        INFO("(d) " << comparison.detail << " | worst metric relative error "
                    << comparison.worstMetricRelativeError << ", worst sample error "
                    << comparison.worstSampleError);
        // ANTI-VACUITY: the reference render must not be silence.
        REQUIRE(reference.rms > 1.0e-4);
        REQUIRE(comparison.withinTolerance());
        REQUIRE(fe.getNonFiniteResetCount() == 0u);
    }
}
