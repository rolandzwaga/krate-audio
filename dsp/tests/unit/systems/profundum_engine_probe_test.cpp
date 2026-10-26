// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_probe_test.cpp
// ==============================================================================
// Hidden cases only (tasks.md deviation 7: every case carries [profundum] and a hidden tag).
// Filled by four tasks in four sequential groups, each appending (tasks.md deviation 4):
//   T006 - P-3 MassDynamics_OversamplingAliasProbe (plan R-C, S10.4; C-3)
//   T013 - P-1, P-2, P-4, H-13
//   T014 - harvests
//   T021 - listening renders
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on.
//
// ---- P-3 (T006) ----------------------------------------------------------------
// Phase 6's Fold-100 E2 cells (mass_dynamics_aliasing_test.cpp mdaRunGrid :285-293 / mdaRunCell :190-273:
// input ShapeCharacterTest::fold100(kSawAnchor), attack A in {+12, -12} dB, E2 only) at 44.1 and 48 kHz,
// through MassDynamics alone at the SC-010 settings (mdaRenderDynamics :107-129, replicated here with the
// FR-082 opt-in as the only difference: setOversampling(on) before prepare), one sample at a time, L == R
// copies in two distinct buffers (so the stereo path of the opt-in runs).
//   (i)  Phase 6's residual method exactly as mdaRunCell calls it: steady aliasedPower on one
//        aliasFftLength(fs, f0) frame at 1 s, and MassDynamicsTest::aliasResidualDb over the burst-onset and
//        the step windows, with the per-sample gain tap shaper().currentGainDb() + compressor().currentGainDb().
//        With the opt-in on the tap is the gain of the last inner sample, and the output is delayed by the
//        halfband pair, so the on-path burst / step residuals also carry that delay: recorded, not the verdict.
//  (ii)  SC-021's estimator on the same steady output: ProfundumTest::aliasedPower(y, aliasFftLength(fs, f0),
//        fs, f0) after a 1 s settle, L and R separately, the worst channel.
// Per cell: `P3 <fs> <cell> off=<dB> on=<dB> estimator_off=<dBFS> estimator_on=<dBFS>` (off / on = the worst
// of the three (i) windows); last line `P3 VERDICT GREEN|RED <worst on dBFS>`: GREEN iff every on-reading of
// (ii) <= -80 dBFS (Q4; the bar is not moved). The input's own estimator reading is printed beside it, so a
// reading set by the fixture rather than the dynamics is attributable. CHECK, never REQUIRE, so the whole
// table prints. Written to evidence/probes/p3-output-alias.txt. Run by evidence/close/probe.sh p3.
//
// ---- T013: P-1, P-2, P-4, H-13 (plan S10.4, R-A, R-B, H-13; tasks.md deviations 4, 10) --------------------
// P-1 VoiceEngine_CpuProbe [.probe][.perf] (run ALONE and pinned: probe.sh p1). The SC-001 setting on both
//     showcase endpoints (excitation 1 / 1, SHAPE 0.5, Fold 100, Character Tube | Fracture at Amount 1, Anchor
//     FOLLOW, feed 1, Drop 24 st retriggered every 250 ms, CYCLE -> Mass depth 1, WIDE with field Motion,
//     Mass 100), the morph ramping 0 <-> 1 (ramp 1000 ms, target toggled every 1 s), host block 64, no observer.
//     The real ProfundumEngine: MONO (E1), POLY 4 (E1 / A1 / E2 / C3) PerVoice and PostSum; 10 s timed after a
//     1 s warm-up, median of 3 fresh engines, % = wall / audio. Then the deviation-10 test-side replica (the same
//     components composed in the S5.4 / S5.6 / S6.5 order, change-only setters, no observer) timed whole and with
//     one stage removed at a time (same protocol), at 1 and 4 voices; cost = full - without. Lines:
//     `P1 MONO|POLY4|POSTSUM4 <tube%> <fracture%>`, `P1 BREAKDOWN ...`, `P1 STAGE ...`, `P1 VERDICT GREEN|RED`
//     (GREEN iff max(MONO) <= 8 and (max(POLY4) <= 25 or max(POSTSUM4) <= 25)). evidence/probes/p1-cpu.txt.
// P-2 VoiceEngine_DeclickReferenceProbe [.probe]: SC-005(a) / SC-006(a) on the worst cells (A_s, E1 and C3,
//     Reset, held victim, SOLID, host block 1) against the polyphony-5 (SC-005) and POLY-2 (SC-006) references,
//     with the (b) margin. `P2 VERDICT GREEN|RED`: RED iff a cell fails (a) while (b) passes.
//     evidence/probes/p2-declick.txt.
// P-4 VoiceEngine_FollowFlipProbe [.probe]: Amount 0 -> 1 in 101 steps, Rectify and Fracture, E1 / C3, on a
//     sine-like voice (A_s: kSineAnchor, Fold 0, excitation 0): the follow-ratio tap per step; the first Amount
//     at which perceivedPitchRatio() != 1, or NO FLIP; FOLLOW INERT if nothing flips.
//     evidence/probes/p4-follow-flip.txt.
// H-13 VoiceEngine_CharacterRecords [.probe] (Record): E0 / E1 / E2 / C3 x six algorithms at Amount 1, Fold 0,
//     kSawAnchor (Phase 5's fixture): complex h1 of the Character tap over the Character input (gain dB, phase),
//     the group delay at f0 from the phase at f0 +- 1 % (pitch bend), and the kGrowl level swing (L_K of the
//     Character tap, Amount 0, SHAPE 0 ... 1 in 11 steps). evidence/h13-character-records.txt.
// Decisions: evidence/run-log/T013.txt.
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "mass_dynamics_probe.h"
#include "mass_dynamics_test_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <enable_ftz_daz.h>

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
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
#include <cstdlib>   // std::getenv (main-loop P-1 variants)

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace p3Md = Krate::DSP::MassDynamicsTest;
namespace p3Ts = Krate::DSP::TransientShaperTest;
namespace p3Sc = Krate::DSP::ShapeCharacterTest;
namespace p3Pt = Krate::DSP::ProfundumTest;

std::string p3EvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

// Phase 6 SC-010 settings (mass_dynamics_aliasing_test.cpp:70-88).
constexpr double kP3BarDbfs = -80.0;   // SC-021(b), Q4
constexpr float kP3DurationMs = TransientShaper::kMinDurationMs;
constexpr float kP3SustainDb = 0.0f;
constexpr float kP3CompAmount = 1.0f;
constexpr float kP3FixedReleaseMs = BassCompressor::kMinReleaseMs;
constexpr std::array<float, 2> kP3AttacksDb{TransientShaper::kMaxGainDb, TransientShaper::kMinGainDb};
constexpr std::array<double, 2> kP3Rates{44100.0, 48000.0};
constexpr double kP3SteadyStartS = 1.0;    // the 1 s settle
constexpr double kP3SteadyWindowS = 1.0;
constexpr double kP3BurstPreRollS = 0.25;
constexpr double kP3BurstHoldS = 0.5;
constexpr double kP3StepAtS = 2.0;
constexpr double kP3StepHighS = 0.5;

struct P3Render {
    std::vector<float> yL;
    std::vector<float> yR;
    std::vector<float> gainDb;
};

/// mdaRenderDynamics (mass_dynamics_aliasing_test.cpp:107-129) with the FR-082 opt-in, one sample at a time.
P3Render p3RenderDynamics(const std::vector<float>& x, double fs, double f0, float attackDb, bool oversampling) {
    auto md = std::make_unique<MassDynamics>();
    md->setOversampling(oversampling);
    md->setOversamplingFactor(ProfundumEngineConfig{}.dynamicsOversamplingFactor);   // the engine's request (4x; Q4 refinement)
    md->setTransientAttackDb(attackDb);
    md->setTransientSustainDb(kP3SustainDb);
    md->setTransientDurationMs(kP3DurationMs);
    md->setCompressionAmount(kP3CompAmount);
    md->setDetectorKeyHz(static_cast<float>(f0));
    md->prepare(fs, 512);
    detail::MassDynamicsProbe::setFixedReleaseMs(detail::MassDynamicsProbe::compressor(*md), kP3FixedReleaseMs);
    CHECK(md->oversamplingActive() == oversampling);

    P3Render r;
    r.yL.assign(x.size(), 0.0f);
    r.yR.assign(x.size(), 0.0f);
    r.gainDb.assign(x.size(), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i) {
        float l = x[i];
        float rr = x[i];
        md->processBlock(&l, &rr, 1);
        r.yL[i] = l;
        r.yR[i] = rr;
        r.gainDb[i] = md->shaper().currentGainDb() + md->compressor().currentGainDb();
    }
    return r;
}

struct P3Reading {
    double resSteady = 0.0;   // (i) steady A_out
    double resBurst = 0.0;    // (i) burst onset residual
    double resStep = 0.0;     // (i) step residual
    double estimator = 0.0;   // (ii) worst channel
    [[nodiscard]] double residualWorst() const { return std::max({resSteady, resBurst, resStep}); }
};

struct P3Inputs {
    std::vector<float> steady;
    std::vector<float> burst;
    std::vector<float> step;
    std::size_t steadyStart = 0;
    std::size_t onset = 0;
    std::size_t stepAt = 0;
    std::size_t frame = 0;   // aliasFftLength(fs, f0)
};

/// The mdaRunCell inputs for one cell, folded (mass_dynamics_aliasing_test.cpp:195-221).
P3Inputs p3MakeInputs(double fs, double f0) {
    P3Inputs in;
    in.steadyStart = p3Ts::secondsToSamples(fs, kP3SteadyStartS);
    const std::size_t steadyWindow = p3Ts::secondsToSamples(fs, kP3SteadyWindowS);
    in.frame = p3Pt::aliasFftLength(fs, f0);
    const std::size_t steadyNeed = in.steadyStart + std::max(steadyWindow, in.frame) + 1;
    in.steady = p3Sc::fold100(HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs,
                                                                static_cast<double>(steadyNeed) / fs + 0.01),
                              fs, f0);
    in.burst = p3Sc::fold100(p3Ts::burst(p3Ts::Tone::Saw, f0, fs, kP3BurstHoldS, kP3BurstPreRollS, 0.0), fs, f0);
    in.onset = p3Ts::onsetIndex(fs, kP3BurstPreRollS);
    in.step = p3Sc::fold100(p3Ts::twoLevelStep(p3Ts::Tone::Saw, f0, fs, kP3StepAtS, kP3StepHighS), fs, f0);
    in.stepAt = p3Ts::secondsToSamples(fs, kP3StepAtS);
    return in;
}

