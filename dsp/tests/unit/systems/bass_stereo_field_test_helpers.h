// ==============================================================================
// Test helpers: the Phase 7 validation chain and the stereo descriptors
//                    (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// tasks.md T007 (plan S10; FR-070; plan S1 C-12, C-16; deviations 7, 8). Test-side only: allocation is
// fine here. Read-only after T007 (tasks.md file map).
//
// Every helper is an `inline` function in namespace Krate::DSP::StereoFieldTest (never an anonymous
// namespace in a header; tasks.md rule 6). The helper namespaces it builds on (MassDynamicsTest,
// ProfundumTest, ShapeCharacterTest, HarmonicFoldTest) define same-named functions, so every call into
// them is qualified and no `using namespace` is opened.
//
// plan S1 C-12: the Phase 6 header (mass_dynamics_test_helpers.h) is included READ-ONLY. Its tail
// (processChainTail, :302-340) has no insertion point for the stage, so processFieldTail below copies its
// statements and inserts the stage after MassDynamics. With every Phase 7 stage off, the call sequence is
// renderChain's exactly (core -> Anchor -> Fold -> tail), so BassStereoField_ChainMatchesPhase6 is bit-exact.
//
// The chain (plan S10, FR-070):
//   ProfundumCore (pan vector from BassStereoField::computePanVector) -> [ShapeMorph] -> HarmonicFold
//   -> [BassCharacterStage, WIDE offsets]  (tap characterL/R)  + AnchorOscillator -> sum -> [pre-gain]
//   -> MassDynamics (tap dynL/R = the stage input) -> BassStereoField::processBlock (tap stageOutL/R)
//   -> DCBlocker x2 (dcL/R) -> TruePeakLimiter (outL/R)
//
// Recorded decisions (evidence/run-log/T007.txt):
//   - bypassField (plan S10: "stage not run, vector not handed") runs neither the stage nor the core's pan
//     handoff. tasks.md T011 needs the vector handed with the stage bypassed (SC-009(a), coreL/R only), so
//     `bypassHandsVector` (default false) hands it anyway. The law is advanced, queried and recorded in
//     both cases; the WIDE offsets are always handed (S10 names only the vector).
//   - A retrigger "from perInterval" (S10 step 3) has no channel in the hook's signature: the only note-on
//     is chain.noteOnSample. perInterval runs on the main field only (never on the generatorGravity field)
//     and must be a pure function of (sample, field): restage() replays it.
//   - vectorPerInterval records the vector handed to the core (after forceSubCentre), one per grid point.
//   - With f0At, the per-sample f0 feeds the core, the Anchor, ShapeMorph and the character stage
//     (f0PerSample); HarmonicFold, the detector key and the Anchor's a1 take f0At at each grid point.
// ==============================================================================

#pragma once

#include "../processors/harmonic_fold_test_helpers.h"
#include "../processors/shape_character_metric_helpers.h"
#include "bass_stereo_field_probe.h"
#include "mass_dynamics_test_helpers.h"
#include "profundum_core_test_helpers.h"
#include "shape_character_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/shape_morph.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Krate::DSP::StereoFieldTest {

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

inline constexpr std::size_t kGrid = MassDynamicsTest::kGrid;
static_assert(kGrid == BassStereoField::kControlInterval, "the field shares the chain's 32-sample grid (FR-074)");

inline constexpr double kTwoPi = 6.283185307179586476925286766559;
inline constexpr double kFloorDb = -120.0;              ///< Q, side-to-mid and W_n floor (spec descriptor block)
inline constexpr double kValidMeanEnergy = 1e-6;        ///< -60 dBFS: a band below this is not read (G8-1; was -100)
inline constexpr std::size_t kMaxBandWindow = std::size_t{1} << 18;
inline constexpr double kMotionWindowSeconds = 2.73;    ///< deviation 8: 2^17 samples at 48 kHz
inline constexpr double kMotionFirstWindowSeconds = 1.0;
inline constexpr double kSubBandQBarDb = 1.0;           ///< SC-001(a)
inline constexpr double kOctaveQBarDb = -3.11;          ///< SC-001(b)
inline constexpr double kSubBandRhoBar = 0.99;          ///< SC-002
inline constexpr double kAntiPhaseRhoPre = -0.5;        ///< G8-1: character-tap sub band predominantly anti-phase -> recorded

/// Bit equality of two float vectors (same size, memcmp; L against R of one render or two renders of the
/// same build in one process: tasks.md rule 8).
inline bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size())
        return false;
    if (a.empty())
        return true;
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// -----------------------------------------------------------------------------
// The validation chain (plan S10)
// -----------------------------------------------------------------------------

struct FieldChainConfig {
    MassDynamicsTest::ChainConfig chain;                 ///< Phase 6: f0, fs, seconds, hostBlock, attachAnchor, Mass, overrides
    BassStereoField::Controls field{};
    std::function<BassStereoField::Controls(std::size_t sample)> fieldAt;   ///< empty -> `field`; read at each grid point
    bool bypassField = false;            ///< C-12: stage not run, vector not handed (see bypassHandsVector)
    bool bypassHandsVector = false;      ///< with bypassField: hand the law's vector anyway (T011, SC-009(a))
    bool shapeOn = true;                 ///< dirty arm: ShapeMorph at `shape`
    float shape = 1.0f;                  ///< SHAPE 1 (BITE)
    std::optional<float> foldAmount;     ///< dirty arm: 100 (else the law's)
    std::optional<BassCharacterStage::Algorithm> algorithm;   ///< empty -> character stage off
    float characterAmount = 1.0f;
    float characterTone = 0.5f;
    ProfundumCore::ExcitationControls excitation = ProfundumCore::kDefaultExcitation;
    bool forceSubCentre = false;         ///< SC-006: entries with (k + 1) f0 < f_c,s set to +0 before the handoff
    std::optional<float> generatorGravity;   ///< SC-006: the vector comes from a second field at this Gravity
    std::function<void(std::size_t sample, BassStereoField&)> perInterval;   ///< test hook (steps, seeds, tempo)
    std::function<double(std::size_t sample)> f0At;      ///< empty -> chain.f0 (per-sample f0, glides)
    std::optional<float> sideFloorOverride;  ///< g_floor on both fields (probe, before prepare)
    std::optional<float> wideScaleOverride;  ///< K_wide on both fields (probe, before prepare)
    bool perSampleStage = false;         ///< the stage one sample at a time; fills modeFadePerSample
};

struct FieldChainTaps : MassDynamicsTest::ChainTaps {
    std::vector<float> characterL, characterR;   ///< dirty-arm output: post-character, pre-Anchor (FR-070)
    std::vector<float> stageOutL, stageOutR;     ///< safety-stage output, before DC x2
    std::vector<std::array<float, kMaxPartials>> vectorPerInterval;   ///< the vectors handed (SC-008, SC-009(a))
    std::vector<BassStereoField::ChannelOffsets> offsetsPerInterval;  ///< the offsets handed ({+0, +0} without a character stage)
    std::vector<float> lawCrossoverPerInterval;   ///< smoothedCrossoverHz() after advanceControl (SC-009(c))
    std::vector<float> stageCrossoverPerInterval; ///< stageCrossoverHz() after advanceControl (SC-009(c))
    std::vector<float> modeFadePerSample;         ///< modeFade() after each sample (perSampleStage only)
    std::vector<float> h1DeliveredPerInterval;    ///< core.deliveredGains()[0] after the interval's first core run (SC-018)
};

