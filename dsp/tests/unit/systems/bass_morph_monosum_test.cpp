// ==============================================================================
// Layer 3: System Tests - BassMorph mono sum
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T012 (SC-016; FR-085). Mono compatibility along the showcase morph, through Phase 7's helpers:
//   BassMorph_MonoSum_<E1|C3>                  [long]  21 static p + one 0 -> 1 ramp (SOLID -> WIDE): the Anchor
//                                                      tap bitwise L == R; StereoFieldTest::evaluateMonoSumCell /
//                                                      monoSumCellPasses with the resolved (landed) field.
//   BassMorph_MonoSum_AnchorThroughTail_<E1|C3> [long] the same renders with muteCharacter: the output's
//                                                      side-to-mid over the read window at the floor (-120 dB);
//                                                      positive control: the character unmuted at p = 1 >= -60 dB.
//   BassMorph_MonoSumSmoke                             E1, p in {0, 0.5, 1}: both reads and the positive control
//                                                      (deviation 5: the gates exclude [long]).
// Recorded decisions (evidence/run-log/T012.txt):
//   - Every render is a steady render (ampEnvelope = false): showcasePair()'s A has ampEnv sustain 0, so an
//     enveloped p = 0 render is silent in the read window and its sub band would not be read (a vacuous pass).
//   - The FieldChainConfig handed to evaluateMonoSumCell carries chain.f0 / chain.fs, field = the resolved field
//     and algorithm = the resolved characterAlgorithm (the morph chain always runs the character stage, so the
//     cell is a nonlinear arm in Phase 7's G8-1 sense). The cell flags are printed and written to the CSV.
//   - The ramp render's cell is read with the landed state's field; evaluateMonoSumCell's window (field motion
//     depth 0) is [kSettleSeconds * fs, end), which the test REQUIREs starts after the SOLID -> WIDE transition
//     end (g_sw + one period + 334 intervals, deviation 10). The transition itself is covered by the Anchor tap's
//     bit identity and (AnchorThroughTail) the whole-render outL == outR print.
// FTZ / DAZ: Krate::DSP::ScopedDenormalMode per render case (tasks.md rule 9: set for the scope, restored on exit).
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/scoped_denormal_mode.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
namespace Sft = Krate::DSP::StereoFieldTest;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::BassStereoField;

constexpr double kBmmFs = 48000.0;
constexpr BassMorphEngine::AnchorMorphMode kBmmMode = BassMorphEngine::AnchorMorphMode::Blend;
constexpr double kBmmRampEventSeconds = 0.25;
constexpr double kBmmPositiveControlBarDb = -60.0;
constexpr const char* kBmmTailCsvHeader = "note,p,kind,out_side_to_mid_db,out_bit_equal,anchor_bit_equal,pass\n";

struct BmmNote {
    const char* name;
    double f0;
};

BmmNote bmmE1() {
    return {"E1", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28))};
}

BmmNote bmmC3() {
    return {"C3", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(48))};
}

/// tasks.md rule 11: evidence goes under specs/profundum-phase8-bass-morph/evidence/.
void bmmWriteEvidence(const std::string& name, const std::string& text) {
    const std::string dir = std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + name);
    out << text;
}

/// The 21 static positions p = i / 20.
std::vector<float> bmmGrid21() {
    std::vector<float> ps;
    for (int i = 0; i <= 20; ++i)
        ps.push_back(static_cast<float>(i) / 20.0f);
    return ps;
}

std::string bmmPLabel(float p) {
    std::ostringstream os;
    os << std::setprecision(6) << p;
    return os.str();
}

/// One render of showcasePair() (Blend, 48 kHz, host block 512, steady). ramp: initialPosition 0 and a Position 1
/// event at 0.25 s at the default ramp; otherwise initialPosition = p (snapped by prepare's reset()).
Bmt::MorphChainTaps bmmRender(const BmmNote& note, float p, bool ramp, bool muteCharacter) {
    const auto pair = Bmt::showcasePair();
    Bmt::MorphChainConfig cfg;
    cfg.a = pair.first;
    cfg.b = pair.second;
    cfg.mode = kBmmMode;
    cfg.f0 = note.f0;
    cfg.fs = kBmmFs;
    cfg.ampEnvelope = false;
    cfg.muteCharacter = muteCharacter;
    if (ramp) {
        cfg.initialPosition = 0.0f;
        cfg.automation.push_back(Bmt::MorphEvent{
            .sample = static_cast<std::size_t>(std::llround(kBmmRampEventSeconds * kBmmFs)),
            .kind = Bmt::MorphEvent::Kind::Position,
            .value = 1.0f});
    } else {
        cfg.initialPosition = p;
    }
    Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);
    REQUIRE(!t.resolvedPerInterval.empty());
    return t;
}

