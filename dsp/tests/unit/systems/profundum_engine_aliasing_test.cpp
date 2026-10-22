// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_aliasing_test.cpp
// ==============================================================================
// Holds: SC-021 (FR-082; plan C-3, C-21) - 18 [long] shards, the [long] Drop-tail case and the per-push Smoke
// sibling (tasks.md T018, deviation 5). T001 registered this TU in dsp_systems_tests (shipping FP mode).
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on.
//
//   VoiceEngine_Aliasing_{44k1,48k,96k}_{Tube,Fold,Rectify,Bite,Crush,Fracture}  [sc-021][long]
//     Setting (spec SC-021): E0, E1, E2; excitation mix 1 / drive 1 / asymmetry 1; SHAPE in {0, 1}; Fold 100; the
//     shard's algorithm at Amount 1, Tone in {0, 1}; Anchor level 1 in LOCK and FOLLOW; anchorFoldFeed 1; WIDE at
//     spread 1; Mass 100; MONO. Protocol: one held note from sample 0, no Drop, no glide, Motion Off, A = B (the
//     morph static). After a 1 s settle, one frame of aliasFftLength(fs, f_m) samples on L and R separately, the
//     worst channel gated. f_m = f0, except f0 / 2 for Fracture and for FOLLOW with r = 0.5.
//     (a) the Sum tap <= -80 dBFS; (b) the output <= -80 dBFS with the MassDynamics opt-in on (default config);
//     the same cell with config.dynamicsOversampling = false recorded, and per arm the dynamics-added term
//     (output over Sum, Phase 5 SC-010 attribution: 10 log10(max(P_out - g P_sum, 0)), g the h1 power gain at f0
//     on the same frame, bass_character_spectral_test.cpp:961-994). Every cell gated, the worst reported.
//     CSV evidence/aliasing-<fs>-<algorithm>.csv.
//   VoiceEngine_AliasingDropTail  [sc-021][long]
//     (c) a 48 st Drop with the steepest tail (H-9: A 48, T 10 ms, shape 1, rebound 1; 0.418 st/sample recorded
//     at E0 / 44.1 kHz, Phase 4 plan C-9), Phase 4's rendered aliased-power method (SC-015,
//     profundum_note_behaviour_spectral_test.cpp:2196-2262: one aliasFftLength frame starting
//     2 x kControlInterval after the last pitch move, at the held f0) on the CORE tap <= -96 dBFS (C-21); the Sum
//     tap of the same frame recorded. 44.1 / 48 / 96 kHz x E0, E1, E2 x {kSawAnchor, kGrowl} (Phase 4 SC-015's
//     recipes). CSV evidence/aliasing-droptail.csv.
//   VoiceEngine_AliasingSmoke  [sc-021]
//     48 kHz, E1, Tube, Tone 0, SHAPE 0, LOCK: (a) and (b), the same bars. CSV evidence/aliasing-48k-smoke.csv.
//
// Recorded decisions (evidence/run-log/T018.txt):
//   - The spec names no recipe for (a) / (b). The cells run kSawAnchor (the Phase 4 / 5 / 6 aliasing recipe, the
//     worst-case partial set) on A_s (C-8: showcase A with ampEnv.sustain = 1); every other field is the spec's.
//   - (c) runs the core at Phase 4's SC-015 condition (the recipe, ProfundumCore::kDefaultExcitation), the rest of
//     the chain at the smoke cell's SC-021 setting. With excitation 1 the core tap carries Phase 2's own -80 dBFS
//     floor, which is the same "different bar" C-21 rules out for the Sum.
//   - One CSV per shard (aliasing-<fs>-<algorithm>.csv), not one per rate: six shards per rate run concurrently.
//   - f_m: the follow ratio is read per interval from the FollowRatio call records; r = 0.5 only arises from
//     Fracture (bass_character_stage.h:467-480), whose lattice is already f0 / 2, so a 0.5 on any other algorithm
//     fails the cell (the frame length was chosen for f0).
//   - Non-vacuity guards (Phase 4 SC-015's): excluded fraction <= 0.5; the frame's total level > -60 dBFS.
//
// TU-local names carry the `vea` / `Vea` / `kVea` prefix inside an anonymous namespace.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <iomanip>
#include <iostream>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace veaPt = Krate::DSP::ProfundumTest;
using VeaAlg = BassCharacterStage::Algorithm;
using VeaMode = AnchorOscillator::Mode;

constexpr double kVeaBarDbfs = -80.0;        // SC-021(a), (b) (Phase 2 / 5 bar; Q4)
constexpr double kVeaDropBarDbfs = -96.0;    // SC-021(c) (Phase 4 SC-015 bar; C-21)
constexpr double kVeaSettleS = 1.0;          // SC-021 protocol
constexpr double kVeaExcludedMax = 0.5;      // Phase 4 SC-015 non-vacuity guard (spectral_test.cpp:2261)
constexpr double kVeaSignalFloorDb = -60.0;  // the frame is not silence
constexpr double kVeaFinalF0Tol = 1e-5;      // Phase 4 SC-015 (spectral_test.cpp:2255)

constexpr float kVeaDropAmountSt = 48.0f;    // H-9: A 48, T 10 ms, shape 1, rebound 1 (Phase 4 plan C-9)
constexpr float kVeaDropTimeMs = 10.0f;
constexpr float kVeaDropShape = 1.0f;
constexpr float kVeaDropRebound = 1.0f;
constexpr double kVeaDropHeadS = 0.25;       // the trajectory must end inside the head
constexpr double kVeaH9SlopeStPerSample = 0.418;

constexpr std::array<std::uint8_t, 3> kVeaNotes{veaPt::kVoiceEngineNoteE0, veaPt::kVoiceEngineNoteE1,
                                                veaPt::kVoiceEngineNoteE2};

[[nodiscard]] std::string veaEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

[[nodiscard]] const char* veaAlgName(VeaAlg a) {
    switch (a) {
        case VeaAlg::Tube: return "Tube";
        case VeaAlg::Fold: return "Fold";
        case VeaAlg::Rectify: return "Rectify";
        case VeaAlg::Bite: return "Bite";
        case VeaAlg::Crush: return "Crush";
        case VeaAlg::Fracture: return "Fracture";
    }
    return "?";
}

[[nodiscard]] const char* veaModeName(VeaMode m) {
    switch (m) {
        case VeaMode::Lock: return "LOCK";
        case VeaMode::Follow: return "FOLLOW";
        case VeaMode::Punch: return "PUNCH";
        case VeaMode::Ghost: return "GHOST";
    }
    return "?";
}

[[nodiscard]] const char* veaNoteName(std::uint8_t n) {
    if (n == veaPt::kVoiceEngineNoteE0)
        return "E0";
    if (n == veaPt::kVoiceEngineNoteE1)
        return "E1";
    if (n == veaPt::kVoiceEngineNoteE2)
        return "E2";
    return "?";
}

[[nodiscard]] std::string veaFsTag(double fs) {
    if (fs == 44100.0)
        return "44k1";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0fk", fs / 1000.0);
    return buf;
}

