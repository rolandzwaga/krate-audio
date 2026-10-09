// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine ghost extension, the per-push lane
//          (specs/vorago-phase10a-ghost-extension)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10a-ghost-extension/spec.md
//            specs/vorago-phase10a-ghost-extension/plan.md
//            specs/vorago-phase10a-ghost-extension/tasks.md  (T003 creates this
//            stub; T007, T009, T011, T014, T016, T018, T019 fill it)
//
// SCOPE OF THIS TU (tasks.md T003's table): SC-001, SC-002, SC-004 (short arm,
// NEVER [long]), SC-005, SC-006, SC-007 (a)-(c), SC-008 (a)(c)(d)(e), SC-011,
// SC-012 and FR-049's short twin. Everything here stays in the per-push lane:
// no case in this file may carry the [long] tag (FR-048's closing sentence and
// CLAUDE.md's Build Commands note - the bounded-grid and guard cases are the
// cross-platform sentinels and must run on every push).
//
// This TU is DELIBERATELY NOT in the -fno-fast-math block of
// dsp/tests/CMakeLists.txt. Only atmosphere_ghost_nonfinite_test.cpp is, because
// it is the one TU that INJECTS NaN/Inf bit patterns. SC-004's detection-only
// non-finiteness check belongs here, on the fast-math path, and uses
// VoragoGhostFix::isNonFiniteBits - never std::isnan/isinf/isfinite, which fold
// to constants under /fp:fast and -ffast-math (core/db_utils.h,
// tools/lint-nonfinite-symbols.js).
//
// ------------------------------------------------------------------------------
// LANDED SO FAR IN THIS TU
//   T007 - SC-008 (c) seed-state arm, SC-007 (c) part 1, SC-006's compile-time
//          half. Written RED: getDroppedTriggerCount(), getTotalTriggeredGrains
//          Born(), getReverseRngState() and kReverseSalt do not exist on the
//          shipped AtmosphereEngine; they arrive at T008, and triggerGrain() at
//          T017 (see the // T017 marker below).
//   T009 - SC-001 clauses 2 and 3 (AtmosphereGhost_DefaultInert) and SC-007 (c)
//          part 2, the replica-RNG arm. The replica arm is the RED one: it
//          falsifies FR-006's "exactly one draw per birth, unconditionally",
//          and until T010 inserts that draw getReverseRngState() never moves.
//          Clauses 2 and 3 are GREEN at T009 by construction (no draw exists to
//          disturb grainRng_) and are regression guards whose falsification
//          target is T010 - they must be green before it and still green after.
//   T011 - SC-011 (AtmosphereGhost_ReverseFillDeficit, arms (a), (a2), (c) -
//          FR-050's binding criteria; there is deliberately NO arm (b), see the
//          theorem quoted at arm (c)), SC-004
//          (AtmosphereGhost_ReverseTruncation, exact + corner arms, NEVER
//          [long]) and FR-049's per-push twin
//          (AtmosphereGhost_ReverseLiveness_Short). RED at T011 and turning
//          green at T013, which gives a reverse grain its own wUp/wDown and adds
//          the FR-050 fill clause: SC-011 (a2) is red now (the first birth lands
//          at A ~ 282 368 against a right-hand side of at least 292 864) and
//          SC-004's exact arm is red now (the FORWARD window computes w = 7 and
//          a lifetime of 9 343, against the reverse w = 9 and 7 267).
//
//          THREE TRIGGER SITES ARE COMMENTED WITH A `// T017` MARKER and must be
//          uncommented there, together with SC-011 (a)'s `coldDelta >= 1` lower
//          bound, which is a consequence of the fired trigger and of nothing
//          else. Until then SC-011 (a)'s `bornDelta == 0` is VACUOUS at seed 1:
//          no density-scheduler tick falls inside its 40 000-sample span, so the
//          arm's teeth - and the FR-050 clause-disabled differential that T013
//          runs on it - only bite once T017 lands. (a2) is unaffected: its first
//          birth comes from a later scheduler tick and is red today.
//   T014 - SC-002 (a)-(d) (AtmosphereGhost_ReverseIsTimeReversed) and SC-008
//          (e), the partition-invariance clause appended to
//          AtmosphereGhost_RtSafety. Clauses (a), (b), (c) and (d) are RED at
//          T014 and turn green at T015: the birth draw (T010) and the
//          direction-dependent window (T013) have landed, so a grain IS born
//          `reversed` and IS admitted on the reverse arithmetic, but
//          renderGrainSpan()'s `advance` lambda still walks the read position
//          FORWARDS (atmosphere_engine.h:1980-1986 - verified this session:
//          there is no `advanceBack`). So the reverse grain currently renders
//          the FORWARD segment, which is exactly what (a)'s correlation pair,
//          (b)'s centroid slope sign, (c)'s age-span identity and (d)'s
//          across-the-change-point slope each detect.
//          Clause (e) is EXPECTED GREEN at T014 - the shipped render already is
//          partition invariant - and is written now because T015 is the edit
//          that can break it: `advanceBack` replaces the scalar index-generation
//          loop, and a decomposition that re-derived the read position from the
//          chunk base rather than from a per-sample recurrence would make the
//          output a function of where the block boundary fell.
//   T016 - SC-005 (AtmosphereGhost_TriggerAccounting, arms (a), (b) cold,
//          (b) warm, (c) and (d1)-(d3)), SC-012
//          (AtmosphereGhost_PassAScratchBound) and SC-008 (a) + (d), the two
//          clauses appended to AtmosphereGhost_RtSafety. T016 also UNCOMMENTS
//          the three `// T017` triggerGrain() sites left by T007/T011 and
//          SC-011 (a)'s `coldDelta >= 1` lower bound, which is a consequence of
//          the fired trigger and of nothing else.
//
//          THE WHOLE OF T016 IS RED AS A COMPILE ERROR at T016:
//          AtmosphereEngine::triggerGrain() does not exist on the shipped
//          header (verified this session - the members pendingTriggers_ /
//          droppedTriggers_ / totalTriggered_ landed at T008,
//          atmosphere_engine.h:2863-2866, but nothing enqueues or drains them).
//          It arrives with its pass-A consumption point at T017.
//          SC-012's chunk-2 assertion is red for a SECOND, independent reason
//          once it compiles: prepare() sizes retiredScratch_/dueScratch_ at
//          `kMaxGrains * 2` (:447-448) and the fixture retires ~188 grains in
//          one chunk. T017 re-sizes both to `kMaxGrains * 3`.
//   T018 - SC-001 clause 1 (the stored-fingerprint clause appended to
//          AtmosphereGhost_DefaultInert) and SC-007 (a) + (b) (the two arms
//          appended to AtmosphereGhost_Determinism).
//
//          CLAUSE 1 RUNS UNCONDITIONALLY. FR-046 requires the comparison to be
//          made at MEASURED per-comparison bounds; the three-toolchain probe
//          that measures them ran on 2026-09-24 (its method and figures are in
//          the block above the two bound constants), so those constants hold
//          measured values, the SKIP guard and its flag are gone, and the clause
//          now reports a colour that means something.
//
//          SC-007 (a) and (b) are EXPECTED GREEN at T018 - every mechanism they
//          rest on (the birth draw from T010, the direction-dependent window
//          from T013, advanceBack from T015 and triggerGrain from T017) has
//          landed. They are regression guards, and their falsification targets
//          are any later edit that makes a render depend on something other
//          than the seed: a `static` or thread-local left in the reverse path,
//          a time- or address-derived value, or an uninitialised read.
// ------------------------------------------------------------------------------
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "atmosphere_ghost_fixtures.h"

#include <krate/dsp/core/pitch_utils.h>       // L0  semitonesToRatio (== ratioAtPitch)
#include <krate/dsp/core/random.h>            // L0  deriveStreamSeed, Xorshift32
#include <krate/dsp/systems/atmosphere_engine.h>  // L3  the component under test

// SC-008 (a). <allocation_detector.h> ONLY, and NEVER
// <allocation_operator_overrides.h>: the single owner of the global
// operator new/delete replacements in dsp_systems_tests is
// unit/systems/selectable_oscillator_test.cpp:388
// (tests/test_helpers/vorago_fixtures.h:26-32), and a second include anywhere in
// the image is a duplicate-symbol link error.
#include <allocation_detector.h>  // test_helpers  SC-008 (a)'s AllocationScope
#include <render_fingerprint.h>  // test_helpers  SC-008 (e)'s partition comparison
#include <reverb_metrics.h>      // test_helpers  spectralCentroidHz (:291), SC-002 (b)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>  // T018  std::setprecision, for the paste-ready literal
#include <memory>
#include <span>
#include <sstream>  // T018  std::ostringstream, for the paste-ready literal
#include <string>   // T018  the paste-ready literal's return type
#include <vector>

using Krate::DSP::AtmosphereEngine;
using Krate::DSP::Xorshift32;

namespace {

/// @brief Render `samples` samples of the shared deterministic excitation.
///
/// Deliberately NOT `VoragoGhostFix::preRollFullRing`: the seed-state arms below
/// need the RNG streams to have been ASKED to do work, not a full 1 048 576-sample
/// ring (21.845 s), and a full-ring pre-roll in every SECTION would cost more than
/// the whole per-push lane is allowed. Cases whose criterion depends on a warm
/// ring (T009's replica arm, T011, T014) call `preRollFullRing` FIRST and then
/// use this helper for the measured span; this helper on its own asserts nothing
/// about ring fullness.
void renderSpan(AtmosphereEngine& engine, std::size_t samples, std::uint32_t exciteSeed,
                std::size_t blockSize = 512) {
    if (samples == 0) {
        return;
    }
    std::vector<float> inLeft(samples, 0.0f);
    std::vector<float> inRight(samples, 0.0f);
    VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, exciteSeed);

    const std::size_t block = (blockSize == 0) ? std::size_t{1} : blockSize;
    std::vector<float> outLeft(block, 0.0f);
    std::vector<float> outRight(block, 0.0f);

    std::size_t rendered = 0;
    while (rendered < samples) {
        const std::size_t n = std::min(block, samples - rendered);
        engine.processStereoBlock(inLeft.data() + rendered, inRight.data() + rendered,
                                  outLeft.data(), outRight.data(), n);
        rendered += n;
    }
}

/// @brief One measurement of the FR-006 replica protocol, at one probability.
///
/// Everything the replica arm needs, gathered in one render so the (expensive)
/// full-ring pre-roll is paid once per probability and not once per assertion.
struct ReplicaArm {
    std::uint32_t reverseStateAtSpanStart = 0u;
    std::uint32_t reverseStateAtSpanEnd = 0u;
    /// Reverse-stream draws the PRE-ROLL is accountable for: see the derivation
    /// in `runReplicaArm()` below.
    std::uint64_t preRollDraws = 0u;
    std::uint64_t spanBirths = 0u;
    std::uint64_t spanRingColdDelta = 0u;
    std::uint64_t spanPoolFullDelta = 0u;
};

/// @brief Full-ring pre-roll, then a fixed further render, at `probability`.
///
/// THE DRAW ACCOUNTING, which is the whole point of the arm and is derived from
/// the shipped control flow of `tryBirthGrain()` rather than assumed:
///
///   * the slot sweep's pool-full early-out (`atmosphere_engine.h:1665-1675`)
///     returns BEFORE any RNG is touched, so a `skipPoolFull_` increment costs
///     ZERO draws on either stream;
///   * every other exit is downstream of the draw site - the three
///     `++skipRingCold_; return;` rejections at `:1736-1742`, `:1755-1761` and
///     `:1797-1803` all sit after the four `grainRng_` draws at `:1680-1683`,
///     which is exactly where FR-006's fifth draw is inserted at T010;
///   * a successful birth reaches `++totalBorn_` at `:1869`.
///
/// So, since the last seeding, the reverse stream has been asked for exactly
/// `getTotalGrainsBorn() + getSkippedTriggerCountRingCold()` values - no more,
/// no fewer. Over a span in which BOTH skip counters are unmoved that collapses
/// to "one draw per birth", which is the identity SC-007 (c) part 2 states.
[[nodiscard]] ReplicaArm runReplicaArm(float probability, std::uint32_t seed,
                                       std::size_t spanSamples, std::size_t spanBlock) {
    VoragoGhostFix::GhostConfig config{};
    config.seed = seed;

    auto engine = std::make_unique<AtmosphereEngine>();
    VoragoGhostFix::applyVoragoGhost(*engine, config);
    // Written AFTER applyVoragoGhost: prepare() ends with reset()
    // (atmosphere_engine.h:400-402) and reset() does NOT clear
    // reverseProbability_ (:651-655 clear the counters only), so a probability
    // written before prepare() would survive - but relying on that would make
    // the fixture depend on a detail no criterion pins.
    engine->setGrainReverseProbability(probability);
    REQUIRE(engine->getGrainReverseProbability() == probability);

    // The ring must end SATURATED: getAvailableSamples() is
    // min(samplesWritten_, capacity_) (rolling_capture_buffer.h:443-445), so a
    // short pre-roll would leave the filling-ring regime in play and the
    // "no ring-cold rejection in the span" precondition below could not hold.
    const std::size_t preRolled =
        VoragoGhostFix::preRollFullRing(*engine, seed, VoragoGhostFix::kReferenceRenderBlock);
    REQUIRE(preRolled == engine->getCaptureCapacitySamples());

    ReplicaArm arm{};
    arm.preRollDraws = engine->getTotalGrainsBorn() + engine->getSkippedTriggerCountRingCold();
    arm.reverseStateAtSpanStart = engine->getReverseRngState();

    const std::uint64_t born0 = engine->getTotalGrainsBorn();
    const std::uint64_t cold0 = engine->getSkippedTriggerCountRingCold();
    const std::uint64_t poolFull0 = engine->getSkippedTriggerCountPoolFull();

    renderSpan(*engine, spanSamples, seed, spanBlock);

    arm.spanBirths = engine->getTotalGrainsBorn() - born0;
    arm.spanRingColdDelta = engine->getSkippedTriggerCountRingCold() - cold0;
    arm.spanPoolFullDelta = engine->getSkippedTriggerCountPoolFull() - poolFull0;
    arm.reverseStateAtSpanEnd = engine->getReverseRngState();
    return arm;
}

/// @brief `state` advanced by exactly `draws` `nextUnipolar()` calls.
[[nodiscard]] std::uint32_t advanceReplica(std::uint32_t state, std::uint64_t draws) noexcept {
    Xorshift32 replica{state};
    for (std::uint64_t i = 0; i < draws; ++i) {
        static_cast<void>(replica.nextUnipolar());
    }
    return replica.state();
}

// =============================================================================
// SC-008 (e) HELPERS - partition invariance of the REVERSE path
// =============================================================================

/// SC-008 (e)'s rate, span and geometry. Deliberately SMALL and deliberately
/// NOT the Vorago ghost point: the 1-sample arm makes one `processStereoBlock`
/// call per sample, so the span is sized to the smallest render that still
/// contains a useful number of reverse births rather than to an operating point.
constexpr double kPartitionSampleRate = 48000.0;
constexpr std::size_t kPartitionSamples = 96000u;  // 2 s

/// @brief One SC-008 (e) arm: the same 2 s reverse render, driven in
///        `blockSize`-sample blocks, reduced to a fingerprint per channel.
///
/// THE CONFIGURATION IS CHOSEN SO THE ARM IS NOT VACUOUS, and each choice is
/// the reason it is there rather than a default:
///   * `captureSeconds = kMinCaptureSeconds` -> C = nextPowerOf2(48 000) =
///     65 536, so the ring saturates 1.365 s in and most of the span renders on
///     a full ring;
///   * `density = 5`, `jitter = 0` -> the scheduler ticks every 9 600 samples
///     exactly (grain_scheduler.h:100-103), i.e. ~10 attempts in the span;
///   * `grainSeconds = 0.2` -> L' = 9 600, one grain at a time, so FR-028's
///     population gain sits at 1/sqrt(1) throughout and the comparison is not
///     dominated by a smoother;
///   * `pitchSemitones = -5` with spread and drift at 0 -> ratio = 0.749154...,
///     a FRACTIONAL ratio. That is the point: at ratio 1 the borrow's
///     ceil-correction branch never fires, and the partition arm would be blind
///     to exactly the defect SC-002 (c) exists for.
/// `blur` and `freeze` are OFF so the comparison sees the grain path alone -
/// an STFT stage has its own hop grid and would confound a partition failure in
/// renderGrainSpan with one in the pump.
struct PartitionArm {
    Krate::DSP::TestUtils::RenderFingerprint left{};
    Krate::DSP::TestUtils::RenderFingerprint right{};
    std::uint64_t born = 0;
    std::uint64_t reverseBorn = 0;
    std::uint64_t ringCold = 0;
    std::uint64_t poolFull = 0;
};

[[nodiscard]] PartitionArm runPartitionArm(std::size_t blockSize) {
    auto engine = std::make_unique<AtmosphereEngine>();
    engine->prepare(kPartitionSampleRate,
                    AtmosphereEngine::PrepareConfig{
                        .captureSeconds = AtmosphereEngine::kMinCaptureSeconds,
                        .blurEnabled = false,
                        .freezeEnabled = false,
                        .blurFftSize = std::size_t{1024},
                        .freezeFftSize = std::size_t{2048},
                        .maxBlockSamples = std::size_t{2048}});
    engine->setSeed(1u);
    engine->setDensity(5.0f);
    engine->setJitter(0.0f);
    engine->setGrainSeconds(0.2f);
    engine->setPitchSemitones(-5.0f);
    engine->setPitchSpread(0.0f);
    engine->setDriftRangeSemitones(0.0f);
    engine->setDriftDepth(0.0f);
    engine->setPositionSeconds(0.0f);
    engine->setPositionSpread(0.0f);
    engine->setDecorrelation(0.0f);
    engine->setPanSpread(0.0f);
    engine->setLevel(1.0f);
    engine->setGrainReverseProbability(1.0f);

    std::vector<float> inLeft(kPartitionSamples, 0.0f);
    std::vector<float> inRight(kPartitionSamples, 0.0f);
    VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, 1u);

    std::vector<float> outLeft(kPartitionSamples, 0.0f);
    std::vector<float> outRight(kPartitionSamples, 0.0f);

    const std::size_t block = (blockSize == 0) ? std::size_t{1} : blockSize;
    std::size_t rendered = 0;
    while (rendered < kPartitionSamples) {
        const std::size_t n = std::min(block, kPartitionSamples - rendered);
        engine->processStereoBlock(inLeft.data() + rendered, inRight.data() + rendered,
                                   outLeft.data() + rendered, outRight.data() + rendered, n);
        rendered += n;
    }

    PartitionArm arm{};
    arm.left = Krate::DSP::TestUtils::fingerprintRender(outLeft);
    arm.right = Krate::DSP::TestUtils::fingerprintRender(outRight);
    arm.born = engine->getTotalGrainsBorn();
    arm.reverseBorn = engine->getTotalReverseGrainsBorn();
    arm.ringCold = engine->getSkippedTriggerCountRingCold();
    arm.poolFull = engine->getSkippedTriggerCountPoolFull();
    return arm;
}

// =============================================================================
// T016 HELPERS - the trigger-accounting operating point (SC-005, SC-008 (a)(d))
// =============================================================================

/// SC-005's rate. 48 kHz, so `kMinGrainSeconds` is exactly 2 400 samples and the
/// FR-007 silence ramp (`kSilenceRampMs` = 10 ms, `atmosphere_engine.h:280`,
/// `silenceStep_ = 1 / (0.01 * sr)` at `:530`) is exactly 480 samples. Both
/// figures are reasoned with below, so they are named rather than repeated.
constexpr double kTriggerSampleRate = 48000.0;

/// `captureSeconds = kMinCaptureSeconds` -> C = nextPowerOf2(48 000) = 65 536,
/// i.e. a 65 536-sample full-ring pre-roll instead of the Vorago ghost point's
/// 1 048 576. SC-005 has eight arms and nearly every one needs a warm ring; at
/// the ghost geometry the case alone would cost more than the rest of the
/// per-push lane, and nothing SC-005 asserts depends on the ring being 21.845 s
/// rather than 1.365 s.
constexpr float kTriggerCaptureSeconds = AtmosphereEngine::kMinCaptureSeconds;

/// FR-019's saturation probe: `kMaxGrains + 10`, so exactly ten calls are
/// refused and `getDroppedTriggerCount()` is an equality rather than a bound.
constexpr std::size_t kSaturatingCalls = AtmosphereEngine::kMaxGrains + std::size_t{10};
constexpr std::uint64_t kExpectedDropped = 10u;

/// @brief The SC-005 fixture engine: every control pinned to a NEUTRAL value so
///        the FR-014 admission arithmetic collapses to one figure the arms quote.
///
/// WHY ALL OF THEM ARE PINNED. `AtmosphereEngine`'s own defaults are NOT neutral
/// - `positionSeconds_ = 1.0`, `positionSpread_ = 0.3`, `pitchSpread_ = 0.15`,
/// `driftRangeSemitones_ = 2.0`, `decorrelation_ = 0.5`
/// (`atmosphere_engine.h:2818-2830`, read this session) - and every one of them
/// feeds `needed` in `tryBirthGrain()`. With them all at zero: ratioMin ==
/// ratioMax == 1, so a FORWARD grain has wUp == wDown == 0 (`:1735-1737`),
/// decorrAge == 0 (`:1718-1720`), birthAge clamps to ageLo == kMinAgeSamples ==
/// 64 (`:1779-1791`) and
///
///     needed == ceil(64) + 64 == 128,   AT EVERY SAMPLE RATE  (:1817-1819)
///
/// A warm ring (available == C >= 128) therefore admits, and a ring holding
/// fewer than 128 samples rejects with `++skipRingCold_`. That single constant
/// is what the cold arms and the warm arms below both rest on.
///
/// `density = kMinDensity` is SC-005's own requirement: the interonset is then
/// `sampleRate / 0.1` = 480 000 samples at 48 kHz, so any render shorter than
/// 10 s contains at most the free tick at sample 0 (the leading 1 in
/// `VoragoGhostFix::schedulerTicks`) and every other birth is a TRIGGER birth.
///
/// Blur is off by default because an STFT pump contributes nothing to a counter
/// assertion; SC-008 (a)'s allocation arm turns it on, because there the point
/// IS to walk the pump.
[[nodiscard]] std::unique_ptr<AtmosphereEngine> makeTriggerEngine(
    float reverseProbability, bool blurEnabled = false,
    double sampleRate = kTriggerSampleRate) {
    auto engine = std::make_unique<AtmosphereEngine>();
    engine->prepare(sampleRate, AtmosphereEngine::PrepareConfig{
                                    .captureSeconds = kTriggerCaptureSeconds,
                                    .blurEnabled = blurEnabled,
                                    .freezeEnabled = false,
                                    .blurFftSize = std::size_t{1024},
                                    .freezeFftSize = std::size_t{2048},
                                    .maxBlockSamples = std::size_t{2048}});
    engine->setSeed(1u);
    engine->setDensity(AtmosphereEngine::kMinDensity);
    engine->setJitter(0.0f);
    engine->setGrainSeconds(AtmosphereEngine::kMinGrainSeconds);
    engine->setPitchSemitones(0.0f);
    engine->setPitchSpread(0.0f);
    engine->setDriftRangeSemitones(0.0f);
    engine->setDriftDepth(0.0f);
    engine->setPositionSeconds(0.0f);
    engine->setPositionSpread(0.0f);
    engine->setDecorrelation(0.0f);
    engine->setPanSpread(0.0f);
    engine->setLevel(1.0f);
    engine->setGrainReverseProbability(reverseProbability);
    return engine;
}

/// @brief Fire `count` triggers, so the arms below read as accounting rather
///        than as loops.
void fireTriggers(AtmosphereEngine& engine, std::size_t count) {
    for (std::size_t k = 0; k < count; ++k) {
        engine.triggerGrain();
    }
}

/// Every counter an SC-005 arm compares, read in ONE place so no arm can report
/// "unmoved" about a counter it never looked at.
struct TriggerCounters {
    std::uint64_t born = 0;
    std::uint64_t retired = 0;
    std::uint64_t triggered = 0;
    std::uint64_t reverseBorn = 0;
    std::uint64_t dropped = 0;
    std::uint64_t poolFull = 0;
    std::uint64_t ringCold = 0;
    std::size_t active = 0;
};

[[nodiscard]] TriggerCounters readCounters(const AtmosphereEngine& engine) noexcept {
    return TriggerCounters{.born = engine.getTotalGrainsBorn(),
                           .retired = engine.getTotalGrainsRetired(),
                           .triggered = engine.getTotalTriggeredGrainsBorn(),
                           .reverseBorn = engine.getTotalReverseGrainsBorn(),
                           .dropped = engine.getDroppedTriggerCount(),
                           .poolFull = engine.getSkippedTriggerCountPoolFull(),
                           .ringCold = engine.getSkippedTriggerCountRingCold(),
                           .active = engine.getActiveGrainCount()};
}

}  // namespace

