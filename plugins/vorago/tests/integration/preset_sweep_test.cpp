// ==============================================================================
// Vorago Phase 14 - factory preset sweep (short bounded + long sweep cases); FR-033, FR-033a, FR-034, FR-037, FR-038; SC-011, SC-012, SC-014, SC-015, SC-022, SC-024; filled by T031, T041
// ==============================================================================
// Registered by T003 (specs/vorago-phase14-presets-release/tasks.md).
//
// T031 - Vorago_PresetSweep_ShortBounded (FR-034, SC-014; per-push). The short
// load-time guard: for the default surface (no setState) and every definition in
// allPresets() (Comp built through the C-4 drive, buildPresetComponentState), four
// streaming renders on a fresh PresetHost:
//   - NoteOn 36, 8 s at 48 kHz
//   - NoteOn 36, 4 s at 44.1 kHz
//   - NoteOn 36, 4 s at 96 kHz
//   - the kCpuNotes chord, 8 s at 48 kHz, polyphony forced to index 3 (4 voices)
// Each render must be finite (bit pattern) with stereo peak <= 0.9661f. It makes
// no sustain-state claim (FR-033 / FR-033a do). Renders run through
// VoragoTest::runJobs on 2 threads; every assertion runs on the test thread.
//
// T041 - the five [long][vorago-sweep] cases (FR-033, FR-033a, FR-037, FR-038;
// SC-011, SC-012, SC-015, SC-022, SC-024). Each iterates the definition indices
// 0..N-1 of shardFromEnv() (plus the pseudo-preset index N for the ablation
// case), reads each index's record through sweepRecordFor (whose renders run
// through VoragoTest::runJobs inside computeSweepRecord), asserts on the test
// thread and prints per-preset figures.
// ==============================================================================

#include "preset_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <numbers>
#include <utility>
#include <vector>

namespace {

namespace PD = ::Vorago::PresetDefs;

constexpr float kShortPeakCeiling = 0.9661f;  // FR-034 / SC-014
constexpr unsigned kShortSweepThreads = 2u;
constexpr std::int32_t kChordPolyIndex = 3;  // kPolyphonyId index 3 -> 4 voices
// Same four notes as processor_cpu_test.cpp:85 (kCpuNotes, a TU-local constant there).
constexpr std::array<std::int16_t, 4> kCpuNotes{36, 40, 43, 47};

struct ShortCase {
    const char* label;
    double sr;
    double seconds;
    bool chord;
};

constexpr std::array<ShortCase, 4> kShortCases{{
    {.label = "note 36, 8 s @ 48 kHz", .sr = 48000.0, .seconds = 8.0, .chord = false},
    {.label = "note 36, 4 s @ 44.1 kHz", .sr = 44100.0, .seconds = 4.0, .chord = false},
    {.label = "note 36, 4 s @ 96 kHz", .sr = 96000.0, .seconds = 4.0, .chord = false},
    {.label = "kCpuNotes chord, poly 4, 8 s @ 48 kHz", .sr = 48000.0, .seconds = 8.0, .chord = true},
}};

}  // namespace

TEST_CASE("Vorago_PresetSweep_ShortBounded", "[vorago][preset]") {
    // Preset slot 0 is the default surface (empty comp: no setState).
    std::vector<std::string> names{"<default surface>"};
    std::vector<std::vector<std::uint8_t>> comps(1);
    for (const PD::VoragoPresetDef& d : PD::allPresets()) {
        std::vector<std::uint8_t> comp;
        std::string why;
        const bool built = VoragoTest::buildPresetComponentState(d, comp, why);
        INFO("preset " << std::string(d.name) << ": " << why);
        REQUIRE(built);
        names.emplace_back(d.name);
        comps.push_back(std::move(comp));
    }

    const std::size_t numJobs = comps.size() * kShortCases.size();
    std::vector<VoragoTest::SweepCapture> results(numJobs);
    std::vector<std::function<void()>> jobs;
    jobs.reserve(numJobs);
    for (std::size_t p = 0; p < comps.size(); ++p) {
        for (std::size_t c = 0; c < kShortCases.size(); ++c) {
            const std::size_t slot = p * kShortCases.size() + c;
            jobs.emplace_back([&comps, &results, p, c, slot] {
                const ShortCase& sc = kShortCases[c];
                VoragoTest::RenderSpec spec;
                spec.comp = std::span<const std::uint8_t>(comps[p]);
                spec.sr = sc.sr;
                spec.end = sc.seconds;
                if (sc.chord) {
                    spec.notes.assign(kCpuNotes.begin(), kCpuNotes.end());
                    spec.forcePolyIndex = kChordPolyIndex;
                }
                results[slot] = VoragoTest::renderPreset(spec);
            });
        }
    }
    VoragoTest::runJobs(jobs, kShortSweepThreads);

    for (std::size_t p = 0; p < comps.size(); ++p) {
        for (std::size_t c = 0; c < kShortCases.size(); ++c) {
            const VoragoTest::SweepCapture& r = results[p * kShortCases.size() + c];
            INFO("preset " << names[p] << ", " << kShortCases[c].label << ": peak " << r.peak);
            CHECK(r.finite);
            CHECK(r.peak <= kShortPeakCeiling);
            // A render that produced no blocks proves nothing.
            CHECK_FALSE(r.blockPowerL.empty());
        }
    }
}

// ==============================================================================
// T033 - records and sharding (plan 5.8 rows Record / Sharding; per-push).
// ==============================================================================

namespace {

VoragoTest::PresetDescriptor makeRecordDescriptor(double base) {
    VoragoTest::PresetDescriptor d;
    for (std::size_t k = 0; k < d.band.size(); ++k) {
        d.band[k] = base + (0.1 * static_cast<double>(k)) + (1.0 / 3.0);
    }
    d.motion = base * 1.25;
    d.flux = -base / 7.0;
    d.corr = 0.123456789012345678;
    d.energySpread = base + 1e-9;
    d.crest = -3.0e-7 * base;
    return d;
}

bool descriptorsEqual(const VoragoTest::PresetDescriptor& a, const VoragoTest::PresetDescriptor& b) {
    return a.band == b.band && a.motion == b.motion && a.flux == b.flux && a.corr == b.corr &&
           a.energySpread == b.energySpread && a.crest == b.crest;
}

std::filesystem::path recordTempPath(const char* stem) {
    return std::filesystem::temp_directory_path() / (std::string("vorago_t033_") + stem + ".txt");
}

}  // namespace