inline bool sameExcitation(const ProfundumCore::ExcitationControls& a, const ProfundumCore::ExcitationControls& b) {
    return std::bit_cast<std::uint32_t>(a.mix) == std::bit_cast<std::uint32_t>(b.mix) &&
           std::bit_cast<std::uint32_t>(a.drive) == std::bit_cast<std::uint32_t>(b.drive) &&
           std::bit_cast<std::uint32_t>(a.asymmetry) == std::bit_cast<std::uint32_t>(b.asymmetry);
}

inline BassStereoField::Controls fieldControlsAt(const FieldChainConfig& cfg, std::size_t n) {
    return cfg.fieldAt ? cfg.fieldAt(n) : cfg.field;
}

/// A field with the probe overrides applied BEFORE prepare (prepare's reset() snaps the side-gain smoother
/// to the instance's floor), the controls of sample 0 (Gravity replaced by `gravity` when set), prepared
/// at the chain's rate. `floorOverride` wins over cfg.sideFloorOverride (restage).
inline std::unique_ptr<BassStereoField> makeField(const FieldChainConfig& cfg, std::optional<float> gravity,
                                                  std::optional<float> floorOverride = std::nullopt) {
    auto f = std::make_unique<BassStereoField>();
    const std::optional<float> floorGain = floorOverride.has_value() ? floorOverride : cfg.sideFloorOverride;
    if (floorGain.has_value())
        Krate::DSP::detail::BassStereoFieldProbe::setSideFloorGain(*f, *floorGain);
    if (cfg.wideScaleOverride.has_value())
        Krate::DSP::detail::BassStereoFieldProbe::setWideOffsetScale(*f, *cfg.wideScaleOverride);
    BassStereoField::Controls c = fieldControlsAt(cfg, 0);
    if (gravity.has_value())
        c.gravity = *gravity;
    f->setControls(c);
    f->prepare(cfg.chain.fs, 512);
    return f;
}

/// plan S10 step 1: setControls(fieldAt(start)), then perInterval (main field only), then advanceControl().
inline void advanceFieldAt(BassStereoField& f, const FieldChainConfig& cfg, std::size_t start, bool runHook,
                           std::optional<float> gravity) {
    BassStereoField::Controls c = fieldControlsAt(cfg, start);
    if (gravity.has_value())
        c.gravity = *gravity;
    f.setControls(c);
    if (runHook && cfg.perInterval)
        cfg.perInterval(start, f);
    f.advanceControl();
}

inline void allocateFieldTaps(FieldChainTaps& t, std::size_t total, const FieldChainConfig& cfg) {
    MassDynamicsTest::allocateTaps(t, total, cfg.chain.perSampleDynamicsTaps);
    for (std::vector<float>* v : {&t.characterL, &t.characterR, &t.stageOutL, &t.stageOutR})
        v->assign(total, 0.0f);
    if (cfg.perSampleStage)
        t.modeFadePerSample.assign(total, 0.0f);
    const std::size_t intervals = (total + kGrid - 1) / kGrid;
    t.vectorPerInterval.reserve(intervals);
    t.offsetsPerInterval.reserve(intervals);
    t.lawCrossoverPerInterval.reserve(intervals);
    t.stageCrossoverPerInterval.reserve(intervals);
    t.h1DeliveredPerInterval.reserve(intervals);
}

/// dyn -> [stage] (tap stageOut) -> DC x2 (tap dc) -> limiter (tap out), over [start, start + len).
/// stage == nullptr: stageOut is a copy of dyn.
inline void processStageAndOutput(FieldChainTaps& taps, const FieldChainConfig& cfg, BassStereoField* stage,
                                  DCBlocker& dcL, DCBlocker& dcR, TruePeakLimiter& limiter, std::size_t start,
                                  std::size_t len) {
    const std::size_t end = start + len;
    std::copy_n(taps.dynL.data() + start, len, taps.stageOutL.data() + start);
    std::copy_n(taps.dynR.data() + start, len, taps.stageOutR.data() + start);
    if (stage != nullptr) {
        if (cfg.perSampleStage) {
            for (std::size_t i = start; i < end; ++i) {
                stage->processBlock(taps.stageOutL.data() + i, taps.stageOutR.data() + i, 1);
                taps.modeFadePerSample[i] = stage->modeFade();
            }
        } else {
            stage->processBlock(taps.stageOutL.data() + start, taps.stageOutR.data() + start, len);
        }
    }
    std::copy_n(taps.stageOutL.data() + start, len, taps.dcL.data() + start);
    std::copy_n(taps.stageOutR.data() + start, len, taps.dcR.data() + start);
    dcL.processBlock(taps.dcL.data() + start, len);
    dcR.processBlock(taps.dcR.data() + start, len);
    std::copy_n(taps.dcL.data() + start, len, taps.outL.data() + start);
    std::copy_n(taps.dcR.data() + start, len, taps.outR.data() + start);
    limiter.processBlock(taps.outL.data() + start, taps.outR.data() + start, static_cast<int>(len));
}

/// MassDynamicsTest::processChainTail's statements (mass_dynamics_test_helpers.h:302-340) with the sum
/// taken from the character tap and the stage inserted after MassDynamics (C-12: the Phase 6 header is
/// never edited).
inline void processFieldTail(FieldChainTaps& taps, const FieldChainConfig& cfg, MassDynamics& md, DCBlocker& dcL,
                             DCBlocker& dcR, TruePeakLimiter& limiter, BassStereoField* stage, std::size_t start,
                             std::size_t len, std::size_t total) {
    const MassDynamicsTest::ChainConfig& cc = cfg.chain;
    const std::size_t end = start + len;
    for (std::size_t i = start; i < end; ++i) {
        taps.sumL[i] = taps.characterL[i] + taps.anchorL[i];
        taps.sumR[i] = taps.characterR[i] + taps.anchorR[i];
    }
    if (cc.preDynamicsGainDb) {
        for (std::size_t i = start; i < end; ++i) {
            const auto g = static_cast<float>(std::pow(10.0, static_cast<double>(cc.preDynamicsGainDb(i)) / 20.0));
            taps.dynL[i] = taps.sumL[i] * g;
            taps.dynR[i] = taps.sumR[i] * g;
        }
    } else {
        std::copy_n(taps.sumL.data() + start, len, taps.dynL.data() + start);
        std::copy_n(taps.sumR.data() + start, len, taps.dynR.data() + start);
    }
    if (cc.perSampleDynamicsTaps) {
        for (std::size_t i = start; i < end; ++i) {
            md.processBlock(taps.dynL.data() + i, taps.dynR.data() + i, 1);
            taps.shaperGainDb[i] = md.shaper().currentGainDb();
            taps.compGainDb[i] = md.compressor().currentGainDb();
            taps.compGrDb[i] = md.compressor().currentGainReductionDb();
        }
    } else {
        md.processBlock(taps.dynL.data() + start, taps.dynR.data() + start, len);
    }
    processStageAndOutput(taps, cfg, stage, dcL, dcR, limiter, start, len);
    if (end % kGrid == 0 || end == total)
        taps.cornerHzPerInterval.push_back(md.compressor().detectorCornerHz());
}

