// ==============================================================================
// Layer 3: System Tests - BloomEngine behaviour
// ==============================================================================
// Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom): BloomEngine.
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase7-harmonic-bloom/spec.md
//            specs/vorago-phase7-harmonic-bloom/plan.md  (S10.2, S10.3, S10.4)
//            specs/vorago-phase7-harmonic-bloom/tasks.md (T001 creates this TU;
//                                                         T005 lands the two
//                                                         cases below)
//
// SCOPE OF THIS TU (plan S10.2): SC-003, SC-005, SC-006, SC-007, SC-008, SC-012,
//   SC-014, SC-016, SC-017, SC-018, plus SC-009's range-and-neutral arm
//   (BloomEngine_ArgumentContract) - FR-009's clamp and documented-neutral limbs
//   need IEEE semantics nowhere, so they belong in this ordinary TU rather than
//   the -fno-fast-math one.
//
// This TU is DELIBERATELY NOT in the "-fno-fast-math -fno-finite-math-only"
//   block of dsp/tests/CMakeLists.txt: the FR-008/FR-009 guards it exercises must
//   be proved in the /fp:fast + -ffast-math mode the header actually ships in.
//   It may therefore never NAME a non-finite value - any such value is built from
//   a bit pattern through a volatile sink and only ever asserted on via counters.
//
// THIS TU IS ALSO THE ONE PLACE WHERE harmonic_cloud.h, entropy_processor.h AND
//   bloom_engine.h MEET IN ONE TRANSLATION UNIT (plan S10.4). That is deliberate:
//   it is the compile that would fail on an ODR or namespace-scope collision
//   between the new component and the two shipped headers whose constants it
//   RESTATES rather than includes (spec D-1/D-2). BloomEngine itself includes
//   neither, and FR-080 keeps both byte-unchanged.
//
// Remaining cases land in later tasks (T007-T010, T014-T019).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

// The SC-012 three-header meeting point. bloom_engine.h includes NEITHER of the
// other two (FR-080, D-1); the static_asserts below are the live cross-check
// that its restated copies have not drifted from their sources.
#include <krate/dsp/processors/entropy_processor.h>
#include <krate/dsp/systems/bloom_engine.h>
#include <krate/dsp/systems/harmonic_cloud.h>

// ALLOCATION DETECTION: <allocation_detector.h> ONLY (T007 case 3 / SC-007).
// NEVER <allocation_operator_overrides.h> - dsp_systems_tests already has its
// single owner and a second include is a duplicate-symbol link error.
#include <allocation_detector.h>

// SC-008 (T014): a render is pinned through render_fingerprint.h's MEASURED
// tolerances (kSampleTolerance = 5.0e-4f, kMetricTolerance = 2.5e-4) and NEVER
// through a bit-exact float golden (tools/lint-float-bit-goldens.js, roadmap
// line 536). Nothing in this TU stores a float bit pattern of a render.
#include "render_fingerprint.h"

// The finiteness surface FR-008 mandates: detail::isFinite / isNaN / isInf, and
// NEVER std::isnan / std::isinf / std::isfinite - in the header OR in any test
// (tools/lint-nonfinite-symbols.js gates it). Reached through bloom_engine.h
// already, but named here because this TU calls it directly.
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>  // Xorshift32 - the T008 fuzz arms' own stream

#include <algorithm>  // std::stable_sort - SC-005's reference ordering (plan R8)
#include <array>
#include <cmath>  // std::log2, std::fabs - the cents arithmetic of SC-006
#include <cstddef>
#include <cstdint>
#include <cstring>  // std::memcmp / std::memcpy - SC-014's and SC-003's byte arms
#include <limits>
#include <vector>  // SC-005's reference selection only - never on an engine path

// ==============================================================================
// The TU-local fixtures (plan S10.1: NO new test helper header)
// ==============================================================================
// Coded ONCE, here, by T007. Later tasks in this TU (T008, T009, T010, T014,
// T016-T019) REUSE these and do not re-declare them.
// ==============================================================================
namespace {

/// @brief The synthetic parent spectrum every cloud-free case in this TU drives.
///
/// `[0, parentCount)` is a plain 1/n harmonic series - every amplitude is well
/// above BloomEngine::kSilentParentAmplitude, so every slot is an ELIGIBLE
/// parent and an arm that observes "no child appeared" is observing the gate,
/// never an ineligible spectrum. `[parentCount, kMaxSlots)` carries the cloud's
/// own padding form (`ratio = i + 1`, `amplitude = 0`, harmonic_cloud.h:825-826)
/// so a byte-identity arm compares against the shape a real caller would hold.
void fillSyntheticParents(std::array<float, Krate::DSP::BloomEngine::kMaxSlots>& ratios,
                          std::array<float, Krate::DSP::BloomEngine::kMaxSlots>& amplitudes,
                          std::size_t parentCount) noexcept {
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = (i < parentCount) ? 1.0f / static_cast<float>(i + 1) : 0.0f;
    }
}

/// @brief The cloud-free driver (tasks.md T007, the shared `stepEngine` fixture).
///
/// A loop of `processChunk(ratios, amplitudes, parentCount, kControlChunkSamples)`
/// over the caller's parent arrays - i.e. exactly the Phase-10 call shape
/// (seraphis_voice.h:1050-1054) with the HarmonicCloud removed. One call is one
/// control step, so `steps` reads as control steps and
/// `steps * kControlChunkSamples / sampleRate` as seconds.
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

}  // namespace

// ==============================================================================
// T005 case 1 - BloomEngine_CloudContractAssumptions (SC-012)
// ==============================================================================
// BloomEngine restates five figures it could otherwise only obtain by including
// a Layer 2 header or a Layer 3 peer. A restated constant that silently drifts
// from its source is the exact failure mode D-2 trades away by not including:
// the engine would keep compiling, keep passing every behavioural case, and
// hand HarmonicCloud a spectrum built against a contract the cloud no longer
// honours. These asserts are the price of that trade, and they fire at COMPILE
// time - a mutated constant breaks the build rather than reddening a run.
//
// STATIC_REQUIRE is a static_assert plus a registered Catch2 assertion, so the
// case is both a compile-time gate and a non-empty test case.
//
// sizeof(HarmonicCloud) is DELIBERATELY NOT PINNED. The bloom writes into the
// caller's arrays, never into the cloud's storage, so the cloud's object layout
// is none of this component's business; pinning it would turn every unrelated
// Seraphis change into a Vorago build break.
// ==============================================================================
TEST_CASE("BloomEngine_CloudContractAssumptions", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;
    using Krate::DSP::EntropyProcessor;
    using Krate::DSP::HarmonicCloud;

    SECTION("slot ceiling: kMaxSlots == HarmonicCloud::kMaxPartials") {
        // harmonic_cloud.h:138. The hard slot ceiling FR-050 pins to; also the
        // reason Child::slot can be a std::uint8_t.
        STATIC_REQUIRE(HarmonicCloud::kMaxPartials == std::size_t{64});
        STATIC_REQUIRE(BloomEngine::kMaxSlots == HarmonicCloud::kMaxPartials);
    }

    SECTION("control grid: kControlChunkSamples == HarmonicCloud::kControlChunkSamples") {
        // harmonic_cloud.h:144. A component that drifted off the shared grid
        // would decorrelate the per-voice modulation clock at Phase 10.
        STATIC_REQUIRE(HarmonicCloud::kControlChunkSamples == std::size_t{64});
        STATIC_REQUIRE(BloomEngine::kControlChunkSamples == HarmonicCloud::kControlChunkSamples);
    }

    SECTION("silence floor: kSilentParentAmplitude == HarmonicCloud::kTargetAmpEpsilon") {
        // harmonic_cloud.h:258. FR-011: a partial the cloud cannot tell from
        // silence has no harmonics to grow, so it is not an eligible parent.
        STATIC_REQUIRE(HarmonicCloud::kTargetAmpEpsilon == 1.0e-5f);
        STATIC_REQUIRE(BloomEngine::kSilentParentAmplitude == HarmonicCloud::kTargetAmpEpsilon);
    }

    SECTION("spacing floor: kMinRatioSpacingCents == EntropyProcessor::kMinRatioSpacingCents") {
        // entropy_processor.h:80. FR-022 adopts the repo's existing
        // partial-collision figure rather than inventing a second one.
        STATIC_REQUIRE(EntropyProcessor::kMinRatioSpacingCents == 24.0f);
        STATIC_REQUIRE(BloomEngine::kMinRatioSpacingCents ==
                       EntropyProcessor::kMinRatioSpacingCents);
        // The log-domain form FR-022's test is actually evaluated in.
        STATIC_REQUIRE(BloomEngine::kMinRatioSpacingLog2 ==
                       BloomEngine::kMinRatioSpacingCents / 1200.0f);
    }

    SECTION("ratio domain: [kMinChildRatio, kMaxChildRatio] == SpectralState's bounds") {
        // spectral_state.h:51-52, restated (D-2) rather than included: pulling
        // in the morph header for two floats would drag SpectralStateId and the
        // whole authored-state validity model into a component with no states.
        // Used ONLY as FR-021's rejection bounds - never as a clamp.
        STATIC_REQUIRE(BloomEngine::kMinChildRatio == 0.5f);
        STATIC_REQUIRE(BloomEngine::kMaxChildRatio == 128.0f);
        STATIC_REQUIRE(BloomEngine::kMinChildRatio < BloomEngine::kMaxChildRatio);
    }

    SECTION("the detune band stays inside centsToPitchRatioFast's accurate domain") {
        // FR-020. pitch_utils' degree-4 polynomial is documented accurate on
        // |cents| <= 50 and degrades outside it.
        STATIC_REQUIRE(BloomEngine::kMaxDetuneCents <= 50.0f);
        STATIC_REQUIRE(BloomEngine::kMinDetuneCents == BloomEngine::kMinRatioSpacingCents);
        STATIC_REQUIRE(BloomEngine::kMinDetuneCents < BloomEngine::kMaxDetuneCents);
    }

    SECTION("the consumer-tilt range is HarmonicCloud::setSpectralTiltDb's range") {
        // harmonic_cloud.h:194-195, restated for FR-023's tilt compensation.
        STATIC_REQUIRE(BloomEngine::kMinConsumerTiltDbPerOct == -12.0f);
        STATIC_REQUIRE(BloomEngine::kMaxConsumerTiltDbPerOct == 12.0f);
    }
}

// ==============================================================================
// T005 case 2 - BloomEngine_ArgumentContract (SC-009, range-and-neutral arm)
// ==============================================================================
// FR-009 (b) and (c) in full: EVERY float and size setter driven past BOTH range
// ends with the getter asserted to report EXACTLY the clamp, and every indexed
// read called out of range and asserted to return the documented neutral.
//
// The comparisons here are deliberately exact (==) and that is legal: a clamp
// result IS the constant it clamped to, produced by std::clamp returning the
// bound by value, so there is no arithmetic and no tolerance to choose. This is
// a within-run structural identity, not a pinned render - no bit-exact float
// golden is involved.
//
// FR-009 (a) - REJECTION of a non-finite argument - is the OTHER half of SC-009
// and is NOT here: it needs IEEE semantics and therefore lives in
// bloom_engine_nonfinite_test.cpp, the only Phase-7 TU compiled -fno-fast-math.
// This TU must never name a non-finite value.
// ==============================================================================
TEST_CASE("BloomEngine_ArgumentContract", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kHuge = std::numeric_limits<std::size_t>::max();

    SECTION("FR-009 (c): every float setter clamps at both ends") {
        BloomEngine engine;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 64, .numChildSlots = 8});

        // ---- depth: [0, 1] (FR-042) -----------------------------------------
        engine.setDepth(-1.0f);
        REQUIRE(engine.getDepth() == 0.0f);
        engine.setDepth(2.0f);
        REQUIRE(engine.getDepth() == 1.0f);

        // ---- spawn rate: [0, kMaxSpawnRateHz] (FR-040) -----------------------
        engine.setSpawnRateHz(-1.0f);
        REQUIRE(engine.getSpawnRateHz() == 0.0f);
        engine.setSpawnRateHz(10.0f);
        INFO("kMaxSpawnRateHz is " << BloomEngine::kMaxSpawnRateHz << " Hz (one per 20 s)");
        REQUIRE(engine.getSpawnRateHz() == BloomEngine::kMaxSpawnRateHz);
        REQUIRE(BloomEngine::kMaxSpawnRateHz == 0.05f);

        // ---- child gain: [0, 1] (FR-023) ------------------------------------
        engine.setChildGain(-0.5f);
        REQUIRE(engine.getChildGain() == 0.0f);
        engine.setChildGain(5.0f);
        REQUIRE(engine.getChildGain() == 1.0f);

        // ---- fade in: [1, 300] s (FR-030) -----------------------------------
        engine.setFadeInSeconds(0.0f);
        REQUIRE(engine.getFadeInSeconds() == BloomEngine::kMinFadeInSeconds);
        REQUIRE(engine.getFadeInSeconds() == 1.0f);
        engine.setFadeInSeconds(1.0e6f);
        REQUIRE(engine.getFadeInSeconds() == BloomEngine::kMaxFadeInSeconds);
        REQUIRE(engine.getFadeInSeconds() == 300.0f);

        // ---- hold: [0, 900] s (FR-030). 0 IS legal - a child may go straight
        //      from fade-in to fade-out.
        engine.setHoldSeconds(-1.0f);
        REQUIRE(engine.getHoldSeconds() == BloomEngine::kMinHoldSeconds);
        REQUIRE(engine.getHoldSeconds() == 0.0f);
        engine.setHoldSeconds(1.0e6f);
        REQUIRE(engine.getHoldSeconds() == BloomEngine::kMaxHoldSeconds);
        REQUIRE(engine.getHoldSeconds() == 900.0f);

        // ---- fade out: [1, 600] s (FR-030) ----------------------------------
        engine.setFadeOutSeconds(0.0f);
        REQUIRE(engine.getFadeOutSeconds() == BloomEngine::kMinFadeOutSeconds);
        REQUIRE(engine.getFadeOutSeconds() == 1.0f);
        engine.setFadeOutSeconds(1.0e6f);
        REQUIRE(engine.getFadeOutSeconds() == BloomEngine::kMaxFadeOutSeconds);
        REQUIRE(engine.getFadeOutSeconds() == 600.0f);

        // ---- hold jitter: [0, 1] (FR-036) -----------------------------------
        engine.setHoldJitterFraction(-1.0f);
        REQUIRE(engine.getHoldJitterFraction() == 0.0f);
        engine.setHoldJitterFraction(2.0f);
        REQUIRE(engine.getHoldJitterFraction() == 1.0f);

        // ---- consumer tilt: [-12, +12] dB/oct (FR-023 / Q1) -----------------
        engine.setConsumerTiltDb(-99.0f);
        REQUIRE(engine.getConsumerTiltDb() == BloomEngine::kMinConsumerTiltDbPerOct);
        REQUIRE(engine.getConsumerTiltDb() == -12.0f);
        engine.setConsumerTiltDb(99.0f);
        REQUIRE(engine.getConsumerTiltDb() == BloomEngine::kMaxConsumerTiltDbPerOct);
        REQUIRE(engine.getConsumerTiltDb() == 12.0f);

        // ---- wake: [0, 1] (FR-035). The low end must land on EXACTLY 0 - that
        //      is what makes setWake(0) and setDormant(true) identical by
        //      construction rather than by luck.
        engine.setWake(-1.0f);
        REQUIRE(engine.getWakeAmount() == 0.0f);
        engine.setWake(2.0f);
        REQUIRE(engine.getWakeAmount() == 1.0f);

        // ---- relation weights: [0, 1] each (FR-016 / FR-060) ----------------
        // 0 is a LEGAL weight: all-zero weights fall back to a uniform draw, so
        // a zero here disables one relation rather than breaking the draw.
        constexpr BloomEngine::Relation kRelations[]{BloomEngine::Relation::Octave,
                                                     BloomEngine::Relation::Fifth,
                                                     BloomEngine::Relation::DetunedNeighbour};
        for (const BloomEngine::Relation r : kRelations) {
            INFO("relation " << static_cast<unsigned>(static_cast<std::uint8_t>(r)));
            engine.setRelationWeight(r, -1.0f);
            REQUIRE(engine.getRelationWeight(r) == 0.0f);
            engine.setRelationWeight(r, 2.0f);
            REQUIRE(engine.getRelationWeight(r) == 1.0f);
        }
    }

    SECTION("FR-009 (c): every size setter clamps at both ends") {
        BloomEngine engine;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 64, .numChildSlots = 8});

        // ---- K, the strongest-K parent count: [1, kMaxParents] (FR-010) -----
        engine.setParentCount(0);
        REQUIRE(engine.getParentCount() == std::size_t{1});
        engine.setParentCount(99);
        REQUIRE(engine.getParentCount() == BloomEngine::kMaxParents);
        REQUIRE(engine.getParentCount() == std::size_t{8});
        engine.setParentCount(kHuge);
        REQUIRE(engine.getParentCount() == BloomEngine::kMaxParents);

        // ---- children per event: [1, kMaxChildrenPerEvent] (FR-015) ---------
        engine.setChildrenPerEvent(0);
        REQUIRE(engine.getChildrenPerEvent() == std::size_t{1});
        engine.setChildrenPerEvent(99);
        REQUIRE(engine.getChildrenPerEvent() == BloomEngine::kMaxChildrenPerEvent);
        REQUIRE(engine.getChildrenPerEvent() == std::size_t{4});
        engine.setChildrenPerEvent(kHuge);
        REQUIRE(engine.getChildrenPerEvent() == BloomEngine::kMaxChildrenPerEvent);

        // ---- capacity: [1, kMaxSlots] (FR-055), with numChildSlots()
        //      RE-DERIVING min(requested, capacity()) on every read so a
        //      shrink/grow pair is reversible (plan S6.3, C-9). A stored
        //      numChildSlots would clamp to 1 here and STAY at 1.
        engine.setCapacity(0);
        REQUIRE(engine.capacity() == std::size_t{1});
        REQUIRE(engine.numChildSlots() == std::size_t{1});
        REQUIRE(engine.reserveBase() == std::size_t{0});

        engine.setCapacity(99);
        REQUIRE(engine.capacity() == BloomEngine::kMaxSlots);
        REQUIRE(engine.capacity() == std::size_t{64});
        REQUIRE(engine.numChildSlots() == std::size_t{8});
        REQUIRE(engine.reserveBase() == std::size_t{56});

        engine.setCapacity(kHuge);
        REQUIRE(engine.capacity() == BloomEngine::kMaxSlots);
        REQUIRE(engine.numChildSlots() == std::size_t{8});

        // The PrepareConfig fields take the same treatment at prepare() time.
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 0, .numChildSlots = 999});
        REQUIRE(engine.capacity() == std::size_t{1});
        // numChildSlots is clamped to min(kMaxChildren, capacity) == 1.
        REQUIRE(engine.numChildSlots() == std::size_t{1});

        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 999, .numChildSlots = 999});
        REQUIRE(engine.capacity() == BloomEngine::kMaxSlots);
        REQUIRE(engine.numChildSlots() == BloomEngine::kMaxChildren);
        REQUIRE(engine.numChildSlots() == std::size_t{16});
    }

    SECTION("FR-009 (b): an out-of-range Relation is a silent no-op") {
        BloomEngine engine;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 64, .numChildSlots = 8});

        // Three DISTINCT in-range values first, so "unchanged" is a real
        // statement rather than a coincidence of three equal defaults.
        engine.setRelationWeight(BloomEngine::Relation::Octave, 0.25f);
        engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.5f);
        engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 0.75f);

        const auto kBogus = static_cast<BloomEngine::Relation>(std::uint8_t{7});
        engine.setRelationWeight(kBogus, 1.0f);

        REQUIRE(engine.getRelationWeight(BloomEngine::Relation::Octave) == 0.25f);
        REQUIRE(engine.getRelationWeight(BloomEngine::Relation::Fifth) == 0.5f);
        REQUIRE(engine.getRelationWeight(BloomEngine::Relation::DetunedNeighbour) == 0.75f);

        // ...and the getter's own neutral for the same bogus relation.
        REQUIRE(engine.getRelationWeight(kBogus) == 0.0f);
    }

    SECTION("FR-009 (b): every indexed read returns its documented neutral") {
        BloomEngine engine;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 64, .numChildSlots = 8});

        // ---- the child table: domain is [0, kMaxChildren) -------------------
        // NOTE ON INDEX CHOICE. kMaxParents (8) is NOT out of range for these
        // getters - kMaxParents < kMaxChildren (8 < 16) - so probing them there
        // would assert the value of a REAL, idle table entry, and Child::ratio
        // defaults to 1.0f rather than to the 0.0f neutral. The genuinely
        // out-of-range probes are kMaxChildren, kMaxChildren + 1 and SIZE_MAX;
        // the idle-entry contract is asserted separately below, which is the
        // clause getChildSlotIndex actually documents.
        constexpr std::size_t kOutOfRange[]{BloomEngine::kMaxChildren,
                                            BloomEngine::kMaxChildren + 1, kHuge};
        for (const std::size_t i : kOutOfRange) {
            INFO("out-of-range child table index " << i);
            REQUIRE(engine.getChildSlotIndex(i) == BloomEngine::kMaxSlots);
            REQUIRE(engine.getChildParentIndex(i) == std::size_t{0});
            REQUIRE(engine.getChildRatio(i) == 0.0f);
            REQUIRE(engine.getChildAmplitude(i) == 0.0f);
            REQUIRE(engine.getChildTargetAmplitude(i) == 0.0f);
            REQUIRE(engine.getChildRelation(i) == BloomEngine::Relation::Octave);
            REQUIRE(engine.getChildPhase(i) == BloomEngine::Phase::Idle);
            REQUIRE(engine.getChildElapsedSeconds(i) == 0.0f);
            REQUIRE_FALSE(engine.getIsChildFallback(i));
            REQUIRE(engine.getChildHoldSeconds(i) == 0.0f);
        }

        // ---- an IN-RANGE but Phase::Idle entry reads kMaxSlots, the
        //      impossible slot index - the other half of getChildSlotIndex's
        //      contract, and the one a fresh engine can actually exhibit.
        for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
            INFO("idle child table entry " << i);
            REQUIRE(engine.getChildPhase(i) == BloomEngine::Phase::Idle);
            REQUIRE(engine.getChildSlotIndex(i) == BloomEngine::kMaxSlots);
        }

        // ---- the last-event parent selection: domain is
        //      [0, getLastParentSelectionCount()), which is EMPTY until an
        //      event has run, so every index - 0, kMaxParents, kMaxChildren and
        //      SIZE_MAX alike - is out of range and reads kMaxSlots.
        REQUIRE(engine.getLastParentSelectionCount() == std::size_t{0});
        constexpr std::size_t kParentProbes[]{std::size_t{0}, BloomEngine::kMaxParents,
                                              BloomEngine::kMaxChildren, kHuge};
        for (const std::size_t k : kParentProbes) {
            INFO("parent selection index " << k << " with an empty selection");
            REQUIRE(engine.getLastParentIndex(k) == BloomEngine::kMaxSlots);
        }
    }
}

// ==============================================================================
// T007 case 1 - BloomEngine_DisabledIsBitIdenticalPassThrough (SC-014)
// ==============================================================================
// FR-054 / SC-014's COLD-START arms plus the unclamped-return arm. The claim
// under test is the strongest one this component makes: a BloomEngine that is
// disabled - by depth, by having no reserved slots, or by dormancy - is
// BIT-IDENTICAL TO NOT BEING IN THE CHAIN AT ALL. Not "close", not "inaudible":
// the two arrays come back byte-for-byte as they went in, and the returned count
// is the caller's own.
//
// That is what makes the component safe to leave wired in at Phase 10 with its
// macro at zero, and it is exactly the property a "write the pad unconditionally"
// implementation would break on the very first call.
//
// WHY 100 000 CHUNKS. At 48 kHz one chunk is 64 samples, so 100 000 chunks is
// 133 seconds of audio and ~0.56 expected internal clock events at the default
// spawn rate. A pass-through that leaked on an EVENT rather than on every call
// would survive a short run; it does not survive this one.
//
// HOW THE ARMS ASSERT. The per-chunk checks accumulate into plain bools and are
// REQUIREd once, after the loop: a REQUIRE inside a 100 000-iteration loop would
// register 100 000 Catch2 assertions and dominate the suite's runtime.
//
// The == and std::memcmp comparisons here are exact and that is legal: they are
// WITHIN-RUN STRUCTURAL IDENTITIES (nothing was computed, so nothing was
// rounded), not a pinned render. No bit-exact float golden is involved.
// ==============================================================================
TEST_CASE("BloomEngine_DisabledIsBitIdenticalPassThrough", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kChunks = 100000;
    constexpr std::size_t kParentCount = 24;
    constexpr std::size_t kBytes = BloomEngine::kMaxSlots * sizeof(float);

    // One arm body, three configurations. The per-chunk observations accumulate
    // into this record and are asserted once, after the loop.
    struct ArmResult {
        bool returnedExactly = true;   ///< every call returned the caller's count
        bool arraysUnchanged = true;   ///< every call left both arrays byte-identical
        bool everEngaged = false;      ///< isEngaged() must never latch
        bool everHadChildren = false;  ///< getLiveChildCount() must stay 0
        bool everNonFinite = false;    ///< stateFinite() must hold throughout
    };

    const auto runArm = [&](bool depthZeroBeforePrepare, std::size_t numChildSlots,
                            bool dormant) -> ArmResult {
        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillSyntheticParents(ratios, amplitudes, kParentCount);
        const std::array<float, BloomEngine::kMaxSlots> pristineRatios = ratios;
        const std::array<float, BloomEngine::kMaxSlots> pristineAmplitudes = amplitudes;

        BloomEngine engine;
        if (depthZeroBeforePrepare) {
            // BEFORE prepare(), so prepare()'s snapTo(depth_) lands the ramp on
            // exactly 0 and there is no 50 ms window in which the gate is open.
            engine.setDepth(0.0f);
        }
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                                       .numChildSlots = numChildSlots});
        engine.setDormant(dormant);

        ArmResult result;
        for (std::size_t c = 0; c < kChunks; ++c) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kParentCount) {
                result.returnedExactly = false;
            }
            // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            if (std::memcmp(ratios.data(), pristineRatios.data(), kBytes) != 0 ||
                std::memcmp(amplitudes.data(), pristineAmplitudes.data(), kBytes) != 0) {
                result.arraysUnchanged = false;
            }
            // NOLINTEND(bugprone-suspicious-memory-comparison)
            if (engine.isEngaged()) {
                result.everEngaged = true;
            }
            if (engine.getLiveChildCount() != 0u) {
                result.everHadChildren = true;
            }
            if (!engine.stateFinite()) {
                result.everNonFinite = true;
            }
        }
        return result;
    };

    SECTION("(a) depth 0 set BEFORE prepare: the smoothed depth never leaves 0") {
        // The PRECONDITION is CHECKED, not assumed (plan S8 addition A-6): if the
        // ramp were anywhere but exactly 0 the gate would be open and this arm
        // would be measuring luck.
        BloomEngine probe;
        probe.setDepth(0.0f);
        probe.prepare(kFs, BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                                      .numChildSlots = 8});
        REQUIRE(probe.getDepth() == 0.0f);
        REQUIRE(probe.getSmoothedDepth() == 0.0f);

        const ArmResult r = runArm(/*depthZeroBeforePrepare=*/true, /*numChildSlots=*/8,
                                   /*dormant=*/false);
        REQUIRE(r.returnedExactly);
        REQUIRE(r.arraysUnchanged);
        REQUIRE_FALSE(r.everEngaged);
        REQUIRE_FALSE(r.everHadChildren);
        REQUIRE_FALSE(r.everNonFinite);
    }

    SECTION("(b) numChildSlots == 0: an exact pass-through by configuration (FR-054)") {
        // reserveBase() == capacity(), so the engine owns no slot at all. Depth
        // is left at its 1.0 default and the internal clock at its default rate -
        // an event may well fire over 133 s, and it must still produce nothing.
        BloomEngine probe;
        probe.prepare(kFs, BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                                      .numChildSlots = 0});
        REQUIRE(probe.numChildSlots() == std::size_t{0});
        REQUIRE(probe.reserveBase() == probe.capacity());
        REQUIRE(probe.getSmoothedDepth() == 1.0f);  // depth is NOT what disables this arm

        const ArmResult r = runArm(/*depthZeroBeforePrepare=*/false, /*numChildSlots=*/0,
                                   /*dormant=*/false);
        REQUIRE(r.returnedExactly);
        REQUIRE(r.arraysUnchanged);
        REQUIRE_FALSE(r.everEngaged);
        REQUIRE_FALSE(r.everHadChildren);
        REQUIRE_FALSE(r.everNonFinite);
    }

    SECTION("(c) dormant: no new spawn, so nothing ever engages") {
        BloomEngine probe;
        probe.prepare(kFs, BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                                      .numChildSlots = 8});
        probe.setDormant(true);
        REQUIRE(probe.isDormant());

        const ArmResult r = runArm(/*depthZeroBeforePrepare=*/false, /*numChildSlots=*/8,
                                   /*dormant=*/true);
        REQUIRE(r.returnedExactly);
        REQUIRE(r.arraysUnchanged);
        REQUIRE_FALSE(r.everEngaged);
        REQUIRE_FALSE(r.everHadChildren);
        REQUIRE_FALSE(r.everNonFinite);
    }

    SECTION("(f) a disengaged call returns the caller's UNCLAMPED parentCount") {
        // THE ARM NO OTHER ARM CAN SEE. Every arm above runs with
        // parentCount <= capacity(), where the clamped and unclamped counts are
        // the same number, so an engine returning min(parentCount, capacity())
        // passes all of them. FR-050 tells a Phase-10 caller to set capacity from
        // HarmonicCloud::getActivePartialCount(), which is routinely BELOW the
        // number of live partials the voice supplies - so parentCount > capacity()
        // is the DEFAULT configuration, not abuse. A component specified to be
        // bit-identical to absent that returned 32 here would make the cloud pad
        // [32, 64) to amplitude 0: audible partial loss (plan S3.1).
        constexpr std::size_t kSmallCapacity = 32;
        constexpr std::size_t kFullParentCount = BloomEngine::kMaxSlots;
        constexpr std::size_t kShortRun = 2000;

        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillSyntheticParents(ratios, amplitudes, kFullParentCount);
        const std::array<float, BloomEngine::kMaxSlots> pristineRatios = ratios;
        const std::array<float, BloomEngine::kMaxSlots> pristineAmplitudes = amplitudes;

        BloomEngine engine;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = kSmallCapacity,
                                                       .numChildSlots = 8});
        engine.setDormant(true);  // disengaged, deterministically
        REQUIRE(engine.capacity() == kSmallCapacity);
        REQUIRE(kFullParentCount > engine.capacity());

        bool returnedUnclamped = true;
        bool arraysUnchanged = true;
        for (std::size_t c = 0; c < kShortRun; ++c) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kFullParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kFullParentCount) {
                returnedUnclamped = false;
            }
            // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            if (std::memcmp(ratios.data(), pristineRatios.data(), kBytes) != 0 ||
                std::memcmp(amplitudes.data(), pristineAmplitudes.data(), kBytes) != 0) {
                arraysUnchanged = false;
            }
            // NOLINTEND(bugprone-suspicious-memory-comparison)
        }
        INFO("capacity() == " << engine.capacity() << ", parentCount == " << kFullParentCount);
        REQUIRE(returnedUnclamped);
        REQUIRE(arraysUnchanged);
        REQUIRE_FALSE(engine.isEngaged());
    }

    // ==========================================================================
    // T010 arms (d), (e) and the in-flight 1 -> 0 arm
    // ==========================================================================
    // Arms (a)-(c) prove the gate SHUT. These three prove it is a CONTINUUM:
    // FR-035's wake and FR-042 (b)'s depth each scale the per-control-step spawn
    // probability LINEARLY, and neither of them touches a child already latched.
    //
    // WHY THE COUNTS ARE COMPARABLE ACROSS ARMS. The FR-041 clock draw is
    // UNCONDITIONAL (header S3.3 step (1)): clockRng_'s position after n control
    // steps is n draws, whatever the gate did. Two arms at the same seed and the
    // same step count therefore see the IDENTICAL uniform sequence u_i, and the
    // events of the `wake = w` arm are exactly `{i : u_i < w * p1}` - a SUBSET of
    // the `wake = 1` arm's. Conditioned on the observed N1 the smaller arm is
    // therefore Binomial(N1, w), whose standard deviation sqrt(N1*w*(1-w)) is
    // bounded by the 4*sqrt(w*N1) band the criterion states.
    //
    // WHY 8 kHz. p = rate * gate * 64 / fs is LARGEST at the lowest legal rate
    // (kMinUsableSampleRate): 4.0e-4 per control step at kMaxSpawnRateHz, six
    // times the 48 kHz figure. That is what makes a 500-event sample affordable
    // in an ordinary, non-[long] case.
    // ==========================================================================

    constexpr double kGateFs = BloomEngine::kMinUsableSampleRate;  // 125 control steps / s
    constexpr std::size_t kGateSteps = 1600000;                    // E[N1] ~ 640 events
    constexpr std::uint32_t kGateSeed = 0x7A4E0001u;
    constexpr std::size_t kGateCapacity = 16;
    constexpr std::size_t kGateChildSlots = 4;
    constexpr std::size_t kGateParents = 8;

    SECTION("(d) fractional wake scales the event rate linearly (FR-035)") {
        // `depth` stays at its 1.0 default, so `gate == wake` exactly and the
        // arm measures the wake limb of the product and nothing else.
        const auto countEvents = [&](float wake) -> double {
            std::array<float, BloomEngine::kMaxSlots> ratios{};
            std::array<float, BloomEngine::kMaxSlots> amplitudes{};
            fillSyntheticParents(ratios, amplitudes, kGateParents);

            BloomEngine engine;
            engine.setSeed(kGateSeed);
            engine.prepare(kGateFs, BloomEngine::PrepareConfig{
                                        .capacity = kGateCapacity,
                                        .numChildSlots = kGateChildSlots});
            engine.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
            engine.setWake(wake);
            static_cast<void>(
                stepEngine(engine, ratios.data(), amplitudes.data(), kGateParents, kGateSteps));
            return static_cast<double>(engine.getSpawnEventCount());
        };

        const double n1 = countEvents(1.0f);
        INFO("N1 (wake = 1) == " << n1 << " events in " << kGateSteps << " control steps");
        REQUIRE(n1 >= 500.0);
        // EXACTLY zero: setWake() snaps <= kWakeSilenceEpsilon to 0.0f, so
        // `gate == 0.0f` is an exact test and p is exactly 0.
        REQUIRE(countEvents(0.0f) == 0.0);

        constexpr std::array<float, 2> kWakeFractions{0.25f, 0.5f};
        for (const float w : kWakeFractions) {
            const double expected = static_cast<double>(w) * n1;
            const double observed = countEvents(w);
            INFO("wake = " << w << ": observed " << observed << ", expected " << expected);
            REQUIRE(std::fabs(observed - expected) <= 4.0 * std::sqrt(expected));
        }

        // ---- Clarification Q7: NO CATCH-UP BURST at a wake edge -------------
        // An event armed while the gate is shut is consumed and DISCARDED on
        // that same control step. An implementation that HELD it would spawn on
        // the first awake step - audibly, a silent patch that blooms the instant
        // the macro is touched - and no other arm can see the difference.
        std::array<float, BloomEngine::kMaxSlots> edgeRatios{};
        std::array<float, BloomEngine::kMaxSlots> edgeAmplitudes{};
        fillSyntheticParents(edgeRatios, edgeAmplitudes, kGateParents);

        BloomEngine edge;
        edge.setSeed(kGateSeed);
        edge.prepare(kFs, BloomEngine::PrepareConfig{.capacity = kGateCapacity,
                                                     .numChildSlots = kGateChildSlots});
        edge.setSpawnRateHz(0.0f);  // triggerBloom() is the ONLY source here
        edge.setWake(0.0f);
        REQUIRE(edge.getWakeAmount() == 0.0f);

        edge.triggerBloom();
        const std::size_t edgeReturn = edge.processChunk(
            edgeRatios.data(), edgeAmplitudes.data(), kGateParents,
            BloomEngine::kControlChunkSamples);
        REQUIRE(edgeReturn == kGateParents);  // never engaged, so the count passes through
        REQUIRE(edge.getDiscardedEventCount() == std::uint64_t{1});
        REQUIRE(edge.getSpawnEventCount() == std::uint64_t{0});

        edge.setWake(1.0f);
        static_cast<void>(stepEngine(edge, edgeRatios.data(), edgeAmplitudes.data(), kGateParents,
                                     1000));
        REQUIRE(edge.getSpawnEventCount() == std::uint64_t{0});
        REQUIRE(edge.getDiscardedEventCount() == std::uint64_t{1});
        REQUIRE(edge.getLiveChildCount() == std::size_t{0});
        REQUIRE_FALSE(edge.isEngaged());
    }

    SECTION("(e) fractional depth scales the event rate linearly (FR-042 (b))") {
        // The mirror of (d) on the OTHER limb of the product. FR-042 (b) has no
        // other criterion anywhere: SC-015 runs at a single constant depth, so
        // without this arm an engine that treated depth as a BOOLEAN gate - open
        // at any depth > 0 - would pass the whole suite.
        //
        // Depth is set BEFORE prepare(), so prepare()'s snapTo(depth_) lands the
        // 50 ms ramp exactly on the value and the gate is constant for the whole
        // run rather than for all-but-the-first-37-steps of it.
        const auto countEvents = [&](float depth) -> double {
            std::array<float, BloomEngine::kMaxSlots> ratios{};
            std::array<float, BloomEngine::kMaxSlots> amplitudes{};
            fillSyntheticParents(ratios, amplitudes, kGateParents);

            BloomEngine engine;
            engine.setSeed(kGateSeed);
            engine.setDepth(depth);
            engine.prepare(kGateFs, BloomEngine::PrepareConfig{
                                        .capacity = kGateCapacity,
                                        .numChildSlots = kGateChildSlots});
            REQUIRE(engine.getSmoothedDepth() == depth);  // the CHECKED precondition
            engine.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
            static_cast<void>(
                stepEngine(engine, ratios.data(), amplitudes.data(), kGateParents, kGateSteps));
            REQUIRE(engine.getSmoothedDepth() == depth);  // and it never drifted
            return static_cast<double>(engine.getSpawnEventCount());
        };

        const double n1 = countEvents(1.0f);
        INFO("N1 (depth = 1) == " << n1 << " events in " << kGateSteps << " control steps");
        REQUIRE(n1 >= 500.0);
        REQUIRE(countEvents(0.0f) == 0.0);

        constexpr std::array<float, 2> kDepthFractions{0.25f, 0.5f};
        for (const float d : kDepthFractions) {
            const double expected = static_cast<double>(d) * n1;
            const double observed = countEvents(d);
            INFO("depth = " << d << ": observed " << observed << ", expected " << expected);
            REQUIRE(std::fabs(observed - expected) <= 4.0 * std::sqrt(expected));
        }
    }

    SECTION("an in-flight depth 1 -> 0 never rescales a LIVE child (FR-042 (a))") {
        // FR-042 (a) scales the amplitude LATCHED AT SPAWN. A live child is a
        // 45-second swell already in the caller's spectrum; rescaling it when
        // the macro moves is a level step in the middle of a fade - exactly the
        // click FR-031 exists to prevent - and the engine must instead let it
        // run its latched lifecycle out.
        constexpr std::size_t kInflightCapacity = 32;
        constexpr std::size_t kInflightChildSlots = 8;
        constexpr std::size_t kInflightParents = 16;
        constexpr std::size_t kInflightGuard = 20000;

        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillSyntheticParents(ratios, amplitudes, kInflightParents);

        BloomEngine engine;
        engine.setSeed(0x1F1E0001u);
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = kInflightCapacity,
                                                       .numChildSlots = kInflightChildSlots});
        engine.setSpawnRateHz(0.0f);  // triggerBloom() is the only source
        engine.setFadeInSeconds(1.0f);
        engine.setHoldSeconds(0.0f);
        engine.setFadeOutSeconds(1.0f);
        REQUIRE(engine.getDepth() == 1.0f);
        REQUIRE(engine.getSmoothedDepth() == 1.0f);

        // The synthetic spectrum is EXACTLY harmonic, so FR-022's 24-cent rule
        // refuses every octave and several fifths of the strongest parents; the
        // loop therefore offers events until one child is placed rather than
        // assuming the first event places one.
        for (std::size_t e = 0; e < 32u && engine.getLiveChildCount() == 0u; ++e) {
            engine.triggerBloom();
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kInflightParents,
                                    BloomEngine::kControlChunkSamples);
            REQUIRE(returned == (engine.isEngaged() ? kInflightCapacity : kInflightParents));
        }
        REQUIRE(engine.getLiveChildCount() > std::size_t{0});
        REQUIRE(engine.isEngaged());

        // The latched targets, read BEFORE the macro moves. No further event can
        // fire (rate 0, no trigger), so a table entry can only go live -> Idle:
        // a still-live entry is necessarily the SAME child.
        std::array<float, BloomEngine::kMaxChildren> latchedTargets{};
        std::array<bool, BloomEngine::kMaxChildren> wasLive{};
        for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
            wasLive[i] = engine.getChildPhase(i) != BloomEngine::Phase::Idle;
            latchedTargets[i] = engine.getChildTargetAmplitude(i);
        }

        engine.setDepth(0.0f);
        REQUIRE(engine.getDepth() == 0.0f);

        bool targetsHeld = true;
        bool engagedHeld = true;
        bool returnHeld = true;
        std::size_t steps = 0;
        while (engine.getLiveChildCount() > 0u && steps < kInflightGuard) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kInflightParents,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kInflightCapacity) {
                returnHeld = false;
            }
            if (!engine.isEngaged()) {
                engagedHeld = false;
            }
            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                if (wasLive[i] && engine.getChildPhase(i) != BloomEngine::Phase::Idle &&
                    engine.getChildTargetAmplitude(i) != latchedTargets[i]) {
                    targetsHeld = false;
                }
            }
            ++steps;
        }
        INFO("steps to drain = " << steps);
        REQUIRE(steps < kInflightGuard);
        REQUIRE(targetsHeld);
        REQUIRE(engagedHeld);
        REQUIRE(returnHeld);
        REQUIRE(engine.getSmoothedDepth() == 0.0f);
        REQUIRE(engine.getLiveChildCount() == std::size_t{0});
        REQUIRE(engine.isEngaged());  // STICKY (Clarification Q8)
    }
}

