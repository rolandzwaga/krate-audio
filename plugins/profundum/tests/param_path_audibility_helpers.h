#pragma once

// ==============================================================================
// Profundum - SC-018 descriptor port and the parameter-path render (Phase 11 T023; plan 6.4)
// ==============================================================================
// The Phase 9 audibility descriptors live in the ANONYMOUS namespace of
// dsp/tests/unit/systems/profundum_engine_audibility_test.cpp (the namespace opens at :90), so they cannot be
// included, and FR-070 forbids editing dsp/. The descriptors of the fifteen SC-018 rows are therefore PORTED
// here, each with a "ported from" comment citing the line it was read from. Ported bodies are verbatim except
// that (a) every DSP name is qualified (Kd:: for Krate::DSP, the Phase 9 aliases auPt / auBm / auSf / auMd are
// kept) because the Phase 9 TU's `using namespace Krate::DSP` is not repeated here, and (b) each Phase 9
// "render + read" function is split into a render (ppRender, below) and a reader on the taps, so the SAME
// reader runs on the engine-direct taps (the fidelity teeth) and on the processor taps (the SC-018 reading).
//
// Shared public Phase 9 helpers are INCLUDED read-only (plan 6.4; FR-070 forbids edits, not includes), through
// the test include directory ${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems (plugins/profundum/tests/CMakeLists.txt):
//   - profundum_voice_engine_test_helpers.h  (VoiceEngineScript / Taps / Observer, voiceEngineRender[WithTwin],
//     asFieldTaps, voiceEngineFrameLength, VoiceEngineHarmonicFit; it pulls bass_morph_test_helpers.h and
//     bass_morph_state_helpers.h: dNlDb, lKDb, eSubDb, dBrOct, midOf, periodEnergies, fallOff, showcasePair);
//   - bass_stereo_field_test_helpers.h  (bandReading; it pulls mass_dynamics_test_helpers.h: descriptorSeconds).
// Qualified names only: no `using namespace Krate::DSP::ProfundumTest` (profundum_test_fixture.h NAMESPACE HAZARD).
//
// Two render paths over one PpArm (the Phase 9 script plus its host-surface form):
//   - EngineDirect: auPt::voiceEngineRender(WithTwin) of the Phase 9 script, exactly as Phase 9 drove it.
//   - Processor: a ::Profundum::Processor. The base endpoints are loaded through setState (writeV2Stream +
//     withEndpoints); every lever / held value is a NORMALISED parameter change delivered by parameter-only
//     process() calls under Edit Target A, then again under Edit Target B, before setupProcessing (FR-010 pass 1
//     reads the Edit Target first); a VoiceEngineObserver is attached through setEngineObserverForTest BEFORE
//     setupProcessing (FR-067); the note events are host events; the output is the processor's own output.
//     The D_nl twin (FR-086 forceLinearCore) cannot be reached through the processor, so it is an engine-direct
//     render of the processor engine's own assembled endpoints and position (getEndpoint / morphPosition read
//     back after the render), with makeProfundumEngineConfig() and forceLinearCore set.
//
// Lever values: the lo arm holds the value the Phase 9 lo state holds and the hi arm the Phase 9 hi value, so the
// processor reading and the engine-direct reading measure the same contrast. One consequence: AT-02 at C3 (CSV
// line 64, base B_s) moves Shape 0.5 -> 1, because Phase 9's lo there is the base itself (B.shape = 0.5,
// bass_morph_state_helpers.h:199; profundum_engine_audibility_test.cpp:1095-1100), not 0.
//
// Bars and Phase 9 values: transcribed from specs/profundum-phase9-voice-engine/evidence/audibility-table.csv
// (`bar` and `value` columns, the line cited per row), never re-derived.
// ==============================================================================

#include "profundum_test_fixture.h"

#include "engine/profundum_engine_config.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include "bass_stereo_field_test_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <k_weighting.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ProfundumPluginTest {

namespace Kd = Krate::DSP;
namespace auPt = Krate::DSP::ProfundumTest;
namespace auBm = Krate::DSP::BassMorphTest;
namespace auSf = Krate::DSP::StereoFieldTest;
namespace auMd = Krate::DSP::MassDynamicsTest;

// -----------------------------------------------------------------------------
// Constants (ported from profundum_engine_audibility_test.cpp:105-137)
// -----------------------------------------------------------------------------

inline constexpr double kAuFs = 48000.0;                                     // :105
inline constexpr std::size_t kAuGrid = auPt::kVoiceEngineGrid;               // :106
inline constexpr std::uint8_t kAuVelocity = 100;                             // :107
inline constexpr std::uint8_t kAuE1 = auPt::kVoiceEngineNoteE1;              // :108
inline constexpr std::uint8_t kAuE2 = auPt::kVoiceEngineNoteE2;              // :109
inline constexpr std::uint8_t kAuC3 = auPt::kVoiceEngineNoteC3;              // :110
inline constexpr std::uint8_t kAuC4 = 60;                                    // :111
inline constexpr double kAuDropAmountSt = 24.0;                              // :123
inline constexpr double kAuMaxGateSeconds = 12.0;                            // :130
inline constexpr double kAuSettleS = 1.0;                                    // :131
inline constexpr double kAuMotionRateHz = 0.1;                               // :132
inline constexpr double kAuMotionCycles = 3.0;                               // :133
inline constexpr double kAuFieldMotionSeconds = 10.0;                        // :137

/// Fidelity teeth (plan 6.4): the engine-direct reading within 10 % (relative) of the CSV `value`.
inline constexpr double kPpPortTolerance = 0.10;

// -----------------------------------------------------------------------------
// Small utilities (ported)
// -----------------------------------------------------------------------------

// ported from profundum_engine_audibility_test.cpp:146
[[nodiscard]] inline bool auFinite(double x) noexcept {
    return ::Krate::DSP::detail::isFinite(x);   // barrier read: a plain bit_cast mask is folded under fast-math
}

// ported from profundum_engine_audibility_test.cpp:155
[[nodiscard]] inline std::uint64_t auAt(double seconds) noexcept {
    const auto n = static_cast<std::uint64_t>(std::llround(seconds * kAuFs));
    return (n / kAuGrid) * kAuGrid;
}

// ported from profundum_engine_audibility_test.cpp:160
[[nodiscard]] inline double auHz(std::uint8_t note) noexcept {
    return static_cast<double>(Kd::midiNoteToFrequency(static_cast<int>(note)));
}

// ported from profundum_engine_audibility_test.cpp:164
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

// ported from profundum_engine_audibility_test.cpp:176
[[nodiscard]] inline std::uint8_t auUpOctave(std::uint8_t n) noexcept {
    return static_cast<std::uint8_t>(n + 12);
}

