#pragma once
// ==============================================================================
// Profundum Phase 13 (specs/profundum-phase13-presets-release) - profundum_audibility_readers.h
// ==============================================================================
// tasks.md T003 (FR-057, FR-021; plan §4.1). The Phase 9 audibility readers of
// profundum_engine_audibility_test.cpp, moved into namespace Krate::DSP::ProfundumAudibility (test-only, no DSP
// layer) so profundum_tests can call them on the processor path. Mechanical edits only: the namespace, `inline`
// on every namespace-scope function and constexpr (ODR: two test binaries include this header), the P-1 auPt
// alias redirected to the render shim below, and the P-10 lifts L1-L11 (evidence/move-lifts.json). The move is
// checked by specs/profundum-phase13-presets-release/evidence/move-check.js.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "bass_morph_test_helpers.h"
#include "bass_stereo_field_test_helpers.h"
#include "mass_dynamics_test_helpers.h"
#include "profundum_core_test_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <enable_ftz_daz.h>
#include <k_weighting.h>

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Krate::DSP::ProfundumAudibility::render {
using namespace Krate::DSP::ProfundumTest;   // every auPt::X except the two below resolves here, unchanged

/// Phase 13 (FR-057, plan P-1): a scoped hook that reroutes the moved readers' renders. Default: none.
using ScriptRenderFn = VoiceEngineTaps (*)(const VoiceEngineScript& script, bool twin, int twinVoice, void* ctx);
struct ScriptRenderHook {
    ScriptRenderFn fn = nullptr;
    void* ctx = nullptr;
};
[[nodiscard]] inline ScriptRenderHook& scriptRenderHook() noexcept {
    thread_local ScriptRenderHook hook{};
    return hook;
}
[[nodiscard]] inline VoiceEngineTaps voiceEngineRender(const VoiceEngineScript& s) {
    const ScriptRenderHook h = scriptRenderHook();
    return h.fn != nullptr ? h.fn(s, false, -1, h.ctx) : ProfundumTest::voiceEngineRender(s);
}
[[nodiscard]] inline VoiceEngineTaps voiceEngineRenderWithTwin(const VoiceEngineScript& s, int voice = -1) {
    const ScriptRenderHook h = scriptRenderHook();
    return h.fn != nullptr ? h.fn(s, true, voice, h.ctx) : ProfundumTest::voiceEngineRenderWithTwin(s, voice);
}
/// RAII: installs {fn, ctx} and restores the previous hook on destruction. Non-copyable.
class ScriptRenderScope {
public:
    ScriptRenderScope(ScriptRenderFn fn, void* ctx) noexcept : prev_(scriptRenderHook()) {
        scriptRenderHook() = ScriptRenderHook{fn, ctx};
    }
    ~ScriptRenderScope() { scriptRenderHook() = prev_; }
    ScriptRenderScope(const ScriptRenderScope&) = delete;
    ScriptRenderScope& operator=(const ScriptRenderScope&) = delete;
private:
    ScriptRenderHook prev_;
};
}  // namespace Krate::DSP::ProfundumAudibility::render

namespace Krate::DSP::ProfundumAudibility {

namespace auPt = Krate::DSP::ProfundumAudibility::render;
namespace auBm = Krate::DSP::BassMorphTest;
namespace auSf = Krate::DSP::StereoFieldTest;
namespace auMd = Krate::DSP::MassDynamicsTest;

using AuKind = auPt::VoiceEngineEventKind;
using AuAlg = BassCharacterStage::Algorithm;
using AuMotion = MorphState::MotionConfig;

// -----------------------------------------------------------------------------
// Constants (FR-080 bars, SC-007 eps, SC-009 protocol)
// -----------------------------------------------------------------------------

inline constexpr double kAuFs = 48000.0;
inline constexpr std::size_t kAuGrid = auPt::kVoiceEngineGrid;
inline constexpr std::uint8_t kAuVelocity = 100;
inline constexpr std::uint8_t kAuE1 = auPt::kVoiceEngineNoteE1;
inline constexpr std::uint8_t kAuE2 = auPt::kVoiceEngineNoteE2;
inline constexpr std::uint8_t kAuC3 = auPt::kVoiceEngineNoteC3;
inline constexpr std::uint8_t kAuC4 = 60;
inline constexpr std::array<std::uint8_t, 2> kAuNotes{kAuE1, kAuC3};

inline constexpr double kAuBarDb = 3.0;          // FR-080: dB descriptors
inline constexpr double kAuBarOct = 0.25;        // FR-080: centroid
inline constexpr double kAuBarRatio = 2.0;       // FR-080: time descriptors
inline constexpr double kAuBarAnchorDb = 2.0;    // FR-080: Anchor rows dE_sub
inline constexpr double kAuBarStartSt = 6.0;     // Phase 4 SC-021: start pitch
inline constexpr double kAuBarAlpha = 0.1;       // Phase 4 SC-021: alpha
inline constexpr double kAuBarRhoFrac = 0.05;    // Phase 4 SC-021: rho >= 0.05 A
inline constexpr double kAuBarCents = 5.0;       // AT-13
inline constexpr double kAuBarPhaseDeg = 30.0;   // AT-43 ... AT-46 phase reading
inline constexpr double kAuDropAmountSt = 24.0;  // AT-19 ... AT-22

inline constexpr double kAuEpsDb = 0.25;      // SC-007 (Phase 8 D-8)
inline constexpr double kAuEpsOct = 0.02;
inline constexpr double kAuEpsRatio = 0.98;   // 2 %
inline constexpr std::size_t kAuSweepSteps = 21;

inline constexpr double kAuMaxGateSeconds = 12.0;   // the Phase 8 gate render's ceiling (bass_morph_test_helpers.h:945)
inline constexpr double kAuSettleS = 1.0;           // S9.11 / SC-009 settle
inline constexpr double kAuMotionRateHz = 0.1;      // AT-34 ... AT-38
inline constexpr double kAuMotionCycles = 3.0;
inline constexpr double kAuEnvRetriggerS = 2.0;     // AT-39, SC-009 ENV record
inline constexpr double kAuEnvSeconds = 6.0;        // three retriggers after the settle
inline constexpr double kAuDriftSeconds = 30.0;
inline constexpr double kAuFieldMotionSeconds = 10.0;
inline constexpr std::size_t kAuRetriggerGapSamples = 64;   // note-off -> note-on gap of a retrigger
inline constexpr std::size_t kAuAmpStepSamples = 64;        // FR-009 record (T015's definition)

// -----------------------------------------------------------------------------
// Small utilities
// -----------------------------------------------------------------------------

/// Bit test (this TU builds with the shipping FP mode; std::isfinite may be folded away under -ffast-math).
[[nodiscard]] inline bool auFinite(double x) noexcept {
    return (std::bit_cast<std::uint64_t>(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

[[nodiscard]] inline bool auSame(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// A grid-aligned sample index at or below `seconds` (events land on grid points, C-9).
[[nodiscard]] inline std::uint64_t auAt(double seconds) noexcept {
    const auto n = static_cast<std::uint64_t>(std::llround(seconds * kAuFs));
    return (n / kAuGrid) * kAuGrid;
}

[[nodiscard]] inline double auHz(std::uint8_t note) noexcept {
    return static_cast<double>(midiNoteToFrequency(static_cast<int>(note)));
}

[[nodiscard]] inline std::string auNoteName(std::uint8_t n) {
    switch (n) {
        case kAuE1: return "E1";
        case kAuE2: return "E2";
        case kAuC3: return "C3";
        case kAuC4: return "C4";
        default: break;
    }
    return "n" + std::to_string(static_cast<int>(n));
}

/// The transition target of a note (E1 -> E2, C3 -> C4).
[[nodiscard]] inline std::uint8_t auUpOctave(std::uint8_t n) noexcept {
    return static_cast<std::uint8_t>(n + 12);
}

[[nodiscard]] inline std::string auEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

inline void auWriteEvidence(const std::string& relPath, const std::string& text) {
    const std::filesystem::path p = std::filesystem::path(auEvidenceDir()) / relPath;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << text;
    CHECK(out.good());
}

[[nodiscard]] inline double auMedian(std::vector<double> v) {
    REQUIRE(!v.empty());
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

[[nodiscard]] inline double auP2p(const std::vector<double>& v) {
    REQUIRE(!v.empty());
    const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    return *mx - *mn;
}

// -----------------------------------------------------------------------------
// Bases (FR-080, C-8, B10-1)
// -----------------------------------------------------------------------------

[[nodiscard]] inline MorphState auStateA() { return auBm::showcasePair().first; }
[[nodiscard]] inline MorphState auStateB() { return auBm::showcasePair().second; }
[[nodiscard]] inline MorphState auStateAs() { return auPt::voiceEngineAs(auStateA()); }   // C-8
[[nodiscard]] inline MorphState auStateBs() { return auPt::voiceEngineBs(auStateB()); }   // B10-1

inline constexpr const char* kAuBaseAs = "A + sustain 1 (C-8)";
inline constexpr const char* kAuBaseBs = "B + sustain 1 (B10-1)";

/// One engine setting: the two endpoints, the morph position and the macros.
struct AuSetting {
    MorphState a{};
    MorphState b{};
    float position = 0.0f;
    ProfundumMacroValues macros{};
};

[[nodiscard]] inline AuSetting auOne(const MorphState& s) {
    return AuSetting{.a = s, .b = s};
}

[[nodiscard]] inline ProfundumMacroValues auMacroAt(ProfundumMacro m, float v) {
    ProfundumMacroValues mv{};
    switch (m) {
        case ProfundumMacro::Weight: mv.weight = v; break;
        case ProfundumMacro::DarkBright: mv.darkBright = v; break;
        case ProfundumMacro::TightLoose: mv.tightLoose = v; break;
        case ProfundumMacro::CleanViolent: mv.cleanViolent = v; break;
        case ProfundumMacro::Count: break;
    }
    return mv;
}

// -----------------------------------------------------------------------------
// Scripts and renders
// -----------------------------------------------------------------------------

[[nodiscard]] inline auPt::VoiceEngineEvent auOn(std::uint64_t at, std::uint8_t note) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = AuKind::NoteOn, .note = note, .velocity = kAuVelocity};
}

[[nodiscard]] inline auPt::VoiceEngineEvent auOff(std::uint64_t at, std::uint8_t note) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = AuKind::NoteOff, .note = note};
}

[[nodiscard]] inline auPt::VoiceEngineEvent auSetIndex(std::uint64_t at, AuKind kind, int index) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = kind, .index = index};
}

[[nodiscard]] inline auPt::VoiceEngineEvent auSetValue(std::uint64_t at, AuKind kind, float value) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = kind, .value = value};
}

/// MONO, one held note from sample 0, the observer attached (summed taps; per-voice taps for [0, tapped)).
[[nodiscard]] inline auPt::VoiceEngineScript auScript(const AuSetting& s, std::uint8_t note, std::uint64_t total,
                                               std::size_t tapped = 0) {
    REQUIRE(isValidMorphState(s.a));
    REQUIRE(isValidMorphState(s.b));
    auPt::VoiceEngineScript sc;
    sc.fs = kAuFs;
    sc.hostBlock = 64;
    sc.totalSamples = total;
    sc.a = s.a;
    sc.b = s.b;
    sc.position = s.position;
    sc.macros = s.macros;
    sc.mode = ProfundumEngine::VoiceMode::Mono;
    sc.attachObserver = true;
    sc.tappedVoices = tapped;
    sc.events.push_back(auOn(0, note));
    return sc;
}

/// The steady render's length: the Phase 8 read window (1 s settle + kLowFrequencyFftSize), grid-rounded, + 1 grid.
[[nodiscard]] inline std::uint64_t auSteadyTotal() {
    const auto n = static_cast<std::uint64_t>(std::ceil(auMd::descriptorSeconds(kAuFs) * kAuFs));
    return ((n + kAuGrid - 1) / kAuGrid) * kAuGrid + kAuGrid;
}

// -----------------------------------------------------------------------------
// Steady readers (Phase 8 readers on the engine taps)
// -----------------------------------------------------------------------------

enum class AuDesc : std::uint8_t { DNl, DBr, ESub, DCx, DSt, DStLow };

[[nodiscard]] inline const char* auDescName(AuDesc d) noexcept {
    switch (d) {
        case AuDesc::DNl: return "D_nl (dB)";
        case AuDesc::DBr: return "D_br (oct)";
        case AuDesc::ESub: return "E_sub (dB)";
        case AuDesc::DCx: return "D_cx (dB)";
        case AuDesc::DSt: return "D_st (dB; band validity G8-1)";
        case AuDesc::DStLow: return "D_st below f_c (dB; side / mid in the band Gravity governs; drafted-descriptor replacement 2026-10-21)";
    }
    return "?";
}

[[nodiscard]] inline const char* auOutName(AuDesc d) noexcept {
    switch (d) {
        case AuDesc::DNl: return "L_K output (dB; D_nl has no output form)";
        case AuDesc::DBr: return "D_br output (oct)";
        case AuDesc::ESub: return "E_sub output (dB)";
        case AuDesc::DCx: return "D_cx output (dB)";
        case AuDesc::DSt: return "D_st output (dB)";
        case AuDesc::DStLow: return "D_st below f_c output (dB)";
    }
    return "?";
}

struct AuReading {
    double value = 0.0;
    bool valid = false;
    double out = 0.0;
    double lk = 0.0;   ///< L_K of the output over the read window (steady readings only; H-14 record)
};

/// D_st above 2 f_c over the Phase 8 read window, with the G8-1 validity flag.
[[nodiscard]] inline auSf::BandReading auDst(const std::vector<float>& L, const std::vector<float>& R, double fcHz) {
    const auBm::ReadWindow w = auBm::readWindow(L.size(), kAuFs);
    return auSf::bandReading(L.data() + w.start, R.data() + w.start, w.length, kAuFs, 2.0 * fcHz, 0.5 * kAuFs);
}

/// D_st below f_c (20 Hz .. f_c): the band the Gravity law governs (AT-32; the above-2-f_c reading is blind to it by
/// construction, 0.25 / 0.07 dB on A_s / B_s in the first run). Main loop 2026-10-21 (G14-8), FR-080 "Drafted
/// descriptors": same family (dB), same bar.
[[nodiscard]] inline auSf::BandReading auDstLow(const std::vector<float>& L, const std::vector<float>& R, double fcHz) {
    const auBm::ReadWindow w = auBm::readWindow(L.size(), kAuFs);
    return auSf::bandReading(L.data() + w.start, R.data() + w.start, w.length, kAuFs, 20.0, fcHz);
}

/// One steady reading of `d` on setting `s` at `note` (a held note from sample 0, the Phase 8 read window).
[[nodiscard]] inline AuReading auSteady(const AuSetting& s, std::uint8_t note, AuDesc d) {
    const double f0 = auHz(note);
    const auPt::VoiceEngineScript sc = auScript(s, note, auSteadyTotal());
    AuReading r;
    if (d == AuDesc::DNl) {
        const auPt::VoiceEngineTaps t = auPt::voiceEngineRenderWithTwin(sc);
        r.value = auBm::dNlDb(t, f0, kAuFs);
        r.out = auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), kAuFs);
        r.lk = r.out;
        r.valid = auFinite(r.value);
        return r;
    }
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(sc);
    r.lk = auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), kAuFs);
    switch (d) {
        case AuDesc::DBr:
            r.value = auBm::dBrOct(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Sum), f0, kAuFs);
            r.out = auBm::dBrOct(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), f0, kAuFs);
            r.valid = auFinite(r.value);
            break;
        case AuDesc::ESub:
            r.value = auBm::eSubDb(t.sumL, t.sumR, f0, kAuFs);
            r.out = auBm::eSubDb(t.outL, t.outR, f0, kAuFs);
            r.valid = auFinite(r.value);
            break;
        case AuDesc::DCx:
            r.value = auBm::dCxDb(t.sumL, t.sumR, f0, kAuFs);
            r.out = auBm::dCxDb(t.outL, t.outR, f0, kAuFs);
            r.valid = auFinite(r.value);
            break;
        case AuDesc::DSt: {
            const double fc = static_cast<double>(s.a.field.crossoverHz);
            const auSf::BandReading b = auDst(t.stageOutL, t.stageOutR, fc);
            r.value = b.sideToMidDb;
            r.out = auDst(t.outL, t.outR, fc).sideToMidDb;
            r.valid = b.valid && auFinite(r.value);
            break;
        }
        case AuDesc::DStLow: {
            const double fc = static_cast<double>(s.a.field.crossoverHz);
            const auSf::BandReading b = auDstLow(t.stageOutL, t.stageOutR, fc);
            r.value = b.sideToMidDb;
            r.out = auDstLow(t.outL, t.outR, fc).sideToMidDb;
            r.valid = b.valid && auFinite(r.value);
            break;
        }
        case AuDesc::DNl: break;
    }
    return r;
}

