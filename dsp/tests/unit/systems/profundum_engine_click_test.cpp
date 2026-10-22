// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_click_test.cpp
// ==============================================================================
// tasks.md T015: clicks. SC-005 (voice steal), SC-006 (MONO note replacement), SC-025 (MotionConfig selectors);
// FR-016, FR-009 (the ampStep record), plan S1 C-4, S9.9, S10.3. Each [long] case has a non-long ...Smoke
// sibling with the same assertions and the same bars on one small cell (tasks.md deviation 5).
//
// Readings (spec SC-005: "All readings are on the pre-dynamics sum; the output is recorded"): every gate reads the
// engine's Sum tap (VoiceEngineTaps::sumL/R), the worse of L / R; the Output tap's ratio is written beside it.
//
// Decisions (evidence/run-log/T015.txt):
//  - Reference timing (spec SC-005 reading refinement, 2026-10-20): the reference's new note is issued at the grid
//    point of the switch sample when the cell's render declicked, else at the event's own grid point. "Declicked"
//    is read from the render itself: a voice that sounded at full g_d just before the event (g_d == 1) whose g_d
//    drops below 1 in [event, event + samplesUntilSwitch + L_d). The positive controls use the timing of the cell
//    they disable (AlwaysRetrigger / Reset / held: a declicked VoiceReuse, plan C-4).
//  - (b) bound: plan C-4 / S9.9 S_max = pi / (2 L_d) for every cell (C-4 names no other gain slope); the SC-005
//    cells add 1.1 D_max of each held filler (the P-2 form, triangle inequality over the voices sounding in W);
//    the glide cells of SC-006 add the steady reference one semitone above the higher note (Phase 4 SC-010(c),
//    voiceEngineGlideStepKeyHz).
//  - Image modes (SC-005 shards): the shard's ImageMode is set on both endpoints; Spread / Wide also set
//    field.spread = 1 (A_s carries spread 0, which would make Spread inert). SC-006 runs each base's own field
//    (A_s SOLID, B WIDE).
//  - SC-006 glide axis: {portamento 0, ConstantTime / ConstantRate / Quantized at 50 ms}; GlideMode is inert at
//    portamento 0, so one porta-0 slice stands for the three. Portamento mode Always (the default).
//  - CSVs are written per TEST_CASE (evidence/<name>/<case>.csv), so concurrently run shards never share a file.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "../processors/shape_character_metric_helpers.h"
#include "bass_morph_state_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <enable_ftz_daz.h>

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>
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
#include <iostream>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace vePt = Krate::DSP::ProfundumTest;
namespace veSc = Krate::DSP::ShapeCharacterTest;

using Krate::DSP::BassStereoField;
using Krate::DSP::GlideMode;
using Krate::DSP::MassLaw;
using Krate::DSP::MorphState;
using Krate::DSP::NoteMemory;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;
using Krate::DSP::ProfundumEngine;
using Krate::DSP::ProfundumVoice;
using Krate::DSP::RetriggerDeclick;

using VeKind = vePt::VoiceEngineEventKind;
using VeImage = BassStereoField::ImageMode;
using VeMotion = MorphState::MotionConfig;

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

constexpr double kVeFs = 48000.0;
constexpr std::size_t kVeGrid = ProfundumEngine::kControlInterval;
constexpr double kVeRatioBar = 1.0592537251772889;   // 10^(0.5 / 20): SC-005(a), SC-006(a), SC-025
constexpr double kVeEventAtS = 0.5;                  // the steal / replacement (P-2's instant)
constexpr double kVeTailS = 0.3;
constexpr double kVeSteadyFromS = 0.5;               // S9.9 steady references: [0.5 s, 1.0 s) of a separate render
constexpr double kVeSteadyToS = 1.0;
constexpr double kVeReleaseLeadS = 0.02;             // releasing victim: note-off 20 ms before the steal
constexpr std::array<int, 3> kVeFillerOffsetsSt{5, 7, 10};
constexpr std::array<double, 3> kVeFillerOnS{0.05, 0.10, 0.15};
constexpr int kVeStealOffsetSt = 3;                  // the fifth note, above the victim
constexpr std::array<int, 3> kVeIntervalsSt{1, 12, 24};
constexpr std::array<std::size_t, 3> kVeBlocks{1, 37, 512};
constexpr std::size_t kVeReplaceBlock = 37;
constexpr float kVeGlideMs = 50.0f;
constexpr float kVeMorphRampMs = 5.0f;               // = BassMorphEngine::kMinPositionRampMs
constexpr std::size_t kVeAmpStepSpan = 64;           // FR-009 record: 64 samples after each receiver change
constexpr std::size_t kVeClickContext = 4096;        // ClickDetector context before W (same for both renders)
constexpr double kVeSelectorSwitchS = 1.0;
constexpr double kVeSelectorTailS = 0.3;
constexpr int kVeLowestNote = 16;                    // E0
constexpr int kVeHighestNote = 127;

constexpr std::array<NoteMemory::Mode, 4> kVeMemModes{NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Mode::Legato,
                                                      NoteMemory::Mode::EnvelopeRetriggerOnly,
                                                      NoteMemory::Mode::OscillatorContinuous};
constexpr std::array<NoteMemory::Retrigger, 2> kVeRetriggers{NoteMemory::Retrigger::Reset,
                                                             NoteMemory::Retrigger::Continue};

enum class VeGlide : std::uint8_t { Off, ConstantTime, ConstantRate, Quantized };
constexpr std::array<VeGlide, 4> kVeGlides{VeGlide::Off, VeGlide::ConstantTime, VeGlide::ConstantRate,
                                           VeGlide::Quantized};

// -----------------------------------------------------------------------------
// Small utilities
// -----------------------------------------------------------------------------

[[nodiscard]] std::string veEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

/// evidence/<folder>/<caseName>.csv (created outside any AllocationScope; tasks.md rule 11).
[[nodiscard]] std::ofstream veOpenCsv(const std::string& folder, const std::string& caseName) {
    const std::filesystem::path dir = std::filesystem::path(veEvidenceDir()) / folder;
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / (caseName + ".csv"));
    out << std::setprecision(6);
    return out;
}

/// A grid-aligned sample index (events land on grid points, so latched and applied coincide, plan C-9).
[[nodiscard]] std::uint64_t veAt(double seconds) noexcept {
    const auto n = static_cast<std::uint64_t>(std::llround(seconds * kVeFs));
    return (n / kVeGrid) * kVeGrid;
}

[[nodiscard]] double veNoteHz(int note) noexcept {
    return static_cast<double>(Krate::DSP::midiNoteToFrequency(note));
}

[[nodiscard]] std::string veNoteName(int n) {
    switch (n) {
        case vePt::kVoiceEngineNoteE1: return "E1";
        case vePt::kVoiceEngineNoteC3: return "C3";
        default: break;
    }
    return "n" + std::to_string(n);
}

[[nodiscard]] const char* veMemName(NoteMemory::Mode m) noexcept {
    switch (m) {
        case NoteMemory::Mode::AlwaysRetrigger: return "AlwaysRetrigger";
        case NoteMemory::Mode::Legato: return "Legato";
        case NoteMemory::Mode::EnvelopeRetriggerOnly: return "EnvelopeRetriggerOnly";
        case NoteMemory::Mode::OscillatorContinuous: return "OscillatorContinuous";
    }
    return "?";
}

[[nodiscard]] const char* veRetrigName(NoteMemory::Retrigger r) noexcept {
    return r == NoteMemory::Retrigger::Reset ? "Reset" : "Continue";
}

[[nodiscard]] const char* veGlideName(VeGlide g) noexcept {
    switch (g) {
        case VeGlide::Off: return "Porta0";
        case VeGlide::ConstantTime: return "ConstantTime";
        case VeGlide::ConstantRate: return "ConstantRate";
        case VeGlide::Quantized: return "Quantized";
    }
    return "?";
}

[[nodiscard]] const char* veImageName(VeImage m) noexcept {
    switch (m) {
        case VeImage::Solid: return "Solid";
        case VeImage::Spread: return "Spread";
        case VeImage::Wide: return "Wide";
    }
    return "?";
}

/// Plan S1 C-8: showcase A with ampEnv.sustain = 1 (A_s).
[[nodiscard]] MorphState veStateAs() {
    return vePt::voiceEngineAs(Krate::DSP::BassMorphTest::showcasePair().first);
}

/// Showcase B as harvested (SC-005's base "B").
[[nodiscard]] MorphState veStateB() {
    return Krate::DSP::BassMorphTest::showcasePair().second;
}