// ==============================================================================
// The T014 fixtures (plan S10.1: NO new test helper header)
// ==============================================================================
// Coded ONCE, here, by T014 - the first behaviour-TU task that needs CloudRig
// (tasks.md, "The four shared fixtures" table). They sit BEFORE
// BloomEngine_BlockPartitionInvariance because T014 EXTENDS that case rather
// than duplicating it, so its new sections need them in scope.
//
// The two parent-spectrum fillers this TU already owns are reused unchanged:
// fillSyntheticParents (above) for the partition arms, and fillWellSpacedParents
// (declared further down, with the cases that first used it) for the two new
// cases at the end of the file. CloudRig therefore does NOT fill the parent
// arrays itself - the caller does, and sets `parentCount` - which is what lets
// one rig serve both spectra without a third filler being invented.
// ==============================================================================
namespace {

constexpr double kDetSampleRate = 48000.0;
constexpr std::size_t kDetSlots = Krate::DSP::BloomEngine::kMaxSlots;

/// The control rate the whole T014 group reasons in: fs / 64 = 750 Hz at 48 kHz.
constexpr float kDetControlRateHz =
    static_cast<float>(kDetSampleRate) /
    static_cast<float>(Krate::DSP::BloomEngine::kControlChunkSamples);

/// A fundamental that puts the whole rendered bank inside the audio band: the
/// largest ratio this TU's spectra reach is 4.4 (an octave of the 2.2 parent),
/// so the highest rendered partial sits near 484 Hz.
constexpr float kDetFundamentalHz = 110.0f;

/// @brief SC-008 (c)'s comparison window, WRITTEN AS THE EXPRESSION THE
///        CRITERION STATES: ceil(kGainRampMs * controlRateHz / 1000).
///
/// 50 ms at the 48 kHz control rate is 37.5 control steps, so this returns 38 -
/// but the number is DERIVED here rather than typed, exactly as spec SC-008 (c)
/// and plan S14 C-1 require ("stated in the criterion as that expression, so an
/// implementer cannot substitute a lucky seed for the window").
///
/// std::ceil is not usable in a constant expression in C++20, so the ceiling is
/// taken with integer arithmetic over the exact double product.
[[nodiscard]] constexpr std::size_t rampSettlingSteps(float controlRateHz) noexcept {
    const double exact = static_cast<double>(Krate::DSP::BloomEngine::kGainRampMs) *
                         static_cast<double>(controlRateHz) / 1000.0;
    const auto floored = static_cast<std::size_t>(exact);
    return (static_cast<double>(floored) < exact) ? (floored + std::size_t{1}) : floored;
}

/// @brief One row of the FR-034 lifecycle table, read through the PUBLIC surface.
///
/// The first five fields are SC-008's "integer/enum surface" verbatim: slot,
/// phase, relation, fallback flag, and the two latched step counts. The step
/// counts are exposed only in seconds (getChildElapsedSeconds,
/// getChildHoldSeconds), and both are `static_cast<float>(integer) *
/// controlDtSec_` with a controlDtSec_ identical across every instance compared
/// here - so `==` on them IS an exact integer comparison wearing a float's
/// clothes, not a float golden.
///
/// `ratio` and `target` are carried too and compared exactly, as a
/// STRENGTHENING beyond the criterion's integer/enum floor: two same-seed runs
/// of the SAME binary over the SAME input execute the same arithmetic, so any
/// difference there is a determinism defect rather than cross-toolchain spread.
/// This is a within-run structural identity, never a checked-in bit pattern -
/// tools/lint-float-bit-goldens.js governs the latter and nothing in this TU
/// stores one; every cross-run render claim goes through compareFingerprints.
struct DetChildRow {
    std::size_t slot = Krate::DSP::BloomEngine::kMaxSlots;
    std::size_t parentIndex = 0;
    Krate::DSP::BloomEngine::Phase phase = Krate::DSP::BloomEngine::Phase::Idle;
    Krate::DSP::BloomEngine::Relation relation = Krate::DSP::BloomEngine::Relation::Octave;
    bool fallback = false;
    float elapsedSeconds = 0.0f;
    float holdSeconds = 0.0f;
    float ratio = 0.0f;
    float target = 0.0f;
};

using DetChildTable = std::array<DetChildRow, Krate::DSP::BloomEngine::kMaxChildren>;

/// @brief Snapshot the whole FR-034 table through the FR-061 read surface.
[[nodiscard]] DetChildTable captureChildTable(const Krate::DSP::BloomEngine& engine) noexcept {
    DetChildTable out{};
    for (std::size_t i = 0; i < Krate::DSP::BloomEngine::kMaxChildren; ++i) {
        out[i].slot = engine.getChildSlotIndex(i);
        out[i].parentIndex = engine.getChildParentIndex(i);
        out[i].phase = engine.getChildPhase(i);
        out[i].relation = engine.getChildRelation(i);
        out[i].fallback = engine.getIsChildFallback(i);
        out[i].elapsedSeconds = engine.getChildElapsedSeconds(i);
        out[i].holdSeconds = engine.getChildHoldSeconds(i);
        out[i].ratio = engine.getChildRatio(i);
        out[i].target = engine.getChildTargetAmplitude(i);
    }
    return out;
}

[[nodiscard]] bool sameChildRow(const DetChildRow& a, const DetChildRow& b) noexcept {
    return a.slot == b.slot && a.parentIndex == b.parentIndex && a.phase == b.phase &&
           a.relation == b.relation && a.fallback == b.fallback &&
           a.elapsedSeconds == b.elapsedSeconds && a.holdSeconds == b.holdSeconds &&
           a.ratio == b.ratio && a.target == b.target;
}

/// @brief The index of the first differing row, or kMaxChildren when the two
///        tables are identical - so a failure reports WHICH child moved.
[[nodiscard]] std::size_t firstChildTableDifference(const DetChildTable& a,
                                                    const DetChildTable& b) noexcept {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sameChildRow(a[i], b[i])) {
            return i;
        }
    }
    return Krate::DSP::BloomEngine::kMaxChildren;
}

[[nodiscard]] bool sameChildTable(const DetChildTable& a, const DetChildTable& b) noexcept {
    return firstChildTableDifference(a, b) == Krate::DSP::BloomEngine::kMaxChildren;
}

/// @brief Every cumulative counter of the FR-061 read surface, in one snapshot.
struct DetCounters {
    std::uint64_t events = 0;
    std::uint64_t discarded = 0;
    std::uint64_t scans = 0;
    std::uint64_t offered = 0;
    std::uint64_t spawned = 0;
    std::uint64_t refused = 0;
    std::uint64_t fallback = 0;
    std::uint64_t completed = 0;
    std::uint64_t rejected = 0;
    std::uint32_t overlap = 0;
    std::size_t live = 0;
    bool engaged = false;
};

[[nodiscard]] DetCounters captureCounters(const Krate::DSP::BloomEngine& engine) noexcept {
    DetCounters c;
    c.events = engine.getSpawnEventCount();
    c.discarded = engine.getDiscardedEventCount();
    c.scans = engine.getParentScanCount();
    c.offered = engine.getOfferedChildCount();
    c.spawned = engine.getSpawnedChildCount();
    c.refused = engine.getRefusedChildCount();
    c.fallback = engine.getFallbackChildCount();
    c.completed = engine.getCompletedChildCount();
    c.rejected = engine.getRejectedSpawnCount();
    c.overlap = engine.getOverlapEngagementCount();
    c.live = engine.getLiveChildCount();
    c.engaged = engine.isEngaged();
    return c;
}

[[nodiscard]] bool sameCounters(const DetCounters& a, const DetCounters& b) noexcept {
    return a.events == b.events && a.discarded == b.discarded && a.scans == b.scans &&
           a.offered == b.offered && a.spawned == b.spawned && a.refused == b.refused &&
           a.fallback == b.fallback && a.completed == b.completed && a.rejected == b.rejected &&
           a.overlap == b.overlap && a.live == b.live && a.engaged == b.engaged;
}

/// @brief The DELTA of every cumulative counter across a window, so an arm whose
///        PREFIX legitimately differs (SC-008 (c) compares four instances that
///        spent their prefix under four different suppression levers, and the
///        always-awake instance of (c)(ii) spawned all through its own) can
///        still compare the window itself exactly.
[[nodiscard]] DetCounters counterDelta(const DetCounters& now, const DetCounters& base) noexcept {
    DetCounters d;
    d.events = now.events - base.events;
    d.discarded = now.discarded - base.discarded;
    d.scans = now.scans - base.scans;
    d.offered = now.offered - base.offered;
    d.spawned = now.spawned - base.spawned;
    d.refused = now.refused - base.refused;
    d.fallback = now.fallback - base.fallback;
    d.completed = now.completed - base.completed;
    d.rejected = now.rejected - base.rejected;
    d.overlap = now.overlap - base.overlap;
    d.live = now.live;        // a LEVEL, not a cumulative count - carried as is
    d.engaged = now.engaged;  // the STICKY latch - a level too
    return d;
}

/// @brief stepEngine's sibling: run `steps` control steps and record the
///        ABSOLUTE control-step index of every step on which an event EXECUTED.
///
/// The transition is read from getSpawnEventCount(), incremented once per
/// executed event at the head of runEvent - SC-008 (c)(ii) names exactly this
/// observable. A DISCARDED event does not move it (plan S8 addition A-1), which
/// is what makes the recorded list "the steps this instance actually spawned on"
/// rather than "the steps its clock fired on".
///
/// @return processChunk's last return - bound, never discarded ([[nodiscard]]).
std::size_t stepEngineRecordingEvents(Krate::DSP::BloomEngine& engine, float* ratios,
                                      float* amplitudes, std::size_t parentCount,
                                      std::size_t steps, std::size_t firstStepIndex,
                                      std::vector<std::size_t>& eventSteps) {
    std::size_t returned = parentCount;
    std::uint64_t previous = engine.getSpawnEventCount();
    for (std::size_t s = 0; s < steps; ++s) {
        returned = engine.processChunk(ratios, amplitudes, parentCount,
                                       Krate::DSP::BloomEngine::kControlChunkSamples);
        const std::uint64_t now = engine.getSpawnEventCount();
        if (now != previous) {
            eventSteps.push_back(firstStepIndex + s);
            previous = now;
        }
    }
    return returned;
}

/// @brief HarmonicCloud + BloomEngine at the PHASE-10 CALL SHAPE
///        (seraphis_voice.h:1050-1054), one 64-sample control chunk per call.
///
/// This is the same fixture the spectral TU codes for T013, with ONE deliberate
/// difference: it does NOT fill the parent arrays. T014's cases drive two
/// different spectra (fillSyntheticParents for the partition arms,
/// fillWellSpacedParents for the determinism arms) and both fillers are already
/// owned by this TU, so the rig takes the arrays as caller state instead of
/// growing a third filler.
///
/// The <= 64-sample slice is a BOUND, not a suggestion (harmonic_cloud.h:735-751):
/// processStereoBlock restarts its internal control grid on every call, so a
/// target supplied once per host block would be frozen for all of that block's
/// internal chunks.
///
/// THE CLOUD RUNS AT richness = 1.0 SO activeCount_ == 64: recalculateAmplitudes()
/// zeroes baseAmplitude_[i] and `continue`s for every i >= activeCount_ BEFORE
/// the spectral-target branch (harmonic_cloud.h:1469-1473), so a child written at
/// or above the active count would be silently inaudible and the render would be
/// pinning silence. `capacity == kMaxSlots` with `activeCount_ == 64` is the
/// FR-050 capacity contract satisfied exactly.
///
/// `seed` drives BOTH the engine stream and the cloud's own drift / pan / phase
/// draws, so a determinism claim made through this rig covers the whole chain a
/// Phase-10 caller assembles, not the engine alone.
struct CloudRig {
    Krate::DSP::HarmonicCloud cloud;
    Krate::DSP::BloomEngine bloom;
    std::array<float, kDetSlots> ratios{};
    std::array<float, kDetSlots> amplitudes{};
    std::size_t parentCount = 1;

    /// The FR-041 internal clock is left OFF here: every case that uses this rig
    /// drives events with triggerBloom() at KNOWN control steps, so a chance
    /// clock spawn cannot make a failure irreproducible. A caller that wants the
    /// clock turns it back on itself.
    void prepare(std::uint32_t seed, std::size_t capacity, std::size_t numChildSlots) noexcept {
        cloud.prepare(kDetSampleRate);
        cloud.setFundamentalHz(kDetFundamentalHz);
        cloud.setRichness(1.0f);
        cloud.setSeed(seed);
        // noteOn() flushes the deferred amplitude recompute while quiescent
        // (harmonic_cloud.h:635-660), which is what makes activeCount_ readable
        // as 64 before the first render rather than one chunk later.
        cloud.noteOn();

        bloom.setSeed(seed);
        bloom.prepare(kDetSampleRate,
                      Krate::DSP::BloomEngine::PrepareConfig{.capacity = capacity,
                                                             .numChildSlots = numChildSlots});
        bloom.setSpawnRateHz(0.0f);
    }

    /// @brief One 64-sample control chunk: bloom -> setSpectralTarget -> render.
    /// @return The count handed to setSpectralTarget. processChunk is
    ///         [[nodiscard]], so the return is BOUND and never discarded
    ///         (C4834 / -Wunused-result under the zero-warning rule).
    std::size_t chunk(float* left, float* right) noexcept {
        const std::size_t returned =
            bloom.processChunk(ratios.data(), amplitudes.data(), parentCount,
                               Krate::DSP::BloomEngine::kControlChunkSamples);
        cloud.setSpectralTarget(ratios.data(), amplitudes.data(), returned);
        cloud.processStereoBlock(left, right, Krate::DSP::BloomEngine::kControlChunkSamples);
        return returned;
    }
};

/// @brief A stereo render reduced the way SC-008 pins it: through
///        render_fingerprint.h's MEASURED tolerances, never a bit pattern.
struct DetRenderFingerprint {
    Krate::DSP::TestUtils::RenderFingerprint left;
    Krate::DSP::TestUtils::RenderFingerprint right;
};

[[nodiscard]] DetRenderFingerprint fingerprintStereo(const std::vector<float>& left,
                                                     const std::vector<float>& right) {
    DetRenderFingerprint fp;
    fp.left = Krate::DSP::TestUtils::fingerprintRender(left);
    fp.right = Krate::DSP::TestUtils::fingerprintRender(right);
    return fp;
}

}  // namespace

// ==============================================================================
// T007 case 2 - BloomEngine_BlockPartitionInvariance (SC-008 (b), FR-005/FR-006)
//              EXTENDED by T014 with the child-table, render and rejected-call
//              arms of SC-008 (b) (the two sections at the end of the case).
// ==============================================================================
// FR-006: internal state after N advanced samples is a function of N ALONE and
// never of how N was partitioned into chunks (the entropy_processor.h:257-259
// wording this component inherits).
//
// THE OBSERVABLE IS THE DEPTH RAMP, NOT A CHILD. At T007 no child can exist, and
// even later a child is a rare event a 1 600-sample run would almost never
// contain - so the ramp is the only per-control-step quantity whose value is
// both continuous and reachable this early. It is a GOOD observable precisely
// because LinearRamp advances by a fixed increment per control step: an engine
// running a BLOCK-RELATIVE grid (numSamples / 64 steps per call, phase reset at
// every call) executes 25 steps for {64x25} but 3 for {512,512,512,64} and 0 for
// the ragged {1,7,383,1209}, so the ramp positions differ grossly.
//
// SATURATION WOULD HIDE THE BUG, so the run is sized to avoid it: the 50 ms ramp
// at the 48 kHz control rate (750 Hz) completes in 0.050 * 750 = 37.5 control
// steps, and the run is 1 600 samples = 25 steps. The ramp is still in flight at
// the end of every partition, so a step-count difference IS a value difference.
//
// The internal clock is disabled outright (setSpawnRateHz(0)) so the comparison
// cannot be perturbed by a chance spawn: the clause under test here is the GRID,
// and leaving a stochastic event in the run would make the case flaky for a
// reason that has nothing to do with partitioning.
// ==============================================================================
TEST_CASE("BloomEngine_BlockPartitionInvariance", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kParentCount = 24;
    constexpr std::size_t kTotalSamples = 1600;  // 25 control steps; the ramp needs 37.5

    // Returns the smoothed depth after `count` chunks whose sizes sum to
    // kTotalSamples. With `interleaveRejects`, three REJECTED calls are issued
    // before every accepted one (FR-005: a rejected call advances NOTHING).
    const auto runDepthRamp = [&](const std::size_t* sizes, std::size_t count,
                                  bool interleaveRejects) -> float {
        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillSyntheticParents(ratios, amplitudes, kParentCount);

        BloomEngine engine;
        engine.setDepth(0.0f);  // before prepare(): the ramp starts at exactly 0
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                                       .numChildSlots = 8});
        engine.setSpawnRateHz(0.0f);  // the internal clock is off; only the ramp moves
        REQUIRE(engine.getSmoothedDepth() == 0.0f);
        engine.setDepth(1.0f);  // the 50 ms ramp is now in flight

        std::size_t total = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (interleaveRejects) {
                // (i) null ratios, (ii) null amplitudes, (iii) numSamples == 0.
                // All three must leave getSmoothedDepth() untouched, and the two
                // null calls must return the caller's UNCLAMPED parentCount.
                const std::size_t r1 =
                    engine.processChunk(nullptr, amplitudes.data(), kParentCount, 512);
                const std::size_t r2 =
                    engine.processChunk(ratios.data(), nullptr, kParentCount, 512);
                const std::size_t r3 =
                    engine.processChunk(ratios.data(), amplitudes.data(), kParentCount, 0);
                REQUIRE(r1 == kParentCount);
                REQUIRE(r2 == kParentCount);
                REQUIRE(r3 == kParentCount);
            }
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount, sizes[i]);
            REQUIRE(returned == kParentCount);  // disengaged: the caller's own count
            total += sizes[i];
        }
        REQUIRE(total == kTotalSamples);
        return engine.getSmoothedDepth();
    };

    std::array<std::size_t, 25> uniform{};
    uniform.fill(BloomEngine::kControlChunkSamples);
    constexpr std::array<std::size_t, 4> kCoarse{512, 512, 512, 64};
    constexpr std::array<std::size_t, 1> kSingle{1600};
    constexpr std::array<std::size_t, 4> kRagged{1, 7, 383, 1209};

    SECTION("FR-006: the depth ramp is bitwise identical across four partitions") {
        const float fromUniform = runDepthRamp(uniform.data(), uniform.size(), false);
        const float fromCoarse = runDepthRamp(kCoarse.data(), kCoarse.size(), false);
        const float fromSingle = runDepthRamp(kSingle.data(), kSingle.size(), false);
        const float fromRagged = runDepthRamp(kRagged.data(), kRagged.size(), false);

        INFO("uniform {64x25} -> " << fromUniform);
        INFO("coarse  {512,512,512,64} -> " << fromCoarse);
        INFO("single  {1600} -> " << fromSingle);
        INFO("ragged  {1,7,383,1209} -> " << fromRagged);

        // The ramp must actually have MOVED and must NOT have saturated, or the
        // three equalities below would be satisfied by 0.0f or by 1.0f and would
        // prove nothing about the grid.
        REQUIRE(fromUniform > 0.0f);
        REQUIRE(fromUniform < 1.0f);

        // BITWISE identity - 25 identical control steps produce 25 identical
        // increments regardless of how the 1 600 samples were handed over.
        REQUIRE(fromCoarse == fromUniform);
        REQUIRE(fromSingle == fromUniform);
        REQUIRE(fromRagged == fromUniform);
    }

    SECTION("FR-005: rejected calls advance nothing and return the unclamped count") {
        const float reference = runDepthRamp(kRagged.data(), kRagged.size(), false);
        const float interleaved = runDepthRamp(kRagged.data(), kRagged.size(), true);
        INFO("ragged reference -> " << reference << ", with rejected calls -> " << interleaved);
        REQUIRE(interleaved == reference);

        // The unclamped-return clause, made NON-VACUOUS: with parentCount above
        // capacity() a null call that returned min(parentCount, capacity()) would
        // hand the caller a truncated count.
        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillSyntheticParents(ratios, amplitudes, BloomEngine::kMaxSlots);

        BloomEngine engine;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = 32, .numChildSlots = 8});
        const std::size_t nullRatios =
            engine.processChunk(nullptr, amplitudes.data(), BloomEngine::kMaxSlots, 512);
        const std::size_t nullAmplitudes =
            engine.processChunk(ratios.data(), nullptr, BloomEngine::kMaxSlots, 512);
        REQUIRE(nullRatios == BloomEngine::kMaxSlots);
        REQUIRE(nullAmplitudes == BloomEngine::kMaxSlots);
        // ...and neither advanced the grid: the ramp is still exactly where
        // prepare() snapped it.
        REQUIRE(engine.getSmoothedDepth() == engine.getDepth());
    }

    // =========================================================================
    // T014 extension - SC-008 (b) at full strength: the CHILD TABLE and a RENDER
    // =========================================================================
    // The two sections above prove the GRID is partition-invariant through the
    // depth ramp, which is all T007 could reach. SC-008 (b) asks for more: the
    // same total sample count delivered as 64, 512, 2048 and a ragged sequence
    // must leave an IDENTICAL CHILD TABLE and a FINGERPRINT-EQUAL output.
    //
    // HOW A SPAWN IS MADE PARTITION-INDEPENDENT. The FR-041 clock is off (a
    // stochastic event would land on a different control step in each scheme
    // only by luck, and at the 0.05 Hz ceiling a one-second run would usually
    // contain none at all). Instead the run is cut into kSegments SEGMENTS of
    // kSegmentSamples each, and one event is armed with triggerBloom() at every
    // segment boundary. Every scheme partitions the SAME segment, so every
    // scheme arms on the SAME control step - triggerBloom() is edge-like
    // (FR-043), so the arm is consumed by the segment's first control step in
    // all four. The partitions differ WITHIN a segment, which is exactly where
    // FR-006 lives.
    //
    // WHY THE RENDER IS A TAIL, NOT THE RUN ITSELF. HarmonicCloud must be fed at
    // most 64 samples per call (harmonic_cloud.h:735-751), so a scheme that
    // hands the ENGINE 2 048 samples at a time cannot also hand the CLOUD the
    // engine's intermediate 64-sample states - they do not exist in the arrays,
    // which only ever hold the post-call value. Rendering during the partition
    // phase would therefore differ between schemes for a reason FR-006 does not
    // claim anything about. FR-006's claim is about INTERNAL STATE AFTER N
    // SAMPLES, so the render is taken AFTER the partitioned phase, identically
    // in every scheme (kTailChunks calls of exactly 64 samples): the tail is a
    // state amplifier, and it is fingerprint-equal if and only if the state the
    // four schemes arrived at is the same one.
    // =========================================================================

    // One partitioned run: its table straight after the partition phase, its
    // table and counters after the common tail, and the tail's fingerprint.
    struct PartitionRun {
        DetChildTable tableAfterPartition{};
        DetChildTable tableAfterTail{};
        DetCounters counters{};
        DetRenderFingerprint fingerprint{};
        std::size_t liveAtTailStart = 0;
        bool rejectedReturnsUnclamped = true;
        bool elapsedUnchangedAcrossZero = true;
    };

    constexpr std::uint32_t kPartitionSeed = 0x0B100B01u;
    constexpr std::size_t kSegmentSamples = 6144u;  // 96 control steps = 128 ms at 48 kHz
    constexpr std::size_t kSegments = 8u;           // 1.024 s of trigger-driven bloom
    constexpr std::size_t kTailChunks = 375u;       // 0.5 s of CloudRig render
    constexpr std::size_t kTailSamples = kTailChunks * BloomEngine::kControlChunkSamples;

    const auto runSegmented = [&](const std::size_t* sizes, std::size_t count,
                                  bool pollute) -> PartitionRun {
        PartitionRun out;
        CloudRig rig;
        rig.prepare(kPartitionSeed, BloomEngine::kMaxSlots, 8u);
        rig.parentCount = kParentCount;
        fillSyntheticParents(rig.ratios, rig.amplitudes, kParentCount);
        // Compressed against the 45 / 120 / 180 s default so a child is born,
        // holds and dies INSIDE this run. Nothing in FR-006 depends on the
        // lifecycle length, and a default-length child would leave every scheme
        // in the same FadeIn phase - the weakest possible table to compare.
        rig.bloom.setFadeInSeconds(0.3f);
        rig.bloom.setHoldSeconds(0.1f);
        rig.bloom.setFadeOutSeconds(0.3f);

        std::size_t lastReturned = 0;
        for (std::size_t seg = 0; seg < kSegments; ++seg) {
            rig.bloom.triggerBloom();
            std::size_t total = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (pollute) {
                    // FR-005's three rejected forms, issued BEFORE every accepted
                    // call: (i) null ratios, (ii) null amplitudes, (iii)
                    // numSamples == 0. None of them may advance the grid, and the
                    // two null forms must return the caller's UNCLAMPED count.
                    // Captured into a bool rather than REQUIREd in the loop -
                    // this runs thousands of times per scheme.
                    const std::size_t r1 = rig.bloom.processChunk(
                        nullptr, rig.amplitudes.data(), kParentCount, 512);
                    const std::size_t r2 =
                        rig.bloom.processChunk(rig.ratios.data(), nullptr, kParentCount, 512);
                    out.rejectedReturnsUnclamped =
                        out.rejectedReturnsUnclamped && (r1 == kParentCount) &&
                        (r2 == kParentCount);

                    // SC-008 (b)'s added clause: getChildElapsedSeconds(i) is a
                    // function of the LATCHED STEP COUNT, so a numSamples == 0
                    // call - which APPLIES the current state without advancing -
                    // must leave every one of them untouched.
                    std::array<float, BloomEngine::kMaxChildren> before{};
                    for (std::size_t t = 0; t < BloomEngine::kMaxChildren; ++t) {
                        before[t] = rig.bloom.getChildElapsedSeconds(t);
                    }
                    // numSamples == 0 is NOT a rejected call: it APPLIES the
                    // current state without advancing, so it returns what an
                    // engaged call returns (capacity()) and what a disengaged
                    // one returns (the caller's unclamped count) respectively.
                    const bool engagedBeforeZero = rig.bloom.isEngaged();
                    const std::size_t r3 = rig.bloom.processChunk(
                        rig.ratios.data(), rig.amplitudes.data(), kParentCount, 0);
                    const std::size_t expectedR3 =
                        engagedBeforeZero ? rig.bloom.capacity() : kParentCount;
                    out.rejectedReturnsUnclamped =
                        out.rejectedReturnsUnclamped && (r3 == expectedR3);
                    for (std::size_t t = 0; t < BloomEngine::kMaxChildren; ++t) {
                        out.elapsedUnchangedAcrossZero =
                            out.elapsedUnchangedAcrossZero &&
                            (rig.bloom.getChildElapsedSeconds(t) == before[t]);
                    }
                }
                lastReturned = rig.bloom.processChunk(rig.ratios.data(), rig.amplitudes.data(),
                                                      kParentCount, sizes[i]);
                total += sizes[i];
            }
            REQUIRE(total == kSegmentSamples);
        }
        REQUIRE(lastReturned > 0u);

        out.tableAfterPartition = captureChildTable(rig.bloom);
        out.liveAtTailStart = rig.bloom.getLiveChildCount();

        std::vector<float> left(kTailSamples, 0.0f);
        std::vector<float> right(kTailSamples, 0.0f);
        for (std::size_t c = 0; c < kTailChunks; ++c) {
            const std::size_t offset = c * BloomEngine::kControlChunkSamples;
            lastReturned = rig.chunk(&left[offset], &right[offset]);
        }
        REQUIRE(lastReturned > 0u);

        out.tableAfterTail = captureChildTable(rig.bloom);
        out.counters = captureCounters(rig.bloom);
        out.fingerprint = fingerprintStereo(left, right);
        return out;
    };

    // The four partitions of ONE 6 144-sample segment. The ragged sequence is
    // SC-008 (b)'s own {1, 7, 383, 4096, ...}, completed so it sums to the
    // segment: 1 + 7 + 383 + 4096 + 1657 = 6144.
    std::array<std::size_t, 96> segUniform{};
    segUniform.fill(BloomEngine::kControlChunkSamples);
    std::array<std::size_t, 12> segCoarse{};
    segCoarse.fill(512u);
    std::array<std::size_t, 3> segBig{};
    segBig.fill(2048u);
    constexpr std::array<std::size_t, 5> kSegRagged{1u, 7u, 383u, 4096u, 1657u};
    static_assert(1u + 7u + 383u + 4096u + 1657u == kSegmentSamples,
                  "the ragged partition must span exactly one segment");

    SECTION("SC-008 (b): child table, counters and render are identical across four partitions") {
        const PartitionRun uniformRun = runSegmented(segUniform.data(), segUniform.size(), false);
        const PartitionRun coarseRun = runSegmented(segCoarse.data(), segCoarse.size(), false);
        const PartitionRun bigRun = runSegmented(segBig.data(), segBig.size(), false);
        const PartitionRun raggedRun = runSegmented(kSegRagged.data(), kSegRagged.size(), false);

        // NON-VACUITY, asserted before any equality: a run in which nothing ever
        // spawned, nothing was live at the tail and the render was silent would
        // satisfy every comparison below while measuring nothing at all.
        INFO("spawned=" << uniformRun.counters.spawned
                        << " events=" << uniformRun.counters.events
                        << " liveAtTailStart=" << uniformRun.liveAtTailStart
                        << " tailRmsL=" << uniformRun.fingerprint.left.rms);
        REQUIRE(uniformRun.counters.engaged);
        REQUIRE(uniformRun.counters.spawned > 0u);
        REQUIRE(uniformRun.counters.events == kSegments);
        REQUIRE(uniformRun.liveAtTailStart > 0u);
        REQUIRE(uniformRun.fingerprint.left.rms > 0.0);
        REQUIRE(uniformRun.fingerprint.right.rms > 0.0);

        const auto compareAgainstUniform = [&](const char* name, const PartitionRun& other) {
            INFO("partition scheme: " << name);
            INFO("first differing table row (after partition phase): "
                 << firstChildTableDifference(other.tableAfterPartition,
                                              uniformRun.tableAfterPartition));
            REQUIRE(sameChildTable(other.tableAfterPartition, uniformRun.tableAfterPartition));
            INFO("first differing table row (after tail): "
                 << firstChildTableDifference(other.tableAfterTail, uniformRun.tableAfterTail));
            REQUIRE(sameChildTable(other.tableAfterTail, uniformRun.tableAfterTail));
            REQUIRE(sameCounters(other.counters, uniformRun.counters));
            REQUIRE(other.liveAtTailStart == uniformRun.liveAtTailStart);

            const auto leftCmp = Krate::DSP::TestUtils::compareFingerprints(
                other.fingerprint.left, uniformRun.fingerprint.left);
            const auto rightCmp = Krate::DSP::TestUtils::compareFingerprints(
                other.fingerprint.right, uniformRun.fingerprint.right);
            INFO("left  " << leftCmp.detail << " worstMetric=" << leftCmp.worstMetricRelativeError
                          << " worstSample=" << leftCmp.worstSampleError);
            INFO("right " << rightCmp.detail << " worstMetric=" << rightCmp.worstMetricRelativeError
                          << " worstSample=" << rightCmp.worstSampleError);
            REQUIRE(leftCmp.withinTolerance());
            REQUIRE(rightCmp.withinTolerance());
        };

        compareAgainstUniform("12 x 512", coarseRun);
        compareAgainstUniform("3 x 2048", bigRun);
        compareAgainstUniform("ragged {1,7,383,4096,1657}", raggedRun);
    }

    SECTION("SC-008 (b) / FR-005: rejected calls move neither the table nor the render") {
        const PartitionRun clean = runSegmented(kSegRagged.data(), kSegRagged.size(), false);
        const PartitionRun polluted = runSegmented(kSegRagged.data(), kSegRagged.size(), true);

        // The two clauses the pollution exists to test, and which the unpolluted
        // partitions structurally cannot see because they never issue a rejected
        // call: the unclamped return on both nullptr paths, and the frozen
        // elapsed clocks across a numSamples == 0 call.
        REQUIRE(polluted.rejectedReturnsUnclamped);
        REQUIRE(polluted.elapsedUnchangedAcrossZero);

        INFO("first differing table row (after partition phase): "
             << firstChildTableDifference(polluted.tableAfterPartition,
                                          clean.tableAfterPartition));
        REQUIRE(sameChildTable(polluted.tableAfterPartition, clean.tableAfterPartition));
        REQUIRE(sameChildTable(polluted.tableAfterTail, clean.tableAfterTail));
        REQUIRE(sameCounters(polluted.counters, clean.counters));

        const auto leftCmp = Krate::DSP::TestUtils::compareFingerprints(polluted.fingerprint.left,
                                                                       clean.fingerprint.left);
        const auto rightCmp = Krate::DSP::TestUtils::compareFingerprints(polluted.fingerprint.right,
                                                                        clean.fingerprint.right);
        INFO("left  " << leftCmp.detail << " worstMetric=" << leftCmp.worstMetricRelativeError);
        INFO("right " << rightCmp.detail << " worstMetric=" << rightCmp.worstMetricRelativeError);
        REQUIRE(leftCmp.withinTolerance());
        REQUIRE(rightCmp.withinTolerance());
    }
}

