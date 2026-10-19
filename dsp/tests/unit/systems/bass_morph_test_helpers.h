// ==============================================================================
// Test helpers - the Bass Morph render fixture and the spec descriptor readers
//                (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T007 (FR-070, FR-071; plan S5, S7.3, C-11; deviations 9, 10). Test-side only: allocation is fine
// here. Read-only after T007 (tasks.md file map). Namespace Krate::DSP::BassMorphTest; every call into the
// Phase 6 / 7 helper namespaces (MassDynamicsTest, StereoFieldTest, ProfundumTest) is qualified. Those helper
// headers are included READ-ONLY (FR-088).
//
// The chain (plan S5.3 - S5.5):
//   BassMorphEngine (one per render, every voice reads the same resolved())
//   per voice: ProfundumCore (pan vector from BassStereoField) -> ShapeMorph -> + feed[n] * anchor[n] -> HarmonicFold
//              -> BassCharacterStage (WIDE offsets) (tap characterL/R); AnchorOscillator (tap anchorL/R);
//              [ampEnvelope: ADSREnvelope gain on character AND anchor]; [drop: DropEnvelope per-sample f0 track]
//   shared tail: StereoFieldTest::processFieldTail (sum = character + anchor -> MassDynamics -> BassStereoField
//              stage -> DC x2 -> TruePeakLimiter)
//
// Recorded decisions (evidence/run-log/T007.txt):
//   - Field voice-state reset at the note-on interval happens in step 9, immediately before that voice's pan
//     query (the Phase 7 FR-015 Reset-exemption order, bass_stereo_field_test_helpers.h:363-376); every other
//     step-11 note-on call (core, Anchor, Drop, ADSR) stays at step 11. ProfundumCore::noteOn and
//     AnchorOscillator::noteOn are deferred to the next block, so their position inside the grid point does not
//     change the render.
//   - With cfg.drop, fNow at a grid point (HarmonicFold f0, detector key, a1) is the f0 of the voice's previous
//     sample, f0 * 2^(DropEnvelope::currentOffset() / 12) (idle: f0); the per-sample track of the block is
//     produced after step 11's trigger, so the core, Anchor, ShapeMorph and character stage see it sample-exact.
//   - The ADSR setters are applied (change-only, step 8) whether or not ampEnvelope is on; with ampEnvelope off
//     the envelope is never processed and no tap is multiplied (unity, bitwise).
//   - ampEnvSetterCalls counts setter applications per interval (in renderMorphPoly one application sets the
//     leaf on every voice and counts once).
//   - muteCharacter: the block's character tap is saved, overwritten with -0.0f (the IEEE additive identity, so
//     sum == anchor bitwise including signed zeros), processFieldTail runs, then the saved tap is restored.
//   - BassCharacterStage::syncPhase(0) at the note-on (step 11, after the core's noteOn): the FR-072 voice contract
//     (Profundum roadmap, Phase 5 handoff (2)). Without it a switch back to Fracture / Crush restarted its phasor at
//     0 at the switch sample, unrelated to the core's phase (Group 8 gate fix, tasks.md Defects found G8-3).
//   - The fold-feed add is skipped in an interval whose previous and current feed are both +0 / -0 (a no-op
//     add), so a feed-0 render is bitwise the Phase 7 chain's Fold input.
//   - Harmonic-power readers (dCxDb, dNlDb, dBrOct) and dStDb read the full kLowFrequencyFftSize frame from the
//     settle point (they are windowed; ProfundumTest::renderedHarmonicPowers takes exactly that frame). The
//     time-domain readers (eSubDb) trim the read window to whole periods of f0.
// ==============================================================================

#pragma once

#include "../processors/shape_character_metric_helpers.h"
#include "anchor_oscillator_test_helpers.h"
#include "bass_morph_state_helpers.h"
#include "bass_stereo_field_test_helpers.h"
#include "profundum_core_test_helpers.h"

#include <allocation_detector.h>
#include <k_weighting.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/shape_morph.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Krate::DSP::BassMorphTest {

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

inline constexpr std::size_t kGrid = MassDynamicsTest::kGrid;
static_assert(kGrid == BassMorphEngine::kControlInterval && kGrid == StereoFieldTest::kGrid,
              "the morph engine shares the chain's 32-sample grid (FR-016)");

/// The full-range pan slew in intervals (deviation 10): ceil(1 / kMaxPanStepPerInterval) = 334.
inline constexpr std::size_t kPanSlewIntervals = 334;
static_assert(static_cast<float>(kPanSlewIntervals) * BassStereoField::kMaxPanStepPerInterval >= 1.0f
                  && static_cast<float>(kPanSlewIntervals - 1) * BassStereoField::kMaxPanStepPerInterval < 1.0f,
              "334 intervals is the first count whose pan travel covers the full 0 -> 1 range");

/// MassLaw's AnchorLevel row, located by target (never by a literal index; tasks.md verified facts).
inline constexpr std::size_t kAnchorLevelRowIndex = [] {
    for (std::size_t i = 0; i < MassLaw::kNumTargets; ++i) {
        if (MassLaw::kRows[i].target == MassLaw::Target::AnchorLevel)
            return i;
    }
    return MassLaw::kNumTargets;
}();
static_assert(kAnchorLevelRowIndex < MassLaw::kNumTargets, "MassLaw::kRows has an AnchorLevel row");

inline constexpr std::uint8_t kNoLeaf = 0xFF;

/// The kFieldTable index of the leaf named @p name; kNoLeaf if there is none.
[[nodiscard]] constexpr std::uint8_t leafIndex(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        if (std::string_view(detail::morph_state_io::kFieldTable[i].name) == name)
            return static_cast<std::uint8_t>(i);
    }
    return kNoLeaf;
}

inline constexpr std::uint8_t kLeafCharacterAlgorithm = leafIndex("characterAlgorithm");
inline constexpr std::uint8_t kLeafFieldMode = leafIndex("field.mode");
inline constexpr std::uint8_t kLeafFieldMotionSource = leafIndex("field.motionSource");
inline constexpr std::uint8_t kLeafFieldCycleSync = leafIndex("field.cycleSync");
inline constexpr std::uint8_t kLeafFieldCycleNote = leafIndex("field.cycleNote");
inline constexpr std::uint8_t kLeafFieldCycleModifier = leafIndex("field.cycleModifier");
inline constexpr std::uint8_t kLeafAnchorWaveform = leafIndex("anchor.waveform");
inline constexpr std::uint8_t kLeafAnchorMode = leafIndex("anchor.mode");
static_assert(kLeafCharacterAlgorithm != kNoLeaf && kLeafFieldMode != kNoLeaf && kLeafFieldMotionSource != kNoLeaf
                  && kLeafFieldCycleSync != kNoLeaf && kLeafFieldCycleNote != kNoLeaf
                  && kLeafFieldCycleModifier != kNoLeaf && kLeafAnchorWaveform != kNoLeaf
                  && kLeafAnchorMode != kNoLeaf,
              "every recorded discrete leaf is a kFieldTable row");

