// ==============================================================================
// Layer 3: System Tests - EcosystemEngine behaviour
//                              (specs/vorago-phase8-ecosystem)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase8-ecosystem/spec.md
//            specs/vorago-phase8-ecosystem/plan.md   (S10.1 TU assignment)
//            specs/vorago-phase8-ecosystem/tasks.md  (T001 creates this stub)
//
// Criteria owned (plan S10.1): SC-002, SC-004, SC-006, SC-007, SC-008, SC-009 (d),
// SC-010, SC-014, SC-015, SC-019, SC-020, SC-021, plus the FR-011 / FR-021 /
// FR-064 and perturb-edge cases. Untagged. This TU is also the only definition of
// Krate::DSP::detail::EcosystemEngineInspectProbe (T008).
//
// STUB: T001 registers this TU with CMake so later tasks' cases actually run.
// Cases land in T003 onwards.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/ecosystem_engine.h>

// ALLOCATION DETECTION: include <allocation_detector.h> ONLY. dsp_systems_tests
// already has the single owner of the global operator new/delete overrides
// (<allocation_operator_overrides.h>); a second include is a duplicate-symbol
// link error (tasks.md, the test conventions block).
#include <allocation_detector.h>

// SHARED METRIC CODE (plan S10.1 / S14 D-D). The verdict function, the late-window
// statistics, the pairwise-correlation metric, the recurrence scan and
// distinctPositions() are needed by BOTH this TU (SC-002, SC-004) and the longrun
// TU (SC-003, SC-005, SC-013, SC-017, SC-018); the header is test-local and carries
// no CMake entry (the dsp_systems_tests source list names .cpp only).
#include "ecosystem_metrics_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// @brief Locate ecosystem_engine.h from THIS translation unit's own path.
///
/// Deriving the path from __FILE__ rather than from a build-time define means a
/// header that has been moved or renamed surfaces as a FAILURE (the FAIL below),
/// never as a case that silently passes because it read nothing.
[[nodiscard]] std::filesystem::path ecosystemEngineHeaderPath() {
    // dsp/tests/unit/systems -> dsp -> dsp/include/krate/dsp/systems
    return std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "include" /
           "krate" / "dsp" / "systems" / "ecosystem_engine.h";
}

/// @brief Strip C++ comments from one line, carrying /* */ state across lines.
///
/// WHY THIS EXISTS. The checks below look for FORBIDDEN CODE (std::isnan and
/// friends, heap terms). A raw substring search over the file cannot tell a
/// call from a sentence, so the header comment that DOCUMENTS the rule -
/// "detail::isFinite(double) ONLY - never std::isnan/isinf/isfinite" - tripped
/// the very check it describes. The repo's canonical gate already settles the
/// semantics: tools/lint-nonfinite-symbols.js, "NOT flagged (deliberately):
/// ... Comments. A line whose match sits after `//` or inside a /* */ block is
/// documentation ABOUT the rule". This test now scans the same surface.
///
/// Double-quoted string literals are tracked so a `"//"` inside one cannot
/// swallow the rest of a line of real code (over-stripping would HIDE a
/// violation, which is the dangerous direction). Character literals are not
/// tracked - the header contains none, and a stray `'/'` would only ever
/// under-strip, i.e. fail loudly rather than pass silently.
[[nodiscard]] std::string stripComments(const std::string& line, bool& inBlockComment) {
    std::string out;
    bool inString = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        const bool hasNext = (i + 1u) < line.size();
        if (inBlockComment) {
            if (c == '*' && hasNext && line[i + 1u] == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inString) {
            out += c;
            if (c == '\\' && hasNext) {
                out += line[i + 1u];
                ++i;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '/' && hasNext && line[i + 1u] == '/') {
            break;  // rest of the line is a comment
        }
        if (c == '/' && hasNext && line[i + 1u] == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
        }
        out += c;
    }
    return out;
}

}  // namespace

// ==============================================================================
// T003 - FR-001 / SC-015: the include block is Layer 0 + stdlib ONLY
// ==============================================================================
// WHY A TEST AND NOT A LINT. tools/lint-layers.js:5-8 only forbids an UPWARD
// include (layer N reaching above N). A Layer-3 header including a Layer-1 or
// Layer-2 header is legal to it, so it structurally cannot see FR-001's stricter
// "Layer 0 + stdlib only" promise for this component (plan S14 D-J). The thing
// that can silently rot therefore gets a test, not a comment.
TEST_CASE("EcosystemEngine_HeaderIncludesOnlyLayerZero", "[ecosystem_engine]") {
    const std::filesystem::path headerPath = ecosystemEngineHeaderPath();

    std::ifstream header(headerPath);
    if (!header.is_open()) {
        FAIL("header not found at " + headerPath.string());
    }

    const std::string kIncludePrefix = "#include <krate/dsp/";

    // COMMENTS ARE STRIPPED. Every clause below is about what the header DOES,
    // so it is scanned over code only - see stripComments() above for the
    // precedent this follows.
    std::string contents;
    std::string rawLine;
    std::size_t krateIncludeCount = 0;
    bool inBlockComment = false;

    while (std::getline(header, rawLine)) {
        const std::string line = stripComments(rawLine, inBlockComment);
        const std::string::size_type pos = line.find(kIncludePrefix);
        if (pos != std::string::npos) {
            const std::string rest = line.substr(pos + kIncludePrefix.size());
            INFO("offending include line: " << rawLine);
            // Every in-tree include must name core/ - Layer 0.
            REQUIRE(rest.rfind("core/", 0) == 0);
            ++krateIncludeCount;
        }
        contents += line;
        contents += '\n';
    }

    // A header that ends inside an unterminated /* would hide everything after
    // it from every clause below, so the scan must have closed cleanly.
    REQUIRE_FALSE(inBlockComment);

    // Non-vacuity: a header whose include block was deleted must NOT pass. The
    // component needs core/random.h (Xorshift32, deriveStreamSeed) and
    // core/db_utils.h (detail::isFinite), so two is the floor.
    REQUIRE(krateIncludeCount >= 2u);

    // FR-083 / tools/lint-nonfinite-symbols.js: the std:: predicates fold away
    // under -ffast-math; the house remedy is detail::isNaN / isInf / isFinite.
    REQUIRE(contents.find("std::isnan") == std::string::npos);
    REQUIRE(contents.find("std::isinf") == std::string::npos);
    REQUIRE(contents.find("std::isfinite") == std::string::npos);

    // FR-003: no heap term anywhere, and no header that would introduce one.
    REQUIRE(contents.find("#include <vector>") == std::string::npos);
    REQUIRE(contents.find("#include <memory>") == std::string::npos);
    REQUIRE(contents.find("new ") == std::string::npos);
    REQUIRE(contents.find("malloc") == std::string::npos);
}

// ==============================================================================
// T004 - FR-041 / SC-001 (c) / SC-006 (a): prepare() is a CONSERVING PARTITION
// ==============================================================================
// Seven clauses, and each one guards a specific way the partition has already
// been got wrong:
//   (1) the three-way split closes to double rounding;
//   (2) the agent share is exactly (1 - initialPoolFraction) * energyBudget;
//   (3) NO CELL IS EMPTY - starting the field at zero gave every seed the same
//       opening transient and a cross-seed correlation of 0.60
//       (FR-041, ecosystem-sim.js:219-232);
//   (4) the pool holds exactly the remainder, and it is strictly positive;
//   (5) the resource field never takes more than half of what the agents left
//       (the min(resSum, remaining * 0.5) cap);
//   (6) the derived clock quantities are what the configuration says they are,
//       and prepare() has fired NO simulation step;
//   (7) two instances prepared identically are BIT-IDENTICAL - same binary, so
//       an exact comparison is legal here (it is a structural identity, not a
//       cross-toolchain float golden).
TEST_CASE("EcosystemEngine_PrepareIsAConservingPartition", "[ecosystem_engine]") {
    using Engine = Krate::DSP::EcosystemEngine;

    // The spec's Appendix-A defaults, spelled out with designated initialisers
    // (a positional brace init would hide a narrowing conversion Clang errors on
    // and MSVC does not - resonance_drift_network.h:297-306).
    const Engine::PrepareConfig cfg{.agentCount = 32u,
                                    .resourceCells = 64u,
                                    .energyBudget = 1.0,
                                    .initialPoolFraction = 0.5,
                                    .stepIntervalChunks = 8u};

    // ~23.5 KB: constructed through make_unique, never as a plain stack local.
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, cfg);

    const double budget = engine->getEnergyBudget();
    REQUIRE(budget == 1.0);
    REQUIRE(engine->getAgentCount() == 32u);
    REQUIRE(engine->getResourceCells() == 64u);

    // (1) the three-way split closes exactly to double rounding.
    REQUIRE(std::abs(engine->getTotalEnergy() - budget) / budget <= 1.0e-12);

    // (2) the agents hold (1 - initialPoolFraction) * energyBudget.
    double agentSum = 0.0;
    for (std::size_t i = 0; i < engine->getAgentCount(); ++i) {
        agentSum += engine->getAgentEnergy(i);
    }
    const double agentTarget = (1.0 - engine->getInitialPoolFraction()) * budget;
    REQUIRE(agentTarget > 0.0);
    REQUIRE(std::abs(agentSum - agentTarget) / agentTarget <= 1.0e-12);

    // (3) NO CELL IS EMPTY.
    double cellSum = 0.0;
    for (std::size_t k = 0; k < engine->getResourceCells(); ++k) {
        INFO("resource cell " << k);
        REQUIRE(engine->getCellEnergy(k) > 0.0);
        cellSum += engine->getCellEnergy(k);
    }

    // (4) the pool is the remainder, and it is strictly positive.
    REQUIRE(engine->getPoolEnergy() > 0.0);
    REQUIRE(std::abs(engine->getPoolEnergy() - (budget - agentSum - cellSum)) <= 1.0e-12);

    // (5) the min(resSum, remaining * 0.5) cap.
    REQUIRE(cellSum <= 0.5 * (budget - agentSum) + 1.0e-12);

    // (6) the derived clock, and no step fired.
    REQUIRE(engine->getStepDurationSeconds() == 8.0 * 64.0 / 48000.0);
    REQUIRE(engine->getSampleRate() == 48000.0);
    REQUIRE(engine->getStepIntervalChunks() == 8u);
    REQUIRE(engine->getControlStepCount() == 0u);
    REQUIRE(engine->isPrepared());
    REQUIRE(engine->getAllocatedBytes() == 0u);

    // (7) two identically-prepared instances are bit-identical. Same binary, so
    // this is a structural identity and not a cross-toolchain float golden.
    auto twin = std::make_unique<Engine>();
    twin->setSeed(0xC0FFEEu);
    twin->prepare(48000.0, cfg);

    constexpr std::size_t kAgents = 32u;
    constexpr std::size_t kGathered = 4u * kAgents + 1u;
    std::array<double, kGathered> first{};
    std::array<double, kGathered> second{};
    for (std::size_t i = 0; i < kAgents; ++i) {
        first[4u * i + 0u] = engine->getAgentEnergy(i);
        first[4u * i + 1u] = engine->getAgentPositionX(i);
        first[4u * i + 2u] = engine->getAgentPositionY(i);
        first[4u * i + 3u] = engine->getAgentPhase(i);
        second[4u * i + 0u] = twin->getAgentEnergy(i);
        second[4u * i + 1u] = twin->getAgentPositionX(i);
        second[4u * i + 2u] = twin->getAgentPositionY(i);
        second[4u * i + 3u] = twin->getAgentPhase(i);
    }
    first[kGathered - 1u] = engine->getPoolEnergy();
    second[kGathered - 1u] = twin->getPoolEnergy();

    // BYTE equality is the assertion here, not value equality: both sides came
    // from one seed through one deterministic code path in one binary, so -0.0
    // vs +0.0 and a changed NaN payload are divergences that `==` would accept
    // and memcmp must not. This is NOT a cross-toolchain float golden.
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
    REQUIRE(std::memcmp(first.data(), second.data(), kGathered * sizeof(double)) == 0);
}

// ==============================================================================
// T005 - FR-064 / plan S8 (iii): every knob CLAMPS, and knob getters have NO
//        unprepared neutral
// ==============================================================================
// WHY THIS CASE EXISTS AT ALL (plan S14 D-O). FR-064 specifies three normative
// setter behaviours and NO spec criterion gates value clamping: SC-001 and
// SC-013 draw every knob INSIDE its range, so nothing else in the suite ever
// probes past a bound. Yet plan S7.3's whole containment argument leans on
// `sigmaSq_ > 0` PRECISELY BECAUSE setKernelSigma clamps to [0.01, 0.35] - a
// setter that stored 0 there would divide by zero in the FR-033 gradient and no
// other case would notice. This is the case that keeps that true.
//
// The table below carries the SAME Appendix-A bounds the header's S1.5 range
// comments carry. If a bound moves in one place and not the other, a row fails.
namespace {

using Engine = Krate::DSP::EcosystemEngine;

/// @brief One Appendix-A knob: how to drive it, how to read it, and its bounds.
///
/// The accessors are captureless lambdas converted to plain function pointers,
/// so the whole table is a POD with no heap term - it may sit inside the
/// AllocationScope below without being the thing that allocates.
struct KnobRow {
    const char* name;
    void (*set)(Engine&, float);
    float (*get)(const Engine&);
    float minimum;
    float maximum;
    float inRange;
    float defaultValue;  ///< the S1.5 member-initialised Appendix-A default
};

constexpr std::size_t kKnobCount = 22u;

// freqLoHz and freqHiHz are driven through the ONE paired setter
// (setFreqRangeHz), each holding the other end at its current value - that is
// the only way a caller can move one end at all, and it is why the pair can
// never transiently invert.
const std::array<KnobRow, kKnobCount> kKnobTable{{
    {"kernelSigma", [](Engine& e, float v) { e.setKernelSigma(v); },
     [](const Engine& e) { return e.getKernelSigma(); }, 0.01f, 0.35f, 0.12f, 0.03f},
    {"exchangeRate", [](Engine& e, float v) { e.setExchangeRate(v); },
     [](const Engine& e) { return e.getExchangeRate(); }, 0.0f, 3.0f, 1.0f, 0.35f},
    {"predation", [](Engine& e, float v) { e.setPredation(v); },
     [](const Engine& e) { return e.getPredation(); }, 0.0f, 1.0f, 0.55f, 0.55f},
    {"preyFloorShares", [](Engine& e, float v) { e.setPreyFloorShares(v); },
     [](const Engine& e) { return e.getPreyFloorShares(); }, 0.0f, 1.6f, 0.5f, 0.5f},
    {"capacityShares", [](Engine& e, float v) { e.setCapacityShares(v); },
     [](const Engine& e) { return e.getCapacityShares(); }, 0.32f, 32.0f, 16.0f, 32.0f},
    {"leakRate", [](Engine& e, float v) { e.setLeakRate(v); },
     [](const Engine& e) { return e.getLeakRate(); }, 0.0f, 1.0f, 0.06f, 0.06f},
    {"leakExponent", [](Engine& e, float v) { e.setLeakExponent(v); },
     [](const Engine& e) { return e.getLeakExponent(); }, 1.0f, 2.5f, 1.2f, 1.0f},
    {"moveRate", [](Engine& e, float v) { e.setMoveRate(v); },
     [](const Engine& e) { return e.getMoveRate(); }, 0.0f, 0.5f, 0.20f, 0.20f},
    {"maxSpeed", [](Engine& e, float v) { e.setMaxSpeed(v); },
     [](const Engine& e) { return e.getMaxSpeed(); }, 0.001f, 0.05f, 0.03f, 0.03f},
    {"forageRate", [](Engine& e, float v) { e.setForageRate(v); },
     [](const Engine& e) { return e.getForageRate(); }, 0.0f, 0.05f, 0.01f, 0.010f},
    {"crowding", [](Engine& e, float v) { e.setCrowding(v); },
     [](const Engine& e) { return e.getCrowding(); }, 0.0f, 0.2f, 0.05f, 0.05f},
    {"crowdingRadius", [](Engine& e, float v) { e.setCrowdingRadius(v); },
     [](const Engine& e) { return e.getCrowdingRadius(); }, 0.005f, 0.05f, 0.02f, 0.02f},
    {"syncRate", [](Engine& e, float v) { e.setSyncRate(v); },
     [](const Engine& e) { return e.getSyncRate(); }, 0.0f, 0.5f, 0.1f, 0.0f},
    {"cellCapacityShares", [](Engine& e, float v) { e.setCellCapacityShares(v); },
     [](const Engine& e) { return e.getCellCapacityShares(); }, 0.32f, 12.8f, 3.2f, 3.2f},
    {"regenRate", [](Engine& e, float v) { e.setRegenRate(v); },
     [](const Engine& e) { return e.getRegenRate(); }, 0.0f, 1.0f, 0.05f, 0.05f},
    {"grazeRate", [](Engine& e, float v) { e.setGrazeRate(v); },
     [](const Engine& e) { return e.getGrazeRate(); }, 0.0f, 3.0f, 0.75f, 0.75f},
    {"feedRate", [](Engine& e, float v) { e.setFeedRate(v); },
     [](const Engine& e) { return e.getFeedRate(); }, 0.0f, 1.0f, 0.0f, 0.0f},
    {"satiationShares", [](Engine& e, float v) { e.setSatiationShares(v); },
     [](const Engine& e) { return e.getSatiationShares(); }, 0.0f, 16.0f, 4.0f, 0.0f},
    {"appetiteDepth", [](Engine& e, float v) { e.setAppetiteDepth(v); },
     [](const Engine& e) { return e.getAppetiteDepth(); }, 0.0f, 1.0f, 0.8f, 0.8f},
    {"freqLoHz", [](Engine& e, float v) { e.setFreqRangeHz(v, e.getFreqHiHz()); },
     [](const Engine& e) { return e.getFreqLoHz(); }, 0.0005f, 0.005f, 0.0015f, 0.0015f},
    {"freqHiHz", [](Engine& e, float v) { e.setFreqRangeHz(e.getFreqLoHz(), v); },
     [](const Engine& e) { return e.getFreqHiHz(); }, 0.006f, 0.05f, 0.018f, 0.0180f},
    {"freqDrift", [](Engine& e, float v) { e.setFreqDrift(v); },
     [](const Engine& e) { return e.getFreqDrift(); }, 0.0f, 0.0002f, 0.00004f, 0.00004f},
}};

/// @brief What one row's three calls actually stored.
///
/// The probe RECORDS and asserts nothing: Catch2's own assertion and INFO
/// machinery reaches the heap, so every REQUIRE in this case is made OUTSIDE the
/// AllocationScope (the resonance_drift_network_test.cpp:588-600 idiom).
struct KnobProbe {
    float atMin = 0.0f;
    float atMax = 0.0f;
    float atInRange = 0.0f;
    float epsilon = 0.0f;
};

/// @brief Drive one row three times: `min - eps`, `max + eps`, one in-range value.
///
/// `eps` is 1e-4 SCALED TO THE KNOB (its span), so the overshoot is comparable
/// at freqDrift's 2e-4 span and at capacityShares' 31.68 one, and is always far
/// enough outside the bound to be representable in float at that magnitude.
[[nodiscard]] KnobProbe probeKnob(const KnobRow& row, Engine& engine) {
    KnobProbe probe{};
    probe.epsilon = 1.0e-4f * (row.maximum - row.minimum);

    row.set(engine, row.minimum - probe.epsilon);
    probe.atMin = row.get(engine);

    row.set(engine, row.maximum + probe.epsilon);
    probe.atMax = row.get(engine);

    row.set(engine, row.inRange);
    probe.atInRange = row.get(engine);

    return probe;
}

/// @brief One affinity entry driven past both rails.
struct AffinityProbe {
    float aboveMax = 0.0f;
    float belowMin = 0.0f;
};

}  // namespace

TEST_CASE("EcosystemEngine_SettersClampToRange", "[ecosystem_engine]") {
    // ~23.5 KB (plan S9): constructed through make_unique and OUTSIDE the
    // AllocationScope, or the construction itself is the allocation counted.
    auto engine = std::make_unique<Engine>();

    // Everything below is RECORDED inside the scope and ASSERTED after it.
    std::array<float, kKnobCount> defaults{};
    std::array<KnobProbe, kKnobCount> beforePrepare{};
    std::array<KnobProbe, kKnobCount> afterPrepare{};
    std::array<float, kKnobCount> survivedPrepare{};
    std::array<float, kKnobCount> beforePrepareFinal{};

    constexpr std::size_t kAffinityPairs = 4u;
    const std::array<std::pair<Engine::Kind, Engine::Kind>, kAffinityPairs> kPairs{{
        {Engine::Kind::Partial, Engine::Kind::Partial},  // the DIAGONAL entry
        {Engine::Kind::Partial, Engine::Kind::Resonator},
        {Engine::Kind::Noise, Engine::Kind::Feedback},
        {Engine::Kind::Ghost, Engine::Kind::Noise},
    }};
    std::array<AffinityProbe, kAffinityPairs> affinity{};

    // An out-of-range Kind is reachable ONLY through a cast, since Kind is a
    // scoped enum. Both argument positions are exercised.
    const auto kBadKind = static_cast<Engine::Kind>(7);
    float badFromGet = -1.0f;
    float badToGet = -1.0f;
    float diagonalAfterBadWrites = 0.0f;

    float invertedLo = 0.0f;
    float invertedHi = 0.0f;
    float invertedLo2 = 0.0f;
    float invertedHi2 = 0.0f;

    std::size_t allocations = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        // ---- (a) S8 (iii): a knob getter has NO unprepared neutral ---------
        // On a default-constructed instance every knob reports its Appendix-A
        // default, not 0.0f. A 0 here would break FR-064's round trip and, for
        // kernelSigma, would report a value the setter may not even store.
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            defaults[i] = kKnobTable[i].get(*engine);
        }

        // ---- (b) the whole table, BEFORE prepare() -------------------------
        // FR-007: every setter is callable on an unprepared object and its
        // clamped value is visible through its getter immediately.
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            beforePrepare[i] = probeKnob(kKnobTable[i], *engine);
        }
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            beforePrepareFinal[i] = kKnobTable[i].get(*engine);
        }

        engine->prepare(48000.0, Engine::PrepareConfig{.agentCount = 32u,
                                                       .resourceCells = 64u,
                                                       .energyBudget = 1.0,
                                                       .initialPoolFraction = 0.5,
                                                       .stepIntervalChunks = 8u});

        // ---- (c) S2.1 step 8: prepare() re-derives STATE, never CONFIG -----
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            survivedPrepare[i] = kKnobTable[i].get(*engine);
        }

        // ---- (d) the whole table again, AFTER prepare() --------------------
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            afterPrepare[i] = probeKnob(kKnobTable[i], *engine);
        }

        // ---- (e) setAffinity: FR-031's [-2, +2] rails ----------------------
        for (std::size_t p = 0; p < kAffinityPairs; ++p) {
            engine->setAffinity(kPairs[p].first, kPairs[p].second, 3.0f);
            affinity[p].aboveMax = engine->getAffinity(kPairs[p].first, kPairs[p].second);
            engine->setAffinity(kPairs[p].first, kPairs[p].second, -3.0f);
            affinity[p].belowMin = engine->getAffinity(kPairs[p].first, kPairs[p].second);
        }

        // An out-of-range Kind is a SILENT no-op that writes nothing and reads
        // nothing; its getter returns the S8 (i) neutral 0.0f.
        engine->setAffinity(kBadKind, Engine::Kind::Partial, 1.75f);
        engine->setAffinity(Engine::Kind::Partial, kBadKind, 1.75f);
        badFromGet = engine->getAffinity(kBadKind, Engine::Kind::Partial);
        badToGet = engine->getAffinity(Engine::Kind::Partial, kBadKind);
        diagonalAfterBadWrites = engine->getAffinity(Engine::Kind::Partial, Engine::Kind::Partial);

        // ---- (f) setFreqRangeHz with the pair INVERTED ---------------------
        // S1.4's ordering rule. Each end is clamped to its own Appendix-A range
        // first, then `hi >= lo + 1e-4f` is enforced.
        engine->setFreqRangeHz(0.005f, 0.0005f);
        invertedLo = engine->getFreqLoHz();
        invertedHi = engine->getFreqHiHz();

        engine->setFreqRangeHz(0.05f, 0.0005f);
        invertedLo2 = engine->getFreqLoHz();
        invertedHi2 = engine->getFreqHiHz();

        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    // ---- assertions, all OUTSIDE the AllocationScope -----------------------
    REQUIRE(allocations == 0u);
    REQUIRE(engine->getAllocatedBytes() == 0u);

    for (std::size_t i = 0; i < kKnobCount; ++i) {
        const KnobRow& row = kKnobTable[i];
        INFO("knob: " << row.name);

        // (a) the Appendix-A default, NOT 0.0f.
        REQUIRE(defaults[i] == row.defaultValue);

        // The overshoot must actually be an overshoot at this knob's magnitude.
        REQUIRE(beforePrepare[i].epsilon > 0.0f);
        REQUIRE(row.minimum - beforePrepare[i].epsilon < row.minimum);
        REQUIRE(row.maximum + beforePrepare[i].epsilon > row.maximum);

        // (b) before prepare(): clamped to the bound, or stored exactly.
        REQUIRE(beforePrepare[i].atMin == row.minimum);
        REQUIRE(beforePrepare[i].atMax == row.maximum);
        REQUIRE(beforePrepare[i].atInRange == row.inRange);

        // (c) prepare() re-derives state, never configuration.
        REQUIRE(survivedPrepare[i] == beforePrepareFinal[i]);

        // (d) after prepare(): identical behaviour, identical values.
        REQUIRE(afterPrepare[i].atMin == row.minimum);
        REQUIRE(afterPrepare[i].atMax == row.maximum);
        REQUIRE(afterPrepare[i].atInRange == row.inRange);
    }

    for (std::size_t p = 0; p < kAffinityPairs; ++p) {
        INFO("affinity pair " << p);
        REQUIRE(affinity[p].aboveMax == Engine::kMaxAffinity);
        REQUIRE(affinity[p].belowMin == Engine::kMinAffinity);
    }
    REQUIRE(Engine::kMaxAffinity == 2.0f);
    REQUIRE(Engine::kMinAffinity == -2.0f);

    REQUIRE(badFromGet == 0.0f);
    REQUIRE(badToGet == 0.0f);
    // The two no-op writes wrote NOTHING - the diagonal entry still holds the
    // -2.0f the rail probe above left there.
    REQUIRE(diagonalAfterBadWrites == Engine::kMinAffinity);

    // (f) The ordering invariant holds after an inverted call. NOTE for a later
    // reader: it holds here by CONSTRUCTION as well as by enforcement, because
    // Appendix A's two ranges are disjoint (lo <= 0.005, hi >= 0.006) so the
    // post-clamp gap is never below 0.001. The `hi >= lo + 1e-4f` line is what
    // keeps the invariant true if either range is ever widened; this assertion
    // is what fails if the pair is ever stored unordered.
    REQUIRE(invertedLo == 0.005f);
    REQUIRE(invertedHi >= invertedLo + 1.0e-4f);
    REQUIRE(invertedLo2 == 0.005f);
    REQUIRE(invertedHi2 >= invertedLo2 + 1.0e-4f);
    REQUIRE(invertedHi2 == 0.006f);
}

