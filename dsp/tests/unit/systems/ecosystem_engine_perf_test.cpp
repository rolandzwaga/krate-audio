// ==============================================================================
// Layer 3: System Tests - EcosystemEngine CPU budget (SC-011) and the FR-085
//          stage cost probe            (specs/vorago-phase8-ecosystem)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase8-ecosystem/spec.md   (FR-085's 53 333 ns/block
//                                                     ceiling, FR-086's pair and
//                                                     cell-visit counts, FR-072's
//                                                     dormancy prediction,
//                                                     SC-011 (a) + (b))
//            specs/vorago-phase8-ecosystem/plan.md   (S10.1 TU assignment,
//                                                     S12.1 measurement basis,
//                                                     S12.2 the projection,
//                                                     S12.3 the lever list)
//            specs/vorago-phase8-ecosystem/tasks.md  (T001 creates this TU;
//                                                     T015 adds both cases below;
//                                                     T016 reads the table)
//
// SCOPE OF THIS TU: hidden, run-on-demand cases only. That is exactly
//   EcosystemEngine_CpuBudget      - tagged "[.perf]"   (SC-011 (a) and (b))
//   EcosystemEngine_StageCostProbe - tagged "[.perf]"   (the non-gating table)
// and nothing else. Neither is ever run by the default suite or by CI. This TU
// deliberately stays OUT of the -fno-fast-math block (dsp/tests/CMakeLists.txt):
// that flag would change the figures it reports. Finiteness is therefore tested
// with detail::isFinite and NEVER with std::isnan/isinf/isfinite, which fold to
// constants under /fp:fast and -ffast-math (core/db_utils.h:118-129,
// tools/lint-nonfinite-symbols.js).
//
// -----------------------------------------------------------------------------
// MEASUREMENT BASIS - INHERITED VERBATIM (plan S12.1, spec.md:214,
// resonance_drift_network_perf_test.cpp:66-88)
// -----------------------------------------------------------------------------
// WHY ns/block AND NOT "% of one core". A percent-of-core figure is not
// reproducible across dev machines or CI runners. The measurement basis is
// NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz, the basis established by
// harmonic_cloud_perf_test.cpp and reused by continuous_body_perf_test.cpp,
// atmosphere_engine_perf_test.cpp:22-70, noise_organism_perf_test.cpp:201-272 and
// resonance_drift_network_perf_test.cpp:66-71. One block period is
// 10 666 667 ns, so FR-085's 0.5 % ceiling is 53 333 ns/block. The percent figure
// is REPORTED, never asserted.
//
// TRIAL SHAPE (tasks.md T015, plan S12.1): best-of-25 x 500 blocks after 400
// warm-up blocks, the atmosphere_engine_perf_test.cpp idiom. Many short trials,
// because the dev machine is a hybrid part and the dominant noise source is a
// whole trial migrating onto an E-core. Affinity pinning was tried and REJECTED
// in both reference perf TUs.
//
// RUN IT ALONE, AND NOT BACK-TO-BACK (CLAUDE.md,
// feedback_cpu_tests_isolation_only.md). Isolation has two clauses: nothing else
// executing - no other suite, no build, no clang-tidy run, no parallel agent -
// AND a settle between runs, because sustained benchmarking heats the package
// and boost clocks drop (the same code drifted +14 % across one session in this
// repo's history).
//
//   node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee cpu.log | tail -60
//
// CAPTURE THE OUTPUT TO A LOG ON THE FIRST RUN AND READ THE LOG. Never re-run a
// perf batch merely to look at output. A verdict that flips between runs is
// measuring the machine, not the code.
//
// -----------------------------------------------------------------------------
// *** STOP-AND-SURFACE RULE (FR-085, inherited VERBATIM from
// *** resonance_drift_network_perf_test.cpp:57-64, itself inherited from
// *** noise_organism_perf_test.cpp:44-58) - NON-NEGOTIABLE ***
// -----------------------------------------------------------------------------
// NO IMPLEMENTING AGENT MAY lower kMaxAgents, raise kBudgetNs, relax a
// threshold, or shrink a workload to make a figure fit. Reduce cost, never move
// the line. The verdict block is emitted loudly via WARN precisely so the
// decision is taken from the measured table rather than from a guess.
//
// FR-085 names the permitted responses to a miss, and the list is closed:
//   1. REDUCE COST. Plan S12.3's levers L1 (the two-stage cutoff), L2 (the
//      syncRate == 0 guard) and L3 (the leakExponent == 1 fast path) are in the
//      component. So are E-1 (FR-040's cell weight by the exact Gaussian
//      recurrence along the uniform cell grid, cellKernelWeight) and E-2
//      (FR-035's and FR-050's sines from one per-agent sin/cos table,
//      refreshPhaseTrig / pairPhaseSine), adopted by the USER on 2026-09-16 from
//      this case's first measured table. All five are exact - algebraic
//      identities, rounding-level error only, asserted against std::exp/std::sin
//      by SC-011 (c) - and not one number moves.
//   2. L4 (the sigma-independent kernel LUT) and L5 (the phase-sine LUT) are NOT
//      PRE-AUTHORISED (plan S14 D-M). They replace formulas the spec states as
//      NORMATIVE - FR-012's w = exp(-d^2/2sigma^2), FR-050's appetite sin,
//      FR-035's Kuramoto sin - with approximations, and Assumption 1 rests on
//      the formulas. They go to the USER with this table, the same route as L6.
//   3. L6 - narrowing FR-082's minimum - was TAKEN TWICE on 2026-09-16, each
//      time by the USER from this case's table: kMinStepIntervalChunks went
//      from 1 to 4 (with E-1/E-2), then from 4 to 8 (the default) when the
//      floor at 4 still read 56 600-60 200 ns/block after E-3 as well. There
//      is nothing left to narrow: the floor is the tuned default.
// Raising the ceiling, restating the budget PER STEP (a relaxation wearing a
// derivation), lowering kMaxAgents, and exempting the cheap end are all
// FORBIDDEN BY NAME (spec.md FR-085, plan S12.3 L6).
//
// -----------------------------------------------------------------------------
// WHY BOTH ENDS OF THE STEP-INTERVAL RANGE ARE GATED (SC-011, spec.md:1219-1224)
// -----------------------------------------------------------------------------
// FR-085 states the ceiling at EVERY legal step interval, so the gate measures
// both ends of the range the spec retains. Since the 2026-09-16 rulings that
// range is [8, 64] (ecosystem_engine.h:286, kMinStepIntervalChunks at :175):
//   (a) stepIntervalChunks = 8  - the floor AND the default: one simulation
//                                 step per 512-sample block, the dearest
//                                 legal configuration;
//   (b) stepIntervalChunks = 64 - the cheap end: one step per 8 blocks;
// both at the same 53 333 ns/block ceiling, both at the worst-case rule
// configuration. (b) cannot fail where (a) passes; it is there so the gate
// states what FR-085 states.
//
// THE MEASURED HISTORY (2026-09-16, pinned to P-cores, alone, best-of-25):
//   floor 1, before E-1/E-2:  (a) at 8: 55 274.8 (1.04x over)
//                             (b) at 1: 404 613.6 (7.59x over)
//     stage probe: grazing loop 32 452 (4 608 cell exps), pair kernel 11 120,
//     Kuramoto sin 6 680, traversal 3 358; Appendix-A defaults 8 143.
//   -> ruling 1: floor 4, levers E-1 (cell-grid Gaussian recurrence) and E-2
//      (sine-difference table); L4/L5 declined.
//   floor 4, after E-1/E-2/E-3: (a) at 8: 26 576-28 796 (~51 % of the ceiling)
//                               (b) at 4: 56 587-60 176 (1.06x-1.13x over)
//     E-3 = invariant hoists and register accumulators, bit-identical, -8 %;
//     reciprocal multiplies and an agent-outer two-pass grazing walk were
//     measured at zero and at +25 % and reverted. The remaining per-visit cost
//     is memory and dependency bound, and the 1 128 pair exps have no exact
//     reduction for arbitrary positions.
//   -> ruling 2: floor 8. The floor is the tuned default; nothing below it
//      had a consumer.
// The plan had projected (a) ~58 000 and (b) ~464 000 (S12.2) and blamed the
// PAIR exps; the probe put two thirds of the step in the CELL loop instead.
// A measured table replaces any projection, and the response to whatever it
// says is the ladder above, taken IN ORDER.
//
// -----------------------------------------------------------------------------
// THE WORST-CASE RULE CONFIGURATION, AND WHY EXACTLY THESE KNOBS
// -----------------------------------------------------------------------------
// SC-011 pins five values and no others (spec.md:966-969, tasks.md T015):
//   agentCount    = 48 (kMaxAgents)  -> 1 128 pairs per step (kMaxPairs, FR-086)
//   resourceCells = 96 (kMaxResourceCells) -> 4 608 cell visits per step
//   kernelSigma   = 0.35 (the Appendix-A maximum) -> EVERY pair and EVERY cell
//                   visit survives the FR-012 cutoff, so lever L1's exp-free
//                   pre-test skips nothing here and the std::exp term is paid in
//                   full. Arithmetically: cutDistSq_ = 2*sigma^2*13.8155 = 3.385
//                   (ecosystem_engine.h:1938-1945) against a maximum reachable
//                   d^2 of 0.5 on the unit torus (wrapDelta bounds each axis to
//                   +-0.5, :1089-1097). The fixture ASSERTS the resulting
//                   getPairInteractionCount() == 1 128 rather than assuming it.
//   syncRate  ON  -> lever L2's guard (:1462-1465) does not fire, so the 1 128
//                    Kuramoto std::sin calls are paid. Set to the Appendix-A
//                    maximum 0.5; any value > 0 costs the same sins.
//   leakExponent > 1 -> lever L3's fast path does not fire, so the 48 std::pow
//                    calls are paid. Set to the Appendix-A maximum 2.5.
// Every OTHER knob stays at its Appendix-A default (ecosystem_engine.h:2185-2211)
// - that is the configuration the spec names, and widening it here would be
// inventing a worst case the criterion does not state. Two defaults are load
// bearing for cost and are therefore called out: forageRate = 0.010 > 0 keeps
// FR-033's gradient term live inside the cell loop (:1672-1676) and
// crowding = 0.05 > 0 keeps the FR-032 repulsion live inside the pair loop
// (:1439-1446).
//
// THE ONE DATA-DEPENDENT COST TERM, REPORTED RATHER THAN ASSUMED: the grazing
// loop skips an EMPTY cell's demand work (`if (!cellLive || ...) continue;`
// inside the agent loop, after lever E-1's run bookkeeping). A measurement
// taken after the population has grazed the field flat would
// therefore price a cell loop the worst case never runs. Every arm reports its
// LIVE-CELL COUNT at the end of the timed run beside its figure, so a reader can
// see whether the cell loop was actually being paid for. A low count is a
// finding to surface under FR-085's rule, not a number to quietly accept.
//
// -----------------------------------------------------------------------------
// HOW THE STAGE PROBE ATTRIBUTES COST WITH THE PUBLIC API ONLY (tasks.md T015,
// plan S12.3's "Sequencing")
// -----------------------------------------------------------------------------
// EcosystemEngine exposes no per-stage hook and this task may edit no other
// file, so the probe attributes cost by DIFFERENCING A MONOTONE LADDER of
// configurations in which each rung changes EXACTLY ONE axis:
//
//   F   A=48 C=1  sigma=0.01 sync=0   leak=1.0   the floor: 1 128 pairs and 48
//                                                cell visits all rejected by the
//                                                exp-free pre-test, plus stages
//                                                4 and 8-13 for 48 agents
//   T1  F + sigma=0.35                           adds the surviving pair kernel:
//                                                1 128 x (exp, sqrt, affinity,
//                                                crowding) + 48 cell exps
//   T2  T1 + C=96                                adds 4 560 more cell visits with
//                                                kernel work (95 of the 96 cells)
//   T3  T2 + sync=0.5                            adds 1 128 Kuramoto std::sin
//   W8  T3 + leak=2.5                            adds 48 std::pow - and W8 IS
//                                                SC-011 (a)'s configuration
//   W64 W8 at stepIntervalChunks=64              1 step per 8 blocks (the
//                                                cheap end; W8 is the floor)
//   D   the Appendix-A defaults (A=32, C=64, sigma=0.03, sync off, leak 1)
//   Z   W8 with EVERY agent dormant              FR-072's prediction, measured
//   F0  A=1 C=1 sigma=0.01 sync=0 leak=1.0       the single-agent floor
//
// WHAT THE LADDER CANNOT SPLIT, STATED RATHER THAN FUDGED: stages 8-11
// (integrate, movement, OU drift, phase) and stage 13 (publication) are three
// per-agent passes with no knob that removes any of them - moveRate = 0 still
// pays the std::sqrt (:1814), freqDrift = 0 still pays the RNG draw (A-8,
// :1838), and publish() runs unconditionally (:1891). They are therefore
// reported TOGETHER, inside F, alongside the rejected-pair traversal. F0 prices
// the same tail for a single agent, so the pair between them bounds how much of
// F is per-agent tail and how much is traversal; the probe prints both and
// claims no finer split.
//
// FR-072's PREDICTION IS THE POINT OF ARM Z: "dormancy gates the output, not the
// simulation - a dormant agent keeps grazing, exchanging and leaking"
// (ecosystem_engine.h:737-752). So Z is predicted NOT to be cheaper than W8. The
// probe is where that prediction is confirmed or found wrong, which is why the
// delta is printed as a signed percentage instead of being asserted away.
//
// This case is a PROBE, NOT A GATE. It REQUIREs only that every figure is finite
// and strictly positive - a zero or a NaN means the measurement is broken, which
// is the one thing that would make the table lie.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>  // detail::isFinite - NEVER std::isfinite here
#include <krate/dsp/systems/ecosystem_engine.h>