/// The SC-005 shard's image mode on a base: Spread / Wide also get spread 1 (A_s carries spread 0).
[[nodiscard]] MorphState veWithImage(MorphState s, VeImage mode) {
    s.field.mode = mode;
    if (mode != VeImage::Solid)
        s.field.spread = 1.0f;
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

/// A base, named for cell ids and the steady cache.
struct VeBase {
    std::string name;
    MorphState state{};
};

[[nodiscard]] vePt::VoiceEngineEvent veOn(std::uint64_t at, int note) {
    return vePt::VoiceEngineEvent{.sample = at, .kind = VeKind::NoteOn, .note = static_cast<std::uint8_t>(note)};
}

[[nodiscard]] vePt::VoiceEngineEvent veOff(std::uint64_t at, int note) {
    return vePt::VoiceEngineEvent{.sample = at, .kind = VeKind::NoteOff, .note = static_cast<std::uint8_t>(note)};
}

[[nodiscard]] vePt::VoiceEngineEvent veSetIndex(VeKind kind, int index, std::uint64_t at = 0) {
    return vePt::VoiceEngineEvent{.sample = at, .kind = kind, .index = index};
}

[[nodiscard]] vePt::VoiceEngineEvent veSetValue(VeKind kind, float value, std::uint64_t at = 0) {
    return vePt::VoiceEngineEvent{.sample = at, .kind = kind, .value = value};
}

/// One state on both endpoints, Note Memory mode and Retrigger set at sample 0.
[[nodiscard]] vePt::VoiceEngineScript veScript(const MorphState& s, ProfundumEngine::VoiceMode mode,
                                               std::size_t polyphony, std::uint64_t total, std::size_t block,
                                               std::size_t tapped, NoteMemory::Mode mem,
                                               NoteMemory::Retrigger retrig) {
    vePt::VoiceEngineScript sc;
    sc.fs = kVeFs;
    sc.hostBlock = block;
    sc.totalSamples = total;
    sc.a = s;
    sc.b = s;
    sc.mode = mode;
    sc.polyphony = polyphony;
    sc.tappedVoices = tapped;
    sc.events = {veSetIndex(VeKind::NoteMemoryMode, static_cast<int>(mem)),
                 veSetIndex(VeKind::NoteRetrigger, static_cast<int>(retrig))};
    return sc;
}

// -----------------------------------------------------------------------------
// Click machinery around the T009 helpers (plan S9.9)
// -----------------------------------------------------------------------------

using VeSteadyCache = std::map<std::string, vePt::VoiceEngineSteady>;

/// S9.9: the steady unit-gain reference of one note on one base (a separate MONO render of the same settings, never
/// the transition trace): [0.5 s, 1.0 s) of voice 0's Character + Anchor taps.
[[nodiscard]] vePt::VoiceEngineSteady veSteady(const VeBase& base, int note, VeSteadyCache& cache) {
    const std::string key = base.name + ":" + std::to_string(note);
    const auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    vePt::VoiceEngineScript sc = veScript(base.state, ProfundumEngine::VoiceMode::Mono, 1, veAt(kVeSteadyToS), 64, 1,
                                          NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
    sc.events.push_back(veOn(0, note));
    const vePt::VoiceEngineTaps t = vePt::voiceEngineRender(sc);
    // G14-5 SC-006(b) (user ruling 2026-10-21): D_max / P_max read from note-on (sample 0), so the bound covers
    // what the same note does when it starts cold (a fresh C5 on B overshoots its after-settle D_max by ~5 % in
    // its first 10 ms through Fold / Character settling); the after-settle reading is kept under ":settled" and
    // recorded beside the bound.
    const vePt::VoiceEngineSteady s = vePt::voiceEngineUnitGainSteady(t.voices[0], 0, t.total);
    const vePt::VoiceEngineSteady settled =
        vePt::voiceEngineUnitGainSteady(t.voices[0], static_cast<std::size_t>(veAt(kVeSteadyFromS)), t.total);
    cache.emplace(key, s);
    cache.emplace(key + ":settled", settled);
    return s;
}

/// The after-settle steady reference (the pre-G14-5 D_max), for the record column.
[[nodiscard]] vePt::VoiceEngineSteady veSteadySettled(const VeBase& base, int note, VeSteadyCache& cache) {
    (void)veSteady(base, note, cache);
    return cache.at(base.name + ":" + std::to_string(note) + ":settled");
}

/// samplesUntilSwitch of the voice's dip (ruling R-B: ProfundumVoice::kDeclickMs) from a fresh arm.
[[nodiscard]] std::size_t veSamplesUntilSwitch() {
    RetriggerDeclick dk;
    dk.prepare(kVeFs, ProfundumVoice::kDeclickMs);
    dk.arm();
    return dk.samplesUntilSwitch();
}

/// The switch sample's grid point, relative to the restart's grid point (rounded up to the grid, plan C-9).
[[nodiscard]] std::uint64_t veSwitchDelay() {
    const auto d = static_cast<std::uint64_t>(veSamplesUntilSwitch());
    return (d + kVeGrid - 1) / kVeGrid * kVeGrid;
}

/// W (SC-005): the event's grid point to the dip's end (samplesUntilSwitch + L_d), plus one period of the slower f0.
[[nodiscard]] vePt::VoiceEngineWindow veWindow(std::uint64_t at, double fOld, double fNew) {
    const std::size_t fadeEnd =
        static_cast<std::size_t>(at) + veSamplesUntilSwitch() + vePt::voiceEngineDeclickLength(kVeFs);
    return vePt::voiceEngineWindow(static_cast<std::size_t>(at), fadeEnd, fOld, fNew, kVeFs);
}

/// True when a voice that sounded at full g_d before `at` dips in [at, at + samplesUntilSwitch + L_d).
[[nodiscard]] bool veDeclicked(const vePt::VoiceEngineTaps& t, std::uint64_t at) {
    const auto a = static_cast<std::size_t>(at);
    REQUIRE(a > 0);
    const std::size_t end = std::min(t.total, a + veSamplesUntilSwitch() + vePt::voiceEngineDeclickLength(kVeFs));
    for (std::size_t v = 0; v < t.tappedVoices; ++v) {
        const std::vector<float>& g = t.voices[v].gD;
        if (g.size() < end || std::bit_cast<std::uint32_t>(g[a - 1]) != std::bit_cast<std::uint32_t>(1.0f))
            continue;
        for (std::size_t n = a; n < end; ++n) {
            if (g[n] < 1.0f)
                return true;
        }
    }
    return false;
}

[[nodiscard]] double veMaxDelta(const std::vector<float>& l, const std::vector<float>& r, vePt::VoiceEngineWindow w) {
    return std::max(vePt::voiceEngineMaxDelta(std::span<const float>(l), w),
                    vePt::voiceEngineMaxDelta(std::span<const float>(r), w));
}

/// SC-005(a) on one tap pair, the worse channel.
[[nodiscard]] double veRatio(const std::vector<float>& yL, const std::vector<float>& yR, const std::vector<float>& rL,
                             const std::vector<float>& rR, vePt::VoiceEngineWindow w) {
    return std::max(
        vePt::voiceEngineDeltaRatio(std::span<const float>(yL), std::span<const float>(rL), w),
        vePt::voiceEngineDeltaRatio(std::span<const float>(yR), std::span<const float>(rR), w));
}

/// The slice the differential click count reads: kVeClickContext samples before W to W's end.
struct VeSlice {
    std::size_t lo = 0;
    std::size_t n = 0;
};

[[nodiscard]] VeSlice veClickSlice(vePt::VoiceEngineWindow w, std::size_t total) {
    const std::size_t lo = w.begin > kVeClickContext ? w.begin - kVeClickContext : 0;
    const std::size_t hi = std::min(w.end, total);
    REQUIRE(hi > lo);
    return VeSlice{.lo = lo, .n = hi - lo};
}

/// ShapeCharacterTest::differentialClicks summed over L and R (refEnd may be nullptr vectors: one reference).
struct VeClicks {
    std::size_t counted = 0;
    std::size_t transition = 0;
    std::size_t reference = 0;
};

[[nodiscard]] VeClicks veClicks(const std::vector<float>& yL, const std::vector<float>& yR,
                                const std::vector<float>& sL, const std::vector<float>& sR,
                                const std::vector<float>* eL, const std::vector<float>* eR, VeSlice s, double f0) {
    VeClicks out;
    const auto one = [&](const std::vector<float>& y, const std::vector<float>& rs, const std::vector<float>* re) {
        REQUIRE(y.size() >= s.lo + s.n);
        REQUIRE(rs.size() >= s.lo + s.n);
        const float* end = nullptr;
        if (re != nullptr) {
            REQUIRE(re->size() >= s.lo + s.n);
            end = re->data() + s.lo;
        }
        const veSc::ClickCount c =
            veSc::differentialClicks(y.data() + s.lo, rs.data() + s.lo, end, s.n, kVeFs, f0);
        out.counted += c.counted;
        out.transition += c.transitionDetections;
        out.reference += c.referenceDetections;
    };
    one(yL, sL, eL);
    one(yR, sR, eR);
    return out;
}

/// One SC-005 / SC-006 cell's readings.
struct VeReading {
    std::string id;
    double ratio = 0.0;       // (a) Sum tap
    bool aGated = true;
    double maxDelta = 0.0;    // (b)
    double bound = 0.0;
    double outRatio = 0.0;    // the Output tap's (a) ratio, recorded
    double refMaxDelta = 0.0;  // the reference's max |dy| over W (G14-5 record)
    double boundSettled = 0.0; // the bound with the after-settle D_max (pre-G14-5 form, recorded)
    double ratioL = 0.0, ratioR = 0.0;   // (a) per channel (probe record)
    VeClicks clicks{};        // (c), recorded
    bool declicked = false;
    std::uint64_t refDelay = 0;
    std::size_t wBegin = 0;
    std::size_t wEnd = 0;
    std::uint64_t dropped = 0;
    [[nodiscard]] bool aPass() const noexcept { return ratio <= kVeRatioBar; }
    [[nodiscard]] bool bPass() const noexcept { return maxDelta <= bound; }
    [[nodiscard]] double margin() const noexcept { return maxDelta > 0.0 ? bound / maxDelta : 0.0; }
};

void veCsvHeader(std::ostream& csv) {
    csv << "cell,ratio_a,a_gated,a_pass,max_dy,bound,margin_b,b_pass,clicks_c,clicks_transition,clicks_reference,"
           "declicked,ref_delay,w_begin,w_end,out_ratio,ampStep,ampStep_ref,bound_settled\n";
}

void veCsvRow(std::ostream& csv, const VeReading& r, const std::string& ampStep = "n/a",
              const std::string& ampStepRef = "n/a") {
    csv << r.id << ',' << r.ratio << ',' << (r.aGated ? 1 : 0) << ',' << (r.aPass() ? 1 : 0) << ',' << r.maxDelta
        << ',' << r.bound << ',' << r.margin() << ',' << (r.bPass() ? 1 : 0) << ',' << r.clicks.counted << ','
        << r.clicks.transition << ',' << r.clicks.reference << ',' << (r.declicked ? 1 : 0) << ',' << r.refDelay << ','
        << r.wBegin << ',' << r.wEnd << ',' << r.outRatio << ',' << ampStep << ',' << ampStepRef << ','
        << r.boundSettled << '\n';
}

/// The gate of one ordinary cell: (a) where gated, (b) always; nothing dropped.
void veGateCell(const VeReading& r) {
    INFO(r.id << " ratio " << r.ratio << " (bar " << kVeRatioBar << ") max_dy " << r.maxDelta << " bound " << r.bound
              << " W [" << r.wBegin << ", " << r.wEnd << ") declicked " << r.declicked);
    CHECK(r.dropped == 0u);
    if (r.aGated)
        CHECK(r.aPass());
    CHECK(r.bPass());
}

/// Reference timing: Detect reads the render; AtSwitch is the declicked cell's timing (the positive controls).
enum class VeRefTiming : std::uint8_t { Detect, AtSwitch };

// -----------------------------------------------------------------------------
// SC-005: voice steal (plan C-4: the steal "fade" is the Note Memory declick)
// -----------------------------------------------------------------------------

struct VeStealCell {
    const VeBase* base = nullptr;
    int victim = vePt::kVoiceEngineNoteE1;
    bool releasing = false;
    NoteMemory::Mode mem = NoteMemory::Mode::AlwaysRetrigger;
    NoteMemory::Retrigger retrig = NoteMemory::Retrigger::Reset;
    std::size_t block = 37;
    bool disableStealFade = false;
};

/// 5 notes into 4 voices (POLY, Oldest / Hard): the victim (the oldest) at 0, three fillers held, the fifth note at
/// 0.5 s. Reference: the same engine, seed and events at polyphony 5 (nothing stolen), the new note at the switch
/// sample's grid point when the cell declicks.
[[nodiscard]] VeReading veStealRun(const VeStealCell& c, VeSteadyCache& cache, VeRefTiming timing) {
    REQUIRE(c.base != nullptr);
    const int newNote = c.victim + kVeStealOffsetSt;
    const std::uint64_t tS = veAt(kVeEventAtS);
    const std::uint64_t total = tS + veAt(kVeTailS);
    vePt::VoiceEngineScript sc = veScript(c.base->state, ProfundumEngine::VoiceMode::Poly, 4, total, c.block, 4,
                                          c.mem, c.retrig);
    sc.config.seams.disableStealFade = c.disableStealFade;
    sc.events.push_back(veOn(0, c.victim));
    std::vector<int> fillers;
    for (std::size_t i = 0; i < kVeFillerOffsetsSt.size(); ++i) {
        const int f = c.victim + kVeFillerOffsetsSt[i];
        fillers.push_back(f);
        sc.events.push_back(veOn(veAt(kVeFillerOnS[i]), f));
    }
    if (c.releasing)
        sc.events.push_back(veOff(tS - veAt(kVeReleaseLeadS), c.victim));
    const std::size_t newIdx = sc.events.size();
    sc.events.push_back(veOn(tS, newNote));

    const vePt::VoiceEngineTaps y = vePt::voiceEngineRender(sc);

    VeReading out;
    out.declicked = timing == VeRefTiming::AtSwitch || veDeclicked(y, tS);
    out.refDelay = out.declicked ? veSwitchDelay() : std::uint64_t{0};
    vePt::VoiceEngineScript ref = sc;
    ref.polyphony = 5;
    ref.tappedVoices = 0;
    ref.config.seams = {};
    ref.events[newIdx].sample = tS + out.refDelay;
    const vePt::VoiceEngineTaps r = vePt::voiceEngineRender(ref);

    const double fOld = veNoteHz(c.victim);
    const double fNew = veNoteHz(newNote);
    const vePt::VoiceEngineWindow w = veWindow(tS, fOld, fNew);
    out.id = std::string("steal ") + c.base->name + " " + veNoteName(c.victim) + (c.releasing ? " releasing" : " held")
             + " " + veMemName(c.mem) + " " + veRetrigName(c.retrig) + " block " + std::to_string(c.block)
             + (c.disableStealFade ? " [disableStealFade]" : "");
    out.ratio = veRatio(y.sumL, y.sumR, r.sumL, r.sumR, w);
    out.outRatio = veRatio(y.outL, y.outR, r.outL, r.outR, w);
    out.maxDelta = veMaxDelta(y.sumL, y.sumR, w);
    const std::array<vePt::VoiceEngineSteady, 2> involved{veSteady(*c.base, c.victim, cache),
                                                          veSteady(*c.base, newNote, cache)};
    double fillerD = 0.0;
    for (const int f : fillers)
        fillerD += veSteady(*c.base, f, cache).dMax;
    // plan S1 C-4 / S9.9: S_max = pi / (2 L_d); P-2's filler term (triangle inequality over the voices in W).
    out.bound = vePt::voiceEngineDeltaBound(involved, vePt::voiceEngineDeclickSlope(kVeFs))
                + vePt::kVoiceEngineBoundFactor * fillerD;
    out.clicks = veClicks(y.sumL, y.sumR, r.sumL, r.sumR, nullptr, nullptr, veClickSlice(w, y.total),
                          std::max(fOld, fNew));
    out.wBegin = w.begin;
    out.wEnd = w.end;
    out.dropped = y.droppedEvents + r.droppedEvents;
    return out;
}

// -----------------------------------------------------------------------------
// SC-006: MONO note replacement
// -----------------------------------------------------------------------------

struct VeReplaceCell {
    const VeBase* base = nullptr;
    int note = vePt::kVoiceEngineNoteE1;
    int intervalSt = 12;
    bool legato = false;
    NoteMemory::Mode mem = NoteMemory::Mode::AlwaysRetrigger;
    NoteMemory::Retrigger retrig = NoteMemory::Retrigger::Reset;
    VeGlide glide = VeGlide::Off;
    std::size_t block = kVeReplaceBlock;
    bool disableDeclick = false;
};

/// The FR-009 arm's endpoint B: A_s with attack, decay, sustain and release moved (the morph at the replacement).
[[nodiscard]] MorphState veEnvelopeMoved(MorphState s) {
    s.ampEnv.attackMs = 40.0f;
    s.ampEnv.decayMs = 600.0f;
    s.ampEnv.sustain = 0.4f;
    s.ampEnv.releaseMs = 500.0f;
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

/// The MONO script of a replacement cell: the old note at 0 (held: Overlap), the new note at 0.5 s.
[[nodiscard]] vePt::VoiceEngineScript veReplaceScript(const VeReplaceCell& c, std::uint64_t total, std::size_t tapped,
                                                      std::size_t* newIdx) {
    const int target = c.note + c.intervalSt;
    const std::uint64_t tS = veAt(kVeEventAtS);
    vePt::VoiceEngineScript sc =
        veScript(c.base->state, ProfundumEngine::VoiceMode::Mono, 1, total, c.block, tapped, c.mem, c.retrig);
    sc.config.seams.disableDeclick = c.disableDeclick;
    sc.events.push_back(veSetIndex(VeKind::Legato, c.legato ? 1 : 0));
    sc.events.push_back(veSetIndex(VeKind::PortamentoMode, static_cast<int>(Krate::DSP::PortaMode::Always)));
    if (c.glide == VeGlide::Off) {
        sc.events.push_back(veSetValue(VeKind::PortamentoTimeMs, 0.0f));
    } else {
        GlideMode g = GlideMode::Quantized;
        if (c.glide == VeGlide::ConstantTime)
            g = GlideMode::ConstantTime;
        else if (c.glide == VeGlide::ConstantRate)
            g = GlideMode::ConstantRate;
        sc.events.push_back(veSetIndex(VeKind::GlideMode, static_cast<int>(g)));
        sc.events.push_back(veSetValue(VeKind::PortamentoTimeMs, kVeGlideMs));
    }
    sc.events.push_back(veOn(0, c.note));
    if (newIdx != nullptr)
        *newIdx = sc.events.size();
    sc.events.push_back(veOn(tS, target));
    return sc;
}

/// SC-006 cell. Reference: a POLY-2 render of the same events (the new note on an idle voice), the new note at the
/// switch sample's grid point when the cell declicks. (a) gated only at portamento 0 (recorded otherwise); (b) with
/// the glide term (the steady reference one semitone above the higher note) where a glide runs.
[[nodiscard]] VeReading veReplaceRun(const VeReplaceCell& c, VeSteadyCache& cache, VeRefTiming timing,
                                     vePt::VoiceEngineTaps* keep = nullptr) {
    REQUIRE(c.base != nullptr);
    const int target = c.note + c.intervalSt;
    const std::uint64_t tS = veAt(kVeEventAtS);
    const std::uint64_t total = tS + veAt(kVeTailS);
    std::size_t newIdx = 0;
    const vePt::VoiceEngineScript sc = veReplaceScript(c, total, 1, &newIdx);
    vePt::VoiceEngineTaps y = vePt::voiceEngineRender(sc);

    VeReading out;
    out.declicked = timing == VeRefTiming::AtSwitch || veDeclicked(y, tS);
    out.refDelay = out.declicked ? veSwitchDelay() : std::uint64_t{0};
    vePt::VoiceEngineScript ref = sc;
    ref.mode = ProfundumEngine::VoiceMode::Poly;
    ref.polyphony = 2;
    ref.tappedVoices = 0;
    ref.config.seams = {};
    ref.events[newIdx].sample = tS + out.refDelay;
    const vePt::VoiceEngineTaps r = vePt::voiceEngineRender(ref);

    const double fOld = veNoteHz(c.note);
    const double fNew = veNoteHz(target);
    const vePt::VoiceEngineWindow w = veWindow(tS, fOld, fNew);
    out.id = std::string("replace ") + c.base->name + " " + veNoteName(c.note) + " " + (c.intervalSt > 0 ? "+" : "")
             + std::to_string(c.intervalSt) + " st " + (c.legato ? "legato" : "retrigger") + " " + veMemName(c.mem)
             + " " + veRetrigName(c.retrig) + " " + veGlideName(c.glide) + " block " + std::to_string(c.block)
             + (c.disableDeclick ? " [disableDeclick]" : "");
    // spec SC-006: (a) recorded where portamento > 0; G14-5 v2 (user ruling 2026-10-21): also recorded for a jump of
    // >= 24 st on a WIDE base when no dip is armed (legato, or a Note Memory policy that suppresses the dip —
    // EnvelopeRetriggerOnly, OscillatorContinuous, Legato), where the Character stage's warm state on the +0.15
    // channel reads 1.068 against the cold-start reference (0.957 in SPREAD, 0.78 in SOLID); (b) gates those cells.
    // The dip is detected on the render (out.declicked), so an AlwaysRetrigger replacement stays gated.
    const bool wideJumpNoDip = !out.declicked && std::abs(c.intervalSt) >= 24
                               && c.base->state.field.mode == VeImage::Wide;
    out.aGated = c.glide == VeGlide::Off && !wideJumpNoDip;
    out.ratio = veRatio(y.sumL, y.sumR, r.sumL, r.sumR, w);
    out.ratioL = vePt::voiceEngineDeltaRatio(std::span<const float>(y.sumL), std::span<const float>(r.sumL), w);
    out.ratioR = vePt::voiceEngineDeltaRatio(std::span<const float>(y.sumR), std::span<const float>(r.sumR), w);
    out.outRatio = veRatio(y.outL, y.outR, r.outL, r.outR, w);
    out.maxDelta = veMaxDelta(y.sumL, y.sumR, w);
    out.refMaxDelta = veMaxDelta(r.sumL, r.sumR, w);
    std::vector<vePt::VoiceEngineSteady> involved{veSteady(*c.base, c.note, cache), veSteady(*c.base, target, cache)};
    if (c.glide != VeGlide::Off) {
        // Phase 4 SC-010(c) (voiceEngineGlideStepKeyHz): the one-semitone step on a phase-continuous oscillator is
        // covered by the steady D at the upper step's frequency.
        involved.push_back(veSteady(*c.base, std::max(c.note, target) + 1, cache));
    }
    out.bound = vePt::voiceEngineDeltaBound(involved, vePt::voiceEngineDeclickSlope(kVeFs));   // plan S1 C-4, S9.9
    {
        std::vector<vePt::VoiceEngineSteady> settled{veSteadySettled(*c.base, c.note, cache),
                                                     veSteadySettled(*c.base, target, cache)};
        if (c.glide != VeGlide::Off)
            settled.push_back(veSteadySettled(*c.base, std::max(c.note, target) + 1, cache));
        out.boundSettled = vePt::voiceEngineDeltaBound(settled, vePt::voiceEngineDeclickSlope(kVeFs));
    }
    out.clicks = veClicks(y.sumL, y.sumR, r.sumL, r.sumR, nullptr, nullptr, veClickSlice(w, y.total),
                          std::max(fOld, fNew));
    out.wBegin = w.begin;
    out.wEnd = w.end;
    out.dropped = y.droppedEvents + r.droppedEvents;
    if (keep != nullptr)
        *keep = std::move(y);
    return out;
}

/// FR-009 record (spec FR-009 "immediate receivers"; recorded, not gated): the same cell with the morph (5 ms ramp,
/// at the replacement) moving attack, decay, sustain and release. ampStep = max |g_amp[n] - g_amp[n-1]| over the
/// 64 samples after each grid point where a time / sustain receiver changed; ampStep_ref the same windows of the
/// no-morph render `plain` (voice 0, tapped).
struct VeAmpStep {
    double ampStep = 0.0;
    double ampStepRef = 0.0;
    std::size_t changedGridPoints = 0;
};

[[nodiscard]] VeAmpStep veAmpStepArm(const VeReplaceCell& c, const vePt::VoiceEngineTaps& plain) {
    const std::uint64_t tS = veAt(kVeEventAtS);
    const std::uint64_t total = tS + veAt(kVeTailS);
    vePt::VoiceEngineScript sc = veReplaceScript(c, total, 1, nullptr);
    sc.b = veEnvelopeMoved(c.base->state);
    sc.events.push_back(veSetValue(VeKind::MorphRampMs, kVeMorphRampMs));
    sc.events.push_back(veSetValue(VeKind::MorphPosition, 1.0f, tS));
    const vePt::VoiceEngineTaps m = vePt::voiceEngineRender(sc);

    const auto stepOver = [](const std::vector<float>& g, std::size_t from, std::size_t to) {
        double s = 0.0;
        for (std::size_t n = std::max<std::size_t>(from, 1); n < std::min(to, g.size()); ++n)
            s = std::max(s, std::abs(static_cast<double>(g[n]) - static_cast<double>(g[n - 1])));
        return s;
    };
    const auto same = [](float a, float b) {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    };
    VeAmpStep out;
    REQUIRE(plain.tappedVoices >= 1);
    for (std::size_t k = 1; k < m.grid.size(); ++k) {
        const MorphState::AmpEnvelopeFields& p = m.grid[k - 1].afterMotion.ampEnv;
        const MorphState::AmpEnvelopeFields& q = m.grid[k].afterMotion.ampEnv;
        if (same(p.attackMs, q.attackMs) && same(p.decayMs, q.decayMs) && same(p.sustain, q.sustain)
            && same(p.releaseMs, q.releaseMs))
            continue;
        ++out.changedGridPoints;
        const auto s0 = static_cast<std::size_t>(m.grid[k].sample);
        out.ampStep = std::max(out.ampStep, stepOver(m.voices[0].gAmp, s0, s0 + kVeAmpStepSpan));
        out.ampStepRef = std::max(out.ampStepRef, stepOver(plain.voices[0].gAmp, s0, s0 + kVeAmpStepSpan));
    }
    return out;
}

[[nodiscard]] std::string veNum(double x) {
    std::ostringstream s;
    s << std::setprecision(6) << x;
    return s.str();
}

// -----------------------------------------------------------------------------
// Positive controls (spec SC-005 / SC-006; plan C-4): each disables its own path and must fail (a) or (b)
// -----------------------------------------------------------------------------

/// At E1, A_s (in `image`), held, AlwaysRetrigger, Reset, host block 1: a steal under disableStealFade and a MONO
/// Reset restart (+3 st) under disableDeclick. The reference timing is the declicked cell's (C-4).
void veRunPositiveControls(VeImage image, std::ostream& csv, VeSteadyCache& cache) {
    const VeBase base{.name = std::string("A_s+sustain1(C-8)/") + veImageName(image),
                      .state = veWithImage(veStateAs(), image)};
    const VeReading steal = veStealRun(VeStealCell{.base = &base,
                                                   .victim = vePt::kVoiceEngineNoteE1,
                                                   .releasing = false,
                                                   .mem = NoteMemory::Mode::AlwaysRetrigger,
                                                   .retrig = NoteMemory::Retrigger::Reset,
                                                   .block = 1,
                                                   .disableStealFade = true},
                                       cache, VeRefTiming::AtSwitch);
    const VeReading restart = veReplaceRun(VeReplaceCell{.base = &base,
                                                         .note = vePt::kVoiceEngineNoteE1,
                                                         .intervalSt = kVeStealOffsetSt,
                                                         .legato = false,
                                                         .mem = NoteMemory::Mode::AlwaysRetrigger,
                                                         .retrig = NoteMemory::Retrigger::Reset,
                                                         .glide = VeGlide::Off,
                                                         .block = 1,
                                                         .disableDeclick = true},
                                           cache, VeRefTiming::AtSwitch);
    for (const VeReading* r : {&steal, &restart}) {
        veCsvRow(csv, *r);
        std::cout << "POSITIVE-CONTROL " << r->id << " ratio " << r->ratio << " max_dy " << r->maxDelta << " bound "
                  << r->bound << " -> " << ((!r->aPass() || !r->bPass()) ? "fails (a) or (b): OK" : "PASSES: DEFECT")
                  << "\n";
        INFO("positive control " << r->id << " ratio " << r->ratio << " max_dy " << r->maxDelta << " bound "
                                 << r->bound);
        CHECK(r->dropped == 0u);
        CHECK((!r->aPass() || !r->bPass()));
    }
}

[[nodiscard]] double veSeconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/// Worst readings of a grid, printed at its end.
struct VeSummary {
    std::size_t cells = 0;
    double worstRatio = 0.0;
    std::string worstRatioCell;
    double minMargin = 1.0e300;
    std::string minMarginCell;

    void add(const VeReading& r) {
        ++cells;
        if (r.aGated && r.ratio > worstRatio) {
            worstRatio = r.ratio;
            worstRatioCell = r.id;
        }
        if (r.margin() < minMargin) {
            minMargin = r.margin();
            minMarginCell = r.id;
        }
    }

    void print(const std::string& label, double seconds) const {
        std::cout << std::setprecision(6) << label << ": " << cells << " cells, worst gated (a) ratio " << worstRatio
                  << " (" << worstRatioCell << "; bar " << kVeRatioBar << "), min (b) margin " << minMargin << " ("
                  << minMarginCell << "), wall clock " << seconds << " s\n";
    }
};

// -----------------------------------------------------------------------------
// SC-005 grid
// -----------------------------------------------------------------------------

struct VeStealGrid {
    VeImage image = VeImage::Solid;
    std::vector<bool> useB;            // false: A_s, true: B
    std::vector<int> notes;
    std::vector<bool> releasing;
    std::vector<NoteMemory::Mode> mems;
    std::vector<NoteMemory::Retrigger> retrigs;
    std::vector<std::size_t> blocks;
};

void veRunStealGrid(const std::string& caseName, const VeStealGrid& g) {
    ::enableFTZDAZ();
    const auto t0 = std::chrono::steady_clock::now();
    std::ofstream csv = veOpenCsv("steal-clicks", caseName);
    veCsvHeader(csv);
    VeSteadyCache cache;
    const VeBase as{.name = std::string("A_s+sustain1(C-8)/") + veImageName(g.image),
                    .state = veWithImage(veStateAs(), g.image)};
    const VeBase b{.name = std::string("B/") + veImageName(g.image), .state = veWithImage(veStateB(), g.image)};
    VeSummary sum;
    for (const bool useB : g.useB) {
        for (const int note : g.notes) {
            for (const bool rel : g.releasing) {
                for (const NoteMemory::Mode mem : g.mems) {
                    for (const NoteMemory::Retrigger rt : g.retrigs) {
                        for (const std::size_t block : g.blocks) {
                            const VeReading r = veStealRun(VeStealCell{.base = useB ? &b : &as,
                                                                       .victim = note,
                                                                       .releasing = rel,
                                                                       .mem = mem,
                                                                       .retrig = rt,
                                                                       .block = block,
                                                                       .disableStealFade = false},
                                                           cache, VeRefTiming::Detect);
                            veCsvRow(csv, r);
                            veGateCell(r);
                            sum.add(r);
                        }
                    }
                }
            }
        }
    }
    veRunPositiveControls(g.image, csv, cache);
    CHECK(csv.good());
    sum.print("SC-005 " + caseName, veSeconds(t0));
}

[[nodiscard]] VeStealGrid veStealFullGrid(VeImage image) {
    return VeStealGrid{.image = image,
                       .useB = {false, true},
                       .notes = {vePt::kVoiceEngineNoteE1, vePt::kVoiceEngineNoteC3},
                       .releasing = {false, true},
                       .mems = {kVeMemModes.begin(), kVeMemModes.end()},
                       .retrigs = {kVeRetriggers.begin(), kVeRetriggers.end()},
                       .blocks = {kVeBlocks.begin(), kVeBlocks.end()}};
}

// -----------------------------------------------------------------------------
// SC-006 grid
// -----------------------------------------------------------------------------

struct VeReplaceGrid {
    bool legato = false;
    std::vector<bool> useB;
    std::vector<int> notes;
    std::vector<int> intervals;   // signed
    std::vector<NoteMemory::Mode> mems;
    std::vector<NoteMemory::Retrigger> retrigs;
    std::vector<VeGlide> glides;
    std::size_t block = kVeReplaceBlock;
};

void veRunReplaceGrid(const std::string& caseName, const VeReplaceGrid& g) {
    ::enableFTZDAZ();
    const auto t0 = std::chrono::steady_clock::now();
    std::ofstream csv = veOpenCsv("mono-replace-clicks", caseName);
    veCsvHeader(csv);
    VeSteadyCache cache;
    const VeBase as{.name = "A_s+sustain1(C-8)", .state = veStateAs()};
    const VeBase b{.name = "B", .state = veStateB()};
    VeSummary sum;
    for (const NoteMemory::Retrigger rt : g.retrigs) {
        for (const NoteMemory::Mode mem : g.mems) {
            for (const VeGlide glide : g.glides) {
                for (const bool useB : g.useB) {
                    for (const int note : g.notes) {
                        for (const int k : g.intervals) {
                            const int target = note + k;
                            if (target < kVeLowestNote || target + 1 > kVeHighestNote) {
                                csv << "skipped " << veNoteName(note) << ' ' << k << " st: target " << target
                                    << " outside [" << kVeLowestNote << ", " << kVeHighestNote << "]\n";
                                continue;
                            }
                            const VeReading r = veReplaceRun(VeReplaceCell{.base = useB ? &b : &as,
                                                                           .note = note,
                                                                           .intervalSt = k,
                                                                           .legato = g.legato,
                                                                           .mem = mem,
                                                                           .retrig = rt,
                                                                           .glide = glide,
                                                                           .block = g.block,
                                                                           .disableDeclick = false},
                                                             cache, VeRefTiming::Detect);
                            veCsvRow(csv, r);
                            veGateCell(r);
                            sum.add(r);
                        }
                    }
                }
                // FR-009 record: one arm per matrix slice (E1, +12 st, A_s), beside the no-morph run's reading.
                const VeReplaceCell arm{.base = &as,
                                        .note = vePt::kVoiceEngineNoteE1,
                                        .intervalSt = 12,
                                        .legato = g.legato,
                                        .mem = mem,
                                        .retrig = rt,
                                        .glide = glide,
                                        .block = g.block,
                                        .disableDeclick = false};
                vePt::VoiceEngineTaps plain;
                VeReading armReading = veReplaceRun(arm, cache, VeRefTiming::Detect, &plain);
                const VeAmpStep st = veAmpStepArm(arm, plain);
                armReading.id = "FR-009 arm (morph 5 ms at the replacement moves attack/decay/sustain/release) "
                                + armReading.id;
                veCsvRow(csv, armReading, veNum(st.ampStep), veNum(st.ampStepRef));
                std::cout << "FR-009 " << armReading.id << " ampStep " << st.ampStep << " ampStep_ref "
                          << st.ampStepRef << " over " << st.changedGridPoints << " changed grid points\n";
                CHECK(st.changedGridPoints > 0u);   // the arm's premise: the morph reached the receivers
            }
        }
    }
    veRunPositiveControls(VeImage::Solid, csv, cache);
    CHECK(csv.good());
    sum.print("SC-006 " + caseName, veSeconds(t0));
}

[[nodiscard]] VeReplaceGrid veReplaceFullGrid(bool legato) {
    std::vector<int> intervals;
    for (const int k : kVeIntervalsSt) {
        intervals.push_back(k);
        intervals.push_back(-k);
    }
    return VeReplaceGrid{.legato = legato,
                         .useB = {false, true},
                         .notes = {vePt::kVoiceEngineNoteE1, vePt::kVoiceEngineNoteC3},
                         .intervals = intervals,
                         .mems = {kVeMemModes.begin(), kVeMemModes.end()},
                         .retrigs = {kVeRetriggers.begin(), kVeRetriggers.end()},
                         .glides = {kVeGlides.begin(), kVeGlides.end()},
                         .block = kVeReplaceBlock};
}

// -----------------------------------------------------------------------------
// SC-025: MotionConfig selectors (Phase 8 handoff 9.8; the Phase 8 SC-004(a)/(b) form, ruled B10-1)
// -----------------------------------------------------------------------------

struct VeSelectorPair {
    std::string name;
    std::string fromKey;   // static-render cache keys
    std::string toKey;
    MorphState from{};
    MorphState to{};
};

/// A_s, Motion CYCLE -> Mass, amount 1, depth 1 (the pairs vary one selector from here).
[[nodiscard]] MorphState veMotionBase() {
    MorphState s = veStateAs();
    s.motion.source = VeMotion::Source::Cycle;
    s.motion.target = VeMotion::Target::Mass;
    s.motion.amount = 1.0f;
    s.motion.depth = 1.0f;
    return s;
}

template <typename T, typename Set>
void veAddPairs(std::vector<VeSelectorPair>& out, const std::string& field, const MorphState& base,
                const std::vector<std::pair<T, std::string>>& values, Set set) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            if (i == j)
                continue;
            VeSelectorPair p;
            p.name = field + " " + values[i].second + "->" + values[j].second;
            p.fromKey = field + ":" + values[i].second;
            p.toKey = field + ":" + values[j].second;
            p.from = base;
            set(p.from, values[i].first);
            p.to = base;
            set(p.to, values[j].first);
            REQUIRE(Krate::DSP::isValidMorphState(p.from));
            REQUIRE(Krate::DSP::isValidMorphState(p.to));
            out.push_back(std::move(p));
        }
    }
}

