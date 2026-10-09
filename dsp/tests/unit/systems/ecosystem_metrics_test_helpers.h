// ==============================================================================
// ecosystem_metrics_test_helpers.h
// Shared metric code for the Vorago Phase 8 EcosystemEngine test TUs
// ==============================================================================
// Spec:  specs/vorago-phase8-ecosystem/spec.md   (SC-002, SC-003, SC-004, SC-005,
//        SC-013, SC-017, SC-018)
// Plan:  specs/vorago-phase8-ecosystem/plan.md   S10.1 (TU assignment),
//        S10.2 (this file, function by function), S14 D-D / D-E (the deviations)
// Tasks: specs/vorago-phase8-ecosystem/tasks.md  T017
//
// WHY THIS FILE EXISTS (plan S14 D-D). FR-091 enumerates four new test TUs and
// nothing else. The verdict function, the activity / frozen metrics, the
// pairwise-correlation metric and the recurrence cycle scan are needed by BOTH
// the behaviour TU (SC-002, SC-004) and the longrun TU (SC-003, SC-005, SC-013,
// SC-017, SC-018). Duplicating ~150 lines of statistics across two TUs is how
// two copies drift apart and a criterion quietly stops measuring what it claims.
//
// NO CMAKE ENTRY. `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` source list is
// enumerated and names `.cpp` only; a test-local header living beside its TUs is
// house-legal, with two precedents in this tree:
//   dsp/tests/unit/processors/arpeggiator_core_test_helpers.h
//   dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h
//
// EVERYTHING IS IN `double` (plan S14 D-E). `tests/test_helpers/statistical_utils.h`
// carries `computeMean` (:41), `computeVariance` (:59) and `computeStdDev` (:76),
// and all three are `float`-ONLY. A `float` std/mean over 1800 samples of a
// 0.03-magnitude signal loses exactly the discrimination SC-002's 0.30 activity
// gate and SC-004's 0.35 correlation gate need, so that helper is read here and
// DELIBERATELY NOT CONSUMED. Every statistic below is a `double` transcription of
// the prototype (`specs/vorago-phase8-ecosystem/prototype/ecosystem-sim.js`) at
// the line cited on it.
//
// Free functions are `inline` (not `static`) so a TU that includes this header
// but uses only part of it does not trip C4505 / -Wunused-function under /W4.
// Test-side heap (`std::vector`) is legal: SC-007 gates the ENGINE's real-time
// safety, measured with an `AllocationScope` around engine calls, never around a
// trace buffer.
// ==============================================================================
#pragma once

#include <krate/dsp/systems/ecosystem_engine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace Krate::DSP::TestUtils::Eco {

// ------------------------------------------------------------------------------
// Thresholds, transcribed from ecosystem-sim.js:570-574 (the round-2 constants)
// ------------------------------------------------------------------------------
/// @brief Late-window length, in seconds (`kLateWindowSeconds`, :570).
inline constexpr double kLateWindowSeconds = 600.0;
/// @brief An agent is FROZEN below this std/grand-mean ratio (`kFrozenActivity`, :571).
inline constexpr double kFrozenActivity = 0.02;
/// @brief A run is ALIVE at or above this mean activity (`kAliveActivity`, :572).
inline constexpr double kAliveActivity = 0.10;
/// @brief ...and with no more than this fraction frozen (`kAliveMaxFrozenFraction`, :573).
inline constexpr double kAliveMaxFrozenFraction = 0.25;
/// @brief A post-decorrelation autocorrelation above this is a cycle (`kCycleAutocorr`, :574).
inline constexpr double kCycleAutocorr = 0.8;
/// @brief The autocorrelation level a series must first fall BELOW to count as
///        having forgotten itself (`ecosystem-sim.js:740`).
inline constexpr double kDecorrelationLevel = 0.2;
/// @brief Floor on the grand mean, so a dead population divides by a small number
///        instead of zero (`ecosystem-sim.js:678` / `:707`, the JS `|| 1e-12`).
inline constexpr double kGrandMeanFloor = 1.0e-12;