/// The ramp render's record: the landing sample, the SOLID -> WIDE switch g_sw, its transition end and the
/// mono-sum cell's window.
struct BmmRampSpan {
    std::size_t landed = 0;
    std::size_t gSw = 0;
    std::size_t transitionEnd = 0;
    std::size_t windowStart = 0;
    std::size_t windowEnd = 0;
};

BmmRampSpan bmmRampSpan(const Bmt::MorphChainTaps& t, const BmmNote& note) {
    BmmRampSpan s;
    std::optional<std::size_t> landed;
    for (std::size_t i = 0; i < t.resolvedPerInterval.size(); ++i) {
        const BassMorphEngine::Resolved& r = t.resolvedPerInterval[i];
        if (r.position == 1.0f && r.anchorPosition == BassMorphEngine::curve(kBmmMode, r.position)) {
            landed = t.advanceSamples[i];
            break;
        }
    }
    INFO(note.name << " ramp: landing at p = 1 (absent = the engine never landed)");
    REQUIRE(landed.has_value());
    s.landed = *landed;
    std::optional<std::size_t> gSw;
    for (const auto& c : t.discreteSetterCalls) {
        if (c.leaf == Bmt::kLeafFieldMode) {
            gSw = c.sample;
            break;
        }
    }
    INFO(note.name << " ramp: field.mode issue (SOLID -> WIDE expected)");
    REQUIRE(gSw.has_value());
    s.gSw = *gSw;
    s.transitionEnd = s.gSw + Bmt::transitionEndSamples(Bmt::TransitionKind::FieldMode, note.f0, kBmmFs);
    s.windowStart = static_cast<std::size_t>(std::llround(Krate::DSP::MassDynamicsTest::kSettleSeconds * kBmmFs));
    s.windowEnd = t.outL.size();
    return s;
}

/// The Phase 7 cell config for a render: f0 / fs, the resolved field and character algorithm of @p r.
Sft::FieldChainConfig bmmFieldCfg(const BassMorphEngine::Resolved& r, const BmmNote& note) {
    Sft::FieldChainConfig fc;
    fc.chain.f0 = note.f0;
    fc.chain.fs = kBmmFs;
    fc.field = r.state.field;
    fc.algorithm = r.state.characterAlgorithm;
    return fc;
}

std::string bmmMonoSumCsvHeader() {
    const std::string h = Sft::monoSumCsvHeader();   // "cell,tap,..."
    return "note,p,pass,anti_phase_exempt,sub_in_band,nonlinear_arm," + h.substr(h.find(',') + 1) + "\n";
}

/// SC-016 mono-sum read of one render: the Anchor tap bit identity (REQUIRE) and the cell (CHECK; returns pass).
bool bmmMonoSumRead(const Bmt::MorphChainTaps& t, const BmmNote& note, const std::string& pLabel,
                    std::ostringstream& csv) {
    INFO(note.name << " p " << pLabel);
    // SC-016: the Anchor tap is bitwise L == R (the fixture writes the mono Anchor to both channels).
    REQUIRE(Sft::bitEqual(t.anchorL, t.anchorR));
    const BassMorphEngine::Resolved& landed = t.resolvedPerInterval.back();
    const Sft::FieldChainConfig fc = bmmFieldCfg(landed, note);
    const Sft::MonoSumCell cell = Sft::evaluateMonoSumCell(t, fc);
    const bool pass = Sft::monoSumCellPasses(cell);
    const Sft::BandReading& core = cell.taps[static_cast<std::size_t>(Sft::MonoSumTap::Core)].sub;
    const Sft::BandReading& stage = cell.taps[static_cast<std::size_t>(Sft::MonoSumTap::StageOut)].sub;
    INFO(std::setprecision(6) << "fc " << cell.crossoverHz << " Hz, mode " << static_cast<int>(landed.state.field.mode)
                              << ", spread " << landed.state.field.spread << " | core Q_sub " << core.q << " dB rho "
                              << core.rho << " valid " << core.valid << " | stageOut Q_sub " << stage.q << " dB rho "
                              << stage.rho << " valid " << stage.valid << " | nonlinearArm " << cell.nonlinearArm
                              << " subInBand " << cell.subHarmonicInBand << " antiPhaseExempt "
                              << cell.antiPhaseExempt);
    CHECK(pass);
    std::ostringstream label;
    label << std::setprecision(6) << note.name << ',' << pLabel << ',' << (pass ? 1 : 0) << ','
          << (cell.antiPhaseExempt ? 1 : 0) << ',' << (cell.subHarmonicInBand ? 1 : 0) << ','
          << (cell.nonlinearArm ? 1 : 0);
    csv << Sft::monoSumCsvLine(cell, label.str()) << '\n';
    return pass;
}