P3Reading p3Measure(const P3Inputs& in, double fs, double f0, float attackDb, bool oversampling) {
    P3Reading r;
    const bool steadyFits = in.steady.size() >= in.steadyStart + in.frame;
    CHECK(steadyFits);
    if (!steadyFits)
        return r;
    const P3Render steady = p3RenderDynamics(in.steady, fs, f0, attackDb, oversampling);
    const P3Render burst = p3RenderDynamics(in.burst, fs, f0, attackDb, oversampling);
    const P3Render step = p3RenderDynamics(in.step, fs, f0, attackDb, oversampling);

    // (i) Phase 6's residual method, as mdaRunCell calls it (:229-237).
    r.resSteady = p3Pt::aliasedPower(steady.yL.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
    r.resBurst = p3Md::aliasResidualDb(in.burst.data(), burst.gainDb.data(), burst.yL.data(), in.burst.size(), fs,
                                       in.onset);
    r.resStep =
        p3Md::aliasResidualDb(in.step.data(), step.gainDb.data(), step.yL.data(), in.step.size(), fs, in.stepAt);

    // (ii) SC-021's estimator after the 1 s settle, worst channel.
    const double eL = p3Pt::aliasedPower(steady.yL.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
    const double eR = p3Pt::aliasedPower(steady.yR.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
    r.estimator = std::max(eL, eR);
    return r;
}

/// Main-loop diagnostic (2026-10-20): the steady estimator with the shaper neutral and / or the compressor off.
double p3SteadyEstimator(const P3Inputs& in, double fs, double f0, float attackDb, float sustainDb, float compAmount,
                         bool oversampling, std::size_t factor = 2) {
    auto md = std::make_unique<MassDynamics>();
    md->setOversampling(oversampling);
    md->setOversamplingFactor(factor);
    md->setTransientAttackDb(attackDb);
    md->setTransientSustainDb(sustainDb);
    md->setTransientDurationMs(kP3DurationMs);
    md->setCompressionAmount(compAmount);
    md->setDetectorKeyHz(static_cast<float>(f0));
    md->prepare(fs, 512);
    detail::MassDynamicsProbe::setFixedReleaseMs(detail::MassDynamicsProbe::compressor(*md), kP3FixedReleaseMs);
    std::vector<float> yL(in.steady.size()), yR(in.steady.size());
    for (std::size_t i = 0; i < in.steady.size(); ++i) {
        float l = in.steady[i];
        float r = in.steady[i];
        md->processBlock(&l, &r, 1);
        yL[i] = l;
        yR[i] = r;
    }
    const double eL = p3Pt::aliasedPower(yL.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
    const double eR = p3Pt::aliasedPower(yR.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
    return std::max(eL, eR);
}

}  // namespace

// Main-loop diagnostic (2026-10-20): which dynamics stage carries the -70 dBFS term with the opt-in on?
TEST_CASE("MassDynamics_OversamplingAliasAttribution", "[systems][profundum][mass-dynamics][voice-engine][.probe]") {
    enableFTZDAZ();
    std::ostringstream out;
    out << std::setprecision(6);
    out << "# P3ATTR steady estimator (worst channel) on Phase 6's Fold-100 E2 saw; variants x opt-in\n";
    struct V { const char* name; float attackDb; float sustainDb; float comp; };
    const std::array<V, 5> vs{{{"full(+12,comp1)", TransientShaper::kMaxGainDb, 0.0f, 1.0f},
                               {"shaper-neutral(0,comp1)", 0.0f, 0.0f, 1.0f},
                               {"comp-off(+12,comp0)", TransientShaper::kMaxGainDb, 0.0f, 0.0f},
                               {"both-neutral(0,comp0)", 0.0f, 0.0f, 0.0f},
                               {"sustain-12(comp1)", 0.0f, TransientShaper::kMaxGainDb, 1.0f}}};
    for (const int midi : {40, 28, 16}) {   // E2; G14-4 (2026-10-21): E1 and E0, where SC-021's grid read the dynamics tap at -66 dBFS
    const double f0 = static_cast<double>(Krate::DSP::midiNoteToFrequency(midi));
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        const P3Inputs in = p3MakeInputs(fs, f0);
        const double inputE = p3Pt::aliasedPower(in.steady.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
        out << "P3ATTR midi=" << midi << " fs=" << fs << " input=" << inputE << "\n";
        for (const V& v : vs) {
            const double off = p3SteadyEstimator(in, fs, f0, v.attackDb, v.sustainDb, v.comp, false);
            const double on = p3SteadyEstimator(in, fs, f0, v.attackDb, v.sustainDb, v.comp, true, 2);
            const double on4 = p3SteadyEstimator(in, fs, f0, v.attackDb, v.sustainDb, v.comp, true, 4);
            out << "P3ATTR midi=" << midi << " fs=" << fs << " " << v.name << " off=" << off << " on2x=" << on
                << " on4x=" << on4 << "\n";
            CHECK(std::isfinite(on));
        }
    }
    }
    std::cout << out.str();
    const std::string dir = p3EvidenceDir() + "probes/";
    std::filesystem::create_directories(dir);
    std::ofstream file(dir + "p3-attribution.txt");
    file << out.str();
    CHECK(file.good());
}

// P-3 (plan R-C, S10.4; C-3; tasks.md T006). Hidden: run by evidence/close/probe.sh p3. A RED verdict is
// STOP AND SURFACE to the user (R-C; Q4's -80 dBFS bar is not moved).
TEST_CASE("MassDynamics_OversamplingAliasProbe", "[systems][profundum][mass-dynamics][voice-engine][.probe]") {
    const double f0 = p3Ts::kE2Hz;
    std::ostringstream table;
    table << std::setprecision(6);
    table << "# P-3 MassDynamics_OversamplingAliasProbe (tasks.md T006; plan R-C, S10.4)\n"
          << "# Fold-100 kSawAnchor at E2 (" << f0 << " Hz) through MassDynamics alone, SC-010 settings, opt-in off / on\n"
          << "# off / on = worst of Phase 6 residual windows (steady A_out, burst, step), dBFS\n"
          << "# estimator = SC-021 aliasedPower(y, aliasFftLength(fs, f0)) after 1 s, worst of L / R, dBFS (gated "
          << kP3BarDbfs << " on the on reading)\n";

    double worstOn = -1.0e300;
    bool green = true;
    for (const double fs : kP3Rates) {
        const P3Inputs in = p3MakeInputs(fs, f0);
        const double estimatorIn = p3Pt::aliasedPower(in.steady.data() + in.steadyStart, in.frame, fs, f0).aliasedDbfs;
        for (const float a : kP3AttacksDb) {
            const std::string cell = std::string("E2_fold100_A") + (a > 0.0f ? "+12" : "-12");
            INFO("P-3 cell fs " << fs << " " << cell);
            const P3Reading off = p3Measure(in, fs, f0, a, false);
            const P3Reading on = p3Measure(in, fs, f0, a, true);
            table << "#   " << fs << " " << cell << " frame " << in.frame << " (bin " << fs / static_cast<double>(in.frame)
                  << " Hz) input_estimator=" << estimatorIn << " | off: steady " << off.resSteady << " burst "
                  << off.resBurst << " step " << off.resStep << " | on: steady " << on.resSteady << " burst "
                  << on.resBurst << " step " << on.resStep << "\n";
            table << "P3 " << fs << " " << cell << " off=" << off.residualWorst() << " on=" << on.residualWorst()
                  << " estimator_off=" << off.estimator << " estimator_on=" << on.estimator << "\n";
            worstOn = std::max(worstOn, on.estimator);
            const bool pass = on.estimator <= kP3BarDbfs;
            green = green && pass;
            CHECK(on.estimator <= kP3BarDbfs);
        }
    }
    table << "P3 VERDICT " << (green ? "GREEN" : "RED") << " " << worstOn << "\n";

    std::cout << table.str();
    const std::string dir = p3EvidenceDir() + "probes/";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + "p3-output-alias.txt");
    out << table.str();
    CHECK(out.good());
}

// ==============================================================================
// T013 - P-1, P-2, P-4, H-13 (plan S10.4, R-A, R-B, H-13; tasks.md T013, deviations 4, 10)
// ==============================================================================

namespace {

namespace t13Pt = Krate::DSP::ProfundumTest;

constexpr double kT13Fs = 48000.0;
constexpr double kT13Pi = 3.14159265358979323846;
constexpr std::size_t kT13Grid = ProfundumEngine::kControlInterval;
constexpr std::uint8_t kT13Velocity = 100;
constexpr std::uint8_t kT13NoteE0 = t13Pt::kVoiceEngineNoteE0;
constexpr std::uint8_t kT13NoteE1 = t13Pt::kVoiceEngineNoteE1;
constexpr std::uint8_t kT13NoteA1 = t13Pt::kVoiceEngineNoteA1;
constexpr std::uint8_t kT13NoteE2 = t13Pt::kVoiceEngineNoteE2;
constexpr std::uint8_t kT13NoteC3 = t13Pt::kVoiceEngineNoteC3;

constexpr std::array<const char*, BassCharacterStage::kNumAlgorithms> kT13AlgNames{"Tube", "Fold", "Rectify",
                                                                                   "Bite", "Crush", "Fracture"};

[[nodiscard]] const char* t13AlgName(BassCharacterStage::Algorithm a) noexcept {
    return kT13AlgNames[static_cast<std::size_t>(a)];
}

[[nodiscard]] std::string t13NoteName(std::uint8_t n) {
    switch (n) {
        case kT13NoteE0: return "E0";
        case kT13NoteE1: return "E1";
        case kT13NoteA1: return "A1";
        case kT13NoteE2: return "E2";
        case kT13NoteC3: return "C3";
        default: break;
    }
    return "n" + std::to_string(static_cast<int>(n));
}

/// Bit test (this TU builds with the shipping FP mode; std::isfinite may be folded away under -ffast-math).
[[nodiscard]] bool t13Finite(double x) noexcept {
    return (std::bit_cast<std::uint64_t>(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

[[nodiscard]] bool t13Same(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// A grid-aligned sample index (events land on grid points, so latched and applied coincide, C-9).
[[nodiscard]] std::uint64_t t13At(double seconds) noexcept {
    const auto n = static_cast<std::uint64_t>(std::llround(seconds * kT13Fs));
    return (n / kT13Grid) * kT13Grid;
}

[[nodiscard]] float t13NoteHz(std::uint8_t n) noexcept {
    return Krate::DSP::midiNoteToFrequency(static_cast<int>(n));
}

/// Plan S1 C-8: showcase A with ampEnv.sustain = 1 (A_s).
[[nodiscard]] MorphState t13StateAs() {
    MorphState s = Krate::DSP::BassMorphTest::showcasePair().first;
    s.ampEnv.sustain = 1.0f;
    return s;
}

/// Ruling B10-1: showcase B with ampEnv.sustain = 1 (B_s), the per-row base of Dark <-> Bright.
[[nodiscard]] MorphState t13StateBs() {
    MorphState s = Krate::DSP::BassMorphTest::showcasePair().second;
    s.ampEnv.sustain = 1.0f;
    return s;
}

void t13WriteEvidence(const std::string& relPath, const std::string& text) {
    const std::filesystem::path p = std::filesystem::path(p3EvidenceDir()) / relPath;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << text;
    CHECK(out.good());
}

[[nodiscard]] double t13Median(std::vector<double> v) {
    REQUIRE(!v.empty());
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// -----------------------------------------------------------------------------
// P-1 (R-A): CPU of the real engine, then the deviation-10 replica breakdown
// -----------------------------------------------------------------------------

constexpr double kP1WarmupS = 1.0;
constexpr double kP1TimedS = 10.0;
constexpr std::size_t kP1Trials = 3;
constexpr std::size_t kP1Block = 64;
constexpr double kP1RetriggerS = 0.25;
constexpr double kP1MorphToggleS = 1.0;
constexpr float kP1MorphRampMs = 1000.0f;
constexpr double kP1MonoBar = 8.0;
constexpr double kP1PolyBar = 25.0;
constexpr std::array<std::uint8_t, 4> kP1Poly4Notes{kT13NoteE1, kT13NoteA1, kT13NoteE2, kT13NoteC3};
constexpr std::array<BassCharacterStage::Algorithm, 2> kP1Algorithms{BassCharacterStage::Algorithm::Tube,
                                                                    BassCharacterStage::Algorithm::Fracture};

/// The SC-001 setting (spec SC-001) applied to one endpoint; everything else stays the showcase endpoint's.
void p1ApplySetting(MorphState& s, BassCharacterStage::Algorithm alg) {
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

[[nodiscard]] bool p1Setting(const char* key) {
    const char* v = std::getenv("P1_SETTING");
    return v != nullptr && std::string(v).find(key) != std::string::npos;
}

void p1ApplySettingVariant(MorphState& s) {
    if (p1Setting("solid")) {
        s.field.mode = BassStereoField::ImageMode::Solid;
        s.field.spread = 0.0f;
        s.field.motionDepth = 0.0f;
    }
    if (p1Setting("nowidemotion"))
        s.field.motionDepth = 0.0f;
    if (p1Setting("noexc"))
        s.excitation.mix = 0.0f;
}

[[nodiscard]] std::pair<MorphState, MorphState> p1Endpoints(BassCharacterStage::Algorithm alg) {
    const std::pair<MorphState, MorphState> pair = Krate::DSP::BassMorphTest::showcasePair();
    MorphState a = pair.first;
    MorphState b = pair.second;
    p1ApplySetting(a, alg);
    p1ApplySetting(b, alg);
    p1ApplySettingVariant(a);
    p1ApplySettingVariant(b);
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

[[nodiscard]] std::vector<std::uint8_t> p1Notes(std::size_t voices) {
    std::vector<std::uint8_t> n;
    if (voices <= 1) {
        n.push_back(kT13NoteE1);
        return n;
    }
    for (std::size_t i = 0; i < std::min(voices, kP1Poly4Notes.size()); ++i)
        n.push_back(kP1Poly4Notes[i]);
    return n;
}

/// Main-loop diagnostic (2026-10-20): P1_VARIANT = "static-morph" (position held at 0.5, Drop retriggers on) or
/// "static-all" (position held, no retriggers) isolates the continuous morph's control-path cost from the audio path.
[[nodiscard]] std::string p1Variant() {
    const char* v = std::getenv("P1_VARIANT");
    return v != nullptr ? std::string(v) : std::string();
}

/// One timed trial of the real engine (no observer). @return % of one core (wall / audio time x 100).
[[nodiscard]] double p1EngineTrial(BassCharacterStage::Algorithm alg, std::size_t voices,
                                   ProfundumNonlinearPlacement placement, double& sink) {
    const std::string variant = p1Variant();
    const bool staticMorph = variant == "static-morph" || variant == "static-all" || variant == "static-breakdown";
    const bool noRetrigger = variant == "static-all";
    const std::pair<MorphState, MorphState> ends = p1Endpoints(alg);
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, ends.first));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, ends.second));
    e->setMorphPosition(0.0f);
    e->setMorphRampMs(kP1MorphRampMs);
    e->setVoiceMode(voices <= 1 ? ProfundumEngine::VoiceMode::Mono : ProfundumEngine::VoiceMode::Poly);
    e->setPolyphony(std::max<std::size_t>(voices, 1));
    ProfundumEngineConfig cfg{};
    cfg.placement = placement;
    if (p1Setting("dyn2x"))
        cfg.dynamicsOversamplingFactor = 2;
    if (p1Setting("dynoff"))
        cfg.dynamicsOversampling = false;
    e->prepare(kT13Fs, cfg);
    const std::vector<std::uint8_t> notes = p1Notes(voices);
    for (const std::uint8_t n : notes)
        e->noteOn(n, kT13Velocity);

    std::array<float, kP1Block> l{};
    std::array<float, kP1Block> r{};
    const auto retrigger = static_cast<std::size_t>(std::llround(kP1RetriggerS * kT13Fs));
    const auto toggle = static_cast<std::size_t>(std::llround(kP1MorphToggleS * kT13Fs));
    std::size_t nextRetrigger = retrigger;
    std::size_t nextToggle = toggle;
    float position = 0.0f;
    if (staticMorph)
        e->setMorphPosition(0.5f);   // settles inside the 1 s warm-up (ramp 1000 ms)
    std::size_t pos = 0;
    double acc = 0.0;
    const auto step = [&]() noexcept {
        if (pos >= nextRetrigger && !noRetrigger) {
            for (const std::uint8_t n : notes) {
                e->noteOff(n);
                e->noteOn(n, kT13Velocity);
            }
            nextRetrigger += retrigger;
        }
        if (pos >= nextToggle && !staticMorph) {
            position = 1.0f - position;
            e->setMorphPosition(position);
            nextToggle += toggle;
        }
        e->processBlock(l.data(), r.data(), kP1Block);
        acc += static_cast<double>(l[0]) + static_cast<double>(r[kP1Block - 1]);
        pos += kP1Block;
    };
    const auto warm = static_cast<std::size_t>(std::llround(kP1WarmupS * kT13Fs));
    while (pos < warm)
        step();
    const std::size_t start = pos;
    const std::size_t end = start + static_cast<std::size_t>(std::llround(kP1TimedS * kT13Fs));
    const auto t0 = std::chrono::steady_clock::now();
    while (pos < end)
        step();
    const auto t1 = std::chrono::steady_clock::now();
    sink += acc;
    const double wall = std::chrono::duration<double>(t1 - t0).count();
    const double audio = static_cast<double>(pos - start) / kT13Fs;
    return 100.0 * wall / audio;
}

// ---- the deviation-10 replica ----

enum class P1Stage : std::uint8_t {
    CoreExcitation, Shape, Fold, Character, Anchor, Pitch, A1Meter, AmpEnv, Dynamics, Field, DcLimiter, Control, Count
};
constexpr std::size_t kP1NumStages = static_cast<std::size_t>(P1Stage::Count);
constexpr std::array<const char*, kP1NumStages> kP1StageNames{
    "core+excitation", "shape",         "fold",          "character",             "anchor",     "pitch(drop+lock)",
    "a1+meter",        "amp-env",       "mass-dynamics", "field(stage+pan+wide)", "dc+limiter", "morph+law+motion+macros"};

struct P1Off {
    std::array<bool, kP1NumStages> off{};
    [[nodiscard]] bool operator()(P1Stage s) const noexcept { return off[static_cast<std::size_t>(s)]; }
};

[[nodiscard]] bool p1SameRecipe(const SpectralShapeRecipe::Controls& a,
                                const SpectralShapeRecipe::Controls& b) noexcept {
    return t13Same(a.depth, b.depth) && t13Same(a.body, b.body) && t13Same(a.bodyCurvature, b.bodyCurvature)
           && t13Same(a.bodyEmphasis, b.bodyEmphasis) && t13Same(a.edge, b.edge) && t13Same(a.shift, b.shift);
}

[[nodiscard]] bool p1SameField(const BassStereoField::Controls& a, const BassStereoField::Controls& b) noexcept {
    return a.mode == b.mode && t13Same(a.spread, b.spread) && t13Same(a.gravity, b.gravity)
           && t13Same(a.crossoverHz, b.crossoverHz) && t13Same(a.curve, b.curve)
           && t13Same(a.motionDepth, b.motionDepth) && t13Same(a.motionRate, b.motionRate)
           && a.motionSource == b.motionSource && a.cycleSync == b.cycleSync && a.cycleNote == b.cycleNote
           && a.cycleModifier == b.cycleModifier;
}

[[nodiscard]] bool p1SameBase(const MassLaw::Targets& a, const MassLaw::Targets& b) noexcept {
    return t13Same(a.depth, b.depth) && t13Same(a.body, b.body)
           && t13Same(a.transientDurationMs, b.transientDurationMs) && t13Same(a.foldAmount, b.foldAmount)
           && t13Same(a.compressionAmount, b.compressionAmount) && t13Same(a.anchorLevel, b.anchorLevel)
           && t13Same(a.outputTrimDb, b.outputTrimDb);
}

[[nodiscard]] bool p1NonZero(float x) noexcept {
    return (std::bit_cast<std::uint32_t>(x) & 0x7FFFFFFFu) != 0u;
}

/// The engine's lawBaseOf (profundum_engine.h, private): copy-then-assign.
[[nodiscard]] MassLaw::Targets p1LawBase(const MorphState& s) noexcept {
    MassLaw::Targets b{};
    b.depth = s.recipe.depth;
    b.body = s.recipe.body;
    b.foldAmount = s.foldAmount;
    b.anchorLevel = s.anchor.level;
    return b;
}

/// One voice of the replica: ProfundumVoice's components in the S5.4 / S5.6 order with change-only setters, every
/// note a Reset-branch restart (no Note Memory / declick bookkeeping: per-event costs, not per-sample ones).
class P1ReplicaVoice {
public:
    static constexpr std::size_t kN = ProfundumEngine::kControlInterval;

    void prepare(double fs, const MorphState& s) noexcept {
        shape_.setShape(s.shape);
        character_.setAlgorithm(s.characterAlgorithm);
        character_.setAmount(s.characterAmount);
        character_.setTone(s.characterTone);
        fold_.setAmount(s.foldAmount);
        fs_ = fs;
        const double fsCore = std::clamp(fs, ProfundumCore::kMinSampleRate, ProfundumCore::kMaxSampleRate);
        capHz_ = SpectralShapeRecipe::capFrequency(fsCore);
        core_.prepare(fs, ProfundumVoice::kNumPartials);
        shape_.prepare(fs, kN);
        fold_.prepare(fs, kN);
        character_.prepare(fs, kN);
        anchor_.prepare(fs);
        drop_.prepare(fs);
        lock_.prepare(fs);
        amp_.prepare(static_cast<float>(fs));
        amp_.setRetriggerMode(Krate::DSP::RetriggerMode::Hard);
        meter_.prepare(fsCore);
        core_.setControls(s.recipe);
        core_.setExcitation(s.excitation);
        anchor_.setControls(s.anchor);
        drop_.setAmount(s.dropAmountSt);
        drop_.setTime(s.dropTimeMs);
        drop_.setShape(s.dropShape);
        drop_.setRebound(s.dropRebound);
        amp_.setAttack(s.ampEnv.attackMs);
        amp_.setDecay(s.ampEnv.decayMs);
        amp_.setSustain(s.ampEnv.sustain);
        amp_.setRelease(s.ampEnv.releaseMs);
        amp_.setAttackCurve(s.ampEnv.attackCurve);
        amp_.setDecayCurve(s.ampEnv.decayCurve);
        amp_.setReleaseCurve(s.ampEnv.releaseCurve);
        lock_.setBypass(s.anchor.mode == AnchorOscillator::Mode::Follow);
        core_.reset();
        shape_.reset();
        fold_.reset();
        character_.reset();
        anchor_.reset();
        drop_.reset();
        lock_.reset();
        amp_.reset();
        fieldState_.reset();
        meter_.reset();
        feedFrom_ = s.anchorFoldFeed;
        feedTo_ = s.anchorFoldFeed;
        handed_ = false;
    }

    /// A Reset-branch note restart (S5.5 applyTransition with AlwaysRetrigger / Reset).
    void trigger(float fNote) noexcept {
        fNote_ = fNote;
        core_.setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
        anchor_.setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
        gridCount_ = 0;
        fieldState_.reset();
        syncPending_ = true;
        drop_.trigger(true);
        lockPending_ = true;
        coreOnPending_ = true;
        anchorOnPending_ = true;
        amp_.gate(true);
    }

    /// S5.4 (the pitch stage first, then the per-interval setters, change-only).
    void beginInterval(const MorphState& s, const SpectralShapeRecipe::Controls& cc, float foldAmount,
                       float anchorLevel, const BassStereoField& field, const P1Off& off) noexcept {
        const bool all = !handed_;
        fRes_.fill(fNote_);
        if (!off(P1Stage::Pitch)) {
            if (lockPending_) {
                lock_.trigger(drop_.plannedCurve(), fRes_[0], gridPhase(), ProfundumCore::kMinF0Hz, capHz_,
                              DropPhaseLock::TriggerKind::SnapReset);
                lockPending_ = false;
            }
            for (std::size_t i = 0; i < kN; ++i) {
                const float d = drop_.process();
                const float dc = lock_.process(d);
                fCore_[i] = fRes_[i] * std::exp2(dc / 12.0f);
            }
        } else {
            fCore_ = fRes_;
        }
        const bool follow = s.anchor.mode == AnchorOscillator::Mode::Follow;
        for (std::size_t i = 0; i < kN; ++i)
            fAnchor_[i] = follow ? fCore_[i] : fRes_[i];

        if (!off(P1Stage::Field)) {
            field.computePanVector(fieldState_, fCore_[0], pan_, ProfundumVoice::kNumPartials);
            bool changed = all;
            for (std::size_t k = 0; !changed && k < pan_.size(); ++k)
                changed = !t13Same(pan_[k], lastPan_[k]);
            if (changed) {
                core_.setPartialPanOffsets(pan_);
                lastPan_ = pan_;
            }
            const BassStereoField::ChannelOffsets w = field.wideOffsets(s.characterAlgorithm);
            if (all || !t13Same(w.amount, lastWide_.amount) || !t13Same(w.tone, lastWide_.tone)) {
                character_.setChannelOffsets(w.amount, w.tone);
                lastWide_ = w;
            }
        }
        if (all || !p1SameRecipe(cc, lastCc_)) {
            core_.setControls(cc);
            lastCc_ = cc;
        }
        {
            const ProfundumCore::ExcitationControls& e = s.excitation;
            if (all || !t13Same(e.mix, lastExc_.mix) || !t13Same(e.drive, lastExc_.drive)
                || !t13Same(e.asymmetry, lastExc_.asymmetry)) {
                core_.setExcitation(e);
                lastExc_ = e;
            }
        }
        if (all || !t13Same(foldAmount, lastFoldAmount_)) {
            fold_.setAmount(foldAmount);
            lastFoldAmount_ = foldAmount;
        }
        if (all || !t13Same(fCore_[0], lastFoldF0_)) {
            fold_.setFundamentalHz(fCore_[0]);
            lastFoldF0_ = fCore_[0];
        }
        if (all || !t13Same(s.shape, lastShape_)) {
            shape_.setShape(s.shape);
            lastShape_ = s.shape;
        }
        if (all || s.characterAlgorithm != lastAlg_) {
            character_.setAlgorithm(s.characterAlgorithm);
            lastAlg_ = s.characterAlgorithm;
        }
        if (all || !t13Same(s.characterAmount, lastAmount_)) {
            character_.setAmount(s.characterAmount);
            lastAmount_ = s.characterAmount;
        }
        if (all || !t13Same(s.characterTone, lastTone_)) {
            character_.setTone(s.characterTone);
            lastTone_ = s.characterTone;
        }
        {
            const bool hw = meter_.halfWaveSymmetric();
            if (all || hw != lastHalfWave_) {
                character_.setInputHalfWaveSymmetric(hw);
                lastHalfWave_ = hw;
            }
        }
        {
            const float r = character_.perceivedPitchRatio();
            if (all || !t13Same(r, lastRatio_)) {
                anchor_.setFollowRatio(r);
                lastRatio_ = r;
            }
        }
        {
            AnchorOscillator::Controls c = s.anchor;
            c.level = anchorLevel;
            const AnchorOscillator::Controls& p = lastAnchor_;
            if (all || c.waveform != p.waveform || c.mode != p.mode || !t13Same(c.level, p.level)
                || !t13Same(c.punchBoostDb, p.punchBoostDb) || !t13Same(c.punchTimeMs, p.punchTimeMs)
                || !t13Same(c.ghostTimeMs, p.ghostTimeMs)) {
                anchor_.setControls(c);
                lastAnchor_ = c;
            }
        }
        if (all || follow != lastBypass_) {
            lock_.setBypass(follow);
            lastBypass_ = follow;
        }
        if (!off(P1Stage::A1Meter)) {
            const float fA = fAnchor_[0];
            constexpr auto kP = static_cast<std::size_t>(ProfundumVoice::kNumPartials);
            const bool shapeChanged = !a1Valid_ || !p1SameRecipe(s.recipe, a1RecipeKey_);
            const bool maskChanged = !a1Valid_ || !t13Same(fA, a1FreqKey_);
            if (shapeChanged) {
                SpectralShapeRecipe::evaluateShape(s.recipe, std::span<float>(a1Shape_.data(), kP));
                a1RecipeKey_ = s.recipe;
            }
            if (maskChanged) {
                SpectralShapeRecipe::evaluateMask(fA, fs_, std::span<float>(a1Mask_.data(), kP));
                a1FreqKey_ = fA;
            }
            if (shapeChanged || maskChanged) {
                SpectralShapeRecipe::applyMask(std::span<const float>(a1Shape_.data(), kP),
                                               std::span<const float>(a1Mask_.data(), kP),
                                               std::span<float>(a1Out_.data(), kP));
                a1_ = a1Out_[0] * SpectralShapeRecipe::kCenterPanGain;
            }
            a1Valid_ = true;
            if (all || !t13Same(a1_, lastA1_)) {
                anchor_.setCoreH1Amplitude(a1_);
                lastA1_ = a1_;
            }
        }
        if (all || !t13Same(s.dropAmountSt, lastDrop_[0])) {
            drop_.setAmount(s.dropAmountSt);
            lastDrop_[0] = s.dropAmountSt;
        }
        if (all || !t13Same(s.dropTimeMs, lastDrop_[1])) {
            drop_.setTime(s.dropTimeMs);
            lastDrop_[1] = s.dropTimeMs;
        }
        if (all || !t13Same(s.dropShape, lastDrop_[2])) {
            drop_.setShape(s.dropShape);
            lastDrop_[2] = s.dropShape;
        }
        if (all || !t13Same(s.dropRebound, lastDrop_[3])) {
            drop_.setRebound(s.dropRebound);
            lastDrop_[3] = s.dropRebound;
        }
        {
            const MorphState::AmpEnvelopeFields& e = s.ampEnv;
            MorphState::AmpEnvelopeFields& p = lastEnv_;
            if (all || !t13Same(e.attackMs, p.attackMs)) {
                amp_.setAttack(e.attackMs);
                p.attackMs = e.attackMs;
            }
            if (all || !t13Same(e.decayMs, p.decayMs)) {
                amp_.setDecay(e.decayMs);
                p.decayMs = e.decayMs;
            }
            if (all || !t13Same(e.sustain, p.sustain)) {
                amp_.setSustain(e.sustain);
                p.sustain = e.sustain;
            }
            if (all || !t13Same(e.releaseMs, p.releaseMs)) {
                amp_.setRelease(e.releaseMs);
                p.releaseMs = e.releaseMs;
            }
            if (all || !t13Same(e.attackCurve, p.attackCurve)) {
                amp_.setAttackCurve(e.attackCurve);
                p.attackCurve = e.attackCurve;
            }
            if (all || !t13Same(e.decayCurve, p.decayCurve)) {
                amp_.setDecayCurve(e.decayCurve);
                p.decayCurve = e.decayCurve;
            }
            if (all || !t13Same(e.releaseCurve, p.releaseCurve)) {
                amp_.setReleaseCurve(e.releaseCurve);
                p.releaseCurve = e.releaseCurve;
            }
        }
        feedFrom_ = feedTo_;
        feedTo_ = s.anchorFoldFeed;
        feedSkip_ = std::bit_cast<std::uint32_t>(feedFrom_) == 0u && std::bit_cast<std::uint32_t>(feedTo_) == 0u;
        handed_ = true;
    }

    /// S5.6 for one whole interval, adding (c * g_amp) + (a * g_amp) into the sums (g_d == 1: no dip here).
    void render(float* sumL, float* sumR, const P1Off& off) noexcept {
        constexpr std::size_t m = kN;
        const bool noteOnNow = coreOnPending_;
        if (syncPending_) {
            character_.syncPhase(0.0f);
            syncPending_ = false;
        }
        if (coreOnPending_) {
            core_.noteOn(fCore_[0]);
            coreOnPending_ = false;
        }
        if (anchorOnPending_) {
            anchor_.noteOn(fAnchor_[0], true);
            anchorOnPending_ = false;
        }
        if (!off(P1Stage::CoreExcitation)) {
            core_.processBlock(coreL_.data(), coreR_.data(), m, fCore_.data());
        } else {
            coreL_.fill(0.0f);
            coreR_.fill(0.0f);
        }
        const std::uint32_t gridAtStart = gridCount_;
        gridCount_ += static_cast<std::uint32_t>(m);
        xL_ = coreL_;
        xR_ = coreR_;
        if (!off(P1Stage::Shape))
            shape_.processBlock(xL_.data(), xR_.data(), m, fCore_.data());
        if (!off(P1Stage::Anchor)) {
            anchor_.processBlock(aL_.data(), aR_.data(), m, fAnchor_.data());
        } else {
            aL_.fill(0.0f);
            aR_.fill(0.0f);
        }
        if (!feedSkip_) {
            for (std::size_t k = 0; k < m; ++k) {
                const float g = feedFrom_ + (feedTo_ - feedFrom_) * static_cast<float>(k + 1) / 32.0f;
                xL_[k] += g * aL_[k];
                xR_[k] += g * aR_[k];
            }
        }
        if (!off(P1Stage::Fold))
            fold_.processBlock(xL_.data(), xR_.data(), m);
        if (!off(P1Stage::A1Meter)) {
            constexpr auto U = static_cast<std::uint32_t>(ProfundumCore::kPitchUpdateInterval);
            for (std::size_t i = 0; i < m; ++i) {
                if ((gridAtStart + static_cast<std::uint32_t>(i)) % U == 0u || (i == 0 && noteOnNow))
                    meter_.setFrequency(std::clamp(fCore_[i], ProfundumCore::kMinF0Hz, capHz_));
                meter_.process(0.5f * (xL_[i] + xR_[i]));
            }
        }
        if (!off(P1Stage::Character))
            character_.processBlock(xL_.data(), xR_.data(), m, fCore_.data());
        const bool amp = !off(P1Stage::AmpEnv);
        for (std::size_t i = 0; i < m; ++i) {
            const float ga = amp ? amp_.process() : 1.0f;
            sumL[i] += (xL_[i] * ga) + (aL_[i] * ga);
            sumR[i] += (xR_[i] * ga) + (aR_[i] * ga);
        }
        lastFCore_ = fCore_[m - 1];
    }

    [[nodiscard]] float lastCoreF0() const noexcept { return lastFCore_; }

private:
    [[nodiscard]] std::size_t gridPhase() const noexcept {
        constexpr std::size_t U = ProfundumCore::kPitchUpdateInterval;
        return (U - static_cast<std::size_t>(gridCount_) % U) % U;
    }

    ProfundumCore core_;
    ShapeMorph shape_;
    HarmonicFold fold_;
    BassCharacterStage character_;
    AnchorOscillator anchor_;
    DropEnvelope drop_;
    DropPhaseLock lock_;
    ADSREnvelope amp_;
    BassStereoField::VoiceState fieldState_{};
    ProfundumVoice::HarmonicBalanceMeter meter_{};

    double fs_ = kT13Fs;
    float capHz_ = 0.0f;
    float fNote_ = 55.0f;
    float lastFCore_ = 0.0f;
    std::uint32_t gridCount_ = 0;
    bool syncPending_ = false;
    bool lockPending_ = false;
    bool coreOnPending_ = false;
    bool anchorOnPending_ = false;
    float feedFrom_ = 0.0f;
    float feedTo_ = 0.0f;
    bool feedSkip_ = true;

    std::array<float, kN> fRes_{}, fCore_{}, fAnchor_{}, coreL_{}, coreR_{}, xL_{}, xR_{}, aL_{}, aR_{};
    std::array<float, Krate::DSP::kMaxPartials> pan_{}, lastPan_{};
    std::array<float, static_cast<std::size_t>(ProfundumVoice::kNumPartials)> a1Shape_{}, a1Mask_{}, a1Out_{};
    SpectralShapeRecipe::Controls a1RecipeKey_{};
    float a1FreqKey_ = 0.0f;
    float a1_ = 0.0f;
    bool a1Valid_ = false;

    bool handed_ = false;
    BassStereoField::ChannelOffsets lastWide_{};
    SpectralShapeRecipe::Controls lastCc_{};
    ProfundumCore::ExcitationControls lastExc_{};
    float lastFoldAmount_ = 0.0f;
    float lastFoldF0_ = 0.0f;
    float lastShape_ = 0.0f;
    float lastAmount_ = 0.0f;
    float lastTone_ = 0.0f;
    BassCharacterStage::Algorithm lastAlg_ = BassCharacterStage::Algorithm::Tube;
    bool lastHalfWave_ = false;
    float lastRatio_ = 1.0f;
    AnchorOscillator::Controls lastAnchor_{};
    bool lastBypass_ = false;
    float lastA1_ = 0.0f;
    std::array<float, 4> lastDrop_{};
    MorphState::AmpEnvelopeFields lastEnv_{};
};

/// The replica engine: S6.5's grid step (control layer, field, per-voice intervals, MassDynamics setters) and
/// renderChunk's global chain, on whole 32-sample intervals (host block 64 = two intervals).
class P1Replica {
public:
    static constexpr std::size_t kN = ProfundumEngine::kControlInterval;

    void prepare(const MorphState& a, const MorphState& b, const std::vector<std::uint8_t>& notes,
                 const P1Off& off) {
        off_ = off;
        const double fs = kT13Fs;
        REQUIRE(morph_.setEndpoint(BassMorphEngine::Endpoint::A, a));
        REQUIRE(morph_.setEndpoint(BassMorphEngine::Endpoint::B, b));
        morph_.setPosition(0.0f);
        morph_.setPositionRampMs(kP1MorphRampMs);
        morph_.prepare(fs);
        state_ = morph_.resolved().state;
        macros_.apply(state_);
        motion_.setSeed(deriveStreamSeed(ProfundumEngine::kDefaultSeed, ProfundumEngine::kMotionDriftSalt));
        motion_.prepare(fs);
        motion_.setTempoBpm(ProfundumEngine::kDefaultTempoBpm);
        field_.setDriftSeed(deriveStreamSeed(ProfundumEngine::kDefaultSeed, ProfundumEngine::kFieldDriftSalt));
        field_.setControls(state_.field);
        field_.prepare(fs, kN);
        field_.setTempoBpm(ProfundumEngine::kDefaultTempoBpm);
        law_.setBase(p1LawBase(state_));
        law_.setMass(state_.mass);
        law_.prepare(fs);
        targets_ = law_.currentTargets();
        anchorLevel_ = state_.anchor.level;
        dyn_.setOversampling(true);
        dyn_.setTransientDurationMs(targets_.transientDurationMs);
        dyn_.setCompressionAmount(targets_.compressionAmount);
        dyn_.setOutputTrimDb(targets_.outputTrimDb);
        dyn_.setTransientAttackDb(state_.transientAttackDb);
        dyn_.setTransientSustainDb(state_.transientSustainDb);
        dyn_.setDetectorKeyHz(0.0f);
        dyn_.prepare(fs, kN);
        dcL_.prepare(fs, ProfundumEngine::kDcCutoffHz);
        dcR_.prepare(fs, ProfundumEngine::kDcCutoffHz);
        limiter_.setCeilingDb(ProfundumEngine::kDefaultCeilingDb);
        limiter_.prepare(fs, kN);
        notes_ = notes;
        voices_.clear();
        for (std::size_t v = 0; v < notes.size(); ++v) {
            voices_.push_back(std::make_unique<P1ReplicaVoice>());
            voices_.back()->prepare(fs, state_);
        }
        retriggerAll();
    }

    void retriggerAll() noexcept {
        for (std::size_t v = 0; v < voices_.size(); ++v)
            voices_[v]->trigger(t13NoteHz(notes_[v]));
        restart_ = true;
    }

    void setMorphPosition(float p) noexcept { morph_.setPosition(p); }

    void processBlock(float* left, float* right, std::size_t n) noexcept {
        for (std::size_t i = 0; i + kN <= n; i += kN) {
            gridStep();
            renderInterval(left + i, right + i);
        }
    }

private:
    void gridStep() noexcept {
        const bool all = !handed_;
        if (!off_(P1Stage::Control)) {
            const BassMorphEngine::Resolved& r = morph_.advanceInterval();
            state_ = r.state;
            macros_.apply(state_);
            cTL_ = macros_.compressionContribution();
            motion_.setConfig(state_.motion);
            if (restart_)
                motion_.noteRestart();
            const ProfundumMotion::Offsets& o = motion_.advanceInterval();
            if (p1NonZero(o.depth))
                state_.recipe.depth = std::clamp(state_.recipe.depth + o.depth, 0.0f, 1.0f);
            if (p1NonZero(o.body))
                state_.recipe.body = std::clamp(state_.recipe.body + o.body, 0.0f, 1.0f);
            if (p1NonZero(o.edge))
                state_.recipe.edge = std::clamp(state_.recipe.edge + o.edge, 0.0f, 1.0f);
            if (p1NonZero(o.shift))
                state_.recipe.shift = std::clamp(state_.recipe.shift + o.shift, -1.0f, 1.0f);
            const float massIn = p1NonZero(o.mass) ? state_.mass + o.mass : state_.mass;
            const MassLaw::Targets base = p1LawBase(state_);
            if (all || !p1SameBase(base, lastBase_)) {
                law_.setBase(base);
                lastBase_ = base;
            }
            if (all || !t13Same(massIn, lastMassIn_)) {
                law_.setMass(massIn);
                lastMassIn_ = massIn;
            }
            static_cast<void>(law_.advanceInterval());
            targets_ = law_.currentTargets();
            const bool lock = morph_.anchorMorphMode() == BassMorphEngine::AnchorMorphMode::Lock;
            const float mA = (!lock && p1NonZero(o.mass)) ? r.anchorMass + o.mass : r.anchorMass;
            anchorLevel_ = MassLaw::evaluateRow(
                MassLaw::kRows[static_cast<std::size_t>(MassLaw::Target::AnchorLevel)], mA, law_.base().anchorLevel);
        }
        restart_ = false;
        if (!off_(P1Stage::Field)) {
            if (all || !p1SameField(state_.field, lastField_)) {
                field_.setControls(state_.field);
                lastField_ = state_.field;
            }
            field_.advanceControl();
            if (field_.sideIdle()) {
                if (!dcSynced_) {
                    dcR_ = dcL_;
                    dcSynced_ = true;
                }
            } else {
                dcSynced_ = false;
            }
        }
        SpectralShapeRecipe::Controls cc = state_.recipe;
        cc.depth = targets_.depth;
        cc.body = targets_.body;
        float keyHz = 0.0f;
        for (const std::unique_ptr<P1ReplicaVoice>& v : voices_) {
            v->beginInterval(state_, cc, targets_.foldAmount, anchorLevel_, field_, off_);
            const float f = v->lastCoreF0();
            if (f > 0.0f && (keyHz == 0.0f || f < keyHz))
                keyHz = f;
        }
        if (!off_(P1Stage::Dynamics)) {
            const float compression = p1NonZero(cTL_) ? std::clamp(targets_.compressionAmount + cTL_, 0.0f, 1.0f)
                                                      : targets_.compressionAmount;
            if (all || !t13Same(targets_.transientDurationMs, lastDyn_[0])) {
                lastDyn_[0] = targets_.transientDurationMs;
                dyn_.setTransientDurationMs(lastDyn_[0]);
            }
            if (all || !t13Same(compression, lastDyn_[1])) {
                lastDyn_[1] = compression;
                dyn_.setCompressionAmount(lastDyn_[1]);
            }
            if (all || !t13Same(targets_.outputTrimDb, lastDyn_[2])) {
                lastDyn_[2] = targets_.outputTrimDb;
                dyn_.setOutputTrimDb(lastDyn_[2]);
            }
            if (all || !t13Same(state_.transientAttackDb, lastDyn_[3])) {
                lastDyn_[3] = state_.transientAttackDb;
                dyn_.setTransientAttackDb(lastDyn_[3]);
            }
            if (all || !t13Same(state_.transientSustainDb, lastDyn_[4])) {
                lastDyn_[4] = state_.transientSustainDb;
                dyn_.setTransientSustainDb(lastDyn_[4]);
            }
            if (all || !t13Same(keyHz, lastDyn_[5])) {
                lastDyn_[5] = keyHz;
                dyn_.setDetectorKeyHz(lastDyn_[5]);
            }
        }
        handed_ = true;
    }

    void renderInterval(float* left, float* right) noexcept {
        sumL_.fill(-0.0f);
        sumR_.fill(-0.0f);
        for (const std::unique_ptr<P1ReplicaVoice>& v : voices_)
            v->render(sumL_.data(), sumR_.data(), off_);
        if (!off_(P1Stage::Dynamics))
            dyn_.processBlock(sumL_.data(), sumR_.data(), kN);
        if (!off_(P1Stage::Field))
            field_.processBlock(sumL_.data(), sumR_.data(), kN);
        if (!off_(P1Stage::DcLimiter)) {
            dcL_.processBlock(sumL_.data(), kN);
            dcR_.processBlock(sumR_.data(), kN);
            limiter_.processBlock(sumL_.data(), sumR_.data(), static_cast<int>(kN));
        }
        std::copy_n(sumL_.data(), kN, left);
        std::copy_n(sumR_.data(), kN, right);
    }

    P1Off off_{};
    BassMorphEngine morph_;
    ProfundumMacroMatrix macros_;
    ProfundumMotion motion_;
    MassLaw law_;
    MassDynamics dyn_;
    BassStereoField field_;
    DCBlocker dcL_;
    DCBlocker dcR_;
    TruePeakLimiter limiter_;
    std::vector<std::unique_ptr<P1ReplicaVoice>> voices_;
    std::vector<std::uint8_t> notes_;
    MorphState state_{};
    MassLaw::Targets targets_{};
    MassLaw::Targets lastBase_{};
    BassStereoField::Controls lastField_{};
    float anchorLevel_ = 0.0f;
    float cTL_ = 0.0f;
    float lastMassIn_ = 0.0f;
    std::array<float, 6> lastDyn_{};
    bool handed_ = false;
    bool restart_ = false;
    bool dcSynced_ = false;
    std::array<float, kN> sumL_{}, sumR_{};
};

/// One timed trial of the replica (same schedule and protocol as p1EngineTrial). @return % of one core.
[[nodiscard]] double p1ReplicaTrial(BassCharacterStage::Algorithm alg, std::size_t voices, const P1Off& off,
                                    double& sink) {
    const std::pair<MorphState, MorphState> ends = p1Endpoints(alg);
    auto rep = std::make_unique<P1Replica>();
    rep->prepare(ends.first, ends.second, p1Notes(voices), off);
    std::array<float, kP1Block> l{};
    std::array<float, kP1Block> r{};
    const auto retrigger = static_cast<std::size_t>(std::llround(kP1RetriggerS * kT13Fs));
    const auto toggle = static_cast<std::size_t>(std::llround(kP1MorphToggleS * kT13Fs));
    std::size_t nextRetrigger = retrigger;
    std::size_t nextToggle = toggle;
    const std::string variant = p1Variant();
    const bool staticMorph = variant == "static-morph" || variant == "static-all" || variant == "static-breakdown";
    const bool noRetrigger = variant == "static-all";
    float position = 0.0f;
    if (staticMorph)
        rep->setMorphPosition(0.5f);
    std::size_t pos = 0;
    double acc = 0.0;
    const auto step = [&]() noexcept {
        if (pos >= nextRetrigger && !noRetrigger) {
            rep->retriggerAll();
            nextRetrigger += retrigger;
        }
        if (pos >= nextToggle && !staticMorph) {
            position = 1.0f - position;
            rep->setMorphPosition(position);
            nextToggle += toggle;
        }
        rep->processBlock(l.data(), r.data(), kP1Block);
        acc += static_cast<double>(l[0]) + static_cast<double>(r[kP1Block - 1]);
        pos += kP1Block;
    };
    const auto warm = static_cast<std::size_t>(std::llround(kP1WarmupS * kT13Fs));
    while (pos < warm)
        step();
    const std::size_t start = pos;
    const std::size_t end = start + static_cast<std::size_t>(std::llround(kP1TimedS * kT13Fs));
    const auto t0 = std::chrono::steady_clock::now();
    while (pos < end)
        step();
    const auto t1 = std::chrono::steady_clock::now();
    sink += acc;
    const double wall = std::chrono::duration<double>(t1 - t0).count();
    return 100.0 * wall / (static_cast<double>(pos - start) / kT13Fs);
}

template <typename Trial>
[[nodiscard]] double p1MedianOf(const Trial& trial, std::ostringstream& log, const std::string& label) {
    std::vector<double> v;
    for (std::size_t i = 0; i < kP1Trials; ++i)
        v.push_back(trial());
    log << "#   " << label << " trials:";
    for (const double x : v)
        log << " " << x;
    const double m = t13Median(v);
    log << " median " << m << "\n";
    return m;
}

}  // namespace

// P-1 (plan R-A, S10.4; tasks.md T013, deviation 10). Hidden; run ALONE and pinned by evidence/close/probe.sh p1.
// A RED verdict is STOP AND SURFACE to the user with the breakdown (R-A stop rule; no ceiling is relaxed).
TEST_CASE("VoiceEngine_CpuProbe", "[systems][profundum][voice-engine][.probe][.perf]") {
    enableFTZDAZ();
    std::ostringstream table;
    table << std::setprecision(5);
    table << "# P-1 VoiceEngine_CpuProbe (tasks.md T013; plan R-A, S10.4, deviation 10)\n"
          << "# SC-001 setting on both showcase endpoints, morph ramp " << kP1MorphRampMs << " ms toggled every "
          << kP1MorphToggleS << " s, Drop 24 st retriggered every " << kP1RetriggerS << " s, block " << kP1Block
          << ", " << kP1TimedS << " s timed after " << kP1WarmupS << " s warm-up, median of " << kP1Trials
          << " fresh instances, % of one core = wall / audio x 100. No observer.\n";
    double sink = 0.0;

    if (!p1Variant().empty()) {   // diagnostic variant: engine trials only, no breakdown, no verdict
        const std::string v = p1Variant();
        const char* setting = std::getenv("P1_SETTING");
        table << "# P1 VARIANT " << v << " SETTING " << (setting != nullptr ? setting : "-") << "\n";
        for (std::size_t ai = 0; ai < kP1Algorithms.size(); ++ai) {
            const BassCharacterStage::Algorithm alg = kP1Algorithms[ai];
            const std::string an = t13AlgName(alg);
            const double m = p1MedianOf([&]() { return p1EngineTrial(alg, 1, ProfundumNonlinearPlacement::PerVoice, sink); },
                                        table, std::string("engine MONO ").append(an).append(" ").append(v));
            const double p = p1MedianOf([&]() { return p1EngineTrial(alg, 4, ProfundumNonlinearPlacement::PerVoice, sink); },
                                        table, std::string("engine POLY4 ").append(an).append(" ").append(v));
            table << "P1V " << v << " " << an << " MONO " << m << " POLY4 " << p << "\n";
        }
        if (v == "static-breakdown") {   // the replica breakdown with the morph held (Tube only)
            table << "# breakdown (static morph): test-side replica, whole and with one stage removed\n";
            for (const std::size_t voices : {std::size_t{1}, std::size_t{4}}) {
                const std::string vn = voices == 1 ? "MONO" : "POLY4";
                const BassCharacterStage::Algorithm alg = kP1Algorithms[0];
                const std::string an = t13AlgName(alg);
                const P1Off none{};
                const double full = p1MedianOf([&]() { return p1ReplicaTrial(alg, voices, none, sink); }, table,
                                               std::string("replica ").append(vn).append(" ").append(an).append(" full ").append(v));
                table << "P1V BREAKDOWN " << vn << " " << an << " replica_full=" << full << "\n";
                for (std::size_t si = 0; si < kP1NumStages; ++si) {
                    P1Off off{};
                    off.off[si] = true;
                    const double without = p1MedianOf([&]() { return p1ReplicaTrial(alg, voices, off, sink); }, table,
                                                      std::string("replica ").append(vn).append(" ").append(an).append(" without ").append(kP1StageNames[si]).append(" ").append(v));
                    table << "P1V STAGE " << vn << " " << an << " " << kP1StageNames[si] << " without=" << without
                          << " cost=" << (full - without) << " per_voice=" << (full - without) / static_cast<double>(voices) << "\n";
                }
            }
        }
        table << "P1V DONE " << v << "\n";
        std::cout << table.str();
        t13WriteEvidence("probes/p1-cpu-" + v + ".txt", table.str());
        CHECK(t13Finite(sink));
        return;
    }

    // ---- the real engine ----
    std::array<double, 2> mono{};
    std::array<double, 2> poly4{};
    std::array<double, 2> post4{};
    for (std::size_t ai = 0; ai < kP1Algorithms.size(); ++ai) {
        const BassCharacterStage::Algorithm alg = kP1Algorithms[ai];
        const std::string an = t13AlgName(alg);
        mono[ai] = p1MedianOf([&]() { return p1EngineTrial(alg, 1, ProfundumNonlinearPlacement::PerVoice, sink); },
                              table, "engine MONO " + an);
        poly4[ai] = p1MedianOf([&]() { return p1EngineTrial(alg, 4, ProfundumNonlinearPlacement::PerVoice, sink); },
                               table, "engine POLY4 " + an);
        post4[ai] = p1MedianOf([&]() { return p1EngineTrial(alg, 4, ProfundumNonlinearPlacement::PostSum, sink); },
                               table, "engine POSTSUM4 " + an);
    }
    table << "P1 MONO " << mono[0] << " " << mono[1] << "\n";
    table << "P1 POLY4 " << poly4[0] << " " << poly4[1] << "\n";
    table << "P1 POSTSUM4 " << post4[0] << " " << post4[1] << "\n";

    // ---- the replica breakdown (deviation 10) ----
    table << "# breakdown: test-side replica, whole and with one stage removed; cost = full - without (% of one "
             "core); per_voice = cost / voices\n";
    for (const std::size_t voices : {std::size_t{1}, std::size_t{4}}) {
        const std::string vn = voices == 1 ? "MONO" : "POLY4";
        for (std::size_t ai = 0; ai < kP1Algorithms.size(); ++ai) {
            const BassCharacterStage::Algorithm alg = kP1Algorithms[ai];
            const std::string an = t13AlgName(alg);
            const P1Off none{};
            const double full = p1MedianOf([&]() { return p1ReplicaTrial(alg, voices, none, sink); }, table,
                                           std::string("replica ").append(vn).append(" ").append(an).append(" full"));
            const double engine = voices == 1 ? mono[ai] : poly4[ai];
            table << "P1 BREAKDOWN " << vn << " " << an << " replica_full=" << full << " engine=" << engine << "\n";
            for (std::size_t si = 0; si < kP1NumStages; ++si) {
                P1Off off{};
                off.off[si] = true;
                const double without = p1MedianOf([&]() { return p1ReplicaTrial(alg, voices, off, sink); }, table,
                                                  std::string("replica ").append(vn).append(" ").append(an).append(" without ").append(kP1StageNames[si]));
                const double cost = full - without;
                table << "P1 STAGE " << vn << " " << an << " " << kP1StageNames[si] << " without=" << without
                      << " cost=" << cost << " per_voice=" << cost / static_cast<double>(voices) << "\n";
            }
        }
    }

    const double maxMono = std::max(mono[0], mono[1]);
    const double maxPoly = std::max(poly4[0], poly4[1]);
    const double maxPost = std::max(post4[0], post4[1]);
    const bool green = maxMono <= kP1MonoBar && (maxPoly <= kP1PolyBar || maxPost <= kP1PolyBar);
    table << "# checksum " << sink << " (keeps the renders observable)\n";
    table << "P1 VERDICT " << (green ? "GREEN" : "RED") << " mono_max=" << maxMono << " (bar " << kP1MonoBar
          << ") poly4_max=" << maxPoly << " postsum4_max=" << maxPost << " (bar " << kP1PolyBar << ")\n";
    std::cout << table.str();
    t13WriteEvidence("probes/p1-cpu.txt", table.str());
    CHECK(t13Finite(sink));
    CHECK(t13Finite(maxMono));
    CHECK(t13Finite(maxPoly));
}

// -----------------------------------------------------------------------------
// P-2 (R-B): SC-005(a) / SC-006(a) on the worst cells, with the (b) margin
// -----------------------------------------------------------------------------

namespace {

constexpr double kP2RatioBar = 1.0592537251772889;   // 10^(0.5 / 20) (SC-005(a))
constexpr double kP2StealAtS = 0.5;
constexpr double kP2TailS = 0.3;
constexpr double kP2SteadyFromS = 0.5;
constexpr double kP2SteadyToS = 1.0;
constexpr std::array<int, 3> kP2FillerOffsetsSt{5, 7, 10};   // the three held notes besides the victim
constexpr std::array<double, 3> kP2FillerOnS{0.05, 0.10, 0.15};
constexpr int kP2StealOffsetSt = 3;                          // the fifth note, above the victim
constexpr std::array<int, 3> kP2IntervalsSt{1, 12, 24};
constexpr int kP2LowestNote = kT13NoteE0;
constexpr int kP2HighestNote = 127;

using P2Kind = t13Pt::VoiceEngineEventKind;

[[nodiscard]] t13Pt::VoiceEngineEvent p2On(std::uint64_t at, std::uint8_t note) {
    return t13Pt::VoiceEngineEvent{.sample = at, .kind = P2Kind::NoteOn, .note = note};
}

[[nodiscard]] t13Pt::VoiceEngineEvent p2Setting(P2Kind kind, int index) {
    return t13Pt::VoiceEngineEvent{.sample = 0, .kind = kind, .index = index};
}

/// A_s (SOLID, Drop 0), AlwaysRetrigger, Retrigger Reset, host block 1.
[[nodiscard]] t13Pt::VoiceEngineScript p2Script(ProfundumEngine::VoiceMode mode, std::size_t polyphony,
                                                std::uint64_t total, std::size_t tapped) {
    t13Pt::VoiceEngineScript sc;
    sc.fs = kT13Fs;
    sc.hostBlock = 1;
    sc.totalSamples = total;
    sc.a = t13StateAs();
    sc.b = sc.a;
    sc.mode = mode;
    sc.polyphony = polyphony;
    sc.tappedVoices = tapped;
    sc.events = {p2Setting(P2Kind::NoteMemoryMode, static_cast<int>(NoteMemory::Mode::AlwaysRetrigger)),
                 p2Setting(P2Kind::NoteRetrigger, static_cast<int>(NoteMemory::Retrigger::Reset))};
    return sc;
}

/// S9.9: the steady unit-gain reference of one note (a separate MONO render, never the transition trace).
[[nodiscard]] t13Pt::VoiceEngineSteady p2Steady(std::uint8_t note, std::map<int, t13Pt::VoiceEngineSteady>& cache) {
    const auto it = cache.find(static_cast<int>(note));
    if (it != cache.end())
        return it->second;
    t13Pt::VoiceEngineScript sc = p2Script(ProfundumEngine::VoiceMode::Mono, 1, t13At(kP2SteadyToS), 1);
    sc.hostBlock = 64;
    sc.events.push_back(p2On(0, note));
    const t13Pt::VoiceEngineTaps t = t13Pt::voiceEngineRender(sc);
    const t13Pt::VoiceEngineSteady s =
        t13Pt::voiceEngineUnitGainSteady(t.voices[0], static_cast<std::size_t>(t13At(kP2SteadyFromS)), t.total);
    cache.emplace(static_cast<int>(note), s);
    return s;
}

/// W (SC-005): the grid point of the steal / restart to the dip's end (samplesUntilSwitch + L_d), plus one period
/// of the slower f0.
[[nodiscard]] t13Pt::VoiceEngineWindow p2Window(std::uint64_t at, double fOld, double fNew) {
    RetriggerDeclick dk;
    dk.prepare(kT13Fs, ProfundumVoice::kDeclickMs);   // R-B
    dk.arm();
    const std::size_t fadeEnd =
        static_cast<std::size_t>(at) + dk.samplesUntilSwitch() + t13Pt::voiceEngineDeclickLength(kT13Fs);
    return t13Pt::voiceEngineWindow(static_cast<std::size_t>(at), fadeEnd, fOld, fNew, kT13Fs);
}

/// Samples from the restart's grid point to the switch sample, for the voice's dip length (R-B): the reference's new
/// note is issued there (reading refinement, 2026-10-20). Events land on the next 32-sample grid point (plan C-9), so
/// the value is rounded up to the grid.
[[nodiscard]] std::uint64_t p2SwitchDelay() {
    RetriggerDeclick dk;
    dk.prepare(kT13Fs, ProfundumVoice::kDeclickMs);
    dk.arm();
    const std::uint64_t d = dk.samplesUntilSwitch();
    constexpr std::uint64_t grid = ProfundumEngine::kControlInterval;
    return (d + grid - 1) / grid * grid;
}

[[nodiscard]] double p2MaxDelta(const t13Pt::VoiceEngineTaps& t, t13Pt::VoiceEngineWindow w) {
    return std::max(t13Pt::voiceEngineMaxDelta(std::span<const float>(t.sumL), w),
                    t13Pt::voiceEngineMaxDelta(std::span<const float>(t.sumR), w));
}

/// SC-005(a) on the pre-dynamics sum, the worse channel.
[[nodiscard]] double p2Ratio(const t13Pt::VoiceEngineTaps& y, const t13Pt::VoiceEngineTaps& ref,
                             t13Pt::VoiceEngineWindow w) {
    return std::max(t13Pt::voiceEngineDeltaRatio(std::span<const float>(y.sumL), std::span<const float>(ref.sumL), w),
                    t13Pt::voiceEngineDeltaRatio(std::span<const float>(y.sumR), std::span<const float>(ref.sumR), w));
}

struct P2Reading {
    std::string id;
    double ratio = 0.0;
    double maxDelta = 0.0;
    double bound = 0.0;
    std::size_t wBegin = 0;
    std::size_t wEnd = 0;
    [[nodiscard]] bool aPass() const noexcept { return ratio <= kP2RatioBar; }
    [[nodiscard]] bool bPass() const noexcept { return maxDelta <= bound; }
};

/// SC-005 worst cell: 5 notes into 4 voices, POLY, the victim (the oldest, held) is `victim`; the reference is the
/// same engine, seed and events at polyphony 5. (b) on the sum: the S9.9 bound over the victim and the new note,
/// plus 1.1 G_max D_max of each held filler (the triangle inequality over the voices sounding in W).
[[nodiscard]] P2Reading p2StealCell(std::uint8_t victim, std::map<int, t13Pt::VoiceEngineSteady>& cache) {
    const auto newNote = static_cast<std::uint8_t>(static_cast<int>(victim) + kP2StealOffsetSt);
    const std::uint64_t tS = t13At(kP2StealAtS);
    const std::uint64_t total = tS + t13At(kP2TailS);
    t13Pt::VoiceEngineScript sc = p2Script(ProfundumEngine::VoiceMode::Poly, 4, total, 0);
    sc.events.push_back(p2On(0, victim));
    std::vector<std::uint8_t> fillers;
    for (std::size_t i = 0; i < kP2FillerOffsetsSt.size(); ++i) {
        const auto f = static_cast<std::uint8_t>(static_cast<int>(victim) + kP2FillerOffsetsSt[i]);
        fillers.push_back(f);
        sc.events.push_back(p2On(t13At(kP2FillerOnS[i]), f));
    }
    sc.events.push_back(p2On(tS, newNote));
    t13Pt::VoiceEngineScript ref = sc;
    ref.polyphony = 5;
    // Reading refinement (main loop, 2026-10-20): the reference's new note starts where the stolen voice actually
    // starts it, the switch sample, so both sums carry the new note at the same phase and (a) isolates the dip.
    ref.events.back() = p2On(tS + p2SwitchDelay(), newNote);
    const t13Pt::VoiceEngineTaps y = t13Pt::voiceEngineRender(sc);
    const t13Pt::VoiceEngineTaps r = t13Pt::voiceEngineRender(ref);
    CHECK(y.droppedEvents == 0u);
    CHECK(r.droppedEvents == 0u);

    const t13Pt::VoiceEngineWindow w =
        p2Window(tS, static_cast<double>(t13NoteHz(victim)), static_cast<double>(t13NoteHz(newNote)));
    P2Reading out;
    out.id = "SC005 steal " + t13NoteName(victim) + " victim held, new +" + std::to_string(kP2StealOffsetSt) + " st";
    out.ratio = p2Ratio(y, r, w);
    out.maxDelta = p2MaxDelta(y, w);
    const std::array<t13Pt::VoiceEngineSteady, 2> involved{p2Steady(victim, cache), p2Steady(newNote, cache)};
    double fillerD = 0.0;
    for (const std::uint8_t f : fillers)
        fillerD += p2Steady(f, cache).dMax;
    out.bound = t13Pt::voiceEngineDeltaBound(involved, t13Pt::voiceEngineDeclickSlope(kT13Fs))
                + t13Pt::kVoiceEngineBoundFactor * fillerD;
    out.wBegin = w.begin;
    out.wEnd = w.end;
    return out;
}

/// SC-006 worst cell: MONO replacement (the old note held: Overlap), AlwaysRetrigger, Reset, no portamento; the
/// reference is a POLY-2 render of the same events (the new note on an idle voice). (b): the S9.9 bound.
[[nodiscard]] P2Reading p2ReplaceCell(std::uint8_t note, int intervalSt,
                                      std::map<int, t13Pt::VoiceEngineSteady>& cache) {
    const auto target = static_cast<std::uint8_t>(static_cast<int>(note) + intervalSt);
    const std::uint64_t tS = t13At(kP2StealAtS);
    const std::uint64_t total = tS + t13At(kP2TailS);
    t13Pt::VoiceEngineScript sc = p2Script(ProfundumEngine::VoiceMode::Mono, 1, total, 0);
    sc.events.push_back(p2On(0, note));
    sc.events.push_back(p2On(tS, target));
    t13Pt::VoiceEngineScript ref = sc;
    ref.mode = ProfundumEngine::VoiceMode::Poly;
    ref.polyphony = 2;
    ref.events.back() = p2On(tS + p2SwitchDelay(), target);   // reading refinement (2026-10-20): at the switch sample
    const t13Pt::VoiceEngineTaps y = t13Pt::voiceEngineRender(sc);
    const t13Pt::VoiceEngineTaps r = t13Pt::voiceEngineRender(ref);

    const t13Pt::VoiceEngineWindow w =
        p2Window(tS, static_cast<double>(t13NoteHz(note)), static_cast<double>(t13NoteHz(target)));
    P2Reading out;
    out.id = "SC006 replace " + t13NoteName(note) + " " + (intervalSt > 0 ? "+" : "") + std::to_string(intervalSt)
             + " st, old held";
    out.ratio = p2Ratio(y, r, w);
    out.maxDelta = p2MaxDelta(y, w);
    const std::array<t13Pt::VoiceEngineSteady, 2> involved{p2Steady(note, cache), p2Steady(target, cache)};
    out.bound = t13Pt::voiceEngineDeltaBound(involved, t13Pt::voiceEngineDeclickSlope(kT13Fs));
    out.wBegin = w.begin;
    out.wEnd = w.end;
    return out;
}

}  // namespace

// P-2 (plan R-B, S10.4; tasks.md T013). Hidden; run by evidence/close/probe.sh p2. A RED verdict is STOP AND
// SURFACE to the user with the readings (R-B; RetriggerDeclick's 1.5 ms is not changed).
TEST_CASE("VoiceEngine_DeclickReferenceProbe", "[systems][profundum][voice-engine][.probe]") {
    enableFTZDAZ();
    std::ostringstream table;
    table << std::setprecision(6);
    table << "# P-2 VoiceEngine_DeclickReferenceProbe (tasks.md T013; plan R-B, S10.4, S9.9)\n"
          << "# A_s (showcase A, sustain 1, SOLID, Drop 0), AlwaysRetrigger, Retrigger Reset, held victim, host "
             "block 1, 48 kHz. Readings on the pre-dynamics Sum tap, the worse of L / R.\n"
          << "# (a) ratio = max|dy| over W / the reference's (SC-005: polyphony 5; SC-006: POLY-2), bar "
          << kP2RatioBar << ". (b) max|dy| over W <= bound (S9.9; SC-005 adds 1.1 D_max of each held filler).\n"
          << "# W = [steal / restart grid point, + samplesUntilSwitch + L_d + one period of the slower f0).\n";
    std::map<int, t13Pt::VoiceEngineSteady> cache;
    std::vector<P2Reading> cells;
    for (const std::uint8_t n : {kT13NoteE1, kT13NoteC3})
        cells.push_back(p2StealCell(n, cache));
    for (const std::uint8_t n : {kT13NoteE1, kT13NoteC3}) {
        for (const int k : kP2IntervalsSt) {
            for (const int sign : {1, -1}) {
                const int target = static_cast<int>(n) + sign * k;
                if (target < kP2LowestNote || target > kP2HighestNote) {
                    table << "# skipped " << t13NoteName(n) << " " << (sign > 0 ? "+" : "-") << k
                          << " st: target note " << target << " outside [" << kP2LowestNote << ", " << kP2HighestNote
                          << "]\n";
                    continue;
                }
                cells.push_back(p2ReplaceCell(n, sign * k, cache));
            }
        }
    }

    bool red = false;
    double worstRatio = 0.0;
    for (const P2Reading& c : cells) {
        const double margin = c.maxDelta > 0.0 ? c.bound / c.maxDelta : 0.0;
        table << "P2 " << c.id << " | W [" << c.wBegin << ", " << c.wEnd << ") ratio=" << c.ratio
              << " a=" << (c.aPass() ? "PASS" : "FAIL") << " max_dy=" << c.maxDelta << " bound=" << c.bound
              << " margin=" << margin << " b=" << (c.bPass() ? "PASS" : "FAIL")
              << ((!c.aPass() && c.bPass()) ? " <- R-B STOP CELL" : "") << (!c.bPass() ? " <- DEFECT (b fails)" : "")
              << "\n";
        red = red || !c.aPass() || !c.bPass();   // main loop 2026-10-20: any failing cell is RED (a (b) failure is a defect)
        worstRatio = std::max(worstRatio, c.ratio);
        CHECK(t13Finite(c.ratio));
        CHECK(t13Finite(c.maxDelta));
        CHECK(t13Finite(c.bound));
    }
    table << "P2 VERDICT " << (red ? "RED" : "GREEN") << " worst_ratio=" << worstRatio << " (bar " << kP2RatioBar
          << ")\n";
    std::cout << table.str();
    t13WriteEvidence("probes/p2-declick.txt", table.str());
}

// Main-loop diagnostic (2026-10-20): where inside W does the SC-006 E1 +1 st restart put its largest sample step,
// and what do the voice's own taps (g_d, g_amp, core, anchor) do there? Variants separate the dip, the Reset and
// the mode. Hidden; not part of any gate.
TEST_CASE("VoiceEngine_DeclickDiagProbe", "[systems][profundum][voice-engine][.probe]") {
    enableFTZDAZ();
    const std::uint8_t note = kT13NoteE1;
    const auto target = static_cast<std::uint8_t>(note + 1);
    const std::uint64_t tS = t13At(kP2StealAtS);
    const std::uint64_t total = tS + t13At(kP2TailS);
    const t13Pt::VoiceEngineWindow w =
        p2Window(tS, static_cast<double>(t13NoteHz(note)), static_cast<double>(t13NoteHz(target)));
    RetriggerDeclick dk;
    dk.prepare(kT13Fs, ProfundumVoice::kDeclickMs);   // R-B
    dk.arm();
    std::ostringstream out;
    out << std::setprecision(6);
    out << "# P2DIAG E1 -> F1 (+1 st), MONO, A_s, host block 1; W [" << w.begin << ", " << w.end << "); tS " << tS
        << "; declick samplesUntilSwitch " << dk.samplesUntilSwitch() << " length "
        << t13Pt::voiceEngineDeclickLength(kT13Fs) << "\n";
    struct Variant {
        const char* name;
        ProfundumEngine::VoiceMode mode;
        std::size_t polyphony;
        NoteMemory::Mode memory;
        NoteMemory::Retrigger retrigger;
        bool disableDeclick;
    };
    const std::array<Variant, 6> variants{{
        {"mono-always-reset", ProfundumEngine::VoiceMode::Mono, 1, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset, false},
        {"mono-always-continue", ProfundumEngine::VoiceMode::Mono, 1, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Continue, false},
        {"mono-always-reset-nodip", ProfundumEngine::VoiceMode::Mono, 1, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset, true},
        {"mono-legato-reset", ProfundumEngine::VoiceMode::Mono, 1, NoteMemory::Mode::Legato, NoteMemory::Retrigger::Reset, false},
        {"mono-osccontinuous", ProfundumEngine::VoiceMode::Mono, 1, NoteMemory::Mode::OscillatorContinuous, NoteMemory::Retrigger::Continue, false},
        {"poly2-reference", ProfundumEngine::VoiceMode::Poly, 2, NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset, false},
    }};
    for (const Variant& v : variants) {
        t13Pt::VoiceEngineScript sc = p2Script(v.mode, v.polyphony, total, 2);
        sc.events = {p2Setting(P2Kind::NoteMemoryMode, static_cast<int>(v.memory)),
                     p2Setting(P2Kind::NoteRetrigger, static_cast<int>(v.retrigger))};
        sc.config.seams.disableDeclick = v.disableDeclick;
        sc.events.push_back(p2On(0, note));
        sc.events.push_back(p2On(tS, target));
        const t13Pt::VoiceEngineTaps t = t13Pt::voiceEngineRender(sc);
        std::size_t arg = w.begin + 1;
        double best = 0.0;
        for (std::size_t i = w.begin + 1; i < std::min<std::size_t>(w.end, t.sumL.size()); ++i) {
            const double d = std::fabs(static_cast<double>(t.sumL[i]) - static_cast<double>(t.sumL[i - 1]));
            if (d > best) {
                best = d;
                arg = i;
            }
        }
        out << "P2DIAG " << v.name << " max_dy=" << best << " at " << arg << " (tS+" << (static_cast<long long>(arg) - static_cast<long long>(tS)) << ")\n";
        const auto dump = [&](const char* label, const std::vector<float>& x) {
            out << "#   " << label << ":";
            for (std::size_t i = arg >= 3 ? arg - 3 : 0; i <= arg + 2 && i < x.size(); ++i)
                out << ' ' << x[i];
            out << "\n";
        };
        dump("sumL", t.sumL);
        dump("v0.gD", t.voices[0].gD);
        dump("v0.gAmp", t.voices[0].gAmp);
        dump("v0.coreL", t.voices[0].coreL);
        dump("v0.charL", t.voices[0].charL);
        dump("v0.anchorL", t.voices[0].anchorL);
        dump("v0.fCore", t.voices[0].fCore);
        if (v.polyphony > 1) {
            dump("v1.gAmp", t.voices[1].gAmp);
            dump("v1.charL", t.voices[1].charL);
        }
        // the largest g_amp step and the largest core step anywhere in W, to see which one is the discontinuity
        double gaStep = 0.0, coreStep = 0.0, chStep = 0.0;
        std::size_t gaAt = 0, coreAt = 0, chAt = 0;
        for (std::size_t i = w.begin + 1; i < std::min<std::size_t>(w.end, t.voices[0].gAmp.size()); ++i) {
            const double ga = std::fabs(static_cast<double>(t.voices[0].gAmp[i]) - static_cast<double>(t.voices[0].gAmp[i - 1]));
            const double co = std::fabs(static_cast<double>(t.voices[0].coreL[i]) - static_cast<double>(t.voices[0].coreL[i - 1]));
            const double ch = std::fabs(static_cast<double>(t.voices[0].charL[i]) - static_cast<double>(t.voices[0].charL[i - 1]));
            if (ga > gaStep) { gaStep = ga; gaAt = i; }
            if (co > coreStep) { coreStep = co; coreAt = i; }
            if (ch > chStep) { chStep = ch; chAt = i; }
        }
        out << "#   max step in W: gAmp " << gaStep << " at tS+" << (static_cast<long long>(gaAt) - static_cast<long long>(tS))
            << "; coreL " << coreStep << " at tS+" << (static_cast<long long>(coreAt) - static_cast<long long>(tS))
            << "; charL " << chStep << " at tS+" << (static_cast<long long>(chAt) - static_cast<long long>(tS)) << "\n";
        CHECK(t13Finite(best));
    }
    std::cout << out.str();
    t13WriteEvidence("probes/p2-declick-diag.txt", out.str());
}

TEST_CASE("VoiceEngine_StealDiagProbe", "[systems][profundum][voice-engine][.probe]") {
    enableFTZDAZ();
    const std::uint8_t victim = kT13NoteE1;
    const auto newNote = static_cast<std::uint8_t>(static_cast<int>(victim) + kP2StealOffsetSt);
    const std::uint64_t tS = t13At(kP2StealAtS);
    const std::uint64_t total = tS + t13At(kP2TailS);
    const t13Pt::VoiceEngineWindow w =
        p2Window(tS, static_cast<double>(t13NoteHz(victim)), static_cast<double>(t13NoteHz(newNote)));
    std::ostringstream out;
    out << std::setprecision(6);
    out << "# STEALDIAG POLY4 victim E1 held, fillers +5/+7/+10 st, new +3 st at " << tS << "; W [" << w.begin << ", "
        << w.end << ")\n";
    for (const std::size_t poly : {std::size_t{4}, std::size_t{5}}) {
        t13Pt::VoiceEngineScript sc = p2Script(ProfundumEngine::VoiceMode::Poly, poly, total, 5);
        sc.events.push_back(p2On(0, victim));
        for (std::size_t i = 0; i < kP2FillerOffsetsSt.size(); ++i)
            sc.events.push_back(p2On(t13At(kP2FillerOnS[i]),
                                     static_cast<std::uint8_t>(static_cast<int>(victim) + kP2FillerOffsetsSt[i])));
        sc.events.push_back(p2On(tS, newNote));
        const t13Pt::VoiceEngineTaps t = t13Pt::voiceEngineRender(sc);
        for (const char ch : {'L', 'R'}) {
            const std::vector<float>& sum = ch == 'L' ? t.sumL : t.sumR;
            std::size_t arg = w.begin + 1;
            double best = 0.0;
            for (std::size_t i = w.begin + 1; i < std::min<std::size_t>(w.end, sum.size()); ++i) {
                const double d = std::fabs(static_cast<double>(sum[i]) - static_cast<double>(sum[i - 1]));
                if (d > best) {
                    best = d;
                    arg = i;
                }
            }
            out << "STEALDIAG poly" << poly << " " << ch << " max_dy=" << best << " at tS+"
                << (static_cast<long long>(arg) - static_cast<long long>(tS)) << "\n";
            const auto dump = [&](const std::string& label, const std::vector<float>& x) {
                out << "#   " << label << ":";
                for (std::size_t i = arg >= 3 ? arg - 3 : 0; i <= arg + 2 && i < x.size(); ++i)
                    out << ' ' << x[i];
                out << "\n";
            };
            dump("sum", sum);
            for (std::size_t v = 0; v < poly; ++v) {
                const auto& vt = t.voices[v];
                const std::vector<float>& c = ch == 'L' ? vt.charL : vt.charR;
                const std::vector<float>& a = ch == 'L' ? vt.anchorL : vt.anchorR;
                dump("v" + std::to_string(v) + ".char", c);
                dump("v" + std::to_string(v) + ".anchor", a);
                dump("v" + std::to_string(v) + ".gAmp", vt.gAmp);
                dump("v" + std::to_string(v) + ".gD", vt.gD);
            }
            // per-voice contribution step at arg: (char + anchor) * gAmp * gD
            for (std::size_t v = 0; v < poly; ++v) {
                const auto& vt = t.voices[v];
                const std::vector<float>& c = ch == 'L' ? vt.charL : vt.charR;
                const std::vector<float>& a = ch == 'L' ? vt.anchorL : vt.anchorR;
                if (arg < 1 || arg >= c.size() || arg >= vt.gAmp.size() || arg >= vt.gD.size())
                    continue;
                const double y1 = (static_cast<double>(c[arg]) + static_cast<double>(a[arg])) * vt.gAmp[arg] * vt.gD[arg];
                const double y0 = (static_cast<double>(c[arg - 1]) + static_cast<double>(a[arg - 1])) * vt.gAmp[arg - 1] * vt.gD[arg - 1];
                out << "#   v" << v << " contribution step " << (y1 - y0) << "\n";
            }
        }
    }
    std::cout << out.str();
    t13WriteEvidence("probes/p2-steal-diag.txt", out.str());
}

// -----------------------------------------------------------------------------
// P-4: the Amount at which FOLLOW's perceived-pitch ratio flips
// -----------------------------------------------------------------------------

namespace {

constexpr std::size_t kP4Steps = 101;
constexpr double kP4LeadS = 0.5;
constexpr double kP4StepS = 0.06;
constexpr std::size_t kP4Block = 64;

/// Voice 0's follow-ratio tap and HalfWave flag from its last chunk (ProfundumVoiceTaps::followRatio is the
/// value handed at the interval's beginInterval, S5.4 step 6).
class P4Observer final : public ProfundumEngineObserver {
public:
    void onVoiceChunk(const ProfundumVoiceTaps& v) noexcept override {
        if (v.voice != 0)
            return;
        ratio_ = v.followRatio;
        halfWave_ = v.halfWave;
    }
    float ratio_ = 1.0f;
    bool halfWave_ = false;
};

struct P4Sweep {
    std::array<float, kP4Steps> ratio{};
    std::array<bool, kP4Steps> halfWave{};
    int firstFlip = -1;
};

/// A_s (kSineAnchor, Fold 0, excitation 0: a half-wave-symmetric Character input), Anchor FOLLOW, MONO, one held
/// note; Amount stepped 0 -> 1 in 101 steps through both endpoints (position 0), each held kP4StepS.
[[nodiscard]] P4Sweep p4Run(BassCharacterStage::Algorithm alg, std::uint8_t note) {
    MorphState s = t13StateAs();
    s.foldAmount = 0.0f;
    s.excitation.mix = 0.0f;
    s.characterAlgorithm = alg;
    s.characterAmount = 0.0f;
    s.anchor.mode = AnchorOscillator::Mode::Follow;
    REQUIRE(isValidMorphState(s));
    P4Observer obs;
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s));
    e->setVoiceMode(ProfundumEngine::VoiceMode::Mono);
    e->setObserver(&obs);
    e->prepare(kT13Fs);
    e->noteOn(note, kT13Velocity);
    std::array<float, kP4Block> l{};
    std::array<float, kP4Block> r{};
    const auto render = [&](std::uint64_t n) {
        while (n > 0) {
            const auto m = static_cast<std::size_t>(std::min<std::uint64_t>(n, kP4Block));
            e->processBlock(l.data(), r.data(), m);
            n -= m;
        }
    };
    render(t13At(kP4LeadS));
    P4Sweep out;
    for (std::size_t i = 0; i < kP4Steps; ++i) {
        s.characterAmount = static_cast<float>(i) / static_cast<float>(kP4Steps - 1);
        REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s));
        REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s));
        render(t13At(kP4StepS));
        out.ratio[i] = obs.ratio_;
        out.halfWave[i] = obs.halfWave_;
        if (out.firstFlip < 0 && !t13Same(obs.ratio_, 1.0f))
            out.firstFlip = static_cast<int>(i);
    }
    e->setObserver(nullptr);
    return out;
}

}  // namespace