/// Every ordered pair of source, target, cycleShape, polarity, cycleSync, cycleRetrigger, and the Phase 8 SC-004
/// representative cycleNote x cycleModifier subset ({SixtyFourth, Quarter, QuadrupleWhole} at None; every modifier
/// at Quarter; specs/profundum-phase8-bass-morph/evidence/phase-handoffs.txt 9.8), the cycle subset with cycleSync on.
[[nodiscard]] std::vector<VeSelectorPair> veSelectorPairs() {
    using Krate::DSP::Waveform;
    std::vector<VeSelectorPair> out;
    const MorphState base = veMotionBase();
    veAddPairs<VeMotion::Source>(out, "source", base,
                                 {{VeMotion::Source::Off, "Off"},
                                  {VeMotion::Source::Env, "Env"},
                                  {VeMotion::Source::Cycle, "Cycle"},
                                  {VeMotion::Source::Drift, "Drift"}},
                                 [](MorphState& s, VeMotion::Source v) { s.motion.source = v; });
    veAddPairs<VeMotion::Target>(out, "target", base,
                                 {{VeMotion::Target::Mass, "Mass"},
                                  {VeMotion::Target::Depth, "Depth"},
                                  {VeMotion::Target::Body, "Body"},
                                  {VeMotion::Target::Edge, "Edge"},
                                  {VeMotion::Target::Shift, "Shift"}},
                                 [](MorphState& s, VeMotion::Target v) { s.motion.target = v; });
    veAddPairs<Waveform>(out, "cycleShape", base,
                         {{Waveform::Sine, "Sine"},
                          {Waveform::Triangle, "Triangle"},
                          {Waveform::Sawtooth, "Sawtooth"},
                          {Waveform::Square, "Square"},
                          {Waveform::SampleHold, "SampleHold"},
                          {Waveform::SmoothRandom, "SmoothRandom"}},
                         [](MorphState& s, Waveform v) { s.motion.cycleShape = v; });
    veAddPairs<VeMotion::Polarity>(out, "polarity", base,
                                   {{VeMotion::Polarity::Bipolar, "Bipolar"},
                                    {VeMotion::Polarity::Unipolar, "Unipolar"}},
                                   [](MorphState& s, VeMotion::Polarity v) { s.motion.polarity = v; });
    veAddPairs<bool>(out, "cycleSync", base, {{false, "off"}, {true, "on"}},
                     [](MorphState& s, bool v) { s.motion.cycleSync = v; });
    veAddPairs<bool>(out, "cycleRetrigger", base, {{false, "off"}, {true, "on"}},
                     [](MorphState& s, bool v) { s.motion.cycleRetrigger = v; });
    MorphState synced = base;
    synced.motion.cycleSync = true;
    synced.motion.cycleModifier = NoteModifier::None;
    veAddPairs<NoteValue>(out, "cycleNote", synced,
                          {{NoteValue::SixtyFourth, "SixtyFourth"},
                           {NoteValue::Quarter, "Quarter"},
                           {NoteValue::QuadrupleWhole, "QuadrupleWhole"}},
                          [](MorphState& s, NoteValue v) { s.motion.cycleNote = v; });
    synced.motion.cycleNote = NoteValue::Quarter;
    veAddPairs<NoteModifier>(out, "cycleModifier", synced,
                             {{NoteModifier::None, "None"},
                              {NoteModifier::Dotted, "Dotted"},
                              {NoteModifier::Triplet, "Triplet"}},
                             [](MorphState& s, NoteModifier v) { s.motion.cycleModifier = v; });
    return out;
}

