// ==============================================================================
// Layer 3: System Tests - BassMorph click / transition
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T010 (SC-004(a)(b)(d) render; FR-034, FR-055; plan C-10, R-12, R-14; deviations 5, 10).
//
// Every discrete field a Phase 8 component renders, switched alone through the FR-070 fixture
// (BassMorphTest::renderMorphChain), steady render, Blend (p_a = p, so the Anchor cases really switch, FR-055),
// E1 and C3 at 48 kHz, ramp {5, 40, 2000} ms, the 0 -> 1 position event at 0.25 s.
//
//   plan S1 C-10: base state = MorphState{} with characterAmount 0.8 and anchor.level 1.0 on both sides (rule 3
//   decides at theta_up); the direct-setter reference = the same fixture with p held at 0 and both endpoints
//   replaced by A' (A with the switched field at B's value) at the transition's switch grid sample g_sw.
//   plan R-12: A-static / B-static renders are shared across the ramp times (cached by (state, note), rendered
//   at the longest ramp's length; the fixture is causal, so a shorter render is a bitwise prefix of a longer one).
//   plan R-14: the 5 ms ramp is in every grid (the character stage queues its last request).
//   deviation 10: transition end via BassMorphTest::transitionEndSamples (field.mode and field.motion* = one
//   period + 334 intervals); window W = [g_sw, g_sw + transition end + one period].
//
//   (a) max |y[n] - y[n-1]| over W (ShapeCharacterTest::maxDeltaAround, centre and half-width covering W) on
//       outL and outR <= 10^(0.5/20) x the direct-setter reference's maximum over W (plan S1 C-15, D-9).
//   (b) ShapeCharacterTest::differentialClicks(transition, A-static, B-static, ...).counted == 0 on outL and outR
//       over [g_sw - 50 ms, end of W].
//
// Shards ([sc-004][long], one per field family; R-12 runs them concurrently) + BassMorph_DiscreteClickFreeSmoke
// ([sc-004], per-push: Tube -> Fold, E1, default ramp; deviation 5). Each shard writes evidence/sc004-<family>.csv.
// FTZ / DAZ is set per case (rule 9; the components' precondition).
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::AnchorOscillator;
using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::BassStereoField;
using Krate::DSP::MorphState;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;

constexpr double kBmcFs = 48000.0;
constexpr int kBmcMidiE1 = 28;
constexpr int kBmcMidiC3 = 48;
constexpr std::array<int, 2> kBmcNotes{kBmcMidiE1, kBmcMidiC3};
constexpr std::array<float, 3> kBmcRampsMs{BassMorphEngine::kMinPositionRampMs, BassMorphEngine::kDefaultPositionRampMs,
                                           BassMorphEngine::kMaxPositionRampMs};
constexpr double kBmcEventSeconds = 0.25;   // the 0 -> 1 position event
constexpr double kBmcPreSeconds = 0.05;     // (b) starts 50 ms before g_sw; the render ends 50 ms after W

/// SC-004(a): 10^(0.5/20) (plan S1 C-15, D-9).
double bmcStepBar() { return std::pow(10.0, 0.5 / 20.0); }

double bmcF0(int midi) { return static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(midi)); }

const char* bmcNoteName(int midi) {
    if (midi == kBmcMidiE1)
        return "E1";
    if (midi == kBmcMidiC3)
        return "C3";
    return "note";
}

std::size_t bmcSamples(double seconds) { return static_cast<std::size_t>(std::llround(seconds * kBmcFs)); }

std::size_t bmcPeriod(double f0) { return static_cast<std::size_t>(std::ceil(kBmcFs / f0)); }

std::size_t bmcRoundUpToGrid(std::size_t n) { return ((n + Bmt::kGrid - 1) / Bmt::kGrid) * Bmt::kGrid; }

/// The 0 -> 1 event sample (a grid sample: 12 000 = 375 * 32 at 48 kHz).
std::size_t bmcEventSample() {
    const std::size_t s = bmcSamples(kBmcEventSeconds);
    REQUIRE(s % Bmt::kGrid == 0);
    return s;
}

// -----------------------------------------------------------------------------
// States (plan S1 C-10)
// -----------------------------------------------------------------------------

/// The SC-004 base state: MorphState{} with characterAmount 0.8 and anchor.level 1.0 (plan S1 C-10).
MorphState bmcBase() {
    MorphState s{};
    s.characterAmount = 0.8f;
    s.anchor.level = 1.0f;
    return s;
}

/// The motion-source cases: motionDepth 1 so the selector is audible.
MorphState bmcMotionBase() {
    MorphState s = bmcBase();
    s.field.motionDepth = 1.0f;
    return s;
}

/// The cycle cases: motionSource Cycle, cycleSync on, motionDepth 1.
MorphState bmcCycleBase() {
    MorphState s = bmcBase();
    s.field.motionSource = BassStereoField::MotionSource::Cycle;
    s.field.cycleSync = true;
    s.field.motionDepth = 1.0f;
    return s;
}