// ==============================================================================
// T006 - shared fixtures for the five behaviour cases below
// ==============================================================================
namespace {

/// @brief The Appendix-A / PrepareConfig defaults, spelled out once.
///
/// Designated initialisers only (resonance_drift_network.h:297-306): a positional
/// brace init can hide a narrowing conversion Clang errors on and MSVC does not,
/// which is a Windows-green / CI-red construct.
[[nodiscard]] Engine::PrepareConfig defaultConfig() noexcept {
    return Engine::PrepareConfig{.agentCount = 32u,
                                 .resourceCells = 64u,
                                 .energyBudget = 1.0,
                                 .initialPoolFraction = 0.5,
                                 .stepIntervalChunks = 8u};
}

/// The largest representable index, i.e. the most hostile out-of-range probe an
/// FR-060 accessor can be handed. std::numeric_limits is used ONLY on an INTEGRAL
/// type here - the -ffast-math hazard the conventions block names applies to the
/// float/double quiet_NaN()/infinity() members, which fold to finite garbage.
constexpr std::size_t kSizeMax = std::numeric_limits<std::size_t>::max();

/// @brief Does EVERY per-agent indexed accessor report its S8 (i) neutral at @p idx?
///
/// Returned as one bool rather than asserted in place so the whole sweep can run
/// inside an AllocationScope: Catch2's REQUIRE and INFO machinery reaches the
/// heap, and the thing under test here is that the ACCESSORS do not.
[[nodiscard]] bool agentNeutralsHold(const Engine& e, std::size_t idx) noexcept {
    return e.getAgentOutput(idx) == 0.0f && e.getAgentEnergy(idx) == 0.0 &&
           e.getAgentKind(idx) == Engine::Kind::Partial && e.getAgentPositionX(idx) == 0.0 &&
           e.getAgentPositionY(idx) == 0.0 && e.getAgentPhase(idx) == 0.0 &&
           e.getAgentWake(idx) == 0.0f && !e.isAgentDormant(idx) &&
           e.getAgentClampedStepFraction(idx) == 0.0f;
}

/// @brief Every FR-065 counter and clock read-back that plan S8 (ii) makes neutral.
[[nodiscard]] bool stateCountersAreZero(const Engine& e) noexcept {
    return e.getControlStepCount() == 0u && e.getPairInteractionCount() == 0u &&
           e.getConservationViolationCount() == 0u && e.getNonFiniteContainmentCount() == 0u &&
           e.getOutputClampEngagementCount() == 0u;
}

// The gathered-state widths the bit-identity comparisons below use. Named
// constants because SC-008 and SC-006 (c) gather the SAME shape, and a gather
// that silently shrank would weaken both criteria at once.
constexpr std::size_t kGatherAgents = 32u;
constexpr std::size_t kGatherCells = 64u;
/// 4 doubles per agent (energy, x, y, phase), every cell, and the pool.
constexpr std::size_t kGatherDoubles = 4u * kGatherAgents + kGatherCells + 1u;
/// 3 floats per agent (output, wake, clamped-step fraction).
constexpr std::size_t kGatherFloats = 3u * kGatherAgents;

/// @brief Gather every conserved double the simulation owns, in a fixed order.
[[nodiscard]] std::array<double, kGatherDoubles> gatherDoubles(const Engine& e) noexcept {
    std::array<double, kGatherDoubles> out{};
    for (std::size_t i = 0; i < kGatherAgents; ++i) {
        out[4u * i + 0u] = e.getAgentEnergy(i);
        out[4u * i + 1u] = e.getAgentPositionX(i);
        out[4u * i + 2u] = e.getAgentPositionY(i);
        out[4u * i + 3u] = e.getAgentPhase(i);
    }
    for (std::size_t k = 0; k < kGatherCells; ++k) {
        out[4u * kGatherAgents + k] = e.getCellEnergy(k);
    }
    out[kGatherDoubles - 1u] = e.getPoolEnergy();
    return out;
}

/// @brief Gather every published float - the surface FR-060's consumers read.
[[nodiscard]] std::array<float, kGatherFloats> gatherFloats(const Engine& e) noexcept {
    std::array<float, kGatherFloats> out{};
    for (std::size_t i = 0; i < kGatherAgents; ++i) {
        out[3u * i + 0u] = e.getAgentOutput(i);
        out[3u * i + 1u] = e.getAgentWake(i);
        out[3u * i + 2u] = e.getAgentClampedStepFraction(i);
    }
    return out;
}

}  // namespace

// ==============================================================================
// T006 (1) - SC-007 (b) / FR-007 / plan S8: an UNPREPARED object is neutral,
//            and the THREE getter classes are asserted SEPARATELY
// ==============================================================================
// The three classes of plan S8 have three DIFFERENT contracts, and collapsing
// them into one "a getter returns 0 when it has nothing" rule is exactly what
// makes such a statement self-contradictory against FR-064: a knob getter that
// returned 0.0f before prepare() would break the setter/getter round trip and,
// for kernelSigma, would report a value the setter is not even allowed to store.
// So each class gets its own block below, and each block names its class.
//
// FALSIFICATION (run it, then restore): make ONE knob getter return 0.0f when
// !prepared_ - e.g. `return prepared_ ? kernelSigma_ : 0.0f;` on
// getKernelSigma(). The (ii) block still passes and THIS case fails on the (iii)
// block's first line. A getter contract that is only prose cannot fail.
TEST_CASE("EcosystemEngine_UnpreparedIsNeutral", "[ecosystem_engine]") {
    // ~23.5 KB (plan S9): make_unique, never a plain stack local, and
    // constructed OUTSIDE any AllocationScope or the construction is counted.
    auto engine = std::make_unique<Engine>();

    // ---- class (ii): STATE getters are neutral on an unprepared object -----
    // Everything the simulation PRODUCES or prepare() DERIVES. This object has
    // produced nothing, so every one of them reads zero / false.
    REQUIRE_FALSE(engine->isPrepared());
    REQUIRE(engine->getAllocatedBytes() == 0u);
    REQUIRE(engine->getPoolEnergy() == 0.0);
    REQUIRE(engine->getTotalEnergy() == 0.0);
    REQUIRE(engine->getStepDurationSeconds() == 0.0);
    REQUIRE(engine->getEnergyEntropy() == 0.0);
    REQUIRE(stateCountersAreZero(*engine));

    // ---- class (iii): KNOB getters report their APPENDIX-A DEFAULT, not 0 ---
    REQUIRE(engine->getKernelSigma() == 0.03f);
    REQUIRE(engine->getPredation() == 0.55f);
    REQUIRE(engine->getLeakExponent() == 1.0f);
    REQUIRE(engine->getSyncRate() == 0.0f);         // FR-035: OFF by default
    REQUIRE(engine->getFeedRate() == 0.0f);         // FR-057: OFF by default
    REQUIRE(engine->getSatiationShares() == 0.0f);  // FR-051: 0 = off
    REQUIRE(engine->getAffinity(Engine::Kind::Partial, Engine::Kind::Partial) == -1.0f);
    REQUIRE(engine->getAffinity(Engine::Kind::Partial, Engine::Kind::Resonator) == 0.45f);

    // ---- class (iii), continued: the PREPARE-TIME READ-BACKS ---------------
    // FR-060's contract is "read back what prepare() actually did", so before
    // prepare() they report the PrepareConfig defaults. A 0 here would be a
    // value prepare() can never produce.
    REQUIRE(engine->getAgentCount() == 32u);
    REQUIRE(engine->getResourceCells() == 64u);
    REQUIRE(engine->getEnergyBudget() == 1.0);
    REQUIRE(engine->getStepIntervalChunks() == 8u);
    REQUIRE(engine->getInitialPoolFraction() == 0.5);
    REQUIRE(engine->getSampleRate() == 48000.0);

    // ---- the no-op clock, the indexed neutrals and the setters -------------
    // All RECORDED inside the scope and ASSERTED after it: Catch2 allocates, the
    // component must not (the resonance_drift_network_test.cpp:588-600 idiom).
    const std::size_t agentCount = engine->getAgentCount();
    const std::size_t cellCount = engine->getResourceCells();
    const std::array<std::size_t, 3> kBadAgentIdx{agentCount, agentCount + 1u, kSizeMax};
    const std::array<std::size_t, 3> kBadCellIdx{cellCount, cellCount + 1u, kSizeMax};

    bool clockIsQuiet = false;
    std::array<bool, 3> agentNeutralBefore{};
    std::array<bool, 3> cellNeutralBefore{};
    bool badKindCountIsZero = false;
    std::array<float, kKnobCount> setterRoundTrip{};
    float affinityRoundTrip = 0.0f;
    std::size_t allocations = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        // FR-007 / FR-081: advancing an unprepared object does NOTHING. The
        // 1'000'000-sample call is the one that would fire ~1953 steps on a
        // prepared instance, so a missing !prepared_ guard cannot hide here.
        engine->processChunk(0u);
        engine->processChunk(1u);
        engine->processChunk(64u);
        engine->processChunk(1000000u);
        clockIsQuiet = stateCountersAreZero(*engine) && !engine->isPrepared() &&
                       engine->getPoolEnergy() == 0.0 && engine->getTotalEnergy() == 0.0 &&
                       engine->getAgentEnergy(0u) == 0.0;

        // class (i): an OUT-OF-RANGE INDEX is the only thing that makes these
        // neutral, and they must read nothing out of range.
        for (std::size_t b = 0; b < kBadAgentIdx.size(); ++b) {
            agentNeutralBefore[b] = agentNeutralsHold(*engine, kBadAgentIdx[b]);
            cellNeutralBefore[b] = (engine->getCellEnergy(kBadCellIdx[b]) == 0.0);
        }
        // A Kind outside the roster is reachable only through a cast.
        badKindCountIsZero =
            (engine->getAgentCountOfKind(static_cast<Engine::Kind>(7)) == std::size_t{0});

        // FR-064: every knob setter is callable BEFORE prepare() and its value is
        // visible through its getter immediately. setAgentWake / setAgentDormant /
        // perturbAgent are driven here for CALLABILITY and allocation only - their
        // round trip is FR-070 / FR-071 behaviour that lands with the wake ramp,
        // and the nonfinite TU owns their rejection arm.
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            kKnobTable[i].set(*engine, kKnobTable[i].inRange);
            setterRoundTrip[i] = kKnobTable[i].get(*engine);
        }
        engine->setAffinity(Engine::Kind::Noise, Engine::Kind::Ghost, 1.25f);
        affinityRoundTrip = engine->getAffinity(Engine::Kind::Noise, Engine::Kind::Ghost);
        engine->setAgentWake(0u, 0.5f);
        engine->setAgentDormant(0u, true);
        engine->perturbAgent(0u, 0.25f);

        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    REQUIRE(allocations == 0u);
    REQUIRE(clockIsQuiet);
    REQUIRE(badKindCountIsZero);
    for (std::size_t b = 0; b < kBadAgentIdx.size(); ++b) {
        INFO("out-of-range index probe " << b << " = " << kBadAgentIdx[b]);
        REQUIRE(agentNeutralBefore[b]);
        REQUIRE(cellNeutralBefore[b]);
    }
    for (std::size_t i = 0; i < kKnobCount; ++i) {
        INFO("knob: " << kKnobTable[i].name);
        REQUIRE(setterRoundTrip[i] == kKnobTable[i].inRange);
    }
    REQUIRE(affinityRoundTrip == 1.25f);

    // ---- class (i) again, AFTER prepare() ----------------------------------
    // The neutral is a property of the INDEX, not of the lifecycle: a prepared
    // object with 32 live agents must still read nothing at index 32, 33 or
    // SIZE_MAX.
    engine->prepare(48000.0, defaultConfig());
    REQUIRE(engine->isPrepared());
    REQUIRE(engine->getAllocatedBytes() == 0u);
    for (std::size_t b = 0; b < kBadAgentIdx.size(); ++b) {
        INFO("post-prepare out-of-range index probe " << b << " = " << kBadAgentIdx[b]);
        REQUIRE(agentNeutralsHold(*engine, kBadAgentIdx[b]));
        REQUIRE(engine->getCellEnergy(kBadCellIdx[b]) == 0.0);
    }
    REQUIRE(engine->getAgentCountOfKind(static_cast<Engine::Kind>(7)) == std::size_t{0});
}

// ==============================================================================
// T006 (2) - SC-008 / FR-081: the step grid is ABSOLUTE, not block-relative
// ==============================================================================
// One million samples delivered four ways must leave four identically-prepared
// instances in the SAME state, bit for bit. The exact comparison is legal here
// and is NOT a float golden: all four arms run in the SAME binary from the same
// seed, so this is a structural identity (the conventions block's SC-008 entry).
//
// FALSIFICATION (run it, then restore): reset `samplePhase_ = 0;` at the top of
// processChunk(). Arm (ii) still passes (512 is a whole number of 64-sample
// chunks), and arms (iii) and (iv) fail on the step count alone - which is
// precisely the bloom_engine.h:899 failure mode this criterion exists to catch.
TEST_CASE("EcosystemEngine_BlockPartitionInvariance", "[ecosystem_engine]") {
    constexpr std::size_t kTotalSamples = 1000000u;
    // 8 chunks * 64 samples = 512 samples per step; 1'000'000 / 512 = 1953.
    constexpr std::uint64_t kExpectedSteps = 1953u;

    std::array<std::unique_ptr<Engine>, 4> arms{};
    for (auto& arm : arms) {
        arm = std::make_unique<Engine>();
        arm->setSeed(0xC0FFEEu);
        arm->prepare(48000.0, defaultConfig());
    }

    // (i) ONE call for the whole million.
    arms[0]->processChunk(kTotalSamples);

    // (ii) 512-sample blocks - a whole step per call, the easy case.
    for (std::size_t n = 0; n < kTotalSamples; n += 512u) {
        arms[1]->processChunk(std::min(std::size_t{512}, kTotalSamples - n));
    }

    // (iii) 64-sample chunks - one control chunk per call, so every step
    //       boundary falls on a CALL boundary and chunkPhase_ must carry.
    for (std::size_t n = 0; n < kTotalSamples; n += 64u) {
        arms[2]->processChunk(std::min(std::size_t{64}, kTotalSamples - n));
    }

    // (iv) a SEEDED irregular partition - the host that never gives you the same
    //      buffer size twice. The sizes straddle the chunk (64) and the step
    //      (512) boundary in both directions, so samplePhase_ AND chunkPhase_
    //      are both mid-residue at most call boundaries.
    {
        const std::array<std::size_t, 8> kSizes{1u, 7u, 513u, 63u, 4096u, 2u, 128u, 999u};
        std::uint32_t lcg = 0xC0FFEEu;
        std::size_t sent = 0;
        while (sent < kTotalSamples) {
            lcg = lcg * 1664525u + 1013904223u;
            const std::size_t pick = static_cast<std::size_t>((lcg >> 16u) % 8u);
            const std::size_t take = std::min(kSizes[pick], kTotalSamples - sent);
            arms[3]->processChunk(take);
            sent += take;
        }
        REQUIRE(sent == kTotalSamples);
    }

    const std::array<double, kGatherDoubles> reference = gatherDoubles(*arms[0]);
    const std::array<float, kGatherFloats> referenceOut = gatherFloats(*arms[0]);
    REQUIRE(arms[0]->getControlStepCount() == kExpectedSteps);

    for (std::size_t a = 1; a < arms.size(); ++a) {
        INFO("delivery arm " << a);
        REQUIRE(arms[a]->getControlStepCount() == kExpectedSteps);

        const std::array<double, kGatherDoubles> gathered = gatherDoubles(*arms[a]);
        // Byte equality, deliberately (see the first memcmp in this file).
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
        REQUIRE(std::memcmp(gathered.data(), reference.data(),
                            kGatherDoubles * sizeof(double)) == 0);

        const std::array<float, kGatherFloats> gatheredOut = gatherFloats(*arms[a]);
        // Byte equality, deliberately (see the first memcmp in this file).
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
        REQUIRE(std::memcmp(gatheredOut.data(), referenceOut.data(),
                            kGatherFloats * sizeof(float)) == 0);
    }
}

// ==============================================================================
// T006 (3) - SC-010 (b) / FR-082: the step interval lands in band at every
//            sample-rate x stepIntervalChunks combination, and re-preparing at a
//            NEW rate leaves no stale dt_ and no stale residue
// ==============================================================================
// Nine cells. The tolerance is ONE STEP, not a percentage: the grid is exact
// arithmetic on sample counts, and the only legitimate discrepancy is the
// partial step left in the residue when the 60-second span does not divide by
// the step size.
//
// FALSIFICATION (run it, then restore): derive dt_ from kDefaultSampleRate
// instead of the floored sampleRate_. The 48 kHz row still passes and the 44.1
// and 96 kHz rows fail - a stale-rate bug a single-rate case cannot see.
TEST_CASE("EcosystemEngine_StepIntervalBand", "[ecosystem_engine]") {
    const std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
    const std::array<std::size_t, 3> kChunks{8u, 16u, 32u};  // FR-082 [8, 64]: the floor and two slower
    constexpr double kDurationSeconds = 60.0;

    auto engine = std::make_unique<Engine>();

    for (const double rate : kRates) {
        for (const std::size_t chunks : kChunks) {
            INFO("sampleRate = " << rate << ", stepIntervalChunks = " << chunks);

            engine->prepare(rate, Engine::PrepareConfig{.agentCount = 32u,
                                                        .resourceCells = 64u,
                                                        .energyBudget = 1.0,
                                                        .initialPoolFraction = 0.5,
                                                        .stepIntervalChunks = chunks});
            REQUIRE(engine->getSampleRate() == rate);
            REQUIRE(engine->getStepIntervalChunks() == chunks);
            REQUIRE(engine->getStepDurationSeconds() ==
                    static_cast<double>(chunks * Engine::kControlChunkSamples) / rate);
            REQUIRE(engine->getControlStepCount() == 0u);

            const std::size_t total = static_cast<std::size_t>(kDurationSeconds * rate);
            for (std::size_t n = 0; n < total; n += 512u) {
                engine->processChunk(std::min(std::size_t{512}, total - n));
            }

            const double expected =
                kDurationSeconds * rate /
                (static_cast<double>(chunks) * static_cast<double>(Engine::kControlChunkSamples));
            const double actual = static_cast<double>(engine->getControlStepCount());
            INFO("expected " << expected << " steps, measured " << actual);
            REQUIRE(std::abs(actual - expected) <= 1.0);
        }
    }

    // Leave a NON-ZERO residue in both clocks (300 samples = 4 whole chunks plus
    // 44), so the re-prepare below has something it can fail to clear.
    engine->processChunk(300u);

    // ---- prepare() a SECOND time, at a different rate -----------------------
    engine->prepare(96000.0, Engine::PrepareConfig{.agentCount = 32u,
                                                   .resourceCells = 64u,
                                                   .energyBudget = 1.0,
                                                   .initialPoolFraction = 0.5,
                                                   .stepIntervalChunks = 8u});
    REQUIRE(engine->getSampleRate() == 96000.0);
    REQUIRE(engine->getStepDurationSeconds() == 8.0 * 64.0 / 96000.0);
    REQUIRE(engine->getControlStepCount() == 0u);

    // BOTH RESIDUES ZEROED, proven at the boundary rather than asserted through
    // a probe: 511 samples fire no step at all, and the 512th fires exactly one.
    // A carried-over samplePhase_ or chunkPhase_ would fire the step early.
    engine->processChunk(511u);
    REQUIRE(engine->getControlStepCount() == 0u);
    engine->processChunk(1u);
    REQUIRE(engine->getControlStepCount() == 1u);

    // And the new rate's grid holds over a span, not only at one boundary.
    constexpr std::size_t kSpan = 480000u;  // 5 s at 96 kHz
    for (std::size_t n = 0; n < kSpan; n += 512u) {
        engine->processChunk(std::min(std::size_t{512}, kSpan - n));
    }
    const double expectedAfter = static_cast<double>(511u + 1u + kSpan) / 512.0;
    REQUIRE(std::abs(static_cast<double>(engine->getControlStepCount()) - expectedAfter) <= 1.0);
}

// ==============================================================================
// T006 (4) - FR-011 / FR-060: kind assignment is a STRATIFIED deal plus a
//            SEEDED SHUFFLE, not the prototype's i.i.d. draw
// ==============================================================================
// Two independent things are gated here, and they fail differently:
//   * the DEAL - the histogram is balanced to within one agent, every kind is
//     represented once the population can afford it, and the five per-kind
//     counts sum to getAgentCount() (FR-060);
//   * the SHUFFLE - kind is not a function of agent index. Without it the
//     affinity matrix sees a block structure: agents 0..8 all repel each other
//     and all sit adjacent in the table, so the kind roster becomes a positional
//     artefact rather than a population.
//
// The Spearman clause is what makes the shuffle falsifiable. A dealt-but-
// unshuffled assignment is monotone non-decreasing in the index and scores
// approximately +1.0; a shuffled one scores 0 within sampling noise (the
// standard error of the 8-seed mean at n = 48 is about 0.05, so the +/-0.25 band
// sits roughly five sigma from noise and twenty sigma from the unshuffled value).
//
// FALSIFICATION (run it, then restore): delete the Fisher-Yates loop in
// initialiseState(). The histogram clauses still pass - the deal is untouched -
// and the two shuffle clauses both fail.
namespace {

/// @brief Spearman rank correlation between agent INDEX and agent KIND index.
///
/// The kind vector is massively tied (5 distinct values over up to 48 agents), so
/// the ties get AVERAGE RANKS - the standard correction. A tie-blind rank
/// assignment would re-introduce the very index ordering the statistic exists to
/// detect, and would report a shuffled population as ordered.
[[nodiscard]] double spearmanIndexVsKind(const Engine& e) noexcept {
    const std::size_t n = e.getAgentCount();
    if (n < 2u) {
        return 0.0;
    }

    std::array<std::size_t, Engine::kNumKinds> counts{};
    for (std::size_t i = 0; i < n; ++i) {
        ++counts[static_cast<std::size_t>(e.getAgentKind(i))];
    }

    // The block of equal kind values occupies ranks [start, start + c - 1],
    // whose mean is start + (c - 1)/2. c == 0 is skipped so the unsigned
    // (c - 1) is never evaluated at zero.
    std::array<double, Engine::kNumKinds> avgRank{};
    std::size_t start = 0;
    for (std::size_t k = 0; k < Engine::kNumKinds; ++k) {
        const std::size_t c = counts[k];
        if (c > 0u) {
            avgRank[k] = static_cast<double>(start) + 0.5 * static_cast<double>(c - 1u);
            start += c;
        }
    }

    const double meanX = 0.5 * static_cast<double>(n - 1u);  // index ranks 0..n-1
    double meanY = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        meanY += avgRank[static_cast<std::size_t>(e.getAgentKind(i))];
    }
    meanY /= static_cast<double>(n);

