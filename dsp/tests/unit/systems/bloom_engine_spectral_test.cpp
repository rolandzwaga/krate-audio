// ==============================================================================
// Layer 3: System Tests - BloomEngine spectral / long-run behaviour
// ==============================================================================
// Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom): BloomEngine.
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase7-harmonic-bloom/spec.md
//            specs/vorago-phase7-harmonic-bloom/plan.md  (S10.2, S10.3)
//            specs/vorago-phase7-harmonic-bloom/tasks.md (T001 creates this TU;
//                                                         T009 lands SC-002)
//
// SCOPE OF THIS TU (plan S10.2): SC-001, SC-002 [long], SC-004 [long],
//   SC-010 [long], SC-015 [long] - the rendered / long-run set.
//
// [long] is applied per the roadmap rule: only to cases costing more than ~15 s
//   whose assertions are toolchain-INDEPENDENT. SC-001 is tagged [long] only if
//   its measured runtime says so at implementation time.
//
// This TU is DELIBERATELY NOT in the "-fno-fast-math -fno-finite-math-only"
//   block of dsp/tests/CMakeLists.txt, for the same reason as
//   bloom_engine_test.cpp: the guards must be proved in the shipping FP mode.
//   It may therefore never NAME a non-finite value.
//
// SC-010 landed with T015, SC-004 with T017, SC-015 with T019. Every case in
//   this TU's scope is now present.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

// harmonic_cloud.h is SC-001's CONSUMER (T013): the bloom's output arrays are
// fed to a real HarmonicCloud at the Phase-10 call shape
// (seraphis_voice.h:1050-1054). bloom_engine.h itself includes NEITHER this
// header nor any other Seraphis header (FR-080, D-1) - the coupling is the
// array contract and nothing else, and this TU is where that contract is
// exercised rather than asserted.
#include <krate/dsp/systems/bloom_engine.h>
#include <krate/dsp/systems/harmonic_cloud.h>

// SC-001's metric. ALL SIX ClickDetectorConfig fields are set explicitly by
// countClicksSpan() below: the struct default sampleRate is 44100.0f
// (artifact_detection.h:38) and isValid() only range-checks it, so a wrong
// rate is used SILENTLY.
#include <artifact_detection.h>

// SC-004 (T017) reads its spectral metric from the SHARED helper rather than
// from a local FFT: extractAudioFeatures (tests/test_helpers/audio_features.h:37)
// Welch-averages 2048-point Hann frames at a 1024-sample hop and computes the
// amplitude-weighted centroid at :88 - the exact computation SC-004 names. It is
// called once per RENDERED SECOND over a one-second mono buffer, never over the
// whole 30-minute render (the streaming-statistics rule).
#include <audio_features.h>

#include <algorithm>  // std::max, std::min, std::sort
#include <array>
#include <cmath>    // std::fabs, std::lround, std::exp2
#include <cstddef>
#include <cstdint>
#include <cstring>  // std::memcmp - SC-001's pre-spawn prefix identity
#include <vector>   // SC-002 (c)(ii)'s one bounded analysis window, and
                    // SC-001's two render buffers, which the click detector
                    // has to see whole

// ==============================================================================
// The TU-local fixtures (plan S10.1: NO new test helper header)
// ==============================================================================
// Coded ONCE by the task named against each. Later tasks in this TU
// (T015-T017) REUSE these and do not re-declare them.
//   stepEngine / fillOneParent / fillTwoParents / prepareTriggerOnly  <- T009
//   countClicks / smallestZeroSigma / CloudRig                        <- T013
//
// SC-002 (T009) DELIBERATELY DOES NOT USE CloudRig: it measures the ENVELOPE
// ITSELF through getChildAmplitude(), and putting a HarmonicCloud between the
// measurement and the thing measured would fold the cloud's own 2 ms amplitude
// smoother and its FR-017 normalizer into every shape claim below. SC-001
// (T013) is the opposite case: its subject IS what the cloud renders, so it
// drives the rig.
// ==============================================================================
namespace {

/// The render rate every case in this TU drives. The control grid is therefore
/// 48000 / 64 = 750 control steps per second, exactly - which is what makes
/// every step count below an exact integer rather than a rounded one.
constexpr double kSampleRate = 48000.0;
constexpr double kControlRateHz = 750.0;

constexpr std::size_t kSlots = Krate::DSP::BloomEngine::kMaxSlots;

/// @brief Seconds -> control steps, MIRRORING BloomEngine::fadeSecondsToSteps.
///
/// The engine rounds `seconds * controlRateHz_` and floors the two FADE
/// conversions at 1 step; the hold is floored at 0 instead (FR-030 admits
/// holdSeconds == 0). Every duration this TU configures - 45, 120, 180, 300,
/// 600, 2, 1, 3 seconds - multiplies by 750 to an exact float, so mirroring the
/// conversion in double here reproduces the engine's integer exactly rather than
/// approximately.
[[nodiscard]] std::uint32_t fadeStepsOf(float seconds) noexcept {
    const long steps = std::lround(static_cast<double>(seconds) * kControlRateHz);
    return static_cast<std::uint32_t>(std::max<long>(1L, steps));
}

[[nodiscard]] std::uint32_t holdStepsOf(float seconds) noexcept {
    const long steps = std::lround(static_cast<double>(seconds) * kControlRateHz);
    return static_cast<std::uint32_t>(std::max<long>(0L, steps));
}

/// @brief ONE eligible parent at ratio 1.0 carrying the swept amplitude.
///
/// Every other slot carries the cloud's own padding form (`ratio = i + 1`,
/// `amplitude = 0`, harmonic_cloud.h:825-826), so it is present in the FR-022
/// occupancy set but never an eligible parent (FR-011: an amplitude at or below
/// kSilentParentAmplitude has no harmonics to grow).
void fillOneParent(float* ratios, float* amplitudes, std::size_t n,
                   float parentAmplitude) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    ratios[0] = 1.0f;
    amplitudes[0] = parentAmplitude;
}

/// @brief TWO well-spaced eligible parents, 454 cents apart.
///
/// Used by the arms that need an event to place more than one child: with
/// parents at 1.0 and 1.3 the two octave candidates are 2.0 and 2.6, which are
/// 454 cents apart and at least 372 cents from either parent, so FR-022's
/// 24-cent spacing rule never fires and first-attempt acceptance is certain.
void fillTwoParents(float* ratios, float* amplitudes, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    ratios[0] = 1.0f;
    amplitudes[0] = 1.0f;
    ratios[1] = 1.3f;
    amplitudes[1] = 0.9f;
}

/// @brief The cloud-free driver (tasks.md T009, the shared `stepEngine` fixture
///        for this TU; its twin in bloom_engine_test.cpp is T007's).
///
/// A loop of `processChunk(ratios, amplitudes, parentCount, kControlChunkSamples)`,
/// i.e. the Phase-10 call shape (seraphis_voice.h:1050-1054) with the
/// HarmonicCloud removed. One call is one control step, so `steps` reads as
/// control steps and `steps / 750` as seconds at 48 kHz.
///
/// @return The count the LAST call returned - processChunk is [[nodiscard]] and
///         every return is bound, never discarded (C4834 / -Wunused-result).
std::size_t stepEngine(Krate::DSP::BloomEngine& engine, float* ratios, float* amplitudes,
                       std::size_t parentCount, std::size_t steps) noexcept {
    std::size_t returned = parentCount;
    for (std::size_t s = 0; s < steps; ++s) {
        returned = engine.processChunk(ratios, amplitudes, parentCount,
                                       Krate::DSP::BloomEngine::kControlChunkSamples);
    }
    return returned;
}

/// @brief Configure an engine with the internal FR-041 clock OFF, so
///        triggerBloom() is the only event source.
///
/// Leaving the clock running would let a chance spawn perturb an arm that counts
/// or times children exactly, and at the default rate of one event per four
/// minutes such a failure would be irreproducible. setSeed() before prepare() is
/// safe: prepare()'s step (6) re-applies seed_, which survives (FR-004).
void prepareTriggerOnly(Krate::DSP::BloomEngine& engine, std::uint32_t seed,
                        std::size_t capacity, std::size_t numChildSlots) noexcept {
    using Krate::DSP::BloomEngine;
    engine.setSeed(seed);
    engine.prepare(kSampleRate,
                   BloomEngine::PrepareConfig{.capacity = capacity,
                                              .numChildSlots = numChildSlots});
    engine.setSpawnRateHz(0.0f);
    // SC-002's fixture: childGain and depth are EXPLICIT non-default overrides
    // (kDefaultChildGain is 0.35f, FR-023), so the measured shape is the
    // envelope's and not the gain scalar's. With both at 1 and consumerTiltDb at
    // its 0 default - where tiltGain() takes its verbatim identity branch - the
    // latched target of FR-023 reduces to the parent amplitude exactly.
    engine.setChildGain(1.0f);
    engine.setDepth(1.0f);
    // Octave only: the relationship is irrelevant to every clause of SC-002, and
    // pinning it removes the seeded relation draw from the fixture entirely.
    engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
    engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.0f);
    engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 0.0f);
}

/// @brief The table index of the single live child, or kMaxChildren if the table
///        holds anything other than exactly one.
[[nodiscard]] std::size_t soleLiveChild(const Krate::DSP::BloomEngine& engine) noexcept {
    using Krate::DSP::BloomEngine;
    std::size_t found = BloomEngine::kMaxChildren;
    std::size_t count = 0;
    for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
        if (engine.getChildPhase(i) != BloomEngine::Phase::Idle) {
            found = i;
            ++count;
        }
    }
    return (count == 1) ? found : BloomEngine::kMaxChildren;
}

// ==============================================================================
// T013 - SC-001's THREE fixtures: countClicks / smallestZeroSigma, and CloudRig
// ==============================================================================
// Coded ONCE, here, by T013 (tasks.md "The four shared fixtures" table). A later
// task in this TU (T015-T017) REUSES these and does not re-declare them.
// ==============================================================================

/// The sigma EVERY gate in this TU is evaluated at. It is a fixture constant and
/// not a tunable: the stop-and-surface rule forbids moving a threshold to make a
/// figure fit, and smallestZeroSigma() below exists precisely so that a failure
/// is reported in detector units instead of being fixed by raising this.
constexpr float kClickSigma = 5.0f;

/// SC-001's TIME-COMPRESSED lifecycle, against the 45 / 120 / 180 s defaults.
/// The envelope is a function of u = step / steps (plan S5.3), so its shape is
/// scale-invariant and the DEFAULT durations are the business of SC-002, which
/// measures them at full length. Compressing here is what makes ten seeds x two
/// renders affordable; it is a fixture choice, not a relaxed threshold.
constexpr float kRigFadeInSeconds = 2.0f;
constexpr float kRigHoldSeconds = 1.0f;
constexpr float kRigFadeOutSeconds = 3.0f;

/// @brief SC-001's PINNED click-detector configuration, over a span.
///
/// Transcribed from atmosphere_engine_spectral_test.cpp:399-409. ALL SIX
/// ClickDetectorConfig fields are designated-initialised, in declaration order
/// (artifact_detection.h:38-43), and `.sampleRate` is THE RENDER RATE: the
/// struct default is 44100.0f (:38) and isValid() only range-checks it
/// (:46-63), so a wrong rate is used SILENTLY - the frame statistics would be
/// computed over the wrong time base and the criterion would measure nothing.
///
/// The span form (pointer + length) rather than only the whole-vector form is
/// what the 200 ms windowed clause needs; ClickDetector::detect already takes
/// `(const float*, size_t)` (artifact_detection.h:130), so a window costs no
/// copy and no second buffer.
///
/// EVERY FIELD EXCEPT `detectionThreshold` IS FIXED BY THE SPEC. sigma is a
/// parameter only so smallestZeroSigma() can walk a ladder for the FAILURE
/// REPORT.
[[nodiscard]] std::size_t countClicksSpan(const float* data, std::size_t n, float sigma) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg{.sampleRate =
                                                       static_cast<float>(kSampleRate),
                                                   .frameSize = 512,
                                                   .hopSize = 256,
                                                   .detectionThreshold = sigma,
                                                   .energyThresholdDb = -60.0f,
                                                   .mergeGap = 5};
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(data, n).size();
}

/// @brief The whole-buffer form of countClicksSpan (the fixture table's name).
[[nodiscard]] std::size_t countClicks(const std::vector<float>& buffer, float sigma) {
    return countClicksSpan(buffer.data(), buffer.size(), sigma);
}

/// @brief Smallest sigma on a fixed ladder that yields 0 detections on `buffer`.
///
/// atmosphere_engine_spectral_test.cpp:414-423, verbatim in shape. Used ONLY to
/// build the failure message, so that a verdict is ATTRIBUTABLE: it says how far
/// the two runs sit from each other in detector units instead of leaving a bare
/// "3 > 2". It is never consulted by a passing run and it is never a gate.
/// Returns 0 when no ladder value clears the buffer.
[[nodiscard]] float smallestZeroSigma(const std::vector<float>& buffer) {
    for (int step = 10; step <= 60; ++step) {  // 5.0 .. 30.0 in 0.5 increments
        const float sigma = 0.5f * static_cast<float>(step);
        if (countClicks(buffer, sigma) == 0u) {
            return sigma;
        }
    }
    return 0.0f;
}

// ------------------------------------------------------------------------------
// The CloudRig parent spectrum, and why it is shaped the way it is
// ------------------------------------------------------------------------------
// SC-001 needs a spectrum that (i) keeps the cloud genuinely sounding, so the
// reference render carries the partial-on-partial beating the differential gate
// exists to subtract out, and (ii) leaves the strongest-K parents' octave and
// fifth images CLEAR of every other partial by more than FR-022's 24 cents, so
// that children reliably SPAWN. A fixture in which every candidate is rejected
// would pass SC-001 vacuously while measuring nothing.
//
// The construction: 32 partials on a geometric grid of kRigGridCents = 210
// cents, amplitude 1/(i+1)^2. The amplitude law is strictly descending, so the
// FR-010 strongest-K selection is DETERMINISTIC - it is always parents 0..3 -
// and the child images are therefore known in advance rather than drawn.
//
// WHY THE AMPLITUDE LAW IS 1/(i+1)^2 AND NOT 1/(i+1), which is what this
// fixture carried first and what the SC-001 windowed clause then failed on.
// MEASURED, on the REFERENCE render (numChildSlots = 0, i.e. no bloom in the
// signal at all), at the pinned detector config and sigma 5, over the whole
// 8.5 s render:
//     1/(i+1)  : seed 0x5C001001 -> 9 detections   (first at samples 744,
//                77258, 133145, 144273, 157959, 215180, 323660, 378276, 402982)
//                seed 0x5C001002 -> 7,  seed 0x5C001003 -> 12
//     1/(i+1)^2: 0 detections on every seed tried  (smallestZeroSigma 5.0)
// The cause is CREST FACTOR, and it is arithmetic rather than bad luck: the
// click detector is a DERIVATIVE detector, and a partial's contribution to the
// derivative is a_i * omega_i. Under 1/(i+1) that product is
// 2^(0.175 i)/(i+1) - it RISES across the bank, so all 32 partials contribute
// about equally to the derivative and the sum behaves like 32 equal-amplitude
// sinusoids: wherever their phases line up, one 512-sample frame sees a
// derivative peak past mean + 5 sigma. Those alignments recur roughly once a
// second, so a 200 ms window has about a one-in-five chance of containing one -
// and SC-001 takes about six windows per seed over ten seeds. The reference run
// showed the SAME detection in the SAME window, which is what proves the
// detection was the carrier's and not the bloom's.
// Under 1/(i+1)^2 the product falls as 2^(0.175 i)/(i+1)^2, so the derivative is
// dominated by the low partials and the carrier is quasi-sinusoidal in its
// first difference: no alignment peak reaches 5 sigma.
// This is a FIXTURE choice, exactly as SC-001's own reasoning demands ("the
// fixture - not the threshold - is what needs revisiting"): kClickSigma, the
// detector config, the window length and the 0-detection requirement are all
// unchanged, and the gate is now STRICTER than it was (the reference scores 0,
// so `detections(bloom) <= detections(reference)` demands a spotless bloom
// render rather than merely a quieter-than-carrier one). The RATIOS are
// untouched, so every clearance figure below still holds verbatim.
//
// CLEARANCE, computed over the grid before this fixture was written: the
// minimum cents distance between any of {2*r_i, 1.5*r_i : i in 0..3} and any
// grid partial, and between any two of those images, is 60 cents at a 210-cent
// grid - 2.5x FR-022's 24-cent floor. So an Octave or a Fifth candidate from
// any of the four selected parents is accepted ON ITS FIRST ATTEMPT. A
// DetunedNeighbour candidate sits 24..50 cents off its own parent and is
// accepted too, except in one narrow band (a detuned child of parent 3 against
// a fifth child of parent 0 closes to 22 cents when the cents draw is near
// +50), which the FR-026 retry absorbs. All three relations are therefore LIVE
// in this fixture, which is why the rig re-enables the two that
// prepareTriggerOnly() pins off.
//
// Every image stays inside [kMinChildRatio, kMaxChildRatio] = [0.5, 128]: the
// largest is 2 * r_3 = 2 * 2^(630/1200) = 2.88.
constexpr std::size_t kRigParents = 32;
constexpr float kRigGridCents = 210.0f;
constexpr float kRigFundamentalHz = 110.0f;

/// @brief The SC-001 parent spectrum (see the block comment above).
///
/// Slots at and above kRigParents carry the cloud's OWN padding form
/// (`ratio = i + 1`, `amplitude = 0`, harmonic_cloud.h:825-826), which is bit-
/// identical to what BloomEngine::applyOutput writes into the FR-051 gap - so
/// engaging the bloom changes the cloud's target in the CHILD SLOTS ONLY, which
/// is what makes the pre-spawn prefix identity below a meaningful check.
void fillRigParents(float* ratios, float* amplitudes, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    for (std::size_t i = 0; i < kRigParents && i < n; ++i) {
        ratios[i] = std::exp2(static_cast<float>(i) * kRigGridCents / 1200.0f);
        const float n1 = static_cast<float>(i + 1);
        amplitudes[i] = 1.0f / (n1 * n1);
    }
}