/// Held E1, MONO, block 64, the morph ramp at 5 ms from sample 0.
/// The selector fields of MotionConfig (the stepped ones SC-025 switches), equal or not.
[[nodiscard]] bool veSelectorsEqual(const MorphState::MotionConfig& a, const MorphState::MotionConfig& b) noexcept {
    return a.source == b.source && a.target == b.target && a.cycleShape == b.cycleShape && a.polarity == b.polarity
           && a.cycleSync == b.cycleSync && a.cycleRetrigger == b.cycleRetrigger && a.cycleNote == b.cycleNote
           && a.cycleModifier == b.cycleModifier;
}

[[nodiscard]] vePt::VoiceEngineScript veSelectorScript(const MorphState& a, const MorphState& b, std::uint64_t total) {
    vePt::VoiceEngineScript sc = veScript(a, ProfundumEngine::VoiceMode::Mono, 1, total, 64, 0,
                                          NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
    sc.b = b;
    sc.events.push_back(veSetValue(VeKind::MorphRampMs, kVeMorphRampMs));
    sc.events.push_back(veOn(0, vePt::kVoiceEngineNoteE1));
    return sc;
}

struct VeSumTap {
    std::vector<float> l;
    std::vector<float> r;
};

void veRunSelectorPairs(const std::string& caseName, const std::vector<std::string>& only) {
    ::enableFTZDAZ();
    const auto t0 = std::chrono::steady_clock::now();
    std::ofstream csv = veOpenCsv("motion-selector-clicks", caseName);
    csv << "pair,ratio,ratio_pass,clicks_morph,clicks_direct,clicks_pass,morph_detections,direct_detections,"
           "static_detections,out_ratio,w_begin,w_end\n";
    const std::uint64_t tSw = veAt(kVeSelectorSwitchS);
    const std::uint64_t total = tSw + veAt(kVeSelectorTailS);
    const double f0 = veNoteHz(vePt::kVoiceEngineNoteE1);
    // Transition window: from the switch's grid point through the ramp, MassLaw's full 0 -> 100 slew
    // (kMassSweepMs) and one period of f0; the morph and the direct-setter reference are read over the same window.
    const auto rampSamples = static_cast<std::size_t>(std::ceil(static_cast<double>(kVeMorphRampMs) * kVeFs / 1000.0));
    const auto slewSamples =
        static_cast<std::size_t>(std::ceil(static_cast<double>(MassLaw::kMassSweepMs) * kVeFs / 1000.0));
    const vePt::VoiceEngineWindow w =
        vePt::voiceEngineWindow(static_cast<std::size_t>(tSw), static_cast<std::size_t>(tSw) + rampSamples + slewSamples,
                                f0, f0, kVeFs);
    const VeSlice slice = veClickSlice(w, static_cast<std::size_t>(total));

    std::map<std::string, VeSumTap> statics;
    const auto staticOf = [&](const std::string& key, const MorphState& s) -> const VeSumTap& {
        auto it = statics.find(key);
        if (it == statics.end()) {
            const vePt::VoiceEngineTaps t = vePt::voiceEngineRender(veSelectorScript(s, s, total));
            CHECK(t.droppedEvents == 0u);
            it = statics.emplace(key, VeSumTap{.l = t.sumL, .r = t.sumR}).first;
        }
        return it->second;
    };

    std::size_t pairs = 0;
    double worst = 0.0;
    std::string worstPair;
    for (const VeSelectorPair& p : veSelectorPairs()) {
        if (!only.empty() && std::find(only.begin(), only.end(), p.name) == only.end())
            continue;
        ++pairs;
        vePt::VoiceEngineScript morph = veSelectorScript(p.from, p.to, total);
        morph.events.push_back(veSetValue(VeKind::MorphPosition, 1.0f, tSw));
        const vePt::VoiceEngineTaps y = vePt::voiceEngineRender(morph);
        // G14-6 (main loop 2026-10-21): the morph's stepped selector switches where the position ramp crosses its
        // midpoint (3 grid steps after tSw at the 5 ms ramp), so the direct-setter reference is issued at that grid
        // point, "the same grid point" of the spec row. Issued at tSw it switched 96 samples earlier and, for a rate
        // switch, left the LFO in a different phase (ratios 1.28 .. 1.34 that were timing, not clicks).
        std::uint64_t sSw = tSw;
        for (const vePt::VoiceEngineGridRecord& g : y.grid) {
            if (g.sample >= tSw && !veSelectorsEqual(g.resolved.state.motion, p.from.motion)) {
                sSw = g.sample;
                break;
            }
        }
        vePt::VoiceEngineScript direct = veSelectorScript(p.from, p.to, total);
        direct.endpointStates = {p.to};
        direct.events.push_back(veSetIndex(VeKind::Endpoint, 0, sSw));   // setEndpoint(A, new value), p held at 0
        const vePt::VoiceEngineTaps r = vePt::voiceEngineRender(direct);
        INFO("switch grid: morph " << sSw << " (tSw + " << (sSw - tSw) << ")");
        const VeSumTap& sA = staticOf(p.fromKey, p.from);
        const VeSumTap& sB = staticOf(p.toKey, p.to);

        const double ratio = veRatio(y.sumL, y.sumR, r.sumL, r.sumR, w);
        const double outRatio = veRatio(y.outL, y.outR, r.outL, r.outR, w);
        const VeClicks cm = veClicks(y.sumL, y.sumR, sA.l, sA.r, &sB.l, &sB.r, slice, f0);
        const VeClicks cd = veClicks(r.sumL, r.sumR, sA.l, sA.r, &sB.l, &sB.r, slice, f0);
        csv << p.name << ',' << ratio << ',' << (ratio <= kVeRatioBar ? 1 : 0) << ',' << cm.counted << ','
            << cd.counted << ',' << (cm.counted <= cd.counted ? 1 : 0) << ',' << cm.transition << ','
            << cd.transition << ',' << cm.reference << ',' << outRatio << ',' << w.begin << ',' << w.end << '\n';
        INFO("SC-025 " << p.name << " ratio " << ratio << " (bar " << kVeRatioBar << ") clicks morph " << cm.counted
                       << " direct " << cd.counted);
        CHECK(y.droppedEvents == 0u);
        CHECK(r.droppedEvents == 0u);
        CHECK(ratio <= kVeRatioBar);
        CHECK(cm.counted <= cd.counted);
        if (ratio > worst) {
            worst = ratio;
            worstPair = p.name;
        }
    }
    CHECK(pairs == (only.empty() ? std::size_t{80} : only.size()));   // 12 + 20 + 30 + 2 + 2 + 2 + 6 + 6
    CHECK(csv.good());
    std::cout << std::setprecision(6) << "SC-025 " << caseName << ": " << pairs << " pairs, worst ratio " << worst
              << " (" << worstPair << "; bar " << kVeRatioBar << "), W [" << w.begin << ", " << w.end
              << "), wall clock " << veSeconds(t0) << " s\n";
}

}  // namespace