    double num = 0.0;
    double dxx = 0.0;
    double dyy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = static_cast<double>(i) - meanX;
        const double dy = avgRank[static_cast<std::size_t>(e.getAgentKind(i))] - meanY;
        num += dx * dy;
        dxx += dx * dx;
        dyy += dy * dy;
    }
    if (dxx <= 0.0 || dyy <= 0.0) {
        return 0.0;  // a single-kind population has no rank variance to correlate
    }
    return num / std::sqrt(dxx * dyy);
}

constexpr std::size_t kSeedCount = 8u;
const std::array<std::uint32_t, kSeedCount> kKindSeeds{0xC0FFEEu,   0xC0FFEFu,   0x5EEDu,
                                                       0x1u,        0x7u,        0xDEADBEEFu,
                                                       0x12345678u, 0xFFFFFFFFu};

}  // namespace

TEST_CASE("EcosystemEngine_KindAssignmentIsStratified", "[ecosystem_engine]") {
    // 1 and 4 sit BELOW kNumKinds (FR-011 names that edge explicitly); 5 is the
    // exact deal; 32 is the default; 47 and 48 are the top of the range with and
    // without a remainder.
    const std::array<std::size_t, 6> kCounts{1u, 4u, 5u, 32u, 47u, 48u};

    auto engine = std::make_unique<Engine>();

    for (const std::size_t count : kCounts) {
        for (const std::uint32_t seed : kKindSeeds) {
            INFO("agentCount = " << count << ", seed = " << seed);

            engine->setSeed(seed);
            engine->prepare(48000.0, Engine::PrepareConfig{.agentCount = count,
                                                           .resourceCells = 64u,
                                                           .energyBudget = 1.0,
                                                           .initialPoolFraction = 0.5,
                                                           .stepIntervalChunks = 8u});
            REQUIRE(engine->getAgentCount() == count);

            std::size_t histogramSum = 0;
            std::size_t smallest = kSizeMax;
            std::size_t largest = 0;
            for (std::size_t k = 0; k < Engine::kNumKinds; ++k) {
                const std::size_t c = engine->getAgentCountOfKind(
                    static_cast<Engine::Kind>(static_cast<std::uint8_t>(k)));
                histogramSum += c;
                smallest = std::min(smallest, c);
                largest = std::max(largest, c);
            }

            // FR-060: the five per-kind counts sum to getAgentCount().
            REQUIRE(histogramSum == count);

            // The histogram must agree with a direct scan of the table, or the
            // counter and the assignment have drifted apart.
            std::array<std::size_t, Engine::kNumKinds> scanned{};
            for (std::size_t i = 0; i < count; ++i) {
                ++scanned[static_cast<std::size_t>(engine->getAgentKind(i))];
            }
            for (std::size_t k = 0; k < Engine::kNumKinds; ++k) {
                INFO("kind " << k);
                REQUIRE(scanned[k] == engine->getAgentCountOfKind(
                                          static_cast<Engine::Kind>(static_cast<std::uint8_t>(k))));
            }

            // A deal plus a ONE-AT-A-TIME remainder differs by at most one.
            INFO("histogram min " << smallest << ", max " << largest);
            REQUIRE(largest - smallest <= 1u);

            // Every kind is represented once the population can afford it.
            // Below kNumKinds, zeroes are permitted - FR-011 names that edge.
            if (count >= Engine::kNumKinds) {
                REQUIRE(smallest >= 1u);
            }

            // Class (i): an out-of-range agent index reports Kind::Partial.
            REQUIRE(static_cast<std::size_t>(engine->getAgentKind(count)) ==
                    static_cast<std::size_t>(Engine::Kind::Partial));
            REQUIRE(static_cast<std::size_t>(engine->getAgentKind(kSizeMax)) ==
                    static_cast<std::size_t>(Engine::Kind::Partial));
        }
    }

    // ---- the SHUFFLE, at the full population --------------------------------
    constexpr std::size_t kShuffleAgents = 48u;
    const std::size_t base = kShuffleAgents / Engine::kNumKinds;  // 9 per kind
    const std::size_t dealtSpan = base * Engine::kNumKinds;       // 45; the tail is remainder
    std::size_t differedFromDealOrder = 0;
    double spearmanSum = 0.0;

    for (const std::uint32_t seed : kKindSeeds) {
        engine->setSeed(seed);
        engine->prepare(48000.0, Engine::PrepareConfig{.agentCount = kShuffleAgents,
                                                       .resourceCells = 64u,
                                                       .energyBudget = 1.0,
                                                       .initialPoolFraction = 0.5,
                                                       .stepIntervalChunks = 8u});

        // The DEAL ORDER is kind_[i] == Kind(i / base) over the dealt span; the
        // shuffle must move at least one agent off it.
        bool differs = false;
        for (std::size_t i = 0; i < dealtSpan; ++i) {
            if (static_cast<std::size_t>(engine->getAgentKind(i)) != (i / base)) {
                differs = true;
                break;
            }
        }
        if (differs) {
            ++differedFromDealOrder;
        }

        spearmanSum += spearmanIndexVsKind(*engine);
    }

    INFO("seeds differing from the deal order: " << differedFromDealOrder << " of " << kSeedCount);
    REQUIRE(differedFromDealOrder >= 7u);

    const double meanRho = spearmanSum / static_cast<double>(kSeedCount);
    INFO("mean Spearman(index, kind) over " << kSeedCount << " seeds = " << meanRho);
    REQUIRE(std::abs(meanRho) <= 0.25);
}

// ==============================================================================
// T006 (5) - SC-006 (c): reset() reproduces prepare() EXACTLY
// ==============================================================================
// Instance A is prepared and left alone. Instance B is prepared identically, run
// for ten thousand control steps, and then reset(). The two must be
// indistinguishable - not close, IDENTICAL - across every conserved double,
// every published float, every counter and the step clock.
//
// The exact comparison is legal for the same reason SC-008's is: both instances
// live in ONE binary and were derived from one seed by one code path, so this is
// a structural identity, not a cross-toolchain float golden.
//
// The published-surface clause (getAgentOutput / getAgentWake) is the one the
// plan adds on top of SC-006 (a), and it is not redundant: the gates and the
// held publication are state that lives OUTSIDE the energy arrays, so a reset()
// that re-derived the economy but left a ramp or a held output behind would pass
// every energy clause and still hand Phase 10 a different modulation value.
//
// FALSIFICATION (run it, then restore): drop clearCounters() from reset(). The
// double and float gathers still match and the counter clauses fail - which is
// exactly the Clarification Q8 promise that a fuzz harness may reuse ONE instance
// across configurations without reconstructing it.
TEST_CASE("EcosystemEngine_ResetReproducesPrepare", "[ecosystem_engine]") {
    const Engine::PrepareConfig cfg = defaultConfig();

    auto a = std::make_unique<Engine>();
    a->setSeed(0xC0FFEEu);
    a->prepare(48000.0, cfg);

    auto b = std::make_unique<Engine>();
    b->setSeed(0xC0FFEEu);
    b->prepare(48000.0, cfg);

    // 8 chunks * 64 samples = one step per call, so the loop count IS the step
    // count and a miscount surfaces on the next line rather than silently.
    constexpr std::uint64_t kSteps = 10000u;
    constexpr std::size_t kSamplesPerStep = 8u * 64u;
    for (std::uint64_t s = 0; s < kSteps; ++s) {
        b->processChunk(kSamplesPerStep);
    }
    REQUIRE(b->getControlStepCount() == kSteps);

    // (*b) not b-> : `b->reset()` reads ambiguously against unique_ptr::reset
    // (readability-ambiguous-smartptr-reset-call). This resets the ENGINE.
    (*b).reset();

    REQUIRE(b->getControlStepCount() == 0u);
    REQUIRE(b->isPrepared());

    const std::array<double, kGatherDoubles> gatheredA = gatherDoubles(*a);
    const std::array<double, kGatherDoubles> gatheredB = gatherDoubles(*b);
    // Byte equality, deliberately (see the first memcmp in this file).
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
    REQUIRE(std::memcmp(gatheredA.data(), gatheredB.data(), kGatherDoubles * sizeof(double)) == 0);

    const std::array<float, kGatherFloats> floatsA = gatherFloats(*a);
    const std::array<float, kGatherFloats> floatsB = gatherFloats(*b);
    // Byte equality, deliberately (see the first memcmp in this file).
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
    REQUIRE(std::memcmp(floatsA.data(), floatsB.data(), kGatherFloats * sizeof(float)) == 0);

    // Every counter (Clarification Q8), and the derived clock.
    REQUIRE(b->getConservationViolationCount() == a->getConservationViolationCount());
    REQUIRE(b->getNonFiniteContainmentCount() == a->getNonFiniteContainmentCount());
    REQUIRE(b->getOutputClampEngagementCount() == a->getOutputClampEngagementCount());
    REQUIRE(b->getPairInteractionCount() == a->getPairInteractionCount());
    REQUIRE(b->getControlStepCount() == a->getControlStepCount());
    REQUIRE(b->getStepDurationSeconds() == a->getStepDurationSeconds());
    REQUIRE(b->getPoolEnergy() == a->getPoolEnergy());
    REQUIRE(b->getTotalEnergy() == a->getTotalEnergy());

    // The kind roster is state too: reset() re-derives it from the same seed.
    for (std::size_t k = 0; k < Engine::kNumKinds; ++k) {
        const auto kind = static_cast<Engine::Kind>(static_cast<std::uint8_t>(k));
        INFO("kind " << k);
        REQUIRE(b->getAgentCountOfKind(kind) == a->getAgentCountOfKind(kind));
    }
}

// ==============================================================================
// T008 - Krate::DSP::detail::EcosystemEngineInspectProbe
// ==============================================================================
// THIS TRANSLATION UNIT IS THE PROBE'S ONLY DEFINITION (plan S7.4, plan R-11).
// The header DECLARES it and befriends it; the library never defines it, so a
// shipping build has no way to call it. A SECOND definition anywhere - in
// another TU, or as a copy pasted into the -fno-fast-math TU - is an ODR
// violation the linker may not diagnose: it would silently pick one definition
// and the criteria below would then be asserting against the other probe's idea
// of the state layout.
//
// It is a SECOND probe rather than a member of EcosystemEngineNonFiniteProbe
// (plan A-1) because SC-020 and SC-021 must be proved in the /fp:fast +
// -ffast-math mode the header actually SHIPS in; a single probe struct has a
// single definition and would drag both criteria into the -fno-fast-math TU,
// proving them in a mode the header never ships in.
//
// Eleven accessors, none of which has - or should acquire - a public getter:
// they exist for SC-020 (scale/outflow/divided/divisions and the pair record),
// SC-021 (lastSnap), SC-002 (d) (clampedSteps, read as a WINDOWED difference of
// two boundary readings) and SC-009 (d) (setPool - a FINITE injection, which is
// why it belongs to this probe and not to the non-finite one).
namespace Krate::DSP::detail {

struct EcosystemEngineInspectProbe {
    [[nodiscard]] static std::size_t pairCount(const EcosystemEngine& e) noexcept {
        return e.pairCount_;
    }
    [[nodiscard]] static double pairFlow(const EcosystemEngine& e, std::size_t p) noexcept {
        return e.pairFlow_[p];
    }
    [[nodiscard]] static std::uint8_t pairI(const EcosystemEngine& e, std::size_t p) noexcept {
        return e.pairI_[p];
    }
    [[nodiscard]] static std::uint8_t pairJ(const EcosystemEngine& e, std::size_t p) noexcept {
        return e.pairJ_[p];
    }
    [[nodiscard]] static double scale(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.scale_[i];
    }
    [[nodiscard]] static double outflow(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.outflow_[i];
    }
    [[nodiscard]] static bool divided(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.divided_[i];
    }
    [[nodiscard]] static std::uint64_t divisions(const EcosystemEngine& e) noexcept {
        return e.exchangeDivisions_;
    }
    [[nodiscard]] static double lastSnap(const EcosystemEngine& e) noexcept {
        return e.lastDenormalSnap_;
    }
    [[nodiscard]] static std::uint64_t clampedSteps(const EcosystemEngine& e,
                                                    std::size_t i) noexcept {
        return e.clampedSteps_[i];
    }
    /// SC-014 (b) reads the FR-070 ramp HERE rather than dividing the published
    /// output by its normalisation: `raw` moves with the agent energy every step,
    /// so a ratio reading would measure the economy as much as the ramp.
    [[nodiscard]] static float gate(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.gate_[i];
    }
    /// SC-006 (a) compares EVERY agent `double` the simulation owns, and freq_
    /// is the one the spec's list names that has NO public getter (tasks.md
    /// T012). Reading it through the probe rather than growing a getter keeps
    /// FR-060's published surface exactly the list the spec enumerates.
    [[nodiscard]] static double freq(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.freq_[i];
    }
    static void setPool(EcosystemEngine& e, double v) noexcept { e.pool_ = v; }

    // --- SC-011 (c): the two exact identities of the 2026-09-16 ruling -------
    // Driven on the ENGINE'S OWN member functions and state, never on a copy of
    // the formula in this TU: a test of a transcription proves nothing about
    // the header.
    [[nodiscard]] static double agentX(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.x_[i];
    }
    [[nodiscard]] static double cellPos(const EcosystemEngine& e, std::size_t k) noexcept {
        return e.cellPos_[k];
    }
    [[nodiscard]] static double invTwoSigmaSq(const EcosystemEngine& e) noexcept {
        return e.invTwoSigmaSq_;
    }
    [[nodiscard]] static double cutDistSq(const EcosystemEngine& e) noexcept {
        return e.cutDistSq_;
    }
    [[nodiscard]] static double wrapDelta(double d) noexcept { return EcosystemEngine::wrapDelta(d); }
    [[nodiscard]] static bool kernelRunSeeded(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.cellSeeded_[i];
    }
    static void breakKernelRun(EcosystemEngine& e, std::size_t i) noexcept {
        e.cellSeeded_[i] = false;
    }
    [[nodiscard]] static double cellKernelWeight(EcosystemEngine& e, std::size_t i, double d,
                                                 double d2) noexcept {
        return e.cellKernelWeight(i, d, d2);
    }
    static void refreshPhaseTrig(EcosystemEngine& e) noexcept { e.refreshPhaseTrig(); }
    [[nodiscard]] static double sinPhase(const EcosystemEngine& e, std::size_t i) noexcept {
        return e.sinPhase_[i];
    }
    [[nodiscard]] static double pairPhaseSine(const EcosystemEngine& e, std::size_t i,
                                              std::size_t j) noexcept {
        return e.pairPhaseSine(i, j);
    }
};

}  // namespace Krate::DSP::detail

namespace {

using Probe = Krate::DSP::detail::EcosystemEngineInspectProbe;

/// @brief detail::isFinite, never std::isfinite: the std:: predicate folds away
///        under /fp:fast and -ffast-math, which is the mode this TU compiles in
///        (tools/lint-nonfinite-symbols.js).
[[nodiscard]] bool isFiniteD(double v) noexcept { return Krate::DSP::detail::isFinite(v); }

/// @brief Every SC-020 tally one arm produces, counted rather than REQUIREd in
///        place.
///
/// Counted, because an arm audits 10^4 to 10^6 agent-steps and pair
/// applications: a REQUIRE per item would spend the whole runtime inside
/// Catch2's expression machinery (which reaches the heap), and a single failure
/// would drown the arm's diagnostics. Each counter is asserted ONCE, at the end,
/// with the arm's totals captured alongside, so a red reports HOW MANY and not
/// merely THAT.
struct ExchangeScaleAudit {
    std::size_t agentSteps = 0;        ///< the audit's own non-vacuity denominator
    std::size_t zeroWantSteps = 0;     ///< agent-steps where want_i was exactly 0.0
    std::size_t pairApplications = 0;  ///< pairs examined across the whole arm
    std::size_t scaleNonFinite = 0;    ///< clause 1a
    std::size_t scaleOutOfRange = 0;   ///< clause 1b: a scale outside [0, 1]
    std::size_t zeroWantScaleNotOne = 0;  ///< clause 2a: want == 0 but scale != 1 exactly
    std::size_t zeroWantDivided = 0;      ///< clause 2b: a division performed at want == 0
    std::size_t signReversals = 0;        ///< clause 3: applied flow opposes pairFlow
};

/// @brief Run @p steps control steps, auditing the FR-023 scale surface after
///        every one of them.
///
/// The three clauses are SC-020's, and each is one a `min(1, spare/want)`
/// paraphrase of FR-023 fails:
///   1. `scale(i)` is finite and in [0, 1] - the paraphrase yields a NEGATIVE
///      scale whenever `spare < 0`, which the refuge floor makes routine;
///   2. where `want_i == 0` exactly, `scale(i) == 1.0` exactly AND
///      `divided(i) == false` - the paraphrase evaluates `0/0` (NaN) and it
///      performs the division, so it fails BOTH halves;
///   3. the applied flow `pairFlow(p) * min(scale_i, scale_j)` never opposes
///      `pairFlow(p)` - a negative scale reverses every flow in the pair, and
///      the reversal is ANTISYMMETRIC, so SC-001's conservation and boundedness
///      still hold exactly. This criterion sits at probe level precisely BECAUSE
///      SC-001 provably cannot discriminate it.
[[nodiscard]] ExchangeScaleAudit auditExchangeScale(Engine& engine, std::size_t steps) {
    ExchangeScaleAudit audit{};
    const std::size_t samplesPerStep =
        engine.getStepIntervalChunks() * Engine::kControlChunkSamples;
    const double dt = engine.getStepDurationSeconds();
    const std::size_t agents = engine.getAgentCount();

    for (std::size_t s = 0; s < steps; ++s) {
        engine.processChunk(samplesPerStep);

        for (std::size_t i = 0; i < agents; ++i) {
            const double scale = Probe::scale(engine, i);
            ++audit.agentSteps;

            if (!isFiniteD(scale)) {
                ++audit.scaleNonFinite;
            } else if (scale < 0.0 || scale > 1.0) {
                ++audit.scaleOutOfRange;
            }

            // want_i exactly as FR-023 defines it: the recorded outgoing total,
            // in the step's own time units.
            const double want = Probe::outflow(engine, i) * dt;
            if (want == 0.0) {
                ++audit.zeroWantSteps;
                if (scale != 1.0) {
                    ++audit.zeroWantScaleNotOne;
                }
                if (Probe::divided(engine, i)) {
                    ++audit.zeroWantDivided;
                }
            }
        }

        const std::size_t pairs = Probe::pairCount(engine);
        for (std::size_t p = 0; p < pairs; ++p) {
            const double flow = Probe::pairFlow(engine, p);
            const double si = Probe::scale(engine, Probe::pairI(engine, p));
            const double sj = Probe::scale(engine, Probe::pairJ(engine, p));
            const double applied = flow * ((si < sj) ? si : sj);
            ++audit.pairApplications;
            if (applied != 0.0 && ((applied > 0.0) != (flow > 0.0))) {
                ++audit.signReversals;
            }
        }
    }
    return audit;
}

}  // namespace

// ==============================================================================
// T008 (1) - SC-020: the FR-023 exchange scale is WELL FORMED
// ==============================================================================
// A PROBE-LEVEL criterion, and the reason is structural rather than convenient:
// a sign-reversed pair flow is still ANTISYMMETRIC, so the pairwise sum is still
// exactly zero, so SC-001's conservation and boundedness hold to the last bit
// while the exchange rule runs backwards. No black-box criterion can see it.
//
// Three arms, ten thousand steps each, chosen so that BOTH guarded branches of
// FR-023's ternary are the common case somewhere:
//   (a) the Appendix-A defaults - pairs exist, so the applied-sign clause has
//       something to audit;
//   (b) agentCount = 1 - NO PAIR EXISTS AT ALL, so want_i == 0 on every single
//       agent-step and the `want > 0` guard is the only thing standing between
//       the implementation and 0/0;
//   (c) preyFloorShares = 1.6 (most agents sit BELOW the refuge floor, so
//       `spare < 0` is the common case) with kernelSigma = 0.01 (most agents
//       have no surviving pair, so `want == 0` is also the common case).
//
// FALSIFICATION (run it, then restore): replace the stage-3 ternary with
// `scale_[i] = std::min(1.0, spare / want)`. Arm (b) fails on scaleNonFinite
// (0/0), arm (c) fails on scaleOutOfRange and signReversals (a negative scale),
// and every SC-001 conservation clause stays green throughout - which is the
// whole reason this criterion exists at probe level.
TEST_CASE("EcosystemEngine_ExchangeScaleIsWellFormed", "[ecosystem_engine]") {
    constexpr std::size_t kSteps = 10000u;

    // ---- arm (a): the Appendix-A defaults -----------------------------------
    {
        auto engine = std::make_unique<Engine>();
        engine->setSeed(0x0EC05EEDu);
        engine->prepare(48000.0, defaultConfig());

        const ExchangeScaleAudit audit = auditExchangeScale(*engine, kSteps);
        INFO("arm (a) defaults: agent-steps " << audit.agentSteps << ", pair applications "
                                              << audit.pairApplications << ", zero-want steps "
                                              << audit.zeroWantSteps << ", divisions "
                                              << Probe::divisions(*engine));
        REQUIRE(audit.scaleNonFinite == 0u);
        REQUIRE(audit.scaleOutOfRange == 0u);
        REQUIRE(audit.zeroWantScaleNotOne == 0u);
        REQUIRE(audit.zeroWantDivided == 0u);
        REQUIRE(audit.signReversals == 0u);
        // Non-vacuity: the applied-sign clause must have had pairs to audit.
        REQUIRE(audit.pairApplications > 0u);
    }

    // ---- arm (b): agentCount = 1, so no pair exists at all -------------------
    {
        auto engine = std::make_unique<Engine>();
        engine->setSeed(0x0EC05EEDu);
        engine->prepare(48000.0, Engine::PrepareConfig{.agentCount = 1u,
                                                       .resourceCells = 64u,
                                                       .energyBudget = 1.0,
                                                       .initialPoolFraction = 0.5,
                                                       .stepIntervalChunks = 8u});
        REQUIRE(engine->getAgentCount() == 1u);

        const ExchangeScaleAudit audit = auditExchangeScale(*engine, kSteps);
        INFO("arm (b) single agent: agent-steps " << audit.agentSteps << ", zero-want steps "
                                                  << audit.zeroWantSteps);
        REQUIRE(audit.scaleNonFinite == 0u);
        REQUIRE(audit.scaleOutOfRange == 0u);
        REQUIRE(audit.zeroWantScaleNotOne == 0u);
        REQUIRE(audit.zeroWantDivided == 0u);
        REQUIRE(audit.signReversals == 0u);
        // Structural, not statistical: with one agent there is no pair, so every
        // agent-step MUST be a want == 0 agent-step and no pair is ever applied.
        REQUIRE(audit.agentSteps == kSteps);
        REQUIRE(audit.zeroWantSteps == kSteps);
        REQUIRE(audit.pairApplications == 0u);
        REQUIRE(Probe::divisions(*engine) == 0u);
    }

    // ---- arm (c): below the refuge floor, and mostly without neighbours ------
    {
        auto engine = std::make_unique<Engine>();
        engine->setSeed(0x0EC05EEDu);
        engine->prepare(48000.0, defaultConfig());
        engine->setPreyFloorShares(1.6f);  // most agents sit BELOW the floor
        engine->setKernelSigma(0.01f);     // most agents have NO surviving pair
        REQUIRE(engine->getPreyFloorShares() == 1.6f);
        REQUIRE(engine->getKernelSigma() == 0.01f);

        const ExchangeScaleAudit audit = auditExchangeScale(*engine, kSteps);
        INFO("arm (c) hostile floor: agent-steps " << audit.agentSteps << ", pair applications "
                                                   << audit.pairApplications << ", zero-want steps "
                                                   << audit.zeroWantSteps);
        REQUIRE(audit.scaleNonFinite == 0u);
        REQUIRE(audit.scaleOutOfRange == 0u);
        REQUIRE(audit.zeroWantScaleNotOne == 0u);
        REQUIRE(audit.zeroWantDivided == 0u);
        REQUIRE(audit.signReversals == 0u);
        // Non-vacuity: this arm exists for the want == 0 clause, so it must have
        // produced want == 0 agent-steps.
        REQUIRE(audit.zeroWantSteps > 0u);
    }
}

