// ==============================================================================
// Layer 2: Processor Tests - TransientShaper
//                              (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// Reference: specs/profundum-phase6-mass-dynamics/spec.md (FR-001 - FR-009, FR-040 - FR-042,
//            FR-047, FR-049; SC-008, SC-009, SC-011 shaper, SC-012/013/016/018/021 shaper arms, SC-025)
//            specs/profundum-phase6-mass-dynamics/plan.md (S4, S1 C-2, C-5, C-10, C-11, S8.1, S12.2, S13.3)
//            specs/profundum-phase6-mass-dynamics/tasks.md (T002 wires this TU, T006 fills it)
//
// SCOPE (tags [processors][profundum][transient-shaper]):
//   TransientShaper_RangesAndDefaults        SC-025, FR-001, FR-009
//   TransientShaper_TransparentAtNeutral     SC-013 shaper arm
//   TransientShaper_GainsMatchParameters     SC-008 (a)(b)(e)(f)
//   TransientShaper_DurationMatches          SC-008 (c), 20 / 40 / 100 / 250 ms
//   TransientShaper_DurationShortestCell     SC-008 (c), 5 ms (plan S1 C-2, RULED (a) 2026-10-17)
//   TransientShaper_StepChangesClickFree     SC-008 (d)
//   TransientShaper_NoWaveformRateRipple     SC-009, FR-004
//   TransientShaper_Audibility               SC-011 shaper, FR-043
//   TransientShaper_MonoSum                  SC-012 shaper arm, FR-005, FR-041
//   TransientShaper_NoAllocation             SC-016 shaper arm
//   TransientShaper_PartitionInvariant       SC-018 shaper arm, FR-049
//   TransientShaper_SampleRates              SC-021 shaper arm
//   TransientShaper_Lifecycle                FR-047
//   TransientShaper_DurationTableHarvest     plan S8.1, hidden [.calibration]; run ONLY by the main
//                                            loop in T019 (writes evidence/duration-table.txt).
//                                            It does NOT carry [transient-shaper]: a Catch2 tag filter
//                                            selects hidden cases too (catch_test_spec.cpp Filter::matches:
//                                            a required pattern sets should_use = true), so the gate's
//                                            "[transient-shaper]~[long]" would otherwise run the harvest.
//
// Harvest-dependent cells (expected-red until T019 pastes the harvested duration table): the
// SC-008(c) durations in TransientShaper_DurationMatches / _DurationShortestCell read the seed table.
//
//   Layer rule: no systems/ include, directly or through a helper.
// ==============================================================================

#include "shape_character_metric_helpers.h"
#include "transient_shaper_probe.h"
#include "transient_shaper_test_helpers.h"

// allocation_detector.h ONLY. NEVER allocation_operator_overrides.h: the global operator
// new/delete replacements are included from exactly one TU of dsp_processors_tests
// (brownian_drift_test.cpp); a second include is a duplicate-symbol link error.
#include <allocation_detector.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/transient_shaper.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;
namespace tst = Krate::DSP::TransientShaperTest;
namespace sct = Krate::DSP::ShapeCharacterTest;
using Tone = tst::Tone;
using Probe = Krate::DSP::detail::TransientShaperProbe;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kHostBlock = 512;
constexpr std::array<float, 5> kGrid{-12.0f, -6.0f, 0.0f, 6.0f, 12.0f};

const char* toneName(Tone t) { return t == Tone::Sine ? "sine" : "saw"; }

float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t b = sink;
    return std::bit_cast<float>(b);
}

/// Per-sample taps (FR-006), recorded from `tapFrom` on (zero before).
struct TsTaps {
    std::vector<float> gainDb;
    std::vector<float> c;
    std::vector<float> fast;
    std::vector<float> slow;
    std::vector<float> aS;
    std::vector<float> sS;
};

/// Setters, then prepare (the setters' values are applied by prepare and the smoothers snapped).
void tsConfigure(TransientShaper& sh, float attackDb, float sustainDb, float durationMs, double fs = kFs) {
    sh.setAttackGainDb(attackDb);
    sh.setSustainGainDb(sustainDb);
    sh.setTransientDurationMs(durationMs);
    sh.prepare(fs, kHostBlock);
}

/// Processes L (and R; R == nullptr -> one channel, right == left) in place in host blocks of
/// `block`. With `taps`, the samples from `tapFrom` on are processed one at a time and every tap is
/// recorded per sample (the shaper is partition-invariant, SC-018, so this reads the same signal).
void tsRender(TransientShaper& sh, float* L, float* R, std::size_t n, std::size_t block, TsTaps* taps = nullptr,
              std::size_t tapFrom = 0) {
    REQUIRE(block >= 1);
    float* right = (R == nullptr) ? L : R;
    if (taps != nullptr) {
        for (std::vector<float>* v : {&taps->gainDb, &taps->c, &taps->fast, &taps->slow, &taps->aS, &taps->sS})
            v->assign(n, 0.0f);
    }
    const std::size_t blockEnd = (taps != nullptr) ? std::min(tapFrom, n) : n;
    std::size_t pos = 0;
    while (pos < blockEnd) {
        const std::size_t len = std::min(block, blockEnd - pos);
        sh.processBlock(L + pos, right + pos, len);
        pos += len;
    }
    if (taps == nullptr)
        return;
    for (; pos < n; ++pos) {
        sh.processBlock(L + pos, right + pos, 1);
        taps->gainDb[pos] = sh.currentGainDb();
        taps->c[pos] = sh.transientFraction();
        taps->fast[pos] = sh.fastEnvelope();
        taps->slow[pos] = sh.slowEnvelope();
        taps->aS[pos] = sh.smoothedAttackDb();
        taps->sS[pos] = sh.smoothedSustainDb();
    }
}

/// c by the plan S4.3 step-5 rule, in double, from the recorded envelopes.
double lawC(float fast, float slow) {
    const auto floorLin = static_cast<double>(TransientShaper::kSilenceFloorLin);
    const auto f = static_cast<double>(fast);
    const auto s = static_cast<double>(slow);
    if (s <= floorLin)
        return f > floorLin ? 1.0 : 0.0;
    if (f <= 0.0)
        return 0.0;
    return std::clamp(20.0 * std::log10(f / s) / static_cast<double>(TransientShaper::kDifferentialRangeDb), 0.0, 1.0);
}