// ==============================================================================
// T007 case 3 - BloomEngine_NoAllocationAfterPrepare (SC-007, FR-071, FR-074)
// ==============================================================================
// Constitution Principle II. TWO scopes, deliberately SEQUENTIAL and never
// nested (AllocationScope::startTracking() zeroes the shared singleton's
// counter, so a nested scope would silently discard the outer count):
//
//   scope 1 - prepare() ITSELF. This component's prepare() is allocation-free,
//             unlike most: it has no heap term at all, every member being a
//             fixed-size std::array or a scalar (plan S9).
//   scope 2 - 10 000 processChunk calls plus EVERY setter and triggerBloom().
//
// HOW THE COUNT IS READ. AllocationScope latches its count in its DESTRUCTOR, so
// the live figure is read from AllocationDetector::instance() while the scope is
// still open. No Catch2 macro (REQUIRE, INFO, WARN) may run inside a scope -
// Catch2 allocates for its own bookkeeping - so every observation accumulates
// into plain locals and is asserted afterwards.
//
// FR-074's FOOTPRINT REPORT LIVES HERE AND NOWHERE ELSE, and is WARNed, never
// asserted: the plan's ~1.2 KB figure is an estimate, and sizeof() legitimately
// differs across MSVC/libstdc++/libc++ padding rules. Pinning it to a byte would
// be a portability trap of exactly the kind this repo has been burned by.
// ==============================================================================
TEST_CASE("BloomEngine_NoAllocationAfterPrepare", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    constexpr double kFs = 48000.0;
    constexpr std::size_t kChunks = 10000;
    constexpr std::size_t kParentCount = 24;

    std::array<float, BloomEngine::kMaxSlots> ratios{};
    std::array<float, BloomEngine::kMaxSlots> amplitudes{};
    fillSyntheticParents(ratios, amplitudes, kParentCount);

    BloomEngine engine;

    // ---- scope 1: prepare() ------------------------------------------------
    std::size_t prepareAllocations = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        engine.prepare(kFs, BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                                       .numChildSlots = 8});
        prepareAllocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    INFO("allocations during prepare(): " << prepareAllocations);
    REQUIRE(prepareAllocations == std::size_t{0});

    // ---- scope 2: the steady state -----------------------------------------
    constexpr std::array<float, 4> kFloatSweep{-1.0f, 0.0f, 0.5f, 2.0f};
    constexpr std::array<std::size_t, 4> kSizeSweep{0u, 1u, 4u, 99u};
    constexpr std::array<BloomEngine::Relation, 3> kRelations{
        BloomEngine::Relation::Octave, BloomEngine::Relation::Fifth,
        BloomEngine::Relation::DetunedNeighbour};

    std::size_t steadyAllocations = 0;
    std::size_t lastReturned = 0;
    bool stayedFinite = true;
    bool engaged = false;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        // FR-043: arm ONE event before the first chunk, so the 10 000-chunk window
        // covers the SPAWN path (runEvent's parent scan, draws, candidate search and
        // latch) and the ENGAGED write region - the two paths a heap term would
        // realistically hide in - rather than only the pass-through. Relying on the
        // FR-041 clock to engage us would leave that coverage to chance: at the
        // default rate the per-step probability is 1/240 * 64/48000 = 5.6e-6, so
        // 10 000 steps engage with probability ~5%, a seed lottery this case must
        // not be playing either way.
        engine.triggerBloom();

        // The per-chunk hot path, through the shared T007 fixture.
        lastReturned = stepEngine(engine, ratios.data(), amplitudes.data(), kParentCount, kChunks);
        stayedFinite = engine.stateFinite();
        engaged = engine.isEngaged();

        // EVERY setter on the FR-060 control surface, swept past both range ends,
        // plus the FR-043 trigger and the two lifecycle methods that are legal
        // after prepare(). None of them may touch the heap.
        for (std::size_t i = 0; i < kFloatSweep.size(); ++i) {
            const float v = kFloatSweep[i];
            engine.setDepth(v);
            engine.setSpawnRateHz(v);
            engine.setChildGain(v);
            engine.setFadeInSeconds(v);
            engine.setHoldSeconds(v);
            engine.setFadeOutSeconds(v);
            engine.setHoldJitterFraction(v);
            engine.setConsumerTiltDb(v);
            engine.setWake(v);
            for (const BloomEngine::Relation r : kRelations) {
                engine.setRelationWeight(r, v);
            }
            engine.setParentCount(kSizeSweep[i]);
            engine.setChildrenPerEvent(kSizeSweep[i]);
            engine.setCapacity(kSizeSweep[i]);
            engine.setDormant((i % 2) == 0);
            engine.setSeed(static_cast<std::uint32_t>(0xB1000000u + i));
            engine.triggerBloom();
        }
        engine.setCapacity(BloomEngine::kMaxSlots);
        engine.reset();

        steadyAllocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    INFO("allocations over " << kChunks << " chunks + the whole setter surface: "
                             << steadyAllocations);
    REQUIRE(steadyAllocations == std::size_t{0});
    // FR-051: the armed event spawned, so the engine is engaged and the sticky
    // latch keeps it returning capacity() for the rest of its life. (This line
    // read `== kParentCount` while T007's runEvent() was still a declared no-op -
    // an assumption about the ENGINE that was only ever an assumption about the
    // unimplemented spawn path, and which the spec contradicts: FR-051 says the
    // engine "cannot be observed to fall back to returning parentCount once it has
    // ever spawned".)
    REQUIRE(engaged);
    REQUIRE(lastReturned == BloomEngine::kMaxSlots);  // == the prepared capacity
    REQUIRE(stayedFinite);

    // FR-071: the getter exists so the Phase-10 host can total its children
    // uniformly (resonance_drift_network.h:906-912); for this component it is 0
    // as a FACT, not as a convention.
    REQUIRE(engine.getAllocatedBytes() == std::size_t{0});

    // FR-074: REPORTED, never asserted to a byte.
    WARN("FR-074 footprint: sizeof(BloomEngine) = "
         << sizeof(BloomEngine)
         << " bytes (reported, not asserted; plan S9 estimates ~1.2 KB and padding is "
            "toolchain-dependent)");
}

// ==============================================================================
// The T008 fixtures (plan S10.1: still NO new test helper header)
// ==============================================================================
// Coded ONCE, here, by T008. Later tasks in this TU (T009, T010, T014, T016-T019)
// REUSE these and do not re-declare them. The T007 fixtures above
// (fillSyntheticParents, stepEngine) are likewise reused, not duplicated.
// ==============================================================================
namespace {

/// Bit patterns, transcribed from
/// dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:130-155.
constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// @brief Build a non-finite float from its bit pattern THROUGH A VOLATILE SINK.
///
/// The volatile READ is the sink: it is what stops the constant being folded
/// back at compile time, which is how a -ffast-math build turns a
/// numeric_limits infinity() literal into a finite number. Idiom from
/// resonance_drift_network_nonfinite_test.cpp:149-155.
///
/// THIS TU IS NOT IN THE -fno-fast-math BLOCK, so it may CONSTRUCT such a value
/// (T008's FR-014 arm and SC-003's poison) but may never ASSERT IEEE semantics
/// on one: every assertion downstream of this function is on a COUNTER or on raw
/// BYTES (std::memcmp), never on a comparison whose answer depends on NaN
/// propagation.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief Cents from `reference` to `value`; positive when `value` is higher.
///        Evaluated in double so the measurement is finer than the 0.1-cent
///        tolerance it feeds.
[[nodiscard]] double centsBetween(float value, float reference) noexcept {
    return 1200.0 * std::log2(static_cast<double>(value) / static_cast<double>(reference));
}

/// Number of live parents in the well-spaced spectrum below.
constexpr std::size_t kWellSpacedParents = 4;

/// @brief The WELL-SPACED parent spectrum every "the path works" arm drives.
///
/// Parents {1.0, 1.3, 1.7, 2.2} sit >= 372 cents apart, and the SORTED union of
/// every candidate they can produce - octaves {2.0, 2.6, 3.4, 4.4}, fifths
/// {1.5, 1.95, 2.55, 3.3} and every +/-[24, 50] cent detune - has a minimum
/// adjacent gap of 24.06 cents (a detune against its OWN parent, which is >= 24
/// by construction) and 33.6 cents between any two candidates of DIFFERENT
/// parents. FR-022's 24-cent rule therefore never fires, so first-attempt
/// acceptance is certain.
///
/// That certainty is load-bearing, not convenience: FR-016's
/// without-replacement arm and FR-025's accounting arm both read the parent
/// drawn by the child that was PLACED, and a retry re-draws the parent (plan
/// S4.2 (d1) sits INSIDE the attempt loop). On a spectrum where candidates
/// collide, those arms would be measuring retry behaviour instead of the clause
/// they name. The FR-026 retry/fallback path has its own fixture - the
/// exactly-harmonic spectrum of fillSyntheticParents - and its own arm.
///
/// Slots at or above kWellSpacedParents carry the cloud's padding form
/// (ratio = i + 1, amplitude = 0, harmonic_cloud.h:825-826): present in the
/// FR-022 occupancy set, never eligible as parents.
void fillWellSpacedParents(float* ratios, float* amplitudes, std::size_t n) noexcept {
    constexpr std::array<float, kWellSpacedParents> kParentRatios{1.0f, 1.3f, 1.7f, 2.2f};
    constexpr std::array<float, kWellSpacedParents> kParentAmps{1.0f, 0.9f, 0.8f, 0.7f};
    for (std::size_t i = 0; i < n; ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    for (std::size_t i = 0; i < kWellSpacedParents && i < n; ++i) {
        ratios[i] = kParentRatios[i];
        amplitudes[i] = kParentAmps[i];
    }
}

/// @brief prepare() with the INTERNAL CLOCK OFF, so triggerBloom() is the only
///        source of events.
///
/// Leaving the FR-041 clock running would let a chance spawn perturb an arm that
/// counts events exactly - and at the default rate one event per 4 minutes is
/// rare enough to make such a failure irreproducible. setSeed() before
/// prepare() is safe: prepare()'s step (6) re-applies seed_, which survives
/// (FR-004).
void prepareTriggerOnly(Krate::DSP::BloomEngine& engine, std::uint32_t seed,
                        std::size_t capacity, std::size_t numChildSlots) noexcept {
    engine.setSeed(seed);
    engine.prepare(48000.0, Krate::DSP::BloomEngine::PrepareConfig{
                                .capacity = capacity, .numChildSlots = numChildSlots});
    engine.setSpawnRateHz(0.0f);
}

/// @brief Arm ONE event and run EXACTLY ONE control step.
/// @return processChunk's return - bound, never discarded ([[nodiscard]]).
std::size_t forceOneEvent(Krate::DSP::BloomEngine& engine, float* ratios, float* amplitudes,
                          std::size_t parentCount) noexcept {
    engine.triggerBloom();
    return engine.processChunk(ratios, amplitudes, parentCount,
                               Krate::DSP::BloomEngine::kControlChunkSamples);
}

/// @brief Fill a region with a repeating {NaN, -1, large garbage} cycle.
///
/// SC-003's padding arm poisons BEFORE EVERY CALL, so "the slot still holds the
/// pad" can never be satisfied by a stale value the engine wrote on an earlier
/// call and then stopped maintaining.
void poisonRegion(float* p, std::size_t n, std::size_t salt) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        switch ((i + salt) % 3) {
            case 0:
                p[i] = makeNonFinite(kQuietNaNBits);
                break;
            case 1:
                p[i] = -1.0f;
                break;
            default:
                p[i] = 1.0e30f;
                break;
        }
    }
}

/// @brief Collect the slot of every live child through the PUBLIC read surface.
///        getChildSlotIndex returns kMaxSlots for an Idle entry (S1.4), so a
///        value below kMaxSlots is exactly "this table entry holds a slot".
/// @return The number of live entries found.
std::size_t collectLiveSlots(
    const Krate::DSP::BloomEngine& engine,
    std::array<std::size_t, Krate::DSP::BloomEngine::kMaxChildren>& out) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < Krate::DSP::BloomEngine::kMaxChildren; ++i) {
        const std::size_t s = engine.getChildSlotIndex(i);
        if (s < Krate::DSP::BloomEngine::kMaxSlots) {
            out[n] = s;
            ++n;
        }
    }
    return n;
}

}  // namespace

// ==============================================================================
// T008 case 1 - BloomEngine_StrongestKParentSelection (SC-005, FR-013, FR-014)
// ==============================================================================
// FR-010/FR-011: on a spawn event the engine selects the strongest K partials of
// the supplied amplitude array over [0, min(parentCount, reserveBase())),
// descending by amplitude, ties broken by LOWER index, with
// amplitude <= kSilentParentAmplitude ineligible.
//
// THE REFERENCE ORDERING IS A std::stable_sort, NOT A std::partial_sort (plan
// R8). std::partial_sort is not stable, so an unqualified one would agree with a
// tie-break-by-lower-index engine only by luck - and the crafted arrays below
// are deliberately full of EXACT ties, which is precisely the case a non-stable
// reference cannot adjudicate.
//
// THE SELECTION IS READ THROUGH getLastParentSelectionCount() /
// getLastParentIndex(k) (plan S8 addition A-4). With K up to 8 and
// childrenPerEvent up to 4, most selected parents never produce a child, so the
// selection is simply invisible through the child table.
// ==============================================================================
TEST_CASE("BloomEngine_StrongestKParentSelection", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;
    constexpr std::size_t kSlots = BloomEngine::kMaxSlots;

    // ---- the reference ordering ------------------------------------------
    const auto referenceSelection = [](const std::array<float, kSlots>& ratios,
                                       const std::array<float, kSlots>& amplitudes,
                                       std::size_t scanEnd, std::size_t k) {
        struct Entry {
            float amp;
            std::size_t index;
        };
        std::vector<Entry> eligible;
        eligible.reserve(BloomEngine::kMaxSlots);
        for (std::size_t i = 0; i < scanEnd; ++i) {
            // FR-009 (e) + FR-011, mirrored exactly. detail::isFinite, never
            // std::isfinite (FR-008, tools/lint-nonfinite-symbols.js).
            if (!Krate::DSP::detail::isFinite(ratios[i]) ||
                !Krate::DSP::detail::isFinite(amplitudes[i])) {
                continue;
            }
            if (ratios[i] <= 0.0f) {
                continue;
            }
            if (amplitudes[i] <= BloomEngine::kSilentParentAmplitude) {
                continue;
            }
            eligible.push_back(Entry{amplitudes[i], i});
        }
        // Pushed on ASCENDING i, so a STABLE sort on descending amplitude leaves
        // an exact tie in ascending-index order: FR-011's tie-break.
        std::stable_sort(eligible.begin(), eligible.end(),
                         [](const Entry& x, const Entry& y) { return x.amp > y.amp; });
        if (eligible.size() > k) {
            eligible.resize(k);
        }
        std::vector<std::size_t> indices;
        indices.reserve(eligible.size());
        for (const Entry& e : eligible) {
            indices.push_back(e.index);
        }
        return indices;
    };

    const auto checkOneArray = [&](std::array<float, kSlots>& ratios,
                                   std::array<float, kSlots>& amplitudes,
                                   std::size_t parentCount, std::size_t k, std::uint32_t seed) {
        BloomEngine engine;
        prepareTriggerOnly(engine, seed, kSlots, 8);
        engine.setParentCount(k);
        const std::size_t effectiveK =
            std::min(std::max(k, std::size_t{1}), BloomEngine::kMaxParents);
        const std::size_t scanEnd = std::min(parentCount, engine.reserveBase());
        const std::vector<std::size_t> expected =
            referenceSelection(ratios, amplitudes, scanEnd, effectiveK);

        const std::size_t returned =
            forceOneEvent(engine, ratios.data(), amplitudes.data(), parentCount);

        INFO("parentCount=" << parentCount << " K=" << k << " scanEnd=" << scanEnd
                            << " seed=" << seed << " returned=" << returned);
        REQUIRE(returned > 0u);
        REQUIRE(engine.getSpawnEventCount() == std::uint64_t{1});
        REQUIRE(engine.getParentScanCount() == std::uint64_t{1});
        REQUIRE(engine.getLastParentSelectionCount() == expected.size());
        for (std::size_t j = 0; j < expected.size(); ++j) {
            INFO("selection rank " << j);
            const std::size_t chosen = engine.getLastParentIndex(j);
            REQUIRE(chosen == expected[j]);
            REQUIRE(chosen < scanEnd);  // never an index at or above min(pc, reserveBase())
            REQUIRE(amplitudes[chosen] > BloomEngine::kSilentParentAmplitude);
        }
        // One past the selection is out of domain and reads the neutral.
        REQUIRE(engine.getLastParentIndex(expected.size()) == BloomEngine::kMaxSlots);
    };

    SECTION("20 crafted arrays: known ordering, EXACT ties, sub-threshold slots") {
        constexpr std::array<std::size_t, 4> kCounts{4, 8, 24, 48};
        constexpr std::array<std::size_t, 4> kKs{1, 3, 4, 8};

        for (std::size_t c = 0; c < 20; ++c) {
            const std::size_t parentCount = kCounts[c % kCounts.size()];
            const std::size_t k = kKs[c % kKs.size()];
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            for (std::size_t i = 0; i < kSlots; ++i) {
                ratios[i] = static_cast<float>(i + 1);
                amplitudes[i] = 0.0f;
            }
            for (std::size_t i = 0; i < parentCount; ++i) {
                switch (c % 5) {
                    case 0:  // strictly descending - the unambiguous ordering
                        amplitudes[i] = 1.0f / static_cast<float>(i + 1);
                        break;
                    case 1:  // strictly ascending - the reversed ordering
                        amplitudes[i] = static_cast<float>(i + 1) * 0.01f;
                        break;
                    case 2:  // EVERY slot exactly tied: pure tie-break
                        amplitudes[i] = 0.5f;
                        break;
                    case 3:  // two in three below kSilentParentAmplitude
                        amplitudes[i] = ((i % 3) == 0) ? 0.5f : 1.0e-6f;
                        break;
                    default:  // PAIRED exact ties at two levels
                        amplitudes[i] = (((i / 2) % 2) == 0) ? 0.8f : 0.4f;
                        break;
                }
            }
            checkOneArray(ratios, amplitudes, parentCount, k,
                          0xC4A17E00u + static_cast<std::uint32_t>(c));
        }
    }

    SECTION("200 random arrays, amplitudes QUANTISED so exact ties occur naturally") {
        Krate::DSP::Xorshift32 rng{0x5EED0007u};
        for (std::size_t t = 0; t < 200; ++t) {
            const std::size_t parentCount =
                1 + static_cast<std::size_t>(rng.nextUnipolar() * 47.0f);
            const std::size_t k = 1 + static_cast<std::size_t>(rng.nextUnipolar() * 7.0f);
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            for (std::size_t i = 0; i < kSlots; ++i) {
                ratios[i] = static_cast<float>(i + 1);
                amplitudes[i] = 0.0f;
            }
            for (std::size_t i = 0; i < parentCount; ++i) {
                if (rng.nextUnipolar() < 0.25f) {
                    amplitudes[i] = 1.0e-7f;  // sub-threshold: ineligible
                } else {
                    // Quantised to sixteenths: exact ties are COMMON, which is
                    // what makes the random half exercise FR-011 too.
                    const auto level = static_cast<std::size_t>(rng.nextUnipolar() * 15.0f);
                    amplitudes[i] = static_cast<float>(level + 1) / 16.0f;
                }
            }
            checkOneArray(ratios, amplitudes, parentCount, k,
                          0x5EED1000u + static_cast<std::uint32_t>(t));
        }
    }

    SECTION("FR-013: EXACTLY one parent scan per executed spawn event") {
        // A per-chunk scan would make getParentScanCount() hundreds of times
        // larger. No CPU budget can police this: a per-chunk scan over 48 floats
        // costs ~100 ns per 512-sample block against SC-011's 10 667 ns ceiling,
        // so SC-011 passes by two orders of magnitude either way (spec FR-013).
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        fillSyntheticParents(ratios, amplitudes, 24);

        BloomEngine engine;
        prepareTriggerOnly(engine, 0xA11CE001u, kSlots, 8);

        constexpr std::size_t kChunks = 5000;
        constexpr std::size_t kTriggerEvery = 100;
        std::uint64_t issued = 0;
        std::size_t lastReturned = 0;
        for (std::size_t c = 0; c < kChunks; ++c) {
            if ((c % kTriggerEvery) == 0) {
                engine.triggerBloom();
                ++issued;
            }
            lastReturned = engine.processChunk(ratios.data(), amplitudes.data(), 24,
                                               BloomEngine::kControlChunkSamples);
        }
        INFO("chunks=" << kChunks << " triggers=" << issued << " last returned=" << lastReturned);
        REQUIRE(lastReturned > 0u);
        REQUIRE(issued == std::uint64_t{50});
        REQUIRE(engine.getSpawnEventCount() == issued);
        REQUIRE(engine.getParentScanCount() == engine.getSpawnEventCount());
        REQUIRE(engine.getDiscardedEventCount() == std::uint64_t{0});
    }

    SECTION("FR-014: an event with no eligible parent is consumed and touches nothing else") {
        // Plan S14 C-10 is normative: such an event advances getSpawnEventCount()
        // and getParentScanCount() by exactly one and NO other counter - in
        // particular NOT getRejectedSpawnCount(), which counts rejected candidate
        // RATIOS, and an event with no eligible parent never forms one.
        const auto runEmptyArm = [&](bool nonFinite) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            for (std::size_t i = 0; i < kSlots; ++i) {
                if (nonFinite) {
                    const std::uint32_t bits = [&]() -> std::uint32_t {
                        if ((i % 3) == 0) {
                            return kQuietNaNBits;
                        }
                        if ((i % 3) == 1) {
                            return kPosInfBits;
                        }
                        return kNegInfBits;
                    }();
                    // CONSTRUCTED through the volatile sink; asserted on only
                    // via counters and raw bytes, never via IEEE semantics.
                    ratios[i] = makeNonFinite(bits);
                    amplitudes[i] = makeNonFinite(bits);
                } else {
                    ratios[i] = static_cast<float>(i + 1);
                    amplitudes[i] = 1.0e-6f;  // <= kSilentParentAmplitude (1e-5)
                }
            }
            const std::array<float, kSlots> pristineRatios = ratios;
            const std::array<float, kSlots> pristineAmplitudes = amplitudes;

            BloomEngine engine;
            prepareTriggerOnly(engine, 0xFACE0014u, kSlots, 8);
            constexpr std::size_t kParentCount = 32;

            const std::size_t returned =
                forceOneEvent(engine, ratios.data(), amplitudes.data(), kParentCount);

            INFO("non-finite arm = " << nonFinite);
            REQUIRE(returned == kParentCount);  // never engaged: the unclamped count
            REQUIRE(engine.getSpawnEventCount() == std::uint64_t{1});
            REQUIRE(engine.getParentScanCount() == std::uint64_t{1});
            REQUIRE(engine.getOfferedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getRejectedSpawnCount() == std::uint64_t{0});
            REQUIRE(engine.getRefusedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getLastParentSelectionCount() == std::size_t{0});
            REQUIRE(engine.getLiveChildCount() == std::size_t{0});
            REQUIRE(engine.stateFinite());
            REQUIRE_FALSE(engine.isEngaged());
            // Nothing engaged, so there is no FR-051 pad region at all and the
            // WHOLE array must be byte-unchanged.
            constexpr std::size_t kBytes = kSlots * sizeof(float);
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            REQUIRE(std::memcmp(ratios.data(), pristineRatios.data(), kBytes) == 0);
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            REQUIRE(std::memcmp(amplitudes.data(), pristineAmplitudes.data(), kBytes) == 0);
        };

        runEmptyArm(false);  // (i) every amplitude at or below the silence floor
        runEmptyArm(true);   // (ii) every entry non-finite
    }
}

