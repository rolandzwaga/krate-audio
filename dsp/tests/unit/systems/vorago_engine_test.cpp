// ==============================================================================
// Layer 3: System Tests - VoragoEngine, main TU
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T014-T016 and
//                                                         T023-T024 fill it)
//
// SCOPE OF THIS TU: the bounded, per-push engine criteria - SC-004a, SC-006,
//   SC-007 (engine), SC-011, SC-012, SC-013a, SC-014, SC-021a, SC-022, SC-023,
//   SC-026, SC-027, SC-028, SC-030, SC-031, plus the setter contracts. The
//   multi-minute renders live in unit/systems/vorago_engine_longrun_test.cpp.
//
// SCOPE AT T016: T014's three cases cover the constants, VoragoEngineConfig,
//   prepare(), polyphony clamping, the envelope fan-out, FR-044's three-pass
//   steal selection and FR-048's per-SLOT seeds. T015 adds the four cases that
//   need a rendered block - SC-007 (engine), SC-022, SC-031 and FR-026's tidal
//   fog fold. T016 retires this TU's local fixtures in favour of the shared
//   ones in tests/test_helpers/vorago_fixtures.h and pins them with
//   VoragoFixtures_FastAttackAndMakeEngine. SC-011's steal ramp, the SC-013a
//   fuzz and the soak sentinels are T023/T024.
//
// ALLOCATION DETECTION: this TU includes <allocation_detector.h> (T023's
//   SC-014 arm) and NEVER <allocation_operator_overrides.h>. The single owner
//   of the global operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error. The
//   detector header only declares the counter the overrides feed, so including
//   it here is safe - and SC-014's clause 0 proves the counter is live in this
//   image rather than assuming it.
//
// PORTABILITY: no std::isnan / std::isinf / std::isfinite anywhere in this TU,
//   so it stays correct under -ffast-math. The degenerate sample rate and the
//   non-finite steal level are both built from BIT PATTERNS through a volatile.
//
// EVERY ENGINE IS HEAP-ALLOCATED. std::array<VoragoVoice, kMaxVoices> is
//   hundreds of kilobytes and MSVC's default main-thread stack is 1 MiB, so a
//   stack-local VoragoEngine is a defect, not a style preference
//   (seraphis_engine.h:201-204). makeEngine() (T016, the shared fixture) is the
//   construction path wherever a case wants a PREPARED engine; the remaining
//   std::make_unique calls below are the cases that deliberately need an
//   unprepared one, or that must call a setter BEFORE prepare().
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/random.h>  // deriveStreamSeed - FR-045's derivation, verified directly
#include <krate/dsp/processors/tidal_modulator.h>  // the FR-026 lane, read through VoragoVoice::tide()
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>  // SC-014's "every macro extreme"

// The shared Phase 10 fixtures (T016). Owns makeEngine(), applyFastAttack(),
// renderEngine() and the analysis helpers, so no case re-implements them.
#include <vorago_fixtures.h>

// T023. <allocation_detector.h> ONLY - see the ALLOCATION DETECTION note above.
#include <allocation_detector.h>
#include <render_fingerprint.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::deriveStreamSeed;
using Krate::DSP::VoiceState;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoVoice;

// The shared fixtures, pulled in by name so the call sites below read exactly as
// they did when this TU carried its own local copies.
using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::bitIdentical;
using Krate::DSP::TestUtils::Vorago::blockRmsDb;
using Krate::DSP::TestUtils::Vorago::kFastAttackEnvelopeConfig;
using Krate::DSP::TestUtils::Vorago::makeEngine;
using Krate::DSP::TestUtils::Vorago::renderEngine;

constexpr double kSampleRate48 = 48000.0;

/// @brief A quiet NaN, built from its bit pattern through a `volatile`.
///
/// NOT std::numeric_limits<double>::quiet_NaN(): -ffast-math lets the compiler
/// fold that away before it ever reaches prepare(), which would silently turn
/// FR-076's first arm into a test of the value 0.0.
[[nodiscard]] double makeNaNDouble() noexcept {
    volatile std::uint64_t pattern = 0x7FF8000000000001ull;
    const std::uint64_t bits = pattern;
    double out = 0.0;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

/// @brief A quiet NaN float, same construction and for the same reason.
[[nodiscard]] float makeNaNFloat() noexcept {
    volatile std::uint32_t pattern = 0x7FC00000u;
    const std::uint32_t bits = pattern;
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

/// @brief One row of SC-012's enumerated victim table.
///
/// The table drives VoragoEngine::selectStealVictim directly, with no render:
/// the rule is public and static for exactly that reason (the same shape
/// VoragoVoice::combineWake takes for SC-019a). Levels are the ones a render
/// WOULD have produced, stated rather than provoked, so every row names its own
/// pass instead of depending on a level trajectory that a later tuning change
/// could move.
struct StealRow {
    const char* name;
    std::array<VoiceState, 4> states;
    std::array<float, 4> levels;
    std::array<std::uint64_t, 4> serials;
    std::size_t count;
    int expected;
};

/// @brief Run one row through the rule under test.
[[nodiscard]] int runRow(const StealRow& row) noexcept {
    return VoragoEngine::selectStealVictim(
        std::span<const VoiceState>{row.states.data(), row.count},
        std::span<const float>{row.levels.data(), row.count},
        std::span<const std::uint64_t>{row.serials.data(), row.count});
}

/// @brief True when @p v is neither infinite nor NaN, read off the exponent
///        field. Immune to -ffast-math, which std::isfinite is not.
[[nodiscard]] bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

struct StereoRender {
    std::vector<float> l;
    std::vector<float> r;
};

/// @brief Render exactly @p total samples, cycling @p pattern for the block
///        sizes. The one render loop every case below uses, so no case rolls
///        its own partition by accident.
[[nodiscard]] StereoRender renderPartitioned(VoragoEngine& engine, std::size_t total,
                                             std::span<const std::size_t> pattern) {
    StereoRender out{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
    std::size_t done = 0;
    std::size_t step = 0;
    while (done < total) {
        const std::size_t want = pattern[step % pattern.size()];
        const std::size_t n = std::min(want, total - done);
        engine.processStereoBlock(out.l.data() + done, out.r.data() + done, n);
        done += n;
        ++step;
    }
    return out;
}

[[nodiscard]] float peakOf(std::span<const float> x) {
    float peak = 0.0f;
    for (const float v : x) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

[[nodiscard]] bool allFinite(const StereoRender& x) {
    for (std::size_t i = 0; i < x.l.size(); ++i) {
        if (!isFiniteBits(x.l[i]) || !isFiniteBits(x.r[i])) {
            return false;
        }
    }
    return true;
}

/// @brief The largest sample-to-sample step in @p x - SC-011's click statistic.
[[nodiscard]] float maxAbsDelta(std::span<const float> x) {
    float worst = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i) {
        worst = std::max(worst, std::fabs(x[i] - x[i - 1u]));
    }
    return worst;
}

/// @brief The first index whose magnitude exceeds @p threshold, or `x.size()`.
[[nodiscard]] std::size_t firstAbove(std::span<const float> x, float threshold) {
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (std::fabs(x[i]) > threshold) {
            return i;
        }
    }
    return x.size();
}

/// @brief Search for an engine seed whose SLOT 0 tidal lane satisfies @p accept,
///        read with no render at all. Returns -1 when none of @p attempts hits.
///
/// WHY A SEARCH AND NOT A RESET LOOP. `TidalModulator::initState()` RE-SEEDS its
/// RNG from the configured seed before drawing the six sine phases
/// (tidal_modulator.h:286-293), so reset() redraws nothing: the lane's value at
/// t = 0 is a pure function of the slot seed, and `VoragoEngine::setSeed()` is
/// the only lever. The search is cheap because `getCurrentValue()` is well
/// defined straight out of `initState()`, which snaps the output smoother to
/// `rawOutput()` (:293) - so no block has to be rendered to know the answer.
///
/// `setSeed()` alone is not enough: it re-seeds the stream (vorago_voice.h:1260)
/// but the PHASES are only redrawn by `tide_.reset()`, which the engine's
/// reset() reaches through `VoragoVoice::clearRunState` (:1315).
[[nodiscard]] long long findSeedForTide(VoragoEngine& engine, bool (*accept)(float),
                                        std::uint32_t firstSeed, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        // A golden-ratio stride, so consecutive attempts are not consecutive
        // hashes of one another.
        const auto seed = static_cast<std::uint32_t>(firstSeed
                                                     + static_cast<std::uint32_t>(i) * 0x9E3779B9u);
        engine.setSeed(seed);
        engine.reset();
        if (accept(engine.getVoice(0).tide().getCurrentValue())) {
            return static_cast<long long>(seed);
        }
    }
    return -1;
}

[[nodiscard]] bool tideIsClearlyPositive(float v) noexcept { return v > 0.02f; }
[[nodiscard]] bool tideIsClearlyNegative(float v) noexcept { return v < -0.02f; }

}  // namespace

// =============================================================================
// SC-023 (engine half) + FR-043's clamp + FR-076's degenerate rates
// =============================================================================

TEST_CASE("VoragoEngine_UnpreparedAndDegenerate", "[systems][vorago]") {
    SECTION("an unprepared engine reports its documented neutrals") {
        auto engine = std::make_unique<VoragoEngine>();

        REQUIRE_FALSE(engine->isPrepared());
        REQUIRE(engine->getAllocatedBytes() == 0u);
        REQUIRE(engine->getPolyphony() == VoragoEngine::kDefaultPolyphony);
        REQUIRE(engine->getSeed() == 1u);
        REQUIRE(engine->getActiveVoiceCount() == 0u);
        REQUIRE(engine->getLastStolenVoiceIndex() == -1);
        REQUIRE(engine->getNonFiniteRecoveryCount() == 0u);
        REQUIRE(engine->getVoiceAllocationSerial(0u) == 0u);

        // Out-of-range indices return the neutral rather than reading out of
        // bounds; getVoice() folds to slot 0 so the reference is always valid.
        REQUIRE(engine->getVoiceLevel(VoragoEngine::kMaxVoices) == 0.0f);
        REQUIRE(engine->getVoiceState(VoragoEngine::kMaxVoices) == VoiceState::Idle);
        REQUIRE(engine->getVoiceAllocationSerial(VoragoEngine::kMaxVoices) == 0u);
        REQUIRE_FALSE(engine->getVoice(VoragoEngine::kMaxVoices).isPrepared());

        // The engine-owned macro bases read their ENGINE FIELDS, so they are the
        // S8.3 table's values BEFORE prepare() rather than the components'
        // (SpectralSmear defaults smear to 0, TapeSaturator to 0.5).
        REQUIRE(engine->getSmearAmount() == 0.20f);
        REQUIRE(engine->getSmearDecoherence() == 0.20f);
        REQUIRE(engine->getSmearTilt() == 0.0f);
        REQUIRE(engine->getSubToneLevelOffsetDb() == 0.0f);
        REQUIRE(engine->getSubTrackingAmount() == 1.0f);  // ruled 2026-09-19: 0.60 -> 1.0
        REQUIRE(engine->getAtmosBlur() == 0.85f);
        REQUIRE(engine->getOutputSaturation() == VoragoEngine::kOutputSaturation);
        REQUIRE(engine->getGhostPeakLevel() == VoragoEngine::kGhostBurstPeak);

        // Notes and clears on an unprepared engine are NO-OPS, not faults.
        engine->noteOn(60u, 100u);
        engine->noteOn(64u, 0u);  // velocity 0 routes to noteOff
        engine->noteOff(60u);
        (*engine).reset();
        engine->silence();
        REQUIRE_FALSE(engine->isPrepared());
        REQUIRE(engine->getActiveVoiceCount() == 0u);
        REQUIRE(engine->getLastStolenVoiceIndex() == -1);
    }

    SECTION("FR-043: polyphony is CLAMPED to [1, kMaxVoices], never rejected") {
        auto engine = std::make_unique<VoragoEngine>();

        // Before prepare(): legal, and it survives prepare() (FR-077).
        engine->setPolyphony(0u);
        REQUIRE(engine->getPolyphony() == 1u);
        engine->setPolyphony(999u);
        REQUIRE(engine->getPolyphony() == VoragoEngine::kMaxVoices);

        engine->prepare(kSampleRate48, VoragoEngineConfig{});
        REQUIRE(engine->isPrepared());
        REQUIRE(engine->getPolyphony() == VoragoEngine::kMaxVoices);

        // ...and after.
        engine->setPolyphony(0u);
        REQUIRE(engine->getPolyphony() == 1u);
        engine->setPolyphony(999u);
        REQUIRE(engine->getPolyphony() == VoragoEngine::kMaxVoices);
        engine->setPolyphony(VoragoEngine::kDefaultPolyphony);
        REQUIRE(engine->getPolyphony() == VoragoEngine::kDefaultPolyphony);

        // Polyphony 1 is a real configuration, not an edge the engine rejects:
        // one note fills the pool and the next one steals.
        engine->setPolyphony(1u);
        REQUIRE(engine->getPolyphony() == 1u);
        engine->noteOn(60u, 100u);
        REQUIRE(engine->getActiveVoiceCount() == 1u);
        REQUIRE(engine->getLastStolenVoiceIndex() == -1);
        engine->noteOn(67u, 100u);
        REQUIRE(engine->getActiveVoiceCount() == 1u);
        REQUIRE(engine->getLastStolenVoiceIndex() == 0);
    }

    SECTION("FR-043: a polyphony SHRINK is a musical release, never a retirement") {
        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});
        engine->setPolyphony(4u);
        engine->noteOn(60u, 100u);
        engine->noteOn(62u, 100u);
        engine->noteOn(64u, 100u);
        engine->noteOn(65u, 100u);
        REQUIRE(engine->getActiveVoiceCount() == 4u);

        // The allocator force-idles the excess slots and hands back NoteOff
        // events; the engine dispatches each as voices_[i].noteOff() and NEVER
        // as allocator_.voiceFinished(i). Nothing allocates.
        const std::size_t bytesBefore = engine->getAllocatedBytes();
        engine->setPolyphony(2u);
        REQUIRE(engine->getPolyphony() == 2u);
        REQUIRE(engine->getAllocatedBytes() == bytesBefore);
        // Only the slots below the new polyphony are reported active.
        REQUIRE(engine->getActiveVoiceCount() <= 2u);
        // Growing back allocates nothing either - prepare() prepared ALL
        // kMaxVoices slots regardless of polyphony (FR-042).
        engine->setPolyphony(VoragoEngine::kMaxVoices);
        REQUIRE(engine->getAllocatedBytes() == bytesBefore);
    }

    SECTION("FR-076: a NaN, zero or sub-floor sample rate is FLOORED, never rejected") {
        // Three arms, one per failure mode the spec names. In all three the
        // engine must come back PREPARED, with every one of the kMaxVoices slots
        // prepared too - "floor, do not reject" is the whole of FR-076.
        const std::array<double, 3> rates{makeNaNDouble(), 0.0, 4000.0};
        for (std::size_t i = 0; i < rates.size(); ++i) {
            INFO("arm " << i
                        << " (0 = NaN bit pattern, 1 = 0.0, 2 = 4000 Hz, below the "
                           "components' shared kMinUsableSampleRate of 8000)");
            auto engine = std::make_unique<VoragoEngine>();
            engine->prepare(rates[i], VoragoEngineConfig{});
            REQUIRE(engine->isPrepared());
            for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
                REQUIRE(engine->getVoice(v).isPrepared());
            }
            // Notes still route; nothing in the ladder throws or faults.
            engine->noteOn(36u, 100u);
            engine->noteOff(36u);
        }
    }

    SECTION("a degenerate config is CLAMPED by its owner, never rejected") {
        auto engine = std::make_unique<VoragoEngine>();
        engine->prepare(kSampleRate48,
                        VoragoEngineConfig{.maxBlockSamples = 0u,
                                           .voice = {},
                                           .atmosCaptureSeconds = 0.0f,
                                           .atmosBlurEnabled = true,
                                           .atmosFreezeEnabled = false,
                                           .atmosBlurFftSize = 1u,
                                           .atmosFreezeFftSize = 1u,
                                           .smearEnabled = true,
                                           .smearFftSize = 1u});
        REQUIRE(engine->isPrepared());
        // The atmosphere FLOORS its capture ring at 1 s rather than refusing:
        // the capacity is rounded up to a power of two, so 1 s at 48 kHz is at
        // least 48 000 samples.
        const std::size_t degenerateCapacity = engine->atmosphere().getCaptureCapacitySamples();
        REQUIRE(degenerateCapacity >= 48000u);

        // Re-preparing at the shipped config is legal and fully reconfigures
        // (FR-077), and the three VoragoEngineConfig fields that diverge from
        // their component defaults are observable on the real object:
        //   - atmosCaptureSeconds 20 s (FR-091: a ghost grain is 12 s long);
        //   - atmosFreezeEnabled FALSE, so no freeze FFT is built;
        //   - atmosBlurEnabled true at 1024, which is the layer's own latency.
        engine->prepare(kSampleRate48, VoragoEngineConfig{});
        REQUIRE(engine->isPrepared());
        const std::size_t shippedCapacity = engine->atmosphere().getCaptureCapacitySamples();
        INFO("capture capacity: degenerate = " << degenerateCapacity
                                               << ", shipped = " << shippedCapacity);
        REQUIRE(shippedCapacity >= 20u * 48000u);
        REQUIRE(shippedCapacity > degenerateCapacity);  // the field really is forwarded
        REQUIRE(engine->atmosphere().getFreezeFftSize() == 0u);
        REQUIRE(engine->atmosphere().getLatencySamples() == 1024u);
    }

    SECTION("FR-014: the envelope fan-out reaches EVERY slot, not just the pool") {
        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});
        // Deliberately below kMaxVoices: these four setters are CONFIGURATION,
        // so a later setPolyphony() growth must not admit a slot carrying a
        // different envelope. That is the property this section pins.
        engine->setPolyphony(2u);

        engine->setEnvelopeStageTimeMs(0, 50.0f);
        engine->setEnvelopeStageTimeMs(1, 60.0f);
        engine->setEnvelopeReleaseMs(100.0f);
        engine->setGrowthDurationSeconds(30.0f);
        engine->setEnvelopeMode(VoragoVoice::EnvelopeMode::Growth);

        REQUIRE(engine->getEnvelopeStageTimeMs(0) == 50.0f);
        REQUIRE(engine->getEnvelopeStageTimeMs(1) == 60.0f);
        REQUIRE(engine->getEnvelopeReleaseMs() == 100.0f);
        REQUIRE(engine->getGrowthDurationSeconds() == 30.0f);
        REQUIRE(engine->getEnvelopeMode() == VoragoVoice::EnvelopeMode::Growth);

        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            INFO("slot " << v);
            REQUIRE(engine->getVoice(v).getEnvelopeStageTimeMs(0) == 50.0f);
            REQUIRE(engine->getVoice(v).getEnvelopeStageTimeMs(1) == 60.0f);
            REQUIRE(engine->getVoice(v).getEnvelopeReleaseMs() == 100.0f);
            REQUIRE(engine->getVoice(v).getGrowthDurationSeconds() == 30.0f);
            REQUIRE(engine->getVoice(v).getEnvelopeMode() == VoragoVoice::EnvelopeMode::Growth);
        }

        // An out-of-range stage index is a silent no-op on set and returns the
        // neutral on get, on every slot.
        engine->setEnvelopeStageTimeMs(-1, 999.0f);
        engine->setEnvelopeStageTimeMs(9999, 999.0f);
        REQUIRE(engine->getEnvelopeStageTimeMs(-1) == 0.0f);
        REQUIRE(engine->getEnvelopeStageTimeMs(9999) == 0.0f);
        REQUIRE(engine->getEnvelopeStageTimeMs(0) == 50.0f);
    }
}