// P-4 (plan S10.4; tasks.md T013; roadmap 688-689). Hidden; run by evidence/close/probe.sh p4. FOLLOW INERT is
// recorded and carried to T016 / T017 (SC-018 / AT-13 then carry the flag).
TEST_CASE("VoiceEngine_FollowFlipProbe", "[systems][profundum][voice-engine][.probe]") {
    enableFTZDAZ();
    std::ostringstream table;
    table << std::setprecision(6);
    table << "# P-4 VoiceEngine_FollowFlipProbe (tasks.md T013; plan S10.4)\n"
          << "# A_s (kSineAnchor, Fold 0, excitation 0, Tone 0.5), Anchor FOLLOW, MONO, one held note; Amount 0 -> 1 "
             "in "
          << kP4Steps << " steps of " << kP4StepS << " s after a " << kP4LeadS
          << " s lead-in; the follow-ratio tap (voice 0's last chunk) per step.\n";
    std::ostringstream summary;
    bool anyFlip = false;
    for (const BassCharacterStage::Algorithm alg :
         {BassCharacterStage::Algorithm::Rectify, BassCharacterStage::Algorithm::Fracture}) {
        for (const std::uint8_t note : {kT13NoteE1, kT13NoteC3}) {
            const P4Sweep sw = p4Run(alg, note);
            for (std::size_t i = 0; i < kP4Steps; ++i) {
                table << "#   " << t13AlgName(alg) << " " << t13NoteName(note) << " amount="
                      << static_cast<double>(i) / static_cast<double>(kP4Steps - 1) << " ratio=" << sw.ratio[i]
                      << " halfWave=" << (sw.halfWave[i] ? 1 : 0) << "\n";
            }
            summary << "P4 " << t13AlgName(alg) << " " << t13NoteName(note) << " ";
            if (sw.firstFlip >= 0) {
                anyFlip = true;
                const auto fi = static_cast<std::size_t>(sw.firstFlip);
                summary << "first_flip_amount="
                        << static_cast<double>(sw.firstFlip) / static_cast<double>(kP4Steps - 1)
                        << " ratio=" << sw.ratio[fi] << " halfWave=" << (sw.halfWave[fi] ? 1 : 0) << "\n";
            } else {
                summary << "NO FLIP (halfWave at Amount 1: " << (sw.halfWave[kP4Steps - 1] ? 1 : 0) << ")\n";
            }
        }
    }
    table << summary.str();
    table << "P4 VERDICT " << (anyFlip ? "FLIPS" : "FOLLOW INERT") << "\n";
    std::cout << table.str();
    t13WriteEvidence("probes/p4-follow-flip.txt", table.str());
}

