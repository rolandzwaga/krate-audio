// ==============================================================================
// Layer 3: System - VoragoMacroMatrix (the performance macros)
// ==============================================================================
// Vorago Phase 10. Spec slug: vorago-phase10-voice-engine.
//   Spec:    specs/vorago-phase10-voice-engine/spec.md
//   Plan:    specs/vorago-phase10-voice-engine/plan.md   (S7.1 - S7.4)
//   Tasks:   specs/vorago-phase10-voice-engine/tasks.md
//   Roadmap: specs/Vorago-roadmap.md, Part A -> Phase 10 (lines 446-474)
//
// The macros are a constexpr DATA table of
// {macro, owner, target, base, amount, curve} rows (AR-6), evaluated through
// the shared Layer 0 applyModCurve() (core/modulation_curves.h:38) - NOT
// "ModulationEngine presets". That type is named NOWHERE in this file.
//
// Feature: vorago-phase10-voice-engine
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (table lookup plus arithmetic; no
//   allocation, lock, exception or IO)
// - Principle III: Modern C++ (C++20, constexpr table, value semantics)
// - Principle IX: Layer 3 - Layers 0-2 plus Layer 3 PEERS only
// - Principle X: DSP Constraints (reject-never-clamp, finite-in / finite-out)
//
// ------------------------------------------------------------------------------
// ARCHITECTURE RULINGS THAT SHAPE THIS FILE
//
// AR-1  TWO APPLICATION SURFACES, because the cavern is Layer 4 and NO effects/
//       HEADER MAY BE INCLUDED HERE, EVER:
//         - apply(VoragoEngine&)     pushes the Voice- and Engine-owned rows
//                                    through the engine's own setters.
//         - computeCavernTargets()   RETURNS the cavern-owned rows as plain
//                                    floats in a POD. No Layer 4 type is named
//                                    anywhere in this file; the CALLER pushes
//                                    them into its reverb. This mirrors
//                                    SeraphisMacroMatrix's answer to the same
//                                    problem (SeraphisAetherTargets,
//                                    seraphis_macro_matrix.h:123-133).
// AR-6  The table IS the record of the tuning. `amount` and `curve` are
//       implementation tuning; what is NORMATIVE is each row's DIRECTION and
//       the per-macro minimum end-to-end effect size. Retuning means editing an
//       `amount` here, never lowering a criterion.
// ------------------------------------------------------------------------------
//
// FR-067 - IDEMPOTENCE IS A PROPERTY OF THE FORWARDERS, AND IT IS STATED HERE
// BECAUSE NOTHING ELSE STATES IT. Every Voice- and Engine-owned forwarder that
// apply() writes through is A PLAIN SCALAR STORE OR A setTarget() on a ramp the
// owning component already runs - NEVER a snapTo(), NEVER a smoother reset,
// NEVER a re-arm of a ramp that is already at its destination. The matrix adds
// no smoother of its own, so calling apply() every block with unchanged macro
// values must step NOTHING. SC-010 cannot see a violation of this (it measures
// a zipper DURING a ramp, where a re-armed ramp looks identical to a correct
// one) and SC-009 clause 3 covers only the neutral; the case that can see it is
// VoragoMacro_ApplyIsIdempotent, which renders the same NON-neutral macro vector
// twice - once with apply() called once before the render, once with it called
// at every control chunk - and requires the two renders to be bit-identical.
//
// PER-TARGET BASE OVERRIDE (Phase 12, specs/vorago-phase12-parameters FR-001 /
// FR-002): setTargetBase / resetTargetBases / getTargetBase.
// WHY IT EXISTS: the plugin exposes each macro target's base as its own
// automatable parameter, and the macros must keep composing ON TOP of that
// user-set base rather than fighting it. evaluateAll() therefore seeds each
// target with the override when one is set, else with the kRows literal, and
// accumulates the row contributions exactly as before.
// WHAT STILL HOLDS: everyRowSharesOneBasePerTarget() still guarantees ONE
// literal base per target at compile time - the override replaces that literal
// as the seed, it never makes two rows disagree. NO HEADROOM RESCALING: an
// override near the destination's travel limit is not re-scaled to leave room
// for the macro; the summed value is clamped by the destination setter (the
// "summation first, clamp at the destination" rule of apply()), and the matrix
// itself clamps nothing. Phase 10's SC-009 clause 1 (every row's base equals the
// prepared value) holds WITH NO OVERRIDE SET; a matrix never given an override,
// or one after resetTargetBases(), is bit-identical to the Phase 10 matrix.
//
// BUILD STATE (tasks.md).
// T006 established this file as a compiling, lint-visible stub.
// T017 (THIS PASS) lands the enums, the PODs, kRows, the six predicates and
//   apply() / computeCavernTargets().
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/db_utils.h>  // detail::isFinite (fast-math-immune)
#include <krate/dsp/core/modulation_curves.h>

// Layer 3: Systems (peer)
#include <krate/dsp/systems/vorago_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// The twelve performance controls (roadmap line 464, OQ-2 confirmed at twelve).
///
/// The three concepts of roadmap line 464 that are NOT here are FOLDED, and the
/// fold is normative on its survivor (FR-068):
///   Decay       -> Age (the shortening half) + Depth (the lengthening half)
///   Instability -> Entropy
///   Distance    -> Fog
enum class VoragoMacro : std::uint8_t {
    Darkness = 0,
    Age,
    Density,
    Movement,
    Gravity,
    Entropy,
    Pressure,
    Weight,
    Fog,
    Life,
    Depth,
    Mass,
    Count
};

/// Which surface owns a row's target.
enum class VoragoMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Cavern };

