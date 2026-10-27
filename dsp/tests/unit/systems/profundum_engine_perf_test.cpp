// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_perf_test.cpp
// ==============================================================================
// tasks.md T020 (SC-001 ... SC-004; FR-083, FR-014; plan S10.6, C-14, C-16). Shipping FP mode (this TU is NOT in
// the -fno-fast-math block). Every case is [performance][.perf]: hidden, never inside a gate (the gate filter ends
// in ~[.]), run by the main loop LAST and ALONE through evidence/close/cpu-lane.sh, pinned to the performance
// cores (tools/pin-perf-cores.ps1).
//
// Protocol shared by SC-001 / SC-002 / SC-004 (the P-1 protocol of profundum_engine_probe_test.cpp, so the lane's
// figures are comparable with evidence/probes/p1-cpu.txt): the spec SC-001 setting on BOTH showcase endpoints
// (excitation mix 1 / drive 1, SHAPE 0.5, Fold 100, Character Tube | Fracture at Amount 1 (C-16), Anchor FOLLOW,
// anchorFoldFeed 1, Drop 24 st retriggered every 250 ms (noteOff + noteOn of every held note at the first host-block
// boundary at or after the mark), Motion CYCLE -> Mass amount 1 depth 1, WIDE spread 1 with field Motion (CYCLE,
// motionDepth 1), Mass 100), the morph ramping 0 <-> 1 on the showcase pair (ramp 1000 ms, target toggled every
// 1 s), host block 64, 48 kHz, FTZ / DAZ on, no observer. 10 s timed after a 1 s warm-up, median of 3 fresh
// engines, % of one core = wall / audio x 100.
//
//   VoiceEngine_CpuMono        SC-001: MONO (E1), Tube and Fracture; gate max <= 8 %. Per-stage breakdown on the
//                              real engine by LEVER ABLATION (one lever at its idle value, the rest unchanged;
//                              cost = full - without), for both algorithms. Not the deviation-10 replica (that
//                              lives in the probe TU); the ablation measures the shipped composition and the idle
//                              value of a stage is what the engine skips or makes cheap, so a cost here is a lever
//                              the instrument really has. Last line "SC001 VERDICT GREEN|RED <%>";
//                              evidence/sc001-cpu.txt.
//   VoiceEngine_CpuPoly4       SC-002: POLY, four held notes E1 / A1 / E2 / C3, PerVoice; gate max <= 25 %. The
//                              8-voice cell (E1 / C3 / A1 / E2 / G1 / B1 / E3 / A2) printed as RECORD (Q8, not
//                              gated). "SC002 VERDICT GREEN|RED <%>"; evidence/sc002-cpu.txt.
//   VoiceEngine_WorstInterval  SC-003 (plan S10.6 row): 4 voices held (E1 / A1 / E2 / C3), block 64, ramp 5 ms, the
//                              position toggled 0 <-> 1 every 100 ms. The endpoint pair differs in all five
//                              selection groups (Character Tube <-> Fracture, ImageMode SOLID <-> WIDE, FieldMotion
//                              Drift <-> Cycle, Motion ENV <-> CYCLE, Anchor Sine <-> Triangle) with every governing
//                              amount non-zero, Anchor morph mode Blend, and all three amp curves -1 <-> +1. Both
//                              endpoints carry ampEnv.sustain = 1 (the C-8 form) so the held voices sound for the
//                              whole render (no retriggers in this protocol). Each processBlock timed with
//                              std::chrono::steady_clock; 3 renders, per-block median across the renders (the
//                              Phase 8 BassMorph_WorstTransitionIntervalRecord precedent: one preemption spike in
//                              one render does not set the figure; the raw maximum is printed beside it); gate
//                              max(block wall / 1.333 ms) <= 1.0. The worst 32-sample interval: the same 3 renders
//                              at host block 32 (each block is exactly one grid interval, C-15), recorded against
//                              Phase 8's 81.6 %. Coincidence and coverage asserts from a 4th, untimed render with an
//                              observer attached: the five selections (read from the stepped fields of the
//                              resolved state, each endpoint pair differing in every one) change together in one
//                              MorphAdvance interval with Resolved::anchorPosition == position bitwise there; and
//                              inside the worst timed block every one of the 4 voices issues an AmpCurve record
//                              with issued == true. H-13 record: the mean block cost inside the Character
//                              crossfade window (switch grid point + one E1 period of pre-roll + kSwitchCrossfadeMs)
//                              as % of a core per voice, against Phase 5's 2.65 %. evidence/sc003-worst-interval.txt.
//   VoiceEngine_PlacementCost  SC-004: PerVoice vs PostSum (C-14) at 4 voices under SC-002's protocol, and FOLLOW
//                              per voice vs off (Anchor mode Lock), both algorithms; the FR-014 criterion evaluated
//                              on both readings (recorded, not gated). evidence/placement.txt.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_morph_modulator.h>   // Phase 14 T027 (SC-014 (b), (d))
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <enable_ftz_daz.h>

#include <krate/dsp/core/db_utils.h>   // Phase 14 T027: detail::isFinite
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace pfPt = Krate::DSP::ProfundumTest;

constexpr double kPfFs = 48000.0;
constexpr std::size_t kPfGrid = ProfundumEngine::kControlInterval;
constexpr std::uint8_t kPfVelocity = 100;
constexpr double kPfWarmupS = 1.0;
constexpr double kPfTimedS = 10.0;
constexpr std::size_t kPfTrials = 3;
constexpr std::size_t kPfBlock = 64;
constexpr double kPfRetriggerS = 0.25;
constexpr double kPfMorphToggleS = 1.0;
constexpr float kPfMorphRampMs = 1000.0f;
constexpr double kPfMonoBarPct = 8.0;     // SC-001
constexpr double kPfPolyBarPct = 25.0;    // SC-002, FR-014
constexpr double kPfBlockBudgetMs = 1000.0 * static_cast<double>(kPfBlock) / kPfFs;   // 1.333 ms (SC-003)
constexpr double kPfIntervalBudgetMs = 1000.0 * static_cast<double>(kPfGrid) / kPfFs;
constexpr double kPfPhase8IntervalPct = 81.6;   // H-25 (Phase 8 worst transition interval)
constexpr double kPfPhase5CrossfadePct = 2.65;  // H-13 (Phase 5 crossfade cell)
constexpr double kPfSc003ToggleS = 0.1;
constexpr float kPfSc003RampMs = BassMorphEngine::kMinPositionRampMs;   // 5 ms (FR-021)

constexpr std::array<std::uint8_t, 4> kPfPoly4Notes{pfPt::kVoiceEngineNoteE1, pfPt::kVoiceEngineNoteA1,
                                                    pfPt::kVoiceEngineNoteE2, pfPt::kVoiceEngineNoteC3};
