// ==============================================================================
// Test helpers: the Mass dynamics validation chain and its measurements
//                    (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// tasks.md T011 (plan S9; FR-031 - FR-034; deviations 5, 8). Test-side only: allocation is fine
// here. Read-only after T011 (tasks.md file map).
//
// Every helper is an `inline` function in namespace Krate::DSP::MassDynamicsTest (never an
// anonymous namespace in a header; tasks.md rule 5). Several helper namespaces define same-named
// functions (HarmonicFoldTest / ProfundumTest: safeDb10, fftDouble, aliasFftLength, ...), so every
// call below is qualified.
//
// The chain (plan S9, FR-031 - FR-033):
//   ProfundumCore -> a1 = deliveredGains()[0] · kCenterPanGain -> AnchorOscillator (Sine, LOCK)
//   ProfundumCore -> HarmonicFold
//   sum = fold + anchor (plain float add) -> [preDynamicsGainDb] -> MassDynamics -> DCBlocker x2
//   -> TruePeakLimiter
// MassLaw drives depth / body (core), fold amount, anchor level, transient duration and compression
// amount once per 32-sample interval of the absolute sample counter (plan C-11).
//
// Recorded decisions (tasks.md T011; evidence/run-log/T011.txt):
//   - noteOnSample must sit on the 32-sample grid (REQUIREd): the core, the Anchor and the Fold are
//     not processed before it (their taps are 0, "silence before"), so their own control grids start
//     at a grid point of the absolute counter. MassDynamics, the DC blockers and the limiter run on
//     the silent sum from sample 0.
//   - The shaper gains are set on MassDynamics BEFORE prepare (prepare applies them and its reset()
//     snaps the 10 ms smoothers), so a render does not open with a 10 ms glide from the +6 / 0 dB
//     defaults. configureDynamics runs after prepare, as plan S9 says.
//   - SubBand also carries `hop`, `period` and `windowSamples`: the trace entry i is the one-period
//     window starting at sample i·hop, which mSubSamples needs to map a time range to entries.
//   - lpGroupDelayPeriods is the GROUP delay the plan's C-10 row quotes (analog 0.361 periods):
//     the Goertzel phase difference in -> out is measured for two steady sines at f0·(1 ± 1 %) and
//     differentiated. One tone's phase difference is the phase delay (analog 0.301 periods).
//   - ChainTaps also carries cornerHzPerInterval (BassCompressor::detectorCornerHz() at the end of
//     every interval), for SC-024(d) on renderPoly.
// ==============================================================================

#pragma once

#include "../processors/transient_shaper_test_helpers.h"
#include "mass_dynamics_probe.h"
#include "profundum_core_test_helpers.h"
#include "shape_character_test_helpers.h"