/// Every parameter any row may write.
///
/// EVERY enumerator is float-valued on a SHIPPED setter (Q1): there is no
/// discrete-target row, and this phase adds no DSP to express one.
///
/// Phase 12 (specs/vorago-phase12-parameters, ruling R-1 path B, B-1 / B-2)
/// adds TWO macro-only targets, each default-inert (base == the shipped value,
/// 0) and each the last enumerator of its owner block: ResonanceOctaveLock
/// (Voice) for Gravity and OutputDriveDb (Engine) for Pressure. Neither adds a
/// registered parameter ID.
///
/// Declared in OWNER BLOCKS, and the block an enumerator sits in IS its owner -
/// everyRowOwnerIsValid() turns that sentence into a compile-time biconditional.
/// The Cavern block's order IS VoragoCavernTargets' field order, so
/// cavernFieldIndex() is a pure offset.
enum class VoragoMacroTarget : std::uint8_t {
    // -- Voice-owned (VoragoVoice forwarders, vorago_voice.h:1052-1213) ------
    CloudRichness,          ///< setRichness            (:1052)
    CloudSpectralTiltDb,    ///< setSpectralTiltDb      (:1060)
    CloudMutation,          ///< setMutation            (:1068)
    CloudInharmonicity,     ///< setInharmonicity       (:1074)
    CloudDriftDepthCents,   ///< setDriftDepthCents     (:1077)
    NoiseLevelDb,           ///< setNoiseLevelDb        (:1086)
    NoiseWakeBase,          ///< setNoiseWakeBase       (:1098)
    NoiseWanderRate,        ///< setNoiseWanderRate     (:1103)
    ResonanceGravity,       ///< setResonanceGravity    (:1109)
    ResonanceMix,           ///< setResonanceMix        (:1115)
    ResonanceWanderRate,    ///< setResonanceWanderRate (:1118)
    EcologyMix,             ///< setEcologyMix          (:1123)
    EcologyLoopGain,        ///< setEcologyLoopGain     (:1127)
    BodyBlend,              ///< setBodyBlend           (:1137)
    BodyDamping,            ///< setBodyDamping         (:1143)
    BodyResonance,          ///< setBodyResonance       (:1151)
    BodyMix,                ///< setBodyMix             (:1159)
    EcosystemDepth,         ///< setEcosystemDepth      (:1178)
    EventRateScale,         ///< setEventRateScale      (:1186)
    BloomDepth,             ///< setBloomDepth          (:1193)
    BloomSpawnRateHz,       ///< setBloomSpawnRateHz    (:1199)
    BreathingDepth,         ///< setBreathingDepth      (:1202)
    BreathingIrregularity,  ///< setBreathingIrregularity (:1207)
    TidalDepth,             ///< setTidalDepth          (:1212)
    ResonanceOctaveLock,    ///< setResonanceOctaveLock (Phase 12, spec B-1)
    // -- Engine-owned (VoragoEngine setters, vorago_engine.h:713-774) --------
    SubToneLevelOffsetDb,  ///< setSubToneLevelOffsetDb (:713)
    SubTrackingAmount,     ///< setSubTrackingAmount    (:719)
    SmearAmount,           ///< setSmearAmount          (:737)
    SmearDecoherence,      ///< setSmearDecoherence     (:743)
    SmearTilt,             ///< setSmearTilt            (:750)
    GhostPeakLevel,        ///< setGhostPeakLevel       (:758)
    AtmosBlur,             ///< setAtmosBlur            (:763)
    OutputSaturation,      ///< setOutputSaturation     (:769)
    OutputDriveDb,         ///< setOutputDriveDb        (Phase 12, spec B-2)
    // -- Cavern-owned (each MUST have a 1:1 VoragoCavernTargets field) -------
    CavernSize,
    CavernDarkness,
    CavernDecaySeconds,
    CavernFog,
    CavernDamperDepth,
    CavernMix,
    CavernWidth,
    Count
};

/// FR-063. The Cavern-owned rows as plain floats - no Layer 4 type is named (AR-1).
///
/// Every default is the CavernVerb shipped literal, DUPLICATED with its source
/// line, because a Layer 3 header may not name a Layer 4 type at all. This is
/// the SeraphisAetherTargets construction (seraphis_macro_matrix.h:105-133)
/// applied to the cavern.
///
/// Fields are declared IN ENUMERATOR ORDER, so the field index is a pure offset.
///
/// A DRIFTED LITERAL IS INVISIBLE TO A LITERAL-VS-LITERAL COMPARISON, which is
/// why SC-009 clause 1's cavern half re-checks each of these against the REAL
/// CavernVerb constant in the only Layer-4-aware TU
/// (unit/effects/vorago_composed_chain_test.cpp, tasks.md T022).
struct VoragoCavernTargets {
    float size = 0.50f;           // cavern_verb.h:253 kDefaultSize
    float darkness = 0.80f;       // :254 kDefaultDarkness
    float decaySeconds = 20.0f;   // :255 kDefaultDecaySeconds
    float fog = 0.30f;            // :259 kDefaultFog
    float damperDepth = 0.35f;    // :247 kDefaultDamperDepth
    float mix = 1.00f;            // :264 kDefaultMix
    float width = 1.00f;          // :263 kDefaultWidth
};

/// FR-061's documented neutrals (plan S8.4, reproduced not re-derived).
///
/// Gravity is BIPOLAR around 0.5 (0 = air, 0.5 = neutral, 1 = stone); the other
/// eleven are unipolar with a neutral of 0. A default-constructed
/// VoragoMacroValues - and therefore a default-constructed VoragoMacroMatrix -
/// is ALREADY at the FR-066 identity.
struct VoragoMacroValues {
    float darkness = 0.0f;
    float age = 0.0f;
    float density = 0.0f;
    float movement = 0.0f;
    float gravity = 0.5f;  ///< BIPOLAR: 0 = air, 0.5 = neutral, 1 = stone
    float entropy = 0.0f;
    float pressure = 0.0f;
    float weight = 0.0f;
    float fog = 0.0f;
    float life = 0.0f;
    float depth = 0.0f;
    float mass = 0.0f;
};

/// FR-060. One row of the mapping; the mapping is DATA, not code.
struct VoragoMacroRow {
    VoragoMacro macro;
    VoragoMacroTargetOwner owner;
    VoragoMacroTarget target;
    /// FR-064: for a Voice- or Engine-owned row this is THE FR-090 PREPARE-TIME
    /// VALUE the voice or engine actually installs on that target - cited to its
    /// plan S8 row - NOT the owning component's own shipped default. Only Cavern
    /// rows use a component default (FR-063), because nothing in this phase
    /// prepares a CavernVerb.
    ///
    /// The argument, restated because it is the one thing a reader will want to
    /// re-litigate: FR-017 sets atmosphere density to 0.30 over the component's
    /// shipped 4.0 (atmosphere_engine.h:821-828), so a row basing on the
    /// COMPONENT default would have apply() write 4.0 back AT THE NEUTRAL,
    /// destroying the ghost configuration on the first block and falsifying
    /// FR-066 and SC-009 by construction. seraphis_macro_matrix.h:160-163
    /// defines `base` the same way for the same reason.
    ///
    /// Rows sharing a target MUST agree on `base`; asserted below the class by
    /// everyRowSharesOneBasePerTarget().
    float base;
    float amount;    ///< SIGNED; implementation tuning (AR-6).
    ModCurve curve;  ///< Linear | Exponential | SCurve ONLY (FR-065).
};

/// @brief The Vorago performance-macro table (Layer 3).
///
/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: every method is noexcept, allocation-free, lock-free,
///      exception-free. evaluateAll() is a fixed-size stack array plus one pass
///      over a constexpr table.
class VoragoMacroMatrix {
public:
    // =========================================================================
    // Constants - ALL class-scoped
    // =========================================================================

    static constexpr std::size_t kNumMacros = static_cast<std::size_t>(VoragoMacro::Count);
    static constexpr std::size_t kNumTargets = static_cast<std::size_t>(VoragoMacroTarget::Count);

    /// FR-060's table length: 3 Darkness + 3 Age + 4 Density + 5 Movement
    /// + 2 Gravity + 5 Entropy + 5 Pressure + 4 Weight + 7 Fog + 3 Life
    /// + 4 Depth + 5 Mass = 50.
    static constexpr std::size_t kNumRows = 50;

    /// The first enumerator of each owner block. The block an enumerator sits in
    /// IS its owner (see VoragoMacroTarget), and these three constants are how
    /// everyRowOwnerIsValid() states that as a compile-time biconditional.
    static constexpr std::size_t kFirstEngineTarget =
        static_cast<std::size_t>(VoragoMacroTarget::SubToneLevelOffsetDb);
    static constexpr std::size_t kFirstCavernTarget =
        static_cast<std::size_t>(VoragoMacroTarget::CavernSize);
    /// VoragoCavernTargets' seven fields, declared in exactly this enum order.
    static constexpr std::size_t kNumCavernTargets = 7;

