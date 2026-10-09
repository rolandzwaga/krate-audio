// ==============================================================================
// Vorago Phase 12 - FR-060 kRows retune probe ([.probe], never in CI)
// ==============================================================================
// Owning task: specs/vorago-phase12-parameters/tasks.md T004 (gate P-0, plan S2.6).
//
// WHAT THIS IS. A HIDDEN measurement, not a test: it prints the per-candidate
// table T005 reads and asserts nothing except that every figure it prints is
// finite. It measures candidate Gravity / Pressure / Mass rows WITHOUT editing
// kRows, by emulating each candidate row through the Phase 12 per-target base
// override (VoragoMacroMatrix::setTargetBase, vorago_macro_matrix.h:953).
//
// THE FIXTURE IS SC-008's, REPRODUCED (vorago_macro_test.cpp:544-649, :723-869,
// :1211-1330): seeds {101, 202, 303}; sweep points {0, .25, .5, .75, 1}; 60 s at
// 48 kHz in 512-sample blocks; window [10 s, 60 s] in samples; polyphony 1;
// applyFastAttack (vorago_fixtures.h:712); macros applied AFTER noteOn; the
// row's own note (Gravity / Pressure at C1 = 36, Mass at C3 = 48); the same
// metrics (meanOctaveOffset re-implemented here - it is TU-local at
// vorago_macro_test.cpp:728; crestFactorDb vorago_fixtures.h:316; bandEnergyDb
// :245 over [20, 80] Hz); spearmanRho (:555); the same endpoint formula. Only
// the metric the axis needs is computed per render (the others are not read).
//
// EMULATION. At sweep point x the macro itself is set to x (so every SHIPPED
// row still applies) and, per candidate row on target t,
//     setTargetBase(t, base(t) + amount * applyModCurve(curve, x))
// where base(t) is the target's kRows literal (getTargetBase() on a matrix
// with no override - the same base a real row on t would have to carry,
// everyRowSharesOneBasePerTarget). Gravity rows use g = (x - 0.5) * 2 and
// contribute amount * applyModCurve(curve, |g|) * sign(g), exactly as
// VoragoMacroMatrix::contributionOf (vorago_macro_matrix.h:1102-1111). Because
// evaluateAll() seeds the target with the override and then adds the shipped
// rows' contributions, the emulated value equals what a real kRows row would
// produce. A candidate that REPLACES a shipped amount (P-a, P-c) is emulated
// as the delta row (new - shipped) on the shipped row's curve.
//
// THRESHOLDS are Phase 10's, unchanged: Gravity rho <= -0.9 and endpoint
// (PercentLower) >= 0.30; Pressure rho <= -0.9 (DbLower, sign-adjusted) and
// endpoint >= 3 dB; Mass rho >= 0.9 and endpoint (DbHigher) >= 0.25 dB.
//
// RUN ALONE (tens of minutes):
//   dsp_systems_tests.exe "VoragoMacro_Phase12RetuneProbe"
//       > specs/vorago-phase12-parameters/artifacts/fr060_probe.log 2>&1
// NOTE: that table was recorded against the Phase 10 kRows. Since T007 landed
// the P-a retune and the OutputDriveDb / ResonanceOctaveLock rows, the T004
// Pressure / Gravity candidates emulate ON TOP of those rows and no longer
// reproduce the log; the recorded log is the T004 evidence.
//
// SECOND HIDDEN CASE (T007 part 2, spec B-2): VoragoMacro_Phase12DriveProbe
// sweeps the Pressure -> OutputDriveDb amount A over {6, 12, 18, 24} dB with
// the P-a retune (Pressure -> OutputSaturation amount 0.88) applied, on the
// same Pressure fixture and thresholds. Both are emulated RELATIVE TO WHAT
// kRows NOW SHIPS (delta = wanted amount - shipped amount, read from kRows at
// run time), so the table is right whatever provisional A is landed. ALONE:
//   dsp_systems_tests.exe "VoragoMacro_Phase12DriveProbe"
//       > specs/vorago-phase12-parameters/artifacts/fr060_drive_probe.log 2>&1
//
// PORTABILITY: no std::isnan / std::isfinite; finiteness via the fast-math-
// immune Krate::DSP::detail::isFinite (core/db_utils.h:118,125).
// HEAP-ONLY ENGINE: every VoragoEngine is built by makeEngine (std::make_unique).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::ModCurve;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroTarget;
using Krate::DSP::VoragoMacroValues;
using Krate::DSP::VoragoVoice;

using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::bandEnergyDb;
using Krate::DSP::TestUtils::Vorago::crestFactorDb;
using Krate::DSP::TestUtils::Vorago::makeEngine;
using Krate::DSP::TestUtils::Vorago::spearmanRho;

// -----------------------------------------------------------------------------
// SC-008 fixture constants (vorago_macro_test.cpp:544-560, :636-640)
// -----------------------------------------------------------------------------

constexpr double kProbeSampleRate = 48000.0;
constexpr std::uint8_t kProbeVelocity = 100u;
constexpr std::uint8_t kNoteC1 = 36u;  ///< Gravity / Pressure rows
constexpr std::uint8_t kNoteC3 = 48u;  ///< Mass row (Weight's construction)

constexpr double kProbeSeconds = 60.0;
constexpr double kProbeWindowStartSeconds = 10.0;
constexpr std::size_t kProbeTotalSamples =
    static_cast<std::size_t>(kProbeSeconds * kProbeSampleRate);  // 2 880 000
constexpr std::size_t kProbeWindowStartSample =
    static_cast<std::size_t>(kProbeWindowStartSeconds * kProbeSampleRate);  // 480 000
constexpr std::size_t kProbeBlockSamples = 512u;

constexpr std::array<std::uint32_t, 3> kProbeSeeds = {{101u, 202u, 303u}};
constexpr std::array<double, 5> kProbePoints = {{0.0, 0.25, 0.50, 0.75, 1.0}};

constexpr std::size_t kNumTargets = VoragoMacroMatrix::kNumTargets;

// -----------------------------------------------------------------------------
// The three axes under probe
// -----------------------------------------------------------------------------

enum class ProbeAxis : std::uint8_t { Gravity, Pressure, Mass };

/// SC-008's endpoint forms used by the three axes (vorago_macro_test.cpp:880-886).
enum class ProbeEndpoint : std::uint8_t { PercentLower, DbLower, DbHigher };

struct AxisSpec {
    VoragoMacro macro;
    const char* metricName;
    std::uint8_t note;
    ProbeEndpoint kind;
    double threshold;
};

[[nodiscard]] AxisSpec specOf(ProbeAxis axis) noexcept {
    switch (axis) {
        case ProbeAxis::Gravity:
            return {VoragoMacro::Gravity, "mean |log2(ratio) - int| @C1", kNoteC1,
                    ProbeEndpoint::PercentLower, 0.30};
        case ProbeAxis::Pressure:
            return {VoragoMacro::Pressure, "crest factor (dB) @C1", kNoteC1,
                    ProbeEndpoint::DbLower, 3.0};
        case ProbeAxis::Mass:
        default:
            return {VoragoMacro::Mass, "energy < 80 Hz (dB) @C3", kNoteC3,
                    ProbeEndpoint::DbHigher, 0.25};
    }
}