// =============================================================================
// SC-005 - voice steal clickless (FR-016; plan S1 C-4, S9.9). Sharded per image mode.
// =============================================================================

// SC-005: victim {held, releasing} x Note Memory mode x Retrigger x host block {1, 37, 512} x base {A_s, B} x
// {E1, C3}, SOLID; positive controls (disableStealFade steal, disableDeclick restart) must fail (a) or (b).
TEST_CASE("VoiceEngine_StealClickless_Solid", "[systems][profundum][voice-engine][sc-005][fr-016][long]") {
    veRunStealGrid("VoiceEngine_StealClickless_Solid", veStealFullGrid(VeImage::Solid));
}

// SC-005: the same grid in SPREAD (spread 1).
TEST_CASE("VoiceEngine_StealClickless_Spread", "[systems][profundum][voice-engine][sc-005][fr-016][long]") {
    veRunStealGrid("VoiceEngine_StealClickless_Spread", veStealFullGrid(VeImage::Spread));
}

// SC-005: the same grid in WIDE (spread 1).
TEST_CASE("VoiceEngine_StealClickless_Wide", "[systems][profundum][voice-engine][sc-005][fr-016][long]") {
    veRunStealGrid("VoiceEngine_StealClickless_Wide", veStealFullGrid(VeImage::Wide));
}