constexpr std::array<std::uint8_t, 8> kPfPoly8Notes{pfPt::kVoiceEngineNoteE1, pfPt::kVoiceEngineNoteC3,
                                                    pfPt::kVoiceEngineNoteA1, pfPt::kVoiceEngineNoteE2,
                                                    pfPt::kVoiceEngineNoteG1, pfPt::kVoiceEngineNoteB1,
                                                    pfPt::kVoiceEngineNoteE3, pfPt::kVoiceEngineNoteA2};
constexpr std::array<BassCharacterStage::Algorithm, 2> kPfAlgorithms{BassCharacterStage::Algorithm::Tube,
                                                                    BassCharacterStage::Algorithm::Fracture};

[[nodiscard]] const char* pfAlgName(BassCharacterStage::Algorithm a) noexcept {
    return a == BassCharacterStage::Algorithm::Tube ? "Tube" : "Fracture";
}

/// Bit test (shipping FP mode: std::isfinite may be folded away under -ffast-math).
[[nodiscard]] bool pfFinite(double x) noexcept {
    return (std::bit_cast<std::uint64_t>(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

[[nodiscard]] bool pfSame(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

std::string pfEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

void pfWriteEvidence(const std::string& name, const std::string& text) {
    const std::filesystem::path p = std::filesystem::path(pfEvidenceDir()) / name;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << text;
    CHECK(out.good());
}

[[nodiscard]] double pfMedian(std::vector<double> v) {
    REQUIRE(!v.empty());
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// -----------------------------------------------------------------------------
// The SC-001 setting and the breakdown levers
// -----------------------------------------------------------------------------

/// One lever at its idle value (SC-001 breakdown). None = the full SC-001 setting.
enum class PfLever : std::uint8_t {
    None, Excitation, Shape, Fold, FoldFeed, Character, Anchor, Follow, Drop, Motion, Field, Mass, DynOversampling,
    MorphRamp, Retrigger, Count
};
constexpr std::size_t kPfNumLevers = static_cast<std::size_t>(PfLever::Count);
constexpr std::array<const char*, kPfNumLevers> kPfLeverNames{
    "full",          "excitation(mix 0)",     "shape(0)",         "fold(0)",
    "fold-feed(0)",  "character(amount 0)",   "anchor(level 0)",  "follow(mode Lock)",
    "drop(0 st)",    "motion(source Off)",    "field(SOLID, spread 0, motionDepth 0)", "mass(neutral)",
    "dynamics-oversampling(off)", "morph(position held at 0)", "retrigger(none)"};

/// The spec SC-001 setting on one endpoint; everything else stays the showcase endpoint's (P-1's p1ApplySetting).
void pfApplySc001(MorphState& s, BassCharacterStage::Algorithm alg) {
    s.excitation.mix = 1.0f;
    s.excitation.drive = 1.0f;
    s.shape = 0.5f;
    s.foldAmount = 100.0f;
    s.characterAlgorithm = alg;
    s.characterAmount = 1.0f;
    s.anchor.mode = AnchorOscillator::Mode::Follow;
    s.anchorFoldFeed = 1.0f;
    s.dropAmountSt = 24.0f;
    s.motion.source = MorphState::MotionConfig::Source::Cycle;
    s.motion.target = MorphState::MotionConfig::Target::Mass;
    s.motion.amount = 1.0f;
    s.motion.depth = 1.0f;
    s.field.mode = BassStereoField::ImageMode::Wide;
    s.field.spread = 1.0f;
    s.field.motionDepth = 1.0f;
    s.field.motionSource = BassStereoField::MotionSource::Cycle;
    s.mass = 100.0f;
}

/// The state-side levers (the run-side ones - DynOversampling, MorphRamp, Retrigger - are read by pfEngineTrial).
void pfApplyLever(MorphState& s, PfLever lever) {
    switch (lever) {
        case PfLever::Excitation: s.excitation.mix = 0.0f; break;
        case PfLever::Shape: s.shape = 0.0f; break;
        case PfLever::Fold: s.foldAmount = 0.0f; break;
        case PfLever::FoldFeed: s.anchorFoldFeed = 0.0f; break;
        case PfLever::Character: s.characterAmount = 0.0f; break;
        case PfLever::Anchor: s.anchor.level = 0.0f; break;
        case PfLever::Follow: s.anchor.mode = AnchorOscillator::Mode::Lock; break;
        case PfLever::Drop: s.dropAmountSt = 0.0f; break;
        case PfLever::Motion: s.motion.source = MorphState::MotionConfig::Source::Off; break;
        case PfLever::Field:
            s.field.mode = BassStereoField::ImageMode::Solid;
            s.field.spread = 0.0f;
            s.field.motionDepth = 0.0f;
            break;
        case PfLever::Mass: s.mass = MassLaw::kNeutralMass; break;
        default: break;
    }
}

[[nodiscard]] std::pair<MorphState, MorphState> pfSc001Endpoints(BassCharacterStage::Algorithm alg, PfLever lever) {
    const std::pair<MorphState, MorphState> pair = BassMorphTest::showcasePair();
    MorphState a = pair.first;
    MorphState b = pair.second;
    pfApplySc001(a, alg);
    pfApplySc001(b, alg);
    pfApplyLever(a, lever);
    pfApplyLever(b, lever);
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

struct PfRun {
    BassCharacterStage::Algorithm alg = BassCharacterStage::Algorithm::Tube;
    std::vector<std::uint8_t> notes;
    ProfundumNonlinearPlacement placement = ProfundumNonlinearPlacement::PerVoice;
    PfLever lever = PfLever::None;
};

[[nodiscard]] PfRun pfMonoRun(BassCharacterStage::Algorithm alg, PfLever lever = PfLever::None) {
    PfRun r;
    r.alg = alg;
    r.notes = {pfPt::kVoiceEngineNoteE1};
    r.lever = lever;
    return r;
}

template <std::size_t N>
[[nodiscard]] PfRun pfPolyRun(BassCharacterStage::Algorithm alg, const std::array<std::uint8_t, N>& notes,
                              ProfundumNonlinearPlacement placement, PfLever lever = PfLever::None) {
    PfRun r;
    r.alg = alg;
    r.notes.assign(notes.begin(), notes.end());
    r.placement = placement;
    r.lever = lever;
    return r;
}

/// One timed trial of the real engine on the SC-001 protocol (no observer). @return % of one core.
[[nodiscard]] double pfEngineTrial(const PfRun& run, double& sink) {
    const std::pair<MorphState, MorphState> ends = pfSc001Endpoints(run.alg, run.lever);
    const bool mono = run.notes.size() <= 1;
    const bool staticMorph = run.lever == PfLever::MorphRamp;
    const bool noRetrigger = run.lever == PfLever::Retrigger;
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, ends.first));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, ends.second));
    e->setMorphPosition(0.0f);
    e->setMorphRampMs(kPfMorphRampMs);
    e->setVoiceMode(mono ? ProfundumEngine::VoiceMode::Mono : ProfundumEngine::VoiceMode::Poly);
    e->setPolyphony(std::max<std::size_t>(run.notes.size(), 1));
    ProfundumEngineConfig cfg{};
    cfg.placement = run.placement;
    if (run.lever == PfLever::DynOversampling)
        cfg.dynamicsOversampling = false;
    e->prepare(kPfFs, cfg);
    for (const std::uint8_t n : run.notes)
        e->noteOn(n, kPfVelocity);

    std::array<float, kPfBlock> l{};
    std::array<float, kPfBlock> r{};
    const auto retrigger = static_cast<std::size_t>(std::llround(kPfRetriggerS * kPfFs));
    const auto toggle = static_cast<std::size_t>(std::llround(kPfMorphToggleS * kPfFs));
    std::size_t nextRetrigger = retrigger;
    std::size_t nextToggle = toggle;
    float position = 0.0f;
    std::size_t pos = 0;
    double acc = 0.0;
    const auto step = [&]() noexcept {
        if (pos >= nextRetrigger && !noRetrigger) {
            for (const std::uint8_t n : run.notes) {
                e->noteOff(n);
                e->noteOn(n, kPfVelocity);
            }
            nextRetrigger += retrigger;
        }
        if (pos >= nextToggle && !staticMorph) {
            position = 1.0f - position;
            e->setMorphPosition(position);
            nextToggle += toggle;
        }
        e->processBlock(l.data(), r.data(), kPfBlock);
        acc += static_cast<double>(l[0]) + static_cast<double>(r[kPfBlock - 1]);
        pos += kPfBlock;
    };
    const auto warm = static_cast<std::size_t>(std::llround(kPfWarmupS * kPfFs));
    while (pos < warm)
        step();
    const std::size_t start = pos;
    const std::size_t end = start + static_cast<std::size_t>(std::llround(kPfTimedS * kPfFs));
    const auto t0 = std::chrono::steady_clock::now();
    while (pos < end)
        step();
    const auto t1 = std::chrono::steady_clock::now();
    sink += acc;
    const double wall = std::chrono::duration<double>(t1 - t0).count();
    const double audio = static_cast<double>(pos - start) / kPfFs;
    return 100.0 * wall / audio;
}

/// Median of kPfTrials fresh-engine trials; every trial is printed to @p log.
[[nodiscard]] double pfMedianTrial(const PfRun& run, std::ostringstream& log, const std::string& label, double& sink) {
    std::vector<double> v;
    v.reserve(kPfTrials);
    log << "trial " << label << ":";
    for (std::size_t t = 0; t < kPfTrials; ++t) {
        v.push_back(pfEngineTrial(run, sink));
        log << ' ' << v.back();
    }
    const double m = pfMedian(v);
    log << " -> median " << m << '\n';
    return m;
}

void pfHeader(std::ostringstream& os, const char* title) {
    os << std::setprecision(5);
    os << "# " << title << " (tasks.md T020; plan S10.6)\n"
       << "# SC-001 setting on both showcase endpoints, morph ramp " << kPfMorphRampMs << " ms toggled every "
       << kPfMorphToggleS << " s, Drop 24 st retriggered every " << kPfRetriggerS << " s, block " << kPfBlock << ", "
       << kPfTimedS << " s timed after " << kPfWarmupS << " s warm-up, median of " << kPfTrials
       << " fresh engines, % of one core = wall / audio x 100, 48 kHz, FTZ/DAZ on, no observer.\n";
}

// -----------------------------------------------------------------------------
// SC-003: the five-group pair, the timed renders and the observer run
// -----------------------------------------------------------------------------

/// The SC-003 endpoint pair (plan S10.6): the SC-001 setting (Tube) as the base, then every selection group and
/// all three amp curves differing; every governing amount non-zero on both sides; sustain 1 on both.
[[nodiscard]] std::pair<MorphState, MorphState> pfSc003Endpoints() {
    std::pair<MorphState, MorphState> ends = pfSc001Endpoints(BassCharacterStage::Algorithm::Tube, PfLever::None);
    MorphState& a = ends.first;
    MorphState& b = ends.second;
    a.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    b.characterAlgorithm = BassCharacterStage::Algorithm::Fracture;
    a.field.mode = BassStereoField::ImageMode::Solid;
    b.field.mode = BassStereoField::ImageMode::Wide;
    a.field.motionSource = BassStereoField::MotionSource::Drift;
    b.field.motionSource = BassStereoField::MotionSource::Cycle;
    a.motion.source = MorphState::MotionConfig::Source::Env;
    b.motion.source = MorphState::MotionConfig::Source::Cycle;
    a.anchor.waveform = AnchorOscillator::Waveform::Sine;
    b.anchor.waveform = AnchorOscillator::Waveform::Triangle;
    for (MorphState* s : {&a, &b}) {
        const float c = (s == &a) ? -1.0f : 1.0f;
        s->ampEnv.attackCurve = c;
        s->ampEnv.decayCurve = c;
        s->ampEnv.releaseCurve = c;
        s->ampEnv.sustain = 1.0f;
    }
    // Every governing amount non-zero on both sides (bass_morph_engine.h:379-383), so every group switches by rule 3.
    for (const MorphState* s : {&a, &b}) {
        REQUIRE(s->characterAmount != 0.0f);
        REQUIRE(s->field.spread != 0.0f);
        REQUIRE(s->field.motionDepth != 0.0f);
        REQUIRE(s->motion.amount != 0.0f);
        REQUIRE(s->anchor.level != 0.0f);
    }
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return ends;
}

/// The five selections inferred from the resolved state's stepped fields (each differs between the endpoints),
/// as a 5-bit mask: bit g set iff group g resolves to endpoint B.
[[nodiscard]] std::uint8_t pfSelectionMask(const MorphState& r, const MorphState& b) noexcept {
    unsigned m = 0u;
    if (r.characterAlgorithm == b.characterAlgorithm)
        m |= 1u << 0;
    if (r.field.mode == b.field.mode)
        m |= 1u << 1;
    if (r.field.motionSource == b.field.motionSource)
        m |= 1u << 2;
    if (r.motion.source == b.motion.source)
        m |= 1u << 3;
    if (r.anchor.waveform == b.anchor.waveform)
        m |= 1u << 4;
    return static_cast<std::uint8_t>(m);
}

/// The untimed SC-003 observer (plan S10.6 coincidence and coverage asserts). Storage sized before the render;
/// the callbacks never allocate.
class PfSc003Observer final : public ProfundumEngineObserver {
public:
    struct Grid {
        std::uint64_t sample = 0;
        std::uint8_t mask = 0;
        float position = 0.0f;
        float anchorPosition = 0.0f;
    };

    PfSc003Observer(const MorphState& b, std::size_t intervals) : b_(b) {
        grid_.reserve(intervals + 4);
        curveVoices_.assign(intervals + 4, 0);
    }

    void onGrid(const ProfundumGridRecord& g) noexcept override {
        if (grid_.size() < grid_.capacity() && g.resolved != nullptr) {
            grid_.push_back(Grid{.sample = g.sample,
                                 .mask = pfSelectionMask(g.resolved->state, b_),
                                 .position = g.resolved->position,
                                 .anchorPosition = g.resolved->anchorPosition});
        }
    }

    void onCall(const ProfundumCallRecord& c) noexcept override {
        if (c.call != ProfundumCall::AmpCurve || !c.issued || c.voice >= ProfundumEngine::kMaxVoices)
            return;
        const std::size_t i = static_cast<std::size_t>(c.sample / kPfGrid);
        if (i < curveVoices_.size())
            curveVoices_[i] = static_cast<std::uint8_t>(curveVoices_[i] | (1u << c.voice));
    }

    [[nodiscard]] const std::vector<Grid>& grid() const noexcept { return grid_; }
    /// Bit v set iff voice v issued an AmpCurve record (issued == true) at grid interval i.
    [[nodiscard]] std::uint8_t curveVoices(std::size_t i) const noexcept {
        return i < curveVoices_.size() ? curveVoices_[i] : std::uint8_t{0};
    }

private:
    MorphState b_;
    std::vector<Grid> grid_;
    std::vector<std::uint8_t> curveVoices_;
};

/// One SC-003 render (1 s warm-up + 10 s; the warm-up blocks are rendered but not reported). With @p blockMs
/// non-null, every reported block's wall time (ms) is written there; with @p observer non-null it is attached.
void pfSc003Render(std::size_t hostBlock, std::vector<double>* blockMs, PfSc003Observer* observer, double& sink) {
    const std::pair<MorphState, MorphState> ends = pfSc003Endpoints();
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, ends.first));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, ends.second));
    e->setAnchorMorphMode(BassMorphEngine::AnchorMorphMode::Blend);
    e->setMorphPosition(0.0f);
    e->setMorphRampMs(kPfSc003RampMs);
    e->setVoiceMode(ProfundumEngine::VoiceMode::Poly);
    e->setPolyphony(kPfPoly4Notes.size());
    if (observer != nullptr)
        e->setObserver(observer);
    e->prepare(kPfFs, ProfundumEngineConfig{});
    for (const std::uint8_t n : kPfPoly4Notes)
        e->noteOn(n, kPfVelocity);

    std::vector<float> l(hostBlock, 0.0f);
    std::vector<float> r(hostBlock, 0.0f);
    const auto toggle = static_cast<std::size_t>(std::llround(kPfSc003ToggleS * kPfFs));
    const auto warm = static_cast<std::size_t>(std::llround(kPfWarmupS * kPfFs));
    const std::size_t total = warm + static_cast<std::size_t>(std::llround(kPfTimedS * kPfFs));
    std::size_t nextToggle = toggle;
    float position = 0.0f;
    double acc = 0.0;
    if (blockMs != nullptr) {
        blockMs->clear();
        blockMs->reserve(total / hostBlock + 1);
    }
    for (std::size_t pos = 0; pos < total; pos += hostBlock) {
        if (pos >= nextToggle) {
            position = 1.0f - position;
            e->setMorphPosition(position);
            nextToggle += toggle;
        }
        const auto t0 = std::chrono::steady_clock::now();
        e->processBlock(l.data(), r.data(), hostBlock);
        const auto t1 = std::chrono::steady_clock::now();
        acc += static_cast<double>(l[0]) + static_cast<double>(r[hostBlock - 1]);
        if (blockMs != nullptr && pos >= warm)
            blockMs->push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    if (observer != nullptr)
        e->setObserver(nullptr);
    sink += acc;
}

