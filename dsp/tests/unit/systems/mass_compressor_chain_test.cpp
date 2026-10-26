// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// Compressor on the chain: SC-006, SC-011 compressor, SC-022, SC-024(d), amount harvest
// FR-011, FR-016, FR-018, FR-019; plan S8.3, R-4
// ==============================================================================
// tasks.md T013. Tags [systems][profundum][mass-dynamics]; the amount-table harvest is hidden
// ([mass-dynamics-harvest][.calibration]) and run only by the main loop in T019. Its tag is NOT
// [mass-dynamics]: a Catch2 tag filter that matches a hidden case runs it, so the gate filter
// "[mass-dynamics]~[long]" must not match the harvest (precedent: transient_shaper_test.cpp
// "[transient-shaper-harvest][.calibration]").
//
// Every render goes through the T011 validation chain (mass_dynamics_test_helpers.h, plan S9):
// the Mass reference voice is ChainConfig's default base (kDefaultControls, Anchor attached, Fold
// at base 0, shaper +6 / 0 dB), keyed at the note under test (FR-018).
//
// Expected red until T019 (seed thresholds and makeup, plan S8.3): MassCompressor_MakeupMatchesGr,
// MassCompressor_NoSubPumping, MassCompressor_RectifiedFeedNegativeControl.
//
// Shipping FP mode (not -fno-fast-math): nothing here builds or compares a non-finite value.
// No bit-exact float goldens. TU-local names carry an `mcc` / `kMcc` prefix (tasks.md rule 5).
// ==============================================================================

#include "mass_dynamics_probe.h"
#include "mass_dynamics_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;
namespace mdx = Krate::DSP::MassDynamicsTest;
namespace mtx = Krate::DSP::TransientShaperTest;
using MccProbe = Krate::DSP::detail::MassDynamicsProbe;
using MccTable = std::array<BassCompressor::AmountRow, 5>;

namespace {

constexpr double kMccFs = 48000.0;

struct MccNote {
    const char* name;
    double hz;
};

const std::array<MccNote, 4>& mccNotes() {
    static const std::array<MccNote, 4> notes{{{"E0", mtx::kE0Hz}, {"E1", mtx::kE1Hz}, {"E2", mtx::kE2Hz}, {"C3", mtx::kC3Hz}}};
    return notes;
}

constexpr std::size_t mccIdx(MassLaw::Target t) { return static_cast<std::size_t>(t); }

std::size_t mccAt(double seconds) { return mtx::secondsToSamples(kMccFs, seconds); }

/// A target override that holds `v` at every sample.
mdx::TargetOverride mccConst(float v) {
    return [v](std::size_t) -> std::optional<float> { return v; };
}

/// The S8.3 calibration voice (= the SC-006 / SC-022 voice): the Mass reference voice at Mass 50
/// (every target at its base) with the compression amount forced by targetOverride, keyed at f0,
/// per-sample gain taps on.
mdx::ChainConfig mccCalibrationConfig(double f0, float amount, double seconds) {
    mdx::ChainConfig c;
    c.f0 = f0;
    c.fs = kMccFs;
    c.seconds = seconds;
    c.massAt = [](std::size_t) { return MassLaw::kNeutralMass; };
    c.targetOverride[mccIdx(MassLaw::Target::CompressionAmount)] = mccConst(amount);
    c.perSampleDynamicsTaps = true;
    return c;
}

/// The Mass 100 voice: all six targets from the law at Mass 100 (plan S8.3 target 2).
mdx::ChainConfig mccMass100Config(double f0, double seconds) {
    mdx::ChainConfig c;
    c.f0 = f0;
    c.fs = kMccFs;
    c.seconds = seconds;
    c.massAt = [](std::size_t) { return MassLaw::kMaxMass; };
    c.perSampleDynamicsTaps = true;
    return c;
}

/// Steady GR in dB of reduction (positive): −mean(compGrDb) over [begin, end).
double mccSteadyGrDb(const std::vector<float>& grDb, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= grDb.size());
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(grDb[i]);
    return -sum / static_cast<double>(end - begin);
}