/// The seven ampEnv leaves, in AmpEnvelopeFields member order (deviation 9: one counter each).
inline constexpr std::size_t kAmpEnvLeafCount = 7;
enum class AmpEnvLeaf : std::uint8_t { AttackMs, DecayMs, Sustain, ReleaseMs, AttackCurve, DecayCurve, ReleaseCurve };

// -----------------------------------------------------------------------------
// Config and taps (plan S5.2; deviation 9)
// -----------------------------------------------------------------------------

struct MorphEvent {
    enum class Kind : std::uint8_t { Position, RampMs, Mode, EndpointA, EndpointB };
    std::size_t sample = 0;    ///< applied at the first grid point >= sample
    Kind kind = Kind::Position;
    float value = 0.0f;        ///< Position, RampMs
    BassMorphEngine::AnchorMorphMode mode = BassMorphEngine::kDefaultAnchorMorphMode;
    MorphState state{};        ///< EndpointA / EndpointB
};

struct MorphChainConfig {
    MorphState a{}, b{};
    BassMorphEngine::AnchorMorphMode mode = BassMorphEngine::kDefaultAnchorMorphMode;
    float initialPosition = 0.0f;                      ///< the engine is reset at this p before the first interval
    float rampMs = BassMorphEngine::kDefaultPositionRampMs;
    double f0 = 0.0, fs = 48000.0, seconds = 0.0;      ///< seconds <= 0 -> MassDynamicsTest::descriptorSeconds(fs)
    std::size_t hostBlock = 512;
    std::size_t noteOnSample = 0;                      ///< multiple of 32
    std::optional<std::size_t> noteOffSample;          ///< ADSR gate off at the first grid point >= this sample
    bool ampEnvelope = true;                           ///< false = the steady render (no ADSR processing at all)
    bool drop = false;                                 ///< true: per-sample f0 track from the voice's DropEnvelope
    bool muteCharacter = false;                        ///< SC-016: characterL/R zeroed (after the tap) before the sum
    std::vector<MorphEvent> automation;                ///< sorted by sample
    StereoFieldTest::FieldChainConfig tail{};          ///< tail options only; chain.f0/fs/seconds/hostBlock overwritten
};

struct MorphChainTaps : StereoFieldTest::FieldChainTaps {
    std::vector<float> linearCoreL, linearCoreR;      ///< the linear twin core (excitation mix 0): D_nl's denominator
    std::vector<BassMorphEngine::Resolved> resolvedPerInterval;
    std::vector<float> anchorLevelHanded;              ///< Mass-biased level handed to the Anchor (FR-054)
    std::vector<std::size_t> advanceSamples;           ///< absolute sample of each advanceInterval call (SC-010)
    struct SetterCall {
        std::size_t sample;
        std::uint8_t leaf;    ///< kFieldTable index (kLeafCharacterAlgorithm, kLeafFieldMode, ...)
        std::uint8_t value;   ///< the enum / bool value issued
    };
    std::vector<SetterCall> discreteSetterCalls;       ///< setAlgorithm / field mode + motion selectors / anchor waveform, mode
    std::vector<std::vector<BassMorphEngine::Resolved>> perVoiceResolved;   ///< renderMorphPoly only (SC-010)
    std::array<std::size_t, kAmpEnvLeafCount> ampEnvSetterCalls{};          ///< deviation 9: one counter per ampEnv leaf
};

// -----------------------------------------------------------------------------
// Construction helpers
// -----------------------------------------------------------------------------

/// Plan S5.3: the MassLaw base of a resolved state. depth / body from the recipe, Fold and the Anchor level from
/// their leaves; transientDurationMs, compressionAmount and outputTrimDb at the Targets{} defaults (FR-070 step 2).
[[nodiscard]] inline MassLaw::Targets baseFor(const MorphState& s) {
    MassLaw::Targets t{};
    t.depth = s.recipe.depth;
    t.body = s.recipe.body;
    t.foldAmount = s.foldAmount;
    t.anchorLevel = s.anchor.level;
    return t;
}

/// The Mass-biased Anchor level (FR-054): the AnchorLevel row at the Anchor's own Mass.
[[nodiscard]] inline float anchorLevelFor(const BassMorphEngine::Resolved& r) {
    return MassLaw::evaluateRow(MassLaw::kRows[kAnchorLevelRowIndex], r.anchorMass, r.state.anchor.level);
}

[[nodiscard]] inline bool sameFloatBits(float x, float y) noexcept {
    return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
}

[[nodiscard]] inline float ampEnvLeaf(const MorphState::AmpEnvelopeFields& e, std::size_t i) noexcept {
    switch (static_cast<AmpEnvLeaf>(i)) {
        case AmpEnvLeaf::AttackMs: return e.attackMs;
        case AmpEnvLeaf::DecayMs: return e.decayMs;
        case AmpEnvLeaf::Sustain: return e.sustain;
        case AmpEnvLeaf::ReleaseMs: return e.releaseMs;
        case AmpEnvLeaf::AttackCurve: return e.attackCurve;
        case AmpEnvLeaf::DecayCurve: return e.decayCurve;
        case AmpEnvLeaf::ReleaseCurve: return e.releaseCurve;
    }
    return e.attackMs;
}

inline void applyAmpEnvLeaf(ADSREnvelope& env, std::size_t i, float v) noexcept {
    switch (static_cast<AmpEnvLeaf>(i)) {
        case AmpEnvLeaf::AttackMs: env.setAttack(v); break;
        case AmpEnvLeaf::DecayMs: env.setDecay(v); break;
        case AmpEnvLeaf::Sustain: env.setSustain(v); break;
        case AmpEnvLeaf::ReleaseMs: env.setRelease(v); break;
        case AmpEnvLeaf::AttackCurve: env.setAttackCurve(v); break;
        case AmpEnvLeaf::DecayCurve: env.setDecayCurve(v); break;
        case AmpEnvLeaf::ReleaseCurve: env.setReleaseCurve(v); break;
    }
}

