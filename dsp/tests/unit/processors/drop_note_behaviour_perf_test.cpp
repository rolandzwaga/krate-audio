// ==============================================================================
// Layer 2: Processor Tests ([.perf], shipping FP mode) - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/spec.md (SC-019, SC-025, E-4)
//            specs/profundum-phase4-drop-note-behaviour/plan.md (S5.4, S10.7, S11.1, S13 R-1)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T006 wires this TU; T018 fills it)
//
// Holds: SC-019, SC-025 cost [.perf]. NOT in the -fno-fast-math block, so the figures reflect the
// shipping FP mode. Hidden: run alone, nothing else executing, through
//   node tools/run-cpu-tests.js dsp_processors_tests
//
// Cases:
//   ProfundumNoteBehaviour_CpuBudget (SC-019, gated <= 0.05 % of one core): the per-voice Phase 4
//     composition at 48 kHz, host block 64, 60 s of audio, median of 5. Records ns/sample, the worst
//     single solve (us) and, recorded only, us/sample of an E-4 every-sample retrigger render.
//   DropPhaseLock_TriggerCost (SC-025, gated <= 2x): trigger + the solving process() at T = 4000 ms
//     against T = 1 ms (A = 48, s = 1, R = 1, bridged). The T = 1 ms arm is the S5.4 worst-case solve,
//     recorded in us.
//   DropPhaseLock_TriggerBurst (S5.4, gated <= 1.5x): 64 triggers at one offset + one process()
//     against one trigger + process(), on the S5.4 worst-case trigger.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>  // detail::isFinite
#include <krate/dsp/core/drop_curve.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Krate::DSP;

namespace {

using Kind = DropPhaseLock::TriggerKind;
using Clock = std::chrono::steady_clock;

constexpr double kFs = 48000.0;
constexpr std::size_t kBlock = 64;            // host block (SC-019)
constexpr double kBudgetSeconds = 60.0;       // audio per timed run (SC-019)
constexpr int kTimedRuns = 5;                 // median of 5, after one warm-up
constexpr std::size_t kEventBlocks = 75;      // 100 ms = 4800 samples = 75 blocks of 64
constexpr std::size_t kGlideBlocks = 750;     // 1 s
constexpr std::size_t kPitchGrid = DropPhaseLock::kPitchUpdateInterval;  // core pitch-update interval (C-14)
constexpr float kMinF0 = 8.0f;                // == ProfundumCore::kMinF0Hz (profundum_core.h:99; C-14)
constexpr float kCapHz = static_cast<float>(0.4 * kFs);
constexpr int kNoteLow = 28;                  // E1
constexpr int kNoteHigh = 52;                 // E3: the 24-semitone Quantized glide
constexpr int kVelocity = 100;
constexpr double kBudgetPct = 0.05;           // SC-019 bar; never relaxed (R-1)
constexpr double kTriggerCostRatio = 2.0;     // SC-025 bar
constexpr double kBurstRatio = 1.5;           // S5.4 bar
constexpr std::size_t kEverySampleSamples = 4800;  // E-4 render length (100 ms), recorded only
constexpr int kSolveReps = 500;               // solves per timed batch (trigger cases)
constexpr int kBurstTriggers = 64;

double secondsBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

/// One Profundum voice's Phase 4 note-behaviour components (SC-019 load).
struct Voice {
    DropEnvelope env;
    DropPhaseLock lock;
    NoteMemory memory;
    RetriggerDeclick declick;
    MonoHandler mono;
    float fRes = 0.0f;
    int note = kNoteLow;

    void prepare() {
        env.prepare(kFs);
        env.setAmount(24.0f);
        env.setTime(200.0f);
        env.setShape(1.0f);  // elastic anchor
        env.setRebound(1.0f);
        lock.prepare(kFs);
        declick.prepare(kFs);
        mono.prepare(kFs);
        mono.setGlideMode(GlideMode::Quantized);
        mono.setPortamentoTime(500.0f);
        fRes = mono.noteOn(note, kVelocity).frequency;
    }