#include <k_weighting.h>

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Krate::DSP::MassDynamicsTest {

// -----------------------------------------------------------------------------
// Grid and render lengths
// -----------------------------------------------------------------------------

/// The shared 32-sample control grid (FR-049, plan C-11).
inline constexpr std::size_t kGrid = MassLaw::kControlInterval;
static_assert(kGrid == ProfundumCore::kControlInterval && kGrid == MassDynamics::kControlInterval &&
                  kGrid == AnchorOscillator::kControlInterval,
              "the chain's stages share one 32-sample grid (plan S11)");

/// Descriptor renders must hold describe()'s full frame (shape_character_test_helpers.h:111-113).
inline constexpr double kSettleSeconds = 1.0;

/// 1 s settle + TestUtils::kLowFrequencyFftSize samples (6.46 s at 48 kHz).
inline double descriptorSeconds(double fs) {
    return kSettleSeconds + static_cast<double>(TestUtils::kLowFrequencyFftSize) / fs;
}

/// round(seconds · fs) samples; seconds <= 0 selects descriptorSeconds(fs).
inline std::size_t renderLength(double fs, double seconds) {
    const double s = seconds > 0.0 ? seconds : descriptorSeconds(fs);
    return static_cast<std::size_t>(std::llround(s * fs));
}

/// Splits host blocks of `hostBlock` samples over [0, total) at every multiple of 32 of the absolute
/// sample counter and calls fn(start, len, atGrid) for each run, in order; atGrid == (start % 32 == 0).
template <class F>
inline void gridDriver(std::size_t total, std::size_t hostBlock, F&& fn) {
    REQUIRE(hostBlock > 0u);
    for (std::size_t b = 0; b < total; b += hostBlock) {
        const std::size_t end = std::min(total, b + hostBlock);
        std::size_t pos = b;
        while (pos < end) {
            const std::size_t next = std::min(end, (pos / kGrid + 1) * kGrid);
            fn(pos, next - pos, pos % kGrid == 0);
            pos = next;
        }
    }
}

// -----------------------------------------------------------------------------
// MassLaw::Targets field access (MassLaw::fieldOf is private)
// -----------------------------------------------------------------------------

inline float& targetField(MassLaw::Targets& t, MassLaw::Target k) noexcept {
    switch (k) {
        case MassLaw::Target::Depth: return t.depth;
        case MassLaw::Target::Body: return t.body;
        case MassLaw::Target::TransientDuration: return t.transientDurationMs;
        case MassLaw::Target::FoldAmount: return t.foldAmount;
        case MassLaw::Target::CompressionAmount: return t.compressionAmount;
        case MassLaw::Target::AnchorLevel: return t.anchorLevel;
        case MassLaw::Target::OutputTrim: return t.outputTrimDb;
    }
    return t.depth;
}

inline float targetField(const MassLaw::Targets& t, MassLaw::Target k) noexcept {
    switch (k) {
        case MassLaw::Target::Depth: return t.depth;
        case MassLaw::Target::Body: return t.body;
        case MassLaw::Target::TransientDuration: return t.transientDurationMs;
        case MassLaw::Target::FoldAmount: return t.foldAmount;
        case MassLaw::Target::CompressionAmount: return t.compressionAmount;
        case MassLaw::Target::AnchorLevel: return t.anchorLevel;
        case MassLaw::Target::OutputTrim: return t.outputTrimDb;
    }
    return t.depth;
}

// -----------------------------------------------------------------------------
// FR-018 / FR-035: the engine-side detector-key rule
// -----------------------------------------------------------------------------

/// The minimum of the positive finite entries; 0 ("no voice") when there are none. Finiteness by
/// detail::isFinite (fast-math immune).
inline float lowestSoundingF0(std::span<const float> f0s) noexcept {
    float best = 0.0f;
    bool found = false;
    for (const float f : f0s) {
        if (!detail::isFinite(f) || !(f > 0.0f))
            continue;
        if (!found || f < best) {
            best = f;
            found = true;
        }
    }
    return found ? best : 0.0f;
}

// -----------------------------------------------------------------------------
// The validation chain (plan S9; FR-031 - FR-034)
// -----------------------------------------------------------------------------

/// Empty, or nullopt at a sample, = the law's value for that target.
using TargetOverride = std::function<std::optional<float>(std::size_t sample)>;

struct ChainConfig {
    SpectralShapeRecipe::Controls base = SpectralShapeRecipe::kDefaultControls;
    double f0 = 0.0;
    double fs = 48000.0;
    double seconds = 0.0;                    ///< <= 0 -> descriptorSeconds(fs)
    std::size_t hostBlock = 512;
    bool attachAnchor = true;
    bool massLawDetached = false;            ///< targets = law.base() (SC-023)
    std::function<float(std::size_t sample)> massAt;                   ///< empty -> 50 (absolute sample)
    std::array<TargetOverride, MassLaw::kNumTargets> targetOverride{};   ///< indexed by MassLaw::Target
    // ---- deviation 8 ----
    std::optional<MassLaw::Targets> lawBase;  ///< empty -> depth / body from `base`, the rest Targets{}
    std::size_t noteOnSample = 0;            ///< must be a multiple of 32; silence before
    std::function<float(std::size_t sample)> preDynamicsGainDb;        ///< empty -> 0 dB; on the sum
    std::function<void(MassDynamics&)> configureDynamics;             ///< called after prepare
    const std::array<MassLaw::Row, MassLaw::kNumTargets>* rowsOverride = nullptr;   ///< evaluateRow per row
    bool unsmoothedMass = false;             ///< targets at massAt(n) directly, no slew (SC-005(a) control)
    float shaperAttackDb = TransientShaper::kDefaultAttackDb;
    float shaperSustainDb = TransientShaper::kDefaultSustainDb;
    bool perSampleDynamicsTaps = false;      ///< MassDynamics one sample at a time; fills the gain taps
};

struct ChainTaps {
    // per sample
    std::vector<float> coreL, coreR, foldL, foldR, anchorL, anchorR, sumL, sumR, dynL, dynR;
    std::vector<float> dcL, dcR;             ///< after the DC blockers = the limiter input
    std::vector<float> outL, outR;
    // per 32-sample interval (recorded at each grid point)
    std::vector<float> a1PerInterval;        ///< a1 fed to the Anchor after the interval's first core run
    std::vector<float> anchorLevelPerInterval;
    std::vector<AnchorOscillator::Controls> anchorControlsPerInterval;   ///< pendingControls() read-back
    std::vector<float> massPerInterval;      ///< the Mass the targets were evaluated at
    std::vector<MassLaw::Targets> targetsPerInterval;   ///< after the overrides
    std::vector<float> cornerHzPerInterval;  ///< BassCompressor::detectorCornerHz() at the interval's end
    // per sample, only with perSampleDynamicsTaps
    std::vector<float> shaperGainDb;         ///< TransientShaper::currentGainDb()
    std::vector<float> compGainDb;           ///< BassCompressor::currentGainDb() (GR + makeup)
    std::vector<float> compGrDb;             ///< BassCompressor::currentGainReductionDb()
};

inline void allocateTaps(ChainTaps& t, std::size_t total, bool perSample) {
    for (std::vector<float>* v : {&t.coreL, &t.coreR, &t.foldL, &t.foldR, &t.anchorL, &t.anchorR, &t.sumL, &t.sumR,
                                  &t.dynL, &t.dynR, &t.dcL, &t.dcR, &t.outL, &t.outR})
        v->assign(total, 0.0f);
    if (perSample) {
        t.shaperGainDb.assign(total, 0.0f);
        t.compGainDb.assign(total, 0.0f);
        t.compGrDb.assign(total, 0.0f);
    }
    const std::size_t intervals = (total + kGrid - 1) / kGrid;
    t.a1PerInterval.reserve(intervals);
    t.anchorLevelPerInterval.reserve(intervals);
    t.anchorControlsPerInterval.reserve(intervals);
    t.massPerInterval.reserve(intervals);
    t.targetsPerInterval.reserve(intervals);
    t.cornerHzPerInterval.reserve(intervals);
}

/// lawBase, or depth / body from `base` with the other four at the Targets{} defaults.
inline MassLaw::Targets lawBaseFor(const ChainConfig& cfg) {
    if (cfg.lawBase.has_value())
        return *cfg.lawBase;
    MassLaw::Targets t{};
    t.depth = cfg.base.depth;
    t.body = cfg.base.body;
    return t;
}

inline float massAtSample(const ChainConfig& cfg, std::size_t n) {
    return cfg.massAt ? cfg.massAt(n) : MassLaw::kNeutralMass;
}

/// One grid point (plan S9 step 1): setMass(massAt(n)), advanceInterval(), the targets (law,
/// rowsOverride, unsmoothedMass, or massLawDetached = law.base()), then each set targetOverride[k]
/// replaces field k. `massUsed` receives the Mass the targets were evaluated at.
inline MassLaw::Targets chainTargets(MassLaw& law, const ChainConfig& cfg, std::size_t n, float& massUsed) {
    const float requested = massAtSample(cfg, n);
    law.setMass(requested);
    const float smoothed = law.advanceInterval();
    massUsed = cfg.unsmoothedMass ? requested : smoothed;
    MassLaw::Targets t{};
    if (cfg.massLawDetached) {
        t = law.base();
    } else if (cfg.rowsOverride != nullptr) {
        for (const MassLaw::Row& row : *cfg.rowsOverride)
            targetField(t, row.target) = MassLaw::evaluateRow(row, massUsed, targetField(law.base(), row.target));
    } else if (cfg.unsmoothedMass) {
        t = MassLaw::evaluate(massUsed, law.base());
    } else {
        t = law.currentTargets();
    }
    for (std::size_t k = 0; k < MassLaw::kNumTargets; ++k) {
        if (!cfg.targetOverride[k])
            continue;
        const std::optional<float> v = cfg.targetOverride[k](n);
        if (v.has_value())
            targetField(t, static_cast<MassLaw::Target>(k)) = *v;
    }
    return t;
}

/// `base` with depth / body from the targets.
inline SpectralShapeRecipe::Controls coreControlsFor(const SpectralShapeRecipe::Controls& base,
                                                     const MassLaw::Targets& t) {
    SpectralShapeRecipe::Controls c = base;
    c.depth = t.depth;
    c.body = t.body;
    return c;
}

/// Sine, LOCK, the given level; every other field at its Controls{} default.
/// FR-031 as amended (user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-3): the a1 fed to
/// the Anchor is the recipe's h1 for the voice's OWN controls, before the Mass Depth / Body biases, times
/// kCenterPanGain, so the Anchor holds the voice's fundamental while Mass thins the core's h1. At Mass 50
/// (no bias) it equals the delivered h1. setCoreH1Amplitude clamps it as before.
inline float unbiasedH1Amplitude(const SpectralShapeRecipe::Controls& base, double f0, double fs) {
    std::array<float, ProfundumCore::kDefaultPartials> g{};
    SpectralShapeRecipe::evaluate(base, static_cast<float>(f0), fs, g);
    return g[0] * SpectralShapeRecipe::kCenterPanGain;
}

inline AnchorOscillator::Controls anchorControlsFor(float level) {
    return AnchorOscillator::Controls{
        .waveform = AnchorOscillator::Waveform::Sine, .mode = AnchorOscillator::Mode::Lock, .level = level};
}

/// The post-voice part of one run: sum = fold + anchor (plain float add, FR-032), the pre-dynamics
/// gain, MassDynamics (per sample with perSampleDynamicsTaps), the DC blockers, the limiter; the
/// corner is recorded at the end of every interval.
inline void processChainTail(ChainTaps& taps, const ChainConfig& cfg, MassDynamics& md, DCBlocker& dcL,
                             DCBlocker& dcR, TruePeakLimiter& limiter, std::size_t start, std::size_t len,
                             std::size_t total) {
    const std::size_t end = start + len;
    for (std::size_t i = start; i < end; ++i) {
        taps.sumL[i] = taps.foldL[i] + taps.anchorL[i];
        taps.sumR[i] = taps.foldR[i] + taps.anchorR[i];
    }
    if (cfg.preDynamicsGainDb) {
        for (std::size_t i = start; i < end; ++i) {
            const auto g = static_cast<float>(std::pow(10.0, static_cast<double>(cfg.preDynamicsGainDb(i)) / 20.0));
            taps.dynL[i] = taps.sumL[i] * g;
            taps.dynR[i] = taps.sumR[i] * g;
        }
    } else {
        std::copy_n(taps.sumL.data() + start, len, taps.dynL.data() + start);
        std::copy_n(taps.sumR.data() + start, len, taps.dynR.data() + start);
    }
    if (cfg.perSampleDynamicsTaps) {
        for (std::size_t i = start; i < end; ++i) {
            md.processBlock(taps.dynL.data() + i, taps.dynR.data() + i, 1);
            taps.shaperGainDb[i] = md.shaper().currentGainDb();
            taps.compGainDb[i] = md.compressor().currentGainDb();
            taps.compGrDb[i] = md.compressor().currentGainReductionDb();
        }
    } else {
        md.processBlock(taps.dynL.data() + start, taps.dynR.data() + start, len);
    }
    std::copy_n(taps.dynL.data() + start, len, taps.dcL.data() + start);
    std::copy_n(taps.dynR.data() + start, len, taps.dcR.data() + start);
    dcL.processBlock(taps.dcL.data() + start, len);
    dcR.processBlock(taps.dcR.data() + start, len);
    std::copy_n(taps.dcL.data() + start, len, taps.outL.data() + start);
    std::copy_n(taps.dcR.data() + start, len, taps.outR.data() + start);
    limiter.processBlock(taps.outL.data() + start, taps.outR.data() + start, static_cast<int>(len));
    if (end % kGrid == 0 || end == total)
        taps.cornerHzPerInterval.push_back(md.compressor().detectorCornerHz());
}

/// The dynamics end of the chain, prepared at fs.
struct ChainDynamics {
    std::unique_ptr<MassDynamics> md;
    DCBlocker dcL, dcR;
    std::unique_ptr<TruePeakLimiter> limiter;
};

inline ChainDynamics makeChainDynamics(const ChainConfig& cfg) {
    ChainDynamics d;
    d.md = std::make_unique<MassDynamics>();
    d.md->setTransientAttackDb(cfg.shaperAttackDb);   // before prepare: see the banner
    d.md->setTransientSustainDb(cfg.shaperSustainDb);
    d.md->prepare(cfg.fs, 512);
    if (cfg.configureDynamics)
        cfg.configureDynamics(*d.md);
    d.dcL.prepare(cfg.fs, 5.0f);   // plan S1 C-10
    d.dcR.prepare(cfg.fs, 5.0f);
    d.limiter = std::make_unique<TruePeakLimiter>();
    d.limiter->prepare(cfg.fs, 512);
    return d;
}

/// The MassLaw, prepared at fs with the chain's base and massAt(0), then reset().
inline void prepareChainLaw(MassLaw& law, const ChainConfig& cfg) {
    law.prepare(cfg.fs);
    law.setBase(lawBaseFor(cfg));
    law.setMass(massAtSample(cfg, 0));
    law.reset();
}

/// Plan S9 exactly (see the file banner for the recorded decisions).
inline ChainTaps renderChain(const ChainConfig& cfg) {
    REQUIRE(cfg.f0 > 0.0);
    REQUIRE(cfg.fs > 0.0);
    REQUIRE(cfg.hostBlock > 0u);
    REQUIRE(cfg.noteOnSample % kGrid == 0u);
    const std::size_t total = renderLength(cfg.fs, cfg.seconds);
    REQUIRE(total > 0u);
    const auto f0 = static_cast<float>(cfg.f0);

    auto core = std::make_unique<ProfundumCore>();
    core->prepare(cfg.fs);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(cfg.fs);
    anchor->setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
    auto fold = std::make_unique<HarmonicFold>();
    fold->prepare(cfg.fs, 512);
    fold->setFundamentalHz(f0);
    MassLaw law;
    prepareChainLaw(law, cfg);
    ChainDynamics dyn = makeChainDynamics(cfg);

    ChainTaps taps;
    allocateTaps(taps, total, cfg.perSampleDynamicsTaps);
    const float a1Unbiased = unbiasedH1Amplitude(cfg.base, cfg.f0, cfg.fs);   // FR-031 (G8-3)
    float a1 = 0.0f;

    gridDriver(total, cfg.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            float massUsed = 0.0f;
            const MassLaw::Targets t = chainTargets(law, cfg, start, massUsed);
            core->setControls(coreControlsFor(cfg.base, t));
            fold->setAmount(t.foldAmount);
            anchor->setControls(anchorControlsFor(t.anchorLevel));
            taps.anchorControlsPerInterval.push_back(anchor->pendingControls());
            taps.anchorLevelPerInterval.push_back(t.anchorLevel);
            taps.massPerInterval.push_back(massUsed);
            taps.targetsPerInterval.push_back(t);
            dyn.md->setTransientDurationMs(t.transientDurationMs);
            dyn.md->setCompressionAmount(t.compressionAmount);
            dyn.md->setOutputTrimDb(t.outputTrimDb);   // FR-021 output trim (G8-5)
            dyn.md->setDetectorKeyHz(f0);
            if (start == cfg.noteOnSample) {
                core->noteOn(f0);           // Reset policy (set above)
                anchor->noteOn(f0, true);   // re-arm
            }
        }
        const bool sounding = start >= cfg.noteOnSample;
        if (sounding) {
            core->processBlock(taps.coreL.data() + start, taps.coreR.data() + start, len);
            a1 = a1Unbiased;   // FR-031 as amended (G8-3): the voice's own h1, not the Mass-biased delivered h1
            anchor->setCoreH1Amplitude(a1);
            if (cfg.attachAnchor)
                anchor->processBlock(taps.anchorL.data() + start, taps.anchorR.data() + start, len);
            std::copy_n(taps.coreL.data() + start, len, taps.foldL.data() + start);
            std::copy_n(taps.coreR.data() + start, len, taps.foldR.data() + start);
            fold->processBlock(taps.foldL.data() + start, taps.foldR.data() + start, len);
        }
        if (atGrid)
            taps.a1PerInterval.push_back(sounding ? a1 : 0.0f);
        processChainTail(taps, cfg, *dyn.md, dyn.dcL, dyn.dcR, *dyn.limiter, start, len, total);
    });
    return taps;
}