TEST_CASE("Vorago_PresetSupport_RecordRoundTrip", "[vorago][preset]") {
    VoragoTest::SweepRecord rec;
    rec.name = "Cathedral Void";
    rec.takeCount = 2;
    rec.tl.A = 12.5;
    rec.tl.rel = 8.25;
    rec.tl.rt60 = 17.0 / 3.0;
    rec.tl.sus0 = 17.5;
    rec.tl.sus1 = 77.5;
    for (int k = 0; k < 3; ++k) {
        rec.tl.m[k][0] = 17.5 + (60.0 * static_cast<double>(k));
        rec.tl.m[k][1] = 77.5 + (60.0 * static_cast<double>(k)) + 1e-12;
    }
    rec.tl.H = 197.5;
    rec.tl.tail0 = 215.75;
    rec.tl.tail1 = 275.75;
    rec.tl.total = 275.75;
    rec.tl.freezeOnTail = true;

    for (int j = 0; j < 2; ++j) {
        const double dj = static_cast<double>(j);
        VoragoTest::TakeRecord t;
        t.seedIndex = 3 + (5 * j);
        t.finite = (j == 0);
        t.peak = 0.6180339f + static_cast<float>(j);
        t.worstHiDb = (-1.0 / 3.0) - dj;
        t.worstLoDb = -61.123456789;
        t.lateVsSusDb = 0.000123 * (dj + 1.0);
        t.tailDb = -240.0;
        t.armPass = {j == 0, true, j != 0, false};
        for (std::size_t m = 0; m < 3; ++m) {
            t.minutes[m] = makeRecordDescriptor(dj + (0.37 * static_cast<double>(m)));
        }
        rec.takes.push_back(t);
    }
    rec.mean = makeRecordDescriptor(std::numbers::e);
    rec.selfDistance = 0.7071067811865476;
    rec.levelTwinD = 0.0123;
    rec.storedPressure = 0.87654321;
    rec.storedWeight = 1.0 / 7.0;

    // A vector mixing outcomes and skip strings (with spaces, and empty).
    for (std::size_t i = 0; i < rec.vec.cells.size(); ++i) {
        VoragoTest::CellOutcome& o = rec.vec.cells[i];
        o.stateOk = (i % 2u) == 0u;
        o.conjunctOk = (i % 3u) != 0u;
        o.rendered = (i % 5u) != 1u;
        o.d = 1.0 / static_cast<double>(i + 1u);
        o.twoS = 0.1 * static_cast<double>(i);
        o.attribBase = (i % 4u == 0u) ? -1.0 : 2.0 / 3.0;
        if (i % 7u == 2u) {
            o.skip = "state-only kind";
        } else if (i % 7u == 4u) {
            o.skip = "override equals stored";
        }
    }
    rec.gesture.loudestDb = -12.25;
    rec.gesture.lastDb = -13.0 / 7.0;
    rec.gesture.floorDb = -32.25;
    rec.gesture.dryLoudestDb = -55.5;
    rec.gesture.pass = true;
    rec.rates.finite441 = true;
    rec.rates.peak441 = 0.75f;
    rec.rates.worstHi441 = -2.0 / 9.0;
    rec.rates.finite96 = false;
    rec.rates.peak96 = 0.5123f;
    rec.rates.worstHi96 = -0.0625;
    rec.reproducible = true;

    const std::filesystem::path path = recordTempPath("roundtrip");
    VoragoTest::writeRecord(rec, path);

    VoragoTest::SweepRecord back;
    REQUIRE(VoragoTest::readRecord(path, back));

    CHECK(back.name == rec.name);
    CHECK(back.takeCount == rec.takeCount);
    CHECK(back.tl.A == rec.tl.A);
    CHECK(back.tl.rel == rec.tl.rel);
    CHECK(back.tl.rt60 == rec.tl.rt60);
    CHECK(back.tl.sus0 == rec.tl.sus0);
    CHECK(back.tl.sus1 == rec.tl.sus1);
    for (int k = 0; k < 3; ++k) {
        CHECK(back.tl.m[k][0] == rec.tl.m[k][0]);
        CHECK(back.tl.m[k][1] == rec.tl.m[k][1]);
    }
    CHECK(back.tl.H == rec.tl.H);
    CHECK(back.tl.tail0 == rec.tl.tail0);
    CHECK(back.tl.tail1 == rec.tl.tail1);
    CHECK(back.tl.total == rec.tl.total);
    CHECK(back.tl.freezeOnTail == rec.tl.freezeOnTail);

    REQUIRE(back.takes.size() == rec.takes.size());
    for (std::size_t j = 0; j < rec.takes.size(); ++j) {
        const VoragoTest::TakeRecord& a = rec.takes[j];
        const VoragoTest::TakeRecord& b = back.takes[j];
        INFO("take " << j);
        CHECK(b.seedIndex == a.seedIndex);
        CHECK(b.finite == a.finite);
        CHECK(b.peak == a.peak);
        CHECK(b.worstHiDb == a.worstHiDb);
        CHECK(b.worstLoDb == a.worstLoDb);
        CHECK(b.lateVsSusDb == a.lateVsSusDb);
        CHECK(b.tailDb == a.tailDb);
        CHECK(b.armPass == a.armPass);
        for (std::size_t m = 0; m < 3; ++m) {
            CHECK(descriptorsEqual(b.minutes[m], a.minutes[m]));
        }
    }
    CHECK(descriptorsEqual(back.mean, rec.mean));
    CHECK(back.selfDistance == rec.selfDistance);
    CHECK(back.levelTwinD == rec.levelTwinD);
    CHECK(back.storedPressure == rec.storedPressure);
    CHECK(back.storedWeight == rec.storedWeight);

    for (std::size_t i = 0; i < rec.vec.cells.size(); ++i) {
        const VoragoTest::CellOutcome& a = rec.vec.cells[i];
        const VoragoTest::CellOutcome& b = back.vec.cells[i];
        INFO("cell " << i);
        CHECK(b.stateOk == a.stateOk);
        CHECK(b.conjunctOk == a.conjunctOk);
        CHECK(b.rendered == a.rendered);
        CHECK(b.d == a.d);
        CHECK(b.twoS == a.twoS);
        CHECK(b.attribBase == a.attribBase);
        CHECK(b.skip == a.skip);
    }

    CHECK(back.gesture.loudestDb == rec.gesture.loudestDb);
    CHECK(back.gesture.lastDb == rec.gesture.lastDb);
    CHECK(back.gesture.floorDb == rec.gesture.floorDb);
    CHECK(back.gesture.dryLoudestDb == rec.gesture.dryLoudestDb);
    CHECK(back.gesture.pass == rec.gesture.pass);
    CHECK(back.rates.finite441 == rec.rates.finite441);
    CHECK(back.rates.peak441 == rec.rates.peak441);
    CHECK(back.rates.worstHi441 == rec.rates.worstHi441);
    CHECK(back.rates.finite96 == rec.rates.finite96);
    CHECK(back.rates.peak96 == rec.rates.peak96);
    CHECK(back.rates.worstHi96 == rec.rates.worstHi96);
    CHECK(back.reproducible == rec.reproducible);

    // A truncated file (the first half of the record) is rejected, as is a missing one.
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    REQUIRE(text.size() > 2u);
    const std::filesystem::path cut = recordTempPath("truncated");
    {
        std::ofstream out(cut, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size() / 2u));
    }
    VoragoTest::SweepRecord partial;
    CHECK_FALSE(VoragoTest::readRecord(cut, partial));
    CHECK_FALSE(VoragoTest::readRecord(recordTempPath("missing"), partial));

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(cut, ec);
}

TEST_CASE("Vorago_PresetSupport_ShardParse", "[vorago][preset]") {
    using VoragoTest::Shard;
    CHECK(VoragoTest::parseShard("3/10") == Shard{3, 10});
    CHECK(VoragoTest::parseShard("0/1") == Shard{0, 1});
    CHECK(VoragoTest::parseShard("10/10") == Shard{0, 0});
    CHECK(VoragoTest::parseShard("x") == Shard{0, 0});
    CHECK(VoragoTest::parseShard("3/") == Shard{0, 0});
    CHECK(VoragoTest::parseShard("3/0") == Shard{0, 0});
    CHECK(VoragoTest::parseShard("") == Shard{.index = 0, .count = 0});

    const Shard s{.index = 3, .count = 10};
    for (std::size_t i = 0; i < 45u; ++i) {
        INFO("index " << i);
        CHECK(VoragoTest::inShard(i, s) == (i % 10u == 3u));
    }

    const std::optional<std::string> env = VoragoTest::sweepEnv("VORAGO_SWEEP_SHARD");
    if (!env.has_value()) {
        CHECK(VoragoTest::shardFromEnv() == Shard{0, 1});
    } else {
        // A sharded CI job sets the variable; shardFromEnv is then parseShard of it.
        CHECK(VoragoTest::shardFromEnv() == VoragoTest::parseShard(*env));
    }
}

// ==============================================================================
// T034 - plan 6.2 windows and 6.3 arms on synthetic block-power vectors
// (48 kHz / 512; per-push). renderTake / computeTakes are exercised by the
// [long] sweep (T041) and the pilot (T038); these cases pin the arithmetic.
// ==============================================================================