/// @brief HarmonicCloud + BloomEngine at the PHASE-10 CALL SHAPE
///        (seraphis_voice.h:1050-1054), one 64-sample control chunk per call.
///
/// The <= 64-sample slice is a BOUND, not a suggestion (harmonic_cloud.h:735-751):
/// processStereoBlock restarts its internal control grid on every call, so a
/// target supplied once per host block would be frozen for all eight of that
/// block's internal chunks and every fade measured through it would be a
/// staircase.
///
/// THE CLOUD IS DRIVEN AT richness = 1.0 SO activeCount_ == 64
/// (harmonic_cloud.h:1462-1463). Overview fact 1: recalculateAmplitudes() zeroes
/// baseAmplitude_[i] and `continue`s for every i >= activeCount_ BEFORE the
/// spectral-target branch (:1469-1473 against :1492-1494), so a child written at
/// or above the active count is SILENTLY INAUDIBLE and this criterion would be
/// measuring silence. `capacity == kMaxSlots` together with `activeCount_ == 64`
/// is the FR-050 capacity contract satisfied exactly.
struct CloudRig {
    Krate::DSP::HarmonicCloud cloud;
    Krate::DSP::BloomEngine bloom;
    std::array<float, kSlots> ratios{};
    std::array<float, kSlots> amplitudes{};
    std::size_t parentCount = kRigParents;

    /// @param seed          Drives BOTH the engine stream and the cloud's own
    ///                      drift / pan / phase draws, so the ten seeds of
    ///                      SC-001 are ten genuinely different renders and not
    ///                      ten different blooms over one fixed carrier.
    /// @param capacity      The engine's write ceiling; SC-001 uses kMaxSlots.
    /// @param numChildSlots 0 builds the REFERENCE rig: the engine never
    ///                      engages, processChunk is a pass-through and the
    ///                      cloud sees exactly the parent spectrum.
    void prepare(std::uint32_t seed, std::size_t capacity, std::size_t numChildSlots) noexcept {
        using Krate::DSP::BloomEngine;

        cloud.prepare(kSampleRate);
        cloud.setFundamentalHz(kRigFundamentalHz);
        cloud.setRichness(1.0f);
        cloud.setSeed(seed);
        // noteOn() flushes the deferred amplitude recompute while quiescent
        // (harmonic_cloud.h:635-660), which is what makes activeCount_ readable
        // as 64 before the first render rather than one chunk later.
        cloud.noteOn();

        fillRigParents(ratios.data(), amplitudes.data(), kSlots);

        // The internal FR-041 clock stays OFF: SC-001's windows are taken at
        // KNOWN instants, and a chance clock spawn at the default
        // one-per-four-minutes rate would make a failure irreproducible.
        prepareTriggerOnly(bloom, seed, capacity, numChildSlots);
        // prepareTriggerOnly pins Octave-only for SC-002. SC-001 wants all three
        // relations live - a detuned child is the one whose ratio does NOT sit
        // on a consonance of an existing partial, i.e. the one most likely to
        // beat audibly against its parent as it fades in.
        bloom.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
        bloom.setRelationWeight(BloomEngine::Relation::Fifth, 1.0f);
        bloom.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 1.0f);
        bloom.setFadeInSeconds(kRigFadeInSeconds);
        bloom.setHoldSeconds(kRigHoldSeconds);
        bloom.setFadeOutSeconds(kRigFadeOutSeconds);
    }

    /// @brief One 64-sample control chunk: bloom -> setSpectralTarget -> render.
    /// @return The count handed to setSpectralTarget. processChunk is
    ///         [[nodiscard]], so the return is BOUND and never discarded
    ///         (C4834 / -Wunused-result under the zero-warning rule).
    std::size_t chunk(float* left, float* right) noexcept {
        using Krate::DSP::BloomEngine;
        const std::size_t returned =
            bloom.processChunk(ratios.data(), amplitudes.data(), parentCount,
                               BloomEngine::kControlChunkSamples);
        cloud.setSpectralTarget(ratios.data(), amplitudes.data(), returned);
        cloud.processStereoBlock(left, right, BloomEngine::kControlChunkSamples);
        return returned;
    }
};

}  // namespace

