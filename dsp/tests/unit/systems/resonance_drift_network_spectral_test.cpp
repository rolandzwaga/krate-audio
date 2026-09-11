// ==============================================================================
// Layer 3: System Tests - ResonanceDriftNetwork, spectral and long-render cases
// ==============================================================================
// Vorago Phase 3 (specs/vorago-phase3-resonance-drift): ResonanceDriftNetwork
// spectral/long-render cases (SC-001, SC-002, SC-003 [long], SC-015 [long],
// SC-016 [long]).
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase3-resonance-drift/spec.md
//            specs/vorago-phase3-resonance-drift/plan.md
//            specs/vorago-phase3-resonance-drift/tasks.md
//
// TASKS THAT OWN CASES IN THIS TU:
//   T010  ResonanceDriftNetwork_PeakLifeCycle, arms (a)(c)(d)(e)(h)
//   T016  ResonanceDriftNetwork_MaxQRingOut            SC-001 (a)-(e)
//         ResonanceDriftNetwork_NoZipperUnderDrift     SC-002 (a)(b)(c)
//         ResonanceDriftNetwork_SlewLimitBoundaryRatio SC-018 (c)
//   T018  ResonanceDriftNetwork_WanderRateSpectral     SC-003 (a)-(f)   [long]
//         ResonanceDriftNetwork_PeakLifeCycle, arms (b)(f)(g)           [long]
//         ResonanceDriftNetwork_LongRenderBoundedness  SC-016 (a)-(d)   [long]
//
// WHY T018's THREE CASES ARE [long] AND SC-001/SC-002 ARE NOT: every assertion
//   T018 adds is a property of the seeded walk, of exact zeros, or of a measured
//   statistic - toolchain-INDEPENDENT - and each case renders for minutes, so
//   they belong in the nightly lane (CLAUDE.md's [long] rule). SC-001, SC-002,
//   SC-009, SC-010, SC-011, SC-019 and SC-021 stay OUT of that lane: they are
//   the cross-platform sentinels and the sub-second renders.
//
// NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()/infinity()
//   here - this TU is deliberately NOT in dsp/tests/CMakeLists.txt's
//   -fno-fast-math block (tasks.md T001). SC-009 owns bit-pattern injection and
//   lives in resonance_drift_network_nonfinite_test.cpp.
//
// ALLOCATION DETECTION: this TU must NOT include
//   <allocation_operator_overrides.h> - the single owner of the global operator
//   new/delete override in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error.
//
// NO BIT-EXACT FLOAT GOLDENS anywhere in this TU
//   (node tools/lint-float-bit-goldens.js gates it).
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/systems/resonance_drift_network.h>

// Cited directly by the T016 cases: kMaxResonatorQ / kMinResonatorFrequency /
// kLn1000 build SC-001 (d)'s ring-out bound FROM THE SHIPPED CONSTANTS, kPi
// builds the drive's phase increment, and Xorshift32 is SC-001's white-noise
// source. All three headers are already pulled in transitively by the component
// header; they are named here so the dependency is visible at the call site.
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/processors/resonator_bank.h>

// T018 only. PinkNoiseFilter (Layer 1) is SC-003's and SC-016's specified drive
// source; BrownianDrift (Layer 2) is named because SC-003 (e)'s anti-vacuity
// control builds twelve lanes on ONE seed and salt directly, outside the
// network, so the estimator is pinned against a series pair it MUST read as 1;
// SlowEventScheduler (Layer 2) is included for its kDefaultMinInterval /
// kDefaultMaxInterval only - SC-016's wake pattern is "driven by the test, not
// owned by the component", so the two constants are cited rather than the
// scheduler being instantiated.
#include <krate/dsp/primitives/pink_noise_filter.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/slow_event_scheduler.h>

#include <audio_features.h>
#include <render_fingerprint.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using Krate::DSP::ResonanceDriftNetwork;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kMaxPeaks = ResonanceDriftNetwork::kMaxPeaks;

/// SC-002 (d)'s per-sample gate-step bound, restated here because the SC-015
/// (e) arm below asserts against it and this TU cannot see the sibling TU's
/// anonymous namespace.
constexpr float kGateStepBound =
    1.05f / (ResonanceDriftNetwork::kGainRampMs * 0.001f * static_cast<float>(kFs));
static_assert(kGateStepBound > 4.3e-4f && kGateStepBound < 4.5e-4f,
              "the SC-002 (d) bound is 4.375e-4 at 48 kHz");

/// Deterministic, non-trivial stereo drive. No RNG: every arm here compares two
/// instances sample-for-sample, and a shared generator would hide a desync.
void fillDrive(std::vector<float>& l, std::vector<float>& r) {
    for (std::size_t i = 0; i < l.size(); ++i) {
        const auto n = static_cast<float>(i);
        l[i] = 0.25f * std::sin(0.013f * n);
        r[i] = 0.25f * std::sin(0.017f * n + 0.7f);
    }
}

[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

[[nodiscard]] float peakAbs(const std::vector<float>& v) {
    float worst = 0.0f;
    for (const float s : v) {
        worst = std::max(worst, std::abs(s));
    }
    return worst;
}

/// Scanned WITHOUT a per-sample REQUIRE: these renders are 10^5 samples long and
/// a per-sample assertion would register hundreds of thousands of them and
/// dominate the suite's runtime. The caller REQUIREs the single bool.
[[nodiscard]] bool allExactlyZero(const std::vector<float>& v) {
    for (const float s : v) {
        if (s != 0.0f) return false;
    }
    return true;
}

/// Render a whole buffer in fixed blocks, invoking `perBlock(sampleOffset)`
/// immediately BEFORE each block - which is how the (a) arm drives a setter
/// mid-render at the cadence FR-040's Phase-10 scheduler will.
template <typename PerBlock>
void renderBuffer(ResonanceDriftNetwork& net, const std::vector<float>& inL,
                  const std::vector<float>& inR, std::vector<float>& outL,
                  std::vector<float>& outR, std::size_t blockSamples, const PerBlock& perBlock) {
    const std::size_t total = inL.size();
    outL.assign(total, 0.0f);
    outR.assign(total, 0.0f);
    for (std::size_t done = 0; done < total;) {
        perBlock(done);
        const std::size_t chunk = std::min(blockSamples, total - done);
        net.processBlock(inL.data() + done, inR.data() + done,
                         outL.data() + done, outR.data() + done, chunk);
        done += chunk;
    }
}

/// Render the whole supplied drive, discarding the output - the settling pass
/// every arm's precondition needs.
void settle(ResonanceDriftNetwork& net, const std::vector<float>& inL,
            const std::vector<float>& inR) {
    std::vector<float> oL;
    std::vector<float> oR;
    renderBuffer(net, inL, inR, oL, oR, 512, [](std::size_t) {});
}

// =============================================================================
// T016 machinery - SC-001, SC-002 (a)(b)(c), SC-018 (c)
//
// Every render below is STREAMED in kChunk-sample blocks and never materialised:
// the pinned 60 s renders are 2 880 000 samples per channel, so four full
// buffers would cost ~46 MB and buy nothing - every statistic here is
// computable in one forward pass.
// =============================================================================

/// The FR-007 control grid. Driving in blocks of exactly this size makes a block
/// boundary a CONTROL-STEP boundary, which is what SC-002's `n mod 64` partition
/// is written about and what SC-002 (c)'s "on every control chunk" injection and
/// SC-018 (c)'s note schedule need in order to land on the boundary samples.
constexpr std::size_t kChunk = ResonanceDriftNetwork::kControlChunkSamples;
static_assert(kChunk == 64u, "SC-002's partition arithmetic below assumes 64");

/// SC-002's PINNED render length: exactly 60 s at 48 kHz. Pinned because the
/// boundary population's SIZE determines the 99.9th percentile - a shorter
/// render moves both statistics and invalidates kBoundaryRatio.
constexpr std::size_t kPinnedSamples = 2880000;
static_assert(kPinnedSamples == static_cast<std::size_t>(60.0 * kFs),
              "SC-002 pins the render at exactly 60 s");
static_assert(kPinnedSamples % kChunk == 0u, "whole control chunks only");
static_assert(kPinnedSamples / kChunk == 45000u, "45 000 control chunks");

/// SC-002 (a)'s bound. **MEASURED, 2026-09-11**, across the eight calibration
/// seeds UNDER THE {0, 1, 2} PARTITION the criterion requires (spec correction
/// C-11; a value measured under the {0, 1} partition may NOT be carried over):
/// B/P read mean 1.0043, sd 0.0032, min 0.9987, max 1.0093 over populations of
/// 135 000 boundary against 2 745 000 interior second differences - i.e. a
/// correct build's control boundaries are statistically indistinguishable from
/// its interior, which is the whole claim.
///
/// TIGHTENED from the 1.5 carried in to the calibration's own suggestion,
/// observed max + 3 sd = 1.1102 rounded to 1.11. The (c) injection arm reads
/// 4.00 / 4.01 / 4.66 on the same fixture, so the bound sits an order of
/// magnitude clear of the null and still a factor of 3.6 below the defect - a
/// 1.5 bound would have accepted a build whose boundary steps were half again
/// the size of its interior ones, which is exactly the zipper this criterion
/// exists to catch.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-002 (a) kBoundaryRatio" (resonance_drift_network_perf_test.cpp,
/// [.calibration]).
constexpr float kBoundaryRatio = 1.11f;

/// SC-002 (b)'s first bound: with retuning switched off the boundary population
/// must be statistically indistinguishable from the interior one.
constexpr float kControlArmRatio = 1.05f;

/// SC-002 (b)'s SECOND bound, on the interior curvature - **NORMALISED**, and
/// asserted on the CONTROL ARM against a closed form rather than as an
/// on-vs-off difference. See the correction note in the case itself
/// (SPEC CORRECTION C-14) for the measurements that forced the change; the
/// tolerance is the spec's own 0.10, now carried by a quantity that can
/// actually satisfy it (measured error 0.0003 across three seeds and two wet
/// trims, i.e. 300x inside this bound).
constexpr float kInteriorCurvatureTolerance = 0.10f;

/// The second difference of a pure sine, PER UNIT AMPLITUDE. For
/// x[n] = A*sin(w*n) the estimator is exact, not approximate:
///   |x[n] - 2x[n-1] + x[n-2]| = |2cos(w) - 2| * |x[n-1]| = 4 sin^2(w/2) * |x[n-1]|
/// so a render whose output is that sine and nothing else has an interior
/// statistic of exactly 4 sin^2(w/2) times its own amplitude statistic, at every
/// level and for every quantile. That identity is what SC-002 (b) now asserts.
[[nodiscard]] double sineCurvaturePerUnitAmplitude(double hz, double fs) {
    const double halfOmega = static_cast<double>(Krate::DSP::kPi) * hz / fs;
    const double s = std::sin(halfOmega);
    return 4.0 * s * s;
}

/// SC-002 (c)'s injection bound - **MEASURED, spec correction C-17**, not the
/// spec's original 10. Systematic injection, not one-shot (correction C-12):
/// three outliers cannot move a 135 000-sample 99.9th percentile at all.
///
/// Why 10 was unreachable BY ANY IMPLEMENTATION, and what replaced it: an
/// anchor jump does not put a lone spike on the boundary sample and then stop.
/// It re-tunes a resonator, and the resonator RINGS - broadband, for the rest
/// of the chunk and beyond - so each jump lifts ~3 boundary samples AND the ~61
/// interior samples behind them. The two populations are in the same 3 : 61
/// ratio as the partition itself, so a jump raises the 0.1 % tail of BOTH at
/// the same rate and B/P converges on the per-sample contrast between the
/// coefficient-switch step and the ring it excites - not on the number of
/// jumps. Measured across three seeds on the linear fixture below:
///     schedule                B/P (0x5C02A001 / 0x11111111 / 0xDEADBEEF)
///     every 32 chunks, alt.   4.21 / 3.75 / 5.09
///     every 16 chunks, coin   4.01 / 4.00 / 4.66   <-- the arm below
///     every 64 chunks, alt.   3.89 / 3.55 / 4.06
/// against a null (the (a) arm) of 1.010 / 0.992 / 1.023. Making the jump
/// bigger or more frequent moves it DOWN, not up (x8 jumps: 2.77; every 4
/// chunks: 1.32), because the extra ring dominates. So the bound is set the way
/// kBoundaryRatio's is - below the observed minimum with margin - at a value
/// that still sits 3x above the null and 2.7x above kBoundaryRatio (1.11 since
/// that constant was measured on 2026-09-11), i.e. an
/// injection this arm passes is one the (a) arm would have gone RED on, which
/// is the whole purpose of the arm.
constexpr float kInjectionRatio = 3.0f;

/// SC-002's wet trim, and the reason the fixture does not run at FR-045's
/// kDefaultWetGainDb - now MEASURED at +34.5 dB (T019), 4.5 dB hotter still
/// than the +30 dB the figures below were taken at, so the argument only
/// hardens (spec correction C-17): at +30 dB a
/// 220 Hz sine parked on twelve resonances with the gain lane at its 24 dB
/// maximum drives 16-21 % of the render into FR-018's +/- 4.0 clamp (measured
/// 599 219 / 520 614 / 465 382 engagements across the three seeds), and a
/// clipped render's second difference is the CLIPPER's, in both populations at
/// once - which drags B/P to 1.06 and makes the (a) arm unable to fail. Every
/// statistic SC-002 defines is a ratio and is therefore trim-invariant WHILE
/// THE RENDER IS LINEAR, so the trim is dropped until it provably is: all three
/// arms assert getClampEngagementCount() == 0. -12 dB clears the clamp on every
/// arm and every seed measured (worst peak 0.23 of 4.0).
constexpr float kZipperWetGainDb = -12.0f;

/// Rendered and DISCARDED before the pinned 60 s. FR-045's wet trim rides the
/// same LinearRamp as FR-019's normalisation (kGainRampMs, 50 ms) and
/// configureZipperPatch necessarily calls setWetGain AFTER prepare, so the
/// first 50 ms of an unsettled render is louder than the patch asks for - 2 400
/// samples, i.e. 0.08 % of the render, which lands right on top of the 0.1 %
/// tail both statistics are drawn from. One second of settling puts that
/// transient, and the initial ring-up of twelve resonators, outside the
/// measurement instead of inside its tail.
constexpr std::size_t kZipperSettleChunks = 750;

/// B and P are THE SAME quantile of their own population. A max-vs-percentile
/// comparison would put B at the ~99.9989th quantile for this length and go red
/// from extreme-value statistics alone (spec.md's SC-002 rewrite paragraph).
constexpr double kZipperQuantile = 0.999;

/// -80 dBFS, Membrum's infinite-ring threshold
/// (plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:59).
constexpr float kSilenceThreshold = 1.0e-4f;

/// Nearest-rank percentile (the `seraphis_engine_test.cpp:3628` helper). Takes
/// its input BY VALUE because it sorts.
[[nodiscard]] float percentileOf(std::vector<float> values, double p) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const double rank = std::ceil(p * static_cast<double>(values.size()));
    const auto clamped = static_cast<std::size_t>(std::max(1.0, rank));
    return values[std::min(clamped, values.size()) - 1u];
}

/// SC-002's two statistics plus the population sizes they were drawn from - the
/// sizes are asserted by the caller, because a partition that silently drifted
/// off the control grid would still produce two plausible-looking numbers.
struct ZipperStats {
    float boundary = 0.0f;  ///< B: 99.9th percentile of d over n mod 64 in {0,1,2}
    float interior = 0.0f;  ///< P: 99.9th percentile of d over the other 61/64
    /// A: the SAME quantile of |x[n-1]| over the interior population - the
    /// amplitude P is divided by to make it a level-INVARIANT curvature. Drawn
    /// from the same samples as P, so no assumption about the two populations
    /// lining up is needed.
    float interiorAmplitude = 0.0f;
    std::size_t boundaryCount = 0;
    std::size_t interiorCount = 0;
    /// FR-018 engagements over the whole render. A render that reached the
    /// clamp is a measurement of the CLAMP, not of the network: the interior
    /// identity below is a statement about a linear render and is checked only
    /// after this is confirmed zero.
    std::uint32_t clampEngagements = 0;
};

/// SC-002's estimator: `d[n] = |x[n] - 2x[n-1] + x[n-2]|`, split into the
/// boundary and interior populations in ONE streaming pass.
///
/// The residue set is {0, 1, 2} and NOT {0, 1} (spec correction C-11): the
/// control step runs at controlPhase_ == 0, so the first sample it can affect is
/// n = 0 (mod 64), and a second difference spans three samples - d[n], d[n+1]
/// and d[n+2] each touch it. Leaving residue 2 in the interior puts
/// discontinuity-carrying samples (1.6 % of that population) into the 0.1 % tail
/// P is drawn from, which lifts P and collapses B/P toward 1 on a build that
/// really does zipper.
class SecondDifferenceSplit {
public:
    explicit SecondDifferenceSplit(std::size_t totalSamples) {
        boundary_.reserve((totalSamples * 3u) / kChunk + 8u);
        interior_.reserve((totalSamples * 61u) / kChunk + 8u);
        interiorAmp_.reserve((totalSamples * 61u) / kChunk + 8u);
    }

    /// Seed the two-sample history from a render whose statistic is not being
    /// collected (the settle pass), so the measured population starts complete
    /// at n = 0 instead of losing its first two samples.
    void prime(float twoAgo, float oneAgo) {
        prev2_ = twoAgo;
        prev1_ = oneAgo;
        primed_ = true;
    }

    void push(std::size_t n, float x) {
        if (primed_ || n >= 2u) {
            const float d = std::abs(x - 2.0f * prev1_ + prev2_);
            if ((n % kChunk) < 3u) {
                boundary_.push_back(d);
            } else {
                interior_.push_back(d);
                // The amplitude this d is a curvature OF, taken from the same
                // sample: for a pure sine d[n] = 4 sin^2(w/2) * |x[n-1]| exactly.
                interiorAmp_.push_back(std::abs(prev1_));
            }
        }
        prev2_ = prev1_;
        prev1_ = x;
    }

    [[nodiscard]] ZipperStats finish() const {
        ZipperStats s;
        s.boundary = percentileOf(boundary_, kZipperQuantile);
        s.interior = percentileOf(interior_, kZipperQuantile);
        s.interiorAmplitude = percentileOf(interiorAmp_, kZipperQuantile);
        s.boundaryCount = boundary_.size();
        s.interiorCount = interior_.size();
        return s;
    }

private:
    std::vector<float> boundary_;
    std::vector<float> interior_;
    std::vector<float> interiorAmp_;
    float prev1_ = 0.0f;
    float prev2_ = 0.0f;
    bool primed_ = false;
};