/// A' = s with the discrete field `leaf` copied from `from` (plan S1 C-10).
MorphState bmcWithDiscrete(MorphState s, std::uint8_t leaf, const MorphState& from) {
    if (leaf == Bmt::kLeafCharacterAlgorithm)
        s.characterAlgorithm = from.characterAlgorithm;
    else if (leaf == Bmt::kLeafFieldMode)
        s.field.mode = from.field.mode;
    else if (leaf == Bmt::kLeafFieldMotionSource)
        s.field.motionSource = from.field.motionSource;
    else if (leaf == Bmt::kLeafFieldCycleSync)
        s.field.cycleSync = from.field.cycleSync;
    else if (leaf == Bmt::kLeafFieldCycleNote)
        s.field.cycleNote = from.field.cycleNote;
    else if (leaf == Bmt::kLeafFieldCycleModifier)
        s.field.cycleModifier = from.field.cycleModifier;
    else if (leaf == Bmt::kLeafAnchorWaveform)
        s.anchor.waveform = from.anchor.waveform;
    else if (leaf == Bmt::kLeafAnchorMode)
        s.anchor.mode = from.anchor.mode;
    else
        FAIL("bmcWithDiscrete: leaf " << static_cast<int>(leaf) << " is not a recorded discrete leaf");
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

// -----------------------------------------------------------------------------
// Families
// -----------------------------------------------------------------------------

struct BmcPair {
    std::string label;
    MorphState a{}, b{};
};

struct BmcFamily {
    std::string name;   ///< evidence/sc004-<name>.csv
    std::uint8_t leaf = Bmt::kNoLeaf;
    Bmt::TransitionKind kind = Bmt::TransitionKind::CharacterAlgorithm;
    bool anchorGroup = false;   ///< the switch is decided at p_a (Anchor block, FR-050)
    std::vector<BmcPair> pairs;
};

/// Every ordered pair (i, j), i != j, of `values`, each side = base with set(value).
template <class T, class Set>
std::vector<BmcPair> bmcOrderedPairs(const MorphState& base, const std::vector<std::pair<T, const char*>>& values,
                                     Set set) {
    std::vector<BmcPair> out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            if (i == j)
                continue;
            BmcPair p;
            p.label = std::string(values[i].second) + "->" + values[j].second;
            p.a = base;
            p.b = base;
            set(p.a, values[i].first);
            set(p.b, values[j].first);
            REQUIRE(Krate::DSP::isValidMorphState(p.a));
            REQUIRE(Krate::DSP::isValidMorphState(p.b));
            out.push_back(p);
        }
    }
    return out;
}

std::vector<std::pair<BassCharacterStage::Algorithm, const char*>> bmcAlgorithms() {
    using A = BassCharacterStage::Algorithm;
    return {{A::Tube, "Tube"}, {A::Fold, "Fold"},   {A::Rectify, "Rectify"},
            {A::Bite, "Bite"}, {A::Crush, "Crush"}, {A::Fracture, "Fracture"}};
}

BmcFamily bmcAlgorithmFamily() {
    BmcFamily f;
    f.name = "algorithm";
    f.leaf = Bmt::kLeafCharacterAlgorithm;
    f.kind = Bmt::TransitionKind::CharacterAlgorithm;
    f.pairs = bmcOrderedPairs(bmcBase(), bmcAlgorithms(),
                              [](MorphState& s, BassCharacterStage::Algorithm v) { s.characterAlgorithm = v; });
    REQUIRE(f.pairs.size() == 30);
    return f;
}

BmcFamily bmcImageModeFamily() {
    using M = BassStereoField::ImageMode;
    BmcFamily f;
    f.name = "image-mode";
    f.leaf = Bmt::kLeafFieldMode;
    f.kind = Bmt::TransitionKind::FieldMode;
    f.pairs = bmcOrderedPairs(bmcBase(),
                              std::vector<std::pair<M, const char*>>{{M::Solid, "Solid"}, {M::Spread, "Spread"},
                                                                     {M::Wide, "Wide"}},
                              [](MorphState& s, M v) { s.field.mode = v; });
    REQUIRE(f.pairs.size() == 6);
    return f;
}

BmcFamily bmcAnchorWaveformFamily() {
    using W = AnchorOscillator::Waveform;
    BmcFamily f;
    f.name = "anchor-waveform";
    f.leaf = Bmt::kLeafAnchorWaveform;
    f.kind = Bmt::TransitionKind::AnchorWaveform;
    f.anchorGroup = true;
    f.pairs = bmcOrderedPairs(bmcBase(),
                              std::vector<std::pair<W, const char*>>{{W::Sine, "Sine"},
                                                                     {W::Triangle, "Triangle"},
                                                                     {W::RoundedSine, "RoundedSine"},
                                                                     {W::AsymmetricSine, "AsymmetricSine"},
                                                                     {W::OctaveDownSine, "OctaveDownSine"}},
                              [](MorphState& s, W v) { s.anchor.waveform = v; });
    REQUIRE(f.pairs.size() == 20);
    return f;
}

