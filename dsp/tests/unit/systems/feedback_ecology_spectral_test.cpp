// ==============================================================================
// Layer 3: System Tests - FeedbackEcology, spectral and long-render cases
// ==============================================================================
// Vorago Phase 5 (specs/vorago-phase5-feedback-ecology): FeedbackEcology
// spectral / soak cases - SC-001, SC-002, SC-003, SC-021 (plan S12.2, "the
// [long] set").
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase5-feedback-ecology/spec.md
//            specs/vorago-phase5-feedback-ecology/plan.md
//            specs/vorago-phase5-feedback-ecology/tasks.md  (T001 creates this
//                                                            TU; later tasks
//                                                            land the cases)
//
// [long] TAG: the cases here are multi-minute renders whose assertions are
//   toolchain-INDEPENDENT, so they carry [long] alongside [feedback_ecology].
//   Per-push CI excludes them (~[long]); they run nightly on all three OSes and
//   locally by default. (T017's helper smoke case is the one exception: it is a
//   4-second measurement of the test instrument itself, not a soak, so it stays
//   in the per-push lane and carries [feedback_ecology] only.)
//
// STREAMING STATISTICS ARE MANDATORY: a materialised 30-minute stereo render is
//   86.4 M samples/channel (~691 MB). Accumulate peak / RMS / per-window RMS /
//   finiteness block by block and keep at most one analysis window in memory.
//
// NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()/infinity()
//   here - this TU is deliberately NOT in dsp/tests/CMakeLists.txt's
//   -fno-fast-math block (tasks.md T001). SC-012 owns bit-pattern injection and
//   lives in feedback_ecology_nonfinite_test.cpp. Finiteness checks use
//   Krate::DSP::detail::isFinite (core/db_utils.h), never std::isnan /
//   std::isinf / std::isfinite.
//
// ALLOCATION DETECTION: include <allocation_detector.h> ONLY, never
//   <allocation_operator_overrides.h>.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>        // detail::isFinite - never std::isnan/isinf
#include <krate/dsp/core/math_constants.h>  // kPi, for SC-003's 110 Hz sine
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/window_functions.h>          // Window::generateHann - SC-021's Welch stage
#include <krate/dsp/primitives/fft.h>                 // FFT, Complex - SC-021's centroid/band vector
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/resonator_bank.h>      // kMinDecayTime/kMaxDecayTime/
                                                      // kMinResonatorFrequency/
                                                      // kMaxResonatorFrequencyRatio - SC-001 (a)/(c)
#include <krate/dsp/systems/feedback_ecology.h>

#include "artifact_detection.h"  // ClickDetector/ClickDetectorConfig/ClickDetection - SC-003 (a)
#include "coherence.h"           // computeMeanCoherence - SC-002 (d), REPORTED not gated

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <sstream>
#include <utility>  // std::cmp_greater
#include <vector>

// Cases land here from T017 onward. This TU is registered in
// dsp/tests/CMakeLists.txt's dsp_systems_tests source list (the list is
// ENUMERATED, not globbed - an unregistered TU silently drops out of the build
// and its cases never run).

namespace {

// ---------------------------------------------------------------------------
// Fixture for the coherence helper's smoke case (T017). The geometry is the one
// plan S12.1 pins for the estimator: Hann, 4096-point, 50 % overlap.
// ---------------------------------------------------------------------------

constexpr double      kCohFs      = 48000.0; ///< the reference rate
constexpr std::size_t kCohFft     = 4096;    ///< plan S12.1's analysis size
constexpr std::size_t kCohHop     = 2048;    ///< kCohFft / 2 (50 % overlap)
constexpr std::size_t kCohSamples = 192000;  ///< 4 s at kCohFs -> ~92 Welch frames

/// SC-002 (d)'s reporting band.
constexpr float kCohBandLowHz  = 40.0f;
constexpr float kCohBandHighHz = 4000.0f;

/// Well inside a 2 kHz lowpass, so the filtered-copy arm measures the passband
/// rather than the skirt where the filtered copy has fallen toward the
/// numerical floor and the estimate is dominated by rounding.
constexpr float kCohPassbandHighHz = 1000.0f;

/// Deterministic white noise at the given peak amplitude.
[[nodiscard]] std::vector<float> makeCoherenceNoise(std::size_t numSamples, float amplitude,
                                                    std::uint32_t seed) {
    Krate::DSP::Xorshift32 rng(seed);
    std::vector<float>     out(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        out[i] = amplitude * rng.nextFloat();
    }
    return out;
}

// Non-finite values for the guard arm, built from BIT PATTERNS. This TU sits
// deliberately outside dsp/tests/CMakeLists.txt's -fno-fast-math block (banner
// above), so std::numeric_limits<T>::quiet_NaN()/infinity() would fold to
// finite garbage on the macOS leg. The volatile integer sink keeps the pattern
// intact and gives it no FP provenance the optimiser can reason about; the
// helper's own detail::isFinite check is itself fast-math immune
// (core/db_utils.h:118-130). No non-finite value is ever fed to a component
// here - only to the helper's argument guards, which reject it before any
// arithmetic happens.
constexpr std::uint32_t kNaNBitsF = 0x7FC00000u;
constexpr std::uint32_t kInfBitsF = 0x7F800000u;
constexpr std::uint64_t kNaNBitsD = 0x7FF8000000000000ULL;
constexpr std::uint64_t kInfBitsD = 0x7FF0000000000000ULL;

[[nodiscard]] float nonFiniteFloat(std::uint32_t bits) {
    volatile std::uint32_t opaque = bits;
    const std::uint32_t    copy   = opaque;
    return std::bit_cast<float>(copy);
}

[[nodiscard]] double nonFiniteDouble(std::uint64_t bits) {
    volatile std::uint64_t opaque = bits;
    const std::uint64_t    copy   = opaque;
    return std::bit_cast<double>(copy);
}

} // namespace

// =============================================================================
// FeedbackEcology_CoherenceHelperSmoke
// =============================================================================
// tests/test_helpers/coherence.h is the instrument SC-002 (d) reads. This case
// pins the instrument itself against pairs whose coherence is known a priori,
// so a helper bug can never be mistaken for a FeedbackEcology result. It uses
// NO FeedbackEcology.
//
// The filtered-copy arm is the one that matters most: a linear time-invariant
// transform does NOT reduce coherence. That is precisely why SC-002 (d) reports
// and never gates - with all six loops driven by one common source every loop
// output is H_i(f) * X(f), and the coherence of two outputs of a common source
// is 1 at every frequency whatever the coupling is set to.
TEST_CASE("FeedbackEcology_CoherenceHelperSmoke", "[feedback_ecology]") {
    using Krate::DSP::TestUtils::computeMeanCoherence;

    const std::vector<float> noiseA = makeCoherenceNoise(kCohSamples, 0.5f, 0x5EED0001u);
    const std::vector<float> noiseB = makeCoherenceNoise(kCohSamples, 0.5f, 0x5EED0002u);

    SECTION("a signal is fully coherent with itself") {
        const double c = computeMeanCoherence(noiseA.data(), noiseA.data(), kCohSamples, kCohFs,
                                              kCohFft, kCohHop, kCohBandLowHz, kCohBandHighHz);
        REQUIRE(c > 0.99);
        REQUIRE(c <= 1.0);
    }

    SECTION("independent noise streams are incoherent") {
        // The Welch bias on independent inputs is ~1/numFrames; at ~92 frames
        // the reading lands near 0.011, an order of magnitude below the gate.
        const double c = computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs,
                                              kCohFft, kCohHop, kCohBandLowHz, kCohBandHighHz);
        REQUIRE(c >= 0.0);
        REQUIRE(c < 0.2);
    }

    SECTION("an LTI filtered copy stays coherent in the passband") {
        Krate::DSP::SVF lowpass;
        lowpass.prepare(kCohFs);
        lowpass.setMode(Krate::DSP::SVFMode::Lowpass);
        lowpass.setCutoff(2000.0f);
        lowpass.setResonance(0.70710678f); // Butterworth

        std::vector<float> filtered(kCohSamples, 0.0f);
        for (std::size_t i = 0; i < kCohSamples; ++i) {
            filtered[i] = lowpass.process(noiseA[i]);
        }

        const double c =
            computeMeanCoherence(noiseA.data(), filtered.data(), kCohSamples, kCohFs, kCohFft,
                                 kCohHop, kCohBandLowHz, kCohPassbandHighHz);
        REQUIRE(c > 0.9);
    }

    SECTION("every ordered input guard returns zero") {
        // Anti-vacuity: the identical call with every argument valid reads
        // non-zero, so the zeros below are the guards firing and not a helper
        // that returns 0.0 for everything.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, kCohFft,
                                     kCohHop, kCohBandLowHz, kCohBandHighHz)
                > 0.0);

        // Null pointers.
        REQUIRE(computeMeanCoherence(nullptr, noiseB.data(), kCohSamples, kCohFs, kCohFft, kCohHop,
                                     kCohBandLowHz, kCohBandHighHz)
                == 0.0);
        REQUIRE(computeMeanCoherence(noiseA.data(), nullptr, kCohSamples, kCohFs, kCohFft, kCohHop,
                                     kCohBandLowHz, kCohBandHighHz)
                == 0.0);

        // Zero length.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), 0, kCohFs, kCohFft, kCohHop,
                                     kCohBandLowHz, kCohBandHighHz)
                == 0.0);

        // Non-power-of-two fftSize, and zero.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, 3000,
                                     kCohHop, kCohBandLowHz, kCohBandHighHz)
                == 0.0);
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, 0, kCohHop,
                                     kCohBandLowHz, kCohBandHighHz)
                == 0.0);

        // hopSize == 0 and hopSize > fftSize.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, kCohFft, 0,
                                     kCohBandLowHz, kCohBandHighHz)
                == 0.0);
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, kCohFft,
                                     kCohFft + 1, kCohBandLowHz, kCohBandHighHz)
                == 0.0);

        // Non-positive sample rate.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, 0.0, kCohFft,
                                     kCohHop, kCohBandLowHz, kCohBandHighHz)
                == 0.0);
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, -48000.0, kCohFft,
                                     kCohHop, kCohBandLowHz, kCohBandHighHz)
                == 0.0);

        // Non-finite sample rate.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples,
                                     nonFiniteDouble(kNaNBitsD), kCohFft, kCohHop, kCohBandLowHz,
                                     kCohBandHighHz)
                == 0.0);
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples,
                                     nonFiniteDouble(kInfBitsD), kCohFft, kCohHop, kCohBandLowHz,
                                     kCohBandHighHz)
                == 0.0);

        // Non-finite band edges.
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, kCohFft,
                                     kCohHop, nonFiniteFloat(kNaNBitsF), kCohBandHighHz)
                == 0.0);
        REQUIRE(computeMeanCoherence(noiseA.data(), noiseB.data(), kCohSamples, kCohFs, kCohFft,
                                     kCohHop, kCohBandLowHz, nonFiniteFloat(kInfBitsF))
                == 0.0);
    }
}

// =============================================================================
// T018 - the two [long] coupling / drift criteria: SC-002 and SC-003
// =============================================================================
// BOTH CASES ARE FLAT - no SECTIONs. Catch2 re-runs a TEST_CASE body once per
// leaf SECTION, so a 60 s (SC-002: ten of them) or 300 s (SC-003) render placed
// above four SECTIONs would be rendered four times over. Every arm below is
// therefore an INFO-scoped REQUIRE in one straight-line body, with the arm
// letter named in the INFO so a failure still says which clause broke.
//
// STREAMING: neither case materialises its render. SC-002 keeps six
// block-sized tap buffers and accumulates sum-of-squares; SC-003 keeps one
// analysis window. The only bounded capture is SC-002 (d)'s coherence window
// (2.73 s x six taps = 3.1 MB), which exists because a Welch estimate cannot be
// computed block by block.
// =============================================================================

using Krate::DSP::FeedbackEcology;

namespace {

constexpr std::size_t kLoops   = FeedbackEcology::kMaxLoops;
constexpr double      kSweepFs = 48000.0;
/// Own feedback for the FEEDBACK arm of the SC-002 sweep. Chosen so the FR-035
/// row-sum cap (kMaxTotalLoopGain = 0.95) never engages at any sweep point:
/// 0.5 + 5 x 0.08 = 0.90. At kDefaultLoopGain = 0.72 the cap engaged from
/// c = 0.05 on and converted OWN regeneration into cross paths, so raising c
/// lowered the driven loop's regeneration - the 2026-09-13 compliance run
/// measured T(0.20) - T(0.02) = 14.96 dB against a feed-forward 19.55 dB, the
/// old delta clause inverted by the normaliser, not by the coupling.
constexpr float       kSweepOwnFeedback = 0.5f;
/// Every loop's resonator centred here for the SC-002 sweep. 1000 Hz is an EXACT
/// comb tooth of every default delay at once (they are integer milliseconds:
/// 41, 67, 109, 173, 281, 449 ms -> 41 ... 449 cycles), so energy coupled in
/// from loop 0 lands inside every receiver's 3 Hz passband AND recirculates in
/// phase in every loop. A per-loop nearest tooth to 300 Hz (292.7 / 298.5 /
/// 302.8 / 300.6 / 298.9 / 300.7 Hz) put the sender's tooth outside the
/// receivers' bands: sender +1.7 dB, receivers +0.8 dB, T down 0.9 dB
/// (2026-09-13 run 3). The loop cutoffs are raised to kSweepCutoffHz so the
/// tooth sits inside every SVF passband too (defaults reach down to 420 Hz). With
/// the default sub-cutoff table (1200 ... 210 Hz) each Q ~ 100 resonator
/// rejects the others' centres by ~30 dB, and the coupled transfer is set by
/// the receiver's first pass alone: measured 2026-09-13, T(c) - T_ff(c) =
/// -0.10 dB at every sweep point, i.e. regeneration invisible on mismatched
/// resonators. That is a property of the default voicing (recorded in the
/// compliance report for Phase 10), not a defect this arm can fail on.
constexpr float       kSweepResonanceHz = 1000.0f;
constexpr float       kSweepCutoffHz    = 2400.0f;  ///< every loop's SVF cutoff for the sweep
constexpr std::size_t kBlock   = 4096;

/// THE REFERENCE DRIVE (tasks.md "The reference patch and the reference
/// drive"): white noise at -12 dBFS **RMS**, not peak. The two differ by
/// 10-12 dB for white noise. Xorshift32::nextFloat() is uniform on [-1, +1],
/// whose RMS is 1/sqrt(3), so the amplitude that lands the RMS on
/// 10^(-12/20) = 0.251189 is 0.251189 * sqrt(3) = 0.435072.
constexpr float         kReferenceDriveAmplitude = 0.435072f;
constexpr std::uint32_t kReferenceDriveSeed      = 0x0D817Eu;
constexpr std::uint32_t kPatchSeed               = 0x5EEDu;

/// The reference drive as a STATEFUL generator: a 60 s render never
/// materialises its input, and a fresh Xorshift32 per block would restart the
/// noise on every block.
class ReferenceDrive {
public:
    explicit ReferenceDrive(std::uint32_t seed = kReferenceDriveSeed) noexcept : rng_(seed) {}