/// One SC-006 cell (plan S13 SC-006 row): Render A (3 s) and Render B (4 s, Anchor 0.25 -> 1 at 2 s).
struct MccPumpReading {
    double mSubDft = 0.0;   ///< (a) single-bin DFT reading over [1 s, 3 s] of Render A
    double mSubLp = 0.0;    ///< (a) 1.5·f0 LP RMS reading over [1 s, 3 s] of Render A
    double lpGdPeriods = 0.0;
    double grA = 0.0;       ///< (d) mean GR [1 s, 3 s] of Render A
    double grB1 = 0.0;      ///< mean GR [1.5 s, 2 s] of Render B (Anchor 0.25)
    double grB2 = 0.0;      ///< mean GR [3 s, 3.5 s] of Render B (Anchor 1.0)
    [[nodiscard]] double b() const { return std::abs(grB1 - grB2); }
};

MccPumpReading mccPumpCell(double f0, const std::function<void(MassDynamics&)>& configure) {
    MccPumpReading r;
    {
        mdx::ChainConfig a = mccCalibrationConfig(f0, 1.0f, 3.0);
        a.configureDynamics = configure;
        const mdx::ChainTaps t = mdx::renderChain(a);
        const mdx::SubBand sb = mdx::subBand(t.outL.data(), t.outL.size(), kMccFs, f0);
        r.mSubDft = mdx::mSubSamples(sb, sb.dftDb, mccAt(1.0), mccAt(3.0));
        r.mSubLp = mdx::mSubSamples(sb, sb.lpRmsDb, mccAt(1.0), mccAt(3.0));
        r.lpGdPeriods = sb.lpGroupDelayPeriods;
        r.grA = mccSteadyGrDb(t.compGrDb, mccAt(1.0), mccAt(3.0));
    }
    {
        mdx::ChainConfig b = mccCalibrationConfig(f0, 1.0f, 4.0);
        b.configureDynamics = configure;
        const std::size_t step = mccAt(2.0);
        b.targetOverride[mccIdx(MassLaw::Target::AnchorLevel)] = [step](std::size_t n) -> std::optional<float> {
            return n < step ? 0.25f : 1.0f;
        };
        const mdx::ChainTaps t = mdx::renderChain(b);
        r.grB1 = mccSteadyGrDb(t.compGrDb, mccAt(1.5), mccAt(2.0));
        r.grB2 = mccSteadyGrDb(t.compGrDb, mccAt(3.0), mccAt(3.5));
    }
    return r;
}

void mccPrintPump(const char* tag, const char* note, const MccPumpReading& r) {
    std::printf("%s %s: (a) M_sub dft %.6f dB, lp %.6f dB (LP group delay %.6f periods); (d) GR %.6f dB; "
                "(b) GR[1.5,2] %.6f dB, GR[3,3.5] %.6f dB, |diff| %.6f dB\n",
                tag, note, r.mSubDft, r.mSubLp, r.lpGdPeriods, r.grA, r.grB1, r.grB2, r.b());
}

std::string mccEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase6-mass-dynamics/evidence/";
}

/// A C++ float literal (always with a '.' or exponent, then 'f'), as transient_shaper_test.cpp.
std::string mccFloatLiteral(float v, int precision) {
    std::ostringstream os;
    os << std::setprecision(precision) << static_cast<double>(v);
    std::string s = os.str();
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
        s += ".0";
    return s + "f";
}

}  // namespace