/// One voice chain (plan S5.3 step 2, S5.6): core -> ShapeMorph -> (+ feed) -> Fold -> character, its Anchor, Drop
/// and ADSR, and its own field VoiceState.
struct MorphVoice {
    float f0 = 0.0f;
    std::unique_ptr<ProfundumCore> core;
    std::unique_ptr<ProfundumCore> linearCore;   ///< same recipe, f0, pan and note-on; excitation mix 0 (D_nl reader)
    std::unique_ptr<ShapeMorph> morph;
    std::unique_ptr<HarmonicFold> fold;
    std::unique_ptr<BassCharacterStage> character;
    std::unique_ptr<AnchorOscillator> anchor;
    std::unique_ptr<DropEnvelope> drop;
    std::unique_ptr<ADSREnvelope> adsr;
    BassStereoField::VoiceState vs{};
    float fNow = 0.0f;     ///< f0 at the current grid point (Drop: the previous sample's tracked f0)
    float a1Next = 0.0f;   ///< step 10: unbiased h1 of the resolved recipe at fNow
    float a1 = 0.0f;       ///< handed to the Anchor at the interval's first run
};

/// Plan S5.3 step 2: ShapeMorph, BassCharacterStage configured from r0 BEFORE their prepare (prepare snaps); the
/// core, Anchor and Drop prepared first and then given r0; the ADSR prepared (its leaves are applied, and counted,
/// at the first interval, step 8); HarmonicFold prepared then keyed (the renderFieldChain order).
[[nodiscard]] inline MorphVoice makeMorphVoice(float f0, const BassMorphEngine::Resolved& r0, double fs) {
    const MorphState& s = r0.state;
    MorphVoice v;
    v.f0 = f0;
    v.fNow = f0;

    v.core = std::make_unique<ProfundumCore>();
    v.core->prepare(fs);
    v.core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    v.core->setControls(s.recipe);
    v.core->setExcitation(s.excitation);
    v.linearCore = std::make_unique<ProfundumCore>();
    v.linearCore->prepare(fs);
    v.linearCore->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    v.linearCore->setControls(s.recipe);
    v.linearCore->setExcitation(ProfundumCore::ExcitationControls{});   // mix 0: linear

    v.anchor = std::make_unique<AnchorOscillator>();
    v.anchor->prepare(fs);
    v.anchor->setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
    AnchorOscillator::Controls ac = s.anchor;
    ac.level = anchorLevelFor(r0);
    v.anchor->setControls(ac);

    v.fold = std::make_unique<HarmonicFold>();
    v.fold->prepare(fs, 512);
    v.fold->setFundamentalHz(f0);

    v.morph = std::make_unique<ShapeMorph>();   // before prepare: prepare's reset() snaps SHAPE
    v.morph->setShape(s.shape);
    v.morph->setFundamentalHz(f0);
    v.morph->prepare(fs, 512);

    v.character = std::make_unique<BassCharacterStage>();   // before prepare: no crossfade, smoothers snapped
    v.character->setAlgorithm(s.characterAlgorithm);
    v.character->setAmount(s.characterAmount);
    v.character->setTone(s.characterTone);
    v.character->setFundamentalHz(f0);
    v.character->prepare(fs, 512);

    v.drop = std::make_unique<DropEnvelope>();
    v.drop->prepare(fs);
    v.drop->setAmount(s.dropAmountSt);
    v.drop->setTime(s.dropTimeMs);
    v.drop->setShape(s.dropShape);
    v.drop->setRebound(s.dropRebound);

    v.adsr = std::make_unique<ADSREnvelope>();
    v.adsr->prepare(static_cast<float>(fs));
    return v;
}

// -----------------------------------------------------------------------------
// The render (plan S5.3 - S5.6)
// -----------------------------------------------------------------------------