// =============================================================================
// SC-012 - FR-044's amnesty steal policy, as an ENUMERATED victim table
// =============================================================================
//
// THE RULE IS TESTED WHERE IT IS STATED. VoragoEngine::selectStealVictim is
// public and static precisely so the six rows below can be enumerated with no
// render at all - the same construction VoragoVoice::combineWake takes for
// SC-019a (vorago_voice.h:966-975). A render-driven table would test the level
// DETECTOR's trajectory at the same time as the selection rule, and a tuning
// change to kLevelReleaseMs would then move a criterion about stealing.
//
// The wiring - that VoragoEngine::noteOn actually consults this rule, frees the
// victim BEFORE the allocator call, and reports it through
// getLastStolenVoiceIndex() - is asserted separately at the bottom, through the
// real note path. SC-011 (T023) measures the ramp the teardown produces.

TEST_CASE("VoragoEngine_StealPolicy", "[systems][vorago]") {
    constexpr float kBelow = 0.01f;   // < kAmnestyLevelThreshold (0.0316)
    constexpr float kAbove = 0.50f;   // > kAmnestyLevelThreshold
    constexpr float kQuiet = 0.001f;  // quieter than everything, but ACTIVE

    SECTION("the enumerated victim table") {
        const std::array<StealRow, 8> rows{{
            // (a) An idle slot exists. The rule reports "nothing to steal" - and
            //     the engine never even asks, because noIdleVoice() is false.
            {"(a) an idle slot exists -> no victim",
             {VoiceState::Active, VoiceState::Idle, VoiceState::Active, VoiceState::Active},
             {kAbove, 0.0f, kAbove, kAbove},
             {1u, 0u, 2u, 3u},
             4u,
             -1},

            // (b) One Releasing slot below the amnesty line among Active ones:
            //     pass 0 finds it, and it wins even though slot 3 is QUIETER.
            //     That is the amnesty: it protects the loud, it does not prefer
            //     them, and a quieter ACTIVE voice does not outrank a released
            //     one that is already on its way out.
            {"(b) one Releasing below the line -> it",
             {VoiceState::Active, VoiceState::Releasing, VoiceState::Active, VoiceState::Active},
             {kAbove, kBelow, 0.40f, kQuiet},
             {1u, 2u, 3u, 4u},
             4u,
             1},

            // (c) Several Releasing, all below: the LOWEST LEVEL wins inside
            //     pass 0. Slot 3 is quieter still but is Active, i.e. pass 2.
            {"(c) several Releasing below -> the quietest of them",
             {VoiceState::Releasing, VoiceState::Releasing, VoiceState::Releasing,
              VoiceState::Active},
             {0.020f, 0.005f, 0.030f, kQuiet},
             {1u, 2u, 3u, 4u},
             4u,
             1},

            // (d) EVERY Releasing slot is ABOVE the line. Pass 0 finds nothing;
            //     PASS 1 - the branch that is not redundant - still steals the
            //     quietest Releasing voice rather than falling through to the
            //     Active one. Slot 2 is far quieter and is NOT the victim.
            {"(d) all Releasing above the line -> pass 1, not pass 2",
             {VoiceState::Releasing, VoiceState::Releasing, VoiceState::Active,
              VoiceState::Active},
             {0.50f, 0.20f, kQuiet, 0.90f},
             {1u, 2u, 3u, 4u},
             4u,
             1},

            // (e) Only Active slots: pass 2, quietest wins.
            {"(e) only Active -> the quietest Active",
             {VoiceState::Active, VoiceState::Active, VoiceState::Active, VoiceState::Active},
             {0.50f, 0.20f, 0.90f, 0.05f},
             {1u, 2u, 3u, 4u},
             4u,
             3},

            // (f) An EXACT level tie goes to the LOWER serial - the older
            //     allocation. Slot 3 was allocated first, so slot 3 goes.
            {"(f) exact tie -> the lower voiceSerial_",
             {VoiceState::Active, VoiceState::Active, VoiceState::Active, VoiceState::Active},
             {0.25f, 0.25f, 0.25f, 0.25f},
             {9u, 4u, 7u, 2u},
             4u,
             3},

            // (f') ...and the same tie-break inside the amnesty band, so the
            //      rule is one rule and not two.
            {"(f') exact tie inside pass 0 -> the lower voiceSerial_",
             {VoiceState::Releasing, VoiceState::Releasing, VoiceState::Releasing,
              VoiceState::Releasing},
             {kBelow, kBelow, kBelow, kBelow},
             {5u, 3u, 8u, 1u},
             4u,
             3},

            // (g) Nothing is a candidate in any pass.
            {"(g) an all-Idle pool -> no victim",
             {VoiceState::Idle, VoiceState::Idle, VoiceState::Idle, VoiceState::Idle},
             {0.0f, 0.0f, 0.0f, 0.0f},
             {1u, 2u, 3u, 4u},
             4u,
             -1},
        }};

        for (const StealRow& row : rows) {
            INFO(row.name);
            REQUIRE(runRow(row) == row.expected);
        }
    }

    SECTION("a NON-FINITE level is not eligible for the amnesty band") {
        // `!(level < threshold)` and NOT `level >= threshold`: with the latter a
        // NaN level would compare false, fall THROUGH the guard and be treated
        // as a pass-0 candidate - i.e. as the quietest voice in the pool, which
        // is the exact opposite of what a poisoned slot deserves. FR-072
        // contains a non-finite voice but does not make one impossible, so this
        // is a reachable state.
        const StealRow row{"NaN level, Releasing",
                           {VoiceState::Releasing, VoiceState::Releasing, VoiceState::Idle,
                            VoiceState::Idle},
                           {makeNaNFloat(), kBelow, 0.0f, 0.0f},
                           {1u, 2u, 0u, 0u},
                           2u,
                           1};
        REQUIRE(runRow(row) == row.expected);
    }

    SECTION("an empty or zero-length pool selects nothing") {
        REQUIRE(VoragoEngine::selectStealVictim({}, {}, {}) == -1);
    }

    SECTION("the rule is WIRED: noteOn frees the chosen victim before allocating") {
        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});
        engine->setPolyphony(4u);

        // (a) end to end. Four distinct notes fill the four slots and NOTHING is
        // stolen - the allocator's own idle search has a candidate every time.
        engine->noteOn(60u, 100u);
        engine->noteOn(62u, 100u);
        engine->noteOn(64u, 100u);
        engine->noteOn(65u, 100u);
        REQUIRE(engine->getActiveVoiceCount() == 4u);
        REQUIRE(engine->getLastStolenVoiceIndex() == -1);

        // The serials are the allocation order the tie-break reads, and they are
        // strictly increasing across note events.
        for (std::size_t v = 1; v < 4u; ++v) {
            REQUIRE(engine->getVoiceAllocationSerial(v)
                    > engine->getVoiceAllocationSerial(v - 1u));
        }

        // (f) end to end. Nothing has rendered, so every level is exactly 0 -
        // a four-way tie in pass 2, decided on the serial. Slot 0 was allocated
        // first, so slot 0 is the victim, and the pool stays at four.
        engine->noteOn(67u, 100u);
        REQUIRE(engine->getLastStolenVoiceIndex() == 0);
        REQUIRE(engine->getActiveVoiceCount() == 4u);
        REQUIRE(engine->getVoiceState(0u) == VoiceState::Active);

        // THE TEETH ON THE TIE-BREAK. The steal re-serialised slot 0 to the
        // NEWEST allocation, so the next steal must take slot 1 - the new
        // oldest - and NOT slot 0 again. A "lowest index" tie-break would pick
        // slot 0 twice, pass the assertion above and fail this one.
        REQUIRE(engine->getVoiceAllocationSerial(0u) > engine->getVoiceAllocationSerial(1u));
        engine->noteOn(69u, 100u);
        REQUIRE(engine->getLastStolenVoiceIndex() == 1);
        REQUIRE(engine->getActiveVoiceCount() == 4u);

        // A same-note retrigger is NOT a steal: that note already owns a slot
        // (slot 1, which the steal above just handed note 69), so nothing is
        // saturated from its point of view and the report does not move.
        engine->noteOn(69u, 100u);
        REQUIRE(engine->getLastStolenVoiceIndex() == 1);
        REQUIRE(engine->getActiveVoiceCount() == 4u);

        // reset() clears the report, because after it no slot is carrying the
        // stolen voice's tail any more.
        (*engine).reset();
        REQUIRE(engine->getLastStolenVoiceIndex() == -1);
    }
}

// =============================================================================
// FR-048 - the seed is per SLOT and is NEVER advanced per note
// =============================================================================

TEST_CASE("VoragoEngine_SeedIsPerSlotNotPerNote", "[systems][vorago]") {
    // FR-045's disjointness, checked where it can be checked at compile time:
    // the voice's own salts occupy 0x0100..0x0C01 and the engine's base sits
    // above all of them, so no slot's derivation can collide with one of its own
    // sub-streams. vorago_voice.h carries the mirror-image static_assert.
    STATIC_REQUIRE(VoragoVoice::kSlotDrawSaltBase + VoragoVoice::kNumEventSchedulers
                   < VoragoEngine::kVoiceSaltBase);
    STATIC_REQUIRE(VoragoEngine::kVoiceSaltBase + VoragoEngine::kMaxVoices
                   < VoragoEngine::kAtmosSalt);

    // A short capture ring: the atmosphere is not what this case measures, and
    // 20 s x 48 kHz x 2 channels of ring per prepare() is pure wall clock here.
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    auto engine = makeEngine(kSampleRate48, cfg);

    SECTION("every slot seed is the documented derivation, and they are distinct") {
        REQUIRE(engine->getSeed() == 1u);
        std::array<std::uint32_t, VoragoEngine::kMaxVoices> derived{};
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            INFO("slot " << v);
            derived[v] = engine->getVoice(v).getSeed();
            REQUIRE(derived[v]
                    == deriveStreamSeed(engine->getSeed(), VoragoEngine::kVoiceSaltBase + v));
        }
        for (std::size_t a = 0; a < derived.size(); ++a) {
            for (std::size_t b = a + 1u; b < derived.size(); ++b) {
                INFO("slots " << a << " and " << b);
                REQUIRE(derived[a] != derived[b]);
            }
        }
    }

    SECTION("noteOn / noteOff / steal cycles leave EVERY seed untouched") {
        std::array<std::uint32_t, VoragoEngine::kMaxVoices> before{};
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            before[v] = engine->getVoice(v).getSeed();
        }
        const std::uint32_t engineSeedBefore = engine->getSeed();

        engine->setPolyphony(4u);
        for (int cycle = 0; cycle < 4; ++cycle) {
            // Fill the pool...
            engine->noteOn(48u, 100u);
            engine->noteOn(50u, 100u);
            engine->noteOn(52u, 100u);
            engine->noteOn(53u, 100u);
            // ...saturate it so the next two notes must STEAL...
            engine->noteOn(55u, 100u);
            engine->noteOn(57u, 100u);
            REQUIRE(engine->getLastStolenVoiceIndex() >= 0);
            // ...then release everything, including the notes the steals evicted.
            for (std::uint8_t note = 48u; note <= 57u; ++note) {
                engine->noteOff(note);
            }
        }

        REQUIRE(engine->getSeed() == engineSeedBefore);
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            INFO("slot " << v << " after four fill/steal/release cycles");
            // THE WHOLE OF FR-048. A seed that advanced per note would make
            // every render of the same preset differ, and would make SC-026's
            // "the same slot playing the same note twice reproduces its first
            // trajectory" false.
            REQUIRE(engine->getVoice(v).getSeed() == before[v]);
        }
    }

    SECTION("setSeed re-derives every slot; prepare() preserves what it derived") {
        constexpr std::uint32_t kNewSeed = 0x12345678u;
        engine->setSeed(kNewSeed);
        REQUIRE(engine->getSeed() == kNewSeed);
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            INFO("slot " << v);
            REQUIRE(engine->getVoice(v).getSeed()
                    == deriveStreamSeed(kNewSeed, VoragoEngine::kVoiceSaltBase + v));
        }

        // A re-prepare() is a reconfiguration, not a re-seed (FR-077): the
        // engine seed and every slot's derivation survive it unchanged.
        engine->prepare(kSampleRate48, cfg);
        REQUIRE(engine->getSeed() == kNewSeed);
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            INFO("slot " << v << " after re-prepare");
            REQUIRE(engine->getVoice(v).getSeed()
                    == deriveStreamSeed(kNewSeed, VoragoEngine::kVoiceSaltBase + v));
        }

        // Seed 0 is LEGAL and is a distinct engine seed: deriveStreamSeed
        // substitutes 0x2545F491 only when the HASH lands on 0
        // (core/random.h:102-111), so the derivation, not the raw value, is what
        // reaches each slot.
        engine->setSeed(0u);
        REQUIRE(engine->getSeed() == 0u);
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            INFO("slot " << v << " at engine seed 0");
            REQUIRE(engine->getVoice(v).getSeed() != 0u);
            REQUIRE(engine->getVoice(v).getSeed()
                    == deriveStreamSeed(0u, VoragoEngine::kVoiceSaltBase + v));
        }
    }
}

// =============================================================================
// SC-007 (engine half) - partition invariance, asserted as exactly as FR-007
// requires it
// =============================================================================
//
// FR-007 demands exactness - "must leave identical state and produce identical
// output" - and all three arms are the same build in the same process, so this
// is a bit-identity check over the whole render and NOT a fingerprint.
// render_fingerprint.h's tolerances exist for cross-toolchain spread, and at
// kMetricTolerance = 2.5e-4 a genuine partition-dependent drift (a control step
// counted per CALL rather than per 64 elapsed SAMPLES, a smoother advanced by
// `slice` instead of by the chunk) stays comfortably inside them on a
// slowly-evolving drone and passes.

TEST_CASE("VoragoEngine_PartitionInvariance", "[systems][vorago]") {
    constexpr std::size_t kTotal = 4096u;

    // The pathological split. 36 and 28 straddle exactly one chunk boundary, 1
    // is a single sample mid-chunk, and 2047/1984 are long blocks that never
    // re-align - so every arm of the chunk walk (partial slice, chunk-aligned
    // slice, multi-chunk block) is exercised.
    constexpr std::array<std::size_t, 5> kPathological{36u, 28u, 1u, 2047u, 1984u};
    static_assert(kPathological[0] + kPathological[1] + kPathological[2] + kPathological[3]
                          + kPathological[4]
                      == kTotal,
                  "SC-007: the pathological split must sum to EXACTLY 4096, or the three arms are "
                  "not rendering the same number of samples and the comparison is meaningless");
    constexpr std::array<std::size_t, 1> kOneShot{kTotal};
    constexpr std::array<std::size_t, 1> kEightBlocks{512u};

    // A short capture ring: the atmosphere's 20 s default is 2 x 20 x fs floats
    // per prepare() and this case builds three engines. All three share it, so
    // the comparison is untouched.
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    const auto build = [&cfg]() {
        auto engine = std::make_unique<VoragoEngine>();
        engine->setSeed(0x5C0071u);
        engine->prepare(kSampleRate48, cfg);
        applyFastAttack(*engine);
        engine->setPolyphony(2u);
        // Two notes, so the sum, the steal-free allocation path and the
        // per-slot life-only advance on the six spare slots all run.
        engine->noteOn(45u, 100u);
        engine->noteOn(52u, 90u);
        return engine;
    };

    auto eOne = build();
    auto eEight = build();
    auto eSplit = build();

    const StereoRender one =
        renderPartitioned(*eOne, kTotal, std::span<const std::size_t>(kOneShot));
    const StereoRender eight =
        renderPartitioned(*eEight, kTotal, std::span<const std::size_t>(kEightBlocks));
    const StereoRender split =
        renderPartitioned(*eSplit, kTotal, std::span<const std::size_t>(kPathological));

    // NON-VACUITY FIRST. Three silent buffers are bit-identical for the wrong
    // reason; the fast-attack fixture exists precisely so this render is
    // audible, and the smear's fftSize of latency is why 4096 rather than 1024.
    INFO("peak = " << peakOf(std::span<const float>(one.l)) << " (L), "
                   << peakOf(std::span<const float>(one.r))
                   << " (R), latency = " << eOne->getLatencySamples());
    REQUIRE(peakOf(std::span<const float>(one.l)) > 0.0f);
    REQUIRE(peakOf(std::span<const float>(one.r)) > 0.0f);
    REQUIRE(allFinite(one));

    REQUIRE(bitIdentical(one.l, eight.l));
    REQUIRE(bitIdentical(one.r, eight.r));
    REQUIRE(bitIdentical(one.l, split.l));
    REQUIRE(bitIdentical(one.r, split.r));

    // ...and the allocator's view agrees EXACTLY, which is the clause that reads
    // the deferred retirement. Retirement runs in the post-render control step
    // on the ABSOLUTE grid; run "once per block" instead it would fire a
    // different number of times per arm and this line would separate them while
    // the buffers above could still look alike.
    INFO("active voices: one-shot " << eOne->getActiveVoiceCount() << ", 8x512 "
                                    << eEight->getActiveVoiceCount() << ", pathological "
                                    << eSplit->getActiveVoiceCount());
    REQUIRE(eEight->getActiveVoiceCount() == eOne->getActiveVoiceCount());
    REQUIRE(eSplit->getActiveVoiceCount() == eOne->getActiveVoiceCount());
    REQUIRE(eEight->getRenderingVoiceCount() == eOne->getRenderingVoiceCount());
    REQUIRE(eSplit->getRenderingVoiceCount() == eOne->getRenderingVoiceCount());
}