BmcFamily bmcAnchorModeFamily() {
    using M = AnchorOscillator::Mode;
    BmcFamily f;
    f.name = "anchor-mode";
    f.leaf = Bmt::kLeafAnchorMode;
    f.kind = Bmt::TransitionKind::AnchorMode;
    f.anchorGroup = true;
    f.pairs = bmcOrderedPairs(bmcBase(),
                              std::vector<std::pair<M, const char*>>{{M::Lock, "Lock"}, {M::Follow, "Follow"},
                                                                     {M::Punch, "Punch"}, {M::Ghost, "Ghost"}},
                              [](MorphState& s, M v) { s.anchor.mode = v; });
    REQUIRE(f.pairs.size() == 12);
    return f;
}

BmcFamily bmcFieldMotionSourceFamily() {
    using S = BassStereoField::MotionSource;
    BmcFamily f;
    f.name = "field-motion-source";
    f.leaf = Bmt::kLeafFieldMotionSource;
    f.kind = Bmt::TransitionKind::FieldMotion;
    f.pairs = bmcOrderedPairs(bmcMotionBase(),
                              std::vector<std::pair<S, const char*>>{{S::Drift, "Drift"}, {S::Cycle, "Cycle"}},
                              [](MorphState& s, S v) { s.field.motionSource = v; });
    REQUIRE(f.pairs.size() == 2);
    return f;
}

BmcFamily bmcFieldCycleSyncFamily() {
    BmcFamily f;
    f.name = "field-cycle-sync";
    f.leaf = Bmt::kLeafFieldCycleSync;
    f.kind = Bmt::TransitionKind::FieldMotion;
    f.pairs = bmcOrderedPairs(bmcCycleBase(),
                              std::vector<std::pair<bool, const char*>>{{false, "SyncOff"}, {true, "SyncOn"}},
                              [](MorphState& s, bool v) { s.field.cycleSync = v; });
    REQUIRE(f.pairs.size() == 2);
    return f;
}

BmcFamily bmcFieldCycleModifierFamily() {
    BmcFamily f;
    f.name = "field-cycle-modifier";
    f.leaf = Bmt::kLeafFieldCycleModifier;
    f.kind = Bmt::TransitionKind::FieldMotion;
    MorphState base = bmcCycleBase();
    base.field.cycleNote = NoteValue::Quarter;
    f.pairs = bmcOrderedPairs(base,
                              std::vector<std::pair<NoteModifier, const char*>>{{NoteModifier::None, "None"},
                                                                                {NoteModifier::Dotted, "Dotted"},
                                                                                {NoteModifier::Triplet, "Triplet"}},
                              [](MorphState& s, NoteModifier v) { s.field.cycleModifier = v; });
    REQUIRE(f.pairs.size() == 6);
    return f;
}

BmcFamily bmcFieldCycleNoteFamily() {
    BmcFamily f;
    f.name = "field-cycle-note";
    f.leaf = Bmt::kLeafFieldCycleNote;
    f.kind = Bmt::TransitionKind::FieldMotion;
    MorphState base = bmcCycleBase();
    base.field.cycleModifier = NoteModifier::None;
    f.pairs = bmcOrderedPairs(base,
                              std::vector<std::pair<NoteValue, const char*>>{
                                  {NoteValue::SixtyFourth, "SixtyFourth"},
                                  {NoteValue::Quarter, "Quarter"},
                                  {NoteValue::QuadrupleWhole, "QuadrupleWhole"}},
                              [](MorphState& s, NoteValue v) { s.field.cycleNote = v; });
    REQUIRE(f.pairs.size() == 6);
    return f;
}

// -----------------------------------------------------------------------------
// Renders
// -----------------------------------------------------------------------------

struct BmcStereo {
    std::vector<float> L, R;
};

/// The common config: steady render (no ADSR), Blend, note-on at 0, 48 kHz, exactly `total` samples.
Bmt::MorphChainConfig bmcConfig(const MorphState& a, const MorphState& b, double f0, std::size_t total,
                                float rampMs) {
    Bmt::MorphChainConfig cfg;
    cfg.a = a;
    cfg.b = b;
    cfg.mode = BassMorphEngine::AnchorMorphMode::Blend;   // p_a = p (FR-055)
    cfg.initialPosition = 0.0f;
    cfg.rampMs = rampMs;
    cfg.f0 = f0;
    cfg.fs = kBmcFs;
    cfg.seconds = static_cast<double>(total) / kBmcFs;
    cfg.noteOnSample = 0;
    cfg.ampEnvelope = false;
    return cfg;
}

Bmt::MorphChainTaps bmcRender(const Bmt::MorphChainConfig& cfg, std::size_t total) {
    Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);
    REQUIRE(t.outL.size() == total);
    REQUIRE(t.outR.size() == total);
    return t;
}

/// plan R-12: A-static / B-static renders shared across ramp times, cached by (state, note).
class BmcStaticCache {
public:
    const BmcStereo& get(const MorphState& s, int midi, std::size_t total) {
        const Key key{Bmt::serializedBytes(s), midi};
        auto it = map_.find(key);
        if (it != map_.end() && it->second.L.size() >= total)
            return it->second;
        const Bmt::MorphChainConfig cfg = bmcConfig(s, s, bmcF0(midi), total, BassMorphEngine::kDefaultPositionRampMs);
        Bmt::MorphChainTaps t = bmcRender(cfg, total);
        BmcStereo st{std::move(t.outL), std::move(t.outR)};
        map_[key] = std::move(st);
        return map_[key];
    }

private:
    using Key = std::pair<std::array<std::byte, Krate::DSP::kMorphStateBytes>, int>;
    std::map<Key, BmcStereo> map_;
};