/// Per-block median across the renders. @return the median series (same length as each render's series).
[[nodiscard]] std::vector<double> pfPerBlockMedian(const std::vector<std::vector<double>>& renders) {
    REQUIRE(!renders.empty());
    const std::size_t n = renders.front().size();
    for (const std::vector<double>& v : renders)
        REQUIRE(v.size() == n);
    std::vector<double> med(n, 0.0);
    std::vector<double> cell(renders.size(), 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < renders.size(); ++k)
            cell[k] = renders[k][i];
        std::sort(cell.begin(), cell.end());
        med[i] = cell[cell.size() / 2];
    }
    return med;
}

}  // namespace

// =============================================================================
// SC-001
// =============================================================================

TEST_CASE("VoiceEngine_CpuMono", "[systems][profundum][voice-engine][sc-001][performance][.perf]") {
    enableFTZDAZ();
    std::ostringstream os;
    pfHeader(os, "SC-001 VoiceEngine_CpuMono (MONO, E1)");
    double sink = 0.0;

    std::array<double, 2> full{};
    for (std::size_t ai = 0; ai < kPfAlgorithms.size(); ++ai) {
        const BassCharacterStage::Algorithm alg = kPfAlgorithms[ai];
        full[ai] = pfMedianTrial(pfMonoRun(alg), os, std::string("MONO ") + pfAlgName(alg) + " full", sink);
        os << "SC001 MONO " << pfAlgName(alg) << ' ' << full[ai] << '\n';
    }

    os << "# per-stage breakdown: lever ablation of the real engine (one lever at its idle value, the rest the SC-001 "
          "setting); cost = full - without, % of one core\n";
    for (std::size_t ai = 0; ai < kPfAlgorithms.size(); ++ai) {
        const BassCharacterStage::Algorithm alg = kPfAlgorithms[ai];
        for (std::size_t li = 1; li < kPfNumLevers; ++li) {
            const auto lever = static_cast<PfLever>(li);
            const double without = pfMedianTrial(pfMonoRun(alg, lever), os,
                                                 std::string("MONO ") + pfAlgName(alg) + " without " + kPfLeverNames[li],
                                                 sink);
            os << "SC001 STAGE " << pfAlgName(alg) << ' ' << kPfLeverNames[li] << " without=" << without
               << " cost=" << (full[ai] - without) << '\n';
        }
    }

    const double worst = std::max(full[0], full[1]);
    const bool pass = worst <= kPfMonoBarPct;
    os << "# checksum " << sink << " (keeps the renders observable)\n";
    os << "SC001 VERDICT " << (pass ? "GREEN" : "RED") << ' ' << worst << " (Tube " << full[0] << ", Fracture "
       << full[1] << "; bar " << kPfMonoBarPct << " %)\n";
    std::cout << os.str();
    pfWriteEvidence("sc001-cpu.txt", os.str());
    CHECK(pfFinite(sink));
    CHECK(worst <= kPfMonoBarPct);
}

