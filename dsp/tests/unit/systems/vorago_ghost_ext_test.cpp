// ==============================================================================
// Layer 3: System Tests - VoragoEngine's ghost extension wiring
//          (specs/vorago-phase10a-ghost-extension)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10a-ghost-extension/spec.md   (SC-010 (a)-(e))
//            specs/vorago-phase10a-ghost-extension/plan.md
//            specs/vorago-phase10a-ghost-extension/tasks.md  (T003 created this
//            stub; T020 fills it; T021 implements what it measures)
//
// SCOPE OF THIS TU (tasks.md T003's table): SC-010 and nothing else - the
// VoragoEngine side of the phase, i.e. the engine-level consequences of the
// AtmosphereEngine ghost extension, including the base-commit Vorago render
// reference harvested by T005 (VoragoGhostFix::kBaseCommitVoragoFingerprint).
// Separated from the atmosphere_ghost_* TUs because its unit under test is
// vorago_engine.h, the phase's second and last production file.
//
// This TU is DELIBERATELY NOT in the -fno-fast-math block of
// dsp/tests/CMakeLists.txt: it injects no bit patterns, and the guards it
// exercises must be proved in the /fp:fast + -ffast-math mode the headers
// actually ship in.
//
// THE ACCESSOR IS `engine.atmosphere()` - `[[nodiscard]] const AtmosphereEngine&
// atmosphere() const noexcept` (vorago_engine.h:1066, read this session). There
// is NO `VoragoEngine::atmos()`; `atmos_` is the private member at :1498.
//
// WHY THIS TU IS RED BEFORE T021: `VoragoEngineConfig` (vorago_engine.h:105-134)
// has neither `atmosGhostReverseProbability` nor `atmosGhostEventTriggers`, so
// makeWiringEngine() below does not compile. That is the intended red - a
// COMPILE failure, not an assertion failure, which is the strongest form the
// FR-030 config half can be tested in.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "atmosphere_ghost_fixtures.h"

#include <krate/dsp/systems/atmosphere_engine.h>  // L3  the ghost tap's component
#include <krate/dsp/systems/vorago_engine.h>      // L3  the unit under test
#include <krate/dsp/systems/vorago_voice.h>       // L3  setEventRateScale / setEcosystemDepth

#include <render_fingerprint.h>  // test_helpers  SC-010 (a)'s comparison
#include <vorago_fixtures.h>     // test_helpers  makeEngine / renderEngine / applyFastAttack

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using Krate::DSP::AtmosphereEngine;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoVoice;

using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::makeEngine;
using Krate::DSP::TestUtils::Vorago::renderEngine;