[[nodiscard]] double veaElapsedS(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/// BH7-windowed single-bin complex DFT at `hz` against a precomputed window (sample 0 = t 0); the arithmetic of
/// goertzelPhasor (profundum_excitation_test_helpers.h:124) without rebuilding the window.
[[nodiscard]] std::complex<double> veaPhasor(const float* x, const std::vector<double>& w, std::size_t L, double fs,
                                             double hz) {
    REQUIRE(w.size() >= L);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = veaPt::kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return {re, im};
}

// -----------------------------------------------------------------------------
// Setting and script
// -----------------------------------------------------------------------------

struct VeaCell {
    double fs = 48000.0;
    std::uint8_t note = veaPt::kVoiceEngineNoteE1;
    VeaAlg alg = VeaAlg::Tube;
    float shape = 0.0f;
    float tone = 0.0f;
    VeaMode mode = VeaMode::Lock;
};

/// The SC-021 setting on A_s (C-8), kSawAnchor (recorded decision).
[[nodiscard]] MorphState veaState(const VeaCell& c) {
    MorphState s = veaPt::voiceEngineAs(BassMorphTest::showcasePair().first);
    s.recipe = SpectralShapeRecipe::kSawAnchor;
    s.excitation.mix = 1.0f;
    s.excitation.drive = 1.0f;
    s.excitation.asymmetry = 1.0f;
    s.shape = c.shape;
    s.foldAmount = 100.0f;
    s.characterAlgorithm = c.alg;
    s.characterAmount = 1.0f;
    s.characterTone = c.tone;
    s.dropAmountSt = 0.0f;
    s.anchor.level = 1.0f;
    s.anchor.mode = c.mode;
    s.anchorFoldFeed = 1.0f;
    s.field.mode = BassStereoField::ImageMode::Wide;
    s.field.spread = 1.0f;
    s.motion.source = MorphState::MotionConfig::Source::Off;
    s.mass = 100.0f;
    REQUIRE(isValidMorphState(s));
    return s;
}

/// MONO, one held note from sample 0, A = B = s, macros neutral, no glide (portamento 0, the engine default).
[[nodiscard]] veaPt::VoiceEngineScript veaScript(const MorphState& s, double fs, std::uint8_t note,
                                                 std::uint64_t total, bool oversampling, std::size_t tapped) {
    veaPt::VoiceEngineScript sc;
    sc.fs = fs;
    sc.hostBlock = 64;
    sc.totalSamples = total;
    sc.a = s;
    sc.b = s;
    sc.mode = ProfundumEngine::VoiceMode::Mono;
    sc.attachObserver = true;
    sc.tappedVoices = tapped;
    sc.config.dynamicsOversampling = oversampling;
    sc.events.push_back(
        veaPt::VoiceEngineEvent{.sample = 0, .kind = veaPt::VoiceEngineEventKind::NoteOn, .note = note});
    return sc;
}

// -----------------------------------------------------------------------------
// Readers
// -----------------------------------------------------------------------------

/// One tap's reading on one frame, L and R separately, worst channel.
struct VeaTapRead {
    double l = 0.0;
    double r = 0.0;
    double excluded = 0.0;
    double totalDb = 0.0;   // the quieter channel's total level (non-vacuity)
    [[nodiscard]] double worst() const noexcept { return std::max(l, r); }
};

[[nodiscard]] VeaTapRead veaReadTap(const std::vector<float>& L, const std::vector<float>& R, std::size_t start,
                                    std::size_t len, double fs, double fm) {
    REQUIRE(start + len <= L.size());
    REQUIRE(start + len <= R.size());
    const veaPt::AliasResult a = veaPt::aliasedPower(L.data() + start, len, fs, fm);
    const veaPt::AliasResult b = veaPt::aliasedPower(R.data() + start, len, fs, fm);
    return VeaTapRead{.l = a.aliasedDbfs,
                      .r = b.aliasedDbfs,
                      .excluded = std::max(a.excludedFraction, b.excludedFraction),
                      .totalDb = std::min(a.totalDb, b.totalDb)};
}

/// Phase 5 SC-010 attribution of one channel: 10 log10(max(P_out - g P_in, 0)), g = |OUT(f0)|^2 / |IN(f0)|^2.
[[nodiscard]] double veaAddedDb(const float* in, const float* out, double aInDb, double aOutDb,
                                const std::vector<double>& w, std::size_t len, double fs, double f0) {
    const double pin = std::norm(veaPhasor(in, w, len, fs, f0));
    const double pout = std::norm(veaPhasor(out, w, len, fs, f0));
    const double g = pout / std::max(pin, 1e-300);
    const double pOut = std::pow(10.0, aOutDb / 10.0);
    const double pIn = std::pow(10.0, aInDb / 10.0);
    return veaPt::safeDb10(std::max(pOut - g * pIn, 0.0));
}

/// One arm (opt-in on or off): the Sum and the output on the same frame, plus the dynamics-added term.
struct VeaArm {
    VeaTapRead sum{};
    VeaTapRead out{};
    double addedDb = 0.0;   // worst channel
    float followMin = 1.0f;
    float followMax = 1.0f;
    std::uint64_t dropped = 0;
};

[[nodiscard]] VeaArm veaRenderArm(const VeaCell& c, const MorphState& s, std::size_t start, std::size_t len,
                                  double f0, double fm, bool oversampling, const std::vector<double>& w) {
    const veaPt::VoiceEngineTaps t =
        veaPt::voiceEngineRender(veaScript(s, c.fs, c.note, start + len, oversampling, 0));
    VeaArm a;
    a.dropped = t.droppedEvents;
    a.sum = veaReadTap(t.sumL, t.sumR, start, len, c.fs, fm);
    a.out = veaReadTap(t.outL, t.outR, start, len, c.fs, fm);
    const double addL = veaAddedDb(t.sumL.data() + start, t.outL.data() + start, a.sum.l, a.out.l, w, len, c.fs, f0);
    const double addR = veaAddedDb(t.sumR.data() + start, t.outR.data() + start, a.sum.r, a.out.r, w, len, c.fs, f0);
    a.addedDb = std::max(addL, addR);
    // FOLLOW ratio per interval inside the frame (the FollowRatio record is logged every interval, issued or not).
    bool any = false;
    for (const ProfundumCallRecord& r : t.calls) {
        if (r.call != ProfundumCall::FollowRatio || r.voice != 0)
            continue;
        if (r.sample < start || r.sample >= start + len)
            continue;
        if (!any) {
            a.followMin = r.value;
            a.followMax = r.value;
            any = true;
        } else {
            a.followMin = std::min(a.followMin, r.value);
            a.followMax = std::max(a.followMax, r.value);
        }
    }
    REQUIRE(any);
    return a;
}

struct VeaCellResult {
    VeaCell cell{};
    double f0 = 0.0;
    double fm = 0.0;
    std::size_t start = 0;
    std::size_t len = 0;
    VeaArm on{};
    VeaArm off{};
};

/// One SC-021 (a) / (b) cell: the opt-in-on arm gated, the off arm recorded.
[[nodiscard]] VeaCellResult veaRunCell(const VeaCell& c) {
    VeaCellResult r;
    r.cell = c;
    r.f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(c.note)));
    const bool fracture = c.alg == VeaAlg::Fracture;
    r.fm = fracture ? 0.5 * r.f0 : r.f0;
    r.len = veaPt::aliasFftLength(c.fs, r.fm);
    r.start = static_cast<std::size_t>(std::llround(kVeaSettleS * c.fs));
    const MorphState s = veaState(c);
    const std::vector<double> w = veaPt::bh7Window(r.len);

    INFO("SC-021 " << veaFsTag(c.fs) << " " << veaNoteName(c.note) << " " << veaAlgName(c.alg) << " SHAPE "
                   << c.shape << " Tone " << c.tone << " " << veaModeName(c.mode) << ": f0 " << r.f0 << " Hz, f_m "
                   << r.fm << " Hz, frame " << r.start << " + " << r.len);

    r.on = veaRenderArm(c, s, r.start, r.len, r.f0, r.fm, true, w);
    r.off = veaRenderArm(c, s, r.start, r.len, r.f0, r.fm, false, w);

    REQUIRE(r.on.dropped == 0);
    REQUIRE(r.off.dropped == 0);
    // f_m rule: r = 0.5 (FOLLOW) needs the f0 / 2 lattice, which only Fracture's frame was sized for.
    const bool followHalf = c.mode == VeaMode::Follow && (r.on.followMin == 0.5f || r.off.followMin == 0.5f);
    INFO("follow ratio on [" << r.on.followMin << ", " << r.on.followMax << "], off [" << r.off.followMin << ", "
                             << r.off.followMax << "]");
    CHECK((!followHalf || fracture));

    INFO("(a) Sum L " << r.on.sum.l << " R " << r.on.sum.r << " dBFS; (b) output (opt-in on) L " << r.on.out.l
                      << " R " << r.on.out.r << " dBFS, dynamics-added " << r.on.addedDb
                      << " dBFS; opt-in off: output " << r.off.out.worst() << " dBFS, dynamics-added "
                      << r.off.addedDb << " dBFS; excluded " << r.on.sum.excluded << ", total " << r.on.sum.totalDb
                      << " / " << r.on.out.totalDb << " dBFS");
    CHECK(r.on.sum.excluded <= kVeaExcludedMax);
    CHECK(r.on.sum.totalDb > kVeaSignalFloorDb);
    CHECK(r.on.out.totalDb > kVeaSignalFloorDb);
    // (a). G14-13 (user ruling 2026-10-21): the E2 Rectify Tone 1 cells below 96 kHz are recorded (-78 .. -80 dBFS on
    // the WIDE arm: the compounded chain's own aliasing at its hottest Rectify setting on the parity-panned
    // half-spectrum channel; SOLID -85, feed 0 -91, 96 kHz green); every other cell is gated.
    const bool recordedA = c.alg == VeaAlg::Rectify && c.tone == 1.0f && c.note == veaPt::kVoiceEngineNoteE2
                           && c.fs < 96000.0;   // below 96 kHz (kVea96k is declared after this function)
    if (recordedA)
        INFO("G14-13: E2 Rectify Tone 1 below 96 kHz, (a) recorded");
    CHECK((recordedA || r.on.sum.worst() <= kVeaBarDbfs));   // (a)
    // (b) at the engine output: recorded (G14-4, user ruling 2026-10-21). The off-lattice content here is the
    // TruePeakLimiter riding its gain on a sustained tone (E2 hot cells: limiter input -71, output -48 dBFS) and the
    // compressor's program-dependent release at heavy gain reduction (E0 Mass 100: dynamics tap -66 vs -105 at
    // Mass 50), not aliasing; the oversampled sidechain's own aliasing is gated standalone with the release pinned
    // (VoiceEngine_DynamicsAliasingGrid, profundum_engine_probe_test.cpp).
    return r;
}

void veaWriteCsvHeader(std::ofstream& csv) {
    csv << "fs,note,algorithm,shape,tone,anchor_mode,f0_hz,fm_hz,frame_start,frame_len,excluded_fraction,"
           "follow_ratio_min,follow_ratio_max,sum_L_dbfs,sum_R_dbfs,sum_worst_dbfs,out_on_L_dbfs,out_on_R_dbfs,"
           "out_on_worst_dbfs,dyn_added_on_dbfs,sum_off_worst_dbfs,out_off_worst_dbfs,dyn_added_off_dbfs,bar_dbfs,"
           "pass_a,pass_b\n";
}

void veaWriteCsvRow(std::ofstream& csv, const VeaCellResult& r) {
    char line[768];
    const bool passA = r.on.sum.worst() <= kVeaBarDbfs;
    const bool passB = r.on.out.worst() <= kVeaBarDbfs;
    std::snprintf(line, sizeof(line),
                  "%.0f,%s,%s,%.1f,%.1f,%s,%.6f,%.6f,%zu,%zu,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                  "%.3f,%.3f,%.1f,%d,%d\n",
                  r.cell.fs, veaNoteName(r.cell.note), veaAlgName(r.cell.alg), static_cast<double>(r.cell.shape),
                  static_cast<double>(r.cell.tone), veaModeName(r.cell.mode), r.f0, r.fm, r.start, r.len,
                  r.on.sum.excluded, static_cast<double>(r.on.followMin), static_cast<double>(r.on.followMax),
                  r.on.sum.l, r.on.sum.r, r.on.sum.worst(), r.on.out.l, r.on.out.r, r.on.out.worst(), r.on.addedDb,
                  r.off.sum.worst(), r.off.out.worst(), r.off.addedDb, kVeaBarDbfs, passA ? 1 : 0, passB ? 1 : 0);
    csv << line;
}