// -----------------------------------------------------------------------------
// POLY fixture (SC-024(d), FR-035 poly record)
// -----------------------------------------------------------------------------

/// One voice: a core + Anchor + Fold at the voice's f0. A voice whose f0 is not positive and finite
/// is silent (never processed) and does not key the detector.
struct PolyVoice {
    double f0 = 0.0;
    bool sounding = false;
    std::unique_ptr<ProfundumCore> core;
    std::unique_ptr<AnchorOscillator> anchor;
    std::unique_ptr<HarmonicFold> fold;
};

/// The voices of f0s (in order) at the law's shared targets, summed, into ONE MassDynamics keyed once
/// per interval by lowestSoundingF0(f0s). common.f0 is ignored. The core / fold / anchor taps are the
/// voice sums; a1PerInterval and anchorControlsPerInterval are not recorded (per-voice quantities).
inline ChainTaps renderPoly(std::span<const double> f0s, const ChainConfig& common) {
    REQUIRE(common.fs > 0.0);
    REQUIRE(common.hostBlock > 0u);
    REQUIRE(common.noteOnSample % kGrid == 0u);
    const std::size_t total = renderLength(common.fs, common.seconds);
    REQUIRE(total > 0u);

    std::vector<float> keys;
    std::vector<PolyVoice> voices(f0s.size());
    for (std::size_t v = 0; v < f0s.size(); ++v) {
        PolyVoice& pv = voices[v];
        pv.f0 = f0s[v];
        const auto f = static_cast<float>(pv.f0);
        keys.push_back(f);
        pv.sounding = detail::isFinite(f) && f > 0.0f;
        pv.core = std::make_unique<ProfundumCore>();
        pv.core->prepare(common.fs);
        pv.core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
        pv.anchor = std::make_unique<AnchorOscillator>();
        pv.anchor->prepare(common.fs);
        pv.anchor->setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
        pv.fold = std::make_unique<HarmonicFold>();
        pv.fold->prepare(common.fs, 512);
        if (pv.sounding)
            pv.fold->setFundamentalHz(f);
    }
    const float key = lowestSoundingF0(keys);

    MassLaw law;
    prepareChainLaw(law, common);
    ChainDynamics dyn = makeChainDynamics(common);

    ChainTaps taps;
    allocateTaps(taps, total, common.perSampleDynamicsTaps);
    std::array<float, kGrid> cL{}, cR{}, aL{}, aR{}, fL{}, fR{};

    gridDriver(total, common.hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            float massUsed = 0.0f;
            const MassLaw::Targets t = chainTargets(law, common, start, massUsed);
            for (PolyVoice& pv : voices) {
                pv.core->setControls(coreControlsFor(common.base, t));
                pv.fold->setAmount(t.foldAmount);
                pv.anchor->setControls(anchorControlsFor(t.anchorLevel));
                if (start == common.noteOnSample && pv.sounding) {
                    pv.core->noteOn(static_cast<float>(pv.f0));
                    pv.anchor->noteOn(static_cast<float>(pv.f0), true);
                }
            }
            taps.anchorLevelPerInterval.push_back(t.anchorLevel);
            taps.massPerInterval.push_back(massUsed);
            taps.targetsPerInterval.push_back(t);
            dyn.md->setTransientDurationMs(t.transientDurationMs);
            dyn.md->setCompressionAmount(t.compressionAmount);
            dyn.md->setOutputTrimDb(t.outputTrimDb);   // FR-021 output trim (G8-5)
            dyn.md->setDetectorKeyHz(key);
        }
        if (start >= common.noteOnSample) {
            for (PolyVoice& pv : voices) {
                if (!pv.sounding)
                    continue;
                pv.core->processBlock(cL.data(), cR.data(), len);
                pv.anchor->setCoreH1Amplitude(unbiasedH1Amplitude(common.base, pv.f0, common.fs));   // FR-031 (G8-3)
                if (common.attachAnchor) {
                    pv.anchor->processBlock(aL.data(), aR.data(), len);
                } else {
                    std::fill(aL.begin(), aL.end(), 0.0f);
                    std::fill(aR.begin(), aR.end(), 0.0f);
                }
                std::copy_n(cL.data(), len, fL.data());
                std::copy_n(cR.data(), len, fR.data());
                pv.fold->processBlock(fL.data(), fR.data(), len);
                for (std::size_t i = 0; i < len; ++i) {
                    taps.coreL[start + i] += cL[i];
                    taps.coreR[start + i] += cR[i];
                    taps.anchorL[start + i] += aL[i];
                    taps.anchorR[start + i] += aR[i];
                    taps.foldL[start + i] += fL[i];
                    taps.foldR[start + i] += fR[i];
                }
            }
        }
        processChainTail(taps, common, *dyn.md, dyn.dcL, dyn.dcR, *dyn.limiter, start, len, total);
    });
    return taps;
}

