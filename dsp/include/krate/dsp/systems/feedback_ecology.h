// ==============================================================================
// Layer 3: System Component - FeedbackEcology
// ==============================================================================
// Up to six coupled micro-feedback loops. Each loop is a filter -> delay ->
// resonator -> DC-blocker chain whose output feeds back into itself and, at a
// far lower amount, into its neighbours; the whole population is held below
// self-oscillation by a five-rung boundedness ladder and an RMS governor.
// Vorago's fourth sound source: it is the organism that sustains, not a delay
// effect - FeedbackNetwork's kMaxFeedback = 1.2f ("120% for self-oscillation",
// feedback_network.h:64) is precisely the contract this component rejects.
//
// Feature: vorago-phase5-feedback-ecology
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept throughout; prepare() is the ONLY
//   allocating method - see getAllocatedBytes below)
// - Principle III: Modern C++ (C++20, RAII, value semantics, no owning ptrs)
// - Principle IX: Layer 3 (composes Layer 0-2 components only)
// - Principle X: DSP Constraints (bounded feedback, de-zippered gates,
//   crossfaded delay motion, DC-blocked loops)
// - Principle XI: Performance Budget (< 1 % CPU per voice at 48 kHz, SC-004)
//
// Reference: specs/vorago-phase5-feedback-ecology/spec.md
//            specs/vorago-phase5-feedback-ecology/plan.md
//
// BUILD STATE (tasks.md T006 - the skeleton pass). What is REAL here:
//   * the FR-001 include list, every S1.2 constant with its two derivation
//     tables, and every static_assert that pins them;
//   * the nested FilterMode / PrepareConfig types and the private Loop struct;
//   * the complete S1.5 member list, INCLUDING the construction-seeded
//     sampleRate_ / maxCutoffHz_ / maxResonanceHz_ (never 0.0f - R-8);
//   * the FR-054 salt table with its overlap static_asserts;
//   * the FR-048 fault-injection probe friend declaration;
//   * the complete public API surface, DECLARED, with the FR-070/FR-071
//     read surface returning the member it names and everything else a
//     documented-neutral stub.
// T007 has since landed: every setter's FR-009 contract, the configuration read
// surface, updateResonator(), applyDefaults(), the user-provided default
// constructor, and the truthful RT60 getter.
// T008 has since landed: prepare()'s 15 steps, reset()'s seven, setSeed()'s
// per-lane stream derivation, clearLoopAudio()'s O(1) read-mute clear,
// snapControlState(), the FR-052/FR-053 lane mapping (mapLaneTargets, laneValue,
// wanderScale), gateSteady() and getAllocatedBytes()'s figure.
// T009 has since landed: processBlock()/processBlockTapped()'s FR-003 guard
// ladder, the FR-007 absolute control grid, renderChunk()'s six S6 steps
// (including the FR-047 trap and the FR-046 ordered clamp), getLoopCurrentDelayMs
// and updateControl()'s NORMATIVE STEP SKELETON - steps (4a), (4c), (4d)'s
// setDelayMs and (4e) are live; steps (1), (2), (3), (4b), (4d)'s crossfade count
// and (5) carry their owner's task number in place.
// T010 has since landed: updateControl() step (1)'s decimated, UNCONDITIONAL
// lane advance, step (4d)'s FR-071 crossfade-ONSET counter, setWanderRate()'s
// FR-055 decimation/smoothness mapping (the single owner of laneDecimation_),
// setWanderEnabled() and the three FR-071 lane getters (getLoopTargetDelayMs,
// getLoopTargetCutoffHz, getLoopCrossfadeCount). Steps (4a) and (4e) - the two
// mappings applied to ALL SIX loops, skipped ones included - were already wired
// by T009 through mapLaneTargets(); T010 is what makes them observably ALIVE.
// T011 has since landed: updateControl() steps (2) and (3) - the 42 control-rate
// smoother advances with advanceControlSmoother()'s bit-exact carve-out, and
// normaliseRows()'s FR-035 row scaling behind the FR-075 count mask - plus the
// three applied-coefficient getters.
// T012 has since landed: updateControl() step (5) - the S5.6 governor law, the
// follower's own finiteness guard (R-16) and the second isFinite test on the
// computed target - plus the two FR-070 governor readings (getGovernorGain
// returns the RAMP's current value, getGovernorRms the follower's). The tracker
// FEED in renderChunk step (4) was already wired by T009; T012 is what turns its
// reading into a gain.
// T013 has since landed: the whole loop life cycle - setLoopWake /
// setLoopDormant, refreshGates() (the FR-064 wake edge and the load-bearing
// target != lastGateTarget re-target guard, both IN THE SETTER), updateControl()
// step (4b)'s FR-063 sleep edge, setNumLoops()'s third mover, and the four
// life-cycle readings (getLoopWakeAmount, isLoopDormant, getLoopGate,
// isLoopEngineActive). Every stub named T013 is now a real body.
//
// NOTHING IS STUBBED OUT ANY MORE. The remaining tasks add TESTS and shared test
// helpers, not component code.
//
// CONSEQUENCE OF THAT ORDERING, kept because it explains the shape of the tests
// T009 wrote and NO LONGER HOLDS: while appliedOwnFb_ / appliedCoupling_ were
// still unwritten, EVERY feedback and coupling coefficient renderChunk read was
// 0.0f and the network was feed-forward. T011 made the coefficients live, so a
// criterion that needs circulating energy (SC-013 (b)'s clamp engagement at an
// in-spec drive) - written against the feed-forward fact in T009 - must be
// re-measured, not trusted.
//
// THREE HELPERS T008 DEFINES THAT A LATER TASK CONSUMES, so that task adds
// CALLERS and never a second definition (a duplicate would be a compile error,
// but the intent belongs in writing):
//   * mapLaneTargets(i) / laneValue() / wanderScale() - T010's updateControl()
//     step 4a maps all six loops through THIS body; prepare() step 15 is only
//     the first caller;
//   * gateSteady(i) - T013 built refreshGates(), the sleep edge and the two life
//     cycle setters ON TOP of this value; prepare() step 12 and reset() step 4
//     are only its first callers;
//   * clearLoopAudio(i) - FR-019's ONE owner, four callers total (prepare()
//     step 13, reset() step 2, updateControl() step 4b's sleep edge and
//     renderChunk() step 3's FR-047 trap). All four are now real.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>          // L0: detail::isFinite/isNaN/isInf, flushDenormal,
                                              //     dbToGain, gainToDb, constexprLn, kLn2
#include <krate/dsp/core/fast_math.h>         // L0: FastMath::fastTanh - FR-042 rung 2 (OQ-2 lever 1)
#include <krate/dsp/core/math_constants.h>    // L0: kPi, kTwoPi
#include <krate/dsp/core/random.h>            // L0: deriveStreamSeed
#include <krate/dsp/primitives/biquad.h>      // L1: Biquad, BiquadCoefficients
#include <krate/dsp/primitives/crossfading_delay_line.h>  // L1
#include <krate/dsp/primitives/dc_blocker.h>  // L1
#include <krate/dsp/primitives/delay_line.h>  // L1: nextPowerOf2 (:26), for FR-082's footprint
#include <krate/dsp/primitives/smoother.h>    // L1: OnePoleSmoother, LinearRamp
#include <krate/dsp/primitives/svf.h>         // L1: SVF, SVFMode
#include <krate/dsp/processors/brownian_drift.h>     // L2
#include <krate/dsp/processors/envelope_follower.h>  // L2: EnvelopeFollower, DetectionMode
#include <krate/dsp/processors/resonator_bank.h>     // L2: rt60ToQ + the resonator constants ONLY

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// WHY primitives/delay_line.h IS LISTED EXPLICITLY (FR-001): getAllocatedBytes()
// computes its figure with Krate::DSP::nextPowerOf2 (delay_line.h:26), which
// reaches this header today only TRANSITIVELY through
// crossfading_delay_line.h:31. A re-plumbing of that header, or an IWYU /
// clang-tidy pass, would break this file on a leg the Windows build cannot catch
// first.
//
// WHY core/audio_constants.h IS NOT LISTED: it carried kMaxAudioFreqHz, and no
// expression here uses it - the cutoff ceiling is SVF::kMaxCutoffRatio * fs
// (prepare() step 3). If a later phase introduces a use, the include comes back
// WITH that use.
//
// WHY THERE IS NO LAYER 3 OR LAYER 4 INCLUDE, and in particular none of
// filter_feedback_matrix.h / feedback_network.h / flexible_feedback_network.h:
// a same-layer include would be LEGAL (tools/lint-layers.js fails only when the
// target layer is HIGHER, and noise_organism.h:108 already does it). The bar is
// not the layer - it is that there is nothing in them to consume.
// FilterFeedbackMatrix<N> carries static_assert(N >= 2 && N <= 4)
// (filter_feedback_matrix.h:72-73) with explicit instantiations for 2/3/4 only,
// so it structurally cannot host kMaxLoops = 6. What transfers from all three is
// TOPOLOGY KNOWLEDGE, cited requirement by requirement, not code.
//
// resonator_bank.h is included for rt60ToQ and the namespace-scope resonator
// constants ONLY. ResonatorBank itself is NEVER instantiated (D-3): a
// single-slot bank costs three OnePoleSmoother advances plus a 16-iteration
// loop with 15 continues per sample to reach one enabled biquad.

namespace Krate::DSP {

namespace detail {
/// @brief FR-048 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A TEST TU.
///
/// FR-047's per-sample non-finite trap inside renderChunk() cannot be reached
/// through the public API: every setter rejects a non-finite argument (FR-009),
/// processBlockTapped sanitises both dry channels per channel before the mono
/// sum, and SVF::process and Biquad::process each reset and return 0.0f on a
/// non-finite input (svf.h:359-363, biquad.h:353-356). The two stages that
/// CANNOT self-heal - DCBlocker, which documents "NaN inputs are propagated"
/// (dc_blocker.h:188), and EnvelopeFollower::processRMS, whose release branch
/// recomputes NaN forever (envelope_follower.h:312-326) - are the only way in,
/// and neither is publicly writable. This friend is therefore the only way rung
/// 5 is testable at all.
///
/// It costs nothing at run time and adds no public surface: the library never
/// defines it, so a shipping build has no way to call it. Pattern quoted from
/// systems/seraphis_engine.h:181-196 (declaration) and :1074 (friend).
///
/// ODR: swept this session - `FeedbackEcologyNonFiniteProbe` has zero matches in
/// dsp/, plugins/ or tools/ outside this phase's own artefacts.
struct FeedbackEcologyNonFiniteProbe;
}  // namespace detail

/// @brief Up to six coupled micro-feedback loops held below self-oscillation.
///
/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 only. NO Layer 3 peer, no
///      Layer 4.
/// @par Real-Time Safety: every method is noexcept, lock-free, exception-free
///      and I/O-free; prepare() is the ONLY method that allocates, and its whole
///      heap term is the six CrossfadingDelayLine ring buffers (FR-081, S10).
/// @par No virtual dispatch anywhere on the control or audio path: BrownianDrift
///      is held by concrete type, never through a ModulationSource& (whose
///      getCurrentValue is virtual, modulation_source.h:37).
class FeedbackEcology {
public:
    // =========================================================================
    // Constants (S1.2)
    // =========================================================================

    // --- topology ------------------------------------------------------------
    /// FR-010, roadmap line 272.
    static constexpr std::size_t kMaxLoops = 6;
    /// FR-007. The control grid is shared with every Vorago sibling.
    static constexpr std::size_t kControlChunkSamples = 64;
    static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

    /// Relative cutoff change below which a control step does NOT push a new
    /// target to the SVF (1e-4 is about a sixth of a cent). SVF::advanceSmoother
    /// early-outs only once |g - gTarget| < 1e-7, which it never reaches while a
    /// slightly different target lands every 64 samples; measured 2026-09-13
    /// (Phase 5 stage probe): six SVFs with the smoother permanently live cost
    /// 40 173 ns per 512-block at 48 kHz, ~13 ns per sample each against ~4 ns
    /// for the core. Pushing only real moves lets the smoother settle in between.
    static constexpr float kCutoffPushRelative = 1e-4f;

    /// FR-055. The slowest wander rate is reached by DECIMATING the lane advance,
    /// not by asking BrownianDrift for a tau it does not have.
    static constexpr std::size_t kMaxLaneDecimation = 17;
    static_assert(static_cast<float>(kMaxLaneDecimation) * BrownianDrift::kTauMax >= 500.0f,
                  "kMaxLaneDecimation must reach the slowest FR-055 wander rate");

    // --- the filter stage (FR-011, FR-012, FR-053) ---------------------------
    static constexpr float kDefaultFilterQ = SVF::kButterworthQ;  // 0.7071067811865476f
    static constexpr float kMinFilterQ = SVF::kMinQ;              // 0.1f
    /// FR-012's rung-1 ceiling. SVF's Lowpass and Highpass mixes peak at Q, so a
    /// Q above Butterworth would put gain > 1 inside a feedback loop. (Bandpass
    /// is constant 0 dB peak gain at ANY Q - svf.h:540-551, :565-572 - so for
    /// that mode the clamp is redundant but harmless.)
    static constexpr float kMaxFilterQ = SVF::kButterworthQ;
    static constexpr float kMinCutoffHz = 20.0f;  // FR-053
    static constexpr std::array<float, kMaxLoops> kDefaultLoopCutoffHz =
        {2400.0f, 1700.0f, 1200.0f, 850.0f, 600.0f, 420.0f};  // FR-053

    // --- the delay stage (FR-020, FR-022) ------------------------------------
    static constexpr float kMinDelayMs = 10.0f;
    static constexpr float kMaxDelayMs = 500.0f;
    static constexpr float kDelayHeadroomMs = 20.0f;
    /// (kMaxDelayMs + kDelayHeadroomMs) / 1000, spelled as the LITERAL float and
    /// held to that derivation by the tolerance static_assert below.
    ///
    /// It may NOT be spelled as the division. Under MSVC /fp:fast the CONSTANT
    /// EVALUATOR does not round a derived float back to float precision, so a
    /// derived `static constexpr float` carries the double value inside every
    /// later constant expression while codegen still emits the correct
    /// 0x3F051EB8. Measured with cl /fp:fast /std:c++20 this session:
    /// `(500.0f + 20.0f) / 1000.0f == 0.52f` is FALSE in a static_assert, as is
    /// `static_cast<float>(0.52) == 0.52f`, yet both values print 0x3F051EB8 at
    /// runtime. A literal is unaffected (`0.52f == 0.52f` holds), which is why
    /// the derivation moves into a tolerance guard - tolerance comparisons are
    /// immune to the extra precision, exact ones are not. Changing kMaxDelayMs
    /// or kDelayHeadroomMs without updating this literal still breaks the build.
    static constexpr float kMaxDelaySeconds = 0.52f;
    static_assert(kMaxDelaySeconds > (kMaxDelayMs + kDelayHeadroomMs) * 0.0009999f
                      && kMaxDelaySeconds < (kMaxDelayMs + kDelayHeadroomMs) * 0.0010001f,
                  "kMaxDelaySeconds must remain (kMaxDelayMs + kDelayHeadroomMs) / 1000");
    /// Equal to CrossfadingDelayLine's own default, restated here so this
    /// component's crossfade survives a change of that default.
    static constexpr float kCrossfadeMs = 20.0f;
    /// FR-022, mutually prime: no two loops' round trips lock into a common period.
    static constexpr std::array<float, kMaxLoops> kDefaultLoopDelayMs =
        {41.0f, 67.0f, 109.0f, 173.0f, 281.0f, 449.0f};

    // --- the resonator stage (FR-013, FR-014, Clarification Q1 / OQ-1) -------
    /// One octave below the matching cutoff.
    static constexpr std::array<float, kMaxLoops> kDefaultLoopResonanceHz =
        {1200.0f, 850.0f, 600.0f, 425.0f, 300.0f, 210.0f};
    /// Seconds. The REQUEST - it is clamped PER CENTRE (see the table below) and
    /// getLoopResonanceRt60() reports the REALISED figure, never this one.
    static constexpr float kDefaultResonanceRt60 = 1.0f;
    static constexpr float kDcBlockerCutoffHz = 10.0f;  // FR-014

    // --- gains and the boundedness ladder (FR-018, FR-035, FR-041..FR-046) ---
    static constexpr float kMinLoopGain = 0.0f;
    /// Roadmap line 272, "gain (< 1)".
    static constexpr float kMaxLoopGain = 0.90f;
    static constexpr float kDefaultLoopGain = 0.72f;
    static constexpr float kDefaultLoopInputGain = 1.0f;  // FR-074
    static constexpr float kMaxCouplingPerPair = 0.5f;    // FR-032
    static constexpr float kDefaultCoupling = 0.04f;      // FR-033, the neighbour ring
    /// FR-035 row-sum ceiling: own feedback + every incoming coupling.
    static constexpr float kMaxTotalLoopGain = 0.95f;
    static constexpr float kOutputClamp = 4.0f;  // FR-046 (noise_organism.h:180)

    // --- the governor (FR-043..FR-045) ---------------------------------------
    /// MEASURED, NOT ASSUMED - see DERIVATION TABLE 3 immediately below. The
    /// threshold names a level on the FR-043 TRACKER (`normGain * sum_i b_i`),
    /// which on this component's default tables sits ~30 dB BELOW the drive that
    /// produced it; the -6.0f this constant carried until the SC-006 sweep was
    /// first run is a level the tracker cannot reach from any in-range input.
    static constexpr float kDefaultGovernorThresholdDb = -52.0f;
    static constexpr float kMinGovernorThresholdDb = -72.0f;
    static constexpr float kMaxGovernorThresholdDb = 0.0f;
    static constexpr float kDefaultGovernorRatio = 8.0f;
    static constexpr float kMinGovernorRatio = 1.0f;
    static constexpr float kMaxGovernorRatio = 20.0f;
    static constexpr float kGovernorMinGain = 0.05f;
    static constexpr float kGovernorAttackMs = 20.0f;
    static constexpr float kGovernorReleaseMs = 800.0f;
    static_assert(kGovernorAttackMs >= EnvelopeFollower::kMinAttackMs
                      && kGovernorAttackMs <= EnvelopeFollower::kMaxAttackMs,
                  "attack inside the follower's range");
    static_assert(kGovernorReleaseMs >= EnvelopeFollower::kMinReleaseMs
                      && kGovernorReleaseMs <= EnvelopeFollower::kMaxReleaseMs,
                  "release inside the follower's range");