namespace {

constexpr double kSynthSr = 48000.0;
constexpr long long kSynthBlock = 512;
constexpr double kSynthFloorDb = -240.0;  // at or below: zero power (all-floor)
constexpr double kSynthTolDb = 1e-9;

/// A = 10, Rel = 5, RT60 = 10: Sus = M1 = [15, 75], M2 = [75, 135],
/// M3 = [135, 195], H = 195; Freeze Off tail [215, 225]; Freeze On tail
/// [210, 270]. The same arithmetic as makeTimeline (plan 6.1).
VoragoTest::SweepTimeline synthTimeline(bool freezeOn) {
    VoragoTest::SweepTimeline tl;
    tl.A = 10.0;
    tl.rel = 5.0;
    tl.rt60 = 10.0;
    tl.sus0 = tl.A + 5.0;
    tl.sus1 = tl.A + 65.0;
    for (int k = 0; k < 3; ++k) {
        tl.m[k][0] = tl.A + 5.0 + (60.0 * static_cast<double>(k));
        tl.m[k][1] = tl.A + 65.0 + (60.0 * static_cast<double>(k));
    }
    tl.H = tl.A + 185.0;
    tl.freezeOnTail = freezeOn;
    if (freezeOn) {
        tl.tail0 = tl.H + tl.rel + 10.0;
        tl.tail1 = tl.H + tl.rel + 70.0;
    } else {
        tl.tail0 = tl.H + tl.rel + tl.rt60 + 5.0;
        tl.tail1 = tl.H + tl.rel + tl.rt60 + 15.0;
    }
    tl.total = tl.tail1;
    return tl;
}

/// One stereo power sum per 512-sample block over [0, tl.total]: each channel of
/// a block starting at time t carries n * 10^(dbAt(t) / 10), so the block's
/// stereo power (sumL + sumR) / (2n) is exactly dbAt(t) in dB.
VoragoTest::SweepCapture synthCapture(const VoragoTest::SweepTimeline& tl,
                                      const std::function<double(double)>& dbAt) {
    VoragoTest::SweepCapture cap;
    cap.finite = true;
    cap.peak = 0.5f;
    const long long total = std::llround(tl.total * kSynthSr);
    for (long long start = 0; start < total; start += kSynthBlock) {
        const long long n = std::min(kSynthBlock, total - start);
        const double db = dbAt(static_cast<double>(start) / kSynthSr);
        const double perSample = (db <= kSynthFloorDb) ? 0.0 : std::pow(10.0, db / 10.0);
        cap.blockPowerL.push_back(static_cast<double>(n) * perSample);
        cap.blockPowerR.push_back(static_cast<double>(n) * perSample);
    }
    return cap;
}

/// -20 dBFS until H, `tailDb` from H on (release region and tail alike).
std::function<double(double)> holdThenTail(const VoragoTest::SweepTimeline& tl, double tailDb) {
    const double H = tl.H;
    return [H, tailDb](double t) { return (t < H) ? -20.0 : tailDb; };
}

}  // namespace

TEST_CASE("Vorago_PresetSupport_WindowRule", "[vorago][preset]") {
    constexpr double a = 12.345;  // deliberately not on a block edge
    const auto checkSnap = [](const VoragoTest::SweepWindow& w) {
        const long long s0 = std::llround(w.t0 * kSynthSr);
        const long long s1 = std::llround(w.t1 * kSynthSr);
        const long long e0 = static_cast<long long>(w.firstBlock) * kSynthBlock;
        const long long e1 = static_cast<long long>(w.endBlock) * kSynthBlock;
        INFO("window [" << w.t0 << ", " << w.t1 << "] -> blocks [" << w.firstBlock << ", "
                        << w.endBlock << ")");
        CHECK(e0 >= s0);  // snapped inward
        CHECK(e1 <= s1);
        CHECK(e0 - s0 <= 511);  // <= 511 samples of slack per edge
        CHECK(s1 - e1 <= 511);
        CHECK(w.t1 - w.t0 >= 10.0 - kSynthTolDb);  // no window shorter than 10 s
    };

    SECTION("25 s: two full windows plus one right-aligned at b") {
        const double b = a + 25.0;
        const std::vector<VoragoTest::SweepWindow> w = VoragoTest::tenSecondWindows(a, b, kSynthSr);
        REQUIRE(w.size() == 3u);
        CHECK(std::fabs(w[0].t0 - a) < kSynthTolDb);
        CHECK(std::fabs(w[0].t1 - (a + 10.0)) < kSynthTolDb);
        CHECK(std::fabs(w[1].t0 - (a + 10.0)) < kSynthTolDb);
        CHECK(std::fabs(w[1].t1 - (a + 20.0)) < kSynthTolDb);
        CHECK(std::fabs(w[2].t0 - (b - 10.0)) < kSynthTolDb);
        CHECK(std::fabs(w[2].t1 - b) < kSynthTolDb);
        for (const VoragoTest::SweepWindow& x : w) {
            checkSnap(x);
        }
    }
    SECTION("30 s: exactly three full windows, no remainder window") {
        const double b = a + 30.0;
        const std::vector<VoragoTest::SweepWindow> w = VoragoTest::tenSecondWindows(a, b, kSynthSr);
        REQUIRE(w.size() == 3u);
        for (std::size_t k = 0; k < w.size(); ++k) {
            const double dk = static_cast<double>(k);
            CHECK(std::fabs(w[k].t0 - (a + (10.0 * dk))) < kSynthTolDb);
            CHECK(std::fabs(w[k].t1 - (a + (10.0 * (dk + 1.0)))) < kSynthTolDb);
            checkSnap(w[k]);
        }
    }
}

