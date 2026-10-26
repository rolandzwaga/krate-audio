// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_chain_test.cpp
// ==============================================================================
// Chain criteria (tasks.md T017; plan S9.10, S10.6; C-1, C-2; FR-029, FR-081, FR-002, FR-007):
//   VoiceEngine_MonoSum_{Solid,Spread,Wide}     SC-015 / SC-016 [long]; VoiceEngine_MonoSumSmoke (SOLID, MONO, A_s, E1)
//   VoiceEngine_AnchorThroughChain_{E0,E1,E2}   SC-017 [long]; VoiceEngine_AnchorThroughChainSmoke (E1, Rectify, Sine)
//   VoiceEngine_FollowTracking                  SC-018 [long]; VoiceEngine_FollowTrackingSmoke (step, Fracture, E1)
// Every [long] case has a non-long smoke sibling with the same assertions and bars (tasks.md deviation 5).
// Renders go through the T009 driver / observer (profundum_voice_engine_test_helpers.h); SC-018 drives the engine
// directly with the same observer, because its Amount sweep changes the endpoints at every grid point.
// TU-local names carry the `pch` / `Pch` / `kPch` prefix inside an anonymous namespace. Decisions:
// specs/profundum-phase9-voice-engine/evidence/run-log/T017.txt.
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
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

using namespace Krate::DSP;