// ==============================================================================
// T008 case 2 - BloomEngine_ChildRatioRelationships (SC-006, minus the consumer arm)
// ==============================================================================
// Every SC-006 arm that does not need a real HarmonicCloud and a fade-in. The
// consumer arm and its negative control need both and are T009's.
//
// EVERY ARM SPAWNS AT MOST numChildSlots() CHILDREN. Without the T009 lifecycle
// clock no child ever retires, so an arm that wanted N events worth of children
// on ONE engine would silently stop spawning after the table filled and would
// then be measuring an exhausted table rather than the relationship law. Each
// "event" below is therefore a FRESH engine driven by exactly one triggerBloom()
// and one control step - which also makes the event stream a pure function of
// (seed, controlStep_ == 0) and so exactly reproducible.
//
// The tolerances: 0.1 cent for the exact intervals (the octave and fifth factors
// are exact floats, so the only error is one float multiply), and [23.9, 50.1]
// for the detune band (centsToPitchRatioFast is documented to 1.06e-4 cent on
// |cents| <= 50, pitch_utils.h:51-53; the 0.1-cent slack absorbs the float log2
// of the measurement itself). No bit-exact float golden is involved anywhere.
// ==============================================================================
TEST_CASE("BloomEngine_ChildRatioRelationships", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;
    constexpr std::size_t kSlots = BloomEngine::kMaxSlots;
    constexpr double kExactIntervalCents = 0.1;
    constexpr double kDetuneFloorCents = 23.9;
    constexpr double kDetuneCeilCents = 50.1;

    SECTION("SC-006: 500 seeded events, every relationship law at once") {
        constexpr std::size_t kEvents = 500;

        std::size_t totalChildren = 0;
        std::size_t octaves = 0;
        std::size_t fifths = 0;
        std::size_t detunes = 0;
        double worstOctaveCents = 0.0;
        double worstFifthCents = 0.0;
        double smallestDetune = 1.0e9;
        double largestDetune = 0.0;
        double tightestSpacing = 1.0e9;
        bool ratiosInBounds = true;
        bool boundsRespectedByRejection = true;

        for (std::size_t e = 0; e < kEvents; ++e) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);
            // The spectrum AS IT STOOD AT SPAWN. The engine never writes the
            // parent region, but snapshotting is what makes that a measured fact
            // rather than an assumption.
            const std::array<float, kSlots> parents = ratios;

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x7E570000u + static_cast<std::uint32_t>(e), kSlots, 8);
            engine.setParentCount(4);
            engine.setChildrenPerEvent(2);

            const std::size_t returned =
                forceOneEvent(engine, ratios.data(), amplitudes.data(), kWellSpacedParents);
            REQUIRE(returned > 0u);
            REQUIRE(engine.getLastParentSelectionCount() == kWellSpacedParents);

            // Collect this event's children once; the spacing clause needs them
            // as a set, not one at a time.
            std::array<float, BloomEngine::kMaxChildren> childRatios{};
            std::size_t numChildren = 0;

            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                if (engine.getChildPhase(i) == BloomEngine::Phase::Idle) {
                    continue;
                }
                ++totalChildren;
                const float childRatio = engine.getChildRatio(i);
                childRatios[numChildren] = childRatio;
                ++numChildren;

                if (childRatio < BloomEngine::kMinChildRatio ||
                    childRatio > BloomEngine::kMaxChildRatio) {
                    ratiosInBounds = false;
                }

                const float parentRatio = parents[engine.getChildParentIndex(i)];
                const BloomEngine::Relation relation = engine.getChildRelation(i);
                const bool fallback = engine.getIsChildFallback(i);

                if (relation == BloomEngine::Relation::Octave) {
                    ++octaves;
                    const double off = std::fabs(
                        centsBetween(childRatio, parentRatio * BloomEngine::kOctaveFactor));
                    if (!fallback) {
                        worstOctaveCents = std::max(worstOctaveCents, off);
                    }
                } else if (relation == BloomEngine::Relation::Fifth) {
                    ++fifths;
                    const double off = std::fabs(
                        centsBetween(childRatio, parentRatio * BloomEngine::kFifthFactor));
                    if (!fallback) {
                        worstFifthCents = std::max(worstFifthCents, off);
                    }
                } else {
                    ++detunes;
                    const double off = std::fabs(centsBetween(childRatio, parentRatio));
                    smallestDetune = std::min(smallestDetune, off);
                    largestDetune = std::max(largestDetune, off);
                }
            }

            // FR-022 / FR-016: no child within kMinRatioSpacingCents of a partial
            // present at spawn, NOR of a sibling of the same event.
            const std::size_t scanEnd = std::min(kWellSpacedParents, engine.reserveBase());
            for (std::size_t a = 0; a < numChildren; ++a) {
                for (std::size_t p = 0; p < scanEnd; ++p) {
                    tightestSpacing =
                        std::min(tightestSpacing, std::fabs(centsBetween(childRatios[a],
                                                                         parents[p])));
                }
                for (std::size_t b = a + 1; b < numChildren; ++b) {
                    tightestSpacing = std::min(
                        tightestSpacing, std::fabs(centsBetween(childRatios[a], childRatios[b])));
                }
            }

            // FR-021 is a REJECTION test: an out-of-range candidate must never be
            // clamped into range. Nothing on this spectrum can leave [0.5, 128],
            // so the clause is asserted here as "no emitted ratio is out of
            // bounds" and separately, non-vacuously, in the FR-021 section below.
            if (engine.getSpawnedChildCount() + engine.getRefusedChildCount() !=
                engine.getOfferedChildCount()) {
                boundsRespectedByRejection = false;
            }
        }

        INFO("children=" << totalChildren << " octaves=" << octaves << " fifths=" << fifths
                         << " detunes=" << detunes);
        INFO("worst octave error (cents) = " << worstOctaveCents);
        INFO("worst fifth error (cents)  = " << worstFifthCents);
        INFO("detune band observed = [" << smallestDetune << ", " << largestDetune << "] cents");
        INFO("tightest spacing (cents)   = " << tightestSpacing);

        // Non-vacuity: every per-relation clause below is a conditional, so each
        // relation must actually have occurred.
        REQUIRE(totalChildren >= kEvents);  // at least one child per event
        REQUIRE(octaves > 0u);
        REQUIRE(fifths > 0u);
        REQUIRE(detunes > 0u);

        REQUIRE(worstOctaveCents <= kExactIntervalCents);
        REQUIRE(worstFifthCents <= kExactIntervalCents);
        REQUIRE(smallestDetune >= kDetuneFloorCents);
        REQUIRE(largestDetune <= kDetuneCeilCents);
        REQUIRE(tightestSpacing >= kDetuneFloorCents);
        REQUIRE(ratiosInBounds);
        REQUIRE(boundsRespectedByRejection);
    }

    SECTION("FR-026: the retry / detuned-fallback path is LIVE, not dead code (plan R5)") {
        // An EXACTLY-HARMONIC parent spectrum (integer ratios) puts the octave of
        // every parent, and the fifth of every even parent, ON an existing
        // partial - so FR-022 rejects them and the retry ladder is forced.
        // Without this arm FR-026's whole path could be unreachable and every
        // other clause would still pass.
        constexpr std::size_t kEvents = 500;
        constexpr std::size_t kParentCount = 12;

        std::uint64_t rejections = 0;
        std::uint64_t fallbacks = 0;
        std::size_t fallbackChildrenSeen = 0;
        bool fallbackRelationOk = true;
        bool fallbackOffsetOk = true;
        double worstFallbackOffset = 0.0;
        double bestFallbackOffset = 1.0e9;

        for (std::size_t e = 0; e < kEvents; ++e) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillSyntheticParents(ratios, amplitudes, kParentCount);
            const std::array<float, kSlots> parents = ratios;

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x4A11BAC0u + static_cast<std::uint32_t>(e), kSlots, 8);
            engine.setParentCount(4);
            engine.setChildrenPerEvent(2);

            const std::size_t returned =
                forceOneEvent(engine, ratios.data(), amplitudes.data(), kParentCount);
            REQUIRE(returned > 0u);

            rejections += engine.getRejectedSpawnCount();
            fallbacks += engine.getFallbackChildCount();

            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                if (engine.getChildPhase(i) == BloomEngine::Phase::Idle) {
                    continue;
                }
                if (!engine.getIsChildFallback(i)) {
                    continue;
                }
                ++fallbackChildrenSeen;
                const BloomEngine::Relation relation = engine.getChildRelation(i);
                // FR-026: a fallback reports the ORIGINAL relation - it is a
                // DETUNED octave or fifth, never a third relation.
                if (relation == BloomEngine::Relation::DetunedNeighbour) {
                    fallbackRelationOk = false;
                    continue;
                }
                const float parentRatio = parents[engine.getChildParentIndex(i)];
                const float exact =
                    parentRatio * ((relation == BloomEngine::Relation::Octave)
                                       ? BloomEngine::kOctaveFactor
                                       : BloomEngine::kFifthFactor);
                const double off = std::fabs(centsBetween(engine.getChildRatio(i), exact));
                worstFallbackOffset = std::max(worstFallbackOffset, off);
                bestFallbackOffset = std::min(bestFallbackOffset, off);
                if (off < kDetuneFloorCents || off > kDetuneCeilCents) {
                    fallbackOffsetOk = false;
                }
            }
        }

        INFO("rejections=" << rejections << " fallbackChildren=" << fallbacks << " seen="
                           << fallbackChildrenSeen);
        INFO("fallback offset band = [" << bestFallbackOffset << ", " << worstFallbackOffset
                                        << "] cents");
        REQUIRE(rejections > std::uint64_t{0});
        REQUIRE(fallbacks > std::uint64_t{0});
        REQUIRE(fallbackChildrenSeen > 0u);
        REQUIRE(fallbackRelationOk);
        REQUIRE(fallbackOffsetOk);
    }

    SECTION("FR-021: an out-of-range candidate is REJECTED, never clamped") {
        // ---- the HIGH end: parent 100 x 2 = 200, above kMaxChildRatio ------
        {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            for (std::size_t i = 0; i < kSlots; ++i) {
                ratios[i] = static_cast<float>(i + 1);
                amplitudes[i] = 0.0f;
            }
            ratios[0] = 100.0f;
            amplitudes[0] = 1.0f;

            BloomEngine engine;
            prepareTriggerOnly(engine, 0xC1A11D00u, kSlots, 8);
            engine.setParentCount(1);
            engine.setChildrenPerEvent(1);
            engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
            engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.0f);
            engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 0.0f);

            const std::size_t returned =
                forceOneEvent(engine, ratios.data(), amplitudes.data(), 1);

            INFO("high-end arm: every attempt is an octave of ratio 100");
            REQUIRE(returned == std::size_t{1});  // never engaged: the unclamped count
            REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{0});
            REQUIRE(engine.getLiveChildCount() == std::size_t{0});
            REQUIRE_FALSE(engine.isEngaged());
            // kMaxSpawnAttempts failed attempts plus the ONE final-attempt
            // fallback re-test, which is also out of range.
            REQUIRE(engine.getRejectedSpawnCount() ==
                    static_cast<std::uint64_t>(BloomEngine::kMaxSpawnAttempts) + 1u);
            REQUIRE(engine.getRefusedChildCount() == std::uint64_t{1});
            REQUIRE(engine.getOfferedChildCount() ==
                    engine.getSpawnedChildCount() + engine.getRefusedChildCount());
        }

        // ---- the LOW end: a downward detune of ratio 0.5 falls below 0.5 ----
        {
            std::uint64_t rejections = 0;
            std::size_t spawned = 0;
            bool allInBounds = true;
            for (std::size_t e = 0; e < 200; ++e) {
                std::array<float, kSlots> ratios{};
                std::array<float, kSlots> amplitudes{};
                for (std::size_t i = 0; i < kSlots; ++i) {
                    ratios[i] = static_cast<float>(i + 1);
                    amplitudes[i] = 0.0f;
                }
                ratios[0] = BloomEngine::kMinChildRatio;  // exactly 0.5
                amplitudes[0] = 1.0f;

                BloomEngine engine;
                prepareTriggerOnly(engine, 0x10E0D000u + static_cast<std::uint32_t>(e), kSlots, 8);
                engine.setParentCount(1);
                engine.setChildrenPerEvent(1);
                engine.setRelationWeight(BloomEngine::Relation::Octave, 0.0f);
                engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.0f);
                engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 1.0f);

                const std::size_t returned =
                    forceOneEvent(engine, ratios.data(), amplitudes.data(), 1);
                REQUIRE(returned > 0u);
                rejections += engine.getRejectedSpawnCount();
                for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                    if (engine.getChildPhase(i) == BloomEngine::Phase::Idle) {
                        continue;
                    }
                    ++spawned;
                    // A clamping implementation would emit exactly 0.5 here.
                    // A rejecting one only ever emits the UPWARD detune.
                    if (engine.getChildRatio(i) <= BloomEngine::kMinChildRatio ||
                        engine.getChildRatio(i) > BloomEngine::kMaxChildRatio) {
                        allInBounds = false;
                    }
                }
            }
            INFO("low-end arm: rejections=" << rejections << " spawned=" << spawned);
            REQUIRE(rejections > std::uint64_t{0});  // the downward draws WERE refused
            REQUIRE(spawned > 0u);                    // and the upward ones were not
            REQUIRE(allInBounds);
        }
    }

    SECTION("FR-060: relation weights steer every child; all-zero falls back to uniform") {
        // Without this arm every per-relation clause of SC-006 is a conditional
        // that an engine always returning Relation::Octave satisfies vacuously.
        const auto sweep = [&](float w0, float w1, float w2) {
            std::array<std::size_t, 3> counts{0, 0, 0};
            std::size_t children = 0;
            for (std::size_t e = 0; e < 200; ++e) {
                std::array<float, kSlots> ratios{};
                std::array<float, kSlots> amplitudes{};
                fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

                BloomEngine engine;
                prepareTriggerOnly(engine, 0xBEE70000u + static_cast<std::uint32_t>(e), kSlots, 8);
                engine.setParentCount(4);
                engine.setChildrenPerEvent(2);
                engine.setRelationWeight(BloomEngine::Relation::Octave, w0);
                engine.setRelationWeight(BloomEngine::Relation::Fifth, w1);
                engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, w2);

                const std::size_t returned =
                    forceOneEvent(engine, ratios.data(), amplitudes.data(), kWellSpacedParents);
                REQUIRE(returned > 0u);
                for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                    if (engine.getChildPhase(i) == BloomEngine::Phase::Idle) {
                        continue;
                    }
                    ++children;
                    ++counts[static_cast<std::size_t>(engine.getChildRelation(i))];
                }
            }
            REQUIRE(children > 0u);
            return counts;
        };

        {
            const std::array<std::size_t, 3> c = sweep(1.0f, 0.0f, 0.0f);
            INFO("weights {1,0,0} -> " << c[0] << ", " << c[1] << ", " << c[2]);
            REQUIRE(c[0] > 0u);
            REQUIRE(c[1] == std::size_t{0});
            REQUIRE(c[2] == std::size_t{0});
        }
        {
            const std::array<std::size_t, 3> c = sweep(0.0f, 1.0f, 0.0f);
            INFO("weights {0,1,0} -> " << c[0] << ", " << c[1] << ", " << c[2]);
            REQUIRE(c[0] == std::size_t{0});
            REQUIRE(c[1] > 0u);
            REQUIRE(c[2] == std::size_t{0});
        }
        {
            const std::array<std::size_t, 3> c = sweep(0.0f, 0.0f, 1.0f);
            INFO("weights {0,0,1} -> " << c[0] << ", " << c[1] << ", " << c[2]);
            REQUIRE(c[0] == std::size_t{0});
            REQUIRE(c[1] == std::size_t{0});
            REQUIRE(c[2] > 0u);
        }
        {
            // The all-zero edge case: a uniform draw, NOT a degenerate one.
            const std::array<std::size_t, 3> c = sweep(0.0f, 0.0f, 0.0f);
            INFO("weights {0,0,0} -> " << c[0] << ", " << c[1] << ", " << c[2]);
            REQUIRE(c[0] > 0u);
            REQUIRE(c[1] > 0u);
            REQUIRE(c[2] > 0u);
        }
    }

    SECTION("FR-016: parents are drawn WITHOUT replacement inside one event") {
        for (std::size_t n = 1; n <= 4; ++n) {
            std::size_t events = 0;
            bool pairwiseDistinct = true;
            bool allPlaced = true;
            for (std::size_t e = 0; e < 200; ++e) {
                std::array<float, kSlots> ratios{};
                std::array<float, kSlots> amplitudes{};
                fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

                BloomEngine engine;
                prepareTriggerOnly(engine, 0x00D15700u + static_cast<std::uint32_t>(e), kSlots, 4);
                engine.setParentCount(4);
                engine.setChildrenPerEvent(n);

                const std::size_t returned =
                    forceOneEvent(engine, ratios.data(), amplitudes.data(), kWellSpacedParents);
                REQUIRE(returned > 0u);
                // childrenPerEvent <= getLastParentSelectionCount(), so every
                // child draws a DIFFERENT parent.
                REQUIRE(engine.getLastParentSelectionCount() == kWellSpacedParents);
                if (engine.getSpawnedChildCount() != static_cast<std::uint64_t>(n)) {
                    allPlaced = false;
                }

                std::array<std::size_t, BloomEngine::kMaxChildren> parentsUsed{};
                std::size_t used = 0;
                for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                    if (engine.getChildPhase(i) == BloomEngine::Phase::Idle) {
                        continue;
                    }
                    parentsUsed[used] = engine.getChildParentIndex(i);
                    ++used;
                }
                for (std::size_t a = 0; a < used; ++a) {
                    for (std::size_t b = a + 1; b < used; ++b) {
                        if (parentsUsed[a] == parentsUsed[b]) {
                            pairwiseDistinct = false;
                        }
                    }
                }
                ++events;
            }
            INFO("childrenPerEvent = " << n);
            REQUIRE(events == std::size_t{200});
            REQUIRE(allPlaced);
            REQUIRE(pairwiseDistinct);
        }
    }

    SECTION("FR-015: no single event offers more than getChildrenPerEvent() children") {
        for (std::size_t n = 1; n <= BloomEngine::kMaxChildrenPerEvent; ++n) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0xCA9E0000u + static_cast<std::uint32_t>(n), kSlots, 8);
            engine.setParentCount(4);
            engine.setChildrenPerEvent(n);
            REQUIRE(engine.getChildrenPerEvent() == n);

            const std::size_t returned =
                forceOneEvent(engine, ratios.data(), amplitudes.data(), kWellSpacedParents);
            INFO("childrenPerEvent = " << n << " returned = " << returned);
            REQUIRE(returned > 0u);
            REQUIRE(engine.getOfferedChildCount() == static_cast<std::uint64_t>(n));
            REQUIRE(engine.getOfferedChildCount() ==
                    engine.getSpawnedChildCount() + engine.getRefusedChildCount());
        }
    }

    SECTION("FR-025: a slot-exhausted child is refused EXACTLY ONCE, not once per attempt") {
        // Plan S4.2 records the draft bug this arm exists for: an earlier shape
        // incremented refusedChildren_ inside the slot-taking helper AND in the
        // tail, counting one slot-exhausted child up to five times, breaking the
        // C-7 identity and inflating getRejectedSpawnCount() fourfold.
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

        BloomEngine engine;
        prepareTriggerOnly(engine, 0x5107F011u, kSlots, /*numChildSlots=*/2);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(4);
        REQUIRE(engine.numChildSlots() == std::size_t{2});

        // Event 1 fills both owned slots; the remaining two children find none.
        const std::size_t r1 =
            forceOneEvent(engine, ratios.data(), amplitudes.data(), kWellSpacedParents);
        REQUIRE(r1 == engine.capacity());
        REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{2});
        REQUIRE(engine.getOfferedChildCount() == std::uint64_t{4});
        REQUIRE(engine.getRefusedChildCount() == std::uint64_t{2});
        REQUIRE(engine.getRejectedSpawnCount() == std::uint64_t{2});

        const std::uint64_t refused0 = engine.getRefusedChildCount();
        const std::uint64_t rejected0 = engine.getRejectedSpawnCount();

        // Event 2 is offered against a FULL table: four refusals, four
        // rejections - NOT sixteen.
        const std::size_t r2 =
            forceOneEvent(engine, ratios.data(), amplitudes.data(), kWellSpacedParents);
        INFO("event 2 returned " << r2);
        REQUIRE(r2 == engine.capacity());
        REQUIRE(engine.getSpawnedChildCount() == std::uint64_t{2});
        REQUIRE(engine.getOfferedChildCount() == std::uint64_t{8});
        REQUIRE(engine.getRefusedChildCount() == refused0 + 4u);
        REQUIRE(engine.getRejectedSpawnCount() == rejected0 + 4u);
        // Plan S14 C-7, exact and unconditional.
        REQUIRE(engine.getOfferedChildCount() ==
                engine.getSpawnedChildCount() + engine.getRefusedChildCount());
    }

    // ==========================================================================
    // T009 - SC-006's CONSUMER arm, and the negative control that proves it fires
    // ==========================================================================
    // Every clause above reads getChildRatio() / getChildAmplitude(), so a
    // perfectly correct ratio written into the WRONG SLOT, or a wrong returned
    // count, is invisible to all of them. This arm drives a REAL HarmonicCloud at
    // the Phase-10 call shape (seraphis_voice.h:1050-1054) and reads the child
    // back through the cloud's own introspection surface.
    //
    // THE NEGATIVE CONTROL IS REQUIRED, not decorative. Overview fact 1:
    // HarmonicCloud::recalculateAmplitudes() zeroes baseAmplitude_[i] and
    // `continue`s for every i >= activeCount_ BEFORE the spectral-target branch
    // is reached (harmonic_cloud.h:1469-1473 against :1492-1494), and
    // activeCount_ = clamp(round(64^richness), 1, 64) (:1462-1463). A child
    // written past the active count is SILENTLY INAUDIBLE - which is exactly what
    // FR-050's capacity contract exists to prevent, and exactly what a Phase-10
    // integration would otherwise discover by ear. Running the same fixture with
    // capacity ABOVE the cloud's active count must make the amplitude clause
    // fail; if it does not, the positive clause was never measuring the cloud.
    //
    // THE FADE IS COMPRESSED TO 1 s. The lifecycle SHAPE is SC-002's business and
    // is measured there against getChildAmplitude() with no cloud in the way;
    // what this arm needs is only that the cloud's target AMPLITUDE for the
    // child's slot follows the child up, which a 1 s fade shows in 48 000
    // rendered samples instead of 2.16 million.
    SECTION("SC-006: the child is verified THROUGH a real HarmonicCloud") {
        using Krate::DSP::HarmonicCloud;

        constexpr float kFundamentalHz = 110.0f;
        constexpr std::size_t kCloudCapacity = 32;
        constexpr std::size_t kCloudChildSlots = 2;
        constexpr std::size_t kSettleChunks = 188;   // ~0.25 s, past the 0.05 s attack
        constexpr std::size_t kFadeSamples = 10;     // every 0.1 s of the 1 s fade-in
        constexpr std::size_t kChunksPerSample = 75;

        struct ConsumerRun {
            std::size_t activeCount = 0;
            std::size_t slot = BloomEngine::kMaxSlots;
            float childRatio = 0.0f;
            double worstFreqCents = 0.0;
            std::array<float, kFadeSamples> cloudAmp{};
            float engineAmpFirst = 0.0f;
            float engineAmpLast = 0.0f;
        };

        const auto drive = [&](float richness) {
            ConsumerRun out{};

            HarmonicCloud cloud;
            cloud.prepare(48000.0);
            cloud.setFundamentalHz(kFundamentalHz);
            cloud.setRichness(richness);
            cloud.noteOn();
            out.activeCount = cloud.getActivePartialCount();

            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0xC10D0006u, kCloudCapacity, kCloudChildSlots);
            engine.setParentCount(4);
            engine.setChildrenPerEvent(1);
            engine.setChildGain(1.0f);
            engine.setFadeInSeconds(1.0f);
            engine.setHoldSeconds(1.0f);
            engine.setFadeOutSeconds(1.0f);

            std::array<float, BloomEngine::kControlChunkSamples> left{};
            std::array<float, BloomEngine::kControlChunkSamples> right{};

            // THE PHASE-10 CALL SHAPE, once per 64-sample control chunk. The
            // <= 64-sample slice is a BOUND and not a suggestion
            // (harmonic_cloud.h:735-751): processStereoBlock restarts its internal
            // control grid on every call, so a target supplied once per host block
            // would be frozen for all eight internal chunks.
            const auto chunk = [&]() {
                const std::size_t returned = engine.processChunk(
                    ratios.data(), amplitudes.data(), kWellSpacedParents,
                    BloomEngine::kControlChunkSamples);
                cloud.setSpectralTarget(ratios.data(), amplitudes.data(), returned);
                cloud.processStereoBlock(left.data(), right.data(),
                                         BloomEngine::kControlChunkSamples);
                return returned;
            };

            for (std::size_t c = 0; c < kSettleChunks; ++c) {
                const std::size_t returned = chunk();
                REQUIRE(returned == kWellSpacedParents);  // disengaged pass-through
            }

            engine.triggerBloom();
            const std::size_t engagedReturn = chunk();
            REQUIRE(engagedReturn == kCloudCapacity);
            REQUIRE(engine.getLiveChildCount() == std::size_t{1});

            std::size_t table = BloomEngine::kMaxChildren;
            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                if (engine.getChildPhase(i) != BloomEngine::Phase::Idle) {
                    table = i;
                }
            }
            REQUIRE(table < BloomEngine::kMaxChildren);
            out.slot = engine.getChildSlotIndex(table);
            out.childRatio = engine.getChildRatio(table);
            REQUIRE(out.slot >= engine.reserveBase());
            REQUIRE(out.slot < kCloudCapacity);
            out.engineAmpFirst = engine.getChildAmplitude(table);

            for (std::size_t s = 0; s < kFadeSamples; ++s) {
                for (std::size_t c = 0; c < kChunksPerSample; ++c) {
                    const std::size_t returned = chunk();
                    if (returned != kCloudCapacity) {
                        FAIL("engaged call did not return capacity()");
                    }
                }
                out.cloudAmp[s] = cloud.getPartialTargetAmplitude(out.slot);
                // FR-024: the ratio is LATCHED, so the cloud's synthesized
                // frequency must equal fundamental x childRatio for the whole
                // life of the child, not merely on the first chunk.
                const double cents =
                    std::fabs(centsBetween(cloud.getPartialFrequencyHz(out.slot),
                                           kFundamentalHz * out.childRatio));
                out.worstFreqCents = std::max(out.worstFreqCents, cents);
            }
            out.engineAmpLast = engine.getChildAmplitude(table);
            REQUIRE(cloud.stateFinite());
            return out;
        };

        // ---- the POSITIVE arm: richness 1.0 -> activeCount 64 > capacity 32 ---
        const ConsumerRun audible = drive(1.0f);
        INFO("audible: activeCount=" << audible.activeCount << " slot=" << audible.slot
                                     << " ratio=" << audible.childRatio
                                     << " worst freq error (cents)=" << audible.worstFreqCents);
        REQUIRE(audible.activeCount == BloomEngine::kMaxSlots);
        REQUIRE(audible.slot < audible.activeCount);
        REQUIRE(audible.worstFreqCents <= kExactIntervalCents);
        REQUIRE(audible.engineAmpLast > audible.engineAmpFirst);
        bool rising = true;
        for (std::size_t s = 1; s < kFadeSamples; ++s) {
            if (!(audible.cloudAmp[s] > audible.cloudAmp[s - 1])) {
                rising = false;
            }
        }
        INFO("cloud target amplitude first=" << audible.cloudAmp[0] << " last="
                                             << audible.cloudAmp[kFadeSamples - 1]);
        REQUIRE(audible.cloudAmp[0] > 0.0f);
        REQUIRE(rising);

        // ---- the NEGATIVE control: richness 0.5 -> activeCount 8 < capacity 32
        const ConsumerRun inaudible = drive(0.5f);
        INFO("inaudible: activeCount=" << inaudible.activeCount << " slot=" << inaudible.slot);
        REQUIRE(inaudible.activeCount == std::size_t{8});
        REQUIRE(inaudible.slot >= inaudible.activeCount);  // the precondition of the control
        // The ENGINE is unchanged - the child grows exactly as before.
        REQUIRE(inaudible.engineAmpLast > inaudible.engineAmpFirst);
        // The CLOUD never hears it: harmonic_cloud.h:1469-1473 zeroes the slot
        // before the spectral-target branch. The positive arm's amplitude clause
        // therefore FAILS here, which is what makes it a real measurement.
        bool everAudible = false;
        for (std::size_t s = 0; s < kFadeSamples; ++s) {
            if (inaudible.cloudAmp[s] != 0.0f) {
                everAudible = true;
            }
        }
        REQUIRE_FALSE(everAudible);
    }
}