#include <catch2/catch_all.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (SC-011, plan S12.1)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// The engine's control chunk (FR-081, kControlChunkSamples). One simulation
/// step fires every `stepIntervalChunks * 64` samples, so the chunk must divide
/// the measured block exactly or "steps per block" is not an integer and the
/// per-arm step-rate precondition below cannot be stated.
constexpr std::size_t kControlChunk = EcosystemEngine::kControlChunkSamples;
static_assert(kBlockSize % kControlChunk == 0,
              "the control chunk must divide the measured block exactly");

/// Wall-clock period of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockPeriodNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// FR-085's ceiling: 0.5 % of one core = 53 333 ns/block (spec.md:692-694, plan
/// S12.1). Written as the literal the spec names and TIED to its derivation by
/// the clause below rather than computed from it.
///
/// NO AGENT MAY RAISE THIS. The stop-and-surface rule in this TU's header
/// governs every response to a miss.
constexpr double kBudgetNs = 53333.0;
static_assert(kBudgetNs >= kBlockPeriodNs * 0.00499 && kBudgetNs <= kBlockPeriodNs * 0.00501,
              "FR-085's budget is 0.5 % of one 512-sample block at 48 kHz");

// Trial shape, pinned by tasks.md T015 and plan S12.1.
constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;
constexpr int kWarmupBlocks = 400;