TEST_CASE("Vorago_PresetSupport_ArmsOnSynthetic", "[vorago][preset]") {
    SECTION("Freeze Off: constant -20 dBFS hold, -70 tail -> arms 1-4 pass") {
        const VoragoTest::SweepTimeline tl = synthTimeline(false);
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, holdThenTail(tl, -70.0)), tl, kSynthSr);
        INFO("hi " << r.worstHiDb << " lo " << r.worstLoDb << " late-sus " << r.lateVsSusDb
                   << " sus " << r.susDb << " tail " << r.tailDb);
        CHECK(r.finite);
        CHECK(r.pass1);
        CHECK(r.pass2);
        CHECK(r.pass3);
        CHECK(std::fabs(r.lateVsSusDb) < kSynthTolDb);  // late - early = 0 dB
        CHECK(std::fabs(r.susDb - -20.0) < kSynthTolDb);
        CHECK(std::fabs(r.worstLoDb - -20.0) < kSynthTolDb);
        CHECK(std::fabs(r.tailDb - -70.0) < kSynthTolDb);
        CHECK(r.pass4);  // -70 <= -20 - 40
    }
    SECTION("Freeze Off: tail at -59 -> arm 4 fails") {
        const VoragoTest::SweepTimeline tl = synthTimeline(false);
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, holdThenTail(tl, -59.0)), tl, kSynthSr);
        CHECK(r.pass1);
        CHECK(r.pass2);
        CHECK(r.pass3);
        CHECK_FALSE(r.pass4);
    }
    SECTION("one 10 s window at -5 dBFS -> arm 1 fails (<= -6 rule)") {
        const VoragoTest::SweepTimeline tl = synthTimeline(false);
        // [30, 40] is the fourth window of [0, Total] (windows start at 0).
        const std::function<double(double)> base = holdThenTail(tl, -70.0);
        const std::function<double(double)> level = [base](double t) {
            return (t >= 30.0 && t < 40.0) ? -5.0 : base(t);
        };
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, level), tl, kSynthSr);
        CHECK(std::fabs(r.worstHiDb - -5.0) < kSynthTolDb);
        CHECK_FALSE(r.pass1);
    }
    SECTION("non-finite or over-ceiling peak -> arm 1 fails") {
        const VoragoTest::SweepTimeline tl = synthTimeline(false);
        VoragoTest::SweepCapture cap = synthCapture(tl, holdThenTail(tl, -70.0));
        cap.peak = 0.97f;
        CHECK_FALSE(VoragoTest::evaluateArms(cap, tl, kSynthSr).pass1);
        cap.peak = 0.5f;
        cap.finite = false;
        CHECK_FALSE(VoragoTest::evaluateArms(cap, tl, kSynthSr).pass1);
    }
    SECTION("a window at -61 dBFS inside [A, H] -> arm 2 fails") {
        const VoragoTest::SweepTimeline tl = synthTimeline(false);
        // [A + 100, A + 110] is the eleventh window of [A, H] (windows start at A).
        const std::function<double(double)> base = holdThenTail(tl, -70.0);
        const double w0 = tl.A + 100.0;
        const std::function<double(double)> level = [base, w0](double t) {
            return (t >= w0 && t < w0 + 10.0) ? -61.0 : base(t);
        };
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, level), tl, kSynthSr);
        CHECK(std::fabs(r.worstLoDb - -61.0) < kSynthTolDb);
        CHECK(r.pass1);
        CHECK_FALSE(r.pass2);
    }
    SECTION("late window 13 dB above Sus -> arm 3 fails") {
        const VoragoTest::SweepTimeline tl = synthTimeline(false);
        const std::function<double(double)> base = holdThenTail(tl, -70.0);
        const double late0 = tl.H - 60.0;
        const double H = tl.H;
        const std::function<double(double)> level = [base, late0, H](double t) {
            return (t >= late0 && t < H) ? -7.0 : base(t);
        };
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, level), tl, kSynthSr);
        CHECK(std::fabs(r.lateVsSusDb - 13.0) < kSynthTolDb);
        CHECK(r.pass1);  // -7 <= -6
        CHECK_FALSE(r.pass3);
    }
    SECTION("Freeze On: flat -30 tail, Sus -20 -> arm 4 passes") {
        const VoragoTest::SweepTimeline tl = synthTimeline(true);
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, holdThenTail(tl, -30.0)), tl, kSynthSr);
        CHECK(std::fabs(r.tailLoudestDb - -30.0) < kSynthTolDb);
        CHECK(std::fabs(r.tailLastDb - -30.0) < kSynthTolDb);
        CHECK(r.pass1);
        CHECK(r.pass2);
        CHECK(r.pass3);
        CHECK(r.pass4);
    }
    SECTION("Freeze On: flat -41 tail, Sus -20 -> fails the -20 dB floor") {
        const VoragoTest::SweepTimeline tl = synthTimeline(true);
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, holdThenTail(tl, -41.0)), tl, kSynthSr);
        CHECK_FALSE(r.pass4);
    }
    SECTION("Freeze On: rising 1.5 dB per window over the six windows -> fails non-growing") {
        const VoragoTest::SweepTimeline tl = synthTimeline(true);
        const double H = tl.H;
        const double t0 = tl.tail0;
        const std::function<double(double)> level = [H, t0](double t) {
            if (t < H) {
                return -20.0;
            }
            if (t < t0) {
                return -30.0;
            }
            return -30.0 + (1.5 * std::floor((t - t0) / 10.0));
        };
        const VoragoTest::ArmResult r =
            VoragoTest::evaluateArms(synthCapture(tl, level), tl, kSynthSr);
        INFO("last " << r.tailLastDb << " loudest " << r.tailLoudestDb << " sus " << r.susDb);
        // Held (last >= loudest - 6) and above the floor: only non-growing bites.
        CHECK(r.tailLastDb >= r.tailLoudestDb - VoragoTest::kFreezeHoldDb);
        CHECK(r.tailLoudestDb >= r.susDb - VoragoTest::kFreezeFloorDb);
        CHECK_FALSE(r.pass4);
    }
    SECTION("Freeze On: all-floor (-240) tail -> fails the floor") {
        const VoragoTest::SweepTimeline tl = synthTimeline(true);
        const VoragoTest::ArmResult r = VoragoTest::evaluateArms(
            synthCapture(tl, holdThenTail(tl, kSynthFloorDb)), tl, kSynthSr);
        CHECK(std::fabs(r.tailLoudestDb - -240.0) < kSynthTolDb);
        CHECK_FALSE(r.pass4);
    }
}

// ==============================================================================
// T035 - plan 6.7 ablation twins, 6.8 attack-window reversion, 6.9 routes, 6.11
// skip rules and the verification vector fill (FR-012, FR-037; per-push).
// ==============================================================================