/// plan S10 exactly (the order of steps 1-6 at each grid point; see the banner for the recorded decisions).
inline FieldChainTaps renderFieldChain(const FieldChainConfig& cfg) {
    const MassDynamicsTest::ChainConfig& cc = cfg.chain;
    REQUIRE(cc.f0 > 0.0);
    REQUIRE(cc.fs > 0.0);
    REQUIRE(cc.hostBlock > 0);
    REQUIRE(cc.noteOnSample % kGrid == 0);
    const std::size_t total = MassDynamicsTest::renderLength(cc.fs, cc.seconds);
    REQUIRE(total > 0);
    const auto f0 = static_cast<float>(cc.f0);

    std::vector<float> f0Track;
    if (cfg.f0At) {
        REQUIRE(cc.noteOnSample < total);
        f0Track.resize(total);
        for (std::size_t i = 0; i < total; ++i)
            f0Track[i] = static_cast<float>(cfg.f0At(i));
    }
    const float noteF0 = f0Track.empty() ? f0 : f0Track[cc.noteOnSample];

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(cc.fs);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    if (!sameExcitation(cfg.excitation, ProfundumCore::kDefaultExcitation))
        core->setExcitation(cfg.excitation);
    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(cc.fs);
    anchor->setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(cc.fs, 512);
    fold->setFundamentalHz(noteF0);
    std::unique_ptr<ShapeMorph> morph;
    if (cfg.shapeOn) {
        morph = std::make_unique<ShapeMorph>();   // configured before prepare: prepare's reset() snaps SHAPE
        morph->setShape(cfg.shape);
        morph->setFundamentalHz(noteF0);
        morph->prepare(cc.fs, 512);
    }
    std::unique_ptr<BassCharacterStage> character;
    if (cfg.algorithm.has_value()) {
        character = std::make_unique<BassCharacterStage>();   // before prepare: no crossfade, smoothers snapped
        character->setAlgorithm(*cfg.algorithm);
        character->setAmount(cfg.characterAmount);
        character->setTone(cfg.characterTone);
        character->setFundamentalHz(noteF0);
        character->prepare(cc.fs, 512);
    }
    MassLaw law;
    MassDynamicsTest::prepareChainLaw(law, cc);
    MassDynamicsTest::ChainDynamics dyn = MassDynamicsTest::makeChainDynamics(cc);

    std::unique_ptr<BassStereoField> field = makeField(cfg, std::nullopt);
    std::unique_ptr<BassStereoField> generator;
    if (cfg.generatorGravity.has_value())
        generator = makeField(cfg, cfg.generatorGravity);
    BassStereoField::VoiceState vs{};
    BassStereoField::VoiceState vsGen{};
    std::array<float, kMaxPartials> pan{};
    const bool handVector = !cfg.bypassField || cfg.bypassHandsVector;

    FieldChainTaps taps;
    allocateFieldTaps(taps, total, cfg);
    const float a1Unbiased = MassDynamicsTest::unbiasedH1Amplitude(cc.base, cc.f0, cc.fs);   // Phase 6 FR-031 (G8-3)
    float a1Next = a1Unbiased;
    float a1 = 0.0f;

    MassDynamicsTest::gridDriver(total, cc.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        const bool sounding = start >= cc.noteOnSample;
        const float* f0p = f0Track.empty() ? nullptr : f0Track.data() + start;
        if (atGrid) {
            // 1. The field's controls, the hook, advanceControl (plan S1 C-7).
            advanceFieldAt(*field, cfg, start, true, std::nullopt);
            if (generator)
                advanceFieldAt(*generator, cfg, start, false, cfg.generatorGravity);
            taps.lawCrossoverPerInterval.push_back(field->smoothedCrossoverHz());
            taps.stageCrossoverPerInterval.push_back(field->stageCrossoverHz());

            // 2. The Phase 6 targets and controls, as renderChain (mass_dynamics_test_helpers.h:400-412).
            const float fNow = f0Track.empty() ? f0 : f0Track[start];
            float massUsed = 0.0f;
            const MassLaw::Targets t = MassDynamicsTest::chainTargets(law, cc, start, massUsed);
            core->setControls(MassDynamicsTest::coreControlsFor(cc.base, t));
            fold->setAmount(cfg.foldAmount.has_value() ? *cfg.foldAmount : t.foldAmount);
            if (!f0Track.empty()) {
                fold->setFundamentalHz(fNow);
                a1Next = MassDynamicsTest::unbiasedH1Amplitude(cc.base, static_cast<double>(fNow), cc.fs);
            }
            anchor->setControls(MassDynamicsTest::anchorControlsFor(t.anchorLevel));
            taps.anchorControlsPerInterval.push_back(anchor->pendingControls());
            taps.anchorLevelPerInterval.push_back(t.anchorLevel);
            taps.massPerInterval.push_back(massUsed);
            taps.targetsPerInterval.push_back(t);
            dyn.md->setTransientDurationMs(t.transientDurationMs);
            dyn.md->setCompressionAmount(t.compressionAmount);
            dyn.md->setOutputTrimDb(t.outputTrimDb);
            dyn.md->setDetectorKeyHz(fNow);

            // 3. The note-on BEFORE the law query (FR-015 Reset exemption).
            if (start == cc.noteOnSample) {
                core->noteOn(noteF0);
                anchor->noteOn(noteF0, true);
                vs.reset();
                vsGen.reset();
            }

            // 4. The law query: the core's f0, or the note's f0 at and before the first note-on.
            const float fq = (start <= cc.noteOnSample) ? noteF0 : core->currentFrequency();
            if (generator)
                generator->computePanVector(vsGen, fq, pan, core->numPartials());
            else
                field->computePanVector(vs, fq, pan, core->numPartials());

            // 5. SC-006 precondition: the generator below f_c,s forced to centre (+0, tasks.md rule 4).
            if (cfg.forceSubCentre) {
                const float fc = (generator ? *generator : *field).smoothedCrossoverHz();
                for (std::size_t k = 0; k < kMaxPartials; ++k) {
                    if (static_cast<float>(k + 1) * fq < fc)
                        pan[k] = 0.0f;
                }
            }
            taps.vectorPerInterval.push_back(pan);

            // 6. The handoffs.
            if (handVector)
                core->setPartialPanOffsets(pan);
            BassStereoField::ChannelOffsets o{.amount = 0.0f, .tone = 0.0f};
            if (character) {
                o = field->wideOffsets(*cfg.algorithm);
                character->setChannelOffsets(o.amount, o.tone);
            }
            taps.offsetsPerInterval.push_back(o);
        }
        if (sounding) {
            core->processBlock(taps.coreL.data() + start, taps.coreR.data() + start, len, f0p);
            a1 = a1Next;
            anchor->setCoreH1Amplitude(a1);
            if (cc.attachAnchor)
                anchor->processBlock(taps.anchorL.data() + start, taps.anchorR.data() + start, len, f0p);
            std::copy_n(taps.coreL.data() + start, len, taps.foldL.data() + start);
            std::copy_n(taps.coreR.data() + start, len, taps.foldR.data() + start);
            if (morph)
                morph->processBlock(taps.foldL.data() + start, taps.foldR.data() + start, len, f0p);
            fold->processBlock(taps.foldL.data() + start, taps.foldR.data() + start, len);
            std::copy_n(taps.foldL.data() + start, len, taps.characterL.data() + start);
            std::copy_n(taps.foldR.data() + start, len, taps.characterR.data() + start);
            if (character)
                character->processBlock(taps.characterL.data() + start, taps.characterR.data() + start, len, f0p);
        }
        if (atGrid) {
            taps.a1PerInterval.push_back(sounding ? a1 : 0.0f);
            taps.h1DeliveredPerInterval.push_back(sounding ? core->deliveredGains()[0] : 0.0f);
        }
        processFieldTail(taps, cfg, *dyn.md, dyn.dcL, dyn.dcR, *dyn.limiter, cfg.bypassField ? nullptr : field.get(),
                         start, len, total);
    });
    return taps;
}