/// One automation event (plain member writes: every MorphEvent member is set).
Bmt::MorphEvent bmcEvent(std::size_t sample, Bmt::MorphEvent::Kind kind, float value, const MorphState& state = MorphState{}) {
    Bmt::MorphEvent e;
    e.sample = sample;
    e.kind = kind;
    e.value = value;
    e.mode = BassMorphEngine::kDefaultAnchorMorphMode;
    e.state = state;
    return e;
}

/// The setter calls of `leaf` the fixture issued.
std::vector<Bmt::MorphChainTaps::SetterCall> bmcCallsOf(const Bmt::MorphChainTaps& t, std::uint8_t leaf) {
    std::vector<Bmt::MorphChainTaps::SetterCall> out;
    for (const auto& c : t.discreteSetterCalls) {
        if (c.leaf == leaf)
            out.push_back(c);
    }
    return out;
}

/// The position the group's selection is decided at (p, or p_a for the Anchor block).
float bmcQ(const BassMorphEngine::Resolved& r, bool anchorGroup) {
    return anchorGroup ? r.anchorPosition : r.position;
}

// -----------------------------------------------------------------------------
// Measurement (SC-004 (a)(b))
// -----------------------------------------------------------------------------

/// max |x[k] - x[k-1]| over [s, s + len] via maxDeltaAround (centre and half-width covering the window).
double bmcMaxDeltaOver(const std::vector<float>& x, std::size_t s, std::size_t len) {
    const std::size_t half = (len + 1) / 2;
    REQUIRE(s + 2 * half < x.size());
    return Krate::DSP::ShapeCharacterTest::maxDeltaAround(x.data(), x.size(), s + half, half);
}

struct BmcCell {
    std::size_t gSw = 0;
    double ratioL = 0.0, ratioR = 0.0;
    std::size_t clicksL = 0, clicksR = 0;
    std::size_t refClicksL = 0, refClicksR = 0;   ///< the direct-setter reference's own differential clicks (recorded)
};

/// (a) and (b) for one switch at g_sw with window length `windowLen` (= transition end + one period).
BmcCell bmcMeasure(const BmcStereo& tr, const BmcStereo& ref, const BmcStereo& aStatic, const BmcStereo& bStatic,
                   std::size_t gSw, std::size_t windowLen, double f0) {
    const std::size_t n = tr.L.size();
    REQUIRE(ref.L.size() == n);
    REQUIRE(aStatic.L.size() >= n);
    REQUIRE(bStatic.L.size() >= n);
    const std::size_t pre = bmcSamples(kBmcPreSeconds);
    REQUIRE(gSw >= pre);
    const std::size_t wEnd = gSw + windowLen;
    REQUIRE(wEnd + 1 < n);

    BmcCell c;
    c.gSw = gSw;
    const double refL = bmcMaxDeltaOver(ref.L, gSw, windowLen);
    const double refR = bmcMaxDeltaOver(ref.R, gSw, windowLen);
    REQUIRE(refL > 0.0);
    REQUIRE(refR > 0.0);
    c.ratioL = bmcMaxDeltaOver(tr.L, gSw, windowLen) / refL;
    c.ratioR = bmcMaxDeltaOver(tr.R, gSw, windowLen) / refR;

    const std::size_t cs = gSw - pre;
    const std::size_t clen = wEnd - cs + 1;
    c.clicksL = Krate::DSP::ShapeCharacterTest::differentialClicks(tr.L.data() + cs, aStatic.L.data() + cs,
                                                                   bStatic.L.data() + cs, clen, kBmcFs, f0)
                    .counted;
    c.clicksR = Krate::DSP::ShapeCharacterTest::differentialClicks(tr.R.data() + cs, aStatic.R.data() + cs,
                                                                   bStatic.R.data() + cs, clen, kBmcFs, f0)
                    .counted;
    // Recorded (main loop, 2026-10-19): the same count on the reference, so a click the component's own switch
    // already has is attributable (D-9: the morph adds no click of its own).
    c.refClicksL = Krate::DSP::ShapeCharacterTest::differentialClicks(ref.L.data() + cs, aStatic.L.data() + cs,
                                                                      bStatic.L.data() + cs, clen, kBmcFs, f0)
                       .counted;
    c.refClicksR = Krate::DSP::ShapeCharacterTest::differentialClicks(ref.R.data() + cs, aStatic.R.data() + cs,
                                                                      bStatic.R.data() + cs, clen, kBmcFs, f0)
                       .counted;
    return c;
}

// -----------------------------------------------------------------------------
// Evidence (tasks.md rule 11)
// -----------------------------------------------------------------------------

std::string bmcEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

std::unique_ptr<std::ofstream> bmcOpenCsv(const std::string& family) {
    const std::string dir = bmcEvidenceDir();
    std::filesystem::create_directories(dir);
    std::string path = dir;
    path += "sc004-";
    path += family;
    path += ".csv";
    auto out = std::make_unique<std::ofstream>(path, std::ios::trunc);
    REQUIRE(out->good());
    *out << "family,pair,note,ramp_ms,g_sw,ratio_L,ratio_R,clicks_L,clicks_R,ref_clicks_L,ref_clicks_R\n";
    return out;
}