// ==============================================================================
// T009 - BloomEngine_LifecycleTimingAndC1Shape (SC-002)
// ==============================================================================
// FR-030/FR-031/FR-033/FR-036: each child runs a 45 s fade-in -> minutes-scale
// hold -> 3 min fade-out on an INTEGER control-step clock, with a C1 amplitude
// envelope whose value AND first derivative are zero at both ends, whose hold
// duration carries a seeded jitter, and whose three step bounds are LATCHED at
// spawn so a setter moved mid-flight cannot retime a sounding child.
//
// THE SERIES IS READ THROUGH getChildAmplitude(), NOT THROUGH A RENDER. Putting
// a HarmonicCloud between the envelope and the measurement would fold the
// cloud's 2 ms amplitude smoother (harmonic_cloud.h:165) and its FR-017
// normalizer into every shape claim; SC-006's consumer arm and SC-001's click
// gate are where the cloud belongs.
//
// STREAMING STATISTICS. The default lifecycle is 45 + 120 + 180 = 345 s =
// 258 750 control steps, swept over four parent amplitudes. Clauses (a), (b),
// (c)(i) and (d) are therefore accumulated INCREMENTALLY inside the single pass
// - crossing indices, per-segment max |d|, the two 2 % window sums, plateau run
// lengths and monotonicity counters - and nothing longer than one value is held.
// The one bounded window this TU materialises is clause (c)(ii)'s 4 501-sample
// compressed series, and the reason it must be compressed is measured below.
//
// WHY (c)(ii) IS MEASURED AT COMPRESSED FADES, AND WHY THAT IS NOT A RELAXED
// THRESHOLD. At the 45 s default the true second difference at the FadeIn->Hold
// junction is 3/n^2 * target = 2.6e-9 * target (n = 33 750 steps), while ONE
// FLOAT ULP at that target is 6e-8 * target - 23x larger. The quantity is below
// the representation floor of the emitted float series: measured at the
// defaults it reads EXACTLY 0.0 at every swept target, and the interior median
// it would be compared against reads exactly 0.0 too at target = 0.35, i.e. the
// ratio is not even defined. Measuring it there would assert on quantisation
// noise. At the compressed 2 s / 1 s / 3 s lifecycle (SC-001's own compression)
// the same quantity is 1.3e-6 * target, twenty times ABOVE one ulp, and the
// measured junction/interior-median ratios are 1.08 - 1.11 against the 3x gate.
// The GATE IS UNCHANGED; only the fixture is one where the number exists.
// ==============================================================================
TEST_CASE("BloomEngine_LifecycleTimingAndC1Shape", "[bloom_engine][long]") {
    using Krate::DSP::BloomEngine;

    SECTION("SC-002 (a), (b), (c)(i), (d): the default 45 s / 120 s / 180 s lifecycle") {
        // The parent amplitude sweep of the criterion: just above
        // kSilentParentAmplitude (the weakest partial that is still an eligible
        // parent at all), two mid-scale values, and unity. The shape claims are
        // therefore not measured only at the most favourable amplitude.
        constexpr std::array<float, 4> kParentAmplitudes{1.1e-5f, 0.01f, 0.35f, 1.0f};

        for (const float parentAmp : kParentAmplitudes) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillOneParent(ratios.data(), amplitudes.data(), kSlots, parentAmp);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x5C002000u, /*capacity=*/8, /*numChildSlots=*/1);
            engine.setParentCount(1);
            engine.setChildrenPerEvent(1);
            // Clause (e) asserts separately that (a)-(d) are unchanged by the
            // jitter setting; this pass runs at jitter 0 so the Hold segment's
            // length is the CONFIGURED one and every junction index below is
            // exact rather than drawn.
            engine.setHoldJitterFraction(0.0f);

            engine.triggerBloom();
            const std::size_t spawnReturn = engine.processChunk(
                ratios.data(), amplitudes.data(), 1, BloomEngine::kControlChunkSamples);
            REQUIRE(spawnReturn == engine.capacity());
            REQUIRE(engine.getLiveChildCount() == std::size_t{1});

            const std::size_t t = soleLiveChild(engine);
            REQUIRE(t < BloomEngine::kMaxChildren);
            REQUIRE(engine.getChildPhase(t) == BloomEngine::Phase::FadeIn);
            // FR-031's C1 START: a child latched on this control step emits
            // amplitude 0 on its very first chunk, because advanceChildren()
            // runs at S3.3 step (4), BEFORE the spawn at step (5).
            REQUIRE(engine.getChildAmplitude(t) == 0.0f);

            // FR-023 with childGain = depth = 1 and consumerTiltDb = 0 reduces to
            // the parent amplitude. Asserted as a precondition so a later clause
            // measuring "50 % of the latched target" is known to be measuring the
            // right target.
            const float target = engine.getChildTargetAmplitude(t);
            INFO("parentAmp = " << parentAmp << " latched target = " << target);
            REQUIRE(std::fabs(target - parentAmp) <= 1.0e-6f * parentAmp);

            const std::uint32_t endIn = fadeStepsOf(engine.getFadeInSeconds());
            const std::uint32_t hold = holdStepsOf(engine.getHoldSeconds());
            const std::uint32_t endHold = endIn + hold;
            const std::uint32_t fadeOut = fadeStepsOf(engine.getFadeOutSeconds());
            const std::uint32_t endOut = endHold + fadeOut;
            REQUIRE(endIn == std::uint32_t{33750});    // 45 s x 750 steps/s
            REQUIRE(hold == std::uint32_t{90000});     // 120 s, unjittered
            REQUIRE(fadeOut == std::uint32_t{135000});  // 180 s
            // The latched hold is the configured one at jitter 0. Compared with a
            // tolerance of under one control step because getChildHoldSeconds()
            // is `float(holdSteps) * controlDtSec_` and 64/48000 is not exact in
            // float - the quantity being asserted is the STEP COUNT, read back
            // through a float conversion.
            REQUIRE(std::fabs(engine.getChildHoldSeconds(t) - engine.getHoldSeconds()) <= 1.0e-3f);

            // The 2 % analysis windows of clause (c)(i), in steps.
            const std::uint32_t wIn = std::max<std::uint32_t>(1u, (endIn + 25u) / 50u);
            const std::uint32_t wOut = std::max<std::uint32_t>(1u, (fadeOut + 25u) / 50u);

            // ---- streaming accumulators -----------------------------------
            float prev = engine.getChildAmplitude(t);
            double maxDIn = 0.0;
            double maxDOut = 0.0;
            double firstSumIn = 0.0;
            double lastSumIn = 0.0;
            double firstSumOut = 0.0;
            double lastSumOut = 0.0;
            std::uint32_t monoViolIn = 0;
            std::uint32_t monoViolOut = 0;
            std::uint32_t plateauIn = 1;
            std::uint32_t plateauOut = 1;
            std::uint32_t runIn = 1;
            std::uint32_t runOut = 1;
            std::uint32_t k50In = 0;
            std::uint32_t k99In = 0;
            std::uint32_t k50Out = 0;
            std::uint32_t k99Out = 0;
            bool holdIsExactTarget = true;
            bool fadeInReachesTarget = false;
            bool phaseOk = true;

            for (std::uint32_t k = 1; k <= endOut; ++k) {
                const std::size_t returned = engine.processChunk(
                    ratios.data(), amplitudes.data(), 1, BloomEngine::kControlChunkSamples);
                if (returned != engine.capacity()) {
                    phaseOk = false;  // engaged_ is STICKY: every call returns capacity()
                }
                const float a = engine.getChildAmplitude(t);
                const double d = static_cast<double>(a) - static_cast<double>(prev);
                const double ad = std::fabs(d);

                if (k <= endIn) {
                    // ---- the FADE-IN segment, samples [0, endIn] ----------
                    if (a < prev) {
                        ++monoViolIn;
                    }
                    maxDIn = std::max(maxDIn, ad);
                    if (k <= wIn) {
                        firstSumIn += ad;
                    }
                    if (k > endIn - wIn) {
                        lastSumIn += ad;
                    }
                    if (a == prev) {
                        ++runIn;
                        plateauIn = std::max(plateauIn, runIn);
                    } else {
                        runIn = 1;
                    }
                    if (k50In == 0 && a >= 0.5f * target) {
                        k50In = k;
                    }
                    if (k99In == 0 && a >= 0.99f * target) {
                        k99In = k;
                    }
                    if (k == endIn) {
                        // Reaches the latched target EXACTLY, not asymptotically:
                        // the Hold branch assigns c.target itself.
                        fadeInReachesTarget = (a == target);
                        if (engine.getChildPhase(t) != BloomEngine::Phase::Hold) {
                            phaseOk = false;
                        }
                    } else if (engine.getChildPhase(t) != BloomEngine::Phase::FadeIn) {
                        phaseOk = false;
                    }
                } else if (k <= endHold) {
                    // ---- the HOLD segment ---------------------------------
                    // The plateau clause is deliberately NOT applied here: the
                    // hold IS a plateau, 120 s of it, and that is the design.
                    if (a != target) {
                        holdIsExactTarget = false;
                    }
                    const BloomEngine::Phase p = engine.getChildPhase(t);
                    const BloomEngine::Phase expected = (k == endHold)
                                                            ? BloomEngine::Phase::FadeOut
                                                            : BloomEngine::Phase::Hold;
                    if (p != expected) {
                        phaseOk = false;
                    }
                } else {
                    // ---- the FADE-OUT segment, samples [endHold, endOut] ---
                    if (a > prev) {
                        ++monoViolOut;
                    }
                    maxDOut = std::max(maxDOut, ad);
                    if (k <= endHold + wOut) {
                        firstSumOut += ad;
                    }
                    if (k > endOut - wOut) {
                        lastSumOut += ad;
                    }
                    if (a == prev) {
                        ++runOut;
                        plateauOut = std::max(plateauOut, runOut);
                    } else {
                        runOut = 1;
                    }
                    if (k50Out == 0 && a <= 0.5f * target) {
                        k50Out = k - endHold;
                    }
                    if (k99Out == 0 && a <= 0.01f * target) {
                        k99Out = k - endHold;
                    }
                    const BloomEngine::Phase p = engine.getChildPhase(t);
                    const BloomEngine::Phase expected = (k == endOut)
                                                            ? BloomEngine::Phase::Idle
                                                            : BloomEngine::Phase::FadeOut;
                    if (p != expected) {
                        phaseOk = false;
                    }
                }
                prev = a;
            }

            INFO("endIn=" << endIn << " endHold=" << endHold << " endOut=" << endOut);
            INFO("(a) k50=" << k50In << " (want " << endIn / 2u << " +/- 1 %)  k99=" << k99In
                            << " (want >= " << (9u * endIn) / 10u << ")");
            INFO("(b) k50Out=" << k50Out << " (want " << fadeOut / 2u << " +/- 1 %)  k99Out="
                               << k99Out << " (want >= " << (9u * fadeOut) / 10u << ")");
            INFO("(c) fade-in  first2%/max=" << (firstSumIn / wIn) / maxDIn
                                             << " last2%/max=" << (lastSumIn / wIn) / maxDIn);
            INFO("(c) fade-out first2%/max=" << (firstSumOut / wOut) / maxDOut
                                             << " last2%/max=" << (lastSumOut / wOut) / maxDOut);
            INFO("(d) monoViol in/out=" << monoViolIn << "/" << monoViolOut
                                        << " maxPlateau in/out=" << plateauIn << "/"
                                        << plateauOut);

            // ---- (a) fade-in timing --------------------------------------
            // smoothstep(0.5) is exactly 0.5, so the 50 % crossing sits on the
            // segment midpoint; smoothstep(u) = 0.99 at u = 0.9411.
            REQUIRE(k50In > 0u);
            REQUIRE(k99In > 0u);
            const double halfIn = 0.5 * static_cast<double>(endIn);
            REQUIRE(std::fabs(static_cast<double>(k50In) - halfIn) <= 0.01 * halfIn);
            REQUIRE(static_cast<double>(k99In) >= 0.9 * static_cast<double>(endIn));
            REQUIRE(k99In <= endIn);
            REQUIRE(fadeInReachesTarget);

            // ---- (b) fade-out timing, mirrored, ending at EXACTLY 0.0f ----
            REQUIRE(k50Out > 0u);
            REQUIRE(k99Out > 0u);
            const double halfOut = 0.5 * static_cast<double>(fadeOut);
            REQUIRE(std::fabs(static_cast<double>(k50Out) - halfOut) <= 0.01 * halfOut);
            REQUIRE(static_cast<double>(k99Out) >= 0.9 * static_cast<double>(fadeOut));
            REQUIRE(k99Out <= fadeOut);
            REQUIRE(engine.getChildAmplitude(t) == 0.0f);
            REQUIRE(engine.getChildPhase(t) == BloomEngine::Phase::Idle);
            REQUIRE(engine.getCompletedChildCount() == std::uint64_t{1});
            REQUIRE(engine.getLiveChildCount() == std::size_t{0});

            // ---- (c)(i) endpoint derivative flatness ----------------------
            // smoothstep's f'(u) = 6u(1-u) gives ~3.9 % here; a LINEAR ramp -
            // the realistic C0-only defect FR-031 exists to forbid - gives
            // exactly 100 % and fails, because every one of its first
            // differences is identical to the maximum.
            REQUIRE(maxDIn > 0.0);
            REQUIRE(maxDOut > 0.0);
            REQUIRE((firstSumIn / wIn) / maxDIn <= 0.10);
            REQUIRE((lastSumIn / wIn) / maxDIn <= 0.10);
            REQUIRE((firstSumOut / wOut) / maxDOut <= 0.10);
            REQUIRE((lastSumOut / wOut) / maxDOut <= 0.10);

            // ---- (d) monotone, non-stalling, correctly phased --------------
            // No minimum-first-difference threshold is asserted: that was the
            // RETRACTED FR-031 step floor, which failed on correct code (the
            // first non-zero first difference at a 45 s fade is ~2.6e-9).
            REQUIRE(monoViolIn == 0u);
            REQUIRE(monoViolOut == 0u);
            REQUIRE(holdIsExactTarget);
            REQUIRE(phaseOk);
            // "No plateau of identical consecutive samples exceeds 1 s", i.e.
            // 750 control steps at 48 kHz. Plan S5.3 computes the worst case at
            // 0.67 s on the 300 s maximum fade; measured here it is 21 steps.
            REQUIRE(plateauIn <= static_cast<std::uint32_t>(kControlRateHz));
            REQUIRE(plateauOut <= static_cast<std::uint32_t>(kControlRateHz));
            REQUIRE(engine.stateFinite());
        }
    }

    SECTION("SC-002 (c)(ii): junction second differences, measured where they exist") {
        // 2 s / 1 s / 3 s - SC-001's own compression. See the case banner for the
        // ulp arithmetic that forces it; the 3x gate itself is unchanged.
        constexpr std::array<float, 2> kParentAmplitudes{1.1e-5f, 1.0f};

        for (const float parentAmp : kParentAmplitudes) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillOneParent(ratios.data(), amplitudes.data(), kSlots, parentAmp);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x5C002001u, /*capacity=*/8, /*numChildSlots=*/1);
            engine.setParentCount(1);
            engine.setChildrenPerEvent(1);
            engine.setHoldJitterFraction(0.0f);
            engine.setFadeInSeconds(2.0f);
            engine.setHoldSeconds(1.0f);
            engine.setFadeOutSeconds(3.0f);

            // Pre-roll through the shared cloud-free driver, so the spawn lands on
            // a NON-ZERO control step - eventRng_ is re-seeded from
            // (seed, controlStep_) at the head of every event (plan S1.6), and an
            // arm that only ever spawns on step 0 never exercises that. The
            // return is also the disengaged pass-through contract: the caller's
            // UNCLAMPED parentCount, not capacity().
            const std::size_t preRoll =
                stepEngine(engine, ratios.data(), amplitudes.data(), 1, 10);
            REQUIRE(preRoll == std::size_t{1});
            REQUIRE_FALSE(engine.isEngaged());

            engine.triggerBloom();
            const std::size_t spawnReturn = engine.processChunk(
                ratios.data(), amplitudes.data(), 1, BloomEngine::kControlChunkSamples);
            REQUIRE(spawnReturn == engine.capacity());
            const std::size_t t = soleLiveChild(engine);
            REQUIRE(t < BloomEngine::kMaxChildren);

            const std::uint32_t endIn = fadeStepsOf(2.0f);
            const std::uint32_t endHold = endIn + holdStepsOf(1.0f);
            const std::uint32_t endOut = endHold + fadeStepsOf(3.0f);
            REQUIRE(endIn == std::uint32_t{1500});
            REQUIRE(endHold == std::uint32_t{2250});
            REQUIRE(endOut == std::uint32_t{4500});

            // THE ONE MATERIALISED WINDOW IN THIS TU: 4 501 floats.
            std::vector<float> series;
            series.reserve(static_cast<std::size_t>(endOut) + 1u);
            series.push_back(engine.getChildAmplitude(t));
            bool returnedCapacity = true;
            for (std::uint32_t k = 1; k <= endOut; ++k) {
                const std::size_t returned = engine.processChunk(
                    ratios.data(), amplitudes.data(), 1, BloomEngine::kControlChunkSamples);
                if (returned != engine.capacity()) {
                    returnedCapacity = false;  // one flag, not 4 500 assertions
                }
                series.push_back(engine.getChildAmplitude(t));
            }
            REQUIRE(returnedCapacity);

            const auto secondDifference = [&series](std::size_t k) {
                return static_cast<double>(series[k + 1]) - 2.0 * static_cast<double>(series[k]) +
                       static_cast<double>(series[k - 1]);
            };
            // The INTERIOR excludes the outer 5 % of the segment at each end, so
            // the reference distribution is the segment's own curvature and not
            // the junction neighbourhoods being tested.
            const auto interiorMedian = [&secondDifference](std::size_t lo, std::size_t hi) {
                const std::size_t margin =
                    static_cast<std::size_t>(std::lround(0.05 * static_cast<double>(hi - lo)));
                std::vector<double> magnitudes;
                magnitudes.reserve(hi - lo + 1u);
                for (std::size_t k = lo + margin; k + margin <= hi; ++k) {
                    magnitudes.push_back(std::fabs(secondDifference(k)));
                }
                std::sort(magnitudes.begin(), magnitudes.end());
                return magnitudes[magnitudes.size() / 2u];
            };

            const double medianIn = interiorMedian(1u, static_cast<std::size_t>(endIn) - 1u);
            const double medianOut = interiorMedian(static_cast<std::size_t>(endHold) + 1u,
                                                    static_cast<std::size_t>(endOut) - 1u);
            const double junctionIn = std::fabs(secondDifference(endIn));
            const double junctionOut = std::fabs(secondDifference(endHold));

            INFO("parentAmp=" << parentAmp << " |d2| FadeIn->Hold=" << junctionIn
                              << " interior median=" << medianIn);
            INFO("parentAmp=" << parentAmp << " |d2| Hold->FadeOut=" << junctionOut
                              << " interior median=" << medianOut);
            // Non-vacuity: if the reference itself is 0 the ratio is undefined and
            // the clause would pass on anything.
            REQUIRE(medianIn > 0.0);
            REQUIRE(medianOut > 0.0);
            REQUIRE(junctionIn <= 3.0 * medianIn);
            REQUIRE(junctionOut <= 3.0 * medianOut);
        }
    }

    SECTION("SC-002 (e): the hold jitter is real, and it does not touch the fades") {
        constexpr std::size_t kEvents = 200;

        // ---- the default holdJitterFraction = 0.5 ------------------------
        std::size_t differing = 0;
        bool inBand = true;
        double smallestHold = 1.0e9;
        double largestHold = 0.0;
        for (std::size_t e = 0; e < kEvents; ++e) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillTwoParents(ratios.data(), amplitudes.data(), kSlots);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x31770000u + static_cast<std::uint32_t>(e),
                               /*capacity=*/8, /*numChildSlots=*/2);
            engine.setParentCount(2);
            engine.setChildrenPerEvent(2);
            REQUIRE(engine.getHoldJitterFraction() == BloomEngine::kDefaultHoldJitterFraction);

            engine.triggerBloom();
            const std::size_t returned = engine.processChunk(
                ratios.data(), amplitudes.data(), 2, BloomEngine::kControlChunkSamples);
            REQUIRE(returned == engine.capacity());
            REQUIRE(engine.getLiveChildCount() == std::size_t{2});

            const float h0 = engine.getChildHoldSeconds(0);
            const float h1 = engine.getChildHoldSeconds(1);
            if (h0 != h1) {
                ++differing;
            }
            const float configured = engine.getHoldSeconds();
            const float lo = 0.5f * configured - 1.0e-3f;  // (1 - 0.5), one step of slack
            const float hi = 1.5f * configured + 1.0e-3f;
            if (h0 < lo || h0 > hi || h1 < lo || h1 > hi) {
                inBand = false;
            }
            smallestHold = std::min({smallestHold, static_cast<double>(h0),
                                     static_cast<double>(h1)});
            largestHold = std::max({largestHold, static_cast<double>(h0),
                                    static_cast<double>(h1)});
        }
        INFO("events=" << kEvents << " with differing holds=" << differing);
        INFO("latched hold band observed = [" << smallestHold << ", " << largestHold << "] s");
        // A rare equal draw is tolerated, not required to fail.
        REQUIRE(differing * 100u >= 90u * kEvents);
        REQUIRE(inBand);
        // Non-vacuity: the band must actually be exercised, not merely respected
        // by an engine that applies no jitter at all.
        REQUIRE(smallestHold < 0.9 * static_cast<double>(BloomEngine::kDefaultHoldSeconds));
        REQUIRE(largestHold > 1.1 * static_cast<double>(BloomEngine::kDefaultHoldSeconds));

        // ---- holdJitterFraction = 0: every latched hold is the configured one
        bool identical = true;
        bool exact = true;
        for (std::size_t e = 0; e < 50; ++e) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillTwoParents(ratios.data(), amplitudes.data(), kSlots);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x317700FFu + static_cast<std::uint32_t>(e),
                               /*capacity=*/8, /*numChildSlots=*/2);
            engine.setParentCount(2);
            engine.setChildrenPerEvent(2);
            engine.setHoldJitterFraction(0.0f);

            engine.triggerBloom();
            const std::size_t returned = engine.processChunk(
                ratios.data(), amplitudes.data(), 2, BloomEngine::kControlChunkSamples);
            REQUIRE(returned == engine.capacity());
            REQUIRE(engine.getLiveChildCount() == std::size_t{2});
            const float h0 = engine.getChildHoldSeconds(0);
            const float h1 = engine.getChildHoldSeconds(1);
            if (h0 != h1) {
                identical = false;
            }
            // Under one control step: the assertion is on the STEP COUNT, read
            // back through `float(holdSteps) * controlDtSec_`, and 64/48000 is
            // not exact in float.
            if (std::fabs(h0 - engine.getHoldSeconds()) > 1.0e-3f) {
                exact = false;
            }
        }
        REQUIRE(identical);
        REQUIRE(exact);

        // ---- the jitter touches ONLY the Hold segment ---------------------
        // Two engines identical but for holdJitterFraction 0 vs 1 must leave
        // FadeIn on exactly the same control step: fadeInSteps and fadeOutSteps
        // are latched UNJITTERED (FR-036, Clarification Q5). The hold draw is
        // taken on both branches, so the seeded stream position is identical and
        // the two children are otherwise the same child.
        const auto fadeInExitStep = [](float jitter) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillOneParent(ratios.data(), amplitudes.data(), kSlots, 1.0f);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x31771234u, /*capacity=*/8, /*numChildSlots=*/1);
            engine.setParentCount(1);
            engine.setChildrenPerEvent(1);
            engine.setHoldJitterFraction(jitter);
            engine.setFadeInSeconds(2.0f);
            engine.setHoldSeconds(1.0f);
            engine.setFadeOutSeconds(3.0f);
            engine.triggerBloom();
            const std::size_t spawnReturn = engine.processChunk(
                ratios.data(), amplitudes.data(), 1, BloomEngine::kControlChunkSamples);
            REQUIRE(spawnReturn == engine.capacity());
            const std::size_t t = soleLiveChild(engine);
            REQUIRE(t < BloomEngine::kMaxChildren);

            std::uint32_t exitStep = 0;
            bool returnedCapacity = true;
            for (std::uint32_t k = 1; k <= 4000u && exitStep == 0u; ++k) {
                const std::size_t returned = engine.processChunk(
                    ratios.data(), amplitudes.data(), 1, BloomEngine::kControlChunkSamples);
                if (returned != engine.capacity()) {
                    returnedCapacity = false;
                }
                if (engine.getChildPhase(t) != BloomEngine::Phase::FadeIn) {
                    exitStep = k;
                }
            }
            REQUIRE(returnedCapacity);
            return exitStep;
        };
        const std::uint32_t exitNoJitter = fadeInExitStep(0.0f);
        const std::uint32_t exitFullJitter = fadeInExitStep(1.0f);
        INFO("FadeIn exit step: jitter 0 -> " << exitNoJitter << ", jitter 1 -> "
                                              << exitFullJitter);
        REQUIRE(exitNoJitter == fadeStepsOf(2.0f));
        REQUIRE(exitFullJitter == exitNoJitter);
    }

    SECTION("SC-002 (f): FR-033 - setters moved while a child is in flight") {
        // THE CLAUSE THIS ARM EXISTS FOR. An implementation that recomputed
        // fadeInSteps per chunk from the CURRENT setter value - the natural
        // mistake - passes every other clause of every other criterion. Here it
        // fails twice: the in-flight child's FadeIn->Hold junction moves from
        // step 33 750 to step 225 000, and its amplitude series takes a
        // derivative step at the setter instant.
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        fillTwoParents(ratios.data(), amplitudes.data(), kSlots);

        BloomEngine engine;
        prepareTriggerOnly(engine, 0x5C002F00u, /*capacity=*/8, /*numChildSlots=*/2);
        engine.setParentCount(2);
        engine.setChildrenPerEvent(1);
        engine.setHoldJitterFraction(0.0f);

        engine.triggerBloom();
        const std::size_t spawnReturn = engine.processChunk(
            ratios.data(), amplitudes.data(), 2, BloomEngine::kControlChunkSamples);
        REQUIRE(spawnReturn == engine.capacity());
        REQUIRE(engine.getLiveChildCount() == std::size_t{1});
        const std::size_t tA = soleLiveChild(engine);
        REQUIRE(tA < BloomEngine::kMaxChildren);

        const std::uint32_t endInA = fadeStepsOf(engine.getFadeInSeconds());
        const std::uint32_t endHoldA = endInA + holdStepsOf(engine.getHoldSeconds());
        const std::uint32_t endOutA = endHoldA + fadeStepsOf(engine.getFadeOutSeconds());
        REQUIRE(endInA == std::uint32_t{33750});
        REQUIRE(endHoldA == std::uint32_t{123750});
        REQUIRE(endOutA == std::uint32_t{258750});
        const float holdSecondsA = engine.getChildHoldSeconds(tA);

        // 20 s elapsed, on the 48 kHz control grid.
        constexpr std::uint32_t kSetterStep = 15000u;
        constexpr std::uint32_t kWindow = 500u;
        // The NEW values, all at their clamp ends: 300 s fade-in, 0 s hold,
        // 600 s fade-out.
        const std::uint32_t endInB = fadeStepsOf(BloomEngine::kMaxFadeInSeconds);
        REQUIRE(endInB == std::uint32_t{225000});

        std::size_t tB = BloomEngine::kMaxChildren;
        std::uint32_t spawnStepB = 0;
        float prev = engine.getChildAmplitude(tA);
        std::uint32_t monoViolA = 0;
        bool phaseOkA = true;
        bool holdUnchangedA = true;
        bool bEverHeld = false;
        std::uint32_t exitFadeInB = 0;
        std::vector<double> windowD;
        windowD.reserve(2u * kWindow + 1u);
        double dAtSetter = -1.0;

        bool returnedCapacity = true;
        for (std::uint32_t k = 1; k <= endOutA; ++k) {
            const std::size_t returned = engine.processChunk(
                ratios.data(), amplitudes.data(), 2, BloomEngine::kControlChunkSamples);
            if (returned != engine.capacity()) {
                returnedCapacity = false;  // one flag, not 258 750 assertions
            }

            const float a = engine.getChildAmplitude(tA);
            if (a < prev && k <= endInA) {
                ++monoViolA;
            }
            const double ad = std::fabs(static_cast<double>(a) - static_cast<double>(prev));
            if (k >= kSetterStep - kWindow && k <= kSetterStep + kWindow) {
                windowD.push_back(ad);
            }
            if (k == kSetterStep + 1u) {
                dAtSetter = ad;
            }
            prev = a;

            // A's phase must follow its ORIGINALLY LATCHED bounds throughout.
            const BloomEngine::Phase expectedA = [&]() -> BloomEngine::Phase {
                if (k < endInA) {
                    return BloomEngine::Phase::FadeIn;
                }
                if (k < endHoldA) {
                    return BloomEngine::Phase::Hold;
                }
                if (k < endOutA) {
                    return BloomEngine::Phase::FadeOut;
                }
                return BloomEngine::Phase::Idle;
            }();
            if (engine.getChildPhase(tA) != expectedA) {
                phaseOkA = false;
            }
            if (k < endOutA && engine.getChildHoldSeconds(tA) != holdSecondsA) {
                holdUnchangedA = false;
            }

            if (k == kSetterStep) {
                // THE SETTERS, at exactly 20 s of the in-flight child's life.
                engine.setFadeInSeconds(BloomEngine::kMaxFadeInSeconds);
                engine.setHoldSeconds(0.0f);
                engine.setFadeOutSeconds(BloomEngine::kMaxFadeOutSeconds);
                REQUIRE(engine.getFadeInSeconds() == BloomEngine::kMaxFadeInSeconds);
                REQUIRE(engine.getHoldSeconds() == 0.0f);
                REQUIRE(engine.getFadeOutSeconds() == BloomEngine::kMaxFadeOutSeconds);
                engine.triggerBloom();  // the NEXT child, which must use them
            } else if (k == kSetterStep + 1u) {
                REQUIRE(engine.getLiveChildCount() == std::size_t{2});
                for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                    if (i != tA && engine.getChildPhase(i) != BloomEngine::Phase::Idle) {
                        tB = i;
                    }
                }
                REQUIRE(tB < BloomEngine::kMaxChildren);
                spawnStepB = k;
                // holdSeconds(0) latched: the child passes from FadeIn straight
                // into FadeOut and never reports Hold.
                REQUIRE(engine.getChildHoldSeconds(tB) <= 1.0e-3f);
            }

            if (tB < BloomEngine::kMaxChildren && k > spawnStepB) {
                const BloomEngine::Phase pB = engine.getChildPhase(tB);
                if (pB == BloomEngine::Phase::Hold) {
                    bEverHeld = true;
                }
                if (exitFadeInB == 0u && pB != BloomEngine::Phase::FadeIn) {
                    exitFadeInB = k - spawnStepB;
                }
            }
        }

        INFO("A: monoViol=" << monoViolA << " |d| at the setter instant=" << dAtSetter);
        INFO("B: spawned at step " << spawnStepB << ", left FadeIn after " << exitFadeInB
                                   << " steps (want " << endInB << ")");
        REQUIRE(returnedCapacity);
        REQUIRE(phaseOkA);          // the latched junctions did not move
        REQUIRE(holdUnchangedA);    // nor did the latched hold
        REQUIRE(monoViolA == 0u);   // monotone across the setter instant

        // No first-difference outlier at the setter instant: the same 3x gate
        // clause (c)(ii) uses, against the median of the 1 001-step neighbourhood.
        REQUIRE(dAtSetter >= 0.0);
        REQUIRE(windowD.size() == static_cast<std::size_t>(2u * kWindow + 1u));
        std::sort(windowD.begin(), windowD.end());
        const double medianD = windowD[windowD.size() / 2u];
        INFO("neighbourhood median |d| = " << medianD);
        REQUIRE(medianD > 0.0);
        REQUIRE(dAtSetter <= 3.0 * medianD);

        // THE NEXT child used the NEW values: a 300 s fade-in (225 000 steps,
        // against the 33 750 the in-flight child kept) and a zero hold.
        REQUIRE(exitFadeInB == endInB);
        REQUIRE_FALSE(bEverHeld);
        REQUIRE(engine.stateFinite());
    }
}