// -----------------------------------------------------------------------------
// s(t): the sub-band level trace (SC-006(a); plan S1 C-10)
// -----------------------------------------------------------------------------

struct SubBand {
    std::vector<double> dftDb;        ///< 20·log10(2|X| / P) of the one-period single-bin DFT at f0
    std::vector<double> lpRmsDb;      ///< RMS dB over one period of the 4th-order Butterworth LP at 1.5·f0
    double lpGroupDelayPeriods = 0.0; ///< measured group delay of that LP at f0, in periods
    std::size_t hop = 1;              ///< entry i = the window starting at sample i·hop
    double period = 0.0;              ///< P = fs / f0, samples
    std::size_t windowSamples = 0;    ///< floor(P) + 1 samples read per window
};

/// max(1, floor(P / 8)) <= P / 8 for P >= 8.
inline std::size_t subBandHop(double fs, double f0) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(fs / f0 / 8.0)));
}

/// Band-limited resampling for the one-period descriptors (SC-005(c); user ruling 2026-10-17,
/// Clarifications "Group 6 gate ruling"): a rectangular DFT over a fractional period leaks the
/// boundary sample into every bin and floors THD near −50 dB on a pure sine, so each window is
/// resampled onto ⌊P⌋ points with a Kaiser-windowed sinc (half-width kResampleHalf, β kResampleBeta;
/// model: sine THD −122 dB, saw within 1e-4 dB of analytic) before an exact periodic DFT.
inline constexpr int kResampleHalf = 32;
inline constexpr double kResampleBeta = 10.0;

