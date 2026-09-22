// ==============================================================================
// Layer 3: System Tests - VoragoVoice, the [long] set
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T018 fills it)
//
// SCOPE OF THIS TU: SC-018, SC-019 and SC-020 - the multi-minute voice renders
//   whose assertions are toolchain-INDEPENDENT. They carry the [long] tag, so
//   the per-push lane excludes them and the nightly workflow runs them on all
//   three operating systems. The bounded, per-push halves of those criteria
//   (SC-017a, SC-018a, SC-019a, SC-019b, SC-020a) live in
//   unit/systems/vorago_voice_test.cpp and must NEVER be tagged [long].
//
// ALLOCATION DETECTION: this TU includes neither <allocation_detector.h> nor
//   <allocation_operator_overrides.h>. The single owner of the global
//   operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error.
//
// PORTABILITY: no std::isnan / std::isinf / std::isfinite anywhere in this TU,
//   so it stays correct under -ffast-math. Finiteness goes through
//   Krate::DSP::detail::isFinite (core/db_utils.h:118), which
//   tools/lint-nonfinite-symbols.js requires.
//
// ------------------------------------------------------------------------------
// FR-086: WHAT "ACCELERATED" MEANS HERE, AND THE FACTOR A
// ------------------------------------------------------------------------------
// FR-086 (spec.md:1288-1310) defines acceleration ONCE: the ONLY permitted
// change is SCALING THE EVENT AND LIFECYCLE CLOCKS by one stated factor `A`;
// everything in the audio path renders every sample at the shipped rate. SC-018,
// SC-019 and SC-020 are event-and-accounting properties and are on FR-086's
// explicit list of criteria that MAY use it.
//
//   A = 10.
//
// A IS NOT A FREE CHOICE: it is the ceiling of VoragoVoice::setEventRateScale
// (vorago_voice.h:1186-1189 clamps to [0.1, 10]), and that setter is the ONLY
// path to the two scheduler interval ranges, because publishIdentity() re-writes
// them from eventRateScale_ on EVERY control step (applyEventRateScale,
// vorago_voice.h:1673-1684). A direct SlowEventScheduler::setIntervalRange from
// the test would be overwritten 750 times a second.
//
// SCALED BY A (each one on FR-086's permitted list):
//   * both SlowEventScheduler interval ranges - via setEventRateScale(10), so
//     the fast scheduler runs 2-9 s and the slow one 18-60 s. accelerate() also
//     writes those two ranges straight onto the schedulers and resets them, so
//     the FR-067 pre-roll is drawn from the ACCELERATED range too; see the
//     comment on that loop for why leaving it to the first control step would
//     put the first onset past the end of the render;
//   * the voice envelope's stage times and its release (FR-014's shipped
//     20/30/45/60 s walk becomes 2/3/4.5/6 s);
//   * GrowthEnvelope::setDuration (120 s -> 12 s; Standard mode never consumes
//     it, scaled for completeness);
//   * BloomEngine's lifecycle durations (45/120/180 s -> 4.5/12/18 s) and its
//     spawn rate (kDefaultSpawnRateHz x 10 = 1/24 Hz, inside kMaxSpawnRateHz);
//   * BreathingModulator::setRate (0.017 Hz -> 0.17 Hz, inside kMaxRate = 0.5).
//
// THE TWO CLOCKS THAT CANNOT TAKE THE FULL FACTOR, STATED RATHER THAN HIDDEN:
//   * EcosystemEngine's step interval. The voice ships stepIntervalChunks = 8
//     (vorago_voice.h:205) and 8 IS kMinStepIntervalChunks
//     (ecosystem_engine.h:192,196) - the shipped value is already the floor, so
//     this clock runs UNACCELERATED. That is conservative for every criterion
//     here: an unaccelerated 30-minute render would take 10x MORE simulation
//     steps than this one, so every "changes per equivalent minute" figure
//     measured below is a LOWER bound on the shipped instrument's.
//   * TidalModulator::setRate takes a NORMALISED rate in [0, 1]
//     (tidal_modulator.h:202-205), not Hz; the shipped 0.25 x 10 clamps to 1.0,
//     i.e. 4x. Nothing asserted in this TU reads the tidal lane - it is
//     published as getTidalFogDepth() for the ENGINE to fold onto its own smear
//     base (FR-026 lane 2) and reaches no destination measured here.
//
// NOT CHANGED, BECAUSE FR-086 FORBIDS IT: the sample rate (48 kHz throughout),
// kControlChunkSamples (64), the block size (every render call below is exactly
// one control chunk), the polyphony (one voice), and every gain, mix, damping
// and feedback value.
//
// The wall-clock cost of each case is MEASURED and printed by the case itself,
// which is the figure compliance.md records (FR-086's last clause).
//
// ------------------------------------------------------------------------------
// WHY THE ASSERTIONS ARE ACCUMULATED RATHER THAN ONE REQUIRE PER STEP
// ------------------------------------------------------------------------------
// These renders run 45 000 - 135 000 control steps. EVERY step is checked - that
// is what SC-018 "asserted every control step" and SC-019's "over the render"
// demand - but a violation is LATCHED with its step index and its numbers and
// reported once, instead of paying Catch2's per-assertion bookkeeping a hundred
// thousand times over. A failure names the first offending control step, which
// is strictly more useful than the hundred-thousandth.
// ==============================================================================

#include <krate/dsp/systems/vorago_voice.h>

#include <krate/dsp/processors/breathing_modulator.h>
#include <krate/dsp/processors/slow_event_scheduler.h>
#include <krate/dsp/processors/tidal_modulator.h>
#include <krate/dsp/systems/bloom_engine.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/feedback_ecology.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/noise_organism.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace {

using Krate::DSP::BloomEngine;
using Krate::DSP::BreathingModulator;
using Krate::DSP::EcosystemEngine;
using Krate::DSP::FeedbackEcology;
using Krate::DSP::HarmonicCloud;
using Krate::DSP::NoiseOrganism;
using Krate::DSP::ResonanceDriftNetwork;
using Krate::DSP::SlowEventScheduler;
using Krate::DSP::TidalModulator;
using Krate::DSP::VoragoVoice;
using Krate::DSP::VoragoVoiceConfig;

using Kind = EcosystemEngine::Kind;
using EventFamily = VoragoVoice::EventFamily;

// -----------------------------------------------------------------------------
// The render clock
// -----------------------------------------------------------------------------

constexpr double kSampleRate = 48000.0;

/// FR-086's factor, stated once. See the banner for why it is exactly 10.
constexpr float kAccelerationFactor = 10.0f;

constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
static_assert(kChunk == 64u, "FR-007: the shared control grid");