    void fill(float* left, float* right, std::size_t numSamples) noexcept {
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i]  = rng_.nextFloat() * kReferenceDriveAmplitude;
            right[i] = rng_.nextFloat() * kReferenceDriveAmplitude;
        }
    }

private:
    Krate::DSP::Xorshift32 rng_;
};

// ---------------------------------------------------------------------------
// SC-002's sweep instrument
// ---------------------------------------------------------------------------

constexpr std::size_t kSweepSamples = 2880000;  ///< 60 s at kSweepFs

/// SC-002 (d)'s coherence capture: 2.73 s starting 10 s in, so the window sits
/// well past the loops' 1.0 s-RT60 charge-up. 131072 samples at kCohFft = 4096 /
/// kCohHop = 2048 is 63 Welch frames - enough for a REPORTED figure, and the
/// arm cannot gate anything whatever it reads (see the algebra in the banner).
constexpr std::size_t kCohStart  = 480000;
constexpr std::size_t kCohWindow = 131072;

/// One point of the SC-002 sweep. Everything the case asserts or prints comes
/// out of this struct, so the render itself happens exactly once per point.
struct TransferReading {
    float  coupling        = 0.0f;
    double drivenRms       = 0.0;  ///< RMS of loop 0's tap
    double undrivenRms     = 0.0;  ///< pooled RMS over the taps of loops 1..5
    double transferDb      = 0.0;  ///< T(c); only meaningful when transferDefined
    bool   transferDefined = false;

    double meanCoherence    = 0.0;  ///< arm (d), REPORTED
    bool   coherenceMeasured = false;

    std::size_t nonZeroUndriven = 0;     ///< arm (a): samples of taps 1..5 that are not 0.0f
    float       maxAbsUndriven  = 0.0f;  ///< arm (a): reported alongside the count

    bool inputGainsZeroed = false;  ///< arm (a) anti-vacuity, read at render start
    bool couplingZeroed   = false;  ///< arm (a) anti-vacuity, read at render start

    std::uint32_t clampEngagements = 0;
    std::uint32_t nonFiniteResets  = 0;
};

/// Renders one sweep point.
///
/// THE FIXTURE RULE IS MANDATORY AND IS WHY (a) IS REACHABLE ON A CORRECT
/// BUILD: prepare() -> write the WHOLE configuration -> reset() -> render.
/// prepare() snaps every inputRamp to kDefaultLoopInputGain = 1.0f and every
/// coupling smoother to FR-033's 0.04 neighbour ring, so a fixture that
/// rendered straight after the setters would glide 1.0 -> 0 over kMixRampMs and
/// 0.04 -> 0 over kCouplingSmoothMs: loops 1-5 DIRECTLY DRIVEN for ~960
/// samples, and that energy then circulating at kDefaultLoopGain = 0.72 through
/// a 1.0 s-RT60 resonator for seconds. reset() snaps all of it and clears the
/// audio in one call, so the configuration is exact from sample 0.
///
/// @param ownFeedback false builds arm (c)'s FEED-FORWARD baseline: every
///        setLoopGain(i, 0) leaves six parallel chains whose only inter-loop
///        path is the coupling matrix itself.
[[nodiscard]] TransferReading measureTransfer(float coupling, bool ownFeedback,
                                              bool captureCoherence) {
    TransferReading out;
    out.coupling = coupling;

    FeedbackEcology fe;
    fe.setSeed(kPatchSeed);
    fe.prepare(kSweepFs,
               FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlock, .numLoops = kLoops});
    // The reference patch: mix = 1 so the criterion measures the WET path and
    // not the crossfade, wetGain = 0 dB. Both AFTER prepare(), whose step 7
    // applyDefaults() restores kDefaultMix = 0.15f on EVERY call (FR-005 (c)).
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);

    // Single-loop excitation.
    fe.setLoopInputGain(0, 1.0f);
    for (std::size_t i = 1; i < kLoops; ++i) {
        fe.setLoopInputGain(i, 0.0f);
    }

    // Both arms set every loop's own feedback explicitly: kSweepOwnFeedback for
    // the feedback arm (under the row-sum cap at every sweep point, see the
    // constant), 0 for the feed-forward baseline.
    // Wander off so every loop's delay stays exactly on its FR-022 base value:
    // the resonance below is placed on a comb tooth of THAT delay, and a
    // wandering delay would walk the tooth off the resonator.
    fe.setWanderEnabled(false);
    for (std::size_t i = 0; i < kLoops; ++i) {
        fe.setLoopGain(i, ownFeedback ? kSweepOwnFeedback : 0.0f);
        // Resonance on the comb tooth shared by every loop's round trip (see
        // kSweepResonanceHz), so recirculation is CONSTRUCTIVE in every loop:
        // at an arbitrary phase the regeneration gain 1 / |1 - ownFb e^{j phi}|
        // ranges 0.67x to 2x and the pooled figure averaged -0.04 dB on the
        // 2026-09-13 run with an untuned shared centre. The tooth formula is
        // kept so a changed default delay table still lands on a tooth.
        const float periodS = fe.getLoopTargetDelayMs(i) * 0.001f;
        const float tooth   = std::round(kSweepResonanceHz * periodS);
        fe.setLoopResonanceHz(i, tooth / periodS);
        fe.setLoopCutoffHz(i, kSweepCutoffHz);
    }

    // Every off-diagonal entry set uniformly to c. The diagonal is a no-op.
    for (std::size_t from = 0; from < kLoops; ++from) {
        for (std::size_t to = 0; to < kLoops; ++to) {
            if (from != to) fe.setCoupling(from, to, coupling);
        }
    }

    fe.reset();

    // Arm (a)'s anti-vacuity guards, read at the moment the render starts: a
    // build that silently ignored the reset() snap fails HERE rather than
    // hiding behind a tolerance downstream.
    out.inputGainsZeroed = true;
    for (std::size_t i = 1; i < kLoops; ++i) {
        if (fe.getLoopInputGain(i) != 0.0f) out.inputGainsZeroed = false;
    }
    out.couplingZeroed = true;
    for (std::size_t i = 1; i < kLoops; ++i) {
        for (std::size_t j = 0; j < kLoops; ++j) {
            if (j == i) continue;
            if (fe.getLoopAppliedCoupling(j, i) != 0.0f) out.couplingZeroed = false;
        }
    }

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    std::array<std::vector<float>, kLoops> tapBuf;
    std::array<float*, kLoops>             taps{};
    for (std::size_t i = 0; i < kLoops; ++i) {
        tapBuf[i].assign(kBlock, 0.0f);
        taps[i] = tapBuf[i].data();
    }

    std::array<std::vector<float>, kLoops> cohCapture;
    if (captureCoherence) {
        for (std::size_t i = 0; i < kLoops; ++i) {
            cohCapture[i].reserve(kCohWindow);
        }
    }

    std::array<double, kLoops> sumSq{};
    ReferenceDrive             drive;

    std::size_t done = 0;
    while (done < kSweepSamples) {
        const std::size_t n = std::min(kBlock, kSweepSamples - done);
        drive.fill(inL.data(), inR.data(), n);
        fe.processBlockTapped(inL.data(), inR.data(), outL.data(), outR.data(), taps.data(), n);

        // Loop 0's tap: the transfer denominator.
        {
            const float* t   = tapBuf[0].data();
            double       acc = 0.0;
            for (std::size_t s = 0; s < n; ++s) {
                acc += static_cast<double>(t[s]) * static_cast<double>(t[s]);
            }
            sumSq[0] += acc;
        }

        // The undriven taps: the transfer numerator, plus arm (a)'s exact-zero
        // count in the SAME pass.
        for (std::size_t i = 1; i < kLoops; ++i) {
            const float* t   = tapBuf[i].data();
            double       acc = 0.0;
            for (std::size_t s = 0; s < n; ++s) {
                const float v = t[s];
                acc += static_cast<double>(v) * static_cast<double>(v);
                if (v != 0.0f) ++out.nonZeroUndriven;
                const float a = std::abs(v);
                if (a > out.maxAbsUndriven) out.maxAbsUndriven = a;
            }
            sumSq[i] += acc;
        }

        if (captureCoherence) {
            const std::size_t blockStart = done;
            const std::size_t blockEnd   = done + n;
            const std::size_t from       = std::max(blockStart, kCohStart);
            const std::size_t to         = std::min(blockEnd, kCohStart + kCohWindow);
            if (from < to) {
                const auto lo = static_cast<std::ptrdiff_t>(from - blockStart);
                const auto hi = static_cast<std::ptrdiff_t>(to - blockStart);
                for (std::size_t i = 0; i < kLoops; ++i) {
                    cohCapture[i].insert(cohCapture[i].end(), tapBuf[i].data() + lo,
                                         tapBuf[i].data() + hi);
                }
            }
        }

        done += n;
    }

    out.clampEngagements = fe.getClampEngagementCount();
    out.nonFiniteResets  = fe.getNonFiniteResetCount();

    const auto nD = static_cast<double>(kSweepSamples);
    out.drivenRms = std::sqrt(sumSq[0] / nD);

    double undrivenAcc = 0.0;
    for (std::size_t i = 1; i < kLoops; ++i) {
        undrivenAcc += sumSq[i];
    }
    out.undrivenRms = std::sqrt(undrivenAcc / (nD * static_cast<double>(kLoops - 1)));

    if (out.drivenRms > 0.0 && out.undrivenRms > 0.0) {
        out.transferDb      = 20.0 * std::log10(out.undrivenRms / out.drivenRms);
        out.transferDefined = true;
    }

    if (captureCoherence && cohCapture[0].size() == kCohWindow) {
        double      acc   = 0.0;
        std::size_t pairs = 0;
        for (std::size_t a = 0; a < kLoops; ++a) {
            for (std::size_t b = a + 1; b < kLoops; ++b) {
                acc += Krate::DSP::TestUtils::computeMeanCoherence(
                    cohCapture[a].data(), cohCapture[b].data(), kCohWindow, kSweepFs, kCohFft,
                    kCohHop, kCohBandLowHz, kCohBandHighHz);
                ++pairs;
            }
        }
        if (pairs != 0) {
            out.meanCoherence     = acc / static_cast<double>(pairs);
            out.coherenceMeasured = true;
        }
    }

    return out;
}

/// One row of the probe table, so the recorded transcript carries the numbers
/// kInteractionMarginDb has to be pinned from (O-1).
void appendRow(std::ostringstream& os, const char* label, const TransferReading& r) {
    os << "  " << label << "  c = " << r.coupling << "   driven RMS " << r.drivenRms
       << "   undriven RMS " << r.undrivenRms << "   T(c) ";
    if (r.transferDefined) {
        os << r.transferDb << " dB";
    } else {
        os << "-inf (the undriven taps are silent)";
    }
    os << "   coherence ";
    if (r.coherenceMeasured) {
        os << r.meanCoherence;
    } else {
        os << "n/a";
    }
    os << "   clamps " << r.clampEngagements << "   nonfinite resets " << r.nonFiniteResets
       << "\n";
}

}  // namespace