// SC-005 smoke (tasks.md deviation 5): SOLID, E1, A_s, held, AlwaysRetrigger, Reset, block 37, plus the two
// positive controls; the same assertions and bars as the shards.
TEST_CASE("VoiceEngine_StealClicklessSmoke", "[systems][profundum][voice-engine][sc-005][fr-016]") {
    veRunStealGrid("VoiceEngine_StealClicklessSmoke", VeStealGrid{.image = VeImage::Solid,
                                                                  .useB = {false},
                                                                  .notes = {vePt::kVoiceEngineNoteE1},
                                                                  .releasing = {false},
                                                                  .mems = {NoteMemory::Mode::AlwaysRetrigger},
                                                                  .retrigs = {NoteMemory::Retrigger::Reset},
                                                                  .blocks = {37}});
}

// =============================================================================
// SC-006 - MONO note replacement clickless (FR-016; FR-009 ampStep record). Sharded per legato.
// =============================================================================

// SC-006: legato on; Retrigger x Note Memory mode x glide {porta 0, ConstantTime, ConstantRate, Quantized} x base
// {A_s, B} x {E1, C3} x {+-1, +-12, +-24} st; reference POLY-2; (a) gated at porta 0, (b) everywhere; FR-009 arm per
// slice; positive controls as SC-005.
TEST_CASE("VoiceEngine_MonoReplaceClickless_Legato", "[systems][profundum][voice-engine][sc-006][fr-016][fr-009][long]") {
    veRunReplaceGrid("VoiceEngine_MonoReplaceClickless_Legato", veReplaceFullGrid(true));
}