// -----------------------------------------------------------------------------
// Gate render and D_t (Phase 8 gate render on the Sum tap, as T014's harvest)
// -----------------------------------------------------------------------------

struct AuGate {
    auPt::VoiceEngineTaps taps;
    std::uint64_t off = 0;
    double seconds = 0.0;
    bool reached = false;
};

/// Note-on at 0, note-off at 1 s (grid-aligned), length note-off + 2 s extended in 1 s steps to 12 s until the
/// Sum mid falls 80 dB below its peak (bass_morph_test_helpers.h:942-966 on the Sum tap).
[[nodiscard]] inline AuGate auGateRender(const AuSetting& s, std::uint8_t note) {
    const double f0 = auHz(note);
    AuGate g;
    g.off = auAt(1.0);
    for (double sec = static_cast<double>(g.off) / kAuFs + 2.0; sec <= kAuMaxGateSeconds + 1e-9; sec += 1.0) {
        auPt::VoiceEngineScript sc = auScript(s, note, auAt(sec));
        sc.events.push_back(auOff(g.off, note));
        g.taps = auPt::voiceEngineRender(sc);
        g.seconds = sec;
        const auBm::FallOff fo =
            auBm::fallOff(auBm::periodEnergies(auBm::midOf(g.taps.sumL, g.taps.sumR), f0, kAuFs));
        if (fo.reached) {
            g.reached = true;
            return g;
        }
    }
    return g;
}

/// D_t (Phase 8's definition, bass_morph_test_helpers.h:919-930) of the mid from sample `from` on, without the
/// reader's REQUIRE: invalid when the 80 dB fall-off is not reached.
[[nodiscard]] inline AuReading auDtMs(const std::vector<float>& L, const std::vector<float>& R, double f0,
                               std::size_t from = 0) {
    const std::vector<float> mid = auBm::midOf(L, R);
    AuReading r;
    if (from >= mid.size())
        return r;
    const std::vector<float> slice(mid.begin() + static_cast<std::ptrdiff_t>(from), mid.end());
    const auBm::PeriodEnergies pe = auBm::periodEnergies(slice, f0, kAuFs);
    const auBm::FallOff fo = auBm::fallOff(pe);
    if (!fo.reached || pe.e2.empty())
        return r;
    const double peak = *std::max_element(pe.e2.begin(), pe.e2.end());
    double d = 0.0;
    for (std::size_t k = 0; k < pe.e2.size(); ++k)
        d += pe.e2[k] * pe.ms[k];
    r.value = d / peak;
    r.valid = auFinite(r.value) && r.value > 0.0;
    return r;
}

/// D_t on the gate render: the whole render (`release` false) or the release reading (from the note-off grid
/// point on, normalised by that segment's own peak period). The output-tap reading is recorded beside it.
[[nodiscard]] inline AuReading auGateDt(const AuSetting& s, std::uint8_t note, bool release) {
    const double f0 = auHz(note);
    const AuGate g = auGateRender(s, note);
    AuReading r;
    if (!g.reached)
        return r;
    const std::size_t from = release ? static_cast<std::size_t>(g.off) : 0;
    r = auDtMs(g.taps.sumL, g.taps.sumR, f0, from);
    const AuReading o = auDtMs(g.taps.outL, g.taps.outR, f0, from);
    r.out = o.valid ? o.value : -1.0;   // -1: the output did not fall 80 dB within the render
    return r;
}

// -----------------------------------------------------------------------------
// Short-window and single-bin readers (AT-11, AT-12, AT-17)
// -----------------------------------------------------------------------------

/// E_sub over [start, start + len) of the mid: the least-squares projection onto {1, cos, sin} at f0 (works for
/// windows shorter than a period), P_1 = (a^2 + b^2) / 2, in RMS dB (the eSubDb convention).
[[nodiscard]] inline double auWindowESubDb(const std::vector<float>& L, const std::vector<float>& R, std::size_t start,
                                    std::size_t len, double f0) {
    REQUIRE(len >= 8);
    REQUIRE(start + len <= L.size());
    std::array<std::array<double, 4>, 3> m{};   // [G | b]
    const double w = auPt::kTwoPi * f0 / kAuFs;
    for (std::size_t k = 0; k < len; ++k) {
        const double x = 0.5 * (static_cast<double>(L[start + k]) + static_cast<double>(R[start + k]));
        const std::array<double, 3> basis{1.0, std::cos(w * static_cast<double>(k)),
                                          std::sin(w * static_cast<double>(k))};
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j)
                m[i][j] += basis[i] * basis[j];
            m[i][3] += basis[i] * x;
        }
    }
    // Gaussian elimination with partial pivoting.
    for (std::size_t c = 0; c < 3; ++c) {
        std::size_t p = c;
        for (std::size_t r = c + 1; r < 3; ++r) {
            if (std::abs(m[r][c]) > std::abs(m[p][c]))
                p = r;
        }
        std::swap(m[c], m[p]);
        REQUIRE(std::abs(m[c][c]) > 0.0);
        for (std::size_t r = 0; r < 3; ++r) {
            if (r == c)
                continue;
            const double f = m[r][c] / m[c][c];
            for (std::size_t j = c; j < 4; ++j)
                m[r][j] -= f * m[c][j];
        }
    }
    const double a = m[1][3] / m[1][1];
    const double b = m[2][3] / m[2][2];
    return 10.0 * std::log10(std::max(0.5 * (a * a + b * b), 1e-300));
}

/// |X(hz)|^2 of a BH7-windowed single-bin DFT of x[0, len) (the goertzelPhase window, profundum_core_test_helpers.h:157).
[[nodiscard]] inline double auBinPower(const float* x, std::size_t len, double hz) {
    const std::vector<double> w = auPt::bh7Window(len);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < len; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = auPt::kTwoPi * hz * static_cast<double>(k) / kAuFs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return re * re + im * im;
}

// -----------------------------------------------------------------------------
// Frame-wise readers (S9.11)
// -----------------------------------------------------------------------------

enum class AuFrameDesc : std::uint8_t { ESub, DCx, DBr };

[[nodiscard]] inline const char* auFrameDescName(AuFrameDesc d) noexcept {
    switch (d) {
        case AuFrameDesc::ESub: return "frame-wise E_sub p2p (dB)";
        case AuFrameDesc::DCx: return "frame-wise D_cx p2p (dB)";
        case AuFrameDesc::DBr: return "frame-wise D_br p2p (oct; full-band centroid, Goertzel at n f0 over the 8-period frame, n <= min(fs / 2 f0, 128); the h1..h8 fit was blind to Edge / Shift, 2026-10-21)";
    }
    return "?";
}

/// Power at `cyclesPerSample` over x[0, n) by Goertzel (mean-square of the sinusoid, (a^2 + b^2) / 2 form).
[[nodiscard]] inline double auGoertzelPower(const float* x, std::size_t n, double cyclesPerSample) {
    const double w = 2.0 * 3.14159265358979323846 * cyclesPerSample;
    const double c = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        s0 = static_cast<double>(x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    const double mag2 = re * re + im * im;   // |X|^2
    const double nn = static_cast<double>(n);
    return 2.0 * mag2 / (nn * nn);            // (a^2 + b^2) / 2 with a, b = 2 |X| / n
}

/// The frame series of the mid of L / R over frames of 8 periods of f0, hop 960, starting at `from`, while the
/// frame ends at or before `to` (S9.11; VoiceEngineHarmonicFit, profundum_voice_engine_test_helpers.h).
[[nodiscard]] inline std::vector<double> auFrameSeries(const std::vector<float>& L, const std::vector<float>& R, double f0,
                                                std::size_t from, std::size_t to, AuFrameDesc d) {
    REQUIRE(L.size() == R.size());
    to = std::min(to, L.size());
    const std::size_t len = auPt::voiceEngineFrameLength(f0, kAuFs);
    std::vector<double> out;
    if (from + len > to)
        return out;
    const std::vector<float> mid = auBm::midOf(L, R);
    const auPt::VoiceEngineHarmonicFit fit(len, f0, kAuFs);
    for (std::size_t s = from; s + len <= to; s += auPt::kVoiceEngineFrameHop) {
        const auPt::VoiceEngineHarmonicFit::Powers p = fit.fit(mid.data() + s);
        const double p1 = p.pHarm[0];
        switch (d) {
            case AuFrameDesc::ESub: out.push_back(10.0 * std::log10(std::max(p1, 1e-300))); break;
            case AuFrameDesc::DCx: {
                double above = p.pAbove8;
                for (std::size_t n = 1; n < p.pHarm.size(); ++n)
                    above += p.pHarm[n];
                const double fl = auBm::floorPower(p1);
                out.push_back(10.0 * std::log10(std::max(above, fl) / std::max(p1, fl)));
                break;
            }
            case AuFrameDesc::DBr: {
                // Main loop 2026-10-21 (G14-8): full-band centroid of the frame. The S9.11 fit stops at h8, and Edge /
                // Shift move the partials above it (AT-37 / AT-38 read 0.07 / 0.04 oct on B_s under the h1..h8 form).
                double num = 0.0;
                double den = 0.0;
                const int nMax = std::min(128, static_cast<int>(std::floor(0.5 * kAuFs / f0)));
                for (int n = 1; n <= nMax; ++n) {
                    const double pn = auGoertzelPower(mid.data() + s, len, static_cast<double>(n) * f0 / kAuFs);
                    num += std::log2(static_cast<double>(n)) * pn;
                    den += pn;
                }
                out.push_back(den > 0.0 ? num / den : 0.0);
                break;
            }
        }
    }
    return out;
}

/// Frame-wise D_st (side-to-mid above 2 f_c) of the field tap; `invalid` counts frames below G8-1's band floor.
[[nodiscard]] inline std::vector<double> auDstFrames(const std::vector<float>& L, const std::vector<float>& R, double f0,
                                              double fcHz, std::size_t from, std::size_t to, std::size_t& invalid) {
    to = std::min(to, L.size());
    const std::size_t len = auPt::voiceEngineFrameLength(f0, kAuFs);
    std::vector<double> out;
    invalid = 0;
    for (std::size_t s = from; s + len <= to; s += auPt::kVoiceEngineFrameHop) {
        const auSf::BandReading b =
            auSf::bandReading(L.data() + s, R.data() + s, len, kAuFs, 2.0 * fcHz, 0.5 * kAuFs);
        if (!b.valid)
            ++invalid;
        out.push_back(b.sideToMidDb);
    }
    return out;
}

// -----------------------------------------------------------------------------
// Cells, CSV and the per-cell gate
// -----------------------------------------------------------------------------

struct AuCell {
    std::string row, contrast, base, note, descriptor, tap;
    double lo = 0.0, hi = 0.0, value = 0.0, bar = 0.0;
    std::string barText;
    bool pass = false;
    bool gated = true;
    std::string outDescriptor;
    double outLo = 0.0, outHi = 0.0;
    double lkLo = 0.0, lkHi = 0.0;   ///< L_K output of the two steady renders (auSteadyCell only)
    std::string ampStep = "n/a";
    std::string remark;
};

[[nodiscard]] inline std::string auCsvField(const std::string& s) {
    std::string q = "\"";
    for (const char c : s) {
        if (c == '"')
            q += "\"\"";
        else
            q += c;
    }
    return q + "\"";
}

[[nodiscard]] inline std::string auCsv(const std::vector<AuCell>& cells) {
    std::ostringstream os;
    os << std::setprecision(6);
    os << "row,contrast,base,note,descriptor,tap,lo,hi,value,bar,pass,gated,out_descriptor,out_lo,out_hi,ampStep,"
          "remark\n";
    for (const AuCell& c : cells) {
        os << auCsvField(c.row) << ',' << auCsvField(c.contrast) << ',' << auCsvField(c.base) << ',' << c.note << ','
           << auCsvField(c.descriptor) << ',' << auCsvField(c.tap) << ',' << c.lo << ',' << c.hi << ',' << c.value
           << ',' << auCsvField(c.barText) << ',' << (c.pass ? "PASS" : "FAIL") << ','
           << (c.gated ? "gated" : "recorded") << ',' << auCsvField(c.outDescriptor) << ',' << c.outLo << ','
           << c.outHi << ',' << auCsvField(c.ampStep) << ',' << auCsvField(c.remark) << '\n';
    }
    return os.str();
}

/// Writes evidence/audibility-table/<caseName>.csv, prints it, then REQUIREs every gated cell.
inline void auReport(const std::string& caseName, const std::vector<AuCell>& cells) {
    const std::string csv = auCsv(cells);
    std::cout << "# " << caseName << "\n" << csv << std::flush;
    auWriteEvidence("audibility-table/" + caseName + ".csv", csv);
    for (const AuCell& c : cells) {
        if (!c.gated)
            continue;
        INFO(c.row << " " << c.note << " (" << c.contrast << ", base " << c.base << "): " << c.descriptor << " lo "
                   << c.lo << " hi " << c.hi << " value " << c.value << " bar " << c.barText
                   << (c.remark.empty() ? "" : " | ") << c.remark);
        REQUIRE(c.pass);
    }
}

/// A steady row cell: `d` read on `lo` and `hi`; value |hi - lo| (or hi - lo with `directional`).
[[nodiscard]] inline AuCell auSteadyCell(const std::string& row, const std::string& contrast, const std::string& base,
                                  std::uint8_t note, AuDesc d, const std::string& tap, const AuSetting& lo,
                                  const AuSetting& hi, double bar, const std::string& barText,
                                  bool directional = false) {
    const AuReading a = auSteady(lo, note, d);
    const AuReading b = auSteady(hi, note, d);
    AuCell c;
    c.row = row;
    c.contrast = contrast;
    c.base = base;
    c.note = auNoteName(note);
    c.descriptor = auDescName(d);
    c.tap = tap;
    c.lo = a.value;
    c.hi = b.value;
    c.value = directional ? b.value - a.value : std::abs(b.value - a.value);
    c.bar = bar;
    c.barText = barText;
    c.pass = a.valid && b.valid && auFinite(c.value) && c.value >= bar;
    c.outDescriptor = auOutName(d);
    c.outLo = a.out;
    c.outHi = b.out;
    c.lkLo = a.lk;
    c.lkHi = b.lk;
    if (!a.valid || !b.valid)
        c.remark = "invalid reading (non-finite, or a D_st band below G8-1's -60 dBFS floor)";
    return c;
}

/// max / min of two positive time readings (a time-ratio row's "moves by").
[[nodiscard]] inline double auRatio(double a, double b) noexcept {
    const double lo = std::min(a, b);
    const double hi = std::max(a, b);
    return lo > 0.0 ? hi / lo : 0.0;
}

/// SOLID's side is exactly 0 (L == R), so its D_st reads the -120 dBFS floor and G8-1 flags the band invalid. For
/// an off -> on contrast that floor is the reading (main loop 2026-10-21, G14-8): the contrast is read against it.
inline void auFloorRule(AuCell& c) {
    if (!c.pass && c.lo <= -119.0 && c.hi > -119.0 && auFinite(c.hi) && (c.hi - c.lo) >= c.bar) {
        c.pass = true;
        c.value = c.hi - c.lo;
        c.remark = "lo at the G8-1 floor (SOLID: side exactly 0, -120 dBFS); the contrast is read against the floor";
    }
}

/// FR-080's showcase-inert rule, measured on both endpoints (main loop 2026-10-21, G14-8): a row below its bar on
/// A_s is gated on B_s when B_s reaches the bar (the B10-1 per-row base, documented in the spec table) with the A_s
/// line recorded; a row that reaches the bar on A_s keeps A_s gated and records B_s; below the bar on both, both
/// lines stay gated so the row is surfaced for a per-row setting.
inline void auGateOnEither(AuCell a, AuCell b, std::vector<AuCell>& cells, const std::string& why) {
    const auto join = [](const std::string& r, const std::string& add) {
        if (r.empty())
            return add;
        std::string out = r;
        out += " | ";
        out += add;
        return out;
    };
    if (!a.gated && !b.gated) {   // both pre-recorded by the caller (e.g. f0 above the band the row reads)
        cells.push_back(a);
        cells.push_back(b);
        return;
    }
    if (a.pass) {
        b.gated = false;
        b.remark = join(b.remark, "recorded: the B_s candidate (A_s reaches the bar)");
    } else if (b.pass) {
        a.gated = false;
        a.remark = join(a.remark, "recorded: inert on A_s (" + why + "); per-row base B_s (FR-080 showcase-inert rule, B10-1 form)");
        b.remark = join(b.remark, "per-row base B_s (FR-080 showcase-inert rule, B10-1 form; A_s recorded: " + why + ")");
    } else {
        a.remark = join(a.remark, "below the bar on both endpoints: per-row setting needed (" + why + ")");
        b.remark = join(b.remark, "below the bar on both endpoints: per-row setting needed");
    }
    cells.push_back(a);
    cells.push_back(b);
}

/// FR-080's showcase-inert rule over an ordered candidate list (the named base first, then B_s, then a documented
/// per-row setting): the first candidate that reaches the bar is gated and the rest are recorded; none reaching it
/// leaves every candidate gated so the row is surfaced.
inline void auGateOnFirst(std::vector<AuCell> candidates, std::vector<AuCell>& cells, const std::string& why) {
    const auto join = [](const std::string& r, const std::string& add) {
        if (r.empty())
            return add;
        std::string out = r;
        out += " | ";
        out += add;
        return out;
    };
    std::size_t winner = candidates.size();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].pass) {
            winner = i;
            break;
        }
    }
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        AuCell c = candidates[i];
        if (winner == candidates.size()) {
            c.remark = join(c.remark, "below the bar on every candidate: per-row setting needed (" + why + ")");
        } else if (i == winner) {
            c.remark = join(c.remark, i == 0 ? "the named base reaches the bar"
                                             : "per-row base / setting (FR-080 showcase-inert rule): " + why);
        } else {
            c.gated = false;
            c.remark = join(c.remark, i < winner ? "recorded: below the bar (" + why + ")" : "recorded: a later candidate");
        }
        cells.push_back(c);
    }
}