/// Modified Bessel function I0 by its power series.
inline double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-18 * sum)
            break;
    }
    return sum;
}

/// The Kaiser-windowed sinc kernel at a distance of d samples (0 outside ±kResampleHalf).
inline double kaiserSinc(double d) {
    static const double norm = besselI0(kResampleBeta);
    const double r = d / static_cast<double>(kResampleHalf);
    if (std::abs(r) >= 1.0)
        return 0.0;
    const double w = besselI0(kResampleBeta * std::sqrt(1.0 - r * r)) / norm;
    const double px = 0.5 * ProfundumTest::kTwoPi * d;
    const double s = std::abs(d) < 1e-12 ? 1.0 : std::sin(px) / px;
    return s * w;
}

/// x at the fractional sample position t. Reads x[⌊t⌋ − kResampleHalf + 1 … ⌊t⌋ + kResampleHalf];
/// the caller guarantees that range lies inside its buffer.
inline double sincInterp(const float* x, double t) {
    const auto m = static_cast<std::ptrdiff_t>(std::floor(t));
    double acc = 0.0;
    for (std::ptrdiff_t k = m - kResampleHalf + 1; k <= m + kResampleHalf; ++k)
        acc += static_cast<double>(x[k]) * kaiserSinc(t - static_cast<double>(k));
    return acc;
}

