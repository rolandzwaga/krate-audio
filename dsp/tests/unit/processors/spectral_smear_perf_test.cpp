// ==============================================================================
// Layer 2: Processor Tests - SpectralSmear CPU budget (SC-013 (a)-(d))
// ==============================================================================
// Spec:  specs/vorago-phase4-spectral-smear/spec.md   (SC-013, FR-060)
// Plan:  specs/vorago-phase4-spectral-smear/plan.md
// Tasks: specs/vorago-phase4-spectral-smear/tasks.md  (T013)
//
// TWO NUMBERS, AND THEY MUST NOT BE CONFUSED.
//   The roadmap budget is 0.5 % of one core at 48 kHz:
//       kReferenceNs = 10 666 667 x 0.005 = 53 333 ns per 512-sample block.
//   But SC-013 inherits the Phase-1 gate idiom, whose
//       static_assert(kBaseline * kRegressionFactor <= kReferenceNs)
//   with kRegressionFactor = 1.5 (vorago_p1_perf_test.cpp:108-110) caps ANY
//   checked-in baseline at
//       53 333 / 1.5 = 35 555 ns/block = 0.333 % of one core.
//   *** 35 555 ns IS THE BINDING FIGURE FOR THIS PHASE *** and the figure each
//   compliance row is measured against. A measurement landing in
//   [35 556, 53 333] ns is NOT SHIPPABLE even though it satisfies the roadmap
//   sentence: no baseline can be encoded for it, the TU stops compiling, and
//   FR-060's lever ladder applies (see THE LEVERS, below).
//
// WHY THE TAG IS "[.perf]" AND NOTHING ELSE:
//   The leading dot hides this case from the default run
//   (vorago_p1_perf_test.cpp:11-13, life_modulators_perf_test.cpp:147). The
//   per-push CI filter is ~[performance]~[perf]~[benchmark]~[!benchmark]~[long]
//   (.github/workflows/ci.yml:366,638,1063) and Catch2 tag exclusion is
//   EXACT-MATCH, so an invented tag would enrol four hard wall-clock REQUIREs in
//   the per-push lane on all three shared-runner OS legs. SC-013 is a
//   DEVELOPER-RUN gate; the part CI enforces continuously is the compiled
//   static_asserts below.
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. The measurement basis
//   is therefore NANOSECONDS PER 512-SAMPLE BLOCK, gated against a checked-in
//   baseline as a relative regression bound (fail if > baseline x 1.5). The two
//   static_asserts pin that gate underneath SC-013's absolute reference so the
//   bound can never become self-referential, and above an anti-no-op floor so a
//   configuration that accidentally measures a bypass cannot pass vacuously.
//
// THE PROJECTION THIS IS MEASURED AGAINST (FR-060, spec.md:595-609):
//   Atmosphere's structurally identical stereo blur stage (STFT<->OverlapAdd,
//   fftSize 1024, 75 % overlap, full per-bin phase randomisation) costs
//   ~23 000 ns/block on the reference machine
//   (atmosphere_engine_perf_test.cpp:376), corroborated by its own (b) - (a)
//   baseline difference of ~25 500 ns (:241-242, 111 815 - 86 305). This
//   component adds, per bin, one FMA for the integrator (FR-020), one FMA for
//   FR-021's blend, one ordered compare (FR-024) and one shared per-frame table
//   lerp (FR-034) => ~26 667-32 000 ns/block, 10-33 % under the ceiling.
//
// NO ALLOCATION-TRACKING INCLUDES HERE:
//   brownian_drift_test.cpp:27-28 is the single owner of the global operator
//   new/delete replacements for the dsp_processors_tests binary; a second
//   include is a duplicate-symbol link error (documented at
//   life_modulators_perf_test.cpp:19-23 and vorago_p1_perf_test.cpp:44-48).
//   Allocation freedom is covered by SpectralSmear_NoAllocation in the main TU.
//
// No std::isnan/isinf/isfinite anywhere: tools/lint-nonfinite-symbols.js bans
// them and -ffast-math folds them on the macOS leg. Finiteness goes through
// Krate::DSP::detail::isFinite (core/db_utils.h:118 float, :126 double).
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_smear.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace Krate::DSP;