// SC-006: legato off (every overlapping note retriggers).
TEST_CASE("VoiceEngine_MonoReplaceClickless_Retrigger",
          "[systems][profundum][voice-engine][sc-006][fr-016][fr-009][long]") {
    veRunReplaceGrid("VoiceEngine_MonoReplaceClickless_Retrigger", veReplaceFullGrid(false));
}

// SC-006 smoke: AlwaysRetrigger / Reset / ConstantTime (portamento 0 and 50 ms), +12 st, E1, A_s, legato off, plus
// the slice's FR-009 arms and the positive controls.
TEST_CASE("VoiceEngine_MonoReplaceClicklessSmoke", "[systems][profundum][voice-engine][sc-006][fr-016][fr-009]") {
    veRunReplaceGrid("VoiceEngine_MonoReplaceClicklessSmoke",
                     VeReplaceGrid{.legato = false,
                                   .useB = {false},
                                   .notes = {vePt::kVoiceEngineNoteE1},
                                   .intervals = {12},
                                   .mems = {NoteMemory::Mode::AlwaysRetrigger},
                                   .retrigs = {NoteMemory::Retrigger::Reset},
                                   .glides = {VeGlide::Off, VeGlide::ConstantTime},
                                   .block = kVeReplaceBlock});
}

// =============================================================================
// SC-025 - MotionConfig selectors click-free (H-26; Phase 8 handoff 9.8; B10-1 form)
// =============================================================================