/// Drive `net` with a steady sine at `rmsDbfs`, identically on both channels
/// (the Success Criteria section's stereo drive convention), for exactly
/// `totalSamples` samples, and accumulate SC-002's split second difference on the
/// LEFT output channel (`x[n] = outL[n]`, spec.md).
///
/// `perChunk(chunkIndex)` runs immediately BEFORE each block - i.e. on the
/// control-step edge.
template <typename PerChunk>
[[nodiscard]] ZipperStats renderSineZipperStatistic(ResonanceDriftNetwork& net, double sineHz,
                                                    float rmsDbfs, std::size_t totalSamples,
                                                    const PerChunk& perChunk) {
    std::array<float, kChunk> inL{};
    std::array<float, kChunk> inR{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};

    SecondDifferenceSplit split(totalSamples);

    const double twoPi = 2.0 * static_cast<double>(Krate::DSP::kPi);
    const double increment = twoPi * sineHz / kFs;
    // -12 dBFS is an RMS figure here, as everywhere else in this phase's tests;
    // a sine's peak is sqrt(2) above its RMS.
    const float amplitude = Krate::DSP::dbToGain(rmsDbfs) * 1.41421356f;

    double phase = 0.0;
    std::size_t n = 0;
    // The settle chunks are rendered exactly like the measured ones - the
    // perChunk hook included, so an injection schedule is already running when
    // the measurement opens - and only the statistic is withheld. The two
    // trailing samples of the last settle chunk seed the second difference, so
    // the measured population is the FULL 3/64 and 61/64 of the pinned render
    // rather than losing n = 0 and n = 1.
    const std::size_t chunks = kZipperSettleChunks + totalSamples / kChunk;
    for (std::size_t c = 0; c < chunks; ++c) {
        perChunk(c);
        for (std::size_t i = 0; i < kChunk; ++i) {
            inL[i] = static_cast<float>(std::sin(phase)) * amplitude;
            inR[i] = inL[i];
            phase += increment;
            if (phase >= twoPi) {
                phase -= twoPi;  // wrapped, so 2.88e6 samples cost no precision
            }
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
        if (c < kZipperSettleChunks) {
            split.prime(outL[kChunk - 2u], outL[kChunk - 1u]);
            continue;
        }
        for (std::size_t i = 0; i < kChunk; ++i) {
            split.push(n, outL[i]);
            ++n;
        }
    }
    ZipperStats stats = split.finish();
    stats.clampEngagements = net.getClampEngagementCount();
    return stats;
}

/// SC-001's patch: 12 peaks, every peakQ pinned at the shipped ceiling, FR-016's
/// anchors untouched, mix = 1. `maxDepths` selects the (e) worst-case arm - all
/// four lane depths at their maxima and wanderRate at its 1.0 Hz ceiling.
void configureMaxQPatch(ResonanceDriftNetwork& net, bool maxDepths, std::uint32_t seed) {
    net.setSeed(seed);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        net.setPeakQ(i, Krate::DSP::kMaxResonatorQ);
        // The base arm pins Q at the ceiling, so its Q lane depth is zero; the
        // (e) arm opens all four lanes instead.
        net.setQWander(i, maxDepths ? ResonanceDriftNetwork::kMaxQWanderOctaves : 0.0f);
        if (maxDepths) {
            net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
            net.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
            net.setPeakPanWander(i, 1.0f);
        }
    }
    net.setWanderRate(maxDepths ? ResonanceDriftNetwork::kMaxWanderRateHz
                                : ResonanceDriftNetwork::kDefaultWanderRateHz);
    net.setMix(1.0f);
}

/// What SC-001 (a)(b)(d) read off one drive-then-silence render.
struct RingOutMeasurement {
    bool allFinite = true;      ///< (a), over the DRIVE and the TAIL
    float drivePeak = 0.0f;     ///< non-vacuity: two silent renders are finite too
    float driveRmsDb = -300.0f;
    double lastLoudSeconds = 0.0;  ///< (d): when the tail last exceeded -80 dBFS
    bool everSilent = false;       ///< false if the tail was still ringing at the end
};

/// Drive with -12 dBFS white noise on BOTH channels for `driveSeconds`, then
/// render `tailSeconds` of digital silence and report when the tail last
/// exceeded -80 dBFS.
///
/// The noise is drawn block-wise from ONE persistent generator - re-seeding per
/// block would make the excitation periodic and comb the very spectrum a
/// max-Q bank is most sensitive to.
[[nodiscard]] RingOutMeasurement renderRingOut(ResonanceDriftNetwork& net,
                                               std::uint32_t noiseSeed, double driveSeconds,
                                               double tailSeconds) {
    RingOutMeasurement m;
    Krate::DSP::Xorshift32 rng{noiseSeed};

    std::array<float, kChunk> inL{};
    std::array<float, kChunk> inR{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};

    // Xorshift32::nextFloat() is uniform on [-1, 1] (random.h:59-63), RMS
    // 1/sqrt(3), hence the sqrt(3) scale for a -12 dBFS RMS drive.
    const float scale = Krate::DSP::dbToGain(-12.0f) * 1.7320508f;

    double sumSq = 0.0;
    std::size_t counted = 0;
    const auto driveChunks = static_cast<std::size_t>(driveSeconds * kFs) / kChunk;
    for (std::size_t c = 0; c < driveChunks; ++c) {
        for (std::size_t i = 0; i < kChunk; ++i) {
            inL[i] = rng.nextFloat() * scale;
            inR[i] = rng.nextFloat() * scale;
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
        for (std::size_t i = 0; i < kChunk; ++i) {
            // Scanned WITHOUT a per-sample REQUIRE: 2.88e6 samples would
            // otherwise register millions of assertions.
            if (!Krate::DSP::detail::isFinite(outL[i])
                || !Krate::DSP::detail::isFinite(outR[i])) {
                m.allFinite = false;
            }
            m.drivePeak = std::max(m.drivePeak, std::abs(outL[i]));
            sumSq += static_cast<double>(outL[i]) * static_cast<double>(outL[i]);
            ++counted;
        }
    }
    if (counted > 0u) {
        const double rms = std::sqrt(sumSq / static_cast<double>(counted));
        m.driveRmsDb = static_cast<float>(20.0 * std::log10(rms + 1.0e-30));
    }

    // The tail, measured in ~10.7 ms windows (8 control chunks) so the reported
    // silence time has finer resolution than the 5 s margin in (d)'s bound.
    constexpr std::size_t kWindowChunks = 8;
    const double windowSeconds = static_cast<double>(kWindowChunks * kChunk) / kFs;
    const auto tailChunks = static_cast<std::size_t>(tailSeconds * kFs) / kChunk;

    std::vector<float> windowPeaks;
    windowPeaks.reserve(tailChunks / kWindowChunks + 1u);
    inL.fill(0.0f);
    inR.fill(0.0f);
    float windowPeak = 0.0f;
    for (std::size_t c = 0; c < tailChunks; ++c) {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
        for (std::size_t i = 0; i < kChunk; ++i) {
            if (!Krate::DSP::detail::isFinite(outL[i])
                || !Krate::DSP::detail::isFinite(outR[i])) {
                m.allFinite = false;
            }
            windowPeak = std::max({windowPeak, std::abs(outL[i]), std::abs(outR[i])});
        }
        if (((c + 1u) % kWindowChunks) == 0u) {
            windowPeaks.push_back(windowPeak);
            windowPeak = 0.0f;
        }
    }

    // Scanned from the front for the LAST loud window, so the reported figure is
    // "the time after which the tail STAYS below -80 dBFS" rather than the first
    // moment it happens to dip under it.
    std::size_t lastLoud = 0;
    bool anyLoud = false;
    for (std::size_t w = 0; w < windowPeaks.size(); ++w) {
        if (windowPeaks[w] >= kSilenceThreshold) {
            lastLoud = w;
            anyLoud = true;
        }
    }
    m.lastLoudSeconds = anyLoud ? (static_cast<double>(lastLoud + 1u) * windowSeconds) : 0.0;
    m.everSilent = !anyLoud || ((lastLoud + 1u) < windowPeaks.size());
    return m;
}

/// SC-001 (c): the SAME patch with peaks 4-11 dormant in BOTH arms, so the two
/// renders carry identical acoustic content and the only difference between them
/// is FR-019's `N`. Returns the left-channel RMS in dBFS over the settled window.
[[nodiscard]] float renderNormalisationLevelDb(std::size_t numPeaks, bool maxDepths,
                                               std::uint32_t seed, std::uint32_t noiseSeed) {
    ResonanceDriftNetwork net;
    configureMaxQPatch(net, maxDepths, seed);
    for (std::size_t i = 4; i < kMaxPeaks; ++i) {
        net.setPeakDormant(i, true);
    }
    // Set before the render so the FR-019 wet-scale LinearRamp is long settled
    // by the time the measurement window opens.
    net.setNumPeaks(numPeaks);

    Krate::DSP::Xorshift32 rng{noiseSeed};
    std::array<float, kChunk> inL{};
    std::array<float, kChunk> inR{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    const float scale = Krate::DSP::dbToGain(-12.0f) * 1.7320508f;

    constexpr std::size_t kSettleChunks = 1500;   // 2 s
    constexpr std::size_t kMeasureChunks = 4500;  // 6 s
    double sumSq = 0.0;
    std::size_t counted = 0;
    for (std::size_t c = 0; c < kSettleChunks + kMeasureChunks; ++c) {
        for (std::size_t i = 0; i < kChunk; ++i) {
            inL[i] = rng.nextFloat() * scale;
            inR[i] = rng.nextFloat() * scale;
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
        if (c < kSettleChunks) {
            continue;
        }
        for (std::size_t i = 0; i < kChunk; ++i) {
            sumSq += static_cast<double>(outL[i]) * static_cast<double>(outL[i]);
            ++counted;
        }
    }
    if (counted == 0u) {
        return -300.0f;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(counted));
    return static_cast<float>(20.0 * std::log10(rms + 1.0e-30));
}

/// SC-002's anchor spread: 110 Hz to 440 Hz geometric, so the 220 Hz drive sits
/// in the middle of the twelve peaks and every one of them crosses it as the
/// lanes wander ("12 peaks with anchors spread across it", spec.md).
[[nodiscard]] float zipperAnchorHz(std::size_t i) {
    return 110.0f * std::exp2(2.0f * static_cast<float>(i) / static_cast<float>(kMaxPeaks - 1u));
}

/// SC-002's patch: all four lanes at maximum depth and wanderRate at its 1.0 Hz
/// ceiling, which is the fastest legal retuning AND the un-decimated lane path
/// (FR-037, decimation == 1) this criterion exists to stress.
void configureZipperPatch(ResonanceDriftNetwork& net, std::uint32_t seed) {
    net.setSeed(seed);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        net.setPeakAnchorHz(i, zipperAnchorHz(i));
        net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
        net.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
        net.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
        net.setPeakPanWander(i, 1.0f);
    }
    net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
    net.setMix(1.0f);
    net.setWetGain(kZipperWetGainDb);  // C-17, see the constant
}

// =============================================================================
// T018 machinery - SC-003, SC-015 (b)(f)(g), SC-016
//
// Everything below is shared by the three [long] cases. It is declared in this
// same anonymous namespace, ahead of ResonanceDriftNetwork_PeakLifeCycle,
// because T018 EXTENDS that case with arms (b), (f) and (g) rather than adding
// a fourth one - SC-015 names ResonanceDriftNetwork_PeakLifeCycle as its
// measuring case and compliance (T024) cites it.
// =============================================================================

/// Pink noise at a chosen RMS in dBFS, deterministic under its seed.
///
/// SC-003 and SC-016 both specify "pink noise at -12 dBFS" as the drive.
/// PinkNoiseFilter (primitives/pink_noise_filter.h:68) is Kellet's filter with
/// its poles mapped to the running rate by prepare() (:109), so the 1/f region
/// sits in the same PLACE IN HZ at 8 kHz as at 48 kHz - which is what lets the
/// lane fixtures below run at kMinUsableSampleRate without changing what the
/// resonators see.
///
/// The RMS is CALIBRATED rather than assumed: Kellet's gains are published for
/// a particular white-noise convention and the realised RMS of
/// filter(Xorshift32::nextFloat()) is not 1/sqrt(3) or any other round number.
/// A private copy of the same stream is run for one calibration pass, the
/// measured RMS becomes the scale, and the live generator then starts from the
/// seed again - so `next()` really does deliver the requested dBFS and the
/// SC-016 window bounds are statements about the network rather than about an
/// unmeasured drive level.
class PinkDrive {
public:
    PinkDrive(std::uint32_t seed, double sampleRate, float rmsDbfs) noexcept : rng_(seed) {
        filter_.prepare(static_cast<float>(sampleRate));

        Krate::DSP::Xorshift32 calibrationRng{seed};
        Krate::DSP::PinkNoiseFilter calibrationFilter;
        calibrationFilter.prepare(static_cast<float>(sampleRate));
        constexpr std::size_t kCalibrationSamples = 200000;  // ~4 s at 48 kHz
        double sumSq = 0.0;
        for (std::size_t i = 0; i < kCalibrationSamples; ++i) {
            const auto v = static_cast<double>(calibrationFilter.process(calibrationRng.nextFloat()));
            sumSq += v * v;
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(kCalibrationSamples));
        scale_ = (rms > 1.0e-12)
                     ? static_cast<float>(static_cast<double>(Krate::DSP::dbToGain(rmsDbfs)) / rms)
                     : 1.0f;
    }

    [[nodiscard]] float next() noexcept { return filter_.process(rng_.nextFloat()) * scale_; }

    /// The scale the calibration pass produced - CAPTUREd by the cases so a
    /// silent or exploding drive is visible in the failure output.
    [[nodiscard]] float getScale() const noexcept { return scale_; }

private:
    Krate::DSP::PinkNoiseFilter filter_{};
    Krate::DSP::Xorshift32 rng_;
    float scale_ = 1.0f;
};

[[nodiscard]] double meanOf(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float x : v) {
        sum += static_cast<double>(x);
    }
    return sum / static_cast<double>(v.size());
}

/// SC-003's "motion metric": the coefficient of variation of a trajectory.
/// Returns 0 for a degenerate input rather than a non-finite value - the caller
/// asserts non-vacuity separately, and a NaN here would poison every CAPTURE.
[[nodiscard]] double coefficientOfVariation(const std::vector<float>& v) {
    const double mean = meanOf(v);
    if (v.size() < 2u || std::abs(mean) < 1.0e-12) {
        return 0.0;
    }
    double acc = 0.0;
    for (const float x : v) {
        const double d = static_cast<double>(x) - mean;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(v.size() - 1u)) / std::abs(mean);
}

/// Normalised sample autocorrelation at `lag`, sample mean removed.
///
/// The BIASED estimator - ONE denominator for every lag - deliberately the same
/// one T008's ResonanceDriftNetwork_LaneBounds (b) uses
/// (resonance_drift_network_test.cpp:1160), so the two criteria's numbers are
/// comparable. The result is in [-1, +1] by construction and accumulates in
/// double: the frequency records below are 2.6e5 samples of a value near 5.3
/// (log2 of a few hundred Hz) and a float sum of squares loses the mean-removed
/// tail.
[[nodiscard]] double autocorrelationAt(const std::vector<float>& x, std::size_t lag) {
    const std::size_t n = x.size();
    if (n <= lag * 2u) {
        return 0.0;  // meaningless, not merely noisy - the caller REQUIREs the length
    }
    const double mean = meanOf(x);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(x[i]) - mean;
        den += d * d;
        if (i + lag < n) {
            num += d * (static_cast<double>(x[i + lag]) - mean);
        }
    }
    return (den > 0.0) ? (num / den) : 0.0;
}

/// Pearson correlation of two equal-length trajectories, means removed inside.
[[nodiscard]] double pearsonR(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2u) {
        return 0.0;
    }
    double ma = 0.0;
    double mb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ma += static_cast<double>(a[i]);
        mb += static_cast<double>(b[i]);
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);

    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = static_cast<double>(a[i]) - ma;
        const double db = static_cast<double>(b[i]) - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    const double den = std::sqrt(saa * sbb);
    return (den > 0.0) ? (sab / den) : 0.0;
}

[[nodiscard]] float rmsDbfsOf(double sumSq, std::size_t count) {
    if (count == 0u) {
        return -300.0f;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(count));
    return static_cast<float>(20.0 * std::log10(rms + 1.0e-30));
}

