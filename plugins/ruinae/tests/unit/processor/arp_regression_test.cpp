// ==============================================================================
// Arpeggiator regression tests (audit fix plan T6.5)
// ==============================================================================
// Promoted from the audit probes (specs/ruinae-audit/probes/audit_probes.cpp.txt,
// "12. Arpeggiator"): every mode is note-balanced and on the grid, and the
// transport / latch / disable / host-loop / no-context edges leave no stuck
// note on the MIDI output and no sounding voice. One real Ruinae::Processor per
// case through RuinaeAuditRig.
// ==============================================================================

#include "audit_rig.h"

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace RuinaeAuditRig;

namespace {

constexpr const char* kArpModeNames[10] = {"Up",       "Down", "UpDown", "DownUp",  "Converge",
                                           "Diverge",  "Random", "Walk", "AsPlayed", "Chord"};

int64_t nonFiniteAll(const Rig& r) {
    int64_t n = 0;
    for (const float v : r.L)
        if (!Krate::DSP::detail::isFinite(v)) ++n;
    for (const float v : r.R)
        if (!Krate::DSP::detail::isFinite(v)) ++n;
    return n;
}

/// C4-E4-G4 style arp patch: MIDI out on, 1/16 synced, transport playing.
void arpPatch(Rig& r, int mode) {
    basePatch(r, 0.0);
    r.useCtx = true;
    r.playing = true;
    r.param(Ruinae::kArpOperatingModeId, 1.0 / 3.0);
    r.param(Ruinae::kArpModeId, mode / 9.0);
    r.param(Ruinae::kArpTempoSyncId, 1.0);
    r.param(Ruinae::kArpNoteValueId, 7.0 / 29.0);  // 1/16
    r.param(Ruinae::kArpLatchModeId, 0.0);
    r.param(Ruinae::kArpMidiOutId, 1.0);
    r.param(Ruinae::kArpSwingId, 0.0);
    r.param(Ruinae::kArpOctaveRangeId, 0.0);
    r.param(Ruinae::kAmpEnvReleaseId, envNorm(30));
}

struct Balance {
    int on = 0, off = 0, unmatched = 0;
};

Balance balance(const std::vector<OutNote>& ev) {
    Balance b;
    std::vector<int> open(128, 0);
    for (const auto& e : ev) {
        const auto p = static_cast<size_t>(e.pitch & 127);
        if (e.on) {
            ++b.on;
            ++open[p];
        } else {
            ++b.off;
            if (open[p] > 0) --open[p];
        }
    }
    for (const int o : open) b.unmatched += o;
    return b;
}

constexpr double kSr = 48000.0;

}  // namespace