// =============================================================================
// SC-002 - cross-loop interaction is real, and rises with coupling
// =============================================================================
// WHY MAGNITUDE-SQUARED COHERENCE IS REPORTED AND NOT GATED. All six loops are
// driven by the same mono signal (FR-016) through taps that default to unity
// (FR-074), so loop i's output is H_i(f)*X(f) for one common X. For two outputs
// of a common source the magnitude-squared coherence is
//   |H_1|^2 |H_2|^2 S_xx^2 / (|H_1|^2 S_xx * |H_2|^2 S_xx) = 1
// at every frequency, INDEPENDENT of H_1, H_2 and therefore of the coupling.
// The only decorrelation in the component is FR-042's tanh (within 2 % of
// linear at default levels) and the 0.03 Hz wander (near time-invariant across
// 85 ms Welch segments) - neither is a function of coupling. A coherence gate
// would sit near unity before the first coupling was ever set. The roadmap's
// named metric (line 281) goes on the record in arm (d); the GATE is the
// transfer measurement under single-loop excitation.
//
// THE DEFECT THIS CATCHES: a build that merely mixes the six loop outputs
// together. It reproduces the feed-forward baseline arm (c) measures IN THE
// SAME TEST and fails there, while sailing past a naive "the undriven loops are
// not silent" check.
// =============================================================================
TEST_CASE("FeedbackEcology_CrossLoopTransfer", "[feedback_ecology][long]") {
    // O-1, PINNED 2026-09-13 from the measurement (was a provisional 1.5 dB with
    // nothing in the tree behind it). On the final fixture (kSweepOwnFeedback
    // 0.5, kSweepResonanceHz 1000 Hz shared tooth, kSweepCutoffHz 2400 Hz,
    // wander off) T(c) - T_ff(c) measured +1.062 / +1.081 / +1.118 / +1.188 dB
    // at c = 0.01 / 0.02 / 0.04 / 0.08 - positive, monotone in c, and spread by
    // hundredths across four 60 s renders. It is below the +4 dB a pure in-band
    // estimate gives because loop 0's tap also carries its Q ~ 100 resonator's
    // broadband skirt leakage, which the receivers' resonators reject rather
    // than regenerate. The margin is half the measured minimum; a build that
    // merely mixes loop outputs sits at exactly 0 dB and still fails. THE SHAPE
    // ASSERTIONS - (a)'s exact zero, (b)'s strict monotonicity, (c)'s strict
    // inequality against the MEASURED feed-forward baseline - MAY NEVER BE
    // WEAKENED, and this margin may be re-pinned only by recording a new
    // measurement that justifies it.
    constexpr double kInteractionMarginDb = 0.5;

    // Sweep kept under the FR-035 cap with kSweepOwnFeedback (max row sum 0.90).
    constexpr std::array<float, 5> kCouplingPoints = {0.0f, 0.01f, 0.02f, 0.04f, 0.08f};
    constexpr std::size_t          kPoints         = kCouplingPoints.size();

    // The gated sweep: feedback at kDefaultLoopGain, coherence captured.
    std::array<TransferReading, kPoints> fb{};
    for (std::size_t k = 0; k < kPoints; ++k) {
        fb[k] = measureTransfer(kCouplingPoints[k], /*ownFeedback=*/true,
                                /*captureCoherence=*/true);
    }

    // Arm (c)'s baseline, MEASURED in the same test and never assumed: every
    // ownFb = 0, i.e. six parallel feed-forward chains.
    std::array<TransferReading, kPoints> ff{};
    for (std::size_t k = 0; k < kPoints; ++k) {
        ff[k] = measureTransfer(kCouplingPoints[k], /*ownFeedback=*/false,
                                /*captureCoherence=*/false);
    }

    {
        std::ostringstream os;
        os << "\nSC-002 cross-loop transfer, 60 s per point at " << kSweepFs
           << " Hz, single-loop excitation (loop 0 tap = 1, loops 1-5 tap = 0)\n"
           << "T(c) = 20*log10( pooled RMS of the taps of loops 1..5 / RMS of loop 0's tap )\n";
        for (std::size_t k = 0; k < kPoints; ++k) {
            appendRow(os, "[fb]", fb[k]);
        }
        for (std::size_t k = 0; k < kPoints; ++k) {
            appendRow(os, "[ff]", ff[k]);
        }
        for (std::size_t k = 1; k < kPoints; ++k) {
            if (fb[k].transferDefined && ff[k].transferDefined) {
                os << "  T(" << kCouplingPoints[k] << ") - T_ff(" << kCouplingPoints[k]
                   << ") = " << (fb[k].transferDb - ff[k].transferDb) << " dB\n";
            }
        }
        os << "  kInteractionMarginDb (O-1) = " << kInteractionMarginDb << " dB\n";
        WARN(os.str());
    }

    // --- (a) the floor is EXACT, not statistical --------------------------
    // At c = 0 an undriven loop's input sum is 0*monoIn + appliedOwnFb*0 + 0,
    // and reset() zeroed the delay buffers, so every sample of five taps is
    // exactly 0.0f from the first - no warm-up, no discarded window.
    {
        INFO("(a) c = 0, feedback sweep: " << fb[0].nonZeroUndriven
                                           << " non-zero samples across the five undriven taps, "
                                              "max |tap| "
                                           << fb[0].maxAbsUndriven);
        REQUIRE(fb[0].inputGainsZeroed);  // anti-vacuity: the reset() snap really landed
        REQUIRE(fb[0].couplingZeroed);    // anti-vacuity: every applied coupling is 0 at start
        REQUIRE(fb[0].nonZeroUndriven == std::size_t{0});
        REQUIRE(fb[0].maxAbsUndriven == 0.0f);
        REQUIRE(fb[0].drivenRms > 0.0);  // and loop 0 was actually excited
    }
    {
        INFO("(a) c = 0, feed-forward sweep: " << ff[0].nonZeroUndriven
                                               << " non-zero samples, max |tap| "
                                               << ff[0].maxAbsUndriven);
        REQUIRE(ff[0].inputGainsZeroed);
        REQUIRE(ff[0].couplingZeroed);
        REQUIRE(ff[0].nonZeroUndriven == std::size_t{0});
        REQUIRE(ff[0].maxAbsUndriven == 0.0f);
        REQUIRE(ff[0].drivenRms > 0.0);
    }

    // Every non-zero point must have produced a measurable transfer at all,
    // otherwise (b) and (c) would be comparing sentinels.
    for (std::size_t k = 1; k < kPoints; ++k) {
        INFO("c = " << kCouplingPoints[k] << ": fb T defined " << fb[k].transferDefined
                    << ", ff T defined " << ff[k].transferDefined);
        REQUIRE(fb[k].transferDefined);
        REQUIRE(ff[k].transferDefined);
        REQUIRE(fb[k].nonFiniteResets == std::uint32_t{0});
        REQUIRE(ff[k].nonFiniteResets == std::uint32_t{0});
    }

    // --- (b) monotone rise across the four non-zero points ----------------
    for (std::size_t k = 2; k < kPoints; ++k) {
        INFO("(b) T(" << kCouplingPoints[k - 1] << ") = " << fb[k - 1].transferDb
                      << " dB must be < T(" << kCouplingPoints[k] << ") = " << fb[k].transferDb
                      << " dB");
        REQUIRE(fb[k - 1].transferDb < fb[k].transferDb);
    }

    // --- (c) anti-vacuity: regeneration, not just mixing --------------------
    // Amended 2026-09-13 (spec SC-002 (c)). At EVERY non-zero coupling point the
    // feedback arm's transfer must exceed the feed-forward baseline by the
    // margin: the receiving loops regenerate what they are handed
    // (1 / (1 - kSweepOwnFeedback) = 2x, about +6 dB, before the loop filters),
    // while a build that merely mixes loop outputs reproduces T_ff exactly. The
    // earlier delta form compared slopes and was inverted by the row-sum
    // normaliser once the sweep crossed the cap (see kSweepOwnFeedback).
    for (std::size_t k = 1; k < kPoints; ++k) {
        INFO("(c) T(" << kCouplingPoints[k] << ") = " << fb[k].transferDb
                      << " dB must be >= T_ff = " << ff[k].transferDb << " dB + "
                      << kInteractionMarginDb << " dB");
        REQUIRE(fb[k].transferDb >= ff[k].transferDb + kInteractionMarginDb);
    }

    // --- (d) REPORTED, never gated ----------------------------------------
    // The table above already carries the figure at every coupling point. The
    // only assertion here is that the estimator RAN and returned something in
    // the range a coherence can occupy - never a threshold on the value, for
    // the reason in the banner.
    for (std::size_t k = 0; k < kPoints; ++k) {
        INFO("(d) c = " << kCouplingPoints[k] << ": mean pairwise coherence over ["
                        << kCohBandLowHz << ", " << kCohBandHighHz
                        << "] Hz = " << fb[k].meanCoherence);
        REQUIRE(fb[k].coherenceMeasured);
        REQUIRE(fb[k].meanCoherence >= 0.0);
        REQUIRE(fb[k].meanCoherence <= 1.0);
    }
}

// =============================================================================
// SC-003 - no zipper on delay-time drift
// =============================================================================
// Input: a 110 Hz sine at -12 dBFS - a PITCHED tone makes delay-position
// artefacts audible where noise hides them - for 300 s, reference patch but
// delayWanderFraction = kMaxDelayWanderFraction on every loop and
// wanderRate = kMaxWanderRateHz, so the CrossfadingDelayLine staircase fires as
// often as the component can make it fire.
//
// WHY ARM (c) EXISTS AND IS CHECKED FIRST: CrossfadingDelayLine::setDelaySamples
// moves only the INACTIVE tap and starts a crossfade only once the target has
// drifted kCrossfadeThresholdSamples = 100 from the ACTIVE tap
// (crossfading_delay_line.h:161-191). A build whose delay never moves that far
// produces a perfectly smooth render and passes (a) and (b) trivially. The
// count is read from the one named accessor, getLoopCrossfadeCount(i) - an
// exact count of crossfade ONSETS - and never from step detection, which misses
// every crossfade retriggered mid-fade (:176-181).
//
// The wet path is MONO (FR-016) and mix = 1, so outR carries the same samples as
// outL; the detector and the first-difference statistics run on outL, and the
// peak is taken over both channels.
// =============================================================================
TEST_CASE("FeedbackEcology_DelayDriftClickFree", "[feedback_ecology][long]") {
    using Krate::DSP::TestUtils::ClickDetection;
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;

    constexpr double      kFs    = 48000.0;
    constexpr std::size_t kTotal = 14400000;  ///< 300 s at kFs

    constexpr double kSineAmplitude = 0.251189;  ///< -12 dBFS, PEAK
    constexpr double kSineHz        = 110.0;
    constexpr double kTwoPiD        = 2.0 * static_cast<double>(Krate::DSP::kPi);

    /// (b) clause 1: the render's own level. A quiet render cannot pass.
    constexpr float kMinPeak = 0.05f;
    /// (b) clause 2: the maximum absolute first difference, as a fraction of
    /// that MEASURED peak.
    constexpr float kMaxFirstDiffFractionOfPeak = 0.05f;
    /// (b) clause 3: and no first difference beyond this multiple of the median
    /// of the largest kTopCount first differences.
    constexpr float       kMaxFirstDiffOverMedian = 8.0f;
    constexpr std::size_t kTopCount               = 1000;
    /// (c): crossfade ONSETS summed over the six loops.
    constexpr std::uint32_t kMinCrossfadeOnsets = 200;

    FeedbackEcology fe;
    fe.setSeed(kPatchSeed);
    fe.prepare(kFs, FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlock, .numLoops = kLoops});
    fe.setMix(1.0f);
    // Wet trim at its maximum (amended 2026-09-13, spec SC-003 (b)): the six
    // Q ~ 100 resonators pass a -12 dBFS sine about 30 dB down (the header's
    // DERIVATION TABLE 3), so at the 0 dB default the render peaks at 0.0095
    // and kMinPeak's "unity-ish wet path" premise was false for every correct
    // build. +24 dB puts the peak near 0.15 against the unchanged 0.05 floor;
    // the trim is a linear gain after the loops, so clicks are unaffected.
    fe.setWetGain(FeedbackEcology::kMaxWetGainDb);
    for (std::size_t i = 0; i < kLoops; ++i) {
        fe.setLoopDelayWander(i, FeedbackEcology::kMaxDelayWanderFraction);
    }
    fe.setWanderRate(FeedbackEcology::kMaxWanderRateHz);
    // configure -> reset() -> render: reset() snaps every ramp to the values
    // just written and re-derives all twelve lane streams from the seed.
    fe.reset();

    REQUIRE(fe.getWanderRate() == FeedbackEcology::kMaxWanderRateHz);
    REQUIRE(fe.getLoopDelayWander(0) == FeedbackEcology::kMaxDelayWanderFraction);

    // The detector, at a config whose sampleRate is the RENDER rate: the
    // struct's default is 44100.0f (artifact_detection.h:38), which at a 48 kHz
    // render mis-reports every timeSeconds it returns.
    ClickDetectorConfig cfg;
    cfg.sampleRate = static_cast<float>(kFs);
    REQUIRE(cfg.isValid());
    ClickDetector detector(cfg);
    detector.prepare();

    // The analysis window is carried with an overlap of one frame, so a click
    // sitting on a chunk boundary is still inside a whole frame of the NEXT
    // analysis buffer. A detection may therefore be counted twice - harmless,
    // because the assertion is ZERO.
    const std::size_t kAnalysisChunk = 49152;  // 1.024 s at kFs
    const std::size_t kAnalysisCarry = cfg.frameSize;

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    std::vector<float> analysis;
    analysis.reserve(kAnalysisChunk + kBlock);

    std::size_t totalDetections     = 0;
    std::size_t firstDetectionIndex = 0;
    bool        haveFirstDetection  = false;
    std::size_t analysisBase        = 0;  // absolute index of analysis[0]

    const auto runDetector = [&]() {
        if (analysis.size() < 2) return;
        const std::vector<ClickDetection> hits = detector.detect(analysis.data(), analysis.size());
        if (!hits.empty() && !haveFirstDetection) {
            haveFirstDetection  = true;
            firstDetectionIndex = analysisBase + hits.front().sampleIndex;
        }
        totalDetections += hits.size();
    };

    // The kTopCount largest absolute first differences, kept as a MIN-heap so a
    // 14.4 M-sample render costs one compare per sample in the common case.
    std::vector<float> top;
    top.reserve(kTopCount + 1);
    const auto considerDiff = [&](float d) {
        if (top.size() < kTopCount) {
            top.push_back(d);
            std::push_heap(top.begin(), top.end(), std::greater<float>{});
        } else if (d > top.front()) {
            std::pop_heap(top.begin(), top.end(), std::greater<float>{});
            top.back() = d;
            std::push_heap(top.begin(), top.end(), std::greater<float>{});
        }
    };

    double       phase        = 0.0;
    const double phaseInc     = kTwoPiD * kSineHz / kFs;
    float        peak         = 0.0f;
    float        maxFirstDiff = 0.0f;
    float        prevSample   = 0.0f;
    bool         havePrev     = false;
    bool         allFinite    = true;
    std::size_t  done         = 0;

    while (done < kTotal) {
        const std::size_t n = std::min(kBlock, kTotal - done);
        for (std::size_t s = 0; s < n; ++s) {
            const auto v = static_cast<float>(kSineAmplitude * std::sin(phase));
            inL[s]       = v;
            inR[s]       = v;
            phase += phaseInc;
            if (phase >= kTwoPiD) phase -= kTwoPiD;
        }
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);

        for (std::size_t s = 0; s < n; ++s) {
            const float l = outL[s];
            const float r = outR[s];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(r)) {
                allFinite = false;
            }
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
            if (havePrev) {
                const float d = std::abs(l - prevSample);
                if (d > maxFirstDiff) maxFirstDiff = d;
                considerDiff(d);
            }
            prevSample = l;
            havePrev   = true;
        }

        analysis.insert(analysis.end(), outL.data(), outL.data() + n);
        if (analysis.size() >= kAnalysisChunk) {
            runDetector();
            const std::size_t keep = std::min(kAnalysisCarry, analysis.size());
            analysisBase += analysis.size() - keep;
            analysis.erase(analysis.begin(), analysis.end() - static_cast<std::ptrdiff_t>(keep));
        }

        done += n;
    }
    runDetector();  // the tail

    std::uint32_t crossfades = 0;
    for (std::size_t i = 0; i < kLoops; ++i) {
        crossfades += fe.getLoopCrossfadeCount(i);
    }

    std::sort(top.begin(), top.end());
    const float medianTopDiff = top.empty() ? 0.0f : top[top.size() / 2];

    {
        std::ostringstream os;
        os << "\nSC-003 delay-drift click freedom, 110 Hz sine at -12 dBFS, "
           << (static_cast<double>(kTotal) / kFs) << " s at " << kFs
           << " Hz, max delay wander at max wander rate\n"
           << "  peak |sample|             = " << peak << "  (floor " << kMinPeak << ")\n"
           << "  max |first difference|    = " << maxFirstDiff << "  ("
           << (peak > 0.0f ? 100.0f * maxFirstDiff / peak : 0.0f) << " % of peak, ceiling "
           << (100.0f * kMaxFirstDiffFractionOfPeak) << " %)\n"
           << "  median of the largest " << kTopCount << " = " << medianTopDiff << "  (ratio "
           << (medianTopDiff > 0.0f ? maxFirstDiff / medianTopDiff : 0.0f) << ", ceiling "
           << kMaxFirstDiffOverMedian << ")\n"
           << "  click detections          = " << totalDetections << "\n"
           << "  crossfade onsets, 6 loops = " << crossfades << "  (floor " << kMinCrossfadeOnsets
           << ")\n"
           << "  clamp engagements         = " << fe.getClampEngagementCount()
           << ", nonfinite resets " << fe.getNonFiniteResetCount() << "\n";
        WARN(os.str());
    }

    REQUIRE(allFinite);
    REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});

    // --- (c) FIRST: (a) and (b) are worthless until the staircase is proved
    //         to have fired at all.
    {
        INFO("(c) crossfade onsets summed over the six loops: " << crossfades);
        REQUIRE(crossfades >= kMinCrossfadeOnsets);
    }

    // --- (b) both clauses, relative to the render's OWN level -------------
    {
        INFO("(b) peak " << peak << ", max first difference " << maxFirstDiff
                         << ", median of the largest " << kTopCount << " first differences "
                         << medianTopDiff);
        REQUIRE(peak >= kMinPeak);
        REQUIRE(top.size() == kTopCount);
        REQUIRE(medianTopDiff > 0.0f);
        REQUIRE(maxFirstDiff < kMaxFirstDiffFractionOfPeak * peak);
        REQUIRE(maxFirstDiff <= kMaxFirstDiffOverMedian * medianTopDiff);
    }

    // --- (a) zero click detections over the whole render ------------------
    {
        INFO("(a) " << totalDetections << " click detections; first at sample "
                    << (haveFirstDetection ? firstDetectionIndex : std::size_t{0}) << " ("
                    << (haveFirstDetection ? static_cast<double>(firstDetectionIndex) / kFs : 0.0)
                    << " s)");
        REQUIRE(totalDetections == std::size_t{0});
    }
}