    // -------------------------------------------------------------------------
    // DERIVATION TABLE 3: kDefaultGovernorThresholdDb is MEASURED, not chosen
    // (FR-044, SC-006 (a); spec.md Assumption 1).
    //
    // WHAT WENT WRONG WITH -6.0f. Assumption 1 sized the threshold from the
    // component's INPUT level ("roughly -12 dBFS per channel"), but FR-043's
    // tracker does not read the input - it reads normGain * sum_i b_i, the
    // loops' own post-DC-blocker sum. On the default tables every loop carries a
    // Q ~ 100 resonator (kMaxResonatorQ; DERIVATION TABLE 2), whose equivalent
    // noise bandwidths are 3.3-18.8 Hz out of 24 kHz, so a BROADBAND drive
    // reaches the tracker ~30 dB down. The spec anticipated exactly this and
    // fixed the remedy in advance: "re-measure and record kGovernorThresholdDb
    // the Phase-3 way, never move an assertion" (SC-006 (a)). This is that
    // measurement.
    //
    // THE MEASUREMENT. 48 kHz, reference patch (makeReference: defaults,
    // mix = 1, wetGain = 0 dB, seed 0x5EED), white-noise drive seed 0x0D817E,
    // ratio = 1 (governor OFF, so the figures are the network's own), 20 s per
    // step, getGovernorRms() read at the end of each step:
    //
    //   drive dBFS   -40    -34    -28    -22    -16    -10     -4      0
    //   tracker dB  -70.3  -66.5  -57.0  -49.1  -45.2  -40.9  -35.6  -31.0
    //
    // and at the -12 dBFS reference drive the tracker reads -42.3 dB. The
    // tracker-to-drive ratio is therefore about -30 dB, not the ~0 dB the old
    // constant assumed - the whole [-36, 0] dB window sat above every level the
    // tracker can produce, which is why the governor was inert at every setting
    // and rung 3 of FR-041 was decorative.
    //
    // WHY -52.0f AND NOT SOME OTHER NUMBER. It is 10 dB below the tracker's
    // nominal reading (-42.3 dB), which places the knee inside SC-006's sweep
    // with the largest margin available on every arm at once:
    //   * (a) straddle: the -40/-34/-28 dBFS steps read below it (governor
    //         EXACTLY idle - measured gain 1.0f, not 1-epsilon) and the
    //         -22...0 dBFS steps above it. Nearest reading is 2.6 dB away.
    //   * (a) 0 dBFS gain 0.137 - clear of the < 0.6 assertion AND of the
    //         kGovernorMinGain = 0.05 floor, so the arm measures the law rather
    //         than the clamp.
    //   * (b) the 40 dB drive rise buys 20.6 dB of output rise (bound: 28).
    //   * (f) numLoops = 1 and 6 cross at the same sweep step. The two counts'
    //         tracker curves are NOT identical - the single-loop case is loop 0
    //         alone, the widest-band resonator of the six, so it reads ~2.9 dB
    //         hotter than the six-loop average even with FR-043's normGain
    //         applied. -52 dB sits in the (-54.6, -49.2) dB window where both
    //         curves cross between the same pair of steps.
    // Lowering it further keeps every arm satisfied but compresses harder at
    // nominal (measured: -55 dB -> gain 0.32 at the reference drive, -60 dB ->
    // 0.19, against 0.44 here); raising it past ~-44 dB fails (b).
    //
    // kMinGovernorThresholdDb moves -36 -> -72 dB for the same reason: the floor
    // must sit under everything the tracker reads (-70.3 dB at the quietest
    // swept level), or the parameter's bottom half is unreachable in practice.
    // kMaxGovernorThresholdDb stays 0 dB - a threshold above every reachable
    // tracker level IS the "off" end of the control, and ratio = 1 is the
    // documented hard off (FR-044).
    // -------------------------------------------------------------------------

    // --- wander (FR-052, FR-053, FR-055) -------------------------------------
    static constexpr float kDefaultWanderRateHz = 0.03f;
    static constexpr float kMinWanderRateHz = 0.002f;
    static constexpr float kMaxWanderRateHz = 1.0f;
    static constexpr float kMaxDelayWanderFraction = 0.5f;
    static constexpr float kMaxCutoffWanderOctaves = 4.0f;
    static constexpr float kDefaultCutoffWanderOctaves = 0.5f;
    /// FR-023's DERIVED table - see the derivation immediately below.
    static constexpr std::array<float, kMaxLoops> kDefaultDelayWanderFraction =
        {0.16f, 0.10f, 0.06f, 0.04f, 0.03f, 0.02f};

    // -------------------------------------------------------------------------
    // DERIVATION TABLE 1: kDefaultDelayWanderFraction is DERIVED, not chosen
    // (FR-023).
    //
    // BrownianDrift's stationary standard deviation is kInternalStd = 0.5f
    // (brownian_drift.h:100), so the difference between two decorrelated lane
    // readings has sigma_delta = 0.5 * sqrt(2) = 0.707. A crossfade costs a lane
    // displacement of 100 / (fraction * baseMs * 0.001 * fs) - the 100 is
    // CrossfadingDelayLine's kCrossfadeThresholdSamples (:78). 44.1 kHz is the
    // BINDING rate: 100 samples is 2.268 ms there against 2.083 ms at 48 kHz and
    // 0.521 ms at 192 kHz.
    //
    //   loop  base ms  base smp @44.1k  frac  smp/unit lane  d(lane)  in sigma_d  peak swing
    //     0      41        1808.1       0.16      289.3       0.346      0.49       +/-6.6 ms
    //     1      67        2954.7       0.10      295.5       0.338      0.48       +/-6.7 ms
    //     2     109        4806.9       0.06      288.4       0.347      0.49       +/-6.5 ms
    //     3     173        7629.3       0.04      305.2       0.328      0.46       +/-6.9 ms
    //     4     281       12392.1       0.03      371.8       0.269      0.38       +/-8.4 ms
    //     5     449       19800.9       0.02      396.0       0.253      0.36       +/-9.0 ms
    //
    // Every peak swing is strictly inside [kMinDelayMs, kMaxDelayMs], so NO
    // default loop's lane is rectified by the clamp. A flat 0.08 would cost the
    // 41 ms loop d(lane) = 0.69 ~ 1 sigma_delta and make SC-005 a coin flip; a
    // flat 0.16 would swing the 449 ms loop +/-72 ms into the clamp.
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // DERIVATION TABLE 2: kDefaultResonanceRt60 = 1.0f is SILENTLY CLAMPED PER
    // CENTRE, and getLoopResonanceRt60() tells the truth (Clarification Q1).
    //
    // rt60ToQ caps Q at kMaxResonatorQ = 100 (resonator_bank.h:51, :92-99), so
    // the longest reachable ring at a centre f is
    //     rt60_max(f) = 100 * kLn1000 / (kPi * f),  kPi / kLn1000 = 0.4547473.
    //
    //   loop  centre Hz  Q at a 1.0 s request  applied Q        realised RT60
    //     0     1200            545.70            100              0.1833 s
    //     1      850            386.54            100              0.2587 s
    //     2      600            272.85            100              0.3665 s
    //     3      425            193.27            100              0.5174 s
    //     4      300            136.42            100              0.7330 s
    //     5      210             95.50          95.50 (no clamp)   1.0000 s
    //
    // 1.0 s is exactly reachable at the LOWEST default centre and reduced above
    // it - which is why the default is 1.0 s and not 2.0 s.
    // getLoopResonanceRt60(i) reports the REALISED figure, derived back from the
    // APPLIED Q, and is deliberately NOT re-clamped into
    // [kMinDecayTime, kMaxDecayTime] on the way out: a second clamp would restore
    // exactly the lie that getter exists to prevent (S4.2).
    // -------------------------------------------------------------------------

    // --- ramps and smoothing (FR-076) ----------------------------------------
    /// Gates, normGain (noise_organism.h:178).
    static constexpr float kGainRampMs = 50.0f;
    /// mix, wet trim, per-loop input taps.
    static constexpr float kMixRampMs = 20.0f;
    /// ownFb + coupling. CONTROL-rate: these smoothers are configured at
    /// fs / kControlChunkSamples, NOT fs (S5.0 - calculateOnePolCoefficient takes
    /// a PER-SAMPLE rate, smoother.h:77-93, so configuring with fs would make the
    /// realised time constant 64 * 20 ms = 1.28 s).
    static constexpr float kCouplingSmoothMs = 20.0f;
    static constexpr float kGovernorRampMs = 20.0f;

    // --- life cycle and the rate floor ---------------------------------------
    static constexpr float kWakeSilenceEpsilon = 1.0e-6f;  // FR-060
    /// FR-083. NOT 1 Hz: this component's resonator clamp pair is
    /// [kMinResonatorFrequency, kMaxResonatorFrequencyRatio * fs], which INVERTS
    /// below ~44 Hz, and std::clamp with hi < lo is UB that MSVC's <algorithm>
    /// traps with _STL_VERIFY. The static_asserts below pin the ordering at the
    /// floor.
    static constexpr double kMinUsableSampleRate = 8000.0;

    // -------------------------------------------------------------------------
    // DERIVATION TABLE 4: FR-041 RUNG 1 - THE l-INFINITY CONTRACTION, AS
    // ARITHMETIC. FR-041 closes with "The header must carry this arithmetic, not
    // a claim", so the numbers live HERE, in constants the static_asserts below
    // evaluate on every CI leg, and not only in a test that one leg might skip.
    //
    // THE PER-STAGE MAGNITUDE LEDGER, one circulation, settled coefficients:
    //
    //   stage                      peak |H|   why
    //   ------------------------   --------   -------------------------------
    //   SVF, Lowpass/Highpass        1.0      peaks AT Q, and FR-012 clamps Q to
    //                                         kMaxFilterQ == kButterworthQ, so
    //                                         the peak is exactly unity
    //                                         (svf.h:117).
    //   SVF, Bandpass                1.0      constant 0 dB peak at ANY Q
    //                                         (svf.h:540-551, :565-572).
    //   CrossfadingDelayLine         1.0      a pure delay - PLUS an equal-power
    //                                         two-tap blend while a crossfade
    //                                         runs; see the sqrt(2) note below.
    //   Biquad, RBJ bandpass         1.0      "Constant 0 dB peak gain"
    //                                         (biquad.h:71). b0 = alpha,
    //                                         b2 = -alpha over a0 = 1 + alpha
    //                                         (resonator_bank.h:668-682) is
    //                                         unit-magnitude at its centre and
    //                                         strictly less everywhere else, at
    //                                         EVERY Q up to kMaxResonatorQ.
    //   DCBlocker                    > 1      THE ONE EXPANSIVE STAGE.
    //                                         y[n] = x[n] - x[n-1] + R*y[n-1]
    //                                         (dc_blocker.h:194) peaks at
    //                                         Nyquist, where (1 - z^-1)/(1 - R
    //                                         z^-1) at z^-1 = -1 is 2/(1 + R).
    //
    // THE DC BLOCKER PEAK, BOUNDED WITHOUT A RUNTIME exp(). R is
    // exp(-2*pi*fc/fs) (dc_blocker.h:135), and std::exp is not constexpr on
    // MSVC, so kDcBlockerPeakGainBound below uses the elementary inequality
    // exp(-a) >= 1 - a for a >= 0, i.e. 2/(1+R) <= 2/(2-a). It is evaluated at
    // kMinUsableSampleRate, where a is LARGEST and the bound loosest, so the one
    // constant dominates every rate a host can hand us:
    //
    //   fs Hz    a = 2*pi*10/fs   R = exp(-a)   2/(1+R)    bound 2/(2-a)
    //     8000     7.85398e-3      0.9921768    1.003927     1.003940
    //    44100     1.42476e-3      0.9985763    1.000712     1.000713
    //    48000     1.30900e-3      0.9986919    1.000655     1.000655
    //   192000     3.27249e-4      0.9996728    1.000164     1.000164
    //
    // THE ROUND TRIP. Every feedback coefficient - a loop own feedback and every
    // coupling entry arriving at it - sits in FR-015 ONE input sum and is applied
    // ONCE per circulation, and normaliseRows() holds their absolute sum to
    // FR-035 kMaxTotalLoopGain. The induced l-infinity gain of one circulation of
    // the LINEARISED network is therefore
    //
    //   kMaxTotalLoopGain * (DC blocker Nyquist peak)
    //     = 0.95 * 1.000655 = 0.950622  at 48 kHz  (-0.4399 dB per circulation)
    //     = 0.95 * 1.003940 = 0.953743  at  8 kHz  (-0.4114 dB per circulation)
    //
    // Both are STRICTLY BELOW UNITY - that is the whole of rung 1. With no input
    // the state decays geometrically: 0.44 dB per circulation on the default
    // 41 ms loop is 60 dB in 137 circulations, 5.6 s. SC-001 (a) measures that
    // decay rather than merely asserting that nothing exploded.
    //
    // WHAT RUNG 1 DOES NOT COVER, written down so no later reader mistakes this
    // ceiling for a proof of everything (FR-040 forbids dropping a rung because
    // another one covers it):
    //   * WHILE A CROSSFADE RUNS the delay contributes two taps at equal-power
    //     gains cos(t) + sin(t) <= sqrt(2), so for two FULLY CORRELATED taps the
    //     transient factor is 1.4142 on top of the product above
    //     (0.950622 * 1.4142 = 1.3444 > 1). The taps are >= 100 samples apart
    //     (CrossfadingDelayLine::kCrossfadeThresholdSamples) in a decorrelating
    //     network, so this is a BOUND and not a measurement - and FR-042 tanh is
    //     sized against it rather than assuming it away. kDelayCrossfadeMaxGain
    //     carries the figure and the static_assert below states the consequence.
    //   * A RESONATOR RETUNE is a STEPPED hard swap (FR-013, FR-076) between two
    //     independently computed unity-peak coefficient sets, so there is no
    //     interpolated intermediate set for this rung to reason about and no
    //     magnitude excursion at the swap - only the click FR-076 declares.
    //   * NON-FINITE state is rung 5 business (FR-047): a contraction argument
    //     says nothing whatever about a NaN.
    // -------------------------------------------------------------------------

    /// FR-041: the largest angular DC-blocker cutoff this component can reach,
    /// at kMinUsableSampleRate - where the bound below is loosest.
    static constexpr float kDcBlockerMaxOmega =
        kTwoPi * kDcBlockerCutoffHz / static_cast<float>(kMinUsableSampleRate);
    /// FR-041: an UPPER BOUND on the DC blocker Nyquist peak 2/(1+R), via
    /// exp(-a) >= 1 - a so the figure is constexpr on every compiler.
    /// 1.003940 at 8 kHz; 1.000655 at 48 kHz.
    static constexpr float kDcBlockerPeakGainBound = 2.0f / (2.0f - kDcBlockerMaxOmega);
    /// FR-041 RUNG 1: the worst-case single-circulation l-infinity gain of the
    /// linearised network at settled coefficients. 0.953743 at the worst rate.
    static constexpr float kWorstCaseRoundTripGain =
        kMaxTotalLoopGain * kDcBlockerPeakGainBound;
    /// FR-041: the equal-power two-tap blend worst case, cos(t) + sin(t) at
    /// t = pi/4. TRANSIENT, and what FR-042 clip is sized against.
    static constexpr float kDelayCrossfadeMaxGain = 1.41421356f;
    /// FR-042 RUNG 2: |tanh(x)| < 1 for every finite x, and the shipped
    /// FastMath::fastTanh preserves the bound (see its call site in renderChunk).
    static constexpr float kSoftClipCeiling = 1.0f;
    /// FR-017 divisor at the maximum loop count, spelled as a literal for the
    /// same /fp:fast constant-evaluator reason as kMaxDelaySeconds.
    static constexpr float kSqrtMaxLoops = 2.4494897f;
    static_assert(kSqrtMaxLoops * kSqrtMaxLoops > static_cast<float>(kMaxLoops) - 1.0e-5f
                      && kSqrtMaxLoops * kSqrtMaxLoops < static_cast<float>(kMaxLoops) + 1.0e-5f,
                  "kSqrtMaxLoops must remain sqrt(kMaxLoops)");

    static_assert(kMaxFilterQ <= SVF::kButterworthQ,
                  "FR-041 rung 1: above Butterworth the SVF Lowpass and Highpass mixes peak ABOVE "
                  "unity and the in-loop stage ledger stops being non-expansive");
    static_assert(kMaxLoopGain <= kMaxTotalLoopGain,
                  "FR-035: a loop own feedback alone may not exceed the row-sum ceiling");
    static_assert(kMaxLoopGain + static_cast<float>(kMaxLoops - 1) * kMaxCouplingPerPair
                      > kMaxTotalLoopGain,
                  "FR-035 row scaling must be REACHABLE from the public API - if the raw worst "
                  "case already sat under the ceiling, normaliseRows() would be decorative and "
                  "rung 1 would rest on the setter clamps instead of on the ceiling");
    static_assert(kDcBlockerPeakGainBound > 1.0f && kDcBlockerPeakGainBound < 1.01f,
                  "FR-041: the DC blocker is the ONE expansive in-loop stage and its excess over "
                  "unity is small - if this bound moves, the round-trip figure moves with it");
    static_assert(kWorstCaseRoundTripGain < 1.0f,
                  "FR-041 RUNG 1 IS THE PHASE: kMaxTotalLoopGain times the DC blocker Nyquist "
                  "peak must be a STRICT contraction at every accepted sample rate. If this "
                  "fires, the linearised network is no longer guaranteed to decay and no other "
                  "rung restores the guarantee - reduce kMaxTotalLoopGain, never the assertion");
    static_assert(kWorstCaseRoundTripGain * kDelayCrossfadeMaxGain > 1.0f,
                  "FR-040: the mid-crossfade worst case EXCEEDS unity, which is exactly why rungs "
                  "2 and 3 may not be removed on the grounds that rung 1 covers them");
    static_assert(static_cast<float>(kMaxLoops) * kSoftClipCeiling / kSqrtMaxLoops < kOutputClamp,
                  "FR-042/FR-046: sqrt(kMaxLoops) is the tanh-bounded ceiling on the normalised "
                  "wet sum and it must sit UNDER the rung-4 clamp - the clamp is unreachable from "
                  "the audio input at any drive level, and only FR-072 positive trim reaches it");