/// The documented sign of the Spearman correlation (vorago_macro_test.cpp:1017).
[[nodiscard]] double expectedRhoSign(ProbeEndpoint k) noexcept {
    return (k == ProbeEndpoint::DbHigher) ? 1.0 : -1.0;
}

/// The endpoint figure in the threshold's units (vorago_macro_test.cpp:1022-1041).
[[nodiscard]] double endpointFigure(ProbeEndpoint k, double v0, double v1) noexcept {
    switch (k) {
        case ProbeEndpoint::PercentLower:
            return (v0 > 0.0) ? ((v0 - v1) / v0) : 0.0;
        case ProbeEndpoint::DbLower:
            return v0 - v1;
        case ProbeEndpoint::DbHigher:
        default:
            return v1 - v0;
    }
}

// -----------------------------------------------------------------------------
// Candidate rows (plan S2.6, tasks.md T004)
// -----------------------------------------------------------------------------

struct ProbeRow {
    VoragoMacroTarget target;
    float amount;  ///< for a REPLACING candidate: the delta (new - shipped)
    ModCurve curve;
};

struct ProbeCandidate {
    std::string name;
    ProbeAxis axis;
    std::vector<ProbeRow> rows;  ///< empty = baseline (shipped rows only)
};

/// Every candidate T004 names: 3 baselines, 3 Mass amounts, G-a / G-b / G-a+G-b,
/// and all 15 non-empty subsets of {P-a, P-b, P-c, P-d}.
[[nodiscard]] std::vector<ProbeCandidate> buildCandidates() {
    std::vector<ProbeCandidate> out;

    out.push_back({"Gravity  baseline", ProbeAxis::Gravity, {}});
    out.push_back({"Pressure baseline", ProbeAxis::Pressure, {}});
    out.push_back({"Mass     baseline", ProbeAxis::Mass, {}});

    // Mass -> SubToneLevelOffsetDb, base 0.0 (shared with Weight), Linear.
    for (const float amount : {3.0f, 4.5f, 6.0f}) {
        std::ostringstream n;
        n << std::fixed << std::setprecision(1) << "Mass     SubToneLevelOffsetDb +" << amount
          << " dB";
        out.push_back({n.str(), ProbeAxis::Mass,
                       {{VoragoMacroTarget::SubToneLevelOffsetDb, amount, ModCurve::Linear}}});
    }

    // Gravity. G-a: BreathingDepth base 0.30, amount -0.30, Linear.
    //          G-b: ResonanceWanderRate base 0.03, amount -0.028, Exponential.
    const ProbeRow ga{VoragoMacroTarget::BreathingDepth, -0.30f, ModCurve::Linear};
    const ProbeRow gb{VoragoMacroTarget::ResonanceWanderRate, -0.028f, ModCurve::Exponential};
    out.push_back({"Gravity  G-a", ProbeAxis::Gravity, {ga}});
    out.push_back({"Gravity  G-b", ProbeAxis::Gravity, {gb}});
    out.push_back({"Gravity  G-a+G-b", ProbeAxis::Gravity, {ga, gb}});

    // Pressure.
    //   P-a: OutputSaturation 0.35 -> 0.88, emulated as the delta +0.53 on the
    //        shipped row's Linear curve (vorago_macro_matrix.h:487-492).
    //   P-b: SubToneLevelOffsetDb base 0.0 (shared), amount -12 dB, Linear.
    //   P-c: EcologyLoopGain 0.16 -> 0.18, delta +0.02 on the shipped Linear
    //        curve (vorago_macro_matrix.h:481-486).
    //   P-d: CloudRichness base 0.70 (shared with Density), amount +0.28.
    //        The plan names no curve for P-d; Linear is used (stated here).
    const std::array<ProbeRow, 4> pRows = {{
        {VoragoMacroTarget::OutputSaturation, 0.53f, ModCurve::Linear},
        {VoragoMacroTarget::SubToneLevelOffsetDb, -12.0f, ModCurve::Linear},
        {VoragoMacroTarget::EcologyLoopGain, 0.02f, ModCurve::Linear},
        {VoragoMacroTarget::CloudRichness, 0.28f, ModCurve::Linear},
    }};
    const std::array<const char*, 4> pNames = {{"P-a", "P-b", "P-c", "P-d"}};
    for (unsigned mask = 1u; mask < 16u; ++mask) {
        ProbeCandidate c{"Pressure ", ProbeAxis::Pressure, {}};
        bool first = true;
        for (std::size_t b = 0; b < pRows.size(); ++b) {
            if ((mask & (1u << b)) == 0u) {
                continue;
            }
            c.name += first ? "" : "+";
            c.name += pNames[b];
            c.rows.push_back(pRows[b]);
            first = false;
        }
        out.push_back(std::move(c));
    }
    return out;
}

/// One candidate row's contribution at macro value @p x - contributionOf()'s
/// arithmetic (vorago_macro_matrix.h:1102-1111), in float as the matrix does it.
[[nodiscard]] float emulatedContribution(const ProbeRow& row, VoragoMacro macro, float x) noexcept {
    if (macro == VoragoMacro::Gravity) {
        const float g = (x - 0.5f) * 2.0f;
        const float sign = (g < 0.0f) ? -1.0f : 1.0f;
        return row.amount * Krate::DSP::applyModCurve(row.curve, std::fabs(g)) * sign;
    }
    return row.amount * Krate::DSP::applyModCurve(row.curve, x);
}

// -----------------------------------------------------------------------------
// Metrics
// -----------------------------------------------------------------------------

/// Gravity's raw figure, re-implemented from vorago_macro_test.cpp:728-746
/// (TU-local there): mean over the network's resolved peaks of
/// |log2(peak / note) - nearest int|.
[[nodiscard]] double meanOctaveOffset(const VoragoVoice& voice) noexcept {
    const double note = static_cast<double>(voice.resonance().getNoteFrequency());
    if (!(note > 0.0)) {
        return 0.0;
    }
    const std::size_t peaks = voice.resonance().getNumPeaks();
    double sum = 0.0;
    std::size_t counted = 0u;
    for (std::size_t p = 0; p < peaks; ++p) {
        const double f = static_cast<double>(voice.resonance().getPeakCurrentFrequency(p));
        if (!(f > 0.0)) {
            continue;
        }
        const double l2 = std::log2(f / note);
        sum += std::fabs(l2 - std::round(l2));
        ++counted;
    }
    return (counted > 0u) ? (sum / static_cast<double>(counted)) : 0.0;
}

