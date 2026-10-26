// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_audibility_test.cpp
// ==============================================================================
// tasks.md T016 (SC-010, SC-007, SC-009; FR-080; plan C-8, C-17, S9.11). Created as a stub by T001.
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on.
//
//   VoiceEngine_AudibilityTable_{Core,Character,Anchor,Pitch,Envelope,Mass,Field,Motion,Morph,NoteMemory,Macros}
//       [sc-010][long]: every FR-080 row AT-01 ... AT-50 at E1 and C3 on the row's base (A -> A_s, C-8; AT-48 on
//       B_s, ruling B10-1, with its A_s reading in a second, ungated line), contrast, descriptor and tap as the spec
//       table writes them. Bars: dB >= 3, D_br >= 0.25 oct, time ratio >= 2, Anchor dE_sub >= 2 dB, Drop rows by
//       Phase 4 SC-021 (start >= 6 st, t_s ratio >= 2, alpha >= 0.1, rho >= 0.05 A), Note Memory >= 3 dB or
//       >= 30 deg, AT-13 within +-5 cents of the flipped ratio. D_nl is always the Character tap over the
//       forceLinearCore twin's core tap (voiceEngineRenderWithTwin). REQUIRE(pass) per gated cell, after the
//       shard's CSV is written (so a red cell never loses the rest of the table).
//       Envelope shard: the FR-009 ampStep record. Mass shard: the H-17 record (evidence/h17-poly-gr.txt).
//   VoiceEngine_AudibilityTableSmoke: AT-03, AT-10, AT-26 at E1 (same readers, same bars).
//   VoiceEngine_MacroMonotone [sc-007][long] (+ ...Smoke: Clean <-> Violent, E1, 5 steps).
//   VoiceEngine_MotionMassSwell [sc-009][long] (+ ...Smoke: 2 Hz, 3 s).
//
// Decisions (evidence/run-log/T016.txt):
//   - CSVs: one file per TEST_CASE under evidence/audibility-table/ (the shards run as concurrent processes in
//     the close lane; one shared audibility-table.csv would be a write race). Columns: row, contrast, base,
//     note, descriptor, tap, lo, hi, value, bar, pass, gated, out_descriptor, out_lo, out_hi, ampStep, remark.
//   - value: |hi - lo| (dB / oct rows) or max / min (time-ratio rows) for every non-macro row ("moves by");
//     the macro rows read the documented axis direction (hi - lo, hi / lo), as the T014 harvest did.
//   - D_st (Field rows) carries Phase 7's band validity (G8-1, -60 dBFS; Phase 8 B10-4): a band with no content
//     above 2 f_c is not a width reading, so a cell passes only when both readings are valid.
//   - AT-24 per-row setting (FR-080 "showcase inert", written into the spec table): plain A (decay 150 ms,
//     sustain 0), because on any sustain-1 base the 1 s gate hold dominates D_t and bounds the attack contrast
//     at <= 1015 / 515 = 1.97 < 2 (A_s and B_s alike). The A_s reading is recorded in an ungated line.
//   - AT-29, AT-31 ... AT-33 (D_st above 2 f_c on A_s's sine core) and AT-37 / AT-38 (Edge / Shift -> D_br on the
//     sine recipe, B10-1's measured 0.016 oct) are predicted inert on A_s. They are GATED on A_s as the spec
//     writes them (moving a row's base to B is a ruling, B10-1 precedent); their B_s reading is recorded in a
//     second, ungated line so the ruling has its measurement.
//   - Note Memory rows (AT-43 ... AT-46): on A_s (sustain 1, attack 1 ms) an envelope restart from sustain 1
//     re-attacks from 1 to 1 (ADSREnvelope::enterAttack starts from output_, adsr_envelope.h:451-463), so
//     every envelope-only Actions difference is silent. Each row therefore uses a pair whose resolved Actions
//     differ in the oscillator restart only (phase reset + its declick; envelope, Drop (amount 0) and Anchor
//     actions identical; note_memory.h kMatrix), and reads the core-h1 Goertzel phase jump (>= 30 deg). The
//     transition grid point is chosen so the predicted jump is near 180 deg (auPickTransition).
//   - H-17: the engine's public pitch API has one global bend and MIDI-note pitches (C-11), so four voices
//     detuned by cents around E1 are not reachable. Recorded instead: MONO E1, and POLY 4 voices on the
//     nearest distinct keys (E1, F1, F#1, G1), beside Phase 6's -10.1 dB; the deviation is written in the file.
// ==============================================================================