double maxOf(std::span<const float> v, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= v.size());
    return static_cast<double>(*std::max_element(v.begin() + static_cast<std::ptrdiff_t>(begin),
                                                 v.begin() + static_cast<std::ptrdiff_t>(end)));
}

double minOf(std::span<const float> v, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= v.size());
    return static_cast<double>(*std::min_element(v.begin() + static_cast<std::ptrdiff_t>(begin),
                                                 v.begin() + static_cast<std::ptrdiff_t>(end)));
}

bool bytesEqual(const float* a, const float* b, std::size_t n) { return std::memcmp(a, b, n * sizeof(float)) == 0; }

/// SC-008(c) cell: A = +12, S = 0, duration d, 48 kHz (or fs), on a burst whose onset is sample 0.
/// argOverrideMs > 0 sets the slow attack argument through the probe (the duration harvest).
/// Returns the transient time in ms (a +inf reading is returned as 1e9 so no inf reaches arithmetic).
double cellTransientTimeMs(const std::vector<float>& x, float durationMs, double fs = kFs, float argOverrideMs = 0.0f) {
    TransientShaper sh;
    tsConfigure(sh, 12.0f, 0.0f, durationMs, fs);
    if (argOverrideMs > 0.0f)
        Probe::setSlowAttackArgMs(sh, argOverrideMs);
    std::vector<float> y = x;
    TsTaps taps;
    tsRender(sh, y.data(), nullptr, y.size(), kHostBlock, &taps, 0);
    const double tt = tst::transientTimeMs(std::span<const float>(taps.gainDb), 0, fs);
    return ProfundumTest::isPosInf(tt) ? 1e9 : tt;
}

struct NoteCell {
    Tone tone;
    double f0;
    const char* note;
};

std::string evidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase6-mass-dynamics/evidence/";
}

/// A float literal in the header's form: setprecision(9), always a decimal point, `f` suffix.
std::string floatLiteral(float v, int precision) {
    std::ostringstream os;
    os << std::setprecision(precision) << static_cast<double>(v);
    std::string s = os.str();
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
        s += ".0";
    return s + "f";
}

}  // namespace

// =============================================================================
// SC-025, FR-001, FR-009
// =============================================================================

TEST_CASE("TransientShaper_RangesAndDefaults", "[processors][profundum][transient-shaper]") {
    static_assert(TransientShaper::latencySamples() == 0);
    static_assert(TransientShaper::kDurationTable.size() == 11);

    const float nanValue = floatFromBits(0x7FC00000u);
    const float posInf = floatFromBits(0x7F800000u);
    const float negInf = floatFromBits(0xFF800000u);

    TransientShaper sh;
    CHECK(sh.attackGainDb() == 6.0f);
    CHECK(sh.sustainGainDb() == 0.0f);
    CHECK(sh.transientDurationMs() == 40.0f);

    for (int pass = 0; pass < 2; ++pass) {
        INFO("pass " << pass << (pass == 0 ? " (before prepare)" : " (after prepare)"));
        if (pass == 1)
            sh.prepare(kFs, kHostBlock);

        sh.setAttackGainDb(20.0f);
        CHECK(sh.attackGainDb() == 12.0f);
        sh.setAttackGainDb(-20.0f);
        CHECK(sh.attackGainDb() == -12.0f);
        sh.setSustainGainDb(20.0f);
        CHECK(sh.sustainGainDb() == 12.0f);
        sh.setSustainGainDb(-20.0f);
        CHECK(sh.sustainGainDb() == -12.0f);
        sh.setTransientDurationMs(1.0f);
        CHECK(sh.transientDurationMs() == 5.0f);
        sh.setTransientDurationMs(1000.0f);
        CHECK(sh.transientDurationMs() == 250.0f);

        sh.setAttackGainDb(3.0f);
        sh.setSustainGainDb(-4.0f);
        sh.setTransientDurationMs(77.0f);
        for (const float bad : {nanValue, posInf, negInf}) {
            sh.setAttackGainDb(bad);
            sh.setSustainGainDb(bad);
            sh.setTransientDurationMs(bad);
            CHECK(sh.attackGainDb() == 3.0f);
            CHECK(sh.sustainGainDb() == -4.0f);
            CHECK(sh.transientDurationMs() == 77.0f);
        }
    }
    CHECK(sh.stateFinite());
}

// =============================================================================
// SC-013 shaper arm
// =============================================================================

TEST_CASE("TransientShaper_TransparentAtNeutral", "[processors][profundum][transient-shaper]") {
    const std::size_t from = tst::secondsToSamples(kFs, 0.1);
    for (const Tone tone : {Tone::Saw, Tone::Sine}) {
        for (const double f0 : {tst::kE0Hz, tst::kC3Hz}) {
            const std::vector<float> x = tst::steadyTone(tone, f0, kFs, 2.0);
            const std::size_t n = x.size();
            std::vector<float> L = x;
            std::vector<float> R = x;
            TransientShaper sh;
            tsConfigure(sh, 0.0f, 0.0f, TransientShaper::kDefaultDurationMs);
            tsRender(sh, L.data(), R.data(), n, kHostBlock);
            const bool eqL = bytesEqual(L.data() + from, x.data() + from, n - from);
            const bool eqR = bytesEqual(R.data() + from, x.data() + from, n - from);
            std::printf("TransientShaper_TransparentAtNeutral: %s %.3f Hz: L %s, R %s\n", toneName(tone), f0,
                        eqL ? "bit-identical" : "DIFFERS", eqR ? "bit-identical" : "DIFFERS");
            CHECK(eqL);
            CHECK(eqR);
        }
    }
}

// =============================================================================
// SC-008 (a)(b)(e)(f)
// =============================================================================

