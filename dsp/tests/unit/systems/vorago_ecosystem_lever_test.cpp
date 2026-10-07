// ==============================================================================
// Vorago Phase 13b - ecosystem levers (plan S5.1 / S5.2)
// ==============================================================================
// Owning spec: specs/vorago-phase13b-ecosystem-audibility (tasks.md T003).
//
// THIS TU IS THE ONE DEFINITION of detail::VoragoEcosystemLeverProbe in
// dsp_systems_tests - no other TU may define it (the [long] sibling,
// vorago_ecosystem_lever_longrun_test.cpp, uses the public API only). The
// friend is forward-declared in vorago_voice.h (namespace detail) and
// befriended by VoragoVoice and VoragoEngine.
//
// The lever-member accessors (injectEco*, noiseLevelBase, loopGainBase,
// shapeLeverInput, the lever constants) and schedLanes were added by T016
// (plan S7 step 3); they compile once T020 adds the members they touch.
//
// Tags [systems][vorago]. PORTABILITY: finiteness via the fast-math-immune
// Krate::DSP::detail::isFinite (core/db_utils.h:118) only - no std::isnan.
// Voices and engines live on the heap (std::make_unique).
//
// HIDDEN DIAGNOSTIC (plan S3.4): VoragoVoice_EcosystemLaneSurvey [.probe]
// gathers the eco lanes of a default voice over 340 s (life-only) and prints
// the per-kind quantiles that feed the wake-base retune rule. Run alone:
//   dsp_systems_tests.exe "VoragoVoice_EcosystemLaneSurvey"
//       > specs/vorago-phase13b-ecosystem-audibility/artifacts/lane_survey.log 2>&1
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>  // T022 Bounded: Life = 1 every block
#include <krate/dsp/systems/vorago_voice.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include "artifact_detection.h"  // tests/test_helpers (T022 ClickFree: ClickDetector)
#include "render_fingerprint.h"  // tests/test_helpers (SC-012 post-clear render)

// T055: the detector only - never allocation_operator_overrides.h (the overrides
// are linked once per test executable).
#include <allocation_detector.h>

// The shared Phase 10 fixtures: applyFastAttack (FR-014a), reused, not re-invented.
#include <vorago_fixtures.h>

// -----------------------------------------------------------------------------
// The friend (B-4): defined here and nowhere else.
// -----------------------------------------------------------------------------

namespace Krate::DSP::detail {

struct VoragoEcosystemLeverProbe {
    using Lanes = std::array<std::array<float, VoragoVoice::kMaxSlotsPerKind>,
                             EcosystemEngine::kNumKinds>;

    /// 64 samples of the identity layer, no audio (vorago_voice.h:2277).
    static void advanceLifeOnly(VoragoVoice& v) { v.advanceOneChunkLifeOnly(); }

    static EcosystemEngine& ecosystem(VoragoVoice& v) { return v.ecosystem_; }
    /// 13c B-6: voice slot i of the engine (the density route reads its lane).
    static VoragoVoice& voice(VoragoEngine& e, std::size_t i) { return e.voices_[i]; }

    // --- FR-023 lane-injection seam (plan S2.5) ------------------------------
    /// Replaces the WHOLE eco lane set at every publish until clearInjection().
    static void injectEco(VoragoVoice& v, const Lanes& eco) {
        v.injectedEco_ = eco;
        v.ecoInjectionActive_ = true;
    }
    static void clearInjection(VoragoVoice& v) { v.ecoInjectionActive_ = false; }
    /// injectEco on every voices_[v] slot of the engine.
    static void injectEcoAll(VoragoEngine& e, const Lanes& eco) {
        for (auto& voice : e.voices_) {
            injectEco(voice, eco);
        }
    }

    // --- FR-021 base shadows --------------------------------------------------
    static float noiseLevelBase(const VoragoVoice& v) { return v.noiseLevelBaseDb_; }
    static float loopGainBase(const VoragoVoice& v) { return v.loopGainBase_; }

    // --- Held lever offsets (plan S2.2), for the SC-012 reset contract ------
    static float noiseLevelOffset(const VoragoVoice& v, std::size_t s) {
        return v.noiseLevelOffsetDb_[s];
    }
    static float loopGainOffset(const VoragoVoice& v, std::size_t l) {
        return v.loopGainOffset_[l];
    }

    // --- Private lever constants (plan S2.1), for expected-value expressions ---
    static constexpr float noiseLevelLeverSpanDb() { return VoragoVoice::kNoiseLevelLeverSpanDb; }
    static constexpr float loopGainLeverSpan() { return VoragoVoice::kLoopGainLeverSpan; }
    static constexpr float leverInputGain(std::size_t kind) {
        return VoragoVoice::kLeverInputGain[kind];
    }
    static constexpr float peakLevelBaseDb() { return VoragoVoice::kPeakLevelBaseDb; }
    static constexpr float peakLevelLeverSpanDb() { return VoragoVoice::kPeakLevelLeverSpanDb; }

    // --- L4 step 2 ladder levers (plan S2.8): constants and slew state ---------
    static constexpr float freqWanderBaseSemis() { return VoragoVoice::kFreqWanderBaseSemis; }
    static constexpr float freqWanderLeverSpanSemis() {
        return VoragoVoice::kFreqWanderLeverSpanSemis;
    }
    static constexpr float ringCouplingBase() { return VoragoVoice::kRingCouplingBase; }
    static constexpr float couplingLeverSpan() { return VoragoVoice::kCouplingLeverSpan; }
    static float freqWanderApplied(const VoragoVoice& v, std::size_t p) {
        return v.freqWanderApplied_[p];
    }
    static float ringCouplingApplied(const VoragoVoice& v, std::size_t l) {
        return v.ringCouplingApplied_[l];
    }
    static float freqWanderSlewPerChunk(const VoragoVoice& v) { return v.freqWanderSlewPerChunk_; }
    /// FR-018b: the production rate-compensation factor itself, and its exponent.
    static float wanderLeverRateComp(float rateHz) {
        return VoragoVoice::wanderLeverRateComp(rateHz);
    }
    static constexpr float wanderLeverRateCompExponent() {
        return VoragoVoice::kWanderLeverRateCompExponent;
    }

    // --- The Partial pair's bases (FR-016 unshaped check, SC-021) -------------
    static float mutationBase(const VoragoVoice& v) { return v.mutationBase_; }
    static float bloomDepthBase(const VoragoVoice& v) { return v.bloomDepthBase_; }

    // --- Phase 13c L2 route sizes (plan S2.2, T030): private, read through the friend ---
    static constexpr float partialLaneGain() { return VoragoVoice::kPartialLaneGain; }
    static constexpr float ghostLaneGain() { return VoragoVoice::kGhostLaneGain; }

    /// SC-021: the production shaping function itself (private static, plan S2.4).
    static float shapeLeverInput(float lane, float gain) {
        return VoragoVoice::shapeLeverInput(lane, gain);
    }

    /// FR-019 / FR-020: the scheduler lanes the LAST publish used, recomputed
    /// without side effects - the const second half of gatherSchedulerLanes
    /// (vorago_voice.h:1885-1893). VALID ONLY IMMEDIATELY AFTER advanceLifeOnly():
    /// publishIdentity() is the last reader of scheduler state in a life-only
    /// chunk, so nothing has moved the schedulers since (plan S5.1).
    static void schedLanes(const VoragoVoice& v, Lanes& out) {
        for (auto& row : out) {
            row.fill(0.0f);
        }
        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            const auto& sched = v.scheduler(k);
            if (!sched.isEventActive()) {
                continue;
            }
            const std::uint8_t family = sched.getActiveTarget();
            if (family >= VoragoVoice::kNumEventFamilies ||
                family == static_cast<std::uint8_t>(VoragoVoice::EventFamily::BloomTrigger)) {
                continue;
            }
            const std::size_t kind = VoragoVoice::kindForFamily(family);
            const std::size_t slot = std::min(static_cast<std::size_t>(v.drawnSlot_[k]),
                                              VoragoVoice::kMaxSlotsPerKind - 1u);
            const float value = std::max(0.0f, sched.getCurrentValue());
            out[kind][slot] = std::max(out[kind][slot], value);
        }
    }

    /// The eco half of the gather (const, no side effects; vorago_voice.h:1842).
    static void gatherEco(const VoragoVoice& v, Lanes& out) {
        VoragoVoice::IdentityLanes l{};
        v.gatherEcosystemLanes(l);
        out = l.eco;
    }

    /// True iff at least one valid agent of `kind` is dealt onto `slot`
    /// (assignAgentSlots, vorago_voice.h:1776-1797).
    static bool slotAddressed(const VoragoVoice& v, std::size_t kind, std::size_t slot) {
        const std::size_t agents =
            std::min(v.ecosystem().getAgentCount(), EcosystemEngine::kMaxAgents);
        for (std::size_t i = 0; i < agents; ++i) {
            if (v.agentValid_[i] != 0u &&
                static_cast<std::size_t>(v.ecosystem().getAgentKind(i)) == kind &&
                static_cast<std::size_t>(v.agentSlot_[i]) == slot) {
                return true;
            }
        }
        return false;
    }

    static float peakWakeBase(const VoragoVoice& v, std::size_t p) { return v.peakWakeBase_[p]; }
    static float loopWakeBase(const VoragoVoice& v, std::size_t l) { return v.loopWakeBase_[l]; }
    static float noiseWakeBase(const VoragoVoice& v, std::size_t s) { return v.noiseWakeBase_[s]; }

    static std::uint8_t lastEventTarget(const VoragoVoice& v, std::size_t k) {
        return v.lastEventTarget_[k];
    }

    static void setLimiterCeilingDb(VoragoEngine& e, float db) { e.limiter_.setCeilingDb(db); }

    static bool isRendering(const VoragoEngine& e, std::size_t v) { return e.isRendering(v); }

    /// T022 Determinism: setEcosystemDepth on EVERY voice slot. VoragoEngine has
    /// no non-const voice access (getVoice is const, vorago_engine.h:790-798).
    static void setEcosystemDepthAll(VoragoEngine& e, float d) {
        for (auto& voice : e.voices_) {
            voice.setEcosystemDepth(d);
        }
    }
};

} // namespace Krate::DSP::detail

namespace {

using Krate::DSP::EcosystemEngine;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoVoice;
using Krate::DSP::VoragoVoiceConfig;
using Probe = Krate::DSP::detail::VoragoEcosystemLeverProbe;

/// Nearest-rank quantile of an already-sorted multiset. Empty -> -1 (printed).
double nearestRank(const std::vector<float>& sorted, double p) {
    if (sorted.empty()) {
        return -1.0;
    }
    const auto n = static_cast<double>(sorted.size());
    auto rank = static_cast<std::size_t>(std::ceil(p * n));
    rank = std::clamp(rank, std::size_t{1}, sorted.size());
    return static_cast<double>(sorted[rank - 1u]);
}

/// A quiet NaN built from its bit pattern through a volatile, so -ffast-math
/// cannot fold it away (no std::isnan anywhere in this TU).
[[nodiscard]] float quietNaN() {
    volatile std::uint32_t opaque = 0x7FC00000u;
    const std::uint32_t bits = opaque;
    return std::bit_cast<float>(bits);
}

/// Every slot of every kind at the lane value `x`.
[[nodiscard]] Probe::Lanes uniformLanes(float x) {
    Probe::Lanes l{};
    for (auto& row : l) {
        row.fill(x);
    }
    return l;
}

/// The plan S3.4 rule: clamp(floor(Q(0.60) / 0.05) * 0.05, 0.05, 0.50).
double retuneRule(double q60) {
    const double floored = std::floor(q60 / 0.05) * 0.05;
    return std::clamp(floored, 0.05, 0.50);
}

constexpr auto kKindPartial = static_cast<std::size_t>(EcosystemEngine::Kind::Partial);
constexpr auto kKindResonator = static_cast<std::size_t>(EcosystemEngine::Kind::Resonator);
constexpr auto kKindNoise = static_cast<std::size_t>(EcosystemEngine::Kind::Noise);
constexpr auto kKindFeedback = static_cast<std::size_t>(EcosystemEngine::Kind::Feedback);
constexpr auto kKindGhost = static_cast<std::size_t>(EcosystemEngine::Kind::Ghost);

/// The FIRST mismatching surface of a chunk, recorded without a Catch2
/// assertion per element (10-minute windows check millions of values); the
/// caller REQUIREs once per chunk on it. `what == nullptr` means all equal.
struct FirstMismatch {
    const char* what = nullptr;
    std::size_t index = 0;
    float got = 0.0f;
    float want = 0.0f;