namespace {

// =============================================================================
// 1. FR-046's MEASURED per-comparison bounds for kBaseCommitVoragoFingerprint
// =============================================================================
//
// THE 2026-09-24 THREE-TOOLCHAIN PROBE (FR-046's measurement, ruling B-1).
//
// This is the SECOND of the phase's two stored-golden comparisons; the first is
// atmosphere_ghost_test.cpp's clause 1, whose block this one reproduces clause
// for clause. Same probe, same run, same method: Fixture B's recipe - the B-2
// sounding recipe of atmosphere_ghost_fixtures.h section 6.2 - compiled
// standalone against dsp/include + tests/test_helpers + the KrateDSP .cpp list
// and run under
//     g++ 13.3.0     -std=c++20 -O3
//     g++ 13.3.0     -std=c++20 -O3 -ffast-math
//     clang++ 18.1.3 -std=c++20 -O2
// on Ubuntu 24.04 (WSL2), each with `enableFTZDAZ()` before the render, diffed
// against the MSVC 19.44 /O2 IN-SUITE fingerprint stored as
// kBaseCommitVoragoFingerprint.
//
// Relative deviation from the stored MSVC reference, per metric, worst over the
// three GNU/LLVM legs:
//
//     rms              8.4859e-4   (clang++ -O2)
//     peak             1.5878e-3   (g++ -O3 -ffast-math)
//     meanAbs          6.2286e-4   (clang++ -O2)
//     totalVariation   6.8181e-5   (clang++ -O2)
//     worst checkpoint 2.1693e-3   (absolute, checkpoint 18; g++ -O3 -ffast-math)
//
// FIXTURE B'S SPREAD IS ~6x FIXTURE A'S, and the reason is structural rather
// than mysterious: this render walks the whole engine - VoragoVoice's OU-drifted
// modulators, SubharmonicEngine, ContinuousBody, the atmosphere tap and the
// master chain - so a legal reassociation early in a 60 s trajectory moves where
// every later sample lands. Ruling B-3 measured a related term on MSVC alone: a
// STANDALONE build of this same recipe at the base commit reads 1.0797e-4 (peak)
// / 6.53e-5 (checkpoint) against the in-suite build, through /fp:fast COMDAT
// selection of two Seraphis-era header inlines. The GNU/LLVM legs above are
// standalone builds compared against the in-suite MSVC reference, so each figure
// already CONTAINS a term of that size; at 1.1e-4 against a 1.6e-3 spread it is
// ~7% of the measurement, and the headroom below covers it several times over.
//
// The bounds sit at ~3x the measured noise, the headroom band
// noise_organism_test.cpp:3244-3286 and render_fingerprint.h:37-39 use.
//
// The discipline is noise_organism_test.cpp:3288-3406's:
//   * a per-comparison bound is a documented LOOSENING of the shared
//     cross-toolchain constants, never a tightening - the first two
//     static_asserts below;
//   * the SHARED constants must still hold their shipped values - the second
//     two - so a "fix" that edits render_fingerprint.h to make this comparison
//     pass breaks the BUILD instead of quietly widening every caller's bound;
//   * a paste-ready literal is printed on failure
//     (baseCommitVoragoFingerprintLiteral below);
//   * the stored fingerprint carries a PROVENANCE block - it does, at
//     atmosphere_ghost_fixtures.h:401-437, naming base commit
//     374580d7d0f0631561413310bd3085e15ba7279c, the machine, the compiler and
//     the date.
//
// NO BOUND MAY BE WIDENED AFTER IT IS RECORDED (FR-046, spec.md:614-616). The
// two numbers below ARE recorded, here and in the phase's compliance table.
// -----------------------------------------------------------------------------

/// Per-comparison checkpoint-sample bound for Fixture B.
/// MEASURED: worst checkpoint spread 2.1693e-3 -> 2.8x headroom.
constexpr float kMeasuredSampleTolerance = 6.0e-3f;

/// Per-comparison aggregate-metric bound for Fixture B.
/// MEASURED: worst metric spread 1.5878e-3 (`peak`) -> 3.1x headroom.
constexpr double kMeasuredMetricTolerance = 5.0e-3;

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
// at `true` (ruling B-1: the bounds land and the guards come out in the same
// edit). Clause (a)'s fingerprint arm runs unconditionally.

/// @brief Format `fp` as the exact `kBaseCommitVoragoFingerprint` initialiser to
///        paste into atmosphere_ghost_fixtures.h section 6.2.
///
/// FR-046's "paste-ready literal printed on comparison failure". It exists so a
/// LEGITIMATE regeneration - one made alongside a deliberate change to what
/// Fixture B renders - is a copy rather than 36 hand-typed numbers. The doubles
/// print at 17 significant digits and the checkpoints at 9, which is exactly how
/// the stored constant is written today (atmosphere_ghost_fixtures.h:524-534),
/// so a paste is a diff of VALUES and never of formatting.
[[nodiscard]] std::string baseCommitVoragoFingerprintLiteral(
    const Krate::DSP::TestUtils::RenderFingerprint& fp) {
    std::ostringstream os;
    os << "inline constexpr Krate::DSP::TestUtils::RenderFingerprint "
          "kBaseCommitVoragoFingerprint{\n";
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

// =============================================================================
// 2. The engaged-arm operating point - Phase 10's makeGhostEngine() shape
// =============================================================================
//
// Restated here rather than reached for, because vorago_engine_test.cpp's
// makeGhostEngine() lives in that TU's own anonymous namespace (:2730) and is
// not linkable from here. Every value is the one SC-010 (b) names, and the ONE
// deliberate divergence from Phase 10's fixture is called out at the field.

constexpr double kSampleRate48 = 48000.0;  ///< clause (a): the default Vorago rate
constexpr double kSampleRate8k = 8000.0;   ///< clauses (b)-(e): FR-086's accelerated floor
constexpr std::uint32_t kGhostSeed = 0x6057u;

/// The ceiling of VoragoVoice::setEventRateScale - clamped to [0.1, 10]
/// (vorago_voice.h:1353-1358, read this session). The only clock it moves is
/// SlowEventScheduler::setIntervalRange, so fast 20-90 s becomes 2-9 s.
constexpr float kGhostEventRateScale = 10.0f;

/// SC-027's two thresholds, derived from the CONSTANT and never from the live
/// getter - so the gated arm, where getGhostPeakLevel() reads 0, cannot silently
/// lower its own bar to zero. kGhostBurstPeak = 0.60f (vorago_engine.h:206), so
/// these are 0.30 and 0.03. Same definition as vorago_engine_test.cpp:2666-2667.
constexpr float kGhostRiseThreshold = 0.5f * VoragoEngine::kGhostBurstPeak;
constexpr float kGhostFallThreshold = 0.05f * VoragoEngine::kGhostBurstPeak;

/// SC-010 (b)'s observation grid. 64 samples so the polling grid EQUALS the
/// control grid (runPreRenderControlStep runs at phase == 0,
/// vorago_engine.h:887-889) and no burst edge is missed by aliasing.
constexpr std::size_t kGhostBlock = 64u;

/// SC-010 (b)/(c)'s render length, in instrument seconds. Phase 10's SC-027
/// clause 2 measured its `>= 6` burst floor over exactly this window
/// (vorago_engine_test.cpp:2819), which is what lets clause (b) reuse the floor.
constexpr double kEngagedSeconds = 600.0;

/// SC-010 (e)'s post-pre-roll window. At AtmosphereEngine density 0.30 (FR-017,
/// vorago_engine.h's prepare block, asserted by VoragoEngine_GhostConfiguration
/// clause 1) the nominal interonset is 1/0.30 = 3.33 s, so 60 s is ~18 density
/// scheduler ticks - "several births" with room for jitter, and cheap at 8 kHz.
constexpr double kReverseSeconds = 60.0;

/// The capture ring at 8 kHz with the SHIPPED atmosCaptureSeconds = 20.0f
/// (vorago_engine.h:120): nextPowerOf2(160 000) = 262 144 samples = 32.768 s.
/// Stated as a constant and ASSERTED against the live getter below, because
/// every pre-roll in this TU is a sample count and never a duration - the ring
/// is power-of-two rounded (rolling_capture_buffer.h:75-93) and a
/// seconds-stated pre-roll would be 7.8 s short, leaving the ring FILLING where
/// FR-050 rejects reverse births and a ring-cold delta assertion fails on
/// correct code.
constexpr std::size_t kGhostCaptureCapacity8k = 262144u;

/// @brief Phase 10's `makeGhostEngine()` shape, plus the two Phase 10a config
///        fields SC-010 exercises.
///
/// @param eventTriggers      `VoragoEngineConfig::atmosGhostEventTriggers`
/// @param reverseProbability `VoragoEngineConfig::atmosGhostReverseProbability`
///
/// THE ONE DELIBERATE DIVERGENCE FROM PHASE 10's FIXTURE: `atmosCaptureSeconds`
/// is left at the shipped 20.0f. Phase 10 set 1.0f (vorago_engine_test.cpp:2742)
/// because SC-027 never listens to a grain, only to the level the gate writes.
/// SC-010 (b) and (e) DO need grains admitted, and a 1 s ring can never hold a
/// 12 s ghost grain (FR-017), so the short ring would make both arms vacuous.
///
/// THE const_cast IS THE ONLY ROUTE to setEventRateScale / setEcosystemDepth:
/// both are voice-owned, VoragoEngine forwards only the four envelope setters
/// (vorago_engine.h:688-696) and getVoice() is const. It is the idiom Phase 10's
/// own fixture uses at vorago_engine_test.cpp:2744 for exactly this reason.
[[nodiscard]] std::unique_ptr<VoragoEngine> makeWiringEngine(bool eventTriggers,
                                                             float reverseProbability) {
    VoragoEngineConfig cfg{};
    cfg.atmosGhostEventTriggers = eventTriggers;
    cfg.atmosGhostReverseProbability = reverseProbability;

    auto engine = makeEngine(kSampleRate8k, cfg);
    engine->setSeed(kGhostSeed);
    engine->setPolyphony(1u);
    applyFastAttack(*engine);
    engine->noteOn(33u, 100u);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto& voice = const_cast<VoragoVoice&>(engine->getVoice(0u));
    voice.setEventRateScale(kGhostEventRateScale);
    REQUIRE(voice.getEventRateScale() == kGhostEventRateScale);

    // FR-021 neutral, and it HAS to be written: without it the ecosystem term
    // `ecosystemDepth_[kGhost] * output[i]` (vorago_voice.h:1734) keeps the
    // ghost request continuously non-zero, and the lane stops being the
    // scheduler's alone - which is the whole basis of the burst-edge count.
    voice.setEcosystemDepth(0.0f);
    REQUIRE(voice.getEcosystemDepth() == 0.0f);

    // The ring geometry every pre-roll and every ring-cold assertion below rests
    // on. If this ever moved, the pre-roll would be measuring a different
    // fixture.
    REQUIRE(engine->atmosphere().getCaptureCapacitySamples() == kGhostCaptureCapacity8k);
    return engine;
}

/// @brief Render exactly `getCaptureCapacitySamples()` samples through @p engine,
///        saturating its capture ring. Returns the samples rendered.
///
/// The Vorago-side counterpart of `VoragoGhostFix::preRollFullRing()` (which
/// takes an `AtmosphereEngine&` and drives it with its own excitation). Here the
/// excitation is the INSTRUMENT: the note started in makeWiringEngine() is what
/// fills the ring, so there is nothing to inject.
///
/// Availability saturates and stays saturated because
/// `RollingCaptureBuffer::getAvailableSamples()` is
/// `std::min(samplesWritten_, capacity_)` (rolling_capture_buffer.h:443-445).
std::size_t preRollVoragoFullRing(VoragoEngine& engine, std::size_t block) {
    const std::size_t capacity = engine.atmosphere().getCaptureCapacitySamples();
    if (capacity == 0u || block == 0u) {
        return std::size_t{0};
    }
    std::vector<float> l(block, 0.0f);
    std::vector<float> r(block, 0.0f);
    std::size_t rendered = 0u;
    while (rendered < capacity) {
        const std::size_t n = std::min(block, capacity - rendered);
        engine.processStereoBlock(l.data(), r.data(), n);
        rendered += n;
    }
    return rendered;
}

/// @brief The four trigger-lane counters, snapshotted as one value.
struct GhostCounters {
    std::uint64_t triggeredBorn = 0u;
    std::uint64_t dropped = 0u;
    std::uint64_t skipPoolFull = 0u;
    std::uint64_t skipRingCold = 0u;
};

[[nodiscard]] GhostCounters snapshotCounters(const AtmosphereEngine& atmos) noexcept {
    return GhostCounters{.triggeredBorn = atmos.getTotalTriggeredGrainsBorn(),
                         .dropped = atmos.getDroppedTriggerCount(),
                         .skipPoolFull = atmos.getSkippedTriggerCountPoolFull(),
                         .skipRingCold = atmos.getSkippedTriggerCountRingCold()};
}

/// @brief What one ghost-gating render observed.
struct GhostTrace {
    std::size_t burstEdges = 0u;   ///< completed excursions COMPLETED INSIDE the window
    bool carriedInBurst = false;   ///< a burst was already in flight when the window opened
    float maxLevel = 0.0f;         ///< the largest AtmosphereEngine::getLevel() seen
    float maxGhostRequest = 0.0f;  ///< the largest VoragoVoice::getGhostRequest() seen
};

/// @brief Drive @p engine for @p samples frames, polling the ghost lane at every
///        block boundary with SC-027's two-state detector
///        (vorago_engine_test.cpp:2699-2704).
///
/// The audio is rendered and discarded: SC-010 (b)-(d) are CONTROL-lane
/// criteria, and the observable is `AtmosphereEngine::getLevel()`, which reports
/// the last value the FR-017 gating write installed rather than a smoothed audio
/// quantity.
///
/// THE ONE ADDITION TO PHASE 10's DETECTOR, AND WHY IT IS NOT A WEAKENING.
/// Phase 10 started its detector on a freshly prepared engine, where the level
/// is exactly the 0.0 base, so "in a burst" was unambiguously false at sample 0.
/// SC-010 (b) opens its window AFTER a 262 144-sample pre-roll, during which
/// bursts fire and their triggers land in the SNAPSHOT rather than in the delta.
/// A burst still in flight when the window opens would therefore contribute an
/// edge with no matching trigger inside the window and break `edges <= delta` on
/// correct code. So the detector seeds itself from the level - `> fall` means
/// "possibly mid-excursion" - and SKIPS the first completed fall when it did.
/// This is the exact mirror image of the documented `+1` for a burst in flight
/// at the END of the window, and it can only ever REMOVE an edge, never invent
/// one: in the ambiguous case (level inside the band but the production latch
/// not yet armed) the trigger DOES land in the window and the arm then measures
/// `delta == edges + 1`, which the band already admits.
[[nodiscard]] GhostTrace traceGhostLane(VoragoEngine& engine, std::size_t samples,
                                        std::size_t block) {
    GhostTrace trace;
    std::vector<float> l(block, 0.0f);
    std::vector<float> r(block, 0.0f);

    bool inBurst = engine.atmosphere().getLevel() > kGhostFallThreshold;
    trace.carriedInBurst = inBurst;
    bool pendingCarryIn = inBurst;

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
            if (pendingCarryIn) {
                pendingCarryIn = false;  // its trigger fired before the window opened
            } else {
                ++trace.burstEdges;
            }
        }
    }
    return trace;
}

