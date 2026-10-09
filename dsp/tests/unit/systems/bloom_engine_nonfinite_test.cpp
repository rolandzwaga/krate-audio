// ==============================================================================
// Layer 3: System Tests - BloomEngine non-finite hygiene (SC-009)
//                              (specs/vorago-phase7-harmonic-bloom)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase7-harmonic-bloom/spec.md   (SC-009, FR-008,
//                                                          FR-009, FR-062)
//            specs/vorago-phase7-harmonic-bloom/plan.md   (S7, S7.5, S7.6,
//                                                          S10.2, S11)
//            specs/vorago-phase7-harmonic-bloom/tasks.md  (T001 creates this TU,
//                                                          T012 lands the case
//                                                          and the probe
//                                                          definition)
//
// SCOPE OF THIS TU (plan S10.2): SC-009's NON-FINITE arms ONLY
//   (BloomEngine_NonFiniteGuards + the probe arm). SC-009's range-and-neutral arm
//   lives in bloom_engine_test.cpp, which needs IEEE semantics nowhere.
//   This TU is also the only definition of
//   Krate::DSP::detail::BloomEngineNonFiniteProbe - bloom_engine.h declares that
//   struct and befriends it, but never defines it.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 7 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt (:920). Non-finite values may be ASSERTED on only
//   here, and even here they are built from bit patterns through a volatile sink
//   - never from std::numeric_limits<float>::quiet_NaN()/infinity(), which fold
//   to finite garbage on the -ffast-math legs. Finiteness is checked with
//   Krate::DSP::detail::isNaN / isInf / isFinite, never std::isnan/isinf/isfinite.
//
// WHY THIS IS A REAL TRACE AND NOT A FORMALITY. std::clamp does NOT reject NaN:
//   with v = NaN both `v < lo` and `hi < v` are false, so v is returned
//   unchanged. A clamp-only setter therefore admits NaN into configuration
//   state, and every configuration scalar of this component reaches an array the
//   caller hands HarmonicCloud::setSpectralTarget. The consequence is stated by
//   harmonic_cloud.h's own doc block above :769: rejection there is WHOLESALE -
//   "nothing is written, not even the slots that passed - on ... any NaN/Inf".
//   One NaN that leaks out of a setter into a latched child target therefore
//   does not merely silence that child: it silences the ENTIRE spectrum of the
//   voice for as long as the child lives, which at a 45 s fade-in plus a 120 s
//   hold plus a 180 s fade-out is five and a half minutes of silence from one
//   bad host automation value.
//
//   FR-009 (a)'s remedy is REJECTION, not substitution: `if
//   (!detail::isFinite(v)) return;` as the first statement of every float
//   setter, so the PREVIOUS value stands (bloom_engine.h:532-537 and the eleven
//   setters that follow it). prepare()'s `double sampleRate` is the one
//   exception the spec names (FR-004): it is SUBSTITUTED by 48 000 and only then
//   floored at kMinUsableSampleRate, because there is no previous rate to fall
//   back to (bloom_engine.h:385).
//
//   FR-009 (e) covers the array path: a non-finite value found in an INCOMING
//   parent slot DISQUALIFIES that slot from selection (bloom_engine.h:1398-1399
//   in the scan, :1421-1423 in the spacing set) and is never copied into an
//   owned slot and never written back.
//
// NO BIT-EXACT FLOAT GOLDENS anywhere in this TU. Every `==` below is a
//   within-run structural identity - "this stored value was NOT overwritten",
//   "this substituted rate is exactly the documented constant" - never "this
//   computation reproduced a pinned number" (node tools/lint-float-bit-goldens.js
//   gates the real thing).
// ==============================================================================

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bloom_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// ==============================================================================
// THE PLAN S7.5 FAULT-INJECTION PROBE - THE SOLE DEFINITION IN THE REPOSITORY
// ==============================================================================
// bloom_engine.h DECLARES this struct (:162) and befriends it (:1652); it never
// defines it, so a shipping build has no way to call it and it adds no public
// surface. Pattern: systems/feedback_ecology.h:172 (declaration) / :1525
// (friend), defined once in feedback_ecology_nonfinite_test.cpp:131-150; the
// Vorago restatement is subharmonic_engine.h:132 / :869, defined once in
// subharmonic_engine_nonfinite_test.cpp:97-142.
//
// WHY IT HAS TO EXIST. BloomEngine::stateFinite() (bloom_engine.h:819) is
// UNREACHABLE-as-false through the public API by construction: every setter
// rejects a non-finite argument, every candidate with a non-finite ratio or
// latched target is rejected rather than clamped, and a non-finite parent slot
// only disqualifies itself. Without this probe the trap could only ever be
// observed returning `true` - and `stateFinite() { return true; }` would pass
// every other arm of this case. The probe is what makes the trap falsifiable.
//
// ODR: swept this session -
//   grep -rn "BloomEngineNonFiniteProbe" dsp/ plugins/ tools/
// returns the header's declaration + friend line, this TU, and the phase's own
// spec/plan/tasks prose. Nothing else.
//
// EXACTLY TWO OPERATIONS. A probe that can write any member is a second,
// untested API, so there is no generic setter here and no reset: a poisoned
// engine is restored with its own public reset().
// ==============================================================================
namespace Krate::DSP::detail {

struct BloomEngineNonFiniteProbe {
    /// @brief Overwrite one lifecycle record's CURRENT ENVELOPE OUTPUT.
    ///
    /// `Child::amplitude` (bloom_engine.h:858) is the field applyOutput() copies
    /// into the callers array (:1571), so it is the field whose corruption
    /// would actually reach HarmonicCloud. It is written every control step by
    /// advanceChildren(), which is why the arm that uses this asserts on
    /// stateFinite() IMMEDIATELY, before any further processChunk() call could
    /// overwrite the poison with a freshly computed smoothstep value.
    ///
    /// An out-of-range table index is a silent no-op, matching the header's own
    /// read-surface convention; the caller asserts on readChildAmplitude()
    /// afterwards, so that can never pass undetected.
    static void poisonChildAmplitude(BloomEngine& engine, std::size_t tableIndex,
                                     float nonFinite) noexcept {
        if (tableIndex >= BloomEngine::kMaxChildren) {
            return;
        }
        engine.children_[tableIndex].amplitude = nonFinite;
    }