// =============================================================================
// T019 - the phase's central criterion (SC-001) and the 30-minute evolution
// render (SC-021)
// =============================================================================
// STREAMING STATISTICS ARE MANDATORY HERE, not a preference. SC-001 (a1), (a2)
// and (b) are 30-minute renders: 86 400 000 samples per channel, ~691 MB
// materialised for the pair. Every statistic below is accumulated block by
// block - peak, finiteness, per-window sum-of-squares, and (SC-021) a Welch
// power spectrum folded one 4096-point frame at a time. The only buffers whose
// size depends on anything are:
//   * SC-001 (d)'s 500 ms analysis segment (25 024 floats, ~98 KB), which exists
//     because ClickDetector::detect takes a contiguous buffer; and
//   * SC-021's 180-entry per-window metric table (a centroid, an RMS and eight
//     log band energies each), which IS the measurement.
// Neither grows with the render length beyond the window count.
//
// SECTION COST. Catch2 re-runs a TEST_CASE body once per leaf SECTION, so the
// SC-001 body holds NOTHING outside its five SECTIONs but one lambda; each arm
// builds its own FeedbackEcology and renders exactly once.
// =============================================================================

namespace {

// ---------------------------------------------------------------------------
// The soak instrument, shared by SC-001 (a1), (a2), (b) and (c)
// ---------------------------------------------------------------------------

constexpr double      kSoakFs        = 48000.0;
constexpr std::size_t kMinuteSamples = 2880000;              ///< 60 s at kSoakFs
constexpr std::size_t kSoakWindows   = 30;                   ///< thirty 1-minute windows
constexpr std::size_t kSoakSamples   = kSoakWindows * kMinuteSamples;  ///< 30 min
static_assert(kSoakSamples == 86400000, "SC-001's soak is 30 minutes at 48 kHz");

/// dB of a non-negative linear level, FLOORED. An exactly silent window would
/// otherwise read -inf: std::log10(0.0) is -inf on every leg, and this TU is
/// deliberately compiled WITHOUT -fno-fast-math (banner), so an infinity that
/// entered the arithmetic here would propagate as finite garbage rather than as
/// a recognisable sentinel. -600 dB is unreachable by any real render and reads
/// unambiguously as "silent" in the reported table.
[[nodiscard]] double rmsDb(double rms) {
    constexpr double kSilenceFloor = 1.0e-30;
    return 20.0 * std::log10(std::max(rms, kSilenceFloor));
}

/// Everything SC-001's four bounded assertions and its level clauses need,
/// accumulated block by block. Nothing here is O(render length).
struct SoakResult {
    float         peakAbs          = 0.0f;   ///< over BOTH channels
    bool          allFinite        = true;   ///< detail::isFinite, never std::isnan
    std::uint32_t clampEngagements = 0;
    std::uint32_t nonFiniteResets  = 0;
    double        overallRms       = 0.0;
    /// One entry per COMPLETE analysis window, in order.
    std::vector<double> windowRms;
};

/// Renders `total` samples with the reference drive on for the first
/// `driveSamples` and silence after, accumulating SC-001's statistics.
///
/// The block split never straddles the drive cut-off or a window boundary, so
/// arm (a2)'s `setWanderEnabled(false)` lands on exactly the sample the
/// criterion names and every window RMS covers exactly `windowSamples` samples.
///
/// @param windowSamples 0 disables windowing; otherwise it must divide `total`.
/// @param disableWanderAtCutoff arm (a2): calls setWanderEnabled(false) at the
///        instant the drive stops, so no NEW crossfade fires during the tail and
///        the loop is the strict contraction rung 1 describes.
///
/// The RMS is taken from the LEFT channel: the wet path is mono (FR-016) and
/// every soak arm that reads an RMS renders at mix = 1, so both channels carry
/// the same samples. The PEAK is taken over both, so a channel-swap defect
/// cannot hide behind the choice.
[[nodiscard]] SoakResult renderSoak(FeedbackEcology& fe, std::size_t total,
                                    std::size_t driveSamples, std::size_t windowSamples,
                                    bool          disableWanderAtCutoff,
                                    std::uint32_t driveSeed = kReferenceDriveSeed) {
    SoakResult out;
    if (windowSamples != 0) {
        out.windowRms.reserve(total / windowSamples + 1);
    }

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    ReferenceDrive drive(driveSeed);

    double      totalSumSq     = 0.0;
    double      windowSumSq    = 0.0;
    std::size_t windowFill     = 0;
    bool        wanderDisabled = false;
    std::size_t done           = 0;

    while (done < total) {
        if (disableWanderAtCutoff && !wanderDisabled && done >= driveSamples) {
            fe.setWanderEnabled(false);
            wanderDisabled = true;
        }

        std::size_t n = std::min(kBlock, total - done);
        if (done < driveSamples) n = std::min(n, driveSamples - done);
        if (windowSamples != 0) n = std::min(n, windowSamples - windowFill);

        if (done < driveSamples) {
            drive.fill(inL.data(), inR.data(), n);
        } else {
            std::fill_n(inL.data(), n, 0.0f);
            std::fill_n(inR.data(), n, 0.0f);
        }

        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);

        double blockSumSq = 0.0;
        for (std::size_t s = 0; s < n; ++s) {
            const float l = outL[s];
            const float r = outR[s];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(r)) {
                out.allFinite = false;
            }
            const float a = std::max(std::abs(l), std::abs(r));
            if (a > out.peakAbs) out.peakAbs = a;
            const auto dl = static_cast<double>(l);
            blockSumSq += dl * dl;
        }
        totalSumSq += blockSumSq;
        windowSumSq += blockSumSq;

        done += n;
        if (windowSamples != 0) {
            windowFill += n;
            if (windowFill == windowSamples) {
                out.windowRms.push_back(
                    std::sqrt(windowSumSq / static_cast<double>(windowSamples)));
                windowSumSq = 0.0;
                windowFill  = 0;
            }
        }
    }

    out.overallRms       = std::sqrt(totalSumSq / static_cast<double>(total));
    out.clampEngagements = fe.getClampEngagementCount();
    out.nonFiniteResets  = fe.getNonFiniteResetCount();
    return out;
}

/// THE WORST-CASE CORNER (spec SC-001, plan S12.3): numLoops = 6; every
/// ownFb = kMaxLoopGain = 0.90; every off-diagonal coupling = kMaxCouplingPerPair
/// = 0.5 (raw row sum 3.40, FR-035-normalised to 0.95); every filter Q at
/// kButterworthQ; every resonator RT60 at kMaxDecayTime = 30 s (clamped again
/// per centre by rt60ToQ's kMaxResonatorQ ceiling - the REQUEST is what the
/// corner names); wander at kMaxWanderRateHz with delayWanderFraction =
/// kMaxDelayWanderFraction and cutoffWanderOctaves = kMaxCutoffWanderOctaves.
///
/// FIXTURE RULE (tasks.md, plan S12): prepare() -> write the WHOLE configuration
/// -> reset() -> render. prepare() snaps every ramp and smoother to the DEFAULT
/// tables, so a setter called after it glides; reset() snaps all of it and
/// clears the audio in one call, so the corner is in force from sample 0 rather
/// than 20-50 ms in.
///
/// @param governorRatio kMinGovernorRatio (1.0) turns the governor OFF, which is
///        what arms (a1)/(a2) require so the STRUCTURAL rungs are measured
///        without it; arm (b) passes kDefaultGovernorRatio.
void makeWorstCaseCorner(FeedbackEcology& fe, double fs, float governorRatio,
                         std::uint32_t seed = kPatchSeed) {
    fe.setSeed(seed);
    fe.prepare(fs, FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlock, .numLoops = kLoops});
    // AFTER prepare(): its applyDefaults() step restores kDefaultMix = 0.15f on
    // EVERY call (FR-005 (c)), so a mix written before it would be discarded.
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);

    for (std::size_t i = 0; i < kLoops; ++i) {
        fe.setLoopGain(i, FeedbackEcology::kMaxLoopGain);
        fe.setLoopFilterQ(i, FeedbackEcology::kMaxFilterQ);  // == SVF::kButterworthQ
        fe.setLoopResonanceRt60(i, Krate::DSP::kMaxDecayTime);
        fe.setLoopDelayWander(i, FeedbackEcology::kMaxDelayWanderFraction);
        fe.setLoopCutoffWander(i, FeedbackEcology::kMaxCutoffWanderOctaves);
    }
    for (std::size_t from = 0; from < kLoops; ++from) {
        for (std::size_t to = 0; to < kLoops; ++to) {
            if (from != to) fe.setCoupling(from, to, FeedbackEcology::kMaxCouplingPerPair);
        }
    }

    fe.setWanderRate(FeedbackEcology::kMaxWanderRateHz);
    fe.setWanderEnabled(true);
    fe.setGovernorThresholdDb(FeedbackEcology::kDefaultGovernorThresholdDb);
    fe.setGovernorRatio(governorRatio);

    fe.reset();
}

// ---------------------------------------------------------------------------
// SC-001 (c): the randomised sweep
// ---------------------------------------------------------------------------

/// The three FilterMode enumerators, as a table rather than a cast of a random
/// integer: an enumerator outside the three named values is a silent no-op in
/// the setter (feedback_ecology.h:864-877), and drawing one would make the arm
/// quietly narrower.
constexpr std::array<FeedbackEcology::FilterMode, 3> kFilterModes = {
    FeedbackEcology::FilterMode::Lowpass, FeedbackEcology::FilterMode::Bandpass,
    FeedbackEcology::FilterMode::Highpass};

constexpr std::size_t   kSweepConfigCount = 256;
constexpr std::uint32_t kSweepSeedBase    = 0x5A0C0001u;
constexpr std::size_t   kSweepRenderTotal = 2880000;  ///< 60 s at kSoakFs
constexpr std::size_t   kSweepRenderDrive = 480000;   ///< drive on for the first 10 s

/// One seeded configuration of arm (c). Every scalar is drawn uniformly from its
/// FULL clamped range, with exactly one documented exception.
///
/// **wetGain is drawn from [kMinWetGainDb, 0] dB, not its full [-24, +24].**
/// Above +4.27 dB (4.0 / sqrt(6) = 1.633) the trim can LEGITIMATELY drive the
/// FR-046 clamp, and this arm's third assertion is that the clamp never engages;
/// a positive draw would therefore be testing the arm's own fixture rather than
/// the component. SC-013 (b) covers the positive half, and covers it as the only
/// control that can reach the clamp. Every other scalar, mix included, is drawn
/// from its full clamped range.
void applyRandomConfig(FeedbackEcology& fe, double fs, std::size_t configIndex) {
    Krate::DSP::Xorshift32 rng(kSweepSeedBase + static_cast<std::uint32_t>(configIndex));

    const auto draw = [&rng](float lo, float hi) {
        return lo + rng.nextUnipolar() * (hi - lo);
    };
    const auto flip = [&rng]() { return (rng.next() & 1u) != 0u; };

    const auto numLoops =
        static_cast<std::size_t>(1u + (rng.next() % static_cast<std::uint32_t>(kLoops)));

    fe.setSeed(rng.next());
    fe.prepare(fs,
               FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlock, .numLoops = numLoops});

    // The two rate-dependent ceilings, spelled the way prepare() derives them
    // (feedback_ecology.h:537 and SVF::kMaxCutoffRatio), so the draw covers the
    // real range instead of relying on the setter's clamp to fold out-of-range
    // draws onto the endpoint - which would bias the sweep toward the maximum.
    const auto  maxCutoffHz = static_cast<float>(fs) * Krate::DSP::SVF::kMaxCutoffRatio;
    const float maxResonanceHz =
        std::max(Krate::DSP::kMinResonatorFrequency,
                 static_cast<float>(fs) * Krate::DSP::kMaxResonatorFrequencyRatio);

    for (std::size_t i = 0; i < kLoops; ++i) {
        fe.setLoopFilterMode(i, kFilterModes[rng.next() % 3u]);
        fe.setLoopCutoffHz(i, draw(FeedbackEcology::kMinCutoffHz, maxCutoffHz));
        fe.setLoopFilterQ(i, draw(FeedbackEcology::kMinFilterQ, FeedbackEcology::kMaxFilterQ));
        fe.setLoopDelayMs(i, draw(FeedbackEcology::kMinDelayMs, FeedbackEcology::kMaxDelayMs));
        fe.setLoopResonanceHz(i, draw(Krate::DSP::kMinResonatorFrequency, maxResonanceHz));
        fe.setLoopResonanceRt60(i, draw(Krate::DSP::kMinDecayTime, Krate::DSP::kMaxDecayTime));
        fe.setLoopGain(i, draw(FeedbackEcology::kMinLoopGain, FeedbackEcology::kMaxLoopGain));
        fe.setLoopInputGain(i, draw(0.0f, 1.0f));
        fe.setLoopDelayWander(i, draw(0.0f, FeedbackEcology::kMaxDelayWanderFraction));
        fe.setLoopCutoffWander(i, draw(0.0f, FeedbackEcology::kMaxCutoffWanderOctaves));
        fe.setLoopWake(i, draw(0.0f, 1.0f));
        fe.setLoopDormant(i, flip());
    }

    for (std::size_t from = 0; from < kLoops; ++from) {
        for (std::size_t to = 0; to < kLoops; ++to) {
            if (from != to) {
                fe.setCoupling(from, to, draw(0.0f, FeedbackEcology::kMaxCouplingPerPair));
            }
        }
    }

    fe.setGovernorThresholdDb(draw(FeedbackEcology::kMinGovernorThresholdDb,
                                   FeedbackEcology::kMaxGovernorThresholdDb));
    fe.setGovernorRatio(
        draw(FeedbackEcology::kMinGovernorRatio, FeedbackEcology::kMaxGovernorRatio));
    fe.setWanderRate(draw(FeedbackEcology::kMinWanderRateHz, FeedbackEcology::kMaxWanderRateHz));
    fe.setWanderEnabled(flip());
    fe.setMix(draw(0.0f, 1.0f));
    fe.setWetGain(draw(FeedbackEcology::kMinWetGainDb, 0.0f));

    fe.reset();
}