TEST_CASE("TransientShaper_GainsMatchParameters", "[processors][profundum][transient-shaper]") {
    const std::array<NoteCell, 4> cells{{{.tone = Tone::Sine, .f0 = tst::kE1Hz, .note = "E1"},
                                         {.tone = Tone::Saw, .f0 = tst::kE1Hz, .note = "E1"},
                                         {.tone = Tone::Sine, .f0 = tst::kC3Hz, .note = "C3"},
                                         {.tone = Tone::Saw, .f0 = tst::kC3Hz, .note = "C3"}}};
    const std::size_t steadyBegin = tst::secondsToSamples(kFs, 0.85);
    const std::size_t steadyEnd = tst::secondsToSamples(kFs, 1.00);
    const std::size_t onsetEnd = tst::secondsToSamples(kFs, 0.050) + 1;
    const std::size_t lawEnd = tst::secondsToSamples(kFs, 1.00);

    for (const NoteCell& cell : cells) {
        const std::vector<float> x = tst::burst(cell.tone, cell.f0, kFs, 1.0, 0.0, 0.25);
        const std::size_t n = x.size();
        double worstA = 0.0;
        double worstB = 0.0;
        double worstF = 0.0;
        for (const float A : kGrid) {
            for (const float S : kGrid) {
                INFO(toneName(cell.tone) << " " << cell.note << " A " << A << " S " << S);
                TransientShaper sh;
                tsConfigure(sh, A, S, 40.0f);
                std::vector<float> y = x;
                TsTaps taps;
                tsRender(sh, y.data(), nullptr, n, kHostBlock, &taps, 0);
                const std::span<const float> g(taps.gainDb);

                // (a) steady gain = S within 0.25 dB.
                const double errA = std::abs(tst::meanOf(g, steadyBegin, steadyEnd) - static_cast<double>(S));
                worstA = std::max(worstA, errA);
                CHECK(errA <= 0.25);

                // (b) S = 0: the onset extreme = A within 0.5 dB.
                if (S == 0.0f && A != 0.0f) {
                    const double ext = (A > 0.0f) ? maxOf(g, 0, onsetEnd) : minOf(g, 0, onsetEnd);
                    const double errB = std::abs(ext - static_cast<double>(A));
                    worstB = std::max(worstB, errB);
                    CHECK(errB <= 0.5);
                }

                // (f) gain law identity at every sample of [onset, onset + 1 s].
                const bool lawCell = (A == -12.0f || A == 6.0f || A == 12.0f) && (S == -6.0f || S == 0.0f || S == 6.0f);
                if (lawCell) {
                    double errF = 0.0;
                    std::size_t at = 0;
                    for (std::size_t i = 0; i < lawEnd; ++i) {
                        const double c = lawC(taps.fast[i], taps.slow[i]);
                        const double law = static_cast<double>(taps.aS[i]) * c + static_cast<double>(taps.sS[i]) * (1.0 - c);
                        const double e = std::abs(static_cast<double>(taps.gainDb[i]) - law);
                        if (e > errF) {
                            errF = e;
                            at = i;
                        }
                    }
                    worstF = std::max(worstF, errF);
                    INFO("worst law residual at sample " << at);
                    CHECK(errF <= 0.1);
                }
            }
        }
        std::printf("TransientShaper_GainsMatchParameters %s %s: (a) worst |steady - S| %.4f dB (bar 0.25); "
                    "(b) worst |onset extreme - A| %.4f dB (bar 0.5); (f) worst law residual %.5f dB (bar 0.1)\n",
                    toneName(cell.tone), cell.note, worstA, worstB, worstF);

        // (e) 2 s silence pre-roll after reset(): onset peak = A within 0.5 dB; no sample above A + 0.5 dB.
        const std::vector<float> xp = tst::burst(cell.tone, cell.f0, kFs, 1.0, 2.0, 0.25);
        const std::size_t onset = tst::onsetIndex(kFs, 2.0);
        for (const float A : {6.0f, 12.0f}) {
            INFO(toneName(cell.tone) << " " << cell.note << " pre-roll A " << A);
            TransientShaper sh;
            tsConfigure(sh, A, 0.0f, 40.0f);
            sh.reset();
            std::vector<float> y = xp;
            TsTaps taps;
            tsRender(sh, y.data(), nullptr, y.size(), kHostBlock, &taps, 0);
            const std::span<const float> g(taps.gainDb);
            const double peak = maxOf(g, onset, onset + onsetEnd);
            const double maxAll = maxOf(g, 0, g.size());
            std::printf("TransientShaper_GainsMatchParameters %s %s (e) A %+.0f: onset peak %.4f dB, render max %.4f dB\n",
                        toneName(cell.tone), cell.note, static_cast<double>(A), peak, maxAll);
            CHECK(std::abs(peak - static_cast<double>(A)) <= 0.5);
            CHECK(maxAll <= static_cast<double>(A) + 0.5);
        }
    }
}

// =============================================================================
// SC-008 (c): durations 20 / 40 / 100 / 250 ms (5 ms is its own case, deviation 11)
// =============================================================================

TEST_CASE("TransientShaper_DurationMatches", "[processors][profundum][transient-shaper]") {
    const std::array<NoteCell, 4> cells{{{.tone = Tone::Sine, .f0 = tst::kE1Hz, .note = "E1"},
                                         {.tone = Tone::Saw, .f0 = tst::kE1Hz, .note = "E1"},
                                         {.tone = Tone::Sine, .f0 = tst::kC3Hz, .note = "C3"},
                                         {.tone = Tone::Saw, .f0 = tst::kC3Hz, .note = "C3"}}};
    for (const NoteCell& cell : cells) {
        const std::vector<float> x = tst::burst(cell.tone, cell.f0, kFs, 1.0, 0.0, 0.25);
        for (const float d : {20.0f, 40.0f, 100.0f, 250.0f}) {
            const double tt = cellTransientTimeMs(x, d);
            const double err = tt / static_cast<double>(d) - 1.0;
            std::printf("TransientShaper_DurationMatches %s %s d %.0f ms: transient time %.3f ms, error %+.2f %% (bar 15 %%)\n",
                        toneName(cell.tone), cell.note, static_cast<double>(d), tt, 100.0 * err);
            INFO(toneName(cell.tone) << " " << cell.note << " d " << d << " ms: transient " << tt << " ms");
            CHECK(std::abs(err) <= 0.15);
        }
    }
}

// =============================================================================
// SC-008 (c) at 5 ms (plan S1 C-2; tasks.md deviation 11)
// =============================================================================