/// The shared implementation of renderMorphChain (one voice at cfg.f0) and renderMorphPoly (one voice per f0,
/// recordPerVoice = true). Voice 0 must be the lowest f0 for the single-voice records (a1PerInterval,
/// h1DeliveredPerInterval, anchorControlsPerInterval, vectorPerInterval): the caller orders nothing, so the lowest
/// voice is looked up.
[[nodiscard]] inline MorphChainTaps renderMorphVoices(std::span<const double> f0s, const MorphChainConfig& cfg,
                                                      bool recordPerVoice) {
    REQUIRE(!f0s.empty());
    REQUIRE(cfg.fs > 0.0);
    REQUIRE(cfg.hostBlock > 0);
    REQUIRE(cfg.noteOnSample % kGrid == 0);
    REQUIRE(std::is_sorted(cfg.automation.begin(), cfg.automation.end(),
                           [](const MorphEvent& x, const MorphEvent& y) { return x.sample < y.sample; }));
    REQUIRE_FALSE(static_cast<bool>(cfg.tail.f0At));          // the fixture owns the f0 track (cfg.drop)
    REQUIRE_FALSE(cfg.tail.generatorGravity.has_value());     // not supported by the morph fixture
    REQUIRE_FALSE(cfg.tail.forceSubCentre);
    for (const double f : f0s)
        REQUIRE((detail::isFinite(static_cast<float>(f)) && f > 0.0));
    const std::size_t total = MassDynamicsTest::renderLength(cfg.fs, cfg.seconds);
    REQUIRE(total > 0);

    // ---- S5.3 step 1: the engine (prepare resets, C-5) ----
    BassMorphEngine engine;
    REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::A, cfg.a));
    REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::B, cfg.b));
    engine.setPositionRampMs(cfg.rampMs);
    engine.setAnchorMorphMode(cfg.mode);
    engine.setPosition(cfg.initialPosition);
    engine.prepare(cfg.fs);
    const BassMorphEngine::Resolved r0 = engine.resolved();

    // ---- the tail configuration: tail options of cfg.tail, chain and field from cfg / r0 ----
    StereoFieldTest::FieldChainConfig tailCfg = cfg.tail;
    tailCfg.chain.f0 = f0s[0];
    tailCfg.chain.fs = cfg.fs;
    tailCfg.chain.seconds = cfg.seconds;
    tailCfg.chain.hostBlock = cfg.hostBlock;
    tailCfg.chain.noteOnSample = cfg.noteOnSample;
    tailCfg.chain.base = r0.state.recipe;
    tailCfg.chain.shaperAttackDb = r0.state.transientAttackDb;
    tailCfg.chain.shaperSustainDb = r0.state.transientSustainDb;
    tailCfg.field = r0.state.field;
    tailCfg.fieldAt = nullptr;

    // ---- S5.3 step 2: the voices; the field through StereoFieldTest::makeField ----
    std::vector<MorphVoice> voices;
    voices.reserve(f0s.size());
    for (const double f : f0s)
        voices.push_back(makeMorphVoice(static_cast<float>(f), r0, cfg.fs));
    std::size_t lowest = 0;
    for (std::size_t v = 1; v < voices.size(); ++v) {
        if (voices[v].f0 < voices[lowest].f0)
            lowest = v;
    }
    std::unique_ptr<BassStereoField> field = StereoFieldTest::makeField(tailCfg, std::nullopt);
    BassStereoField* const stage = tailCfg.bypassField ? nullptr : field.get();
    const bool handVector = !tailCfg.bypassField || tailCfg.bypassHandsVector;

    // ---- S5.3 step 3: MassLaw and the dynamics ----
    MassLaw law;
    law.prepare(cfg.fs);
    law.setBase(baseFor(r0.state));
    law.setMass(r0.state.mass);
    law.reset();
    MassDynamicsTest::ChainDynamics dyn = MassDynamicsTest::makeChainDynamics(tailCfg.chain);

    MorphChainTaps taps;
    StereoFieldTest::allocateFieldTaps(taps, total, tailCfg);
    taps.linearCoreL.assign(total, 0.0f);
    taps.linearCoreR.assign(total, 0.0f);
    const std::size_t intervals = (total + kGrid - 1) / kGrid;
    taps.resolvedPerInterval.reserve(intervals);
    taps.anchorLevelHanded.reserve(intervals);
    taps.advanceSamples.reserve(intervals);
    taps.a1PerInterval.reserve(intervals);
    if (recordPerVoice) {
        taps.perVoiceResolved.resize(voices.size());
        for (auto& pv : taps.perVoiceResolved)
            pv.reserve(intervals);
    }

    // Change-only state (the components were given r0, so r0's discrete values are the "last issued" ones).
    BassCharacterStage::Algorithm lastAlgorithm = r0.state.characterAlgorithm;
    AnchorOscillator::Waveform lastWaveform = r0.state.anchor.waveform;
    AnchorOscillator::Mode lastAnchorMode = r0.state.anchor.mode;
    BassStereoField::Controls lastField = r0.state.field;
    std::array<float, kAmpEnvLeafCount> lastAmpEnv{};
    bool ampEnvApplied = false;
    float feedPrev = r0.state.anchorFoldFeed;
    float feedTarget = r0.state.anchorFoldFeed;
    bool noteOffDone = false;
    std::size_t nextEvent = 0;

    std::array<float, kMaxPartials> pan{};
    std::array<float, kMaxPartials> lowestPan{};
    std::array<float, kGrid> f0Blk{}, cL{}, cR{}, lcL{}, lcR{}, aL{}, aR{}, fL{}, fR{}, keepL{}, keepR{};

    MassDynamicsTest::gridDriver(total, cfg.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        const bool sounding = start >= cfg.noteOnSample;
        if (atGrid) {
            // 1. Due automation events, then exactly one advanceInterval().
            while (nextEvent < cfg.automation.size() && cfg.automation[nextEvent].sample <= start) {
                const MorphEvent& e = cfg.automation[nextEvent];
                switch (e.kind) {
                    case MorphEvent::Kind::Position: engine.setPosition(e.value); break;
                    case MorphEvent::Kind::RampMs: engine.setPositionRampMs(e.value); break;
                    case MorphEvent::Kind::Mode: engine.setAnchorMorphMode(e.mode); break;
                    case MorphEvent::Kind::EndpointA:
                        REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::A, e.state));
                        break;
                    case MorphEvent::Kind::EndpointB:
                        REQUIRE(engine.setEndpoint(BassMorphEngine::Endpoint::B, e.state));
                        break;
                }
                ++nextEvent;
            }
            const BassMorphEngine::Resolved& r = engine.advanceInterval();
            const MorphState& s = r.state;
            taps.resolvedPerInterval.push_back(r);
            taps.advanceSamples.push_back(start);

            // 2. The Mass split (FR-054): every row at the slewed Mass, the AnchorLevel row at anchorMass.
            law.setBase(baseFor(s));
            law.setMass(s.mass);
            const float massUsed = law.advanceInterval();
            const MassLaw::Targets t = law.currentTargets();
            const float lvl = anchorLevelFor(r);
            taps.anchorLevelHanded.push_back(lvl);
            taps.anchorLevelPerInterval.push_back(lvl);
            taps.massPerInterval.push_back(massUsed);
            taps.targetsPerInterval.push_back(t);

            for (std::size_t vi = 0; vi < voices.size(); ++vi) {
                MorphVoice& v = voices[vi];
                v.fNow = cfg.drop ? static_cast<float>(static_cast<double>(v.f0)
                                                       * std::exp2(static_cast<double>(v.drop->currentOffset()) / 12.0))
                                  : v.f0;
                // 3. Core controls + excitation.
                v.core->setControls(MassDynamicsTest::coreControlsFor(s.recipe, t));
                v.core->setExcitation(s.excitation);
                v.linearCore->setControls(MassDynamicsTest::coreControlsFor(s.recipe, t));
                // 4. Fold + SHAPE.
                v.fold->setAmount(t.foldAmount);
                if (cfg.drop)
                    v.fold->setFundamentalHz(v.fNow);
                v.morph->setShape(s.shape);
            }

            // 5. The character stage: setAlgorithm only on change (recorded), then Amount and Tone.
            if (s.characterAlgorithm != lastAlgorithm) {
                for (MorphVoice& v : voices)
                    v.character->setAlgorithm(s.characterAlgorithm);
                taps.discreteSetterCalls.push_back(
                    {start, kLeafCharacterAlgorithm, static_cast<std::uint8_t>(s.characterAlgorithm)});
                lastAlgorithm = s.characterAlgorithm;
            }
            for (MorphVoice& v : voices) {
                v.character->setAmount(s.characterAmount);
                v.character->setTone(s.characterTone);
            }

            // 6. The Anchor with the Mass-biased level (waveform / mode issues recorded); the feed target.
            {
                AnchorOscillator::Controls ac = s.anchor;
                ac.level = lvl;
                for (MorphVoice& v : voices)
                    v.anchor->setControls(ac);
                if (s.anchor.waveform != lastWaveform) {
                    taps.discreteSetterCalls.push_back(
                        {start, kLeafAnchorWaveform, static_cast<std::uint8_t>(s.anchor.waveform)});
                    lastWaveform = s.anchor.waveform;
                }
                if (s.anchor.mode != lastAnchorMode) {
                    taps.discreteSetterCalls.push_back(
                        {start, kLeafAnchorMode, static_cast<std::uint8_t>(s.anchor.mode)});
                    lastAnchorMode = s.anchor.mode;
                }
                taps.anchorControlsPerInterval.push_back(voices[lowest].anchor->pendingControls());
                feedPrev = feedTarget;
                feedTarget = s.anchorFoldFeed;
            }

            // 7. Drop setters (latched by the component at its next trigger).
            for (MorphVoice& v : voices) {
                v.drop->setAmount(s.dropAmountSt);
                v.drop->setTime(s.dropTimeMs);
                v.drop->setShape(s.dropShape);
                v.drop->setRebound(s.dropRebound);
            }

            // 8. ADSR setters, change-only per leaf by bit compare; the first interval applies all seven.
            for (std::size_t i = 0; i < kAmpEnvLeafCount; ++i) {
                const float x = ampEnvLeaf(s.ampEnv, i);
                if (ampEnvApplied && sameFloatBits(x, lastAmpEnv[i]))
                    continue;
                for (MorphVoice& v : voices)
                    applyAmpEnvLeaf(*v.adsr, i, x);
                lastAmpEnv[i] = x;
                ++taps.ampEnvSetterCalls[i];
            }
            ampEnvApplied = true;

            // 9. The field: setControls (+ the tail's hook) + advanceControl (discrete issues recorded), then per
            //    voice the pan vector and the WIDE offsets.
            field->setControls(s.field);
            if (tailCfg.perInterval)
                tailCfg.perInterval(start, *field);
            field->advanceControl();
            {
                const BassStereoField::Controls& c = s.field;
                if (c.mode != lastField.mode)
                    taps.discreteSetterCalls.push_back({start, kLeafFieldMode, static_cast<std::uint8_t>(c.mode)});
                if (c.motionSource != lastField.motionSource)
                    taps.discreteSetterCalls.push_back(
                        {start, kLeafFieldMotionSource, static_cast<std::uint8_t>(c.motionSource)});
                if (c.cycleSync != lastField.cycleSync)
                    taps.discreteSetterCalls.push_back(
                        {start, kLeafFieldCycleSync, static_cast<std::uint8_t>(c.cycleSync ? 1 : 0)});
                if (c.cycleNote != lastField.cycleNote)
                    taps.discreteSetterCalls.push_back(
                        {start, kLeafFieldCycleNote, static_cast<std::uint8_t>(c.cycleNote)});
                if (c.cycleModifier != lastField.cycleModifier)
                    taps.discreteSetterCalls.push_back(
                        {start, kLeafFieldCycleModifier, static_cast<std::uint8_t>(c.cycleModifier)});
                lastField = c;
            }
            taps.lawCrossoverPerInterval.push_back(field->smoothedCrossoverHz());
            taps.stageCrossoverPerInterval.push_back(field->stageCrossoverHz());
            const BassStereoField::ChannelOffsets o = field->wideOffsets(s.characterAlgorithm);
            for (std::size_t vi = 0; vi < voices.size(); ++vi) {
                MorphVoice& v = voices[vi];
                if (start == cfg.noteOnSample)
                    v.vs.reset();   // the Phase 7 Reset-exemption order (banner)
                const float fq = (start <= cfg.noteOnSample) ? v.f0 : v.core->currentFrequency();
                field->computePanVector(v.vs, fq, pan, v.core->numPartials());
                if (handVector) {
                    v.core->setPartialPanOffsets(pan);
                    v.linearCore->setPartialPanOffsets(pan);
                }
                v.character->setChannelOffsets(o.amount, o.tone);
                if (vi == lowest)
                    lowestPan = pan;
            }
            taps.vectorPerInterval.push_back(lowestPan);
            taps.offsetsPerInterval.push_back(o);

            // 10. MassDynamics; a1 recomputed every interval from the resolved recipe (never stale).
            dyn.md->setTransientAttackDb(s.transientAttackDb);
            dyn.md->setTransientSustainDb(s.transientSustainDb);
            dyn.md->setTransientDurationMs(t.transientDurationMs);
            dyn.md->setCompressionAmount(t.compressionAmount);
            dyn.md->setOutputTrimDb(t.outputTrimDb);
            dyn.md->setDetectorKeyHz(voices[lowest].fNow);
            for (MorphVoice& v : voices)
                v.a1Next = MassDynamicsTest::unbiasedH1Amplitude(s.recipe, static_cast<double>(v.fNow), cfg.fs);

            // 11. Note-on / note-off.
            if (start == cfg.noteOnSample) {
                for (MorphVoice& v : voices) {
                    v.core->noteOn(v.f0);
                    v.linearCore->noteOn(v.f0);
                    v.anchor->noteOn(v.f0, true);
                    // FR-072 / Phase 5 handoff (2): the voice syncs the character stage's Crush / Fracture
                    // clock to the core's h1 phase at note-on. The core restarts at phase 0 (RetriggerPhase::Reset)
                    // on this block's first sample, so coreCycles = 0; a later algorithm switch then starts its
                    // branch at the core's phase instead of 0 (SC-006(b): A -> B -> A back to Fracture).
                    v.character->syncPhase(0.0f);
                    v.drop->trigger(true);
                    v.adsr->gate(true);
                }
            }
            if (cfg.noteOffSample.has_value() && !noteOffDone && start >= *cfg.noteOffSample) {
                for (MorphVoice& v : voices)
                    v.adsr->gate(false);
                noteOffDone = true;
            }

            if (recordPerVoice) {
                for (std::size_t vi = 0; vi < voices.size(); ++vi)
                    taps.perVoiceResolved[vi].push_back(engine.resolved());
            }
        }

        // ---- S5.5: per rendered block ----
        if (sounding) {
            const bool feedActive = !(feedPrev == 0.0f && feedTarget == 0.0f);
            for (std::size_t vi = 0; vi < voices.size(); ++vi) {
                MorphVoice& v = voices[vi];
                const float* f0p = nullptr;
                if (cfg.drop) {
                    for (std::size_t i = 0; i < len; ++i) {
                        const double d = static_cast<double>(v.drop->process());
                        f0Blk[i] = static_cast<float>(static_cast<double>(v.f0) * std::exp2(d / 12.0));
                    }
                    f0p = f0Blk.data();
                }
                v.core->processBlock(cL.data(), cR.data(), len, f0p);
                v.linearCore->processBlock(lcL.data(), lcR.data(), len, f0p);
                v.a1 = v.a1Next;
                v.anchor->setCoreH1Amplitude(v.a1);
                if (tailCfg.chain.attachAnchor) {
                    v.anchor->processBlock(aL.data(), aR.data(), len, f0p);
                } else {
                    std::fill_n(aL.data(), len, 0.0f);
                    std::fill_n(aR.data(), len, 0.0f);
                }
                std::copy_n(cL.data(), len, fL.data());
                std::copy_n(cR.data(), len, fR.data());
                v.morph->processBlock(fL.data(), fR.data(), len, f0p);
                if (feedActive) {   // C-11: the feed ramps linearly across the interval from the previous value
                    for (std::size_t i = 0; i < len; ++i) {
                        const std::size_t phase = (start + i) % kGrid;
                        const float w = static_cast<float>(phase + 1) / static_cast<float>(kGrid);
                        const float feed = feedPrev + (feedTarget - feedPrev) * w;
                        fL[i] += feed * aL[i];
                        fR[i] += feed * aR[i];
                    }
                }
                v.fold->processBlock(fL.data(), fR.data(), len);
                const bool first = vi == 0;
                for (std::size_t i = 0; i < len; ++i) {
                    const std::size_t n = start + i;
                    taps.coreL[n] = first ? cL[i] : taps.coreL[n] + cL[i];
                    taps.coreR[n] = first ? cR[i] : taps.coreR[n] + cR[i];
                    taps.linearCoreL[n] = first ? lcL[i] : taps.linearCoreL[n] + lcL[i];
                    taps.linearCoreR[n] = first ? lcR[i] : taps.linearCoreR[n] + lcR[i];
                    taps.foldL[n] = first ? fL[i] : taps.foldL[n] + fL[i];
                    taps.foldR[n] = first ? fR[i] : taps.foldR[n] + fR[i];
                }
                v.character->processBlock(fL.data(), fR.data(), len, f0p);
                if (cfg.ampEnvelope) {   // the Anchor is part of the voice and is released with it
                    for (std::size_t i = 0; i < len; ++i) {
                        const float g = v.adsr->process();
                        fL[i] *= g;
                        fR[i] *= g;
                        aL[i] *= g;
                        aR[i] *= g;
                    }
                }
                for (std::size_t i = 0; i < len; ++i) {
                    const std::size_t n = start + i;
                    taps.characterL[n] = first ? fL[i] : taps.characterL[n] + fL[i];
                    taps.characterR[n] = first ? fR[i] : taps.characterR[n] + fR[i];
                    taps.anchorL[n] = first ? aL[i] : taps.anchorL[n] + aL[i];
                    taps.anchorR[n] = first ? aR[i] : taps.anchorR[n] + aR[i];
                }
            }
        }
        if (atGrid) {
            taps.a1PerInterval.push_back(sounding ? voices[lowest].a1 : 0.0f);
            taps.h1DeliveredPerInterval.push_back(sounding ? voices[lowest].core->deliveredGains()[0] : 0.0f);
        }
        if (cfg.muteCharacter) {   // SC-016: the tap keeps the character; the sum sees -0 (additive identity)
            std::copy_n(taps.characterL.data() + start, len, keepL.data());
            std::copy_n(taps.characterR.data() + start, len, keepR.data());
            std::fill_n(taps.characterL.data() + start, len, -0.0f);
            std::fill_n(taps.characterR.data() + start, len, -0.0f);
        }
        StereoFieldTest::processFieldTail(taps, tailCfg, *dyn.md, dyn.dcL, dyn.dcR, *dyn.limiter, stage, start, len,
                                          total);
        if (cfg.muteCharacter) {
            std::copy_n(keepL.data(), len, taps.characterL.data() + start);
            std::copy_n(keepR.data(), len, taps.characterR.data() + start);
        }
    });
    return taps;
}