// ==============================================================================
// T013 - BloomEngine_ChildSpawnAndDeathAreClickFree (SC-001)
// ==============================================================================
// Roadmap line 351: "children appear and disappear without clicks". This is the
// ONLY criterion in the phase that listens to the bloom through a real
// HarmonicCloud over its whole lifecycle; every other rendered arm looks at one
// slot or one segment.
//
// WHY THE GATE IS DIFFERENTIAL AND NOT "ZERO DETECTIONS" (spec SC-001, verbatim
// reasoning). A 64-partial cloud at richness 1.0 beats against itself, and its
// per-partial Brownian drift and mutation lanes keep every partial's amplitude
// moving; 5-sigma frame outliers produced by that beating have nothing to do
// with a bloom. An absolute zero-detection gate over the full render would
// therefore either fail on correct code or force the sigma up until it measured
// nothing. The REFERENCE RUN is what separates the two: the same seed, the same
// cloud, the same parent spectrum, the same trigger instants, and
// numChildSlots = 0 so the engine never engages. Anything the bloom adds shows
// up as a DIFFERENCE.
// MEASURED ON THIS FIXTURE, and worth stating because it makes the gate
// stricter than the spec's floor: with the amplitude law the parent-spectrum
// block comment derives (1/(i+1)^2), the reference scores 0 detections at
// sigma 5 on every seed, so `bloom <= reference` is `bloom == 0`. The
// differential SHAPE of the clause is kept rather than collapsed to a constant
// because it is the spec's, and because it keeps the case honest if a later
// fixture change makes the carrier beat again.
//
// WHY THE WINDOWED CLAUSE IS ALSO NEEDED. The differential clause is a whole-
// render count, and a count can hide a swap: an implementation that removed one
// beating detection somewhere and added one click at a spawn passes it. The
// 200 ms windows (the feedback_ecology_test.cpp:3689-3696 idiom) are taken at
// the instants where a click can only be the bloom's - the control step on
// which a child is latched, and the control step on which one retires - and
// there the requirement is 0.
//
// WHY 0 IS ACHIEVABLE THERE, i.e. why this clause can pass on correct code:
//   - at a SPAWN the child's amplitude is exactly 0.0f on its first emitted
//     chunk (advanceChildren() runs at S3.3 step (4), BEFORE the spawn at step
//     (5)), so the slot's amplitude goes pad-0 -> child-0 with no step at all;
//     the slot's RATIO does jump, from the pad form float(s+1) to the child
//     ratio, but it does so while the slot is silent;
//   - at a DEATH the fade-out is written as 1 - smoothstep(v) and therefore
//     lands on EXACTLY 0.0f (plan S5.3) before applyOutput's unconditional
//     owned-region pad repaints the slot, so the amplitude is continuous across
//     the retirement and the ratio again jumps while silent.
// Both are properties the implementation has; the clause exists to keep them.
//
// THE INSTANTS ARE READ FROM THE COUNTERS, NOT PREDICTED. getSpawnEventCount()
// and getCompletedChildCount() transitions are sampled after every control
// chunk, so a death instant is the jittered one that actually happened rather
// than one computed from holdSeconds - which is the whole point of FR-036's
// jitter being a draw.
//
// TAGGING. tasks.md T013: "[long] ONLY if the measured runtime says so
// (> ~15 s)". MEASURED: 0.48 s wall clock for the whole case (10 seeds x 2 runs
// x 8.5 s = 170 s of 64-partial stereo audio plus the detector passes), MSVC
// Release, x64. That is two orders of magnitude under the bar, so the case
// stays UNTAGGED and runs in the per-push lane.
//
// FALSIFICATION (tasks.md T013), RUN AND REPORTED HONESTLY: IT DOES NOT FIRE.
// The mutation the task proposes - in BloomEngine::advanceChildren, emit
// `ch.target` instead of `ch.target * smoothstep(u)` while in Phase::FadeIn, so
// a child reaches full amplitude in ONE control chunk - was applied, built and
// measured over 3 seeds x 4 fixture variants (fundamental 110 and 440 Hz,
// childGain 0.35 and 1.0). Every 200 ms window scored 0 detections at sigma 5
// in every variant, i.e. the mutated build passes this case.
// THE REASON IS THE CONSUMER, AND IT IS STRUCTURAL: HarmonicCloud smooths every
// partial amplitude with a one-pole at kAmpSmoothTimeSec = 2 ms
// (harmonic_cloud.h:165), so an instantaneous change to a TARGET amplitude
// reaches the output as a ~2 ms exponential ramp - roughly 96 samples - which a
// derivative detector thresholding at mean + 5 sigma over a 512-sample frame
// cannot resolve as an outlier. At sigma 2 the mutated windows do light up
// (e.g. 23-84 detections per window), so the difference is present in the
// signal and simply sits far below the pinned threshold.
// WHAT THIS CASE THEREFORE IS: a guard against a discontinuity the cloud's own
// smoother does NOT absorb - a stale or un-repadded slot, a NaN-rejected array,
// an owned-slot write that lands outside the smoothed path - and against the
// bloom raising the carrier's artifact count at all. It is NOT evidence that
// the FR-031 fade shape itself is smooth; that is SC-002's job, which measures
// the envelope directly through getChildAmplitude() and where the equivalent
// mutation fails loudly. Do not "strengthen" this case by lowering kClickSigma:
// the threshold is pinned by the spec and by the house pattern.
// ==============================================================================
TEST_CASE("BloomEngine_ChildSpawnAndDeathAreClickFree", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    // ---- the fixture, in control steps and samples --------------------------
    // 8.5 s at 48 kHz is 6 375 control chunks. The two triggers sit at 0.5 s and
    // 1.5 s, so the second event spawns while the first event's children are
    // still fading in - the overlapping case, which a single-event fixture
    // cannot reach. The worst-case last death is
    //   trigger B (1 125) + fadeIn (1 500) + max jittered hold (1 125)
    //                     + fadeOut (2 250) = 6 000 steps = 384 000 samples,
    // and its 200 ms window ends at 393 600, inside the 408 000-sample render.
    // That margin is why the render is 8.5 s and not 8.0 s.
    constexpr std::size_t kChunks = 6375;
    constexpr std::size_t kRenderSamples = kChunks * BloomEngine::kControlChunkSamples;
    constexpr std::size_t kWindowSamples = 9600;  // 200 ms at 48 kHz
    constexpr std::size_t kTriggerChunkA = 375;   // 0.5 s, past the cloud's onset
    constexpr std::size_t kTriggerChunkB = 1125;  // 1.5 s, first event still fading in
    constexpr std::size_t kCapacity = kSlots;     // == HarmonicCloud activeCount_ at richness 1
    constexpr std::size_t kChildSlots = 4;        // exactly the 2 events x 2 children

    // Ten seeds. They drive the engine's draws AND the cloud's drift/pan/phase,
    // so each pair is a different carrier as well as a different bloom.
    constexpr std::array<std::uint32_t, 10> kSeeds{0x5C001001u, 0x5C001002u, 0x5C001003u,
                                                   0x5C001004u, 0x5C001005u, 0x5C001006u,
                                                   0x5C001007u, 0x5C001008u, 0x5C001009u,
                                                   0x5C00100Au};

    struct RunResult {
        std::vector<float> left;
        std::vector<float> right;
        std::vector<std::size_t> instants;  ///< first sample of each spawn/death chunk
        std::uint64_t spawned = 0;
        std::uint64_t completed = 0;
        std::size_t activeCount = 0;
        float rms = 0.0f;  ///< non-vacuity: the detector SKIPS frames below
                           ///< energyThresholdDb (-60 dB, artifact_detection.h:203-205)
        bool finite = true;
        bool returnsOk = true;
    };

    // One render. The rig lives only inside this lambda, so at most one
    // HarmonicCloud is on the stack at a time; the two buffers that survive it
    // are the render itself, which the detector has to see whole.
    const auto render = [&](std::uint32_t seed, std::size_t numChildSlots) {
        RunResult out;
        out.left.assign(kRenderSamples, 0.0f);
        out.right.assign(kRenderSamples, 0.0f);

        CloudRig rig;
        rig.prepare(seed, kCapacity, numChildSlots);
        out.activeCount = rig.cloud.getActivePartialCount();

        std::uint64_t events = 0;
        std::uint64_t deaths = 0;
        for (std::size_t c = 0; c < kChunks; ++c) {
            if (c == kTriggerChunkA || c == kTriggerChunkB) {
                rig.bloom.triggerBloom();
            }
            const std::size_t at = c * BloomEngine::kControlChunkSamples;
            const std::size_t returned = rig.chunk(out.left.data() + at, out.right.data() + at);

            // FR-005 / S3.1: a disengaged engine returns the caller's count and
            // an engaged one returns capacity(). Checked here because it is the
            // cheapest possible guard against a fixture that silently stopped
            // handing the cloud the child slots.
            const std::size_t expected = rig.bloom.isEngaged() ? kCapacity : rig.parentCount;
            if (returned != expected) {
                out.returnsOk = false;
            }

            if (rig.bloom.getSpawnEventCount() != events ||
                rig.bloom.getCompletedChildCount() != deaths) {
                events = rig.bloom.getSpawnEventCount();
                deaths = rig.bloom.getCompletedChildCount();
                out.instants.push_back(at);
            }
            if (!rig.bloom.stateFinite() || !rig.cloud.stateFinite()) {
                out.finite = false;
            }
        }
        out.spawned = rig.bloom.getSpawnedChildCount();
        out.completed = rig.bloom.getCompletedChildCount();
        double acc = 0.0;
        for (const float x : out.left) {
            acc += static_cast<double>(x) * static_cast<double>(x);
        }
        out.rms = static_cast<float>(std::sqrt(acc / static_cast<double>(out.left.size())));
        return out;
    };

    for (const std::uint32_t seed : kSeeds) {
        const RunResult bloomRun = render(seed, kChildSlots);
        const RunResult refRun = render(seed, 0);

        INFO("seed = " << seed << ", spawned = " << bloomRun.spawned
                       << ", completed = " << bloomRun.completed
                       << ", instants = " << bloomRun.instants.size());

        // ---- fixture preconditions -----------------------------------------
        // Overview fact 1: a child at or above the cloud's active count is
        // silently inaudible, so this criterion would be measuring silence.
        REQUIRE(bloomRun.activeCount == kSlots);
        REQUIRE(refRun.activeCount == kSlots);
        // AND THE RENDER MUST BE AUDIBLE. processFrame() RETURNS EARLY on any
        // frame whose RMS is below energyThresholdDb = -60 dB
        // (artifact_detection.h:203-205), so a silent render scores 0 detections
        // and every clause below would pass while measuring nothing. Measured
        // here: 0.336 (-9.5 dB) with a peak of 0.85, so -26 dB is a floor with
        // 16 dB of margin under the measurement and 34 dB over the detector's.
        INFO("rms: bloom = " << bloomRun.rms << ", reference = " << refRun.rms);
        REQUIRE(bloomRun.rms > 0.05f);
        REQUIRE(refRun.rms > 0.05f);
        REQUIRE(bloomRun.finite);
        REQUIRE(refRun.finite);
        REQUIRE(bloomRun.returnsOk);
        REQUIRE(refRun.returnsOk);
        // The reference must be a genuine no-bloom run, and the bloom run must
        // actually bloom - otherwise both clauses below are vacuous.
        REQUIRE(refRun.spawned == std::uint64_t{0});
        REQUIRE(bloomRun.spawned >= std::uint64_t{2});
        // Every child that was born also died inside the render, so the death
        // windows below cover the whole population rather than a prefix of it.
        REQUIRE(bloomRun.completed == bloomRun.spawned);
        REQUIRE(bloomRun.instants.size() >= std::size_t{3});  // 2 spawns + >= 1 death

        // ---- the two runs are the SAME render until the first child exists --
        // Before engagement applyOutput early-returns without touching either
        // array (S3.4), so the cloud receives byte-identical targets in both
        // runs and the renders are bit-identical up to the first spawn. This is
        // a WITHIN-RUN structural identity, not a checked-in float golden: it
        // proves the differential comparison below is apples-to-apples, and it
        // fails loudly if the two rigs ever drift apart in configuration.
        const std::size_t firstSpawn = bloomRun.instants.front();
        REQUIRE(firstSpawn == kTriggerChunkA * BloomEngine::kControlChunkSamples);
        REQUIRE(std::memcmp(bloomRun.left.data(), refRun.left.data(),
                            firstSpawn * sizeof(float)) == 0);
        REQUIRE(std::memcmp(bloomRun.right.data(), refRun.right.data(),
                            firstSpawn * sizeof(float)) == 0);

        // ---- clause 1: the DIFFERENTIAL gate over the whole render ----------
        const std::size_t bloomL = countClicks(bloomRun.left, kClickSigma);
        const std::size_t bloomR = countClicks(bloomRun.right, kClickSigma);
        const std::size_t refL = countClicks(refRun.left, kClickSigma);
        const std::size_t refR = countClicks(refRun.right, kClickSigma);
        INFO("full render at sigma " << kClickSigma << ": bloom L/R = " << bloomL << "/" << bloomR
                                     << ", reference L/R = " << refL << "/" << refR);
        if (bloomL > refL || bloomR > refR) {
            // Attribution, not a remedy: the answer to a miss is to find what
            // the bloom added, never to raise kClickSigma.
            WARN("SC-001 differential gate missed. smallestZeroSigma bloom L/R = "
                 << smallestZeroSigma(bloomRun.left) << "/" << smallestZeroSigma(bloomRun.right)
                 << ", reference L/R = " << smallestZeroSigma(refRun.left) << "/"
                 << smallestZeroSigma(refRun.right));
        }
        REQUIRE(bloomL <= refL);
        REQUIRE(bloomR <= refR);

        // ---- clause 2: ZERO detections in the 200 ms after every instant ----
        for (const std::size_t instant : bloomRun.instants) {
            // The transition was observed after the chunk starting at `instant`
            // was rendered, so the window opens at that chunk's FIRST sample and
            // covers the chunk the child was latched or retired on.
            REQUIRE(instant + kWindowSamples <= kRenderSamples);
            const std::size_t hitsL =
                countClicksSpan(bloomRun.left.data() + instant, kWindowSamples, kClickSigma);
            const std::size_t hitsR =
                countClicksSpan(bloomRun.right.data() + instant, kWindowSamples, kClickSigma);
            // The reference's SAME window, reported alongside: if it also fires,
            // the detection is the carrier's beating and not the bloom's, and
            // the fixture - not the threshold - is what needs revisiting.
            const std::size_t refHitsL =
                countClicksSpan(refRun.left.data() + instant, kWindowSamples, kClickSigma);
            const std::size_t refHitsR =
                countClicksSpan(refRun.right.data() + instant, kWindowSamples, kClickSigma);
            INFO("200 ms window at sample " << instant << " ("
                                            << static_cast<double>(instant) / kSampleRate
                                            << " s): bloom L/R = " << hitsL << "/" << hitsR
                                            << ", reference L/R = " << refHitsL << "/"
                                            << refHitsR);
            REQUIRE(hitsL == std::size_t{0});
            REQUIRE(hitsR == std::size_t{0});
        }
    }
}