// -----------------------------------------------------------------------------
// The SC-011 worst-case rule configuration (spec.md:966-969). Five values, and
// no others: every remaining knob keeps its Appendix-A default.
// -----------------------------------------------------------------------------
constexpr std::size_t kWorstAgents = EcosystemEngine::kMaxAgents;         // 48
constexpr std::size_t kWorstCells = EcosystemEngine::kMaxResourceCells;   // 96
constexpr float kWorstSigma = 0.35f;        ///< every pair survives FR-012's cutoff
constexpr float kWorstSyncRate = 0.5f;      ///< lever L2's guard does not fire
constexpr float kWorstLeakExponent = 2.5f;  ///< lever L3's fast path does not fire

/// FR-086's pair count at kMaxAgents: 48 * 47 / 2. Asserted as a FIXTURE
/// PRECONDITION, because a figure measured with fewer surviving pairs is not a
/// measurement of the worst case at all.
constexpr std::size_t kWorstCasePairs = EcosystemEngine::kMaxPairs;  // 1 128
static_assert(kWorstCasePairs == 1128u, "FR-086's worst-case pair count");

/// The two gated step intervals (SC-011 (a) and (b)): both ends of FR-082's
/// retained range. Its floor is the default since the two 2026-09-16 rulings
/// narrowed it 1 -> 4 -> 8 (FR-085's named escalation, each time from this
/// case's own measured table - see the header), so (a) IS the floor and (b) is
/// the cheap end. (b) cannot fail where (a) passes; it is kept so the gate
/// still states the ceiling at every legal step interval, as FR-085 does.
constexpr std::size_t kArmAChunks = EcosystemEngine::kMinStepIntervalChunks;  // 8, the default
constexpr std::size_t kArmBChunks = EcosystemEngine::kMaxStepIntervalChunks;  // 64
static_assert(kArmAChunks == EcosystemEngine::kDefaultStepIntervalChunks && kArmAChunks == 8u &&
                  kArmBChunks == 64u,
              "SC-011 gates BOTH ends of FR-082's retained range; its floor is the default");