// ==============================================================================
// T008 case 3 - BloomEngine_SlotAccountingInvariantsUnderFuzz (SC-003)
// ==============================================================================
// SC-003's WRITE-REGION arms. The 1 000-config fuzz, the FR-032 death arm and
// the sticky arm are added to this same case by T009; the FR-055 capacity arm by
// T010. Everything here is reachable with the lifecycle clock still a no-op,
// because every fixture spawns at most numChildSlots() children.
//
// THE POISON IS RE-APPLIED BEFORE EVERY CALL. An engine that wrote the pad once
// and then stopped maintaining it would pass a test that poisoned only at the
// start; it cannot pass this one. FR-051's pad is an EVERY-CALL obligation
// precisely because a retired child's slot has to be re-padded on the very step
// it retires (FR-032), and the same loop is what does both.
//
// The == and std::memcmp comparisons are exact and legal: they are WITHIN-RUN
// STRUCTURAL IDENTITIES - a pad value that was stored, not computed, and a byte
// range that was never touched. No bit-exact float golden is involved.
// ==============================================================================
TEST_CASE("BloomEngine_SlotAccountingInvariantsUnderFuzz", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;
    constexpr std::size_t kSlots = BloomEngine::kMaxSlots;
    constexpr std::size_t kCapacity = 48;
    constexpr std::size_t kChildSlots = 8;
    constexpr std::size_t kParentCount = 16;

    // Engage an engine DETERMINISTICALLY: one triggered event on the well-spaced
    // spectrum is accepted on the first attempt, so engaged_ latches and the
    // whole write region becomes observable.
    const auto engage = [](BloomEngine& engine, float* ratios, float* amplitudes,
                           std::size_t bufferLength, std::uint32_t seed, std::size_t capacity,
                           std::size_t childSlots, std::size_t parentCount) {
        fillWellSpacedParents(ratios, amplitudes, bufferLength);
        prepareTriggerOnly(engine, seed, capacity, childSlots);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(2);
        const std::size_t returned = forceOneEvent(engine, ratios, amplitudes, parentCount);
        REQUIRE(returned == capacity);
        REQUIRE(engine.isEngaged());
        REQUIRE(engine.getLiveChildCount() > std::size_t{0});
    };

    SECTION("padding, canary and snapshot over the WHOLE write region") {
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        BloomEngine engine;
        engage(engine, ratios.data(), amplitudes.data(), kSlots, 0x51070001u, kCapacity,
               kChildSlots, kParentCount);

        const std::size_t base = engine.reserveBase();
        REQUIRE(base == kCapacity - kChildSlots);
        REQUIRE(kParentCount < base);  // the FR-051 GAP is non-empty for this arm

        bool padOk = true;
        bool parentRegionOk = true;
        bool canaryOk = true;
        bool structuralOk = true;
        std::array<std::size_t, BloomEngine::kMaxChildren> liveSlots{};

        constexpr std::size_t kCalls = 200;
        for (std::size_t c = 0; c < kCalls; ++c) {
            poisonRegion(ratios.data(), kSlots, c);
            poisonRegion(amplitudes.data(), kSlots, c + 1);
            const std::array<float, kSlots> preRatios = ratios;
            const std::array<float, kSlots> preAmplitudes = amplitudes;

            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);

            // ---- structural invariants, every call ------------------------
            if (returned != kCapacity || returned > engine.capacity()) {
                structuralOk = false;
            }
            const std::size_t live = collectLiveSlots(engine, liveSlots);
            if (live != engine.getLiveChildCount() || live > engine.numChildSlots()) {
                structuralOk = false;
            }
            for (std::size_t a = 0; a < live; ++a) {
                if (liveSlots[a] < base || liveSlots[a] >= kCapacity) {
                    structuralOk = false;
                }
                for (std::size_t b = a + 1; b < live; ++b) {
                    if (liveSlots[a] == liveSlots[b]) {
                        structuralOk = false;  // no two live children share a slot
                    }
                }
            }

            // ---- the pad, HARD, over the WHOLE write region ---------------
            // [parentCount, reserveBase()) is the FR-051 gap and
            // [reserveBase(), capacity()) the owned region; every index of
            // either that is not held by a live child must be EXACTLY the
            // cloud's own padding form.
            for (std::size_t i = kParentCount; i < kCapacity; ++i) {
                bool held = false;
                for (std::size_t a = 0; a < live; ++a) {
                    if (liveSlots[a] == i) {
                        held = true;
                    }
                }
                if (held) {
                    continue;
                }
                if (ratios[i] != static_cast<float>(i + 1) || amplitudes[i] != 0.0f) {
                    padOk = false;
                }
            }

            // ---- the parent region is byte-identical to the pre-call snapshot
            const std::size_t parentBytes = std::min(kParentCount, base) * sizeof(float);
            if (std::memcmp(ratios.data(), preRatios.data(), parentBytes) != 0 ||
                std::memcmp(amplitudes.data(), preAmplitudes.data(), parentBytes) != 0) {
                parentRegionOk = false;
            }

            // ---- the out-of-capacity canary is bit-unchanged ---------------
            const std::size_t tailBytes = (kSlots - kCapacity) * sizeof(float);
            // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            if (std::memcmp(ratios.data() + kCapacity, preRatios.data() + kCapacity, tailBytes) !=
                    0 ||
                std::memcmp(amplitudes.data() + kCapacity, preAmplitudes.data() + kCapacity,
                            tailBytes) != 0) {
                canaryOk = false;
            }
            // NOLINTEND(bugprone-suspicious-memory-comparison)
        }

        INFO("calls=" << kCalls << " capacity=" << kCapacity << " reserveBase=" << base);
        REQUIRE(structuralOk);
        REQUIRE(padOk);
        REQUIRE(parentRegionOk);
        REQUIRE(canaryOk);
        REQUIRE(engine.stateFinite());
    }

    SECTION("FR-052 overlap counter: EXACTLY zero while parentCount <= reserveBase()") {
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        BloomEngine engine;
        engage(engine, ratios.data(), amplitudes.data(), kSlots, 0x51070002u, kCapacity,
               kChildSlots, kParentCount);
        REQUIRE(kParentCount <= engine.reserveBase());
        REQUIRE(engine.getOverlapEngagementCount() == std::uint32_t{0});

        bool returnedCapacity = true;
        for (std::size_t c = 0; c < 500; ++c) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kCapacity) {
                returnedCapacity = false;
            }
        }
        REQUIRE(returnedCapacity);
        REQUIRE(engine.getOverlapEngagementCount() == std::uint32_t{0});
    }

    SECTION("FR-052 overlap counter: N engaged calls give EXACTLY N at parentCount == capacity()") {
        // Without the POSITIVE direction an engine that has lost FR-052's only
        // observable passes the zero arm trivially, and Phase 10's "assert it is
        // zero" integration test is silently green forever.
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        BloomEngine engine;
        engage(engine, ratios.data(), amplitudes.data(), kSlots, 0x51070003u, kCapacity,
               kChildSlots, /*parentCount=*/kCapacity);
        REQUIRE(engine.numChildSlots() > std::size_t{0});
        REQUIRE(kCapacity > engine.reserveBase());
        // The engaging call itself was an engaged, overlapping call.
        const std::uint32_t baseline = engine.getOverlapEngagementCount();
        REQUIRE(baseline == std::uint32_t{1});

        constexpr std::size_t kN = 250;
        bool returnedCapacity = true;
        for (std::size_t c = 0; c < kN; ++c) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kCapacity,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kCapacity) {
                returnedCapacity = false;
            }
        }
        INFO("overlap count after " << kN << " engaged overlapping calls: "
                                    << engine.getOverlapEngagementCount());
        REQUIRE(returnedCapacity);
        REQUIRE(engine.getOverlapEngagementCount() == baseline + static_cast<std::uint32_t>(kN));
    }

    SECTION("buffer precondition (i): the SHORT-BUFFER canary is bit-unchanged") {
        // The canary is placed where a caller who sized its buffer from
        // capacity() - the natural mistake the S1.4 precondition exists to
        // forbid - would first be overrun, AND past index 63. Neither may move.
        constexpr std::size_t kGuard = 8;
        std::array<float, kSlots + kGuard> ratios{};
        std::array<float, kSlots + kGuard> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), ratios.size());
        poisonRegion(ratios.data() + kCapacity, ratios.size() - kCapacity, 3);
        poisonRegion(amplitudes.data() + kCapacity, amplitudes.size() - kCapacity, 5);
        const std::array<float, kSlots + kGuard> preRatios = ratios;
        const std::array<float, kSlots + kGuard> preAmplitudes = amplitudes;

        BloomEngine engine;
        prepareTriggerOnly(engine, 0x51070004u, kCapacity, kChildSlots);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(2);
        const std::size_t returned =
            forceOneEvent(engine, ratios.data(), amplitudes.data(), kParentCount);
        REQUIRE(returned == kCapacity);
        REQUIRE(engine.isEngaged());

        bool returnedCapacity = true;
        for (std::size_t c = 0; c < 100; ++c) {
            const std::size_t r =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (r != kCapacity) {
                returnedCapacity = false;
            }
        }
        const std::size_t tailBytes = (ratios.size() - kCapacity) * sizeof(float);
        REQUIRE(returnedCapacity);
        // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        REQUIRE(std::memcmp(ratios.data() + kCapacity, preRatios.data() + kCapacity, tailBytes) ==
                0);
        REQUIRE(std::memcmp(amplitudes.data() + kCapacity, preAmplitudes.data() + kCapacity,
                            tailBytes) == 0);
        // NOLINTEND(bugprone-suspicious-memory-comparison)
    }

    SECTION("buffer precondition (ii): setCapacity() RAISES the write ceiling above prepare()") {
        // THE CASE THAT PROVES WHY THE PRECONDITION IS STATED AGAINST kMaxSlots
        // AND NOT AGAINST PrepareConfig::capacity (plan S14 C-11). A caller that
        // sized its arrays from the prepare-time capacity of 16 would be overrun
        // here by 48 floats, from a control-thread call it cannot correlate with
        // its buffer length.
        constexpr std::size_t kSmallCapacity = 16;
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

        BloomEngine engine;
        prepareTriggerOnly(engine, 0x51070005u, kSmallCapacity, kChildSlots);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(2);
        REQUIRE(engine.capacity() == kSmallCapacity);
        REQUIRE(engine.reserveBase() == kSmallCapacity - kChildSlots);

        const std::size_t r0 = forceOneEvent(engine, ratios.data(), amplitudes.data(), 6);
        REQUIRE(r0 == kSmallCapacity);
        REQUIRE(engine.isEngaged());
        REQUIRE(engine.getLiveChildCount() > std::size_t{0});

        // Poison everything the SMALL capacity could never reach.
        poisonRegion(ratios.data() + kSmallCapacity, kSlots - kSmallCapacity, 7);
        poisonRegion(amplitudes.data() + kSmallCapacity, kSlots - kSmallCapacity, 11);

        engine.setCapacity(BloomEngine::kMaxSlots);
        REQUIRE(engine.capacity() == kSlots);
        REQUIRE(engine.reserveBase() == kSlots - kChildSlots);

        const std::size_t r1 = engine.processChunk(ratios.data(), amplitudes.data(), 6,
                                                   BloomEngine::kControlChunkSamples);
        REQUIRE(r1 == kSlots);

        // Every child spawned under the small capacity now sits BELOW the raised
        // reserveBase(), so it is a legacy slot that is not written - which means
        // the whole of [16, 64) must carry the pad form.
        bool raisedRegionWritten = true;
        for (std::size_t i = kSmallCapacity; i < kSlots; ++i) {
            if (ratios[i] != static_cast<float>(i + 1) || amplitudes[i] != 0.0f) {
                raisedRegionWritten = false;
            }
        }
        INFO("live children after the raise: " << engine.getLiveChildCount());
        REQUIRE(raisedRegionWritten);
        REQUIRE(engine.stateFinite());
    }

    // ==========================================================================
    // T009 - the 1 000-configuration fuzz (SC-003's main body)
    // ==========================================================================
    // Every arm above runs one hand-picked configuration; this one sweeps the
    // whole legal product - capacity in [1, 64], numChildSlots in [0, 16],
    // parentCount in [0, 64] - at the MAXIMUM spawn rate and the MAXIMUM
    // childrenPerEvent, for 30 simulated minutes each, and re-asserts every
    // structural invariant at every observation point.
    //
    // IT RUNS AT kMinUsableSampleRate (8 kHz), AND THAT IS WHAT MAKES IT
    // TRACTABLE. 30 simulated minutes is 1 800 s x 125 control steps/s = 225 000
    // control steps per configuration, against 1 350 000 at 48 kHz - a 6x
    // difference across 1 000 configurations. Every invariant asserted below is
    // a slot-accounting identity and is rate-INDEPENDENT by construction: not one
    // of them mentions a duration. SC-003 is one of the two cross-platform
    // sentinels and is never tagged [long] (plan S10.2), so its cost has to sit
    // inside the per-push lane, and the low rate additionally exercises the
    // prepare() sample-rate floor that no other arm reaches.
    //
    // THE LIFECYCLE DURATIONS ARE DRAWN PER CONFIGURATION rather than left at the
    // 45/120/180 s defaults. At the defaults a child lives 345 s against one
    // event per 20 s, so the 16-entry table saturates in the first three minutes
    // of every configuration and stays saturated - the arm would then spend 90 %
    // of its 30 minutes measuring a full table and would never once observe a
    // slot being VACATED AND RE-TAKEN, which is the accounting hazard it exists
    // to find.
    //
    // THE WRITE REGION IS POISONED BEFORE EVERY CALL, so "the slot still holds
    // the pad" can never be satisfied by a value the engine wrote earlier and
    // then stopped maintaining. The poison starts at min(pc, reserveBase()) - the
    // first index the engine may write - so the parent region it reads for
    // analysis is never corrupted.
    SECTION("SC-003: 1 000 seeded configurations, 30 simulated minutes each") {
        constexpr std::size_t kConfigs = 1000;
        constexpr double kFuzzSampleRate = BloomEngine::kMinUsableSampleRate;  // 8 kHz
        constexpr std::size_t kStepsPerSecond = 125;  // 8000 / 64, exact
        constexpr std::size_t kSecondsPerCall = 10;   // the COARSE grid
        constexpr std::size_t kCallSamples =
            BloomEngine::kControlChunkSamples * kStepsPerSecond * kSecondsPerCall;
        constexpr std::size_t kCalls = 180;  // 1 800 s = 30 simulated minutes
        constexpr std::size_t kGuard = 8;

        Krate::DSP::Xorshift32 rng{0x5C003F00u};

        bool structuralOk = true;
        bool returnOk = true;
        bool overlapOk = true;
        bool padOk = true;
        bool parentRegionOk = true;
        bool canaryOk = true;
        bool finiteOk = true;
        std::size_t engagedConfigs = 0;
        std::uint64_t totalSpawned = 0;
        std::uint64_t totalCompleted = 0;
        std::size_t maxLiveObserved = 0;

        std::array<float, kSlots + kGuard> ratios{};
        std::array<float, kSlots + kGuard> amplitudes{};
        std::array<std::size_t, BloomEngine::kMaxChildren> liveSlots{};

        for (std::size_t c = 0; c < kConfigs; ++c) {
            const std::size_t capacity = std::size_t{1} + (rng.next() % 64u);
            const std::size_t childSlots = rng.next() % 17u;
            const std::size_t parentCount = rng.next() % 65u;
            // Short, drawn lifecycles - see the arm banner. Mean lifetime is
            // ~26 s against one event per 20 s at the maximum spawn rate, so a
            // configuration both FILLS its table and EMPTIES it several times
            // inside its 30 simulated minutes, which is the only regime in which
            // a vacate-and-retake accounting bug can show.
            const float fadeIn = 1.0f + static_cast<float>(rng.next() % 10u);
            const float hold = static_cast<float>(rng.next() % 31u);
            const float fadeOut = 1.0f + static_cast<float>(rng.next() % 10u);

            for (std::size_t i = 0; i < ratios.size(); ++i) {
                ratios[i] = static_cast<float>(i + 1);
                amplitudes[i] = (i < parentCount) ? 1.0f / static_cast<float>(i + 1) : 0.0f;
            }
            poisonRegion(ratios.data() + kSlots, kGuard, c);
            poisonRegion(amplitudes.data() + kSlots, kGuard, c + 1);
            const std::array<float, kSlots + kGuard> preRatios = ratios;
            const std::array<float, kSlots + kGuard> preAmplitudes = amplitudes;

            BloomEngine engine;
            engine.setSeed(0xF0220000u + static_cast<std::uint32_t>(c));
            engine.prepare(kFuzzSampleRate, BloomEngine::PrepareConfig{
                                                .capacity = capacity,
                                                .numChildSlots = childSlots});
            engine.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
            engine.setChildrenPerEvent(BloomEngine::kMaxChildrenPerEvent);
            engine.setParentCount(BloomEngine::kMaxParents);
            engine.setFadeInSeconds(fadeIn);
            engine.setHoldSeconds(hold);
            engine.setFadeOutSeconds(fadeOut);

            const std::size_t base = engine.reserveBase();
            const std::size_t pc = std::min(parentCount, capacity);
            const std::size_t writeStart = std::min(pc, base);
            const std::size_t parentBytes = std::min(parentCount, base) * sizeof(float);

            for (std::size_t call = 0; call < kCalls; ++call) {
                poisonRegion(ratios.data() + writeStart, capacity - writeStart, call);
                poisonRegion(amplitudes.data() + writeStart, capacity - writeStart, call + 1u);

                const std::size_t returned = engine.processChunk(
                    ratios.data(), amplitudes.data(), parentCount, kCallSamples);

                // ---- the return contract ---------------------------------
                // A call that wrote nothing returns the caller's UNCLAMPED
                // parentCount (SC-014 (f)); an engaged call returns capacity().
                // Stating it this way rather than as "returned <= capacity()" is
                // deliberate: with capacity 16 and parentCount 64 a DISENGAGED
                // engine legitimately returns 64, and the weaker form would
                // report that as a violation.
                if (engine.isEngaged()) {
                    if (returned != capacity) {
                        returnOk = false;
                    }
                } else if (returned != parentCount) {
                    returnOk = false;
                }

                // ---- the live table --------------------------------------
                const std::size_t live = collectLiveSlots(engine, liveSlots);
                maxLiveObserved = std::max(maxLiveObserved, live);
                if (live != engine.getLiveChildCount() || live > engine.numChildSlots()) {
                    structuralOk = false;
                }
                std::uint64_t liveMask = 0u;
                for (std::size_t a = 0; a < live; ++a) {
                    if (liveSlots[a] < base || liveSlots[a] >= capacity) {
                        structuralOk = false;
                        continue;
                    }
                    const std::uint64_t bit = std::uint64_t{1} << liveSlots[a];
                    if ((liveMask & bit) != 0u) {
                        structuralOk = false;  // no two live children share a slot
                    }
                    liveMask |= bit;
                }

                // ---- FR-052, the negative direction ----------------------
                if (parentCount <= base && engine.getOverlapEngagementCount() != 0u) {
                    overlapOk = false;
                }

                // ---- the pad, over the WHOLE write region -----------------
                if (engine.isEngaged()) {
                    for (std::size_t i = writeStart; i < capacity; ++i) {
                        if ((liveMask & (std::uint64_t{1} << i)) != 0u) {
                            continue;
                        }
                        if (ratios[i] != static_cast<float>(i + 1) || amplitudes[i] != 0.0f) {
                            padOk = false;
                        }
                    }
                }

                // ---- the parent region and the out-of-capacity canary -----
                if (parentBytes > 0 && (std::memcmp(ratios.data(), preRatios.data(),
                                                    parentBytes) != 0 ||
                                        std::memcmp(amplitudes.data(), preAmplitudes.data(),
                                                    parentBytes) != 0)) {
                    parentRegionOk = false;
                }
                // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
                if (std::memcmp(ratios.data() + kSlots, preRatios.data() + kSlots,
                                kGuard * sizeof(float)) != 0 ||
                    std::memcmp(amplitudes.data() + kSlots, preAmplitudes.data() + kSlots,
                                kGuard * sizeof(float)) != 0) {
                    canaryOk = false;
                }
                // NOLINTEND(bugprone-suspicious-memory-comparison)
                if (!engine.stateFinite()) {
                    finiteOk = false;
                }
            }

            if (engine.isEngaged()) {
                ++engagedConfigs;
            }
            totalSpawned += engine.getSpawnedChildCount();
            totalCompleted += engine.getCompletedChildCount();
        }

        INFO("configs=" << kConfigs << " engaged=" << engagedConfigs
                        << " spawned=" << totalSpawned << " completed=" << totalCompleted
                        << " max live observed=" << maxLiveObserved);
        REQUIRE(structuralOk);
        REQUIRE(returnOk);
        REQUIRE(overlapOk);
        REQUIRE(padOk);
        REQUIRE(parentRegionOk);
        REQUIRE(canaryOk);
        REQUIRE(finiteOk);
        // Non-vacuity: the sweep must actually have grown AND retired children,
        // or every invariant above is satisfied by an engine that does nothing.
        REQUIRE(engagedConfigs > std::size_t{0});
        REQUIRE(totalSpawned > std::uint64_t{0});
        REQUIRE(totalCompleted > std::uint64_t{0});
        REQUIRE(maxLiveObserved > std::size_t{1});
    }

    // ==========================================================================
    // T009 - FR-032: the death arm
    // ==========================================================================
    // THE DIRECT TEST that a retired child's slot is REPADDED rather than left
    // sounding forever at its last fade-out value. Without it, "the drone grows
    // and never dies back" passes SC-001, SC-003, SC-004, SC-014, SC-015 and
    // SC-016 unchanged, because every one of those reads either the engine's own
    // counters or a region the child never occupied.
    SECTION("FR-032: a retired child's slot is repadded on the very step it completes") {
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        BloomEngine engine;
        fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);
        prepareTriggerOnly(engine, 0x51070006u, kCapacity, kChildSlots);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(1);
        // A 1 s fade-in, no hold, a 1 s fade-out: 1 500 control steps at 48 kHz.
        // holdSeconds == 0 makes the latched hold exactly 0 whatever the jitter
        // draw is, so the completion step is known in advance and the assertion
        // below is on an EXACT step rather than on "eventually".
        engine.setFadeInSeconds(1.0f);
        engine.setHoldSeconds(0.0f);
        engine.setFadeOutSeconds(1.0f);

        const std::size_t spawnReturn =
            forceOneEvent(engine, ratios.data(), amplitudes.data(), kParentCount);
        REQUIRE(spawnReturn == kCapacity);
        REQUIRE(engine.getLiveChildCount() == std::size_t{1});

        std::array<std::size_t, BloomEngine::kMaxChildren> liveSlots{};
        REQUIRE(collectLiveSlots(engine, liveSlots) == std::size_t{1});
        const std::size_t slot = liveSlots[0];
        const std::size_t base = engine.reserveBase();
        REQUIRE(slot >= base);
        REQUIRE(slot < kCapacity);

        bool returnedCapacity = true;
        std::size_t steps = 0;
        while (engine.getCompletedChildCount() == std::uint64_t{0} && steps < 10000u) {
            // Poison the whole write region INCLUDING the dying child's slot, so
            // the value read back after the retiring call must have been WRITTEN
            // by this call and cannot be a leftover.
            poisonRegion(ratios.data() + kParentCount, kCapacity - kParentCount, steps);
            poisonRegion(amplitudes.data() + kParentCount, kCapacity - kParentCount, steps + 1u);
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kCapacity) {
                returnedCapacity = false;
            }
            ++steps;
        }

        INFO("child completed after " << steps << " control steps (want 1 500)");
        REQUIRE(returnedCapacity);
        REQUIRE(engine.getCompletedChildCount() == std::uint64_t{1});
        REQUIRE(engine.getLiveChildCount() == std::size_t{0});
        REQUIRE(steps == std::size_t{1500});
        // ON THAT SAME CHUNK: the vacated slot carries the cloud's own padding
        // form (harmonic_cloud.h:825-826), not the child's last fade-out value.
        REQUIRE(ratios[slot] == static_cast<float>(slot + 1));
        REQUIRE(amplitudes[slot] == 0.0f);
        REQUIRE(engine.stateFinite());
    }

    // ==========================================================================
    // T009 - Clarification Q8: engaged_ is STICKY
    // ==========================================================================
    // An implementation that fell back to the pass-through once
    // getLiveChildCount() reached 0 would stop padding the gap, and the caller's
    // stale bytes - whatever they happen to be - would sound. engaged_ is set
    // ONCE, in the S4.5 latch, and cleared only by prepare()/reset().
    SECTION("Clarification Q8: the pad continues after the last child has died") {
        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        BloomEngine engine;
        fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);
        prepareTriggerOnly(engine, 0x51070007u, kCapacity, kChildSlots);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(1);
        engine.setFadeInSeconds(1.0f);
        engine.setHoldSeconds(0.0f);
        engine.setFadeOutSeconds(1.0f);

        const std::size_t spawnReturn =
            forceOneEvent(engine, ratios.data(), amplitudes.data(), kParentCount);
        REQUIRE(spawnReturn == kCapacity);

        bool ok = true;
        std::size_t steps = 0;
        while (engine.getCompletedChildCount() == std::uint64_t{0} && steps < 10000u) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kCapacity) {
                ok = false;
            }
            ++steps;
        }
        REQUIRE(ok);
        REQUIRE(engine.getLiveChildCount() == std::size_t{0});
        REQUIRE(engine.isEngaged());

        // capacity() - 1 further calls with an EMPTY table: still engaged, still
        // returning capacity(), still padding the whole write region.
        bool stillPadding = true;
        bool stillCapacity = true;
        for (std::size_t c = 0; c + 1 < kCapacity; ++c) {
            poisonRegion(ratios.data() + kParentCount, kCapacity - kParentCount, c);
            poisonRegion(amplitudes.data() + kParentCount, kCapacity - kParentCount, c + 1u);
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                    BloomEngine::kControlChunkSamples);
            if (returned != kCapacity) {
                stillCapacity = false;
            }
            for (std::size_t i = kParentCount; i < kCapacity; ++i) {
                if (ratios[i] != static_cast<float>(i + 1) || amplitudes[i] != 0.0f) {
                    stillPadding = false;
                }
            }
        }
        INFO("post-death calls = " << kCapacity - 1);
        REQUIRE(stillCapacity);
        REQUIRE(stillPadding);
        REQUIRE(engine.isEngaged());
        REQUIRE(engine.getLiveChildCount() == std::size_t{0});
    }

    // ==========================================================================
    // T010 - the CAPACITY arm (Clarification Q3, FR-055, FR-053)
    // ==========================================================================
    // setCapacity() is a CONTROL-THREAD call that moves the write ceiling under a
    // running engine, and the two directions are deliberately asymmetric:
    //
    //   GROWTH is immediate   - peekSlot() re-clamps the cursor into the new
    //                           range AS A LOCAL, so the new slots are spawnable
    //                           on the very next control step.
    //   SHRINKAGE is DEFERRED - a live child above the new capacity is NOT
    //                           killed, retimed or evicted. Killing it would cut
    //                           a 45-second swell mid-fade, which is the click
    //                           FR-031 exists to prevent. It keeps advancing its
    //                           latched lifecycle, is skipped by the write loop,
    //                           and frees its slot on completion.
    //
    // THE BUG THIS ARM IS SHAPED AGAINST is a numChildSlots() that OVERWRITES the
    // request instead of re-deriving min(request, capacity) on every read: a
    // shrink to 32 would permanently clamp the reserve, so the grow back to 64
    // would silently never restore it. numChildSlots() is asserted through the
    // shrink, and the grow-back's slot re-use is asserted through FR-053.
    //
    // 8 kHz (kMinUsableSampleRate) gives 125 control steps per second EXACTLY, so
    // "30 simulated seconds" is 3 750 steps and every latched bound below is an
    // exact integer: fadeIn 250, hold 3 750 (jitter disabled), fadeOut 250, i.e.
    // a 4 250-step / 34-second life that OUTLASTS the 30-second shrink window.
    // ==========================================================================
    SECTION("Clarification Q3 / FR-055: a live setCapacity() shrink is DEFERRED") {
        constexpr std::size_t kRuns = 200;
        constexpr double kCapFs = BloomEngine::kMinUsableSampleRate;
        constexpr std::size_t kStepsPerSecond = 125;  // 8000 / 64, exact
        constexpr std::size_t kWide = 64;
        constexpr std::size_t kNarrow = 32;
        constexpr std::size_t kCapChildSlots = 8;
        constexpr std::size_t kCapParents = 16;
        constexpr float kControlDt = 64.0f / 8000.0f;

        constexpr float kTrackedFadeIn = 2.0f;
        constexpr float kTrackedHold = 30.0f;
        constexpr float kTrackedFadeOut = 2.0f;
        constexpr std::size_t kFadeInSteps = 250;   // round(2 * 125)
        constexpr std::size_t kHoldSteps = 3750;    // round(30 * 125), jitter disabled
        constexpr std::size_t kFadeOutSteps = 250;  // round(2 * 125)
        constexpr std::size_t kLifeSteps = kFadeInSteps + kHoldSteps + kFadeOutSteps;
        constexpr std::size_t kShrinkWindow = 30 * kStepsPerSecond;  // 30 simulated seconds
        static_assert(kShrinkWindow < kLifeSteps,
                      "the tracked children must SURVIVE the shrink window, or the grow-back "
                      "has no legacy child left to prove FR-053 against");
        constexpr std::size_t kCanaryFloats = kWide - kNarrow;
        constexpr std::size_t kCanaryBytes = kCanaryFloats * sizeof(float);
        constexpr std::size_t kReuseEvents = 12;
        constexpr std::size_t kReuseGap = 300;  // > the 250-step life of a short child

        bool writeCeilingOk = true;   // nothing at or above the new capacity is touched
        bool elapsedOk = true;        // a legacy child's clock keeps running, unshifted
        bool phaseOk = true;          // and its transitions land at the LATCHED offsets
        bool uniqueOk = true;         // no two live children share a slot (FR-053)
        bool slotHeldOk = true;       // a legacy child keeps its own slot while it lives
        bool returnOk = true;         // an engaged call returns the CURRENT capacity
        bool newChildInRangeOk = true;  // no NEW child lands at or above the new capacity
        bool reuseAfterDeathOk = true;  // and the slot IS re-usable once it dies

        std::array<std::size_t, BloomEngine::kMaxChildren> liveSlots{};

        for (std::size_t run = 0; run < kRuns; ++run) {
            std::array<float, kSlots> ratios{};
            std::array<float, kSlots> amplitudes{};
            fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);

            BloomEngine engine;
            engine.setSeed(0xCA9A0000u + static_cast<std::uint32_t>(run));
            engine.prepare(kCapFs, BloomEngine::PrepareConfig{.capacity = kWide,
                                                             .numChildSlots = kCapChildSlots});
            engine.setSpawnRateHz(0.0f);  // triggerBloom() is the only source
            engine.setParentCount(4);
            engine.setChildrenPerEvent(2);
            engine.setHoldJitterFraction(0.0f);  // the latched hold is EXACTLY kHoldSteps
            engine.setFadeInSeconds(kTrackedFadeIn);
            engine.setHoldSeconds(kTrackedHold);
            engine.setFadeOutSeconds(kTrackedFadeOut);

            const std::size_t spawnReturn =
                forceOneEvent(engine, ratios.data(), amplitudes.data(), kCapParents);
            REQUIRE(spawnReturn == kWide);
            REQUIRE(engine.getLiveChildCount() == std::size_t{2});

            // The two children of that one event. The cursor starts at
            // reserveBase() == 56, so both sit in [56, 64) - which the shrink
            // below turns into LEGACY slots.
            std::array<std::size_t, BloomEngine::kMaxChildren> trackedEntry{};
            std::array<std::size_t, BloomEngine::kMaxChildren> trackedSlot{};
            std::array<float, BloomEngine::kMaxChildren> prevElapsed{};
            std::size_t tracked = 0;
            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                const std::size_t s = engine.getChildSlotIndex(i);
                if (s < BloomEngine::kMaxSlots) {
                    trackedEntry[tracked] = i;
                    trackedSlot[tracked] = s;
                    prevElapsed[tracked] = engine.getChildElapsedSeconds(i);
                    ++tracked;
                }
            }
            REQUIRE(tracked == std::size_t{2});
            REQUIRE(trackedSlot[0] >= kNarrow);
            REQUIRE(trackedSlot[1] >= kNarrow);
            // The quantity being asserted is the latched STEP COUNT (kHoldSteps),
            // read back through getChildHoldSeconds()'s float conversion
            // `float(holdSteps) * controlDtSec_`. It is compared with a tolerance
            // of a QUARTER of a control step, which pins the integer uniquely
            // (adjacent step counts are a whole kControlDt apart) - the same idiom
            // and the same reason as bloom_engine_spectral_test.cpp:267-273.
            //
            // An exact `==` against the compile-time product is NOT a valid
            // structural identity here: the tests build under /fp:fast (see
            // dsp/tests/CMakeLists.txt:841-918), which folds the constexpr
            // `float(3750) * (64.0f / 8000.0f)` in DOUBLE and yields exactly 30.0,
            // while the engine's runtime float chain yields 30.0000019 (float
            // 64/8000 is 0.00800000038). Measured with MSVC 14.44 /O2 /fp:fast:
            // test side 30, engine side 30.0000019, equal == 0.
            REQUIRE(std::fabs(engine.getChildHoldSeconds(trackedEntry[0]) -
                              static_cast<float>(kHoldSteps) * kControlDt) < 0.25f * kControlDt);

            // Every LATER child is short-lived, so the triggers below free their
            // slots again quickly. An in-flight child keeps its LATCHED bounds
            // (FR-033), so this cannot retime the two tracked children - which is
            // precisely what the phase assertions re-prove, step by step.
            engine.setFadeInSeconds(1.0f);
            engine.setHoldSeconds(0.0f);
            engine.setFadeOutSeconds(1.0f);

            engine.setCapacity(kNarrow);
            REQUIRE(engine.capacity() == kNarrow);
            REQUIRE(engine.numChildSlots() == kCapChildSlots);  // RE-DERIVED, not overwritten
            REQUIRE(engine.reserveBase() == kNarrow - kCapChildSlots);

            // The canary is planted AFTER the shrink: before it, [32, 64) was a
            // legitimate part of the write region. Nothing re-poisons it, so any
            // write the engine makes there persists and is caught at the end of
            // the window.
            poisonRegion(ratios.data() + kNarrow, kCanaryFloats, run);
            poisonRegion(amplitudes.data() + kNarrow, kCanaryFloats, run + 1u);
            const std::array<float, kSlots> canaryRatios = ratios;
            const std::array<float, kSlots> canaryAmplitudes = amplitudes;

            for (std::size_t k = 1; k <= kLifeSteps; ++k) {
                if (k == kShrinkWindow + 1) {
                    // End of the 30 simulated seconds: the write ceiling held.
                    // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - bit-exact check
                    if (std::memcmp(ratios.data() + kNarrow, canaryRatios.data() + kNarrow,
                                    kCanaryBytes) != 0 ||
                        std::memcmp(amplitudes.data() + kNarrow,
                                    canaryAmplitudes.data() + kNarrow, kCanaryBytes) != 0) {
                        writeCeilingOk = false;
                    }
                    // NOLINTEND(bugprone-suspicious-memory-comparison)
                    engine.setCapacity(kWide);  // GROWTH: effective immediately
                }
                const std::size_t expectCapacity = (k <= kShrinkWindow) ? kNarrow : kWide;

                // Two triggers inside the shrink window and three after the
                // grow-back. Their children must never take a slot a legacy
                // child still holds (FR-053) and, while the capacity is 32, must
                // never land at or above it.
                if (k == 500u || k == 1500u || k == kShrinkWindow + 50u ||
                    k == kShrinkWindow + 150u || k == kShrinkWindow + 250u) {
                    engine.triggerBloom();
                }

                const std::size_t returned =
                    engine.processChunk(ratios.data(), amplitudes.data(), kCapParents,
                                        BloomEngine::kControlChunkSamples);
                if (returned != expectCapacity) {
                    returnOk = false;
                }

                // ---- no two live children share a slot (FR-053) -------------
                const std::size_t live = collectLiveSlots(engine, liveSlots);
                std::uint64_t liveMask = 0u;
                for (std::size_t i = 0; i < live; ++i) {
                    const std::uint64_t bit = std::uint64_t{1} << liveSlots[i];
                    if ((liveMask & bit) != 0u) {
                        uniqueOk = false;
                    }
                    liveMask |= bit;
                    if (k <= kShrinkWindow && liveSlots[i] >= kNarrow &&
                        liveSlots[i] != trackedSlot[0] && liveSlots[i] != trackedSlot[1]) {
                        newChildInRangeOk = false;
                    }
                }

                // ---- the legacy children keep advancing, UNSHIFTED ----------
                for (std::size_t i = 0; i < tracked; ++i) {
                    const std::size_t t = trackedEntry[i];
                    const BloomEngine::Phase expected = [&]() -> BloomEngine::Phase {
                        if (k < kFadeInSteps) {
                            return BloomEngine::Phase::FadeIn;
                        }
                        if (k < kFadeInSteps + kHoldSteps) {
                            return BloomEngine::Phase::Hold;
                        }
                        if (k < kLifeSteps) {
                            return BloomEngine::Phase::FadeOut;
                        }
                        return BloomEngine::Phase::Idle;
                    }();
                    if (engine.getChildPhase(t) != expected) {
                        phaseOk = false;
                    }
                    if (k < kLifeSteps) {
                        // The clock is an INDEPENDENT one - the test's own step
                        // count - so an implementation that RESTARTED the child
                        // at the shrink (elapsed back to 0, phases self-
                        // consistent) is caught here and nowhere else.
                        const float elapsed = engine.getChildElapsedSeconds(t);
                        if (std::fabs(elapsed - static_cast<float>(k) * kControlDt) >
                            0.25f * kControlDt) {
                            elapsedOk = false;
                        }
                        if (!(elapsed > prevElapsed[i])) {
                            elapsedOk = false;
                        }
                        prevElapsed[i] = elapsed;
                        if (engine.getChildSlotIndex(t) != trackedSlot[i]) {
                            slotHeldOk = false;
                        }
                    }
                }
            }

            // Both tracked children retired on step kLifeSteps exactly.
            REQUIRE(engine.getCompletedChildCount() >= std::uint64_t{2});

            // ---- FR-053's POSITIVE direction --------------------------------
            // Their slots were refused to every child above while they lived;
            // now that they are gone, the round-robin must hand them out again.
            bool reusedFirst = false;
            bool reusedSecond = false;
            for (std::size_t e = 0; e < kReuseEvents && !(reusedFirst && reusedSecond); ++e) {
                engine.triggerBloom();
                const std::size_t returned =
                    engine.processChunk(ratios.data(), amplitudes.data(), kCapParents,
                                        BloomEngine::kControlChunkSamples);
                if (returned != kWide) {
                    returnOk = false;
                }
                const std::size_t live = collectLiveSlots(engine, liveSlots);
                for (std::size_t i = 0; i < live; ++i) {
                    if (liveSlots[i] == trackedSlot[0]) {
                        reusedFirst = true;
                    }
                    if (liveSlots[i] == trackedSlot[1]) {
                        reusedSecond = true;
                    }
                }
                static_cast<void>(stepEngine(engine, ratios.data(), amplitudes.data(),
                                             kCapParents, kReuseGap));
            }
            if (!reusedFirst || !reusedSecond) {
                reuseAfterDeathOk = false;
            }
        }

        INFO("runs = " << kRuns << ", shrink window = " << kShrinkWindow
                       << " control steps, tracked life = " << kLifeSteps);
        REQUIRE(writeCeilingOk);
        REQUIRE(elapsedOk);
        REQUIRE(phaseOk);
        REQUIRE(uniqueOk);
        REQUIRE(slotHeldOk);
        REQUIRE(returnOk);
        REQUIRE(newChildInRangeOk);
        REQUIRE(reuseAfterDeathOk);
    }
}