// ==============================================================================
// T015 - BloomEngine_SampleRateIndependence (SC-010)
// ==============================================================================
// FR-041 / FR-030 / FR-004: the FR-041 clock's mean inter-event time is a number
// of SECONDS, a child's fade-in is a number of SECONDS, and prepare() is a full
// re-initialisation. None of the three may move when the host rate does.
//
// THE THREE ARMS, and what each one would catch:
//   (a) EVENT RATE. The per-step probability is rate-normalised
//       (`spawnRateHz_ * gate * kControlChunkSamples * invSampleRate_`,
//       bloom_engine.h:947-948). Drop the `invSampleRate_` factor - or compute
//       the probability per BLOCK instead of per control chunk - and the event
//       rate scales with the host rate. That defect is invisible at a single
//       sample rate, which is why this arm exists at five.
//   (b) TIMING. Seconds are DERIVED from an integer control-step counter
//       (bloom_engine.h:795-797, :1064-1077); the falsification arm (d) below
//       measures what accumulating them in float instead would cost.
//   (c) RE-PREPARE. prepare() steps (1)-(7) re-derive the two rate scalars and
//       call reset() last (bloom_engine.h:383-407). A child that survived that
//       would be running a lifecycle latched in the OLD rate's steps, i.e. it
//       would retime mid-flight - which FR-033 forbids and which the spec's
//       "Sample-rate change mid-life" edge case names explicitly.
//
// WHY THE EVENT ARM RUNS WITH numChildSlots = 0. peekSlot() returns false when
// reserveBase() >= capacity_ (bloom_engine.h:1146-1149), so no child is ever
// placed, engaged_ stays false and applyOutput() early-returns
// (bloom_engine.h:1526-1528): the run is a BARE CLOCK. runEvent() still
// increments spawnEvents_ and parentScans_ before anything else
// (bloom_engine.h:1390-1391), so the event stream is fully observable while the
// per-step cost is a draw, a ramp step and sixteen Idle tests. parentCount is 0
// as well, so scanEnd is 0 and the FR-014 early return fires immediately -
// nothing in the parent arrays is even read.
//
// WHY 2 000 EVENTS AND WHY THE BAND IS WRITTEN AS AN EXPRESSION. The spec's own
// arithmetic (SC-010): inter-event time is geometric, so the sample mean of N
// draws has relative standard error 1/sqrt(N). At N = 200 the +/-10 % band is
// 1.41 sigma and roughly one seed in six fails per rate - a red that says
// nothing, and whose cheapest "fix" is a new seed, which FR-072 forbids. At
// N = 2 000 the standard error is 2.24 % and the band below, written as
// `1/spawnRateHz * (1 +/- 4/sqrt(N))`, is 4 sigma (8.94 %). The 4 and the
// sqrt(N) are in the source so the arithmetic is auditable rather than folded
// into a magic +/-8.94 %.
//
// ONE SEED FOR ALL FIVE RATES, DELIBERATELY. clockRng_ is seeded from seed_
// alone (bloom_engine.h:422, :460), so the five runs walk the SAME uniform
// sequence and differ only in the threshold `p` they test it against - and
// because p is smaller at a higher rate, the 192 kHz events are a strict
// subsequence of the 44.1 kHz ones. That is exactly the comparison the
// criterion wants (the same draws, rate-normalised) rather than five unrelated
// samples, and it also means a seed-level outlier fails all five arms together
// instead of producing one confusing single-rate red. If this arm ever goes
// red: the answer is to find what moved in the probability expression, NEVER to
// try another seed (FR-072 stop-and-surface).
//
// FALSIFICATION (tasks.md T015), RUN IN-TEST RATHER THAN BY MUTATION, AND THE
// RESULT IS RATE-DEPENDENT. The proposed mutation is "accumulate
// `elapsedSeconds += controlDtSec_` instead of counting integer steps". That is
// a change to bloom_engine.h, which this task may not touch, so section (d)
// evaluates BOTH designs' arithmetic directly, on the engine's own two float
// scalars, over the engine's own latched step count - and asserts that the
// shipped derivation holds the band while the accumulating one breaks it.
// DERIVED (IEEE-754 binary32, round-to-nearest, sequential accumulation pinned
// with a volatile accumulator so no toolchain may reassociate it), at the 300 s
// maximum fade-in, as relative error against the exact step/rate quotient:
//     44 100 Hz  206 719 steps   derived 0.0000 %   accumulated 0.052 %
//     48 000 Hz  225 000 steps   derived 0.0000 %   accumulated 0.042 %
//     88 200 Hz  413 438 steps   derived 0.0000 %   accumulated 0.495 %
//     96 000 Hz  450 000 steps   derived 0.0000 %   accumulated 0.333 %
//    192 000 Hz  900 000 steps   derived 0.0000 %   accumulated 0.521 %
// So the falsification FIRES, but only at 192 kHz (0.521 % against the 0.5 %
// band) and only just misses at 88.2 kHz. Plan R1's "~0.67 %" is the WORST-CASE
// forward error bound n*u (n = 225 000 additions, u = 2^-24), not the
// round-to-nearest outcome, which is smaller and non-monotone in n because the
// per-addition rounding errors partly cancel. Two consequences worth stating
// plainly rather than leaving for a later reader to rediscover:
//   * 192 kHz is LOAD-BEARING in this criterion. Drop it from the rate list and
//     the timing arm can no longer distinguish the integer clock from the
//     accumulating one at all - which is precisely the spec's reason for naming
//     192 kHz ("the real 192 kHz risks are timing drift and event-rate drift,
//     and those are what SC-010 measures at that rate").
//   * the +/-0.5 % band is not slack to be tightened away either: the shipped
//     derivation sits at ~1e-6 % of it, so the band is doing no work for the
//     correct design and all of its work as a falsifier at the top rate.
// The numbers above are re-derived by section (d) on every run, so they are a
// record of a computation the test repeats rather than a claim to be trusted.
//
// TAGGING. [long] as the spec and tasks.md both specify. The dominant cost is
// arm (a): 2 000 events at one per 20 s is 40 000 s of simulated time per rate,
// which is sum(rate)/64 * 40 000 = 293 million control steps across the five.
// Arm (b) adds 2.5 million and arms (c)/(d) are negligible. The case is
// toolchain-INDEPENDENT (integer step counts, a 4-sigma statistical band, and a
// binary32 computation pinned by a volatile), which is the second half of the
// roadmap's [long] rule.
// ==============================================================================
TEST_CASE("BloomEngine_SampleRateIndependence", "[bloom_engine][long]") {
    using Krate::DSP::BloomEngine;

    // The five rates of SC-010. Held in DOUBLE, and every derived quantity in
    // this case (control rate, seconds per step, elapsed seconds) is computed in
    // double as well, so the MEASUREMENT never shares the float arithmetic it is
    // measuring - the one exception is section (d), whose whole subject is that
    // float arithmetic.
    constexpr std::array<double, 5> kRates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};

    /// ONE seed for every rate and every arm (see the block comment).
    constexpr std::uint32_t kSeed = 0x5C010A01u;

    /// @brief Seconds -> control steps at an ARBITRARY rate, mirroring
    ///        BloomEngine::fadeSecondsToSteps (bloom_engine.h:1284-1287).
    ///
    /// The TU-level fadeStepsOf() cannot be used here: it is pinned to
    /// kControlRateHz (750 Hz, i.e. 48 kHz), and this case is the one that
    /// leaves 48 kHz. Computed in double and rounded once; section (d) checks
    /// this double form against the engine's float form at every rate rather
    /// than assuming the two agree.
    const auto fadeSteps = [](double seconds, double rate) -> std::size_t {
        const double controlRateHz =
            rate / static_cast<double>(BloomEngine::kControlChunkSamples);
        const long long steps = std::llround(seconds * controlRateHz);
        return static_cast<std::size_t>(std::max<long long>(1LL, steps));
    };

    // ==========================================================================
    // (a) The FR-041 clock's mean inter-event time, at five rates
    // ==========================================================================
    SECTION("SC-010 (a): mean inter-event time is 1/spawnRateHz at every rate") {
        constexpr std::uint64_t kEvents = 2000u;
        // THE BAND, AS AN EXPRESSION. 4 standard errors of the mean of N
        // geometric draws; at N = 2 000 this is 8.94 %.
        const double kBand = 4.0 / std::sqrt(static_cast<double>(kEvents));
        // The rate is the MAXIMUM, so one event costs 20 s of simulated time
        // rather than the 240 s of the default - which is what makes 2 000
        // events affordable at all.
        const double kExpectedSeconds = 1.0 / static_cast<double>(BloomEngine::kMaxSpawnRateHz);
        // Steps advanced per processChunk() call. Coarse on purpose: the engine
        // advances `floor((N + phase) / 64)` steps for N samples
        // (bloom_engine.h:903-915) and the phase is 0 at every call boundary
        // here, so a call of 512 * 64 samples is exactly 512 control steps and
        // the per-call overhead is amortised 512-fold. The cost is that the
        // 2 000th event is located only to within one call - 512 steps out of 13
        // to 120 million, i.e. below 1e-5 of the measured mean, against a band
        // of 8.94e-2.
        constexpr std::size_t kStepsPerCall = 512;

        for (const double rate : kRates) {
            INFO("rate = " << rate << " Hz");

            BloomEngine engine;
            engine.setSeed(kSeed);
            // Both BEFORE prepare(), so prepare()'s depthRamp_.snapTo(depth_)
            // (bloom_engine.h:399-400) leaves no 50 ms ramp window at t = 0 - a
            // window in which the gate is strictly below 1 and the measured rate
            // would be biased low by its first 37 steps.
            engine.setDepth(1.0f);
            engine.setWake(1.0f);
            engine.prepare(rate, BloomEngine::PrepareConfig{.capacity = kSlots,
                                                            .numChildSlots = 0});
            engine.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);

            // Preconditions, stated rather than assumed: SC-010 is specified AT
            // depth = 1 and wake = 1, because FR-042 and FR-035 scale the same
            // per-step probability and 1/spawnRateHz is the expectation ONLY
            // there.
            REQUIRE(engine.isPrepared());
            REQUIRE(engine.getSampleRate() == rate);
            REQUIRE(engine.getSmoothedDepth() == 1.0f);
            REQUIRE(engine.getWakeAmount() == 1.0f);
            REQUIRE_FALSE(engine.isDormant());
            REQUIRE(engine.getSpawnRateHz() == BloomEngine::kMaxSpawnRateHz);
            REQUIRE(engine.numChildSlots() == std::size_t{0});

            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillOneParent(ratios.data(), amplitudes.data(), kSlots, 0.0f);

            const double controlRateHz =
                rate / static_cast<double>(BloomEngine::kControlChunkSamples);
            // Twice the expected total, so a clock that has stopped or slowed
            // fails in bounded time instead of hanging the suite.
            const std::size_t kStepCap = static_cast<std::size_t>(
                2.0 * kExpectedSeconds * static_cast<double>(kEvents) * controlRateHz);

            std::size_t steps = 0;
            bool returnMismatch = false;
            while (engine.getSpawnEventCount() < kEvents && steps < kStepCap) {
                // parentCount 0: scanEnd is 0, FR-014 fires, nothing is read.
                const std::size_t returned =
                    engine.processChunk(ratios.data(), amplitudes.data(), std::size_t{0},
                                        kStepsPerCall * BloomEngine::kControlChunkSamples);
                // Accumulated rather than REQUIREd per iteration: this loop runs
                // up to 234 000 times per rate, and a per-iteration assertion
                // would cost more than the engine it is measuring.
                returnMismatch = returnMismatch || (returned != std::size_t{0});
                steps += kStepsPerCall;
            }
            REQUIRE_FALSE(returnMismatch);

            const std::uint64_t observed = engine.getSpawnEventCount();
            REQUIRE(observed >= kEvents);
            // Free cross-checks on the same run: FR-013's identity, and the
            // proof that this really was a bare clock.
            REQUIRE(engine.getParentScanCount() == observed);
            REQUIRE(engine.getDiscardedEventCount() == std::uint64_t{0});
            REQUIRE(engine.getOfferedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getLiveChildCount() == std::size_t{0});
            REQUIRE_FALSE(engine.isEngaged());

            // The last event landed somewhere inside the final call, so the
            // midpoint of that call is the unbiased estimator of the elapsed
            // time; the residual uncertainty is +/-256 steps.
            const double midSteps =
                static_cast<double>(steps) - 0.5 * static_cast<double>(kStepsPerCall);
            const double elapsedSeconds = midSteps / controlRateHz;
            const double meanInterEvent = elapsedSeconds / static_cast<double>(observed);
            const double lower = kExpectedSeconds * (1.0 - kBand);
            const double upper = kExpectedSeconds * (1.0 + kBand);
            INFO("observed " << observed << " events over " << elapsedSeconds
                             << " s; mean inter-event = " << meanInterEvent << " s, band = ["
                             << lower << ", " << upper << "] s");
            REQUIRE(meanInterEvent >= lower);
            REQUIRE(meanInterEvent <= upper);
        }
    }

    // ==========================================================================
    // (b) A child's fade-in duration IN SECONDS, at five rates
    // ==========================================================================
    SECTION("SC-010 (b): fade-in duration in seconds is within +/-0.5 % at every rate") {
        constexpr double kTolerance = 0.005;
        // The 45 s default and the 300 s kMaxFadeInSeconds. The long one is the
        // case plan R1 is about: it is where an accumulating clock drifts.
        constexpr std::array<double, 2> kFadeSeconds{45.0, 300.0};
        // A SMALL capacity on purpose. This arm runs 2.5 million engaged control
        // steps, and applyOutput() repads [parentCount, capacity) on every one
        // of them (bloom_engine.h:1543-1560); capacity 8 makes that seven slots
        // instead of sixty-four. Nothing in this arm depends on the width of the
        // spectrum - the subject is one child's clock.
        constexpr std::size_t kCapacity = 8;
        constexpr std::size_t kChildSlots = 1;
        constexpr std::size_t kParents = 1;

        for (const double rate : kRates) {
            for (const double configured : kFadeSeconds) {
                INFO("rate = " << rate << " Hz, configured fade-in = " << configured << " s");

                BloomEngine engine;
                engine.setSeed(kSeed);
                engine.setDepth(1.0f);
                engine.setWake(1.0f);
                engine.prepare(rate, BloomEngine::PrepareConfig{.capacity = kCapacity,
                                                                .numChildSlots = kChildSlots});
                // The internal clock OFF: triggerBloom() is the only event
                // source, so the child is latched on a KNOWN control step and
                // the step count below is the fade and nothing else.
                engine.setSpawnRateHz(0.0f);
                engine.setChildGain(1.0f);
                engine.setChildrenPerEvent(1);
                // Jitter 0 so holdSteps is exactly round(holdSec * controlRate):
                // the FadeIn -> Hold transition this arm times must exist, and
                // FR-036's jitter is SC-002's subject, not this one's.
                engine.setHoldJitterFraction(0.0f);
                engine.setFadeInSeconds(static_cast<float>(configured));
                engine.setHoldSeconds(1.0f);
                engine.setFadeOutSeconds(1.0f);
                // Octave only: one parent at ratio 1.0 gives the candidate 2.0,
                // which is 1 200 cents from the only occupied log-ratio and is
                // therefore accepted on its first attempt, with no draw-dependent
                // outcome anywhere in the arm.
                engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
                engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.0f);
                engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 0.0f);

                REQUIRE(engine.getFadeInSeconds() == static_cast<float>(configured));
                REQUIRE(engine.numChildSlots() == kChildSlots);
                REQUIRE(engine.getSmoothedDepth() == 1.0f);

                std::array<float, kSlots> ratios{};
                std::array<float, kSlots> amplitudes{};
                fillOneParent(ratios.data(), amplitudes.data(), kSlots, 1.0f);

                engine.triggerBloom();
                const std::size_t firstReturn =
                    engine.processChunk(ratios.data(), amplitudes.data(), kParents,
                                        BloomEngine::kControlChunkSamples);
                REQUIRE(firstReturn == kCapacity);
                REQUIRE(engine.getSpawnEventCount() == std::uint64_t{1});
                REQUIRE(engine.getLiveChildCount() == std::size_t{1});

                const std::size_t idx = soleLiveChild(engine);
                REQUIRE(idx < BloomEngine::kMaxChildren);
                // The latch's own state, asserted as the point the duration
                // below is measured FROM: step 0, amplitude 0.
                REQUIRE(engine.getChildPhase(idx) == BloomEngine::Phase::FadeIn);
                REQUIRE(engine.getChildAmplitude(idx) == 0.0f);
                REQUIRE(engine.getChildElapsedSeconds(idx) == 0.0f);

                const std::size_t expectedSteps = fadeSteps(configured, rate);
                const std::size_t stepCap = expectedSteps + 16;
                std::size_t stepsInFade = 0;
                bool returnMismatch = false;
                while (engine.getChildPhase(idx) == BloomEngine::Phase::FadeIn &&
                       stepsInFade < stepCap) {
                    const std::size_t returned =
                        engine.processChunk(ratios.data(), amplitudes.data(), kParents,
                                            BloomEngine::kControlChunkSamples);
                    returnMismatch = returnMismatch || (returned != kCapacity);
                    ++stepsInFade;
                }
                REQUIRE_FALSE(returnMismatch);
                REQUIRE(engine.getChildPhase(idx) == BloomEngine::Phase::Hold);

                // THE INTEGER STATEMENT, and it is an exact one: advanceChildren()
                // leaves FadeIn on the step where c.step reaches fadeInSteps
                // (bloom_engine.h:1071-1077), so the number of control steps
                // observed IS the latched bound. An integer equality is not a
                // float golden - it is the whole content of plan S5.1.
                REQUIRE(stepsInFade == expectedSteps);

                const double controlRateHz =
                    rate / static_cast<double>(BloomEngine::kControlChunkSamples);
                // Two independent readings of the same duration: the one this
                // test counted, and the one the engine reports through its own
                // step -> seconds conversion. The second is what a caller sees,
                // and it is what an accumulating implementation would corrupt.
                const double measuredSeconds = static_cast<double>(stepsInFade) / controlRateHz;
                const double reportedSeconds =
                    static_cast<double>(engine.getChildElapsedSeconds(idx));
                const double measuredError = std::fabs(measuredSeconds - configured) / configured;
                const double reportedError = std::fabs(reportedSeconds - configured) / configured;
                INFO("steps in fade = " << stepsInFade << " (expected " << expectedSteps
                                        << "), counted = " << measuredSeconds << " s ("
                                        << measuredError * 100.0 << " %), engine-reported = "
                                        << reportedSeconds << " s (" << reportedError * 100.0
                                        << " %)");
                REQUIRE(measuredError <= kTolerance);
                REQUIRE(reportedError <= kTolerance);
            }
        }
    }

    // ==========================================================================
    // (c) Re-prepare mid-lifecycle
    // ==========================================================================
    SECTION("SC-010 (c): re-preparing mid-lifecycle leaves the exact post-prepare state") {
        constexpr std::size_t kCapacity = kSlots;
        constexpr std::size_t kChildSlots = 4;
        constexpr double kFirstRate = 48000.0;
        // BOTH forms of the operation: a re-prepare at the SAME rate (a host
        // stop/start) and one at a NEW rate (a host rate change). They exercise
        // different halves of prepare() - the second re-derives controlDtSec_
        // and controlRateHz_ as well - and the post-state must be identical in
        // kind either way.
        constexpr std::array<double, 2> kSecondRates{48000.0, 96000.0};

        for (const double secondRate : kSecondRates) {
            INFO("re-prepare " << kFirstRate << " Hz -> " << secondRate << " Hz");

            BloomEngine engine;
            engine.setSeed(kSeed);
            engine.prepare(kFirstRate, BloomEngine::PrepareConfig{.capacity = kCapacity,
                                                                  .numChildSlots = kChildSlots});
            engine.setSpawnRateHz(0.0f);
            engine.setChildGain(1.0f);
            engine.setChildrenPerEvent(2);
            engine.setParentCount(2);
            engine.setHoldJitterFraction(0.0f);
            engine.setFadeInSeconds(45.0f);
            engine.setHoldSeconds(120.0f);
            engine.setFadeOutSeconds(180.0f);
            // A NON-DEFAULT tilt, carried purely so the FR-004 "configuration
            // survives prepare()" clause below has something to prove.
            engine.setConsumerTiltDb(-3.0f);
            engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
            engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.0f);
            engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 0.0f);

            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillTwoParents(ratios.data(), amplitudes.data(), kSlots);

            // ---- get two children genuinely half-faded ----------------------
            engine.triggerBloom();
            const std::size_t r0 = engine.processChunk(ratios.data(), amplitudes.data(),
                                                       std::size_t{2},
                                                       BloomEngine::kControlChunkSamples);
            REQUIRE(r0 == kCapacity);
            REQUIRE(engine.getLiveChildCount() == std::size_t{2});
            REQUIRE(engine.isEngaged());

            // 3 000 steps is 4 s at 48 kHz, ~9 % into the 45 s fade-in: far
            // enough that every child amplitude is strictly positive, which is
            // what makes "no half-faded child" a claim with content.
            constexpr std::size_t kMidSteps = 3000;
            const std::size_t rMid = stepEngine(engine, ratios.data(), amplitudes.data(),
                                                std::size_t{2}, kMidSteps);
            REQUIRE(rMid == kCapacity);
            std::size_t halfFaded = 0;
            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                if (engine.getChildPhase(i) == BloomEngine::Phase::FadeIn &&
                    engine.getChildAmplitude(i) > 0.0f) {
                    ++halfFaded;
                }
            }
            REQUIRE(halfFaded == std::size_t{2});
            REQUIRE(engine.getSpawnEventCount() == std::uint64_t{1});
            REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{2});

            // ---- the re-prepare ---------------------------------------------
            engine.prepare(secondRate, BloomEngine::PrepareConfig{.capacity = kCapacity,
                                                                  .numChildSlots = kChildSlots});

            REQUIRE(engine.isPrepared());
            REQUIRE(engine.getSampleRate() == secondRate);
            REQUIRE(engine.getSeed() == kSeed);  // FR-004: the seed survives
            REQUIRE(engine.getLiveChildCount() == std::size_t{0});
            REQUIRE_FALSE(engine.isEngaged());   // FR-051's sticky latch cleared
            REQUIRE(engine.stateFinite());
            REQUIRE(engine.getSmoothedDepth() == engine.getDepth());  // snapped, not ramping

            // NO HALF-FADED CHILD - over the WHOLE table, not just the two
            // entries that were live, so a stale entry anywhere is caught.
            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                REQUIRE(engine.getChildPhase(i) == BloomEngine::Phase::Idle);
                REQUIRE(engine.getChildAmplitude(i) == 0.0f);
                REQUIRE(engine.getChildTargetAmplitude(i) == 0.0f);
                REQUIRE(engine.getChildElapsedSeconds(i) == 0.0f);
                REQUIRE(engine.getChildHoldSeconds(i) == 0.0f);
                REQUIRE(engine.getChildSlotIndex(i) == kSlots);  // the impossible index
                REQUIRE_FALSE(engine.getIsChildFallback(i));
            }

            // EVERY counter zeroed (bloom_engine.h:436-446).
            REQUIRE(engine.getSpawnEventCount() == std::uint64_t{0});
            REQUIRE(engine.getDiscardedEventCount() == std::uint64_t{0});
            REQUIRE(engine.getParentScanCount() == std::uint64_t{0});
            REQUIRE(engine.getOfferedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getRefusedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getFallbackChildCount() == std::uint64_t{0});
            REQUIRE(engine.getCompletedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getRejectedSpawnCount() == std::uint64_t{0});
            REQUIRE(engine.getOverlapEngagementCount() == std::uint32_t{0});
            REQUIRE(engine.getLastParentSelectionCount() == std::size_t{0});

            // FR-004: every configuration scalar survives.
            REQUIRE(engine.getChildGain() == 1.0f);
            REQUIRE(engine.getFadeInSeconds() == 45.0f);
            REQUIRE(engine.getHoldSeconds() == 120.0f);
            REQUIRE(engine.getFadeOutSeconds() == 180.0f);
            REQUIRE(engine.getHoldJitterFraction() == 0.0f);
            REQUIRE(engine.getConsumerTiltDb() == -3.0f);
            REQUIRE(engine.getSpawnRateHz() == 0.0f);
            REQUIRE(engine.getParentCount() == std::size_t{2});
            REQUIRE(engine.getChildrenPerEvent() == std::size_t{2});
            REQUIRE(engine.capacity() == kCapacity);
            REQUIRE(engine.numChildSlots() == kChildSlots);

            // DISENGAGED AGAIN, not merely counter-zeroed: applyOutput() must
            // early-return, which means the caller's arrays come back byte-for-
            // byte and the returned count is the caller's own parentCount.
            fillTwoParents(ratios.data(), amplitudes.data(), kSlots);
            const std::array<float, kSlots> ratiosBefore = ratios;
            const std::array<float, kSlots> amplitudesBefore = amplitudes;
            const std::size_t rIdle = engine.processChunk(ratios.data(), amplitudes.data(),
                                                          std::size_t{2},
                                                          BloomEngine::kControlChunkSamples);
            REQUIRE(rIdle == std::size_t{2});
            // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            REQUIRE(std::memcmp(ratios.data(), ratiosBefore.data(), sizeof(ratios)) == 0);
            REQUIRE(std::memcmp(amplitudes.data(), amplitudesBefore.data(),
                                sizeof(amplitudes)) == 0);
            // NOLINTEND(bugprone-suspicious-memory-comparison)

            // ---- and it spawns again FROM THE TOP ---------------------------
            // FR-056: reset() rewinds cursor_ to reserveBase(), so the first
            // child of the first post-prepare event takes the FIRST owned slot.
            // Before the re-prepare the cursor had advanced past two slots; a
            // reset that missed it would place this child at reserveBase() + 2.
            engine.triggerBloom();
            const std::size_t rNew = engine.processChunk(ratios.data(), amplitudes.data(),
                                                         std::size_t{2},
                                                         BloomEngine::kControlChunkSamples);
            REQUIRE(rNew == kCapacity);
            REQUIRE(engine.getLiveChildCount() == std::size_t{2});

            const std::size_t base = engine.reserveBase();
            bool sawBaseSlot = false;
            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                if (engine.getChildPhase(i) == BloomEngine::Phase::Idle) {
                    continue;
                }
                // A NEW child, at step 0 - not a survivor resumed.
                REQUIRE(engine.getChildElapsedSeconds(i) == 0.0f);
                REQUIRE(engine.getChildAmplitude(i) == 0.0f);
                // And its lifecycle was latched in the NEW rate's steps: with
                // jitter 0 the latched hold is round(120 s * controlRate) steps,
                // which reads back as 120 s at that rate and would read back as
                // 60 s (or 240 s) had the old rate's scalars survived.
                const double holdSeconds = static_cast<double>(engine.getChildHoldSeconds(i));
                INFO("post-prepare child hold = " << holdSeconds << " s (want 120 s at "
                                                  << secondRate << " Hz)");
                REQUIRE(std::fabs(holdSeconds - 120.0) <= 120.0 * 1.0e-3);
                sawBaseSlot = sawBaseSlot || (engine.getChildSlotIndex(i) == base);
            }
            REQUIRE(sawBaseSlot);
        }
    }

    // ==========================================================================
    // (d) The falsification, evaluated rather than mutated
    // ==========================================================================
    SECTION("SC-010 falsification: accumulated float seconds vs the integer step clock") {
        constexpr double kTolerance = 0.005;    // the SAME +/-0.5 % band as arm (b)
        constexpr double kFadeSeconds = 300.0;  // kMaxFadeInSeconds - plan R1's case

        double worstDerivedDrift = 0.0;
        double worstAccumulatedDrift = 0.0;

        for (const double rate : kRates) {
            // The engine's OWN two float scalars, recomputed from the same two
            // expressions (bloom_engine.h:387-388) rather than read back through
            // a getter, so this section is arithmetic about the design and does
            // not depend on any engine state.
            const float controlRateHzF =
                static_cast<float>(rate) / static_cast<float>(BloomEngine::kControlChunkSamples);
            const float controlDtSecF =
                static_cast<float>(BloomEngine::kControlChunkSamples) / static_cast<float>(rate);

            // The engine's float conversion and this case's double one must
            // agree on the latched step count, or the comparison below would be
            // measuring two different fades.
            const std::size_t steps = fadeSteps(kFadeSeconds, rate);
            const std::size_t engineSteps = static_cast<std::size_t>(
                std::max(1.0f, std::round(static_cast<float>(kFadeSeconds) * controlRateHzF)));
            INFO("rate = " << rate << " Hz, steps = " << steps << " (engine form " << engineSteps
                           << ")");
            REQUIRE(steps == engineSteps);

            // The exact duration those integer steps represent, in double.
            const double exactSeconds = static_cast<double>(steps) *
                                        static_cast<double>(BloomEngine::kControlChunkSamples) /
                                        rate;

            // (i) THE SHIPPED DESIGN: seconds DERIVED from the integer counter,
            //     exactly as getChildElapsedSeconds() does (bloom_engine.h:795-797).
            const double derived = static_cast<double>(static_cast<float>(steps) * controlDtSecF);

            // (ii) THE FALSIFIED DESIGN: `elapsedSeconds += controlDtSec_`, one
            //      addition per control step. The accumulator is VOLATILE so the
            //      loop is a strictly sequential binary32 recurrence on every
            //      toolchain - without it, a vectorising or reassociating
            //      compiler (the -ffast-math legs) would split it into partial
            //      sums, shrink the error and quietly turn this falsification
            //      into a no-op. Simple assignment, never `+=`: compound
            //      assignment to a volatile is deprecated in C++20.
            volatile float accumulator = 0.0f;
            for (std::size_t k = 0; k < steps; ++k) {
                accumulator = accumulator + controlDtSecF;
            }
            const double accumulated = static_cast<double>(accumulator);

            const double derivedDrift = std::fabs(derived - exactSeconds) / exactSeconds;
            const double accumulatedDrift = std::fabs(accumulated - exactSeconds) / exactSeconds;
            worstDerivedDrift = std::max(worstDerivedDrift, derivedDrift);
            worstAccumulatedDrift = std::max(worstAccumulatedDrift, accumulatedDrift);

            // The table the block comment records, re-derived on every run.
            WARN("SC-010 falsification @ " << rate << " Hz: " << steps << " steps, derived = "
                                           << derived << " s (" << derivedDrift * 100.0
                                           << " %), accumulated = " << accumulated << " s ("
                                           << accumulatedDrift * 100.0 << " %)");

            // The shipped design holds the band at EVERY rate.
            REQUIRE(derivedDrift <= kTolerance);
        }

        REQUIRE(worstDerivedDrift <= kTolerance);
        // ...and the accumulating design does NOT, at at least one of the five
        // rates. It is 192 kHz, and only 192 kHz, that breaks it: see the block
        // comment for why that makes the top rate load-bearing rather than
        // decorative, and why neither the band nor the rate list may be trimmed.
        REQUIRE(worstAccumulatedDrift > kTolerance);
    }
}