// ==============================================================================
// T008 (2) - FR-021: predation == 0.5 disables the exchange rule EXACTLY
// ==============================================================================
// FR-021's SECOND defence, made observable rather than only documented (the
// first is SC-012's coverage assertion, the third is the header note that sits
// on the flow line itself). The trap is not hypothetical: an ablation run in the
// prototype reported "no exchange" as BIT-IDENTICAL TO BASELINE, TO EVERY
// PRINTED DIGIT, because the default at the time WAS 0.5 and the rule had been
// silently off in the very configuration being validated (FINDINGS.md:119-122).
//
// The load-bearing half of this case is `pairCount() > 0` on every step: without
// it, an implementation whose neighbour cutoff had gone wrong and produced an
// EMPTY pair list would pass the "every flow is zero" clause for entirely the
// wrong reason. Pairs survive the w < 1e-6 cutoff; the zero is the
// `(1 - 2*predation)` FACTOR.
//
// The control arm at predation = 0.55 (the Appendix-A default) is what makes the
// case DEMONSTRATED to discriminate rather than assumed to.
TEST_CASE("EcosystemEngine_ExchangeDisabledAtPredationHalf", "[ecosystem_engine]") {
    constexpr std::size_t kSteps = 2000u;
    constexpr std::size_t kSamplesPerStep = 8u * 64u;  // defaultConfig() = one step per call

    auto engine = std::make_unique<Engine>();
    engine->setSeed(0x5A1FED0Cu);
    engine->prepare(48000.0, defaultConfig());
    engine->setPredation(0.5f);
    REQUIRE(engine->getPredation() == 0.5f);

    std::size_t stepsWithNoPair = 0;
    std::size_t nonZeroFlows = 0;
    std::size_t nonFiniteReads = 0;

    for (std::size_t s = 0; s < kSteps; ++s) {
        engine->processChunk(kSamplesPerStep);

        const std::size_t pairs = Probe::pairCount(*engine);
        if (pairs == 0u) {
            ++stepsWithNoPair;
        }
        for (std::size_t p = 0; p < pairs; ++p) {
            // EXACTLY zero. -0.0 == 0.0 in IEEE, which is the right reading
            // here: the sign of a zero flow moves no energy either way.
            if (Probe::pairFlow(*engine, p) != 0.0) {
                ++nonZeroFlows;
            }
        }
        for (std::size_t i = 0; i < engine->getAgentCount(); ++i) {
            if (!isFiniteD(engine->getAgentEnergy(i)) ||
                !isFiniteD(engine->getAgentPositionX(i)) ||
                !isFiniteD(engine->getAgentPositionY(i)) ||
                !isFiniteD(engine->getAgentPhase(i))) {
                ++nonFiniteReads;
            }
        }
    }

    INFO("steps with no pair " << stepsWithNoPair << ", non-zero flows " << nonZeroFlows
                               << ", non-finite reads " << nonFiniteReads);
    // The zero is the FACTOR, not an empty pair list.
    REQUIRE(stepsWithNoPair == 0u);
    REQUIRE(nonZeroFlows == 0u);

    // The Edge Case's "must be bounded" half: the run stays finite and conserving.
    REQUIRE(nonFiniteReads == 0u);
    REQUIRE(isFiniteD(engine->getPoolEnergy()));
    REQUIRE(isFiniteD(engine->getTotalEnergy()));
    REQUIRE(engine->getConservationViolationCount() == 0u);
    REQUIRE(engine->getNonFiniteContainmentCount() == 0u);

    // ---- CONTROL ARM: the default predation, same seed and step count --------
    auto control = std::make_unique<Engine>();
    control->setSeed(0x5A1FED0Cu);
    control->prepare(48000.0, defaultConfig());
    REQUIRE(control->getPredation() == 0.55f);

    std::size_t controlNonZeroFlows = 0;
    for (std::size_t s = 0; s < kSteps; ++s) {
        control->processChunk(kSamplesPerStep);
        const std::size_t pairs = Probe::pairCount(*control);
        for (std::size_t p = 0; p < pairs; ++p) {
            if (Probe::pairFlow(*control, p) != 0.0) {
                ++controlNonZeroFlows;
            }
        }
    }
    INFO("control arm non-zero flows " << controlNonZeroFlows);
    REQUIRE(controlNonZeroFlows > 0u);
}

namespace {

/// @brief The control-step size for defaultConfig(): 8 chunks of 64 samples, so
///        exactly ONE simulation step per processChunk() call.
constexpr std::size_t kDefaultSamplesPerStep = 8u * 64u;

/// @brief One arm of SC-009 (d), run at a chosen `feedRate`.
///
/// TWO ENGINES, IDENTICALLY SEEDED AND IDENTICALLY DRIVEN, are what bounds "the
/// legal per-agent movement" without hard-coding a number that would go stale
/// the moment a knob default moved: after the settle they hold bit-identical
/// state, so any difference in the single step that follows is attributable to
/// the ONE thing that differs between them - the injected pool.
///
///   arm A: pool_ = -0.25 * energyBudget   (FR-056 permits a negative pool and
///                                          never clamps it)
///   arm B: pool_ =  0.0                   (the same step with nothing to spend)
///
/// Under FR-054's floor, `avail` is ZERO in both arms, so the two steps must
/// agree to the last bit. Under the floor-inside-the-branch mistake, arm A walks
/// a NEGATIVE `avail` into stage 7, where `if (globalFeed > avail)` stops being a
/// min() and becomes an assignment, and agent 0 is charged the whole deficit.
void runNegativePoolArm(float feedRate) {
    constexpr std::size_t kSettleSteps = 200u;

    auto armA = std::make_unique<Engine>();
    auto armB = std::make_unique<Engine>();
    armA->setSeed(0xC0FFEEu);
    armB->setSeed(0xC0FFEEu);
    armA->prepare(48000.0, defaultConfig());
    armB->prepare(48000.0, defaultConfig());
    armA->setFeedRate(feedRate);
    armB->setFeedRate(feedRate);
    REQUIRE(armA->getFeedRate() == feedRate);
    REQUIRE(armB->getFeedRate() == feedRate);

    for (std::size_t s = 0; s < kSettleSteps; ++s) {
        armA->processChunk(kDefaultSamplesPerStep);
        armB->processChunk(kDefaultSamplesPerStep);
    }

    const std::size_t agents = armA->getAgentCount();

    // The premise the whole comparison rests on, asserted rather than assumed.
    std::size_t settleMismatches = 0;
    for (std::size_t i = 0; i < agents; ++i) {
        if (armA->getAgentEnergy(i) != armB->getAgentEnergy(i)) {
            ++settleMismatches;
        }
    }
    INFO("feedRate " << feedRate << ": settle mismatches " << settleMismatches);
    REQUIRE(settleMismatches == 0u);

    std::array<double, Engine::kMaxAgents> before{};
    for (std::size_t i = 0; i < agents; ++i) {
        before[i] = armA->getAgentEnergy(i);
    }

    const double budget = armA->getEnergyBudget();
    const double deficit = -0.25 * budget;
    const double meanShare = budget / static_cast<double>(agents);

    Probe::setPool(*armA, deficit);
    Probe::setPool(*armB, 0.0);
    REQUIRE(armA->getPoolEnergy() == deficit);
    REQUIRE(armB->getPoolEnergy() == 0.0);

    armA->processChunk(kDefaultSamplesPerStep);
    armB->processChunk(kDefaultSamplesPerStep);

    std::size_t deltaMismatches = 0;
    double worstDifference = 0.0;
    for (std::size_t i = 0; i < agents; ++i) {
        const double deltaA = armA->getAgentEnergy(i) - before[i];
        const double deltaB = armB->getAgentEnergy(i) - before[i];
        const double magnitude = std::fabs(deltaA);
        const double other = std::fabs(deltaB);
        // The normaliser is the agent's own movement where it has any, and the
        // mean share otherwise, so a near-zero delta is not compared against a
        // near-zero tolerance.
        double norm = (magnitude > other) ? magnitude : other;
        if (norm < meanShare) {
            norm = meanShare;
        }
        const double difference = std::fabs(deltaA - deltaB);
        if (difference > worstDifference) {
            worstDifference = difference;
        }
        if (difference > 1.0e-12 * norm) {
            ++deltaMismatches;
        }
    }

    INFO("feedRate " << feedRate << ": worst per-agent delta difference " << worstDifference
                     << ", mismatching agents " << deltaMismatches);
    REQUIRE(deltaMismatches == 0u);

    // The "in particular" clause, written RELATIVE to agent 0's own energy: the
    // failure mode zeroes agent 0 outright (it is charged `influx = graze_0 +
    // avail`, then stage 8's zero clamp returns the overdraw), so a 5 % band
    // around one step's legitimate movement - which is order 0.1 % here - is the
    // discriminating shape, and it does not depend on how much energy agent 0
    // happens to be holding.
    REQUIRE(before[0] > 0.0);  // non-vacuity: the clause needs something to lose
    const double agentZeroDelta = armA->getAgentEnergy(0) - before[0];
    INFO("feedRate " << feedRate << ": agent 0 held " << before[0] << " and moved by "
                     << agentZeroDelta << " against a deficit of " << deficit);
    REQUIRE(std::fabs(agentZeroDelta) <= 0.05 * before[0]);

    // FR-056: the pool is a LATCHED signal. The deficit is still there, and it
    // was not healed by destroying an agent's state.
    REQUIRE(isFiniteD(armA->getPoolEnergy()));
    REQUIRE(armA->getPoolEnergy() < 0.0);
}

}  // namespace

// ==============================================================================
// T009 (1) - SC-009 (d): a NEGATIVE pool is not absorbed by an agent
// ==============================================================================
// The blocker regression for plan S4.5's hoisted floor, and a one-step case.
//
// FR-054's running balance has TWO withdrawal sites - stage 5's regrowth and
// stage 7's global feed - so the floor has to be structural
// (`avail = (pool_ > 0.0) ? pool_ : 0.0` BEFORE `if (avail > 0)`) rather than a
// clamp inside the branch. With the clamp inside, `pool_ < 0` skips the branch
// entirely and a NEGATIVE `avail` reaches stage 7. What happens there is not a
// rounding artefact and it fires AT THE DEFAULT `feedRate = 0`: the product
// `0.0 * negative` is `-0.0`, `-0.0 > avail` is true, so the line intended as
// `min(globalFeed, avail)` instead ASSIGNS the whole deficit to the first agent
// in the loop. `avail -= globalFeed` then zeroes the balance so no other agent is
// touched, and stage 8's zero clamp returns the overdraw to the pool. Net effect:
// agent 0's energy is wiped, the pool is quietly healed toward zero, and NO
// COUNTER RECORDS ANY OF IT - FR-056's "a negative pool is a readable, latched
// signal" undone in silence.
//
// The second arm at `feedRate = 0.5` exists so the case is not accidentally
// specific to the `-0.0 > avail` path: there the product is a genuine small
// negative and the same assignment fires for a different arithmetic reason.
TEST_CASE("EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent", "[ecosystem_engine]") {
    runNegativePoolArm(0.0f);  // the Appendix-A default: the -0.0 path
    runNegativePoolArm(0.5f);  // a genuine negative product, same assignment
}

// ==============================================================================
// T009 (2) - SC-021: the FR-043 denormal cell snap RETURNS ITS ENERGY
// ==============================================================================
// SC-001 (c) structurally cannot see this: its gate is 1e-9 RELATIVE on a budget
// of order 1, which sits fourteen decades above a quantity of 1e-30 or less. An
// implementation that zeroes the cell and drops the energy on the floor passes
// every other criterion in this spec.
//
// TWO CONFIGURATION CHOICES THAT ARE PART OF THE MEASUREMENT, NOT CONVENIENCE:
//
//  1. `leakRate`, `exchangeRate`, `feedRate` and `regenRate` are all ZERO, so the
//     snap is the ONLY writer of `poolDelta` in the step. Stage 8's clamps cannot
//     fire either (every `dE` is a non-negative grazing influx and `capacityAbs`
//     is the whole budget), so the step's pool movement IS the snapped amount and
//     an exact comparison means something.
//  2. The pool is driven to 0.0 through the probe BEFORE EVERY STEP. Without
//     that, `pool + 1e-31 == pool` in double for any pool this component can
//     hold, so the transfer would be absorbed by the very rounding that makes
//     SC-001 blind to it and the case would pass whether or not the energy was
//     returned. Zeroing the pool is what gives the criterion teeth. It costs
//     nothing else: at `regenRate == 0` and `feedRate == 0` nothing else reads
//     the pool during a step.
//
// `kernelSigma` is widened to 0.12 so every agent carries demand on every cell in
// the strip (FR-040's x-only separation over a unit torus caps |d| at 0.5, which
// is inside the cutoff at that sigma). That makes the field's decay geometric and
// the sub-guard crossing DETERMINISTIC rather than a wait for agents to wander
// over a particular cell.
TEST_CASE("EcosystemEngine_DenormalCellSnapConserves", "[ecosystem_engine]") {
    constexpr std::size_t kMaxSteps = 20000u;

    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());
    engine->setCellCapacityShares(0.32f);  // Appendix-A MINIMUM
    engine->setGrazeRate(3.0f);            // Appendix-A MAXIMUM
    engine->setRegenRate(0.0f);            // nothing refills a cell
    engine->setLeakRate(0.0f);             // isolate the pool's only writer
    engine->setExchangeRate(0.0f);         // ... and keep stage 8's clamps quiet
    engine->setFeedRate(0.0f);
    engine->setKernelSigma(0.12f);
    REQUIRE(engine->getCellCapacityShares() == 0.32f);
    REQUIRE(engine->getGrazeRate() == 3.0f);
    REQUIRE(engine->getRegenRate() == 0.0f);
    REQUIRE(engine->getLeakRate() == 0.0f);

    const std::size_t cells = engine->getResourceCells();

    std::size_t snapStep = kMaxSteps;
    double snapped = 0.0;
    std::size_t poolMismatches = 0;

    for (std::size_t s = 0; s < kMaxSteps; ++s) {
        Probe::setPool(*engine, 0.0);
        engine->processChunk(kDefaultSamplesPerStep);

        const double snap = Probe::lastSnap(*engine);
        // EXACT, every step, snap or no snap: the pool's movement over a step is
        // the snapped amount and nothing else.
        if (engine->getPoolEnergy() != snap) {
            ++poolMismatches;
        }
        if (snap != 0.0) {
            snapStep = s;
            snapped = snap;
            break;
        }
    }

    INFO("snap step " << snapStep << " of " << kMaxSteps << ", snapped " << snapped
                      << ", pool mismatches " << poolMismatches);
    REQUIRE(poolMismatches == 0u);
    // Non-vacuity: the case is worthless if no cell ever went sub-guard.
    REQUIRE(snapStep < kMaxSteps);
    REQUIRE(snapped != 0.0);
    REQUIRE(isFiniteD(snapped));
    REQUIRE(engine->getPoolEnergy() == snapped);

    // Every cell is either EXACTLY zero or comfortably above the subnormal
    // range. A bit-inspection stand-in for std::fpclassify, which -ffast-math
    // may fold: 1e-300 is four decades above the double subnormal ceiling
    // (~2.2e-308) and 270 decades below the 1e-30 guard, so nothing legitimate
    // can sit between them.
    std::size_t subnormalCells = 0;
    std::size_t snappedCells = 0;
    for (std::size_t k = 0; k < cells; ++k) {
        const double c = engine->getCellEnergy(k);
        REQUIRE(isFiniteD(c));
        if (c == 0.0) {
            ++snappedCells;
            continue;
        }
        if (std::fabs(c) < 1.0e-300) {
            ++subnormalCells;
        }
    }
    INFO("cells exactly zero " << snappedCells << ", cells holding a subnormal " << subnormalCells);
    REQUIRE(subnormalCells == 0u);
    // At least one cell was snapped to EXACTLY 0.0 - not 1e-234, not a subnormal.
    REQUIRE(snappedCells > 0u);
}

// ==============================================================================
// T010 - SC-001 (a)(b)(c) / FR-012 / FR-014 / FR-055 / FR-056 / FR-083:
//        the economy CONSERVES, and stays inside its rails, over a short run
// ==============================================================================
// The first case in this TU that runs the WHOLE thirteen-stage step at length,
// and therefore the first one that can see a stage-9/10/11 mistake at all: a
// movement step that escapes the torus, a phase that walks off [0, 1), or a pool
// update that quietly heals a deficit are each invisible to a one-step case.
//
// 50 000 control steps is ~8.9 minutes of simulated time at the FR-082 default
// dt (8 chunks * 64 samples / 48 kHz = 10.67 ms), which is long enough for the
// slow terms - the OU drift's 0.01 mean-reversion coefficient above all - to
// matter, and short enough to stay a per-push test rather than a soak.
//
// SAMPLING EVERY 93rd STEP (~1 Hz at that dt) is not a cost dodge: the
// assertions below are on STATE, and state is only observable between steps, so
// a denser sample would re-read the same invariant against the same arithmetic
// without covering anything new. What is NOT sampled is the two counters -
// those are cumulative and are read once at the end, so a violation on an
// unsampled step is still caught.
//
// THE TWO COUNTERS ARE ASSERTED SEPARATELY (FR-056 / FR-083). One counter
// carrying two meanings cannot say WHICH failure mode fired, and the two have
// completely different causes: a negative end-of-step pool is an economy bug, a
// non-finite containment is a numerics bug.
//
// THE CONSERVATION BOUND is SC-001 (c)'s 1e-9 RELATIVE gate. The prototype
// measured 2.4e-15 to 1.9e-13 over the same horizon, so this is roughly four
// decades of margin - the bound is not tuned to what the implementation happens
// to achieve, and a real leak (a clamp that forgets to return its delta, a
// grazed amount debited twice) is orders of magnitude larger than either figure.
TEST_CASE("EcosystemEngine_ConservesOverAShortRun", "[ecosystem_engine]") {
    // Three seeds, because every rule in the step is seeded - positions, kinds,
    // energies, phases, frequencies and the persistent drift stream - and a
    // conservation defect that only fires for one initial layout is still a
    // conservation defect.
    constexpr std::array<std::uint32_t, 3> kSeeds{0xC0FFEEu, 0xC0FFEFu, 0x5EEDu};
    constexpr std::size_t kSteps = 50000u;
    constexpr std::size_t kSampleEvery = 93u;  // ~1 Hz at the default dt
    constexpr double kRelativeBound = 1.0e-9;  // SC-001 (c)

    for (const std::uint32_t seed : kSeeds) {
        // ~23.5 KB: make_unique, never a plain stack local (plan S9).
        auto engine = std::make_unique<Engine>();
        engine->setSeed(seed);
        engine->prepare(48000.0, defaultConfig());
        REQUIRE(engine->isPrepared());

        const std::size_t agents = engine->getAgentCount();
        const std::size_t cells = engine->getResourceCells();
        const double budget = engine->getEnergyBudget();
        REQUIRE(agents > 0u);
        REQUIRE(cells > 0u);
        REQUIRE(budget > 0.0);

        // capacityAbs_ has no public getter and must not acquire one for a test
        // (plan S7.4). It is re-derived here from the two knobs that define it,
        // in the SAME expression order refreshDerivedScales() uses, so the
        // comparison against stage 8's upper clamp is exact rather than
        // approximately exact.
        const double meanShare = budget / static_cast<double>(agents);
        const double capacityAbs = static_cast<double>(engine->getCapacityShares()) * meanShare;

        // Counted, not REQUIREd in place: this arm inspects ~540 samples of 32
        // agents and 64 cells, and a REQUIRE per item would spend the runtime
        // inside Catch2's expression machinery (which reaches the heap) while
        // drowning the arm's diagnostics on the first failure.
        std::size_t samples = 0;
        std::size_t conservationBreaches = 0;
        std::size_t nonFiniteReads = 0;
        std::size_t positionOutOfRange = 0;
        std::size_t phaseOutOfRange = 0;
        std::size_t energyBelowZero = 0;
        std::size_t energyAboveCapacity = 0;
        double worstRelative = 0.0;

        for (std::size_t s = 0; s < kSteps; ++s) {
            engine->processChunk(kDefaultSamplesPerStep);

            if ((s % kSampleEvery) != 0u) {
                continue;
            }
            ++samples;

            // ---- SC-001 (c): agents + cells + pool == the budget ------------
            const double total = engine->getTotalEnergy();
            if (!isFiniteD(total)) {
                ++nonFiniteReads;
                ++conservationBreaches;
            } else {
                const double rel = std::fabs(total - budget) / budget;
                if (rel > worstRelative) {
                    worstRelative = rel;
                }
                if (rel > kRelativeBound) {
                    ++conservationBreaches;
                }
            }

            if (!isFiniteD(engine->getPoolEnergy())) {
                ++nonFiniteReads;
            }

            // ---- FR-012 / FR-014 / FR-055: every agent inside its rails -----
            for (std::size_t i = 0; i < agents; ++i) {
                const double e = engine->getAgentEnergy(i);
                const double px = engine->getAgentPositionX(i);
                const double py = engine->getAgentPositionY(i);
                const double ph = engine->getAgentPhase(i);

                if (!isFiniteD(e) || !isFiniteD(px) || !isFiniteD(py) || !isFiniteD(ph)) {
                    ++nonFiniteReads;
                    continue;  // a non-finite value cannot also be range-tested
                }
                // The torus wrap is HALF-OPEN on both axes: wrap01 maps 1.0 to
                // 0.0, so a position or phase of exactly 1.0 is a wrap that did
                // not happen, not a rounding curiosity.
                if (px < 0.0 || px >= 1.0 || py < 0.0 || py >= 1.0) {
                    ++positionOutOfRange;
                }
                if (ph < 0.0 || ph >= 1.0) {
                    ++phaseOutOfRange;
                }
                if (e < 0.0) {
                    ++energyBelowZero;
                }
                if (e > capacityAbs) {
                    ++energyAboveCapacity;
                }
            }

            for (std::size_t k = 0; k < cells; ++k) {
                if (!isFiniteD(engine->getCellEnergy(k))) {
                    ++nonFiniteReads;
                }
            }
        }

        INFO("seed " << seed << ", samples " << samples << ", worst relative error "
                     << worstRelative << ", breaches " << conservationBreaches
                     << ", non-finite reads " << nonFiniteReads << ", positions out of range "
                     << positionOutOfRange << ", phases out of range " << phaseOutOfRange
                     << ", energies below zero " << energyBelowZero << ", energies above capacity "
                     << energyAboveCapacity << ", violations "
                     << engine->getConservationViolationCount() << ", containments "
                     << engine->getNonFiniteContainmentCount());

        // Non-vacuity first: an arm that sampled nothing proves nothing.
        REQUIRE(samples > 0u);
        REQUIRE(engine->getControlStepCount() == static_cast<std::uint64_t>(kSteps));

        REQUIRE(nonFiniteReads == 0u);
        REQUIRE(conservationBreaches == 0u);
        REQUIRE(worstRelative <= kRelativeBound);
        REQUIRE(positionOutOfRange == 0u);
        REQUIRE(phaseOutOfRange == 0u);
        REQUIRE(energyBelowZero == 0u);
        REQUIRE(energyAboveCapacity == 0u);

        // SC-001 (b), and the two counters ASSERTED SEPARATELY (FR-056/FR-083).
        REQUIRE(engine->getConservationViolationCount() == 0u);
        REQUIRE(engine->getNonFiniteContainmentCount() == 0u);
    }
}