/// @brief Wall-clock seconds since @p start, for the runtime the tasks require
///        recorded for every arm of this TU.
[[nodiscard]] double elapsedSeconds(
    const std::chrono::steady_clock::time_point& start) noexcept {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start).count();
}

}  // namespace

// =============================================================================
// SC-010 (a), (c), (e) - the untagged arms
// =============================================================================
// (b) and (d) live in the `[long]` case below, per decision D-4: Phase 10's own
// 600 s ghost case is untagged (vorago_engine_test.cpp:2759), but that one never
// renders a grain, while (b) does. (c) stays HERE even though it also runs 600 s
// at 8 kHz, because with the gate closed no grain is ever spawned and its cost
// is Phase 10's own - the tasks assign it to the untagged lane and the measured
// runtime is reported below either way.
TEST_CASE("VoragoEngine_GhostExtensionWiring", "[vorago][ghost]") {
    // -------------------------------------------------------------------------
    // (a) part 1 - the config fields are INERT AT THEIR DEFAULTS (FR-030, ADR-3)
    // -------------------------------------------------------------------------
    // Cheap, and deliberately NOT behind the measured-bounds protocol: this half of
    // clause (a) is an integer/exact-float statement about the shipped defaults
    // and has no toolchain spread for a measured bound to absorb. Phase 10's
    // SC-027 and its checked-in kEngineBaselineNsAtPoly4 were measured on the
    // shipped ghost path, and a phase whose own criteria say the ceiling is
    // unchanged may not move either; Phase 14's presets are what engage these.
    SECTION("clause (a) - VoragoEngineConfig ships both new fields inert") {
        const VoragoEngineConfig defaults{};
        REQUIRE(defaults.atmosGhostReverseProbability == 0.0f);
        REQUIRE_FALSE(defaults.atmosGhostEventTriggers);  // ADR-3: inert, i.e. false

        auto engine = makeEngine(kSampleRate48, defaults);

        // FR-034's forwarding, observed at its default: prepare() must have
        // written the config value onto the component, and the config value is 0.
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 0.0f);

        // Nothing has been rendered, so the trigger lane cannot have been entered
        // even by an implementation that mis-gated the latch at prepare time.
        REQUIRE(engine->atmosphere().getTotalTriggeredGrainsBorn() == std::uint64_t{0});
    }

    // -------------------------------------------------------------------------
    // (a) part 2 - the 60 s default render is unchanged from the base commit
    // -------------------------------------------------------------------------
    // PROTOCOL: exactly Fixture B of atmosphere_ghost_fixtures.h section 6.2, the
    // SOUNDING recipe of ruling B-2 (spec.md, Session 2026-09-23 build stage) -
    // makeEngine(48000.0, VoragoEngineConfig{}) (vorago_fixtures.h:734), then
    // setSeed(0x6057u), setPolyphony(1u), noteOn(33u, 100u) at sample 0 and
    // NOTHING else written (no macro write, no applyFastAttack, no
    // ecosystem-depth write), then renderEngine(engine, l, r, 2 880 000, 512)
    // (:750), fingerprinting the LEFT channel. The note is held for the full
    // 60 s. Any deviation makes the stored constant inapplicable.
    //
    // The reference is a sounding render (B-2: the no-note-on render was digital
    // silence and pinned nothing), so FR-046's measured bounds apply to it
    // normally - every metric and checkpoint is exercised. A vacuity guard
    // REQUIRE(actual.rms > 0.0) after the render makes sure the comparison can
    // never pass on silence-versus-silence.
    SECTION("clause (a) - the 60 s default render is unchanged from the base commit") {
        const auto started = std::chrono::steady_clock::now();

        auto engine = makeEngine(kSampleRate48, VoragoEngineConfig{});
        engine->setSeed(0x6057u);
        engine->setPolyphony(1u);
        engine->noteOn(33u, 100u);
        std::vector<float> left;
        std::vector<float> right;
        renderEngine(*engine, left, right, VoragoGhostFix::kReferenceRenderSamples,
                     VoragoGhostFix::kReferenceRenderBlock);
        REQUIRE(left.size() == VoragoGhostFix::kReferenceRenderSamples);

        // SC-010 (a)'s two explicit non-fingerprint assertions, taken AFTER the
        // render: with both config fields inert, 60 s of rendering must leave the
        // trigger lane entirely unentered and the component's reverse
        // probability at the value prepare() forwarded.
        REQUIRE(engine->atmosphere().getTotalTriggeredGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 0.0f);

        const Krate::DSP::TestUtils::RenderFingerprint actual =
            Krate::DSP::TestUtils::fingerprintRender(left);

        // Vacuity guard (B-2): the reference is a sounding render, so a silent
        // actual render is a defect in its own right, never a match.
        REQUIRE(actual.rms > 0.0);

        const auto cmp = Krate::DSP::TestUtils::compareFingerprints(
            actual, VoragoGhostFix::kBaseCommitVoragoFingerprint, kMeasuredMetricTolerance,
            kMeasuredSampleTolerance);

        INFO(cmp.detail);
        INFO("worst metric relative error " << cmp.worstMetricRelativeError << " (bound "
                                            << cmp.metricTolerance << "), worst sample error "
                                            << cmp.worstSampleError << " (bound "
                                            << cmp.sampleTolerance << ")");

        // FR-046's paste-ready literal, emitted whenever the comparison is about
        // to fail. The guard is a purely runtime term, so it cannot fold to a
        // constant conditional (MSVC C4127).
        if (!cmp.withinTolerance()) {
            WARN(
                "SC-010 (a) reference - paste this over kBaseCommitVoragoFingerprint in "
                "atmosphere_ghost_fixtures.h section 6.2 ONLY alongside a DELIBERATE change to "
                "what Fixture B renders, refilling the PROVENANCE block in the SAME edit. "
                "Never to make this clause green:\n\n"
                << baseCommitVoragoFingerprintLiteral(actual));
        }

        REQUIRE(cmp.withinTolerance());

        WARN("SC-010 (a) fingerprint arm ran in " << elapsedSeconds(started) << " s");
    }

    // -------------------------------------------------------------------------
    // (c) - the closed-gate arm, which is literally SC-027 clause 2's
    // -------------------------------------------------------------------------
    // setGhostPeakLevel(0.0f) (vorago_engine.h:814; Phase 10's gated arm at
    // vorago_engine_test.cpp:2793-2795), everything else as in (b). FR-031
    // latches on `ghostPeak_ * ghost`, which is identically zero here, so no
    // grain may be spawned - AND the events still fired, which is what makes the
    // zero the gate closing rather than a silent lane. Without that last
    // assertion an implementation that simply stopped scheduling ghost events
    // would pass.
    SECTION("clause (c) - the closed gate spawns nothing while the events still fire") {
        const auto started = std::chrono::steady_clock::now();

        auto engine = makeWiringEngine(/*eventTriggers=*/true, /*reverseProbability=*/0.0f);
        engine->setGhostPeakLevel(0.0f);
        REQUIRE(engine->getGhostPeakLevel() == 0.0f);

        // The same full-ring pre-roll as (b), so the arm differs from it in the
        // gate and in nothing else.
        REQUIRE(preRollVoragoFullRing(*engine, kGhostBlock) == kGhostCaptureCapacity8k);

        const GhostCounters before = snapshotCounters(engine->atmosphere());

        const auto samples = static_cast<std::size_t>(kEngagedSeconds * kSampleRate8k);
        const GhostTrace closed = traceGhostLane(*engine, samples, kGhostBlock);

        CAPTURE(closed.burstEdges);
        CAPTURE(closed.maxLevel);
        CAPTURE(closed.maxGhostRequest);
        CAPTURE(closed.carriedInBurst);

        // The level held its 0.0 base for the whole render...
        REQUIRE(closed.maxLevel == 0.0f);
        REQUIRE(closed.burstEdges == 0u);

        // ...so nothing was spawned, absolutely and as a delta.
        REQUIRE(engine->atmosphere().getTotalTriggeredGrainsBorn() == std::uint64_t{0});
        REQUIRE(engine->atmosphere().getTotalTriggeredGrainsBorn() == before.triggeredBorn);

        // ...and the events DID fire, so the two zeroes above are the gate
        // closing rather than a lane that went quiet (vorago_engine_test.cpp:2828).
        REQUIRE(closed.maxGhostRequest >= kGhostRiseThreshold);

        WARN("SC-010 (c) closed-gate arm: " << kEngagedSeconds << " s at " << kSampleRate8k
                                            << " Hz ran in " << elapsedSeconds(started)
                                            << " s; max ghost request " << closed.maxGhostRequest);
    }

    // -------------------------------------------------------------------------
    // (e) - the reverse-probability wire (spec §8 ruling 11)
    // -------------------------------------------------------------------------
    // WHY THIS ARM EXISTS AT ALL. No other clause in the phase moves
    // atmosGhostReverseProbability off its default - (a) asserts the component
    // reads 0.0f at config defaults, and (b)-(d) engage atmosGhostEventTriggers
    // alone - so an implementation that simply OMITTED
    // `atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability)` would
    // pass every other criterion, because the component's own default is already
    // 0.0f (atmosphere_engine.h:958). This arm is the only thing standing between
    // that omission and a green phase.
    //
    // At probability 1 the only forward outcome is the single exact draw 1.0f, an
    // expected rate of 2^-32, so the equality below is the right assertion and
    // not an approximation. A ZERO BIRTH COUNT IS A FIXTURE DEFECT TO FIX, never
    // a reason to weaken the arm - which is why getTotalGrainsBorn() > 0 is
    // asserted first and separately.
    SECTION("clause (e) - atmosGhostReverseProbability reaches the component") {
        const auto started = std::chrono::steady_clock::now();

        auto engine = makeWiringEngine(/*eventTriggers=*/false, /*reverseProbability=*/1.0f);

        // FR-034's forwarding, observed immediately after prepare() and before a
        // single sample is rendered.
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 1.0f);

        // FR-050 rejects reverse births while the ring is still FILLING, so the
        // pre-roll is what makes "births happen" true rather than hopeful.
        REQUIRE(preRollVoragoFullRing(*engine, kGhostBlock) == kGhostCaptureCapacity8k);

        const auto samples = static_cast<std::size_t>(kReverseSeconds * kSampleRate8k);
        std::vector<float> l;
        std::vector<float> r;
        renderEngine(*engine, l, r, samples, kGhostBlock);

        const std::uint64_t born = engine->atmosphere().getTotalGrainsBorn();
        const std::uint64_t reverseBorn = engine->atmosphere().getTotalReverseGrainsBorn();
        CAPTURE(born);
        CAPTURE(reverseBorn);

        // The event-trigger lane is OFF on this arm, so every one of those births
        // is the density scheduler's - stated so the arm cannot be read as
        // measuring the trigger path.
        REQUIRE(engine->atmosphere().getTotalTriggeredGrainsBorn() == std::uint64_t{0});

        REQUIRE(born > std::uint64_t{0});
        REQUIRE(reverseBorn == born);

        WARN("SC-010 (e) reverse-wire arm: " << born << " grains born, all reverse; ran in "
                                             << elapsedSeconds(started) << " s");
    }
}