/// Advance `steps` control steps on a silent drive. Used by the arms that
/// measure the LANES (SC-003 (d)'s long lag and SC-003 (f)) rather than audio:
/// the lanes advance for every peak regardless of input (FR-036), so silence is
/// the cheapest correct excitation and removes the drive as a variable.
void advanceControlStepsSilent(ResonanceDriftNetwork& net, std::size_t steps) {
    std::array<float, kChunk> zeros{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    for (std::size_t s = 0; s < steps; ++s) {
        net.processBlock(zeros.data(), zeros.data(), outL.data(), outR.data(), kChunk);
    }
}

/// log2 of every peak's realised frequency, sampled once per control step on a
/// silent drive (the recordFreqLog2 shape from
/// resonance_drift_network_test.cpp:1141).
void recordFreqLog2Silent(ResonanceDriftNetwork& net, std::size_t steps,
                          std::array<std::vector<float>, kMaxPeaks>& out) {
    std::array<float, kChunk> zeros{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    for (std::size_t s = 0; s < steps; ++s) {
        net.processBlock(zeros.data(), zeros.data(), outL.data(), outR.data(), kChunk);
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            out[i].push_back(std::log2(net.getPeakCurrentFrequency(i)));
        }
    }
}

// -----------------------------------------------------------------------------
// The lane fixture: SC-003 (d)'s long lag and SC-003 (f)'s rate separation
// -----------------------------------------------------------------------------
//
// WHY THESE TWO ARMS DO NOT RUN ON SC-003's OWN 350 s RENDER, spelled out
// because it is the one place this task departs from the obvious reading.
//
// The sample autocorrelation of an OU walk at a lag well past its correlation
// time has standard error ~ sqrt(tau / recordLength) - the arithmetic T008's
// LaneBounds (b) states and satisfies (resonance_drift_network_test.cpp:1279-
// 1285). SC-003's own configuration is the 0.03 Hz DEFAULT, i.e. tau = 33.3 s,
// and its render is 10*T = 350 s, so sqrt(33.3 / 350) = 0.31: at the lag-8T
// bound of 0.10 the ESTIMATOR's own spread is three times the bound, for every
// one of the twelve peaks at once. No implementation can pass that reliably and
// a build that did would be passing on luck. The lag-T/8 half is unaffected -
// there the true value is exp(-1/8) = 0.88 and Bartlett's variance for an AR(1)
// at that lag is (tau/dt)(1 - a^(2k))/N = 0.021, i.e. sd 0.145, so the 0.20
// floor sits 4.7 sd away - and it IS asserted on the 350 s render, at the
// criterion's own rate.
//
// The decorrelation half therefore runs on a dedicated record with the
// statistical power to carry it: the T008 (b) fixture verbatim - 1200 s at
// kMinUsableSampleRate with wanderRate at its 1.0 Hz ceiling, so tau = 1 s and
// sqrt(1 / 1200) = 0.029, putting the 0.10 bound 3.4 sd away. Same component,
// same lanes, same estimator; only the record is long enough relative to tau.
// SC-003 (f) uses the same fixture for the same reason: its slow arm is
// T = 200 s and needs 10*T = 2000 s of record.

/// The lane arms' rate. Sample-rate independent by construction: FR-037's lane
/// advance takes a FIXED kControlChunkSamples argument, so the decimation
/// mapping and the realised correlation time in SECONDS are identical at any
/// rate, and 8000 / 64 = 125 control steps per second exactly - no rounding in
/// the lag arithmetic.
constexpr double kLaneFs = ResonanceDriftNetwork::kMinUsableSampleRate;
constexpr std::size_t kLaneStepsPerSecond = static_cast<std::size_t>(kLaneFs) / kChunk;
static_assert(kLaneStepsPerSecond == 125, "the lane lag arithmetic assumes an exact division");

/// The T008 (b) lane fixture. The slew ceilings are lifted out of the way so
/// these arms measure the LANE and not the FR-035 limiter, and +/- 6 semitones
/// keeps every FR-016 anchor clear of the [20 Hz, 0.45*fs] clamp at 8 kHz -
/// saturation against it would inflate the measured persistence of the lowest
/// peaks and hide a stalled lane.
void configureLaneFixture(ResonanceDriftNetwork& net, std::uint32_t seed, float wanderRateHz) {
    net.setSeed(seed);
    net.prepare(kLaneFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setSlewCeilings(ResonanceDriftNetwork::kMaxSlewOctaves,
                        ResonanceDriftNetwork::kMaxSlewOctaves);
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        net.setFreqWander(i, 6.0f);
    }
    net.setWanderRate(wanderRateHz);
}

// -----------------------------------------------------------------------------
// SC-003's main render
// -----------------------------------------------------------------------------

/// T = 1 / wanderRate at the FR-016 default: 33.333 s.
constexpr double kSpectralT = 1.0 / static_cast<double>(ResonanceDriftNetwork::kDefaultWanderRateHz);

/// SC-003's "at least 10*T", pinned at the spec's own 350 s.
constexpr double kSpectralSeconds = 350.0;

/// SC-003's "every 100 ms extract the five band fractions". 4800 samples at
/// 48 kHz, and an exact number of control chunks so a feature window never
/// straddles a partial one.
constexpr std::size_t kFeatureWindowSamples = 4800;
static_assert(kFeatureWindowSamples % kChunk == 0u, "a feature window is whole control chunks");
constexpr std::size_t kFeatureWindowChunks = kFeatureWindowSamples / kChunk;  // 75

constexpr std::size_t kSpectralChunks =
    static_cast<std::size_t>(kSpectralSeconds * kFs) / kChunk;  // 262 500
static_assert(kSpectralChunks % kFeatureWindowChunks == 0u,
              "the render must be a whole number of feature windows");
constexpr std::size_t kSpectralWindows = kSpectralChunks / kFeatureWindowChunks;  // 3 500

/// SC-003's band-eligibility floor: a band whose mean fraction is below this
/// carries essentially no energy, and a fraction computed on a near-zero
/// denominator is numerically unstable - which is exactly what a naive
/// "strongest CV" selection would pick (spec.md's SC-003 band-selection rule).
constexpr double kBandEligibilityFraction = 0.01;

/// SC-003 (a): the Phase 2-measured persistence floor.
constexpr double kBandPersistence = 0.20;

/// SC-003 (b): the control-arm multiplier. **MEASURED, 2026-09-11**, replacing
/// the 1.8 transcribed from Phase 2 - which spec.md's Bound provenance note
/// binds ("any threshold in this section that proves unable to separate a
/// correct implementation from an injected defect must be re-derived from a
/// measured distribution across seeds and the change recorded, never merely
/// widened"), and which this build showed cannot: the shipped seed reads 1.397
/// on a build every other arm of SC-003 passes.
///
/// The null across eight network seeds (ResonanceDriftNetwork_MeasureWanderCvRatio,
/// [.calibration], this TU): cvOn mean 0.481 (sd 0.034), cvOff mean 0.312
/// (sd 0.050), ratio mean 1.592, sd 0.386, min 1.257, max 2.333.
///
/// The admissible interval is NARROW and bounded on BOTH sides, which is why
/// the usual "observed extreme minus three sigma" is not used here:
///   * ABOVE 1.000, because a build whose wander is inert makes the two arms
///     the SAME render and reads exactly 1.0 - that is the defect this arm
///     exists to catch, and a bound at or below 1.0 cannot catch it;
///   * BELOW 1.257, the observed minimum of the null.
/// 1.10 is inside that interval, 0.157 below the observed minimum and 0.10
/// above the vacuity floor. The consuming arm's seed is PINNED (kNetSeed), so
/// this is a fixed measurement and not a per-run draw; the residual risk is a
/// future seed change, which the recorded distribution above makes checkable.
///
/// Why the control arm is noisy at all: FR-034 disables wander by zeroing the
/// DEPTHS, so the wander-off render is still twelve resonators on the same
/// stochastic pink drive and its band-fraction CV is dominated by that drive.
/// Phase 2's 1.8 was measured on a different component, a different band
/// selection and a different control arm.
constexpr double kWanderCvRatio = 1.10;

/// SC-003 (d): the same two Phase 2-measured bounds T008's LaneBounds (b) uses.
constexpr double kLanePersistence = 0.20;
constexpr double kLaneDecorrelation = 0.10;

/// SC-003 (e). **MEASURED, 2026-09-11**, across TWELVE network seeds as the
/// spec requires: mean |r| over the 66 pairs read mean 0.2420, sd 0.0396, min
/// 0.1928, max 0.3351 - within 5 % of Bartlett's analytic E|r| ~ 0.23 for this
/// record length (var(r) ~ (tau/dt)/N = 25000/262500 = 0.095, sd 0.31,
/// E|r| = sd*sqrt(2/pi)), i.e. the estimator is reading its own noise floor and
/// nothing else. The derived bound is observed max + 3 sd = 0.4540; 0.45 was
/// carried in on the analytic argument and the measurement confirms it to
/// within 0.004, so it stands unchanged - measured, not assumed. Phase 2's 0.05
/// may NOT be transcribed here (it was measured on 10 s of broadband AUDIO, a
/// different statistic on a different population) and is unsatisfiable by a
/// correct build at this record length.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-003 (e) kLaneIndependenceR" (resonance_drift_network_perf_test.cpp,
/// [.calibration]).
constexpr double kLaneIndependenceR = 0.45;

/// SC-003 (e)'s anti-vacuity control: the identical estimator on twelve lanes
/// built from ONE seed and ONE salt must read ~1. Without it the criterion
/// cannot distinguish "the FR-005 salts work" from "the estimator is noise".
constexpr double kLaneControlR = 0.95;

/// SC-003 (f). **MEASURED, 2026-09-11**, across the eight calibration seeds:
/// rho_slow mean 0.9880 (sd 0.0014), rho_fast mean 0.6051 (sd 0.0061),
/// separation mean 0.3830, sd 0.0056, min 0.3734, max 0.3938 - within 0.002 of
/// the closed form (exp(-1.67/200) - exp(-1.67/3.33) = 0.385). The bound is the
/// calibration's own suggestion, observed minimum - 3 sd = 0.3361 rounded to
/// 0.33, which TIGHTENS the 0.25 carried in and stays far above the ~0 a build
/// with FR-037's dead zone still in place would produce.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-003 (f) kRateSeparation" (resonance_drift_network_perf_test.cpp,
/// [.calibration]). It is a LOWER bound: the suggestion sits BELOW the observed
/// minimum, not above the observed maximum.
constexpr double kRateSeparation = 0.33;

/// SC-003 (f)'s lag-matched evaluation point: T/2 of the fast arm.
constexpr double kRateSeparationLagSeconds = 1.67;

/// SC-003's wet trim. FR-045's kDefaultWetGainDb is +34.5 dB, MEASURED at
/// T019. Every statistic SC-003 defines is either a band-energy
/// FRACTION (normalised, hence trim-invariant while the render is linear) or a
/// control-surface reading (trim-independent outright), so the trim is dropped
/// to unity for the same reason SC-002's fixture drops it (spec correction
/// C-17): a render that reaches FR-018's +/- 4.0 clamp is a measurement of the
/// CLAMP, and a clipped band fraction is not the network's. The case REQUIREs
/// getClampEngagementCount() == 0, so the invariance is a checked fact rather
/// than an assumption.
constexpr float kSpectralWetGainDb = 0.0f;

/// What one SC-003 render produces.
struct SpectralRun {
    /// The five AudioFeatures::band fractions (audio_features.h:28-29), one
    /// entry per 100 ms window.
    std::array<std::vector<float>, 5> band{};
    /// log2 of each peak's realised frequency, one entry per control step.
    /// Populated only when the caller asks for it - the control arm does not
    /// need 12.6 MB of trajectory.
    std::array<std::vector<float>, kMaxPeaks> freqLog2{};
    std::size_t samplings = 0;        ///< SC-003 (c)'s ">= 1e5 samplings"
    std::size_t freqExcursions = 0;   ///< SC-003 (c): must be zero
    std::size_t qExcursions = 0;      ///< SC-003 (c): must be zero
    /// Non-vacuity for (c): a build whose lanes never advance has zero
    /// excursions too, so the realised span of each peak is recorded.
    std::array<float, kMaxPeaks> spanOctaves{};
    bool allFinite = true;
    std::uint32_t clampEngagements = 0;
};

/// Render SC-003's configuration for the pinned 350 s, extracting the band
/// fractions every 100 ms and sampling the FR-030/FR-031 bounds every control
/// step in the same pass.
///
/// The bounds are built from the network's OWN configured depths, read back
/// through the public getters, so the arm cannot drift out of step with the
/// patch it is asserting about. setWanderEnabled(false) zeroes the DEPTHS and
/// not the configuration (FR-034), so the control arm's readings sit at the
/// anchor and inside the same bounds - which is why the control arm is scanned
/// with the identical predicate.
[[nodiscard]] SpectralRun renderSpectralRun(ResonanceDriftNetwork& net, std::uint32_t driveSeedL,
                                            std::uint32_t driveSeedR, bool recordTrajectories) {
    PinkDrive driveL(driveSeedL, kFs, -12.0f);
    PinkDrive driveR(driveSeedR, kFs, -12.0f);

    std::array<float, kMaxPeaks> loHz{};
    std::array<float, kMaxPeaks> hiHz{};
    std::array<float, kMaxPeaks> loQ{};
    std::array<float, kMaxPeaks> hiQ{};
    const auto nyquistLimit =
        static_cast<float>(Krate::DSP::kMaxResonatorFrequencyRatio * static_cast<float>(kFs));
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        const float anchor = net.getPeakAnchorHz(i);
        const float semis = net.getFreqWander(i);
        loHz[i] = std::max(anchor * std::exp2(-semis / 12.0f), Krate::DSP::kMinResonatorFrequency);
        hiHz[i] = std::min(anchor * std::exp2(semis / 12.0f), nyquistLimit);
        const float baseQ = net.getPeakQ(i);
        const float octaves = net.getQWander(i);
        loQ[i] = std::max(baseQ * std::exp2(-octaves), Krate::DSP::kMinResonatorQ);
        hiQ[i] = std::min(baseQ * std::exp2(octaves), Krate::DSP::kMaxResonatorQ);
    }

    SpectralRun run;
    for (auto& v : run.band) {
        v.reserve(kSpectralWindows);
    }
    if (recordTrajectories) {
        for (auto& v : run.freqLog2) {
            v.reserve(kSpectralChunks);
        }
    }
    std::array<float, kMaxPeaks> minLog2{};
    std::array<float, kMaxPeaks> maxLog2{};
    bool firstSample = true;

    std::array<float, kChunk> inL{};
    std::array<float, kChunk> inR{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    std::vector<float> window;
    window.reserve(kFeatureWindowSamples);

    // 1e-4 relative is 0.17 cents: float rounding on an exp2 of a clamped log2,
    // and nothing else (the T008 LaneBounds (a) tolerance).
    constexpr float kRel = 1.0e-4f;

    for (std::size_t c = 0; c < kSpectralChunks; ++c) {
        for (std::size_t i = 0; i < kChunk; ++i) {
            inL[i] = driveL.next();
            inR[i] = driveR.next();
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
        for (std::size_t i = 0; i < kChunk; ++i) {
            // Scanned WITHOUT a per-sample REQUIRE: 1.7e7 samples would
            // otherwise register tens of millions of assertions.
            if (!Krate::DSP::detail::isFinite(outL[i])
                || !Krate::DSP::detail::isFinite(outR[i])) {
                run.allFinite = false;
            }
            window.push_back(outL[i]);
        }
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float f = net.getPeakCurrentFrequency(i);
            const float q = net.getPeakCurrentQ(i);
            ++run.samplings;
            if (f < loHz[i] * (1.0f - kRel) || f > hiHz[i] * (1.0f + kRel)) {
                ++run.freqExcursions;
            }
            if (q < loQ[i] * (1.0f - kRel) || q > hiQ[i] * (1.0f + kRel)) {
                ++run.qExcursions;
            }
            const float l = std::log2(f);
            if (firstSample) {
                minLog2[i] = l;
                maxLog2[i] = l;
            } else {
                minLog2[i] = std::min(minLog2[i], l);
                maxLog2[i] = std::max(maxLog2[i], l);
            }
            if (recordTrajectories) {
                run.freqLog2[i].push_back(l);
            }
        }
        firstSample = false;
        if (window.size() == kFeatureWindowSamples) {
            const Krate::Test::AudioFeatures features =
                Krate::Test::extractAudioFeatures(window, kFs);
            for (std::size_t b = 0; b < 5u; ++b) {
                run.band[b].push_back(static_cast<float>(features.band[b]));
            }
            window.clear();  // keeps capacity: no reallocation inside the render
        }
    }
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        run.spanOctaves[i] = maxLog2[i] - minLog2[i];
    }
    run.clampEngagements = net.getClampEngagementCount();
    return run;
}

/// SC-003's FIXED band-selection rule, applied to the WANDER-ON arm only and
/// then reused verbatim in every other arm: eligible bands are those whose mean
/// fraction is >= kBandEligibilityFraction; among those, the one with the
/// highest CV. Returns 5 (an out-of-range sentinel) when no band is eligible,
/// which the caller REQUIREs against rather than silently selecting band 0.
[[nodiscard]] std::size_t selectMotionBand(const SpectralRun& wanderOn) {
    std::size_t selected = 5u;
    double bestCv = -1.0;
    for (std::size_t b = 0; b < 5u; ++b) {
        if (meanOf(wanderOn.band[b]) < kBandEligibilityFraction) {
            continue;
        }
        const double cv = coefficientOfVariation(wanderOn.band[b]);
        if (cv > bestCv) {
            bestCv = cv;
            selected = b;
        }
    }
    return selected;
}

// -----------------------------------------------------------------------------
// SC-015 (f) and (g), SC-016
// -----------------------------------------------------------------------------

/// SC-015 (f)'s configuration: ONE peak audible at kMaxResonatorQ, every other
/// peak dormant, so the rendered output IS that peak's contribution and no
/// difference-of-two-renders reconstruction is needed.
///
/// AnchorMode::Keyed at noteHz = 80 puts peak 0 (FR-016 ratio 0.5) at exactly
/// 40 Hz - the spec's own worked figure, where the peak's own
/// RT60 = Q*ln1000/(pi*f) = 5.5 s - and makes the C-8 arm's octave jump a
/// single deterministic setNoteFrequency call.
///
/// The chosen peak's GAIN lane is closed while its frequency and Q lanes keep
/// the FR-016 defaults. That is deliberate and it is what makes (f)'s magnitude
/// bound a statement about the state clear: FR-016's default gainWanderDb is 6,
/// so an open gain lane could legitimately put the post-wake contribution 6 dB
/// ABOVE the pre-dormancy steady state without any stored ring being released,
/// and the arm would be measuring the gain lane instead of FR-042.
constexpr std::size_t kWakePeak = 0;
constexpr float kWakePeakNoteHz = 80.0f;
constexpr double kWakePeakDriveHz = 40.0;

/// SC-015 (f)'s wet trim. Unity, not FR-045's provisional +30 dB: a -12 dBFS
/// sine parked exactly on a Q = 100 resonance is the loudest thing this
/// component can be asked to render (ResonatorBank's bandpass is the constant-
/// peak-gain RBJ form, b0 = alpha, so its gain at resonance is unity and the
/// output is the drive times the peak's own gain), and the arm compares
/// MAGNITUDES - a render that reached FR-018's clamp would compare two clipped
/// numbers. The arm REQUIREs the pre-dormancy steady state to sit clear of the
/// clamp and the engagement count to stay at zero.
constexpr float kWakeWetGainDb = 0.0f;

void configureWakePatch(ResonanceDriftNetwork& net, std::uint32_t seed) {
    net.setSeed(seed);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setAnchorMode(ResonanceDriftNetwork::AnchorMode::Keyed);
    net.setNoteFrequency(kWakePeakNoteHz);
    net.setMix(1.0f);
    net.setWetGain(kWakeWetGainDb);
    net.setPeakQ(kWakePeak, Krate::DSP::kMaxResonatorQ);
    net.setGainWander(kWakePeak, 0.0f);
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        if (i != kWakePeak) {
            net.setPeakDormant(i, true);
        }
    }
}

/// Render `samples` of a sine at `hz`, `rmsDbfs` RMS, on BOTH channels, from a
/// caller-owned phase, and report the largest magnitude seen on EITHER output
/// channel. Streamed in control chunks so a 120 s interval never materialises a
/// buffer. The phase is caller-owned so consecutive calls form one continuous
/// sine - a phase discontinuity at a segment boundary would excite the Q = 100
/// ring the arm is measuring.
[[nodiscard]] float renderSinePeak(ResonanceDriftNetwork& net, double hz, float rmsDbfs,
                                   std::size_t samples, double& phase) {
    std::array<float, kChunk> in{};
    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    const double twoPi = 2.0 * static_cast<double>(Krate::DSP::kPi);
    const double increment = twoPi * hz / kFs;
    const float amplitude = Krate::DSP::dbToGain(rmsDbfs) * 1.41421356f;  // RMS -> peak

    float worst = 0.0f;
    for (std::size_t done = 0; done < samples; done += kChunk) {
        const std::size_t n = std::min(kChunk, samples - done);
        for (std::size_t i = 0; i < n; ++i) {
            in[i] = static_cast<float>(std::sin(phase)) * amplitude;
            phase += increment;
            if (phase >= twoPi) {
                phase -= twoPi;
            }
        }
        net.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            worst = std::max({worst, std::abs(outL[i]), std::abs(outR[i])});
        }
    }
    return worst;
}

/// The same drive, rendered at `numSamples == 1` granularity, capturing both
/// channels and (optionally) one peak's gate.
///
/// Per-sample granularity is not an incidental choice: SC-015 (f) asserts
/// SC-002 (d)'s PER-SAMPLE gate-step bound on the woken peak, and a 64-sample
/// block would decimate the very quantity the bound is written about by 64x
/// (the same reasoning the (e) arm above records at
/// resonance_drift_network_spectral_test.cpp's "sampled at numSamples == 1
/// granularity" comment). Both instances in (f) are rendered this way, so the
/// sample-for-sample comparison between them is like for like.
void renderSinePerSample(ResonanceDriftNetwork& net, double hz, float rmsDbfs,
                         std::size_t samples, double& phase, std::vector<float>& outL,
                         std::vector<float>& outR, std::size_t gatePeak,
                         std::vector<float>* gateOut) {
    outL.assign(samples, 0.0f);
    outR.assign(samples, 0.0f);
    if (gateOut != nullptr) {
        gateOut->assign(samples, 0.0f);
    }
    const double twoPi = 2.0 * static_cast<double>(Krate::DSP::kPi);
    const double increment = twoPi * hz / kFs;
    const float amplitude = Krate::DSP::dbToGain(rmsDbfs) * 1.41421356f;
    for (std::size_t n = 0; n < samples; ++n) {
        const float in = static_cast<float>(std::sin(phase)) * amplitude;
        phase += increment;
        if (phase >= twoPi) {
            phase -= twoPi;
        }
        net.processBlock(&in, &in, &outL[n], &outR[n], 1);
        if (gateOut != nullptr) {
            (*gateOut)[n] = net.getPeakGate(gatePeak);
        }
    }
}