// =============================================================================
// SC-002
// =============================================================================

TEST_CASE("VoiceEngine_CpuPoly4", "[systems][profundum][voice-engine][sc-002][performance][.perf]") {
    enableFTZDAZ();
    std::ostringstream os;
    pfHeader(os, "SC-002 VoiceEngine_CpuPoly4 (POLY, E1 / A1 / E2 / C3, PerVoice)");
    double sink = 0.0;

    std::array<double, 2> poly4{};
    std::array<double, 2> poly8{};
    for (std::size_t ai = 0; ai < kPfAlgorithms.size(); ++ai) {
        const BassCharacterStage::Algorithm alg = kPfAlgorithms[ai];
        poly4[ai] = pfMedianTrial(pfPolyRun(alg, kPfPoly4Notes, ProfundumNonlinearPlacement::PerVoice), os,
                                  std::string("POLY4 ") + pfAlgName(alg), sink);
        os << "SC002 POLY4 " << pfAlgName(alg) << ' ' << poly4[ai] << '\n';
    }
    for (std::size_t ai = 0; ai < kPfAlgorithms.size(); ++ai) {
        const BassCharacterStage::Algorithm alg = kPfAlgorithms[ai];
        poly8[ai] = pfMedianTrial(pfPolyRun(alg, kPfPoly8Notes, ProfundumNonlinearPlacement::PerVoice), os,
                                  std::string("POLY8 ") + pfAlgName(alg), sink);
    }

    const double worst = std::max(poly4[0], poly4[1]);
    const double worst8 = std::max(poly8[0], poly8[1]);
    const bool pass = worst <= kPfPolyBarPct;
    os << "# checksum " << sink << " (keeps the renders observable)\n";
    os << "SC002 RECORD POLY8 (E1 / C3 / A1 / E2 / G1 / B1 / E3 / A2; Q8, not gated) Tube " << poly8[0]
       << " Fracture " << poly8[1] << " max " << worst8 << '\n';
    os << "SC002 VERDICT " << (pass ? "GREEN" : "RED") << ' ' << worst << " (Tube " << poly4[0] << ", Fracture "
       << poly4[1] << "; bar " << kPfPolyBarPct << " %)\n";
    std::cout << os.str();
    pfWriteEvidence("sc002-cpu.txt", os.str());
    CHECK(pfFinite(sink));
    CHECK(pfFinite(worst8));
    CHECK(worst <= kPfPolyBarPct);
}