/// Deviation 7: re-run only the stage on the recorded stage input (dynL/R) at another g_floor. A fresh
/// field (the same overrides, floor = floorGain, set before prepare) replays the fieldAt / perInterval
/// sequence and the advanceControl cadence on the same host partition; fresh DC x2 + limiter from
/// makeChainDynamics(cfg.chain). Returns a copy of t with stageOut, dc and out (and modeFadePerSample with
/// perSampleStage) replaced. At the field's own floor it reproduces t bit for bit.
inline FieldChainTaps restage(const FieldChainTaps& t, const FieldChainConfig& cfg, float floorGain) {
    const MassDynamicsTest::ChainConfig& cc = cfg.chain;
    REQUIRE_FALSE(cfg.bypassField);
    REQUIRE(cc.hostBlock > 0);
    const std::size_t total = t.dynL.size();
    REQUIRE(total == MassDynamicsTest::renderLength(cc.fs, cc.seconds));
    FieldChainTaps r = t;
    if (cfg.perSampleStage)
        r.modeFadePerSample.assign(total, 0.0f);
    std::unique_ptr<BassStereoField> field = makeField(cfg, std::nullopt, floorGain);
    MassDynamicsTest::ChainDynamics dyn = MassDynamicsTest::makeChainDynamics(cc);
    MassDynamicsTest::gridDriver(total, cc.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid)
            advanceFieldAt(*field, cfg, start, true, std::nullopt);
        processStageAndOutput(r, cfg, field.get(), dyn.dcL, dyn.dcR, *dyn.limiter, start, len);
    });
    return r;
}

/// One POLY voice (Edge case "POLY"; record only): core -> [ShapeMorph] -> Fold -> [character], own VoiceState.
struct PolyFieldVoice {
    float f0 = 0.0f;
    bool sounding = false;
    std::unique_ptr<ProfundumCore> core;
    std::unique_ptr<ShapeMorph> morph;
    std::unique_ptr<HarmonicFold> fold;
    std::unique_ptr<BassCharacterStage> character;
    BassStereoField::VoiceState vs{};
};

/// One voice chain per f0 (in order) at the law's shared targets, summed; + the Anchor of the lowest
/// sounding voice; then the shared tail and one field stage. common.chain.f0 is ignored. The core / fold /
/// character taps are the voice sums; vectorPerInterval records the lowest voice's vector. f0At,
/// generatorGravity and forceSubCentre are not supported (REQUIREd unset).
inline FieldChainTaps renderFieldPoly(std::span<const double> f0s, const FieldChainConfig& common) {
    const MassDynamicsTest::ChainConfig& cc = common.chain;
    REQUIRE(!f0s.empty());
    REQUIRE(cc.fs > 0.0);
    REQUIRE(cc.hostBlock > 0);
    REQUIRE(cc.noteOnSample % kGrid == 0);
    REQUIRE_FALSE(static_cast<bool>(common.f0At));
    REQUIRE_FALSE(common.generatorGravity.has_value());
    REQUIRE_FALSE(common.forceSubCentre);
    const std::size_t total = MassDynamicsTest::renderLength(cc.fs, cc.seconds);
    REQUIRE(total > 0);

    std::vector<float> keys;
    std::vector<PolyFieldVoice> voices(f0s.size());
    for (std::size_t v = 0; v < f0s.size(); ++v) {
        PolyFieldVoice& pv = voices[v];
        pv.f0 = static_cast<float>(f0s[v]);
        keys.push_back(pv.f0);
        pv.sounding = detail::isFinite(pv.f0) && pv.f0 > 0.0f;
        pv.core = std::make_unique<ProfundumCore>();
        pv.core->prepare(cc.fs);
        pv.core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
        if (!sameExcitation(common.excitation, ProfundumCore::kDefaultExcitation))
            pv.core->setExcitation(common.excitation);
        pv.fold = std::make_unique<HarmonicFold>();
        pv.fold->prepare(cc.fs, 512);
        if (pv.sounding)
            pv.fold->setFundamentalHz(pv.f0);
        if (common.shapeOn) {
            pv.morph = std::make_unique<ShapeMorph>();
            pv.morph->setShape(common.shape);
            if (pv.sounding)
                pv.morph->setFundamentalHz(pv.f0);
            pv.morph->prepare(cc.fs, 512);
        }
        if (common.algorithm.has_value()) {
            pv.character = std::make_unique<BassCharacterStage>();
            pv.character->setAlgorithm(*common.algorithm);
            pv.character->setAmount(common.characterAmount);
            pv.character->setTone(common.characterTone);
            if (pv.sounding)
                pv.character->setFundamentalHz(pv.f0);
            pv.character->prepare(cc.fs, 512);
        }
    }
    const float key = MassDynamicsTest::lowestSoundingF0(std::span<const float>(keys.data(), keys.size()));
    REQUIRE(key > 0.0f);
    std::size_t lowest = 0;
    for (std::size_t v = 0; v < voices.size(); ++v) {
        if (voices[v].sounding && std::bit_cast<std::uint32_t>(voices[v].f0) == std::bit_cast<std::uint32_t>(key)) {
            lowest = v;
            break;
        }
    }

    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(cc.fs);
    anchor->setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
    const float a1 = MassDynamicsTest::unbiasedH1Amplitude(cc.base, static_cast<double>(key), cc.fs);
    MassLaw law;
    MassDynamicsTest::prepareChainLaw(law, cc);
    MassDynamicsTest::ChainDynamics dyn = MassDynamicsTest::makeChainDynamics(cc);
    std::unique_ptr<BassStereoField> field = makeField(common, std::nullopt);
    const bool handVector = !common.bypassField || common.bypassHandsVector;

    FieldChainTaps taps;
    allocateFieldTaps(taps, total, common);
    std::array<float, kGrid> cL{}, cR{}, fL{}, fR{};
    std::array<float, kMaxPartials> pan{};
    std::array<float, kMaxPartials> lowestPan{};

    MassDynamicsTest::gridDriver(total, cc.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        const bool sounding = start >= cc.noteOnSample;
        if (atGrid) {
            advanceFieldAt(*field, common, start, true, std::nullopt);
            taps.lawCrossoverPerInterval.push_back(field->smoothedCrossoverHz());
            taps.stageCrossoverPerInterval.push_back(field->stageCrossoverHz());
            float massUsed = 0.0f;
            const MassLaw::Targets t = MassDynamicsTest::chainTargets(law, cc, start, massUsed);
            const BassStereoField::ChannelOffsets o =
                common.algorithm.has_value() ? field->wideOffsets(*common.algorithm)
                                             : BassStereoField::ChannelOffsets{.amount = 0.0f, .tone = 0.0f};
            for (std::size_t v = 0; v < voices.size(); ++v) {
                PolyFieldVoice& pv = voices[v];
                pv.core->setControls(MassDynamicsTest::coreControlsFor(cc.base, t));
                pv.fold->setAmount(common.foldAmount.has_value() ? *common.foldAmount : t.foldAmount);
                if (start == cc.noteOnSample && pv.sounding) {
                    pv.core->noteOn(pv.f0);
                    pv.vs.reset();
                }
                if (!pv.sounding)
                    continue;
                const float fq = (start <= cc.noteOnSample) ? pv.f0 : pv.core->currentFrequency();
                field->computePanVector(pv.vs, fq, pan, pv.core->numPartials());
                if (handVector)
                    pv.core->setPartialPanOffsets(pan);
                if (pv.character)
                    pv.character->setChannelOffsets(o.amount, o.tone);
                if (v == lowest)
                    lowestPan = pan;
            }
            anchor->setControls(MassDynamicsTest::anchorControlsFor(t.anchorLevel));
            if (start == cc.noteOnSample)
                anchor->noteOn(key, true);
            taps.anchorControlsPerInterval.push_back(anchor->pendingControls());
            taps.anchorLevelPerInterval.push_back(t.anchorLevel);
            taps.massPerInterval.push_back(massUsed);
            taps.targetsPerInterval.push_back(t);
            taps.vectorPerInterval.push_back(lowestPan);
            taps.offsetsPerInterval.push_back(o);
            dyn.md->setTransientDurationMs(t.transientDurationMs);
            dyn.md->setCompressionAmount(t.compressionAmount);
            dyn.md->setOutputTrimDb(t.outputTrimDb);
            dyn.md->setDetectorKeyHz(key);
        }
        if (sounding) {
            for (PolyFieldVoice& pv : voices) {
                if (!pv.sounding)
                    continue;
                pv.core->processBlock(cL.data(), cR.data(), len);
                std::copy_n(cL.data(), len, fL.data());
                std::copy_n(cR.data(), len, fR.data());
                if (pv.morph)
                    pv.morph->processBlock(fL.data(), fR.data(), len);
                pv.fold->processBlock(fL.data(), fR.data(), len);
                for (std::size_t i = 0; i < len; ++i) {
                    taps.coreL[start + i] += cL[i];
                    taps.coreR[start + i] += cR[i];
                    taps.foldL[start + i] += fL[i];
                    taps.foldR[start + i] += fR[i];
                }
                if (pv.character)
                    pv.character->processBlock(fL.data(), fR.data(), len);
                for (std::size_t i = 0; i < len; ++i) {
                    taps.characterL[start + i] += fL[i];
                    taps.characterR[start + i] += fR[i];
                }
            }
            anchor->setCoreH1Amplitude(a1);
            if (cc.attachAnchor)
                anchor->processBlock(taps.anchorL.data() + start, taps.anchorR.data() + start, len);
        }
        if (atGrid) {
            taps.a1PerInterval.push_back(sounding ? a1 : 0.0f);
            taps.h1DeliveredPerInterval.push_back(sounding ? voices[lowest].core->deliveredGains()[0] : 0.0f);
        }
        processFieldTail(taps, common, *dyn.md, dyn.dcL, dyn.dcR, *dyn.limiter,
                         common.bypassField ? nullptr : field.get(), start, len, total);
    });
    return taps;
}