/// Runs `cells`, writes evidence/<csvName>, prints every cell and the worst (a) / (b) readings.
void veaRunCells(const std::vector<VeaCell>& cells, const std::string& csvName, const char* label) {
    const auto t0 = std::chrono::steady_clock::now();
    std::filesystem::create_directories(veaEvidenceDir());
    std::ofstream csv(veaEvidenceDir() + csvName);
    CHECK(csv.is_open());
    veaWriteCsvHeader(csv);
    double worstA = -1.0e9;
    double worstB = -1.0e9;
    double worstOff = -1.0e9;
    std::string worstACell;
    std::string worstBCell;
    for (const VeaCell& c : cells) {
        const VeaCellResult r = veaRunCell(c);
        veaWriteCsvRow(csv, r);
        char name[160];
        std::snprintf(name, sizeof(name), "%s %s %s SHAPE %.0f Tone %.0f %s", veaFsTag(c.fs).c_str(),
                      veaNoteName(c.note), veaAlgName(c.alg), static_cast<double>(c.shape),
                      static_cast<double>(c.tone), veaModeName(c.mode));
        std::printf("SC-021 %s: (a) Sum %.2f dBFS, (b) out on %.2f dBFS (added %.2f), off %.2f dBFS (added %.2f); "
                    "bar -80\n",
                    name, r.on.sum.worst(), r.on.out.worst(), r.on.addedDb, r.off.out.worst(), r.off.addedDb);
        if (r.on.sum.worst() > worstA) {
            worstA = r.on.sum.worst();
            worstACell = name;
        }
        if (r.on.out.worst() > worstB) {
            worstB = r.on.out.worst();
            worstBCell = name;
        }
        worstOff = std::max(worstOff, r.off.out.worst());
    }
    std::printf("%s: %zu cells, worst (a) Sum %.2f dBFS (%s), worst (b) output %.2f dBFS (%s), worst opt-in-off "
                "output %.2f dBFS (recorded); bar -80; wall clock %.1f s\n",
                label, cells.size(), worstA, worstACell.c_str(), worstB, worstBCell.c_str(), worstOff,
                veaElapsedS(t0));
}

/// One [long] shard: one rate x one algorithm, every note x SHAPE x Tone x Anchor mode.
void veaRunShard(double fs, VeaAlg alg) {
    std::vector<VeaCell> cells;
    for (const std::uint8_t note : kVeaNotes) {
        for (const float shape : {0.0f, 1.0f}) {
            for (const float tone : {0.0f, 1.0f}) {
                for (const VeaMode mode : {VeaMode::Lock, VeaMode::Follow}) {
                    cells.push_back(
                        VeaCell{.fs = fs, .note = note, .alg = alg, .shape = shape, .tone = tone, .mode = mode});
                }
            }
        }
    }
    const std::string label = "SC-021 shard " + veaFsTag(fs) + " " + veaAlgName(alg);
    veaRunCells(cells, "aliasing-" + veaFsTag(fs) + "-" + veaAlgName(alg) + ".csv", label.c_str());
}

// -----------------------------------------------------------------------------
// (c) the 48 st Drop tail (C-21)
// -----------------------------------------------------------------------------

struct VeaDropResult {
    std::size_t trajEnd = 0;
    std::size_t frameStart = 0;
    std::size_t len = 0;
    double fFinal = 0.0;
    double maxUpSlopeSt = 0.0;
    VeaTapRead core{};
    VeaTapRead sum{};
};

[[nodiscard]] VeaDropResult veaRunDropCell(double fs, std::uint8_t note, const SpectralShapeRecipe::Controls& recipe,
                                           const char* recipeName) {
    // The smoke cell's SC-021 setting downstream; the core at Phase 4 SC-015's condition (recorded decision).
    MorphState s = veaState(VeaCell{.fs = fs, .note = note});
    s.recipe = recipe;
    s.excitation = ProfundumCore::kDefaultExcitation;
    s.dropAmountSt = kVeaDropAmountSt;
    s.dropTimeMs = kVeaDropTimeMs;
    s.dropShape = kVeaDropShape;
    s.dropRebound = kVeaDropRebound;
    REQUIRE(isValidMorphState(s));

    const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(note)));
    VeaDropResult r;
    r.len = veaPt::aliasFftLength(fs, f0);
    const auto head = static_cast<std::size_t>(std::llround(kVeaDropHeadS * fs));
    const std::size_t margin = 2 * veaPt::kVoiceEngineGrid;
    const std::size_t total = head + margin + r.len;
    INFO("SC-021(c) " << veaFsTag(fs) << " " << veaNoteName(note) << " " << recipeName << ": Drop A "
                      << kVeaDropAmountSt << " st, T " << kVeaDropTimeMs << " ms, shape " << kVeaDropShape
                      << ", rebound " << kVeaDropRebound);
    const veaPt::VoiceEngineTaps t = veaPt::voiceEngineRender(veaScript(s, fs, note, total, true, 1));
    REQUIRE(t.droppedEvents == 0);
    const std::vector<float>& fc = t.voices[0].fCore;
    REQUIRE(fc.size() == total);

    // The trajectory ends at the last commanded pitch move inside the head (Phase 4 SC-015).
    for (std::size_t n = 1; n < head; ++n) {
        if (fc[n] != fc[n - 1])
            r.trajEnd = n + 1;
        if (fc[n] > 0.0f && fc[n - 1] > 0.0f) {
            const double st = 12.0 * std::log2(static_cast<double>(fc[n]) / static_cast<double>(fc[n - 1]));
            r.maxUpSlopeSt = std::max(r.maxUpSlopeSt, st);
        }
    }
    REQUIRE(r.trajEnd > 0);   // the Drop moved the pitch
    bool heldAfterHead = true;
    for (std::size_t n = head; n < total; ++n)
        heldAfterHead = heldAfterHead && fc[n] == fc[head - 1];
    REQUIRE(heldAfterHead);   // and it ended inside the head
    r.frameStart = r.trajEnd + margin;
    REQUIRE(r.frameStart + r.len <= total);
    r.fFinal = static_cast<double>(fc[r.frameStart]);
    INFO("trajectory end " << r.trajEnd << ", frame " << r.frameStart << " + " << r.len << ", f " << r.fFinal
                           << " Hz, steepest upward step " << r.maxUpSlopeSt << " st/sample (H-9 "
                           << kVeaH9SlopeStPerSample << ")");
    CHECK(std::abs(r.fFinal / f0 - 1.0) <= kVeaFinalF0Tol);

    r.core = veaReadTap(t.coreL, t.coreR, r.frameStart, r.len, fs, r.fFinal);
    r.sum = veaReadTap(t.sumL, t.sumR, r.frameStart, r.len, fs, r.fFinal);
    INFO("core L " << r.core.l << " R " << r.core.r << " dBFS (bar -96); Sum " << r.sum.worst()
                   << " dBFS (recorded); excluded " << r.core.excluded << ", core total " << r.core.totalDb << " dBFS");
    CHECK(r.core.excluded <= kVeaExcludedMax);
    CHECK(r.core.totalDb > kVeaSignalFloorDb);
    CHECK(r.core.worst() <= kVeaDropBarDbfs);   // (c), the core tap (C-21)
    return r;
}

// -----------------------------------------------------------------------------
// Cells
// -----------------------------------------------------------------------------

constexpr double kVea44k1 = 44100.0;
constexpr double kVea48k = 48000.0;
constexpr double kVea96k = 96000.0;

}  // namespace

// =============================================================================
// SC-021 (a) / (b): 18 [long] shards (one per rate x algorithm)
// =============================================================================

TEST_CASE("VoiceEngine_Aliasing_44k1_Tube", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea44k1, VeaAlg::Tube);
}
TEST_CASE("VoiceEngine_Aliasing_44k1_Fold", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea44k1, VeaAlg::Fold);
}
TEST_CASE("VoiceEngine_Aliasing_44k1_Rectify", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea44k1, VeaAlg::Rectify);
}
TEST_CASE("VoiceEngine_Aliasing_44k1_Bite", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea44k1, VeaAlg::Bite);
}
TEST_CASE("VoiceEngine_Aliasing_44k1_Crush", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea44k1, VeaAlg::Crush);
}
TEST_CASE("VoiceEngine_Aliasing_44k1_Fracture", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea44k1, VeaAlg::Fracture);
}

TEST_CASE("VoiceEngine_Aliasing_48k_Tube", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea48k, VeaAlg::Tube);
}
TEST_CASE("VoiceEngine_Aliasing_48k_Fold", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea48k, VeaAlg::Fold);
}
TEST_CASE("VoiceEngine_Aliasing_48k_Rectify", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea48k, VeaAlg::Rectify);
}
TEST_CASE("VoiceEngine_Aliasing_48k_Bite", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea48k, VeaAlg::Bite);
}
TEST_CASE("VoiceEngine_Aliasing_48k_Crush", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea48k, VeaAlg::Crush);
}
TEST_CASE("VoiceEngine_Aliasing_48k_Fracture", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea48k, VeaAlg::Fracture);
}

TEST_CASE("VoiceEngine_Aliasing_96k_Tube", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea96k, VeaAlg::Tube);
}
TEST_CASE("VoiceEngine_Aliasing_96k_Fold", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea96k, VeaAlg::Fold);
}
TEST_CASE("VoiceEngine_Aliasing_96k_Rectify", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea96k, VeaAlg::Rectify);
}
TEST_CASE("VoiceEngine_Aliasing_96k_Bite", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea96k, VeaAlg::Bite);
}
TEST_CASE("VoiceEngine_Aliasing_96k_Crush", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea96k, VeaAlg::Crush);
}
TEST_CASE("VoiceEngine_Aliasing_96k_Fracture", "[systems][profundum][voice-engine][sc-021][fr-082][long]") {
    veaRunShard(kVea96k, VeaAlg::Fracture);
}

// =============================================================================
// SC-021 (c): the 48 st Drop tail on the core tap (C-21), the Sum recorded
// =============================================================================