// =============================================================================
// SC-022 - latency is reported correctly (FR-052)
// =============================================================================

TEST_CASE("VoragoEngine_ReportedLatency", "[systems][vorago]") {
    SECTION("clause 1: the engine forwards SpectralSmear's latency, at every configuration") {
        struct Arm {
            const char* name;
            bool enabled;
            std::size_t fftSize;
            std::size_t expected;
        };
        const std::array<Arm, 3> arms{{
            {"enabled, fftSize 2048", true, 2048u, 2048u},
            {"enabled, fftSize 1024", true, 1024u, 1024u},
            // THE CLAUSE THAT MAKES THE FORWARDING MEAN SOMETHING: disabled, and
            // BOTH are 0. A getter returning a hard-coded fftSize would pass the
            // two rows above and fail here.
            {"DISABLED - both are 0", false, 2048u, 0u},
        }};

        for (const Arm& arm : arms) {
            INFO(arm.name);
            VoragoEngineConfig cfg{};
            cfg.atmosCaptureSeconds = 1.0f;
            cfg.smearEnabled = arm.enabled;
            cfg.smearFftSize = arm.fftSize;
            auto engine = makeEngine(kSampleRate48, cfg);
            REQUIRE(engine->getLatencySamples() == engine->smear().getLatencySamples());
            REQUIRE(engine->getLatencySamples() == arm.expected);
        }

        // An UNPREPARED engine reports 0, and still by forwarding.
        auto fresh = std::make_unique<VoragoEngine>();
        REQUIRE(fresh->getLatencySamples() == fresh->smear().getLatencySamples());
        REQUIRE(fresh->getLatencySamples() == 0u);
    }

    SECTION("clause 2: the onset moves by exactly the reported latency") {
        constexpr std::size_t kFftSize = 1024u;
        constexpr std::size_t kTotal = 8192u;
        constexpr std::array<std::size_t, 1> kOneShot{kTotal};

        // THE NOTE STARTS AFTER A DISCARDED PRELUDE, and the prelude is what
        // makes the +/-1-sample bound a property a correct implementation has.
        //
        // "identity delayed by fftSize" (spectral_smear.h:13, A-10) is true
        // OUTSIDE the STFT warm-up only. Output [0, fftSize) is the warm-up
        // counter's literal zeros and output [fftSize, 2*fftSize - hop) is
        // OverlapAdd's COLA ramp-up, where the synthesis windows have not yet
        // summed to the COLA constant (stft.h:289-315 accumulates w^2 *
        // colaNormalization_ and COLA needs 4 frames, :249-262). The component's
        // OWN criteria are all told to skip it: its round-trip null discards
        // 2 * fftSize (spectral_smear_test.cpp:749) and its impulse arm places
        // the impulse at 2 * fftSize precisely so the peak lands clear of
        // "[fftSize, 2*fftSize - hop)" (:346, vorago-phase4 tasks.md:99,
        // plan.md:1519-1524).
        //
        // With noteOn at sample 0 the delayed onset landed at output 1243 -
        // INSIDE [1024, 1792) at this geometry - where the ramp still attenuates
        // the signal, so the threshold crossing arrived 155 samples late and the
        // case measured the warm-up taper rather than the latency. Rendering
        // 2 * fftSize samples of pre-note silence first walks the smear through
        // the whole warm-up on zeros; every sample measured afterwards is in the
        // exact-identity region. NOTHING about the criterion is relaxed: the
        // bound is still +/-1 sample against getLatencySamples(), and both arms
        // get the identical prelude, so the difference is still theirs alone.
        constexpr std::size_t kPreludeSamples = 2u * kFftSize;
        constexpr std::array<std::size_t, 1> kPreludePattern{kPreludeSamples};

        const auto build = [](bool smearEnabled) {
            VoragoEngineConfig cfg{};
            cfg.atmosCaptureSeconds = 1.0f;
            cfg.smearEnabled = smearEnabled;
            cfg.smearFftSize = kFftSize;

            auto engine = std::make_unique<VoragoEngine>();
            // A-10. ONLY at smearAmount 0 AND decoherence 0 is SpectralSmear an
            // exact identity delayed by fftSize (spectral_smear.h:13); at the
            // S8.3 defaults it actively smears, the onset is spread along with
            // everything else, and a +/-1-sample equality is not a property a
            // correct implementation has.
            //
            // Set BEFORE prepare(), so prepare()'s step 5c SNAPS the smear's
            // control smoothers to 0 (spectral_smear.h:271-278) instead of
            // ramping down from the 0.20 default straight across the onset.
            engine->setSmearAmount(0.0f);
            engine->setSmearDecoherence(0.0f);
            // The ghost is a parallel wet path summed into the same bus, so a
            // burst during the onset window would be indistinguishable from
            // program material at the threshold. Base 0 silences it outright.
            engine->setGhostPeakLevel(0.0f);
            // THE SUBHARMONIC STAGE IS THE SECOND SUCH PATH, and unlike the
            // ghost it sounds CONTINUOUSLY. Its three tones are oscillators, not
            // a filtered copy of the bus: with FR-051's held fundamental (55 Hz
            // out of prepare) and tracking at the S8.3 default 0.60 the chain
            // leaves a floor, and a silent engine measured 0.0296 (-30.6 dBFS)
            // of steady 55 Hz hum with no note allocated. That floor is FIVE
            // TIMES the note onset's own threshold here, so with it present
            // `firstAbove` indexes the hum's rise rather than the note's, in
            // BOTH arms - which is what the original form of this case measured
            // (onset 64 in the undelayed arm, before a 50 ms attack can have
            // produced anything). -60 dB is SubharmonicEngine::kMinToneLevelDb,
            // the exact fader bottom whose gain is a LITERAL 0.0f
            // (subharmonic_engine.h:210, :978), so this removes the stage
            // exactly rather than merely attenuating it.
            //
            // NOTHING IS RELAXED: SC-022 measures SpectralSmear's latency, and
            // the sub sits BEFORE the smear in the chain, so its presence or
            // absence cannot move the quantity under test. The ghost is
            // silenced two lines up for exactly this reason.
            engine->setSubToneLevelOffsetDb(-60.0f);
            engine->prepare(kSampleRate48, cfg);
            applyFastAttack(*engine);
            engine->setPolyphony(1u);
            return engine;
        };

        auto withSmear = build(true);
        auto without = build(false);

        // FR-026's fold re-writes the component from `smearBase_ + max fog`
        // EVERY control chunk, so setSmearAmount(0) only survives while the
        // sounding voice's tidal lane is at or below zero - the lane being a NET
        // (vorago_voice.h:951). The seed search below picks a slot-0 draw that is
        // comfortably negative; at a ~180 s layer period the lane cannot climb
        // 0.02 inside this 0.21 s render (prelude included).
        const long long seed = findSeedForTide(*withSmear, &tideIsClearlyNegative, 0x1A7E0Cu, 48);
        INFO("tidal seed search result: " << seed);
        REQUIRE(seed >= 0);
        const auto chosen = static_cast<std::uint32_t>(seed);
        // Both engines take the SAME seed and the same single reset, so slot 0's
        // six sine phases - and therefore the lane - are identical in both.
        withSmear->setSeed(chosen);
        (*withSmear).reset();
        without->setSeed(chosen);
        (*without).reset();
        REQUIRE(withSmear->getVoice(0).tide().getCurrentValue()
                == without->getVoice(0).tide().getCurrentValue());

        // The discarded prelude. Both arms render it through their own
        // processStereoBlock, so the smear-enabled arm's OverlapAdd completes
        // its COLA ramp-up on silence before the note exists.
        const StereoRender preludeA = renderPartitioned(
            *withSmear, kPreludeSamples, std::span<const std::size_t>(kPreludePattern));
        const StereoRender preludeB = renderPartitioned(
            *without, kPreludeSamples, std::span<const std::size_t>(kPreludePattern));
        // THE GUARD THAT MAKES THE ONSET AN ONSET. With no note allocated, the
        // ghost at base 0 and the sub tones at their exact fader bottom, the
        // engine's output is EXACTLY silent - so the first sample above the
        // threshold below really is the note arriving and not a standing floor
        // that both arms already carry. Exact 0.0f, not "small": every path that
        // could contribute is off, and a non-zero here means one of them is back
        // and the measurement is confounded again.
        REQUIRE(peakOf(std::span<const float>(preludeA.l)) == 0.0f);
        REQUIRE(peakOf(std::span<const float>(preludeB.l)) == 0.0f);

        withSmear->noteOn(45u, 100u);
        without->noteOn(45u, 100u);
        const StereoRender a =
            renderPartitioned(*withSmear, kTotal, std::span<const std::size_t>(kOneShot));
        const StereoRender b =
            renderPartitioned(*without, kTotal, std::span<const std::size_t>(kOneShot));

        const std::size_t latency = withSmear->getLatencySamples();
        REQUIRE(latency == kFftSize);
        REQUIRE(without->getLatencySamples() == 0u);
        // The fold really did leave the identity alone.
        REQUIRE(withSmear->smear().getSmearAmount() == 0.0f);
        REQUIRE(withSmear->smear().getDecoherence() == 0.0f);

        // The threshold comes from the UNDELAYED arm over the window both arms
        // share, so it indexes the same feature of the same signal in both.
        const float peak = peakOf(std::span<const float>(b.l.data(), kTotal - latency));
        REQUIRE(peak > 0.0f);
        const float threshold = 0.05f * peak;

        const std::size_t onsetWith = firstAbove(std::span<const float>(a.l), threshold);
        const std::size_t onsetWithout = firstAbove(std::span<const float>(b.l), threshold);
        INFO("onset with smear = " << onsetWith << ", without = " << onsetWithout
                                   << ", reported latency = " << latency
                                   << ", threshold = " << threshold);
        REQUIRE(onsetWith < kTotal);
        REQUIRE(onsetWithout < kTotal);

        const auto delta = static_cast<long long>(onsetWith) - static_cast<long long>(onsetWithout);
        const long long error = delta - static_cast<long long>(latency);
        REQUIRE(error <= 1);
        REQUIRE(error >= -1);
    }
}

// =============================================================================
// SC-031 - the sub-fundamental is HELD, never reset (FR-051)
// =============================================================================

TEST_CASE("VoragoEngine_HeldSubFundamental", "[systems][vorago]") {
    // 8 kHz is the components' shared kMinUsableSampleRate, and the criterion
    // pins a >= 10 SECOND gap - a duration, not a sample count. At 8 kHz that is
    // 88 000 samples instead of 528 000, and nothing this case measures (a held
    // scalar and a sample-to-sample delta) is rate dependent.
    constexpr double kSampleRate8k = 8000.0;
    constexpr std::size_t kChunk = VoragoEngine::kControlChunkSamples;
    constexpr std::array<std::size_t, 1> kChunkPattern{kChunk};
    constexpr std::size_t kPreSamples = 24000u;  // 3 s
    constexpr std::size_t kGapSamples = 88000u;  // 11 s - SC-031 asks for >= 10

    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    auto engine = std::make_unique<VoragoEngine>();
    engine->setSeed(0x5B0F17u);
    // The ghost is an EVENT: a burst opening inside the gap would show up in the
    // click statistic as a level step that has nothing to do with FR-051. Base 0
    // takes it out of the measurement without touching the sub path.
    engine->setGhostPeakLevel(0.0f);
    engine->prepare(kSampleRate8k, cfg);
    applyFastAttack(*engine);
    engine->setPolyphony(2u);

    // MIDI 24 = 32.70 Hz, the lowest note this case plays.
    engine->noteOn(24u, 100u);
    const StereoRender pre =
        renderPartitioned(*engine, kPreSamples, std::span<const std::size_t>(kChunkPattern));
    REQUIRE(allFinite(pre));

    const float held = engine->subharmonic().getFundamentalHz();
    INFO("held fundamental = " << held << " Hz");
    REQUIRE(held > 0.0f);
    REQUIRE(std::fabs(held - 32.703f) < 0.5f);

    // The reference is the STEADY-STATE maximum step, measured over the second
    // half of the pre-gap render so the fast-attack onset ramp is not what sets
    // the bar.
    const float reference = maxAbsDelta(
        std::span<const float>(pre.l.data() + (kPreSamples / 2u), kPreSamples / 2u));
    REQUIRE(reference > 0.0f);

    engine->noteOff(24u);

    std::vector<float> l(kChunk, 0.0f);
    std::vector<float> r(kChunk, 0.0f);
    float previous = pre.l[kPreSamples - 1u];
    float worstGapDelta = 0.0f;
    bool heldThroughout = true;
    bool finiteThroughout = true;
    for (std::size_t done = 0; done < kGapSamples; done += kChunk) {
        engine->processStereoBlock(l.data(), r.data(), kChunk);
        for (std::size_t i = 0; i < kChunk; ++i) {
            if (!isFiniteBits(l[i]) || !isFiniteBits(r[i])) {
                finiteThroughout = false;
            }
            worstGapDelta = std::max(worstGapDelta, std::fabs(l[i] - previous));
            previous = l[i];
        }
        // THE WHOLE OF FR-051, asserted at EVERY control chunk of the gap rather
        // than only at its end: an implementation that reset the fundamental to
        // a default the moment the last voice retired - which is what would
        // glissando the subs on every note - would be invisible to an end-only
        // check once a later note re-wrote it.
        if (engine->subharmonic().getFundamentalHz() != held) {
            heldThroughout = false;
        }
    }
    INFO("worst gap step = " << worstGapDelta << ", steady-state reference = " << reference
                             << ", bound = " << (1.5f * reference));
    REQUIRE(finiteThroughout);
    REQUIRE(heldThroughout);
    REQUIRE(worstGapDelta <= 1.5f * reference);

    // ...and the value is HELD, not FROZEN. A LOWER note is chosen deliberately:
    // it becomes the lowest sounding voice whether or not the first slot has
    // retired by now, so the assertion does not depend on the retirement clock.
    engine->noteOn(12u, 100u);  // MIDI 12 = 16.35 Hz
    engine->processStereoBlock(l.data(), r.data(), kChunk);
    const float moved = engine->subharmonic().getFundamentalHz();
    INFO("fundamental after the lower note = " << moved << " Hz");
    REQUIRE(moved != held);
    REQUIRE(std::fabs(moved - 16.352f) < 0.5f);
}

// =============================================================================
// FR-026 (engine half) - the tidal fog fold, and the precedence rule no success
// criterion covers
// =============================================================================
//
// This is the case that fails if setSmearAmount() writes the COMPONENT instead
// of `smearBase_`, or if getSmearAmount() reports the component: the fold
// re-writes SpectralSmear every control chunk, so a component-as-base
// implementation accumulates the lane into its own base and walks off to 1.0,
// and a component-reading getter reports a number the macro matrix does not own.

TEST_CASE("VoragoEngine_TidalFogFold", "[systems][vorago]") {
    constexpr double kSampleRate8k = 8000.0;
    constexpr std::size_t kChunk = VoragoEngine::kControlChunkSamples;

    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    std::vector<float> l(kChunk, 0.0f);
    std::vector<float> r(kChunk, 0.0f);

    SECTION("no rendering voice publishes fog: the component IS smearBase_, exactly") {
        auto engine = makeEngine(kSampleRate8k, cfg);
        REQUIRE(engine->getSmearAmount() == 0.20f);  // the S8.3 default

        for (int chunk = 0; chunk < 8; ++chunk) {
            INFO("chunk " << chunk);
            engine->processStereoBlock(l.data(), r.data(), kChunk);
            REQUIRE(engine->smear().getSmearAmount() == engine->getSmearAmount());
        }

        // ...INCLUDING after a non-neutral write, which is precisely what an
        // apply() at a non-neutral Fog does to this Engine-owned target (T017).
        // A fold that summed the lane onto the COMPONENT would leave this at
        // 0.73 too - but only because the lane is 0 here, which is why the
        // sounding-voice section below exists.
        engine->setSmearAmount(0.73f);
        for (int chunk = 0; chunk < 8; ++chunk) {
            INFO("chunk " << chunk << " after setSmearAmount(0.73)");
            engine->processStereoBlock(l.data(), r.data(), kChunk);
            REQUIRE(engine->getSmearAmount() == 0.73f);
            REQUIRE(engine->smear().getSmearAmount() == 0.73f);
        }
    }

    SECTION("with a sounding voice: clamp(smearBase_ + max fog), at every control step") {
        auto engine = std::make_unique<VoragoEngine>();
        engine->prepare(kSampleRate8k, cfg);

        // A draw whose slot-0 tidal lane is clearly POSITIVE, so the fold's
        // second term is non-zero and the two wrong implementations above are
        // actually separable. Without this the section could pass vacuously.
        const long long seed = findSeedForTide(*engine, &tideIsClearlyPositive, 0xF0607Du, 48);
        INFO("tidal seed search result: " << seed);
        REQUIRE(seed >= 0);

        applyFastAttack(*engine);
        // Polyphony 1 makes slot 0 the ONLY rendering voice: every other slot is
        // isFinished() out of clearRunState() (vorago_voice.h:1365) and takes the
        // advanceLifeOnly path, so `max over RENDERING voices` is exactly
        // getVoice(0).getTidalFogDepth() and the expectation below is readable
        // from the public surface.
        engine->setPolyphony(1u);
        REQUIRE(engine->getVoice(0).getTidalDepth() == 0.40f);  // S8.2's shipped depth

        engine->noteOn(45u, 100u);

        float bestFog = 0.0f;
        for (int chunk = 0; chunk < 24; ++chunk) {
            INFO("chunk " << chunk);
            // Captured BEFORE the block: the pre-render control step at phase 0
            // reads the value the voice published at its PREVIOUS control step,
            // and the voice re-publishes while rendering this one.
            const float fogBefore = engine->getVoice(0).getTidalFogDepth();
            engine->processStereoBlock(l.data(), r.data(), kChunk);
            REQUIRE(engine->smear().getSmearAmount()
                    == std::clamp(engine->getSmearAmount() + fogBefore, 0.0f, 1.0f));
            bestFog = std::max(bestFog, fogBefore);
        }
        INFO("best tidal fog observed = " << bestFog);
        REQUIRE(bestFog > 0.0f);

        // THE PRECEDENCE RULE. The base is re-written mid-render and the fold
        // recomputes the component FROM IT on the very next chunk - never from
        // whatever the component happened to be holding. 0.95 + a positive lane
        // also exercises the upper clamp.
        const std::array<float, 3> bases{0.95f, 0.05f, 0.40f};
        for (const float base : bases) {
            engine->setSmearAmount(base);
            const float fogBefore = engine->getVoice(0).getTidalFogDepth();
            engine->processStereoBlock(l.data(), r.data(), kChunk);
            INFO("base " << base << ", fog " << fogBefore);
            REQUIRE(engine->getSmearAmount() == base);
            REQUIRE(engine->smear().getSmearAmount() == std::clamp(base + fogBefore, 0.0f, 1.0f));
        }
    }
}