// ---------------------------------------------------------------------------
// SC-001 (d): the parameter-jump arm and its click-exemption table
// ---------------------------------------------------------------------------

/// Every setter the jump arm reaches. `prepare` and `reset` are DELIBERATELY
/// absent: they are lifecycle calls, not parameter jumps, and SC-011 owns the
/// re-prepare() behaviour.
enum class JumpSetter : std::uint8_t {
    // --- FR-076 SMOOTHED: held to the boundedness assertions AND to the click
    //     assertion.
    LoopCutoffHz = 0,
    LoopFilterQ,
    LoopDelayMs,
    LoopGain,
    LoopInputGain,
    Coupling,
    CouplingMatrix,
    GovernorThresholdDb,
    GovernorRatio,
    LoopDelayWander,
    LoopCutoffWander,
    LoopWake,
    LoopDormant,
    NumLoops,
    Mix,
    WetGain,
    // --- FR-076 STEPPED but HELD TO THE CLICK ASSERTION. setWanderRate writes
    //     only laneDecimation_ and the twelve lanes' smoothness; it steps NO
    //     mapped value in the signal path, so it cannot click and must not be
    //     excused from proving it (plan S12.3).
    WanderRate,
    // --- FR-076 STEPPED and EXEMPT FROM THE CLICK ASSERTION ONLY - exactly
    //     these five, enumerated in kClickExemptSetters below so the exemption
    //     cannot quietly widen.
    LoopFilterMode,
    LoopResonanceHz,
    LoopResonanceRt60,
    Seed,
    WanderEnabled,
    kCount
};

constexpr std::size_t kJumpSetterCount = static_cast<std::size_t>(JumpSetter::kCount);

constexpr std::array<const char*, kJumpSetterCount> kJumpSetterNames = {
    "setLoopCutoffHz",       "setLoopFilterQ",       "setLoopDelayMs",
    "setLoopGain",           "setLoopInputGain",     "setCoupling",
    "setCouplingMatrix",     "setGovernorThresholdDb", "setGovernorRatio",
    "setLoopDelayWander",    "setLoopCutoffWander",  "setLoopWake",
    "setLoopDormant",        "setNumLoops",          "setMix",
    "setWetGain",            "setWanderRate",        "setLoopFilterMode",
    "setLoopResonanceHz",    "setLoopResonanceRt60", "setSeed",
    "setWanderEnabled"};

/// THE EXEMPTION LIST, NAMED. SC-001 (d) exempts exactly these five setters from
/// the click assertion - and from nothing else: the four boundedness assertions
/// apply to every setter without exception.
constexpr std::array<JumpSetter, 5> kClickExemptSetters = {
    JumpSetter::LoopFilterMode, JumpSetter::LoopResonanceHz, JumpSetter::LoopResonanceRt60,
    JumpSetter::Seed, JumpSetter::WanderEnabled};

[[nodiscard]] constexpr bool isClickExempt(JumpSetter which) noexcept {
    for (const JumpSetter exempt : kClickExemptSetters) {
        if (exempt == which) return true;
    }
    return false;
}

static_assert(kClickExemptSetters.size() == 5,
              "SC-001 (d): the click exemption is EXACTLY five setters and may not widen");
static_assert(!isClickExempt(JumpSetter::WanderRate),
              "setWanderRate is FR-076-stepped but steps no mapped value in the signal path - it "
              "is HELD to the click assertion, never exempt (plan S12.3)");
static_assert(!isClickExempt(JumpSetter::NumLoops),
              "setNumLoops is FR-076-smoothed (FR-075's three ramps) - held to the click assertion");
static_assert(!isClickExempt(JumpSetter::CouplingMatrix),
              "a whole-matrix write is FR-034-smoothed per pair - held to the click assertion");

/// Jumps one setter to one randomly chosen EXTREME of its range.
void applyJump(FeedbackEcology& fe, JumpSetter which, Krate::DSP::Xorshift32& rng, double fs) {
    const bool high = (rng.next() & 1u) != 0u;
    const auto loop = static_cast<std::size_t>(
        rng.next() % static_cast<std::uint32_t>(FeedbackEcology::kMaxLoops));

    const auto  maxCutoffHz = static_cast<float>(fs) * Krate::DSP::SVF::kMaxCutoffRatio;
    const float maxResonanceHz =
        std::max(Krate::DSP::kMinResonatorFrequency,
                 static_cast<float>(fs) * Krate::DSP::kMaxResonatorFrequencyRatio);

    switch (which) {
    case JumpSetter::LoopCutoffHz:
        fe.setLoopCutoffHz(loop, high ? maxCutoffHz : FeedbackEcology::kMinCutoffHz);
        break;
    case JumpSetter::LoopFilterQ:
        fe.setLoopFilterQ(loop,
                          high ? FeedbackEcology::kMaxFilterQ : FeedbackEcology::kMinFilterQ);
        break;
    case JumpSetter::LoopDelayMs:
        fe.setLoopDelayMs(loop,
                          high ? FeedbackEcology::kMaxDelayMs : FeedbackEcology::kMinDelayMs);
        break;
    case JumpSetter::LoopGain:
        fe.setLoopGain(loop, high ? FeedbackEcology::kMaxLoopGain : FeedbackEcology::kMinLoopGain);
        break;
    case JumpSetter::LoopInputGain:
        fe.setLoopInputGain(loop, high ? 1.0f : 0.0f);
        break;
    case JumpSetter::Coupling: {
        const auto to = static_cast<std::size_t>(
            rng.next() % static_cast<std::uint32_t>(FeedbackEcology::kMaxLoops));
        fe.setCoupling(loop, to, high ? FeedbackEcology::kMaxCouplingPerPair : 0.0f);
        break;
    }
    case JumpSetter::CouplingMatrix: {
        // Every off-diagonal pair to the same extreme in ONE write - the
        // discontinuity FR-034's per-pair smoother has to absorb.
        std::array<std::array<float, FeedbackEcology::kMaxLoops>, FeedbackEcology::kMaxLoops> m{};
        const float v = high ? FeedbackEcology::kMaxCouplingPerPair : 0.0f;
        for (std::size_t from = 0; from < FeedbackEcology::kMaxLoops; ++from) {
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                m[from][to] = (from == to) ? 0.0f : v;
            }
        }
        fe.setCouplingMatrix(m);
        break;
    }
    case JumpSetter::GovernorThresholdDb:
        fe.setGovernorThresholdDb(high ? FeedbackEcology::kMaxGovernorThresholdDb
                                       : FeedbackEcology::kMinGovernorThresholdDb);
        break;
    case JumpSetter::GovernorRatio:
        fe.setGovernorRatio(high ? FeedbackEcology::kMaxGovernorRatio
                                 : FeedbackEcology::kMinGovernorRatio);
        break;
    case JumpSetter::LoopDelayWander:
        fe.setLoopDelayWander(loop, high ? FeedbackEcology::kMaxDelayWanderFraction : 0.0f);
        break;
    case JumpSetter::LoopCutoffWander:
        fe.setLoopCutoffWander(loop, high ? FeedbackEcology::kMaxCutoffWanderOctaves : 0.0f);
        break;
    case JumpSetter::LoopWake:
        fe.setLoopWake(loop, high ? 1.0f : 0.0f);
        break;
    case JumpSetter::LoopDormant:
        fe.setLoopDormant(loop, high);
        break;
    case JumpSetter::NumLoops:
        fe.setNumLoops(high ? FeedbackEcology::kMaxLoops : std::size_t{1});
        break;
    case JumpSetter::Mix:
        fe.setMix(high ? 1.0f : 0.0f);
        break;
    case JumpSetter::WetGain:
        // [-24, 0] dB, for arm (c)'s reason: above +4.27 dB the trim can
        // legitimately reach the FR-046 clamp and this arm asserts it never does.
        fe.setWetGain(high ? 0.0f : FeedbackEcology::kMinWetGainDb);
        break;
    case JumpSetter::WanderRate:
        fe.setWanderRate(high ? FeedbackEcology::kMaxWanderRateHz
                              : FeedbackEcology::kMinWanderRateHz);
        break;
    case JumpSetter::LoopFilterMode:
        fe.setLoopFilterMode(loop, high ? FeedbackEcology::FilterMode::Highpass
                                        : FeedbackEcology::FilterMode::Lowpass);
        break;
    case JumpSetter::LoopResonanceHz:
        fe.setLoopResonanceHz(loop, high ? maxResonanceHz : Krate::DSP::kMinResonatorFrequency);
        break;
    case JumpSetter::LoopResonanceRt60:
        fe.setLoopResonanceRt60(loop,
                                high ? Krate::DSP::kMaxDecayTime : Krate::DSP::kMinDecayTime);
        break;
    case JumpSetter::Seed:
        fe.setSeed(rng.next());
        break;
    case JumpSetter::WanderEnabled:
        fe.setWanderEnabled(high);
        break;
    case JumpSetter::kCount:
        break;  // never drawn; enumerated so the switch stays exhaustive
    }
}

}  // namespace