// ==============================================================================
// T011 helpers - the wake ramp's own instrument
// ==============================================================================
namespace {

/// @brief The Appendix-A defaults with ONE knob moved: the control-step size.
///
/// SC-014 (b) is asserted at stepIntervalChunks 8, 16 and 64 because rampSteps_
/// is a FUNCTION of that knob (5, 3 and 1 steps at 48 kHz) and the third arm is
/// the degenerate one where the ramp collapses to a single jump. 8 is FR-082's
/// floor AND its default since the 2026-09-16 rulings (kMinStepIntervalChunks).
[[nodiscard]] Engine::PrepareConfig configWithStepChunks(std::size_t chunks) noexcept {
    Engine::PrepareConfig cfg = defaultConfig();
    cfg.stepIntervalChunks = chunks;
    return cfg;
}

/// @brief FR-070's `rampSteps = max(1, ceil(0.050 / dt))`, recomputed in the
///        test from the PUBLISHED step duration rather than from a literal.
///
/// Deriving it from getStepDurationSeconds() keeps the assertion honest if the
/// clock ever moves; the arms below still pin the expected integer (38 / 5 / 1)
/// against this, so a silent change in EITHER the clock or the ramp rule fails
/// here rather than quietly re-scaling the criterion.
[[nodiscard]] std::size_t expectedRampSteps(double stepSeconds) noexcept {
    const double steps = std::ceil(0.050 / stepSeconds);
    const std::size_t rounded = static_cast<std::size_t>(steps);
    return (rounded < 1u) ? 1u : rounded;
}

/// @brief One SC-014 (b) arm at a chosen `stepIntervalChunks`.
///
/// The gate is read through the inspect probe, NOT inferred from
/// getAgentOutput(): the published value is `raw * gate` and `raw` moves every
/// step with the agent energy, so a ratio-based reading would measure the
/// economy as much as the ramp - and would collapse outright if `raw` ever
/// reached the FR-061 upper rail. The published surface is asserted at the one
/// point where it is unambiguous (a zero gate publishes exactly 0.0f), which is
/// also the half of the contract that the gate-INSIDE-the-normalisation form is
/// what makes true.
void runWakeRampArm(std::size_t stepChunks, std::size_t pinnedRampSteps) {
    constexpr std::size_t kAgent = 3u;

    auto engine = std::make_unique<Engine>();
    engine->prepare(48000.0, configWithStepChunks(stepChunks));
    REQUIRE(engine->getStepIntervalChunks() == stepChunks);

    const std::size_t samplesPerStep = stepChunks * 64u;
    const double stepSeconds = engine->getStepDurationSeconds();
    REQUIRE(stepSeconds > 0.0);

    const std::size_t rampSteps = expectedRampSteps(stepSeconds);
    INFO("stepIntervalChunks " << stepChunks << ", dt " << stepSeconds << " s, rampSteps "
                               << rampSteps);
    REQUIRE(rampSteps == pinnedRampSteps);
    const float stepGain = 1.0f / static_cast<float>(rampSteps);

    // ---- the gate comes up SNAPPED to 1: agents are born awake ------------
    REQUIRE(Probe::gate(*engine, kAgent) == 1.0f);

    // ---- the setter moves the TARGET, never the gate (FR-062) -------------
    engine->setAgentWake(kAgent, 0.0f);
    REQUIRE(engine->getAgentWake(kAgent) == 0.0f);
    REQUIRE(Probe::gate(*engine, kAgent) == 1.0f);  // no publication has happened yet

    // One extra step past rampSteps so the arithmetic residue of rampSteps float
    // subtractions is certainly consumed by the max() snap.
    for (std::size_t s = 0; s <= rampSteps; ++s) {
        engine->processChunk(samplesPerStep);
    }
    REQUIRE(Probe::gate(*engine, kAgent) == 0.0f);
    // THE GATE IS INSIDE THE NORMALISATION. `raw` itself is non-zero here - the
    // agent is still holding energy, because dormancy gates the OUTPUT and not
    // the simulation (FR-072) - so the exactly-zero publication is the gate
    // multiplying INSIDE the clamp, not an agent that stopped living.
    REQUIRE(engine->getAgentEnergy(kAgent) > 0.0);
    REQUIRE(engine->getAgentOutput(kAgent) == 0.0f);

    // ---- the 0 -> 1 ramp, measured on the control-step grid ---------------
    engine->setAgentWake(kAgent, 1.0f);
    REQUIRE(engine->getAgentWake(kAgent) == 1.0f);
    REQUIRE(Probe::gate(*engine, kAgent) == 0.0f);  // still untouched by the setter

    float previous = 0.0f;
    float worstIncrement = 0.0f;
    float firstIncrement = 0.0f;
    std::size_t stepsToTarget = 0u;
    std::size_t decreasingSteps = 0u;
    std::size_t oversizedSteps = 0u;

    const std::size_t window = rampSteps + 2u;
    for (std::size_t s = 1u; s <= window; ++s) {
        engine->processChunk(samplesPerStep);
        const float g = Probe::gate(*engine, kAgent);
        const float increment = g - previous;
        if (s == 1u) {
            firstIncrement = increment;
        }
        if (increment < 0.0f) {
            ++decreasingSteps;
        }
        if (increment > worstIncrement) {
            worstIncrement = increment;
        }
        // The per-step ceiling is `1 / rampSteps` of full scale; the epsilon is
        // float accumulation slack, not a relaxation of the rule.
        if (increment > stepGain + 1.0e-6f) {
            ++oversizedSteps;
        }
        if (stepsToTarget == 0u && g >= 1.0f) {
            stepsToTarget = s;
        }
        previous = g;
    }

    INFO("steps to target " << stepsToTarget << " (expected " << rampSteps
                            << " +/- 1), worst increment " << worstIncrement << ", stepGain "
                            << stepGain << ", decreasing steps " << decreasingSteps
                            << ", oversized steps " << oversizedSteps);

    REQUIRE(decreasingSteps == 0u);  // monotone non-decreasing
    REQUIRE(oversizedSteps == 0u);   // no step larger than 1 / rampSteps
    REQUIRE(stepsToTarget != 0u);    // it DID reach the target inside the window
    REQUIRE(stepsToTarget + 1u >= rampSteps);
    REQUIRE(stepsToTarget <= rampSteps + 1u);
    REQUIRE(Probe::gate(*engine, kAgent) == 1.0f);

    if (rampSteps == 1u) {
        // THE DEGENERATE ARM, ASSERTED EXPLICITLY. At stepIntervalChunks = 64
        // one control step is 85.3 ms at 48 kHz - already longer than the 50 ms
        // fade - so there is no ramp to observe and the contract is a SINGLE
        // full-scale jump. Asserting "monotone over one sample" would be vacuous.
        REQUIRE(stepsToTarget == 1u);
        REQUIRE(firstIncrement == 1.0f);
    } else {
        // Non-vacuity for the ramping arms: the first step must NOT jump the
        // whole way, or "monotone with bounded increments" is trivially true.
        REQUIRE(firstIncrement < 1.0f);
    }
}

}  // namespace

// ==============================================================================
// T011 - SC-014 (a)(b)(c): dormancy, the wake ramp, and the two silence snaps
// ==============================================================================
// (a) is the house rule identity claim: setAgentWake(i, 0) and
// setAgentDormant(i, true) fold into ONE steady gate target, so two identically
// seeded instances driven the two different ways must publish bit-identical
// trajectories. Dormancy gates the OUTPUT and nothing else (FR-072), which is
// also why the two instances agree on ENERGY: the dormant agent is still
// grazing, exchanging and leaking.
//
// (b) states the ramp in CONTROL STEPS, the only unit this component can
// produce - a "50 ms +/- 1 ms" form is forbidden by name (FR-070, SC-014 (b)):
// FR-062 recomputes the published value only at a control step, so at the
// default the measured duration is quantised to {42.7, 53.3} ms and can never
// land inside 50 +/- 1 ms.
//
// (c) proves the TWO epsilon snaps are both present and are NOT redundant: the
// setter snap makes setAgentWake(i, 1e-8) exactly dormant, and the publication
// snap makes a published value at or below 1e-6 exactly 0.0f even when the
// setter stored the wake unchanged.
TEST_CASE("EcosystemEngine_DormancyMatchesHouseRule", "[ecosystem_engine]") {
    // -------------------------------------------------------------------------
    // (a) wake 0 and dormant true are the SAME published trajectory
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kSteps = 2000u;

        auto viaWake = std::make_unique<Engine>();
        auto viaDormant = std::make_unique<Engine>();
        viaWake->setSeed(0xC0FFEEu);
        viaDormant->setSeed(0xC0FFEEu);
        viaWake->prepare(48000.0, defaultConfig());
        viaDormant->prepare(48000.0, defaultConfig());

        const std::size_t agents = viaWake->getAgentCount();
        REQUIRE(agents == 32u);
        REQUIRE(viaDormant->getAgentCount() == agents);

        // Every EVEN agent is silenced, each instance by its own route; the odd
        // agents are the live control group the non-vacuity clauses read.
        for (std::size_t i = 0; i < agents; i += 2u) {
            viaWake->setAgentWake(i, 0.0f);
            viaDormant->setAgentDormant(i, true);
        }
        REQUIRE(viaWake->getAgentWake(0) == 0.0f);
        REQUIRE(!viaWake->isAgentDormant(0));
        REQUIRE(viaDormant->isAgentDormant(0));
        REQUIRE(viaDormant->getAgentWake(0) == 1.0f);  // the two routes really differ

        std::size_t outputMismatches = 0u;
        std::size_t energyMismatches = 0u;
        std::size_t silentAgentBreaches = 0u;
        std::size_t stepsWithALiveAgent = 0u;

        // The gate needs rampSteps publications to reach zero; only after that
        // is "the silenced agents publish exactly 0" a claim about the steady
        // state rather than about the fade.
        const std::size_t settled = expectedRampSteps(viaWake->getStepDurationSeconds()) + 1u;

        for (std::size_t s = 1u; s <= kSteps; ++s) {
            viaWake->processChunk(kDefaultSamplesPerStep);
            viaDormant->processChunk(kDefaultSamplesPerStep);

            bool liveAgentThisStep = false;
            for (std::size_t i = 0; i < agents; ++i) {
                const float a = viaWake->getAgentOutput(i);
                const float b = viaDormant->getAgentOutput(i);
                if (a != b) {
                    ++outputMismatches;
                }
                if (viaWake->getAgentEnergy(i) != viaDormant->getAgentEnergy(i)) {
                    ++energyMismatches;
                }
                if ((i % 2u) == 0u) {
                    if (s >= settled && a != 0.0f) {
                        ++silentAgentBreaches;
                    }
                } else if (a > 0.0f) {
                    liveAgentThisStep = true;
                }
            }
            if (liveAgentThisStep) {
                ++stepsWithALiveAgent;
            }
        }

        INFO("output mismatches " << outputMismatches << ", energy mismatches " << energyMismatches
                                  << ", silent-agent breaches " << silentAgentBreaches
                                  << ", steps with a live agent " << stepsWithALiveAgent << " of "
                                  << kSteps);

        REQUIRE(outputMismatches == 0u);
        // FR-072, the mechanism half: a dormant agent is NOT frozen. Both
        // instances run the same economy, so their energies agree bit for bit.
        REQUIRE(energyMismatches == 0u);
        // Non-vacuity: two all-zero trajectories would also be "identical".
        REQUIRE(stepsWithALiveAgent == kSteps);
        REQUIRE(silentAgentBreaches == 0u);
        REQUIRE(viaWake->getAgentEnergy(0) > 0.0);  // the silenced agent still holds energy
    }

    // -------------------------------------------------------------------------
    // (b) the wake ramp, in control steps, at stepIntervalChunks 8, 16 and 64
    // -------------------------------------------------------------------------
    // 48 kHz: dt = chunks * 64 / 48000, so rampSteps = ceil(0.050 / dt) is
    // ceil(4.6875) = 5, ceil(2.34375) = 3 and max(1, ceil(0.586)) = 1. The
    // first arm sits at kMinStepIntervalChunks, FR-082's floor (and default)
    // since the 2026-09-16 rulings.
    static_assert(Engine::kMinStepIntervalChunks == 8u, "SC-014 (b)'s first arm is the floor");
    runWakeRampArm(8u, 5u);
    runWakeRampArm(16u, 3u);
    runWakeRampArm(64u, 1u);

    // -------------------------------------------------------------------------
    // (c) the two silence snaps, separately
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kAgent = 5u;

        // ---- arm 1: the SETTER snap (bloom_engine.h:650-655) ---------------
        auto setterArm = std::make_unique<Engine>();
        setterArm->prepare(48000.0, defaultConfig());
        setterArm->setAgentWake(kAgent, 1.0e-8f);
        REQUIRE(setterArm->getAgentWake(kAgent) == 0.0f);  // stored as EXACTLY zero

        const std::size_t settleSteps = expectedRampSteps(setterArm->getStepDurationSeconds()) + 1u;
        for (std::size_t s = 0; s < settleSteps; ++s) {
            setterArm->processChunk(kDefaultSamplesPerStep);
        }
        REQUIRE(Probe::gate(*setterArm, kAgent) == 0.0f);
        REQUIRE(setterArm->getAgentOutput(kAgent) == 0.0f);

        // ---- arm 2: the PUBLICATION snap, on a wake the setter KEPT --------
        // 1.1e-6 is above kWakeSilenceEpsilon, so the setter stores it
        // unchanged; the published product `0.5 * e * n / B * gate` is then of
        // order 1e-7, and the FR-063 snap at the source is the ONLY thing that
        // can make the output exactly zero. If the two snaps were one mechanism,
        // one of these two arms could not exist.
        constexpr float kTinyWake = 1.1e-6f;
        static_assert(kTinyWake > Engine::kWakeSilenceEpsilon,
                      "arm 2 must survive the setter snap");

        auto publishArm = std::make_unique<Engine>();
        publishArm->prepare(48000.0, defaultConfig());
        publishArm->setAgentWake(kAgent, kTinyWake);
        REQUIRE(publishArm->getAgentWake(kAgent) == kTinyWake);  // NOT snapped

        for (std::size_t s = 0; s < settleSteps; ++s) {
            publishArm->processChunk(kDefaultSamplesPerStep);
        }
        REQUIRE(Probe::gate(*publishArm, kAgent) == kTinyWake);

        // The premise, asserted rather than assumed: the un-snapped product is
        // strictly positive and at or below the epsilon.
        const double raw = Engine::kOutputAnchor * publishArm->getAgentEnergy(kAgent) *
                           static_cast<double>(publishArm->getAgentCount()) /
                           publishArm->getEnergyBudget() * static_cast<double>(kTinyWake);
        INFO("un-snapped published product " << raw << " against epsilon "
                                             << Engine::kWakeSilenceEpsilon);
        REQUIRE(raw > 0.0);
        REQUIRE(raw <= static_cast<double>(Engine::kWakeSilenceEpsilon));

        REQUIRE(publishArm->getAgentOutput(kAgent) == 0.0f);
    }
}

// ==============================================================================
// T012 helpers - streaming statistics, deliberately FILE-LOCAL and T012-named
// ==============================================================================
// tasks.md T012 clause 2 reaches for `lateWindow()` from T017's shared helper
// header (plan S10.2), which does not exist yet: T017 sits in Group O, four
// groups after this one. The task's own instruction for that ordering is to
// "code the two statistics file-locally and delete them when T017 lands", so
// they live here, in the anonymous namespace, under a `t012` prefix that CANNOT
// collide with `Krate::DSP::TestUtils::Eco::lateWindow` when that header arrives
// (an unqualified anonymous-namespace name and a namespace-scope one of the same
// spelling are an ambiguity at the call site, not a harmless shadow).
//
// Everything is in DOUBLE. statistical_utils.h's helpers are float-only (:41,
// :76) and a float std/mean over 600 samples of a 0.03-magnitude signal loses
// exactly the discrimination SC-014 (d)'s 0.30 threshold needs (plan S14 D-E
// records the same decision for the shared header).
namespace {

/// @brief Welford's online mean/variance - NOT the sum-of-squares form.
///
/// The naive `sumSq/n - mean^2` on agent energies of ~0.03 subtracts two numbers
/// of order 9e-4 to leave a variance of order 8e-5, throwing away a decimal
/// digit for nothing. SC-014 (d) reads a std/mean RATIO against a 0.30 line, so
/// the estimator is part of the criterion, not an implementation taste.
class T012Running {
public:
    void add(double v) noexcept {
        ++n_;
        const double delta = v - mean_;
        mean_ += delta / static_cast<double>(n_);
        m2_ += delta * (v - mean_);
    }
    [[nodiscard]] std::size_t count() const noexcept { return n_; }
    [[nodiscard]] double average() const noexcept { return mean_; }
    [[nodiscard]] double stdDev() const noexcept {
        if (n_ < 2u) {
            return 0.0;
        }
        const double var = m2_ / static_cast<double>(n_);
        return (var > 0.0) ? std::sqrt(var) : 0.0;
    }

private:
    std::size_t n_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

/// @brief Pearson correlation of two equal-length series, in double.
///
/// Returns 0 when either series is constant: a constant series has no linear
/// relationship to anything, and SC-006 (b) gates |rho| from ABOVE, so the
/// degenerate case must not manufacture a correlation. The case that uses it
/// asserts its own non-vacuity (both series must MOVE) separately.
[[nodiscard]] double t012Pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
    if (n < 2u) {
        return 0.0;
    }
    double sumA = 0.0;
    double sumB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sumA += a[i];
        sumB += b[i];
    }
    const double meanA = sumA / static_cast<double>(n);
    const double meanB = sumB / static_cast<double>(n);
    double cov = 0.0;
    double varA = 0.0;
    double varB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        cov += da * db;
        varA += da * da;
        varB += db * db;
    }
    const double denom = std::sqrt(varA * varB);
    return (denom > 0.0) ? (cov / denom) : 0.0;
}

/// @brief Linearly interpolated percentile. SORTS @p samples in place.
[[nodiscard]] double t012Percentile(std::vector<double>& samples, double fraction) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const double position = fraction * static_cast<double>(samples.size() - 1u);
    const double lower = std::floor(position);
    const std::size_t i0 = static_cast<std::size_t>(lower);
    const std::size_t i1 = (i0 + 1u < samples.size()) ? (i0 + 1u) : i0;
    const double t = position - lower;
    return samples[i0] * (1.0 - t) + samples[i1] * t;
}

/// The two windows every 1800 s case below shares, named once.
constexpr double kT012RunSeconds = 1800.0;
constexpr double kT012LateWindowSeconds = 600.0;

/// @brief Steps in @p seconds of simulated time at @p stepSeconds per step.
[[nodiscard]] std::size_t t012StepsFor(double seconds, double stepSeconds) noexcept {
    const double steps = std::floor(seconds / stepSeconds);
    return (steps < 1.0) ? std::size_t{1} : static_cast<std::size_t>(steps);
}

/// @brief The 1 Hz sampling stride on the control grid (94 at the FR-082 default).
[[nodiscard]] std::size_t t012SampleStride(double stepSeconds) noexcept {
    const double stride = std::floor(1.0 / stepSeconds + 0.5);
    return (stride < 1.0) ? std::size_t{1} : static_cast<std::size_t>(stride);
}

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// std::numeric_limits<float>::quiet_NaN()/infinity() are FORBIDDEN here by the
/// tasks.md conventions block: they fold to finite garbage on the -ffast-math
/// macOS/Linux legs, so a rejection test written with them passes for the wrong
/// reason. Idiom transcribed from
/// resonance_drift_network_nonfinite_test.cpp:149-155.
///
/// This TU is NOT in the -fno-fast-math block, so the value below is only ever
/// HANDED to a setter; the assertion that follows is "the previous value still
/// stands", never an IEEE claim about the injected value itself.
[[nodiscard]] float t012MakeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

}  // namespace

// ==============================================================================
// T012 (1) - SC-014 (e) / FR-062: outputs are HELD between control steps
// ==============================================================================
// WHY THIS CASE EXISTS, AND WHY NEITHER BIT-IDENTITY CRITERION SUBSUMES IT.
// SC-006 (a) and SC-008 are both DOUBLES-SCOPED comparisons of end state. An
// implementation that recomputed the published float on EVERY processChunk()
// call would keep every double identical and would pass both of them unchanged,
// while breaking the one contract FeedbackEcology depends on:
// refreshGates()'s `target != lastGateTarget` guard is load-bearing, and "a
// Phase-8 agent writing one loop's wake per block would otherwise stretch every
// OTHER loop's ramp without bound" (feedback_ecology.h:2292-2296).
//
// A held-vs-republished distinction is INVISIBLE on a settled output, so the
// case first drives a wake ramp 0 -> 1 and observes only while the published
// value is IN MOTION. The non-vacuity clause at the end asserts the value really
// did move at the step boundaries; without it, an engine that published a
// constant would pass this case trivially.
//
// FALSIFICATION (run it, then restore): move the publish() call out of
// simulationStep() and to the top of processChunk(). Both sub-arms below must
// fail on their mid-step mismatch counters, while SC-006 (a) and SC-008 stay
// green - which is the whole argument for this case existing.
TEST_CASE("EcosystemEngine_OutputsAreHeldBetweenSteps", "[ecosystem_engine]") {
    // ~23.5 KB (plan S9): make_unique, never a plain stack local.
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());

    const std::size_t agents = engine->getAgentCount();
    REQUIRE(agents == 32u);
    REQUIRE(engine->getStepIntervalChunks() == 8u);
    // 8 chunks x 64 samples: the step boundary falls every 512 samples.
    REQUIRE(kDefaultSamplesPerStep == 512u);

    const std::size_t rampSteps = expectedRampSteps(engine->getStepDurationSeconds());
    REQUIRE(rampSteps == 5u);  // 50 ms / 10.667 ms, quantised up (FR-070)

    // ---- put the published surface INTO MOTION -----------------------------
    // Every agent is driven to silence first, so the 0 -> 1 ramp below is a
    // full-scale move rather than a settled value the case could not see held.
    for (std::size_t i = 0; i < agents; ++i) {
        engine->setAgentWake(i, 0.0f);
    }
    for (std::size_t s = 0; s <= rampSteps; ++s) {
        engine->processChunk(kDefaultSamplesPerStep);
    }
    std::size_t nonSilentAfterFade = 0;
    for (std::size_t i = 0; i < agents; ++i) {
        if (engine->getAgentOutput(i) != 0.0f) {
            ++nonSilentAfterFade;
        }
    }
    REQUIRE(nonSilentAfterFade == 0u);
    for (std::size_t i = 0; i < agents; ++i) {
        engine->setAgentWake(i, 1.0f);
    }

    // Both sub-arms start FROM a step boundary: every settle call above was
    // exactly 512 samples, so both clock residues are 0 here.
    std::array<float, Engine::kMaxAgents> heldOutput{};
    std::array<float, Engine::kMaxAgents> heldWake{};

    std::size_t midStepOutputMismatches = 0;
    std::size_t midStepWakeMismatches = 0;
    std::size_t boundariesThatMoved = 0;
    std::size_t observedBoundaries = 0;

    // One arm: deliver each step's 512 samples as `calls` calls of
    // `512 / calls` samples, checking the held surface after each of the first
    // `calls - 1` of them.
    const auto runHeldArm = [&](std::size_t calls, std::size_t steps) {
        const std::size_t perCall = kDefaultSamplesPerStep / calls;
        for (std::size_t s = 0; s < steps; ++s) {
            for (std::size_t i = 0; i < agents; ++i) {
                heldOutput[i] = engine->getAgentOutput(i);
                heldWake[i] = engine->getAgentWake(i);
            }
            for (std::size_t c = 1; c < calls; ++c) {
                engine->processChunk(perCall);
                for (std::size_t i = 0; i < agents; ++i) {
                    if (engine->getAgentOutput(i) != heldOutput[i]) {
                        ++midStepOutputMismatches;
                    }
                    if (engine->getAgentWake(i) != heldWake[i]) {
                        ++midStepWakeMismatches;
                    }
                }
            }
            // The ONE call that crosses the boundary. Only this one may change
            // the published surface.
            engine->processChunk(perCall);
            ++observedBoundaries;
            bool moved = false;
            for (std::size_t i = 0; i < agents; ++i) {
                if (engine->getAgentOutput(i) != heldOutput[i]) {
                    moved = true;
                }
            }
            if (moved) {
                ++boundariesThatMoved;
            }
        }
    };

    // ---- arm 1: 16 calls of 32 samples, across the whole 0 -> 1 ramp -------
    runHeldArm(16u, rampSteps);

    // ---- arm 2: 512 calls of 1 sample --------------------------------------
    // The first ramp has reached its target by now, so a second one (1 -> 0) is
    // started to keep this arm's observations on a MOVING surface too.
    for (std::size_t i = 0; i < agents; ++i) {
        engine->setAgentWake(i, 0.0f);
    }
    runHeldArm(512u, rampSteps);

    INFO("mid-step output mismatches " << midStepOutputMismatches << ", wake mismatches "
                                       << midStepWakeMismatches << ", boundaries that moved "
                                       << boundariesThatMoved << " of " << observedBoundaries);

    REQUIRE(midStepOutputMismatches == 0u);
    REQUIRE(midStepWakeMismatches == 0u);
    // NON-VACUITY: a surface that never moves is "held" for free. Both ramps are
    // full-scale over rampSteps steps, so every observed boundary must move.
    REQUIRE(observedBoundaries == 2u * rampSteps);
    REQUIRE(boundariesThatMoved == observedBoundaries);
}