/// 48 000 / 64. One processStereoBlock(64) call is EXACTLY one control chunk
/// (vorago_voice.h:799-828: the carry FIFO renders a whole chunk the moment its
/// first sample is requested and never a partial one).
constexpr std::uint64_t kChunksPerAudioSecond = 750u;
static_assert(kChunksPerAudioSecond * kChunk == 48000u, "one audio second in chunks");

/// Control steps needed to cover @p equivalentSeconds of the SHIPPED instrument's
/// event timeline, at acceleration A.
[[nodiscard]] constexpr std::uint64_t chunksForEquivalentSeconds(double equivalentSeconds) noexcept {
    const double audioSeconds = equivalentSeconds / static_cast<double>(kAccelerationFactor);
    return static_cast<std::uint64_t>(audioSeconds * static_cast<double>(kChunksPerAudioSecond));
}

[[nodiscard]] constexpr std::uint64_t chunksForAudioSeconds(double audioSeconds) noexcept {
    return static_cast<std::uint64_t>(audioSeconds * static_cast<double>(kChunksPerAudioSecond));
}

// -----------------------------------------------------------------------------
// The shipped bases the FR-021 destinations sit on (vorago_voice.h prepare())
// -----------------------------------------------------------------------------
// noiseWakeBase_ has a public setter and getter (setNoiseWakeBase / getNoiseWakeBase,
// :1098-1101), so its base is read from the VOICE and cannot drift from this TU.
// peakWakeBase_ and loopWakeBase_ have NEITHER - they are filled at prepare()
// (vorago_voice.h:578 and :591) and are not on the macro surface - so their
// shipped values are reproduced here. A drift between prepare() and these two
// literals turns SC-019 clause 1 red, which is the correct outcome: clause 1
// asserts the destinations read EXACTLY their configured bases, and a base
// nobody can name is not a configured base.

constexpr float kShippedPeakWakeBase = 0.50f;  ///< vorago_voice.h:578
constexpr float kShippedLoopWakeBase = 0.50f;  ///< vorago_voice.h:591

// -----------------------------------------------------------------------------
// Test-side doors through the voice's const sub-component accessors
// -----------------------------------------------------------------------------
// The accessors are const BY DESIGN - the voice owns every write path onto its
// components. What this TU needs on the far side of that const is (a) the bloom's
// lifecycle durations, (b) the two life-modulator rates and (c) the scheduler
// depth ranges, all of which FR-086 lists as legal acceleration knobs but none of
// which is on the voice's own macro surface, plus (d) EcosystemEngine::setAgentDormant
// for SC-019 clause 3's dormancy arm. The referenced components are non-const
// members of a non-const heap-allocated VoragoVoice, so removing const is well
// defined - the same idiom, and the same justification, as
// vorago_voice_test.cpp:1219-1231.

BloomEngine& mutableBloom(VoragoVoice& v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<BloomEngine&>(v.bloom());
}

EcosystemEngine& mutableEcosystem(VoragoVoice& v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<EcosystemEngine&>(v.ecosystem());
}

SlowEventScheduler& mutableScheduler(VoragoVoice& v, std::size_t k) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<SlowEventScheduler&>(v.scheduler(k));
}

BreathingModulator& mutableBreathing(VoragoVoice& v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<BreathingModulator&>(v.breathing());
}

TidalModulator& mutableTide(VoragoVoice& v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<TidalModulator&>(v.tide());
}

// -----------------------------------------------------------------------------
// FR-086's acceleration, applied in one place
// -----------------------------------------------------------------------------

void accelerate(VoragoVoice& v) {
    // The scheduler interval ranges, through the ONE owner of both of them.
    v.setEventRateScale(kAccelerationFactor);

    // ...and the same two ranges written STRAIGHT ONTO the schedulers, followed
    // by a reset. WHY BOTH, and why this is not a second configuration:
    // setEventRateScale only stores the scale (vorago_voice.h:1186-1189); the
    // ranges themselves are written by applyEventRateScale() inside
    // publishIdentity(), i.e. at the FIRST CONTROL STEP - which is AFTER
    // prepare()'s reset() has already had initState() draw the FR-067 PRE-ROLL
    // (slow_event_scheduler.h:540-556) from the UNACCELERATED range. Left alone,
    // the fast scheduler's first onset lands 20-90 s into the render and the slow
    // one's 180-600 s in - past the end of an accelerated render, which would
    // make every event clause here vacuous or red for the wrong reason. The
    // numbers written below are EXACTLY the ones applyEventRateScale() writes on
    // every subsequent control step (vorago_voice.h:1673-1684), so this is the
    // shipped configuration arriving one control step early; reset() then
    // re-draws the pre-roll from it and re-seeds the RNG from the voice-derived
    // seed (slow_event_scheduler.h:213-214), so determinism is untouched.
    for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
        SlowEventScheduler& s = mutableScheduler(v, k);
        if (k == 0u) {
            s.setIntervalRange(VoragoVoice::kFastEventIntervalMinSeconds / kAccelerationFactor,
                               VoragoVoice::kFastEventIntervalMaxSeconds / kAccelerationFactor);
        } else {
            s.setIntervalRange(VoragoVoice::kSlowEventIntervalMinSeconds / kAccelerationFactor,
                               VoragoVoice::kSlowEventIntervalMaxSeconds / kAccelerationFactor);
        }
        s.reset();
    }

    // The voice envelope's stage times and release (FR-086's explicit list).
    for (int st = 0; st < VoragoVoice::kEnvelopeStages; ++st) {
        const auto i = static_cast<std::size_t>(st);
        v.setEnvelopeStageTimeMs(st, VoragoVoice::kDefaultStageTimesMs[i] / kAccelerationFactor);
    }
    v.setEnvelopeReleaseMs(VoragoVoice::kDefaultReleaseMs / kAccelerationFactor);
    v.setGrowthDurationSeconds(VoragoVoice::kDefaultGrowthDurationSeconds / kAccelerationFactor);

    // The bloom's lifecycle. The three shipped figures are vorago_voice.h:604-606;
    // divided by 10 they are 4.5 / 12 / 18 s, all inside the component's own
    // [kMinFadeInSeconds, kMaxFadeInSeconds] style clamps (bloom_engine.h:273-275).
    BloomEngine& b = mutableBloom(v);
    b.setFadeInSeconds(45.0f / kAccelerationFactor);
    b.setHoldSeconds(120.0f / kAccelerationFactor);
    b.setFadeOutSeconds(180.0f / kAccelerationFactor);
    // kDefaultSpawnRateHz = 1/240; x10 = 1/24 Hz <= kMaxSpawnRateHz (0.05).
    v.setBloomSpawnRateHz(BloomEngine::kDefaultSpawnRateHz * kAccelerationFactor);

    // The two life modulators. Breathing takes Hz and accepts the full factor;
    // the tidal rate is NORMALISED and saturates at 1.0 (see the banner).
    mutableBreathing(v).setRate(0.017f * kAccelerationFactor);
    mutableTide(v).setRate(std::min(1.0f, 0.25f * kAccelerationFactor));
}