/// The output side-to-mid over the read window, 20 Hz .. fs / 2 (dB, floored at kFloorDb).
double bmmOutSideToMidDb(const Bmt::MorphChainTaps& t) {
    const Bmt::ReadWindow w = Bmt::readWindow(t.outL.size(), kBmmFs);
    return Sft::bandReading(t.outL.data() + w.start, t.outR.data() + w.start, w.length, kBmmFs, 20.0, kBmmFs / 2.0)
        .sideToMidDb;
}

/// SC-016 discriminating read of one muted render: the output side-to-mid at the floor (CHECK; returns pass).
bool bmmAnchorTailRead(const Bmt::MorphChainTaps& t, const BmmNote& note, const std::string& pLabel,
                       std::ostringstream& csv) {
    const double s2m = bmmOutSideToMidDb(t);
    const bool outEqual = Sft::bitEqual(t.outL, t.outR);
    const bool anchorEqual = Sft::bitEqual(t.anchorL, t.anchorR);
    INFO(std::setprecision(6) << note.name << " p " << pLabel << " (character muted): out side-to-mid " << s2m
                              << " dB (bar <= " << Sft::kFloorDb << "), outL == outR bitwise " << outEqual
                              << ", anchorL == anchorR bitwise " << anchorEqual);
    const bool pass = s2m <= Sft::kFloorDb;
    CHECK(pass);
    csv << std::setprecision(6) << note.name << ',' << pLabel << ",muted," << s2m << ',' << (outEqual ? 1 : 0) << ','
        << (anchorEqual ? 1 : 0) << ',' << (pass ? 1 : 0) << '\n';
    return pass;
}

/// Positive control: the unmuted p = 1 render (WIDE, Spread 1) must read >= -60 dB, so the read can fail.
bool bmmPositiveControl(const Bmt::MorphChainTaps& unmutedP1, const BmmNote& note, std::ostringstream& csv) {
    REQUIRE(unmutedP1.resolvedPerInterval.back().state.field.mode == BassStereoField::ImageMode::Wide);
    const double s2m = bmmOutSideToMidDb(unmutedP1);
    INFO(std::setprecision(6) << note.name << " positive control p 1 (character unmuted): out side-to-mid " << s2m
                              << " dB (bar >= " << kBmmPositiveControlBarDb << ")");
    const bool pass = s2m >= kBmmPositiveControlBarDb;
    CHECK(pass);
    csv << std::setprecision(6) << note.name << ",1,unmuted_control," << s2m << ','
        << (Sft::bitEqual(unmutedP1.outL, unmutedP1.outR) ? 1 : 0) << ','
        << (Sft::bitEqual(unmutedP1.anchorL, unmutedP1.anchorR) ? 1 : 0) << ',' << (pass ? 1 : 0) << '\n';
    return pass;
}

/// BassMorph_MonoSum_<note>: 21 static p + the ramp.
void bmmMonoSumCase(const BmmNote& note) {
    const Krate::DSP::ScopedDenormalMode ftz;   // tasks.md rule 9
    std::ostringstream csv;
    csv << bmmMonoSumCsvHeader();
    std::size_t failures = 0;
    for (const float p : bmmGrid21()) {
        const Bmt::MorphChainTaps t = bmmRender(note, p, false, false);
        if (!bmmMonoSumRead(t, note, bmmPLabel(p), csv))
            ++failures;
    }
    {
        const Bmt::MorphChainTaps t = bmmRender(note, 0.0f, true, false);
        const BmmRampSpan s = bmmRampSpan(t, note);
        INFO(note.name << " ramp: landed at sample " << s.landed << ", SOLID -> WIDE g_sw " << s.gSw
                       << ", transition end " << s.transitionEnd << ", cell window [" << s.windowStart << ", "
                       << s.windowEnd << ")");
        REQUIRE(t.resolvedPerInterval.back().state.field.mode == BassStereoField::ImageMode::Wide);
        REQUIRE(s.transitionEnd <= s.windowStart);   // the landed-field cell reads only the landed state
        if (!bmmMonoSumRead(t, note, "ramp", csv))
            ++failures;
        csv << "# ramp span: landed " << s.landed << ", g_sw " << s.gSw << ", transition end " << s.transitionEnd
            << ", cell window [" << s.windowStart << ", " << s.windowEnd << ")\n";
    }
    bmmWriteEvidence(std::string("sc016-monosum-") + note.name + ".csv", csv.str());
    REQUIRE(failures == 0);
}