// ------------------------------------------------------------------------------
// Trace: one sampled run (plan S10.2)
// ------------------------------------------------------------------------------
/// @brief Agent energies and published outputs sampled on a fixed slow grid.
///
/// Row-major by SAMPLE: `energy[sample][agent]`, matching the prototype's
/// `agentSeries` (`ecosystem-sim.js:645`). `sampleHz` is the grid the trace was
/// ACTUALLY taken on, not the one that was requested - the control step is
/// `stepIntervalChunks * 64 / sampleRate` seconds and a whole number of steps per
/// sample rarely lands on exactly 1 Hz (at the defaults it is 94 steps = 0.99734 Hz,
/// the same 93.75/94 the prototype carries at `:697`). `lateWindow()` sizes its
/// window from this field, so the window means the same duration either way.
struct Trace {
    std::size_t agents = 0;
    std::vector<std::vector<double>> energy;  ///< [sample][agent] - the conserved quantity
    std::vector<std::vector<double>> output;  ///< [sample][agent] - FR-061's published value
    double sampleHz = 1.0;                    ///< the grid actually used
};

/// @brief The late-window statistics of one trace (plan S10.2).
///
/// `lateActivity`, `lateFrozen` and `latePairCorr` are computed on ENERGY - the
/// conserved quantity every rule acts on. `cycle` is computed on OUTPUT, per
/// Clarification Q3: the one place the C++ deliberately differs from
/// `ecosystem-sim.js:576-583`, whose cycle clause ran on `hSeries` (the entropy
/// series), a statistic FR-066 records as permutation-invariant and non-gating.
struct Liveness {
    double lateActivity = 0.0;   ///< mean over agents of std(e_i)/grandMean
    std::size_t lateFrozen = 0;  ///< count of agents with std(e_i)/grandMean < kFrozenActivity
    double latePairCorr = 1.0;   ///< mean over i<j of |pearson(e_i, e_j)|
    bool cycle = false;          ///< any agent's OUTPUT series has a short cycle
};

// ------------------------------------------------------------------------------
// Scalar statistics - ecosystem-sim.js:585-614
// ------------------------------------------------------------------------------

/// @brief Arithmetic mean (`ecosystem-sim.js:585`). Empty series -> 0.
[[nodiscard]] inline double meanD(std::span<const double> series) noexcept {
    if (series.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double v : series) {
        sum += v;
    }
    return sum / static_cast<double>(series.size());
}

/// @brief Sum of squared deviations from the mean - the shared kernel of
///        `stdDevD` and `autocorrD` (`ecosystem-sim.js:586-589`, `:610`).
[[nodiscard]] inline double sumSquaredDeviationsD(std::span<const double> series) noexcept {
    const double m = meanD(series);
    double acc = 0.0;
    for (const double v : series) {
        const double d = v - m;
        acc += d * d;
    }
    return acc;
}

/// @brief POPULATION standard deviation, divisor `n` (`ecosystem-sim.js:586-589`).
///
/// The prototype divides by `n`, not `n - 1`, and SC-002's 0.30 activity gate was
/// measured against that estimator; switching to the sample form would move every
/// published figure by a factor the criterion cannot absorb.
[[nodiscard]] inline double stdDevD(std::span<const double> series) noexcept {
    if (series.empty()) {
        return 0.0;
    }
    return std::sqrt(sumSquaredDeviationsD(series) / static_cast<double>(series.size()));
}

/// @brief Pearson correlation of two equal-length series (`ecosystem-sim.js:593-604`).
///
/// Returns 0 when fewer than two paired samples exist, or when either series is
/// constant (a zero denominator) - the prototype's `da > 0 && db > 0` guard. That
/// 0 is "no linear relationship measurable", and SC-004 averages |corr| over pairs,
/// so an unmeasurable pair lowers the mean rather than poisoning it with a NaN.
[[nodiscard]] inline double pearsonD(std::span<const double> a,
                                     std::span<const double> b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2u) {
        return 0.0;
    }
    const std::span<const double> as = a.first(n);
    const std::span<const double> bs = b.first(n);
    const double ma = meanD(as);
    const double mb = meanD(bs);
    double num = 0.0;
    double da = 0.0;
    double db = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double xa = as[i] - ma;
        const double xb = bs[i] - mb;
        num += xa * xb;
        da += xa * xa;
        db += xb * xb;
    }
    return (da > 0.0 && db > 0.0) ? num / std::sqrt(da * db) : 0.0;
}