/// The 4th-order Butterworth low-pass of the C-10 second reading.
inline BiquadCascade<2> subBandLowpass(double fs, double f0) {
    BiquadCascade<2> lp;
    lp.setButterworth(FilterType::Lowpass, static_cast<float>(1.5 * f0), static_cast<float>(fs));
    return lp;
}

/// Group delay of subBandLowpass(fs, f0) at f0, in periods of f0: the Goertzel (BH7) phase
/// difference in -> out of a steady sine, measured at f0·(1 ± 1 %) and differentiated.
inline double lpGroupDelayPeriods(double fs, double f0) {
    const auto phaseShift = [&](double hz) {
        const std::size_t settle = std::max(static_cast<std::size_t>(std::llround(fs)),
                                            static_cast<std::size_t>(std::llround(40.0 * fs / hz)));
        const auto L = static_cast<std::size_t>(std::llround(64.0 * fs / hz));
        std::vector<float> x(settle + L);
        std::vector<float> y(settle + L);
        BiquadCascade<2> lp = subBandLowpass(fs, f0);
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<float>(0.25 * std::sin(ProfundumTest::kTwoPi * hz * static_cast<double>(i) / fs));
            y[i] = lp.process(x[i]);
        }
        const double in = ProfundumTest::goertzelPhase(x.data() + settle, L, fs, hz);
        const double out = ProfundumTest::goertzelPhase(y.data() + settle, L, fs, hz);
        return out - in;
    };
    const double fLo = 0.99 * f0;
    const double fHi = 1.01 * f0;
    double d = phaseShift(fHi) - phaseShift(fLo);
    while (d > ProfundumTest::kTwoPi * 0.5)
        d -= ProfundumTest::kTwoPi;
    while (d < -ProfundumTest::kTwoPi * 0.5)
        d += ProfundumTest::kTwoPi;
    const double tauSeconds = -d / (ProfundumTest::kTwoPi * (fHi - fLo));
    return tauSeconds * f0;
}

/// s(t) of y[0..n) at f0: the sliding one-period single-bin DFT (amplitude 2|X| / P, dB) with hop
/// <= P / 8, and the RMS dB over one period of the 1.5·f0 low-pass output (C-10), at the same windows.
inline SubBand subBand(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(y != nullptr);
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);
    SubBand sb;
    sb.period = fs / f0;
    const auto M = static_cast<std::size_t>(std::floor(sb.period));
    const double frac = sb.period - static_cast<double>(M);
    sb.windowSamples = M + 1;
    REQUIRE(M >= 8u);
    REQUIRE(n >= sb.windowSamples);
    sb.hop = subBandHop(fs, f0);

    const double omega = ProfundumTest::kTwoPi * f0 / fs;
    std::vector<double> c(M + 1);
    std::vector<double> s(M + 1);
    for (std::size_t k = 0; k <= M; ++k) {
        c[k] = std::cos(omega * static_cast<double>(k));
        s[k] = std::sin(omega * static_cast<double>(k));
    }
    std::vector<float> lpOut(n);
    BiquadCascade<2> lp = subBandLowpass(fs, f0);
    for (std::size_t i = 0; i < n; ++i)
        lpOut[i] = lp.process(y[i]);

    const std::size_t count = (n - sb.windowSamples) / sb.hop + 1;
    sb.dftDb.reserve(count);
    sb.lpRmsDb.reserve(count);
    for (std::size_t w = 0; w < count; ++w) {
        const std::size_t st = w * sb.hop;
        double re = 0.0;
        double im = 0.0;
        double sq = 0.0;
        for (std::size_t k = 0; k < M; ++k) {
            const double v = static_cast<double>(y[st + k]);
            re += v * c[k];
            im -= v * s[k];
            const double l = static_cast<double>(lpOut[st + k]);
            sq += l * l;
        }
        const double vM = frac * static_cast<double>(y[st + M]);
        re += vM * c[M];
        im -= vM * s[M];
        const double lM = static_cast<double>(lpOut[st + M]);
        sq += frac * lM * lM;
        const double amp = 2.0 * std::sqrt(re * re + im * im) / sb.period;
        sb.dftDb.push_back(20.0 * std::log10(std::max(amp, 1e-300)));
        sb.lpRmsDb.push_back(ProfundumTest::safeDb10(sq / sb.period));
    }
    sb.lpGroupDelayPeriods = lpGroupDelayPeriods(fs, f0);
    return sb;
}

/// M_sub = max − min of trace[begin, end) (trace indices).
inline double mSub(const std::vector<double>& trace, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= trace.size());
    const auto [lo, hi] = std::minmax_element(trace.begin() + static_cast<std::ptrdiff_t>(begin),
                                              trace.begin() + static_cast<std::ptrdiff_t>(end));
    return *hi - *lo;
}

