// ==============================================================================
// Layer 3: System Tests - EcosystemEngine non-finite hygiene (SC-009)
//                              (specs/vorago-phase8-ecosystem)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase8-ecosystem/spec.md   (SC-009, FR-064, FR-083)
//            specs/vorago-phase8-ecosystem/plan.md   (S10.1 TU assignment, S7)
//            specs/vorago-phase8-ecosystem/tasks.md  (T001 creates this stub,
//                                                     T007 lands SC-009 (a)+(c),
//                                                     T014 lands SC-009 (b) and
//                                                     the NonFiniteProbe)
//
// Criteria owned (plan S10.1): SC-009 only. This is the ONLY one of the four
// Phase 8 TUs compiled -fno-fast-math (T001's second CMake edit), so it is the
// only place IEEE semantics may be asserted on a NaN/Inf. Non-finite values are
// built from bit patterns through a volatile sink - std::numeric_limits folds to
// finite garbage under -ffast-math on the macOS/Linux legs. This TU is also the
// only definition of Krate::DSP::detail::EcosystemEngineNonFiniteProbe (T014).
//
// WHY THIS IS A REAL TRACE AND NOT A FORMALITY. std::clamp does NOT reject NaN:
//   with v = NaN both `v < lo` and `hi < v` are false, so v is returned
//   UNCHANGED. A clamp-only setter therefore admits NaN straight into
//   configuration state, from where it reaches the step. Two named routes:
//     setKernelSigma(NaN) -> kernelSigma_ = NaN -> sigmaSq_ / twoSigmaSq_ /
//       cutDistSq_ all NaN (ecosystem_engine.h:1145-1152) -> every pair-pass
//       distance pre-test `d2 > cutDistSq_` is false, every weight is NaN, and
//       the whole exchange pass writes NaN energies;
//     PrepareConfig{.energyBudget = 0.0} with the kMinEnergyBudget clamp removed
//       -> FR-061's publication divides by energyBudget_
//       (ecosystem_engine.h:1333-1334) -> every published output is Inf or NaN,
//       reachable through documented API with no non-finite input at all.
//   The remedy is REJECTION for the runtime setters (the PREVIOUS value stands,
//   ecosystem_engine.h:438-444) and SANITISE-THEN-FLOOR / CLAMP for prepare()'s
//   arguments, which have no previous value to fall back on
//   (ecosystem_engine.h:312-321).
//
// FINITENESS IS READ WITH Krate::DSP::detail::isFinite (core/db_utils.h:118 for
//   float, :125-129 for double), which inspects the IEEE-754 exponent field
//   through an optimisation barrier - never std::isnan / std::isinf /
//   std::isfinite, which fold away in the same place the injected value would
//   (tools/lint-nonfinite-symbols.js gates this, FR-083).
// ==============================================================================

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/ecosystem_engine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

using Krate::DSP::detail::isFinite;