// -----------------------------------------------------------------------------
// Band descriptors (spec descriptor block; deviation 8: Parseval over the masked bins)
// -----------------------------------------------------------------------------

struct Band {
    double lo = 0.0;   ///< Hz, inclusive
    double hi = 0.0;   ///< Hz, exclusive
};

/// The largest power of two <= min(n, cap) (1 when n < 2).
inline std::size_t largestPow2AtMost(std::size_t n, std::size_t cap = kMaxBandWindow) {
    std::size_t w = 1;
    while (w * 2 <= n && w * 2 <= cap)
        w *= 2;
    return w;
}

/// The power of two nearest x (linear distance; ties go down).
inline std::size_t nearestPow2(double x) {
    std::size_t lo = 1;
    while (static_cast<double>(lo * 2) <= x)
        lo *= 2;
    const std::size_t hi = lo * 2;
    return (x - static_cast<double>(lo) <= static_cast<double>(hi) - x) ? lo : hi;
}

/// Periodic Hann window, w[k] = 0.5 - 0.5 cos(2 pi k / W).
inline std::vector<double> hannWindow(std::size_t W) {
    std::vector<double> w(W, 0.0);
    for (std::size_t k = 0; k < W; ++k)
        w[k] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(k) / static_cast<double>(W));
    return w;
}

/// The Hann-tapered spectra of L and R over the first W = largestPow2AtMost(n) samples (the same taper).
struct BandSpectra {
    std::vector<std::complex<double>> L, R;
    std::size_t length = 0;
    double sumW2 = 0.0;
};

inline BandSpectra bandSpectra(const float* L, const float* R, std::size_t n) {
    REQUIRE(L != nullptr);
    REQUIRE(R != nullptr);
    const std::size_t W = largestPow2AtMost(n);
    REQUIRE(W >= 2);
    const std::vector<double> w = hannWindow(W);
    BandSpectra s;
    s.length = W;
    s.L.resize(W);
    s.R.resize(W);
    for (std::size_t k = 0; k < W; ++k) {
        s.L[k] = std::complex<double>(static_cast<double>(L[k]) * w[k], 0.0);
        s.R[k] = std::complex<double>(static_cast<double>(R[k]) * w[k], 0.0);
        s.sumW2 += w[k] * w[k];
    }
    ProfundumTest::fftDouble(s.L);
    ProfundumTest::fftDouble(s.R);
    return s;
}

/// Time-domain energies of the tapered, zero-phase-masked signals by Parseval: ll = sum l_m^2,
/// rr = sum r_m^2, lr = sum l_m r_m, over the bins k <= W/2 with k fs / W in [fLo, fHi) and their mirrors.
struct BandEnergies {
    double ll = 0.0;
    double rr = 0.0;
    double lr = 0.0;
    double sumW2 = 0.0;      ///< sum of the squared taper (mean-square normalisation)
    std::size_t length = 0;  ///< W
};

inline BandEnergies bandEnergiesFromSpectra(const BandSpectra& s, double fs, double fLo, double fHi) {
    const std::size_t W = s.length;
    BandEnergies e;
    e.sumW2 = s.sumW2;
    e.length = W;
    for (std::size_t k = 0; k <= W / 2; ++k) {
        const double f = static_cast<double>(k) * fs / static_cast<double>(W);
        if (!(f >= fLo && f < fHi))
            continue;
        const double weight = (k == 0 || k == W / 2) ? 1.0 : 2.0;
        e.ll += weight * std::norm(s.L[k]);
        e.rr += weight * std::norm(s.R[k]);
        e.lr += weight * (s.L[k] * std::conj(s.R[k])).real();
    }
    const double inv = 1.0 / static_cast<double>(W);
    e.ll *= inv;
    e.rr *= inv;
    e.lr *= inv;
    return e;
}

inline BandEnergies bandEnergies(const float* L, const float* R, std::size_t n, double fs, double fLo, double fHi) {
    return bandEnergiesFromSpectra(bandSpectra(L, R, n), fs, fLo, fHi);
}

struct BandReading {
    double q = 0.0;               ///< Q_band, dB (floored at kFloorDb); 0 for a silent band
    double rho = 0.0;             ///< zero-lag correlation after the mask; 0 when a channel is silent
    double sideToMidDb = kFloorDb;   ///< sum |L - R|^2 / sum |L + R|^2, dB (floored at kFloorDb)
    double meanEnergy = 0.0;      ///< (E[L^2] + E[R^2]) / 2 of the band, taper-compensated
    bool valid = false;           ///< meanEnergy >= kValidMeanEnergy (-60 dBFS, G8-1)
};