/// M_sub over the windows lying wholly inside the samples [beginSample, endSample).
inline double mSubSamples(const SubBand& sb, const std::vector<double>& trace, std::size_t beginSample,
                          std::size_t endSample) {
    REQUIRE(endSample >= beginSample + sb.windowSamples);
    const std::size_t first = (beginSample + sb.hop - 1) / sb.hop;
    const std::size_t last = std::min(trace.size(), (endSample - sb.windowSamples) / sb.hop + 1);
    return mSub(trace, first, last);
}

// -----------------------------------------------------------------------------
// One-period descriptors (SC-005(c))
// -----------------------------------------------------------------------------

/// Descriptors of one fractional period resampled onto N = ⌊P⌋ points (sincInterp), harmonic
/// powers w_n = (2|X_n| / N)² / 2 from the periodic DFT at every n·f0 < fs/2 (n < N/2). Power
/// ratios in dB, as ShapeCharacterTest::Descriptors.
struct PeriodDescriptors {
    double L;       ///< RMS dBFS over the resampled period, Σ r² / N
    double Elow;    ///< Σ_{n=2..8} w / Σ w
    double Ehigh;   ///< Σ_{n≥9} w / Σ w
    double Thd;     ///< Σ_{n≥2} w / w_1
    double C;       ///< Σ w·log2(n) / Σ w over 20 Hz <= n·f0 <= min(fs/2, 20 kHz), octaves above f0
    double w1Frac;  ///< w_1 / Σ w (linear fraction)
};

/// y must hold y[start − kResampleHalf … start + ⌈fs / f0⌉ + kResampleHalf] (start ≥ kResampleHalf):
/// the window's resampling taps reach that far either side (Group 6 ruling 2026-10-17).
inline PeriodDescriptors periodDescriptors(const float* y, std::size_t start, double fs, double f0) {
    REQUIRE(y != nullptr);
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);
    REQUIRE(start >= static_cast<std::size_t>(kResampleHalf));
    const double P = fs / f0;
    const auto N = static_cast<std::size_t>(std::floor(P));
    REQUIRE(N >= 4u);
    const double dN = static_cast<double>(N);

    // One period, t_k = start + k·P/N, onto an integer grid of N points.
    std::vector<double> r(N);
    for (std::size_t k = 0; k < N; ++k)
        r[k] = sincInterp(y, static_cast<double>(start) + static_cast<double>(k) * P / dN);

    double sq = 0.0;
    for (const double v : r)
        sq += v * v;

    double total = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    double w1 = 0.0;
    double cNum = 0.0;
    double cDen = 0.0;
    const double fMax = std::min(fs * 0.5, 20000.0);
    for (int h = 1; static_cast<double>(h) * f0 < fs * 0.5 && 2 * static_cast<std::size_t>(h) < N; ++h) {
        const double hz = static_cast<double>(h) * f0;
        double re = 0.0;
        double im = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            const double ph = ProfundumTest::kTwoPi * static_cast<double>(h) * static_cast<double>(k) / dN;
            re += r[k] * std::cos(ph);
            im -= r[k] * std::sin(ph);
        }
        const double amp = 2.0 * std::hypot(re, im) / dN;
        const double w = 0.5 * amp * amp;
        total += w;
        if (h == 1)
            w1 = w;
        else if (h <= 8)
            lo += w;
        else
            hi += w;
        if (hz >= 20.0 && hz <= fMax) {
            cNum += w * std::log2(static_cast<double>(h));
            cDen += w;
        }
    }
    REQUIRE(total > 0.0);
    PeriodDescriptors d{};
    d.L = ProfundumTest::safeDb10(sq / dN);
    d.Elow = ProfundumTest::safeDb10(lo / total);
    d.Ehigh = ProfundumTest::safeDb10(hi / total);
    d.Thd = ProfundumTest::safeDb10((lo + hi) / std::max(w1, 1e-300));
    d.C = cDen > 0.0 ? cNum / cDen : 0.0;
    d.w1Frac = w1 / total;
    return d;
}

// -----------------------------------------------------------------------------
// ΔL_step (SC-001(b) compression row, SC-011)
// -----------------------------------------------------------------------------

/// level(high) − level(low), each the RMS dB over the whole f0 periods that fit in its window.
inline double deltaLStepDb(std::span<const float> y, double fs, double f0, std::size_t lowBegin, std::size_t lowEnd,
                           std::size_t highBegin, std::size_t highEnd) {
    REQUIRE(lowBegin < lowEnd);
    REQUIRE(lowEnd <= y.size());
    REQUIRE(highBegin < highEnd);
    REQUIRE(highEnd <= y.size());
    const double low = TransientShaperTest::levelDbPeriods(y.data() + lowBegin, lowEnd - lowBegin, fs, f0);
    const double high = TransientShaperTest::levelDbPeriods(y.data() + highBegin, highEnd - highBegin, fs, f0);
    return high - low;
}

// -----------------------------------------------------------------------------
// The C-3 residual aliasing metric (SC-010; plan S1 C-3)
// -----------------------------------------------------------------------------

inline constexpr double kAliasPreMs = 50.0;     ///< taper before t0
inline constexpr double kAliasMeasureMs = 100.0;
inline constexpr double kAliasPostMs = 50.0;    ///< taper after t0 + 100 ms

/// Samples of the [t0 − 50 ms, t0 + 150 ms] segment.
inline std::size_t aliasResidualSegment(double fs) {
    return static_cast<std::size_t>(std::llround((kAliasPreMs + kAliasMeasureMs + kAliasPostMs) * 0.001 * fs));
}