/// Plan S5.3 - S5.5: one voice at cfg.f0.
[[nodiscard]] inline MorphChainTaps renderMorphChain(const MorphChainConfig& cfg) {
    REQUIRE(cfg.f0 > 0.0);
    const std::array<double, 1> f0s{cfg.f0};
    return renderMorphVoices(std::span<const double>(f0s.data(), f0s.size()), cfg, false);
}

/// Plan S5.6: one voice chain per f0 (cfg.f0 ignored), all reading the same engine.resolved() at each grid point;
/// perVoiceResolved[v] records what voice v applied; one shared tail keyed by the lowest voice.
[[nodiscard]] inline MorphChainTaps renderMorphPoly(std::span<const double> f0s, const MorphChainConfig& common) {
    return renderMorphVoices(f0s, common, true);
}

// -----------------------------------------------------------------------------
// Descriptor readers (plan S5.7; spec descriptor block)
// -----------------------------------------------------------------------------

/// The read window: [kSettleSeconds·fs, + kLowFrequencyFftSize); `wholePeriods` is the window length trimmed to
/// whole periods of f0 (0 when no f0 is given).
struct ReadWindow {
    std::size_t start = 0;
    std::size_t length = 0;
    std::size_t wholePeriods = 0;
};

[[nodiscard]] inline ReadWindow readWindow(std::size_t n, double fs, double f0 = 0.0) {
    REQUIRE(fs > 0.0);
    ReadWindow w;
    w.start = static_cast<std::size_t>(std::llround(MassDynamicsTest::kSettleSeconds * fs));
    w.length = TestUtils::kLowFrequencyFftSize;
    REQUIRE(w.start + w.length <= n);
    if (f0 > 0.0) {
        const double period = fs / f0;
        const double periods = std::floor(static_cast<double>(w.length) / period);
        REQUIRE(periods >= 1.0);
        w.wholePeriods = static_cast<std::size_t>(std::llround(periods * period));
        w.wholePeriods = std::min(w.wholePeriods, w.length);
    }
    return w;
}