/// ONE probe render at sweep point @p x for @p candidate and @p seed, returning
/// the axis's metric. Mirrors renderSweepPoint (vorago_macro_test.cpp:764-869)
/// for the parts the three metrics read.
[[nodiscard]] double renderProbePoint(const ProbeCandidate& candidate, float x,
                                      std::uint32_t seed) {
    const AxisSpec spec = specOf(candidate.axis);

    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(kProbeSampleRate, cfg);
    engine->setSeed(seed);
    engine->setPolyphony(1u);
    applyFastAttack(*engine);
    engine->noteOn(spec.note, kProbeVelocity);

    // The macro itself at x (neutral elsewhere), so every shipped row applies.
    VoragoMacroMatrix matrix;
    VoragoMacroValues macros{};
    switch (spec.macro) {
        case VoragoMacro::Gravity:
            macros.gravity = x;
            break;
        case VoragoMacro::Pressure:
            macros.pressure = x;
            break;
        case VoragoMacro::Mass:
        default:
            macros.mass = x;
            break;
    }
    matrix.setMacros(macros);

    // Emulate the candidate rows: base(t) + sum of their contributions.
    std::array<float, kNumTargets> delta{};
    std::array<bool, kNumTargets> touched{};
    for (const ProbeRow& row : candidate.rows) {
        const auto i = static_cast<std::size_t>(row.target);
        delta[i] += emulatedContribution(row, spec.macro, x);
        touched[i] = true;
    }
    for (std::size_t i = 0; i < kNumTargets; ++i) {
        if (touched[i]) {
            const auto t = static_cast<VoragoMacroTarget>(i);
            // No override has been set on this fresh matrix, so this is the
            // kRows literal base of the target.
            matrix.setTargetBase(t, matrix.getTargetBase(t) + delta[i]);
        }
    }
    matrix.apply(*engine);

    std::vector<float> l(kProbeBlockSamples, 0.0f);
    std::vector<float> r(kProbeBlockSamples, 0.0f);
    std::vector<float> mono;
    mono.reserve(kProbeTotalSamples - kProbeWindowStartSample);

    double offsetAccum = 0.0;
    std::size_t polls = 0u;

    for (std::size_t done = 0; done < kProbeTotalSamples; done += kProbeBlockSamples) {
        const std::size_t n = std::min(kProbeBlockSamples, kProbeTotalSamples - done);
        engine->processStereoBlock(l.data(), r.data(), n);
        engine->processOutputStage(l.data(), r.data(), n);

        if ((done + n) > kProbeWindowStartSample) {
            const std::size_t from =
                (done >= kProbeWindowStartSample) ? 0u : (kProbeWindowStartSample - done);
            for (std::size_t i = from; i < n; ++i) {
                mono.push_back(0.5f * (l[i] + r[i]));
            }
            if (candidate.axis == ProbeAxis::Gravity) {
                offsetAccum += meanOctaveOffset(engine->getVoice(0));
                ++polls;
            }
        }
    }

    const std::span<const float> window(mono);
    switch (candidate.axis) {
        case ProbeAxis::Gravity:
            return (polls > 0u) ? (offsetAccum / static_cast<double>(polls)) : 0.0;
        case ProbeAxis::Pressure:
            return crestFactorDb(window);
        case ProbeAxis::Mass:
        default:
            return bandEnergyDb(window, kProbeSampleRate, 20.0, 80.0);
    }
}

/// Render every candidate over the SC-008 fixture and print its per-seed and
/// mean figures plus a one-line summary each (defined below the T004 case).
/// Asserts finiteness only.
void runProbeTable(const std::vector<ProbeCandidate>& candidates);

}  // namespace

// =============================================================================
// FR-060 retune probe (T004) - hidden, prints the table, asserts finiteness only
// =============================================================================

TEST_CASE("VoragoMacro_Phase12RetuneProbe", "[.probe][vorago]") {
    const std::vector<ProbeCandidate> candidates = buildCandidates();

    {
        const VoragoMacroMatrix fresh{};
        std::cout << std::fixed << std::setprecision(4)
                  << "FR-060 retune probe (SC-008 fixture: seeds 101/202/303 x points "
                     "0/.25/.5/.75/1 x 60 s @ 48 kHz; window samples ["
                  << kProbeWindowStartSample << ", " << kProbeTotalSamples
                  << "); polyphony 1; fast attack; macros after noteOn)\n"
                  << "kRows literal bases used by the emulation: BreathingDepth="
                  << fresh.getTargetBase(VoragoMacroTarget::BreathingDepth)
                  << " ResonanceWanderRate="
                  << fresh.getTargetBase(VoragoMacroTarget::ResonanceWanderRate)
                  << " OutputSaturation="
                  << fresh.getTargetBase(VoragoMacroTarget::OutputSaturation)
                  << " SubToneLevelOffsetDb="
                  << fresh.getTargetBase(VoragoMacroTarget::SubToneLevelOffsetDb)
                  << " EcologyLoopGain=" << fresh.getTargetBase(VoragoMacroTarget::EcologyLoopGain)
                  << " CloudRichness=" << fresh.getTargetBase(VoragoMacroTarget::CloudRichness)
                  << "\nPhase 10 reference (specs/vorago-phase10-voice-engine/compliance.md:20): "
                     "Gravity endpoint 0.18014; Pressure rho -0.566667 endpoint 0.0735783 dB; "
                     "Mass rho -1 endpoint -0.446365 dB\n"
                  << "Thresholds (Phase 10, unchanged): Gravity rho<=-0.9 endpoint>=0.30; "
                     "Pressure rho<=-0.9 endpoint>=3 dB; Mass rho>=0.9 endpoint>=0.25 dB\n"
                  << std::flush;
    }

    runProbeTable(candidates);
}

namespace {

void runProbeTable(const std::vector<ProbeCandidate>& candidates) {
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(4);
    summary << "\n==== SUMMARY (candidate | mean rho | mean endpoint | verdict) ====\n";

    for (const ProbeCandidate& candidate : candidates) {
        const AxisSpec spec = specOf(candidate.axis);

        std::array<double, 5> metricSum{};
        std::array<double, 3> seedRho{};
        std::array<double, 3> seedEndpoint{};
        double rhoSum = 0.0;

        for (std::size_t s = 0; s < kProbeSeeds.size(); ++s) {
            std::array<double, 5> series{};
            for (std::size_t p = 0; p < kProbePoints.size(); ++p) {
                series[p] = renderProbePoint(candidate, static_cast<float>(kProbePoints[p]),
                                             kProbeSeeds[s]);
                REQUIRE(Krate::DSP::detail::isFinite(series[p]));
                metricSum[p] += series[p];
            }
            seedRho[s] = spearmanRho(kProbePoints, series);
            seedEndpoint[s] = endpointFigure(spec.kind, series.front(), series.back());
            REQUIRE(Krate::DSP::detail::isFinite(seedRho[s]));
            REQUIRE(Krate::DSP::detail::isFinite(seedEndpoint[s]));
            rhoSum += seedRho[s];
        }

        std::array<double, 5> meanMetric{};
        for (std::size_t p = 0; p < meanMetric.size(); ++p) {
            meanMetric[p] = metricSum[p] / static_cast<double>(kProbeSeeds.size());
            REQUIRE(Krate::DSP::detail::isFinite(meanMetric[p]));
        }
        const double meanRho = rhoSum / static_cast<double>(kProbeSeeds.size());
        const double meanEndpoint =
            endpointFigure(spec.kind, meanMetric.front(), meanMetric.back());
        REQUIRE(Krate::DSP::detail::isFinite(meanRho));
        REQUIRE(Krate::DSP::detail::isFinite(meanEndpoint));

        const bool pass = ((meanRho * expectedRhoSign(spec.kind)) >= 0.9)
                          && (meanEndpoint >= spec.threshold);

        std::ostringstream os;
        os << std::fixed << std::setprecision(4);
        os << candidate.name << "  [" << spec.metricName << "]\n"
           << "    mean rho=" << meanRho << "  mean endpoint=" << meanEndpoint
           << "  threshold=" << spec.threshold << "  -> " << (pass ? "PASS" : "FAIL") << "\n"
           << "    per-seed rho/endpoint:";
        for (std::size_t s = 0; s < kProbeSeeds.size(); ++s) {
            os << "  " << kProbeSeeds[s] << ": " << seedRho[s] << " / " << seedEndpoint[s];
        }
        os << "\n    mean metric [0, .25, .5, .75, 1]: [";
        for (std::size_t p = 0; p < meanMetric.size(); ++p) {
            os << ((p == 0u) ? "" : ", ") << meanMetric[p];
        }
        os << "]\n";
        std::cout << os.str() << std::flush;

        summary << "  " << std::left << std::setw(40) << candidate.name << std::right
                << "  rho=" << std::setw(8) << meanRho << "  endpoint=" << std::setw(9)
                << meanEndpoint << "  " << (pass ? "PASS" : "FAIL") << "\n";
    }

    std::cout << summary.str() << std::flush;
}

/// The amount kRows currently ships on the (@p macro, @p target) row, or 0 if
/// no such row exists. Used so the drive probe's emulation is relative to the
/// landed table rather than to a hard-coded snapshot of it.
[[nodiscard]] float shippedAmount(VoragoMacro macro, VoragoMacroTarget target) noexcept {
    for (const auto& row : VoragoMacroMatrix::kRows) {
        if (row.macro == macro && row.target == target) {
            return row.amount;
        }
    }
    return 0.0f;
}

}  // namespace

