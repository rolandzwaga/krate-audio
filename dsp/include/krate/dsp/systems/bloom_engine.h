// ==============================================================================
// Layer 3: System Component - BloomEngine
// ==============================================================================
// A fixed 16-entry lifecycle table of CHILD PARTIALS. Each child is a latched
// (slot, ratio, amplitude) triple carried on an INTEGER control-step clock
// through a smoothstep fade-in / hold / fade-out, written into reserved indices
// of the ratio/amplitude arrays a Vorago voice is about to hand
// HarmonicCloud::setSpectralTarget.
//
// THERE IS NO AUDIO PATH, NO NEW DSP MATHEMATICS AND NO OSCILLATOR ANYWHERE.
// The component is bookkeeping: the partials it creates are rendered by the
// cloud's existing SIMD bank, which is why FR-072's ceiling is 0.1 % of one
// core rather than a per-voice percentage.
//
// NO AMENDMENT TO ANY SHIPPED HEADER (FR-080): harmonic_cloud.h,
// entropy_processor.h, spectral_state.h, spectral_morph_engine.h,
// seraphis_voice.h, seraphis_engine.h, harmonic_snapshot.h,
// spectral_coring_estimator.h, fft_autocorrelation.h,
// sympathetic_resonance_simd.h and slow_event_scheduler.h are byte-unchanged by
// this phase. This file includes NONE of them.
//
// Feature: vorago-phase7-harmonic-bloom
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept throughout; NO method allocates,
//   prepare() included - see getAllocatedBytes below)
// - Principle III: Modern C++ (C++20, value semantics, no owning pointers)
// - Principle IX: Layer 3 (composes Layer 0/1 components only)
// - Principle X: DSP Constraints (C1 envelopes, reject-never-clamp candidate
//   hygiene, a four-rung boundedness ladder)
// - Principle XI: Performance Budget (< 0.1 % of a 512-sample block at 48 kHz,
//   FR-072 / SC-011)
//
// Reference: specs/vorago-phase7-harmonic-bloom/spec.md
//            specs/vorago-phase7-harmonic-bloom/plan.md
//
// ------------------------------------------------------------------------------
// THE THREE NEAR-NAME `bloom` FAMILIES ALREADY IN THE TREE (plan R13, S0).
// None of them is related to this component; all three are class-scoped and
// survive unchanged. A future reader must not conflate them with BloomEngine:
//   1. AetherReverb::kMaxBloomResonators = 32 (effects/aether_reverb.h:1442) -
//      a bank of REVERB-SIDE shimmer resonators inside the Seraphis space
//      engine, i.e. resonators in a tail, not partials in a cloud.
//   2. SpectralMorphEngine's morph-stagger `bloom`
//      (systems/spectral_morph_engine.h:64, :227) - a scalar that STAGGERS
//      per-partial completion while travelling between two authored
//      SpectralStates. It spawns nothing.
//   3. SeraphisEngine::BloomEvents / kBloomPartialCap = 32
//      (systems/seraphis_engine.h:261, :293) - Seraphis's own event surface,
//      plus SeraphisMacro::Bloom (seraphis_macro_matrix.h:50) and
//      SeraphisVoice::setBloom (seraphis_voice.h:680).
// Nested `Phase` enums likewise already coexist (SlowEventScheduler::Phase
// slow_event_scheduler.h:180, GrowthEnvelope::Phase growth_envelope.h:207);
// tools/lint-odr.js:18-21 qualifies nested types by their enclosing class, so
// BloomEngine::Phase claims no namespace-scope name.
// ------------------------------------------------------------------------------
//
// BUILD STATE (tasks.md T004 - the skeleton pass).
// T004 establishes:
//   * the plan S1.1 include list - Layer 0/1 + stdlib ONLY;
//   * every plan S1.2 constant with EVERY static_assert live, each restated one
//     carrying the header and line it was copied from, so this file failing to
//     compile IS the falsification for the skeleton;
//   * the nested Relation / Phase / PrepareConfig types and the private Child
//     record (plan S1.3);
//   * the complete plan S1.4 public surface with FR-009 semantics ALREADY REAL
//     on every setter (reject non-finite / no-op out-of-range index / clamp
//     out-of-range value) - that is what T005 asserts;
//   * the complete plan S1.5 private state in declaration order;
//   * the plan S1.6 salt table with its overlap static_assert and the two
//     differently-shaped RNGs;
//   * plan S2's prepare() / reset() / setSeed();
//   * the plan S7.5 fault-injection probe forward declaration and friend.
// T007 adds:
//   * plan S3.1's FULL four-step guard ladder;
//   * plan S3.2's ABSOLUTE-RESIDUE control loop - controlPhase_ is carried
//     ACROSS calls, which is the whole of FR-006. A block-relative grid
//     (numSamples / 64 steps per call, phase reset at each call) is the natural
//     mistake and is exactly what BloomEngine_BlockPartitionInvariance fails on;
//   * plan S3.3's controlStep() in its NORMATIVE six-step order;
//   * plan S3.4's applyOutput() NOT-ENGAGED early return - the whole of the
//     write path a disengaged engine can reach, and therefore the whole of
//     SC-014's bit-identical pass-through contract.
//   advanceChildren() (plan S5.2) is a DECLARED NO-OP carrying a // T009 marker.
// T008 adds:
//   * plan S4.1's strongest-K parent scan, S4.2's normative per-child draw
//     sequence, S4.3's accept() (four tests, ALL rejections, no clamps),
//     S4.4's peekSlot/commitSlot split and S4.5's tilt-compensating latch -
//     which is the ONE place engaged_ is set;
//   * plan S3.4's FULL applyOutput() write region: the saturating FR-052 overlap
//     counter, the FR-051 gap pad, the unconditional owned-region pad and the
//     live-child write.
//   Every T008 assertion is reachable WITHOUT the lifecycle clock, because a
//   fixture that spawns at most numChildSlots() children never needs a child to
//   retire.
// T009 adds:
//   * plan S5.1/S5.2's advanceChildren() - the INTEGER control-step clock, the
//     phase derived from ONE step counter against THREE LATCHED bounds (which is
//     what makes FR-033 true by construction) and retire() on the same control
//     step the amplitude reaches 0 (FR-032), paired with S3.4's unconditional
//     owned-region pad that rewrites the vacated slot on that very chunk;
//   * plan S5.3's smoothstep envelope, evaluated in DOUBLE - see the measured
//     justification on smoothstep() itself, which is the one deliberate
//     deviation from the plan's code shape in this task.
//   T010 lands capacity/wake/depth.
// ==============================================================================

#pragma once

// Layer 3 (systems/). Dependencies: Layer 0/1 + stdlib only - deliberately NOT
// Layer 2 and NOT a Layer 3 peer. This component is decoupled from HarmonicCloud
// by the array contract (spec D-1), so systems/harmonic_cloud.h is NOT included;
// the facts it needs from that header (the slot-indexed tilt law, the ratio
// bounds, the silence epsilon, the control-chunk size) are RESTATED as
// class-scoped constants with a source comment (spec D-2, D-9), and SC-012's
// static_asserts in bloom_engine_test.cpp are the live cross-check that the
// copies have not drifted. processors/entropy_processor.h,
// processors/spectral_state.h and systems/slow_event_scheduler.h are likewise
// NOT included (FR-080, D-1/D-2): Phase 10 owns the scheduler, exactly as
// noise_organism.h:841-843 records for the noise organism.
#include <krate/dsp/core/db_utils.h>        // L0: detail::isNaN/isInf/isFinite/flushDenormal
#include <krate/dsp/core/pitch_utils.h>     // L0: centsToPitchRatioFast (FR-020 detune)
#include <krate/dsp/core/random.h>          // L0: Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>  // L1: LinearRamp (FR-042 depth ramp only)

#include <algorithm>  // std::clamp, std::max, std::min
#include <array>
#include <cmath>  // std::log2, std::exp2, std::round
#include <cstddef>
#include <cstdint>

// NO <vector>, NO <memory>, NO <functional>, NO <random>, no I/O, no exceptions.
// tools/lint-layers.js reads the `#include <krate/dsp/{layer}/...>` lines and
// fails a lower layer reaching upward; every include above is Layer 0 or 1, so a
// Layer-3 file is clean STRUCTURALLY rather than by luck.

namespace Krate::DSP {

namespace detail {
/// @brief SC-009 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A TEST TU.
///
/// The FR-062 non-finite trap (BloomEngine::stateFinite) is UNREACHABLE through
/// the public API by construction: every setter rejects a non-finite argument
/// (FR-009 (a)), every candidate with a non-finite ratio or latched target is
/// rejected rather than clamped (FR-021, plan correction C-4), and a non-finite
/// value in an incoming parent slot only DISQUALIFIES that slot (FR-009 (e)).
/// Without a friend the trap could therefore only ever be observed returning
/// `true`, which proves nothing. This probe poisons a Child record directly so
/// SC-009 can prove the trap FIRES.
///
/// It costs nothing at run time and adds no public surface: the library never
/// defines it, so a shipping build has no way to call it. Pattern quoted from
/// systems/feedback_ecology.h:166-172 (declaration) and
/// systems/subharmonic_engine.h:112-130 (the Vorago Phase-6 restatement). Its
/// ONE definition lives in dsp/tests/unit/systems/bloom_engine_nonfinite_test.cpp
/// (tasks.md T012), the only Phase-7 TU in the -fno-fast-math block - declaring
/// the friend HERE is deliberate, so T012 need not edit this header.
///
/// ODR: swept this session - `BloomEngineNonFiniteProbe` has zero matches in
/// dsp/, plugins/ or tools/ outside this phase's own artefacts.
struct BloomEngineNonFiniteProbe;
}  // namespace detail

/// @brief Minutes-scale child-partial generation for a HarmonicCloud spectrum.
///
/// @par Layer: 3 (systems/). Dependencies: Layer 0/1 + stdlib only. NO Layer 2,
///      NO Layer 3 peer, NO Layer 4.
///
/// @par Real-Time Safety: EVERY method is noexcept and allocation-free,
///      prepare() INCLUDED. This component has no heap term at all - every
///      member is a fixed-size std::array or a scalar - so getAllocatedBytes()
///      returns 0 unconditionally (FR-004, FR-071; the
///      resonance_drift_network.h:906-912 precedent, kept so the Phase-10 host
///      can total its children uniformly).
///
/// @par The contract in one paragraph (FR-002, D-1)
///      The caller owns two float arrays - ratios and amplitudes - that it is
///      about to hand HarmonicCloud::setSpectralTarget (harmonic_cloud.h:769).
///      It hands them here FIRST. The engine READS the parent region
///      `[0, min(parentCount, reserveBase()))` for analysis and WRITES only the
///      reserved region it owns, `[reserveBase(), capacity())`, plus the FR-051
///      pad over the gap `[parentCount, reserveBase())`. It returns the count
///      the caller should pass on. There is no HarmonicCloud reference, no
///      include, and no audio.
///
/// @par CRITICAL BUFFER PRECONDITION (plan S14 C-11)
///      `ratios` and `amplitudes` must EACH address at least kMaxSlots (64)
///      writable floats - NOT `parentCount` floats, and NOT `capacity()` floats.
///      See the processChunk doc block: sizing a buffer from
///      PrepareConfig::capacity is not sufficient, because setCapacity() may
///      raise the write ceiling afterwards.
class BloomEngine {
public:
    // =========================================================================
    // Constants (plan S1.2) - every static_assert below is live
    // =========================================================================