// =============================================================================
// SC-008 (c) - triggerGrain() before prepare() is a no-op that moves no counter
// =============================================================================
//
// FR-023's third clause. An unprepared engine has no pool, no ring and no
// scheduler geometry; the entry point must therefore refuse the request
// OUTRIGHT rather than bank it, which is observable exactly as "neither counter
// moved" - the dropped counter is NOT incremented either, because a call that
// never reached the queue is not a drop (a drop is FR-019's saturation case,
// covered at T016).
//
// This case grows clauses (a), (d) and (e) at T016 / T014; clause (b) lives in
// atmosphere_ghost_nonfinite_test.cpp, the one TU on the -fno-fast-math list.
TEST_CASE("AtmosphereGhost_RtSafety", "[atmosphere][ghost]") {
    SECTION("(c) triggerGrain() before prepare() is a no-op that moves no counter") {
        AtmosphereEngine engine;  // NEVER prepared - that is the whole fixture

        // ---------------------------------------------------------------------
        // UNCOMMENTED AT T016 (tasks.md T016's closing clause). triggerGrain()
        // lands with its consumption point (pass A) at T017; until then this
        // whole TU is red as a COMPILE error, which is exactly T016's declared
        // red state. The guard being exercised is the `!prepared_` early-out.
        // ---------------------------------------------------------------------
        engine.triggerGrain();

        REQUIRE(engine.getDroppedTriggerCount() == std::uint64_t{0});
        REQUIRE(engine.getTotalTriggeredGrainsBorn() == std::uint64_t{0});
    }

    // =========================================================================
    // SC-008 (a) - NO RENDER PATH ALLOCATES (T016)
    // =========================================================================
    //
    // The grid is (probability 0 / 0.5 / 1) x (no triggers / one trigger per
    // block), because the three code paths the phase adds are reached by
    // different combinations: probability 0 never takes the reverse branch of
    // renderGrainSpan, probability 1 always does, and 0.5 mixes both inside one
    // chunk; the trigger axis reaches the second birth site in pass A.
    // `silence()` and one further block are inside the scope too, so the FR-007
    // ramp and the mid-block latch are covered rather than assumed.
    //
    // THE COUNT IS READ WHILE THE SCOPE IS STILL OPEN.
    // TestHelpers::AllocationScope latches its own total in its DESTRUCTOR
    // (tests/test_helpers/allocation_detector.h:117-120), so a `scope`-relative
    // assertion written after the closing brace would read a value the object no
    // longer updates. Catch2's REQUIRE and INFO both allocate, so they stay
    // OUTSIDE - this is vorago_engine_test.cpp:1705-1710's idiom verbatim.
    SECTION("(a) FR-045 - every render path is allocation-free, at probability 0 / 0.5 / 1") {
        // --- clause 0: the counter is LIVE in this image -----------------------
        // The global operator new/delete replacements live in
        // unit/systems/selectable_oscillator_test.cpp:388, not here. If they were
        // ever dropped, this whole criterion would silently become "0 == 0", so a
        // DELIBERATE allocation is counted first.
        std::size_t deliberate = 0u;
        float sink = 0.0f;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            std::vector<float> victim(1024u, 1.0f);
            sink = victim[512u];  // read it, so nothing may elide the allocation
            deliberate = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        INFO("clause 0: a deliberate 1024-float vector counted " << deliberate
                                                                 << " allocation(s)");
        REQUIRE(sink == 1.0f);
        REQUIRE(deliberate > std::size_t{0});

        constexpr std::size_t kBlock = 512u;
        constexpr std::size_t kBlocks = 8u;

        for (const float probability : {0.0f, 0.5f, 1.0f}) {
            for (const bool withTriggers : {false, true}) {
                auto engine = makeTriggerEngine(probability, /*blurEnabled=*/true);
                // Overridden for THIS arm only: the fixture's kMinDensity /
                // kMinGrainSeconds point is chosen for counter arithmetic and
                // leaves the grain sweep nearly idle. At density 20 (interonset
                // 2 400) and grainSeconds 0.5 (L' = 24 000) roughly ten grains
                // are live throughout, so pass B really runs.
                engine->setDensity(AtmosphereEngine::kMaxDensity);
                engine->setGrainSeconds(0.5f);

                const std::size_t capacity = engine->getCaptureCapacitySamples();
                REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, kBlock) == capacity);

                // EVERY buffer is allocated before the scope opens.
                std::vector<float> inLeft(kBlock, 0.0f);
                std::vector<float> inRight(kBlock, 0.0f);
                VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, 1u);
                std::vector<float> outLeft(kBlock, 0.0f);
                std::vector<float> outRight(kBlock, 0.0f);

                const std::uint64_t born0 = engine->getTotalGrainsBorn();

                std::size_t counted = 0u;
                {
                    [[maybe_unused]] const TestHelpers::AllocationScope scope;
                    for (std::size_t b = 0; b < kBlocks; ++b) {
                        if (withTriggers) {
                            engine->triggerGrain();
                        }
                        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                                   outRight.data(), kBlock);
                    }
                    // The FR-007 ramp and the mid-block latch, on the audio
                    // thread, inside the same scope.
                    engine->silence();
                    engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                               outRight.data(), kBlock);
                    counted = TestHelpers::AllocationDetector::instance().getAllocationCount();
                }

                const std::uint64_t bornDelta = engine->getTotalGrainsBorn() - born0;
                INFO("probability=" << probability << " withTriggers=" << withTriggers
                                    << " allocations=" << counted << " bornDelta=" << bornDelta
                                    << " triggered=" << engine->getTotalTriggeredGrainsBorn());
                REQUIRE(counted == std::size_t{0});
                // NOT VACUOUS: an arm that rendered no grain at all would be
                // allocation-free for the wrong reason.
                REQUIRE(bornDelta > std::uint64_t{0});
                if (withTriggers) {
                    REQUIRE(engine->getTotalTriggeredGrainsBorn() > std::uint64_t{0});
                }
            }
        }
    }

    // =========================================================================
    // SC-008 (d) - prepare() ACROSS RATES, and FR-024's prepare() half (T016)
    // =========================================================================
    //
    // Two independent claims, measured in one sweep over
    // 44 100 / 48 000 / 96 000 / 192 000 Hz:
    //
    //   1. `getReverseRngState()` is the SAME at every rate for the same seed.
    //      reset() seeds it as `deriveStreamSeed(seed_, kReverseSalt)`
    //      (atmosphere_engine.h:564) and setSeed() repeats that verbatim
    //      (:1047) - neither reads sampleRate_. An implementation that ever
    //      folded the rate (or the capacity, which IS rate-dependent) into the
    //      reverse seed would make every Vorago render rate-dependent in its
    //      grain DIRECTIONS, which no fingerprint tolerance could absorb.
    //   2. FR-024's prepare() clause, on CONSUMPTION EVIDENCE rather than on
    //      births (a births assertion would be vacuous: prepare() empties the
    //      ring too, so nothing could be born either way). Each iteration leaves
    //      a SATURATED queue behind - 64 pending plus 10 dropped - and the next
    //      iteration's prepare() must have cleared it: on the fresh cold ring
    //      the ONLY ring-cold rejection may be the scheduler's own free tick.
    //      An uncleared queue shows up as `64 + schedulerTicks`.
    //
    // The absolute (not delta) form is deliberate: prepare() ends with reset()
    // (:538) and reset() zeroes skipRingCold_ at :646.
    SECTION("(d) FR-024 / SC-008 (d) - prepare() at 44.1 / 48 / 96 / 192 kHz") {
        constexpr std::array<double, 4> kRates{44100.0, 48000.0, 96000.0, 192000.0};
        constexpr std::size_t kColdProbeSamples = 128u;
        constexpr std::size_t kBlock = 64u;
        constexpr std::size_t kBlocks = 64u;

        auto engine = std::make_unique<AtmosphereEngine>();
        std::uint32_t referenceReverseState = 0u;

        for (std::size_t r = 0; r < kRates.size(); ++r) {
            const double rate = kRates[r];
            INFO("rate=" << rate);

            engine->prepare(rate, AtmosphereEngine::PrepareConfig{
                                      .captureSeconds = kTriggerCaptureSeconds,
                                      .blurEnabled = false,
                                      .freezeEnabled = false,
                                      .blurFftSize = std::size_t{1024},
                                      .freezeFftSize = std::size_t{2048},
                                      .maxBlockSamples = std::size_t{2048}});
            engine->setSeed(1u);
            engine->setDensity(AtmosphereEngine::kMinDensity);
            engine->setJitter(0.0f);
            engine->setGrainSeconds(AtmosphereEngine::kMinGrainSeconds);
            engine->setPitchSemitones(0.0f);
            engine->setPitchSpread(0.0f);
            engine->setDriftRangeSemitones(0.0f);
            engine->setDriftDepth(0.0f);
            engine->setPositionSeconds(0.0f);
            engine->setPositionSpread(0.0f);
            engine->setDecorrelation(0.0f);
            engine->setPanSpread(0.0f);
            engine->setLevel(1.0f);
            engine->setGrainReverseProbability(1.0f);

            // --- claim 1 -----------------------------------------------------
            const std::uint32_t state = engine->getReverseRngState();
            if (r == 0) {
                referenceReverseState = state;
            }
            INFO("reverseRngState=" << state << " reference=" << referenceReverseState);
            REQUIRE(state == referenceReverseState);

            // --- claim 2: FR-024's prepare() half, on the COLD ring ----------
            renderSpan(*engine, kColdProbeSamples, 1u);
            const std::size_t ticks = VoragoGhostFix::schedulerTicks(
                kColdProbeSamples, rate, AtmosphereEngine::kMinDensity);
            INFO("coldProbe ringCold=" << engine->getSkippedTriggerCountRingCold()
                                       << " ticks=" << ticks
                                       << " born=" << engine->getTotalGrainsBorn()
                                       << " dropped=" << engine->getDroppedTriggerCount());
            REQUIRE(ticks == std::size_t{1});  // 128 samples is far under 10 s at every rate
            REQUIRE(engine->getSkippedTriggerCountRingCold()
                    == static_cast<std::uint64_t>(ticks));
            REQUIRE(engine->getTotalGrainsBorn() == std::uint64_t{0});
            REQUIRE(engine->getDroppedTriggerCount() == std::uint64_t{0});

            // --- a REVERSE render at this rate: non-finiteness and level -----
            const std::size_t capacity = engine->getCaptureCapacitySamples();
            REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, 512u) == capacity);

            std::vector<float> inLeft(kBlock, 0.0f);
            std::vector<float> inRight(kBlock, 0.0f);
            VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, 1u);
            std::vector<float> outLeft(kBlock, 0.0f);
            std::vector<float> outRight(kBlock, 0.0f);

            std::size_t nonFiniteSamples = 0;
            std::size_t overLevelSamples = 0;
            float worstAbs = 0.0f;
            for (std::size_t b = 0; b < kBlocks; ++b) {
                engine->triggerGrain();
                engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                           outRight.data(), kBlock);
                for (std::size_t i = 0; i < kBlock; ++i) {
                    const float l = outLeft[i];
                    const float rr = outRight[i];
                    // BIT-PATTERN detection, never std::isnan/std::isinf: this TU
                    // is deliberately off the -fno-fast-math list.
                    if (VoragoGhostFix::isNonFiniteBits(l) || VoragoGhostFix::isNonFiniteBits(rr)) {
                        ++nonFiniteSamples;
                        continue;  // |NaN| comparisons below are meaningless
                    }
                    const float worst = std::max(std::abs(l), std::abs(rr));
                    if (worst > worstAbs) {
                        worstAbs = worst;
                    }
                    if (worst > AtmosphereEngine::kMaxLevel) {
                        ++overLevelSamples;
                    }
                }
            }
            INFO("reverse render: nonFinite=" << nonFiniteSamples
                                              << " overLevel=" << overLevelSamples
                                              << " worstAbs=" << worstAbs << " triggered="
                                              << engine->getTotalTriggeredGrainsBorn()
                                              << " reverseBorn="
                                              << engine->getTotalReverseGrainsBorn());
            REQUIRE(nonFiniteSamples == std::size_t{0});
            REQUIRE(overLevelSamples == std::size_t{0});
            // Not vacuous: a rate at which nothing was born proves nothing.
            REQUIRE(engine->getTotalTriggeredGrainsBorn() > std::uint64_t{0});
            REQUIRE(engine->getTotalReverseGrainsBorn() > std::uint64_t{0});

            // --- leave a SATURATED queue for the next iteration's prepare() ---
            fireTriggers(*engine, kSaturatingCalls);
            REQUIRE(engine->getDroppedTriggerCount() == kExpectedDropped);
        }
    }

    // =========================================================================
    // SC-008 (e) - PARTITION INVARIANCE, on the backwards path
    // =========================================================================
    //
    // The same reverse render driven in blocks of 512, 64, 37 and 1 sample must
    // produce the same output. 37 is there because it is coprime with
    // kControlChunkSamples = 64, so every control chunk in the arm is split at a
    // different phase; 1 is the degenerate limit in which every chunk is a
    // single sample.
    //
    // WHY THE DEFAULT render_fingerprint.h TOLERANCES ARE THE RIGHT BOUNDS HERE
    // and no measured per-comparison override is needed: this is a SAME-BINARY,
    // same-machine comparison of one code path against itself, so the
    // cross-toolchain spread the shared constants were derived from
    // (render_fingerprint.h:32-39) is strictly an upper bound on what can
    // legitimately differ. kMetricTolerance = 2.5e-4 and kSampleTolerance =
    // 5.0e-4f (:58-61, verified this session).
    //
    // THIS IS THE PROPERTY THE SHIPPED CODE ALREADY GOES OUT OF ITS WAY TO HOLD.
    // renderGrainSpan()'s age recurrence forms every age against THIS SAMPLE's
    // write head in the integer domain rather than against the end-of-chunk head
    // (atmosphere_engine.h:2002-2012 records the 3.3e-4 partition failure the
    // other shape measured), and LinearReader rebases its index by
    // `numSamples - 1 - i` for the same reason
    // (rolling_capture_buffer.h:243-255). T015 rewrites the read-position walk
    // inside exactly that loop, which is why the clause is written for the
    // reverse path now: it is EXPECTED GREEN at T014 and its falsification
    // target is T015.
    SECTION("(e) partition invariance - 512 / 64 / 37 / 1 sample blocks agree") {
        using Krate::DSP::TestUtils::compareFingerprints;

        const PartitionArm reference = runPartitionArm(512u);

        // --- Fixture validity, asserted so a green run means something. A span
        //     with no reverse birth in it would compare two silences.
        INFO("reference arm: born=" << reference.born
                                    << " reverseBorn=" << reference.reverseBorn
                                    << " ringCold=" << reference.ringCold
                                    << " poolFull=" << reference.poolFull
                                    << " rms=" << reference.left.rms
                                    << " peak=" << reference.left.peak);
        REQUIRE(reference.born > std::uint64_t{0});
        // At probability 1 every birth is reversed except for the single exact
        // draw of 1.0f, whose rate is 2^-32 per birth (atmosphere_engine.h:1694).
        REQUIRE(reference.reverseBorn == reference.born);
        REQUIRE(reference.poolFull == std::uint64_t{0});
        REQUIRE(reference.left.peak > 0.0);
        REQUIRE(reference.right.peak > 0.0);

        for (const std::size_t block : {std::size_t{64}, std::size_t{37}, std::size_t{1}}) {
            const PartitionArm arm = runPartitionArm(block);
            INFO("block=" << block << " born=" << arm.born
                          << " reverseBorn=" << arm.reverseBorn
                          << " ringCold=" << arm.ringCold << " poolFull=" << arm.poolFull);

            // The admission DECISIONS must match first: a partition that changed
            // which attempts were admitted would also change the fingerprint,
            // and this separates the two failure modes.
            REQUIRE(arm.born == reference.born);
            REQUIRE(arm.reverseBorn == reference.reverseBorn);
            REQUIRE(arm.ringCold == reference.ringCold);
            REQUIRE(arm.poolFull == reference.poolFull);

            const auto leftCmp = compareFingerprints(arm.left, reference.left);
            INFO("left: worstMetric=" << leftCmp.worstMetricRelativeError
                                      << " worstSample=" << leftCmp.worstSampleError << " ("
                                      << leftCmp.detail << ")");
            REQUIRE(leftCmp.withinTolerance());

            const auto rightCmp = compareFingerprints(arm.right, reference.right);
            INFO("right: worstMetric=" << rightCmp.worstMetricRelativeError
                                       << " worstSample=" << rightCmp.worstSampleError << " ("
                                       << rightCmp.detail << ")");
            REQUIRE(rightCmp.withinTolerance());
        }
    }
}

// =============================================================================
// T018 HELPERS - SC-001 clause 1 (the stored-fingerprint clause) and
//                SC-007 (a)(b) (determinism, and separation by seed)
// =============================================================================
namespace {

// -----------------------------------------------------------------------------
// FR-046's MEASURED per-comparison bounds for kBaseCommitFingerprint.
//
// THE 2026-09-24 THREE-TOOLCHAIN PROBE (FR-046's measurement, ruling B-1).
//
// Method (reproducible): Fixture A's recipe - the EXACT protocol of
// atmosphere_ghost_fixtures.h section 6.1, re-implemented as a standalone
// `main()` with the same loop `renderSpanCapturingLeft` runs - compiled against
// dsp/include + tests/test_helpers + the KrateDSP .cpp list and run under
//     g++ 13.3.0     -std=c++20 -O3
//     g++ 13.3.0     -std=c++20 -O3 -ffast-math
//     clang++ 18.1.3 -std=c++20 -O2
// on Ubuntu 24.04 (WSL2, build dirs on F:), each with `enableFTZDAZ()` called
// before the render, and diffed against the MSVC 19.44 /O2 in-suite fingerprint
// stored as kBaseCommitFingerprint. The recipe is confirmed identical, not
// assumed: all three legs report `getGrainRngState() == 8918586` and
// `getTotalGrainsBorn() == 17`, i.e. the same integers the no-trigger arms pin
// against the stored base-commit constants.
//
// Relative deviation from the stored MSVC reference, per metric, worst over the
// three GNU/LLVM legs:
//
//     rms              2.5702e-7   (g++ -O3 and clang++ -O2, identical)
//     peak             2.7235e-4   (g++ -O3 and clang++ -O2, identical)
//     meanAbs          1.9474e-6
//     totalVariation   9.7665e-6
//     worst checkpoint 2.9029e-4   (absolute, checkpoint 20; g++ -O3)
//
// The two GNU legs and the LLVM leg agree with each other to the 7th
// significant digit (-ffast-math moves Fixture A's aggregates by <= 1.6e-6), so
// the whole spread above is MSVC vs GNU/LLVM codegen, concentrated in `peak`
// and in the checkpoint samples - exactly the two sample-valued quantities, and
// exactly the shape noise_organism_test.cpp:3216-3247 measured for its own
// render.
//
// The bounds below sit at ~3.5x the measured noise, the headroom band
// noise_organism_test.cpp and render_fingerprint.h:37-39 already use. BOTH ARE
// TIGHTER THAN THE PLACEHOLDERS THEY REPLACE (2.0e-3f / 1.0e-2): the measurement
// bought discrimination, it did not buy slack.
//
// The discipline is noise_organism_test.cpp:3288-3406's, reproduced clause for
// clause, because that TU is the repo's worked example of the same protocol:
//   * a per-comparison bound is a documented LOOSENING of the shared
//     cross-toolchain constants, never a tightening - the first two
//     static_asserts below;
//   * the SHARED constants must still hold their shipped values - the second
//     two - so a "fix" that edits render_fingerprint.h to make this comparison
//     pass breaks the BUILD instead of quietly widening every caller's bound;
//   * a paste-ready literal is printed on failure (baseCommitFingerprintLiteral
//     below), so a LEGITIMATE regeneration never has to be hand-typed;
//   * the stored fingerprint carries a PROVENANCE block - it does, at
//     atmosphere_ghost_fixtures.h:401-437, naming base commit
//     374580d7d0f0631561413310bd3085e15ba7279c, the machine, the compiler and
//     the date.
//
// NO BOUND MAY BE WIDENED AFTER IT IS RECORDED (FR-046, spec.md:614-616). The
// numbers below ARE recorded, here and in the phase's compliance table, so a red
// comparison from here on is a FINDING about the render - never an invitation to
// grow the number or to re-harvest the reference. Regeneration is legitimate only
// alongside a DELIBERATE change to what the fixture renders, and the PROVENANCE
// block is refilled in the same edit.
// -----------------------------------------------------------------------------

/// Per-comparison checkpoint-sample bound for Fixture A.
/// MEASURED: worst checkpoint spread 2.9029e-4 -> 3.4x headroom.
constexpr float kMeasuredSampleTolerance = 1.0e-3f;

/// Per-comparison aggregate-metric bound for Fixture A.
/// MEASURED: worst metric spread 2.7235e-4 (`peak`) -> 3.7x headroom.
constexpr double kMeasuredMetricTolerance = 1.0e-3;

static_assert(kMeasuredSampleTolerance > Krate::DSP::TestUtils::kSampleTolerance,
              "a per-comparison sample bound TIGHTER than the shared one would be a silent "
              "tightening of every other caller's expectation, not the documented loosening "
              "FR-046 authorises");
static_assert(Krate::DSP::TestUtils::kSampleTolerance == 5.0e-4f,
              "the SHARED sample bound (render_fingerprint.h:58) must not be edited for this "
              "caller's sake - if this fires, someone loosened the shared constant instead of "
              "passing a per-comparison one");
static_assert(kMeasuredMetricTolerance > Krate::DSP::TestUtils::kMetricTolerance,
              "same rule for the metric bound: per-comparison means looser than the shared "
              "one, never tighter");
static_assert(Krate::DSP::TestUtils::kMetricTolerance == 2.5e-4,
              "the SHARED metric bound (render_fingerprint.h:61) must not be edited for this "
              "caller's sake - if this fires, someone loosened the shared constant instead of "
              "passing a per-comparison one");

// The `kMeasuredBoundsLanded` flag and its accessor are GONE, not left standing
// at `true`: ruling B-1 says the bounds land and the guards come out in the same
// edit, and a flag that can only ever read `true` is a latch nobody will notice
// has been flipped back. The clause below runs unconditionally.

/// @brief Fixture A's render, KEEPING the left output channel.
///
/// `renderSpan()` above reuses one block-sized output buffer and discards the
/// render, which is all the counter and RNG-state arms need. Clause 1
/// fingerprints the whole left channel, so it needs the output kept. Everything
/// else is identical - the same `excitePinkPlusTone` over the whole span, the
/// same front-to-back walk in `blockSize`-sample blocks - so the two helpers
/// drive the engine the same way and only the destination differs. That matters
/// because AtmosphereEngine's output is partition invariant only to within the
/// render_fingerprint.h tolerances (SC-008 (e)), so a helper that rendered at a
/// different block size would not be reproducing Fixture A's measurement.
[[nodiscard]] std::vector<float> renderSpanCapturingLeft(AtmosphereEngine& engine,
                                                         std::size_t samples,
                                                         std::uint32_t exciteSeed,
                                                         std::size_t blockSize) {
    std::vector<float> outLeft(samples, 0.0f);
    if (samples == 0) {
        return outLeft;
    }

    std::vector<float> inLeft(samples, 0.0f);
    std::vector<float> inRight(samples, 0.0f);
    VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, exciteSeed);
    std::vector<float> outRight(samples, 0.0f);

    const std::size_t block = (blockSize == 0) ? std::size_t{1} : blockSize;
    std::size_t rendered = 0;
    while (rendered < samples) {
        const std::size_t n = std::min(block, samples - rendered);
        engine.processStereoBlock(inLeft.data() + rendered, inRight.data() + rendered,
                                  outLeft.data() + rendered, outRight.data() + rendered, n);
        rendered += n;
    }
    return outLeft;
}

/// @brief Format `fp` as the exact `kBaseCommitFingerprint` initialiser to paste
///        into atmosphere_ghost_fixtures.h section 6.1.
///
/// FR-046's "paste-ready literal printed on comparison failure". It exists so a
/// LEGITIMATE regeneration - one made alongside a deliberate change to what
/// Fixture A renders - is a copy rather than 36 hand-typed numbers, and so the
/// throwaway capture TU that would otherwise be needed never has to exist
/// (the harmonic_cloud_pre_amendment_fingerprints.h idiom, kept in-file).
///
/// The doubles print at 17 significant digits and the checkpoints at 9, which
/// is exactly how the stored constant is written today
/// (atmosphere_ghost_fixtures.h:476-489) - so a paste is a diff of VALUES and
/// never of formatting.
[[nodiscard]] std::string baseCommitFingerprintLiteral(
    const Krate::DSP::TestUtils::RenderFingerprint& fp) {
    std::ostringstream os;
    os << "inline constexpr Krate::DSP::TestUtils::RenderFingerprint kBaseCommitFingerprint{\n";
    os << std::setprecision(17);
    os << "    .rms = " << fp.rms << ",\n";
    os << "    .peak = " << fp.peak << ",\n";
    os << "    .meanAbs = " << fp.meanAbs << ",\n";
    os << "    .totalVariation = " << fp.totalVariation << ",\n";
    os << "    .checkpoints = {";
    // showpoint: an exact-zero checkpoint must print as 0.00000000f, never as the
    // invalid literal 0f (checkpoint 0 of a render that starts silent IS 0).
    os << std::setprecision(9) << std::showpoint;
    for (std::size_t k = 0; k < Krate::DSP::TestUtils::kRenderCheckpoints; ++k) {
        if (k % 4 == 0) {
            os << "\n        ";
        }
        os << fp.checkpoints[k] << "f,";
        if (k % 4 != 3) {
            os << ' ';
        }
    }
    os << "}};\n";
    return os.str();
}