// =============================================================================
// SC-001 - bounded output for ANY parameter combination, over 30 minutes
// =============================================================================
// ROADMAP LINE 280: "this is the critical test". THE FOUR BOUNDED ASSERTIONS,
// applied by every arm:
//   1. peak |sample| < kOutputClamp (4.0)
//   2. getClampEngagementCount() == 0
//   3. getNonFiniteResetCount()  == 0
//   4. every sample finite by detail::isFinite
//
// If this criterion cannot be made to pass BY REDUCING GAIN - never by widening
// the clamp, relaxing a threshold or shortening a render - the component is
// wrong.
// =============================================================================
TEST_CASE("FeedbackEcology_BoundednessSoak", "[feedback_ecology][long]") {
    const auto requireBounded = [](const char* arm, const SoakResult& r) {
        INFO(arm << ": peak |sample| " << r.peakAbs << " (ceiling "
                 << FeedbackEcology::kOutputClamp << "), clamp engagements " << r.clampEngagements
                 << ", non-finite resets " << r.nonFiniteResets << ", all finite "
                 << (r.allFinite ? 1 : 0) << ", overall RMS " << rmsDb(r.overallRms) << " dB");
        REQUIRE(r.allFinite);
        REQUIRE(r.peakAbs < FeedbackEcology::kOutputClamp);
        REQUIRE(r.clampEngagements == std::uint32_t{0});
        REQUIRE(r.nonFiniteResets == std::uint32_t{0});
    };

    // -----------------------------------------------------------------------
    // (a) The worst-case corner, wander ON throughout, governor OFF.
    // -----------------------------------------------------------------------
    // THE SPEC FIXTURE, WITH THE SPEC ASSERTION ON IT. spec.md SC-001 (a) names
    // "wander at kMaxWanderRateHz with maximum depths, governor at ratio = 1",
    // and puts the ">= 60 dB of tail decay" assertion on THAT fixture. An
    // earlier revision of this file split the arm in two and moved the decay
    // assertion onto a NEW wander-off fixture, leaving this one reporting the
    // figure without gating it - so the fixture the criterion names was asserted
    // nowhere. That split was never recorded in the spec and is undone here.
    //
    // The arithmetic that motivated the split is real and is kept, in
    // DERIVATION TABLE 4 and in the (a2) banner: inside a crossfade window the
    // equal-power two-tap blend can contribute up to sqrt(2) (+3.010 dB), so the
    // worst-case INSTANTANEOUS round-trip gain is
    // 0.95 * 1.41421 * 1.000654 = 1.34438 > 1 and rung 1 alone does not bound
    // that instant - rungs 2-5 do. What it does NOT imply is that the 30-minute
    // TAIL fails to decay: crossfades are transient and decorrelating, the
    // measured decay here is the same 549.0 dB as on the wander-off fixture, and
    // the criterion asks about the tail, not about one crossfade window. (a2)
    // remains below as an ADDITIONAL arm that isolates the two.
    SECTION("(a) worst-case corner, wander on, governor off - bounded, decaying") {
        constexpr double kMinTailDecayDb = 60.0;

        FeedbackEcology fe;
        makeWorstCaseCorner(fe, kSoakFs, FeedbackEcology::kMinGovernorRatio);
        REQUIRE(fe.getGovernorRatio() == FeedbackEcology::kMinGovernorRatio);  // governor OFF
        REQUIRE(fe.isWanderEnabled());
        REQUIRE(fe.getWanderRate() == FeedbackEcology::kMaxWanderRateHz);

        const SoakResult r = renderSoak(fe, kSoakSamples, kMinuteSamples, kMinuteSamples,
                                        /*disableWanderAtCutoff=*/false);
        REQUIRE(r.windowRms.size() == kSoakWindows);

        const double driveDb = rmsDb(r.windowRms.front());
        const double tailDb  = rmsDb(r.windowRms.back());
        {
            std::ostringstream os;
            os << "\nSC-001 (a) worst-case corner, 30 min at " << kSoakFs
               << " Hz, wander on at kMaxWanderRateHz, governor ratio 1 (off)\n"
               << "  60 s of drive, then 29 min of silence\n"
               << "  RMS of the 60 s ending at cut-off = " << driveDb << " dB\n"
               << "  RMS of the final 60 s             = " << tailDb << " dB\n"
               << "  measured decay across the tail    = " << (driveDb - tailDb)
               << " dB  (floor " << kMinTailDecayDb << " dB)\n"
               << "  peak |sample| " << r.peakAbs << ", clamps " << r.clampEngagements
               << ", non-finite resets " << r.nonFiniteResets << "\n";
            WARN(os.str());
        }

        requireBounded("(a)", r);

        // Anti-vacuity: the drive window must actually have been excited, or
        // "non-divergence" is a statement about two silences.
        INFO("(a) drive-window RMS " << driveDb << " dB must be non-zero");
        REQUIRE(r.windowRms.front() > 0.0);

        // Non-divergence: the tail is not LOUDER than the driven window.
        INFO("(a) non-divergence: final-60 s RMS "
             << tailDb << " dB must not exceed the cut-off window's " << driveDb << " dB");
        REQUIRE(r.windowRms.back() <= r.windowRms.front());

        // THE DECAY ASSERTION THE SPEC PUTS ON THIS FIXTURE (spec.md SC-001 (a),
        // "the RMS of the final 60 s is at least 60 dB below the RMS of the 60 s
        // ending at input cut-off - i.e. it decays, which is FR-041's contraction
        // under test rather than merely does not explode"). It belongs HERE, on
        // the wander-ON corner the criterion names, and an earlier revision of
        // this file moved it onto the (a2) fixture instead and left this arm
        // reporting the figure without gating it - an unrecorded amendment that
        // left the spec's own fixture asserted nowhere. (a2) is KEPT below, as an
        // additional arm that isolates the contraction from the crossfade, not as
        // a replacement for this one.
        INFO("(a) tail decay " << (driveDb - tailDb) << " dB must be at least " << kMinTailDecayDb
                               << " dB");
        REQUIRE(tailDb <= driveDb - kMinTailDecayDb);
    }

    // -----------------------------------------------------------------------
    // (a2) The contraction arm.
    // -----------------------------------------------------------------------
    // Identical to (a1) except setWanderEnabled(false) at the input cut-off, so
    // no NEW crossfade fires during the tail and the loop is the strict
    // contraction rung 1 derives (0.950621 per circulation, -0.4399 dB).
    // Margin: the shortest loop circulates 24.39 times/s (-10.73 dB/s, 60 dB in
    // 5.6 s), the longest 2.227 times/s (-0.980 dB/s, 60 dB in 61.2 s); in
    // series with a 30 s resonator RT60 an order-of-magnitude estimate for the
    // composite tail is ~91 s against this arm's 1 740 s - a factor of ~19.
    // THIS ARM IS ADDITIONAL, NOT A RELOCATION. The spec puts its 60 dB decay
    // assertion on the wander-ON corner and that assertion is made there, in (a)
    // above; the measured decay is the same 549.0 dB on both fixtures. What (a2)
    // adds is the fixture where rung 1's argument is operative WITHOUT a
    // crossfade running - the sqrt(2) two-tap excursion DERIVATION TABLE 4
    // records is absent here, so a regression that only shows up mid-crossfade
    // separates the two arms instead of hiding inside one.
    SECTION("(a2) contraction arm - wander off at cut-off, 60 dB of decay") {
        constexpr double kMinTailDecayDb = 60.0;

        FeedbackEcology fe;
        makeWorstCaseCorner(fe, kSoakFs, FeedbackEcology::kMinGovernorRatio);
        REQUIRE(fe.isWanderEnabled());

        const SoakResult r = renderSoak(fe, kSoakSamples, kMinuteSamples, kMinuteSamples,
                                        /*disableWanderAtCutoff=*/true);
        REQUIRE(r.windowRms.size() == kSoakWindows);

        // Anti-vacuity: the wander really was turned off at the cut-off, so the
        // arm measured the fixture it claims to.
        REQUIRE_FALSE(fe.isWanderEnabled());

        const double driveDb = rmsDb(r.windowRms.front());
        const double tailDb  = rmsDb(r.windowRms.back());
        {
            std::ostringstream os;
            os << "\nSC-001 (a2) contraction arm, 30 min at " << kSoakFs
               << " Hz, wander disabled at the input cut-off, governor off\n"
               << "  RMS of the 60 s ending at cut-off = " << driveDb << " dB\n"
               << "  RMS of the final 60 s             = " << tailDb << " dB\n"
               << "  measured decay across the tail    = " << (driveDb - tailDb) << " dB  (floor "
               << kMinTailDecayDb << " dB)\n"
               << "  peak |sample| " << r.peakAbs << ", clamps " << r.clampEngagements
               << ", non-finite resets " << r.nonFiniteResets << "\n";
            WARN(os.str());
        }

        requireBounded("(a2)", r);

        INFO("(a2) drive-window RMS " << driveDb << " dB must be non-zero");
        REQUIRE(r.windowRms.front() > 0.0);

        INFO("(a2) tail decay " << (driveDb - tailDb) << " dB must be at least " << kMinTailDecayDb
                                << " dB");
        REQUIRE(tailDb <= driveDb - kMinTailDecayDb);
    }

    // -----------------------------------------------------------------------
    // (b) Sustained drive, 30 minutes, governor ON.
    // -----------------------------------------------------------------------
    SECTION("(b) sustained drive, 30 min, governor on - bounded and stationary") {
        constexpr double kStationarityBandDb = 1.5;

        FeedbackEcology fe;
        makeWorstCaseCorner(fe, kSoakFs, FeedbackEcology::kDefaultGovernorRatio);
        REQUIRE(fe.getGovernorRatio() == FeedbackEcology::kDefaultGovernorRatio);
        REQUIRE(fe.getGovernorThresholdDb() == FeedbackEcology::kDefaultGovernorThresholdDb);

        const SoakResult r = renderSoak(fe, kSoakSamples, kSoakSamples, kMinuteSamples,
                                        /*disableWanderAtCutoff=*/false);
        REQUIRE(r.windowRms.size() == kSoakWindows);

        std::vector<double> sorted = r.windowRms;
        std::sort(sorted.begin(), sorted.end());
        const double median   = 0.5 * (sorted[kSoakWindows / 2 - 1] + sorted[kSoakWindows / 2]);
        const double medianDb = rmsDb(median);

        double      worstDeviationDb = 0.0;
        std::size_t worstWindow      = 0;
        for (std::size_t w = 0; w < r.windowRms.size(); ++w) {
            const double d = std::abs(rmsDb(r.windowRms[w]) - medianDb);
            if (d > worstDeviationDb) {
                worstDeviationDb = d;
                worstWindow      = w;
            }
        }

        {
            std::ostringstream os;
            os << "\nSC-001 (b) sustained drive, 30 min at " << kSoakFs
               << " Hz, worst-case corner, governor at defaults\n"
               << "  median window RMS = " << medianDb << " dB\n"
               << "  worst deviation   = " << worstDeviationDb << " dB at window " << worstWindow
               << "  (band +/-" << kStationarityBandDb << " dB)\n"
               << "  peak |sample| " << r.peakAbs << ", clamps " << r.clampEngagements
               << ", non-finite resets " << r.nonFiniteResets << "\n"
               << "  per-minute RMS (dB):";
            for (const double w : r.windowRms) {
                os << " " << rmsDb(w);
            }
            os << "\n";
            WARN(os.str());
        }

        requireBounded("(b)", r);

        // Anti-vacuity: a silent render is trivially stationary.
        INFO("(b) median window RMS " << medianDb << " dB must be non-zero");
        REQUIRE(median > 0.0);

        for (std::size_t w = 0; w < r.windowRms.size(); ++w) {
            INFO("(b) window " << w << " RMS " << rmsDb(r.windowRms[w]) << " dB vs median "
                               << medianDb << " dB");
            REQUIRE(std::abs(rmsDb(r.windowRms[w]) - medianDb) <= kStationarityBandDb);
        }
    }

    // -----------------------------------------------------------------------
    // (c) The randomised sweep, accelerated.
    // -----------------------------------------------------------------------
    SECTION("(c) 256 randomised configurations, 60 s each - bounded") {
        float       worstPeak      = 0.0f;
        std::size_t worstPeakIndex = 0;

        for (std::size_t k = 0; k < kSweepConfigCount; ++k) {
            FeedbackEcology fe;
            applyRandomConfig(fe, kSoakFs, k);

            const SoakResult r =
                renderSoak(fe, kSweepRenderTotal, kSweepRenderDrive, kSweepRenderTotal,
                           /*disableWanderAtCutoff=*/false,
                           kReferenceDriveSeed + static_cast<std::uint32_t>(k));

            if (r.peakAbs > worstPeak) {
                worstPeak      = r.peakAbs;
                worstPeakIndex = k;
            }

            INFO("(c) configuration " << k << ": numLoops " << fe.getNumLoops() << ", mix "
                                      << fe.getMix() << ", wetGain " << fe.getWetGain()
                                      << " dB, wanderRate " << fe.getWanderRate()
                                      << " Hz, governor ratio " << fe.getGovernorRatio()
                                      << "; peak |sample| " << r.peakAbs << ", clamps "
                                      << r.clampEngagements << ", non-finite resets "
                                      << r.nonFiniteResets);
            REQUIRE(r.allFinite);
            REQUIRE(r.peakAbs < FeedbackEcology::kOutputClamp);
            REQUIRE(r.clampEngagements == std::uint32_t{0});
            REQUIRE(r.nonFiniteResets == std::uint32_t{0});
        }

        std::ostringstream os;
        os << "\nSC-001 (c) randomised sweep: " << kSweepConfigCount << " configurations x "
           << (static_cast<double>(kSweepRenderTotal) / kSoakFs) << " s at " << kSoakFs
           << " Hz, drive on for the first " << (static_cast<double>(kSweepRenderDrive) / kSoakFs)
           << " s\n"
           << "  worst peak |sample| = " << worstPeak << " at configuration " << worstPeakIndex
           << "  (ceiling " << FeedbackEcology::kOutputClamp << ")\n";
        WARN(os.str());
    }

    // -----------------------------------------------------------------------
    // (d) The parameter-jump arm.
    // -----------------------------------------------------------------------
    // Reference patch WITH every off-diagonal pair pre-seeded at
    // kMaxCouplingPerPair (not the default 0.04 ring), so every setLoopDormant
    // and setNumLoops jump is drawn against the worst-case fixture for FR-031's
    // post-gate construction: on the 0.04 ring an instantly-deleted coupling
    // coefficient is a -28 dB event the detector can miss, and at 0.5 it is the
    // discontinuity this arm exists to catch.
    SECTION("(d) parameter jumps every 500 ms for 5 minutes - bounded and click-free") {
        constexpr std::size_t kJumpIntervalSamples = 24000;  ///< 500 ms at kSoakFs
        constexpr std::size_t kJumpCount           = 600;    ///< 5 minutes
        /// A click landing exactly on a segment boundary needs a predecessor
        /// sample to be a first difference at all, so each analysis buffer
        /// carries the previous segment's tail. Hits INSIDE that lead-in belong
        /// to the PREVIOUS jump and are counted there, never here - an earlier
        /// revision attributed them to the current jump and double-counted them.
        constexpr std::size_t kJumpLeadIn = 1024;
        /// THE WINDOW THE SPEC'S CLICK CLAUSE NAMES. spec.md SC-001 (d) says
        /// "no click above the SC-003 threshold AT ANY JUMP INSTANT", so the
        /// gated window is the span in which a setter's own declared FR-076
        /// smoothing is still running: the longest of those is kGainRampMs
        /// (50 ms, gates / normGain / setNumLoops) and a delay retarget can add
        /// one kCrossfadeMs (20 ms) on top, so the window is
        /// (kGainRampMs + kCrossfadeMs) = 70 ms = 3360 samples at kSoakFs.
        /// DERIVED, not chosen: nothing a setter declares smoothed is still
        /// moving after it. Hits LATER in the 500 ms segment are reported, not
        /// gated - they are the render's own doing, not the setter's, and an
        /// earlier revision gated on them and so measured the wrong thing.
        const auto kAtJumpSamples = static_cast<std::size_t>(
            (FeedbackEcology::kGainRampMs + FeedbackEcology::kCrossfadeMs) * 0.001 * kSoakFs);

        /// TWO PASSES OVER THE SAME JUMP SCHEDULE, and the reason is a measured
        /// property of the instrument rather than convenience.
        ///
        /// ClickDetector is a RELATIVE outlier detector: within each 512-sample
        /// frame it thresholds |first difference| at mean + 5 sigma of that
        /// frame's own |first difference| (artifact_detection.h:186-196). Under a
        /// BROADBAND drive at mix < 1 the output is dry noise, whose first
        /// differences are triangular and bounded - the detector cannot fire
        /// inside a settled frame. But a setMix jump RAMPS that noise in or out
        /// over kMixRampMs, so a frame straddling the ramp contains a quiet half
        /// and a loud half and the loud half's samples clear the quiet half's
        /// 5 sigma. Measured this session: setMix(1 -> 0) on this fixture fires
        /// the detector at offsets 103 and 119 with amplitudes 0.070 and -0.092,
        /// and the surrounding samples show NO step - they are the drive's own
        /// noise at (1 - m) = 0.11. The identical hits, at the identical offsets
        /// and amplitudes, appear with the coupling matrix zeroed, which places
        /// them in the dry path and not in the feedback network. spec.md SC-006
        /// (e) records the same hazard in its own words ("a window that includes
        /// the step sample fires the detector on the fixture rather than on the
        /// component").
        ///
        /// So the CLICK clause runs on a pass driven by SC-003's smooth carrier -
        /// a 110 Hz sine at -12 dBFS, where the dry path carries no broadband
        /// noise for a ramp to modulate and every first-difference outlier in the
        /// output is the COMPONENT's. The four BOUNDEDNESS assertions run on both
        /// passes, so the broadband drive keeps its worst-case excitation.
        /// The jump schedule is bit-identical between the passes (same RNG seed,
        /// same order), so the two are measuring the same 600 jumps.
        /// SC-003 carrier, restated: 110 Hz at -12 dBFS peak.
        constexpr double kClickSineHz    = 110.0;
        constexpr double kClickSineAmp   = 0.251189;
        constexpr double kClickSineTwoPi = 2.0 * static_cast<double>(Krate::DSP::kPi);

        enum class DrivePass : std::uint8_t { Broadband, Sine };

        struct JumpPassResult {
            float       peakAbs         = 0.0f;
            bool        allFinite       = true;
            std::size_t gatedViolations = 0;   ///< hits inside the jump window
            std::size_t laterHits       = 0;   ///< hits later in the segment
            std::size_t leadInHits      = 0;   ///< hits in the previous segment's tail
            std::size_t firstBadJump    = 0;
            JumpSetter  firstBadSetter  = JumpSetter::kCount;
            std::uint32_t clamps        = 0;
            std::uint32_t resets        = 0;
            std::array<std::size_t, kJumpSetterCount> jumpsBySetter{};
            std::array<std::size_t, kJumpSetterCount> violationsBySetter{};
            std::vector<std::string>                  detail;
        };

        const auto runPass = [&](DrivePass which) {
            JumpPassResult out;

            FeedbackEcology fe;
            fe.setSeed(kPatchSeed);
            fe.prepare(kSoakFs, FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlock,
                                                               .numLoops       = kLoops});
            fe.setMix(1.0f);
            fe.setWetGain(0.0f);
            for (std::size_t from = 0; from < kLoops; ++from) {
                for (std::size_t to = 0; to < kLoops; ++to) {
                    if (from != to) fe.setCoupling(from, to, FeedbackEcology::kMaxCouplingPerPair);
                }
            }
            fe.reset();

            // Anti-vacuity: the pre-seeding really is in force from sample 0, not
            // gliding up from FR-033's 0.04 ring while the first jumps land.
            REQUIRE(fe.getCoupling(0, 1) == FeedbackEcology::kMaxCouplingPerPair);
            REQUIRE(fe.getLoopAppliedCoupling(0, 1) > 0.0f);

            // The detector, at a config whose sampleRate is the RENDER rate: the
            // struct's default is 44100.0f (artifact_detection.h:38). Everything
            // else is SC-003's threshold, unchanged.
            Krate::DSP::TestUtils::ClickDetectorConfig cfg;
            cfg.sampleRate = static_cast<float>(kSoakFs);
            REQUIRE(cfg.isValid());
            Krate::DSP::TestUtils::ClickDetector detector(cfg);
            detector.prepare();

            Krate::DSP::Xorshift32 jumpRng(0x0BADBEEFu);
            ReferenceDrive         drive(kReferenceDriveSeed);

            // SC-003's carrier, restated here rather than shared, because this
            // pass needs its own phase accumulator across 600 segments.
            double       sinePhase = 0.0;
            const double sineInc   = 2.0 * static_cast<double>(Krate::DSP::kPi) * kClickSineHz / kSoakFs;

            std::vector<float> inL(kBlock, 0.0f);
            std::vector<float> inR(kBlock, 0.0f);
            std::vector<float> outL(kBlock, 0.0f);
            std::vector<float> outR(kBlock, 0.0f);

            std::vector<float> segment;
            segment.reserve(kJumpLeadIn + kJumpIntervalSamples);
            std::vector<float> carry;

            for (std::size_t j = 0; j < kJumpCount; ++j) {
                const auto jw = static_cast<JumpSetter>(
                    jumpRng.next() % static_cast<std::uint32_t>(kJumpSetterCount));
                ++out.jumpsBySetter[static_cast<std::size_t>(jw)];
                applyJump(fe, jw, jumpRng, kSoakFs);

                segment.clear();
                segment.insert(segment.end(), carry.begin(), carry.end());

                std::size_t rendered = 0;
                while (rendered < kJumpIntervalSamples) {
                    const std::size_t n = std::min(kBlock, kJumpIntervalSamples - rendered);
                    if (which == DrivePass::Broadband) {
                        drive.fill(inL.data(), inR.data(), n);
                    } else {
                        for (std::size_t s = 0; s < n; ++s) {
                            const auto v = static_cast<float>(kClickSineAmp * std::sin(sinePhase));
                            inL[s]       = v;
                            inR[s]       = v;
                            sinePhase += sineInc;
                            if (sinePhase >= kClickSineTwoPi) sinePhase -= kClickSineTwoPi;
                        }
                    }
                    fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
                    for (std::size_t s = 0; s < n; ++s) {
                        const float l = outL[s];
                        const float r = outR[s];
                        if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(r)) {
                            out.allFinite = false;
                        }
                        const float a = std::max(std::abs(l), std::abs(r));
                        if (a > out.peakAbs) out.peakAbs = a;
                    }
                    segment.insert(segment.end(), outL.data(), outL.data() + n);
                    rendered += n;
                }

                const std::vector<Krate::DSP::TestUtils::ClickDetection> hits =
                    detector.detect(segment.data(), segment.size());
                for (const Krate::DSP::TestUtils::ClickDetection& h : hits) {
                    const auto off = static_cast<std::ptrdiff_t>(h.sampleIndex)
                                     - static_cast<std::ptrdiff_t>(carry.size());
                    if (off < 0) {
                        // The previous segment's tail: that jump's business, and
                        // it was already offered to the detector there.
                        ++out.leadInHits;
                        continue;
                    }
                    if (std::cmp_greater(off, kAtJumpSamples)) {
                        ++out.laterHits;
                        continue;
                    }
                    if (isClickExempt(jw)) continue;
                    if (out.gatedViolations == 0) {
                        out.firstBadJump   = j;
                        out.firstBadSetter = jw;
                    }
                    ++out.gatedViolations;
                    ++out.violationsBySetter[static_cast<std::size_t>(jw)];
                    if (out.detail.size() < 24) {
                        std::ostringstream d;
                        d << "      jump " << j << " "
                          << kJumpSetterNames[static_cast<std::size_t>(jw)] << " offset " << off
                          << " amp " << h.amplitude;
                        out.detail.push_back(d.str());
                    }
                }

                const std::size_t keep = std::min(kJumpLeadIn, segment.size());
                carry.assign(segment.end() - static_cast<std::ptrdiff_t>(keep), segment.end());
            }

            out.clamps = fe.getClampEngagementCount();
            out.resets = fe.getNonFiniteResetCount();
            return out;
        };

        const JumpPassResult broadband = runPass(DrivePass::Broadband);
        const JumpPassResult sine      = runPass(DrivePass::Sine);

        const auto report = [&](const char* name, const JumpPassResult& r) {
            std::ostringstream os;
            os << "\nSC-001 (d) parameter-jump arm, " << name << " pass: " << kJumpCount
               << " jumps, one every " << (static_cast<double>(kJumpIntervalSamples) / kSoakFs)
               << " s over " << (static_cast<double>(kJumpCount * kJumpIntervalSamples) / kSoakFs)
               << " s at " << kSoakFs << " Hz, every off-diagonal pair pre-seeded at "
               << FeedbackEcology::kMaxCouplingPerPair << "\n"
               << "  peak |sample| " << r.peakAbs << " (ceiling " << FeedbackEcology::kOutputClamp
               << "), clamps " << r.clamps << ", non-finite resets " << r.resets << "\n"
               << "  click detections inside the " << kAtJumpSamples
               << "-sample jump window, non-exempt setters = " << r.gatedViolations << "\n"
               << "  reported, NOT gated: later in the segment " << r.laterHits
               << ", inside the previous segment's lead-in " << r.leadInHits << "\n"
               << "  jumps per setter:\n";
            for (std::size_t s = 0; s < kJumpSetterCount; ++s) {
                os << "    " << kJumpSetterNames[s] << "  " << r.jumpsBySetter[s]
                   << (isClickExempt(static_cast<JumpSetter>(s)) ? "   [click-exempt]" : "")
                   << "   violations " << r.violationsBySetter[s] << "\n";
            }
            for (const std::string& d : r.detail) os << d << "\n";
            WARN(os.str());
        };
        report("broadband", broadband);
        report("110 Hz sine", sine);

        // The four bounded assertions apply to EVERY setter without exception,
        // on BOTH passes.
        for (const auto& pr : {std::cref(broadband), std::cref(sine)}) {
            const JumpPassResult& r = pr.get();
            INFO("(d) peak |sample| " << r.peakAbs << ", clamps " << r.clamps
                                      << ", non-finite resets " << r.resets);
            REQUIRE(r.allFinite);
            REQUIRE(r.peakAbs < FeedbackEcology::kOutputClamp);
            REQUIRE(r.clamps == std::uint32_t{0});
            REQUIRE(r.resets == std::uint32_t{0});

            // Anti-vacuity: the render was audible, and every setter in the table
            // really was jumped - otherwise the arm silently narrows to whatever
            // the RNG happened to pick.
            REQUIRE(r.peakAbs > 0.0f);
            for (std::size_t s = 0; s < kJumpSetterCount; ++s) {
                INFO("(d) " << kJumpSetterNames[s] << " was jumped " << r.jumpsBySetter[s]
                            << " times");
                REQUIRE(r.jumpsBySetter[s] > 0);
            }
        }
        // The two passes really did run the SAME schedule.
        REQUIRE(broadband.jumpsBySetter == sine.jumpsBySetter);

        // The click assertion, on the smooth-carrier pass, on every setter that is
        // NOT one of the five named exemptions, inside the window the spec's
        // "at any jump instant" names.
        INFO("(d) " << sine.gatedViolations
                    << " click detections on non-exempt setters; first at jump "
                    << sine.firstBadJump << " ("
                    << (sine.firstBadSetter == JumpSetter::kCount
                            ? "none"
                            : kJumpSetterNames[static_cast<std::size_t>(sine.firstBadSetter)])
                    << ")");
        REQUIRE(sine.gatedViolations == std::size_t{0});
    }
}