TEST_CASE("VoiceEngine_AliasingDropTail", "[systems][profundum][voice-engine][sc-021][c-21][long]") {
    const auto t0 = std::chrono::steady_clock::now();
    std::filesystem::create_directories(veaEvidenceDir());
    std::ofstream csv(veaEvidenceDir() + "aliasing-droptail.csv");
    CHECK(csv.is_open());
    csv << "fs,note,recipe,drop_amount_st,drop_time_ms,drop_shape,drop_rebound,traj_end,frame_start,frame_len,"
           "f_final_hz,max_up_slope_st_per_sample,excluded_fraction,core_L_dbfs,core_R_dbfs,core_worst_dbfs,"
           "sum_worst_dbfs,bar_dbfs,pass\n";
    struct VeaRecipe {
        const SpectralShapeRecipe::Controls* controls;
        const char* name;
    };
    const std::array<VeaRecipe, 2> recipes{VeaRecipe{&SpectralShapeRecipe::kSawAnchor, "kSawAnchor"},
                                           VeaRecipe{&SpectralShapeRecipe::kGrowl, "kGrowl"}};
    double worstCore = -1.0e9;
    double worstSum = -1.0e9;
    double steepest = 0.0;
    std::string worstCell;
    std::size_t cells = 0;
    for (const double fs : {kVea44k1, kVea48k, kVea96k}) {
        for (const std::uint8_t note : kVeaNotes) {
            for (const VeaRecipe& rc : recipes) {
                const VeaDropResult r = veaRunDropCell(fs, note, *rc.controls, rc.name);
                ++cells;
                char line[512];
                std::snprintf(line, sizeof(line),
                              "%.0f,%s,%s,%.1f,%.1f,%.1f,%.1f,%zu,%zu,%zu,%.6f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.1f,%d\n",
                              fs, veaNoteName(note), rc.name, static_cast<double>(kVeaDropAmountSt),
                              static_cast<double>(kVeaDropTimeMs), static_cast<double>(kVeaDropShape),
                              static_cast<double>(kVeaDropRebound), r.trajEnd, r.frameStart, r.len, r.fFinal,
                              r.maxUpSlopeSt, r.core.excluded, r.core.l, r.core.r, r.core.worst(), r.sum.worst(),
                              kVeaDropBarDbfs, r.core.worst() <= kVeaDropBarDbfs ? 1 : 0);
                csv << line;
                std::printf("SC-021(c) %s %s %s: core %.2f dBFS (bar -96), Sum %.2f dBFS (recorded), steepest upward "
                            "step %.4f st/sample, frame %zu + %zu at %.4f Hz\n",
                            veaFsTag(fs).c_str(), veaNoteName(note), rc.name, r.core.worst(), r.sum.worst(),
                            r.maxUpSlopeSt, r.frameStart, r.len, r.fFinal);
                if (r.core.worst() > worstCore) {
                    worstCore = r.core.worst();
                    worstCell = veaFsTag(fs) + " " + veaNoteName(note) + " " + rc.name;
                }
                worstSum = std::max(worstSum, r.sum.worst());
                steepest = std::max(steepest, r.maxUpSlopeSt);
            }
        }
    }
    std::printf("SC-021(c): %zu cells, worst core %.2f dBFS (%s; bar -96), worst Sum %.2f dBFS (recorded), steepest "
                "upward step %.4f st/sample (H-9 %.3f), wall clock %.1f s\n",
                cells, worstCore, worstCell.c_str(), worstSum, steepest, kVeaH9SlopeStPerSample, veaElapsedS(t0));
}

// =============================================================================
// Per-push smoke (deviation 5): 48 kHz, E1, Tube, Tone 0, SHAPE 0, LOCK - (a) and (b), the same bars
// =============================================================================

TEST_CASE("VoiceEngine_AliasingSmoke", "[systems][profundum][voice-engine][sc-021][fr-082]") {
    veaRunCells({VeaCell{.fs = kVea48k,
                         .note = veaPt::kVoiceEngineNoteE1,
                         .alg = VeaAlg::Tube,
                         .shape = 0.0f,
                         .tone = 0.0f,
                         .mode = VeaMode::Lock}},
                "aliasing-48k-smoke.csv", "SC-021 smoke");
}

// =============================================================================
// Main-loop probe (hidden; Group 14, 2026-10-21). Not a gate: it prints.
// =============================================================================

/// G14-4: SC-021(b)'s attribution along the global chain. The first grid read the output at -46.7 dBFS on the hot
/// E2 Tube SHAPE 1 Tone 1 cells with the MassDynamics opt-in on and off alike, so the stage that adds the
/// off-lattice content is read tap by tap: Sum, dynamics, field stage, DC (the limiter's input) and the output.
TEST_CASE("VoiceEngine_AliasingChainProbe", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    struct P {
        std::uint8_t note;
        float shape, tone;
        float mass = 100.0f;
        bool solid = false;
    };
    for (const P p : {P{veaPt::kVoiceEngineNoteE2, 1.0f, 1.0f}, P{veaPt::kVoiceEngineNoteE1, 1.0f, 1.0f},
                      P{16, 0.0f, 0.0f}, P{veaPt::kVoiceEngineNoteE2, 0.0f, 0.0f},
                      P{16, 0.0f, 0.0f, 50.0f}, P{16, 0.0f, 0.0f, 0.0f}, P{16, 0.0f, 0.0f, 100.0f, true}}) {
        VeaCell c;
        c.fs = 48000.0;
        c.note = p.note;
        c.alg = VeaAlg::Tube;
        c.shape = p.shape;
        c.tone = p.tone;
        c.mode = VeaMode::Lock;
        const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(c.note)));
        const std::size_t len = veaPt::aliasFftLength(c.fs, f0);
        const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * c.fs));
        MorphState s = veaState(c);
        s.mass = p.mass;
        if (p.solid)
            s.field.mode = BassStereoField::ImageMode::Solid;
        REQUIRE(isValidMorphState(s));
        const veaPt::VoiceEngineTaps t = veaPt::voiceEngineRender(veaScript(s, c.fs, c.note, start + len, true, 0));
        const auto rd = [&](const std::vector<float>& L, const std::vector<float>& R) {
            if (L.size() < start + len || R.size() < start + len)
                return 999.0;
            return veaReadTap(L, R, start, len, c.fs, f0).worst();
        };
        std::cout << std::setprecision(5) << "PROBE SC-021 48k " << veaNoteName(c.note) << " Tube SHAPE " << c.shape
                  << " Tone " << c.tone << " LOCK (Mass " << p.mass << ", " << (p.solid ? "SOLID" : "WIDE spread 1")
                  << "): aliased dBFS sum " << rd(t.sumL, t.sumR)
                  << " dyn " << rd(t.dynL, t.dynR) << " field " << rd(t.stageOutL, t.stageOutR) << " dc(limiter in) "
                  << rd(t.dcL, t.dcR) << " out " << rd(t.outL, t.outR) << " (999 = tap not captured)\n";
    }
}

/// Round 6 helper: the inter-harmonic power of one channel binned by its offset from the nearest harmonic
/// (0.25 Hz buckets, folded to [0, f_m / 2]), the aliasedPower mask; the 8 / 16 / 24 / 32 Hz family and the rest.
struct VeaOffsetHist {
    double total = -300.0;
    double family = -300.0;
    double rest = -300.0;
    std::vector<std::pair<double, double>> top;   // (offset Hz, dBFS)
};

[[nodiscard]] VeaOffsetHist veaOffsetHist(const float* x, std::size_t len, double fs, double fm) {
    const std::vector<double> ph = veaPt::bh7PowerSpectrum(x, len);
    const double binHz = fs / static_cast<double>(len);
    const std::size_t half = len / 2;
    std::vector<char> ex(half + 1, 0);
    for (std::size_t k = 0; k <= std::min<std::size_t>(8, half); ++k)
        ex[k] = 1;
    for (int n = 1; static_cast<double>(n) * fm < fs * 0.5; ++n) {
        const double centre = static_cast<double>(n) * fm / binHz;
        const double lo = std::max(0.0, std::ceil(centre - 8.0));
        const double hi = std::min(static_cast<double>(half), std::floor(centre + 8.0));
        for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
            ex[k] = 1;
    }
    constexpr double kBucketHz = 0.25;
    std::vector<double> hist(static_cast<std::size_t>(0.5 * fm / kBucketHz) + 2, 0.0);
    double total = 0.0;
    for (std::size_t k = 1; k <= half; ++k) {
        if (ex[k] != 0)
            continue;
        double off = std::fmod(static_cast<double>(k) * binHz, fm);
        if (off > 0.5 * fm)
            off = fm - off;
        hist[std::min(hist.size() - 1, static_cast<std::size_t>(off / kBucketHz))] += ph[k];
        total += ph[k];
    }
    double family = 0.0;
    std::vector<std::pair<double, std::size_t>> order;
    for (std::size_t b = 0; b < hist.size(); ++b) {
        order.emplace_back(hist[b], b);
        const double o = (static_cast<double>(b) + 0.5) * kBucketHz;
        for (const double h : {8.0, 16.0, 24.0, 32.0})
            if (std::abs(o - h) <= 0.75) {
                family += hist[b];
                break;
            }
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    VeaOffsetHist r;
    r.total = veaPt::safeDb10(total / 0.5);
    r.family = veaPt::safeDb10(family / 0.5);
    r.rest = veaPt::safeDb10(std::max(total - family, 0.0) / 0.5);
    for (std::size_t i = 0; i < std::min<std::size_t>(4, order.size()); ++i)
        r.top.emplace_back((static_cast<double>(order[i].second) + 0.5) * kBucketHz,
                           veaPt::safeDb10(order[i].first / 0.5));
    return r;
}

/// Round 6: the standalone ProfundumCore with its excitation on (no engine, no voice): the 8 Hz family's origin.
TEST_CASE("VoiceEngine_AliasingCoreProbe", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    struct V {
        const char* name;
        float mix, drive, asym;
    };
    const std::vector<V> variants{{"mix 1 drive 1 asym 1", 1.0f, 1.0f, 1.0f}, {"asym 0", 1.0f, 1.0f, 0.0f},
                                  {"drive 0.5", 1.0f, 0.5f, 1.0f},           {"mix 0.5", 0.5f, 1.0f, 1.0f},
                                  {"drive 0", 1.0f, 0.0f, 1.0f},             {"mix 0", 0.0f, 1.0f, 1.0f}};
    struct C {
        double fs;
        std::uint8_t note;
    };
    for (const C cc : {C{kVea48k, veaPt::kVoiceEngineNoteE1}, C{kVea44k1, veaPt::kVoiceEngineNoteE0},
                       C{kVea96k, veaPt::kVoiceEngineNoteE1}}) {
        const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(cc.note)));
        const std::size_t len = veaPt::aliasFftLength(cc.fs, f0);
        const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * cc.fs));
        for (const V& v : variants) {
            ProfundumCore core;
            core.prepare(cc.fs);
            core.setExcitation(ProfundumCore::ExcitationControls{.mix = v.mix, .drive = v.drive, .asymmetry = v.asym});
            const veaPt::Render r = veaPt::renderCore(core, SpectralShapeRecipe::kSawAnchor, static_cast<float>(f0),
                                                      static_cast<double>(start + len) / cc.fs + 0.01);
            REQUIRE(r.L.size() >= start + len);
            const VeaOffsetHist hL = veaOffsetHist(r.L.data() + start, len, cc.fs, f0);
            const VeaOffsetHist hR = veaOffsetHist(r.R.data() + start, len, cc.fs, f0);
            const VeaOffsetHist& h = hR.total > hL.total ? hR : hL;
            std::cout << std::setprecision(5) << "PROBE core " << veaFsTag(cc.fs) << " " << veaNoteName(cc.note) << " ["
                      << v.name << "]: off-lattice " << h.total << " dBFS, 8/16/24/32 Hz family " << h.family
                      << ", rest " << h.rest << "; top";
            for (const auto& [off, db] : h.top)
                std::cout << " " << off << "Hz:" << db;
            std::cout << " dBFS\n";
        }
    }
}