// The Appendix-A defaults, spelled out for the probe's defaults arm
// (ecosystem_engine.h:2172-2211). They are transcribed, not guessed: a defaults
// arm built from the wrong numbers prices a configuration the component never
// ships.
constexpr std::size_t kDefaultAgents = 32;
constexpr std::size_t kDefaultCells = 64;
constexpr float kDefaultSigma = 0.03f;
constexpr float kDefaultSyncRate = 0.0f;
constexpr float kDefaultLeakExponent = 1.0f;

// =============================================================================
// Best-of-N driver (the atmosphere_engine_perf_test.cpp /
// resonance_drift_network_perf_test.cpp:283-305 idiom)
// =============================================================================

/// Pinned warm-up, then best-of-`kTrials` x `kBlocksPerTrial`; returns the
/// winning trial's ns per invocation of `runBlock`. One invocation is one
/// 512-sample block, so the result is ns/block.
///
/// `runBlock` is taken by const reference, not by forwarding reference: it is
/// INVOKED, many times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestTrialNs(const BlockFn& runBlock)
{
    for (int i = 0; i < kWarmupBlocks; ++i) {
        runBlock();
    }

    double best = -1.0;
    for (int trial = 0; trial < kTrials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocksPerTrial; ++i) {
            runBlock();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        const double nsPerCall = elapsedNs / static_cast<double>(kBlocksPerTrial);
        if (best < 0.0 || nsPerCall < best) {
            best = nsPerCall;
        }
    }
    return best;
}

// =============================================================================
// One measured arm
// =============================================================================

/// @brief One configuration of the ladder. Defaults ARE the SC-011 worst case,
///        so an arm states only what it changes.
struct ArmConfig {
    const char* label = "";
    std::size_t agentCount = kWorstAgents;
    std::size_t resourceCells = kWorstCells;
    std::size_t stepIntervalChunks = kArmAChunks;
    float kernelSigma = kWorstSigma;
    float syncRate = kWorstSyncRate;
    float leakExponent = kWorstLeakExponent;
    bool allDormant = false;
};

/// @brief What one arm measured, and the fixture evidence that says whether the
///        figure priced what it claims to.
struct ArmResult {
    double nsPerBlock = 0.0;
    std::size_t pairCount = 0;    ///< survivors at the LAST step of the timed run
    std::size_t liveCells = 0;    ///< cells with energy > 0 after the timed run
    std::size_t totalCells = 0;
    std::uint64_t stepsInEightBlocks = 0;  ///< the FR-082 grid, measured not assumed
    std::uint64_t expectedStepsInEightBlocks = 0;
    bool allFinite = true;
    bool anyNonZeroOutput = false;
    bool allExactZeroOutput = true;
};