namespace {

constexpr double      kSr48      = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// FR-060's absolute reference: 0.5 % of that budget (~53 333 ns/block).
constexpr double kReferenceNs = kBlockBudgetNs * 0.005;

/// Relative regression bound applied to every checked-in baseline (SC-013).
constexpr double kRegressionFactor = 1.5;

/// The BINDING ceiling: the largest baseline the headroom static_assert admits.
/// 53 333.33 / 1.5 = 35 555.55 ns/block = 0.333 % of one core.
constexpr double kEffectiveCeilingNs = kReferenceNs / kRegressionFactor;

/// The anti-no-op floor: kReferenceNs / 50 = 1 066.67 ns/block. A configuration
/// whose baseline sits under this is measuring a bypass or a dead-coded loop,
/// not the component (spec.md:984-999, the SC-013 (a) correction C-13).
constexpr double kAntiNoOpFloorNs = kReferenceNs / 50.0;

// -----------------------------------------------------------------------------
// BASELINE PROVENANCE - these four constants ARE MEASUREMENTS.
// -----------------------------------------------------------------------------
// Machine   : 13th Gen Intel Core i9-13900HX, Windows 11, MSVC Release,
//             build/windows-x64-release (the reference machine Phases 1-3 used,
//             spec.md:1387).
// Method    : five consecutive runs of this case ALONE - nothing else
//             executing, no build, no other suite, no parallel agent - pinned to
//             the P-cores with `start /affinity FFFF`, 20 s of settle between
//             runs. THE AFFINITY MASK IS PART OF THE METHOD, not decoration: an
//             unpinned run that lands on an E-core measures ~2.3x these figures
//             (35 648 / 45 398 / 44 453 ns for (a)/(b)/(c) in one such run),
//             which is a measurement of the scheduler, not of the code.
// Rule      : each baseline is the WORST (largest) of its five runs, rounded up.
//             A baseline is a MEASUREMENT, not an allowance
//             (atmosphere_engine_perf_test.cpp:155-156).
//
//   config                       five runs (ns/block)                    worst    spread
//   (a) transparent   15179.2  14871.4  14794.8  15534.6  16791.2       16791.2   13.5 %
//   (b) worst case    18003.4  17805.2  18064.0  18428.2  18939.4       18939.4    6.4 %
//   (c) fft 512       18468.8  18438.6  17628.2  17728.6  18435.2       18468.8    4.8 %
//   (d) rebuild 4096  32496.8  33101.8  32249.6  32977.8  33046.6       33101.8    2.6 %
//
// All four sit under the BINDING 35 555 ns effective ceiling (0.333 % of one
// core). (d) has the least room - 33 500 is 94 % of the ceiling - so any future
// work that adds cost at fftSize 4096 will surface there first, as a compile
// error from the headroom static_assert rather than as a quiet regression.
//
// WHAT (d) COST BEFORE THE FR-060 LEVER WAS SPENT, recorded because the lever is
// the whole reason this row now fits: with the per-bin scalar rebuild
// (one log + three pow + three exp per bin, ~14 300 transcendentals at
// numBins = 2049) configuration (d) measured 108 326 ns/block - 2.1x its own
// gate and 3.0x the effective ceiling. FR-060's ladder says reduce cost, never
// raise the baseline, so rebuildPoleTables() was rewritten to carry its
// transcendentals in the bulk SIMD primitives (core/spectral_simd.h's
// batchLog10 / batchPow10) instead of one scalar call per bin; the law and the
// tables are unchanged to within one float ULP (max |pole difference| 2.98e-07,
// measured over all 2049 bins), and the rebuild itself fell from 92 065 ns to
// ~12 000. The lever ladder's other rungs were NOT needed and were NOT spent:
// no default geometry changed, no rebuild cadence coarsened, no workload shrunk.
//
// IF A FUTURE MEASUREMENT EXCEEDS 35 555 ns/block, the ladder (tasks.md T013, in
// order) is: (i) the two identity-gate skips; (ii) fuse the integrator and the
// blend into one pass over the bins reading the shared per-frame pole table;
// (iii) raise the default fftSize; for (d) specifically, a coarser rebuild
// cadence - which belongs to the user, not to this file. IF NONE SUFFICES, STOP
// AND SURFACE TO THE USER WITH THE MEASURED TABLE. Never raise a baseline, never
// relax the budget, never shrink the workload. A replacement that has to go UP
// past the ceiling does not "break the build" accidentally: the headroom
// static_assert refusing to compile IS that clause working
// (atmosphere_engine_perf_test.cpp:168-180 records the same episode).
// -----------------------------------------------------------------------------

/// (a) The transparent cost: default geometry, ENABLED, identity gates engaged.
/// 16 791 ns worst of five = 0.157 % of one core; ceiling 35 555 ns (0.333 %).
constexpr double kBaselineTransparentNs = 17000.0;

/// (b) The worst case at the reference geometry: smear 1, decoherence 1, tilt
/// written every block (the modulation target, so the per-frame table resolve
/// runs on every frame instead of hitting its equality guard).
/// 18 939 ns worst of five = 0.178 % of one core; ceiling 35 555 ns (0.333 %).
constexpr double kBaselineWorstCaseNs = 19000.0;

/// (c) The highest frame-rate geometry: fftSize 512 => hop 128 => four
/// frame-pairs per 512-sample block, same worst-case controls.
/// 18 469 ns worst of five = 0.173 % of one core; ceiling 35 555 ns (0.333 %).
constexpr double kBaselineSmallFftNs = 18500.0;

/// (d) The rebuild: fftSize 4096, worst-case controls, plus one
/// setSmearTimeLow() per block so FR-036's deferred pole-table rebuild fires
/// once per block at the geometry where it is most expensive.
/// 33 102 ns worst of five = 0.310 % of one core; ceiling 35 555 ns (0.333 %).
constexpr double kBaselineRebuildNs = 33500.0;

// Headroom clause: without this the gate is self-referential - any measured
// cost, however large, could be enshrined as "the baseline" and the row would
// pass forever (life_modulators_perf_test.cpp:60-66).
static_assert(kBaselineTransparentNs * kRegressionFactor <= kReferenceNs,
              "SC-013 (a): baseline x factor must stay below the 53 333 ns reference");
static_assert(kBaselineWorstCaseNs * kRegressionFactor <= kReferenceNs,
              "SC-013 (b): baseline x factor must stay below the 53 333 ns reference");
static_assert(kBaselineSmallFftNs * kRegressionFactor <= kReferenceNs,
              "SC-013 (c): baseline x factor must stay below the 53 333 ns reference");
static_assert(kBaselineRebuildNs * kRegressionFactor <= kReferenceNs,
              "SC-013 (d): baseline x factor must stay below the 53 333 ns reference");

// Anti-no-op floor: a baseline under kReferenceNs / 50 means the configuration
// measured a bypass, not the component. This is the clause that would have
// caught the literal reading of "(a) defaults" - PrepareConfig::enabled defaults
// to false (spec.md:984-999, plan S17 C-13).
static_assert(kBaselineTransparentNs >= kAntiNoOpFloorNs,
              "SC-013 (a): baseline below the anti-no-op floor - this measured a bypass");
static_assert(kBaselineWorstCaseNs >= kAntiNoOpFloorNs,
              "SC-013 (b): baseline below the anti-no-op floor - this measured a bypass");
static_assert(kBaselineSmallFftNs >= kAntiNoOpFloorNs,
              "SC-013 (c): baseline below the anti-no-op floor - this measured a bypass");
static_assert(kBaselineRebuildNs >= kAntiNoOpFloorNs,
              "SC-013 (d): baseline below the anti-no-op floor - this measured a bypass");

// The geometry each configuration is asserted to have actually prepared at, so a
// silently-clamped or bit_floor'd fftSize cannot turn one row into a duplicate
// of another (FR-011 clamps to [512, 4096] then bit_floors).
static_assert(SpectralSmear::kDefaultFftSize == 2048,
              "SC-013 (a)/(b) measure the 2048-point reference geometry");
static_assert(SpectralSmear::kMinFftSize == 512,
              "SC-013 (c) measures the highest legal frame rate, i.e. kMinFftSize");
static_assert(SpectralSmear::kMaxFftSize == 4096,
              "SC-013 (d) measures the rebuild at the most expensive legal geometry");
static_assert(SpectralSmear::kOverlapFactor == 4,
              "the frame-count arithmetic in this TU assumes 75 % overlap");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants.
//
// MANY SHORT TRIALS (25 x 500 blocks), copied deliberately from
// atmosphere_engine_perf_test.cpp:681-683, and the reason is the dev machine's
// CPU: a 13th Gen Intel Core i9 is a HYBRID part. The dominant noise source is
// not scheduling jitter smeared across a trial, it is the whole trial being
// migrated onto an E-core, which is a ~20 % step in ns/block that best-of-N
// cannot reject when N is small and each trial is long enough to be migrated.
//
// 400 warm-up blocks is 4.27 s of audio: far past the 50 ms control smoothing
// (FR-035), so the smoothers have SNAPPED to target (smoother.h:197-201) and the
// applied reads below are exact, and far past the longest warm-up the component
// itself imposes (FR-014's fftSize-sample FIFO pre-roll: 4096 samples = 8 blocks
// at the largest geometry).
constexpr int kTrials         = 25;
constexpr int kWarmupBlocks   = 400;
constexpr int kBlocksPerTrial = 500;

// =============================================================================
// Buffers and excitation
// =============================================================================

/// One stereo block. SpectralSmear::processBlock renders IN PLACE
/// (spectral_smear.h:341), so the excitation is kept in a separate pair of
/// arrays and copied into the working pair at the top of every block.
///
/// THE COPY IS INSIDE THE TIMED REGION AND THAT IS DELIBERATE. Re-feeding the
/// component its own output would close a unity-gain recirculation loop whose
/// spectrum drifts over 12 900 blocks - and with it the workload, because
/// FR-024's denormal floor is an ordered compare whose branch outcome depends on
/// the magnitudes present. A constant 2 x 512-float copy (~150-250 ns against a
/// block cost in the tens of microseconds, i.e. under 1 % of the figure, and
/// IDENTICAL across all four configurations so it cannot distort the comparison)
/// buys a stationary input instead.
struct Stereo {
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
};

/// Deterministic decorrelated stereo noise, peak 0.25 (~-17 dBFS RMS), built
/// once and replayed every block.
///
/// Sustained broadband excitation, not an impulse: every bin must carry real
/// energy on every frame, otherwise large stretches of the magnitude state sit
/// at the FR-024 denormal floor and take the cheap side of the compare - which
/// is precisely the cost this case is trying to measure. Xorshift32 rather than
/// <random> so the sequence is identical on every toolchain
/// (std::uniform_real_distribution is not portable), copying
/// atmosphere_engine_perf_test.cpp:707-721.
void fillExcitation(Stereo& buf) noexcept {
    std::uint32_t state = 0x9E3779B9u;

    const auto next = [&state]() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // [-0.25, 0.25): 24 mantissa bits scaled, no division by a magic float.
        return (static_cast<float>(state >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.5f;
    };

    for (std::size_t i = 0; i < kBlockSize; ++i) {
        buf.left[i]  = next();
        buf.right[i] = next();
    }
}

/// Tilt values written one per block by configurations (b)-(d): a triangle
/// sweeping the full legal range [-1, +1] and back over 128 blocks.
///
/// A SWEEP, NOT A HELD VALUE, and the difference is the whole point of this arm.
/// resolveTiltScratch() early-returns on `tilt == lastResolvedTilt_`
/// (spectral_smear.h:652-653) - exact float equality, which a settled smoother
/// reaches by snapping - so a held tilt costs ONE COMPARE per frame instead of a
/// full numBins pass. Writing a moving target every block is what keeps the
/// per-frame table resolve inside the measured region, which is what "worst
/// case" means here.
constexpr int kTiltPeriodBlocks = 128;

[[nodiscard]] std::array<float, static_cast<std::size_t>(kTiltPeriodBlocks)>
makeTiltSequence() noexcept {
    std::array<float, static_cast<std::size_t>(kTiltPeriodBlocks)> seq{};
    for (int i = 0; i < kTiltPeriodBlocks; ++i) {
        const float phase = static_cast<float>(i) / static_cast<float>(kTiltPeriodBlocks);
        seq[static_cast<std::size_t>(i)] =
            (phase < 0.5f) ? (-1.0f + 4.0f * phase) : (3.0f - 4.0f * phase);
    }
    return seq;
}

/// The two endpoint values configuration (d) alternates between, both well
/// inside [kMinSmearSeconds, kMaxSmearSeconds] so no clamp intervenes. The
/// alternation is belt-and-braces: setSmearTimeLow() raises poleTablesDirty_
/// UNCONDITIONALLY (spectral_smear.h:402-406), so even a repeated identical
/// value would fire the rebuild - but a changing value also proves, through
/// getSmearTimeLow(), that the setter really ran.
///
/// NEITHER MAY EQUAL kDefaultSmearTimeLow (3.0 s), and that is not a stylistic
/// choice: the (d)-specific guard below reads getSmearTimeLow() back as the sole
/// observable trace of a setter that fired on every block, and if the last value
/// written were the default the guard would pass identically on a build where
/// the per-block setter never ran at all. The static_asserts make that a
/// compile-time obligation rather than a comment.
constexpr float kRebuildTauA = 2.0f;
constexpr float kRebuildTauB = 5.0f;

static_assert(kRebuildTauA != SpectralSmear::kDefaultSmearTimeLow,
              "SC-013 (d): a rebuild tau equal to the default makes its guard vacuous");
static_assert(kRebuildTauB != SpectralSmear::kDefaultSmearTimeLow,
              "SC-013 (d): a rebuild tau equal to the default makes its guard vacuous");
static_assert(kRebuildTauA >= SpectralSmear::kMinSmearSeconds
                  && kRebuildTauA <= SpectralSmear::kMaxSmearSeconds,
              "SC-013 (d): a clamped rebuild tau would silently stop alternating");
static_assert(kRebuildTauB >= SpectralSmear::kMinSmearSeconds
                  && kRebuildTauB <= SpectralSmear::kMaxSmearSeconds,
              "SC-013 (d): a clamped rebuild tau would silently stop alternating");

// =============================================================================
// Configurations
// =============================================================================

struct ConfigSpec {
    const char* label             = "";
    std::size_t fftSize           = SpectralSmear::kDefaultFftSize;
    float       smearAmount       = 0.0f;
    float       decoherence       = 0.0f;
    bool        modulateTilt      = false;
    bool        rebuildEveryBlock = false;
    double      baselineNs        = 0.0;
};

/// (a) THE TRANSPARENT COST. Default geometry, `.enabled = true`, and default
/// CONTROL values - smearAmount 0, decoherence 0, tilt 0 - so both identity
/// gates (FR-021, FR-041) are engaged while the full stereo STFT round trip is
/// still paid.
///
/// `.enabled = true` IS SPELT OUT HERE ON PURPOSE (spec correction C-13,
/// plan S17): PrepareConfig::enabled DEFAULTS TO false (spectral_smear.h:149),
/// so a literal reading of "(a) defaults" would prepare a TRUE BYPASS whose
/// processBlock returns at the first guard (spectral_smear.h:344-346), measure
/// tens of nanoseconds, and collide head-on with the anti-no-op floor above.
/// D-11 states the intent: an ENABLED instance at default control values.
constexpr ConfigSpec kConfigTransparent{
    .label             = "(a) transparent: fft 2048, enabled, smear 0 / decoh 0 / tilt 0",
    .fftSize           = SpectralSmear::kDefaultFftSize,
    .smearAmount       = 0.0f,
    .decoherence       = 0.0f,
    .modulateTilt      = false,
    .rebuildEveryBlock = false,
    .baselineNs        = kBaselineTransparentNs};

/// (b) THE WORST CASE at the reference geometry. smearAmount 1, decoherence 1,
/// tilt written every block. THE TIME ENDPOINTS ARE NOT SWEPT HERE: tilt is the
/// modulation target (FR-033), the endpoints are patch-cadence controls, and
/// they get configuration (d) to themselves.
constexpr ConfigSpec kConfigWorstCase{
    .label             = "(b) worst case: fft 2048, smear 1 / decoh 1 / tilt swept per block",
    .fftSize           = SpectralSmear::kDefaultFftSize,
    .smearAmount       = 1.0f,
    .decoherence       = 1.0f,
    .modulateTilt      = true,
    .rebuildEveryBlock = false,
    .baselineNs        = kBaselineWorstCaseNs};

/// (c) THE HIGHEST FRAME-RATE GEOMETRY. fftSize 512 => hop 128 => four
/// frame-pairs per 512-sample block, four times (b)'s frame rate against a
/// quarter of its bins. FR-060 predicts these very nearly cancel (the per-block
/// products go as 2048 log N and 1024); this row is what tests that prediction
/// rather than asserting it.
constexpr ConfigSpec kConfigSmallFft{
    .label             = "(c) fastest frames: fft 512, smear 1 / decoh 1 / tilt swept per block",
    .fftSize           = SpectralSmear::kMinFftSize,
    .smearAmount       = 1.0f,
    .decoherence       = 1.0f,
    .modulateTilt      = true,
    .rebuildEveryBlock = false,
    .baselineNs        = kBaselineSmallFftNs};

/// (d) THE REBUILD. fftSize 4096, worst-case controls, plus one
/// setSmearTimeLow() per block, so FR-036's deferred pole-table rebuild fires
/// once per block at numBins = 2049 - about 14 300 transcendentals
/// (spectral_smear.h:589-591). This is the one operation in the component with a
/// large, geometry-scaled SYNCHRONOUS cost that FR-006 puts on the audio thread;
/// without this row it is absent from every criterion in the phase.
constexpr ConfigSpec kConfigRebuild{
    .label             = "(d) rebuild: fft 4096, smear 1 / decoh 1, setSmearTimeLow every block",
    .fftSize           = SpectralSmear::kMaxFftSize,
    .smearAmount       = 1.0f,
    .decoherence       = 1.0f,
    .modulateTilt      = true,
    .rebuildEveryBlock = true,
    .baselineNs        = kBaselineRebuildNs};

// =============================================================================
// Measurement
// =============================================================================

struct Measurement {
    double      nsPerBlock         = 0.0;
    double      sink               = 0.0;
    bool        prepared           = false;
    bool        enabled            = false;
    std::size_t fftSize            = 0;
    std::size_t hopSize            = 0;
    std::size_t numBins            = 0;
    std::size_t latencySamples     = 0;
    std::size_t allocatedBytes     = 0;
    float       appliedSmear       = 0.0f;
    float       appliedDecoherence = 0.0f;
    float       smearTimeLow       = 0.0f;
    float       roundTripDeviation = 0.0f;
};

/// Best-of-25 ns/block for one configuration.
///
/// ONE PREPARED INSTANCE FOR THE WHOLE MEASUREMENT, warmed up once and then
/// reused across all 25 trials (atmosphere_engine_perf_test.cpp:900-905 does the
/// same). Re-preparing per trial would reset FR-014's fftSize-sample FIFO
/// pre-roll, so the opening blocks of every trial would run the cheaper warm-up
/// pop path and the figure would be biased low - by 1.6 % at fftSize 4096 (8 of
/// 500 blocks) and more if the trial shape ever shortens.
[[nodiscard]] Measurement measure(const ConfigSpec& cfg) {
    Stereo source{};
    fillExcitation(source);
    Stereo work{};

    const auto tiltSequence = makeTiltSequence();

    SpectralSmear smear;
    smear.prepare(kSr48, SpectralSmear::PrepareConfig{.fftSize = cfg.fftSize, .enabled = true});
    smear.setSeed(SpectralSmear::kDefaultSeed);

    // The endpoints are pinned EXPLICITLY rather than left implicit, so this
    // configuration is reproducible from this file alone if a default ever moves
    // (atmosphere_engine_perf_test.cpp:784-785 states the same rule).
    smear.setSmearTimeLow(SpectralSmear::kDefaultSmearTimeLow);
    smear.setSmearTimeHigh(SpectralSmear::kDefaultSmearTimeHigh);
    smear.setSmearAmount(cfg.smearAmount);
    smear.setDecoherence(cfg.decoherence);
    smear.setSmearTilt(0.0f);

    double sink       = 0.0;
    int    blockIndex = 0;

    // The reads at the bottom exist for the reason stated at
    // life_modulators_perf_test.cpp:124-130: without a consumer of the rendered
    // samples the optimizer may dead-code the render away and the row measures
    // nothing. Two array reads per block is not artificial overhead.
    const auto renderBlock = [&]() noexcept {
        std::copy_n(source.left.data(), kBlockSize, work.left.data());
        std::copy_n(source.right.data(), kBlockSize, work.right.data());

        if (cfg.modulateTilt) {
            smear.setSmearTilt(
                tiltSequence[static_cast<std::size_t>(blockIndex % kTiltPeriodBlocks)]);
        }
        if (cfg.rebuildEveryBlock) {
            smear.setSmearTimeLow((blockIndex % 2 == 0) ? kRebuildTauA : kRebuildTauB);
        }

        smear.processBlock(work.left.data(), work.right.data(), kBlockSize);

        sink += static_cast<double>(work.left[0])
                + static_cast<double>(work.right[kBlockSize - 1]);
        ++blockIndex;
    };

    for (int i = 0; i < kWarmupBlocks; ++i) {
        renderBlock();
    }

    double bestNsPerBlock = std::numeric_limits<double>::max();

    for (int trial = 0; trial < kTrials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocksPerTrial; ++i) {
            renderBlock();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        bestNsPerBlock =
            std::min(bestNsPerBlock, elapsedNs / static_cast<double>(kBlocksPerTrial));
    }

    // One more block OUTSIDE the timed region, purely to prove a real round trip
    // ran: a bypassed processBlock leaves the buffer bit-identical to the copied
    // excitation (FR-019, SC-002 (d)), so a strictly positive deviation is proof
    // that the entry guard did not fire.
    renderBlock();

    float deviation = 0.0f;
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        deviation = std::max(deviation, std::fabs(work.left[i] - source.left[i]));
        deviation = std::max(deviation, std::fabs(work.right[i] - source.right[i]));
    }

    Measurement out{};
    out.nsPerBlock         = bestNsPerBlock;
    out.sink               = sink;
    out.prepared           = smear.isPrepared();
    out.enabled            = smear.isEnabled();
    out.fftSize            = smear.getFftSize();
    out.hopSize            = smear.getHopSize();
    out.numBins            = smear.getNumBins();
    out.latencySamples     = smear.getLatencySamples();
    out.allocatedBytes     = smear.getAllocatedBytes();
    out.appliedSmear       = smear.getAppliedSmearAmount();
    out.appliedDecoherence = smear.getAppliedDecoherence();
    out.smearTimeLow       = smear.getSmearTimeLow();
    out.roundTripDeviation = deviation;
    return out;
}

/// Structural guards shared by all four rows. These are ANTI-VACUITY checks, not
/// decoration: each one names a way a configuration could silently degenerate
/// into a cheaper measurement than the one its label claims.
void requireConfigurationTookEffect(const Measurement& m, const ConfigSpec& cfg) {
    // The instance really is rendering: an unprepared or disabled one returns at
    // processBlock's first guard and the whole row measures the copy loop.
    REQUIRE(m.prepared);
    REQUIRE(m.enabled);
    REQUIRE(m.allocatedBytes > 0);

    // getLatencySamples() is fftSize only when prepared AND enabled
    // (spectral_smear.h:472-478), so this is a second, independent witness that
    // the bypass path was not taken.
    REQUIRE(m.latencySamples == cfg.fftSize);

    // The geometry is the one the row is named for. FR-011 clamps to
    // [512, 4096] then bit_floors, so a mis-stated fftSize would silently turn
    // one row into a duplicate of another rather than fail.
    REQUIRE(m.fftSize == cfg.fftSize);
    REQUIRE(m.hopSize == cfg.fftSize / SpectralSmear::kOverlapFactor);
    REQUIRE(m.numBins == cfg.fftSize / 2 + 1);

    // The controls actually settled where the row claims. 400 warm-up blocks is
    // ~85x the 50 ms smoothing constant, so a settled smoother has snapped and
    // exact equality is the right test (smoother.h:197-201).
    REQUIRE(m.appliedSmear == cfg.smearAmount);
    REQUIRE(m.appliedDecoherence == cfg.decoherence);

    // A real STFT round trip ran: under a true bypass the buffer would still be
    // a bit-identical copy of the excitation.
    REQUIRE(m.roundTripDeviation > 0.0f);

    // Nothing was dead-coded away, and the render stayed finite.
    REQUIRE(detail::isFinite(m.sink));
}

/// Total blocks rendered per configuration, warm-up plus every trial. Used only
/// to predict which of (d)'s two alternating endpoint values was written last.
constexpr int kTotalRenderedBlocks = kWarmupBlocks + kTrials * kBlocksPerTrial;

}  // namespace