/// Round 7: partial count and the time-domain envelope of the standalone excited core.
TEST_CASE("VoiceEngine_AliasingCoreProbe2", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    const double fsHz = kVea48k;
    const std::uint8_t note = veaPt::kVoiceEngineNoteE1;
    const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(note)));
    const std::size_t len = veaPt::aliasFftLength(fsHz, f0);
    const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * fsHz));
    for (const int partials : {8, 16, 64, 128}) {
        for (const float mix : {1.0f, 0.0f}) {
            ProfundumCore core;
            core.prepare(fsHz, partials);
            core.setExcitation(ProfundumCore::ExcitationControls{.mix = mix, .drive = 1.0f, .asymmetry = 1.0f});
            const veaPt::Render r = veaPt::renderCore(core, SpectralShapeRecipe::kSawAnchor, static_cast<float>(f0),
                                                      static_cast<double>(start + len) / fsHz + 0.01);
            REQUIRE(r.L.size() >= start + len);
            const VeaOffsetHist h = veaOffsetHist(r.L.data() + start, len, fsHz, f0);
            // Envelope: RMS per 5 ms hop over the frame, mean removed, DFT at 0.5 .. 40 Hz in 0.25 Hz steps.
            const auto hop = static_cast<std::size_t>(std::llround(0.005 * fsHz));
            std::vector<double> env;
            for (std::size_t pos = start; pos + hop <= start + len; pos += hop) {
                double acc = 0.0;
                for (std::size_t i = 0; i < hop; ++i)
                    acc += static_cast<double>(r.L[pos + i]) * static_cast<double>(r.L[pos + i]);
                env.push_back(std::sqrt(acc / static_cast<double>(hop)));
            }
            double mean = 0.0;
            for (const double e : env)
                mean += e;
            mean /= static_cast<double>(env.size());
            std::vector<std::pair<double, double>> lines;   // (dB re mean, Hz)
            const double envRate = fsHz / static_cast<double>(hop);
            for (double fh = 0.5; fh <= 40.0; fh += 0.25) {
                std::complex<double> acc{0.0, 0.0};
                for (std::size_t i = 0; i < env.size(); ++i) {
                    const double ph = -2.0 * 3.14159265358979323846 * fh * static_cast<double>(i) / envRate;
                    acc += (env[i] - mean) * std::complex<double>(std::cos(ph), std::sin(ph));
                }
                const double amp = 2.0 * std::abs(acc) / static_cast<double>(env.size());
                lines.emplace_back(20.0 * std::log10(std::max(amp / std::max(mean, 1e-30), 1e-30)), fh);
            }
            std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            std::cout << std::setprecision(5) << "PROBE core2 48k E1 partials " << partials << " mix " << mix
                      << ": off-lattice " << h.total << " dBFS, 8 Hz family " << h.family << ", rest " << h.rest
                      << "; top offsets";
            for (const auto& [off, db] : h.top)
                std::cout << " " << off << "Hz:" << db;
            std::cout << "; RMS-envelope lines (dB re mean)";
            for (std::size_t i = 0; i < std::min<std::size_t>(4, lines.size()); ++i)
                std::cout << " " << lines[i].second << "Hz:" << lines[i].first;
            std::cout << " | mean RMS " << mean << "\n";
        }
    }
}

/// Round 8: the excitation path's linear skeleton, element by element, on the clean core output.
TEST_CASE("VoiceEngine_AliasingCoreProbe3", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    const std::uint8_t note = veaPt::kVoiceEngineNoteE1;
    const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(note)));
    for (const double fsHz : {kVea48k, kVea96k}) {
        const std::size_t len = veaPt::aliasFftLength(fsHz, f0);
        const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * fsHz));
        ProfundumCore core;
        core.prepare(fsHz);
        core.setExcitation(ProfundumCore::ExcitationControls{.mix = 0.0f, .drive = 1.0f, .asymmetry = 1.0f});
        const veaPt::Render r = veaPt::renderCore(core, SpectralShapeRecipe::kSawAnchor, static_cast<float>(f0),
                                                  static_cast<double>(start + len) / fsHz + 0.01);
        REQUIRE(r.L.size() >= start + len);
        const std::vector<float>& x = r.L;
        const auto report = [&](const char* tag, const std::vector<float>& y) {
            const VeaOffsetHist h = veaOffsetHist(y.data() + start, len, fsHz, f0);
            std::cout << std::setprecision(5) << "PROBE core3 " << veaFsTag(fsHz) << " E1 [" << tag << "]: off-lattice "
                      << h.total << " dBFS, 8 Hz family " << h.family << ", rest " << h.rest << "; top";
            for (const auto& [off, db] : h.top)
                std::cout << " " << off << "Hz:" << db;
            std::cout << " dBFS\n";
        };
        report("x (mix 0 core)", x);
        {   // A: the base-rate split LP alone (500 Hz Butterworth, TDF2 biquad).
            Biquad lp;
            lp.configure(FilterType::Lowpass, 500.0f, kButterworthQ, 0.0f,
                         static_cast<float>(fsHz));
            std::vector<float> y(x.size());
            for (std::size_t i = 0; i < x.size(); ++i)
                y[i] = lp.process(x[i]);
            report("A splitBase LP", y);
        }
        for (const std::size_t factor : {std::size_t{2}, std::size_t{4}}) {   // B: halfband up -> down, identity hook.
            PolyphaseHalfbandOversampler os;
            os.setFactor(factor);
            std::array<float, 4> buf{};
            std::vector<float> y(x.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                os.upsample(x[i], buf.data());
                y[i] = os.downsample(buf.data());
            }
            report(factor == 2 ? "B halfband 2x identity" : "B halfband 4x identity", y);
        }
        {   // C: halfband 2x with the three 2fs notches in the hook.
            const std::size_t factor = 2;
            PolyphaseHalfbandOversampler os;
            os.setFactor(factor);
            std::array<Biquad, 3> notch;
            const std::array<float, 3> fr{0.50f, 0.52f, 0.55f};
            const std::array<float, 3> q{1.5f, 2.0f, 1.5f};
            for (std::size_t k = 0; k < 3; ++k)
                notch[k].configure(FilterType::Notch, fr[k] * static_cast<float>(fsHz), q[k], 0.0f,
                                   2.0f * static_cast<float>(fsHz));
            std::array<float, 4> buf{};
            std::vector<float> y(x.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                os.upsample(x[i], buf.data());
                y[i] = os.downsample(buf.data(), [&notch](float t) noexcept {
                    for (Biquad& bq : notch)
                        t = bq.process(t);
                    return t;
                });
            }
            report("C halfband 2x + 2fs notches", y);
        }
        {   // D: the 2 Hz DC blocker alone.
            DCBlocker dc;
            dc.prepare(fsHz, 2.0f);
            std::vector<float> y(x.size());
            for (std::size_t i = 0; i < x.size(); ++i)
                y[i] = dc.process(x[i]);
            report("D DCBlocker 2 Hz", y);
        }
        {   // E: the full linear replica at m = 1, w = 1: lo + D(u - LPo(u)) through the notches and the DC blocker.
            const std::size_t factor = 2;
            PolyphaseHalfbandOversampler os;
            os.setFactor(factor);
            Biquad lp, lpOs;
            lp.configure(FilterType::Lowpass, 500.0f, kButterworthQ, 0.0f,
                         static_cast<float>(fsHz));
            lpOs.configure(FilterType::Lowpass, 500.0f, kButterworthQ, 0.0f,
                           static_cast<float>(fsHz) * static_cast<float>(factor));
            std::array<Biquad, 3> notch;
            const std::array<float, 3> fr{0.50f, 0.52f, 0.55f};
            const std::array<float, 3> q{1.5f, 2.0f, 1.5f};
            for (std::size_t k = 0; k < 3; ++k)
                notch[k].configure(FilterType::Notch, fr[k] * static_cast<float>(fsHz), q[k], 0.0f,
                                   2.0f * static_cast<float>(fsHz));
            DCBlocker dc;
            dc.prepare(fsHz, 2.0f);
            std::array<float, 4> buf{};
            std::vector<float> y(x.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                const float lo = lp.process(x[i]);
                os.upsample(x[i], buf.data());
                for (std::size_t j = 0; j < factor; ++j) {
                    const float u = buf[j];
                    const float ul = lpOs.process(u);
                    buf[j] = u - ul;
                }
                const float res = os.downsample(buf.data(), [&notch](float t) noexcept {
                    for (Biquad& bq : notch)
                        t = bq.process(t);
                    return t;
                });
                y[i] = lo + dc.process(res);
            }
            report("E linear replica lo + D(u - LPo u)", y);
        }
    }
}