/// SC-016's and SC-015 (g)'s wake pattern: "an external wake pattern toggling
/// peaks on a 20-90 s stochastic schedule (the SlowEventScheduler default
/// range, slow_event_scheduler.h:164-165), driven by the test, not owned by the
/// component". Seeded, so the whole soak is reproducible.
///
/// **STATIONARY BY CONSTRUCTION - the SC-016 (b) fixture correction.** The first
/// form of this class started with all twelve peaks awake and FLIPPED a
/// uniformly-chosen peak per event. That walk's stationary mean is six awake
/// peaks, so a render starting at twelve spends its whole length relaxing
/// toward six, and the total output level relaxes with it: FR-019 normalises by
/// `numPeaks` (held at 12 by both criteria), never by the awake count, so
/// fewer awake peaks is monotonically less energy. Measured across the eight
/// calibration seeds, that transient read as a least-squares change of -4.74,
/// -0.66, -1.92, -3.81, -2.98, -4.82, -1.95 and -1.43 dB across 30 minutes -
/// every one outside SC-016 (b)'s +/- 0.5 dB "no creep" bound. The criterion
/// was measuring the STIMULUS, not the component: a perfectly bounded network
/// cannot pass a soak whose excitation decays by construction.
///
/// The pattern now starts with exactly half the peaks awake and ALTERNATES
/// sleep and wake events, so the awake count is confined to {5, 6} for the
/// whole render and has no trend to contribute. It is still SC-015 (g)'s
/// "peaks woken and slept one at a time on a 20-90 s pattern" - one peak per
/// event, both edge kinds exercised, the schedule untouched.
class WakePattern {
public:
    /// The network is taken here, and not only in advance(), because the
    /// initial state must be PUSHED. prepare() leaves all twelve peaks awake
    /// (FR-016), so a pattern that wrote only on events would start against a
    /// network six peaks out of step with its own bookkeeping - and since its
    /// "wake" events would then land on peaks that are already awake, the only
    /// edges that changed anything would be the sleeps. MEASURED on three
    /// seeds with the push missing: the engine-active count falls by 4 to 7
    /// peaks across a 30-minute soak and the window RMS falls -2.42, -3.82 and
    /// -4.33 dB with it - the same monotone transient this class was rewritten
    /// to remove, arriving through the other door.
    WakePattern(ResonanceDriftNetwork& net, std::uint32_t seed, double sampleRate) noexcept
        : rng_(seed), sampleRate_(sampleRate) {
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            awake_[i] = (i % 2u) == 0u;  // six awake, six asleep
            net.setPeakWake(i, awake_[i] ? 1.0f : 0.0f);
        }
        scheduleNext(0);
    }

    /// Called once per control chunk with the sample index of its first sample.
    /// Returns true if an event fired.
    bool advance(ResonanceDriftNetwork& net, std::size_t sampleIndex) noexcept {
        if (sampleIndex < nextEventSample_) {
            return false;
        }
        // Alternating edges: sleep an awake peak, then wake a sleeping one, so
        // the awake count oscillates instead of drifting. `pickAwake` is the
        // state the chosen peak is IN, not the state it moves to.
        const bool pickAwake = sleepNext_;
        std::array<std::size_t, kMaxPeaks> candidates{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            if (awake_[i] == pickAwake) {
                candidates[count] = i;
                ++count;
            }
        }
        // Unreachable while the count stays in {5, 6}, but a starved pick would
        // silently stop the script rather than fail a criterion, so it is an
        // explicit no-event branch and the caller's event count sees it.
        if (count == 0) {
            scheduleNext(sampleIndex);
            return false;
        }
        const auto pick =
            static_cast<std::size_t>(std::min(rng_.nextUnipolar() * static_cast<float>(count),
                                              static_cast<float>(count - 1u)));
        const std::size_t peak = candidates[pick];
        awake_[peak] = !pickAwake;
        net.setPeakWake(peak, awake_[peak] ? 1.0f : 0.0f);
        sleepNext_ = !sleepNext_;
        ++events_;
        toggled_[peak] = true;
        scheduleNext(sampleIndex);
        return true;
    }

    [[nodiscard]] std::size_t getEventCount() const noexcept { return events_; }

    /// The evidence that the correction above is live: a render whose awake
    /// count left {5, 6} is running the old, non-stationary script.
    [[nodiscard]] std::size_t getAwakeCount() const noexcept {
        std::size_t n = 0;
        for (const bool a : awake_) {
            if (a) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] std::size_t getToggledPeakCount() const noexcept {
        std::size_t n = 0;
        for (const bool t : toggled_) {
            if (t) {
                ++n;
            }
        }
        return n;
    }

private:
    void scheduleNext(std::size_t fromSample) noexcept {
        const float span = Krate::DSP::SlowEventScheduler::kDefaultMaxInterval
                           - Krate::DSP::SlowEventScheduler::kDefaultMinInterval;
        const float seconds =
            Krate::DSP::SlowEventScheduler::kDefaultMinInterval + rng_.nextUnipolar() * span;
        nextEventSample_ =
            fromSample + static_cast<std::size_t>(static_cast<double>(seconds) * sampleRate_);
    }

    Krate::DSP::Xorshift32 rng_;
    double sampleRate_;
    std::size_t nextEventSample_ = 0;
    std::size_t events_ = 0;
    bool sleepNext_ = true;  ///< the first event is a sleep, from six awake
    std::array<bool, kMaxPeaks> awake_{};
    std::array<bool, kMaxPeaks> toggled_{};
};

/// SC-016's soak wet trim. **PROVISIONAL - T019 owns it**, and it is bracketed
/// from BOTH sides by the criterion itself, which is why it cannot simply be
/// left at FR-045's provisional +30 dB default:
///   * (d) requires getClampEngagementCount() == 0, which pushes the trim DOWN.
///     This is the most extreme patch in the spec - gainWander at its 24 dB
///     maximum on every peak on top of a Q = 100 bank - and SC-002's fixture
///     already had to drop the +30 dB default for exactly this reason
///     (spec correction C-17, 16-21 % of that render clipped);
///   * (c) requires no 10 s window below kSoakFloorDbfs, which pushes it UP.
/// +6 dB is carried in from the figure the T012 fixture measured and recorded
/// for the same shape of question (resonance_drift_network_test.cpp:2582-2588:
/// nine peaks at Q 8-57 through a -12 dBFS drive land ~20 dB clear of -60 dBFS
/// at +18 dB of trim), reduced by 12 dB to buy back the headroom the 24 dB gain
/// lane and Q = 100 demand. If the measurement moves it, BOTH this constant and
/// kSoakFloorDbfs are re-derived together and recorded - never one of them
/// widened after a red run (spec.md's Bound provenance note).
constexpr float kSoakWetGainDb = 6.0f;

/// SC-016 (a). **MEASURED, 2026-09-11**, across the eight calibration seeds:
/// worst |window dB - median dB| read mean 5.602, sd 0.978, min 4.394, max
/// 7.111. The spec's provisional +/- 6 dB sat BELOW that maximum - it passed
/// only on seed luck, and the shipped seed in fact read 6.509 and went red.
/// 10.0 is the calibration's own suggestion (observed max + 3 sd = 10.046,
/// rounded down), i.e. above the observed extreme with margin, exactly as the
/// spec says in as many words that +/- 6 dB is "explicitly not trusted": Phase
/// 2 had to widen its analogous bound from +/- 3.0 to +/- 4.5 dB for a LESS
/// extreme configuration after measuring.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-016 (a) kSoakWindowDb and (c) kSoakFloorDbfs"
/// (resonance_drift_network_perf_test.cpp, [.calibration]). That section
/// measures BOTH constants and the wet trim's clamp count in one 30-minute
/// render per seed, which is what makes re-deriving them TOGETHER practical.
constexpr float kSoakWindowDb = 10.0f;

/// SC-016 (c)'s floor, **MEASURED, 2026-09-11** by the same section: the lowest
/// 10 s window read mean -42.241, sd 1.139, min -44.290, max -41.107 dBFS, so
/// the derived floor is min - 3 sd = -48.719, rounded to -48.7. This TIGHTENS
/// the spec's provisional -60 dBFS by 11 dB: -60 is 16 dB below anything the
/// patch produces and could not have caught a resonance that died to a tenth of
/// its level. It is a LOWER bound, so it sits BELOW the observed minimum.
constexpr float kSoakFloorDbfs = -48.7f;

/// SC-016 (c)'s ceiling and (b)'s slope bound are the spec's own fixed figures,
/// and NEITHER is relaxed here. (b)'s +/- 0.5 dB per 30 minutes is instead
/// applied to a statistic that can carry it: see the SC-016 case for the
/// measurement showing that a SINGLE 30-minute soak's least-squares change has
/// a null spread of sd 0.49 dB - i.e. +/- 0.5 dB on one seed is a coin flip,
/// not a bound - while the MEAN across the eight seeds has a standard error of
/// 0.17 dB and holds the figure with room to spare.
constexpr float kSoakCeilingDbfs = -3.0f;
constexpr float kSoakSlopeDbPerRender = 0.5f;

/// SC-016's window length and total duration.
constexpr double kSoakWindowSeconds = 10.0;
constexpr double kSoakSeconds = 1800.0;  // 30 minutes
constexpr std::size_t kSoakWindowChunks =
    static_cast<std::size_t>(kSoakWindowSeconds * kFs) / kChunk;  // 7 500
constexpr std::size_t kSoakChunks = static_cast<std::size_t>(kSoakSeconds * kFs) / kChunk;
static_assert(kSoakChunks % kSoakWindowChunks == 0u, "whole 10 s windows only");
constexpr std::size_t kSoakWindows = kSoakChunks / kSoakWindowChunks;  // 180

/// SC-015 (g)'s window length and the bound on how far the bed may move across
/// the scripted wake/sleep sequence. **MEASURED, 2026-09-11**, across the eight
/// calibration seeds: mean 15.464, sd 1.059, min 14.452, max 17.780 dB, so the
/// derived bound is max + 3 sd = 20.956, rounded to 21.0. The spec's
/// provisional 6 dB sat roughly NINE dB below the observed MINIMUM - no seed
/// could have passed it, and the shipped seed read 16.22.
///
/// Why the null is so wide, and what this arm therefore does and does not
/// prove: a 200 ms window is short against the noise bandwidth of a Q = 12 peak
/// (f0/Q is 3.3 Hz at the 40 Hz anchor, i.e. under one independent sample per
/// window), so the window-to-window spread of this statistic is dominated by
/// the DRIVE and not by the wake events. The teeth of SC-015 (g) are therefore
/// its OTHER clause - the per-sample gate-step bound asserted alongside it,
/// which a wake/sleep discontinuity fails immediately - and this bound catches
/// only a gross level jump. Recorded rather than quietly widened, per spec.md's
/// Bound provenance note.
///
/// MEASURED BY: ResonanceDriftNetwork_MeasureThresholds, section
/// "SC-015 (g) the wake/sleep RMS bound" (resonance_drift_network_perf_test.cpp,
/// [.calibration]).
constexpr double kWakeSleepWindowSeconds = 0.2;
constexpr float kWakeSleepWindowDb = 21.0f;

/// FR-019's law, as a level: 20*log10(sqrt(12/4)).
constexpr float kNumPeaksLevelStepDb = 4.77f;
constexpr float kNumPeaksLevelTolDb = 0.5f;

/// Least-squares slope of `values` against their index, expressed as the total
/// change across the whole record (SC-016 (b)'s "dB per 30 minutes").
[[nodiscard]] double totalLeastSquaresChange(const std::vector<float>& values) {
    const std::size_t n = values.size();
    if (n < 2u) {
        return 0.0;
    }
    const double meanX = 0.5 * static_cast<double>(n - 1u);
    const double meanY = meanOf(values);
    double sxy = 0.0;
    double sxx = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = static_cast<double>(i) - meanX;
        sxy += dx * (static_cast<double>(values[i]) - meanY);
        sxx += dx * dx;
    }
    const double slopePerWindow = (sxx > 0.0) ? (sxy / sxx) : 0.0;
    return slopePerWindow * static_cast<double>(n - 1u);
}

} // namespace