/// (L + R) / 2 per sample.
[[nodiscard]] inline std::vector<float> midOf(const std::vector<float>& L, const std::vector<float>& R) {
    REQUIRE(L.size() == R.size());
    std::vector<float> m(L.size(), 0.0f);
    for (std::size_t i = 0; i < L.size(); ++i)
        m[i] = 0.5f * (L[i] + R[i]);
    return m;
}

/// The descriptor floor (spec descriptor block "Floors", reading refinement 2026-10-19): each harmonic power in a
/// D_cx / D_nl ratio is floored at kDescriptorFloorRelDb below the reference tap's fundamental power w_1, not at an
/// absolute -120 dB. With the absolute floor, A's character tap (a -100 dBFS n >= 9 residue of the ROUND stage) against
/// a numerically silent linear core read as +11.7 dB of "nonlinearity" and no showcase B could travel 3 dB (harvest
/// run 2). -60 dB is Phase 7's G8-1 validity floor, applied relatively so the descriptors stay scale-free.
inline constexpr double kDescriptorFloorRelDb = -60.0;
[[nodiscard]] inline double floorPower(double referenceW1) {
    return std::max(referenceW1, 1e-300) * std::pow(10.0, kDescriptorFloorRelDb / 10.0);
}

/// w_n (n = 1 .. ceil(fs / 2f0)) of the read-window frame of x.
[[nodiscard]] inline std::vector<double> readWindowHarmonicPowers(const std::vector<float>& x, double fs, double f0) {
    REQUIRE(f0 > 0.0);
    const ReadWindow w = readWindow(x.size(), fs);
    const int N = static_cast<int>(std::ceil(0.5 * fs / f0));
    return ProfundumTest::renderedHarmonicPowers(x.data() + w.start, fs, f0, N);
}

/// Σ_{n >= nMin} w_n (n is 1-based).
[[nodiscard]] inline double powerFrom(const std::vector<double>& w, std::size_t nMin) {
    double s = 0.0;
    for (std::size_t i = nMin - 1; i < w.size(); ++i)
        s += w[i];
    return s;
}