// =============================================================================
// SC-013: SpectralSmear CPU budget, four configurations, four baselines
// =============================================================================

TEST_CASE("SpectralSmear_CpuBudget", "[spectral_smear][.perf]") {
    const Measurement a = measure(kConfigTransparent);
    const Measurement b = measure(kConfigWorstCase);
    const Measurement c = measure(kConfigSmallFft);
    const Measurement d = measure(kConfigRebuild);

    requireConfigurationTookEffect(a, kConfigTransparent);
    requireConfigurationTookEffect(b, kConfigWorstCase);
    requireConfigurationTookEffect(c, kConfigSmallFft);
    requireConfigurationTookEffect(d, kConfigRebuild);

    // (d)-specific: the per-block endpoint setter really did run, so the
    // deferred rebuild really was marked dirty on every block. setSmearTimeLow()
    // raises the flag unconditionally and processBlock consumes it at the top
    // (spectral_smear.h:348-352), so the last value written is the observable
    // trace of a setter that fired on every one of the rendered blocks. The
    // measurement pass renders kTotalRenderedBlocks blocks and then one more for
    // the round-trip check, so the LAST index written is kTotalRenderedBlocks.
    REQUIRE(d.smearTimeLow == ((kTotalRenderedBlocks % 2 == 0) ? kRebuildTauA : kRebuildTauB));

    // ...and the other three rows did NOT pay it, so (d) is a distinct
    // measurement rather than a fourth copy of the worst case.
    REQUIRE(a.smearTimeLow == SpectralSmear::kDefaultSmearTimeLow);
    REQUIRE(b.smearTimeLow == SpectralSmear::kDefaultSmearTimeLow);
    REQUIRE(c.smearTimeLow == SpectralSmear::kDefaultSmearTimeLow);

    const auto percentOfCore = [](double ns) { return (ns / kBlockBudgetNs) * 100.0; };

    WARN("SC-013 SpectralSmear CPU budget (stereo, 48 kHz, ns per 512-sample block, best of "
         << kTrials << " x " << kBlocksPerTrial << " blocks after " << kWarmupBlocks
         << " warm-up blocks):\n"
         << "  " << kConfigTransparent.label << "\n"
         << "        measured " << a.nsPerBlock << " ns/block (" << percentOfCore(a.nsPerBlock)
         << " % of one core), baseline " << kBaselineTransparentNs << ", gate "
         << (kBaselineTransparentNs * kRegressionFactor) << "\n"
         << "  " << kConfigWorstCase.label << "\n"
         << "        measured " << b.nsPerBlock << " ns/block (" << percentOfCore(b.nsPerBlock)
         << " % of one core), baseline " << kBaselineWorstCaseNs << ", gate "
         << (kBaselineWorstCaseNs * kRegressionFactor) << "\n"
         << "  " << kConfigSmallFft.label << "\n"
         << "        measured " << c.nsPerBlock << " ns/block (" << percentOfCore(c.nsPerBlock)
         << " % of one core), baseline " << kBaselineSmallFftNs << ", gate "
         << (kBaselineSmallFftNs * kRegressionFactor) << "\n"
         << "  " << kConfigRebuild.label << " (numBins " << d.numBins << ")\n"
         << "        measured " << d.nsPerBlock << " ns/block (" << percentOfCore(d.nsPerBlock)
         << " % of one core), baseline " << kBaselineRebuildNs << ", gate "
         << (kBaselineRebuildNs * kRegressionFactor) << "\n"
         << "  block budget (512 @ 48 kHz)        : " << kBlockBudgetNs << " ns\n"
         << "  FR-060 reference (0.5 % of core)   : " << kReferenceNs << " ns/block\n"
         << "  BINDING effective ceiling (0.333 %): " << kEffectiveCeilingNs << " ns/block\n"
         << "  anti-no-op floor                   : " << kAntiNoOpFloorNs << " ns/block\n"
         << "  baselines: the worst of five consecutive P-core-pinned idle runs\n"
         << "  on the reference machine - provenance banner at the head of this TU.");

    // The binding assertions. A red here is NOT closed by raising a baseline:
    // spend the FR-060 levers in order, and if none suffices, stop and surface
    // the measured table to the user (tasks.md T013).
    REQUIRE(a.nsPerBlock <= kConfigTransparent.baselineNs * kRegressionFactor);
    REQUIRE(b.nsPerBlock <= kConfigWorstCase.baselineNs * kRegressionFactor);
    REQUIRE(c.nsPerBlock <= kConfigSmallFft.baselineNs * kRegressionFactor);
    REQUIRE(d.nsPerBlock <= kConfigRebuild.baselineNs * kRegressionFactor);
}