// =============================================================================
// SC-003
// =============================================================================

TEST_CASE("VoiceEngine_WorstInterval", "[systems][profundum][voice-engine][sc-003][performance][.perf]") {
    enableFTZDAZ();
    std::ostringstream os;
    os << std::setprecision(5);
    os << "# SC-003 VoiceEngine_WorstInterval (tasks.md T020; plan S10.6 SC-003 row)\n"
       << "# 4 voices held E1 / A1 / E2 / C3, block " << kPfBlock << ", ramp " << kPfSc003RampMs
       << " ms, position toggled 0 <-> 1 every " << kPfSc003ToggleS
       << " s; pair Tube<->Fracture / SOLID<->WIDE / field Motion Drift<->Cycle / Motion ENV<->CYCLE / Anchor "
          "Sine<->Triangle, all amp curves -1<->+1, sustain 1, Anchor morph Blend; "
       << kPfTimedS << " s reported after " << kPfWarmupS << " s warm-up; " << kPfTrials
       << " renders, per-block median across renders\n";
    double sink = 0.0;

    // ---- timed: host block 64 ----
    std::vector<std::vector<double>> renders64(kPfTrials);
    for (std::vector<double>& v : renders64)
        pfSc003Render(kPfBlock, &v, nullptr, sink);
    const std::vector<double> med64 = pfPerBlockMedian(renders64);
    REQUIRE(!med64.empty());
    const auto worstIt = std::max_element(med64.begin(), med64.end());
    const std::size_t worstBlock = static_cast<std::size_t>(std::distance(med64.begin(), worstIt));
    const double worstRatio = *worstIt / kPfBlockBudgetMs;
    double rawMax = 0.0;
    for (const std::vector<double>& v : renders64)
        rawMax = std::max(rawMax, *std::max_element(v.begin(), v.end()));
    double meanMs = 0.0;
    for (const double x : med64)
        meanMs += x;
    meanMs /= static_cast<double>(med64.size());

    // ---- timed: host block 32 (one block == one grid interval, C-15) ----
    std::vector<std::vector<double>> renders32(kPfTrials);
    for (std::vector<double>& v : renders32)
        pfSc003Render(kPfGrid, &v, nullptr, sink);
    const std::vector<double> med32 = pfPerBlockMedian(renders32);
    REQUIRE(!med32.empty());
    const double worstIntervalPct = 100.0 * *std::max_element(med32.begin(), med32.end()) / kPfIntervalBudgetMs;

    // ---- untimed observer run (coincidence and coverage asserts) ----
    const std::pair<MorphState, MorphState> ends = pfSc003Endpoints();
    const std::size_t totalSamples =
        static_cast<std::size_t>(std::llround((kPfWarmupS + kPfTimedS) * kPfFs)) + kPfBlock;
    auto observer = std::make_unique<PfSc003Observer>(ends.second, totalSamples / kPfGrid + 2);
    pfSc003Render(kPfBlock, nullptr, observer.get(), sink);
    const std::vector<PfSc003Observer::Grid>& grid = observer->grid();
    // One grid record per interval of the render (the reserve never ran out).
    REQUIRE(grid.size() + 1 >= (totalSamples - kPfBlock) / kPfGrid);

    std::size_t switches = 0;
    std::size_t coincident = 0;
    std::size_t anchorAligned = 0;
    std::vector<std::uint64_t> charSwitchSamples;
    for (std::size_t i = 1; i < grid.size(); ++i) {
        const std::uint8_t changed = static_cast<std::uint8_t>(grid[i].mask ^ grid[i - 1].mask);
        if (changed == 0)
            continue;
        ++switches;
        if (changed == 0x1Fu)
            ++coincident;
        if (pfSame(grid[i].anchorPosition, grid[i].position))
            ++anchorAligned;
        if ((changed & 0x1u) != 0u)
            charSwitchSamples.push_back(grid[i].sample);
    }

    // Coverage (main loop 2026-10-21, G14-7): inside every switch block (the host block that starts at a
    // Character-switch grid point) all 4 voices issued an AmpCurve record. The timed worst block is whichever block
    // cost most; it need not be a switch block (the first run's worst block held no switch and read mask 0x0), so
    // the worst block's mask is recorded beside it.
    const std::size_t warmSamples = static_cast<std::size_t>(std::llround(kPfWarmupS * kPfFs));
    const std::size_t worstStart = warmSamples + worstBlock * kPfBlock;
    std::uint8_t curveVoices = 0;
    for (std::size_t s = worstStart; s < worstStart + kPfBlock; s += kPfGrid)
        curveVoices = static_cast<std::uint8_t>(curveVoices | observer->curveVoices(s / kPfGrid));
    int curveVoiceCount = charSwitchSamples.empty() ? 0 : 4;
    std::size_t switchBlocksFull = 0;
    bool worstIsSwitch = false;
    for (const std::uint64_t sw : charSwitchSamples) {
        std::uint8_t m = 0;
        for (std::uint64_t s = sw; s < sw + kPfBlock; s += kPfGrid)
            m = static_cast<std::uint8_t>(m | observer->curveVoices(static_cast<std::size_t>(s / kPfGrid)));
        const int n = std::popcount(static_cast<unsigned>(m));
        curveVoiceCount = std::min(curveVoiceCount, n);
        if (static_cast<std::size_t>(n) == kPfPoly4Notes.size())
            ++switchBlocksFull;
        if (sw >= worstStart && sw < worstStart + kPfBlock)
            worstIsSwitch = true;
    }

    // H-13: mean block cost inside the Character crossfade window, % of a core per voice.
    const double e1PeriodS = 1.0 / static_cast<double>(midiNoteToFrequency(pfPt::kVoiceEngineNoteE1));
    const auto windowSamples = static_cast<std::size_t>(
        std::llround((e1PeriodS + BassCharacterStage::kSwitchCrossfadeMs / 1000.0) * kPfFs));
    std::vector<bool> inWindow(med64.size(), false);
    for (const std::uint64_t s0 : charSwitchSamples) {
        for (std::size_t b = 0; b < med64.size(); ++b) {
            const std::size_t bs = warmSamples + b * kPfBlock;
            const std::size_t be = bs + kPfBlock;
            if (be > s0 && bs < s0 + windowSamples)
                inWindow[b] = true;
        }
    }
    double windowMs = 0.0;
    std::size_t windowBlocks = 0;
    for (std::size_t b = 0; b < med64.size(); ++b) {
        if (inWindow[b]) {
            windowMs += med64[b];
            ++windowBlocks;
        }
    }
    const double voices = static_cast<double>(kPfPoly4Notes.size());
    const double windowPctPerVoice =
        windowBlocks > 0 ? 100.0 * (windowMs / static_cast<double>(windowBlocks)) / kPfBlockBudgetMs / voices : 0.0;
    const double meanPctPerVoice = 100.0 * meanMs / kPfBlockBudgetMs / voices;

    const bool pass = worstRatio <= 1.0;
    os << "# checksum " << sink << " (keeps the renders observable)\n";
    os << "SC003 BLOCK worst_median_ms=" << *worstIt << " (block " << worstBlock << ", sample " << worstStart
       << ") ratio=" << worstRatio << " raw_max_ms=" << rawMax << " raw_ratio=" << rawMax / kPfBlockBudgetMs
       << " mean_ms=" << meanMs << " budget_ms=" << kPfBlockBudgetMs << '\n';
    os << "SC003 RECORD worst 32-sample interval " << worstIntervalPct << " % of a core (Phase 8: "
       << kPfPhase8IntervalPct << " %, fixture overhead included; recorded, not gated)\n";
    os << "SC003 COINCIDENCE switches=" << switches << " all_five_together=" << coincident
       << " anchorPosition==position=" << anchorAligned << '\n';
    os << "SC003 COVERAGE switch blocks: AmpCurve issued on min " << curveVoiceCount << " voices over "
       << charSwitchSamples.size() << " switches (" << switchBlocksFull << " blocks with all 4); worst timed block "
       << (worstIsSwitch ? "is" : "is not") << " a switch block (its AmpCurve mask 0x" << std::hex
       << static_cast<unsigned>(curveVoices) << std::dec << ", recorded)\n";
    os << "H13 RECORD crossfade window (switch + one E1 period + " << BassCharacterStage::kSwitchCrossfadeMs
       << " ms) blocks=" << windowBlocks << " cost=" << windowPctPerVoice << " % of a core per voice (all-block mean "
       << meanPctPerVoice << " %; Phase 5 crossfade cell " << kPfPhase5CrossfadePct << " %)\n";
    os << "SC003 VERDICT " << (pass ? "GREEN" : "RED") << ' ' << worstRatio << " (bar 1.0)\n";
    std::cout << os.str();
    pfWriteEvidence("sc003-worst-interval.txt", os.str());

    CHECK(pfFinite(sink));
    CHECK(switches > 0);
    CHECK(coincident == switches);
    CHECK(anchorAligned == switches);
    CHECK(!charSwitchSamples.empty());
    CHECK(static_cast<std::size_t>(curveVoiceCount) >= kPfPoly4Notes.size());
    CHECK(worstRatio <= 1.0);
}