[[nodiscard]] std::unique_ptr<VoragoVoice> makeAcceleratedVoice(std::uint32_t seed) {
    // NEVER a test local: sizeof(VoragoVoice) is ~120 kB (vorago_voice.h:424).
    auto v = std::make_unique<VoragoVoice>();
    v->setSeed(seed);
    v->prepare(kSampleRate, VoragoVoiceConfig{});
    accelerate(*v);
    return v;
}

// -----------------------------------------------------------------------------
// Rendering, one control chunk at a time
// -----------------------------------------------------------------------------

struct ChunkBuffer {
    std::array<float, kChunk> l{};
    std::array<float, kChunk> r{};
};

/// Render EXACTLY one control chunk and report whether every sample is finite.
[[nodiscard]] bool renderChunk(VoragoVoice& v, ChunkBuffer& buf) {
    v.processStereoBlock(buf.l.data(), buf.r.data(), kChunk);
    for (std::size_t s = 0; s < kChunk; ++s) {
        if (!Krate::DSP::detail::isFinite(buf.l[s]) || !Krate::DSP::detail::isFinite(buf.r[s])) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// The five destination families, read through the getters SC-019 names
// -----------------------------------------------------------------------------
// Indexed by EcosystemEngine::Kind rather than by VoragoVoice::EventFamily: the
// two rosters are in DIFFERENT orders on purpose (vorago_voice.h:248-261) and
// Kind is the one the dormancy arm addresses. kindForFamily() is the voice's own
// mapping and familyKind() below reproduces it.

struct KindReading {
    std::array<float, VoragoVoice::kMaxSlotsPerKind> v{};
    std::size_t count = 0;
    bool operator==(const KindReading&) const = default;
};

/// The routed value at every destination of @p k, read through the FR-071
/// surface SC-019 (1) enumerates:
///   Partial   -> HarmonicCloud::getMutation (harmonic_cloud.h:493) and
///                BloomEngine::getDepth      (bloom_engine.h:679)   -- A-9
///   Resonator -> ResonanceDriftNetwork::getPeakWakeAmount   (:855)
///   Noise     -> NoiseOrganism::getSourceWakeAmount         (:880)
///   Feedback  -> FeedbackEcology::getLoopWakeAmount         (:1425)
///   Ghost     -> VoragoVoice::getGhostRequest (FR-020b; measured at polyphony 1,
///                where the voice's own request IS the observable - the
///                AtmosphereEngine is engine-owned, OQ-1 ruling (b))
[[nodiscard]] KindReading readKind(const VoragoVoice& v, Kind k) {
    KindReading out;
    switch (k) {
        case Kind::Partial:
            out.v[0] = v.cloud().getMutation();
            out.v[1] = v.bloom().getDepth();
            out.count = 2u;
            break;
        case Kind::Resonator: {
            const std::size_t n =
                std::min(v.resonance().getNumPeaks(), ResonanceDriftNetwork::kMaxPeaks);
            for (std::size_t i = 0; i < n; ++i) {
                out.v[i] = v.resonance().getPeakWakeAmount(i);
            }
            out.count = n;
            break;
        }
        case Kind::Noise: {
            const std::size_t n = std::min(v.noise().getNumSources(), NoiseOrganism::kMaxSources);
            for (std::size_t i = 0; i < n; ++i) {
                out.v[i] = v.noise().getSourceWakeAmount(i);
            }
            out.count = n;
            break;
        }
        case Kind::Feedback: {
            const std::size_t n = std::min(v.ecology().getNumLoops(), FeedbackEcology::kMaxLoops);
            for (std::size_t i = 0; i < n; ++i) {
                out.v[i] = v.ecology().getLoopWakeAmount(i);
            }
            out.count = n;
            break;
        }
        case Kind::Ghost:
            out.v[0] = v.getGhostRequest();
            out.count = 1u;
            break;
    }
    return out;
}

/// The configured base every destination of @p k sits on with both contributions
/// at zero - FR-021, read back through the voice wherever the voice owns it.
[[nodiscard]] KindReading baseForKind(const VoragoVoice& v, Kind k) {
    KindReading out;
    switch (k) {
        case Kind::Partial:
            // VoragoVoice::getMutation()/getBloomDepth() report mutationBase_ and
            // bloomDepthBase_ (vorago_voice.h:1072, :1197) - the bases themselves,
            // not the components' applied values, which is exactly the comparand.
            out.v[0] = v.getMutation();
            out.v[1] = v.getBloomDepth();
            out.count = 2u;
            break;
        case Kind::Resonator: {
            const std::size_t n =
                std::min(v.resonance().getNumPeaks(), ResonanceDriftNetwork::kMaxPeaks);
            for (std::size_t i = 0; i < n; ++i) {
                out.v[i] = kShippedPeakWakeBase;
            }
            out.count = n;
            break;
        }
        case Kind::Noise: {
            const std::size_t n = std::min(v.noise().getNumSources(), NoiseOrganism::kMaxSources);
            for (std::size_t i = 0; i < n; ++i) {
                out.v[i] = v.getNoiseWakeBase();
            }
            out.count = n;
            break;
        }
        case Kind::Feedback: {
            const std::size_t n = std::min(v.ecology().getNumLoops(), FeedbackEcology::kMaxLoops);
            for (std::size_t i = 0; i < n; ++i) {
                out.v[i] = kShippedLoopWakeBase;
            }
            out.count = n;
            break;
        }
        case Kind::Ghost:
            // FR-020b: the ghost request has NO base. It IS the combine.
            out.v[0] = 0.0f;
            out.count = 1u;
            break;
    }
    return out;
}

[[nodiscard]] const char* kindName(Kind k) {
    switch (k) {
        case Kind::Partial: return "Partial";
        case Kind::Resonator: return "Resonator";
        case Kind::Noise: return "Noise";
        case Kind::Feedback: return "Feedback";
        case Kind::Ghost: return "Ghost";
    }
    return "?";
}

/// VoragoVoice::kindForFamily (vorago_voice.h:1405-1413), reproduced on the test
/// side. The two rosters are in different orders and an implicit cast would
/// quietly route a peak wake into the noise organism.
[[nodiscard]] Kind familyKind(std::uint8_t family) {
    switch (static_cast<EventFamily>(family)) {
        case EventFamily::BloomTrigger: return Kind::Partial;
        case EventFamily::NoiseWake: return Kind::Noise;
        case EventFamily::PeakWake: return Kind::Resonator;
        case EventFamily::LoopWake: return Kind::Feedback;
        case EventFamily::GhostBurst: return Kind::Ghost;
    }
    return Kind::Partial;
}

[[nodiscard]] float maxOf(const KindReading& r) {
    float m = 0.0f;
    for (std::size_t i = 0; i < r.count; ++i) {
        m = std::max(m, r.v[i]);
    }
    return m;
}

/// Mark every agent of kind @p k dormant (or wake them). Dormancy gates the
/// agent's published OUTPUT and leaves the simulation running
/// (ecosystem_engine.h:759-786), which is precisely what SC-019 clause 3's
/// attribution arm needs: the ecosystem contribution goes static while every
/// other contribution carries on untouched.
void setKindDormant(VoragoVoice& v, Kind k, bool dormant) {
    EcosystemEngine& e = mutableEcosystem(v);
    const std::size_t agents = e.getAgentCount();
    for (std::size_t i = 0; i < agents; ++i) {
        if (e.getAgentKind(i) == k) {
            e.setAgentDormant(i, dormant);
        }
    }
}

/// A latched first violation: the step it happened at and what was read.
struct FirstViolation {
    bool hit = false;
    std::uint64_t step = 0;
    std::string what;

    void latch(std::uint64_t s, const std::string& message) {
        if (!hit) {
            hit = true;
            step = s;
            what = message;
        }
    }
};

[[nodiscard]] double wallSecondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

// =============================================================================
// SC-018 - bloom slot accounting never exceeds cloud capacity
// =============================================================================
// spec.md:1666-1671, plan.md:1139-1143, tasks.md T018.
//
// WHAT IS BEING BOUNDED. updateSpectrumTarget() (vorago_voice.h:1766-1812) hands
// HarmonicCloud::setSpectralTarget the value BloomEngine::processChunk returned.
// That value is one of exactly two things (bloom_engine.h:520 and :1534-1584):
//   * `capacity()` on an ENGAGED call (the return at :1584), or
//   * the caller's own `parentCount` on a disengaged one (the early return at
//     :1537) - and the voice passes `bloom_.reserveBase()` as parentCount
//     (vorago_voice.h:1780, B-1 / Q-D).
// Both are therefore bounded by `capacity()`, and `capacity()` is what this case
// checks against HarmonicCloud::kMaxPartials at every control step, together
// with reserveBase() >= kMinParentSlots - B-1's floor, the one that stops the
// bloom deleting the parent spectrum.
//
// WHY THE RICHNESS SWEEP. capacity = clamp(getActivePartialCount(), 14, 64) and
// N(r) = round(64^r) (harmonic_cloud.h:1459-1463), so at a FIXED richness the
// capacity never moves and both bounds are vacuous - the render would assert two
// constants 135 000 times. The sweep walks r over [0, 1] and back, which drives
// the accounting onto BOTH bounds: N(0) = 1 puts capacity on the
// kMinCloudCapacity floor (reserveBase exactly kMinParentSlots) and N(1) = 64
// puts it on kMaxPartials. The sweep is part of the system under test, NOT part
// of the acceleration - FR-086 governs the clock scaling and says nothing about
// which configurations a render may visit.
// =============================================================================

TEST_CASE("VoragoVoice_BloomSlotAccounting", "[systems][vorago][long]") {
    constexpr double kEquivalentSeconds = 1800.0;  // 30 minutes of shipped timeline
    const std::uint64_t totalChunks = chunksForEquivalentSeconds(kEquivalentSeconds);
    REQUIRE(totalChunks == 135000u);  // 180 s of audio at A = 10

    // One richness value per audio second; 20 steps up, 20 down, so r hits
    // exactly 0.0 and exactly 1.0 on every sweep.
    constexpr std::uint64_t kRichnessHoldChunks = kChunksPerAudioSecond;
    constexpr std::uint64_t kRichnessStepsPerRamp = 20u;

    auto v = makeAcceleratedVoice(0x5C018u);
    v->noteOn(55.0f, 1.0f);

    ChunkBuffer buf;
    FirstViolation violation;
    bool allFinite = true;
    bool sawTarget = false;
    bool sawNoTarget = false;
    std::size_t minCapacity = HarmonicCloud::kMaxPartials;
    std::size_t maxCapacity = 0;
    std::size_t minReserve = HarmonicCloud::kMaxPartials;
    std::size_t maxLiveChildren = 0;
    std::uint64_t richnessStep = std::numeric_limits<std::uint64_t>::max();

    const auto t0 = std::chrono::steady_clock::now();
    for (std::uint64_t c = 0; c < totalChunks; ++c) {
        const std::uint64_t step = c / kRichnessHoldChunks;
        if (step != richnessStep) {
            richnessStep = step;
            const std::uint64_t phase = step % (2u * kRichnessStepsPerRamp);
            const std::uint64_t up =
                (phase <= kRichnessStepsPerRamp) ? phase : (2u * kRichnessStepsPerRamp - phase);
            v->setRichness(static_cast<float>(up) / static_cast<float>(kRichnessStepsPerRamp));
        }

        allFinite = renderChunk(*v, buf) && allFinite;

        const std::size_t capacity = v->bloom().capacity();
        const std::size_t reserve = v->bloom().reserveBase();
        const std::size_t live = v->bloom().getLiveChildCount();
        const bool wantTarget = (live > 0u);
        const bool hasTarget = v->cloud().hasSpectralTarget();

        minCapacity = std::min(minCapacity, capacity);
        maxCapacity = std::max(maxCapacity, capacity);
        minReserve = std::min(minReserve, reserve);
        maxLiveChildren = std::max(maxLiveChildren, live);
        sawTarget = sawTarget || hasTarget;
        sawNoTarget = sawNoTarget || !hasTarget;

        // (1) The count handed to setSpectralTarget is bounded by capacity(),
        //     and capacity() never exceeds the cloud's slot count.
        if (capacity > HarmonicCloud::kMaxPartials || reserve > capacity) {
            std::ostringstream os;
            os << "capacity " << capacity << ", reserveBase " << reserve << ", kMaxPartials "
               << HarmonicCloud::kMaxPartials;
            violation.latch(c, os.str());
        }
        // (2) B-1's floor. "reserveBase() never goes negative" is a std::size_t
        //     subtraction (bloom_engine.h:710) - it would WRAP, not go negative -
        //     so the clause with teeth is the parent region the voice reserves.
        if (reserve < VoragoVoice::kMinParentSlots) {
            std::ostringstream os;
            os << "reserveBase " << reserve << " < kMinParentSlots "
               << VoragoVoice::kMinParentSlots << " (capacity " << capacity << ")";
            violation.latch(c, os.str());
        }
        // (3) setSpectralTarget is never REJECTED, asserted POSITIVELY: the voice
        //     sets a target iff a child is live and clears it otherwise
        //     (vorago_voice.h:1804-1811), so hasSpectralTarget() must track that
        //     expectation exactly. A rejected call (count == 0, count > 64, a
        //     non-finite or non-positive ratio - harmonic_cloud.h:770-774) leaves
        //     hasTarget_ where it was and this comparison catches it.
        if (hasTarget != wantTarget) {
            std::ostringstream os;
            os << "hasSpectralTarget " << hasTarget << " but liveChildCount " << live
               << " (capacity " << capacity << ", reserveBase " << reserve << ")";
            violation.latch(c, os.str());
        }
    }
    const double wall = wallSecondsSince(t0);

    WARN("SC-018 bloom accounting: A = " << kAccelerationFactor << ", "
         << (kEquivalentSeconds / 60.0) << " equivalent minutes, " << totalChunks
         << " control steps (" << (static_cast<double>(totalChunks) / kChunksPerAudioSecond)
         << " s of audio), wall clock " << wall << " s; capacity " << minCapacity << ".."
         << maxCapacity << ", min reserveBase " << minReserve << ", max live children "
         << maxLiveChildren);

    INFO("first violation at control step " << violation.step << ": " << violation.what);
    REQUIRE_FALSE(violation.hit);
    REQUIRE(allFinite);

    // NON-VACUITY. Without these the three clauses above could have been checked
    // against one unchanging state for the whole render.
    REQUIRE(minCapacity == VoragoVoice::kMinCloudCapacity);   // the B-1 floor was reached
    REQUIRE(maxCapacity == HarmonicCloud::kMaxPartials);      // ...and so was the ceiling
    REQUIRE(minReserve == VoragoVoice::kMinParentSlots);      // the floor clause is not slack
    REQUIRE(sawTarget);                                       // the spawn state was visited
    REQUIRE(sawNoTarget);                                     // ...and so was the cleared one
}

// =============================================================================
// SC-019 - ecosystem routing is observable
// =============================================================================
// spec.md:1678-1716, plan.md:2501, tasks.md T018. Three clauses, all at
// polyphony 1 (one voice, so getGhostRequest() is directly observable).
//
// NO Catch2 SECTIONs. A SECTION re-runs the whole TEST_CASE body from the top,
// which would re-render every arm once per section.
// =============================================================================

TEST_CASE("VoragoVoice_EcosystemRouting", "[systems][vorago][long]") {
    constexpr double kEquivalentSeconds = 600.0;  // 10 minutes of shipped timeline
    const std::uint64_t totalChunks = chunksForEquivalentSeconds(kEquivalentSeconds);
    REQUIRE(totalChunks == 45000u);  // 60 s of audio at A = 10

    constexpr std::array<Kind, EcosystemEngine::kNumKinds> kKinds{
        Kind::Partial, Kind::Resonator, Kind::Noise, Kind::Feedback, Kind::Ghost};

    const auto t0 = std::chrono::steady_clock::now();

    // -------------------------------------------------------------------------
    // CLAUSE 1 - the unmodulated baseline
    // -------------------------------------------------------------------------
    // Ecosystem depth 0 AND every scheduler depth 0. The scheduler precondition
    // is REQUIRED, not decorative: FR-022 routes the schedulers to four of the
    // same five destination families and FR-023 combines by maximum, so ecosystem
    // depth 0 ALONE does not stop the destinations moving.
    //
    // setDepthRange(0, 0) is the depth-0 state: drawCycle() draws the event depth
    // uniformly from [minDepth_, maxDepth_] (slow_event_scheduler.h:490-492) and
    // getCurrentValue() is polarity * depth * envelope (:331-341), so every event
    // still FIRES - it just carries zero level. That is the point: a dead
    // scheduler would make this clause vacuous, and the onset counter below is
    // what proves it was not.
    {
        auto v = makeAcceleratedVoice(0x5C019u);
        v->setEcosystemDepth(0.0f);
        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            mutableScheduler(*v, k).setDepthRange(0.0f, 0.0f);
        }
        v->noteOn(55.0f, 1.0f);

        std::array<KindReading, EcosystemEngine::kNumKinds> bases{};
        for (std::size_t i = 0; i < kKinds.size(); ++i) {
            bases[i] = baseForKind(*v, kKinds[i]);
        }

        ChunkBuffer buf;
        FirstViolation violation;
        bool allFinite = true;
        std::uint64_t onsets = 0;
        float maxAgentOutput = 0.0f;
        std::array<bool, VoragoVoice::kNumEventSchedulers> wasActive{};

        for (std::uint64_t c = 0; c < totalChunks; ++c) {
            allFinite = renderChunk(*v, buf) && allFinite;

            for (std::size_t i = 0; i < kKinds.size(); ++i) {
                const KindReading got = readKind(*v, kKinds[i]);
                if (got != bases[i]) {
                    std::ostringstream os;
                    os << kindName(kKinds[i]) << " family: ";
                    for (std::size_t s = 0; s < got.count; ++s) {
                        os << "[" << s << "] " << got.v[s] << " vs base " << bases[i].v[s] << "; ";
                    }
                    violation.latch(c, os.str());
                }
            }

            for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
                const bool active = v->scheduler(k).isEventActive();
                if (active && !wasActive[k]) {
                    ++onsets;
                }
                wasActive[k] = active;
            }
            const std::size_t agents = v->ecosystem().getAgentCount();
            for (std::size_t a = 0; a < agents; ++a) {
                maxAgentOutput = std::max(maxAgentOutput, v->ecosystem().getAgentOutput(a));
            }
        }

        WARN("SC-019 (1) baseline: " << onsets << " scheduler onsets at depth 0, max agent output "
                                     << maxAgentOutput);
        INFO("first violation at control step " << violation.step << ": " << violation.what);
        REQUIRE_FALSE(violation.hit);
        REQUIRE(allFinite);
        // NON-VACUITY, both halves: the schedulers really did fire (so "depth 0"
        // is a level statement, not a dead component) and the ecosystem really
        // was producing output (so "depth 0" is what zeroed the lane).
        REQUIRE(onsets > 0u);
        REQUIRE(maxAgentOutput > 0.0f);
    }

    // -------------------------------------------------------------------------
    // CLAUSES 2 AND 3 - the ecosystem contribution with the schedulers live
    // -------------------------------------------------------------------------
    // TWO VOICES IN LOCKSTEP, identical in seed, configuration and call pattern,
    // differing ONLY in ecosystem depth. Nothing downstream of the identity layer
    // feeds back into it - the ecosystem simulation, both schedulers and both life
    // modulators advance on the control clock alone (renderOneChunk step 1,
    // vorago_voice.h:1828-1849) - so the SCHEDULER contribution is identical in
    // the two arms by construction, and the difference between them IS the
    // ecosystem contribution. That is what makes clause 2's "never falls below the
    // scheduler-only trace" a comparison rather than a re-derivation.
    auto ecoArm = makeAcceleratedVoice(0x5C019u);
    auto schedArm = makeAcceleratedVoice(0x5C019u);
    ecoArm->setEcosystemDepth(1.0f);
    schedArm->setEcosystemDepth(0.0f);
    ecoArm->noteOn(55.0f, 1.0f);
    schedArm->noteOn(55.0f, 1.0f);

    {
        ChunkBuffer ecoBuf;
        ChunkBuffer schedBuf;
        FirstViolation belowTrace;
        bool allFinite = true;
        std::array<std::uint64_t, EcosystemEngine::kNumKinds> attributable{};
        std::array<float, EcosystemEngine::kNumKinds> maxExcess{};
        std::array<KindReading, EcosystemEngine::kNumKinds> prevEco{};
        std::array<KindReading, EcosystemEngine::kNumKinds> prevSched{};
        bool havePrev = false;

        for (std::uint64_t c = 0; c < totalChunks; ++c) {
            allFinite = renderChunk(*ecoArm, ecoBuf) && allFinite;
            allFinite = renderChunk(*schedArm, schedBuf) && allFinite;

            for (std::size_t i = 0; i < kKinds.size(); ++i) {
                const KindReading eco = readKind(*ecoArm, kKinds[i]);
                const KindReading sched = readKind(*schedArm, kKinds[i]);

                // CLAUSE 2. The FR-023 maximum, observed on the product in the
                // state it actually runs in.
                //
                // (A-9) FOR THE `Partial` FAMILY THIS REDUCES TO "NEVER BELOW
                // BASE", AND THAT IS A DECISION, NOT AN OMISSION. FR-022's five
                // scheduler families are BloomTrigger, NoiseWake, PeakWake,
                // LoopWake and GhostBurst (vorago_voice.h:254-260), and
                // BloomTrigger calls bloom_.triggerBloom() on the ONSET EDGE
                // (:1578-1580) - it writes neither HarmonicCloud::setMutation nor
                // BloomEngine::setDepth. So the scheduler-only trace for the two
                // `Partial` destinations IS their configured base, and the
                // comparison below degenerates to "never below base" there of its
                // own accord, with no special case in the code.
                for (std::size_t s = 0; s < eco.count; ++s) {
                    if (eco.v[s] < sched.v[s]) {
                        std::ostringstream os;
                        os << kindName(kKinds[i]) << " slot " << s << ": ecosystem arm "
                           << eco.v[s] << " below scheduler-only arm " << sched.v[s];
                        belowTrace.latch(c, os.str());
                    }
                    maxExcess[i] = std::max(maxExcess[i], eco.v[s] - sched.v[s]);
                }

                // CLAUSE 3. A change attributable to the ecosystem: the family
                // moved in the ecosystem arm at a control step where the
                // scheduler contribution did not move at all. For the `Partial`
                // family every change is so attributable, since no scheduler
                // writes it - again, no special case is needed.
                if (havePrev && (sched == prevSched[i]) && (eco != prevEco[i])) {
                    ++attributable[i];
                }
                prevEco[i] = eco;
                prevSched[i] = sched;
            }
            havePrev = true;
        }

        const double equivalentMinutes = kEquivalentSeconds / 60.0;
        for (std::size_t i = 0; i < kKinds.size(); ++i) {
            WARN("SC-019 (3) " << kindName(kKinds[i]) << ": " << attributable[i]
                 << " ecosystem-attributable changes over " << equivalentMinutes
                 << " equivalent minutes = "
                 << (static_cast<double>(attributable[i]) / equivalentMinutes)
                 << " per minute; max (eco - scheduler) excess " << maxExcess[i]);
        }

        INFO("first clause-2 violation at control step " << belowTrace.step << ": "
                                                         << belowTrace.what);
        REQUIRE_FALSE(belowTrace.hit);
        REQUIRE(allFinite);

        for (std::size_t i = 0; i < kKinds.size(); ++i) {
            const double perMinute = static_cast<double>(attributable[i]) / equivalentMinutes;
            // If this misses, the lever is the DESTINATION BASE the ecosystem
            // contribution has to climb over (FR-021's table - noise 0.35, peaks
            // and loops 0.50), never this threshold: a family whose base already
            // sits above everything the ecosystem produces is a routing that
            // changes nothing, which is precisely what SC-019 exists to catch.
            INFO(kindName(kKinds[i]) << " family: " << perMinute
                 << " ecosystem-attributable changes per equivalent minute, max excess over the "
                    "scheduler-only trace "
                 << maxExcess[i]);
            REQUIRE(perMinute >= 3.0);
        }
    }

    // -------------------------------------------------------------------------
    // CLAUSE 3, second half - the driving agent kind, confirmed by dormancy
    // -------------------------------------------------------------------------
    // Setting a kind's agents dormant drives their published output to exactly 0
    // (the kWakeSilenceEpsilon snap at ecosystem_engine.h:2324-2327) WITHOUT
    // touching the simulation, so that family's ecosystem contribution becomes
    // the constant 0 and the two arms must read EXACTLY equal there - while every
    // other contribution, the schedulers included, carries on.
    //
    // The gate walks to its target in rampSteps_ = max(1, ceil(0.050 / dt))
    // publications (ecosystem_engine.h:357, :2298-2309), i.e. ~5 simulation steps
    // at the shipped 8-chunk interval - about 53 ms. The settle window below is
    // two AUDIO seconds, ~187 simulation steps, so the ramp is long finished
    // before the measurement window opens.
    {
        const std::uint64_t settleChunks = chunksForAudioSeconds(2.0);
        const std::uint64_t measureChunks = chunksForAudioSeconds(10.0);

        ChunkBuffer ecoBuf;
        ChunkBuffer schedBuf;
        bool allFinite = true;

        for (std::size_t i = 0; i < kKinds.size(); ++i) {
            const Kind k = kKinds[i];
            setKindDormant(*ecoArm, k, true);

            for (std::uint64_t c = 0; c < settleChunks; ++c) {
                allFinite = renderChunk(*ecoArm, ecoBuf) && allFinite;
                allFinite = renderChunk(*schedArm, schedBuf) && allFinite;
            }

            FirstViolation notStatic;
            std::uint64_t schedulerChanges = 0;
            std::array<KindReading, EcosystemEngine::kNumKinds> prevSched{};
            bool havePrev = false;

            for (std::uint64_t c = 0; c < measureChunks; ++c) {
                allFinite = renderChunk(*ecoArm, ecoBuf) && allFinite;
                allFinite = renderChunk(*schedArm, schedBuf) && allFinite;

                const KindReading eco = readKind(*ecoArm, k);
                const KindReading sched = readKind(*schedArm, k);
                if (eco != sched) {
                    std::ostringstream os;
                    os << kindName(k) << " still carries an ecosystem contribution while dormant: ";
                    for (std::size_t s = 0; s < eco.count; ++s) {
                        os << "[" << s << "] " << eco.v[s] << " vs " << sched.v[s] << "; ";
                    }
                    notStatic.latch(c, os.str());
                }

                // "...while the scheduler contribution continues" - measured over
                // ALL FIVE families rather than this one. The family a scheduler
                // event addresses is a uniform draw over five targets
                // (slow_event_scheduler.h:497-503), so no bounded window can be
                // guaranteed an event on one NAMED family; what the clause needs
                // is that the schedulers did not stop, and that is a statement
                // about the roster.
                for (std::size_t j = 0; j < kKinds.size(); ++j) {
                    const KindReading s = readKind(*schedArm, kKinds[j]);
                    if (havePrev && (s != prevSched[j])) {
                        ++schedulerChanges;
                    }
                    prevSched[j] = s;
                }
                havePrev = true;
            }

            WARN("SC-019 (3) dormancy " << kindName(k) << ": " << schedulerChanges
                 << " scheduler-arm changes across the measurement window");
            INFO("first non-static step " << notStatic.step << ": " << notStatic.what);
            REQUIRE_FALSE(notStatic.hit);
            // ...WHILE THE SCHEDULER CONTRIBUTION CONTINUES. Without this the
            // equality above would also hold on a voice whose identity layer had
            // stopped writing anything at all, which is the opposite of what the
            // dormancy arm is meant to demonstrate. The window is 10 audio
            // seconds - 100 equivalent seconds - against a fast scheduler drawing
            // 2-9 s periods, so an event is not a coincidence.
            REQUIRE(schedulerChanges > 0u);

            setKindDormant(*ecoArm, k, false);
        }
        REQUIRE(allFinite);
    }

    WARN("SC-019 ecosystem routing: A = " << kAccelerationFactor << ", "
         << (kEquivalentSeconds / 60.0)
         << " equivalent minutes per arm, wall clock " << wallSecondsSince(t0) << " s");
}