void bmcRecord(std::ofstream* csv, const std::string& family, const std::string& pair, int midi, float rampMs,
               const BmcCell& c) {
    std::ostringstream row;
    row << std::setprecision(6) << family << ',' << pair << ',' << bmcNoteName(midi) << ',' << rampMs << ','
        << c.gSw << ',' << c.ratioL << ',' << c.ratioR << ',' << c.clicksL << ',' << c.clicksR << ','
        << c.refClicksL << ',' << c.refClicksR;
    std::cout << "SC004 " << row.str() << '\n';
    if (csv != nullptr) {
        *csv << row.str() << '\n';
        csv->flush();
    }
}

void bmcCheckCell(const BmcCell& c, const std::string& family, const std::string& pair, int midi, float rampMs) {
    INFO("family " << family << ", pair " << pair << ", note " << bmcNoteName(midi) << ", ramp " << rampMs
                   << " ms, g_sw " << c.gSw << ", ratio L " << c.ratioL << ", ratio R " << c.ratioR << " (bar "
                   << bmcStepBar() << "), clicks L " << c.clicksL << ", clicks R " << c.clicksR << " (reference's own "
                   << c.refClicksL << " / " << c.refClicksR << ")");
    // B10-2 (2026-10-19): the ImageMode governing-amount rows record (a) (the G7-2 engagement ramp against a
    // no-step reference), everything else gates it.
    const bool gateRatio = family != "governing-amount" || !pair.starts_with("ImageMode");
    if (gateRatio) {
        CHECK(c.ratioL <= bmcStepBar());   // SC-004(a)
        CHECK(c.ratioR <= bmcStepBar());
    } else {
        std::cout << "SC004 recorded(a) " << family << ',' << pair << ',' << bmcNoteName(midi) << ',' << rampMs
                  << ",ratio_L," << c.ratioL << ",ratio_R," << c.ratioR << " (B10-2: engagement ramp, not gated)\n";
    }
    // SC-004(b), B10-1 (2026-10-19): the morph adds no click of its own -- no more clicks than the direct-setter
    // reference at the same grid point; the reference's own clicks are the component's and are recorded.
    CHECK(c.clicksL <= c.refClicksL);
    CHECK(c.clicksR <= c.refClicksR);
}

// -----------------------------------------------------------------------------
// SC-004(a)(b): one family (0 -> 1, one switch per render)
// -----------------------------------------------------------------------------

/// Render length: 0.25 s + rampMs + the transition end + one period + 50 ms.
std::size_t bmcTotal(float rampMs, std::size_t transitionEnd, std::size_t period) {
    return bmcEventSample() + bmcSamples(static_cast<double>(rampMs) / 1000.0) + transitionEnd + period
           + bmcSamples(kBmcPreSeconds);
}

void bmcRunFamily(const BmcFamily& fam, std::span<const int> notes, std::span<const float> ramps,
                  std::ofstream* csv) {
    REQUIRE(fam.leaf != Bmt::kNoLeaf);
    REQUIRE(!ramps.empty());
    BmcStaticCache statics;
    const std::size_t eventSample = bmcEventSample();
    for (const int midi : notes) {
        const double f0 = bmcF0(midi);
        const std::size_t period = bmcPeriod(f0);
        const std::size_t transitionEnd = Bmt::transitionEndSamples(fam.kind, f0, kBmcFs);
        const std::size_t windowLen = transitionEnd + period;   // W = [g_sw, g_sw + transition end + one period]
        const float longestRamp = *std::max_element(ramps.begin(), ramps.end());
        const std::size_t staticTotal = bmcTotal(longestRamp, transitionEnd, period);
        for (const BmcPair& pair : fam.pairs) {
            const BmcStereo& aStatic = statics.get(pair.a, midi, staticTotal);
            const BmcStereo& bStatic = statics.get(pair.b, midi, staticTotal);
            for (const float rampMs : ramps) {
                INFO("family " << fam.name << ", pair " << pair.label << ", note " << bmcNoteName(midi) << ", ramp "
                               << rampMs << " ms");
                const std::size_t total = bmcTotal(rampMs, transitionEnd, period);

                // The transition: A -> B, the 0 -> 1 position event at 0.25 s.
                Bmt::MorphChainConfig tc = bmcConfig(pair.a, pair.b, f0, total, rampMs);
                tc.automation.push_back(bmcEvent(eventSample, Bmt::MorphEvent::Kind::Position, 1.0f));
                Bmt::MorphChainTaps tt = bmcRender(tc, total);
                const auto calls = bmcCallsOf(tt, fam.leaf);
                REQUIRE(calls.size() == 1);   // one switch per traversal (rule 3, no chatter on a monotone ramp)
                const std::size_t gSw = calls[0].sample;
                REQUIRE(gSw % Bmt::kGrid == 0);
                const std::size_t idx = gSw / Bmt::kGrid;
                REQUIRE(idx > 0);
                REQUIRE(idx < tt.resolvedPerInterval.size());
                REQUIRE(tt.advanceSamples[idx] == gSw);
                // Rule 3 (both governing amounts non-zero, plan S1 C-10): the switch is at the first q >= theta_up.
                CHECK(bmcQ(tt.resolvedPerInterval[idx], fam.anchorGroup) >= BassMorphEngine::kThetaUp);
                CHECK(bmcQ(tt.resolvedPerInterval[idx - 1], fam.anchorGroup) < BassMorphEngine::kThetaUp);

                // The direct-setter reference (plan S1 C-10): p held at 0, both endpoints = A' at g_sw.
                const MorphState aPrime = bmcWithDiscrete(pair.a, fam.leaf, pair.b);
                Bmt::MorphChainConfig rc = bmcConfig(pair.a, pair.a, f0, total, rampMs);
                rc.automation.push_back(bmcEvent(gSw, Bmt::MorphEvent::Kind::EndpointA, 0.0f, aPrime));
                rc.automation.push_back(bmcEvent(gSw, Bmt::MorphEvent::Kind::EndpointB, 0.0f, aPrime));
                Bmt::MorphChainTaps rt = bmcRender(rc, total);
                const auto refCalls = bmcCallsOf(rt, fam.leaf);
                REQUIRE(refCalls.size() == 1);   // the same setter path at the same grid point
                REQUIRE(refCalls[0].sample == gSw);
                REQUIRE(refCalls[0].value == calls[0].value);

                const BmcStereo tr{std::move(tt.outL), std::move(tt.outR)};
                const BmcStereo ref{std::move(rt.outL), std::move(rt.outR)};
                // The statics are rendered at the longest ramp's length; a shorter render is a bitwise prefix
                // (the fixture is causal), so bmcMeasure reads their first `total` samples.
                const BmcCell cell = bmcMeasure(tr, ref, aStatic, bStatic, gSw, windowLen, f0);
                bmcRecord(csv, fam.name, pair.label, midi, rampMs, cell);
                bmcCheckCell(cell, fam.name, pair.label, midi, rampMs);
            }
        }
    }
}