TEST_CASE("TransientShaper_DurationShortestCell", "[processors][profundum][transient-shaper]") {
    // RULED 2026-10-17, plan S1 C-2 (a): the 5 ms point is gated at C3 and recorded at E1
    // (spec SC-008(c)). Index 0 = E1, 1 = C3. The E1-sine cell is recorded (model +30.0 %, plan S12.2).
    constexpr std::array<bool, 2> kGate5msAt{false, true};
    const std::array<double, 2> notes{tst::kE1Hz, tst::kC3Hz};
    const std::array<const char*, 2> names{"E1", "C3"};
    for (std::size_t k = 0; k < notes.size(); ++k) {
        for (const Tone tone : {Tone::Sine, Tone::Saw}) {
            const std::vector<float> x = tst::burst(tone, notes[k], kFs, 1.0, 0.0, 0.25);
            const double tt = cellTransientTimeMs(x, 5.0f);
            const double err = tt / 5.0 - 1.0;
            std::printf("TransientShaper_DurationShortestCell %s %s d 5 ms: transient time %.3f ms, error %+.2f %% "
                        "(bar 15 %%, %s)\n",
                        toneName(tone), names[k], tt, 100.0 * err, kGate5msAt[k] ? "gated" : "recorded");
            if (kGate5msAt[k]) {
                INFO(toneName(tone) << " " << names[k] << " 5 ms: transient " << tt << " ms");
                CHECK(std::abs(err) <= 0.15);
            }
        }
    }
}

// =============================================================================
// SC-008 (d)
// =============================================================================

namespace {

struct ShaperSetting {
    float attackDb;
    float sustainDb;
    float durationMs;
};

/// A 3 s kSawAnchor note, start setting until `stepAt`, the end setting from `stepAt` on.
std::vector<float> renderStep(const std::vector<float>& x, const ShaperSetting& start, const ShaperSetting& end,
                              std::size_t stepAt, bool smootherBypass) {
    TransientShaper sh;
    tsConfigure(sh, start.attackDb, start.sustainDb, start.durationMs);
    Probe::setGainSmootherBypass(sh, smootherBypass);
    std::vector<float> y = x;
    tsRender(sh, y.data(), nullptr, stepAt, kHostBlock);
    sh.setAttackGainDb(end.attackDb);
    sh.setSustainGainDb(end.sustainDb);
    sh.setTransientDurationMs(end.durationMs);
    tsRender(sh, y.data() + stepAt, nullptr, y.size() - stepAt, kHostBlock);
    return y;
}

}  // namespace

TEST_CASE("TransientShaper_StepChangesClickFree", "[processors][profundum][transient-shaper]") {
    struct StepCase {
        const char* name;
        ShaperSetting start;
        ShaperSetting end;
    };
    const std::array<StepCase, 4> steps{{
        {.name = "attack 6 -> 12",
         .start = {.attackDb = 6.0f, .sustainDb = 0.0f, .durationMs = 40.0f},
         .end = {.attackDb = 12.0f, .sustainDb = 0.0f, .durationMs = 40.0f}},
        {.name = "sustain 0 -> +6",
         .start = {.attackDb = 6.0f, .sustainDb = 0.0f, .durationMs = 40.0f},
         .end = {.attackDb = 6.0f, .sustainDb = 6.0f, .durationMs = 40.0f}},
        {.name = "sustain 0 -> -6",
         .start = {.attackDb = 6.0f, .sustainDb = 0.0f, .durationMs = 40.0f},
         .end = {.attackDb = 6.0f, .sustainDb = -6.0f, .durationMs = 40.0f}},
        {.name = "duration 40 -> 250",
         .start = {.attackDb = 6.0f, .sustainDb = 0.0f, .durationMs = 40.0f},
         .end = {.attackDb = 6.0f, .sustainDb = 0.0f, .durationMs = 250.0f}},
    }};
    const std::size_t stepAt = tst::secondsToSamples(kFs, 1.5);

    for (const double f0 : {tst::kE1Hz, tst::kC3Hz}) {
        const std::vector<float> x = tst::burst(Tone::Saw, f0, kFs, 3.0, 0.0, 0.0);
        const std::size_t n = x.size();
        for (const StepCase& sc : steps) {
            const std::vector<float> tr = renderStep(x, sc.start, sc.end, stepAt, false);
            const std::vector<float> refStart = renderStep(x, sc.start, sc.start, stepAt, false);
            const std::vector<float> refEnd = renderStep(x, sc.end, sc.end, stepAt, false);
            const sct::ClickCount cc = sct::differentialClicks(tr.data(), refStart.data(), refEnd.data(), n, kFs, f0);
            std::printf("TransientShaper_StepChangesClickFree %.3f Hz %s: counted %zu (transition %zu, reference %zu, "
                        "guarded %zu, N %zu)\n",
                        f0, sc.name, cc.counted, cc.transitionDetections, cc.referenceDetections, cc.guardedOut,
                        cc.windowN);
            INFO(f0 << " Hz " << sc.name);
            CHECK(cc.counted == 0u);
        }

        // Positive control: the unsmoothed sustain 0 -> +12 dB step must be flagged.
        const ShaperSetting s0{.attackDb = 6.0f, .sustainDb = 0.0f, .durationMs = 40.0f};
        const ShaperSetting s1{.attackDb = 6.0f, .sustainDb = 12.0f, .durationMs = 40.0f};
        const std::vector<float> tr = renderStep(x, s0, s1, stepAt, true);
        const std::vector<float> refStart = renderStep(x, s0, s0, stepAt, false);
        const std::vector<float> refEnd = renderStep(x, s1, s1, stepAt, false);
        const sct::ClickCount cc = sct::differentialClicks(tr.data(), refStart.data(), refEnd.data(), n, kFs, f0);
        std::printf("TransientShaper_StepChangesClickFree %.3f Hz positive control (smoother bypass, sustain 0 -> +12): "
                    "counted %zu (guarded %zu)\n",
                    f0, cc.counted, cc.guardedOut);
        INFO(f0 << " Hz positive control");
        CHECK(cc.counted >= 1u);
    }
}

// =============================================================================
// SC-009, FR-004
// =============================================================================