/// @brief Normalised autocorrelation at `lag` (`ecosystem-sim.js:606-614`).
///
/// TRANSCRIBED ASYMMETRY, NOT A BUG: the denominator runs over the WHOLE series
/// while the numerator runs over the `n - lag` overlapping pairs, so the value
/// decays with lag even for a perfectly periodic signal (a period-60 sine over
/// 1800 samples peaks at 0.9667, not 1.0, at lag 60 - measured). Every threshold
/// below - `kDecorrelationLevel` and `kCycleAutocorr` - was measured on this
/// estimator, so the asymmetry is part of the criterion.
[[nodiscard]] inline double autocorrD(std::span<const double> series,
                                      std::size_t lag) noexcept {
    const std::size_t n = series.size();
    if (lag >= n) {
        return 0.0;
    }
    const std::size_t count = n - lag;
    if (count <= 1u) {
        return 0.0;
    }
    const double m = meanD(series);
    const double den = sumSquaredDeviationsD(series);
    if (!(den > 0.0)) {
        return 0.0;
    }
    double num = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        num += (series[i] - m) * (series[i + lag] - m);
    }
    return num / den;
}

// ------------------------------------------------------------------------------
// runTrace - drive the engine and sample it (plan S10.2)
// ------------------------------------------------------------------------------
/// @brief Advance a PREPARED engine for `seconds` and sample it at ~`sampleHz`.
///
/// The engine is advanced in whole control steps: one sample point costs
/// `stepsPerSample = max(1, round(1 / (sampleHz * stepDuration)))` steps, delivered
/// as a single `processChunk(stepIntervalChunks * 64 * stepsPerSample)` call. That
/// is exact, not approximate: `processChunk`'s two residues live across calls, so a
/// whole-step multiple always fires exactly that many steps and never leaves a
/// partial chunk behind to shift the next sample (`ecosystem_engine.h:400-412`).
///
/// An UNPREPARED engine yields an empty trace rather than a division by zero -
/// `getStepDurationSeconds()` is 0.0 until `prepare()` runs
/// (`ecosystem_engine.h:2327`).
[[nodiscard]] inline Trace runTrace(EcosystemEngine& engine, double seconds,
                                    double sampleHz = 1.0) {
    Trace trace;
    trace.agents = engine.getAgentCount();
    const double stepSeconds = engine.getStepDurationSeconds();
    if (!engine.isPrepared() || !(stepSeconds > 0.0) || !(sampleHz > 0.0) ||
        !(seconds > 0.0) || trace.agents == 0u) {
        return trace;
    }

    const double stepsPerSampleReal = 1.0 / (sampleHz * stepSeconds);
    const long long stepsPerSampleRounded = std::llround(stepsPerSampleReal);
    const std::size_t stepsPerSample =
        (stepsPerSampleRounded < 1) ? std::size_t{1}
                                    : static_cast<std::size_t>(stepsPerSampleRounded);

    trace.sampleHz = 1.0 / (static_cast<double>(stepsPerSample) * stepSeconds);

    const long long sampleCountRounded = std::llround(seconds * trace.sampleHz);
    const std::size_t sampleCount =
        (sampleCountRounded < 1) ? std::size_t{1}
                                 : static_cast<std::size_t>(sampleCountRounded);

    const std::size_t samplesPerAdvance =
        engine.getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples * stepsPerSample;

    trace.energy.reserve(sampleCount);
    trace.output.reserve(sampleCount);

    std::vector<double> energyRow(trace.agents, 0.0);
    std::vector<double> outputRow(trace.agents, 0.0);
    for (std::size_t s = 0; s < sampleCount; ++s) {
        engine.processChunk(samplesPerAdvance);
        for (std::size_t i = 0; i < trace.agents; ++i) {
            energyRow[i] = engine.getAgentEnergy(i);
            outputRow[i] = static_cast<double>(engine.getAgentOutput(i));
        }
        trace.energy.push_back(energyRow);
        trace.output.push_back(outputRow);
    }
    return trace;
}