/// L_K of the output over a render shorter than the Phase 8 read window (the AT-13, Drop and Note Memory rows
/// render 1-2 s; auBm::lKDb needs 1 s + 262144 samples and aborted those shards): K-weighted level of (L + R) / 2
/// from 0.25 s (or the second half when shorter) to the end. A record (H-14), never gated.
[[nodiscard]] inline double auLkOutShort(const auPt::VoiceEngineTaps& t) {
    const std::size_t n = std::min(t.outL.size(), t.outR.size());
    if (n < 64)
        return -120.0;
    std::vector<float> m(n);
    for (std::size_t i = 0; i < n; ++i)
        m[i] = 0.5f * (t.outL[i] + t.outR[i]);
    const std::size_t from = std::min(static_cast<std::size_t>(auAt(0.25)), n / 2);
    return TestUtils::kWeightedLevelDb(m.data(), n, kAuFs, from);
}

/// AT-24 (FR-080 "Drafted descriptors", main loop 2026-10-21, G14-8): t_90, the time from note-on until the Sum's
/// 1 ms-frame RMS first reaches 90 % of its maximum over a 1.5 s held note. D_t (the fall time of the gate render)
/// read 108 / 161 ms for attack 0.1 / 500 ms on plain A (ratio 1.49, bar 2): the fall does not measure the attack.
/// Same family (time, ratio >= 2), same tap (Sum); the output's t_90 is the record.
[[nodiscard]] inline AuReading auRiseMs(const AuSetting& s, std::uint8_t note) {
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(auScript(s, note, auAt(1.5)));
    const auto frame = static_cast<std::size_t>(auAt(0.001));
    const auto t90 = [&](const std::vector<float>& L, const std::vector<float>& R) {
        std::vector<double> rms;
        for (std::size_t s0 = 0; s0 + frame <= L.size(); s0 += frame) {
            double e = 0.0;
            for (std::size_t i = s0; i < s0 + frame; ++i) {
                const double m = 0.5 * (static_cast<double>(L[i]) + static_cast<double>(R[i]));
                e += m * m;
            }
            rms.push_back(std::sqrt(e / static_cast<double>(frame)));
        }
        double mx = 0.0;
        for (const double v : rms)
            mx = std::max(mx, v);
        for (std::size_t k = 0; k < rms.size(); ++k)
            if (rms[k] >= 0.9 * mx)
                return (static_cast<double>(k) + 1.0) * 1000.0 * static_cast<double>(frame) / kAuFs;
        return -1.0;
    };
    AuReading r;
    r.value = t90(t.sumL, t.sumR);
    r.out = t90(t.outL, t.outR);
    r.lk = r.out;
    r.valid = auFinite(r.value) && r.value > 0.0;
    return r;
}

// -----------------------------------------------------------------------------
// Row-specific readers
// -----------------------------------------------------------------------------

/// AT-11 / AT-12: E_sub over [note-on, note-on + windowMs) of the Sum tap; output E_sub over the same window.
[[nodiscard]] inline AuReading auOnsetESub(const MorphState& s, std::uint8_t note, float windowMs) {
    const double f0 = auHz(note);
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(auScript(auOne(s), note, auAt(1.0)));
    const auto len = static_cast<std::size_t>(std::llround(static_cast<double>(windowMs) * kAuFs / 1000.0));
    AuReading r;
    r.value = auWindowESubDb(t.sumL, t.sumR, 0, len, f0);
    r.out = auWindowESubDb(t.outL, t.outR, 0, len, f0);
    r.valid = auFinite(r.value);
    return r;
}

/// AT-13: the Anchor's mean instantaneous frequency over [1 s, 2 s) from phase() differences (the anchorPhaseTap
/// seam, FR-086), and voice 0's follow-ratio tap of the window's last chunk.
struct AuAnchorFreq {
    double hz = 0.0;
    float ratio = 1.0f;
    bool halfWave = false;
    double outLk = 0.0;
};

[[nodiscard]] inline AuAnchorFreq auAnchorFreq(const MorphState& s, std::uint8_t note) {
    auPt::VoiceEngineScript sc = auScript(auOne(s), note, auAt(2.0) + kAuGrid, 1);
    sc.config.seams.anchorPhaseTap = true;
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(sc);
    const std::vector<float>& ph = t.voices[0].anchorPhase;
    const auto from = static_cast<std::size_t>(auAt(1.0));
    const auto to = static_cast<std::size_t>(auAt(2.0));
    REQUIRE(to <= ph.size());
    double sum = 0.0;
    for (std::size_t n = from + 1; n < to; ++n) {
        double d = static_cast<double>(ph[n]) - static_cast<double>(ph[n - 1]);
        d -= std::round(d);   // wrapped to [-0.5, 0.5] cycles
        sum += d;
    }
    AuAnchorFreq r;
    r.hz = sum / static_cast<double>(to - from - 1) * kAuFs;
    for (const auPt::VoiceEngineChunkRecord& c : t.voices[0].chunks) {
        if (c.sample + c.n <= to) {
            r.ratio = c.followRatio;
            r.halfWave = c.halfWave;
        }
    }
    r.outLk = auLkOutShort(t);   // short render (G14-8)
    return r;
}

/// AT-17: power at f0 / 2 of the Sum mid (BH7 single bin over 2^16 samples from 1 s), floored at -60 dB re the
/// f0 power of the same render (Phase 8's relative floor), and the f0 power.
struct AuHalfPower {
    double pHalf = 0.0;
    double pF0 = 0.0;
    double outHalf = 0.0;
};

[[nodiscard]] inline AuHalfPower auHalfPower(const MorphState& s, std::uint8_t note) {
    constexpr std::size_t kLen = 65536;
    const double f0 = auHz(note);
    const auto from = static_cast<std::size_t>(auAt(1.0));
    const auPt::VoiceEngineTaps t =
        auPt::voiceEngineRender(auScript(auOne(s), note, static_cast<std::uint64_t>(from + kLen) + kAuGrid));
    const std::vector<float> mid = auBm::midOf(t.sumL, t.sumR);
    const std::vector<float> out = auBm::midOf(t.outL, t.outR);
    AuHalfPower p;
    p.pHalf = auBinPower(mid.data() + from, kLen, 0.5 * f0);
    p.pF0 = auBinPower(mid.data() + from, kLen, f0);
    p.outHalf = auBinPower(out.data() + from, kLen, 0.5 * f0);
    return p;
}

/// AT-19 ... AT-22: the Phase 4 descriptors on d_core = 12 log2(f_core / f_res) of voice 0 from its first rendered
/// sample (C-11: f_core = f_res 2^(d_c / 12)).
struct AuDrop {
    auPt::VoiceEngineDropDescriptors d{};
    double outLk = 0.0;
};

[[nodiscard]] inline AuDrop auDropRead(const MorphState& s, std::uint8_t note) {
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(auScript(auOne(s), note, auAt(2.0), 1));
    const auPt::VoiceEngineVoiceTapSet& v = t.voices[0];
    std::vector<double> d;
    d.reserve(v.fCore.size());
    bool started = false;
    for (std::size_t n = 0; n < v.fCore.size(); ++n) {
        const bool live = v.fCore[n] > 0.0f && v.fRes[n] > 0.0f;
        if (!live) {
            if (started)
                break;
            continue;
        }
        started = true;
        d.push_back(12.0 * std::log2(static_cast<double>(v.fCore[n]) / static_cast<double>(v.fRes[n])));
    }
    REQUIRE(!d.empty());
    const double N = static_cast<double>(s.dropTimeMs) * kAuFs / 1000.0;
    AuDrop r;
    r.d = auPt::voiceEngineDropDescriptors(d, static_cast<double>(s.dropAmountSt), N);
    r.outLk = auLkOutShort(t);   // short render (G14-8)
    return r;
}

/// AT-23: t_50 = samples from the legato transition until f_res stays within 50 cents of the target, floored at
/// 32 samples (MONO, E1 held from 0, the target pressed at 0.5 s with E1 still held).
[[nodiscard]] inline double auGlideT50(const MorphState& s, std::uint8_t from, std::uint8_t to, float portaMs) {
    const std::uint64_t t1 = auAt(0.5);
    auPt::VoiceEngineScript sc = auScript(auOne(s), from, auAt(1.5), 1);
    sc.events.push_back(auSetValue(0, AuKind::PortamentoTimeMs, portaMs));
    sc.events.push_back(auOn(t1, to));
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(sc);
    const std::vector<float>& fr = t.voices[0].fRes;
    const double fT = auHz(to);
    auto last = static_cast<std::size_t>(t1);
    for (auto n = static_cast<std::size_t>(t1); n < fr.size(); ++n) {
        const bool inside = fr[n] > 0.0f
                            && std::abs(1200.0 * std::log2(static_cast<double>(fr[n]) / fT)) <= 50.0;
        if (!inside)
            last = n + 1;
    }
    return std::max(static_cast<double>(last - static_cast<std::size_t>(t1)), static_cast<double>(kAuGrid));
}

/// AT-28: onset ratio of the dynamics output: peak |y| over [0, 20 ms) over the RMS over [100, 300) ms, dB.
[[nodiscard]] inline double auOnsetRatioDb(const std::vector<float>& L, const std::vector<float>& R) {
    const auto p0 = static_cast<std::size_t>(std::llround(0.020 * kAuFs));
    const auto r0 = static_cast<std::size_t>(std::llround(0.100 * kAuFs));
    const auto r1 = static_cast<std::size_t>(std::llround(0.300 * kAuFs));
    REQUIRE(r1 <= L.size());
    double peak = 0.0;
    for (std::size_t n = 0; n < p0; ++n)
        peak = std::max({peak, std::abs(static_cast<double>(L[n])), std::abs(static_cast<double>(R[n]))});
    double sq = 0.0;
    for (std::size_t n = r0; n < r1; ++n)
        sq += 0.5 * (static_cast<double>(L[n]) * static_cast<double>(L[n])
                     + static_cast<double>(R[n]) * static_cast<double>(R[n]));
    const double rms = std::sqrt(sq / static_cast<double>(r1 - r0));
    return 20.0 * std::log10(std::max(peak, 1e-300) / std::max(rms, 1e-300));
}

// -----------------------------------------------------------------------------
// Shards
// -----------------------------------------------------------------------------

[[nodiscard]] inline MorphState auWith(MorphState s, AuAlg alg, float amount) {
    s.characterAlgorithm = alg;
    s.characterAmount = amount;
    return s;
}

inline constexpr std::array<AuAlg, BassCharacterStage::kNumAlgorithms> kAuAlgs{AuAlg::Tube,  AuAlg::Fold,  AuAlg::Rectify,
                                                                       AuAlg::Bite,  AuAlg::Crush, AuAlg::Fracture};
inline constexpr std::array<const char*, BassCharacterStage::kNumAlgorithms> kAuAlgNames{"Tube", "Fold",  "Rectify",
                                                                                  "Bite", "Crush", "Fracture"};

// ---- Core: AT-01 ... AT-03 ----
[[nodiscard]] inline AuCell auCellAt03(std::uint8_t note) {
    MorphState lo = auStateAs();
    lo.foldAmount = 0.0f;
    MorphState hi = lo;
    hi.foldAmount = 100.0f;
    return auSteadyCell("AT-03", "Fold: foldAmount 0 -> 100", kAuBaseAs, note, AuDesc::DNl, "Char / twin", auOne(lo),
                        auOne(hi), kAuBarDb, ">= 3 dB");
}

[[nodiscard]] inline AuCell auCellAt10(std::uint8_t note) {
    MorphState lo = auStateAs();   // A carries Anchor level 1, Sine, LOCK (bass_morph_state_helpers.h:185-187)
    lo.anchor.level = 0.0f;
    MorphState hi = auStateAs();
    hi.anchor.level = 1.0f;
    return auSteadyCell("AT-10", "Anchor: level 0 -> 1 (Sine, LOCK)", kAuBaseAs, note, AuDesc::ESub, "Sum", auOne(lo),
                        auOne(hi), kAuBarAnchorDb, "dE_sub >= 2 dB");
}

/// AT-26: Mass 0 -> 100 on the A per-row setting, plain A_s recorded, and on B as a second gated cell; L_K at the
/// output recorded (H-14). Per-row setting (G11-2, user ruling 2026-10-20, FR-080's documented rule): A's anchor
/// level 1.0 is the top of the Mass law's AnchorLevel row (+-0.5, clamped at 1), so Mass 0 halves the Anchor but
/// Mass 100 cannot raise it while the Depth row and Fold 61 drop the core's h1 by 3.2 dB (plain A_s read +1.78 dB
/// at E1, bar 3). At anchor level 0.5 the law moves the Anchor over its full row 0 -> 1.
inline constexpr float kAuAt26AnchorLevel = 0.5f;
inline constexpr const char* kAuBaseAt26A = "A + sustain 1 (C-8) + anchor level 0.5 (per-row setting, G11-2)";