TEST_CASE("TransientShaper_NoWaveformRateRipple", "[processors][profundum][transient-shaper]") {
    const std::size_t from = tst::secondsToSamples(kFs, 3.0);
    const std::size_t to = tst::secondsToSamples(kFs, 4.0);
    const std::array<double, 3> notes{tst::kE0Hz, tst::kE1Hz, tst::kE2Hz};
    const std::array<const char*, 3> names{"E0", "E1", "E2"};

    for (std::size_t k = 0; k < notes.size(); ++k) {
        for (const Tone tone : {Tone::Sine, Tone::Saw}) {
            const std::vector<float> x = tst::steadyTone(tone, notes[k], kFs, 4.0);
            REQUIRE(x.size() >= to);
            for (const float d : {40.0f, 5.0f}) {
                double worst = 0.0;
                float worstA = 0.0f;
                float worstS = 0.0f;
                for (const float A : kGrid) {
                    for (const float S : kGrid) {
                        TransientShaper sh;
                        tsConfigure(sh, A, S, d);
                        std::vector<float> y = x;
                        TsTaps taps;
                        tsRender(sh, y.data(), nullptr, to, kHostBlock, &taps, from);
                        const double ripple = tst::rippleDb(std::span<const float>(taps.gainDb), from, to);
                        if (ripple > worst) {
                            worst = ripple;
                            worstA = A;
                            worstS = S;
                        }
                        INFO(names[k] << " " << toneName(tone) << " d " << d << " A " << A << " S " << S);
                        CHECK(ripple <= 0.2);
                    }
                }
                std::printf("TransientShaper_NoWaveformRateRipple %s %s d %.0f ms: worst ripple %.5f dB at A %+.0f "
                            "S %+.0f (bar 0.2)\n",
                            names[k], toneName(tone), static_cast<double>(d), worst, static_cast<double>(worstA),
                            static_cast<double>(worstS));
            }
        }
    }

    // Negative control: the EFs fed max(|L|, |R|) directly (no hold) ride the E0 waveform.
    const std::vector<float> x = tst::steadyTone(Tone::Sine, tst::kE0Hz, kFs, 4.0);
    TransientShaper sh;
    tsConfigure(sh, 12.0f, 0.0f, 5.0f);
    Probe::setHoldBypass(sh, true);
    std::vector<float> y = x;
    TsTaps taps;
    tsRender(sh, y.data(), nullptr, to, kHostBlock, &taps, from);
    const double ripple = tst::rippleDb(std::span<const float>(taps.gainDb), from, to);
    std::printf("TransientShaper_NoWaveformRateRipple negative control (hold bypass, E0 sine, +12 / 0, 5 ms): "
                "ripple %.4f dB (must exceed 0.2)\n",
                ripple);
    CHECK(ripple > 0.2);
}

// =============================================================================
// SC-011 shaper arm, FR-043
// =============================================================================

TEST_CASE("TransientShaper_Audibility", "[processors][profundum][transient-shaper]") {
    const std::vector<float> x = tst::burst(Tone::Saw, tst::kE1Hz, kFs, 2.0, 0.0, 0.25);
    const std::size_t n = x.size();
    const std::size_t hold = tst::secondsToSamples(kFs, 2.0);
    const std::size_t W = tst::dTrWindow(kFs, 250.0);
    REQUIRE(W == tst::secondsToSamples(kFs, 0.375));
    const std::size_t steadyStart = hold - W;

    auto render = [&](float A, float S, float d) {
        TransientShaper sh;
        tsConfigure(sh, A, S, d);
        std::vector<float> y = x;
        tsRender(sh, y.data(), nullptr, n, kHostBlock);
        return y;
    };
    auto dTr = [&](const std::vector<float>& y) { return tst::dTrDb(y.data(), n, 0, steadyStart, W); };
    auto level = [&](const std::vector<float>& y) {
        return tst::levelDbPeriods(y.data() + steadyStart, W, kFs, tst::kE1Hz);
    };

    const double dTrUp = dTr(render(12.0f, 0.0f, 40.0f));
    const double dTrDown = dTr(render(-12.0f, 0.0f, 40.0f));
    const double lUp = level(render(0.0f, 12.0f, 40.0f));
    const double lDown = level(render(0.0f, -12.0f, 40.0f));
    const double dTrLong = dTr(render(6.0f, 0.0f, 250.0f));
    const double dTrShort = dTr(render(6.0f, 0.0f, 5.0f));
    const double dTrInert = dTr(render(0.0f, 0.0f, 40.0f));
    std::printf("TransientShaper_Audibility E1 saw: D_tr(A +12) %.3f - D_tr(A -12) %.3f = %.3f dB (bar 3); "
                "L(S +12) %.3f - L(S -12) %.3f = %.3f dB (bar 3); D_tr(250 ms) %.3f - D_tr(5 ms) %.3f = %.3f dB "
                "(bar 3); D_tr(0 / 0, inert, recorded) %.3f dB\n",
                dTrUp, dTrDown, dTrUp - dTrDown, lUp, lDown, lUp - lDown, dTrLong, dTrShort, dTrLong - dTrShort,
                dTrInert);
    CHECK(dTrUp - dTrDown >= 3.0);
    CHECK(lUp - lDown >= 3.0);
    CHECK(dTrLong - dTrShort >= 3.0);
}

// =============================================================================
// SC-012 shaper arm, FR-005, FR-041
// =============================================================================

