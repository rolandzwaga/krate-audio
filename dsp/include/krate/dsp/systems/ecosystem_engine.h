// ==============================================================================
// Layer 3: System Component - EcosystemEngine
// ==============================================================================
// A fixed table of <= 48 AGENTS holding `double` energy on a 2-D toroidal
// habitat, stepped once every `stepIntervalChunks * 64` samples through thirteen
// normative stages that move energy between the agents, a <= 96-cell resource
// strip field and one pool WITHOUT EVER CREATING OR DESTROYING A JOULE, and
// publish one clamped `float` in [0, 1] per agent.
//
// THERE IS NO AUDIO PATH, NO OSCILLATOR, NO FILTER AND NO PER-SAMPLE LOOP
// ANYWHERE IN THIS FILE. The component is an economy plus a publication
// surface; what a Phase-10 host does with that surface is the host's business
// (see "The read surface" below, FR-060).
//
// Feature: vorago-phase8-ecosystem
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (every method noexcept and allocation-free,
//   prepare() included - every member is a fixed-size std::array or a scalar)
// - Principle III: Modern C++ (C++20, value semantics, no owning pointers)
// - Principle IX: Layer 3, but deliberately stricter than the layer rule -
//   Layer 0 + stdlib ONLY (FR-001)
// - Principle X: DSP Constraints (a conserved economy, reject-never-clamp
//   setter hygiene, a non-finite guard ladder)
// - Principle XI: Performance Budget (FR-085, SC-011)
//
// Reference: specs/vorago-phase8-ecosystem/spec.md
//            specs/vorago-phase8-ecosystem/plan.md
//            specs/vorago-phase8-ecosystem/tasks.md
//
// ------------------------------------------------------------------------------
// NO SHIPPED HEADER IS AMENDED BY THIS PHASE (FR-090). modulation_engine.h,
// modulation_matrix.h, voice_mod_router.h, voice_mod_types.h,
// modulation_source.h, modulation_types.h, envelope_follower.h,
// slow_event_scheduler.h, noise_organism.h, resonance_drift_network.h,
// feedback_ecology.h, subharmonic_engine.h, bloom_engine.h, harmonic_cloud.h,
// atmosphere_engine.h AND core/random.h are byte-unchanged. This file includes
// none of them.
// ------------------------------------------------------------------------------
//
// BUILD STATE (tasks.md T003 - the skeleton pass).
// T003 establishes:
//   * the plan S1.1 include list - Layer 0 + stdlib ONLY, with the literal
//     include-block check in ecosystem_engine_test.cpp standing in for a lint
//     that structurally cannot see the rule (plan S14 D-J);
//   * every plan S1.2 constant with EVERY static_assert live;
//   * the plan S1.3 Kind enum and PrepareConfig, and the two probe forward
//     declarations of plan R-11;
//   * the complete plan S1.4 public surface. Read accessors are REAL and carry
//     the plan S8 three-class neutral contract; every MUTATOR is a documented
//     stub carrying the task that lands it;
//   * the complete plan S1.5 private state, in declaration order;
//   * the plan S1.6 salt table and the `double` RNG helpers;
//   * every normative doxygen passage tasks.md T003 enumerates, INCLUDING the
//     passages whose code lands in T008-T011 - a later task does not come back
//     for prose.
// T004 lands prepare() / initialiseState() / reset() / setSeed() /
//   refreshDerivedScales() / processChunk().
// T005 lands the 23 setters, setAffinity and setFreqRangeHz.
// T008-T011 land simulationStep()'s thirteen stages, the wake/dormancy surface
//   and perturbAgent().
// T013 lands the FR-083 guard ladder and the repair rule.
// ==============================================================================

#pragma once

// Layer 3 (systems/). Dependencies: Layer 0 + stdlib ONLY (FR-001) - NOT Layer 1
// (in particular primitives/smoother.h is NOT pulled in for the wake ramp: that
// ramp is four lines of arithmetic on the control-step grid, plan S6.2), NOT
// Layer 2, NOT a Layer 3 peer, NOT Layer 4, and no consumer header of any kind.
#include <krate/dsp/core/db_utils.h>  // L0: detail::isFinite(float), detail::isFinite(double)
#include <krate/dsp/core/random.h>    // L0: Xorshift32, deriveStreamSeed

#include <algorithm>  // std::clamp, std::min, std::max
#include <array>
#include <cmath>  // std::exp, std::sin, std::cos, std::sqrt, std::floor, std::pow, std::ceil, std::log2
#include <cstddef>
#include <cstdint>

// NO <vector>, NO <memory>, NO <functional>, NO <random>, no I/O, no exceptions.
// The include set above is the whole of FR-001 and is asserted line-by-line by
// EcosystemEngine_HeaderIncludesOnlyLayerZero.

namespace Krate::DSP {

namespace detail {

/// @brief SC-009 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A TEST TU.
///
/// Its ONE definition lives in
/// dsp/tests/unit/systems/ecosystem_engine_nonfinite_test.cpp - the only Phase-8
/// TU in the -fno-fast-math block, and therefore the only place IEEE semantics
/// may be asserted on an injected NaN/Inf. A second definition anywhere is an
/// ODR violation the linker may not diagnose (plan R-11).
///
/// The library never defines it, so a shipping build has no way to call it.
/// Declaring the friend HERE is deliberate: the test TU needs no header edit
/// (the bloom_engine.h:150-162 rationale).
struct EcosystemEngineNonFiniteProbe;

/// @brief SC-020 / SC-021 / SC-009 (d) inspection probe. DECLARED HERE, DEFINED
///        ONLY BY A TEST TU.
///
/// Its ONE definition lives in dsp/tests/unit/systems/ecosystem_engine_test.cpp
/// (tasks.md T008). It is a SECOND probe rather than a member of the one above
/// (plan A-1) because SC-020 and SC-021 must be proved in the /fp:fast +
/// -ffast-math mode the header actually ships in; a single probe struct has a
/// single definition and would drag both criteria into the -fno-fast-math TU.
struct EcosystemEngineInspectProbe;

}  // namespace detail

/// @brief A conserved agent economy on a toroidal habitat, published as one
///        normalised float per agent.
///
/// @par Layer: 3 (systems/). Dependencies: Layer 0 + stdlib ONLY. NO Layer 1,
///      NO Layer 2, NO Layer 3 peer, NO Layer 4.
///
/// @par Real-Time Safety: EVERY method is noexcept and allocation-free,
///      prepare() INCLUDED. There is no heap term at all - every member is a
///      fixed-size std::array or a scalar - so getAllocatedBytes() returns 0
///      unconditionally (the resonance_drift_network.h:906-912 precedent, kept
///      so the Phase-10 host can total its children uniformly).
///
/// @par FOOTPRINT: the object is approximately 23.5 KB (plan S9's itemised
///      ledger is the single authority; a second figure in a second place is how
///      a later reader "reconciles" the two by shrinking the object). That is
///      too large for a casual stack local: CONSTRUCT IT AS A MEMBER, OR THROUGH
///      std::make_unique - never as a plain stack local, in production code or
///      in a test. SC-007 additionally requires the construction to happen
///      OUTSIDE its AllocationScope, or the allocation is counted and the
///      criterion fails for the wrong reason.
///
/// @par The contract in one paragraph
///      prepare() partitions a fixed energy budget three ways - agents, resource
///      cells, pool - and processChunk() advances a control clock that fires one
///      simulationStep() every `stepIntervalChunks * 64` samples. Each step runs
///      thirteen normative stages (FR-087) that move energy between those three
///      holders and never create or destroy any. getAgentOutput(i) reports the
///      agent's energy as a share of the mean, anchored so that the mean share
///      publishes 0.5, gated by wake/dormancy and clamped to [0, 1].
class EcosystemEngine {
public:
    // =========================================================================
    // Constants (plan S1.2) - every static_assert below is live
    // =========================================================================

    // ---- capacities (FR-004) ------------------------------------------------

    /// Roadmap line 381's upper bound on the population (OQ-3).
    static constexpr std::size_t kMaxAgents = 48;
    static constexpr std::size_t kMinAgents = 1;
    static_assert(kMaxAgents == 48, "FR-004: roadmap line 381's population ceiling (OQ-3)");
    // FR-004 requires a LIVE assert on kMinAgents too, and "== 1" alone would be a
    // tautology restating the line above it. The second clause is the load-bearing
    // one: prepare() clamps agentCount into [kMinAgents, kMaxAgents], and an empty
    // or inverted range would make that clamp meaningless while still compiling.
    static_assert(kMinAgents == 1 && kMinAgents <= kMaxAgents,
                  "FR-004: the one-agent edge is legal and the clamp range is non-empty");

    /// The resource strip's cell ceiling (run.js:317's upper bound).
    static constexpr std::size_t kMaxResourceCells = 96;
    static_assert(kMaxResourceCells == 96 && kMaxResourceCells >= 1,
                  "FR-004: the fuzz box's cell ceiling (run.js:317), and at least one cell");

    /// The Kind roster size (ecosystem-sim.js:63).
    static constexpr std::size_t kNumKinds = 5;
    static_assert(kNumKinds == 5, "FR-004: the Kind roster size (ecosystem-sim.js:63)");

    /// Every unordered pair of agents, which is what the stage-2 scratch sizes.
    static constexpr std::size_t kMaxPairs = kMaxAgents * (kMaxAgents - 1) / 2;
    static_assert(kMaxPairs == 1128, "pair scratch sizing");
    static_assert(kMaxAgents <= 255, "pair index arrays are std::uint8_t");

    // ---- the shared control grid --------------------------------------------

    /// The library-wide control clock (bloom_engine.h:221, harmonic_cloud.h:144,
    /// noise_organism.h:150, resonance_drift_network.h:135,
    /// subharmonic_engine.h:168). A component that drifted off it would
    /// decorrelate the per-voice modulation grid at Phase 10.
    static constexpr std::size_t kControlChunkSamples = 64;
    static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

    /// FR-082's minimum was NARROWED from 1 to 8 by two user rulings on
    /// 2026-09-16, FR-085's named escalation, each taken from SC-011's measured
    /// table: at one step per 64-sample chunk the worst case costs ~405 000
    /// ns/block against a 53 333 ns ceiling and cannot fit at any lever; at one
    /// step per 256 samples it still reads 56 600-60 200 after every exact
    /// lever (E-1, E-2, E-3). The floor is therefore the tuned default. Not a
    /// threshold move: the public range shrank, the ceiling did not.
    static constexpr std::size_t kMinStepIntervalChunks = 8;   ///< FR-082 (ruled 2026-09-16)
    static constexpr std::size_t kMaxStepIntervalChunks = 64;  ///< FR-082

    /// 8 chunks = 512 samples = the tuned dt (OQ-2).
    static constexpr std::size_t kDefaultStepIntervalChunks = 8;

    /// prepare()'s sample-rate floor (the resonance_drift_network.h:281 figure).
    /// dt_, sqrtDt_ and rampSteps_ all derive from the floored rate, which is
    /// why SC-009 (c) drives both halves of the sanitise-then-floor form.
    static constexpr double kMinUsableSampleRate = 8000.0;
    static constexpr double kDefaultSampleRate = 48000.0;
    // FR-004's live assert on the floor. The second clause is the one with teeth:
    // prepare() takes max(floor, rate), so a floor above the default rate would
    // silently re-rate every host session at 8 kHz rather than clamp an outlier.
    static_assert(kMinUsableSampleRate == 8000.0 && kMinUsableSampleRate <= kDefaultSampleRate,
                  "FR-004: the resonance_drift_network.h:281 floor, below the default rate");

    // ---- energy budget domain (FR-005) --------------------------------------

    /// LOAD-BEARING, not cosmetic: this is FR-061's divisor floor. Without it
    /// `energyBudget = 0` is reachable through the documented API and every
    /// published output is non-finite.
    static constexpr double kMinEnergyBudget = 1.0e-3;
    static constexpr double kMaxEnergyBudget = 1.0e3;

    // ---- publication (FR-061, FR-063, FR-070) -------------------------------

    /// The silence epsilon the three sleeping siblings already share
    /// (feedback_ecology.h:428, resonance_drift_network.h:266,
    /// bloom_engine.h:292).
    static constexpr float kWakeSilenceEpsilon = 1.0e-6f;

    /// The Dormancy rule's re-entry fade, identical in five shipped components
    /// (noise_organism.h:178, resonance_drift_network.h:143,
    /// feedback_ecology.h:417, subharmonic_engine.h:177, bloom_engine.h:287).
    static constexpr float kGainRampMs = 50.0f;

    /// FR-061: an agent holding exactly the mean share publishes 0.5, so the
    /// output surface is invariant to energyBudget and agentCount (SC-019).
    static constexpr double kOutputAnchor = 0.5;

    // ---- affinity matrix range (FR-031) -------------------------------------

    // setAffinity() is the ONLY setter whose argument is not an Appendix-A
    // scalar, so its bounds live here rather than in the S1.5 range comments.
    static constexpr float kMinAffinity = -2.0f;
    static constexpr float kMaxAffinity = +2.0f;

    // ---- numerics -----------------------------------------------------------

    /// FR-012's normative neighbour cutoff (ecosystem-sim.js:274). Kept VERBATIM
    /// as the second half of the two-stage test in plan S4.0; the exp-free
    /// pre-test in front of it is strictly conservative, so the interacting SET
    /// is exactly the prototype's.
    static constexpr double kNeighbourWeightCutoff = 1.0e-6;

    /// FR-043's per-cell denormal guard. A cell whose magnitude falls below it
    /// is snapped to exactly 0.0 and the snapped amount is RETURNED TO THE POOL.
    static constexpr double kDenormalCellGuard = 1.0e-30;

    /// ecosystem-sim.js:294 - the unit-vector divisor's zero guard.
    static constexpr double kUnitVectorEpsilon = 1.0e-9;

    /// ecosystem-sim.js:253's `twoPi`, carried in DOUBLE.
    ///
    /// core/math_constants.h's kTwoPi is a `float` and this header's include set
    /// is Layer 0 + stdlib ONLY (FR-001), so the two phase terms - the Kuramoto
    /// coupling (stage 2) and the appetite gate (stage 4) - carry their own
    /// double literal rather than rounding 2*pi to float first and then widening
    /// the rounded value back out.
    static constexpr double kTwoPi = 6.283185307179586;

    // ---- SC-012's structural anchor (plan addition A-2) ---------------------

    /// The number of knobs in the spec's Appendix A (affinity counted as one).
    /// SC-012's fuzz-coverage table is asserted against this. RAISE IT IN THE
    /// SAME COMMIT THAT ADDS A SETTER, or EcosystemEngine_FuzzCoverageIsComplete
    /// fails.
    static constexpr std::size_t kConfigKnobCount = 28;

    // =========================================================================
    // Nested types (plan S1.3)
    // =========================================================================

    /// @brief The agent roster (FR-011).
    ///
    /// APPEND ONLY - this becomes a persisted plugin parameter at Phase 12, and
    /// the affinity matrix is INDEXED BY IT, so the order is normative
    /// (ecosystem-sim.js:63). Inserting a value renumbers every stored affinity
    /// entry and silently changes every trajectory.
    enum class Kind : std::uint8_t {
        Partial = 0,
        Resonator = 1,
        Noise = 2,
        Feedback = 3,
        Ghost = 4
    };
    static_assert(static_cast<std::size_t>(Kind::Ghost) + 1u == kNumKinds, "Kind roster");

    /// @brief The prepare-time configuration, and NOTHING else (FR-005, FR-006).
    ///
    /// Callers MUST use designated initialisers -
    /// `EcosystemEngine::PrepareConfig{.agentCount = 24, .resourceCells = 96}` -
    /// so that no narrowing conversion can hide in a positional brace init.
    /// Clang errors on such a narrowing where MSVC does not, which makes a
    /// positional init a Windows-green / CI-red construct
    /// (resonance_drift_network.h:297-306).
    ///
    /// Every RULE knob is a runtime setter (FR-064) with a member-initialiser
    /// default, which is what makes FR-006 a compile-time fact rather than a
    /// documented promise.
    struct PrepareConfig {
        std::size_t agentCount = 32;             ///< clamped [kMinAgents, kMaxAgents]
        std::size_t resourceCells = 64;          ///< clamped [1, kMaxResourceCells]
        double energyBudget = 1.0;               ///< clamped [kMinEnergyBudget, kMaxEnergyBudget]
        double initialPoolFraction = 0.5;        ///< clamped [0.1, 0.9]
        std::size_t stepIntervalChunks = kDefaultStepIntervalChunks;  ///< clamped [8, 64]
    };

    // =========================================================================
    // Construction
    // =========================================================================

    EcosystemEngine() noexcept = default;
    EcosystemEngine(const EcosystemEngine&) = default;
    EcosystemEngine& operator=(const EcosystemEngine&) = default;
    EcosystemEngine(EcosystemEngine&&) noexcept = default;
    EcosystemEngine& operator=(EcosystemEngine&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-005, FR-080) - plan S2
    // =========================================================================

    /// @brief Derive the initial state for @p config at @p sampleRate.
    ///
    /// Allocation-free in fact, not merely by promise: every member is a fixed
    /// std::array. A non-finite sample rate is SUBSTITUTED and then FLOORED at
    /// kMinUsableSampleRate (the bloom_engine.h:385 sanitise-then-floor form);
    /// every PrepareConfig field is clamped and the matching getter reports the
    /// clamp.
    ///
    /// prepare() RE-DERIVES STATE, NEVER CONFIGURATION. The affinity matrix and
    /// all 23 rule knobs survive it unchanged, exactly as `seed_` and every
    /// configuration scalar survive bloom_engine.h:386-388's prepare().
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // --- plan S2.1, step 1: SANITISE THEN FLOOR -------------------------
        // Two distinct failure modes, two distinct remedies, in this order: a
        // non-finite rate is SUBSTITUTED (there is nothing to clamp), and the
        // substituted-or-real rate is then FLOORED. dt_, sqrtDt_ and rampSteps_
        // all derive from this one number, which is why SC-009 (c) drives both
        // halves (bloom_engine.h:385).
        sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, kDefaultSampleRate));

        // --- step 2: the five prepare-time clamps ---------------------------
        agentCount_ = std::clamp(config.agentCount, kMinAgents, kMaxAgents);
        resourceCells_ = std::clamp(config.resourceCells, std::size_t{1}, kMaxResourceCells);
        stepChunks_ =
            std::clamp(config.stepIntervalChunks, kMinStepIntervalChunks, kMaxStepIntervalChunks);
        energyBudget_ =
            std::clamp(sanitise(config.energyBudget, 1.0), kMinEnergyBudget, kMaxEnergyBudget);
        initialPoolFrac_ = std::clamp(sanitise(config.initialPoolFraction, 0.5), 0.1, 0.9);

        // --- step 3: the control-step grid ----------------------------------
        dt_ = static_cast<double>(stepChunks_ * kControlChunkSamples) / sampleRate_;
        sqrtDt_ = std::sqrt(dt_);
        rampSteps_ = std::max(std::size_t{1}, static_cast<std::size_t>(std::ceil(0.050 / dt_)));

        // --- steps 4-5: the derived scales ----------------------------------
        refreshDerivedScales();       // FR-008's share -> absolute conversion (plan S5)
        refreshKernelDerivatives();   // plan S4.0

        // --- steps 6-7: counters and both clock residues ---------------------
        clearCounters();  // Clarification Q8: EVERY counter, not just the public ones
        samplePhase_ = 0;
        chunkPhase_ = 0;

        // --- step 8: prepare() RE-DERIVES STATE, NEVER CONFIGURATION --------
        // The affinity matrix and all 23 rule knobs are deliberately untouched
        // here. They are member-initialised at construction and survive every
        // prepare(), exactly as `seed_` and every configuration scalar survive
        // bloom_engine.h:386-388's prepare().

        // --- steps 9-10 -----------------------------------------------------
        initialiseState();
        prepared_ = true;
        publish(false);  // the gates were SNAPPED in initialiseState(): no 50 ms
                         // window at t = 0 (bloom_engine.h:399-400). This is not
                         // a simulation step, so it engages no rail counter.
    }

    /// @brief Return to exactly the state prepare() produced for the current
    ///        seed and configuration (FR-005).
    ///
    /// Re-runs the initial-state derivation, clears every counter and both clock
    /// residues, and publishes once. It does NOT re-read a PrepareConfig and
    /// does not touch any rule knob.
    void reset() noexcept {
        initialiseState();
        clearCounters();
        samplePhase_ = 0;
        chunkPhase_ = 0;
        publish(false);
    }

    /// @brief Set the master seed and re-derive from it.
    ///
    /// This RE-DERIVES THE INITIAL STATE (a re-prepare() in effect); it does NOT
    /// re-seed the lanes in place while the simulation keeps its current
    /// energies, positions and phases. `seed_ = seed;` followed by exactly
    /// reset(). setSeed(0) is legal and exercises both zero substitutions -
    /// Xorshift32::seed()'s kDefaultSeed (core/random.h:72-74) and
    /// deriveStreamSeed()'s 0x2545F491 (core/random.h:112).
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        reset();
    }

    // =========================================================================
    // The control clock (FR-081, FR-082) - plan S3
    // =========================================================================

    /// @brief Advance the control clock by @p numSamples and fire every
    ///        simulation step that falls inside that span.
    ///
    /// BOTH RESIDUES LIVE ACROSS CALLS - `samplePhase_` within
    /// kControlChunkSamples, `chunkPhase_` within `stepIntervalChunks`. The
    /// number of steps after N total advanced samples is therefore
    /// `floor((N + phase0) / (stepIntervalChunks * 64))`: a function of N alone,
    /// never of how N was partitioned into calls.
    ///
    /// THE NAMED FAILURE MODE (bloom_engine.h:899, quoted because it is the
    /// natural mistake): computing `numSamples / kControlChunkSamples` steps per
    /// call and discarding the residue at the call boundary. That is a
    /// BLOCK-RELATIVE grid, not an absolute one; it makes the step count a
    /// function of the host's buffer size and it is exactly what SC-008's
    /// partition-invariance case fails on.
    ///
    /// `numSamples == 0` applies the current state WITHOUT advancing anything
    /// and draws from NO RNG stream (FR-081).
    void processChunk(std::size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0u) {
            return;  // FR-007 / FR-081: zero advances nothing and draws no RNG
        }
        std::size_t remaining = numSamples;
        while (remaining > 0u) {
            const std::size_t take = std::min(remaining, kControlChunkSamples - samplePhase_);
            samplePhase_ += take;
            remaining -= take;
            if (samplePhase_ == kControlChunkSamples) {
                samplePhase_ = 0u;
                if (++chunkPhase_ >= stepChunks_) {
                    chunkPhase_ = 0u;
                    simulationStep();
                }
            }
        }
    }

    // =========================================================================
    // Rule knobs (FR-064) - every setter has a getter. Plan S1.4 / S5 / S8 (iii)
    // =========================================================================
    // Every setter obeys FR-064's three behaviours IN THIS ORDER:
    //   1. a non-finite argument is REJECTED and the previous value stands
    //      (detail::isFinite, never the std:: predicates, which fold away under
    //      -ffast-math - bloom_engine.h:624-630, tools/lint-nonfinite-symbols.js);
    //   2. an out-of-range value is CLAMPED to its Appendix-A range;
    //   3. the clamped value is stored, and the getter reports it.
    // The four share-unit setters additionally re-run refreshDerivedScales()
    // (plan S5); setKernelSigma additionally recomputes the S4.0 kernel
    // derivatives. NEITHER is done per step.
    //
    // Knob getters have NO NEUTRAL (plan S8 (iii)): they report the current
    // configuration whether the object is prepared or not, because FR-064
    // requires the setter/getter round trip to hold and prepare() re-derives
    // state, never configuration.

    /// @brief Interaction kernel width, in habitat units. Range [0.01, 0.35].
    void setKernelSigma(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        kernelSigma_ = std::clamp(v, 0.01f, 0.35f);  // FR-064 (2) + (3)
        refreshKernelDerivatives();
    }
    [[nodiscard]] float getKernelSigma() const noexcept { return kernelSigma_; }

    /// @brief Exchange rule gain. Range [0, 3.0].
    void setExchangeRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        exchangeRate_ = std::clamp(v, 0.0f, 3.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getExchangeRate() const noexcept { return exchangeRate_; }

    /// @brief Predation asymmetry. Range [0, 1].
    ///
    /// SEE THE FR-021 TRAP on the exchange flow line (the simulationStep()
    /// doxygen below): `predation == 0.5f` disables the exchange rule EXACTLY.
    void setPredation(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        predation_ = std::clamp(v, 0.0f, 1.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getPredation() const noexcept { return predation_; }

    /// @brief The refuge floor an agent will not give below, in SHARES of the
    ///        mean (FR-008). Range [0, 1.6].
    void setPreyFloorShares(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        preyFloorShares_ = std::clamp(v, 0.0f, 1.6f);  // FR-064 (2) + (3)
        refreshDerivedScales();
    }
    [[nodiscard]] float getPreyFloorShares() const noexcept { return preyFloorShares_; }

    /// @brief Per-agent carrying capacity, in SHARES of the mean (FR-008).
    ///        Range [0.32, 32].
    void setCapacityShares(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        capacityShares_ = std::clamp(v, 0.32f, 32.0f);  // FR-064 (2) + (3)
        refreshDerivedScales();
    }
    [[nodiscard]] float getCapacityShares() const noexcept { return capacityShares_; }

    /// @brief Metabolic leak coefficient. Range [0, 1.0].
    void setLeakRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        leakRate_ = std::clamp(v, 0.0f, 1.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getLeakRate() const noexcept { return leakRate_; }

    /// @brief Leak nonlinearity exponent. Range [1.0, 2.5].
    ///
    /// FR-052's MACRO BAND, and it is header content because the range is wider
    /// than the usable band: "leakExponent above ~1.3 kills the ecosystem - at
    /// per-agent energies of ~0.03 a superlinear leak all but vanishes, agents
    /// fill to capacity and sit (63 % alive in the lowest tercile vs 13 % in the
    /// highest, run.js:349-353). Phase 10's macros must stay AT OR BELOW 1.3;
    /// the full [1.0, 2.5] range exists so the fuzz box can prove the component
    /// stays BOUNDED there, not because the upper half is musically usable."
    void setLeakExponent(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        leakExponent_ = std::clamp(v, 1.0f, 2.5f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getLeakExponent() const noexcept { return leakExponent_; }

    /// @brief Affinity/crowding force gain on movement. Range [0, 0.5].
    void setMoveRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        moveRate_ = std::clamp(v, 0.0f, 0.5f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getMoveRate() const noexcept { return moveRate_; }

    /// @brief Per-step movement slew limit, in habitat units per second.
    ///        Range [0.001, 0.05].
    void setMaxSpeed(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        maxSpeed_ = std::clamp(v, 0.001f, 0.05f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getMaxSpeed() const noexcept { return maxSpeed_; }

    /// @brief Resource-gradient climbing gain (x axis only, FR-040).
    ///        Range [0, 0.05].
    void setForageRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        forageRate_ = std::clamp(v, 0.0f, 0.05f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getForageRate() const noexcept { return forageRate_; }

    /// @brief Short-range repulsion strength. Range [0, 0.2].
    void setCrowding(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        crowding_ = std::clamp(v, 0.0f, 0.2f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getCrowding() const noexcept { return crowding_; }

    /// @brief Short-range repulsion radius. Range [0.005, 0.05].
    void setCrowdingRadius(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        crowdingRadius_ = std::clamp(v, 0.005f, 0.05f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getCrowdingRadius() const noexcept { return crowdingRadius_; }

    /// @brief Kuramoto phase-coupling gain. Range [0, 0.5]. DEFAULT 0 (FR-035).
    void setSyncRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        syncRate_ = std::clamp(v, 0.0f, 0.5f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getSyncRate() const noexcept { return syncRate_; }

    /// @brief Per-cell carrying capacity, in SHARES of `energyBudget /
    ///        resourceCells` (FR-008). Range [0.32, 12.8].
    void setCellCapacityShares(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        cellCapShares_ = std::clamp(v, 0.32f, 12.8f);  // FR-064 (2) + (3)
        refreshDerivedScales();
    }
    [[nodiscard]] float getCellCapacityShares() const noexcept { return cellCapShares_; }

    /// @brief Resource regrowth rate. Range [0, 1.0].
    void setRegenRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        regenRate_ = std::clamp(v, 0.0f, 1.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getRegenRate() const noexcept { return regenRate_; }

    /// @brief Grazing rate. Range [0, 3.0].
    void setGrazeRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        grazeRate_ = std::clamp(v, 0.0f, 3.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getGrazeRate() const noexcept { return grazeRate_; }

    /// @brief Global pool-to-agent feed rate. Range [0, 1.0]. DEFAULT 0 (FR-057).
    void setFeedRate(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        feedRate_ = std::clamp(v, 0.0f, 1.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getFeedRate() const noexcept { return feedRate_; }

    /// @brief Satiation threshold, in SHARES of the mean (FR-008); 0 = off.
    ///        Range [0, 16].
    void setSatiationShares(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        satiationShares_ = std::clamp(v, 0.0f, 16.0f);  // FR-064 (2) + (3)
        refreshDerivedScales();
    }
    [[nodiscard]] float getSatiationShares() const noexcept { return satiationShares_; }

    /// @brief Appetite modulation depth (FR-050). Range [0, 1].
    void setAppetiteDepth(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        appetiteDepth_ = std::clamp(v, 0.0f, 1.0f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getAppetiteDepth() const noexcept { return appetiteDepth_; }

    /// @brief ONE setter for the two ends of the agent-frequency range.
    ///
    /// Two knobs, one setter, because the pair must stay ORDERED or FR-013's
    /// hard clamp inverts. A caller that set them independently could
    /// transiently invert the range; this makes that unreachable. Each end is
    /// clamped to its Appendix-A range (lo [0.0005, 0.005], hi [0.006, 0.05])
    /// and then `hi >= lo + 1e-4f` is enforced. Two getters, as FR-064 requires.
    void setFreqRangeHz(float lo, float hi) noexcept {
        // FR-064 (1), applied to the PAIR: a non-finite end rejects the whole
        // call and both previous values stand. Letting the finite end through
        // would store half a pair the caller never asked for, which is exactly
        // the transient inversion this single setter exists to make unreachable.
        if (!detail::isFinite(lo) || !detail::isFinite(hi)) {
            return;
        }
        const float clampedLo = std::clamp(lo, 0.0005f, 0.005f);   // FR-064 (2)
        const float clampedHi = std::clamp(hi, 0.006f, 0.05f);
        freqLoHz_ = clampedLo;
        // S1.4's ordering rule. With Appendix A's two ranges DISJOINT
        // (lo <= 0.005, hi >= 0.006) the post-clamp gap is never below 0.001 and
        // this max() never bites; it is what keeps FR-013's hard clamp from
        // inverting if either range is ever widened.
        freqHiHz_ = std::max(clampedHi, clampedLo + 1.0e-4f);
    }
    [[nodiscard]] float getFreqLoHz() const noexcept { return freqLoHz_; }
    [[nodiscard]] float getFreqHiHz() const noexcept { return freqHiHz_; }

    /// @brief Bounded OU drift amplitude on agent frequency. Range [0, 0.0002].
    void setFreqDrift(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous value stands
        }
        freqDrift_ = std::clamp(v, 0.0f, 0.0002f);  // FR-064 (2) + (3)
    }
    [[nodiscard]] float getFreqDrift() const noexcept { return freqDrift_; }

    /// @brief Set one entry of the kind-by-kind affinity matrix (FR-031).
    ///
    /// An out-of-range Kind (reachable only through a cast, since Kind is a
    /// scoped enum) is a SILENT NO-OP; a non-finite @p v is rejected; otherwise
    /// the value is clamped to [kMinAffinity, kMaxAffinity].
    ///
    /// THE MATRIX IS NOT SYMMETRISED. FR-031 is per entry and the pair pass
    /// reads `affinity_[kind_i][kind_j]` for the i-side force only, so
    /// setAffinity(a, b, ...) and setAffinity(b, a, ...) are genuinely two
    /// knobs, not two spellings of one.
    void setAffinity(Kind from, Kind to, float v) noexcept {
        const auto f = static_cast<std::size_t>(from);
        const auto t = static_cast<std::size_t>(to);
        if (f >= kNumKinds || t >= kNumKinds) {
            return;  // FR-064: an out-of-range Kind is a SILENT no-op and
                     // writes nothing - the index guard comes FIRST, so a
                     // non-finite value at a bad index cannot reach the store.
        }
        if (!detail::isFinite(v)) {
            return;  // FR-064 (1): the previous entry stands
        }
        // NOT symmetrised: affinity_[t][f] is a different knob (FR-031).
        affinity_[f][t] = std::clamp(v, kMinAffinity, kMaxAffinity);
    }
    [[nodiscard]] float getAffinity(Kind from, Kind to) const noexcept {
        const auto f = static_cast<std::size_t>(from);
        const auto t = static_cast<std::size_t>(to);
        if (f >= kNumKinds || t >= kNumKinds) {
            return 0.0f;  // plan S8 (i): out-of-range index neutral
        }
        return affinity_[f][t];
    }

    // =========================================================================
    // Sleep / wake / events (FR-070, FR-071, FR-072) - plan S6
    // =========================================================================

    /// @brief Drive agent @p i's wake amount in [0, 1] (FR-070).
    ///
    /// An out-of-range index is a silent no-op; a non-finite amount is rejected;
    /// the clamped value is snapped to exactly 0 at or below kWakeSilenceEpsilon
    /// (the bloom_engine.h:650-655 form). This does NOT advance the simulation
    /// and does NOT touch the ramp: the ramp target is recomputed at the next
    /// publication, which is the whole point of FR-062.
    void setAgentWake(std::size_t i, float amount) noexcept {
        if (i >= agentCount_) {
            return;  // FR-064: an out-of-range index is a silent no-op
        }
        if (!detail::isFinite(amount)) {
            return;  // FR-064 (1): the previous value stands
        }
        const float w = std::clamp(amount, 0.0f, 1.0f);
        // The SETTER's snap (bloom_engine.h:650-655), which is NOT the same
        // mechanism as the published-value snap in publish(): this one makes
        // setAgentWake(i, 1e-8f) exactly dormant, that one makes a near-zero
        // PRODUCT publish exactly zero. Both are required by FR-063 and neither
        // subsumes the other.
        wake_[i] = (w <= kWakeSilenceEpsilon) ? 0.0f : w;
        // gate_ IS DELIBERATELY NOT TOUCHED HERE - the ramp target is re-read at
        // the next publication, which is the whole point of FR-062.
    }

    /// @brief Mark agent @p i dormant (FR-072).
    ///
    /// DORMANCY GATES THE OUTPUT, NOT THE SIMULATION. A dormant agent keeps
    /// grazing, exchanging and leaking; only its published gate is driven to 0.
    /// The cross-cutting Dormancy rule (roadmap lines 543-545) demands the
    /// mechanism-level argument for that, and it is this: THE AGENT *IS* THE
    /// GENERATOR AND THE MODULATION LANE - there is no chain behind it to skip -
    /// and its energy is a share of a CONSERVED budget, so excluding it from the
    /// economy would strand or destroy that share and break FR-023/FR-054 and
    /// every boundedness criterion in the spec. Nothing "burns" that the rule
    /// was written to stop: the per-agent cost IS the simulation, which must run
    /// regardless.
    ///
    /// setAgentWake(i, 0.0f) and setAgentDormant(i, true) fold into one steady
    /// gate target and are behaviourally identical, exactly as
    /// ResonanceDriftNetwork::gateSteady() (:787-790) and
    /// FeedbackEcology::gateSteady() (:2268-2273) make them.
    void setAgentDormant(std::size_t i, bool dormant) noexcept {
        if (i >= agentCount_) {
            return;  // FR-064: an out-of-range index is a silent no-op
        }
        dormant_[i] = dormant;
        // Like setAgentWake(), this advances nothing and touches no gate: the
        // agent keeps grazing, exchanging and leaking (FR-072) and only its
        // published gate target changes, at the next publication.
    }

    /// @brief Move energy between the pool and agent @p i (FR-071).
    ///
    /// @p amount is clamped to [-1, +1]. Positive moves pool -> agent, bounded
    /// by the agent's headroom to capacity and by the (floored) pool; negative
    /// moves agent -> pool, bounded by what the agent holds above its refuge
    /// floor. The transfer is exactly antisymmetric, so the budget is untouched.
    ///
    /// @par THE CORRECTED NEGATIVE BRANCH (plan S14 D-L)
    ///      FR-071's closed formula writes the giving branch as
    ///      `max(a * (e_i - preyFloor), -e_i)`. For `a < 0` and an agent INSIDE
    ///      the refuge (`e_i < preyFloorAbs_`) the inner term is NEGATIVE, so
    ///      `a * negative` is POSITIVE and `max` selects it: a "take energy"
    ///      call then PAYS the agent up to `|a| * preyFloorAbs_` - and, unlike
    ///      the receiving branch, out of a pool with no `max(0, pool_)` guard,
    ///      driving `pool_` negative. At steady state the pool IS empty (the
    ///      agents hold ~96 % of the budget), so that is the NORMAL case, not a
    ///      corner, and SC-001 (d)'s `getConservationViolationCount() == 0`
    ///      would fail by construction. The inner `std::max(0.0, ...)` below
    ///      floors the giving term BEFORE the sign is applied, so delta on this
    ///      branch is always in [-energy_[i], 0]. spec.md FR-071 carries the
    ///      same defect and is corrected in the same pass.
    void perturbAgent(std::size_t i, float amount) noexcept {
        if (i >= agentCount_) {
            return;  // FR-064: an out-of-range index is a silent no-op
        }
        if (!detail::isFinite(amount)) {
            return;  // FR-064 (1): a non-finite amount moves nothing
        }
        const double a = std::clamp(static_cast<double>(amount), -1.0, 1.0);
        const double delta =
            (a >= 0.0)
                ? std::min(a * (capacityAbs_ - energy_[i]), std::max(0.0, pool_))  // pool -> agent
                : std::max(a * std::max(0.0, energy_[i] - preyFloorAbs_),
                           -energy_[i]);  // agent -> pool (S14 D-L)
        // EXACTLY ANTISYMMETRIC: the budget is untouched by construction, which
        // is why this event hook needs no conservation repair afterwards.
        energy_[i] += delta;
        pool_ -= delta;
    }

    [[nodiscard]] float getAgentWake(std::size_t i) const noexcept {
        return (i < agentCount_) ? wake_[i] : 0.0f;
    }
    [[nodiscard]] bool isAgentDormant(std::size_t i) const noexcept {
        return (i < agentCount_) ? dormant_[i] : false;
    }

    // =========================================================================
    // The read surface (FR-060, FR-061)
    // =========================================================================
    // Roadmap lines 386-388 describe the INTENDED USE of this surface - a
    // partial agent feeding a harmonic cloud partial's amplitude, a resonator
    // agent a drift-network peak's wake, a feedback agent an ecology loop's
    // coupling. Those are uses a Phase-10 host composes; this component includes
    // nothing of them, names no consumer type, and is complete without any of
    // them.
    //
    // THE THREE-CLASS GETTER CONTRACT (plan S8), stated ONCE, here. There are
    // three kinds of getter on this class and they have three DIFFERENT
    // contracts; collapsing them into one "a getter returns 0 when it has
    // nothing" rule is what makes such a statement self-contradictory against
    // FR-064.
    //
    //   (i)  INDEXED getters - getAgentOutput / Energy / Kind / PositionX /
    //        PositionY / Phase / Wake / ClampedStepFraction, isAgentDormant,
    //        getCellEnergy, getAgentCountOfKind and getAffinity. Neutral on an
    //        OUT-OF-RANGE INDEX ONLY (0.0f / 0.0 / 0 / Kind::Partial / false),
    //        before AND after prepare(), and they read nothing out of range.
    //        The index is the only thing that makes them neutral: an in-range
    //        index on an unprepared object reports the member it names, which
    //        is the zero/Partial/false every array is value-initialised to.
    //
    //   (ii) STATE getters - everything the simulation PRODUCES or prepare()
    //        DERIVES: getPoolEnergy, getTotalEnergy, getEnergyEntropy,
    //        getStepDurationSeconds, getPairInteractionCount,
    //        getControlStepCount, getAllocatedBytes, isPrepared and the three
    //        FR-065 counters. Neutral on an UNPREPARED object (FR-007), which
    //        has produced nothing.
    //
    //   (iii) KNOB getters - every Appendix-A knob above, getAffinity at an
    //        IN-RANGE Kind pair, AND the prepare-time read-backs below
    //        (getEnergyBudget, getResourceCells, getStepIntervalChunks,
    //        getInitialPoolFraction, getSampleRate, getSeed, getAgentCount).
    //        They have NO NEUTRAL: they always report the CURRENT
    //        CONFIGURATION - the member-initialised Appendix-A / PrepareConfig
    //        default, or the last clamped value a setter stored - prepared or
    //        not. FR-064 requires the setter/getter round trip to hold and
    //        prepare() re-derives state, never configuration; a 0 here would
    //        break that round trip and, for kernelSigma, would report a value
    //        the setter is not even allowed to store.
    //        getStepDurationSeconds is the ONE exception that sits with (ii)
    //        instead: it is DERIVED (dt_), and dt_ is 0.0 until prepare().
    //
    // EcosystemEngine_SettersClampToRange and
    // EcosystemEngine_UnpreparedIsNeutral assert the three classes SEPARATELY.

    /// @brief Agent @p i's published value, in [0, 1] (FR-061, FR-062).
    ///
    /// Recomputed ONLY at a simulation step and HELD between steps. Neutral
    /// 0.0f on an out-of-range index.
    [[nodiscard]] float getAgentOutput(std::size_t i) const noexcept {
        return (i < agentCount_) ? output_[i] : 0.0f;
    }

    [[nodiscard]] double getAgentEnergy(std::size_t i) const noexcept {
        return (i < agentCount_) ? energy_[i] : 0.0;
    }
    [[nodiscard]] Kind getAgentKind(std::size_t i) const noexcept {
        return (i < agentCount_) ? kind_[i] : Kind::Partial;
    }
    [[nodiscard]] double getAgentPositionX(std::size_t i) const noexcept {
        return (i < agentCount_) ? x_[i] : 0.0;
    }
    [[nodiscard]] double getAgentPositionY(std::size_t i) const noexcept {
        return (i < agentCount_) ? y_[i] : 0.0;
    }
    [[nodiscard]] double getAgentPhase(std::size_t i) const noexcept {
        return (i < agentCount_) ? phase_[i] : 0.0;
    }
    [[nodiscard]] std::size_t getAgentCount() const noexcept { return agentCount_; }
    [[nodiscard]] std::size_t getAgentCountOfKind(Kind kind) const noexcept {
        const auto k = static_cast<std::size_t>(kind);
        return (k < kNumKinds) ? kindCount_[k] : std::size_t{0};
    }

    // =========================================================================
    // Prepare-time read-back (FR-060, Clarification Q8)
    // =========================================================================
    // These sit with the KNOBS, not with the state getters (plan S8 (iii)):
    // they are member-initialised to the PrepareConfig defaults and report them
    // before prepare(), because FR-060's contract is "read back what prepare()
    // actually did" and a 0 there would be a value prepare() can never produce.
    // getStepDurationSeconds() is the one exception and belongs to plan S8 (ii):
    // it is DERIVED, and dt_ is 0.0 until prepare() computes it.

    [[nodiscard]] double getEnergyBudget() const noexcept { return energyBudget_; }
    [[nodiscard]] std::size_t getResourceCells() const noexcept { return resourceCells_; }
    [[nodiscard]] std::size_t getStepIntervalChunks() const noexcept { return stepChunks_; }
    [[nodiscard]] double getStepDurationSeconds() const noexcept { return dt_; }
    [[nodiscard]] double getInitialPoolFraction() const noexcept { return initialPoolFrac_; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }

    // =========================================================================
    // Diagnostics (FR-065, FR-066) - plan S8 (ii)
    // =========================================================================

    /// @brief The pool's current energy. MAY BE NEGATIVE - see the pool-update
    ///        note on simulationStep(): a negative pool is a readable, latched
    ///        signal, never a clamped-away one.
    [[nodiscard]] double getPoolEnergy() const noexcept { return prepared_ ? pool_ : 0.0; }

    [[nodiscard]] double getCellEnergy(std::size_t k) const noexcept {
        return (k < resourceCells_) ? res_[k] : 0.0;
    }

    /// @brief agents + cells + pool. SC-001 (c) gates this against
    ///        getEnergyBudget() to 1e-9 relative.
    [[nodiscard]] double getTotalEnergy() const noexcept {
        if (!prepared_) {
            return 0.0;
        }
        double total = pool_;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            total += energy_[i];
        }
        for (std::size_t k = 0; k < resourceCells_; ++k) {
            total += res_[k];
        }
        return total;
    }

    /// @brief The number of agent pairs that survived FR-012's cutoff on the
    ///        most recent simulation step (NOT a cumulative total).
    [[nodiscard]] std::size_t getPairInteractionCount() const noexcept { return pairCount_; }

    /// @brief Steps whose end-of-step pool was negative (FR-056).
    ///
    /// Counts EXACTLY ONE THING. Non-finite containment has its own counter
    /// (FR-083), because one counter carrying two meanings cannot tell SC-001
    /// (b) which failure mode fired.
    [[nodiscard]] std::uint64_t getConservationViolationCount() const noexcept {
        return conservationViolations_;
    }

    /// @brief Times the FR-083 guard ladder repaired a non-finite value.
    [[nodiscard]] std::uint64_t getNonFiniteContainmentCount() const noexcept {
        return nonFiniteContainments_;
    }

    /// @brief Steps on which at least one published output hit the UPPER rail.
    ///
    /// Both rail counters count the UPPER rail ONLY (Clarification Q4). A
    /// published 0.0f from dormancy, wake gating or the FR-063 silence snap is a
    /// DESIGNED state and is never counted.
    [[nodiscard]] std::uint64_t getOutputClampEngagementCount() const noexcept {
        return outputClampEngagements_;
    }

    /// @brief Agent @p i's share of steps whose publication hit the upper rail.
    [[nodiscard]] float getAgentClampedStepFraction(std::size_t i) const noexcept {
        if (i >= agentCount_ || stepCount_ == 0u) {
            return 0.0f;
        }
        return static_cast<float>(static_cast<double>(clampedSteps_[i]) /
                                  static_cast<double>(stepCount_));
    }

    /// @brief Simulation steps since prepare() / reset() / setSeed()
    ///        (plan addition A-3).
    ///
    /// SC-010 (b) asserts this to within one step of
    /// `duration * sampleRate / (stepIntervalChunks * 64)`, and it is the
    /// denominator getAgentClampedStepFraction() divides by.
    [[nodiscard]] std::uint64_t getControlStepCount() const noexcept { return stepCount_; }

    /// @brief Shannon entropy, in bits, of the normalised agent-energy
    ///        distribution (FR-066).
    ///
    /// REPORTED AND NEVER GATED, and the reason is a measurement, not a taste:
    /// it is PERMUTATION-INVARIANT, so it cannot see WHICH agent holds the
    /// energy, and it sat at 4.9-5.0 across regimes differing 5x in per-agent
    /// activity (FINDINGS.md:148-157). D-9 and SC-002/SC-005 forbid gating on
    /// it; the one place the prototype did gate on it is the reason SC-013's
    /// reference figure is annotated as re-measurable.
    [[nodiscard]] double getEnergyEntropy() const noexcept {
        if (!prepared_) {
            return 0.0;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            sum += energy_[i];
        }
        if (sum <= 0.0) {
            return 0.0;
        }
        double h = 0.0;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            const double p = energy_[i] / sum;
            if (p > 0.0) {
                h -= p * std::log2(p);
            }
        }
        return h;
    }

    /// @brief Always 0: this component has no heap term at all (FR-003).
    ///
    /// Deliberately NON-static - the sibling zero-heap component
    /// (resonance_drift_network.h:912) has the identical shape, and the
    /// allocating siblings return a member and so cannot be static. The uniform
    /// shape is what lets the Phase-10 host total its children.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return 0u; }

private:
    // =========================================================================
    // FR-080 salt table (plan S1.6)
    // =========================================================================
    // APPEND ONLY. Renumbering silently changes every trajectory, because each
    // lane's stream is deriveStreamSeed(seed_, salt) (core/random.h:102).
    //
    // The strictly-increasing assert below is LOAD-BEARING and not decoration:
    // Xorshift32::seed() substitutes its own kDefaultSeed for 0
    // (core/random.h:72-74), so two lanes that hashed to 0 would COLLAPSE ONTO
    // ONE STREAM - deriveStreamSeed's 0x2545F491 substitution (core/random.h:112)
    // prevents the zero, and distinct salts are what prevent the collision.
    static constexpr std::size_t kSaltPositions = 0;
    static constexpr std::size_t kSaltEnergies = 1;
    static constexpr std::size_t kSaltFrequencies = 2;
    static constexpr std::size_t kSaltPhases = 3;
    static constexpr std::size_t kSaltKinds = 4;  ///< deal remainder AND the shuffle
    static constexpr std::size_t kSaltDrift = 5;  ///< the ONE persistent stream
    static constexpr std::size_t kSaltResourceFill = 6;
    static constexpr std::size_t kSaltNextFree = 7;
    static_assert(kSaltPositions < kSaltEnergies && kSaltEnergies < kSaltFrequencies &&
                      kSaltFrequencies < kSaltPhases && kSaltPhases < kSaltKinds &&
                      kSaltKinds < kSaltDrift && kSaltDrift < kSaltResourceFill &&
                      kSaltResourceFill < kSaltNextFree,
                  "salt table must be strictly increasing - two lanes sharing a salt "
                  "share a stream");

    // =========================================================================
    // RNG helpers (plan S1.6, addition A-5) - and they are LOAD-BEARING
    // =========================================================================
    // Xorshift32::nextUnipolar() returns `float(next()) * kToFloat` with a FLOAT
    // kToFloat (core/random.h:67, :88), while the prototype computes the same
    // product in doubles (ecosystem-sim.js:44). Using the shipped float accessor
    // for positions and energies would introduce a ~1e-7 relative difference at
    // t = 0 and make every prototype figure incomparable for a reason that has
    // nothing to do with the rules. core/random.h is NOT modified (FR-090):
    // these are local helpers over the shipped next().

    /// == the prototype's kToFloat, exactly.
    static constexpr double kToDouble = 1.0 / 4294967295.0;

    /// @return a draw in (0, 1] - NOT [0, 1). Xorshift32::next() returns
    ///         [1, 2^32-1] (core/random.h:51-55), so zero is unreachable; this
    ///         matches the prototype, which is what makes the initial draws
    ///         comparable.
    [[nodiscard]] static double nextUnipolarD(Xorshift32& r) noexcept {
        return static_cast<double>(r.next()) * kToDouble;
    }

    /// @return a draw in (-1, 1].
    [[nodiscard]] static double nextBipolarD(Xorshift32& r) noexcept {
        return static_cast<double>(r.next()) * kToDouble * 2.0 - 1.0;
    }

    /// @return a draw in (lo, hi] (ecosystem-sim.js:47).
    [[nodiscard]] static double rangeD(Xorshift32& r, double lo, double hi) noexcept {
        return lo + (hi - lo) * nextUnipolarD(r);
    }

    // =========================================================================
    // Toroidal geometry (ecosystem-sim.js:240-246)
    // =========================================================================
    /// @brief Separation on ONE axis of the unit torus, result in [-0.5, +0.5].
    ///
    /// The habitat wraps on both axes (FR-010), so the separation an agent feels
    /// is the SHORTER of the two ways round. Written as the prototype writes it
    /// - two compares, no fmod, no branchless remainder trick - because the
    /// positions it is handed are already confined to [0, 1) by the stage-9
    /// wrap, so exactly one correction can ever be needed.
    [[nodiscard]] static double wrapDelta(double d) noexcept {
        if (d > 0.5) {
            return d - 1.0;
        }
        if (d < -0.5) {
            return d + 1.0;
        }
        return d;
    }

    /// @brief Confine a coordinate to the unit torus. The result is ALWAYS in
    ///        `[0, 1)` - half-open, both ends (FR-010, FR-012, FR-014).
    ///
    /// `t - std::floor(t)` rather than the prototype's `(t + 1) % 1`
    /// (ecosystem-sim.js:497): the two agree exactly over the reachable range,
    /// and this form is also correct below -1, where the prototype's is not.
    ///
    /// THE `>= 1.0` LINE IS NOT DEFENSIVE PADDING. For a tiny negative `t` -
    /// reachable when an agent sitting at exactly 0.0 takes a sub-ULP backward
    /// step - `std::floor(t)` is -1 and `t + 1.0` ROUNDS TO EXACTLY 1.0 in
    /// double. The subtraction alone therefore breaks the half-open contract
    /// every other stage and EcosystemEngine_ConservesOverAShortRun rely on, and
    /// 0.0 is the value the torus identifies 1.0 with.
    [[nodiscard]] static double wrap01(double t) noexcept {
        const double r = t - std::floor(t);
        return (r >= 1.0) ? 0.0 : r;
    }

    // =========================================================================
    // simulationStep() - FR-087's thirteen stages, in order
    // =========================================================================
    /// @brief One control step of the economy. THE STAGE ORDER IS NORMATIVE
    ///        (FR-087, Clarification Q2); a reordering is a spec amendment, not
    ///        an implementation detail.
    ///
    /// The stages themselves land in T004 (the clock) and T008-T011. The
    /// normative prose below is written HERE, with the skeleton, because
    /// tasks.md T003 requires every normative passage in this task and a later
    /// task does not come back for prose. Each block names the line it must sit
    /// on when that line is written.
    ///
    /// @par ON THE EXCHANGE `flow` LINE (stage 2, FR-021)
    ///      **`predation == 0.5f` disables this rule exactly.** The factor
    ///      `(1 - 2*predation)` is zero there, so every recorded flow is `0.0`
    ///      and the exchange rule is silently off while every other stage runs
    ///      normally. This is not hypothetical: an ablation run in the prototype
    ///      reported "no exchange" as **bit-identical to baseline, to every
    ///      printed digit**, because the default at the time *was* 0.5 and the
    ///      rule had been off in the very configuration being validated
    ///      (FINDINGS.md:119-122).
    ///
    /// @par ABOVE THE KURAMOTO BLOCK (stage 2, FR-035)
    ///      **"synchronize is a macro, not a default rule."** Removing sync at
    ///      sigma = 0.03 nearly doubled activity in 1-D (+86 %) and halved
    ///      correlation; re-adding it at the final 2-D defaults costs 18 %
    ///      activity (FINDINGS.md:228-233). It is retained at a default of 0.0f
    ///      so a Phase-10 COHERENCE macro can dial it in deliberately, and the
    ///      `syncRate_ > 0` guard on the sin() is exactly equivalent (the
    ///      product is zero either way).
    ///
    /// @par ABOVE STAGE 7 (FR-052)
    ///      "leakExponent above ~1.3 kills the ecosystem - at per-agent energies
    ///      of ~0.03 a superlinear leak all but vanishes, agents fill to
    ///      capacity and sit (63 % alive in the lowest tercile vs 13 % in the
    ///      highest, run.js:349-353). Phase 10's macros must stay at or below
    ///      1.3; the full [1.0, 2.5] range exists so the fuzz box can prove the
    ///      component stays BOUNDED there, not because the upper half is
    ///      musically usable."
    ///
    /// @par ON THE POOL UPDATE (stage 12, FR-056)
    ///      **The pool is never clamped.** Clamping a negative pool to zero
    ///      would CREATE energy and break the invariant the whole boundedness
    ///      argument rests on; a negative pool is a real design error and the
    ///      metrics must see it. The counter increments ONCE PER STEP WHOSE
    ///      END-OF-STEP POOL IS NEGATIVE - not once per transition, and not once
    ///      per negative read. That choice is recorded here so a later reader
    ///      does not "fix" it: the counter's contract is monotonic and
    ///      non-saturating, SC-001 (b) asserts it is 0, so any per-step-or-finer
    ///      rule satisfies the criterion - and per-step is the one a soak test
    ///      can also read as a RATE.
    ///
    /// @par ON THE PUBLICATION (stage 13, FR-062, FR-070)
    ///      Outputs are recomputed only here and HELD between steps. That is
    ///      required by a consumer, not chosen for tidiness -
    ///      FeedbackEcology::refreshGates()'s `target != lastGateTarget` guard is
    ///      load-bearing, and "a Phase-8 agent writing one loop's wake per block
    ///      would otherwise stretch every OTHER loop's ramp without bound"
    ///      (feedback_ecology.h:2292-2296).
    ///      RAMP QUANTISATION (FR-070): the 50 ms re-entry fade runs on the
    ///      CONTROL-STEP grid, not per sample - `rampSteps_ = max(1, ceil(0.050
    ///      / dt_))`. At the FR-082 default that is 5 steps = 53.33 ms, and above
    ///      stepIntervalChunks ~= 47 at 48 kHz it floors at 1 and the output
    ///      reaches its target in a single step. A "50 ms +/- 1 ms" contract is
    ///      structurally unmeasurable here because this component has no
    ///      per-sample path at all (FR-002).
    void simulationStep() noexcept {
        ++stepCount_;

        // =====================================================================
        // STAGE 1 - the non-finite guard and the FR-083 repair rule
        // =====================================================================
        // It runs BEFORE anything below reads state, which is why the slot is
        // here and not appended later: every stage below is written on the
        // assumption that the values it reads are finite.
        //
        // REPAIR, NOT ABANDONMENT. "Skip the offending write and carry on" is
        // NOT containment: the bad value stays in place, the next step's guard
        // fires on it again, and because FR-062 HOLDS the published outputs
        // between steps the component would freeze at its last publication
        // FOREVER while every finiteness assertion still passed. SC-009 (b)'s
        // last clause ("1000 further steps produce finite in-range outputs that
        // CHANGE") is the case that fails such an implementation.
        //
        // detail::isFinite(double) ONLY - never std::isnan/isinf/isfinite,
        // which fold to constants under -ffast-math on the macOS and Linux legs
        // (core/db_utils.h:125-129, tools/lint-nonfinite-symbols.js).
        {
            bool contained = false;
            // Which agents this step repaired. kMaxAgents == 48 fits a single
            // word, so the record costs one register and no per-step memset; the
            // restore below needs it to know who to charge when the budget
            // cannot fund the repair (see the block at the end of this scope).
            static_assert(kMaxAgents <= 64, "repairedMask is one std::uint64_t");
            std::uint64_t repairedMask = 0u;
            std::size_t repairedAgents = 0u;
            for (std::size_t i = 0; i < agentCount_; ++i) {
                if (!detail::isFinite(energy_[i]) || !detail::isFinite(x_[i]) ||
                    !detail::isFinite(y_[i]) || !detail::isFinite(phase_[i]) ||
                    !detail::isFinite(freq_[i])) {
                    // The repair targets are prepare()'s values FOR THIS INDEX
                    // under the current seed (x0_/y0_/phase0_/freq0_ exist for
                    // exactly this rung), and the mean share for the energy -
                    // the one value that is meaningful without knowing how much
                    // of the budget the rest of the population currently holds.
                    energy_[i] = meanShare_;
                    x_[i] = x0_[i];
                    y_[i] = y0_[i];
                    phase_[i] = phase0_[i];
                    freq_[i] = freq0_[i];
                    // The gate is SNAPPED to its steady target so no half-ramp
                    // is left behind: the agent's published output after the
                    // repair must be the one its repaired energy earns, not a
                    // value mid-way through an FR-070 ramp nobody asked for.
                    gate_[i] = dormant_[i] ? 0.0f : wake_[i];
                    repairedMask |= (std::uint64_t{1} << i);
                    ++repairedAgents;
                    contained = true;
                }
            }
            for (std::size_t k = 0; k < resourceCells_; ++k) {
                if (!detail::isFinite(res_[k])) {
                    res_[k] = 0.0;
                    contained = true;
                }
            }
            if (!detail::isFinite(pool_)) {
                pool_ = 0.0;
                contained = true;
            }
            if (contained) {
                // THE CONSERVED TOTAL, RESTORED EXACTLY. FR-083 says to charge
                // "the difference between the pre-repair and post-repair
                // totals" to the pool; that arithmetic CANNOT BE WRITTEN when
                // the pre-repair total is NaN, because NaN minus anything is
                // NaN (plan S14 D-C). The operative form sets the pool to
                // energyBudget_ - (repaired agents + cells), which re-establishes
                // total == energyBudget_ exactly - which is precisely what
                // SC-001 (c) gates. Intent preserved, arithmetic the only one
                // that exists.
                //
                // THE REPAIR MAY NOT SPEND ENERGY THE BUDGET DOES NOT HAVE.
                // Reinstating a repaired agent at meanShare_ CREATES energy
                // whenever the value it replaced was smaller, and the pool is
                // the residual of a budgeted economy - at steady state the
                // population holds ~97 % of the budget and the pool holds
                // ~0.06 % of it. Charging the whole repair to the pool therefore
                // drives the pool NEGATIVE for roughly any agent that was below
                // the mean (measured at the defaults: pool 6.2e-4, repaired
                // agent 2.51e-2 against meanShare_ 3.125e-2, pool after the
                // charge -4.9e-3). That would trip stage 12's FR-056 counter on
                // a containment event, which is exactly the two-meanings-in-one-
                // counter confusion FR-056 and SC-009 (b) forbid: the counter
                // says "the ECONOMY overdrew the pool", and the repair is not
                // the economy.
                //
                // So the pool's floor is honoured FIRST and the repaired agents
                // are reinstated at meanShare_ *capped by what the budget can
                // fund* - they take the spare energy there is, pro rata, and the
                // pool lands at zero instead of below it. Conservation is still
                // exact (pool_ is set by subtraction, so total == energyBudget_
                // to one rounding), the containment is still a repair and not an
                // abandonment, and FR-056's counter keeps its single meaning.
                // Deviation from FR-083's "reset to the population mean share",
                // recorded deliberately: the mean share is the TARGET, and a
                // target that cannot be paid for is capped rather than funded by
                // an overdraft the spec elsewhere defines as a defect.
                double bodies = 0.0;
                for (std::size_t i = 0; i < agentCount_; ++i) {
                    bodies += energy_[i];
                }
                for (std::size_t k = 0; k < resourceCells_; ++k) {
                    bodies += res_[k];
                }
                double newPool = energyBudget_ - bodies;
                if (newPool < 0.0 && repairedAgents > 0u) {
                    // Every repaired agent was just set to the same meanShare_,
                    // so pro rata is an equal split. It cannot drive one below
                    // zero when the pre-injection state was conserved with a
                    // non-negative pool (the shortfall is then at most
                    // repairedAgents * meanShare_); the std::max is for the case
                    // where it was not - a pool already negative before the
                    // injection stays negative and stays visible.
                    const double perAgent =
                        -newPool / static_cast<double>(repairedAgents);
                    for (std::size_t i = 0; i < agentCount_; ++i) {
                        if ((repairedMask & (std::uint64_t{1} << i)) != 0u) {
                            energy_[i] = std::max(0.0, energy_[i] - perAgent);
                        }
                    }
                    bodies = 0.0;
                    for (std::size_t i = 0; i < agentCount_; ++i) {
                        bodies += energy_[i];
                    }
                    for (std::size_t k = 0; k < resourceCells_; ++k) {
                        bodies += res_[k];
                    }
                    newPool = energyBudget_ - bodies;
                }
                pool_ = newPool;
                // ONE increment per containment EVENT, not per repaired value:
                // the counter answers "did the trap fire on this step", and
                // SC-009 (b) asserts it advanced by exactly 1.
                ++nonFiniteContainments_;
            }
            // ... and the step now PROCEEDS NORMALLY. Nothing below is skipped.
        }

        // ---------------------------------------------------------------------
        // WHY NO OTHER RUNG IS NEEDED (plan S7.3)
        // ---------------------------------------------------------------------
        // After FR-005's prepare-time clamps the economy performs no division
        // by a possibly-zero quantity:
        //   * energyBudget_ >= 1e-3 and agentCount_ >= 1 (prepare() clamps), so
        //     meanShare_ > 0 and publish()'s /energyBudget_ is safe;
        //   * resourceCells_ >= 1, so the cell share is finite;
        //   * dist = std::sqrt(d2) + 1e-9 > 0 always (stage 2/9);
        //   * demandSum > 0 is TESTED before res_[k]/ask (stage 6);
        //   * want > 0 is TESTED before spare/want (stage 3);
        //   * appetiteSum > 0 is TESTED before the FR-057 feed share (stage 7);
        //   * sigmaSq_ > 0 BECAUSE setKernelSigma clamps to [0.01, 0.35] -
        //     EcosystemEngine_SettersClampToRange is what keeps that true, and
        //     an unclamped setKernelSigma(0.0f) would turn stage 6's
        //     w * (d / sigmaSq_) into a division by zero SILENTLY, since every
        //     fuzz box draws its knobs inside their ranges by construction;
        //   * std::pow(0, x) is 0 for x > 0.
        // A non-finite state is therefore reachable only through the test probe
        // or a caller-supplied value, and FR-064 rejects non-finite input at
        // every setter. The ladder above is a TRAP, not a routine repair path -
        // and SC-009 proves the trap fires.

        // =====================================================================
        // STAGE 2 - ONE pair pass, RECORDING ONLY (ecosystem-sim.js:266-321)
        // =====================================================================
        // Nothing in this stage MUTATES the economy. Every exchange flow is
        // recorded into the pair arrays and applied in stage 3, because
        // antisymmetry alone is NOT enough for conservation: the prototype
        // applied flows here, clamped the agents it drove negative to zero and
        // charged the overdraw to the pool - an uncapped withdrawal that drove
        // the pool negative in 468 of 1000 fuzzed configurations.
        pairCount_ = 0u;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            dE_[i] = 0.0;
            fx_[i] = 0.0;
            fy_[i] = 0.0;
            dPhase_[i] = 0.0;
            outflow_[i] = 0.0;
        }
        // Lever E-2: one sin/cos per agent, read by the Kuramoto term below and
        // by stage 4's appetite gate. Both read the phase at the START of the
        // step (it integrates at stage 11), so one table serves both stages.
        refreshPhaseTrig();

        // Step invariants, read ONCE into locals (lever E-3, exact): every
        // store below goes through a member array of this object, and a
        // compiler that cannot prove those stores do not alias the member
        // scalars reloads each scalar on every pair. Same values, same
        // expressions, same association - bit-identical arithmetic.
        const double cutDistSq = cutDistSq_;
        const double invTwoSigmaSq = invTwoSigmaSq_;
        const double exchangeRate = static_cast<double>(exchangeRate_);
        const double exchangeSign = 1.0 - (2.0 * static_cast<double>(predation_));
        const bool crowdOn = crowding_ > 0.0f;
        const double crowding = static_cast<double>(crowding_);
        const double crowdRadius = static_cast<double>(crowdingRadius_);
        const bool syncOn = syncRate_ > 0.0f;
        const double syncRate = static_cast<double>(syncRate_);

        for (std::size_t i = 0; i < agentCount_; ++i) {
            const double xi = x_[i];
            const double yi = y_[i];
            const double ei = energy_[i];
            const auto ki = static_cast<std::size_t>(kind_[i]);
            // Row i's four accumulators live in registers for the whole row
            // (lever E-3, bit-identical): every pair (i, j) with j > i adds to
            // them in the same order it always did, nothing in this row reads
            // or writes the i-side arrays through any other path, and rows
            // i' < i have already stored their contributions before this row
            // loads them. What changes is only that four read-modify-writes
            // through memory per pair become four register adds.
            double fxi = fx_[i];
            double fyi = fy_[i];
            double outflowI = outflow_[i];
            double dPhaseI = dPhase_[i];

            for (std::size_t j = i + 1u; j < agentCount_; ++j) {
                const double dx = wrapDelta(x_[j] - xi);
                const double dy = wrapDelta(y_[j] - yi);
                const double d2 = (dx * dx) + (dy * dy);

                // THE TWO-STAGE CUTOFF (plan S4.0, cost lever L1). std::exp is
                // the dominant per-step cost and at the defaults ~93 % of pairs
                // never survive FR-012's cutoff, so the exp-free pre-test comes
                // FIRST. It is monotone-equivalent to the normative test and the
                // (1 + 1e-9) inflation baked into cutDistSq_ makes it strictly
                // conservative, so no pair with w >= 1e-6 can ever be lost: the
                // interacting SET is exactly the prototype's, and this is a cost
                // lever with NO behavioural term. The normative comparison below
                // is still executed on every survivor, VERBATIM.
                if (d2 > cutDistSq) {
                    continue;  // exp-free pre-test
                }
                const double w = std::exp(-d2 * invTwoSigmaSq);
                if (w < kNeighbourWeightCutoff) {
                    continue;  // FR-012, kept VERBATIM
                }
                const double ej = energy_[j];

                // ---- RULE 1: EXCHANGE, recorded and NOT applied -------------
                // predation == 0.5f DISABLES THIS RULE EXACTLY. The factor
                // (1 - 2*predation) is zero there, so every recorded flow is 0.0
                // and the exchange rule is silently off while every other stage
                // runs normally. Not hypothetical: an ablation run in the
                // prototype reported "no exchange" as BIT-IDENTICAL TO BASELINE,
                // TO EVERY PRINTED DIGIT, because the default at the time WAS
                // 0.5 and the rule had been off in the very configuration being
                // validated (FINDINGS.md:119-122). The sign flips either side of
                // 0.5: +1 is purely diffusive, -1 purely predatory.
                // exchangeSign is the hoisted (1 - 2 * predation); the products
                // associate exactly as written in FR-020.
                const double flow = exchangeRate * w * (ej - ei) * exchangeSign;
                pairI_[pairCount_] = static_cast<std::uint8_t>(i);
                pairJ_[pairCount_] = static_cast<std::uint8_t>(j);
                pairFlow_[pairCount_] = flow;
                ++pairCount_;  // <= kMaxPairs by the S1.2 static_assert
                if (flow > 0.0) {
                    outflow_[j] += flow;
                } else {
                    outflowI -= flow;
                }

                // ---- RULE 2: AFFINITY, scaled by the NEIGHBOUR's energy -----
                // The neighbour's energy, not the agent's: a spent agent stops
                // PULLING, so the topology tracks where the energy actually is.
                const double a =
                    static_cast<double>(affinity_[ki][static_cast<std::size_t>(kind_[j])]);
                const double dist = std::sqrt(d2) + kUnitVectorEpsilon;
                const double ux = dx / dist;
                const double uy = dy / dist;
                // ((a * w) * energy) * u - the association FR-031 writes, with
                // the shared prefixes computed once (E-3, bit-identical).
                const double aw = a * w;
                const double awj = aw * ej;
                const double awi = aw * ei;
                fxi += awj * ux;
                fyi += awj * uy;
                fx_[j] -= awi * ux;
                fy_[j] -= awi * uy;

                // ---- CROWDING: kind- AND energy-independent repulsion -------
                // Attraction with no short-range repulsion collapsed the
                // population into a handful of exactly co-located clumps
                // (measured: 32 agents at six distinct positions after 30 min).
                // A clump of starved agents exerts no force, feels none and
                // never moves again - and co-located agents are ONE modulation
                // signal copied. The repulsion is deliberately independent of
                // both kind and energy so it cannot be switched off by the
                // affinity matrix or starved into ineffectiveness.
                if (crowdOn && dist < crowdRadius) {
                    const double push = crowding * (1.0 - (dist / crowdRadius));
                    fxi -= push * ux;
                    fyi -= push * uy;
                    fx_[j] += push * ux;
                    fy_[j] += push * uy;
                }

                // ---- RULE 3: KURAMOTO phase coupling ------------------------
                // "SYNCHRONIZE IS A MACRO, NOT A DEFAULT RULE" (FR-035).
                // Removing sync at sigma = 0.03 nearly doubled activity in 1-D
                // (+86 %) and halved correlation; re-adding it at the final 2-D
                // defaults costs 18 % activity (FINDINGS.md:228-233). It is
                // retained at a default of 0.0f so a Phase-10 COHERENCE macro
                // can dial it in deliberately.
                //
                // The syncRate_ > 0 guard is lever L2 and is EXACTLY equivalent
                // to the prototype's unconditional sin(): the product is zero
                // either way. It removes 34 sin() calls per step at the
                // defaults. It does NOT help SC-011's worst case, where sync is
                // on by construction.
                //
                // sin(2 pi (phase_j - phase_i)) is taken from the per-agent
                // sin/cos table by the difference identity (lever E-2, ruling
                // 2026-09-16): an algebraic identity, not an approximation,
                // asserted against std::sin by SC-011 (c). 96 transcendental
                // calls per step instead of 1 128 at the worst case.
                if (syncOn) {
                    const double s = pairPhaseSine(i, j);
                    const double coupling = syncRate * w * s;
                    dPhaseI += coupling;
                    dPhase_[j] -= coupling;
                }
            }
            fx_[i] = fxi;
            fy_[i] = fyi;
            outflow_[i] = outflowI;
            dPhase_[i] = dPhaseI;
        }

        // =====================================================================
        // STAGE 3 - EXCHANGE, PASS TWO (FR-023, ecosystem-sim.js:325-347)
        // =====================================================================
        // Scale every agent's outgoing transfers so the total cannot exceed what
        // it holds ABOVE the refuge floor. Each pair is then applied at
        // min(scale_i, scale_j), which is what KEEPS THE FLOW ANTISYMMETRIC:
        // both sides move by the same amount, the pairwise sum is exactly zero
        // in double, and exchange therefore NEVER touches the pool or the field
        // (FR-024). A solvent agent is never throttled - its scale is 1.
        //
        // THE REFUGE (preyFloorAbs_): an agent can only transfer away what it
        // holds above the floor. Without it, predation above ~0.6 drains the
        // weakest agents to zero and KEEPS them there (a rich neighbour takes
        // each joule they graze) - a cliff 0.05 from the default.
        //
        // THE TERNARY IS NORMATIVE AND IS WRITTEN VERBATIM. FR-023 forbids the
        // `min(1, spare/want)` paraphrase BY NAME, and both of its failure modes
        // are reachable at the defaults rather than exotic:
        //   * want == 0 is the COMMON case at kernelSigma = 0.03 (~34 surviving
        //     pairs out of 496) and the ONLY case at kMinAgents = 1. The outer
        //     `want > 0.0` test is what stops 0/0 (NaN) and spare/0 (+/-Inf).
        //   * spare < 0 is routine under a high refuge floor, and
        //     min(1, negative) yields a NEGATIVE scale - every flow in that pair
        //     applied with REVERSED SIGN and arbitrary magnitude, still
        //     antisymmetric and therefore INVISIBLE to SC-001. SC-020 is the
        //     criterion that sees it.
        // divided_ is set on the DIVISION BRANCH ONLY, beside the counter
        // increment, because exchangeDivisions_ is cumulative across agents and
        // cannot express SC-020's PER-AGENT clause "no division is performed
        // when want_i == 0" (plan A-10).
        for (std::size_t i = 0; i < agentCount_; ++i) {
            divided_[i] = false;
        }
        for (std::size_t i = 0; i < agentCount_; ++i) {
            const double want = outflow_[i] * dt_;
            const double spare = energy_[i] - preyFloorAbs_;
            // The nested conditional is deliberate and is NOT a style slip:
            // FR-023 pins this expression "verbatim in this guarded form", and
            // an if/else paraphrase reads the same but stops being the
            // normative text the comment block above argues for.
            // NOLINTBEGIN(readability-avoid-nested-conditional-operator)
            scale_[i] = (want > 0.0 && want > spare)
                            ? (spare > 0.0 ? (++exchangeDivisions_, divided_[i] = true, spare / want)
                                           : 0.0)
                            : 1.0;
            // NOLINTEND(readability-avoid-nested-conditional-operator)
        }
        for (std::size_t p = 0; p < pairCount_; ++p) {
            const std::size_t i = pairI_[p];
            const std::size_t j = pairJ_[p];
            const double s = (scale_[i] < scale_[j]) ? scale_[i] : scale_[j];
            const double f = pairFlow_[p] * s;
            dE_[i] += f;
            dE_[j] -= f;
        }

        // =====================================================================
        // STAGE 4 - the APPETITE GATE (FR-050, ecosystem-sim.js:352-360)
        // =====================================================================
        // Read from the phase at the START of the step; the phase integrates at
        // stage 11. This is the load-bearing rule of the whole economy, not a
        // decoration: removing it costs -96 % activity and freezes 26 of 32
        // agents (FINDINGS.md:264). It is also what couples the sync rule back
        // INTO the energy economy instead of leaving it purely cosmetic.
        //
        // appetiteSum is consumed by stage 7's global feed.
        // sinPhase_[i] IS std::sin(kTwoPi * phase_[i]) at the start of the step
        // (lever E-2's table, filled before stage 2; nothing between there and
        // here writes phase_).
        double appetiteSum = 0.0;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            const double gate = 1.0 + (static_cast<double>(appetiteDepth_) * sinPhase_[i]);
            appetite_[i] = (gate > 0.0) ? gate : 0.0;
            appetiteSum += appetite_[i];
        }

        // =====================================================================
        // STAGE 5 - PROPORTIONAL REGROWTH, pool -> cells (FR-053, FR-054)
        // =====================================================================
        // poolDelta accumulates every movement into or out of the pool across
        // stages 5-8 and is applied ONCE, at stage 12. Accumulating it is what
        // makes each stage's bookkeeping auditable: no stage may touch pool_.
        double poolDelta = 0.0;

        // THE SINGLE WITHDRAWAL BALANCE (FR-054). THE FLOOR IS OUTSIDE THE
        // BRANCH ON PURPOSE, and the reason is on this line rather than only in
        // the plan: FR-054's "never below zero" promise has TWO withdrawal sites
        // - this stage and stage 7's global feed - and a clamp written INSIDE
        // the branch below is unreachable exactly when it matters. With
        // pool_ < 0 (FR-056 permits that and never clamps it) the branch is
        // skipped entirely and a NEGATIVE avail walks into stage 7, where
        // `if (globalFeed > avail)` stops being a min() and becomes an
        // ASSIGNMENT: at the default feedRate = 0 the product is
        // 0.0 * negative = -0.0, `-0.0 > avail` is true, agent 0 is charged the
        // whole deficit in one step, stage 8's zero clamp hands it back, and
        // FR-056's "a negative pool is a readable, latched signal" is undone by
        // wiping one agent's state with NO COUNTER RECORDING IT. Gated by
        // EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent.
        double avail = (pool_ > 0.0) ? pool_ : 0.0;
        if (avail > 0.0) {
            // PROPORTIONAL, NEVER INDEX-ORDER. Index-order regrowth - walking
            // the cells and letting each take what it wants until the balance
            // runs out - is FORBIDDEN BY NAME: at steady state cells 0-19 took
            // every joule and 70 % of the habitat became a permanent desert
            // (FINDINGS.md:192-200). So does the other shape that looks
            // equivalent, TWO INDEPENDENT CAPS against the start-of-step pool:
            // 591 of 1000 fuzzed configurations drove the pool negative
            // (FINDINGS.md:30-34). avail is the ONE running balance for regrowth
            // AND the FR-057 feed, and nothing else may withdraw.
            double want = 0.0;
            for (std::size_t k = 0; k < resourceCells_; ++k) {
                const double room = cellCapAbs_ - res_[k];
                if (room > 0.0) {
                    want += static_cast<double>(regenRate_) * room * dt_;
                }
            }
            // want > avail implies want > 0, so the division is guarded by the
            // comparison itself.
            const double share = (want > avail) ? (avail / want) : 1.0;
            for (std::size_t k = 0; k < resourceCells_; ++k) {
                const double room = cellCapAbs_ - res_[k];
                if (room <= 0.0) {
                    continue;
                }
                const double give = static_cast<double>(regenRate_) * room * dt_ * share;
                if (give <= 0.0) {
                    continue;
                }
                res_[k] += give;
                poolDelta -= give;
                avail -= give;
            }
            // The rounding backstop; the floor above is the guarantee.
            // std::max keeps the NaN behaviour of the `if (avail < 0.0)` form it
            // replaces: with a NaN avail the comparison is false either way and
            // the NaN survives to the stage-13 guard ladder (FR-083).
            avail = std::max(avail, 0.0);
        }

        // =====================================================================
        // STAGE 6 - DEMAND-SCALED GRAZING and the FR-033 foraging gradient
        // =====================================================================
        // Regrowth ran FIRST (FR-087 stage 5, explicit) so a grazed cell cannot
        // be refilled in the step it was grazed.
        //
        // THE FORBIDDEN SHAPE, NAMED: a FIXED AMOUNT PER CELL SPLIT BY DEMAND
        // SHARE. It made a lone grazer eat exactly as much as a crowded one
        // whatever its appetite said, so the FR-050 phase gate only decided who
        // WON a contested cell and every configuration settled at a starvation
        // fixed point (FINDINGS.md:202-207). The amount taken is
        // `grazeRate * res * dt` PER UNIT OF DEMAND, capped at what the cell
        // holds.
        //
        // THE TOUCH LIST is not an optimisation for its own sake. It replaces
        // the prototype's per-cell `Float64Array(n)` allocation
        // (ecosystem-sim.js:409), which this component may not have (FR-003).
        // (The C++ allocation keyword is kept out of this file even in prose:
        // EcosystemEngine_HeaderIncludesOnlyLayerZero scans the WHOLE file for
        // it as a substring, per tasks.md T003.) A reused member scratch
        // array without the list would carry the PREVIOUS cell's demand into the
        // second inner loop, and clearing all 48 entries per cell costs 4608
        // stores a step for nothing. Iterating the list reads only what THIS
        // cell wrote, so the arithmetic is identical and no clearing is needed.
        for (std::size_t i = 0; i < agentCount_; ++i) {
            graze_[i] = 0.0;
            forage_[i] = 0.0;
        }
        lastDenormalSnap_ = 0.0;  // stage 6b's record, cleared at the top of the stage
        // Lever E-1's kernel runs are per step: no run survives the previous
        // step's field.
        for (std::size_t i = 0; i < agentCount_; ++i) {
            cellSeeded_[i] = false;
        }
        // Step invariants into locals (lever E-3, exact - see stage 2; stage 2's
        // `cutDistSq` is still in scope and is reused here).
        const double satiationAbs = satiationAbs_;
        const double sigmaSq = sigmaSq_;
        const double cellCapAbs = cellCapAbs_;
        const bool forageOn = forageRate_ > 0.0f;

        for (std::size_t k = 0; k < resourceCells_; ++k) {
            // An EMPTY cell contributes nothing, but every agent's kernel run
            // (lever E-1, cellKernelWeight) must still be advanced across it, so
            // the skip sits INSIDE the agent loop, after that bookkeeping. What
            // the cell contributes is unchanged: nothing below the skip runs.
            const bool cellLive = res_[k] > 0.0;
            const double cellX = cellPos_[k];
            // FR-033's "how full the cell is", the same quotient for every agent
            // on this cell: computed once per cell instead of once per visit
            // (E-3, bit-identical). Only read on a live cell.
            const double fill = cellLive ? (res_[k] / cellCapAbs) : 0.0;
            std::size_t touchCount = 0;
            double demandSum = 0.0;

            for (std::size_t i = 0; i < agentCount_; ++i) {
                // FR-040's STRIP GEOMETRY: the field is one-dimensional, so the
                // separation that matters is the X one. An agent's y position
                // does not move it toward or away from a cell.
                const double d = wrapDelta(cellX - x_[i]);
                const double d2 = d * d;

                // The same two-stage cutoff as stage 2 (plan S4.0, lever L1):
                // the exp-free pre-test first, the VERBATIM normative test on
                // every survivor. A rejected cell also ENDS the agent's kernel
                // run: the next in-range cell re-seeds it from std::exp.
                if (d2 > cutDistSq) {
                    cellSeeded_[i] = false;
                    continue;
                }
                // == std::exp(-d2 * invTwoSigmaSq_), by the exact grid recurrence
                // (lever E-1, ruling 2026-09-16; SC-011 (c) asserts the equality).
                const double w = cellKernelWeight(i, d, d2);
                if (!cellLive || w < kNeighbourWeightCutoff) {
                    continue;
                }

                // satiationAbs_ == 0 means SATIATION IS OFF (FR-008), not "an
                // agent is infinitely hungry at zero energy" - the division
                // would be by zero.
                const double hunger =
                    (satiationAbs > 0.0) ? std::max(0.0, 1.0 - (energy_[i] / satiationAbs))
                                         : 1.0;

                const double demand = w * appetite_[i] * hunger;
                cellDemand_[i] = demand;
                cellTouched_[touchCount] = static_cast<std::uint8_t>(i);
                ++touchCount;
                demandSum += demand;

                // FR-033: the gradient pulls toward richer cells, scaled by how
                // full the cell is (`fill`, hoisted per cell). sigmaSq_ > 0 is
                // guaranteed by setKernelSigma's [0.01, 0.35] clamp (plan S7.3).
                if (forageOn) {
                    forage_[i] += fill * w * (d / sigmaSq);
                }
            }

            if (demandSum <= 0.0) {
                continue;
            }
            const double perUnitDemand = static_cast<double>(grazeRate_) * res_[k] * dt_;
            const double ask = perUnitDemand * demandSum;
            const double cap = (ask > res_[k]) ? (res_[k] / ask) : 1.0;
            double taken = 0.0;
            for (std::size_t t = 0; t < touchCount; ++t) {
                const std::size_t i = cellTouched_[t];
                const double g = perUnitDemand * cellDemand_[i] * cap;
                graze_[i] += g;
                taken += g;
            }
            res_[k] -= taken;
        }

        // =====================================================================
        // STAGE 6b - the FR-043 DENORMAL CELL SNAP
        // =====================================================================
        // FR-087's stage list does not name FR-043; it is placed HERE (plan S14
        // D-B) because immediately after grazing is the only point at which a
        // cell can have just been driven sub-guard, and it must precede the NEXT
        // step's regrowth read of `room`.
        //
        // THE MAGNITUDE TEST IS TWO-SIDED ON PURPOSE. `res_[k] -= taken` with
        // taken <= res_[k] can still land a few ULP BELOW zero, and returning
        // that negative residue to the pool is what keeps the invariant exact
        // instead of papering over it. The snapped amount goes to poolDelta AND
        // is recorded in lastDenormalSnap_ - dropping it on the floor passes
        // every other criterion in the spec (SC-001's 1e-9 RELATIVE gate on a
        // budget of order 1 sits fourteen decades above 1e-30) and fails only
        // EcosystemEngine_DenormalCellSnapConserves.
        for (std::size_t k = 0; k < resourceCells_; ++k) {
            const double c = res_[k];
            if (c != 0.0 && c < kDenormalCellGuard && c > -kDenormalCellGuard) {
                poolDelta += c;
                lastDenormalSnap_ += c;
                res_[k] = 0.0;
            }
        }

        // =====================================================================
        // STAGE 7 - the FR-057 GLOBAL FEED and the FR-052 NONLINEAR LEAK
        // =====================================================================
        // FR-052's macro band, stated here because the spec makes it header
        // content: `leakExponent` above ~1.3 KILLS THE ECOSYSTEM - at per-agent
        // energies of ~0.03 a superlinear leak all but vanishes, agents fill to
        // capacity and sit (63 % alive in the lowest tercile vs 13 % in the
        // highest, run.js:349-353). Phase 10's macros must stay at or below 1.3;
        // the full [1.0, 2.5] range exists so the fuzz box can prove the
        // component stays BOUNDED there, not because the upper half is musically
        // usable.
        for (std::size_t i = 0; i < agentCount_; ++i) {
            double globalFeed =
                (appetiteSum > 0.0)
                    ? (static_cast<double>(feedRate_) * avail * (appetite_[i] / appetiteSum) * dt_)
                    : 0.0;
            // A min(), AND IT IS ONE ONLY BECAUSE STAGE 5 FLOORED avail AT ZERO
            // OUTSIDE ITS BRANCH. With a negative avail this line is an
            // assignment and the whole deficit lands on agent 0 - see stage 5.
            globalFeed = std::min(globalFeed, avail);
            avail -= globalFeed;

            const double influx = graze_[i] + globalFeed;
            // The leakExponent == 1 fast path is EXACT (pow(x, 1) == x) and it
            // is the default, so std::pow would cost 32-48 calls a step for
            // nothing (plan S12.3 lever L3).
            const double leak =
                (leakExponent_ == 1.0f)
                    ? (static_cast<double>(leakRate_) * energy_[i] * dt_)
                    : (static_cast<double>(leakRate_) *
                       std::pow(energy_[i], static_cast<double>(leakExponent_)) * dt_);

            // dE_[i] * dt_: stage 3's exchange flows are PER-SECOND RATES and
            // are multiplied by dt exactly here. Stage 3's `want = outflow * dt`
            // uses the same convention, which is what makes its solvency scale
            // correct.
            dE_[i] = (dE_[i] * dt_) + influx - leak;

            // ONLY the global feed is debited from the pool. The grazed part
            // already left the cells at stage 6; debiting it twice destroys
            // energy silently (ecosystem-sim.js:467-469).
            poolDelta += leak - globalFeed;
        }

        // =====================================================================
        // STAGE 8 - INTEGRATE, with BOTH clamps returning their delta (FR-055)
        // =====================================================================
        // Neither clamp may silently create or destroy energy: what the zero
        // clamp refuses to take away and what the capacity clamp refuses to hold
        // both go back to the pool. This is half of the conservation argument
        // and SC-001 (c) is what watches it.
        for (std::size_t i = 0; i < agentCount_; ++i) {
            energy_[i] += dE_[i];
            if (energy_[i] < 0.0) {
                poolDelta += energy_[i];
                energy_[i] = 0.0;
            } else if (energy_[i] > capacityAbs_) {
                poolDelta += energy_[i] - capacityAbs_;
                energy_[i] = capacityAbs_;
            }
        }

        // =====================================================================
        // STAGES 9-11 - MOVE, DRIFT, INTEGRATE PHASE (one pass over the agents)
        // =====================================================================
        // Three stages, ONE loop, because each reads only agent i's own state:
        // splitting them into three passes would triple the traversal of x_, y_,
        // freq_ and phase_ for no change in arithmetic. They are kept
        // individually labelled below so the FR-087 stage order stays legible.
        //
        // NOTHING HERE TOUCHES ENERGY. Movement, drift and phase are the
        // component's geometry and timing, not its economy: stage 12's pool
        // update below sees exactly the poolDelta stages 5-8 built, and no term
        // in this block can add to or subtract from it. That separation is why
        // SC-001's conservation bound is unaffected by any movement knob.
        const double maxStep = static_cast<double>(maxSpeed_) * dt_;

        for (std::size_t i = 0; i < agentCount_; ++i) {
            // -------------------------------------------------------------
            // STAGE 9 - velocity, the slew limit, then the FR-010 torus wrap
            // -------------------------------------------------------------
            // FORAGING ENTERS X ONLY (FR-040). The resource field is a
            // one-dimensional STRIP: it has no y gradient to climb, so a y
            // foraging term would be reading a slope that does not exist. The
            // social force fx_/fy_ from stage 2 is 2-D and enters both axes.
            double vx = ((static_cast<double>(moveRate_) * fx_[i]) +
                         (static_cast<double>(forageRate_) * forage_[i])) *
                        dt_;
            double vy = static_cast<double>(moveRate_) * fy_[i] * dt_;

            // The slew limit is on the MAGNITUDE of the step, not per axis: a
            // per-axis clamp would let a diagonal step travel sqrt(2) times the
            // limit and would also bend the direction of travel, turning a
            // speed cap into a silent steering term.
            const double speed = std::sqrt((vx * vx) + (vy * vy));
            if (speed > maxStep) {
                const double kk = maxStep / speed;
                vx *= kk;
                vy *= kk;
            }
            x_[i] = wrap01(x_[i] + vx);
            y_[i] = wrap01(y_[i] + vy);

            // -------------------------------------------------------------
            // STAGE 10 - the FR-013 BOUNDED OU FREQUENCY DRIFT
            // -------------------------------------------------------------
            // THE DRAW IS UNCONDITIONAL (plan addition A-8) - `nz` is taken from
            // driftRng_ for every agent on every step, and only its APPLICATION
            // is guarded on freqDrift_ > 0. The prototype draws inside the guard
            // (ecosystem-sim.js:504), which makes the stream's POSITION a
            // function of a runtime knob: toggling setFreqDrift(0) and back
            // silently re-aligns every subsequent draw, so two runs that agree
            // on every parameter at every instant would still diverge. That
            // breaks the determinism SC-006 and SC-008 rest on the moment a test
            // or a Phase-10 macro touches the knob. Drawing always and applying
            // conditionally is the bloom_engine.h:914-916 rule. At the default
            // (4e-5 > 0) the behaviour is identical, so no prototype figure
            // moves.
            const double nz = nextBipolarD(driftRng_) * static_cast<double>(freqDrift_) * sqrtDt_;
            if (freqDrift_ > 0.0f) {
                // Wiener increment (hence sqrtDt_, precomputed at prepare())
                // plus a weak pull back toward the frequency prepare() dealt
                // this agent - that mean reversion is what makes the walk
                // BOUNDED rather than merely slow, and the clamp is the hard
                // rail underneath it.
                freq_[i] += nz - (0.5 * (freq_[i] - freq0_[i]) * dt_ * 0.01);
                freq_[i] = std::clamp(freq_[i], static_cast<double>(freqLoHz_),
                                      static_cast<double>(freqHiHz_));
            }

            // -------------------------------------------------------------
            // STAGE 11 - the FR-014 PHASE INTEGRATION
            // -------------------------------------------------------------
            // dPhase_[i] is stage 2's accumulated Kuramoto coupling, in the same
            // per-second units as freq_[i], so the two add before the single
            // multiplication by dt.
            phase_[i] = wrap01(phase_[i] + ((freq_[i] + dPhase_[i]) * dt_));
        }

        // =====================================================================
        // STAGE 13 - publication, the wake ramp and the silence snap
        // =====================================================================
        // Its body is publish(), shared with prepare()/reset(); the CALL is at
        // the bottom of this function, after the pool update below, because the
        // publication reads state and writes only output_ and the pool update is
        // the last line that moves any state. `countRails = true`: this IS a
        // simulation step, so the FR-061 upper-rail counters are live here (and
        // only here).

        // =====================================================================
        // STAGE 12 - the POOL UPDATE (FR-056)
        // =====================================================================
        // Every stage above moved energy into or out of poolDelta and NONE of
        // them touched pool_; this is the one line that does. THE POOL IS NEVER
        // CLAMPED - clamping a negative pool to zero would CREATE energy and
        // break the invariant the whole boundedness argument rests on.
        pool_ += poolDelta;

        // The FR-056 counter, and its rule in one line: ONCE PER STEP WHOSE
        // END-OF-STEP POOL IS NEGATIVE - not once per transition, and not once
        // per negative read. The full rationale is on the doxygen above; the
        // short form is that the counter's contract is monotonic and
        // non-saturating, SC-001 (b) asserts it is 0, so any per-step-or-finer
        // rule satisfies the criterion and per-step is the one a soak test can
        // also read as a RATE. It counts EXACTLY ONE THING: non-finite
        // containment has its own counter (FR-083).
        if (pool_ < 0.0) {
            ++conservationViolations_;
        }

        // ---- STAGE 13 (see the block above) ---------------------------------
        publish(true);
    }

    // =========================================================================
    // Sanitise-then-floor helpers (bloom_engine.h:879-882)
    // =========================================================================
    // detail::isFinite, never the std:: predicates: those fold away under
    // -ffast-math on the macOS and Linux legs (tools/lint-nonfinite-symbols.js).

    [[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    // =========================================================================
    // FR-008's share-unit conversion - converted ONCE, in ONE function (plan S5)
    // =========================================================================
    /// @brief Re-derive every absolute quantity from its share-unit knob.
    ///
    /// Called from exactly two places: prepare() (step 4) and each of the four
    /// share-unit setters. NEVER per step - FR-008 says "converted to an absolute
    /// energy quantity exactly once, at prepare()", and because energyBudget_,
    /// agentCount_ and resourceCells_ are all prepare-time (FR-006) the
    /// conversion cannot drift underneath a running simulation.
    ///
    /// This is what makes SC-019 invariant on BOTH its axes: the mean-share rules
    /// scale with energyBudget/agentCount and the cell rule with
    /// energyBudget/resourceCells, so the nine-cell grid is the same dynamical
    /// system rescaled and FR-061's anchor lands at 0.5 by construction.
    void refreshDerivedScales() noexcept {
        meanShare_ = energyBudget_ / static_cast<double>(agentCount_);
        const double cellShare = energyBudget_ / static_cast<double>(resourceCells_);
        preyFloorAbs_ = static_cast<double>(preyFloorShares_) * meanShare_;
        capacityAbs_ = static_cast<double>(capacityShares_) * meanShare_;
        satiationAbs_ = static_cast<double>(satiationShares_) * meanShare_;  // 0 stays 0 = off
        cellCapAbs_ = static_cast<double>(cellCapShares_) * cellShare;
    }

    /// @brief Re-derive the plan S4.0 kernel constants.
    ///
    /// Recomputed when kernelSigma_ changes - in prepare() and in
    /// setKernelSigma() - and NEVER per step. `cutDistSq_` is the exp-free
    /// pre-test bound: 13.815510557964274 == -ln(1e-6), inflated by (1 + 1e-9)
    /// so the pre-test is strictly conservative and the interacting SET stays
    /// exactly the one FR-012's normative `w < kNeighbourWeightCutoff` selects.
    /// The three cell-pitch terms (lever E-1) also depend on resourceCells_,
    /// which prepare() clamps before it calls this.
    void refreshKernelDerivatives() noexcept {
        const double sigma = static_cast<double>(kernelSigma_);
        sigmaSq_ = sigma * sigma;
        twoSigmaSq_ = 2.0 * sigmaSq_;
        invTwoSigmaSq_ = (twoSigmaSq_ > 0.0) ? (1.0 / twoSigmaSq_) : 0.0;
        cutDistSq_ = twoSigmaSq_ * 13.815510557964274 * (1.0 + 1.0e-9);
        cellSpacing_ =
            (resourceCells_ > 0u) ? (1.0 / static_cast<double>(resourceCells_)) : 0.0;
        cellSpacingSq_ = cellSpacing_ * cellSpacing_;
        cellRatioStep_ = std::exp(-2.0 * cellSpacingSq_ * invTwoSigmaSq_);
    }

    /// @brief Lever E-1 (user ruling 2026-09-16, FR-085's "reduce cost"):
    ///        FR-040's cell weight `w = exp(-d^2 / (2 sigma^2))` for agent @p i
    ///        on the cell at signed x-separation @p d (@p d2 = d*d), evaluated
    ///        by the EXACT Gaussian recurrence along the uniform cell grid
    ///        instead of one std::exp per (cell, agent) visit.
    ///
    /// Consecutive cells sit exactly `cellSpacing_` (= h) apart, so
    ///     w(d + h) = w(d) * exp(-(2 d h + h^2) / (2 sigma^2)),
    /// and that ratio itself advances by the constant `cellRatioStep_` =
    /// exp(-h^2 / sigma^2) from one cell to the next. Two std::exp seed a run
    /// of consecutive in-range cells; every further cell of the run costs two
    /// multiplies. At the worst case (48 agents, 96 cells, sigma 0.35, every
    /// visit in range) that is ~192 exp per step instead of 4 608 - the term the
    /// stage probe measured at two thirds of the whole step.
    ///
    /// It is an algebraic identity, NOT an approximation: no table, no
    /// interpolation, rounding-level error only (~n ulp over a run of n cells,
    /// bounded by SC-011 (c) at 1e-12 relative against std::exp on the engine's
    /// own state). FR-040's formula is unchanged; only its evaluation order is.
    ///
    /// The run re-seeds at the torus wrap (d DROPS instead of growing by h) and
    /// after any cell the exp-free pre-test rejected (the caller clears
    /// cellSeeded_[i] there), so the recurrence never crosses a gap. Two
    /// consecutive in-range cells need h <= 2 * cutDist, i.e. h^2/sigma^2 <= 111,
    /// so cellRatioStep_ is never denormal where it is multiplied.
    ///
    /// MUST be called for every (cell, agent) visit IN CELL ORDER, live cell or
    /// not, or a run silently spans a skipped cell: that is why stage 6's
    /// empty-cell skip sits after this call, not before the agent loop.
    [[nodiscard]] KRATE_DETAIL_FORCEINLINE double cellKernelWeight(std::size_t i, double d,
                                                                  double d2) noexcept {
        if (!cellSeeded_[i] || d < cellPrevD_[i]) {
            cellW_[i] = std::exp(-d2 * invTwoSigmaSq_);
            cellRatio_[i] =
                std::exp(-((2.0 * d * cellSpacing_) + cellSpacingSq_) * invTwoSigmaSq_);
            cellSeeded_[i] = true;
        } else {
            cellW_[i] *= cellRatio_[i];
            cellRatio_[i] *= cellRatioStep_;
        }
        cellPrevD_[i] = d;
        return cellW_[i];
    }

    /// @brief Lever E-2 (user ruling 2026-09-16): the per-agent sin and cos of
    ///        `2 pi phase_i` at the START of a step. Stage 2's Kuramoto term
    ///        takes `sin(2 pi (phase_j - phase_i))` from it by the difference
    ///        identity (pairPhaseSine) and stage 4 its appetite sine (FR-050)
    ///        directly. 48 + 48 transcendental calls per step instead of
    ///        1 128 + 48 at the worst case. The cosines are needed only while
    ///        sync is on (FR-035's syncRate > 0).
    void refreshPhaseTrig() noexcept {
        const bool needCos = syncRate_ > 0.0f;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            const double a = kTwoPi * phase_[i];
            sinPhase_[i] = std::sin(a);
            cosPhase_[i] = needCos ? std::cos(a) : 0.0;
        }
    }

    /// @brief FR-035's `sin(2 pi (phase_j - phase_i))` as
    ///        `sin b cos a - cos b sin a` over refreshPhaseTrig()'s table. An
    ///        identity, asserted against std::sin by SC-011 (c).
    [[nodiscard]] double pairPhaseSine(std::size_t i, std::size_t j) const noexcept {
        return (sinPhase_[j] * cosPhase_[i]) - (cosPhase_[j] * sinPhase_[i]);
    }

    /// @brief Clear EVERY counter, public and probe-only (Clarification Q8).
    void clearCounters() noexcept {
        conservationViolations_ = 0u;
        nonFiniteContainments_ = 0u;
        outputClampEngagements_ = 0u;
        exchangeDivisions_ = 0u;
        lastDenormalSnap_ = 0.0;
        stepCount_ = 0u;
        pairCount_ = 0u;
        clampedSteps_.fill(0u);
    }

    // =========================================================================
    // initialiseState() - FR-041's three-way partition (plan S2.2)
    // =========================================================================
    /// @brief Derive the whole simulation state from `seed_` and the current
    ///        prepare-time configuration.
    ///
    /// The draw ORDER is normative: it fixes each stream's position, and a
    /// reordering silently changes every trajectory while every conservation
    /// criterion still passes (ecosystem-sim.js:179-232).
    ///
    /// Six of the seven streams are LOCALS destroyed at the end of this function;
    /// only the drift stream persists, because it is drawn every step
    /// (ecosystem-sim.js:171-177).
    void initialiseState() noexcept {
        Xorshift32 rPos(deriveStreamSeed(seed_, kSaltPositions));
        Xorshift32 rEnergy(deriveStreamSeed(seed_, kSaltEnergies));
        Xorshift32 rFreq(deriveStreamSeed(seed_, kSaltFrequencies));
        Xorshift32 rPhase(deriveStreamSeed(seed_, kSaltPhases));
        Xorshift32 rKind(deriveStreamSeed(seed_, kSaltKinds));
        driftRng_.seed(deriveStreamSeed(seed_, kSaltDrift));
        Xorshift32 rRes(deriveStreamSeed(seed_, kSaltResourceFill));

        // Every slot, including the ones past agentCount_/resourceCells_, so a
        // second prepare() at a SMALLER count cannot leave a stale tail behind
        // for a later prepare() at a larger one to resurrect.
        kind_.fill(Kind::Partial);
        energy_.fill(0.0);
        x_.fill(0.0);
        y_.fill(0.0);
        phase_.fill(0.0);
        freq_.fill(0.0);
        freq0_.fill(0.0);
        x0_.fill(0.0);
        y0_.fill(0.0);
        phase0_.fill(0.0);
        res_.fill(0.0);
        cellPos_.fill(0.0);
        output_.fill(0.0f);
        kindCount_.fill(std::size_t{0});

        // ---- (a) KIND: a STRATIFIED deal, then a Fisher-Yates shuffle ------
        // FR-011 / Clarification Q6, NOT the prototype's i.i.d. draw
        // (ecosystem-sim.js:192). The shuffle consumes rKind AFTER the remainder
        // draws, so both consumers share one stream in a fixed order and no
        // salt is added. Without the shuffle, kind would be a function of agent
        // index and the affinity matrix would see a block structure.
        const std::size_t base = agentCount_ / kNumKinds;
        const std::size_t rem = agentCount_ % kNumKinds;
        std::size_t slot = 0;
        for (std::size_t k = 0; k < kNumKinds; ++k) {
            for (std::size_t c = 0; c < base; ++c) {
                kind_[slot++] = static_cast<Kind>(static_cast<std::uint8_t>(k));
            }
        }
        // FR-011 distributes the remainder ONE KIND AT A TIME: the rem extra
        // agents go to rem DISTINCT kinds, which is what makes the histogram
        // spread (max_k - min_k) at most one. A draw WITH replacement would let
        // two remainder units land on the same kind and widen the spread to 2+.
        // A partial Fisher-Yates over a local kind roster picks rem distinct
        // kinds in exactly rem draws from the same stream, so the remainder
        // consumes rKind at the same rate the shuffle below then continues at.
        std::array<std::size_t, kNumKinds> roster{};
        for (std::size_t k = 0; k < kNumKinds; ++k) {
            roster[k] = k;
        }
        for (std::size_t r = 0; r < rem; ++r) {
            // rem < kNumKinds always, so the unused tail [r, kNumKinds) is never
            // empty and every pick is a kind no earlier remainder unit took.
            const std::size_t span = kNumKinds - r;
            std::size_t j =
                r + static_cast<std::size_t>(std::floor(nextUnipolarD(rKind) *
                                                        static_cast<double>(span)));
            if (j >= kNumKinds) {
                j = kNumKinds - 1u;  // the draw is (0, 1], so u == 1 lands here
            }
            const std::size_t picked = roster[j];
            roster[j] = roster[r];
            roster[r] = picked;
            kind_[slot++] = static_cast<Kind>(static_cast<std::uint8_t>(picked));
        }
        for (std::size_t i = agentCount_ - 1u; i > 0u; --i) {
            auto j = static_cast<std::size_t>(nextUnipolarD(rKind) *
                                              static_cast<double>(i + 1u));
            j = std::min(j, i);  // the draw is (0, 1], so u == 1 lands here
            const Kind swapped = kind_[i];
            kind_[i] = kind_[j];
            kind_[j] = swapped;
        }
        for (std::size_t i = 0; i < agentCount_; ++i) {
            ++kindCount_[static_cast<std::size_t>(kind_[i])];
        }

        // ---- (b) per-agent draws, in this ORDER (ecosystem-sim.js:192-206) --
        for (std::size_t i = 0; i < agentCount_; ++i) {
            x_[i] = nextUnipolarD(rPos);
            x0_[i] = x_[i];
            y_[i] = nextUnipolarD(rPos);  // 2-D always (FR-012)
            y0_[i] = y_[i];
            energy_[i] = nextUnipolarD(rEnergy);  // raw; normalised in (c)
            phase_[i] = nextUnipolarD(rPhase);
            phase0_[i] = phase_[i];
            freq_[i] = rangeD(rFreq, static_cast<double>(freqLoHz_), static_cast<double>(freqHiHz_));
            freq0_[i] = freq_[i];
        }

        // ---- (c) normalise the agent share (ecosystem-sim.js:207-211) ------
        const double agentTarget = energyBudget_ * (1.0 - initialPoolFrac_);
        double energySum = 0.0;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            energySum += energy_[i];
        }
        const double energyScale = (energySum > 0.0) ? (agentTarget / energySum) : 0.0;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            energy_[i] *= energyScale;
        }

        // ---- (d) the SEEDED resource fill (ecosystem-sim.js:219-232) -------
        // CELLS ARE NEVER LEFT EMPTY. Starting the field at zero gave every seed
        // the same opening transient and a cross-seed correlation of 0.60
        // (FR-041); the seeded fill is what decorrelates the opening.
        // cellPos_ is an X COORDINATE ONLY - FR-040's strip geometry.
        for (std::size_t k = 0; k < resourceCells_; ++k) {
            cellPos_[k] = (static_cast<double>(k) + 0.5) / static_cast<double>(resourceCells_);
            res_[k] = nextUnipolarD(rRes) * cellCapAbs_;
        }
        double resSum = 0.0;
        for (std::size_t k = 0; k < resourceCells_; ++k) {
            resSum += res_[k];
        }
        const double remainingEnergy = energyBudget_ - agentTarget;
        const double resTarget = std::min(resSum, remainingEnergy * 0.5);
        const double resScale = (resSum > 0.0) ? (resTarget / resSum) : 0.0;
        for (std::size_t k = 0; k < resourceCells_; ++k) {
            res_[k] *= resScale;
        }
        pool_ = remainingEnergy - resTarget;

        // ---- (e) gates: SNAPPED, never ramped, at t = 0 --------------------
        // Agents come up AWAKE, the way ResonanceDriftNetwork's peaks do
        // (resonance_drift_network.h:1006, :1361). Wake and dormancy are agent
        // STATE (roadmap lines 381-382), so prepare() and reset() re-derive
        // them like every other state field, and the gate is snapped to its
        // steady target so a caller reading getAgentOutput() before the first
        // processChunk() sees the initial state and not a 50 ms window
        // (bloom_engine.h:399-400).
        wake_.fill(1.0f);
        dormant_.fill(false);
        for (std::size_t i = 0; i < kMaxAgents; ++i) {
            gate_[i] = dormant_[i] ? 0.0f : wake_[i];
        }
    }

    // =========================================================================
    // publish() - stage 13's body (plan S4.11)
    // =========================================================================
    /// @brief Recompute every agent's published output from its energy and gate.
    ///
    /// @param countRails whether this publication may engage the FR-061 upper
    ///        rail counters. TRUE only from a simulation step: both counters are
    ///        PER-STEP quantities (getAgentClampedStepFraction() divides by
    ///        getControlStepCount()), so the prepare()/reset() publication -
    ///        which fires no step and leaves the count at zero - must not
    ///        contribute to them.
    ///
    /// `++stepCount_` belongs to simulationStep(), not here, for the same
    /// reason: prepare() publishes without having stepped.
    ///
    /// THE GATE IS INSIDE THE NORMALISATION, exactly as FR-061 writes it.
    /// clamp(raw * gate) and clamp(raw) * gate differ, and only the former makes
    /// a dormant agent's output exactly 0.
    void publish(bool countRails) noexcept {
        const float stepGain = 1.0f / static_cast<float>(rampSteps_);
        bool anyRail = false;
        for (std::size_t i = 0; i < agentCount_; ++i) {
            // gateSteady(): dormancy and wake fold into ONE number, which is
            // what makes setAgentWake(i, 0) and setAgentDormant(i, true)
            // behaviourally identical (resonance_drift_network.h:787-790).
            const float target = dormant_[i] ? 0.0f : wake_[i];
            if (gate_[i] < target) {
                gate_[i] = std::min(target, gate_[i] + stepGain);
            } else if (gate_[i] > target) {
                gate_[i] = std::max(target, gate_[i] - stepGain);
            }

            double raw = kOutputAnchor * energy_[i] * static_cast<double>(agentCount_) /
                         energyBudget_ * static_cast<double>(gate_[i]);
            if (raw > 1.0) {
                if (countRails) {
                    ++clampedSteps_[i];
                    anyRail = true;
                }
                raw = 1.0;
            } else if (raw < 0.0) {
                raw = 0.0;  // unreachable while stage 8's zero clamp holds
            }

            float out = static_cast<float>(raw);
            if (out <= kWakeSilenceEpsilon) {
                out = 0.0f;  // FR-063, snapped AT THE SOURCE
            }
            output_[i] = out;
        }
        if (countRails && anyRail) {
            ++outputClampEngagements_;
        }
    }

    // =========================================================================
    // Configuration, fixed at prepare() (FR-006) - plan S1.5
    // =========================================================================
    bool prepared_ = false;
    double sampleRate_ = kDefaultSampleRate;
    std::size_t agentCount_ = 32;
    std::size_t resourceCells_ = 64;
    std::size_t stepChunks_ = kDefaultStepIntervalChunks;
    double energyBudget_ = 1.0;
    double initialPoolFrac_ = 0.5;
    std::uint32_t seed_ = 0xC0FFEEu;  ///< the prototype's reference seed

    double dt_ = 0.0;              ///< stepChunks_ * 64 / sampleRate_  (FR-082)
    double sqrtDt_ = 0.0;          ///< precomputed for FR-013's OU term
    std::size_t rampSteps_ = 1;    ///< max(1, ceil(0.050 / dt_))       (FR-070)

    // =========================================================================
    // Rule knobs, runtime (FR-064). Defaults = the spec's Appendix A.
    // =========================================================================
    // value                            default    Appendix-A range
    float kernelSigma_ = 0.03f;      // [0.01, 0.35]
    float exchangeRate_ = 0.35f;     // [0, 3.0]
    float predation_ = 0.55f;        // [0, 1]
    float preyFloorShares_ = 0.5f;   // [0, 1.6]     shares
    float capacityShares_ = 32.0f;   // [0.32, 32]   shares
    float leakRate_ = 0.06f;         // [0, 1.0]
    float leakExponent_ = 1.0f;      // [1.0, 2.5];  Phase-10 macros <= 1.3 (FR-052)
    float moveRate_ = 0.20f;         // [0, 0.5]
    float maxSpeed_ = 0.03f;         // [0.001, 0.05]
    float forageRate_ = 0.010f;      // [0, 0.05]
    float crowding_ = 0.05f;         // [0, 0.2]
    float crowdingRadius_ = 0.02f;   // [0.005, 0.05]
    float syncRate_ = 0.0f;          // [0, 0.5]     OFF by default: "synchronize is a
                                     //              macro, not a default rule" (FR-035)
    float cellCapShares_ = 3.2f;     // [0.32, 12.8] cell-shares
    float regenRate_ = 0.05f;        // [0, 1.0]
    float grazeRate_ = 0.75f;        // [0, 3.0]
    float feedRate_ = 0.0f;          // [0, 1.0]     OFF by default (FR-057)
    float satiationShares_ = 0.0f;   // [0, 16]      0 = off
    float appetiteDepth_ = 0.8f;     // [0, 1]
    float freqLoHz_ = 0.0015f;       // [0.0005, 0.005]
    float freqHiHz_ = 0.0180f;       // [0.006, 0.05]
    float freqDrift_ = 0.00004f;     // [0, 0.0002]

    /// The FR-031 affinity matrix, range [kMinAffinity, kMaxAffinity].
    /// Member-initialised to the prototype's pattern (ecosystem-sim.js:154-167):
    /// -1.0 on the diagonal (same kind repels), +0.45 elsewhere. It is a RUNTIME
    /// knob and therefore survives prepare() like every other knob.
    [[nodiscard]] static constexpr std::array<std::array<float, kNumKinds>, kNumKinds>
    defaultAffinity() noexcept {
        std::array<std::array<float, kNumKinds>, kNumKinds> m{};
        for (std::size_t i = 0; i < kNumKinds; ++i) {
            for (std::size_t j = 0; j < kNumKinds; ++j) {
                m[i][j] = (i == j) ? -1.0f : 0.45f;
            }
        }
        return m;
    }
    std::array<std::array<float, kNumKinds>, kNumKinds> affinity_ = defaultAffinity();

    // =========================================================================
    // FR-008 absolute conversions, re-derived on prepare() AND on each
    // share-unit setter (plan S5) - NEVER per step
    // =========================================================================
    double preyFloorAbs_ = 0.0;  ///< preyFloorShares_ * energyBudget_/agentCount_
    double capacityAbs_ = 0.0;   ///< capacityShares_  * energyBudget_/agentCount_
    double satiationAbs_ = 0.0;  ///< satiationShares_ * energyBudget_/agentCount_ (0 = off)
    double cellCapAbs_ = 0.0;    ///< cellCapShares_   * energyBudget_/resourceCells_
    double meanShare_ = 0.0;     ///< energyBudget_ / agentCount_  (FR-061, FR-083)

    // =========================================================================
    // Kernel derivatives, recomputed when kernelSigma_ changes (plan S4.0)
    // =========================================================================
    double twoSigmaSq_ = 0.0;     ///< 2*sigma^2
    double invTwoSigmaSq_ = 0.0;  ///< 1/(2*sigma^2)
    double sigmaSq_ = 0.0;        ///< sigma^2, the FR-033 gradient divisor
    double cutDistSq_ = 0.0;      ///< the exp-free pre-test bound
    double cellSpacing_ = 0.0;    ///< h = 1/resourceCells_, the cell pitch (lever E-1)
    double cellSpacingSq_ = 0.0;  ///< h^2
    double cellRatioStep_ = 0.0;  ///< exp(-h^2/sigma^2), the recurrence's constant ratio step

    // =========================================================================
    // Agent state (structure-of-arrays), all kMaxAgents-sized
    // =========================================================================
    // WHY SoA AND NOT std::array<Agent, 48> (plan S14 D-A): the pair loop reads
    // x_, y_, energy_, kind_, phase_ of two agents at a stride of 1; the cell
    // loop reads x_, energy_, appetite_ of every agent once per cell, 96 times
    // per step. SoA keeps each of those scans in one cache line per 8 agents
    // instead of touching a 100-byte record. The cell loop is the dominant cost,
    // so this is the layout the FR-085 budget depends on - not a premature one.
    // An `Agent` type would then exist only to be decomposed, so it is not
    // introduced at all, and this component adds exactly ONE namespace-scope
    // name to Krate::DSP.
    std::array<Kind, kMaxAgents> kind_{};
    std::array<double, kMaxAgents> energy_{};
    std::array<double, kMaxAgents> x_{}, y_{}, phase_{}, freq_{}, freq0_{};
    /// FR-083's repair targets: the values prepare() produced for this index.
    std::array<double, kMaxAgents> x0_{}, y0_{}, phase0_{};
    std::array<float, kMaxAgents> wake_{};    ///< [0, 1]
    std::array<bool, kMaxAgents> dormant_{};
    std::array<float, kMaxAgents> gate_{};    ///< the FR-070 ramp's current value
    std::array<float, kMaxAgents> output_{};  ///< FR-062's held publication
    std::array<std::uint64_t, kMaxAgents> clampedSteps_{};  ///< FR-061 upper rail, per agent
    std::array<std::size_t, kNumKinds> kindCount_{};        ///< FR-060

    // =========================================================================
    // Resource field + pool
    // =========================================================================
    std::array<double, kMaxResourceCells> res_{}, cellPos_{};
    double pool_ = 0.0;

    // =========================================================================
    // Per-step scratch - MEMBERS, NOT LOCALS (FR-003)
    // =========================================================================
    std::array<double, kMaxAgents> dE_{}, fx_{}, fy_{}, dPhase_{};
    std::array<double, kMaxAgents> outflow_{}, scale_{}, appetite_{}, graze_{}, forage_{};
    std::array<double, kMaxAgents> cellDemand_{};  ///< per-cell demand, reset by the touch list
    /// Lever E-1's per-agent kernel run (cellKernelWeight) and lever E-2's
    /// per-agent sin/cos table (refreshPhaseTrig) - the two exact identities
    /// adopted by the 2026-09-16 ruling on SC-011's measured table.
    std::array<double, kMaxAgents> cellW_{}, cellRatio_{}, cellPrevD_{};
    std::array<bool, kMaxAgents> cellSeeded_{};
    std::array<double, kMaxAgents> sinPhase_{}, cosPhase_{};
    std::array<bool, kMaxAgents> divided_{};       ///< SC-020's per-agent record (plan A-10)
    std::array<std::uint8_t, kMaxAgents> cellTouched_{};  ///< agents with demand on this cell
    std::array<std::uint8_t, kMaxPairs> pairI_{}, pairJ_{};
    std::array<double, kMaxPairs> pairFlow_{};
    std::size_t pairCount_ = 0;

    // =========================================================================
    // The control clock (FR-081) - ABSOLUTE RESIDUES, CARRIED ACROSS CALLS
    // =========================================================================
    std::size_t samplePhase_ = 0;  ///< 0..kControlChunkSamples-1
    std::size_t chunkPhase_ = 0;   ///< 0..stepChunks_-1
    std::uint64_t stepCount_ = 0;

    // =========================================================================
    // Counters (FR-065). Cleared by prepare() / reset() / setSeed().
    // =========================================================================
    std::uint64_t conservationViolations_ = 0;
    std::uint64_t nonFiniteContainments_ = 0;
    std::uint64_t outputClampEngagements_ = 0;
    /// Probe-only, no public getter (plan S7.4): they exist for SC-020 and
    /// SC-021, and a public counter for them would be surface nothing consumes.
    std::uint64_t exchangeDivisions_ = 0;
    double lastDenormalSnap_ = 0.0;

    // =========================================================================
    // RNG: ONE persistent stream (FR-080, kSaltDrift)
    // =========================================================================
    // The other six streams are LOCAL Xorshift32 objects constructed inside
    // initialiseState() and destroyed at its end, matching the prototype
    // (ecosystem-sim.js:171-177). Only the drift stream persists, because it is
    // drawn every step.
    Xorshift32 driftRng_{1u};

    friend struct detail::EcosystemEngineNonFiniteProbe;  // plan S7.4
    friend struct detail::EcosystemEngineInspectProbe;    // plan S7.4
};

}  // namespace Krate::DSP