#include "profundum_audibility_readers.h"

#include <catch2/catch_test_macros.hpp>

using namespace Krate::DSP;
using namespace Krate::DSP::ProfundumAudibility;

// =============================================================================
// SC-010: the FR-080 audibility table, one [long] shard per row group
// =============================================================================

namespace {

using AuShard = void (*)(std::vector<AuCell>&, std::uint8_t);

void auRunShard(const std::string& caseName, AuShard shard) {
    enableFTZDAZ();
    std::vector<AuCell> cells;
    for (const std::uint8_t note : kAuNotes)
        shard(cells, note);
    auReport(caseName, cells);
}

}  // namespace

TEST_CASE("VoiceEngine_AudibilityTable_Core", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Core", auShardCore);
}

TEST_CASE("VoiceEngine_AudibilityTable_Character", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Character", auShardCharacter);
}

TEST_CASE("VoiceEngine_AudibilityTable_Anchor", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Anchor", auShardAnchor);
}

TEST_CASE("VoiceEngine_AudibilityTable_Pitch", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Pitch", auShardPitch);
}

TEST_CASE("VoiceEngine_AudibilityTable_Envelope", "[systems][profundum][voice-engine][sc-010][fr-009][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Envelope", auShardEnvelope);
}

TEST_CASE("VoiceEngine_AudibilityTable_Mass", "[systems][profundum][voice-engine][sc-010][long]") {
    enableFTZDAZ();
    auH17Record();   // Record (H-17), not gated; written before the gated cells so a red cell never loses it
    auRunShard("VoiceEngine_AudibilityTable_Mass", auShardMass);
}

TEST_CASE("VoiceEngine_AudibilityTable_Field", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Field", auShardField);
}

TEST_CASE("VoiceEngine_AudibilityTable_Motion", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Motion", auShardMotion);
}

TEST_CASE("VoiceEngine_AudibilityTable_Morph", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Morph", auShardMorph);
}

TEST_CASE("VoiceEngine_AudibilityTable_NoteMemory", "[systems][profundum][voice-engine][sc-010][long]") {
    auRunShard("VoiceEngine_AudibilityTable_NoteMemory", auShardNoteMemory);
}

TEST_CASE("VoiceEngine_AudibilityTable_Macros", "[systems][profundum][voice-engine][sc-010][sc-007][long]") {
    auRunShard("VoiceEngine_AudibilityTable_Macros", auShardMacros);
}

// Smoke (tasks.md deviation 5): AT-03, AT-10, AT-26 (A per-row, plain A_s recorded, B) at E1, the same readers and bars.
TEST_CASE("VoiceEngine_AudibilityTableSmoke", "[systems][profundum][voice-engine][sc-010]") {
    enableFTZDAZ();
    std::vector<AuCell> cells;
    cells.push_back(auCellAt03(kAuE1));
    cells.push_back(auCellAt10(kAuE1));
    auCellsAt26(cells, kAuE1);
    REQUIRE(cells.size() == 6);   // AT-03, AT-10, AT-26 x {A per-row, plain A_s, B, B_s}
    auReport("VoiceEngine_AudibilityTableSmoke", cells);
}

// =============================================================================
// SC-007: macro sweeps monotone
// =============================================================================

TEST_CASE("VoiceEngine_MacroMonotone", "[systems][profundum][voice-engine][sc-007][long]") {
    enableFTZDAZ();
    std::vector<AuSweepLine> lines;
    for (std::size_t mi = 0; mi < ProfundumMacroMatrix::kNumMacros; ++mi) {
        for (const std::uint8_t note : kAuNotes)
            auSweep(static_cast<ProfundumMacro>(mi), note, kAuSweepSteps, lines);
    }
    auSweepReport("macro-monotone.csv", lines);
}