// -----------------------------------------------------------------------------
// H-13 Record: the Character h1 transfer, group delay and growl level swing in the composed voice
// -----------------------------------------------------------------------------

namespace {

constexpr std::array<std::uint8_t, 4> kH13Notes{kT13NoteE0, kT13NoteE1, kT13NoteE2, kT13NoteC3};
constexpr double kH13RenderS = 1.5;
constexpr double kH13WindowFromS = 1.0;
constexpr double kH13WindowS = 0.5;
constexpr double kH13Step = 1.01;   // f0 +- 1 %
constexpr std::size_t kH13ShapeSteps = 11;
constexpr double kH13AbsentDb = -60.0;

[[nodiscard]] MorphState h13State(BassCharacterStage::Algorithm alg, float amount, float shape,
                                  const SpectralShapeRecipe::Controls& recipe) {
    MorphState s = t13StateAs();
    s.recipe = recipe;
    s.foldAmount = 0.0f;
    s.excitation.mix = 0.0f;
    s.shape = shape;
    s.characterAlgorithm = alg;
    s.characterAmount = amount;
    s.anchorFoldFeed = 0.0f;
    REQUIRE(isValidMorphState(s));
    return s;
}

/// MONO, one held note from sample 0, optional pitch bend issued before it (bend range: the engine default 2 st).
[[nodiscard]] t13Pt::VoiceEngineTaps h13Render(const MorphState& s, std::uint8_t note, float bend) {
    t13Pt::VoiceEngineScript sc;
    sc.fs = kT13Fs;
    sc.hostBlock = 64;
    sc.totalSamples = t13At(kH13RenderS);
    sc.a = s;
    sc.b = s;
    sc.mode = ProfundumEngine::VoiceMode::Mono;
    sc.tappedVoices = 1;
    if (!t13Same(bend, 0.0f))
        sc.events.push_back(t13Pt::VoiceEngineEvent{.sample = 0, .kind = P2Kind::PitchBend, .value = bend});
    sc.events.push_back(p2On(0, note));
    return t13Pt::voiceEngineRender(sc);
}

/// The Character input: with Fold 0 HarmonicFold is the bit-exact idle copy (harmonic_fold.h:215-224) and a feed of
/// +0 at both ends is skipped (C-10), so the input is the ShapeMorph output. Rebuilt here from the voice's core tap
/// by a ShapeMorph given the voice's setter sequence (profundum_voice.h:201, :212, :231, :256 in prepare, the
/// engine's reset() repeating :231 / :256, then the first interval's :445) and run in the voice's own chunk
/// partition with its f_core tap (:796).
void h13CharacterInput(const t13Pt::VoiceEngineVoiceTapSet& v, float shape, std::vector<float>& inL,
                       std::vector<float>& inR) {
    auto sm = std::make_unique<ShapeMorph>();
    sm->setShape(shape);
    sm->prepare(kT13Fs, kT13Grid);
    sm->setShape(shape);
    (*sm).reset();
    sm->setShape(shape);
    (*sm).reset();
    sm->setShape(shape);
    inL.assign(v.coreL.size(), 0.0f);
    inR.assign(v.coreR.size(), 0.0f);
    std::array<float, kT13Grid> bl{};
    std::array<float, kT13Grid> br{};
    for (const t13Pt::VoiceEngineChunkRecord& c : v.chunks) {
        const auto s0 = static_cast<std::size_t>(c.sample);
        const std::size_t m = std::min(c.n, kT13Grid);
        if (s0 + m > v.coreL.size())
            break;
        std::copy_n(v.coreL.data() + s0, m, bl.data());
        std::copy_n(v.coreR.data() + s0, m, br.data());
        sm->processBlock(bl.data(), br.data(), m, v.fCore.data() + s0);
        std::copy_n(bl.data(), m, inL.data() + s0);
        std::copy_n(br.data(), m, inR.data() + s0);
    }
}

/// Complex h1 of the mid over [start, start + n) at f (single-bin DFT, amplitude-scaled).
[[nodiscard]] std::complex<double> h13H1(const std::vector<float>& L, const std::vector<float>& R, std::size_t start,
                                         std::size_t n, double f) {
    std::complex<double> acc(0.0, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        const double x = 0.5 * (static_cast<double>(L[start + k]) + static_cast<double>(R[start + k]));
        const double ph = 2.0 * kT13Pi * f * static_cast<double>(k) / kT13Fs;
        acc += x * std::complex<double>(std::cos(ph), -std::sin(ph));
    }
    return acc * (2.0 / static_cast<double>(n));
}

struct H13Transfer {
    double f0 = 0.0;
    double fDev = 0.0;       // max |f_core / f0 - 1| over the window
    double gainDb = 0.0;
    double phaseRad = 0.0;
    double inDb = 0.0;       // |X| in dB
    bool halfWave = false;
    float foldTarget = 0.0f;
};

/// One render's h1 transfer of the Character tap over the Character input, on whole periods near 0.5 s from 1 s.
[[nodiscard]] H13Transfer h13Measure(BassCharacterStage::Algorithm alg, std::uint8_t note, float bend) {
    const MorphState s = h13State(alg, 1.0f, 0.0f, SpectralShapeRecipe::kSawAnchor);
    const t13Pt::VoiceEngineTaps t = h13Render(s, note, bend);
    const t13Pt::VoiceEngineVoiceTapSet& v = t.voices[0];
    std::vector<float> inL;
    std::vector<float> inR;
    h13CharacterInput(v, s.shape, inL, inR);
    const auto start = static_cast<std::size_t>(t13At(kH13WindowFromS));
    H13Transfer out;
    out.f0 = static_cast<double>(v.fCore[start]);
    REQUIRE(out.f0 > 0.0);
    const double periods = std::max(4.0, std::floor(kH13WindowS * out.f0));
    const auto n = static_cast<std::size_t>(std::llround(periods * kT13Fs / out.f0));
    REQUIRE(start + n <= t.total);
    for (std::size_t k = start; k < start + n; ++k)
        out.fDev = std::max(out.fDev, std::abs(static_cast<double>(v.fCore[k]) / out.f0 - 1.0));
    const std::complex<double> x = h13H1(inL, inR, start, n, out.f0);
    const std::complex<double> y = h13H1(v.charL, v.charR, start, n, out.f0);
    const double ax = std::max(std::abs(x), 1e-300);
    out.inDb = 20.0 * std::log10(ax);
    out.gainDb = 20.0 * std::log10(std::max(std::abs(y), 1e-300) / ax);
    out.phaseRad = std::arg(y / x);
    if (!v.chunks.empty())
        out.halfWave = v.chunks.back().halfWave;
    if (!t.grid.empty())
        out.foldTarget = t.grid.back().targets.foldAmount;
    return out;
}

/// Phase 5's row of evidence/sc020-h1-transfer.csv (BassCharacterStage, setting = algorithm, tone 0.5, kSawAnchor).
[[nodiscard]] std::string h13Phase5Row(const char* alg, const std::string& note) {
    const std::string path = std::string(KRATE_DSP_TESTS_DIR)
                             + "/../../specs/profundum-phase5-shape-character/evidence/sc020-h1-transfer.csv";
    std::ifstream in(path);
    if (!in)
        return "phase5 n/a (sc020-h1-transfer.csv not found)";
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> col;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ','))
            col.push_back(cell);
        if (col.size() < 9)
            continue;
        if (col[0] == "BassCharacterStage" && col[1] == alg && col[2] == "0.500000" && col[3] == "kSawAnchor"
            && col[4] == note) {
            return "phase5 h1_db=" + col[6] + " h1_deg=" + col[7] + " group_delay_samples=" + col[8]
                   + (col.size() > 10 ? " status=" + col[10] : std::string());
        }
    }
    return "phase5 n/a (no row)";
}

}  // namespace