/// @brief Prepare @p cfg, warm it, time it, then read back the fixture evidence.
///
/// CONSTRUCTED THROUGH std::make_unique, never as a plain stack local: the object
/// is ~23.5 KB (ecosystem_engine.h:126-133, plan S9's ledger).
///
/// ORDER MATTERS AND IS DELIBERATE. The knobs are set BEFORE prepare() because
/// prepare() re-derives STATE, never CONFIGURATION (:346-351) and re-runs both
/// refresh passes at step 4-5, so the absolutes are correct whichever order is
/// used. Dormancy is set AFTER prepare(), because initialiseState() fills
/// wake_ = 1 and dormant_ = false (:2105-2106) and would undo it.
[[nodiscard]] ArmResult measureArm(const ArmConfig& cfg, double& sink)
{
    auto engine = std::make_unique<EcosystemEngine>();
    engine->setKernelSigma(cfg.kernelSigma);
    engine->setSyncRate(cfg.syncRate);
    engine->setLeakExponent(cfg.leakExponent);
    engine->prepare(kSr48, EcosystemEngine::PrepareConfig{
                               .agentCount = cfg.agentCount,
                               .resourceCells = cfg.resourceCells,
                               .stepIntervalChunks = cfg.stepIntervalChunks});
    if (cfg.allDormant) {
        for (std::size_t i = 0; i < engine->getAgentCount(); ++i) {
            engine->setAgentDormant(i, true);
        }
    }

    EcosystemEngine* const raw = engine.get();
    const auto runBlock = [raw, &sink]() {
        raw->processChunk(kBlockSize);
        // Read one published value so no arm can be dead-coded away. Not a
        // result: the sink is only checked for finiteness at the call site.
        sink += static_cast<double>(raw->getAgentOutput(0));
    };

    ArmResult r;
    r.nsPerBlock = bestTrialNs(runBlock);

    // --- fixture evidence, read AFTER the timed run --------------------------
    r.pairCount = engine->getPairInteractionCount();
    r.totalCells = engine->getResourceCells();
    for (std::size_t k = 0; k < r.totalCells; ++k) {
        if (engine->getCellEnergy(k) > 0.0) {
            ++r.liveCells;
        }
    }
    for (std::size_t i = 0; i < engine->getAgentCount(); ++i) {
        const float out = engine->getAgentOutput(i);
        if (!detail::isFinite(out)) {
            r.allFinite = false;
        }
        if (out != 0.0f) {
            r.anyNonZeroOutput = true;
            r.allExactZeroOutput = false;
        }
    }

    // The FR-082 grid, MEASURED: eight 512-sample blocks are 4 096 samples, so
    // the step count must advance by 64 / stepIntervalChunks - which is an
    // integer at 8 and 64 alike. A cheap figure taken from an engine that
    // never stepped is exactly what this catches.
    const std::uint64_t before = engine->getControlStepCount();
    for (int b = 0; b < 8; ++b) {
        engine->processChunk(kBlockSize);
    }
    r.stepsInEightBlocks = engine->getControlStepCount() - before;
    const std::size_t samplesPerStep = kControlChunk * engine->getStepIntervalChunks();
    r.expectedStepsInEightBlocks = static_cast<std::uint64_t>((8u * kBlockSize) / samplesPerStep);

    return r;
}

// =============================================================================
// Reporting helpers
// =============================================================================

/// One table line: label, ns/block, and the REPORTED (never asserted) percent of
/// one core.
[[nodiscard]] std::string row(const char* label, double ns)
{
    std::ostringstream os;
    os << "  " << std::left << std::setw(44) << label << std::right << std::fixed
       << std::setprecision(1) << std::setw(13) << ns << " ns/block " << std::setprecision(3)
       << std::setw(9) << (100.0 * ns / kBlockPeriodNs) << " % core";
    return os.str();
}

/// The fixture evidence line that travels with every measured figure.
[[nodiscard]] std::string evidence(const ArmResult& r)
{
    std::ostringstream os;
    os << "        pairs " << r.pairCount << "   live cells " << r.liveCells << " / "
       << r.totalCells << "   steps/8 blocks " << r.stepsInEightBlocks << " (expected "
       << r.expectedStepsInEightBlocks << ")   finite " << (r.allFinite ? "yes" : "NO")
       << "   any output > 0 " << (r.anyNonZeroOutput ? "yes" : "no");
    return os.str();
}

/// A signed delta between two rungs of the ladder, with what it prices.
[[nodiscard]] std::string delta(const char* label, double hi, double lo)
{
    std::ostringstream os;
    os << "  " << std::left << std::setw(44) << label << std::right << std::fixed
       << std::setprecision(1) << std::setw(13) << (hi - lo) << " ns/block "
       << std::setprecision(2) << std::setw(8)
       << (hi > 0.0 ? (100.0 * (hi - lo) / hi) : 0.0) << " % of its rung";
    return os.str();
}

}  // namespace