TEST_CASE("Arp: every mode is note-balanced, on the 1/16 grid and drift-free over 8 bars",
          "[arp][regression]") {
    for (int mode = 0; mode < 10; ++mode) {
        DYNAMIC_SECTION("mode " << kArpModeNames[mode]) {
            Rig r(kSr);
            arpPatch(r, mode);
            for (const int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
            const int64_t bars8 = r.at(16.0);
            for (const int n : {60, 64, 67}) r.noteOff(n, bars8);
            r.render(bars8 + r.at(1.0));

            const uint32_t voicesAtEnd = r.proc->engine().getActiveVoiceCount();
            const Balance b = balance(r.outNotes);
            const double grid = 0.125 * kSr;  // 1/16 at 120 BPM
            double maxErr = 0, firstErr = 0, lastErr = 0;
            bool firstSet = false;
            for (const auto& e : r.outNotes) {
                if (!e.on || e.pos >= bars8) continue;
                const double k = std::round(static_cast<double>(e.pos) / grid);
                const double err = static_cast<double>(e.pos) - k * grid;
                maxErr = std::max(maxErr, std::fabs(err));
                if (!firstSet) {
                    firstErr = err;
                    firstSet = true;
                }
                lastErr = err;
            }
            const int expected = mode == 9 ? 384 : 128;
            INFO("note-ons " << b.on << " note-offs " << b.off << " unmatched " << b.unmatched
                             << " maxGridErr " << maxErr << " drift " << (lastErr - firstErr)
                             << " voices " << voicesAtEnd);
            CHECK(b.on == expected);
            CHECK(b.off == expected);
            CHECK(b.unmatched == 0);
            CHECK(maxErr <= 1.0);
            CHECK(lastErr == firstErr);
            CHECK(voicesAtEnd == 0u);
            CHECK(nonFiniteAll(r) == 0);
        }
    }
}

TEST_CASE("Arp: transport stop with keys held leaves no unmatched note and no voice",
          "[arp][regression]") {
    Rig r(kSr);
    arpPatch(r, 0);
    for (const int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
    r.render(r.at(2.0) + 1234);
    r.playing = false;
    r.render(r.at(1.5));
    const uint32_t v = r.proc->engine().getActiveVoiceCount();
    const Balance b = balance(r.outNotes);
    INFO("unmatched " << b.unmatched << " voices " << v);
    CHECK(b.unmatched == 0);
    CHECK(v == 0u);
    for (const int n : {60, 64, 67}) r.noteOff(n, r.now());
    r.render(r.at(0.5));
    CHECK(nonFiniteAll(r) == 0);
}

TEST_CASE("Arp: disabling the arp mid-note leaves no unmatched note and no voice",
          "[arp][regression]") {
    Rig r(kSr);
    arpPatch(r, 0);
    for (const int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
    r.render(r.at(2.0) + 3000);
    r.param(Ruinae::kArpOperatingModeId, 0.0);
    r.render(r.at(1.5));
    const uint32_t v = r.proc->engine().getActiveVoiceCount();
    const Balance b = balance(r.outNotes);
    INFO("unmatched " << b.unmatched << " voices " << v);
    CHECK(b.unmatched == 0);
    CHECK(v == 0u);
    for (const int n : {60, 64, 67}) r.noteOff(n, r.now());
    r.render(r.at(1.0));
    CHECK(r.proc->engine().getActiveVoiceCount() == 0u);
    CHECK(nonFiniteAll(r) == 0);
}

TEST_CASE("Arp: releasing every key with latch off stops the arp and leaves no voice",
          "[arp][regression]") {
    Rig r(kSr);
    arpPatch(r, 2);
    for (const int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
    const int64_t off = r.at(2.0) + 777;
    r.noteOff(60, off);
    r.noteOff(64, off + 100);
    r.noteOff(67, off + 2000);
    r.render(off + r.at(1.5));
    const uint32_t v = r.proc->engine().getActiveVoiceCount();
    const Balance b = balance(r.outNotes);
    int onsAfter = 0;
    for (const auto& e : r.outNotes)
        if (e.on && e.pos > off + 2000) ++onsAfter;
    INFO("unmatched " << b.unmatched << " voices " << v << " note-ons after last key-up " << onsAfter);
    CHECK(b.unmatched == 0);
    CHECK(v == 0u);
    CHECK(onsAfter == 0);
    CHECK(nonFiniteAll(r) == 0);
}

TEST_CASE("Arp: a backward ppq jump (host loop) leaves no unmatched note and no voice",
          "[arp][regression]") {
    Rig r(kSr);
    arpPatch(r, 0);
    for (const int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
    r.render(r.at(3.0) + 1000);
    r.ppq = 0.0;  // loop back
    r.render(r.at(2.0));
    for (const int n : {60, 64, 67}) r.noteOff(n, r.now());
    r.render(r.at(1.0));
    const uint32_t v = r.proc->engine().getActiveVoiceCount();
    const Balance b = balance(r.outNotes);
    INFO("unmatched " << b.unmatched << " voices " << v);
    CHECK(b.unmatched == 0);
    CHECK(v == 0u);
    CHECK(nonFiniteAll(r) == 0);
}

TEST_CASE("Arp: without a process context it still plays and leaves no unmatched note or voice",
          "[arp][regression]") {
    Rig r(kSr);
    arpPatch(r, 0);
    r.useCtx = false;
    for (const int n : {60, 64, 67}) r.noteOn(n, 0.8f, 0);
    r.render(r.at(1.5));
    for (const int n : {60, 64, 67}) r.noteOff(n, r.now() + 10);
    r.render(r.at(1.0));
    const uint32_t v = r.proc->engine().getActiveVoiceCount();
    const Balance b = balance(r.outNotes);
    INFO("note-ons " << b.on << " unmatched " << b.unmatched << " voices " << v);
    CHECK(b.on > 0);
    CHECK(b.unmatched == 0);
    CHECK(v == 0u);
    CHECK(nonFiniteAll(r) == 0);
}