    /// FR-002: construction is equivalent to prepare(48000.0, PrepareConfig{}).
    /// The rate-derived clamp bounds are SEEDED FROM THIS at construction (S1.5)
    /// and are never left at 0.0f - std::clamp(v, 20.0f, 0.0f) is UB (R-8) and
    /// SC-017 calls every getter on an UNPREPARED instance.
    static constexpr double kConstructionSampleRate = 48000.0;

    static_assert(static_cast<double>(kMaxResonatorFrequencyRatio) * kMinUsableSampleRate
                      > static_cast<double>(kMinResonatorFrequency),
                  "the resonator clamp pair must stay ordered at the lowest accepted rate");
    static_assert(static_cast<double>(SVF::kMaxCutoffRatio) * kMinUsableSampleRate
                      > static_cast<double>(kMinCutoffHz),
                  "the cutoff clamp pair must stay ordered at the lowest accepted rate");
    static_assert(kConstructionSampleRate >= kMinUsableSampleRate,
                  "the construction rate must satisfy both clamp-pair orderings");

    // --- output stage (FR-072) -----------------------------------------------
    static constexpr float kDefaultMix = 0.15f;
    static constexpr float kDefaultWetGainDb = 0.0f;
    static constexpr float kMinWetGainDb = -24.0f;
    static constexpr float kMaxWetGainDb = 24.0f;

    // =========================================================================
    // Nested types (S1.3)
    // =========================================================================

    /// @brief FR-011. APPEND ONLY - this becomes a persisted plugin parameter at
    /// Phase 12, so the underlying values may never be renumbered.
    ///
    /// Deliberately NOT named SVFMode / FilterType at namespace scope: SVFMode
    /// (svf.h:38) and FilterType (biquad.h:68) are BOTH already at namespace
    /// scope in headers this one includes. Nested, the EnvelopeFilter precedent.
    enum class FilterMode : std::uint8_t { Lowpass = 0, Bandpass = 1, Highpass = 2 };

    /// @brief FR-002. Callers MUST use designated initialisers -
    /// PrepareConfig{.numLoops = 5} - so no narrowing conversion hides in a
    /// positional brace init (Clang errors where MSVC does not). Nested,
    /// following NoiseOrganism::PrepareConfig (noise_organism.h:190) and
    /// ResonanceDriftNetwork::PrepareConfig (:297).
    struct PrepareConfig {
        /// Clamped [64, 8192]; retained and reported, but SIZES NOTHING - the
        /// render is per-sample with local dry capture, so processBlock accepts
        /// any numSamples whatever this says (FR-085).
        std::size_t maxBlockSamples = 2048;
        /// Clamped [1, kMaxLoops].
        std::size_t numLoops = kMaxLoops;
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief FR-002. Allocation-free, and equivalent in configuration to
    /// prepare(48000.0, PrepareConfig{}).
    ///
    /// The body is applyDefaults() followed by updateResonator(i) for every loop,
    /// evaluated at kConstructionSampleRate. Both halves are load-bearing (S9):
    ///  * applyDefaults() is the ONE owner of the four per-index default tables,
    ///    which cannot be written as default member initialisers because a member
    ///    initialiser has no loop index. Without it getLoopDelayMs(0) would read
    ///    0.0f on an unprepared instance instead of 41.0f.
    ///  * updateResonator(i) is the ONE rate-dependent thing the constructor must
    ///    still do, because a getter reports it. Without it appliedResonanceQ
    ///    sits at its kMinResonatorQ = 0.1 initialiser and getLoopResonanceRt60()
    ///    on an unprepared instance reports 0.0003 s instead of DERIVATION
    ///    TABLE 2. It is safe here and is NOT an exception to the
    ///    "no audio-object state before prepare()" rule: updateResonator needs
    ///    only sampleRate_ and the two clamp bounds, and all three are seeded at
    ///    construction (S1.5). prepare() step 10 recomputes them at the real rate.
    FeedbackEcology() noexcept {
        applyDefaults();
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            updateResonator(i);
        }
    }