// =============================================================================
// SC-011 (a) + (b) - THE GATE
// =============================================================================
TEST_CASE("EcosystemEngine_CpuBudget", "[ecosystem_engine][.perf]")
{
    double sink = 0.0;

    const ArmConfig armA{.label = "(a) worst case, stepIntervalChunks = 8 (floor)",
                         .stepIntervalChunks = kArmAChunks};
    const ArmConfig armB{.label = "(b) worst case, stepIntervalChunks = 64",
                         .stepIntervalChunks = kArmBChunks};

    const ArmResult a = measureArm(armA, sink);
    const ArmResult b = measureArm(armB, sink);

    // -------------------------------------------------------------------------
    // Report FIRST, assert second: on a miss the failure has to carry the
    // evidence the T016 escalation decision is taken from. The plan projects
    // BOTH arms to miss (S12.2), so this table is the deliverable of the task
    // whether or not the case passes.
    // -------------------------------------------------------------------------
    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  EcosystemEngine SC-011 CPU BUDGET   (48 kHz, 512-sample blocks)\n"
       << "  specs/vorago-phase8-ecosystem, SC-011 / FR-085, tasks.md T015\n"
       << "  best-of-" << kTrials << " x " << kBlocksPerTrial << " blocks after "
       << kWarmupBlocks << " warm-up blocks\n"
       << "  worst-case rule configuration: agents " << kWorstAgents << ", cells "
       << kWorstCells << ", kernelSigma " << std::fixed << std::setprecision(2) << kWorstSigma
       << ", syncRate " << kWorstSyncRate << ", leakExponent " << kWorstLeakExponent << "\n"
       << "  (every other knob at its Appendix-A default)\n"
       << "=================================================================================\n"
       << row(armA.label, a.nsPerBlock) << "\n"
       << evidence(a) << "\n"
       << row(armB.label, b.nsPerBlock) << "\n"
       << evidence(b) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << row("FR-085 CEILING (0.5 % of one core)", kBudgetNs) << "\n"
       << "  (a) / (b) step-rate scaling                  " << std::fixed
       << std::setprecision(2) << std::setw(13)
       << (b.nsPerBlock > 0.0 ? (a.nsPerBlock / b.nsPerBlock) : 0.0)
       << "x         [1 step/block vs 1 per 8 blocks; <= 8x expected]\n"
       << "  measured 2026-09-16 before any ruling (plan S12.2):  (a) 55 275   (at 1) 404 614 ns\n"
       << "  measured after E-1/E-2/E-3, floor 4 (2nd ruling):   (a) 27 224   (at 4)  60 176 ns\n"
       << "=================================================================================\n";

    for (const ArmResult* r : {&a, &b}) {
        const char* const label = (r == &a) ? "(a)" : "(b)";
        if (r->nsPerBlock > kBudgetNs) {
            os << "  *** SC-011 " << label << " IS OVER THE CEILING BY " << std::fixed
               << std::setprecision(1) << (r->nsPerBlock - kBudgetNs) << " ns ("
               << std::setprecision(2) << (r->nsPerBlock / kBudgetNs) << "x).\n";
        } else {
            os << "  SC-011 " << label << " IS WITHIN BUDGET: " << std::fixed
               << std::setprecision(1) << (kBudgetNs - r->nsPerBlock) << " ns of headroom ("
               << std::setprecision(2) << (100.0 * r->nsPerBlock / kBudgetNs)
               << " % of the ceiling).\n";
        }
    }

    if (a.nsPerBlock > kBudgetNs || b.nsPerBlock > kBudgetNs) {
        os << "  *** FR-085 STOP-AND-SURFACE APPLIES. HALT and put THIS TABLE, together\n"
           << "  *** with EcosystemEngine_StageCostProbe's per-stage breakdown, to the\n"
           << "  *** USER (plan S12.3, tasks.md T016). The ladder as it stands:\n"
           << "  ***   L1/L2/L3 (two-stage cutoff, syncRate == 0 guard, leakExponent == 1\n"
           << "  ***      fast path) and E-1/E-2 (the cell-grid Gaussian recurrence and the\n"
           << "  ***      sine-difference table, ruled 2026-09-16) are ALREADY in the\n"
           << "  ***      component and are exact. Confirm they are present.\n"
           << "  ***   L4 (kernel LUT) and L5 (phase-sine LUT) are NOT PRE-AUTHORISED:\n"
           << "  ***      they replace FR-012's, FR-050's and FR-035's NORMATIVE formulas\n"
           << "  ***      and need USER SIGN-OFF from this table (plan S14 D-M).\n"
           << "  ***   L6 - FR-082's minimum was ALREADY narrowed to 8 (the default) by\n"
           << "  ***      the 2026-09-16 rulings; there is nothing left to narrow.\n"
           << "  *** NO AGENT MAY lower kMaxAgents, raise kBudgetNs, restate the budget\n"
           << "  *** per STEP, exempt the cheap end, relax a threshold or shrink a\n"
           << "  *** workload to make this fit. Reduce cost, never move the line.\n"
           << "=================================================================================\n";
    }

    WARN(os.str());

    // -------------------------------------------------------------------------
    // Preconditions, BEFORE the timing verdicts. A figure taken from a fixture
    // that was not running the specified configuration is not a measurement of
    // anything, and must fail as a FIXTURE error rather than as a budget miss.
    // -------------------------------------------------------------------------
    REQUIRE(detail::isFinite(sink));

    for (const ArmResult* r : {&a, &b}) {
        CAPTURE(r->nsPerBlock, r->pairCount, r->liveCells, r->totalCells,
                r->stepsInEightBlocks, r->expectedStepsInEightBlocks);
        REQUIRE(detail::isFinite(r->nsPerBlock));
        REQUIRE(r->nsPerBlock > 0.0);
        REQUIRE(r->allFinite);
        // Every pair survives at sigma = 0.35: this is THE clause that says the
        // worst case was actually rendered (FR-086, spec.md:1184-1186).
        REQUIRE(r->pairCount == kWorstCasePairs);
        // The step grid fired at the rate FR-082 specifies - the anti-no-op
        // clause on the SIMULATION rather than on the clock.
        REQUIRE(r->stepsInEightBlocks == r->expectedStepsInEightBlocks);
        REQUIRE(r->stepsInEightBlocks > 0u);
        // A silent engine cannot have been doing the work.
        REQUIRE(r->anyNonZeroOutput);
        REQUIRE_FALSE(r->allExactZeroOutput);
        // The full cell field was configured...
        REQUIRE(r->totalCells == kWorstCells);
        // ...and at least some of it was still being grazed. A field grazed
        // completely flat skips every cell's demand work (`if (!cellLive ||
        // ...) continue;` in stage 6), so a figure measured there prices a
        // grazing loop the worst case never runs. That is a fixture failure,
        // not a cheap result.
        REQUIRE(r->liveCells > 0u);
    }

    // -------------------------------------------------------------------------
    // THE GATE. CHECK, not REQUIRE, so a miss on (a) still reports (b): the
    // escalation decision needs BOTH figures, and stopping at the first failure
    // would withhold half the table the user has to decide from.
    // -------------------------------------------------------------------------
    CAPTURE(a.nsPerBlock, kBudgetNs);
    CHECK(a.nsPerBlock <= kBudgetNs);  // SC-011 (a), stepIntervalChunks = 8 (floor + default)

    CAPTURE(b.nsPerBlock, kBudgetNs);
    CHECK(b.nsPerBlock <= kBudgetNs);  // SC-011 (b), stepIntervalChunks = 64 (the cheap end)
}