/// BassMorph_MonoSum_AnchorThroughTail_<note>: the same renders with muteCharacter + the positive control.
void bmmAnchorTailCase(const BmmNote& note) {
    const Krate::DSP::ScopedDenormalMode ftz;   // tasks.md rule 9
    std::ostringstream csv;
    csv << kBmmTailCsvHeader;
    std::size_t failures = 0;
    for (const float p : bmmGrid21()) {
        const Bmt::MorphChainTaps t = bmmRender(note, p, false, true);
        if (!bmmAnchorTailRead(t, note, bmmPLabel(p), csv))
            ++failures;
    }
    {
        const Bmt::MorphChainTaps t = bmmRender(note, 0.0f, true, true);
        if (!bmmAnchorTailRead(t, note, "ramp", csv))
            ++failures;
    }
    if (!bmmPositiveControl(bmmRender(note, 1.0f, false, false), note, csv))
        ++failures;
    bmmWriteEvidence(std::string("sc016-monosum-") + note.name + "-anchor-tail.csv", csv.str());
    REQUIRE(failures == 0);
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-016 (FR-085): mono compatibility along the showcase morph
// -----------------------------------------------------------------------------

// SC-016
TEST_CASE("BassMorph_MonoSum_E1", "[systems][profundum][bass-morph][sc-016][long]") {
    bmmMonoSumCase(bmmE1());
}

// SC-016
TEST_CASE("BassMorph_MonoSum_C3", "[systems][profundum][bass-morph][sc-016][long]") {
    bmmMonoSumCase(bmmC3());
}

// SC-016 (discriminating read)
TEST_CASE("BassMorph_MonoSum_AnchorThroughTail_E1", "[systems][profundum][bass-morph][sc-016][long]") {
    bmmAnchorTailCase(bmmE1());
}

// SC-016 (discriminating read)
TEST_CASE("BassMorph_MonoSum_AnchorThroughTail_C3", "[systems][profundum][bass-morph][sc-016][long]") {
    bmmAnchorTailCase(bmmC3());
}

// SC-016 (deviation 5: the per-push sibling of the four [long] cases)
TEST_CASE("BassMorph_MonoSumSmoke", "[systems][profundum][bass-morph][sc-016]") {
    const Krate::DSP::ScopedDenormalMode ftz;   // tasks.md rule 9
    const BmmNote note = bmmE1();
    std::ostringstream monoCsv;
    monoCsv << bmmMonoSumCsvHeader();
    std::ostringstream tailCsv;
    tailCsv << kBmmTailCsvHeader;
    std::size_t failures = 0;
    std::optional<Bmt::MorphChainTaps> unmutedP1;
    for (const float p : std::array<float, 3>{0.0f, 0.5f, 1.0f}) {
        Bmt::MorphChainTaps t = bmmRender(note, p, false, false);
        if (!bmmMonoSumRead(t, note, bmmPLabel(p), monoCsv))
            ++failures;
        if (p == 1.0f)
            unmutedP1 = std::move(t);
        const Bmt::MorphChainTaps m = bmmRender(note, p, false, true);
        if (!bmmAnchorTailRead(m, note, bmmPLabel(p), tailCsv))
            ++failures;
    }
    REQUIRE(unmutedP1.has_value());
    if (!bmmPositiveControl(*unmutedP1, note, tailCsv))   // the p = 1 render above: character unmuted
        ++failures;
    bmmWriteEvidence("sc016-monosum-E1-smoke.csv", monoCsv.str() + "\n" + tailCsv.str());
    REQUIRE(failures == 0);
}