    /// @brief FR-005. The ONLY allocating method. Restores every default table on
    /// EVERY call (FR-005 (c)) - it is not idempotent with respect to
    /// configuration; only reset() is.
    ///
    /// S2's 15 numbered steps, in order. Each comment states WHAT BREAKS IF THE
    /// STEP MOVES; three orderings are load-bearing and are marked
    /// *** FR-005 (a) / (b) / (c) ***.
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // --- 1. Rate. ---------------------------------------------------------
        // WHY THE 8 kHz FLOOR AND NOT 1 Hz (FR-083): this component's resonator
        // clamp pair is [kMinResonatorFrequency, kMaxResonatorFrequencyRatio *
        // fs]. At 1 Hz that pair is [20, 0.45] - INVERTED - and std::clamp with
        // hi < lo is UB that MSVC's <algorithm> traps with _STL_VERIFY. The
        // class-scope static_asserts pin the ordering at the floor.
        // A NON-FINITE request becomes 48 000, NOT the floor: see sanitise().
        sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));
        const auto fs = static_cast<float>(sampleRate_);

        // --- 2. Clamp the caller's request ONCE, here. ------------------------
        // applyDefaults() (step 7) never touches PrepareConfig fields, so this
        // is the single owner of both. Moving it after step 7 would be harmless;
        // moving it after step 6 or step 11 would not - both read config_.
        config_.maxBlockSamples =
            std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});
        config_.numLoops = std::clamp(config.numLoops, std::size_t{1}, kMaxLoops);

        // --- 3. RE-cache the rate-dependent clamp bounds. ---------------------
        // "RE-cache", not "cache": both are SEEDED AT CONSTRUCTION from
        // kConstructionSampleRate (S1.5) and are never 0.0f. Step 1's floor
        // already makes both std::max calls no-ops at every accepted rate; they
        // are written anyway so the ordering std::clamp needs is visible HERE
        // rather than inferred from a constant three pages away.
        // IF THIS MOVES AFTER STEP 10: updateResonator() would clamp against the
        // PREVIOUS rate's ceiling.
        maxCutoffHz_ = std::max(kMinCutoffHz, fs * SVF::kMaxCutoffRatio);
        maxResonanceHz_ = std::max(kMinResonatorFrequency, fs * kMaxResonatorFrequencyRatio);

        // --- 4. The six loops' audio stages, in signal order. -----------------
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            L.svf.prepare(sampleRate_);
            L.svf.enableSmoothing(true);  // FR-011, kDefaultSmoothingTimeSec = 5 ms
            L.delay.prepare(sampleRate_, kMaxDelaySeconds);
            // *** FR-005 (a) IS LOAD-BEARING ***: CrossfadingDelayLine::prepare
            // sets sampleRate_ (:101) and THEN overwrites the crossfade time
            // with its own default (:119). A setCrossfadeTime placed BEFORE
            // prepare() is discarded, and on a fresh object it would have
            // computed its increment against the constructed 44100.0 default.
            L.delay.setCrossfadeTime(kCrossfadeMs);
            L.dcBlocker.prepare(sampleRate_, kDcBlockerCutoffHz);
            // Biquad has no prepare(); its coefficients are written in step 10.
        }

        // --- 5. The twelve wander lanes. --------------------------------------
        // BrownianDrift::setDepth is LEFT AT kDefaultDepth = 1.0 - the
        // FR-052/FR-053 depth terms are this component's own multipliers, and
        // using the lane's internal depth as well would square the control.
        // IF THIS MOVES AFTER STEP 14: BrownianDrift::prepare() calls initState()
        // (:128), which re-seeds from configuredSeed_ - so a lane prepared after
        // setSeed still gets the right stream, but the walk would be rewound
        // twice. Keeping it here makes step 14's "LAST" rule the only rule.
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            loops_[i].delayLane.prepare(sampleRate_);
            loops_[i].cutoffLane.prepare(sampleRate_);
        }

        // --- 6. The governor's tracker. ---------------------------------------
        // maxBlockSize is IGNORED by EnvelopeFollower::prepare (:107) and it
        // allocates nothing; it is passed because that is the signature.
        follower_.prepare(sampleRate_, config_.maxBlockSamples);
        follower_.setMode(DetectionMode::RMS);
        follower_.setAttackTime(kGovernorAttackMs);
        follower_.setReleaseTime(kGovernorReleaseMs);
        // FR-043: the governor MUST see the sub content, which is Vorago's
        // identity. The follower's sidechain highpass would hide exactly the
        // band that runs away first.
        follower_.setSidechainEnabled(false);

        // --- 7. applyDefaults(). *** FR-005 (c) *** ---------------------------
        // EVERY call, including a re-prepare on a live, previously configured
        // object, restores the four per-index tables, the FR-033 coupling ring,
        // the gains, the output stage and the governor controls - DISCARDING
        // whatever the caller had configured. prepare() is therefore NOT
        // idempotent with respect to configuration; only reset() is (FR-004).
        // It does NOT touch seed_ (step 14 re-applies it) and does NOT touch
        // PrepareConfig fields (step 2 owns those).
        // IF THIS MOVES AFTER STEP 11 OR 12: every smoother and ramp would be
        // snapped to the OLD configuration and then silently glide to the
        // restored defaults over the following 20-50 ms.
        applyDefaults();

        // --- 8. prepared_. ----------------------------------------------------
        // READABILITY ONLY - this step is order-INDEPENDENT. NO SETTER IS GATED
        // ON prepared_ (FR-006 "callable at any time"; FR-009's contract has
        // exactly three rejection rules and "unprepared" is not one of them).
        prepared_ = true;

        // --- 9. The wander rate. ----------------------------------------------
        // setWanderRate is THE SINGLE OWNER of laneDecimation_ and of every
        // lane's setSmoothness (FR-055), so prepare() and a later caller cannot
        // disagree about the mapping. It must run AFTER step 5 - setSmoothness
        // writes coefficients BrownianDrift::prepare recomputes.
        setWanderRate(kDefaultWanderRateHz);

        // --- 10. Resonator coefficients (S4.1). -------------------------------
        // AFTER step 3 (it clamps against maxResonanceHz_) and AFTER step 7 (it
        // reads the restored resonanceHz / resonanceRt60Req).
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            updateResonator(i);
        }

        // --- 11. Control-rate smoothers, at fs / kControlChunkSamples. --------
        // NOT fs (S5.0): calculateOnePolCoefficient takes a PER-SAMPLE rate
        // (smoother.h:77-93), so configuring with fs would make the realised
        // time constant 64 * 20 ms = 1.28 s.
        // They are SNAPPED, not ramped, which is what makes a configuration
        // chosen through PrepareConfig exact from sample 0.
        const float controlRate = fs / static_cast<float>(kControlChunkSamples);
        for (std::size_t from = 0; from < kMaxLoops; ++from) {
            loops_[from].ownFbSmoother.configure(kCouplingSmoothMs, controlRate);
            loops_[from].ownFbSmoother.snapTo(loops_[from].ownFbTarget);
            for (std::size_t to = 0; to < kMaxLoops; ++to) {
                // The diagonal is configured and snapped too: it is never READ
                // (a loop's own feedback is ownFbSmoother), and a six-entry
                // exception would be more code than the six stores it saves.
                couplingSmoother_[from][to].configure(kCouplingSmoothMs, controlRate);
                couplingSmoother_[from][to].snapTo(couplingTarget_[from][to]);
            }
            countSmoother_[from].configure(kGainRampMs, controlRate);
            countSmoother_[from].snapTo(from < config_.numLoops ? 1.0f : 0.0f);
        }
        // ONE normaliseRows() so appliedOwnFb_ / appliedCoupling_ /
        // appliedTotalGain_ are correct BEFORE THE FIRST SAMPLE - the per-sample
        // path reads them directly (S6 step 2) and the first control step does
        // not run until sample 0 of the first block.
        normaliseRows();

        // --- 12. Per-sample ramps, at fs, SNAPPED. ----------------------------
        // The FlexibleFeedbackNetwork::snapParameters() idiom (:453): a prepared
        // network must be able to reach steady state without a ramp.
        normGainRamp_.configure(kGainRampMs, fs);
        normGainRamp_.snapTo(1.0f / std::sqrt(static_cast<float>(config_.numLoops)));
        governorRamp_.configure(kGovernorRampMs, fs);
        governorRamp_.snapTo(1.0f);
        mixRamp_.configure(kMixRampMs, fs);
        mixRamp_.snapTo(mix_);
        wetGainRamp_.configure(kMixRampMs, fs);
        wetGainRamp_.snapTo(dbToGain(wetGainDb_));
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            L.inputRamp.configure(kMixRampMs, fs);
            L.inputRamp.snapTo(L.inputGain);
            L.gate.configure(kGainRampMs, fs);
            const float steady = gateSteady(i);
            L.gate.snapTo(steady);
            // lastGateTarget MUST start in sync, or the first refreshGates()
            // would skip a real re-target (S7.3).
            L.lastGateTarget = steady;
            L.engineActive = (steady > 0.0f);
        }

        // --- 13. Counters, grid, previous-sample vectors, footprint. ----------
        controlPhase_ = 0;
        laneCounter_ = 0;
        clampEngagements_ = 0;
        nonFiniteResets_ = 0;
        prevY_.fill(0.0f);
        prevOut_.fill(0.0f);
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            clearLoopAudio(i);   // FR-019's one owner
            // The full O(buffer) wipe, which clearLoopAudio deliberately does
            // NOT do (S3.1): prepare() is a control-thread call, not an
            // audio-thread one, and a literally zeroed buffer is what SC-009
            // (b)'s reproducibility needs.
            L.delay.reset();
            L.readMuteSamples = 0;  // nothing stale is left to mute after the wipe
            L.crossfadeCount = 0;
            L.lastCrossfading = false;
        }
        // S10 / FR-082. kMaxLoops, NOT numLoops: all six lines are prepared
        // regardless of the count because setNumLoops must not allocate.
        allocatedBytes_ =
            kMaxLoops
            * nextPowerOf2(static_cast<std::size_t>(
                               sampleRate_ * static_cast<double>(kMaxDelaySeconds))
                           + 1u)
            * sizeof(float);

        // --- 14. setSeed(seed_). *** FR-005 (b) *** LAST. ---------------------
        // BrownianDrift::setSeed reseeds the RNG (:145-148) and the mandatory
        // per-lane reset() that follows snaps the output smoother, so a seed
        // distributed before step 5's lane prepare() would be discarded.
        setSeed(seed_);

        // --- 15. snapControlState(). ------------------------------------------
        // Without it the first control step would issue a setDelayMs against
        // taps prepare() left at 0 samples, and EVERY loop would open with a
        // spurious crossfade from silence.
        snapControlState();
    }

    /// @brief FR-004. Rewinds all audio and modulation state while PRESERVING
    /// configuration, so Phase 10 can reset all three siblings at one moment.
    ///
    /// S3.2's seven steps. It does NOT restore the FR-013/FR-022/FR-033/
    /// FR-052/FR-053 default tables, the coupling matrix, the loop gains, mix or
    /// wetGain - prepare() is the only path back to those (FR-005 (c)).
    ///
    /// It is the test suite's only exact way to put a configuration in force
    /// from sample 0: it snaps every per-sample ramp (inputRamp included) and
    /// every control-rate smoother (couplingSmoother_ included) to the value the
    /// caller most recently set, and clears all audio in the same breath.
    void reset() noexcept {
        // 1. Counters and the absolute control grid.
        controlPhase_ = 0;
        laneCounter_ = 0;
        clampEngagements_ = 0;
        nonFiniteResets_ = 0;

        // 2. Audio state. The extra delay.reset() is the O(buffer) wipe S3.1
        //    keeps OFF the audio thread; reset() is a control-thread call.
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            clearLoopAudio(i);
            L.delay.reset();
            L.readMuteSamples = 0;
            L.crossfadeCount = 0;
        }

        // 3. The governor's tracker.
        follower_.reset();

        // 4. Per-sample ramps SNAPPED, not ramped.
        normGainRamp_.snapTo(1.0f / std::sqrt(static_cast<float>(config_.numLoops)));
        governorRamp_.snapTo(1.0f);
        mixRamp_.snapTo(mix_);
        wetGainRamp_.snapTo(dbToGain(wetGainDb_));
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            L.inputRamp.snapTo(L.inputGain);
            const float steady = gateSteady(i);
            L.gate.snapTo(steady);
            L.lastGateTarget = steady;
            L.engineActive = (steady > 0.0f);
        }

        // 5. Control-rate smoothers snapped to their targets.
        for (std::size_t from = 0; from < kMaxLoops; ++from) {
            loops_[from].ownFbSmoother.snapTo(loops_[from].ownFbTarget);
            for (std::size_t to = 0; to < kMaxLoops; ++to) {
                couplingSmoother_[from][to].snapTo(couplingTarget_[from][to]);
            }
            countSmoother_[from].snapTo(from < config_.numLoops ? 1.0f : 0.0f);
        }
        // The applied coefficients follow the just-snapped smoothers, so a
        // configuration written before reset() is in force from sample 0.
        normaliseRows();

        // 6. THE ONE LINE THAT MAKES A RENDER REPRODUCIBLE FROM THE TOP
        //    (SC-009 (b)): every lane stream is re-derived from the stored seed.
        setSeed(seed_);

        // 7. The control state, snapped from the just-rewound lanes.
        snapControlState();
    }

    /// @brief The RT-SAFE half of reset(): clears every OWNED loop's audio with
    /// NO O(buffer) wipe. Added for Vorago Phase 10's voice steal and deferred
    /// non-finite-recovery paths (plan B-7, specs/vorago-phase10-voice-engine).
    ///
    /// It calls clearLoopAudio(i) for i < config_.numLoops and NOTHING ELSE: no
    /// delay.reset(), no counter cleared, no configuration touched, no smoother
    /// snapped, no lane reseeded, no control residue moved. Safe on the audio
    /// thread, which reset() deliberately is not (see its step 2 above:
    /// "reset() is a control-thread call").
    ///
    /// WHY DROPPING THE O(buffer) WIPE IS SOUND, not a corner cut. reset()'s
    /// extra L.delay.reset() is a std::fill over the whole power-of-two ring -
    /// 131 072 B per loop at 44.1/48 kHz and 524 288 B at 192 kHz - so six loops
    /// clear ~786 KB at 48 kHz and ~3.1 MB at 192 kHz, against an 8 889 ns
    /// control-chunk budget that a SINGLE 131 KB fill already exceeds (the
    /// measurement is clearLoopAudio()'s own, below). clearLoopAudio does not
    /// zero the ring: it installs a READ-MUTE WINDOW OF EXACTLY ONE DELAY LENGTH
    /// with the write position frozen while the window runs (renderChunk's
    /// read-mute branch and updateControl step (4d)). During the window the read
    /// contributes 0; after it, every sample the read tap can reach was WRITTEN
    /// AFTER the clear. A stale - or non-finite - sample already in the ring is
    /// therefore never read, which is the entire property the wipe was buying.
    ///
    /// The bound is config_.numLoops, NOT kMaxLoops: an out-of-count loop is
    /// gated to silence by FR-075 (gateSteady) and was already cleared by its
    /// sleep edge, so clearing it again would cost work on the audio thread and
    /// buy nothing.
    void silenceAudio() noexcept {
        if (!prepared_) { return; }
        for (std::size_t i = 0; i < config_.numLoops; ++i) { clearLoopAudio(i); }
    }

    /// @brief FR-003. Renders one block, overwriting both outputs. The inputs may
    /// alias the outputs in either pairing.
    ///
    /// It FORWARDS to processBlockTapped with loopTaps = nullptr - ONE function
    /// body, which is what makes SC-016's tap/no-tap bit-identity STRUCTURAL
    /// rather than a coincidence two parallel bodies would have to maintain.
    void processBlock(const float* inL, const float* inR, float* outL, float* outR,
                      std::size_t numSamples) noexcept {
        processBlockTapped(inL, inR, outL, outR, nullptr, numSamples);
    }

    /// @brief FR-003 + the observation-only per-loop taps. `loopTaps` may be
    /// nullptr, and any individual tap pointer may be nullptr.
    ///
    /// FR-003's GUARD LADDER, in exactly this order (SC-024):
    ///   1. any null channel pointer  -> return, writing NOTHING and advancing
    ///      NOTHING (not even one control step);
    ///   2. numSamples == 0           -> return, consuming NO control step. The
    ///      residue below is absolute, so a zero-length call must not be able to
    ///      move it;
    ///   3. !prepared_                -> exactly numSamples zeros on both
    ///      channels and no state advance. Not silence-by-rendering: an
    ///      unprepared CrossfadingDelayLine has no buffer.
    /// The order matters: a null pointer with numSamples == 0 must not be
    /// dereferenced by std::fill_n, and an unprepared instance must still honour
    /// the null contract.
    ///
    /// THE CONTROL GRID IS AN ABSOLUTE RESIDUE CARRIED ACROSS CALLS (FR-007,
    /// S5.1), never a block-relative grid: a 36 + 28 split runs ONE control step,
    /// exactly as an unsplit 64 does, and SC-010 / SC-024 (b) are the criteria
    /// that catch the block-relative shape. Phase-3 precedent,
    /// resonance_drift_network.h:530-546.
    ///
    /// `loopTaps`, when non-null, must point at kMaxLoops pointers, each either
    /// null or addressing at least numSamples floats. It is written but never
    /// read (FR-073).
    void processBlockTapped(const float* inL, const float* inR, float* outL, float* outR,
                            float* const* loopTaps, std::size_t numSamples) noexcept {
        if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) return;
        if (numSamples == 0) return;
        if (!prepared_) {
            std::fill_n(outL, numSamples, 0.0f);
            std::fill_n(outR, numSamples, 0.0f);
            return;
        }

        std::size_t done = 0;
        while (done < numSamples) {
            if (controlPhase_ == 0) updateControl();
            const std::size_t chunk =
                std::min(numSamples - done, kControlChunkSamples - controlPhase_);
            renderChunk(inL + done, inR + done, outL + done, outR + done, loopTaps, done, chunk);
            controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
            done += chunk;
        }
    }

    // =========================================================================
    // Topology
    // =========================================================================

    /// FR-075. Clamped [1, kMaxLoops]. MUST NOT allocate - all six lines are
    /// prepared regardless of the count.
    ///
    /// Three quantities move and ALL THREE are ramped at kGainRampMs (S7.5):
    /// the dropped loops' gates, the FR-017 normGain divisor (1/sqrt(6) ->
    /// 1/sqrt(5) is 0.792 dB - a step here makes SC-022 (c) unreachable), and
    /// the FR-075 count mask on the dropped loops' OUTGOING coupling. Nothing
    /// steps. The divisor is ALWAYS numLoops, never the awake count: an
    /// awake-count reading computes 1/sqrt(0) = inf when every loop sleeps and
    /// inf * 0.0f is NaN.
    void setNumLoops(std::size_t n) noexcept {
        config_.numLoops = std::clamp(n, std::size_t{1}, kMaxLoops);
        normGainRamp_.setTarget(1.0f / std::sqrt(static_cast<float>(config_.numLoops)));
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            countSmoother_[i].setTarget(i < config_.numLoops ? 1.0f : 0.0f);
        }
        // THE THIRD MOVER. gateSteady() returns 0.0f for every slot the new
        // count excludes, so a dropped loop is dropped EXACTLY the way a sleep
        // is done - a 50 ms fade and then the FR-063 sleep edge when the gate
        // settles - never a cut. Loops re-admitted by a later, larger n take the
        // FR-064 wake path unchanged. refreshGates() is idempotent: a slot whose
        // steady value has not moved keeps its lastGateTarget and is not
        // re-targeted, so calling setNumLoops with the count it already has
        // does not restart a single ramp (S7.3).
        refreshGates();
    }

    // =========================================================================
    // Per-loop stages
    // =========================================================================

    /// FR-011, STEPPED (FR-076): re-pointing the SVF output tap is a genuine
    /// signal discontinuity, and SC-001 (d) exempts this setter by name.
    /// An enumerator outside the three named values is a silent no-op.
    void setLoopFilterMode(std::size_t loop, FilterMode mode) noexcept {
        if (loop >= kMaxLoops) return;
        if (mode != FilterMode::Lowpass && mode != FilterMode::Bandpass
            && mode != FilterMode::Highpass) {
            return;
        }
        Loop& L = loops_[loop];
        L.filterMode = mode;
        L.svf.setMode(toSvfMode(mode));
    }

    /// FR-011/FR-053 base cutoff. Caches baseLog2Cutoff so the S5.4 control step
    /// costs one std::exp2 and NO std::log2.
    ///
    /// It deliberately does NOT write svf.setCutoff: the control step is the
    /// SINGLE owner of that write (S5.2 step 4e), which is what makes
    /// getLoopCurrentCutoffHz diverge from getLoopTargetCutoffHz under FR-062's
    /// write skip and under NOTHING else (S9). The new base reaches the filter on
    /// the next control step, or at the FR-064 wake edge if the loop is asleep.
    void setLoopCutoffHz(std::size_t loop, float hz) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(hz)) return;
        Loop& L = loops_[loop];
        L.baseCutoffHz = std::clamp(hz, kMinCutoffHz, maxCutoffHz_);
        // Runtime std::log2, not a constexpr one: a CONSTEXPR std::log2 is a
        // GCC/MSVC builtin extension Clang rejects (S5.4). applyDefaults() uses
        // detail::constexprLn / detail::kLn2 for the same reason; SC-017's
        // constexpr-log arm pins the two to 1e-6 of each other.
        L.baseLog2Cutoff = std::log2(L.baseCutoffHz);
    }

    /// FR-012, clamped [kMinFilterQ, kMaxFilterQ]. The SVF's own per-sample
    /// coefficient smoother (enabled in prepare() step 4) de-zippers it; the
    /// control step never writes resonance, so this setter must.
    void setLoopFilterQ(std::size_t loop, float q) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(q)) return;
        Loop& L = loops_[loop];
        L.filterQ = std::clamp(q, kMinFilterQ, kMaxFilterQ);
        L.svf.setResonance(L.filterQ);
    }

    /// FR-022 base, clamped [kMinDelayMs, kMaxDelayMs]. Stores only: the FR-021
    /// crossfade staircase carries it, through the control-grid setDelayMs
    /// (S11). An extra smoother would be dead weight below
    /// CrossfadingDelayLine's 100-sample threshold and would fight the crossfade
    /// above it.
    void setLoopDelayMs(std::size_t loop, float ms) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(ms)) return;
        loops_[loop].baseDelayMs = std::clamp(ms, kMinDelayMs, kMaxDelayMs);
    }

    /// FR-013, STEPPED via updateResonator() - one setCoefficients call, in force
    /// on the very next sample (S4.1, FR-076).
    void setLoopResonanceHz(std::size_t loop, float hz) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(hz)) return;
        loops_[loop].resonanceHz = std::clamp(hz, kMinResonatorFrequency, maxResonanceHz_);
        updateResonator(loop);
    }

    /// FR-013, STEPPED via updateResonator(). The REQUEST is clamped to
    /// [kMinDecayTime, kMaxDecayTime]; it is then clamped AGAIN, per centre, by
    /// rt60ToQ's kMaxResonatorQ ceiling, and getLoopResonanceRt60() reports that
    /// realised figure rather than this request (DERIVATION TABLE 2, S4.2).
    void setLoopResonanceRt60(std::size_t loop, float seconds) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(seconds)) return;
        loops_[loop].resonanceRt60Req = std::clamp(seconds, kMinDecayTime, kMaxDecayTime);
        updateResonator(loop);
    }

    /// FR-018 own feedback, clamped [kMinLoopGain, kMaxLoopGain]. Reaches the
    /// per-sample path through the control-rate ownFbSmoother and then
    /// normaliseRows() (FR-035, FR-076).
    void setLoopGain(std::size_t loop, float gain) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(gain)) return;
        Loop& L = loops_[loop];
        L.ownFbTarget = std::clamp(gain, kMinLoopGain, kMaxLoopGain);
        L.ownFbSmoother.setTarget(L.ownFbTarget);
    }

    /// FR-074 input tap, clamped [0, 1]. Written through the per-sample
    /// inputRamp, so a tap opening from zero is a kMixRampMs fade and not a step
    /// in x_i.
    void setLoopInputGain(std::size_t loop, float gain) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(gain)) return;
        Loop& L = loops_[loop];
        L.inputGain = std::clamp(gain, 0.0f, 1.0f);
        L.inputRamp.setTarget(L.inputGain);
    }

    // =========================================================================
    // Coupling
    // =========================================================================

    /// FR-032. Clamped [0, kMaxCouplingPerPair] - coupling is NON-NEGATIVE here,
    /// unlike FilterFeedbackMatrix::kMinFeedback = -1.0f: a negative coupling is a
    /// phase inversion and with six loops and a governor it produces cancellation
    /// notches that read as level drops rather than as interaction (D-5).
    /// The diagonal is a no-op: a loop's own feedback is setLoopGain.
    void setCoupling(std::size_t from, std::size_t to, float amount) noexcept {
        if (from >= kMaxLoops || to >= kMaxLoops) return;
        if (from == to) return;
        if (!detail::isFinite(amount)) return;
        const float a = std::clamp(amount, 0.0f, kMaxCouplingPerPair);
        couplingTarget_[from][to] = a;
        couplingSmoother_[from][to].setTarget(a);
    }

    /// FR-032. The SAME per-entry rule, applied element by element: each argument
    /// is judged independently, so one bad value cannot discard a good one. The
    /// diagonal is ignored, and one non-finite entry leaves only that pair's
    /// previous value standing (S8.6).
    void setCouplingMatrix(
        const std::array<std::array<float, kMaxLoops>, kMaxLoops>& m) noexcept {
        for (std::size_t from = 0; from < kMaxLoops; ++from) {
            for (std::size_t to = 0; to < kMaxLoops; ++to) {
                setCoupling(from, to, m[from][to]);
            }
        }
    }

    // =========================================================================
    // Governor
    // =========================================================================

    /// FR-044, clamped [kMinGovernorThresholdDb, kMaxGovernorThresholdDb].
    /// Stores only: governorRamp_ is re-targeted from these two on every control
    /// step (S5.6), so there is nothing for the setter to push.
    void setGovernorThresholdDb(float db) noexcept {
        if (!detail::isFinite(db)) return;
        governorThresholdDb_ = std::clamp(db, kMinGovernorThresholdDb, kMaxGovernorThresholdDb);
    }
    /// FR-044, clamped [kMinGovernorRatio, kMaxGovernorRatio]. Same mechanism.
    void setGovernorRatio(float ratio) noexcept {
        if (!detail::isFinite(ratio)) return;
        governorRatio_ = std::clamp(ratio, kMinGovernorRatio, kMaxGovernorRatio);
    }

    // =========================================================================
    // Life modulation
    // =========================================================================

    /// FR-052, clamped [0, kMaxDelayWanderFraction]. A DEPTH, read by the S5.4
    /// mapping on the next control step; it never touches a lane.
    void setLoopDelayWander(std::size_t loop, float fraction) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(fraction)) return;
        loops_[loop].delayWanderFrac = std::clamp(fraction, 0.0f, kMaxDelayWanderFraction);
    }
    /// FR-053, clamped [0, kMaxCutoffWanderOctaves]. Same rule.
    void setLoopCutoffWander(std::size_t loop, float octaves) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(octaves)) return;
        loops_[loop].cutoffWanderOct = std::clamp(octaves, 0.0f, kMaxCutoffWanderOctaves);
    }
    /// FR-055. THE SINGLE OWNER of laneDecimation_ and of every lane's
    /// setSmoothness. Never recomputed anywhere else, so prepare() and a later
    /// caller cannot disagree about the mapping.
    /// @param hz Clamped [kMinWanderRateHz, kMaxWanderRateHz] = [0.002, 1.0].
    ///
    /// Phase 3's mapping VERBATIM (resonance_drift_network.h:715-740). Without
    /// the decimation BrownianDrift's tau saturates at kTauMax = 30 s and the
    /// whole sub-range [0.002, 0.0333] Hz - INCLUDING this component's own
    /// 0.03 Hz default - would be a dead zone in which every rate rendered
    /// identically. Decimating the lane advance by D multiplies the reachable
    /// period by D: 17 * 30 s = 510 s covers the 500 s the slowest rate asks for.
    ///
    /// The table SC-025 pins (plan S11), so the mapping is arithmetic and not a
    /// re-derivation:
    ///   0.002 Hz -> tau 500 s   -> decimation 17, per-advance tau 29.412 s,
    ///                              smoothness 0.98027, effective 500 s
    ///   0.03  Hz -> tau 33.33 s -> decimation  2, per-advance tau 16.667 s,
    ///                              smoothness 0.55257, effective 33.3 s
    ///   1.0   Hz -> tau 1 s     -> decimation  1, per-advance tau 1 s,
    ///                              smoothness 0.02685, effective 1 s
    ///
    /// STEPPED (FR-076) but CLICK-FREE: it writes only laneDecimation_ and the
    /// twelve lanes' smoothness, and steps no mapped value in the signal path -
    /// so it is HELD TO SC-001 (d)'s click assertion, not exempt from it (S12.3).
    void setWanderRate(float hz) noexcept {
        if (!detail::isFinite(hz)) return;
        wanderRateHz_ = std::clamp(hz, kMinWanderRateHz, kMaxWanderRateHz);

        const float requestedTau = 1.0f / wanderRateHz_;  // [1, 500] s
        const auto ceilSteps =
            static_cast<std::size_t>(std::ceil(requestedTau / BrownianDrift::kTauMax));
        const std::size_t newDecimation =
            std::clamp(ceilSteps, std::size_t{1}, kMaxLaneDecimation);
        const float tau = requestedTau / static_cast<float>(newDecimation);
        const float smoothness = std::clamp(
            (tau - BrownianDrift::kTauMin) / (BrownianDrift::kTauMax - BrownianDrift::kTauMin),
            0.0f, 1.0f);

        for (Loop& L : loops_) {
            L.delayLane.setSmoothness(smoothness);
            L.cutoffLane.setSmoothness(smoothness);
        }

        // REBASE, DO NOT RESET. A rate change mid-render must neither force an
        // extra lane advance nor skip one; `laneCounter_ = 0` would advance the
        // lanes on the very next control step however recently they last moved,
        // and a caller re-writing the same rate on a fine grid (SC-025 (c)) would
        // turn a decimated lane into an undecimated one.
        laneCounter_ %= newDecimation;
        laneDecimation_ = newDecimation;
    }
    /// FR-056. Off zeroes every DEPTH; it does not rewind a lane, and it does
    /// NOT freeze the lane advance - updateControl() step 1 is unconditional and
    /// wanderScale() zeroes only the depth TERM (Q2, Phase-3 verbatim,
    /// resonance_drift_network.h:742-744). Re-enabling therefore moves from base
    /// to wherever the still-advancing lane now sits; there is no reading under
    /// which that is jump-free and this header does not claim one (S5.4).
    void setWanderEnabled(bool enabled) noexcept { wanderEnabled_ = enabled; }
    /// FR-054. Stores the seed and re-derives all twelve lane streams; every
    /// re-seed is followed by a mandatory per-lane reset(), because
    /// BrownianDrift::setSeed reseeds the RNG but does not rewind the walk
    /// (brownian_drift.h:145-148 vs reset() :133).
    ///
    /// deriveStreamSeed's guaranteed-non-zero result is load-bearing, not
    /// hygiene: Xorshift32::seed(0) silently substitutes its own default
    /// (random.h:73-75), so two lanes hashing to 0 would COLLAPSE ONTO ONE
    /// STREAM.
    ///
    /// STEPPED (FR-076) and deliberately separate from reset(): calling it
    /// mid-render is legal and does NOT clear the audio state - the render
    /// continues without a discontinuity while the modulation restarts.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            L.delayLane.setSeed(deriveStreamSeed(seed_, kSaltDelayLane + i));
            L.delayLane.reset();
            L.cutoffLane.setSeed(deriveStreamSeed(seed_, kSaltCutoffLane + i));
            L.cutoffLane.reset();
        }
    }

    // =========================================================================
    // Loop life cycle
    // =========================================================================

    /// FR-060. Clamped [0, 1], and THE CLAMP IS LOAD-BEARING: a Phase-8 agent
    /// driving wake from energy, or a Phase-10 caller writing
    /// getEnvelopeValue() * getActiveDepth(), must not be able to push a loop
    /// past unity or invert it (resonance_drift_network.h:760-771).
    ///
    /// The kWakeSilenceEpsilon snap is NOT here - it lives in gateSteady(), at
    /// the source, so the value this setter STORES is the caller's own (a
    /// release tail at 1e-8 reads back as 1e-8) while the value the gate ramps
    /// to is exactly 0.0f (SC-014 (e)).
    ///
    /// SMOOTHED (FR-076): refreshGates() re-targets the gate ramp at
    /// kGainRampMs; it never writes the gate directly.
    void setLoopWake(std::size_t loop, float amount) noexcept {
        if (loop >= kMaxLoops) return;
        if (!detail::isFinite(amount)) return;
        loops_[loop].wakeAmount = std::clamp(amount, 0.0f, 1.0f);
        refreshGates();
    }
    /// FR-060. The other half of the ONE steady-state gate value, which is what
    /// makes setLoopDormant(i, true) and setLoopWake(i, 0.0f) behaviourally
    /// indistinguishable - the cross-cutting Dormancy rule's core claim
    /// (SC-014 (a)).
    void setLoopDormant(std::size_t loop, bool dormant) noexcept {
        if (loop >= kMaxLoops) return;
        loops_[loop].dormant = dormant;
        refreshGates();
    }

    // =========================================================================
    // Output stage
    // =========================================================================

    /// FR-072, clamped [0, 1], carried by mixRamp_ at kMixRampMs.
    void setMix(float mix) noexcept {
        if (!detail::isFinite(mix)) return;
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixRamp_.setTarget(mix_);
    }
    /// FR-072, clamped [kMinWetGainDb, kMaxWetGainDb]. getWetGain() reports dB;
    /// the ramp carries the LINEAR trim, so the glide is in gain, not in decibels.
    void setWetGain(float db) noexcept {
        if (!detail::isFinite(db)) return;
        wetGainDb_ = std::clamp(db, kMinWetGainDb, kMaxWetGainDb);
        wetGainRamp_.setTarget(dbToGain(wetGainDb_));
    }

    // =========================================================================
    // FR-070: configuration read surface
    // =========================================================================
    // UNPREPARED STATE (S9): every configuration getter returns its
    // POST-CONSTRUCTION DEFAULT, because FR-002 leaves construction in the state
    // prepare(48000.0, PrepareConfig{}) produces. The ONLY two exceptions are
    // getAllocatedBytes() (0 before prepare(), SC-008) and isPrepared() (false).
    // "Neutral" is reserved for FR-009's out-of-range-index case and is NOT a
    // second meaning for the unprepared state.
    //
    // T007 lands the per-loop bodies; the whole-object getters below are already
    // final because each is exactly the member it names.

    [[nodiscard]] std::size_t getNumLoops() const noexcept { return config_.numLoops; }
    [[nodiscard]] std::size_t getMaxBlockSamples() const noexcept {
        return config_.maxBlockSamples;
    }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] float getWanderRate() const noexcept { return wanderRateHz_; }
    [[nodiscard]] bool isWanderEnabled() const noexcept { return wanderEnabled_; }
    [[nodiscard]] float getMix() const noexcept { return mix_; }
    /// dB, not linear.
    [[nodiscard]] float getWetGain() const noexcept { return wetGainDb_; }
    [[nodiscard]] float getGovernorThresholdDb() const noexcept { return governorThresholdDb_; }
    [[nodiscard]] float getGovernorRatio() const noexcept { return governorRatio_; }
    /// FR-017. The wet sum's divisor is 1/sqrt(THIS), computed from numLoops and
    /// NEVER from the awake count: an awake-count reading computes 1/sqrt(0) = inf
    /// when every loop sleeps, and inf * 0.0f is NaN.
    [[nodiscard]] std::size_t getNormalisationLoopCount() const noexcept {
        return config_.numLoops;
    }
    /// FR-082. 0 before prepare(); the figure is computed once, in prepare()
    /// step 13, and is kMaxLoops-scaled (S10) because all six delay lines are
    /// prepared regardless of numLoops.
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return allocatedBytes_; }

    [[nodiscard]] float getLoopDelayMs(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].baseDelayMs;
    }
    [[nodiscard]] float getLoopCutoffHz(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].baseCutoffHz;
    }
    /// FR-009's documented neutral for an out-of-range index is the enum's zero
    /// value, FilterMode::Lowpass - which is why the guard must come FIRST rather
    /// than being folded into the array read.
    [[nodiscard]] FilterMode getLoopFilterMode(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return FilterMode::Lowpass;
        return loops_[loop].filterMode;
    }
    [[nodiscard]] float getLoopFilterQ(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].filterQ;
    }
    [[nodiscard]] float getLoopResonanceHz(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].resonanceHz;
    }
    /// FR-013 / Clarification Q1: the REALISED ring, derived back from the
    /// APPLIED Q, never an echo of the request. See DERIVATION TABLE 2.
    ///
    /// It is deliberately NOT re-clamped into [kMinDecayTime, kMaxDecayTime] on
    /// the way out: a second clamp would restore exactly the lie this getter
    /// exists to prevent (S4.2).
    ///
    /// maxResonanceHz_ is SEEDED AT CONSTRUCTION (S1.5) and is never 0.0f, so the
    /// clamp pair below is ordered on an UNPREPARED instance too - which SC-017
    /// exercises directly. With a 0.0f initialiser this line would be
    /// std::clamp(1200.0f, 20.0f, 0.0f): UB, trapped by MSVC's _STL_VERIFY, and
    /// where it is not trapped it returns 0 and the division yields inf (R-8).
    [[nodiscard]] float getLoopResonanceRt60(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        const Loop& L = loops_[loop];
        const float f = std::clamp(L.resonanceHz, kMinResonatorFrequency, maxResonanceHz_);
        // Exact inverse of rt60ToQ (resonator_bank.h:92-99): Q = pi*f*rt60 / ln1000.
        return (L.appliedResonanceQ * kLn1000) / (kPi * f);
    }
    [[nodiscard]] float getLoopGain(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].ownFbTarget;
    }
    [[nodiscard]] float getLoopInputGain(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].inputGain;
    }
    [[nodiscard]] float getLoopDelayWander(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].delayWanderFrac;
    }
    [[nodiscard]] float getLoopCutoffWander(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].cutoffWanderOct;
    }
    /// The value setLoopWake STORED, not the gate it produces: the
    /// kWakeSilenceEpsilon snap is gateSteady()'s and does not rewrite the
    /// caller's number (FR-060, FR-070).
    [[nodiscard]] float getLoopWakeAmount(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].wakeAmount;
    }
    [[nodiscard]] bool isLoopDormant(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return false;
        return loops_[loop].dormant;
    }
    /// FR-009: 0.0f for an out-of-range index or from == to, WITHOUT indexing.
    [[nodiscard]] float getCoupling(std::size_t from, std::size_t to) const noexcept {
        if (from >= kMaxLoops || to >= kMaxLoops) return 0.0f;
        if (from == to) return 0.0f;
        return couplingTarget_[from][to];
    }

    // =========================================================================
    // FR-071: realised-state read surface (S9)
    // =========================================================================

    /// The delay position ACTUALLY in force, as CrossfadingDelayLine reports it.
    /// While a crossfade is in flight this is the GAIN-WEIGHTED AVERAGE of the
    /// two taps (crossfading_delay_line.h:306-309), not either end point - which
    /// is why SC-005 counts a reading as settled only when
    /// getLoopCrossfadeCount(i) is unchanged either side of it.
    ///
    /// On a NEVER-PREPARED instance the line has no buffer and reports 0.0f;
    /// sampleRate_ is the construction-seeded 48 000 (S1.5), never 0, so the
    /// division is safe at every point in the object's life (S9).
    [[nodiscard]] float getLoopCurrentDelayMs(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].delay.getCurrentDelaySamples() * 1000.0f
               / static_cast<float>(sampleRate_);
    }
    /// The last COMMANDED cutoff, svf.getCutoff() (svf.h:328). It COINCIDES with
    /// getLoopTargetCutoffHz while the loop runs - SVF's per-sample smoothing
    /// changes the internal g/k, not the value getCutoff() reports - and diverges
    /// only under FR-062's write skip.
    [[nodiscard]] float getLoopCurrentCutoffHz(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        // Awake: the commanded value coincides with the target (Clarifications
        // Q7); the SVF itself may lag it by up to kCutoffPushRelative because
        // control steps push only real moves. Dormant: the value the SVF holds,
        // frozen by FR-062's write skip.
        const Loop& L = loops_[loop];
        return L.engineActive ? L.targetCutoffHz : L.svf.getCutoff();
    }
    /// The FR-052 mapped value as of the last control step, WHETHER OR NOT the
    /// loop is skipped: the continuous lane trajectory, not the realised
    /// crossfade staircase.
    [[nodiscard]] float getLoopTargetDelayMs(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].targetDelayMs;
    }
    [[nodiscard]] float getLoopTargetCutoffHz(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].targetCutoffHz;
    }
    /// Crossfades STARTED, not completed: the two differ whenever a crossfade is
    /// retriggered mid-fade (crossfading_delay_line.h:176-181).
    [[nodiscard]] std::uint32_t getLoopCrossfadeCount(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0u;
        return loops_[loop].crossfadeCount;
    }
    /// The own-feedback coefficient the per-sample path is ACTUALLY multiplying
    /// by: the smoothed target after FR-035's row scaling, not the configured
    /// value getLoopGain reports.
    [[nodiscard]] float getLoopAppliedOwnFeedback(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return appliedOwnFb_[loop];
    }
    /// FR-035's per-pair evidence. Without it SC-015 could observe the row
    /// normalisation only through getLoopAppliedTotalGain, which is
    /// min(g, kMaxTotalLoopGain) BY CONSTRUCTION and therefore a tautology.
    [[nodiscard]] float getLoopAppliedCoupling(std::size_t from,
                                               std::size_t to) const noexcept {
        if (from >= kMaxLoops || to >= kMaxLoops) return 0.0f;
        if (from == to) return 0.0f;
        return appliedCoupling_[from][to];
    }
    /// FR-035's realised row sum, min(g, kMaxTotalLoopGain) BY CONSTRUCTION -
    /// which is exactly why SC-015 (a) reconstructs the sum from
    /// getLoopAppliedCoupling instead of trusting this number on its own.
    [[nodiscard]] float getLoopAppliedTotalGain(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return appliedTotalGain_[loop];
    }
    /// FR-061's per-sample fade, as the render is ACTUALLY multiplying by it -
    /// the ramp's current value, not gateSteady()'s target. The two differ for
    /// kGainRampMs after every life-cycle setter, and it is the ramped value
    /// SC-014 (c)'s monotonicity arm is about. Exactly 0.0f once a sleep has
    /// settled (LinearRamp::process lands ON its target, smoother.h:379-383),
    /// which is what makes the sleep edge's `== 0.0f` test exact.
    [[nodiscard]] float getLoopGate(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return 0.0f;
        return loops_[loop].gate.getCurrentValue();
    }
    /// FR-062's chain-skip flag: false once the sleep edge has run, true again
    /// from the instant of the FR-064 wake edge (which is inside the SETTER, not
    /// at the next control step - S7.3).
    [[nodiscard]] bool isLoopEngineActive(std::size_t loop) const noexcept {
        if (loop >= kMaxLoops) return false;
        return loops_[loop].engineActive;
    }
    /// FR-070. The gain the per-sample path is ACTUALLY multiplying by - the
    /// governor ramp's current value, not the law's target: the two differ for
    /// kGovernorRampMs after every control step, and it is the ramped value that
    /// SC-006's "non-increasing" and "never below kGovernorMinGain" arms are
    /// about. Exactly 1.0f whenever the tracker sits at or below the threshold,
    /// and at ratio = 1 at every level (the exponent identity in S5.6).
    [[nodiscard]] float getGovernorGain() const noexcept {
        return governorRamp_.getCurrentValue();
    }
    /// FR-070. The tracked quantity itself: the RMS of normGain * sum_i b_i,
    /// measured PRE-governor, PRE-gate and PRE-tanh - the quantity being
    /// controlled, observed before its own action, which is what makes the loop a
    /// first-order regulator rather than an oscillator. It is NOT the output RMS
    /// and a criterion that wants the output must measure the output.
    [[nodiscard]] float getGovernorRms() const noexcept {
        return follower_.getCurrentValue();
    }
    [[nodiscard]] std::size_t getLaneDecimation() const noexcept { return laneDecimation_; }
    /// FR-046. Monotone; reset() clears it.
    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept {
        return clampEngagements_;
    }
    /// FR-047. Monotone; reset() clears it.
    [[nodiscard]] std::uint32_t getNonFiniteResetCount() const noexcept {
        return nonFiniteResets_;
    }