// =============================================================================
// T016 - the shared fixtures themselves (tests/test_helpers/vorago_fixtures.h)
// =============================================================================
// The fixture header is infrastructure that other criteria LEAN ON rather than
// test: makeEngine() is the only legal construction path (a stack-local
// VoragoEngine overflows MSVC's 1 MiB main-thread stack), applyFastAttack() is
// what brings SC-021a's, SC-008's and SC-022 clause 2's renders to a steady
// state inside their windows, and renderEngine() is the standard partition. A
// silent bug in any of the three would WEAKEN those criteria instead of failing
// them - a fixture that quietly left the 20 s shipped attack in place would turn
// SC-021a's 1 s render into a measurement of the first 5 % of an onset - so the
// three are pinned here, in the TU that owns the engine.

TEST_CASE("VoragoFixtures_FastAttackAndMakeEngine", "[systems][vorago]") {
    SECTION("clause 1: makeEngine returns a PREPARED engine on the heap") {
        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});

        REQUIRE(engine != nullptr);
        REQUIRE(engine->isPrepared());
        REQUIRE(engine->getPolyphony() == VoragoEngine::kDefaultPolyphony);
        // prepare() rewinds the FR-072 counter, so a fresh engine starts clean -
        // which is what lets every later case read the counter as an assertion.
        REQUIRE(engine->getNonFiniteRecoveryCount() == 0u);
    }

    SECTION("clause 2: applyFastAttack reaches EVERY slot, inside FR-014a's 100 ms ceiling") {
        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});

        // NON-VACUITY FIRST. The shipped attack is 20 000 ms
        // (vorago_voice.h:322), so "<= 100 ms" is only evidence that the fixture
        // did something if the engine demonstrably held the slow shape before
        // the call.
        REQUIRE(engine->getEnvelopeStageTimeMs(0) == VoragoVoice::kDefaultStageTimesMs[0]);
        REQUIRE(engine->getEnvelopeReleaseMs() == VoragoVoice::kDefaultReleaseMs);

        applyFastAttack(*engine);

        // EVERY slot, not just the one the engine's getters read (slot 0): the
        // fan-out over all kMaxVoices is what stops a later setPolyphony()
        // growth from admitting a slot that still carries the 20 s attack.
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            const VoragoVoice& voice = engine->getVoice(v);
            INFO("slot " << v);
            REQUIRE(voice.getEnvelopeMode() == VoragoVoice::EnvelopeMode::Standard);
            // Stage 0 IS the attack (vorago_voice.h:315).
            REQUIRE(voice.getEnvelopeStageTimeMs(0) <= 100.0f);
            for (int stage = 0; stage < VoragoVoice::kEnvelopeStages; ++stage) {
                INFO("stage " << stage << " = " << voice.getEnvelopeStageTimeMs(stage) << " ms");
                REQUIRE(voice.getEnvelopeStageTimeMs(stage) <= 100.0f);
            }
            REQUIRE(voice.getEnvelopeReleaseMs() == 100.0f);
        }

        // ...and the engine's own fan-out getters agree with the POD, so a case
        // that reads the configuration back through the engine reads the same
        // numbers the fixture states.
        REQUIRE(engine->getEnvelopeStageTimeMs(0) == kFastAttackEnvelopeConfig.stages[0].ms);
        REQUIRE(engine->getEnvelopeReleaseMs() == kFastAttackEnvelopeConfig.releaseMs);
    }

    SECTION("clause 3: a 1 s renderEngine at polyphony 4 with one note is NON-SILENT") {
        // This is the property SC-021a depends on and the reason the fixture
        // exists: one second is shorter than the shipped attack's first 5 %, so
        // without applyFastAttack this render would be indistinguishable from
        // silence and SC-021a's +/-3 dB comparison would be vacuous.
        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});
        applyFastAttack(*engine);
        engine->setPolyphony(4u);  // the shipped default, stated rather than assumed
        engine->noteOn(45u, 100u);

        const auto kTotal = static_cast<std::size_t>(kSampleRate48);  // exactly 1 s
        std::vector<float> l;
        std::vector<float> r;
        renderEngine(*engine, l, r, kTotal, 256u);

        REQUIRE(l.size() == kTotal);
        REQUIRE(r.size() == kTotal);

        for (std::size_t i = 0; i < kTotal; ++i) {
            if (!isFiniteBits(l[i]) || !isFiniteBits(r[i])) {
                FAIL("non-finite sample at index " << i);
            }
        }

        // Broadband RMS over the WHOLE second, measured with the shared
        // statistic (one block = one number).
        const std::vector<double> rmsL = blockRmsDb(std::span<const float>(l), kTotal);
        const std::vector<double> rmsR = blockRmsDb(std::span<const float>(r), kTotal);
        REQUIRE(rmsL.size() == 1u);
        REQUIRE(rmsR.size() == 1u);
        INFO("broadband RMS: L " << rmsL[0] << " dBFS, R " << rmsR[0] << " dBFS");
        REQUIRE(rmsL[0] > -60.0);
        REQUIRE(rmsR[0] > -60.0);
    }
}

// =============================================================================
// T023 - SC-006, SC-011, SC-014, SC-026, SC-030
// =============================================================================
// All five cases below are UNTAGGED. They are determinism, click-freedom,
// allocation-accounting and state-parity criteria - the cross-platform
// sentinels (FR-085) - and a [long]-only sentinel surfaces its Linux/macOS
// failure a day late, which is exactly the failure mode the project rule names.
//
// NO BIT-EXACT FLOAT GOLDEN IS STORED ANYWHERE HERE (FR-074). Every cross-run
// comparison goes through render_fingerprint.h's aggregate metrics and spaced
// checkpoints at the SHARED constants (kMetricTolerance = 2.5e-4,
// kSampleTolerance = 5.0e-4, render_fingerprint.h:55-61); the two arms that
// must be EXACT (SC-007's partitions, above) use a bit-identity check on the same
// in-process build instead, which is a run-to-run identity and not a golden.
// -----------------------------------------------------------------------------

namespace Krate::DSP::detail {

/// @brief SC-011's silence-ramp probe.
///
/// DECLARED in vorago_voice.h:160 and DEFINED HERE, in the test TU, in the
/// seraphis_engine.h:193-195 shape (B-4 / Q-C): there is no
/// KRATE_DSP_VORAGO_TEST_HOOKS define and no target_compile_definitions line
/// anywhere in the build, and `VoragoVoice` befriends the forward declaration
/// (vorago_voice.h:1240).
///
/// WHY A PROBE AND NOT AN INFERENCE. `fadeRemaining_` and `silenceRampSamples_`
/// (vorago_voice.h:2065-2066) ARE the ramp. Reading "the stolen voice went
/// quiet" off the mixed engine output infers it instead: at polyphony 4 the
/// three surviving voices and the incoming note share the bus, so a teardown
/// that cut the victim dead and let the OTHER voices mask the step would look
/// identical to one that ramped it. SC-011 says the ramp is OBSERVED, so it is
/// read at its source.
struct VoragoVoiceSilenceRampProbe {
    [[nodiscard]] static int fadeRemaining(const VoragoVoice& v) noexcept {
        return v.fadeRemaining_;
    }
    [[nodiscard]] static int silenceRampSamples(const VoragoVoice& v) noexcept {
        return v.silenceRampSamples_;
    }
};

}  // namespace Krate::DSP::detail

namespace {

using Krate::DSP::EcosystemEngine;
using Krate::DSP::SlowEventScheduler;
using Krate::DSP::VoragoVoiceConfig;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;
using Krate::DSP::TestUtils::kMetricTolerance;
using Krate::DSP::TestUtils::kSampleTolerance;
using Krate::DSP::TestUtils::RenderFingerprint;

using SilenceRampProbe = Krate::DSP::detail::VoragoVoiceSilenceRampProbe;

/// 8 kHz - the floor every sub-component publishes as kMinUsableSampleRate, and
/// FR-086's acceleration lever.
///
/// The three cases that use it (SC-014, SC-026, SC-030) measure quantities
/// defined in CONTROL STEPS and SECONDS - an allocation count, a seeded
/// trajectory, an ecosystem step count, a scheduler interval - none of which is
/// a function of the sample rate. Running the control clock at 8 kHz therefore
/// covers the same span of instrument time in one sixth of the samples, and
/// NOTHING IS APPROXIMATED. SC-006 and SC-011 deliberately do NOT use it: the
/// determinism harness is the roadmap's own 60 s at the shipped rate, and the
/// steal ramp is a SAMPLE-domain statistic whose ramp length is derived from
/// the rate.
constexpr double kSampleRate8k = 8000.0;

/// @brief One scheduled note event in a determinism render.
struct NoteEvent {
    std::size_t atSample;
    std::uint8_t note;
    std::uint8_t velocity;
    bool on;
};

/// @brief Render exactly @p total samples in @p block-sized blocks, applying
///        @p events at their stated sample offsets.
///
/// Events are applied at BLOCK boundaries, so every engine driven with the same
/// `events`, `total` and `block` sees each event at exactly the same point on
/// the absolute FR-007 grid. `events` must be sorted by `atSample`.
void renderWithSchedule(VoragoEngine& engine, std::span<const NoteEvent> events,
                        std::size_t total, std::size_t block, std::vector<float>& l,
                        std::vector<float>& r) {
    l.assign(total, 0.0f);
    r.assign(total, 0.0f);
    if (total == 0u) {
        return;
    }
    const std::size_t step = (block == 0u) ? total : block;
    std::size_t next = 0u;
    std::size_t done = 0u;
    while (done < total) {
        while (next < events.size() && events[next].atSample <= done) {
            const NoteEvent& e = events[next];
            if (e.on) {
                engine.noteOn(e.note, e.velocity);
            } else {
                engine.noteOff(e.note);
            }
            ++next;
        }
        const std::size_t n = std::min(step, total - done);
        engine.processStereoBlock(l.data() + done, r.data() + done, n);
        done += n;
    }
}

/// @brief SC-006's note sequence: four overlapping notes and two releases over
///        the 60 s window, at offsets that are whole multiples of the 512-sample
///        block so every arm lands them identically.
constexpr std::array<NoteEvent, 6> kDeterminismSchedule{{
    {.atSample = 0u, .note = 33u, .velocity = 100u, .on = true},
    {.atSample = 512u * 480u, .note = 40u, .velocity = 88u, .on = true},
    {.atSample = 512u * 960u, .note = 45u, .velocity = 120u, .on = true},
    {.atSample = 512u * 1920u, .note = 33u, .velocity = 0u, .on = false},
    {.atSample = 512u * 2880u, .note = 52u, .velocity = 70u, .on = true},
    {.atSample = 512u * 4680u, .note = 40u, .velocity = 0u, .on = false},
}};

/// @brief SC-030's per-slot life trace: the ecosystem's control-step count and
///        the number of scheduler ONSETS observed on slot 0.
struct LifeTrace {
    std::array<std::size_t, VoragoVoice::kNumEventSchedulers> events{};
    std::uint64_t controlSteps = 0u;
};

/// @brief Drive @p engine over @p total samples with @p pattern cycled for the
///        block sizes, counting slot 0's scheduler onsets at every boundary.
///
/// SlowEventScheduler publishes no event COUNTER (its whole read surface is
/// slow_event_scheduler.h:275-400), so the count is the number of Idle ->
/// non-Idle transitions of getEventPhase() (:365). Two engines driven with the
/// SAME pattern observe at the SAME sample offsets, which is what makes the two
/// counts comparable; an event whose entire attack/hold/release fitted between
/// two boundaries would be missed by BOTH arms identically, and the scheduler's
/// phases are seconds long against a <= 2047-sample (<= 256 ms at the 8 kHz
/// clock) observation spacing in any case.
[[nodiscard]] LifeTrace tracePartitioned(VoragoEngine& engine, std::size_t total,
                                         std::span<const std::size_t> pattern) {
    LifeTrace trace;
    std::array<SlowEventScheduler::Phase, VoragoVoice::kNumEventSchedulers> prev{};
    for (auto& p : prev) {
        p = SlowEventScheduler::Phase::Idle;
    }
    // One scratch block, sized to the largest element of the pattern.
    std::size_t widest = 1u;
    for (const std::size_t n : pattern) {
        widest = std::max(widest, n);
    }
    std::vector<float> l(widest, 0.0f);
    std::vector<float> r(widest, 0.0f);

    std::size_t done = 0u;
    std::size_t step = 0u;
    while (done < total) {
        const std::size_t want = pattern[step % pattern.size()];
        const std::size_t n = std::min(want, total - done);
        engine.processStereoBlock(l.data(), r.data(), n);
        done += n;
        ++step;
        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            const SlowEventScheduler::Phase now = engine.getVoice(0u).scheduler(k).getEventPhase();
            if (prev[k] == SlowEventScheduler::Phase::Idle
                && now != SlowEventScheduler::Phase::Idle) {
                ++trace.events[k];
            }
            prev[k] = now;
        }
    }
    trace.controlSteps = engine.getVoice(0u).ecosystem().getControlStepCount();
    return trace;
}

/// @brief Slot 0's per-agent ecosystem outputs, as a plain vector.
///
/// This is the quantity SC-030's second clause compares. EcosystemEngine holds
/// no audio at all - both chunk paths call processChunk(n) and nothing else
/// (vorago_voice.h:1834, :1940) - so it is exactly the part of a voice's state
/// that a rendering advance and a life-only advance MUST agree on.
[[nodiscard]] std::vector<float> agentOutputs(const VoragoVoice& voice) {
    const std::size_t count = voice.ecosystem().getAgentCount();
    std::vector<float> out(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = voice.ecosystem().getAgentOutput(i);
    }
    return out;
}

}  // namespace

// =============================================================================
// SC-006 - the determinism harness (roadmap line 473)
// =============================================================================

TEST_CASE("VoragoEngine_DeterminismHarness", "[systems][vorago]") {
    // A SHORT capture ring. The atmosphere's 20 s default is 2 x 20 x fs floats
    // per prepare() and this case builds three engines in turn; all three share
    // the value, so the comparison is untouched by it.
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    constexpr std::size_t kTotal = 2880000u;  // exactly 60 s at 48 kHz
    constexpr std::size_t kBlock = 512u;
    static_assert(kTotal % kBlock == 0u,
                  "SC-006: the render must be a whole number of blocks, or the scheduled events "
                  "land at different points in the last block of different arms");

    struct ArmFingerprint {
        RenderFingerprint l;
        RenderFingerprint r;
    };

    // The engine and its two 60 s buffers die with each call, so the peak
    // footprint is ONE arm (~23 MB of samples) rather than three.
    const auto run = [&](std::uint32_t seed) {
        auto engine = std::make_unique<VoragoEngine>();
        engine->setSeed(seed);
        engine->prepare(kSampleRate48, cfg);
        // FR-014a. Without it the whole 60 s window sits inside the shipped 20 s
        // attack and the fingerprint would be comparing two onsets.
        applyFastAttack(*engine);
        std::vector<float> l;
        std::vector<float> r;
        renderWithSchedule(*engine, std::span<const NoteEvent>(kDeterminismSchedule), kTotal,
                           kBlock, l, r);
        return ArmFingerprint{fingerprintRender(std::span<const float>(l)),
                              fingerprintRender(std::span<const float>(r))};
    };

    const ArmFingerprint a = run(0x0D0DA1u);
    const ArmFingerprint b = run(0x0D0DA1u);  // SAME seed, config and note sequence
    const ArmFingerprint c = run(0x0D0DA2u);  // the ONLY difference is the seed

    // NON-VACUITY FIRST: three silent renders agree for the wrong reason.
    INFO("arm A: rms L " << a.l.rms << " peak L " << a.l.peak << ", rms R " << a.r.rms << " peak R "
                         << a.r.peak);
    REQUIRE(a.l.peak > 0.0);
    REQUIRE(a.r.peak > 0.0);

    const auto sameL = compareFingerprints(b.l, a.l);
    const auto sameR = compareFingerprints(b.r, a.r);
    INFO("same seed: worst metric L " << sameL.worstMetricRelativeError << " R "
                                      << sameR.worstMetricRelativeError << ", worst sample L "
                                      << sameL.worstSampleError << " R " << sameR.worstSampleError
                                      << " [" << sameL.detail << " | " << sameR.detail << "]");
    REQUIRE(sameL.withinTolerance());
    REQUIRE(sameR.withinTolerance());

    // ...and a different seed is MEASURABLY different, or "within tolerance"
    // above would also be satisfied by an engine that ignored its seed entirely.
    const auto diffL = compareFingerprints(c.l, a.l);
    const auto diffR = compareFingerprints(c.r, a.r);
    const double worstDiff =
        std::max(diffL.worstMetricRelativeError, diffR.worstMetricRelativeError);
    INFO("different seed: worst metric " << worstDiff << " (bar " << (100.0 * kMetricTolerance)
                                         << ")");
    REQUIRE(worstDiff > 100.0 * kMetricTolerance);
}

// =============================================================================
// SC-011 - a steal is click-free, and the ramp is READ rather than inferred
// =============================================================================