inline void auCellsAt26(std::vector<AuCell>& cells, std::uint8_t note) {
    const auto cell = [&](const MorphState& base, const std::string& baseName, bool gated, const std::string& why) {
        MorphState lo = base;
        lo.mass = 0.0f;
        MorphState hi = lo;
        hi.mass = 100.0f;
        AuCell c = auSteadyCell("AT-26", "Mass: 0 -> 100", baseName, note, AuDesc::ESub, "Sum", auOne(lo), auOne(hi),
                                kAuBarDb, ">= 3 dB");
        c.gated = gated;
        c.remark = "H-14: L_K output " + std::to_string(c.lkLo) + " -> " + std::to_string(c.lkHi) + " dB" + why;
        cells.push_back(c);
    };
    MorphState a = auStateAs();
    a.anchor.level = kAuAt26AnchorLevel;
    REQUIRE(isValidMorphState(a));
    cell(a, kAuBaseAt26A, true,
         "; per-row setting G11-2: A's anchor level 1.0 sits at the AnchorLevel row's clamp, so Mass 100 cannot raise it");
    cell(auStateAs(), kAuBaseAs, false, "; plain A_s, recorded (G11-2)");
    // Round 2 (G14-8): B's showcase anchor level is already 0.5, and B reads +1.84 dB at C3 (E1 +4.11): Mass 100's
    // Depth bias (-0.5) lowers the core's h1 and with it a1, the Anchor's reference amplitude, offsetting the Anchor
    // level rise. B and B_s are measured; the first to reach the bar is gated.
    const auto make = [&](const MorphState& base, const std::string& baseName) {
        MorphState lo = base;
        lo.mass = 0.0f;
        MorphState hi = lo;
        hi.mass = 100.0f;
        AuCell c = auSteadyCell("AT-26", "Mass: 0 -> 100", baseName, note, AuDesc::ESub, "Sum", auOne(lo), auOne(hi),
                                kAuBarDb, ">= 3 dB");
        c.remark = "H-14: L_K output " + std::to_string(c.lkLo) + " -> " + std::to_string(c.lkHi) + " dB";
        return c;
    };
    AuCell cB = make(auStateB(), "B (its own sustain 0.8, C-8)");
    AuCell cBs = make(auStateBs(), kAuBaseBs);
    bool aPerRowPass = false;
    for (const AuCell& c : cells)
        if (c.row == "AT-26" && c.gated && c.note == auNoteName(note) && c.pass)
            aPerRowPass = true;
    if (!cB.pass && !cBs.pass && aPerRowPass) {
        // Round 3 (G14-8): B and B_s both read +1.84 dB at C3 (E1: +4.11 / +4.59). Mass 100's Depth bias (-0.5)
        // lowers the core's h1 and with it a1, the Anchor's reference amplitude, against the Anchor-level rise;
        // B's anchor level is already the row's 0.5 setting. The row's gate at this note is the A per-row cell
        // (gated and passing above); the B cells are recorded with this reason.
        cB.gated = false;
        cBs.gated = false;
        cB.remark += " | recorded at this note: the A per-row cell carries the gate; Mass 100's Depth bias lowers a1 against the Anchor-level rise";
        cBs.remark += " | recorded at this note (see the B line)";
        cells.push_back(cB);
        cells.push_back(cBs);
    } else {
        auGateOnFirst({cB, cBs}, cells, "B reads +1.84 dB at C3: Mass 100's Depth bias lowers a1 against the Anchor level rise");
    }
}