// ------------------------------------------------------------------------------
// hasShortCycle - the RECURRENCE scan (ecosystem-sim.js:724-757, SC-005)
// ------------------------------------------------------------------------------
/// @brief True when `series` repeats itself on a short period.
///
/// THE NAIVE FORM IS FORBIDDEN BY NAME (spec SC-005, `ecosystem-sim.js:730-737`):
/// "the maximum autocorrelation over all lags >= 10 s" is NOT a cycle test. For any
/// smooth signal that maximum always sits at the SHORTEST lag scanned, so the first
/// version of this metric reported "limit cycle: 0.909 at lag 9.0 s" for a 30-minute
/// run whose scan merely started at 9 s - it was measuring smoothness, not
/// periodicity. A genuine cycle is a RECURRENCE: the autocorrelation must first
/// DECAY (the signal forgets itself), and only a peak AFTER that decay is evidence
/// of a repeat.
///
/// The scan, in order:
///   1. GUARDED ESCAPE. A series whose sample variance over the window is exactly
///      zero returns TRUE (cycle) outright, rather than falling through to step 3's
///      escape. A railed constant output never decorrelates either, and without this
///      clause the criterion would award the most degenerate possible run a pass
///      (tasks.md T017).
///   2. Scan `lag = 1 ...` until the autocorrelation first falls below
///      `kDecorrelationLevel` -> `decorrLag`.
///   3. If no such lag exists within `len / 2`, there is NO cycle: the series is one
///      slow trend, which is the opposite of a short limit cycle.
///   4. Otherwise the maximum autocorrelation over `[decorrLag, len/2)` must be
///      `<= kCycleAutocorr`; above it, that is the recurrence.
///
/// ZERO VARIANCE IS TESTED AS "EVERY SAMPLE EQUALS THE FIRST", NOT AS
/// `sumSquaredDeviationsD(series) == 0.0`, and the difference is load-bearing, not
/// taste. The mean of 1800 copies of a value that is not exactly representable in
/// binary is not that value: measured, a constant series of 0.42 leaves a sum of
/// squared deviations of 1.68e-25, which is NOT 0.0, so the sum-of-squares form
/// falls THROUGH the guard, never decorrelates, and returns "no cycle" - the exact
/// pass this clause exists to deny. All-samples-equal is what "sample variance
/// exactly zero" means, and it is exact for every representable value.
///
/// FALSIFICATION (tasks.md T017, measured before this function was written, with a
/// Node transcription of exactly this algorithm):
///   sine, period 60 s, 1800 samples at 1 Hz -> TRUE   (decorrLag 14, peak 0.9667 at lag 60)
///   monotone ramp, 1800 samples             -> FALSE  (decorrLag 507, peak 0.1997)
///   constant 0.42, 1800 samples             -> TRUE   (guarded escape)
///   constant 1.0 (railed), 1800 samples     -> TRUE   (guarded escape)
[[nodiscard]] inline bool hasShortCycle(std::span<const double> series) noexcept {
    // (1) the guarded escape - see the paragraph above on why this is not a
    //     sum-of-squared-deviations test. An empty series is vacuously constant
    //     and is likewise denied a pass.
    if (series.empty()) {
        return true;
    }
    const double firstSample = series.front();
    const bool allEqual = std::all_of(series.begin(), series.end(),
                                      [firstSample](double v) { return v == firstSample; });
    if (allEqual) {
        return true;
    }

    const std::size_t maxLag = series.size() / 2u;

    // (2) first lag at which the series has forgotten itself.
    std::size_t decorrLag = 0u;  // 0 == "none found"; lag 0 is never scanned
    for (std::size_t lag = 1u; lag < maxLag; ++lag) {
        if (autocorrD(series, lag) < kDecorrelationLevel) {
            decorrLag = lag;
            break;
        }
    }

    // (3) never decorrelated within half the series: one slow trend, not a cycle.
    if (decorrLag == 0u) {
        return false;
    }

    // (4) the recurrence peak AFTER the decay.
    double worst = 0.0;
    for (std::size_t lag = decorrLag; lag < maxLag; ++lag) {
        worst = std::max(worst, autocorrD(series, lag));
    }
    return worst > kCycleAutocorr;
}