// H-13 Record (FR-070; plan S10.4; tasks.md T013). Hidden; run by evidence/close/probe.sh h13. Recorded, not gated:
// every reading is checked finite (bit test).
TEST_CASE("VoiceEngine_CharacterRecords", "[systems][profundum][voice-engine][.probe]") {
    enableFTZDAZ();
    std::ostringstream table;
    table << std::setprecision(6);
    table << "# H-13 VoiceEngine_CharacterRecords (FR-070 H-13 Record; tasks.md T013; plan S10.4)\n"
          << "# Composed voice, MONO, 48 kHz, A_s with kSawAnchor, Fold 0, excitation 0, SHAPE 0, Tone 0.5, Amount 1.\n"
          << "# Transfer: complex h1 of the Character tap over the Character input (the ShapeMorph output, rebuilt from "
             "the core tap), mid, whole periods near "
          << kH13WindowS << " s from " << kH13WindowFromS << " s.\n"
          << "# Group delay at f0: -(phi(+1 %) - phi(-1 %)) / (2 pi (f+ - f-)) x fs, f0 +- 1 % by pitch bend (range "
          << ProfundumEngine::kDefaultBendRangeSt << " st). Phase 5 reference: "
             "specs/profundum-phase5-shape-character/evidence/sc020-h1-transfer.csv (tone 0.5 rows) and the "
             "crossfade CPU cell in .../sc015-cpu.txt (recorded by SC-003 here).\n";
    const auto bendFor = [](double ratio) {
        return static_cast<float>(12.0 * std::log2(ratio) / static_cast<double>(ProfundumEngine::kDefaultBendRangeSt));
    };
    const float bendUp = bendFor(kH13Step);
    const float bendDown = bendFor(2.0 - kH13Step);
    for (const std::uint8_t note : kH13Notes) {
        for (std::size_t ai = 0; ai < BassCharacterStage::kNumAlgorithms; ++ai) {
            const auto alg = static_cast<BassCharacterStage::Algorithm>(ai);
            const H13Transfer c = h13Measure(alg, note, 0.0f);
            const H13Transfer up = h13Measure(alg, note, bendUp);
            const H13Transfer dn = h13Measure(alg, note, bendDown);
            const double dPhi = std::remainder(up.phaseRad - dn.phaseRad, 2.0 * kT13Pi);
            const double df = up.f0 - dn.f0;
            const double gd = df != 0.0 ? -dPhi / (2.0 * kT13Pi * df) * kT13Fs : 0.0;
            const bool absent = c.gainDb < kH13AbsentDb;
            table << "H13 TRANSFER " << t13NoteName(note) << " " << t13AlgName(alg) << " f0=" << c.f0
                  << " gain_db=" << c.gainDb << " phase_deg=" << c.phaseRad * 180.0 / kT13Pi
                  << " group_delay_samples=" << gd << " (f- " << dn.f0 << ", f+ " << up.f0 << ") input_h1_db="
                  << c.inDb << " f_dev=" << c.fDev << " halfWave=" << (c.halfWave ? 1 : 0)
                  << " fold_target=" << c.foldTarget << (absent ? " h1_absent" : "") << " | "
                  << h13Phase5Row(t13AlgName(alg), t13NoteName(note)) << "\n";
            CHECK(c.foldTarget == 0.0f);   // the Character input is the Shape output only at Fold 0
            CHECK(t13Finite(c.gainDb));
            CHECK(t13Finite(c.phaseRad));
            CHECK(t13Finite(gd));
        }
    }

    // Growl level swing (the Phase 5 C-6 measurand): L_K of the Character tap, kGrowl, Amount 0, SHAPE 0 ... 1.
    table << "# Growl level swing: kGrowl, Tube at Amount 0, SHAPE 0 ... 1 in " << kH13ShapeSteps
          << " steps; L_K = mean of the K-weighted levels of the Character tap L / R over [" << kH13WindowFromS
          << ", " << kH13WindowFromS + kH13WindowS << ") s\n";
    for (const std::uint8_t note : kH13Notes) {
        double lo = 1e300;
        double hi = -1e300;
        for (std::size_t i = 0; i < kH13ShapeSteps; ++i) {
            const float shape = static_cast<float>(i) / static_cast<float>(kH13ShapeSteps - 1);
            const MorphState s = h13State(BassCharacterStage::Algorithm::Tube, 0.0f, shape, SpectralShapeRecipe::kGrowl);
            const t13Pt::VoiceEngineTaps t = h13Render(s, note, 0.0f);
            const auto start = static_cast<std::size_t>(t13At(kH13WindowFromS));
            const auto n = static_cast<std::size_t>(std::llround(kH13WindowS * kT13Fs));
            REQUIRE(start + n <= t.total);
            const std::vector<double> kL = t13Pt::voiceEngineKWeighted(t.characterL, kT13Fs);
            const std::vector<double> kR = t13Pt::voiceEngineKWeighted(t.characterR, kT13Fs);
            const double lk =
                0.5 * (t13Pt::voiceEngineKLevelDb(kL, start, n) + t13Pt::voiceEngineKLevelDb(kR, start, n));
            lo = std::min(lo, lk);
            hi = std::max(hi, lk);
            table << "H13 GROWL " << t13NoteName(note) << " shape=" << shape << " lk_db=" << lk << "\n";
            CHECK(t13Finite(lk));
        }
        table << "H13 GROWL_SWING " << t13NoteName(note) << " " << hi - lo << " dB (min " << lo << ", max " << hi
              << ")\n";
    }
    std::cout << table.str();
    t13WriteEvidence("h13-character-records.txt", table.str());
}