    // =========================================================================
    // FR-060. THE TABLE (plan S7.3, transcribed row for row; it is NORMATIVE
    // and is not re-derived here).
    // =========================================================================
    //
    // Every `base` is cited to its plan S8 row - the value VoragoVoice::prepare()
    // step 5 (S8.2) or VoragoEngine::prepare() (S8.3) actually installs - except
    // the Cavern rows, whose bases are the VoragoCavernTargets literals above.
    // Every `amount` is SIGNED so the row's direction is readable at the literal.
    //
    // THREE CORRECTIONS ARE ALREADY APPLIED IN PLAN S7.3 AND MUST SURVIVE HERE:
    //   1. Fog -> GhostPeakLevel and Mass -> SubTrackingAmount are ENGINE-owned,
    //      not Voice-owned. Both targets sit in the Engine block, so a Voice
    //      owner fails everyRowOwnerIsValid() at COMPILE time and, taken at face
    //      value instead, would route both writes to VoragoVoice forwarders that
    //      do not exist.
    //   2. Mass -> BodyBlend IS DELETED. It moved the blend from 0.35 toward
    //      body B (SteelTank) while SC-008's Mass metric integrates body A's
    //      (StoneChamber's) first eight mode bands - so the row ATTENUATED the
    //      exact band the criterion measures and opposed the BodyResonance row
    //      on the same macro. Mass is not one of FR-068's six normative
    //      mappings, so nothing required it to touch the blend; blend travel is
    //      left to Weight alone. The >= 4 dB endpoint threshold is UNCHANGED.
    //   3. NoiseLevelDb, BreathingDepth and TidalDepth each get a REAL,
    //      DIRECTIONAL row (on Density, Movement and Fog respectively), not an
    //      amount = 0 claim row: all three are parameters a macro has an honest
    //      reason to move. Without them everyTargetIsClaimed() fails.
    static constexpr std::array<VoragoMacroRow, kNumRows> kRows = {{
        // ---------------------------------------------------------------------
        // DARKNESS - spectral tilt down, the space absorbs HF, the smear leans low.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Darkness,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudSpectralTiltDb,
         .base = -4.0f,  // S8.2 cloud_.setSpectralTiltDb
         .amount = -6.0f,
         .curve = ModCurve::Linear},  // tilt darkens (centroid down, SC-008 row 1)
        {.macro = VoragoMacro::Darkness,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernDarkness,
         .base = 0.80f,  // VoragoCavernTargets::darkness
         .amount = 0.20f,
         .curve = ModCurve::SCurve},  // the space absorbs HF
        {.macro = VoragoMacro::Darkness,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SmearTilt,
         .base = 0.0f,  // S8.3 smear_.setSmearTilt
         .amount = -0.5f,
         .curve = ModCurve::Linear},  // the smear leans low

        // ---------------------------------------------------------------------
        // AGE - carries the SHORTENING half of the folded `Decay` concept
        //       (FR-068), plus HF loss.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Age,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BodyDamping,
         .base = 0.25f,  // S8.2 bodies_[*].setDamping
         .amount = 0.55f,
         .curve = ModCurve::Linear},  // carries `Decay`'s shortening half
        {.macro = VoragoMacro::Age,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernDecaySeconds,
         .base = 20.0f,  // VoragoCavernTargets::decaySeconds
         .amount = -14.0f,
         .curve = ModCurve::Linear},  // `Decay` shortening half (SC-008 fold clause)
        {.macro = VoragoMacro::Age,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudSpectralTiltDb,
         .base = -4.0f,  // shares Darkness's target and base
         .amount = -4.0f,
         .curve = ModCurve::Linear},  // SC-008 row 2: > 4 kHz energy down >= 3 dB