void bmcRunShard(const BmcFamily& fam) {
    enableFTZDAZ();
    auto csv = bmcOpenCsv(fam.name);
    bmcRunFamily(fam, std::span<const int>(kBmcNotes.data(), kBmcNotes.size()),
                 std::span<const float>(kBmcRampsMs.data(), kBmcRampsMs.size()), csv.get());
}

// -----------------------------------------------------------------------------
// SC-004(d) render: the governing-amount rows, 0 -> 1 -> 0
// -----------------------------------------------------------------------------

struct BmcGovRow {
    std::string label;
    MorphState a{}, b{};
    std::uint8_t leaf = Bmt::kNoLeaf;
    Bmt::TransitionKind kind = Bmt::TransitionKind::CharacterAlgorithm;
    bool anchorGroup = false;
    bool zeroOnA = true;   ///< the governing amount is 0 on A (else on B)
};

/// The T004 test 7 rows on the C-10 base, each with its reverse (the zero on B's side).
std::vector<BmcGovRow> bmcGovRows() {
    std::vector<BmcGovRow> rows;
    auto add = [&rows](const std::string& label, const MorphState& a, const MorphState& b, std::uint8_t leaf,
                       Bmt::TransitionKind kind, bool anchorGroup) {
        REQUIRE(Krate::DSP::isValidMorphState(a));
        REQUIRE(Krate::DSP::isValidMorphState(b));
        rows.push_back(BmcGovRow{.label = label + " zeroA", .a = a, .b = b, .leaf = leaf, .kind = kind,
                                 .anchorGroup = anchorGroup, .zeroOnA = true});
        rows.push_back(BmcGovRow{.label = label + " zeroB", .a = b, .b = a, .leaf = leaf, .kind = kind,
                                 .anchorGroup = anchorGroup, .zeroOnA = false});
    };
    {   // Character: Tube / Amount 0 -> Fold / 0.8
        MorphState a = bmcBase();
        a.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
        a.characterAmount = 0.0f;
        MorphState b = bmcBase();
        b.characterAlgorithm = BassCharacterStage::Algorithm::Fold;
        b.characterAmount = 0.8f;
        add("Character Tube/0->Fold/0.8", a, b, Bmt::kLeafCharacterAlgorithm,
            Bmt::TransitionKind::CharacterAlgorithm, false);
    }
    {   // ImageMode: SOLID / spread 0 -> WIDE / spread 1
        MorphState a = bmcBase();
        a.field.mode = BassStereoField::ImageMode::Solid;
        a.field.spread = 0.0f;
        MorphState b = bmcBase();
        b.field.mode = BassStereoField::ImageMode::Wide;
        b.field.spread = 1.0f;
        add("ImageMode Solid/0->Wide/1", a, b, Bmt::kLeafFieldMode, Bmt::TransitionKind::FieldMode, false);
    }
    {   // Anchor waveform: Sine / level 0 -> Triangle / 0.7
        MorphState a = bmcBase();
        a.anchor.waveform = AnchorOscillator::Waveform::Sine;
        a.anchor.level = 0.0f;
        MorphState b = bmcBase();
        b.anchor.waveform = AnchorOscillator::Waveform::Triangle;
        b.anchor.level = 0.7f;
        add("AnchorWaveform Sine/0->Triangle/0.7", a, b, Bmt::kLeafAnchorWaveform,
            Bmt::TransitionKind::AnchorWaveform, true);
    }
    {   // Anchor mode: Lock / level 0 -> Punch / 0.7
        MorphState a = bmcBase();
        a.anchor.mode = AnchorOscillator::Mode::Lock;
        a.anchor.level = 0.0f;
        MorphState b = bmcBase();
        b.anchor.mode = AnchorOscillator::Mode::Punch;
        b.anchor.level = 0.7f;
        add("AnchorMode Lock/0->Punch/0.7", a, b, Bmt::kLeafAnchorMode, Bmt::TransitionKind::AnchorMode, true);
    }
    REQUIRE(rows.size() == 8);
    return rows;
}