// =============================================================================
// FR-060 drive probe (T007 part 2, spec B-2) - hidden, prints the table
// =============================================================================

TEST_CASE("VoragoMacro_Phase12DriveProbe", "[.probe][vorago]") {
    // P-a: Pressure -> OutputSaturation amount 0.88 (base 0.12 unchanged).
    constexpr float kPaAmount = 0.88f;
    const float shippedSat =
        shippedAmount(VoragoMacro::Pressure, VoragoMacroTarget::OutputSaturation);
    const float shippedDrive =
        shippedAmount(VoragoMacro::Pressure, VoragoMacroTarget::OutputDriveDb);

    std::cout << std::fixed << std::setprecision(4)
              << "FR-060 drive probe (T007 part 2, spec B-2; SC-008 Pressure fixture: seeds "
                 "101/202/303 x points 0/.25/.5/.75/1 x 60 s @ 48 kHz; note C1)\n"
              << "Shipped kRows amounts: Pressure->OutputSaturation=" << shippedSat
              << "  Pressure->OutputDriveDb=" << shippedDrive
              << " dB; every candidate below is emulated as the delta to P-a ("
              << kPaAmount << ") plus the named drive amount\n"
              << "Threshold (Phase 10, unchanged): Pressure rho<=-0.9 endpoint>=3 dB\n"
              << std::flush;

    std::vector<ProbeCandidate> candidates;
    candidates.push_back({"Pressure shipped kRows", ProbeAxis::Pressure, {}});
    for (const float amountDb : {6.0f, 12.0f, 18.0f, 24.0f}) {
        std::ostringstream n;
        n << std::fixed << std::setprecision(0) << "Pressure P-a + OutputDriveDb +" << amountDb
          << " dB";
        candidates.push_back(
            {n.str(),
             ProbeAxis::Pressure,
             {{VoragoMacroTarget::OutputSaturation, kPaAmount - shippedSat, ModCurve::Linear},
              {VoragoMacroTarget::OutputDriveDb, amountDb - shippedDrive, ModCurve::Linear}}});
    }


    // Main-loop extension (2026-09-25): the drive alone tops out at 2.60 dB at the
    // saturator's +24 dB ceiling (18 dB: 2.49), so the crest floor - the sub-tone
    // beating - is attacked directly with a Pressure -> SubToneLevelOffsetDb row
    // (P-b family, base 0.0 dB shared with Weight and Mass) on top of P-a + drive.
    for (const float driveDb : {18.0f, 24.0f}) {
        for (const float subDb : {-12.0f, -18.0f, -24.0f}) {
            std::ostringstream n;
            n << std::fixed << std::setprecision(0) << "Pressure P-a + OutputDriveDb +" << driveDb
              << " dB + SubToneLevelOffsetDb " << subDb << " dB";
            candidates.push_back(
                {n.str(),
                 ProbeAxis::Pressure,
                 {{VoragoMacroTarget::OutputSaturation, kPaAmount - shippedSat, ModCurve::Linear},
                  {VoragoMacroTarget::OutputDriveDb, driveDb - shippedDrive, ModCurve::Linear},
                  {VoragoMacroTarget::SubToneLevelOffsetDb, subDb, ModCurve::Linear}}});
        }
    }

    runProbeTable(candidates);
}

// =============================================================================
// Phase 13d row-emulation probe (specs/vorago-phase13d-engine-ceilings/tasks.md
// T021, plan S2.5) - hidden, prints the SweepAxes rows a candidate touches
// =============================================================================
// WHAT THIS IS. The macro PRE-SCREEN for 13d's row rungs and M4's member
// isolation (I-M4). It emulates candidate kRows deltas WITHOUT editing kRows,
// through setTargetBase exactly as the Phase 12 case above does
// (renderProbePoint), and renders the VoragoMacro_SweepAxes fixture for every
// SweepAxes row the candidate touches. No assertion on any figure: it prints.
//
// INPUT.
//   VORAGO_ROWPROBE="<macro>:<target>:<amount>:<curve>[;...]"
//       <macro>  a VoragoMacro enumerator name   (vorago_macro_matrix.h:106-120)
//       <target> a VoragoMacroTarget name        (vorago_macro_matrix.h:140-185)
//       <amount> a signed float, the DELTA row's amount
//       <curve>  Linear | Exponential | SCurve   (FR-065; Stepped is refused)
//     Each entry adds amount * applyModCurve(curve, m) to its target's base,
//     m being the entry macro's value in the render (Gravity bipolar, as
//     contributionOf does, vorago_macro_matrix.h:1168-1176). A negative amount
//     equal to a shipped row's amount on the same curve CANCELS that member.
//     The SweepAxes row of every entry's macro is rendered.
//   VORAGO_ROWPROBE_ROWS="<macro>[,<macro>...]"
//     SweepAxes rows to render in addition to those the entries touch, so the
//     shipped table can be read with no delta at all (T021's check:
//     VORAGO_ROWPROBE empty, VORAGO_ROWPROBE_ROWS=Movement reproduces
//     base_sweepaxes.log's Movement rho 0.8333).
//   An unknown name, a malformed amount or a malformed entry FAILs the case.
//
// THE FIXTURE IS VoragoMacro_SweepAxes', REPRODUCED (vorago_macro_test.cpp,
// TU-local there): seeds {101, 202, 303}; points {0, .25, .5, .75, 1}; 60 s at
// 48 kHz in 512-sample blocks; spectral window [10 s, 60 s] in samples;
// polyphony 1; applyFastAttack; macros applied AFTER noteOn; the row's note
// (C1, Age C4, Weight / Mass C3); Life renders 210 s and counts wake/sleep
// edges over [90 s, 210 s]; Entropy / Fog flatness over [0.94 f0, 20 f0]. The
// row rho is the mean of the per-seed rhos and the endpoint is taken on the
// mean series, as SweepAxes computes them. Thresholds are SweepAxes', unchanged.
//
// RUN ALONE (minutes per row):
//   VORAGO_ROWPROBE_ROWS=Movement dsp_systems_tests.exe "VoragoMacro_Phase13dRowProbe"
//       > specs/vorago-phase13d-engine-ceilings/artifacts/t021_check.log 2>&1
// =============================================================================