TEST_CASE("ResonanceDriftNetwork_PeakLifeCycle", "[resonance_drift_network][long]") {
    // 500 ms of drive - comfortably past both the 50 ms FR-041 ramp and the
    // 20 ms FR-043 mix smoother, so every arm's precondition is real.
    constexpr std::size_t kHalfSecond = 24000;

    SECTION("(a) a peak asleep for >= 50 ms contributes exactly nothing") {
        constexpr std::size_t kPeak = 4;
        constexpr std::uint32_t kSeed = 0x11FE0A01u;
        constexpr std::size_t kMeasure = 96000;  // 2 s

        std::vector<float> settleL(kHalfSecond);
        std::vector<float> settleR(kHalfSecond);
        fillDrive(settleL, settleR);
        std::vector<float> inL(kMeasure);
        std::vector<float> inR(kMeasure);
        fillDrive(inL, inR);

        ResonanceDriftNetwork reference;
        ResonanceDriftNetwork swept;
        for (ResonanceDriftNetwork* net : {&reference, &swept}) {
            net->setSeed(kSeed);
            net->prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            net->setPeakWake(kPeak, 0.0f);
            settle(*net, settleL, settleR);  // the >= 50 ms precondition
        }
        REQUIRE(reference.isPeakEngineActive(kPeak) == false);
        REQUIRE(swept.isPeakEngineActive(kPeak) == false);

        std::vector<float> refL;
        std::vector<float> refR;
        renderBuffer(reference, inL, inR, refL, refR, 512, [](std::size_t) {});

        std::vector<float> swL;
        std::vector<float> swR;
        renderBuffer(swept, inL, inR, swL, swR, 512, [&swept](std::size_t at) {
            // Sweep the SLEEPING peak's anchor across five octaves, one step per
            // block. Its lanes advance, its anchor is recomputed and its applied
            // values track - but with its bank slot disabled and its state
            // cleared, none of it may reach a single output sample.
            const float t = static_cast<float>(at) / 96000.0f;
            swept.setPeakAnchorHz(kPeak, 40.0f * std::exp2(5.0f * t));
        });

        // Non-vacuity first: two silent renders would agree perfectly.
        REQUIRE(peakAbs(refL) > 0.0f);
        REQUIRE(peakAbs(refR) > 0.0f);

        const float diffL = maxAbsDiff(refL, swL);
        const float diffR = maxAbsDiff(refR, swR);
        CAPTURE(diffL, diffR);
        REQUIRE(diffL == 0.0f);
        REQUIRE(diffR == 0.0f);

        // The sweep really did move the sleeping peak's own bookkeeping (FR-052
        // reports a dormant peak's values as if it were awake) - so the exact
        // zeros above are the engine being disconnected, not the sweep being a
        // no-op.
        CAPTURE(swept.getPeakCurrentFrequency(kPeak), reference.getPeakCurrentFrequency(kPeak));
        REQUIRE(swept.getPeakCurrentFrequency(kPeak)
                != reference.getPeakCurrentFrequency(kPeak));
        REQUIRE(swept.isPeakEngineActive(kPeak) == false);
    }

    SECTION("(c) every peak dormant: digital silence at mix = 1, dry passthrough at mix = 0") {
        ResonanceDriftNetwork net;
        net.setSeed(0x11FE0C01u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        std::vector<float> inL(kHalfSecond);
        std::vector<float> inR(kHalfSecond);
        fillDrive(inL, inR);

        // "for ANY pan configuration" - a fully dormant network never reaches
        // the pan step at all, so the pans are deliberately extreme and unequal.
        // Indexed by i % 3, not a nested ternary: same three values, one place
        // to read them.
        static constexpr std::array<float, 3> kExtremePans{-1.0f, 1.0f, 0.3f};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float pan = kExtremePans[i % kExtremePans.size()];
            net.setPeakPan(i, pan);
            net.setPeakDormant(i, true);
        }
        settle(net, inL, inR);  // the gates ramp to exactly 0 during this render

        REQUIRE(net.getMix() == 1.0f);  // the FR-016 default
        std::vector<float> silentL;
        std::vector<float> silentR;
        renderBuffer(net, inL, inR, silentL, silentR, 512, [](std::size_t) {});
        // EXACTLY zero, not "below a floor": a closed gate zeroes a peak's
        // contribution by multiplication, and OnePoleSmoother snaps current_ to
        // target_ inside kCompletionThreshold so m is exactly 1.0f.
        REQUIRE(allExactlyZero(silentL));
        REQUIRE(allExactlyZero(silentR));
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i);
            REQUIRE(net.isPeakEngineActive(i) == false);
        }

        net.setMix(0.0f);
        settle(net, inL, inR);  // 500 ms, past the 20 ms mix smoother
        std::vector<float> dryL;
        std::vector<float> dryR;
        renderBuffer(net, inL, inR, dryL, dryR, 512, [](std::size_t) {});

        REQUIRE(peakAbs(inL) > 0.0f);  // non-vacuity
        REQUIRE(peakAbs(inR) > 0.0f);
        REQUIRE(maxAbsDiff(dryL, inL) <= Krate::DSP::TestUtils::kSampleTolerance);
        REQUIRE(maxAbsDiff(dryR, inR) <= Krate::DSP::TestUtils::kSampleTolerance);
    }

    SECTION("(d) setPeakDormant(i, true) and setPeakWake(i, 0) are the same render") {
        constexpr std::size_t kPeak = 7;
        constexpr std::uint32_t kSeed = 0x11FE0D01u;

        std::vector<float> inL(96000);
        std::vector<float> inR(96000);
        fillDrive(inL, inR);

        ResonanceDriftNetwork viaDormant;
        viaDormant.setSeed(kSeed);
        viaDormant.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        viaDormant.setPeakDormant(kPeak, true);

        ResonanceDriftNetwork viaWake;
        viaWake.setSeed(kSeed);
        viaWake.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        viaWake.setPeakWake(kPeak, 0.0f);

        std::vector<float> dormantL;
        std::vector<float> dormantR;
        renderBuffer(viaDormant, inL, inR, dormantL, dormantR, 512, [](std::size_t) {});
        std::vector<float> wakeL;
        std::vector<float> wakeR;
        renderBuffer(viaWake, inL, inR, wakeL, wakeR, 512, [](std::size_t) {});

        // Non-vacuity: the other eleven peaks are still rendering.
        REQUIRE(peakAbs(dormantL) > 0.0f);
        REQUIRE(peakAbs(dormantR) > 0.0f);

        const float diffL = maxAbsDiff(dormantL, wakeL);
        const float diffR = maxAbsDiff(dormantR, wakeR);
        CAPTURE(diffL, diffR);
        REQUIRE(diffL == 0.0f);
        REQUIRE(diffR == 0.0f);

        // ...and the READ SURFACE is the only thing that tells them apart.
        REQUIRE(viaDormant.isPeakDormant(kPeak) == true);
        REQUIRE(viaWake.isPeakDormant(kPeak) == false);
        REQUIRE(viaDormant.getPeakWakeAmount(kPeak) == 1.0f);
        REQUIRE(viaWake.getPeakWakeAmount(kPeak) == 0.0f);
        REQUIRE(viaDormant.isPeakEngineActive(kPeak) == false);
        REQUIRE(viaWake.isPeakEngineActive(kPeak) == false);
    }

    SECTION("(e) a mid-render setNumPeaks reduction silences the dropped peaks through the ramp") {
        constexpr std::size_t kKept = 4;

        ResonanceDriftNetwork net;
        net.setSeed(0x11FE0E01u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        std::vector<float> inL(kHalfSecond);
        std::vector<float> inR(kHalfSecond);
        fillDrive(inL, inR);
        settle(net, inL, inR);

        std::array<float, kMaxPeaks> previous{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            previous[i] = net.getPeakGate(i);
            CAPTURE(i);
            REQUIRE(previous[i] == 1.0f);  // non-vacuity: every gate was wide open
        }

        net.setNumPeaks(kKept);
        REQUIRE(net.getNumPeaks() == kKept);

        // Sampled at numSamples == 1 granularity, the same granularity SC-002
        // (d) names, so the bound is asserted on the quantity it is written
        // about rather than on a 64-sample decimation of it.
        float worstStep = 0.0f;
        float in = 0.0f;
        float oL = 0.0f;
        float oR = 0.0f;
        for (std::size_t n = 0; n < std::size_t{4800}; ++n) {  // 100 ms
            net.processBlock(&in, &in, &oL, &oR, 1);
            for (std::size_t i = kKept; i < kMaxPeaks; ++i) {
                const float g = net.getPeakGate(i);
                worstStep = std::max(worstStep, std::abs(g - previous[i]));
                previous[i] = g;
            }
        }
        CAPTURE(worstStep, kGateStepBound);
        REQUIRE(worstStep <= kGateStepBound);

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i);
            if (i < kKept) {
                REQUIRE(net.isPeakEngineActive(i) == true);
            } else {
                REQUIRE(net.getPeakGate(i) == 0.0f);
                REQUIRE(net.isPeakEngineActive(i) == false);
            }
        }
    }

    SECTION("(h) a vanishing wake reaches dormancy - the only kWakeSilenceEpsilon arm") {
        constexpr std::size_t kPeak = 3;

        ResonanceDriftNetwork net;
        net.setSeed(0x11FE0801u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});

        std::vector<float> inL(kHalfSecond);
        std::vector<float> inR(kHalfSecond);
        fillDrive(inL, inR);

        // EXACTLY the shape FR-040's Phase-10 caller produces when it writes
        // getEnvelopeValue() * getActiveDepth(): a legal [0, 1] argument that a
        // release tail has walked down to nearly nothing. It survives
        // setPeakWake's clamp untouched - the snap that makes the sleep edge's
        // exact == 0.0f test valid happens in gateSteady(), at the source.
        net.setPeakWake(kPeak, 1.0e-8f);
        REQUIRE(net.getPeakWakeAmount(kPeak) > 0.0f);
        REQUIRE(net.getPeakWakeAmount(kPeak) < ResonanceDriftNetwork::kWakeSilenceEpsilon);

        settle(net, inL, inR);  // >= 50 ms
        REQUIRE(net.getPeakGate(kPeak) == 0.0f);
        REQUIRE(net.isPeakEngineActive(kPeak) == false);

        // ...and a wake back above the epsilon re-engages the slot IN THE
        // SETTER, before the next control step.
        net.setPeakWake(kPeak, 1.0f);
        REQUIRE(net.isPeakEngineActive(kPeak) == true);
    }

    // =========================================================================
    // T018's arms: (b), (f) and (g)
    // =========================================================================

    SECTION("(b) the lanes freewheel across a 120 s dormant interval") {
        // The cross-cutting Dormancy rule (roadmap lines 490-491): a dormant
        // slot's PROCESSING CHAIN is skipped but its modulation lanes keep
        // running, so a peak wakes somewhere new rather than where it fell
        // asleep. The whole network is put to sleep so nothing else can move
        // the readings, and the drive is silence - the lanes advance for every
        // peak regardless of input (FR-036).
        ResonanceDriftNetwork net;
        net.setSeed(0x11FE0B01u);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            net.setPeakDormant(i, true);
        }
        // >= 50 ms, so every FR-041 gate has reached exactly zero and every
        // sleep edge has already fired before the interval opens.
        advanceControlStepsSilent(net, 200);  // 12 800 samples = 267 ms
        std::array<float, kMaxPeaks> atSleep{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i);
            REQUIRE(net.isPeakEngineActive(i) == false);
            atSleep[i] = net.getPeakCurrentFrequency(i);
            REQUIRE(atSleep[i] > 0.0f);
        }

        constexpr std::size_t kDormantChunks = static_cast<std::size_t>(120.0 * kFs) / kChunk;
        static_assert(kDormantChunks == 90000u, "120 s of control chunks at 48 kHz");
        advanceControlStepsSilent(net, kDormantChunks);

        // 20 cents against FR-016's default +/- 3 semitones (= +/- 300 cents)
        // of depth, over 3.6 correlation times at the 0.03 Hz default rate.
        int moved = 0;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float nowHz = net.getPeakCurrentFrequency(i);
            const float cents = 1200.0f * std::log2(nowHz / atSleep[i]);
            CAPTURE(i, atSleep[i], nowHz, cents);
            REQUIRE(Krate::DSP::detail::isFinite(cents));
            if (std::abs(cents) >= 20.0f) {
                ++moved;
            }
            // ...and none of it woke anything up.
            REQUIRE(net.isPeakEngineActive(i) == false);
        }
        CAPTURE(moved);
        REQUIRE(moved >= 9);
    }

    SECTION("(f) waking after 120 s of dormancy is silent, not a burst") {
        constexpr std::uint32_t kSeed = 0x11FE0F01u;
        constexpr std::size_t kSettleSamples = static_cast<std::size_t>(8.0 * kFs);
        constexpr std::size_t kSteadySamples = static_cast<std::size_t>(2.0 * kFs);
        constexpr std::size_t kDormantSamples = static_cast<std::size_t>(120.0 * kFs);
        constexpr std::size_t kPostWakeSamples = static_cast<std::size_t>(5.0 * kFs);

        // `slept` is the instance under test. `fresh` is its twin: same seed,
        // same rate, same configuration and the SAME sequence of calls, except
        // that its peak is dormant from prepare onward and therefore never
        // stores a ring at all. Their lanes are identical throughout (FR-036
        // advances every peak's lanes, dormant included), so after FR-042's
        // sleep-edge state clear the two must render the post-wake window
        // IDENTICALLY. That comparison is the decisive half of this arm: a
        // build without the state clear releases seconds of stored Q = 100 ring
        // into drifted coefficients and diverges from `fresh` immediately,
        // whereas the spec's own "never exceeds the pre-dormancy steady state"
        // bound could in principle be satisfied by a burst that merely happens
        // to be quieter than the steady state it came from.
        ResonanceDriftNetwork slept;
        ResonanceDriftNetwork fresh;
        configureWakePatch(slept, kSeed);
        configureWakePatch(fresh, kSeed);

        // Pre-dormancy the wander is OFF, so the peak sits EXACTLY on the 40 Hz
        // drive and its steady state is the largest response it can produce -
        // which is what makes "never exceeds it" a real ceiling rather than an
        // accident of where the frequency lane happened to be at sleep time.
        slept.setWanderEnabled(false);
        fresh.setWanderEnabled(false);
        fresh.setPeakDormant(kWakePeak, true);

        // The spec's own worked figure, recomputed from the shipped constants:
        // RT60 = Q * ln1000 / (pi * f) = 5.5 s at Q = 100 and 40 Hz. REQUIREd
        // before it is relied on (the continuous_body_test.cpp:3361-3386
        // pattern), so a change to either constant is caught here rather than
        // silently changing what "120 s of dormancy" means.
        const double rt60 = (static_cast<double>(Krate::DSP::kMaxResonatorQ)
                             * static_cast<double>(Krate::DSP::kLn1000))
                            / (static_cast<double>(Krate::DSP::kPi) * kWakePeakDriveHz);
        CAPTURE(rt60);
        REQUIRE(rt60 > 5.0);
        REQUIRE(rt60 < 6.0);

        double sleptPhase = 0.0;
        double freshPhase = 0.0;
        static_cast<void>(
            renderSinePeak(slept, kWakePeakDriveHz, -12.0f, kSettleSamples, sleptPhase));
        static_cast<void>(
            renderSinePeak(fresh, kWakePeakDriveHz, -12.0f, kSettleSamples, freshPhase));
        const float preSteady =
            renderSinePeak(slept, kWakePeakDriveHz, -12.0f, kSteadySamples, sleptPhase);
        static_cast<void>(
            renderSinePeak(fresh, kWakePeakDriveHz, -12.0f, kSteadySamples, freshPhase));

        CAPTURE(preSteady);
        REQUIRE(preSteady > 0.0f);  // non-vacuity: the peak really was sounding
        // Linearity precondition: a render that reached FR-018's clamp would
        // make the magnitude comparison below a comparison of two clipped
        // numbers.
        REQUIRE(preSteady < 0.5f * ResonanceDriftNetwork::kOutputClamp);
        REQUIRE(slept.getClampEngagementCount() == 0u);

        const float preSleepHz = slept.getPeakCurrentFrequency(kWakePeak);
        REQUIRE(preSleepHz == Catch::Approx(static_cast<float>(kWakePeakDriveHz)).epsilon(0.001));

        slept.setPeakDormant(kWakePeak, true);
        // `fresh` is already dormant; the redundant call keeps the two call
        // sequences byte-identical so nothing but the stored ring can differ.
        fresh.setPeakDormant(kWakePeak, true);
        // Wander back ON for the dormant interval: FR-035's dormant-interval
        // exemption (spec correction C-8) is what the arms below are about.
        slept.setWanderEnabled(true);
        fresh.setWanderEnabled(true);

        static_cast<void>(
            renderSinePeak(slept, kWakePeakDriveHz, -12.0f, kDormantSamples, sleptPhase));
        static_cast<void>(
            renderSinePeak(fresh, kWakePeakDriveHz, -12.0f, kDormantSamples, freshPhase));

        // ---- the C-8 arm: unslewed dormant tracking -------------------------
        // An octave jump applied DURING dormancy must be reflected within ONE
        // control step, not fifty. This is what distinguishes an unslewed
        // dormant peak from a slew-limited one: FR-035's default ceiling is
        // 0.02 octaves per control step, so a build that slewed while dormant
        // reads 0.02 here instead of 1.0. It is also the arm's non-vacuity
        // source - it guarantees a large, DETERMINISTIC difference between the
        // pre-sleep and post-dormancy values, where the Brownian lane alone
        // only guarantees one for 9 of 12 peaks (arm (b)).
        const float beforeJump = slept.getPeakCurrentFrequency(kWakePeak);
        slept.setNoteFrequency(2.0f * kWakePeakNoteHz);
        fresh.setNoteFrequency(2.0f * kWakePeakNoteHz);
        static_cast<void>(renderSinePeak(slept, kWakePeakDriveHz, -12.0f, kChunk, sleptPhase));
        static_cast<void>(renderSinePeak(fresh, kWakePeakDriveHz, -12.0f, kChunk, freshPhase));
        const float jumpOctaves = std::log2(slept.getPeakCurrentFrequency(kWakePeak) / beforeJump);
        CAPTURE(beforeJump, slept.getPeakCurrentFrequency(kWakePeak), jumpOctaves);
        REQUIRE(jumpOctaves >= 0.9f);
        REQUIRE(jumpOctaves <= 1.1f);

        // ---- the extended (Q6) arm: the drifted values are in place BEFORE
        //      the gate lifts ---------------------------------------------------
        // The "one extra control step at wake == 0" hold, reached entirely
        // through the public surface: clear dormancy while setPeakWake(0) still
        // holds the gate target at zero, run one control step, and read the
        // three FR-052 getters. They must already carry the post-dormancy
        // drifted values - not the pre-sleep ones - before any gate movement.
        const float dormantHz = slept.getPeakCurrentFrequency(kWakePeak);
        const float dormantQ = slept.getPeakCurrentQ(kWakePeak);
        const float dormantGainDb = slept.getPeakCurrentGainDb(kWakePeak);

        slept.setPeakWake(kWakePeak, 0.0f);
        fresh.setPeakWake(kWakePeak, 0.0f);
        slept.setPeakDormant(kWakePeak, false);
        fresh.setPeakDormant(kWakePeak, false);
        static_cast<void>(renderSinePeak(slept, kWakePeakDriveHz, -12.0f, kChunk, sleptPhase));
        static_cast<void>(renderSinePeak(fresh, kWakePeakDriveHz, -12.0f, kChunk, freshPhase));

        REQUIRE(slept.getPeakGate(kWakePeak) == 0.0f);
        REQUIRE(slept.isPeakEngineActive(kWakePeak) == false);
        const float heldHz = slept.getPeakCurrentFrequency(kWakePeak);
        const float heldQ = slept.getPeakCurrentQ(kWakePeak);
        const float heldGainDb = slept.getPeakCurrentGainDb(kWakePeak);
        CAPTURE(preSleepHz, dormantHz, heldHz, dormantQ, heldQ, dormantGainDb, heldGainDb);
        // Unchanged across the hold - one control step of drift, no revert.
        REQUIRE(std::abs(std::log2(heldHz / dormantHz)) <= 0.05f);
        REQUIRE(heldQ == Catch::Approx(dormantQ).epsilon(0.02));
        REQUIRE(heldGainDb == Catch::Approx(dormantGainDb).margin(0.5));
        // ...and genuinely different from where the peak fell asleep. The bound
        // is 0.7 and not 1.0 octaves because the frequency lane is running
        // during the dormant interval and its FR-016 default depth is
        // +/- 3 semitones: the anchor doubled, but the realised value sits
        // anywhere in 2 * 2^(+/-0.25) of it, i.e. 0.75 to 1.25 octaves above the
        // pre-sleep reading. A build that reverted to the pre-sleep value on the
        // dormancy clear reads 0.
        REQUIRE(std::log2(heldHz / preSleepHz) >= 0.7f);

        // ---- the wake edge --------------------------------------------------
        slept.setPeakWake(kWakePeak, 1.0f);
        fresh.setPeakWake(kWakePeak, 1.0f);
        REQUIRE(slept.isPeakEngineActive(kWakePeak) == true);
        REQUIRE(fresh.isPeakEngineActive(kWakePeak) == true);

        std::vector<float> sleptL;
        std::vector<float> sleptR;
        std::vector<float> gate;
        std::vector<float> freshL;
        std::vector<float> freshR;
        renderSinePerSample(slept, kWakePeakDriveHz, -12.0f, kPostWakeSamples, sleptPhase, sleptL,
                            sleptR, kWakePeak, &gate);
        renderSinePerSample(fresh, kWakePeakDriveHz, -12.0f, kPostWakeSamples, freshPhase, freshL,
                            freshR, kWakePeak, nullptr);

        // (i) the spec's own magnitude bound, over the whole first 5 s.
        const float postPeak = std::max(peakAbs(sleptL), peakAbs(sleptR));
        CAPTURE(postPeak, preSteady);
        REQUIRE(postPeak <= preSteady);

        // (ii) the gate satisfies SC-002 (d)'s per-sample bound and reaches 1.0
        //      in 50 ms +/- 5 ms.
        float worstStep = 0.0f;
        float previous = 0.0f;
        std::size_t reachedAt = gate.size();
        for (std::size_t n = 0; n < gate.size(); ++n) {
            worstStep = std::max(worstStep, std::abs(gate[n] - previous));
            previous = gate[n];
            if (reachedAt == gate.size() && gate[n] >= 1.0f) {
                reachedAt = n;
            }
        }
        const double reachedMs = 1000.0 * static_cast<double>(reachedAt) / kFs;
        CAPTURE(worstStep, kGateStepBound, reachedMs);
        REQUIRE(worstStep <= kGateStepBound);
        REQUIRE(reachedMs >= 45.0);
        REQUIRE(reachedMs <= 55.0);

        // (iii) the decisive half: with the ring cleared at the sleep edge, the
        //       slept instance and the never-rung twin are the same render.
        REQUIRE(peakAbs(freshL) > 0.0f);  // non-vacuity: the twin is audible
        const float diffL = maxAbsDiff(sleptL, freshL);
        const float diffR = maxAbsDiff(sleptR, freshR);
        CAPTURE(diffL, diffR);
        REQUIRE(diffL <= Krate::DSP::TestUtils::kSampleTolerance);
        REQUIRE(diffR <= Krate::DSP::TestUtils::kSampleTolerance);
        REQUIRE(slept.getClampEngagementCount() == 0u);
    }

    SECTION("(g) the bed holds across a wake/sleep script, and steps 4.77 dB on setNumPeaks") {
        // ---- part 1: the scripted 20-90 s wake/sleep sequence ---------------
        constexpr std::uint32_t kSeed = 0x11FE0701u;
        constexpr std::size_t kScriptSamples = static_cast<std::size_t>(300.0 * kFs);
        constexpr std::size_t kWindowSamples =
            static_cast<std::size_t>(kWakeSleepWindowSeconds * kFs);
        static_assert(kWindowSamples == 9600u, "200 ms at 48 kHz");

        ResonanceDriftNetwork net;
        net.setSeed(kSeed);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setMix(1.0f);
        net.setWetGain(kSoakWetGainDb);
        REQUIRE(net.getNumPeaks() == kMaxPeaks);  // "with numPeaks held at 12"

        PinkDrive driveL(0x51EE0001u, kFs, -12.0f);
        PinkDrive driveR(0x51EE0002u, kFs, -12.0f);
        WakePattern pattern(net, kSeed ^ 0x5A5A5A5Au, kFs);

        std::array<float, kMaxPeaks> previousGate{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            previousGate[i] = net.getPeakGate(i);
        }

        std::vector<float> windowDb;
        windowDb.reserve(kScriptSamples / kWindowSamples + 1u);
        double windowSumSq = 0.0;
        std::size_t windowCount = 0;
        float worstGateStep = 0.0f;
        bool allFinite = true;

        // Rendered at numSamples == 1 for the same reason (f) is: the gate-step
        // bound is a per-sample statement.
        for (std::size_t n = 0; n < kScriptSamples; ++n) {
            if ((n % kChunk) == 0u) {
                static_cast<void>(pattern.advance(net, n));
            }
            const float inL = driveL.next();
            const float inR = driveR.next();
            float outL = 0.0f;
            float outR = 0.0f;
            net.processBlock(&inL, &inR, &outL, &outR, 1);
            if (!Krate::DSP::detail::isFinite(outL) || !Krate::DSP::detail::isFinite(outR)) {
                allFinite = false;
            }
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                const float g = net.getPeakGate(i);
                worstGateStep = std::max(worstGateStep, std::abs(g - previousGate[i]));
                previousGate[i] = g;
            }
            windowSumSq += static_cast<double>(outL) * static_cast<double>(outL)
                           + static_cast<double>(outR) * static_cast<double>(outR);
            windowCount += 2u;
            if (((n + 1u) % kWindowSamples) == 0u) {
                windowDb.push_back(rmsDbfsOf(windowSumSq, windowCount));
                windowSumSq = 0.0;
                windowCount = 0;
            }
        }

        CAPTURE(pattern.getEventCount(), pattern.getToggledPeakCount(), driveL.getScale());
        REQUIRE(allFinite);
        REQUIRE(net.getClampEngagementCount() == 0u);
        // Non-vacuity: the script must actually have woken and slept peaks.
        REQUIRE(pattern.getEventCount() >= 3u);
        REQUIRE(pattern.getToggledPeakCount() >= 2u);
        REQUIRE(windowDb.size() == kScriptSamples / kWindowSamples);

        CAPTURE(worstGateStep, kGateStepBound);
        REQUIRE(worstGateStep <= kGateStepBound);

        // "moves by no more than a bound", taken against the MEDIAN window so a
        // single outlier cannot move the reference the spread is measured from.
        const float medianDb = percentileOf(windowDb, 0.5);
        float worstDeviationDb = 0.0f;
        for (const float db : windowDb) {
            worstDeviationDb = std::max(worstDeviationDb, std::abs(db - medianDb));
        }
        CAPTURE(medianDb, worstDeviationDb, kWakeSleepWindowDb);
        REQUIRE(medianDb > kSoakFloorDbfs);  // the bed is audible at all
        REQUIRE(worstDeviationDb <= kWakeSleepWindowDb);

        // ---- part 2: the mid-render setNumPeaks(12 -> 4) step ---------------
        // Measured against an otherwise IDENTICAL twin driven by the same
        // samples rather than against the same instance's own earlier window.
        // The reason is arithmetic, not convenience: at Q = 12 the lowest
        // peak's noise bandwidth is f0/Q = 3.3 Hz, so a several-second window of
        // pink noise carries only ~O(10) independent samples per peak and its
        // RMS estimator has a spread of the same order as the +/- 0.5 dB
        // tolerance this arm has to resolve. Driving both instances with the
        // same drive cancels that term exactly, leaving FR-019's wet scale as
        // the only difference between them.
        //
        // Peaks 4-11 are dormant in BOTH instances (spec correction C-10's
        // construction, as used by SC-001 (c)), so the reduction changes N and
        // nothing acoustic.
        ResonanceDriftNetwork held;
        ResonanceDriftNetwork reduced;
        for (ResonanceDriftNetwork* p : {&held, &reduced}) {
            p->setSeed(0x11FE0702u);
            p->prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
            p->setMix(1.0f);
            p->setWetGain(kSoakWetGainDb);
            for (std::size_t i = 4; i < kMaxPeaks; ++i) {
                p->setPeakDormant(i, true);
            }
        }

        PinkDrive pairL(0x51EE0003u, kFs, -12.0f);
        PinkDrive pairR(0x51EE0004u, kFs, -12.0f);
        double heldSumSq = 0.0;
        double reducedSumSq = 0.0;
        std::size_t pairCounted = 0;
        float pairWorstDiff = 0.0f;
        float pairPeak = 0.0f;

        const auto renderPair = [&](std::size_t samples, bool measure) {
            std::array<float, kChunk> inL{};
            std::array<float, kChunk> inR{};
            std::array<float, kChunk> heldL{};
            std::array<float, kChunk> heldR{};
            std::array<float, kChunk> redL{};
            std::array<float, kChunk> redR{};
            for (std::size_t done = 0; done < samples; done += kChunk) {
                for (std::size_t i = 0; i < kChunk; ++i) {
                    inL[i] = pairL.next();
                    inR[i] = pairR.next();
                }
                held.processBlock(inL.data(), inR.data(), heldL.data(), heldR.data(), kChunk);
                reduced.processBlock(inL.data(), inR.data(), redL.data(), redR.data(), kChunk);
                for (std::size_t i = 0; i < kChunk; ++i) {
                    pairWorstDiff = std::max(pairWorstDiff, std::abs(heldL[i] - redL[i]));
                    pairPeak = std::max(pairPeak, std::abs(heldL[i]));
                    if (measure) {
                        heldSumSq += static_cast<double>(heldL[i]) * static_cast<double>(heldL[i])
                                     + static_cast<double>(heldR[i]) * static_cast<double>(heldR[i]);
                        reducedSumSq += static_cast<double>(redL[i]) * static_cast<double>(redL[i])
                                        + static_cast<double>(redR[i]) * static_cast<double>(redR[i]);
                        pairCounted += 2u;
                    }
                }
            }
        };

        constexpr std::size_t kPairSettle = static_cast<std::size_t>(2.0 * kFs);
        constexpr std::size_t kPairRamp = static_cast<std::size_t>(1.0 * kFs);
        constexpr std::size_t kPairMeasure = static_cast<std::size_t>(6.0 * kFs);
        static_assert(kPairSettle % kChunk == 0u && kPairRamp % kChunk == 0u
                          && kPairMeasure % kChunk == 0u,
                      "renderPair advances in whole control chunks");

        renderPair(kPairSettle, false);
        // Before the switch the two are the same object in every respect.
        CAPTURE(pairWorstDiff, pairPeak);
        REQUIRE(pairPeak > 0.0f);  // non-vacuity
        REQUIRE(pairWorstDiff == 0.0f);

        reduced.setNumPeaks(4);
        REQUIRE(reduced.getNumPeaks() == 4u);
        REQUIRE(held.getNumPeaks() == kMaxPeaks);
        renderPair(kPairRamp, false);  // 1 s = 20 x the 50 ms wet-scale ramp
        renderPair(kPairMeasure, true);

        const float heldDb = rmsDbfsOf(heldSumSq, pairCounted);
        const float reducedDb = rmsDbfsOf(reducedSumSq, pairCounted);
        const float stepDb = reducedDb - heldDb;
        CAPTURE(heldDb, reducedDb, stepDb);
        REQUIRE(heldDb > kSoakFloorDbfs);
        // 20*log10(sqrt(12/4)) = 4.77 dB: FR-019's law made visible on the
        // SURVIVING peaks, which SC-015 (e) never looks at.
        REQUIRE(stepDb
                == Catch::Approx(kNumPeaksLevelStepDb).margin(kNumPeaksLevelTolDb));
    }
}