// =============================================================================
// SC-021 - thirty minutes of evolution: never static, never divergent
// =============================================================================
// Roadmap lines 29-30 ("Nothing repeats exactly") and lines 94-95 (the overnight
// requirement), at the length a test can afford. Arm (d) DELIBERATELY overlaps
// SC-001 (b)'s stationarity clause - that one runs on the worst-case corner with
// the governor on, this one on the reference patch under slow excitation, and a
// drift that appears under only one of the two is exactly the finding worth
// having.
//
// STREAMING, MANDATORY: 86 400 000 samples are never materialised. The Welch
// power spectrum is folded one 4096-point frame at a time and collapsed to a
// centroid plus eight band energies at every 10-second boundary; the render
// keeps a single 4096-sample frame plus the 180-row metric table.
// =============================================================================

namespace {

constexpr std::size_t kEvoFft           = 4096;
constexpr std::size_t kEvoHop           = 2048;    ///< 50 % overlap
constexpr std::size_t kEvoWindowSamples = 480000;  ///< 10 s at kSoakFs
constexpr std::size_t kEvoWindowCount   = 180;     ///< 30 min / 10 s
constexpr std::size_t kEvoBands         = 8;
static_assert(kEvoWindowCount * kEvoWindowSamples == kSoakSamples,
              "SC-021's 180 ten-second windows must tile the 30-minute render exactly");

/// SC-021's excitation after the first 60 s of flat reference drive: the same
/// white noise, amplitude-modulated by a unipolar 0.05 Hz raised cosine (a 20 s
/// period). This is the operational reading of the criterion's "slow 0.05 Hz
/// noise excitation" - the rate is the criterion's, the raised-cosine shape is
/// this test's stated choice, recorded here rather than left for a second
/// implementer to guess differently.
constexpr double kEvoExcitationHz = 0.05;
constexpr double kEvoTwoPi        = 2.0 * static_cast<double>(Krate::DSP::kPi);

/// The eight octave bands, centres 31.25 * 2^k Hz for k = 0..7 (31.25 Hz to
/// 4 kHz), edges at centre / sqrt(2) and centre * sqrt(2). Spelled as literals
/// rather than derived: std::sqrt is not a portable constant expression, and a
/// constexpr derivation that compiles under MSVC breaks the Clang leg.
constexpr std::array<double, kEvoBands + 1> kEvoBandEdgeHz = {
    22.097086912079612,  44.194173824159224,  88.38834764831845,  176.7766952966369,
    353.5533905932738,   707.1067811865476,   1414.2135623730952, 2828.4271247461903,
    5656.854249492381};

/// SC-021's per-window measurement.
struct EvolutionWindow {
    double                          centroidHz = 0.0;
    double                          rms        = 0.0;
    std::array<double, kEvoBands>   logBand{};   ///< natural log of each band energy
    /// (b) the six continuous FR-052 cutoff trajectories (getLoopTargetCutoffHz)
    /// sampled at the window's end - the component's own evolution, independent
    /// of the drive's envelope.
    std::array<float, kLoops>       cutoffHz{};
    bool                            valid      = false;
};

/// A streaming Welch power-spectrum accumulator: samples in one at a time,
/// a centroid and an eight-band energy vector out at every window boundary.
/// Test-only, allocates in prepare(), never on the sample path.
class EvolutionSpectrum {
public:
    void prepare(double sampleRate) {
        fft_.prepare(kEvoFft);
        window_.assign(kEvoFft, 0.0f);
        Krate::DSP::Window::generateHann(window_.data(), kEvoFft);
        frame_.assign(kEvoFft, 0.0f);
        windowed_.assign(kEvoFft, 0.0f);
        spectrum_.assign(fft_.numBins(), Krate::DSP::Complex{});
        power_.assign(fft_.numBins(), 0.0);
        binHz_  = sampleRate / static_cast<double>(kEvoFft);
        fill_   = 0;
        frames_ = 0;
    }

    void push(float s) noexcept {
        frame_[fill_] = s;
        ++fill_;
        if (fill_ < kEvoFft) return;

        for (std::size_t i = 0; i < kEvoFft; ++i) {
            windowed_[i] = frame_[i] * window_[i];
        }
        fft_.forward(windowed_.data(), spectrum_.data());
        for (std::size_t k = 0; k < power_.size(); ++k) {
            const auto re = static_cast<double>(spectrum_[k].real);
            const auto im = static_cast<double>(spectrum_[k].imag);
            power_[k] += re * re + im * im;
        }
        ++frames_;

        // Slide by the hop; the tail of this frame is the head of the next.
        std::copy(frame_.begin() + static_cast<std::ptrdiff_t>(kEvoHop), frame_.end(),
                  frame_.begin());
        fill_ = kEvoFft - kEvoHop;
    }