    /// @brief Read one lifecycle record's current envelope output back.
    ///
    /// BloomEngine::getChildAmplitude() (bloom_engine.h:780) reads the same
    /// field publicly, but reading it THROUGH THE PROBE is the point: it proves
    /// the poison landed in the record stateFinite() walks, not in some copy.
    [[nodiscard]] static float readChildAmplitude(const BloomEngine& engine,
                                                  std::size_t tableIndex) noexcept {
        if (tableIndex >= BloomEngine::kMaxChildren) {
            return 0.0f;
        }
        return engine.children_[tableIndex].amplitude;
    }
};

}  // namespace Krate::DSP::detail

namespace {

using Probe = Krate::DSP::detail::BloomEngineNonFiniteProbe;
using Krate::DSP::BloomEngine;
using Krate::DSP::detail::isFinite;

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

/// The binary64 twins, for prepare()'s `double sampleRate`. Same order.
constexpr std::array<std::uint64_t, 3> kPatterns64{{
    0x7FF8000000000000ULL,  // quiet NaN
    0x7FF0000000000000ULL,  // +Inf
    0xFFF0000000000000ULL,  // -Inf
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time, which is how a -ffast-math build
/// turns an "infinity" literal into a finite number. Idiom copied from
/// dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief The binary64 twin of makeNonFinite, for prepare()'s sampleRate.
[[nodiscard]] double makeNonFiniteDouble(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    const std::uint64_t materialized = sink;
    double out = 0.0;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief The bit pattern of a float, so "this slot was not written" can be
///        asserted on a value no comparison operator can compare (NaN != NaN).
[[nodiscard]] std::uint32_t bitsOf(float v) noexcept {
    std::uint32_t out = 0u;
    std::memcpy(&out, &v, sizeof(out));
    return out;
}

// =============================================================================
// Fixtures (plan S10.1: file-local, no new test helper header)
// =============================================================================

using SlotArray = std::array<float, BloomEngine::kMaxSlots>;

/// Number of leading array slots this TU treats as live parent content.
constexpr std::size_t kParentSlots = 8;

/// The capacity / owned-slot split every array arm below runs at.
/// reserveBase() == 32 - 8 == 24, so the parent region [0, 8) sits well below
/// the FR-051 gap [8, 24) and below the owned region [24, 32). A poisoned entry
/// at index 0 or 1 is therefore inside the scanned region (scanEnd == min(pc,
/// reserveBase()) == 8) and outside everything the engine writes.
constexpr std::size_t kCapacity = 32;
constexpr std::size_t kChildSlots = 8;

/// @brief A deliberately WELL-SPACED parent spectrum.
///
/// The ratios are mutually further apart than kMinRatioSpacingCents (24 cents)
/// and their octaves/fifths mostly land clear of them too, so a spawn that does
/// not happen is the gate talking and never the FR-022 spacing rule. Amplitudes
/// descend strictly, so "the strongest K" has one unambiguous answer and a
/// poisoned slot that WAS selected would be visible as a specific wrong index.
/// Slots at or above kParentSlots carry the cloud's own padding form
/// (ratio = i + 1, amplitude = 0; harmonic_cloud.h:825-826).
void fillWellSpacedParents(SlotArray& ratios, SlotArray& amplitudes) noexcept {
    constexpr std::array<float, kParentSlots> kRatios{1.0f, 1.3f, 1.7f, 2.2f,
                                                      2.9f, 3.7f, 4.6f, 5.9f};
    constexpr std::array<float, kParentSlots> kAmps{1.0f, 0.9f, 0.8f, 0.7f,
                                                    0.6f, 0.5f, 0.4f, 0.3f};
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    for (std::size_t i = 0; i < kParentSlots; ++i) {
        ratios[i] = kRatios[i];
        amplitudes[i] = kAmps[i];
    }
}

/// @brief prepare() with the INTERNAL CLOCK OFF, so triggerBloom() is the only
///        source of events (the bloom_engine_test.cpp:1197 fixture, restated
///        here because plan S10.1 forbids a shared helper header).
void prepareTriggerOnly(BloomEngine& engine, std::uint32_t seed) noexcept {
    engine.setSeed(seed);
    engine.prepare(48000.0,
                   BloomEngine::PrepareConfig{.capacity = kCapacity, .numChildSlots = kChildSlots});
    engine.setSpawnRateHz(0.0f);
}

/// @brief FR-009 (d) over the whole region the engine owns plus the FR-051 gap.
///
/// Everything at or above `pc` and below capacity() is written by the engine on
/// an engaged call, and every one of those floats is about to be handed to
/// HarmonicCloud::setSpectralTarget, whose rejection is wholesale. So the test
/// is exactly setSpectralTarget's own admission test: finite, ratio > 0,
/// amplitude >= 0.
void requireWrittenRegionClean(const SlotArray& ratios, const SlotArray& amplitudes,
                               std::size_t pc) {
    for (std::size_t i = pc; i < kCapacity; ++i) {
        INFO("written slot index " << i);
        REQUIRE(isFinite(ratios[i]));
        REQUIRE(ratios[i] > 0.0f);
        REQUIRE(isFinite(amplitudes[i]));
        REQUIRE(amplitudes[i] >= 0.0f);
    }
}

/// @brief The table index of the first non-Idle lifecycle record, or
///        kMaxChildren when the table is empty. Read through the PUBLIC surface
///        (getChildPhase, bloom_engine.h:791) on purpose: the probe is needed to
///        WRITE a Child, never to find one.
[[nodiscard]] std::size_t firstLiveChildIndex(const BloomEngine& engine) noexcept {
    for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
        if (engine.getChildPhase(i) != BloomEngine::Phase::Idle) {
            return i;
        }
    }
    return BloomEngine::kMaxChildren;
}

}  // namespace

// ==============================================================================
// T012 - BloomEngine_NonFiniteGuards (SC-009, the non-finite arms)
// ==============================================================================
TEST_CASE("BloomEngine_NonFiniteGuards", "[bloom_engine]") {
    SECTION("(a) every float setter REJECTS a non-finite argument") {
        // FR-009 (a). The previous value stands and the getter reports it. The
        // in-range values below are all distinct from each other AND from the
        // header's defaults, so a setter that substituted a default instead of
        // rejecting would fail here rather than pass by coincidence.
        for (const NonFinitePattern& pat : kPatterns) {
            INFO("pattern: " << pat.name);
            const float bad = makeNonFinite(pat.bits);
            REQUIRE_FALSE(isFinite(bad));  // the volatile sink did its job

            BloomEngine engine;
            engine.prepare(48000.0, BloomEngine::PrepareConfig{.capacity = kCapacity,
                                                              .numChildSlots = kChildSlots});

            constexpr float kDepth = 0.625f;
            constexpr float kRate = 0.03f;  // kMaxSpawnRateHz is 0.05
            constexpr float kGain = 0.4f;
            constexpr float kFadeIn = 12.0f;   // [1, 300]
            constexpr float kHold = 34.0f;     // [0, 900]
            constexpr float kFadeOut = 56.0f;  // [1, 600]
            constexpr float kJitter = 0.25f;
            constexpr float kTilt = -3.0f;  // [-12, +12]
            constexpr float kWake = 0.75f;  // above kWakeSilenceEpsilon
            constexpr float kWeightOctave = 0.2f;
            constexpr float kWeightFifth = 0.4f;
            constexpr float kWeightDetuned = 0.6f;

            engine.setDepth(kDepth);
            engine.setSpawnRateHz(kRate);
            engine.setChildGain(kGain);
            engine.setFadeInSeconds(kFadeIn);
            engine.setHoldSeconds(kHold);
            engine.setFadeOutSeconds(kFadeOut);
            engine.setHoldJitterFraction(kJitter);
            engine.setConsumerTiltDb(kTilt);
            engine.setWake(kWake);
            engine.setRelationWeight(BloomEngine::Relation::Octave, kWeightOctave);
            engine.setRelationWeight(BloomEngine::Relation::Fifth, kWeightFifth);
            engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, kWeightDetuned);

            // ---- the poison pass: every float setter, same bad value --------
            engine.setDepth(bad);
            engine.setSpawnRateHz(bad);
            engine.setChildGain(bad);
            engine.setFadeInSeconds(bad);
            engine.setHoldSeconds(bad);
            engine.setFadeOutSeconds(bad);
            engine.setHoldJitterFraction(bad);
            engine.setConsumerTiltDb(bad);
            engine.setWake(bad);
            engine.setRelationWeight(BloomEngine::Relation::Octave, bad);
            engine.setRelationWeight(BloomEngine::Relation::Fifth, bad);
            engine.setRelationWeight(BloomEngine::Relation::DetunedNeighbour, bad);

            // ---- the previous value stands, exactly -------------------------
            REQUIRE(engine.getDepth() == kDepth);
            REQUIRE(engine.getSpawnRateHz() == kRate);
            REQUIRE(engine.getChildGain() == kGain);
            REQUIRE(engine.getFadeInSeconds() == kFadeIn);
            REQUIRE(engine.getHoldSeconds() == kHold);
            REQUIRE(engine.getFadeOutSeconds() == kFadeOut);
            REQUIRE(engine.getHoldJitterFraction() == kJitter);
            REQUIRE(engine.getConsumerTiltDb() == kTilt);
            REQUIRE(engine.getWakeAmount() == kWake);
            REQUIRE(engine.getRelationWeight(BloomEngine::Relation::Octave) == kWeightOctave);
            REQUIRE(engine.getRelationWeight(BloomEngine::Relation::Fifth) == kWeightFifth);
            REQUIRE(engine.getRelationWeight(BloomEngine::Relation::DetunedNeighbour) ==
                    kWeightDetuned);

            // ---- and nothing leaked into the state -------------------------
            REQUIRE(isFinite(engine.getDepth()));
            REQUIRE(isFinite(engine.getSmoothedDepth()));
            REQUIRE(engine.stateFinite());

            // ---- the engine still runs, and still writes clean floats -------
            // Without this the arm proves only that a getter echoes a scalar; a
            // setter could have stored the poison in a SECOND, shadow member and
            // still pass every assertion above.
            // The internal FR-041 clock is silenced ONLY NOW, after the
            // rejection assertions above have read kRate back: a spontaneous
            // event during the drive loop would make the event-count assertion
            // at the tail of this arm fail about once in four hundred runs, and
            // an irreproducible red is worse than no assertion.
            engine.setSpawnRateHz(0.0f);
            REQUIRE(engine.getSpawnRateHz() == 0.0f);

            SlotArray ratios{};
            SlotArray amplitudes{};
            fillWellSpacedParents(ratios, amplitudes);
            engine.triggerBloom();
            for (std::size_t step = 0; step < 64; ++step) {
                const std::size_t returned = engine.processChunk(
                    ratios.data(), amplitudes.data(), kParentSlots,
                    BloomEngine::kControlChunkSamples);
                REQUIRE(returned <= BloomEngine::kMaxSlots);
                requireWrittenRegionClean(ratios, amplitudes, kParentSlots);
                REQUIRE(engine.stateFinite());
            }
            REQUIRE(engine.getSpawnEventCount() == 1u);
        }
    }

    SECTION("(b) a non-finite parent slot is disqualified, never copied, never written back") {
        // FR-009 (e). The poison is planted in the TWO STRONGEST slots on
        // purpose: index 0 carries the largest amplitude and index 1 the second
        // largest, so an engine that failed to disqualify them would select them
        // FIRST and the selection assertion below names the exact wrong index.
        for (const NonFinitePattern& pat : kPatterns) {
            INFO("pattern: " << pat.name);
            const float bad = makeNonFinite(pat.bits);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x51EED00Du);
            engine.setParentCount(4);
            engine.setChildrenPerEvent(4);

            SlotArray ratios{};
            SlotArray amplitudes{};
            fillWellSpacedParents(ratios, amplitudes);
            amplitudes[0] = bad;  // strongest slot, poisoned AMPLITUDE
            ratios[1] = bad;      // second strongest, poisoned RATIO

            engine.triggerBloom();
            std::size_t returned = engine.processChunk(ratios.data(), amplitudes.data(),
                                                       kParentSlots,
                                                       BloomEngine::kControlChunkSamples);
            REQUIRE(returned == kCapacity);  // the event engaged the engine

            // ---- the scan ran and picked only CLEAN slots -------------------
            REQUIRE(engine.getSpawnEventCount() == 1u);
            REQUIRE(engine.getParentScanCount() == 1u);
            // Six clean eligible slots remain (indices 2..7), so a K of 4 is
            // fully satisfiable WITHOUT either poisoned slot.
            REQUIRE(engine.getLastParentSelectionCount() == 4u);
            for (std::size_t k = 0; k < engine.getLastParentSelectionCount(); ++k) {
                INFO("selected parent " << k << " -> index " << engine.getLastParentIndex(k));
                REQUIRE(engine.getLastParentIndex(k) != 0u);
                REQUIRE(engine.getLastParentIndex(k) != 1u);
                REQUIRE(engine.getLastParentIndex(k) < kParentSlots);
            }

            // ---- the arm is NOT vacuous: children really did spawn ----------
            REQUIRE(engine.getLiveChildCount() > 0u);
            REQUIRE(engine.isEngaged());

            // ---- the poison was never written back --------------------------
            REQUIRE(bitsOf(amplitudes[0]) == pat.bits);
            REQUIRE(bitsOf(ratios[1]) == pat.bits);
            // ... and the clean halves of those two slots are untouched too.
            REQUIRE(ratios[0] == 1.0f);
            REQUIRE(amplitudes[1] == 0.9f);

            // ---- and it was never copied into anything the engine owns ------
            requireWrittenRegionClean(ratios, amplitudes, kParentSlots);
            REQUIRE(engine.stateFinite());

            // Keep driving with the poison in place: a child whose ratio or
            // target had captured the poison would surface it in the owned
            // region on some later control step, not necessarily the first.
            for (std::size_t step = 0; step < 200; ++step) {
                // Re-plant before every call so "still finite" can never be
                // satisfied by a value the engine wrote once and stopped
                // maintaining.
                amplitudes[0] = bad;
                ratios[1] = bad;
                returned = engine.processChunk(ratios.data(), amplitudes.data(), kParentSlots,
                                               BloomEngine::kControlChunkSamples);
                REQUIRE(returned == kCapacity);
                requireWrittenRegionClean(ratios, amplitudes, kParentSlots);
                REQUIRE(engine.stateFinite());
                REQUIRE(bitsOf(amplitudes[0]) == pat.bits);
                REQUIRE(bitsOf(ratios[1]) == pat.bits);
            }

            for (std::size_t i = 0; i < BloomEngine::kMaxChildren; ++i) {
                INFO("child table entry " << i);
                REQUIRE(isFinite(engine.getChildRatio(i)));
                REQUIRE(isFinite(engine.getChildTargetAmplitude(i)));
                REQUIRE(isFinite(engine.getChildAmplitude(i)));
                REQUIRE(engine.getChildAmplitude(i) >= 0.0f);
            }
        }
    }