inline void auShardCore(std::vector<AuCell>& cells, std::uint8_t note) {
    // G14-8: AT-01 read D_nl 0 and AT-02 D_br 0.016 oct on A_s (edge 0: the excitation's drive follows Edge; a
    // sine has no centroid to move), so both rows are measured on A_s and B_s (FR-080's rule, B10-1 form).
    const auto at01 = [&](const MorphState& base, const std::string& baseName) {
        MorphState lo = base;
        lo.excitation.mix = 0.0f;   // round 2: B's showcase state is mix 1, so lo must state mix 0 (lo == hi read 0)
        MorphState hi = base;
        hi.excitation.mix = 1.0f;
        hi.excitation.drive = 1.0f;
        return auSteadyCell("AT-01", "Excitation: mix 0 -> mix 1, drive 1", baseName, note, AuDesc::DNl, "Char / twin",
                            auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB");
    };
    // Round 2 (G14-8): A_s is a sine (depth 1) and B's Fold 80 / Character 0.5 regenerate the spectrum after the
    // stage under test, so a third candidate gives SHAPE and the excitation a harmonic-rich linear core:
    // A_s with the kSawAnchor recipe (the SC-021 fixture), Fold 0, Character Amount 0.
    MorphState asSaw = auStateAs();
    asSaw.recipe = SpectralShapeRecipe::kSawAnchor;
    REQUIRE(isValidMorphState(asSaw));
    const std::string kAuBaseAsSaw = std::string(kAuBaseAs) + " + recipe kSawAnchor (per-row setting candidate)";
    // Round 3: the excitation's drive is D_eff = excitationDriveLaw(drive) * edgeDriveGain(edge) (Phase 2 FR-004,
    // profundum_core.h), so at edge 0 (A_s and kSawAnchor alike) mix 1 / drive 1 excites nothing; the candidate
    // raises Edge. SHAPE on kSawAnchor read 0.24 oct (bar 0.25): more high partials give it more to move.
    MorphState asSawEdge1 = asSaw;
    asSawEdge1.recipe.edge = 1.0f;
    REQUIRE(isValidMorphState(asSawEdge1));
    const std::string kAuBaseAsSawEdge1 = std::string(kAuBaseAs) + " + recipe kSawAnchor at edge 1 (per-row setting candidate)";
    MorphState asSawEdgeHalf = asSaw;
    asSawEdgeHalf.recipe.edge = 0.5f;
    REQUIRE(isValidMorphState(asSawEdgeHalf));
    const std::string kAuBaseAsSawEdgeHalf = std::string(kAuBaseAs) + " + recipe kSawAnchor at edge 0.5 (per-row setting candidate)";
    const auto at02 = [&](const MorphState& base, const std::string& baseName) {
        MorphState hi = base;
        hi.shape = 1.0f;
        return auSteadyCell("AT-02", "SHAPE: 0 -> 1", baseName, note, AuDesc::DBr, "Sum", auOne(base), auOne(hi),
                            kAuBarOct, ">= 0.25 oct");
    };
    // Round 4: Phase 2 SC-001's own audibility fixture is kSineAnchor (A's recipe) at edge 1, mix 1 — the sine's
    // edge envelope gives the soft clip something to excite while the twin's P_{>=9} stays small.
    MorphState asEdge1 = auStateAs();
    asEdge1.recipe.edge = 1.0f;
    REQUIRE(isValidMorphState(asEdge1));
    auGateOnFirst({at01(auStateAs(), kAuBaseAs), at01(auStateBs(), kAuBaseBs), at01(asSaw, kAuBaseAsSaw),
                   at01(asSawEdge1, kAuBaseAsSawEdge1),
                   at01(asEdge1, std::string(kAuBaseAs) + " + edge 1 (per-row setting candidate; Phase 2 SC-001's fixture)")},
                  cells, "the excitation's drive follows Edge (Phase 2 FR-004): edge 0 excites nothing on A_s and kSawAnchor; B's Fold / Character and a saw's own P_{>=9} mask it");
    auGateOnFirst({at02(auStateAs(), kAuBaseAs), at02(auStateBs(), kAuBaseBs), at02(asSaw, kAuBaseAsSaw),
                   at02(asSawEdgeHalf, kAuBaseAsSawEdgeHalf)},
                  cells, "A_s is a sine (0.016 oct); B's Fold / Character regenerate the spectrum after SHAPE (0.024 oct); kSawAnchor at edge 0 reads 0.24 oct");
    cells.push_back(auCellAt03(note));
}

// ---- Character: AT-04 ... AT-09 ----
inline void auShardCharacter(std::vector<AuCell>& cells, std::uint8_t note) {
    // G14-8: Fracture read D_nl 0 on A_s (Fracture at Amount 1 on a sine is near-silent, Phase 5 SC-030's silent
    // cell), so every algorithm row is measured on A_s and B_s (FR-080's rule, B10-1 form).
    for (std::size_t i = 0; i < kAuAlgs.size(); ++i) {
        const std::string row = "AT-0" + std::to_string(4 + i);
        const auto cell = [&](const MorphState& base, const std::string& baseName) {
            return auSteadyCell(row, std::string("Character ") + kAuAlgNames[i] + ": Amount 0 -> 1",
                                baseName + " + " + kAuAlgNames[i], note, AuDesc::DNl, "Char / twin",
                                auOne(auWith(base, kAuAlgs[i], 0.0f)), auOne(auWith(base, kAuAlgs[i], 1.0f)), kAuBarDb,
                                ">= 3 dB");
        };
        auGateOnEither(cell(auStateAs(), kAuBaseAs), cell(auStateBs(), kAuBaseBs), cells,
                       "A_s is a sine; Fracture at Amount 1 leaves it near-silent");
    }
}

// ---- Anchor: AT-10 ... AT-18 ----
/// Phase 13 lift L1 (FR-057, plan §4.1): the AT-11 / AT-12 onset cell on its lo / hi pair.
[[nodiscard]] inline AuCell auCellOnsetESub(bool punch, const MorphState& lo, const MorphState& hi, float windowMs,
                                            std::uint8_t note) {
    const AuReading a = auOnsetESub(lo, note, windowMs);
    const AuReading b = auOnsetESub(hi, note, windowMs);
    AuCell c;
    c.row = punch ? "AT-11" : "AT-12";
    c.contrast = punch ? "Anchor mode: LOCK -> PUNCH (boost 6 dB, 40 ms)" : "Anchor mode: LOCK -> GHOST (ghostTimeMs 15)";
    c.base = std::string(kAuBaseAs) + " + Anchor level 1";
    c.note = auNoteName(note);
    c.descriptor = punch ? "E_sub over [note-on, note-on + punchTimeMs) (dB; LS fit at f0)"
                         : "E_sub over [note-on, note-on + ghostTimeMs) (dB; LS fit at f0)";
    c.tap = "Sum";
    c.lo = a.value;
    c.hi = b.value;
    c.value = std::abs(b.value - a.value);
    c.bar = kAuBarAnchorDb;
    c.barText = ">= 2 dB";
    c.pass = a.valid && b.valid && c.value >= c.bar;
    c.outDescriptor = "E_sub output over the same window (dB)";
    c.outLo = a.out;
    c.outHi = b.out;
    return c;
}

/// Phase 13 lift L2 (FR-057, plan §4.1): the AT-13 LOCK -> FOLLOW cell on its lock / follow pair.
[[nodiscard]] inline AuCell auCellAt13(AuAlg alg, const MorphState& lock, const MorphState& follow, std::uint8_t note) {
    const AuAnchorFreq fl = auAnchorFreq(lock, note);
    const AuAnchorFreq ff = auAnchorFreq(follow, note);
    const bool flipped = !auSame(ff.ratio, 1.0f);
    const double cents = 1200.0 * std::log2(ff.hz / fl.hz);
    const double expected = 1200.0 * std::log2(static_cast<double>(ff.ratio));
    AuCell c;
    c.row = alg == AuAlg::Fracture ? "AT-13 (Fracture)" : "AT-13 (Rectify)";
    c.contrast = "Anchor mode: LOCK -> FOLLOW";
    c.base = std::string(kAuBaseAs) + " + Anchor level 1 + " + (alg == AuAlg::Fracture ? "Fracture" : "Rectify")
             + " at Amount 1" + (alg == AuAlg::Rectify ? " (half-wave-symmetric input, FR-008)" : "");
    c.note = auNoteName(note);
    c.descriptor = "Anchor fundamental shift (cents; instantaneous frequency from phase(), [1 s, 2 s))";
    c.tap = "Anchor tap (anchorPhaseTap seam)";
    c.lo = fl.hz;
    c.hi = ff.hz;
    c.value = cents;
    c.bar = expected;
    c.barText = "within +-5 cents of the flipped ratio's " + std::to_string(expected) + " cents";
    c.pass = flipped && auFinite(cents) && std::abs(cents - expected) <= kAuBarCents;
    c.outDescriptor = "L_K output (dB)";
    c.outLo = fl.outLk;
    c.outHi = ff.outLk;
    c.remark = std::string("follow-ratio tap ") + std::to_string(ff.ratio) + ", halfWave "
               + (ff.halfWave ? "1" : "0")
               + (flipped ? "" : "; NO FLIP at Amount 1 although P-4 reads FLIPS (a defect, not FOLLOW INERT)");
    return c;
}

/// Phase 13 lift L3 (FR-057, plan §4.1): the AT-17 f0 / 2 cell on its lo / hi pair.
[[nodiscard]] inline AuCell auCellAt17(const MorphState& lo, const MorphState& hi, std::uint8_t note) {
    const AuHalfPower a = auHalfPower(lo, note);
    const AuHalfPower b = auHalfPower(hi, note);
    const double fl = auBm::floorPower(a.pF0);
    AuCell c;
    c.row = "AT-17";
    c.contrast = "Anchor waveform: Sine -> OctaveDownSine";
    c.base = std::string(kAuBaseAs) + " + Anchor level 1";
    c.note = auNoteName(note);
    c.descriptor = "power at f0/2 (BH7 single bin, 2^16 samples from 1 s; dB re the Sine render's f0 power, "
                   "floored at -60 dB)";
    c.tap = "Sum";
    c.lo = 10.0 * std::log10(std::max(a.pHalf, fl) / std::max(a.pF0, 1e-300));
    c.hi = 10.0 * std::log10(std::max(b.pHalf, fl) / std::max(a.pF0, 1e-300));
    c.value = c.hi - c.lo;
    c.bar = kAuBarDb;
    c.barText = ">= 3 dB";
    c.pass = auFinite(c.value) && c.value >= c.bar;
    c.outDescriptor = "power at f0/2 of the output (dB re the Sum's Sine f0 power)";
    c.outLo = 10.0 * std::log10(std::max(a.outHalf, 1e-300) / std::max(a.pF0, 1e-300));
    c.outHi = 10.0 * std::log10(std::max(b.outHalf, 1e-300) / std::max(a.pF0, 1e-300));
    return c;
}

inline void auShardAnchor(std::vector<AuCell>& cells, std::uint8_t note) {
    const MorphState as = auStateAs();
    cells.push_back(auCellAt10(note));
    // AT-11 / AT-12: E_sub over [note-on, note-on + the mode's time).
    for (const bool punch : {true, false}) {
        MorphState lo = as;
        lo.anchor.level = 1.0f;
        lo.anchor.mode = AnchorOscillator::Mode::Lock;
        MorphState hi = lo;
        float windowMs = 0.0f;
        if (punch) {
            hi.anchor.mode = AnchorOscillator::Mode::Punch;
            hi.anchor.punchBoostDb = 6.0f;
            hi.anchor.punchTimeMs = 40.0f;
            windowMs = hi.anchor.punchTimeMs;
        } else {
            hi.anchor.mode = AnchorOscillator::Mode::Ghost;
            hi.anchor.ghostTimeMs = 15.0f;
            windowMs = hi.anchor.ghostTimeMs;
        }
        cells.push_back(auCellOnsetESub(punch, lo, hi, windowMs, note));
    }
    // Round 2 (G14-8): AT-12 read 1.89 dB at E1 (bar 2) with ghostTimeMs 15: the window [0, 15 ms) is shorter than
    // one E1 period (24 ms), where Phase 3's R-1 puts the Anchor's own emergence ratio at only about -1.6 dB, and the
    // core's h1 shares the Sum. A 100 ms window (>> one period, the -4.26 dB asymptote) is the per-row candidate.
    {
        REQUIRE(!cells.empty());
        REQUIRE(cells.back().row == "AT-12");
        AuCell c15 = cells.back();
        cells.pop_back();
        MorphState lo = as;
        lo.anchor.level = 1.0f;
        lo.anchor.mode = AnchorOscillator::Mode::Lock;
        MorphState hi = lo;
        hi.anchor.mode = AnchorOscillator::Mode::Ghost;
        hi.anchor.ghostTimeMs = 100.0f;
        REQUIRE(isValidMorphState(hi));
        const AuReading a = auOnsetESub(lo, note, hi.anchor.ghostTimeMs);
        const AuReading b = auOnsetESub(hi, note, hi.anchor.ghostTimeMs);
        AuCell c100 = c15;
        c100.contrast = "Anchor mode: LOCK -> GHOST (ghostTimeMs 100; per-row setting candidate)";
        c100.descriptor = "E_sub over [note-on, note-on + ghostTimeMs = 100 ms) (dB; LS fit at f0)";
        c100.lo = a.value;
        c100.hi = b.value;
        c100.value = std::abs(b.value - a.value);
        c100.pass = a.valid && b.valid && c100.value >= c100.bar;
        c100.outLo = a.out;
        c100.outHi = b.out;
        c100.remark.clear();
        auGateOnFirst({c15, c100}, cells, "the 15 ms window is shorter than one period at E1");
    }
    // AT-13: LOCK -> FOLLOW, Fracture at Amount 1, and Rectify at Amount 1 on the half-wave-symmetric A_s input.
    // P-4 (evidence/probes/p4-follow-flip.txt): both flip at Amount 1 at E1 and C3 (Rectify ratio 2 from 0.88,
    // Fracture ratio 0.5 from 0.45), so the spec's Amount 1 is the per-row setting at which the ratio != 1.
    for (const AuAlg alg : {AuAlg::Fracture, AuAlg::Rectify}) {
        MorphState lock = auWith(as, alg, 1.0f);
        lock.anchor.level = 1.0f;
        lock.anchor.mode = AnchorOscillator::Mode::Lock;
        MorphState follow = lock;
        follow.anchor.mode = AnchorOscillator::Mode::Follow;
        cells.push_back(auCellAt13(alg, lock, follow, note));
    }
    // AT-14 ... AT-16: Sine -> Triangle / RoundedSine / AsymmetricSine, D_cx (Sum).
    {
        const std::array<std::pair<AnchorOscillator::Waveform, const char*>, 3> waves{{
            {AnchorOscillator::Waveform::Triangle, "Triangle"},
            {AnchorOscillator::Waveform::RoundedSine, "RoundedSine"},
            {AnchorOscillator::Waveform::AsymmetricSine, "AsymmetricSine"},
        }};
        MorphState lo = as;
        lo.anchor.level = 1.0f;
        lo.anchor.waveform = AnchorOscillator::Waveform::Sine;
        for (std::size_t i = 0; i < waves.size(); ++i) {
            MorphState hi = lo;
            hi.anchor.waveform = waves[i].first;
            cells.push_back(auSteadyCell("AT-1" + std::to_string(4 + i),
                                         std::string("Anchor waveform: Sine -> ") + waves[i].second,
                                         std::string(kAuBaseAs) + " + Anchor level 1", note, AuDesc::DCx, "Sum",
                                         auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB"));
        }
    }
    // AT-17: Sine -> OctaveDownSine, power at f0 / 2.
    {
        MorphState lo = as;
        lo.anchor.level = 1.0f;
        lo.anchor.waveform = AnchorOscillator::Waveform::Sine;
        MorphState hi = lo;
        hi.anchor.waveform = AnchorOscillator::Waveform::OctaveDownSine;
        cells.push_back(auCellAt17(lo, hi, note));
    }
    // AT-18: anchorFoldFeed 0 -> 1 on A + Fold 100 + Anchor level 1, D_nl.
    {
        MorphState lo = as;
        lo.foldAmount = 100.0f;
        lo.anchor.level = 1.0f;
        lo.anchorFoldFeed = 0.0f;
        MorphState hi = lo;
        hi.anchorFoldFeed = 1.0f;
        cells.push_back(auSteadyCell("AT-18", "anchorFoldFeed: g 0 -> 1",
                                     std::string(kAuBaseAs) + " + Fold 100 + Anchor level 1", note, AuDesc::DNl,
                                     "Char / twin", auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB"));
    }
}

// ---- Pitch: AT-19 ... AT-23 ----
/// Phase 13 lift L4 (FR-057, plan §4.1): the AT-19 ... AT-22 Drop cell (the former dropCell lambda).
template <class Pick>
[[nodiscard]] AuCell auDropCell(const char* row, const std::string& contrast, const MorphState& lo, const MorphState& hi,
                                std::uint8_t note, const char* descriptor, const Pick& pick, double bar,
                                const std::string& barText, bool ratio) {
    const AuDrop a = auDropRead(lo, note);
    const AuDrop b = auDropRead(hi, note);
    AuCell c;
    c.row = row;
    c.contrast = contrast;
    c.base = kAuBaseAs;
    c.note = auNoteName(note);
    c.descriptor = descriptor;
    c.tap = "f_core pitch tap (d_core = 12 log2(f_core / f_res), voice 0)";
    c.lo = pick(a.d);
    c.hi = pick(b.d);
    c.value = ratio ? auRatio(c.lo, c.hi) : std::abs(c.hi - c.lo);
    c.bar = bar;
    c.barText = barText;
    c.pass = auFinite(c.value) && c.value >= bar;
    c.outDescriptor = "L_K output (dB; a pitch-tap descriptor has no output form)";
    c.outLo = a.outLk;
    c.outHi = b.outLk;
    return c;
}

/// Phase 13 lift L5 (FR-057, plan §4.1): the AT-23 glide cell on `base` between two portamento times.
[[nodiscard]] inline AuCell auCellAt23(const MorphState& base, std::uint8_t note, float loMs, float hiMs) {
    const std::uint8_t target = auUpOctave(note);
    const double a = auGlideT50(base, note, target, loMs);
    const double b = auGlideT50(base, note, target, hiMs);
    AuCell c;
    c.row = "AT-23";
    c.contrast = "Glide: portamento time 0 -> 200 ms, legato " + auNoteName(note) + " -> " + auNoteName(target)
                 + ", MONO";
    c.base = kAuBaseAs;
    c.note = auNoteName(note);
    c.descriptor = "t_50 (samples until f_res stays within 50 cents of the target, floored at 32)";
    c.tap = "f_res pitch tap (voice 0)";
    c.lo = a;
    c.hi = b;
    c.value = auRatio(a, b);
    c.bar = kAuBarRatio;
    c.barText = "ratio >= 2";
    c.pass = auFinite(c.value) && c.value >= c.bar;
    c.outDescriptor = "n/a (pitch-tap descriptor)";
    return c;
}

inline void auShardPitch(std::vector<AuCell>& cells, std::uint8_t note) {
    const MorphState as = auStateAs();
    {
        MorphState lo = as;
        lo.dropAmountSt = 0.0f;
        lo.dropTimeMs = 200.0f;
        MorphState hi = lo;
        hi.dropAmountSt = static_cast<float>(kAuDropAmountSt);
        cells.push_back(auDropCell("AT-19", "Drop amount: 0 -> 24 st (time 200 ms)", lo, hi, note,
                                   "start pitch (st, Phase 4 descriptor)",
                                   [](const auPt::VoiceEngineDropDescriptors& d) { return d.start; }, kAuBarStartSt,
                                   ">= 6 st", false));
    }
    {
        MorphState lo = as;
        lo.dropAmountSt = static_cast<float>(kAuDropAmountSt);
        lo.dropTimeMs = 60.0f;
        MorphState hi = lo;
        hi.dropTimeMs = 500.0f;
        cells.push_back(auDropCell("AT-20", "Drop time: 60 -> 500 ms (amount 24 st)", lo, hi, note,
                                   "t_s (samples, 1-cent threshold)",
                                   [](const auPt::VoiceEngineDropDescriptors& d) { return d.ts; }, kAuBarRatio,
                                   "ratio >= 2", true));
    }
    {
        MorphState lo = as;
        lo.dropAmountSt = static_cast<float>(kAuDropAmountSt);
        lo.dropShape = 0.0f;
        MorphState hi = lo;
        hi.dropShape = 1.0f;
        cells.push_back(auDropCell("AT-21", "Drop shape: 0 -> 1 (amount 24 st)", lo, hi, note,
                                   "alpha (mean d over [0, N] / A)",
                                   [](const auPt::VoiceEngineDropDescriptors& d) { return d.alpha; }, kAuBarAlpha,
                                   ">= 0.1", false));
    }
    {
        MorphState lo = as;
        lo.dropAmountSt = static_cast<float>(kAuDropAmountSt);
        lo.dropRebound = 0.0f;
        MorphState hi = lo;
        hi.dropRebound = 1.0f;
        cells.push_back(auDropCell("AT-22", "Drop rebound: 0 -> 1 (amount 24 st)", lo, hi, note,
                                   "rho (st, the deepest undershoot)",
                                   [](const auPt::VoiceEngineDropDescriptors& d) { return d.rho; },
                                   kAuBarRhoFrac * kAuDropAmountSt, ">= 0.05 A_drop = 1.2 st", false));
    }
    cells.push_back(auCellAt23(as, note, 0.0f, 200.0f));
}

// ---- Envelope: AT-24, AT-25 (+ the FR-009 ampStep record) ----

/// FR-009 record (T015's definition): a morph-driven arm on A_s (held note, 5 ms ramp from p = 0 to p = 1 at
/// 0.5 s) whose endpoints differ in one amp-envelope field; ampStep = max |g_amp[n] - g_amp[n-1]| over the 64
/// samples after each grid point where that receiver changed (bitwise, grid records), beside ampStep_ref of the
/// no-morph run at the same grid points.
[[nodiscard]] inline std::string auAmpStepArm(float MorphState::AmpEnvelopeFields::*field, float lo, float hi,
                                       std::uint8_t note) {
    MorphState a = auStateAs();
    a.ampEnv.*field = lo;
    MorphState b = auStateAs();
    b.ampEnv.*field = hi;
    const AuSetting s{.a = a, .b = b};
    auPt::VoiceEngineScript sc = auScript(s, note, auAt(1.0) + kAuGrid, 1);
    sc.events.push_back(auSetValue(0, AuKind::MorphRampMs, 5.0f));
    auPt::VoiceEngineScript ref = sc;
    sc.events.push_back(auSetValue(auAt(0.5), AuKind::MorphPosition, 1.0f));
    const auPt::VoiceEngineTaps tm = auPt::voiceEngineRender(sc);
    const auPt::VoiceEngineTaps tr = auPt::voiceEngineRender(ref);
    std::vector<std::size_t> points;
    for (std::size_t i = 1; i < tm.grid.size(); ++i) {
        if (!auSame(tm.grid[i].afterMotion.ampEnv.*field, tm.grid[i - 1].afterMotion.ampEnv.*field))
            points.push_back(static_cast<std::size_t>(tm.grid[i].sample));
    }
    const auto step = [&](const std::vector<float>& g) {
        double m = 0.0;
        for (const std::size_t p : points) {
            for (std::size_t n = p + 1; n <= p + kAuAmpStepSamples && n < g.size(); ++n)
                m = std::max(m, std::abs(static_cast<double>(g[n]) - static_cast<double>(g[n - 1])));
        }
        return m;
    };
    std::ostringstream os;
    os << std::setprecision(6) << step(tm.voices[0].gAmp) << " (ref " << step(tr.voices[0].gAmp) << "; "
       << points.size() << " grid points)";
    return os.str();
}

/// Phase 13 lift L6 (FR-057, plan §4.1): the AT-24 attack cell on built lo / hi settings.
[[nodiscard]] inline AuCell auCellAt24(const AuSetting& lo, const AuSetting& hi, const std::string& baseName, bool gated,
                                       const std::string& remark, std::uint8_t note, const std::string& ampStep) {
    const AuReading a = auRiseMs(lo, note);
    const AuReading b = auRiseMs(hi, note);
    AuCell c;
    c.row = "AT-24";
    c.contrast = "Amp envelope attack: 0.1 -> 500 ms";
    c.base = baseName;
    c.note = auNoteName(note);
    c.descriptor = "t_90 (ms): first 1 ms frame of the Sum's RMS at >= 90 % of its maximum (drafted-descriptor replacement, G14-8)";
    c.tap = "Sum";
    c.lo = a.value;
    c.hi = b.value;
    c.value = auRatio(a.value, b.value);
    c.bar = kAuBarRatio;
    c.barText = "ratio >= 2";
    c.pass = a.valid && b.valid && c.value >= c.bar;
    c.gated = gated;
    c.outDescriptor = "t_90 output (ms)";
    c.outLo = a.out;
    c.outHi = b.out;
    c.ampStep = ampStep;
    c.remark = remark;
    return c;
}

/// Phase 13 lift L7 (FR-057, plan §4.1): the AT-25 release cell on built lo / hi settings.
[[nodiscard]] inline AuCell auCellAt25(const AuSetting& lo, const AuSetting& hi, std::uint8_t note,
                                       const std::string& ampStep) {
    const AuReading a = auGateDt(lo, note, true);
    const AuReading b = auGateDt(hi, note, true);
    AuCell c;
    c.row = "AT-25";
    c.contrast = "Amp envelope release: 50 -> 2000 ms";
    c.base = kAuBaseAs;
    c.note = auNoteName(note);
    c.descriptor = "D_t release reading (ms: D_t of the gate render from the note-off grid point on)";
    c.tap = "Sum";
    c.lo = a.value;
    c.hi = b.value;
    c.value = auRatio(a.value, b.value);
    c.bar = kAuBarRatio;
    c.barText = "ratio >= 2";
    c.pass = a.valid && b.valid && c.value >= c.bar;
    c.outDescriptor = "D_t output release reading (ms; -1 where the output does not fall 80 dB)";
    c.outLo = a.out;
    c.outHi = b.out;
    c.ampStep = ampStep;
    return c;
}

inline void auShardEnvelope(std::vector<AuCell>& cells, std::uint8_t note) {
    // AT-24: per-row setting (FR-080 showcase-inert; spec table): plain A (decay 150 ms, sustain 0).
    {
        const std::string ampStep = auAmpStepArm(&MorphState::AmpEnvelopeFields::attackMs, 0.1f, 500.0f, note);
        const auto cell = [&](const MorphState& base, const std::string& baseName, bool gated,
                              const std::string& remark) {
            MorphState lo = base;
            lo.ampEnv.attackMs = 0.1f;
            MorphState hi = base;
            hi.ampEnv.attackMs = 500.0f;
            cells.push_back(auCellAt24(auOne(lo), auOne(hi), baseName, gated, remark, note, ampStep));
        };
        cell(auStateA(), "A (per-row setting: without C-8's sustain 1)", true,
             "FR-080 per-row setting (T016): on any sustain-1 base the 1 s gate hold dominates D_t, bounding the "
             "ratio at <= 1015 / 515 = 1.97 < 2 (A_s and B_s alike); on A the attack is the note");
        cell(auStateAs(), kAuBaseAs, false, "the C-8 base, recorded (analytically below the bar, see the gated line)");
    }
    // AT-25: release 50 -> 2000 ms, the release reading (D_t from the note-off grid point on).
    {
        MorphState lo = auStateAs();
        lo.ampEnv.releaseMs = 50.0f;
        MorphState hi = auStateAs();
        hi.ampEnv.releaseMs = 2000.0f;
        cells.push_back(auCellAt25(auOne(lo), auOne(hi), note,
                                   auAmpStepArm(&MorphState::AmpEnvelopeFields::releaseMs, 50.0f, 2000.0f, note)));
    }
}

// ---- Mass: AT-26 ... AT-28 (+ H-17) ----
/// Phase 13 lift L8 (FR-057, plan §4.1): the AT-27 dL_K reading (the former dlk lambda; the caller sets Mass)
/// and the AT-27 cell on built lo / hi settings (the former cell lambda).
[[nodiscard]] inline double auDlk(const AuSetting& s, std::uint8_t note, double& outLk) {
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(auScript(s, note, auSteadyTotal()));
    outLk = auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), kAuFs);
    return auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Dynamics), kAuFs)
           - auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Sum), kAuFs);
}