// SC-025: every ordered selector pair through the morph (5 ms ramp, amount 1, depth 1, held E1 on A_s): max |dy| over
// the transition window <= 10^(0.5/20) x the direct-setter reference (setEndpoint(A, new value) at g_sw, p held 0);
// differential click count (refStart = A static, refEnd = B static) <= the reference's.
TEST_CASE("VoiceEngine_MotionSelectorsClickFree", "[systems][profundum][voice-engine][sc-025][long]") {
    veRunSelectorPairs("VoiceEngine_MotionSelectorsClickFree", {});
}

// SC-025 smoke: source Off -> Cycle and target Mass -> Depth.
TEST_CASE("VoiceEngine_MotionSelectorsClickFreeSmoke", "[systems][profundum][voice-engine][sc-025]") {
    veRunSelectorPairs("VoiceEngine_MotionSelectorsClickFreeSmoke", {"source Off->Cycle", "target Mass->Depth"});
}

// =============================================================================
// Main-loop probes (hidden; Group 14, 2026-10-21). Not gates: they print.
// =============================================================================

/// G14-5: the SC-006 worst cells (replace B C3 +24 st legato, block 37) across host blocks: where in W the max
/// |dy| sits relative to the replacement instant, and g_d / g_amp there.
TEST_CASE("VoiceEngine_ReplaceBlockProbe", "[.][systems][profundum][voice-engine][probe]") {
    ::enableFTZDAZ();
    const VeBase b{.name = "B", .state = veStateB()};
    const VeBase bSolid{.name = "B/Solid", .state = veWithImage(veStateB(), VeImage::Solid)};
    const VeBase bSpread{.name = "B/Spread", .state = veWithImage(veStateB(), VeImage::Spread)};
    VeSteadyCache cache;
    const std::uint64_t tS = veAt(kVeEventAtS);
    for (const VeBase* base : {&b, &bSolid, &bSpread})
    for (const std::size_t block : {std::size_t{37}}) {
        for (const NoteMemory::Mode mem : {NoteMemory::Mode::EnvelopeRetriggerOnly, NoteMemory::Mode::AlwaysRetrigger,
                                           NoteMemory::Mode::OscillatorContinuous}) {
            VeReplaceCell c;
            c.base = base;
            c.note = vePt::kVoiceEngineNoteC3;
            c.intervalSt = 24;
            c.legato = true;
            c.mem = mem;
            c.retrig = NoteMemory::Retrigger::Reset;
            c.glide = VeGlide::Off;
            c.block = block;
            vePt::VoiceEngineTaps y;
            const VeReading r = veReplaceRun(c, cache, VeRefTiming::Detect, &y);
            std::size_t am = 0;
            double amv = 0.0;
            for (std::size_t n = std::max<std::size_t>(r.wBegin, 1); n < std::min(r.wEnd, y.sumL.size()); ++n) {
                const double d = std::max(std::abs(static_cast<double>(y.sumL[n]) - static_cast<double>(y.sumL[n - 1])),
                                          std::abs(static_cast<double>(y.sumR[n]) - static_cast<double>(y.sumR[n - 1])));
                if (d > amv) {
                    amv = d;
                    am = n;
                }
            }
            const float gD = am < y.voices[0].gD.size() ? y.voices[0].gD[am] : -1.0f;
            const float gA = am < y.voices[0].gAmp.size() ? y.voices[0].gAmp[am] : -1.0f;
            std::cout << std::setprecision(6) << "PROBE SC-006 " << r.id << ": ratio " << r.ratio << " (L " << r.ratioL
                      << " R " << r.ratioR << ") max_dy " << r.maxDelta << " ref_max_dy " << r.refMaxDelta << " bound "
                      << r.bound << " argmax " << am << " (tS "
                      << (static_cast<long long>(am) - static_cast<long long>(tS)) << ") gD " << gD << " gAmp " << gA
                      << " W [" << r.wBegin << ", " << r.wEnd << ") declicked " << r.declicked << "\n";
        }
    }
}

/// G14-6: two SC-025 pairs through the morph and through the direct setter: the grid's Motion source / output and
/// Mass around the switch, and the max |dy| sample of each path.
TEST_CASE("VoiceEngine_SelectorProbe", "[.][systems][profundum][voice-engine][probe]") {
    ::enableFTZDAZ();
    const std::uint64_t tSw = veAt(kVeSelectorSwitchS);
    const std::uint64_t total = tSw + veAt(kVeSelectorTailS);
    const double f0 = veNoteHz(vePt::kVoiceEngineNoteE1);
    const auto rampSamples = static_cast<std::size_t>(std::ceil(static_cast<double>(kVeMorphRampMs) * kVeFs / 1000.0));
    const auto slewSamples =
        static_cast<std::size_t>(std::ceil(static_cast<double>(MassLaw::kMassSweepMs) * kVeFs / 1000.0));
    const vePt::VoiceEngineWindow w =
        vePt::voiceEngineWindow(static_cast<std::size_t>(tSw), static_cast<std::size_t>(tSw) + rampSamples + slewSamples,
                                f0, f0, kVeFs);
    const auto argmax = [&](const vePt::VoiceEngineTaps& t, std::size_t& at) {
        double v = 0.0;
        at = 0;
        for (std::size_t n = std::max<std::size_t>(w.begin, 1); n < std::min(w.end, t.sumL.size()); ++n) {
            const double d = std::max(std::abs(static_cast<double>(t.sumL[n]) - static_cast<double>(t.sumL[n - 1])),
                                      std::abs(static_cast<double>(t.sumR[n]) - static_cast<double>(t.sumR[n - 1])));
            if (d > v) {
                v = d;
                at = n;
            }
        }
        return v;
    };
    for (const VeSelectorPair& p : veSelectorPairs()) {
        if (p.name != "source Off->Env" && p.name != "cycleNote SixtyFourth->Quarter" && p.name != "source Drift->Env")
            continue;
        vePt::VoiceEngineScript morph = veSelectorScript(p.from, p.to, total);
        morph.events.push_back(veSetValue(VeKind::MorphPosition, 1.0f, tSw));
        vePt::VoiceEngineScript direct = veSelectorScript(p.from, p.to, total);
        direct.endpointStates = {p.to};
        direct.events.push_back(veSetIndex(VeKind::Endpoint, 0, tSw));
        const vePt::VoiceEngineTaps y = vePt::voiceEngineRender(morph);
        const vePt::VoiceEngineTaps r = vePt::voiceEngineRender(direct);
        std::size_t ay = 0, ar = 0;
        const double vy = argmax(y, ay);
        const double vr = argmax(r, ar);
        std::cout << std::setprecision(6) << "PROBE SC-025 " << p.name << ": morph max_dy " << vy << " at tSw"
                  << (static_cast<long long>(ay) - static_cast<long long>(tSw)) << ", direct max_dy " << vr << " at tSw"
                  << (static_cast<long long>(ar) - static_cast<long long>(tSw)) << ", ratio " << (vr > 0.0 ? vy / vr : 0.0)
                  << ", W [" << w.begin << ", " << w.end << ")\n";
        std::cout << "  grid (sample-tSw: morph src/out/massIn/smoothed | direct src/out/massIn/smoothed)\n";
        for (std::size_t i = 0; i < std::min(y.grid.size(), r.grid.size()); ++i) {
            const long long rel = static_cast<long long>(y.grid[i].sample) - static_cast<long long>(tSw);
            if (rel < -4 * 32 || rel > 24 * 32)
                continue;
            std::cout << "  " << rel << ": " << y.grid[i].motionSource << " / " << y.grid[i].motionOutput << " / "
                      << y.grid[i].massIn << " / " << y.grid[i].smoothedMass << " | " << r.grid[i].motionSource << " / "
                      << r.grid[i].motionOutput << " / " << r.grid[i].massIn << " / " << r.grid[i].smoothedMass << "\n";
        }
    }
}