inline BandReading readingFromEnergies(const BandEnergies& e) {
    const double mid4 = std::max(e.ll + e.rr + 2.0 * e.lr, 0.0);    // sum |L + R|^2
    const double side4 = std::max(e.ll + e.rr - 2.0 * e.lr, 0.0);   // sum |L - R|^2
    const double meanPow = 0.5 * (e.ll + e.rr);
    BandReading r;
    r.q = meanPow > 0.0 ? std::max(ProfundumTest::safeDb10(0.25 * mid4 / meanPow), kFloorDb) : 0.0;
    r.rho = (e.ll > 0.0 && e.rr > 0.0) ? e.lr / std::sqrt(e.ll * e.rr) : 0.0;
    r.sideToMidDb = std::max(ProfundumTest::safeDb10(side4 / std::max(mid4, 1e-300)), kFloorDb);
    if (!(side4 > 0.0))
        r.sideToMidDb = kFloorDb;
    r.meanEnergy = e.sumW2 > 0.0 ? meanPow / e.sumW2 : 0.0;
    r.valid = r.meanEnergy >= kValidMeanEnergy;
    return r;
}

/// One Hann-tapered window of the largest power of two <= n (<= 2^18) from L[0] / R[0], FFT, Parseval
/// over the bins in [fLo, fHi) (deviation 8).
inline BandReading bandReading(const float* L, const float* R, std::size_t n, double fs, double fLo, double fHi) {
    return readingFromEnergies(bandEnergies(L, R, n, fs, fLo, fHi));
}

/// bandReading for several bands of one window (one FFT pair).
inline std::vector<BandReading> bandReadings(const float* L, const float* R, std::size_t n, double fs,
                                             const std::vector<Band>& bands) {
    const BandSpectra s = bandSpectra(L, R, n);
    std::vector<BandReading> out;
    out.reserve(bands.size());
    for (const Band& b : bands)
        out.push_back(readingFromEnergies(bandEnergiesFromSpectra(s, fs, b.lo, b.hi)));
    return out;
}

/// Window starts for moving renders: settle + k * hop while the window [start, start + lenPow2) fits in n.
inline std::vector<std::size_t> windows(std::size_t n, double fs, double settleSeconds, std::size_t lenPow2,
                                        std::size_t hop) {
    REQUIRE(lenPow2 > 0);
    REQUIRE(hop > 0);
    std::vector<std::size_t> starts;
    const auto settle = static_cast<std::size_t>(std::llround(settleSeconds * fs));
    for (std::size_t s = settle; s + lenPow2 <= n; s += hop)
        starts.push_back(s);
    return starts;
}

/// [fc 2^i, fc 2^(i+1)) up to 10 kHz, the last band truncated at 10 kHz.
inline std::vector<Band> octaveBands(double fc) {
    constexpr double kTop = 10000.0;
    REQUIRE(fc > 0.0);
    std::vector<Band> bands;
    for (double lo = fc; lo < kTop; lo *= 2.0)
        bands.push_back(Band{.lo = lo, .hi = std::min(2.0 * lo, kTop)});
    return bands;
}

/// Third octaves [fLo 2^(i/3), fLo 2^((i+1)/3)) up to fHi, the last band truncated at fHi.
inline std::vector<Band> thirdOctaves(double fLo, double fHi) {
    REQUIRE(fLo > 0.0);
    std::vector<Band> bands;
    for (int i = 0;; ++i) {
        const double lo = fLo * std::exp2(static_cast<double>(i) / 3.0);
        if (!(lo < fHi))
            break;
        bands.push_back(Band{.lo = lo, .hi = std::min(fLo * std::exp2(static_cast<double>(i + 1) / 3.0), fHi)});
    }
    return bands;
}

/// W_n = wS_n / wM_n in dB (floored at kFloorDb), n = 1..N, from ProfundumTest::renderedHarmonicPowers of
/// M = (L + R) / 2 and S = (L - R) / 2 (computed in double, rounded to float for the helper) over the first
/// TestUtils::kLowFrequencyFftSize samples.
inline std::vector<double> widthDb(const float* L, const float* R, double fs, double f0, int N) {
    REQUIRE(L != nullptr);
    REQUIRE(R != nullptr);
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    std::vector<float> m(kFrame, 0.0f);
    std::vector<float> s(kFrame, 0.0f);
    for (std::size_t i = 0; i < kFrame; ++i) {
        const auto l = static_cast<double>(L[i]);
        const auto r = static_cast<double>(R[i]);
        m[i] = static_cast<float>(0.5 * (l + r));
        s[i] = static_cast<float>(0.5 * (l - r));
    }
    const std::vector<double> wM = ProfundumTest::renderedHarmonicPowers(m.data(), fs, f0, N);
    const std::vector<double> wS = ProfundumTest::renderedHarmonicPowers(s.data(), fs, f0, N);
    std::vector<double> out(wM.size(), kFloorDb);
    for (std::size_t n = 0; n < wM.size(); ++n) {
        if (wM[n] > 0.0 && wS[n] > 0.0)
            out[n] = std::max(ProfundumTest::safeDb10(wS[n] / wM[n]), kFloorDb);
    }
    return out;
}

/// Amplitude and sine phase of one tone (C-16; ProfundumTest::goertzelPhase gives the phase only, of a
/// BH7-windowed DFT): x ~ amplitude * sin(2 pi hz k / fs + phase) over x[0..L), by the rectangular
/// single-bin DFT (exact over whole periods). phase in (-pi, pi].
struct ToneFit {
    double amplitude = 0.0;
    double phase = 0.0;
};

inline double wrapPhase(double p) {
    const double half = 0.5 * kTwoPi;
    p = std::fmod(p, kTwoPi);
    if (p > half)
        p -= kTwoPi;
    else if (p <= -half)
        p += kTwoPi;
    return p;
}

inline ToneFit goertzelComplex(const float* x, std::size_t L, double fs, double hz) {
    REQUIRE(x != nullptr);
    REQUIRE(L > 0);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double ph = kTwoPi * hz * static_cast<double>(k) / fs;
        const auto v = static_cast<double>(x[k]);
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    ToneFit t;
    t.amplitude = 2.0 * std::hypot(re, im) / static_cast<double>(L);
    t.phase = wrapPhase(std::atan2(im, re) + 0.25 * kTwoPi);   // a sin(w k + phi) = a cos(w k + phi - pi/2)
    return t;
}

// -----------------------------------------------------------------------------
// Pan references (plan S1 C-16)
// -----------------------------------------------------------------------------

enum class PanReferenceMode : std::uint8_t { Held, Interpolated };

/// Harmonic n (index n - 1) of a settled Spread 0 render: amplitude a_n on ONE channel (it carries the
/// bank's centre pan gain) and the sine phase phi_n referred to sample 0.
struct FittedHarmonic {
    double amplitude = 0.0;
    double phase = 0.0;
};

/// (a_n, phi_n), n = 1..N, by goertzelComplex at n f0 over x[start, start + length) (whole periods), each
/// phase rotated back to sample 0 (static f0).
inline std::vector<FittedHarmonic> fitHarmonics(const float* x, std::size_t start, std::size_t length, double fs,
                                                double f0, int N) {
    REQUIRE(x != nullptr);
    REQUIRE(N >= 1);
    std::vector<FittedHarmonic> h(static_cast<std::size_t>(N));
    for (int n = 1; n <= N; ++n) {
        const double hz = static_cast<double>(n) * f0;
        const ToneFit t = goertzelComplex(x + start, length, fs, hz);
        h[static_cast<std::size_t>(n - 1)] = FittedHarmonic{
            .amplitude = t.amplitude, .phase = wrapPhase(t.phase - kTwoPi * hz * static_cast<double>(start) / fs)};
    }
    return h;
}