    SECTION("(c) a non-finite sample rate is substituted by 48 000, then floored at 8 000") {
        // FR-004. The ORDER is the whole assertion. A prepare() that floored
        // without substituting first - `std::max(kMinUsableSampleRate, rate)` on
        // the raw argument - reports 8 000 for -Inf and for NaN (std::max
        // returns its first argument when the comparison is false) and +Inf for
        // +Inf. All three of those are distinguishable from 48 000, so this arm
        // fails on every shape of the mistake.
        for (const std::uint64_t bits : kPatterns64) {
            INFO("double bit pattern: " << bits);
            const double bad = makeNonFiniteDouble(bits);

            BloomEngine engine;
            engine.prepare(bad, BloomEngine::PrepareConfig{.capacity = kCapacity,
                                                           .numChildSlots = kChildSlots});
            REQUIRE(engine.getSampleRate() == 48000.0);
            REQUIRE(engine.isPrepared());
            REQUIRE(engine.stateFinite());

            // The derived per-control-step time constants are finite too, which
            // is only observable once a child exists: getChildHoldSeconds and
            // getChildElapsedSeconds are both `steps * controlDtSec_`.
            engine.setSpawnRateHz(0.0f);
            SlotArray ratios{};
            SlotArray amplitudes{};
            fillWellSpacedParents(ratios, amplitudes);
            engine.triggerBloom();
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentSlots,
                                    BloomEngine::kControlChunkSamples);
            REQUIRE(returned == kCapacity);
            const std::size_t live = firstLiveChildIndex(engine);
            REQUIRE(live < BloomEngine::kMaxChildren);
            REQUIRE(isFinite(engine.getChildHoldSeconds(live)));
            REQUIRE(engine.getChildHoldSeconds(live) >= 0.0f);
            REQUIRE(isFinite(engine.getChildElapsedSeconds(live)));
            requireWrittenRegionClean(ratios, amplitudes, kParentSlots);
        }