// =============================================================================
// SC-020 - slow events fire and are heard
// =============================================================================
// spec.md:1727-1731, plan.md:2504, tasks.md T018.
//
// THE ECOSYSTEM IS AT DEPTH 0 FOR THIS CASE, and that is what makes the third
// clause an attribution rather than a coincidence: with the ecosystem lane at
// zero the ONLY thing that can lift a destination off its base is a scheduler
// event (FR-023's maximum, vorago_voice.h:976-980).
//
// THE THIRD CLAUSE'S ONE REDUCTION, STATED RATHER THAN HIDDEN. FR-023 combines a
// scheduler contribution with the destination's configured base by MAXIMUM, so an
// event whose drawn depth is BELOW that base cannot move the destination - the
// base already dominates it. The shipped bases are noise 0.35 and peaks/loops
// 0.50 (vorago_voice.h:564, :578, :591) while the shipped depth range is
// [0.4, 1.0] (:640), so a minority of peak/loop events are legitimately
// invisible. The clause is therefore asserted in the discriminating direction:
// EVERY event whose peak value exceeds its destination's base MUST produce a
// change, and every event that produced no change MUST be one whose depth did
// not clear the base. A routing failure - an event that reaches no destination at
// all - shows up as a change-less event with depth above the base, which is
// exactly what the counters below separate out.
// =============================================================================