// ==============================================================================
// T012 (2) - SC-014 (d) / FR-072: a DORMANT agent stays in the economy
// ==============================================================================
// This is the one place the spec argues AGAINST the cross-cutting Dormancy rule
// (roadmap lines 543-545), so it carries the strongest evidence available rather
// than the cheapest. "Their energies still move" is NOT the criterion: one ULP
// of a neighbour's rounding satisfies that, including in an implementation that
// froze the dormant agent and let float noise leak in. The criterion is that the
// dormant population's per-agent ACTIVITY - std(e_i) / grandMean over the late
// 600 s window - clears 0.30 in its own right AND sits inside 0.5x-2x of the
// awake population's in the SAME run, i.e. the two halves live the same economy.
//
// The mechanism-level argument the rule demands is already on setAgentDormant()
// in the header: THE AGENT *IS* THE GENERATOR, there is no chain behind it to
// skip, and its energy is a share of a CONSERVED budget - excluding it would
// strand or destroy that share and break FR-023/FR-054 outright. SC-001's
// conservation clauses are therefore asserted here too: if dormancy leaked
// energy, this is where it would show.
//
// FALSIFICATION (run it, then restore): add `if (dormant_[i]) continue;` at the
// top of stage 6 (grazing). The dormant activity collapses and both the >= 0.30
// and the 0.5x-2x clauses fail, while every OTHER case in this TU stays green.
TEST_CASE("EcosystemEngine_DormantAgentsStayInTheEconomy", "[ecosystem_engine]") {
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());

    const std::size_t agents = engine->getAgentCount();
    REQUIRE(agents == 32u);

    // Half the population dormant - every EVEN index.
    for (std::size_t i = 0; i < agents; i += 2u) {
        engine->setAgentDormant(i, true);
    }
    REQUIRE(engine->isAgentDormant(0));
    REQUIRE(!engine->isAgentDormant(1));

    const double stepSeconds = engine->getStepDurationSeconds();
    const std::size_t totalSteps = t012StepsFor(kT012RunSeconds, stepSeconds);
    const std::size_t windowSteps = t012StepsFor(kT012LateWindowSeconds, stepSeconds);
    const std::size_t windowStart = totalSteps - windowSteps;
    const std::size_t stride = t012SampleStride(stepSeconds);
    REQUIRE(stride == 94u);  // 1 Hz on a 10.667 ms grid

    // STREAMING (tasks.md conventions): a 48-agent x 1800 s trace is legal at
    // ~700 KB, but nothing here needs the samples twice, so the window is
    // accumulated incrementally and never materialised.
    std::array<T012Running, Engine::kMaxAgents> perAgent{};
    double worstRelativeError = 0.0;
    std::size_t nonFiniteSamples = 0;
    const double budget = engine->getEnergyBudget();

    for (std::size_t s = 1; s <= totalSteps; ++s) {
        engine->processChunk(kDefaultSamplesPerStep);
        if (s <= windowStart || ((s - windowStart) % stride) != 0u) {
            continue;
        }
        for (std::size_t i = 0; i < agents; ++i) {
            const double e = engine->getAgentEnergy(i);
            if (!isFiniteD(e)) {
                ++nonFiniteSamples;
            }
            perAgent[i].add(e);
        }
        const double error = std::fabs(engine->getTotalEnergy() - budget) / budget;
        if (error > worstRelativeError) {
            worstRelativeError = error;
        }
    }

    // ---- SC-001's conservation clauses, on a run half of which is dormant ---
    INFO("worst relative conservation error "
         << worstRelativeError << ", non-finite samples " << nonFiniteSamples << ", violations "
         << engine->getConservationViolationCount() << ", containments "
         << engine->getNonFiniteContainmentCount());
    REQUIRE(nonFiniteSamples == 0u);
    REQUIRE(worstRelativeError <= 1.0e-9);
    REQUIRE(engine->getConservationViolationCount() == 0u);
    REQUIRE(engine->getNonFiniteContainmentCount() == 0u);

    // ---- the activity statistic (plan S10.2's lateWindow, file-local here) --
    // grandMean = mean over agents of mean(e_i), floored at 1e-12 so a dead
    // population cannot manufacture a large ratio out of a tiny denominator.
    double meanOfMeans = 0.0;
    std::size_t thinlySampledAgents = 0;
    for (std::size_t i = 0; i < agents; ++i) {
        if (perAgent[i].count() < 500u) {
            ++thinlySampledAgents;
        }
        meanOfMeans += perAgent[i].average();
    }
    REQUIRE(thinlySampledAgents == 0u);  // the window really was sampled
    double grandMean = meanOfMeans / static_cast<double>(agents);
    if (grandMean < 1.0e-12) {
        grandMean = 1.0e-12;
    }

    double dormantActivity = 0.0;
    double awakeActivity = 0.0;
    std::size_t dormantCount = 0;
    std::size_t awakeCount = 0;
    for (std::size_t i = 0; i < agents; ++i) {
        const double activity = perAgent[i].stdDev() / grandMean;
        if (engine->isAgentDormant(i)) {
            dormantActivity += activity;
            ++dormantCount;
        } else {
            awakeActivity += activity;
            ++awakeCount;
        }
    }
    REQUIRE(dormantCount == 16u);
    REQUIRE(awakeCount == 16u);
    dormantActivity /= static_cast<double>(dormantCount);
    awakeActivity /= static_cast<double>(awakeCount);

    INFO("grand mean " << grandMean << ", dormant activity " << dormantActivity
                       << ", awake activity " << awakeActivity);

    // The dormant half is economically active IN ITS OWN RIGHT...
    REQUIRE(dormantActivity >= 0.30);
    // ...and indistinguishable in scale from the half that is publishing.
    REQUIRE(awakeActivity > 0.0);
    REQUIRE(dormantActivity >= 0.5 * awakeActivity);
    REQUIRE(dormantActivity <= 2.0 * awakeActivity);

    // The OUTPUT half of FR-072, restated here so the case cannot be read as
    // "dormancy does nothing": every dormant agent publishes EXACTLY zero, and
    // at least one of them is doing so while still HOLDING real energy. The
    // live-agent search is not decoration - agents really do die at the defaults
    // (FINDINGS.md's terciles report 13-63 % alive by regime), so a hard-coded
    // index would make this clause a lottery.
    std::size_t dormantPublishingNonZero = 0;
    std::size_t liveDormantAgents = 0;
    std::size_t publishingAwakeAgents = 0;
    for (std::size_t i = 0; i < agents; ++i) {
        if (engine->isAgentDormant(i)) {
            if (engine->getAgentOutput(i) != 0.0f) {
                ++dormantPublishingNonZero;
            }
            if (engine->getAgentEnergy(i) > 0.0) {
                ++liveDormantAgents;
            }
        } else if (engine->getAgentOutput(i) > 0.0f) {
            ++publishingAwakeAgents;
        }
    }
    INFO("dormant agents publishing non-zero " << dormantPublishingNonZero << ", live dormant "
                                               << liveDormantAgents << ", publishing awake "
                                               << publishingAwakeAgents);
    REQUIRE(dormantPublishingNonZero == 0u);
    REQUIRE(liveDormantAgents > 0u);
    REQUIRE(publishingAwakeAgents > 0u);
}

// ==============================================================================
// T012 (3) - SC-019: FR-061's output ANCHOR is invariant to budget and
//                    population; the upper TAIL is not, and is reported
// ==============================================================================
// This is the consumer contract Phase 10's depth mapping rests on, and no other
// criterion stands in for it: SC-009 only asserts the outputs stay finite and
// inside [0, 1], which a wrongly scaled anchor satisfies just as well.
//
// WHAT IS INVARIANT, AND WHY - the correction spec.md now carries (SC-019).
// The ANCHOR is invariant by construction: the population mean of FR-061's
// output is exactly `0.5 * (sum e)/B`, and the pool/agent split is itself
// share-scaled (plan S5: every share-unit rule scales with energyBudget /
// agentCount, the cell rule with energyBudget / resourceCells), so every cell
// lands at ~0.48 whatever the budget and the population.
//
// The DISTRIBUTION is NOT the same system rescaled, and the earlier reading of
// this case - "the nine cells are the same dynamical system rescaled", so every
// percentile must agree to +/- 0.05 - is false on BOTH axes, by the spec's own
// normative rules:
//   * energyBudget axis: FR-030's affinity force is scaled by the neighbour's
//     ABSOLUTE energy, while FR-032's crowding repulsion is energy-INDEPENDENT
//     and FR-034's moveRate / maxSpeed are absolute habitat units. FR-033 says
//     so in as many words ("at a 1.0 budget over 32 agents a neighbour holds
//     ~0.016, so the force never approached maxSpeed"). Scale the budget and
//     the movement regime changes; every other rule is homogeneous of degree 1
//     in energy. MEASURED, in the reference prototype: with moveRate,
//     forageRate and crowding all 0 the nine cells' statistics collapse to
//     IDENTICAL values across B = 0.1 / 1 / 10 to four decimals, and with
//     movement on they do not. The movement rules are the whole of the budget
//     dependence.
//   * agentCount axis: n agents in a FIXED unit torus at a fixed kernelSigma,
//     grazing a FIXED resourceCells field, is a different interaction topology,
//     not a rescaling - pair density per agent rises with n, so predation
//     (FR-020, default 0.55) concentrates energy harder and the upper tail
//     widens. FR-008's share conversion normalises the THRESHOLDS (refuge
//     floor, capacity, cell capacity, satiation); it cannot normalise the
//     number of neighbours.
// Both effects are real, documented, prototype-proven behaviour - not defects,
// and not something an implementation is free to remove.
//
// So this case gates the anchor and the LOWER band, which are invariant, and
// states the upper band as an ENVELOPE every cell must sit inside rather than
// as a cross-cell agreement the rules cannot deliver. The envelope figures are
// transcribed from the measured runs (the same "transcribed at implementation
// time" rule SC-002 (d) already uses), with the margin recorded on each line.
// A wrongly scaled anchor - a missing agentCount, a doubled budget, a dropped
// 0.5 - blows through every one of them at once.
//
// Plan R-5 is why the energy-share report is not optional: the population mean
// output is `0.5 * (sum e)/B`, and at steady state the agents hold ~96 % of the
// budget, so the EXPECTED mean is ~0.48 and a cell that missed the band would
// say immediately whether the ANCHOR moved or the agents' SHARE of the budget
// did. It is a WARN, never a gate - gating on it would be gating on the
// pool/agent split, which is not what SC-019 is about.
namespace {

/// One (energyBudget x agentCount) cell of SC-019's grid, pooled over its seeds.
struct T012AnchorCell {
    double budget = 0.0;
    std::size_t agents = 0;
    double meanOutput = 0.0;    ///< late-window population mean of getAgentOutput
    double p05 = 0.0;           ///< late-window population 5th percentile
    double p25 = 0.0;           ///< late-window population 25th percentile
    double p50 = 0.0;           ///< late-window population median
    double p75 = 0.0;           ///< late-window population 75th percentile
    double p95 = 0.0;           ///< late-window population 95th percentile
    double railFraction = 0.0;  ///< share of late-window samples at FR-061's upper rail
    double energyShare = 0.0;   ///< late-window mean of (sum energy_)/energyBudget
};

/// @brief Run one SC-019 cell: three seeded 1800 s runs, statistics pooled.
[[nodiscard]] T012AnchorCell t012RunAnchorCell(double budget, std::size_t agents,
                                               const std::array<std::uint32_t, 3>& seeds) {
    T012AnchorCell cell{};
    cell.budget = budget;
    cell.agents = agents;

    T012Running outputMean{};
    T012Running shareMean{};
    std::size_t railed = 0;
    // The percentiles need the samples themselves; 3 seeds x ~600 samples x 48
    // agents is ~86 k doubles - test-side heap, materialised one cell at a time.
    std::vector<double> population;

    for (const std::uint32_t seed : seeds) {
        auto engine = std::make_unique<Engine>();
        engine->setSeed(seed);
        Engine::PrepareConfig cfg = defaultConfig();
        cfg.agentCount = agents;
        cfg.energyBudget = budget;
        engine->prepare(48000.0, cfg);

        const double stepSeconds = engine->getStepDurationSeconds();
        const std::size_t totalSteps = t012StepsFor(kT012RunSeconds, stepSeconds);
        const std::size_t windowSteps = t012StepsFor(kT012LateWindowSeconds, stepSeconds);
        const std::size_t windowStart = totalSteps - windowSteps;
        const std::size_t stride = t012SampleStride(stepSeconds);
        const std::size_t live = engine->getAgentCount();

        for (std::size_t s = 1; s <= totalSteps; ++s) {
            engine->processChunk(kDefaultSamplesPerStep);
            if (s <= windowStart || ((s - windowStart) % stride) != 0u) {
                continue;
            }
            double energySum = 0.0;
            for (std::size_t i = 0; i < live; ++i) {
                const float published = engine->getAgentOutput(i);
                const double out = static_cast<double>(published);
                outputMean.add(out);
                population.push_back(out);
                // FR-061's UPPER rail. The clamp produces exactly 1.0f, so this
                // is not a tolerance question: it is the same rail
                // getAgentClampedStepFraction() counts, read here from the
                // published surface.
                if (published >= 1.0f) {
                    ++railed;
                }
                energySum += engine->getAgentEnergy(i);
            }
            shareMean.add(energySum / engine->getEnergyBudget());
        }
    }

    cell.meanOutput = outputMean.average();
    cell.energyShare = shareMean.average();
    cell.railFraction = population.empty()
                            ? 0.0
                            : (static_cast<double>(railed) / static_cast<double>(population.size()));
    cell.p05 = t012Percentile(population, 0.05);
    cell.p25 = t012Percentile(population, 0.25);
    cell.p50 = t012Percentile(population, 0.50);
    cell.p75 = t012Percentile(population, 0.75);
    cell.p95 = t012Percentile(population, 0.95);
    return cell;
}

}  // namespace

TEST_CASE("EcosystemEngine_OutputAnchorIsScaleInvariant", "[ecosystem_engine]") {
    constexpr std::array<double, 3> kBudgets{{0.1, 1.0, 10.0}};
    constexpr std::array<std::size_t, 3> kAgentCounts{{24u, 32u, 48u}};
    constexpr std::array<std::uint32_t, 3> kSeeds{{0xC0FFEEu, 0x5EEDu, 0x1234567u}};

    std::array<T012AnchorCell, 9> cells{};
    std::size_t next = 0;
    for (const double budget : kBudgets) {
        for (const std::size_t agents : kAgentCounts) {
            cells[next] = t012RunAnchorCell(budget, agents, kSeeds);
            ++next;
        }
    }
    REQUIRE(next == cells.size());

    // ---- the plan R-5 report: NEVER a gate, always printed -----------------
    for (const T012AnchorCell& cell : cells) {
        WARN("SC-019 cell B=" << cell.budget << " n=" << cell.agents << ": mean output "
                              << cell.meanOutput << ", sum(e)/B " << cell.energyShare << ", p05 "
                              << cell.p05 << ", p25 " << cell.p25 << ", p50 " << cell.p50 << ", p75 "
                              << cell.p75 << ", p95 " << cell.p95 << ", rail " << cell.railFraction);
    }

    // ---- clause 1: the anchor lands at 0.5 +/- 0.05 in EVERY cell ----------
    for (const T012AnchorCell& cell : cells) {
        INFO("cell B=" << cell.budget << " n=" << cell.agents << ": mean output "
                       << cell.meanOutput << " (sum(e)/B " << cell.energyShare << ")");
        REQUIRE(cell.meanOutput >= 0.45);
        REQUIRE(cell.meanOutput <= 0.55);
    }

    // ---- clause 2 (b): the LOWER band agrees across all nine cells ---------
    // This is the half of the old range clause that SURVIVES, and it survives
    // on measurement, not on argument: p05 spans [0.174, 0.190] over the nine
    // cells, spread 0.0156 against the +/- 0.05 bound - a 3x margin. The p95
    // half of that clause is struck; see the banner and spec.md SC-019.
    double lowestP05 = cells[0].p05;
    double highestP05 = cells[0].p05;
    double lowestP95 = cells[0].p95;
    double highestP95 = cells[0].p95;
    for (const T012AnchorCell& cell : cells) {
        lowestP05 = (cell.p05 < lowestP05) ? cell.p05 : lowestP05;
        highestP05 = (cell.p05 > highestP05) ? cell.p05 : highestP05;
        lowestP95 = (cell.p95 < lowestP95) ? cell.p95 : lowestP95;
        highestP95 = (cell.p95 > highestP95) ? cell.p95 : highestP95;
    }
    INFO("p05 spread " << (highestP05 - lowestP05) << " over [" << lowestP05 << ", " << highestP05
                       << "], p95 spread " << (highestP95 - lowestP95) << " over [" << lowestP95
                       << ", " << highestP95 << "]");
    REQUIRE((highestP05 - lowestP05) <= 0.05);

    // ---- clause 2 (c): every cell sits inside the measured ENVELOPE --------
    // Transcribed from the measured nine-cell run (1800 s x 3 seeds), the way
    // SC-002 (d)'s figure is transcribed at implementation time. Measured span
    // -> bound, so the margin is auditable:
    //   p05    [0.174, 0.190]  -> [0.12, 0.24]
    //   median [0.316, 0.472]  -> [0.25, 0.55]
    //   p95    [0.801, 1.000]  -> [0.70, 1.00]
    //   rail   [0.0003, 0.107] -> <= 0.20   (worst cell B = 10, n = 48)
    // The envelope is what carries the criterion's TEETH now that the cross-cell
    // agreement clause is gone: dropping agentCount from FR-061's formula
    // (outputs collapse to ~0.02), doubling the anchor, or normalising by
    // resourceCells instead of agentCount each breaks clause 1 AND every line
    // below, at every cell, at once.
    for (const T012AnchorCell& cell : cells) {
        INFO("cell B=" << cell.budget << " n=" << cell.agents << ": p05 " << cell.p05 << ", p25 "
                       << cell.p25 << ", p50 " << cell.p50 << ", p75 " << cell.p75 << ", p95 "
                       << cell.p95 << ", rail " << cell.railFraction);
        REQUIRE(cell.p05 >= 0.12);
        REQUIRE(cell.p05 <= 0.24);
        REQUIRE(cell.p50 >= 0.25);
        REQUIRE(cell.p50 <= 0.55);
        REQUIRE(cell.p95 >= 0.70);
        REQUIRE(cell.p95 <= 1.00);
        // FR-061's clamp is the cost of the anchor and must not swallow the
        // signal: an upward mis-scale rails the whole population and is caught
        // here even if some other statistic were massaged back into band.
        REQUIRE(cell.railFraction <= 0.20);
    }

    // NON-VACUITY: a population railed at one value would also "agree" across
    // cells. The 5th-95th band must be a real spread.
    REQUIRE((highestP95 - lowestP05) > 0.05);
}

// ==============================================================================
// T012 (4) - SC-006: DETERMINISM under seed, and INDEPENDENCE across seeds
// ==============================================================================
// (a) is a SAME-BINARY STRUCTURAL IDENTITY, which is the one class of exact
// float comparison this phase allows (tasks.md conventions; the others are
// SC-006 (c), SC-008, SC-014 (a)/(e), SC-020's `scale == 1.0`, SC-021's
// `cell == 0.0` and FR-071's `delta == 0.0`). It compares EVERY agent double
// INCLUDING freq_ - which has no public getter and is read through the inspect
// probe - and, explicitly, getAgentOutput()/getAgentWake(): a doubles-only
// comparison would leave FR-062's published surface outside BOTH bit-identity
// criteria (SC-006 (a) and SC-008), which is exactly the gap SC-014 (e) above
// closes from the other side.
//
// (b) is the independence half. Adjacent seeds must not produce correlated runs;
// the prototype measured rho = -0.02 on the entropy series.
//
// (d) NO BIT-EXACT FLOAT GOLDEN IS CHECKED IN, and nothing here needs one: every
// exact comparison below is between two objects in THIS binary, in this process,
// this second. A cross-run or cross-toolchain comparison would have to go
// through tests/test_helpers/render_fingerprint.h (kSampleTolerance = 5.0e-4f,
// kMetricTolerance = 2.5e-4); there is no such comparison anywhere in this TU,
// which is what keeps node tools/lint-float-bit-goldens.js clean.
namespace {

constexpr std::size_t kT012DetAgents = 32u;
constexpr std::size_t kT012DetCells = 64u;
/// 5 doubles per agent (energy, x, y, phase, FREQ), every cell, and the pool.
constexpr std::size_t kT012DetDoubles = 5u * kT012DetAgents + kT012DetCells + 1u;
/// 2 floats per agent: the FR-062 published surface (output, wake).
constexpr std::size_t kT012DetFloats = 2u * kT012DetAgents;

[[nodiscard]] std::array<double, kT012DetDoubles> t012GatherDoubles(const Engine& e) noexcept {
    std::array<double, kT012DetDoubles> out{};
    for (std::size_t i = 0; i < kT012DetAgents; ++i) {
        out[5u * i + 0u] = e.getAgentEnergy(i);
        out[5u * i + 1u] = e.getAgentPositionX(i);
        out[5u * i + 2u] = e.getAgentPositionY(i);
        out[5u * i + 3u] = e.getAgentPhase(i);
        out[5u * i + 4u] = Probe::freq(e, i);
    }
    for (std::size_t k = 0; k < kT012DetCells; ++k) {
        out[5u * kT012DetAgents + k] = e.getCellEnergy(k);
    }
    out[kT012DetDoubles - 1u] = e.getPoolEnergy();
    return out;
}

[[nodiscard]] std::array<float, kT012DetFloats> t012GatherPublished(const Engine& e) noexcept {
    std::array<float, kT012DetFloats> out{};
    for (std::size_t i = 0; i < kT012DetAgents; ++i) {
        out[2u * i + 0u] = e.getAgentOutput(i);
        out[2u * i + 1u] = e.getAgentWake(i);
    }
    return out;
}

/// @brief The entropy series of one seeded 900 s run, sampled at 1 Hz.
[[nodiscard]] std::vector<double> t012EntropySeries(std::uint32_t seed) {
    auto engine = std::make_unique<Engine>();
    engine->setSeed(seed);
    engine->prepare(48000.0, defaultConfig());

    const double stepSeconds = engine->getStepDurationSeconds();
    const std::size_t totalSteps = t012StepsFor(900.0, stepSeconds);
    const std::size_t stride = t012SampleStride(stepSeconds);

    std::vector<double> series;
    series.reserve(totalSteps / stride + 1u);
    for (std::size_t s = 1; s <= totalSteps; ++s) {
        engine->processChunk(kDefaultSamplesPerStep);
        if ((s % stride) == 0u) {
            series.push_back(engine->getEnergyEntropy());
        }
    }
    return series;
}

}  // namespace