struct PanReference {
    std::vector<double> L, R;
};

/// Synthesised in double: L/R = sum_n a_n sin(phi_n + 2 pi n cyc(i)) * sqrt(2) * {cos, sin}(pi/4 + p_n pi/4)
/// (the bank's constant-power law; sqrt(2) undoes the centre gain carried by a_n), cyc(i) = sum_{j<i} f0At(j)
/// / fs. p_n is entry n - 1 of the interval's vector: Held applies vector i / 32 for the whole interval
/// (what the core does); Interpolated interpolates p linearly between interval centres (i = 32 q + 15.5),
/// held flat before the first and after the last centre. Past the last vector the last one holds.
template <class F0At>
inline PanReference renderPanReference(double fs, std::size_t total, const F0At& f0At,
                                       const std::vector<FittedHarmonic>& harmonics,
                                       const std::vector<std::array<float, kMaxPartials>>& vectorsPerInterval,
                                       PanReferenceMode mode) {
    REQUIRE(fs > 0.0);
    REQUIRE(!vectorsPerInterval.empty());
    REQUIRE(harmonics.size() <= kMaxPartials);
    const std::size_t N = harmonics.size();
    const std::size_t Q = vectorsPerInterval.size();
    const double quarterPi = 0.125 * kTwoPi;
    const double sqrt2 = std::sqrt(2.0);
    const auto g = static_cast<double>(kGrid);
    PanReference out;
    out.L.assign(total, 0.0);
    out.R.assign(total, 0.0);
    double cyc = 0.0;
    for (std::size_t i = 0; i < total; ++i) {
        std::size_t q0 = std::min(i / kGrid, Q - 1);
        std::size_t q1 = q0;
        double frac = 0.0;
        if (mode == PanReferenceMode::Interpolated) {
            const double t = (static_cast<double>(i) - 0.5 * (g - 1.0)) / g;
            if (t <= 0.0) {
                q0 = 0;
                q1 = 0;
            } else {
                q0 = static_cast<std::size_t>(std::floor(t));
                if (q0 + 1 >= Q) {
                    q0 = Q - 1;
                    q1 = Q - 1;
                } else {
                    q1 = q0 + 1;
                    frac = t - static_cast<double>(q0);
                }
            }
        }
        double l = 0.0;
        double r = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            const auto p0 = static_cast<double>(vectorsPerInterval[q0][k]);
            const auto p1 = static_cast<double>(vectorsPerInterval[q1][k]);
            // One expression for both modes (Held has q1 == q0 and frac == 0, so p == p0 exactly): a mode
            // branch here lets /fp:fast compile the two modes' sin / cos differently (8e-9 apart, measured).
            const double p = (1.0 - frac) * p0 + frac * p1;
            const double theta = quarterPi + p * quarterPi;
            const double x = harmonics[k].amplitude *
                             std::sin(harmonics[k].phase + kTwoPi * static_cast<double>(k + 1) * cyc);
            l += x * sqrt2 * std::cos(theta);
            r += x * sqrt2 * std::sin(theta);
        }
        out.L[i] = l;
        out.R[i] = r;
        cyc += static_cast<double>(f0At(i)) / fs;
        cyc -= std::floor(cyc);   // n * cyc stays exact modulo whole cycles
    }
    return out;
}

// -----------------------------------------------------------------------------
// Mono-sum cells (SC-001, SC-002; deviation 8)
// -----------------------------------------------------------------------------

enum class MonoSumTap : std::uint8_t { Core, Character, Dyn, StageOut, Out };
inline constexpr std::size_t kNumMonoSumTaps = 5;
inline constexpr std::array<const char*, kNumMonoSumTaps> kMonoSumTapNames{"core", "character", "dyn", "stageOut",
                                                                            "out"};

inline std::pair<const std::vector<float>*, const std::vector<float>*> tapPair(const FieldChainTaps& t, MonoSumTap tap) {
    switch (tap) {
        case MonoSumTap::Core: return {&t.coreL, &t.coreR};
        case MonoSumTap::Character: return {&t.characterL, &t.characterR};
        case MonoSumTap::Dyn: return {&t.dynL, &t.dynR};
        case MonoSumTap::StageOut: return {&t.stageOutL, &t.stageOutR};
        case MonoSumTap::Out: return {&t.outL, &t.outR};
    }
    return {&t.outL, &t.outR};
}

/// Folds reading b into the worst-so-far acc: valid windows win over invalid ones; among windows of the
/// same validity, the lowest Q (Q <= 0 dB always, so also the largest |Q|), the lowest rho, the largest
/// side-to-mid and the lowest mean energy.
inline void mergeWorst(BandReading& acc, const BandReading& b, bool first) {
    if (first || (!acc.valid && b.valid)) {
        acc = b;
        return;
    }
    if (acc.valid != b.valid)
        return;
    acc.q = std::min(acc.q, b.q);
    acc.rho = std::min(acc.rho, b.rho);
    acc.sideToMidDb = std::max(acc.sideToMidDb, b.sideToMidDb);
    acc.meanEnergy = std::min(acc.meanEnergy, b.meanEnergy);
}

struct TapReading {
    BandReading sub{};                  ///< [0, f_c), worst over windows
    std::vector<BandReading> octaves;   ///< octaveBands(f_c), worst over windows
};

struct MonoSumCell {
    double crossoverHz = 0.0;
    std::vector<Band> bands;            ///< the octave bands read
    std::size_t windowCount = 0;
    std::size_t windowLength = 0;       ///< samples per window (before the power-of-two cut)
    std::array<TapReading, kNumMonoSumTaps> taps{};
    bool nonlinearArm = false;      ///< a character stage is in the chain (dirty or Anchor arm): octave Q recorded (G8-1)
    bool subHarmonicInBand = true;  ///< G10-2: a harmonic lies in [20 Hz, f_c); otherwise the sub-band gates are recorded
    bool antiPhaseExempt = false;   ///< G8-1: character-tap sub band anti-correlated (rho_pre < 0): sub-band gates recorded
};

/// The cell's readings at every tap. The controls of sample 0 (fieldAt(0) or `field`) give f_c and the
/// MOTION depth. Depth 0: one window over [kSettleSeconds, end). Depth > 0: windows of the power of two
/// nearest 2.73 s at fs, hop the same, from 1 s; the worst window per reading.
inline MonoSumCell evaluateMonoSumCell(const FieldChainTaps& t, const FieldChainConfig& cfg) {
    const double fs = cfg.chain.fs;
    const BassStereoField::Controls c = fieldControlsAt(cfg, 0);
    const std::size_t total = t.outL.size();
    MonoSumCell cell;
    cell.crossoverHz = static_cast<double>(c.crossoverHz);
    cell.bands = octaveBands(cell.crossoverHz);
    std::vector<std::size_t> starts;
    if (c.motionDepth > 0.0f) {
        cell.windowLength = nearestPow2(kMotionWindowSeconds * fs);
        starts = windows(total, fs, kMotionFirstWindowSeconds, cell.windowLength, cell.windowLength);
    } else {
        const auto settle = static_cast<std::size_t>(std::llround(MassDynamicsTest::kSettleSeconds * fs));
        REQUIRE(total > settle);
        cell.windowLength = total - settle;
        starts.push_back(settle);
    }
    REQUIRE(!starts.empty());
    cell.windowCount = starts.size();

    std::vector<Band> all;
    all.push_back(Band{.lo = 0.0, .hi = cell.crossoverHz});
    all.insert(all.end(), cell.bands.begin(), cell.bands.end());

    for (std::size_t tap = 0; tap < kNumMonoSumTaps; ++tap) {
        const auto [L, R] = tapPair(t, static_cast<MonoSumTap>(tap));
        REQUIRE(L->size() == total);
        REQUIRE(R->size() == total);
        TapReading& tr = cell.taps[tap];
        tr.octaves.assign(cell.bands.size(), BandReading{});
        for (std::size_t w = 0; w < starts.size(); ++w) {
            const std::vector<BandReading> rs =
                bandReadings(L->data() + starts[w], R->data() + starts[w], cell.windowLength, fs, all);
            mergeWorst(tr.sub, rs[0], w == 0);
            for (std::size_t b = 0; b < cell.bands.size(); ++b)
                mergeWorst(tr.octaves[b], rs[b + 1], w == 0);
        }
    }
    // G8-1 (spec SC-001(b), SC-002): the arm kind and the anti-phase exemption.
    cell.nonlinearArm = cfg.algorithm.has_value();
    cell.subHarmonicInBand = cfg.chain.f0 < cell.crossoverHz;   // G10-2 (the spec's Band is a set of harmonics)
    const BandReading& pre = cell.taps[static_cast<std::size_t>(MonoSumTap::Character)].sub;
    cell.antiPhaseExempt = cell.nonlinearArm && pre.valid && pre.rho <= kAntiPhaseRhoPre;
    return cell;
}