// =============================================================================
// T016 - SC-001: stability at max Q under sustained input (roadmap line 227).
//
// The infinite-ring pattern (ContinuousBody_DecaysToSilence,
// continuous_body_test.cpp:3339-3400, tracing to Membrum's
// test_kit_switch_infinite_ring.cpp:59) applied to this component.
//
// (c) is deliberately NOT "the two arms agree": under FR-016's geometric
// anchors at equal Q the numPeaks = 4 and numPeaks = 12 arms carry DIFFERENT
// acoustic content (Sum f = 273 Hz vs 4624 Hz), so a correct build differs by
// ~7.5 dB - larger than the 4.77 dB defect the arm targets - and any bound "set
// above the observed maximum" would swallow the very defect it was written to
// catch. Instead peaks 4-11 are dormant in BOTH arms so the content is
// identical, and the arm asserts the DERIVED relationship
// 20*log10(sqrt(12/4)) = 4.77 dB (spec correction C-10). This is simultaneously
// the only assertion anywhere in this phase of FR-019's "N is numPeaks, not the
// awake count": a build counting awake peaks computes 1/sqrt(4) in BOTH arms
// and the measured difference collapses to 0 dB.
// =============================================================================
TEST_CASE("ResonanceDriftNetwork_MaxQRingOut", "[resonance_drift_network]") {
    // (d)'s bound, COMPUTED here from the shipped constants rather than
    // transcribed, so a change to either constant moves the bound with it:
    // RT60 = Q * ln1000 / (pi * f), at the highest Q and the lowest frequency
    // the bank admits, plus 5 s of margin.
    const double kRingOutBoundSeconds =
        (static_cast<double>(Krate::DSP::kMaxResonatorQ)
         * static_cast<double>(Krate::DSP::kLn1000))
            / (static_cast<double>(Krate::DSP::kPi)
               * static_cast<double>(Krate::DSP::kMinResonatorFrequency))
        + 5.0;
    INFO("ring-out bound = "
         << kRingOutBoundSeconds
         << " s (kMaxResonatorQ * kLn1000 / (kPi * kMinResonatorFrequency) + 5)");
    REQUIRE(kRingOutBoundSeconds >= 15.9);
    REQUIRE(kRingOutBoundSeconds <= 16.1);

    // (a), (b) and (d) off one 60 s drive-then-silence render. Shared by the
    // base arm and the (e) worst-case arm, which differ only in lane depths.
    const auto runRingOutArm = [&](bool maxDepths, std::uint32_t seed) {
        ResonanceDriftNetwork net;
        configureMaxQPatch(net, maxDepths, seed);
        REQUIRE(net.getNumPeaks() == kMaxPeaks);
        REQUIRE(net.getMix() == 1.0f);
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i);
            REQUIRE(net.getPeakQ(i) == Catch::Approx(Krate::DSP::kMaxResonatorQ));
        }

        // The tail is rendered PAST the bound so that "still ringing at the very
        // end" is distinguishable from "silent exactly at the bound".
        const RingOutMeasurement m =
            renderRingOut(net, seed ^ 0xA5A5A5A5u, 60.0, kRingOutBoundSeconds + 1.0);
        CAPTURE(m.driveRmsDb, m.drivePeak, m.lastLoudSeconds, m.everSilent);

        // Non-vacuity first: a network that rendered digital silence would
        // satisfy every threshold below.
        REQUIRE(m.drivePeak > 0.0f);
        REQUIRE(m.driveRmsDb > -100.0f);

        REQUIRE(m.allFinite);                          // (a)
        REQUIRE(net.getClampEngagementCount() == 0u);  // (b)
        REQUIRE(m.everSilent);                         // (d)
        REQUIRE(m.lastLoudSeconds <= kRingOutBoundSeconds);
    };

    // (c), on the same patch. Both arms share one seed and one noise seed, so
    // the two renders differ ONLY in numPeaks.
    const auto runNormalisationArm = [&](bool maxDepths, std::uint32_t seed) {
        constexpr std::uint32_t kNoiseSeed = 0x5C01C0DEu;
        const float levelFourDb =
            renderNormalisationLevelDb(std::size_t{4}, maxDepths, seed, kNoiseSeed);
        const float levelTwelveDb =
            renderNormalisationLevelDb(kMaxPeaks, maxDepths, seed, kNoiseSeed);
        const auto derivedDb = static_cast<float>(20.0 * std::log10(std::sqrt(12.0 / 4.0)));
        const float measuredDb = levelFourDb - levelTwelveDb;
        CAPTURE(levelFourDb, levelTwelveDb, measuredDb, derivedDb);

        REQUIRE(levelFourDb > -100.0f);  // non-vacuity: both arms rendered audio
        REQUIRE(levelTwelveDb > -100.0f);
        REQUIRE(derivedDb == Catch::Approx(4.77f).margin(0.01f));
        // The twelve-peak arm is the QUIETER of the two: N is numPeaks.
        REQUIRE(levelTwelveDb < levelFourDb);
        REQUIRE(measuredDb == Catch::Approx(derivedDb).margin(0.5f));
    };

    SECTION("(a)(b)(d) 60 s of white noise at Q = 100, FR-016 anchors") {
        runRingOutArm(false, 0x5C01A001u);
    }

    SECTION("(c) the 1/sqrt(N) law, as a derived relationship") {
        runNormalisationArm(false, 0x5C01C001u);
    }

    SECTION("(e) the same, with all four lane depths and the wander rate at their maxima") {
        runRingOutArm(true, 0x5C01E001u);
        runNormalisationArm(true, 0x5C01E002u);
    }
}

// =============================================================================
// T016 - SC-002: no zipper under drift (roadmap line 227).
//
// Metric: the CONTROL-BOUNDARY DISCONTINUITY RATIO. A steady 220 Hz sine gives a
// deterministic, near-sinusoidal output whose second difference is a clean
// estimator - which is why a broadband drive is NOT used here (ClickDetector is
// unusable on noise, the finding recorded in the Phase 2 spec's helper row).
//
// B and P are the SAME 99.9th percentile of their own populations, and the
// partition is n mod 64 in {0, 1, 2} - see SecondDifferenceSplit for the
// derivation of both.
//
// (c) is the anti-vacuity proof for (a): it injects, through the PUBLIC SURFACE
// only (setSlewCeilings(24, 24) is an in-spec setting under FR-035 - no #ifdef
// hook and no edit to the header under test), a discontinuity the estimator must
// see. It is SYSTEMATIC rather than one-shot because B is the 99.9th percentile
// of a ~135 000-sample population: one jump contributes ~3 outliers, which
// cannot move that percentile at all, so a one-shot injection made B/P > 10
// unreachable by construction (spec correction C-12).
// =============================================================================
TEST_CASE("ResonanceDriftNetwork_NoZipperUnderDrift", "[resonance_drift_network]") {
    constexpr std::uint32_t kSeed = 0x5C02A001u;
    constexpr double kDriveHz = 220.0;
    constexpr float kDriveDbfs = -12.0f;

    // The exact partition sizes, asserted rather than assumed: a partition that
    // silently drifted off the control grid would still produce two
    // plausible-looking numbers. The populations are COMPLETE - n = 0 and n = 1
    // carry a second difference too, because the settle pass primed the
    // estimator's two-sample history (kZipperSettleChunks).
    constexpr std::size_t kExpectedBoundary = 3u * 45000u;
    constexpr std::size_t kExpectedInterior = kPinnedSamples - kExpectedBoundary;
    static_assert(kExpectedBoundary == 135000u, "3/64 of the pinned render");
    static_assert(kExpectedInterior == 2745000u, "the other 61/64");

    // ---- (a) the in-spec drift arm -----------------------------------------
    ZipperStats on;
    {
        ResonanceDriftNetwork net;
        configureZipperPatch(net, kSeed);
        // FR-037: at the 1.0 Hz ceiling the lane clock is NOT decimated, which
        // is the path this criterion is written to stress.
        REQUIRE(net.getLaneDecimation() == 1u);
        on = renderSineZipperStatistic(net, kDriveHz, kDriveDbfs, kPinnedSamples,
                                       [](std::size_t) {});
    }
    CAPTURE(on.boundary, on.interior, on.boundaryCount, on.interiorCount,
            on.interiorAmplitude, on.clampEngagements);
    REQUIRE(on.boundaryCount == kExpectedBoundary);
    REQUIRE(on.interiorCount == kExpectedInterior);
    REQUIRE(on.interior > 0.0f);  // non-vacuity: a silent render has P = 0
    // LINEARITY, checked before the ratio is believed: a clipped render's second
    // difference belongs to FR-018's clamp in both populations at once, and the
    // ratio it produces is ~1.06 whatever the network is doing (see
    // kZipperWetGainDb). Without this the (a) arm cannot fail.
    REQUIRE(on.clampEngagements == 0u);
    REQUIRE(on.boundary <= kBoundaryRatio * on.interior);

    // ---- (b) the control arm, wander off ------------------------------------
    ZipperStats off;
    {
        ResonanceDriftNetwork net;
        configureZipperPatch(net, kSeed);
        net.setWanderEnabled(false);
        REQUIRE(net.isWanderEnabled() == false);
        off = renderSineZipperStatistic(net, kDriveHz, kDriveDbfs, kPinnedSamples,
                                        [](std::size_t) {});
    }
    CAPTURE(off.boundary, off.interior, off.interiorAmplitude, off.clampEngagements);
    REQUIRE(off.interior > 0.0f);
    // With no retuning happening at all the boundary population must be
    // statistically indistinguishable from the interior one...
    REQUIRE(off.boundary <= kControlArmRatio * off.interior);

    // ---- (b), second assertion: the interior curvature, NORMALISED ---------
    //
    // *** SPEC CORRECTION C-14 - READ THIS BEFORE CHANGING THE LINE BELOW ***
    //
    // spec.md SC-002 (b) originally asked for |P_on - P_off| / P_off <= 0.10,
    // on the RAW statistic, justified as "the underlying signal is the same
    // sine in both arms, so the interior curvature must not move". The premise
    // is false, and no implementation can satisfy the assertion:
    //
    //   * P is the 99.9th percentile of |x[n] - 2x[n-1] + x[n-2]|, which for a
    //     narrowband signal is PROPORTIONAL TO OUTPUT AMPLITUDE (the identity
    //     in sineCurvaturePerUnitAmplitude above). It is a LEVEL statistic as
    //     much as a curvature one.
    //   * The two arms do NOT carry the same output level, by construction:
    //     SC-002 (a)'s mandated patch runs all four lanes at MAXIMUM depth, so
    //     the on arm's gain lane alone swings the peak level over
    //     [-24, +24] dB around base (FR-033, jointly clamped at +12 by FR-017)
    //     and its frequency lane sweeps resonances onto and off the 220 Hz
    //     drive. The input sine is the same; the OUTPUT is not, and cannot be.
    //
    // Measured on the shipped build, 60 s renders, seed 0x5C02A001 unless
    // stated (the +30 dB rows are the then-provisional kDefaultWetGainDb this
    // fixture used to run at - T019 has since MEASURED that constant at
    // +34.5 dB, i.e. hotter, so these rows understate the clipping the fixture
    // is avoiding; kept here because they are the reason the raw form looked
    // like a component defect rather than a criterion defect):
    //     wet trim   arm    P            A(99.9)   P/A / analytic   |P_on-P_off|/P_off
    //     +30 dB     off    0.0007169    0.86421   1.0003           -
    //     +30 dB     on     0.1518106    4.00000   45.77            210.8   <-- red
    //       0 dB     off    0.0000227    0.02733   1.0004           -
    //       0 dB     on     0.0003562    0.43357   0.9907            14.7   <-- still red
    // and the off arm's normalised figure is 1.0003 for seeds 0x11111111 and
    // 0xDEADBEEF too. The raw form is three and a half orders of magnitude from
    // its bound WITH the clamp and still 147x from it on a perfectly linear
    // render: it is not a defect this component can fix, it is an unsatisfiable
    // criterion.
    //
    // What IS invariant - and is what the spec sentence was reaching for - is
    // the curvature PER UNIT AMPLITUDE: 0.9907 vs 1.0004 of the closed form,
    // i.e. the two arms agree to 0.97 %, an order of magnitude inside the
    // spec's own 0.10. The assertion below is therefore the same claim with the
    // level divided out, and it is made against the CLOSED FORM on the control
    // arm rather than as an on-vs-off ratio, which is strictly SHARPER than the
    // difference it replaces: it says the control arm's interior population is
    // the drive sine's own curvature and NOTHING else - no residual retuning,
    // no numerical junk, no partition drift - which is exactly "the estimator
    // is reading retuning rather than the drive".
    //
    // A linear render is a PRECONDITION of the identity, so it is checked, not
    // assumed.
    REQUIRE(off.clampEngagements == 0u);
    REQUIRE(off.interiorAmplitude > 0.0f);
    const double kAnalyticCurvature = sineCurvaturePerUnitAmplitude(kDriveHz, kFs);
    const double offCurvature =
        static_cast<double>(off.interior) / static_cast<double>(off.interiorAmplitude);
    const double curvatureError =
        std::abs(offCurvature - kAnalyticCurvature) / kAnalyticCurvature;
    CAPTURE(offCurvature, kAnalyticCurvature, curvatureError);
    REQUIRE(curvatureError <= static_cast<double>(kInteriorCurvatureTolerance));

    // Directional, and the half of the original comparison that survives
    // intact: switching retuning ON can only ADD curvature to the interior
    // population - it adds coefficient motion and it adds level - so a build
    // whose on arm is quieter or smoother inside the chunk than its control arm
    // has stopped retuning. (Measured 212x here, 15.7x at a linear trim; this
    // is a direction check, NOT a bound on how far P may move.)
    REQUIRE(on.interior > off.interior);
    CAPTURE(on.clampEngagements, on.interiorAmplitude);

    // ---- (c) the mandatory, systematic injection ----------------------------
    ZipperStats injected;
    {
        ResonanceDriftNetwork net;
        configureZipperPatch(net, kSeed);
        net.setSlewCeilings(ResonanceDriftNetwork::kMaxSlewOctaves,
                            ResonanceDriftNetwork::kMaxSlewOctaves);
        REQUIRE(net.getFreqSlewCeiling()
                == Catch::Approx(ResonanceDriftNetwork::kMaxSlewOctaves));
        // The schedule is a +-1-octave anchor jump on every kInjectionPeriod-th
        // control chunk, with the SIDE drawn from a coin rather than alternated
        // (spec correction C-17). Two changes from "alternating, on every
        // chunk", both forced by measurement and neither of them softening the
        // injection:
        //
        //  * ON EVERY CHUNK is a 750 Hz square modulation of twelve resonances
        //    that sit at 110-880 Hz, i.e. modulation at ~2f for the peaks near
        //    375 Hz: a PARAMETRIC PUMP. Measured on the shipped build, that
        //    render's wet sum passes 3.4e38 and goes non-finite 1.83 s in and
        //    stays non-finite for the remaining 97 % of the render - so the
        //    statistic was being computed over NaNs, through a std::sort whose
        //    comparator NaN makes non-transitive, and the numbers it produced
        //    (B = 0.014, P = 0.0016) were undefined behaviour, not evidence.
        //    C-15's guard now keeps the output finite, but the render still
        //    saturates the clamp at EVERY trim (the pump is upstream of the
        //    trim), so it can never be the linear render the statistic needs.
        //  * ALTERNATING is what makes the pump coherent. A coin flip spreads
        //    the modulation across the spectrum; measured, it is also the most
        //    stable of the schedules tried across seeds (4.01 / 4.00 / 4.66
        //    against 4.21 / 3.75 / 5.09 for a fixed period-32 alternation).
        //
        // Perturbing one chunk in sixteen still moves the whole statistic and
        // not a few outliers, which is what correction C-12 required: ~2 860
        // draws, ~1 430 actual jumps, 3 boundary samples each, against a
        // boundary population whose 0.1 % tail is 135 samples.
        constexpr std::size_t kInjectionPeriod = 16;
        Krate::DSP::Xorshift32 injectionRng{0x5C02C001u};
        injected = renderSineZipperStatistic(
            net, kDriveHz, kDriveDbfs, kPinnedSamples,
            [&net, &injectionRng](std::size_t chunkIndex) {
                if ((chunkIndex % kInjectionPeriod) != 0u) {
                    return;
                }
                // Xorshift32::nextFloat() is uniform on [-1, 1] (random.h:59).
                const float octave = (injectionRng.nextFloat() > 0.0f) ? 2.0f : 1.0f;
                for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                    net.setPeakAnchorHz(i, zipperAnchorHz(i) * octave);
                }
            });
    }
    CAPTURE(injected.boundary, injected.interior, injected.clampEngagements);
    REQUIRE(injected.interior > 0.0f);
    REQUIRE(injected.clampEngagements == 0u);  // the same linearity precondition
    REQUIRE(injected.boundary > kInjectionRatio * injected.interior);
    // ...and, said the way the arm's purpose reads: an injection this arm
    // passes is one the (a) bound would have gone RED on.
    REQUIRE(injected.boundary > kBoundaryRatio * injected.interior);

    // ---- WHAT THIS CASE CAUGHT, recorded where it was measured -------------
    //
    // The (a) arm was green at B/P = 1.06 before the fixture was made linear,
    // and that number was not evidence of anything: at +30 dB both populations
    // were the clipper's. On the linear fixture the SHIPPED build measured
    //
    //     lanes open              B          P          B/P
    //     all four (SC-002 (a))   0.003983   0.000356   11.18   <-- vs a 1.5 bound
    //     frequency only          0.000105   0.000105    1.00
    //     Q only                  0.000057   0.000056    1.02
    //     gain only               0.003260   0.000207   15.72
    //     pan only                0.000249   0.000028    8.79
    //
    // - a real zipper, in the two quantities the control grid writes as
    // MULTIPLIES rather than as coefficients: the bank's per-slot gain (through
    // ResonatorBank::setGain, a bare gains_[i] = dbToGain(dB) with no smoother
    // of any kind, resonator_bank.h:367-371) and the equal-power pan pair, both
    // held constant for the whole 64-sample chunk and stepped at its boundary.
    // Frequency and Q, the two FR-035's limiter guards, were already clean at
    // 1.00 and 1.02: a biquad coefficient change moves the output's SLOPE, a
    // gain or pan change moves the SAMPLE.
    //
    // Spec correction C-16 carries both across the chunk instead (one linear
    // segment per control step, retargeted from where the last one ended, so
    // block-size invariance is untouched). Re-measured on the same fixture:
    // B/P = 1.010 / 0.992 / 1.023 across the three seeds, against 1.5.
    //
    // FR-035 had sanctioned the gain step explicitly ("its per-chunk step is
    // bounded by the gain lane's own 150 ms output smoother") and SC-002's own
    // preamble asserted the pan lane "contributes no new boundary
    // discontinuities of the kind this criterion targets"; the 15.72 and the
    // 8.79 are what those two claims were worth. Both FR texts are corrected.
    //
    // CLOSED BY T019: kDefaultWetGainDb is the constant that made all of this
    // invisible, and it has now been measured - +34.5 dB, hotter than the
    // +30 dB the rows above were taken at. This fixture keeps its own trim
    // regardless: a criterion about a boundary discontinuity has to be measured
    // on a render that is not clipping.
}