// ------------------------------------------------------------------------------
// meanOutputSeries - the POPULATION-MEAN published output, the cycle clause's series
// ------------------------------------------------------------------------------
/// @brief Mean over agents of `getAgentOutput` at every sample from @p first on.
///
/// The recurrence scan (SC-005, and SC-002's verdict clause (iii)) runs on THIS
/// series - user ruling of 2026-09-16, T023. Scanned per agent it flagged FR-050's
/// DESIGNED oscillation: each agent's appetite is phase-gated at its own intrinsic
/// frequency (periods 56-667 s at the defaults), so its output decorrelates at a
/// quarter period and recurs at one period - 3 of 32 agents above 0.8 at the
/// defaults (worst 0.848 at 109 s), 127 of 500 sane-box configurations dead on that
/// clause alone, with activity and pairwise decorrelation healthy in every one of
/// them. An ecosystem limit cycle - the population locked into a common boom/bust,
/// the thing the roadmap forbids - shows in the population mean; independent
/// intrinsic oscillations average out of it. Still output, never energy and never
/// entropy (FR-066, D-9).
[[nodiscard]] inline std::vector<double> meanOutputSeries(const Trace& trace,
                                                          std::size_t first = 0u) {
    std::vector<double> out;
    if (trace.agents == 0u || first >= trace.output.size()) {
        return out;
    }
    out.reserve(trace.output.size() - first);
    for (std::size_t s = first; s < trace.output.size(); ++s) {
        const std::vector<double>& row = trace.output[s];
        const std::size_t width = std::min(trace.agents, row.size());
        double sum = 0.0;
        for (std::size_t i = 0; i < width; ++i) {
            sum += row[i];
        }
        out.push_back(sum / static_cast<double>(trace.agents));
    }
    return out;
}

// ------------------------------------------------------------------------------
// lateWindow - fixed-window liveness (ecosystem-sim.js:688-716)
// ------------------------------------------------------------------------------
/// @brief Activity, frozen count, pairwise correlation and the cycle flag over the
///        last `windowSeconds` of a trace.
///
/// WHY A FIXED WINDOW (`ecosystem-sim.js:691-697`): whole-run statistics scale with
/// run length for signals this slow, and the entropy late-quarter std once used as
/// the verdict flipped between 1200 s ("frozen") and 1800 s ("still moving") on the
/// SAME configuration. Everything here is computed over the last `windowSeconds` on
/// the trace's own grid, so the number means the same thing at any run length at or
/// above the window.
///
/// `grandMean` is the mean over agents of each agent's window mean, floored at
/// `kGrandMeanFloor` (the prototype's `|| 1e-12` at `:707`), so a dead population
/// divides by a small number instead of by zero.
[[nodiscard]] inline Liveness lateWindow(const Trace& trace,
                                         double windowSeconds = kLateWindowSeconds) {
    Liveness out;  // lateActivity 0, lateFrozen 0, latePairCorr 1, cycle false
    const std::size_t samples = trace.energy.size();
    if (trace.agents == 0u || samples < 2u) {
        return out;  // matches the prototype's `if (agentSeries.length > 1)` guard
    }

    // lateCount = min(samples, max(2, round(window * sampleHz)))  -  :704-705
    const long long wanted = std::llround(windowSeconds * trace.sampleHz);
    const std::size_t requested =
        (wanted < 2) ? std::size_t{2} : static_cast<std::size_t>(wanted);
    const std::size_t lateCount = std::min(samples, requested);
    const std::size_t first = samples - lateCount;

    // Columns: one series per agent over the window.
    std::vector<std::vector<double>> cols(trace.agents, std::vector<double>(lateCount, 0.0));
    for (std::size_t s = 0; s < lateCount; ++s) {
        const std::vector<double>& row = trace.energy[first + s];
        const std::size_t width = std::min(trace.agents, row.size());
        for (std::size_t i = 0; i < width; ++i) {
            cols[i][s] = row[i];
        }
    }

    double grandMean = 0.0;
    for (const std::vector<double>& c : cols) {
        grandMean += meanD(c);
    }
    grandMean /= static_cast<double>(trace.agents);
    if (!(grandMean > kGrandMeanFloor)) {
        grandMean = kGrandMeanFloor;  // the prototype's `|| 1e-12`, and NaN-safe
    }

    double activity = 0.0;
    for (const std::vector<double>& c : cols) {
        const double s = stdDevD(c) / grandMean;
        activity += s;
        if (s < kFrozenActivity) {
            ++out.lateFrozen;
        }
    }
    out.lateActivity = activity / static_cast<double>(trace.agents);

    double corrSum = 0.0;
    std::size_t corrCount = 0u;
    for (std::size_t i = 0; i < trace.agents; ++i) {
        for (std::size_t j = i + 1u; j < trace.agents; ++j) {
            corrSum += std::abs(pearsonD(cols[i], cols[j]));
            ++corrCount;
        }
    }
    out.latePairCorr = (corrCount > 0u) ? corrSum / static_cast<double>(corrCount) : 1.0;

    // The cycle clause runs on the POPULATION-MEAN OUTPUT (ruling 2026-09-16,
    // see meanOutputSeries), never on energy and never on entropy (Clarification
    // Q3, D-9). Only the same window is scanned, so the flag means the same
    // thing as the three statistics beside it.
    if (trace.output.size() == samples) {
        out.cycle = hasShortCycle(meanOutputSeries(trace, first));
    }

    return out;
}