// ==============================================================================
// T011 - BloomEngine_OwnedSlotRoundRobinRotation (SC-018; FR-056, FR-043)
// ==============================================================================
// FR-056 places a spawning child at the slot found by advancing a ROUND-ROBIN
// CURSOR over [reserveBase(), capacity()), wrapping at capacity() and skipping
// occupied slots, with the cursor always advancing PAST the slot it selected.
// Why that matters is Overview fact 2: the slot a child lands in determines the
// drift / pan / mutation lane it inherits from HarmonicCloud, so an engine that
// collapsed onto the lowest free index would give every child of a session the
// SAME lane and the bloom would read as one partial doubling over and over.
//
// The whole case is driven by triggerBloom(), NEVER by the FR-041 internal clock
// (plan S14 C-6). A seeded clock arms at DIFFERENT control steps under a
// different seed, and clause (d) - "the slot sequence is identical under a
// different seed" - would then be measuring when the clock fired rather than
// whether slot choice consumed a draw. With setSpawnRateHz(0) the clock's
// probability is exactly 0 and the trigger is the only event source, so the two
// runs of clause (d) are step-for-step aligned by construction.
//
// The fixture spectrum is fillWellSpacedParents, for the reason its own doc
// block gives: on that spectrum FR-021/FR-022 never fire, so EVERY offered child
// is placed on its first attempt regardless of what the draws returned. That is
// load-bearing for clause (d) - a seed-dependent REJECTION would mean a
// seed-dependent number of commitSlot() calls, and the slot sequences of two
// seeds would legitimately differ on correct code.
// ==============================================================================
namespace {

/// SC-018's owned-region width. capacity() is full width, so the owned region is
/// [kMaxSlots - 8, kMaxSlots) == [56, 64).
constexpr std::size_t kRotationChildSlots = 8;

/// The first owned slot: reserveBase() for the rig below.
constexpr std::size_t kRotationReserveBase =
    Krate::DSP::BloomEngine::kMaxSlots - kRotationChildSlots;

/// SC-018's cycle count.
constexpr std::size_t kRotationCycles = 200;

/// SC-018 (a): full rotation must complete inside this many cycles (8 slots,
/// plus one spare cycle for an occupied-slot skip).
constexpr std::size_t kRotationCoverageCycles = 16;

/// SC-018 (c) runs at childrenPerEvent = 2; 16 events is four full rotations of
/// the 8-slot region, so "the cursor still advances" is observed repeatedly.
constexpr std::size_t kSimultaneousEvents = 16;

/// The rig runs at the MINIMUM usable rate (resonance_drift_network.h:281, the
/// figure BloomEngine restates). That is not a shortcut: the control rate is
/// then 8000/64 = 125 steps per second, so the shortest configurable lifecycle
/// (kMinFadeInSeconds + 0 hold + kMinFadeOutSeconds) is 250 control steps, and
/// 200 cycles cost ~50 000 processChunk calls instead of ~300 000 at 48 kHz.
/// Nothing in SC-018 is rate-dependent - the cursor is integer bookkeeping.
constexpr double kRotationSampleRate = Krate::DSP::BloomEngine::kMinUsableSampleRate;

/// Loop guard. The shortest lifecycle is 250 control steps at this rate; 2000 is
/// an order of margin, and a cycle that hits it is reported as a failure rather
/// than hanging the suite.
constexpr std::size_t kMaxStepsPerCycle = 2000;

/// Trace capacity: every cycle may record up to kMaxChildrenPerEvent slots.
constexpr std::size_t kMaxRotationTrace =
    kRotationCycles * Krate::DSP::BloomEngine::kMaxChildrenPerEvent;

/// @brief The recorded slot sequence plus the invariants checked WHILE recording.
///
/// The per-step observations accumulate into flags and are asserted once, after
/// the run: a REQUIRE inside a 50 000-iteration loop would register 50 000
/// assertions and report the first failure without its cycle index.
struct RotationTrace {
    std::array<std::size_t, kMaxRotationTrace> slots{};
    std::size_t count = 0;
    bool everyEventPlacedAll = true;         ///< every event placed childrenPerEvent children
    bool everySlotDistinctWhileLive = true;  ///< SC-003's no-shared-slot invariant, re-checked
    bool everySlotInOwnedRegion = true;      ///< every slot lies in [reserveBase, capacity)
    bool everyReturnWasCapacity = true;      ///< FR-051: an engaged engine returns capacity()
    bool everyCycleCompleted = true;         ///< every child retired inside the guard
    bool stateAlwaysFinite = true;
};

/// @brief prepare() a TRIGGER-ONLY engine with the shortest configurable
///        lifecycle, so one spawn/death cycle is 250 control steps.
///
/// setHoldSeconds(0) is legal (FR-030 admits it) and setHoldJitterFraction(0)
/// makes the latched hold exactly 0 steps, so every cycle of every run has the
/// SAME length - which is what keeps the two seeds of clause (d) aligned
/// step-for-step. setSeed() before prepare() is safe: prepare()'s step (6)
/// re-applies seed_, which survives (FR-004).
void prepareRotationRig(Krate::DSP::BloomEngine& engine, std::uint32_t seed,
                        std::size_t childrenPerEvent) noexcept {
    using Krate::DSP::BloomEngine;
    engine.setSeed(seed);
    engine.prepare(kRotationSampleRate,
                   BloomEngine::PrepareConfig{.capacity = BloomEngine::kMaxSlots,
                                              .numChildSlots = kRotationChildSlots});
    engine.setSpawnRateHz(0.0f);  // the FR-041 clock OFF - plan S14 C-6
    engine.setChildrenPerEvent(childrenPerEvent);
    engine.setFadeInSeconds(BloomEngine::kMinFadeInSeconds);
    engine.setHoldSeconds(0.0f);
    engine.setFadeOutSeconds(BloomEngine::kMinFadeOutSeconds);
    engine.setHoldJitterFraction(0.0f);
}

/// @brief One trigger-driven spawn/death cycle: arm, run ONE control step (the
///        event resolves there), record the slots, then step until the table is
///        empty again.
///
/// The slots are recorded in TABLE-ENTRY order through collectLiveSlots(), which
/// is deterministic: place() takes the lowest Idle entry (findFreeTableEntry),
/// and with one event's children alive at a time entry order IS spawn order.
void runRotationCycles(std::uint32_t seed, std::size_t childrenPerEvent, std::size_t cycles,
                       RotationTrace& trace) noexcept {
    using Krate::DSP::BloomEngine;

    BloomEngine engine;
    prepareRotationRig(engine, seed, childrenPerEvent);

    std::array<float, BloomEngine::kMaxSlots> ratios{};
    std::array<float, BloomEngine::kMaxSlots> amplitudes{};
    fillWellSpacedParents(ratios.data(), amplitudes.data(), BloomEngine::kMaxSlots);

    std::array<std::size_t, BloomEngine::kMaxChildren> live{};

    const auto checkLiveSet = [&trace](const std::array<std::size_t, BloomEngine::kMaxChildren>& s,
                                       std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            if (s[i] < kRotationReserveBase || s[i] >= BloomEngine::kMaxSlots) {
                trace.everySlotInOwnedRegion = false;
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (s[i] == s[j]) {
                    trace.everySlotDistinctWhileLive = false;
                }
            }
        }
    };

    for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
        // The event resolves on this one control step: advanceChildren() runs
        // first (nothing is live), then runEvent() latches the children.
        engine.triggerBloom();
        const std::size_t spawnReturn =
            engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                BloomEngine::kControlChunkSamples);
        if (spawnReturn != BloomEngine::kMaxSlots) {
            trace.everyReturnWasCapacity = false;
        }
        if (!engine.stateFinite()) {
            trace.stateAlwaysFinite = false;
        }

        const std::size_t placed = collectLiveSlots(engine, live);
        if (placed != childrenPerEvent) {
            trace.everyEventPlacedAll = false;
        }
        checkLiveSet(live, placed);
        for (std::size_t i = 0; i < placed && trace.count < trace.slots.size(); ++i) {
            trace.slots[trace.count] = live[i];
            ++trace.count;
        }

        std::size_t guard = 0;
        while (engine.getLiveChildCount() > 0 && guard < kMaxStepsPerCycle) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                    BloomEngine::kControlChunkSamples);
            if (returned != BloomEngine::kMaxSlots) {
                trace.everyReturnWasCapacity = false;
            }
            const std::size_t stillLive = collectLiveSlots(engine, live);
            checkLiveSet(live, stillLive);
            ++guard;
        }
        if (engine.getLiveChildCount() != 0) {
            trace.everyCycleCompleted = false;
        }
    }
}

/// @brief The parent spectrum of the PEEK-vs-COMMIT arm: ONE parent whose
///        octave is IN the FR-021 band, one whose octave is OUT of it, and two
///        NON-PARENT partials that flank the in-band octave's FR-026 fallback
///        band.
///
/// THE ARM NEEDS A CHILD THAT IS PEEKED AND NEVER PLACED, and getting one is
/// harder than it looks, because FR-026's retry RE-DRAWS THE PARENT (S4.2 puts
/// draw (d1) inside the attempt loop) and FR-016 draws without replacement: with
/// kMaxSpawnAttempts == 4 and K <= 4 selected parents, every selected parent is
/// visited before the attempts run out, so a single in-band parent rescues EVERY
/// child and offered == spawned. (That is exactly what an earlier version of
/// this fixture - {1.0, 1.3, 70, 90}, all four selected - measured: 400 offered,
/// 400 spawned, and the non-vacuity REQUIRE below failed on correct code.)
///
/// The construction that does produce a refusal, per event and by construction:
///   index 0: ratio 1.0, amplitude 1.0   - PARENT, octave candidate 2.0;
///   index 1: ratio 70.0, amplitude 0.9  - PARENT, octave candidate 140, above
///            kMaxChildRatio (128), rejected on every attempt AND on the FR-026
///            detuned-octave fallback (140 * 2^(+/-50/1200) is in [136, 144],
///            still above 128);
///   index 2/3: ratios 2 * 2^(-/+37/1200), amplitudes 0.5 / 0.4 - PARTIALS IN
///            THE ARRAY, so FR-022 measures against them, but NOT parents,
///            because the rig pins the FR-010 K-selection to the two strongest
///            (setParentCount(2)). They sit 37 cents either side of 2.0.
///
/// What that gives, for every event at childrenPerEvent == 2:
///   * the FIRST child is always PLACED at 2.0 - 37 cents of clearance against
///     both blockers is more than FR-022's 24, and the retry reaches parent 1.0
///     within two attempts however the draws fall;
///   * the SECOND child is always REFUSED - its octave via parent 1.0 collides
///     with the sibling at 2.0 (0 cents, FR-016's sibling clause), its octave
///     via parent 70 is out of band, and the final-attempt fallback lands in
///     +/-[24, 50] cents of 2.0, i.e. within 13 cents of a blocker, on every
///     draw in the band.
/// So offered == 2 x spawned, and every refused child was PEEKED first - the
/// only state in which advancing the cursor in peekSlot() and advancing it in
/// commitSlot() differ.
///
/// The refusal is DETERMINISTIC rather than seed-dependent, and that is what the
/// clause needs: the SLOT SEQUENCE must be the bare round robin indexed by
/// PLACEMENT ordinal under every seed, which is precisely what a cursor advanced
/// on a peek breaks (it would step 56, 58, 60, 62, ... - two per event).
void fillBoundsSplitParents(float* ratios, float* amplitudes, std::size_t n) noexcept {
    // 37 cents is the midpoint of the FR-020 detune band [24, 50], so a fallback
    // anywhere in that band is at most 13 cents from a blocker - comfortably
    // inside FR-022's 24-cent rejection radius at both ends.
    const float kBlockerOffsetCents = 37.0f;
    const float kBlockerLow = 2.0f * std::exp2(-kBlockerOffsetCents / 1200.0f);
    const float kBlockerHigh = 2.0f * std::exp2(kBlockerOffsetCents / 1200.0f);
    const std::array<float, 4> kParentRatios{1.0f, 70.0f, kBlockerLow, kBlockerHigh};
    const std::array<float, 4> kParentAmps{1.0f, 0.9f, 0.5f, 0.4f};
    for (std::size_t i = 0; i < n; ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    for (std::size_t i = 0; i < kParentRatios.size() && i < n; ++i) {
        ratios[i] = kParentRatios[i];
        amplitudes[i] = kParentAmps[i];
    }
}

/// Number of parents fillBoundsSplitParents writes; also the analysis length.
constexpr std::size_t kBoundsSplitParents = 4;

/// @brief The PEEK-vs-COMMIT trace: the sequence of slots actually COMMITTED,
///        plus the counters that prove some offered child was refused.
struct PeekCommitTrace {
    std::array<std::size_t, kMaxRotationTrace> slots{};
    std::size_t count = 0;
    std::uint64_t offered = 0;
    std::uint64_t spawned = 0;
    std::uint64_t rejected = 0;
    bool everyCycleCompleted = true;
    bool everySlotInOwnedRegion = true;
};

/// @brief Trigger-driven cycles on the bounds-split spectrum at
///        childrenPerEvent = 2, Octave-only relation weights.
void runBoundsSplitCycles(std::uint32_t seed, std::size_t cycles,
                          PeekCommitTrace& trace) noexcept {
    using Krate::DSP::BloomEngine;

    BloomEngine engine;
    prepareRotationRig(engine, seed, std::size_t{2});
    // K = 2: the two blockers of fillBoundsSplitParents are PARTIALS (FR-022
    // measures against them) but must never be PARENTS - a blocker parent's own
    // octave would be a fresh in-band candidate and would rescue the second
    // child of every event.
    engine.setParentCount(std::size_t{2});
    // Octave ONLY: a DetunedNeighbour candidate against 70 would be IN band
    // (70 * 2^(50/1200) = 72) and would rescue the child, which is exactly what
    // this arm must not allow.
    engine.setRelationWeight(BloomEngine::Relation::Octave, 1.0f);
    engine.setRelationWeight(BloomEngine::Relation::Fifth, 0.0f);
    engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, 0.0f);

    std::array<float, BloomEngine::kMaxSlots> ratios{};
    std::array<float, BloomEngine::kMaxSlots> amplitudes{};
    fillBoundsSplitParents(ratios.data(), amplitudes.data(), BloomEngine::kMaxSlots);

    std::array<std::size_t, BloomEngine::kMaxChildren> live{};

    for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
        engine.triggerBloom();
        static_cast<void>(engine.processChunk(ratios.data(), amplitudes.data(),
                                              kBoundsSplitParents,
                                              BloomEngine::kControlChunkSamples));

        const std::size_t placed = collectLiveSlots(engine, live);
        for (std::size_t i = 0; i < placed && trace.count < trace.slots.size(); ++i) {
            if (live[i] < kRotationReserveBase || live[i] >= BloomEngine::kMaxSlots) {
                trace.everySlotInOwnedRegion = false;
            }
            trace.slots[trace.count] = live[i];
            ++trace.count;
        }

        std::size_t guard = 0;
        while (engine.getLiveChildCount() > 0 && guard < kMaxStepsPerCycle) {
            static_cast<void>(engine.processChunk(ratios.data(), amplitudes.data(),
                                                  kBoundsSplitParents,
                                                  BloomEngine::kControlChunkSamples));
            ++guard;
        }
        if (engine.getLiveChildCount() != 0) {
            trace.everyCycleCompleted = false;
        }
    }

    trace.offered = engine.getOfferedChildCount();
    trace.spawned = engine.getSpawnedChildCount();
    trace.rejected = engine.getRejectedSpawnCount();
}

}  // namespace

TEST_CASE("BloomEngine_OwnedSlotRoundRobinRotation", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    static_assert(kRotationChildSlots <= BloomEngine::kMaxChildren,
                  "the owned region must fit the FR-034 lifecycle table");
    static_assert(kRotationReserveBase + kRotationChildSlots == BloomEngine::kMaxSlots,
                  "the rig's owned region is the top kRotationChildSlots slots");

    // =========================================================================
    // (a) full rotation, (b) no premature reuse, (d) the slot sequence is
    //     SEED-INDEPENDENT - all three read from the same three traces.
    // =========================================================================
    SECTION("(a)(b)(d) 200 trigger-driven cycles rotate, never repeat, and ignore the seed") {
        constexpr std::uint32_t kSeedA = 0x51075A01u;
        constexpr std::uint32_t kSeedB = 0x0BADF00Du;
        static_assert(kSeedA != kSeedB, "clause (d) needs two DIFFERENT seeds");

        RotationTrace traceA1;
        RotationTrace traceA2;
        RotationTrace traceB;
        runRotationCycles(kSeedA, std::size_t{1}, kRotationCycles, traceA1);
        runRotationCycles(kSeedA, std::size_t{1}, kRotationCycles, traceA2);
        runRotationCycles(kSeedB, std::size_t{1}, kRotationCycles, traceB);

        INFO("cycles = " << kRotationCycles << ", owned region = [" << kRotationReserveBase << ", "
                         << BloomEngine::kMaxSlots << ")");

        // Preconditions: every cycle really did spawn, run and retire. Without
        // these, an engine that placed NOTHING would satisfy (b) and (d)
        // vacuously with three empty traces.
        REQUIRE(traceA1.everyEventPlacedAll);
        REQUIRE(traceA1.everyCycleCompleted);
        REQUIRE(traceA1.everyReturnWasCapacity);
        REQUIRE(traceA1.stateAlwaysFinite);
        REQUIRE(traceA1.everySlotInOwnedRegion);
        REQUIRE(traceA1.everySlotDistinctWhileLive);
        REQUIRE(traceA1.count == kRotationCycles);

        // ---- (a) all 8 owned slots used within the first 16 cycles ----------
        std::array<bool, BloomEngine::kMaxSlots> seen{};
        for (std::size_t i = 0; i < kRotationCoverageCycles; ++i) {
            seen[traceA1.slots[i]] = true;
        }
        std::size_t covered = 0;
        for (std::size_t s = kRotationReserveBase; s < BloomEngine::kMaxSlots; ++s) {
            if (seen[s]) {
                ++covered;
            }
        }
        INFO("slots covered in the first " << kRotationCoverageCycles << " cycles: " << covered);
        REQUIRE(covered == kRotationChildSlots);

        // ---- (b) two consecutive cycles never reuse a slot ------------------
        // Only one child is alive at a time and the region holds 8 slots, so a
        // free slot ALWAYS exists: SC-018 (b)'s "while another is free"
        // qualifier is satisfied unconditionally here.
        bool consecutiveDistinct = true;
        std::size_t firstRepeatIndex = kRotationCycles;
        for (std::size_t i = 1; i < traceA1.count; ++i) {
            if (traceA1.slots[i] == traceA1.slots[i - 1]) {
                consecutiveDistinct = false;
                if (firstRepeatIndex == kRotationCycles) {
                    firstRepeatIndex = i;
                }
            }
        }
        INFO("first consecutive repeat at index " << firstRepeatIndex);
        REQUIRE(consecutiveDistinct);

        // ---- (d) the sequence is identical under the SAME seed, and identical
        //          under a DIFFERENT seed (Clarification Q6: slot choice
        //          consumes no draw) -------------------------------------------
        REQUIRE(traceA2.count == traceA1.count);
        REQUIRE(traceB.count == traceA1.count);
        REQUIRE(traceB.everyEventPlacedAll);
        REQUIRE(traceB.everyCycleCompleted);

        bool sameSeedIdentical = true;
        bool crossSeedIdentical = true;
        std::size_t firstCrossSeedDivergence = kRotationCycles;
        for (std::size_t i = 0; i < traceA1.count; ++i) {
            if (traceA2.slots[i] != traceA1.slots[i]) {
                sameSeedIdentical = false;
            }
            if (traceB.slots[i] != traceA1.slots[i]) {
                crossSeedIdentical = false;
                if (firstCrossSeedDivergence == kRotationCycles) {
                    firstCrossSeedDivergence = i;
                }
            }
        }
        INFO("first cross-seed divergence at index " << firstCrossSeedDivergence);
        REQUIRE(sameSeedIdentical);
        REQUIRE(crossSeedIdentical);
    }

    // =========================================================================
    // (c) two children of ONE event take different slots, and the cursor has
    //     advanced past BOTH by the next event.
    // =========================================================================
    SECTION("(c) simultaneous children take different slots and the cursor advances past both") {
        BloomEngine engine;
        prepareRotationRig(engine, 0xC0FFEE11u, std::size_t{2});
        REQUIRE(engine.getChildrenPerEvent() == std::size_t{2});

        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), BloomEngine::kMaxSlots);

        std::array<std::size_t, BloomEngine::kMaxChildren> live{};
        std::array<bool, BloomEngine::kMaxSlots> seen{};

        bool bothPlaced = true;
        bool pairDistinct = true;
        bool disjointFromPrevious = true;
        bool inOwnedRegion = true;
        bool returnOk = true;
        bool completedOk = true;
        std::size_t previousA = BloomEngine::kMaxSlots;
        std::size_t previousB = BloomEngine::kMaxSlots;

        for (std::size_t e = 0; e < kSimultaneousEvents; ++e) {
            engine.triggerBloom();
            const std::size_t spawnReturn =
                engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                    BloomEngine::kControlChunkSamples);
            if (spawnReturn != BloomEngine::kMaxSlots) {
                returnOk = false;
            }

            const std::size_t placed = collectLiveSlots(engine, live);
            if (placed != std::size_t{2}) {
                bothPlaced = false;
            } else {
                if (live[0] == live[1]) {
                    pairDistinct = false;  // cross-check of SC-003's no-shared-slot invariant
                }
                for (std::size_t i = 0; i < std::size_t{2}; ++i) {
                    if (live[i] < kRotationReserveBase || live[i] >= BloomEngine::kMaxSlots) {
                        inOwnedRegion = false;
                    }
                    // THE CURSOR-ADVANCE CLAUSE: the previous event's slots are
                    // free again by now, so an engine whose cursor did not move
                    // past the slots it took would hand out the SAME pair.
                    if (live[i] == previousA || live[i] == previousB) {
                        disjointFromPrevious = false;
                    }
                    seen[live[i]] = true;
                }
                previousA = live[0];
                previousB = live[1];
            }

            std::size_t guard = 0;
            while (engine.getLiveChildCount() > 0 && guard < kMaxStepsPerCycle) {
                const std::size_t returned =
                    engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                        BloomEngine::kControlChunkSamples);
                if (returned != BloomEngine::kMaxSlots) {
                    returnOk = false;
                }
                const std::size_t stillLive = collectLiveSlots(engine, live);
                if (stillLive == std::size_t{2} && live[0] == live[1]) {
                    pairDistinct = false;
                }
                ++guard;
            }
            if (engine.getLiveChildCount() != 0) {
                completedOk = false;
            }
        }

        std::size_t covered = 0;
        for (std::size_t s = kRotationReserveBase; s < BloomEngine::kMaxSlots; ++s) {
            if (seen[s]) {
                ++covered;
            }
        }

        INFO("events = " << kSimultaneousEvents << ", owned slots visited = " << covered);
        REQUIRE(bothPlaced);
        REQUIRE(completedOk);
        REQUIRE(returnOk);
        REQUIRE(inOwnedRegion);
        REQUIRE(pairDistinct);
        REQUIRE(disjointFromPrevious);
        REQUIRE(covered == kRotationChildSlots);
        REQUIRE(engine.getSpawnedChildCount() ==
                static_cast<std::uint64_t>(kSimultaneousEvents) * std::uint64_t{2});
    }

    // =========================================================================
    // (e) FR-043: triggerBloom() is EDGE-like, not level-like.
    // =========================================================================
    // A Phase-10 caller polling SlowEventScheduler::isEventActive()
    // (slow_event_scheduler.h:361) as a LEVEL calls triggerBloom() on every
    // block for as long as the event is active. If armed_ were a counter rather
    // than a bool, that caller would get n x childrenPerEvent simultaneous
    // children from one scheduler event - the exact failure this clause exists
    // to catch, and the one the falsification reproduces.
    // =========================================================================
    SECTION("(e) FR-043: 50 triggers inside one control chunk arm exactly one event") {
        constexpr std::size_t kTriggers = 50;
        static_assert(kTriggers < BloomEngine::kControlChunkSamples,
                      "the 50 triggers must all land INSIDE one control chunk: their sub-chunk "
                      "calls must sum to fewer than kControlChunkSamples samples");

        BloomEngine engine;
        prepareRotationRig(engine, 0x5EED0043u, std::size_t{2});

        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), BloomEngine::kMaxSlots);

        // 50 triggers, each followed by a ONE-sample advance: the control phase
        // reaches 50 of 64, so NO control step runs between them.
        bool subChunkReturnOk = true;
        for (std::size_t i = 0; i < kTriggers; ++i) {
            engine.triggerBloom();
            const std::size_t returned = engine.processChunk(ratios.data(), amplitudes.data(),
                                                             kWellSpacedParents, std::size_t{1});
            // Nothing has spawned yet, so the engine is not engaged and returns
            // the caller's UNCLAMPED parentCount (plan S3.1).
            if (returned != kWellSpacedParents) {
                subChunkReturnOk = false;
            }
        }
        REQUIRE(subChunkReturnOk);
        REQUIRE(engine.getSpawnEventCount() == std::uint64_t{0});
        REQUIRE(engine.getOfferedChildCount() == std::uint64_t{0});
        REQUIRE_FALSE(engine.isEngaged());

        // Close the chunk: exactly one control step runs.
        const std::size_t stepReturn =
            engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                BloomEngine::kControlChunkSamples - kTriggers);

        INFO("spawn events = " << engine.getSpawnEventCount()
                               << ", offered children = " << engine.getOfferedChildCount()
                               << ", live = " << engine.getLiveChildCount());
        REQUIRE(engine.getSpawnEventCount() == std::uint64_t{1});
        REQUIRE(engine.getOfferedChildCount() <=
                static_cast<std::uint64_t>(engine.getChildrenPerEvent()));
        REQUIRE(engine.getLiveChildCount() <= engine.getChildrenPerEvent());
        REQUIRE(engine.getDiscardedEventCount() == std::uint64_t{0});
        REQUIRE(stepReturn == BloomEngine::kMaxSlots);  // engaged: FR-051 returns capacity()

        // The arm was CONSUMED, not merely decremented: a further control step
        // with no new trigger produces no second event.
        const std::size_t idleReturn =
            engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                BloomEngine::kControlChunkSamples);
        REQUIRE(idleReturn == BloomEngine::kMaxSlots);
        REQUIRE(engine.getSpawnEventCount() == std::uint64_t{1});
        REQUIRE(engine.getOfferedChildCount() <=
                static_cast<std::uint64_t>(engine.getChildrenPerEvent()));
    }

    // =========================================================================
    // (d, second arm) THE CURSOR ADVANCES ON COMMIT, NOT ON PEEK.
    // =========================================================================
    // Plan S4.4 splits peekSlot() (pure) from commitSlot() (advances), and the
    // tasks.md falsification for this criterion is "advance cursor_ inside
    // peekSlot instead of commitSlot - clause (d) must fail".
    //
    // ON AN ALWAYS-ACCEPTING SPECTRUM THAT MUTATION IS INVISIBLE: peekSlot() is
    // called exactly once per offered child, and if every offered child is also
    // placed then peeks == commits and the two cursors advance identically. The
    // clause only has teeth where a child is PEEKED and then NEVER PLACED.
    //
    // fillBoundsSplitParents + setParentCount(2) + Octave-only weights produce
    // exactly that, and its doc block carries the full derivation: of the two
    // children of every event the first is always placed at ratio 2.0 and the
    // second is always refused - its own octave collides with that sibling
    // (FR-016), the other parent's octave is out of the FR-021 band on every
    // attempt, and the FR-026 final-attempt fallback lands inside FR-022's
    // rejection radius of a non-parent blocker partial. Half of all offered
    // children are therefore peeked and never placed, while the sequence of
    // slots the placements land in must remain the bare round robin
    // 56, 57, ... 63, 56, ... - one step per PLACEMENT, not one per offer.
    // =========================================================================
    SECTION("(d) the committed slot sequence skips refused children and ignores the seed") {
        constexpr std::uint32_t kSeedC = 0x11223344u;
        constexpr std::uint32_t kSeedD = 0xA5A5F00Fu;
        static_assert(kSeedC != kSeedD, "the cross-seed clause needs two DIFFERENT seeds");

        PeekCommitTrace traceC;
        PeekCommitTrace traceD;
        runBoundsSplitCycles(kSeedC, kRotationCycles, traceC);
        runBoundsSplitCycles(kSeedD, kRotationCycles, traceD);

        INFO("seed C: offered " << traceC.offered << ", spawned " << traceC.spawned
                                << ", rejected attempts " << traceC.rejected << " | seed D: offered "
                                << traceD.offered << ", spawned " << traceD.spawned
                                << ", rejected attempts " << traceD.rejected);

        REQUIRE(traceC.everyCycleCompleted);
        REQUIRE(traceD.everyCycleCompleted);
        REQUIRE(traceC.everySlotInOwnedRegion);
        REQUIRE(traceD.everySlotInOwnedRegion);

        // THE ARM IS NOT VACUOUS. Without these four, the section degenerates
        // into the always-accepted case above and the peek/commit mutation
        // passes it.
        REQUIRE(traceC.rejected > std::uint64_t{0});
        REQUIRE(traceC.spawned > std::uint64_t{0});
        REQUIRE(traceC.offered > traceC.spawned);
        REQUIRE(traceD.offered > traceD.spawned);

        // Every live child seen at its spawn step was recorded exactly once.
        REQUIRE(traceC.count == static_cast<std::size_t>(traceC.spawned));
        REQUIRE(traceD.count == static_cast<std::size_t>(traceD.spawned));

        // The committed slots are the bare round robin, indexed by PLACEMENT
        // ordinal - never by offer ordinal. A cursor advanced in peekSlot()
        // advances on the refused children too, so its placement sequence skips
        // slots and this fails on both seeds.
        bool roundRobinC = true;
        std::size_t firstOffOrbitC = traceC.count;
        for (std::size_t i = 0; i < traceC.count; ++i) {
            if (traceC.slots[i] != kRotationReserveBase + (i % kRotationChildSlots)) {
                roundRobinC = false;
                if (firstOffOrbitC == traceC.count) {
                    firstOffOrbitC = i;
                }
            }
        }
        bool roundRobinD = true;
        for (std::size_t i = 0; i < traceD.count; ++i) {
            if (traceD.slots[i] != kRotationReserveBase + (i % kRotationChildSlots)) {
                roundRobinD = false;
            }
        }
        INFO("first off-orbit placement (seed C) at ordinal " << firstOffOrbitC);
        REQUIRE(roundRobinC);
        REQUIRE(roundRobinD);

        // And the same sequence under a different seed, over their common
        // prefix (the comparison is written over the common prefix so that a
        // seed-dependent placement COUNT would not by itself fail it; the SLOTS
        // must not move).
        const std::size_t common = std::min(traceC.count, traceD.count);
        REQUIRE(common > std::size_t{0});
        bool crossSeedIdentical = true;
        for (std::size_t i = 0; i < common; ++i) {
            if (traceC.slots[i] != traceD.slots[i]) {
                crossSeedIdentical = false;
            }
        }
        REQUIRE(crossSeedIdentical);
    }
}