// =============================================================================
// T014 - VoiceEngine_MacroHarvest (plan S4.2, S10.5; tasks.md T014; deviation 12)
// =============================================================================
// Per macro, from the S4.2 seed amounts (signs fixed): every row of the macro is scaled by k in steps of 0.25 (k = 1
// ... 4) under one curve, until the AT-47 ... AT-50 row (the SC-007 axis descriptor, macro 0 -> 1, the others neutral,
// base A_s, MONO, one held note) passes its family bar at E1 and C3 AND the 21-step SC-007 sweep is monotone within
// eps against the running maximum (Phase 8 deviation 8, bass_morph_trajectory_test.cpp:182-204) at both notes.
// Candidate order: Linear k = 1 ... 3; then Exponential k = 1 ... 4; SCurve k = 1 ... 4; then Linear k = 3.25 ... 4.
// Axes (spec SC-007, plan S10.6): Weight -> E_sub (Sum) >= 3 dB; Dark <-> Bright -> D_br (Sum) >= 0.25 oct;
// Tight <-> Loose -> D_t (Sum, gate render) ratio >= 2; Clean <-> Violent -> D_nl (Character over the
// forceLinearCore twin) >= 3 dB. Eps 0.25 dB / 0.02 oct / 2 %.
//
// Emulation (kRows is constexpr, so a candidate table cannot be handed to the engine): the candidate's offsets are
// applied to the endpoint state test-side with the matrix's own arithmetic (S9.3: add + clamp, amp times x 2^c,
// a +-0 contribution skipped), both endpoints carry it and the engine's macros stay at the neutral, where every
// contribution is +-0 and apply() leaves the state untouched (SC-008). The port is proven bit-equal to
// ProfundumMacroMatrix::apply on the live kRows before the search. Row 11 (CompressionAmount, C-7) is not a
// MorphState field: it is handed post-law to MassDynamics only (profundum_engine.h step 8), after the Sum tap and
// outside the Character / core taps, so no harvest descriptor can read it; it is scaled with its macro like every
// other row. The AT row reads macro 0 and 1 only, where applyModCurve(c, 1) == 1 for every curve
// (modulation_curves.h:39-50), so the row reading depends on k alone and is cached per k.
//
// Output: evidence/macro-rows.txt (the chosen candidates, the 16-row table in the S4.2 format, HARVEST GREEN or one
// HARVEST RED <macro> line per failing macro; the full search log is this case's stdout, kept by harvest.sh);
// evidence/macro-rows.inc (the kRows initialiser for paste-harvest.js), written only when every macro is green (a
// stale .inc is removed on red). HARVEST RED is STOP AND SURFACE: no bar is changed. Run by evidence/close/harvest.sh.