// ported from profundum_engine_audibility_test.cpp:198
[[nodiscard]] inline double auP2p(const std::vector<double>& v) {
    REQUIRE(!v.empty());
    const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    return *mx - *mn;
}

// ported from profundum_engine_audibility_test.cpp:698
[[nodiscard]] inline double auRatio(double a, double b) noexcept {
    const double lo = std::min(a, b);
    const double hi = std::max(a, b);
    return lo > 0.0 ? hi / lo : 0.0;
}

// -----------------------------------------------------------------------------
// Bases (ported from profundum_engine_audibility_test.cpp:208-228)
// -----------------------------------------------------------------------------

[[nodiscard]] inline Kd::MorphState auStateA() { return auBm::showcasePair().first; }    // :208
[[nodiscard]] inline Kd::MorphState auStateB() { return auBm::showcasePair().second; }   // :209
[[nodiscard]] inline Kd::MorphState auStateAs() { return auPt::voiceEngineAs(auStateA()); }   // :210 (C-8)
[[nodiscard]] inline Kd::MorphState auStateBs() { return auPt::voiceEngineBs(auStateB()); }   // :211 (B10-1)

// ported from profundum_engine_audibility_test.cpp:217
struct AuSetting {
    Kd::MorphState a{};
    Kd::MorphState b{};
    float position = 0.0f;
    Kd::ProfundumMacroValues macros{};
};

// ported from profundum_engine_audibility_test.cpp:224
[[nodiscard]] inline AuSetting auOne(const Kd::MorphState& s) {
    return AuSetting{.a = s, .b = s};
}

// ported from profundum_engine_audibility_test.cpp:974
[[nodiscard]] inline Kd::MorphState auWith(Kd::MorphState s, Kd::BassCharacterStage::Algorithm alg, float amount) {
    s.characterAlgorithm = alg;
    s.characterAmount = amount;
    return s;
}

// -----------------------------------------------------------------------------
// Scripts (ported from profundum_engine_audibility_test.cpp:244-285)
// -----------------------------------------------------------------------------

using AuKind = auPt::VoiceEngineEventKind;

// ported from profundum_engine_audibility_test.cpp:244
[[nodiscard]] inline auPt::VoiceEngineEvent auOn(std::uint64_t at, std::uint8_t note) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = AuKind::NoteOn, .note = note, .velocity = kAuVelocity};
}

// ported from profundum_engine_audibility_test.cpp:248
[[nodiscard]] inline auPt::VoiceEngineEvent auOff(std::uint64_t at, std::uint8_t note) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = AuKind::NoteOff, .note = note};
}

// ported from profundum_engine_audibility_test.cpp:252
[[nodiscard]] inline auPt::VoiceEngineEvent auSetIndex(std::uint64_t at, AuKind kind, int index) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = kind, .index = index};
}

// ported from profundum_engine_audibility_test.cpp:256
[[nodiscard]] inline auPt::VoiceEngineEvent auSetValue(std::uint64_t at, AuKind kind, float value) {
    return auPt::VoiceEngineEvent{.sample = at, .kind = kind, .value = value};
}

// ported from profundum_engine_audibility_test.cpp:261 (MONO, one held note from sample 0, observer attached)
[[nodiscard]] inline auPt::VoiceEngineScript auScript(const AuSetting& s, std::uint8_t note, std::uint64_t total,
                                                      std::size_t tapped = 0) {
    REQUIRE(Kd::isValidMorphState(s.a));
    REQUIRE(Kd::isValidMorphState(s.b));
    auPt::VoiceEngineScript sc;
    sc.fs = kAuFs;
    sc.hostBlock = 64;
    sc.totalSamples = total;
    sc.a = s.a;
    sc.b = s.b;
    sc.position = s.position;
    sc.macros = s.macros;
    sc.mode = Kd::ProfundumEngine::VoiceMode::Mono;
    sc.attachObserver = true;
    sc.tappedVoices = tapped;
    sc.events.push_back(auOn(0, note));
    return sc;
}

// ported from profundum_engine_audibility_test.cpp:281
[[nodiscard]] inline std::uint64_t auSteadyTotal() {
    const auto n = static_cast<std::uint64_t>(std::ceil(auMd::descriptorSeconds(kAuFs) * kAuFs));
    return ((n + kAuGrid - 1) / kAuGrid) * kAuGrid + kAuGrid;
}

// -----------------------------------------------------------------------------
// Readers (ported)
// -----------------------------------------------------------------------------

// ported from profundum_engine_audibility_test.cpp:316
struct AuReading {
    double value = 0.0;
    bool valid = false;
    double out = 0.0;
    double lk = 0.0;
};

// ported from profundum_engine_audibility_test.cpp:423 (D_t of the mid from `from` on; invalid without the fall-off)
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