/// D_cx on a channel pair: 10·log10(Σ_{n>=2} w_n / w_1) of the mid, each power floored at -60 dB below w_1.
[[nodiscard]] inline double dCxDb(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs) {
    const std::vector<double> w = readWindowHarmonicPowers(midOf(L, R), fs, f0);
    REQUIRE(!w.empty());
    const double fl = floorPower(w[0]);
    return 10.0 * std::log10(std::max(powerFrom(w, 2), fl) / std::max(w[0], fl));
}

/// D_cx (sine -> complex; steady): the core tap.
[[nodiscard]] inline double dCxDb(const StereoFieldTest::FieldChainTaps& taps, double f0, double fs) {
    return dCxDb(taps.coreL, taps.coreR, f0, fs);
}

/// D_nl (clean -> nonlinear; steady): Σ_{n>=9} w_n of the character tap over that of the LINEAR core (the twin
/// core with excitation mix 0; reading refinement 2026-10-19: the core tap already carries the Phase 2 excitation
/// nonlinearity, so against it the post-core stages read as less nonlinear on every harvest candidate), both mids,
/// each floored.
[[nodiscard]] inline double dNlDb(const MorphChainTaps& taps, double f0, double fs) {
    REQUIRE(taps.linearCoreL.size() == taps.characterL.size());
    const std::vector<double> wc = readWindowHarmonicPowers(midOf(taps.characterL, taps.characterR), fs, f0);
    const std::vector<double> wk = readWindowHarmonicPowers(midOf(taps.linearCoreL, taps.linearCoreR), fs, f0);
    REQUIRE(!wk.empty());
    const double fl = floorPower(wk[0]);   // -60 dB below the linear core's fundamental
    return 10.0 * std::log10(std::max(powerFrom(wc, 9), fl) / std::max(powerFrom(wk, 9), fl));
}

/// D_st (mono -> stereo; steady): side-to-mid of the output above 2·f_c (Phase 7's descriptor).
[[nodiscard]] inline double dStDb(const StereoFieldTest::FieldChainTaps& taps, double fcHz, double fs) {
    REQUIRE(taps.outL.size() == taps.outR.size());
    const ReadWindow w = readWindow(taps.outL.size(), fs);
    return StereoFieldTest::bandReading(taps.outL.data() + w.start, taps.outR.data() + w.start, w.length, fs,
                                        2.0 * fcHz, 0.5 * fs)
        .sideToMidDb;
}

/// D_st on the clean core tap (B10-4, 2026-10-19): the SC-008(a) monotone read; the output version above is recorded.
/// Carries Phase 7's band validity (G8-1, -60 dBFS): a near-sine core has nothing above 2 f_c, and the side-to-mid
/// of that residue is not a width reading (E1 p = 0.05 / 0.1 read -19.5 / -23.8 dB on float residue).
struct DstCoreReading {
    double db = StereoFieldTest::kFloorDb;
    bool valid = false;
};
[[nodiscard]] inline DstCoreReading dStCore(const StereoFieldTest::FieldChainTaps& taps, double fcHz, double fs) {
    REQUIRE(taps.coreL.size() == taps.coreR.size());
    const ReadWindow w = readWindow(taps.coreL.size(), fs);
    const StereoFieldTest::BandReading b = StereoFieldTest::bandReading(
        taps.coreL.data() + w.start, taps.coreR.data() + w.start, w.length, fs, 2.0 * fcHz, 0.5 * fs);
    return {b.sideToMidDb, b.valid};
}
[[nodiscard]] inline double dStDbCore(const StereoFieldTest::FieldChainTaps& taps, double fcHz, double fs) {
    return dStCore(taps, fcHz, fs).db;
}

/// D_br (dark -> bright; steady): the spectral centroid of the output mid, octaves above f0.
[[nodiscard]] inline double dBrOct(const StereoFieldTest::FieldChainTaps& taps, double f0, double fs) {
    const std::vector<float> mid = midOf(taps.outL, taps.outR);
    const ReadWindow w = readWindow(mid.size(), fs);
    const int N = static_cast<int>(std::ceil(0.5 * fs / f0));
    return ProfundumTest::describeRendered(mid.data() + w.start, fs, f0, N).centroidOct;
}

/// L_K (steady): the mean of the K-weighted level of outL and outR over the read window (measureFrom = settle).
[[nodiscard]] inline double lKDb(const StereoFieldTest::FieldChainTaps& taps, double fs) {
    REQUIRE(taps.outL.size() == taps.outR.size());
    const ReadWindow w = readWindow(taps.outL.size(), fs);
    const std::size_t end = w.start + w.length;
    return 0.5 * (TestUtils::kWeightedLevelDb(taps.outL.data(), end, fs, w.start)
                  + TestUtils::kWeightedLevelDb(taps.outR.data(), end, fs, w.start));
}

/// E_sub (steady) of any tap pair — the f0 component (spec descriptor block, reading refinement 2026-10-19): the mean
/// of MassDynamicsTest::subBand(mid).dftDb (the one-period single-bin DFT amplitude at f0, 20·log10(2|X|/P)) over the
/// trace entries whose one-period window lies inside the read window trimmed to whole periods, expressed as RMS dB
/// (−20·log10 √2) so a sine of amplitude a reads 20·log10(a/√2). The 1.5·f0 low-pass RMS (eSubLpDb) is recorded
/// beside it, not gated: at the monster end h1 is ≈ 35 dB down and the low-pass skirt passes h2 (coherence probe).
[[nodiscard]] inline double eSubDb(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs) {
    const std::vector<float> mid = midOf(L, R);
    const ReadWindow w = readWindow(mid.size(), fs, f0);
    const std::size_t end = w.start + w.wholePeriods;
    const MassDynamicsTest::SubBand sb = MassDynamicsTest::subBand(mid.data(), end, fs, f0);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < sb.dftDb.size(); ++i) {
        const std::size_t s = i * sb.hop;
        if (s >= w.start && s + sb.windowSamples <= end) {
            sum += sb.dftDb[i];
            ++count;
        }
    }
    REQUIRE(count > 0);
    return sum / static_cast<double>(count) - 20.0 * std::log10(std::sqrt(2.0));
}

/// The recorded reader: the mean of subBand(mid).lpRmsDb (one-period RMS of the 4th-order Butterworth low-pass at
/// 1.5·f0) over the same entries. Phase 6's pumping reader; reported beside eSubDb, never gated in Phase 8.
[[nodiscard]] inline double eSubLpDb(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs) {
    const std::vector<float> mid = midOf(L, R);
    const ReadWindow w = readWindow(mid.size(), fs, f0);
    const std::size_t end = w.start + w.wholePeriods;
    const MassDynamicsTest::SubBand sb = MassDynamicsTest::subBand(mid.data(), end, fs, f0);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < sb.lpRmsDb.size(); ++i) {
        const std::size_t s = i * sb.hop;
        if (s >= w.start && s + sb.windowSamples <= end) {
            sum += sb.lpRmsDb[i];
            ++count;
        }
    }
    REQUIRE(count > 0);
    return sum / static_cast<double>(count);
}