private:
    /// FR-048 fault injection - see the declaration above the class.
    friend struct detail::FeedbackEcologyNonFiniteProbe;

    // =========================================================================
    // FR-054 salt table - APPEND ONLY (S1.6)
    // =========================================================================
    // Renumbering a base silently changes every Phase-5 render. A later phase
    // adding a lane takes a NEW base at kSaltNextFree and moves that constant up.
    static constexpr std::size_t kSaltDelayLane = 0;    // + loop
    static constexpr std::size_t kSaltCutoffLane = 16;  // + loop
    static constexpr std::size_t kSaltNextFree = 32;
    static_assert(kSaltDelayLane + kMaxLoops <= kSaltCutoffLane,
                  "delay salts overlap the cutoff block");
    static_assert(kSaltCutoffLane + kMaxLoops <= kSaltNextFree, "salt table overflow");

    // =========================================================================
    // Private state (S1.5), in declaration order
    // =========================================================================
    // SKELETON-PASS NOTE (T006 only). Members no stub body reads yet carry
    // [[maybe_unused]] with the task number that consumes them. This is not
    // decoration: Clang's -Wall includes -Wunused-private-field, which fires on
    // a private field of TRIVIALLY-constructible type that no member function
    // touches - and this repo builds dsp/ with -Wall -Wextra on the GCC and
    // Clang legs (dsp/CMakeLists.txt:54-55), where MSVC has no equivalent
    // diagnostic. Each attribute is REMOVED by the task named beside it, when
    // that task gives the member its first reader.

    /// One micro-loop: filter -> delay -> resonator -> DC blocker, plus the two
    /// life-modulation lanes and the ramps that de-zipper it.
    /// PRIVATE and NESTED deliberately: a namespace-scope type for a private
    /// implementation detail is a future ODR liability for no gain. (MicroLoop
    /// and EcologyLoop both swept clean as names and are still rejected.)
    struct Loop {
        // --- audio stages, in signal order ---------------------------------
        SVF svf;                     ///< FR-011, smoothing enabled in prepare() step 4.
        CrossfadingDelayLine delay;  ///< FR-020.
        Biquad resonator;            ///< FR-013, DIRECT RBJ coefficients - never configure().
        DCBlocker dcBlocker;         ///< FR-014.

        // --- life-modulation lanes (FR-050) --------------------------------
        BrownianDrift delayLane;
        BrownianDrift cutoffLane;

        // --- per-sample ramps (FR-061, FR-074, FR-076) ---------------------
        LinearRamp gate;       ///< kGainRampMs.
        LinearRamp inputRamp;  ///< kMixRampMs.
        /// The load-bearing re-target shadow: refreshGates() must not restart a
        /// gate ramp that is already heading for the same target (S7.3).
        float lastGateTarget = 0.0f;

        // --- control-rate smoother (FR-076; configured at fs/64 - S5.0) ----
        OnePoleSmoother ownFbSmoother;  ///< kCouplingSmoothMs.

        // --- configuration (restored by prepare(), preserved by reset()) ----
        // The four per-index default TABLES cannot be written as default member
        // initialisers - a member initialiser has no loop index. applyDefaults()
        // (T007) is their ONE owner and the constructor runs it, so an unprepared
        // object already reports kDefaultLoopDelayMs[i] and friends (S9).
        float baseDelayMs = 0.0f;                    ///< kDefaultLoopDelayMs[i]
        float baseCutoffHz = 0.0f;                   ///< kDefaultLoopCutoffHz[i]
        float baseLog2Cutoff = 0.0f;                 ///< cached: no std::log2 per control step
        float filterQ = kDefaultFilterQ;
        FilterMode filterMode = FilterMode::Lowpass;
        float resonanceHz = 0.0f;                    ///< kDefaultLoopResonanceHz[i]
        float resonanceRt60Req = kDefaultResonanceRt60;  ///< the REQUEST, clamped per centre
        float appliedResonanceQ = kMinResonatorQ;    ///< what rt60ToQ actually produced
        float ownFbTarget = kDefaultLoopGain;
        float inputGain = kDefaultLoopInputGain;
        float delayWanderFrac = 0.0f;                ///< kDefaultDelayWanderFraction[i]
        float cutoffWanderOct = kDefaultCutoffWanderOctaves;
        float wakeAmount = 1.0f;
        bool dormant = false;

        // --- realised / reported state -------------------------------------
        float targetDelayMs = 0.0f;         ///< FR-062: updated even while skipped
        float targetCutoffHz = 0.0f;        ///< FR-062: updated even while skipped
        float pushedCutoffHz = 0.0f;        ///< last value handed to svf.setCutoff (kCutoffPushRelative)
        std::uint32_t crossfadeCount = 0;   ///< FR-071: monotone ONSET count
        bool lastCrossfading = false;
        bool engineActive = false;          ///< FR-062's chain-skip flag

        /// FR-063's stale-ring guarantee, realised in O(1) (S3.1). While this is
        /// non-zero the loop WRITES the delay line but does not READ it, so no
        /// pre-clear sample can reach the output; it counts down one per rendered
        /// sample and freezes the delay position while it runs. The alternative,
        /// DelayLine::reset(), is a std::fill over 131 072 B per line at 48 kHz
        /// (524 288 B at 192 kHz) and two of clearLoopAudio()'s four callers are
        /// AUDIO-THREAD paths against an 8 889 ns chunk budget.
        std::size_t readMuteSamples = 0;
    };

    std::array<Loop, kMaxLoops> loops_{};

    // FR-031: the TWO previous-sample vectors. Own feedback reads prevY_
    // (PRE-gate); cross-coupling reads prevOut_ (POST-gate).
    std::array<float, kMaxLoops> prevY_{};    // zeroed by clearLoopAudio (T008)
    std::array<float, kMaxLoops> prevOut_{};  // zeroed by clearLoopAudio (T008)

    // FR-030 / FR-034 / FR-035: coupling targets, their smoothers, applied values.
    // 36 smoothers are allocated and 30 are used - the diagonal is never a
    // coupling (a loop's own feedback is ownFbSmoother), which is why S5.0 counts
    // 42 control-rate smoothers and not 48.
    std::array<std::array<float, kMaxLoops>, kMaxLoops> couplingTarget_{};
    std::array<std::array<OnePoleSmoother, kMaxLoops>, kMaxLoops> couplingSmoother_{};
    std::array<std::array<float, kMaxLoops>, kMaxLoops> appliedCoupling_{};  // normaliseRows()
    std::array<float, kMaxLoops> appliedOwnFb_{};      // normaliseRows(); read by renderChunk
    std::array<float, kMaxLoops> appliedTotalGain_{};  // normaliseRows(); FR-071 read surface

    /// FR-075: the count mask. One control-grid OnePoleSmoother per SOURCE slot,
    /// target 1.0 while the slot is inside numLoops and 0.0 once setNumLoops drops
    /// it, at kGainRampMs - the SAME constant FR-076 assigns setNumLoops and the
    /// same one the gate uses. It multiplies that slot's OUTGOING coupling inside
    /// normaliseRows(), so a dropped loop leaves its neighbours' input sums as a
    /// 50 ms glide instead of a one-control-step step. It reaches EXACTLY 0.0f
    /// because OnePoleSmoother::process snaps inside kCompletionThreshold
    /// (smoother.h:200-203), which is what keeps SC-015 (d)'s 1e-6 count-inertness
    /// claim exact.
    std::array<OnePoleSmoother, kMaxLoops> countSmoother_{};

    // Governor (FR-043 - FR-045)
    EnvelopeFollower follower_{};  // configured by prepare() step 6 (T008)
    LinearRamp governorRamp_;      // kGovernorRampMs, advanced per sample by T012
    float governorThresholdDb_ = kDefaultGovernorThresholdDb;
    float governorRatio_ = kDefaultGovernorRatio;

    // Output stage (FR-017, FR-072)
    LinearRamp normGainRamp_;  // kGainRampMs, target 1/sqrt(numLoops)
    LinearRamp mixRamp_;       // kMixRampMs
    LinearRamp wetGainRamp_;   // kMixRampMs, carries the LINEAR trim
    float mix_ = kDefaultMix;
    float wetGainDb_ = kDefaultWetGainDb;

    // Grid, wander, seed
    /// FR-007: an ABSOLUTE residue carried ACROSS calls, never a block-relative
    /// grid - a 36 + 28 split must run ONE control step, not two (SC-010).
    std::size_t controlPhase_ = 0;  // zeroed by prepare()/reset(); advanced by T009
    std::size_t laneCounter_ = 0;   // zeroed by prepare()/reset(); advanced by T010
    std::size_t laneDecimation_ = 1;
    float wanderRateHz_ = kDefaultWanderRateHz;
    bool wanderEnabled_ = true;
    /// FR-054. 0 is a legitimate base: deriveStreamSeed() guarantees a non-zero
    /// result for every salt (random.h:102-113), and Xorshift32::kDefaultSeed is
    /// private. Phase-3 precedent, resonance_drift_network.h:1086.
    std::uint32_t seed_ = 0;

    // Cached rate-dependent bounds. Recomputed in prepare() step 3, but SEEDED
    // HERE from kConstructionSampleRate and NEVER left at 0.0f. Both are the upper
    // half of a std::clamp pair whose lower half is a fixed constant, and
    // std::clamp with hi < lo is UB that MSVC's <algorithm> traps with
    // _STL_VERIFY (R-8). SC-017 calls getLoopResonanceRt60(i) - which clamps
    // against maxResonanceHz_ - on an UNPREPARED instance, so a 0.0f initialiser
    // is a guaranteed trap, not a latent one.
    //
    // INVARIANT, enforced by the S1.2 static_asserts and restated here: neither
    // member may ever hold a value below its paired minimum, at any point in the
    // object's life. ANY GETTER ADDED LATER THAT CLAMPS AGAINST A RATE-DERIVED
    // BOUND MUST BE AUDITED AGAINST THIS RULE (S9). Today there are exactly two
    // such bounds and only getLoopResonanceRt60 reads one of them.
    double sampleRate_ = kConstructionSampleRate;
    float maxCutoffHz_ =
        std::max(kMinCutoffHz, static_cast<float>(kConstructionSampleRate) * SVF::kMaxCutoffRatio);
    float maxResonanceHz_ =
        std::max(kMinResonatorFrequency,
                 static_cast<float>(kConstructionSampleRate) * kMaxResonatorFrequencyRatio);

    PrepareConfig config_{};
    bool prepared_ = false;

    std::size_t allocatedBytes_ = 0;      ///< FR-082
    std::uint32_t clampEngagements_ = 0;  ///< FR-046
    std::uint32_t nonFiniteResets_ = 0;   ///< FR-047

    // =========================================================================
    // Private helpers (T007, T008)
    // =========================================================================

    /// @brief FR-083 / R-8. A non-finite request becomes a NAMED NEUTRAL, never
    /// the floor. NoiseOrganism::sanitise's per-argument rule; only the double
    /// overload exists because only prepare()'s rate needs it - every float
    /// setter rejects a non-finite argument outright (FR-009).
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    /// @brief S5.2 step 2's ONE carve-out (OQ-2 lever 3): a control-rate
    /// smoother whose current value is BIT-EXACT at its target skips its
    /// process() call.
    ///
    /// EXACT, not approximate: OnePoleSmoother::process() SNAPS
    /// current_ = target_ inside kCompletionThreshold = 1e-4
    /// (smoother.h:199-202), so a settling smoother REACHES bit-equality rather
    /// than approaching it - the value the skipped call would write is the value
    /// already there. In the reference patch's steady state that is all 42 of
    /// them; the saving is the whole of OQ-2 lever 3.
    ///
    /// It is a CPU lever and NOTHING ELSE. Skipping an unsettled smoother is the
    /// desynchronisation bug filter_feedback_matrix.h:595-598 exists to prevent.
    static void advanceControlSmoother(OnePoleSmoother& s) noexcept {
        if (s.getCurrentValue() == s.getTarget()) return;
        static_cast<void>(s.process());
    }

    /// @brief S5.3 / FR-035 - THE STRUCTURAL HALF OF BOUNDEDNESS. Scales every
    /// row of coefficients so the absolute values feeding loop `i` sum to at
    /// most kMaxTotalLoopGain, which is FR-041's l-infinity contraction argument
    /// stated as code.
    ///
    /// THE COUNT MASK (FR-075) IS WHY THIS IS NOT A THREE-LINE FUNCTION. An
    /// earlier revision keyed an out-of-count slot's exclusion on
    /// `i >= config_.numLoops` and deleted that slot's coefficients the instant
    /// setNumLoops returned. That is wrong twice: FR-076 declares setNumLoops
    /// SMOOTHED at kGainRampMs, and FR-075 says a dropped loop leaves its
    /// neighbours' coupling as soon as its GATE reaches zero - some 50 ms later,
    /// and by the gate rather than by deleting a coefficient. At SC-001 (d)'s
    /// fixture, where every off-diagonal pair is pre-seeded at
    /// kMaxCouplingPerPair = 0.5, a 0.5-weighted contribution vanishing in one
    /// control step is exactly the discontinuity that arm exists to catch.
    /// Deferring the exclusion to the sleep edge does not fix it either - the
    /// RENORMALISATION would then step there instead, because a smaller row sum
    /// means a larger `scale` and every surviving coefficient jumps UP.
    /// The count mask makes every quantity in the per-sample path continuous
    /// through a count change.
    ///
    /// THE MASK IS APPLIED ON THE SOURCE INDEX j ONLY, and only the APPLIED
    /// values are scaled - the stored targets survive round-trip through
    /// getLoopGain / getCoupling (SC-015 (b)). A loop still fading out keeps
    /// RECEIVING its neighbours' energy at full coupling, exactly as a loop
    /// fading into dormancy does; the two are the same behaviour (FR-075).
    ///
    /// The row sum ranges over j != i and IGNORES wake/dormancy (Q3), and
    /// reaches the count-excluded answer EXACTLY because countSmoother_[j] snaps
    /// to 0.0f - which is what makes SC-015 (d)'s 1e-6 count-inertness claim and
    /// its `== 0.0f` per-pair claim exact rather than approximate.
    void normaliseRows() noexcept {
        // FR-075's count mask, read once. Exactly 1.0f / 0.0f in the steady
        // state (OnePoleSmoother snaps inside kCompletionThreshold).
        std::array<float, kMaxLoops> cm{};
        for (std::size_t j = 0; j < kMaxLoops; ++j) {
            cm[j] = countSmoother_[j].getCurrentValue();
        }

        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            // A slot that is BOTH fully out of the count AND silent contributes
            // nothing and reports nothing. BOTH HALVES ARE REQUIRED:
            // cm[i] == 0.0f alone would zero a row whose gate is still fading,
            // and !engineActive alone would zero a DORMANT row and break
            // SC-015 (d)'s second arm, which requires dormancy to be inert.
            if (cm[i] == 0.0f && !loops_[i].engineActive) {
                appliedOwnFb_[i] = 0.0f;
                appliedTotalGain_[i] = 0.0f;
                for (std::size_t j = 0; j < kMaxLoops; ++j) {
                    appliedCoupling_[j][i] = 0.0f;
                }
                continue;
            }

            const float own = loops_[i].ownFbSmoother.getCurrentValue();
            float g = own;
            for (std::size_t j = 0; j < kMaxLoops; ++j) {
                if (j == i) continue;
                g += cm[j] * couplingSmoother_[j][i].getCurrentValue();
            }
            const float scale = (g > kMaxTotalLoopGain) ? (kMaxTotalLoopGain / g) : 1.0f;

            appliedOwnFb_[i] = own * scale;
            for (std::size_t j = 0; j < kMaxLoops; ++j) {
                // coupling_[i][i] is never used: a diagonal write is a silent
                // no-op and the diagonal reads back 0.0f (FR-030). Self-feedback
                // is ownFb_i, a separate and separately clamped quantity.
                appliedCoupling_[j][i] =
                    (j == i) ? 0.0f
                             : cm[j] * couplingSmoother_[j][i].getCurrentValue() * scale;
            }
            appliedTotalGain_[i] = std::min(g, kMaxTotalLoopGain);
        }
    }

    /// @brief S5.2. ONE 64-sample control step. THE ORDER BELOW IS NORMATIVE -
    /// every numbered step keeps its slot even before its owner has filled it in,
    /// so the ordering is never re-invented from scratch by a later task.
    ///
    /// Step (4b) sits BEFORE (4d)/(4e) so a loop that has just gone to sleep
    /// falls through the engineActive early-out on the SAME step instead of being
    /// written and then cleared. The WAKE edge is not here at all - it lives in
    /// refreshGates(), i.e. in the setter (S7.3).
    void updateControl() noexcept {
        // --- (1) THE DECIMATED LANE ADVANCE, FIRST, before anything reads a
        //         lane. UNCONDITIONAL IN THREE SEPARATE WAYS, each with a
        //         criterion behind it:
        //           * a ZERO-DEPTH lane advances, so raising a depth resumes the
        //             trajectory an always-on lane would have been on (FR-056);
        //           * a DORMANT loop's lanes advance - the Dormancy rule's other
        //             half, and what makes a woken loop's delay and cutoff arrive
        //             already displaced (FR-062, SC-014 (b2));
        //           * setWanderEnabled(false) scales the DEPTHS via
        //             wanderScale(); it does NOT freeze the motion (FR-056,
        //             resonance_drift_network.h:742-744 verbatim).
        //         THE FIXED 64-SAMPLE ARGUMENT is what makes the decimation
        //         SAMPLE-RATE INDEPENDENT: between advances a lane HOLDS its
        //         output, so it experiences 64/fs seconds of its own evolution
        //         per laneDecimation_ * 64/fs seconds elapsed, and fs cancels.
        //         (kControlChunkSamples is static_asserted == 64 at class scope.)
        if (laneCounter_ == 0) {
            for (std::size_t i = 0; i < kMaxLoops; ++i) {
                loops_[i].delayLane.processBlock(kControlChunkSamples);
                loops_[i].cutoffLane.processBlock(kControlChunkSamples);
            }
        }
        laneCounter_ = (laneCounter_ + 1) % laneDecimation_;

        // --- (2) Advance the 42 control-rate smoothers (36 coupling +
        //         6 own-feedback + 6 count-mask; FR-034, FR-075) - EVERY pair on
        //         EVERY control step, whether or not its path contributed. The
        //         else branch at filter_feedback_matrix.h:595-598 exists
        //         precisely because a smoother that stops advancing
        //         desynchronises from its neighbours and STEPS when its path
        //         re-engages.
        //         THE ONE CARVE-OUT is advanceControlSmoother()'s bit-exact
        //         early-out - see the comment on that helper. It is never a
        //         licence to skip an UNSETTLED smoother.
        for (std::size_t from = 0; from < kMaxLoops; ++from) {
            advanceControlSmoother(loops_[from].ownFbSmoother);
            for (std::size_t to = 0; to < kMaxLoops; ++to) {
                // The diagonal advances too. It is never READ (a loop's own
                // feedback is ownFbSmoother) and it is snapped to
                // couplingTarget_[i][i] == 0.0f, so its early-out fires on
                // every step and it costs one comparison.
                advanceControlSmoother(couplingSmoother_[from][to]);
            }
            advanceControlSmoother(countSmoother_[from]);
        }

        // --- (3) normaliseRows() - FR-035. IT RUNS ON THE SMOOTHED VALUES AND
        //         IS THE LAST STEP BEFORE THE COEFFICIENTS REACH THE PER-SAMPLE
        //         PATH, which is what makes the applied row sum bounded at
        //         EVERY INSTANT and not only at the settled end points
        //         (SC-015 (a)).
        normaliseRows();

        // --- (4) Per loop, for ALL kMaxLoops - a slot above the count must still
        //         fade and take its sleep edge (FR-075).
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];

            // (4a) Both lane mappings, for EVERY loop INCLUDING A SKIPPED ONE:
            //      FR-062 makes the target getters the only observable proof the
            //      lanes are alive, and SC-014 (b) asserts on them.
            mapLaneTargets(i);

            // (4b) THE SLEEP EDGE (FR-063), BEFORE ANY WRITE, so a loop that has
            //      just gone to sleep falls through (4c)'s early-out on the SAME
            //      step instead of being written and then cleared.
            //
            //      THE EXACT `== 0.0f` IS CORRECT BY CONSTRUCTION, not by luck:
            //      gateSteady() returns a LITERAL 0.0f in all three sleeping
            //      cases and LinearRamp::process() lands exactly ON its target
            //      rather than approaching it (smoother.h:379-383).
            //
            //      *** THE CLEAR IS THE RATIFIED DEVIATION FROM THE DORMANCY
            //      RULE, AND THIS IS ITS JUSTIFICATION. *** The rule as written
            //      says only that the chain is skipped. A feedback loop has no
            //      generator behind it - the loop IS the chain - so "skipping"
            //      it freezes a fully charged delay line. On the wake edge that
            //      frozen ring would be re-injected at full amplitude behind a
            //      50 ms fade, minutes after the audio that produced it: a
            //      listener hears a stale burst, not a loop opening. At 30 s of
            //      resonator RT60 and a 449 ms line the stored energy is
            //      substantial. Clearing here makes a woken loop refill from its
            //      input tap, which is what "a feedback agent opens an ecology
            //      loop's coupling" is supposed to sound like. SC-014 (d)
            //      asserts it at -80 dBFS over the first 500 ms after a silent
            //      wake; a build that only skips the chain fails by 60 dB or
            //      more. House support: Phase 3's own sleep-edge clear
            //      (resonance_drift_network.h:1681-1686).
            if (L.engineActive && L.gate.isComplete() && L.gate.getCurrentValue() == 0.0f) {
                clearLoopAudio(i);  // FR-019's ONE owner, and O(1) (S3.1)
                L.engineActive = false;
            }

            // (4c) FR-062: nothing below reaches the audio objects.
            if (!L.engineActive) continue;

            // (4d) THE READ-MUTE FREEZE IS LOAD-BEARING, not an optimisation:
            //      clearLoopAudio()'s window is exactly one delay length only if
            //      the delay position does not GROW while it runs (S3.1).
            //      Skipping the write for at most one delay length costs a
            //      deferred crossfade against a lane whose fastest correlation
            //      time is 1 s.
            if (L.readMuteSamples == 0) {
                L.delay.setDelayMs(L.targetDelayMs);
                // THE FR-071 CROSSFADE-ONSET COUNT (S5.5), taken here and only
                // here. A crossfade can BEGIN only inside setDelaySamples
                // (crossfading_delay_line.h:180-190), which this component calls
                // only from this line, so the counter is an EXACT count of
                // ONSETS - not of completed steps; the two differ whenever a
                // crossfade is retriggered mid-fade (:176-181). The sampling
                // cannot miss one: a 20 ms crossfade spans ~15 control steps at
                // 48 kHz.
                const bool xf = L.delay.isCrossfading();  // :301
                if (xf && !L.lastCrossfading) ++L.crossfadeCount;
                L.lastCrossfading = xf;
            }

            // (4e) The SVF's own per-sample smoother carries the cutoff - pushed
            //      only when the target really moved (kCutoffPushRelative), so
            //      the smoother can settle and its per-sample early-out applies.
            if (std::abs(L.targetCutoffHz - L.pushedCutoffHz) >
                kCutoffPushRelative * L.pushedCutoffHz) {
                L.svf.setCutoff(L.targetCutoffHz);
                L.pushedCutoffHz = L.targetCutoffHz;
            }
        }

        // --- (5) THE GOVERNOR (S5.6, FR-043 - FR-045). ONCE per control step
        //         (750/s at 48 kHz), never per sample: the follower's own 20 ms
        //         attack is far slower than the 1.33 ms grid, so evaluating the
        //         law at 750 Hz loses nothing the follower could have expressed
        //         (D-9), and it buys ONE std::pow per 64 samples instead of 64.
        //         The tracker itself IS fed per sample - renderChunk step (4),
        //         with normGain * bSum, the SAME 1/sqrt(numLoops) FR-017 applies
        //         to the wet sum (Q6). Without that scaling "-6 dB" would name a
        //         level up to 10*log10(6) = 7.8 dB apart between numLoops = 1
        //         and 6, which is exactly what SC-006 (f) measures.
        const float rms = follower_.getCurrentValue();  // read ONLY here

        // *** THE FOLLOWER'S OWN HEALTH GUARD (FR-047, rung 5's OTHER half) ***
        // NOT defensive, and NOT redundant with the per-sample b_i trap - the
        // per-sample trap is UNREACHABLE from this failure mode, and without
        // this guard the component mutes itself permanently and silently. The
        // chain, verified end to end:
        //   (1) EnvelopeFollower::processRMS takes the release branch on ANY NaN
        //       comparison and recomputes squaredEnvelope_ = squared +
        //       releaseCoeff_ * (NaN - squared) = NaN forever
        //       (envelope_follower.h:313-322); envelope_ = std::sqrt(NaN) (:325).
        //   (2) the law below then yields
        //       target = std::clamp(std::pow(NaN, ...), 0.05f, 1.0f) = NaN,
        //       because std::clamp returns v when both comparisons are false.
        //   (3) LinearRamp::setTarget does NOT propagate that NaN - it MUTES:
        //       target_ = current_ = increment_ = 0.0f (smoother.h:342-348).
        //   (4) govGain is then exactly 0.0f, y_i = fastTanh(b_i * 0) = 0, and
        //       every b_i stays FINITE on every subsequent sample - so the S6
        //       step-3 trap never fires, both health counters read clean, and
        //       nothing outside reset()/prepare() ever clears follower_.
        // The result would be a component muted for the life of the object with
        // both health counters reading clean: exactly the "fails catastrophically
        // and quietly" mode the ladder exists to close. The claim that the
        // governor "can never mute" because kGovernorMinGain = 0.05 is FALSE on
        // this path, and this guard is what makes it true. Cost: one ordered
        // bit-pattern comparison per control step.
        if (!detail::isFinite(rms)) {
            follower_.reset();              // envelope_follower.h:128 - the only cure
            ++nonFiniteResets_;             // the counter SC-012 (c) asserts on
            retargetGovernorRamp(1.0f);     // recover to unity over kGovernorRampMs
            return;
        }

        const float threshold = dbToGain(governorThresholdDb_);  // in (0, 1]
        const float over = rms / threshold;
        // ratio == 1 is EXACTLY unity: the exponent 1/1 - 1 is exactly 0.0f and
        // std::pow(x, 0.0f) is exactly 1.0f for every finite positive x. That is
        // the documented "governor off" setting (FR-044) and it is why SC-006
        // (d)'s exact-identity claim is reachable at all.
        const float target =
            (over <= 1.0f)
                ? 1.0f
                : std::clamp(std::pow(over, 1.0f / governorRatio_ - 1.0f), kGovernorMinGain, 1.0f);

        // Second half of the same guard: governorThresholdDb_ and governorRatio_
        // are finite by FR-009 and rms is finite by the branch above, so `target`
        // is finite by construction TODAY. The test is written anyway, because
        // the failure mode it catches is a silent permanent mute rather than an
        // audible artefact, and because a later phase adding a term to this law
        // must not be able to reintroduce it.
        retargetGovernorRamp(detail::isFinite(target) ? target : 1.0f);
    }

    /// @brief FR-045's ramp write - issued ONLY when the target actually moves.
    ///
    /// THE GUARD IS NOT AN OPTIMISATION. Re-issuing an unchanged target is not a
    /// no-op on `LinearRamp`: `setTarget` recomputes
    /// `increment_ = (target_ - current_) / rampSamples` (smoother.h:353) from
    /// the CURRENT position, so a target re-issued every control step turns the
    /// 20 ms LINEAR ramp into a GEOMETRIC approach that covers only
    /// `kControlChunkSamples / (kGovernorRampMs * fs / 1000) = 64/960 = 6.67 %`
    /// of the remaining distance per step and therefore never trips
    /// `process()`'s overshoot clamp (smoother.h:378-382) - the one place a
    /// `LinearRamp` is ever set exactly equal to its target.
    ///
    /// It then STALLS one rounding step short, permanently: once the remainder
    /// falls under `rampSamples * ulp(1.0f)/2 = 960 * 5.96e-8 / 2 = 2.9e-5`,
    /// `current_ += increment_` rounds back to `current_` and the gain sticks at
    /// `0.999973` for the life of the object. Measured this session on the
    /// SC-006 sweep before the guard: every step AFTER the governor's first
    /// momentary engagement reported `getGovernorGain() == 0.999973f` even at
    /// input levels 20 dB below the threshold, which (a) breaks SC-006 (a)'s
    /// "exactly 1.0f below the threshold" and (b) made SC-006 (f)'s crossing
    /// level report the first MOMENTARY touch of the threshold rather than the
    /// level at which the governor is actually engaged - a 6 dB error there.
    ///
    /// With the guard the constant-target case is the shipped linear ramp: the
    /// increment is computed once, `process()` walks it to the target and the
    /// overshoot clamp lands it exactly on `1.0f` within `kGovernorRampMs`.
    /// While the law's target is genuinely moving (`over > 1`) the behaviour is
    /// unchanged - a fresh increment per control step is what tracking means.
    void retargetGovernorRamp(float target) noexcept {
        if (target != governorRamp_.getTarget()) governorRamp_.setTarget(target);
    }

    /// @brief S6 - the per-sample law (FR-015), six steps, in this order.
    ///
    /// Called only from processBlockTapped, only with `n` in
    /// [1, kControlChunkSamples - controlPhase_], and only on a prepared
    /// instance. `tapOffset` is the caller's absolute offset into the block, so
    /// the taps line up with outL/outR however the chunk loop partitions it.
    ///
    /// SEVEN THINGS IN THIS BODY ARE DECISIONS, NOT STYLE (S6):
    ///  1. THERE IS EXACTLY ONE LOOP-GAIN FACTOR PER ROUND TRIP AND IT SITS IN
    ///     THE INPUT SUM. An output-side `* loopGain` as well would apply the
    ///     same stored quantity twice per circulation (0.90^2 = 0.81, not 0.90)
    ///     and would break FR-041's l-infinity row-sum argument, which is exactly
    ///     "the absolute coefficients feeding loop i sum to G_i <= 0.95" (S8.1).
    ///  2. TWO previous-sample vectors, not one (Q4, FR-031). Own feedback reads
    ///     prevY_ (PRE-gate) so a loop's circulation is untouched by its own wake
    ///     state; cross-coupling reads prevOut_ (POST-gate) so a loop fading
    ///     toward sleep fades its contribution to its neighbours continuously
    ///     instead of stepping at the end of the 50 ms ramp.
    ///  3. The dry is sanitised PER CHANNEL, and the same sanitised value feeds
    ///     the mono engine. Without it a non-finite input reaches outL through
    ///     the crossfade at any mix < 1.
    ///  4. mix == 0.0f takes an EXPLICIT branch: (1-0)*dry + 0*wet is bit-exact
    ///     for every finite dry EXCEPT -0.0f, where -0.0f + 0.0f is +0.0f, and
    ///     SC-018 (a) asserts bit-identity.
    ///  5. A skipped loop's `continue` does NO stores - prevY_[i] and prevOut_[i]
    ///     are already exactly 0.0f from the sleep edge's clearLoopAudio, which
    ///     makes FR-062 true BY CONSTRUCTION and buys SC-004 (c)'s dormant saving.
    ///  6. The tap write reads the ALREADY-COMPUTED prevOut_[i]; there is no
    ///     second arithmetic path, which is what SC-016's bit-identity requires.
    ///  7. The read-mute branch IS the whole of FR-063's clear on the audio
    ///     thread - one predicted branch instead of a 131 KB-524 KB std::fill
    ///     (S3.1). It sits INSIDE the engineActive block deliberately: a sleeping
    ///     loop is not rendered, so its window does not count down while it
    ///     sleeps and is recomputed at the FR-064 wake edge instead.
    ///
    /// `wet` CANNOT be NaN by construction, so the ordered clamp (which a NaN
    /// would walk straight through, every comparison against NaN being false) is
    /// sufficient: the FR-047 trap has already made every b_i finite, fastTanh
    /// of a finite product is finite, every ramp is finite and wetTrim <= 15.85.
    void renderChunk(const float* inL, const float* inR, float* outL, float* outR,
                     float* const* loopTaps, std::size_t tapOffset, std::size_t n) noexcept {
        std::uint32_t engagements = 0;  // chunk-local; folded into the member once

        for (std::size_t s = 0; s < n; ++s) {
            // --- (0) sanitise the dry, PER CHANNEL, before the mono sum -------
            const float dryL = detail::isFinite(inL[s]) ? inL[s] : 0.0f;
            const float dryR = detail::isFinite(inR[s]) ? inR[s] : 0.0f;
            const float monoIn = 0.5f * (dryL + dryR);  // FR-016: the engine is MONO

            // --- (1) advance EVERY per-sample ramp, UNCONDITIONALLY, for EVERY
            //         slot - dormant and out-of-count included. The ramps must be
            //         a pure function of the ABSOLUTE sample count however the
            //         caller partitions its blocks, which is exactly what SC-010
            //         asserts. A LinearRamp already at its target early-outs
            //         (smoother.h:372-374), so this costs one predicted branch.
            const float normGain = normGainRamp_.process();
            const float govGain = governorRamp_.process();
            const float m = mixRamp_.process();
            const float wetTrim = wetGainRamp_.process();
            std::array<float, kMaxLoops> gate{};
            std::array<float, kMaxLoops> tapGain{};
            for (std::size_t i = 0; i < kMaxLoops; ++i) {
                gate[i] = loops_[i].gate.process();
                tapGain[i] = loops_[i].inputRamp.process();
            }

            // --- (2) form ALL inputs from the PREVIOUS sample, before ANY loop's
            //         stages run. This is what makes the network independent of
            //         loop index order (FR-031, D-7).
            //
            //         BOTH BOUNDS ARE kMaxLoops AND engineActive, NEVER
            //         config_.numLoops, AND THAT IS LOAD-BEARING (FR-075,
            //         SC-001 (d)). An earlier revision bounded both loops by
            //         config_.numLoops, which setNumLoops writes IMMEDIATELY
            //         while every quantity FR-075 declares smoothed - the count
            //         mask, the gates, normGain - takes kGainRampMs to move. The
            //         effect was a HARD CUT that defeated the very smoothing
            //         normaliseRows() exists to provide: at SC-001 (d)'s fixture,
            //         where every off-diagonal pair sits at kMaxCouplingPerPair,
            //         a setNumLoops(6 -> 1) removed five terms of
            //         cm[j] * 0.5 * scale * prevOut_[j] - about 0.74 of the
            //         survivor's input sum - from one sample to the next, and the
            //         step arrived at the output one delay period later. Measured
            //         at 4.93e-4 on a 110 Hz-sine render whose peak was 0.4325,
            //         through a loop whose delay a previous jump had set to
            //         kMinDelayMs.
            //
            //         SC-001 (d)'s smooth-carrier pass IS THE REGRESSION GUARD,
            //         and deliberately so - a cheaper case was written and then
            //         DELETED because it could not discriminate. Reverting these
            //         two bounds to config_.numLoops takes that pass from 0
            //         detections to exactly 1 (jump 256, setNumLoops, offset 513,
            //         amplitude 4.93e-4); restoring them takes it back to 0. The
            //         effect only reaches that size once the network is in the
            //         state 256 random extreme jumps have put it in - short
            //         delays, opened resonators, loops carrying real energy - and
            //         three short fixtures were measured on both builds and read
            //         the same to three digits (drive on, worst frame slope/level
            //         ratio 1.009 vs 1.009; freely ringing, 4.18 vs 4.21). A test
            //         that reads the same on both builds is not a guard, so the
            //         guard is the criterion that actually moves.
            //
            //         Bounding by kMaxLoops is not a widening of the network: the
            //         count mask is ALREADY folded into appliedCoupling_[j][i] by
            //         normaliseRows(), and cm[j] SNAPS to exactly 0.0f for an
            //         out-of-count slot (OnePoleSmoother's kCompletionThreshold),
            //         so every excluded term is exactly 0.0f in the steady state
            //         and the arithmetic is bit-identical to the old bound at both
            //         numLoops = 1 and numLoops = 6. Only the TRANSITION differs,
            //         and only by being continuous.
            //
            //         The OUTER bound is engineActive - the same predicate step
            //         (3) uses - so a slot fading out of the count keeps being fed
            //         its own tap while it fades, which is what makes FR-075's
            //         "a dropped loop is dropped exactly the way a sleep is done"
            //         true of the INPUT sum as well as of the gate. A slot that is
            //         not engineActive is skipped by step (3) anyway, so computing
            //         its x would be dead work.
            std::array<float, kMaxLoops> x{};
            for (std::size_t i = 0; i < kMaxLoops; ++i) {
                if (!loops_[i].engineActive) continue;
                float acc = tapGain[i] * monoIn + appliedOwnFb_[i] * prevY_[i];  // PRE-gate
                for (std::size_t j = 0; j < kMaxLoops; ++j) {
                    if (j == i) continue;
                    acc += appliedCoupling_[j][i] * prevOut_[j];  // POST-gate (Q4)
                }
                x[i] = acc;
            }

            // --- (3) run the chains. prevY_ and prevOut_ were fully consumed by
            //         (2), so writing them here cannot disturb any other loop.
            float bSum = 0.0f;
            float wetSum = 0.0f;
            for (std::size_t i = 0; i < kMaxLoops; ++i) {
                Loop& L = loops_[i];
                if (!L.engineActive) continue;  // FR-062 - note 5 above

                const float sf = L.svf.process(x[i]);  // FR-011

                // FR-020: write then read - EXCEPT inside the read-mute window,
                // where the loop WRITES but does not READ, so nothing the buffer
                // held before clearLoopAudio() can reach the output.
                float d;
                if (L.readMuteSamples != 0) {
                    L.delay.write(sf);  // crossfading_delay_line.h:223
                    d = 0.0f;
                    --L.readMuteSamples;
                } else {
                    d = L.delay.process(sf);  // :291 - write then read
                }

                const float r = L.resonator.process(d);  // FR-013
                float b = L.dcBlocker.process(r);        // FR-014, LAST in the loop

                if (!detail::isFinite(b)) {  // RUNG 5 (FR-047)
                    clearLoopAudio(i);       // O(1) - S3.1; a per-sample call site
                                             //   cannot afford an O(buffer) fill
                    follower_.reset();
                    ++nonFiniteResets_;
                    b = 0.0f;
                }
                bSum += b;

                // RUNGS 2 + 3, NO loop gain. FastMath::fastTanh, NOT std::tanh:
                // OQ-2 LEVER 1, applied from FR-080 measured stage probe
                // (feedback_ecology_perf_test.cpp arm (f) of the probe table -
                // std::tanh x 6 costs 18737 ns/block against fastTanh 8689,
                // 10048 ns/block of the FR-080 budget for a Pade (5,4)
                // approximant). MEASURED by FeedbackEcology_FastTanhErrorBound:
                // max |fastTanh - std::tanh| = 1.822e-3, at the x = 3.5
                // saturation seam where fastTanh returns exactly 1 and std::tanh
                // is still 0.9981779; the relative error for |x| <= 1, which is
                // the range FR-042 calls "within 2 % of linear", is 2.51e-7.
                // THE BOUND IS PRESERVED, which is the only thing rung 2 owes
                // FR-042: fastTanh returns exactly +/-1 outside +/-3.5
                // (fast_math.h:74-80) and inside it the Pade quotient
                // x(945 + 105x^2 + x^4)/(945 + 420x^2 + 15x^4) rises
                // monotonically to 0.999245 at x = 3.5, so |y| <= 1 holds for
                // EVERY finite input exactly as std::tanh does. Asserted, both
                // halves - the bound and the error - by
                // FeedbackEcology_FastTanhErrorBound.
                const float y = FastMath::fastTanh(b * govGain);
                const float o = y * gate[i];             // FR-061's 50 ms wake ramp
                prevY_[i] = detail::flushDenormal(y);    // FR-084
                prevOut_[i] = detail::flushDenormal(o);
                wetSum += prevOut_[i];
            }

            // --- (4) the governor's tracker, EVERY sample, on FR-043's quantity
            static_cast<void>(follower_.processSample(normGain * bSum));

            // --- (5) the output stage - THE ORDER IS NORMATIVE (FR-015, FR-046,
            //         FR-072): trim the normalised wet sum, flush, clamp, mix.
            float wet = wetTrim * (wetSum * normGain);  // FR-017 then FR-072's trim
            wet = detail::flushDenormal(wet);
            if (wet > kOutputClamp) {  // RUNG 4
                wet = kOutputClamp;
                ++engagements;
            } else if (wet < -kOutputClamp) {
                wet = -kOutputClamp;
                ++engagements;
            }

            if (m == 0.0f) {  // FR-072: BIT-EXACT dry - the wet is NOT summed
                outL[s] = dryL;
                outR[s] = dryR;
            } else {
                outL[s] = (1.0f - m) * dryL + m * wet;
                outR[s] = (1.0f - m) * dryR + m * wet;
            }

            // --- (6) FR-073's observation-only tap write ----------------------
            if (loopTaps != nullptr) {
                for (std::size_t i = 0; i < kMaxLoops; ++i) {
                    if (loopTaps[i] != nullptr) loopTaps[i][tapOffset + s] = prevOut_[i];
                }
            }
        }

        clampEngagements_ += engagements;
    }

    /// @brief S7.1. The gate value a loop settles at, folding FR-060's two
    /// controls and FR-075's count into ONE number - which is what makes
    /// setLoopWake(0) and setLoopDormant(true) behaviourally indistinguishable
    /// (SC-014 (a)).
    ///
    /// The kWakeSilenceEpsilon snap happens HERE, at the source, which keeps the
    /// sleep edge's exact `== 0.0f` test valid by construction once a Phase-8
    /// agent writes getEnvelopeValue() * getActiveDepth() into setLoopWake: a
    /// release tail that stops at 1e-8 must not leave a loop burning forever.
    ///
    /// DEFINED BY T008 because prepare() step 12 and reset() step 4 must snap
    /// the gate ramp to it. refreshGates(), the sleep and wake edges and the two
    /// life-cycle setters are all built ON TOP of this ONE value; none of them
    /// re-derives it, which is what keeps the three sleeping cases in agreement.
    [[nodiscard]] float gateSteady(std::size_t i) const noexcept {
        // FR-075: an out-of-count loop sleeps.
        if (i >= kMaxLoops || i >= config_.numLoops) return 0.0f;
        const Loop& L = loops_[i];
        if (L.dormant) return 0.0f;
        return (L.wakeAmount <= kWakeSilenceEpsilon) ? 0.0f : L.wakeAmount;
    }

    /// @brief S7.3. THE WAKE EDGE (FR-064) AND THE GATE RE-TARGET, both of them
    /// in the SETTER. Exactly three callers - setLoopWake, setLoopDormant and
    /// setNumLoops - and nothing else. prepare() and reset() bypass it
    /// deliberately, snapping the gate and seeding lastGateTarget in the same
    /// breath so the shadow can never start out of sync.
    ///
    /// WHY THE WAKE EDGE IS NOT AT THE NEXT CONTROL STEP. A setter called
    /// mid-chunk starts the ramp moving on the next SAMPLE, so a loop left
    /// engineActive == false while its gate was already rising would be silent
    /// for up to 63 samples - a <= 1.3 ms attack notch. Keeping the test
    /// unconditional costs one bool and one float compare and makes the bad
    /// state unreachable (resonance_drift_network.h:1288-1306).
    ///
    /// WHY THE `target != lastGateTarget` GUARD IS LOAD-BEARING, NOT AN
    /// OPTIMISATION. LinearRamp::setTarget recomputes
    /// increment_ = (target - current) / (rampMs * 0.001 * fs) on EVERY call
    /// (smoother.h:342-354), so re-targeting a gate that is mid-ramp to the
    /// value it is already heading for RESTARTS its 50 ms from wherever it has
    /// got to. A Phase-8 agent writing one loop's wake per block would otherwise
    /// stretch every OTHER loop's ramp without bound. The compare is exact
    /// because the target is either a literal 0.0f from gateSteady() or the
    /// value setLoopWake stored verbatim.
    void refreshGates() noexcept {
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            const float target = gateSteady(i);

            if (!L.engineActive && target != 0.0f) {
                // THE WAKE EDGE (FR-064), deliberately OUTSIDE the
                // change-detection guard below. Both writes are safe here
                // precisely because the loop is silent and FR-063 has already
                // cleared it - there is no signal in flight to click.
                //
                // snapToDelayMs (crossfading_delay_line.h:213), NOT setDelayMs:
                // a queued crossfade would fire at the wake edge from a stale
                // tap position instead of being snapped.
                L.delay.snapToDelayMs(L.targetDelayMs);
                // S3.1's read-mute window, RECOMPUTED from the position the loop
                // actually wakes at. The snap above may have moved the tap a
                // long way from where clearLoopAudio() measured it at the sleep
                // edge, and the window is exactly one delay length only if it is
                // measured AFTER the snap.
                L.readMuteSamples =
                    static_cast<std::size_t>(std::ceil(L.delay.getCurrentDelaySamples())) + 1u;
                L.svf.setCutoff(L.targetCutoffHz);
                L.pushedCutoffHz = L.targetCutoffHz;
                L.svf.snapToTarget();  // svf.h:309 - no 5 ms glide from a
                                       //   minutes-old coefficient
                L.lastCrossfading = false;
                L.engineActive = true;
            }

            if (target != L.lastGateTarget) {  // *** LOAD-BEARING - see above ***
                L.gate.setTarget(target);
                L.lastGateTarget = target;
            }
        }
    }

    /// @brief S5.4. One lane reading, re-clamped, with a non-finite reading
    /// mapped to 0. BrownianDrift already clamps to [-1, +1]
    /// (brownian_drift.h:212-214); the second clamp costs nothing and makes the
    /// range a property of THIS file.
    [[nodiscard]] static float laneValue(const BrownianDrift& lane) noexcept {
        const float v = lane.getCurrentValue();
        return detail::isFinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
    }

    /// @brief FR-056 / Q2. Off zeroes the DEPTH TERM and nothing else: the lanes
    /// keep advancing and the mapped values glide back to base over their own
    /// smoothing paths. Phase 3's shipped behaviour verbatim.
    [[nodiscard]] float wanderScale() const noexcept { return wanderEnabled_ ? 1.0f : 0.0f; }

    /// @brief S5.4's two mappings for one loop, THE ONE OWNER of targetDelayMs
    /// and targetCutoffHz.
    ///
    /// Delay is MULTIPLICATIVE so a 449 ms loop and a 41 ms loop move by
    /// comparable fractions; a fixed +/-ms depth would move the long loop
    /// imperceptibly and the short one by a tenth of its length. Cutoff is in
    /// the LOG2 DOMAIN so an octave of wander is an octave at every base -
    /// baseLog2Cutoff is cached by setLoopCutoffHz and applyDefaults(), so the
    /// cost is one std::exp2 and NO std::log2.
    ///
    /// DEFINED BY T008 for prepare() step 15. T010's updateControl() step 4a
    /// maps all six loops through THIS body (including skipped ones - the target
    /// getters are the only observable proof the lanes are alive).
    void mapLaneTargets(std::size_t i) noexcept {
        Loop& L = loops_[i];
        const float scale = wanderScale();
        L.targetDelayMs =
            std::clamp(L.baseDelayMs
                           * (1.0f + scale * L.delayWanderFrac * laneValue(L.delayLane)),
                       kMinDelayMs, kMaxDelayMs);
        L.targetCutoffHz =
            std::clamp(std::exp2(L.baseLog2Cutoff
                                 + scale * L.cutoffWanderOct * laneValue(L.cutoffLane)),
                       kMinCutoffHz, maxCutoffHz_);
    }

    /// @brief prepare() step 15 and reset() step 7: evaluate both mappings once
    /// from the just-rewound lanes (both sit at mean 0, so both map to base) and
    /// write them to the audio objects WITHOUT a transient.
    ///
    /// The filter mode and Q are pushed here too, and that is not decoration:
    /// applyDefaults() restores the CONFIGURATION members but is forbidden from
    /// touching audio-object state (S9), so on a re-prepare of an object whose
    /// caller had set FilterMode::Highpass the SVF would still be a highpass
    /// while getLoopFilterMode() reported Lowpass. FR-005 (c) is only true if
    /// one step writes them, and this is that step.
    void snapControlState() noexcept {
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            mapLaneTargets(i);

            L.svf.setMode(toSvfMode(L.filterMode));
            L.svf.setResonance(L.filterQ);
            L.svf.setCutoff(L.targetCutoffHz);
            L.pushedCutoffHz = L.targetCutoffHz;
            L.svf.snapToTarget();  // svf.h:309 - no coefficient glide from the old rate

            L.delay.snapToDelayMs(L.targetDelayMs);
            L.lastCrossfading = false;
        }
    }

    /// @brief FR-019's ONE owner, and O(1) (S3.1). RT-safe on the audio thread,
    /// which prepare()/reset()'s extra delay.reset() deliberately is not.
    ///
    /// WHY THERE IS NO DelayLine::reset() HERE. That call is a std::fill over
    /// the whole power-of-two buffer - 131 072 B per loop at 44.1/48 kHz and
    /// 524 288 B at 192 kHz - and TWO of this function's four callers run on the
    /// AUDIO THREAD (updateControl() step 4b's FR-063 sleep edge and
    /// renderChunk() step 3's FR-047 trap).
    /// setNumLoops(6 -> 1) drops five loops whose gates settle on the same
    /// sample, so six clears can land inside ONE 64-sample control chunk against
    /// an 8 889 ns chunk budget - a single 131 KB fill already exceeds it.
    ///
    /// WHAT REPLACES IT. FR-063 requires that a woken loop refills from its
    /// input tap rather than replaying the stale ring. CrossfadingDelayLine's
    /// write() (:223) and read() (:233) are separable, so a loop that WRITES but
    /// does not READ cannot emit anything the buffer already held: the delay
    /// half of the clear is a READ-MUTE WINDOW of exactly one delay length.
    /// One delay length is exactly enough only if the position does not GROW
    /// while the window runs, which is why renderChunk freezes it (the control
    /// step skips setDelayMs while readMuteSamples != 0) and why the +1u absorbs
    /// the fractional read.
    ///
    /// It does NOT reset crossfadeCount - that counter is monotone for the life
    /// of the object except across prepare() and reset(), which clear it
    /// themselves.
    void clearLoopAudio(std::size_t i) noexcept {
        Loop& L = loops_[i];
        L.svf.reset();        // svf.h:277 - clears both integrators AND snaps g/k/mix
        L.resonator.reset();  // biquad.h:385 - clears z1_/z2_, LEAVES the coefficients
        L.dcBlocker.reset();  // dc_blocker.h:155 - x1_ = y1_ = 0

        // snapToDelaySamples (:202-209) writes targetDelaySamples_, both taps,
        // crossfading_ = false and crossfadePosition_ = 0 - everything
        // CrossfadingDelayLine::reset() does EXCEPT the O(buffer) wipe. Snapping
        // to the CURRENT position, not the target, is what makes the mute length
        // below exact.
        const float delaySamples = L.delay.getCurrentDelaySamples();  // :306
        L.delay.snapToDelaySamples(delaySamples);
        L.readMuteSamples = static_cast<std::size_t>(std::ceil(delaySamples)) + 1u;

        prevY_[i] = 0.0f;  // FR-031's two previous-sample vectors
        prevOut_[i] = 0.0f;
        L.lastCrossfading = false;
    }

    /// FR-011's mode map. FeedbackEcology::FilterMode is NOT SVFMode renumbered -
    /// SVFMode is {Lowpass, Highpass, Bandpass, ...} (svf.h:38-47) while this
    /// component's APPEND-ONLY enum is {Lowpass, Bandpass, Highpass} - so the
    /// translation must be an explicit switch and never a static_cast.
    [[nodiscard]] static SVFMode toSvfMode(FilterMode mode) noexcept {
        switch (mode) {
            case FilterMode::Bandpass:
                return SVFMode::Bandpass;
            case FilterMode::Highpass:
                return SVFMode::Highpass;
            case FilterMode::Lowpass:
                break;
        }
        return SVFMode::Lowpass;
    }

    /// @brief FR-013's coefficient path (S4.1): a direct RBJ constant-peak-gain
    /// bandpass, written with Biquad::setCoefficients (biquad.h:325).
    ///
    /// NEVER Biquad::configure (biquad.h:330) and never SmoothedBiquad::setTarget
    /// (:547): both route through BiquadCoefficients::calculate, which clamps Q at
    /// biquad.h's kMaxQ = 30 (:53, :673) and would silently undercut the
    /// kMaxResonatorQ = 100 ceiling FR-013 specifies - the realised ring at
    /// 210 Hz would be 0.314 s instead of 1.000 s. Transcribed from
    /// ResonatorBank::updateFilterCoefficients (resonator_bank.h:651-682), the
    /// route that actually reaches Q = 100.
    ///
    /// An arbitrarily high Q is safe here: the RBJ bandpass is documented
    /// "Constant 0 dB peak gain" (biquad.h:71) and is built normalised by
    /// a0 = 1 + alpha, so its magnitude is exactly 1 at the centre and strictly
    /// below 1 everywhere else, whatever Q is. A higher Q lengthens the ring
    /// without raising the peak. That is rung 2 of FR-041.
    ///
    /// Callers: the constructor, setLoopResonanceHz, setLoopResonanceRt60, and
    /// prepare() step 10. A retune is a STEPPED hard swap (FR-076): the new
    /// coefficients are in force on the very next sample, with no interpolation,
    /// because the centre and RT60 are rare user-set controls and not wander
    /// targets. FR-041 is unaffected - each end point is independently
    /// unity-peak, so there is no interpolated intermediate set to be unstable.
    void updateResonator(std::size_t i) noexcept {
        Loop& L = loops_[i];
        const float fs = static_cast<float>(sampleRate_);

        // Both clamp pairs are the construction-seeded / step-3 cached bounds,
        // whose ORDERING the class-scope static_asserts pin at every accepted
        // rate (R-8).
        const float f = std::clamp(L.resonanceHz, kMinResonatorFrequency, maxResonanceHz_);
        // rt60ToQ already clamps to [kMinResonatorQ, kMaxResonatorQ]
        // (resonator_bank.h:97); the outer clamp is written anyway so the ceiling
        // this component depends on is visible at the call site.
        const float q =
            std::clamp(rt60ToQ(f, L.resonanceRt60Req), kMinResonatorQ, kMaxResonatorQ);
        L.appliedResonanceQ = q;  // the TRUTH getLoopResonanceRt60 reports back (S4.2)

        const float omega = kTwoPi * f / fs;
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * q);

        const float a0 = 1.0f + alpha;
        const float invA0 = 1.0f / a0;

        BiquadCoefficients c;
        c.b0 = alpha * invA0;
        c.b1 = 0.0f;  // the RBJ bandpass has no b1 term
        c.b2 = -alpha * invA0;
        c.a1 = (-2.0f * cosOmega) * invA0;
        c.a2 = (1.0f - alpha) * invA0;
        L.resonator.setCoefficients(c);
    }

    /// @brief The ONE owner of the default tables (FR-005 (c), S9).
    ///
    /// Called by the constructor and by prepare() step 7 - so an UNPREPARED
    /// object already reports kDefaultLoopDelayMs[i] and friends, which is what
    /// makes S9's unprepared-state rule true rather than aspirational. The four
    /// per-index tables cannot be written as default member initialisers because
    /// a member initialiser has no loop index.
    ///
    /// It must NOT touch PrepareConfig fields (prepare() step 2 owns those) and
    /// must NOT touch any AUDIO-OBJECT state: the rate-dependent snap of delay
    /// positions and SVF coefficients is prepare() step 15's snapControlState(),
    /// which needs a prepared CrossfadingDelayLine and stays there.
    void applyDefaults() noexcept {
        for (std::size_t i = 0; i < kMaxLoops; ++i) {
            Loop& L = loops_[i];
            L.baseDelayMs = kDefaultLoopDelayMs[i];
            L.baseCutoffHz = kDefaultLoopCutoffHz[i];
            // detail::constexprLn / detail::kLn2, NEVER a constexpr std::log2:
            // that is a GCC/MSVC builtin extension Clang REJECTS, so the macOS
            // and Linux legs would break while Windows stayed green (S5.4,
            // resonance_drift_network.h:255-266). SC-017's constexpr-log arm pins
            // this series to std::log2 at runtime.
            L.baseLog2Cutoff = detail::constexprLn(kDefaultLoopCutoffHz[i]) / detail::kLn2;
            L.filterQ = kDefaultFilterQ;
            L.filterMode = FilterMode::Lowpass;
            L.resonanceHz = kDefaultLoopResonanceHz[i];
            L.resonanceRt60Req = kDefaultResonanceRt60;
            L.ownFbTarget = kDefaultLoopGain;
            L.inputGain = kDefaultLoopInputGain;
            L.delayWanderFrac = kDefaultDelayWanderFraction[i];
            L.cutoffWanderOct = kDefaultCutoffWanderOctaves;
            L.wakeAmount = 1.0f;
            L.dormant = false;
        }

        // FR-033: kDefaultCoupling from each loop to its two cyclic neighbours
        // (i -> i +/- 1 mod numLoops) and 0 elsewhere. A ring rather than
        // all-to-all because six loops all-to-all at 4 % is a row sum of 0.20,
        // five times the ring's 0.08, and the roadmap's "neighbours" is literal.
        for (auto& row : couplingTarget_) {
            row.fill(0.0f);
        }
        const std::size_t n = config_.numLoops;
        if (n >= 2) {
            for (std::size_t i = 0; i < n; ++i) {
                couplingTarget_[i][(i + 1) % n] = kDefaultCoupling;
                couplingTarget_[i][(i + n - 1) % n] = kDefaultCoupling;
            }
        }

        mix_ = kDefaultMix;
        wetGainDb_ = kDefaultWetGainDb;
        governorThresholdDb_ = kDefaultGovernorThresholdDb;
        governorRatio_ = kDefaultGovernorRatio;
    }
};

} // namespace Krate::DSP