void bmcRunGoverningRow(const BmcGovRow& row, int midi, std::ofstream* csv) {
    const double f0 = bmcF0(midi);
    const float rampMs = BassMorphEngine::kDefaultPositionRampMs;
    const std::size_t period = bmcPeriod(f0);
    const std::size_t windowLen = Bmt::transitionEndSamples(row.kind, f0, kBmcFs) + period;
    const std::size_t pre = bmcSamples(kBmcPreSeconds);
    const std::size_t rampSamples = bmcRoundUpToGrid(bmcSamples(static_cast<double>(rampMs) / 1000.0)) + Bmt::kGrid;
    const std::size_t up = bmcEventSample();
    // The 1 -> 0 event after the rising window and its 50 ms lead-in are clear of each other.
    const std::size_t down = bmcRoundUpToGrid(up + rampSamples + windowLen + 2 * pre);
    const std::size_t total = down + rampSamples + windowLen + pre;
    INFO("row " << row.label << ", note " << bmcNoteName(midi) << ", up " << up << ", down " << down << ", total "
                << total);

    Bmt::MorphChainConfig tc = bmcConfig(row.a, row.b, f0, total, rampMs);
    tc.automation.push_back(bmcEvent(up, Bmt::MorphEvent::Kind::Position, 1.0f));
    tc.automation.push_back(bmcEvent(down, Bmt::MorphEvent::Kind::Position, 0.0f));
    Bmt::MorphChainTaps tt = bmcRender(tc, total);

    // The intervals T004 test 7 names: zero on A -> first q > 0 (rising), landing on 0 (falling);
    // zero on B -> landing on 1 (rising), first q < 1 (falling).
    const auto& rs = tt.resolvedPerInterval;
    std::size_t iRise = rs.size();
    for (std::size_t i = 0; i < rs.size(); ++i) {
        const float q = bmcQ(rs[i], row.anchorGroup);
        if (row.zeroOnA ? (q > 0.0f) : (q >= 1.0f)) {
            iRise = i;
            break;
        }
    }
    REQUIRE(iRise < rs.size());
    std::size_t iFall = rs.size();
    for (std::size_t i = iRise + 1; i < rs.size(); ++i) {
        const float q = bmcQ(rs[i], row.anchorGroup);
        if (row.zeroOnA ? (q <= 0.0f) : (q < 1.0f)) {
            iFall = i;
            break;
        }
    }
    REQUIRE(iFall < rs.size());
    REQUIRE(tt.advanceSamples[iRise] < down);    // the rising switch precedes the 1 -> 0 event
    REQUIRE(tt.advanceSamples[iFall] >= down);

    const auto calls = bmcCallsOf(tt, row.leaf);
    INFO("setter calls " << calls.size() << ", expected rise at " << tt.advanceSamples[iRise] << ", fall at "
                         << tt.advanceSamples[iFall]);
    REQUIRE(calls.size() == 2);   // exactly one per direction
    CHECK(calls[0].sample == tt.advanceSamples[iRise]);
    CHECK(calls[1].sample == tt.advanceSamples[iFall]);
    const std::size_t g1 = calls[0].sample;
    const std::size_t g2 = calls[1].sample;
    REQUIRE(g2 + windowLen + 1 < total);

    // Reference for a rule-2 row (reading refinement, main loop 2026-10-19; spec SC-004(d)): the SAME morph, same
    // automation and ramp, with the discrete field pre-set to the non-silent side on both endpoints, so no step ever
    // fires and the reference carries the identical governing-amount ramp. The C-10 "p held at 0, A' at g_sw"
    // reference is meaningless here: with the governing amount at 0 that side is bypassed, so switching it does
    // nothing and the ratio measured the amount ramp against a static sine (1.84 at E1, clicks 0).
    const MorphState& nonSilent = row.zeroOnA ? row.b : row.a;
    const MorphState aRef = bmcWithDiscrete(row.a, row.leaf, nonSilent);
    const MorphState bRef = bmcWithDiscrete(row.b, row.leaf, nonSilent);
    Bmt::MorphChainConfig rc = bmcConfig(aRef, bRef, f0, total, rampMs);
    rc.automation.push_back(bmcEvent(up, Bmt::MorphEvent::Kind::Position, 1.0f));
    rc.automation.push_back(bmcEvent(down, Bmt::MorphEvent::Kind::Position, 0.0f));
    Bmt::MorphChainTaps rt = bmcRender(rc, total);
    const auto refCalls = bmcCallsOf(rt, row.leaf);
    REQUIRE(refCalls.empty());   // equal on both sides: no step in the reference

    // A-static and B-static (the click references).
    const Bmt::MorphChainTaps at = bmcRender(bmcConfig(row.a, row.a, f0, total, rampMs), total);
    const Bmt::MorphChainTaps bt = bmcRender(bmcConfig(row.b, row.b, f0, total, rampMs), total);
    const BmcStereo aStatic{at.outL, at.outR};
    const BmcStereo bStatic{bt.outL, bt.outR};
    const BmcStereo tr{std::move(tt.outL), std::move(tt.outR)};
    const BmcStereo ref{std::move(rt.outL), std::move(rt.outR)};

    const BmcCell rise = bmcMeasure(tr, ref, aStatic, bStatic, g1, windowLen, f0);
    bmcRecord(csv, "governing-amount", row.label + " rise", midi, rampMs, rise);
    bmcCheckCell(rise, "governing-amount", row.label + " rise", midi, rampMs);
    const BmcCell fall = bmcMeasure(tr, ref, aStatic, bStatic, g2, windowLen, f0);
    bmcRecord(csv, "governing-amount", row.label + " fall", midi, rampMs, fall);
    bmcCheckCell(fall, "governing-amount", row.label + " fall", midi, rampMs);
}

}  // namespace