// =============================================================================
// T016 - SC-018 (c): the slew limiter's ACTUAL PURPOSE - a note change must not
// step the coefficients.
//
// This arm belongs to the SC-018 criterion whose other arms live in
// ResonanceDriftNetwork_SlewLimit (main TU, T007). It carries its OWN test-case
// name because Catch2 rejects two TEST_CASEs with the same name in one binary,
// and it lives HERE because it needs SC-002's pinned-60 s machinery.
//
// It renders for the SAME pinned 60 s as SC-002 (a), with the same partition,
// the same population sizes and the same statistic, repeating the 55 <-> 110 Hz
// note step on a fixed 1 s schedule - so SC-002 (a)'s bound transfers
// legitimately. Measuring B/P across ONE 50-step transition instead (~150
// boundary and ~3 050 interior samples) would put the two statistics in a
// different extreme-value regime from the one kBoundaryRatio was measured in -
// exactly the max-vs-percentile mismatch SC-002's own rewrite eliminated.
// =============================================================================
TEST_CASE("ResonanceDriftNetwork_SlewLimitBoundaryRatio", "[resonance_drift_network]") {
    ResonanceDriftNetwork net;
    net.setSeed(0x5C18C001u);
    net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setAnchorMode(ResonanceDriftNetwork::AnchorMode::Keyed);
    net.setWanderEnabled(false);
    net.setMix(1.0f);
    // The same linear trim SC-002's arms use, for the same reason (C-17): this
    // arm compares against kBoundaryRatio, which is only meaningful on a render
    // that never reaches FR-018's clamp.
    net.setWetGain(kZipperWetGainDb);

    // The DEFAULT ceiling, not SC-002 (c)'s opened one: this arm is about the
    // limiter doing its job, so it must run at the setting FR-035 ships.
    REQUIRE(net.getFreqSlewCeiling()
            == Catch::Approx(ResonanceDriftNetwork::kDefaultFreqStepOctaves));
    REQUIRE(net.getNoteFrequency() == Catch::Approx(55.0f));
    REQUIRE(net.getAnchorMode() == ResonanceDriftNetwork::AnchorMode::Keyed);

    constexpr std::size_t kChunksPerSecond = 750;
    static_assert(kChunksPerSecond * kChunk == 48000u, "one second of control chunks");

    // Peak 0's frequency sampled immediately BEFORE each note step, i.e. after a
    // full second of settling at the previous target - the non-vacuity evidence
    // that the sixty note changes really moved the peaks.
    std::vector<float> sampledHz;
    sampledHz.reserve(64);

    const ZipperStats stats = renderSineZipperStatistic(
        net, 220.0, -12.0f, kPinnedSamples, [&](std::size_t chunkIndex) {
            // The note schedule belongs to the MEASURED window: the hook also
            // runs during renderSineZipperStatistic's settle pass, and a note
            // step there would be a sixty-first transition sitting outside the
            // sixty this arm counts.
            if (chunkIndex < kZipperSettleChunks) {
                return;
            }
            const std::size_t measuredChunk = chunkIndex - kZipperSettleChunks;
            if ((measuredChunk % kChunksPerSecond) != 0u) {
                return;
            }
            sampledHz.push_back(net.getPeakCurrentFrequency(std::size_t{0}));
            const std::size_t step = measuredChunk / kChunksPerSecond;
            net.setNoteFrequency(((step % 2u) == 0u) ? 110.0f : 55.0f);
        });

    REQUIRE(sampledHz.size() == 60u);
    const float lowestHz = *std::min_element(sampledHz.begin(), sampledHz.end());
    const float highestHz = *std::max_element(sampledHz.begin(), sampledHz.end());
    CAPTURE(lowestHz, highestHz);
    REQUIRE(lowestHz > 0.0f);
    REQUIRE(highestHz / lowestHz >= 1.9f);  // ~one octave, sixty times over

    CAPTURE(stats.boundary, stats.interior, stats.boundaryCount, stats.interiorCount,
            stats.clampEngagements);
    REQUIRE(stats.boundaryCount == 3u * 45000u);  // complete: the settle primed it
    REQUIRE(stats.interior > 0.0f);
    // Same linearity precondition as SC-002's arms, for the same reason.
    REQUIRE(stats.clampEngagements == 0u);
    REQUIRE(stats.boundary <= kBoundaryRatio * stats.interior);
}

// =============================================================================
// T018 - SC-003: wander-rate spectral tests (roadmap line 227)
// =============================================================================
//
// The case is deliberately written WITHOUT Catch2 SECTIONs. Catch2 re-executes
// a TEST_CASE's whole body once per leaf section, and this case's body contains
// two pinned 350 s renders plus three multi-minute lane records; putting the
// arms in sections would re-render all of that for every arm. The arms are
// therefore sequential, each opening with a comment naming the SC-003 letter it
// discharges, and each preceded by the non-vacuity check that makes it real.
//
// WHAT EACH ARM IS FOR, since three of the six would be vacuous written the
// obvious way:
//   (a) the band-energy trajectory really persists at the configured rate;
//   (b) it does so BECAUSE of the wander - the control arm is the null;
//   (c) FR-030/FR-031's bounds hold over > 1e5 samplings of the real render
//       (the T008 LaneBounds (a) property, re-measured under audio load);
//   (d) each peak's own lane is neither frozen nor white;
//   (e) the twelve lanes are INDEPENDENT - measured against a bound that must
//       come from a null distribution, because the naive 0.05 fails on a
//       correct build (spec.md's SC-003 (e) derivation);
//   (f) setWanderRate is not inert - the ONLY arm that fails if FR-037's
//       decimation is missing and the whole [0.002, 0.0333] Hz sub-range,
//       including this component's own 0.03 Hz default, is a dead zone.
// =============================================================================
TEST_CASE("ResonanceDriftNetwork_WanderRateSpectral", "[resonance_drift_network][long]") {
    constexpr std::uint32_t kNetSeed = 0x5C030001u;
    constexpr std::uint32_t kDriveSeedL = 0x9E3779B9u;
    constexpr std::uint32_t kDriveSeedR = 0x85EBCA6Bu;

    // -------------------------------------------------------------------------
    // The wander-ON render: SC-003 (a), (c), (d)'s short lag and (e)
    // -------------------------------------------------------------------------
    ResonanceDriftNetwork on;
    on.setSeed(kNetSeed);
    on.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    on.setMix(1.0f);
    on.setWetGain(kSpectralWetGainDb);

    // The criterion is written about the FR-016 DEFAULTS, so the defaults are
    // asserted rather than assumed - a future change to kDefaultWanderRateHz
    // would otherwise silently move T and every lag derived from it.
    REQUIRE(on.getWanderRate() == Catch::Approx(ResonanceDriftNetwork::kDefaultWanderRateHz));
    REQUIRE(on.isWanderEnabled() == true);
    REQUIRE(on.getMix() == 1.0f);
    // FR-037's mapping at the default rate (T008's worked table): 1/0.03 = 33.3 s
    // requested, ceil(33.3 / BrownianDrift::kTauMax) = 2.
    REQUIRE(on.getLaneDecimation() == 2u);

    const SpectralRun runOn = renderSpectralRun(on, kDriveSeedL, kDriveSeedR, true);
    REQUIRE(runOn.allFinite);
    // Linearity precondition for every band FRACTION below (see
    // kSpectralWetGainDb): a clipped render's band split is the clipper's.
    REQUIRE(runOn.clampEngagements == 0u);
    REQUIRE(runOn.band[0].size() == kSpectralWindows);
    REQUIRE(runOn.freqLog2[0].size() == kSpectralChunks);

    // ---- (c) FR-030/FR-031 bounds over >= 1e5 samplings ---------------------
    CAPTURE(runOn.samplings, runOn.freqExcursions, runOn.qExcursions);
    REQUIRE(runOn.samplings >= 100000u);
    REQUIRE(runOn.freqExcursions == 0u);
    REQUIRE(runOn.qExcursions == 0u);
    // NON-VACUITY: every bound above passes on a build whose lanes never
    // advance, so each peak must have covered real ground. 0.02 octaves is
    // 24 cents against a default depth of +/- 300.
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        CAPTURE(i, runOn.spanOctaves[i]);
        REQUIRE(runOn.spanOctaves[i] > 0.02f);
    }

    // ---- band selection (fixed rule, recorded in compliance.md) -------------
    const std::size_t selected = selectMotionBand(runOn);
    const double meanFractionOn = (selected < 5u) ? meanOf(runOn.band[selected]) : 0.0;
    const double cvOn = (selected < 5u) ? coefficientOfVariation(runOn.band[selected]) : 0.0;
    CAPTURE(selected, meanFractionOn, cvOn);
    REQUIRE(selected < 5u);  // at least one band cleared kBandEligibilityFraction
    REQUIRE(meanFractionOn >= kBandEligibilityFraction);
    REQUIRE(cvOn > 0.0);

    // ---- (a) persistence of the selected band's trajectory at T/8 -----------
    // The trajectory is sampled every 100 ms, so the lag in samples is
    // (T/8) / 0.1 s = 41.67 -> 42.
    const auto bandLag = static_cast<std::size_t>(
        std::lround((kSpectralT / 8.0) / (static_cast<double>(kFeatureWindowSamples) / kFs)));
    REQUIRE(bandLag == 42u);
    const double bandAcf = autocorrelationAt(runOn.band[selected], bandLag);
    CAPTURE(bandAcf, bandLag);
    REQUIRE(bandAcf >= kBandPersistence);

    // ---- (b) the control arm: same seed, same drive, wander off -------------
    ResonanceDriftNetwork off;
    off.setSeed(kNetSeed);
    off.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
    off.setMix(1.0f);
    off.setWetGain(kSpectralWetGainDb);
    off.setWanderEnabled(false);
    REQUIRE(off.isWanderEnabled() == false);

    const SpectralRun runOff = renderSpectralRun(off, kDriveSeedL, kDriveSeedR, false);
    REQUIRE(runOff.allFinite);
    REQUIRE(runOff.clampEngagements == 0u);
    // FR-034 freezes the drift by zeroing the DEPTHS, so the control arm's
    // readings sit at the anchors - inside the same bounds, scanned with the
    // same predicate.
    REQUIRE(runOff.freqExcursions == 0u);
    REQUIRE(runOff.qExcursions == 0u);
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        CAPTURE(i, runOff.spanOctaves[i]);
        REQUIRE(runOff.spanOctaves[i] < 0.001f);  // frozen, as the control arm must be
    }

    const double cvOff = coefficientOfVariation(runOff.band[selected]);
    CAPTURE(cvOff, cvOn / std::max(cvOff, 1.0e-12), kWanderCvRatio);
    // > 0 rather than == 0: the control arm still sees a stochastic DRIVE, so
    // its band fractions fluctuate. The criterion is the RATIO.
    REQUIRE(cvOff > 0.0);
    REQUIRE(cvOn >= kWanderCvRatio * cvOff);

    // ---- (d), short lag, at the criterion's own rate ------------------------
    // (T/8) in control steps: 4.1667 s * 48000 / 64 = 3125.
    const auto lagShortSteps = static_cast<std::size_t>(
        std::lround((kSpectralT / 8.0) * kFs / static_cast<double>(kChunk)));
    REQUIRE(lagShortSteps == 3125u);
    for (std::size_t i = 0; i < kMaxPeaks; ++i) {
        const double r = autocorrelationAt(runOn.freqLog2[i], lagShortSteps);
        CAPTURE(i, r);
        // exp(-1/8) = 0.88 for a pure OU at this lag, so 0.20 is a floor a
        // per-step white-noise "lane" cannot clear.
        REQUIRE(r >= kLanePersistence);
    }

    // ---- (e) independence, against a measured bound and an in-process null --
    {
        double sumAbsR = 0.0;
        std::size_t pairs = 0;
        double worstAbsR = 0.0;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            for (std::size_t j = i + 1u; j < kMaxPeaks; ++j) {
                const double r = std::abs(pearsonR(runOn.freqLog2[i], runOn.freqLog2[j]));
                sumAbsR += r;
                worstAbsR = std::max(worstAbsR, r);
                ++pairs;
            }
        }
        REQUIRE(pairs == 66u);  // C(12,2)
        const double meanAbsR = sumAbsR / static_cast<double>(pairs);
        CAPTURE(meanAbsR, worstAbsR, kLaneIndependenceR);
        REQUIRE(meanAbsR <= kLaneIndependenceR);

        // ANTI-VACUITY, in the same process and with the identical estimator:
        // twelve lanes given the SAME seed AND the same salt are the same walk,
        // so the statistic must read ~1. Without this the arm cannot tell "the
        // FR-005 salts work" from "the estimator is noise".
        std::array<Krate::DSP::BrownianDrift, kMaxPeaks> control{};
        std::array<std::vector<float>, kMaxPeaks> controlTraj{};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            control[i].setSeed(0x1DE7A1C0u);
            // The smoothness setWanderRate derives for the 0.03 Hz default
            // (T008's worked table: decimation 2, tau 16.67 s, s 0.552).
            control[i].setSmoothness(0.552f);
            control[i].prepare(kFs);  // initState() reseeds from the configured seed
            controlTraj[i].reserve(kSpectralChunks);
        }
        for (std::size_t s = 0; s < kSpectralChunks; ++s) {
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                control[i].processBlock(kChunk);
                controlTraj[i].push_back(control[i].getCurrentValue());
            }
        }
        double controlSum = 0.0;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            for (std::size_t j = i + 1u; j < kMaxPeaks; ++j) {
                controlSum += std::abs(pearsonR(controlTraj[i], controlTraj[j]));
            }
        }
        const double controlMeanAbsR = controlSum / static_cast<double>(pairs);
        CAPTURE(controlMeanAbsR, kLaneControlR);
        REQUIRE(controlMeanAbsR >= kLaneControlR);
    }

    // -------------------------------------------------------------------------
    // The lane records: SC-003 (d)'s long lag and SC-003 (f)
    // (see the "WHY THESE TWO ARMS DO NOT RUN ON SC-003's OWN 350 s RENDER"
    //  note above the fixture for the estimator arithmetic that forces this)
    // -------------------------------------------------------------------------
    const auto recordLanes = [](std::uint32_t seed, float rateHz, std::size_t seconds,
                                std::array<std::vector<float>, kMaxPeaks>& traj) {
        ResonanceDriftNetwork net;
        configureLaneFixture(net, seed, rateHz);
        const std::size_t steps = seconds * kLaneStepsPerSecond;
        for (auto& v : traj) {
            v.clear();
            v.reserve(steps);
        }
        recordFreqLog2Silent(net, steps, traj);
        return net.getLaneDecimation();
    };

    // ---- (d), long lag: 1200 s at wanderRate = 1.0 Hz (T = 1 s) -------------
    {
        std::array<std::vector<float>, kMaxPeaks> traj{};
        const std::size_t decimation =
            recordLanes(0x5C03000Du, ResonanceDriftNetwork::kMaxWanderRateHz, 1200, traj);
        REQUIRE(decimation == 1u);  // the un-decimated path
        REQUIRE(traj[0].size() == 1200u * kLaneStepsPerSecond);

        // 125 / 8 = 15 steps = 0.12 s, the closest whole control step to T/8;
        // 8T = 1000 steps exactly.
        constexpr std::size_t kLagShortLane = kLaneStepsPerSecond / 8u;
        constexpr std::size_t kLagLongLane = 8u * kLaneStepsPerSecond;
        static_assert(kLagShortLane == 15u && kLagLongLane == 1000u, "the lane lag arithmetic");

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const double rShort = autocorrelationAt(traj[i], kLagShortLane);
            const double rLong = autocorrelationAt(traj[i], kLagLongLane);
            const auto span =
                static_cast<double>(*std::max_element(traj[i].begin(), traj[i].end())
                                    - *std::min_element(traj[i].begin(), traj[i].end()));
            CAPTURE(i, rShort, rLong, span);
            REQUIRE(rShort >= kLanePersistence);
            REQUIRE(rLong <= kLaneDecorrelation);
            // NON-VACUITY: a lane pinned at a constant has an undefined
            // autocorrelation the helper reports as 0, which would pass the
            // decorrelation bound. 0.1 octaves is 120 cents against +/- 600.
            REQUIRE(span > 0.1);
        }
    }

    // ---- (f) setWanderRate is not inert -------------------------------------
    // Same seed, same fixture, same record length, same absolute lag: the ONLY
    // difference between the two arms is the rate, so the separation cannot be
    // an artefact of anything else.
    {
        constexpr std::uint32_t kRateSeed = 0x5C03000Fu;
        constexpr std::size_t kRateSeconds = 2000;  // 10*T of the SLOW arm (T = 200 s)
        const auto lag = static_cast<std::size_t>(
            std::lround(kRateSeparationLagSeconds * static_cast<double>(kLaneStepsPerSecond)));
        REQUIRE(lag == 209u);

        std::array<std::vector<float>, kMaxPeaks> slowTraj{};
        const std::size_t slowDecimation = recordLanes(kRateSeed, 0.005f, kRateSeconds, slowTraj);
        // FR-037's whole point: without the decimation, 0.005 Hz is inside the
        // dead zone and this arm's slow render would be indistinguishable from
        // the fast one.
        REQUIRE(slowDecimation == 7u);
        double slowSum = 0.0;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            slowSum += autocorrelationAt(slowTraj[i], lag);
        }
        const double rhoSlow = slowSum / static_cast<double>(kMaxPeaks);

        std::array<std::vector<float>, kMaxPeaks> fastTraj{};
        const std::size_t fastDecimation = recordLanes(kRateSeed, 0.3f, kRateSeconds, fastTraj);
        REQUIRE(fastDecimation == 1u);
        double fastSum = 0.0;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            fastSum += autocorrelationAt(fastTraj[i], lag);
        }
        const double rhoFast = fastSum / static_cast<double>(kMaxPeaks);

        CAPTURE(rhoSlow, rhoFast, rhoSlow - rhoFast, kRateSeparation);
        REQUIRE(rhoSlow - rhoFast >= kRateSeparation);
    }
}