// -----------------------------------------------------------------------------
// SC-007 (a)(b)'s operating point.
//
// DELIBERATELY NOT THE VORAGO GHOST POINT. Fixture A's ring is 1 048 576 samples
// (21.845 s) and AtmosphereGhost_DefaultInert already renders 60 s through it
// once per SECTION; SC-007 (a) needs SIX renders (two engines x three
// probabilities) and (b) two more, so at the ghost geometry this pair of arms
// alone would cost more than the rest of the per-push lane. Nothing either arm
// asserts depends on the ring being 21.845 s.
//
// Every value below is chosen so the arms are NOT VACUOUS, which for a
// determinism criterion is the whole risk: two renders of silence agree
// perfectly, and two renders of one grain agree on their aggregates by
// accident.
//   * captureSeconds = 4 -> C = nextPowerOf2(192 000) = 262 144 (5.461 s), so
//     the full-ring pre-roll is ~4x cheaper than Fixture A's and still leaves
//     the admission arithmetic with room to spare: at positionSeconds 1.0 with
//     spread, birthAge tops out near 1.5 s (72 000 samples) and the reverse
//     window adds well under 20 000 more, against 262 144 available.
//   * density 3 with jitter 0.5 -> ~7 scheduler ticks in the 2 s span, at
//     interonsets that DEPEND ON THE SEED. That is what gives (b) its teeth:
//     with jitter, spread and drift all at zero, two seeds would differ only in
//     which grains are reversed, and a reversed pink-noise grain has very
//     nearly the same rms and total variation as the forward one - the
//     aggregate metrics would barely move and (b)'s 100x separation could fail
//     on correct code.
//   * pitchSpread / positionSpread / decorrelation / panSpread nonzero for the
//     same reason: they are the per-grain draws that make two seeds render
//     different SOURCE MATERIAL rather than the same material rearranged.
//   * grainSeconds 0.3 at level 1.0 -> ~3 concurrent grains, far below
//     kMaxGrains = 64, so no arm is shaped by pool saturation.
//   * blur and freeze OFF: an STFT stage has its own hop grid and would
//     confound a determinism failure in the grain path with one in the pump.
// -----------------------------------------------------------------------------
constexpr double kDetSampleRate = 48000.0;
constexpr float kDetCaptureSeconds = 4.0f;       ///< C = nextPowerOf2(192 000) = 262 144
constexpr std::size_t kDetSpanSamples = 96000u;  ///< 2 s at 48 kHz
constexpr std::size_t kDetBlock = 512u;

/// The TRIGGER SCHEDULE SC-007 (a) names: one `triggerGrain()` before every
/// 16th block of the measured span, i.e. at samples 0, 8 192, 16 384, ...
/// 12 triggers over the 188-block span. It is a function of the BLOCK INDEX
/// alone, so two engines driven by this loop see the identical schedule
/// whatever their seed - which is what makes "same seed, same schedule" a
/// statement about the engine and not about the driver.
constexpr std::size_t kDetTriggerEveryBlocks = 16u;

/// Excitation seed, FIXED across every arm including (b)'s. (b) must separate
/// two ENGINE seeds; feeding the two arms different audio would separate them
/// for a reason that has nothing to do with `reverseRng_`.
constexpr std::uint32_t kDetExciteSeed = 1u;

struct DeterminismArm {
    Krate::DSP::TestUtils::RenderFingerprint left{};
    std::uint64_t bornDelta = 0;
    std::uint64_t reverseBornDelta = 0;
    std::uint64_t triggeredDelta = 0;
    std::uint64_t ringColdDelta = 0;
    std::uint64_t poolFullDelta = 0;
    std::uint64_t droppedDelta = 0;
};

/// @brief One SC-007 arm: a freshly constructed engine at `seed`, pre-rolled to
///        a full ring, then 2 s rendered against the fixed trigger schedule.
///
/// The counters are returned as DELTAS over the measured span so the pre-roll's
/// own births - which are real, and differ between seeds - never leak into an
/// arm's anti-vacuity check.
[[nodiscard]] DeterminismArm runDeterminismArm(std::uint32_t seed, float probability) {
    auto engine = std::make_unique<AtmosphereEngine>();
    engine->prepare(kDetSampleRate, AtmosphereEngine::PrepareConfig{
                                        .captureSeconds = kDetCaptureSeconds,
                                        .blurEnabled = false,
                                        .freezeEnabled = false,
                                        .blurFftSize = std::size_t{1024},
                                        .freezeFftSize = std::size_t{2048},
                                        .maxBlockSamples = std::size_t{2048}});
    engine->setSeed(seed);
    engine->setDensity(3.0f);
    engine->setJitter(0.5f);
    engine->setGrainSeconds(0.3f);
    engine->setPitchSemitones(-5.0f);
    engine->setPitchSpread(0.2f);
    engine->setDriftRangeSemitones(0.0f);
    engine->setDriftDepth(0.0f);
    engine->setPositionSeconds(1.0f);
    engine->setPositionSpread(0.5f);
    engine->setDecorrelation(0.5f);
    engine->setPanSpread(0.5f);
    engine->setLevel(1.0f);
    engine->setGrainReverseProbability(probability);

    // Warm ring first: getAvailableSamples() is min(samplesWritten_, capacity_)
    // (rolling_capture_buffer.h:443-445), so a short pre-roll would leave the
    // filling-ring regime in play and the arms would be measuring admission
    // rejections as much as determinism.
    const std::size_t preRolled =
        VoragoGhostFix::preRollFullRing(*engine, kDetExciteSeed, kDetBlock);
    REQUIRE(preRolled == engine->getCaptureCapacitySamples());

    std::vector<float> inLeft(kDetSpanSamples, 0.0f);
    std::vector<float> inRight(kDetSpanSamples, 0.0f);
    VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, kDetExciteSeed);
    std::vector<float> outLeft(kDetSpanSamples, 0.0f);
    std::vector<float> outRight(kDetSpanSamples, 0.0f);

    const TriggerCounters before = readCounters(*engine);

    std::size_t rendered = 0;
    std::size_t blockIndex = 0;
    while (rendered < kDetSpanSamples) {
        if (blockIndex % kDetTriggerEveryBlocks == 0) {
            engine->triggerGrain();
        }
        const std::size_t n = std::min(kDetBlock, kDetSpanSamples - rendered);
        engine->processStereoBlock(inLeft.data() + rendered, inRight.data() + rendered,
                                   outLeft.data() + rendered, outRight.data() + rendered, n);
        rendered += n;
        ++blockIndex;
    }

    const TriggerCounters after = readCounters(*engine);

    DeterminismArm arm{};
    arm.left = Krate::DSP::TestUtils::fingerprintRender(outLeft);
    arm.bornDelta = after.born - before.born;
    arm.reverseBornDelta = after.reverseBorn - before.reverseBorn;
    arm.triggeredDelta = after.triggered - before.triggered;
    arm.ringColdDelta = after.ringCold - before.ringCold;
    arm.poolFullDelta = after.poolFull - before.poolFull;
    arm.droppedDelta = after.dropped - before.dropped;
    return arm;
}

/// Everything an SC-007 arm must satisfy before its fingerprint means anything.
/// Gathered in one place so no arm can claim "the renders agree" about two
/// renders of silence.
void requireArmIsNotVacuous(const char* label, const DeterminismArm& arm) {
    INFO("arm " << label << ": born=" << arm.bornDelta << " reverseBorn=" << arm.reverseBornDelta
                << " triggered=" << arm.triggeredDelta << " ringCold=" << arm.ringColdDelta
                << " poolFull=" << arm.poolFullDelta << " dropped=" << arm.droppedDelta
                << " rms=" << arm.left.rms << " peak=" << arm.left.peak
                << " totalVariation=" << arm.left.totalVariation);

    // Grains were actually born in the measured span - not only in the pre-roll.
    REQUIRE(arm.bornDelta > std::uint64_t{0});
    // The trigger schedule reached the engine: 12 calls, none of them refused.
    REQUIRE(arm.triggeredDelta > std::uint64_t{0});
    REQUIRE(arm.droppedDelta == std::uint64_t{0});
    // The pool never saturated, so nothing below is shaped by kMaxGrains.
    REQUIRE(arm.poolFullDelta == std::uint64_t{0});
    // And the render carries signal, so "the fingerprints agree" is a statement
    // about audio rather than about two silences.
    REQUIRE(arm.left.rms > 1.0e-6);
    REQUIRE(arm.left.peak > 1.0e-5);
    REQUIRE(arm.left.totalVariation > 1.0);
}

}  // namespace

// =============================================================================
// SC-001 clauses 2 and 3 - the default-inert bar, on INTEGERS
// =============================================================================
//
// ADR-2's structural claim, stated so that no tolerance can absorb it: the
// reverse bit is drawn from reverseRng_, a stream of its own, and NEVER from
// grainRng_. If a future edit moved the draw onto grainRng_ - the cheap,
// obvious thing to do - every Seraphis render would re-shuffle, and the only
// witness that survives is the INTEGER equality below: grainRng_'s state after
// a fixed render must be bit-identical to the base commit's, at reverse
// probability 0 AND at 1.
//
// Clause 2's probability-1 arm is the one with teeth. At probability 0 a
// guarded draw (`if (reverseProbability_ > 0.0f)`) would also pass; only the
// probability-1 arm can see a draw that was taken from the wrong stream.
//
// WHY THE PROBABILITY-1 ARM STILL EXPECTS THE SAME grainRng_ STATE even after
// T013 gives reverse grains their own birth window: grainRng_ is asked for its
// four values once per birth ATTEMPT that clears the pool-full early-out
// (atmosphere_engine.h:1665-1683), and this fixture never reaches the pool cap
// (kBaseCommitSkipPoolFull == 0 below says so, with kMaxGrains = 64 against
// ~3.6 concurrent grains at density 0.30 / grainSeconds 12). The attempt COUNT
// is therefore fixed by the scheduler - its own stream - and is independent of
// how many of those attempts are admitted. Rejections downstream of the draws
// change totalBorn_, never grainRng_.
//
// Clause 1 - the render fingerprint under FR-046's measured-bounds protocol -
// landed at T018 as the first SECTION below and has run unconditionally since
// 2026-09-24, when the three-toolchain probe replaced the two placeholder bounds
// (kMeasuredMetricTolerance / kMeasuredSampleTolerance above) with measured
// figures. See the T018 entry in this file's banner.
//
// PROTOCOL: exactly Fixture A of atmosphere_ghost_fixtures.h section 6.1 -
// applyVoragoGhost() with the DEFAULT GhostConfig (48 kHz, seed 1,
// captureSeconds = 20, blur on, freeze off, the seven FR-017 values),
// excitePinkPlusTone(.., 1u) over the whole 2 880 000-sample span, rendered
// front to back in blocks of kReferenceRenderBlock = 512. Any deviation makes
// the stored constants inapplicable.
TEST_CASE("AtmosphereGhost_DefaultInert", "[atmosphere][ghost]") {
    VoragoGhostFix::GhostConfig config{};  // seed 1, 48 kHz, the FR-017 seven

    // Heap-allocated: AtmosphereEngine carries kMaxGrains = 64 grain records
    // plus the capture ring's bookkeeping, which is more than a test stack
    // frame should hold, and Fixture A's protocol names the heap.
    auto engine = std::make_unique<AtmosphereEngine>();
    VoragoGhostFix::applyVoragoGhost(*engine, config);

    // Recorded alongside the stored constants (fixtures section 6.1) and
    // re-checked here: if the ring geometry ever changed, every one of the
    // transcribed figures below would be measuring a different fixture.
    REQUIRE(engine->getCaptureCapacitySamples() == VoragoGhostFix::kBaseCommitCaptureCapacity);

    // =========================================================================
    // CLAUSE 1 - render identity across the change, at FR-046's MEASURED bounds
    // =========================================================================
    //
    // The audible half of the default-inert bar, and the only clause a listener
    // could have noticed: with NEITHER new feature engaged - reverse
    // probability at its FR-002 default of 0, no trigger ever fired - the
    // 60 s Vorago-ghost render must still be the render the base commit
    // produced.
    //
    // WHAT THIS CLAUSE CATCHES THAT CLAUSES 2 AND 3 CANNOT. Clause 2 pins
    // grainRng_'s state and clause 3 pins the counters, so between them they
    // see any change to WHICH grains are born and to HOW MANY draws each birth
    // costs. Neither sees a change to what a grain SOUNDS like: a reworked
    // window, an off-by-one in the borrow arithmetic, a population gain applied
    // at the wrong point, or an `advanceBack` decomposition (T015) that leaks
    // into the forward path would all leave every integer above untouched and
    // move this fingerprint.
    //
    // WHAT IT DELIBERATELY DOES NOT CATCH: anything at probability > 0. This is
    // the DEFAULT-inert clause; the reverse path's own audible criteria are
    // SC-002 and SC-011.
    SECTION("clause 1 - the 60 s default render is unchanged from the base commit") {
        // FR-002's default, asserted rather than assumed: the whole clause is
        // about the engine with NEITHER feature engaged, and a component whose
        // default had drifted would be measuring a different fixture.
        REQUIRE(engine->getGrainReverseProbability() == 0.0f);

        const std::vector<float> left =
            renderSpanCapturingLeft(*engine, VoragoGhostFix::kReferenceRenderSamples, config.seed,
                                    VoragoGhostFix::kReferenceRenderBlock);
        REQUIRE(left.size() == VoragoGhostFix::kReferenceRenderSamples);

        // No trigger was fired anywhere above, so the trigger lane must be
        // entirely unentered. Without this the clause could be satisfied by a
        // render in which triggered grains happened to cancel out.
        REQUIRE(engine->getTotalTriggeredGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getTotalReverseGrainsBorn() == std::uint64_t{0});

        const Krate::DSP::TestUtils::RenderFingerprint actual =
            Krate::DSP::TestUtils::fingerprintRender(left);

        // A fingerprint of silence would make the comparison vacuous: every
        // metric would be 0 and "within tolerance" would really be a statement
        // about compareFingerprints' 1.0e-12 denominator guard
        // (render_fingerprint.h:130).
        REQUIRE(actual.rms > 1.0e-5);
        REQUIRE(actual.peak > 1.0e-4);
        REQUIRE(actual.totalVariation > 1.0);

        const auto cmp = Krate::DSP::TestUtils::compareFingerprints(
            actual, VoragoGhostFix::kBaseCommitFingerprint, kMeasuredMetricTolerance,
            kMeasuredSampleTolerance);

        INFO(cmp.detail);
        INFO("worst metric relative error " << cmp.worstMetricRelativeError << " (bound "
                                            << cmp.metricTolerance << "), worst sample error "
                                            << cmp.worstSampleError << " (bound "
                                            << cmp.sampleTolerance << ")");

        // FR-046's paste-ready literal, emitted whenever the comparison is
        // about to fail - the noise_organism_test.cpp:3461 shape. The guard is
        // a purely runtime term, so it cannot fold to a constant conditional
        // (MSVC C4127); the "reference not captured yet" half of that TU's
        // condition has no counterpart here, because this phase's reference IS
        // captured (atmosphere_ghost_fixtures.h:476) - what is missing is the
        // measured BOUNDS, and that is what the SKIP above stands on.
        if (!cmp.withinTolerance()) {
            WARN(
                "SC-001 clause 1 reference - paste this over kBaseCommitFingerprint in "
                "atmosphere_ghost_fixtures.h section 6.1 ONLY alongside a DELIBERATE change to "
                "what Fixture A renders, refilling the PROVENANCE block in the SAME edit. "
                "Never to make this clause green:\n\n"
                << baseCommitFingerprintLiteral(actual));
        }

        REQUIRE(cmp.withinTolerance());
    }

    SECTION("clause 2 (p = 0) and clause 3 - grain stream and every counter unmoved") {
        engine->setGrainReverseProbability(0.0f);
        // FR-002's default, written explicitly rather than assumed.
        REQUIRE(engine->getGrainReverseProbability() == 0.0f);

        renderSpan(*engine, VoragoGhostFix::kReferenceRenderSamples, config.seed,
                   VoragoGhostFix::kReferenceRenderBlock);

        // --- Clause 2: the grain stream is bit-identical to the base commit.
        REQUIRE(engine->getGrainRngState() == VoragoGhostFix::kBaseCommitGrainRngState);

        // --- Clause 3: counter identity (atmosphere_engine.h:1084-1101, :1190).
        REQUIRE(engine->getTotalGrainsBorn() == VoragoGhostFix::kBaseCommitTotalBorn);
        REQUIRE(engine->getTotalGrainsRetired() == VoragoGhostFix::kBaseCommitTotalRetired);
        REQUIRE(engine->getSkippedTriggerCountPoolFull()
                == VoragoGhostFix::kBaseCommitSkipPoolFull);
        REQUIRE(engine->getSkippedTriggerCountRingCold()
                == VoragoGhostFix::kBaseCommitSkipRingCold);
        REQUIRE(engine->getLatencySamples() == VoragoGhostFix::kBaseCommitLatencySamples);
    }

    SECTION("clause 2 (p = 1) - the grain stream is STILL bit-identical") {
        engine->setGrainReverseProbability(1.0f);
        REQUIRE(engine->getGrainReverseProbability() == 1.0f);

        renderSpan(*engine, VoragoGhostFix::kReferenceRenderSamples, config.seed,
                   VoragoGhostFix::kReferenceRenderBlock);

        REQUIRE(engine->getGrainRngState() == VoragoGhostFix::kBaseCommitGrainRngState);

        // Deliberately NOT asserted here: the birth/retire counters. Clause 3
        // pins them at probability 0 only, because T013 gives a reverse grain a
        // different birth window and is entitled to change how many of the SAME
        // number of attempts are admitted. What may never change is the stream
        // state above.
    }

    // =========================================================================
    // CLAUSE 2 - THE TRIGGER ARM (spec.md:626-639)
    // =========================================================================
    //
    // The two arms above are NO-TRIGGER arms: they render a fixture in which
    // `triggerGrain()` is never called, so they pin the grain stream only on the
    // path the base commit already had. The base commit has no `triggerGrain()`
    // at all, so no base-commit literal can cover a TRIGGERED birth - which is
    // exactly why the spec makes this arm a COMPUTED-DELTA REPLICA rather than a
    // transcribed constant (Clarifications 2026-09-22, Q3).
    //
    // WHAT IT CATCHES AND THE NO-TRIGGER ARMS CANNOT: a triggered birth that
    // drew a DIFFERENT NUMBER of times from `grainRng_` than a scheduled one -
    // an extra draw to pick a "trigger flavour", a skipped draw because the
    // trigger path took a shortcut, a re-ordered draw block. Every such
    // implementation leaves the no-trigger render bit-identical and still
    // destroys FR-027's per-attempt contract, on which SC-007's determinism and
    // every future reference render stand.
    //
    // THE DRAW ACCOUNTING, derived from the shipped control flow exactly as
    // `runReplicaArm`'s comment above derives the reverse stream's:
    //   * the slot-sweep pool-full early-out returns BEFORE the draw site
    //     (`atmosphere_engine.h:1734-1740`), so a `skipPoolFull_` increment
    //     costs ZERO `grainRng_` draws;
    //   * EVERY other exit is downstream of the four draws at `:1747-1750` -
    //     the `headroom <= 2.0`, `lifetime < 2.0`, FR-014 `needed` and FR-050
    //     fill-deficit rejections all `++skipRingCold_` and return;
    //   * a successful birth reaches `++totalBorn_`.
    //   => draws since the last seeding
    //      == 4 * (getTotalGrainsBorn() + getSkippedTriggerCountRingCold()).
    // The spec states the identity in its collapsed form - "four draws per
    // observed admitted birth" - and notes it is valid because the WARM RING
    // makes attempted and admitted coincide ACROSS THE MEASURED SPAN. The
    // pre-roll starts on a COLD ring and therefore does contribute ring-cold
    // attempts, so the replica is advanced over the engine's whole life by the
    // uncollapsed identity, and the span's own `skipRingCold_` delta is asserted
    // to be ZERO so the collapsed form the spec states is what this arm actually
    // exercises. Counting the pre-roll's rejections is strictly more exact than
    // ignoring them; it is not a weakening.
    SECTION("clause 2 trigger arm - 100 triggers on a warm ring, grainRng_ replica") {
        // SC-005's trigger fixture, not Fixture A's: 12 s grains at the Vorago
        // ghost point would fill the 64-slot pool after 64 triggers and turn the
        // rest into pool-full skips, which is a different arm's subject.
        // kMinGrainSeconds (2 400 samples at 48 kHz) lets each triggered grain
        // retire before the next trigger, so the pool is empty at every fire.
        auto trig = makeTriggerEngine(0.0f);
        const std::size_t capacity = trig->getCaptureCapacitySamples();
        REQUIRE(VoragoGhostFix::preRollFullRing(*trig, 1u, 512u) == capacity);

        // The precondition the whole arm rests on, ASSERTED: a saturated ring.
        // getAvailableSamples() is min(samplesWritten_, capacity_)
        // (rolling_capture_buffer.h:443-445), so after `capacity` rendered
        // samples the filling regime - and with it FR-050 and every other
        // ring-cold rejection - is behind us.
        REQUIRE(capacity == std::size_t{65536});

        const TriggerCounters before = readCounters(*trig);

        // 100 triggers, fired ONE AT A TIME with the grain's whole life rendered
        // between them. Firing all 100 up front would hit FR-019's queue cap of
        // kMaxGrains = 64 and drop 36 of them, so the arm would measure 64
        // triggers while claiming 100.
        constexpr std::size_t kTriggerCount = 100u;
        constexpr std::size_t kGapSamples = 2560u;  // > round(0.05 * 48 000) = 2 400
        for (std::size_t k = 0; k < kTriggerCount; ++k) {
            trig->triggerGrain();
            renderSpan(*trig, kGapSamples, 1u, 512u);
        }

        const TriggerCounters after = readCounters(*trig);
        const std::uint64_t triggeredDelta = after.triggered - before.triggered;
        const std::uint64_t coldDelta = after.ringCold - before.ringCold;
        const std::uint64_t poolFullDelta = after.poolFull - before.poolFull;
        const std::uint64_t droppedDelta = after.dropped - before.dropped;
        INFO("triggeredDelta=" << triggeredDelta << " coldDelta=" << coldDelta << " poolFullDelta="
                               << poolFullDelta << " droppedDelta=" << droppedDelta
                               << " bornTotal=" << after.born << " coldTotal=" << after.ringCold);

        // The span is the WARM-RING span the spec names: nothing was refused at
        // the queue, nothing was refused for a cold ring, nothing was refused for
        // a full pool. So "attempted" and "admitted" coincide here and the
        // collapsed identity is the one being exercised.
        REQUIRE(droppedDelta == std::uint64_t{0});
        REQUIRE(coldDelta == std::uint64_t{0});
        REQUIRE(poolFullDelta == std::uint64_t{0});
        REQUIRE(triggeredDelta == static_cast<std::uint64_t>(kTriggerCount));

        // A vacuity guard: if the span produced no triggered birth the replica
        // identity below would hold for a component that ignored triggerGrain()
        // entirely.
        REQUIRE(after.born > before.born);

        // The replica: seeded exactly as the component seeds grainRng_
        // (atmosphere_engine.h:1109, `deriveStreamSeed(seedValue, kGrainSalt)`)
        // and advanced FOUR draws per attempt that reached the draw site.
        Krate::DSP::Xorshift32 replica{
            Krate::DSP::deriveStreamSeed(1u, AtmosphereEngine::kGrainSalt)};
        const std::uint64_t attempts = after.born + after.ringCold;
        for (std::uint64_t i = 0; i < attempts * 4u; ++i) {
            static_cast<void>(replica.next());
        }
        INFO("attempts=" << attempts << " replica=" << replica.state()
                         << " engine=" << trig->getGrainRngState());
        REQUIRE(replica.state() == trig->getGrainRngState());
    }
}