TEST_CASE("EcosystemEngine_DeterministicUnderSeed", "[ecosystem_engine]") {
    // -------------------------------------------------------------------------
    // (a) same seed, same binary, 100 000 control steps -> bit-identical
    // -------------------------------------------------------------------------
    {
        constexpr std::size_t kSteps = 100000u;
        constexpr std::size_t kCheckEvery = 10000u;

        auto left = std::make_unique<Engine>();
        auto right = std::make_unique<Engine>();
        left->setSeed(0xC0FFEEu);
        right->setSeed(0xC0FFEEu);
        left->prepare(48000.0, defaultConfig());
        right->prepare(48000.0, defaultConfig());
        REQUIRE(left->getAgentCount() == kT012DetAgents);
        REQUIRE(left->getResourceCells() == kT012DetCells);

        // Both instances get the SAME non-trivial wake pattern. SC-006 (a) is
        // about what the SEED determines, and the published surface is part of
        // that; a uniform wake of 1 would leave the float half of the comparison
        // carrying almost no information.
        for (std::size_t i = 0; i < kT012DetAgents; i += 3u) {
            left->setAgentWake(i, 0.25f);
            right->setAgentWake(i, 0.25f);
        }

        std::size_t doubleMismatchChecks = 0;
        std::size_t floatMismatchChecks = 0;
        for (std::size_t s = 1; s <= kSteps; ++s) {
            left->processChunk(kDefaultSamplesPerStep);
            right->processChunk(kDefaultSamplesPerStep);
            if ((s % kCheckEvery) != 0u) {
                continue;
            }
            const std::array<double, kT012DetDoubles> a = t012GatherDoubles(*left);
            const std::array<double, kT012DetDoubles> b = t012GatherDoubles(*right);
            // Byte equality, deliberately (see the first memcmp in this file).
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
            if (std::memcmp(a.data(), b.data(), sizeof(a)) != 0) {
                ++doubleMismatchChecks;
            }
            const std::array<float, kT012DetFloats> pa = t012GatherPublished(*left);
            const std::array<float, kT012DetFloats> pb = t012GatherPublished(*right);
            // Byte equality, deliberately (see the first memcmp in this file).
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
            if (std::memcmp(pa.data(), pb.data(), sizeof(pa)) != 0) {
                ++floatMismatchChecks;
            }
        }

        INFO("double mismatches " << doubleMismatchChecks << ", published-float mismatches "
                                  << floatMismatchChecks << " over " << (kSteps / kCheckEvery)
                                  << " checkpoints");
        REQUIRE(doubleMismatchChecks == 0u);
        REQUIRE(floatMismatchChecks == 0u);
        REQUIRE(left->getControlStepCount() == kSteps);
        REQUIRE(right->getControlStepCount() == kSteps);

        // NON-VACUITY: two frozen instances are also "identical". The run must
        // have gone somewhere, and the published surface must carry real values.
        // Counted over the population rather than pinned to one index: agents do
        // die at the defaults, so a hard-coded index would be a lottery.
        std::size_t liveAgents = 0;
        std::size_t publishingAgents = 0;
        for (std::size_t i = 0; i < kT012DetAgents; ++i) {
            if (left->getAgentEnergy(i) > 0.0) {
                ++liveAgents;
            }
            if (left->getAgentOutput(i) > 0.0f) {
                ++publishingAgents;
            }
        }
        INFO("live agents " << liveAgents << ", publishing agents " << publishingAgents);
        REQUIRE(liveAgents > 0u);
        REQUIRE(publishingAgents > 0u);
        REQUIRE(left->getAgentWake(0) == 0.25f);
    }

    // -------------------------------------------------------------------------
    // (b) adjacent seeds are UNCORRELATED
    // -------------------------------------------------------------------------
    // The per-agent cross-seed |corr| bound is SC-004 (b)'s and belongs to T018,
    // which owns the within-run floor it is stated against; this clause is the
    // aggregate half, on the entropy series, where the prototype measured -0.02.
    {
        constexpr std::array<std::uint32_t, 3> kBaseSeeds{{0xC0FFEEu, 100u, 7u}};
        for (const std::uint32_t seed : kBaseSeeds) {
            const std::vector<double> a = t012EntropySeries(seed);
            const std::vector<double> b = t012EntropySeries(seed + 1u);
            REQUIRE(a.size() >= 800u);
            REQUIRE(a.size() == b.size());

            // Non-vacuity: a constant series correlates with nothing, so the
            // bound would pass for the wrong reason. Both series must MOVE.
            T012Running statsA{};
            T012Running statsB{};
            for (std::size_t i = 0; i < a.size(); ++i) {
                statsA.add(a[i]);
                statsB.add(b[i]);
            }
            const double rho = t012Pearson(a, b);
            INFO("seeds " << seed << " / " << (seed + 1u) << ": rho " << rho << ", std(a) "
                          << statsA.stdDev() << ", std(b) " << statsB.stdDev());
            REQUIRE(statsA.stdDev() > 0.0);
            REQUIRE(statsB.stdDev() > 0.0);
            REQUIRE(std::fabs(rho) <= 0.2);
        }
    }
}

// ==============================================================================
// T012 (5) - SC-007 (a) / FR-003: NO ALLOCATION AFTER prepare()
// ==============================================================================
// The engine is constructed OUTSIDE the AllocationScope on purpose: it is
// ~23.5 KB (plan S9) and a make_unique inside the scope would be counted as an
// allocation and would fail the criterion for entirely the wrong reason.
//
// Nothing inside the scope may REQUIRE or INFO: Catch2's expression and message
// machinery reaches the heap, so every assertion is made after the scope closes
// (the resonance_drift_network_test.cpp:588-600 idiom). The scope latches its
// count in its destructor, so the live count is read through the detector while
// the scope is still open.
TEST_CASE("EcosystemEngine_NoAllocationAfterPrepare", "[ecosystem_engine]") {
    auto engine = std::make_unique<Engine>();

    constexpr std::size_t kChunkCalls = 10000u;

    std::size_t allocations = 0;
    std::size_t reportedBytes = 0;
    std::uint64_t stepsRun = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        engine->prepare(48000.0, defaultConfig());

        for (std::size_t s = 0; s < kChunkCalls; ++s) {
            engine->processChunk(kDefaultSamplesPerStep);
        }
        stepsRun = engine->getControlStepCount();

        // EVERY setter: the 22 Appendix-A knobs through the T005 table (which
        // drives freqLoHz/freqHiHz through the one paired setter), the paired
        // setter directly, the affinity matrix, and the three FR-070 / FR-071 /
        // FR-072 event hooks.
        for (std::size_t i = 0; i < kKnobCount; ++i) {
            kKnobTable[i].set(*engine, kKnobTable[i].inRange);
        }
        engine->setFreqRangeHz(0.0012f, 0.020f);
        for (std::size_t f = 0; f < Engine::kNumKinds; ++f) {
            for (std::size_t t = 0; t < Engine::kNumKinds; ++t) {
                engine->setAffinity(static_cast<Engine::Kind>(f), static_cast<Engine::Kind>(t),
                                    0.3f);
            }
        }
        const std::size_t agents = engine->getAgentCount();
        for (std::size_t i = 0; i < agents; ++i) {
            engine->setAgentWake(i, 0.75f);
            engine->setAgentDormant(i, (i % 2u) == 0u);
            engine->perturbAgent(i, 0.1f);
            engine->perturbAgent(i, -0.1f);
        }

        // (*engine) not engine-> : the ENGINE's reset, not unique_ptr::reset.
        (*engine).reset();
        engine->setSeed(0x1234u);
        engine->processChunk(kDefaultSamplesPerStep);

        reportedBytes = engine->getAllocatedBytes();
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    INFO("allocations " << allocations << ", reported bytes " << reportedBytes << ", steps run "
                        << stepsRun);
    // Non-vacuity: the scope really did cover a working simulation.
    REQUIRE(stepsRun == kChunkCalls);
    REQUIRE(allocations == 0u);
    REQUIRE(reportedBytes == 0u);
}

// ==============================================================================
// T012 (6) - SC-001 (d)'s TARGETED EDGES, and the plan S14 D-L REGRESSION
// ==============================================================================
// SC-001's random perturb schedule reaches these corners only by luck. Each arm
// below aims at one, and clause 3 is the D-L regression: FR-071's own closed
// formula, as spec.md still writes it, PAYS an agent inside the refuge out of an
// unguarded pool on a "take energy" call. At steady state the pool IS empty (the
// agents hold ~96 % of the budget), so that is the NORMAL case, not a corner,
// and SC-001 (d)'s getConservationViolationCount() == 0 would fail by
// construction.
//
// ON `==` RATHER THAN memcmp. perturbAgent's zero-delta giving branch produces
// `-0.0` (a negative `amount` times a floored-to-zero inner term), and
// `pool_ -= -0.0` would turn a `-0.0` pool into `+0.0`. That pair is the ONLY
// difference `==` cannot see and memcmp can, and it is not a change in energy.
// The arms are additionally arranged so the bit patterns are unchanged anyway:
// clause 2 drains the agent FIRST, which leaves the pool strictly positive.
//
// FALSIFICATION (run it, then restore): revert perturbAgent()'s negative branch
// to the spec's uncorrected `std::max(a * (energy_[i] - preyFloorAbs_),
// -energy_[i])`. Clause 3 must fail on BOTH its energy and its pool assertion,
// and every other clause here must stay green.
namespace {

/// @brief A settled engine at the defaults: 5000 steps at seed 0xC0FFEE.
[[nodiscard]] std::unique_ptr<Engine> t012SettledEngine() {
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());
    for (std::size_t s = 0; s < 5000u; ++s) {
        engine->processChunk(kDefaultSamplesPerStep);
    }
    return engine;
}

/// @brief FR-008's share -> absolute conversion, recomputed test-side.
///
/// Identical arithmetic to refreshDerivedScales() (`shares * energyBudget_ /
/// agentCount_`), so the two agree to the last bit and an "exactly at capacity"
/// agent can be IDENTIFIED rather than assumed.
[[nodiscard]] double t012AbsFromShares(const Engine& e, float shares) noexcept {
    const double meanShare = e.getEnergyBudget() / static_cast<double>(e.getAgentCount());
    return static_cast<double>(shares) * meanShare;
}

/// @brief The first agent still holding energy, or kSizeMax if none does.
///
/// The arms below must NOT hard-code an index: agents really do die at the
/// defaults (FINDINGS.md's terciles report 13-63 % alive depending on the
/// regime), and an arm aimed at a dead agent would assert its premise against a
/// zero and go red for a reason that has nothing to do with perturbAgent.
[[nodiscard]] std::size_t t012FirstLiveAgent(const Engine& e) noexcept {
    for (std::size_t i = 0; i < e.getAgentCount(); ++i) {
        if (e.getAgentEnergy(i) > 0.0) {
            return i;
        }
    }
    return kSizeMax;
}

}  // namespace

TEST_CASE("EcosystemEngine_PerturbAgentConservesUnderFuzz", "[ecosystem_engine]") {
    const std::unique_ptr<Engine> settled = t012SettledEngine();
    REQUIRE(settled->getControlStepCount() == 5000u);
    REQUIRE(settled->getConservationViolationCount() == 0u);

    // -------------------------------------------------------------------------
    // clause 1: an agent EXACTLY at capacity, +1.0f -> delta == 0
    // -------------------------------------------------------------------------
    // Driven there through the documented API rather than by probe injection:
    // capacityShares at its Appendix-A minimum puts capacityAbs_ (0.01 at the
    // defaults) below the steady-state per-agent energy (~0.03), and stage 8's
    // upper clamp then assigns `energy_[i] = capacityAbs_` EXACTLY.
    {
        auto engine = std::make_unique<Engine>(*settled);
        engine->setCapacityShares(0.32f);
        REQUIRE(engine->getCapacityShares() == 0.32f);
        engine->processChunk(kDefaultSamplesPerStep);

        const double capacityAbs = t012AbsFromShares(*engine, engine->getCapacityShares());
        const std::size_t agents = engine->getAgentCount();
        std::size_t target = kSizeMax;
        for (std::size_t i = 0; i < agents; ++i) {
            if (engine->getAgentEnergy(i) == capacityAbs) {
                target = i;
                break;
            }
        }
        INFO("capacityAbs " << capacityAbs << ", agent sitting exactly at capacity " << target);
        REQUIRE(target != kSizeMax);  // non-vacuity: the clamp really engaged

        const double totalBefore = engine->getTotalEnergy();
        const double poolBefore = engine->getPoolEnergy();
        const double energyBefore = engine->getAgentEnergy(target);
        const std::uint64_t violationsBefore = engine->getConservationViolationCount();

        engine->perturbAgent(target, 1.0f);

        REQUIRE(engine->getAgentEnergy(target) == energyBefore);
        REQUIRE(engine->getPoolEnergy() == poolBefore);
        REQUIRE(engine->getTotalEnergy() == totalBefore);
        REQUIRE(engine->getConservationViolationCount() == violationsBefore);
    }

    // -------------------------------------------------------------------------
    // clause 2: an agent at energy 0, -1.0f -> delta == 0
    // -------------------------------------------------------------------------
    // With preyFloorShares at 0 the giving branch is `max(-1 * e, -e) == -e`, so
    // ONE call lands the agent at exactly 0.0 and the SECOND is the arm under
    // test. Draining first is also what leaves the pool strictly positive, so
    // the `pool_ -= -0.0` below cannot even flip a sign bit.
    {
        auto engine = std::make_unique<Engine>(*settled);
        engine->setPreyFloorShares(0.0f);
        REQUIRE(engine->getPreyFloorShares() == 0.0f);

        const std::size_t kAgent = t012FirstLiveAgent(*engine);
        REQUIRE(kAgent != kSizeMax);
        REQUIRE(engine->getAgentEnergy(kAgent) > 0.0);
        engine->perturbAgent(kAgent, -1.0f);
        REQUIRE(engine->getAgentEnergy(kAgent) == 0.0);
        REQUIRE(engine->getPoolEnergy() > 0.0);

        const double totalBefore = engine->getTotalEnergy();
        const double poolBefore = engine->getPoolEnergy();
        const std::uint64_t violationsBefore = engine->getConservationViolationCount();

        engine->perturbAgent(kAgent, -1.0f);

        REQUIRE(engine->getAgentEnergy(kAgent) == 0.0);
        REQUIRE(engine->getPoolEnergy() == poolBefore);
        REQUIRE(engine->getTotalEnergy() == totalBefore);
        REQUIRE(engine->getConservationViolationCount() == violationsBefore);
    }

    // -------------------------------------------------------------------------
    // clause 3: THE D-L REGRESSION - an agent BELOW the refuge floor, amount < 0
    // -------------------------------------------------------------------------
    // preyFloorShares at its Appendix-A maximum puts preyFloorAbs_ (0.05 at the
    // defaults) ABOVE the steady-state per-agent energy (~0.03), so essentially
    // the whole population sits inside the refuge - precisely the regime D-L
    // calls "the normal case, not a corner".
    //
    // The uncorrected formula computes `max(-0.5 * (0.03 - 0.05), -0.03)` =
    // `max(+0.01, -0.03)` = +0.01: a TAKE call that PAYS the agent 0.01 out of
    // the pool. Total energy is conserved either way (the transfer is
    // antisymmetric), so the discriminating assertions are the AGENT's energy
    // and the POOL - not the total.
    {
        auto engine = std::make_unique<Engine>(*settled);
        engine->setPreyFloorShares(1.6f);
        REQUIRE(engine->getPreyFloorShares() == 1.6f);

        const double preyFloorAbs = t012AbsFromShares(*engine, engine->getPreyFloorShares());
        const std::size_t agents = engine->getAgentCount();
        std::size_t target = kSizeMax;
        for (std::size_t i = 0; i < agents; ++i) {
            const double e = engine->getAgentEnergy(i);
            if (e > 0.0 && e < preyFloorAbs) {
                target = i;
                break;
            }
        }
        INFO("preyFloorAbs " << preyFloorAbs << ", agent strictly inside the refuge " << target);
        REQUIRE(target != kSizeMax);  // non-vacuity: the regime really is reached

        const double totalBefore = engine->getTotalEnergy();
        const double poolBefore = engine->getPoolEnergy();
        const double energyBefore = engine->getAgentEnergy(target);
        const std::uint64_t violationsBefore = engine->getConservationViolationCount();

        engine->perturbAgent(target, -0.5f);

        // delta == 0.0 EXACTLY, read through the two quantities it would move.
        REQUIRE(engine->getAgentEnergy(target) == energyBefore);
        REQUIRE(engine->getPoolEnergy() == poolBefore);
        REQUIRE(engine->getTotalEnergy() == totalBefore);
        REQUIRE(engine->getConservationViolationCount() == violationsBefore);

        // The full-magnitude form of the same call, for good measure.
        engine->perturbAgent(target, -1.0f);
        REQUIRE(engine->getAgentEnergy(target) == energyBefore);
        REQUIRE(engine->getPoolEnergy() == poolBefore);
    }

    // -------------------------------------------------------------------------
    // clause 4: a NEGATIVE pool, amount > 0 -> pays nothing, deepens nothing
    // -------------------------------------------------------------------------
    // FR-056 makes a negative pool a readable, LATCHED signal that is never
    // clamped; FR-071's positive branch is bounded by `max(0, pool_)`, so a
    // "give energy" call against a pool in deficit must move exactly nothing.
    {
        auto engine = std::make_unique<Engine>(*settled);
        const double deficit = -0.25 * engine->getEnergyBudget();
        Probe::setPool(*engine, deficit);
        REQUIRE(engine->getPoolEnergy() == deficit);

        const std::size_t kAgent = t012FirstLiveAgent(*engine);
        REQUIRE(kAgent != kSizeMax);
        const double capacityAbs = t012AbsFromShares(*engine, engine->getCapacityShares());
        REQUIRE(engine->getAgentEnergy(kAgent) < capacityAbs);  // there IS headroom to fill

        const double totalBefore = engine->getTotalEnergy();
        const double energyBefore = engine->getAgentEnergy(kAgent);
        const std::uint64_t violationsBefore = engine->getConservationViolationCount();

        engine->perturbAgent(kAgent, 1.0f);

        REQUIRE(engine->getAgentEnergy(kAgent) == energyBefore);
        REQUIRE(engine->getPoolEnergy() == deficit);  // not deepened, not healed
        REQUIRE(engine->getTotalEnergy() == totalBefore);
        REQUIRE(engine->getConservationViolationCount() == violationsBefore);
    }

    // -------------------------------------------------------------------------
    // clause 5: an OUT-OF-RANGE index is a silent no-op (FR-064)
    // -------------------------------------------------------------------------
    {
        auto engine = std::make_unique<Engine>(*settled);
        const double totalBefore = engine->getTotalEnergy();
        const double poolBefore = engine->getPoolEnergy();
        const std::uint64_t violationsBefore = engine->getConservationViolationCount();
        const std::size_t agents = engine->getAgentCount();

        engine->perturbAgent(agents, 1.0f);
        engine->perturbAgent(agents + 1u, -1.0f);
        engine->perturbAgent(kSizeMax, 1.0f);
        engine->perturbAgent(Engine::kMaxAgents, -1.0f);

        REQUIRE(engine->getTotalEnergy() == totalBefore);
        REQUIRE(engine->getPoolEnergy() == poolBefore);
        REQUIRE(engine->getConservationViolationCount() == violationsBefore);
    }

    // -------------------------------------------------------------------------
    // clause 6: a NON-FINITE amount is rejected (FR-064 (1))
    // -------------------------------------------------------------------------
    // The values are built from BIT PATTERNS through a volatile sink.
    // std::numeric_limits<float>::quiet_NaN()/infinity() fold to finite garbage
    // under -ffast-math and would make this arm pass for the wrong reason. This
    // TU is not in the -fno-fast-math block, so the assertion is "the previous
    // value still stands" - never an IEEE claim about the injected value.
    {
        auto engine = std::make_unique<Engine>(*settled);
        const std::size_t kAgent = t012FirstLiveAgent(*engine);
        REQUIRE(kAgent != kSizeMax);
        const double totalBefore = engine->getTotalEnergy();
        const double poolBefore = engine->getPoolEnergy();
        const double energyBefore = engine->getAgentEnergy(kAgent);
        const std::uint64_t violationsBefore = engine->getConservationViolationCount();
        REQUIRE(energyBefore > 0.0);  // non-vacuity: there is something to lose

        constexpr std::array<std::uint32_t, 3> kPatterns{{
            0x7FC00000u,  // quiet NaN
            0x7F800000u,  // +Inf
            0xFF800000u,  // -Inf
        }};
        for (const std::uint32_t bits : kPatterns) {
            engine->perturbAgent(kAgent, t012MakeNonFinite(bits));
        }

        REQUIRE(engine->getAgentEnergy(kAgent) == energyBefore);
        REQUIRE(engine->getPoolEnergy() == poolBefore);
        REQUIRE(engine->getTotalEnergy() == totalBefore);
        REQUIRE(engine->getConservationViolationCount() == violationsBefore);
    }
}

// ==============================================================================
// T016 - SC-011 (c): the two exact identities adopted by the 2026-09-16 ruling
//        on SC-011's measured table (plan S12.3 E-1 / E-2, S14 D-P)
// ==============================================================================
// Neither lever is an approximation, and this case is what makes that claim
// checkable rather than asserted: both are driven on the ENGINE'S OWN member
// functions and state through the probe and compared with std::exp / std::sin
// evaluated on the same inputs. A test that re-derived the recurrence in this
// TU would prove the recurrence, not the header.
//
// E-1 tolerance: 1e-12 RELATIVE. A run of n consecutive cells accumulates ~2n
// ulp of multiplicative rounding (n <= 96 -> ~2e-14), plus the ~1e-16 by which
// a freshly computed wrapDelta(cellPos - x) differs from the previous d + h,
// amplified by |d|/sigma^2 <= 5.3e-14 inside the pre-test bound at sigma = 0.01.
// 1e-12 sits two decades above the worst of that and eight below anything the
// behaviour criteria can see (SC-002 carries +-30 %).
// E-2 tolerance: 1e-14 ABSOLUTE - four products and a subtraction of
// unit-magnitude terms.
//
// FALSIFICATION (run it, then restore): drop the `cellRatio_[i] *=
// cellRatioStep_` line of cellKernelWeight. The seed cell and the one after it
// still agree with std::exp; the third cell of every run is off by a factor
// exp(h^2/sigma^2) and the 96-cell / sigma 0.35 arm fails at ~1e-3.
TEST_CASE("EcosystemEngine_ExactIdentitiesMatchFormulas", "[ecosystem_engine]") {
    auto engine = std::make_unique<Engine>();

    SECTION("E-1: the cell-grid Gaussian recurrence equals exp(-d^2 / 2 sigma^2)") {
        // Every sigma the setter admits at both rails and two in between, and
        // cell counts from the degenerate single cell to the maximum. The 96 x
        // 0.35 arm is SC-011's worst case: every visit in range, every run the
        // full width of the torus, one wrap re-seed per agent.
        const std::array<float, 4> kSigmas{0.01f, 0.03f, 0.12f, 0.35f};
        const std::array<std::size_t, 5> kCells{1u, 2u, 7u, 64u, 96u};

        double worstRel = 0.0;
        std::size_t inRangeVisits = 0;
        std::size_t advancedVisits = 0;  // the multiply path, not the seed path

        for (const float sigma : kSigmas) {
            for (const std::size_t cells : kCells) {
                engine->setKernelSigma(sigma);
                engine->setSeed(0xC0FFEEu + static_cast<std::uint32_t>(cells));  // 48 seeded x per arm
                engine->prepare(48000.0, Engine::PrepareConfig{.agentCount = Engine::kMaxAgents,
                                                               .resourceCells = cells});
                REQUIRE(engine->getResourceCells() == cells);
                const double invTwoSigmaSq = Probe::invTwoSigmaSq(*engine);
                const double cutDistSq = Probe::cutDistSq(*engine);

                for (std::size_t i = 0; i < Engine::kMaxAgents; ++i) {
                    Probe::breakKernelRun(*engine, i);
                }
                // The SAME visit order and the SAME pre-test as stage 6.
                for (std::size_t k = 0; k < cells; ++k) {
                    const double cellX = Probe::cellPos(*engine, k);
                    for (std::size_t i = 0; i < Engine::kMaxAgents; ++i) {
                        const double d = Probe::wrapDelta(cellX - Probe::agentX(*engine, i));
                        const double d2 = d * d;
                        if (d2 > cutDistSq) {
                            Probe::breakKernelRun(*engine, i);
                            continue;
                        }
                        const bool wasSeeded = Probe::kernelRunSeeded(*engine, i);
                        const double w = Probe::cellKernelWeight(*engine, i, d, d2);
                        const double ref = std::exp(-d2 * invTwoSigmaSq);
                        REQUIRE(isFiniteD(w));
                        REQUIRE(ref > 0.0);
                        const double rel = std::abs(w - ref) / ref;
                        INFO("sigma " << sigma << ", cells " << cells << ", cell " << k
                                      << ", agent " << i << ", d " << d << ": w " << w
                                      << " vs exp " << ref << " (rel " << rel << ")");
                        REQUIRE(rel <= 1.0e-12);
                        worstRel = std::max(worstRel, rel);
                        ++inRangeVisits;
                        if (wasSeeded) {
                            ++advancedVisits;
                        }
                    }
                }
            }
        }
        INFO("in-range visits " << inRangeVisits << ", of which advanced by the recurrence "
                                << advancedVisits << "; worst relative error " << worstRel);
        // Non-vacuity, both halves: the seed path AND the multiply path ran,
        // and the multiply path ran at scale (the 96-cell / sigma 0.35 arm
        // alone advances 48 agents across ~94 cells each).
        REQUIRE(inRangeVisits > advancedVisits);
        REQUIRE(advancedVisits >= 4000u);
        REQUIRE(worstRel <= 1.0e-12);
    }

    SECTION("E-2: sin(2 pi (phase_j - phase_i)) by the per-agent sin/cos table") {
        engine->setSyncRate(0.5f);  // the cosines are filled only while sync is on
        engine->setSeed(0xBEEF01u);
        engine->prepare(48000.0, Engine::PrepareConfig{.agentCount = Engine::kMaxAgents});
        Probe::refreshPhaseTrig(*engine);

        double worstAbs = 0.0;
        std::size_t pairs = 0;
        for (std::size_t i = 0; i < Engine::kMaxAgents; ++i) {
            const double phaseI = engine->getAgentPhase(i);
            // Stage 4's appetite term is the table entry itself.
            REQUIRE(Probe::sinPhase(*engine, i) == std::sin(Engine::kTwoPi * phaseI));
            for (std::size_t j = i + 1u; j < Engine::kMaxAgents; ++j) {
                const double ref =
                    std::sin(Engine::kTwoPi * (engine->getAgentPhase(j) - phaseI));
                const double s = Probe::pairPhaseSine(*engine, i, j);
                const double err = std::abs(s - ref);
                INFO("pair (" << i << ", " << j << "): identity " << s << " vs std::sin " << ref);
                REQUIRE(err <= 1.0e-14);
                worstAbs = std::max(worstAbs, err);
                ++pairs;
            }
        }
        INFO("pairs " << pairs << ", worst absolute error " << worstAbs);
        REQUIRE(pairs == Engine::kMaxPairs);
        REQUIRE(worstAbs <= 1.0e-14);
    }
}