TEST_CASE("VoragoVoice_SlowEventRouting", "[systems][vorago][long]") {
    constexpr double kEquivalentSeconds = 1800.0;  // 30 minutes of shipped timeline
    const std::uint64_t totalChunks = chunksForEquivalentSeconds(kEquivalentSeconds);
    REQUIRE(totalChunks == 135000u);  // 180 s of audio at A = 10

    auto v = makeAcceleratedVoice(0x5C020u);
    v->setEcosystemDepth(0.0f);
    v->noteOn(55.0f, 1.0f);

    // The configured range, READ FROM THE COMPONENT rather than recomputed here.
    // accelerate() wrote it and publishIdentity() re-writes the identical numbers
    // on every control step (applyEventRateScale, vorago_voice.h:1673-1684), so
    // this IS the range every drawn period came from - including the pre-roll.
    std::array<double, VoragoVoice::kNumEventSchedulers> minInterval{};
    std::array<double, VoragoVoice::kNumEventSchedulers> maxInterval{};
    for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
        minInterval[k] = static_cast<double>(v->scheduler(k).getMinIntervalSeconds());
        maxInterval[k] = static_cast<double>(v->scheduler(k).getMaxIntervalSeconds());
    }
    REQUIRE(minInterval[0] == Catch::Approx(2.0).margin(1.0e-4));
    REQUIRE(maxInterval[0] == Catch::Approx(9.0).margin(1.0e-4));

    // Onsets are taken on the scheduler's own 32-sample control grid
    // (slow_event_scheduler.h:290-320) and observed here on the voice's 64-sample
    // one, so a measured onset-to-onset interval can sit up to 96 samples either
    // side of the drawn period. 0.01 s (480 samples) is comfortably outside that
    // and far inside the 2 s minimum it guards.
    constexpr double kIntervalToleranceSeconds = 0.01;
    /// A depth within this of its destination's base is a boundary case and is
    /// excluded from BOTH directions of the third clause - the comparison there
    /// is exactly the one FR-023's max() makes, and float equality at the
    /// boundary decides nothing.
    constexpr float kBaseGuardBand = 1.0e-3f;

    struct EventTracker {
        bool active = false;
        std::uint8_t family = SlowEventScheduler::kNoTarget;
        float depth = 0.0f;
        float base = 0.0f;
        float maxObserved = 0.0f;
        bool haveLastOnset = false;
        std::uint64_t lastOnsetChunk = 0;
    };
    std::array<EventTracker, VoragoVoice::kNumEventSchedulers> tracker{};

    ChunkBuffer buf;
    bool allFinite = true;
    std::array<std::uint64_t, VoragoVoice::kNumEventFamilies> familyDraws{};
    std::array<std::uint64_t, VoragoVoice::kNumEventFamilies> familyChanges{};
    std::uint64_t intervalsMeasured = 0;
    std::uint64_t intervalsOutOfRange = 0;
    double worstIntervalSeconds = 0.0;
    std::uint64_t bloomTriggers = 0;
    std::uint64_t bloomTriggersWithoutEvent = 0;
    std::uint64_t silentEventsAboveBase = 0;  // THE defect signature
    std::uint64_t silentEventsBelowBase = 0;  // FR-023's max(), working as specified
    std::uint64_t boundaryEvents = 0;

    const auto t0 = std::chrono::steady_clock::now();
    for (std::uint64_t c = 0; c < totalChunks; ++c) {
        // BloomTrigger's destination observable is the bloom's own event
        // accounting, sampled ACROSS the chunk: publishIdentity() calls
        // triggerBloom() in step 1 and updateSpectrumTarget() runs the bloom in
        // step 2 of the SAME chunk (vorago_voice.h:1834-1846), and the arm is
        // consumed there (bloom_engine.h:963-978). A consumed arm increments
        // either the spawn counter or - if the gate is shut - the discard
        // counter; both are the destination reacting.
        const std::uint64_t bloomEventsBefore =
            v->bloom().getSpawnEventCount() + v->bloom().getDiscardedEventCount();

        allFinite = renderChunk(*v, buf) && allFinite;

        const std::uint64_t bloomEventsAfter =
            v->bloom().getSpawnEventCount() + v->bloom().getDiscardedEventCount();

        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            EventTracker& t = tracker[k];
            const bool active = v->scheduler(k).isEventActive();

            if (active && !t.active) {
                // ---- ONSET -------------------------------------------------
                t.family = v->scheduler(k).getActiveTarget();
                t.depth = v->scheduler(k).getActiveDepth();
                t.maxObserved = 0.0f;
                if (t.family < VoragoVoice::kNumEventFamilies) {
                    ++familyDraws[t.family];
                    const Kind kind = familyKind(t.family);
                    t.base = maxOf(baseForKind(*v, kind));
                    if (static_cast<EventFamily>(t.family) == EventFamily::BloomTrigger) {
                        ++bloomTriggers;
                        if (bloomEventsAfter <= bloomEventsBefore) {
                            ++bloomTriggersWithoutEvent;
                        }
                    }
                }
                if (t.haveLastOnset) {
                    const double seconds =
                        static_cast<double>((c - t.lastOnsetChunk) * kChunk) / kSampleRate;
                    ++intervalsMeasured;
                    if (seconds < minInterval[k] - kIntervalToleranceSeconds
                        || seconds > maxInterval[k] + kIntervalToleranceSeconds) {
                        ++intervalsOutOfRange;
                    }
                    worstIntervalSeconds = std::max(worstIntervalSeconds, seconds);
                }
                t.haveLastOnset = true;
                t.lastOnsetChunk = c;
            }

            if (active && t.family < VoragoVoice::kNumEventFamilies) {
                t.maxObserved = std::max(t.maxObserved, maxOf(readKind(*v, familyKind(t.family))));
            }

            if (!active && t.active) {
                // ---- THE EVENT IS OVER: classify it -------------------------
                if (t.family < VoragoVoice::kNumEventFamilies
                    && static_cast<EventFamily>(t.family) != EventFamily::BloomTrigger) {
                    const bool changed = t.maxObserved > t.base + kBaseGuardBand;
                    const bool couldChange = t.depth > t.base + kBaseGuardBand;
                    const bool boundary = std::abs(t.depth - t.base) <= kBaseGuardBand;
                    if (changed) {
                        ++familyChanges[t.family];
                    } else if (boundary) {
                        ++boundaryEvents;
                    } else if (couldChange) {
                        ++silentEventsAboveBase;
                    } else {
                        ++silentEventsBelowBase;
                    }
                }
            }
            t.active = active;
        }
    }
    const double wall = wallSecondsSince(t0);

    std::uint64_t totalDraws = 0;
    for (const std::uint64_t d : familyDraws) {
        totalDraws += d;
    }

    WARN("SC-020 slow events: A = " << kAccelerationFactor << ", " << (kEquivalentSeconds / 60.0)
         << " equivalent minutes, " << totalChunks << " control steps, wall clock " << wall
         << " s; " << totalDraws << " events, draws per family {" << familyDraws[0] << ", "
         << familyDraws[1] << ", " << familyDraws[2] << ", " << familyDraws[3] << ", "
         << familyDraws[4] << "}, observed changes {" << familyChanges[0] << ", "
         << familyChanges[1] << ", " << familyChanges[2] << ", " << familyChanges[3] << ", "
         << familyChanges[4] << "}, bloom triggers " << bloomTriggers << ", silent-above-base "
         << silentEventsAboveBase << ", silent-below-base " << silentEventsBelowBase
         << ", boundary " << boundaryEvents << ", intervals " << intervalsMeasured << " (worst "
         << worstIntervalSeconds << " s)");

    REQUIRE(allFinite);

    // (1) The measured inter-event interval distribution lies inside the
    //     configured range.
    INFO("intervals measured " << intervalsMeasured << ", out of range " << intervalsOutOfRange
                               << ", tolerance " << kIntervalToleranceSeconds << " s");
    REQUIRE(intervalsMeasured > 0u);  // non-vacuity
    REQUIRE(intervalsOutOfRange == 0u);

    // (2) Every target index in [0, targetCount) is selected at least once. The
    //     union of BOTH schedulers' draws: the roster is the voice's, not one
    //     scheduler's, and both address it (setTargetCount(kNumEventFamilies),
    //     vorago_voice.h:645). At ~36 draws over this render a uniform 5-way
    //     choice misses a family with probability ~2e-3; the seed is fixed, so
    //     this is deterministic - if it ever fails, the lever is the SEED (or a
    //     longer render), never the criterion.
    for (std::size_t f = 0; f < VoragoVoice::kNumEventFamilies; ++f) {
        INFO("family " << f << " was drawn " << familyDraws[f] << " times out of " << totalDraws);
        REQUIRE(familyDraws[f] > 0u);
    }

    // (3) Each event produces a measurable change on its destination's
    //     observable. See the banner above this case for the one reduction.
    INFO("events that cleared their destination's base but moved nothing: "
         << silentEventsAboveBase);
    REQUIRE(silentEventsAboveBase == 0u);
    // BloomTrigger's half: every onset is consumed by the bloom on the same
    // control chunk.
    INFO("bloom triggers that produced neither a spawn nor a discard: "
         << bloomTriggersWithoutEvent << " of " << bloomTriggers);
    REQUIRE(bloomTriggers > 0u);  // non-vacuity
    REQUIRE(bloomTriggersWithoutEvent == 0u);
    // ...and the level-routing half is not vacuous either: at least one event on
    // each of the four level-carrying families really did move its destination.
    for (std::size_t f = 1; f < VoragoVoice::kNumEventFamilies; ++f) {
        INFO("family " << f << ": " << familyDraws[f] << " draws, " << familyChanges[f]
                       << " observed destination changes");
        REQUIRE(familyChanges[f] > 0u);
    }
}