        // The floor is LIVE, not decorative: a finite sub-minimum rate is raised
        // to kMinUsableSampleRate rather than accepted.
        BloomEngine low;
        low.prepare(100.0, BloomEngine::PrepareConfig{});
        REQUIRE(low.getSampleRate() == BloomEngine::kMinUsableSampleRate);

        // And a normal rate is passed through untouched, so neither guard is a
        // blanket substitution.
        BloomEngine normal;
        normal.prepare(44100.0, BloomEngine::PrepareConfig{});
        REQUIRE(normal.getSampleRate() == 44100.0);
    }

    SECTION("probe arm: the FR-062 trap FIRES on a poisoned live child") {
        // Without this arm stateFinite() is only ever observed returning true,
        // and `return true;` would satisfy every other assertion in this case.
        for (const NonFinitePattern& pat : kPatterns) {
            INFO("pattern: " << pat.name);

            BloomEngine engine;
            prepareTriggerOnly(engine, 0x7A1157EDu);
            engine.setChildrenPerEvent(2);

            SlotArray ratios{};
            SlotArray amplitudes{};
            fillWellSpacedParents(ratios, amplitudes);

            engine.triggerBloom();
            const std::size_t returned =
                engine.processChunk(ratios.data(), amplitudes.data(), kParentSlots,
                                    BloomEngine::kControlChunkSamples);
            REQUIRE(returned == kCapacity);
            REQUIRE(engine.getLiveChildCount() > 0u);

            const std::size_t live = firstLiveChildIndex(engine);
            REQUIRE(live < BloomEngine::kMaxChildren);

            // --- the NEGATIVE CONTROL, and it is the whole falsification -----
            // Remove the poison below and this line still passes while the two
            // after it fail: `true` here and `false` there cannot both be
            // satisfied by a constant.
            REQUIRE(engine.stateFinite());
            REQUIRE(isFinite(Probe::readChildAmplitude(engine, live)));

            const float poison = makeNonFinite(pat.bits);
            Probe::poisonChildAmplitude(engine, live, poison);

            REQUIRE(bitsOf(Probe::readChildAmplitude(engine, live)) == pat.bits);
            REQUIRE_FALSE(isFinite(Probe::readChildAmplitude(engine, live)));
            REQUIRE_FALSE(engine.stateFinite());

            // reset() is configuration-preserving and rewinds the whole table,
            // so the trap goes quiet again - proving it reports STATE and is not
            // a latch that once tripped stays tripped.
            engine.reset();
            REQUIRE(engine.stateFinite());
            REQUIRE(engine.getLiveChildCount() == 0u);
        }
    }
}