namespace {

using OverrideList = std::vector<std::pair<Steinberg::Vst::ParamID, double>>;

/// A component state built through THE drive (buildPresetComponentState) from
/// `params` on top of the registered defaults. An empty list is the default surface.
std::vector<std::uint8_t> t035State(std::vector<PD::ParamSetting> params) {
    const PD::VoragoPresetDef def{.name = "T035 state",
                                  .category = "Drones",
                                  .description = "T035 harness state",
                                  .primary = PD::Capability::S1Noise,
                                  .secondaries = {},
                                  .params = std::move(params)};
    std::vector<std::uint8_t> comp;
    std::string why;
    const bool built = VoragoTest::buildPresetComponentState(def, comp, why);
    INFO(why);
    REQUIRE(built);
    return comp;
}

OverrideList sortedById(OverrideList v) {
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return v;
}

OverrideList concatOverrides(std::initializer_list<OverrideList> parts) {
    OverrideList out;
    for (const OverrideList& p : parts) {
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

/// The pool width of the sweep harness (plan 5.8 Pool): VORAGO_SWEEP_THREADS,
/// else min(hardware_concurrency, 4).
unsigned t035PoolWidth() {
    if (const std::optional<std::string> env = VoragoTest::sweepEnv("VORAGO_SWEEP_THREADS")) {
        const unsigned long v = std::strtoul(env->c_str(), nullptr, 10);
        if (v > 0ul) {
            return static_cast<unsigned>(v);
        }
    }
    return std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
}

bool isRouteCell(PD::Capability c) {
    const auto v = static_cast<std::size_t>(c);
    return v >= static_cast<std::size_t>(PD::Capability::E1PartialBloom) &&
           v <= static_cast<std::size_t>(PD::Capability::E5GhostBursts);
}

/// skipReason exactly as the vector fill calls it: the route overrides for
/// E1-E5, the twin overrides for every other cell.
std::string t035SkipReason(const std::vector<std::uint8_t>& comp, PD::Capability c) {
    VoragoTest::DecodedPresetState st;
    REQUIRE(VoragoTest::decodePresetState(comp, st));
    const std::map<Steinberg::Vst::ParamID, double> stored =
        VoragoTest::storedNormalizedValues(comp);
    const OverrideList ov =
        isRouteCell(c) ? VoragoTest::routeOverrides(c) : VoragoTest::twinOverrides(c, st);
    return VoragoTest::skipReason(c, st, stored, ov);
}

}  // namespace

TEST_CASE("Vorago_PresetSupport_TwinOverrides", "[vorago][preset]") {
    using C = PD::Capability;
    const std::vector<std::uint8_t> defComp = t035State({});
    VoragoTest::DecodedPresetState st;
    REQUIRE(VoragoTest::decodePresetState(defComp, st));

    SECTION("S5: the three tone levels and the level offset -> 0") {
        const OverrideList expected{{::Vorago::kSubLevelOffsetId, 0.0},
                                    {::Vorago::kSubDiv2LevelId, 0.0},
                                    {::Vorago::kSubDiv4LevelId, 0.0},
                                    {::Vorago::kSubFifthBelowLevelId, 0.0}};
        CHECK(sortedById(VoragoTest::twinOverrides(C::S5Sub, st)) == sortedById(expected));
    }
    SECTION("routes: E_k keeps its own destination, R_0 removes all five") {
        // Destinations E1<->S6, E2<->S2, E3<->S1, E4<->S4, E5<->S9 (plan 6.9).
        const OverrideList s6 = VoragoTest::twinOverrides(C::S6Bloom, st);
        const OverrideList s2 = VoragoTest::twinOverrides(C::S2Resonance, st);
        const OverrideList s1 = VoragoTest::twinOverrides(C::S1Noise, st);
        const OverrideList s4 = VoragoTest::twinOverrides(C::S4Ecology, st);
        const OverrideList s9 = VoragoTest::twinOverrides(C::S9Ghost, st);
        CHECK(s6 == OverrideList{{::Vorago::kBloomDepthId, 0.0}});
        CHECK(s2 == OverrideList{{::Vorago::kResonanceMixId, 0.0}});
        CHECK(s1 == OverrideList{{::Vorago::kNoiseLevelId, 0.0}});
        CHECK(s4 == OverrideList{{::Vorago::kEcologyMixId, 0.0}});
        CHECK(s9 == OverrideList{{::Vorago::kGhostPeakLevelId, 0.0}});

        const OverrideList e3 = concatOverrides({s6, s2, s4, s9});
        CHECK(sortedById(VoragoTest::routeOverrides(C::E3NoiseWake)) == sortedById(e3));
        const OverrideList e1 = concatOverrides({s2, s1, s4, s9});
        CHECK(sortedById(VoragoTest::routeOverrides(C::E1PartialBloom)) == sortedById(e1));

        // R_0 (Capability::Count): all five destinations.
        const OverrideList all = concatOverrides({s6, s2, s1, s4, s9});
        CHECK(sortedById(VoragoTest::routeOverrides(C::Count)) == sortedById(all));

        // The 0 variants add 900 -> 0.0.
        const OverrideList depth0{{::Vorago::kEcosystemDepthId, 0.0}};
        CHECK(sortedById(VoragoTest::routeOverrides(C::E3NoiseWake, true)) ==
              sortedById(concatOverrides({e3, depth0})));
        CHECK(sortedById(VoragoTest::routeOverrides(C::Count, true)) ==
              sortedById(concatOverrides({all, depth0})));
    }
    SECTION("D1 per-material reversion: Material A = Glass -> 1004 to its default") {
        const std::vector<std::uint8_t> comp = t035State(
            {{.id = ::Vorago::kBodyMaterialAId, .normalized = ::Vorago::indexToNormalized(0, 11)}});  // 0 = Glass
        VoragoTest::DecodedPresetState glass;
        REQUIRE(VoragoTest::decodePresetState(comp, glass));
        const OverrideList expected{
            {::Vorago::kBodyMaterialAId, ::Vorago::indexToNormalized(5, 11)}};
        CHECK(VoragoTest::twinOverrides(C::D1Glass, glass) == expected);
    }
    SECTION("envelope reversion of a Growth preset -> 1200 to Standard") {
        const std::vector<std::uint8_t> comp =
            t035State({{.id = ::Vorago::kEnvelopeModeId, .normalized = 1.0}});  // index 1 = Growth
        VoragoTest::DecodedPresetState growth;
        REQUIRE(VoragoTest::decodePresetState(comp, growth));
        const OverrideList expected{{::Vorago::kEnvelopeModeId, 0.0}};
        CHECK(VoragoTest::twinOverrides(C::D8Growth, growth) == expected);
    }
}

TEST_CASE("Vorago_PresetSupport_SkipRules", "[vorago][preset]") {
    // T001 item 5 ruled "skips acknowledged": a render is skipped only where its
    // verdict is known exactly, and the reason is recorded (plan 6.11).
    using C = PD::Capability;
    const std::vector<std::uint8_t> defComp = t035State({});

    SECTION("override equals the stored value for every ID -> d = 0, not verified") {
        const std::vector<std::uint8_t> comp = t035State({{.id = ::Vorago::kSubLevelOffsetId, .normalized = 0.0},
                                                          {.id = ::Vorago::kSubDiv2LevelId, .normalized = 0.0},
                                                          {.id = ::Vorago::kSubDiv4LevelId, .normalized = 0.0},
                                                          {.id = ::Vorago::kSubFifthBelowLevelId, .normalized = 0.0}});
        const std::string why = t035SkipReason(comp, C::S5Sub);
        CHECK(why == "override equals stored");
        VoragoTest::CellOutcome o{.stateOk = true, .conjunctOk = true};
        VoragoTest::markSkipped(o, why);
        CHECK_FALSE(o.rendered);
        CHECK(o.d == 0.0);
        CHECK(o.skip == "override equals stored");
        CHECK_FALSE(VoragoTest::verifiedAt(o, C::S5Sub, VoragoTest::ClaimRole::Secondary));
        CHECK_FALSE(VoragoTest::verifiedAt(o, C::S5Sub, VoragoTest::ClaimRole::Primary));
        // One override that differs from the stored value is not a skip.
        const std::vector<std::uint8_t> partial = t035State({{.id = ::Vorago::kSubDiv2LevelId, .normalized = 0.0}});
        CHECK(t035SkipReason(partial, C::S5Sub).empty());
    }
    SECTION("M displacement below the threshold") {
        CHECK(t035SkipReason(t035State({{.id = ::Vorago::kMacroDarknessId, .normalized = 0.4}}), C::M1Darkness) ==
              "M displacement");
        CHECK(t035SkipReason(defComp, C::M5Gravity) == "M displacement");  // 0.5 = default
        CHECK(t035SkipReason(t035State({{.id = ::Vorago::kMacroDarknessId, .normalized = 0.9}}), C::M1Darkness)
                  .empty());
    }
    SECTION("ecosystem depth 900 stored at 0.0 -> E1-E5") {
        const std::vector<std::uint8_t> comp = t035State({{.id = ::Vorago::kEcosystemDepthId, .normalized = 0.0}});
        for (const C e : {C::E1PartialBloom, C::E2ResonatorPeaks, C::E3NoiseWake,
                          C::E4FeedbackLoopWake, C::E5GhostBursts}) {
            INFO("cell " << std::string(PD::cellSpecs()[static_cast<std::size_t>(e)].label));
            CHECK(t035SkipReason(comp, e) == "ecosystem depth 0");
            CHECK(t035SkipReason(defComp, e).empty());  // default depth 0.85
        }
    }
    SECTION("D cell with a false state predicate") {
        CHECK(t035SkipReason(defComp, C::D14Breathing) == "state false");   // 0.30 < 0.7
        CHECK(t035SkipReason(defComp, C::D13SlowEvents) == "state false");  // 1.0x > 0.3
        CHECK(t035SkipReason(t035State({{::Vorago::kLifeBreathingDepthId, 1.0}}),
                             C::D14Breathing)
                  .empty());
    }
    SECTION("E-ext cell with a false side predicate") {
        CHECK(t035SkipReason(defComp, C::E6SyncRateHi) == "side predicate");
        CHECK(t035SkipReason(defComp, C::E7SelfAffinityHi) == "side predicate");
        CHECK(t035SkipReason(t035State({{::Vorago::kEcosystemSyncRateId, 1.0}}),
                             C::E6SyncRateHi)
                  .empty());
    }
    SECTION("otherwise empty") {
        CHECK(t035SkipReason(defComp, C::S1Noise).empty());
        CHECK(t035SkipReason(defComp, C::S7Ecosystem).empty());
        CHECK(t035SkipReason(defComp, C::E3NoiseWake).empty());
    }
}

TEST_CASE("Vorago_PresetSupport_VectorOnShortTimeline", "[vorago][preset]") {
    // Mechanics only: no verdict is asserted on a shortened timeline.
    using C = PD::Capability;
    VoragoTest::SweepTimeline tl;
    tl.A = 2.0;
    tl.sus0 = 2.0;
    tl.sus1 = 12.0;
    for (int k = 0; k < 3; ++k) {
        tl.m[k][0] = 2.0 + (10.0 * static_cast<double>(k));
        tl.m[k][1] = 12.0 + (10.0 * static_cast<double>(k));
    }
    tl.H = 32.0;
    tl.tail0 = 32.0;
    tl.tail1 = 32.0;
    tl.total = 32.0;

    // P_Sus: the default surface's stored-seed (index 0) Sus, rendered like a twin
    // with no override; s(P) from its half-window self-distance.
    const std::vector<std::uint8_t> comp;  // empty: the default surface
    VoragoTest::RenderSpec spec;
    spec.seedIndex = 0;
    spec.sr = VoragoTest::kSweepSampleRate;
    spec.noteOffAt = tl.H;
    spec.end = tl.sus1;
    spec.capture = {{tl.sus0, tl.sus1}};
    const VoragoTest::SweepCapture cap = VoragoTest::renderPreset(spec);
    REQUIRE(cap.finite);
    REQUIRE(cap.capL.size() == 1u);
    REQUIRE_FALSE(cap.capL[0].empty());
    const VoragoTest::PresetDescriptor pSus =
        VoragoTest::describe(cap.capL[0], cap.capR[0], spec.sr);
    const double selfDistance = VoragoTest::susHalfSelfDistance(cap, spec.sr);
    REQUIRE(Krate::DSP::detail::isFinite(selfDistance));

    const VoragoTest::VerificationVector vec = VoragoTest::computeVerificationVector(
        nullptr, comp, tl, selfDistance, pSus, t035PoolWidth());

    REQUIRE(vec.cells.size() == PD::kNumCapabilities);
    REQUIRE(vec.cells.size() == 79u);
    std::size_t renderedCount = 0;
    for (std::size_t i = 0; i < vec.cells.size(); ++i) {
        const auto c = static_cast<C>(i);
        const VoragoTest::CellOutcome& o = vec.cells[i];
        INFO("cell " << std::string(PD::cellSpecs()[i].label) << ": rendered " << o.rendered
                     << ", d " << o.d << ", skip [" << o.skip << "]");
        CHECK(Krate::DSP::detail::isFinite(o.d));
        if (VoragoTest::isRenderScored(c, C::Count)) {
            CHECK((o.rendered || !o.skip.empty()));
            if (o.rendered) {
                ++renderedCount;
                CHECK(o.skip.empty());
                CHECK(Krate::DSP::detail::isFinite(o.twoS));
            }
        } else if (VoragoTest::detail::capInRange(c, C::D3Direct, C::D3MetallicHiss)) {
            // D3 primary rule (2026-09-30): the cell carries its S1 conjunct's
            // render terms (pass 3), so it reads exactly like S1 here.
            const VoragoTest::CellOutcome& s1 = vec.cells[static_cast<std::size_t>(C::S1Noise)];
            CHECK(o.rendered == s1.rendered);
            CHECK(o.d == s1.d);
            CHECK(o.skip == (s1.rendered ? std::string() : s1.skip));
            CHECK(o.twoS == 0.0);
        } else {
            CHECK_FALSE(o.rendered);
            CHECK(o.skip == "state-only kind");
            CHECK(o.d == 0.0);
            CHECK(o.twoS == 0.0);
        }
    }
    // The default surface renders its S cells and E1-E5 (depth 0.85): not vacuous.
    CHECK(vec.cells[static_cast<std::size_t>(C::S1Noise)].rendered);
    CHECK(vec.cells[static_cast<std::size_t>(C::E3NoiseWake)].rendered);
    CHECK(vec.cells[static_cast<std::size_t>(C::E3NoiseWake)].attribBase >= 0.0);
    CHECK(vec.cells[static_cast<std::size_t>(C::S7Ecosystem)].rendered);
    CHECK(renderedCount >= 7u);  // S1, S7 and E1-E5 at least
}

// ==============================================================================
// T036 - plan 6.3 freeze gesture, FR-033a rates, FR-038 reproducibility, plan
// 6.13 controls and computeSweepRecord (per-push: the pure pieces only; the
// renders run in the [long][vorago-sweep] cases).
// ==============================================================================

TEST_CASE("Vorago_PresetSupport_FreezeGestureBlock", "[vorago][preset]") {
    // Plan 6.1: kSpaceFreezeId -> 1.0 lands in the first 512-sample block whose
    // start sample is >= llround((A + 65) * sr).
    SECTION("A = 155 s (the default surface): A + 65 = 220 s falls on a block edge") {
        const VoragoTest::GestureBlock g = VoragoTest::freezeGestureBlock(155.0, 48000.0, 512);
        CHECK(g.block == 20625);
        CHECK(g.startSample == 10560000);
        CHECK(g.latenessSamples == 0);
    }
    SECTION("A = 10.3 s: the gesture is late by less than one block") {
        const VoragoTest::GestureBlock g = VoragoTest::freezeGestureBlock(10.3, 48000.0, 512);
        CHECK(g.block == 7060);
        CHECK(g.startSample == 3614720);
        CHECK(g.latenessSamples == 320);
        CHECK(g.latenessSamples <= 511);
        CHECK(static_cast<double>(g.latenessSamples) / 48000.0 <= 0.0107);  // <= 10.7 ms
    }
}

namespace {

/// A hand-built record for the control-set rule: only the fields controlSet reads.
VoragoTest::SweepRecord controlRecord(const char* name, double s, bool s8Primary, double pressure,
                                      double weight) {
    VoragoTest::SweepRecord r;
    r.name = name;
    r.selfDistance = s;
    r.storedPressure = pressure;
    r.storedWeight = weight;
    // computeVerificationVector sets D10.1's stateOk iff the preset's primary is S8.
    r.vec.cells[static_cast<std::size_t>(PD::Capability::D10FreezeHolds)].stateOk = s8Primary;
    return r;
}

std::vector<std::size_t> sortedMembers(std::vector<std::size_t> v) {
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace

TEST_CASE("Vorago_PresetSupport_ControlSet", "[vorago][preset]") {
    SECTION("argmax / argmin s(P), the S8 preset, highest Pressure, highest Weight; deduplicated") {
        std::vector<VoragoTest::SweepRecord> recs;
        recs.push_back(controlRecord("r0", 1.0, false, 0.2, 0.1));
        recs.push_back(controlRecord("r1 max s, max Weight", 3.0, false, 0.1, 0.9));
        recs.push_back(controlRecord("r2 min s", 0.5, false, 0.3, 0.2));
        recs.push_back(controlRecord("r3 S8, max Pressure", 2.0, true, 0.8, 0.3));
        recs.push_back(controlRecord("r4", 1.5, false, 0.4, 0.4));
        const VoragoTest::ControlSet cs = VoragoTest::controlSet(recs);
        CHECK(cs.ok);
        CHECK(sortedMembers(cs.members) == std::vector<std::size_t>{1u, 2u, 3u});
    }
    SECTION("five distinct roles give five members") {
        std::vector<VoragoTest::SweepRecord> recs;
        recs.push_back(controlRecord("max s", 3.0, false, 0.1, 0.1));
        recs.push_back(controlRecord("min s", 0.2, false, 0.2, 0.2));
        recs.push_back(controlRecord("S8", 1.0, true, 0.3, 0.3));
        recs.push_back(controlRecord("max Pressure", 1.1, false, 0.9, 0.4));
        recs.push_back(controlRecord("max Weight", 1.2, false, 0.5, 0.95));
        recs.push_back(controlRecord("none", 1.3, false, 0.0, 0.0));
        const VoragoTest::ControlSet cs = VoragoTest::controlSet(recs);
        CHECK(cs.ok);
        CHECK(sortedMembers(cs.members) == std::vector<std::size_t>{0u, 1u, 2u, 3u, 4u});
    }
    SECTION("fewer than 3 distinct presets -> reported as a failure (FR-017 path)") {
        std::vector<VoragoTest::SweepRecord> recs;
        recs.push_back(controlRecord("a: max s, S8, max Pressure, max Weight", 2.0, true, 0.9, 0.9));
        recs.push_back(controlRecord("b: min s", 1.0, false, 0.1, 0.1));
        const VoragoTest::ControlSet cs = VoragoTest::controlSet(recs);
        CHECK_FALSE(cs.ok);
        CHECK(sortedMembers(cs.members) == std::vector<std::size_t>{0u, 1u});
        CHECK_FALSE(VoragoTest::controlSet(std::vector<VoragoTest::SweepRecord>{}).ok);
    }
    SECTION("level twin (a): T026's synthetic signal x 10^(-6/20) scores d < 0.05") {
        constexpr double kSr = 48000.0;
        std::vector<std::vector<float>> L(1);
        std::vector<std::vector<float>> R(1);
        VoragoTest::makeDescriptorTestSignal(static_cast<std::size_t>(3u * 48000u), kSr, L[0], R[0]);
        const double d = VoragoTest::levelTwinD(L, R, kSr);
        INFO("level-twin d " << d);
        REQUIRE(Krate::DSP::detail::isFinite(d));
        CHECK(d < VoragoTest::kLevelTwinBound);

        // levelTwinD is exactly d(describe(buffers), describe(buffers x 10^(-6/20))).
        std::vector<float> sL = L[0];
        std::vector<float> sR = R[0];
        const auto gain = static_cast<float>(VoragoTest::kLevelTwinGain);
        for (float& x : sL) {
            x *= gain;
        }
        for (float& x : sR) {
            x *= gain;
        }
        const double direct = VoragoTest::descriptorDistance(VoragoTest::describe(L[0], R[0], kSr),
                                                             VoragoTest::describe(sL, sR, kSr));
        CHECK(std::fabs(d - direct) <= 1e-12);
        CHECK(std::fabs(VoragoTest::kLevelTwinGain - std::pow(10.0, -6.0 / 20.0)) <= 1e-15);
    }
}

// T040 - the ruled take count K, frozen after gate G2. Source: compliance.md
// "## FR-017a pilot / G2" ("Pilot run 3 - gate G2") and the pilot log
// specs/vorago-phase14-presets-release/artifacts/pilot_calibrate_run3.log:43,
// "G2: PROCEED with K = 4" (run 1, artifacts/pilot_calibrate.log, stopped at G2
// and was re-run under the user's G2 rulings).
TEST_CASE("Vorago_PresetSupport_RuledTakes", "[vorago][preset]") {
    static_assert(VoragoTest::kRuledTakes >= 1 &&
                  VoragoTest::kRuledTakes <= VoragoTest::kMaxTakes);
    REQUIRE(VoragoTest::kRuledTakes == 4);
}

// Vorago Phase 13c T043 - FR-016 (b) measured attack window (plan 3.3; per-push,
// arithmetic only). W_end = max(reach_P, reach_rev) + 5 s; nullopt when either
// reach is absent or later than captureEnd - 5 (the boundary is inclusive).
TEST_CASE("Vorago_PresetSweep_MeasuredAttackWindow", "[vorago][preset]") {
    using VoragoTest::measuredAttackWindowEndSeconds;
    constexpr double kTol = 1e-9;

    SECTION("both reaches inside the capture -> max(reach) + 5 s") {
        const std::optional<double> w = measuredAttackWindowEndSeconds(3.0, 7.0, 60.0);
        REQUIRE(w.has_value());
        CHECK(std::fabs(*w - 12.0) < kTol);
    }
    SECTION("a missing reach -> nullopt") {
        CHECK_FALSE(measuredAttackWindowEndSeconds(std::nullopt, 7.0, 60.0).has_value());
        CHECK_FALSE(measuredAttackWindowEndSeconds(3.0, std::nullopt, 60.0).has_value());
    }
    SECTION("a reach later than captureEnd - 5 -> nullopt") {
        CHECK_FALSE(measuredAttackWindowEndSeconds(3.0, 56.0, 60.0).has_value());
    }
    SECTION("a reach exactly at captureEnd - 5 -> captureEnd (boundary inclusive)") {
        const std::optional<double> w = measuredAttackWindowEndSeconds(55.0, 3.0, 60.0);
        REQUIRE(w.has_value());
        CHECK(std::fabs(*w - 60.0) < kTol);
    }
}

// ==============================================================================
// T041 - the [long][vorago-sweep] cases (FR-033, FR-033a, FR-037, FR-038;
// SC-011, SC-012, SC-015, SC-022, SC-024). Never in the per-push filter.
// Records come from sweepRecordFor one index at a time: computeSweepRecord
// already fans each preset's renders out through runJobs on the pool width, so
// indices are not also run concurrently (plan 5.8 Pool: <= 4 concurrent jobs
// per process, the 7 GB macOS runner memory budget).
// ==============================================================================

namespace {

/// The definition indices of this process's shard, ascending: 0..N-1, plus the
/// default-surface pseudo-preset N when `withPseudo`. REQUIREs a well-formed shard.
std::vector<std::size_t> t041ShardIndices(bool withPseudo) {
    const VoragoTest::Shard shard = VoragoTest::shardFromEnv();
    INFO("VORAGO_SWEEP_SHARD must be i/n with 0 <= i < n");
    REQUIRE(shard.count > 0u);
    const std::size_t n = PD::allPresets().size();
    const std::size_t last = withPseudo ? n + 1u : n;
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < last; ++i) {
        if (VoragoTest::inShard(i, shard)) {
            out.push_back(i);
        }
    }
    std::printf("shard %zu/%zu: %zu of %zu indices (N = %zu%s)\n", shard.index, shard.count,
                out.size(), last, n, withPseudo ? ", index N = default surface" : "");
    std::fflush(stdout);
    return out;
}

/// The record of `defIndex`, REQUIREd complete (K takes). A record that failed to
/// build, decode or load carries no takes and the reason in its name.
const VoragoTest::SweepRecord& t041Record(std::size_t defIndex) {
    std::printf("[sweep] index %zu: computing / loading record...\n", defIndex);
    std::fflush(stdout);
    const VoragoTest::SweepRecord& rec = VoragoTest::sweepRecordFor(defIndex);
    INFO("index " << defIndex << ": " << rec.name);
    REQUIRE(rec.takes.size() == static_cast<std::size_t>(VoragoTest::kRuledTakes));
    return rec;
}

std::string t041Label(PD::Capability c) {
    const auto i = static_cast<std::size_t>(c);
    if (i >= PD::kNumCapabilities) {
        return "<none>";
    }
    return std::string(PD::cellSpecs()[i].label);
}

const char* t041Yes(bool b) {
    return b ? "yes" : "NO";
}

}  // namespace

TEST_CASE("Vorago_PresetSweep_LongRender", "[vorago][preset][long][vorago-sweep]") {
    const std::vector<std::size_t> indices = t041ShardIndices(false);

    std::printf("\n=== Vorago_PresetSweep_LongRender (FR-033, SC-012; K = %d) ===\n",
                VoragoTest::kRuledTakes);
    for (const std::size_t i : indices) {
        const VoragoTest::SweepRecord& rec = t041Record(i);
        const VoragoTest::SweepTimeline& tl = rec.tl;
        std::printf("\n%s (index %zu): A %.2f s  Rel %.2f s  RT60 %.2f s  H %.2f s  "
                    "Tail [%.2f, %.2f]  Total %.2f s  freeze tail %s\n",
                    rec.name.c_str(), i, tl.A, tl.rel, tl.rt60, tl.H, tl.tail0, tl.tail1,
                    tl.total, tl.freezeOnTail ? "On" : "Off");
        for (std::size_t j = 0; j < rec.takes.size(); ++j) {
            const VoragoTest::TakeRecord& t = rec.takes[j];
            std::printf("  take %zu seed %2d: finite %s  peak %.4f  arm1 hi %.2f dB [%s]  "
                        "arm2 lo %.2f dB [%s]  arm3 late-sus %+.2f dB [%s]  arm4 tail %.2f dB [%s]\n",
                        j, t.seedIndex, t041Yes(t.finite), static_cast<double>(t.peak),
                        t.worstHiDb, t041Yes(t.armPass[0]), t.worstLoDb, t041Yes(t.armPass[1]),
                        t.lateVsSusDb, t041Yes(t.armPass[2]), t.tailDb, t041Yes(t.armPass[3]));
            INFO(rec.name << " take " << j << " (seed " << t.seedIndex << "): peak " << t.peak
                          << ", hi " << t.worstHiDb << ", lo " << t.worstLoDb << ", late-sus "
                          << t.lateVsSusDb << ", tail " << t.tailDb);
            CHECK(t.finite);
            CHECK(t.armPass[0]);  // arm 1: finite, peak <= 0.9661, every 10 s <= -6 dBFS
            CHECK(t.armPass[1]);  // arm 2: every 10 s window of [A, H] >= -60 dBFS
            CHECK(t.armPass[2]);  // arm 3: late vs Sus in [-18, +12] dB
            CHECK(t.armPass[3]);  // arm 4: the tail arm for the stored freeze
        }
        std::fflush(stdout);
    }
}

TEST_CASE("Vorago_PresetSweep_FreezeGesture", "[vorago][preset][long][vorago-sweep]") {
    const std::vector<std::size_t> indices = t041ShardIndices(false);
    const std::vector<PD::VoragoPresetDef>& defs = PD::allPresets();
    std::vector<std::size_t> s8;
    for (std::size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].primary == PD::Capability::S8Cavern) {
            s8.push_back(i);
        }
    }
    INFO("SC-024 needs the S8-primary preset in allPresets()");
    REQUIRE_FALSE(s8.empty());

    std::printf("\n=== Vorago_PresetSweep_FreezeGesture (FR-033, SC-024) ===\n");
    std::size_t scored = 0;
    for (const std::size_t i : s8) {
        if (std::find(indices.begin(), indices.end(), i) == indices.end()) {
            std::printf("%s (index %zu): not in this shard, skipped\n",
                        std::string(defs[i].name).c_str(), i);
            continue;
        }
        const VoragoTest::SweepRecord& rec = t041Record(i);
        const VoragoTest::GestureResult& g = rec.gesture;
        const double susDb = g.floorDb + VoragoTest::kFreezeFloorDb;  // floorDb = RMS(Sus) - 20
        const double dryBound = susDb - VoragoTest::kTailDropDb;
        std::printf("%s (index %zu): RMS(Sus) %.2f dB  G loudest %.2f  G last %.2f  "
                    "G floor %.2f  G0 loudest %.2f (bound %.2f)  G - G0 %.2f dB  pass %s\n",
                    rec.name.c_str(), i, susDb, g.loudestDb, g.lastDb, g.floorDb,
                    g.dryLoudestDb, dryBound, g.loudestDb - g.dryLoudestDb, t041Yes(g.pass));
        std::fflush(stdout);
        INFO(rec.name << ": G loudest " << g.loudestDb << ", last " << g.lastDb << ", floor "
                      << g.floorDb << ", G0 loudest " << g.dryLoudestDb);
        // G: arm 1 and the Freeze-On arm 4 (floor included); G0: its -40 dB bound.
        CHECK(g.pass);
        CHECK(g.loudestDb >= g.floorDb);
        CHECK(g.dryLoudestDb <= dryBound);
        const VoragoTest::CellOutcome& d10 =
            rec.vec.cells[static_cast<std::size_t>(PD::Capability::D10FreezeHolds)];
        CHECK(VoragoTest::verifiedAt(d10, PD::Capability::D10FreezeHolds,
                                     VoragoTest::ClaimRole::Secondary));
        ++scored;
    }
    if (scored == 0u) {
        // Not a Catch2 SKIP: a filtered run whose only case skipped would exit non-zero.
        SUCCEED("the S8-primary preset is not in this shard");
    }
}

TEST_CASE("Vorago_PresetSweep_SustainAtAllRates", "[vorago][preset][long][vorago-sweep]") {
    const std::vector<std::size_t> indices = t041ShardIndices(false);

    std::printf("\n=== Vorago_PresetSweep_SustainAtAllRates (FR-033a, SC-022) ===\n");
    for (const std::size_t i : indices) {
        const VoragoTest::SweepRecord& rec = t041Record(i);
        const VoragoTest::RateResult& r = rec.rates;
        std::printf("%s (index %zu): [0, %.2f s]  44.1 kHz finite %s peak %.4f hi %.2f dB  |  "
                    "96 kHz finite %s peak %.4f hi %.2f dB\n",
                    rec.name.c_str(), i, rec.tl.A + VoragoTest::kGestureAfterAttackSeconds,
                    t041Yes(r.finite441), static_cast<double>(r.peak441), r.worstHi441,
                    t041Yes(r.finite96), static_cast<double>(r.peak96), r.worstHi96);
        std::fflush(stdout);
        INFO(rec.name << ": 44.1 kHz peak " << r.peak441 << " hi " << r.worstHi441
                      << "; 96 kHz peak " << r.peak96 << " hi " << r.worstHi96);
        CHECK(r.finite441);
        CHECK(r.peak441 <= VoragoTest::kSweepPeakCeiling);
        CHECK(r.worstHi441 <= VoragoTest::kRunawayDb);
        CHECK(r.finite96);
        CHECK(r.peak96 <= VoragoTest::kSweepPeakCeiling);
        CHECK(r.worstHi96 <= VoragoTest::kRunawayDb);
    }
}

TEST_CASE("Vorago_PresetSweep_AblationVerifiesClaims", "[vorago][preset][long][vorago-sweep]") {
    using VoragoTest::ClaimRole;
    const std::vector<std::size_t> indices = t041ShardIndices(true);
    const std::vector<PD::VoragoPresetDef>& defs = PD::allPresets();

    std::printf("\n=== Vorago_PresetSweep_AblationVerifiesClaims (FR-012, FR-037, SC-011) ===\n");
    for (const std::size_t i : indices) {
        const VoragoTest::SweepRecord& rec = t041Record(i);
        const PD::VoragoPresetDef* def = (i < defs.size()) ? &defs[i] : nullptr;
        const PD::Capability primary = (def != nullptr) ? def->primary : PD::Capability::Count;

        std::printf("\n%s (index %zu): primary %s, %zu secondaries, s(P) %.4f\n",
                    rec.name.c_str(), i, t041Label(primary).c_str(),
                    (def != nullptr) ? def->secondaries.size() : std::size_t{0}, rec.selfDistance);
        std::printf("  %-28s %-5s %-5s %-4s %-5s %10s %10s %10s  %-3s %-3s  %s\n", "cell", "claim",
                    "state", "conj", "rend", "d", "2s(P)", "attrib", "sec", "pri", "skip");
        for (std::size_t c = 0; c < rec.vec.cells.size(); ++c) {
            const auto cell = static_cast<PD::Capability>(c);
            const VoragoTest::CellOutcome& o = rec.vec.cells[c];
            const char* claim = "";
            if (def != nullptr) {
                if (cell == primary) {
                    claim = "PRI";
                } else if (std::find(def->secondaries.begin(), def->secondaries.end(), cell) !=
                           def->secondaries.end()) {
                    claim = "sec";
                }
            }
            std::printf("  %-28s %-5s %-5s %-4s %-5s %10.4f %10.4f %10.4f  %-3s %-3s  %s\n",
                        t041Label(cell).c_str(), claim, t041Yes(o.stateOk), t041Yes(o.conjunctOk),
                        t041Yes(o.rendered), o.d, o.twoS, o.attribBase,
                        t041Yes(VoragoTest::verifiedAt(o, cell, ClaimRole::Secondary)),
                        t041Yes(VoragoTest::verifiedAt(o, cell, ClaimRole::Primary)),
                        o.skip.c_str());
        }
        std::fflush(stdout);

        if (def == nullptr) {
            // The default surface: vector recorded (the default-state set), no claims.
            std::printf("  (default surface: vector recorded, no claims)\n");
            continue;
        }

        const auto outcome = [&rec](PD::Capability c) -> const VoragoTest::CellOutcome& {
            return rec.vec.cells[static_cast<std::size_t>(c)];
        };
        {
            const VoragoTest::CellOutcome& o = outcome(primary);
            INFO(rec.name << " primary " << t041Label(primary) << ": d " << o.d << ", 2s(P) "
                          << o.twoS << ", attrib " << o.attribBase << ", skip [" << o.skip
                          << "]");
            // The one rule (plan 6.12): the primary verifies at 1.5 and at F = 4.0.
            CHECK(VoragoTest::verifiedAt(o, primary, ClaimRole::Secondary));
            CHECK(VoragoTest::verifiedAt(o, primary, ClaimRole::Primary));
        }
        for (const PD::Capability c : def->secondaries) {
            const VoragoTest::CellOutcome& o = outcome(c);
            INFO(rec.name << " secondary " << t041Label(c) << ": d " << o.d << ", 2s(P) "
                          << o.twoS << ", attrib " << o.attribBase << ", skip [" << o.skip
                          << "]");
            CHECK(VoragoTest::verifiedAt(o, c, ClaimRole::Secondary));
        }
    }
}

TEST_CASE("Vorago_PresetSweep_RendersAreReproducible", "[vorago][preset][long][vorago-sweep]") {
    const std::vector<std::size_t> indices = t041ShardIndices(false);

    std::printf("\n=== Vorago_PresetSweep_RendersAreReproducible (FR-038, SC-015) ===\n");
    for (const std::size_t i : indices) {
        const VoragoTest::SweepRecord& rec = t041Record(i);
        std::printf("%s (index %zu): two fresh hosts over [0, %.2f s], withinTolerance per "
                    "channel: %s\n",
                    rec.name.c_str(), i, rec.tl.A + VoragoTest::kGestureAfterAttackSeconds,
                    t041Yes(rec.reproducible));
        std::fflush(stdout);
        INFO(rec.name << ": compareFingerprints(...).withinTolerance() on L and R");
        CHECK(rec.reproducible);
    }
}