// =============================================================================
// SC-007 (c) part 1 - the reverse stream's seed state
// =============================================================================
//
// FR-004 / FR-005. Three things, none of which any other criterion can see:
//
//   1. reverseRng_ is its OWN stream. If it were seeded from the raw engine seed
//      it would correlate with any other lane seeded the same way, and if it
//      were seeded from deriveStreamSeed(seed, kGrainSalt) it would be a second
//      view of grainRng_ - which would make ADR-2's whole point (the grain
//      stream stays bit-identical) false while every fingerprint criterion
//      still passed.
//   2. reset() restores it. Every other stream is re-seeded in reset()
//      (atmosphere_engine.h:534-556); a stream that is not re-seeded there
//      resumes mid-sequence and the post-reset render does not reproduce the
//      original - the exact failure the scheduler's own comment at :546-550
//      records.
//   3. setSeed() re-seeds it mid-render (atmosphere_engine.h:1013-1020 seeds
//      every other stream there).
//
// Clause (c) part 2 - the replica-RNG arm that proves FR-006's unconditional
// draw - is the middle SECTION of this case (T009).
//
// T018 APPENDS CLAUSES (a) AND (b), the render-level half of the criterion,
// which the three (c) arms above cannot reach: every one of them reads an RNG
// STATE, so an engine whose grain rendering depended on something other than
// the seed - a `static` or thread-local left in the reverse path, a time- or
// address-derived value, an uninitialised read - would satisfy all of them and
// still render differently on the second run.
//   (a) two separately constructed engines at the same seed, the same
//       configuration and the same trigger schedule agree within the DEFAULT
//       render_fingerprint.h tolerances, at probability 0, 0.5 and 1. The
//       default constants are the right bounds here and FR-046's measured-bounds
//       protocol does NOT apply: both renders come from the SAME BINARY, so
//       there is no cross-toolchain spread to absorb - the only bound that could
//       be crossed is one the engine itself opened.
//   (b) two engines at DIFFERENT seeds at probability 0.5 separate by more than
//       100 x kMetricTolerance - the Phase-10 SC-026 separation form. Without
//       it, (a) would be satisfiable by an engine that ignored its seed
//       entirely.
// Clause (d), the 100 000-grain reverse-fraction calibration sweep, is [long]
// and lives in atmosphere_ghost_longrun_test.cpp (T019), not here.
TEST_CASE("AtmosphereGhost_Determinism", "[atmosphere][ghost]") {
    using Krate::DSP::deriveStreamSeed;

    constexpr std::uint32_t kSeedA = 1u;
    constexpr std::uint32_t kSeedB = 7919u;  // distinct, otherwise arbitrary
    constexpr std::size_t kSpanSamples = 8192u;

    VoragoGhostFix::GhostConfig config{};
    config.seed = kSeedA;

    AtmosphereEngine engine;
    VoragoGhostFix::applyVoragoGhost(engine, config);

    // The state immediately after prepare()+setSeed(kSeedA). prepare() ends with
    // reset() (atmosphere_engine.h:400-402) and setSeed() re-seeds every stream,
    // so this is "the value after prepare() at the same seed" that clause (c)
    // names.
    const std::uint32_t seededState = engine.getReverseRngState();

    SECTION("(c) the reverse stream is its own - not the raw seed, not the grain stream") {
        REQUIRE(seededState == deriveStreamSeed(kSeedA, AtmosphereEngine::kReverseSalt));
        REQUIRE(seededState != kSeedA);
        REQUIRE(seededState != deriveStreamSeed(kSeedA, AtmosphereEngine::kGrainSalt));

        // deriveStreamSeed never yields 0 (core/random.h:102-111): a zero seed
        // would be silently substituted by Xorshift32::seed() (:72-74) and two
        // lanes hashing to 0 would COLLAPSE ONTO ONE STREAM.
        REQUIRE(seededState != std::uint32_t{0});
    }

    SECTION("(c) getReverseRngState() after reset() equals its value after prepare()") {
        renderSpan(engine, kSpanSamples, kSeedA);
        engine.reset();
        REQUIRE(engine.getReverseRngState() == seededState);
    }

    SECTION("(c) setSeed() mid-render re-seeds the reverse stream") {
        renderSpan(engine, kSpanSamples, kSeedA);
        const std::uint32_t midRenderState = engine.getReverseRngState();

        engine.setSeed(kSeedB);
        const std::uint32_t reseededState = engine.getReverseRngState();

        REQUIRE(reseededState == deriveStreamSeed(kSeedB, AtmosphereEngine::kReverseSalt));
        REQUIRE(reseededState != midRenderState);
    }

    // =========================================================================
    // SC-007 (c) PART 2 - the replica arm. FR-006's "exactly one draw per
    // birth, UNCONDITIONALLY".
    // =========================================================================
    //
    // This is the arm that is RED at T009 and turns green at T010. Nothing else
    // in the phase can see the two failures it is built for:
    //
    //   * a GUARDED draw - `if (reverseProbability_ > 0.0f) { ... }` - is the
    //     natural micro-optimisation and passes every audible criterion,
    //     because at probability 0 no grain is reversed either way. It fails
    //     the p = 0 arm below, where the stream must advance ANYWAY: the
    //     control value is a gain on a FIXED stream, not a switch that moves
    //     the stream's position. Without that, sweeping the probability during
    //     a render would desynchronise every subsequent grain's direction from
    //     the seed, and SC-007 (a)'s determinism would hold only for constant
    //     probability.
    //   * TWO draws per birth (e.g. a stray `nextUnipolar()` in a log line, or
    //     a draw taken before the pool-full early-out) fails BOTH arms.
    //
    // The replica starts at the stream's ORIGIN - deriveStreamSeed(seed,
    // kReverseSalt), the value reset() installs (atmosphere_engine.h:564) - and
    // is walked forward by the draw count derived in runReplicaArm()'s banner,
    // so the check covers the pre-roll as well as the measured span and there
    // is no point at which the test simply copies the engine's own state.
    SECTION("(c) one reverse draw per birth, unconditionally - at probability 0 and 1") {
        // 10 s at 48 kHz. At density 0.30 the nominal interonset is 3.33 s and
        // the shortest jittered one is shorter still, so the span always
        // contains births; the REQUIRE below asserts that rather than assuming
        // it, so a scheduler change cannot quietly make this arm vacuous.
        constexpr std::size_t kReplicaSpanSamples = 480000u;
        constexpr std::size_t kReplicaSpanBlock = 512u;

        const std::uint32_t origin = deriveStreamSeed(kSeedA, AtmosphereEngine::kReverseSalt);

        const ReplicaArm forward =
            runReplicaArm(0.0f, kSeedA, kReplicaSpanSamples, kReplicaSpanBlock);
        const ReplicaArm reversed =
            runReplicaArm(1.0f, kSeedA, kReplicaSpanSamples, kReplicaSpanBlock);

        const auto checkArm = [origin](const char* label, const ReplicaArm& arm) {
            INFO("arm: " << label << "; preRollDraws=" << arm.preRollDraws
                         << " spanBirths=" << arm.spanBirths
                         << " spanRingColdDelta=" << arm.spanRingColdDelta
                         << " spanPoolFullDelta=" << arm.spanPoolFullDelta);

            // --- Fixture preconditions, asserted so a green run means what it
            //     says. The ring is FULL (preRollFullRing rendered exactly
            //     getCaptureCapacitySamples() samples), so no admission test
            //     may reject inside the span; with both skip deltas at zero,
            //     "attempts past the pool-full early-out" and "births" are the
            //     same number over the span, which is what lets the one-draw-
            //     per-birth identity be stated on getTotalGrainsBorn() alone.
            REQUIRE(arm.spanRingColdDelta == std::uint64_t{0});
            REQUIRE(arm.spanPoolFullDelta == std::uint64_t{0});
            REQUIRE(arm.spanBirths > std::uint64_t{0});
            REQUIRE(arm.preRollDraws > std::uint64_t{0});

            // --- The pre-roll's own accounting: born + ring-cold rejections.
            REQUIRE(advanceReplica(origin, arm.preRollDraws) == arm.reverseStateAtSpanStart);

            // --- THE CLAUSE: exactly one nextUnipolar() per birth in the span.
            REQUIRE(advanceReplica(arm.reverseStateAtSpanStart, arm.spanBirths)
                    == arm.reverseStateAtSpanEnd);

            // --- The stream actually moved. Cheap, and it localises a failure
            //     in which the draw was never inserted at all (the T009 red
            //     state) versus one in which it was inserted the wrong number
            //     of times.
            REQUIRE(arm.reverseStateAtSpanEnd != origin);
        };

        checkArm("probability 0", forward);
        checkArm("probability 1", reversed);

        // The two weaker equalities the task keeps because they cost nothing
        // and split the failure space: the stream's position must be a function
        // of the SEED alone, never of the control value.
        REQUIRE(forward.reverseStateAtSpanStart == reversed.reverseStateAtSpanStart);
        REQUIRE(forward.reverseStateAtSpanEnd == reversed.reverseStateAtSpanEnd);
        REQUIRE(forward.spanBirths == reversed.spanBirths);
    }

    // =========================================================================
    // SC-007 (a) - same seed, same configuration, same trigger schedule
    // =========================================================================
    //
    // Run at all THREE probabilities on purpose. Probability 0 is the arm a
    // reverse-path defect cannot reach at all, so it isolates "the engine is
    // deterministic" from "the reverse path is deterministic"; probability 1
    // puts every grain on the backwards read; and 0.5 is the only one that
    // exercises the DRAW as a decision - a per-grain branch whose two sides
    // must both be reproducible from the seed alone. An implementation that
    // reversed grains by, say, hashing the grain's slot index would pass 0 and
    // 1 and fail 0.5 the moment slot assignment changed with pool occupancy.
    //
    // DEFAULT TOLERANCES, DELIBERATELY. Both renders are produced by the same
    // binary on the same machine in the same process, so the shared
    // cross-toolchain constants are not merely adequate here, they are the
    // TIGHTEST honest bound available; passing FR-046's measured bounds would
    // be loosening a comparison that has no toolchain spread in it.
    SECTION("(a) two engines at the same seed and schedule render the same audio") {
        // A plain loop rather than GENERATE: this TU includes only
        // <catch2/catch_test_macros.hpp> and <catch2/catch_approx.hpp>, and a
        // generator would drag in <catch2/generators/catch_generators.hpp> for
        // three values.
        constexpr std::array<float, 3> kProbabilities{0.0f, 0.5f, 1.0f};

        for (const float probability : kProbabilities) {
            INFO("probability = " << probability);

            const DeterminismArm first = runDeterminismArm(kSeedA, probability);
            const DeterminismArm second = runDeterminismArm(kSeedA, probability);

            requireArmIsNotVacuous("first", first);
            requireArmIsNotVacuous("second", second);

            // The admission DECISIONS must match before the audio is compared:
            // a pair that disagreed on how many grains were born would also
            // disagree on the fingerprint, and this separates the two failure
            // modes.
            REQUIRE(first.bornDelta == second.bornDelta);
            REQUIRE(first.reverseBornDelta == second.reverseBornDelta);
            REQUIRE(first.triggeredDelta == second.triggeredDelta);
            REQUIRE(first.ringColdDelta == second.ringColdDelta);

            // The probability really is a gain on the reverse decision and not
            // an inert control: at 0 nothing born is reversed. At 1 and at 0.5
            // the split is whatever the seed says, so it is REPORTED by
            // requireArmIsNotVacuous' INFO rather than predicted here - a fixed
            // expectation on an RNG sequence would be a golden, and SC-007 (d)
            // measures that fraction statistically instead.
            if (probability == 0.0f) {
                REQUIRE(first.reverseBornDelta == std::uint64_t{0});
            } else {
                REQUIRE(first.reverseBornDelta > std::uint64_t{0});
            }

            const auto cmp = Krate::DSP::TestUtils::compareFingerprints(first.left, second.left);
            INFO(cmp.detail);
            INFO("worst metric relative error " << cmp.worstMetricRelativeError << " (bound "
                                                << cmp.metricTolerance << "), worst sample error "
                                                << cmp.worstSampleError << " (bound "
                                                << cmp.sampleTolerance << ")");
            REQUIRE(cmp.withinTolerance());
        }
    }

    // =========================================================================
    // SC-007 (b) - different seeds separate, by two orders of magnitude
    // =========================================================================
    //
    // The anti-vacuity half of (a): an engine that ignored its seed - or that
    // derived every per-grain value from a constant - would satisfy (a)
    // perfectly. The Phase-10 SC-026 separation form asks for a margin of
    // 100 x kMetricTolerance (2.5e-2), which is far above any legal codegen
    // spread and far below what two independently seeded grain clouds actually
    // produce.
    //
    // THE EXCITATION IS THE SAME IN BOTH ARMS (kDetExciteSeed). Only the ENGINE
    // seed differs, so the separation cannot be credited to the input audio.
    SECTION("(b) two engines at different seeds separate at probability 0.5") {
        constexpr float kProbability = 0.5f;

        const DeterminismArm armA = runDeterminismArm(kSeedA, kProbability);
        const DeterminismArm armB = runDeterminismArm(kSeedB, kProbability);

        requireArmIsNotVacuous("seed A", armA);
        requireArmIsNotVacuous("seed B", armB);

        const auto cmp = Krate::DSP::TestUtils::compareFingerprints(armA.left, armB.left);
        INFO(cmp.detail);
        INFO("worst metric relative error "
             << cmp.worstMetricRelativeError << " against a separation floor of "
             << (100.0 * Krate::DSP::TestUtils::kMetricTolerance) << "; worst sample error "
             << cmp.worstSampleError);
        REQUIRE(cmp.worstMetricRelativeError > 100.0 * Krate::DSP::TestUtils::kMetricTolerance);
    }

    // =========================================================================
    // The weak consistency check, kept and LABELLED weak
    // =========================================================================
    //
    // A render in which no grain is born must leave getTotalReverseGrainsBorn()
    // at zero. This is worth a line because the counter is the one SC-007 (d)
    // divides by, and a counter incremented on the DRAW rather than on the
    // BIRTH would make that fraction meaningless while every other arm in this
    // file stayed green - at probability 1 the reverse stream is drawn from on
    // every attempt that clears the pool-full early-out, including the ones
    // the ring-cold clause then rejects (the draw accounting in runReplicaArm's
    // banner above).
    //
    // IT IS NOT, AND MUST NEVER BE CITED AS, FR-008's EVIDENCE. FR-008 is about
    // live grains keeping their `reversed` flag across a mid-render setSeed(),
    // and its evidence is T014 (d). This arm only says a counter that counts
    // births does not move when there are none.
    SECTION("weak consistency - a render with no birth leaves the reverse counter at zero") {
        // 64 samples - one kControlChunkSamples - on a freshly prepared engine,
        // so the ring holds at most 64 samples against the `needed == 128` this
        // fixture's neutral controls collapse to (see makeTriggerEngine's
        // banner). The free scheduler tick at sample 0 therefore ATTEMPTS a
        // birth and is refused ring-cold, whichever order the block's capture
        // write and the scheduler tick happen in.
        constexpr std::size_t kNoBirthSamples = AtmosphereEngine::kControlChunkSamples;

        // Named `coldEngine` rather than `engine`: this case already has an
        // `AtmosphereEngine engine` at TEST_CASE scope, and shadowing it would
        // leave a reader of a failure unsure which one moved.
        auto coldEngine = makeTriggerEngine(1.0f);  // probability 1: the draw is taken
        REQUIRE(coldEngine->getGrainReverseProbability() == 1.0f);

        renderSpan(*coldEngine, kNoBirthSamples, kSeedA, kNoBirthSamples);

        const TriggerCounters counters = readCounters(*coldEngine);
        INFO("born=" << counters.born << " reverseBorn=" << counters.reverseBorn
                     << " ringCold=" << counters.ringCold << " poolFull=" << counters.poolFull);

        // The premise: nothing was born, and the attempt that would have been
        // was refused for the documented reason.
        REQUIRE(counters.born == std::uint64_t{0});
        REQUIRE(counters.ringCold >= std::uint64_t{1});

        // The check itself.
        REQUIRE(counters.reverseBorn == std::uint64_t{0});
    }
}

// =============================================================================
// SC-006's COMPILE-TIME HALF - the append-only bar, as far as a TU can carry it
// =============================================================================
//
// SC-006 has six clauses and this case discharges exactly one and a half of
// them; the INFO below records where each of the six actually lives, so a reader
// of a green run is not left believing the case proved more than it did. The
// static_asserts are the part a compiler can enforce:
//
//   - kReverseSalt sits STRICTLY ABOVE kDriftSaltBase + kMaxGrains (FR-004).
//     The drift lanes occupy kDriftSaltBase + i for i in [0, kMaxGrains), so
//     anything at or below that bar would collide with a per-grain drift stream
//     and silently share its sequence. This is the same disjointness the header
//     already static_asserts for the shipped salts (atmosphere_engine.h:360).
//   - The three shipped capacities are UNCHANGED. A phase that quietly moved
//     kMaxGrains, kMinAgeSamples or kControlChunkSamples would invalidate
//     Seraphis's measured baselines and FR-015's pass-B validity argument
//     (kMinAgeSamples >= kControlChunkSamples, static-asserted at :345) while
//     every runtime criterion here still passed.
TEST_CASE("AtmosphereGhost_AppendOnly", "[atmosphere][ghost]") {
    static_assert(AtmosphereEngine::kReverseSalt > AtmosphereEngine::kDriftSaltBase
                                                       + AtmosphereEngine::kMaxGrains,
                  "salt ranges must not overlap");
    static_assert(AtmosphereEngine::kMaxGrains == 64);            // :189, unchanged
    static_assert(AtmosphereEngine::kMinAgeSamples == 64);        // :251, unchanged
    static_assert(AtmosphereEngine::kControlChunkSamples == 64);  // :271, unchanged

    INFO("SC-006 has SIX clauses; this case carries only the compile-time part.\n"
         "  1. zero modified files under dsp/tests/unit/systems/ except\n"
         "     vorago_perf_test.cpp (FR-047's seven-constant move) - discharged by\n"
         "     tools/check-seraphis-green.js (T004) and the phase compliance table.\n"
         "  2. atmosphere_engine.h deletes lines at only FR-045's six anchor sites -\n"
         "     discharged by T023's APPEND_ONLY_HEADERS entry plus git diff HEAD -U0.\n"
         "  3. the full dsp_systems_tests suite, every atmosphere_engine_* and\n"
         "     seraphis_* case green with no assertion edited - discharged by the\n"
         "     phase's own suite runs (plan section 4.4).\n"
         "  4. dsp_effects_tests / dsp_processors_tests / vorago_* green, SC-027's\n"
         "     VoragoEngine_GhostConfiguration unedited - same runs.\n"
         "  5. zero modified files under plugins/seraphis/, Seraphis + seraphis_tests\n"
         "     build warning-free and pluginval passes - the untouched-consumer gate.\n"
         "  6. the FR-013 / FR-025 structural gate: a git diff HEAD -U0 code review\n"
         "     at renderGrainSpan and the pass-A loop - discharged by review, not\n"
         "     by any timing arm (FR-025 states why a CPU criterion cannot see it).\n"
         "  THIS CASE: the kReverseSalt disjointness (part of clause 2's intent) and\n"
         "  the three unchanged capacities. Nothing else.");
    SUCCEED();
}

// =============================================================================
// T011 HELPERS - contiguous, block-observable rendering
// =============================================================================
//
// `renderSpan()` above is deliberately unsuitable for the three cases below:
// it generates a FRESH excitation on every call and hands back nothing between
// blocks. SC-011 needs the pre-roll and the measured span to be ONE continuous
// signal (fixtures section 6.3's Fixture C protocol says "over the whole
// 280 000-sample span"), and SC-004 needs to read
// `getLastBornGrainLifetimeSamples()` after EVERY 64-sample block. Both are
// what `ContinuousRender` below provides.
namespace {

/// @brief One excitation buffer plus a render cursor, driven in fixed blocks.
///
/// The whole excitation is generated ONCE at construction, so a fixture that
/// renders `[0, A)` and then continues to `[A, B)` feeds the engine the same
/// uninterrupted signal it would have got from a single call - there is no
/// generator restart at the boundary and therefore no discontinuity that could
/// be mistaken for a grain artefact.
///
/// PREFIX IDENTITY, stated because SC-011 (c) depends on it: the generator in
/// `VoragoGhostFix::excitePinkPlusTone` is sequential and its per-sample state
/// depends only on the samples before it, so the first N samples of a longer
/// call are bit-identical to an N-sample call at the same seed. Fixture C's
/// stored constants (fixtures section 6.3) were harvested over exactly
/// 280 000 samples, and arm (c) below renders exactly 280 000 samples; arms (a)
/// and (a2) use a longer buffer whose first 280 000 samples are the same signal.
class ContinuousRender {
public:
    ContinuousRender(std::size_t totalSamples, std::uint32_t exciteSeed, std::size_t blockSize)
        : inLeft_(totalSamples, 0.0f),
          inRight_(totalSamples, 0.0f),
          outLeft_((blockSize == 0) ? std::size_t{1} : blockSize, 0.0f),
          outRight_((blockSize == 0) ? std::size_t{1} : blockSize, 0.0f),
          block_((blockSize == 0) ? std::size_t{1} : blockSize) {
        VoragoGhostFix::excitePinkPlusTone(inLeft_, inRight_, exciteSeed);
    }

    /// @brief Render forward until the cursor reaches `target`, calling
    ///        `onBlock(renderedSoFar)` after every block.
    ///
    /// The callback runs AFTER `processStereoBlock` returns, so every engine
    /// accessor it reads reports the state at that block boundary - which is
    /// the observation grid SC-004 and SC-011 (a2) both specify.
    /// `onBlock` is a CONST LVALUE REFERENCE, not a forwarding reference: it is
    /// invoked once per block inside the loop below, so forwarding it would be a
    /// use-after-move on the second iteration. Every observer passed here is a
    /// non-mutable lambda or a free function, both of which bind and call fine.
    template <typename OnBlock>
    void renderTo(AtmosphereEngine& engine, std::size_t target, const OnBlock& onBlock) {
        const std::size_t limit = std::min(target, inLeft_.size());
        while (rendered_ < limit) {
            const std::size_t n = std::min(block_, limit - rendered_);
            engine.processStereoBlock(inLeft_.data() + rendered_, inRight_.data() + rendered_,
                                      outLeft_.data(), outRight_.data(), n);
            rendered_ += n;
            lastBlock_ = n;
            onBlock(rendered_);
        }
    }

    [[nodiscard]] std::size_t rendered() const noexcept { return rendered_; }
    [[nodiscard]] const float* lastOutLeft() const noexcept { return outLeft_.data(); }
    [[nodiscard]] const float* lastOutRight() const noexcept { return outRight_.data(); }
    [[nodiscard]] std::size_t lastBlockSamples() const noexcept { return lastBlock_; }

private:
    std::vector<float> inLeft_;
    std::vector<float> inRight_;
    std::vector<float> outLeft_;
    std::vector<float> outRight_;
    std::size_t block_;
    std::size_t rendered_ = 0;
    std::size_t lastBlock_ = 0;
};

/// @brief A `renderTo` callback that observes nothing.
inline void noBlockObserver(std::size_t) noexcept {}

/// The 64-sample observation grid SC-004 and SC-011 both name. It is
/// deliberately `kControlChunkSamples`: the engine advances its control grid
/// once per 64 samples (`atmosphere_engine.h:271`), so a 64-sample block puts
/// exactly one control step between two observations.
constexpr std::size_t kObservationBlock = 64u;

}  // namespace

