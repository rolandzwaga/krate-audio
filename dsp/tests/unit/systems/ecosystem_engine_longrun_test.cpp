// ==============================================================================
// Layer 3: System Tests - EcosystemEngine long-run / fuzz set
//                              (specs/vorago-phase8-ecosystem)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase8-ecosystem/spec.md
//            specs/vorago-phase8-ecosystem/plan.md   (S10.1 TU assignment, S10.3)
//            specs/vorago-phase8-ecosystem/tasks.md  (T001 creates this stub,
//                                                     T019 lands the fuzz harness)
//
// Criteria owned (plan S10.1): SC-001 (a)-(d) with the perturb schedule folded
// into the one 1000-config batch, SC-003, SC-005, SC-012, SC-013, SC-017, SC-018.
// Every case here is tagged [long] - per-push CI excludes that tag and runs it
// nightly on all three OSes.
//
// T019 lands the SHARED FUZZ HARNESS (plan S10.3) - RuleConfig, the static knob
// table, the exemption arrays and both box generators - plus SC-012, the
// criterion that gates the other two fuzz criteria: SC-001 and SC-013 are only
// worth their thresholds if the harness is provably still testing every rule.
// The later long-run cases (T020, T021) consume the harness below unchanged.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/systems/ecosystem_engine.h>

// The Phase-8 metric helpers (T017). A test-local header beside its TUs: no
// CMake entry, two precedents in this tree (plan S14 D-D).
#include "ecosystem_metrics_test_helpers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Krate::DSP::EcosystemEngine;
using Krate::DSP::Xorshift32;

// ==============================================================================
// The fuzz harness (plan S10.3, tasks T019)
// ==============================================================================

/// @brief The prototype's `Xorshift32::range` (`ecosystem-sim.js:47`), in double.
///
/// `nextUnipolar()` is `float` in C++ and `double` in JS (`core/random.h:66-68`
/// vs `ecosystem-sim.js:45`), so the C++ draws are NOT expected to be per-config
/// identical to the prototype's. Plan S10.3 makes that explicit: every comparison
/// to the prototype's batch figures (500/500 bounded, 83.0 % alive) is
/// STATISTICAL, never per-config.
[[nodiscard]] double rangeD(Xorshift32& rng, double lo, double hi) noexcept {
    return lo + (hi - lo) * static_cast<double>(rng.nextUnipolar());
}

/// @brief One fuzzed rule configuration: all 28 Appendix-A knobs (plan S10.3).
///
/// EVERY KNOB IS A `double`, including the three `std::size_t` ones. The knob
/// table below is `{ const char* name; double RuleConfig::* member; }`, and one
/// uniform member type is what lets clause (b) of SC-012 iterate the whole knob
/// set with a single loop instead of a hand-maintained switch that can rot.
///
/// The affinity matrix is represented in the knob table by the single scalar
/// `affinityMagnitude` (plan S10.3: "the affinity matrix as one
/// `affinityMagnitude`"); the matrix itself rides along in `affinity` because
/// `applyTo()` has to replay all 25 entries through `setAffinity()`.
///
/// DEPARTURE, recorded here rather than left implicit. Plan S10.3 words
/// `affinityMagnitude` as "driving `randomAffinity`'s range". The prototype's
/// range bound is a CONSTANT per box (+/-2.0 hostile `run.js:330`, +/-1.5 sane
/// `run.js:376`), and a constant fails SC-012 clause (b)'s `min < max` outright
/// - while SC-012's exemption list is normative and closed ("No other exemption
/// may be added without amending this criterion", spec SC-012), so affinity
/// cannot be exempted out of the problem either. `affinityMagnitude` is
/// therefore the REALISED magnitude of the config's own matrix (the largest
/// `|entry|` actually drawn), which:
///   * leaves `randomAffinity`'s draw bit-for-bit the prototype's, so the box
///     distribution the 500/500 reference was measured on is unchanged - the
///     alternative (drawing the bound per config) would silently WEAKEN the
///     hostile box's affinity forces, and boundedness is what that box exists
///     to prove;
///   * and is a STRICTLY BETTER detector for the failure SC-012 exists to catch:
///     delete the affinity draw from a generator and the realised magnitude
///     stops varying, so clause (b) fires - whereas a box-bound constant would
///     have gone on reporting the same value whether the matrix was drawn or
///     left at its default.
struct RuleConfig {
    // ---- FR-006 prepare-time set (Appendix A, "Lifetime" column = prepare) ---
    double agentCount = 32.0;
    double resourceCells = 64.0;
    double energyBudget = 1.0;
    double initialPoolFraction = 0.5;
    double stepIntervalChunks =
        static_cast<double>(EcosystemEngine::kDefaultStepIntervalChunks);

    // ---- FR-064 runtime knobs -----------------------------------------------
    double kernelSigma = 0.03;
    double exchangeRate = 0.35;
    double predation = 0.55;
    double preyFloorShares = 0.5;
    double capacityShares = 32.0;
    double leakRate = 0.06;
    double leakExponent = 1.0;
    double moveRate = 0.20;
    double maxSpeed = 0.03;
    double forageRate = 0.010;
    double crowding = 0.05;
    double crowdingRadius = 0.02;
    double syncRate = 0.0;
    double cellCapacityShares = 3.2;
    double regenRate = 0.05;
    double grazeRate = 0.75;
    double feedRate = 0.0;
    double satiationShares = 0.0;
    double appetiteDepth = 0.8;
    double freqLo = 0.0015;
    double freqHi = 0.018;
    double freqDrift = 0.00004;
    double affinityMagnitude = 1.0;  ///< realised max |entry| of `affinity`

    /// The matrix itself. NOT a knob-table entry (see `affinityMagnitude`).
    std::array<std::array<double, EcosystemEngine::kNumKinds>,
               EcosystemEngine::kNumKinds>
        affinity{};

    /// @brief `prepare()` with the five prepare-time fields, then every runtime
    ///        setter (plan S10.3).
    ///
    /// Designated initialisers, in declaration order, because a positional brace
    /// init of `PrepareConfig` narrows `double` -> `std::size_t`: Clang errors
    /// where MSVC does not, i.e. a Windows-green / CI-red construct
    /// (ecosystem_engine.h:277-284).
    void applyTo(EcosystemEngine& engine) const noexcept {
        engine.prepare(EcosystemEngine::kDefaultSampleRate,
                       EcosystemEngine::PrepareConfig{
                           .agentCount = static_cast<std::size_t>(agentCount),
                           .resourceCells = static_cast<std::size_t>(resourceCells),
                           .energyBudget = energyBudget,
                           .initialPoolFraction = initialPoolFraction,
                           .stepIntervalChunks =
                               static_cast<std::size_t>(stepIntervalChunks)});

        engine.setKernelSigma(static_cast<float>(kernelSigma));
        engine.setExchangeRate(static_cast<float>(exchangeRate));
        engine.setPredation(static_cast<float>(predation));
        engine.setPreyFloorShares(static_cast<float>(preyFloorShares));
        engine.setCapacityShares(static_cast<float>(capacityShares));
        engine.setLeakRate(static_cast<float>(leakRate));
        engine.setLeakExponent(static_cast<float>(leakExponent));
        engine.setMoveRate(static_cast<float>(moveRate));
        engine.setMaxSpeed(static_cast<float>(maxSpeed));
        engine.setForageRate(static_cast<float>(forageRate));
        engine.setCrowding(static_cast<float>(crowding));
        engine.setCrowdingRadius(static_cast<float>(crowdingRadius));
        engine.setSyncRate(static_cast<float>(syncRate));
        engine.setCellCapacityShares(static_cast<float>(cellCapacityShares));
        engine.setRegenRate(static_cast<float>(regenRate));
        engine.setGrazeRate(static_cast<float>(grazeRate));
        engine.setFeedRate(static_cast<float>(feedRate));
        engine.setSatiationShares(static_cast<float>(satiationShares));
        engine.setAppetiteDepth(static_cast<float>(appetiteDepth));
        engine.setFreqRangeHz(static_cast<float>(freqLo), static_cast<float>(freqHi));
        engine.setFreqDrift(static_cast<float>(freqDrift));

        for (std::size_t i = 0; i < EcosystemEngine::kNumKinds; ++i) {
            for (std::size_t j = 0; j < EcosystemEngine::kNumKinds; ++j) {
                engine.setAffinity(static_cast<EcosystemEngine::Kind>(i),
                                   static_cast<EcosystemEngine::Kind>(j),
                                   static_cast<float>(affinity[i][j]));
            }
        }
    }
};

/// @brief One entry of SC-012's static knob table.
struct Knob {
    const char* name;            ///< the Appendix-A name, which is what the
                                 ///< exemption list is written in
    double RuleConfig::* member;  ///< `std::size_t` knobs ride a `double` field
};

/// @brief SC-012 (a): the static knob table, one entry per Appendix-A knob.
///
/// The `static_assert` below is the structural half of clause (a) (plan addition
/// A-2): adding a setter without raising `kConfigKnobCount` is a lie the author
/// has to write by hand, and raising the constant without extending this table
/// FAILS TO COMPILE.
constexpr std::array<Knob, EcosystemEngine::kConfigKnobCount> kKnobs{{
    // ---- prepare-time (Appendix A) -----------------------------------------
    {"agentCount", &RuleConfig::agentCount},
    {"resourceCells", &RuleConfig::resourceCells},
    {"energyBudget", &RuleConfig::energyBudget},
    {"initialPoolFraction", &RuleConfig::initialPoolFraction},
    {"stepIntervalChunks", &RuleConfig::stepIntervalChunks},
    // ---- runtime ------------------------------------------------------------
    {"kernelSigma", &RuleConfig::kernelSigma},
    {"exchangeRate", &RuleConfig::exchangeRate},
    {"predation", &RuleConfig::predation},
    {"preyFloor", &RuleConfig::preyFloorShares},
    {"capacity", &RuleConfig::capacityShares},
    {"leakRate", &RuleConfig::leakRate},
    {"leakExponent", &RuleConfig::leakExponent},
    {"moveRate", &RuleConfig::moveRate},
    {"maxSpeed", &RuleConfig::maxSpeed},
    {"forageRate", &RuleConfig::forageRate},
    {"crowding", &RuleConfig::crowding},
    {"crowdingRadius", &RuleConfig::crowdingRadius},
    {"syncRate", &RuleConfig::syncRate},
    {"cellCapacity", &RuleConfig::cellCapacityShares},
    {"regenRate", &RuleConfig::regenRate},
    {"grazeRate", &RuleConfig::grazeRate},
    {"feedRate", &RuleConfig::feedRate},
    {"satiation", &RuleConfig::satiationShares},
    {"appetiteDepth", &RuleConfig::appetiteDepth},
    {"freqLo", &RuleConfig::freqLo},
    {"freqHi", &RuleConfig::freqHi},
    {"freqDrift", &RuleConfig::freqDrift},
    {"affinity", &RuleConfig::affinityMagnitude},
}};
static_assert(kKnobs.size() == EcosystemEngine::kConfigKnobCount,
              "a knob was added to the engine without a fuzz-coverage entry");

// EACH EXEMPTION ARRAY IS SIZED TO ITS CONTENTS - no sentinel, no padding to a
// common length. Clause (c) below is an UNCONDITIONAL loop over every element,
// and a nullptr entry would be strcmp'd: UB, and on a hardened runtime a crash
// in the [long] lane rather than a test failure, in the one test whose whole
// purpose is that nothing is exempted by omission or by typo.
//
// The list is NORMATIVE (spec SC-012) and is exactly:
//   energyBudget        pinned at its default in both boxes - it is FR-006
//                       prepare-time, it is SC-001 (c)'s conservation reference,
//                       and varying it would make the relative drift figures
//                       incomparable across configurations (run.js:415).
//   stepIntervalChunks  pinned at 8, the tuned dt; the prototype has no such
//                       knob, and SC-010 (a) gates the band instead.
//   feedRate            SANE BOX ONLY - pinned at 0 by design (run.js:363),
//                       exactly as run.js:415 exempts it there.
// No other exemption may be added without amending the criterion.
constexpr std::array<const char*, 2> kExemptHostile{{"energyBudget", "stepIntervalChunks"}};
constexpr std::array<const char*, 3> kExemptSane{
    {"energyBudget", "stepIntervalChunks", "feedRate"}};

/// @brief Is @p name on @p exempt? (`std::strcmp`, never pointer equality - the
///        literals live in two arrays and need not be pooled.)
[[nodiscard]] bool isExempt(const char* name, std::span<const char* const> exempt) {
    for (const char* e : exempt) {
        if (std::strcmp(name, e) == 0) {
            return true;
        }
    }
    return false;
}