    // ---- structure ----------------------------------------------------------

    /// The cloud's hard slot ceiling. NOT included from harmonic_cloud.h
    /// (D-1/D-2): restated, with the static_assert below and SC-012's
    /// cross-check in bloom_engine_test.cpp standing in for the include.
    static constexpr std::size_t kMaxSlots = 64;
    static_assert(kMaxSlots == 64, "== HarmonicCloud::kMaxPartials (harmonic_cloud.h:138)");
    static_assert(kMaxSlots <= 255, "Child::slot is a std::uint8_t (plan S1.3)");

    /// FR-034 lifecycle table size. Fixed at compile time; no free list, no growth.
    static constexpr std::size_t kMaxChildren = 16;
    static constexpr std::size_t kMaxParents = 8;               ///< FR-010 K ceiling
    static constexpr std::size_t kDefaultParentCount = 4;       ///< FR-010
    static constexpr std::size_t kMaxChildrenPerEvent = 4;      ///< FR-015
    static constexpr std::size_t kDefaultChildrenPerEvent = 2;  ///< FR-015
    static constexpr std::size_t kMaxSpawnAttempts = 4;   ///< FR-026 (first draw + 3 retries)
    static constexpr std::size_t kDefaultChildSlots = 8;  ///< PrepareConfig::numChildSlots

    /// The shared library-wide control clock (harmonic_cloud.h:144,
    /// noise_organism.h:150, resonance_drift_network.h:135,
    /// subharmonic_engine.h:168). A component that drifted off it would
    /// decorrelate the per-voice modulation grid at Phase 10.
    static constexpr std::size_t kControlChunkSamples = 64;  ///< FR-007
    static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

    /// prepare()'s sample-rate floor (the resonance_drift_network.h:281 figure).
    /// Nothing here inverts a clamp at a low rate, but the floor also bounds the
    /// step-count conversions of plan S5.1: at 8 kHz a 600 s fade is 75 000
    /// control steps, and the 1-step floor there is what keeps a 1 s fade
    /// non-degenerate.
    static constexpr double kMinUsableSampleRate = 8000.0;

    // ---- ratio domain -------------------------------------------------------

    /// Restated from SpectralState::kMinStateRatio / kMaxStateRatio
    /// (spectral_state.h:51-52) rather than included (D-2). Used ONLY as the
    /// comparison bounds of FR-021's rejection test - NEVER as a clamp.
    static constexpr float kMinChildRatio = 0.5f;
    static constexpr float kMaxChildRatio = 128.0f;

    /// Restated from EntropyProcessor::kMinRatioSpacingCents (entropy_processor.h:80).
    static constexpr float kMinRatioSpacingCents = 24.0f;
    static constexpr float kMinRatioSpacingLog2 = kMinRatioSpacingCents / 1200.0f;  // 0.02

    static constexpr float kMinDetuneCents = kMinRatioSpacingCents;  ///< 24 - FR-020
    static constexpr float kMaxDetuneCents = 50.0f;  ///< centsToPitchRatioFast domain
    static_assert(kMaxDetuneCents <= 50.0f,
                  "wider than centsToPitchRatioFast's documented accurate domain "
                  "(pitch_utils.h:55-62) - use std::exp2 instead of widening this");
    static_assert(kMinDetuneCents < kMaxDetuneCents, "empty detune band");

    // ---- relations ----------------------------------------------------------

    static constexpr float kOctaveFactor = 2.0f;
    static constexpr float kFifthFactor = 1.5f;

    // ---- amplitude ----------------------------------------------------------

    /// FR-011. Matches HarmonicCloud::kTargetAmpEpsilon (harmonic_cloud.h:258):
    /// a partial the cloud cannot tell from silence has no harmonics to grow.
    static constexpr float kSilentParentAmplitude = 1.0e-5f;
    static constexpr float kDefaultChildGain = 0.35f;  ///< FR-023, configurable [0, 1]

    /// FR-023 / Clarification Q1. Restated verbatim from harmonic_cloud.h:1433.
    static constexpr float kLog2TenOver20 = 3.32192809488736235f / 20.0f;
    /// Restated from HarmonicCloud::kMinTiltDbPerOct / kMaxTiltDbPerOct (:194-195).
    static constexpr float kMinConsumerTiltDbPerOct = -12.0f;
    static constexpr float kMaxConsumerTiltDbPerOct = 12.0f;

    // ---- lifecycle (FR-030, roadmap line 347) -------------------------------

    static constexpr float kDefaultFadeInSeconds = 45.0f;
    static constexpr float kDefaultHoldSeconds = 120.0f;
    static constexpr float kDefaultFadeOutSeconds = 180.0f;
    static constexpr float kMinFadeInSeconds = 1.0f, kMaxFadeInSeconds = 300.0f;
    static constexpr float kMinHoldSeconds = 0.0f, kMaxHoldSeconds = 900.0f;
    static constexpr float kMinFadeOutSeconds = 1.0f, kMaxFadeOutSeconds = 600.0f;
    static constexpr float kDefaultHoldJitterFraction = 0.5f;  ///< FR-036, Clarification Q5

    // ---- triggering ---------------------------------------------------------

    static constexpr float kMaxSpawnRateHz = 0.05f;              ///< FR-040: one per 20 s
    static constexpr float kDefaultSpawnRateHz = 1.0f / 240.0f;  ///< one per 4 minutes
    static constexpr float kDefaultDepth = 1.0f;                 ///< FR-042

    /// The house 50 ms control ramp (noise_organism.h:178,
    /// resonance_drift_network.h:143, subharmonic_engine.h:177). Used for
    /// `depth` ONLY (FR-042) - there is no gain in this component.
    static constexpr float kGainRampMs = 50.0f;

    /// resonance_drift_network.h:266. A wake at or below this snaps to EXACTLY
    /// 0, which is what makes setWake(0) and setDormant(true) identical BY
    /// CONSTRUCTION rather than by luck (FR-035, SC-014 (d)).
    static constexpr float kWakeSilenceEpsilon = 1.0e-6f;

    static constexpr std::uint32_t kDefaultSeed = 0xB10035EDu;  // "bloomseed"

    // ---- diagnostics --------------------------------------------------------

    /// FR-052's engagement counter SATURATES here rather than wrapping - the
    /// ResonanceDriftNetwork::kMaxClampCount idiom (resonance_drift_network.h:985).
    /// A bare ++ on a std::uint32_t WRAPS; SC-003 asserts this counter reads
    /// EXACTLY 0 while parentCount <= reserveBase(), and a wrap would let a
    /// long-running engaged render - the one case where the number actually
    /// matters - report a FALSE ZERO.
    static constexpr std::uint32_t kMaxOverlapCount = 0xFFFFFFFFu;

    // =========================================================================
    // Nested types (plan S1.3)
    // =========================================================================

    /// @brief FR-020 child-partial relationship to its parent.
    ///
    /// APPEND ONLY - this becomes a persisted plugin parameter at Phase 12, so a
    /// renumbering silently reinterprets every saved preset. Nested, following
    /// SubharmonicEngine::Tone (subharmonic_engine.h:320) and
    /// ResonanceDriftNetwork::AnchorMode (:294).
    enum class Relation : std::uint8_t { Octave = 0, Fifth = 1, DetunedNeighbour = 2 };
    static constexpr std::size_t kNumRelations = 3;

    /// @brief FR-030 child lifecycle phase.
    ///
    /// The exact-shape precedent is SlowEventScheduler::Phase
    /// (slow_event_scheduler.h:180) - `enum class Phase : std::uint8_t
    /// { Idle = 0, Attack = 1, Hold = 2, Release = 3 }`, public and nested.
    /// GrowthEnvelope::Phase (growth_envelope.h:207) is a DIFFERENT shape -
    /// `{ Idle, Rising, Complete }`, private, no explicit underlying type - and
    /// is cited only as evidence that nested `Phase` enums coexist, never as a
    /// shape to copy.
    enum class Phase : std::uint8_t { Idle = 0, FadeIn = 1, Hold = 2, FadeOut = 3 };

    /// @brief FR-050 prepare-time configuration.
    ///
    /// Callers MUST use designated initialisers - `PrepareConfig{.capacity = 32}`
    /// - so no narrowing conversion hides in a positional brace init (Clang
    /// errors where MSVC does not; plan R7). Nested, following
    /// NoiseOrganism::PrepareConfig (noise_organism.h:190),
    /// ResonanceDriftNetwork::PrepareConfig (:299) and
    /// SubharmonicEngine::PrepareConfig (:327).
    struct PrepareConfig {
        /// THE CALLER'S PROMISE ABOUT HOW MANY SLOTS THE CLOUD WILL ACTUALLY
        /// SOUND, i.e. HarmonicCloud::getActivePartialCount()
        /// (harmonic_cloud.h:950) - NOT kMaxPartials. recalculateAmplitudes()
        /// zeroes and `continue`s every slot at or above activeCount_ BEFORE the
        /// spectral-target branch (harmonic_cloud.h:1469-1473), so a child
        /// written past it is silently inaudible. Clamped [1, kMaxSlots]. Only
        /// the INITIAL value: setCapacity() owns it afterwards (FR-055) and this
        /// field is never re-read.
        ///
        /// THIS FIELD DOES NOT BOUND THE WRITE REGION and must not be used to
        /// size the arrays. processChunk() requires at least kMaxSlots (64)
        /// writable floats whatever this value is, because setCapacity() can
        /// raise the ceiling afterwards - see the processChunk precondition
        /// below and plan S14 C-11.
        std::size_t capacity = kMaxSlots;

        /// Clamped [0, min(kMaxChildren, capacity)]. 0 makes the component an
        /// exact pass-through (FR-054).
        std::size_t numChildSlots = kDefaultChildSlots;
    };

    // =========================================================================
    // Construction (FR-004) - every default is a member initialiser
    // =========================================================================

    BloomEngine() noexcept = default;
    /// Copyable and movable: the state holds no pointer into itself.
    BloomEngine(const BloomEngine&) = default;
    BloomEngine& operator=(const BloomEngine&) = default;
    BloomEngine(BloomEngine&&) noexcept = default;
    BloomEngine& operator=(BloomEngine&&) noexcept = default;