        // ---------------------------------------------------------------------
        // DENSITY - more partials, more awake noise sources, a thicker bed,
        //           more surviving bloom children.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Density,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudRichness,
         .base = 0.70f,  // S8.2 cloud_.setRichness
         .amount = 0.28f,
         .curve = ModCurve::Linear},  // active partial count up (-> 0.98)
        {.macro = VoragoMacro::Density,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::NoiseWakeBase,
         .base = 0.35f,  // S8.2 noise wake base (voice-side)
         .amount = 0.65f,
         .curve = ModCurve::SCurve},  // awake noise-source count up (-> 1.0)
        {.macro = VoragoMacro::Density,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::NoiseLevelDb,
         .base = -18.0f,  // S8.2 noise_.setSourceLevel(all)
         .amount = 6.0f,
         .curve = ModCurve::Linear},  // the bed thickens with the wake
        {.macro = VoragoMacro::Density,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BloomDepth,
         .base = 0.60f,  // S8.2 bloom_.setDepth
         .amount = 0.40f,
         .curve = ModCurve::Linear},  // more children survive

        // ---------------------------------------------------------------------
        // MOVEMENT - everything that wanders, wanders further and faster.
        //
        // RETUNED AGAINST SC-008 (AR-6). The first amounts (22 / 0.12 / 0.12 /
        // 0.40) measured +6.5 % on the per-band total-variation metric against
        // the >= 20 % endpoint: the voice's output is a near-sinusoid (the
        // bodies leave the harmonics 35 dB down), so a 30-cent drift and a
        // 0.15 Hz wander barely move any band. Each row now runs to its
        // component's ceiling at Movement = 1 - drift to HarmonicCloud::
        // kMaxDriftCents (50), both wander rates to their 1 Hz clamps, breath
        // to 1.0 - which measured +24 % over the three SC-008 seeds.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Movement,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudDriftDepthCents,
         .base = 8.0f,  // S8.2 cloud_.setDriftDepthCents
         .amount = 42.0f,
         .curve = ModCurve::Linear},  // per-band total variation up (-> kMaxDriftCents 50)
        {.macro = VoragoMacro::Movement,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::ResonanceWanderRate,
         .base = 0.03f,  // S8.2 resonance_.setWanderRate
         .amount = 0.97f,
         .curve = ModCurve::Exponential},  // peaks wander faster (-> kMaxWanderRateHz 1.0)
        {.macro = VoragoMacro::Movement,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::NoiseWanderRate,
         .base = 0.03f,  // S8.2 noise_.setWanderRate
         .amount = 0.97f,
         .curve = ModCurve::Exponential},  // filters wander faster (in step with the peaks)
        {.macro = VoragoMacro::Movement,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BreathingDepth,
         .base = 0.30f,  // S8.2 breath_.setDepth
         .amount = 0.70f,
         .curve = ModCurve::Linear},  // FR-026's gravity lane swings to full depth
        {.macro = VoragoMacro::Movement,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernDamperDepth,
         .base = 0.35f,  // VoragoCavernTargets::damperDepth
         .amount = 0.45f,
         .curve = ModCurve::Linear},  // the dampers move

        // ---------------------------------------------------------------------
        // GRAVITY - the ONE BIPOLAR macro (FR-064a, Q4). 0 = air, 0.5 = neutral,
        //   1 = stone. The row is driven by g = (gravity - 0.5) * 2 as
        //   amount * curve(|g|) * sign(g), so at 0.5 it contributes exactly 0
        //   and it travels in BOTH directions from its S8.2 base.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Gravity,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::ResonanceGravity,
         .base = 0.0f,  // S8.2 resonance_.setGravity, AnchorMode::Hybrid (FR-016)
         .amount = 1.0f,
         .curve = ModCurve::Linear},  // air -1.0 <- 0.0 -> +1.0 stone
        // FR-060 (Phase 12, spec Q9 / B-1, R-1 path B): no admissible row on the
        // 39 Phase 10 targets reached the 30 % endpoint (probe best 0.2024), so
        // Gravity gains the octave-lock target. Bipolar contribution: +1 at stone
        // (every keyed anchor on an octave of the note), 0 at neutral, -1 at air,
        // which the setter's [0, 1] clamp makes inert. Base 0 == shipped ratios.
        {.macro = VoragoMacro::Gravity,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::ResonanceOctaveLock,
         .base = 0.0f,  // ResonanceDriftNetwork octaveLock_ default (applyDefaults)
         .amount = 1.0f,
         .curve = ModCurve::Linear},  // stone locks the keyed anchors to octaves

        // ---------------------------------------------------------------------
        // ENTROPY - carries the folded `Instability` concept (FR-068).
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Entropy,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudMutation,
         .base = 0.15f,  // S8.2 cloud_.setMutation
         .amount = 0.55f,
         .curve = ModCurve::Linear},  // carries `Instability`
        {.macro = VoragoMacro::Entropy,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudInharmonicity,
         .base = 0.015f,  // S8.2 cloud_.setInharmonicity
         .amount = 0.055f,
         .curve = ModCurve::Linear},  // spectral flatness up
        {.macro = VoragoMacro::Entropy,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SmearDecoherence,
         .base = 0.20f,  // S8.3 smear_.setDecoherence
         .amount = 0.60f,
         .curve = ModCurve::SCurve},  // phase decoherence up
        {.macro = VoragoMacro::Entropy,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BreathingIrregularity,
         .base = 0.30f,  // S8.2 breath_.setIrregularity
         .amount = 0.60f,
         .curve = ModCurve::Linear},  // `Instability`: life-mod depth up
        {.macro = VoragoMacro::Entropy,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudDriftDepthCents,
         .base = 8.0f,  // shares Movement's target and base
         .amount = 12.0f,
         .curve = ModCurve::Linear},  // `Instability`

        // ---------------------------------------------------------------------
        // PRESSURE - the ecology and the glue close in; crest factor down.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Pressure,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::EcologyMix,
         .base = 0.15f,  // S8.2 ecology_.setMix (kDefaultMix)
         .amount = 0.35f,
         .curve = ModCurve::SCurve},  // crest factor down
        {.macro = VoragoMacro::Pressure,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::EcologyLoopGain,
         .base = 0.72f,  // S8.2 ecology_.setLoopGain(all) (kDefaultLoopGain)
         .amount = 0.16f,
         .curve = ModCurve::Linear},  // ceiling is kMaxLoopGain 0.90
        {.macro = VoragoMacro::Pressure,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::OutputSaturation,
         .base = 0.12f,  // S8.3 satL_/satR_.setSaturation (kOutputSaturation)
         .amount = 0.88f,  // Phase 12 P-a retune (spec B-2): 0.35 -> 0.88
         .curve = ModCurve::Linear},  // the glue closes
        // FR-060 (Phase 12, spec Q9 / B-2 / B-7, R-1 path B): no admissible set on
        // the Phase 10 targets reached 3 dB (probe best P-a+P-b 1.5030 dB), so
        // Pressure gains the makeup-COMPENSATED output drive. Base 0 dB ==
        // kOutputDriveDb (default-inert). The drive ALONE tops out at 2.60 dB at
        // the saturator's +24 dB ceiling (spec Q10: 6/12/18/24 dB -> 1.17 / 2.02
        // / 2.49 / 2.60), because the crest floor is the sub-tone beating - so
        // B-7 pairs it with the sub-level row below. +18 dB is the smallest
        // drive amount whose pair passes (3.5217 dB).
        {.macro = VoragoMacro::Pressure,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::OutputDriveDb,
         .base = 0.0f,  // VoragoEngine::kOutputDriveDb
         .amount = 18.0f,
         .curve = ModCurve::Linear},  // crest factor down, loudness held
        // B-7: the sub tones ARE the crest floor at C1, so Pressure lowers them
        // directly. Shares Weight's and Mass's target and base (0.0 dB);
        // contributes 0 at Pressure = 0 (Linear). -12 dB is the smallest amount
        // that passes with +18 dB drive (spec Q10: 3.5217 dB >= 3 dB, rho -1).
        {.macro = VoragoMacro::Pressure,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SubToneLevelOffsetDb,
         .base = 0.0f,
         .amount = -12.0f,
         .curve = ModCurve::Linear},  // the floor itself comes down

        // ---------------------------------------------------------------------
        // WEIGHT - FR-068 normative (roadmap line 465): sub levels up, the blend
        //          moves toward the heavier body, damping up, tilt darkens.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Weight,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SubToneLevelOffsetDb,
         .base = 0.0f,  // S8.3 sub_ shared offset
         .amount = 9.0f,
         .curve = ModCurve::Linear},  // roadmap 465: sub levels up
        {.macro = VoragoMacro::Weight,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BodyBlend,
         .base = 0.35f,  // S8.2 setBodyBlend
         .amount = 0.45f,
         .curve = ModCurve::SCurve},  // roadmap 465: toward the heavier body (Q1)
        {.macro = VoragoMacro::Weight,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BodyDamping,
         .base = 0.25f,  // shares Age's target and base
         .amount = 0.25f,
         .curve = ModCurve::Linear},  // roadmap 465
        {.macro = VoragoMacro::Weight,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudSpectralTiltDb,
         .base = -4.0f,  // shares Darkness's target and base
         .amount = -4.0f,
         .curve = ModCurve::Linear},  // roadmap 465: tilt darkening

        // ---------------------------------------------------------------------
        // FOG - FR-068 normative (roadmap line 467) AND the folded `Distance`
        //       concept, which is why the macro reaches both the engine's smear
        //       and the cavern's distance filtering.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SmearAmount,
         .base = 0.20f,  // S8.3 smear_.setSmearAmount
         .amount = 0.70f,
         .curve = ModCurve::SCurve},  // roadmap 467: smear up (-> 0.90)
        // CORRECTION 1 (plan S7.3): ENGINE-owned. GhostPeakLevel sits in the
        // Engine block, and a Voice owner here fails everyRowOwnerIsValid().
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::GhostPeakLevel,
         .base = 0.60f,  // S8.3 kGhostBurstPeak
         .amount = 0.40f,  // 0.60 overshot setGhostPeakLevel's clamp of 1.0 from Fog 0.67 up
         .curve = ModCurve::Linear},  // roadmap 467: ghost mix up (-> 1.0, the ceiling)
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::AtmosBlur,
         .base = 0.85f,  // S8.3 atmos_.setBlur (FR-017)
         .amount = 0.15f,
         .curve = ModCurve::Linear},  // blur to the ceiling
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernFog,
         .base = 0.30f,  // VoragoCavernTargets::fog
         .amount = 0.55f,
         .curve = ModCurve::SCurve},  // `Distance`: distance filtering up (Q1)
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernDarkness,
         .base = 0.80f,  // shares Darkness's target and base
         .amount = 0.15f,
         .curve = ModCurve::Linear},  // `Distance`
        // A CLAIM, NOT A MOVEMENT (2026-09-19 ruling on SC-008's Fog row). The
        // voice-side `Distance` tilt (-3 dB/oct) opposed the row's amended
        // metric outright: Fog is measured as spectral flatness over the
        // harmonic band, which the smear and the ghost RAISE (blur) and which a
        // steeper tilt COLLAPSES (measured -61 % alone, against +19 % for the
        // ghost). `Distance` keeps its cavern-side rows (CavernFog,
        // CavernDarkness) and the tilt target stays claimed here at 0.
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::CloudSpectralTiltDb,
         .base = -4.0f,  // shares Darkness's target and base
         .amount = 0.0f,
         .curve = ModCurve::Linear},
        {.macro = VoragoMacro::Fog,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::TidalDepth,
         .base = 0.40f,  // S8.2 tide_.setDepth
         .amount = 0.40f,
         .curve = ModCurve::Linear},  // FR-026's fog lane rolls in harder

        // ---------------------------------------------------------------------
        // LIFE - FR-068 normative (roadmap line 466): the ecosystem gets louder,
        //        events come sooner, blooms spawn more often.
        // ---------------------------------------------------------------------
        // Ruled 2026-09-19 (spec Q-J): the shipped routing depth rose 0.50 ->
        // 0.85 to lift SC-005's centroid CV; the ENDPOINT is kept where it was
        // (0.50 + 0.50 = 0.85 + 0.15 = 1.0), so the amount shrinks to 0.15.
        {.macro = VoragoMacro::Life,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::EcosystemDepth,
         .base = 0.85f,  // S8.2 ecosystem_ depth (voice-side; was 0.50f)
         .amount = 0.15f,  // was 0.50f
         .curve = ModCurve::Linear},  // roadmap 466: ecosystem activity up (-> 1.0)
        // SIGN: POSITIVE. VoragoVoice::applyEventRateScale() DIVIDES both
        // interval ranges by the scale (vorago_voice.h:1831-1839), so a LARGER
        // scale is a SHORTER interval and a higher event rate. The first draft
        // carried -0.65 here, which lengthened the fast range to 57-257 s at
        // Life = 1 and read 0 edges on SC-008's Life row. 9.0 runs the scale to
        // setEventRateScale()'s ceiling of 10 (:1276), i.e. 2-9 s at Life = 1.
        {.macro = VoragoMacro::Life,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::EventRateScale,
         .base = 1.0f,  // S8.2 setEventRateScale identity
         .amount = 9.0f,
         .curve = ModCurve::Linear},  // roadmap 466: shorter intervals = rate up
        {.macro = VoragoMacro::Life,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BloomSpawnRateHz,
         // SC-009 clause 1 compares this against the value the voice READS BACK,
         // so it must be the shipped constant itself, not a 4-decimal rounding
         // of it: prepare() writes BloomEngine::kDefaultSpawnRateHz
         // (bloom_engine.h:281 == 1.0f/240.0f == 0.00416667f, vorago_voice.h:595)
         // and getBloomSpawnRateHz() reports exactly that.
         .base = BloomEngine::kDefaultSpawnRateHz,  // S8.2 bloom_.setSpawnRateHz (1/240)
         .amount = 0.0208f,
         .curve = ModCurve::Exponential},  // roadmap 466: ceiling kMaxSpawnRateHz 0.05

        // ---------------------------------------------------------------------
        // DEPTH - carries the LENGTHENING half of the folded `Decay` concept
        //         (FR-068) and grows the space.
        // ---------------------------------------------------------------------
        // A CLAIM, NOT A MOVEMENT. CavernMix's base is already the component's
        // clamp maximum (fully wet), so the useful travel is zero; the row exists
        // because every enumerated target must be claimed (everyTargetIsClaimed)
        // and every Cavern enumerator needs a 1:1 POD field (FR-063). SC-008's
        // Depth metric is amended to match (plan A-1) and measures the composed
        // render against the same render with CavernVerb::setMix(0).
        {.macro = VoragoMacro::Depth,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernMix,
         .base = 1.00f,  // VoragoCavernTargets::mix
         .amount = 0.0f,
         .curve = ModCurve::Linear},
        {.macro = VoragoMacro::Depth,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernSize,
         .base = 0.50f,  // VoragoCavernTargets::size
         .amount = 0.45f,
         .curve = ModCurve::SCurve},  // a larger space (SC-008 fold clause)
        {.macro = VoragoMacro::Depth,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernDecaySeconds,
         .base = 20.0f,  // shares Age's target and base
         .amount = 25.0f,
         .curve = ModCurve::Linear},  // carries `Decay`'s lengthening half
        // A CLAIM, NOT A MOVEMENT - same construction as CavernMix above:
        // CavernWidth's base is already the component's clamp maximum.
        {.macro = VoragoMacro::Depth,
         .owner = VoragoMacroTargetOwner::Cavern,
         .target = VoragoMacroTarget::CavernWidth,
         .base = 1.00f,  // VoragoCavernTargets::width
         .amount = 0.0f,
         .curve = ModCurve::Linear},

        // ---------------------------------------------------------------------
        // MASS - sub-band energy up (SC-008 row 12 as ruled 2026-09-19: energy
        //        below 80 Hz at C3, the construction Weight uses). CORRECTION 2
        //        (plan S7.3): the Mass -> BodyBlend row is DELETED; see the block
        //        comment above kRows. BodyResonance still sharpens both bodies'
        //        modal peaks (the setter fans out); the ResonanceMix row below
        //        carries the measurable half of the macro and its sign is
        //        explained where it stands; SubTracking is a claim row.
        // ---------------------------------------------------------------------
        {.macro = VoragoMacro::Mass,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BodyResonance,
         .base = 0.70f,  // S8.2 bodies_[*].setResonance
         .amount = 0.28f,
         .curve = ModCurve::Linear},  // modal-band energy up (SC-008 row 12)
        // A CLAIM, NOT A MOVEMENT. BodyMix's base is already 1.00 (fully wet:
        // the excitation reaches the output only through the resonators, S8.2),
        // so there is no useful travel; the row keeps the target claimed.
        {.macro = VoragoMacro::Mass,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::BodyMix,
         .base = 1.00f,  // S8.2 bodies_[*].setMix
         .amount = 0.0f,
         .curve = ModCurve::Linear},
        // SIGN BELOW (2026-09-19 ruling on SC-008's Mass row). Mass is now
        // measured as energy below 80 Hz at C3 - the sub band - and at C3 the
        // voice bus sits UNDER SubharmonicEngine's -18 dB tracking reference,
        // so the tracked sub level follows the bus and a row that thins the
        // bus LOWERS the subs (measured -1.2 dB at Mass = 1 with this row
        // positive). Mass therefore takes the network's wet share DOWN, which
        // leaves more of the bus for the subs to track.
        {.macro = VoragoMacro::Mass,
         .owner = VoragoMacroTargetOwner::Voice,
         .target = VoragoMacroTarget::ResonanceMix,
         .base = 0.45f,  // S8.2 resonance_.setMix
         .amount = -0.30f,
         .curve = ModCurve::SCurve},  // the network carries less of the bus (-> 0.15)
        // A CLAIM, NOT A MOVEMENT (2026-09-19 ruling). The Mass -> SubTracking
        // row is DROPPED: the shipped tracking default became 1.0 (fully
        // tracked) because a static (1 - a) sub floor never went silent after
        // note-off, and a row that lowered it would bring that floor back. The
        // target stays claimed here at 0 (everyTargetIsClaimed).
        // CORRECTION 1 (plan S7.3): ENGINE-owned. SubTrackingAmount sits in the
        // Engine block; base 1.0 matches S8.3's engine default.
        {.macro = VoragoMacro::Mass,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SubTrackingAmount,
         .base = 1.0f,  // S8.3 sub_.setTrackingAmount (kDefaultSubTracking, ruled 2026-09-19)
         .amount = 0.0f,
         .curve = ModCurve::Linear},
        // FR-060 (Phase 12, OQ-2 ruled (b)): the sub band is Mass's metric (Q-Q), so Mass raises it directly.
        // Shares Weight's target and base (0.0 dB). Contributes 0 at Mass = 0 (Linear).
        // Amount +3.0 dB: the smallest candidate the T004 probe measured PASS
        // (specs/vorago-phase12-parameters/artifacts/fr060_probe.log: rho 1.0, endpoint 2.4343 dB).
        {.macro = VoragoMacro::Mass,
         .owner = VoragoMacroTargetOwner::Engine,
         .target = VoragoMacroTarget::SubToneLevelOffsetDb,
         .base = 0.0f,  // == Weight's base (everyRowSharesOneBasePerTarget)
         .amount = 3.0f,
         .curve = ModCurve::Linear},
    }};

    // =========================================================================
    // The six compile-time predicates (plan S7.4)
    // =========================================================================

    /// The owner a target's BLOCK implies. The enum's three blocks are the
    /// definition of ownership, so this function is the whole of it.
    [[nodiscard]] static constexpr VoragoMacroTargetOwner ownerOfTarget(
        VoragoMacroTarget t) noexcept {
        const auto i = static_cast<std::size_t>(t);
        if (i < kFirstEngineTarget) {
            return VoragoMacroTargetOwner::Voice;
        }
        if (i < kFirstCavernTarget) {
            return VoragoMacroTargetOwner::Engine;
        }
        return VoragoMacroTargetOwner::Cavern;
    }

    /// The VoragoCavernTargets field index for a Cavern-owned target, or -1.
    /// The POD's fields are declared in enum order, so this is a pure offset -
    /// which is what keeps computeCavernTargets() and the predicate below from
    /// ever disagreeing.
    [[nodiscard]] static constexpr int cavernFieldIndex(VoragoMacroTarget t) noexcept {
        const auto i = static_cast<std::size_t>(t);
        if (i < kFirstCavernTarget || i >= (kFirstCavernTarget + kNumCavernTargets)) {
            return -1;
        }
        return static_cast<int>(i - kFirstCavernTarget);
    }

    /// FR-060: no row may be unreachable. A row's `owner` must agree with the
    /// BLOCK its `target` sits in, in both directions - otherwise apply() and
    /// computeCavernTargets() would disagree about who writes it, or the row
    /// would name a forwarder that does not exist on the owner it claims.
    [[nodiscard]] static constexpr bool everyRowOwnerIsValid(
        const std::array<VoragoMacroRow, kNumRows>& rows) noexcept {
        for (const VoragoMacroRow& row : rows) {
            if (static_cast<std::size_t>(row.macro) >= kNumMacros) {
                return false;
            }
            if (static_cast<std::size_t>(row.target) >= kNumTargets) {
                return false;
            }
            if (row.owner != ownerOfTarget(row.target)) {
                return false;
            }
        }
        return true;
    }

    /// FR-063: a Cavern row absent from VoragoCavernTargets is a compile error.
    [[nodiscard]] static constexpr bool everyCavernRowHasAPodField(
        const std::array<VoragoMacroRow, kNumRows>& rows) noexcept {
        for (const VoragoMacroRow& row : rows) {
            if (row.owner == VoragoMacroTargetOwner::Cavern && cavernFieldIndex(row.target) < 0) {
                return false;
            }
        }
        return true;
    }

    /// FR-065. ModCurve::Stepped is std::floor(x*4)/3
    /// (core/modulation_curves.h:53-54) - 18 zero-change steps and 3 jumps of
    /// ~1/3 over a 21-step sweep, which breaks SC-010's continuity bound BY
    /// CONSTRUCTION. Linear | Exponential | SCurve only.
    [[nodiscard]] static constexpr bool noRowUsesSteppedCurve(
        const std::array<VoragoMacroRow, kNumRows>& rows) noexcept {
        for (const VoragoMacroRow& row : rows) {
            if (row.curve == ModCurve::Stepped) {
                return false;
            }
        }
        return true;
    }

    /// FR-068: every enumerated target is claimed by at least one row. The enum
    /// IS the union of the twelve mappings, so "no target is orphaned" and "no
    /// mapping lost a row" are the same statement - this is the predicate that
    /// catches the fold that vanishes in the build.
    [[nodiscard]] static constexpr bool everyTargetIsClaimed(
        const std::array<VoragoMacroRow, kNumRows>& rows) noexcept {
        for (std::size_t t = 0; t < kNumTargets; ++t) {
            bool found = false;
            for (const VoragoMacroRow& row : rows) {
                if (static_cast<std::size_t>(row.target) == t) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    /// FR-064: evaluateAll() seeds `acc = base(t)` ONCE per target, so two rows
    /// on one target that disagree about `base` would silently make the result
    /// depend on table order. Five targets are hit by more than one macro
    /// (CloudSpectralTiltDb, BodyDamping, CloudDriftDepthCents, CavernDarkness,
    /// CavernDecaySeconds) and that is specified, not accidental. This predicate
    /// is also what catches a plan S8 default and a kRows literal drifting apart
    /// at COMPILE time, before SC-009 catches it at run time.
    [[nodiscard]] static constexpr bool everyRowSharesOneBasePerTarget(
        const std::array<VoragoMacroRow, kNumRows>& rows) noexcept {
        for (std::size_t a = 0; a < rows.size(); ++a) {
            for (std::size_t b = a + 1; b < rows.size(); ++b) {
                if (rows[a].target == rows[b].target && rows[a].base != rows[b].base) {
                    return false;
                }
            }
        }
        return true;
    }

    // =========================================================================
    // Knobs (FR-061, FR-069)
    // =========================================================================

    /// FR-061's documented neutral for one macro: 0.5 for the bipolar Gravity,
    /// 0 for the other eleven - and 0 for anything that is not an enumerator,
    /// so an out-of-range read is a defined, inert answer rather than garbage.
    [[nodiscard]] static constexpr float neutralFor(VoragoMacro macro) noexcept {
        return (macro == VoragoMacro::Gravity) ? 0.5f : 0.0f;
    }

    /// @brief FR-069. Clamped to [0, 1]; `Count` and out-of-range are SILENT
    ///        no-ops (reject-never-crash).
    ///
    /// A NON-FINITE ARGUMENT IS REJECTED AND THE PREVIOUS VALUE STANDS - the
    /// uniform rule FR-069 and FR-071 both state, and the one every other Vorago
    /// setter already follows (vorago_voice.h:1272-1277 setEcosystemDepth,
    /// :1283-1288 setEventRateScale). It is a REJECTION, not a repair: an
    /// earlier draft restored that macro's neutral, which silently moved a
    /// value the caller never asked to move. Nothing is poisoned either way -
    /// the NaN never reaches `values_`, so no row can propagate it into a
    /// target on the next apply().
    void setMacro(VoragoMacro macro, float value) noexcept {
        if (!isFiniteBits(value)) {
            return;  // FR-069 / FR-071: rejected, the previous value stands
        }
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        switch (macro) {
            case VoragoMacro::Darkness:
                values_.darkness = clamped;
                break;
            case VoragoMacro::Age:
                values_.age = clamped;
                break;
            case VoragoMacro::Density:
                values_.density = clamped;
                break;
            case VoragoMacro::Movement:
                values_.movement = clamped;
                break;
            case VoragoMacro::Gravity:
                values_.gravity = clamped;
                break;
            case VoragoMacro::Entropy:
                values_.entropy = clamped;
                break;
            case VoragoMacro::Pressure:
                values_.pressure = clamped;
                break;
            case VoragoMacro::Weight:
                values_.weight = clamped;
                break;
            case VoragoMacro::Fog:
                values_.fog = clamped;
                break;
            case VoragoMacro::Life:
                values_.life = clamped;
                break;
            case VoragoMacro::Depth:
                values_.depth = clamped;
                break;
            case VoragoMacro::Mass:
                values_.mass = clamped;
                break;
            case VoragoMacro::Count:
            default:
                break;
        }
    }

    /// @brief FR-069. An out-of-range enumerator returns THAT macro's neutral.
    [[nodiscard]] float getMacro(VoragoMacro macro) const noexcept {
        switch (macro) {
            case VoragoMacro::Darkness:
                return values_.darkness;
            case VoragoMacro::Age:
                return values_.age;
            case VoragoMacro::Density:
                return values_.density;
            case VoragoMacro::Movement:
                return values_.movement;
            case VoragoMacro::Gravity:
                return values_.gravity;
            case VoragoMacro::Entropy:
                return values_.entropy;
            case VoragoMacro::Pressure:
                return values_.pressure;
            case VoragoMacro::Weight:
                return values_.weight;
            case VoragoMacro::Fog:
                return values_.fog;
            case VoragoMacro::Life:
                return values_.life;
            case VoragoMacro::Depth:
                return values_.depth;
            case VoragoMacro::Mass:
                return values_.mass;
            case VoragoMacro::Count:
            default:
                return neutralFor(macro);
        }
    }

    /// @brief Bulk set - one call per knob, so a bulk write CANNOT bypass
    ///        setMacro()'s sanitising clamp (FR-069, SC-028).
    void setMacros(const VoragoMacroValues& v) noexcept {
        setMacro(VoragoMacro::Darkness, v.darkness);
        setMacro(VoragoMacro::Age, v.age);
        setMacro(VoragoMacro::Density, v.density);
        setMacro(VoragoMacro::Movement, v.movement);
        setMacro(VoragoMacro::Gravity, v.gravity);
        setMacro(VoragoMacro::Entropy, v.entropy);
        setMacro(VoragoMacro::Pressure, v.pressure);
        setMacro(VoragoMacro::Weight, v.weight);
        setMacro(VoragoMacro::Fog, v.fog);
        setMacro(VoragoMacro::Life, v.life);
        setMacro(VoragoMacro::Depth, v.depth);
        setMacro(VoragoMacro::Mass, v.mass);
    }

    [[nodiscard]] VoragoMacroValues getMacros() const noexcept { return values_; }

    // =========================================================================
    // Per-target base override (Phase 12 FR-001 / FR-002)
    // =========================================================================

    /// @brief FR-001. Replaces @p target's seeded base with @p base.
    ///
    /// An out-of-range target (>= kNumTargets, including `Count`) or a
    /// non-finite base is a SILENT NO-OP - the previous base stands
    /// (isFiniteBits, never std::isnan). NOT clamped here: the destination
    /// setter clamps the summed value.
    void setTargetBase(VoragoMacroTarget target, float base) noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets || !isFiniteBits(base)) {
            return;
        }
        baseOverride_[i] = base;
        hasOverride_[i] = true;
    }

    /// @brief FR-001. Drops every override; every target seeds from kRows again.
    void resetTargetBases() noexcept {
        hasOverride_.fill(false);
        baseOverride_.fill(0.0f);
    }

    /// @brief FR-001. The override if one is set, else the kRows literal; 0 for
    ///        an out-of-range target.
    [[nodiscard]] float getTargetBase(VoragoMacroTarget target) const noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets) {
            return 0.0f;
        }
        return hasOverride_[i] ? baseOverride_[i] : literalBaseFor(target);
    }

    // =========================================================================
    // AR-1's two application surfaces (FR-062)
    // =========================================================================

    /// @brief FR-062. Pushes the Voice- and Engine-owned rows through the engine.
    ///
    /// Reaches VoragoEngine::voices_ directly via `friend class VoragoMacroMatrix`
    /// (vorago_engine.h:1006), so FR-085's getVoice() can stay const for tests.
    /// Iterates `i < getPolyphony()`: slots above the current polyphony are
    /// neither summed nor allocatable, so writing them would be work with no
    /// observable and would make the cost depend on kMaxVoices rather than N.
    ///
    /// AN UNPREPARED ENGINE IS WRITTEN NOTHING (SC-023). prepare() is the only
    /// allocating path on the voice and the engine, and every value here is a
    /// prepare-time default the caller has not installed yet; half-writing a
    /// pool that has not been sized is a defect, not a convenience.
    ///
    /// CAVERN ROWS ARE NEVER WRITTEN HERE (AR-1) - this header may not name a
    /// Layer 4 type. They are returned by computeCavernTargets() instead.
    ///
    /// Each value goes through the owning setter, which does its OWN clamping:
    /// summation first, clamp at the destination.
    void apply(VoragoEngine& engine) const noexcept {
        if (!engine.isPrepared()) {
            return;
        }
        const std::array<float, kNumTargets> v = evaluateAll();

        // -- Engine-owned ----------------------------------------------------
        engine.setSubToneLevelOffsetDb(at(v, VoragoMacroTarget::SubToneLevelOffsetDb));
        engine.setSubTrackingAmount(at(v, VoragoMacroTarget::SubTrackingAmount));
        engine.setSmearAmount(at(v, VoragoMacroTarget::SmearAmount));
        engine.setSmearDecoherence(at(v, VoragoMacroTarget::SmearDecoherence));
        engine.setSmearTilt(at(v, VoragoMacroTarget::SmearTilt));
        engine.setGhostPeakLevel(at(v, VoragoMacroTarget::GhostPeakLevel));
        engine.setAtmosBlur(at(v, VoragoMacroTarget::AtmosBlur));
        engine.setOutputSaturation(at(v, VoragoMacroTarget::OutputSaturation));
        engine.setOutputDriveDb(at(v, VoragoMacroTarget::OutputDriveDb));

        // -- Voice-owned -----------------------------------------------------
        const std::size_t voiceCount = engine.getPolyphony();
        for (std::size_t i = 0; i < voiceCount; ++i) {
            VoragoVoice& voice = engine.voices_[i];

            // Harmonic cloud
            voice.setRichness(at(v, VoragoMacroTarget::CloudRichness));
            voice.setSpectralTiltDb(at(v, VoragoMacroTarget::CloudSpectralTiltDb));
            voice.setMutation(at(v, VoragoMacroTarget::CloudMutation));
            voice.setInharmonicity(at(v, VoragoMacroTarget::CloudInharmonicity));
            voice.setDriftDepthCents(at(v, VoragoMacroTarget::CloudDriftDepthCents));

            // Noise organism
            voice.setNoiseLevelDb(at(v, VoragoMacroTarget::NoiseLevelDb));
            voice.setNoiseWakeBase(at(v, VoragoMacroTarget::NoiseWakeBase));
            voice.setNoiseWanderRate(at(v, VoragoMacroTarget::NoiseWanderRate));

            // Resonant network
            voice.setResonanceGravity(at(v, VoragoMacroTarget::ResonanceGravity));
            voice.setResonanceMix(at(v, VoragoMacroTarget::ResonanceMix));
            voice.setResonanceWanderRate(at(v, VoragoMacroTarget::ResonanceWanderRate));

            // Feedback ecology
            voice.setEcologyMix(at(v, VoragoMacroTarget::EcologyMix));
            voice.setEcologyLoopGain(at(v, VoragoMacroTarget::EcologyLoopGain));

            // The two bodies
            voice.setBodyBlend(at(v, VoragoMacroTarget::BodyBlend));
            voice.setBodyDamping(at(v, VoragoMacroTarget::BodyDamping));
            voice.setBodyResonance(at(v, VoragoMacroTarget::BodyResonance));
            voice.setBodyMix(at(v, VoragoMacroTarget::BodyMix));

            // Identity layer
            voice.setEcosystemDepth(at(v, VoragoMacroTarget::EcosystemDepth));
            voice.setEventRateScale(at(v, VoragoMacroTarget::EventRateScale));
            voice.setBloomDepth(at(v, VoragoMacroTarget::BloomDepth));
            voice.setBloomSpawnRateHz(at(v, VoragoMacroTarget::BloomSpawnRateHz));

            // Life modulators
            voice.setBreathingDepth(at(v, VoragoMacroTarget::BreathingDepth));
            voice.setBreathingIrregularity(at(v, VoragoMacroTarget::BreathingIrregularity));
            voice.setTidalDepth(at(v, VoragoMacroTarget::TidalDepth));

            // Phase 12 (spec B-1)
            voice.setResonanceOctaveLock(at(v, VoragoMacroTarget::ResonanceOctaveLock));
        }
    }

    /// @brief FR-062 / FR-063. Pure function of the knobs and the table; writes
    ///        nothing, and names no Layer 4 type.
    ///
    /// The POD carries the RAW SUM. Range clamping for these seven belongs to
    /// the Layer-4 setter the caller pushes into, which is also the only place
    /// that knows the reverb's documented ranges.
    ///
    /// EXACT AT THE NEUTRALS: applyModCurve(c, 0) == 0 for all three permitted
    /// curves and g == 0 at Gravity = 0.5, so a default-constructed matrix
    /// returns each Cavern row's `base` bit-for-bit - which is a
    /// default-constructed VoragoCavernTargets.
    [[nodiscard]] VoragoCavernTargets computeCavernTargets() const noexcept {
        const std::array<float, kNumTargets> v = evaluateAll();
        VoragoCavernTargets out{};
        out.size = at(v, VoragoMacroTarget::CavernSize);
        out.darkness = at(v, VoragoMacroTarget::CavernDarkness);
        out.decaySeconds = at(v, VoragoMacroTarget::CavernDecaySeconds);
        out.fog = at(v, VoragoMacroTarget::CavernFog);
        out.damperDepth = at(v, VoragoMacroTarget::CavernDamperDepth);
        out.mix = at(v, VoragoMacroTarget::CavernMix);
        out.width = at(v, VoragoMacroTarget::CavernWidth);
        return out;
    }