// =============================================================================
// Phase 14 (specs/profundum-phase14-morph-modulation) T027: SC-014 (b), (d); FR-053
// =============================================================================
// VoiceEngine_WorstIntervalMorphMod: VoiceEngine_WorstInterval's method (the SC-003 pair, 4 voices held E1 / A1 /
// E2 / C3, host block 64, ramp 5 ms, the base position toggled 0 <-> 1 every 100 ms, 1 s warm-up + 10 s reported,
// 3 renders, per-block median across the renders with the raw maximum printed beside it; gate
// max(block wall / 1.333 ms) <= 1.0; the worst 32-sample interval recorded) with the SC-014 Morph Modulation
// running: Source Cycle, Shape Sine, 20 Hz, Sync Off, Amount 1. A 4th, untimed render with an observer proves the
// modulation ran: a non-zero offset at some grid point, a target strictly inside (0, 1) handed to the morph (the
// toggle alone hands only 0 and 1), and selection switches. Then SC-014 (d): ProfundumMorphModulator::advanceInterval
// timed over kPfMmAdvanceCalls calls per source, ns per call printed (recorded, not gated). Readings go to stdout only
// (Phase 9 evidence is never written by this phase; the main loop quotes them into EVID/run-log/T035.txt).
// =============================================================================

namespace {

constexpr float kPfMmCycleHz = 20.0f;
constexpr float kPfMmAmount = 1.0f;
constexpr std::size_t kPfMmAdvanceCalls = 1000000;
constexpr std::size_t kPfMmNumSources = 5;
constexpr std::array<ProfundumMorphModulator::Source, kPfMmNumSources> kPfMmSources{
    ProfundumMorphModulator::Source::Off, ProfundumMorphModulator::Source::Envelope,
    ProfundumMorphModulator::Source::Cycle, ProfundumMorphModulator::Source::Velocity,
    ProfundumMorphModulator::Source::ModWheel};
constexpr std::array<const char*, kPfMmNumSources> kPfMmSourceNames{"Off", "Envelope", "Cycle", "Velocity",
                                                                    "ModWheel"};

/// The SC-014 modulation: Cycle, Sine, 20 Hz, free (Sync Off), Amount 1; the envelope fields stay at their defaults.
[[nodiscard]] ProfundumMorphModulator::Config pfMorphModCycleConfig() noexcept {
    ProfundumMorphModulator::Config c{};
    c.source = ProfundumMorphModulator::Source::Cycle;
    c.amount = kPfMmAmount;
    c.cycleShape = Krate::DSP::Waveform::Sine;
    c.cycleRateHz = kPfMmCycleHz;
    c.cycleSync = false;
    return c;
}

/// Non-vacuity observer of the untimed render: counters only, the callbacks never allocate.
class PfMorphModObserver final : public ProfundumEngineObserver {
public:
    explicit PfMorphModObserver(const MorphState& b) : b_(b) {}

