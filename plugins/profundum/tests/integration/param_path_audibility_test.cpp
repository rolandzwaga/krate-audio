// ==============================================================================
// Profundum - audibility through the parameter path (Phase 11 T023; FR-064, SC-018, SC-024)
// ==============================================================================
// Profundum_ParamPathAudibility [long]: the fifteen SC-018 rows, one SECTION each, at every (row, note) pair the
//   Phase 9 CSV gates. The base is loaded through setState, the lever moves only by a normalised parameter
//   change, the ported descriptor is read on the row's tap through setEngineObserverForTest
//   (param_path_audibility_helpers.h). Fidelity teeth first: the same row engine-direct must land within 10 % of
//   the CSV `value`, else "descriptor port drift" and the processor reading is not taken. Then reading >= bar.
//   Results go to specs/profundum-phase11-parameters/evidence/param-path-audibility.csv.
// Profundum_MassOffsetAudibility [long] (SC-024 "every Morph position"): both endpoints persisted at Mass 0,
//   Mod Wheel 0 -> 1 only; the AT-27 descriptor on dynL/R and sumL/R >= its bar at Morph Position 0, 0.5 and 1.
// ==============================================================================

#include "param_path_audibility_helpers.h"
#include "profundum_test_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

struct PpRowResult {
    const PpBarRow* bar = nullptr;
    PpReading direct{};
    PpReading host{};
    bool drift = false;
    bool pass = false;
};

/// Every gated (row, note) of `row`: engine-direct first (fidelity), then the processor path; CSV lines appended
/// for all notes before anything is asserted.
void ppRunRow(PpRow row) {
    std::vector<PpRowResult> results;
    for (const PpBarRow& b : kPpBars) {
        if (b.row != row) {
            continue;
        }
        PpRowResult res;
        res.bar = &b;
        res.direct = ppEvaluate(b, PpPath::EngineDirect);
        res.drift = !res.direct.valid ||   // a NaN reading counts as drift, as before (!(x <= tol), not x > tol)
                    !(std::abs(res.direct.value - b.phase9Value) <= kPpPortTolerance * std::abs(b.phase9Value));
        if (!res.drift) {
            res.host = ppEvaluate(b, PpPath::Processor);
        }
        res.pass = !res.drift && res.host.valid && auFinite(res.host.value) && res.host.value >= b.bar;
        ppCsvAppend(b, res.direct, res.host, res.drift, res.pass);
        results.push_back(res);
    }
    REQUIRE(!results.empty());
    for (const PpRowResult& res : results) {
        const PpBarRow& b = *res.bar;
        INFO(b.id << " " << auNoteName(b.note) << " (CSV line " << b.csvLine << "): " << b.descriptor << " on "
                  << b.tap << " | Phase 9 " << b.phase9Value << ", engine-direct " << res.direct.value << " (lo "
                  << res.direct.lo << ", hi " << res.direct.hi << "), param path " << res.host.value << " (lo "
                  << res.host.lo << ", hi " << res.host.hi << "), bar " << b.bar);
        if (res.drift) {
            FAIL_CHECK("descriptor port drift: the engine-direct reading is not within 10 % of the Phase 9 value");
            continue;
        }
        CHECK(res.host.valid);
        CHECK(res.host.value >= b.bar);
    }
}

}  // namespace

TEST_CASE("Profundum_ParamPathAudibility", "[profundum][audibility][long]") {
    static bool csvStarted = false;   // one header per process; each SECTION run appends its rows
    if (!csvStarted) {
        ppCsvBegin();
        csvStarted = true;
    }

    SECTION("AT-27 macro: Mass 104 0 -> 100 (dynL/R, sumL/R)") { ppRunRow(PpRow::AT27); }
    SECTION("AT-01 core: Excitation Mix 206 0 -> 1, Drive 207 = 1, Edge 204 = 1 (Char / twin)") { ppRunRow(PpRow::AT01); }
    SECTION("AT-10 anchor: Anchor Level 302 0 -> 1 (Sum)") { ppRunRow(PpRow::AT10); }
    SECTION("AT-11 anchor PUNCH: Anchor Mode 301 Lock -> Punch, Level 302 = 1 (Sum)") { ppRunRow(PpRow::AT11); }
    SECTION("AT-21 drop: Drop Shape 402 0 -> 1, Drop Amount 400 = 24 st (fCore / fRes)") { ppRunRow(PpRow::AT21); }
    SECTION("AT-23 glide: Portamento 412 0 -> 200 ms, Voice Mode 1 = Mono (fRes)") { ppRunRow(PpRow::AT23); }
    SECTION("AT-25 amp envelope: Release 423 50 -> 2000 ms (Sum)") { ppRunRow(PpRow::AT25); }
    SECTION("AT-43 note memory: Note Memory 415 index 2 -> 0, Retrigger 416 = Reset (core)") { ppRunRow(PpRow::AT43); }
    SECTION("AT-03 fold: Harmonic Fold 500 0 -> 100 (Char / twin)") { ppRunRow(PpRow::AT03); }
    SECTION("AT-02 shape: Shape 501 -> 1 on the per-row recipe (Sum)") { ppRunRow(PpRow::AT02); }
    SECTION("AT-05 character: Character Amount 601 0 -> 1, Character 600 = Wavefold (Char / twin)") { ppRunRow(PpRow::AT05); }
    SECTION("AT-28 dynamics: Transient Attack 700 0 -> +12 dB (dynL/R)") { ppRunRow(PpRow::AT28); }
    SECTION("AT-33 field: Field Motion Depth 805 0 -> 1, Image Mode 800 = Spread, Spread 801 = 1 (fieldL/R)") { ppRunRow(PpRow::AT33); }
    SECTION("AT-38 motion: Motion Source 900 Off -> Cycle, Shift / 1 / 1 / 0.1 Hz (Sum)") { ppRunRow(PpRow::AT38); }
    SECTION("AT-42 morph: Morph Position 105 0 -> 0.5, A = A_s, B = B (Char / twin)") { ppRunRow(PpRow::AT42); }
}