// ==============================================================================
// T018 - SC-002 (a)-(d) and SC-004 (a)(b): THE DEFAULTS GATE
// ==============================================================================
// Spec:  spec.md SC-002 ("the defaults gate", three seeds, 1800 s) and SC-004.
// Plan:  plan.md S10.4 (the five rows), S10.2 (the metrics), S10.5 (~18 s total).
// Tasks: tasks.md T018.
//
// THESE ARE NOT THE VERDICT FUNCTION. Eco::verdictAlive (activity >= 0.10,
// <= 25 % frozen) classifies ARBITRARY configurations for the fuzz batches. The
// gate below is strictly tighter by design - activity >= 0.30, frozen == 0
// EXACTLY - because roadmap line 394 says "no frozen fixed points" and an
// implementation landing at 0.11 with 12 of 48 agents permanently frozen would
// satisfy the verdict function while contradicting the roadmap outright.
//
// EVERY PROTOTYPE FIGURE QUOTED BELOW IS A COMPARISON BASELINE, NOT A TARGET
// (spec.md SC-002's note, Clarification Q6). FR-011's kind draw is STRATIFIED -
// dealt round-robin and shuffled - while ecosystem-sim.js drew each agent's kind
// i.i.d., so the per-kind histogram this engine produces is not the histogram any
// prototype number was measured on. That is why every case here prints its own
// measured figure AND the histogram it was measured under. A measured value that
// misses a threshold is a FINDING TO SURFACE under FR-085's stop-and-surface rule,
// with the histogram attached - never a threshold to move.
//
// FALSIFICATION (run each, then restore) - the mutation that must make the new
// assertion fail, per case:
//   LateWindowLiveness           -> setLeakRate(0.0f) and setForageRate(0.0f)
//                                   before prepare(): nothing drives the economy,
//                                   activity collapses toward 0 and lateFrozen
//                                   climbs off 0.
//   AgentsDoNotCollapseSpatially -> the control arm IS the falsification, and it
//                                   is asserted, not printed: with crowding = 0
//                                   the population must FAIL the same bound the
//                                   defaults arm passes.
//   OutputsAreNotRailed          -> read getAgentClampedStepFraction(i) once at
//                                   the end instead of differencing the two
//                                   boundary readings; a late-only railing is then
//                                   diluted by the 1200 s that precede the window
//                                   and the case stops discriminating.
//   AgentsDecorrelate            -> setKernelSigma(0.35f) (one habitat-wide
//                                   resource pool, every agent grazing the same
//                                   field): the failure regime FINDINGS.md:62-63
//                                   measured at |corr| 0.84-0.99.
//   SeedsProduceDifferentVoices  -> drive all 8 runs from ONE seed: the cross-seed
//                                   mean rises toward 1.0 while the within-run
//                                   floor is unchanged, so the relative bound fails.
//
// SC-015's header arm - EcosystemEngine_HeaderIncludesOnlyLayerZero, written in
// T003 at the top of this TU - is the sixth case T018 owns; it needs no new code
// here, only that it still passes after every header edit of this phase.
// ==============================================================================

namespace {

namespace Eco = Krate::DSP::TestUtils::Eco;

/// SC-002's three seeds (plan S10.4). Fixed, not drawn: the gate is a statement
/// about THESE runs, and a seed that drifts between builds turns a red into a
/// coin flip.
constexpr std::array<std::uint32_t, 3> kLivenessSeeds{0xC0FFEEu, 0xC0FFEFu, 0x5EEDu};

/// The defaults-gate run length. The late window is the last kLateWindowSeconds
/// (600 s) of it, so the 1800 s figure buys a 1200 s settling head.
constexpr double kDefaultsRunSeconds = 1800.0;

/// SC-004 (b)'s duration - run.js:572's own 900 s, the length its 0.13 / 0.12
/// reference pair was measured at.
constexpr double kSeedRunSeconds = 900.0;

/// The requested sample grid. Eco::runTrace reports the grid it ACTUALLY used
/// (94 control steps = 0.99734 Hz at the defaults), and every window is sized from
/// that, so the numbers mean the same duration either way.
constexpr double kSampleHz = 1.0;

/// SC-002 (c)'s bound: distinct positions >= 0.8 x agentCount.
constexpr double kSpatialFraction = 0.8;

/// SC-004 (b)'s multiple of the within-run floor (run.js:622, kSeedBlindFactor).
constexpr double kSeedBlindFactor = 1.5;

/// @brief Advance a PREPARED engine by a whole number of control steps covering
///        @p seconds, in a single processChunk call.
///
/// Used where only the END state is read (SC-002 (c)'s positions, SC-002 (d)'s two
/// boundary readings) so the case does not pay for a trace it never looks at. One
/// call rather than many is exact, not approximate: processChunk's two residues
/// live across calls and the step count after N advanced samples is a function of N
/// alone (ecosystem_engine.h:400-412, SC-008).
void advanceSeconds(Engine& e, double seconds) noexcept {
    const double stepSeconds = e.getStepDurationSeconds();
    if (!e.isPrepared() || !(stepSeconds > 0.0) || !(seconds > 0.0)) {
        return;
    }
    const long long steps = std::llround(seconds / stepSeconds);
    if (steps < 1) {
        return;
    }
    e.processChunk(static_cast<std::size_t>(steps) * e.getStepIntervalChunks() *
                   Engine::kControlChunkSamples);
}

/// @brief The five per-kind counts as P/R/N/F/G, for the printed report.
///
/// Attached to every measured figure because FR-011's stratified deal is a
/// deliberate deviation from the prototype (spec Clarification Q6): a deviation
/// surfaced WITHOUT the histogram it was measured under cannot be diagnosed.
[[nodiscard]] std::string kindHistogram(const Engine& e) {
    std::string out;
    for (std::size_t k = 0; k < Engine::kNumKinds; ++k) {
        if (k != 0u) {
            out += "/";
        }
        out += std::to_string(
            e.getAgentCountOfKind(static_cast<Engine::Kind>(static_cast<std::uint8_t>(k))));
    }
    return out;
}

/// @brief Transpose a trace's energy rows into one series per agent.
///
/// Trace::energy is row-major BY SAMPLE (energy[sample][agent], matching the
/// prototype's agentSeries), and every correlation here is between two AGENTS'
/// series, so the transpose happens once per run rather than inside the O(n^2)
/// pair loop.
[[nodiscard]] std::vector<std::vector<double>> energyColumns(const Eco::Trace& trace) {
    const std::size_t samples = trace.energy.size();
    std::vector<std::vector<double>> cols(trace.agents, std::vector<double>(samples, 0.0));
    for (std::size_t s = 0; s < samples; ++s) {
        const std::vector<double>& row = trace.energy[s];
        const std::size_t width = std::min(trace.agents, row.size());
        for (std::size_t i = 0; i < width; ++i) {
            cols[i][s] = row[i];
        }
    }
    return cols;
}

}  // namespace

// ==============================================================================
// SC-002 (a) + (b): the late window is ALIVE, and NOTHING is frozen
// ==============================================================================
// 1800 s, three seeds, the shipped defaults; all three must pass.
//   (a) lateActivity >= 0.30   - prototype 0.44 (FINDINGS.md:252), so the gate is
//                                a ~30 % margin for toolchain and sampling
//                                variation, NOT a re-derivation of the prototype.
//   (b) lateFrozen  == 0       - EXACTLY zero, prototype 0 of 32. Not the verdict
//                                function's 25 %: "no frozen fixed points" is the
//                                roadmap's words, and a population with one
//                                permanently frozen agent has one.
//
// Both statistics are computed on ENERGY - the conserved quantity every rule acts
// on, and the quantity every prototype figure was measured on. The bridge to the
// value a consumer actually sees is SC-002 (d), two cases below.
//
// MEASURED (C++, stratified kinds): each seed's activity, frozen count and per-kind
// histogram are printed by this case's own WARN line and transcribed into the
// compliance table from the captured log. A miss is surfaced under FR-085 with the
// histogram attached, never absorbed by widening the gate toward the verdict
// function's 0.10 / 25 %.
TEST_CASE("EcosystemEngine_LateWindowLiveness", "[ecosystem_engine]") {
    auto engine = std::make_unique<Engine>();

    for (const std::uint32_t seed : kLivenessSeeds) {
        INFO("seed = " << seed);

        engine->setSeed(seed);
        engine->prepare(48000.0, defaultConfig());
        REQUIRE(engine->getAgentCount() == defaultConfig().agentCount);

        const Eco::Trace trace = Eco::runTrace(*engine, kDefaultsRunSeconds, kSampleHz);
        REQUIRE(trace.agents == engine->getAgentCount());
        // Non-vacuity: the late window must actually fit inside the trace, or
        // lateWindow() would silently measure the whole (shorter) run instead.
        REQUIRE(static_cast<double>(trace.energy.size()) >
                Eco::kLateWindowSeconds * trace.sampleHz);

        const Eco::Liveness live = Eco::lateWindow(trace, Eco::kLateWindowSeconds);

        WARN("SC-002 (a)(b) seed " << seed << ": lateActivity " << live.lateActivity
                                   << " (gate >= 0.30, prototype 0.44), lateFrozen "
                                   << live.lateFrozen << " of " << trace.agents
                                   << " (gate == 0, prototype 0), latePairCorr "
                                   << live.latePairCorr << ", kinds P/R/N/F/G "
                                   << kindHistogram(*engine) << ", grid " << trace.sampleHz
                                   << " Hz x " << trace.energy.size() << " samples");

        REQUIRE(live.lateActivity >= 0.30);
        REQUIRE(live.lateFrozen == 0u);
    }
}

// ==============================================================================
// SC-002 (c): the agents do not collapse onto one another
// ==============================================================================
// distinctPositions(engine, 2) >= 0.8 x agentCount after 1800 s (prototype 31 of
// 32), PLUS a crowding = 0 control arm asserted to FAIL that same bound
// (prototype 6 of 32, FINDINGS.md:210-214).
//
// THE CONTROL ARM IS MANDATORY, and the reason is a measurement rather than a
// preference: the ablation prices FR-032's removal at -2 % activity and -2 %
// correlation, "within noise in 2-D" (FINDINGS.md:274), so SC-002 (a) and SC-004
// PROVABLY cannot discriminate crowding repulsion. A positional metric is the only
// thing that can, and a positional metric that is not shown to fail when the rule
// is removed is an assumption, not a test.
//
// Both arms share seed 0xC0FFEE and differ in exactly one knob, so the comparison
// is an ablation rather than two unrelated runs. setCrowding BEFORE prepare() is
// deliberate: prepare() re-derives STATE, never configuration (FR-064 holds
// unprepared, plan S8 (iii)), which the getter assertions below pin.
TEST_CASE("EcosystemEngine_AgentsDoNotCollapseSpatially", "[ecosystem_engine]") {
    const std::size_t agentCount = defaultConfig().agentCount;
    const double bound = kSpatialFraction * static_cast<double>(agentCount);

    // ---- arm 1: the shipped defaults (crowding at its 0.05 default) ----------
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());
    REQUIRE(engine->getCrowding() == 0.05f);  // the Appendix-A default, unaltered
    REQUIRE(engine->getAgentCount() == agentCount);
    advanceSeconds(*engine, kDefaultsRunSeconds);
    const std::size_t distinctWithCrowding = Eco::distinctPositions(*engine, 2);

    // ---- arm 2: FR-032 removed ----------------------------------------------
    auto control = std::make_unique<Engine>();
    control->setCrowding(0.0f);
    control->setSeed(0xC0FFEEu);
    control->prepare(48000.0, defaultConfig());
    REQUIRE(control->getCrowding() == 0.0f);  // FR-064 (2): the knob survived prepare()
    REQUIRE(control->getAgentCount() == agentCount);
    advanceSeconds(*control, kDefaultsRunSeconds);
    const std::size_t distinctWithoutCrowding = Eco::distinctPositions(*control, 2);

    WARN("SC-002 (c): distinct positions (2 dp) after "
         << kDefaultsRunSeconds << " s - defaults " << distinctWithCrowding << " of "
         << agentCount << " (gate >= " << bound << ", prototype 31), crowding = 0 control "
         << distinctWithoutCrowding << " of " << agentCount
         << " (must FAIL that gate, prototype 6), kinds P/R/N/F/G " << kindHistogram(*engine));

    REQUIRE(static_cast<double>(distinctWithCrowding) >= bound);
    // The discriminating half: remove the rule and the metric must notice.
    REQUIRE(static_cast<double>(distinctWithoutCrowding) < bound);
}

// ==============================================================================
// SC-002 (d): the published outputs are not railed - WINDOWED, NOT CUMULATIVE
// ==============================================================================
// WHY THE CUMULATIVE READING IS WRONG, not merely less precise.
// getAgentClampedStepFraction(i) divides by getControlStepCount(), which counts
// steps since prepare() (ecosystem_engine.h:972-986). Reading it once at the end of
// an 1800 s run therefore reports the fraction over the WHOLE run, not over the
// 600 s window this criterion names. That errs in both directions, and the second
// one is fatal: it judges an implementation that rails only during the early
// transient too harshly, and it DILUTES one that rails only late - which is
// precisely the implementation (a) cannot see and (d) exists to catch.
//
// So the fraction is differenced between the two late-window boundaries, from
// InspectProbe::clampedSteps(i) (the per-agent tally the public getter divides) and
// getControlStepCount():
//     frac_i = (clamped_end - clamped_start) / (steps_end - steps_start)
// Mean over agents <= 0.05; no single agent > 0.25.
//
// WHY THIS CLAUSE EXISTS AT ALL. (a)-(c) are stated on raw energy e_i because that
// is the conserved quantity every rule acts on, but the only value a consumer ever
// sees is FR-061's clamped output, which saturates at twice the mean share. Without
// (d), a configuration whose PUBLISHED outputs are constant could pass (a) while
// std(e_i)/grandMean reported its railed agent as the liveliest in the run.
//
// MEASURED (C++, stratified kinds): mean = <transcribe from this case's WARN line>,
// worst agent = <transcribe from this case's WARN line>. The compliance pass fills
// both from the captured defaults run; a value above either threshold is a finding
// to surface under FR-085, not a threshold to move.
TEST_CASE("EcosystemEngine_OutputsAreNotRailed", "[ecosystem_engine]") {
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());

    const std::size_t agents = engine->getAgentCount();
    REQUIRE(agents == defaultConfig().agentCount);

    // ---- boundary 1: the start of the late window ---------------------------
    advanceSeconds(*engine, kDefaultsRunSeconds - Eco::kLateWindowSeconds);
    const std::uint64_t stepsStart = engine->getControlStepCount();
    std::vector<std::uint64_t> clampedStart(agents, 0u);
    for (std::size_t i = 0; i < agents; ++i) {
        clampedStart[i] = Probe::clampedSteps(*engine, i);
    }
    REQUIRE(stepsStart > 0u);

    // ---- boundary 2: the end of the run -------------------------------------
    advanceSeconds(*engine, Eco::kLateWindowSeconds);
    const std::uint64_t stepsEnd = engine->getControlStepCount();
    REQUIRE(stepsEnd > stepsStart);
    const double windowSteps = static_cast<double>(stepsEnd - stepsStart);

    double fractionSum = 0.0;
    double worstFraction = 0.0;
    std::size_t worstAgent = 0;
    for (std::size_t i = 0; i < agents; ++i) {
        const std::uint64_t clampedEnd = Probe::clampedSteps(*engine, i);
        INFO("agent " << i);
        // The tally is monotone; a decrease would mean the counter was cleared
        // mid-run and the difference below would be meaningless.
        REQUIRE(clampedEnd >= clampedStart[i]);
        const double fraction = static_cast<double>(clampedEnd - clampedStart[i]) / windowSteps;
        fractionSum += fraction;
        if (fraction > worstFraction) {
            worstFraction = fraction;
            worstAgent = i;
        }
    }
    const double meanFraction = fractionSum / static_cast<double>(agents);

    WARN("SC-002 (d): windowed clamp fraction over the last "
         << Eco::kLateWindowSeconds << " s (" << windowSteps << " control steps of "
         << stepsEnd << ") - mean " << meanFraction << " (gate <= 0.05), worst agent "
         << worstAgent << " at " << worstFraction << " (gate <= 0.25), kinds P/R/N/F/G "
         << kindHistogram(*engine));

    REQUIRE(meanFraction <= 0.05);
    REQUIRE(worstFraction <= 0.25);
}

// ==============================================================================
// SC-004 (a): the agents are a bank, not one signal copied
// ==============================================================================
// 1800 s at the defaults, last 600 s: mean pairwise |corr(e_i, e_j)| <= 0.35.
// Prototype 0.18 (FINDINGS.md:252). The single-global-pool failure regime this
// guards against measured 0.84-0.99 (FINDINGS.md:62-63), so the gate sits roughly
// midway between the passing and the failing population rather than on a hair.
//
// On ENERGY, over the same window and the same grid as SC-002 (a)(b), so the three
// figures in this phase's report are commensurable.
TEST_CASE("EcosystemEngine_AgentsDecorrelate", "[ecosystem_engine]") {
    auto engine = std::make_unique<Engine>();
    engine->setSeed(0xC0FFEEu);
    engine->prepare(48000.0, defaultConfig());

    const Eco::Trace trace = Eco::runTrace(*engine, kDefaultsRunSeconds, kSampleHz);
    REQUIRE(trace.agents == engine->getAgentCount());
    REQUIRE(trace.agents > 1u);  // non-vacuity: a one-agent run has no pairs
    REQUIRE(static_cast<double>(trace.energy.size()) >
            Eco::kLateWindowSeconds * trace.sampleHz);

    const Eco::Liveness live = Eco::lateWindow(trace, Eco::kLateWindowSeconds);

    WARN("SC-004 (a): mean pairwise |corr(e_i, e_j)| over the last "
         << Eco::kLateWindowSeconds << " s = " << live.latePairCorr
         << " (gate <= 0.35, prototype 0.18, failure regime 0.84-0.99), lateActivity "
         << live.lateActivity << ", kinds P/R/N/F/G " << kindHistogram(*engine));

    REQUIRE(live.latePairCorr <= 0.35);
}

// ==============================================================================
// SC-004 (b): seeds produce different voices - RELATIVE, NEVER ABSOLUTE
// ==============================================================================
// THE ABSOLUTE FORM IS FORBIDDEN BY NAME (Clarification Q7, FINDINGS.md:130-145).
// These signals decorrelate over ~150 s, so a 900 s window holds only a handful of
// INDEPENDENT samples and two genuinely unrelated slow signals correlate high by
// chance. Judged against a fixed 0.5 threshold, the PASSING configuration was
// declared "SEED-BLIND". The round-1 figures that produced that verdict - 0.61 at
// 900 s and 0.50 at 3600 s (run.js:600-604) - ARE STRUCK from this criterion: they
// were measured under no-longer-shipped defaults and do not apply to the 1.5x gate.
//
// The reference is therefore the noise floor of this very system: the mean
// |corr(e_i, e_j)| between DIFFERENT AGENTS of the seed-0 run, which are as
// unrelated as two signals here ever get. Computed over the FULL 900 s (not the
// last-600-s window clause (a) uses), on the same ~1 Hz grid, on ENERGIES - not
// output, not entropy.
//
// Cross-seed: for each pair of runs, the mean over agents of |corr| between the
// SAME agent index in the two runs (run.js:587-599 - agent i drives target i in
// both voices, so the agent-wise comparison is the one that answers "would these
// two voices sound alike?"). Averaged over all 28 seed pairs, that must be
// <= 1.5 x the within-run floor. Reference at the shipped defaults: 0.13 against a
// floor of 0.12 - "a comparison of two noisy estimates should not flip on a hair",
// which is exactly why the bound is a MULTIPLE and not a difference.
//
// The eight seeds are drawn from a meta-RNG seeded 0x5EED, as run.js:576 draws
// them, so the C++ batch is comparable to the prototype's.
TEST_CASE("EcosystemEngine_SeedsProduceDifferentVoices", "[ecosystem_engine]") {
    constexpr std::size_t kRunCount = 8u;

    Krate::DSP::Xorshift32 meta(0x5EEDu);
    auto engine = std::make_unique<Engine>();

    // runs[r][agent] = that agent's energy series over the FULL 900 s.
    std::vector<std::vector<std::vector<double>>> runs;
    runs.reserve(kRunCount);
    std::size_t agents = 0;
    std::size_t samples = 0;
    std::string firstHistogram;

    for (std::size_t r = 0; r < kRunCount; ++r) {
        INFO("run " << r);
        engine->setSeed(meta.next());
        engine->prepare(48000.0, defaultConfig());
        const Eco::Trace trace = Eco::runTrace(*engine, kSeedRunSeconds, kSampleHz);
        REQUIRE(trace.agents == engine->getAgentCount());
        REQUIRE(trace.energy.size() > 2u);
        if (r == 0u) {
            agents = trace.agents;
            samples = trace.energy.size();
            firstHistogram = kindHistogram(*engine);
        }
        // Every run must share the grid, or the correlations are not comparable.
        REQUIRE(trace.agents == agents);
        REQUIRE(trace.energy.size() == samples);
        runs.push_back(energyColumns(trace));
    }
    REQUIRE(agents > 1u);

    // ---- the within-run floor: different agents of the seed-0 run -----------
    double floorSum = 0.0;
    std::size_t floorPairs = 0;
    for (std::size_t a = 0; a < agents; ++a) {
        for (std::size_t b = a + 1u; b < agents; ++b) {
            floorSum += std::abs(Eco::pearsonD(runs[0][a], runs[0][b]));
            ++floorPairs;
        }
    }
    REQUIRE(floorPairs > 0u);
    const double withinRunFloor = floorSum / static_cast<double>(floorPairs);

    // ---- cross-seed: the same agent index across two runs --------------------
    double crossSum = 0.0;
    std::size_t crossPairs = 0;
    double worstRunPair = 0.0;
    for (std::size_t i = 0; i < kRunCount; ++i) {
        for (std::size_t j = i + 1u; j < kRunCount; ++j) {
            double perAgent = 0.0;
            for (std::size_t a = 0; a < agents; ++a) {
                perAgent += std::abs(Eco::pearsonD(runs[i][a], runs[j][a]));
            }
            perAgent /= static_cast<double>(agents);
            crossSum += perAgent;
            ++crossPairs;
            worstRunPair = std::max(worstRunPair, perAgent);
        }
    }
    REQUIRE(crossPairs > 0u);
    const double crossSeed = crossSum / static_cast<double>(crossPairs);

    WARN("SC-004 (b): " << kRunCount << " seeds x " << kSeedRunSeconds << " s (" << samples
                        << " samples), cross-seed per-agent |corr| " << crossSeed
                        << " vs within-run floor " << withinRunFloor << " (gate <= "
                        << kSeedBlindFactor << "x floor = "
                        << (kSeedBlindFactor * withinRunFloor)
                        << ", reference 0.13 vs 0.12); worst run pair " << worstRunPair
                        << ", seed-0 kinds P/R/N/F/G " << firstHistogram);

    // A zero floor would make the bound vacuously 0 and the comparison
    // meaningless - the reference is an ESTIMATE from the same short window, and
    // an estimate of exactly zero means the transpose or the grid is broken.
    REQUIRE(withinRunFloor > 0.0);
    REQUIRE(crossSeed <= kSeedBlindFactor * withinRunFloor);
}