    // =========================================================================
    // Lifecycle (plan S2)
    // =========================================================================

    /// @brief Configure for a sample rate and a slot budget. ALLOCATION-FREE.
    ///
    /// Re-preparing a live object is legal and fully re-initialises; `seed_` and
    /// every configuration scalar survive (FR-004). A non-finite sample rate is
    /// substituted by 48 000 and then floored at kMinUsableSampleRate.
    ///
    /// @param sampleRate Host sample rate in Hz.
    /// @param config     Designated-initialiser-only slot budget.
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // (1)
        sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));
        // (2)
        controlDtSec_ = static_cast<float>(kControlChunkSamples) / static_cast<float>(sampleRate_);
        controlRateHz_ = static_cast<float>(sampleRate_) / static_cast<float>(kControlChunkSamples);
        invSampleRate_ = 1.0f / static_cast<float>(sampleRate_);
        // (3)
        capacity_ = std::clamp(config.capacity, std::size_t{1}, kMaxSlots);
        requestedChildSlots_ =
            std::clamp(config.numChildSlots, std::size_t{0}, std::min(kMaxChildren, capacity_));
        // NOTE: requestedChildSlots_ is clamped against the INITIAL capacity here
        // and never again; numChildSlots() re-derives min(requested, capacity_)
        // so an FR-055 shrink/grow pair is reversible (plan S6.3, C-9).
        // (4) ONE STEP PER CONTROL CHUNK, so the ramp completes in 50 ms of real
        //     time regardless of block size (the subharmonic_engine.h:441 idiom).
        depthRamp_.configure(kGainRampMs, controlRateHz_);
        depthRamp_.snapTo(depth_);  // no 50 ms window at t = 0 (SC-014 (a))
        // (5)
        prepared_ = true;
        // (6) LAST configuration step, house order.
        setSeed(seed_);
        // (7)
        reset();
    }

    /// @brief Rewind every clock, table, counter and cursor. CONFIGURATION-PRESERVING.
    ///
    /// Resetting the counters is deliberate: SC-015's expectations are stated
    /// over a run that begins at a prepare().
    void reset() noexcept {
        children_.fill(Child{});  // every phase back to Idle
        slotMask_ = 0u;
        liveCount_ = 0;
        cursor_ = reserveBase();  // FR-056
        engaged_ = false;
        armed_ = false;
        controlStep_ = 0u;
        controlPhase_ = 0;
        clockRng_.seed(deriveStreamSeed(seed_, kSaltClock));
        // eventRng_ is re-seeded at the head of EVERY event from the control-step
        // index (plan S1.6), so its value between events is never read. It is
        // rewound here only so a reset leaves no stale stream state behind.
        eventRng_.seed(deriveStreamSeed(seed_, kSaltEvent));
        depthRamp_.snapTo(depth_);

        spawnEvents_ = 0u;
        discardedEvents_ = 0u;
        parentScans_ = 0u;
        offeredChildren_ = 0u;
        spawnedChildren_ = 0u;
        refusedChildren_ = 0u;
        fallbackChildren_ = 0u;
        completedChildren_ = 0u;
        rejectedSpawns_ = 0u;
        overlapEngagements_ = 0u;

        // Per-event scratch. Only parentSelected_ and occupiedCount_ are
        // load-bearing (they are the lengths that gate every read); the arrays
        // are cleared alongside them so a reset leaves no stale selection
        // readable through getLastParentIndex().
        parentIdx_.fill(std::size_t{0});
        parentAmp_.fill(0.0f);
        parentRatio_.fill(0.0f);
        parentSelected_ = 0;
        parentUsedMask_ = 0u;
        occupiedLog2_.fill(0.0f);
        occupiedCount_ = 0;
    }

    /// @brief Re-seed. Legal while live; changes only FUTURE draws.
    ///
    /// Does NOT touch controlStep_, the child table or the counters. eventRng_
    /// needs no action here: it is re-seeded at every event from seed_ and the
    /// step index (plan S1.6).
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        clockRng_.seed(deriveStreamSeed(seed_, kSaltClock));
    }

    // =========================================================================
    // The one per-chunk entry point (FR-005)
    // =========================================================================

    /// @brief Read the parent spectrum, advance the bloom, write the owned slots.
    ///
    /// PRECONDITION - NORMATIVE, and deliberately NOT the EntropyProcessor
    /// contract: `ratios` and `amplitudes` MUST EACH address at least kMaxSlots
    /// (64) writable floats, regardless of `parentCount` and regardless of the
    /// current capacity().
    ///
    /// Unlike EntropyProcessor::processChunk (entropy_processor.h:269), whose
    /// `count` is documented as "Number of valid entries, clamped to kPartials"
    /// (:266) and which touches only [0, min(count, kPartials)) (:296), THIS
    /// component WRITES ABOVE `parentCount` - the FR-051 gap pad and the whole
    /// owned region - and setCapacity() (FR-055) may RAISE that write ceiling
    /// after prepare(), from the control thread, through a call the caller
    /// cannot correlate with its buffer length. Sizing a buffer from
    /// PrepareConfig::capacity is therefore NOT sufficient: size it from
    /// kMaxSlots. `parentCount` is an ANALYSIS length, never a buffer length.
    /// (Plan S14 C-11.)
    ///
    /// @param ratios      Partial ratio array; >= kMaxSlots writable floats.
    /// @param amplitudes  Partial amplitude array; >= kMaxSlots writable floats.
    /// @param parentCount Number of live parent partials - an ANALYSIS length.
    /// @param numSamples  Samples this chunk advances; 0 applies the current
    ///                    state WITHOUT advancing.
    /// @return The count the caller should hand HarmonicCloud::setSpectralTarget.
    ///         A call that wrote nothing returns the caller's UNCLAMPED
    ///         parentCount (plan S3.1): returning the clamped value would
    ///         truncate the caller's spectrum (SC-014 (f)).
    [[nodiscard]] std::size_t processChunk(float* ratios, float* amplitudes,
                                           std::size_t parentCount,
                                           std::size_t numSamples) noexcept {
        // (0) PRECONDITION, not checkable here and therefore stated normatively
        //     in the doxygen above and in plan S14 C-11: both arrays address
        //     >= kMaxSlots floats. NOTHING in this ladder bounds the write region
        //     by the caller's buffer, because the signature carries no length for
        //     it - parentCount is an ANALYSIS length. The write region is
        //     [pc, capacity_), and capacity_ can be raised by setCapacity()
        //     after prepare().
        // (1) FR-005 / entropy_processor.h:271-277: a rejected call advances NOTHING.
        if (ratios == nullptr || amplitudes == nullptr) {
            return parentCount;
        }
        // (2) An unprepared object behaves as a pass-through, never as a crash.
        if (!prepared_) {
            return parentCount;
        }
        // (3) FR-005: parentCount is clamped to capacity() for every LATER use.
        //     `pc` is an internal analysis-and-write bound only; it is NEVER the
        //     return value of a call that wrote nothing (plan S3.1, SC-014 (f)).
        const std::size_t pc = std::min(parentCount, capacity_);
        // (4) FR-005: numSamples == 0 applies the current state WITHOUT advancing.
        if (numSamples > 0) {
            advance(ratios, amplitudes, pc, numSamples);
        }
        return applyOutput(ratios, amplitudes, parentCount, pc);
    }

    // =========================================================================
    // Control surface (FR-060) - FR-009 applied uniformly (plan S7.6)
    // =========================================================================
    // A non-finite float argument is REJECTED and the previous value stands.
    // An out-of-range INDEX (a Relation) is a SILENT NO-OP.
    // An out-of-range float or size is CLAMPED and the getter reports the clamp.
    // =========================================================================

    /// FR-042. Clamped [0, 1]; retargets the 50 ms control-rate ramp.
    void setDepth(float depth) noexcept {
        if (!detail::isFinite(depth)) {
            return;  // FR-009 (a): reject, the previous value stands
        }
        depth_ = std::clamp(depth, 0.0f, 1.0f);
        depthRamp_.setTarget(depth_);
    }

    /// FR-040. Clamped [0, kMaxSpawnRateHz]. Exactly 0 disables the internal
    /// clock entirely (p == 0), leaving triggerBloom() as the only source; the
    /// two sources are additive and independent.
    void setSpawnRateHz(float hz) noexcept {
        if (!detail::isFinite(hz)) {
            return;
        }
        spawnRateHz_ = std::clamp(hz, 0.0f, kMaxSpawnRateHz);
    }

    /// FR-010. Clamped [1, kMaxParents].
    void setParentCount(std::size_t k) noexcept {
        parentCountK_ = std::clamp(k, std::size_t{1}, kMaxParents);
    }

    /// FR-015. Clamped [1, kMaxChildrenPerEvent].
    void setChildrenPerEvent(std::size_t n) noexcept {
        childrenPerEvent_ = std::clamp(n, std::size_t{1}, kMaxChildrenPerEvent);
    }

    /// FR-023. Clamped [0, 1].
    void setChildGain(float gain) noexcept {
        if (!detail::isFinite(gain)) {
            return;
        }
        childGain_ = std::clamp(gain, 0.0f, 1.0f);
    }

    /// FR-030. Clamped [kMinFadeInSeconds, kMaxFadeInSeconds]. Affects the NEXT
    /// child only: an in-flight child's step bounds are latched (FR-033).
    void setFadeInSeconds(float seconds) noexcept {
        if (!detail::isFinite(seconds)) {
            return;
        }
        fadeInSec_ = std::clamp(seconds, kMinFadeInSeconds, kMaxFadeInSeconds);
    }

    /// FR-030. Clamped [kMinHoldSeconds, kMaxHoldSeconds]. Next child only.
    void setHoldSeconds(float seconds) noexcept {
        if (!detail::isFinite(seconds)) {
            return;
        }
        holdSec_ = std::clamp(seconds, kMinHoldSeconds, kMaxHoldSeconds);
    }

    /// FR-030. Clamped [kMinFadeOutSeconds, kMaxFadeOutSeconds]. Next child only.
    void setFadeOutSeconds(float seconds) noexcept {
        if (!detail::isFinite(seconds)) {
            return;
        }
        fadeOutSec_ = std::clamp(seconds, kMinFadeOutSeconds, kMaxFadeOutSeconds);
    }

    /// FR-036. Clamped [0, 1]. Exactly 0 makes every latched hold equal
    /// getHoldSeconds() exactly.
    void setHoldJitterFraction(float fraction) noexcept {
        if (!detail::isFinite(fraction)) {
            return;
        }
        holdJitter_ = std::clamp(fraction, 0.0f, 1.0f);
    }

    /// FR-016. Clamped [0, 1]. An out-of-range Relation is a SILENT NO-OP
    /// (FR-009 (b)) leaving all three weights unchanged. All-zero weights fall
    /// back to a uniform draw (FR-016), which is why 0 is a legal weight.
    void setRelationWeight(Relation r, float weight) noexcept {
        const auto idx = static_cast<std::size_t>(r);
        if (idx >= kNumRelations || !detail::isFinite(weight)) {
            return;
        }
        relationWeight_[idx] = std::clamp(weight, 0.0f, 1.0f);
    }

    /// FR-023 / Clarification Q1. The CONSUMER cloud's spectral tilt, restated
    /// here so the latch can COMPENSATE for it - the tilt is slot-indexed, so a
    /// child written into a high reserved slot is tilted as though it were a
    /// high harmonic number regardless of its actual pitch. Clamped [-12, +12]
    /// dB/oct, the HarmonicCloud::setSpectralTiltDb range (harmonic_cloud.h:194-195).
    void setConsumerTiltDb(float dbPerOct) noexcept {
        if (!detail::isFinite(dbPerOct)) {
            return;
        }
        consumerTiltDb_ = std::clamp(dbPerOct, kMinConsumerTiltDbPerOct, kMaxConsumerTiltDbPerOct);
    }

    /// FR-055. Clamped [1, kMaxSlots]. Growth takes effect immediately;
    /// shrinkage is DEFERRED - a live child above the new capacity is not
    /// killed, retimed or evicted (plan S6.3).
    void setCapacity(std::size_t capacity) noexcept {
        capacity_ = std::clamp(capacity, std::size_t{1}, kMaxSlots);
    }

    /// FR-035. Dormant means NO NEW SPAWNS. The FR-041 clock draw keeps running,
    /// the depth ramp keeps advancing, and children already in flight run their
    /// lifecycle to completion - this component has no audio chain to skip
    /// (FR-002), and cutting a 45-second swell at a dormancy edge is exactly the
    /// click FR-031 exists to prevent (plan S6.1, open item OQ-B).
    void setDormant(bool dormant) noexcept { dormant_ = dormant; }

    /// FR-035. Clamped [0, 1]; a value at or below kWakeSilenceEpsilon snaps to
    /// EXACTLY 0.0f, which is what makes setWake(0) and setDormant(true)
    /// identical by construction rather than by luck. Wake feeds the single
    /// spawn gate and NOTHING else: it does not scale child gain, child ratio,
    /// an in-flight envelope, or triggerBloom().
    ///
    /// Plan S6.1 sketches this setter with `sanitise(amount, 1.0f)`; FR-009 (a)
    /// and tasks.md T004 are normative over that sketch, so a non-finite
    /// argument is REJECTED here exactly as in every other setter.
    void setWake(float amount) noexcept {
        if (!detail::isFinite(amount)) {
            return;
        }
        const float w = std::clamp(amount, 0.0f, 1.0f);
        wake_ = (w <= kWakeSilenceEpsilon) ? 0.0f : w;
    }

    /// FR-040 (a) / FR-043. EDGE-LIKE: calling it n times between two control
    /// steps arms ONE event, because armed_ is a bool consumed and cleared by
    /// the control step. That is what makes it safe for a caller polling
    /// SlowEventScheduler::isEventActive() (slow_event_scheduler.h:361) rather
    /// than its onset. The natural bug is armed_ as a COUNTER - exactly the
    /// shape a level-polling caller invites - which would produce a burst of
    /// n x childrenPerEvent simultaneous children (SC-018 (e)).
    void triggerBloom() noexcept { armed_ = true; }

    // =========================================================================
    // Read surface (FR-061)
    // =========================================================================
    // An out-of-range index returns the documented neutral and never reads out
    // of bounds (the noise_organism.h:856-861 form): 0 for size getters, 0.0f
    // for float getters, Relation::Octave and Phase::Idle for the two enums,
    // false for getIsChildFallback, and kMaxSlots - an impossible slot index -
    // for getLastParentIndex and for getChildSlotIndex on a Phase::Idle table
    // entry. These neutrals are a PUBLIC CONTRACT, not #ifdef scaffolding
    // (the entropy_processor.h:299-302 form).
    // =========================================================================

    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }
    /// The CONFIGURED depth (FR-042); getSmoothedDepth() reports the ramp.
    [[nodiscard]] float getDepth() const noexcept { return depth_; }
    /// Plan S8 addition A-6: SC-014 (a)'s precondition ("the smoothed depth is
    /// never non-zero") must be CHECKED, not assumed, or the criterion is a coin
    /// flip on ramp timing.
    [[nodiscard]] float getSmoothedDepth() const noexcept { return depthRamp_.getCurrentValue(); }
    [[nodiscard]] float getSpawnRateHz() const noexcept { return spawnRateHz_; }
    [[nodiscard]] std::size_t getParentCount() const noexcept { return parentCountK_; }
    [[nodiscard]] std::size_t getChildrenPerEvent() const noexcept { return childrenPerEvent_; }
    [[nodiscard]] float getChildGain() const noexcept { return childGain_; }
    [[nodiscard]] float getFadeInSeconds() const noexcept { return fadeInSec_; }
    [[nodiscard]] float getHoldSeconds() const noexcept { return holdSec_; }
    [[nodiscard]] float getFadeOutSeconds() const noexcept { return fadeOutSec_; }
    [[nodiscard]] float getHoldJitterFraction() const noexcept { return holdJitter_; }

    [[nodiscard]] float getRelationWeight(Relation r) const noexcept {
        const auto idx = static_cast<std::size_t>(r);
        return (idx < kNumRelations) ? relationWeight_[idx] : 0.0f;
    }

    [[nodiscard]] float getConsumerTiltDb() const noexcept { return consumerTiltDb_; }
    [[nodiscard]] bool isDormant() const noexcept { return dormant_; }
    [[nodiscard]] float getWakeAmount() const noexcept { return wake_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// FR-055 / plan C-9: RE-DERIVED on every read, never stored, so an FR-055
    /// shrink/grow pair is reversible.
    [[nodiscard]] std::size_t numChildSlots() const noexcept {
        return std::min(requestedChildSlots_, capacity_);
    }

    /// The first index of the reserved region the engine owns.
    [[nodiscard]] std::size_t reserveBase() const noexcept { return capacity_ - numChildSlots(); }

    [[nodiscard]] std::size_t getLiveChildCount() const noexcept { return liveCount_; }

    /// Plan S8 addition A-5. FR-051's STICKY latch (Clarification Q8): set once,
    /// when the first child is latched, and cleared only by prepare()/reset().
    /// It is NOT level-tracking.
    [[nodiscard]] bool isEngaged() const noexcept { return engaged_; }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

    // ---- cumulative counters, all zeroed by prepare()/reset() ---------------

    /// Events that were EXECUTED (a parent scan ran) - FR-013's left-hand side.
    [[nodiscard]] std::uint64_t getSpawnEventCount() const noexcept { return spawnEvents_; }
    /// Plan S8 addition A-1 (Clarification Q7): an event armed on a step where
    /// the gate is 0 is consumed and DISCARDED on that same step, never held -
    /// there is no catch-up burst at a wake edge. A discarded event increments
    /// NO other counter.
    [[nodiscard]] std::uint64_t getDiscardedEventCount() const noexcept { return discardedEvents_; }
    /// FR-013: must equal getSpawnEventCount() EXACTLY - a per-chunk scan would
    /// make this far larger, and no CPU budget can police that.
    [[nodiscard]] std::uint64_t getParentScanCount() const noexcept { return parentScans_; }
    /// Plan S8 addition A-2, half of the C-7 identity `offered == spawned + refused`.
    [[nodiscard]] std::uint64_t getOfferedChildCount() const noexcept { return offeredChildren_; }
    [[nodiscard]] std::uint64_t getSpawnedChildCount() const noexcept { return spawnedChildren_; }
    /// Plan S8 addition A-3: children refused for want of a free slot, counted
    /// EXACTLY ONCE per offered child (FR-025).
    [[nodiscard]] std::uint64_t getRefusedChildCount() const noexcept { return refusedChildren_; }
    /// FR-026 detuned-fallback children.
    [[nodiscard]] std::uint64_t getFallbackChildCount() const noexcept { return fallbackChildren_; }
    [[nodiscard]] std::uint64_t getCompletedChildCount() const noexcept {
        return completedChildren_;
    }
    /// FR-026: counted per ATTEMPT, not per child - which is why it is NOT part
    /// of the C-7 accounting identity. FR-014 does NOT contribute (plan C-10).
    [[nodiscard]] std::uint64_t getRejectedSpawnCount() const noexcept { return rejectedSpawns_; }
    /// FR-052. Saturates at kMaxOverlapCount rather than wrapping.
    [[nodiscard]] std::uint32_t getOverlapEngagementCount() const noexcept {
        return overlapEngagements_;
    }

    // ---- last-event parent selection (plan S8 addition A-4) ----------------
    // SC-005's only direct observable: with K up to 8 and childrenPerEvent up to
    // 4, most selected parents never produce a child, so the selection would
    // otherwise be invisible through the child table.

    [[nodiscard]] std::size_t getLastParentSelectionCount() const noexcept {
        return parentSelected_;
    }
    [[nodiscard]] std::size_t getLastParentIndex(std::size_t k) const noexcept {
        return (k < parentSelected_) ? parentIdx_[k] : kMaxSlots;
    }

    // ---- per-child introspection by TABLE index i in [0, kMaxChildren) -----

    /// kMaxSlots (an impossible index) for an out-of-range i OR a Phase::Idle entry.
    [[nodiscard]] std::size_t getChildSlotIndex(std::size_t i) const noexcept {
        if (i >= kMaxChildren || children_[i].phase == Phase::Idle) {
            return kMaxSlots;
        }
        return static_cast<std::size_t>(children_[i].slot);
    }
    [[nodiscard]] std::size_t getChildParentIndex(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? static_cast<std::size_t>(children_[i].parentIndex)
                                  : std::size_t{0};
    }
    [[nodiscard]] float getChildRatio(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? children_[i].ratio : 0.0f;
    }
    [[nodiscard]] float getChildAmplitude(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? children_[i].amplitude : 0.0f;
    }
    /// Plan S8 addition A-7: SC-015 (b) asserts every emitted amplitude lies in
    /// [0, latched target] and that the target equals FR-023's formula.
    [[nodiscard]] float getChildTargetAmplitude(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? children_[i].target : 0.0f;
    }
    [[nodiscard]] Relation getChildRelation(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? children_[i].relation : Relation::Octave;
    }
    [[nodiscard]] Phase getChildPhase(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? children_[i].phase : Phase::Idle;
    }
    /// Derived from the INTEGER step counter, never accumulated (plan S5.1).
    [[nodiscard]] float getChildElapsedSeconds(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? static_cast<float>(children_[i].step) * controlDtSec_ : 0.0f;
    }
    [[nodiscard]] bool getIsChildFallback(std::size_t i) const noexcept {
        return (i < kMaxChildren) && children_[i].fallback;
    }
    /// Plan S8 addition A-8: SC-002 (e) asserts two children of one event get
    /// DIFFERENT latched holds, each inside holdSeconds * (1 +/- 0.5).
    [[nodiscard]] float getChildHoldSeconds(std::size_t i) const noexcept {
        return (i < kMaxChildren) ? static_cast<float>(children_[i].holdSteps) * controlDtSec_
                                  : 0.0f;
    }

    /// @brief Always 0 (FR-004, FR-071). This component has NO heap term: every
    /// member is a fixed-size std::array or a scalar, so prepare() is
    /// allocation-free in fact. The getter exists so the Phase-10 host can total
    /// its children uniformly (resonance_drift_network.h:906-912).
    /// Deliberately NON-static: the sibling zero-heap component
    /// (resonance_drift_network.h:912) has the identical shape, and the
    /// allocating siblings (subharmonic_engine.h:865, feedback_ecology.h:1330)
    /// return a member and so cannot be static. The uniform shape is the point.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return 0u; }

    /// @brief FR-062 rung 4. A READ, not a repair: nothing in rungs 1-3 can
    /// produce a non-finite child, so a `false` here is a defect report, never a
    /// recovery path. Uses detail::isNaN / detail::isInf (db_utils.h:99, :260) -
    /// NEVER std::isnan/isinf/isfinite, which tools/lint-nonfinite-symbols.js
    /// forbids because they fold to constants under -ffast-math (FR-008).
    [[nodiscard]] bool stateFinite() const noexcept {
        // dsp/include uses std::ranges NOWHERE (0 occurrences, this tree); a
        // ranges predicate here would be its first and would bury the
        // four-clause FR-062 read inside a lambda.
        // NOLINTNEXTLINE(readability-use-anyofallof)
        for (const Child& c : children_) {
            if (detail::isNaN(c.ratio) || detail::isInf(c.ratio) || detail::isNaN(c.target) ||
                detail::isInf(c.target) || detail::isNaN(c.amplitude) ||
                detail::isInf(c.amplitude)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // FR-070 salt table (plan S1.6)
    // =========================================================================
    // APPEND ONLY. Renumbering a base silently changes every Phase-7 render.
    static constexpr std::size_t kSaltClock = 0;  ///< the FR-041 Bernoulli stream
    static constexpr std::size_t kSaltEvent = 1;  ///< the per-event stream BASE
    static constexpr std::size_t kSaltNextFree = 2;
    static_assert(kSaltClock < kSaltEvent && kSaltEvent < kSaltNextFree, "salt table overlap");

    // =========================================================================
    // The lifecycle record (FR-034, plan S1.3)
    // =========================================================================
    // PRIVATE, following ResonanceDriftNetwork::Peak (resonance_drift_network.h:995,
    // inside the private: section that opens at :928) - the one verified in-tree
    // precedent for the ACCESS LEVEL. NoiseOrganism::DustGrain
    // (noise_organism.h:203-208) is the precedent for a nested fixed-size
    // per-slot record but is PUBLIC, so it is deliberately not cited here.
    // 32 bytes with natural padding; 16 of them is 512 bytes (plan S9).
    struct Child {
        std::uint32_t step = 0;          ///< Control steps since spawn. EXACT - plan S5.1.
        std::uint32_t fadeInSteps = 1;   ///< Latched at spawn (FR-033).
        std::uint32_t holdSteps = 0;     ///< Latched, INCLUDING the FR-036 jitter.
        std::uint32_t fadeOutSteps = 1;  ///< Latched at spawn.
        float ratio = 1.0f;              ///< Latched at spawn (FR-024).
        float target = 0.0f;             ///< Latched amplitude (FR-023), tilt-compensated.
        float amplitude = 0.0f;          ///< Current envelope output; exactly 0 at both ends.
        std::uint8_t slot = 0;           ///< Owned slot index; kMaxSlots <= 255 (S1.2).
        std::uint8_t parentIndex = 0;    ///< Informational, for SC-005 (plan S8 addition A-4).
        Relation relation = Relation::Octave;
        Phase phase = Phase::Idle;
        bool fallback = false;  ///< FR-026 detuned-fallback child.
    };

    // =========================================================================
    // Helpers (plan S2.1)
    // =========================================================================

    [[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    // =========================================================================
    // The control-step grid (plan S3.2) - AN ABSOLUTE RESIDUE, NOT A
    // BLOCK-RELATIVE GRID
    // =========================================================================

    /// @brief Run every control step the `numSamples` just handed over spans.
    ///
    /// controlPhase_ lives ACROSS CALLS, so a 36 + 28 split runs exactly the one
    /// control step an unsplit 64 runs and the number of steps executed after N
    /// total advanced samples is `floor((N + phase0) / 64)` - a function of N
    /// ALONE (FR-006). The idiom is subharmonic_engine.h:592-607 with the
    /// rendering removed, because this component renders nothing.
    ///
    /// THE FAILURE THIS SHAPE EXISTS TO PREVENT: `numSamples / kControlChunkSamples`
    /// steps per call, phase discarded at the call boundary. That form drops
    /// every partial chunk on the floor - a caller feeding 1, 7, 383, 1209 would
    /// advance the engine ZERO steps in 1 600 samples - and it is what
    /// BloomEngine_BlockPartitionInvariance measures.
    ///
    /// THE ARRAYS TRAVEL WITH THE CLOCK ON PURPOSE: the FR-010 parent scan reads
    /// live partial content, and the only live content the engine ever sees is
    /// the caller's array on the call that crosses a step boundary. This is the
    /// one place the analysis and the clock meet.
    ///
    /// `numSamples` may be any value, including one far larger than 64; the loop
    /// runs as many steps as the sample count spans.
    void advance(float* ratios, float* amplitudes, std::size_t pc,
                 std::size_t numSamples) noexcept {
        std::size_t remaining = numSamples;
        while (remaining > 0) {
            const std::size_t take = std::min(remaining, kControlChunkSamples - controlPhase_);
            controlPhase_ += take;
            remaining -= take;
            if (controlPhase_ == kControlChunkSamples) {
                controlPhase_ = 0;
                controlStep(ratios, amplitudes, pc);
            }
        }
    }

    /// @brief One control step. THE ORDER OF THE SIX NUMBERED STEPS IS NORMATIVE
    ///        (plan S3.3) - each of them is load-bearing for a different criterion.
    void controlStep(float* ratios, float* amplitudes, std::size_t pc) noexcept {
        // (1) The clock draw is UNCONDITIONAL (FR-041, Clarification Q7), taken
        //     BEFORE the probability is even computed. clockRng_'s position after
        //     n control steps is therefore n draws - a pure function of elapsed
        //     control steps, never of dormancy / wake / depth / rate history.
        //     Drawing it inside the `u < p` test instead would make a dormant
        //     prefix consume a different number of draws than an awake one, and
        //     every later child would differ.
        const float u = clockRng_.nextUnipolar();

        // (2) ONE ramp step per control chunk, so the 50 ms depth ramp completes
        //     in 50 ms of real time regardless of block size. LinearRamp::process()
        //     is [[nodiscard]] (smoother.h:370): the return is BOUND, never
        //     discarded (C4834 / -Wunused-result under the zero-warning rule),
        //     and binding it also removes the read-after-write ordering coupling
        //     a second getCurrentValue() read would create.
        const float smoothedDepth = depthRamp_.process();

        // (3) THE SINGLE OWNER OF THE EFFECTIVE PROBABILITY. `gate == 0.0f` is an
        //     EXACT test and is reachable exactly because setWake() snaps
        //     <= kWakeSilenceEpsilon to 0.0f at the source and dormant_ forces the
        //     product to zero - which is what makes setWake(0) and
        //     setDormant(true) identical BY CONSTRUCTION (FR-035, SC-014 (d)).
        //     depth reaches the same test through the ramp: LinearRamp::process()
        //     lands exactly on its target (smoother.h:379-383).
        //     p <= kMaxSpawnRateHz * 64 / kMinUsableSampleRate = 4.0e-4, so there
        //     is no p >= 1 degeneracy anywhere in the configured domain.
        const float gate = dormant_ ? 0.0f : wake_ * smoothedDepth;
        const float p =
            spawnRateHz_ * gate * static_cast<float>(kControlChunkSamples) * invSampleRate_;

        // (4) BEFORE any spawn, so a child latched on this very step emits
        //     amplitude 0 on its first chunk (FR-031's C1 start).
        advanceChildren();

        // (5) Arm consumption. armed_ is a bool, consumed and cleared HERE, which
        //     is what makes triggerBloom() edge-like (FR-043): n calls between two
        //     control steps arm ONE event, not n.
        const bool clockArm = (u < p);
        const bool arm = clockArm || armed_;
        armed_ = false;
        if (arm) {
            if (gate == 0.0f) {
                // FR-035 / Clarification Q7: DISCARDED on this same step, never
                // held. There is no catch-up burst at a wake edge, and a
                // discarded event increments NO other counter.
                ++discardedEvents_;
            } else {
                runEvent(ratios, amplitudes, pc, smoothedDepth);
            }
        }

        // (6)
        ++controlStep_;
    }

    /// @brief FR-031's C1 envelope shape, `3u^2 - 2u^3`. Plan S5.3.
    ///
    /// The ALGEBRAIC form is the plan's, unchanged: `f(0) = 0` and `f(1) = 1` are
    /// both exact, `f'(u) = 6u(1-u)` so `f'(0) = f'(1) = 0`, and the FadeOut
    /// branch below is written as `1 - smoothstep(v)` and NEVER
    /// `smoothstep(1 - v)` - the two are algebraically identical but only the
    /// first lands on exact `0.0f` at `v = 1`.
    ///
    /// THE ARGUMENT AND THE ARITHMETIC ARE DOUBLE. That is a deliberate,
    /// measured deviation from plan S5.3's `float`-typed signature, and SC-002
    /// (d)'s monotonicity clause is what forces it. Measured this session over
    /// exactly the float expression the plan writes - `fl(fl(u*u) * fl(3 - 2u))`
    /// with `u = fl(step/n)` - swept over the configured fade lengths:
    ///
    ///   n =  33 750 steps (the 45 s default fade-in  at 48 kHz): 0 violations
    ///   n = 135 000 steps (the 180 s default fade-out at 48 kHz): TEN 1-ulp DIPS,
    ///                     all inside the last 0.2 % of the segment
    ///
    /// The cause is arithmetic, not a defect in the shape: at `u -> 1` with
    /// n = 135 000 the true per-step increment is `6u(1-u)du = 5.3e-9` relative,
    /// while the two roundings inside the float expression each move the result
    /// by up to `6e-8` relative - the signal is an order of magnitude below the
    /// noise of its own evaluation, so the emitted float series jitters by
    /// +/- 1 ulp instead of rising. Evaluating the SAME expression in double
    /// drops that evaluation noise to ~1e-16 relative, so the single rounding to
    /// float is a monotone map of a strictly increasing quantity: the emitted
    /// series is then monotone non-decreasing EXACTLY. Re-verified with zero
    /// violations over n in {750, 33 750, 135 000, 225 000, 450 000} x target in
    /// {1.1e-5, 0.01, 0.35, 1.0}, fade-in and fade-out forms, with the fade-out
    /// still landing on exactly 0.0f.
    ///
    /// The alternative - widening SC-002 (d) to "monotone to within 1 ulp" -
    /// was rejected: the criterion is the line, and the line does not move.
    ///
    /// Cost: one double multiply chain per LIVE child per control step, i.e. at
    /// most kMaxChildren (16) per 64 samples, against FR-072's 10 667 ns budget
    /// for a 512-sample block. The FR-073 stage probe prices the owned-slot
    /// write phase as the dominant term; this is not in that neighbourhood.
    [[nodiscard]] static constexpr double smoothstep(double u) noexcept {
        return u * u * (3.0 - 2.0 * u);
    }

    /// @brief FR-032. Retire a child ON THE SAME CONTROL STEP its amplitude
    ///        reaches 0 (plan S5.2).
    ///
    /// The slot bit is cleared here, so S3.4's UNCONDITIONAL owned-region pad -
    /// which runs after this on the very same chunk - rewrites the vacated slot
    /// as `ratio = index + 1, amplitude = 0`. Without that pairing a retired
    /// child's slot keeps its last fade-out value and sounds forever: "the drone
    /// grows and never dies back", the failure this phase exists to prevent.
    void retire(Child& c) noexcept {
        c.phase = Phase::Idle;
        c.amplitude = 0.0f;  // EXACTLY zero, never an asymptotic tail
        slotMask_ &= ~(std::uint64_t{1} << static_cast<std::size_t>(c.slot));
        if (liveCount_ > 0) {
            --liveCount_;
        }
        ++completedChildren_;
    }

    /// @brief Plan S5.2, the per-child lifecycle pass. ONE pass, executed at
    ///        S3.3 step (4) - BEFORE any spawn of the same control step.
    ///
    /// The ordering is load-bearing: a child latched later in this same step has
    /// `step == 0` and emits amplitude 0 on its first chunk, which is FR-031's
    /// C1 start.
    ///
    /// TIME IS COUNTED IN INTEGER CONTROL STEPS, never in accumulated seconds
    /// (plan S5.1). Accumulating `elapsedSec += controlDtSec_` over a 300 s fade
    /// at 48 kHz is 225 000 float additions whose worst-case accumulated
    /// rounding is ~0.67 % - outside SC-010's +/- 0.5 % band ON CORRECT CODE.
    /// Integer counting is exact; the only quantisation left is the one-step
    /// rounding at spawn (<= 0.67 ms at 48 kHz).
    ///
    /// THE PHASE IS DERIVED FROM ONE `step` COUNTER AGAINST THREE LATCHED
    /// BOUNDS, which is what makes FR-033 true BY CONSTRUCTION: nothing a setter
    /// writes after the latch is ever read again by a child already in flight.
    /// The natural mistake - recomputing `fadeInSteps` per chunk from the
    /// current setter value - passes every other clause of every other
    /// criterion and is exactly what SC-002 (f) exists to catch.
    ///
    /// `fadeInSteps` and `fadeOutSteps` are floored at 1 by fadeSecondsToSteps(),
    /// so neither division below can be by zero. `holdSteps` may legitimately be
    /// 0 (FR-030 admits holdSeconds == 0), in which case `endHold == endIn` and
    /// the child passes from FadeIn straight into FadeOut at value `target` -
    /// still C1, because both sides of that junction have derivative 0.
    /// Range: the longest configurable lifetime is 300 + 900 + 600 = 1800 s,
    /// which at 192 kHz is 5.4e6 steps - three orders below UINT32_MAX.
    void advanceChildren() noexcept {
        for (Child& c : children_) {
            if (c.phase == Phase::Idle) {
                continue;
            }
            ++c.step;
            const std::uint32_t endIn = c.fadeInSteps;
            const std::uint32_t endHold = endIn + c.holdSteps;
            const std::uint32_t endOut = endHold + c.fadeOutSteps;
            const double target = static_cast<double>(c.target);
            if (c.step < endIn) {
                c.phase = Phase::FadeIn;
                const double u = static_cast<double>(c.step) / static_cast<double>(endIn);
                c.amplitude = static_cast<float>(target * smoothstep(u));
            } else if (c.step < endHold) {
                c.phase = Phase::Hold;
                c.amplitude = c.target;  // EXACTLY the latched target
            } else if (c.step < endOut) {
                c.phase = Phase::FadeOut;
                const double v = static_cast<double>(c.step - endHold) /
                                 static_cast<double>(c.fadeOutSteps);
                c.amplitude = static_cast<float>(target * (1.0 - smoothstep(v)));
            } else {
                retire(c);  // FR-032
            }
        }
    }

    // =========================================================================
    // The spawn event (plan S4) - T008
    // =========================================================================

    /// @brief FR-010/FR-011 strongest-K insertion, maintained on ASCENDING `i`.
    ///
    /// The comparison is a STRICT `>` and the scan runs on ascending index, so
    /// an equal amplitude can never displace an earlier (lower) index: FR-011's
    /// tie-break falls out of the loop shape rather than being a special case.
    void insertDescending(std::size_t index, float amp, float ratio) noexcept {
        const std::size_t k = std::min(parentCountK_, kMaxParents);  // k >= 1 always
        std::size_t pos = parentSelected_;
        while (pos > 0 && amp > parentAmp_[pos - 1]) {
            --pos;
        }
        if (pos >= k) {
            return;  // weaker than every retained entry, and the list is full
        }
        const std::size_t last = std::min(parentSelected_, k - 1);
        for (std::size_t j = last; j > pos; --j) {
            parentIdx_[j] = parentIdx_[j - 1];
            parentAmp_[j] = parentAmp_[j - 1];
            parentRatio_[j] = parentRatio_[j - 1];
        }
        parentIdx_[pos] = index;
        parentAmp_[pos] = amp;
        parentRatio_[pos] = ratio;
        if (parentSelected_ < k) {
            ++parentSelected_;
        }
    }

    /// @brief Append one log2-ratio to the FR-022 spacing set, never overflowing.
    ///
    /// The array is sized kMaxSlots + kMaxChildrenPerEvent, which is exactly the
    /// worst case (scanEnd + owned live children <= capacity_ <= kMaxSlots, plus
    /// at most kMaxChildrenPerEvent siblings), so the bound below can never
    /// actually reject. It is kept because a silent out-of-bounds write is the
    /// one failure mode a fixed-capacity design must not have.
    void pushOccupied(float logRatio) noexcept {
        if (occupiedCount_ < occupiedLog2_.size()) {
            occupiedLog2_[occupiedCount_] = logRatio;
            ++occupiedCount_;
        }
    }

    /// @brief Plan S4.4 PURE PEEK. Reads cursor_ and slotMask_, mutates NEITHER.
    ///
    /// Called once per offered child at S4.2 (s0), BEFORE any draw. The split
    /// from commitSlot() is load-bearing: a cursor advanced on a child that was
    /// later refused would make the slot sequence depend on the seeded
    /// accept/reject outcomes, and SC-018 (d) - an identical slot sequence under
    /// a DIFFERENT seed - would fail on correct code.
    [[nodiscard]] bool peekSlot(std::size_t& out) const noexcept {
        const std::size_t base = reserveBase();
        const std::size_t cap = capacity_;
        if (base >= cap) {
            return false;  // numChildSlots() == 0: the FR-054 pass-through
        }
        const std::size_t span = cap - base;
        // Re-clamped AS A LOCAL after an FR-055 setCapacity(); cursor_ itself is
        // NOT written here - that is what makes this a pure peek.
        const std::size_t start = (cursor_ < base || cursor_ >= cap) ? base : cursor_;
        for (std::size_t n = 0; n < span; ++n) {
            const std::size_t s = base + ((start - base + n) % span);
            if ((slotMask_ & (std::uint64_t{1} << s)) == 0u) {
                out = s;
                return true;
            }
        }
        return false;  // every owned slot is live -> FR-025
    }

    /// @brief Plan S4.4 COMMIT. Called ONLY from the latch, ONLY on an accepted
    ///        candidate. Advances cursor_ PAST the taken slot (FR-056).
    void commitSlot(std::size_t s) noexcept {
        const std::size_t base = reserveBase();
        const std::size_t cap = capacity_;
        slotMask_ |= (std::uint64_t{1} << s);
        if (cap > base) {
            cursor_ = base + ((s - base + 1) % (cap - base));
        }
    }

    /// @brief The first Idle entry of the FR-034 table, or kMaxChildren.
    [[nodiscard]] std::size_t findFreeTableEntry() const noexcept {
        for (std::size_t t = 0; t < kMaxChildren; ++t) {
            if (children_[t].phase == Phase::Idle) {
                return t;
            }
        }
        return kMaxChildren;
    }

    /// @brief Draw (d1). FR-016: WITHOUT replacement while unused parents remain;
    ///        the mask is cleared first once every selected parent has supplied a
    ///        child. Consumes EXACTLY ONE nextUnipolar(), unconditionally.
    /// @return A position in [0, parentSelected_), i.e. an index into parentIdx_.
    [[nodiscard]] std::size_t drawParent() noexcept {
        std::size_t available = 0;
        for (std::size_t j = 0; j < parentSelected_; ++j) {
            if ((parentUsedMask_ & (std::uint32_t{1} << j)) == 0u) {
                ++available;
            }
        }
        if (available == 0) {
            parentUsedMask_ = 0u;  // FR-016 with-replacement fallback
            available = parentSelected_;
        }
        const float u = eventRng_.nextUnipolar();
        // The min is what keeps u == 1.0f (reachable: nextUnipolar() is (0, 1]) in range.
        const std::size_t pick =
            std::min(static_cast<std::size_t>(u * static_cast<float>(available)), available - 1);
        std::size_t seen = 0;
        for (std::size_t j = 0; j < parentSelected_; ++j) {
            if ((parentUsedMask_ & (std::uint32_t{1} << j)) != 0u) {
                continue;
            }
            if (seen == pick) {
                parentUsedMask_ |= (std::uint32_t{1} << j);
                return j;
            }
            ++seen;
        }
        return 0;  // unreachable: available >= 1 guarantees a hit above
    }

    /// @brief Draw (d2). FR-060 weights; all-zero falls back to a UNIFORM draw.
    ///        Consumes EXACTLY ONE nextUnipolar() on BOTH branches, so the stream
    ///        position never depends on the weight configuration.
    [[nodiscard]] Relation drawRelation() noexcept {
        float total = 0.0f;
        for (std::size_t j = 0; j < kNumRelations; ++j) {
            total += relationWeight_[j];
        }
        const float u = eventRng_.nextUnipolar();
        if (total <= 0.0f) {
            const std::size_t idx =
                std::min(static_cast<std::size_t>(u * static_cast<float>(kNumRelations)),
                         kNumRelations - 1);
            return static_cast<Relation>(idx);
        }
        const float x = u * total;
        float acc = 0.0f;
        for (std::size_t j = 0; j < kNumRelations; ++j) {
            acc += relationWeight_[j];
            if (x <= acc) {
                return static_cast<Relation>(j);
            }
        }
        return static_cast<Relation>(kNumRelations - 1);
    }

    /// @brief Draws (d3)/(d4). ONE nextFloat() mapped to the PUNCTURED band
    ///        [-kMaxDetuneCents, -kMinDetuneCents] u [kMinDetuneCents,
    ///        kMaxDetuneCents] in a single expression (FR-020). `b == 0` takes the
    ///        positive branch rather than producing 0.
    [[nodiscard]] float drawDetuneCents() noexcept {
        const float b = eventRng_.nextFloat();  // bipolar, random.h:59
        const float sign = (b < 0.0f) ? -1.0f : 1.0f;
        const float magnitude =
            kMinDetuneCents + std::fabs(b) * (kMaxDetuneCents - kMinDetuneCents);
        return sign * magnitude;
    }

    /// @brief FR-020's per-relation ratio factor. `cents` is read only by
    ///        DetunedNeighbour.
    [[nodiscard]] static float relationFactor(Relation r, float cents) noexcept {
        if (r == Relation::Octave) {
            return kOctaveFactor;
        }
        if (r == Relation::Fifth) {
            return kFifthFactor;
        }
        return centsToPitchRatioFast(cents);  // in-domain by kMaxDetuneCents == 50
    }

    /// @brief FR-023 / Clarification Q1. HarmonicCloud's own slot-indexed tilt
    ///        law, RESTATED (D-2/D-9) so the latch can CANCEL it.
    ///
    /// The identity branch is copied verbatim from harmonic_cloud.h:1430-1432, so
    /// consumerTiltDb_ == 0 and index == 0 return EXACTLY 1.0f and the whole Q1
    /// mechanism is bit-inert at its default.
    [[nodiscard]] float tiltGain(std::size_t index) const noexcept {
        if (consumerTiltDb_ == 0.0f || index == 0) {
            return 1.0f;
        }
        return std::exp2(consumerTiltDb_ * std::log2(static_cast<float>(index + 1)) *
                         kLog2TenOver20);
    }

    /// @brief Plan S5.1. Seconds -> INTEGER control steps, floored at 1 for the
    ///        two fades so a 1 s fade at 8 kHz is never degenerate.
    [[nodiscard]] std::uint32_t fadeSecondsToSteps(float seconds) const noexcept {
        const float steps = std::round(std::max(0.0f, seconds) * controlRateHz_);
        return std::max(std::uint32_t{1}, static_cast<std::uint32_t>(steps));
    }

    /// @brief The hold's twin, floored at 0 - FR-030 admits holdSeconds == 0.
    [[nodiscard]] std::uint32_t holdSecondsToSteps(float seconds) const noexcept {
        const float steps = std::round(std::max(0.0f, seconds) * controlRateHz_);
        return static_cast<std::uint32_t>(steps);
    }

    /// @brief Plan S4.3. FOUR tests, in the given order, ALL of them REJECTIONS.
    ///
    /// Nothing here clamps: "clamping moves a partial to a pitch nobody asked
    /// for, which is a defect; refusing to grow one is not" (FR-021). Test (4) is
    /// the plan's S14 C-4 addition - the tilt divisor reaches x3993 at
    /// consumerTiltDb == -12 and slot 63, so a pathological parent amplitude can
    /// overflow the latched target to Inf, which FR-009 (d) forbids writing.
    ///
    /// @param candidateRatio  The candidate child ratio.
    /// @param slot            The owned slot peekSlot() reserved for this child.
    /// @param parentAmp       The parent's amplitude AT SPAWN (FR-023).
    /// @param smoothedDepth   The value S3.3 step (2) bound from depthRamp_.process().
    /// @param outTarget       Set to the latched target ONLY on acceptance.
    [[nodiscard]] bool accept(float candidateRatio, std::size_t slot, float parentAmp,
                              float smoothedDepth, float& outTarget) const noexcept {
        // (1) finiteness and positivity - also what keeps the std::log2 below in domain
        if (!detail::isFinite(candidateRatio) || candidateRatio <= 0.0f) {
            return false;
        }
        // (2) FR-021 ratio bounds - a REJECTION test, NEVER a clamp
        if (candidateRatio < kMinChildRatio || candidateRatio > kMaxChildRatio) {
            return false;
        }
        // (3) FR-022 / FR-016 minimum spacing, evaluated in the log-ratio domain
        const float candidateLog2 = std::log2(candidateRatio);
        for (std::size_t j = 0; j < occupiedCount_; ++j) {
            if (std::fabs(candidateLog2 - occupiedLog2_[j]) < kMinRatioSpacingLog2) {
                return false;
            }
        }
        // (4) latched-target finiteness (plan S14 C-4)
        const float target = parentAmp * childGain_ * smoothedDepth / tiltGain(slot);
        if (!detail::isFinite(target) || target < 0.0f) {
            return false;
        }
        outTarget = target;
        return true;
    }

    /// @brief Plan S4.5, the latch. The ONLY place engaged_ is set.
    ///
    /// The hold-jitter nextFloat() here is draw (d5) of S4.2's normative
    /// sequence - the LAST draw of the child, taken only now that the candidate
    /// is accepted. It is BIPOLAR, so `1 + holdJitter_ * u >= 0` for every
    /// holdJitter_ in [0, 1] and the jittered hold is never negative (FR-036); at
    /// holdJitter_ == 0 the product is exactly holdSec_ * 1.0f, so every latched
    /// hold equals the configured value bit-for-bit (SC-002 (e)).
    ///
    /// @return false when the FR-034 table has no Idle entry - which cannot
    ///         happen after a successful peekSlot() in the ordinary
    ///         configuration, but is handled rather than assumed because an
    ///         FR-055 shrink can leave legacy children holding table entries.
    [[nodiscard]] bool place(std::size_t slot, std::size_t chosen, float candidateRatio,
                             float target, Relation relation, bool isFallback) noexcept {
        const std::size_t t = findFreeTableEntry();
        if (t >= kMaxChildren) {
            return false;
        }
        Child& ch = children_[t];
        ch.step = 0;
        ch.fadeInSteps = fadeSecondsToSteps(fadeInSec_);    // FR-033, UNJITTERED
        ch.fadeOutSteps = fadeSecondsToSteps(fadeOutSec_);  // FR-033, UNJITTERED
        ch.holdSteps = holdSecondsToSteps(holdSec_ * (1.0f + holdJitter_ * eventRng_.nextFloat()));
        ch.ratio = candidateRatio;  // FR-024, latched
        ch.target = target;         // FR-023, latched and tilt-compensated
        ch.amplitude = 0.0f;        // FR-031's C1 start
        ch.slot = static_cast<std::uint8_t>(slot);
        ch.parentIndex = static_cast<std::uint8_t>(parentIdx_[chosen]);
        ch.relation = relation;  // the ORIGINAL relation, even for a fallback (FR-026)
        ch.phase = Phase::FadeIn;
        ch.fallback = isFallback;

        // A sibling is a collision candidate for the NEXT child of this same
        // event (FR-016, Clarification Q4) even though nothing is in the array yet.
        pushOccupied(std::log2(candidateRatio));

        commitSlot(slot);
        ++liveCount_;
        ++spawnedChildren_;
        engaged_ = true;  // STICKY (Clarification Q8): set ONCE, here, and nowhere else
        return true;
    }

    /// @brief Plan S4, the spawn event. Executes only on a control step where
    ///        the gate is non-zero and an event is armed (S3.3 step 5).
    void runEvent(const float* ratios, const float* amplitudes, std::size_t pc,
                  float smoothedDepth) noexcept {
        // Plan S1.6 / S4.2: THE FIRST STATEMENT. A SEQUENTIAL event stream cannot
        // satisfy FR-041 - an instance awake during a prefix would consume event
        // draws a dormant one does not - so the stream is re-derived from
        // (seed, control step) alone.
        eventRng_.seed(deriveStreamSeed(deriveStreamSeed(seed_, kSaltEvent),
                                        static_cast<std::size_t>(controlStep_)));

        // ---- S4.1 the strongest-K parent scan, ONCE PER EVENT (FR-013) ------
        ++spawnEvents_;
        ++parentScans_;  // adjacent on purpose: FR-013's identity holds by construction
        parentSelected_ = 0;
        parentUsedMask_ = 0u;
        const std::size_t scanEnd = std::min(pc, reserveBase());  // FR-010, load-bearing
        for (std::size_t i = 0; i < scanEnd; ++i) {
            const float r = ratios[i];
            const float a = amplitudes[i];
            if (!detail::isFinite(r) || !detail::isFinite(a)) {
                continue;  // FR-009 (e): disqualify, never copy, never write back
            }
            if (r <= 0.0f) {
                continue;
            }
            if (a <= kSilentParentAmplitude) {
                continue;  // FR-011: a silent partial has no harmonics to grow
            }
            insertDescending(i, a, r);
        }
        if (parentSelected_ == 0) {
            // FR-014: the event is CONSUMED and produces no child. Plan S14 C-10
            // is normative here - this path touches NO other counter, and in
            // particular NOT getRejectedSpawnCount(), which counts rejected
            // candidate RATIOS and an event with no eligible parent never forms one.
            return;
        }

        // ---- S4.3's spacing set, built ONCE per event -----------------------
        occupiedCount_ = 0;
        for (std::size_t i = 0; i < scanEnd; ++i) {
            const float r = ratios[i];
            if (!detail::isFinite(r) || r <= 0.0f) {
                continue;
            }
            pushOccupied(std::log2(r));
        }
        for (std::size_t c = 0; c < kMaxChildren; ++c) {
            const Child& ch = children_[c];
            if (ch.phase == Phase::Idle) {
                continue;
            }
            const auto s = static_cast<std::size_t>(ch.slot);
            if (s < reserveBase() || s >= capacity_) {
                // An FR-055 LEGACY child is never written into the array, so it
                // is not a partial anything can collide with. It is NOT excluded
                // from slot occupancy - FR-053 holds wherever the slot sits.
                continue;
            }
            pushOccupied(std::log2(ch.ratio));
        }

        // ---- S4.2 the per-child draw sequence -------------------------------
        const std::size_t numChildren = childrenPerEvent_;
        for (std::size_t k = 0; k < numChildren; ++k) {
            ++offeredChildren_;

            // (s0) SLOT AVAILABILITY IS A PRECONDITION OF THE CHILD, tested ONCE,
            //      BEFORE the attempt loop and BEFORE any draw, with a PURE PEEK.
            //      FR-025 is a per-EVENT condition - retrying a draw cannot make a
            //      slot appear - so a slot-exhausted child consumes NO attempt and
            //      NO draw and is counted refused EXACTLY ONCE, here.
            std::size_t slot = 0;
            if (!peekSlot(slot)) {
                ++rejectedSpawns_;
                ++refusedChildren_;
                continue;
            }

            bool placed = false;
            for (std::size_t attempt = 0; attempt < kMaxSpawnAttempts && !placed; ++attempt) {
                const std::size_t chosen = drawParent();                            // (d1)
                const Relation relation = drawRelation();                           // (d2)
                const float cents =                                                 // (d3)
                    (relation == Relation::DetunedNeighbour) ? drawDetuneCents() : 0.0f;
                const float parentRatio = parentRatio_[chosen];
                const float parentAmp = parentAmp_[chosen];
                const float candidate = parentRatio * relationFactor(relation, cents);

                float target = 0.0f;
                if (accept(candidate, slot, parentAmp, smoothedDepth, target)) {
                    if (!place(slot, chosen, candidate, target, relation, false)) {
                        break;  // no table entry: the tail below counts ONE refusal
                    }
                    placed = true;
                    break;
                }
                ++rejectedSpawns_;  // FR-026: EVERY failed attempt counts

                if (attempt == kMaxSpawnAttempts - 1 && relation != Relation::DetunedNeighbour) {
                    // FR-026 final-attempt fallback: a DETUNED variant of the SAME
                    // relation, re-tested once. A DetunedNeighbour has no separate
                    // fallback form - it is already detuned.
                    const float fallbackCents = drawDetuneCents();  // (d4)
                    const float fallbackRatio = parentRatio * relationFactor(relation, 0.0f) *
                                                centsToPitchRatioFast(fallbackCents);
                    float fallbackTarget = 0.0f;
                    if (accept(fallbackRatio, slot, parentAmp, smoothedDepth, fallbackTarget)) {
                        if (!place(slot, chosen, fallbackRatio, fallbackTarget, relation, true)) {
                            break;
                        }
                        ++fallbackChildren_;
                        placed = true;
                    } else {
                        ++rejectedSpawns_;
                    }
                }
            }

            if (!placed) {
                // The ONLY other refusal increment. Nothing to release: (s0)
                // mutated no state, and place() is the only thing that commits.
                ++refusedChildren_;
            }
        }
    }

    // =========================================================================
    // The write region (plan S3.4)
    // =========================================================================

    /// @brief Write the owned slots and return the count the caller passes on.
    ///
    /// The NOT-ENGAGED early return is the whole of FR-051's disengaged
    /// contract, and it is what makes a disabled BloomEngine BIT-IDENTICAL to
    /// not being in the chain at all (SC-014). A call that wrote nothing returns
    /// the caller's UNCLAMPED parentCount, exactly as the nullptr path does -
    /// returning the clamped `pc` would make the cloud pad `[pc, parentCount)`
    /// to amplitude 0, i.e. audible partial loss from a component specified to
    /// be inert (plan S3.1).
    ///
    /// engaged_ is STICKY (Clarification Q8): set once, in the S4.5 latch, and
    /// cleared only by prepare()/reset(). It is deliberately NOT level-tracking -
    /// an implementation that fell back to a pass-through when
    /// getLiveChildCount() reached 0 would stop padding the gap and the caller's
    /// stale bytes would sound.
    [[nodiscard]] std::size_t applyOutput(float* ratios, float* amplitudes,
                                          std::size_t parentCount, std::size_t pc) noexcept {
        if (!engaged_) {
            return parentCount;
        }
        const std::size_t base = reserveBase();

        if (pc > base) {
            // FR-052, SATURATING rather than wrapping (the
            // ResonanceDriftNetwork::kMaxClampCount idiom). ONE increment per
            // ENGAGED CALL, which is what SC-003's positive arm counts; a wrap
            // would let a long-running engaged render - the one case where the
            // number actually matters - report a FALSE ZERO.
            const std::uint32_t headroom = kMaxOverlapCount - overlapEngagements_;
            overlapEngagements_ += std::min(std::uint32_t{1}, headroom);
        }

        // (a) THE GAP, and it is a HARD REQUIREMENT (FR-051). Slots in
        //     [pc, reserveBase()) sit BELOW the returned count, so the cloud
        //     reads them as live partial content and one stale NaN there rejects
        //     the WHOLE array. Padded with the cloud's own form
        //     (harmonic_cloud.h:825-826) so the two agree by construction.
        for (std::size_t i = pc; i < base; ++i) {
            ratios[i] = static_cast<float>(i + 1);
            amplitudes[i] = 0.0f;
        }
        // (b) THE OWNED REGION, padded UNCONDITIONALLY and BEFORE the child
        //     loop, on EVERY engaged call. That is what repads the slot of a
        //     child that retired on this very step (FR-032); without it a
        //     retired child's slot keeps its last fade-out value and sounds
        //     forever - "the drone grows and never dies back", which is the
        //     failure this phase exists to prevent.
        for (std::size_t i = base; i < capacity_; ++i) {
            ratios[i] = static_cast<float>(i + 1);
            amplitudes[i] = 0.0f;
        }
        for (std::size_t c = 0; c < kMaxChildren; ++c) {
            const Child& ch = children_[c];
            if (ch.phase == Phase::Idle) {
                continue;
            }
            const auto s = static_cast<std::size_t>(ch.slot);
            if (s < base || s >= capacity_) {
                continue;  // FR-055 LEGACY child: never written
            }
            ratios[s] = ch.ratio;                             // latched, finite, > 0 by S4.3
            amplitudes[s] = detail::flushDenormal(ch.amplitude);  // in [0, target] by S5.3
        }
        return capacity_;
    }

    // =========================================================================
    // State (plan S1.5, declaration order)
    // =========================================================================

    // --- configuration ------------------------------------------------------
    double sampleRate_ = 48000.0;
    float invSampleRate_ = 1.0f / 48000.0f;
    float controlDtSec_ = 64.0f / 48000.0f;   ///< kControlChunkSamples / fs
    float controlRateHz_ = 48000.0f / 64.0f;  ///< fs / kControlChunkSamples
    bool prepared_ = false;

    std::size_t capacity_ = kMaxSlots;
    std::size_t requestedChildSlots_ = kDefaultChildSlots;  ///< see numChildSlots()
    std::size_t parentCountK_ = kDefaultParentCount;
    std::size_t childrenPerEvent_ = kDefaultChildrenPerEvent;

    float depth_ = kDefaultDepth;
    float wake_ = 1.0f;
    bool dormant_ = false;
    float spawnRateHz_ = kDefaultSpawnRateHz;
    float childGain_ = kDefaultChildGain;
    float fadeInSec_ = kDefaultFadeInSeconds;
    float holdSec_ = kDefaultHoldSeconds;
    float fadeOutSec_ = kDefaultFadeOutSeconds;
    float holdJitter_ = kDefaultHoldJitterFraction;
    float consumerTiltDb_ = 0.0f;
    std::array<float, kNumRelations> relationWeight_{1.0f, 1.0f, 1.0f};  ///< FR-016 default

    // --- smoothing ----------------------------------------------------------
    LinearRamp depthRamp_{kDefaultDepth};  ///< advanced ONCE PER CONTROL STEP (plan S3.3)

    // --- RNG (plan S1.6) ----------------------------------------------------
    // Two streams, SHAPED DIFFERENTLY ON PURPOSE:
    //  1. clockRng_ - a persistent sequential stream, drawn exactly once per
    //     control step, UNCONDITIONALLY (FR-041, Clarification Q7), before the
    //     probability is even computed. Its position after n control steps is n
    //     draws: a pure function of elapsed control steps, never of
    //     dormancy/wake/depth/rate history.
    //  2. eventRng_ - COUNTER-BASED, re-seeded at the head of every event from
    //     deriveStreamSeed(deriveStreamSeed(seed_, kSaltEvent), controlStep_).
    //     A sequential event stream CANNOT satisfy FR-041: an instance awake
    //     during a prefix consumes event draws a dormant one does not, so the
    //     two positions diverge permanently and every later child differs.
    //     Re-seeding from the step index makes the event draw sequence a
    //     function of (seed, step) alone (plan correction C-5).
    std::uint32_t seed_ = kDefaultSeed;
    Xorshift32 clockRng_{deriveStreamSeed(kDefaultSeed, kSaltClock)};
    Xorshift32 eventRng_{1u};        ///< RE-SEEDED per event from the step index
    std::uint64_t controlStep_ = 0;  ///< total control steps since prepare()/reset()

    // --- grid ---------------------------------------------------------------
    std::size_t controlPhase_ = 0;  ///< absolute residue carried ACROSS calls (plan S3.2)

    // --- lifecycle table (FR-034) -------------------------------------------
    std::array<Child, kMaxChildren> children_{};
    std::uint64_t slotMask_ = 0;  ///< bit i set == some live child holds slot i
    std::size_t liveCount_ = 0;
    std::size_t cursor_ = 0;  ///< FR-056 round-robin cursor
    bool engaged_ = false;    ///< FR-051 STICKY latch (Clarification Q8)
    bool armed_ = false;      ///< one pending event (FR-043, edge-like)

    // --- per-event scratch (fixed size, never resized) ----------------------
    std::array<std::size_t, kMaxParents> parentIdx_{};
    std::array<float, kMaxParents> parentAmp_{};
    std::array<float, kMaxParents> parentRatio_{};
    std::size_t parentSelected_ = 0;
    std::uint32_t parentUsedMask_ = 0;  ///< FR-016 without-replacement
    std::array<float, kMaxSlots + kMaxChildrenPerEvent> occupiedLog2_{};
    std::size_t occupiedCount_ = 0;

    // --- counters -----------------------------------------------------------
    std::uint64_t spawnEvents_ = 0, discardedEvents_ = 0, parentScans_ = 0;
    std::uint64_t offeredChildren_ = 0, spawnedChildren_ = 0, refusedChildren_ = 0;
    std::uint64_t fallbackChildren_ = 0, completedChildren_ = 0, rejectedSpawns_ = 0;
    std::uint32_t overlapEngagements_ = 0;

    friend struct detail::BloomEngineNonFiniteProbe;  // plan S7.5
};

}  // namespace Krate::DSP