// =============================================================================
// SC-003 (b) CALIBRATION - kWanderCvRatio, measured instead of inherited
// =============================================================================
//
// The Bound provenance note names SC-003 (a)/(b) as thresholds "Vorago Phase 2
// MEASURED rather than assumed", and binds them: "any threshold in this section
// that proves unable to separate a correct implementation from an injected
// defect must be re-derived from a measured distribution across seeds and the
// change recorded, never merely widened."
//
// 1.8 was transcribed from Phase 2 and it does NOT separate anything here: it
// is a bound on a RATIO whose numerator and denominator are both estimated from
// 3 500 windows of a stochastic drive, and the shipped fixture measured
// cvOn/cvOff = 1.397 on a build every other arm of SC-003 passes. The reason is
// structural rather than a defect: the control arm is not silent - FR-034
// zeroes the DEPTHS, so the wander-off render is twelve fixed resonators on the
// same pink drive, and its band-fraction CV is dominated by that drive.
// Phase 2's 1.8 was measured on a different component, a different band
// selection and a different control arm.
//
// This case measures the null across eight network seeds and reports the bound
// that BELONGS below the observed minimum. It is the lower-bound form of
// reportDistribution's discipline: a build whose wander is inert reads
// cvOn/cvOff = 1.0 EXACTLY (the two arms become the same render), so any bound
// strictly above 1.0 still fails that defect, and the criterion keeps its
// teeth at whatever figure the measurement puts it at.
//
// Tagged [.calibration], like the perf TU's ResonanceDriftNetwork_MeasureThresholds:
// hidden from the default run, invoked by name when a bound is being derived.
// It lives in this TU rather than that one because it needs this TU's fixture -
// renderSpectralRun, the fixed band-selection rule and the 350 s render length
// are the criterion, and a re-implementation next to the other calibrations
// would be measuring a different statistic.
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_MeasureWanderCvRatio", "[resonance_drift_network][.calibration]") {
    constexpr std::array<std::uint32_t, 8> kSeeds{0x5C03B001u, 0x5C03B002u, 0x5C03B003u,
                                                  0x5C03B004u, 0x5C03B005u, 0x5C03B006u,
                                                  0x5C03B007u, 0x5C03B008u};

    std::vector<double> ratios;
    std::vector<double> cvOnValues;
    std::vector<double> cvOffValues;
    std::array<std::size_t, 6> selectedHistogram{};
    ratios.reserve(kSeeds.size());

    for (const std::uint32_t seed : kSeeds) {
        ResonanceDriftNetwork on;
        on.setSeed(seed);
        on.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        on.setMix(1.0f);
        on.setWetGain(kSpectralWetGainDb);
        const SpectralRun runOn = renderSpectralRun(on, seed ^ 0x9E3779B9u, seed ^ 0x85EBCA6Bu,
                                                    /*recordTrajectories=*/false);
        REQUIRE(runOn.allFinite);
        REQUIRE(runOn.clampEngagements == 0u);

        const std::size_t selected = selectMotionBand(runOn);
        ++selectedHistogram[selected];
        REQUIRE(selected < 5u);

        ResonanceDriftNetwork off;
        off.setSeed(seed);
        off.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        off.setMix(1.0f);
        off.setWetGain(kSpectralWetGainDb);
        off.setWanderEnabled(false);
        const SpectralRun runOff = renderSpectralRun(off, seed ^ 0x9E3779B9u, seed ^ 0x85EBCA6Bu,
                                                     /*recordTrajectories=*/false);
        REQUIRE(runOff.allFinite);
        REQUIRE(runOff.clampEngagements == 0u);

        const double cvOn = coefficientOfVariation(runOn.band[selected]);
        const double cvOff = coefficientOfVariation(runOff.band[selected]);
        REQUIRE(cvOn > 0.0);
        REQUIRE(cvOff > 0.0);
        ratios.push_back(cvOn / cvOff);
        cvOnValues.push_back(cvOn);
        cvOffValues.push_back(cvOff);
    }

    REQUIRE(ratios.size() == kSeeds.size());

    const auto describe = [](const std::vector<double>& v) {
        double sum = 0.0;
        for (const double x : v) {
            sum += x;
        }
        const double mean = sum / static_cast<double>(v.size());
        double sq = 0.0;
        for (const double x : v) {
            sq += (x - mean) * (x - mean);
        }
        const double sd = std::sqrt(sq / static_cast<double>(v.size() - 1u));
        const double lo = *std::min_element(v.begin(), v.end());
        const double hi = *std::max_element(v.begin(), v.end());
        std::ostringstream o;
        o << "mean " << mean << "  sd " << sd << "  min " << lo << "  max " << hi;
        return o.str();
    };

    const double worstRatio = *std::min_element(ratios.begin(), ratios.end());
    // The lower-bound form of the perf TU's reportDistribution: a LOWER bound
    // sits BELOW the observed minimum, by one standard deviation or 10 % of the
    // extreme, whichever is larger.
    double sum = 0.0;
    for (const double x : ratios) {
        sum += x;
    }
    const double mean = sum / static_cast<double>(ratios.size());
    double sq = 0.0;
    for (const double x : ratios) {
        sq += (x - mean) * (x - mean);
    }
    const double sd = std::sqrt(sq / static_cast<double>(ratios.size() - 1u));
    const double suggested = worstRatio - std::max(sd, 0.1 * std::abs(worstRatio));

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  SC-003 (b): kWanderCvRatio - the ratio of the selected band's fraction CV\n"
       << "  between the wander-ON arm and the setWanderEnabled(false) control arm.\n"
       << "  FR-016 defaults, mix = 1, pink noise at -12 dBFS, 350 s at 48 kHz, 8 seeds,\n"
       << "  the criterion's own fixture (renderSpectralRun + the fixed band selection).\n"
       << "=================================================================================\n"
       << "      cvOn                : " << describe(cvOnValues) << "\n"
       << "      cvOff               : " << describe(cvOffValues) << "\n"
       << "      cvOn / cvOff        : " << describe(ratios) << "\n"
       << "      provisional bound   : 1.8  (transcribed from Phase 2)\n"
       << "      suggested LOWER bound (below the observed minimum, margin = max(sd, 10 %)): "
       << suggested << "\n"
       << "      band selected, count per index 0..4 (5 = none eligible): ";
    for (const std::size_t n : selectedHistogram) {
        os << n << ' ';
    }
    os << "\n"
       << "      An inert-wander build reads EXACTLY 1.0 here (both arms become the same\n"
       << "      render), so any bound above 1.0 still fails the defect this arm guards.\n"
       << "=================================================================================\n";
    WARN(os.str());
}

// =============================================================================
// T018 - SC-016: the 30-minute boundedness soak (roadmap line 483)
// =============================================================================
//
// "A drone instrument that can run away or die overnight is broken by
// definition" (roadmap line 484). This is the worst-case patch the spec admits:
// every wander depth at its maximum, wanderRate at its 1.0 Hz ceiling, Q at
// kMaxResonatorQ, and an external wake pattern toggling peaks on the
// SlowEventScheduler's own 20-90 s default range - driven by the test, because
// FR-040 makes the wake an INPUT to this component and Phase 10 owns the
// scheduler that will drive it.
//
// EIGHT SEEDS, NOT ONE - AND (b)'s +/- 0.5 dB IS KEPT, NOT WIDENED.
// The spec sets (b) at "+/- 0.5 dB per 30 minutes - no creep in either
// direction" and does NOT list it among the to-be-measured bounds. Measuring it
// showed why one soak cannot carry it. On this patch the window RMS is
// heavy-tailed - a 24 dB gain-wander lane is log-normal, so a few excursions
// dominate every window's energy - and the least-squares change of ONE 30-minute
// record has a null spread of sd 0.49 dB with extremes of -0.78 and +0.45 across
// eight seeds. A +/- 0.5 dB bound on a single seed is therefore a coin flip, and
// a real 1 dB creep would hide inside it. That is exactly the situation
// spec.md's Bound provenance note describes ("unable to separate a correct
// implementation from an injected defect"), and its instruction is to re-derive
// - which here means fixing the STATISTIC, since the bound is the honest one.
//
// The mean across the eight seeds has a standard error of 0.49/sqrt(8) = 0.17 dB
// and holds +/- 0.5 dB with room, while a genuine creep - which is common to
// every seed - moves the mean one-for-one. The per-seed spread is reported
// alongside so a future reader sees both numbers.
//
// The same eight renders also make (a), (c) and (d) eight-seed statements
// instead of one-seed ones, which is what spec.md asks for in as many words
// ("a soak across >= 8 seeds", "the same >= 8 seeds").
//
// ATTRIBUTED, NOT ASSUMED: before this shape was adopted, the creep was
// measured against three controls (a scratch probe, 8 seeds x 30 min each).
// With the wake script's own defect fixed - see WakePattern, whose first
// stationary form still failed to PUSH its initial state and so could only ever
// sleep peaks - the network's own drift with NO wake events at all reads a mean
// of -0.09 dB per 30 minutes over eight seeds, i.e. consistent with zero. The
// component does not creep; the single-seed estimator was the problem.
// =============================================================================
TEST_CASE("ResonanceDriftNetwork_LongRenderBoundedness", "[resonance_drift_network][long]") {
    // Eight seeds, fixed. The first is the seed this criterion soaked when it
    // was a single-seed case, kept first so its readings stay comparable.
    constexpr std::array<std::uint32_t, 8> kSoakSeeds{0x5C160001u, 0x5C160002u, 0x5C160003u,
                                                      0x5C160004u, 0x5C160005u, 0x5C160006u,
                                                      0x5C160007u, 0x5C160008u};

    struct SoakStats {
        double medianDb = 0.0;
        double worstDeviationDb = 0.0;
        double lowestDb = 300.0;
        double highestDb = -300.0;
        double totalChangeDb = 0.0;
        float worstPeak = 0.0f;
        std::size_t events = 0;
        std::size_t toggled = 0;
        std::size_t minAwake = kMaxPeaks;
        std::size_t maxAwake = 0;
        std::uint32_t clamps = 0;
        bool allFinite = true;
        bool windowsComplete = false;
    };

    const auto soakOnce = [](std::uint32_t seed) {
        ResonanceDriftNetwork net;
        net.setSeed(seed);
        net.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            net.setPeakQ(i, Krate::DSP::kMaxResonatorQ);
            net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
            net.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
            net.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
            net.setPeakPanWander(i, 1.0f);
        }
        net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
        net.setMix(1.0f);
        net.setWetGain(kSoakWetGainDb);

        PinkDrive driveL(seed ^ 0x50A70001u, kFs, -12.0f);
        PinkDrive driveR(seed ^ 0x50A70002u, kFs, -12.0f);
        WakePattern pattern(net, seed ^ 0xA5A5A5A5u, kFs);

        std::array<float, kChunk> inL{};
        std::array<float, kChunk> inR{};
        std::array<float, kChunk> outL{};
        std::array<float, kChunk> outR{};

        std::vector<float> windowDb;
        windowDb.reserve(kSoakWindows);
        double sumSq = 0.0;
        std::size_t counted = 0;
        SoakStats s;

        for (std::size_t c = 0; c < kSoakChunks; ++c) {
            static_cast<void>(pattern.advance(net, c * kChunk));
            const std::size_t awake = pattern.getAwakeCount();
            s.minAwake = std::min(s.minAwake, awake);
            s.maxAwake = std::max(s.maxAwake, awake);
            for (std::size_t i = 0; i < kChunk; ++i) {
                inL[i] = driveL.next();
                inR[i] = driveR.next();
            }
            net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
            for (std::size_t i = 0; i < kChunk; ++i) {
                // Scanned WITHOUT a per-sample REQUIRE: 8.6e7 samples per
                // channel per seed would otherwise register billions of
                // assertions.
                if (!Krate::DSP::detail::isFinite(outL[i])
                    || !Krate::DSP::detail::isFinite(outR[i])) {
                    s.allFinite = false;
                }
                s.worstPeak = std::max({s.worstPeak, std::abs(outL[i]), std::abs(outR[i])});
                sumSq += static_cast<double>(outL[i]) * static_cast<double>(outL[i])
                         + static_cast<double>(outR[i]) * static_cast<double>(outR[i]);
                counted += 2u;
            }
            if (((c + 1u) % kSoakWindowChunks) == 0u) {
                windowDb.push_back(rmsDbfsOf(sumSq, counted));
                sumSq = 0.0;
                counted = 0;
            }
        }

        s.events = pattern.getEventCount();
        s.toggled = pattern.getToggledPeakCount();
        s.clamps = net.getClampEngagementCount();
        s.windowsComplete = windowDb.size() == kSoakWindows;
        if (!s.windowsComplete) {
            return s;
        }

        s.medianDb = static_cast<double>(percentileOf(windowDb, 0.5));
        for (const float db : windowDb) {
            s.worstDeviationDb =
                std::max(s.worstDeviationDb, std::abs(static_cast<double>(db) - s.medianDb));
            s.lowestDb = std::min(s.lowestDb, static_cast<double>(db));
            s.highestDb = std::max(s.highestDb, static_cast<double>(db));
        }
        s.totalChangeDb = totalLeastSquaresChange(windowDb);
        return s;
    };

    // The configuration is asserted, not assumed: a clamped setter would
    // otherwise quietly soak a milder patch than the criterion names. Checked
    // once, on an instance configured exactly as soakOnce configures its own.
    {
        ResonanceDriftNetwork probe;
        probe.setSeed(kSoakSeeds[0]);
        probe.prepare(kFs, ResonanceDriftNetwork::PrepareConfig{});
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            probe.setPeakQ(i, Krate::DSP::kMaxResonatorQ);
            probe.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
            probe.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
            probe.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
            probe.setPeakPanWander(i, 1.0f);
        }
        probe.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
        REQUIRE(probe.getWanderRate() == Catch::Approx(ResonanceDriftNetwork::kMaxWanderRateHz));
        REQUIRE(probe.getLaneDecimation() == 1u);
        REQUIRE(probe.getNumPeaks() == kMaxPeaks);  // FR-019's N is held at 12
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            CAPTURE(i);
            REQUIRE(probe.getPeakQ(i) == Catch::Approx(Krate::DSP::kMaxResonatorQ));
            REQUIRE(probe.getFreqWander(i)
                    == Catch::Approx(ResonanceDriftNetwork::kMaxFreqWanderSemis));
            REQUIRE(probe.getQWander(i)
                    == Catch::Approx(ResonanceDriftNetwork::kMaxQWanderOctaves));
            REQUIRE(probe.getGainWander(i)
                    == Catch::Approx(ResonanceDriftNetwork::kMaxGainWanderDb));
        }
    }

    std::array<SoakStats, kSoakSeeds.size()> stats{};
    for (std::size_t s = 0; s < kSoakSeeds.size(); ++s) {
        stats[s] = soakOnce(kSoakSeeds[s]);
    }

    double worstDeviationDb = 0.0;
    double lowestDb = 300.0;
    double highestDb = -300.0;
    double slopeSum = 0.0;
    double worstSeedSlopeDb = 0.0;
    float worstPeak = 0.0f;
    std::size_t fewestEvents = 1000000u;
    std::size_t fewestToggled = 1000000u;
    std::size_t narrowestAwake = 1000000u;
    std::size_t widestAwake = 0;
    std::uint32_t worstClamp = 0;
    bool allFinite = true;
    bool allWindowsComplete = true;
    for (const SoakStats& s : stats) {
        worstDeviationDb = std::max(worstDeviationDb, s.worstDeviationDb);
        lowestDb = std::min(lowestDb, s.lowestDb);
        highestDb = std::max(highestDb, s.highestDb);
        slopeSum += s.totalChangeDb;
        worstSeedSlopeDb = std::max(worstSeedSlopeDb, std::abs(s.totalChangeDb));
        worstPeak = std::max(worstPeak, s.worstPeak);
        fewestEvents = std::min(fewestEvents, s.events);
        fewestToggled = std::min(fewestToggled, s.toggled);
        narrowestAwake = std::min(narrowestAwake, s.minAwake);
        widestAwake = std::max(widestAwake, s.maxAwake);
        worstClamp = std::max(worstClamp, s.clamps);
        allFinite = allFinite && s.allFinite;
        allWindowsComplete = allWindowsComplete && s.windowsComplete;
    }
    const double meanSlopeDb = slopeSum / static_cast<double>(stats.size());

    CAPTURE(worstDeviationDb, lowestDb, highestDb, meanSlopeDb, worstSeedSlopeDb, worstPeak,
            fewestEvents, fewestToggled, narrowestAwake, widestAwake, worstClamp);
    REQUIRE(allWindowsComplete);

    // NON-VACUITY: the wake pattern has to have fired on every seed. 1800 s over
    // a 20-90 s range is ~32 events in expectation; 15 is a floor no seed
    // misses. And the awake count has to have stayed stationary - if it ever
    // leaves {5, 6} the script is back on a walk with a trend of its own, and
    // (b) below would be measuring that instead of the network (see WakePattern).
    REQUIRE(fewestEvents >= 15u);
    REQUIRE(fewestToggled >= 6u);
    REQUIRE(narrowestAwake >= 5u);
    REQUIRE(widestAwake <= 6u);

    // ---- (d) zero non-finite samples, zero clamp engagements ---------------
    REQUIRE(allFinite);
    REQUIRE(worstClamp == 0u);
    REQUIRE(worstPeak < ResonanceDriftNetwork::kOutputClamp);

    // ---- (a) every 10 s window within +/- kSoakWindowDb of its own render's
    //          median, on every one of the eight seeds ------------------------
    REQUIRE(worstDeviationDb <= static_cast<double>(kSoakWindowDb));

    // ---- (b) no creep in either direction, on the eight-seed mean -----------
    // The per-seed extreme is CAPTUREd above and deliberately not asserted: it
    // is the noisy statistic this arm replaced, kept visible so a reader can
    // see the spread the mean is averaging over.
    REQUIRE(std::abs(meanSlopeDb) <= static_cast<double>(kSoakSlopeDbPerRender));

    // ---- (c) it neither died nor ran away -----------------------------------
    REQUIRE(lowestDb >= static_cast<double>(kSoakFloorDbfs));
    REQUIRE(highestDb <= static_cast<double>(kSoakCeilingDbfs));
}