// ported from profundum_engine_audibility_test.cpp:464 (E_sub over [start, start + len) by LS fit at f0)
[[nodiscard]] inline double auWindowESubDb(const std::vector<float>& L, const std::vector<float>& R,
                                           std::size_t start, std::size_t len, double f0) {
    REQUIRE(len >= 8u);
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

// ported from profundum_engine_audibility_test.cpp:520
enum class AuFrameDesc : std::uint8_t { ESub, DCx, DBr };

// ported from profundum_engine_audibility_test.cpp:532
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

// ported from profundum_engine_audibility_test.cpp:550 (frame series of the mid, 8-period frames, hop 960)
[[nodiscard]] inline std::vector<double> auFrameSeries(const std::vector<float>& L, const std::vector<float>& R,
                                                       double f0, std::size_t from, std::size_t to, AuFrameDesc d) {
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

// ported from profundum_engine_audibility_test.cpp:593 (frame-wise D_st above 2 f_c; `invalid` counts G8-1 misses)
[[nodiscard]] inline std::vector<double> auDstFrames(const std::vector<float>& L, const std::vector<float>& R,
                                                     double f0, double fcHz, std::size_t from, std::size_t to,
                                                     std::size_t& invalid) {
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

// ported from profundum_engine_audibility_test.cpp:784 (L_K of the output over a short render; a record)
[[nodiscard]] inline double auLkOutShort(const auPt::VoiceEngineTaps& t) {
    const std::size_t n = std::min(t.outL.size(), t.outR.size());
    if (n < 64)
        return -120.0;
    std::vector<float> m(n);
    for (std::size_t i = 0; i < n; ++i)
        m[i] = 0.5f * (t.outL[i] + t.outR[i]);
    const std::size_t from = std::min(static_cast<std::size_t>(auAt(0.25)), n / 2);
    return Kd::TestUtils::kWeightedLevelDb(m.data(), n, kAuFs, from);
}

// ported from profundum_engine_audibility_test.cpp:954 (peak |y| over [0, 20 ms) over RMS over [100, 300) ms, dB)
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

// ported from profundum_engine_audibility_test.cpp:1779
[[nodiscard]] inline Kd::MorphState::MotionConfig auCycle(Kd::MorphState::MotionConfig::Target target,
                                                          double rateHz) {
    Kd::MorphState::MotionConfig m{};
    m.source = Kd::MorphState::MotionConfig::Source::Cycle;
    m.target = target;
    m.amount = 1.0f;
    m.depth = 1.0f;
    m.cycleRateHz = static_cast<float>(rateHz);
    m.polarity = Kd::MorphState::MotionConfig::Polarity::Bipolar;
    return m;
}

// ported from profundum_engine_audibility_test.cpp:1933 (the transition grid point nearest a half-cycle jump)
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

// -----------------------------------------------------------------------------
// The fifteen SC-018 rows and their gated (row, note) pairs (spec SC-018 table; CSV `gated` column)
// -----------------------------------------------------------------------------

enum class PpRow : std::uint8_t { AT27, AT01, AT10, AT11, AT21, AT23, AT25, AT43, AT03, AT02, AT05, AT28, AT33, AT38, AT42 };

/// The base of a gated CSV line (`base` column). AsEdge1 / AsSawEdgeHalf are the per-row settings of AT-01 / AT-02:
/// engine-direct they are the Phase 9 MorphStates; through the host they are A_s plus the recipe written through
/// 200-205 (spec SC-018 table).
enum class PpBase : std::uint8_t { As, B, Bs, AsEdge1, AsSawEdgeHalf, ShowcasePair };

struct PpBarRow {
    PpRow row;
    const char* id;
    std::uint8_t note;
    PpBase base;
    const char* descriptor;
    const char* tap;
    const char* outDescriptor;
    double phase9Value;   ///< CSV `value`
    double bar;           ///< CSV `bar`
    int csvLine;          ///< line in specs/profundum-phase9-voice-engine/evidence/audibility-table.csv
};

// Transcribed from the CSV (`value`, `bar`), one entry per gated (row, note) line; never re-derived.
inline constexpr std::array<PpBarRow, 30> kPpBars{{
    {PpRow::AT27, "AT-27", kAuE1, PpBase::B, "dL_K = L_K(dyn) - L_K(Sum) (dB)", "dynL/R and sumL/R", "L_K output (dB)", 3.51596, 3.0, 106},
    {PpRow::AT27, "AT-27", kAuC3, PpBase::Bs, "dL_K = L_K(dyn) - L_K(Sum) (dB)", "dynL/R and sumL/R", "L_K output (dB)", 4.01259, 3.0, 114},
    {PpRow::AT01, "AT-01", kAuE1, PpBase::AsEdge1, "D_nl (dB)", "Char / twin", "L_K output (dB)", 29.3673, 3.0, 52},
    {PpRow::AT01, "AT-01", kAuC3, PpBase::AsEdge1, "D_nl (dB)", "Char / twin", "L_K output (dB)", 29.4025, 3.0, 62},
    {PpRow::AT10, "AT-10", kAuE1, PpBase::As, "E_sub (dB)", "sumL/R", "E_sub output (dB)", 5.94809, 2.0, 2},
    {PpRow::AT10, "AT-10", kAuC3, PpBase::As, "E_sub (dB)", "sumL/R", "E_sub output (dB)", 5.92968, 2.0, 13},
    {PpRow::AT11, "AT-11", kAuE1, PpBase::As, "E_sub over [0, punchTimeMs) (dB; LS fit)", "sumL/R", "E_sub output, same window (dB)", 2.08853, 2.0, 3},
    {PpRow::AT11, "AT-11", kAuC3, PpBase::As, "E_sub over [0, punchTimeMs) (dB; LS fit)", "sumL/R", "E_sub output, same window (dB)", 2.03878, 2.0, 14},
    {PpRow::AT21, "AT-21", kAuE1, PpBase::As, "alpha (mean d over [0, N] / A)", "fCore / fRes voice 0", "L_K output short (dB)", 0.755316, 0.1, 168},
    {PpRow::AT21, "AT-21", kAuC3, PpBase::As, "alpha (mean d over [0, N] / A)", "fCore / fRes voice 0", "L_K output short (dB)", 0.767995, 0.1, 173},
    {PpRow::AT23, "AT-23", kAuE1, PpBase::As, "t_50 ratio (samples)", "fRes voice 0", "L_K output short (dB)", 287.344, 2.0, 170},
    {PpRow::AT23, "AT-23", kAuC3, PpBase::As, "t_50 ratio (samples)", "fRes voice 0", "L_K output short (dB)", 287.219, 2.0, 175},
    {PpRow::AT25, "AT-25", kAuE1, PpBase::As, "D_t release reading ratio (ms)", "sumL/R", "D_t output release (ms)", 38.1903, 2.0, 70},
    {PpRow::AT25, "AT-25", kAuC3, PpBase::As, "D_t release reading ratio (ms)", "sumL/R", "D_t output release (ms)", 40.3031, 2.0, 73},
    {PpRow::AT43, "AT-43", kAuE1, PpBase::As, "core-h1 phase jump (deg)", "coreL/R", "L_K output short (dB)", 177.667, 30.0, 158},
    {PpRow::AT43, "AT-43", kAuC3, PpBase::As, "core-h1 phase jump (deg)", "coreL/R", "L_K output short (dB)", 167.525, 30.0, 162},
    {PpRow::AT03, "AT-03", kAuE1, PpBase::As, "D_nl (dB)", "Char / twin", "L_K output (dB)", 47.6755, 3.0, 57},
    {PpRow::AT03, "AT-03", kAuC3, PpBase::As, "D_nl (dB)", "Char / twin", "L_K output (dB)", 47.6593, 3.0, 67},
    {PpRow::AT02, "AT-02", kAuE1, PpBase::AsSawEdgeHalf, "D_br (oct)", "sumL/R", "D_br output (oct)", 2.47299, 0.25, 56},
    {PpRow::AT02, "AT-02", kAuC3, PpBase::Bs, "D_br (oct)", "sumL/R", "D_br output (oct)", 0.378903, 0.25, 64},
    {PpRow::AT05, "AT-05", kAuE1, PpBase::As, "D_nl (dB)", "Char / twin", "L_K output (dB)", 6.50986, 3.0, 26},
    {PpRow::AT05, "AT-05", kAuC3, PpBase::As, "D_nl (dB)", "Char / twin", "L_K output (dB)", 6.49428, 3.0, 38},
    {PpRow::AT28, "AT-28", kAuE1, PpBase::B, "onset ratio (dB)", "dynL/R", "onset ratio output (dB)", 10.4185, 3.0, 108},
    {PpRow::AT28, "AT-28", kAuC3, PpBase::B, "onset ratio (dB)", "dynL/R", "onset ratio output (dB)", 10.6639, 3.0, 115},
    {PpRow::AT33, "AT-33", kAuE1, PpBase::Bs, "frame-wise D_st p2p (dB; G8-1)", "fieldL/R", "frame-wise D_st p2p output (dB)", 16.3826, 3.0, 81},
    {PpRow::AT33, "AT-33", kAuC3, PpBase::As, "frame-wise D_st p2p (dB; G8-1)", "fieldL/R", "frame-wise D_st p2p output (dB)", 11.2431, 3.0, 89},
    {PpRow::AT38, "AT-38", kAuE1, PpBase::Bs, "frame-wise D_br p2p (oct)", "sumL/R", "frame-wise D_br p2p output (oct)", 0.299477, 0.25, 131},
    {PpRow::AT38, "AT-38", kAuC3, PpBase::Bs, "frame-wise D_br p2p (oct)", "sumL/R", "frame-wise D_br p2p output (oct)", 0.303864, 0.25, 150},
    {PpRow::AT42, "AT-42", kAuE1, PpBase::ShowcasePair, "D_nl (dB)", "Char / twin", "L_K output (dB)", 8.15013, 3.0, 117},
    {PpRow::AT42, "AT-42", kAuC3, PpBase::ShowcasePair, "D_nl (dB)", "Char / twin", "L_K output (dB)", 6.72976, 3.0, 119},
}};

// -----------------------------------------------------------------------------
// Host-surface form: normalised parameter values
// -----------------------------------------------------------------------------

struct PpParam {
    Steinberg::Vst::ParamID id;
    double norm;
};

/// The normalised value of a plain value through the plugin's own mapping laws: E rows by their kLawTable row
/// (sound_param_row.h normalizedFromPlain; enum rows index / maxEnum), the note and morph-control rows by theirs.
[[nodiscard]] inline double ppNorm(Steinberg::Vst::ParamID id, double plain) {
    if (const ::Profundum::SoundParamRow* row = ::Profundum::soundRowOf(id); row != nullptr) {
        const ::Profundum::FieldInfo& f = ::Profundum::lawOf(row->leaf);
        if (f.kind != Kd::MorphState::Kind::F32) {
            return plain / static_cast<double>(f.maxEnum);
        }
        return ::Profundum::normalizedFromPlain(f, plain);
    }
    switch (id) {
        case ::Profundum::kVoiceModeId: return plain;           // 0 Mono, 1 Poly (global_params.h:116-117)
        case ::Profundum::kModWheelId: return plain;            // identity (performance_params.h:54-55)
        case ::Profundum::kMorphPositionId: return plain;       // identity (morph_params.h:226-228)
        case ::Profundum::kNotePortamentoId: return ::Profundum::normalizedFromPlain(::Profundum::kPortamentoLaw, plain);
        case ::Profundum::kNoteMemoryId: return plain / static_cast<double>(::Profundum::kMaxNoteMemoryIndex);
        case ::Profundum::kNoteRetriggerId: return plain / static_cast<double>(::Profundum::kMaxNoteRetriggerIndex);
        default: break;
    }
    FAIL_CHECK("ppNorm: no law for parameter " << id);
    return 0.0;
}

/// One render: the Phase 9 script (engine-direct) and its host-surface form (base endpoints + parameter writes).
struct PpArm {
    auPt::VoiceEngineScript direct;
    Kd::MorphState hostA{};
    Kd::MorphState hostB{};
    std::vector<PpParam> params;
};

[[nodiscard]] inline PpArm ppArm(const AuSetting& direct, std::uint8_t note, std::uint64_t total, std::size_t tapped,
                                 const Kd::MorphState& hostA, const Kd::MorphState& hostB,
                                 std::vector<PpParam> params) {
    PpArm arm;
    arm.direct = auScript(direct, note, total, tapped);
    arm.hostA = hostA;
    arm.hostB = hostB;
    arm.params = std::move(params);
    return arm;
}

enum class PpPath : std::uint8_t { EngineDirect, Processor };

/// Engine-direct: the Phase 9 driver, unchanged.
[[nodiscard]] inline auPt::VoiceEngineTaps ppRenderDirect(const PpArm& arm, bool twin) {
    return twin ? auPt::voiceEngineRenderWithTwin(arm.direct) : auPt::voiceEngineRender(arm.direct);
}

/// Processor path (header comment). Only NoteOn / NoteOff of the script are sent as host events; every other
/// Phase 9 engine event (portamento time, Note Memory mode, retrigger) has its host form in arm.params.
[[nodiscard]] inline auPt::VoiceEngineTaps ppRenderProcessor(const PpArm& arm, bool twin) {
    const auPt::VoiceEngineScript& s = arm.direct;
    auto fx = std::make_unique<ProcessorFixture>();

    // 1. The base endpoints through the host surface (a v2 setState stream).
    const StateBytes state = withEndpoints(writeV2Stream(*fx->proc), arm.hostA, arm.hostB);
    REQUIRE(loadStateBytes(*fx->proc, state) == Steinberg::kResultOk);

    // 2. Lever and held values: normalised changes, under Edit Target A and then B, before setupProcessing.
    MultiParamChanges pc;
    pc.reserve(arm.params.size() + 1u);
    for (const double target : {0.0, 1.0}) {
        pc.clear();
        pc.addQueue(::Profundum::kMorphEditTargetId).addTestPoint(0, target);
        for (const PpParam& p : arm.params) {
            pc.addQueue(p.id).addTestPoint(0, p.norm);
        }
        REQUIRE(fx->processNoOutputs(&pc) == Steinberg::kResultOk);
    }

    // 3. The tap seam (FR-067), attached before setupProcessing.
    auPt::VoiceEngineTaps taps;
    auPt::voiceEngineAllocateTaps(taps, s);
    auPt::VoiceEngineObserver observer(taps);
    fx->proc->setEngineObserverForTest(&observer);
    REQUIRE(fx->prepare(s.fs, static_cast<Steinberg::int32>(s.hostBlock)) == Steinberg::kResultOk);

    // 4. The note events, at the script's samples.
    std::vector<ProcessorFixture::ScriptedEvent> events;
    for (const auPt::VoiceEngineEvent& ev : s.events) {
        if (ev.kind != AuKind::NoteOn && ev.kind != AuKind::NoteOff) {
            continue;
        }
        ProcessorFixture::ScriptedEvent e{};
        e.sample = static_cast<Steinberg::int64>(ev.sample);
        e.kind = ev.kind == AuKind::NoteOn ? ProcessorFixture::ScriptedEvent::Kind::NoteOn
                                           : ProcessorFixture::ScriptedEvent::Kind::NoteOff;
        e.pitch = static_cast<Steinberg::int16>(ev.note);
        e.velocity = static_cast<float>(ev.velocity) / 127.0f;
        events.push_back(e);
    }
    const auto total = static_cast<std::size_t>(s.totalSamples);
    fx->renderScript(events, total, s.hostBlock);
    REQUIRE(fx->capturedL.size() == total);
    REQUIRE(fx->capturedR.size() == total);
    REQUIRE(fx->proc->endpointRejectionsForTest() == 0u);
    taps.outL = fx->capturedL;
    taps.outR = fx->capturedR;

    // 5. The D_nl twin: the processor engine's own endpoints and position, forceLinearCore (FR-086).
    if (twin) {
        REQUIRE(fx->engine() != nullptr);
        auPt::VoiceEngineScript t = s;
        t.a = fx->engine()->getEndpoint(Kd::BassMorphEngine::Endpoint::A);
        t.b = fx->engine()->getEndpoint(Kd::BassMorphEngine::Endpoint::B);
        t.position = fx->engine()->morphPosition();
        t.config = ::Profundum::makeProfundumEngineConfig();
        const auPt::VoiceEngineTaps tw = auPt::voiceEngineRender(auPt::voiceEngineTwinScript(t));
        taps.linearCoreL = tw.coreL;
        taps.linearCoreR = tw.coreR;
    }

    // 6. Detach while inactive (FR-067), before the observer goes out of scope.
    fx->proc->setActive(false);
    fx->proc->setEngineObserverForTest(nullptr);
    return taps;
}

[[nodiscard]] inline auPt::VoiceEngineTaps ppRender(PpPath path, const PpArm& arm, bool twin) {
    return path == PpPath::Processor ? ppRenderProcessor(arm, twin) : ppRenderDirect(arm, twin);
}

// -----------------------------------------------------------------------------
// Bases, host form
// -----------------------------------------------------------------------------

/// The Phase 9 base MorphState (engine-direct).
[[nodiscard]] inline Kd::MorphState ppDirectBase(PpBase b) {
    switch (b) {
        case PpBase::B: return auStateB();
        case PpBase::Bs: return auStateBs();
        case PpBase::AsEdge1: {   // ported from profundum_engine_audibility_test.cpp:1102-1104 (asEdge1)
            Kd::MorphState s = auStateAs();
            s.recipe.edge = 1.0f;
            REQUIRE(Kd::isValidMorphState(s));
            return s;
        }
        case PpBase::AsSawEdgeHalf: {   // ported from profundum_engine_audibility_test.cpp:1077-1094 (asSawEdgeHalf)
            Kd::MorphState s = auStateAs();
            s.recipe = Kd::SpectralShapeRecipe::kSawAnchor;
            s.recipe.edge = 0.5f;
            REQUIRE(Kd::isValidMorphState(s));
            return s;
        }
        case PpBase::As:
        case PpBase::ShowcasePair: break;
    }
    return auStateAs();
}

/// The base loaded through setState for the host path (A_s for the two per-row recipe settings).
[[nodiscard]] inline Kd::MorphState ppHostBase(PpBase b) {
    switch (b) {
        case PpBase::B: return auStateB();
        case PpBase::Bs: return auStateBs();
        default: break;
    }
    return auStateAs();
}

/// The per-row setting written through the host (spec SC-018 table: Edge 204 = 1 for AT-01; kSawAnchor at edge
/// 0.5 written through 200-205 for AT-02 at E1).
[[nodiscard]] inline std::vector<PpParam> ppBaseParams(PpBase b) {
    namespace P = ::Profundum;
    if (b == PpBase::AsEdge1) {
        return {{P::kCoreEdgeId, ppNorm(P::kCoreEdgeId, 1.0)}};
    }
    if (b == PpBase::AsSawEdgeHalf) {
        const Kd::SpectralShapeRecipe::Controls r = Kd::SpectralShapeRecipe::kSawAnchor;
        return {{P::kCoreDepthId, ppNorm(P::kCoreDepthId, static_cast<double>(r.depth))},
                {P::kCoreBodyId, ppNorm(P::kCoreBodyId, static_cast<double>(r.body))},
                {P::kCoreBodyCurvatureId, ppNorm(P::kCoreBodyCurvatureId, static_cast<double>(r.bodyCurvature))},
                {P::kCoreBodyEmphasisId, ppNorm(P::kCoreBodyEmphasisId, static_cast<double>(r.bodyEmphasis))},
                {P::kCoreEdgeId, ppNorm(P::kCoreEdgeId, 0.5)},
                {P::kCoreShiftId, ppNorm(P::kCoreShiftId, static_cast<double>(r.shift))}};
    }
    return {};
}

[[nodiscard]] inline std::vector<PpParam> ppWith(std::vector<PpParam> a, std::initializer_list<PpParam> more) {
    a.insert(a.end(), more.begin(), more.end());
    return a;
}

// -----------------------------------------------------------------------------
// Row evaluation: one (row, note) contrast on one path
// -----------------------------------------------------------------------------

struct PpReading {
    double lo = 0.0;
    double hi = 0.0;
    double value = 0.0;   ///< the row's "moves by" (|hi - lo|, a ratio, or a wrapped phase), as Phase 9
    double outLo = 0.0;
    double outHi = 0.0;
    bool valid = false;
};

/// Steady D_nl (ported from profundum_engine_audibility_test.cpp:338-346): Char tap over the twin's core tap.
[[nodiscard]] inline double ppReadDnl(const auPt::VoiceEngineTaps& t, double f0, double& out) {
    out = auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), kAuFs);
    return auBm::dNlDb(t, f0, kAuFs);
}

/// AT-25 gate render and release D_t (ported from profundum_engine_audibility_test.cpp:402-461): note-off at 1 s,
/// the render extended 1 s at a time to 12 s until the Sum mid falls 80 dB; D_t from the note-off grid point on.
[[nodiscard]] inline AuReading ppGateReleaseDt(PpPath path, const Kd::MorphState& direct, const Kd::MorphState& host,
                                               const std::vector<PpParam>& params, std::uint8_t note) {
    const double f0 = auHz(note);
    const std::uint64_t off = auAt(1.0);
    for (double sec = static_cast<double>(off) / kAuFs + 2.0; sec <= kAuMaxGateSeconds + 1e-9; sec += 1.0) {
        PpArm arm = ppArm(auOne(direct), note, auAt(sec), 0, host, host, params);
        arm.direct.events.push_back(auOff(off, note));
        const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
        const auBm::FallOff fo = auBm::fallOff(auBm::periodEnergies(auBm::midOf(t.sumL, t.sumR), f0, kAuFs));
        if (!fo.reached)
            continue;
        AuReading r = auDtMs(t.sumL, t.sumR, f0, static_cast<std::size_t>(off));
        const AuReading o = auDtMs(t.outL, t.outR, f0, static_cast<std::size_t>(off));
        r.out = o.valid ? o.value : -1.0;
        return r;
    }
    return {};
}

/// The row's lo and hi renders on `path` and the Phase 9 combination of their readings.
[[nodiscard]] inline PpReading ppEvaluate(const PpBarRow& bar, PpPath path) {
    namespace P = ::Profundum;
    using Alg = Kd::BassCharacterStage::Algorithm;
    using Motion = Kd::MorphState::MotionConfig;
    const std::uint8_t note = bar.note;
    const double f0 = auHz(note);
    const Kd::MorphState base = ppDirectBase(bar.base);
    const Kd::MorphState host = ppHostBase(bar.base);
    const std::vector<PpParam> held = ppBaseParams(bar.base);
    PpReading r;
    const auto diff = [&r]() {
        r.value = std::abs(r.hi - r.lo);
        r.valid = r.valid && auFinite(r.value);
    };

    switch (bar.row) {
        case PpRow::AT27: {   // ported from profundum_engine_audibility_test.cpp:1496-1525 (dlk / cell)
            const auto read = [&](float mass, double& outLk) {
                Kd::MorphState s = base;
                s.mass = mass;
                const PpArm arm = ppArm(auOne(s), note, auSteadyTotal(), 0, host, host,
                                        ppWith(held, {{P::kMassId, ppNorm(P::kMassId, mass)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                outLk = auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), kAuFs);
                return auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Dynamics), kAuFs)
                       - auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Sum), kAuFs);
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(100.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT01: {   // ported from profundum_engine_audibility_test.cpp:1068-1076 (at01), D_nl :338-346
            const auto read = [&](bool on, double& out) {
                Kd::MorphState s = base;
                s.excitation.mix = on ? 1.0f : 0.0f;
                if (on)
                    s.excitation.drive = 1.0f;
                const PpArm arm = ppArm(auOne(s), note, auSteadyTotal(), 0, host, host,
                                        ppWith(held, {{P::kCoreExcitationDriveId, ppNorm(P::kCoreExcitationDriveId, 1.0)},
                                                      {P::kCoreExcitationMixId, ppNorm(P::kCoreExcitationMixId, on ? 1.0 : 0.0)}}));
                return ppReadDnl(ppRender(path, arm, true), f0, out);
            };
            r.lo = read(false, r.outLo);
            r.hi = read(true, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT10: {   // ported from profundum_engine_audibility_test.cpp:995-1003, E_sub :363-366
            const auto read = [&](float level, double& out) {
                Kd::MorphState s = base;
                s.anchor.level = level;
                const PpArm arm = ppArm(auOne(s), note, auSteadyTotal(), 0, host, host,
                                        ppWith(held, {{P::kAnchorLevelId, ppNorm(P::kAnchorLevelId, level)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                out = auBm::eSubDb(t.outL, t.outR, f0, kAuFs);
                return auBm::eSubDb(t.sumL, t.sumR, f0, kAuFs);
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(1.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT11: {   // ported from profundum_engine_audibility_test.cpp:1137-1178 (punch arm), :833-842
            const auto read = [&](bool punch, double& out) {
                Kd::MorphState s = base;
                s.anchor.level = 1.0f;
                s.anchor.mode = Kd::AnchorOscillator::Mode::Lock;
                if (punch) {
                    s.anchor.mode = Kd::AnchorOscillator::Mode::Punch;
                    s.anchor.punchBoostDb = 6.0f;
                    s.anchor.punchTimeMs = 40.0f;
                }
                const double modeIndex = static_cast<double>(static_cast<int>(s.anchor.mode));
                const PpArm arm = ppArm(auOne(s), note, auAt(1.0), 0, host, host,
                                        ppWith(held, {{P::kAnchorLevelId, ppNorm(P::kAnchorLevelId, 1.0)},
                                                      {P::kAnchorModeId, ppNorm(P::kAnchorModeId, modeIndex)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                const auto len = static_cast<std::size_t>(std::llround(40.0 * kAuFs / 1000.0));
                out = auWindowESubDb(t.outL, t.outR, 0, len, f0);
                return auWindowESubDb(t.sumL, t.sumR, 0, len, f0);
            };
            r.lo = read(false, r.outLo);
            r.hi = read(true, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT21: {   // ported from profundum_engine_audibility_test.cpp:909-931 (auDropRead), :1344-1353
            const auto read = [&](float shape, double& out) {
                Kd::MorphState s = base;
                s.dropAmountSt = static_cast<float>(kAuDropAmountSt);
                s.dropShape = shape;
                const PpArm arm = ppArm(auOne(s), note, auAt(2.0), 1, host, host,
                                        ppWith(held, {{P::kDropAmountId, ppNorm(P::kDropAmountId, kAuDropAmountSt)},
                                                      {P::kDropShapeId, ppNorm(P::kDropShapeId, shape)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
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
                out = auLkOutShort(t);
                return auPt::voiceEngineDropDescriptors(d, static_cast<double>(s.dropAmountSt), N).alpha;
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(1.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT23: {   // ported from profundum_engine_audibility_test.cpp:935-951 (auGlideT50), :1370-1390
            const std::uint8_t target = auUpOctave(note);
            const auto read = [&](float portaMs, double& out) {
                const std::uint64_t t1 = auAt(0.5);
                PpArm arm = ppArm(auOne(base), note, auAt(1.5), 1, host, host,
                                  ppWith(held, {{P::kVoiceModeId, ppNorm(P::kVoiceModeId, 0.0)},
                                                {P::kNotePortamentoId, ppNorm(P::kNotePortamentoId, portaMs)}}));
                arm.direct.events.push_back(auSetValue(0, AuKind::PortamentoTimeMs, portaMs));
                arm.direct.events.push_back(auOn(t1, target));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                const std::vector<float>& fr = t.voices[0].fRes;
                const double fT = auHz(target);
                auto last = static_cast<std::size_t>(t1);
                for (auto n = static_cast<std::size_t>(t1); n < fr.size(); ++n) {
                    const bool inside = fr[n] > 0.0f
                                        && std::abs(1200.0 * std::log2(static_cast<double>(fr[n]) / fT)) <= 50.0;
                    if (!inside)
                        last = n + 1;
                }
                out = auLkOutShort(t);
                return std::max(static_cast<double>(last - static_cast<std::size_t>(t1)), static_cast<double>(kAuGrid));
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(200.0f, r.outHi);
            r.value = auRatio(r.lo, r.hi);
            r.valid = auFinite(r.value);
            return r;
        }
        case PpRow::AT25: {   // ported from profundum_engine_audibility_test.cpp:1464-1490
            const auto read = [&](float releaseMs, double& out, bool& ok) {
                Kd::MorphState s = base;
                s.ampEnv.releaseMs = releaseMs;
                const AuReading g = ppGateReleaseDt(path, s, host,
                                                    ppWith(held, {{P::kAmpReleaseId, ppNorm(P::kAmpReleaseId, releaseMs)}}),
                                                    note);
                out = g.out;
                ok = ok && g.valid;
                return g.value;
            };
            bool ok = true;
            r.lo = read(50.0f, r.outLo, ok);
            r.hi = read(2000.0f, r.outHi, ok);
            r.value = auRatio(r.lo, r.hi);
            r.valid = ok && auFinite(r.value);
            return r;
        }
        case PpRow::AT43: {   // ported from profundum_engine_audibility_test.cpp:1959-1976 (auNoteMemoryPhase), :2003-2028
            using M = Kd::NoteMemory::Mode;
            const std::uint64_t t1 = auPickTransition(f0, auHz(auUpOctave(note)));
            const auto phase = [&](M mode, double& out) {
                const std::uint8_t target = auUpOctave(note);
                const double f2 = auHz(target);
                const auto len = static_cast<std::size_t>(std::llround(4.0 * kAuFs / f2));
                const std::uint64_t ws = t1 + auAt(0.020);
                PpArm arm = ppArm(auOne(base), note, ws + len + kAuGrid, 0, host, host,
                                  ppWith(held, {{P::kVoiceModeId, ppNorm(P::kVoiceModeId, 0.0)},
                                                {P::kNoteRetriggerId, ppNorm(P::kNoteRetriggerId, 0.0)},
                                                {P::kNoteMemoryId, ppNorm(P::kNoteMemoryId, static_cast<double>(static_cast<int>(mode)))}}));
                arm.direct.events.push_back(auSetIndex(0, AuKind::NoteMemoryMode, static_cast<int>(mode)));
                arm.direct.events.push_back(
                    auSetIndex(0, AuKind::NoteRetrigger, static_cast<int>(Kd::NoteMemory::Retrigger::Reset)));
                arm.direct.events.push_back(auOn(t1, target));   // Overlap: the first note still held
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                const std::vector<float> core = auBm::midOf(t.coreL, t.coreR);
                REQUIRE(ws + len <= core.size());
                out = auLkOutShort(t);
                return auPt::goertzelPhase(core.data() + ws, len, kAuFs, f2);
            };
            const double pr = phase(M::EnvelopeRetriggerOnly, r.outLo);   // M_ref (index 2)
            const double pm = phase(M::AlwaysRetrigger, r.outHi);         // the mode under test (index 0)
            double d = pm - pr;
            d -= auPt::kTwoPi * std::round(d / auPt::kTwoPi);
            r.lo = pr * 180.0 / (0.5 * auPt::kTwoPi);
            r.hi = pm * 180.0 / (0.5 * auPt::kTwoPi);
            r.value = std::abs(d) * 180.0 / (0.5 * auPt::kTwoPi);
            r.valid = auFinite(r.value);
            return r;
        }
        case PpRow::AT03: {   // ported from profundum_engine_audibility_test.cpp:986-993
            const auto read = [&](float fold, double& out) {
                Kd::MorphState s = base;
                s.foldAmount = fold;
                const PpArm arm = ppArm(auOne(s), note, auSteadyTotal(), 0, host, host,
                                        ppWith(held, {{P::kFoldAmountId, ppNorm(P::kFoldAmountId, fold)}}));
                return ppReadDnl(ppRender(path, arm, true), f0, out);
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(100.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT02: {   // ported from profundum_engine_audibility_test.cpp:1095-1100 (at02), D_br :358-361
            const auto read = [&](float shape, double& out) {
                Kd::MorphState s = base;
                s.shape = shape;
                const PpArm arm = ppArm(auOne(s), note, auSteadyTotal(), 0, host, host,
                                        ppWith(held, {{P::kFoldShapeId, ppNorm(P::kFoldShapeId, shape)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                out = auBm::dBrOct(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Output), f0, kAuFs);
                return auBm::dBrOct(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Sum), f0, kAuFs);
            };
            r.lo = read(base.shape, r.outLo);   // Phase 9's lo is the base itself (header comment)
            r.hi = read(1.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT05: {   // ported from profundum_engine_audibility_test.cpp:1117-1131 (Fold arm)
            const auto read = [&](float amount, double& out) {
                const double foldIndex = static_cast<double>(static_cast<int>(Alg::Fold));
                const PpArm arm = ppArm(auOne(auWith(base, Alg::Fold, amount)), note, auSteadyTotal(), 0, host, host,
                                        ppWith(held, {{P::kCharacterId, ppNorm(P::kCharacterId, foldIndex)},
                                                      {P::kCharacterAmountId, ppNorm(P::kCharacterAmountId, amount)}}));
                return ppReadDnl(ppRender(path, arm, true), f0, out);
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(1.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT28: {   // ported from profundum_engine_audibility_test.cpp:1526-1551
            const auto read = [&](float attackDb, double& out) {
                Kd::MorphState s = base;
                s.transientAttackDb = attackDb;
                const PpArm arm = ppArm(auOne(s), note, auAt(1.0), 0, host, host,
                                        ppWith(held, {{P::kDynamicsTransientAttackId,
                                                       ppNorm(P::kDynamicsTransientAttackId, attackDb)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                out = auOnsetRatioDb(t.outL, t.outR);
                return auOnsetRatioDb(t.dynL, t.dynR);
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(12.0f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT33: {   // ported from profundum_engine_audibility_test.cpp:1600-1638 (auFieldMotionCell)
            const auto from = static_cast<std::size_t>(auAt(kAuSettleS));
            const auto to = static_cast<std::size_t>(auAt(kAuSettleS + kAuFieldMotionSeconds));
            const auto read = [&](float depth, double& outP2p, std::size_t& invalid) {
                Kd::MorphState s = base;
                s.field.mode = Kd::BassStereoField::ImageMode::Spread;
                s.field.spread = 1.0f;
                s.field.motionDepth = depth;
                const double spreadIndex = static_cast<double>(static_cast<int>(Kd::BassStereoField::ImageMode::Spread));
                const PpArm arm = ppArm(auOne(s), note, static_cast<std::uint64_t>(to) + kAuGrid, 0, host, host,
                                        ppWith(held, {{P::kFieldImageModeId, ppNorm(P::kFieldImageModeId, spreadIndex)},
                                                      {P::kFieldSpreadId, ppNorm(P::kFieldSpreadId, 1.0)},
                                                      {P::kFieldMotionDepthId, ppNorm(P::kFieldMotionDepthId, depth)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                const double fc = static_cast<double>(s.field.crossoverHz);
                std::size_t outInvalid = 0;
                outP2p = auP2p(auDstFrames(t.outL, t.outR, f0, fc, from, to, outInvalid));
                return auP2p(auDstFrames(t.stageOutL, t.stageOutR, f0, fc, from, to, invalid));
            };
            std::size_t invLo = 0;
            std::size_t invHi = 0;
            r.lo = read(0.0f, r.outLo, invLo);
            r.hi = read(1.0f, r.outHi, invHi);
            r.valid = invLo == 0 && invHi == 0;
            diff();
            return r;
        }
        case PpRow::AT38: {   // ported from profundum_engine_audibility_test.cpp:1711-1777 (no extra events), :1800-1862
            const double cycleSeconds = std::max(kAuMotionCycles / kAuMotionRateHz, 3.0);
            const auto from = static_cast<std::size_t>(auAt(kAuSettleS));
            const auto to = static_cast<std::size_t>(auAt(kAuSettleS + cycleSeconds));
            const auto read = [&](bool on, double& outP2p) {
                Kd::MorphState s = base;
                if (on)
                    s.motion = auCycle(Motion::Target::Shift, kAuMotionRateHz);
                else
                    s.motion.source = Motion::Source::Off;
                const double source = static_cast<double>(static_cast<int>(on ? Motion::Source::Cycle : Motion::Source::Off));
                const double shift = static_cast<double>(static_cast<int>(Motion::Target::Shift));
                const PpArm arm = ppArm(auOne(s), note, static_cast<std::uint64_t>(to) + kAuGrid, 0, host, host,
                                        ppWith(held, {{P::kMotionTargetId, ppNorm(P::kMotionTargetId, shift)},
                                                      {P::kMotionAmountId, ppNorm(P::kMotionAmountId, 1.0)},
                                                      {P::kMotionDepthId, ppNorm(P::kMotionDepthId, 1.0)},
                                                      {P::kMotionCycleRateId, ppNorm(P::kMotionCycleRateId, kAuMotionRateHz)},
                                                      {P::kMotionSourceId, ppNorm(P::kMotionSourceId, source)}}));
                const auPt::VoiceEngineTaps t = ppRender(path, arm, false);
                outP2p = auP2p(auFrameSeries(t.outL, t.outR, f0, from, to, AuFrameDesc::DBr));
                return auP2p(auFrameSeries(t.sumL, t.sumR, f0, from, to, AuFrameDesc::DBr));
            };
            r.lo = read(false, r.outLo);
            r.hi = read(true, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
        case PpRow::AT42: {   // ported from profundum_engine_audibility_test.cpp:1914-1925 (auShardMorph, p = 0.5)
            const auto read = [&](float position, double& out) {
                const AuSetting s{.a = auStateAs(), .b = auStateB(), .position = position};
                const PpArm arm = ppArm(s, note, auSteadyTotal(), 0, auStateAs(), auStateB(),
                                        {{P::kMorphPositionId, ppNorm(P::kMorphPositionId, position)}});
                return ppReadDnl(ppRender(path, arm, true), f0, out);
            };
            r.lo = read(0.0f, r.outLo);
            r.hi = read(0.5f, r.outHi);
            r.valid = true;
            diff();
            return r;
        }
    }
    FAIL_CHECK("ppEvaluate: unhandled row " << bar.id);
    return r;
}

// -----------------------------------------------------------------------------
// SC-024 audibility arm (Profundum_MassOffsetAudibility): the AT-27 descriptor with the offset driven only by
// Mod Wheel (ID 6); both endpoints persisted at Mass 0.
// -----------------------------------------------------------------------------

/// dL_K = L_K(dyn) - L_K(Sum) on the processor path (the AT-27 reader, profundum_engine_audibility_test.cpp:1498-1504).
/// Endpoints: A = B at Mass 0, B = B_s at Mass 0 (both AT-27 bases that reach the bar at E1: CSV lines 106, 107).
[[nodiscard]] inline double ppMassOffsetDlk(float position, double modWheel) {
    namespace P = ::Profundum;
    Kd::MorphState a = auStateB();
    a.mass = 0.0f;
    Kd::MorphState b = auStateBs();
    b.mass = 0.0f;
    const AuSetting s{.a = a, .b = b, .position = position};
    std::vector<PpParam> params{{P::kMorphPositionId, ppNorm(P::kMorphPositionId, position)}};
    if (modWheel > 0.0) {
        params.push_back({P::kModWheelId, ppNorm(P::kModWheelId, modWheel)});
    }
    const PpArm arm = ppArm(s, kAuE1, auSteadyTotal(), 0, a, b, std::move(params));
    const auPt::VoiceEngineTaps t = ppRenderProcessor(arm, false);
    return auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Dynamics), kAuFs)
           - auBm::lKDb(auPt::asFieldTaps(t, auPt::VoiceEngineTap::Sum), kAuFs);
}

// -----------------------------------------------------------------------------
// Evidence CSV (specs/profundum-phase11-parameters/evidence/param-path-audibility.csv)
// -----------------------------------------------------------------------------

[[nodiscard]] inline std::filesystem::path ppEvidenceCsvPath() {
    // PROFUNDUM_RESOURCES_DIR = plugins/profundum/resources (tests/CMakeLists.txt): three levels up is the repo root.
    return std::filesystem::path(PROFUNDUM_RESOURCES_DIR) / ".." / ".." / ".." / "specs" /
           "profundum-phase11-parameters" / "evidence" / "param-path-audibility.csv";
}

[[nodiscard]] inline std::string ppCsvField(const std::string& s) {
    std::string q = "\"";
    for (const char c : s) {
        if (c == '"')
            q += "\"\"";
        else
            q += c;
    }
    return q + "\"";
}

/// Truncates the CSV and writes the header (once per process).
inline void ppCsvBegin() {
    const std::filesystem::path p = ppEvidenceCsvPath();
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::trunc);
    out << "row,note,descriptor,tap,phase9_value,engine_direct,param_path,bar,out_descriptor,pass\n";
    CHECK(out.good());
}

inline void ppCsvAppend(const PpBarRow& b, const PpReading& direct, const PpReading& host, bool drift, bool pass) {
    std::ostringstream os;
    os << std::setprecision(6);
    os << b.id << ',' << auNoteName(b.note) << ',' << ppCsvField(b.descriptor) << ',' << ppCsvField(b.tap) << ','
       << b.phase9Value << ',' << direct.value << ',';
    if (drift)
        os << "\"not run: descriptor port drift\"";
    else
        os << host.value;
    std::ostringstream outText;
    outText << std::setprecision(6) << b.outDescriptor << ": " << host.outLo << " -> " << host.outHi;
    os << ',' << b.bar << ',' << ppCsvField(drift ? std::string("n/a") : outText.str()) << ','
       << (pass ? "PASS" : "FAIL") << '\n';
    std::cout << os.str() << std::flush;
    std::ofstream out(ppEvidenceCsvPath(), std::ios::app);
    out << os.str();
    CHECK(out.good());
}

}  // namespace ProfundumPluginTest