// ------------------------------------------------------------------------------
// verdictAlive - SC-002's VERDICT FUNCTION, which is NOT its defaults gate
// ------------------------------------------------------------------------------
/// @brief Classify a run of >= 900 s as alive over its last 600 s.
///
/// Alive iff ALL THREE hold (`ecosystem-sim.js:576-583`, amended by Clarification Q3):
///   (i)   `lateActivity >= kAliveActivity` (0.10);
///   (ii)  `lateFrozen <= kAliveMaxFrozenFraction * agents` (25 %);
///   (iii) the POPULATION-MEAN output series has no short cycle (meanOutputSeries;
///         ruling 2026-09-16 - per agent, the clause flagged FR-050's designed
///         intrinsic oscillation).
///
/// Clause (iii) is the one place the C++ deliberately differs from the prototype,
/// whose cycle clause ran on `hSeries` - the entropy series. FR-066 records entropy
/// as permutation-invariant and explicitly NON-GATING: it cannot see WHICH agent
/// holds the energy, so energy sloshing between agents in a fixed pattern holds it
/// exactly constant while every published output swings. The output is the only
/// value a consumer ever sees, so it is the series the cycle clause must read.
///
/// THIS IS NOT SC-002's DEFAULTS GATE. The defaults gate is stricter by design
/// (activity >= 0.30, frozen == 0 exactly); this function is the looser classifier
/// the fuzz batches (SC-001, SC-003, SC-013) use to label a configuration.
[[nodiscard]] inline bool verdictAlive(const Trace& trace) {
    if (trace.agents == 0u || trace.energy.size() < 2u) {
        return false;
    }
    const Liveness live = lateWindow(trace, kLateWindowSeconds);
    const bool activeEnough = live.lateActivity >= kAliveActivity;
    const bool fewEnoughFrozen = static_cast<double>(live.lateFrozen) <=
                                 kAliveMaxFrozenFraction * static_cast<double>(trace.agents);
    return activeEnough && fewEnoughFrozen && !live.cycle;
}

// ------------------------------------------------------------------------------
// distinctPositions - SC-002 (c)'s spatial-collapse metric
// ------------------------------------------------------------------------------
/// @brief Number of distinct agent positions, both axes rounded to `dp` decimals.
///
/// FR-032's gate. Without crowding repulsion the 32 agents occupied SIX distinct
/// positions to two decimals after 30 minutes (`FINDINGS.md:210`); with it, 31 of
/// 32. SC-002 (a) and SC-004 provably cannot discriminate that rule - the ablation
/// prices its removal at -2 % activity and -2 % correlation, "within noise in 2-D" -
/// so a positional metric is the only thing that can.
///
/// The rounded key is folded modulo the grid: the habitat is a TORUS (FR-012), so
/// x = 0.999 and x = 0.001 are neighbours, not opposite ends, and both must land in
/// the same cell or two adjacent agents would be counted as distinct purely because
/// the wrap fell between them.
[[nodiscard]] inline std::size_t distinctPositions(const EcosystemEngine& engine, int dp = 2) {
    const std::size_t agents = engine.getAgentCount();
    if (agents == 0u) {
        return 0u;
    }
    const int decimals = std::clamp(dp, 0, 9);
    long long modulus = 1;
    for (int d = 0; d < decimals; ++d) {
        modulus *= 10;
    }
    const double scale = static_cast<double>(modulus);

    const auto key = [scale, modulus](double coordinate) noexcept -> long long {
        long long k = std::llround(coordinate * scale);
        k %= modulus;
        if (k < 0) {
            k += modulus;
        }
        return k;
    };

    std::vector<std::pair<long long, long long>> cells;
    cells.reserve(agents);
    for (std::size_t i = 0; i < agents; ++i) {
        cells.emplace_back(key(engine.getAgentPositionX(i)), key(engine.getAgentPositionY(i)));
    }
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    return cells.size();
}

}  // namespace Krate::DSP::TestUtils::Eco