TEST_CASE("TransientShaper_MonoSum", "[processors][profundum][transient-shaper]") {
    const std::vector<float> x = tst::burst(Tone::Saw, tst::kE1Hz, kFs, 1.0, 0.0, 0.25);
    const std::size_t n = x.size();

    // (a) L == R input: the outputs are bit-identical at every grid point, durations 5 and 40 ms.
    std::size_t mismatches = 0;
    for (const float d : {5.0f, 40.0f}) {
        for (const float A : kGrid) {
            for (const float S : kGrid) {
                TransientShaper sh;
                tsConfigure(sh, A, S, d);
                std::vector<float> L = x;
                std::vector<float> R = x;
                tsRender(sh, L.data(), R.data(), n, kHostBlock);
                if (!bytesEqual(L.data(), R.data(), n)) {
                    ++mismatches;
                    UNSCOPED_INFO("L != R at d " << d << " A " << A << " S " << S);
                }
            }
        }
    }
    std::printf("TransientShaper_MonoSum (a): %zu of 50 grid points with L != R\n", mismatches);
    CHECK(mismatches == 0u);

    // (b) L = x, R = 0.5 x at A = +12, S = -6: one gain for both channels.
    std::vector<float> inR(n);
    for (std::size_t i = 0; i < n; ++i)
        inR[i] = 0.5f * x[i];
    std::vector<float> L = x;
    std::vector<float> R = inR;
    TransientShaper sh;
    tsConfigure(sh, 12.0f, -6.0f, TransientShaper::kDefaultDurationMs);
    tsRender(sh, L.data(), R.data(), n, kHostBlock);
    const double eps = static_cast<double>(std::numeric_limits<float>::epsilon());
    std::size_t violations = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double lhs = static_cast<double>(L[i]) * static_cast<double>(inR[i]);
        const double rhs = static_cast<double>(R[i]) * static_cast<double>(x[i]);
        if (std::abs(lhs - rhs) > 4.0 * eps * std::abs(lhs))
            ++violations;
    }
    const double smIn = tst::sideMidRatioDb(x.data(), inR.data(), n);
    const double smOut = tst::sideMidRatioDb(L.data(), R.data(), n);
    std::printf("TransientShaper_MonoSum (b): cross-product violations %zu; side/mid in %.5f dB, out %.5f dB, "
                "diff %.6f dB (bar 0.01)\n",
                violations, smIn, smOut, smOut - smIn);
    CHECK(violations == 0u);
    CHECK(std::abs(smOut - smIn) <= 0.01);
}

// =============================================================================
// SC-016 shaper arm
// =============================================================================

TEST_CASE("TransientShaper_NoAllocation", "[processors][profundum][transient-shaper]") {
    const std::vector<float> x = tst::steadyTone(Tone::Saw, tst::kE1Hz, kFs, 0.25);
    const std::size_t n = x.size();
    std::vector<float> L = x;
    std::vector<float> R = x;
    const float nanValue = floatFromBits(0x7FC00000u);
    const float posInf = floatFromBits(0x7F800000u);
    const float negInf = floatFromBits(0xFF800000u);

    TransientShaper sh;
    tsConfigure(sh, 12.0f, -6.0f, 40.0f);
    std::size_t observed = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        sh.processBlock(L.data(), R.data(), n);
        sh.processBlock(L.data(), L.data(), n);
        for (const float v : {6.0f, -3.0f, 100.0f, nanValue, posInf, negInf}) {
            sh.setAttackGainDb(v);
            sh.setSustainGainDb(v);
            sh.setTransientDurationMs(v);
        }
        sh.reset();
        sh.processBlock(L.data(), R.data(), n);
        // AllocationScope latches in its destructor, so the live figure is read while it is open.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    std::printf("TransientShaper_NoAllocation: allocations %zu\n", observed);
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;  // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}

// =============================================================================
// SC-018 shaper arm, FR-049 (plan C-11: the mid-render change at absolute sample 8704)
// =============================================================================

namespace {

constexpr std::size_t kGridEventSample = 8704;  // lcm(17, 32, 64, 512)

/// Stereo render of (inL, inR) on a prepared shaper in host blocks of `block`; at absolute sample
/// 8704 setTransientDurationMs(100) and setAttackGainDb(6) are issued.
void renderPartition(TransientShaper& sh, const std::vector<float>& inL, const std::vector<float>& inR, std::size_t block,
                     std::vector<float>& L, std::vector<float>& R) {
    L = inL;
    R = inR;
    const std::size_t n = L.size();
    bool applied = false;
    std::size_t pos = 0;
    while (pos < n) {
        if (pos == kGridEventSample) {
            sh.setTransientDurationMs(100.0f);
            sh.setAttackGainDb(6.0f);
            applied = true;
        }
        std::size_t len = std::min(block, n - pos);
        if (pos < kGridEventSample)
            len = std::min(len, kGridEventSample - pos);
        sh.processBlock(L.data() + pos, R.data() + pos, len);
        pos += len;
    }
    REQUIRE(applied);
}

}  // namespace

TEST_CASE("TransientShaper_PartitionInvariant", "[processors][profundum][transient-shaper]") {
    const std::vector<float> inL = tst::steadyTone(Tone::Saw, tst::kE1Hz, kFs, 2.0);
    const std::vector<float> inR = tst::steadyTone(Tone::Sine, tst::kE1Hz, kFs, 2.0);
    REQUIRE(inL.size() == inR.size());
    const std::size_t n = inL.size();

    std::vector<float> refL;
    std::vector<float> refR;
    {
        TransientShaper sh;
        tsConfigure(sh, 12.0f, -6.0f, 40.0f);
        renderPartition(sh, inL, inR, 1, refL, refR);
    }
    for (const std::size_t block : {std::size_t{17}, std::size_t{32}, std::size_t{64}, std::size_t{512}}) {
        TransientShaper sh;
        tsConfigure(sh, 12.0f, -6.0f, 40.0f);
        std::vector<float> L;
        std::vector<float> R;
        renderPartition(sh, inL, inR, block, L, R);
        const bool eq = bytesEqual(L.data(), refL.data(), n) && bytesEqual(R.data(), refR.data(), n);
        std::printf("TransientShaper_PartitionInvariant: block %zu vs block 1: %s\n", block,
                    eq ? "bit-identical" : "DIFFERS");
        INFO("block " << block);
        CHECK(eq);
    }

    // Two renders after reset() are bit-identical.
    TransientShaper sh;
    tsConfigure(sh, 12.0f, -6.0f, 40.0f);
    std::vector<float> L1;
    std::vector<float> R1;
    renderPartition(sh, inL, inR, 512, L1, R1);
    sh.setAttackGainDb(12.0f);
    sh.setTransientDurationMs(40.0f);
    sh.reset();
    std::vector<float> L2;
    std::vector<float> R2;
    renderPartition(sh, inL, inR, 512, L2, R2);
    const bool eqReset = bytesEqual(L1.data(), L2.data(), n) && bytesEqual(R1.data(), R2.data(), n);
    std::printf("TransientShaper_PartitionInvariant: two renders from reset(): %s\n",
                eqReset ? "bit-identical" : "DIFFERS");
    CHECK(eqReset);
}

// =============================================================================
// SC-021 shaper arm
// =============================================================================

TEST_CASE("TransientShaper_SampleRates", "[processors][profundum][transient-shaper]") {
    const std::array<double, 5> rates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    std::array<double, 5> steady{};
    std::array<double, 5> onsetPeak{};
    const double bound = static_cast<double>(dbToGain(12.0f)) * (1.0 + 1e-6);

    for (std::size_t r = 0; r < rates.size(); ++r) {
        const double fs = rates[r];
        const std::vector<float> x = tst::burst(Tone::Saw, tst::kE1Hz, fs, 1.0, 0.0, 0.25);
        const std::size_t n = x.size();

        // SC-008(a) steady gain at (+12, -6), plus finiteness and the bound on every sample.
        {
            TransientShaper sh;
            tsConfigure(sh, 12.0f, -6.0f, 40.0f, fs);
            std::vector<float> y = x;
            TsTaps taps;
            tsRender(sh, y.data(), nullptr, n, kHostBlock, &taps, 0);
            steady[r] = tst::meanOf(std::span<const float>(taps.gainDb), tst::secondsToSamples(fs, 0.85),
                                    tst::secondsToSamples(fs, 1.00));
            std::size_t nonFinite = 0;
            std::size_t overBound = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (!tst::isFiniteBits(y[i])) {
                    ++nonFinite;
                    continue;
                }
                if (std::abs(static_cast<double>(y[i])) > std::abs(static_cast<double>(x[i])) * bound)
                    ++overBound;
            }
            INFO("fs " << fs);
            CHECK(nonFinite == 0u);
            CHECK(overBound == 0u);
        }
        // SC-008(b) onset peak at A = +12, S = 0.
        {
            TransientShaper sh;
            tsConfigure(sh, 12.0f, 0.0f, 40.0f, fs);
            std::vector<float> y = x;
            TsTaps taps;
            tsRender(sh, y.data(), nullptr, n, kHostBlock, &taps, 0);
            onsetPeak[r] = maxOf(std::span<const float>(taps.gainDb), 0, tst::secondsToSamples(fs, 0.050) + 1);
        }
    }
    constexpr std::size_t k48 = 1;
    for (std::size_t r = 0; r < rates.size(); ++r) {
        std::printf("TransientShaper_SampleRates fs %.0f: steady gain (+12 / -6) %.4f dB (48k %.4f), onset peak (+12 / 0) "
                    "%.4f dB (48k %.4f)\n",
                    rates[r], steady[r], steady[k48], onsetPeak[r], onsetPeak[k48]);
        INFO("fs " << rates[r]);
        CHECK(std::abs(steady[r] - steady[k48]) <= 0.25);
        CHECK(std::abs(onsetPeak[r] - onsetPeak[k48]) <= 0.25);
    }

    // prepare(48k) -> prepare(96k) (with a render in between) -> prepare(48k): equal to a fresh 48 kHz render.
    const std::vector<float> x48 = tst::burst(Tone::Saw, tst::kE1Hz, 48000.0, 1.0, 0.0, 0.25);
    const std::vector<float> x96 = tst::steadyTone(Tone::Saw, tst::kE1Hz, 96000.0, 0.5);
    TransientShaper restarted;
    tsConfigure(restarted, 12.0f, -6.0f, 40.0f, 48000.0);
    restarted.prepare(96000.0, kHostBlock);
    std::vector<float> dirty = x96;
    tsRender(restarted, dirty.data(), nullptr, dirty.size(), kHostBlock);
    restarted.prepare(48000.0, kHostBlock);
    std::vector<float> yRestart = x48;
    tsRender(restarted, yRestart.data(), nullptr, yRestart.size(), kHostBlock);
    TransientShaper fresh;
    tsConfigure(fresh, 12.0f, -6.0f, 40.0f, 48000.0);
    std::vector<float> yFresh = x48;
    tsRender(fresh, yFresh.data(), nullptr, yFresh.size(), kHostBlock);
    const bool eq = bytesEqual(yRestart.data(), yFresh.data(), yFresh.size());
    std::printf("TransientShaper_SampleRates: 48k -> 96k -> 48k restart vs fresh 48k: %s\n",
                eq ? "bit-identical" : "DIFFERS");
    CHECK(eq);
}