    /// Collapses everything folded since the last call into a centroid and an
    /// eight-band energy vector, then clears the accumulator. The FRAME buffer
    /// is deliberately NOT cleared: the analysis is continuous across window
    /// boundaries, and a frame that straddles one is attributed to the window it
    /// completes in.
    [[nodiscard]] bool finishWindow(double& centroidHz,
                                    std::array<double, kEvoBands>& bandEnergy) noexcept {
        for (double& e : bandEnergy) {
            e = 0.0;
        }
        centroidHz = 0.0;
        if (frames_ == 0) return false;

        const double inv = 1.0 / static_cast<double>(frames_);
        double       num = 0.0;
        double       den = 0.0;
        // Bin 0 is DC and carries no centroid information; the in-loop DC
        // blocker (FR-014) has removed it anyway.
        for (std::size_t k = 1; k < power_.size(); ++k) {
            const double p = power_[k] * inv;
            const double f = static_cast<double>(k) * binHz_;
            num += f * p;
            den += p;
            for (std::size_t b = 0; b < kEvoBands; ++b) {
                if (f >= kEvoBandEdgeHz[b] && f < kEvoBandEdgeHz[b + 1]) {
                    bandEnergy[b] += p;
                    break;
                }
            }
        }

        std::fill(power_.begin(), power_.end(), 0.0);
        frames_ = 0;

        if (den <= 0.0) return false;
        centroidHz = num / den;
        return true;
    }

private:
    Krate::DSP::FFT                   fft_;
    std::vector<float>                window_;
    std::vector<float>                frame_;
    std::vector<float>                windowed_;
    std::vector<Krate::DSP::Complex>  spectrum_;
    std::vector<double>               power_;
    double                            binHz_  = 0.0;
    std::size_t                       fill_   = 0;
    std::size_t                       frames_ = 0;
};

/// Cosine similarity of two log-band-energy vectors, SC-021 (b)'s distance.
[[nodiscard]] double cosineSimilarity(const std::array<double, kEvoBands>& a,
                                      const std::array<double, kEvoBands>& b) {
    double dot = 0.0;
    double na  = 0.0;
    double nb  = 0.0;
    for (std::size_t k = 0; k < kEvoBands; ++k) {
        dot += a[k] * b[k];
        na += a[k] * a[k];
        nb += b[k] * b[k];
    }
    if (na <= 0.0 || nb <= 0.0) return 0.0;
    return dot / std::sqrt(na * nb);
}

}  // namespace

TEST_CASE("FeedbackEcology_LongEvolution", "[feedback_ecology][long]") {
    constexpr std::size_t kEvoDriveSamples = kMinuteSamples;  ///< 60 s of flat drive
    /// (b) sweeps lags of 15 to 25 minutes, i.e. 90 to 150 ten-second windows.
    constexpr std::size_t kEvoMinLagWindows = 90;
    constexpr std::size_t kEvoMaxLagWindows = 150;
    /// (a) the centroid's coefficient of variation must exceed this.
    constexpr double kEvoMinCentroidCv = 0.03;
    /// (b) two windows "repeat" when all six cutoff trajectories agree within
    /// this relative tolerance (amended 2026-09-13, spec SC-021 (b)). A frozen
    /// or LFO-driven wander repeats exactly at the lag that matches its period;
    /// a bounded random walk never does. The log-band cosine similarity is still
    /// computed and REPORTED: on a 0.05 Hz periodically excited render it sits
    /// at 0.99999 between windows 20 s apart in phase regardless of the
    /// component, so it measures the drive, not the ecology.
    constexpr float kEvoRepeatTolerance = 1.0e-6f;
    /// (c) the centroid stays inside [kEvoCentroidLo, kEvoCentroidHi] x median.
    constexpr double kEvoCentroidLo = 0.5;
    constexpr double kEvoCentroidHi = 2.0;
    /// (d) the least-squares dB trend across the whole render.
    constexpr double kEvoMaxTrendDb = 1.0;
    /// Energies are floored before the log so an empty band cannot produce
    /// -inf. 1e-30 is 300 dB below any level this render can produce.
    constexpr double kEvoEnergyFloor = 1.0e-30;

    // --- the reference patch (tasks.md "The reference patch"): the default
    //     FR-013/FR-022/FR-033/FR-052/FR-053 tables, wander on at
    //     kDefaultWanderRateHz, governor at its defaults, mix = 1 so the
    //     criterion measures the WET path, wetGain = 0 dB, fixed seed.
    FeedbackEcology fe;
    fe.setSeed(kPatchSeed);
    fe.prepare(kSoakFs,
               FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlock, .numLoops = kLoops});
    fe.setMix(1.0f);
    fe.setWetGain(0.0f);
    fe.reset();

    REQUIRE(fe.getNumLoops() == kLoops);
    REQUIRE(fe.isWanderEnabled());
    REQUIRE(fe.getWanderRate() == FeedbackEcology::kDefaultWanderRateHz);
    REQUIRE(fe.getGovernorRatio() == FeedbackEcology::kDefaultGovernorRatio);

    EvolutionSpectrum analyser;
    analyser.prepare(kSoakFs);

    Krate::DSP::Xorshift32 noise(kReferenceDriveSeed);

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    std::vector<EvolutionWindow> windows;
    windows.reserve(kEvoWindowCount);

    double      excitationPhase = 0.0;
    const double excitationInc  = kEvoTwoPi * kEvoExcitationHz / kSoakFs;

    double      windowSumSq = 0.0;
    std::size_t windowFill  = 0;
    bool        allFinite   = true;
    std::size_t done        = 0;

    while (done < kSoakSamples) {
        std::size_t n = std::min(kBlock, kSoakSamples - done);
        if (done < kEvoDriveSamples) n = std::min(n, kEvoDriveSamples - done);
        n = std::min(n, kEvoWindowSamples - windowFill);

        const bool slow = done >= kEvoDriveSamples;
        for (std::size_t s = 0; s < n; ++s) {
            const double env = slow ? (0.5 - 0.5 * std::cos(excitationPhase)) : 1.0;
            const auto   g   = static_cast<float>(env) * kReferenceDriveAmplitude;
            inL[s]           = noise.nextFloat() * g;
            inR[s]           = noise.nextFloat() * g;
            excitationPhase += excitationInc;
            if (excitationPhase >= kEvoTwoPi) excitationPhase -= kEvoTwoPi;
        }

        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);

        for (std::size_t s = 0; s < n; ++s) {
            const float l = outL[s];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(outR[s])) {
                allFinite = false;
            }
            const auto dl = static_cast<double>(l);
            windowSumSq += dl * dl;
            analyser.push(l);
        }

        done += n;
        windowFill += n;
        if (windowFill == kEvoWindowSamples) {
            EvolutionWindow w;
            std::array<double, kEvoBands> bandEnergy{};
            w.valid = analyser.finishWindow(w.centroidHz, bandEnergy);
            w.rms   = std::sqrt(windowSumSq / static_cast<double>(kEvoWindowSamples));
            for (std::size_t b = 0; b < kEvoBands; ++b) {
                w.logBand[b] = std::log(std::max(bandEnergy[b], kEvoEnergyFloor));
            }
            for (std::size_t i = 0; i < kLoops; ++i) {
                w.cutoffHz[i] = fe.getLoopTargetCutoffHz(i);
            }
            windows.push_back(w);
            windowSumSq = 0.0;
            windowFill  = 0;
        }
    }

    // --- measurement validity ------------------------------------------------
    // Not one of the four arms: a render that went non-finite would make every
    // statistic below meaningless, so the arms are worth reading only once this
    // holds.
    REQUIRE(allFinite);
    REQUIRE(fe.getNonFiniteResetCount() == std::uint32_t{0});
    REQUIRE(windows.size() == kEvoWindowCount);
    for (std::size_t w = 0; w < windows.size(); ++w) {
        INFO("window " << w << " produced no measurable spectrum");
        REQUIRE(windows[w].valid);
    }

    // --- (a) not static ------------------------------------------------------
    double centroidSum = 0.0;
    for (const EvolutionWindow& w : windows) {
        centroidSum += w.centroidHz;
    }
    const double centroidMean = centroidSum / static_cast<double>(windows.size());
    double       centroidVar  = 0.0;
    for (const EvolutionWindow& w : windows) {
        const double d = w.centroidHz - centroidMean;
        centroidVar += d * d;
    }
    centroidVar /= static_cast<double>(windows.size());
    const double centroidSd = std::sqrt(centroidVar);
    const double centroidCv = (centroidMean > 0.0) ? (centroidSd / centroidMean) : 0.0;

    // --- (b) not periodic ----------------------------------------------------
    double      maxSimilarity   = -1.0;
    std::size_t maxSimilarityA  = 0;
    std::size_t maxSimilarityB  = 0;
    std::size_t comparedPairs   = 0;
    std::size_t repeatedPairs   = 0;
    std::size_t firstRepeatA    = 0;
    std::size_t firstRepeatB    = 0;
    for (std::size_t lag = kEvoMinLagWindows; lag <= kEvoMaxLagWindows; ++lag) {
        for (std::size_t i = 0; i + lag < windows.size(); ++i) {
            const double c = cosineSimilarity(windows[i].logBand, windows[i + lag].logBand);
            ++comparedPairs;
            if (c > maxSimilarity) {
                maxSimilarity  = c;
                maxSimilarityA = i;
                maxSimilarityB = i + lag;
            }
            bool repeats = true;
            for (std::size_t k = 0; k < kLoops; ++k) {
                const float a = windows[i].cutoffHz[k];
                const float b = windows[i + lag].cutoffHz[k];
                if (std::abs(a - b) > kEvoRepeatTolerance * std::max(std::abs(a), std::abs(b))) {
                    repeats = false;
                    break;
                }
            }
            if (repeats) {
                if (repeatedPairs == 0) {
                    firstRepeatA = i;
                    firstRepeatB = i + lag;
                }
                ++repeatedPairs;
            }
        }
    }

    // --- (c) not divergent ---------------------------------------------------
    std::vector<double> centroidSorted;
    centroidSorted.reserve(windows.size());
    for (const EvolutionWindow& w : windows) {
        centroidSorted.push_back(w.centroidHz);
    }
    std::sort(centroidSorted.begin(), centroidSorted.end());
    const double centroidMedian = 0.5
                                  * (centroidSorted[centroidSorted.size() / 2 - 1]
                                     + centroidSorted[centroidSorted.size() / 2]);

    // --- (d) neither dying nor creeping --------------------------------------
    // Least-squares slope of the window RMS in dB against time in seconds,
    // multiplied by the render length.
    const double windowSeconds = static_cast<double>(kEvoWindowSamples) / kSoakFs;
    const double renderSeconds = static_cast<double>(kSoakSamples) / kSoakFs;
    double       sx            = 0.0;
    double       sy            = 0.0;
    double       sxx           = 0.0;
    double       sxy           = 0.0;
    const auto   nPoints       = static_cast<double>(windows.size());
    for (std::size_t w = 0; w < windows.size(); ++w) {
        const double x = (static_cast<double>(w) + 0.5) * windowSeconds;  // window centre
        const double y = rmsDb(windows[w].rms);
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const double denom     = nPoints * sxx - sx * sx;
    const double slopeDbPerS = (denom != 0.0) ? ((nPoints * sxy - sx * sy) / denom) : 0.0;
    const double trendDb   = slopeDbPerS * renderSeconds;

    {
        std::ostringstream os;
        os << "\nSC-021 long evolution, " << (renderSeconds / 60.0) << " min at " << kSoakFs
           << " Hz, reference patch, " << (static_cast<double>(kEvoDriveSamples) / kSoakFs)
           << " s of flat drive then a " << kEvoExcitationHz
           << " Hz raised-cosine-modulated noise excitation\n"
           << "  windows                       = " << windows.size() << " x " << windowSeconds
           << " s\n"
           << "  (a) centroid mean             = " << centroidMean << " Hz, sd " << centroidSd
           << " Hz, cv " << centroidCv << "  (floor " << kEvoMinCentroidCv << ")\n"
           << "  (b) max cosine similarity     = " << maxSimilarity << " between windows "
           << maxSimilarityA << " and " << maxSimilarityB << " over " << comparedPairs
           << " pairs at lags " << kEvoMinLagWindows << "-" << kEvoMaxLagWindows
           << " windows  (REPORTED; gated clause: " << repeatedPairs
           << " repeating cutoff-vector pair(s), tolerance " << kEvoRepeatTolerance << ")\n"
           << "  (c) centroid median           = " << centroidMedian << " Hz, min "
           << centroidSorted.front() << " Hz, max " << centroidSorted.back() << " Hz  (band ["
           << (kEvoCentroidLo * centroidMedian) << ", " << (kEvoCentroidHi * centroidMedian)
           << "] Hz)\n"
           << "  (d) window-RMS trend          = " << slopeDbPerS << " dB/s x " << renderSeconds
           << " s = " << trendDb << " dB  (band +/-" << kEvoMaxTrendDb << " dB)\n"
           << "  log-band-energy vector of window " << maxSimilarityA << ":";
        for (const double v : windows[maxSimilarityA].logBand) {
            os << " " << v;
        }
        os << "\n  log-band-energy vector of window " << maxSimilarityB << ":";
        for (const double v : windows[maxSimilarityB].logBand) {
            os << " " << v;
        }
        os << "\n";
        WARN(os.str());
    }

    // Anti-vacuity: the render was audible. Every arm below is a statement
    // about a signal, and each of them is satisfiable by silence if this is not
    // checked first (a silent render has a zero centroid, a zero variance and a
    // flat dB trend at the floor).
    REQUIRE(centroidMean > 0.0);
    REQUIRE(centroidMedian > 0.0);
    REQUIRE(rmsDb(windows.front().rms) > -120.0);

    // --- (a) --------------------------------------------------------------
    {
        INFO("(a) centroid sd " << centroidSd << " Hz is " << (100.0 * centroidCv)
                                << " % of the mean " << centroidMean << " Hz; floor "
                                << (100.0 * kEvoMinCentroidCv) << " %");
        REQUIRE(centroidCv > kEvoMinCentroidCv);
    }

    // --- (b) --------------------------------------------------------------
    {
        WARN("(b) REPORTED: max log-band cosine similarity " << maxSimilarity
             << " between windows " << maxSimilarityA << " and " << maxSimilarityB << " over "
             << comparedPairs << " compared pairs (drive-dominated, not gated)");
        INFO("(b) " << repeatedPairs << " window pair(s) at lags 15-25 min repeat all six cutoff "
                    << "trajectories within " << kEvoRepeatTolerance << " relative; first "
                    << firstRepeatA << " / " << firstRepeatB);
        REQUIRE(comparedPairs > std::size_t{0});
        REQUIRE(repeatedPairs == std::size_t{0});
    }

    // --- (c) --------------------------------------------------------------
    for (std::size_t w = 0; w < windows.size(); ++w) {
        INFO("(c) window " << w << " centroid " << windows[w].centroidHz << " Hz vs median "
                           << centroidMedian << " Hz");
        REQUIRE(windows[w].centroidHz >= kEvoCentroidLo * centroidMedian);
        REQUIRE(windows[w].centroidHz <= kEvoCentroidHi * centroidMedian);
    }

    // --- (d) --------------------------------------------------------------
    {
        INFO("(d) least-squares window-RMS slope " << slopeDbPerS << " dB/s over " << renderSeconds
                                                   << " s is " << trendDb << " dB; band +/-"
                                                   << kEvoMaxTrendDb << " dB");
        REQUIRE(std::abs(trendDb) <= kEvoMaxTrendDb);
    }
}