namespace {

namespace vfx = Krate::DSP::TestUtils::Vorago;

// -- names (enumerator order; the static_asserts pin the counts) --------------

constexpr auto kRowProbeMacroNames = std::to_array<const char*>(
    {"Darkness", "Age", "Density", "Movement", "Gravity", "Entropy", "Pressure", "Weight", "Fog",
     "Life", "Depth", "Mass"});
static_assert(kRowProbeMacroNames.size() == VoragoMacroMatrix::kNumMacros,
              "every VoragoMacro needs its name");

constexpr auto kRowProbeTargetNames = std::to_array<const char*>(
    {// Voice-owned
     "CloudRichness", "CloudSpectralTiltDb", "CloudMutation", "CloudInharmonicity",
     "CloudDriftDepthCents", "NoiseLevelDb", "NoiseWakeBase", "NoiseWanderRate",
     "ResonanceGravity", "ResonanceMix", "ResonanceWanderRate", "EcologyMix", "EcologyLoopGain",
     "BodyBlend", "BodyDamping", "BodyResonance", "BodyMix", "EcosystemDepth", "EventRateScale",
     "BloomDepth", "BloomSpawnRateHz", "BreathingDepth", "BreathingIrregularity", "TidalDepth",
     "ResonanceOctaveLock",
     // Engine-owned
     "SubToneLevelOffsetDb", "SubTrackingAmount", "SmearAmount", "SmearDecoherence", "SmearTilt",
     "GhostPeakLevel", "AtmosBlur", "OutputSaturation", "OutputDriveDb",
     // Cavern-owned
     "CavernSize", "CavernDarkness", "CavernDecaySeconds", "CavernFog", "CavernDamperDepth",
     "CavernMix", "CavernWidth"});
static_assert(kRowProbeTargetNames.size() == VoragoMacroMatrix::kNumTargets,
              "every VoragoMacroTarget needs its name");
static_assert(static_cast<std::size_t>(VoragoMacroTarget::BreathingDepth) == 21u
                  && static_cast<std::size_t>(VoragoMacroTarget::SubToneLevelOffsetDb) == 25u
                  && static_cast<std::size_t>(VoragoMacroTarget::CavernSize) == 34u,
              "the target name table must follow the enumerator order");

[[nodiscard]] const char* rowProbeMacroName(VoragoMacro m) noexcept {
    const auto i = static_cast<std::size_t>(m);
    return (i < kRowProbeMacroNames.size()) ? kRowProbeMacroNames[i] : "?";
}

[[nodiscard]] const char* rowProbeTargetName(VoragoMacroTarget t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    return (i < kRowProbeTargetNames.size()) ? kRowProbeTargetNames[i] : "?";
}

[[nodiscard]] const char* rowProbeCurveName(ModCurve c) noexcept {
    switch (c) {
        case ModCurve::Linear:
            return "Linear";
        case ModCurve::Exponential:
            return "Exponential";
        case ModCurve::SCurve:
            return "SCurve";
        case ModCurve::Stepped:
        default:
            return "Stepped";
    }
}

// -- the SweepAxes fixture's per-row constructions -----------------------------

constexpr std::uint8_t kRowProbeAgeNote = 60u;     ///< C4 (vorago_macro_test.cpp kAgeNote)
constexpr std::uint8_t kRowProbeWeightNote = 48u;  ///< C3 (kWeightNote)

constexpr double kRowProbeLifeRenderSeconds = 210.0;
constexpr double kRowProbeLifeEdgeWindowStartSeconds = 90.0;
constexpr std::size_t kRowProbeLifeRenderSamples =
    static_cast<std::size_t>(kRowProbeLifeRenderSeconds * kProbeSampleRate);  // 10 080 000
constexpr std::size_t kRowProbeLifeEdgeWindowStartSample =
    static_cast<std::size_t>(kRowProbeLifeEdgeWindowStartSeconds * kProbeSampleRate);

constexpr double kRowProbeHarmonicBandLow = 0.94;
constexpr double kRowProbeHarmonicBandHigh = 20.0;
constexpr float kRowProbeAwakeThreshold = 0.5f;
constexpr double kRowProbeDarknessBandEdge = 4.0;
constexpr double kRowProbeUnboundedImprovement = 1.0e9;

enum class RowProbeMetric : std::uint8_t {
    DarkRatioDb,
    HfEnergyDb,
    SourceCount,
    BandVariation,
    OctaveOffset,
    Flatness,
    CrestDb,
    LowEnergyDb,
    LifeEdgesPerMinute
};

enum class RowProbeEndpoint : std::uint8_t {
    PercentLower,
    PercentHigher,
    DbLower,
    DbHigher,
    RatioHigher
};

struct RowProbeAxis {
    VoragoMacro macro;
    const char* name;
    RowProbeMetric metric;
    RowProbeEndpoint kind;
    double threshold;
    std::uint8_t note;
};

/// SweepAxes' eleven rows (vorago_macro_test.cpp kSweepRows), thresholds
/// unchanged. Depth has no SweepAxes row.
constexpr std::array<RowProbeAxis, 11> kRowProbeAxes = {{
    {.macro = VoragoMacro::Darkness,
     .name = "Darkness  energy >4f0 rel. <4f0 (dB)",
     .metric = RowProbeMetric::DarkRatioDb,
     .kind = RowProbeEndpoint::DbLower,
     .threshold = 6.0,
     .note = kNoteC1},
    {.macro = VoragoMacro::Age,
     .name = "Age       HF energy > 4 kHz (dB) @C4",
     .metric = RowProbeMetric::HfEnergyDb,
     .kind = RowProbeEndpoint::DbLower,
     .threshold = 3.0,
     .note = kRowProbeAgeNote},
    {.macro = VoragoMacro::Density,
     .name = "Density   partials + awake sources",
     .metric = RowProbeMetric::SourceCount,
     .kind = RowProbeEndpoint::PercentHigher,
     .threshold = 0.50,
     .note = kNoteC1},
    {.macro = VoragoMacro::Movement,
     .name = "Movement  per-band total variation",
     .metric = RowProbeMetric::BandVariation,
     .kind = RowProbeEndpoint::PercentHigher,
     .threshold = 0.20,
     .note = kNoteC1},
    {.macro = VoragoMacro::Gravity,
     .name = "Gravity   mean |log2(ratio) - int|",
     .metric = RowProbeMetric::OctaveOffset,
     .kind = RowProbeEndpoint::PercentLower,
     .threshold = 0.30,
     .note = kNoteC1},
    {.macro = VoragoMacro::Entropy,
     .name = "Entropy   spectral flatness [f0, 20 f0]",
     .metric = RowProbeMetric::Flatness,
     .kind = RowProbeEndpoint::PercentHigher,
     .threshold = 0.25,
     .note = kNoteC1},
    {.macro = VoragoMacro::Pressure,
     .name = "Pressure  crest factor (dB)",
     .metric = RowProbeMetric::CrestDb,
     .kind = RowProbeEndpoint::DbLower,
     .threshold = 3.0,
     .note = kNoteC1},
    {.macro = VoragoMacro::Weight,
     .name = "Weight    energy < 80 Hz (dB) @C3",
     .metric = RowProbeMetric::LowEnergyDb,
     .kind = RowProbeEndpoint::DbHigher,
     .threshold = 6.0,
     .note = kRowProbeWeightNote},
    {.macro = VoragoMacro::Fog,
     .name = "Fog       spectral flatness [f0, 20 f0]",
     .metric = RowProbeMetric::Flatness,
     .kind = RowProbeEndpoint::PercentHigher,
     .threshold = 0.10,
     .note = kNoteC1},
    {.macro = VoragoMacro::Life,
     .name = "Life      wake/sleep edges per min [90 s, 210 s]",
     .metric = RowProbeMetric::LifeEdgesPerMinute,
     .kind = RowProbeEndpoint::RatioHigher,
     .threshold = 2.0,
     .note = kNoteC1},
    {.macro = VoragoMacro::Mass,
     .name = "Mass      energy < 80 Hz (dB) @C3",
     .metric = RowProbeMetric::LowEnergyDb,
     .kind = RowProbeEndpoint::DbHigher,
     .threshold = 0.25,
     .note = kRowProbeWeightNote},
}};

[[nodiscard]] double rowProbeRhoSign(RowProbeEndpoint k) noexcept {
    return (k == RowProbeEndpoint::PercentLower || k == RowProbeEndpoint::DbLower) ? -1.0 : 1.0;
}

[[nodiscard]] double rowProbeEndpointFigure(RowProbeEndpoint k, double v0, double v1) noexcept {
    switch (k) {
        case RowProbeEndpoint::PercentLower:
            return (v0 > 0.0) ? ((v0 - v1) / v0) : 0.0;
        case RowProbeEndpoint::PercentHigher:
            if (v0 > 0.0) {
                return (v1 - v0) / v0;
            }
            return (v1 > 0.0) ? kRowProbeUnboundedImprovement : 0.0;
        case RowProbeEndpoint::DbLower:
            return v0 - v1;
        case RowProbeEndpoint::DbHigher:
            return v1 - v0;
        case RowProbeEndpoint::RatioHigher:
            if (v0 > 0.0) {
                return v1 / v0;
            }
            return (v1 > 0.0) ? kRowProbeUnboundedImprovement : 0.0;
        default:
            return 0.0;
    }
}

[[nodiscard]] double rowProbeMidiToHz(std::uint8_t note) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

/// SweepAxes' harmonicBandFlatness (TU-local in vorago_macro_test.cpp),
/// reproduced on the fixture's own spectrum machinery.
[[nodiscard]] double rowProbeHarmonicBandFlatness(std::span<const float> x, double sr,
                                                  double noteHz) {
    if (x.empty() || !(sr > 0.0) || !(noteHz > 0.0)) {
        return 0.0;
    }
    const std::size_t fftSize = vfx::detail::analysisFftSize(x.size());
    const std::vector<double> power = vfx::detail::welchPowerSpectrum(x, fftSize);
    const double binHz = sr / static_cast<double>(fftSize);
    const double loHz = kRowProbeHarmonicBandLow * noteHz;
    const double hiHz = kRowProbeHarmonicBandHigh * noteHz;

    double logSum = 0.0;
    double linearSum = 0.0;
    std::size_t count = 0u;
    for (std::size_t k = 1; k < power.size(); ++k) {  // skip DC
        const double freq = static_cast<double>(k) * binHz;
        if (freq < loHz || freq > hiHz) {
            continue;
        }
        const double p = std::max(power[k], vfx::detail::kPowerFloor);
        logSum += std::log(p);
        linearSum += p;
        ++count;
    }
    if (count == 0u) {
        return 0.0;
    }
    const double geometricMean = std::exp(logSum / static_cast<double>(count));
    const double arithmeticMean = linearSum / static_cast<double>(count);
    return (arithmeticMean > 0.0) ? (geometricMean / arithmeticMean) : 0.0;
}

/// SweepAxes' Density figure: active partials plus awake noise sources.
[[nodiscard]] double rowProbeSourceCount(const VoragoVoice& voice) noexcept {
    double count = static_cast<double>(voice.cloud().getActivePartialCount());
    const std::size_t sources = voice.noise().getNumSources();
    for (std::size_t s = 0; s < sources; ++s) {
        if (voice.noise().getSourceWakeAmount(s) >= kRowProbeAwakeThreshold) {
            count += 1.0;
        }
    }
    return count;
}

// -- input --------------------------------------------------------------------

struct RowProbeEntry {
    VoragoMacro macro;
    ProbeRow row;  ///< target, delta amount, curve
};

struct RowProbeInput {
    std::vector<RowProbeEntry> entries;
    std::array<bool, VoragoMacroMatrix::kNumMacros> rowRequested{};
    std::string error;  ///< non-empty = the input is refused
};

/// The variable's value, or empty when unset (getenv_s under MSVC, as
/// preset_test_support.h sweepEnv does).
[[nodiscard]] std::string rowProbeEnv(const char* name) {
#ifdef _MSC_VER
    std::size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len == 0u) {
        return {};
    }
    std::string value(len, '\0');  // len counts the terminator
    if (getenv_s(&len, value.data(), value.size(), name) != 0) {
        return {};
    }
    value.resize((len > 0u) ? len - 1u : 0u);
    return value;
#else
    const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe) test thread only
    return (v != nullptr) ? std::string(v) : std::string{};