// =============================================================================
// SC-011 - FR-050 rejects inside the FILLING regime, and admits no earlier than
//          its threshold. THE binding criterion for FR-050.
// =============================================================================
//
// WHY THIS CASE HAS TO EXIST AT ALL. Every other reverse fixture in the phase
// pre-rolls to a full ring (VoragoGhostFix::preRollFullRing), which makes
// FR-050's `t* = min(lifetime, capacity - available)` exactly 0 and the whole
// clause a no-op. A wrong-signed FR-050, a wrong-termed FR-050 and an ENTIRELY
// ABSENT FR-050 all pass those fixtures. SC-003 (c) is not a backstop either:
// it folds the COMPUTED read age, which stays inside [64, C - 2] precisely
// while the read is clamped and stale (plan P-1). This is the one case that
// renders the filling-ring regime, and the only place the clause is falsifiable.
//
// CONFIGURATION: SC-003's Vorago ghost point (VoragoGhostFix::GhostConfig -
// 48 kHz, seed 1, captureSeconds = 20, density 0.30, grainSeconds 12,
// pitchSemitones -12, positionSpread 0.90, decorrelation 0.85, blur on) at
// reverse probability 1, driven in 64-sample blocks, with ONE DECLARED
// DEVIATION: pitchSpread = 0 and driftRangeSemitones = 0.
//
// The deviation is not a convenience. It pins ratioMax == ratioMin ==
// 2^(-12/12) = 0.5 EXACTLY, so every threshold below is computable by the test
// from public accessors. At the Vorago point's drift range of 2 the per-grain
// ratioMax is a draw with no getter, and a test forced to use the CONFIGURED
// upper bound would assert a threshold larger than the one FR-050 actually
// applied - red on correct code. The deviation changes nothing FR-050 does; the
// clause is evaluated identically, only with a known rMax.
//
// EVERY NUMBER BELOW IS RECOMPUTED IN THE TEST, NEVER TRANSCRIBED. For the
// record, at this configuration they come out as: C = 1 048 576, L' = 576 000,
// deficit = ceil(0.5 * 576 000) = 288 000, admission threshold
// kMinAgeSamples + kMinAgeSamples + deficit = 288 128 samples (6.003 s), branch
// crossover C - L' = 472 576.
TEST_CASE("AtmosphereGhost_ReverseFillDeficit", "[atmosphere][ghost]") {
    using Krate::DSP::semitonesToRatio;

    VoragoGhostFix::GhostConfig config{};  // seed 1, 48 kHz, the FR-017 seven

    // The two fixture constants this case shares with Fixture C (fixtures
    // section 6.3), named there so arm (c) and arms (a)/(a2) cannot drift apart.
    constexpr std::size_t kPreRollSamples = VoragoGhostFix::kShortPreRollSamples;  // 240 000
    constexpr std::size_t kRejectSpan = VoragoGhostFix::kShortPreRollSpanSamples;  // 40 000
    static_assert(VoragoGhostFix::kShortPreRollBlock == kObservationBlock,
                  "Fixture C was harvested on the 64-sample grid this case renders on");

    const double guard = static_cast<double>(AtmosphereEngine::kMinAgeSamples);

    SECTION("(a) rejection inside the filling regime, then (a2) admission at the threshold") {
        auto engine = std::make_unique<AtmosphereEngine>();
        VoragoGhostFix::applyVoragoGhost(*engine, config);
        // --- the one declared deviation, applied AFTER applyVoragoGhost so the
        //     shared operating point is still the thing being deviated from.
        engine->setPitchSpread(0.0f);
        engine->setDriftRangeSemitones(0.0f);
        engine->setGrainReverseProbability(1.0f);
        REQUIRE(engine->getGrainReverseProbability() == 1.0f);

        // ---------------------------------------------------------------------
        // Derived quantities, all from public accessors and the component's own
        // shipped semitonesToRatio (core/pitch_utils.h:23) - the SAME function
        // AtmosphereEngine::ratioAtPitch calls (:1599-1601), so rMax here is
        // bit-identical to the rMax the admission arithmetic used.
        // ---------------------------------------------------------------------
        const double capacity = static_cast<double>(engine->getCaptureCapacitySamples());
        REQUIRE(engine->getCaptureCapacitySamples() == VoragoGhostFix::kBaseCommitCaptureCapacity);

        // pitchSpread = 0 and driftRange = 0 collapse the birth pitch envelope to
        // the single configured value, so semisLo == semisHi == pitchSemitones.
        const double rMax = static_cast<double>(semitonesToRatio(config.pitchSemitones));
        REQUIRE(rMax == 0.5);  // exact: std::pow(2.0f, -1.0f) is exactly 0.5f

        // `requested` is the engine's own grain length in samples (the
        // std::round(grainSeconds * sampleRate) at :1733).
        const double requested =
            std::round(static_cast<double>(config.grainSeconds) * config.sampleRate);

        // FIXTURE VALIDITY: the grain must NOT be truncated, or L' below is not
        // `requested`. The reverse window width is w = 1 + rMax (FR-014), and the
        // worst case is the largest decorrelation draw, decorrelation *
        // kMaxDecorrelationMs * sampleRate. If this ever stops holding, the arm's
        // thresholds would be computed against the wrong lifetime - so it is
        // asserted rather than assumed.
        const double w = 1.0 + rMax;
        const double decorrMax = static_cast<double>(config.decorrelation)
                                 * static_cast<double>(AtmosphereEngine::kMaxDecorrelationMs)
                                 * 0.001 * config.sampleRate;
        const double slackMin = capacity - 2.0 - guard - guard - decorrMax - 2.0;
        REQUIRE(w * requested <= slackMin);
        const double lifetimeSamples = requested;  // L' = 576 000

        // The admission threshold at the END of the rejecting span - the LARGEST
        // `available` the arm reaches, hence the SMALLEST threshold it must clear.
        const double availableAtSpanEnd = static_cast<double>(kPreRollSamples + kRejectSpan);
        const double admitThresholdSamples =
            guard + guard
            + std::ceil(rMax * std::min(lifetimeSamples, capacity - availableAtSpanEnd));

        // THE DRIFT GUARD. If kMinAgeSamples, the capacity or the grain length
        // ever move, the arm must fail HERE rather than silently slide into the
        // admitting regime and assert "no births" where births are legal.
        INFO("C=" << capacity << " L'=" << lifetimeSamples << " rMax=" << rMax
                  << " admitThresholdSamples=" << admitThresholdSamples);
        REQUIRE(availableAtSpanEnd < admitThresholdSamples);

        // ---------------------------------------------------------------------
        // The fixture: ONE continuous excitation over the whole 17 s, rendered on
        // the 64-sample grid. 816 000 = the 5 s pre-roll plus one full grain life.
        // ---------------------------------------------------------------------
        const std::size_t kTotalSamples =
            kPreRollSamples + static_cast<std::size_t>(lifetimeSamples);
        ContinuousRender render(kTotalSamples, config.seed, kObservationBlock);

        // Pre-roll to a DELIBERATELY FILLING ring: 240 000 samples, stated as a
        // sample count (never a duration) and NOT preRollFullRing - a full ring
        // is precisely the regime in which FR-050 is vacuous.
        render.renderTo(*engine, kPreRollSamples, noBlockObserver);
        REQUIRE(render.rendered() == kPreRollSamples);

        const std::uint64_t born0 = engine->getTotalGrainsBorn();
        const std::uint64_t cold0 = engine->getSkippedTriggerCountRingCold();
        const std::uint64_t poolFull0 = engine->getSkippedTriggerCountPoolFull();

        // ---------------------------------------------------------------------
        // UNCOMMENTED AT T016 (tasks.md T016's closing clause).
        //
        // One fired trigger is what makes the rejection arm's lower bound below
        // (">= 1 attempt") guaranteed rather than seed-dependent: over a
        // 40 000-sample span the density scheduler at density 0.30 / jitter 0.5
        // is not guaranteed to tick at all, its shortest interonset being
        // 160 000 * 0.75 = 120 000 samples. It is consumed on the FIRST sample of
        // the next block (FR-020: at most one per sample, in sample order), which
        // is inside the rejecting span.
        // ---------------------------------------------------------------------
        engine->triggerGrain();

        // (a2) records the FIRST birth boundary; the observer is installed here
        // so it covers the rejecting span too - if a birth happens inside (a)'s
        // span the (a) assertion below fails first and this reports where.
        bool sawBirth = false;
        double aBorn = 0.0;
        double birthAgeAtBirth = 0.0;
        std::uint64_t lifetimeAtBirth = 0u;
        const auto observeFirstBirth = [&](std::size_t renderedSoFar) {
            if (!sawBirth && engine->getTotalGrainsBorn() > born0) {
                sawBirth = true;
                // `available` saturates at the capacity
                // (rolling_capture_buffer.h:443-445), and reading it at the BLOCK
                // END means it is never smaller than the `available` the
                // admission itself saw - which keeps the inequality a NECESSARY
                // condition rather than an approximate one.
                aBorn = static_cast<double>(
                    std::min(renderedSoFar, engine->getCaptureCapacitySamples()));
                birthAgeAtBirth = static_cast<double>(engine->getLastBornGrainBirthAgeSamples());
                lifetimeAtBirth = engine->getLastBornGrainLifetimeSamples();
            }
        };

        // --- (a) THE REJECTION ARM -------------------------------------------
        render.renderTo(*engine, kPreRollSamples + kRejectSpan, observeFirstBirth);
        REQUIRE(render.rendered() == kPreRollSamples + kRejectSpan);

        const std::uint64_t bornDelta = engine->getTotalGrainsBorn() - born0;
        const std::uint64_t coldDelta = engine->getSkippedTriggerCountRingCold() - cold0;
        const std::uint64_t poolFullDelta = engine->getSkippedTriggerCountPoolFull() - poolFull0;

        INFO("(a) bornDelta=" << bornDelta << " coldDelta=" << coldDelta
                              << " poolFullDelta=" << poolFullDelta);

        // THE TEETH. With FR-050 absent the shipped admission test needs only
        // `needed <= 92 488` against available = 240 000, so an attempt inside
        // this span is admitted at once and this assertion is red.
        REQUIRE(bornDelta == std::uint64_t{0});
        // Nothing was rejected for the WRONG reason.
        REQUIRE(poolFullDelta == std::uint64_t{0});
        // At most one scheduler tick (shortest interonset 120 000 > 40 000) plus,
        // once T017 lands, the one fired trigger. ATTEMPTS ARE BOUNDED, NEVER
        // PREDICTED: samplesUntilNextGrain_ is a draw (grain_scheduler.h:80-84).
        REQUIRE(coldDelta <= std::uint64_t{2});
        // ---------------------------------------------------------------------
        // UNCOMMENTED AT T016, with the triggerGrain() call above. The fired
        // trigger is what guarantees the span contains at least one ATTEMPT,
        // which is what stops `bornDelta == 0` above from being vacuously true.
        // ---------------------------------------------------------------------
        REQUIRE(coldDelta >= std::uint64_t{1});

        // --- (a2) THE ADMISSION ARM ------------------------------------------
        // Continue the SAME fixture to the end of the grain life. The threshold
        // IS reached, and not before.
        render.renderTo(*engine, kTotalSamples, observeFirstBirth);
        REQUIRE(render.rendered() == kTotalSamples);

        // A configuration that never births at all would make (a) vacuous, so
        // the absence of a birth is a fixture defect and fails loudly.
        REQUIRE(sawBirth);

        const double lpAtBirth = static_cast<double>(lifetimeAtBirth);
        const double neededLowerBound = std::ceil(birthAgeAtBirth) + guard
                                        + std::ceil(rMax * std::min(lpAtBirth, capacity - aBorn));
        INFO("(a2) A_born=" << aBorn << " birthAge=" << birthAgeAtBirth << " L'=" << lpAtBirth
                            << " rhs=" << neededLowerBound);
        // NECESSARY-CONDITION FORM, and deliberately so: the drawn decorrAge >= 0
        // is unobservable (no getter), so the test uses the decorr-FREE lower
        // bound on `needed`. The :1810 clip cannot bind here - birthAge <= 91 200
        // against capacity - 2 - guard = 1 048 510.
        REQUIRE(aBorn >= neededLowerBound);
    }

    // =========================================================================
    // (c) THE FORWARD ARM - "never evaluated for a forward grain", pinned.
    // =========================================================================
    //
    // FR-050 is reverse-only, and a forward render at a FILLING ring is exactly
    // where a misplaced clause shows up. SC-001 clause 3 pins only the full-ring
    // case, so without this arm a clause that fired for forward grains too would
    // pass every other criterion in the phase.
    //
    // PROTOCOL: exactly Fixture C of atmosphere_ghost_fixtures.h section 6.3 -
    // applyVoragoGhost() with the default GhostConfig, then SC-011's one declared
    // deviation, excitePinkPlusTone(.., 1u) over the whole 280 000-sample span,
    // rendered front to back in blocks of 64. No trigger is fired: triggerGrain()
    // does not exist at the base commit, so a fired trigger would not be
    // measuring against these figures.
    //
    // THERE IS NO CLAUSE (b). "Admitted implies never stale" is a THEOREM
    // (plan section A-5): FR-050 admits iff A >= needed + ceil(rMax * t*), which
    // is exactly the negation of the crossing condition, so any arm that first
    // REQUIREs a birth and then bounds the read age cannot fail. The protected
    // quantity is measured only in T013's clause-disabled run.
    SECTION("(c) forward arm at the filling ring - base-commit counters, unmoved") {
        auto engine = std::make_unique<AtmosphereEngine>();
        VoragoGhostFix::applyVoragoGhost(*engine, config);
        engine->setPitchSpread(0.0f);
        engine->setDriftRangeSemitones(0.0f);
        engine->setGrainReverseProbability(0.0f);
        REQUIRE(engine->getGrainReverseProbability() == 0.0f);
        REQUIRE(engine->getCaptureCapacitySamples() == VoragoGhostFix::kBaseCommitCaptureCapacity);

        ContinuousRender render(kPreRollSamples + kRejectSpan, config.seed,
                                VoragoGhostFix::kShortPreRollBlock);
        render.renderTo(*engine, kPreRollSamples + kRejectSpan, noBlockObserver);
        REQUIRE(render.rendered() == kPreRollSamples + kRejectSpan);

        // Integers, no tolerance - the stored references of fixtures section 6.3.
        REQUIRE(engine->getTotalGrainsBorn() == VoragoGhostFix::kBaseCommitShortPreRollBorn);
        REQUIRE(engine->getTotalGrainsRetired() == VoragoGhostFix::kBaseCommitShortPreRollRetired);
        REQUIRE(engine->getSkippedTriggerCountRingCold()
                == VoragoGhostFix::kBaseCommitShortPreRollRingCold);
        REQUIRE(engine->getSkippedTriggerCountPoolFull()
                == VoragoGhostFix::kBaseCommitShortPreRollPoolFull);
        REQUIRE(engine->getGrainRngState()
                == VoragoGhostFix::kBaseCommitShortPreRollGrainRngState);
    }
}