TEST_CASE("VoragoEngine_StealRamp", "[systems][vorago]") {
    constexpr std::size_t kWindow = 4096u;
    static_assert(kWindow % VoragoEngine::kControlChunkSamples == 0u,
                  "the pre-steal and steal windows must be whole control chunks, or the steal "
                  "below stops landing mid-chunk");

    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;
    // THE SMEAR IS OFF HERE, AND ONLY HERE. getLatencySamples() is the smear's
    // fftSize while it is enabled (vorago_engine.h:947-949), so a steal at
    // sample T would not reach the output until T + 2048 and "the steal window"
    // would name a stretch of buffer the steal has not touched yet. With it off
    // the reported latency is 0 and the window holds the samples the steal
    // actually produced.
    cfg.smearEnabled = false;

    auto engine = std::make_unique<VoragoEngine>();
    engine->setSeed(0x57EA11u);
    // BEFORE prepare(). prepare() SNAPS the FR-043 sum gain to
    // sumGainForPolyphony(polyphony_) (vorago_engine.h:352-354), so setting the
    // pool size first leaves that gain CONSTANT for the whole case - otherwise
    // the 100 ms ramp a later setPolyphony() starts would be measured by the
    // click statistic as if it were the steal.
    engine->setPolyphony(4u);
    engine->prepare(kSampleRate48, cfg);
    applyFastAttack(*engine);

    // Saturate the pool: four notes, four slots, no steal yet.
    engine->noteOn(33u, 100u);
    engine->noteOn(40u, 100u);
    engine->noteOn(45u, 100u);
    engine->noteOn(52u, 100u);
    REQUIRE(engine->getActiveVoiceCount() == 4u);
    REQUIRE(engine->getLastStolenVoiceIndex() == -1);

    std::vector<float> scratchL;
    std::vector<float> scratchR;
    // 48040 samples: one second of settling PLUS 40, so the noteOn below lands
    // at grid phase 40 of 64 - SC-011 says the steal is MID-CHUNK.
    renderEngine(*engine, scratchL, scratchR, 48040u, 512u);

    std::vector<float> preL;
    std::vector<float> preR;
    renderEngine(*engine, preL, preR, kWindow, 512u);
    const float preDelta = std::max(maxAbsDelta(std::span<const float>(preL)),
                                    maxAbsDelta(std::span<const float>(preR)));
    // NON-VACUITY: a silent pre-steal window makes "<= 1.5 x" unfalsifiable.
    INFO("pre-steal max |delta| = " << preDelta
                                    << ", pre-steal peak = " << peakOf(std::span<const float>(preL)));
    REQUIRE(preDelta > 0.0f);
    REQUIRE(peakOf(std::span<const float>(preL)) > 0.0f);

    // THE STEAL. The pool is saturated, so freeChosenVictimSlot() runs
    // (vorago_engine.h:1273) and the victim is torn down before the allocator
    // hands its slot to the incoming note.
    engine->noteOn(57u, 100u);
    const int victim = engine->getLastStolenVoiceIndex();
    REQUIRE(victim >= 0);
    REQUIRE(std::cmp_less(victim, VoragoEngine::kMaxVoices));
    const VoragoVoice& stolen = engine->getVoice(static_cast<std::size_t>(victim));

    // --- the ramp, READ through the probe (B-4) ------------------------------
    // vorago_voice.h:680-682 derives the length as
    // max(1, lround(0.001f * kSilenceRampMs * sampleRate)); it is reproduced
    // here rather than hard-coded as 48, so a rate change moves both sides.
    const int expectedRamp =
        std::max(1, static_cast<int>(std::lround(0.001f * VoragoVoice::kSilenceRampMs
                                                 * static_cast<float>(kSampleRate48))));
    const int rampSamples = SilenceRampProbe::silenceRampSamples(stolen);
    const int armed = SilenceRampProbe::fadeRemaining(stolen);
    INFO("victim slot " << victim << ": silenceRampSamples_ = " << rampSamples << " (expected "
                        << expectedRamp << "), fadeRemaining_ = " << armed);
    REQUIRE(rampSamples == expectedRamp);
    // "silent within kSilenceRampMs" is only a bound if the ramp fits inside one
    // control chunk - vorago_voice.h:291-293 says it must.
    REQUIRE(std::cmp_less(rampSamples, VoragoEngine::kControlChunkSamples));
    // silence() armed the fade from the voice's last emitted sample pair.
    REQUIRE(armed == rampSamples);

    // ONE control chunk of render consumes it: the voice renders WHOLE chunks
    // (D1) and the fade tail is applied once per rendered sample
    // (vorago_voice.h:1906-1914).
    std::vector<float> postL;
    std::vector<float> postR;
    renderEngine(*engine, postL, postR, VoragoEngine::kControlChunkSamples, 0u);
    INFO("fadeRemaining_ after one chunk = " << SilenceRampProbe::fadeRemaining(stolen));
    REQUIRE(SilenceRampProbe::fadeRemaining(stolen) == 0);

    std::vector<float> tailL;
    std::vector<float> tailR;
    renderEngine(*engine, tailL, tailR, kWindow - VoragoEngine::kControlChunkSamples, 512u);

    // The steal window, with the LAST PRE-STEAL SAMPLE prepended so the seam
    // itself is measured - a click at the join is exactly the artefact SC-011
    // exists for, and three separately-measured buffers would step over it.
    const auto join = [](const std::vector<float>& before, const std::vector<float>& first,
                         const std::vector<float>& second) {
        std::vector<float> out;
        out.reserve(1u + first.size() + second.size());
        out.push_back(before.back());
        out.insert(out.end(), first.begin(), first.end());
        out.insert(out.end(), second.begin(), second.end());
        return out;
    };
    const std::vector<float> windowL = join(preL, postL, tailL);
    const std::vector<float> windowR = join(preR, postR, tailR);

    for (std::size_t i = 0; i < windowL.size(); ++i) {
        if (!isFiniteBits(windowL[i]) || !isFiniteBits(windowR[i])) {
            FAIL("non-finite sample in the steal window at index " << i);
        }
    }

    const float stealDelta = std::max(maxAbsDelta(std::span<const float>(windowL)),
                                      maxAbsDelta(std::span<const float>(windowR)));
    INFO("steal-window max |delta| = " << stealDelta << ", bound = " << (1.5f * preDelta));
    REQUIRE(stealDelta <= 1.5f * preDelta);
}

// =============================================================================
// SC-014 - zero allocation after prepare (FR-070), on the per-push arm
// =============================================================================
// This is the 10-minute-equivalent EDGE-COVERAGE arm. FR-070's 8 h
// getAllocatedBytes()-invariance clause is SC-004b's, unaccelerated, in the
// [long] lane (spec A-3): neither narrows the other and the build may not merge
// them - 10 minutes does not prove 8 h, and an 8 h held note does not walk these
// edges.
// -----------------------------------------------------------------------------

TEST_CASE("VoragoEngine_NoAllocationAfterPrepare", "[systems][vorago]") {
    // --- clause 0: the counter is LIVE in this image -------------------------
    // The global operator new/delete replacements live in
    // unit/systems/selectable_oscillator_test.cpp:388, not here. If they were
    // ever dropped from the image, this whole criterion would silently become
    // "0 == 0", so a DELIBERATE heap allocation is counted first.
    std::size_t deliberate = 0u;
    float sink = 0.0f;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        std::vector<float> victim(1024u, 1.0f);
        sink = victim[512u];  // read it, so nothing may elide the allocation
        // Read from the singleton while the scope is still OPEN: AllocationScope
        // latches its own count in its DESTRUCTOR
        // (tests/test_helpers/allocation_detector.h:118-120).
        deliberate = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    INFO("clause 0: a deliberate 1024-float vector counted " << deliberate << " allocation(s)");
    REQUIRE(sink == 1.0f);
    REQUIRE(deliberate > 0u);

    // --- the engine ----------------------------------------------------------
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    auto engine = std::make_unique<VoragoEngine>();
    engine->setSeed(0xA110C8u);
    engine->prepare(kSampleRate8k, cfg);
    applyFastAttack(*engine);

    const std::size_t bytesAfterPrepare = engine->getAllocatedBytes();
    REQUIRE(bytesAfterPrepare > 0u);

    // TEN MINUTES OF CONTROL CLOCK at the 8 kHz floor (FR-086). Allocation
    // accounting is an event-and-accounting property, so it survives the clock
    // scaling: the same 600 s of instrument time, the same 9 375 blocks, the
    // same wake / sleep edges, bloom spawns and retires - one sixth of the
    // samples.
    constexpr std::size_t kBlock = 512u;
    constexpr std::size_t kTotal = 4800000u;  // 600 s x 8 kHz
    constexpr std::size_t kEdgeEveryBlocks = 64u;

    // EVERYTHING the loop touches is built BEFORE the scope opens, and nothing
    // inside it is a Catch2 macro: REQUIRE and INFO allocate, and an assertion
    // inside the scope would be counted as the engine's.
    std::vector<float> l(kBlock, 0.0f);
    std::vector<float> r(kBlock, 0.0f);
    VoragoMacroMatrix matrix;
    constexpr std::array<std::uint8_t, 6> kNotes{28u, 33u, 38u, 43u, 48u, 53u};

    std::size_t allocations = 0u;
    std::size_t edges = 0u;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        std::size_t done = 0u;
        std::size_t blockIndex = 0u;
        while (done < kTotal) {
            const std::size_t n = std::min(kBlock, kTotal - done);
            engine->processStereoBlock(l.data(), r.data(), n);
            // The far side of the AR-1 seam too, so the saturators and the
            // limiter are inside the scope.
            engine->processOutputStage(l.data(), r.data(), n);
            done += n;

            if ((blockIndex % kEdgeEveryBlocks) == 0u) {
                const std::size_t k = blockIndex / kEdgeEveryBlocks;
                // FOUR notes held against a four- (and, half the time, one-)
                // slot pool, so the pool is saturated and every note-on from
                // here on is a STEAL.
                engine->noteOn(kNotes[k % kNotes.size()], 100u);
                engine->noteOff(kNotes[(k + 2u) % kNotes.size()]);
                // Both polyphony extremes: the shrink's orphan-tail path and
                // the growth's spare-slot path.
                engine->setPolyphony(((k / 8u) % 2u == 0u) ? std::size_t{4} : std::size_t{1});
                // EVERY macro at BOTH extremes, through the shipped matrix, so
                // the value that reaches each setter is the one the instrument
                // would really install.
                const auto macro = static_cast<VoragoMacro>(
                    static_cast<std::uint8_t>(k % VoragoMacroMatrix::kNumMacros));
                matrix.setMacro(macro,
                                ((k / VoragoMacroMatrix::kNumMacros) % 2u == 0u) ? 0.0f : 1.0f);
                matrix.apply(*engine);
                ++edges;
            }
            ++blockIndex;
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    INFO("10-minute-equivalent render at " << kSampleRate8k << " Hz: " << edges
                                           << " event edges, " << allocations << " allocation(s)");
    // Every macro, both extremes - otherwise "every macro extreme" is a claim
    // the loop never made good on.
    REQUIRE(edges >= 2u * VoragoMacroMatrix::kNumMacros);
    REQUIRE(allocations == 0u);
    INFO("getAllocatedBytes: after prepare " << bytesAfterPrepare << ", after the render "
                                             << engine->getAllocatedBytes());
    REQUIRE(engine->getAllocatedBytes() == bytesAfterPrepare);
    // ...and the render did not divert itself down the containment path, which
    // would have made the zero above a statement about a different workload.
    REQUIRE(engine->getNonFiniteRecoveryCount() == 0u);
}

// =============================================================================
// SC-026 - a slot's seed is per slot, and is never advanced per note (FR-048)
// =============================================================================
// WHY EVERY ARM REWINDS WITH VoragoEngine::reset() RATHER THAN SIMPLY REPLAYING.
// FR-046 and SC-030 require the identity layer to KEEP RUNNING while a slot is
// idle: a retired slot's ecosystem, schedulers, breath and tide are deliberately
// not rewound by retirement, and noteOn() does not rewind them either
// (vorago_voice.h:855-886 retunes and gates; it clears no run state). That is
// the whole point of advanceLifeOnly(), and SC-030 below asserts it directly.
// So "play, retire, play again" cannot reproduce its first trajectory on its
// own, and an assertion that it does would contradict SC-030 rather than test
// FR-048.
//
// reset() is the rewind that makes the two renders comparable, and it is also
// what makes this criterion READ FR-048: it clears run state on every slot and
// the global chain, but it DOES NOT re-derive the slot seeds - only prepare()
// and setSeed() do. The trajectory it restores is therefore a function of the
// seed the note cycle LEFT BEHIND, so a noteOn / noteOff / retire / steal cycle
// that consumed, advanced or perturbed a slot seed replays a DIFFERENT
// trajectory here. Re-preparing instead would have overwritten exactly that
// evidence.
//
// AND WHY EVERY ARM PRIMES THE PITCH FIRST. SC-026's premise is "same engine
// seed AND CONFIGURATION throughout", and noteOn() is a configuration write:
// it pushes the note frequency into the cloud, both bodies and the resonance
// network, and ContinuousBody holds it in a SMOOTHER (continuous_body.h:1442),
// which its own reset() snaps to the stored value. A voice straight out of
// prepare() has never been told a pitch, so its FIRST note glides up from
// kDefaultNoteHz, while the replay's note starts already on pitch. Measured on
// the shipped code, that glide alone is worth a worstMetricRelativeError of
// about 1.6e-2 - sixty-five times the tolerance - and it is not a seed
// property at all. primePitch() therefore leaves the voice at exactly the pitch
// configuration the REPLAY will start from, before the first playing is
// captured: (a) at kNote, (b) at kOtherNote, because that is what the steal
// leaves behind. With the premise honoured the two arms measure the seed and
// nothing else; the residue is 4.9e-5 and 2.1e-5 against a 2.5e-4 bar.
// -----------------------------------------------------------------------------

TEST_CASE("VoragoEngine_SlotSeedReproducibility", "[systems][vorago]") {
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    constexpr std::size_t kWindow = 16000u;  // 2 s of the 8 kHz control clock
    constexpr std::uint8_t kNote = 45u;
    constexpr std::uint8_t kOtherNote = 52u;
    constexpr std::uint32_t kSeed = 0x51075Eu;

    // Polyphony is set BEFORE prepare() so the FR-043 sum gain is SNAPPED to
    // sumGainForPolyphony(1) and is constant in every window compared below.
    const auto buildMono = [&]() {
        auto engine = std::make_unique<VoragoEngine>();
        engine->setSeed(kSeed);
        engine->setPolyphony(1u);
        engine->prepare(kSampleRate8k, cfg);
        applyFastAttack(*engine);
        return engine;
    };

    // Put the voice's pitch CONFIGURATION where the replay will find it, and
    // rewind everything the two notes touched, so the arm below starts from the
    // same configuration it ends on. No audio is rendered here, so nothing is
    // consumed: a noteOn that is never rendered advances no clock.
    const auto primePitch = [](VoragoEngine& engine, std::uint8_t note) {
        engine.noteOn(note, 100u);
        engine.noteOff(note);
        engine.reset();
    };

    SECTION("(a) the slot replays its trajectory after a RETIRE") {
        auto engine = buildMono();
        primePitch(*engine, kNote);
        std::vector<float> l;
        std::vector<float> r;

        engine->noteOn(kNote, 100u);
        renderEngine(*engine, l, r, kWindow, 512u);
        const RenderFingerprint first = fingerprintRender(std::span<const float>(l));
        INFO("first playing: rms " << first.rms << ", peak " << first.peak);
        REQUIRE(first.peak > 0.0);

        // LET IT RETIRE, for real. FR-013's counter is ten SECONDS of quiescence
        // below -90 dBFS (vorago_voice.h:285-297) and retirement itself lands in
        // the post-render control step (vorago_engine.h:1228-1232).
        engine->noteOff(kNote);
        bool retired = false;
        int secondsWaited = 0;
        for (; secondsWaited < 180 && !retired; ++secondsWaited) {
            renderEngine(*engine, l, r, static_cast<std::size_t>(kSampleRate8k), 512u);
            retired = engine->getVoiceState(0u) == VoiceState::Idle;
        }
        INFO("slot 0 retired after " << secondsWaited << " s of tail");
        REQUIRE(retired);

        (*engine).reset();
        engine->noteOn(kNote, 100u);
        renderEngine(*engine, l, r, kWindow, 512u);
        const RenderFingerprint again = fingerprintRender(std::span<const float>(l));

        const auto cmp = compareFingerprints(again, first);
        INFO("worst metric " << cmp.worstMetricRelativeError << ", worst sample "
                             << cmp.worstSampleError << " [" << cmp.detail << "]");
        REQUIRE(cmp.withinTolerance());
    }

    SECTION("(b) a STEAL does not advance the slot's seed") {
        auto engine = buildMono();
        // kOtherNote, not kNote: the steal below is what leaves the voice's
        // pitch configuration behind, and it leaves it on the STEALING note.
        primePitch(*engine, kOtherNote);
        std::vector<float> l;
        std::vector<float> r;

        engine->noteOn(kNote, 100u);
        renderEngine(*engine, l, r, kWindow, 512u);
        const RenderFingerprint first = fingerprintRender(std::span<const float>(l));
        REQUIRE(first.peak > 0.0);

        // At polyphony 1 one sounding note saturates the pool, so this IS a
        // steal of slot 0 and not an allocation onto a free slot.
        engine->noteOn(kOtherNote, 100u);
        REQUIRE(engine->getLastStolenVoiceIndex() == 0);
        renderEngine(*engine, l, r, kWindow, 512u);

        (*engine).reset();
        engine->noteOn(kNote, 100u);
        renderEngine(*engine, l, r, kWindow, 512u);
        const RenderFingerprint again = fingerprintRender(std::span<const float>(l));

        const auto cmp = compareFingerprints(again, first);
        INFO("worst metric " << cmp.worstMetricRelativeError << ", worst sample "
                             << cmp.worstSampleError << " [" << cmp.detail << "]");
        REQUIRE(cmp.withinTolerance());
    }

    SECTION("(c) the same note on a DIFFERENT slot is a different trajectory") {
        // Both engines sound EXACTLY the same two notes at exactly the same
        // moment; the only difference is which slot got which note, because the
        // allocator's idle search hands out the lowest idle slot first
        // (voice_allocator.h:568-582 - every timestamp is equal on a fresh
        // pool). With identical slot seeds the two sums would be identical,
        // because addition is commutative, so any difference measured here is
        // FR-045's disjoint salts and nothing else.
        const auto buildPair = [&](std::uint8_t firstNote, std::uint8_t secondNote) {
            auto engine = std::make_unique<VoragoEngine>();
            engine->setSeed(kSeed);
            engine->setPolyphony(2u);
            engine->prepare(kSampleRate8k, cfg);
            applyFastAttack(*engine);
            engine->noteOn(firstNote, 100u);
            engine->noteOn(secondNote, 100u);
            return engine;
        };

        auto straight = buildPair(kNote, kOtherNote);
        auto swapped = buildPair(kOtherNote, kNote);
        REQUIRE(straight->getActiveVoiceCount() == 2u);
        REQUIRE(swapped->getActiveVoiceCount() == 2u);

        std::vector<float> l;
        std::vector<float> r;
        renderEngine(*straight, l, r, kWindow, 512u);
        const RenderFingerprint a = fingerprintRender(std::span<const float>(l));
        renderEngine(*swapped, l, r, kWindow, 512u);
        const RenderFingerprint b = fingerprintRender(std::span<const float>(l));

        REQUIRE(a.peak > 0.0);
        REQUIRE(b.peak > 0.0);

        const auto cmp = compareFingerprints(b, a);
        INFO("slot-swapped worst metric " << cmp.worstMetricRelativeError << " (bar "
                                          << (100.0 * kMetricTolerance) << ")");
        REQUIRE(cmp.worstMetricRelativeError > 100.0 * kMetricTolerance);
    }
}

// =============================================================================
// SC-030 - an idle voice's life state matches a rendering one (FR-009, FR-046)
// =============================================================================

TEST_CASE("VoragoEngine_AdvanceLifeOnlyParity", "[systems][vorago]") {
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;
    // The smear has no bearing on the identity layer and is the most expensive
    // stage in a render this long; BOTH arms drop it identically.
    cfg.smearEnabled = false;

    // SC-007's pathological split, reused verbatim: 36 and 28 straddle exactly
    // one chunk boundary, 1 is a single sample mid-chunk, and 2047 / 1984 never
    // re-align. "The same sample count across a PARTITIONED schedule" is
    // precisely this.
    constexpr std::array<std::size_t, 5> kPathological{36u, 28u, 1u, 2047u, 1984u};
    // 469 cycles of the 4096-sample pattern = 1 921 024 samples, about 240 s of
    // the 8 kHz control clock. The fast scheduler's interval range is 20-90 s
    // (vorago_voice.h:276-279), so the window holds several events rather than
    // none - which is what keeps the event-count comparison from being 0 == 0.
    constexpr std::size_t kTotal = 469u * 4096u;
    // EcosystemEngine::getControlStepCount() counts SIMULATION steps, not
    // control chunks: processChunk() advances a 64-sample sample phase and fires
    // a step once every stepIntervalChunks of them, so a step is
    // kControlChunkSamples * stepIntervalChunks samples long
    // (ecosystem_engine.h:417-419, :999-1001). The voice configures that interval
    // from VoragoVoiceConfig::ecosystemStepChunks, which ships at the component
    // floor of 8 (vorago_voice.h:205, clamped at :495), so one step is 512
    // samples here and NOT 64. Dividing by the chunk alone would demand eight
    // times the steps that any conforming implementation can produce.
    constexpr std::size_t kEcosystemStepChunks = VoragoVoiceConfig{}.ecosystemStepChunks;
    static_assert(kEcosystemStepChunks >= EcosystemEngine::kMinStepIntervalChunks
                      && kEcosystemStepChunks <= EcosystemEngine::kMaxStepIntervalChunks,
                  "SC-030: the shipped step interval must survive the voice's own clamp, or the "
                  "expected count below is derived from a number the component never used");
    constexpr std::size_t kEcosystemStepSamples =
        VoragoEngine::kControlChunkSamples * kEcosystemStepChunks;
    static_assert(kTotal % kEcosystemStepSamples == 0u,
                  "SC-030: the trace must end on an ecosystem STEP boundary, or the two arms' step "
                  "counts differ for a reason that is not the property under test");

    const auto build = [&](bool withNote) {
        auto engine = std::make_unique<VoragoEngine>();
        engine->setSeed(0x11FEA1u);
        engine->prepare(kSampleRate8k, cfg);
        applyFastAttack(*engine);
        if (withNote) {
            engine->noteOn(45u, 100u);
        }
        return engine;
    };

    // Same engine seed and same configuration, so slot 0 of each carries the
    // SAME derived slot seed, deriveStreamSeed(seed, kVoiceSaltBase + 0).
    auto rendering = build(true);
    auto idle = build(false);
    REQUIRE(rendering->getVoice(0u).getSeed() == idle->getVoice(0u).getSeed());
    // Non-vacuity: the two arms really are on different code paths.
    REQUIRE(rendering->getRenderingVoiceCount() == 1u);
    REQUIRE(idle->getRenderingVoiceCount() == 0u);

    const LifeTrace rendered =
        tracePartitioned(*rendering, kTotal, std::span<const std::size_t>(kPathological));
    const LifeTrace advanced =
        tracePartitioned(*idle, kTotal, std::span<const std::size_t>(kPathological));

    INFO("control steps: rendering " << rendered.controlSteps << ", life-only "
                                     << advanced.controlSteps << ", expected "
                                     << (kTotal / kEcosystemStepSamples));
    REQUIRE(rendered.controlSteps == kTotal / kEcosystemStepSamples);
    REQUIRE(advanced.controlSteps == rendered.controlSteps);

    std::size_t totalEvents = 0u;
    for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
        INFO("scheduler " << k << ": rendering " << rendered.events[k] << " onsets, life-only "
                          << advanced.events[k]);
        REQUIRE(rendered.events[k] == advanced.events[k]);
        totalEvents += rendered.events[k];
    }
    // NON-VACUITY: 0 == 0 would pass the loop above on an engine that never ran
    // a scheduler at all.
    INFO("total scheduler onsets over the window: " << totalEvents);
    REQUIRE(totalEvents >= 1u);

    // --- the idle slot must not start from a COLD ecosystem ------------------
    std::vector<float> l;
    std::vector<float> r;
    idle->noteOn(45u, 100u);
    renderEngine(*idle, l, r, VoragoEngine::kControlChunkSamples, 0u);
    renderEngine(*rendering, l, r, VoragoEngine::kControlChunkSamples, 0u);

    const std::vector<float> wokenAgents = agentOutputs(idle->getVoice(0u));
    const std::vector<float> liveAgents = agentOutputs(rendering->getVoice(0u));
    REQUIRE(!liveAgents.empty());
    REQUIRE(wokenAgents.size() == liveAgents.size());

    const RenderFingerprint wokenFp = fingerprintRender(std::span<const float>(wokenAgents));
    const RenderFingerprint liveFp = fingerprintRender(std::span<const float>(liveAgents));
    INFO("agent outputs: live peak " << liveFp.peak << ", woken peak " << wokenFp.peak);
    REQUIRE(liveFp.peak > 0.0);  // non-vacuity: all-zero agent outputs prove nothing

    const auto cmp = compareFingerprints(wokenFp, liveFp);
    INFO("first-chunk agent outputs: worst metric " << cmp.worstMetricRelativeError
                                                    << ", worst sample " << cmp.worstSampleError
                                                    << " [" << cmp.detail << "]");
    REQUIRE(cmp.withinTolerance());

    // ...and a genuinely COLD slot does NOT match, so the clause above is a
    // measurement rather than a tautology.
    auto cold = build(true);
    renderEngine(*cold, l, r, VoragoEngine::kControlChunkSamples, 0u);
    const std::vector<float> coldAgents = agentOutputs(cold->getVoice(0u));
    REQUIRE(coldAgents.size() == liveAgents.size());
    float worstCold = 0.0f;
    for (std::size_t i = 0; i < coldAgents.size(); ++i) {
        worstCold = std::max(worstCold, std::fabs(coldAgents[i] - liveAgents[i]));
    }
    INFO("cold-vs-live worst agent-output difference = " << worstCold << " (bar "
                                                         << kSampleTolerance << ")");
    REQUIRE(worstCold > kSampleTolerance);
}