    /// Restart the 24-semitone Quantized glide (staccato noteOff + noteOn; PortaMode::Always glides).
    void restartGlide() {
        (void)mono.noteOff(note);
        note = (note == kNoteLow) ? kNoteHigh : kNoteLow;
        fRes = mono.noteOn(note, kVelocity).frequency;
    }

    /// One retrigger event (every 100 ms): a NoteMemory resolve, the Drop trigger, the lock's
    /// trigger plan and the declick. Events alternate snap and bridged (SC-019 script).
    void event(bool snap, std::size_t gridPhase, double& sink) {
        const NoteMemory::Actions a =
            memory.resolve(snap ? NoteMemory::Transition::FromSilence : NoteMemory::Transition::Tail, !snap);
        sink += a.declick ? 1.0 : 0.0;
        env.trigger(snap);
        lock.trigger(env.plannedCurve(), fRes, gridPhase, kMinF0, kCapHz, snap ? Kind::SnapReset : Kind::Bridged);
        declick.arm();
    }
};

struct BudgetRun {
    double wallSeconds = 0.0;
    double worstSolveSeconds = 0.0;
};

/// 60 s of the SC-019 load in host blocks of 64. The first lock sample after each event (the
/// solving process()) is timed on its own for the worst-solve figure (2 clock reads per 4800
/// samples, inside the timed region).
BudgetRun budgetRun(Voice& v, std::size_t totalSamples, double& sink) {
    std::array<float, kBlock> d{};
    std::array<float, kBlock> dc{};
    std::array<float, kBlock> g{};
    std::array<float, kBlock> f{};
    BudgetRun result;
    std::size_t eventIndex = 0;

    const auto start = Clock::now();
    std::size_t block = 0;
    for (std::size_t pos = 0; pos < totalSamples; pos += kBlock, ++block) {
        const std::size_t n = std::min(kBlock, totalSamples - pos);
        if (block != 0 && block % kGlideBlocks == 0)
            v.restartGlide();
        const bool isEvent = (block % kEventBlocks) == 0;
        if (isEvent) {
            const bool snap = (eventIndex++ % 2) == 0;
            v.event(snap, (kPitchGrid - pos % kPitchGrid) % kPitchGrid, sink);
        }
        v.env.processBlock(d.data(), n);
        std::size_t first = 0;
        if (isEvent) {
            const auto t0 = Clock::now();
            dc[0] = v.lock.process(d[0]);
            const auto t1 = Clock::now();
            result.worstSolveSeconds = std::max(result.worstSolveSeconds, secondsBetween(t0, t1));
            first = 1;
        }
        v.lock.processBlock(d.data() + first, dc.data() + first, n - first);
        v.declick.processBlock(g.data(), n);
        // Stored, not summed: a running float sum would add a loop-carried dependency the
        // Voice does not have (harness bookkeeping, T023 item 6); one element per block feeds the sink.
        for (std::size_t k = 0; k < n; ++k)
            f[k] = v.mono.processPortamento();
        sink += static_cast<double>(dc[n - 1]) + static_cast<double>(g[n - 1]) + static_cast<double>(f[n - 1]);
    }
    const auto stop = Clock::now();
    result.wallSeconds = secondsBetween(start, stop);
    return result;
}

/// E-4 (recorded only): the composition with a bridged retrigger on every sample.
double everySampleRun(Voice& v, std::size_t samples, double& sink) {
    float acc = 0.0f;
    const auto start = Clock::now();
    for (std::size_t i = 0; i < samples; ++i) {
        v.event(false, (kPitchGrid - i % kPitchGrid) % kPitchGrid, sink);
        const float d = v.env.process();
        const float dc = v.lock.process(d);
        const float g = v.declick.process();
        acc += dc + g + v.mono.processPortamento();
    }
    const auto stop = Clock::now();
    sink += static_cast<double>(acc);
    return secondsBetween(start, stop);
}

/// The S5.4 worst-case trigger setup: a lock running a SnapReset plan of a T = 20 ms drop
/// (A = 48, s = 1, R = 1) stopped at mid-fall (480 samples, on the pitch grid), and the bridged curve
/// a retrigger at that sample plans with Time = newTimeMs. Each timed trigger starts from a copy
/// of `lock`, so every solve carries the same running correction.
struct PrimedTrigger {
    DropPhaseLock lock;
    DropCurve bridged{};
    float d0 = 0.0f;  // the bridged curve's first planned sample
    float fRes = 0.0f;
};

std::unique_ptr<PrimedTrigger> makePrimedTrigger(float newTimeMs) {
    auto p = std::make_unique<PrimedTrigger>();
    p->fRes = static_cast<float>(440.0 * std::exp2((static_cast<double>(kNoteLow) - 69.0) / 12.0));  // E1
    DropEnvelope env;
    env.prepare(kFs);
    env.setAmount(48.0f);
    env.setTime(20.0f);
    env.setShape(1.0f);
    env.setRebound(1.0f);
    env.trigger(true);
    p->lock.prepare(kFs);
    p->lock.trigger(env.plannedCurve(), p->fRes, 0, kMinF0, kCapHz, Kind::SnapReset);
    constexpr std::size_t kMidFall = 480;  // u = 0.5 of the 960-sample fall; a multiple of 16
    for (std::size_t i = 0; i < kMidFall; ++i)
        (void)p->lock.process(env.process());
    env.setTime(newTimeMs);
    env.trigger(false);
    p->bridged = env.plannedCurve();
    p->d0 = env.process();
    return p;
}

/// Mean seconds per (`triggers` x trigger + one solving process()) over kSolveReps, each timed on
/// its own after restoring the primed lock (the copy is outside the timed interval).
double solveBatchSeconds(const PrimedTrigger& p, DropPhaseLock& work, int triggers, double& sink) {
    double total = 0.0;
    for (int r = 0; r < kSolveReps; ++r) {
        work = p.lock;
        const auto t0 = Clock::now();
        for (int k = 0; k < triggers; ++k)
            work.trigger(p.bridged, p.fRes, 0, kMinF0, kCapHz, Kind::Bridged);
        const float y = work.process(p.d0);
        const auto t1 = Clock::now();
        total += secondsBetween(t0, t1);
        sink += static_cast<double>(y) + work.correctionGamma();
    }
    return total / static_cast<double>(kSolveReps);
}

/// Median of 5 batches (after one warm-up batch), in microseconds per solve.
double medianSolveMicros(const PrimedTrigger& p, int triggers, double& sink) {
    auto work = std::make_unique<DropPhaseLock>();
    (void)solveBatchSeconds(p, *work, triggers, sink);  // warm-up
    std::vector<double> batches;
    batches.reserve(static_cast<std::size_t>(kTimedRuns));
    for (int run = 0; run < kTimedRuns; ++run)
        batches.push_back(solveBatchSeconds(p, *work, triggers, sink));
    return medianOf(batches) * 1e6;
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-019: per-voice cost of the Phase 4 components <= 0.05 % of one core (plan S10.7, S11.1)
// ------------------------------------------------------------------------------
TEST_CASE("ProfundumNoteBehaviour_CpuBudget", "[processors][profundum][drop][.perf]") {
    const auto totalSamples = static_cast<std::size_t>(std::llround(kBudgetSeconds * kFs));
    double sink = 0.0;

    {
        auto warm = std::make_unique<Voice>();
        warm->prepare();
        (void)budgetRun(*warm, totalSamples, sink);
    }
    std::vector<double> walls;
    walls.reserve(static_cast<std::size_t>(kTimedRuns));
    double worstSolve = 0.0;
    for (int run = 0; run < kTimedRuns; ++run) {
        auto v = std::make_unique<Voice>();
        v->prepare();
        const BudgetRun r = budgetRun(*v, totalSamples, sink);
        walls.push_back(r.wallSeconds);
        worstSolve = std::max(worstSolve, r.worstSolveSeconds);
    }
    const double median = medianOf(walls);
    const double pct = median / kBudgetSeconds * 100.0;
    const double nsPerSample = median / static_cast<double>(totalSamples) * 1e9;
    const double worstSolveUs = worstSolve * 1e6;

    // E-4, recorded only (no bar): bridged retrigger on every sample.
    double e4UsPerSample = 0.0;
    {
        auto warm = std::make_unique<Voice>();
        warm->prepare();
        (void)everySampleRun(*warm, kEverySampleSamples, sink);
        std::vector<double> e4;
        e4.reserve(static_cast<std::size_t>(kTimedRuns));
        for (int run = 0; run < kTimedRuns; ++run) {
            auto v = std::make_unique<Voice>();
            v->prepare();
            e4.push_back(everySampleRun(*v, kEverySampleSamples, sink));
        }
        e4UsPerSample = medianOf(e4) / static_cast<double>(kEverySampleSamples) * 1e6;
    }

    std::printf("PROFUNDUM_DROP_PERF SC-019 pct=%.4f ns_per_sample=%.3f worst_solve_us=%.3f e4_us_per_sample=%.3f\n",
                pct, nsPerSample, worstSolveUs, e4UsPerSample);
    INFO("SC-019 median " << pct << " % of one core, " << nsPerSample << " ns/sample; worst single solve "
                          << worstSolveUs << " us; E-4 every-sample retrigger " << e4UsPerSample
                          << " us/sample (recorded)");
    REQUIRE(detail::isFinite(sink));  // keeps the render from being elided
    REQUIRE(pct <= kBudgetPct);
}

// ------------------------------------------------------------------------------
// SC-025: the trigger cost does not grow with Time (no per-sample dry run)
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_TriggerCost", "[processors][profundum][drop][.perf]") {
    double sink = 0.0;
    const auto shortT = makePrimedTrigger(1.0f);
    const auto longT = makePrimedTrigger(4000.0f);
    REQUIRE(shortT->bridged.fallStart > 0.0);  // bridged plans (brake + rise), not snaps
    REQUIRE(longT->bridged.fallStart > 0.0);

    const double us1 = medianSolveMicros(*shortT, 1, sink);
    const double us4000 = medianSolveMicros(*longT, 1, sink);

    std::printf("PROFUNDUM_DROP_PERF SC-025 T1_us=%.3f T4000_us=%.3f ratio=%.3f (S5.4 worst-case solve = T1_us)\n",
                us1, us4000, us4000 / us1);
    INFO("trigger + solving process(): T = 1 ms " << us1 << " us (S5.4 worst case, recorded), T = 4000 ms "
                                                  << us4000 << " us, ratio " << us4000 / us1);
    REQUIRE(detail::isFinite(sink));
    REQUIRE(us4000 <= kTriggerCostRatio * us1);
}

// ------------------------------------------------------------------------------
// S5.4: N triggers at one offset cost one solve (the lazy solve)
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_TriggerBurst", "[processors][profundum][drop][.perf]") {
    double sink = 0.0;
    const auto p = makePrimedTrigger(1.0f);  // the S5.4 worst-case trigger

    const double single = medianSolveMicros(*p, 1, sink);
    const double burst = medianSolveMicros(*p, kBurstTriggers, sink);

    std::printf("PROFUNDUM_DROP_PERF S5.4 burst single_us=%.3f burst64_us=%.3f ratio=%.3f\n", single, burst,
                burst / single);
    INFO("one trigger + process() " << single << " us; 64 triggers + process() " << burst << " us; ratio "
                                    << burst / single);
    REQUIRE(detail::isFinite(sink));
    REQUIRE(burst <= kBurstRatio * single);
}