// ==============================================================================
// T014 case 1 - BloomEngine_SeedDeterminism (SC-008 (a), FR-070)
// ==============================================================================
// Two instances given the same seed, the same configuration and the same input
// must be indistinguishable: the same children, at the same slots, with the same
// latched step counts, and - once the spectrum is handed to a real HarmonicCloud
// - the same audio.
//
// THE STRUCTURAL CLAIM IS MADE OVER 10 SIMULATED MINUTES, CLOUD-FREE. At the
// 48 kHz control rate that is 10 * 60 * 750 = 450 000 control steps per
// instance, driven at the FR-040 spawn-rate ceiling (0.05 Hz, one event per 20 s)
// so the run contains a realistic number of events - the expectation is
// 600 s * 0.05 = 30 - and at the DEFAULT 45 / 120 / 180 s lifecycle so children
// are born, hold, contend for the eight owned slots, retire and are replaced.
// The two instances are stepped in LOCKSTEP and compared once per simulated
// second: a divergence is then reported at the second it appeared rather than
// only at the end, and nothing longer than one table is ever held.
//
// THE AUDIO CLAIM IS MADE OVER A BOUNDED CloudRig RENDER, NOT OVER THE SAME TEN
// MINUTES, and that is a cost decision with no effect on what is proved.
// Rendering 600 s of a 64-partial additive cloud twice is 57.6 million
// sample-frames per instance; the four-second trigger-driven render below is
// 192 000, contains a complete spawn -> fade-in -> hold -> fade-out, and pins
// exactly the same property - that the spectrum this engine hands the cloud is
// reproducible sample for sample. The structural arm above is what carries the
// ten minutes.
//
// NO BIT-EXACT FLOAT GOLDEN IS CHECKED IN (roadmap line 536,
// tools/lint-float-bit-goldens.js). The render is compared against the OTHER
// INSTANCE'S RENDER through render_fingerprint.h's measured tolerances; no float
// bit pattern of any render appears in this file.
// ==============================================================================
TEST_CASE("BloomEngine_SeedDeterminism", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    constexpr std::uint32_t kSeed = 0xD37E5EEDu;
    constexpr std::size_t kCapacity = BloomEngine::kMaxSlots;
    constexpr std::size_t kChildSlots = 8u;

    SECTION("SC-008 (a): two same-seed instances agree at every second of ten simulated minutes") {
        constexpr std::size_t kStepsPerSecond = static_cast<std::size_t>(kDetControlRateHz);
        constexpr std::size_t kSimulatedSeconds = 10u * 60u;
        constexpr std::size_t kTotalSteps = kSimulatedSeconds * kStepsPerSecond;
        static_assert(kStepsPerSecond == 750u,
                      "48 kHz / 64 samples = 750 control steps per second");

        std::array<float, BloomEngine::kMaxSlots> ratiosA{};
        std::array<float, BloomEngine::kMaxSlots> amplitudesA{};
        std::array<float, BloomEngine::kMaxSlots> ratiosB{};
        std::array<float, BloomEngine::kMaxSlots> amplitudesB{};
        fillWellSpacedParents(ratiosA.data(), amplitudesA.data(), ratiosA.size());
        fillWellSpacedParents(ratiosB.data(), amplitudesB.data(), ratiosB.size());

        // Two INDEPENDENT objects, each with its OWN parent arrays, so the
        // comparison below covers the bytes the engine writes as well as the
        // state it keeps. Sharing one array pair would let a write-side
        // divergence hide.
        BloomEngine a;
        BloomEngine b;
        prepareTriggerOnly(a, kSeed, kCapacity, kChildSlots);
        prepareTriggerOnly(b, kSeed, kCapacity, kChildSlots);
        // prepareTriggerOnly pins the FR-041 clock off; this arm wants it ON, at
        // the ceiling, because the clock draw is the thing whose determinism is
        // under test here.
        a.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
        b.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);

        std::size_t returnMismatches = 0;
        std::size_t tableMismatches = 0;
        std::size_t counterMismatches = 0;
        std::size_t arrayMismatches = 0;
        std::size_t firstMismatchSecond = kSimulatedSeconds;  // sentinel: never
        std::size_t maxLive = 0;

        for (std::size_t s = 0; s < kTotalSteps; ++s) {
            const std::size_t ra = a.processChunk(ratiosA.data(), amplitudesA.data(),
                                                  kWellSpacedParents,
                                                  BloomEngine::kControlChunkSamples);
            const std::size_t rb = b.processChunk(ratiosB.data(), amplitudesB.data(),
                                                  kWellSpacedParents,
                                                  BloomEngine::kControlChunkSamples);
            if (ra != rb) {
                ++returnMismatches;
            }
            maxLive = std::max(maxLive, a.getLiveChildCount());

            if ((s + 1) % kStepsPerSecond != 0) {
                continue;
            }
            const std::size_t second = (s + 1) / kStepsPerSecond - 1;
            bool agreed = true;
            if (!sameChildTable(captureChildTable(a), captureChildTable(b))) {
                ++tableMismatches;
                agreed = false;
            }
            if (!sameCounters(captureCounters(a), captureCounters(b))) {
                ++counterMismatches;
                agreed = false;
            }
            // NOLINTBEGIN(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            if (std::memcmp(ratiosA.data(), ratiosB.data(), sizeof(ratiosA)) != 0 ||
                std::memcmp(amplitudesA.data(), amplitudesB.data(), sizeof(amplitudesA)) != 0) {
                ++arrayMismatches;
                agreed = false;
            }
            // NOLINTEND(bugprone-suspicious-memory-comparison)
            if (!agreed && firstMismatchSecond == kSimulatedSeconds) {
                firstMismatchSecond = second;
            }
        }

        // NON-VACUITY FIRST. Two engines that never spawned anything would agree
        // perfectly while proving nothing about the seeded draws.
        INFO("events=" << a.getSpawnEventCount() << " spawned=" << a.getSpawnedChildCount()
                       << " completed=" << a.getCompletedChildCount()
                       << " refused=" << a.getRefusedChildCount() << " maxLive=" << maxLive);
        REQUIRE(a.getSpawnEventCount() > 0u);
        REQUIRE(a.getSpawnedChildCount() > 0u);
        REQUIRE(a.getCompletedChildCount() > 0u);  // children actually retired in ten minutes
        REQUIRE(maxLive > 0u);
        REQUIRE(a.isEngaged());

        INFO("first disagreeing simulated second: " << firstMismatchSecond);
        REQUIRE(returnMismatches == 0u);
        REQUIRE(tableMismatches == 0u);
        REQUIRE(counterMismatches == 0u);
        REQUIRE(arrayMismatches == 0u);
        REQUIRE(sameChildTable(captureChildTable(a), captureChildTable(b)));
        REQUIRE(sameCounters(captureCounters(a), captureCounters(b)));
    }

    SECTION("SC-008 (a): the CloudRig render of two same-seed instances is fingerprint-equal") {
        constexpr std::size_t kRenderChunks = 3000u;  // 4.0 s at 48 kHz
        constexpr std::size_t kRenderSamples = kRenderChunks * BloomEngine::kControlChunkSamples;
        // Trigger instants in CONTROL STEPS, well inside the render and spaced
        // wider than the compressed lifecycle so each event's children are born
        // and die inside the window that is fingerprinted.
        constexpr std::array<std::size_t, 3> kTriggerChunks{100u, 1000u, 2000u};

        struct RenderRun {
            DetChildTable table{};
            DetCounters counters{};
            DetRenderFingerprint fingerprint{};
            std::size_t maxLive = 0;
        };

        const auto renderOnce = [&]() -> RenderRun {
            RenderRun out;
            CloudRig rig;
            rig.prepare(kSeed, kCapacity, kChildSlots);
            rig.parentCount = kWellSpacedParents;
            fillWellSpacedParents(rig.ratios.data(), rig.amplitudes.data(), rig.ratios.size());
            rig.bloom.setFadeInSeconds(0.3f);
            rig.bloom.setHoldSeconds(0.1f);
            rig.bloom.setFadeOutSeconds(0.3f);

            std::vector<float> left(kRenderSamples, 0.0f);
            std::vector<float> right(kRenderSamples, 0.0f);
            std::size_t lastReturned = 0;
            for (std::size_t c = 0; c < kRenderChunks; ++c) {
                for (const std::size_t t : kTriggerChunks) {
                    if (c == t) {
                        rig.bloom.triggerBloom();
                    }
                }
                const std::size_t offset = c * BloomEngine::kControlChunkSamples;
                lastReturned = rig.chunk(&left[offset], &right[offset]);
                out.maxLive = std::max(out.maxLive, rig.bloom.getLiveChildCount());
            }
            REQUIRE(lastReturned > 0u);

            out.table = captureChildTable(rig.bloom);
            out.counters = captureCounters(rig.bloom);
            out.fingerprint = fingerprintStereo(left, right);
            return out;
        };

        const RenderRun first = renderOnce();
        const RenderRun second = renderOnce();

        INFO("events=" << first.counters.events << " spawned=" << first.counters.spawned
                       << " maxLive=" << first.maxLive
                       << " rmsL=" << first.fingerprint.left.rms);
        REQUIRE(first.counters.events == kTriggerChunks.size());
        REQUIRE(first.counters.spawned > 0u);
        REQUIRE(first.maxLive > 0u);
        REQUIRE(first.fingerprint.left.rms > 0.0);
        REQUIRE(first.fingerprint.right.rms > 0.0);

        REQUIRE(sameChildTable(second.table, first.table));
        REQUIRE(sameCounters(second.counters, first.counters));

        const auto leftCmp = Krate::DSP::TestUtils::compareFingerprints(second.fingerprint.left,
                                                                       first.fingerprint.left);
        const auto rightCmp = Krate::DSP::TestUtils::compareFingerprints(second.fingerprint.right,
                                                                        first.fingerprint.right);
        INFO("left  " << leftCmp.detail << " worstMetric=" << leftCmp.worstMetricRelativeError
                      << " worstSample=" << leftCmp.worstSampleError);
        INFO("right " << rightCmp.detail << " worstMetric=" << rightCmp.worstMetricRelativeError
                      << " worstSample=" << rightCmp.worstSampleError);
        REQUIRE(leftCmp.withinTolerance());
        REQUIRE(rightCmp.withinTolerance());
    }
}

// ==============================================================================
// T014 case 2 - BloomEngine_RngPositionIsStepPure (SC-008 (c), FR-041, FR-070)
// ==============================================================================
// FR-041's claim, restated per plan S14 C-1: an instance's RNG POSITION is a
// pure function of elapsed control steps, never of its dormancy / wake / depth /
// spawn-rate history. The implementation earns it twice over (bloom_engine.h:
// controlStep() step (1) draws the clock UNCONDITIONALLY, before the probability
// is even computed; runEvent()'s first statement RE-SEEDS eventRng_ from
// (seed, controlStep_) so the per-event draws are counter-based rather than
// sequential) - and each half needs its own arm, because neither observable
// catches the other's defect.
//
// WHY THE SPEC'S ORIGINAL WORDING CANNOT BE TESTED LITERALLY (plan S14 C-1):
// comparing an instance awake for the whole run against one dormant for its
// prefix compares two different STATES, not two RNG positions - the awake one
// spawned, took slots, moved the round-robin cursor and filled table entries.
// The three sections below are the restatement: (i) suppression EQUIVALENCE,
// (ii) arm-step-index IDENTITY against the always-awake instance, (iii) the
// event stream's purity, which is the half the falsification targets.
//
// THE SETTLING WINDOW, AND WHY THE CLOCK IS HELD ACROSS IT. Three of the four
// suppression means re-enable instantly (setDormant(false), setWake(1),
// setSpawnRateHz(r)); setDepth(1) re-enables through depthRamp_, whose gate -
// and therefore whose Bernoulli p - is strictly LOWER for the whole 50 ms ramp.
// SC-008 (c) therefore opens the comparison window
// ceil(kGainRampMs * controlRateHz / 1000) control steps after the enable
// instant (rampSettlingSteps() computes that expression; it is 38 at 48 kHz).
// That alone is not quite enough: an event ARMING inside the window for one
// instance and not another does not merely perturb the window, it leaves a child
// in the table FOREVER, so the post-window tables would differ permanently -
// the plan prices that residual at ~0.25 % of runs per seed at the spawn-rate
// ceiling. This fixture removes it outright by holding every instance's clock at
// 0 Hz across the settling window and restoring it, for all four at once, at the
// window's open. That is a STRICTER fixture, not a relaxed threshold: no
// tolerance, no threshold and no workload is reduced, and the window's own
// spawn-freeness is asserted rather than assumed (windowEvents == 0).
// ==============================================================================
TEST_CASE("BloomEngine_RngPositionIsStepPure", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;

    constexpr std::uint32_t kSeed = 0x5EEDC008u;
    constexpr std::size_t kCapacity = BloomEngine::kMaxSlots;
    constexpr std::size_t kChildSlots = 8u;
    constexpr std::size_t kStepsPerSecond = static_cast<std::size_t>(kDetControlRateHz);
    // 200 s of suppressed prefix: the expectation at the FR-040 ceiling is
    // 200 * 0.05 = 10 clock arms, so the suppression is genuinely exercised and
    // the always-awake instance of (ii) genuinely spawns during it.
    constexpr std::size_t kPrefixSteps = 200u * kStepsPerSecond;
    // 300 s of comparison window: the expectation is 15 executed events, which
    // is what makes the event-step lists below a real sequence rather than a
    // coin flip.
    constexpr std::size_t kPostSteps = 300u * kStepsPerSecond;
    // SC-008 (c)'s window, WRITTEN AS THE EXPRESSION rather than as 38.
    constexpr std::size_t kWindowSteps = rampSettlingSteps(kDetControlRateHz);
    static_assert(kWindowSteps == 38u, "ceil(50 ms * 750 Hz / 1000) - derived, not typed");

    enum class Suppression : std::uint8_t { Dormant, Wake, Depth, Rate };

    struct PurityRun {
        DetChildTable table{};
        DetCounters windowDelta{};
        std::vector<std::size_t> eventSteps;
        float depthBeforeEnable = -1.0f;
        float depthAtWindowOpen = -1.0f;
        std::size_t prefixEvents = 0;
        std::uint64_t prefixDiscarded = 0;
        std::size_t windowEvents = 0;
    };

    // One instance: suppressed for the prefix by ONE of the four means, enabled,
    // settled across the ramp with the clock held, then measured.
    const auto runSuppressed = [&](Suppression mode) -> PurityRun {
        PurityRun out;
        out.eventSteps.reserve(64);

        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), ratios.size());

        BloomEngine engine;
        if (mode == Suppression::Depth) {
            // BEFORE prepare(), so prepare()'s depthRamp_.snapTo(depth_) puts the
            // smoothed depth at EXACTLY 0 from step 0 - there is no 50 ms window
            // at the head of the prefix in which this instance could still spawn.
            engine.setDepth(0.0f);
        }
        prepareTriggerOnly(engine, kSeed, kCapacity, kChildSlots);
        if (mode == Suppression::Dormant) {
            engine.setDormant(true);
        }
        if (mode == Suppression::Wake) {
            engine.setWake(0.0f);
        }
        // The Rate instance's suppression IS the clock; the other three run the
        // clock at the ceiling so their gate suppression is what is being tested
        // and not an absence of arms.
        engine.setSpawnRateHz(mode == Suppression::Rate ? 0.0f : BloomEngine::kMaxSpawnRateHz);
        // Compressed against the 45 / 120 / 180 s default so the post-window
        // table CHURNS - children are born, retire and are replaced inside the
        // 300 s window rather than all sitting in FadeIn at the end.
        engine.setFadeInSeconds(5.0f);
        engine.setHoldSeconds(10.0f);
        engine.setFadeOutSeconds(5.0f);

        std::size_t lastReturned = 0;
        std::vector<std::size_t> prefixEventSteps;
        lastReturned = stepEngineRecordingEvents(engine, ratios.data(), amplitudes.data(),
                                                 kWellSpacedParents, kPrefixSteps, 0,
                                                 prefixEventSteps);
        out.prefixEvents = prefixEventSteps.size();
        out.prefixDiscarded = engine.getDiscardedEventCount();
        out.depthBeforeEnable = engine.getSmoothedDepth();

        // ---- the enable instant -------------------------------------------
        switch (mode) {
            case Suppression::Dormant:
                engine.setDormant(false);
                break;
            case Suppression::Wake:
                engine.setWake(1.0f);
                break;
            case Suppression::Depth:
                engine.setDepth(1.0f);  // re-enables THROUGH the 50 ms ramp
                break;
            case Suppression::Rate:
                break;  // its lever is the clock, restored below with everyone else's
        }
        // The clock hold across the settling window (see the case banner).
        engine.setSpawnRateHz(0.0f);

        std::vector<std::size_t> windowEventSteps;
        lastReturned = stepEngineRecordingEvents(engine, ratios.data(), amplitudes.data(),
                                                 kWellSpacedParents, kWindowSteps, kPrefixSteps,
                                                 windowEventSteps);
        out.windowEvents = windowEventSteps.size();
        out.depthAtWindowOpen = engine.getSmoothedDepth();

        // ---- the window opens: identical configuration for all four ---------
        const DetCounters base = captureCounters(engine);
        engine.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
        lastReturned = stepEngineRecordingEvents(engine, ratios.data(), amplitudes.data(),
                                                 kWellSpacedParents, kPostSteps,
                                                 kPrefixSteps + kWindowSteps, out.eventSteps);
        REQUIRE(lastReturned > 0u);

        out.table = captureChildTable(engine);
        out.windowDelta = counterDelta(captureCounters(engine), base);
        return out;
    };

    SECTION("SC-008 (c)(i): four suppression means converge after the settling window") {
        const PurityRun dormant = runSuppressed(Suppression::Dormant);
        const PurityRun wake = runSuppressed(Suppression::Wake);
        const PurityRun depth = runSuppressed(Suppression::Depth);
        const PurityRun rate = runSuppressed(Suppression::Rate);

        // ---- the preconditions the criterion names explicitly ---------------
        // Before the enable: three instances sit at a smoothed depth of exactly
        // 1 and are suppressed by something else; the fourth sits at exactly 0.
        REQUIRE(dormant.depthBeforeEnable == 1.0f);
        REQUIRE(wake.depthBeforeEnable == 1.0f);
        REQUIRE(rate.depthBeforeEnable == 1.0f);
        REQUIRE(depth.depthBeforeEnable == 0.0f);
        // At the window's open ALL FOUR sit at exactly 1 - this is SC-008 (c)'s
        // stated precondition, CHECKED rather than assumed, and it is what makes
        // the comparison that follows a comparison of equals.
        REQUIRE(dormant.depthAtWindowOpen == 1.0f);
        REQUIRE(wake.depthAtWindowOpen == 1.0f);
        REQUIRE(depth.depthAtWindowOpen == 1.0f);
        REQUIRE(rate.depthAtWindowOpen == 1.0f);
        // The suppression actually held: not one executed event in the prefix.
        REQUIRE(dormant.prefixEvents == 0u);
        REQUIRE(wake.prefixEvents == 0u);
        REQUIRE(depth.prefixEvents == 0u);
        REQUIRE(rate.prefixEvents == 0u);
        // ...and the settling window is spawn-free by construction, asserted.
        REQUIRE(dormant.windowEvents == 0u);
        REQUIRE(wake.windowEvents == 0u);
        REQUIRE(depth.windowEvents == 0u);
        REQUIRE(rate.windowEvents == 0u);

        // NOT ONE ARMED EVENT IN ANY PREFIX, and the reason differs by
        // instance - which is the point of using four means rather than one.
        // The gate multiplies INTO the Bernoulli probability
        // (bloom_engine.h controlStep() step (3): p = spawnRateHz * gate *
        // chunk / fs), so a gate-suppressed instance never reaches `u < p` at
        // all; the rate-suppressed one has the other factor at zero. Clarification
        // Q7's DISCARD path is therefore reachable only through triggerBloom()
        // while suppressed, which this arm deliberately does not do - so all four
        // discard counts are 0 and the prefix leaves nothing behind but 150 000
        // unconditional clock draws.
        INFO("prefix discarded: dormant=" << dormant.prefixDiscarded
                                          << " wake=" << wake.prefixDiscarded
                                          << " depth=" << depth.prefixDiscarded
                                          << " rate=" << rate.prefixDiscarded);
        REQUIRE(dormant.prefixDiscarded == 0u);
        REQUIRE(wake.prefixDiscarded == 0u);
        REQUIRE(depth.prefixDiscarded == 0u);
        REQUIRE(rate.prefixDiscarded == 0u);

        // ---- NON-VACUITY: the window contains a real event sequence ---------
        INFO("post-window events=" << dormant.eventSteps.size()
                                   << " spawned=" << dormant.windowDelta.spawned);
        REQUIRE(!dormant.eventSteps.empty());
        REQUIRE(dormant.windowDelta.spawned > 0u);

        // ---- the claim ------------------------------------------------------
        REQUIRE(wake.eventSteps == dormant.eventSteps);
        REQUIRE(depth.eventSteps == dormant.eventSteps);
        REQUIRE(rate.eventSteps == dormant.eventSteps);

        INFO("first differing table row (wake vs dormant): "
             << firstChildTableDifference(wake.table, dormant.table));
        REQUIRE(sameChildTable(wake.table, dormant.table));
        INFO("first differing table row (depth vs dormant): "
             << firstChildTableDifference(depth.table, dormant.table));
        REQUIRE(sameChildTable(depth.table, dormant.table));
        INFO("first differing table row (rate vs dormant): "
             << firstChildTableDifference(rate.table, dormant.table));
        REQUIRE(sameChildTable(rate.table, dormant.table));

        REQUIRE(sameCounters(wake.windowDelta, dormant.windowDelta));
        REQUIRE(sameCounters(depth.windowDelta, dormant.windowDelta));
        REQUIRE(sameCounters(rate.windowDelta, dormant.windowDelta));
    }

    SECTION("SC-008 (c)(ii): an always-awake instance arms on the same control steps") {
        const PurityRun dormant = runSuppressed(Suppression::Dormant);

        // The same object, never suppressed and never clock-held: awake and
        // spawning from step 0 to the end of the run.
        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        fillWellSpacedParents(ratios.data(), amplitudes.data(), ratios.size());

        BloomEngine awake;
        prepareTriggerOnly(awake, kSeed, kCapacity, kChildSlots);
        awake.setSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
        awake.setFadeInSeconds(5.0f);
        awake.setHoldSeconds(10.0f);
        awake.setFadeOutSeconds(5.0f);

        std::vector<std::size_t> beforeWindow;
        beforeWindow.reserve(64);
        std::vector<std::size_t> afterWindow;
        afterWindow.reserve(64);

        std::size_t lastReturned =
            stepEngineRecordingEvents(awake, ratios.data(), amplitudes.data(), kWellSpacedParents,
                                      kPrefixSteps + kWindowSteps, 0, beforeWindow);
        const std::size_t prefixEvents = beforeWindow.size();
        lastReturned = stepEngineRecordingEvents(awake, ratios.data(), amplitudes.data(),
                                                 kWellSpacedParents, kPostSteps,
                                                 kPrefixSteps + kWindowSteps, afterWindow);
        REQUIRE(lastReturned > 0u);

        // NON-VACUITY: this instance really did spawn during the prefix - it is
        // in a completely different STATE from the suppressed ones (children in
        // flight, slots taken, cursor moved), which is exactly why the claim is
        // about ARM STEP INDICES and not about its table.
        INFO("awake prefix events=" << prefixEvents
                                    << " post-window events=" << afterWindow.size()
                                    << " spawned=" << awake.getSpawnedChildCount());
        REQUIRE(prefixEvents > 0u);
        REQUIRE(awake.getSpawnedChildCount() > 0u);
        REQUIRE(!afterWindow.empty());

        // THE CLAIM: the clock draw is unconditional, so its position after n
        // control steps is n draws whatever happened in between - the awake
        // instance arms at EXACTLY the steps the suppressed ones do.
        REQUIRE(afterWindow == dormant.eventSteps);
    }

    SECTION("SC-008 (c)(iii): the event draw sequence is a function of (seed, step) alone") {
        // ---------------------------------------------------------------------
        // THE ARM THE FALSIFICATION TARGETS (tasks.md T014): make eventRng_ a
        // persistent sequential stream instead of the per-event counter-based
        // re-seed (plan S1.6, correction C-5) and this section must fail.
        //
        // Section (i) above cannot catch that mutation and is not asked to: none
        // of its four suppressed instances ever EXECUTES an event in the prefix,
        // so none of them consumes an event draw and a sequential stream would
        // still be at the same position for all four. The defect only becomes
        // visible when an instance that HAS spawned is compared with one that
        // has not - which is what this section builds.
        //
        // THE CONSTRUCTION. Two instances, one seed, the FR-041 clock off so
        // every event is placed by hand:
        //   * the PRIMED one runs three triggerBloom() events at control steps
        //     50, 1650 and 3250, each of which draws a parent, a relation, a
        //     detune and a hold jitter per child;
        //   * the CLEAN one runs the same steps with no events at all.
        // The lifecycle is the SHORTEST ONE FR-030 ADMITS - kMinFadeInSeconds /
        // 0 s hold / kMinFadeOutSeconds, i.e. 1 s / 0 s / 1 s = 1500 control
        // steps at the fixture's 48 kHz (750 Hz control rate). It is NOT a free
        // choice: setFadeInSeconds/setFadeOutSeconds clamp to [1, 300] s and
        // [1, 600] s (spec.md:394, bloom_engine.h:273-275), so a sub-second
        // request is silently raised to 1 s and a fixture that assumes a
        // 30-step lifecycle would leave every primed child alive.
        // The triggers are 1600 steps apart - one more than a whole lifecycle -
        // so every primed child is born and retired before the next event, and
        // both tables are EMPTY and EQUAL by step kDrainSteps (the last child
        // retires at 3250 + 1500 = 4750). The two are then given ONE event at
        // the SAME control step (kDrainSteps), and the children it latches must
        // be identical.
        //
        // Under the shipped counter-based stream they are: eventRng_ is re-seeded
        // from (seed, 1000) at the head of runEvent, so its draws cannot
        // remember the primed instance's three earlier events. Under a
        // sequential stream the primed instance is ~9 draws further along and
        // every latched field below differs.
        //
        // AND THE SAME COMPARISON IS SC-008 (c)(iii)'s OWN CLAIM - slot choice
        // consumes no draw. The primed instance's round-robin cursor has moved
        // (its children took the first slots of the owned region) while the
        // clean one's still sits at reserveBase(), so the two children land in
        // DIFFERENT SLOTS. Every drawn quantity is nevertheless identical, which
        // is only possible if peekSlot()/commitSlot() took nothing from the
        // stream. The slot inequality is asserted, so the arm cannot pass by the
        // two instances happening to agree on everything including the slot.
        // (SC-018 (d), asserted by T011, is the same property approached from
        // the seed-independence side.)
        // ---------------------------------------------------------------------
        // 1 s fade-in + 0 s hold + 1 s fade-out at 750 control steps per second:
        // a child placed on control step t is retired on step t + 1500 (it is
        // advanced for the first time on step t + 1, and retires when its own
        // step counter reaches fadeInSteps + holdSteps + fadeOutSteps).
        constexpr std::size_t kLifecycleSteps = 1500u;
        constexpr std::size_t kPrimeSpacing = 1600u;
        constexpr std::size_t kDrainSteps = 4800u;
        constexpr std::array<std::size_t, 3> kPrimeSteps{50u, 50u + kPrimeSpacing,
                                                         50u + 2u * kPrimeSpacing};
        static_assert(kPrimeSpacing > kLifecycleSteps,
                      "each primed event must fully drain before the next one");
        static_assert(kPrimeSteps[2] + kLifecycleSteps < kDrainSteps,
                      "the table must be empty again when the common event runs");

        struct PrimedRun {
            DetCounters drained{};
            DetChildTable table{};
        };

        const auto runPrimed = [&](bool prime) -> PrimedRun {
            PrimedRun out;
            std::array<float, BloomEngine::kMaxSlots> ratios{};
            std::array<float, BloomEngine::kMaxSlots> amplitudes{};
            fillWellSpacedParents(ratios.data(), amplitudes.data(), ratios.size());

            BloomEngine engine;
            prepareTriggerOnly(engine, kSeed, kCapacity, kChildSlots);
            engine.setFadeInSeconds(BloomEngine::kMinFadeInSeconds);    // 750 steps
            engine.setHoldSeconds(0.0f);                                // FR-030 admits a zero hold
            engine.setFadeOutSeconds(BloomEngine::kMinFadeOutSeconds);  // 750 steps
            engine.setConsumerTiltDb(0.0f);   // tiltGain() is the exact 1.0f identity
                                              // branch, so the latched target does
                                              // not depend on which slot was taken

            std::size_t lastReturned = 0;
            for (std::size_t s = 0; s < kDrainSteps; ++s) {
                if (prime) {
                    for (const std::size_t t : kPrimeSteps) {
                        if (s == t) {
                            engine.triggerBloom();
                        }
                    }
                }
                lastReturned = engine.processChunk(ratios.data(), amplitudes.data(),
                                                   kWellSpacedParents,
                                                   BloomEngine::kControlChunkSamples);
            }
            out.drained = captureCounters(engine);

            // The one common event, at control step kDrainSteps in BOTH runs.
            engine.triggerBloom();
            lastReturned = engine.processChunk(ratios.data(), amplitudes.data(),
                                               kWellSpacedParents,
                                               BloomEngine::kControlChunkSamples);
            REQUIRE(lastReturned > 0u);
            out.table = captureChildTable(engine);
            return out;
        };

        const PrimedRun primed = runPrimed(true);
        const PrimedRun clean = runPrimed(false);

        // The precondition the whole section rests on: the primed instance spawned
        // and then DRAINED, so the two tables were empty and equal when the common
        // event ran, and its cursor is off reserveBase() (a count in [1, 8) cannot
        // have wrapped the eight-slot owned region back onto its start).
        INFO("primed spawned=" << primed.drained.spawned
                               << " completed=" << primed.drained.completed
                               << " live=" << primed.drained.live);
        REQUIRE(primed.drained.spawned > 0u);
        REQUIRE(primed.drained.spawned < kChildSlots);
        REQUIRE(primed.drained.completed == primed.drained.spawned);
        REQUIRE(primed.drained.live == 0u);
        REQUIRE(clean.drained.spawned == 0u);
        REQUIRE(clean.drained.live == 0u);

        // The common event placed at least one child in BOTH: table entry 0 is the
        // first Idle entry findFreeTableEntry() hands out, and both tables were
        // empty when it ran, so entry 0 is that child in both runs.
        REQUIRE(primed.table[0].slot < BloomEngine::kMaxSlots);
        REQUIRE(clean.table[0].slot < BloomEngine::kMaxSlots);
        REQUIRE(static_cast<int>(primed.table[0].phase) ==
                static_cast<int>(BloomEngine::Phase::FadeIn));
        REQUIRE(static_cast<int>(clean.table[0].phase) ==
                static_cast<int>(BloomEngine::Phase::FadeIn));

        // Every DRAWN quantity is identical...
        for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
            const DetChildRow& p = primed.table[i];
            const DetChildRow& c = clean.table[i];
            INFO("table row " << i);
            REQUIRE(static_cast<int>(p.phase) == static_cast<int>(c.phase));
            REQUIRE(p.parentIndex == c.parentIndex);
            REQUIRE(static_cast<int>(p.relation) == static_cast<int>(c.relation));
            REQUIRE(p.fallback == c.fallback);
            REQUIRE(p.ratio == c.ratio);
            REQUIRE(p.target == c.target);
            REQUIRE(p.holdSeconds == c.holdSeconds);
            REQUIRE(p.elapsedSeconds == c.elapsedSeconds);
        }

        // ...while the SLOT is not, which is what proves slot choice took nothing
        // from the stream rather than the two runs simply being the same run.
        INFO("primed slot=" << primed.table[0].slot << " clean slot=" << clean.table[0].slot);
        REQUIRE(primed.table[0].slot != clean.table[0].slot);
    }
}

// ==============================================================================
// T016 - BloomEngine_ChildrenAreAudibleWithinCloudActiveCount (SC-016)
// ==============================================================================
// THE ROADMAP'S ONE ASSERTED PHASE-7 CRITERION (roadmap line 352) IS RESERVED-SLOT
// ACCOUNTING AGAINST *CLOUD* CAPACITY - and the real audibility boundary is
// HarmonicCloud::activeCount_, NOT kMaxPartials. recalculateAmplitudes() sets
// baseAmplitude_[i] = 0 and `continue`s for every i >= activeCount_ BEFORE the
// spectral-target branch is reached (harmonic_cloud.h:1469-1473 against
// :1492-1494), and activeCount_ = clamp(round(64^richness), 1, 64) (:1462-1463).
// A child written at or above the active count is therefore SILENTLY INAUDIBLE:
// the array is accepted, the engine's own read surface reports a healthy child,
// and nothing sounds.
//
// EVERY OTHER CLOUD ARM IN THIS PHASE HIDES THAT HAZARD. SC-001/SC-008 pin
// richness to 1.0, where activeCount_ == 64 and no slot can be past it, and
// SC-003's fuzz drives no cloud at all. This case is the only one that runs the
// cloud at richness < 1.0 with capacity taken from getActivePartialCount()
// (harmonic_cloud.h:950) exactly as FR-050's PrepareConfig::capacity doc comment
// demands, so it is the only place the Overview's "latent" failure mode becomes
// an assertion instead of an inference.
//
// THE FOUR CLAUSES, all hard (spec SC-016):
//   (1) every live child's slot index is < cloud.getActivePartialCount();
//   (2) cloud.getPartialTargetAmplitude(slot) > 0 for every live child once past
//       its fade-in onset - AUDIBLE, not merely written;
//   (3) cloud.hasSpectralTarget() is true after EVERY processChunk ->
//       setSpectralTarget handoff, the direct test that the array was ACCEPTED
//       rather than wholesale-rejected (harmonic_cloud.h:812-818);
//   (4) all of the above at parentCount in {0, 1, reserveBase()/2}, with the gap
//       region [parentCount, reserveBase()) POISONED before every single call.
//
// WHY THE CLOUD MUST STILL BE TARGET-FREE AT THE FIRST ENGAGED HANDOFF. Clause
// (3) only has teeth while hasTarget_ is false: setSpectralTarget's wholesale
// rejection (harmonic_cloud.h:812-818) returns WITHOUT clearing hasTarget_, so a
// cloud that already accepted one array would keep reporting true forever and an
// unpadded gap would sail past. The warm-up below therefore runs the engine to
// engagement and renders the cloud (so its 50 ms attack is finished) WITHOUT ever
// calling setSpectralTarget; the first array the cloud ever sees is an ENGAGED
// one carrying the poisoned gap, and if the engine did not pad it the very first
// handoff is rejected and clause (3) fails on chunk 0.
//
// WHY parentCount IS CHANGED *AFTER* ENGAGEMENT. FR-051's padding contract is in
// force only once engaged_ is latched, and engaged_ is latched only by a child,
// and a child needs an eligible parent - so parentCount == 0 cannot both engage
// the engine and be the value under test at the same time. engaged_ is STICKY
// (Clarification Q8, bloom_engine.h:1516-1520) precisely so that the padding
// survives the parent spectrum going away, which is the contract this arm
// measures: warm up at kWellSpacedParents, then drive the whole measurement
// window at the parentCount under test.
//
// THE POISON IS REWRITTEN BEFORE EVERY CALL, so "the gap is clean" can never be
// satisfied by a value the engine wrote on an earlier chunk and then stopped
// maintaining. poisonRegion() builds its NaN through the volatile sink
// (makeNonFinite), and NOTHING here asserts IEEE semantics on it: every
// non-finiteness verdict goes through Krate::DSP::detail::isFinite - a bit-
// pattern test - and lands in a COUNTER, which is what keeps this legal in a TU
// that ships under /fp:fast + -ffast-math.
//
// THE FALSIFICATION IS PLAN R14'S TRIPWIRE and it is executed as the second
// SECTION, not described: the identical fixture with capacity =
// HarmonicCloud::kMaxPartials against a cloud whose activeCount_ is ~32. The
// engine is provably unchanged there - it still returns capacity() and still
// hands the cloud an array it accepts - while clause (1) and clause (2) both
// fail for EVERY child. That is the Phase-10 caller who sized capacity from
// kMaxPartials, and this case is the tripwire the plan says it is.
// ==============================================================================
namespace {

/// @brief The measurement-window parent spectrum of the SC-016 arms.
///
/// Deliberately NOT fillWellSpacedParents: that fixture's 24-cent-gap guarantee
/// exists to make a SPAWN's first attempt succeed, and no spawn happens inside
/// the measurement window (the internal clock is off and nothing is triggered).
/// What the measurement window needs from `[0, parentCount)` is only that the
/// CLOUD accepts it - finite, ratio > 0, amplitude >= 0
/// (harmonic_cloud.h:812-818) - for parent counts up to reserveBase()/2, which
/// is 14 slots and past the four fillWellSpacedParents defines.
void fillAudibilityParents(float* ratios, float* amplitudes, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        ratios[i] = 1.0f + 0.25f * static_cast<float>(i);
        amplitudes[i] = 1.0f / static_cast<float>(i + 1);
    }
}

}  // namespace