namespace {

namespace t14Pt = Krate::DSP::ProfundumTest;
namespace t14Bm = Krate::DSP::BassMorphTest;

using T14Target = ProfundumMacroTarget;
using T14Rows = std::array<ProfundumMacroRow, ProfundumMacroMatrix::kNumTargets>;

/// Plan S4.2 seed table (the harvest always starts here, whatever kRows holds after an earlier paste).
constexpr T14Rows kT14SeedRows{{
    {.macro = ProfundumMacro::Weight, .target = T14Target::RecipeDepth, .amount = 0.25f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::Weight, .target = T14Target::RecipeBody, .amount = 0.25f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::Weight, .target = T14Target::AnchorLevel, .amount = 0.25f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::DarkBright, .target = T14Target::RecipeEdge, .amount = 0.5f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::DarkBright, .target = T14Target::RecipeShift, .amount = 0.5f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::DarkBright, .target = T14Target::Shape, .amount = 0.0f, .curve = ModCurve::Linear},   // B10-2: dropped
    {.macro = ProfundumMacro::DarkBright, .target = T14Target::CharacterTone, .amount = 0.3f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::TightLoose, .target = T14Target::AmpAttackOct, .amount = 2.0f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::TightLoose, .target = T14Target::AmpDecayOct, .amount = 2.0f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::TightLoose, .target = T14Target::AmpReleaseOct, .amount = 2.0f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::TightLoose, .target = T14Target::TransientAttackDb, .amount = -6.0f,
     .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::TightLoose, .target = T14Target::CompressionAmount, .amount = -0.5f,
     .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::CleanViolent, .target = T14Target::ExcitationMix, .amount = 0.5f,
     .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::CleanViolent, .target = T14Target::ExcitationDrive, .amount = 0.5f,
     .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::CleanViolent, .target = T14Target::FoldAmount, .amount = 50.0f, .curve = ModCurve::Linear},
    {.macro = ProfundumMacro::CleanViolent, .target = T14Target::CharacterAmount, .amount = 0.5f,
     .curve = ModCurve::Linear},
}};

constexpr std::array<const char*, ProfundumMacroMatrix::kNumMacros> kT14MacroNames{"Weight", "DarkBright", "TightLoose",
                                                                                   "CleanViolent"};
constexpr std::array<const char*, ProfundumMacroMatrix::kNumTargets> kT14TargetNames{
    "RecipeDepth",   "RecipeBody",    "AnchorLevel",     "RecipeEdge",        "RecipeShift",
    "Shape",         "CharacterTone", "AmpAttackOct",    "AmpDecayOct",       "AmpReleaseOct",
    "TransientAttackDb", "CompressionAmount", "ExcitationMix", "ExcitationDrive", "FoldAmount",
    "CharacterAmount"};
constexpr std::array<const char*, ProfundumMacroMatrix::kNumTargets> kT14Units{
    "recipe [0, 1]", "recipe [0, 1]", "level [0, 1]", "recipe [0, 1]", "recipe [-1, 1]",
    "SHAPE [0, 1], toward BITE (1)", "Tone [0, 1]", "octaves of time (x4 at Loose, /4 at Tight, at the seed)",
    "octaves", "octaves", "dB, [-12, 12]", "amount [0, 1], post-law (C-7)", "[0, 1]", "[0, 1]", "[0, 100]",
    "[0, 1]"};

constexpr std::array<std::uint8_t, 2> kT14Notes{kT13NoteE1, kT13NoteC3};
constexpr std::size_t kT14SweepSteps = 21;    // SC-007
constexpr float kT14ScaleStep = 0.25f;        // 25 % of the seed
constexpr float kT14LinearFirstMax = 3.0f;    // other curves are tried once Linear is still red at 3x
constexpr float kT14MaxScale = 4.0f;          // HARVEST RED beyond 4x with every curve
constexpr std::size_t kT14NumScales = 13;     // k = 1, 1.25, ..., 4
constexpr double kT14BarDb = 3.0;             // FR-080: dB descriptors (E_sub, D_nl)
constexpr double kT14BarOct = 0.25;           // FR-080: centroid
constexpr double kT14BarRatio = 2.0;          // FR-080: time descriptors
constexpr double kT14EpsDb = 0.25;            // SC-007 eps (Phase 8 D-8)
constexpr double kT14EpsOct = 0.02;
constexpr double kT14EpsRatio = 0.98;         // 2 %
constexpr double kT14MaxGateSeconds = 12.0;   // the Phase 8 gate render's ceiling (bass_morph_test_helpers.h:945)
constexpr float kT14FoldMax = 100.0f;         // the matrix's kFoldMax (profundum_macro_matrix.h, S9.3)

enum class T14Axis : std::uint8_t { ESub, Dbr, Dt, Dnl };

[[nodiscard]] T14Axis t14AxisOf(ProfundumMacro m) noexcept {
    switch (m) {
        case ProfundumMacro::Weight: return T14Axis::ESub;
        case ProfundumMacro::DarkBright: return T14Axis::Dbr;
        case ProfundumMacro::TightLoose: return T14Axis::Dt;
        case ProfundumMacro::CleanViolent:
        case ProfundumMacro::Count: break;
    }
    return T14Axis::Dnl;
}

[[nodiscard]] const char* t14AxisName(T14Axis a) noexcept {
    switch (a) {
        case T14Axis::ESub: return "E_sub (Sum) dB";
        case T14Axis::Dbr: return "D_br (Sum) oct";
        case T14Axis::Dt: return "D_t (Sum, gate render) ms";
        case T14Axis::Dnl: return "D_nl (Character / twin) dB";
    }
    return "?";
}

[[nodiscard]] const char* t14CurveName(ModCurve c) noexcept {
    switch (c) {
        case ModCurve::Linear: return "Linear";
        case ModCurve::Exponential: return "Exponential";
        case ModCurve::SCurve: return "SCurve";
        case ModCurve::Stepped: return "Stepped";
    }
    return "?";
}

/// The candidate table: the rows of macro m at seed x scale under `curve`; every other row at its seed.
[[nodiscard]] T14Rows t14Candidate(ProfundumMacro m, float scale, ModCurve curve) {
    T14Rows rows = kT14SeedRows;
    for (ProfundumMacroRow& r : rows) {
        if (r.macro == m) {
            r.amount = r.amount * scale;
            r.curve = curve;
        }
    }
    return rows;
}

[[nodiscard]] bool t14NonZero(float c) noexcept {
    return (std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) != 0u;
}

void t14AddClamped(float& x, float c, float lo, float hi) noexcept {
    if (!t14NonZero(c))
        return;
    const float v = x + c;
    if (Krate::DSP::detail::isFinite(v))
        x = std::clamp(v, lo, hi);
}

void t14ScaleTimeClamped(float& t, float c) noexcept {
    if (!t14NonZero(c))
        return;
    const float v = t * std::exp2(c);
    if (Krate::DSP::detail::isFinite(v))
        t = std::clamp(v, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
}

/// The matrix's S9.3 arithmetic (profundum_macro_matrix.h contributionOf / apply) for macro m at `value`, the other
/// macros neutral, over `rows`. CompressionAmount is skipped exactly as apply() skips it (C-7).
[[nodiscard]] MorphState t14Offset(const MorphState& base, const T14Rows& rows, ProfundumMacro m, float value) {
    MorphState s = base;
    const float v = std::clamp(value, 0.0f, 1.0f);
    const float u = (v - ProfundumMacroMatrix::kNeutral) * 2.0f;
    for (const ProfundumMacroRow& row : rows) {
        if (row.macro != m)
            continue;
        const float c = row.amount * applyModCurve(row.curve, std::fabs(u)) * (u < 0.0f ? -1.0f : 1.0f);
        switch (row.target) {
            case T14Target::RecipeDepth: t14AddClamped(s.recipe.depth, c, 0.0f, 1.0f); break;
            case T14Target::RecipeBody: t14AddClamped(s.recipe.body, c, 0.0f, 1.0f); break;
            case T14Target::AnchorLevel: t14AddClamped(s.anchor.level, c, 0.0f, 1.0f); break;
            case T14Target::RecipeEdge: t14AddClamped(s.recipe.edge, c, 0.0f, 1.0f); break;
            case T14Target::RecipeShift: t14AddClamped(s.recipe.shift, c, -1.0f, 1.0f); break;
            case T14Target::Shape: t14AddClamped(s.shape, c, 0.0f, 1.0f); break;
            case T14Target::CharacterTone: t14AddClamped(s.characterTone, c, 0.0f, 1.0f); break;
            case T14Target::AmpAttackOct: t14ScaleTimeClamped(s.ampEnv.attackMs, c); break;
            case T14Target::AmpDecayOct: t14ScaleTimeClamped(s.ampEnv.decayMs, c); break;
            case T14Target::AmpReleaseOct: t14ScaleTimeClamped(s.ampEnv.releaseMs, c); break;
            case T14Target::TransientAttackDb:
                t14AddClamped(s.transientAttackDb, c, TransientShaper::kMinGainDb, TransientShaper::kMaxGainDb);
                break;
            case T14Target::CompressionAmount: break;   // post-law, MassDynamics only (C-7)
            case T14Target::ExcitationMix: t14AddClamped(s.excitation.mix, c, 0.0f, 1.0f); break;
            case T14Target::ExcitationDrive: t14AddClamped(s.excitation.drive, c, 0.0f, 1.0f); break;
            case T14Target::FoldAmount: t14AddClamped(s.foldAmount, c, 0.0f, kT14FoldMax); break;
            case T14Target::CharacterAmount: t14AddClamped(s.characterAmount, c, 0.0f, 1.0f); break;
            case T14Target::Count: break;
        }
    }
    return s;
}

/// MONO, one held note from sample 0, A = B = `s`, macros neutral, the observer attached (summed taps only).
[[nodiscard]] t14Pt::VoiceEngineScript t14Script(const MorphState& s, std::uint8_t note, std::uint64_t total) {
    t14Pt::VoiceEngineScript sc;
    sc.fs = kT13Fs;
    sc.hostBlock = 64;
    sc.totalSamples = total;
    sc.a = s;
    sc.b = s;
    sc.mode = ProfundumEngine::VoiceMode::Mono;
    sc.attachObserver = true;
    sc.tappedVoices = 0;
    sc.events.push_back(t14Pt::VoiceEngineEvent{
        .sample = 0, .kind = t14Pt::VoiceEngineEventKind::NoteOn, .note = note, .velocity = kT13Velocity});
    return sc;
}

struct T14Reading {
    double value = 0.0;
    bool valid = false;        ///< false for a D_t gate render that never fell 80 dB within 12 s, or a non-finite read
    double gateSeconds = 0.0;  ///< D_t only
};

/// One reading of the macro's axis on `s` at `note`.
[[nodiscard]] T14Reading t14Measure(const MorphState& s, std::uint8_t note, T14Axis axis) {
    REQUIRE(isValidMorphState(s));
    const double f0 = static_cast<double>(t13NoteHz(note));
    T14Reading r;
    if (axis == T14Axis::Dt) {
        // The gate render (bass_morph_test_helpers.h:942-966) on the Sum tap: note-off at 1 s (grid-aligned), length
        // note-off + 2 s, extended in 1 s steps to 12 s until the Sum mid falls 80 dB below its peak.
        const std::uint64_t off = (static_cast<std::uint64_t>(std::floor(kT13Fs)) / kT13Grid) * kT13Grid;
        for (double sec = static_cast<double>(off) / kT13Fs + 2.0; sec <= kT14MaxGateSeconds + 1e-9; sec += 1.0) {
            const std::uint64_t total =
                (static_cast<std::uint64_t>(std::llround(sec * kT13Fs)) / kT13Grid) * kT13Grid;
            t14Pt::VoiceEngineScript sc = t14Script(s, note, total);
            sc.events.push_back(
                t14Pt::VoiceEngineEvent{.sample = off, .kind = t14Pt::VoiceEngineEventKind::NoteOff, .note = note});
            const t14Pt::VoiceEngineTaps t = t14Pt::voiceEngineRender(sc);
            const t14Bm::FallOff fo =
                t14Bm::fallOff(t14Bm::periodEnergies(t14Bm::midOf(t.sumL, t.sumR), f0, kT13Fs));
            r.gateSeconds = sec;
            if (fo.reached) {
                r.value = t14Bm::dTMs(t.sumL, t.sumR, f0, kT13Fs);
                r.valid = t13Finite(r.value);
                return r;
            }
        }
        return r;
    }
    const auto n = static_cast<std::uint64_t>(std::ceil(p3Md::descriptorSeconds(kT13Fs) * kT13Fs));
    const std::uint64_t total = ((n + kT13Grid - 1) / kT13Grid) * kT13Grid + kT13Grid;
    const t14Pt::VoiceEngineScript sc = t14Script(s, note, total);
    if (axis == T14Axis::Dnl) {
        const t14Pt::VoiceEngineTaps t = t14Pt::voiceEngineRenderWithTwin(sc);
        r.value = t14Bm::dNlDb(t, f0, kT13Fs);
    } else {
        const t14Pt::VoiceEngineTaps t = t14Pt::voiceEngineRender(sc);
        r.value = axis == T14Axis::ESub
                      ? t14Bm::eSubDb(t.sumL, t.sumR, f0, kT13Fs)
                      : t14Bm::dBrOct(t14Pt::asFieldTaps(t, t14Pt::VoiceEngineTap::Sum), f0, kT13Fs);
    }
    r.valid = t13Finite(r.value);
    return r;
}

/// The row's contrast: hi - lo (dB / oct) or hi / lo (D_t).
[[nodiscard]] double t14Contrast(T14Axis axis, double lo, double hi) noexcept {
    if (axis == T14Axis::Dt)
        return lo > 0.0 ? hi / lo : 0.0;
    return hi - lo;
}

[[nodiscard]] double t14Bar(T14Axis axis) noexcept {
    switch (axis) {
        case T14Axis::ESub:
        case T14Axis::Dnl: return kT14BarDb;
        case T14Axis::Dbr: return kT14BarOct;
        case T14Axis::Dt: return kT14BarRatio;
    }
    return kT14BarDb;
}

/// Running-maximum monotone test of one step (Phase 8 deviation 8).
[[nodiscard]] bool t14WithinEps(T14Axis axis, double value, double runMax) noexcept {
    switch (axis) {
        case T14Axis::ESub:
        case T14Axis::Dnl: return value >= runMax - kT14EpsDb;
        case T14Axis::Dbr: return value >= runMax - kT14EpsOct;
        case T14Axis::Dt: return value >= kT14EpsRatio * runMax;
    }
    return false;
}

/// The macro value of SC-007 step i (0 ... 20).
[[nodiscard]] float t14Step(std::size_t i) noexcept {
    return static_cast<float>(i) / static_cast<float>(kT14SweepSteps - 1);
}

[[nodiscard]] float t14ScaleAt(std::size_t i) noexcept {
    return 1.0f + kT14ScaleStep * static_cast<float>(i);
}

/// The AT row at macro 0 and 1 (curve-independent: applyModCurve(c, 1) == 1), per note.
struct T14RowResult {
    bool evaluated = false;
    bool pass = false;
    std::array<T14Reading, 2> lo{};
    std::array<T14Reading, 2> hi{};
};

[[nodiscard]] T14RowResult t14EvaluateRow(const MorphState& base, ProfundumMacro m, float scale,
                                        std::ostream& trace) {
    const T14Axis axis = t14AxisOf(m);
    const T14Rows rows = t14Candidate(m, scale, ModCurve::Linear);
    T14RowResult r;
    r.evaluated = true;
    r.pass = true;
    for (std::size_t ni = 0; ni < kT14Notes.size(); ++ni) {
        const std::uint8_t note = kT14Notes[ni];
        r.lo[ni] = t14Measure(t14Offset(base, rows, m, 0.0f), note, axis);
        r.hi[ni] = t14Measure(t14Offset(base, rows, m, 1.0f), note, axis);
        const bool valid = r.lo[ni].valid && r.hi[ni].valid;
        const double contrast = valid ? t14Contrast(axis, r.lo[ni].value, r.hi[ni].value) : 0.0;
        const bool pass = valid && contrast >= t14Bar(axis);
        trace << "T14 ROW " << kT14MacroNames[static_cast<std::size_t>(m)] << " k=" << scale << " "
            << t13NoteName(note) << " lo=" << r.lo[ni].value << " hi=" << r.hi[ni].value
            << (axis == T14Axis::Dt ? " ratio=" : " delta=") << contrast << " bar=" << t14Bar(axis);
        if (axis == T14Axis::Dt)
            trace << " gate_s=" << r.lo[ni].gateSeconds << "/" << r.hi[ni].gateSeconds;
        trace << (valid ? "" : " (invalid reading: no 80 dB fall-off within 12 s, or non-finite)") << " "
            << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass)
            r.pass = false;
    }
    return r;
}

/// The SC-007 sweep for a candidate whose row passed: the 21 steps at both notes, the endpoints taken from the row
/// (identical states), early exit on the first step below the running maximum minus eps.
[[nodiscard]] bool t14SweepMonotone(const MorphState& base, ProfundumMacro m, float scale, ModCurve curve,
                                    const T14RowResult& row, std::ostream& trace) {
    const T14Axis axis = t14AxisOf(m);
    const T14Rows rows = t14Candidate(m, scale, curve);
    for (std::size_t ni = 0; ni < kT14Notes.size(); ++ni) {
        const std::uint8_t note = kT14Notes[ni];
        double runMax = row.lo[ni].value;
        trace << "T14 SWEEP " << kT14MacroNames[static_cast<std::size_t>(m)] << " " << t14CurveName(curve)
            << " k=" << scale << " " << t13NoteName(note) << " :";
        for (std::size_t i = 0; i < kT14SweepSteps; ++i) {
            T14Reading rd;
            if (i == 0)
                rd = row.lo[ni];
            else if (i + 1 == kT14SweepSteps)
                rd = row.hi[ni];
            else
                rd = t14Measure(t14Offset(base, rows, m, t14Step(i)), note, axis);
            trace << " " << rd.value;
            if (!rd.valid || !t14WithinEps(axis, rd.value, runMax)) {
                trace << " BREAK at step " << i << " (macro " << t14Step(i) << ", running max " << runMax
                    << (rd.valid ? "" : ", invalid reading") << ")\n";
                return false;
            }
            runMax = std::max(runMax, rd.value);
        }
        trace << " MONOTONE\n";
    }
    return true;
}

struct T14Choice {
    bool green = false;
    float scale = 1.0f;
    ModCurve curve = ModCurve::Linear;
};

struct T14Cand {
    std::size_t scaleIndex = 0;
    ModCurve curve = ModCurve::Linear;
};

/// The search of one macro (candidate order in the section banner). Each log line is flushed to stdout as it is
/// written, so a long run shows its progress in the harvest log.
[[nodiscard]] T14Choice t14Harvest(const MorphState& base, ProfundumMacro m) {
    std::vector<T14Cand> order;
    for (std::size_t i = 0; i < kT14NumScales; ++i) {
        if (t14ScaleAt(i) <= kT14LinearFirstMax)
            order.push_back(T14Cand{.scaleIndex = i, .curve = ModCurve::Linear});
    }
    for (const ModCurve c : {ModCurve::Exponential, ModCurve::SCurve}) {
        for (std::size_t i = 0; i < kT14NumScales; ++i)
            order.push_back(T14Cand{.scaleIndex = i, .curve = c});
    }
    for (std::size_t i = 0; i < kT14NumScales; ++i) {
        if (t14ScaleAt(i) > kT14LinearFirstMax)
            order.push_back(T14Cand{.scaleIndex = i, .curve = ModCurve::Linear});
    }
    REQUIRE(t14ScaleAt(kT14NumScales - 1) == kT14MaxScale);

    std::array<T14RowResult, kT14NumScales> cache{};
    for (const T14Cand& c : order) {
        const float k = t14ScaleAt(c.scaleIndex);
        std::ostringstream trace;
        trace << std::setprecision(6);
        if (!cache[c.scaleIndex].evaluated)
            cache[c.scaleIndex] = t14EvaluateRow(base, m, k, trace);
        bool monotone = false;
        if (cache[c.scaleIndex].pass)
            monotone = t14SweepMonotone(base, m, k, c.curve, cache[c.scaleIndex], trace);
        std::cout << trace.str() << std::flush;
        if (monotone)
            return T14Choice{.green = true, .scale = k, .curve = c.curve};
    }
    return T14Choice{};
}

/// A C++ float literal with an f suffix that round-trips (9 significant digits; a decimal point or an exponent is
/// always present, so 50 prints as 50.0f, never the integer-literal 50f).
[[nodiscard]] std::string t14FloatLiteral(float v) {
    std::ostringstream os;
    os << std::setprecision(9) << v;
    std::string s = os.str();
    if (s.find_first_of(".eE") == std::string::npos)
        s += ".0";
    return s + "f";
}

}  // namespace

// Main-loop diagnostic (2026-10-20, B10-1 re-harvest): which Dark <-> Bright target turns the Sum-tap centroid
// back down above macro 0.55 on B_s? Each row alone, the others at 0, Linear, amount as seeded, 21 steps.
TEST_CASE("VoiceEngine_DarkBrightTargetSweep", "[systems][profundum][voice-engine][.probe]") {
    enableFTZDAZ();
    const MorphState baseB = t13StateBs();
    REQUIRE(isValidMorphState(baseB));
    std::ostringstream out;
    out << std::setprecision(5);
    out << "# DBSWEEP B_s, E1 and C3, D_br (Sum) oct, one Dark<->Bright row alone (Linear, seed amount), 21 steps\n";
    for (std::size_t ri = 0; ri < kT14SeedRows.size(); ++ri) {
        if (kT14SeedRows[ri].macro != ProfundumMacro::DarkBright)
            continue;
        T14Rows rows = kT14SeedRows;
        for (std::size_t rj = 0; rj < rows.size(); ++rj) {
            if (rj != ri)
                rows[rj].amount = 0.0f;
            rows[rj].curve = ModCurve::Linear;
        }
        for (const std::uint8_t note : {kT13NoteE1, kT13NoteC3}) {
            out << "DBSWEEP row " << ri << " target " << static_cast<int>(kT14SeedRows[ri].target) << " "
                << t13NoteName(note) << " :";
            for (std::size_t i = 0; i < 21; ++i) {
                const T14Reading rd = t14Measure(t14Offset(baseB, rows, ProfundumMacro::DarkBright, t14Step(i)), note,
                                                 T14Axis::Dbr);
                out << ' ' << rd.value;
            }
            out << "\n";
        }
    }
    // all four together (the harvest's k = 1 Linear) for reference
    {
        T14Rows rows = kT14SeedRows;
        for (auto& r : rows)
            r.curve = ModCurve::Linear;
        for (const std::uint8_t note : {kT13NoteE1, kT13NoteC3}) {
            out << "DBSWEEP all-rows " << t13NoteName(note) << " :";
            for (std::size_t i = 0; i < 21; ++i) {
                const T14Reading rd =
                    t14Measure(t14Offset(baseB, rows, ProfundumMacro::DarkBright, t14Step(i)), note, T14Axis::Dbr);
                out << ' ' << rd.value;
            }
            out << "\n";
        }
    }
    std::cout << out.str();
    t13WriteEvidence("probes/darkbright-target-sweep.txt", out.str());
}

// S10.5 harvest (hidden; tasks.md T014). Run by evidence/close/harvest.sh, never by a gate.
TEST_CASE("VoiceEngine_MacroHarvest", "[systems][profundum][voice-engine][sc-007][sc-010][.harvest]") {
    enableFTZDAZ();
    const MorphState base = t13StateAs();   // A_s (plan S1 C-8)
    REQUIRE(isValidMorphState(base));
    // Ruling B10-1 (2026-10-20): Dark <-> Bright is harvested on B_s. On the sine-recipe A_s every one of its targets
    // sits at its clamp by 2x the seed and the centroid moves 0.016 oct (run-log/close/harvest-macro.log, run 1).
    const MorphState baseB = t13StateBs();
    REQUIRE(isValidMorphState(baseB));
    const auto baseOf = [&](ProfundumMacro m) -> const MorphState& {
        return m == ProfundumMacro::DarkBright ? baseB : base;
    };

    // The seed table and the live kRows agree on every row's macro, target and sign (deviation 12: the paste changes
    // magnitudes and curves only).
    for (std::size_t i = 0; i < kT14SeedRows.size(); ++i) {
        INFO("row " << i);
        const ProfundumMacroRow& seed = kT14SeedRows[i];
        const ProfundumMacroRow& live = ProfundumMacroMatrix::kRows[i];
        REQUIRE(seed.macro == live.macro);
        REQUIRE(seed.target == live.target);
        REQUIRE(static_cast<std::size_t>(seed.target) == i);
        REQUIRE((seed.amount < 0.0f) == (live.amount < 0.0f));
    }

    // The emulation is the matrix's own arithmetic: bit-equal to ProfundumMacroMatrix::apply on the live kRows.
    for (std::size_t mi = 0; mi < ProfundumMacroMatrix::kNumMacros; ++mi) {
        const auto m = static_cast<ProfundumMacro>(mi);
        for (const float v : {0.0f, 0.2f, 0.45f, 0.5f, 0.55f, 0.85f, 1.0f}) {
            INFO("macro " << kT14MacroNames[mi] << " value " << v);
            ProfundumMacroMatrix mm;
            mm.setMacro(m, v);
            MorphState real = base;
            mm.apply(real);
            REQUIRE(t14Bm::bitEqual(t14Offset(base, ProfundumMacroMatrix::kRows, m, v), real));
        }
    }

    std::cout << std::setprecision(6)
              << "# T014 VoiceEngine_MacroHarvest (plan S4.2, S10.5). 48 kHz, MONO, base A_s (C-8; B_s for Dark <-> "
                 "Bright, ruling B10-1), one held note per render (E1, C3), the other macros neutral; bars E_sub / D_nl >= "
              << kT14BarDb << " dB, D_br >= " << kT14BarOct << " oct, D_t ratio >= " << kT14BarRatio
              << "; sweep 21 steps against the running max, eps " << kT14EpsDb << " dB / " << kT14EpsOct << " oct / "
              << (1.0 - kT14EpsRatio) * 100.0 << " %\n";

    std::array<T14Choice, ProfundumMacroMatrix::kNumMacros> choice{};
    std::vector<std::string> red;
    std::ostringstream summary;
    summary << std::setprecision(6);
    for (std::size_t mi = 0; mi < ProfundumMacroMatrix::kNumMacros; ++mi) {
        const auto m = static_cast<ProfundumMacro>(mi);
        std::cout << "# ---- " << kT14MacroNames[mi] << ": " << t14AxisName(t14AxisOf(m))
                  << (m == ProfundumMacro::DarkBright ? " (base B_s, B10-1)" : " (base A_s)") << "\n" << std::flush;
        choice[mi] = t14Harvest(baseOf(m), m);
        if (choice[mi].green) {
            summary << "T14 CHOSEN " << kT14MacroNames[mi] << " (" << t14AxisName(t14AxisOf(m))
                    << ") k=" << choice[mi].scale << " curve " << t14CurveName(choice[mi].curve) << "\n";
        } else {
            summary << "HARVEST RED " << kT14MacroNames[mi] << " (no curve passes the AT row and the SC-007 sweep at <= "
                    << kT14MaxScale << "x the seed; STOP AND SURFACE, no bar is changed)\n";
            red.emplace_back(kT14MacroNames[mi]);
        }
    }

    // The harvested table: each macro's rows at its chosen k and curve (a red macro keeps its seed and is not pasted).
    T14Rows harvested = kT14SeedRows;
    for (ProfundumMacroRow& r : harvested) {
        const T14Choice& c = choice[static_cast<std::size_t>(r.macro)];
        if (c.green) {
            r.amount = r.amount * c.scale;
            r.curve = c.curve;
        }
    }

    std::ostringstream table;
    table << std::setprecision(9);
    table << "\n| Row | Macro | Target | Seed amount | Amount | Curve | Unit |\n|---|---|---|---|---|---|---|\n";
    for (std::size_t i = 0; i < harvested.size(); ++i) {
        const ProfundumMacroRow& r = harvested[i];
        table << "| " << i << " | " << kT14MacroNames[static_cast<std::size_t>(r.macro)] << " | "
              << kT14TargetNames[static_cast<std::size_t>(r.target)] << " | " << kT14SeedRows[i].amount << " | "
              << r.amount << " | " << t14CurveName(r.curve) << " | " << kT14Units[i] << " |\n";
    }
    table << (red.empty() ? "\nHARVEST GREEN\n" : "\n");

    std::ostringstream inc;
    inc << "    static constexpr std::array<ProfundumMacroRow, kNumTargets> kRows{{\n";
    for (const ProfundumMacroRow& r : harvested) {
        inc << "        {.macro = ProfundumMacro::" << kT14MacroNames[static_cast<std::size_t>(r.macro)]
            << ", .target = ProfundumMacroTarget::" << kT14TargetNames[static_cast<std::size_t>(r.target)]
            << ", .amount = " << t14FloatLiteral(r.amount) << ", .curve = ModCurve::" << t14CurveName(r.curve)
            << "},\n";
    }
    inc << "    }};\n";

    std::ostringstream evidence;
    evidence << "# T014 VoiceEngine_MacroHarvest (plan S4.2, S10.5; tasks.md T014). Base A_s (C-8; B_s for Dark <-> Bright, "
                "ruling B10-1), MONO, 48 kHz, E1 and C3. The search log (every row and sweep reading) is this case's stdout, kept by evidence/close/harvest.sh "
                "in evidence/run-log/close/harvest-macro.log.\n"
             << summary.str() << table.str();
    std::cout << evidence.str();
    t13WriteEvidence("macro-rows.txt", evidence.str());
    if (red.empty()) {
        t13WriteEvidence("macro-rows.inc", inc.str());
    } else {
        std::error_code ec;   // never leave a stale initialiser for the paste
        std::filesystem::remove(std::filesystem::path(p3EvidenceDir()) / "macro-rows.inc", ec);
    }
    CHECK(red.empty());
}