/// @brief Is @p name a member of the static knob table?
[[nodiscard]] bool knobTableContains(const char* name) {
    for (const Knob& k : kKnobs) {
        if (std::strcmp(name, k.name) == 0) {
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------------------
// The two boxes (plan S10.3), and their three documented departures
// ------------------------------------------------------------------------------
// 1. `dimensions` IS DROPPED. FR-012 fixes the habitat at 2-D, so the
//    prototype's 500/500 - drawn over 1-D and 2-D alike - covers a SUPERSET of
//    the shipped geometry (spec SC-001's own note). The prototype's
//    `rng.nextUnipolar()` draw for it is dropped with it; the stream is not
//    aligned to the JS one in any case (see `rangeD`).
// 2. THE FOUR SHARE-UNIT KNOBS (preyFloor, capacity, cellCapacity, satiation)
//    ARE DRAWN OVER THEIR APPENDIX-A SHARE RANGES, not the prototype's absolute
//    ranges - FR-008 restates the same box in share units. For the HOSTILE box
//    the two are the same region exactly: the Appendix-A share range is the
//    prototype's absolute range times the Appendix-A denominators
//    (energyBudget / agentCount = 1/32, energyBudget / resourceCells = 1/64):
//      preyFloor    run.js 0.0  - 0.05 abs  x32 -> [0,    1.6 ] shares
//      capacity     run.js 0.01 - 1.0  abs  x32 -> [0.32, 32  ] shares
//      cellCapacity run.js 0.005- 0.2  abs  x64 -> [0.32, 12.8] cell-shares
//      satiation    run.js 0.0  - 0.5  abs  x32 -> [0,    16  ] shares
//    For the SANE box the same conversion is applied to the prototype's sane
//    bands (they are a musically reasonable band around the default, so the full
//    Appendix-A range would destroy exactly the property that makes the box
//    sane):
//      preyFloor    run.js 0.002- 0.02 abs  x32 -> [0.064, 0.64] shares
//      capacity     run.js 0.05 - 1.0  abs  x32 -> [1.6,  32   ] shares
//      cellCapacity run.js 0.02 - 0.1  abs  x64 -> [1.28,  6.4 ] cell-shares
//      satiation    run.js 0.02 - 0.06 abs  x32 -> [0.64,  1.92] shares
//    Every converted bound lands inside the corresponding setter's clamp
//    (ecosystem_engine.h:486-504, :590-597, :628-635), so no draw is silently
//    clamped away and clause (b) measures what was actually drawn.
// 3. THE PROTOTYPE'S META-RNG SEEDS ARE KEPT (0xf0f0f0 hostile, 0x5a5e0001 sane,
//    run.js:400) so the batches are COMPARABLE, while the comparison itself
//    stays statistical, never per-config (see `rangeD`).

/// @brief Meta-RNG seed for the hostile batch (`run.js:400`).
constexpr std::uint32_t kHostileMetaSeed = 0xf0f0f0u;
/// @brief Meta-RNG seed for the sane batch (`run.js:400`).
constexpr std::uint32_t kSaneMetaSeed = 0x5a5e0001u;

/// @brief `run.js:283-296` - a symmetric 5x5 affinity matrix drawn over
///        [@p lo, @p hi], and the realised max |entry| that represents it in the
///        knob table.
///
/// The symmetrisation is the prototype's: an asymmetric matrix means i pulls j
/// while j pushes i, which is a momentum source. (The ENGINE does not symmetrise
/// - FR-031 is per entry, ecosystem_engine.h:684-690 - so this is a property of
/// the fuzz box, not of the component.)
void drawAffinity(Xorshift32& rng, double lo, double hi, RuleConfig& cfg) {
    for (std::size_t i = 0; i < EcosystemEngine::kNumKinds; ++i) {
        for (std::size_t j = 0; j < EcosystemEngine::kNumKinds; ++j) {
            cfg.affinity[i][j] = rangeD(rng, lo, hi);
        }
    }
    for (std::size_t i = 0; i < EcosystemEngine::kNumKinds; ++i) {
        for (std::size_t j = i + 1; j < EcosystemEngine::kNumKinds; ++j) {
            cfg.affinity[j][i] = cfg.affinity[i][j];
        }
    }
    double magnitude = 0.0;
    for (std::size_t i = 0; i < EcosystemEngine::kNumKinds; ++i) {
        for (std::size_t j = 0; j < EcosystemEngine::kNumKinds; ++j) {
            magnitude = std::max(magnitude, std::abs(cfg.affinity[i][j]));
        }
    }
    cfg.affinityMagnitude = magnitude;
}

/// @brief The HOSTILE box (`run.js:302-332`): every knob over its full plausible
///        range, INCLUDING values that are dead by construction (no regrowth, no
///        grazing, a leak that empties an agent in a second).
///
/// This is the BOUNDEDNESS fuzz (SC-001); liveness in this box is informational.
/// Draw ORDER is the prototype's, key for key.
[[nodiscard]] RuleConfig makeHostileConfig(std::uint32_t configSeed) {
    Xorshift32 rng(configSeed);
    RuleConfig c{};
    c.agentCount = std::round(rangeD(rng, 24.0, 48.0));
    // `dimensions` dropped - departure 1.
    c.energyBudget = 1.0;  // EXEMPT: pinned, SC-001 (c)'s conservation reference
    c.initialPoolFraction = rangeD(rng, 0.1, 0.9);
    c.kernelSigma = rangeD(rng, 0.01, 0.35);
    c.exchangeRate = rangeD(rng, 0.0, 3.0);
    c.predation = rangeD(rng, 0.0, 1.0);
    c.capacityShares = rangeD(rng, 0.32, 32.0);
    c.leakExponent = rangeD(rng, 1.0, 2.5);
    c.moveRate = rangeD(rng, 0.0, 0.5);
    c.maxSpeed = rangeD(rng, 0.001, 0.05);
    c.forageRate = rangeD(rng, 0.0, 0.05);
    c.syncRate = rangeD(rng, 0.0, 0.5);
    c.resourceCells = std::round(rangeD(rng, 8.0, 96.0));
    c.cellCapacityShares = rangeD(rng, 0.32, 12.8);
    c.regenRate = rangeD(rng, 0.0, 1.0);
    c.grazeRate = rangeD(rng, 0.0, 3.0);
    c.feedRate = rangeD(rng, 0.0, 1.0);
    c.leakRate = rangeD(rng, 0.0, 1.0);
    c.freqLo = rangeD(rng, 0.0005, 0.005);
    c.freqHi = rangeD(rng, 0.006, 0.05);
    c.freqDrift = rangeD(rng, 0.0, 0.0002);
    c.appetiteDepth = rangeD(rng, 0.0, 1.0);
    c.satiationShares = rangeD(rng, 0.0, 16.0);
    c.crowding = rangeD(rng, 0.0, 0.2);
    c.crowdingRadius = rangeD(rng, 0.005, 0.05);
    c.preyFloorShares = rangeD(rng, 0.0, 1.6);
    drawAffinity(rng, -2.0, 2.0, c);
    c.stepIntervalChunks =
        static_cast<double>(EcosystemEngine::kDefaultStepIntervalChunks);  // EXEMPT
    return c;
}

/// @brief The SANE box (`run.js:338-377`): the region Phase 10's concept macros
///        would plausibly reach - every knob within a musically reasonable band
///        around the default, nothing dead by construction.
///
/// This is the LIVENESS fuzz (SC-013). The two narrowed bands carry the
/// prototype's own measured reasons.
[[nodiscard]] RuleConfig makeSaneConfig(std::uint32_t configSeed) {
    Xorshift32 rng(configSeed);
    RuleConfig c{};
    c.agentCount = std::round(rangeD(rng, 24.0, 48.0));
    // `dimensions` is 2 by FR-012 - departure 1, and the sane box drew no random
    // value for it in the prototype either (`run.js:345`).
    c.energyBudget = 1.0;  // EXEMPT
    c.initialPoolFraction = rangeD(rng, 0.3, 0.7);
    c.kernelSigma = rangeD(rng, 0.02, 0.06);
    c.exchangeRate = rangeD(rng, 0.0, 1.0);
    c.predation = rangeD(rng, 0.0, 1.0);
    c.capacityShares = rangeD(rng, 1.6, 32.0);
    // leakExponent > ~1.3 kills: at per-agent energies of ~0.03 a superlinear
    // leak all but vanishes, agents fill to capacity and sit (sane fuzz round 2:
    // 63 % alive at 1.0-1.33, 13 % at 1.65-2.0). Macros stay in [1, 1.3].
    c.leakExponent = rangeD(rng, 1.0, 1.3);
    c.moveRate = rangeD(rng, 0.0, 0.5);
    c.maxSpeed = rangeD(rng, 0.01, 0.05);
    // `run.js:357`: max(defaultForageRate * 3, 0.01) = max(0.03, 0.01) = 0.03.
    c.forageRate = rangeD(rng, 0.0, 0.03);
    c.syncRate = rangeD(rng, 0.0, 0.1);
    c.resourceCells = std::round(rangeD(rng, 32.0, 96.0));
    c.cellCapacityShares = rangeD(rng, 1.28, 6.4);
    c.regenRate = rangeD(rng, 0.02, 0.2);
    c.grazeRate = rangeD(rng, 0.5, 3.0);
    c.feedRate = 0.0;  // EXEMPT IN THIS BOX ONLY: off by design (run.js:363)
    c.leakRate = rangeD(rng, 0.02, 0.15);
    c.freqLo = rangeD(rng, 0.001, 0.003);
    c.freqHi = rangeD(rng, 0.01, 0.03);
    c.freqDrift = rangeD(rng, 0.0, 0.0001);
    // A shallow gate starves the dynamics (25 % alive at 0.4-0.6 vs 47 % at
    // 0.8-1.0 in sane fuzz round 2): the gate is THE load-bearing rule.
    c.appetiteDepth = rangeD(rng, 0.6, 1.0);
    // Satiation is OFF by default and cost 31 % activity in ablation; the box
    // leaves it off in most configs and probes a mild setting in the rest. The
    // roll is drawn unconditionally and the band only on the else branch, which
    // is the prototype's ternary exactly (`run.js:370`).
    const double satiationRoll = static_cast<double>(rng.nextUnipolar());
    c.satiationShares = (satiationRoll < 0.7) ? 0.0 : rangeD(rng, 0.64, 1.92);
    // `run.js:373`: [max(defaultCrowding * 0.5, 0.01), max(defaultCrowding * 2, 0.05)]
    //             = [max(0.025, 0.01), max(0.1, 0.05)] = [0.025, 0.1].
    c.crowding = rangeD(rng, 0.025, 0.1);
    c.crowdingRadius = rangeD(rng, 0.01, 0.04);
    c.preyFloorShares = rangeD(rng, 0.064, 0.64);
    drawAffinity(rng, -1.5, 1.5, c);
    c.stepIntervalChunks =
        static_cast<double>(EcosystemEngine::kDefaultStepIntervalChunks);  // EXEMPT
    return c;
}

/// @brief Build a seeded batch, the prototype's way (`run.js:400-406`): one
///        meta-RNG per batch, one `next()` per configuration, each configuration
///        generated from its own `Xorshift32`.
///
/// Sizes are the criteria's own: SC-001 draws 1000 hostile, SC-013 draws 500
/// sane. SC-012 asserts coverage over exactly those batches, so it cannot pass
/// on a batch the other criteria do not actually run.
[[nodiscard]] std::vector<RuleConfig> buildBatch(bool sane, std::size_t count) {
    Xorshift32 meta(sane ? kSaneMetaSeed : kHostileMetaSeed);
    std::vector<RuleConfig> configs;
    configs.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const std::uint32_t configSeed = meta.next();
        configs.push_back(sane ? makeSaneConfig(configSeed) : makeHostileConfig(configSeed));
    }
    return configs;
}

/// @brief Batch size for the hostile box (SC-001's own 1000, roadmap 393-394).
constexpr std::size_t kHostileBatchSize = 1000;
/// @brief Batch size for the sane box (SC-013's own 500).
constexpr std::size_t kSaneBatchSize = 500;

/// @brief SC-012 clause (b), for one box: every non-exempt knob must actually
///        VARY across the batch, or the test fails outright naming the knob.
void requireEveryKnobVaries(const std::vector<RuleConfig>& batch,
                            std::span<const char* const> exempt,
                            const char* boxName) {
    REQUIRE_FALSE(batch.empty());
    for (const Knob& knob : kKnobs) {
        if (isExempt(knob.name, exempt)) {
            continue;
        }
        double lo = batch.front().*(knob.member);
        double hi = lo;
        for (const RuleConfig& cfg : batch) {
            const double v = cfg.*(knob.member);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (!(lo < hi)) {
            FAIL(std::string(boxName) + " box: knob '" + knob.name +
                 "' never varies across the batch (constant " + std::to_string(lo) +
                 ") - the fuzzer proves NOTHING about this rule");
        }
    }
}

}  // namespace

// ==============================================================================
// SC-012 - The fuzz harness cannot silently stop testing a rule
// ==============================================================================
//
// THIS CRITERION IS BUILT FIRST because it gates the other two fuzz criteria:
// SC-001's "0 unbounded / 1000" and SC-013's ">= 70 % alive" are worth their
// thresholds only if the harness is provably still exercising every rule.
//
// The prototype's FIRST fuzzer omitted `predation`, `capacity`, `leakExponent`
// and the whole resource field, so `predation` sat at its 0.5 default - THE
// EXACT VALUE THAT ZEROES THE EXCHANGE RULE (FR-021) - and reported
// "0 unbounded / 1000" while never visiting the regime already known to break
// conservation (`run.js:381-387`, `FINDINGS.md:327-328`).
//
// FALSIFICATION (tasks T019): delete one knob's write from `makeHostileConfig`
// - e.g. the `c.predation = ...` line - and clause (b) must fail naming
// `predation`. Restore afterwards.
//
// Tagged [long] with the rest of this TU: it builds 1500 configurations (no
// simulation), which is cheap, but it belongs with the batches it certifies.
TEST_CASE("EcosystemEngine_FuzzCoverageIsComplete", "[ecosystem_engine][long]") {
    // ---- (a) the table length equals kConfigKnobCount ------------------------
    // The structural half is the static_assert beside kKnobs (plan addition
    // A-2): raising the constant without extending the table fails to COMPILE.
    // The runtime half is here so the criterion shows up as an assertion in the
    // report rather than only as a compile-time fact.
    STATIC_REQUIRE(kKnobs.size() == EcosystemEngine::kConfigKnobCount);
    REQUIRE(kKnobs.size() == EcosystemEngine::kConfigKnobCount);
    REQUIRE(EcosystemEngine::kConfigKnobCount == 28u);

    // ---- (c) every exemption is present in the knob table --------------------
    // UNCONDITIONAL loops over every element of both arrays (which is why
    // neither carries a nullptr sentinel): a knob can never be exempted by
    // omission or by a typo that matches nothing.
    for (const char* name : kExemptHostile) {
        INFO("hostile exemption: " << name);
        REQUIRE(knobTableContains(name));
    }
    for (const char* name : kExemptSane) {
        INFO("sane exemption: " << name);
        REQUIRE(knobTableContains(name));
    }

    // The exemption list is normative (spec SC-012): hostile exempts exactly
    // energyBudget and stepIntervalChunks; sane exempts those plus feedRate.
    // Pinning the SIZES here makes "one more exemption slipped in" a test
    // failure rather than a silently wider hole.
    REQUIRE(kExemptHostile.size() == 2u);
    REQUIRE(kExemptSane.size() == 3u);
    for (const char* name : kExemptHostile) {
        INFO("hostile exemption must also be exempt in the sane box: " << name);
        REQUIRE(isExempt(name, kExemptSane));
    }

    // ---- (b) every non-exempt knob varies across the batch -------------------
    const std::vector<RuleConfig> hostile = buildBatch(false, kHostileBatchSize);
    const std::vector<RuleConfig> sane = buildBatch(true, kSaneBatchSize);
    REQUIRE(hostile.size() == kHostileBatchSize);
    REQUIRE(sane.size() == kSaneBatchSize);

    requireEveryKnobVaries(hostile, kExemptHostile, "hostile");
    requireEveryKnobVaries(sane, kExemptSane, "sane");

    // The two exempted-by-pinning knobs are asserted to be genuinely PINNED,
    // not merely unmeasured: an exemption that hid a varying knob would make
    // SC-001 (c)'s relative-drift figures incomparable across configurations,
    // which is the reason energyBudget is exempt in the first place.
    for (const RuleConfig& cfg : hostile) {
        REQUIRE(cfg.energyBudget == 1.0);
        REQUIRE(cfg.stepIntervalChunks ==
                static_cast<double>(EcosystemEngine::kDefaultStepIntervalChunks));
    }
    for (const RuleConfig& cfg : sane) {
        REQUIRE(cfg.energyBudget == 1.0);
        REQUIRE(cfg.stepIntervalChunks ==
                static_cast<double>(EcosystemEngine::kDefaultStepIntervalChunks));
        REQUIRE(cfg.feedRate == 0.0);
    }
}

// ==============================================================================
// SC-001 (a)-(d) - Boundedness and conservation under hostile rule fuzzing
// ==============================================================================
//
// The anonymous namespace is REOPENED rather than the T019 block above being
// edited: that block is the shared harness every fuzz criterion consumes, and
// nothing below it changes a single draw.
//
// tasks.md T020 / plan S10.1, S10.5, S14 D-P.
namespace {

using Krate::DSP::detail::isFinite;

/// @brief Main batch duration: the 900 s the prototype's 500/500 reference was
///        measured at (`run.js:401`, spec SC-001).
constexpr double kHostileSeconds = 900.0;

/// @brief The spec's own second sub-batch: 50 configurations at 1800 s, which
///        covers the longer horizon without multiplying the batch.
constexpr std::size_t kSubBatchConfigs = 50;
constexpr double kSubBatchSeconds = 1800.0;

/// @brief Sampling stride for clauses (a)-(c): 93 control steps, i.e. ~1.008 Hz
///        at the FR-082 default control rate of 93.75 Hz (spec SC-001;
///        `run.js:432` samples every 94 at the prototype's own rate).
constexpr std::uint64_t kSampleEverySteps = 93;

/// @brief SC-001 (c): relative total-energy drift bound.
constexpr double kDriftTolerance = 1.0e-9;

/// @brief The batch's wall-clock budget. PART OF THE CRITERION, not a hint.
/// SC-001's runtime budget. Written as 30 minutes; the batch measured 32.9 min
/// alone and 43 min after a full suite (Release, pinned, 2026-09-16) with every
/// clause green and the exact CPU levers exhausted, and the USER amended it to
/// 60 minutes (spec SC-001, plan R-10). The config count, the durations and the
/// perturb schedule are unchanged - those are not to be shrunk (FR-085).
constexpr double kWallClockBudgetSeconds = 60.0 * 60.0;

/// @brief Steps one configuration runs at the FR-082 default grid: 900 s is
///        84 375 control steps, 1800 s is 168 750 (spec SC-001).
constexpr std::uint64_t kStepsAt900s = 84375;
constexpr std::uint64_t kStepsAt1800s = 168750;

/// @brief Seed for the perturb schedule of clause (d). ONE stream for the whole
///        batch, so the schedule is reproducible end to end.
constexpr std::uint32_t kPerturbScheduleSeed = 0x5ed0d1u;

/// @brief Non-finite binary32 bit patterns for clause (d)'s amounts.
///
/// BIT PATTERNS, never `std::numeric_limits`: under -ffast-math the library
/// constants fold to finite values and the arm would silently test nothing
/// (ecosystem_engine_nonfinite_test.cpp:94-107 carries the same idiom).
constexpr std::array<std::uint32_t, 3> kPerturbNonFiniteBits{{
    0x7FC00000u,  // quiet NaN
    0x7F800000u,  // +Inf
    0xFF800000u,  // -Inf
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time.
[[nodiscard]] float makeNonFiniteFloat(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief What the perturb schedule actually fired, so clause (d) can be shown
///        to be NON-VACUOUS rather than merely present.
struct PerturbStats {
    std::uint64_t calls = 0;            ///< total `perturbAgent` calls
    std::uint64_t inRange = 0;          ///< index < getAgentCount()
    std::uint64_t outOfRange = 0;       ///< index >= getAgentCount() (FR-064 no-op)
    std::uint64_t nonFiniteAmount = 0;  ///< amount built from a bit pattern
    std::uint64_t aboveUpperClamp = 0;  ///< amount > +1: FR-071's clamp past the top
    std::uint64_t belowLowerClamp = 0;  ///< amount < -1: FR-071's clamp past the bottom
};

/// @brief Index span of the schedule: [0, 2 * kMaxAgents), so roughly half the
///        draws land outside ANY prepared agent count and FR-064's silent no-op
///        is exercised on every configuration.
constexpr std::uint32_t kPerturbIndexSpan =
    2u * static_cast<std::uint32_t>(EcosystemEngine::kMaxAgents);

/// @brief One scheduled `perturbAgent` call (SC-001 (d), plan S14 D-P).
///
/// Amounts are drawn over [-2, +2] so FR-071's `clamp(amount, -1, 1)` is
/// exercised past BOTH ends; one call in eight instead carries a non-finite
/// amount, which FR-064 (1) must reject outright.
void firePerturb(EcosystemEngine& engine, Xorshift32& rng, PerturbStats& stats) noexcept {
    const auto index = static_cast<std::size_t>(rng.next() % kPerturbIndexSpan);

    float amount = 0.0f;
    if ((rng.next() % 8u) == 0u) {
        const auto which = static_cast<std::size_t>(rng.next() % 3u);
        amount = makeNonFiniteFloat(kPerturbNonFiniteBits[which]);
        ++stats.nonFiniteAmount;
    } else {
        amount = -2.0f + 4.0f * rng.nextUnipolar();
        if (amount > 1.0f) {
            ++stats.aboveUpperClamp;
        }
        if (amount < -1.0f) {
            ++stats.belowLowerClamp;
        }
    }

    if (index < engine.getAgentCount()) {
        ++stats.inRange;
    } else {
        ++stats.outOfRange;
    }
    ++stats.calls;

    engine.perturbAgent(index, amount);
}

/// @brief SC-001 (a): every agent energy, position and phase, plus the pool,
///        read through `detail::isFinite` (bit pattern) - NEVER `std::isnan`,
///        which folds away under -ffast-math.
[[nodiscard]] bool allStateFinite(const EcosystemEngine& engine) noexcept {
    if (!isFinite(engine.getPoolEnergy())) {
        return false;
    }
    const std::size_t agents = engine.getAgentCount();
    for (std::size_t i = 0; i < agents; ++i) {
        if (!isFinite(engine.getAgentEnergy(i)) || !isFinite(engine.getAgentPositionX(i)) ||
            !isFinite(engine.getAgentPositionY(i)) || !isFinite(engine.getAgentPhase(i))) {
            return false;
        }
    }
    return true;
}

/// @brief The first violation found, kept as DATA so the measured table can be
///        printed BEFORE the assertion that fails the case.
///
/// A REQUIRE inside the 92.8 M-step loop would throw on the first violation and
/// take the wall-clock table with it - and that table is exactly what FR-085's
/// stop-and-surface rule needs. The batch therefore records and stops.
struct FuzzFailure {
    bool failed = false;
    std::string what;
};

/// @brief What the batch measured, for the printed table and the non-vacuity
///        assertions.
struct BatchStats {
    std::size_t configsRun = 0;
    std::uint64_t stepsRun = 0;
    std::uint64_t sampledSteps = 0;
    double worstDrift = 0.0;
    std::uint64_t maxConservationViolations = 0;
    std::uint64_t maxContainments = 0;
    PerturbStats perturb{};
};

/// @brief The per-configuration SIMULATION seeds of a batch, in batch order.
///
/// MIRRORS `buildBatch` EXACTLY - same meta seed, one `meta.next()` per
/// configuration - because the prototype runs configuration k at `seeds[k]`
/// (`run.js:409-413`, `:432`). A single fixed simulation seed would collapse
/// 1000 initial states onto one, and the batch would be testing the knobs only.
[[nodiscard]] std::vector<std::uint32_t> buildBatchSeeds(bool sane, std::size_t count) {
    Xorshift32 meta(sane ? kSaneMetaSeed : kHostileMetaSeed);
    std::vector<std::uint32_t> seeds;
    seeds.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        seeds.push_back(meta.next());
    }
    return seeds;
}

/// @brief Run ONE hostile configuration for @p seconds on the REUSED @p engine,
///        with the perturb schedule of clause (d) running throughout.
///
/// FR-065's counters clear on prepare() (Clarification Q8), which is what makes
/// one engine instance legitimate across all 1050 configurations.
///
/// @return false on the first violation, with @p failure filled in.
[[nodiscard]] bool runHostileConfig(EcosystemEngine& engine, const RuleConfig& cfg,
                                    std::uint32_t seed, double seconds, std::size_t configIndex,
                                    std::uint64_t expectedSteps, Xorshift32& perturbRng,
                                    BatchStats& stats, FuzzFailure& failure) {
    cfg.applyTo(engine);
    engine.setSeed(seed);  // re-derives the initial state from this config's own seed

    const std::size_t samplesPerStep =
        engine.getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples;
    const auto totalSteps = static_cast<std::uint64_t>(
        std::llround(seconds * engine.getSampleRate() / static_cast<double>(samplesPerStep)));

    std::ostringstream where;
    where << "config " << configIndex << " (seed 0x" << std::hex << seed << std::dec << ", "
          << seconds << " s, predation " << cfg.predation << ", leakRate " << cfg.leakRate
          << ", exchangeRate " << cfg.exchangeRate << ")";

    if (totalSteps != expectedSteps) {
        std::ostringstream os;
        os << where.str() << ": FIXTURE ERROR - " << totalSteps << " control steps scheduled, "
           << expectedSteps << " expected. The batch is not running the specified workload.";
        failure.failed = true;
        failure.what = os.str();
        return false;
    }

    std::uint64_t done = 0;
    while (done < totalSteps) {
        const std::uint64_t take = std::min(kSampleEverySteps, totalSteps - done);
        engine.processChunk(static_cast<std::size_t>(take) * samplesPerStep);
        done += take;

        // Clause (d): the schedule fires BEFORE the sampled assertions, so every
        // clause below is measured on a state a perturbation has just touched.
        firePerturb(engine, perturbRng, stats.perturb);

        ++stats.sampledSteps;

        // ---- (a) no non-finite value anywhere in the state -------------------
        if (!allStateFinite(engine)) {
            std::ostringstream os;
            os << where.str() << ": SC-001 (a) - a non-finite agent energy, position, phase or "
               << "pool at control step " << done << " (pool " << engine.getPoolEnergy() << ")";
            failure.failed = true;
            failure.what = os.str();
            return false;
        }

        // ---- (b) the two counters, SEPARATELY --------------------------------
        const std::uint64_t violations = engine.getConservationViolationCount();
        const std::uint64_t containments = engine.getNonFiniteContainmentCount();
        stats.maxConservationViolations = std::max(stats.maxConservationViolations, violations);
        stats.maxContainments = std::max(stats.maxContainments, containments);
        if (violations != 0u) {
            std::ostringstream os;
            os << where.str() << ": SC-001 (b) - getConservationViolationCount() == " << violations
               << " by control step " << done << " (the pool went NEGATIVE; this is FR-056, NOT "
               << "non-finite containment, which stands at " << containments << ")";
            failure.failed = true;
            failure.what = os.str();
            return false;
        }
        if (containments != 0u) {
            std::ostringstream os;
            os << where.str()
               << ": SC-001 (b) - getNonFiniteContainmentCount() == " << containments
               << " by control step " << done << " (FR-083's guard ladder repaired a non-finite "
               << "value; this is NOT a pool-negative violation, which stands at " << violations
               << ")";
            failure.failed = true;
            failure.what = os.str();
            return false;
        }

        // ---- (c) relative total-energy drift ---------------------------------
        const double budget = engine.getEnergyBudget();
        const double drift = std::fabs(engine.getTotalEnergy() - budget) / budget;
        stats.worstDrift = std::max(stats.worstDrift, drift);
        if (!(drift <= kDriftTolerance)) {
            std::ostringstream os;
            os << where.str() << ": SC-001 (c) - relative drift " << std::scientific
               << std::setprecision(3) << drift << " > " << kDriftTolerance << " at control step "
               << done;
            failure.failed = true;
            failure.what = os.str();
            return false;
        }
    }

    // The engine really stepped the scheduled grid (plan addition A-3's counter).
    if (engine.getControlStepCount() != totalSteps) {
        std::ostringstream os;
        os << where.str() << ": FIXTURE ERROR - getControlStepCount() == "
           << engine.getControlStepCount() << " after " << totalSteps << " scheduled steps.";
        failure.failed = true;
        failure.what = os.str();
        return false;
    }

    ++stats.configsRun;
    stats.stepsRun += done;
    return true;
}

}  // namespace

// ==============================================================================
// SC-001 (a)-(d) - the boundedness batch
// ==============================================================================
//
// 1000 seeded hostile configurations x 900 simulated seconds (84 375 control
// steps each) plus a sub-batch of 50 configurations at 1800 s, ONE engine
// instance reused throughout (FR-065's counters clear on prepare(),
// Clarification Q8), with the seeded perturbAgent schedule of clause (d)
// running INSIDE the batch, throughout every configuration (plan S14 D-P).
//
// 92 812 500 control steps in total. The prototype's reference under the same
// rules at 900 s: 500/500 with 0 non-finite, 0 pool-negative, 0 unbounded,
// drift 2.4e-15 - 1.9e-13 (FINDINGS.md:21-23, :295-300). A regression in either
// conservation guard - FR-023's two-pass exchange, FR-054's single withdrawal
// balance - fails HERE FIRST.
//
// RUNTIME IS PART OF THE CRITERION: <= 60 minutes single-threaded in Release
// (30 as written; amended by the user on 2026-09-16 from the measured 32.9 min,
// see kWallClockBudgetSeconds), and the case prints its own wall clock either way. On a miss, FR-085's
// stop-and-surface rule applies: the config count (the roadmap's own 1000), the
// duration (the 900 s the 500/500 reference was measured at) and the perturb
// schedule's coverage are NOT to be shrunk - reduce per-step cost, or put the
// measured table below to the user. A Debug build is 10-50x slower and will
// never fit; the [long] lane is Release-only.
//
// FALSIFICATION (tasks T020): break FR-054's single withdrawal balance (or
// FR-023's second exchange pass) and clause (b) or (c) must fail HERE, naming
// the configuration. Restore afterwards.
TEST_CASE("EcosystemEngine_HostileFuzzStaysBounded", "[ecosystem_engine][long]") {
    const std::vector<RuleConfig> configs = buildBatch(false, kHostileBatchSize);
    const std::vector<std::uint32_t> seeds = buildBatchSeeds(false, kHostileBatchSize);
    REQUIRE(configs.size() == kHostileBatchSize);
    REQUIRE(seeds.size() == kHostileBatchSize);
    STATIC_REQUIRE(kSubBatchConfigs <= kHostileBatchSize);

    // ONE engine, heap-allocated: the object is ~23.5 KB (plan S9) and a
    // stack-local of that size in a test is exactly the shape plan R-9 warns
    // about.
    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();

    Xorshift32 perturbRng(kPerturbScheduleSeed);
    BatchStats stats{};
    FuzzFailure failure{};

    const auto wallStart = std::chrono::steady_clock::now();

    // ---- the main batch: 1000 x 900 s ---------------------------------------
    for (std::size_t k = 0; k < kHostileBatchSize; ++k) {
        if (!runHostileConfig(*engine, configs[k], seeds[k], kHostileSeconds, k, kStepsAt900s,
                              perturbRng, stats, failure)) {
            break;
        }
    }

    // ---- the sub-batch: the first 50 configurations at 1800 s ----------------
    // The spec's own "second sub-batch of 50 configurations at 1800 s"; the
    // configurations are the batch's own first 50, re-prepared from the same
    // seeds, so the longer horizon is covered without a second draw.
    const std::size_t mainBatchConfigs = stats.configsRun;
    if (!failure.failed) {
        for (std::size_t k = 0; k < kSubBatchConfigs; ++k) {
            if (!runHostileConfig(*engine, configs[k], seeds[k], kSubBatchSeconds, k,
                                  kStepsAt1800s, perturbRng, stats, failure)) {
                break;
            }
        }
    }

    const auto wallEnd = std::chrono::steady_clock::now();
    const double wallSeconds = std::chrono::duration<double>(wallEnd - wallStart).count();

    // -------------------------------------------------------------------------
    // The measured table, printed BEFORE any verdict so that a clause violation
    // and a budget miss both arrive with their numbers attached (FR-085).
    // -------------------------------------------------------------------------
    const std::uint64_t expectedSteps =
        kStepsAt900s * static_cast<std::uint64_t>(kHostileBatchSize) +
        kStepsAt1800s * static_cast<std::uint64_t>(kSubBatchConfigs);
    {
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-001 (a)-(d) hostile fuzz - MEASURED\n"
           << "-----------------------------------------------------------------------------\n"
           << "  main batch            : " << mainBatchConfigs << " / " << kHostileBatchSize
           << " configs x " << kHostileSeconds << " s (" << kStepsAt900s << " steps each)\n"
           << "  sub-batch             : " << (stats.configsRun - mainBatchConfigs) << " / "
           << kSubBatchConfigs << " configs x " << kSubBatchSeconds << " s (" << kStepsAt1800s
           << " steps each)\n"
           << "  control steps run     : " << stats.stepsRun << " of " << expectedSteps
           << " scheduled\n"
           << "  sampled steps         : " << stats.sampledSteps << " (every " << kSampleEverySteps
           << " steps, ~1 Hz)\n"
           << "  worst relative drift  : " << std::scientific << std::setprecision(3)
           << stats.worstDrift << "  (bound " << kDriftTolerance
           << ", prototype 2.4e-15 - 1.9e-13)\n"
           << "  conservation violations (max over configs): " << stats.maxConservationViolations
           << "\n"
           << "  non-finite containments (max over configs): " << stats.maxContainments << "\n"
           << "  perturb schedule      : " << stats.perturb.calls << " calls - "
           << stats.perturb.inRange << " in range, " << stats.perturb.outOfRange
           << " out of range, " << stats.perturb.nonFiniteAmount << " non-finite amounts, "
           << stats.perturb.aboveUpperClamp << " above +1, " << stats.perturb.belowLowerClamp
           << " below -1\n"
           << "  wall clock            : " << std::fixed << std::setprecision(1) << wallSeconds
           << " s (" << std::setprecision(2) << (wallSeconds / 60.0) << " min), budget "
           << (kWallClockBudgetSeconds / 60.0) << " min\n";
        if (wallSeconds > kWallClockBudgetSeconds) {
            os << "-----------------------------------------------------------------------------\n"
               << "  *** FR-085 STOP-AND-SURFACE APPLIES: the batch is " << std::setprecision(2)
               << (wallSeconds / kWallClockBudgetSeconds) << "x over its "
               << std::setprecision(0) << (kWallClockBudgetSeconds / 60.0) << "-minute budget.\n"
               << "  *** HALT and put THIS TABLE to the USER. NO AGENT MAY shrink the config\n"
               << "  *** count (the roadmap's own 1000), the duration (900 s - what the\n"
               << "  *** prototype's 500/500 reference was measured at), the 50 x 1800 s\n"
               << "  *** sub-batch, or the perturb schedule's coverage. Reduce per-step cost,\n"
               << "  *** or surface the measured table. Confirm first that this was a RELEASE\n"
               << "  *** build run alone (a Debug build is 10-50x slower and never fits).\n";
        }
        os << "=============================================================================\n";
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // Verdicts. The clause violation first - a batch that broke conservation is
    // not a batch whose runtime means anything.
    // -------------------------------------------------------------------------
    if (failure.failed) {
        FAIL(failure.what);
    }

    // Non-vacuity: the whole scheduled workload really ran.
    REQUIRE(stats.configsRun == kHostileBatchSize + kSubBatchConfigs);
    REQUIRE(stats.stepsRun == expectedSteps);
    REQUIRE(stats.sampledSteps > 0u);

    // (a)-(c), restated as batch-level assertions so the criterion shows up in
    // the report as passing assertions rather than only as an absent failure.
    REQUIRE(stats.maxConservationViolations == 0u);
    REQUIRE(stats.maxContainments == 0u);
    REQUIRE(stats.worstDrift <= kDriftTolerance);

    // (d) is non-vacuous: the schedule really fired, really went out of range,
    // really carried non-finite amounts, and really exceeded FR-071's clamp at
    // BOTH ends. Without these the clause could pass on a schedule that never
    // called anything.
    REQUIRE(stats.perturb.calls > 0u);
    REQUIRE(stats.perturb.inRange > 0u);
    REQUIRE(stats.perturb.outOfRange > 0u);
    REQUIRE(stats.perturb.nonFiniteAmount > 0u);
    REQUIRE(stats.perturb.aboveUpperClamp > 0u);
    REQUIRE(stats.perturb.belowLowerClamp > 0u);

    // The budget is part of the criterion, so it is asserted - but with CHECK,
    // which is non-fatal: a miss must not suppress the clause verdicts above,
    // and the table has already been printed with the stop-and-surface banner.
    CHECK(wallSeconds <= kWallClockBudgetSeconds);
}

// ==============================================================================
// Group S (tasks.md T021) - SC-003, SC-005, SC-013, SC-017, SC-018
// ==============================================================================
//
// The anonymous namespace is REOPENED a third time rather than the T019/T020
// blocks above being edited: those blocks are the shared fuzz harness (RuleConfig,
// the knob table, both box generators, `buildBatch`, `buildBatchSeeds`) and the
// SC-001 machinery (`allStateFinite`, `kDriftTolerance`, `kSampleEverySteps`),
// and every one of them is consumed below UNCHANGED. Nothing here alters a single
// draw of either batch.
//
// All five cases are [long]: per-push CI excludes the tag and the nightly
// long-tests workflow runs it on all three OSes. Every case prints its own wall
// clock so the plan S10.5 projection (~11 min for the group) can be checked
// against a measurement rather than asserted from memory.
//
// Statistics come from `ecosystem_metrics_test_helpers.h` (T017) - the SAME
// `lateWindow`, `hasShortCycle` and `verdictAlive` the behaviour TU gates SC-002
// and SC-004 with. That shared implementation is the point: two copies of a
// verdict function drift apart and a criterion quietly stops measuring what it
// claims.
// ==============================================================================


namespace {

namespace Eco = Krate::DSP::TestUtils::Eco;

/// @brief The SHIPPED defaults, as a `PrepareConfig` (Appendix A).
///
/// NOT `RuleConfig{}`. `RuleConfig::affinity` default-initialises to all zeros and
/// `applyTo()` replays all 25 entries through `setAffinity()`, so a default-built
/// RuleConfig would silently install a ZERO affinity matrix where the engine ships
/// `defaultAffinity()` (ecosystem_engine.h:2373). Every "at the defaults" case
/// below therefore prepares a bare engine and touches only the knobs its own
/// criterion sweeps - which is also what makes those sweeps ablations rather than
/// two unrelated runs.
[[nodiscard]] EcosystemEngine::PrepareConfig defaultConfig() noexcept {
    return EcosystemEngine::PrepareConfig{.agentCount = 32u,
                                          .resourceCells = 64u,
                                          .energyBudget = 1.0,
                                          .initialPoolFraction = 0.5,
                                          .stepIntervalChunks = 8u};
}

/// @brief Prepare @p engine at the shipped defaults and re-derive its state from
///        @p seed.
///
/// `prepare()` first, `setSeed()` second: `setSeed()` is a re-derive (it is
/// `seed_ = seed;` followed by exactly `reset()`, ecosystem_engine.h:389-392), so
/// this order leaves the engine holding the seeded initial state of the config
/// just prepared. Knob setters run AFTER both, so a swept knob is unambiguously
/// in force (FR-064 (2) keeps it across prepare() either way, but the getter
/// assertions in each case pin that rather than assume it).
void prepareDefaults(EcosystemEngine& engine, std::uint32_t seed) noexcept {
    engine.prepare(EcosystemEngine::kDefaultSampleRate, defaultConfig());
    engine.setSeed(seed);
}

/// @brief The three fixed seeds SC-002 / SC-003 / SC-017 are stated on (plan S10.4).
///
/// Fixed, never drawn: these criteria are statements about THESE runs, and a seed
/// that drifts between builds turns a red into a coin flip.
constexpr std::array<std::uint32_t, 3> kDefaultsSeeds{{0xC0FFEEu, 0xC0FFEFu, 0x5EEDu}};

/// @brief The requested trace grid. `Eco::runTrace` reports the grid it ACTUALLY
///        used (93 or 94 control steps per sample at the FR-082 default), and
///        every window is sized from that field, so a window means the same
///        DURATION whatever the realised rate.
constexpr double kSampleHz = 1.0;

/// @brief First sample index of the last @p windowSeconds of @p trace.
///
/// Mirrors `Eco::lateWindow`'s own window arithmetic (`ecosystem-sim.js:704-705`)
/// so a case that needs the raw rows of the window - SC-017 reads minimum energy,
/// SC-005 reads the variance guard - looks at exactly the samples the Liveness
/// statistics beside it were computed from.
[[nodiscard]] std::size_t lateFirstIndex(const Eco::Trace& trace,
                                         double windowSeconds) noexcept {
    const std::size_t samples = trace.energy.size();
    if (samples < 2u) {
        return 0u;
    }
    const long long wanted = std::llround(windowSeconds * trace.sampleHz);
    const std::size_t requested =
        (wanted < 2) ? std::size_t{2} : static_cast<std::size_t>(wanted);
    return samples - std::min(samples, requested);
}

/// @brief One agent's column of a trace's OUTPUT rows over `[first, end)`.
[[nodiscard]] std::vector<double> outputColumn(const Eco::Trace& trace, std::size_t agent,
                                               std::size_t first) {
    const std::size_t samples = trace.output.size();
    std::vector<double> col;
    if (first >= samples) {
        return col;
    }
    col.reserve(samples - first);
    for (std::size_t s = first; s < samples; ++s) {
        const std::vector<double>& row = trace.output[s];
        col.push_back((agent < row.size()) ? row[agent] : 0.0);
    }
    return col;
}

/// @brief True when every sample of @p series is bit-identical to the first.
///
/// SC-005's zero-variance guard, tested as all-samples-equal rather than as
/// `sumSquaredDeviationsD(series) == 0.0` - the same load-bearing distinction the
/// helper header documents: the mean of 1800 copies of a value that is not exactly
/// representable leaves a sum of squared deviations of ~1.7e-25, which is NOT 0.0,
/// so the sum-of-squares form falls THROUGH the guard.
[[nodiscard]] bool isConstantSeries(std::span<const double> series) noexcept {
    if (series.empty()) {
        return true;
    }
    const double first = series.front();
    return std::all_of(series.begin(), series.end(),
                       [first](double v) { return v == first; });
}

/// @brief The recurrence scan's intermediate numbers, FOR REPORTING ONLY.
///
/// The VERDICT is `Eco::hasShortCycle` and nothing else - this struct exists so
/// the printed table can carry the same shape of evidence as the prototype's
/// "worst post-decay peak 0.175 at 557 s" (FINDINGS.md:253) instead of a bare
/// pass/fail. It deliberately repeats the helper's scan rather than exposing the
/// helper's internals, and it gates NOTHING.
struct Recurrence {
    std::size_t decorrLag = 0;  ///< 0 == never fell below 0.2 within len/2
    std::size_t peakLag = 0;    ///< lag of the largest post-decay autocorrelation
    double peak = 0.0;          ///< that autocorrelation
};

/// @brief Reporting-only transcription of the helper's scan (see `Recurrence`).
[[nodiscard]] Recurrence scanRecurrence(std::span<const double> series) {
    Recurrence r;
    const std::size_t maxLag = series.size() / 2u;
    for (std::size_t lag = 1u; lag < maxLag; ++lag) {
        if (Eco::autocorrD(series, lag) < Eco::kDecorrelationLevel) {
            r.decorrLag = lag;
            break;
        }
    }
    if (r.decorrLag == 0u) {
        return r;  // one slow trend: there is no post-decay window to report
    }
    for (std::size_t lag = r.decorrLag; lag < maxLag; ++lag) {
        const double a = Eco::autocorrD(series, lag);
        if (a > r.peak) {
            r.peak = a;
            r.peakLag = lag;
        }
    }
    return r;
}

/// @brief Elapsed seconds since @p start on the steady clock.
[[nodiscard]] double wallSecondsSince(
    const std::chrono::steady_clock::time_point& start) noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

// ==============================================================================
// SC-003 - The liveness verdict does not depend on how long you looked
// ==============================================================================
//
// The same configuration (the shipped defaults) at 600 / 900 / 1200 / 1800 /
// 3600 simulated seconds x 3 seeds = 15 cells. Two clauses:
//
//   (i)  `Eco::verdictAlive` is TRUE in every one of the 15 cells;
//   (ii) the spread of `lateActivity` across the five lengths, stated as
//        (max - min) / mean < 0.20.
//
// THE STATISTIC IS WRITTEN OUT because "varies by < 20 %" has three incompatible
// readings - (max-min)/min, (max-min)/mean and max/min - 1 - which differ by up
// to a factor of two on the prototype's own numbers (spec SC-003). Computed the
// way this case computes it, the prototype's 0.430 / 0.434 / 0.433 / 0.442 /
// 0.459 gives (0.459 - 0.430) / 0.4396 = 0.066, so the C++ figure below is
// comparable to a real measurement rather than to a bare threshold.
//
// CLAUSE (ii) IS ASSERTED PER SEED, not on the seed-averaged series. "The same
// configuration at five lengths" is one series of five numbers, and there are
// three of those here - one per seed. Averaging the three first would let two
// seeds' opposite drifts cancel, which is exactly the shape of the failure this
// criterion exists to catch. The seed-averaged spread is REPORTED alongside so
// both readings are on the record.
//
// WHY THIS CRITERION EXISTS: round 1's "current best configuration" was a
// decaying transient whose whole-run activity of 0.32 came entirely from its
// first ten minutes, and whose verdict flipped between 1200 s and 1800 s
// (FINDINGS.md:177-190). A fixed late window is what makes a length-independent
// statement possible at all; this case is the proof that it delivers one.
//
// FALSIFICATION: replace `Eco::lateWindow`'s fixed window with a whole-run
// statistic and clause (ii) must fail - the 600 s cell then carries the transient
// at full weight while the 3600 s cell dilutes it sixfold.
TEST_CASE("EcosystemEngine_LivenessIsDurationStable", "[ecosystem_engine][long]") {
    // The five lengths of SC-003, in seconds. 600 is deliberately BELOW the
    // verdict function's stated "run of >= 900 s": at that length the late window
    // IS the whole run, transient included, which is the hardest of the five
    // cells and the one whose disagreement with the others would be the finding.
    constexpr std::array<double, 5> kLengths{{600.0, 900.0, 1200.0, 1800.0, 3600.0}};
    constexpr double kSpreadBound = 0.20;
    constexpr std::size_t kSeeds = kDefaultsSeeds.size();
    constexpr std::size_t kLens = kLengths.size();

    const auto wallStart = std::chrono::steady_clock::now();

    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();

    // [seed][length], filled cell by cell so the table can be printed BEFORE any
    // verdict (FR-085: a miss arrives with its numbers attached).
    std::array<std::array<double, kLens>, kSeeds> activity{};
    std::array<std::array<std::size_t, kLens>, kSeeds> frozen{};
    std::array<std::array<bool, kLens>, kSeeds> alive{};
    std::array<std::array<double, kLens>, kSeeds> corr{};

    for (std::size_t s = 0; s < kSeeds; ++s) {
        for (std::size_t L = 0; L < kLens; ++L) {
            prepareDefaults(*engine, kDefaultsSeeds[s]);
            REQUIRE(engine->getAgentCount() == defaultConfig().agentCount);

            const Eco::Trace trace = Eco::runTrace(*engine, kLengths[L], kSampleHz);
            REQUIRE(trace.agents == engine->getAgentCount());
            REQUIRE(trace.energy.size() >= 2u);

            const Eco::Liveness live = Eco::lateWindow(trace, Eco::kLateWindowSeconds);
            activity[s][L] = live.lateActivity;
            frozen[s][L] = live.lateFrozen;
            corr[s][L] = live.latePairCorr;
            alive[s][L] = Eco::verdictAlive(trace);
        }
    }

    const double wallSeconds = wallSecondsSince(wallStart);

    // ---- the measured table --------------------------------------------------
    std::array<double, kSeeds> spread{};
    std::array<double, kLens> seedMeanActivity{};
    {
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-003 duration stability - MEASURED (defaults, 15 cells)\n"
           << "-----------------------------------------------------------------------------\n"
           << "  seed             600 s      900 s     1200 s     1800 s     3600 s"
           << "   (max-min)/mean\n";
        for (std::size_t s = 0; s < kSeeds; ++s) {
            double lo = activity[s][0];
            double hi = activity[s][0];
            double sum = 0.0;
            for (std::size_t L = 0; L < kLens; ++L) {
                lo = std::min(lo, activity[s][L]);
                hi = std::max(hi, activity[s][L]);
                sum += activity[s][L];
                seedMeanActivity[L] += activity[s][L] / static_cast<double>(kSeeds);
            }
            const double mean = sum / static_cast<double>(kLens);
            spread[s] = (mean > 0.0) ? ((hi - lo) / mean) : 1.0;

            os << "  0x" << std::hex << kDefaultsSeeds[s] << std::dec << "  act ";
            for (std::size_t L = 0; L < kLens; ++L) {
                os << std::fixed << std::setprecision(4) << std::setw(11) << activity[s][L];
            }
            os << std::setw(17) << std::setprecision(4) << spread[s] << "\n";
            os << "            frz ";
            for (std::size_t L = 0; L < kLens; ++L) {
                os << std::setw(11) << frozen[s][L];
            }
            os << "\n            alv ";
            for (std::size_t L = 0; L < kLens; ++L) {
                os << std::setw(11) << (alive[s][L] ? "yes" : "NO");
            }
            os << "\n            cor ";
            for (std::size_t L = 0; L < kLens; ++L) {
                os << std::fixed << std::setprecision(4) << std::setw(11) << corr[s][L];
            }
            os << "\n";
        }
        double lo = seedMeanActivity[0];
        double hi = seedMeanActivity[0];
        double sum = 0.0;
        for (const double a : seedMeanActivity) {
            lo = std::min(lo, a);
            hi = std::max(hi, a);
            sum += a;
        }
        const double pooledMean = sum / static_cast<double>(kLens);
        os << "  seed-mean   ";
        for (const double a : seedMeanActivity) {
            os << std::fixed << std::setprecision(4) << std::setw(11) << a;
        }
        os << std::setw(17) << std::setprecision(4)
           << ((pooledMean > 0.0) ? ((hi - lo) / pooledMean) : 1.0)
           << "  (reported, not gated)\n"
           << "-----------------------------------------------------------------------------\n"
           << "  gate: verdictAlive true in all 15 cells; per-seed (max-min)/mean < "
           << kSpreadBound << "\n"
           << "  prototype: 0.4300 0.4340 0.4330 0.4420 0.4590 -> 0.066, 0 frozen at every"
           << " length\n"
           << "  wall clock: " << std::fixed << std::setprecision(1) << wallSeconds << " s\n"
           << "=============================================================================\n";
        WARN(os.str());
    }

    // ---- (i) alive at every length, every seed -------------------------------
    for (std::size_t s = 0; s < kSeeds; ++s) {
        for (std::size_t L = 0; L < kLens; ++L) {
            INFO("seed 0x" << std::hex << kDefaultsSeeds[s] << std::dec << " at " << kLengths[L]
                           << " s: lateActivity " << activity[s][L] << ", lateFrozen "
                           << frozen[s][L]);
            REQUIRE(alive[s][L]);
        }
    }

    // ---- (ii) the spread, per seed -------------------------------------------
    for (std::size_t s = 0; s < kSeeds; ++s) {
        INFO("seed 0x" << std::hex << kDefaultsSeeds[s] << std::dec
                       << ": (max-min)/mean of lateActivity over the five lengths = "
                       << spread[s] << " (bound " << kSpreadBound << ", prototype 0.066)");
        REQUIRE(spread[s] < kSpreadBound);
    }
}

// ==============================================================================
// SC-005 - No short limit cycle, by RECURRENCE
// ==============================================================================
//
// 1800 s at the shipped defaults; for the POPULATION-MEAN `getAgentOutput`
// series, `Eco::hasShortCycle(series) == false`; every agent's own series is
// scanned and REPORTED.
//
// WHY THE POPULATION MEAN (user ruling 2026-09-16, T023). Scanned per agent,
// the clause flagged FR-050's DESIGNED oscillation: each agent's appetite is
// phase-gated at its own intrinsic frequency (periods 56-667 s at the defaults),
// so its output decorrelates at a quarter period and recurs at one period. First
// measured: 3 of 32 agents above 0.8, worst 0.848 at 109 s (agent 1, decorrLag
// 25 s), with activity 0.42-0.48, zero frozen and pairwise |corr| 0.17-0.19 -
// a healthy, decorrelated bank. The limit cycle the roadmap forbids is the
// ECOSYSTEM's: the population locked into a common boom/bust, which is exactly
// what the population mean shows and what independent intrinsic oscillations
// average out of. The prototype's own clause was population-level too (on the
// entropy series, which D-9 forbids gating on).
//
// THE SCAN IS A RECURRENCE TEST, NOT A MAXIMUM. The naive "maximum
// autocorrelation over all lags >= 10 s" form is forbidden by name (spec SC-005):
// for any smooth signal that maximum always sits at the SHORTEST lag scanned, and
// it reported "limit cycle: 0.909 at lag 9.0 s" for a 30-minute run whose scan
// merely started at 9 s - it was measuring smoothness (FINDINGS.md:164-174). The
// helper instead scans upward to the first lag whose autocorrelation falls below
// 0.2 (the signal forgets itself) and only then requires the maximum to be <= 0.8.
//
// THE ESCAPE CLAUSE IS GUARDED, and this case asserts the guard SEPARATELY rather
// than trusting the helper's internal all-samples-equal test to have been reached:
// a series railed at FR-061's clamp never decorrelates either, so "never
// decorrelated -> no cycle" would hand the most degenerate possible run a pass.
// A series whose LATE-WINDOW sample variance is exactly zero FAILS OUTRIGHT here.
//
// The series scanned is the FULL 1800 s run (spec SC-005: "over an 1800 s run");
// the zero-variance guard is stated on the LAST 600 s (tasks.md T021), which is
// the strictly stronger reading - an output that rails only in the late window
// would be diluted out of a whole-run variance.
//
// Prototype reference: worst post-decay peak 0.175 at 557 s (FINDINGS.md:253).
//
// FALSIFICATION: the helper's own falsification record (T017) covers the metric -
// a period-60 s sine returns TRUE, a monotone ramp FALSE, a constant TRUE.
TEST_CASE("EcosystemEngine_NoShortLimitCycle", "[ecosystem_engine][long]") {
    constexpr double kRunSeconds = 1800.0;

    const auto wallStart = std::chrono::steady_clock::now();

    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();
    prepareDefaults(*engine, kDefaultsSeeds[0]);
    REQUIRE(engine->getAgentCount() == defaultConfig().agentCount);
    REQUIRE(engine->getSyncRate() == 0.0f);  // the shipped default: sync is OFF

    const Eco::Trace trace = Eco::runTrace(*engine, kRunSeconds, kSampleHz);
    REQUIRE(trace.agents == engine->getAgentCount());
    REQUIRE(trace.output.size() == trace.energy.size());
    // Non-vacuity: the late window must actually fit inside the run, or the
    // variance guard below would silently test the whole (shorter) series.
    REQUIRE(static_cast<double>(trace.output.size()) >
            Eco::kLateWindowSeconds * trace.sampleHz);

    const std::size_t lateFirst = lateFirstIndex(trace, Eco::kLateWindowSeconds);

    // Per agent: the gate (helper, full series) and the reporting scan.
    std::vector<char> cycled(trace.agents, 0);
    std::vector<char> lateConstant(trace.agents, 0);
    std::vector<Recurrence> report(trace.agents);
    std::size_t worstAgent = 0;
    double worstPeak = -1.0;
    std::size_t neverDecorrelated = 0;
    std::size_t cycledCount = 0;
    std::size_t constantCount = 0;

    for (std::size_t i = 0; i < trace.agents; ++i) {
        const std::vector<double> full = outputColumn(trace, i, 0u);
        const std::vector<double> late = outputColumn(trace, i, lateFirst);
        cycled[i] = Eco::hasShortCycle(full) ? char{1} : char{0};
        lateConstant[i] = isConstantSeries(late) ? char{1} : char{0};
        cycledCount += static_cast<std::size_t>(cycled[i] != char{0});
        constantCount += static_cast<std::size_t>(lateConstant[i] != char{0});
        report[i] = scanRecurrence(full);
        if (report[i].decorrLag == 0u) {
            ++neverDecorrelated;
        }
        if (report[i].peak > worstPeak) {
            worstPeak = report[i].peak;
            worstAgent = i;
        }
    }

    // THE GATE: the population-mean output series (ruling 2026-09-16).
    const std::vector<double> meanFull = Eco::meanOutputSeries(trace, 0u);
    const std::vector<double> meanLate = Eco::meanOutputSeries(trace, lateFirst);
    const bool meanCycled = Eco::hasShortCycle(meanFull);
    const bool meanLateConstant = isConstantSeries(meanLate);
    const Recurrence meanReport = scanRecurrence(meanFull);

    // The wall clock covers the run AND the scan: at 1800 samples the recurrence
    // scan is ~1.6 M operations per agent, which is not free and belongs in the
    // number the group budget is checked against.
    const double wallSeconds = wallSecondsSince(wallStart);

    // ---- the measured table --------------------------------------------------
    {
        const double grid = (trace.sampleHz > 0.0) ? (1.0 / trace.sampleHz) : 1.0;
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-005 recurrence scan - MEASURED (defaults, " << kRunSeconds << " s, seed 0x"
           << std::hex << kDefaultsSeeds[0] << std::dec << ")\n"
           << "-----------------------------------------------------------------------------\n"
           << "  grid                  : " << std::fixed << std::setprecision(5)
           << trace.sampleHz << " Hz x " << trace.output.size() << " samples ("
           << std::setprecision(4) << grid << " s per lag)\n"
           << "  THE GATE - population-mean output (ruling 2026-09-16):\n"
           << "    cycled              : " << (meanCycled ? "YES" : "no") << " (gate no)\n"
           << "    late window constant: " << (meanLateConstant ? "YES" : "no")
           << " (gate no - the guarded escape)\n"
           << "    decorrLag           : " << meanReport.decorrLag << " ("
           << (static_cast<double>(meanReport.decorrLag) * grid) << " s"
           << (meanReport.decorrLag == 0u ? "; never - one slow trend" : "") << ")\n"
           << "    post-decay peak     : " << std::setprecision(4) << meanReport.peak
           << " at lag " << meanReport.peakLag << " ("
           << (static_cast<double>(meanReport.peakLag) * grid) << " s)\n"
           << "  REPORTED - per agent (intrinsic-period recurrence is FR-050's design):\n"
           << "    agents scanned      : " << trace.agents << "\n"
           << "    agents recurring    : " << cycledCount << " (not gated)\n"
           << "    late window constant: " << constantCount << " (gate 0)\n"
           << "    never decorrelated  : " << neverDecorrelated << " of " << trace.agents
           << " (one slow trend)\n"
           << "    worst post-decay peak: " << std::setprecision(4) << worstPeak << " at lag "
           << report[worstAgent].peakLag << " ("
           << (static_cast<double>(report[worstAgent].peakLag) * grid) << " s), agent "
           << worstAgent << ", decorrLag " << report[worstAgent].decorrLag << " ("
           << (static_cast<double>(report[worstAgent].decorrLag) * grid) << " s)\n"
           << "  bound                 : <= " << Eco::kCycleAutocorr
           << "  (prototype 0.175 at 557 s, population-level on entropy)\n"
           << "  wall clock            : " << std::setprecision(1) << wallSeconds << " s\n"
           << "=============================================================================\n";
        WARN(os.str());
    }

    // The guarded escape, asserted on its own so a railed late window fails as a
    // ZERO-VARIANCE finding rather than as an opaque cycle flag - on the gated
    // series and, as the stricter sanity check, on every agent's own.
    for (std::size_t i = 0; i < trace.agents; ++i) {
        INFO("agent " << i << ": decorrLag " << report[i].decorrLag << ", post-decay peak "
                      << report[i].peak << " at lag " << report[i].peakLag);
        REQUIRE(lateConstant[i] == char{0});
    }
    INFO("population mean: decorrLag " << meanReport.decorrLag << ", post-decay peak "
                                       << meanReport.peak << " at lag " << meanReport.peakLag);
    REQUIRE_FALSE(meanLateConstant);
    REQUIRE_FALSE(meanCycled);
}

// ==============================================================================
// SC-013 - The macro-reachable region is mostly alive
// ==============================================================================
//
// 500 configurations from the SANE box (the region a Phase-10 concept macro would
// plausibly reach) x 900 simulated seconds - the duration the prototype's 83.0 %
// reference was measured at (run.js:401, FINDINGS.md:284-293), so the C++ figure
// is comparable to real evidence rather than to a number from another regime:
//   >= 70 % alive under SC-002's VERDICT FUNCTION, and 0 unbounded.
//
// THE 83.0 % REFERENCE IS A COMPARISON BASELINE, NOT A THRESHOLD. It was produced
// by the prototype's own `liveness()`, whose cycle clause ran on the ENTROPY
// series (hSeries, ecosystem-sim.js:736-757), not on OUTPUT as
// `Eco::verdictAlive` requires after Clarification Q3. The C++ figure re-measures
// under the corrected clause and may differ; a divergence is a FINDING TO SURFACE
// under FR-085, never a threshold to move. First measured with the clause on
// PER-AGENT output: 52.8 % (264/500), 127 dead on the cycle clause alone - the
// agents' own FR-050 oscillation, not an ecosystem cycle (see SC-005). The
// 2026-09-16 ruling moved the clause to the POPULATION-MEAN output.
//
// "UNBOUNDED" is SC-001's clauses, read at the end of each configuration: any
// non-finite value in the state, any conservation violation, any non-finite
// containment, or a relative total-energy drift above 1e-9. The two counters are
// CUMULATIVE since prepare() (and FR-065 clears them there, Clarification Q8), so
// an end-of-run read sees anything that happened at any point in the 900 s - which
// is why this case does not pay for per-sample sampling on top of the trace it
// already builds. SC-001's own batch is where per-sample boundedness is gated.
//
// THE PER-TERCILE REPORT is part of the criterion's stated output: the alive
// fraction per tercile of each non-exempt knob, so Phase 10 inherits the death
// predictors rather than rediscovering them. It is REPORTED, never gated - the
// criterion gates one aggregate, and a per-tercile assertion would be a threshold
// nobody has measured.
TEST_CASE("EcosystemEngine_SaneBoxLiveness", "[ecosystem_engine][long]") {
    constexpr double kSaneSeconds = 900.0;
    constexpr double kAliveFractionGate = 0.70;
    constexpr std::size_t kTerciles = 3;

    const std::vector<RuleConfig> configs = buildBatch(true, kSaneBatchSize);
    const std::vector<std::uint32_t> seeds = buildBatchSeeds(true, kSaneBatchSize);
    REQUIRE(configs.size() == kSaneBatchSize);
    REQUIRE(seeds.size() == kSaneBatchSize);

    // ONE engine, heap-allocated and reused across all 500 configurations
    // (~23.5 KB, plan S9 / R-9); FR-065's counters clear on prepare().
    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();

    // `char`, not `bool`: std::vector<bool> is a bitfield proxy and the tercile
    // report indexes it through a sorted permutation.
    std::vector<char> aliveFlag(kSaneBatchSize, 0);
    std::size_t aliveCount = 0;
    std::size_t unboundedCount = 0;
    std::string firstUnboundedWhat;
    double worstDrift = 0.0;
    double meanActivity = 0.0;
    std::size_t quietDeaths = 0;   // failed the verdict on the activity clause
    std::size_t frozenDeaths = 0;  // failed on the frozen-fraction clause
    std::size_t cycleDeaths = 0;   // failed on the population-mean output cycle clause alone

    const auto wallStart = std::chrono::steady_clock::now();

    for (std::size_t k = 0; k < kSaneBatchSize; ++k) {
        configs[k].applyTo(*engine);
        engine->setSeed(seeds[k]);

        const Eco::Trace trace = Eco::runTrace(*engine, kSaneSeconds, kSampleHz);

        // ---- bounded? --------------------------------------------------------
        const double budget = engine->getEnergyBudget();
        const double drift = std::fabs(engine->getTotalEnergy() - budget) / budget;
        worstDrift = std::max(worstDrift, drift);
        const std::uint64_t violations = engine->getConservationViolationCount();
        const std::uint64_t containments = engine->getNonFiniteContainmentCount();
        const bool finite = allStateFinite(*engine);
        if (!finite || violations != 0u || containments != 0u || !(drift <= kDriftTolerance)) {
            ++unboundedCount;
            if (firstUnboundedWhat.empty()) {
                std::ostringstream os;
                os << "sane config " << k << " (seed 0x" << std::hex << seeds[k] << std::dec
                   << "): finite " << (finite ? "yes" : "NO") << ", conservationViolations "
                   << violations << ", nonFiniteContainments " << containments
                   << ", relative drift " << std::scientific << std::setprecision(3) << drift
                   << " (bound " << kDriftTolerance << ")";
                firstUnboundedWhat = os.str();
            }
        }

        // ---- alive? ----------------------------------------------------------
        // `verdictAlive` recomputes `lateWindow` internally, so the window
        // statistics are built twice per configuration. That is deliberate: ONE
        // implementation of the verdict function is worth far more than the ~3 %
        // of this batch the second pass costs, and hand-inlining the three
        // clauses here is exactly how a second copy starts drifting from the one
        // the behaviour TU gates SC-002 with.
        const Eco::Liveness live = Eco::lateWindow(trace, Eco::kLateWindowSeconds);
        meanActivity += live.lateActivity / static_cast<double>(kSaneBatchSize);
        const bool alive = Eco::verdictAlive(trace);
        aliveFlag[k] = alive ? char{1} : char{0};
        if (alive) {
            ++aliveCount;
        } else if (live.lateActivity < Eco::kAliveActivity) {
            // Attribute the death to its clause, so the report says HOW the region
            // dies rather than only how often. The order matches verdictAlive's
            // own clause order, so each death is charged to the FIRST clause it
            // failed.
            ++quietDeaths;
        } else if (static_cast<double>(live.lateFrozen) >
                   Eco::kAliveMaxFrozenFraction * static_cast<double>(trace.agents)) {
            ++frozenDeaths;
        } else {
            ++cycleDeaths;
        }
    }

    const double wallSeconds = wallSecondsSince(wallStart);
    const double aliveFraction =
        static_cast<double>(aliveCount) / static_cast<double>(kSaneBatchSize);

    // ---- the aggregate table -------------------------------------------------
    {
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-013 sane-box liveness - MEASURED (" << kSaneBatchSize << " configs x "
           << kSaneSeconds << " s)\n"
           << "-----------------------------------------------------------------------------\n"
           << "  alive                 : " << aliveCount << " / " << kSaneBatchSize << " = "
           << std::fixed << std::setprecision(1) << (100.0 * aliveFraction) << " %  (gate >= "
           << (100.0 * kAliveFractionGate) << " %)\n"
           << "  prototype baseline    : 83.0 % (415/500) - measured with the ENTROPY-based\n"
           << "                          cycle clause, NOT the output-based verdict this case\n"
           << "                          uses (Clarification Q3). A divergence is a FINDING to\n"
           << "                          surface under FR-085, not a threshold to move.\n"
           << "                          First C++ figure, clause on PER-AGENT output: 52.8 %\n"
           << "                          (127 cycle deaths = FR-050's own oscillation); the\n"
           << "                          2026-09-16 ruling moved it to the POPULATION MEAN.\n"
           << "  deaths by clause      : " << quietDeaths << " too quiet (activity < "
           << std::setprecision(2) << Eco::kAliveActivity << "), " << frozenDeaths
           << " too frozen (> " << (100.0 * Eco::kAliveMaxFrozenFraction) << " %), "
           << cycleDeaths << " short cycle on the population-mean output\n"
           << "  mean lateActivity     : " << std::setprecision(4) << meanActivity << "\n"
           << "  unbounded             : " << unboundedCount << " (gate 0)\n"
           << "  worst relative drift  : " << std::scientific << std::setprecision(3)
           << worstDrift << " (bound " << kDriftTolerance << ")\n"
           << "  wall clock            : " << std::fixed << std::setprecision(1) << wallSeconds
           << " s (" << std::setprecision(2) << (wallSeconds / 60.0) << " min)\n";
        if (!firstUnboundedWhat.empty()) {
            os << "  first unbounded       : " << firstUnboundedWhat << "\n";
        }
        os << "=============================================================================\n";
        WARN(os.str());
    }

    // ---- the per-tercile death-predictor report (REPORTED, never gated) ------
    {
        std::vector<std::size_t> order(kSaneBatchSize, 0u);
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-013 alive fraction per tercile of each sane-box knob (REPORTED, NOT GATED)\n"
           << "  Phase 10 inherits the death predictors from this table rather than\n"
           << "  rediscovering them. The three knobs the sane box pins (energyBudget,\n"
           << "  stepIntervalChunks, feedRate) are exempt there and are omitted here.\n"
           << "-----------------------------------------------------------------------------\n";
        for (const Knob& knob : kKnobs) {
            if (isExempt(knob.name, kExemptSane)) {
                continue;
            }
            for (std::size_t k = 0; k < kSaneBatchSize; ++k) {
                order[k] = k;
            }
            const auto member = knob.member;  // double RuleConfig::*
            std::sort(order.begin(), order.end(),
                      [&configs, member](std::size_t a, std::size_t b) {
                          return configs[a].*member < configs[b].*member;
                      });

            os << "  " << std::left << std::setw(20) << knob.name << std::right;
            for (std::size_t t = 0; t < kTerciles; ++t) {
                const std::size_t begin = (kSaneBatchSize * t) / kTerciles;
                const std::size_t end = (kSaneBatchSize * (t + 1u)) / kTerciles;
                std::size_t aliveHere = 0;
                for (std::size_t p = begin; p < end; ++p) {
                    aliveHere += static_cast<std::size_t>(aliveFlag[order[p]] != char{0});
                }
                const std::size_t count = end - begin;
                const double lo = configs[order[begin]].*member;
                const double hi = configs[order[end - 1u]].*member;
                std::ostringstream cell;
                cell << std::fixed << std::setprecision(4) << lo << ".." << hi;
                os << "  " << std::left << std::setw(19) << cell.str() << std::right
                   << std::setw(4) << aliveHere << "/" << std::setw(3) << count;
            }
            os << "\n";
        }
        os << "=============================================================================\n";
        WARN(os.str());
    }

    // ---- the verdicts --------------------------------------------------------
    if (!firstUnboundedWhat.empty()) {
        FAIL("SC-013 - the sane box must be bounded everywhere: " << firstUnboundedWhat);
    }
    REQUIRE(unboundedCount == 0u);
    REQUIRE(worstDrift <= kDriftTolerance);
    REQUIRE(aliveFraction >= kAliveFractionGate);
}

// ==============================================================================
// SC-017 - The refuge floor holds the predation cliff (FR-022)
// ==============================================================================
//
// The defaults with `predation` swept over {0.6, 0.7, 0.85} x 3 seeds x 1800 s:
// `lateFrozen == 0` at every point, and no agent's energy sits at 0 for any part
// of the late window (the prototype's claim is "no agent freezes up to predation
// 0.85", FINDINGS.md:224-226).
//
// THE preyFloorShares = 0 CONTROL ARM AT predation = 0.7 IS NOT OPTIONAL, and the
// reason is a measurement rather than a preference. At the DEFAULTS the ablation
// shows that removing the floor RAISES activity - 0.522 vs 0.44, +18 %
// (FINDINGS.md:274) - so a missing or mis-scaled `preyFloor` makes SC-002 GREENER,
// not redder; and SC-013 randomises `preyFloor` alongside every other knob and
// gates only an aggregate. Without SC-017, an implementation that silently dropped
// FR-022 would pass EVERY OTHER CRITERION IN THIS SPEC. The control arm is what
// demonstrates the metric discriminates the floor rather than assuming it does.
//
// "SITS AT 0" IS STATED IN SHARE UNITS, and that is a measurement decision, not a
// softening. With `preyFloorAbs_ == 0` the exchange scale is `spare / want` with
// `spare == energy_[i]` (ecosystem_engine.h:1556-1562), so a giving agent is
// drained to its own holding - but the drain arrives as a sum of per-pair flows
// (`:1564-1571`), whose floating-point residue is not bit-exactly 0. An `== 0.0`
// test would therefore be a test of rounding, and it would report GREEN on a
// population parked at 1e-18 of a share. The threshold below is 1e-3 SHARES - one
// thousandth of an agent's fair share of the budget, three orders of magnitude
// below anything a living agent holds - and the measured MINIMUM in each cell is
// printed, so the margin is on the record rather than assumed.
//
// THE CONTROL ARM'S BOUND: it is asserted to fail on AT LEAST ONE of the three
// seeds, and the per-seed outcome is printed. The prototype's evidence is a single
// measurement ("about a third of the agents parked at zero permanently", a cliff
// 0.05 away from the default); "the metric discriminates the floor" is
// demonstrated by one cell, whereas requiring all three would be a claim about the
// effect's seed-reliability that no measurement in this phase supports. If the arm
// fails at all three, that is the stronger result and it is visible in the table.
TEST_CASE("EcosystemEngine_RefugeFloorPreventsPredationCliff", "[ecosystem_engine][long]") {
    constexpr std::array<float, 3> kPredations{{0.6f, 0.7f, 0.85f}};
    constexpr float kControlPredation = 0.7f;
    constexpr float kDefaultPreyFloorShares = 0.5f;  // the Appendix-A default
    constexpr double kRunSeconds = 1800.0;
    /// "At zero", in shares of `energyBudget / agentCount` - see the block above.
    constexpr double kZeroEnergyShares = 1.0e-3;

    /// @brief One cell's outcome. `minShares` is the smallest energy ANY agent
    ///        held at ANY sampled point of the late window, in shares.
    struct Cell {
        float predation = 0.0f;
        float preyFloor = 0.0f;
        std::uint32_t seed = 0u;
        double activity = 0.0;
        std::size_t frozen = 0u;
        double minShares = 0.0;
        std::size_t agentsAtZero = 0u;  ///< agents whose late-window minimum is "at zero"
        bool passesBound = false;       ///< frozen == 0 AND nothing at zero
    };

    const auto wallStart = std::chrono::steady_clock::now();

    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();

    // `runCell` prepares at the defaults and applies the two swept knobs, so the
    // two arms differ in EXACTLY ONE KNOB - which is what makes the control arm an
    // ablation rather than an unrelated run.
    const auto runCell = [&](float predation, float preyFloorShares,
                             std::uint32_t seed) -> Cell {
        Cell cell;
        cell.predation = predation;
        cell.preyFloor = preyFloorShares;
        cell.seed = seed;

        prepareDefaults(*engine, seed);
        engine->setPredation(predation);
        engine->setPreyFloorShares(preyFloorShares);

        const Eco::Trace trace = Eco::runTrace(*engine, kRunSeconds, kSampleHz);
        const Eco::Liveness live = Eco::lateWindow(trace, Eco::kLateWindowSeconds);
        cell.activity = live.lateActivity;
        cell.frozen = live.lateFrozen;

        if (trace.agents == 0u || trace.energy.empty()) {
            return cell;  // an unprepared engine cannot pass the bound
        }

        const double meanShare =
            engine->getEnergyBudget() / static_cast<double>(trace.agents);
        const std::size_t first = lateFirstIndex(trace, Eco::kLateWindowSeconds);

        double worst = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < trace.agents; ++i) {
            double agentMin = std::numeric_limits<double>::max();
            for (std::size_t s = first; s < trace.energy.size(); ++s) {
                const std::vector<double>& row = trace.energy[s];
                if (i < row.size()) {
                    agentMin = std::min(agentMin, row[i]);
                }
            }
            const double shares = (meanShare > 0.0) ? (agentMin / meanShare) : 0.0;
            worst = std::min(worst, shares);
            if (shares <= kZeroEnergyShares) {
                ++cell.agentsAtZero;
            }
        }
        cell.minShares = worst;
        cell.passesBound = (cell.frozen == 0u) && (cell.agentsAtZero == 0u);
        return cell;
    };

    // ---- the defaults arm: three predations x three seeds --------------------
    std::vector<Cell> defaultsArm;
    defaultsArm.reserve(kPredations.size() * kDefaultsSeeds.size());
    for (const float predation : kPredations) {
        for (const std::uint32_t seed : kDefaultsSeeds) {
            defaultsArm.push_back(runCell(predation, kDefaultPreyFloorShares, seed));
            // FR-064 (2): the swept knobs really are what the cell claims.
            REQUIRE(engine->getPredation() == predation);
            REQUIRE(engine->getPreyFloorShares() == kDefaultPreyFloorShares);
        }
    }

    // ---- the control arm: preyFloorShares = 0 at predation 0.7 ---------------
    std::vector<Cell> controlArm;
    controlArm.reserve(kDefaultsSeeds.size());
    for (const std::uint32_t seed : kDefaultsSeeds) {
        controlArm.push_back(runCell(kControlPredation, 0.0f, seed));
        REQUIRE(engine->getPreyFloorShares() == 0.0f);  // FR-022 really is removed
    }

    const double wallSeconds = wallSecondsSince(wallStart);

    std::size_t controlFailures = 0;
    for (const Cell& c : controlArm) {
        if (!c.passesBound) {
            ++controlFailures;
        }
    }

    // ---- the measured table --------------------------------------------------
    {
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-017 refuge floor vs the predation cliff - MEASURED (" << kRunSeconds
           << " s per cell)\n"
           << "-----------------------------------------------------------------------------\n"
           << "  bound: lateFrozen == 0 AND no agent's late-window energy <= "
           << std::scientific << std::setprecision(1) << kZeroEnergyShares << " shares\n"
           << "-- defaults arm (preyFloor 0.5 shares) - every cell must PASS ----------------\n";
        const auto row = [&os](const Cell& c) {
            os << "  predation " << std::fixed << std::setprecision(2) << c.predation
               << "  preyFloor " << std::setprecision(2) << c.preyFloor << "  seed 0x"
               << std::hex << c.seed << std::dec << "  activity " << std::setprecision(4)
               << std::setw(8) << c.activity << "  frozen " << std::setw(3) << c.frozen
               << "  min energy " << std::scientific << std::setprecision(3) << std::setw(11)
               << c.minShares << " shares  atZero " << std::setw(3) << c.agentsAtZero << "   "
               << (c.passesBound ? "PASSES" : "FAILS") << "\n";
        };
        for (const Cell& c : defaultsArm) {
            row(c);
        }
        os << "-- control arm (preyFloor 0, FR-022 REMOVED) - must FAIL ---------------------\n";
        for (const Cell& c : controlArm) {
            row(c);
        }
        os << "-----------------------------------------------------------------------------\n"
           << "  control arm failed the bound at " << controlFailures << " of "
           << controlArm.size() << " seeds (gate >= 1 - the metric must be SHOWN to\n"
           << "  discriminate FR-022; prototype: about a third of the agents parked at zero\n"
           << "  permanently at predation 0.7, a cliff 0.05 away from the default)\n"
           << "  wall clock            : " << std::fixed << std::setprecision(1) << wallSeconds
           << " s\n"
           << "=============================================================================\n";
        WARN(os.str());
    }

    // ---- the verdicts --------------------------------------------------------
    for (const Cell& c : defaultsArm) {
        INFO("defaults arm: predation " << c.predation << ", seed 0x" << std::hex << c.seed
                                        << std::dec << ", activity " << c.activity
                                        << ", min late-window energy " << c.minShares
                                        << " shares, agents at zero " << c.agentsAtZero);
        REQUIRE(c.frozen == 0u);
        REQUIRE(c.agentsAtZero == 0u);
        REQUIRE(c.minShares > kZeroEnergyShares);
    }

    // THE CONTROL ARM IS AN ASSERTION, not a print: a falsification that is not
    // asserted is an assumption. If this passes, the bound above cannot see
    // FR-022 and the defaults arm proves nothing about the refuge floor.
    INFO("control arm (preyFloorShares = 0 at predation "
         << kControlPredation << "): " << controlFailures << " of " << controlArm.size()
         << " seeds failed the frozen/at-zero bound");
    REQUIRE(controlFailures >= 1u);
}

// ==============================================================================
// SC-018 - The overnight soak
// ==============================================================================
//
// The theme-level cross-cutting constraint, roadmap lines 532-534: "a drone
// instrument that can run away or die overnight is broken by definition."
//
// 28 800 simulated seconds (8 hours) at the shipped defaults x 3 seeds =
// 2 700 000 control steps per seed, stated so the cost is on the record. Two
// families of assertion:
//   * SC-001's (a), (b) and (c) THROUGHOUT, sampled at ~1 Hz;
//   * SC-002's DEFAULTS GATE - (a) lateActivity >= 0.30 and (b) lateFrozen == 0 -
//     measured over the FINAL 600 s window. Not the verdict function's looser
//     0.10 / 25 %: this is the defaults configuration, and the defaults gate is
//     what the defaults are held to.
//
// NO OTHER CRITERION IN THIS SPEC REACHES A DRONE-REALISTIC DURATION - SC-001 runs
// 900 s, SC-003 tops out at 3600 s - and the risk is demonstrated rather than
// hypothetical: round 1's best configuration was a decaying transient whose
// verdict flipped between 1200 s and 1800 s (FINDINGS.md:177-190).
//
// THE STATISTICS ARE STREAMING: the full trace is NOT materialised. 28 800 samples
// x 32 agents x 2 series x 8 bytes is ~15 MB per seed held live for a statistic
// that only ever looks at the last 600 s, and the SC-001 clauses need no history
// at all - they are read off the engine at each sampled step. Only the tail window
// is recorded, into a Trace sized to it, so `Eco::lateWindow` computes the
// final-600 s figures through exactly the code path the behaviour TU gates SC-002
// with.
//
// FALSIFICATION: make FR-052's leak superlinear enough to starve the economy
// (leakExponent 2.0) and the final-600 s gate must fail here while SC-001's own
// 900 s batch stays green - that length difference IS this criterion.
TEST_CASE("EcosystemEngine_OvernightSoak", "[ecosystem_engine][long]") {
    constexpr double kSoakSeconds = 28800.0;        // 8 hours
    constexpr std::uint64_t kSoakSteps = 2700000u;  // at the FR-082 default grid
    constexpr double kDefaultsActivityGate = 0.30;  // SC-002 (a), the DEFAULTS gate

    /// @brief One seed's soak, as data - so the measured table can be printed
    ///        BEFORE any verdict (FR-085).
    struct SeedResult {
        std::uint32_t seed = 0u;
        std::uint64_t steps = 0u;
        std::uint64_t samples = 0u;
        double worstDrift = 0.0;
        std::uint64_t violations = 0u;
        std::uint64_t containments = 0u;
        std::size_t tailSamples = 0u;
        double activity = 0.0;
        std::size_t frozen = 0u;
        double pairCorr = 0.0;
        bool failed = false;
        std::string what;
    };

    const auto wallStart = std::chrono::steady_clock::now();

    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();

    std::array<SeedResult, kDefaultsSeeds.size()> results{};

    for (std::size_t s = 0; s < kDefaultsSeeds.size(); ++s) {
        SeedResult& r = results[s];
        r.seed = kDefaultsSeeds[s];

        prepareDefaults(*engine, r.seed);
        REQUIRE(engine->getAgentCount() == defaultConfig().agentCount);

        const std::size_t agents = engine->getAgentCount();
        const std::size_t samplesPerStep =
            engine->getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples;
        const double stepSeconds = engine->getStepDurationSeconds();
        REQUIRE(stepSeconds > 0.0);

        const auto totalSteps = static_cast<std::uint64_t>(std::llround(
            kSoakSeconds * engine->getSampleRate() / static_cast<double>(samplesPerStep)));
        // The cost is part of the criterion's own statement, so it is ASSERTED
        // rather than described: 2 700 000 control steps per seed.
        REQUIRE(totalSteps == kSoakSteps);

        // The tail Trace: only the final 600 s, on the same ~1 Hz grid every other
        // case uses, so `Eco::lateWindow` reads it exactly as it reads a full run.
        Eco::Trace tail;
        tail.agents = agents;
        tail.sampleHz = 1.0 / (static_cast<double>(kSampleEverySteps) * stepSeconds);
        const auto tailSteps =
            static_cast<std::uint64_t>(std::llround(Eco::kLateWindowSeconds / stepSeconds));
        REQUIRE(tailSteps > 0u);
        REQUIRE(tailSteps < totalSteps);
        const std::uint64_t recordFromStep = totalSteps - tailSteps;
        const auto tailCapacity =
            static_cast<std::size_t>(tailSteps / kSampleEverySteps) + 2u;
        tail.energy.reserve(tailCapacity);
        tail.output.reserve(tailCapacity);

        std::vector<double> energyRow(agents, 0.0);
        std::vector<double> outputRow(agents, 0.0);

        const double budget = engine->getEnergyBudget();
        std::uint64_t done = 0;
        while (done < totalSteps) {
            const std::uint64_t take =
                std::min<std::uint64_t>(kSampleEverySteps, totalSteps - done);
            engine->processChunk(static_cast<std::size_t>(take) * samplesPerStep);
            done += take;
            ++r.samples;

            // ---- SC-001 (a): no non-finite value anywhere in the state --------
            // `detail::isFinite` (bit pattern) via allStateFinite - NEVER
            // std::isnan, which folds away under -ffast-math.
            if (!allStateFinite(*engine)) {
                std::ostringstream os;
                os << "seed 0x" << std::hex << r.seed << std::dec
                   << ": SC-001 (a) - a non-finite agent energy, position, phase or pool at "
                   << "control step " << done << " of " << totalSteps << " (pool "
                   << engine->getPoolEnergy() << ")";
                r.failed = true;
                r.what = os.str();
                break;
            }

            // ---- SC-001 (b): the two counters, SEPARATELY ---------------------
            r.violations = engine->getConservationViolationCount();
            r.containments = engine->getNonFiniteContainmentCount();
            if (r.violations != 0u || r.containments != 0u) {
                std::ostringstream os;
                os << "seed 0x" << std::hex << r.seed << std::dec
                   << ": SC-001 (b) at control step " << done << " of " << totalSteps
                   << " - conservationViolations " << r.violations
                   << " (the pool went NEGATIVE, FR-056) and nonFiniteContainments "
                   << r.containments << " (FR-083's guard ladder repaired a value). The two "
                   << "failure modes are reported separately and must not be confused.";
                r.failed = true;
                r.what = os.str();
                break;
            }

            // ---- SC-001 (c): relative total-energy drift ----------------------
            const double drift = std::fabs(engine->getTotalEnergy() - budget) / budget;
            r.worstDrift = std::max(r.worstDrift, drift);
            if (!(drift <= kDriftTolerance)) {
                std::ostringstream os;
                os << "seed 0x" << std::hex << r.seed << std::dec << ": SC-001 (c) - relative "
                   << "drift " << std::scientific << std::setprecision(3) << drift << " > "
                   << kDriftTolerance << " at control step " << done << " of " << totalSteps;
                r.failed = true;
                r.what = os.str();
                break;
            }

            // ---- the tail window, recorded, and nothing else ------------------
            if (done > recordFromStep) {
                for (std::size_t i = 0; i < agents; ++i) {
                    energyRow[i] = engine->getAgentEnergy(i);
                    outputRow[i] = static_cast<double>(engine->getAgentOutput(i));
                }
                tail.energy.push_back(energyRow);
                tail.output.push_back(outputRow);
            }
        }
        r.steps = done;
        r.tailSamples = tail.energy.size();

        // The soak really stepped the scheduled grid (the same absolute-clock
        // check SC-001's batch makes, plan addition A-3).
        if (!r.failed && engine->getControlStepCount() != totalSteps) {
            std::ostringstream os;
            os << "seed 0x" << std::hex << r.seed << std::dec
               << ": FIXTURE ERROR - getControlStepCount() == " << engine->getControlStepCount()
               << " after " << totalSteps << " scheduled steps.";
            r.failed = true;
            r.what = os.str();
        }

        if (!r.failed) {
            // SC-002's defaults gate over the FINAL 600 s. The window is the whole
            // tail Trace by construction; `lateWindow` sizes it from tail.sampleHz
            // either way, so the figure means the same 600 s as everywhere else.
            const Eco::Liveness live = Eco::lateWindow(tail, Eco::kLateWindowSeconds);
            r.activity = live.lateActivity;
            r.frozen = live.lateFrozen;
            r.pairCorr = live.latePairCorr;
        }
    }

    const double wallSeconds = wallSecondsSince(wallStart);

    // ---- the measured table, printed BEFORE any verdict ----------------------
    {
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-018 overnight soak - MEASURED (" << std::fixed << std::setprecision(0)
           << kSoakSeconds << " s = 8 h x " << kDefaultsSeeds.size() << " seeds, defaults)\n"
           << "-----------------------------------------------------------------------------\n";
        for (const SeedResult& r : results) {
            os << "  seed 0x" << std::hex << r.seed << std::dec << ": " << r.steps << " / "
               << kSoakSteps << " control steps, " << r.samples << " sampled points, "
               << r.tailSamples << " tail samples\n"
               << "      worst relative drift " << std::scientific << std::setprecision(3)
               << r.worstDrift << " (bound " << kDriftTolerance << "), conservationViolations "
               << r.violations << ", nonFiniteContainments " << r.containments << "\n"
               << "      final 600 s: lateActivity " << std::fixed << std::setprecision(4)
               << r.activity << " (gate >= " << std::setprecision(2) << kDefaultsActivityGate
               << "), lateFrozen " << r.frozen << " (gate == 0), latePairCorr "
               << std::setprecision(4) << r.pairCorr << "\n";
            if (r.failed) {
                os << "      *** FAILED: " << r.what << "\n";
            }
        }
        os << "-----------------------------------------------------------------------------\n"
           << "  statistics are STREAMING: only the final 600 s is materialised\n"
           << "  wall clock            : " << std::fixed << std::setprecision(1) << wallSeconds
           << " s (" << std::setprecision(2) << (wallSeconds / 60.0) << " min)\n"
           << "=============================================================================\n";
        WARN(os.str());
    }

    // ---- the verdicts --------------------------------------------------------
    for (const SeedResult& r : results) {
        if (r.failed) {
            FAIL(r.what);
        }
    }
    for (const SeedResult& r : results) {
        INFO("seed 0x" << std::hex << r.seed << std::dec << ": " << r.steps
                       << " control steps, final-600 s activity " << r.activity << ", frozen "
                       << r.frozen);
        // Non-vacuity: the whole 8 hours really ran, and the tail window really
        // held a full 600 s of samples.
        REQUIRE(r.steps == kSoakSteps);
        REQUIRE(r.samples > 0u);
        REQUIRE(static_cast<double>(r.tailSamples) >= 0.9 * Eco::kLateWindowSeconds);

        // SC-001 (a)-(c), restated as assertions so the criterion shows up in the
        // report as passing rather than only as an absent failure.
        REQUIRE(r.violations == 0u);
        REQUIRE(r.containments == 0u);
        REQUIRE(r.worstDrift <= kDriftTolerance);

        // SC-002's DEFAULTS gate, over the final 600 s.
        REQUIRE(r.activity >= kDefaultsActivityGate);
        REQUIRE(r.frozen == 0u);
    }
}

// ==============================================================================
// SC-010 (a) + (c) - The rule set stays ALIVE across dt (FR-082, T021 case 6)
// ==============================================================================
//
// SC-010's own framing: changing the sample rate from 48 kHz to 96 kHz halves
// `dt` EXACTLY as changing stepIntervalChunks from 16 to 8 does, so the two are
// gated to the same standard. `EcosystemEngine_StepIntervalBand` (behaviour TU)
// already gates clause (b) - the step COUNT is the right one in every cell - but
// a count is arithmetic on sample totals and is blind to the thing that actually
// breaks when a rule set is dt-sensitive: the ECOSYSTEM dying at a dt it was not
// tuned at. That is this case, and it is the only one in the phase that evaluates
// liveness anywhere other than 48 kHz / 8 chunks.
//
// EIGHTEEN CELLS, as SC-010 (a) states them: three seeds x
//   arm "rate"   - 44 100 / 48 000 / 96 000 Hz at the FR-082 default 8 chunks;
//   arm "chunks" - 8 / 16 / 32 chunks at 48 kHz (the floor and two slower; the
//                  band was 4 / 8 / 16 until the 2026-09-16 rulings moved FR-082's
//                  floor to 8, which is why plan.md's SC-010 (a) row still reads
//                  4 / 8 / 16 - the spec is the authority and says 8 / 16 / 32).
// 48 kHz / 8 chunks appears in BOTH arms deliberately: it is the shared corner
// the other five cells are compared against, and dropping the duplicate would
// leave each arm without its own baseline.
//
// THE GATE IS `verdictAlive`, NOT SC-002's DEFAULTS GATE. SC-010 (a) names the
// verdict function - activity >= 0.10, frozen <= 25 %, no short cycle on the
// population-mean output - which is the looser classifier. The stricter defaults
// gate (activity >= 0.30, frozen == 0) is SC-002's and is asserted at 48 kHz by
// `EcosystemEngine_LateWindowLiveness`; asserting it here would silently re-gate
// SC-002 at three sample rates on evidence nobody has measured.
//
// CLAUSE (c) IS REPORTED AND NOT GATED, and that is a measurement decision, not a
// softening: every prototype figure was taken at ONE dt (`ecosystem-sim.js:619`,
// `blockRate = 48000/512`), and the only spread data that exists (0.430...0.459,
// FINDINGS.md:256-258) is WITHIN seed across run lengths, not across dt. A band
// asserted on a statistic whose cross-seed spread has never been measured is a
// coin flip. The table below is that first measurement; per SC-010 (c) a band
// derived from it may be added by amendment.
//
// FALSIFICATION (run it, then restore): in `prepare()`, derive `dt_` from
// `kDefaultSampleRate` instead of the floored `sampleRate_`. Every per-second
// rate then means something different at 44.1 and 96 kHz, and the rate arm's
// activity column moves while the 48 kHz cells stay put.
TEST_CASE("EcosystemEngine_SampleRateIndependent", "[ecosystem_engine][long]") {
    constexpr double kRunSeconds = 1800.0;

    struct Cell {
        const char* arm;
        double sampleRate;
        std::size_t chunks;
    };
    constexpr std::array<Cell, 6> kCells{{
        {"rate", 44100.0, 8u},
        {"rate", 48000.0, 8u},
        {"rate", 96000.0, 8u},
        {"chunks", 48000.0, 8u},
        {"chunks", 48000.0, 16u},
        {"chunks", 48000.0, 32u},
    }};
    constexpr std::size_t kCellCount = kCells.size();
    constexpr std::size_t kSeedCount = kDefaultsSeeds.size();

    struct CellResult {
        double activity = 0.0;
        std::size_t frozen = 0;
        double corr = 1.0;
        bool cycle = false;
        bool alive = false;
        double gridHz = 0.0;
        std::uint64_t steps = 0;
    };
    std::array<std::array<CellResult, kSeedCount>, kCellCount> results{};

    const std::unique_ptr<EcosystemEngine> engine = std::make_unique<EcosystemEngine>();
    const auto wallStart = std::chrono::steady_clock::now();

    for (std::size_t c = 0; c < kCellCount; ++c) {
        const Cell& cell = kCells[c];
        for (std::size_t s = 0; s < kSeedCount; ++s) {
            INFO("arm " << cell.arm << ", sampleRate " << cell.sampleRate
                        << ", stepIntervalChunks " << cell.chunks << ", seed 0x" << std::hex
                        << kDefaultsSeeds[s] << std::dec);

            EcosystemEngine::PrepareConfig config = defaultConfig();
            config.stepIntervalChunks = cell.chunks;
            engine->prepare(cell.sampleRate, config);
            engine->setSeed(kDefaultsSeeds[s]);

            // NON-VACUITY, first: a clamped rate or chunk count would make this
            // cell a duplicate of another one while still reporting "alive".
            REQUIRE(engine->getSampleRate() == cell.sampleRate);
            REQUIRE(engine->getStepIntervalChunks() == cell.chunks);
            REQUIRE(engine->getStepDurationSeconds() ==
                    static_cast<double>(cell.chunks * EcosystemEngine::kControlChunkSamples) /
                        cell.sampleRate);

            const Eco::Trace trace = Eco::runTrace(*engine, kRunSeconds, kSampleHz);

            // ...and the run really happened, on a grid whose late window really
            // holds 600 s of samples.
            REQUIRE(trace.agents == engine->getAgentCount());
            REQUIRE(trace.sampleHz > 0.0);
            REQUIRE(static_cast<double>(trace.energy.size()) >=
                    0.9 * kRunSeconds * trace.sampleHz);

            const Eco::Liveness live = Eco::lateWindow(trace, Eco::kLateWindowSeconds);
            CellResult& r = results[c][s];
            r.activity = live.lateActivity;
            r.frozen = live.lateFrozen;
            r.corr = live.latePairCorr;
            r.cycle = live.cycle;
            r.alive = Eco::verdictAlive(trace);
            r.gridHz = trace.sampleHz;
            r.steps = engine->getControlStepCount();
        }
    }

    const double wallSeconds = wallSecondsSince(wallStart);

    // ---- SC-010 (c): the table. REPORTED, never gated. -----------------------
    {
        std::ostringstream os;
        os << "\n=============================================================================\n"
           << "SC-010 (a)+(c) cross-dt liveness - MEASURED (" << (kCellCount * kSeedCount)
           << " cells x " << kRunSeconds << " s)\n"
           << "  FIRST cross-dt measurement of this rule set. Clause (a) gates the verdict\n"
           << "  column; the activity figures and the cross-seed spread are clause (c) and\n"
           << "  are NOT gated - no band has ever been measured (spec SC-010 (c)).\n"
           << "-----------------------------------------------------------------------------\n"
           << "  arm       rate  chunks   dt (ms)   grid (Hz)     steps   activity  frozen"
              "    corr  cycle  alive\n";
        for (std::size_t c = 0; c < kCellCount; ++c) {
            const Cell& cell = kCells[c];
            const double dtMs =
                1000.0 *
                static_cast<double>(cell.chunks * EcosystemEngine::kControlChunkSamples) /
                cell.sampleRate;
            for (std::size_t s = 0; s < kSeedCount; ++s) {
                const CellResult& r = results[c][s];
                os << "  " << std::left << std::setw(8) << cell.arm << std::right << std::fixed
                   << std::setprecision(0) << std::setw(6) << cell.sampleRate << std::setw(8)
                   << cell.chunks << std::setprecision(3) << std::setw(10) << dtMs
                   << std::setprecision(5) << std::setw(12) << r.gridHz << std::setw(10)
                   << r.steps << std::setprecision(4) << std::setw(11) << r.activity
                   << std::setw(8) << r.frozen << std::setprecision(3) << std::setw(8) << r.corr
                   << std::setw(7) << (r.cycle ? "YES" : "no") << std::setw(7)
                   << (r.alive ? "yes" : "NO") << "\n";
            }
        }

        os << "-----------------------------------------------------------------------------\n"
           << "  per-cell cross-seed spread of lateActivity, (max - min) / mean\n";
        double worstSpread = 0.0;
        for (std::size_t c = 0; c < kCellCount; ++c) {
            double lo = results[c][0].activity;
            double hi = results[c][0].activity;
            double mean = 0.0;
            for (std::size_t s = 0; s < kSeedCount; ++s) {
                lo = std::min(lo, results[c][s].activity);
                hi = std::max(hi, results[c][s].activity);
                mean += results[c][s].activity / static_cast<double>(kSeedCount);
            }
            const double spread = (mean > 0.0) ? (hi - lo) / mean : 0.0;
            worstSpread = std::max(worstSpread, spread);
            os << "  " << std::left << std::setw(8) << kCells[c].arm << std::right << std::fixed
               << std::setprecision(0) << std::setw(6) << kCells[c].sampleRate << std::setw(8)
               << kCells[c].chunks << std::setprecision(4) << "   mean " << std::setw(9) << mean
               << "   min " << std::setw(9) << lo << "   max " << std::setw(9) << hi
               << "   spread " << std::setw(9) << spread << "\n";
        }
        os << "  worst cross-seed spread: " << std::setprecision(4) << worstSpread
           << "  (REPORTED - SC-010 (c) states no band; one derived from this\n"
           << "   table may be added by amendment, per the criterion's own last sentence)\n"
           << "  wall clock             : " << std::setprecision(1) << wallSeconds << " s ("
           << std::setprecision(2) << (wallSeconds / 60.0) << " min)\n"
           << "=============================================================================\n";
        WARN(os.str());
    }

    // ---- SC-010 (a): the verdict, in all 18 cells ----------------------------
    for (std::size_t c = 0; c < kCellCount; ++c) {
        for (std::size_t s = 0; s < kSeedCount; ++s) {
            const CellResult& r = results[c][s];
            INFO("arm " << kCells[c].arm << ", sampleRate " << kCells[c].sampleRate
                        << ", stepIntervalChunks " << kCells[c].chunks << ", seed 0x" << std::hex
                        << kDefaultsSeeds[s] << std::dec << ": activity " << r.activity
                        << " (gate >= " << Eco::kAliveActivity << "), frozen " << r.frozen
                        << " of " << engine->getAgentCount() << " (gate <= "
                        << Eco::kAliveMaxFrozenFraction << " of the population), cycle "
                        << (r.cycle ? "YES" : "no"));
            REQUIRE(r.alive);
        }
    }
}