private:
    /// -ffast-math folds std::isnan away on the macOS leg, so finiteness is a
    /// BIT-PATTERN question here. Delegates to the barrier-hardened core check:
    /// a plain local memcpy/bit-mask is foldable under newer fast-math compilers
    /// via finite-math value propagation.
    [[nodiscard]] static bool isFiniteBits(float value) noexcept { return detail::isFinite(value); }

    [[nodiscard]] static float at(const std::array<float, kNumTargets>& v,
                                  VoragoMacroTarget t) noexcept {
        return v[static_cast<std::size_t>(t)];
    }

    /// FR-064a's signed, curved contribution of one row.
    ///
    /// Gravity is the one BIPOLAR macro: its knob deviation g = (m - 0.5) * 2
    /// spans [-1, +1] and the row contributes amount * curve(|g|) * sign(g), so
    /// ONE row expresses both the air and the stone half and both halves have
    /// travel from the plan S8.2 base.
    [[nodiscard]] float contributionOf(const VoragoMacroRow& row) const noexcept {
        const float m = getMacro(row.macro);
        if (row.macro == VoragoMacro::Gravity) {
            const float g = (m - 0.5f) * 2.0f;
            const float sign = (g < 0.0f) ? -1.0f : 1.0f;
            return row.amount * applyModCurve(row.curve, std::fabs(g)) * sign;
        }
        return row.amount * applyModCurve(row.curve, m);
    }

    /// `acc = base(t)` seeded ONCE per target, then one contribution per row on t.
    ///
    /// THERE IS DELIBERATELY NO `if (neutral) return;` FAST PATH. At the FR-061
    /// neutral every term is exactly 0 - applyModCurve(c, 0) == 0 for all three
    /// permitted curves and g == 0 for Gravity - so FR-066's identity is a
    /// PROPERTY OF THE ARITHMETIC. A shortcut would let a mis-signed row hide
    /// behind it.
    [[nodiscard]] std::array<float, kNumTargets> evaluateAll() const noexcept {
        std::array<float, kNumTargets> value{};
        std::array<bool, kNumTargets> seeded{};
        for (const VoragoMacroRow& row : kRows) {
            const auto i = static_cast<std::size_t>(row.target);
            if (!seeded[i]) {
                value[i] = hasOverride_[i] ? baseOverride_[i] : row.base;
                seeded[i] = true;
            }
            value[i] += contributionOf(row);
        }
        return value;
    }

    /// The kRows base for @p t: the first row on it (everyTargetIsClaimed
    /// guarantees one exists; everyRowSharesOneBasePerTarget makes it THE base).
    [[nodiscard]] static constexpr float literalBaseFor(VoragoMacroTarget t) noexcept {
        for (const VoragoMacroRow& row : kRows) {
            if (row.target == t) {
                return row.base;
            }
        }
        return 0.0f;  // unreachable: everyTargetIsClaimed is static_assert'ed
    }

    /// FR-061: the twelve knobs, already at their documented neutrals.
    VoragoMacroValues values_{};

    /// FR-001: the per-target seed overrides; hasOverride_[i] false = kRows literal.
    std::array<float, kNumTargets> baseOverride_{};
    std::array<bool, kNumTargets> hasOverride_{};
};