/// SC-001 / SC-002 per cell: |Q_sub| <= 1 dB at core, stageOut and out; every valid octave Q >= -3.11 dB at
/// the same taps; rho_sub >= 0.99 at core and stageOut. A sub band below -100 dBFS (no harmonic below f_c)
/// is not read.
/// G8-1 (ruled 2026-10-18): the gates read the taps the SC rows name, the generator output (SC-001(c), SC-002) and
/// the safety-stage output; the chain output after Phase 6's DC and limiter is recorded. The octave bar is gated on
/// the clean core only and recorded on the nonlinear arms; a nonlinear-arm cell whose character-tap sub band is
/// predominantly anti-phase (antiPhaseExempt, rho_pre <= kAntiPhaseRhoPre) is recorded, not gated.
inline bool monoSumCellPasses(const MonoSumCell& cell) {
    bool ok = true;
    for (const MonoSumTap tap : {MonoSumTap::Core, MonoSumTap::StageOut}) {
        const TapReading& tr = cell.taps[static_cast<std::size_t>(tap)];
        const bool subGated = cell.subHarmonicInBand && !cell.antiPhaseExempt;   // G10-2, G8-1
        if (subGated && tr.sub.valid && !(std::abs(tr.sub.q) <= kSubBandQBarDb))
            ok = false;
        if (!cell.nonlinearArm) {
            for (const BandReading& b : tr.octaves) {
                if (b.valid && !(b.q >= kOctaveQBarDb))
                    ok = false;
            }
        }
        if (subGated && tr.sub.valid && !(tr.sub.rho >= kSubBandRhoBar))
            ok = false;
    }
    return ok;
}

inline std::string monoSumCsvHeader() {
    return "cell,tap,fc_hz,windows,sub_valid,q_sub_db,rho_sub,s2m_sub_db,sub_mean_dbfs,oct_valid,oct_worst_q_db,"
           "oct_worst_lo_hz,oct_worst_rho,oct_q_db_list";
}

/// One CSV line per tap (joined by '\n', no trailing newline), setprecision(6).
inline std::string monoSumCsvLine(const MonoSumCell& cell, const std::string& label) {
    std::ostringstream os;
    os << std::setprecision(6);
    for (std::size_t tap = 0; tap < kNumMonoSumTaps; ++tap) {
        const TapReading& tr = cell.taps[tap];
        std::size_t validCount = 0;
        double worstQ = 0.0;
        double worstLo = 0.0;
        double worstRho = 1.0;
        std::ostringstream list;
        list << std::setprecision(6);
        for (std::size_t b = 0; b < tr.octaves.size(); ++b) {
            const BandReading& r = tr.octaves[b];
            if (b > 0)
                list << ';';
            if (!r.valid) {
                list << "na";
                continue;
            }
            list << r.q;
            if (validCount == 0 || r.q < worstQ) {
                worstQ = r.q;
                worstLo = cell.bands[b].lo;
            }
            worstRho = std::min(worstRho, r.rho);
            ++validCount;
        }
        if (tap > 0)
            os << '\n';
        os << label << ',' << kMonoSumTapNames[tap] << ',' << cell.crossoverHz << ',' << cell.windowCount << ','
           << (tr.sub.valid ? 1 : 0) << ',' << tr.sub.q << ',' << tr.sub.rho << ',' << tr.sub.sideToMidDb << ','
           << ProfundumTest::safeDb10(tr.sub.meanEnergy) << ',' << validCount << ',' << worstQ << ',' << worstLo << ','
           << worstRho << ',' << list.str();
    }
    return os.str();
}

// -----------------------------------------------------------------------------
// WIDE aliasing and static lever moves
// -----------------------------------------------------------------------------

/// Phase 5's SC-010 measure (bass_character_spectral_test.cpp:140-149): HarmonicFoldTest::aliasedPower on
/// the last HarmonicFoldTest::aliasFftLength(fs, f) samples after the settle, f = f0 / 2 for Fracture (its
/// output period is 2 / f0, :998) and f0 otherwise.
inline double wideAliasedDbfs(const std::vector<float>& y, double fs, double f0, BassCharacterStage::Algorithm a) {
    const double f = a == BassCharacterStage::Algorithm::Fracture ? 0.5 * f0 : f0;
    const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, f);
    REQUIRE(y.size() >= L + static_cast<std::size_t>(std::llround(fs * MassDynamicsTest::kSettleSeconds)));
    return HarmonicFoldTest::aliasedPower(y.data() + (y.size() - L), L, fs, f).aliasedDbfs;
}

struct LeverMove {
    double sideToMidDb = 0.0;   ///< the largest |delta side-to-mid| over the octave bands, dB
    double rho = 0.0;           ///< the largest |delta rho| over the octave bands
};

/// SC-010 static levers: a against b at stageOutL/R, one window over [kSettleSeconds, end), over
/// octaveBands(fc); only bands valid in both renders count.
inline LeverMove staticLeverMove(const FieldChainTaps& a, const FieldChainTaps& b, double fs, double fc) {
    const auto settle = static_cast<std::size_t>(std::llround(MassDynamicsTest::kSettleSeconds * fs));
    REQUIRE(a.stageOutL.size() > settle);
    REQUIRE(b.stageOutL.size() > settle);
    const std::vector<Band> bands = octaveBands(fc);
    const std::vector<BandReading> ra = bandReadings(a.stageOutL.data() + settle, a.stageOutR.data() + settle,
                                                     a.stageOutL.size() - settle, fs, bands);
    const std::vector<BandReading> rb = bandReadings(b.stageOutL.data() + settle, b.stageOutR.data() + settle,
                                                     b.stageOutL.size() - settle, fs, bands);
    LeverMove m;
    for (std::size_t i = 0; i < bands.size(); ++i) {
        if (!ra[i].valid || !rb[i].valid)
            continue;
        m.sideToMidDb = std::max(m.sideToMidDb, std::abs(ra[i].sideToMidDb - rb[i].sideToMidDb));
        m.rho = std::max(m.rho, std::abs(ra[i].rho - rb[i].rho));
    }
    return m;
}

}  // namespace Krate::DSP::StereoFieldTest