// =============================================================================
// SC-004 HELPERS - the extreme-ratio truncation corner
// =============================================================================
namespace {

/// SC-004's rate. Named once so the corner's capacity
/// (`nextPowerOf2(kMinCaptureSeconds * kTruncationSampleRate)`) and its
/// scheduler interonset are computed from the same figure the engine was
/// prepared at.
constexpr double kTruncationSampleRate = 48000.0;

/// @brief `nextPowerOf2`, as `RollingCaptureBuffer::prepare()` applies it
///        (`rolling_capture_buffer.h:75-93`), so the expected capacity below is
///        derived rather than transcribed.
[[nodiscard]] constexpr std::size_t nextPowerOfTwo(std::size_t value) noexcept {
    std::size_t result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

/// Everything one SC-004 arm measures, gathered in a single render.
struct TruncationArm {
    /// One entry per OBSERVED birth: `getLastBornGrainLifetimeSamples()` read at
    /// the block boundary on which `getTotalGrainsBorn()` advanced.
    std::vector<std::uint64_t> lifetimes;
    std::uint64_t bornDelta = 0;
    /// The largest per-block birth delta seen. A value above 1 means one block
    /// contained two births and the accessor could not have observed both - a
    /// FIXTURE DEFECT, which the case fails on rather than silently skipping.
    std::uint64_t maxBlockBornDelta = 0;
    /// Births inside the measured span that the TRIGGER did not claim, i.e.
    /// density-scheduler births. The span is phased to exclude the scheduler
    /// (see `runTruncationArm`), so this is the direct witness of that phasing
    /// and must be 0; it is what keeps `maxBlockBornDelta <= 1` a statement
    /// about the component rather than a coin toss on the scheduler's phase.
    std::uint64_t schedulerBirths = 0;
    std::size_t nonFiniteSamples = 0;
    std::size_t overLevelSamples = 0;
    float worstAbs = 0.0f;
    std::size_t capacity = 0;
    std::size_t spanSamples = 0;
};

/// @brief Build the SC-004 corner engine.
///
/// `captureSeconds = kMinCaptureSeconds` (1.0, `atmosphere_engine.h:317`) and
/// `grainSeconds = kMaxGrainSeconds` (30.0, `:302`) are the corner in which
/// truncation bites hardest: the requested grain is 1 440 000 samples against a
/// 65 536-sample ring. `pitchSemitones = kMaxPitchSemitones` (+24, `:306`) with
/// `driftRangeSemitones = kMaxDriftRangeSemitones` (12, `:308`) drives the birth
/// pitch envelope into the `kMaxAbsGrainSemitones = 36` clamp (`:313`), i.e. the
/// largest ratio the component admits.
///
/// `decorrelation = 0` is ADDED BY THE PLAN (not by the spec's own wording) for
/// one reason: `decorrAge` is a per-grain DRAW with no getter, so with
/// decorrelation non-zero `slack` is not recomputable from public accessors and
/// the lifetime assertions below would have to be transcribed literals.
///
/// The ring is pre-rolled FULL by the caller, so FR-050 is vacuous here
/// (`t* = 0`) and the shipped truncation alone sets the lifetime.
[[nodiscard]] std::unique_ptr<AtmosphereEngine> makeTruncationEngine(float pitchSpread) {
    auto engine = std::make_unique<AtmosphereEngine>();
    engine->prepare(kTruncationSampleRate,
                    AtmosphereEngine::PrepareConfig{
                        .captureSeconds = AtmosphereEngine::kMinCaptureSeconds,
                        .blurEnabled = true,
                        .freezeEnabled = false,
                        .blurFftSize = std::size_t{1024},
                        .freezeFftSize = std::size_t{2048},
                        .maxBlockSamples = std::size_t{2048}});
    engine->setSeed(1u);
    engine->setGrainSeconds(AtmosphereEngine::kMaxGrainSeconds);
    engine->setDensity(AtmosphereEngine::kMinDensity);
    engine->setPitchSemitones(AtmosphereEngine::kMaxPitchSemitones);
    engine->setPitchSpread(pitchSpread);
    engine->setDriftRangeSemitones(AtmosphereEngine::kMaxDriftRangeSemitones);
    engine->setDecorrelation(0.0f);
    engine->setBlur(0.85f);
    // Level 1.0 rather than the ghost's 0.60: the |sample| <= kMaxLevel guard
    // below is only worth asserting at the loudest legal setting the component's
    // own default provides.
    engine->setLevel(1.0f);
    engine->setGrainReverseProbability(1.0f);
    return engine;
}

/// @brief Run one SC-004 arm: full-ring pre-roll, then a 64-sample-block render
///        with an observation after every block.
[[nodiscard]] TruncationArm runTruncationArm(float pitchSpread) {
    auto engine = makeTruncationEngine(pitchSpread);

    TruncationArm arm{};
    arm.capacity = engine->getCaptureCapacitySamples();

    const std::size_t preRolled =
        VoragoGhostFix::preRollFullRing(*engine, 1u, kObservationBlock);
    REQUIRE(preRolled == arm.capacity);

    // SPAN SIZING, computed rather than guessed - and, since T017 landed the
    // second birth site, sized to EXCLUDE the density scheduler rather than to
    // contain it.
    //
    // T011 sized the span to guarantee at least one scheduler tick, because the
    // scheduler was then the ONLY birth source and `bornDelta >= 1` had to be an
    // assertion rather than a hope. T016 gave the measured span one
    // `triggerGrain()` per block, which supplies the births - and turned the old
    // sizing into a defect: the one block carrying the scheduler tick then holds
    // TWO births, of which `getLastBornGrainLifetimeSamples()` can report only
    // the second, so `maxBlockBornDelta <= 1` fails as a FIXTURE defect. (It did:
    // span 600 064, bornDelta 5 275, observations 5 274, maxBlockBornDelta 2.)
    // This is the fix the file already named - shorten the span so the scheduler
    // never ticks inside it.
    //
    // THE SCHEDULER IS PHASED OUT, NOT HOPED AWAY. `samplesUntilNextGrain_` is
    // private with no accessor (grain_scheduler.h:105), so the arm does not
    // predict the tick; it RENDERS UNTIL IT TICKS - every tick bumps exactly one
    // of born / ring-cold / pool-full, those being tryBirthGrain()'s only exits
    // (atmosphere_engine.h:1740, 1824, 1843, 1885, 1904 against the birth at the
    // end) - and opens the measured span there. The NEXT tick cannot arrive
    // before `interonset * (1 - 0.5 * jitter)` further samples, because the
    // countdown is redrawn as `interonset * (1 + off * 0.5 * jitter)` with
    // `off = rng.nextFloat()` in [-1, 1] (grain_scheduler.h:82-84). A measured
    // span shorter than that, less the 64-sample granularity with which the
    // settle observed the tick, is therefore tick-free BY CONSTRUCTION - and the
    // `schedulerBirths == 0` check in `checkCommon` witnesses it per run.
    const double interonset =
        kTruncationSampleRate / static_cast<double>(engine->getDensity());
    const double jitter = static_cast<double>(engine->getJitter());
    const double longestInteronset = interonset * (1.0 + 0.5 * jitter);
    const double shortestInteronset = interonset * (1.0 - 0.5 * jitter);

    // 96 blocks. The pool holds kMaxGrains = 64 slots and nothing born here
    // retires inside the span (this corner's lifetime is thousands of samples),
    // so 96 trigger attempts observe every birth the configuration can admit and
    // then skip pool-full, which is the regime the assertions below are about.
    // Nothing is transcribed from this figure: every threshold is recomputed.
    constexpr std::size_t kSpanBlocks = 96;
    arm.spanSamples = kSpanBlocks * kObservationBlock;
    INFO("interonset=" << interonset << " shortest=" << shortestInteronset
                       << " span=" << arm.spanSamples);
    REQUIRE(static_cast<double>(arm.spanSamples + kObservationBlock) < shortestInteronset);

    // --- THE SETTLE: render, WITHOUT triggers, up to and including the block
    //     in which the scheduler ticks. Bounded by the longest interonset plus
    //     one block, so a scheduler that never ticks fails loudly here instead
    //     of running for ever.
    const std::size_t settleCap =
        static_cast<std::size_t>(std::ceil(longestInteronset)) + kObservationBlock;
    ContinuousRender render(settleCap + arm.spanSamples, 1u, kObservationBlock);

    const auto attemptCount = [&engine]() -> std::uint64_t {
        return engine->getTotalGrainsBorn() + engine->getSkippedTriggerCountRingCold()
               + engine->getSkippedTriggerCountPoolFull();
    };
    const std::uint64_t attempts0 = attemptCount();
    std::size_t settleEnd = 0;
    for (std::size_t end = kObservationBlock; end <= settleCap; end += kObservationBlock) {
        render.renderTo(*engine, end, noBlockObserver);
        if (attemptCount() > attempts0) {
            settleEnd = render.rendered();
            break;
        }
    }
    // A scheduler that never attempted inside its own longest interonset would
    // leave the span unphased - a fixture defect, not a component defect.
    REQUIRE(settleEnd > std::size_t{0});

    const std::uint64_t born0 = engine->getTotalGrainsBorn();
    const std::uint64_t triggeredBorn0 = engine->getTotalTriggeredGrainsBorn();
    std::uint64_t prevBorn = born0;

    render.renderTo(*engine, settleEnd + arm.spanSamples, [&](std::size_t) {
        // ---------------------------------------------------------------------
        // UNCOMMENTED AT T016 (tasks.md T016's closing clause). At most ONE
        // trigger per block, which is the rule that makes "every admitted grain
        // is observed" true (a second birth in the same block would be invisible
        // to getLastBornGrainLifetimeSamples, which reports the MOST RECENT
        // birth only).
        //
        // THE HAZARD T016 RECORDED HERE IS NOW CLOSED, not merely documented:
        // the span is phased to sit inside one scheduler interonset (see the
        // sizing above), so this trigger is the ONLY birth site inside it and
        // one birth per block is a property, not a hope.
        // ---------------------------------------------------------------------
        engine->triggerGrain();

        const std::uint64_t born = engine->getTotalGrainsBorn();
        const std::uint64_t delta = born - prevBorn;
        prevBorn = born;
        if (delta > arm.maxBlockBornDelta) {
            arm.maxBlockBornDelta = delta;
        }
        if (delta > 0) {
            arm.lifetimes.push_back(engine->getLastBornGrainLifetimeSamples());
        }

        // Non-finiteness and level, scanned per sample but ACCUMULATED - a
        // Catch2 assertion per sample would cost more than the render. The
        // predicate is VoragoGhostFix::isNonFiniteBits: a BIT-PATTERN test, never
        // std::isnan/std::isinf, which fold to constants under /fp:fast and
        // -ffast-math. This TU is deliberately NOT on the -fno-fast-math list,
        // which is exactly why the detection has to be done on the bits.
        const std::size_t n = render.lastBlockSamples();
        const float* outL = render.lastOutLeft();
        const float* outR = render.lastOutRight();
        for (std::size_t i = 0; i < n; ++i) {
            const float l = outL[i];
            const float r = outR[i];
            if (VoragoGhostFix::isNonFiniteBits(l) || VoragoGhostFix::isNonFiniteBits(r)) {
                ++arm.nonFiniteSamples;
                continue;  // |NaN| comparisons below are meaningless
            }
            const float absL = std::abs(l);
            const float absR = std::abs(r);
            const float worst = std::max(absL, absR);
            if (worst > arm.worstAbs) {
                arm.worstAbs = worst;
            }
            if (worst > AtmosphereEngine::kMaxLevel) {
                ++arm.overLevelSamples;
            }
        }
    });

    arm.bornDelta = engine->getTotalGrainsBorn() - born0;
    arm.schedulerBirths =
        arm.bornDelta - (engine->getTotalTriggeredGrainsBorn() - triggeredBorn0);
    return arm;
}

}  // namespace

// =============================================================================
// SC-004 - Truncation is correct at the extremes
// =============================================================================
//
// NEVER TAGGED [long], AT ANY MEASURED COST. FR-048's closing sentence restates
// the standing rule in CLAUDE.md's Build Commands note: a NaN/Inf-guard case, a
// bounded-grid case or a state-format case stays in the per-push lane whatever
// it costs, because those are the cross-platform sentinels. This case is all
// three, and it is the phase's ONLY reverse-path NaN/Inf and kMaxLevel check at
// the extreme ratio corner. If it measures over ~15 s it SPLITS the way FR-049
// splits SC-003 - this case keeps the sentinel assertions on a handful of births
// and an AtmosphereGhost_ReverseTruncation_Sweep [long] sibling in
// atmosphere_ghost_longrun_test.cpp takes the exhaustive sweep. It is not tagged.
//
// THE ARITHMETIC, recomputed in the test from public accessors and never
// transcribed. At `captureSeconds = kMinCaptureSeconds` the capacity is
// nextPowerOf2(48 000) = 65 536 (1.365 s), and with `decorrelation = 0`
//   slack = C - 2 - 2*kMinAgeSamples - 2 = 65 404
// exactly (atmosphere_engine.h's headroom at :1750 and slack at :1765).
//   exact arm : pitchSpread = 0 pins staticSemis = +24, semisHi = clamp(36) = 36,
//               so rMax = 8 EXACTLY and the reverse window is w = 1 + rMax = 9.
//               lifetime == floor(65 404 / 9) = 7 267 samples (0.151 s).
//   corner arm: pitchSpread = 1 makes rMax a draw in [4, 8], so w is in [5, 9]
//               and lifetime <= floor(65 404 / 5) = 13 080 holds for EVERY
//               admissible draw.
// With the FORWARD window still in place (pre-T013) the exact arm computes
// w = wUp = rMax - 1 = 7 and lifetime = floor(65 404 / 7) = 9 343, so the exact
// arm is RED until T013 lands - which is the point of writing it now.
TEST_CASE("AtmosphereGhost_ReverseTruncation", "[atmosphere][ghost]") {
    using Krate::DSP::semitonesToRatio;

    constexpr float kMaxAbs = AtmosphereEngine::kMaxAbsGrainSemitones;
    const double guard = static_cast<double>(AtmosphereEngine::kMinAgeSamples);

    /// Assertions every arm carries, whatever its lifetime bound.
    const auto checkCommon = [](const TruncationArm& arm, const char* label) {
        INFO("arm: " << label << "; capacity=" << arm.capacity << " span=" << arm.spanSamples
                     << " bornDelta=" << arm.bornDelta
                     << " observations=" << arm.lifetimes.size()
                     << " maxBlockBornDelta=" << arm.maxBlockBornDelta
                     << " schedulerBirths=" << arm.schedulerBirths
                     << " worstAbs=" << arm.worstAbs);

        // The corner's capacity, re-checked against the ring's OWN rounding rule
        // rather than a literal: every lifetime threshold below is derived from
        // `arm.capacity`, so a geometry change must fail here and not silently
        // move the bound it is measured against. At kMinCaptureSeconds = 1 and
        // 48 kHz this is nextPowerOf2(48 000) = 65 536 samples (1.365 s).
        REQUIRE(arm.capacity
                == nextPowerOfTwo(static_cast<std::size_t>(
                       static_cast<double>(AtmosphereEngine::kMinCaptureSeconds)
                       * kTruncationSampleRate)));

        // OBSERVATION GRANULARITY - the assertions that close "every admitted
        // grain". A block with two births could not have been observed twice, so
        // it fails the case as a fixture defect. The span carries ONE birth site
        // (the per-block trigger), the density scheduler having been phased out
        // of it; `schedulerBirths` is the direct witness that the phasing held
        // on this run, and it fails FIRST because it names the cause.
        REQUIRE(arm.schedulerBirths == std::uint64_t{0});
        REQUIRE(arm.maxBlockBornDelta <= std::uint64_t{1});
        REQUIRE(arm.lifetimes.size() == static_cast<std::size_t>(arm.bornDelta));
        // Not vacuous: an arm that never births proves nothing about truncation.
        REQUIRE(arm.bornDelta >= std::uint64_t{1});

        // The cross-platform sentinels.
        REQUIRE(arm.nonFiniteSamples == std::size_t{0});
        REQUIRE(arm.overLevelSamples == std::size_t{0});

        // No birth with lifetime < 2 - the rejection at :1770-1773. A grain
        // shorter than two samples has no defined envelope phase (the FR-026
        // denominator is L' - 1).
        for (const std::uint64_t lifetime : arm.lifetimes) {
            REQUIRE(lifetime >= std::uint64_t{2});
        }
    };

    SECTION("exact arm - pitchSpread 0 pins rMax = 8, so the lifetime is an equality") {
        const TruncationArm arm = runTruncationArm(0.0f);
        checkCommon(arm, "exact (pitchSpread = 0)");

        const double slack = static_cast<double>(arm.capacity) - 2.0 - guard - guard - 2.0;

        // semisHi at birth: staticSemis = clamp(+24 + 0, +/-36) = 24, then
        // clamp(24 + driftRange, +/-36) = clamp(36) = 36. Both clamps are the
        // component's own (:1700-1710), reproduced with the same std::clamp.
        const float staticSemis =
            std::clamp(AtmosphereEngine::kMaxPitchSemitones, -kMaxAbs, kMaxAbs);
        const float semisHi = std::clamp(staticSemis + AtmosphereEngine::kMaxDriftRangeSemitones,
                                         -kMaxAbs, kMaxAbs);
        const double rMax = static_cast<double>(semitonesToRatio(semisHi));
        REQUIRE(rMax == 8.0);  // exact: std::pow(2.0f, 3.0f) is exactly 8.0f

        // FR-014's reverse window: wUp is identically 0 and wDown = 1 + rMax, so
        // w = 9. The forward window at this corner is w = rMax - 1 = 7, which is
        // why this equality is the assertion T013 has to turn green.
        const double w = 1.0 + rMax;
        const auto expectedLifetime = static_cast<std::uint64_t>(std::floor(slack / w));
        INFO("slack=" << slack << " w=" << w << " expectedLifetime=" << expectedLifetime);
        for (const std::uint64_t lifetime : arm.lifetimes) {
            REQUIRE(lifetime == expectedLifetime);
        }
    }

    SECTION("corner arm - pitchSpread 1 makes rMax a draw, so the lifetime is bounded") {
        const TruncationArm arm = runTruncationArm(1.0f);
        checkCommon(arm, "corner (pitchSpread = 1)");

        const double slack = static_cast<double>(arm.capacity) - 2.0 - guard - guard - 2.0;

        // The SMALLEST rMax any draw can produce, hence the SMALLEST w, hence the
        // LARGEST admissible lifetime. uPitch is in [-1, 1] and the spread term is
        // `uPitch * pitchSpread * (kPitchSpreadCents / 100)` (:1700-1702), so at
        // pitchSpread = 1 the static pitch bottoms out at +24 - 12 = +12 and
        // semisHi at +24, i.e. rMax = 4 and w = 5.
        const float staticSemisLow =
            std::clamp(AtmosphereEngine::kMaxPitchSemitones
                           - (AtmosphereEngine::kPitchSpreadCents / 100.0f),
                       -kMaxAbs, kMaxAbs);
        const float semisHiLow = std::clamp(
            staticSemisLow + AtmosphereEngine::kMaxDriftRangeSemitones, -kMaxAbs, kMaxAbs);
        const double wMin = 1.0 + static_cast<double>(semitonesToRatio(semisHiLow));
        REQUIRE(wMin == 5.0);

        const auto lifetimeCeiling = static_cast<std::uint64_t>(std::floor(slack / wMin));
        INFO("slack=" << slack << " wMin=" << wMin << " lifetimeCeiling=" << lifetimeCeiling);
        for (const std::uint64_t lifetime : arm.lifetimes) {
            REQUIRE(lifetime <= lifetimeCeiling);
        }
    }
}

// =============================================================================
// FR-049's PER-PUSH TWIN of SC-003 - the reverse birth window is non-empty
// =============================================================================
//
// SC-003 itself (atmosphere_ghost_longrun_test.cpp, T012) pre-rolls the same
// full ring and then renders TEN MINUTES; it is tagged [long] and runs nightly.
// This twin keeps clause (a) - and only clause (a) - over a ~30 s measured span
// so the assertion with teeth runs on EVERY push: if the direction-dependent
// window of FR-014 ever leaves the reverse birth window empty at the Vorago
// operating point, the ring-cold counter starts climbing and this fails within
// seconds instead of overnight.
//
// THE PRE-ROLL IS A SAMPLE COUNT, NEVER A DURATION (plan P-2). The ring is
// nextPowerOf2(20 * 48 000) = 1 048 576 samples = 21.845 s, NOT the configured
// 20 s, and a seconds-stated pre-roll leaves it FILLING - where FR-050 rejects
// births by design and this delta assertion would be red on correct code.
// VoragoGhostFix::preRollFullRing is the one definition of that pre-roll.
//
// THE BOUND IS RECOMPUTED, NOT THE LITERAL 9. It is the number of scheduler
// ticks the pre-roll can contain: the free tick at sample 0 (reset() sets
// samplesUntilNextGrain_ = 0.0f, grain_scheduler.h:40, and process() decrements
// BEFORE testing <= 0.0f, :73-76) plus the ticks the SHORTEST interval jitter
// permits, (1 / density) * (1 - 0.5 * jitter) = 2.5 s at density 0.30,
// jitter 0.5 (grain_scheduler.h:78-88, interonset at :102). At this
// configuration that is 1 + floor(21.845 / 2.5) = 9.
TEST_CASE("AtmosphereGhost_ReverseLiveness_Short", "[atmosphere][ghost]") {
    VoragoGhostFix::GhostConfig config{};  // SC-003's Vorago ghost point, seed 1

    auto engine = std::make_unique<AtmosphereEngine>();
    VoragoGhostFix::applyVoragoGhost(*engine, config);
    engine->setGrainReverseProbability(1.0f);
    REQUIRE(engine->getGrainReverseProbability() == 1.0f);

    const std::size_t capacity = engine->getCaptureCapacitySamples();
    REQUIRE(capacity == VoragoGhostFix::kBaseCommitCaptureCapacity);

    const std::size_t preRolled =
        VoragoGhostFix::preRollFullRing(*engine, config.seed, VoragoGhostFix::kReferenceRenderBlock);
    REQUIRE(preRolled == capacity);

    // --- clause (a), first half: the cold-start skips are bounded.
    const double preRollSeconds = static_cast<double>(capacity) / config.sampleRate;
    const double shortestInteronsetSeconds = (1.0 / static_cast<double>(engine->getDensity()))
                                             * (1.0 - 0.5 * static_cast<double>(engine->getJitter()));
    const auto maxColdStartSkips = static_cast<std::uint64_t>(
        1.0 + std::floor(preRollSeconds / shortestInteronsetSeconds));
    const std::uint64_t coldStartSkips = engine->getSkippedTriggerCountRingCold();

    INFO("preRollSeconds=" << preRollSeconds
                           << " shortestInteronsetSeconds=" << shortestInteronsetSeconds
                           << " maxColdStartSkips=" << maxColdStartSkips
                           << " coldStartSkips=" << coldStartSkips);
    REQUIRE(coldStartSkips <= maxColdStartSkips);

    // --- clause (a), second half: over the measured span the counter DOES NOT
    //     ADVANCE AT ALL. That is the assertion with teeth - on a saturated ring
    //     FR-050 is vacuous, so every ring-cold rejection here would be a birth
    //     window that the direction-dependent w had emptied.
    const auto kMeasuredSpanSamples = static_cast<std::size_t>(30.0 * config.sampleRate);
    const std::uint64_t born0 = engine->getTotalGrainsBorn();

    renderSpan(*engine, kMeasuredSpanSamples, config.seed, VoragoGhostFix::kReferenceRenderBlock);

    REQUIRE(engine->getSkippedTriggerCountRingCold() == coldStartSkips);
    // A span in which nothing was even attempted would make the equality above
    // vacuous, so the births are asserted rather than assumed.
    REQUIRE(engine->getTotalGrainsBorn() > born0);
}

// =============================================================================
// SC-002 HELPERS - the ONE-GRAIN protocol
// =============================================================================
//
// WHY THE PROTOCOL IS BUILT AROUND A SINGLE, ISOLATED GRAIN. `processStereoBlock`
// emits only the SUMMED grain bus after FR-028's population gain
// (atmosphere_engine.h:2275-2296), so with more than one grain live no
// individual grain's waveform, envelope endpoint or spectral trajectory is
// observable at the output at all. Every clause below reads the rendered span as
// if it were one grain, and the case ASSERTS that it is rather than assuming it.
//
// -----------------------------------------------------------------------------
// T017: THE SPEC'S PROTOCOL NAMES triggerGrain(); IT DOES NOT EXIST YET.
//
// tasks.md T008 item 8 holds triggerGrain() back to T017 so the header never
// carries an entry point nothing drains, and T014 runs before it. The isolation
// this case needs is therefore obtained from the DENSITY SCHEDULER instead, and
// it is obtained EXACTLY, not approximately:
//
//   * `jitter = 0` takes GrainScheduler::process()'s regular-interval branch
//     (grain_scheduler.h:85-88), so the countdown is reloaded with
//     interonsetSamples_ = sampleRate / density verbatim (:100-103) and never
//     with a draw;
//   * reset() sets samplesUntilNextGrain_ = 0.0f (:40) and process() decrements
//     BEFORE testing <= 0.0f (:73-76), so the scheduler fires on absolute sample
//     0 and then on every multiple of the interonset;
//   * at `density = kMinDensity = 0.1` and 48 kHz that interonset is 480 000
//     samples, an integer and a multiple of kControlChunkSamples = 64. Both
//     facts are RECOMPUTED AND ASSERTED below (`requireOneIsolatedGrain`), never
//     transcribed: if either stops holding the case fails loudly instead of
//     measuring the wrong sample.
//   * the tick at sample 0 is REJECTED - the ring holds one sample against a
//     `needed` of kMinAgeSamples + kMinAgeSamples = 128 (:1816-1820) - so the
//     tick at T = 480 000 births the FIRST and, within this render, the ONLY
//     grain. `born == 1` and `maxActiveOverall == 1` are asserted.
//
// WHEN T017 LANDS, DO NOT SIMPLY ADD A triggerGrain() CALL HERE. The fixture is
// already single-grain by construction; a fired trigger would add a SECOND birth
// and break `born == 1`. Either leave this case as it is, or move the birth onto
// the trigger AND move the scheduler tick out of the render (a larger
// interonset), so that exactly one grain is still born.
// -----------------------------------------------------------------------------
namespace {

/// SC-002's geometry. `captureSeconds = 4` gives C = nextPowerOf2(192 000) =
/// 262 144, which is what makes clause (c)'s "no truncation" claim true:
/// w * requested = 1.749154 * 24 000 = 41 980 against slack ~= 262 012.
constexpr double kReverseSampleRate = 48000.0;
constexpr float kReverseCaptureSeconds = 4.0f;
constexpr float kReverseGrainSeconds = 0.5f;

/// SC-002's excitation: a LINEAR chirp, 200 Hz -> 4 kHz over exactly 1.0 s, i.e.
/// a sweep rate of 3 800 Hz/s. Clause (b) reads that rate straight back off the
/// rendered grain as a spectral-centroid slope, with the sign the direction of
/// the read walk imposes.
constexpr double kChirpF0 = 200.0;
constexpr double kChirpF1 = 4000.0;

/// Clause (b)'s analysis grid. 1 024 samples (21.3 ms) is the shortest frame
/// whose FFT bin width (46.9 Hz) is small against the sweep, and over one frame
/// the chirp moves only 81 Hz, so the per-frame centroid is a clean read of the
/// instantaneous frequency rather than a smear.
constexpr std::size_t kCentroidFrameSamples = 1024u;
constexpr std::size_t kCentroidHopSamples = 512u;

/// Clause (a)'s lag search. The birth read age is the kMinAgeSamples = 64 clamp
/// and the ratio is exactly 1, so the correct alignment is lag 0; the search
/// exists only so a one-sample bookkeeping slip reports as a shifted peak rather
/// than as a failed correlation. At 64 lags out of 24 000 the overlap loss is
/// 0.27 %, far inside the 0.90 bar.
constexpr std::ptrdiff_t kMaxCorrelationLag = 64;

/// Clause (b)'s floor. The expected magnitude is the chirp's own sweep rate,
/// 3 800 Hz/s at ratio 1 - a 7.6x margin over this bar, which is therefore
/// visibly a FLOOR and not a fitted threshold.
constexpr double kMinCentroidSlopeMagnitude = 500.0;

/// @brief Peak normalised cross-correlation of `signal` against `reference`.
///
/// Normalised by the FULL-span energies of both sequences (not by the overlap),
/// so a lag that loses overlap can only LOWER the score - the peak can never be
/// inflated by shortening the window. The correlation is SIGNED, not absolute: a
/// polarity inversion is a real defect here, because the grain's only gains are
/// the equal-power pan (positive, atmosphere_engine.h:1848-1850), the FR-028
/// population gain and the level trim.
[[nodiscard]] double normalisedCrossCorrelationPeak(std::span<const float> signal,
                                                    std::span<const float> reference,
                                                    std::ptrdiff_t maxLag) {
    const std::size_t n = std::min(signal.size(), reference.size());
    if (n == 0u) {
        return 0.0;
    }

    double energySignal = 0.0;
    double energyReference = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = static_cast<double>(signal[i]);
        const double r = static_cast<double>(reference[i]);
        energySignal += s * s;
        energyReference += r * r;
    }
    const double denom = std::sqrt(energySignal * energyReference);
    if (!(denom > 0.0)) {
        return 0.0;
    }

    double best = -1.0;
    const auto count = static_cast<std::ptrdiff_t>(n);
    for (std::ptrdiff_t lag = -maxLag; lag <= maxLag; ++lag) {
        const std::ptrdiff_t first = std::max<std::ptrdiff_t>(0, -lag);
        const std::ptrdiff_t last = std::min<std::ptrdiff_t>(count, count - lag);
        double acc = 0.0;
        for (std::ptrdiff_t i = first; i < last; ++i) {
            acc += static_cast<double>(signal[static_cast<std::size_t>(i)]) *
                   static_cast<double>(reference[static_cast<std::size_t>(i + lag)]);
        }
        best = std::max(best, acc / denom);
    }
    return best;
}

/// @brief Least-squares slope, in Hz per second, of the spectral-centroid
///        trajectory of `x`.
///
/// The per-frame centroid is `TestUtils::spectralCentroidHz`
/// (tests/test_helpers/reverb_metrics.h:291, re-exported at
/// vorago_fixtures.h:86) - REUSED, NOT RE-IMPLEMENTED. It is preferred here over
/// `TestUtils::Vorago::frameMagnitudes` (vorago_fixtures.h:137) for one reason:
/// frameMagnitudes hands back magnitudes without a frequency axis, so reducing
/// them to a centroid would be a SECOND implementation of the very function
/// reverb_metrics.h already provides. The framing this helper does - fixed
/// frame, fixed hop, whole frames only - is the only thing it adds.
///
/// Frames whose centroid comes back as 0 (an all-zero spectrum, the helper's own
/// empty answer) are skipped rather than regressed against, which would drag the
/// slope toward the frame's time value for no reason.
[[nodiscard]] double centroidSlopeHzPerSecond(std::span<const float> x, double sampleRate,
                                              std::size_t frameSamples,
                                              std::size_t hopSamples) {
    if ((frameSamples == 0u) || (hopSamples == 0u) || (x.size() < frameSamples) ||
        !(sampleRate > 0.0)) {
        return 0.0;
    }

    std::vector<double> times;
    std::vector<double> centroids;
    for (std::size_t start = 0; (start + frameSamples) <= x.size(); start += hopSamples) {
        const double centroid =
            Krate::DSP::TestUtils::spectralCentroidHz(x.subspan(start, frameSamples), sampleRate);
        if (!(centroid > 0.0)) {
            continue;
        }
        times.push_back((static_cast<double>(start) + (0.5 * static_cast<double>(frameSamples))) /
                        sampleRate);
        centroids.push_back(centroid);
    }
    if (times.size() < 2u) {
        return 0.0;
    }

    const auto count = static_cast<double>(times.size());
    double timeMean = 0.0;
    double centroidMean = 0.0;
    for (std::size_t i = 0; i < times.size(); ++i) {
        timeMean += times[i];
        centroidMean += centroids[i];
    }
    timeMean /= count;
    centroidMean /= count;

    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const double dt = times[i] - timeMean;
        numerator += dt * (centroids[i] - centroidMean);
        denominator += dt * dt;
    }
    if (!(denominator > 0.0)) {
        return 0.0;
    }
    return numerator / denominator;
}

/// @brief Everything one SC-002 render produces, gathered in a single pass.
struct IsolatedGrain {
    std::vector<float> source;   ///< the excitation, as the ring saw it
    std::vector<float> outLeft;  ///< the WHOLE render, not just the grain span
    std::vector<float> outRight;

    std::size_t capacity = 0;
    float interonsetSamples = 0.0f;
    std::size_t birthSample = 0;       ///< T
    std::size_t readStart = 0;         ///< T - kMinAgeSamples, the FIRST source index read
    std::size_t expectedLifetime = 0;  ///< L' before any truncation
    std::size_t lifetime = 0;          ///< getLastBornGrainLifetimeSamples()
    std::size_t firstBirthBoundary = 0;
    std::size_t maxActiveBeforeBirth = 0;
    std::size_t maxActiveOverall = 0;

    std::uint64_t born = 0;
    std::uint64_t reverseBorn = 0;
    std::uint64_t ringCold = 0;
    std::uint64_t poolFull = 0;

    bool reversedAtBirth = false;
    bool flipped = false;
    float probabilityAtEnd = 0.0f;
    float birthAge = 0.0f;
    float minAge = 0.0f;
    float maxAge = 0.0f;
    double ratioAtBirth = 0.0;
};

/// @brief SC-002's engine.
///
/// Every setting is the spec's, and each one removes a DRAW from the birth so
/// that the grain's geometry is known rather than sampled:
/// `pitchSpread = driftRange = driftDepth = 0` pins `ratio` to the configured
/// pitch for the whole life; `positionSeconds = positionSpread = 0` drives the
/// raw birth age to 0, which the window's `ageLo` then clamps UP to
/// `ceil(wUp * L') + kMinAgeSamples` - and `wUp` is identically 0 for a reverse
/// grain (FR-014, atmosphere_engine.h:1729), so the birth age is the 64-sample
/// clamp EXACTLY, for the forward comparison arm as well (there `wUp` is
/// max(ratioMax - 1, 0) = 0 at ratio 1). `decorrelation = 0` makes the two
/// channels read the same point, `panSpread = 0` puts both at the equal-power
/// centre, and blur/freeze are OFF so getLatencySamples() is 0 (:1190-1192) and
/// output sample T IS the grain's first sample.
[[nodiscard]] std::unique_ptr<AtmosphereEngine> makeIsolatedGrainEngine(float pitchSemitones,
                                                                        float probability) {
    auto engine = std::make_unique<AtmosphereEngine>();
    engine->prepare(kReverseSampleRate,
                    AtmosphereEngine::PrepareConfig{
                        .captureSeconds = kReverseCaptureSeconds,
                        .blurEnabled = false,
                        .freezeEnabled = false,
                        .blurFftSize = std::size_t{1024},
                        .freezeFftSize = std::size_t{2048},
                        .maxBlockSamples = std::size_t{2048}});
    engine->setSeed(1u);
    engine->setGrainReverseProbability(probability);
    engine->setPitchSemitones(pitchSemitones);
    engine->setPitchSpread(0.0f);
    engine->setDriftRangeSemitones(0.0f);
    engine->setDriftDepth(0.0f);
    engine->setPositionSeconds(0.0f);
    engine->setPositionSpread(0.0f);
    engine->setDecorrelation(0.0f);
    engine->setPanSpread(0.0f);
    engine->setLevel(1.0f);
    engine->setJitter(0.0f);
    engine->setDensity(AtmosphereEngine::kMinDensity);
    engine->setGrainSeconds(kReverseGrainSeconds);
    return engine;
}