// ==============================================================================
// T017 - BloomEngine_ThirtyMinuteEvolutionTrajectory (SC-004)
// ==============================================================================
// "Never static, never divergent" (roadmap line 353), measured over a THIRTY
// MINUTE CloudRig render at the SHIPPING defaults - the internal FR-041 clock
// at kDefaultSpawnRateHz (one event per four minutes), the 45 / 120 / 180 s
// lifecycle, kDefaultChildGain and kDefaultChildSlots. This is the only case in
// this TU that does NOT script its events: SC-001 and SC-002 pin the clock off
// and call triggerBloom() at known instants because they measure a SHAPE, and a
// chance spawn would make a failure irreproducible. SC-004 measures the
// unattended TRAJECTORY, so the clock is the subject and scripting it would
// remove the thing under test.
//
// WHAT IS LOGGED, PER SECOND (three series, streaming):
//   * centroidHz - extractAudioFeatures(mono second).centroidHz
//     (audio_features.h:88), the amplitude-weighted spectral centroid;
//   * the partial count - the number of i < cloud.getActivePartialCount()
//     (harmonic_cloud.h:950) whose cloud.getPartialCurrentAmplitude(i) (:959)
//     exceeds -60 dB of the largest such value. READ FROM THE CLOUD, NOT FROM
//     FFT BIN PEAKS: the spec records that the method changes the number
//     materially, and a bin-peak count would additionally merge two partials
//     that share one 23 Hz bin into a single peak;
//   * broadband RMS, in linear amplitude (converted to dB only at the
//     comparison, so the window average is an ENERGY average and not an average
//     of logarithms).
//
// MEMORY: three one-second float buffers (left / right / mono, 48 000 floats
// each = 576 kB in total) plus six 1 800-entry per-second series (~57 kB). The
// 86 400 000-sample render is NEVER materialised - that is the streaming-
// statistics rule, and at 30 minutes stereo it would be 691 MB per run.
//
// THE THREE CLAUSES
//   (a) NEVER STATIC, MEASURED DIFFERENTIALLY. The identical seed and cloud
//       configuration is rendered TWICE - once bloom-engaged, once with
//       numChildSlots = 0 as the reference - and the gate is on the DIFFERENCE:
//       max |centroid_on(t) - centroid_off(t)| >= 5 % of the reference run's
//       mean centroid, SUSTAINED for >= 60 consecutive seconds at or after the
//       first spawn. The absolute "centroid stddev >= 2 % of its mean" form was
//       DELETED from the spec because HarmonicCloud's own Brownian detune,
//       mutation lane and per-partial envelopes move the spectrum continuously
//       at defaults: that threshold passes with the bloom switched off
//       entirely, i.e. it tests nothing this phase builds. The partial-count
//       claim is kept only in its reference-relative form - the bloom run must
//       reach a count the reference NEVER reaches.
//   (b) NEVER DIVERGENT. Broadband RMS over the last five minutes is within
//       +-1.5 dB of minutes 5-10 (the Membrum infinite-ring pattern, roadmap
//       lines 522-524), the peak sample stays below 1.0, and both cloud and
//       engine report stateFinite() on EVERY one of the 1 350 000 control
//       chunks, not merely at the end.
//   (c) SPAWN ACTIVITY IS REAL, POOLED ACROSS SEEDS. The count over 30 minutes
//       at 1/240 Hz is Poisson with lambda = 7.5, for which P(X <= 4) = 0.13: a
//       single-seed ">= 5" floor is a coin flip on the seed and invites
//       seed-shopping instead of investigation. FIVE seeds are pooled against
//       kLambdaTotal, and the floor is 30 (P(fail) < 1 %). kLambdaTotal is
//       DERIVED from kDefaultSpawnRateHz rather than typed as 37.5, so a future
//       default change moves the expectation with it instead of silently
//       invalidating it.
//
// WHY CLAUSE (c) STEPS THE ENGINE WITHOUT THE CLOUD. getSpawnEventCount() is a
// function of the FR-041 Bernoulli clock alone - controlStep() draws
// clockRng_.nextUnipolar() UNCONDITIONALLY and compares it against
// spawnRateHz * gate * kControlChunkSamples * invSampleRate_ - and the cloud
// never writes to the engine or to its arrays. Five more RENDERS would cost
// about five times this case's whole runtime to compute a number that does not
// depend on a single rendered sample. So clause (c) drives the same engine
// configuration and the same parent array through stepEngine(), and the claim
// that this is equivalent is not asserted from the reasoning above but
// MEASURED: seed[0] is the seed that was actually rendered, and its engine-only
// count must equal the rendered run's getSpawnEventCount() EXACTLY. If the
// shortcut is ever wrong, that identity is what fails.
//
// FALSIFICATION (tasks.md T017, to be run and recorded): re-run the bloom arm
// with setDepth(0.0f). The gate collapses to 0, no event ever fires, the engine
// never engages, applyOutput early-returns and the two renders become the SAME
// render - so clause (a)'s sustained differential goes to zero and the case
// must FAIL. A passing run under depth = 0 would mean the differential is
// measuring the cloud's own drift and not the bloom.
//
// COST (Release, 48 kHz): two 1 800-second renders of a 64-partial cloud plus
// 3 600 one-second feature extractions plus five engine-only 30-minute walks.
// Tagged [long] and therefore excluded from the per-push CI filter; it runs
// nightly on all three OSes.
// ==============================================================================
TEST_CASE("BloomEngine_ThirtyMinuteEvolutionTrajectory", "[bloom_engine][long]") {
    using Krate::DSP::BloomEngine;

    // ---- the fixture, in seconds, control chunks and samples ----------------
    constexpr std::size_t kRenderSeconds = 1800;   ///< 30 minutes
    constexpr std::size_t kChunksPerSecond = 750;  ///< 48 000 / 64, exactly
    constexpr std::size_t kSamplesPerSecond = kChunksPerSecond * BloomEngine::kControlChunkSamples;
    static_assert(kSamplesPerSecond == 48000,
                  "the per-second buffers assume this TU's 48 kHz render rate");
    constexpr std::size_t kCapacity = kSlots;
    constexpr std::size_t kChildSlots = BloomEngine::kDefaultChildSlots;

    // ---- the thresholds, all from SC-004 ------------------------------------
    constexpr double kCentroidFraction = 0.05;     ///< (a) 5 % of the reference mean
    constexpr std::size_t kSustainedSeconds = 60;  ///< (a) consecutive seconds
    constexpr double kRmsToleranceDb = 1.5;        ///< (b) +-1.5 dB
    /// (b) peak: the cloud promises only its own output clamp (harmonic_cloud.h:174,
    /// applied :935-936); its FR-017 normaliser pins RMS, not peak, so an absolute
    /// full-scale bound is not a property of this component (FR-023). The bloom
    /// raises crest factor, not energy: measured bloom peak 1.024 against a
    /// reference peak 0.733 (+2.9 dB) with RMS identical. Ruled 2026-09-14.
    constexpr double kPeakCeiling = static_cast<double>(Krate::DSP::HarmonicCloud::kOutputClamp);
    constexpr double kCrestMarginDb = 6.0;         ///< (b) bloom peak <= reference peak + 6 dB
    constexpr std::size_t kEarlyWindowStart = 300;                  ///< (b) minute 5
    constexpr std::size_t kEarlyWindowEnd = 600;                    ///< (b) minute 10
    constexpr std::size_t kLateWindowStart = kRenderSeconds - 300;  ///< (b) last 5 minutes
    /// -60 dB expressed as an AMPLITUDE ratio: 10^(-60/20) = 1e-3.
    /// getPartialCurrentAmplitude returns an amplitude, not a power.
    constexpr double kPartialFloorRatio = 1.0e-3;

    // Five seeds. Each drives BOTH the engine's draws and the cloud's
    // drift / pan / phase, so seed[0]'s pair of renders is one genuine carrier
    // with and without a bloom on top of it.
    constexpr std::array<std::uint32_t, 5> kSeeds{0x5C004001u, 0x5C004002u, 0x5C004003u,
                                                  0x5C004004u, 0x5C004005u};

    /// (c) The POOLED Poisson expectation, DERIVED rather than typed: five seeds
    /// x 1 800 s x kDefaultSpawnRateHz (1/240 Hz) = 37.5 events. The floor below
    /// it is 30, for which P(X < 30 | lambda = 37.5) < 1 %.
    constexpr double kLambdaTotal = static_cast<double>(BloomEngine::kDefaultSpawnRateHz) *
                                    static_cast<double>(kRenderSeconds) *
                                    static_cast<double>(kSeeds.size());
    constexpr std::uint64_t kPooledSpawnFloor = 30;

    // ---- "at defaults", restated by NAME ------------------------------------
    // CloudRig::prepare() routes through prepareTriggerOnly(), whose entire
    // purpose is the opposite of this criterion's: it pins the FR-041 clock OFF,
    // and CloudRig then compresses the lifecycle to 2 / 1 / 3 s so SC-001's ten
    // seeds are affordable. Every one of those overrides is put back here, by
    // the header's own constant and never by a literal, so a future default
    // change moves this fixture with it instead of leaving it measuring a
    // configuration the plugin will never ship.
    const auto applyDefaults = [](BloomEngine& engine) noexcept {
        engine.setSpawnRateHz(BloomEngine::kDefaultSpawnRateHz);
        engine.setChildGain(BloomEngine::kDefaultChildGain);
        engine.setDepth(BloomEngine::kDefaultDepth);
        engine.setFadeInSeconds(BloomEngine::kDefaultFadeInSeconds);
        engine.setHoldSeconds(BloomEngine::kDefaultHoldSeconds);
        engine.setFadeOutSeconds(BloomEngine::kDefaultFadeOutSeconds);
        engine.setHoldJitterFraction(BloomEngine::kDefaultHoldJitterFraction);
        // All three relations live - the header default (relationWeight_ is
        // {1, 1, 1}) and what CloudRig::prepare already restores after
        // prepareTriggerOnly pins Octave-only. Restated so this fixture does not
        // depend on the order of those two.
        engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
        engine.setRelationWeight(BloomEngine::Relation::Fifth, 1.0f);
        engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 1.0f);
    };

    // The fixture IS the shipping configuration - checked, not assumed. Each of
    // these is a setter round-trip on an in-range value, i.e. a within-run
    // structural identity and not a checked-in float golden. setParentCount and
    // setChildrenPerEvent are deliberately NEVER called anywhere in this case:
    // they are asserted here at their construction defaults instead.
    {
        BloomEngine probe;
        prepareTriggerOnly(probe, kSeeds[0], kCapacity, kChildSlots);
        applyDefaults(probe);
        REQUIRE(probe.getSpawnRateHz() == BloomEngine::kDefaultSpawnRateHz);
        REQUIRE(probe.getChildGain() == BloomEngine::kDefaultChildGain);
        REQUIRE(probe.getDepth() == BloomEngine::kDefaultDepth);
        REQUIRE(probe.getFadeInSeconds() == BloomEngine::kDefaultFadeInSeconds);
        REQUIRE(probe.getHoldSeconds() == BloomEngine::kDefaultHoldSeconds);
        REQUIRE(probe.getFadeOutSeconds() == BloomEngine::kDefaultFadeOutSeconds);
        REQUIRE(probe.getHoldJitterFraction() == BloomEngine::kDefaultHoldJitterFraction);
        REQUIRE(probe.getParentCount() == BloomEngine::kDefaultParentCount);
        REQUIRE(probe.getChildrenPerEvent() == BloomEngine::kDefaultChildrenPerEvent);
        REQUIRE(probe.numChildSlots() == kChildSlots);
        REQUIRE(probe.reserveBase() == kCapacity - kChildSlots);
    }

    /// One render's per-second series plus its whole-run scalars. The three
    /// series ARE the streaming statistics; nothing else survives a second.
    struct RunSeries {
        std::vector<double> centroidHz;
        std::vector<double> rmsLinear;
        std::vector<std::size_t> partialCount;
        double peak = 0.0;
        bool finite = true;     ///< cloud AND engine, on every control chunk
        bool returnsOk = true;  ///< FR-005 / S3.1's returned-count contract
        std::uint64_t events = 0;
        std::uint64_t spawnedChildren = 0;
        std::size_t firstSpawnSecond = 0;  ///< kRenderSeconds when none ever spawned
        std::size_t activeCount = 0;
    };

    // One 30-minute render. The rig lives only inside this lambda, so at most
    // one HarmonicCloud exists at a time and the two runs cannot share state.
    const auto render = [&](std::uint32_t seed, std::size_t numChildSlots) {
        RunSeries out;
        out.firstSpawnSecond = kRenderSeconds;
        out.centroidHz.reserve(kRenderSeconds);
        out.rmsLinear.reserve(kRenderSeconds);
        out.partialCount.reserve(kRenderSeconds);

        CloudRig rig;
        rig.prepare(seed, kCapacity, numChildSlots);
        applyDefaults(rig.bloom);
        out.activeCount = rig.cloud.getActivePartialCount();

        std::vector<float> left(kSamplesPerSecond, 0.0f);
        std::vector<float> right(kSamplesPerSecond, 0.0f);
        std::vector<float> mono(kSamplesPerSecond, 0.0f);

        for (std::size_t sec = 0; sec < kRenderSeconds; ++sec) {
            for (std::size_t c = 0; c < kChunksPerSecond; ++c) {
                const std::size_t at = c * BloomEngine::kControlChunkSamples;
                const std::size_t returned = rig.chunk(left.data() + at, right.data() + at);

                // S3.1: a disengaged engine returns the caller's count and an
                // engaged one returns capacity(). The cheapest possible guard
                // against a rig that silently stopped handing the cloud its
                // child slots - without it, a 30-minute render of the carrier
                // alone would satisfy clause (b) perfectly.
                const std::size_t expected = rig.bloom.isEngaged() ? kCapacity : rig.parentCount;
                if (returned != expected) {
                    out.returnsOk = false;
                }
                // Clause (b)'s "throughout": every chunk, not just the last.
                if (!rig.bloom.stateFinite() || !rig.cloud.stateFinite()) {
                    out.finite = false;
                }
            }

            if (out.firstSpawnSecond == kRenderSeconds &&
                rig.bloom.getSpawnedChildCount() > std::uint64_t{0}) {
                // A child was BORN, which is what moves the spectrum; an event
                // that offered nothing placeable would not.
                out.firstSpawnSecond = sec;
            }

            // ---- the second's broadband statistics, in double ---------------
            double sumSq = 0.0;
            for (std::size_t i = 0; i < kSamplesPerSecond; ++i) {
                const double l = static_cast<double>(left[i]);
                const double r = static_cast<double>(right[i]);
                out.peak = std::max(out.peak, std::max(std::fabs(l), std::fabs(r)));
                const double m = 0.5 * (l + r);
                mono[i] = static_cast<float>(m);
                sumSq += m * m;
            }
            out.rmsLinear.push_back(std::sqrt(sumSq / static_cast<double>(kSamplesPerSecond)));

            // ---- the second's spectral centroid -----------------------------
            const Krate::Test::AudioFeatures features =
                Krate::Test::extractAudioFeatures(mono, kSampleRate);
            out.centroidHz.push_back(features.centroidHz);

            // ---- the partial count, READ FROM THE CLOUD ---------------------
            const std::size_t active = rig.cloud.getActivePartialCount();
            double largest = 0.0;
            for (std::size_t i = 0; i < active; ++i) {
                largest = std::max(largest,
                                   static_cast<double>(rig.cloud.getPartialCurrentAmplitude(i)));
            }
            std::size_t above = 0;
            if (largest > 0.0) {
                const double floorAmp = largest * kPartialFloorRatio;
                for (std::size_t i = 0; i < active; ++i) {
                    if (static_cast<double>(rig.cloud.getPartialCurrentAmplitude(i)) > floorAmp) {
                        ++above;
                    }
                }
            }
            out.partialCount.push_back(above);
        }

        out.events = rig.bloom.getSpawnEventCount();
        out.spawnedChildren = rig.bloom.getSpawnedChildCount();
        return out;
    };

    const RunSeries bloomRun = render(kSeeds[0], kChildSlots);
    const RunSeries refRun = render(kSeeds[0], std::size_t{0});  // the no-bloom reference

    // ---- fixture preconditions ---------------------------------------------
    INFO("seed = " << kSeeds[0] << ", bloom events = " << bloomRun.events << ", children = "
                   << bloomRun.spawnedChildren << ", first spawn at second "
                   << bloomRun.firstSpawnSecond << "; reference events = " << refRun.events);
    // Overview fact 1 (harmonic_cloud.h:1469-1473): a child written at or above
    // the cloud's active count is SILENTLY inaudible, so this criterion would be
    // measuring silence rather than a bloom.
    REQUIRE(bloomRun.activeCount == kSlots);
    REQUIRE(refRun.activeCount == kSlots);
    REQUIRE(bloomRun.centroidHz.size() == kRenderSeconds);
    REQUIRE(refRun.centroidHz.size() == kRenderSeconds);
    REQUIRE(bloomRun.returnsOk);
    REQUIRE(refRun.returnsOk);
    // The reference must be a genuine no-bloom run. Its internal clock still
    // FIRES - numChildSlots = 0 refuses every offered child rather than
    // suppressing the event - so the claim is about CHILDREN, never events.
    REQUIRE(refRun.spawnedChildren == std::uint64_t{0});
    REQUIRE(bloomRun.spawnedChildren >= std::uint64_t{1});
    REQUIRE(bloomRun.firstSpawnSecond < kRenderSeconds);

    // ---- clause (a): never static, measured DIFFERENTIALLY ------------------
    double refCentroidSum = 0.0;
    for (const double c : refRun.centroidHz) {
        refCentroidSum += c;
    }
    const double refMeanCentroid = refCentroidSum / static_cast<double>(kRenderSeconds);
    REQUIRE(refMeanCentroid > 0.0);  // a silent reference would make the gate vacuous
    const double centroidThreshold = kCentroidFraction * refMeanCentroid;

    double maxAbsDiff = 0.0;
    std::size_t longestRun = 0;
    std::size_t longestRunStart = 0;
    std::size_t currentRun = 0;
    std::size_t currentRunStart = 0;
    for (std::size_t s = bloomRun.firstSpawnSecond; s < kRenderSeconds; ++s) {
        const double diff = std::fabs(bloomRun.centroidHz[s] - refRun.centroidHz[s]);
        maxAbsDiff = std::max(maxAbsDiff, diff);
        if (diff >= centroidThreshold) {
            if (currentRun == 0) {
                currentRunStart = s;
            }
            ++currentRun;
            if (currentRun > longestRun) {
                longestRun = currentRun;
                longestRunStart = currentRunStart;
            }
        } else {
            currentRun = 0;
        }
    }

    std::size_t maxBloomPartials = 0;
    std::size_t maxRefPartials = 0;
    for (std::size_t s = 0; s < kRenderSeconds; ++s) {
        maxBloomPartials = std::max(maxBloomPartials, bloomRun.partialCount[s]);
        maxRefPartials = std::max(maxRefPartials, refRun.partialCount[s]);
    }

    WARN("SC-004 (a) differential: reference mean centroid = "
         << refMeanCentroid << " Hz, 5 % threshold = " << centroidThreshold
         << " Hz, max |diff| after the first spawn = " << maxAbsDiff
         << " Hz, longest sustained run = " << longestRun << " s (from second " << longestRunStart
         << "), floor = " << kSustainedSeconds << " s; partial count max bloom / reference = "
         << maxBloomPartials << " / " << maxRefPartials);
    REQUIRE(maxAbsDiff >= centroidThreshold);
    REQUIRE(longestRun >= kSustainedSeconds);
    // Reference-relative, per the spec: the absolute ">= 3 distinct values" form
    // is a property of HarmonicCloud and passes with the bloom switched off.
    REQUIRE(maxBloomPartials > maxRefPartials);

    // ---- clause (b): never divergent ----------------------------------------
    // An ENERGY average over the window (mean of squares, then dB), never a mean
    // of decibels - the two differ materially on a series this dynamic.
    const auto windowRmsDb = [](const std::vector<double>& rms, std::size_t from, std::size_t to) {
        double acc = 0.0;
        for (std::size_t s = from; s < to; ++s) {
            acc += rms[s] * rms[s];
        }
        return Krate::Test::linToDbfs(std::sqrt(acc / static_cast<double>(to - from)));
    };
    const double earlyDb = windowRmsDb(bloomRun.rmsLinear, kEarlyWindowStart, kEarlyWindowEnd);
    const double lateDb = windowRmsDb(bloomRun.rmsLinear, kLateWindowStart, kRenderSeconds);
    const double refEarlyDb = windowRmsDb(refRun.rmsLinear, kEarlyWindowStart, kEarlyWindowEnd);
    const double refLateDb = windowRmsDb(refRun.rmsLinear, kLateWindowStart, kRenderSeconds);
    WARN("SC-004 (b) boundedness: bloom RMS minutes 5-10 = "
         << earlyDb << " dBFS, last 5 minutes = " << lateDb << " dBFS, drift = "
         << (lateDb - earlyDb) << " dB (band +-" << kRmsToleranceDb << "); reference " << refEarlyDb
         << " -> " << refLateDb << " dBFS; peak bloom / reference = " << bloomRun.peak << " / "
         << refRun.peak);
    REQUIRE(std::fabs(lateDb - earlyDb) <= kRmsToleranceDb);
    REQUIRE(bloomRun.peak < kPeakCeiling);
    REQUIRE(refRun.peak < kPeakCeiling);
    REQUIRE(refRun.peak > 0.0);
    {
        const double crestRiseDb = 20.0 * std::log10(bloomRun.peak / refRun.peak);
        INFO("(b) bloom peak " << bloomRun.peak << " vs reference peak " << refRun.peak << " = "
                               << crestRiseDb << " dB (margin +" << kCrestMarginDb << " dB)");
        REQUIRE(crestRiseDb <= kCrestMarginDb);
    }
    // "cloud.stateFinite() true throughout" - sampled on every one of the
    // 1 350 000 control chunks of each run, not once at the end.
    REQUIRE(bloomRun.finite);
    REQUIRE(refRun.finite);

    // ---- clause (c): spawn activity is real, POOLED over five seeds ---------
    // The engine alone, at the rendered configuration and over the same parent
    // array (see the block comment for why this is equivalent, and for the
    // identity that MEASURES the equivalence rather than assuming it).
    const auto engineOnlyEventCount = [&](std::uint32_t seed) {
        BloomEngine engine;
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        fillRigParents(ratios.data(), amplitudes.data(), kSlots);
        prepareTriggerOnly(engine, seed, kCapacity, kChildSlots);
        applyDefaults(engine);
        const std::size_t returned = stepEngine(engine, ratios.data(), amplitudes.data(),
                                                kRigParents, kRenderSeconds * kChunksPerSecond);
        REQUIRE(returned <= kCapacity);
        return engine.getSpawnEventCount();
    };

    std::uint64_t pooledEvents = 0;
    for (std::size_t i = 0; i < kSeeds.size(); ++i) {
        const std::uint64_t events = engineOnlyEventCount(kSeeds[i]);
        if (i == 0) {
            // THE EQUIVALENCE, MEASURED. seed[0] is the seed that was actually
            // rendered above, so an engine-only walk that disagrees with the
            // rendered run invalidates the shortcut the other four seeds take -
            // and this is the assertion that says so.
            REQUIRE(events == bloomRun.events);
        }
        WARN("SC-004 (c) seed " << kSeeds[i] << " (engine-only, 30 min): " << events
                                << " spawn events");
        pooledEvents += events;
    }
    WARN("SC-004 (c) pooled over " << kSeeds.size() << " seeds: " << pooledEvents
                                   << " events against lambda_total = " << kLambdaTotal
                                   << ", floor = " << kPooledSpawnFloor);
    REQUIRE(pooledEvents >= kPooledSpawnFloor);
}