// =============================================================================
// SC-004(a)(b) shards ([long]; one per field family, plan R-12) — plan S1 C-10, deviation 10
// =============================================================================

// SC-004: all 30 ordered characterAlgorithm pairs.
TEST_CASE("BassMorph_DiscreteClickFree_Algorithm", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcAlgorithmFamily());
}

// SC-004: the 6 ordered field.mode pairs (transition end = one period + 334 intervals, deviation 10).
TEST_CASE("BassMorph_DiscreteClickFree_ImageMode", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcImageModeFamily());
}

// SC-004: the 20 ordered anchor.waveform pairs (Blend: p_a = p, FR-055).
TEST_CASE("BassMorph_DiscreteClickFree_AnchorWaveform", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcAnchorWaveformFamily());
}

// SC-004: the 12 ordered anchor.mode pairs (Blend: p_a = p, FR-055).
TEST_CASE("BassMorph_DiscreteClickFree_AnchorMode", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcAnchorModeFamily());
}

// SC-004: the 2 ordered field.motionSource pairs (motionDepth 1).
TEST_CASE("BassMorph_DiscreteClickFree_FieldMotionSource", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcFieldMotionSourceFamily());
}

// SC-004: the 2 ordered field.cycleSync pairs (motionSource Cycle, motionDepth 1).
TEST_CASE("BassMorph_DiscreteClickFree_FieldCycleSync", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcFieldCycleSyncFamily());
}

// SC-004: the 6 ordered field.cycleModifier pairs at cycleNote Quarter (Cycle, sync on, motionDepth 1).
TEST_CASE("BassMorph_DiscreteClickFree_FieldCycleModifier", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcFieldCycleModifierFamily());
}

// SC-004: the 6 ordered field.cycleNote pairs among {SixtyFourth, Quarter, QuadrupleWhole} at modifier None.
TEST_CASE("BassMorph_DiscreteClickFree_FieldCycleNote", "[systems][profundum][bass-morph][sc-004][long]") {
    bmcRunShard(bmcFieldCycleNoteFamily());
}

// SC-004(d) render (FR-034 rule 2): the T004 test 7 rows, 0 -> 1 -> 0 at the default ramp; one setter call per
// direction at the intervals T004 test 7 names; each switch meets (a) and (b) against its direct-setter reference.
TEST_CASE("BassMorph_DiscreteClickFree_GoverningAmount", "[systems][profundum][bass-morph][sc-004][long]") {
    enableFTZDAZ();
    auto csv = bmcOpenCsv("governing-amount");
    for (const BmcGovRow& row : bmcGovRows()) {
        for (const int midi : kBmcNotes)
            bmcRunGoverningRow(row, midi, csv.get());
    }
}

// =============================================================================
// Smoke sibling (deviation 5): one cell, the same assertions, per-push
// =============================================================================

// SC-004(a)(b): Tube -> Fold, E1, default ramp — plan S1 C-10.
TEST_CASE("BassMorph_DiscreteClickFreeSmoke", "[systems][profundum][bass-morph][sc-004]") {
    enableFTZDAZ();
    BmcFamily fam = bmcAlgorithmFamily();
    std::vector<BmcPair> one;
    for (const BmcPair& p : fam.pairs) {
        if (p.a.characterAlgorithm == BassCharacterStage::Algorithm::Tube
            && p.b.characterAlgorithm == BassCharacterStage::Algorithm::Fold)
            one.push_back(p);
    }
    REQUIRE(one.size() == 1);
    fam.pairs = one;
    const std::array<int, 1> notes{kBmcMidiE1};
    const std::array<float, 1> ramps{BassMorphEngine::kDefaultPositionRampMs};
    bmcRunFamily(fam, std::span<const int>(notes.data(), notes.size()),
                 std::span<const float>(ramps.data(), ramps.size()), nullptr);
}