/// Round 9 helper: an RBJ Butterworth low-pass, TDF2, with T-precision coefficients and state.
template <class T>
struct VeaLpT {
    T b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void configure(double hz, double fsHz) {
        const double w0 = 2.0 * 3.14159265358979323846 * hz / fsHz;
        const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        b0 = static_cast<T>((1.0 - cw) * 0.5 / a0);
        b1 = static_cast<T>((1.0 - cw) / a0);
        b2 = b0;
        a1 = static_cast<T>(-2.0 * cw / a0);
        a2 = static_cast<T>((1.0 - alpha) / a0);
    }
    T process(T x) {
        const T y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/// Round 9: the linear replica with float vs double split low-passes (halfband, notches, DC blocker as shipped).
TEST_CASE("VoiceEngine_AliasingCoreProbe4", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    const std::uint8_t note = veaPt::kVoiceEngineNoteE1;
    const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(note)));
    for (const double fsHz : {kVea48k, kVea96k}) {
        const std::size_t len = veaPt::aliasFftLength(fsHz, f0);
        const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * fsHz));
        ProfundumCore core;
        core.prepare(fsHz);
        core.setExcitation(ProfundumCore::ExcitationControls{.mix = 0.0f, .drive = 1.0f, .asymmetry = 1.0f});
        const veaPt::Render r = veaPt::renderCore(core, SpectralShapeRecipe::kSawAnchor, static_cast<float>(f0),
                                                  static_cast<double>(start + len) / fsHz + 0.01);
        REQUIRE(r.L.size() >= start + len);
        const std::vector<float>& x = r.L;
        const auto report = [&](const char* tag, const std::vector<float>& y) {
            const VeaOffsetHist h = veaOffsetHist(y.data() + start, len, fsHz, f0);
            std::cout << std::setprecision(5) << "PROBE core4 " << veaFsTag(fsHz) << " E1 [" << tag << "]: off-lattice "
                      << h.total << " dBFS, 8 Hz family " << h.family << ", rest " << h.rest << "; top";
            for (const auto& [off, db] : h.top)
                std::cout << " " << off << "Hz:" << db;
            std::cout << " dBFS\n";
        };
        const auto replica = [&](auto lp, auto lpOs, const char* tag) {
            const std::size_t factor = 2;
            PolyphaseHalfbandOversampler os;
            os.setFactor(factor);
            lp.configure(500.0, fsHz);
            lpOs.configure(500.0, fsHz * static_cast<double>(factor));
            std::array<Biquad, 3> notch;
            const std::array<float, 3> fr{0.50f, 0.52f, 0.55f};
            const std::array<float, 3> q{1.5f, 2.0f, 1.5f};
            for (std::size_t k = 0; k < 3; ++k)
                notch[k].configure(FilterType::Notch, fr[k] * static_cast<float>(fsHz), q[k], 0.0f,
                                   2.0f * static_cast<float>(fsHz));
            DCBlocker dc;
            dc.prepare(fsHz, 2.0f);
            std::array<float, 4> buf{};
            std::vector<float> y(x.size());
            using T = decltype(lp.b0);
            for (std::size_t i = 0; i < x.size(); ++i) {
                const float lo = static_cast<float>(lp.process(static_cast<T>(x[i])));
                os.upsample(x[i], buf.data());
                for (std::size_t j = 0; j < factor; ++j) {
                    const float u = buf[j];
                    const float ul = static_cast<float>(lpOs.process(static_cast<T>(u)));
                    buf[j] = u - ul;
                }
                const float res = os.downsample(buf.data(), [&notch](float t) noexcept {
                    for (Biquad& bq : notch)
                        t = bq.process(t);
                    return t;
                });
                y[i] = lo + dc.process(res);
            }
            report(tag, y);
        };
        report("x (mix 0 core)", x);
        replica(VeaLpT<float>{}, VeaLpT<float>{}, "replica, float split LPs");
        replica(VeaLpT<double>{}, VeaLpT<double>{}, "replica, double split LPs");
        {   // The base-rate split alone, float vs double.
            VeaLpT<float> lf;
            VeaLpT<double> ld;
            lf.configure(500.0, fsHz);
            ld.configure(500.0, fsHz);
            std::vector<float> yf(x.size()), yd(x.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                yf[i] = lf.process(x[i]);
                yd[i] = static_cast<float>(ld.process(static_cast<double>(x[i])));
            }
            report("split LP alone, float", yf);
            report("split LP alone, double", yd);
        }
    }
}

/// G14-13: the ten cells left after G14-12 (half-wave flag flips in FOLLOW; Crush at fractional amounts).
TEST_CASE("VoiceEngine_AliasingG1413Probe", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    // (a) flag flips and FOLLOW ratio records inside the frame.
    struct FC {
        double fs;
        std::uint8_t note;
        float tone;
        VeaMode mode;
        float mass;
    };
    for (const FC fc : {FC{kVea96k, veaPt::kVoiceEngineNoteE1, 1.0f, VeaMode::Follow, 100.0f},
                        FC{kVea96k, veaPt::kVoiceEngineNoteE1, 0.0f, VeaMode::Follow, 100.0f},
                        FC{kVea96k, veaPt::kVoiceEngineNoteE1, 1.0f, VeaMode::Follow, 50.0f},
                        FC{kVea48k, veaPt::kVoiceEngineNoteE2, 1.0f, VeaMode::Follow, 100.0f},
                        FC{kVea44k1, veaPt::kVoiceEngineNoteE2, 1.0f, VeaMode::Lock, 100.0f}}) {
        VeaCell c;
        c.fs = fc.fs;
        c.note = fc.note;
        c.alg = VeaAlg::Rectify;
        c.shape = 0.0f;
        c.tone = fc.tone;
        c.mode = fc.mode;
        MorphState st = veaState(c);
        st.mass = fc.mass;
        const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(c.note)));
        const std::size_t len = veaPt::aliasFftLength(c.fs, f0);
        const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * c.fs));
        const veaPt::VoiceEngineTaps t = veaPt::voiceEngineRender(veaScript(st, c.fs, c.note, start + len, true, 0));
        std::size_t flips = 0, flipsInFrame = 0, ratioChanges = 0;
        float lastHw = -1.0f, lastRatio = -1.0f;
        std::vector<std::uint64_t> flipAt;
        for (const ProfundumCallRecord& r : t.calls) {
            if (r.voice != 0)
                continue;
            if (r.call == ProfundumCall::HalfWave && r.issued) {
                if (lastHw >= 0.0f && r.value != lastHw) {
                    ++flips;
                    if (r.sample >= start && r.sample < start + len) {
                        ++flipsInFrame;
                        if (flipAt.size() < 12)
                            flipAt.push_back(r.sample);
                    }
                }
                lastHw = r.value;
            }
            if (r.call == ProfundumCall::FollowRatio) {
                if (lastRatio >= 0.0f && r.value != lastRatio && r.sample >= start && r.sample < start + len)
                    ++ratioChanges;
                lastRatio = r.value;
            }
        }
        std::cout << std::setprecision(5) << "PROBE G14-13(a) " << veaFsTag(c.fs) << " " << veaNoteName(c.note)
                  << " Rectify SHAPE 0 Tone " << c.tone << " " << veaModeName(c.mode) << " Mass " << fc.mass
                  << ": half-wave flag flips total " << flips << ", inside the frame " << flipsInFrame
                  << " (periods in the frame " << static_cast<double>(len) * f0 / c.fs << "), FOLLOW ratio changes in the frame "
                  << ratioChanges << ", first flips at";
        for (const std::uint64_t smp : flipAt)
            std::cout << " " << smp;
        std::cout << "; Sum " << veaReadTap(t.sumL, t.sumR, start, len, c.fs, f0).worst() << " dBFS\n";
    }
    // (b2) Crush in WIDE at 44.1 kHz E2: one lever at a time.
    {
        struct W {
            const char* name;
            BassStereoField::ImageMode mode;
            float spread, amount, tone;
            std::uint8_t note;
        };
        const std::uint8_t e2 = veaPt::kVoiceEngineNoteE2;
        for (const W w : {W{"WIDE spread 1 (the cell)", BassStereoField::ImageMode::Wide, 1.0f, 1.0f, 0.0f, e2},
                          W{"SPREAD spread 1", BassStereoField::ImageMode::Spread, 1.0f, 1.0f, 0.0f, e2},
                          W{"WIDE spread 0.5", BassStereoField::ImageMode::Wide, 0.5f, 1.0f, 0.0f, e2},
                          W{"WIDE spread 0.25", BassStereoField::ImageMode::Wide, 0.25f, 1.0f, 0.0f, e2},
                          W{"WIDE spread 1 amount 0.5", BassStereoField::ImageMode::Wide, 1.0f, 0.5f, 0.0f, e2},
                          W{"WIDE spread 1 Tone 0.5", BassStereoField::ImageMode::Wide, 1.0f, 1.0f, 0.5f, e2},
                          W{"WIDE spread 1 note 39", BassStereoField::ImageMode::Wide, 1.0f, 1.0f, 0.0f, 39},
                          W{"WIDE spread 1 note 41", BassStereoField::ImageMode::Wide, 1.0f, 1.0f, 0.0f, 41},
                          W{"WIDE spread 1 note 43", BassStereoField::ImageMode::Wide, 1.0f, 1.0f, 0.0f, 43}}) {
            VeaCell c;
            c.fs = kVea44k1;
            c.note = w.note;
            c.alg = VeaAlg::Crush;
            c.shape = 0.0f;
            c.tone = w.tone;
            c.mode = VeaMode::Lock;
            MorphState st = veaState(c);
            st.field.mode = w.mode;
            st.field.spread = w.spread;
            st.characterAmount = w.amount;
            REQUIRE(isValidMorphState(st));
            const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(c.note)));
            const std::size_t len = veaPt::aliasFftLength(c.fs, f0);
            const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * c.fs));
            const veaPt::VoiceEngineTaps t =
                veaPt::voiceEngineRender(veaScript(st, c.fs, c.note, start + len, true, 0));
            const VeaTapRead sr = veaReadTap(t.sumL, t.sumR, start, len, c.fs, f0);
            const VeaOffsetHist h = veaOffsetHist((sr.r > sr.l ? t.sumR : t.sumL).data() + start, len, c.fs, f0);
            std::cout << "PROBE G14-13(b2) 44k1 Crush [" << w.name << "]: Sum L " << sr.l << " R " << sr.r
                      << " dBFS; core " << veaReadTap(t.coreL, t.coreR, start, len, c.fs, f0).worst() << " char "
                      << veaReadTap(t.characterL, t.characterR, start, len, c.fs, f0).worst() << "; top offsets";
            for (const auto& [off, db] : h.top)
                std::cout << " " << off << "Hz:" << db;
            std::cout << "\n";
        }
    }
    // (b) Crush at fractional amounts, SOLID (the WIDE offset emulated on one channel), 44.1 vs 48 kHz, E2.
    for (const double fsHz : {kVea44k1, kVea48k}) {
        for (const float amount : {1.0f, 0.95f, 0.9f, 0.8f, 0.7f, 0.5f}) {
            VeaCell c;
            c.fs = fsHz;
            c.note = veaPt::kVoiceEngineNoteE2;
            c.alg = VeaAlg::Crush;
            c.shape = 0.0f;
            c.tone = 0.0f;
            c.mode = VeaMode::Lock;
            MorphState st = veaState(c);
            st.field.mode = BassStereoField::ImageMode::Solid;
            st.characterAmount = amount;
            REQUIRE(isValidMorphState(st));
            const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(c.note)));
            const std::size_t len = veaPt::aliasFftLength(c.fs, f0);
            const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * c.fs));
            const veaPt::VoiceEngineTaps t =
                veaPt::voiceEngineRender(veaScript(st, c.fs, c.note, start + len, true, 0));
            const VeaOffsetHist h = veaOffsetHist(t.sumL.data() + start, len, c.fs, f0);
            std::cout << "PROBE G14-13(b) " << veaFsTag(c.fs) << " E2 Crush SOLID amount " << amount << ": Sum "
                      << veaReadTap(t.sumL, t.sumR, start, len, c.fs, f0).worst() << " dBFS; top offsets";
            for (const auto& [off, db] : h.top)
                std::cout << " " << off << "Hz:" << db;
            std::cout << "\n";
        }
    }
}