#endif
}

[[nodiscard]] std::string rowProbeTrim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return {};
    }
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1u);
}

/// Splits @p s on @p sep, trims each item, drops empty items.
[[nodiscard]] std::vector<std::string> rowProbeSplit(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t next = s.find(sep, pos);
        if (next == std::string::npos) {
            next = s.size();
        }
        std::string item = rowProbeTrim(s.substr(pos, next - pos));
        if (!item.empty()) {
            out.push_back(std::move(item));
        }
        pos = next + 1u;
    }
    return out;
}

[[nodiscard]] bool rowProbeParseMacro(const std::string& s, VoragoMacro& out) noexcept {
    for (std::size_t i = 0; i < kRowProbeMacroNames.size(); ++i) {
        if (s == kRowProbeMacroNames[i]) {
            out = static_cast<VoragoMacro>(i);
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool rowProbeParseTarget(const std::string& s, VoragoMacroTarget& out) noexcept {
    for (std::size_t i = 0; i < kRowProbeTargetNames.size(); ++i) {
        if (s == kRowProbeTargetNames[i]) {
            out = static_cast<VoragoMacroTarget>(i);
            return true;
        }
    }
    return false;
}

/// Linear | Exponential | SCurve only - the curves a row may carry (FR-065).
[[nodiscard]] bool rowProbeParseCurve(const std::string& s, ModCurve& out) noexcept {
    for (const ModCurve c : {ModCurve::Linear, ModCurve::Exponential, ModCurve::SCurve}) {
        if (s == rowProbeCurveName(c)) {
            out = c;
            return true;
        }
    }
    return false;
}

[[nodiscard]] RowProbeInput rowProbeParse(const std::string& entriesText,
                                          const std::string& rowsText) {
    RowProbeInput in;
    for (const std::string& item : rowProbeSplit(entriesText, ';')) {
        const std::vector<std::string> f = rowProbeSplit(item, ':');
        if (f.size() != 4u) {
            in.error = "VORAGO_ROWPROBE entry '" + item
                       + "' is not <macro>:<target>:<amount>:<curve>";
            return in;
        }
        RowProbeEntry e{VoragoMacro::Count, {VoragoMacroTarget::Count, 0.0f, ModCurve::Linear}};
        if (!rowProbeParseMacro(f[0], e.macro)) {
            in.error = "VORAGO_ROWPROBE: unknown macro '" + f[0] + "'";
            return in;
        }
        if (!rowProbeParseTarget(f[1], e.row.target)) {
            in.error = "VORAGO_ROWPROBE: unknown target '" + f[1] + "'";
            return in;
        }
        const char* begin = f[2].c_str();
        char* end = nullptr;
        const double amount = std::strtod(begin, &end);
        if (end == begin || *end != '\0' || !Krate::DSP::detail::isFinite(amount)) {
            in.error = "VORAGO_ROWPROBE: amount '" + f[2] + "' is not a finite number";
            return in;
        }
        e.row.amount = static_cast<float>(amount);
        if (!rowProbeParseCurve(f[3], e.row.curve)) {
            in.error = "VORAGO_ROWPROBE: unknown curve '" + f[3]
                       + "' (Linear | Exponential | SCurve)";
            return in;
        }
        in.entries.push_back(e);
        in.rowRequested[static_cast<std::size_t>(e.macro)] = true;
    }
    for (const std::string& name : rowProbeSplit(rowsText, ',')) {
        VoragoMacro m = VoragoMacro::Count;
        if (!rowProbeParseMacro(name, m)) {
            in.error = "VORAGO_ROWPROBE_ROWS: unknown macro '" + name + "'";
            return in;
        }
        in.rowRequested[static_cast<std::size_t>(m)] = true;
    }
    return in;
}

// -- one render ---------------------------------------------------------------

/// ONE SweepAxes render of @p axis at point @p x and @p seed, with every
/// candidate entry emulated through setTargetBase. Mirrors renderSweepPoint
/// (vorago_macro_test.cpp) for the metric the row reads.
[[nodiscard]] double renderRowProbePoint(const RowProbeAxis& axis,
                                         const std::vector<RowProbeEntry>& entries, float x,
                                         std::uint32_t seed) {
    const bool lifeRow = (axis.macro == VoragoMacro::Life);
    const std::size_t renderSamples = lifeRow ? kRowProbeLifeRenderSamples : kProbeTotalSamples;
    const std::size_t edgeWindowStartSample = lifeRow ? kRowProbeLifeEdgeWindowStartSample : 0u;

    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(kProbeSampleRate, cfg);
    engine->setSeed(seed);
    engine->setPolyphony(1u);
    applyFastAttack(*engine);
    engine->noteOn(axis.note, kProbeVelocity);

    // The row's macro at x, every other macro at its neutral (SweepAxes'
    // neutralExcept), so every shipped row applies.
    VoragoMacroMatrix matrix;
    matrix.setMacro(axis.macro, x);

    // Emulate the delta rows: base(t) + sum of their contributions, each at
    // its own macro's value in this render (a neutral macro contributes 0).
    // With no entry, no override is set and the matrix is SweepAxes' own.
    std::array<float, kNumTargets> delta{};
    std::array<bool, kNumTargets> touched{};
    for (const RowProbeEntry& e : entries) {
        const auto i = static_cast<std::size_t>(e.row.target);
        delta[i] += emulatedContribution(e.row, e.macro, matrix.getMacro(e.macro));
        touched[i] = true;
    }
    for (std::size_t i = 0; i < kNumTargets; ++i) {
        if (touched[i]) {
            const auto t = static_cast<VoragoMacroTarget>(i);
            matrix.setTargetBase(t, matrix.getTargetBase(t) + delta[i]);
        }
    }
    matrix.apply(*engine);

    std::vector<float> l(kProbeBlockSamples, 0.0f);
    std::vector<float> r(kProbeBlockSamples, 0.0f);
    std::vector<float> mono;
    mono.reserve(kProbeTotalSamples - kProbeWindowStartSample);

    std::array<bool, VoragoVoice::kNumEventSchedulers> wasActive{};
    double sourceAccum = 0.0;
    double offsetAccum = 0.0;
    std::size_t polls = 0u;
    std::size_t edges = 0u;

    for (std::size_t done = 0; done < renderSamples; done += kProbeBlockSamples) {
        const std::size_t n = std::min(kProbeBlockSamples, renderSamples - done);
        engine->processStereoBlock(l.data(), r.data(), n);
        engine->processOutputStage(l.data(), r.data(), n);

        const VoragoVoice& sounding = engine->getVoice(0);

        if (lifeRow) {
            for (std::size_t k = 0; k < VoragoVoice::kNumEventSchedulers; ++k) {
                const bool active = sounding.scheduler(k).isEventActive();
                const std::uint8_t family = sounding.scheduler(k).getActiveTarget();
                const bool inWindow = (done >= edgeWindowStartSample);
                if (active && !wasActive[k]) {
                    if (family < static_cast<std::uint8_t>(VoragoVoice::kNumEventFamilies)) {
                        edges += inWindow ? 1u : 0u;
                        wasActive[k] = true;
                    }
                } else if (!active && wasActive[k]) {
                    edges += inWindow ? 1u : 0u;
                    wasActive[k] = false;
                }
            }
        }

        if ((done + n) > kProbeWindowStartSample && done < kProbeTotalSamples) {
            const std::size_t from =
                (done >= kProbeWindowStartSample) ? 0u : (kProbeWindowStartSample - done);
            const std::size_t to = std::min(n, kProbeTotalSamples - done);
            for (std::size_t i = from; i < to; ++i) {
                mono.push_back(0.5f * (l[i] + r[i]));
            }
            if (axis.metric == RowProbeMetric::SourceCount) {
                sourceAccum += rowProbeSourceCount(sounding);
            } else if (axis.metric == RowProbeMetric::OctaveOffset) {
                offsetAccum += meanOctaveOffset(sounding);
            }
            ++polls;
        }
    }

    const std::span<const float> window(mono);
    const double noteHz = rowProbeMidiToHz(axis.note);
    switch (axis.metric) {
        case RowProbeMetric::DarkRatioDb:
            return bandEnergyDb(window, kProbeSampleRate, kRowProbeDarknessBandEdge * noteHz,
                                kProbeSampleRate * 0.5)
                   - bandEnergyDb(window, kProbeSampleRate, 20.0,
                                  kRowProbeDarknessBandEdge * noteHz);
        case RowProbeMetric::HfEnergyDb:
            return bandEnergyDb(window, kProbeSampleRate, 4000.0, kProbeSampleRate * 0.5);
        case RowProbeMetric::SourceCount:
            return (polls > 0u) ? (sourceAccum / static_cast<double>(polls)) : 0.0;
        case RowProbeMetric::BandVariation:
            return vfx::perBandTotalVariation(window, kProbeSampleRate);
        case RowProbeMetric::OctaveOffset:
            return (polls > 0u) ? (offsetAccum / static_cast<double>(polls)) : 0.0;
        case RowProbeMetric::Flatness:
            return rowProbeHarmonicBandFlatness(window, kProbeSampleRate, noteHz);
        case RowProbeMetric::CrestDb:
            return crestFactorDb(window);
        case RowProbeMetric::LowEnergyDb:
            return bandEnergyDb(window, kProbeSampleRate, 20.0, 80.0);
        case RowProbeMetric::LifeEdgesPerMinute:
        default: {
            const double edgeWindowSeconds =
                static_cast<double>(renderSamples - edgeWindowStartSample) / kProbeSampleRate;
            return (edgeWindowSeconds > 0.0)
                       ? (static_cast<double>(edges) * 60.0 / edgeWindowSeconds)
                       : 0.0;
        }
    }
}

}  // namespace

TEST_CASE("VoragoMacro_Phase13dRowProbe", "[.probe][vorago]") {
    const RowProbeInput input =
        rowProbeParse(rowProbeEnv("VORAGO_ROWPROBE"), rowProbeEnv("VORAGO_ROWPROBE_ROWS"));
    if (!input.error.empty()) {
        FAIL(input.error);
    }

    {
        const VoragoMacroMatrix fresh{};
        std::ostringstream os;
        os << std::fixed << std::setprecision(4)
           << "Phase 13d row-emulation probe (SweepAxes fixture: seeds 101/202/303 x points "
              "0/.25/.5/.75/1 x 60 s @ 48 kHz; window samples ["
           << kProbeWindowStartSample << ", " << kProbeTotalSamples
           << "); polyphony 1; fast attack; macros after noteOn; Life 210 s, edges over [90 s, "
              "210 s])\n"
           << "Delta entries (" << input.entries.size() << "):\n";
        for (const RowProbeEntry& e : input.entries) {
            os << "  " << rowProbeMacroName(e.macro) << " -> " << rowProbeTargetName(e.row.target)
               << "  delta=" << e.row.amount << " " << rowProbeCurveName(e.row.curve)
               << "  (shipped amount on this row=" << shippedAmount(e.macro, e.row.target)
               << ", target base=" << fresh.getTargetBase(e.row.target) << ")\n";
        }
        if (input.entries.empty()) {
            os << "  (none - the shipped kRows)\n";
        }
        if (input.rowRequested[static_cast<std::size_t>(VoragoMacro::Depth)]) {
            os << "  note: Depth has no SweepAxes row; nothing is rendered for it\n";
        }
        std::cout << os.str() << std::flush;
    }

    bool anyRow = false;
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(4)
            << "\n==== ROWPROBE SUMMARY (row | mean rho | mean endpoint | threshold | verdict) "
               "====\n";

    for (const RowProbeAxis& axis : kRowProbeAxes) {
        if (!input.rowRequested[static_cast<std::size_t>(axis.macro)]) {
            continue;
        }
        anyRow = true;

        std::array<std::array<double, 5>, 3> series{};
        std::array<double, 3> seedRho{};
        std::array<double, 3> seedEndpoint{};
        std::array<double, 5> metricSum{};
        double rhoSum = 0.0;

        for (std::size_t s = 0; s < kProbeSeeds.size(); ++s) {
            for (std::size_t p = 0; p < kProbePoints.size(); ++p) {
                series[s][p] = renderRowProbePoint(axis, input.entries,
                                                   static_cast<float>(kProbePoints[p]),
                                                   kProbeSeeds[s]);
                metricSum[p] += series[s][p];
            }
            seedRho[s] = spearmanRho(kProbePoints, series[s]);
            seedEndpoint[s] =
                rowProbeEndpointFigure(axis.kind, series[s].front(), series[s].back());
            rhoSum += seedRho[s];
        }

        std::array<double, 5> meanMetric{};
        for (std::size_t p = 0; p < meanMetric.size(); ++p) {
            meanMetric[p] = metricSum[p] / static_cast<double>(kProbeSeeds.size());
        }
        const double meanRho = rhoSum / static_cast<double>(kProbeSeeds.size());
        const double meanEndpoint =
            rowProbeEndpointFigure(axis.kind, meanMetric.front(), meanMetric.back());
        const bool rhoPass = (meanRho * rowProbeRhoSign(axis.kind)) >= 0.9;
        const bool endpointPass = meanEndpoint >= axis.threshold;
        const bool pass = rhoPass && endpointPass;

        std::ostringstream os;
        os << std::fixed << std::setprecision(4);
        os << "ROW " << axis.name << "\n";
        for (std::size_t s = 0; s < kProbeSeeds.size(); ++s) {
            os << "    seed " << kProbeSeeds[s] << " [0, .25, .5, .75, 1]: [";
            for (std::size_t p = 0; p < kProbePoints.size(); ++p) {
                os << ((p == 0u) ? "" : ", ") << series[s][p];
            }
            os << "]  rho=" << seedRho[s] << "  endpoint=" << seedEndpoint[s] << "\n";
        }
        os << "    mean metric [0, .25, .5, .75, 1]: [";
        for (std::size_t p = 0; p < meanMetric.size(); ++p) {
            os << ((p == 0u) ? "" : ", ") << meanMetric[p];
        }
        os << "]\n"
           << "    mean rho=" << meanRho << " (need "
           << ((rowProbeRhoSign(axis.kind) > 0.0) ? ">= 0.9" : "<= -0.9") << ", "
           << (rhoPass ? "ok" : "MISS") << ")  mean endpoint=" << meanEndpoint << " (need >= "
           << axis.threshold << ", " << (endpointPass ? "ok" : "MISS") << ")  -> "
           << (pass ? "PASS" : "FAIL") << "\n";
        std::cout << os.str() << std::flush;

        summary << "  " << std::left << std::setw(50) << axis.name << std::right
                << "  rho=" << std::setw(8) << meanRho << "  endpoint=" << std::setw(9)
                << meanEndpoint << "  threshold=" << axis.threshold << "  "
                << (pass ? "PASS" : "FAIL") << "\n";
    }

    if (!anyRow) {
        std::cout << "No SweepAxes row requested: set VORAGO_ROWPROBE=\"<macro>:<target>:"
                     "<amount>:<curve>[;...]\" and/or VORAGO_ROWPROBE_ROWS=\"<macro>[,...]\"\n"
                  << std::flush;
        return;
    }
    std::cout << summary.str() << std::flush;
}