namespace {

using Engine = Krate::DSP::EcosystemEngine;
using Kind = Engine::Kind;

// =============================================================================
// Non-finite construction (never std::numeric_limits) - shared with T014
// =============================================================================

struct NonFinitePattern {
    const char* name;
    std::uint32_t bits;
};

/// The three binary32 patterns, named once rather than spelled at every
/// injection site.
constexpr std::array<NonFinitePattern, 3> kPatterns{{
    {"quiet NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

struct NonFinitePattern64 {
    const char* name;
    std::uint64_t bits;
};

/// The binary64 twins, for prepare()'s `double sampleRate` and the two `double`
/// PrepareConfig fields. Same order.
constexpr std::array<NonFinitePattern64, 3> kPatterns64{{
    {"quiet NaN", 0x7FF8000000000000ULL},
    {"+Inf", 0x7FF0000000000000ULL},
    {"-Inf", 0xFFF0000000000000ULL},
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time, which is how a -ffast-math build
/// turns an "infinity" literal into a finite number. Idiom copied verbatim from
/// dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief The binary64 twin of makeNonFinite, for prepare()'s double arguments.
[[nodiscard]] double makeNonFiniteDouble(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    const std::uint64_t materialized = sink;
    double out = 0.0;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

// =============================================================================
// The Appendix-A knob table
// =============================================================================

/// @brief One Appendix-A knob: how to drive it, how to read it, and the
///        in-range value this case parks it at before injecting.
///
/// The accessors are captureless lambdas converted to plain function pointers,
/// exactly as in ecosystem_engine_test.cpp's SettersClampToRange table, so the
/// whole table is a POD with no heap term.
struct KnobRow {
    const char* name;
    void (*set)(Engine&, float);
    float (*get)(const Engine&);
    float settled;  ///< in-range AND deliberately != the S1.5 default
};

/// 22 scalar rows. The 23rd rule knob is the affinity matrix, which takes two
/// Kind arguments and is driven separately below.
constexpr std::size_t kKnobCount = 22u;

// `settled` is in range but DIFFERENT from the member-initialised default
// (ecosystem_engine.h:1376-1398). That is what gives the case teeth against a
// setter that "rejects" by substituting a neutral rather than by leaving the
// previous value in place: a substituting setter would report the default, not
// `settled`, and only a NaN-storing setter is caught by the == alone.
const std::array<KnobRow, kKnobCount> kKnobTable{{
    {"kernelSigma", [](Engine& e, float v) { e.setKernelSigma(v); },
     [](const Engine& e) { return e.getKernelSigma(); }, 0.12f},
    {"exchangeRate", [](Engine& e, float v) { e.setExchangeRate(v); },
     [](const Engine& e) { return e.getExchangeRate(); }, 1.0f},
    {"predation", [](Engine& e, float v) { e.setPredation(v); },
     [](const Engine& e) { return e.getPredation(); }, 0.30f},
    {"preyFloorShares", [](Engine& e, float v) { e.setPreyFloorShares(v); },
     [](const Engine& e) { return e.getPreyFloorShares(); }, 0.9f},
    {"capacityShares", [](Engine& e, float v) { e.setCapacityShares(v); },
     [](const Engine& e) { return e.getCapacityShares(); }, 16.0f},
    {"leakRate", [](Engine& e, float v) { e.setLeakRate(v); },
     [](const Engine& e) { return e.getLeakRate(); }, 0.20f},
    {"leakExponent", [](Engine& e, float v) { e.setLeakExponent(v); },
     [](const Engine& e) { return e.getLeakExponent(); }, 1.2f},
    {"moveRate", [](Engine& e, float v) { e.setMoveRate(v); },
     [](const Engine& e) { return e.getMoveRate(); }, 0.35f},
    {"maxSpeed", [](Engine& e, float v) { e.setMaxSpeed(v); },
     [](const Engine& e) { return e.getMaxSpeed(); }, 0.012f},
    {"forageRate", [](Engine& e, float v) { e.setForageRate(v); },
     [](const Engine& e) { return e.getForageRate(); }, 0.02f},
    {"crowding", [](Engine& e, float v) { e.setCrowding(v); },
     [](const Engine& e) { return e.getCrowding(); }, 0.12f},
    {"crowdingRadius", [](Engine& e, float v) { e.setCrowdingRadius(v); },
     [](const Engine& e) { return e.getCrowdingRadius(); }, 0.035f},
    {"syncRate", [](Engine& e, float v) { e.setSyncRate(v); },
     [](const Engine& e) { return e.getSyncRate(); }, 0.1f},
    {"cellCapacityShares", [](Engine& e, float v) { e.setCellCapacityShares(v); },
     [](const Engine& e) { return e.getCellCapacityShares(); }, 6.4f},
    {"regenRate", [](Engine& e, float v) { e.setRegenRate(v); },
     [](const Engine& e) { return e.getRegenRate(); }, 0.30f},
    {"grazeRate", [](Engine& e, float v) { e.setGrazeRate(v); },
     [](const Engine& e) { return e.getGrazeRate(); }, 1.5f},
    {"feedRate", [](Engine& e, float v) { e.setFeedRate(v); },
     [](const Engine& e) { return e.getFeedRate(); }, 0.25f},
    {"satiationShares", [](Engine& e, float v) { e.setSatiationShares(v); },
     [](const Engine& e) { return e.getSatiationShares(); }, 4.0f},
    {"appetiteDepth", [](Engine& e, float v) { e.setAppetiteDepth(v); },
     [](const Engine& e) { return e.getAppetiteDepth(); }, 0.4f},
    // The two frequency ends are driven through the ONE paired setter, each
    // holding the other end at its current value - that is the only way a
    // caller can move one end at all (ecosystem_engine.h:637-653).
    {"freqLoHz", [](Engine& e, float v) { e.setFreqRangeHz(v, e.getFreqHiHz()); },
     [](const Engine& e) { return e.getFreqLoHz(); }, 0.0035f},
    {"freqHiHz", [](Engine& e, float v) { e.setFreqRangeHz(e.getFreqLoHz(), v); },
     [](const Engine& e) { return e.getFreqHiHz(); }, 0.030f},
    {"freqDrift", [](Engine& e, float v) { e.setFreqDrift(v); },
     [](const Engine& e) { return e.getFreqDrift(); }, 0.00012f},
}};

/// @brief One affinity entry, settled at a value that is neither the Appendix-A
///        default nor a rail.
struct AffinityRow {
    const char* name;
    Kind from;
    Kind to;
    float settled;
};

constexpr std::size_t kAffinityCount = 4u;

constexpr std::array<AffinityRow, kAffinityCount> kAffinityTable{{
    {"Partial->Partial (diagonal)", Kind::Partial, Kind::Partial, -0.75f},
    {"Partial->Resonator", Kind::Partial, Kind::Resonator, 0.75f},
    {"Noise->Feedback", Kind::Noise, Kind::Feedback, 1.25f},
    {"Ghost->Partial", Kind::Ghost, Kind::Partial, -1.5f},
}};

// =============================================================================
// Shared drivers
// =============================================================================

/// The agent index every per-agent probe writes to. Any index < agentCount
/// works; pinning one keeps the failure message unambiguous.
constexpr std::size_t kProbeAgent = 3u;

/// @brief Advance exactly @p steps control steps, whatever the CLAMPED grid is.
///
/// The step length is read back from the engine rather than assumed, because
/// every arm of DivisorKnobExtremes drives stepIntervalChunks and the sample
/// rate to a bound and the grid is therefore not the caller's to predict.
void stepControlSteps(Engine& engine, std::size_t steps) noexcept {
    const std::size_t samplesPerStep =
        engine.getStepIntervalChunks() * Engine::kControlChunkSamples;
    for (std::size_t s = 0; s < steps; ++s) {
        engine.processChunk(samplesPerStep);
    }
}

/// @brief Index of the first output that is non-finite or outside [0, 1].
/// @return SIZE_MAX when every published output is finite and in range.
[[nodiscard]] std::size_t firstBadOutput(const Engine& engine) noexcept {
    for (std::size_t i = 0; i < engine.getAgentCount(); ++i) {
        const float v = engine.getAgentOutput(i);
        if (!isFinite(v) || v < 0.0f || v > 1.0f) {
            return i;
        }
    }
    return SIZE_MAX;
}

}  // namespace

// ==============================================================================
// T007 case 1 - SC-009 (a): every setter REJECTS a non-finite argument
// ==============================================================================
// The assertion is not "the setter did something sensible" but the exact one
// FR-064 (1) states: the PREVIOUS value stands, to the bit. A setter that stored
// the NaN fails (NaN == anything is false); a setter that substituted a neutral
// fails too, because `settled` is deliberately not the default.
TEST_CASE("EcosystemEngine_NonFiniteInputsRejected", "[ecosystem_engine]") {
    // ~23.5 KB (plan S9): never a plain stack local in a test.
    auto owned = std::make_unique<Engine>();
    Engine& engine = *owned;
    engine.prepare(48000.0, Engine::PrepareConfig{});

    // --- park every knob at a known, non-default, in-range value -------------
    for (const auto& row : kKnobTable) {
        row.set(engine, row.settled);
    }
    for (const auto& row : kAffinityTable) {
        engine.setAffinity(row.from, row.to, row.settled);
    }

    std::array<float, kKnobCount> settledBack{};
    for (std::size_t k = 0; k < kKnobCount; ++k) {
        settledBack[k] = kKnobTable[k].get(engine);
        INFO("knob " << kKnobTable[k].name << " did not settle at its in-range probe");
        REQUIRE(settledBack[k] == kKnobTable[k].settled);
    }
    std::array<float, kAffinityCount> affinityBack{};
    for (std::size_t a = 0; a < kAffinityCount; ++a) {
        affinityBack[a] = engine.getAffinity(kAffinityTable[a].from, kAffinityTable[a].to);
        INFO("affinity " << kAffinityTable[a].name << " did not settle");
        REQUIRE(affinityBack[a] == kAffinityTable[a].settled);
    }

    const float wakeBefore = engine.getAgentWake(kProbeAgent);
    const double energyBefore = engine.getAgentEnergy(kProbeAgent);
    const double poolBefore = engine.getPoolEnergy();

    // --- the injection sweep -------------------------------------------------
    for (const auto& pattern : kPatterns) {
        INFO("binary32 pattern: " << pattern.name);
        const float bad = makeNonFinite(pattern.bits);

        // The clause that fails FIRST if the volatile sink is ever removed and
        // the constant folds to finite garbage. Without it every assertion
        // below would pass vacuously on a -ffast-math leg.
        REQUIRE_FALSE(isFinite(bad));

        for (std::size_t k = 0; k < kKnobCount; ++k) {
            INFO("knob: " << kKnobTable[k].name);
            kKnobTable[k].set(engine, bad);
            REQUIRE(kKnobTable[k].get(engine) == settledBack[k]);
        }

        for (std::size_t a = 0; a < kAffinityCount; ++a) {
            INFO("affinity: " << kAffinityTable[a].name);
            engine.setAffinity(kAffinityTable[a].from, kAffinityTable[a].to, bad);
            REQUIRE(engine.getAffinity(kAffinityTable[a].from, kAffinityTable[a].to) ==
                    affinityBack[a]);
        }

        // Both arguments of the ONE paired frequency setter, including the
        // both-ends-bad call. A setter that rejected only the argument it was
        // told about would store half a pair the caller never asked for.
        const float loBefore = engine.getFreqLoHz();
        const float hiBefore = engine.getFreqHiHz();
        engine.setFreqRangeHz(bad, hiBefore);
        REQUIRE(engine.getFreqLoHz() == loBefore);
        REQUIRE(engine.getFreqHiHz() == hiBefore);
        engine.setFreqRangeHz(loBefore, bad);
        REQUIRE(engine.getFreqLoHz() == loBefore);
        REQUIRE(engine.getFreqHiHz() == hiBefore);
        engine.setFreqRangeHz(bad, bad);
        REQUIRE(engine.getFreqLoHz() == loBefore);
        REQUIRE(engine.getFreqHiHz() == hiBefore);

        // The two event-surface entry points (FR-070, FR-071). Both are stubs
        // until T011; these clauses hold for the stub and keep their teeth
        // afterwards, which is why they are written now rather than deferred.
        engine.setAgentWake(kProbeAgent, bad);
        REQUIRE(engine.getAgentWake(kProbeAgent) == wakeBefore);

        engine.perturbAgent(kProbeAgent, bad);
        REQUIRE(engine.getAgentEnergy(kProbeAgent) == energyBefore);
        REQUIRE(engine.getPoolEnergy() == poolBefore);
    }

    // The binary64 patterns are the ones prepare()'s double arguments see. They
    // are built here so a folding regression surfaces in BOTH widths; their
    // behavioural arm is DivisorKnobExtremes below.
    for (const auto& pattern : kPatterns64) {
        INFO("binary64 pattern: " << pattern.name);
        REQUIRE_FALSE(isFinite(makeNonFiniteDouble(pattern.bits)));
    }

    // Rejection is not paralysis: a finite value still lands afterwards.
    engine.setPredation(0.42f);
    REQUIRE(engine.getPredation() == 0.42f);
}

// ==============================================================================
// T007 case 2 - SC-009 (c): the divisor knobs at their extremes
// ==============================================================================
// FR-005 / FR-006's prepare-time clamps are the ONLY thing standing between a
// documented API call and a division by zero. FR-061's publication divides by
// energyBudget_ and FR-008's share conversion divides by agentCount_ and
// resourceCells_ (ecosystem_engine.h:1129-1136, :1333-1334); dt_, sqrtDt_ and
// rampSteps_ all derive from sampleRate_ (:322-325).
//
// FALSIFICATION (tasks.md T007): remove the kMinEnergyBudget clamp from
// prepare() - the `energyBudget = 0.0` cell then publishes Inf/NaN and this
// case's firstBadOutput() clause fails.
TEST_CASE("EcosystemEngine_DivisorKnobExtremes", "[ecosystem_engine]") {
    auto owned = std::make_unique<Engine>();
    Engine& engine = *owned;

    constexpr std::size_t kSteps = 1000u;

    // --- (a) energyBudget ----------------------------------------------------
    struct BudgetRow {
        const char* name;
        double requested;
        double expected;
    };
    // The non-finite rows are SUBSTITUTED by sanitise()'s 1.0 neutral and then
    // clamped (ecosystem_engine.h:320), so they report 1.0, not a rail.
    const std::array<BudgetRow, 6> kBudgets{{
        {"zero", 0.0, Engine::kMinEnergyBudget},
        {"negative", -1.0, Engine::kMinEnergyBudget},
        {"1e-300", 1.0e-300, Engine::kMinEnergyBudget},
        {"1e300", 1.0e300, Engine::kMaxEnergyBudget},
        {"NaN", makeNonFiniteDouble(kPatterns64[0].bits), 1.0},
        {"+Inf", makeNonFiniteDouble(kPatterns64[1].bits), 1.0},
    }};
    for (const auto& row : kBudgets) {
        INFO("energyBudget arm: " << row.name);
        engine.prepare(48000.0, Engine::PrepareConfig{.energyBudget = row.requested});
        REQUIRE(engine.getEnergyBudget() == row.expected);
        stepControlSteps(engine, kSteps);
        REQUIRE(engine.getControlStepCount() == kSteps);
        REQUIRE(firstBadOutput(engine) == SIZE_MAX);
    }

    // --- (b) agentCount ------------------------------------------------------
    struct SizeRow {
        const char* name;
        std::size_t requested;
        std::size_t expected;
    };
    const std::array<SizeRow, 2> kAgentCounts{{
        {"zero", std::size_t{0}, Engine::kMinAgents},
        {"SIZE_MAX", SIZE_MAX, Engine::kMaxAgents},
    }};
    for (const auto& row : kAgentCounts) {
        INFO("agentCount arm: " << row.name);
        engine.prepare(48000.0, Engine::PrepareConfig{.agentCount = row.requested});
        REQUIRE(engine.getAgentCount() == row.expected);
        stepControlSteps(engine, kSteps);
        REQUIRE(engine.getControlStepCount() == kSteps);
        REQUIRE(firstBadOutput(engine) == SIZE_MAX);
    }

    // --- (c) resourceCells ---------------------------------------------------
    const std::array<SizeRow, 2> kResourceCells{{
        {"zero", std::size_t{0}, std::size_t{1}},
        {"SIZE_MAX", SIZE_MAX, Engine::kMaxResourceCells},
    }};
    for (const auto& row : kResourceCells) {
        INFO("resourceCells arm: " << row.name);
        engine.prepare(48000.0, Engine::PrepareConfig{.resourceCells = row.requested});
        REQUIRE(engine.getResourceCells() == row.expected);
        stepControlSteps(engine, kSteps);
        REQUIRE(engine.getControlStepCount() == kSteps);
        REQUIRE(firstBadOutput(engine) == SIZE_MAX);
    }

    // --- (d) stepIntervalChunks ----------------------------------------------
    const std::array<SizeRow, 2> kStepIntervals{{
        {"zero", std::size_t{0}, Engine::kMinStepIntervalChunks},
        {"SIZE_MAX", SIZE_MAX, Engine::kMaxStepIntervalChunks},
    }};
    for (const auto& row : kStepIntervals) {
        INFO("stepIntervalChunks arm: " << row.name);
        engine.prepare(48000.0, Engine::PrepareConfig{.stepIntervalChunks = row.requested});
        REQUIRE(engine.getStepIntervalChunks() == row.expected);
        // The grid must follow the CLAMPED interval, not the requested one.
        REQUIRE(engine.getStepDurationSeconds() ==
                static_cast<double>(row.expected * Engine::kControlChunkSamples) / 48000.0);
        stepControlSteps(engine, kSteps);
        REQUIRE(engine.getControlStepCount() == kSteps);
        REQUIRE(firstBadOutput(engine) == SIZE_MAX);
    }

    // --- (e) sample rate, BOTH halves of the sanitise-then-floor form ---------
    // Two distinct failure modes with two distinct remedies, in this order
    // (ecosystem_engine.h:312): a NON-FINITE rate has nothing to clamp and is
    // SUBSTITUTED by kDefaultSampleRate; the substituted-or-real rate is then
    // FLOORED at kMinUsableSampleRate. Only the sub-floor arm exercises that
    // std::max, and only the non-finite arm exercises the substitution - which
    // is why the two halves report DIFFERENT rates and are asserted separately
    // rather than both against 8000.
    struct RateRow {
        const char* name;
        double requested;
        double expected;
    };
    const std::array<RateRow, 7> kRates{{
        {"NaN", makeNonFiniteDouble(kPatterns64[0].bits), Engine::kDefaultSampleRate},
        {"+Inf", makeNonFiniteDouble(kPatterns64[1].bits), Engine::kDefaultSampleRate},
        {"-Inf", makeNonFiniteDouble(kPatterns64[2].bits), Engine::kDefaultSampleRate},
        {"zero", 0.0, Engine::kMinUsableSampleRate},
        {"negative", -48000.0, Engine::kMinUsableSampleRate},
        {"one", 1.0, Engine::kMinUsableSampleRate},
        {"just under the floor", 7999.0, Engine::kMinUsableSampleRate},
    }};
    for (const auto& row : kRates) {
        INFO("sampleRate arm: " << row.name);
        engine.prepare(row.requested, Engine::PrepareConfig{});
        REQUIRE(engine.getSampleRate() == row.expected);
        REQUIRE(engine.getStepDurationSeconds() ==
                static_cast<double>(engine.getStepIntervalChunks() *
                                    Engine::kControlChunkSamples) /
                    row.expected);
        stepControlSteps(engine, kSteps);
        REQUIRE(engine.getControlStepCount() == kSteps);
        REQUIRE(firstBadOutput(engine) == SIZE_MAX);
    }
}

// ==============================================================================
// T014 - Krate::DSP::detail::EcosystemEngineNonFiniteProbe
// ==============================================================================
// THIS TRANSLATION UNIT IS THE PROBE'S ONLY DEFINITION (plan S7.4, plan R-11).
// The header DECLARES it (ecosystem_engine.h:100) and befriends it
// (ecosystem_engine.h:2253); the library never defines it, so a shipping build
// has no way to call it. A SECOND definition anywhere - in another TU, or as a
// copy pasted into the behaviour TU - is an ODR violation the linker may not
// diagnose: it would silently pick one definition and the case below would be
// asserting against the other probe's idea of the state layout.
//
// It is the -fno-fast-math TU's probe by construction: every value it plants is
// a NaN or an Inf, and IEEE semantics on those may only be asserted here (plan
// A-1). Its finite twin - EcosystemEngineInspectProbe::setPool, which SC-009 (d)
// and the perturbAgent edge cases need - lives in the behaviour TU instead,
// because a NEGATIVE-BUT-FINITE pool is not about IEEE semantics and proving it
// here would prove it in a mode the header never ships in.
namespace Krate::DSP::detail {

struct EcosystemEngineNonFiniteProbe {
    static void injectAgentEnergy(EcosystemEngine& e, std::size_t i, double v) noexcept {
        e.energy_[i] = v;
    }
    static void injectCellEnergy(EcosystemEngine& e, std::size_t k, double v) noexcept {
        e.res_[k] = v;
    }
    static void injectPool(EcosystemEngine& e, double v) noexcept { e.pool_ = v; }
};

}  // namespace Krate::DSP::detail

namespace {

using NonFiniteProbe = Krate::DSP::detail::EcosystemEngineNonFiniteProbe;

/// @brief The three places a single non-finite value can be planted - one arm
///        each, never two at once, so a failing arm names its own site.
enum class InjectionSite : std::uint8_t { AgentEnergy, CellEnergy, Pool };

struct InjectionSiteRow {
    const char* name;
    InjectionSite site;
};

constexpr std::array<InjectionSiteRow, 3> kInjectionSites{{
    {"agent energy", InjectionSite::AgentEnergy},
    {"cell energy", InjectionSite::CellEnergy},
    {"pool", InjectionSite::Pool},
}};

/// The resource cell every per-cell injection writes to. Any index <
/// resourceCells works; pinning one keeps the failure message unambiguous.
constexpr std::size_t kProbeCell = 17u;

/// Long enough that the economy is off its prepare() transient and the injection
/// lands in a settled state rather than in the opening ramp (FR-070).
constexpr std::size_t kSettleSteps = 1000u;

/// SC-009 (b)'s "N >= 1000 further steps".
constexpr std::size_t kFollowSteps = 1000u;

/// @brief Plant @p bad at exactly one site, through the friend probe.
void injectAt(Engine& engine, InjectionSite site, double bad) noexcept {
    switch (site) {
        case InjectionSite::AgentEnergy:
            NonFiniteProbe::injectAgentEnergy(engine, kProbeAgent, bad);
            break;
        case InjectionSite::CellEnergy:
            NonFiniteProbe::injectCellEnergy(engine, kProbeCell, bad);
            break;
        case InjectionSite::Pool:
            NonFiniteProbe::injectPool(engine, bad);
            break;
    }
}

/// @brief Every published output, so two publications can be compared.
[[nodiscard]] std::array<float, Engine::kMaxAgents> snapshotOutputs(const Engine& engine) noexcept {
    std::array<float, Engine::kMaxAgents> out{};
    for (std::size_t i = 0; i < engine.getAgentCount(); ++i) {
        out[i] = engine.getAgentOutput(i);
    }
    return out;
}

/// @brief |getTotalEnergy() - getEnergyBudget()| / getEnergyBudget().
///
/// The magnitude is written out rather than taken from <cmath>: this TU already
/// avoids every std:: floating-point predicate on principle (the folding
/// argument in the header block above), and a two-line ternary needs no include.
[[nodiscard]] double conservationRelativeError(const Engine& engine) noexcept {
    const double d = engine.getTotalEnergy() - engine.getEnergyBudget();
    return ((d < 0.0) ? -d : d) / engine.getEnergyBudget();
}

}  // namespace

// ==============================================================================
// T014 case - SC-009 (b): an injected non-finite state is CONTAINED, not frozen
// ==============================================================================
// Nine arms: three injection sites x the three binary64 patterns. Each arm gets
// its own freshly prepared engine, settles 1000 steps, plants ONE non-finite
// value through the probe, and then asserts what FR-083's REPAIR rule - not an
// abandonment rule - is obliged to deliver on the very next step:
//
//   1. every published output is finite and in [0, 1];
//   2. getNonFiniteContainmentCount() advanced by EXACTLY 1 (one increment per
//      containment EVENT, not per repaired value);
//   3. getConservationViolationCount() is UNCHANGED - asserted separately from
//      (2), because one counter carrying two meanings cannot say which failure
//      mode fired;
//   4. the conserved total is back inside SC-001 (c)'s 1e-9 relative bound in
//      ONE step, which is what pool_ += (energyBudget_ - postRepairTotal)
//      re-establishes (ecosystem_engine.h:1249-1257, plan S14 D-C);
//   5. and 1000 FURTHER steps produce finite, in-range outputs that CHANGE.
//
// Clause 5 is the one with teeth, and it is why this case exists at all. An
// implementation that "contains" by abandoning the write - skip this agent this
// step, leave the NaN where it is - passes clauses 1-4 in appearance while the
// guard re-fires every step and FR-062's HELD outputs freeze the component at
// its last publication forever. Clauses 2 and 5 are then both false: the
// containment counter keeps climbing and no output ever moves again.
//
// FALSIFICATION (tasks.md T014): replace the repair in
// ecosystem_engine.h:1206-1227 with "skip this agent this step" - clause 5 fails
// (no output changes across the 1000 follow steps) and the post-follow
// containment-count clause fails with it. Restore.
TEST_CASE("EcosystemEngine_NonFiniteStateIsContained", "[ecosystem_engine]") {
    for (const auto& siteRow : kInjectionSites) {
        for (const auto& pattern : kPatterns64) {
            INFO("injection site: " << siteRow.name << ", binary64 pattern: " << pattern.name);

            // ~23.5 KB (plan S9): never a plain stack local in a test.
            auto owned = std::make_unique<Engine>();
            Engine& engine = *owned;
            engine.prepare(48000.0, Engine::PrepareConfig{});

            // The injection indices must be INSIDE the prepared population, or
            // the probe would plant its value in a slot the step never reads and
            // every clause below would pass without the guard ever firing.
            REQUIRE(engine.getAgentCount() > kProbeAgent);
            REQUIRE(engine.getResourceCells() > kProbeCell);

            // --- settle ------------------------------------------------------
            stepControlSteps(engine, kSettleSteps);
            REQUIRE(engine.getControlStepCount() == kSettleSteps);
            REQUIRE(firstBadOutput(engine) == SIZE_MAX);

            const std::uint64_t containmentsBefore = engine.getNonFiniteContainmentCount();
            const std::uint64_t violationsBefore = engine.getConservationViolationCount();
            // Settling alone must not have tripped either trap; otherwise the
            // "+1" and "unchanged" clauses below would be measuring a baseline
            // that is already wrong.
            REQUIRE(containmentsBefore == 0u);
            REQUIRE(violationsBefore == 0u);

            // --- inject ------------------------------------------------------
            const double bad = makeNonFiniteDouble(pattern.bits);
            // The clause that fails FIRST if the volatile sink is ever removed
            // and the pattern folds to finite garbage: without it, every
            // assertion below would pass vacuously with nothing injected.
            REQUIRE_FALSE(isFinite(bad));
            injectAt(engine, siteRow.site, bad);

            // --- the NEXT step -----------------------------------------------
            stepControlSteps(engine, 1u);
            REQUIRE(engine.getControlStepCount() == kSettleSteps + 1u);

            REQUIRE(firstBadOutput(engine) == SIZE_MAX);
            REQUIRE(engine.getNonFiniteContainmentCount() == containmentsBefore + 1u);
            REQUIRE(engine.getConservationViolationCount() == violationsBefore);
            REQUIRE(conservationRelativeError(engine) <= 1.0e-9);

            const std::array<float, Engine::kMaxAgents> atContainment = snapshotOutputs(engine);

            // --- 1000 further steps ------------------------------------------
            std::size_t badStep = SIZE_MAX;
            std::size_t badAgent = SIZE_MAX;
            for (std::size_t s = 0; s < kFollowSteps; ++s) {
                stepControlSteps(engine, 1u);
                const std::size_t offender = firstBadOutput(engine);
                if (offender != SIZE_MAX) {
                    badStep = s;
                    badAgent = offender;
                    break;
                }
            }
            INFO("first non-finite / out-of-range output after containment: step "
                 << badStep << ", agent " << badAgent);
            REQUIRE(badStep == SIZE_MAX);

            // THE ANTI-FREEZE CLAUSE. At least one agent's published output
            // differs from its value at the containment step; an engine that
            // abandoned the write and re-fires the guard every step republishes
            // the same held values forever and fails exactly here.
            const std::array<float, Engine::kMaxAgents> afterFollow = snapshotOutputs(engine);
            bool changed = false;
            for (std::size_t i = 0; i < engine.getAgentCount(); ++i) {
                if (afterFollow[i] != atContainment[i]) {
                    changed = true;
                    break;
                }
            }
            REQUIRE(changed);

            // The trap fired ONCE and stayed quiet: a repaired state is a finite
            // state, so no later step may re-contain (plan S7.3). This is the
            // second clause the abandonment implementation fails - its counter
            // would have advanced by 1001, not 1.
            REQUIRE(engine.getNonFiniteContainmentCount() == containmentsBefore + 1u);
            REQUIRE(engine.getConservationViolationCount() == violationsBefore);
            REQUIRE(conservationRelativeError(engine) <= 1.0e-9);
        }
    }
}