    void onGrid(const ProfundumGridRecord& g) noexcept override {
        ++grids_;
        if (g.morphModOffset != 0.0f)
            ++offsetNonZero_;
        if (g.morphTarget > 0.0f && g.morphTarget < 1.0f)
            ++interiorTargets_;
        if (g.resolved != nullptr) {
            const std::uint8_t m = pfSelectionMask(g.resolved->state, b_);
            if (haveMask_ && m != mask_)
                ++switches_;
            mask_ = m;
            haveMask_ = true;
        }
    }

    [[nodiscard]] std::size_t grids() const noexcept { return grids_; }
    [[nodiscard]] std::size_t offsetNonZero() const noexcept { return offsetNonZero_; }
    [[nodiscard]] std::size_t interiorTargets() const noexcept { return interiorTargets_; }
    [[nodiscard]] std::size_t switches() const noexcept { return switches_; }

private:
    MorphState b_;
    std::size_t grids_ = 0;
    std::size_t offsetNonZero_ = 0;
    std::size_t interiorTargets_ = 0;
    std::size_t switches_ = 0;
    std::uint8_t mask_ = 0;
    bool haveMask_ = false;
};

/// One SC-003 render (pfSc003Render's protocol) with the SC-014 modulation set after prepare. With @p blockMs
/// non-null, every reported block's wall time (ms) is written there; with @p observer non-null it is attached.
void pfMorphModRender(std::size_t hostBlock, std::vector<double>* blockMs, ProfundumEngineObserver* observer,
                      double& sink) {
    const std::pair<MorphState, MorphState> ends = pfSc003Endpoints();
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, ends.first));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, ends.second));
    e->setAnchorMorphMode(BassMorphEngine::AnchorMorphMode::Blend);
    e->setMorphPosition(0.0f);
    e->setMorphRampMs(kPfSc003RampMs);
    e->setVoiceMode(ProfundumEngine::VoiceMode::Poly);
    e->setPolyphony(kPfPoly4Notes.size());
    if (observer != nullptr)
        e->setObserver(observer);
    e->prepare(kPfFs, ProfundumEngineConfig{});
    e->setMorphModulation(pfMorphModCycleConfig());
    REQUIRE(e->morphModulation().source == ProfundumMorphModulator::Source::Cycle);
    REQUIRE(e->morphModulation().amount == kPfMmAmount);
    REQUIRE_FALSE(e->morphModBypass());
    for (const std::uint8_t n : kPfPoly4Notes)
        e->noteOn(n, kPfVelocity);

    std::vector<float> l(hostBlock, 0.0f);
    std::vector<float> r(hostBlock, 0.0f);
    const auto toggle = static_cast<std::size_t>(std::llround(kPfSc003ToggleS * kPfFs));
    const auto warm = static_cast<std::size_t>(std::llround(kPfWarmupS * kPfFs));
    const std::size_t total = warm + static_cast<std::size_t>(std::llround(kPfTimedS * kPfFs));
    std::size_t nextToggle = toggle;
    float position = 0.0f;
    double acc = 0.0;
    if (blockMs != nullptr) {
        blockMs->clear();
        blockMs->reserve(total / hostBlock + 1);
    }
    for (std::size_t pos = 0; pos < total; pos += hostBlock) {
        if (pos >= nextToggle) {
            position = 1.0f - position;
            e->setMorphPosition(position);
            nextToggle += toggle;
        }
        const auto t0 = std::chrono::steady_clock::now();
        e->processBlock(l.data(), r.data(), hostBlock);
        const auto t1 = std::chrono::steady_clock::now();
        acc += static_cast<double>(l[0]) + static_cast<double>(r[hostBlock - 1]);
        if (blockMs != nullptr && pos >= warm)
            blockMs->push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    if (observer != nullptr)
        e->setObserver(nullptr);
    sink += acc;
}

}  // namespace