/// The upsampled-domain FFT length: the next power of two >= 8 × the segment. Bin Hz = 8·fs / length.
inline std::size_t aliasResidualFftLength(double fs) {
    const std::size_t need = 8 * aliasResidualSegment(fs);
    std::size_t len = 8;
    while (len < need)
        len <<= 1;
    return len;
}

/// Inverse FFT by conjugation, normalised by 1 / size.
inline void inverseFftDouble(std::vector<std::complex<double>>& a) {
    for (auto& v : a)
        v = std::conj(v);
    ProfundumTest::fftDouble(a);
    const double scale = 1.0 / static_cast<double>(a.size());
    for (auto& v : a)
        v = std::conj(v) * scale;
}

/// 8× band-limited upsample by FFT zero-padding of seg (size a power of two); the original samples
/// reappear at every 8th output.
inline std::vector<double> upsample8(const std::vector<double>& seg) {
    const std::size_t N = seg.size();
    std::vector<std::complex<double>> A(N);
    for (std::size_t k = 0; k < N; ++k)
        A[k] = std::complex<double>(seg[k], 0.0);
    ProfundumTest::fftDouble(A);
    const std::size_t N8 = 8 * N;
    std::vector<std::complex<double>> B(N8, std::complex<double>(0.0, 0.0));
    for (std::size_t k = 0; k < N / 2; ++k)
        B[k] = A[k];
    for (std::size_t k = 1; k < N / 2; ++k)
        B[N8 - k] = A[N - k];
    B[N / 2] = 0.5 * A[N / 2];
    B[N8 - N / 2] = 0.5 * A[N / 2];
    inverseFftDouble(B);
    std::vector<double> out(N8);
    for (std::size_t m = 0; m < N8; ++m)
        out[m] = 8.0 * B[m].real();
    return out;
}

/// Plan C-3: in double, an 8× FFT zero-pad upsample of x and of the linear gain 10^(gainDb / 20) over
/// [t0 − 50 ms, t0 + 150 ms] with 50 ms Hann tapers; their product; a brick-wall cut at fs / 2 (bins
/// |f| >= fs / 2 zeroed); decimation by 8 = the ideal product. r = y − ideal; returns
/// 10·log10(mean(r²) / 0.5) over [t0, t0 + 100 ms] (the aliasedPower "re 0.5" convention).
inline double aliasResidualDb(const float* x, const float* gainDb, const float* y, std::size_t n, double fs,
                              std::size_t t0) {
    REQUIRE(x != nullptr);
    REQUIRE(gainDb != nullptr);
    REQUIRE(y != nullptr);
    const auto pre = static_cast<std::size_t>(std::llround(kAliasPreMs * 0.001 * fs));
    const auto meas = static_cast<std::size_t>(std::llround(kAliasMeasureMs * 0.001 * fs));
    const std::size_t S = aliasResidualSegment(fs);
    const std::size_t post = S - pre - meas;
    REQUIRE(pre > 0u);
    REQUIRE(post > 0u);
    REQUIRE(t0 >= pre);
    REQUIRE(t0 + meas + post <= n);
    const std::size_t seg0 = t0 - pre;
    const std::size_t N8 = aliasResidualFftLength(fs);
    const std::size_t N = N8 / 8;
    REQUIRE(N >= S);

    // Group 7 refinement of C-3 (2026-10-17, main loop; spec SC-010 row): a gain applied to an exact-zero
    // sample is unobservable, so over leading zero input the trace is held at its value on the first
    // non-zero sample. Band-limiting the shaper's silence-branch step on its own (plan S4.3 step 5) rang
    // in the "ideal" product and read -67 dBFS on an output that is exactly g * x.
    std::size_t firstNonZero = 0;
    while (firstNonZero < S && x[seg0 + firstNonZero] == 0.0f)
        ++firstNonZero;
    const float holdDb = gainDb[seg0 + (firstNonZero < S ? firstNonZero : 0)];

    std::vector<double> xs(N, 0.0);
    std::vector<double> gs(N, 0.0);
    for (std::size_t k = 0; k < S; ++k) {
        double w = 1.0;
        if (k < pre)
            w = 0.5 * (1.0 - std::cos(ProfundumTest::kTwoPi * 0.5 * static_cast<double>(k) / static_cast<double>(pre)));
        else if (k >= pre + meas)
            w = 0.5 * (1.0 - std::cos(ProfundumTest::kTwoPi * 0.5 * static_cast<double>(S - k) /
                                      static_cast<double>(post)));
        xs[k] = static_cast<double>(x[seg0 + k]) * w;
        const float gDb = k < firstNonZero ? holdDb : gainDb[seg0 + k];
        gs[k] = std::pow(10.0, static_cast<double>(gDb) / 20.0) * w;
    }
    const std::vector<double> xu = upsample8(xs);
    const std::vector<double> gu = upsample8(gs);
    std::vector<std::complex<double>> p(N8);
    for (std::size_t m = 0; m < N8; ++m)
        p[m] = std::complex<double>(xu[m] * gu[m], 0.0);
    ProfundumTest::fftDouble(p);
    for (std::size_t k = N / 2; k <= N8 - N / 2; ++k)
        p[k] = std::complex<double>(0.0, 0.0);
    inverseFftDouble(p);

    double sum = 0.0;
    for (std::size_t k = pre; k < pre + meas; ++k) {
        const double ideal = p[8 * k].real();
        const double r = static_cast<double>(y[seg0 + k]) - ideal;
        sum += r * r;
    }
    return ProfundumTest::safeDb10(sum / static_cast<double>(meas) / 0.5);
}

}  // namespace Krate::DSP::MassDynamicsTest