TEST_CASE("VoiceEngine_MacroMonotoneSmoke", "[systems][profundum][voice-engine][sc-007]") {
    enableFTZDAZ();
    std::vector<AuSweepLine> lines;
    auSweep(ProfundumMacro::CleanViolent, kAuE1, 5, lines);
    auSweepReport("macro-monotone-smoke.csv", lines);
}

// =============================================================================
// SC-009: Motion -> Mass swell measured
// =============================================================================

TEST_CASE("VoiceEngine_MotionMassSwell", "[systems][profundum][voice-engine][sc-009][long]") {
    enableFTZDAZ();
    std::ostringstream os;
    os << "# SC-009 VoiceEngine_MotionMassSwell (tasks.md T016; plan S9.11). A_s (C-8), Mass 50, E1, MONO, 48 kHz;\n"
       << "# CYCLE -> Mass, amount 1, depth 1 (half-span: Mass 0 ... 100); 1 s settle + max(3 cycles, 3 s); frames of 8\n"
       << "# periods of f0, hop 960; peak-to-peak over the whole cycles; boxcar = |sinc(pi f_m 8 / f0)|.\n"
       << "# Gate: D_nl p2p at 0.1 Hz >= 3 dB. ENV (retriggered every 2 s) and DRIFT (smoothness 0.5) recorded.\n";
    double gate = 0.0;
    for (const double rate : {0.1, 0.5, 2.0}) {
        const double seconds = std::max(3.0 / rate, 3.0);
        const AuSwell w = auSwell(auCycle(AuMotion::Target::Mass, rate), seconds, rate, {});
        os << auSwellLine("SC009 CYCLE rate_hz=" + std::to_string(rate), w) << "\n";
        if (rate == 0.1)
            gate = w.dNlP2p;
    }
    {
        AuMotion env{};
        env.source = AuMotion::Source::Env;
        env.target = AuMotion::Target::Mass;
        env.amount = 1.0f;
        env.depth = 1.0f;
        os << auSwellLine("SC009 ENV retrigger_s=2", auSwell(env, kAuEnvSeconds, 0.0, auEnvRetriggers(kAuE1, kAuEnvSeconds)))
           << "\n";
    }
    {
        AuMotion drift{};
        drift.source = AuMotion::Source::Drift;
        drift.target = AuMotion::Target::Mass;
        drift.amount = 1.0f;
        drift.depth = 1.0f;
        drift.driftSmoothness = 0.5f;
        os << auSwellLine("SC009 DRIFT smoothness=0.5", auSwell(drift, kAuDriftSeconds, 0.0, {})) << "\n";
    }
    const bool pass = auFinite(gate) && gate >= kAuBarDb;
    os << "SC009 GATE D_nl_p2p_at_0.1Hz=" << gate << " bar=" << kAuBarDb << " " << (pass ? "PASS" : "FAIL") << "\n";
    std::cout << os.str();
    auWriteEvidence("motion-mass-swell.txt", os.str());
    INFO("D_nl peak-to-peak at 0.1 Hz " << gate << " dB (bar " << kAuBarDb << " dB)");
    REQUIRE(pass);
}

// Smoke: 2 Hz, 3 s, the same frame and the same 3 dB bar on D_nl's peak-to-peak.
TEST_CASE("VoiceEngine_MotionMassSwellSmoke", "[systems][profundum][voice-engine][sc-009]") {
    enableFTZDAZ();
    const AuSwell w = auSwell(auCycle(AuMotion::Target::Mass, 2.0), 3.0, 2.0, {});
    const std::string line = auSwellLine("SC009 SMOKE CYCLE rate_hz=2", w);
    std::cout << line << "\n";
    auWriteEvidence("motion-mass-swell-smoke.txt", line + "\n");
    REQUIRE(w.frames > 0);
    INFO(line);
    REQUIRE(auFinite(w.dNlP2p));
    REQUIRE(w.dNlP2p >= kAuBarDb);
}