    void check(const char* w, std::size_t i, float g, float x) {
        if (what == nullptr && !(g == x)) {
            what = w;
            index = i;
            got = g;
            want = x;
        }
    }
};

/// The three lever destinations' expected values for a UNIFORM eco lane `lane`
/// (every slot of every kind), written with the SAME float expressions as
/// production (plan S2.3 / S2.4): the offset is span * shapeLeverInput(lane, g)
/// held in a float, then clamp(base + offset). The Resonator lever is the
/// single clamp(kPeakLevelBaseDb + span * x) expression.
struct LeverExpect {
    float noise = 0.0f;
    float peak = 0.0f;
    float loop = 0.0f;
    float wanderGoal = 0.0f;  // L4 step 2: the slew's goal, reached after the settle time
    float coupling = 0.0f;    // L4 step 2: the ring pair's coupling, written at once
};
[[nodiscard]] LeverExpect expectedLevers(const VoragoVoice& v, float lane) {
    using Krate::DSP::FeedbackEcology;
    using Krate::DSP::ResonanceDriftNetwork;
    LeverExpect e{};
    const float noiseOffset = Probe::noiseLevelLeverSpanDb() *
                              Probe::shapeLeverInput(lane, Probe::leverInputGain(kKindNoise));
    e.noise = std::clamp(Probe::noiseLevelBase(v) + noiseOffset, -96.0f, 12.0f);
    const float xr = Probe::shapeLeverInput(lane, Probe::leverInputGain(kKindResonator));
    e.peak = std::clamp(Probe::peakLevelBaseDb() + Probe::peakLevelLeverSpanDb() * xr,
                        ResonanceDriftNetwork::kMinPeakLevelDb,
                        ResonanceDriftNetwork::kMaxPeakLevelDb);
    const float xf = Probe::shapeLeverInput(lane, Probe::leverInputGain(kKindFeedback));
    const float loopOffset = Probe::loopGainLeverSpan() * xf;
    e.loop = std::clamp(Probe::loopGainBase(v) + loopOffset, FeedbackEcology::kMinLoopGain,
                        FeedbackEcology::kMaxLoopGain);
    // FR-018b: the span is scaled by the voice's CURRENT resonance wander rate,
    // in the same float expression as production (span * comp, then * x).
    const float wanderSpan = Probe::freqWanderLeverSpanSemis() *
                             Probe::wanderLeverRateComp(v.getResonanceWanderRate());
    e.wanderGoal = std::clamp(Probe::freqWanderBaseSemis() + wanderSpan * xr, 0.0f,
                              ResonanceDriftNetwork::kMaxFreqWanderSemis);
    e.coupling = std::clamp(Probe::ringCouplingBase() + Probe::couplingLeverSpan() * xf, 0.0f,
                            FeedbackEcology::kMaxCouplingPerPair);
    return e;
}

/// Active slot counts, exactly as applyIdentityLanes bounds its loops.
struct ActiveCounts {
    std::size_t sources = 0;
    std::size_t peaks = 0;
    std::size_t loops = 0;
};
[[nodiscard]] ActiveCounts activeCounts(const VoragoVoice& v) {
    using Krate::DSP::FeedbackEcology;
    using Krate::DSP::NoiseOrganism;
    using Krate::DSP::ResonanceDriftNetwork;
    return ActiveCounts{
        std::min(v.noise().getNumSources(), NoiseOrganism::kMaxSources),
        std::min(v.resonance().getNumPeaks(), ResonanceDriftNetwork::kMaxPeaks),
        std::min(v.ecology().getNumLoops(), FeedbackEcology::kMaxLoops)};
}

/// Every active lever destination == the expected value `e` (FR-015 / FR-019:
/// whatever the scheduler lanes are).
void checkLevers(const VoragoVoice& v, const ActiveCounts& n, const LeverExpect& e,
                 FirstMismatch& m) {
    for (std::size_t s = 0; s < n.sources; ++s) {
        m.check("noise.getSourceLevel", s, v.noise().getSourceLevel(s), e.noise);
    }
    for (std::size_t p = 0; p < n.peaks; ++p) {
        m.check("resonance.getPeakLevel", p, v.resonance().getPeakLevel(p), e.peak);
    }
    for (std::size_t l = 0; l < n.loops; ++l) {
        m.check("ecology.getLoopGain", l, v.ecology().getLoopGain(l), e.loop);
        if (n.loops > 1u) {  // L4 step 2: the shipped ring pair (l, l+1), written at once
            m.check("ecology.getCoupling(ring)", l, v.ecology().getCoupling(l, (l + 1u) % n.loops),
                    e.coupling);
        }
    }
}

// L4 step 2: the freq-wander lever is slewed over kFreqWanderLeverSlewSeconds,
// so it is checked only once the lane has been held long past the settle time.
// The slew's last step is `applied += (goal - applied)`, which can land one ulp
// off the goal, hence the tolerance.
void checkWanderSettled(const VoragoVoice& v, const ActiveCounts& n, const LeverExpect& e,
                        FirstMismatch& m) {
    for (std::size_t p = 0; p < n.peaks; ++p) {
        const float got = v.resonance().getFreqWander(p);
        if (std::fabs(got - e.wanderGoal) > 1e-5f) {
            m.check("resonance.getFreqWander(settled)", p, got, e.wanderGoal);
        }
    }
}

/// Every active wake surface == combineWake(base, eco, S[kind][slot]) (FR-020),
/// with the RAW eco lane `eco` (FR-019a / SC-021) and the scheduler lanes `S`
/// the same publish used.
void checkWakes(const VoragoVoice& v, const ActiveCounts& n, float eco, const Probe::Lanes& S,
                FirstMismatch& m) {
    for (std::size_t s = 0; s < n.sources; ++s) {
        m.check("noise.getSourceWakeAmount", s, v.noise().getSourceWakeAmount(s),
                VoragoVoice::combineWake(Probe::noiseWakeBase(v, s), eco, S[kKindNoise][s]));
    }
    for (std::size_t p = 0; p < n.peaks; ++p) {
        m.check("resonance.getPeakWakeAmount", p, v.resonance().getPeakWakeAmount(p),
                VoragoVoice::combineWake(Probe::peakWakeBase(v, p), eco, S[kKindResonator][p]));
    }
    for (std::size_t l = 0; l < n.loops; ++l) {
        m.check("ecology.getLoopWakeAmount", l, v.ecology().getLoopWakeAmount(l),
                VoragoVoice::combineWake(Probe::loopWakeBase(v, l), eco, S[kKindFeedback][l]));
    }
}

/// SC-006 neutral: every active lever destination reads its base exactly.
void checkNeutral(const VoragoVoice& v, const ActiveCounts& n, FirstMismatch& m) {
    for (std::size_t s = 0; s < n.sources; ++s) {
        m.check("noise.getSourceLevel", s, v.noise().getSourceLevel(s), Probe::noiseLevelBase(v));
    }
    for (std::size_t p = 0; p < n.peaks; ++p) {
        m.check("resonance.getPeakLevel", p, v.resonance().getPeakLevel(p),
                Probe::peakLevelBaseDb());
    }
    for (std::size_t l = 0; l < n.loops; ++l) {
        m.check("ecology.getLoopGain", l, v.ecology().getLoopGain(l), Probe::loopGainBase(v));
        if (n.loops > 1u) {  // L4 step 2
            m.check("ecology.getCoupling(ring)", l, v.ecology().getCoupling(l, (l + 1u) % n.loops),
                    Probe::ringCouplingBase());
        }
    }
    for (std::size_t p = 0; p < n.peaks; ++p) {  // L4 step 2: snapped at the base, never moved
        m.check("resonance.getFreqWander", p, v.resonance().getFreqWander(p),
                Probe::freqWanderBaseSemis());
        m.check("freqWanderApplied", p, Probe::freqWanderApplied(v, p), Probe::freqWanderBaseSemis());
    }
}

/// Seed first, then prepare (vorago_voice engine order, vorago_engine.h:346-348).
[[nodiscard]] std::unique_ptr<VoragoVoice> makeLeverVoice(double fs, std::uint32_t seed) {
    auto v = std::make_unique<VoragoVoice>();
    v->setSeed(seed);
    v->prepare(fs, VoragoVoiceConfig{});
    return v;
}

[[nodiscard]] std::uint32_t leverSeed() {
    return Krate::DSP::deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u);
}

} // namespace

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLaneSurvey (plan S3.4; SC-020 "before" figures)
// -----------------------------------------------------------------------------
TEST_CASE("VoragoVoice_EcosystemLaneSurvey", "[systems][vorago][.probe]") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
    constexpr std::size_t kTotalChunks = 340u * 48000u / kChunk;   // 340 s
    constexpr std::size_t kWindowStartChunk = 155u * 48000u / kChunk;  // 155 s

    constexpr auto kResonator = static_cast<std::size_t>(EcosystemEngine::Kind::Resonator);
    constexpr auto kFeedback = static_cast<std::size_t>(EcosystemEngine::Kind::Feedback);
    constexpr auto kNoise = static_cast<std::size_t>(EcosystemEngine::Kind::Noise);

    auto voice = std::make_unique<VoragoVoice>();
    // Engine order (vorago_engine.h:346-348): seed first, then prepare.
    const std::uint32_t seed =
        Krate::DSP::deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u);
    voice->setSeed(seed);
    voice->prepare(kSampleRate, VoragoVoiceConfig{});
    REQUIRE(voice->isPrepared());
    // Ecosystem depth left at the prepare default (0.85).
    voice->noteOn(65.406f, 100.0f / 127.0f);

    struct KindSurvey {
        const char* name;
        std::size_t kind;
        std::vector<float> lanes;
        std::size_t aboveBase = 0;
        std::size_t nonFinite = 0;
    };
    std::array<KindSurvey, 3> survey{{
        {"Resonator", kResonator, {}, 0u, 0u},
        {"Feedback", kFeedback, {}, 0u, 0u},
        {"Noise", kNoise, {}, 0u, 0u},
    }};
    for (auto& ks : survey) {
        ks.lanes.reserve(20000u * VoragoVoice::kMaxSlotsPerKind);
    }

    auto baseFor = [&](std::size_t kind, std::size_t slot) -> float {
        if (kind == kResonator) {
            return Probe::peakWakeBase(*voice, slot);
        }
        if (kind == kFeedback) {
            return Probe::loopWakeBase(*voice, slot);
        }
        return Probe::noiseWakeBase(*voice, slot);
    };

    Probe::Lanes lanes{};
    std::uint64_t lastStep = voice->ecosystem().getControlStepCount();
    std::size_t stepsSurveyed = 0;
    for (std::size_t c = 0; c < kTotalChunks; ++c) {
        Probe::advanceLifeOnly(*voice);
        const std::uint64_t step = voice->ecosystem().getControlStepCount();
        const bool stepped = step != lastStep;
        lastStep = step;
        if (!stepped || c < kWindowStartChunk) {
            continue;
        }
        ++stepsSurveyed;
        Probe::gatherEco(*voice, lanes);
        for (auto& ks : survey) {
            for (std::size_t s = 0; s < VoragoVoice::kMaxSlotsPerKind; ++s) {
                if (!Probe::slotAddressed(*voice, ks.kind, s)) {
                    continue;
                }
                const float x = lanes[ks.kind][s];
                if (!Krate::DSP::detail::isFinite(x)) {
                    ++ks.nonFinite;
                    continue;
                }
                ks.lanes.push_back(x);
                if (x > baseFor(ks.kind, s)) {
                    ++ks.aboveBase;
                }
            }
        }
    }

    constexpr std::array<double, 6> kQuantiles{0.10, 0.25, 0.50, 0.60, 0.75, 0.90};
    std::printf("\n=== VoragoVoice_EcosystemLaneSurvey (plan S3.4) ===\n");
    std::printf("fs=%.0f  seed=0x%08X  depth=%.2f  note=65.406 Hz vel=100/127\n", kSampleRate,
                static_cast<unsigned>(seed), static_cast<double>(voice->getEcosystemDepth()));
    std::printf("window=[155 s, 340 s]  chunks=%zu  simulation steps surveyed=%zu\n",
                kTotalChunks, stepsSurveyed);

    for (auto& ks : survey) {
        std::sort(ks.lanes.begin(), ks.lanes.end());
        const std::size_t n = ks.lanes.size();
        std::printf("\n[%s] addressed (step, slot) pairs=%zu  non-finite=%zu\n", ks.name, n,
                    ks.nonFinite);
        std::printf("[%s] addressed slots:", ks.name);
        for (std::size_t s = 0; s < VoragoVoice::kMaxSlotsPerKind; ++s) {
            if (Probe::slotAddressed(*voice, ks.kind, s)) {
                std::printf(" %zu(base=%.3f)", s, static_cast<double>(baseFor(ks.kind, s)));
            }
        }
        std::printf("\n");
        for (const double p : kQuantiles) {
            std::printf("[%s] Q(%.2f) = %.6f\n", ks.name, p, nearestRank(ks.lanes, p));
        }
        const double frac =
            (n > 0u) ? static_cast<double>(ks.aboveBase) / static_cast<double>(n) : -1.0;
        std::printf("[%s] fraction of addressed pairs with lane > current base = %.4f (%zu/%zu)\n",
                    ks.name, frac, ks.aboveBase, n);
        if (ks.kind == kResonator || ks.kind == kFeedback) {
            const double q60 = nearestRank(ks.lanes, 0.60);
            std::printf("[%s] S3.4 rule: clamp(floor(Q(0.60)/0.05)*0.05, 0.05, 0.50) = %.2f\n",
                        ks.name, retuneRule(q60));
            if (q60 < 0.05) {
                std::printf("SURVEY STOP: floor wins for %s\n", ks.name);
            }
        }
    }
    std::printf("=== end survey ===\n");
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverBaseReadBack (SC-009, FR-021, E-7)
// -----------------------------------------------------------------------------
// The shipped noise-level and loop-gain setters/getters keep round-tripping the
// BASE exactly while a lever offset is held, and the component carries
// clamp(base + span * shaped lane) - including after a rejected non-finite
// write and a same-value write (E-7 early-out).
TEST_CASE("VoragoVoice_EcosystemLeverBaseReadBack", "[systems][vorago]") {
    using Krate::DSP::FeedbackEcology;
    constexpr auto kNoise = static_cast<std::size_t>(EcosystemEngine::Kind::Noise);
    constexpr auto kFeedback = static_cast<std::size_t>(EcosystemEngine::Kind::Feedback);

    auto voice = std::make_unique<VoragoVoice>();
    voice->setSeed(Krate::DSP::deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u));
    voice->prepare(48000.0, VoragoVoiceConfig{});
    REQUIRE(voice->isPrepared());

    REQUIRE(voice->getNoiseLevelDb() == -18.0f);
    REQUIRE(voice->getEcologyLoopGain() == FeedbackEcology::kDefaultLoopGain);

    voice->noteOn(65.406f, 100.0f / 127.0f);
    Probe::injectEco(*voice, uniformLanes(1.0f));
    for (int c = 0; c < 16; ++c) {
        Probe::advanceLifeOnly(*voice);
    }

    const std::size_t sources =
        std::min(voice->noise().getNumSources(), Krate::DSP::NoiseOrganism::kMaxSources);
    const std::size_t loops =
        std::min(voice->ecology().getNumLoops(), FeedbackEcology::kMaxLoops);
    REQUIRE(sources > 0u);
    REQUIRE(loops > 0u);

    // The production expression (plan S2.3/S2.4): offset = span * shaped lane,
    // then clamp(base + offset).
    const float noiseOffset =
        Probe::noiseLevelLeverSpanDb() * Probe::shapeLeverInput(1.0f, Probe::leverInputGain(kNoise));
    const float loopOffset =
        Probe::loopGainLeverSpan() * Probe::shapeLeverInput(1.0f, Probe::leverInputGain(kFeedback));
    auto expectedNoise = [&](float dB) { return std::clamp(dB + noiseOffset, -96.0f, 12.0f); };
    auto expectedLoop = [&](float g) {
        return std::clamp(g + loopOffset, FeedbackEcology::kMinLoopGain,
                          FeedbackEcology::kMaxLoopGain);
    };

    // --- Noise level ---------------------------------------------------------
    for (const float dB : {-30.0f, -12.0f, 0.0f}) {
        CAPTURE(dB);
        voice->setNoiseLevelDb(dB);
        REQUIRE(voice->getNoiseLevelDb() == dB);
        REQUIRE(Probe::noiseLevelBase(*voice) == dB);
        Probe::advanceLifeOnly(*voice);
        REQUIRE(voice->getNoiseLevelDb() == dB);
        for (std::size_t s = 0; s < sources; ++s) {
            CAPTURE(s);
            REQUIRE(voice->noise().getSourceLevel(s) == expectedNoise(dB));
        }
    }
    {
        const float before = voice->getNoiseLevelDb();
        voice->setNoiseLevelDb(quietNaN());  // FR-071: rejected
        REQUIRE(voice->getNoiseLevelDb() == before);
        Probe::advanceLifeOnly(*voice);
        REQUIRE(voice->getNoiseLevelDb() == before);
        for (std::size_t s = 0; s < sources; ++s) {
            CAPTURE(s);
            REQUIRE(voice->noise().getSourceLevel(s) == expectedNoise(before));
        }
    }
    {
        // E-7: the same value twice leaves every component level unchanged.
        voice->setNoiseLevelDb(-12.0f);
        Probe::advanceLifeOnly(*voice);
        std::array<float, Krate::DSP::NoiseOrganism::kMaxSources> held{};
        for (std::size_t s = 0; s < sources; ++s) {
            held[s] = voice->noise().getSourceLevel(s);
        }
        voice->setNoiseLevelDb(-12.0f);
        voice->setNoiseLevelDb(-12.0f);
        REQUIRE(voice->getNoiseLevelDb() == -12.0f);
        for (std::size_t s = 0; s < sources; ++s) {
            CAPTURE(s);
            REQUIRE(voice->noise().getSourceLevel(s) == held[s]);
            REQUIRE(held[s] == expectedNoise(-12.0f));
        }
    }

    // --- Loop gain -----------------------------------------------------------
    for (const float g : {0.5f, 0.8f}) {
        CAPTURE(g);
        voice->setEcologyLoopGain(g);
        REQUIRE(voice->getEcologyLoopGain() == g);
        REQUIRE(Probe::loopGainBase(*voice) == g);
        Probe::advanceLifeOnly(*voice);
        REQUIRE(voice->getEcologyLoopGain() == g);
        for (std::size_t l = 0; l < loops; ++l) {
            CAPTURE(l);
            REQUIRE(voice->ecology().getLoopGain(l) == expectedLoop(g));
        }
    }
    {
        const float before = voice->getEcologyLoopGain();
        voice->setEcologyLoopGain(quietNaN());  // mirrors the owner's reject
        REQUIRE(voice->getEcologyLoopGain() == before);
        Probe::advanceLifeOnly(*voice);
        REQUIRE(voice->getEcologyLoopGain() == before);
        for (std::size_t l = 0; l < loops; ++l) {
            CAPTURE(l);
            REQUIRE(voice->ecology().getLoopGain(l) == expectedLoop(before));
        }
    }
    {
        voice->setEcologyLoopGain(0.5f);
        std::array<float, FeedbackEcology::kMaxLoops> held{};
        for (std::size_t l = 0; l < loops; ++l) {
            held[l] = voice->ecology().getLoopGain(l);
        }
        voice->setEcologyLoopGain(0.5f);
        REQUIRE(voice->getEcologyLoopGain() == 0.5f);
        for (std::size_t l = 0; l < loops; ++l) {
            CAPTURE(l);
            REQUIRE(voice->ecology().getLoopGain(l) == held[l]);
        }
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverReset (SC-012, FR-022, E-11, E-13)
// -----------------------------------------------------------------------------
// Every clearing path - reset(), silence() + resetForSteal(), resetForRecovery()
// and a sample-rate re-prepare (96 kHz -> 48 kHz) - returns every lever
// destination, every held offset and every wake surface of a voice that was
// driven at L = 1 for 200 chunks to EXACTLY the value a freshly prepared twin
// (same config, same seed) holds. Then, with the seam released and the
// ecosystem at depth 0, the cleared voice renders what the twin renders.
TEST_CASE("VoragoVoice_EcosystemLeverReset", "[systems][vorago]") {
    using Krate::DSP::FeedbackEcology;
    using Krate::DSP::NoiseOrganism;
    using Krate::DSP::ResonanceDriftNetwork;
    namespace TU = Krate::DSP::TestUtils;

    constexpr double kSampleRate = 48000.0;
    constexpr int kDirtyChunks = 200;
    constexpr std::size_t kBlock = 64;
    constexpr std::size_t kRenderSamples = 48000;  // 1 s at 48 kHz
    constexpr float kNoteHz = 65.406f;
    constexpr float kVelocity = 100.0f / 127.0f;
    const std::uint32_t seed =
        Krate::DSP::deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u);

    // Engine order (vorago_engine.h:346-348): seed first, then prepare.
    auto makePrepared = [&]() {
        auto v = std::make_unique<VoragoVoice>();
        v->setSeed(seed);
        v->prepare(kSampleRate, VoragoVoiceConfig{});
        return v;
    };

    // Every lever destination, held offset and wake surface, exact.
    auto requireSameLeverState = [](const VoragoVoice& a, const VoragoVoice& b) {
        for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
            CAPTURE(s);
            REQUIRE(a.noise().getSourceLevel(s) == b.noise().getSourceLevel(s));
            REQUIRE(a.noise().getSourceWakeAmount(s) == b.noise().getSourceWakeAmount(s));
            REQUIRE(Probe::noiseLevelOffset(a, s) == Probe::noiseLevelOffset(b, s));
        }
        for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
            CAPTURE(p);
            REQUIRE(a.resonance().getPeakLevel(p) == b.resonance().getPeakLevel(p));
            REQUIRE(a.resonance().getPeakWakeAmount(p) == b.resonance().getPeakWakeAmount(p));
            // L4 step 2: the slewed wander state and its destination.
            REQUIRE(a.resonance().getFreqWander(p) == b.resonance().getFreqWander(p));
            REQUIRE(Probe::freqWanderApplied(a, p) == Probe::freqWanderApplied(b, p));
        }
        for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
            CAPTURE(l);
            REQUIRE(a.ecology().getLoopGain(l) == b.ecology().getLoopGain(l));
            REQUIRE(a.ecology().getLoopWakeAmount(l) == b.ecology().getLoopWakeAmount(l));
            REQUIRE(Probe::loopGainOffset(a, l) == Probe::loopGainOffset(b, l));
            // L4 step 2: the ring coupling written and its shadow.
            REQUIRE(Probe::ringCouplingApplied(a, l) == Probe::ringCouplingApplied(b, l));
            for (std::size_t to = 0; to < FeedbackEcology::kMaxLoops; ++to) {
                REQUIRE(a.ecology().getCoupling(l, to) == b.ecology().getCoupling(l, to));
            }
        }
        // The Partial pair (FR-016) and the base shadows (FR-021).
        REQUIRE(a.cloud().getMutation() == b.cloud().getMutation());
        REQUIRE(a.bloom().getDepth() == b.bloom().getDepth());
        REQUIRE(a.getNoiseLevelDb() == b.getNoiseLevelDb());
        REQUIRE(a.getEcologyLoopGain() == b.getEcologyLoopGain());
    };

    // True iff any wake surface differs - the non-vacuity check that the
    // injected L = 1 really moved the voice away from the fresh state.
    auto anyWakeDiffers = [](const VoragoVoice& a, const VoragoVoice& b) {
        for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
            if (a.noise().getSourceWakeAmount(s) != b.noise().getSourceWakeAmount(s)) {
                return true;
            }
        }
        for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
            if (a.resonance().getPeakWakeAmount(p) != b.resonance().getPeakWakeAmount(p)) {
                return true;
            }
        }
        for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
            if (a.ecology().getLoopWakeAmount(l) != b.ecology().getLoopWakeAmount(l)) {
                return true;
            }
        }
        return false;
    };

    auto render = [&](VoragoVoice& v, std::vector<float>& outL, std::vector<float>& outR) {
        outL.assign(kRenderSamples, 0.0f);
        outR.assign(kRenderSamples, 0.0f);
        for (std::size_t i = 0; i < kRenderSamples; i += kBlock) {
            const std::size_t n = std::min(kBlock, kRenderSamples - i);
            v.processStereoBlock(outL.data() + i, outR.data() + i, n);
        }
    };

    enum class Path : std::uint8_t { Reset, Steal, Recovery, Reprepare };
    struct PathCase {
        Path path;
        const char* name;
    };
    constexpr std::array<PathCase, 4> kPaths{{
        {Path::Reset, "reset()"},
        {Path::Steal, "silence() + resetForSteal()"},
        {Path::Recovery, "resetForRecovery()"},
        {Path::Reprepare, "prepare(96000) -> prepare(48000)"},
    }};

    std::vector<float> aL;
    std::vector<float> aR;
    std::vector<float> bL;
    std::vector<float> bR;

    for (const auto& pc : kPaths) {
        CAPTURE(pc.name);

        // Voice A: driven at L = 1 on every slot of every kind for 200 chunks.
        auto a = makePrepared();
        REQUIRE(a->isPrepared());
        a->noteOn(kNoteHz, kVelocity);
        Probe::injectEco(*a, uniformLanes(1.0f));
        for (int c = 0; c < kDirtyChunks; ++c) {
            Probe::advanceLifeOnly(*a);
        }

        // Voice B: the freshly prepared twin.
        auto b = makePrepared();
        REQUIRE(b->isPrepared());
        REQUIRE(anyWakeDiffers(*a, *b));  // the dirty state is really dirty

        switch (pc.path) {
            case Path::Reset:
                (*a).reset();  // the voice's reset(), not unique_ptr::reset()
                break;
            case Path::Steal:
                a->silence();
                a->resetForSteal();
                break;
            case Path::Recovery:
                a->resetForRecovery();
                break;
            case Path::Reprepare:
                a->prepare(96000.0, VoragoVoiceConfig{});
                REQUIRE(a->isPrepared());
                a->prepare(kSampleRate, VoragoVoiceConfig{});
                REQUIRE(a->isPrepared());
                break;
        }

        requireSameLeverState(*a, *b);

        // Post-clear render at depth 0 with the seam released.
        Probe::clearInjection(*a);
        a->setEcosystemDepth(0.0f);
        b->setEcosystemDepth(0.0f);
        a->noteOn(kNoteHz, kVelocity);
        b->noteOn(kNoteHz, kVelocity);
        render(*a, aL, aR);
        render(*b, bL, bR);

        const auto cmpL = TU::compareFingerprints(
            TU::fingerprintRender(std::span<const float>(aL)),
            TU::fingerprintRender(std::span<const float>(bL)));
        const auto cmpR = TU::compareFingerprints(
            TU::fingerprintRender(std::span<const float>(aR)),
            TU::fingerprintRender(std::span<const float>(bR)));
        std::printf("[LeverReset] %-34s L metric=%.3e sample=%.3e | R metric=%.3e sample=%.3e\n",
                    pc.name, cmpL.worstMetricRelativeError,
                    static_cast<double>(cmpL.worstSampleError), cmpR.worstMetricRelativeError,
                    static_cast<double>(cmpR.worstSampleError));
        INFO("L: " << cmpL.detail);
        REQUIRE(cmpL.withinTolerance());
        INFO("R: " << cmpR.detail);
        REQUIRE(cmpR.withinTolerance());
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverLifeOnlyParity (FR-022)
// -----------------------------------------------------------------------------
// Levers are written only from applyIdentityLanes via publishIdentity(), which
// step 1 of renderOneChunk() and advanceOneChunkLifeOnly() share. So a voice
// rendered through processStereoBlock(..., 64) and a twin (same seed, same
// config, depth 1) advanced through advanceLifeOnly(64) - the same carry clock -
// hold EXACTLY equal lever destinations and wake surfaces after every chunk.
TEST_CASE("VoragoVoice_EcosystemLeverLifeOnlyParity", "[systems][vorago]") {
    using Krate::DSP::FeedbackEcology;
    using Krate::DSP::NoiseOrganism;
    using Krate::DSP::ResonanceDriftNetwork;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
    constexpr std::size_t kChunks = 30u * 48000u / kChunk;  // 30 s
    constexpr float kNoteHz = 65.406f;
    constexpr float kVelocity = 100.0f / 127.0f;
    const std::uint32_t seed =
        Krate::DSP::deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u);

    // Engine order (vorago_engine.h:346-348): seed first, then prepare.
    auto makeVoice = [&]() {
        auto v = std::make_unique<VoragoVoice>();
        v->setSeed(seed);
        v->prepare(kSampleRate, VoragoVoiceConfig{});
        v->setEcosystemDepth(1.0f);
        v->noteOn(kNoteHz, kVelocity);
        return v;
    };
    auto rendered = makeVoice();
    auto lifeOnly = makeVoice();
    REQUIRE(rendered->isPrepared());
    REQUIRE(lifeOnly->isPrepared());
    REQUIRE(rendered->getEcosystemDepth() == 1.0f);

    // The first differing surface, if any. `what == nullptr` means all equal.
    struct Mismatch {
        const char* what = nullptr;
        std::size_t index = 0;
        float rendered = 0.0f;
        float lifeOnly = 0.0f;
    };
    auto firstMismatch = [](const VoragoVoice& a, const VoragoVoice& b) {
        Mismatch m{};
        auto check = [&m](const char* what, std::size_t i, float x, float y) {
            if (m.what == nullptr && !(x == y)) {
                m = Mismatch{what, i, x, y};
            }
        };
        for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
            check("noise.getSourceLevel", s, a.noise().getSourceLevel(s),
                  b.noise().getSourceLevel(s));
            check("noise.getSourceWakeAmount", s, a.noise().getSourceWakeAmount(s),
                  b.noise().getSourceWakeAmount(s));
        }
        for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
            check("resonance.getPeakLevel", p, a.resonance().getPeakLevel(p),
                  b.resonance().getPeakLevel(p));
            check("resonance.getPeakWakeAmount", p, a.resonance().getPeakWakeAmount(p),
                  b.resonance().getPeakWakeAmount(p));
        }
        for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
            check("ecology.getLoopGain", l, a.ecology().getLoopGain(l),
                  b.ecology().getLoopGain(l));
            check("ecology.getLoopWakeAmount", l, a.ecology().getLoopWakeAmount(l),
                  b.ecology().getLoopWakeAmount(l));
        }
        // The Partial pair (FR-016).
        check("cloud.getMutation", 0u, a.cloud().getMutation(), b.cloud().getMutation());
        check("bloom.getDepth", 0u, a.bloom().getDepth(), b.bloom().getDepth());
        return m;
    };

    // Non-vacuity: the wake surfaces must actually move over the 30 s, or
    // equality of two constant states would prove nothing.
    std::array<float, NoiseOrganism::kMaxSources> prevNoiseWake{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> prevPeakWake{};
    std::array<float, FeedbackEcology::kMaxLoops> prevLoopWake{};
    auto snapshotWakes = [&](const VoragoVoice& v) {
        bool moved = false;
        for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
            const float w = v.noise().getSourceWakeAmount(s);
            moved = moved || (w != prevNoiseWake[s]);
            prevNoiseWake[s] = w;
        }
        for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
            const float w = v.resonance().getPeakWakeAmount(p);
            moved = moved || (w != prevPeakWake[p]);
            prevPeakWake[p] = w;
        }
        for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
            const float w = v.ecology().getLoopWakeAmount(l);
            moved = moved || (w != prevLoopWake[l]);
            prevLoopWake[l] = w;
        }
        return moved;
    };
    (void)snapshotWakes(*rendered);

    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    std::size_t chunksChecked = 0;
    std::size_t chunksWakeMoved = 0;
    for (std::size_t c = 0; c < kChunks; ++c) {
        rendered->processStereoBlock(outL.data(), outR.data(), kChunk);
        lifeOnly->advanceLifeOnly(kChunk);

        REQUIRE(rendered->ecosystem().getControlStepCount() ==
                lifeOnly->ecosystem().getControlStepCount());
        const Mismatch mm = firstMismatch(*rendered, *lifeOnly);
        if (mm.what != nullptr) {
            INFO("chunk " << c << ": " << mm.what << "[" << mm.index
                          << "] rendered=" << mm.rendered << " lifeOnly=" << mm.lifeOnly);
            REQUIRE(mm.rendered == mm.lifeOnly);
        }
        if (snapshotWakes(*rendered)) {
            ++chunksWakeMoved;
        }
        ++chunksChecked;
    }

    std::printf("[LeverLifeOnlyParity] seed=0x%08X chunks checked=%zu, chunks with a wake move=%zu\n",
                static_cast<unsigned>(seed), chunksChecked, chunksWakeMoved);
    REQUIRE(chunksChecked == kChunks);
    REQUIRE(chunksWakeMoved > 0u);
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverMapping (FR-015, FR-017, FR-019, FR-020)
// -----------------------------------------------------------------------------
// The injection seam covers only lanes.eco; the schedulers keep running (plan
// S2.5), so the scheduler lanes are OBSERVED through schedLanes, not forced.
// After EVERY chunk of a 10-minute life-only window per uniform lane L:
//   - each lever destination == clamp(base + span * shapeLeverInput(L, g)),
//     whatever the scheduler lanes are (FR-015 / FR-017 / FR-019);
//   - each wake surface == combineWake(base, L, S[kind][slot]) (FR-020).
// Non-vacuity per L: a chunk where the scheduler is live on a wake kind (the
// lever ignored it), a chunk where the scheduler DECIDES a wake (S > L; only
// reachable for L < 1, since getCurrentValue() <= 1 by its terminal clamp,
// slow_event_scheduler.h:339-340), and a chunk with S all zero.
TEST_CASE("VoragoVoice_EcosystemLeverMapping", "[systems][vorago]") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunks = 600u * 48000u / VoragoVoice::kControlChunkSamples;  // 10 min
    const std::uint32_t seed = leverSeed();

    for (const float L : {0.0f, 0.25f, 0.5f, 1.0f}) {
        CAPTURE(L);
        auto voice = makeLeverVoice(kSampleRate, seed);
        REQUIRE(voice->isPrepared());
        voice->noteOn(65.406f, 100.0f / 127.0f);
        voice->setEventRateScale(10.0f);
        REQUIRE(voice->getEventRateScale() == 10.0f);
        Probe::injectEco(*voice, uniformLanes(L));

        const ActiveCounts n = activeCounts(*voice);
        REQUIRE(n.sources > 0u);
        REQUIRE(n.peaks > 0u);
        REQUIRE(n.loops > 0u);

        Probe::Lanes S{};
        std::size_t chunksSchedLive = 0;    // S > 0 on some active wake-kind slot
        std::size_t chunksSchedAboveL = 0;  // S > L on some active wake-kind slot
        std::size_t chunksSchedZero = 0;    // S all zero
        for (std::size_t c = 0; c < kChunks; ++c) {
            Probe::advanceLifeOnly(*voice);
            Probe::schedLanes(*voice, S);  // valid: immediately after advanceLifeOnly

            // The bases are read back every chunk: nothing in a life-only
            // advance may move them, and the expected value must track them.
            const LeverExpect e = expectedLevers(*voice, L);
            FirstMismatch m{};
            checkLevers(*voice, n, e, m);
            checkWakes(*voice, n, L, S, m);
            if (m.what != nullptr) {
                INFO("chunk " << c << ": " << m.what << "[" << m.index << "] got=" << m.got
                              << " want=" << m.want);
                REQUIRE(m.got == m.want);
            }

            bool live = false;
            bool aboveL = false;
            bool allZero = true;
            auto scan = [&](std::size_t kind, std::size_t count) {
                for (std::size_t slot = 0; slot < count; ++slot) {
                    const float x = S[kind][slot];
                    live = live || (x > 0.0f);
                    aboveL = aboveL || (x > L);
                }
            };
            scan(kKindNoise, n.sources);
            scan(kKindResonator, n.peaks);
            scan(kKindFeedback, n.loops);
            for (const auto& row : S) {
                for (const float x : row) {
                    allZero = allZero && (x == 0.0f);
                }
            }
            chunksSchedLive += live ? 1u : 0u;
            chunksSchedAboveL += aboveL ? 1u : 0u;
            chunksSchedZero += allZero ? 1u : 0u;
        }

        {
            // L4 step 2: after 10 min at a held lane the slewed wander sits at its goal.
            const LeverExpect e = expectedLevers(*voice, L);
            FirstMismatch m{};
            checkWanderSettled(*voice, n, e, m);
            if (m.what != nullptr) {
                INFO(m.what << "[" << m.index << "] got=" << m.got << " want=" << m.want);
                REQUIRE(m.got == m.want);
            }
        }

        std::printf("[LeverMapping] seed=0x%08X L=%.2f chunks=%zu  sched-live=%zu  "
                    "sched>L=%zu  sched-all-zero=%zu\n",
                    static_cast<unsigned>(seed), static_cast<double>(L), kChunks,
                    chunksSchedLive, chunksSchedAboveL, chunksSchedZero);
        REQUIRE(chunksSchedLive >= 1u);  // FR-019: the lever ignored a live scheduler
        REQUIRE(chunksSchedZero >= 1u);
        if (L < 1.0f) {
            REQUIRE(chunksSchedAboveL >= 1u);  // FR-020: the scheduler decided a wake
        }
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverRateCompensation (FR-018b, SC-022)
// -----------------------------------------------------------------------------
// The wander lever's span follows min(1, 0.03 Hz / resonance wander rate): full
// at the shipped 0.03 Hz and below it, 3 % at the Movement macro's 1 Hz
// ceiling, and it re-settles when the rate changes mid-hold.
TEST_CASE("VoragoVoice_EcosystemLeverRateCompensation", "[systems][vorago]") {
    using Krate::DSP::ResonanceDriftNetwork;
    constexpr float kDefaultRate = ResonanceDriftNetwork::kDefaultWanderRateHz;
    REQUIRE(Probe::wanderLeverRateComp(kDefaultRate) == 1.0f);
    REQUIRE(Probe::wanderLeverRateComp(ResonanceDriftNetwork::kMinWanderRateHz) == 1.0f);
    const float k = Probe::wanderLeverRateCompExponent();
    REQUIRE(k > 0.0f);
    REQUIRE(k <= 1.0f);
    REQUIRE(Probe::wanderLeverRateComp(1.0f) == std::pow(kDefaultRate / 1.0f, k));
    REQUIRE(Probe::wanderLeverRateComp(0.3f) == std::pow(kDefaultRate / 0.3f, k));

    constexpr double kSampleRate = 48000.0;
    // 2 s per hold: the full-span slew is 50 ms (kFreqWanderLeverSlewSeconds).
    constexpr std::size_t kHoldChunks = 2u * 48000u / VoragoVoice::kControlChunkSamples;
    auto voice = makeLeverVoice(kSampleRate, leverSeed());
    REQUIRE(voice->isPrepared());
    voice->noteOn(65.406f, 100.0f / 127.0f);
    Probe::injectEco(*voice, uniformLanes(1.0f));
    const ActiveCounts n = activeCounts(*voice);
    REQUIRE(n.peaks > 0u);

    auto holdAndCheck = [&](float rateHz) {
        CAPTURE(rateHz);
        voice->setResonanceWanderRate(rateHz);
        REQUIRE(voice->getResonanceWanderRate() == rateHz);
        for (std::size_t c = 0; c < kHoldChunks; ++c) {
            Probe::advanceLifeOnly(*voice);
        }
        const LeverExpect e = expectedLevers(*voice, 1.0f);
        FirstMismatch m{};
        checkWanderSettled(*voice, n, e, m);
        if (m.what != nullptr) {
            INFO(m.what << "[" << m.index << "] got=" << m.got << " want=" << m.want);
            REQUIRE(m.got == m.want);
        }
        return e.wanderGoal;
    };

    const float atDefault = holdAndCheck(kDefaultRate);
    const float atCeiling = holdAndCheck(ResonanceDriftNetwork::kMaxWanderRateHz);
    const float atMiddle = holdAndCheck(0.3f);
    const float backAtDefault = holdAndCheck(kDefaultRate);
    const float belowDefault = holdAndCheck(0.01f);
    std::printf("[LeverRateComp] settled wander: 0.03 Hz %.4f  1 Hz %.4f  0.3 Hz %.4f  "
                "0.03 Hz %.4f  0.01 Hz %.4f  (base %.2f, span %.2f)\n",
                static_cast<double>(atDefault), static_cast<double>(atCeiling),
                static_cast<double>(atMiddle), static_cast<double>(backAtDefault),
                static_cast<double>(belowDefault),
                static_cast<double>(Probe::freqWanderBaseSemis()),
                static_cast<double>(Probe::freqWanderLeverSpanSemis()));
    REQUIRE(atDefault > Probe::freqWanderBaseSemis());
    REQUIRE(atCeiling < atMiddle);
    REQUIRE(atMiddle < atDefault);
    REQUIRE(backAtDefault == atDefault);
    REQUIRE(belowDefault == atDefault);
    // The lever at 1 Hz adds comp(1 Hz) of the span: the Phase 10 zipper bound's room.
    REQUIRE(atCeiling - Probe::freqWanderBaseSemis() <=
            Probe::wanderLeverRateComp(1.0f) * Probe::freqWanderLeverSpanSemis() *
                    Probe::shapeLeverInput(1.0f, Probe::leverInputGain(kKindResonator)) + 1e-5f);
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverNeutral (SC-006, SC-018)
// -----------------------------------------------------------------------------
// At ecosystem depth 0 every lever destination reads its base EXACTLY on every
// chunk of a 10-minute life-only window, at three sample rates, while the
// schedulers (as shipped) keep firing events.
TEST_CASE("VoragoVoice_EcosystemLeverNeutral", "[systems][vorago]") {
    const std::uint32_t seed = leverSeed();

    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        CAPTURE(fs);
        auto voice = makeLeverVoice(fs, seed);
        REQUIRE(voice->isPrepared());
        voice->setEcosystemDepth(0.0f);
        REQUIRE(voice->getEcosystemDepth() == 0.0f);
        voice->noteOn(65.406f, 100.0f / 127.0f);

        const ActiveCounts n = activeCounts(*voice);
        REQUIRE(n.sources > 0u);
        REQUIRE(n.peaks > 0u);
        REQUIRE(n.loops > 0u);

        const std::size_t chunks =
            static_cast<std::size_t>(fs) * 600u / VoragoVoice::kControlChunkSamples;
        std::array<bool, VoragoVoice::kNumEventSchedulers> wasActive{};
        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            wasActive[k] = voice->scheduler(k).isEventActive();
        }
        std::size_t events = 0;
        for (std::size_t c = 0; c < chunks; ++c) {
            Probe::advanceLifeOnly(*voice);
            FirstMismatch m{};
            checkNeutral(*voice, n, m);
            if (m.what != nullptr) {
                INFO("chunk " << c << ": " << m.what << "[" << m.index << "] got=" << m.got
                              << " want=" << m.want);
                REQUIRE(m.got == m.want);
            }
            for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
                const bool active = voice->scheduler(k).isEventActive();
                events += (active && !wasActive[k]) ? 1u : 0u;
                wasActive[k] = active;
            }
        }
        std::printf("[LeverNeutral] fs=%.0f seed=0x%08X chunks=%zu scheduler events seen=%zu\n",
                    fs, static_cast<unsigned>(seed), chunks, events);
        REQUIRE(events >= 1u);
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverSchedulerBlind (SC-007)
// -----------------------------------------------------------------------------
// Depth 0, events 10x as frequent: every one of the three wake families fires
// at least once (a seed that misses a family FAILS, it is never skipped), and
// the levers still read their bases exactly on every chunk - the scheduler
// never reaches a lever.
TEST_CASE("VoragoVoice_EcosystemLeverSchedulerBlind", "[systems][vorago]") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunks = 600u * 48000u / VoragoVoice::kControlChunkSamples;  // 10 min
    const std::uint32_t seed = leverSeed();

    auto voice = makeLeverVoice(kSampleRate, seed);
    REQUIRE(voice->isPrepared());
    voice->setEcosystemDepth(0.0f);
    voice->setEventRateScale(10.0f);
    REQUIRE(voice->getEcosystemDepth() == 0.0f);
    REQUIRE(voice->getEventRateScale() == 10.0f);
    voice->noteOn(65.406f, 100.0f / 127.0f);

    const ActiveCounts n = activeCounts(*voice);
    REQUIRE(n.sources > 0u);
    REQUIRE(n.peaks > 0u);
    REQUIRE(n.loops > 0u);

    // isEventActive() rising edges, and lastEventTarget moves, per family.
    std::array<std::size_t, VoragoVoice::kNumEventFamilies> onsets{};
    std::array<std::size_t, VoragoVoice::kNumEventFamilies> targetChanges{};
    std::array<bool, VoragoVoice::kNumEventSchedulers> wasActive{};
    std::array<std::uint8_t, VoragoVoice::kNumEventSchedulers> lastTarget{};
    for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
        wasActive[k] = voice->scheduler(k).isEventActive();
        lastTarget[k] = Probe::lastEventTarget(*voice, k);
    }

    for (std::size_t c = 0; c < kChunks; ++c) {
        Probe::advanceLifeOnly(*voice);
        for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
            const auto& sched = voice->scheduler(k);
            const bool active = sched.isEventActive();
            if (active && !wasActive[k]) {
                const std::uint8_t family = sched.getActiveTarget();
                REQUIRE(static_cast<std::size_t>(family) < VoragoVoice::kNumEventFamilies);
                ++onsets[family];
                // The voice latched the same onset in this chunk's publish.
                REQUIRE(Probe::lastEventTarget(*voice, k) == family);
            }
            wasActive[k] = active;
            const std::uint8_t t = Probe::lastEventTarget(*voice, k);
            if (t != lastTarget[k]) {
                if (static_cast<std::size_t>(t) < VoragoVoice::kNumEventFamilies) {
                    ++targetChanges[t];
                }
                lastTarget[k] = t;
            }
        }
        FirstMismatch m{};
        checkNeutral(*voice, n, m);
        if (m.what != nullptr) {
            INFO("chunk " << c << ": " << m.what << "[" << m.index << "] got=" << m.got
                          << " want=" << m.want);
            REQUIRE(m.got == m.want);
        }
    }

    constexpr std::array<const char*, VoragoVoice::kNumEventFamilies> kNames{
        "BloomTrigger", "NoiseWake", "PeakWake", "LoopWake", "GhostBurst"};
    std::printf("[LeverSchedulerBlind] seed=0x%08X chunks=%zu rateScale=10\n",
                static_cast<unsigned>(seed), kChunks);
    for (std::size_t f = 0; f < VoragoVoice::kNumEventFamilies; ++f) {
        std::printf("[LeverSchedulerBlind]   %-12s onsets=%zu lastEventTarget changes=%zu\n",
                    kNames[f], onsets[f], targetChanges[f]);
    }
    using Family = VoragoVoice::EventFamily;
    REQUIRE(onsets[static_cast<std::size_t>(Family::NoiseWake)] >= 1u);
    REQUIRE(onsets[static_cast<std::size_t>(Family::PeakWake)] >= 1u);
    REQUIRE(onsets[static_cast<std::size_t>(Family::LoopWake)] >= 1u);
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLaneShapingFidelity (SC-021, FR-019a, FR-016)
// -----------------------------------------------------------------------------
// FR-019a's lane shaping touches ONLY the input of the three FR-015 levers:
// shapeLeverInput maps 0 -> 0 exactly and [0, 1] into [0, 1] monotonically,
// while the wake combine and the Partial / Ghost writes keep the RAW lane.
TEST_CASE("VoragoVoice_EcosystemLaneShapingFidelity", "[systems][vorago]") {
    static_assert(Probe::leverInputGain(kKindPartial) == 1.0f &&
                      Probe::leverInputGain(kKindGhost) == 1.0f,
                  "FR-016 / FR-019a: Partial and Ghost are never shaped");

    // Read through a volatile so neither arm below is constant-folded into
    // dead code while the shipped gains are all 1 (MSVC C4702).
    bool shapingUsed = false;
    for (std::size_t k = 0; k < EcosystemEngine::kNumKinds; ++k) {
        volatile float opaque = Probe::leverInputGain(k);
        const float g = opaque;
        shapingUsed = shapingUsed || (g != 1.0f);
    }
    if (!shapingUsed) {
        SUCCEED("shaping not used");
        return;
    }

    // --- The shaping function's own contract, per shaped kind ----------------
    constexpr std::array<std::size_t, 3> kShaped{kKindNoise, kKindResonator, kKindFeedback};
    for (const std::size_t kind : kShaped) {
        CAPTURE(kind);
        const float g = Probe::leverInputGain(kind);
        CAPTURE(g);
        REQUIRE(Probe::shapeLeverInput(0.0f, g) == 0.0f);
        std::size_t outOfRange = 0;
        std::size_t decreasing = 0;
        float prev = Probe::shapeLeverInput(0.0f, g);
        for (int i = 0; i <= 10000; ++i) {
            const float x = static_cast<float>(i) / 10000.0f;
            const float y = Probe::shapeLeverInput(x, g);
            outOfRange += (y >= 0.0f && y <= 1.0f) ? 0u : 1u;
            decreasing += (y < prev) ? 1u : 0u;
            prev = y;
        }
        REQUIRE(outOfRange == 0u);
        REQUIRE(decreasing == 0u);
    }

    // --- The raw lane reaches the combine and the Partial / Ghost writes -----
    auto voice = makeLeverVoice(48000.0, leverSeed());
    REQUIRE(voice->isPrepared());
    voice->noteOn(65.406f, 100.0f / 127.0f);
    const ActiveCounts n = activeCounts(*voice);
    REQUIRE(n.sources > 0u);
    REQUIRE(n.peaks > 0u);
    REQUIRE(n.loops > 0u);

    Probe::Lanes S{};
    for (const float raw : {0.1f, 0.3f, 0.55f, 0.8f, 1.0f}) {
        CAPTURE(raw);
        Probe::injectEco(*voice, uniformLanes(raw));  // every kind, Partial included
        Probe::advanceLifeOnly(*voice);
        Probe::schedLanes(*voice, S);  // valid: immediately after advanceLifeOnly

        FirstMismatch m{};
        checkWakes(*voice, n, raw, S, m);                        // RAW lane in the combine
        checkLevers(*voice, n, expectedLevers(*voice, raw), m);  // shaped lane in the lever
        m.check("cloud.getMutation", 0u, voice->cloud().getMutation(),
                std::clamp(Probe::mutationBase(*voice) + raw, 0.0f, 1.0f));
        m.check("bloom.getDepth", 0u, voice->bloom().getDepth(),
                std::clamp(Probe::bloomDepthBase(*voice) + raw, 0.0f, 1.0f));
        m.check("getGhostRequest", 0u, voice->getGhostRequest(),
                VoragoVoice::combineWake(0.0f, raw, S[kKindGhost][0]));
        if (m.what != nullptr) {
            INFO(m.what << "[" << m.index << "] got=" << m.got << " want=" << m.want);
            REQUIRE(m.got == m.want);
        }
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_RouteLeverZeroAtZeroLane (Phase 13c FR-012, SC-009; 13b FR-017 / FR-019)
// -----------------------------------------------------------------------------
// Owning spec: specs/vorago-phase13c-capability-audibility (tasks.md T028).
// The two L2 route sizes (kPartialLaneGain on mutation and bloom depth,
// kGhostLaneGain on the ghost request's eco term) are ZERO at a zero lane:
// with every eco lane injected at 0, mutation and bloom depth read their bases
// and the ghost request reads the scheduler term alone, exactly, at whatever
// gains are compiled. At a nonzero raw lane the eco term is scaled by the gain:
//   mutation   == clamp(mutationBase   + kPartialLaneGain * raw, 0, 1)
//   bloomDepth == clamp(bloomDepthBase + kPartialLaneGain * raw, 0, 1)
//   ghost      == combineWake(0, min(1, kGhostLaneGain * raw), sched)
// The sched term is untouched (FR-019: scheduler-blind route sizes).
TEST_CASE("VoragoVoice_RouteLeverZeroAtZeroLane", "[systems][vorago]") {
    auto voice = makeLeverVoice(48000.0, leverSeed());
    REQUIRE(voice->isPrepared());
    voice->noteOn(65.406f, 100.0f / 127.0f);

    const float partialGain = Probe::partialLaneGain();
    const float ghostGain = Probe::ghostLaneGain();
    CAPTURE(partialGain, ghostGain);

    Probe::Lanes S{};

    // --- 1. Zero lane: every route reads its base / the sched term exactly ---
    Probe::injectEco(*voice, uniformLanes(0.0f));
    Probe::advanceLifeOnly(*voice);
    Probe::schedLanes(*voice, S);  // valid: immediately after advanceLifeOnly
    REQUIRE(voice->cloud().getMutation() == Probe::mutationBase(*voice));
    REQUIRE(voice->bloom().getDepth() == Probe::bloomDepthBase(*voice));
    REQUIRE(voice->getGhostRequest() ==
            VoragoVoice::combineWake(0.0f, 0.0f, S[kKindGhost][0]));

    // --- 2. Nonzero raw lane: the eco term scales by the compiled gain -------
    for (const float raw : {0.1f, 0.5f, 1.0f}) {
        CAPTURE(raw);
        Probe::injectEco(*voice, uniformLanes(raw));
        Probe::advanceLifeOnly(*voice);
        Probe::schedLanes(*voice, S);  // valid: immediately after advanceLifeOnly
        REQUIRE(voice->cloud().getMutation() ==
                std::clamp(Probe::mutationBase(*voice) + partialGain * raw, 0.0f, 1.0f));
        REQUIRE(voice->bloom().getDepth() ==
                std::clamp(Probe::bloomDepthBase(*voice) + partialGain * raw, 0.0f, 1.0f));
        REQUIRE(voice->getGhostRequest() ==
                VoragoVoice::combineWake(0.0f, std::min(1.0f, ghostGain * raw),
                                         S[kKindGhost][0]));
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_CeilingLeverNeutral (Phase 13d T-M3; FR-012 (a), plan s4.1)
// -----------------------------------------------------------------------------
// Owning spec: specs/vorago-phase13d-engine-ceilings (tasks.md T009).
// The five route seams install the compiled values at prepare() (so a voice
// nobody tweaks renders exactly as before), a zero lane still reads every base
// exactly, a set seam is what applyIdentityLanes reads, and every float seam
// rejects non-finite input and clamps.
TEST_CASE("VoragoVoice_CeilingLeverNeutral", "[systems][vorago]") {
    auto voice = makeLeverVoice(48000.0, leverSeed());
    REQUIRE(voice->isPrepared());
    voice->noteOn(65.406f, 100.0f / 127.0f);

    // --- 1. prepare() installs the compiled values -----------------------------
    REQUIRE(voice->getPartialBloomLaneGain() == 1.0f);
    REQUIRE(voice->getPartialMutationLaneGain() == 1.0f);
    REQUIRE(voice->getLoopGainLeverSpan() == 0.18f);
    REQUIRE(voice->getCouplingLeverSpan() == 0.30f);
    REQUIRE(voice->getLoopWakeBase() == VoragoVoice::kLoopWakeBase);
    REQUIRE(voice->getBloomParentCount() == 4u);
    REQUIRE(voice->getBloomChildrenPerEvent() == 2u);

    const std::size_t loops = voice->ecology().getNumLoops();
    REQUIRE(loops > 0u);
    Probe::Lanes S{};

    // --- 2. Zero lane: every route reads its base exactly ----------------------
    Probe::injectEco(*voice, uniformLanes(0.0f));
    Probe::advanceLifeOnly(*voice);
    REQUIRE(voice->cloud().getMutation() == Probe::mutationBase(*voice));
    REQUIRE(voice->bloom().getDepth() == Probe::bloomDepthBase(*voice));
    for (std::size_t l = 0; l < loops; ++l) {
        CAPTURE(l);
        REQUIRE(Probe::loopGainOffset(*voice, l) == 0.0f);
        REQUIRE(Probe::ringCouplingApplied(*voice, l) == Probe::ringCouplingBase());
    }

    // --- 3. Set seams: applyIdentityLanes reads them ---------------------------
    voice->setPartialBloomLaneGain(2.5f);
    voice->setPartialMutationLaneGain(0.5f);
    voice->setLoopGainLeverSpan(0.26f);
    voice->setCouplingLeverSpan(0.40f);
    voice->setLoopWakeBase(0.15f);
    for (const float raw : {0.1f, 0.5f, 1.0f}) {
        CAPTURE(raw);
        Probe::injectEco(*voice, uniformLanes(raw));
        Probe::advanceLifeOnly(*voice);
        Probe::schedLanes(*voice, S);  // valid: immediately after advanceLifeOnly
        const float x = std::clamp(Probe::leverInputGain(kKindFeedback) * raw, 0.0f, 1.0f);
        REQUIRE(voice->cloud().getMutation() ==
                std::clamp(Probe::mutationBase(*voice) + 0.5f * raw, 0.0f, 1.0f));
        REQUIRE(voice->bloom().getDepth() ==
                std::clamp(Probe::bloomDepthBase(*voice) + 2.5f * raw, 0.0f, 1.0f));
        for (std::size_t l = 0; l < loops; ++l) {
            CAPTURE(l);
            REQUIRE(Probe::loopGainOffset(*voice, l) == 0.26f * x);
            REQUIRE(Probe::ringCouplingApplied(*voice, l) ==
                    std::clamp(Probe::ringCouplingBase() + 0.40f * x, 0.0f, 0.5f));
            REQUIRE(voice->ecology().getLoopWakeAmount(l) ==
                    VoragoVoice::combineWake(0.15f, raw, S[kKindFeedback][l]));
        }
    }

    // --- 4. Clamps and non-finite rejection -------------------------------------
    voice->setLoopWakeBase(0.0f);
    REQUIRE(voice->getLoopWakeBase() == 0.05f);  // E-8: kMinRetunedWakeBase
    voice->setLoopWakeBase(2.0f);
    REQUIRE(voice->getLoopWakeBase() == 1.0f);
    voice->setPartialBloomLaneGain(9.0f);
    REQUIRE(voice->getPartialBloomLaneGain() == 8.0f);

    voice->setPartialBloomLaneGain(2.5f);
    voice->setPartialMutationLaneGain(0.5f);
    voice->setLoopWakeBase(0.15f);
    voice->setLoopGainLeverSpan(0.26f);
    voice->setCouplingLeverSpan(0.40f);
    for (const std::uint32_t bits : {0x7FC00000u, 0x7F800000u}) {
        CAPTURE(bits);
        const float bad = std::bit_cast<float>(bits);
        voice->setPartialBloomLaneGain(bad);
        voice->setPartialMutationLaneGain(bad);
        voice->setLoopWakeBase(bad);
        voice->setLoopGainLeverSpan(bad);
        voice->setCouplingLeverSpan(bad);
        REQUIRE(voice->getPartialBloomLaneGain() == 2.5f);
        REQUIRE(voice->getPartialMutationLaneGain() == 0.5f);
        REQUIRE(voice->getLoopWakeBase() == 0.15f);
        REQUIRE(voice->getLoopGainLeverSpan() == 0.26f);
        REQUIRE(voice->getCouplingLeverSpan() == 0.40f);
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverAttribution (SC-008, SC-018; plan S3.3, S5.2)
// -----------------------------------------------------------------------------
// At depth 1, after a 155 s warm-up (life-only), for each lever kind K in
// {Noise, Resonator, Feedback}:
//   (1) a clean minute: EVERY kind's lever makes >= 3 value changes, each at
//       least the lever's minimum step (level 0.5 dB, loop gain 0.01);
//   (2) a dormant minute: every agent of kind K is set dormant; after T_settle
//       (plan S3.3, from the LIVE getStepIntervalChunks(), printed) K's lever
//       reads EXACTLY its base on every chunk to the minute's end, while every
//       other kind's lever still makes >= 3 changes in that minute. Then K is
//       un-dormanted and the next kind follows.
// A "change" is a chunk in which at least one active slot of the lever moved
// >= the minimum step away from that slot's last counted value.
// T_lever = 0: the level / loop-gain getters return the stored TARGET
// (noise_organism.h:872, resonance_drift_network.h:847, feedback_ecology.h:1406).
// Coupling / freq-wander levers are not shipped (plan S2.8 not applied), so
// they are not counted. A lever that never reaches 3 changes/min is a finding
// against the spans (plan S3.5), never a test edit.
TEST_CASE("VoragoVoice_EcosystemLeverAttribution", "[systems][vorago]") {
    constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
    constexpr std::size_t kNumLevers = 3;
    constexpr std::array<std::size_t, kNumLevers> kLeverKind{kKindNoise, kKindResonator,
                                                             kKindFeedback};
    constexpr std::array<const char*, kNumLevers> kLeverName{
        "Noise source level", "Resonator peak level", "Feedback loop gain"};
    constexpr std::array<float, kNumLevers> kMinStep{0.5f, 0.5f, 0.01f};  // dB, dB, gain
    constexpr std::size_t kRequiredChanges = 3;
    const std::uint32_t seed = leverSeed();

    // The lever destination of lever `k` on slot `s`.
    auto leverValue = [](const VoragoVoice& v, std::size_t k, std::size_t s) -> float {
        if (k == 0u) {
            return v.noise().getSourceLevel(s);
        }
        if (k == 1u) {
            return v.resonance().getPeakLevel(s);
        }
        return v.ecology().getLoopGain(s);
    };
    // The lever's base: its lane-0 expected value, the same float expression as
    // production (expectedLevers above).
    auto leverBase = [](const VoragoVoice& v, std::size_t k) -> float {
        const LeverExpect e = expectedLevers(v, 0.0f);
        if (k == 0u) {
            return e.noise;
        }
        if (k == 1u) {
            return e.peak;
        }
        return e.loop;
    };

    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        CAPTURE(fs);
        auto voice = makeLeverVoice(fs, seed);
        REQUIRE(voice->isPrepared());
        voice->setEcosystemDepth(1.0f);
        REQUIRE(voice->getEcosystemDepth() == 1.0f);
        voice->noteOn(65.406f, 100.0f / 127.0f);

        const ActiveCounts n = activeCounts(*voice);
        REQUIRE(n.sources > 0u);
        REQUIRE(n.peaks > 0u);
        REQUIRE(n.loops > 0u);
        const std::array<std::size_t, kNumLevers> slots{n.sources, n.peaks, n.loops};

        EcosystemEngine& eco = Probe::ecosystem(*voice);
        const std::size_t agents = std::min(eco.getAgentCount(), EcosystemEngine::kMaxAgents);
        for (std::size_t k = 0; k < kNumLevers; ++k) {
            CAPTURE(kLeverName[k]);
            std::size_t ofKind = 0;
            for (std::size_t i = 0; i < agents; ++i) {
                ofKind +=
                    (static_cast<std::size_t>(eco.getAgentKind(i)) == kLeverKind[k]) ? 1u : 0u;
            }
            REQUIRE(ofKind > 0u);  // dormancy of an empty kind would be vacuous
        }

        // T_settle (plan S3.3), from the live step interval.
        const double dt = static_cast<double>(eco.getStepIntervalChunks()) *
                          static_cast<double>(kChunk) / fs;
        REQUIRE(dt > 0.0);
        const double rampSteps = std::max(1.0, std::ceil(0.050 / dt));
        const double tLever = 0.0;  // targets, not ramps (see header comment)
        const double tSettle = (rampSteps + 1.0) * dt + tLever;
        const auto settleChunks =
            static_cast<std::size_t>(std::ceil(tSettle * fs / static_cast<double>(kChunk)));
        const auto warmupChunks =
            static_cast<std::size_t>(fs * 155.0 / static_cast<double>(kChunk));
        const auto minuteChunks =
            static_cast<std::size_t>(fs * 60.0 / static_cast<double>(kChunk));
        std::printf("[LeverAttribution] fs=%.0f seed=0x%08X stepChunks=%zu dt=%.4f ms "
                    "rampSteps=%.0f T_settle=%.4f ms (%zu chunks)\n",
                    fs, static_cast<unsigned>(seed), eco.getStepIntervalChunks(), dt * 1000.0,
                    rampSteps, tSettle * 1000.0, settleChunks);
        REQUIRE(settleChunks < minuteChunks);

        for (std::size_t c = 0; c < warmupChunks; ++c) {
            Probe::advanceLifeOnly(*voice);
        }

        // One minute: per lever, the number of chunks with a change. Lever
        // `dormantLever` (kNumLevers for none) must also read its base on every
        // chunk once settleChunks chunks have run.
        auto runMinute = [&](std::size_t dormantLever) {
            std::array<std::array<float, VoragoVoice::kMaxSlotsPerKind>, kNumLevers> ref{};
            for (std::size_t k = 0; k < kNumLevers; ++k) {
                for (std::size_t s = 0; s < slots[k]; ++s) {
                    ref[k][s] = leverValue(*voice, k, s);
                }
            }
            std::array<std::size_t, kNumLevers> changes{};
            for (std::size_t c = 0; c < minuteChunks; ++c) {
                Probe::advanceLifeOnly(*voice);
                for (std::size_t k = 0; k < kNumLevers; ++k) {
                    bool changed = false;
                    for (std::size_t s = 0; s < slots[k]; ++s) {
                        const float x = leverValue(*voice, k, s);
                        if (std::abs(x - ref[k][s]) >= kMinStep[k]) {
                            changed = true;
                            ref[k][s] = x;
                        }
                    }
                    changes[k] += changed ? 1u : 0u;
                }
                if (dormantLever < kNumLevers && c + 1u >= settleChunks) {
                    const float base = leverBase(*voice, dormantLever);
                    FirstMismatch m{};
                    for (std::size_t s = 0; s < slots[dormantLever]; ++s) {
                        m.check(kLeverName[dormantLever], s,
                                leverValue(*voice, dormantLever, s), base);
                    }
                    if (m.what != nullptr) {
                        INFO("dormant minute, chunk " << c << " (settle " << settleChunks
                                                      << " chunks): " << m.what << "["
                                                      << m.index << "] got=" << m.got
                                                      << " base=" << m.want);
                        REQUIRE(m.got == m.want);
                    }
                }
            }
            return changes;
        };

        auto setKindDormant = [&](std::size_t lever, bool on) {
            for (std::size_t i = 0; i < agents; ++i) {
                if (static_cast<std::size_t>(eco.getAgentKind(i)) == kLeverKind[lever]) {
                    eco.setAgentDormant(i, on);
                }
            }
        };

        for (std::size_t K = 0; K < kNumLevers; ++K) {
            CAPTURE(kLeverName[K]);

            // (1) The clean minute before K.
            const auto clean = runMinute(kNumLevers);
            std::printf("[LeverAttribution] fs=%.0f before %-20s changes/min: noise=%zu "
                        "peak=%zu loop=%zu\n",
                        fs, kLeverName[K], clean[0], clean[1], clean[2]);
            for (std::size_t k = 0; k < kNumLevers; ++k) {
                CAPTURE(kLeverName[k]);
                REQUIRE(clean[k] >= kRequiredChanges);
            }

            // (2) K dormant for one minute.
            setKindDormant(K, true);
            const auto dormant = runMinute(K);
            std::printf("[LeverAttribution] fs=%.0f %-20s dormant, changes/min: noise=%zu "
                        "peak=%zu loop=%zu (at base after %zu chunks)\n",
                        fs, kLeverName[K], dormant[0], dormant[1], dormant[2], settleChunks);
            for (std::size_t k = 0; k < kNumLevers; ++k) {
                if (k == K) {
                    continue;
                }
                CAPTURE(kLeverName[k]);
                REQUIRE(dormant[k] >= kRequiredChanges);
            }

            // Un-dormant K and continue.
            setKindDormant(K, false);
        }
    }
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemWakeUnmasked (SC-020, FR-018; plan S3.4, S5.2)
// -----------------------------------------------------------------------------
// The S3.4 surface (48 kHz, the default voice seed, FR-090 depth 0.85, MIDI 36
// at velocity 100/127), life-only to 340 s. On every simulation step in
// [155 s, 340 s], over (step, slot) pairs whose slot is addressed by at least
// one valid agent (Q6): the fraction whose eco lane exceeds the SHIPPED wake
// base. Resonator and Feedback must reach 25 %; Noise is printed only (Q1).
TEST_CASE("VoragoVoice_EcosystemWakeUnmasked", "[systems][vorago]") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
    constexpr std::size_t kTotalChunks = 340u * 48000u / kChunk;       // 340 s
    constexpr std::size_t kWindowStartChunk = 155u * 48000u / kChunk;  // 155 s
    constexpr double kRequiredFraction = 0.25;

    const std::uint32_t seed = leverSeed();
    auto voice = makeLeverVoice(kSampleRate, seed);
    REQUIRE(voice->isPrepared());
    REQUIRE(voice->getEcosystemDepth() == 0.85f);  // FR-090 default, as prepared
    voice->noteOn(65.406f, 100.0f / 127.0f);

    struct KindTally {
        const char* name;
        std::size_t kind;
        std::size_t pairs;
        std::size_t above;
    };
    std::array<KindTally, 3> tally{{
        {"Resonator", kKindResonator, 0u, 0u},
        {"Feedback", kKindFeedback, 0u, 0u},
        {"Noise", kKindNoise, 0u, 0u},
    }};

    auto baseFor = [&](std::size_t kind, std::size_t slot) -> float {
        if (kind == kKindResonator) {
            return Probe::peakWakeBase(*voice, slot);
        }
        if (kind == kKindFeedback) {
            return Probe::loopWakeBase(*voice, slot);
        }
        return Probe::noiseWakeBase(*voice, slot);
    };

    Probe::Lanes lanes{};
    std::uint64_t lastStep = voice->ecosystem().getControlStepCount();
    std::size_t stepsSurveyed = 0;
    for (std::size_t c = 0; c < kTotalChunks; ++c) {
        Probe::advanceLifeOnly(*voice);
        const std::uint64_t step = voice->ecosystem().getControlStepCount();
        const bool stepped = step != lastStep;
        lastStep = step;
        if (!stepped || c < kWindowStartChunk) {
            continue;
        }
        ++stepsSurveyed;
        Probe::gatherEco(*voice, lanes);
        for (auto& t : tally) {
            for (std::size_t s = 0; s < VoragoVoice::kMaxSlotsPerKind; ++s) {
                if (!Probe::slotAddressed(*voice, t.kind, s)) {
                    continue;
                }
                ++t.pairs;
                t.above += (lanes[t.kind][s] > baseFor(t.kind, s)) ? 1u : 0u;
            }
        }
    }

    // The three shipped bases and the three fractions are printed FIRST.
    std::printf("\n[WakeUnmasked] fs=%.0f seed=0x%08X depth=%.2f window=[155 s, 340 s] "
                "steps surveyed=%zu\n",
                kSampleRate, static_cast<unsigned>(seed),
                static_cast<double>(voice->getEcosystemDepth()), stepsSurveyed);
    std::printf("[WakeUnmasked] shipped bases: peakWakeBase=%.3f loopWakeBase=%.3f "
                "noiseWakeBase=%.3f\n",
                static_cast<double>(Probe::peakWakeBase(*voice, 0)),
                static_cast<double>(Probe::loopWakeBase(*voice, 0)),
                static_cast<double>(Probe::noiseWakeBase(*voice, 0)));
    std::array<double, 3> fraction{};
    for (std::size_t i = 0; i < tally.size(); ++i) {
        const KindTally& t = tally[i];
        fraction[i] = (t.pairs > 0u)
                          ? static_cast<double>(t.above) / static_cast<double>(t.pairs)
                          : -1.0;
        std::printf("[WakeUnmasked] %-9s fraction above shipped base = %.4f (%zu/%zu)%s\n",
                    t.name, fraction[i], t.above, t.pairs,
                    (t.kind == kKindNoise) ? "  [recorded, not gated]" : "");
    }

    REQUIRE(stepsSurveyed > 0u);
    REQUIRE(tally[0].pairs > 0u);
    REQUIRE(tally[1].pairs > 0u);
    CAPTURE(fraction[0], fraction[1], fraction[2]);
    REQUIRE(fraction[0] >= kRequiredFraction);  // Resonator
    REQUIRE(fraction[1] >= kRequiredFraction);  // Feedback
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverClickFree (SC-010(a); plan S5.2, tasks T022)
// -----------------------------------------------------------------------------
// 48 kHz, one voice (fixed seed), fast attack (FR-014a applyFastAttack), a 20 s
// warm-up. Then per K in {Noise, Resonator, Feedback, Partial, Ghost}, one at a
// time and finally all together: 1 s at 0, 1 s at 1 on every slot of K, 1 s at
// 0 - the injected eco lane stepping hard on and off. The twin renders the same
// seed and schedule with the injection held at 0 throughout. Both arms inject
// 0 from before noteOn, so they differ ONLY in the 1 s "at 1" segments.
// REQUIRE clicks(stress) <= clicks(twin) on L and R separately (pinned
// ClickDetector config), counted over the scheduled 18 s.
TEST_CASE("VoragoVoice_EcosystemLeverClickFree", "[systems][vorago]") {
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;
    using Krate::DSP::TestUtils::Vorago::applyFastAttack;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
    constexpr std::size_t kWarmupSamples = 20u * 48000u;  // 20 s
    constexpr std::size_t kSegmentSamples = 48000u;       // 1 s
    constexpr std::size_t kNumPhases = 6;                 // 5 kinds, then all
    constexpr std::size_t kSegmentsPerPhase = 3;          // 0, 1, 0
    constexpr std::size_t kScheduledSamples = kNumPhases * kSegmentsPerPhase * kSegmentSamples;
    static_assert(kWarmupSamples % kChunk == 0u && kSegmentSamples % kChunk == 0u,
                  "whole control chunks only, so both arms step identically");
    constexpr std::array<std::size_t, 5> kKinds{kKindNoise, kKindResonator, kKindFeedback,
                                                kKindPartial, kKindGhost};
    constexpr std::array<const char*, kNumPhases> kPhaseName{"Noise",   "Resonator", "Feedback",
                                                             "Partial", "Ghost",     "All"};
    const std::uint32_t seed = leverSeed();

    auto renderArm = [&](bool stress, std::vector<float>& l, std::vector<float>& r) {
        auto voice = makeLeverVoice(kSampleRate, seed);
        REQUIRE(voice->isPrepared());
        applyFastAttack(*voice);
        Probe::injectEco(*voice, uniformLanes(0.0f));
        voice->noteOn(65.406f, 100.0f / 127.0f);

        std::array<float, kChunk> scratchL{};
        std::array<float, kChunk> scratchR{};
        for (std::size_t i = 0; i < kWarmupSamples; i += kChunk) {
            voice->processStereoBlock(scratchL.data(), scratchR.data(), kChunk);
        }

        l.assign(kScheduledSamples, 0.0f);
        r.assign(kScheduledSamples, 0.0f);
        std::size_t done = 0;
        for (std::size_t p = 0; p < kNumPhases; ++p) {
            for (std::size_t seg = 0; seg < kSegmentsPerPhase; ++seg) {
                Probe::Lanes lanes = uniformLanes(0.0f);
                if (stress && seg == 1u) {
                    if (p < kKinds.size()) {
                        lanes[kKinds[p]].fill(1.0f);
                    } else {
                        lanes = uniformLanes(1.0f);
                    }
                }
                Probe::injectEco(*voice, lanes);
                for (std::size_t i = 0; i < kSegmentSamples; i += kChunk) {
                    voice->processStereoBlock(l.data() + done, r.data() + done, kChunk);
                    done += kChunk;
                }
            }
        }
        REQUIRE(done == kScheduledSamples);
    };

    std::vector<float> sL;
    std::vector<float> sR;
    std::vector<float> tL;
    std::vector<float> tR;
    renderArm(true, sL, sR);
    renderArm(false, tL, tR);

    // Non-vacuity: the stress arm sounds, every sample is finite, and the
    // injection really moved the render away from the twin.
    float peak = 0.0f;
    float maxDiff = 0.0f;
    std::size_t nonFinite = 0;
    for (std::size_t i = 0; i < kScheduledSamples; ++i) {
        const bool finite =
            Krate::DSP::detail::isFinite(sL[i]) && Krate::DSP::detail::isFinite(sR[i]) &&
            Krate::DSP::detail::isFinite(tL[i]) && Krate::DSP::detail::isFinite(tR[i]);
        if (!finite) {
            ++nonFinite;
            continue;
        }
        peak = std::max({peak, std::abs(sL[i]), std::abs(sR[i])});
        maxDiff = std::max({maxDiff, std::abs(sL[i] - tL[i]), std::abs(sR[i] - tR[i])});
    }

    const ClickDetectorConfig cfg{.sampleRate = 48000.0f,
                                  .frameSize = 512,
                                  .hopSize = 256,
                                  .detectionThreshold = 5.0f,
                                  .energyThresholdDb = -60.0f,
                                  .mergeGap = 5};
    REQUIRE(cfg.isValid());
    ClickDetector detector(cfg);
    detector.prepare();
    const std::size_t clicksStressL = detector.detect(sL.data(), sL.size()).size();
    const std::size_t clicksStressR = detector.detect(sR.data(), sR.size()).size();
    const std::size_t clicksTwinL = detector.detect(tL.data(), tL.size()).size();
    const std::size_t clicksTwinR = detector.detect(tR.data(), tR.size()).size();

    std::printf("[LeverClickFree] fs=%.0f seed=0x%08X schedule:", kSampleRate,
                static_cast<unsigned>(seed));
    for (const char* name : kPhaseName) {
        std::printf(" %s", name);
    }
    std::printf(" (each 1 s @0, 1 s @1, 1 s @0)\n");
    std::printf("[LeverClickFree] clicks stress L=%zu R=%zu | twin L=%zu R=%zu | stress peak=%.4f "
                "max |stress - twin|=%.4e non-finite=%zu\n",
                clicksStressL, clicksStressR, clicksTwinL, clicksTwinR,
                static_cast<double>(peak), static_cast<double>(maxDiff), nonFinite);

    REQUIRE(nonFinite == 0u);
    REQUIRE(peak > 0.0f);
    REQUIRE(maxDiff > 0.0f);
    REQUIRE(clicksStressL <= clicksTwinL);
    REQUIRE(clicksStressR <= clicksTwinR);
}

// -----------------------------------------------------------------------------
// VoragoEngine_EcosystemLeverBounded (SC-011(a)(b), SC-018, E-4, E-5)
// -----------------------------------------------------------------------------
// The engine at full polyphony (kMaxVoices = 6), six held notes, a
// VoragoMacroMatrix with Life = 1 applied every block (the vorago_macro_test
// pattern), chain processStereoBlock -> processOutputStage (no cavern: a
// systems TU may not name a Layer 4 type). Arm (a): injectEcoAll at 1 on every
// voice; arm (b): the natural colony. 60 s per arm at 44.1 / 48 / 96 kHz:
// every sample finite and <= the output ceiling, no non-finite recovery, the
// allocated footprint unchanged. At 48 kHz a lockstep twin with the limiter
// ceiling raised to +60 dB PRINTS the maximum gain reduction (plan S4.4
// formula; informative). NOT [long]: a bounds sentinel. The case's wall clock
// is printed (ruling R-2: > 4 min measured alone -> stop and surface).
TEST_CASE("VoragoEngine_EcosystemLeverBounded", "[systems][vorago]") {
    using Krate::DSP::VoragoEngineConfig;
    using Krate::DSP::VoragoMacro;
    using Krate::DSP::VoragoMacroMatrix;

    const auto caseStart = std::chrono::steady_clock::now();

    constexpr std::array<std::uint8_t, VoragoEngine::kMaxVoices> kNotes{36u, 43u, 48u,
                                                                        55u, 60u, 67u};
    constexpr std::uint8_t kVelocity = 100u;
    constexpr float kCeiling = 0.9661f;  // kOutputCeilingDb = -0.3 dB (vorago_engine.h:255)
    constexpr std::size_t kBlock = 512;
    constexpr float kTwinCeilingDb = 60.0f;

    auto makeArmEngine = [&](double fs, bool inject) {
        auto e = std::make_unique<VoragoEngine>();
        e->setSeed(1u);
        e->prepare(fs, VoragoEngineConfig{});
        e->setPolyphony(VoragoEngine::kMaxVoices);
        for (const std::uint8_t note : kNotes) {
            e->noteOn(note, kVelocity);
        }
        if (inject) {
            Probe::injectEcoAll(*e, uniformLanes(1.0f));
        }
        return e;
    };

    struct Arm {
        const char* name;
        bool inject;
    };
    constexpr std::array<Arm, 2> kArms{{{"(a) injected L=1", true}, {"(b) natural colony", false}}};

    for (const Arm& arm : kArms) {
        CAPTURE(arm.name);
        for (const double fs : {44100.0, 48000.0, 96000.0}) {
            CAPTURE(fs);
            auto engine = makeArmEngine(fs, arm.inject);
            REQUIRE(engine->isPrepared());
            for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
                CAPTURE(v);
                REQUIRE(Probe::isRendering(*engine, v));
            }

            const bool withTwin = (fs == 48000.0);
            std::unique_ptr<VoragoEngine> twin;
            if (withTwin) {
                twin = makeArmEngine(fs, arm.inject);
                REQUIRE(twin->isPrepared());
                Probe::setLimiterCeilingDb(*twin, kTwinCeilingDb);
            }

            VoragoMacroMatrix matrix;
            matrix.setMacro(VoragoMacro::Life, 1.0f);

            const std::size_t bytesBefore = engine->getAllocatedBytes();
            const auto total = static_cast<std::size_t>(fs * 60.0);

            std::vector<float> l(kBlock, 0.0f);
            std::vector<float> r(kBlock, 0.0f);
            std::vector<float> ul(kBlock, 0.0f);
            std::vector<float> ur(kBlock, 0.0f);

            std::size_t nonFinite = 0;
            std::size_t overCeiling = 0;
            float peak = 0.0f;
            double grMaxDb = 0.0;
            std::size_t grAt = 0;
            std::size_t grNonFinite = 0;

            for (std::size_t done = 0; done < total; done += kBlock) {
                const std::size_t n = std::min(kBlock, total - done);
                matrix.apply(*engine);
                engine->processStereoBlock(l.data(), r.data(), n);
                engine->processOutputStage(l.data(), r.data(), n);
                if (withTwin) {
                    matrix.apply(*twin);
                    twin->processStereoBlock(ul.data(), ur.data(), n);
                    twin->processOutputStage(ul.data(), ur.data(), n);
                }
                for (std::size_t i = 0; i < n; ++i) {
                    if (!Krate::DSP::detail::isFinite(l[i]) ||
                        !Krate::DSP::detail::isFinite(r[i])) {
                        ++nonFinite;
                        continue;
                    }
                    const float y = std::max(std::abs(l[i]), std::abs(r[i]));
                    peak = std::max(peak, y);
                    overCeiling += (y <= kCeiling) ? 0u : 1u;
                    if (withTwin) {
                        if (!Krate::DSP::detail::isFinite(ul[i]) ||
                            !Krate::DSP::detail::isFinite(ur[i])) {
                            ++grNonFinite;
                            continue;
                        }
                        const float u = std::max(std::abs(ul[i]), std::abs(ur[i]));
                        if (y > 1e-6f) {
                            const double gr = 20.0 * std::log10(static_cast<double>(u) /
                                                                static_cast<double>(y));
                            if (gr > grMaxDb) {
                                grMaxDb = gr;
                                grAt = done + i;
                            }
                        }
                    }
                }
            }

            const std::size_t bytesAfter = engine->getAllocatedBytes();
            const std::uint32_t recoveries = engine->getNonFiniteRecoveryCount();
            std::printf("[LeverBounded] %-20s fs=%.0f voices=%zu 60 s: peak=%.6f over-ceiling=%zu "
                        "non-finite=%zu recoveries=%u bytes before=%zu after=%zu\n",
                        arm.name, fs, VoragoEngine::kMaxVoices, static_cast<double>(peak),
                        overCeiling, nonFinite, static_cast<unsigned>(recoveries), bytesBefore,
                        bytesAfter);
            if (withTwin) {
                std::printf("[LeverBounded] %-20s fs=%.0f GR twin (ceiling +%.0f dB): max "
                            "reduction=%.3f dB at t=%.3f s (twin non-finite=%zu) [informative]\n",
                            arm.name, fs, static_cast<double>(kTwinCeilingDb), grMaxDb,
                            static_cast<double>(grAt) / fs, grNonFinite);
            }

            REQUIRE(nonFinite == 0u);
            REQUIRE(overCeiling == 0u);
            REQUIRE(peak > 0.0f);  // non-vacuity: six voices really sounded
            REQUIRE(recoveries == 0u);
            REQUIRE(bytesAfter == bytesBefore);
        }
    }

    const double wallSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - caseStart).count();
    std::printf("[LeverBounded] case wall clock = %.1f s (ruling R-2: > 240 s measured alone -> "
                "stop and surface)\n",
                wallSeconds);
}

// -----------------------------------------------------------------------------
// VoragoEngine_CapabilityLeverBounded (Phase 13c T055; SC-013, E-12, E-13)
// -----------------------------------------------------------------------------
// The VoragoEngine_EcosystemLeverBounded shape above, at the WORST CASE SC-013
// names on the fully-ruled tree: six voices held; all twelve macros at 1 with
// Gravity run at BOTH 0 and 1 (bipolar, E-6); bloom depth 1 and spawn rate
// BloomEngine::kMaxSpawnRateHz (shipped child gain and placement); ghost peak
// level 1 (shipped ghost-tap make-up and density); ecosystem depth 1 with the
// eco lanes injected at 1 through the friend. The macro matrix is applied every
// block (as the test above), so the bloom / ghost / eco extremes are re-written
// AFTER each apply - otherwise the rows (Life -> BloomSpawnRateHz tops out
// below kMaxSpawnRateHz) would pull them back. 60 s per arm at 44.1 / 48 /
// 96 kHz.
//
// The render loop runs inside TestHelpers::AllocationScope and records into
// plain locals only (Catch2's REQUIRE / INFO allocate); the live count is read
// from the detector singleton while the scope is still open (the scope latches
// its own count in its destructor). Every check runs after the scope closes:
// zero allocations, getAllocatedBytes() unchanged from its after-prepare
// value, every sample finite by bit pattern, |out| <= 0.9661. A guard on the
// ruled tree, expected green; a red is a defect in a ruled lever. NOT [long].
// The case's wall clock is printed (> 4 min measured alone -> stop and surface).
TEST_CASE("VoragoEngine_CapabilityLeverBounded", "[systems][vorago]") {
    using Krate::DSP::BloomEngine;
    using Krate::DSP::VoragoEngineConfig;
    using Krate::DSP::VoragoMacro;
    using Krate::DSP::VoragoMacroMatrix;

    const auto caseStart = std::chrono::steady_clock::now();

    constexpr std::array<std::uint8_t, VoragoEngine::kMaxVoices> kNotes{36u, 43u, 48u,
                                                                        55u, 60u, 67u};
    constexpr std::uint8_t kVelocity = 100u;
    constexpr float kCeiling = 0.9661f;  // kOutputCeilingDb = -0.3 dB (vorago_engine.h:255)
    constexpr std::size_t kBlock = 512;
    const Probe::Lanes kEcoAtOne = uniformLanes(1.0f);

    for (const float gravity : {0.0f, 1.0f}) {
        CAPTURE(gravity);
        for (const double fs : {44100.0, 48000.0, 96000.0}) {
            CAPTURE(fs);

            auto engine = std::make_unique<VoragoEngine>();
            engine->setSeed(1u);
            engine->prepare(fs, VoragoEngineConfig{});
            REQUIRE(engine->isPrepared());
            const std::size_t bytesAfterPrepare = engine->getAllocatedBytes();
            engine->setPolyphony(VoragoEngine::kMaxVoices);
            for (const std::uint8_t note : kNotes) {
                engine->noteOn(note, kVelocity);
            }
            for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
                CAPTURE(v);
                REQUIRE(Probe::isRendering(*engine, v));
            }

            VoragoMacroMatrix matrix;
            for (std::size_t m = 0; m < VoragoMacroMatrix::kNumMacros; ++m) {
                matrix.setMacro(static_cast<VoragoMacro>(m), 1.0f);
            }
            matrix.setMacro(VoragoMacro::Gravity, gravity);

            const auto total = static_cast<std::size_t>(fs * 60.0);
            std::vector<float> l(kBlock, 0.0f);
            std::vector<float> r(kBlock, 0.0f);

            std::size_t allocations = 0;
            std::size_t nonFinite = 0;
            std::size_t overCeiling = 0;
            float peak = 0.0f;

            {
                [[maybe_unused]] const TestHelpers::AllocationScope scope;

                for (std::size_t done = 0; done < total; done += kBlock) {
                    const std::size_t n = std::min(kBlock, total - done);
                    matrix.apply(*engine);
                    // The extremes the rows do not reach (or may not hold).
                    engine->setGhostPeakLevel(1.0f);
                    Probe::setEcosystemDepthAll(*engine, 1.0f);
                    for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
                        VoragoVoice& voice = Probe::voice(*engine, v);
                        voice.setBloomDepth(1.0f);
                        voice.setBloomSpawnRateHz(BloomEngine::kMaxSpawnRateHz);
                    }
                    Probe::injectEcoAll(*engine, kEcoAtOne);

                    engine->processStereoBlock(l.data(), r.data(), n);
                    engine->processOutputStage(l.data(), r.data(), n);
                    for (std::size_t i = 0; i < n; ++i) {
                        if (!Krate::DSP::detail::isFinite(l[i]) ||
                            !Krate::DSP::detail::isFinite(r[i])) {
                            ++nonFinite;
                            continue;
                        }
                        const float y = std::max(std::abs(l[i]), std::abs(r[i]));
                        peak = std::max(peak, y);
                        overCeiling += (y <= kCeiling) ? 0u : 1u;
                    }
                }

                allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
            }

            const std::size_t bytesAfter = engine->getAllocatedBytes();
            const std::uint32_t recoveries = engine->getNonFiniteRecoveryCount();
            std::printf("[CapabilityBounded] gravity=%.0f fs=%.0f voices=%zu 60 s: peak=%.6f "
                        "over-ceiling=%zu non-finite=%zu recoveries=%u allocations=%zu "
                        "bytes after-prepare=%zu after=%zu\n",
                        static_cast<double>(gravity), fs, VoragoEngine::kMaxVoices,
                        static_cast<double>(peak), overCeiling, nonFinite,
                        static_cast<unsigned>(recoveries), allocations, bytesAfterPrepare,
                        bytesAfter);

            REQUIRE(allocations == 0u);
            REQUIRE(bytesAfter == bytesAfterPrepare);
            REQUIRE(nonFinite == 0u);
            REQUIRE(overCeiling == 0u);
            REQUIRE(peak > 0.0f);  // non-vacuity: six voices really sounded
        }
    }

    const double wallSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - caseStart).count();
    std::printf("[CapabilityBounded] case wall clock = %.1f s (> 240 s measured alone -> stop "
                "and surface)\n",
                wallSeconds);
}

// -----------------------------------------------------------------------------
// VoragoEngine_EcosystemLeverDeterminism (SC-013, FR-025)
// -----------------------------------------------------------------------------
// The VoragoEngine_DeterminismHarness shape (vorago_engine_test.cpp:1498-1560,
// copied; that file is not edited), with setEcosystemDepth(1) on every voice so
// the levers are maximally active. Seed A twice: within the portable
// fingerprint tolerance; seed A vs A+1: worst metric > 100 * kMetricTolerance.
namespace {

/// One scheduled note event (copied from vorago_engine_test.cpp:1371-1376).
struct LeverDetNoteEvent {
    std::size_t atSample;
    std::uint8_t note;
    std::uint8_t velocity;
    bool on;
};

/// Render `total` samples in `block`-sized blocks, applying events at block
/// boundaries (copied from vorago_engine_test.cpp:1384-1408).
void renderLeverDetSchedule(VoragoEngine& engine, std::span<const LeverDetNoteEvent> events,
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
            const LeverDetNoteEvent& e = events[next];
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

/// SC-006's note sequence (copied from vorago_engine_test.cpp:1414-1421).
constexpr std::array<LeverDetNoteEvent, 6> kLeverDetSchedule{{
    {.atSample = 0u, .note = 33u, .velocity = 100u, .on = true},
    {.atSample = 512u * 480u, .note = 40u, .velocity = 88u, .on = true},
    {.atSample = 512u * 960u, .note = 45u, .velocity = 120u, .on = true},
    {.atSample = 512u * 1920u, .note = 33u, .velocity = 0u, .on = false},
    {.atSample = 512u * 2880u, .note = 52u, .velocity = 70u, .on = true},
    {.atSample = 512u * 4680u, .note = 40u, .velocity = 0u, .on = false},
}};

} // namespace

TEST_CASE("VoragoEngine_EcosystemLeverDeterminism", "[systems][vorago]") {
    using Krate::DSP::VoragoEngineConfig;
    using Krate::DSP::TestUtils::compareFingerprints;
    using Krate::DSP::TestUtils::fingerprintRender;
    using Krate::DSP::TestUtils::kMetricTolerance;
    using Krate::DSP::TestUtils::RenderFingerprint;
    using Krate::DSP::TestUtils::Vorago::applyFastAttack;

    VoragoEngineConfig cfg{};
    cfg.atmosCaptureSeconds = 1.0f;  // as the harness: identical in every arm

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kTotal = 2880000u;  // exactly 60 s at 48 kHz
    constexpr std::size_t kBlock = 512u;
    static_assert(kTotal % kBlock == 0u, "SC-013: a whole number of blocks in every arm");

    struct ArmFingerprint {
        RenderFingerprint l;
        RenderFingerprint r;
    };

    const auto run = [&](std::uint32_t seed) {
        auto engine = std::make_unique<VoragoEngine>();
        engine->setSeed(seed);
        engine->prepare(kSampleRate, cfg);
        applyFastAttack(*engine);
        Probe::setEcosystemDepthAll(*engine, 1.0f);
        for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
            CAPTURE(v);
            REQUIRE(engine->getVoice(v).getEcosystemDepth() == 1.0f);
        }
        std::vector<float> l;
        std::vector<float> r;
        renderLeverDetSchedule(*engine, std::span<const LeverDetNoteEvent>(kLeverDetSchedule),
                               kTotal, kBlock, l, r);
        return ArmFingerprint{fingerprintRender(std::span<const float>(l)),
                              fingerprintRender(std::span<const float>(r))};
    };

    constexpr std::uint32_t kSeedA = 0x0D0DA1u;
    const ArmFingerprint a = run(kSeedA);
    const ArmFingerprint b = run(kSeedA);       // same seed, config and schedule
    const ArmFingerprint c = run(kSeedA + 1u);  // the ONLY difference is the seed

    INFO("arm A: rms L " << a.l.rms << " peak L " << a.l.peak << ", rms R " << a.r.rms
                         << " peak R " << a.r.peak);
    REQUIRE(a.l.peak > 0.0);
    REQUIRE(a.r.peak > 0.0);

    const auto sameL = compareFingerprints(b.l, a.l);
    const auto sameR = compareFingerprints(b.r, a.r);
    const auto diffL = compareFingerprints(c.l, a.l);
    const auto diffR = compareFingerprints(c.r, a.r);
    const double worstDiff =
        std::max(diffL.worstMetricRelativeError, diffR.worstMetricRelativeError);
    std::printf("[LeverDeterminism] depth=1 seed A=0x%08X: same-seed worst metric L=%.3e R=%.3e; "
                "seed A+1 worst metric=%.3e (bar %.3e)\n",
                static_cast<unsigned>(kSeedA), sameL.worstMetricRelativeError,
                sameR.worstMetricRelativeError, worstDiff, 100.0 * kMetricTolerance);

    INFO("same seed: [" << sameL.detail << " | " << sameR.detail << "]");
    REQUIRE(sameL.withinTolerance());
    REQUIRE(sameR.withinTolerance());
    REQUIRE(worstDiff > 100.0 * kMetricTolerance);
}

// =============================================================================
// Phase 13c ruling B-6 (FR-013): the ghost density route. The engine's applied
// ghost density is ghostDensityBase_ + kGhostDensitySpan * max(raw Ghost eco lane
// over the rendering voices): exactly the base at lane 0 (13b FR-017), the raw
// lane otherwise (scheduler-blind, 13b FR-019), and the measurement seam's base
// reads back unchanged under the route. Fails to compile before T070 adds
// getGhostEcoLane() / kGhostDensitySpan.
// =============================================================================
TEST_CASE("VoragoEngine_GhostDensityRoute", "[systems][vorago]") {
    auto e = std::make_unique<VoragoEngine>();
    e->setSeed(1u);
    e->prepare(48000.0, Krate::DSP::VoragoEngineConfig{});
    e->setPolyphony(1u);
    e->noteOn(45u, 100u);
    std::vector<float> l(VoragoVoice::kControlChunkSamples, 0.0f);
    std::vector<float> r(VoragoVoice::kControlChunkSamples, 0.0f);
    const auto chunks = [&](int n) {
        for (int i = 0; i < n; ++i) {
            e->processStereoBlock(l.data(), r.data(), l.size());
        }
    };
    const auto expected = [](float base, float raw) {
        return std::clamp(base + VoragoEngine::kGhostDensitySpan * raw,
                          Krate::DSP::AtmosphereEngine::kMinDensity,
                          Krate::DSP::AtmosphereEngine::kMaxDensity);
    };

    // (1) every lane 0: exactly the base, on the component and on the seam.
    Probe::injectEcoAll(*e, uniformLanes(0.0f));
    chunks(2);
    REQUIRE(Probe::voice(*e, 0).getGhostEcoLane() == 0.0f);
    REQUIRE(e->atmosphere().getDensity() == VoragoEngine::kGhostDensity);
    REQUIRE(e->getGhostDensity() == VoragoEngine::kGhostDensity);

    // (2) a raw lane: the voice reports it unshaped, the component reads
    //     base + span * raw, the seam still reads the base.
    constexpr std::array<float, 3> kRaw{0.1f, 0.5f, 1.0f};
    for (const float raw : kRaw) {
        CAPTURE(raw);
        Probe::injectEcoAll(*e, uniformLanes(raw));
        chunks(2);
        REQUIRE(Probe::voice(*e, 0).getGhostEcoLane() == raw);
        REQUIRE(e->atmosphere().getDensity() == expected(VoragoEngine::kGhostDensity, raw));
        REQUIRE(e->getGhostDensity() == VoragoEngine::kGhostDensity);
    }

    // (3) the seam moves the base; the route rides the new base.
    e->setGhostDensity(0.6f);
    REQUIRE(e->getGhostDensity() == 0.6f);
    Probe::injectEcoAll(*e, uniformLanes(1.0f));
    chunks(2);
    REQUIRE(e->getGhostDensity() == 0.6f);
    REQUIRE(e->atmosphere().getDensity() == expected(0.6f, 1.0f));
    Probe::injectEcoAll(*e, uniformLanes(0.0f));
    chunks(2);
    REQUIRE(e->atmosphere().getDensity() == 0.6f);
}

// -----------------------------------------------------------------------------
// VoragoVoice_EcoLaneMeanReadout (Phase 13d T-M4; FR-015, plan s2.3)
// -----------------------------------------------------------------------------
// Owning spec: specs/vorago-phase13d-engine-ceilings (tasks.md T012).
// publishIdentity() publishes, per kind, the mean of that kind's eco lane over
// its addressed slots. With every slot injected at x, every kind that addresses
// at least one slot reads x exactly (0, 0.5 and 1 are exact under sum / count),
// and an out-of-range kind reads 0.
TEST_CASE("VoragoVoice_EcoLaneMeanReadout", "[systems][vorago]") {
    auto voice = makeLeverVoice(48000.0, leverSeed());
    REQUIRE(voice->isPrepared());
    voice->noteOn(65.406f, 100.0f / 127.0f);

    // Addressed slots per kind, read from the public owners (slotCountForKind's
    // roster: Partial and Ghost are single-destination families).
    std::array<std::size_t, EcosystemEngine::kNumKinds> slots{};
    slots[kKindPartial] = 1u;
    slots[kKindResonator] = voice->resonance().getNumPeaks();
    slots[kKindNoise] = voice->noise().getNumSources();
    slots[kKindFeedback] = voice->ecology().getNumLoops();
    slots[kKindGhost] = 1u;

    for (const float x : {0.0f, 0.5f, 1.0f}) {
        CAPTURE(x);
        Probe::injectEco(*voice, uniformLanes(x));
        Probe::advanceLifeOnly(*voice);
        for (std::size_t k = 0; k < 5u; ++k) {
            CAPTURE(k);
            if (slots[k] == 0u) {
                continue;
            }
            REQUIRE(voice->getEcoLaneMean(static_cast<EcosystemEngine::Kind>(k)) == x);
        }
        REQUIRE(voice->getEcoLaneMean(static_cast<EcosystemEngine::Kind>(5)) == 0.0f);
    }
}