// =============================================================================
// T024 - the per-push sentinels and the engine setter contract
// =============================================================================
// SC-004a, SC-013a, SC-021a, SC-027 and SC-028 (engine half). ALL FIVE CASES ARE
// UNTAGGED, deliberately: they are the cross-platform sentinels - boundedness,
// finiteness, a sample-rate extreme, a configuration read-back and a setter
// contract - and a [long]-only boundedness case surfaces its Linux/macOS failure
// a day late, which is exactly the failure mode the project rule names. THE
// BUILD MAY NOT RE-MERGE ANY OF THEM WITH ITS [long] PARTNER in
// unit/systems/vorago_engine_longrun_test.cpp: SC-004a is not SC-004b, SC-013a
// is not SC-013b and SC-021a is not SC-021b.
//
// THE SENTINEL HALVES DUPLICATE SOME OF THE [long] TU's RENDER SHAPE, and that
// is a decision rather than an oversight: moving the fuzz draw or the rate
// render into tests/test_helpers/vorago_fixtures.h would make one header the
// shared owner of both lanes, so a single edit could weaken the per-push gate
// and the nightly gate together. The two lanes are kept independent on purpose;
// where a constant must agree across them (the fuzz seed base, the 32/968
// split) it is restated here WITH the longrun line it must match.
// -----------------------------------------------------------------------------

namespace {

using Krate::DSP::MultiStageEnvelope;
using Krate::DSP::VoragoMacroValues;
using Krate::DSP::Xorshift32;

/// @brief A float built from @p bits through a `volatile`, so no constant
///        folding can reach it.
///
/// The ONLY construction path for a non-finite float in this block.
/// std::numeric_limits<float>::infinity() is deliberately NOT used: under
/// -ffast-math the compiler is licensed to fold it away before it reaches the
/// setter, which would silently turn SC-028's first clause into a test of a
/// finite number.
[[nodiscard]] float makeNonFiniteFloat(std::uint32_t bits) noexcept {
    volatile std::uint32_t pattern = bits;
    const std::uint32_t copy = pattern;
    float out = 0.0f;
    std::memcpy(&out, &copy, sizeof(out));
    return out;
}

constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// @brief dBFS of a mean-square value, floored at -240 dBFS.
///
/// The mean square is accumulated, never the dB: a mean square averages
/// arithmetically over a window, a dB value does not. The floor mirrors the
/// shared blockRmsDb() helper's (1e-12 in amplitude = 1e-24 in mean square).
[[nodiscard]] double dbFromMeanSquare(double meanSquare) noexcept {
    return 10.0 * std::log10(std::max(meanSquare, 1e-24));
}

/// The render partition every sentinel here uses. 512 divides 48 000 x 60
/// exactly, so SC-004a's window is a whole number of blocks.
constexpr std::size_t kSentinelBlock = 512u;

/// "Non-silent" needs a number to be an assertion. -80 dBFS is two orders below
/// SC-004b's own -60 dBFS floor, so it fires only on a render that is silent or
/// all but silent, never on one that is merely quiet. Restated from
/// vorago_engine_longrun_test.cpp's kNonSilentFloorDb so the two lanes read the
/// same signal the same way.
constexpr double kSentinelNonSilentFloorDb = -80.0;

/// @brief What one sentinel render observed.
struct SentinelRender {
    bool allFinite = true;
    float peakAbs = 0.0f;
    double rmsDb = -300.0;
    std::uint32_t recoveries = 0u;
};

/// @brief Render @p samples frames through BOTH halves of the AR-1 seam.
///
/// processStereoBlock() then processOutputStage(), because `|out| <= 1.0` is a
/// statement about the engine's FINAL output - the TruePeakLimiter at
/// vorago_engine.h:933-946 is the stage that makes it true, and a sentinel that
/// measured the pre-limiter bus would be asserting something the product never
/// ships. The caller's Layer-4 cavern is deliberately absent: it is owned by the
/// caller (AR-1), and the composed chain is
/// unit/effects/vorago_composed_chain_test.cpp.
[[nodiscard]] SentinelRender renderThroughOutputStage(VoragoEngine& engine, std::size_t samples,
                                                      std::size_t block) {
    SentinelRender out;
    std::vector<float> l(block, 0.0f);
    std::vector<float> r(block, 0.0f);
    double sumSquares = 0.0;
    std::size_t counted = 0u;

    for (std::size_t done = 0; done < samples; done += block) {
        const std::size_t n = std::min(block, samples - done);
        std::fill(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
        std::fill(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);

        engine.processStereoBlock(l.data(), r.data(), n);
        engine.processOutputStage(l.data(), r.data(), n);

        for (std::size_t i = 0; i < n; ++i) {
            const float a = l[i];
            const float b = r[i];
            if (!isFiniteBits(a) || !isFiniteBits(b)) {
                out.allFinite = false;
                continue;  // a NaN may not enter the accumulators
            }
            out.peakAbs = std::max(out.peakAbs, std::max(std::fabs(a), std::fabs(b)));
            sumSquares += (static_cast<double>(a) * static_cast<double>(a))
                          + (static_cast<double>(b) * static_cast<double>(b));
            counted += 2u;
        }
    }

    out.recoveries = engine.getNonFiniteRecoveryCount();
    out.rmsDb = (counted > 0u) ? dbFromMeanSquare(sumSquares / static_cast<double>(counted))
                               : -300.0;
    return out;
}

}  // namespace

// =============================================================================
// SC-004a - the soak SENTINEL (60 s), not the 8 h soak
// =============================================================================
// FR-085's per-push half of SC-004. The four assertions are exactly the four the
// criterion names: every sample finite, |out| <= 1.0 AFTER processOutputStage,
// getNonFiniteRecoveryCount() == 0, and getAllocatedBytes() identical to the
// value read immediately after prepare().
//
// kFastAttackEnvelopeConfig IS used here and FR-014a authorises it: this is a
// 60 s window and the shipped 20 s attack followed by 30 / 45 / 60 s body stages
// would leave the render inside its own onset for the whole measurement, so the
// boundedness claim would be a claim about a fade-in. FR-014a's ban names
// SC-004b - the criterion whose SUBJECT is the slow envelope - and nothing here
// touches that render. The non-vacuity clause below is what makes the
// substitution load-bearing rather than cosmetic: a sentinel that passed over
// silence would prove nothing at all.
// -----------------------------------------------------------------------------

TEST_CASE("VoragoEngine_SoakSentinel", "[systems][vorago]") {
    constexpr double kSoakSentinelSeconds = 60.0;
    const auto samples = static_cast<std::size_t>(kSoakSentinelSeconds * kSampleRate48);

    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(kSampleRate48, cfg);
    engine->setSeed(0x50AC0000u);
    engine->setPolyphony(VoragoEngine::kMaxVoices);  // FULL polyphony
    applyFastAttack(*engine);

    // Read AFTER every configuration write and BEFORE the render, so the
    // comparison below says "the RENDER allocated nothing" rather than "the
    // setters allocated nothing".
    const std::size_t bytesAfterPrepare = engine->getAllocatedBytes();
    REQUIRE(bytesAfterPrepare > 0u);

    engine->noteOn(33u, 100u);  // ONE held note, never released

    const SentinelRender render = renderThroughOutputStage(*engine, samples, kSentinelBlock);

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "SC-004a: " << kSoakSentinelSeconds << " s at " << kSampleRate48 << " Hz, polyphony "
           << engine->getPolyphony() << ", one held note; peak |out| = " << render.peakAbs
           << ", RMS = " << render.rmsDb << " dBFS, recoveries = " << render.recoveries
           << ", getAllocatedBytes " << bytesAfterPrepare << " -> " << engine->getAllocatedBytes();
        WARN(os.str());
    }

    CAPTURE(render.peakAbs);
    CAPTURE(render.rmsDb);
    CAPTURE(render.recoveries);

    REQUIRE(render.allFinite);         // every sample finite (bit pattern)
    REQUIRE(render.peakAbs <= 1.0f);   // |out| <= 1.0 after processOutputStage
    REQUIRE(render.recoveries == 0u);  // the containment path never ran
    REQUIRE(engine->getAllocatedBytes() == bytesAfterPrepare);

    // Non-vacuity. Without this the three clauses above are satisfied by a render
    // of pure silence, which is the one failure mode a boundedness sentinel
    // cannot otherwise see.
    REQUIRE(render.rmsDb > kSentinelNonSilentFloorDb);
}

// =============================================================================
// SC-013a - the configuration-fuzz SENTINEL (32 configurations x 2 s)
// =============================================================================
// FR-085's per-push half of SC-013. UNACCELERATED, per the criterion: the [long]
// remainder (SC-013b, vorago_engine_longrun_test.cpp:906) carries the FR-086
// acceleration and the other 968 configurations, and the two halves PARTITION
// one seeded sequence - configurations [0, 32) here, [32, 1000) there - so no
// configuration is covered twice and none is skipped. The seed base and the
// split are restated from vorago_engine_longrun_test.cpp:452 and :459 and must
// stay identical to them.
//
// The renders are near their onset at 2 s unaccelerated, and that is correct for
// this criterion: FR-073 is a BOUNDEDNESS requirement, not a steady-state one,
// and an unaccelerated render is the only way the per-push lane exercises the
// shipped envelope clocks at all.
// -----------------------------------------------------------------------------