// =============================================================================
// SC-006 (a)(b)(c)(d): no sub pumping through the D-6 detection form (FR-011 (b), FR-018, FR-019).
// plan S1 C-6 (HP -> true-peak -> one-period hold), C-10 (s(t) readings), R-4 (E0 fallback).
// =============================================================================
TEST_CASE("MassCompressor_NoSubPumping", "[systems][profundum][mass-dynamics]") {
    MccPumpReading e0{};
    for (const MccNote& note : mccNotes()) {
        const MccPumpReading r = mccPumpCell(note.hz, {});
        mccPrintPump("MassCompressor_NoSubPumping", note.name, r);
        INFO("note " << note.name);
        CHECK(r.mSubDft <= 0.5);   // (a)
        CHECK(r.mSubLp <= 0.5);    // (a)
        CHECK(r.grA >= 3.0);       // (d) engagement floor
        CHECK(r.b() <= 1.0);       // (b)
        if (note.hz == mtx::kE0Hz) {
            e0 = r;
            if (r.b() > 1.0) {
                std::printf("DEFECT: MassCompressor_NoSubPumping SC-006(b) at E0 with the detector HP: expected |dGR| <= 1 dB, "
                            "observed %.6f dB; the 12 dB/oct HP leaves the sub in the detector (plan R-4): apply the "
                            "BiquadCascade<2> fallback inside dsp/include/krate/dsp/systems/mass_dynamics.h (T020)\n",
                            r.b());
            }
        }
    }

    // (c) Positive control at E0: the same two renders with the detector high-pass bypassed.
    const MccPumpReading c = mccPumpCell(mtx::kE0Hz, [](MassDynamics& md) {
        MccProbe::setHpBypass(MccProbe::compressor(md), true);
    });
    mccPrintPump("MassCompressor_NoSubPumping (c) HP bypassed", "E0", c);
    const bool aExceeds = c.mSubDft > 0.5 || c.mSubLp > 0.5;
    const bool bExceeds = c.b() > 1.0;
    std::printf("MassCompressor_NoSubPumping (c) E0 positive control: (a) %s, (b) %s -> %s (with HP: |dGR| %.6f dB)\n",
                aExceeds ? "EXCEEDS" : "within", bExceeds ? "EXCEEDS" : "within",
                (aExceeds || bExceeds) ? "control has teeth" : "CONTROL PASSES (no teeth)", e0.b());
    CHECK((aExceeds || bExceeds));
}

// =============================================================================
// SC-006(e): the forbidden struck feed (unfiltered max(|L|, |R|) into the internal sidechain)
// must fail (b) at E2 or (d) at E0 / E1.
// =============================================================================
TEST_CASE("MassCompressor_RectifiedFeedNegativeControl", "[systems][profundum][mass-dynamics]") {
    bool teeth = false;
    for (const MccNote& note : mccNotes()) {
        const MccPumpReading r = mccPumpCell(note.hz, [](MassDynamics& md) {
            MccProbe::setRectifiedFeed(MccProbe::compressor(md), true);
        });
        mccPrintPump("MassCompressor_RectifiedFeedNegativeControl", note.name, r);
        const bool failsB = r.b() > 1.0;
        const bool failsD = r.grA < 3.0;
        const bool counts = (note.hz == mtx::kE2Hz && failsB) ||
                            ((note.hz == mtx::kE0Hz || note.hz == mtx::kE1Hz) && failsD);
        std::printf("MassCompressor_RectifiedFeedNegativeControl %s verdict: (b) %s, (d) %s%s\n", note.name,
                    failsB ? "FAILS" : "passes", failsD ? "FAILS" : "passes", counts ? " -> counted failure" : "");
        teeth = teeth || counts;
    }
    if (!teeth)
        FAIL("fixture lacks teeth: the rectified feed passes (b) at E2 and (d) at E0 and E1");
    CHECK(teeth);
}