/// SC-021(a) attribution (main loop, 2026-10-21). The pre-dynamics Sum read -46 .. -79 dBFS on 15 of the 18 shards
/// (first comply run and the close lane alike, identical figures); G14-4 attributed (b) only. Per worst cell: the
/// aliased reading at the core, Character, Anchor and Sum taps and the FOLLOW ratio range inside the frame; the eight
/// loudest off-lattice bins of the Sum (frequency, level, f / f0, the distance to the nearest mirrored harmonic
/// |n f0 - m fs| for m = 1..3, which is what aliasing lands on); then the same cell with one lever moved at a time.
TEST_CASE("VoiceEngine_AliasingSumProbe", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    struct P {
        double fs;
        std::uint8_t note;
        VeaAlg alg;
        float shape, tone;
        VeaMode mode;
    };
    const std::vector<P> cells{
        {kVea44k1, veaPt::kVoiceEngineNoteE0, VeaAlg::Bite, 1.0f, 1.0f, VeaMode::Lock},      // -75.89
        {kVea44k1, veaPt::kVoiceEngineNoteE2, VeaAlg::Crush, 0.0f, 0.0f, VeaMode::Lock},     // -46.56
        {kVea44k1, veaPt::kVoiceEngineNoteE0, VeaAlg::Fold, 1.0f, 1.0f, VeaMode::Lock},      // -72.11
        {kVea44k1, veaPt::kVoiceEngineNoteE0, VeaAlg::Rectify, 1.0f, 1.0f, VeaMode::Lock},   // -69.08
        {kVea44k1, veaPt::kVoiceEngineNoteE0, VeaAlg::Tube, 1.0f, 1.0f, VeaMode::Lock},      // -77.25
        {kVea48k, veaPt::kVoiceEngineNoteE1, VeaAlg::Bite, 1.0f, 1.0f, VeaMode::Lock},       // -76.12
        {kVea96k, veaPt::kVoiceEngineNoteE1, VeaAlg::Fold, 1.0f, 1.0f, VeaMode::Lock},       // -64.11
        {kVea96k, veaPt::kVoiceEngineNoteE2, VeaAlg::Tube, 1.0f, 1.0f, VeaMode::Lock},       // -70.02
        {kVea96k, veaPt::kVoiceEngineNoteE1, VeaAlg::Rectify, 0.0f, 1.0f, VeaMode::Follow},  // -7.57, ratio [1, 2]
        {kVea96k, veaPt::kVoiceEngineNoteE1, VeaAlg::Fracture, 1.0f, 1.0f, VeaMode::Follow}, // -76.11
        {kVea44k1, veaPt::kVoiceEngineNoteE2, VeaAlg::Rectify, 0.0f, 1.0f, VeaMode::Lock},    // G14-13: -78.1 on the G14-12 bits
        {kVea48k, veaPt::kVoiceEngineNoteE2, VeaAlg::Rectify, 0.0f, 1.0f, VeaMode::Follow},   // G14-13: -78.9
    };
    enum class Lever : std::uint8_t { Base, Solid, NoFeed, Lock, Shape0, CharOff, ExcOff, Mass50, FoldOff, Anchor0, Mass0, Mass0Fold0, Mass0Fold100 };
    const auto leverName = [](Lever l) {
        switch (l) {
            case Lever::Base: return "base";
            case Lever::Solid: return "SOLID";
            case Lever::NoFeed: return "anchorFoldFeed 0";
            case Lever::Lock: return "LOCK";
            case Lever::Shape0: return "SHAPE 0";
            case Lever::CharOff: return "Character amount 0";
            case Lever::ExcOff: return "excitation mix 0";
            case Lever::Mass50: return "Mass 50";
            case Lever::FoldOff: return "Fold 0";
            case Lever::Anchor0: return "Anchor level 0";
            case Lever::Mass0: return "Mass 0";
            case Lever::Mass0Fold0: return "Mass 0 + Fold 0";
            case Lever::Mass0Fold100: return "Mass 0 + Fold 100";
        }
        return "?";
    };
    for (const P& pc : cells) {
        for (const Lever lv : {Lever::Base, Lever::Solid, Lever::NoFeed, Lever::Lock, Lever::Shape0, Lever::CharOff,
                               Lever::ExcOff, Lever::Mass50, Lever::FoldOff, Lever::Anchor0, Lever::Mass0,
                               Lever::Mass0Fold0, Lever::Mass0Fold100}) {
            if (lv == Lever::Lock && pc.mode == VeaMode::Lock)
                continue;
            if (lv == Lever::Shape0 && pc.shape == 0.0f)
                continue;
            VeaCell c;
            c.fs = pc.fs;
            c.note = pc.note;
            c.alg = pc.alg;
            c.shape = pc.shape;
            c.tone = pc.tone;
            c.mode = pc.mode;
            MorphState s = veaState(c);
            switch (lv) {
                case Lever::Solid: s.field.mode = BassStereoField::ImageMode::Solid; break;
                case Lever::NoFeed: s.anchorFoldFeed = 0.0f; break;
                case Lever::Lock: s.anchor.mode = VeaMode::Lock; break;
                case Lever::Shape0: s.shape = 0.0f; break;
                case Lever::CharOff: s.characterAmount = 0.0f; break;
                case Lever::ExcOff: s.excitation.mix = 0.0f; break;
                case Lever::Mass50: s.mass = 50.0f; break;
                case Lever::FoldOff: s.foldAmount = 0.0f; break;
                case Lever::Anchor0: s.anchor.level = 0.0f; break;
                case Lever::Mass0: s.mass = 0.0f; break;
                case Lever::Mass0Fold0: s.mass = 0.0f; s.foldAmount = 0.0f; break;
                case Lever::Mass0Fold100: s.mass = 0.0f; s.foldAmount = 100.0f; break;
                case Lever::Base: break;
            }
            REQUIRE(isValidMorphState(s));
            const double f0 = static_cast<double>(midiNoteToFrequency(static_cast<int>(c.note)));
            const double fm = c.alg == VeaAlg::Fracture ? 0.5 * f0 : f0;
            const std::size_t len = veaPt::aliasFftLength(c.fs, fm);
            const auto start = static_cast<std::size_t>(std::llround(kVeaSettleS * c.fs));
            const veaPt::VoiceEngineTaps t =
                veaPt::voiceEngineRender(veaScript(s, c.fs, c.note, start + len, true, 0));
            const auto rd = [&](const std::vector<float>& L, const std::vector<float>& R) {
                if (L.size() < start + len || R.size() < start + len)
                    return 999.0;
                return veaReadTap(L, R, start, len, c.fs, fm).worst();
            };
            float fMin = 0.0f, fMax = 0.0f;
            bool any = false;
            for (const ProfundumCallRecord& r : t.calls) {
                if (r.call != ProfundumCall::FollowRatio || r.voice != 0 || r.sample < start || r.sample >= start + len)
                    continue;
                fMin = any ? std::min(fMin, r.value) : r.value;
                fMax = any ? std::max(fMax, r.value) : r.value;
                any = true;
            }
            std::cout << std::setprecision(5) << "PROBE SC-021(a) " << veaFsTag(c.fs) << " " << veaNoteName(c.note)
                      << " " << veaAlgName(c.alg) << " SHAPE " << c.shape << " Tone " << c.tone << " "
                      << veaModeName(c.mode) << " [" << leverName(lv) << "]: aliased dBFS core " << rd(t.coreL, t.coreR)
                      << " char " << rd(t.characterL, t.characterR) << " anchor " << rd(t.anchorL, t.anchorR)
                      << " sum " << rd(t.sumL, t.sumR) << " out " << rd(t.outL, t.outR) << "; follow ratio ["
                      << fMin << ", " << fMax << "]\n";
            if (t.sumL.size() >= start + len) {
                // The residual on the f0 / k lattices: content that sits exactly on n f0 / k is a period-k structure
                // (a detector cycle, a sub-harmonic), not aliasing; aliasing lands between the lattices at every k.
                std::cout << "  residual on the f0/k lattice:";
                for (int k = 1; k <= 8; ++k)
                    std::cout << " k" << k << " " << veaReadTap(t.sumL, t.sumR, start, len, c.fs, fm / k).worst();
                std::cout << " dBFS\n";
            }
            if (t.sumL.size() >= start + len) {
                // Round 3: the residual with the sidebands n f_m +- m f_h masked (m = 1..4, +-8 bins each) for
                // candidate modulation rates f_h; the minimum sits at the true rate.
                const std::vector<double> ps2L = veaPt::bh7PowerSpectrum(t.sumL.data() + start, len);
                const std::vector<double> ps2R = veaPt::bh7PowerSpectrum(t.sumR.data() + start, len);
                const double binHz2 = c.fs / static_cast<double>(len);
                const std::size_t half2 = len / 2;
                std::cout << "  residual with n f_m +- m f_h masked (m <= 4):";
                for (const double fh : {7.0, 7.5, 8.0, 8.5, 9.0}) {
                    std::vector<char> ex(half2 + 1, 0);
                    for (std::size_t k = 0; k <= std::min<std::size_t>(8, half2); ++k)
                        ex[k] = 1;
                    const auto mask = [&](double centreHz) {
                        if (centreHz <= 0.0 || centreHz >= c.fs * 0.5)
                            return;
                        const double centre = centreHz / binHz2;
                        const double lo = std::max(0.0, std::ceil(centre - 8.0));
                        const double hi = std::min(static_cast<double>(half2), std::floor(centre + 8.0));
                        for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
                            ex[k] = 1;
                    };
                    for (int n = 1; static_cast<double>(n) * fm < c.fs * 0.5; ++n) {
                        mask(static_cast<double>(n) * fm);
                        for (int m = 1; m <= 4; ++m) {
                            mask(static_cast<double>(n) * fm + static_cast<double>(m) * fh);
                            mask(static_cast<double>(n) * fm - static_cast<double>(m) * fh);
                        }
                    }
                    double aL = 0.0, aR = 0.0;
                    for (std::size_t k = 1; k <= half2; ++k)
                        if (ex[k] == 0) {
                            aL += ps2L[k];
                            aR += ps2R[k];
                        }
                    std::cout << " f_h " << fh << ": " << std::max(veaPt::safeDb10(aL / 0.5), veaPt::safeDb10(aR / 0.5));
                }
                std::cout << " dBFS\n";
            }
            // Round 5: the same histogram at the core and Character taps (the stage where the 8 Hz family appears).
            const auto offsetHist = [&](const std::vector<float>& L, const std::vector<float>& R, const char* tag) {
                if (L.size() < start + len || R.size() < start + len)
                    return;
                const VeaTapRead sr = veaReadTap(L, R, start, len, c.fs, fm);
                const std::vector<float>& xh = sr.r > sr.l ? R : L;
                const std::vector<double> ph = veaPt::bh7PowerSpectrum(xh.data() + start, len);
                const double binHzH = c.fs / static_cast<double>(len);
                const std::size_t halfH = len / 2;
                std::vector<char> exh(halfH + 1, 0);
                for (std::size_t k = 0; k <= std::min<std::size_t>(8, halfH); ++k)
                    exh[k] = 1;
                for (int n = 1; static_cast<double>(n) * fm < c.fs * 0.5; ++n) {
                    const double centre = static_cast<double>(n) * fm / binHzH;
                    const double lo = std::max(0.0, std::ceil(centre - 8.0));
                    const double hi = std::min(static_cast<double>(halfH), std::floor(centre + 8.0));
                    for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
                        exh[k] = 1;
                }
                constexpr double kBucketHz = 0.25;
                std::vector<double> hist(static_cast<std::size_t>(0.5 * fm / kBucketHz) + 2, 0.0);
                double totalOff = 0.0;
                for (std::size_t k = 1; k <= halfH; ++k) {
                    if (exh[k] != 0)
                        continue;
                    const double f = static_cast<double>(k) * binHzH;
                    double off = std::fmod(f, fm);
                    if (off > 0.5 * fm)
                        off = fm - off;
                    hist[std::min(hist.size() - 1, static_cast<std::size_t>(off / kBucketHz))] += ph[k];
                    totalOff += ph[k];
                }
                std::vector<std::pair<double, std::size_t>> top;
                for (std::size_t b = 0; b < hist.size(); ++b)
                    top.emplace_back(hist[b], b);
                std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                double family = 0.0;
                for (std::size_t b = 0; b < hist.size(); ++b) {
                    const double o = (static_cast<double>(b) + 0.5) * kBucketHz;
                    for (const double h : {8.0, 16.0, 24.0, 32.0})
                        if (std::abs(o - h) <= 0.75) {
                            family += hist[b];
                            break;
                        }
                }
                std::cout << "  " << tag << " tap " << sr.worst() << " dBFS: top";
                for (std::size_t i = 0; i < std::min<std::size_t>(4, top.size()); ++i)
                    std::cout << " " << (static_cast<double>(top[i].second) + 0.5) * kBucketHz << "Hz:"
                              << veaPt::safeDb10(top[i].first / 0.5);
                std::cout << "; 8/16/24/32 Hz family " << veaPt::safeDb10(family / 0.5) << ", rest "
                          << veaPt::safeDb10(std::max(totalOff - family, 0.0) / 0.5) << " dBFS\n";
            };
            offsetHist(t.coreL, t.coreR, "core");
            offsetHist(t.characterL, t.characterR, "char");
            if (t.sumL.size() >= start + len) {
                // Round 4: offset histogram of the inter-harmonic power (the louder channel, the aliasedPower mask).
                const VeaTapRead sr = veaReadTap(t.sumL, t.sumR, start, len, c.fs, fm);
                const std::vector<float>& xh = sr.r > sr.l ? t.sumR : t.sumL;
                const std::vector<double> ph = veaPt::bh7PowerSpectrum(xh.data() + start, len);
                const double binHzH = c.fs / static_cast<double>(len);
                const std::size_t halfH = len / 2;
                std::vector<char> exh(halfH + 1, 0);
                for (std::size_t k = 0; k <= std::min<std::size_t>(8, halfH); ++k)
                    exh[k] = 1;
                for (int n = 1; static_cast<double>(n) * fm < c.fs * 0.5; ++n) {
                    const double centre = static_cast<double>(n) * fm / binHzH;
                    const double lo = std::max(0.0, std::ceil(centre - 8.0));
                    const double hi = std::min(static_cast<double>(halfH), std::floor(centre + 8.0));
                    for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
                        exh[k] = 1;
                }
                constexpr double kBucketHz = 0.25;
                std::vector<double> hist(static_cast<std::size_t>(0.5 * fm / kBucketHz) + 2, 0.0);
                double totalOff = 0.0;
                for (std::size_t k = 1; k <= halfH; ++k) {
                    if (exh[k] != 0)
                        continue;
                    const double f = static_cast<double>(k) * binHzH;
                    double off = std::fmod(f, fm);
                    if (off > 0.5 * fm)
                        off = fm - off;
                    hist[std::min(hist.size() - 1, static_cast<std::size_t>(off / kBucketHz))] += ph[k];
                    totalOff += ph[k];
                }
                std::vector<std::pair<double, std::size_t>> top;
                for (std::size_t b = 0; b < hist.size(); ++b)
                    top.emplace_back(hist[b], b);
                std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                double family = 0.0;   // within +-0.75 Hz of 8, 16, 24, 32 Hz
                for (std::size_t b = 0; b < hist.size(); ++b) {
                    const double o = (static_cast<double>(b) + 0.5) * kBucketHz;
                    for (const double h : {8.0, 16.0, 24.0, 32.0})
                        if (std::abs(o - h) <= 0.75) {
                            family += hist[b];
                            break;
                        }
                }
                std::cout << "  offset histogram (0.25 Hz buckets, offset to the nearest harmonic): top";
                for (std::size_t i = 0; i < std::min<std::size_t>(6, top.size()); ++i)
                    std::cout << " " << (static_cast<double>(top[i].second) + 0.5) * kBucketHz << "Hz:"
                              << veaPt::safeDb10(top[i].first / 0.5);
                std::cout << " dBFS; 8/16/24/32 Hz family " << veaPt::safeDb10(family / 0.5) << " of "
                          << veaPt::safeDb10(totalOff / 0.5) << " dBFS, rest "
                          << veaPt::safeDb10(std::max(totalOff - family, 0.0) / 0.5) << " dBFS\n";
            }
            if (lv != Lever::Base || t.sumL.size() < start + len)
                continue;
            // The eight loudest off-lattice bins of the Sum (the louder channel), with the aliasedPower mask.
            const bool useR = veaReadTap(t.sumL, t.sumR, start, len, c.fs, fm).r >
                              veaReadTap(t.sumL, t.sumR, start, len, c.fs, fm).l;
            const std::vector<float>& x = useR ? t.sumR : t.sumL;
            const std::vector<double> ps = veaPt::bh7PowerSpectrum(x.data() + start, len);
            const double binHz = c.fs / static_cast<double>(len);
            const std::size_t half = len / 2;
            std::vector<char> excluded(half + 1, 0);
            for (std::size_t k = 0; k <= std::min<std::size_t>(8, half); ++k)
                excluded[k] = 1;
            for (int n = 1; static_cast<double>(n) * fm < c.fs * 0.5; ++n) {
                const double centre = static_cast<double>(n) * fm / binHz;
                const double lo = std::max(0.0, std::ceil(centre - 8.0));
                const double hi = std::min(static_cast<double>(half), std::floor(centre + 8.0));
                for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
                    excluded[k] = 1;
            }
            std::vector<std::pair<double, std::size_t>> bins;
            double below = 0.0, mid = 0.0, high = 0.0;
            for (std::size_t k = 1; k <= half; ++k) {
                if (excluded[k] != 0)
                    continue;
                const double f = static_cast<double>(k) * binHz;
                if (f < fm)
                    below += ps[k];
                else if (f < 0.25 * c.fs)
                    mid += ps[k];
                else
                    high += ps[k];
                bins.emplace_back(ps[k], k);
            }
            std::sort(bins.begin(), bins.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            std::cout << "  off-lattice power: below f_m " << veaPt::safeDb10(below / 0.5) << " dBFS, f_m .. fs/4 "
                      << veaPt::safeDb10(mid / 0.5) << " dBFS, above fs/4 " << veaPt::safeDb10(high / 0.5)
                      << " dBFS (" << (useR ? "R" : "L") << ")\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(8, bins.size()); ++i) {
                const double f = static_cast<double>(bins[i].second) * binHz;
                double mirror = 1e9;
                for (int m = 1; m <= 3; ++m) {
                    const double n1 = std::round((static_cast<double>(m) * c.fs - f) / f0);
                    const double n2 = std::round((static_cast<double>(m) * c.fs + f) / f0);
                    mirror = std::min({mirror, std::abs(std::abs(n1 * f0 - static_cast<double>(m) * c.fs) - f),
                                       std::abs(std::abs(n2 * f0 - static_cast<double>(m) * c.fs) - f)});
                }
                std::cout << "  bin " << bins[i].second << " f " << f << " Hz (" << f / f0 << " f0) "
                          << veaPt::safeDb10(bins[i].first / 0.5) << " dBFS, nearest mirrored harmonic "
                          << mirror << " Hz away\n";
            }
        }
    }
}