// =============================================================================
// FR-047
// =============================================================================

TEST_CASE("TransientShaper_Lifecycle", "[processors][profundum][transient-shaper]") {
    const std::vector<float> x = tst::burst(Tone::Saw, tst::kE1Hz, kFs, 1.0, 0.0, 0.25);
    const std::size_t n = x.size();

    // (1) Before prepare: the sentinel buffers are untouched and the gain tap reads 0.
    {
        std::vector<float> sentinel(1024);
        for (std::size_t i = 0; i < sentinel.size(); ++i)
            sentinel[i] = 0.125f + 0.001f * static_cast<float>(i);
        std::vector<float> L = sentinel;
        std::vector<float> R = sentinel;
        TransientShaper sh;
        sh.processBlock(L.data(), R.data(), L.size());
        sh.processBlock(L.data(), L.data(), L.size());
        CHECK(bytesEqual(L.data(), sentinel.data(), sentinel.size()));
        CHECK(bytesEqual(R.data(), sentinel.data(), sentinel.size()));
        CHECK(sh.currentGainDb() == 0.0f);
    }

    // (2) After prepare: a null channel and numSamples == 0 are no-ops; nothing advanced.
    {
        TransientShaper a;
        TransientShaper b;
        tsConfigure(a, 12.0f, 0.0f, 40.0f);
        tsConfigure(b, 12.0f, 0.0f, 40.0f);
        std::vector<float> L = x;
        std::vector<float> R = x;
        a.processBlock(nullptr, R.data(), n);
        a.processBlock(L.data(), nullptr, n);
        a.processBlock(L.data(), R.data(), 0);
        CHECK(bytesEqual(L.data(), x.data(), n));
        CHECK(bytesEqual(R.data(), x.data(), n));
        std::vector<float> ya = x;
        std::vector<float> yb = x;
        tsRender(a, ya.data(), nullptr, n, kHostBlock);
        tsRender(b, yb.data(), nullptr, n, kHostBlock);
        CHECK(bytesEqual(ya.data(), yb.data(), n));
    }

    // (3) reset() after 2 s of shaped signal, then a render == the same render on a fresh instance.
    {
        const std::vector<float> warm = tst::steadyTone(Tone::Saw, tst::kE1Hz, kFs, 2.0);
        TransientShaper used;
        tsConfigure(used, 12.0f, 0.0f, 40.0f);
        std::vector<float> w = warm;
        tsRender(used, w.data(), nullptr, w.size(), kHostBlock);
        used.reset();
        std::vector<float> yUsed = x;
        tsRender(used, yUsed.data(), nullptr, n, kHostBlock);
        TransientShaper fresh;
        tsConfigure(fresh, 12.0f, 0.0f, 40.0f);
        std::vector<float> yFresh = x;
        tsRender(fresh, yFresh.data(), nullptr, n, kHostBlock);
        const bool eq = bytesEqual(yUsed.data(), yFresh.data(), n);
        std::printf("TransientShaper_Lifecycle (3): reset() after 2 s vs fresh: %s\n", eq ? "bit-identical" : "DIFFERS");
        CHECK(eq);
    }
}