// =============================================================================
// T021: VoiceEngine_ListeningRenders [.listen] (FR-070 Listen rows, FR-071, SC-035; tasks.md T021)
// =============================================================================
// One WAV per Listen row of FR-070 (H-1, H-2, H-8, H-10, H-14, H-19, H-28 x2), rendered through the real
// ProfundumEngine (MONO, 48 kHz, host block 64, default config) to evidence/listen/<row>.wav, 32-bit float stereo
// (TU-local writer, the profundum_edge_fold_chain_test.cpp:598-634 form; no new shared helper). Each row is a
// sequence of segments; each segment is a fresh engine: note-on at 0, `hold` seconds held (optional retriggers /
// morph-position toggles inside the hold), note-off, 0.75 s tail, 0.25 s of silence. The base of every segment is
// A_s (t13StateAs, plan S1 C-8). Segment start times are written to evidence/listen/manifest.txt and match the
// table in evidence/listening-sheet.md. Not a gate: the verdicts are the user's (SC-035). Run by
// evidence/close/listen.sh.

namespace {

constexpr std::size_t kT21Block = 64;
constexpr double kT21TailS = 0.75;
constexpr double kT21GapS = 0.25;
constexpr float kT21SwitchRampMs = 5.0f;   // Phase 8 handoff 9.11: the component switches at a 5 ms ramp

struct T21Segment {
    std::string label;
    MorphState a{};
    MorphState b{};
    std::uint8_t note = kT13NoteE1;
    double holdS = 3.0;
    double retriggerS = 0.0;   // 0: no retrigger (noteOff + noteOn of the same note, the P-1 pattern)
    double toggleS = 0.0;      // 0: no morph toggle; else the position flips 0 <-> 1 every toggleS
};

struct T21Row {
    std::string id;
    std::string file;
    std::vector<T21Segment> segments;
};

[[nodiscard]] bool t21Finite(float x) noexcept {
    return (std::bit_cast<std::uint32_t>(x) & 0x7F800000u) != 0x7F800000u;
}

void t21AppendLe16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFFu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

void t21AppendLe32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8)
        b.push_back(static_cast<unsigned char>((v >> shift) & 0xFFu));
}

void t21AppendTag(std::vector<unsigned char>& b, const char* tag) {
    for (std::size_t i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(tag[i]));
}

/// RIFF/WAVE, WAVE_FORMAT_IEEE_FLOAT (3), stereo, 32-bit, little-endian, 18-byte fmt chunk plus the fact chunk.
[[nodiscard]] bool t21WriteWav(const std::filesystem::path& path, const std::vector<float>& L,
                               const std::vector<float>& R, double fs) {
    if (L.size() != R.size())
        return false;
    const std::uint64_t frames = L.size();
    const std::uint64_t dataBytes = frames * 8u;
    if (dataBytes > 0xFFFF0000ull)
        return false;
    const auto sr = static_cast<std::uint32_t>(std::llround(fs));
    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(dataBytes) + 64u);
    t21AppendTag(b, "RIFF");
    t21AppendLe32(b, static_cast<std::uint32_t>(4u + (8u + 18u) + (8u + 4u) + 8u + dataBytes));
    t21AppendTag(b, "WAVE");
    t21AppendTag(b, "fmt ");
    t21AppendLe32(b, 18u);
    t21AppendLe16(b, 3u);        // WAVE_FORMAT_IEEE_FLOAT
    t21AppendLe16(b, 2u);        // channels
    t21AppendLe32(b, sr);
    t21AppendLe32(b, sr * 8u);   // byte rate
    t21AppendLe16(b, 8u);        // block align
    t21AppendLe16(b, 32u);       // bits per sample
    t21AppendLe16(b, 0u);        // cbSize
    t21AppendTag(b, "fact");
    t21AppendLe32(b, 4u);
    t21AppendLe32(b, static_cast<std::uint32_t>(frames));
    t21AppendTag(b, "data");
    t21AppendLe32(b, static_cast<std::uint32_t>(dataBytes));
    for (std::size_t i = 0; i < L.size(); ++i) {
        t21AppendLe32(b, std::bit_cast<std::uint32_t>(L[i]));
        t21AppendLe32(b, std::bit_cast<std::uint32_t>(R[i]));
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

[[nodiscard]] std::size_t t21Samples(double seconds) noexcept {
    return static_cast<std::size_t>(std::llround(seconds * kT13Fs));
}

/// Renders one segment through a fresh engine and appends it (plus tail and gap) to L / R.
void t21RenderSegment(const T21Segment& s, std::vector<float>& L, std::vector<float>& R) {
    INFO("segment " << s.label);
    REQUIRE(isValidMorphState(s.a));
    REQUIRE(isValidMorphState(s.b));
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, s.a));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, s.b));
    e->setMorphPosition(0.0f);
    e->setMorphRampMs(kT21SwitchRampMs);
    e->setVoiceMode(ProfundumEngine::VoiceMode::Mono);
    e->setPolyphony(1);
    e->prepare(kT13Fs);
    e->noteOn(s.note, kT13Velocity);

    const std::size_t hold = t21Samples(s.holdS);
    const std::size_t total = hold + t21Samples(kT21TailS);
    const std::size_t retrigger = s.retriggerS > 0.0 ? t21Samples(s.retriggerS) : 0;
    const std::size_t toggle = s.toggleS > 0.0 ? t21Samples(s.toggleS) : 0;
    std::size_t nextRetrigger = retrigger > 0 ? retrigger : total;
    std::size_t nextToggle = toggle > 0 ? toggle : total;
    float position = 0.0f;
    bool released = false;
    std::array<float, kT21Block> l{};
    std::array<float, kT21Block> r{};
    std::size_t pos = 0;
    while (pos < total) {
        if (!released && pos >= nextRetrigger && pos < hold) {
            e->noteOff(s.note);
            e->noteOn(s.note, kT13Velocity);
            nextRetrigger += retrigger;
        }
        if (!released && pos >= nextToggle && pos < hold) {
            position = 1.0f - position;
            e->setMorphPosition(position);
            nextToggle += toggle;
        }
        if (!released && pos >= hold) {
            e->noteOff(s.note);
            released = true;
        }
        const std::size_t m = std::min(kT21Block, total - pos);
        e->processBlock(l.data(), r.data(), m);
        L.insert(L.end(), l.begin(), l.begin() + static_cast<std::ptrdiff_t>(m));
        R.insert(R.end(), r.begin(), r.begin() + static_cast<std::ptrdiff_t>(m));
        pos += m;
    }
    L.insert(L.end(), t21Samples(kT21GapS), 0.0f);
    R.insert(R.end(), t21Samples(kT21GapS), 0.0f);
}

[[nodiscard]] T21Segment t21Hold(std::string label, const MorphState& s, std::uint8_t note, double holdS,
                                 double retriggerS = 0.0) {
    T21Segment seg;
    seg.label = std::move(label);
    seg.a = s;
    seg.b = s;
    seg.note = note;
    seg.holdS = holdS;
    seg.retriggerS = retriggerS;
    return seg;
}

[[nodiscard]] T21Segment t21Switch(std::string label, const MorphState& a, const MorphState& b, std::uint8_t note) {
    T21Segment seg;
    seg.label = std::move(label);
    seg.a = a;
    seg.b = b;
    seg.note = note;
    seg.holdS = 6.0;
    seg.toggleS = 1.0;   // A -> B at 1 s, back at 2 s, ... five switches
    return seg;
}

[[nodiscard]] std::vector<T21Row> t21Rows() {
    using Alg = BassCharacterStage::Algorithm;
    using AW = AnchorOscillator::Waveform;
    const MorphState base = t13StateAs();
    std::vector<T21Row> rows;

    {   // H-1: excitation split hump (+1.2 ... +1.6 dB at 500-550 Hz on the excitation's linear path)
        T21Row row{.id = "H-1", .file = "h01-split-hump.wav", .segments = {}};
        MorphState off = base;
        off.recipe = SpectralShapeRecipe::kSawAnchor;
        off.excitation.mix = 0.0f;
        MorphState on = off;
        on.excitation.mix = 1.0f;
        row.segments.push_back(t21Hold("E1 saw, excitation mix 0", off, kT13NoteE1, 3.0));
        row.segments.push_back(t21Hold("E1 saw, excitation mix 1", on, kT13NoteE1, 3.0));
        row.segments.push_back(t21Hold("A1 saw, excitation mix 0", off, kT13NoteA1, 3.0));
        row.segments.push_back(t21Hold("A1 saw, excitation mix 1", on, kT13NoteA1, 3.0));
        rows.push_back(std::move(row));
    }
    {   // H-2: C3 through the Fold, 60 s (the 0.59 deg / 60 s "breathing")
        T21Row row{.id = "H-2", .file = "h02-fold-breathing-c3.wav", .segments = {}};
        MorphState s = base;
        s.recipe = SpectralShapeRecipe::kSawAnchor;
        s.foldAmount = 100.0f;
        row.segments.push_back(t21Hold("C3 saw, Fold 100, held 60 s", s, kT13NoteC3, 60.0));
        rows.push_back(std::move(row));
    }
    {   // H-8: Anchor raw level swing on hard snaps (E0, Drop A 48 st, T 10 ms)
        T21Row row{.id = "H-8", .file = "h08-drop-snaps-e0.wav", .segments = {}};
        MorphState hard = base;
        hard.dropAmountSt = 48.0f;
        hard.dropTimeMs = 10.0f;
        MorphState soft = hard;
        soft.dropTimeMs = DropEnvelope::kDefaultTimeMs;
        row.segments.push_back(t21Hold("E0, Drop 48 st T 10 ms, retrigger every 1 s", hard, kT13NoteE0, 8.0, 1.0));
        row.segments.push_back(
            t21Hold("E0, Drop 48 st T 100 ms (reference), retrigger every 1 s", soft, kT13NoteE0, 8.0, 1.0));
        rows.push_back(std::move(row));
    }
    {   // H-10: a note after a Drop vs a fresh note (waveform-dependent stages on)
        T21Row row{.id = "H-10", .file = "h10-after-drop-vs-fresh.wav", .segments = {}};
        MorphState fresh = base;
        fresh.recipe = SpectralShapeRecipe::kSawAnchor;
        fresh.foldAmount = 100.0f;
        fresh.characterAlgorithm = Alg::Tube;
        fresh.characterAmount = 1.0f;
        fresh.dropAmountSt = 0.0f;
        MorphState dropped = fresh;
        dropped.dropAmountSt = 24.0f;
        dropped.dropTimeMs = DropEnvelope::kDefaultTimeMs;
        row.segments.push_back(t21Hold("E1 fresh note (no Drop)", fresh, kT13NoteE1, 3.0));
        row.segments.push_back(t21Hold("E1 after a 24 st / 100 ms Drop", dropped, kT13NoteE1, 3.0));
        rows.push_back(std::move(row));
    }
    {   // H-14: sub-only voice at Mass 100 vs the reference and saw voices
        T21Row row{.id = "H-14", .file = "h14-mass100-voices.wav", .segments = {}};
        MorphState sub = base;
        sub.mass = 100.0f;   // A_s: kSineAnchor, excitation 0, Fold 0, Character 0, Anchor level 1
        MorphState reference = sub;
        reference.recipe = SpectralShapeRecipe::kDefaultControls;
        MorphState saw = sub;
        saw.recipe = SpectralShapeRecipe::kSawAnchor;
        row.segments.push_back(
            t21Hold("E1 sub-only (kSineAnchor), Mass 100, retrigger at 2 s", sub, kT13NoteE1, 4.0, 2.0));
        row.segments.push_back(t21Hold("E1 reference (kDefaultControls), Mass 100, retrigger at 2 s", reference,
                                       kT13NoteE1, 4.0, 2.0));
        row.segments.push_back(t21Hold("E1 saw (kSawAnchor), Mass 100, retrigger at 2 s", saw, kT13NoteE1, 4.0, 2.0));
        rows.push_back(std::move(row));
    }
    {   // H-19: Fold / Rectify at the WIDE offset ceilings (Spread 1), with and without the Anchor
        T21Row row{.id = "H-19", .file = "h19-wide-ceiling-fold-rectify.wav", .segments = {}};
        MorphState s = base;
        s.recipe = SpectralShapeRecipe::kSawAnchor;
        s.characterAmount = 1.0f;
        s.field.mode = BassStereoField::ImageMode::Wide;
        s.field.spread = 1.0f;
        for (const Alg alg : {Alg::Fold, Alg::Rectify}) {
            for (const float level : {1.0f, 0.0f}) {
                MorphState v = s;
                v.characterAlgorithm = alg;
                v.anchor.level = level;
                row.segments.push_back(t21Hold(std::string("E1 WIDE Spread 1, ") + t13AlgName(alg) +
                                                   " Amount 1, Anchor level " + (level > 0.5f ? "1" : "0"),
                                               v, kT13NoteE1, 3.0));
            }
        }
        rows.push_back(std::move(row));
    }
    {   // H-28 (1): Fracture -> Crush / Rectify at C3 (OQ-9: listened, not re-measured)
        T21Row row{.id = "H-28a", .file = "h28a-fracture-switch-c3.wav", .segments = {}};
        MorphState from = base;
        from.recipe = SpectralShapeRecipe::kSawAnchor;
        from.characterAmount = 1.0f;
        from.characterAlgorithm = Alg::Fracture;
        for (const Alg to : {Alg::Crush, Alg::Rectify}) {
            MorphState b = from;
            b.characterAlgorithm = to;
            row.segments.push_back(t21Switch(std::string("C3 Fracture <-> ") + t13AlgName(to) +
                                                 " (Amount 1, 5 ms ramp, switch every 1 s)",
                                             from, b, kT13NoteC3));
        }
        rows.push_back(std::move(row));
    }
    {   // H-28 (2): OctaveDownSine -> each other Anchor waveform at E1
        T21Row row{.id = "H-28b", .file = "h28b-octavedown-switch-e1.wav", .segments = {}};
        MorphState from = base;
        from.anchor.waveform = AW::OctaveDownSine;
        const std::array<std::pair<AW, const char*>, 4> to{{{AW::Sine, "Sine"},
                                                            {AW::Triangle, "Triangle"},
                                                            {AW::RoundedSine, "RoundedSine"},
                                                            {AW::AsymmetricSine, "AsymmetricSine"}}};
        for (const auto& [w, name] : to) {
            MorphState b = from;
            b.anchor.waveform = w;
            row.segments.push_back(t21Switch(std::string("E1 Anchor OctaveDownSine <-> ") + name +
                                                 " (5 ms ramp, switch every 1 s)",
                                             from, b, kT13NoteE1));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace

// Hidden; run explicitly: dsp_systems_tests.exe "VoiceEngine_ListeningRenders" (evidence/close/listen.sh).
TEST_CASE("VoiceEngine_ListeningRenders", "[systems][profundum][voice-engine][.listen]") {
    const std::filesystem::path dir = std::filesystem::path(p3EvidenceDir()) / "listen";
    std::ostringstream manifest;
    manifest << "# T021 VoiceEngine_ListeningRenders: ProfundumEngine, MONO, 48 kHz, block 64, 32-bit float stereo.\n"
             << "# Each segment: note-on at its start, hold, note-off, " << kT21TailS << " s tail, " << kT21GapS
             << " s silence.\n";
    std::size_t written = 0;
    for (const T21Row& row : t21Rows()) {
        std::vector<float> L;
        std::vector<float> R;
        manifest << row.id << " " << row.file << "\n";
        for (const T21Segment& seg : row.segments) {
            manifest << "  start " << std::fixed << std::setprecision(2) << static_cast<double>(L.size()) / kT13Fs
                     << " s  hold " << seg.holdS << " s  " << seg.label << "\n";
            t21RenderSegment(seg, L, R);
        }
        float peak = 0.0f;
        bool finite = true;
        for (std::size_t i = 0; i < L.size(); ++i) {
            finite = finite && t21Finite(L[i]) && t21Finite(R[i]);
            peak = std::max({peak, std::fabs(L[i]), std::fabs(R[i])});
        }
        const double peakDb = peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak)) : -999.0;
        std::ostringstream line;
        line << "LISTEN " << row.id << " " << row.file << " seconds=" << std::fixed << std::setprecision(2)
             << static_cast<double>(L.size()) / kT13Fs << " peak_dbfs=" << peakDb
             << " finite=" << (finite ? "yes" : "NO");
        std::cout << line.str() << "\n";
        manifest << "  " << line.str() << "\n";
        CHECK(finite);
        INFO("WAV " << (dir / row.file).string());
        CHECK(t21WriteWav(dir / row.file, L, R, kT13Fs));
        ++written;
    }
    t13WriteEvidence("listen/manifest.txt", manifest.str());
    std::cout << "LISTEN DONE " << written << " WAVs in " << dir.string() << "\n";
    CHECK(written == 8);
}

// =============================================================================
// SC-021(b) as ruled (G14-4, user ruling 2026-10-21): the oversampled sidechain's own aliasing, standalone
// =============================================================================

TEST_CASE("VoiceEngine_DynamicsAliasingGrid", "[systems][profundum][voice-engine][sc-021][mass-dynamics]") {
    enableFTZDAZ();
    constexpr double kBarDbfs = -80.0;
    std::ostringstream out;
    out << std::setprecision(6);
    out << "# SC-021(b) (G14-4): MassDynamics standalone on Phase 6's Fold-100 saw at each note and rate, release pinned\n"
        << "# (kP3FixedReleaseMs), shaper +12 dB, compression 1, detector keyed at f0; the opt-in on at 4x is gated\n"
        << "# <= -80 dBFS on the SC-021 off-lattice mask; off and on2x recorded. The engine output per cell is\n"
        << "# recorded by the VoiceEngine_Aliasing_* shards (limiter gain riding and the program-dependent release).\n";
    double worst = -300.0;
    for (const int midi : {16, 28, 40}) {   // E0, E1, E2
        const double f0 = static_cast<double>(Krate::DSP::midiNoteToFrequency(midi));
        for (const double fs : {44100.0, 48000.0, 96000.0}) {
            const P3Inputs in = p3MakeInputs(fs, f0);
            const double off = p3SteadyEstimator(in, fs, f0, TransientShaper::kMaxGainDb, 0.0f, 1.0f, false);
            const double on2 = p3SteadyEstimator(in, fs, f0, TransientShaper::kMaxGainDb, 0.0f, 1.0f, true, 2);
            const double on4 = p3SteadyEstimator(in, fs, f0, TransientShaper::kMaxGainDb, 0.0f, 1.0f, true, 4);
            out << "SC021B midi=" << midi << " fs=" << fs << " off=" << off << " on2x=" << on2 << " on4x=" << on4
                << (on4 <= kBarDbfs ? "  green" : "  RED") << "\n";
            INFO("SC-021(b) midi " << midi << " fs " << fs << ": on4x " << on4 << " dBFS (bar " << kBarDbfs << ")");
            CHECK(std::isfinite(on4));
            CHECK(on4 <= kBarDbfs);
            worst = std::max(worst, on4);
        }
    }
    out << "SC021B VERDICT " << (worst <= kBarDbfs ? "GREEN" : "RED") << " worst_on4x=" << worst << "\n";
    std::cout << out.str();
    std::ofstream file(p3EvidenceDir() + "sc021-dynamics-grid.txt");
    file << out.str();
    CHECK(file.good());
}