namespace {

/// vorago_engine_longrun_test.cpp:459 - 'VCFG'. The two lanes MUST agree.
constexpr std::uint32_t kSentinelFuzzSeedBase = 0x56434647u;
/// vorago_engine_longrun_test.cpp:452.
constexpr std::size_t kSentinelFuzzConfigs = 32u;
constexpr double kSentinelFuzzSeconds = 2.0;

[[nodiscard]] float drawSentinelRange(Xorshift32& rng, float lo, float hi) noexcept {
    return lo + ((hi - lo) * rng.nextUnipolar());
}

[[nodiscard]] std::size_t drawSentinelIndex(Xorshift32& rng, std::size_t lo,
                                            std::size_t hi) noexcept {
    return lo + static_cast<std::size_t>(rng.next() % static_cast<std::uint32_t>((hi - lo) + 1u));
}

[[nodiscard]] bool drawSentinelFlag(Xorshift32& rng) noexcept { return (rng.next() & 1u) != 0u; }

/// @brief A macro value biased towards its two ENDPOINTS.
///
/// A uniform draw over [0, 1] almost never reaches an endpoint, and the
/// endpoints are where a macro row's clamp either holds or does not.
[[nodiscard]] float drawSentinelMacro(Xorshift32& rng) noexcept {
    const std::uint32_t k = rng.next() % 3u;
    if (k == 0u) {
        return 0.0f;
    }
    if (k == 1u) {
        return 1.0f;
    }
    return rng.nextUnipolar();
}

/// @brief The prepare-time half of one fuzzed configuration.
///
/// Every field is CLAMPED by its owner rather than rejected
/// (vorago_engine.h:105-133), so a draw outside a component's range is itself
/// part of what is under test. The two FFT sizes are left at their defaults: a
/// non-power-of-two would exercise the FFT's own validation rather than the
/// engine's boundedness, and FR-073 is what this case gates.
[[nodiscard]] VoragoEngineConfig drawSentinelConfig(Xorshift32& rng) noexcept {
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = drawSentinelRange(rng, 1.0f, 30.0f);
    cfg.atmosBlurEnabled = drawSentinelFlag(rng);
    cfg.smearEnabled = drawSentinelFlag(rng);
    cfg.voice.numNoiseSources = drawSentinelIndex(rng, 1u, 4u);
    cfg.voice.numResonancePeaks = drawSentinelIndex(rng, 1u, 12u);
    cfg.voice.numEcologyLoops = drawSentinelIndex(rng, 1u, 6u);
    cfg.voice.ecosystemAgents = drawSentinelIndex(rng, 4u, 64u);
    cfg.voice.ecosystemCells = drawSentinelIndex(rng, 8u, 64u);
    cfg.voice.bloomChildSlots = drawSentinelIndex(rng, 0u, 6u);
    cfg.voice.maxCombDelayMs = drawSentinelRange(rng, 5.0f, 200.0f);
    return cfg;
}

/// @brief The run-time half: every macro, then every exposed engine setter.
///
/// ORDER IS A DECISION. The macro matrix writes all eight Engine-owned targets
/// (vorago_macro_matrix.h:150-158), so a direct setter called BEFORE apply()
/// would be overwritten and the drawn value would never render. apply()
/// therefore runs first and the direct setters follow, which leaves the drawn
/// setter values live on the eight engine targets while the macro state still
/// drives all twenty-four Voice-owned ones.
///
/// NOTHING HERE TOUCHES THE ENVELOPE CLOCKS. SC-013a is the unaccelerated half;
/// the FR-086 acceleration lever belongs to SC-013b alone.
void configureSentinelEngine(VoragoEngine& engine, VoragoMacroMatrix& matrix,
                             Xorshift32& rng) noexcept {
    engine.setSeed(
        deriveStreamSeed(kSentinelFuzzSeedBase, static_cast<std::size_t>(rng.next() % 4096u)));

    VoragoMacroValues macros{};
    macros.darkness = drawSentinelMacro(rng);
    macros.age = drawSentinelMacro(rng);
    macros.density = drawSentinelMacro(rng);
    macros.movement = drawSentinelMacro(rng);
    macros.gravity = drawSentinelMacro(rng);
    macros.entropy = drawSentinelMacro(rng);
    macros.pressure = drawSentinelMacro(rng);
    macros.weight = drawSentinelMacro(rng);
    macros.fog = drawSentinelMacro(rng);
    macros.life = drawSentinelMacro(rng);
    macros.depth = drawSentinelMacro(rng);
    macros.mass = drawSentinelMacro(rng);
    matrix.setMacros(macros);
    matrix.apply(engine);

    engine.setSubToneLevelOffsetDb(drawSentinelRange(rng, -24.0f, 24.0f));
    engine.setSubTrackingAmount(drawSentinelRange(rng, 0.0f, 1.0f));
    engine.setSmearAmount(drawSentinelRange(rng, 0.0f, 1.0f));
    engine.setSmearDecoherence(drawSentinelRange(rng, 0.0f, 1.0f));
    engine.setSmearTilt(drawSentinelRange(rng, -1.0f, 1.0f));
    engine.setGhostPeakLevel(drawSentinelRange(rng, 0.0f, 1.0f));
    engine.setAtmosBlur(drawSentinelRange(rng, 0.0f, 1.0f));
    engine.setOutputSaturation(drawSentinelRange(rng, 0.0f, 1.0f));

    engine.setEnvelopeMode(drawSentinelFlag(rng) ? VoragoVoice::EnvelopeMode::Growth
                                                 : VoragoVoice::EnvelopeMode::Standard);
}

/// @brief Render one configuration and return its three SC-013a observables.
///
/// THE POLYPHONY IS NOT DRAWN. SC-013a's fixture is "every polyphony in
/// [1, kMaxVoices]", and a uniform draw over 32 configurations leaves a value
/// uncovered with probability (7/8)^32 ~ 1.4 % per value - an assertion that is
/// silently weaker on some runs than on others. The deterministic stride below
/// covers each of the kMaxVoices values exactly kSentinelFuzzConfigs /
/// kMaxVoices times, on every machine.
[[nodiscard]] SentinelRender renderSentinelConfig(std::size_t configIndex) {
    Xorshift32 rng(deriveStreamSeed(kSentinelFuzzSeedBase, configIndex));

    const VoragoEngineConfig cfg = drawSentinelConfig(rng);
    auto engine = makeEngine(kSampleRate48, cfg);

    VoragoMacroMatrix matrix;
    configureSentinelEngine(*engine, matrix, rng);

    const std::size_t polyphony = 1u + (configIndex % VoragoEngine::kMaxVoices);
    engine->setPolyphony(polyphony);
    for (std::size_t v = 0; v < polyphony; ++v) {
        engine->noteOn(static_cast<std::uint8_t>(24u + (5u * v)), static_cast<std::uint8_t>(100));
    }

    const auto samples = static_cast<std::size_t>(kSentinelFuzzSeconds * kSampleRate48);
    return renderThroughOutputStage(*engine, samples, kSentinelBlock);
}

}  // namespace

TEST_CASE("VoragoEngine_ConfigurationFuzzSentinel", "[systems][vorago]") {
    static_assert(kSentinelFuzzConfigs >= VoragoEngine::kMaxVoices,
                  "SC-013a: the polyphony stride 1 + (index % kMaxVoices) must reach every value "
                  "at least once (equal coverage is not required; 32 configs over 6 values "
                  "since the Q-A ruling lowered kMaxVoices to 6)");

    std::size_t firstNonFinite = kSentinelFuzzConfigs;
    std::size_t firstUnbounded = kSentinelFuzzConfigs;
    std::size_t firstRecovery = kSentinelFuzzConfigs;
    std::uint32_t recoveriesThere = 0u;
    float worstPeak = 0.0f;
    std::size_t worstPeakConfig = 0u;

    for (std::size_t index = 0; index < kSentinelFuzzConfigs; ++index) {
        const SentinelRender outcome = renderSentinelConfig(index);

        if (outcome.peakAbs > worstPeak) {
            worstPeak = outcome.peakAbs;
            worstPeakConfig = index;
        }
        if (!outcome.allFinite && (firstNonFinite == kSentinelFuzzConfigs)) {
            firstNonFinite = index;
        }
        // Written positively so a NaN peak takes the FAILING branch.
        if (!(outcome.peakAbs <= 1.0f) && (firstUnbounded == kSentinelFuzzConfigs)) {
            firstUnbounded = index;
        }
        if ((outcome.recoveries != 0u) && (firstRecovery == kSentinelFuzzConfigs)) {
            firstRecovery = index;
            recoveriesThere = outcome.recoveries;
        }
    }

    {
        std::ostringstream os;
        os << std::fixed;
        os << "SC-013a: " << kSentinelFuzzConfigs << " configurations [0, " << kSentinelFuzzConfigs
           << ") x " << std::setprecision(1) << kSentinelFuzzSeconds << " s UNACCELERATED at "
           << kSampleRate48 << " Hz; worst |out| = " << std::setprecision(4) << worstPeak
           << " at configuration " << worstPeakConfig;
        WARN(os.str());
    }

    CAPTURE(firstNonFinite);
    CAPTURE(firstUnbounded);
    CAPTURE(firstRecovery);
    CAPTURE(recoveriesThere);
    CAPTURE(worstPeak);
    CAPTURE(worstPeakConfig);

    REQUIRE(firstNonFinite == kSentinelFuzzConfigs);
    REQUIRE(firstUnbounded == kSentinelFuzzConfigs);
    REQUIRE(firstRecovery == kSentinelFuzzConfigs);
    REQUIRE(worstPeak <= 1.0f);
}

// =============================================================================
// SC-021a - the sample-rate SENTINEL (48 kHz and 192 kHz)
// =============================================================================
// FR-085's per-push half of SC-021: the base rate and the extreme that has
// historically broken first. The remaining four rates are SC-021b
// (vorago_engine_longrun_test.cpp:979) and this case may not be merged into it.
//
// kFastAttackEnvelopeConfig IS REQUIRED HERE, not merely permitted: the first
// assertion is NON-SILENCE inside a 1 s window and the shipped 20 s attack
// cannot clear one. FR-014a names SC-021a explicitly as a criterion the fixture
// serves.
//
// THE EXCITATION IS IDENTICAL AT BOTH RATES - same seed, same kMaxVoices notes,
// same chord - because the +/- 3 dB clause compares LEVELS across rates and
// would otherwise be comparing two different pieces of music.
// -----------------------------------------------------------------------------

namespace {

constexpr double kSampleRate192 = 192000.0;
constexpr double kSentinelRateSeconds = 1.0;
constexpr double kSentinelRateToleranceDb = 3.0;

[[nodiscard]] SentinelRender renderSentinelAtRate(double sampleRate) {
    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(sampleRate, cfg);
    engine->setSeed(7u);
    engine->setPolyphony(VoragoEngine::kMaxVoices);
    applyFastAttack(*engine);
    for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
        engine->noteOn(static_cast<std::uint8_t>(36u + (3u * v)), static_cast<std::uint8_t>(100));
    }

    const auto samples = static_cast<std::size_t>(kSentinelRateSeconds * sampleRate);
    return renderThroughOutputStage(*engine, samples, kSentinelBlock);
}

}  // namespace

TEST_CASE("VoragoEngine_SampleRateSentinel", "[systems][vorago]") {
    const SentinelRender at48 = renderSentinelAtRate(kSampleRate48);
    const SentinelRender at192 = renderSentinelAtRate(kSampleRate192);

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        os << "SC-021a: " << kSentinelRateSeconds
           << " s full-polyphony renders, kFastAttackEnvelopeConfig (FR-014a);"
           << "\n  48 kHz : RMS = " << at48.rmsDb << " dBFS, peak " << at48.peakAbs
           << "\n  192 kHz: RMS = " << at192.rmsDb << " dBFS, peak " << at192.peakAbs << " ("
           << (at192.rmsDb - at48.rmsDb) << " dB vs 48 kHz)";
        WARN(os.str());
    }

    CAPTURE(at48.rmsDb);
    CAPTURE(at48.peakAbs);
    CAPTURE(at48.recoveries);
    CAPTURE(at192.rmsDb);
    CAPTURE(at192.peakAbs);
    CAPTURE(at192.recoveries);

    REQUIRE(at48.allFinite);
    REQUIRE(at48.recoveries == 0u);
    REQUIRE(at48.peakAbs <= 1.0f);
    REQUIRE(at48.rmsDb > kSentinelNonSilentFloorDb);

    REQUIRE(at192.allFinite);
    REQUIRE(at192.recoveries == 0u);
    REQUIRE(at192.peakAbs <= 1.0f);
    REQUIRE(at192.rmsDb > kSentinelNonSilentFloorDb);

    // Written positively so a NaN difference takes the FAILING branch.
    REQUIRE(std::fabs(at192.rmsDb - at48.rmsDb) <= kSentinelRateToleranceDb);
}

// =============================================================================
// SC-027 - the ghost configuration, on the object that now owns it
// =============================================================================
// OQ-1 ruling (b) moved the atmosphere off VoragoVoiceConfig and onto the
// engine, so both clauses read VoragoEngine::atmosphere() (vorago_engine.h:1012).
//
// CLAUSE 1 IS EXACT AND IT IS NOT DECORATION. An implementation that left
// AtmosphereEngine at its shipped defaults (density 4.0,
// atmosphere_engine.h:821-828) would satisfy every other criterion in this spec,
// and roadmap line 114's deliverable would ship unverified.
//
// CLAUSE 2, AND THE ONE DEVIATION THIS CASE RECORDS. The criterion's second arm
// is worded "with that scheduler's depth at 0". THERE IS NO SUCH PUBLIC LEVER:
// SlowEventScheduler::setDepthRange is written once at VoragoVoice::prepare()
// (vorago_voice.h:630) and the voice's only read surface is the CONST
// scheduler(i) accessor (:1226-1228), while VoragoEngine's sole mutable route
// into a voice is the four envelope forwarders (vorago_engine.h:668-690). The
// reachable gate on the same lane is FR-017's own gating multiply,
// `atmos_.setLevel(ghostPeak_ * ghost)` (vorago_engine.h:1215), closed with
// setGhostPeakLevel(0) - and closing it is what the criterion is really about:
// "bursts of ghosts, not a continuous wash" is a statement about that product.
// The arm is kept from becoming a silence test by asserting that the SAME render
// still drove getGhostRequest() above the burst threshold: the zero is the gate
// closing, not an absence of events.
//
// WHY THE MACRO MATRIX IS *NOT* APPLIED HERE, AND WHY APPLYING IT WAS A DEFECT.
// An earlier shape of this case put Life = 1 on the engine before rendering, on
// the reasoning that the voice ships ecosystemDepth_ = 0.0f and that the matrix
// Life row (base 0.85 + amount 0.15 since the 2026-09-19 Q-J ruling; it was
// 0.50 + 0.50, vorago_macro_matrix.h:557-561) is the only
// public route to the ecosystem Ghost lane. Both halves are wrong. prepare()
// ships setEcosystemDepth(0.85f) (vorago_voice.h:605; 0.0f is only the member
// initialiser of an UNPREPARED voice), and the route that matters here is the
// SCHEDULER, not the ecosystem. The old rig measured 0 burst edges over the full
// 600 s with the gate wide open. FR-020b makes the request the MAXIMUM of two
// contributions:
//
//     ghostRequest_ = max(ecosystemDepth_ * agentOutput, schedulerValue)
//     (vorago_voice.h, applyIdentityLanes() over reduceAgentLanes())
//
// The published agent output is a CONTINUOUS quantity - normalised energy times
// gate, ecosystem_engine.h publish() - so at ecosystemDepth_ = 1 the strongest
// of the six or seven Ghost-kind agents addressing slot 0 holds that maximum
// near its rail for the whole render. Measured on the old rig: max request
// 0.9824, max level 0.58944, and the level NEVER came back to the 0.05 x peak
// fall threshold. That is a continuous wash BY CONSTRUCTION, which is precisely
// what this criterion exists to rule out, so an arm that turns it on can never
// see a burst edge.
//
// The lane SC-027 is about is the SCHEDULER lane. FR-017: the burst gating
// "substitutes for the event-triggered scheduling AtmosphereEngine does not
// have"; AR-3: "the slow-event scheduler drives setLevel, which produces bursts
// of ghosts from a continuous-density scheduler"; FR-022 lists ghost-burst level
// among the five scheduler destination families. That lane is live straight out
// of VoragoVoice::prepare() (setDepthRange(0.4, 1.0), setTargetCount(5),
// vorago_voice.h:628-633) and needs no macro at all. The rig therefore writes
// FR-021's neutral itself - "at depth 0 every destination reads its configured
// base value and the ecosystem changes nothing" - which is what makes
// getGhostRequest() the scheduler value and nothing else, and is the same
// baseline SC-019 clause 1 sets up in the same words. Applying the matrix at
// Life = 0 would NOT achieve it: that row's base alone is 0.50.
//
// WHAT THAT LEAVES ON THE RECORD, for the phase owner. At the SHIPPED defaults
// (ecosystem depth 0.50, ghost peak 0.60) the ghost lane carries a continuous
// floor of about 0.5 x agentOutput, i.e. an atmosphere level around 0.29 that
// never returns to the 0.0 base between scheduler events. FR-017 says of that
// base "a base of 0 means silence between events, which is what makes a burst a
// burst", and FR-020b says the request is the maximum over the ecosystem lane
// as well. Those two read together only give silence between events at ecosystem
// depth 0. That is a spec-level tension, not something this case may decide, so
// it is recorded here rather than papered over: SC-027 measures the gating
// FR-017/AR-3 describe, on the lane they name.
//
// FR-086 ACCELERATION, AND WHY THE CRITERION NEEDS IT. At the shipped ranges one
// scheduler cycle is a period drawn from 20-90 s (fast) or 180-600 s (slow), and
// one family in five is the ghost, so 600 s of instrument time carries about 2.5
// expected ghost events - the >= 6 bar is unreachable without the acceleration
// the criterion itself calls for. A is applied to
// SlowEventScheduler::setIntervalRange, the FIRST entry on FR-086's permitted
// list, through the one setter that owns both ranges
// (VoragoVoice::applyEventRateScale, rewritten from eventRateScale_ at every
// control step). Nothing else moves: not the sample rate, the block size, the
// polyphony, nor any macro, gain, mix, damping or feedback value. The event
// envelope trio is deliberately NOT scaled and does not need to be:
// SlowEventScheduler fits the trio inside minIntervalSeconds_ at every onset
// (the FIT RULE, slow_event_scheduler.h:94-103), so a shorter interval shortens
// the event with it and events still cannot overlap.
//
// RESOLVED 2026-09-22. This block used to flag that FR-086's closing
// enumeration of the criteria permitted to use A (SC-013b, SC-014, SC-018,
// SC-019, SC-020, SC-020a) omitted SC-027, although SC-027's own text reads
// "accelerated per FR-086". SC-027 clause 2 is an event-count property on a
// control lane - exactly the family FR-086 admits - so the omission was a
// transcription gap between two paragraphs of the same requirement, not a
// decision. FR-086's enumeration now names SC-027 and states why
// (specs/vorago-phase10-voice-engine/spec.md, FR-086). Nothing in this case
// changed: the acceleration it applies was always the one SC-027 granted it.
// -----------------------------------------------------------------------------