/// @brief Render the one-grain protocol and report everything it produced.
///
/// THE CHIRP'S PLACEMENT, which is what makes clause (a)'s two references
/// meaningful. The grain's first read is source index
/// `readStart = T - kMinAgeSamples`; a REVERSE grain then walks down to
/// `readStart - (L' - 1)` and a FORWARD one up to `readStart + (L' - 1)`. The
/// chirp is therefore written over exactly `[readStart - (L' - 1), readStart +
/// L']`, i.e. 2*L' samples = 1.0 s at this configuration, so its MIDPOINT sits
/// on `readStart` and each direction of travel covers one half-sweep and no
/// silence. (The spec words this as "midpoint at the trigger sample T"; T and
/// readStart differ by the 64-sample birth-age clamp, and aligning on the sample
/// the grain actually reads is what removes the 64-sample sliver of silence the
/// T-aligned placement would leave at the far end of the reverse span.)
///
/// @param flipAtMidlife clause (d): write probability 0 at the first block
///        boundary at or past the grain's half-life.
[[nodiscard]] IsolatedGrain renderIsolatedGrain(float pitchSemitones, float probability,
                                                bool flipAtMidlife) {
    auto engine = makeIsolatedGrainEngine(pitchSemitones, probability);

    IsolatedGrain grain{};
    grain.capacity = engine->getCaptureCapacitySamples();

    // T, reproduced from GrainScheduler's own arithmetic (:100-103) rather than
    // transcribed. getDensity() reports the engine's clamped value, which is the
    // value runControlStep() pushes into the scheduler (:2321).
    grain.interonsetSamples = static_cast<float>(kReverseSampleRate) / engine->getDensity();
    grain.birthSample = static_cast<std::size_t>(grain.interonsetSamples);
    grain.expectedLifetime = static_cast<std::size_t>(
        std::round(static_cast<double>(kReverseGrainSeconds) * kReverseSampleRate));
    grain.readStart = grain.birthSample - AtmosphereEngine::kMinAgeSamples;

    const std::size_t chirpStart = grain.readStart - (grain.expectedLifetime - 1u);
    const std::size_t chirpSamples = 2u * grain.expectedLifetime;
    const std::size_t totalSamples = grain.birthSample + grain.expectedLifetime;

    grain.source.assign(totalSamples, 0.0f);
    VoragoGhostFix::exciteChirp(std::span<float>(grain.source).subspan(chirpStart, chirpSamples),
                                kReverseSampleRate, kChirpF0, kChirpF1,
                                static_cast<double>(chirpSamples) / kReverseSampleRate);

    grain.outLeft.assign(totalSamples, 0.0f);
    grain.outRight.assign(totalSamples, 0.0f);

    const std::size_t midlifeBoundary = grain.birthSample + (grain.expectedLifetime / 2u);
    std::size_t rendered = 0;
    while (rendered < totalSamples) {
        const std::size_t n = std::min(kObservationBlock, totalSamples - rendered);
        // The SAME buffer feeds both inputs: the excitation is mono, and with
        // panSpread = 0 and decorrelation = 0 the two output channels agree.
        engine->processStereoBlock(grain.source.data() + rendered, grain.source.data() + rendered,
                                   grain.outLeft.data() + rendered,
                                   grain.outRight.data() + rendered, n);
        rendered += n;

        const std::size_t active = engine->getActiveGrainCount();
        if (rendered <= grain.birthSample) {
            grain.maxActiveBeforeBirth = std::max(grain.maxActiveBeforeBirth, active);
        }
        grain.maxActiveOverall = std::max(grain.maxActiveOverall, active);

        if ((grain.firstBirthBoundary == 0u) && (engine->getTotalGrainsBorn() > 0u)) {
            grain.firstBirthBoundary = rendered;
            grain.reversedAtBirth = engine->getLastBornGrainReversed();
            grain.ratioAtBirth = static_cast<double>(engine->getLastBornGrainRatioAtBirth());
            grain.birthAge = engine->getLastBornGrainBirthAgeSamples();
            grain.lifetime = static_cast<std::size_t>(engine->getLastBornGrainLifetimeSamples());
        }

        if (flipAtMidlife && !grain.flipped && (rendered >= midlifeBoundary)) {
            engine->setGrainReverseProbability(0.0f);
            grain.flipped = true;
        }
    }

    grain.born = engine->getTotalGrainsBorn();
    grain.reverseBorn = engine->getTotalReverseGrainsBorn();
    grain.ringCold = engine->getSkippedTriggerCountRingCold();
    grain.poolFull = engine->getSkippedTriggerCountPoolFull();
    grain.probabilityAtEnd = engine->getGrainReverseProbability();
    grain.minAge = engine->getMinObservedGrainAgeSamples();
    grain.maxAge = engine->getMaxObservedGrainAgeSamples();
    return grain;
}

/// @brief SC-002's isolation preconditions, asserted for every arm.
///
/// A COLLIDING SCHEDULER BIRTH FAILS THE CASE LOUDLY. It is a fixture defect to
/// be fixed, never a reason to widen clause (a)'s or (b)'s bounds.
void requireOneIsolatedGrain(const IsolatedGrain& grain) {
    INFO("capacity=" << grain.capacity << " interonset=" << grain.interonsetSamples
                     << " T=" << grain.birthSample << " readStart=" << grain.readStart
                     << " L'=" << grain.lifetime << " (expected " << grain.expectedLifetime
                     << ") firstBirthBoundary=" << grain.firstBirthBoundary
                     << " born=" << grain.born << " reverseBorn=" << grain.reverseBorn
                     << " maxActiveBeforeBirth=" << grain.maxActiveBeforeBirth
                     << " maxActiveOverall=" << grain.maxActiveOverall
                     << " ringCold=" << grain.ringCold << " poolFull=" << grain.poolFull
                     << " birthAge=" << grain.birthAge << " ratio=" << grain.ratioAtBirth
                     << " minAge=" << grain.minAge << " maxAge=" << grain.maxAge);

    // The ring geometry, from RollingCaptureBuffer's own rounding rule.
    REQUIRE(grain.capacity
            == nextPowerOfTwo(static_cast<std::size_t>(
                   static_cast<double>(kReverseCaptureSeconds) * kReverseSampleRate)));

    // THE SCHEDULER GRID. The countdown is a float decremented by 1.0f per
    // sample (grain_scheduler.h:74), so a non-integral interonset would put the
    // tick on an unpredictable sample and the chirp would be misplaced; and a
    // tick off the 64-sample control grid would put the birth mid-block, where
    // the observation boundary below is no longer the birth block.
    REQUIRE(grain.interonsetSamples == std::floor(grain.interonsetSamples));
    REQUIRE((grain.birthSample % kObservationBlock) == std::size_t{0});

    // Step 2 of the protocol: nothing was live when the grain was born.
    REQUIRE(grain.maxActiveBeforeBirth == std::size_t{0});
    // The tick at absolute sample 0, rejected on a one-sample ring, and nothing
    // else: a second ring-cold rejection would mean an attempt this fixture did
    // not account for.
    REQUIRE(grain.ringCold == std::uint64_t{1});
    REQUIRE(grain.poolFull == std::uint64_t{0});

    // Steps 3 and 4: exactly one grain, born in the block that contains T, and
    // never more than one alive at any block boundary of the measured span.
    REQUIRE(grain.born == std::uint64_t{1});
    REQUIRE(grain.firstBirthBoundary == grain.birthSample + kObservationBlock);
    REQUIRE(grain.maxActiveOverall == std::size_t{1});

    // No truncation - clause (c)'s identity is stated on L' - 1 and would be
    // measuring a different grain if the window had truncated it.
    REQUIRE(grain.lifetime == grain.expectedLifetime);
    // The birth age is the kMinAgeSamples clamp, KNOWN and not drawn, which is
    // what lets the references below be built from a fixed source index.
    REQUIRE(grain.birthAge == static_cast<float>(AtmosphereEngine::kMinAgeSamples));
}

/// @brief The grain's own output span: `[T, T + L')` of the left channel.
[[nodiscard]] std::span<const float> grainSpanOf(const IsolatedGrain& grain) {
    return std::span<const float>(grain.outLeft).subspan(grain.birthSample, grain.lifetime);
}

/// @brief Clause (a)'s two references, Hann-windowed exactly as the grain is.
///
/// `reversed == true` builds `w[k] * source[readStart - k]` - the source segment
/// the grain would read walking BACKWARDS, which is the same thing as
/// "`[readStart - L' + 1, readStart]` reversed". `false` builds
/// `w[k] * source[readStart + k]`, the forward segment.
///
/// THE WINDOW IS THE ANALYTIC HANN, not a copy of the engine's table. The engine
/// looks up a 4096-entry sampling of the same raised cosine
/// (core/grain_envelope.h:45-52) with FR-027's endpoint conditioning applied to
/// the outer 64 entries (atmosphere_engine.h:1400-1414) - a region where Hann is
/// already below 2.5e-3, so the two differ by parts in 1e-4 of the span's
/// energy. Correlating a windowed signal against an UNwindowed reference, by
/// contrast, caps the score at mean(w)/sqrt(mean(w^2)) = 0.8165 and would fail
/// the 0.90 bar on correct code - which is why the reference is windowed at all.
[[nodiscard]] std::vector<float> buildReference(const IsolatedGrain& grain, bool reversed) {
    std::vector<float> reference(grain.lifetime, 0.0f);
    if (grain.lifetime < 2u) {
        return reference;
    }
    const double denom = static_cast<double>(grain.lifetime - 1u);
    for (std::size_t k = 0; k < grain.lifetime; ++k) {
        const double phase = static_cast<double>(k) / denom;
        const double window =
            0.5 * (1.0 - std::cos(static_cast<double>(Krate::DSP::kTwoPi) * phase));
        const std::size_t index = reversed ? (grain.readStart - k) : (grain.readStart + k);
        reference[k] = static_cast<float>(window * static_cast<double>(grain.source[index]));
    }
    return reference;
}

/// @brief Clause (b)'s slope over the MIDDLE 80 % of the grain span.
///
/// The Hann envelope drives the outer tenths to ~0, where the centroid is a
/// measurement of the numerical floor rather than of the chirp.
[[nodiscard]] double middleCentroidSlope(const IsolatedGrain& grain) {
    const std::span<const float> span = grainSpanOf(grain);
    const std::size_t edge = span.size() / 10u;
    if (span.size() <= (2u * edge)) {
        return 0.0;
    }
    return centroidSlopeHzPerSecond(span.subspan(edge, span.size() - (2u * edge)),
                                    kReverseSampleRate, kCentroidFrameSamples,
                                    kCentroidHopSamples);
}

}  // namespace

// =============================================================================
// SC-002 - A REVERSE GRAIN IS THE TIME-REVERSED READ. The criterion with teeth.
// =============================================================================
//
// Four clauses, each covering a failure the others cannot see:
//
//   (a) the waveform IS the reversed source (correlation, both ways round) and
//       the envelope endpoints are exactly 0;
//   (b) the spectral centroid trajectory has the NEGATIVE slope only a backwards
//       read can produce. (a) alone is satisfiable by a symmetric artefact -
//       anything whose autocorrelation is even scores well against a reversed
//       reference - and the chirp's slope is the asymmetry that rules it out;
//   (c) THE FRACTIONAL-RATIO ARM, which is the clause that makes FR-012
//       falsifiable at all. (a) and (b) both run at ratio == 1, where the
//       borrow's ceil-correction branch never fires; an off-by-one borrow
//       renders deterministically, NaN-free, bounded and partition-invariantly
//       at ratio 1 and is wrong at EVERY production ratio;
//   (d) FR-008's birth snapshot, asserted where it is observable: the grain must
//       keep reading backwards after the control value has changed under it.
//
// RED AT T014, GREEN AT T015. The birth draw (T010) and the direction-dependent
// window (T013) have landed - the grain IS born `reversed`
// (atmosphere_engine.h:1694, :1887) and IS admitted on wUp = 0, wDown = 1 + r
// (:1729-1731) - but renderGrainSpan()'s read walk is still the forward
// `advance` lambda (:1980-1986). A reverse grain therefore renders the FORWARD
// segment today, so (a)'s first REQUIRE, (b)'s sign, (c)'s span and (d)'s slope
// all fail, and the FORWARD comparison arms - which are the fixture's own
// controls - pass.
TEST_CASE("AtmosphereGhost_ReverseIsTimeReversed", "[atmosphere][ghost]") {
    using Krate::DSP::semitonesToRatio;

    SECTION("(a) and (b) at ratio 1 - the waveform and the centroid slope") {
        const IsolatedGrain reverse = renderIsolatedGrain(0.0f, 1.0f, /*flipAtMidlife=*/false);
        requireOneIsolatedGrain(reverse);
        // Step 5: the per-birth draw is PINNED, so the accessor is read by a
        // criterion rather than merely existing (FR-009, FR-006).
        REQUIRE(reverse.reversedAtBirth);
        REQUIRE(reverse.reverseBorn == std::uint64_t{1});
        REQUIRE(reverse.ratioAtBirth == Catch::Approx(1.0).epsilon(1.0e-6));

        const std::span<const float> span = grainSpanOf(reverse);
        const std::vector<float> reversedRef = buildReference(reverse, /*reversed=*/true);
        const std::vector<float> forwardRef = buildReference(reverse, /*reversed=*/false);

        const double peakReversed =
            normalisedCrossCorrelationPeak(span, reversedRef, kMaxCorrelationLag);
        const double peakForward =
            normalisedCrossCorrelationPeak(span, forwardRef, kMaxCorrelationLag);
        INFO("(a) peakReversed=" << peakReversed << " peakForward=" << peakForward);

        // --- (a) THE TEETH. Today the reverse grain renders the forward
        //     segment, so these two scores are swapped and the first fails.
        REQUIRE(peakReversed >= 0.90);
        REQUIRE(peakForward <= 0.30);

        // --- (a), the endpoint guarantee relocated here from SC-003 (the
        //     2026-09-23 phase ruling, item 10): with more than one grain live no
        //     individual endpoint reaches the output, so the ONE-grain protocol
        //     is the only place it is observable. EXACTLY and bit-wise - the
        //     envelope's forced entry 0 and its kEnvelopeTailZeroEntries tail run
        //     (atmosphere_engine.h:1409-1414) put a hard zero at both ends for
        //     every legal L' >= 2 (banner :96-101).
        REQUIRE(span.front() == 0.0f);
        REQUIRE(span.back() == 0.0f);

        // --- (b) the centroid slope, against the same grain born forward.
        const IsolatedGrain forward = renderIsolatedGrain(0.0f, 0.0f, /*flipAtMidlife=*/false);
        requireOneIsolatedGrain(forward);
        REQUIRE_FALSE(forward.reversedAtBirth);
        REQUIRE(forward.reverseBorn == std::uint64_t{0});

        const double reverseSlope = middleCentroidSlope(reverse);
        const double forwardSlope = middleCentroidSlope(forward);
        INFO("(b) reverseSlope=" << reverseSlope << " Hz/s forwardSlope=" << forwardSlope
                                 << " Hz/s (expected -+3800)");
        REQUIRE(reverseSlope < 0.0);
        REQUIRE(std::abs(reverseSlope) >= kMinCentroidSlopeMagnitude);
        REQUIRE(forwardSlope > 0.0);
        REQUIRE(std::abs(forwardSlope) >= kMinCentroidSlopeMagnitude);

        // --- The fixture's own control, GREEN today and green after T015: the
        //     FORWARD grain must correlate with the FORWARD reference. Without
        //     it, a fixture that rendered silence, or one whose chirp was
        //     misplaced, would satisfy both of (a)'s bounds vacuously.
        const std::span<const float> forwardSpan = grainSpanOf(forward);
        const std::vector<float> controlForwardRef = buildReference(forward, false);
        const std::vector<float> controlReversedRef = buildReference(forward, true);
        const double controlForward =
            normalisedCrossCorrelationPeak(forwardSpan, controlForwardRef, kMaxCorrelationLag);
        const double controlReversed =
            normalisedCrossCorrelationPeak(forwardSpan, controlReversedRef, kMaxCorrelationLag);
        INFO("control arm: forward=" << controlForward << " reversed=" << controlReversed);
        REQUIRE(controlForward >= 0.90);
        REQUIRE(controlReversed <= 0.30);
    }

    SECTION("(c) fractional ratio - the exact age-span identity FR-012 must satisfy") {
        constexpr float kFractionalSemitones = -5.0f;
        const IsolatedGrain grain =
            renderIsolatedGrain(kFractionalSemitones, 1.0f, /*flipAtMidlife=*/false);
        requireOneIsolatedGrain(grain);
        REQUIRE(grain.reversedAtBirth);

        // The ratio is the component's own semitonesToRatio (core/pitch_utils.h),
        // which is what ratioAtPitch calls - so this is the same number the birth
        // arithmetic used, not an independent 2^(-5/12).
        const double ratio = static_cast<double>(semitonesToRatio(kFractionalSemitones));
        REQUIRE(grain.ratioAtBirth == Catch::Approx(ratio).epsilon(1.0e-6));
        REQUIRE(ratio > 0.0);
        REQUIRE(ratio < 1.0);

        // THE IDENTITY. A reverse grain's read walks backwards at `ratio` per
        // sample while the write head walks forwards at 1, so its read age grows
        // at exactly 1 + ratio per sample and, over L' - 1 steps, spans
        // (1 + ratio) * (L' - 1). The folds are engine-lifetime
        // (atmosphere_engine.h:1130-1134) and that is clean here: this render
        // contains exactly ONE grain (asserted above), its birth age is the
        // 64-sample clamp, and both the birth fold (:1911) and the retirement
        // fold (:1966-1973) are covered because T and L' are both multiples of
        // kControlChunkSamples.
        //
        // Under the FORWARD walk the age SHRINKS at ratio per sample instead, so
        // the span comes out as (1 - ratio) * (L' - 1) = 6 019 against the
        // 41 979 required here. That is the T014 red state.
        const double observedSpan =
            static_cast<double>(grain.maxAge) - static_cast<double>(grain.minAge);
        const double expectedSpan = (1.0 + ratio) * static_cast<double>(grain.lifetime - 1u);
        INFO("(c) observedSpan=" << observedSpan << " expectedSpan=" << expectedSpan
                                 << " minAge=" << grain.minAge << " maxAge=" << grain.maxAge);
        REQUIRE(static_cast<double>(grain.minAge)
                == Catch::Approx(static_cast<double>(AtmosphereEngine::kMinAgeSamples))
                       .margin(1.0));
        REQUIRE(observedSpan == Catch::Approx(expectedSpan).margin(1.0));

        // Secondary, from clause (b)'s machinery: the read walks backwards at
        // `ratio`, so the centroid falls at ratio * 3 800 = 2 847 Hz/s.
        const double slope = middleCentroidSlope(grain);
        INFO("(c) slope=" << slope << " Hz/s (expected ~" << -(ratio * 3800.0) << ")");
        REQUIRE(slope < 0.0);
        REQUIRE(std::abs(slope) >= kMinCentroidSlopeMagnitude);
    }

    SECTION("(d) FR-008 - the direction is a BIRTH snapshot, not a per-sample read") {
        const IsolatedGrain grain = renderIsolatedGrain(0.0f, 1.0f, /*flipAtMidlife=*/true);
        requireOneIsolatedGrain(grain);
        REQUIRE(grain.reversedAtBirth);

        // The change actually happened, and it happened while the grain was
        // live. Asserting a consequence of a write that never landed would be
        // the same dishonesty as asserting a consequence of a commented-out call.
        REQUIRE(grain.flipped);
        REQUIRE(grain.probabilityAtEnd == 0.0f);

        const std::span<const float> span = grainSpanOf(grain);
        const std::vector<float> reversedRef = buildReference(grain, /*reversed=*/true);
        const double peakReversed =
            normalisedCrossCorrelationPeak(span, reversedRef, kMaxCorrelationLag);
        const double slope = middleCentroidSlope(grain);
        INFO("(d) peakReversed=" << peakReversed << " slope=" << slope << " Hz/s");

        // Over the WHOLE span, not just the half before the change point. A
        // per-sample re-read of reverseProbability_ turns the second half
        // forwards: the correlation roughly halves and the slope over the whole
        // span collapses toward 0 or flips.
        REQUIRE(peakReversed >= 0.90);
        REQUIRE(slope < 0.0);
        REQUIRE(std::abs(slope) >= kMinCentroidSlopeMagnitude);
        REQUIRE(span.front() == 0.0f);
        REQUIRE(span.back() == 0.0f);
    }
}