// =============================================================================
// Plan S8.1 duration-table harvest (hidden; T019 main loop only)
// =============================================================================

TEST_CASE("TransientShaper_DurationTableHarvest", "[transient-shaper-harvest][.calibration]") {
    const std::array<NoteCell, 4> cells{{{.tone = Tone::Sine, .f0 = tst::kE1Hz, .note = "E1"},
                                         {.tone = Tone::Saw, .f0 = tst::kE1Hz, .note = "E1"},
                                         {.tone = Tone::Sine, .f0 = tst::kC3Hz, .note = "C3"},
                                         {.tone = Tone::Saw, .f0 = tst::kC3Hz, .note = "C3"}}};
    std::array<std::vector<float>, 4> inputs;
    for (std::size_t c = 0; c < cells.size(); ++c)
        inputs[c] = tst::burst(cells[c].tone, cells[c].f0, kFs, 1.0, 0.0, 0.25);

    constexpr std::array<float, 11> durations{5.0f, 7.5f, 10.0f, 15.0f, 20.0f, 30.0f, 40.0f, 60.0f, 100.0f, 150.0f, 250.0f};
    static_assert(durations.size() == TransientShaper::kDurationTable.size());

    auto meanLogError = [&](float d, float arg, std::array<double, 4>* errs) {
        double sum = 0.0;
        for (std::size_t c = 0; c < cells.size(); ++c) {
            const double tt = cellTransientTimeMs(inputs[c], d, kFs, arg);
            const double ratio = tt / static_cast<double>(d);
            if (errs != nullptr)
                (*errs)[c] = ratio - 1.0;
            sum += std::log(ratio);
        }
        return sum / static_cast<double>(cells.size());
    };

    std::array<float, 11> args{};
    std::array<std::array<double, 4>, 11> cellErr{};
    for (std::size_t k = 0; k < durations.size(); ++k) {
        const float d = durations[k];
        const double a0 = HarmonicFoldTest::kTwoPi * static_cast<double>(d) / (0.438 * 8.0);
        double lo = 0.2 * a0;
        double hi = 5.0 * a0;
        for (int step = 0; step < 40; ++step) {
            const double mid = std::sqrt(lo * hi);
            if (meanLogError(d, static_cast<float>(mid), nullptr) > 0.0)
                hi = mid;
            else
                lo = mid;
        }
        args[k] = static_cast<float>(std::sqrt(lo * hi));
        const double residual = meanLogError(d, args[k], &cellErr[k]);
        std::printf("TransientShaper_DurationTableHarvest d %.1f ms: argument %.6f ms (seed %.3f), mean log error %+.6f\n",
                    static_cast<double>(d), static_cast<double>(args[k]),
                    static_cast<double>(TransientShaper::kDurationTable[k].slowAttackArgMs), residual);
    }

    const std::string path = evidenceDir() + "duration-table.txt";
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << "BEGIN-INITIALIZER duration-table\n";
    for (std::size_t k = 0; k < durations.size(); ++k) {
        out << "        {.durationMs = " << floatLiteral(durations[k], 9)
            << ", .slowAttackArgMs = " << floatLiteral(args[k], 9) << "},\n";
    }
    out << "END-INITIALIZER duration-table\n";
    out << "# TransientShaper_DurationTableHarvest (plan S8.1): 48 kHz, A = +12 dB, S = 0 dB; geometric bisection (40 "
           "steps) over [0.2 a0, 5 a0], a0 = 2 pi d / (0.438 * 8); mean ln(transient / d) over the four cells = 0.\n";
    out << "# Per-cell transient-time error (%): sine E1, saw E1, sine C3, saw C3. The E1-sine cells at 5 and 7.5 ms are "
           "recorded (plan C-2; extended to 7.5 ms by user ruling 2026-10-18, G9-2); any other cell beyond +/-15 % is "
           "surfaced to the user as a stop line (FR-008), never a retune.\n";
    for (std::size_t k = 0; k < durations.size(); ++k) {
        out << "d " << std::fixed << std::setprecision(1) << static_cast<double>(durations[k]) << " ms:";
        for (std::size_t c = 0; c < cells.size(); ++c)
            out << " " << std::showpos << std::setprecision(2) << 100.0 * cellErr[k][c] << std::noshowpos;
        out << std::defaultfloat << "\n";
        for (std::size_t c = 0; c < cells.size(); ++c) {
            if (std::abs(cellErr[k][c]) > 0.15) {
                // C-2 (5 ms) and G9-2 (7.5 ms): the E1 sine's first peak lands 6.07 ms after onset.
                const bool ruled = cells[c].tone == Tone::Sine && cells[c].f0 == tst::kE1Hz && durations[k] < 10.0f;
                out << (ruled ? "  recorded (C-2 / G9-2, FR-048 measurement): d " : "STOP AND SURFACE: FR-008 duration cell beyond 15 %: d ")
                    << std::fixed << std::setprecision(1) << static_cast<double>(durations[k]) << " ms "
                    << toneName(cells[c].tone) << " " << cells[c].note << " error " << std::showpos
                    << std::setprecision(2) << 100.0 * cellErr[k][c] << std::noshowpos << " %" << std::defaultfloat << "\n";
            }
        }
    }
    REQUIRE(out.good());
    std::printf("TransientShaper_DurationTableHarvest: wrote %s\n", path.c_str());
}