// =============================================================================
// The six compile-time guards. Namespace scope, because a member-specification
// static_assert cannot call a member function whose body has not been parsed yet.
// =============================================================================

static_assert(VoragoMacroMatrix::kRows.size() == VoragoMacroMatrix::kNumRows,
              "FR-060: kNumRows must match the table");
static_assert(VoragoMacroMatrix::everyRowOwnerIsValid(VoragoMacroMatrix::kRows),
              "FR-060: every row's owner must agree with the block its target sits in");
static_assert(VoragoMacroMatrix::everyCavernRowHasAPodField(VoragoMacroMatrix::kRows),
              "FR-063: every Cavern row must have a 1:1 VoragoCavernTargets field");
static_assert(VoragoMacroMatrix::noRowUsesSteppedCurve(VoragoMacroMatrix::kRows),
              "FR-065: ModCurve::Stepped breaks SC-010's continuity bound by construction");
static_assert(VoragoMacroMatrix::everyTargetIsClaimed(VoragoMacroMatrix::kRows),
              "FR-068: every enumerated target must be claimed by at least one row");
static_assert(VoragoMacroMatrix::everyRowSharesOneBasePerTarget(VoragoMacroMatrix::kRows),
              "FR-064: rows sharing a target must agree on `base`");

// 25 Voice-owned + 9 Engine-owned + 7 Cavern-owned (Phase 10's 24 + 8 + 7 plus
// the Phase 12 spec B-1 / B-2 amendment). A thirteenth macro or a forty-second
// target is a spec amendment, not an edit.
static_assert(static_cast<std::size_t>(VoragoMacroTarget::Count) == 41,
              "FR-060: 25 Voice + 9 Engine + 7 Cavern targets");
static_assert(VoragoMacroMatrix::kNumMacros == 12, "OQ-2: the macro count is confirmed at twelve");
static_assert(VoragoMacroMatrix::kFirstCavernTarget + VoragoMacroMatrix::kNumCavernTargets
                  == VoragoMacroMatrix::kNumTargets,
              "FR-063: the Cavern block must be the LAST block, so cavernFieldIndex() is an offset");

}  // namespace DSP
}  // namespace Krate