TEST_CASE("VoiceEngine_WorstIntervalMorphMod",
          "[systems][profundum][morphmod][voice-engine][performance][.perf]") {
    enableFTZDAZ();
    std::ostringstream os;
    os << std::setprecision(5);
    os << "# SC-014 (b) VoiceEngine_WorstIntervalMorphMod (Phase 14 tasks.md T027; SC-003 method + modulation)\n"
       << "# 4 voices held E1 / A1 / E2 / C3, block " << kPfBlock << ", ramp " << kPfSc003RampMs
       << " ms, base position toggled 0 <-> 1 every " << kPfSc003ToggleS
       << " s; the SC-003 five-group pair, Anchor morph Blend; Morph Mod Cycle Sine " << kPfMmCycleHz
       << " Hz, Sync Off, Amount " << kPfMmAmount << "; " << kPfTimedS << " s reported after " << kPfWarmupS
       << " s warm-up; " << kPfTrials << " renders, per-block median across renders\n";
    double sink = 0.0;

    // ---- timed: host block 64 ----
    std::vector<std::vector<double>> renders64(kPfTrials);
    for (std::vector<double>& v : renders64)
        pfMorphModRender(kPfBlock, &v, nullptr, sink);
    const std::vector<double> med64 = pfPerBlockMedian(renders64);
    REQUIRE(!med64.empty());
    const auto worstIt = std::max_element(med64.begin(), med64.end());
    const std::size_t worstBlock = static_cast<std::size_t>(std::distance(med64.begin(), worstIt));
    const double worstRatio = *worstIt / kPfBlockBudgetMs;
    double rawMax = 0.0;
    for (const std::vector<double>& v : renders64)
        rawMax = std::max(rawMax, *std::max_element(v.begin(), v.end()));
    double meanMs = 0.0;
    for (const double x : med64)
        meanMs += x;
    meanMs /= static_cast<double>(med64.size());

    // ---- timed: host block 32 (one block == one grid interval), recorded ----
    std::vector<std::vector<double>> renders32(kPfTrials);
    for (std::vector<double>& v : renders32)
        pfMorphModRender(kPfGrid, &v, nullptr, sink);
    const std::vector<double> med32 = pfPerBlockMedian(renders32);
    REQUIRE(!med32.empty());
    const double worstIntervalPct = 100.0 * *std::max_element(med32.begin(), med32.end()) / kPfIntervalBudgetMs;

    // ---- untimed observer run (non-vacuity: the modulation ran and the groups switched) ----
    const std::pair<MorphState, MorphState> ends = pfSc003Endpoints();
    auto observer = std::make_unique<PfMorphModObserver>(ends.second);
    pfMorphModRender(kPfBlock, nullptr, observer.get(), sink);

    // ---- SC-014 (d): ProfundumMorphModulator::advanceInterval, ns per call, per source ----
    std::array<double, kPfMmNumSources> nsPerCall{};
    for (std::size_t i = 0; i < kPfMmNumSources; ++i) {
        auto m = std::make_unique<ProfundumMorphModulator>();
        m->prepare(kPfFs);
        ProfundumMorphModulator::Config c = pfMorphModCycleConfig();
        c.source = kPfMmSources[i];
        m->setConfig(c);
        m->setModWheel(0.5f);
        m->noteOn(kPfVelocity);
        double acc = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t k = 0; k < kPfMmAdvanceCalls; ++k)
            acc += static_cast<double>(m->advanceInterval());
        const auto t1 = std::chrono::steady_clock::now();
        nsPerCall[i] =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(kPfMmAdvanceCalls);
        sink += acc;
    }

    const bool pass = worstRatio <= 1.0;
    os << "# checksum " << sink << " (keeps the renders observable)\n";
    os << "SC014B BLOCK worst_median_ms=" << *worstIt << " (block " << worstBlock << ") ratio=" << worstRatio
       << " raw_max_ms=" << rawMax << " raw_ratio=" << rawMax / kPfBlockBudgetMs << " mean_ms=" << meanMs
       << " budget_ms=" << kPfBlockBudgetMs << '\n';
    os << "SC014B RECORD worst 32-sample interval " << worstIntervalPct << " % of a core (Phase 8: "
       << kPfPhase8IntervalPct << " %, fixture overhead included; recorded, not gated)\n";
    os << "SC014B NONVACUITY grids=" << observer->grids() << " offset_nonzero=" << observer->offsetNonZero()
       << " interior_targets=" << observer->interiorTargets() << " selection_switches=" << observer->switches()
       << '\n';
    for (std::size_t i = 0; i < kPfMmNumSources; ++i)
        os << "SC014D advanceInterval source=" << kPfMmSourceNames[i] << " ns_per_call=" << nsPerCall[i] << " ("
           << kPfMmAdvanceCalls << " calls; recorded, not gated)\n";
    os << "SC014B VERDICT " << (pass ? "GREEN" : "RED") << ' ' << worstRatio << " (bar 1.0)\n";
    std::cout << os.str();

    CHECK(Krate::DSP::detail::isFinite(sink));
    CHECK(observer->grids() > 0);
    CHECK(observer->offsetNonZero() > 0);
    CHECK(observer->interiorTargets() > 0);
    CHECK(observer->switches() > 0);
    for (const double ns : nsPerCall) {
        CHECK(Krate::DSP::detail::isFinite(ns));
        CHECK(ns > 0.0);
    }
    CHECK(worstRatio <= 1.0);
}

// =============================================================================
// SC-004
// =============================================================================

TEST_CASE("VoiceEngine_PlacementCost", "[systems][profundum][voice-engine][sc-004][performance][.perf]") {
    enableFTZDAZ();
    std::ostringstream os;
    pfHeader(os, "SC-004 VoiceEngine_PlacementCost (4 voices E1 / A1 / E2 / C3, SC-002 protocol)");
    double sink = 0.0;

    std::array<double, 2> perVoice{};
    std::array<double, 2> postSum{};
    std::array<double, 2> followOff{};
    for (std::size_t ai = 0; ai < kPfAlgorithms.size(); ++ai) {
        const BassCharacterStage::Algorithm alg = kPfAlgorithms[ai];
        const std::string an = pfAlgName(alg);
        perVoice[ai] = pfMedianTrial(pfPolyRun(alg, kPfPoly4Notes, ProfundumNonlinearPlacement::PerVoice), os,
                                     "PERVOICE4 " + an, sink);
        postSum[ai] = pfMedianTrial(pfPolyRun(alg, kPfPoly4Notes, ProfundumNonlinearPlacement::PostSum), os,
                                    "POSTSUM4 " + an, sink);
        followOff[ai] = pfMedianTrial(
            pfPolyRun(alg, kPfPoly4Notes, ProfundumNonlinearPlacement::PerVoice, PfLever::Follow), os,
            "FOLLOWOFF4 " + an, sink);
        os << "SC004 " << an << " pervoice=" << perVoice[ai] << " postsum=" << postSum[ai]
           << " follow_on=" << perVoice[ai] << " follow_off=" << followOff[ai]
           << " follow_cost=" << (perVoice[ai] - followOff[ai]) << '\n';
    }

    const double pv = std::max(perVoice[0], perVoice[1]);
    const double ps = std::max(postSum[0], postSum[1]);
    const double fOff = std::max(followOff[0], followOff[1]);
    const bool adoptPostSum = pv > kPfPolyBarPct && ps <= kPfPolyBarPct;
    const bool followBreaches = pv > kPfPolyBarPct && fOff <= kPfPolyBarPct;
    os << "# checksum " << sink << " (keeps the renders observable)\n";
    os << "FR014 READING placement: per-voice max=" << pv << " post-sum max=" << ps << " (bar " << kPfPolyBarPct
       << " %); criterion per-voice > bar AND post-sum <= bar -> " << (adoptPostSum ? "ADOPT POSTSUM" : "KEEP PERVOICE")
       << '\n';
    os << "FR014 READING follow: follow-on (per voice) max=" << pv << " follow-off max=" << fOff << " cost="
       << (pv - fOff) << "; criterion follow-on > bar AND follow-off <= bar -> "
       << (followBreaches ? "FOLLOW ALONE BREACHES" : "KEEP FOLLOW PER VOICE") << '\n';
    std::cout << os.str();
    pfWriteEvidence("placement.txt", os.str());
    CHECK(pfFinite(sink));
    CHECK(pfFinite(pv));
    CHECK(pfFinite(ps));
    CHECK(pfFinite(fOff));
}