TEST_CASE("BloomEngine_ChildrenAreAudibleWithinCloudActiveCount", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;
    using Krate::DSP::HarmonicCloud;

    constexpr std::size_t kSlots = BloomEngine::kMaxSlots;

    // prepareTriggerOnly() prepares at 48 kHz (its own literal), so the cloud is
    // prepared at the same rate here rather than at an independent one.
    constexpr double kSampleRate = 48000.0;
    constexpr float kFundamentalHz = 110.0f;

    // N(r) = clamp(round(64^r), 1, 64) (harmonic_cloud.h:1462-1463), so
    // r = log_64(32) = 5/6 lands the active count on 32: HALF the 64 slots a
    // BloomEngine can address. That is the configuration in which a capacity
    // taken from kMaxPartials puts every child past the audibility boundary.
    constexpr float kRichnessForHalfBank = 5.0f / 6.0f;

    constexpr std::size_t kChildSlots = 4;

    // ~0.25 s. Past the cloud's 0.05 s envelope attack (harmonic_cloud.h:2133)
    // and far past the one control step the spawn event needs.
    constexpr std::size_t kWarmupChunks = 188;

    // 0.5 s. With the fade-in compressed to 1 s (750 control steps at 48 kHz)
    // the children end the window at control step 563 - still inside FadeIn, at
    // smoothstep(0.751) ~ 0.53 of the latched target, i.e. FAR past the onset
    // and far above HarmonicCloud::kTargetAmpEpsilon (1e-5, :258), so every
    // child slot is genuinely marked dirty and recomputed.
    constexpr std::size_t kMeasureChunks = 375;

    // One arm's verdict, carried as COUNTERS so a poisoned value can never be
    // compared with an IEEE operator (this TU ships under -ffast-math).
    struct AudibilityArm {
        std::size_t activeCount = 0;
        std::size_t capacity = 0;
        std::size_t reserveBase = 0;
        std::size_t liveChildren = 0;
        std::size_t childrenSeen = 0;
        std::size_t slotsAtOrAboveActive = 0;
        std::size_t inaudibleChildSlots = 0;
        std::size_t rejectedHandoffs = 0;
        std::size_t nonFiniteHandoffEntries = 0;
        std::size_t unacceptableHandoffEntries = 0;
        std::size_t wrongReturnCounts = 0;
        float minChildCloudAmp = 0.0f;
        bool cloudFinite = false;
    };

    const auto drive = [&](std::size_t parentUnderTest, std::size_t capacityRequest) {
        AudibilityArm out{};

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setFundamentalHz(kFundamentalHz);
        cloud.setRichness(kRichnessForHalfBank);
        cloud.setSeed(0xA0D1B10Eu);
        // noteOn() flushes the deferred config-rate recompute while quiescent
        // (harmonic_cloud.h:635-660), which is what makes activeCount_ readable
        // BEFORE the first render rather than one chunk later.
        cloud.noteOn();
        out.activeCount = cloud.getActivePartialCount();
        REQUIRE_FALSE(cloud.hasSpectralTarget());

        BloomEngine engine;
        prepareTriggerOnly(engine, 0xB100AD10u, capacityRequest, kChildSlots);
        engine.setParentCount(4);
        engine.setChildrenPerEvent(2);
        engine.setChildGain(1.0f);                            // no attenuation to explain away
        engine.setFadeInSeconds(1.0f);                        // BloomEngine::kMinFadeInSeconds
        engine.setHoldSeconds(BloomEngine::kMaxHoldSeconds);  // nothing retires mid-window
        engine.setFadeOutSeconds(1.0f);
        engine.setHoldJitterFraction(0.0f);
        out.capacity = engine.capacity();
        out.reserveBase = engine.reserveBase();

        std::array<float, kSlots> ratios{};
        std::array<float, kSlots> amplitudes{};
        std::array<float, BloomEngine::kControlChunkSamples> left{};
        std::array<float, BloomEngine::kControlChunkSamples> right{};

        // ---- warm-up: engage the engine and finish the cloud's attack, with NO
        //      handoff, so the cloud is still target-free when the first ENGAGED
        //      array reaches it. fillWellSpacedParents is used here because this
        //      IS the spawn, and its 24-cent-gap construction makes first-attempt
        //      acceptance certain.
        fillWellSpacedParents(ratios.data(), amplitudes.data(), kSlots);
        engine.triggerBloom();
        for (std::size_t c = 0; c < kWarmupChunks; ++c) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                    BloomEngine::kControlChunkSamples);
            if (returned != engine.capacity()) {
                ++out.wrongReturnCounts;
            }
            cloud.processStereoBlock(left.data(), right.data(),
                                     BloomEngine::kControlChunkSamples);
        }
        REQUIRE(engine.isEngaged());
        REQUIRE(engine.getLiveChildCount() >= std::size_t{1});
        REQUIRE_FALSE(cloud.hasSpectralTarget());

        // ---- the measurement window, at the parentCount under test -----------
        out.minChildCloudAmp = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < kMeasureChunks; ++c) {
            // The caller's array is REBUILT FROM SCRATCH every call: the parent
            // region, then POISON over everything above it - the FR-051 gap
            // [parentCount, reserveBase()) AND the owned region alike.
            fillAudibilityParents(ratios.data(), amplitudes.data(), parentUnderTest);
            poisonRegion(ratios.data() + parentUnderTest, kSlots - parentUnderTest, c);
            poisonRegion(amplitudes.data() + parentUnderTest, kSlots - parentUnderTest, c + 1);

            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), parentUnderTest,
                                    BloomEngine::kControlChunkSamples);
            if (returned != engine.capacity()) {
                ++out.wrongReturnCounts;
            }

            // The handoff region measured against setSpectralTarget's OWN
            // acceptance test (harmonic_cloud.h:812-818), restated as counters.
            // The finiteness verdict is Krate::DSP::detail::isFinite - a bit-
            // pattern test - and the ordering comparisons below are reached ONLY
            // on the finite branch, so no IEEE semantics are asserted anywhere.
            const std::size_t handoff = std::min(returned, kSlots);
            for (std::size_t i = 0; i < handoff; ++i) {
                if (!Krate::DSP::detail::isFinite(ratios[i]) ||
                    !Krate::DSP::detail::isFinite(amplitudes[i])) {
                    ++out.nonFiniteHandoffEntries;
                } else if (ratios[i] <= 0.0f || amplitudes[i] < 0.0f) {
                    ++out.unacceptableHandoffEntries;
                }
            }

            cloud.setSpectralTarget(ratios.data(), amplitudes.data(), returned);
            if (!cloud.hasSpectralTarget()) {
                ++out.rejectedHandoffs;  // clause (3): the array was REJECTED
            }
            cloud.processStereoBlock(left.data(), right.data(),
                                     BloomEngine::kControlChunkSamples);
        }

        // ---- the verdict, read through the CLOUD's introspection surface -----
        out.liveChildren = engine.getLiveChildCount();
        for (std::size_t t = 0; t < BloomEngine::kMaxChildren; ++t) {
            const std::size_t slot = engine.getChildSlotIndex(t);
            if (slot >= BloomEngine::kMaxSlots) {
                continue;  // Phase::Idle, or out of range (S1.4)
            }
            ++out.childrenSeen;
            if (slot >= out.activeCount) {
                ++out.slotsAtOrAboveActive;  // clause (1)
            }
            const float cloudAmp = cloud.getPartialTargetAmplitude(slot);
            if (!(cloudAmp > 0.0f)) {
                ++out.inaudibleChildSlots;  // clause (2)
            }
            out.minChildCloudAmp = std::min(out.minChildCloudAmp, cloudAmp);
        }
        out.cloudFinite = cloud.stateFinite();
        return out;
    };

    // The cloud's active count is DISCOVERED, never typed: capacity is derived
    // from getActivePartialCount() exactly as FR-050 requires of a caller.
    HarmonicCloud probe;
    probe.prepare(kSampleRate);
    probe.setFundamentalHz(kFundamentalHz);
    probe.setRichness(kRichnessForHalfBank);
    probe.noteOn();
    const std::size_t activeCount = probe.getActivePartialCount();
    INFO("cloud active partial count at richness " << kRichnessForHalfBank << " = "
                                                   << activeCount);
    REQUIRE(activeCount >= std::size_t{24});
    REQUIRE(activeCount <= std::size_t{40});
    // THE PRECONDITION OF THE WHOLE CASE: the active count is BELOW the slot
    // ceiling, so "capacity from kMaxPartials" and "capacity from the active
    // count" are genuinely different configurations and the hazard can fire.
    REQUIRE(activeCount < HarmonicCloud::kMaxPartials);

    const std::size_t reserveBaseAtActive = activeCount - kChildSlots;

    SECTION("SC-016: every live child is audible at parentCount 0, 1 and reserveBase()/2") {
        const std::array<std::size_t, 3> kParentCounts{std::size_t{0}, std::size_t{1},
                                                       reserveBaseAtActive / 2};

        for (const std::size_t pc : kParentCounts) {
            const AudibilityArm arm = drive(pc, activeCount);
            INFO("parentCount=" << pc << " capacity=" << arm.capacity << " reserveBase="
                                << arm.reserveBase << " activeCount=" << arm.activeCount
                                << " live=" << arm.liveChildren
                                << " slotsAtOrAboveActive=" << arm.slotsAtOrAboveActive
                                << " inaudible=" << arm.inaudibleChildSlots
                                << " rejectedHandoffs=" << arm.rejectedHandoffs
                                << " nonFiniteHandoffEntries=" << arm.nonFiniteHandoffEntries
                                << " minChildCloudAmp=" << arm.minChildCloudAmp);

            // The fixture itself, so a failure below is never a broken setup.
            REQUIRE(arm.activeCount == activeCount);
            REQUIRE(arm.capacity == activeCount);
            REQUIRE(arm.reserveBase == reserveBaseAtActive);
            REQUIRE(arm.wrongReturnCounts == std::size_t{0});
            REQUIRE(arm.liveChildren >= std::size_t{1});
            REQUIRE(arm.childrenSeen == arm.liveChildren);

            // FR-051's padding, measured on the array the cloud was handed: not
            // one poisoned byte survived anywhere in [0, returned).
            REQUIRE(arm.nonFiniteHandoffEntries == std::size_t{0});
            REQUIRE(arm.unacceptableHandoffEntries == std::size_t{0});

            // Clause (3) - the array was ACCEPTED on every single handoff.
            REQUIRE(arm.rejectedHandoffs == std::size_t{0});

            // Clause (1) - every live child sits below the audibility boundary.
            REQUIRE(arm.slotsAtOrAboveActive == std::size_t{0});

            // Clause (2) - and the cloud is actually sounding it.
            REQUIRE(arm.inaudibleChildSlots == std::size_t{0});
            REQUIRE(arm.minChildCloudAmp > 0.0f);

            REQUIRE(arm.cloudFinite);
        }
    }

    // ==========================================================================
    // THE FALSIFICATION (plan R14's tripwire), EXECUTED RATHER THAN DESCRIBED
    // ==========================================================================
    // The identical fixture with capacity = HarmonicCloud::kMaxPartials against a
    // cloud whose activeCount_ is ~32 - i.e. the Phase-10 caller who sized
    // capacity from the slot ceiling instead of getActivePartialCount(). Clauses
    // (1) and (2) must BOTH fail, for EVERY child, while the engine's own
    // behaviour is provably unchanged (it still returns capacity() and still
    // hands the cloud an array the cloud accepts). If that inversion did not
    // happen, the positive section above was never measuring the cloud at all.
    // ==========================================================================
    SECTION("SC-016 falsification: capacity from kMaxPartials silences every child") {
        const AudibilityArm falsified = drive(std::size_t{1}, HarmonicCloud::kMaxPartials);
        INFO("falsified: capacity=" << falsified.capacity << " reserveBase="
                                    << falsified.reserveBase
                                    << " activeCount=" << falsified.activeCount
                                    << " live=" << falsified.liveChildren
                                    << " slotsAtOrAboveActive=" << falsified.slotsAtOrAboveActive
                                    << " inaudible=" << falsified.inaudibleChildSlots
                                    << " minChildCloudAmp=" << falsified.minChildCloudAmp);

        REQUIRE(falsified.capacity == HarmonicCloud::kMaxPartials);
        REQUIRE(falsified.activeCount == activeCount);
        REQUIRE(falsified.liveChildren >= std::size_t{1});
        REQUIRE(falsified.childrenSeen == falsified.liveChildren);

        // THE ENGINE IS UNCHANGED - which is exactly what makes the hazard silent.
        REQUIRE(falsified.wrongReturnCounts == std::size_t{0});
        REQUIRE(falsified.nonFiniteHandoffEntries == std::size_t{0});
        REQUIRE(falsified.unacceptableHandoffEntries == std::size_t{0});
        REQUIRE(falsified.rejectedHandoffs == std::size_t{0});
        REQUIRE(falsified.cloudFinite);

        // ...and clauses (1) and (2) invert, for every child.
        REQUIRE(falsified.slotsAtOrAboveActive == falsified.childrenSeen);
        REQUIRE(falsified.inaudibleChildSlots == falsified.childrenSeen);
        REQUIRE(falsified.minChildCloudAmp == 0.0f);
    }
}

// ==============================================================================
// T018 - BloomEngine_TiltCompensationMatchesIntendedLevel (SC-017, FR-023, Q1)
// ==============================================================================
// HarmonicCloud's spectral tilt is SLOT-INDEXED, not pitch-indexed:
// `baseAmplitude_[i] = targetAmp_[i] * tiltGain(i)` with
// `tiltGain(i) = 10^(tiltDb * log2(i + 1) / 20)` (harmonic_cloud.h:1493, :1429-1435).
// A child written into a high RESERVED slot is therefore tilted as though it were
// a high harmonic NUMBER regardless of the pitch it actually sounds - at
// tiltDb = -6 dB/oct a child in slot 63 is attenuated by 36 dB purely because of
// where it was parked. Clarification Q1's answer is FR-023's divisor: the latch
// stores `parentAmp * childGain * depth / tiltGain(slot)` (bloom_engine.h:1326),
// whose `tiltGain` is BloomEngine's OWN restatement of the cloud's law
// (bloom_engine.h:1274-1279). The two cancel, and the child lands at the level
// the caller asked for.
//
// WHY THE MEASUREMENT IS A RATIO AGAINST SLOT 0, NOT A BARE AMPLITUDE. The cloud
// applies ONE more factor to every partial before `currentAmplitude_` is
// readable: the FR-017 normalizer, `unmutatedTarget_[i] = gainSmoothed_ *
// baseAmplitude_[i]` (harmonic_cloud.h:1709). `gainSmoothed_` is a single scalar
// shared by every slot in the same chunk, so it is not a property of the child
// and cannot be predicted from the caller's array - but it DIVIDES OUT exactly
// against a reference partial read from the same cloud on the same chunk. Slot 0
// is that reference and is exact rather than approximate: `tiltGain(0)` takes the
// identity branch and returns EXACTLY 1.0f at every tilt setting
// (harmonic_cloud.h:1430-1432), so slot 0's base amplitude IS the amplitude the
// caller handed it. The measured level of a child is therefore
//
//     measured = currentAmplitude[childSlot] / currentAmplitude[0] * suppliedAmp[0]
//
// and SC-017's threshold is |20*log10(measured / parentAmplitudeAtSpawn)| <= 0.5 dB.
//
// THE REMAINING FACTORS ARE PINNED TO UNITY BY THE FIXTURE, not assumed away:
// `targetAmplitude_[i] = unmutatedTarget_[i] * w * env` (harmonic_cloud.h:1750)
// with `w` EXACTLY 1.0f on the explicit `mutationAmount_ <= 0` branch (:1737) at
// setMutation(0), and `env` exactly 1.0f once every partial has reached
// kEnvStageHold (:1634) - 0.05 s of attack with the offset spread at 0, i.e. 38
// control steps into a 3000-step run. The kernel's last factor,
// `antiAliasGain_[i] = fade * corr` (:1587), is the only one that does NOT cancel:
// it is frequency-dependent. The fixture keeps it inert by construction - at a
// 55 Hz fundamental the highest child ratio these parents can produce is 4.4
// (242 Hz), where `corr = sqrt(1 - sin^2(pi*f/fs))` differs from slot 0's by
// 0.001 dB, which is 1/500th of the +/-0.5 dB band, and `fade` is exactly 1 three
// octaves below fadeStart_.
//
// THE BAND IS +/-0.5 dB AND MUST NOT BE TIGHTENED (plan R11). MSVC, GCC and Apple
// Clang disagree on std::log2/std::exp2 in the last bits, and the macOS leg
// builds -ffast-math; +/-0.5 dB has roughly seven orders of margin over that.
// A bit-exact comparison here would be a float golden, which
// tools/lint-float-bit-goldens.js forbids.
//
// SLOT SPREAD. Children only ever occupy [reserveBase(), capacity())
// (bloom_engine.h:1567-1571), so "a spread of reserved slots" is produced by
// driving three CAPACITIES against the same four parents: capacity 8 puts the
// four children in slots 4-7 (the index band parents normally occupy), capacity
// 16 in slots 12-15, capacity 64 in slots 60-63. The cloud's tilt divisor spans
// -13.9 dB to -36.0 dB across that range, so a compensation that were merely
// slot-INDEPENDENT (a single scalar) would fail on at least two of the three.
//
// THE NEGATIVE CONTROL is Q1's exact hazard: a caller that sets the cloud's tilt
// and forgets to mirror it into setConsumerTiltDb(). The engine then latches an
// UNcompensated target, the cloud tilts it anyway, and the child lands
// -6 * log2(slot + 1) dB low - 13.9 dB at slot 4 and 36.0 dB at slot 63. The
// arm asserts BOTH that the miss exceeds 10 dB AND that its size matches the
// cloud's own tilt law to within 1 dB, so the control is known to fire for the
// stated reason rather than for any reason at all.
//
// FALSIFICATION (run, and recorded in the compliance notes): deleting the
// `/ tiltGain(slot)` divisor at bloom_engine.h:1326 makes the positive section
// fail for every child of every capacity, by exactly the negative control's
// margin - the two configurations become the same computation. Note that the
// clause "children in slot 0 still pass" is UNREACHABLE as a fixture and is not
// claimed here: a child can only land in slot 0 when reserveBase() == 0, and the
// parent scan runs over [0, min(parentCount, reserveBase())) (bloom_engine.h:1394),
// so a reserveBase() of 0 leaves no eligible parent and no child is ever spawned.
// The identity branch is covered instead by BloomEngine_ArgumentContract's
// default-tilt arms and by the exact `tiltGain(0) == 1.0f` cancellation this
// case's own slot-0 reference depends on.
// ==============================================================================
namespace {

/// @brief 20*log10(measured / expected), evaluated in DOUBLE.
///
/// Deliberately not Krate::DSP::gainToDb (db_utils.h:317): that helper floors at
/// kSilenceFloorDb (-144 dB), which would silently convert a measurement that
/// collapsed to zero into a finite-looking number. Both arguments are checked
/// strictly positive by the caller before this is reached.
[[nodiscard]] double levelErrorDb(double measured, double expected) noexcept {
    return 20.0 * std::log10(measured / expected);
}

/// @brief HarmonicCloud's slot-indexed tilt expressed in dB (harmonic_cloud.h:1434).
///
/// `tiltGain(i) = exp2(tiltDb * log2(i + 1) * log2(10) / 20)` is
/// `10^(tiltDb * log2(i + 1) / 20)` by the identity the cloud's own comment
/// states, so the attenuation in dB is simply `tiltDb * log2(i + 1)`. Used ONLY
/// by the negative control, to check that the miss it measures has the size the
/// cloud's law predicts.
[[nodiscard]] double cloudTiltDbAtSlot(std::size_t slot, double tiltDbPerOct) noexcept {
    return tiltDbPerOct * std::log2(static_cast<double>(slot + 1));
}

}  // namespace

TEST_CASE("BloomEngine_TiltCompensationMatchesIntendedLevel", "[bloom_engine]") {
    using Krate::DSP::BloomEngine;
    using Krate::DSP::HarmonicCloud;

    constexpr std::size_t kSlots = BloomEngine::kMaxSlots;
    constexpr double kSampleRate = 48000.0;  // prepareTriggerOnly's own rate

    // 55 Hz keeps every partial this fixture can produce (max ratio 4.4, i.e.
    // 242 Hz) three octaves below the cloud's anti-alias fade, so
    // antiAliasGain_ is 1 to within 0.001 dB at every slot read below.
    constexpr float kFundamentalHz = 55.0f;
    constexpr float kCloudTiltDb = -6.0f;

    constexpr std::size_t kChildSlots = 4;  // == kMaxChildrenPerEvent: one event fills them

    // 750 control steps per second at 48 kHz / 64. The fade-in is the
    // kMinFadeInSeconds floor (1 s = 750 steps); the remaining 2250 steps are
    // 3 s of Hold, which is 1500 time constants of the cloud's 2 ms amplitude
    // smoother (harmonic_cloud.h:165) and far past the 0.05 s envelope attack.
    constexpr std::size_t kChunks = 3000;

    constexpr double kBandDb = 0.5;  // SC-017's threshold
    constexpr double kNegativeControlMinMissDb = 10.0;
    constexpr double kPredictedMissToleranceDb = 1.0;

    // The three capacities, and therefore the three reserved-slot bands:
    // {4..7}, {12..15}, {60..63} at kChildSlots = 4.
    constexpr std::array<std::size_t, 3> kCapacities{std::size_t{8}, std::size_t{16},
                                                     std::size_t{64}};

    // The parent spectrum, held INDEPENDENTLY of the array the engine writes
    // into, so "the parent's amplitude at spawn" is read from a fixture the
    // engine provably never touched rather than from the live buffer.
    std::array<float, kSlots> refRatios{};
    std::array<float, kSlots> refAmps{};
    fillWellSpacedParents(refRatios.data(), refAmps.data(), kSlots);
    REQUIRE(refAmps[0] > 0.0f);

    struct ChildLevel {
        std::size_t slot = 0;
        std::size_t parentIndex = 0;
        double expected = 0.0;  ///< parentAmp * childGain(1) * depth(1)
        double measured = 0.0;
        double errDb = 0.0;
        bool measurable = false;
        bool held = false;
    };

    struct TiltArm {
        std::array<ChildLevel, BloomEngine::kMaxChildren> children{};
        std::size_t childCount = 0;
        std::size_t heldChildren = 0;
        std::size_t measurableChildren = 0;
        std::size_t distinctSlots = 0;
        std::size_t reserveBase = 0;
        std::size_t capacity = 0;
        std::size_t rejectedHandoffs = 0;
        float referenceCloudAmp = 0.0f;
        bool cloudFinite = false;
    };

    const auto drive = [&](std::size_t capacityRequest, float consumerTiltDb) {
        TiltArm out{};

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setFundamentalHz(kFundamentalHz);
        // richness 1.0 -> N(r) = round(64^1) = 64 (harmonic_cloud.h:1462-1463), so
        // EVERY slot is inside the audible count at every capacity below and
        // SC-016's audibility boundary cannot confound this measurement.
        cloud.setRichness(1.0f);
        cloud.setSpectralTiltDb(kCloudTiltDb);
        // The three factors that would otherwise sit between baseAmplitude_ and
        // currentAmplitude_, each pinned at its inert value EXPLICITLY rather
        // than left to a default that a later Seraphis change could move.
        cloud.setMutation(0.0f);              // w == EXACTLY 1.0f (harmonic_cloud.h:1737)
        cloud.setEnvelopeOffsetSpread(0.0f);  // every env reaches Hold together
        cloud.setDriftDepthCents(0.0f);       // detuneMultiplier_ == 1: AA gain is static
        cloud.setInharmonicity(0.0f);         // frequencies are exactly ratio * fundamental
        cloud.setSpectralGravity(0.0f);
        cloud.setSeed(0x5C17B100u);
        cloud.noteOn();
        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

        BloomEngine engine;
        prepareTriggerOnly(engine, 0x5C17B100u, capacityRequest, kChildSlots);
        engine.setParentCount(kWellSpacedParents);
        engine.setChildrenPerEvent(BloomEngine::kMaxChildrenPerEvent);
        // SC-017's explicit overrides: with childGain and depth both 1 the
        // intended level IS the parent's spawn-time amplitude, so the +/-0.5 dB
        // band measures the tilt cancellation and nothing else.
        engine.setChildGain(1.0f);
        engine.setDepth(1.0f);
        engine.setConsumerTiltDb(consumerTiltDb);
        engine.setFadeInSeconds(BloomEngine::kMinFadeInSeconds);
        engine.setHoldSeconds(BloomEngine::kMaxHoldSeconds);  // nothing retires in-window
        engine.setFadeOutSeconds(BloomEngine::kMinFadeOutSeconds);
        engine.setHoldJitterFraction(0.0f);
        out.capacity = engine.capacity();
        out.reserveBase = engine.reserveBase();

        // The ramp is snapped at prepare() and setDepth(1) retargets it to the
        // value it already holds, so the latch sees a smoothed depth of exactly
        // 1 - checked, not assumed (the getSmoothedDepth() addition A-6 idiom).
        REQUIRE(engine.getSmoothedDepth() == 1.0f);

        std::array<float, kSlots> ratios = refRatios;
        std::array<float, kSlots> amplitudes = refAmps;
        std::array<float, BloomEngine::kControlChunkSamples> left{};
        std::array<float, BloomEngine::kControlChunkSamples> right{};

        engine.triggerBloom();
        for (std::size_t c = 0; c < kChunks; ++c) {
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kWellSpacedParents,
                                    BloomEngine::kControlChunkSamples);
            cloud.setSpectralTarget(ratios.data(), amplitudes.data(), returned);
            if (!cloud.hasSpectralTarget()) {
                ++out.rejectedHandoffs;
            }
            cloud.processStereoBlock(left.data(), right.data(),
                                     BloomEngine::kControlChunkSamples);
        }

        // ---- the reference partial, read from the SAME cloud on the SAME chunk
        out.referenceCloudAmp = cloud.getPartialCurrentAmplitude(0);
        const double refCloudAmp = static_cast<double>(out.referenceCloudAmp);
        const double refSuppliedAmp = static_cast<double>(refAmps[0]);

        for (std::size_t t = 0; t < BloomEngine::kMaxChildren; ++t) {
            const std::size_t slot = engine.getChildSlotIndex(t);
            if (slot >= BloomEngine::kMaxSlots) {
                continue;  // Phase::Idle, or out of range (S1.4)
            }
            ChildLevel& rec = out.children[out.childCount];
            ++out.childCount;
            rec.slot = slot;
            rec.parentIndex = engine.getChildParentIndex(t);
            rec.held = (engine.getChildPhase(t) == BloomEngine::Phase::Hold);
            if (rec.held) {
                ++out.heldChildren;
            }
            rec.expected =
                (rec.parentIndex < kSlots) ? static_cast<double>(refAmps[rec.parentIndex]) : 0.0;
            const double childCloudAmp =
                static_cast<double>(cloud.getPartialCurrentAmplitude(slot));
            // Both divisions are guarded: a zero reference or a silenced child
            // leaves `measurable` false, and the caller REQUIREs that count to be
            // kChildSlots - an unmeasured child must never sail through the
            // +/-0.5 dB band on a default-constructed 0.0 dB error.
            if (refCloudAmp > 0.0 && childCloudAmp > 0.0 && rec.expected > 0.0) {
                rec.measured = (childCloudAmp / refCloudAmp) * refSuppliedAmp;
                rec.errDb = levelErrorDb(rec.measured, rec.expected);
                rec.measurable = true;
                ++out.measurableChildren;
            }
        }

        // Slot distinctness, over the just-collected records (kMaxChildren is 16,
        // so the quadratic scan is 120 comparisons at worst).
        for (std::size_t a = 0; a < out.childCount; ++a) {
            bool seenEarlier = false;
            for (std::size_t b = 0; b < a; ++b) {
                if (out.children[b].slot == out.children[a].slot) {
                    seenEarlier = true;
                }
            }
            if (!seenEarlier) {
                ++out.distinctSlots;
            }
        }

        out.cloudFinite = cloud.stateFinite();
        return out;
    };

    // The fixture assertions shared by both sections: a failure below is then
    // never a broken setup, always the clause the section names.
    const auto checkFixture = [&](const TiltArm& arm, std::size_t capacityRequest) {
        INFO("capacity=" << arm.capacity << " reserveBase=" << arm.reserveBase
                         << " children=" << arm.childCount << " held=" << arm.heldChildren
                         << " measurable=" << arm.measurableChildren
                         << " distinctSlots=" << arm.distinctSlots
                         << " referenceCloudAmp=" << arm.referenceCloudAmp
                         << " rejectedHandoffs=" << arm.rejectedHandoffs);
        REQUIRE(arm.capacity == capacityRequest);
        REQUIRE(arm.reserveBase == capacityRequest - kChildSlots);
        REQUIRE(arm.rejectedHandoffs == std::size_t{0});
        REQUIRE(arm.cloudFinite);
        REQUIRE(arm.referenceCloudAmp > 0.0f);
        // One event, four children per event, four free owned slots.
        REQUIRE(arm.childCount == kChildSlots);
        // Every fade-in has COMPLETED - SC-017's precondition, checked rather
        // than inferred from the chunk count.
        REQUIRE(arm.heldChildren == kChildSlots);
        REQUIRE(arm.measurableChildren == kChildSlots);
        // The tilt's slot-dependence is only exercised if the children actually
        // spread across the reserved band instead of stacking in one lane.
        REQUIRE(arm.distinctSlots == kChildSlots);
    };

    SECTION("SC-017: a matched consumer tilt lands every child at its parent's spawn level") {
        for (const std::size_t capacityRequest : kCapacities) {
            const TiltArm arm = drive(capacityRequest, kCloudTiltDb);
            checkFixture(arm, capacityRequest);

            for (std::size_t i = 0; i < arm.childCount; ++i) {
                const ChildLevel& ch = arm.children[i];
                INFO("capacity=" << capacityRequest << " slot=" << ch.slot
                                 << " parent=" << ch.parentIndex << " expected=" << ch.expected
                                 << " measured=" << ch.measured << " errDb=" << ch.errDb
                                 << " cloudTiltAtSlotDb="
                                 << cloudTiltDbAtSlot(ch.slot, static_cast<double>(kCloudTiltDb)));
                REQUIRE(ch.measurable);
                REQUIRE(ch.held);
                REQUIRE(ch.slot >= arm.reserveBase);
                REQUIRE(ch.slot < arm.capacity);
                // THE CRITERION: the cloud's slot-indexed tilt is cancelled, so
                // the child sounds at the level the caller asked for - whatever
                // slot it landed in.
                REQUIRE(std::fabs(ch.errDb) <= kBandDb);
            }
        }
    }

    // ==========================================================================
    // THE NEGATIVE CONTROL, EXECUTED RATHER THAN DESCRIBED
    // ==========================================================================
    // The identical fixture with setConsumerTiltDb(0) against a cloud still at
    // -6 dB/oct: Clarification Q1's caller who set one tilt and forgot the other.
    // Nothing else moves - the engine draws the same parents into the same slots,
    // because consumerTiltDb_ feeds accept()'s target arithmetic only and never a
    // draw (bloom_engine.h:1326) - so the ONLY difference reaching the cloud is
    // the missing divisor.
    // ==========================================================================
    SECTION("SC-017 negative control: an unsynced consumer tilt misses by the cloud's own tilt") {
        for (const std::size_t capacityRequest : kCapacities) {
            const TiltArm arm = drive(capacityRequest, 0.0f);
            checkFixture(arm, capacityRequest);

            for (std::size_t i = 0; i < arm.childCount; ++i) {
                const ChildLevel& ch = arm.children[i];
                const double predictedDb =
                    cloudTiltDbAtSlot(ch.slot, static_cast<double>(kCloudTiltDb));
                INFO("capacity=" << capacityRequest << " slot=" << ch.slot
                                 << " parent=" << ch.parentIndex << " expected=" << ch.expected
                                 << " measured=" << ch.measured << " errDb=" << ch.errDb
                                 << " predictedDb=" << predictedDb);
                REQUIRE(ch.measurable);
                // (1) the measurement is OUTSIDE the +/-0.5 dB band by more than 10 dB
                REQUIRE(std::fabs(ch.errDb) > kNegativeControlMinMissDb);
                // (2) ...and the miss is the size the cloud's tilt law predicts,
                //     so the control fires for the stated reason rather than for
                //     any reason at all.
                REQUIRE(std::fabs(ch.errDb - predictedDb) <= kPredictedMissToleranceDb);
            }
        }
    }
}