// =============================================================================
// THE STAGE COST PROBE - WARN-reported, NOT a gate
// =============================================================================
TEST_CASE("EcosystemEngine_StageCostProbe", "[ecosystem_engine][.perf]")
{
    double sink = 0.0;

    // The monotone ladder: each rung changes EXACTLY ONE axis from the one above
    // it, so the difference prices that axis and nothing else.
    const ArmConfig cfgF0{.label = "F0  1 agent, 1 cell, sigma 0.01 (floor)",
                          .agentCount = 1,
                          .resourceCells = 1,
                          .kernelSigma = 0.01f,
                          .syncRate = 0.0f,
                          .leakExponent = 1.0f};
    const ArmConfig cfgF{.label = "F   48 agents, 1 cell, sigma 0.01",
                         .resourceCells = 1,
                         .kernelSigma = 0.01f,
                         .syncRate = 0.0f,
                         .leakExponent = 1.0f};
    const ArmConfig cfgT1{.label = "T1  F + sigma 0.35 (pair kernel survives)",
                          .resourceCells = 1,
                          .syncRate = 0.0f,
                          .leakExponent = 1.0f};
    const ArmConfig cfgT2{.label = "T2  T1 + 96 cells (grazing loop)",
                          .syncRate = 0.0f,
                          .leakExponent = 1.0f};
    const ArmConfig cfgT3{.label = "T3  T2 + syncRate 0.5 (Kuramoto sin)",
                          .leakExponent = 1.0f};
    const ArmConfig cfgW8{.label = "W8  T3 + leakExponent 2.5  == SC-011 (a)"};
    const ArmConfig cfgW64{.label = "W64 W8 at stepIntervalChunks = 64",
                           .stepIntervalChunks = 64};
    const ArmConfig cfgD{.label = "D   Appendix-A defaults",
                         .agentCount = kDefaultAgents,
                         .resourceCells = kDefaultCells,
                         .kernelSigma = kDefaultSigma,
                         .syncRate = kDefaultSyncRate,
                         .leakExponent = kDefaultLeakExponent};
    const ArmConfig cfgZ{.label = "Z   W8 with EVERY agent dormant", .allDormant = true};

    const ArmResult f0 = measureArm(cfgF0, sink);
    const ArmResult f = measureArm(cfgF, sink);
    const ArmResult t1 = measureArm(cfgT1, sink);
    const ArmResult t2 = measureArm(cfgT2, sink);
    const ArmResult t3 = measureArm(cfgT3, sink);
    const ArmResult w8 = measureArm(cfgW8, sink);
    const ArmResult w64 = measureArm(cfgW64, sink);
    const ArmResult d = measureArm(cfgD, sink);
    const ArmResult z = measureArm(cfgZ, sink);

    const std::array<const ArmResult*, 9> all{&f0, &f, &t1, &t2, &t3, &w8, &w64, &d, &z};
    const std::array<const ArmConfig*, 9> allCfg{&cfgF0, &cfgF,   &cfgT1, &cfgT2, &cfgT3,
                                                 &cfgW8, &cfgW64, &cfgD,  &cfgZ};

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  EcosystemEngine STAGE COST PROBE   (48 kHz, 512-sample blocks)   NOT A GATE\n"
       << "  specs/vorago-phase8-ecosystem, SC-011's non-gating table / FR-072, tasks.md T015\n"
       << "  best-of-" << kTrials << " x " << kBlocksPerTrial << " blocks after "
       << kWarmupBlocks << " warm-up blocks\n"
       << "=================================================================================\n"
       << "  THE LADDER (each rung changes exactly one axis from the one above it)\n";

    for (std::size_t i = 0; i < all.size(); ++i) {
        os << row(allCfg[i]->label, all[i]->nsPerBlock) << "\n" << evidence(*all[i]) << "\n";
    }

    os << "---------------------------------------------------------------------------------\n"
       << "  STAGE ATTRIBUTION, BY DIFFERENCING THE LADDER\n"
       << delta("pair kernel work, 1 128 pairs (T1 - F)", t1.nsPerBlock, f.nsPerBlock) << "\n"
       << "        + 48 cell visits at C = 1 (each seeding an E-1 run: 2 exps)\n"
       << delta("grazing loop, 95 of 96 cells (T2 - T1)", t2.nsPerBlock, t1.nsPerBlock) << "\n"
       << "        extrapolated to all 96 cells: " << std::fixed << std::setprecision(1)
       << ((t2.nsPerBlock - t1.nsPerBlock) * 96.0 / 95.0) << " ns/block\n"
       << "        (4 608 visits: ~192 exps by lever E-1's recurrence, was 4 608)\n"
       << delta("Kuramoto term, 1 128 pairs (T3 - T2)", t3.nsPerBlock, t2.nsPerBlock) << "\n"
       << "        (96 sin/cos by lever E-2's table, was 1 128 sin)\n"
       << delta("nonlinear leak, 48 pow (W8 - T3)", w8.nsPerBlock, t3.nsPerBlock) << "\n"
       << "  " << std::left << std::setw(44) << "traversal + per-agent tail (F itself)"
       << std::right << std::fixed << std::setprecision(1) << std::setw(13) << f.nsPerBlock
       << " ns/block\n"
       << "        stages 4 and 8-13 for 48 agents, PLUS 1 128 pairs and 48 cell visits\n"
       << "        rejected by the exp-free pre-test. NOT splittable through the public\n"
       << "        API: moveRate 0 still pays the sqrt, freqDrift 0 still draws (A-8),\n"
       << "        and publish() is unconditional. F0 (" << std::setprecision(1)
       << f0.nsPerBlock << " ns/block) is the same tail for ONE agent.\n"
       << "---------------------------------------------------------------------------------\n"
       << "  STEP-RATE SCALING (FR-082's range [8, 64], at the worst case)\n"
       << "  W8 / W64 " << std::fixed << std::setprecision(2)
       << (w64.nsPerBlock > 0.0 ? (w8.nsPerBlock / w64.nsPerBlock) : 0.0)
       << "x   [1 step/block vs 1 per 8 blocks; ~8x expected]\n"
       << "  defaults D as a fraction of W8: " << std::setprecision(2)
       << (w8.nsPerBlock > 0.0 ? (100.0 * d.nsPerBlock / w8.nsPerBlock) : 0.0) << " %\n"
       << "---------------------------------------------------------------------------------\n"
       << "  FR-072's PREDICTION: all-dormant is NOT cheaper (dormancy gates the OUTPUT,\n"
       << "  not the simulation - ecosystem_engine.h:737-752).\n"
       << "  Z - W8 = " << std::fixed << std::setprecision(1) << (z.nsPerBlock - w8.nsPerBlock)
       << " ns/block (" << std::setprecision(2)
       << (w8.nsPerBlock > 0.0 ? (100.0 * (z.nsPerBlock - w8.nsPerBlock) / w8.nsPerBlock) : 0.0)
       << " % of W8).  A saving materially larger than run-to-run noise would mean the\n"
       << "  prediction is WRONG and FR-072's rationale needs re-reading - a finding to\n"
       << "  surface, not a number to absorb.\n"
       << "---------------------------------------------------------------------------------\n"
       << row("FR-085 CEILING (0.5 % of one core)", kBudgetNs) << "\n"
       << "  This case asserts NOTHING about any of the figures above beyond finiteness\n"
       << "  and positivity. The gate is EcosystemEngine_CpuBudget; the response to a\n"
       << "  miss is plan S12.3's ladder, taken IN ORDER (L1-L3 and E-1/E-2 are in),\n"
       << "  with L4/L5 and any further L6 narrowing going to the USER.\n"
       << "  NO AGENT MAY relax a threshold or shrink a workload from this table.\n"
       << "=================================================================================\n";

    WARN(os.str());

    // -------------------------------------------------------------------------
    // The ONLY assertions a probe may carry: a zero or a non-finite figure means
    // the measurement is broken, which is the one thing that would make the
    // table lie.
    // -------------------------------------------------------------------------
    REQUIRE(detail::isFinite(sink));
    for (std::size_t i = 0; i < all.size(); ++i) {
        CAPTURE(i, allCfg[i]->label, all[i]->nsPerBlock);
        REQUIRE(detail::isFinite(all[i]->nsPerBlock));
        REQUIRE(all[i]->nsPerBlock > 0.0);
    }
}