// =============================================================================
// SC-010 (b) and (d) - the engaged arm
// =============================================================================
// Tagged `[long]` per decision D-4 / FR-048: a 32.768 s pre-roll plus 600 s of
// instrument time at 8 kHz with grains actually rendering. The assertion is
// toolchain-INDEPENDENT (a trigger count against an edge count), which is the
// other half of FR-048's test for the tag. If the measured runtime reported
// below comes in under ~15 s, fold this case back into the untagged one.
//
// HONESTY ABOUT THE EVIDENCE, stated because the spec states it: FR-031 latches
// on `ghostPeak_ * ghost`, which is exactly the value setLevel stores and
// getLevel() returns, so the detector and the production latch see THE SAME
// SIGNAL by construction. There is no independent public observable of the
// scheduler's own event edges - SlowEventScheduler::isEventActive()
// (slow_event_scheduler.h:361) lives on `sched_`, private to VoragoVoice and
// unreachable from VoragoEngine. The teeth of this case are therefore the band
// and the rejection counters, not a second signal source:
//
//   * a level-POLLING implementation that triggered every control step:
//         delta >> edges                          -> the upper bound fails
//   * a missing or mis-gated wire:
//         delta == 0                              -> the >= 6 floor fails
//   * a per-VOICE rather than per-fold latch:
//         delta > edges + 1                       -> the upper bound fails
//   * a trigger consumed but never turned into a grain:
//         delta < edges, with a skip counter moving -> lower bound AND the
//                                                    zero-rejection assertions
//   * a latch that never re-arms:
//         delta == 1                              -> the >= 6 floor fails
//
// The rejection assertions are what stop the band being satisfied CHEAPLY:
// without them an implementation could fire a trigger per control step and have
// almost all of them rejected pool-full, landing inside the band by accident.
// FR-048 TAG DECISION FROM THE MEASUREMENT: **9.192 s** (2026-09-24, Release,
// MSVC 19.44, `-d yes`, nothing else running) against FR-048s ~15 s bar, so the
// estimate-era [long] tag comes off. That matters beyond tidiness: SC-010 (b)
// and (d) are the ONLY criteria in this phase that observe a rising edge
// actually spawning a grain, and while they were tagged they never ran in any
// gate - every gate command in this phase excludes [long].
TEST_CASE("VoragoEngine_GhostExtensionWiring_Engaged", "[vorago][ghost]") {
    const auto started = std::chrono::steady_clock::now();

    auto engine = makeWiringEngine(/*eventTriggers=*/true, /*reverseProbability=*/0.0f);
    REQUIRE(engine->getGhostPeakLevel() > 0.0f);  // the gate is OPEN on this arm

    // The full-ring pre-roll (FR-050 is vacuous after it), stated in SAMPLES.
    const auto preRolled = preRollVoragoFullRing(*engine, kGhostBlock);
    REQUIRE(preRolled == kGhostCaptureCapacity8k);
    const double preRollSeconds = static_cast<double>(preRolled) / kSampleRate8k;

    // Snapshot AFTER the pre-roll: bursts fire during it and their triggers must
    // land outside the measured delta. traceGhostLane's carry-in rule is the
    // matching half of this decision - see its comment.
    const GhostCounters before = snapshotCounters(engine->atmosphere());

    const auto samples = static_cast<std::size_t>(kEngagedSeconds * kSampleRate8k);
    const GhostTrace open = traceGhostLane(*engine, samples, kGhostBlock);

    const GhostCounters after = snapshotCounters(engine->atmosphere());
    const std::uint64_t deltaTriggered = after.triggeredBorn - before.triggeredBorn;
    const std::uint64_t deltaDropped = after.dropped - before.dropped;
    const std::uint64_t deltaPoolFull = after.skipPoolFull - before.skipPoolFull;
    const std::uint64_t deltaRingCold = after.skipRingCold - before.skipRingCold;
    const auto edges = static_cast<std::uint64_t>(open.burstEdges);

    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(4);
        os << "SC-010 (b)/(d): pre-roll " << preRolled << " samples (" << std::setprecision(3)
           << preRollSeconds << " s), then " << std::setprecision(1) << kEngagedSeconds << " s at "
           << kSampleRate8k << " Hz in " << kGhostBlock
           << "-sample blocks, polyphony 1, ecosystem depth 0, event rate scale "
           << kGhostEventRateScale << "; rise >= " << std::setprecision(4) << kGhostRiseThreshold
           << ", fall <= " << kGhostFallThreshold << "\n  burst edges     : " << open.burstEdges
           << (open.carriedInBurst ? " (one carry-in excursion skipped)" : "")
           << "\n  d triggeredBorn : " << deltaTriggered << "\n  d dropped       : " << deltaDropped
           << "\n  d skipPoolFull  : " << deltaPoolFull
           << "\n  d skipRingCold  : " << deltaRingCold << "\n  max level       : " << open.maxLevel
           << "\n  max ghost req   : " << open.maxGhostRequest
           << "\n  wall clock      : " << std::setprecision(2) << elapsedSeconds(started) << " s";
        WARN(os.str());
    }

    CAPTURE(open.burstEdges);
    CAPTURE(open.carriedInBurst);
    CAPTURE(open.maxLevel);
    CAPTURE(open.maxGhostRequest);
    CAPTURE(deltaTriggered);
    CAPTURE(deltaDropped);
    CAPTURE(deltaPoolFull);
    CAPTURE(deltaRingCold);

    // --- (b) the band. The detector counts COMPLETED excursions while FR-031
    // fires on the RISE, so a burst still in flight at the end of the window has
    // triggered but is not yet counted - that, and nothing else, is the `+ 1`.
    REQUIRE(edges <= deltaTriggered);
    REQUIRE(deltaTriggered <= edges + std::uint64_t{1});

    // --- (b) the band cannot be satisfied by rejections.
    REQUIRE(deltaDropped == std::uint64_t{0});
    REQUIRE(deltaPoolFull == std::uint64_t{0});
    REQUIRE(deltaRingCold == std::uint64_t{0});

    // --- (b) SC-027's own floor, measured by Phase 10 on this fixture
    // (vorago_engine_test.cpp:2819). Below it the band above is satisfiable by a
    // lane that barely fires.
    REQUIRE(deltaTriggered >= std::uint64_t{6});

    // --- (d) the spawn path did not REPLACE the level gate (FR-033): the same
    // detector still sees Phase 10's burst population on the same signal.
    REQUIRE(edges >= std::uint64_t{6});
}