namespace {

namespace pchPt = Krate::DSP::ProfundumTest;
namespace pchSft = Krate::DSP::StereoFieldTest;
namespace pchBmt = Krate::DSP::BassMorphTest;

using PchKind = pchPt::VoiceEngineEventKind;
using PchMode = BassStereoField::ImageMode;
using PchAlg = BassCharacterStage::Algorithm;
using PchWave = AnchorOscillator::Waveform;

constexpr double kPchFs = 48000.0;
constexpr double kPchPi = 3.14159265358979323846;
constexpr std::size_t kPchGrid = ProfundumEngine::kControlInterval;
constexpr std::uint8_t kPchVelocity = 100;

constexpr std::array<const char*, BassCharacterStage::kNumAlgorithms> kPchAlgNames{"Tube",  "Fold",  "Rectify",
                                                                                   "Bite", "Crush", "Fracture"};

[[nodiscard]] const char* pchAlgName(PchAlg a) noexcept { return kPchAlgNames[static_cast<std::size_t>(a)]; }

[[nodiscard]] const char* pchWaveName(PchWave w) noexcept {
    switch (w) {
        case PchWave::Sine: return "Sine";
        case PchWave::Triangle: return "Triangle";
        case PchWave::RoundedSine: return "RoundedSine";
        case PchWave::AsymmetricSine: return "AsymmetricSine";
        case PchWave::OctaveDownSine: return "OctaveDownSine";
    }
    return "?";
}

[[nodiscard]] const char* pchModeName(PchMode m) noexcept {
    switch (m) {
        case PchMode::Solid: return "SOLID";
        case PchMode::Spread: return "SPREAD";
        case PchMode::Wide: return "WIDE";
    }
    return "?";
}

[[nodiscard]] std::string pchNoteName(std::uint8_t n) {
    switch (n) {
        case pchPt::kVoiceEngineNoteE0: return "E0";
        case pchPt::kVoiceEngineNoteE1: return "E1";
        case pchPt::kVoiceEngineNoteE2: return "E2";
        case pchPt::kVoiceEngineNoteC3: return "C3";
        default: break;
    }
    return "n" + std::to_string(static_cast<int>(n));
}

[[nodiscard]] float pchNoteHz(int note) noexcept { return midiNoteToFrequency(note); }

/// Bit test (this TU builds with the shipping FP mode; std::isfinite may be folded away under -ffast-math).
[[nodiscard]] bool pchFinite(double x) noexcept {
    return (std::bit_cast<std::uint64_t>(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

[[nodiscard]] bool pchSame(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// A grid-aligned sample index (events land on grid points, so latched and applied coincide, C-9).
[[nodiscard]] std::uint64_t pchAt(double seconds) noexcept {
    const auto n = static_cast<std::uint64_t>(std::llround(seconds * kPchFs));
    return (n / kPchGrid) * kPchGrid;
}

[[nodiscard]] std::string pchEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

void pchWriteEvidence(const std::string& relPath, const std::string& text) {
    const std::filesystem::path p = std::filesystem::path(pchEvidenceDir()) / relPath;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << text;
    CHECK(out.good());
}

/// Plan S1 C-8: showcase A with ampEnv.sustain = 1 (A_s).
[[nodiscard]] MorphState pchStateAs() { return pchPt::voiceEngineAs(pchBmt::showcasePair().first); }

[[nodiscard]] MorphState pchStateB() { return pchBmt::showcasePair().second; }

/// Complex h1 of the mid over [start, start + n) at f (single-bin DFT, amplitude-scaled).
[[nodiscard]] std::complex<double> pchH1(const std::vector<float>& L, const std::vector<float>& R, std::size_t start,
                                         std::size_t n, double f) {
    REQUIRE(start + n <= L.size());
    REQUIRE(start + n <= R.size());
    std::complex<double> acc(0.0, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        const double x = 0.5 * (static_cast<double>(L[start + k]) + static_cast<double>(R[start + k]));
        const double ph = 2.0 * kPchPi * f * static_cast<double>(k) / kPchFs;
        acc += x * std::complex<double>(std::cos(ph), -std::sin(ph));
    }
    return acc * (2.0 / static_cast<double>(n));
}

[[nodiscard]] double pchDb20(double x) noexcept { return 20.0 * std::log10(std::max(x, 1e-300)); }

// =============================================================================
// SC-015 / SC-016: the full-engine mono sum (FR-081, FR-029; C-1, C-2; H-18, H-19, H-20)
// =============================================================================
// Cells: image mode x MONO / 4 voices x A_s / B x E1 / C3. SPREAD and WIDE cells run at spread 1 (the base's own
// spread is 0 on A_s, which would make the image mode a no-op). SOLID cells start in WIDE (spread 1) and switch both
// endpoints to SOLID at 0.5 s, so every SOLID cell exercises the FR-029 re-sync (C-2). 4 voices = root + {0, 5, 7,
// 10} st. Read window: [2 s, 2 s + 2^17) (2 s settle). Q_sub / rho on [0, f_c) by StereoFieldTest::bandReading,
// gated at the field tap and the output tap (the full engine) under Phase 7's G10-2 rule (a cell whose lowest f0
// is >= f_c has no harmonic in the sub band: recorded) and validity rule (a band below -60 dBFS is not read);
// no anti-phase exemption (H-19 is gated through SC-015). The core, Character, Sum and Dynamics taps are recorded.

constexpr double kPchMsSettleS = 2.0;
constexpr std::size_t kPchMsWindow = std::size_t{1} << 17;
constexpr double kPchMsSwitchS = 0.5;
constexpr std::array<int, 4> kPchMsPolyOffsetsSt{0, 5, 7, 10};

enum class PchBase : std::uint8_t { As, B };

struct PchMsCell {
    PchMode mode = PchMode::Solid;
    ProfundumEngine::VoiceMode voices = ProfundumEngine::VoiceMode::Mono;
    PchBase base = PchBase::As;
    std::uint8_t note = pchPt::kVoiceEngineNoteE1;
};

[[nodiscard]] std::string pchMsLabel(const PchMsCell& c) {
    std::ostringstream os;
    os << pchModeName(c.mode) << ',' << (c.voices == ProfundumEngine::VoiceMode::Mono ? "MONO" : "POLY4") << ','
       << (c.base == PchBase::As ? "A_s" : "B") << ',' << pchNoteName(c.note);
    return os.str();
}

/// The cell's first state: the base with the image mode set (SOLID cells start in WIDE), spread 1 off SOLID.
[[nodiscard]] MorphState pchMsFirstState(const PchMsCell& c) {
    MorphState s = c.base == PchBase::As ? pchStateAs() : pchStateB();
    s.field.mode = c.mode == PchMode::Solid ? PchMode::Wide : c.mode;
    s.field.spread = 1.0f;
    REQUIRE(isValidMorphState(s));
    return s;
}

[[nodiscard]] pchPt::VoiceEngineTaps pchMsRender(const PchMsCell& c) {
    pchPt::VoiceEngineScript sc;
    sc.fs = kPchFs;
    sc.hostBlock = 64;
    sc.totalSamples = pchAt(kPchMsSettleS) + kPchMsWindow + kPchGrid;
    const MorphState first = pchMsFirstState(c);
    sc.a = first;
    sc.b = first;
    sc.mode = c.voices;
    sc.polyphony = kPchMsPolyOffsetsSt.size();
    const bool poly = c.voices == ProfundumEngine::VoiceMode::Poly;
    sc.tappedVoices = poly ? kPchMsPolyOffsetsSt.size() : 1;
    if (poly) {
        for (const int off : kPchMsPolyOffsetsSt) {
            sc.events.push_back(pchPt::VoiceEngineEvent{
                .sample = 0, .kind = PchKind::NoteOn, .note = static_cast<std::uint8_t>(c.note + off)});
        }
    } else {
        sc.events.push_back(pchPt::VoiceEngineEvent{.sample = 0, .kind = PchKind::NoteOn, .note = c.note});
    }
    if (c.mode == PchMode::Solid) {
        MorphState solid = first;
        solid.field.mode = PchMode::Solid;
        REQUIRE(isValidMorphState(solid));
        sc.endpointStates = {solid, solid};
        const std::uint64_t sw = pchAt(kPchMsSwitchS);
        sc.events.push_back(pchPt::VoiceEngineEvent{.sample = sw, .kind = PchKind::Endpoint, .index = 0});
        sc.events.push_back(pchPt::VoiceEngineEvent{.sample = sw, .kind = PchKind::Endpoint, .index = 1});
    }
    return pchPt::voiceEngineRender(sc);
}

[[nodiscard]] std::size_t pchCountDiffering(const std::vector<float>& L, const std::vector<float>& R, std::size_t from,
                                            std::size_t to) {
    std::size_t n = 0;
    for (std::size_t i = from; i < to && i < L.size() && i < R.size(); ++i) {
        if (!pchSame(L[i], R[i]))
            ++n;
    }
    return n;
}

/// H-20: the output's side power (S = (L - R) / 2) in [lo, fs / 2) over the read window, dBFS (mean square).
[[nodiscard]] double pchSideResidueDb(const std::vector<float>& L, const std::vector<float>& R, std::size_t start,
                                      double lo) {
    std::vector<float> side(kPchMsWindow, 0.0f);
    for (std::size_t i = 0; i < kPchMsWindow; ++i)
        side[i] = 0.5f * (L[start + i] - R[start + i]);
    const pchSft::BandReading r =
        pchSft::bandReading(side.data(), side.data(), kPchMsWindow, kPchFs, lo, 0.5 * kPchFs);
    return pchPt::safeDb10(r.meanEnergy);
}

struct PchMsTap {
    const char* name;
    const std::vector<float>* L;
    const std::vector<float>* R;
    bool gated;
};

/// One cell: SC-016 (Anchor tap L == R bitwise), the SOLID re-sync (C-2), the sub-band bars and the H-20 record.
/// Returns pass; every gate is a CHECK so the CSV holds every cell.
bool pchMonoSumCell(const PchMsCell& c, std::ostringstream& csv) {
    const std::string label = pchMsLabel(c);
    INFO("cell " << label);
    const pchPt::VoiceEngineTaps t = pchMsRender(c);
    REQUIRE(t.droppedEvents == 0);
    REQUIRE(!t.resolvedPerInterval.empty());
    const MorphState& landed = t.resolvedPerInterval.back().state;
    REQUIRE(landed.field.mode == c.mode);
    const std::size_t total = t.total;
    const auto start = static_cast<std::size_t>(pchAt(kPchMsSettleS));
    REQUIRE(start + kPchMsWindow <= total);

    // SC-016: the Anchor tap, summed over every voice and per tapped voice, bitwise L == R in every chunk. The
    // Anchor must be sounding, so the identity is not read on silence.
    double anchorPeak = 0.0;
    for (const float x : t.anchorL)
        anchorPeak = std::max(anchorPeak, std::abs(static_cast<double>(x)));
    REQUIRE(anchorPeak > 1e-4);
    bool anchorEqual = pchSft::bitEqual(t.anchorL, t.anchorR);
    for (std::size_t v = 0; v < t.tappedVoices; ++v)
        anchorEqual = anchorEqual && pchSft::bitEqual(t.voices[v].anchorL, t.voices[v].anchorR);
    CHECK(anchorEqual);

    // FR-029 / C-2: one DcResync per SOLID entry; output L == R bitwise from that grid point to the end.
    std::size_t resyncBefore = 0;
    std::size_t resyncAfter = 0;
    std::uint64_t resyncAt = 0;
    const std::uint64_t sw = pchAt(kPchMsSwitchS);
    for (const ProfundumCallRecord& r : t.calls) {
        if (r.call != ProfundumCall::DcResync)
            continue;
        if (c.mode == PchMode::Solid && r.sample >= sw) {
            if (resyncAfter == 0)
                resyncAt = r.sample;
            ++resyncAfter;
        } else {
            ++resyncBefore;
        }
    }
    bool solidOk = true;
    std::size_t preResyncDiff = 0;
    std::size_t leadInDiff = 0;
    if (c.mode == PchMode::Solid) {
        CHECK(resyncBefore == 0);   // the cell starts in WIDE
        REQUIRE(resyncAfter == 1);  // C-2: the copy runs once per SOLID entry
        // G14-1 (2026-10-21): the limiter's look-ahead delay line (kLimiterLookaheadSamples, the FR-025 latency)
        // still holds the last pre-copy L != R samples at the DC-blocker copy; the identity holds from the copy plus
        // the latency (FR-029 as amended; the smoke read exactly 64 differing samples after the copy).
        const std::size_t latency = Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples;
        const auto from = static_cast<std::size_t>(resyncAt) + latency;
        REQUIRE(from < start);      // the crossfade has ended before the read window
        solidOk = std::memcmp(t.outL.data() + from, t.outR.data() + from, (total - from) * sizeof(float)) == 0;
        INFO("DcResync at sample " << resyncAt << " + latency " << latency << " (switch at " << sw
                                   << "), L != R samples after it: " << pchCountDiffering(t.outL, t.outR, from, total));
        CHECK(solidOk);
        preResyncDiff = pchCountDiffering(t.outL, t.outR, static_cast<std::size_t>(sw), from);
        leadInDiff = pchCountDiffering(t.outL, t.outR, 0, static_cast<std::size_t>(sw));
    } else {
        CHECK(resyncAfter + resyncBefore == 0);   // never SOLID
    }

    // SC-015: the sub band [0, f_c).
    const double fc = static_cast<double>(landed.field.crossoverHz);
    const double lowest = static_cast<double>(pchNoteHz(c.note));
    const bool subInBand = lowest < fc;   // Phase 7 G10-2
    const std::array<PchMsTap, 6> taps{{{"core", &t.coreL, &t.coreR, false},
                                        {"character", &t.characterL, &t.characterR, false},
                                        {"sum", &t.sumL, &t.sumR, false},
                                        {"dyn", &t.dynL, &t.dynR, false},
                                        {"field", &t.stageOutL, &t.stageOutR, true},
                                        {"out", &t.outL, &t.outR, true}}};
    const int topNote =
        c.note + (c.voices == ProfundumEngine::VoiceMode::Poly ? kPchMsPolyOffsetsSt.back() : 0);
    const double h20Lo = static_cast<double>(pchNoteHz(topNote)) * static_cast<double>(ProfundumVoice::kNumPartials);
    const bool h20Valid = h20Lo < 0.5 * kPchFs;
    const double h20Db = h20Valid ? pchSideResidueDb(t.outL, t.outR, start, h20Lo) : 0.0;
    const char* solidCol = "na";
    if (c.mode == PchMode::Solid)
        solidCol = solidOk ? "1" : "0";
    bool bandsOk = true;
    for (const PchMsTap& tap : taps) {
        REQUIRE(tap.L->size() == total);
        REQUIRE(tap.R->size() == total);
        const pchSft::BandReading r =
            pchSft::bandReading(tap.L->data() + start, tap.R->data() + start, kPchMsWindow, kPchFs, 0.0, fc);
        const bool gated = tap.gated && subInBand && r.valid;
        const bool pass = !gated || (std::abs(r.q) <= pchSft::kSubBandQBarDb && r.rho >= pchSft::kSubBandRhoBar);
        if (gated) {
            INFO(tap.name << " Q_sub " << r.q << " dB (bar |Q| <= " << pchSft::kSubBandQBarDb << "), rho " << r.rho
                          << " (bar >= " << pchSft::kSubBandRhoBar << "), f_c " << fc << " Hz");
            CHECK(pass);
        }
        bandsOk = bandsOk && pass;
        csv << label << ',' << tap.name << ',' << fc << ',' << (subInBand ? 1 : 0) << ',' << (r.valid ? 1 : 0) << ','
            << r.q << ',' << r.rho << ',' << r.sideToMidDb << ',' << pchPt::safeDb10(r.meanEnergy) << ','
            << (gated ? 1 : 0) << ',' << (pass ? 1 : 0) << ',' << (anchorEqual ? 1 : 0) << ','
            << (c.mode == PchMode::Solid ? std::to_string(resyncAt) : std::string("na")) << ','
            << solidCol << ',' << preResyncDiff << ','
            << leadInDiff << ',';
        if (h20Valid)
            csv << h20Lo << ',' << h20Db;
        else
            csv << "na,na";
        csv << '\n';
    }
    return anchorEqual && solidOk && bandsOk;
}

[[nodiscard]] std::string pchMsCsvHeader() {
    return "mode,voices,base,note,tap,fc_hz,sub_in_band,sub_valid,q_sub_db,rho_sub,s2m_sub_db,sub_mean_dbfs,gated,pass,"
           "anchor_lr_bitwise,dc_resync_sample,solid_out_lr_bitwise_from_resync,lr_diff_samples_switch_to_resync,"
           "lr_diff_samples_wide_lead_in,h20_band_lo_hz,h20_out_side_residue_dbfs\n";
}

void pchMonoSumRun(const std::vector<PchMsCell>& cells, const std::string& file) {
    enableFTZDAZ();
    std::ostringstream csv;
    csv << std::setprecision(6) << pchMsCsvHeader();
    std::size_t failures = 0;
    for (const PchMsCell& c : cells) {
        if (!pchMonoSumCell(c, csv))
            ++failures;
    }
    std::cout << "SC-015 " << file << ": " << cells.size() << " cells, " << failures << " failing\n";
    pchWriteEvidence(file, csv.str());
    CHECK(failures == 0);
}

[[nodiscard]] std::vector<PchMsCell> pchMsGrid(PchMode mode) {
    std::vector<PchMsCell> cells;
    for (const ProfundumEngine::VoiceMode v : {ProfundumEngine::VoiceMode::Mono, ProfundumEngine::VoiceMode::Poly}) {
        for (const PchBase b : {PchBase::As, PchBase::B}) {
            for (const std::uint8_t n : {pchPt::kVoiceEngineNoteE1, pchPt::kVoiceEngineNoteC3})
                cells.push_back(PchMsCell{.mode = mode, .voices = v, .base = b, .note = n});
        }
    }
    return cells;
}

// =============================================================================
// SC-017: the Anchor through max Fold + Character (H-3; FR-002; plan S9.10)
// =============================================================================
// Base A_s (SOLID, kSineAnchor, Drop 0, MONO, one held note), Anchor level 1, LOCK, feed 0. S: Fold 100, Amount 1,
// excitation mix / drive / asymmetry 1. R: Fold 0, Amount 0, excitation mix 0 (same algorithm, waveform and note).
// Window: whole periods of f0 (f_core of R at the window start), max(8, floor(0.45 f0)) periods from 0.75 s; the
// same window on every tap of both renders. C = Character tap, A = Anchor tap (voice 0), Sigma = Sum tap.

constexpr double kPchAcRenderS = 1.25;
constexpr double kPchAcWindowFromS = 0.75;
constexpr double kPchAcPeriodsPerS = 0.45;
constexpr double kPchAcMinPeriods = 8.0;
constexpr double kPchAcBarADb = 1.0;            // (a)
constexpr double kPchAcBarBDb = 4.1;            // (b)
constexpr double kPchAcBarCDb = 0.1;            // (c)
constexpr double kPchAcBarFloorDb = 2.0;        // (b) floor, h1-removing algorithms: E_sub(S) >= E_sub(A_S) - 2 dB (G11-3)
constexpr double kPchAcPremiseAnchorDb = 0.01;  // the Anchor equal in S and R
// G11-3 (user ruling 2026-10-20): the drafted R-coherence premise (|C_R| / |A_R| within 0.05 dB, arg within 2 deg)
// cannot hold on any Phase 9 chain: ShapeMorph at SHAPE 0 rotates h1 by -9.9 deg on every algorithm (Phase 5 FR-040),
// so rho_R / theta_R are recorded and (c) uses the two-chain form
// 20 log10(|1 + rho_S e^{j theta_S}| / |1 + rho_R e^{j theta_R}|) (the -9.9 deg moves it by 0.008 dB).

/// (b)'s 4.1 dB bar applies to the algorithms whose Amount-1 output keeps the core's h1. Rectify, Crush and Fracture
/// remove or invert it by definition (the sum then falls by >= 6.02 dB while the Anchor's own w1 is unchanged), so
/// their (b) reading is recorded under the floor gate E_sub(S) >= E_sub(A_S) - kPchAcBarFloorDb (G11-3).
[[nodiscard]] constexpr bool pchAcKeepsH1(PchAlg alg) noexcept {
    return alg == PchAlg::Tube || alg == PchAlg::Fold || alg == PchAlg::Bite;
}
constexpr double kPchAcGainSteadyTol = 1e-6;
constexpr std::array<PchWave, 5> kPchAcWaves{PchWave::Sine, PchWave::Triangle, PchWave::RoundedSine,
                                             PchWave::AsymmetricSine, PchWave::OctaveDownSine};

[[nodiscard]] MorphState pchAcState(PchAlg alg, PchWave wave, bool stressed) {
    MorphState s = pchStateAs();
    s.characterAlgorithm = alg;
    s.anchor.waveform = wave;
    s.anchor.mode = AnchorOscillator::Mode::Lock;
    s.anchor.level = 1.0f;
    s.anchorFoldFeed = 0.0f;
    if (stressed) {
        s.foldAmount = 100.0f;
        s.characterAmount = 1.0f;
        s.excitation.mix = 1.0f;
        s.excitation.drive = 1.0f;
        s.excitation.asymmetry = 1.0f;
    } else {
        s.foldAmount = 0.0f;
        s.characterAmount = 0.0f;
        s.excitation.mix = 0.0f;
    }
    REQUIRE(isValidMorphState(s));
    return s;
}

[[nodiscard]] pchPt::VoiceEngineTaps pchAcRender(const MorphState& s, std::uint8_t note) {
    pchPt::VoiceEngineScript sc;
    sc.fs = kPchFs;
    sc.hostBlock = 64;
    sc.totalSamples = pchAt(kPchAcRenderS);
    sc.a = s;
    sc.b = s;
    sc.mode = ProfundumEngine::VoiceMode::Mono;
    sc.tappedVoices = 1;
    sc.events.push_back(pchPt::VoiceEngineEvent{.sample = 0, .kind = PchKind::NoteOn, .note = note});
    return pchPt::voiceEngineRender(sc);
}

struct PchAcWindow {
    std::size_t start = 0;
    std::size_t n = 0;
    double f0 = 0.0;
};

[[nodiscard]] PchAcWindow pchAcWindow(const pchPt::VoiceEngineTaps& r) {
    PchAcWindow w;
    w.start = static_cast<std::size_t>(pchAt(kPchAcWindowFromS));
    REQUIRE(w.start < r.voices[0].fCore.size());
    w.f0 = static_cast<double>(r.voices[0].fCore[w.start]);
    REQUIRE(w.f0 > 0.0);
    const double periods = std::max(kPchAcMinPeriods, std::floor(kPchAcPeriodsPerS * w.f0));
    w.n = static_cast<std::size_t>(std::llround(periods * kPchFs / w.f0));
    REQUIRE(w.start + w.n <= r.total);
    return w;
}

struct PchAcTaps {
    std::complex<double> a, c, sum, aHalf;
    bool gainsSteady = true;
    double fDev = 0.0;   // max |f_core / f0 - 1| over the window
};

[[nodiscard]] PchAcTaps pchAcRead(const pchPt::VoiceEngineTaps& t, const PchAcWindow& w) {
    const pchPt::VoiceEngineVoiceTapSet& v = t.voices[0];
    REQUIRE(!v.chunks.empty());
    PchAcTaps out;
    out.a = pchH1(v.anchorL, v.anchorR, w.start, w.n, w.f0);
    out.c = pchH1(v.charL, v.charR, w.start, w.n, w.f0);
    out.sum = pchH1(t.sumL, t.sumR, w.start, w.n, w.f0);
    out.aHalf = pchH1(v.anchorL, v.anchorR, w.start, w.n, 0.5 * w.f0);
    const float g0 = v.gAmp[w.start];
    for (std::size_t k = w.start; k < w.start + w.n; ++k) {
        const double dg = std::abs(static_cast<double>(v.gAmp[k]) - static_cast<double>(g0));
        if (!pchSame(v.gD[k], 1.0f) || dg > kPchAcGainSteadyTol)
            out.gainsSteady = false;
        out.fDev = std::max(out.fDev, std::abs(static_cast<double>(v.fCore[k]) / w.f0 - 1.0));
    }
    return out;
}

[[nodiscard]] std::string pchAcCsvHeader() {
    return "note,algorithm,waveform,gated,f0_hz,window_samples,fold_target_s,rho,theta_deg,rho_r_db,theta_r_deg,"
           "anchor_s_vs_r_db,e_sub_s_db,e_sub_r_db,de_meas_db,de_pred_db,c_err_db,tapsum_residual_s,tapsum_residual_r,"
           "gains_steady,f_dev,anchor_half_f0_s_db,premises_ok,b_gate,e_sub_s_minus_anchor_db,pass_a,pass_b,pass_c\n";
}

/// One SC-017 cell (S and R renders). Gates are CHECKs, so the CSV holds every cell. Returns pass.
bool pchAcCell(std::uint8_t note, PchAlg alg, PchWave wave, std::ostringstream& csv) {
    const bool gatedRow = wave != PchWave::OctaveDownSine;   // f0 / 2 carries no core content: recorded
    INFO(pchNoteName(note) << ' ' << pchAlgName(alg) << ' ' << pchWaveName(wave));
    const pchPt::VoiceEngineTaps tr = pchAcRender(pchAcState(alg, wave, false), note);
    const pchPt::VoiceEngineTaps ts = pchAcRender(pchAcState(alg, wave, true), note);
    REQUIRE(tr.droppedEvents == 0);
    REQUIRE(ts.droppedEvents == 0);
    const PchAcWindow w = pchAcWindow(tr);
    REQUIRE(ts.total == tr.total);
    const PchAcTaps r = pchAcRead(tr, w);
    const PchAcTaps s = pchAcRead(ts, w);
    const float foldTarget = ts.grid.empty() ? -1.0f : ts.grid.back().targets.foldAmount;

    const double absAs = std::abs(s.a);
    const double absAr = std::abs(r.a);
    REQUIRE(absAr > 0.0);
    REQUIRE(absAs > 0.0);
    const double rho = std::abs(s.c) / absAs;
    const double theta = std::arg(s.c / s.a);
    const double rhoRDb = pchDb20(std::abs(r.c) / absAr);
    const double thetaR = std::arg(r.c / r.a);   // (-pi, pi]: wrapped
    const double anchorDb = pchDb20(absAs / absAr);
    const double eSubS = pchDb20(std::abs(s.sum));
    const double eSubR = pchDb20(std::abs(r.sum));
    const double deMeas = eSubS - eSubR;
    const double rhoR = std::abs(r.c) / absAr;
    const double dePred = pchDb20(std::abs(std::complex<double>(1.0, 0.0) + std::polar(rho, theta))
                                  / std::abs(std::complex<double>(1.0, 0.0) + std::polar(rhoR, thetaR)));   // G11-3
    const double sumVsAnchorS = eSubS - pchDb20(absAs);   // (b) floor reading: the sum's h1 over the Anchor's own, in S
    const bool keepsH1 = pchAcKeepsH1(alg);
    const double cErr = std::abs(deMeas - dePred);
    const double tapSumS = std::abs(s.sum - (s.a + s.c)) / std::max(std::abs(s.sum), 1e-300);
    const double tapSumR = std::abs(r.sum - (r.a + r.c)) / std::max(std::abs(r.sum), 1e-300);
    const double thetaDeg = theta * 180.0 / kPchPi;
    const double thetaRDeg = thetaR * 180.0 / kPchPi;

    const bool premises = std::abs(anchorDb) <= kPchAcPremiseAnchorDb && r.gainsSteady && s.gainsSteady;
    const bool passA = std::abs(anchorDb) <= kPchAcBarADb;
    const bool passB = keepsH1 ? std::abs(deMeas) <= kPchAcBarBDb : sumVsAnchorS >= -kPchAcBarFloorDb;
    const bool passC = premises && cErr <= kPchAcBarCDb;

    std::cout << std::setprecision(6) << "SC-017 " << pchNoteName(note) << ' ' << pchAlgName(alg) << ' '
              << pchWaveName(wave) << (gatedRow ? "" : " (recorded)") << ": rho " << rho << " theta " << thetaDeg
              << " deg, R " << rhoRDb << " dB / " << thetaRDeg << " deg, anchor S/R " << anchorDb << " dB, dE meas "
              << deMeas << " pred " << dePred << " (|err| " << cErr << "), tap-sum residual S " << tapSumS << " R "
              << tapSumR << "\n";
    CHECK(pchFinite(rho));
    CHECK(pchFinite(theta));
    CHECK(pchFinite(deMeas));
    CHECK(pchFinite(dePred));
    if (gatedRow) {
        // Premises (plan S9.10, G11-3): the Anchor equal in S and R, steady gains (g_amp constant, g_d = 1). R's
        // rho_R / theta_R are recorded (ShapeMorph rotates h1 on every chain) and enter the two-chain prediction.
        INFO("premises: |20 log10(|A_S| / |A_R|)| " << std::abs(anchorDb) << " dB (<= " << kPchAcPremiseAnchorDb
                                                    << "), gains steady R " << r.gainsSteady << " S " << s.gainsSteady
                                                    << "; recorded R: " << rhoRDb << " dB / " << thetaRDeg << " deg");
        CHECK(std::abs(anchorDb) <= kPchAcPremiseAnchorDb);
        CHECK(r.gainsSteady);
        CHECK(s.gainsSteady);
        INFO("(a) Anchor w1 S vs R " << anchorDb << " dB (bar " << kPchAcBarADb << ")");
        CHECK(passA);
        if (keepsH1) {
            INFO("(b) |E_sub(S) - E_sub(R)| " << std::abs(deMeas) << " dB (bar " << kPchAcBarBDb << ")");
            CHECK(passB);
        } else {
            INFO("(b) floor (G11-3, h1-removing algorithm; |dE| " << std::abs(deMeas)
                 << " dB recorded): E_sub(S) - E_sub(A_S) " << sumVsAnchorS << " dB (bar >= -" << kPchAcBarFloorDb << ")");
            CHECK(passB);
        }
        if (premises) {
            INFO("(c) |dE_meas - dE_pred| " << cErr << " dB (bar " << kPchAcBarCDb << "), dE_meas " << deMeas
                                            << " dE_pred " << dePred);
            CHECK(cErr <= kPchAcBarCDb);
        }
    }
    csv << pchNoteName(note) << ',' << pchAlgName(alg) << ',' << pchWaveName(wave) << ',' << (gatedRow ? 1 : 0) << ','
        << w.f0 << ',' << w.n << ',' << foldTarget << ',' << rho << ',' << thetaDeg << ',' << rhoRDb << ','
        << thetaRDeg << ',' << anchorDb << ',' << eSubS << ',' << eSubR << ',' << deMeas << ',' << dePred << ','
        << cErr << ',' << tapSumS << ',' << tapSumR << ',' << ((r.gainsSteady && s.gainsSteady) ? 1 : 0) << ','
        << std::max(r.fDev, s.fDev) << ',' << pchDb20(std::abs(s.aHalf)) << ',' << (premises ? 1 : 0) << ','
        << (keepsH1 ? "4.1dB" : "floor") << ',' << sumVsAnchorS << ',' << (passA ? 1 : 0) << ',' << (passB ? 1 : 0)
        << ',' << (passC ? 1 : 0) << '\n';
    return !gatedRow || (passA && passB && passC);
}

void pchAcRun(std::uint8_t note, const std::vector<PchAlg>& algs, const std::vector<PchWave>& waves,
              const std::string& file) {
    enableFTZDAZ();
    std::ostringstream csv;
    csv << std::setprecision(6) << pchAcCsvHeader();
    std::size_t failures = 0;
    for (const PchAlg alg : algs) {
        for (const PchWave wave : waves) {
            if (!pchAcCell(note, alg, wave, csv))
                ++failures;
        }
    }
    std::cout << "SC-017 " << file << ": " << algs.size() * waves.size() << " cells, " << failures << " failing\n";
    pchWriteEvidence(file, csv.str());
    CHECK(failures == 0);
}

[[nodiscard]] std::vector<PchAlg> pchAllAlgs() {
    std::vector<PchAlg> a;
    for (std::size_t i = 0; i < BassCharacterStage::kNumAlgorithms; ++i)
        a.push_back(static_cast<PchAlg>(i));
    return a;
}

[[nodiscard]] std::vector<PchWave> pchAllWaves() { return {kPchAcWaves.begin(), kPchAcWaves.end()}; }

// =============================================================================
// SC-018: FOLLOW tracking through a pitch-moving stage (H-4; FR-007; H-12)
// =============================================================================
// The P-4 setting (evidence/probes/p4-follow-flip.txt): A_s (kSineAnchor, Fold 0, excitation 0: a half-wave-
// symmetric Character input), Anchor FOLLOW, MONO, one held note; the Amount moves through both endpoints (position 0)
// at grid points. (i) Sweep: 0.5 s at Amount 0, Amount 0 -> 1 over 10 s (one value per grid interval), held at 1 for
// 6.5 s. (ii) Step: Amount 0 for 6.5 s, 1 from one grid point on for 6.5 s. Event = the chunk at which voice 0's
// follow-ratio tap changes value. Anchor frequency per sample = wrapped phase() differences x fs (anchorPhaseTap
// seam). Reference = ProfundumTest::anchorShsF0 of the Character tap mid on the last 2^18 samples of the segment
// after the event (wholly inside the final hold). Settling = the first sample after the event from which the Anchor
// stays within +-5 cents of that reference to the end of the segment. Gate: settling exists (and the error after it
// is <= 5 cents, by its definition, asserted). Latency = settling - event, recorded. H-12 (Fracture at Tone >= 0.75 on
// a saw-like voice, kSawAnchor) is recorded, not gated.

constexpr double kPchFtLeadS = 0.5;
constexpr double kPchFtSweepS = 10.0;
constexpr double kPchFtHoldS = 6.5;   // >= 6 s (spec SC-018)
constexpr double kPchFtCentsBar = 5.0;
constexpr std::size_t kPchFtFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;  // anchorShsF0's frame (262144)

enum class PchFtKind : std::uint8_t { Sweep, Step };

struct PchFtSpec {
    PchAlg alg = PchAlg::Fracture;
    std::uint8_t note = pchPt::kVoiceEngineNoteE1;
    PchFtKind kind = PchFtKind::Step;
    float tone = 0.5f;
    bool sawRecipe = false;   // H-12: kSawAnchor instead of A_s's kSineAnchor
    bool gated = true;
};

[[nodiscard]] MorphState pchFtState(const PchFtSpec& sp, float amount) {
    MorphState s = pchStateAs();
    if (sp.sawRecipe)
        s.recipe = SpectralShapeRecipe::kSawAnchor;
    s.foldAmount = 0.0f;
    s.excitation.mix = 0.0f;
    s.characterAlgorithm = sp.alg;
    s.characterAmount = amount;
    s.characterTone = sp.tone;
    s.anchor.mode = AnchorOscillator::Mode::Follow;
    REQUIRE(isValidMorphState(s));
    return s;
}

struct PchFtSchedule {
    std::uint64_t total = 0;
    std::uint64_t holdStart = 0;   // the final hold at Amount 1: [holdStart, total)
    std::uint64_t lead = 0;        // sweep: the Amount-0 lead-in
    std::uint64_t sweepLen = 0;    // sweep: its length
    PchFtKind kind = PchFtKind::Step;

    [[nodiscard]] float amountAt(std::uint64_t pos) const noexcept {
        if (kind == PchFtKind::Step)
            return pos >= holdStart ? 1.0f : 0.0f;
        if (pos < lead)
            return 0.0f;
        if (pos >= lead + sweepLen)
            return 1.0f;
        return static_cast<float>(static_cast<double>(pos - lead) / static_cast<double>(sweepLen));
    }
};

[[nodiscard]] PchFtSchedule pchFtSchedule(PchFtKind kind) {
    PchFtSchedule s;
    s.kind = kind;
    if (kind == PchFtKind::Step) {
        s.holdStart = pchAt(kPchFtHoldS);
        s.total = s.holdStart + pchAt(kPchFtHoldS);
    } else {
        s.lead = pchAt(kPchFtLeadS);
        s.sweepLen = pchAt(kPchFtSweepS);
        s.holdStart = s.lead + s.sweepLen;
        s.total = s.holdStart + pchAt(kPchFtHoldS);
    }
    return s;
}

/// The engine driven directly (one grid interval per processBlock), the T009 observer recording voice 0.
[[nodiscard]] pchPt::VoiceEngineTaps pchFtRender(const PchFtSpec& sp, const PchFtSchedule& sch) {
    MorphState s = pchFtState(sp, sch.amountAt(0));
    pchPt::VoiceEngineScript sc;
    sc.fs = kPchFs;
    sc.hostBlock = kPchGrid;
    sc.totalSamples = sch.total;
    sc.a = s;
    sc.b = s;
    sc.mode = ProfundumEngine::VoiceMode::Mono;
    sc.tappedVoices = 1;
    sc.config.seams.anchorPhaseTap = true;
    pchPt::VoiceEngineTaps taps;
    pchPt::voiceEngineAllocateTaps(taps, sc);
    pchPt::VoiceEngineObserver observer(taps);
    auto e = std::make_unique<ProfundumEngine>();
    e->setSeed(sc.seed);
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s));
    e->setVoiceMode(ProfundumEngine::VoiceMode::Mono);
    e->setObserver(&observer);
    e->prepare(sc.fs, sc.config);
    e->noteOn(sp.note, kPchVelocity);
    float last = s.characterAmount;
    bool endpointsOk = true;
    for (std::uint64_t pos = 0; pos < sch.total; pos += kPchGrid) {
        const float amount = sch.amountAt(pos);
        if (!pchSame(amount, last)) {
            s.characterAmount = amount;
            const bool okA = e->setEndpoint(BassMorphEngine::Endpoint::A, s);
            const bool okB = e->setEndpoint(BassMorphEngine::Endpoint::B, s);
            endpointsOk = endpointsOk && okA && okB;
            last = amount;
        }
        const auto m = static_cast<std::size_t>(std::min<std::uint64_t>(kPchGrid, sch.total - pos));
        e->processBlock(taps.outL.data() + pos, taps.outR.data() + pos, m);
    }
    taps.droppedEvents = e->droppedEvents();
    e->setObserver(nullptr);
    REQUIRE(endpointsOk);
    REQUIRE(taps.droppedEvents == 0);
    return taps;
}

struct PchFtEvent {
    std::uint64_t sample = 0;
    float from = 1.0f;
    float to = 1.0f;
};

[[nodiscard]] std::vector<PchFtEvent> pchFtEvents(const pchPt::VoiceEngineVoiceTapSet& v) {
    std::vector<PchFtEvent> out;
    for (std::size_t i = 1; i < v.chunks.size(); ++i) {
        if (!pchSame(v.chunks[i].followRatio, v.chunks[i - 1].followRatio))
            out.push_back(PchFtEvent{.sample = v.chunks[i].sample,
                                     .from = v.chunks[i - 1].followRatio,
                                     .to = v.chunks[i].followRatio});
    }
    return out;
}

/// Anchor frequency per sample: the wrapped difference of phase() (cycles, [0, 1)) times fs; 0 at sample 0.
[[nodiscard]] std::vector<double> pchAnchorHz(const pchPt::VoiceEngineVoiceTapSet& v) {
    std::vector<double> f(v.anchorPhase.size(), 0.0);
    for (std::size_t n = 1; n < v.anchorPhase.size(); ++n) {
        double d = static_cast<double>(v.anchorPhase[n]) - static_cast<double>(v.anchorPhase[n - 1]);
        if (d < 0.0)
            d += 1.0;
        f[n] = d * kPchFs;
    }
    return f;
}

[[nodiscard]] double pchCents(double f, double ref) noexcept {
    if (!(f > 0.0) || !(ref > 0.0))
        return 1.0e9;   // no frequency: out of any tolerance
    return 1200.0 * std::log2(f / ref);
}

/// anchorShsF0 of the Character tap mid over [start, start + 2^18).
[[nodiscard]] double pchShsRef(const pchPt::VoiceEngineVoiceTapSet& v, std::size_t start, double hint) {
    REQUIRE(start + kPchFtFrame <= v.charL.size());
    std::vector<float> mid(kPchFtFrame, 0.0f);
    for (std::size_t i = 0; i < kPchFtFrame; ++i)
        mid[i] = 0.5f * (v.charL[start + i] + v.charR[start + i]);
    return pchPt::anchorShsF0(mid.data(), kPchFs, hint);
}

[[nodiscard]] std::string pchFtCsvHeader() {
    return "case,algorithm,note,tone,recipe,kind,gated,event_index,event_sample,ratio_from,ratio_to,f_core_hz,"
           "post_ref_hz,post_ref_vs_fcore_ratio_cents,post_frame_start,post_frame_in_hold,settling_sample,"
           "settled,latency_samples,latency_ms,max_abs_err_after_settling_cents,pre_ref_hz,pre_mean_abs_err_cents,"
           "pass\n";
}

[[nodiscard]] std::string pchFtCaseName(const PchFtSpec& sp) {
    std::ostringstream os;
    os << (sp.gated ? "SC-018" : "H-12") << ' ' << pchAlgName(sp.alg) << ' ' << pchNoteName(sp.note) << ' '
       << (sp.kind == PchFtKind::Sweep ? "sweep" : "step") << " tone " << sp.tone;
    return os.str();
}

/// One SC-018 case. `inert`: P-4 read FOLLOW INERT (then no event may occur). Returns pass.
bool pchFtCase(const PchFtSpec& sp, bool inert, std::ostringstream& csv) {
    const std::string name = pchFtCaseName(sp);
    INFO(name);
    const PchFtSchedule sch = pchFtSchedule(sp.kind);
    const pchPt::VoiceEngineTaps t = pchFtRender(sp, sch);
    const pchPt::VoiceEngineVoiceTapSet& v = t.voices[0];
    REQUIRE(!v.chunks.empty());
    const std::vector<PchFtEvent> events = pchFtEvents(v);
    const std::size_t total = t.total;
    const std::string prefix = std::string(sp.gated ? "SC-018" : "H-12") + ',' + pchAlgName(sp.alg) + ','
                               + pchNoteName(sp.note) + ',' + std::to_string(sp.tone) + ','
                               + (sp.sawRecipe ? "kSawAnchor" : "kSineAnchor") + ','
                               + (sp.kind == PchFtKind::Sweep ? "sweep" : "step") + ',' + (sp.gated ? "1" : "0");

    if (inert) {
        std::cout << name << ": FOLLOW INERT (P-4), events " << events.size() << "\n";
        csv << prefix << ",FOLLOW INERT,events=" << events.size() << ",,,,,,,,,,,,,,," << (events.empty() ? 1 : 0)
            << '\n';
        if (sp.gated)
            CHECK(events.empty());
        return events.empty();
    }
    if (events.empty()) {
        std::cout << name << ": no follow-ratio event\n";
        csv << prefix << ",no_event,,,,,,,,,,,,,,,,0\n";
        if (sp.gated)
            CHECK(!events.empty());
        return !sp.gated;
    }

    const std::vector<double> fA = pchAnchorHz(v);
    bool allPass = true;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const PchFtEvent& ev = events[i];
        const auto e0 = static_cast<std::size_t>(ev.sample);
        const std::size_t segEnd = i + 1 < events.size() ? static_cast<std::size_t>(events[i + 1].sample) : total;
        INFO("event " << i << " at sample " << ev.sample << " ratio " << ev.from << " -> " << ev.to << ", segment end "
                      << segEnd);
        const double fCore = static_cast<double>(v.fCore[e0]);
        REQUIRE(fCore > 0.0);
        // The post-event reference frame: the segment's last 2^18 samples, wholly inside the final hold.
        const bool frameFits = segEnd >= kPchFtFrame && segEnd - kPchFtFrame >= e0;
        const std::size_t frameStart = frameFits ? segEnd - kPchFtFrame : 0;
        const bool frameInHold = frameFits && frameStart >= static_cast<std::size_t>(sch.holdStart);
        if (sp.gated)
            CHECK(frameInHold);
        double ref = 0.0;
        if (frameFits)
            ref = pchShsRef(v, frameStart, fCore * static_cast<double>(ev.to));
        const double refVsCore = frameFits ? pchCents(ref, fCore * static_cast<double>(ev.to)) : 0.0;

        // Settling: one past the last out-of-tolerance sample after the event.
        std::size_t settling = e0 + 1;
        for (std::size_t n = segEnd; n-- > e0 + 1;) {
            if (!(std::abs(pchCents(fA[n], ref)) <= kPchFtCentsBar)) {
                settling = n + 1;
                break;
            }
        }
        const bool settled = frameFits && settling < segEnd;
        double maxErr = 0.0;
        for (std::size_t n = settling; n < segEnd; ++n)
            maxErr = std::max(maxErr, std::abs(pchCents(fA[n], ref)));
        const std::size_t latency = settling - e0;

        // Pre-event reference (recorded): the 2^18 samples ending at the event, when they fit after the note-on.
        double preRef = 0.0;
        double preErr = 0.0;
        const bool preFits = e0 >= kPchFtFrame + kPchGrid;
        if (preFits) {
            const std::size_t ps = e0 - kPchFtFrame;
            preRef = pchShsRef(v, ps, fCore * static_cast<double>(ev.from));
            double acc = 0.0;
            for (std::size_t n = ps; n < e0; ++n)
                acc += std::abs(pchCents(fA[n], preRef));
            preErr = acc / static_cast<double>(kPchFtFrame);
        }

        const bool pass = frameInHold && settled && maxErr <= kPchFtCentsBar && pchFinite(ref);
        if (sp.gated) {
            INFO("reference " << ref << " Hz (f_core x ratio " << fCore * static_cast<double>(ev.to)
                              << " Hz), settling sample " << settling << ", latency " << latency
                              << " samples, max |err| after settling " << maxErr << " cents (bar " << kPchFtCentsBar
                              << ")");
            CHECK(settled);
            CHECK(maxErr <= kPchFtCentsBar);
            CHECK(pchFinite(ref));
        }
        allPass = allPass && pass;
        std::cout << std::setprecision(6) << name << ": event " << i << " @" << ev.sample << " ratio " << ev.from
                  << " -> " << ev.to << ", ref " << ref << " Hz (" << refVsCore << " cents vs f_core x ratio), latency "
                  << latency << " samples (" << 1000.0 * static_cast<double>(latency) / kPchFs
                  << " ms), max |err| after " << maxErr << " cents, settled " << settled << "\n";
        csv << prefix << ',' << i << ',' << ev.sample << ',' << ev.from << ',' << ev.to << ',' << fCore << ',' << ref
            << ',' << refVsCore << ',' << frameStart << ',' << (frameInHold ? 1 : 0) << ',' << settling << ','
            << (settled ? 1 : 0) << ',' << latency << ',' << 1000.0 * static_cast<double>(latency) / kPchFs << ','
            << maxErr << ',';
        if (preFits)
            csv << preRef << ',' << preErr;
        else
            csv << "na,na";
        csv << ',' << (pass ? 1 : 0) << '\n';
    }
    return !sp.gated || allPass;
}

/// P-4's verdict (tasks.md T017: "If evidence/probes/p4-follow-flip.txt reads FOLLOW INERT ...").
[[nodiscard]] bool pchFollowInert() {
    std::ifstream in(pchEvidenceDir() + "probes/p4-follow-flip.txt");
    REQUIRE(in.good());
    std::string line;
    bool verdictSeen = false;
    bool inert = false;
    while (std::getline(in, line)) {
        if (line.starts_with("P4 VERDICT")) {
            verdictSeen = true;
            inert = line.find("FOLLOW INERT") != std::string::npos;
        }
    }
    REQUIRE(verdictSeen);
    return inert;
}

void pchFtRun(const std::vector<PchFtSpec>& specs, const std::string& file) {
    enableFTZDAZ();
    const bool inert = pchFollowInert();
    std::ostringstream csv;
    csv << std::setprecision(6) << pchFtCsvHeader();
    if (inert)
        csv << "# FOLLOW INERT (evidence/probes/p4-follow-flip.txt): every case asserts that no event occurs\n";
    std::size_t failures = 0;
    for (const PchFtSpec& sp : specs) {
        if (!pchFtCase(sp, inert, csv))
            ++failures;
    }
    std::cout << "SC-018 " << file << ": " << specs.size() << " cases, " << failures << " failing"
              << (inert ? " (FOLLOW INERT)" : "") << "\n";
    pchWriteEvidence(file, csv.str());
    CHECK(failures == 0);
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-015 / SC-016
// -----------------------------------------------------------------------------

TEST_CASE("VoiceEngine_MonoSum_Solid", "[systems][profundum][voice-engine][sc-015][sc-016][long]") {
    pchMonoSumRun(pchMsGrid(PchMode::Solid), "monosum-solid.csv");
}

TEST_CASE("VoiceEngine_MonoSum_Spread", "[systems][profundum][voice-engine][sc-015][sc-016][long]") {
    pchMonoSumRun(pchMsGrid(PchMode::Spread), "monosum-spread.csv");
}

TEST_CASE("VoiceEngine_MonoSum_Wide", "[systems][profundum][voice-engine][sc-015][sc-016][long]") {
    pchMonoSumRun(pchMsGrid(PchMode::Wide), "monosum-wide.csv");
}

TEST_CASE("VoiceEngine_MonoSumSmoke", "[systems][profundum][voice-engine][sc-015][sc-016]") {
    pchMonoSumRun({PchMsCell{.mode = PchMode::Solid,
                             .voices = ProfundumEngine::VoiceMode::Mono,
                             .base = PchBase::As,
                             .note = pchPt::kVoiceEngineNoteE1}},
                  "monosum-smoke.csv");
}

// -----------------------------------------------------------------------------
// SC-017
// -----------------------------------------------------------------------------

TEST_CASE("VoiceEngine_AnchorThroughChain_E0", "[systems][profundum][voice-engine][sc-017][long]") {
    pchAcRun(pchPt::kVoiceEngineNoteE0, pchAllAlgs(), pchAllWaves(), "anchor-chain-E0.csv");
}

TEST_CASE("VoiceEngine_AnchorThroughChain_E1", "[systems][profundum][voice-engine][sc-017][long]") {
    pchAcRun(pchPt::kVoiceEngineNoteE1, pchAllAlgs(), pchAllWaves(), "anchor-chain-E1.csv");
}

TEST_CASE("VoiceEngine_AnchorThroughChain_E2", "[systems][profundum][voice-engine][sc-017][long]") {
    pchAcRun(pchPt::kVoiceEngineNoteE2, pchAllAlgs(), pchAllWaves(), "anchor-chain-E2.csv");
}

TEST_CASE("VoiceEngine_AnchorThroughChainSmoke", "[systems][profundum][voice-engine][sc-017]") {
    pchAcRun(pchPt::kVoiceEngineNoteE1, {PchAlg::Rectify}, {PchWave::Sine}, "anchor-chain-smoke.csv");
}

// -----------------------------------------------------------------------------
// SC-018
// -----------------------------------------------------------------------------

TEST_CASE("VoiceEngine_FollowTracking", "[systems][profundum][voice-engine][sc-018][long]") {
    std::vector<PchFtSpec> specs;
    for (const PchAlg alg : {PchAlg::Rectify, PchAlg::Fracture}) {
        for (const std::uint8_t note : {pchPt::kVoiceEngineNoteE1, pchPt::kVoiceEngineNoteC3}) {
            for (const PchFtKind kind : {PchFtKind::Sweep, PchFtKind::Step})
                specs.push_back(PchFtSpec{.alg = alg, .note = note, .kind = kind});
        }
    }
    // H-12 (recorded): Fracture on a saw-like voice at Tone >= 0.75, step, E1.
    for (const float tone : {0.75f, 1.0f}) {
        specs.push_back(PchFtSpec{.alg = PchAlg::Fracture,
                                  .note = pchPt::kVoiceEngineNoteE1,
                                  .kind = PchFtKind::Step,
                                  .tone = tone,
                                  .sawRecipe = true,
                                  .gated = false});
    }
    pchFtRun(specs, "follow-tracking.csv");
}

TEST_CASE("VoiceEngine_FollowTrackingSmoke", "[systems][profundum][voice-engine][sc-018]") {
    pchFtRun({PchFtSpec{.alg = PchAlg::Fracture, .note = pchPt::kVoiceEngineNoteE1, .kind = PchFtKind::Step}},
             "follow-tracking-smoke.csv");
}