[[nodiscard]] inline AuCell auCellAt27(const AuSetting& lo, const AuSetting& hi, const std::string& baseName,
                                       std::uint8_t note) {
    AuCell c;
    c.row = "AT-27";
    c.contrast = "Mass dynamics, gain: Mass 0 -> 100";
    c.base = baseName;
    c.note = auNoteName(note);
    c.descriptor = "dL_K = L_K(dynamics output) - L_K(Sum) (dB)";
    c.tap = "dynamics output tap and Sum";
    c.lo = auDlk(lo, note, c.outLo);
    c.hi = auDlk(hi, note, c.outHi);
    c.value = std::abs(c.hi - c.lo);
    c.bar = kAuBarDb;
    c.barText = ">= 3 dB";
    c.pass = auFinite(c.value) && c.value >= c.bar;
    c.outDescriptor = "L_K output (dB)";
    return c;
}

/// Phase 13 lift L9 (FR-057, plan §4.1): the AT-28 onset reading (the former onset lambda; the caller sets
/// transientAttackDb) and the AT-28 cell on built lo / hi settings.
[[nodiscard]] inline double auOnsetDyn(const AuSetting& s, std::uint8_t note, double& outRatio) {
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(auScript(s, note, auAt(1.0)));
    outRatio = auOnsetRatioDb(t.outL, t.outR);
    return auOnsetRatioDb(t.dynL, t.dynR);
}

[[nodiscard]] inline AuCell auCellAt28(const AuSetting& lo, const AuSetting& hi, std::uint8_t note) {
    AuCell c;
    c.row = "AT-28";
    c.contrast = "Mass dynamics, transient: transientAttackDb 0 -> +12";
    c.base = "B";
    c.note = auNoteName(note);
    c.descriptor = "onset ratio: peak |y| over [0, 20 ms) over RMS over [100, 300) ms (dB)";
    c.tap = "dynamics output tap";
    c.lo = auOnsetDyn(lo, note, c.outLo);
    c.hi = auOnsetDyn(hi, note, c.outHi);
    c.value = std::abs(c.hi - c.lo);
    c.bar = kAuBarDb;
    c.barText = ">= 3 dB";
    c.pass = auFinite(c.value) && c.value >= c.bar;
    c.outDescriptor = "onset ratio of the output (dB)";
    return c;
}

inline void auShardMass(std::vector<AuCell>& cells, std::uint8_t note) {
    auCellsAt26(cells, note);
    // AT-27: dL_K = L_K(dynamics output) - L_K(Sum), compared between Mass 0 and 100, on B.
    {
        // One auStateB() / auStateBs() call each, as the former cell(auStateB(), ...) / cell(auStateBs(), ...)
        // calls made (same assertion count; FR-058 gate).
        const MorphState b = auStateB();
        const MorphState bs = auStateBs();
        MorphState b0 = b;
        b0.mass = 0.0f;
        MorphState b100 = b;
        b100.mass = 100.0f;
        MorphState bs0 = bs;
        bs0.mass = 0.0f;
        MorphState bs100 = bs;
        bs100.mass = 100.0f;
        // G14-8: B read 2.92 dB at C3 (E1 3.50); B_s (sustain 1) measured as the candidate per-row base.
        auGateOnEither(auCellAt27(auOne(b0), auOne(b100), "B", note),
                       auCellAt27(auOne(bs0), auOne(bs100), kAuBaseBs, note), cells, "B reads 2.92 dB at C3");
    }
    // AT-28: transientAttackDb 0 -> +12 on B, onset ratio of the dynamics output.
    {
        MorphState lo = auStateB();
        lo.transientAttackDb = 0.0f;
        MorphState hi = auStateB();
        hi.transientAttackDb = 12.0f;
        cells.push_back(auCellAt28(auOne(lo), auOne(hi), note));
    }
}

/// H-17 record: the steady sub-band GR (compressorGrDb grid field, median over [0.5 s, 1.5 s)) at Mass 100, base B.
inline void auH17Record() {
    MorphState b = auStateB();
    b.mass = 100.0f;
    REQUIRE(isValidMorphState(b));
    const auto gr = [&](ProfundumEngine::VoiceMode mode, std::initializer_list<std::uint8_t> notes) {
        auPt::VoiceEngineScript sc;
        sc.fs = kAuFs;
        sc.hostBlock = 64;
        sc.totalSamples = auAt(1.5) + kAuGrid;
        sc.a = b;
        sc.b = b;
        sc.mode = mode;
        sc.polyphony = 4;
        sc.tappedVoices = 0;
        for (const std::uint8_t n : notes)
            sc.events.push_back(auOn(0, n));
        const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(sc);
        std::vector<double> v;
        for (const auPt::VoiceEngineGridRecord& g : t.grid) {
            if (g.sample >= auAt(0.5) && g.sample < auAt(1.5))
                v.push_back(static_cast<double>(g.compressorGrDb));
        }
        return auMedian(v);
    };
    const double mono = gr(ProfundumEngine::VoiceMode::Mono, {kAuE1});
    const double poly = gr(ProfundumEngine::VoiceMode::Poly, {kAuE1, 29, 30, 31});
    std::ostringstream os;
    os << std::setprecision(6)
       << "# H-17 record (tasks.md T016; spec H-17, Phase 6 handoff (8)). Base B, Mass 100, 48 kHz; steady sub-band GR\n"
       << "# = median of the grid record's compressorGrDb over [0.5 s, 1.5 s). Phase 6: 4 voices at E1 detuned\n"
       << "# {-6, -2, +2, +6} cents read -10.1172 dB (specs/profundum-phase6-mass-dynamics/evidence/phase9-handoff.txt:42).\n"
       << "# DEVIATION: the engine's pitch API is MIDI notes plus ONE global bend (C-11: POLY f = midiNoteToFrequency(note)\n"
       << "# x bend ratio), so four voices detuned by cents around E1 (E1 +- 7 / +- 13 cents) are not reachable through it.\n"
       << "# Recorded instead: MONO E1 (one voice) and POLY 4 voices on the nearest distinct keys E1 / F1 / F#1 / G1.\n"
       << "H17 MONO E1 gr_db=" << mono << "\n"
       << "H17 POLY4 E1,F1,F#1,G1 gr_db=" << poly << "\n"
       << "H17 PHASE6 4x detuned E1 gr_db=-10.1172\n";
    std::cout << os.str();
    auWriteEvidence("h17-poly-gr.txt", os.str());
    CHECK(auFinite(mono));
    CHECK(auFinite(poly));
}

// ---- Field: AT-29 ... AT-33 ----

/// AT-33: frame-wise D_st peak-to-peak of the field tap, motionDepth lo vs hi, over [1 s, 1 s + 10 s).
[[nodiscard]] inline AuCell auFieldMotionCell(const MorphState& base, const std::string& baseName, std::uint8_t note,
                                       bool gated, const std::string& remark) {
    const double f0 = auHz(note);
    const auto from = static_cast<std::size_t>(auAt(kAuSettleS));
    const auto to = static_cast<std::size_t>(auAt(kAuSettleS + kAuFieldMotionSeconds));
    const auto read = [&](float depth, double& outP2p, std::size_t& invalid) {
        MorphState s = base;
        s.field.mode = BassStereoField::ImageMode::Spread;
        s.field.spread = 1.0f;
        s.field.motionDepth = depth;
        const auPt::VoiceEngineTaps t =
            auPt::voiceEngineRender(auScript(auOne(s), note, static_cast<std::uint64_t>(to) + kAuGrid));
        const double fc = static_cast<double>(s.field.crossoverHz);
        std::size_t outInvalid = 0;
        outP2p = auP2p(auDstFrames(t.outL, t.outR, f0, fc, from, to, outInvalid));
        return auP2p(auDstFrames(t.stageOutL, t.stageOutR, f0, fc, from, to, invalid));
    };
    AuCell c;
    std::size_t invLo = 0;
    std::size_t invHi = 0;
    c.row = "AT-33";
    c.contrast = "Field MOTION: motionDepth 0 -> 1 (SPREAD, spread 1)";
    c.base = baseName;
    c.note = auNoteName(note);
    c.descriptor = "frame-wise D_st p2p (dB; S9.11 frames, band validity G8-1)";
    c.tap = "field stage tap";
    c.lo = read(0.0f, c.outLo, invLo);
    c.hi = read(1.0f, c.outHi, invHi);
    c.value = std::abs(c.hi - c.lo);
    c.bar = kAuBarDb;
    c.barText = ">= 3 dB";
    c.pass = invLo == 0 && invHi == 0 && auFinite(c.value) && c.value >= c.bar;
    c.gated = gated;
    c.outDescriptor = "frame-wise D_st p2p of the output (dB)";
    c.remark = remark + (invLo + invHi > 0 ? " | frames below the G8-1 band floor: " + std::to_string(invLo) + " / "
                                                 + std::to_string(invHi)
                                           : "");
    return c;
}