/// One-period energies of x from sample 0: period k = [round(k·P), round((k+1)·P)), P = fs / f0; e2 = mean square,
/// ms = the period's length in ms. Only whole periods that fit in x.
struct PeriodEnergies {
    std::vector<double> e2;
    std::vector<double> ms;
};

[[nodiscard]] inline PeriodEnergies periodEnergies(const std::vector<float>& x, double f0, double fs) {
    REQUIRE(f0 > 0.0);
    REQUIRE(fs > 0.0);
    const double period = fs / f0;
    PeriodEnergies pe;
    for (std::size_t k = 0;; ++k) {
        const auto b = static_cast<std::size_t>(std::llround(static_cast<double>(k) * period));
        const auto e = static_cast<std::size_t>(std::llround(static_cast<double>(k + 1) * period));
        if (e > x.size() || e <= b)
            break;
        double sq = 0.0;
        for (std::size_t i = b; i < e; ++i)
            sq += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        pe.e2.push_back(sq / static_cast<double>(e - b));
        pe.ms.push_back(1000.0 * static_cast<double>(e - b) / fs);
    }
    return pe;
}

/// The 80 dB fall-off: some period after the peak period whose energy is >= 80 dB below the peak's.
struct FallOff {
    bool reached = false;
    double peakDb = StereoFieldTest::kFloorDb;          ///< 10·log10(e_peak²)
    double lowestAfterPeakDb = StereoFieldTest::kFloorDb;   ///< the quietest period after the peak, dB re the peak
};

[[nodiscard]] inline FallOff fallOff(const PeriodEnergies& pe) {
    FallOff f;
    if (pe.e2.empty())
        return f;
    const auto peakIt = std::max_element(pe.e2.begin(), pe.e2.end());
    const double peak = *peakIt;
    f.peakDb = ProfundumTest::safeDb10(peak);
    if (!(peak > 0.0))
        return f;
    double lowest = peak;
    for (auto it = peakIt + 1; it != pe.e2.end(); ++it)
        lowest = std::min(lowest, *it);
    f.lowestAfterPeakDb = ProfundumTest::safeDb10(lowest / peak);
    f.reached = lowest <= peak * 1e-8;
    return f;
}

/// D_t on a channel pair: Σ e_k²·P_k / e_peak² over the whole periods of the mid (ms). REQUIREs the 80 dB fall-off
/// before the end of the render.
[[nodiscard]] inline double dTMs(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs) {
    const PeriodEnergies pe = periodEnergies(midOf(L, R), f0, fs);
    const FallOff fo = fallOff(pe);
    INFO("D_t: peak " << fo.peakDb << " dB, quietest period after the peak " << fo.lowestAfterPeakDb << " dB re peak");
    REQUIRE(fo.reached);
    const double peak = *std::max_element(pe.e2.begin(), pe.e2.end());
    double d = 0.0;
    for (std::size_t k = 0; k < pe.e2.size(); ++k)
        d += pe.e2[k] * pe.ms[k];
    return d / peak;
}

/// D_t (short -> long; gate render): the output.
[[nodiscard]] inline double dTMs(const StereoFieldTest::FieldChainTaps& taps, double f0, double fs) {
    return dTMs(taps.outL, taps.outR, f0, fs);
}

/// D_t on the pre-dynamics voice-sum mid (B10-4, 2026-10-19): before MassDynamics and the limiter; the output
/// version above is recorded.
[[nodiscard]] inline double dTMsSum(const StereoFieldTest::FieldChainTaps& taps, double f0, double fs) {
    return dTMs(taps.sumL, taps.sumR, f0, fs);
}

/// The gate render (spec descriptor block): amp envelope on, note-on at 0, note-off at fs · 1.0 s rounded down to a
/// multiple of 32, length note-off + 2 s, extended in 1 s steps up to 12 s until the output mid's one-period RMS
/// is >= 80 dB below its peak; 12 s without it FAILs with the reading printed.
[[nodiscard]] inline MorphChainTaps gateRender(const MorphChainConfig& cfg) {
    REQUIRE(cfg.f0 > 0.0);
    REQUIRE(cfg.fs > 0.0);
    constexpr double kMaxGateSeconds = 12.0;
    MorphChainConfig c = cfg;
    c.ampEnvelope = true;
    c.noteOnSample = 0;
    const std::size_t off = (static_cast<std::size_t>(std::floor(cfg.fs * 1.0)) / kGrid) * kGrid;
    c.noteOffSample = off;
    FallOff last{};
    double lastSeconds = 0.0;
    for (double sec = static_cast<double>(off) / cfg.fs + 2.0; sec <= kMaxGateSeconds + 1e-9; sec += 1.0) {
        c.seconds = sec;
        MorphChainTaps t = renderMorphChain(c);
        last = fallOff(periodEnergies(midOf(t.outL, t.outR), cfg.f0, cfg.fs));
        lastSeconds = sec;
        if (last.reached)
            return t;
    }
    FAIL("gate render did not fall 80 dB below its peak within " << kMaxGateSeconds << " s (last length "
                                                                   << lastSeconds << " s, peak " << last.peakDb
                                                                   << " dB, quietest period after the peak "
                                                                   << last.lowestAfterPeakDb << " dB re peak)");
    return {};
}

// -----------------------------------------------------------------------------
// SC-004 transition windows (deviation 10)
// -----------------------------------------------------------------------------

enum class TransitionKind : std::uint8_t {
    CharacterAlgorithm,   ///< one period (character pre-roll) + 10 ms crossfade
    AnchorWaveform,       ///< 5 ms
    AnchorMode,           ///< 5 ms
    FieldMode,            ///< one period + 334 intervals (the full-range pan slew)
    FieldMotion,          ///< the four field.motion* selectors: as FieldMode
};

/// Samples from the switch grid sample g_sw to the component's transition end.
[[nodiscard]] inline std::size_t transitionEndSamples(TransitionKind k, double f0, double fs) {
    REQUIRE(f0 > 0.0);
    REQUIRE(fs > 0.0);
    const auto period = static_cast<std::size_t>(std::ceil(fs / f0));
    switch (k) {
        case TransitionKind::CharacterAlgorithm:
            return period + static_cast<std::size_t>(std::llround(0.010 * fs));
        case TransitionKind::AnchorWaveform:
        case TransitionKind::AnchorMode:
            return static_cast<std::size_t>(std::llround(0.005 * fs));
        case TransitionKind::FieldMode:
        case TransitionKind::FieldMotion:
            return period + kPanSlewIntervals * kGrid;
    }
    return 0;
}

}  // namespace Krate::DSP::BassMorphTest