TEST_CASE("Profundum_MassOffsetAudibility", "[profundum][audibility][long]") {
    // The AT-27 bar (CSV line 106, `bar` ">= 3 dB"), the same descriptor and taps (SC-024).
    const PpBarRow& at27 = kPpBars[0];
    REQUIRE(at27.row == PpRow::AT27);
    REQUIRE(at27.note == kAuE1);

    const auto start = std::chrono::steady_clock::now();
    for (const float position : {0.0f, 0.5f, 1.0f}) {
        const double lo = ppMassOffsetDlk(position, 0.0);
        const double hi = ppMassOffsetDlk(position, 1.0);
        const double value = std::abs(hi - lo);
        std::cout << "Profundum_MassOffsetAudibility position " << position << ": dL_K lo " << lo << " hi " << hi
                  << " value " << value << " bar " << at27.bar << '\n';
        INFO("Morph Position " << position << ": dL_K (Mod Wheel 0) " << lo << " dB, (Mod Wheel 1) " << hi
                               << " dB, |diff| " << value << " dB, bar " << at27.bar << " dB");
        CHECK(auFinite(value));
        CHECK(value >= at27.bar);
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Profundum_MassOffsetAudibility wall clock " << seconds << " s\n" << std::flush;
}

TEST_CASE("Profundum_PrepareHoldsLoadedEndpoints", "[profundum][audibility][state]") {
    // FR-042 / FR-064 (SC-011, SC-018 AT-11): endpoints loaded before setupProcessing are in force when the engine
    // is prepared, so the first block renders them with no glide from the engine's defaults. Teeth: the processor
    // path's Sum-tap E_sub over the first 40 ms (the AT-11 window) equals the engine-direct render of the same
    // state, in both AT-11 arms (LOCK and PUNCH) at E1.
    namespace P = ::Profundum;
    const double f0 = auHz(kAuE1);
    const auto len = static_cast<std::size_t>(std::llround(40.0 * kAuFs / 1000.0));
    for (const bool punch : {false, true}) {
        Kd::MorphState s = auStateAs();
        s.anchor.level = 1.0f;
        s.anchor.mode = punch ? Kd::AnchorOscillator::Mode::Punch : Kd::AnchorOscillator::Mode::Lock;
        const double modeIndex = static_cast<double>(static_cast<int>(s.anchor.mode));
        const PpArm arm = ppArm(auOne(s), kAuE1, auAt(0.1), 0, auStateAs(), auStateAs(),
                                {{.id = P::kAnchorLevelId, .norm = ppNorm(P::kAnchorLevelId, 1.0)},
                                 {.id = P::kAnchorModeId, .norm = ppNorm(P::kAnchorModeId, modeIndex)}});
        const auPt::VoiceEngineTaps direct = ppRender(PpPath::EngineDirect, arm, false);
        const auPt::VoiceEngineTaps host = ppRender(PpPath::Processor, arm, false);
        const double dDirect = auWindowESubDb(direct.sumL, direct.sumR, 0, len, f0);
        const double dHost = auWindowESubDb(host.sumL, host.sumR, 0, len, f0);
        INFO((punch ? "PUNCH" : "LOCK") << ": E_sub [0, 40 ms) engine-direct " << dDirect << " dB, processor "
                                        << dHost << " dB");
        CHECK(auFinite(dHost));
        CHECK(std::abs(dHost - dDirect) <= 0.01);
    }
}