// ==============================================================================
// SC-015 (tasks.md T019) - THE EIGHT-HOUR ACCELERATED SOAK
// ==============================================================================
// THE ROADMAP'S BOUNDEDNESS GATE (lines 522-524). A drone component that can run
// away or die overnight is broken by definition, so this case runs EIGHT
// SIMULATED HOURS at SC-011's worst-case configuration - numChildSlots = 16
// (== kMaxChildren, every table entry reachable), parentCount = 48, K = 8,
// childrenPerEvent = 4, spawnRateHz at kMaxSpawnRateHz - over 25 seeds, and
// checks six clauses on EVERY one of the 21 600 000 control steps of each seed.
//
// ACCELERATED MEANS "NO AUDIO". The criterion is about the engine's own state -
// live count, latched targets, emitted ratios, counters - none of which is a
// function of a rendered sample. Driving 25 x 8 h through a HarmonicCloud would
// cost about 5 500 000 000 partial-samples per seed to compute numbers that do
// not depend on a single one of them. So this case steps the SHARED stepEngine()
// fixture (T009) at one 64-sample control chunk per call, which is exactly the
// Phase-10 call shape (seraphis_voice.h:1050-1054) with the cloud removed.
//
// STREAMING STATISTICS ONLY. 540 000 000 control steps cannot each carry a
// Catch2 assertion - every REQUIRE registers an assertion and the run would
// never finish - so the loop accumulates flags and extremes into SoakSeries and
// the REQUIREs are taken once per seed, against those. That is the same shape
// SC-004's RunSeries uses above.
//
// THE PARENT SPECTRUM, AND WHY THE GRID IS 105 CENTS. 48 parents on a geometric
// grid, amplitude 1/(i+1)^2 - strictly descending, so FR-010's strongest-K
// selection is DETERMINISTIC (always parents 0..7) and the candidate set is
// known in advance rather than drawn. The grid spacing is the one free
// parameter, and it is chosen so that FR-022's 24-cent spacing rule does not
// reject the bulk of the candidate set - a fixture in which every candidate is
// refused would satisfy clauses (a)-(d) vacuously and then FAIL clause (e),
// which is the clause that measures that children are actually being born.
// Computed over the grid before this fixture was written, at g = 105 cents:
//   * an OCTAVE image sits 1200 cents above its parent; 1200 = 11.43 g, so the
//     nearest grid partial is 0.43 g = 45 cents away  (> 24);
//   * a FIFTH image sits 702 cents above its parent; 702 = 6.69 g, nearest grid
//     partial 0.31 g = 33 cents away                  (> 24);
//   * an octave image of parent i against a fifth image of parent j differs by
//     498 + g(i - j) cents; 498 = 4.74 g, so the closest such pair is
//     0.26 g = 27 cents apart                         (> 24);
//   * a DETUNED image sits 24..50 cents off its own parent and 55..81 cents off
//     the neighbouring grid partial. Against an octave image the residue would
//     need i - j = -11, and only parents 0..7 are ever selected, so that pair
//     cannot form; against a fifth image it closes below 24 cents only in the
//     narrow band d >= 48, which the FR-026 retry absorbs.
// Every image stays inside [kMinChildRatio, kMaxChildRatio] = [0.5, 128]: the
// largest is 2 * r_47 = 2 * 2^(47 * 105 / 1200) = 34.6, and the smallest is the
// fifth of parent 0 at 1.5.
// A 120- or 100-cent grid would have put every octave image EXACTLY on an
// existing partial (1200 mod g == 0) and refused every octave child; that is
// the failure this arithmetic exists to avoid.
//
// WHY CLAUSE (b) DOES NOT READ getChildSlotIndex(). FR-023's latched target is
// parentAmplitude_at_spawn * childGain * depth / tiltGain(childSlotIndex), and
// this run leaves consumerTiltDb at its 0 default - where tiltGain() takes its
// verbatim identity branch and returns EXACTLY 1.0f for EVERY index
// (bloom_engine.h:1274-1276). The divisor is therefore identically 1 here and
// the slot index does not enter the expectation. The divisor itself is the
// subject of SC-017 (BloomEngine_TiltCompensationMatchesIntendedLevel), not of
// this criterion.
// parentAmplitude_at_spawn is recoverable EXACTLY because the engine never
// writes below reserveBase(): with parentCount == 48 == reserveBase() the FR-051
// gap loop is empty and the owned region is [48, 64), so amplitudes[0..47] hold
// the fixture's values unchanged for the whole eight hours and
// amplitudes[getChildParentIndex(c)] IS the amplitude that was latched.
//
// WHY THERE IS NO ABSOLUTE [0, 1] AMPLITUDE BOUND (spec RN-2). FR-023
// deliberately does not clamp the latched target and HarmonicCloud accepts
// amplitudes above 1 by design, so an absolute bound would either fail on a
// correct engine or pass only because this fixture happens to synthesise
// parents <= 1 - proving nothing. The bound asserted is the RELATIVE one:
// [0, getChildTargetAmplitude(i)].
//
// CLAUSE (e)'s TWO EXPECTATIONS ARE WRITTEN DOWN, NOT LEFT TO THE IMPLEMENTER.
// The configuration is SLOT-SATURATED and the naive rate x time is the wrong
// expectation for the completion count: demand is
// childrenPerEvent * spawnRateHz = 0.2 children/s, while 16 slots at the default
// 45 + 120 + 180 = 345 s lifetime sustain only 16/345 = 0.046 children/s. So the
// expectation is the spec's min(childrenPerEvent * events, numChildSlots * T /
// lifetime) and the test REPORTS WHICH BRANCH IS ACTIVE, so a future reader sees
// the saturation rather than re-deriving it.
// The event count itself is the exact Bernoulli mean (FR-041):
// spawnRateHz * depth * wake * T = 0.05 * 1 * 1 * 28800 = 1440 events, for which
// the Poisson sigma is 37.9 - the +-25 % band is 9.5 sigma, i.e. the band is
// loose enough that a red result is a defect and not a seed.
// The third part of (e) is an EXACT identity rather than a band:
// offered == spawned + refused (plan S14 C-7). Every offered child takes exactly
// one of the two outcomes - the (s0) slot-exhaustion path and the if (!placed)
// tail are the only two refusal increments, and place() is the only spawn
// increment - so this is a within-run structural identity, not a float golden.
//
// CLAUSE (f) IS THE "DIES OVERNIGHT" CLAUSE. A bloom that wedges - every slot
// held by a child that never retires, or a clock that stops arming - passes (a)
// through (d) perfectly while being exactly the failure this phase exists to
// prevent. So the live count may not hold ONE value for more than 60 simulated
// minutes, and the event counter must advance at least once per 60 simulated
// minutes. At the configured rate an event is expected every 20 s, so a 60 min
// gap is about 180 expected events wide.
//
// FALSIFICATION (tasks.md T019, to be run and recorded by the build gate):
// delete the slotMask_ release from BloomEngine::retire() (bloom_engine.h:1025-
// 1033). Slots are then never returned to peekSlot(), so after the first 16
// children every offered child is refused: getLiveChildCount() falls to 0 and
// STAYS there for the remaining ~7.9 hours, and clause (f)'s live-count hold
// fails. liveCount_ is still decremented on that mutation, so clause (a) is
// untouched - which is why the falsification is stated against (a) OR (f).
//
// COST (Release, 48 kHz): 25 x 21 600 000 control steps, no audio. Tagged
// [long] and therefore excluded from the per-push CI filter; it runs nightly on
// all three OSes.
// ==============================================================================
TEST_CASE("BloomEngine_EightHourAcceleratedSoak", "[bloom_engine][long]") {
    using Krate::DSP::BloomEngine;

    // ---- the worst-case fixture (SC-011's configuration) --------------------
    constexpr std::size_t kCapacity = kSlots;                          ///< 64
    constexpr std::size_t kSoakChildSlots = 16;                        ///< == kMaxChildren
    constexpr std::size_t kSoakParents = kCapacity - kSoakChildSlots;  ///< 48
    constexpr std::size_t kSoakParentK = 8;                            ///< K, == kMaxParents
    constexpr std::size_t kSoakChildrenPerEvent = 4;                   ///< == kMaxChildrenPerEvent
    constexpr float kSoakChildGain = 1.0f;  ///< prepareTriggerOnly's explicit override
    constexpr float kSoakDepth = 1.0f;      ///< prepareTriggerOnly's explicit override
    constexpr float kSoakWake = 1.0f;       ///< the construction default
    constexpr float kSoakGridCents = 105.0f;  ///< see the block comment
    static_assert(kSoakChildSlots == BloomEngine::kMaxChildren,
                  "the worst case must reach every entry of the FR-034 table");
    static_assert(kSoakParentK == BloomEngine::kMaxParents, "SC-011's K");
    static_assert(kSoakChildrenPerEvent == BloomEngine::kMaxChildrenPerEvent, "SC-011's N");
    static_assert(kSoakParents + kSoakChildSlots == kCapacity,
                  "parentCount == reserveBase(), so the FR-051 gap loop is empty and the "
                  "parent amplitudes survive the whole run unchanged");

    // ---- the simulated clock ------------------------------------------------
    constexpr std::size_t kChunksPerSecond = 750;  ///< 48 000 / 64, exactly
    constexpr std::size_t kSoakSeconds = 8u * 3600u;                     ///< 8 hours
    constexpr std::size_t kSoakSteps = kSoakSeconds * kChunksPerSecond;  ///< 21 600 000
    /// Clause (f): 60 simulated minutes, in control steps.
    constexpr std::size_t kStallSteps = 60u * 60u * kChunksPerSecond;  ///< 2 700 000

    // ---- the thresholds, all from SC-015 ------------------------------------
    constexpr double kCountBand = 0.25;          ///< (e) +-25 %
    constexpr double kTargetTolerance = 1.0e-6;  ///< (b) the FR-023 latch formula
    /// (e) the default lifetime, by NAME rather than as the literal 345 - a
    /// future default change must move this expectation with it.
    constexpr double kLifetimeSeconds = static_cast<double>(BloomEngine::kDefaultFadeInSeconds) +
                                        static_cast<double>(BloomEngine::kDefaultHoldSeconds) +
                                        static_cast<double>(BloomEngine::kDefaultFadeOutSeconds);
    /// (e) the exact Bernoulli mean of FR-041's clock: rate * depth * wake * T.
    constexpr double kExpectedEvents = static_cast<double>(BloomEngine::kMaxSpawnRateHz) *
                                       static_cast<double>(kSoakDepth) *
                                       static_cast<double>(kSoakWake) *
                                       static_cast<double>(kSoakSeconds);

    // 25 seeds, spread across the std::uint32_t domain rather than consecutive,
    // so a defect that depends on a low bit of the seed cannot hide behind them.
    constexpr std::array<std::uint32_t, 25> kSeeds{
        0x5C015001u, 0x5C015002u, 0x5C015003u, 0x5C015004u, 0x5C015005u, 0x11111111u, 0x22222222u,
        0x33333333u, 0x44444444u, 0x55555555u, 0x66666666u, 0x77777777u, 0x88888888u, 0x99999999u,
        0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu, 0xEEEEEEEEu, 0xFFFFFFFFu, 0x00000001u,
        0x0000FFFFu, 0x7FFFFFFFu, 0x80000000u, 0xB10035EDu};

    /// @brief The soak's parent spectrum (see the block comment for the grid
    ///        arithmetic). Slots at and above kSoakParents carry the cloud's own
    ///        padding form (harmonic_cloud.h:825-826), which is bit-identical to
    ///        what applyOutput() writes into the owned region before the child
    ///        loop - so the engine's first engaged call changes the CHILD SLOTS
    ///        only. Everything it reads besides its two parameters is a constexpr
    ///        of the enclosing scope.
    const auto fillSoakParents = [&](float* ratios, float* amplitudes) noexcept {
        for (std::size_t i = 0; i < kSlots; ++i) {
            ratios[i] = static_cast<float>(i + 1);
            amplitudes[i] = 0.0f;
        }
        for (std::size_t i = 0; i < kSoakParents; ++i) {
            ratios[i] = std::exp2(static_cast<float>(i) * kSoakGridCents / 1200.0f);
            const float n1 = static_cast<float>(i + 1);
            amplitudes[i] = 1.0f / (n1 * n1);
        }
    };

    /// One seed's whole eight hours, compressed to flags and extremes. NOTHING
    /// per-step survives the loop.
    struct SoakSeries {
        // --- clause (a)
        std::size_t maxLive = 0;
        // --- clauses (b) and (c), as flags plus the worst excursion for the report
        bool ampInBand = true;
        bool targetMatches = true;
        bool ratioInBand = true;
        bool parentIndexOk = true;
        double worstAmpOvershoot = 0.0;
        double worstTargetError = 0.0;
        double minRatio = 1.0e30;
        double maxRatio = 0.0;
        // --- clause (d), sampled on EVERY control step and not once at the end
        bool finite = true;
        // --- the S3.1 returned-count contract: the cheapest guard against a run
        //     that silently stopped writing its owned region at all
        bool returnsOk = true;
        // --- clause (f)
        std::size_t longestLiveHoldSteps = 0;
        std::size_t longestEventGapSteps = 0;
        // --- clause (e)
        std::uint64_t events = 0;
        std::uint64_t offered = 0;
        std::uint64_t spawned = 0;
        std::uint64_t refused = 0;
        std::uint64_t completed = 0;
        // --- the fixture itself, checked rather than assumed
        bool configOk = true;
    };

    const auto soak = [&](std::uint32_t seed) {
        SoakSeries out;

        BloomEngine engine;
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        fillSoakParents(ratios.data(), amplitudes.data());

        // prepareTriggerOnly pins the FR-041 clock OFF and Octave-only for
        // SC-002; this criterion wants the opposite of both, so every one of its
        // overrides that matters here is put back by NAME.
        prepareTriggerOnly(engine, seed, kCapacity, kSoakChildSlots);
        engine.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
        engine.setParentCount(kSoakParentK);
        engine.setChildrenPerEvent(kSoakChildrenPerEvent);
        engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
        engine.setRelationWeight(BloomEngine::Relation::Fifth, 1.0f);
        engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 1.0f);

        // The fixture IS the worst case - checked, not assumed. Each of these is
        // a setter round-trip on an in-range value, i.e. a within-run structural
        // identity and not a checked-in float golden. getSmoothedDepth() == 1.0f
        // is the precondition clause (b)'s formula rests on: prepare() SNAPS the
        // depth ramp, so there is no 50 ms window in which a spawn could latch
        // against a partly-ramped depth.
        out.configOk =
            engine.isPrepared() && engine.numChildSlots() == kSoakChildSlots &&
            engine.reserveBase() == kSoakParents && engine.capacity() == kCapacity &&
            engine.getSpawnRateHz() == BloomEngine::kMaxSpawnRateHz &&
            engine.getParentCount() == kSoakParentK &&
            engine.getChildrenPerEvent() == kSoakChildrenPerEvent &&
            engine.getChildGain() == kSoakChildGain && engine.getDepth() == kSoakDepth &&
            engine.getSmoothedDepth() == kSoakDepth && engine.getWakeAmount() == kSoakWake &&
            !engine.isDormant() && engine.getConsumerTiltDb() == 0.0f &&
            engine.getFadeInSeconds() == BloomEngine::kDefaultFadeInSeconds &&
            engine.getHoldSeconds() == BloomEngine::kDefaultHoldSeconds &&
            engine.getFadeOutSeconds() == BloomEngine::kDefaultFadeOutSeconds;

        std::size_t lastLive = engine.getLiveChildCount();
        std::size_t liveHeldSteps = 0;
        std::uint64_t lastEvents = engine.getSpawnEventCount();
        std::size_t eventGapSteps = 0;

        for (std::size_t s = 0; s < kSoakSteps; ++s) {
            const std::size_t returned =
                stepEngine(engine, ratios.data(), amplitudes.data(), kSoakParents,
                           std::size_t{1});

            // S3.1: a disengaged engine returns the caller's count and an engaged
            // one returns capacity().
            const std::size_t expectedReturn = engine.isEngaged() ? kCapacity : kSoakParents;
            if (returned != expectedReturn) {
                out.returnsOk = false;
            }

            // ---- clause (d), on EVERY control step --------------------------
            if (!engine.stateFinite()) {
                out.finite = false;
            }

            // ---- clause (a) -------------------------------------------------
            const std::size_t live = engine.getLiveChildCount();
            if (live > out.maxLive) {
                out.maxLive = live;
            }

            // ---- clauses (b) and (c), over every non-Idle table entry --------
            for (std::size_t c = 0; c < BloomEngine::kMaxChildren; ++c) {
                if (engine.getChildPhase(c) == BloomEngine::Phase::Idle) {
                    continue;
                }
                const float amp = engine.getChildAmplitude(c);
                const float tgt = engine.getChildTargetAmplitude(c);
                const float rat = engine.getChildRatio(c);

                if (amp < 0.0f || amp > tgt) {
                    out.ampInBand = false;
                    const double over = static_cast<double>(amp) - static_cast<double>(tgt);
                    out.worstAmpOvershoot = std::max(out.worstAmpOvershoot, over);
                }

                if (rat < BloomEngine::kMinChildRatio || rat > BloomEngine::kMaxChildRatio) {
                    out.ratioInBand = false;
                }
                const double ratd = static_cast<double>(rat);
                out.minRatio = std::min(out.minRatio, ratd);
                out.maxRatio = std::max(out.maxRatio, ratd);

                const std::size_t parent = engine.getChildParentIndex(c);
                if (parent >= kSoakParents) {
                    // The FR-010 scan never selects at or above reserveBase()
                    // (its scanEnd), so this can only fire on a defect - and
                    // without the guard the expectation below would read past the
                    // parent region.
                    out.parentIndexOk = false;
                    continue;
                }
                // FR-023 / Clarification Q1. tiltGain(slot) is EXACTLY 1 for
                // every slot at consumerTiltDb == 0 (bloom_engine.h:1274-1276),
                // so the divisor does not appear here.
                const double expectedTarget = static_cast<double>(amplitudes[parent]) *
                                              static_cast<double>(kSoakChildGain) *
                                              static_cast<double>(kSoakDepth);
                const double err = std::fabs(static_cast<double>(tgt) - expectedTarget);
                if (err > kTargetTolerance) {
                    out.targetMatches = false;
                }
                out.worstTargetError = std::max(out.worstTargetError, err);
            }

            // ---- clause (f), the two stall watchdogs ------------------------
            if (live == lastLive) {
                ++liveHeldSteps;
                if (liveHeldSteps > out.longestLiveHoldSteps) {
                    out.longestLiveHoldSteps = liveHeldSteps;
                }
            } else {
                lastLive = live;
                liveHeldSteps = 0;
            }

            const std::uint64_t events = engine.getSpawnEventCount();
            if (events == lastEvents) {
                ++eventGapSteps;
                if (eventGapSteps > out.longestEventGapSteps) {
                    out.longestEventGapSteps = eventGapSteps;
                }
            } else {
                lastEvents = events;
                eventGapSteps = 0;
            }
        }

        out.events = engine.getSpawnEventCount();
        out.offered = engine.getOfferedChildCount();
        out.spawned = engine.getSpawnedChildCount();
        out.refused = engine.getRefusedChildCount();
        out.completed = engine.getCompletedChildCount();
        return out;
    };

    // The slot-limited branch of clause (e)'s min(). Independent of the seed, so
    // it is computed once.
    constexpr double kBranchSlotLimited =
        static_cast<double>(kSoakChildSlots) * static_cast<double>(kSoakSeconds) /
        kLifetimeSeconds;

    std::size_t slotLimitedSeeds = 0;
    std::size_t offerLimitedSeeds = 0;

    for (std::size_t k = 0; k < kSeeds.size(); ++k) {
        const SoakSeries r = soak(kSeeds[k]);

        const double branchOfferLimited =
            static_cast<double>(kSoakChildrenPerEvent) * static_cast<double>(r.events);
        const bool slotLimited = kBranchSlotLimited <= branchOfferLimited;
        const double expectedCompleted = slotLimited ? kBranchSlotLimited : branchOfferLimited;
        if (slotLimited) {
            ++slotLimitedSeeds;
        } else {
            ++offerLimitedSeeds;
        }

        INFO("SC-015 seed index " << k << " = " << kSeeds[k]);
        WARN("SC-015 seed "
             << kSeeds[k] << ": events = " << r.events << " (expected " << kExpectedEvents
             << " +-25 %), offered = " << r.offered << ", spawned = " << r.spawned
             << ", refused = " << r.refused << ", completed = " << r.completed << " (expected "
             << expectedCompleted << " +-25 %, min branch = "
             << (slotLimited ? "SLOT-LIMITED" : "OFFER-LIMITED")
             << "; slot branch = " << kBranchSlotLimited << ", offer branch = "
             << branchOfferLimited << "); maxLive = " << r.maxLive << "/" << kSoakChildSlots
             << ", ratio span = [" << r.minRatio << ", " << r.maxRatio
             << "], worst target error = " << r.worstTargetError << ", worst amp overshoot = "
             << r.worstAmpOvershoot << ", longest live hold = "
             << (static_cast<double>(r.longestLiveHoldSteps) /
                 (60.0 * static_cast<double>(kChunksPerSecond)))
             << " min, longest event gap = "
             << (static_cast<double>(r.longestEventGapSteps) /
                 (60.0 * static_cast<double>(kChunksPerSecond)))
             << " min (both bounded by 60 min)");

        REQUIRE(r.configOk);
        REQUIRE(r.returnsOk);

        // ---- (a) the live-child count never exceeds numChildSlots() ---------
        REQUIRE(r.maxLive <= kSoakChildSlots);
        // A soak in which nothing ever lived would satisfy every bound below
        // vacuously.
        REQUIRE(r.maxLive >= std::size_t{1});

        // ---- (b) amplitude in [0, latched target]; the FR-023 latch formula --
        REQUIRE(r.parentIndexOk);
        REQUIRE(r.ampInBand);
        REQUIRE(r.targetMatches);
        REQUIRE(r.worstTargetError <= kTargetTolerance);

        // ---- (c) every emitted ratio in [0.5, 128] --------------------------
        REQUIRE(r.ratioInBand);
        REQUIRE(r.minRatio >= static_cast<double>(BloomEngine::kMinChildRatio));
        REQUIRE(r.maxRatio <= static_cast<double>(BloomEngine::kMaxChildRatio));

        // ---- (d) stateFinite() throughout -----------------------------------
        REQUIRE(r.finite);

        // ---- (e) the counters, against expectations written down ------------
        REQUIRE(std::fabs(static_cast<double>(r.events) - kExpectedEvents) <=
                kCountBand * kExpectedEvents);
        REQUIRE(std::fabs(static_cast<double>(r.completed) - expectedCompleted) <=
                kCountBand * expectedCompleted);
        // The EXACT identity (plan S14 C-7): every offered child takes exactly
        // one of the two outcomes.
        REQUIRE(r.offered == r.spawned + r.refused);
        // ... and every event offers exactly childrenPerEvent children, which is
        // what makes the identity above a statement about the OUTCOMES rather
        // than about a counter that simply tracks itself.
        REQUIRE(r.offered == static_cast<std::uint64_t>(kSoakChildrenPerEvent) * r.events);

        // ---- (f) no stall ---------------------------------------------------
        REQUIRE(r.longestLiveHoldSteps <= kStallSteps);
        REQUIRE(r.longestEventGapSteps <= kStallSteps);
    }

    // Which branch of clause (e)'s min() was active, reported ONCE for the whole
    // run so the saturation is visible to a future reader rather than re-derived.
    WARN("SC-015 (e) min() branch over " << kSeeds.size() << " seeds: " << slotLimitedSeeds
                                         << " slot-limited, " << offerLimitedSeeds
                                         << " offer-limited (slot branch = " << kBranchSlotLimited
                                         << " completions in " << kSoakSeconds << " s at a "
                                         << kLifetimeSeconds << " s lifetime)");
    REQUIRE(slotLimitedSeeds + offerLimitedSeeds == kSeeds.size());
}