namespace {

using Krate::DSP::AtmosphereEngine;

/// FR-017's burst edge: the level leaves the 0.0 base, reaches at least half the
/// configured peak, and comes back. The thresholds are derived from the
/// CONSTANT, never from the live getter, so the gated arm - where the getter
/// reads 0 - cannot silently lower its own bar to zero.
constexpr float kGhostRiseThreshold = 0.5f * VoragoEngine::kGhostBurstPeak;
constexpr float kGhostFallThreshold = 0.05f * VoragoEngine::kGhostBurstPeak;

/// @brief What one ghost-gating render observed.
struct GhostTrace {
    std::size_t burstEdges = 0u;   ///< completed 0 -> >= half-peak -> 0 excursions
    float maxLevel = 0.0f;         ///< the largest AtmosphereEngine::getLevel() seen
    float maxGhostRequest = 0.0f;  ///< the largest VoragoVoice::getGhostRequest() seen
};

/// @brief Drive @p engine for @p samples frames, polling the ghost lane at every
///        block boundary.
///
/// The audio is rendered and discarded: SC-027 is a CONTROL-lane criterion, and
/// the observable is AtmosphereEngine::getLevel(), which reports the last value
/// the FR-017 gating write installed (atmosphere_engine.h:982-986) rather than a
/// smoothed audio quantity.
[[nodiscard]] GhostTrace traceGhostLane(VoragoEngine& engine, std::size_t samples,
                                        std::size_t block) {
    GhostTrace trace;
    std::vector<float> l(block, 0.0f);
    std::vector<float> r(block, 0.0f);
    bool inBurst = false;

    for (std::size_t done = 0; done < samples; done += block) {
        const std::size_t n = std::min(block, samples - done);
        engine.processStereoBlock(l.data(), r.data(), n);

        const float level = engine.atmosphere().getLevel();
        trace.maxLevel = std::max(trace.maxLevel, level);
        trace.maxGhostRequest =
            std::max(trace.maxGhostRequest, engine.getVoice(0u).getGhostRequest());

        if (!inBurst && (level >= kGhostRiseThreshold)) {
            inBurst = true;
        } else if (inBurst && (level <= kGhostFallThreshold)) {
            inBurst = false;
            ++trace.burstEdges;
        }
    }
    return trace;
}

/// FR-086's factor for this case, stated once and printed with the result.
///
/// A = 10 is the ceiling of VoragoVoice::setEventRateScale (clamped to [0.1, 10])
/// and the only clock it moves is SlowEventScheduler::setIntervalRange: fast
/// 20-90 s becomes 2-9 s, slow 180-600 s becomes 18-60 s, both still above
/// kMinIntervalSeconds = 1.0 (slow_event_scheduler.h:154).
constexpr float kGhostEventRateScale = 10.0f;

/// @brief A polyphony-1 engine whose ghost lane is the SCHEDULER lane alone.
///
/// ecosystemDepth_ is left at the shipped 0.0, FR-021's neutral, so
/// getGhostRequest() is the ghost-destined scheduler's own value - see the banner
/// for why applying the Life macro here measured a wash instead.
///
/// THE const_cast IS THE ONLY ROUTE TO FR-086's LEVER. setEventRateScale is
/// voice-owned, VoragoEngine forwards only the four envelope setters
/// (vorago_engine.h:668-690) and getVoice() is const, so the alternative is the
/// macro matrix - whose Life row also drives EcosystemDepth and would put back
/// exactly the wash this case must not have. It is the idiom the Vorago voice TU
/// already uses for the same reason (mutableBloom / mutableCloud, after
/// seraphis_macro_test.cpp:420-435).
[[nodiscard]] std::unique_ptr<VoragoEngine> makeGhostEngine() {
    // A SHORT capture ring: the 20 s default is 2 x 20 x fs floats, and this case
    // never listens to a grain - only to the level the gate writes.
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;

    auto engine = makeEngine(kSampleRate8k, cfg);
    engine->setSeed(0x6057u);
    engine->setPolyphony(1u);
    applyFastAttack(*engine);
    engine->noteOn(33u, 100u);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto& voice = const_cast<VoragoVoice&>(engine->getVoice(0u));
    voice.setEventRateScale(kGhostEventRateScale);
    REQUIRE(voice.getEventRateScale() == kGhostEventRateScale);

    // FR-021 neutral, and it has to be written: prepare() ships ecosystem depth
    // at 0.50 (vorago_voice.h:605), not 0, so the ecosystem lane is HALF open by
    // default. SC-019 clause 1 sets up the same baseline in the same words
    // ("with ecosystem depth at 0"), and it is what leaves getGhostRequest()
    // equal to the scheduler value alone.
    voice.setEcosystemDepth(0.0f);
    REQUIRE(voice.getEcosystemDepth() == 0.0f);
    return engine;
}

}  // namespace

TEST_CASE("VoragoEngine_GhostConfiguration", "[systems][vorago]") {
    SECTION("clause 1: FR-017's values read back immediately after prepare()") {
        const VoragoEngineConfig cfg{};
        auto engine = makeEngine(kSampleRate48, cfg);
        const AtmosphereEngine& atmos = engine->atmosphere();

        // EXACT comparisons: both sides are the same literals, and a drifted copy
        // is precisely what this clause rules out.
        REQUIRE(atmos.getDensity() == 0.30f);
        REQUIRE(atmos.getGrainSeconds() == 12.0f);
        REQUIRE(atmos.getPitchSemitones() == -12.0f);
        REQUIRE(atmos.getPositionSpread() == 0.90f);
        REQUIRE(atmos.getBlur() == 0.85f);
        REQUIRE(atmos.getDecorrelation() == 0.85f);

        // The seventh value: the BASE is 0.0, which is what makes a burst a burst
        // (vorago_engine.h:305-307).
        REQUIRE(atmos.getLevel() == 0.0f);
    }

    SECTION("clause 2: the level is EVENT-GATED, not a continuous wash") {
        // 10 minutes of instrument time at the 8 kHz floor (FR-086). Every
        // quantity this clause counts - a scheduler interval, an ecosystem step, a
        // burst envelope - is defined in SECONDS or in CONTROL STEPS, none of them
        // a function of the sample rate, so the same 600 s is covered in one sixth
        // of the samples and nothing is approximated.
        constexpr double kGhostSeconds = 600.0;
        const auto samples = static_cast<std::size_t>(kGhostSeconds * kSampleRate8k);

        auto live = makeGhostEngine();
        REQUIRE(live->getGhostPeakLevel() > 0.0f);  // the gate is OPEN on this arm
        REQUIRE(live->getVoice(0u).getEcosystemDepth() == 0.0f);  // FR-021 neutral
        const GhostTrace open = traceGhostLane(*live, samples, kSentinelBlock);

        auto gated = makeGhostEngine();
        gated->setGhostPeakLevel(0.0f);
        REQUIRE(gated->getGhostPeakLevel() == 0.0f);
        const GhostTrace closed = traceGhostLane(*gated, samples, kSentinelBlock);

        {
            std::ostringstream os;
            os << std::fixed << std::setprecision(4);
            os << "SC-027 clause 2: " << std::setprecision(1) << kGhostSeconds << " s at "
               << kSampleRate8k << " Hz, polyphony 1, ecosystem depth 0, FR-086 A = "
               << kGhostEventRateScale << "; rise >= " << std::setprecision(4)
               << kGhostRiseThreshold << ", fall <= " << kGhostFallThreshold
               << "\n  gate OPEN  : " << open.burstEdges << " burst edges, max level "
               << open.maxLevel << ", max ghost request " << open.maxGhostRequest
               << "\n  gate CLOSED: " << closed.burstEdges << " burst edges, max level "
               << closed.maxLevel << ", max ghost request " << closed.maxGhostRequest;
            WARN(os.str());
        }

        CAPTURE(open.burstEdges);
        CAPTURE(open.maxLevel);
        CAPTURE(open.maxGhostRequest);
        CAPTURE(closed.burstEdges);
        CAPTURE(closed.maxLevel);
        CAPTURE(closed.maxGhostRequest);

        REQUIRE(open.burstEdges >= 6u);

        // The gated arm: exactly zero edges, and the level held its 0.0 base for
        // the whole render.
        REQUIRE(closed.burstEdges == 0u);
        REQUIRE(closed.maxLevel == 0.0f);

        // ...and the gated render still FIRED the events, so the zero above is the
        // gate closing rather than a silent lane.
        REQUIRE(closed.maxGhostRequest >= kGhostRiseThreshold);
    }
}

// =============================================================================
// SC-028 (engine half) - the setter contract
// =============================================================================
// FR-071, stated once and evaluated over a table rather than at twenty call
// sites: every public float setter rejects non-finite input WITH THE PREVIOUS
// VALUE STANDING, clamps out-of-range input with the getter reporting the clamp,
// and every index-taking setter treats an out-of-range index as a silent no-op.
// This is the uniform rule phases 2-9 all adopted - bloom_engine.h:524-529 states
// it verbatim - and the plan restates it for this phase's own setters (plan S5,
// `setBodyBlend(float b)` rejects a non-finite argument (previous value stands)).
//
// THE WHOLE TABLE IS MEASURED BEFORE ANY REQUIRE FIRES. A row-at-a-time case
// reports one offender per run, and a contract this wide is only actionable as a
// list - the same per-file discipline continuous_body_perf_test.cpp:875-880 sets
// for the material survey.
//
// THREE CLAMP KINDS, because "out of range" is only meaningful where a range
// exists:
//   Range       - the setter's own clamp is stated in vorago_engine.h, so the
//                 expected post-clamp value is a number and is asserted as one.
//   ClampsAway  - the clamp belongs to the owning component and is NOT restated
//                 here (a guessed bound would be a second surface that can
//                 disagree with the first). The assertion is that an absurd
//                 argument does not survive to the getter.
//   Unbounded   - the setter documents no range at all, so only finiteness is
//                 asserted. Asserting a clamp here would be inventing one.
// -----------------------------------------------------------------------------

namespace {

enum class ClampKind : std::uint8_t { Range, ClampsAway, Unbounded };

/// The absurd arguments. 1e9 is far outside every range any of these setters
/// carries and is exactly representable in a float, so `== kHugeHigh` is a sound
/// way to ask "did this survive unclamped?".
constexpr float kHugeHigh = 1.0e9f;
constexpr float kHugeLow = -1.0e9f;

/// @brief One row of SC-028's setter table, for either class.
template <typename T>
struct FloatSetterRow {
    const char* name;
    void (T::*set)(float);
    float (T::*get)() const;
    float probe;  ///< an in-range value, deliberately NOT the shipped default
    ClampKind kind;
    float lo;  ///< meaningful only when kind == ClampKind::Range
    float hi;
};

/// @brief What one row's six writes produced.
struct SetterOutcome {
    const char* name = "";
    float afterProbe = 0.0f;
    float afterNaN = 0.0f;
    float afterPosInf = 0.0f;
    float afterNegInf = 0.0f;
    float afterHigh = 0.0f;
    float afterLow = 0.0f;
};

/// @brief Run one row: the probe, then the three non-finites, then both extremes.
///
/// Every non-finite write STARTS FROM the probe value - the first follows the
/// probe read directly, the other two re-write it - so the three are independent
/// observations of the same contract rather than a chain in which the first
/// failure hides the other two.
template <typename T>
[[nodiscard]] SetterOutcome runSetterRow(T& target, const FloatSetterRow<T>& row) {
    const float nanValue = makeNaNFloat();
    const float posInf = makeNonFiniteFloat(kPosInfBits);
    const float negInf = makeNonFiniteFloat(kNegInfBits);

    SetterOutcome out;
    out.name = row.name;

    (target.*row.set)(row.probe);
    out.afterProbe = (target.*row.get)();

    (target.*row.set)(nanValue);
    out.afterNaN = (target.*row.get)();

    (target.*row.set)(row.probe);
    (target.*row.set)(posInf);
    out.afterPosInf = (target.*row.get)();

    (target.*row.set)(row.probe);
    (target.*row.set)(negInf);
    out.afterNegInf = (target.*row.get)();

    (target.*row.set)(kHugeHigh);
    out.afterHigh = (target.*row.get)();

    (target.*row.set)(kHugeLow);
    out.afterLow = (target.*row.get)();

    return out;
}

/// @brief The whole table, one line per row, printed before any REQUIRE.
[[nodiscard]] std::string formatSetterTable(const char* title,
                                            std::span<const SetterOutcome> rows) {
    std::ostringstream os;
    os << title << "\n  " << std::setw(30) << std::left << "setter" << std::right << std::setw(12)
       << "probe" << std::setw(12) << "NaN" << std::setw(12) << "+Inf" << std::setw(12) << "-Inf"
       << std::setw(12) << "+1e9" << std::setw(12) << "-1e9";
    os << std::fixed << std::setprecision(4);
    for (const SetterOutcome& o : rows) {
        os << "\n  " << std::setw(30) << std::left << o.name << std::right << std::setw(12)
           << o.afterProbe << std::setw(12) << o.afterNaN << std::setw(12) << o.afterPosInf
           << std::setw(12) << o.afterNegInf << std::setw(12) << o.afterHigh << std::setw(12)
           << o.afterLow;
    }
    return os.str();
}

/// @brief FR-071's three clauses, asserted for one measured row.
template <typename T>
void requireSetterContract(const FloatSetterRow<T>& row, const SetterOutcome& outcome) {
    INFO("setter " << row.name);

    // The probe must land somewhere usable at all, or the clauses below are
    // comparisons against a broken reading.
    REQUIRE(isFiniteBits(outcome.afterProbe));

    // (a) A non-finite argument is REJECTED and the previous value stands.
    REQUIRE(outcome.afterNaN == outcome.afterProbe);
    REQUIRE(outcome.afterPosInf == outcome.afterProbe);
    REQUIRE(outcome.afterNegInf == outcome.afterProbe);

    // (b) An out-of-range argument is CLAMPED and the getter reports the clamp.
    REQUIRE(isFiniteBits(outcome.afterHigh));
    REQUIRE(isFiniteBits(outcome.afterLow));
    switch (row.kind) {
        case ClampKind::Range:
            REQUIRE(outcome.afterHigh == row.hi);
            REQUIRE(outcome.afterLow == row.lo);
            break;
        case ClampKind::ClampsAway:
            REQUIRE(outcome.afterHigh != kHugeHigh);
            REQUIRE(outcome.afterLow != kHugeLow);
            break;
        case ClampKind::Unbounded:
            break;
    }
}

/// SC-028's engine table: every public float setter on VoragoEngine, in
/// declaration order (vorago_engine.h:685-785).
constexpr std::array<FloatSetterRow<VoragoEngine>, 10> kEngineSetters{{
    // No clamp is stated for the offset itself: SubharmonicEngine clamps the
    // three tone levels it is fanned out onto, and the getter reports the stored
    // offset (vorago_engine.h:724-728).
    {.name = "setSubToneLevelOffsetDb",
     .set = &VoragoEngine::setSubToneLevelOffsetDb,
     .get = &VoragoEngine::getSubToneLevelOffsetDb,
     .probe = -6.0f,
     .kind = ClampKind::Unbounded,
     .lo = 0.0f,
     .hi = 0.0f},
    {.name = "setSubTrackingAmount",
     .set = &VoragoEngine::setSubTrackingAmount,
     .get = &VoragoEngine::getSubTrackingAmount,
     .probe = 0.25f,
     .kind = ClampKind::Range,
     .lo = 0.0f,
     .hi = 1.0f},
    {.name = "setSmearAmount",
     .set = &VoragoEngine::setSmearAmount,
     .get = &VoragoEngine::getSmearAmount,
     .probe = 0.75f,
     .kind = ClampKind::Range,
     .lo = 0.0f,
     .hi = 1.0f},
    {.name = "setSmearDecoherence",
     .set = &VoragoEngine::setSmearDecoherence,
     .get = &VoragoEngine::getSmearDecoherence,
     .probe = 0.35f,
     .kind = ClampKind::Range,
     .lo = 0.0f,
     .hi = 1.0f},
    {.name = "setSmearTilt",
     .set = &VoragoEngine::setSmearTilt,
     .get = &VoragoEngine::getSmearTilt,
     .probe = -0.40f,
     .kind = ClampKind::Range,
     .lo = -1.0f,
     .hi = 1.0f},
    {.name = "setGhostPeakLevel",
     .set = &VoragoEngine::setGhostPeakLevel,
     .get = &VoragoEngine::getGhostPeakLevel,
     .probe = 0.25f,
     .kind = ClampKind::Range,
     .lo = 0.0f,
     .hi = 1.0f},
    {.name = "setAtmosBlur",
     .set = &VoragoEngine::setAtmosBlur,
     .get = &VoragoEngine::getAtmosBlur,
     .probe = 0.40f,
     .kind = ClampKind::Range,
     .lo = 0.0f,
     .hi = 1.0f},
    {.name = "setOutputSaturation",
     .set = &VoragoEngine::setOutputSaturation,
     .get = &VoragoEngine::getOutputSaturation,
     .probe = 0.30f,
     .kind = ClampKind::Range,
     .lo = 0.0f,
     .hi = 1.0f},
    // The getter reads the voice's raw shadow (vorago_voice.h:1013-1016, :1035):
    // MultiStageEnvelope applies its own ceiling downstream, so no range is
    // asserted on this surface.
    {.name = "setEnvelopeReleaseMs",
     .set = &VoragoEngine::setEnvelopeReleaseMs,
     .get = &VoragoEngine::getEnvelopeReleaseMs,
     .probe = 250.0f,
     .kind = ClampKind::Unbounded,
     .lo = 0.0f,
     .hi = 0.0f},
    // GrowthEnvelope clamps to [kMinDuration, maxDuration_], and prepare() raises
    // the per-instance ceiling to kGrowthMaxDurationSeconds
    // (growth_envelope.h:97, :146-147; vorago_voice.h:671).
    {.name = "setGrowthDurationSeconds",
     .set = &VoragoEngine::setGrowthDurationSeconds,
     .get = &VoragoEngine::getGrowthDurationSeconds,
     .probe = 30.0f,
     .kind = ClampKind::Range,
     .lo = 1.0f,
     .hi = VoragoVoice::kGrowthMaxDurationSeconds},
}};

}  // namespace

TEST_CASE("VoragoEngine_SetterContract", "[systems][vorago]") {
    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;
    auto engine = makeEngine(kSampleRate8k, cfg);

    SECTION("every public float setter: reject non-finite, clamp out-of-range") {
        std::array<SetterOutcome, kEngineSetters.size()> outcomes{};
        for (std::size_t i = 0; i < kEngineSetters.size(); ++i) {
            outcomes[i] = runSetterRow(*engine, kEngineSetters[i]);
        }

        WARN(formatSetterTable("SC-028 (engine half) - VoragoEngine float setters",
                               std::span<const SetterOutcome>(outcomes.data(), outcomes.size())));

        for (std::size_t i = 0; i < kEngineSetters.size(); ++i) {
            requireSetterContract(kEngineSetters[i], outcomes[i]);
        }
    }

    SECTION("an out-of-range stage index is a silent no-op that writes NOTHING") {
        // The guard is against MultiStageEnvelope::kMaxStages (8), not against
        // VoragoVoice::kEnvelopeStages (6): both shadow arrays are sized to
        // kMaxStages (vorago_voice.h:2097-2098), so stages 6 and 7 are legal
        // storage and only -1 and >= 8 are out of range.
        constexpr int kStages = MultiStageEnvelope::kMaxStages;

        for (int stage = 0; stage < kStages; ++stage) {
            engine->setEnvelopeStageTimeMs(stage, 40.0f + static_cast<float>(stage));
        }

        std::array<float, static_cast<std::size_t>(kStages)> before{};
        for (int stage = 0; stage < kStages; ++stage) {
            before[static_cast<std::size_t>(stage)] = engine->getEnvelopeStageTimeMs(stage);
        }

        engine->setEnvelopeStageTimeMs(-1, 999.0f);
        engine->setEnvelopeStageTimeMs(kStages, 999.0f);
        engine->setEnvelopeStageTimeMs(kStages + 7, 999.0f);

        for (int stage = 0; stage < kStages; ++stage) {
            INFO("stage " << stage);
            REQUIRE(engine->getEnvelopeStageTimeMs(stage)
                    == before[static_cast<std::size_t>(stage)]);
        }

        // ...and the out-of-range READ is a no-op too, so a caller cannot use it
        // to observe something that was never written.
        REQUIRE(engine->getEnvelopeStageTimeMs(-1) == 0.0f);
        REQUIRE(engine->getEnvelopeStageTimeMs(kStages) == 0.0f);
    }
}