inline void auShardField(std::vector<AuCell>& cells, std::uint8_t note) {
    constexpr const char* kPredicted =
        "PREDICTED INERT on A_s: the sine core has no partial above 2 f_c (Phase 7 G8-1, Phase 8 B10-4); the B_s "
        "line below is recorded for a B10-1-style per-row ruling";
    // G14-8 (main loop 2026-10-21): every row measured on A_s and B_s and gated by FR-080's rule (auGateOnEither);
    // AT-29 / AT-31 read SOLID's side (exactly 0) against the G8-1 floor (auFloorRule); AT-32 reads the band Gravity
    // governs (D_st below f_c, auDstLow); AT-30 runs WIDE at spread 1 (WIDE's offsets scale with Spread, Phase 7
    // FR-040: at spread 0 they are {+0, +0} and the first run read -120 / -120 dBFS).
    (void)kPredicted;
    const auto at29 = [&](const MorphState& base, const std::string& baseName) {
        MorphState lo = base;
        lo.field.mode = BassStereoField::ImageMode::Solid;
        MorphState hi = base;
        hi.field.mode = BassStereoField::ImageMode::Spread;
        hi.field.spread = 1.0f;
        AuCell c = auSteadyCell("AT-29", "Image mode: SOLID -> SPREAD (spread 1)", baseName, note, AuDesc::DSt,
                                "field stage tap", auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB");
        auFloorRule(c);
        return c;
    };
    const auto at31 = [&](const MorphState& base, const std::string& baseName) {
        MorphState lo = base;
        lo.field.mode = BassStereoField::ImageMode::Spread;
        lo.field.spread = 0.0f;
        MorphState hi = lo;
        hi.field.spread = 1.0f;
        AuCell c = auSteadyCell("AT-31", "Spread: 0 -> 1 (SPREAD)", baseName, note, AuDesc::DSt, "field stage tap",
                                auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB");
        auFloorRule(c);
        return c;
    };
    const auto at32 = [&](const MorphState& base, const std::string& baseName) {
        MorphState lo = base;
        lo.field.mode = BassStereoField::ImageMode::Spread;
        lo.field.spread = 1.0f;
        lo.field.gravity = 0.0f;
        MorphState hi = lo;
        hi.field.gravity = 1.0f;
        AuCell c = auSteadyCell("AT-32", "Gravity: 0 -> 1 (SPREAD, spread 1)", baseName, note, AuDesc::DStLow,
                                "field stage tap", auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB");
        if (auHz(note) >= static_cast<double>(base.field.crossoverHz)) {   // round 2: no partial in the band Gravity governs
            c.gated = false;
            c.remark = "recorded: f0 " + std::to_string(auHz(note)) + " Hz is above f_c " +
                       std::to_string(base.field.crossoverHz) + " Hz, so no partial lies in the band Gravity governs";
        }
        return c;
    };
    auGateOnEither(at29(auStateAs(), kAuBaseAs), at29(auStateBs(), kAuBaseBs), cells, "the sine core has no partial above 2 f_c");
    auGateOnEither(at31(auStateAs(), kAuBaseAs), at31(auStateBs(), kAuBaseBs), cells, "the sine core has no partial above 2 f_c");
    auGateOnEither(at32(auStateAs(), kAuBaseAs), at32(auStateBs(), kAuBaseBs), cells, "the sine core has one partial");
    auGateOnEither(auFieldMotionCell(auStateAs(), kAuBaseAs, note, true, ""),
                   auFieldMotionCell(auStateBs(), kAuBaseBs, note, true, ""), cells,
                   "the sine core has no partial above 2 f_c (2.4 dB at E1)");
    // AT-30 (spec base A_s + Rectify 0.5, so WIDE's per-channel Character offsets act on a running stage).
    {
        MorphState lo = auWith(auStateAs(), AuAlg::Rectify, 0.5f);
        lo.field.mode = BassStereoField::ImageMode::Solid;
        MorphState hi = lo;
        hi.field.mode = BassStereoField::ImageMode::Wide;
        hi.field.spread = 1.0f;   // G14-8: WIDE's offsets scale with Spread (Phase 7 FR-040)
        AuCell c = auSteadyCell("AT-30", "Image mode: SOLID -> WIDE (spread 1)",
                                std::string(kAuBaseAs) + " + Character Rectify at Amount 0.5 + spread 1", note,
                                AuDesc::DSt, "field stage tap", auOne(lo), auOne(hi), kAuBarDb, ">= 3 dB");
        auFloorRule(c);
        cells.push_back(c);
    }
}

// ---- Motion: AT-34 ... AT-40 ----

/// A frame-wise peak-to-peak cell: lo = base with Motion Off, hi = base with `motion`; both with `extraEvents`.
[[nodiscard]] inline AuCell auMotionCell(const std::string& row, const std::string& contrast, const MorphState& base,
                                  const std::string& baseName, std::uint8_t note, const AuMotion& motion,
                                  AuFrameDesc d, double seconds, const std::vector<auPt::VoiceEngineEvent>& extraEvents,
                                  double bar, const std::string& barText, bool gated, const std::string& remark) {
    const double f0 = auHz(note);
    const auto from = static_cast<std::size_t>(auAt(kAuSettleS));
    const auto to = static_cast<std::size_t>(auAt(kAuSettleS + seconds));
    // Round 2 (G14-8): with retrigger events (AT-39: note-off every 2 s, note-on 64 samples later) the frame p2p of
    // both arms was the release / attack itself (12.5 dB on the Off arm). The series is read per held span: frames
    // starting >= kAuSettleS after each note-on and ending before the next note-off; the p2p is the largest over spans.
    std::vector<std::uint64_t> ons{0};
    std::vector<std::uint64_t> offs;
    for (const auPt::VoiceEngineEvent& e : extraEvents) {
        if (e.kind == AuKind::NoteOn)
            ons.push_back(e.sample);
        else if (e.kind == AuKind::NoteOff)
            offs.push_back(e.sample);
    }
    std::sort(ons.begin(), ons.end());
    std::sort(offs.begin(), offs.end());
    const auto read = [&](const MorphState& s, double& outP2p) {
        auPt::VoiceEngineScript sc = auScript(auOne(s), note, static_cast<std::uint64_t>(to) + kAuGrid);
        for (const auPt::VoiceEngineEvent& e : extraEvents)
            sc.events.push_back(e);
        const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(sc);
        if (extraEvents.empty()) {
            outP2p = auP2p(auFrameSeries(t.outL, t.outR, f0, from, to, d));
            return auP2p(auFrameSeries(t.sumL, t.sumR, f0, from, to, d));
        }
        double worstSum = 0.0, worstOut = 0.0;
        for (std::size_t k = 0; k < ons.size(); ++k) {
            // Round 3: the ENV source is 10 ms attack / 300 ms decay (MotionConfig defaults), so the Mass swing sits
            // in the first 300 ms after each retrigger; the span starts two grid steps after the note-on.
            const std::size_t spanFrom = static_cast<std::size_t>(ons[k]) + 2 * static_cast<std::size_t>(kAuGrid);
            std::size_t spanTo = to;
            for (const std::uint64_t off : offs)
                if (off > ons[k]) { spanTo = std::min(spanTo, static_cast<std::size_t>(off)); break; }
            if (spanFrom >= spanTo)
                continue;
            worstSum = std::max(worstSum, auP2p(auFrameSeries(t.sumL, t.sumR, f0, spanFrom, spanTo, d)));
            worstOut = std::max(worstOut, auP2p(auFrameSeries(t.outL, t.outR, f0, spanFrom, spanTo, d)));
        }
        outP2p = worstOut;
        return worstSum;
    };
    MorphState lo = base;
    lo.motion.source = AuMotion::Source::Off;
    MorphState hi = base;
    hi.motion = motion;
    AuCell c;
    c.row = row;
    c.contrast = contrast;
    c.base = baseName;
    c.note = auNoteName(note);
    c.descriptor = auFrameDescName(d);
    c.tap = "Sum";
    c.lo = read(lo, c.outLo);
    c.hi = read(hi, c.outHi);
    c.value = std::abs(c.hi - c.lo);
    c.bar = bar;
    c.barText = barText;
    c.pass = auFinite(c.value) && c.value >= bar;
    c.gated = gated;
    c.outDescriptor = std::string(auFrameDescName(d)) + " of the output";
    c.remark = remark;
    return c;
}

[[nodiscard]] inline AuMotion auCycle(AuMotion::Target target, double rateHz) {
    AuMotion m{};
    m.source = AuMotion::Source::Cycle;
    m.target = target;
    m.amount = 1.0f;
    m.depth = 1.0f;
    m.cycleRateHz = static_cast<float>(rateHz);
    m.polarity = AuMotion::Polarity::Bipolar;
    return m;
}

/// ENV retriggered every 2 s: the note released and re-pressed (note-off at k 2 s, note-on 64 samples later).
[[nodiscard]] inline std::vector<auPt::VoiceEngineEvent> auEnvRetriggers(std::uint8_t note, double seconds) {
    std::vector<auPt::VoiceEngineEvent> ev;
    for (double t = kAuEnvRetriggerS; t < kAuSettleS + seconds; t += kAuEnvRetriggerS) {
        ev.push_back(auOff(auAt(t), note));
        ev.push_back(auOn(auAt(t) + kAuRetriggerGapSamples, note));
    }
    return ev;
}

inline void auShardMotion(std::vector<AuCell>& cells, std::uint8_t note) {
    const double cycleSeconds = std::max(kAuMotionCycles / kAuMotionRateHz, 3.0);
    struct TargetRow {
        const char* row;
        AuMotion::Target target;
        const char* name;
        AuFrameDesc d;
        double bar;
        const char* barText;
        bool predictedInert;
    };
    const std::array<TargetRow, 5> rows{{
        {"AT-34", AuMotion::Target::Mass, "Mass", AuFrameDesc::ESub, kAuBarDb, ">= 3 dB", false},
        {"AT-35", AuMotion::Target::Depth, "Depth", AuFrameDesc::ESub, kAuBarDb, ">= 3 dB", false},
        {"AT-36", AuMotion::Target::Body, "Body", AuFrameDesc::DCx, kAuBarDb, ">= 3 dB", false},
        {"AT-37", AuMotion::Target::Edge, "Edge", AuFrameDesc::DBr, kAuBarOct, ">= 0.25 oct", true},
        {"AT-38", AuMotion::Target::Shift, "Shift", AuFrameDesc::DBr, kAuBarOct, ">= 0.25 oct", true},
    }};
    // G14-8 (main loop 2026-10-21): the Mass-target row runs on the AT-26 per-row setting (A_s + anchor level 0.5;
    // Mass moves the Anchor over its full row there, plain A_s read 2.67 dB), plain A_s and B_s recorded; the
    // spectral-target rows are measured on A_s and B_s and gated by FR-080's rule (a sine has no spectrum to move).
    MorphState asAnchor = auStateAs();
    asAnchor.anchor.level = kAuAt26AnchorLevel;
    const std::string kAuBaseAsAnchor = std::string(kAuBaseAs) + " + anchor level 0.5 (per-row setting, G11-2 / G14-8)";
    for (const TargetRow& r : rows) {
        const std::string contrast =
            std::string("Motion target ") + r.name + ": Motion Off -> CYCLE 0.1 Hz, amount 1, Motion Depth 1";
        (void)r.predictedInert;
        if (r.target == AuMotion::Target::Mass) {
            cells.push_back(auMotionCell(r.row, contrast, asAnchor, kAuBaseAsAnchor, note,
                                         auCycle(r.target, kAuMotionRateHz), r.d, cycleSeconds, {}, r.bar, r.barText,
                                         true, "the AT-26 per-row setting (G11-2)"));
            cells.push_back(auMotionCell(r.row, contrast, auStateAs(), kAuBaseAs, note,
                                         auCycle(r.target, kAuMotionRateHz), r.d, cycleSeconds, {}, r.bar, r.barText,
                                         false, "recorded: plain A_s (anchor level 1.0 at the AnchorLevel row's clamp)"));
            cells.push_back(auMotionCell(r.row, contrast, auStateBs(), kAuBaseBs, note,
                                         auCycle(r.target, kAuMotionRateHz), r.d, cycleSeconds, {}, r.bar, r.barText,
                                         false, "recorded: B_s"));
            continue;
        }
        if (r.target == AuMotion::Target::Edge) {
            // Round 2 (G14-8): Edge read 0.13 / 0.18 oct on B_s (B's edge is 1.0, so the bipolar swing clamps at the
            // top; A_s at depth 1 is a sine). Candidate: A_s with the kSawAnchor recipe at edge 0.5, the full 0 .. 1 swing.
            MorphState sawEdge = auStateAs();
            sawEdge.recipe = SpectralShapeRecipe::kSawAnchor;
            sawEdge.recipe.edge = 0.5f;
            REQUIRE(isValidMorphState(sawEdge));
            auGateOnFirst({auMotionCell(r.row, contrast, auStateAs(), kAuBaseAs, note, auCycle(r.target, kAuMotionRateHz),
                                        r.d, cycleSeconds, {}, r.bar, r.barText, true, ""),
                           auMotionCell(r.row, contrast, auStateBs(), kAuBaseBs, note, auCycle(r.target, kAuMotionRateHz),
                                        r.d, cycleSeconds, {}, r.bar, r.barText, true, ""),
                           auMotionCell(r.row, contrast, sawEdge,
                                        std::string(kAuBaseAs) + " + recipe kSawAnchor at edge 0.5 (per-row setting candidate)",
                                        note, auCycle(r.target, kAuMotionRateHz), r.d, cycleSeconds, {}, r.bar, r.barText,
                                        true, "")},
                          cells, "A_s is a sine; B's edge 1.0 clamps the bipolar swing at the top");
            continue;
        }
        auGateOnEither(auMotionCell(r.row, contrast, auStateAs(), kAuBaseAs, note, auCycle(r.target, kAuMotionRateHz),
                                    r.d, cycleSeconds, {}, r.bar, r.barText, true, ""),
                       auMotionCell(r.row, contrast, auStateBs(), kAuBaseBs, note, auCycle(r.target, kAuMotionRateHz),
                                    r.d, cycleSeconds, {}, r.bar, r.barText, true, ""),
                       cells, "A_s is a sine: no spectrum for the target to move");
    }
    {
        AuMotion env{};
        env.source = AuMotion::Source::Env;
        env.target = AuMotion::Target::Mass;
        env.amount = 1.0f;
        env.depth = 1.0f;
        env.polarity = AuMotion::Polarity::Bipolar;
        // Round 4: the ENV defaults are 10 ms attack / 300 ms decay, and the 8-period frame at E1 is 194 ms, so the
        // boxcar takes most of the swing (E1 1.51 dB, C3 3.14 dB); the candidate decays over 1000 ms.
        AuMotion envLong = env;
        envLong.envDecayMs = 1000.0f;
        auGateOnFirst({auMotionCell("AT-39", "Motion source -> Mass: Off -> ENV (retriggered every 2 s)", asAnchor,
                                    kAuBaseAsAnchor, note, env, AuFrameDesc::ESub, kAuEnvSeconds,
                                    auEnvRetriggers(note, kAuEnvSeconds), kAuBarDb, ">= 3 dB", true,
                                    "retrigger = note-off at k 2 s, note-on 64 samples later (both arms); the AT-26 per-row setting (G11-2)"),
                       auMotionCell("AT-39", "Motion source -> Mass: Off -> ENV (10 ms / 1000 ms, retriggered every 2 s)", asAnchor,
                                    kAuBaseAsAnchor + " + ENV decay 1000 ms (per-row setting candidate)", note, envLong,
                                    AuFrameDesc::ESub, kAuEnvSeconds, auEnvRetriggers(note, kAuEnvSeconds), kAuBarDb,
                                    ">= 3 dB", true, "the 194 ms frame at E1 takes most of a 300 ms decay")},
                      cells, "the 8-period frame boxcar attenuates the 300 ms default decay at E1");
        cells.push_back(auMotionCell("AT-39", "Motion source -> Mass: Off -> ENV (retriggered every 2 s)", auStateAs(),
                                     kAuBaseAs, note, env, AuFrameDesc::ESub, kAuEnvSeconds,
                                     auEnvRetriggers(note, kAuEnvSeconds), kAuBarDb, ">= 3 dB", false,
                                     "recorded: plain A_s"));
        cells.push_back(auMotionCell("AT-39", "Motion source -> Mass: Off -> ENV (retriggered every 2 s)", auStateBs(),
                                     kAuBaseBs, note, env, AuFrameDesc::ESub, kAuEnvSeconds,
                                     auEnvRetriggers(note, kAuEnvSeconds), kAuBarDb, ">= 3 dB", false,
                                     "recorded: B_s"));
    }
    {
        AuMotion drift{};
        drift.source = AuMotion::Source::Drift;
        drift.target = AuMotion::Target::Mass;
        drift.amount = 1.0f;
        drift.depth = 1.0f;
        drift.driftSmoothness = 0.5f;
        drift.polarity = AuMotion::Polarity::Bipolar;
        cells.push_back(auMotionCell("AT-40", "Motion source -> Mass: Off -> DRIFT (smoothness 0.5)", asAnchor,
                                     kAuBaseAsAnchor, note, drift, AuFrameDesc::ESub, kAuDriftSeconds, {}, kAuBarDb,
                                     ">= 3 dB", true, "the AT-26 per-row setting (G11-2)"));
        cells.push_back(auMotionCell("AT-40", "Motion source -> Mass: Off -> DRIFT (smoothness 0.5)", auStateAs(),
                                     kAuBaseAs, note, drift, AuFrameDesc::ESub, kAuDriftSeconds, {}, kAuBarDb,
                                     ">= 3 dB", false, "recorded: plain A_s"));
        cells.push_back(auMotionCell("AT-40", "Motion source -> Mass: Off -> DRIFT (smoothness 0.5)", auStateBs(),
                                     kAuBaseBs, note, drift, AuFrameDesc::ESub, kAuDriftSeconds, {}, kAuBarDb,
                                     ">= 3 dB", false, "recorded: B_s"));
    }
}

// ---- Morph: AT-41, AT-42 ----
inline void auShardMorph(std::vector<AuCell>& cells, std::uint8_t note) {
    const AuSetting lo{.a = auStateAs(), .b = auStateB(), .position = 0.0f};
    for (const float p : {1.0f, 0.5f}) {
        AuSetting hi = lo;
        hi.position = p;
        cells.push_back(auSteadyCell(p == 1.0f ? "AT-41" : "AT-42",
                                     p == 1.0f ? "Morph position: 0 -> 1" : "Morph position: 0 -> 0.5",
                                     "showcase pair (A_s (C-8), B)", note, AuDesc::DNl, "Char / twin", lo, hi,
                                     kAuBarDb, ">= 3 dB"));
    }
}

// ---- Note Memory: AT-43 ... AT-46 ----

enum class AuTransition : std::uint8_t { Overlap, Tail };

/// The transition grid point in [0.5 s, 0.5 s + one period of the first note) whose predicted phase jump between a
/// restarting and a continuing oscillator, frac(f1 g / fs + f2 L_d / fs) cycles (the restart lands L_d samples
/// after the grid point, behind the declick dip), is nearest to half a cycle.
[[nodiscard]] inline std::uint64_t auPickTransition(double f1, double f2) {
    const std::uint64_t g0 = auAt(0.5);
    const auto period = static_cast<std::uint64_t>(std::ceil(kAuFs / f1));
    const double ld = static_cast<double>(auPt::voiceEngineDeclickLength(kAuFs));
    std::uint64_t best = g0;
    double bestErr = 1.0;
    for (std::uint64_t g = g0; g <= g0 + period; g += kAuGrid) {
        double c = f1 * static_cast<double>(g) / kAuFs + f2 * ld / kAuFs;
        c -= std::floor(c);
        const double err = std::abs(c - 0.5);
        if (err < bestErr) {
            bestErr = err;
            best = g;
        }
    }
    return best;
}

struct AuPhase {
    double phaseRad = 0.0;
    double outLk = 0.0;
};

/// MONO on A_s, Retrigger Reset, Note Memory mode `m`: `note` from 0; Overlap = the octave above pressed at t1
/// with `note` held; Tail = `note` released 10 ms before t1, the octave above pressed at t1. The core-h1 Goertzel
/// phase at the new f0 over 4 periods from t1 + 20 ms (after the 3 ms dip), on the summed core tap.
[[nodiscard]] inline AuPhase auNoteMemoryPhase(const MorphState& base, NoteMemory::Mode m, AuTransition kind,
                                               std::uint8_t note, std::uint64_t t1) {
    const std::uint8_t target = auUpOctave(note);
    const double f2 = auHz(target);
    const auto len = static_cast<std::size_t>(std::llround(4.0 * kAuFs / f2));
    const std::uint64_t ws = t1 + auAt(0.020);
    auPt::VoiceEngineScript sc = auScript(auOne(base), note, ws + len + kAuGrid);
    sc.events.push_back(auSetIndex(0, AuKind::NoteMemoryMode, static_cast<int>(m)));
    sc.events.push_back(auSetIndex(0, AuKind::NoteRetrigger, static_cast<int>(NoteMemory::Retrigger::Reset)));
    if (kind == AuTransition::Tail)
        sc.events.push_back(auOff(t1 - auAt(0.010), note));
    sc.events.push_back(auOn(t1, target));
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRender(sc);
    const std::vector<float> core = auBm::midOf(t.coreL, t.coreR);
    REQUIRE(ws + len <= core.size());
    AuPhase r;
    r.phaseRad = auPt::goertzelPhase(core.data() + ws, len, kAuFs, f2);
    r.outLk = auLkOutShort(t);   // short render (G14-8)
    return r;
}

/// Phase 13 lift L10 (FR-057, plan §4.1): one AT-43 ... AT-46 row (the former local Row), the four rows, and
/// the row's cell on `base`.
struct AuNoteMemoryRow {
    const char* row;
    NoteMemory::Mode mode;
    const char* modeName;
    NoteMemory::Mode ref;
    const char* refName;
    AuTransition kind;
    const char* kindName;
    const char* actions;
};

[[nodiscard]] inline const std::array<AuNoteMemoryRow, 4>& auNoteMemoryRows() {
    using M = NoteMemory::Mode;
    // note_memory.h kMatrix (osc, env): Overlap AR 11 / ERO 01; Tail AR, Legato 11 / ERO, OC 01. Every pair
    // differs in the oscillator restart only.
    static const std::array<AuNoteMemoryRow, 4> rows{{
        {"AT-43", M::AlwaysRetrigger, "AlwaysRetrigger", M::EnvelopeRetriggerOnly, "EnvelopeRetriggerOnly",
         AuTransition::Overlap, "Overlap", "(osc 1, env 1) vs (osc 0, env 1)"},
        {"AT-44", M::Legato, "Legato", M::OscillatorContinuous, "OscillatorContinuous", AuTransition::Tail, "Tail",
         "(osc 1, env 1) vs (osc 0, env 1)"},
        {"AT-45", M::EnvelopeRetriggerOnly, "EnvelopeRetriggerOnly", M::Legato, "Legato", AuTransition::Tail, "Tail",
         "(osc 0, env 1) vs (osc 1, env 1)"},
        {"AT-46", M::OscillatorContinuous, "OscillatorContinuous", M::AlwaysRetrigger, "AlwaysRetrigger",
         AuTransition::Tail, "Tail", "(osc 0, env 1) vs (osc 1, env 1)"},
    }};
    return rows;
}

[[nodiscard]] inline AuCell auCellNoteMemory(const MorphState& baseM, const MorphState& baseRef,
                                             const AuNoteMemoryRow& r, std::uint8_t note, std::uint64_t t1) {
    const AuPhase pm = auNoteMemoryPhase(baseM, r.mode, r.kind, note, t1);
    const AuPhase pr = auNoteMemoryPhase(baseRef, r.ref, r.kind, note, t1);
    double d = pm.phaseRad - pr.phaseRad;
    d -= auPt::kTwoPi * std::round(d / auPt::kTwoPi);
    AuCell c;
    c.row = r.row;
    c.contrast = std::string("Note Memory ") + r.modeName + " vs M_ref " + r.refName + ", " + r.kindName + " "
                 + auNoteName(note) + " -> " + auNoteName(auUpOctave(note)) + " at sample " + std::to_string(t1)
                 + ", Retrigger Reset, MONO";
    c.base = kAuBaseAs;
    c.note = auNoteName(note);
    c.descriptor = "core-h1 Goertzel phase jump |wrap(phi_M - phi_ref)| (deg; 4 periods from t1 + 20 ms)";
    c.tap = "core tap";
    c.lo = pr.phaseRad * 180.0 / (0.5 * auPt::kTwoPi);
    c.hi = pm.phaseRad * 180.0 / (0.5 * auPt::kTwoPi);
    c.value = std::abs(d) * 180.0 / (0.5 * auPt::kTwoPi);
    c.bar = kAuBarPhaseDeg;
    c.barText = ">= 30 deg (phase reading)";
    c.pass = auFinite(c.value) && c.value >= c.bar;
    c.outDescriptor = "L_K output (dB)";
    c.outLo = pr.outLk;
    c.outHi = pm.outLk;
    c.remark = std::string("resolved Actions ") + r.actions
               + ": the pair differs in the oscillator restart only (phase reset + declick); envelope restart "
                 "from sustain 1 is silent on A_s (C-8), Drop amount 0";
    return c;
}

inline void auShardNoteMemory(std::vector<AuCell>& cells, std::uint8_t note) {
    const std::uint64_t t1 = auPickTransition(auHz(note), auHz(auUpOctave(note)));
    for (const AuNoteMemoryRow& r : auNoteMemoryRows()) {
        cells.push_back(auCellNoteMemory(auStateAs(), auStateAs(), r, note, t1));
    }
}

// ---- Macros: AT-47 ... AT-50, and the SC-007 sweep ----

enum class AuAxis : std::uint8_t { ESub, Dbr, Dt, Dnl };

[[nodiscard]] inline AuAxis auAxisOf(ProfundumMacro m) noexcept {
    switch (m) {
        case ProfundumMacro::Weight: return AuAxis::ESub;
        case ProfundumMacro::DarkBright: return AuAxis::Dbr;
        case ProfundumMacro::TightLoose: return AuAxis::Dt;
        case ProfundumMacro::CleanViolent:
        case ProfundumMacro::Count: break;
    }
    return AuAxis::Dnl;
}

[[nodiscard]] inline const char* auAxisName(AuAxis a) noexcept {
    switch (a) {
        case AuAxis::ESub: return "E_sub (dB)";
        case AuAxis::Dbr: return "D_br (oct)";
        case AuAxis::Dt: return "D_t (ms, gate render)";
        case AuAxis::Dnl: return "D_nl (dB)";
    }
    return "?";
}

[[nodiscard]] inline const char* auAxisTap(AuAxis a) noexcept {
    return a == AuAxis::Dnl ? "Char / twin" : "Sum";
}

inline constexpr std::array<const char*, ProfundumMacroMatrix::kNumMacros> kAuMacroNames{"Weight", "Dark <-> Bright",
                                                                                  "Tight <-> Loose",
                                                                                  "Clean <-> Violent"};

/// The macro's harvest base: A_s, B_s for Dark <-> Bright (ruling B10-1).
[[nodiscard]] inline MorphState auMacroBase(ProfundumMacro m) {
    return m == ProfundumMacro::DarkBright ? auStateBs() : auStateAs();
}

/// Phase 13 lift L11 (FR-057, plan §4.1): macro m's axis reading on a built setting (the former auMacroRead switch).
[[nodiscard]] inline AuReading auMacroAxisRead(const AuSetting& s, ProfundumMacro m, std::uint8_t note) {
    switch (auAxisOf(m)) {
        case AuAxis::ESub: return auSteady(s, note, AuDesc::ESub);
        case AuAxis::Dbr: return auSteady(s, note, AuDesc::DBr);
        case AuAxis::Dt: return auGateDt(s, note, false);
        case AuAxis::Dnl: return auSteady(s, note, AuDesc::DNl);
    }
    return {};
}

/// One reading of macro m's axis at value v (the others neutral) on `base` at `note`, through the engine's matrix.
[[nodiscard]] inline AuReading auMacroRead(const MorphState& base, ProfundumMacro m, float v, std::uint8_t note) {
    AuSetting s = auOne(base);
    s.macros = auMacroAt(m, v);
    return auMacroAxisRead(s, m, note);
}

[[nodiscard]] inline double auAxisBar(AuAxis a) noexcept {
    switch (a) {
        case AuAxis::ESub:
        case AuAxis::Dnl: return kAuBarDb;
        case AuAxis::Dbr: return kAuBarOct;
        case AuAxis::Dt: return kAuBarRatio;
    }
    return kAuBarDb;
}

/// Phase 13 lift L11 (FR-057, plan §4.1): the AT-47 ... AT-50 cell on built lo / hi settings (the former
/// auMacroCell body).
[[nodiscard]] inline AuCell auMacroCellOn(ProfundumMacro m, const AuSetting& lo, const AuSetting& hi,
                                          const std::string& baseName, std::uint8_t note, bool gated,
                                          const std::string& remark) {
    const AuAxis axis = auAxisOf(m);
    const AuReading a = auMacroAxisRead(lo, m, note);
    const AuReading b = auMacroAxisRead(hi, m, note);
    AuCell c;
    c.row = "AT-" + std::to_string(47 + static_cast<int>(m));
    c.contrast = std::string("Macro ") + kAuMacroNames[static_cast<std::size_t>(m)] + ": 0 -> 1, the others at neutral";
    c.base = baseName;
    c.note = auNoteName(note);
    c.descriptor = std::string("the SC-007 axis: ") + auAxisName(axis);
    c.tap = auAxisTap(axis);
    c.lo = a.value;
    c.hi = b.value;
    if (axis == AuAxis::Dt)
        c.value = a.value > 0.0 ? b.value / a.value : 0.0;
    else
        c.value = b.value - a.value;
    c.bar = auAxisBar(axis);
    if (axis == AuAxis::Dt)
        c.barText = "ratio >= 2 (hi / lo)";
    else if (axis == AuAxis::Dbr)
        c.barText = ">= 0.25 oct (hi - lo)";
    else
        c.barText = ">= 3 dB (hi - lo)";
    c.pass = a.valid && b.valid && auFinite(c.value) && c.value >= c.bar;
    c.gated = gated;
    c.outDescriptor = axis == AuAxis::Dnl ? "L_K output (dB)" : std::string(auAxisName(axis)) + " of the output";
    c.outLo = a.out;
    c.outHi = b.out;
    c.remark = remark;
    return c;
}

[[nodiscard]] inline AuCell auMacroCell(ProfundumMacro m, const MorphState& base, const std::string& baseName,
                                 std::uint8_t note, bool gated, const std::string& remark) {
    AuSetting lo = auOne(base);
    lo.macros = auMacroAt(m, 0.0f);
    AuSetting hi = auOne(base);
    hi.macros = auMacroAt(m, 1.0f);
    return auMacroCellOn(m, lo, hi, baseName, note, gated, remark);
}

inline void auShardMacros(std::vector<AuCell>& cells, std::uint8_t note) {
    for (std::size_t mi = 0; mi < ProfundumMacroMatrix::kNumMacros; ++mi) {
        const auto m = static_cast<ProfundumMacro>(mi);
        if (m == ProfundumMacro::DarkBright) {
            cells.push_back(auMacroCell(m, auStateBs(), kAuBaseBs, note, true, "per-row base, ruling B10-1"));
            cells.push_back(auMacroCell(m, auStateAs(), kAuBaseAs, note, false,
                                        "A_s reading, recorded (ungated): the macro is inert on A by construction "
                                        "(B10-1)"));
        } else {
            cells.push_back(auMacroCell(m, auStateAs(), kAuBaseAs, note, true, ""));
        }
    }
}

/// SC-007 running-maximum test of one step.
[[nodiscard]] inline bool auWithinEps(AuAxis axis, double value, double runMax) noexcept {
    switch (axis) {
        case AuAxis::ESub:
        case AuAxis::Dnl: return value >= runMax - kAuEpsDb;
        case AuAxis::Dbr: return value >= runMax - kAuEpsOct;
        case AuAxis::Dt: return value >= kAuEpsRatio * runMax;
    }
    return false;
}

struct AuSweepLine {
    std::string macro, note;
    float value = 0.0f;
    double reading = 0.0;
    double runMax = 0.0;
    bool valid = false;
    bool pass = false;
};

/// SC-007: macro m from 0 to 1 in `steps` steps (the others neutral) at `note`; appends one line per step.
inline void auSweep(ProfundumMacro m, std::uint8_t note, std::size_t steps, std::vector<AuSweepLine>& lines) {
    REQUIRE(steps >= 2);
    const AuAxis axis = auAxisOf(m);
    const MorphState base = auMacroBase(m);
    double runMax = 0.0;
    bool haveMax = false;
    for (std::size_t i = 0; i < steps; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(steps - 1);
        const AuReading r = auMacroRead(base, m, v, note);
        AuSweepLine l;
        l.macro = kAuMacroNames[static_cast<std::size_t>(m)];
        l.note = auNoteName(note);
        l.value = v;
        l.reading = r.value;
        l.valid = r.valid;
        l.runMax = haveMax ? runMax : r.value;
        l.pass = r.valid && (!haveMax || auWithinEps(axis, r.value, runMax));
        if (r.valid) {
            runMax = haveMax ? std::max(runMax, r.value) : r.value;
            haveMax = true;
        }
        lines.push_back(l);
    }
}

[[nodiscard]] inline std::string auSweepCsv(const std::vector<AuSweepLine>& lines) {
    std::ostringstream os;
    os << std::setprecision(6);
    os << "# SC-007 VoiceEngine_MacroMonotone: each macro 0 -> 1, the others neutral; base A_s (C-8), B_s for Dark "
          "<-> Bright (B10-1); eps 0.25 dB / 0.02 oct / 2 %; D(p_j) >= max_{i<j} D(p_i) - eps\n";
    os << "macro,note,macro_value,axis_reading,running_max_before,valid,pass\n";
    for (const AuSweepLine& l : lines) {
        os << '"' << l.macro << "\"," << l.note << ',' << l.value << ',' << l.reading << ',' << l.runMax << ','
           << (l.valid ? 1 : 0) << ',' << (l.pass ? "PASS" : "FAIL") << '\n';
    }
    return os.str();
}

inline void auSweepReport(const std::string& relPath, const std::vector<AuSweepLine>& lines) {
    const std::string csv = auSweepCsv(lines);
    std::cout << csv << std::flush;
    auWriteEvidence(relPath, csv);
    for (const AuSweepLine& l : lines) {
        INFO(l.macro << " " << l.note << " macro " << l.value << ": reading " << l.reading << " running max "
                     << l.runMax << (l.valid ? "" : " (invalid reading)"));
        REQUIRE(l.pass);
    }
}

// ---- SC-009: Motion -> Mass swell ----

struct AuSwell {
    double eSubP2p = 0.0, dNlP2p = 0.0, lKP2p = 0.0, boxcar = 0.0, seconds = 0.0;
    std::size_t frames = 0;
};

/// A_s (Mass 50) at E1 with `motion` on Mass; 1 s settle + `seconds`; frames per S9.11 over [settle, settle +
/// seconds); E_sub and L_K on the Sum tap, D_nl the Character tap over the twin's core tap.
[[nodiscard]] inline AuSwell auSwell(const AuMotion& motion, double seconds, double rateHz,
                              const std::vector<auPt::VoiceEngineEvent>& extraEvents) {
    MorphState s = auStateAs();
    s.mass = 50.0f;
    s.motion = motion;
    const double f0 = auHz(kAuE1);
    const auto from = static_cast<std::size_t>(auAt(kAuSettleS));
    const auto to = static_cast<std::size_t>(auAt(kAuSettleS + seconds));
    auPt::VoiceEngineScript sc = auScript(auOne(s), kAuE1, static_cast<std::uint64_t>(to) + kAuGrid);
    for (const auPt::VoiceEngineEvent& e : extraEvents)
        sc.events.push_back(e);
    const auPt::VoiceEngineTaps t = auPt::voiceEngineRenderWithTwin(sc);
    const std::vector<auPt::VoiceEngineFrame> frames = auPt::voiceEngineFrames(t.sumL, t.sumR, f0, kAuFs, from, to);
    const std::vector<auPt::VoiceEngineFrame> ch =
        auPt::voiceEngineFrames(t.characterL, t.characterR, f0, kAuFs, from, to, false);
    const std::vector<auPt::VoiceEngineFrame> lin =
        auPt::voiceEngineFrames(t.linearCoreL, t.linearCoreR, f0, kAuFs, from, to, false);
    const std::vector<double> dnl = auPt::voiceEngineFrameDnlDb(ch, lin);
    REQUIRE(!frames.empty());
    std::vector<double> es;
    std::vector<double> lk;
    for (const auPt::VoiceEngineFrame& f : frames) {
        es.push_back(f.eSubDb);
        lk.push_back(f.lKDb);
    }
    AuSwell w;
    w.eSubP2p = auP2p(es);
    w.dNlP2p = auP2p(dnl);
    w.lKP2p = auP2p(lk);
    w.boxcar = rateHz > 0.0 ? auPt::voiceEngineBoxcarAttenuation(rateHz, f0) : 1.0;
    w.seconds = seconds;
    w.frames = frames.size();
    return w;
}

[[nodiscard]] inline std::string auSwellLine(const std::string& label, const AuSwell& w) {
    std::ostringstream os;
    os << std::setprecision(6) << label << " seconds=" << w.seconds << " frames=" << w.frames
       << " E_sub_p2p_db=" << w.eSubP2p << " D_nl_p2p_db=" << w.dNlP2p << " L_K_sum_p2p_db=" << w.lKP2p
       << " boxcar_attenuation=" << w.boxcar;
    return os.str();
}

}  // namespace Krate::DSP::ProfundumAudibility