// =============================================================================
// SC-022 (FR-016): every row's makeup equals the steady GR it causes on the calibration voice.
// =============================================================================
TEST_CASE("MassCompressor_MakeupMatchesGr", "[systems][profundum][mass-dynamics]") {
    constexpr std::array<float, 5> kAmounts{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const MccTable& table = BassCompressor::kAmountTable;
    for (std::size_t i = 0; i < table.size(); ++i)
        REQUIRE(table[i].amount == kAmounts[i]);

    const std::array<MccNote, 2> notes{{{"E1", mtx::kE1Hz}, {"C3", mtx::kC3Hz}}};
    for (std::size_t i = 0; i < table.size(); ++i) {
        const BassCompressor::AmountRow& row = table[i];
        for (const MccNote& note : notes) {
            const mdx::ChainTaps t = mdx::renderChain(mccCalibrationConfig(note.hz, row.amount, 3.0));
            const double gr = mccSteadyGrDb(t.compGrDb, mccAt(1.0), mccAt(3.0));
            const double residual = static_cast<double>(row.makeupDb) - gr;
            std::printf("MassCompressor_MakeupMatchesGr amount %.6f %s: steady GR %.6f dB, makeup %.6f dB, residual %+.6f dB\n",
                        static_cast<double>(row.amount), note.name, gr, static_cast<double>(row.makeupDb), residual);
            INFO("amount " << row.amount << " note " << note.name);
            CHECK(std::abs(residual) <= 0.5);
            if (i == 0) {
                CHECK(row.makeupDb == 0.0f);
                std::size_t nonZero = 0;
                for (const float g : t.compGrDb)
                    nonZero += (g != 0.0f) ? 1u : 0u;
                std::printf("MassCompressor_MakeupMatchesGr amount 0 %s: %zu samples with GR != 0.0f\n", note.name, nonZero);
                CHECK(nonZero == 0);
            }
        }
    }
}

// =============================================================================
// SC-011 compressor (FR-043): the rendered two-level step, not the internal GR, proves the lever.
// =============================================================================
TEST_CASE("MassCompressor_Audibility", "[systems][profundum][mass-dynamics]") {
    const std::size_t step = mccAt(2.0);
    auto render = [&](float amount) {
        mdx::ChainConfig c;
        c.base = SpectralShapeRecipe::kSawAnchor;
        c.f0 = mtx::kE1Hz;
        c.fs = kMccFs;
        c.seconds = 4.0;
        c.massAt = [](std::size_t) { return MassLaw::kNeutralMass; };
        c.preDynamicsGainDb = [step](std::size_t n) { return n < step ? -12.0f : 0.0f; };
        c.targetOverride[mccIdx(MassLaw::Target::CompressionAmount)] = mccConst(amount);
        c.perSampleDynamicsTaps = true;
        return mdx::renderChain(c);
    };
    const mdx::ChainTaps t0 = render(0.0f);
    const mdx::ChainTaps t1 = render(1.0f);
    const double dl0 = mdx::deltaLStepDb(std::span<const float>(t0.outL), kMccFs, mtx::kE1Hz, mccAt(1.0), mccAt(2.0),
                                         mccAt(3.0), mccAt(4.0));
    const double dl1 = mdx::deltaLStepDb(std::span<const float>(t1.outL), kMccFs, mtx::kE1Hz, mccAt(1.0), mccAt(2.0),
                                         mccAt(3.0), mccAt(4.0));
    const double grLow = mccSteadyGrDb(t1.compGrDb, mccAt(1.0), mccAt(2.0));
    const double grHigh = mccSteadyGrDb(t1.compGrDb, mccAt(3.0), mccAt(4.0));
    std::printf("MassCompressor_Audibility E1 kSawAnchor: dL_step(0) %.6f dB, dL_step(1) %.6f dB, difference %.6f dB "
                "(bar >= 3); amount-1 steady GR (recorded) low %.6f dB, high %.6f dB\n",
                dl0, dl1, dl0 - dl1, grLow, grHigh);
    CHECK(dl0 - dl1 >= 3.0);
}

// =============================================================================
// SC-024(d) (FR-018, FR-035): POLY {E1, C3} keys the detector at the LOWER note, in either order.
// =============================================================================
TEST_CASE("MassCompressor_KeytrackedCornerPoly", "[systems][profundum][mass-dynamics]") {
    const double expected = 2.0 * mtx::kE1Hz;
    const double maxRule = 2.0 * mtx::kC3Hz;
    // The minimum and maximum rules are > 1 % apart, so the 1 % window discriminates.
    CHECK(std::abs(expected - maxRule) / maxRule > 0.01);

    const std::array<std::array<double, 2>, 2> orders{{{mtx::kE1Hz, mtx::kC3Hz}, {mtx::kC3Hz, mtx::kE1Hz}}};
    for (const auto& order : orders) {
        mdx::ChainConfig common;
        common.fs = kMccFs;
        common.seconds = 0.5;
        const mdx::ChainTaps t = mdx::renderPoly(std::span<const double>(order), common);
        const std::size_t settle = mccAt(0.2);
        const std::size_t total = t.outL.size();
        REQUIRE(t.cornerHzPerInterval.size() == (total + mdx::kGrid - 1) / mdx::kGrid);
        double worst = 0.0;
        double maxRuleBest = 1e300;
        std::size_t checked = 0;
        for (std::size_t k = 0; k < t.cornerHzPerInterval.size(); ++k) {
            const std::size_t end = std::min(total, (k + 1) * mdx::kGrid);
            if (end < settle)
                continue;
            const double c = static_cast<double>(t.cornerHzPerInterval[k]);
            worst = std::max(worst, std::abs(c - expected) / expected);
            maxRuleBest = std::min(maxRuleBest, std::abs(c - maxRule) / maxRule);
            ++checked;
        }
        std::printf("MassCompressor_KeytrackedCornerPoly order {%.5f, %.5f}: %zu intervals from 200 ms, worst |corner/2E1 - 1| "
                    "%.6f (bar 0.01); closest |corner/2C3 - 1| %.6f (must exceed 0.01)\n",
                    order[0], order[1], checked, worst, maxRuleBest);
        REQUIRE(checked > 0);
        CHECK(worst <= 0.01);
        CHECK(maxRuleBest > 0.01);
    }
}

// =============================================================================
// Plan S8.3 amount-table harvest (hidden; run only by the main loop in T019). Writes
// evidence/amount-table.txt for paste-harvest.js. A target it cannot meet prints
// "STOP AND SURFACE: <target> <measurement>" (FR-048), never a moved bar.
// =============================================================================
TEST_CASE("MassCompressor_AmountTableHarvest", "[mass-dynamics-harvest][.calibration]") {
    MccTable table = BassCompressor::kAmountTable;   // the seed: slope and knee are kept
    const MccTable seed = table;
    const auto withTable = [&table](MassDynamics& md) {
        MccProbe::overrideAmountTable(MccProbe::compressor(md), &table);
    };
    std::size_t stops = 0;
    std::vector<std::string> stopLines;
    auto stop = [&](const std::string& line) {
        std::printf("STOP AND SURFACE: %s\n", line.c_str());
        stopLines.push_back("STOP AND SURFACE: " + line);
        ++stops;
    };

    // Steady GR of the calibration voice at `amount` under the current candidate table.
    auto calGr = [&](double f0, float amount) {
        mdx::ChainConfig c = mccCalibrationConfig(f0, amount, 3.0);
        c.configureDynamics = withTable;
        const mdx::ChainTaps t = mdx::renderChain(c);
        return mccSteadyGrDb(t.compGrDb, mccAt(1.0), mccAt(3.0));
    };
    // Steady GR of the Mass 100 voice (all six targets from the law) under the current table.
    auto mass100Gr = [&](double f0) {
        mdx::ChainConfig c = mccMass100Config(f0, 3.0);
        c.configureDynamics = withTable;
        const mdx::ChainTaps t = mdx::renderChain(c);
        return mccSteadyGrDb(t.compGrDb, mccAt(1.0), mccAt(3.0));
    };
    auto minAmount1Gr = [&]() {
        double m = 1e300;
        for (const MccNote& note : mccNotes())
            m = std::min(m, calGr(note.hz, 1.0f));
        return m;
    };
    auto minMass100Gr = [&]() { return std::min(mass100Gr(mtx::kE1Hz), mass100Gr(mtx::kC3Hz)); };

    // Rows whose threshold is harvested; the others are linear in amount between them (step 3).
    std::array<bool, 5> harvested{true, false, false, false, true};
    auto interpolate = [&]() {
        for (std::size_t i = 1; i + 1 < table.size(); ++i) {
            if (harvested[i])
                continue;
            std::size_t lo = i;
            while (!harvested[lo])
                --lo;
            std::size_t hi = i;
            while (!harvested[hi])
                ++hi;
            const float t = (table[i].amount - table[lo].amount) / (table[hi].amount - table[lo].amount);
            table[i].thresholdDb = table[lo].thresholdDb + t * (table[hi].thresholdDb - table[lo].thresholdDb);
        }
    };

    // ---- (1) amount-1 threshold: min over {E0, E1, E2, C3} of steady GR = 4.0 +/- 0.05 dB ----
    constexpr double kTarget1Db = 4.0;
    constexpr double kTarget1Tol = 0.05;
    double t1Lo = -60.0;   // more GR
    double t1Hi = 0.0;     // less GR
    table[4].thresholdDb = static_cast<float>(t1Lo);
    const double grAtLo = minAmount1Gr();
    bool t1Done = false;
    double t1Gr = grAtLo;
    if (grAtLo < kTarget1Db - kTarget1Tol) {
        std::ostringstream os;
        os << std::setprecision(6) << "target1 (SC-006(d)) amount-1 min steady GR " << grAtLo
           << " dB at threshold -60 dB < 3.95 dB";
        stop(os.str());
    } else {
        for (int it = 0; it < 40 && !t1Done; ++it) {
            const double mid = 0.5 * (t1Lo + t1Hi);
            table[4].thresholdDb = static_cast<float>(mid);
            t1Gr = minAmount1Gr();
            std::printf("MassCompressor_AmountTableHarvest (1) iter %d: threshold %.6f dB -> min GR %.6f dB\n", it, mid, t1Gr);
            if (std::abs(t1Gr - kTarget1Db) <= kTarget1Tol)
                t1Done = true;
            else if (t1Gr > kTarget1Db)
                t1Lo = mid;
            else
                t1Hi = mid;
        }
        if (!t1Done) {
            std::ostringstream os;
            os << std::setprecision(6) << "target1 (SC-006(d)) bisection did not reach 4.0 +/- 0.05 dB: last threshold "
               << table[4].thresholdDb << " dB, min GR " << t1Gr << " dB";
            stop(os.str());
        }
    }
    const float threshold1 = table[4].thresholdDb;
    interpolate();

    // ---- (2) the law's Mass 100 compression amount: steady GR(100) >= 3.5 dB at E1 and C3 ----
    constexpr double kTarget2Db = 3.5;
    const float a100 = MassLaw::evaluate(MassLaw::kMaxMass, MassLaw::Targets{}).compressionAmount;
    std::size_t r = 0;
    while (r + 1 < table.size() && table[r].amount < a100)
        ++r;
    std::printf("MassCompressor_AmountTableHarvest (2) law Mass 100 compression amount %.6f -> row %zu (amount %.6f)\n",
                static_cast<double>(a100), r, static_cast<double>(table[r].amount));
    double gr100 = minMass100Gr();
    std::printf("MassCompressor_AmountTableHarvest (2) Mass 100 min GR (E1, C3) after (1): %.6f dB\n", gr100);
    if (r == 0) {
        std::ostringstream os;
        os << std::setprecision(6) << "target2 (SC-002) the law's Mass 100 compression amount " << a100
           << " maps to row 0 (slope 0): GR(100) " << gr100 << " dB";
        stop(os.str());
    } else if (gr100 < kTarget2Db) {
        harvested[r] = true;
        const MccTable before = table;
        auto setRow = [&](double thr) {
            table = before;
            table[r].thresholdDb = static_cast<float>(thr);
            for (std::size_t j = r + 1; j < table.size(); ++j)   // stay monotone
                table[j].thresholdDb = std::min(before[j].thresholdDb, table[r].thresholdDb);
            interpolate();
        };
        double lo = -60.0;                                       // meets (checked below)
        double hi = static_cast<double>(before[r].thresholdDb);   // fails
        setRow(lo);
        const double grLo = minMass100Gr();
        if (grLo < kTarget2Db) {
            std::ostringstream os;
            os << std::setprecision(6) << "target2 (SC-002) Mass 100 min GR " << grLo << " dB at row " << r
               << " threshold -60 dB < 3.5 dB";
            stop(os.str());
        } else {
            for (int it = 0; it < 40 && hi - lo > 0.01; ++it) {
                const double mid = 0.5 * (lo + hi);
                setRow(mid);
                const double g = minMass100Gr();
                std::printf("MassCompressor_AmountTableHarvest (2) iter %d: row %zu threshold %.6f dB -> min GR(100) %.6f dB\n",
                            it, r, mid, g);
                if (g >= kTarget2Db)
                    lo = mid;
                else
                    hi = mid;
            }
            setRow(lo);
        }
    }

    // ---- (3) steady GR per row at every note; monotone in amount ----
    std::array<std::array<double, 4>, 5> rowGr{};
    for (std::size_t i = 0; i < table.size(); ++i)
        for (std::size_t n = 0; n < mccNotes().size(); ++n)
            rowGr[i][n] = calGr(mccNotes()[n].hz, table[i].amount);
    bool monotone = true;
    for (std::size_t i = 0; i + 1 < table.size(); ++i) {
        for (std::size_t n = 0; n < mccNotes().size(); ++n) {
            // 1e-6 dB float allowance: rows below threshold read GR ~0 on both sides.
            if (rowGr[i + 1][n] < rowGr[i][n] - 1e-6) {
                monotone = false;
                std::ostringstream os;
                os << std::setprecision(6) << "target3 (monotone GR) " << mccNotes()[n].name << " row " << (i + 1) << " GR "
                   << rowGr[i + 1][n] << " dB < row " << i << " GR " << rowGr[i][n] << " dB";
                stop(os.str());
            }
        }
    }
    CHECK(monotone);

    // ---- (4) makeup = mean of the E1 and C3 steady GR; spread <= 0.8 dB ----
    constexpr std::size_t kE1 = 1;
    constexpr std::size_t kC3 = 3;
    table[0].makeupDb = 0.0f;
    for (std::size_t i = 1; i < table.size(); ++i) {
        table[i].makeupDb = static_cast<float>(0.5 * (rowGr[i][kE1] + rowGr[i][kC3]));
        const double spread = std::abs(rowGr[i][kE1] - rowGr[i][kC3]);
        if (spread > 0.8) {
            std::ostringstream os;
            os << std::setprecision(6) << "target3 (SC-022) row " << i << " amount " << table[i].amount << " E1/C3 spread "
               << spread << " dB > 0.8 dB (E1 " << rowGr[i][kE1] << ", C3 " << rowGr[i][kC3] << ")";
            stop(os.str());
        }
    }
    // The header's static_assert invariants (mass_dynamics.h): threshold non-increasing; makeup non-decreasing.
    for (std::size_t i = 0; i + 1 < table.size(); ++i) {
        if (table[i + 1].thresholdDb > table[i].thresholdDb || table[i + 1].makeupDb < table[i].makeupDb) {
            std::ostringstream os;
            os << std::setprecision(6) << "table invariant rows " << i << "/" << (i + 1) << ": threshold "
               << table[i].thresholdDb << " -> " << table[i + 1].thresholdDb << ", makeup " << table[i].makeupDb << " -> "
               << table[i + 1].makeupDb;
            stop(os.str());
        }
    }
    const double gr100E1 = mass100Gr(mtx::kE1Hz);
    const double gr100C3 = mass100Gr(mtx::kC3Hz);
    if (std::min(gr100E1, gr100C3) < kTarget2Db) {
        std::ostringstream os;
        os << std::setprecision(6) << "target2 (SC-002) final table Mass 100 GR E1 " << gr100E1 << " dB, C3 " << gr100C3
           << " dB < 3.5 dB";
        stop(os.str());
    }

    // ---- evidence/amount-table.txt ----
    const std::string dir = mccEvidenceDir();
    std::filesystem::create_directories(dir);
    const std::string path = dir + "amount-table.txt";
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << "BEGIN-INITIALIZER amount-table\n";
    for (const BassCompressor::AmountRow& row : table) {
        out << "        {.amount = " << mccFloatLiteral(row.amount, 9) << ", .thresholdDb = " << mccFloatLiteral(row.thresholdDb, 9)
            << ", .slope = " << mccFloatLiteral(row.slope, 9) << ", .kneeDb = " << mccFloatLiteral(row.kneeDb, 9)
            << ", .makeupDb = " << mccFloatLiteral(row.makeupDb, 9) << "},\n";
    }
    out << "END-INITIALIZER amount-table\n";
    out << std::setprecision(6);
    out << "# MassCompressor_AmountTableHarvest (plan S8.3): 48 kHz, host block 512; calibration voice = the Mass reference\n"
           "# voice at Mass 50 with targetOverride[CompressionAmount]; steady GR = -mean(currentGainReductionDb) over [1 s, 3 s]\n"
           "# of a 3 s render. Slope and knee keep the seed.\n";
    out << "# (1) amount-1 threshold " << threshold1 << " dB (seed " << seed[4].thresholdDb << "): min GR over E0..C3 "
        << t1Gr << " dB (target 4.0 +/- 0.05)\n";
    out << "# (2) law Mass 100 compression amount " << a100 << " -> row " << r << "; Mass 100 GR E1 " << gr100E1 << " dB, C3 "
        << gr100C3 << " dB (target >= 3.5)\n";
    out << "# (3) harvested rows:";
    for (std::size_t i = 0; i < harvested.size(); ++i)
        out << (harvested[i] ? " " + std::to_string(i) : std::string());
    out << "; the other thresholds are linear in amount between them\n";
    out << "# Seed thresholds:";
    for (const BassCompressor::AmountRow& row : seed)
        out << " " << row.thresholdDb;
    out << "\n# Steady GR per row (dB): amount, E0, E1, E2, C3\n";
    for (std::size_t i = 0; i < table.size(); ++i) {
        out << "row " << i << " amount " << table[i].amount << ":";
        for (std::size_t n = 0; n < mccNotes().size(); ++n)
            out << " " << mccNotes()[n].name << " " << rowGr[i][n];
        out << "\n";
    }
    out << "# Makeup residual per row and note (makeup - GR, dB; gated at E1 and C3 by SC-022, E0 / E2 recorded)\n";
    for (std::size_t i = 0; i < table.size(); ++i) {
        out << "row " << i << " makeup " << table[i].makeupDb << ":";
        for (std::size_t n = 0; n < mccNotes().size(); ++n)
            out << " " << mccNotes()[n].name << " " << std::showpos << (static_cast<double>(table[i].makeupDb) - rowGr[i][n])
                << std::noshowpos;
        out << "\n";
    }
    for (const std::string& line : stopLines)
        out << line << "\n";
    REQUIRE(out.good());
    // Never print the stop phrase here: harvest.sh treats any line carrying it as a stop.
    std::printf("MassCompressor_AmountTableHarvest: wrote %s (%zu unmet targets)\n", path.c_str(), stops);
    CHECK(stops == 0);
}