// =============================================================================
// SC-005 - TRIGGER ACCOUNTING (T016)
// =============================================================================
//
// FR-019 through FR-024, measured as arithmetic on counters rather than as
// audio. Every arm runs at `density = kMinDensity = 0.1`
// (atmosphere_engine.h:303), so the interonset is 480 000 samples at 48 kHz and
// the density scheduler contributes AT MOST the free tick at sample 0 to any
// render shorter than 10 s. Every other birth below is a trigger birth, and
// where a scheduler tick can still land the assertion is a BOUND, never an
// equality (VoragoGhostFix::schedulerTicks is the one definition of that count).
//
// THE ADMISSION CONSTANT, stated once here and used by every arm:
// makeTriggerEngine() pins every control that feeds `needed` to a neutral value,
// so `needed == kMinAgeSamples + kMinAgeSamples == 128` at every rate
// (atmosphere_engine.h:1817-1819). A ring holding >= 128 samples admits a
// forward grain; a ring holding fewer rejects it with ++skipRingCold_. That is
// the difference between the "warm" and "cold" arms below, and it is a constant
// rather than a measurement.
TEST_CASE("AtmosphereGhost_TriggerAccounting", "[atmosphere][ghost]") {
    using AE = AtmosphereEngine;

    // =========================================================================
    // (a) N triggers on a WARM ring produce N triggered births
    // =========================================================================
    //
    // The pool-full term is carried explicitly (`poolFullDelta`) and then
    // asserted to be zero, rather than being left out of the identity: with the
    // precondition below - an EMPTY pool at the moment of firing - no N in
    // {1, 5, 64} can reach kMaxGrains, so a non-zero poolFullDelta is a real
    // failure and not a fixture artefact, and the reader can see which.
    //
    // getTotalGrainsBorn() is BOUNDED, not equated: the scheduler could in
    // principle tick once inside the span. At this fixture it cannot (the
    // countdown after a 65 536-sample pre-roll is ~414 000 samples), but writing
    // the bound costs nothing and survives a seed or geometry change.
    SECTION("(a) N triggers on a warm ring -> N triggered births, N in {1, 5, 64}") {
        for (const std::size_t n : {std::size_t{1}, std::size_t{5}, std::size_t{64}}) {
            INFO("N=" << n);
            auto engine = makeTriggerEngine(0.0f);
            const std::size_t capacity = engine->getCaptureCapacitySamples();
            REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, 512u) == capacity);

            // THE PRECONDITION, ASSERTED. At kMinGrainSeconds a grain lives
            // round(0.05 * 48 000) = 2 400 samples, so a short idle render
            // retires anything the pre-roll's free tick may have started.
            renderSpan(*engine, 2400u, 1u);
            REQUIRE(engine->getActiveGrainCount() == std::size_t{0});

            const TriggerCounters before = readCounters(*engine);
            fireTriggers(*engine, n);
            // n <= kMaxGrains, so nothing may be refused at the queue.
            REQUIRE(engine->getDroppedTriggerCount() == before.dropped);

            // >= N samples, so all N are consumed at FR-020's one per sample.
            renderSpan(*engine, n, 1u);
            const TriggerCounters after = readCounters(*engine);

            const std::uint64_t poolFullDelta = after.poolFull - before.poolFull;
            const std::uint64_t triggeredDelta = after.triggered - before.triggered;
            const std::uint64_t bornDelta = after.born - before.born;
            INFO("poolFullDelta=" << poolFullDelta << " triggeredDelta=" << triggeredDelta
                                  << " bornDelta=" << bornDelta << " active=" << after.active);

            REQUIRE(poolFullDelta == std::uint64_t{0});
            REQUIRE(triggeredDelta == static_cast<std::uint64_t>(n) - poolFullDelta);
            REQUIRE(bornDelta >= static_cast<std::uint64_t>(n));
            REQUIRE(bornDelta <= static_cast<std::uint64_t>(n) + std::uint64_t{1});
            REQUIRE(after.dropped == before.dropped);
        }
    }

    // =========================================================================
    // (b) COLD ARM - queue saturation, and FR-020's ONE PER SAMPLE
    // =========================================================================
    //
    // 74 = kMaxGrains + 10 calls before any render at all. The queue saturates at
    // kMaxGrains and the last ten are DROPPED, never banked (FR-019's "skip,
    // never steal" applied to the queue).
    //
    // THE RING-COLD TOTAL IS `64 + schedulerTicks`, NOT `64`. The scheduler's
    // free tick at sample 0 also fails admission on an empty ring
    // (grain_scheduler.h:40 plus :73-76 make it fire on the very first rendered
    // sample; the ring then holds one sample against a `needed` of 128), so a
    // bare `== 64` is WRONG ON CORRECT CODE. schedulerTicks() is the one
    // definition of that count and returns 1 for any render under 10 s here.
    //
    // THE RATE ASSERTION is the only purchase this phase has on FR-020's "at most
    // one per sample, in sample order", and it is what stops an implementation
    // that drains the whole queue on the first sample from passing every other
    // criterion in the phase while invalidating FR-051's derivation (which counts
    // AT MOST TWO births per sample).
    SECTION("(b) cold arm - 74 calls, 10 dropped, 64 consumed ONE PER SAMPLE") {
        auto engine = makeTriggerEngine(0.0f);

        fireTriggers(*engine, kSaturatingCalls);
        REQUIRE(engine->getDroppedTriggerCount() == kExpectedDropped);
        REQUIRE(engine->getTotalGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getSkippedTriggerCountRingCold() == std::uint64_t{0});

        // --- sample 0: the scheduler's free tick PLUS exactly one trigger -----
        renderSpan(*engine, 1u, 1u);
        const std::uint64_t afterFirst = engine->getSkippedTriggerCountRingCold();
        INFO("after sample 0: ringCold=" << afterFirst);
        REQUIRE(afterFirst == std::uint64_t{2});

        // --- sample 1: exactly one more --------------------------------------
        renderSpan(*engine, 1u, 1u);
        INFO("after sample 1: ringCold=" << engine->getSkippedTriggerCountRingCold());
        REQUIRE(engine->getSkippedTriggerCountRingCold() - afterFirst == std::uint64_t{1});

        // --- the remaining 126 samples: the other 62 queued triggers ----------
        renderSpan(*engine, 126u, 1u);
        REQUIRE(engine->getSkippedTriggerCountRingCold() - afterFirst == std::uint64_t{63});

        const std::size_t ticks =
            VoragoGhostFix::schedulerTicks(128u, kTriggerSampleRate, AE::kMinDensity);
        INFO("total ringCold=" << engine->getSkippedTriggerCountRingCold()
                               << " schedulerTicks=" << ticks);
        REQUIRE(ticks == std::size_t{1});
        REQUIRE(engine->getSkippedTriggerCountRingCold()
                == static_cast<std::uint64_t>(AE::kMaxGrains) + static_cast<std::uint64_t>(ticks));
        REQUIRE(engine->getTotalGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getTotalTriggeredGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getDroppedTriggerCount() == kExpectedDropped);
        // This arm reaches the QUEUE cap, never the POOL cap - the warm arm below
        // is the one that reaches the pool.
        REQUIRE(engine->getSkippedTriggerCountPoolFull() == std::uint64_t{0});
    }

    // =========================================================================
    // (b) WARM ARM - the POOL bound (roadmap line 509), and "skip, never steal"
    // =========================================================================
    //
    // Run at reverse probability 0 so every one of the 64 admissible births IS
    // admitted by the shipped arithmetic; the reverse warm case is T012's.
    //
    // THE SECOND HALF IS THE ONE WITH TEETH. Ten further calls against a FULL
    // pool must move skipPoolFull_ by exactly ten, move totalBorn_ by zero, and
    // leave getActiveGrainCount() at kMaxGrains - i.e. no live grain was stolen
    // (atmosphere_engine.h:1665-1675 returns BEFORE any RNG draw or any slot
    // write; contrast GrainPool::acquireGrain, primitives/grain_pool.h:71-91,
    // which does steal).
    SECTION("(b) warm arm - the pool cap holds, and steals nothing") {
        auto engine = makeTriggerEngine(0.0f);
        const std::size_t capacity = engine->getCaptureCapacitySamples();
        REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, 512u) == capacity);

        const TriggerCounters before = readCounters(*engine);
        const std::size_t k0 = before.active;
        INFO("k0=" << k0);

        fireTriggers(*engine, kSaturatingCalls);
        renderSpan(*engine, AE::kMaxGrains, 1u);  // 64 samples, one trigger each

        const TriggerCounters mid = readCounters(*engine);
        INFO("bornDelta=" << (mid.born - before.born)
                          << " poolFullDelta=" << (mid.poolFull - before.poolFull)
                          << " droppedDelta=" << (mid.dropped - before.dropped)
                          << " active=" << mid.active);
        REQUIRE(mid.born - before.born
                == static_cast<std::uint64_t>(AE::kMaxGrains) - static_cast<std::uint64_t>(k0));
        REQUIRE(mid.poolFull - before.poolFull == static_cast<std::uint64_t>(k0));
        REQUIRE(mid.dropped - before.dropped == kExpectedDropped);
        REQUIRE(mid.active == AE::kMaxGrains);

        // --- ten further calls against a FULL pool ----------------------------
        // Sixteen samples is long enough to consume all ten and far short of the
        // 2 400-sample grain lifetime, so nothing retires and frees a slot.
        fireTriggers(*engine, 10u);
        renderSpan(*engine, 16u, 1u);

        const TriggerCounters end = readCounters(*engine);
        INFO("second half: bornDelta=" << (end.born - mid.born)
                                       << " poolFullDelta=" << (end.poolFull - mid.poolFull)
                                       << " retiredDelta=" << (end.retired - mid.retired)
                                       << " active=" << end.active);
        REQUIRE(end.born - mid.born == std::uint64_t{0});
        REQUIRE(end.poolFull - mid.poolFull == std::uint64_t{10});
        REQUIRE(end.dropped == mid.dropped);
        REQUIRE(end.active == AE::kMaxGrains);
        // Skip, NEVER steal: no grain was retired to make room either.
        REQUIRE(end.retired == mid.retired);
    }

    // =========================================================================
    // (c) FR-022 - a cold-ring trigger is consumed EXACTLY ONCE
    // =========================================================================
    //
    // The failure this arm exists for is a "helpful" implementation that leaves a
    // rejected trigger in the queue so it can birth once the ring warms up. That
    // is a deferred burst, and it is exactly what FR-022 forbids: the queue is a
    // request counter, not a retry list.
    SECTION("(c) a cold-ring trigger is consumed exactly once - no deferred burst") {
        auto engine = makeTriggerEngine(0.0f);
        constexpr std::size_t kColdCalls = 32u;

        fireTriggers(*engine, kColdCalls);
        REQUIRE(engine->getDroppedTriggerCount() == std::uint64_t{0});

        renderSpan(*engine, kColdCalls, 1u);  // 32 samples, one trigger each
        const std::size_t ticks =
            VoragoGhostFix::schedulerTicks(kColdCalls, kTriggerSampleRate, AE::kMinDensity);
        INFO("cold: ringCold=" << engine->getSkippedTriggerCountRingCold() << " ticks=" << ticks);
        REQUIRE(engine->getTotalGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getTotalTriggeredGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getSkippedTriggerCountRingCold()
                == static_cast<std::uint64_t>(kColdCalls) + static_cast<std::uint64_t>(ticks));

        // --- warm the ring right up, then keep rendering -----------------------
        const std::size_t capacity = engine->getCaptureCapacitySamples();
        renderSpan(*engine, capacity + 1024u, 1u);
        INFO("after warming: born=" << engine->getTotalGrainsBorn()
                                    << " triggered=" << engine->getTotalTriggeredGrainsBorn());
        REQUIRE(engine->getTotalTriggeredGrainsBorn() == std::uint64_t{0});

        // NOT VACUOUS: the warm ring really would have admitted one. A fresh
        // trigger fired now DOES birth, so the zero above is the absence of a
        // deferred burst and not the absence of an admitting ring.
        fireTriggers(*engine, 1u);
        renderSpan(*engine, 64u, 1u);
        REQUIRE(engine->getTotalTriggeredGrainsBorn() == std::uint64_t{1});
    }

    // =========================================================================
    // (d1) FR-023 - a LATCHED engine refuses a trigger and moves NO counter
    // =========================================================================
    //
    // A latched engine returns from processStereoBlock before pass A
    // (atmosphere_engine.h:707-712), so a trigger banked while Latched would
    // never be drained - it would sit in the queue until reset() and then fire as
    // a burst. triggerGrain() must therefore refuse it at the entry point.
    SECTION("(d1) FR-023 - a latched engine refuses a trigger and moves no counter") {
        auto engine = makeTriggerEngine(0.0f);
        const std::size_t capacity = engine->getCaptureCapacitySamples();
        REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, 512u) == capacity);

        fireTriggers(*engine, 1u);
        renderSpan(*engine, 64u, 1u);
        REQUIRE(engine->getActiveGrainCount() > std::size_t{0});  // something to silence

        // kSilenceRampMs = 10 ms = 480 samples at 48 kHz; 2 048 is well past it.
        engine->silence();
        renderSpan(*engine, 2048u, 1u);
        REQUIRE(engine->getActiveGrainCount() == std::size_t{0});

        // --- the latch really is latched: the output is EXACT zero -------------
        constexpr std::size_t kProbe = 256u;
        std::vector<float> inLeft(kProbe, 0.5f);
        std::vector<float> inRight(kProbe, 0.5f);
        std::vector<float> outLeft(kProbe, 1.0f);  // poisoned, so a missing write shows
        std::vector<float> outRight(kProbe, 1.0f);
        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                   kProbe);
        std::size_t nonZero = 0;
        for (std::size_t i = 0; i < kProbe; ++i) {
            if (outLeft[i] != 0.0f || outRight[i] != 0.0f) {
                ++nonZero;
            }
        }
        REQUIRE(nonZero == std::size_t{0});

        // --- the trigger, against the latch -----------------------------------
        const TriggerCounters before = readCounters(*engine);
        fireTriggers(*engine, 8u);
        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                   kProbe);
        const TriggerCounters after = readCounters(*engine);
        INFO("latched: born " << before.born << "->" << after.born << " dropped " << before.dropped
                              << "->" << after.dropped << " triggered " << before.triggered << "->"
                              << after.triggered);
        REQUIRE(after.born == before.born);
        REQUIRE(after.retired == before.retired);
        REQUIRE(after.triggered == before.triggered);
        REQUIRE(after.reverseBorn == before.reverseBorn);
        REQUIRE(after.dropped == before.dropped);
        REQUIRE(after.poolFull == before.poolFull);
        REQUIRE(after.ringCold == before.ringCold);
        REQUIRE(after.active == std::size_t{0});
    }

    // =========================================================================
    // (d2) FR-024 - reset() CLEARS THE PENDING QUEUE
    // =========================================================================
    //
    // ASSERTED ON CONSUMPTION, NOT ON BIRTHS. A births assertion would be
    // vacuous: reset() also empties the capture ring (atmosphere_engine.h:542),
    // so an UNCLEARED queue would be rejected ring-cold and totalBorn_ would read
    // 0 either way. What distinguishes the two is how many ring-cold REJECTIONS
    // the post-reset render records - 64 + schedulerTicks if the queue survived,
    // schedulerTicks if it did not.
    //
    // The comparison is ABSOLUTE and not a delta, because reset() zeroes
    // skipRingCold_ itself (:646).
    SECTION("(d2a) reset() clears the queue - counted on CONSUMPTION, cold ring") {
        auto engine = makeTriggerEngine(0.0f);

        fireTriggers(*engine, kSaturatingCalls);
        // The queue is provably AT ITS CAP: the last ten were refused, which can
        // only happen once pendingTriggers_ has reached kMaxGrains.
        REQUIRE(engine->getDroppedTriggerCount() == kExpectedDropped);

        (*engine).reset();

        renderSpan(*engine, 128u, 1u);
        const std::size_t ticks =
            VoragoGhostFix::schedulerTicks(128u, kTriggerSampleRate, AE::kMinDensity);
        INFO("post-reset ringCold=" << engine->getSkippedTriggerCountRingCold()
                                    << " ticks=" << ticks);
        REQUIRE(ticks == std::size_t{1});
        REQUIRE(engine->getSkippedTriggerCountRingCold() == static_cast<std::uint64_t>(ticks));
        REQUIRE(engine->getTotalGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getDroppedTriggerCount() == std::uint64_t{0});
    }

    // The other half: the three counters reset() must zero, each PROVED non-zero
    // first. Without this an implementation that forgot `totalReverseBorn_ = 0;`
    // or `totalTriggered_ = 0;` passes every other criterion in the phase.
    SECTION("(d2b) reset() zeroes the triggered / reverse / dropped counters") {
        auto engine = makeTriggerEngine(1.0f);  // probability 1: every birth reversed
        const std::size_t capacity = engine->getCaptureCapacitySamples();
        REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, 512u) == capacity);

        fireTriggers(*engine, kSaturatingCalls);
        REQUIRE(engine->getDroppedTriggerCount() == kExpectedDropped);
        renderSpan(*engine, AE::kMaxGrains, 1u);

        INFO("before reset: triggered=" << engine->getTotalTriggeredGrainsBorn()
                                        << " reverseBorn=" << engine->getTotalReverseGrainsBorn()
                                        << " dropped=" << engine->getDroppedTriggerCount());
        REQUIRE(engine->getTotalTriggeredGrainsBorn() > std::uint64_t{0});
        REQUIRE(engine->getTotalReverseGrainsBorn() > std::uint64_t{0});
        REQUIRE(engine->getDroppedTriggerCount() > std::uint64_t{0});

        (*engine).reset();

        REQUIRE(engine->getTotalTriggeredGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getTotalReverseGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->getDroppedTriggerCount() == std::uint64_t{0});
    }

    // =========================================================================
    // (d3) FR-023 clause 3 - `Silencing` ACCEPTS triggers
    // =========================================================================
    //
    // The clause an over-broad `runState_ != RunState::Running` guard silently
    // breaks. While Silencing the engine still captures, still ticks the
    // scheduler and still births (only finishChunk multiplies by the decaying
    // ramp, atmosphere_engine.h:2674-2681), so a trigger fired there MUST be
    // admitted.
    //
    // THE BIRTH, NOT THE ABSENCE OF A DROP, IS THE ASSERTION WITH TEETH: a
    // triggerGrain() that returned early would ALSO leave
    // getDroppedTriggerCount() unmoved.
    SECTION("(d3) FR-023 clause 3 - Silencing accepts triggers") {
        auto engine = makeTriggerEngine(0.0f);
        const std::size_t capacity = engine->getCaptureCapacitySamples();
        REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, 512u) == capacity);

        fireTriggers(*engine, 1u);
        renderSpan(*engine, 64u, 1u);
        REQUIRE(engine->getActiveGrainCount() > std::size_t{0});

        engine->silence();

        // 64 of the 480-sample ramp. Still Silencing, and still sounding: the
        // grain alive from above has 2 400 samples to run.
        renderSpan(*engine, 64u, 1u);
        REQUIRE(engine->getActiveGrainCount() > std::size_t{0});

        const TriggerCounters before = readCounters(*engine);
        fireTriggers(*engine, 1u);
        renderSpan(*engine, 64u, 1u);  // 128 of 480: the ramp has NOT latched
        const TriggerCounters after = readCounters(*engine);

        INFO("silencing: droppedDelta=" << (after.dropped - before.dropped) << " triggeredDelta="
                                        << (after.triggered - before.triggered)
                                        << " active=" << after.active);
        REQUIRE(after.dropped == before.dropped);
        REQUIRE(after.triggered - before.triggered >= std::uint64_t{1});
        // Proof the engine really was Silencing and not already Latched: a latch
        // retires every grain and zeroes activeCount_ (:2703-2712).
        REQUIRE(after.active > std::size_t{0});
    }
}

// =============================================================================
// SC-012 - FR-051's PASS-A SCRATCH BOUND (T016)
// =============================================================================
//
// UNTAGGED, and the BINDING portable criterion for FR-051. `retiredScratch_` and
// `dueScratch_` are sized ONCE in prepare() at `kMaxGrains * 2` = 128 entries
// (atmosphere_engine.h:443-448) and both are written by UNCHECKED INDEX on the
// audio thread (`retiredScratch_[retiredCount]` at :2214-2216, `dueScratch_[k]`
// at :2278-2284). With T017's SECOND birth site in pass A, one control chunk can
// retire far more than 128 grains, and each retirement is exactly one
// retiredScratch_ write and one consumed dueScratch_ entry.
//
// WHAT AllocationScope CAN AND CANNOT PROVE HERE, stated so no compliance row
// overstates it:
//   CAN    - that the two renders below moved no allocation onto the audio
//            thread.
//   CANNOT - the overflow itself. Writing past a std::vector's SIZE allocates
//            nothing; it is a plain out-of-bounds store. The portable witness is
//            therefore the RETIRED DELTA asserted at chunk 2, and the direct
//            witness is T027's ASan run, which must be recorded RED at
//            `kMaxGrains * 2` and GREEN at `kMaxGrains * 3`.
//
// THE REACHING CONFIGURATION (plan P-5), and why each number is what it is:
//   * `sampleRate = 20` with `density = kMaxDensity = 20` makes the interonset
//     exactly 1.0 sample, so GrainScheduler::process() fires on EVERY sample
//     (processors/grain_scheduler.h:73-76, interonset at :100-103). Together with
//     one consumed trigger per sample that is pass A's maximum of TWO births per
//     sample, which is precisely the rate FR-051's sizing has to cover.
//   * `captureSeconds = 30` at that rate gives C = nextPowerOf2(600) = 1 024, so
//     the full-ring pre-roll is 1 024 samples rather than a million.
//   * blur and freeze off, reverse probability 0: direction is irrelevant to the
//     sizing, and an STFT pump would only add cost.
//   * kMaxGrains triggerGrain() calls before EACH block, so the FR-019 queue is
//     at its cap at every chunk boundary.
//   * The PRE-ROLL runs at `grainSeconds = kMinGrainSeconds`, where
//     round(0.05 * 20) = 1 and the `lifetime < 2` test at :1774-1776 rejects every
//     attempt. The ring therefore fills while the POOL stays empty, which is what
//     lets chunk 1 fill the pool from scratch with grains of a known age.
//
// Every control that feeds `needed` is pinned neutral, so - exactly as in
// SC-005's fixture - `needed == 128` and the full 1 024-sample ring admits.
TEST_CASE("AtmosphereGhost_PassAScratchBound", "[atmosphere][ghost]") {
    using AE = AtmosphereEngine;

    constexpr double kScratchSampleRate = 20.0;
    constexpr std::size_t kChunk = AE::kControlChunkSamples;  // 64, and the render length
    constexpr std::size_t kExpectedCapacity = 1024u;

    auto engine = std::make_unique<AE>();
    engine->prepare(kScratchSampleRate, AE::PrepareConfig{.captureSeconds = 30.0f,
                                                          .blurEnabled = false,
                                                          .freezeEnabled = false,
                                                          .blurFftSize = std::size_t{1024},
                                                          .freezeFftSize = std::size_t{2048},
                                                          .maxBlockSamples = std::size_t{2048}});
    engine->setSeed(1u);
    engine->setDensity(AE::kMaxDensity);
    engine->setJitter(0.0f);
    engine->setGrainSeconds(AE::kMinGrainSeconds);  // pre-roll only: L = 1, always rejected
    engine->setPitchSemitones(0.0f);
    engine->setPitchSpread(0.0f);
    engine->setDriftRangeSemitones(0.0f);
    engine->setDriftDepth(0.0f);
    engine->setPositionSeconds(0.0f);
    engine->setPositionSpread(0.0f);
    engine->setDecorrelation(0.0f);
    engine->setPanSpread(0.0f);
    engine->setLevel(1.0f);
    engine->setGrainReverseProbability(0.0f);

    // The geometry, asserted rather than assumed: every count below is derived
    // from C = 1 024 and from kChunk = 64.
    REQUIRE(engine->getCaptureCapacitySamples() == kExpectedCapacity);
    // A COMPILE-TIME fact, so it is a static_assert rather than a REQUIRE on two
    // constants: FR-051's derivation ("<= kMaxGrains active at a chunk start")
    // is only the same statement as "<= kChunk" while these two are equal.
    static_assert(kChunk == AE::kMaxGrains,
                  "SC-012's chunk arithmetic assumes kControlChunkSamples == kMaxGrains");

    REQUIRE(VoragoGhostFix::preRollFullRing(*engine, 1u, kChunk) == kExpectedCapacity);
    // The pre-roll warmed the RING and left the POOL untouched - which is the
    // whole reason it ran at kMinGrainSeconds.
    REQUIRE(engine->getTotalGrainsBorn() == std::uint64_t{0});
    REQUIRE(engine->getActiveGrainCount() == std::size_t{0});

    std::vector<float> inLeft(kChunk, 0.0f);
    std::vector<float> inRight(kChunk, 0.0f);
    VoragoGhostFix::excitePinkPlusTone(inLeft, inRight, 1u);
    std::vector<float> outLeft(kChunk, 0.0f);
    std::vector<float> outRight(kChunk, 0.0f);

    // =========================================================================
    // CHUNK 1 - fill the pool with grains whose whole life is one chunk long
    // =========================================================================
    //
    // round(3.2 * 20) = 64, so L == kChunk: a grain born at offset i inside this
    // chunk survives into the NEXT one unless i == 0, and the two grains born at
    // i == 0 (the scheduler's and the trigger's) are due inside this chunk
    // (`i + lifetime <= numSamples`, :2275-2277). The due list is drained AFTER
    // the birth loop (:2292-2296), so nothing can refill the slots they vacate.
    //
    // >>> DEVIATION FROM tasks.md T016, RECORDED RATHER THAN SMOOTHED OVER.
    // >>> T016 writes these two as `getActiveGrainCount() == kMaxGrains` and
    // >>> `delta retired <= 1`. Under T017's TWO birth sites they cannot both
    // >>> hold: the i == 0 pair is born, is due, and retires at the chunk-end
    // >>> drain, so the pool ends at kMaxGrains - 2 with a retired delta of 2.
    // >>> The pair below asserts exactly the two facts T016 wanted, in the form
    // >>> the arithmetic supports: the pool DID reach its cap (the sum identity)
    // >>> and the ONLY retirements were the ones due at offset 0 (the bound).
    engine->setGrainSeconds(3.2f);
    fireTriggers(*engine, AE::kMaxGrains);

    const std::uint64_t retiredBeforeChunk1 = engine->getTotalGrainsRetired();
    {
        // See "WHAT AllocationScope CAN AND CANNOT PROVE HERE" above: this proves
        // no allocation reached the audio thread. It cannot see the overflow.
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                   kChunk);
    }
    const std::uint64_t deltaRetiredChunk1 =
        engine->getTotalGrainsRetired() - retiredBeforeChunk1;
    const std::size_t activeAfterChunk1 = engine->getActiveGrainCount();

    INFO("chunk 1: active=" << activeAfterChunk1 << " deltaRetired=" << deltaRetiredChunk1
                            << " born=" << engine->getTotalGrainsBorn()
                            << " triggered=" << engine->getTotalTriggeredGrainsBorn()
                            << " poolFull=" << engine->getSkippedTriggerCountPoolFull());
    REQUIRE(deltaRetiredChunk1 <= std::uint64_t{2});
    REQUIRE(static_cast<std::uint64_t>(activeAfterChunk1) + deltaRetiredChunk1
            == static_cast<std::uint64_t>(AE::kMaxGrains));

    // =========================================================================
    // CHUNK 2 - the teeth
    // =========================================================================
    //
    // round(0.1 * 20) = 2, the SHORTEST admissible lifetime (kMinGrainSeconds * 20
    // = 1 is rejected by the `lifetime < 2` test at :1774-1776). Every grain born
    // in this chunk retires two samples later, freeing its slot for the next
    // birth, and the grains chunk 1 left behind come due across its first half.
    //
    // WHY THE DELTA IS THE RIGHT WITNESS. Within one chunk `dueCount` never
    // decreases (:2192-2204 builds it, :2275-2285 appends to it, :2292-2296
    // drains it) and every retirement is one `retiredScratch_` write, so a
    // retired delta above `kMaxGrains * 2` IS a write past the shipped sizing.
    // The assertion is the STRUCTURAL `> 128`, never the measured figure
    // (~188 at this fixture); the upper bound is FR-051's own `3 * kMaxGrains`.
    engine->setGrainSeconds(0.1f);
    fireTriggers(*engine, AE::kMaxGrains);

    const std::uint64_t retiredBeforeChunk2 = engine->getTotalGrainsRetired();
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        engine->processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                   kChunk);
    }
    const std::uint64_t deltaRetired = engine->getTotalGrainsRetired() - retiredBeforeChunk2;

    INFO("chunk 2: deltaRetired=" << deltaRetired << " active=" << engine->getActiveGrainCount()
                                  << " born=" << engine->getTotalGrainsBorn()
                                  << " triggered=" << engine->getTotalTriggeredGrainsBorn());
    REQUIRE(deltaRetired > static_cast<std::uint64_t>(AE::kMaxGrains * 2));   // the teeth: > 128
    REQUIRE(deltaRetired <= static_cast<std::uint64_t>(AE::kMaxGrains * 3));  // FR-051's bound

    // SC-012 says "measured runtime recorded like every other case", and the
    // compliance table also has to carry the measured DELTA - the one number that
    // says how far past the shipped 128 this fixture actually writes. An INFO is
    // printed only on failure, so on a green run it reaches no artifact and the
    // table would have to quote a re-run with `-s`. WARN prints unconditionally,
    // which is the convention the phase's other measured figures already use
    // (vorago_ghost_ext_test.cpp:509, :555, :608).
    WARN("SC-012 measured: chunk 1 active=" << activeAfterChunk1 << " deltaRetired="
                                            << deltaRetiredChunk1 << "; chunk 2 deltaRetired="
                                            << deltaRetired << " (teeth: > " << (AE::kMaxGrains * 2)
                                            << ", FR-051 bound: <= " << (AE::kMaxGrains * 3)
                                            << ")");
}
