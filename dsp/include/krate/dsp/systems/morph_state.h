// ==============================================================================
// Layer 3: System - Profundum Phase 8 (specs/profundum-phase8-bass-morph)
// ==============================================================================
// MorphState (state aggregate, field table, validation, serialisation).
//   Serves: FR-001 - FR-007, FR-030; SC-007(a)(b), SC-019(a)(c).
//   Encodes: plan S3 (S3.1 - S3.6); C-1 (the one field-descriptor table lives here),
//            C-8 (amp-envelope defaults = ADSREnvelope member defaults), C-13 (a stream bool byte > 1 is rejected).
//   Spec:  specs/profundum-phase8-bass-morph/spec.md
//   Plan:  specs/profundum-phase8-bass-morph/plan.md
//   Tasks: specs/profundum-phase8-bass-morph/tasks.md (T002)
//
// Plain data only: no processing method and no setter of any component is called here. The peers below are
// included for their aggregates, enums and range constants.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                       // L0: detail::isFinite
#include <krate/dsp/core/note_value.h>                     // L0: NoteValue, NoteModifier
#include <krate/dsp/primitives/envelope_utils.h>           // L1: kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs
#include <krate/dsp/primitives/lfo.h>                      // L1: Waveform, kMinFrequency, kMaxFrequency
#include <krate/dsp/processors/drop_envelope.h>            // L2: DropEnvelope ranges and defaults
#include <krate/dsp/processors/spectral_shape_recipe.h>    // L2: SpectralShapeRecipe::Controls
#include <krate/dsp/processors/transient_shaper.h>         // L2: TransientShaper gain range and defaults
#include <krate/dsp/systems/anchor_oscillator.h>           // L3 peer: AnchorOscillator::Controls
#include <krate/dsp/systems/bass_character_stage.h>        // L3 peer: BassCharacterStage::Algorithm
#include <krate/dsp/systems/bass_stereo_field.h>           // L3 peer: BassStereoField::Controls
#include <krate/dsp/systems/mass_law.h>                    // L3 peer: MassLaw mass range
#include <krate/dsp/systems/profundum_core.h>              // L3 peer: ProfundumCore::ExcitationControls

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Krate::DSP {

/// FR-001: every sound-shaping control of one Profundum patch, as a plain aggregate (plan S3.1).
/// The member order is the serialised order (FR-007) and is frozen by SC-019(a)'s structured bindings.
struct MorphState {
    // ---- nested vocabulary (plan S1 C-1) ----
    enum class Law : std::uint8_t { Linear, Logarithmic, EqualPower, Stepped };
    enum class Axis : std::uint8_t { SineComplex, CleanNonlinear, MonoStereo, ShortLong, DarkBright, Anchor, Motion };
    enum class Kind : std::uint8_t { F32, Enum8, Bool };
    enum class SelectionGroup : std::uint8_t { None, Character, ImageMode, FieldMotion, Motion, Anchor };
    static constexpr std::size_t kNumSelectionGroups = 5;   // excluding None
    /// The ONE enum -> array-index / bit mapping (plan S4.2): None = 0 is not a slot. Precondition g != None.
    [[nodiscard]] static constexpr std::size_t groupIndex(SelectionGroup g) noexcept {
        return static_cast<std::size_t>(g) - 1;
    }

    /// One row of the field-descriptor table (plan S3.2). Aggregate; every row sets every member.
    struct FieldInfo {
        const char* name;             // "recipe.depth" - used by the FR-003 exclusion test
        Kind kind;
        Law law;
        Axis axis;
        SelectionGroup group;         // Stepped rows only; None otherwise
        bool anchorBlock;             // resolves at p_a (FR-050)
        float lo;                     // F32 rows; the box is closed except hiOpen
        float hi;
        bool hiOpen;                  // motion.cyclePhaseDeg only: [0, 360)
        float logOffset;              // FR-032 o; 1 ms for motion.cycleFadeInMs, else 0
        std::uint8_t maxEnum;         // Enum8 rows: last declared enumerator; Bool: 1
        std::uint8_t bytes;           // 4 (F32) or 1 (Enum8, Bool)
    };

    struct AmpEnvelopeFields {                     // FR-002 (Q4); defaults plan S1 C-8 (adsr_envelope.h:521-533)
        float attackMs = 10.0f;                    // [kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs]
        float decayMs = 50.0f;
        float sustain = 0.5f;                      // [0, 1]
        float releaseMs = 100.0f;
        float attackCurve = 0.7f;                  // [-1, +1]
        float decayCurve = 0.7f;
        float releaseCurve = 0.7f;
    };

    struct MotionConfig {                          // FR-002 MotionConfig table (Q4), exact member order
        enum class Source : std::uint8_t { Off, Env, Cycle, Drift };
        enum class Target : std::uint8_t { Mass, Depth, Body, Edge, Shift };
        enum class Polarity : std::uint8_t { Bipolar, Unipolar };
        Source source = Source::Off;
        Target target = Target::Mass;
        float amount = 0.0f;                       // [-1, 1]
        float depth = 1.0f;                        // [0, 1]
        float cycleRateHz = 1.0f;                  // [kMinFrequency, kMaxFrequency] (lfo.h:34-35)
        ::Krate::DSP::Waveform cycleShape = ::Krate::DSP::Waveform::Sine;   // lfo.h:47; no new Waveform
        bool cycleSync = false;
        NoteValue cycleNote = NoteValue::Quarter;
        NoteModifier cycleModifier = NoteModifier::None;
        float cyclePhaseDeg = 0.0f;                // [0, 360)
        bool cycleRetrigger = true;
        float cycleFadeInMs = 0.0f;                // [0, 5000], log law on (t + 1 ms)
        Polarity polarity = Polarity::Bipolar;
        float driftSmoothness = 0.5f;              // [0, 1]
        float envAttackMs = 10.0f;                 // [0.1, 10000]
        float envDecayMs = 300.0f;                 // [0.1, 10000]
    };

    SpectralShapeRecipe::Controls recipe = SpectralShapeRecipe::kDefaultControls;
    ProfundumCore::ExcitationControls excitation = ProfundumCore::kDefaultExcitation;
    float foldAmount = 0.0f;                                         // [0, 100]
    float shape = 0.0f;                                              // [0, 1]; ShapeMorph reset value (shape_morph.h:620)
    BassCharacterStage::Algorithm characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    float characterAmount = 0.0f;                                    // [0, 1]
    float characterTone = 0.5f;                                      // [0, 1]
    float dropAmountSt = DropEnvelope::kDefaultAmountSemitones;      // [0, 48]
    float dropTimeMs = DropEnvelope::kDefaultTimeMs;                 // [1, 4000]
    float dropShape = DropEnvelope::kDefaultShape;                   // [0, 1]
    float dropRebound = DropEnvelope::kDefaultRebound;               // [0, 1]
    AmpEnvelopeFields ampEnv{};
    BassStereoField::Controls field{};
    MotionConfig motion{};
    float mass = MassLaw::kNeutralMass;                              // [0, 100]
    float transientAttackDb = TransientShaper::kDefaultAttackDb;     // [-12, 12]
    float transientSustainDb = TransientShaper::kDefaultSustainDb;   // [-12, 12]
    AnchorOscillator::Controls anchor{};
    float anchorFoldFeed = 0.0f;                                     // [0, 1]
};
static_assert(std::is_trivially_copyable_v<MorphState>);
static_assert(std::is_aggregate_v<MorphState>);
static_assert(!std::is_polymorphic_v<MorphState>);
static_assert(MorphState::groupIndex(MorphState::SelectionGroup::Character) == 0
              && MorphState::groupIndex(MorphState::SelectionGroup::Motion) == 3
              && MorphState::groupIndex(MorphState::SelectionGroup::Anchor) == MorphState::kNumSelectionGroups - 1);

/// FR-006: version byte at offset 0 of the stream.
inline constexpr std::uint8_t kMorphStateFormatVersion = 1;
/// 62 leaves: 46 floats, 13 enums, 3 bools (plan S3.1).
inline constexpr std::size_t kMorphLeafFieldCount = 62;
/// 1 + 46 * 4 + 16 * 1 (plan S3.2).
inline constexpr std::size_t kMorphStateBytes = 201;

namespace detail::morph_state_io {

/// The one field-descriptor table (FR-030, plan S1 C-1, S3.2), rows in member order. Ranges come from their source
/// constants where one exists; the literals are 100 (Fold, harmonic_fold.h:166-168), 5000 (fade-in, drafted D-14),
/// 360 and the [-1, 1] / [0, 1] boxes.
inline constexpr std::array<MorphState::FieldInfo, kMorphLeafFieldCount> kFieldTable = [] {
    using K = MorphState::Kind;
    using L = MorphState::Law;
    using A = MorphState::Axis;
    using G = MorphState::SelectionGroup;
    using Alg = BassCharacterStage::Algorithm;
    using Img = BassStereoField::ImageMode;
    using Src = BassStereoField::MotionSource;
    using MSrc = MorphState::MotionConfig::Source;
    using MTgt = MorphState::MotionConfig::Target;
    using MPol = MorphState::MotionConfig::Polarity;
    using AW = AnchorOscillator::Waveform;
    using AM = AnchorOscillator::Mode;
    constexpr auto u8 = [](auto e) { return static_cast<std::uint8_t>(e); };
    constexpr float tMin = ::Krate::DSP::kMinEnvelopeTimeMs;
    constexpr float tMax = ::Krate::DSP::kMaxEnvelopeTimeMs;
    return std::array<MorphState::FieldInfo, kMorphLeafFieldCount>{{
        // ---- recipe (0 - 5) ----
        {.name = "recipe.depth", .kind = K::F32, .law = L::Linear, .axis = A::SineComplex, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "recipe.body", .kind = K::F32, .law = L::Linear, .axis = A::SineComplex, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "recipe.bodyCurvature", .kind = K::F32, .law = L::Linear, .axis = A::SineComplex, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "recipe.bodyEmphasis", .kind = K::F32, .law = L::Linear, .axis = A::SineComplex, .group = G::None,
         .anchorBlock = false, .lo = -1.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "recipe.edge", .kind = K::F32, .law = L::Linear, .axis = A::DarkBright, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "recipe.shift", .kind = K::F32, .law = L::Linear, .axis = A::DarkBright, .group = G::None,
         .anchorBlock = false, .lo = -1.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- excitation (6 - 8) ----
        {.name = "excitation.mix", .kind = K::F32, .law = L::EqualPower, .axis = A::CleanNonlinear, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "excitation.drive", .kind = K::F32, .law = L::Linear, .axis = A::CleanNonlinear, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "excitation.asymmetry", .kind = K::F32, .law = L::Linear, .axis = A::CleanNonlinear, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- fold, shape (9 - 10) ----
        {.name = "foldAmount", .kind = K::F32, .law = L::Linear, .axis = A::CleanNonlinear, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 100.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "shape", .kind = K::F32, .law = L::Linear, .axis = A::DarkBright, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- character (11 - 13) ----
        {.name = "characterAlgorithm", .kind = K::Enum8, .law = L::Stepped, .axis = A::CleanNonlinear,
         .group = G::Character, .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(Alg::Fracture), .bytes = 1},
        {.name = "characterAmount", .kind = K::F32, .law = L::Linear, .axis = A::CleanNonlinear, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "characterTone", .kind = K::F32, .law = L::Linear, .axis = A::DarkBright, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- drop (14 - 17) ----
        {.name = "dropAmountSt", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = DropEnvelope::kMaxAmountSemitones, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = 0, .bytes = 4},
        {.name = "dropTimeMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = DropEnvelope::kMinTimeMs, .hi = DropEnvelope::kMaxTimeMs, .hiOpen = false,
         .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "dropShape", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "dropRebound", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- ampEnv (18 - 24) ----
        {.name = "ampEnv.attackMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = tMin, .hi = tMax, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "ampEnv.decayMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = tMin, .hi = tMax, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "ampEnv.sustain", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "ampEnv.releaseMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = tMin, .hi = tMax, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "ampEnv.attackCurve", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = -1.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "ampEnv.decayCurve", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = -1.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "ampEnv.releaseCurve", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = -1.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- field (25 - 35) ----
        {.name = "field.mode", .kind = K::Enum8, .law = L::Stepped, .axis = A::MonoStereo, .group = G::ImageMode,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = u8(Img::Wide),
         .bytes = 1},
        {.name = "field.spread", .kind = K::F32, .law = L::Linear, .axis = A::MonoStereo, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "field.gravity", .kind = K::F32, .law = L::Linear, .axis = A::MonoStereo, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "field.crossoverHz", .kind = K::F32, .law = L::Logarithmic, .axis = A::MonoStereo, .group = G::None,
         .anchorBlock = false, .lo = BassStereoField::kMinCrossoverHz, .hi = BassStereoField::kMaxCrossoverHz,
         .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "field.curve", .kind = K::F32, .law = L::Linear, .axis = A::MonoStereo, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "field.motionDepth", .kind = K::F32, .law = L::Linear, .axis = A::MonoStereo, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "field.motionRate", .kind = K::F32, .law = L::Linear, .axis = A::MonoStereo, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "field.motionSource", .kind = K::Enum8, .law = L::Stepped, .axis = A::MonoStereo,
         .group = G::FieldMotion, .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(Src::Cycle), .bytes = 1},
        {.name = "field.cycleSync", .kind = K::Bool, .law = L::Stepped, .axis = A::MonoStereo, .group = G::FieldMotion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 1, .bytes = 1},
        {.name = "field.cycleNote", .kind = K::Enum8, .law = L::Stepped, .axis = A::MonoStereo, .group = G::FieldMotion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(NoteValue::SixtyFourth), .bytes = 1},
        {.name = "field.cycleModifier", .kind = K::Enum8, .law = L::Stepped, .axis = A::MonoStereo,
         .group = G::FieldMotion, .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(NoteModifier::Triplet), .bytes = 1},
        // ---- motion (36 - 51) ----
        {.name = "motion.source", .kind = K::Enum8, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = u8(MSrc::Drift),
         .bytes = 1},
        {.name = "motion.target", .kind = K::Enum8, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = u8(MTgt::Shift),
         .bytes = 1},
        {.name = "motion.amount", .kind = K::F32, .law = L::Linear, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = -1.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.depth", .kind = K::F32, .law = L::Linear, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.cycleRateHz", .kind = K::F32, .law = L::Logarithmic, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = ::Krate::DSP::kMinFrequency, .hi = ::Krate::DSP::kMaxFrequency, .hiOpen = false,
         .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.cycleShape", .kind = K::Enum8, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(::Krate::DSP::Waveform::SmoothRandom), .bytes = 1},
        {.name = "motion.cycleSync", .kind = K::Bool, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 1, .bytes = 1},
        {.name = "motion.cycleNote", .kind = K::Enum8, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(NoteValue::SixtyFourth), .bytes = 1},
        {.name = "motion.cycleModifier", .kind = K::Enum8, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(NoteModifier::Triplet), .bytes = 1},
        {.name = "motion.cyclePhaseDeg", .kind = K::F32, .law = L::Linear, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 360.0f, .hiOpen = true, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.cycleRetrigger", .kind = K::Bool, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 1, .bytes = 1},
        {.name = "motion.cycleFadeInMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 5000.0f, .hiOpen = false, .logOffset = 1.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.polarity", .kind = K::Enum8, .law = L::Stepped, .axis = A::Motion, .group = G::Motion,
         .anchorBlock = false, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(MPol::Unipolar), .bytes = 1},
        {.name = "motion.driftSmoothness", .kind = K::F32, .law = L::Linear, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.envAttackMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = tMin, .hi = tMax, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "motion.envDecayMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::Motion, .group = G::None,
         .anchorBlock = false, .lo = tMin, .hi = tMax, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- mass, transient gains (52 - 54) ----
        {.name = "mass", .kind = K::F32, .law = L::Linear, .axis = A::CleanNonlinear, .group = G::None,
         .anchorBlock = false, .lo = MassLaw::kMinMass, .hi = MassLaw::kMaxMass, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = 0, .bytes = 4},
        {.name = "transientAttackDb", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = TransientShaper::kMinGainDb, .hi = TransientShaper::kMaxGainDb, .hiOpen = false,
         .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "transientSustainDb", .kind = K::F32, .law = L::Linear, .axis = A::ShortLong, .group = G::None,
         .anchorBlock = false, .lo = TransientShaper::kMinGainDb, .hi = TransientShaper::kMaxGainDb, .hiOpen = false,
         .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        // ---- Anchor block (55 - 61), resolved at p_a (FR-050) ----
        {.name = "anchor.waveform", .kind = K::Enum8, .law = L::Stepped, .axis = A::Anchor, .group = G::Anchor,
         .anchorBlock = true, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
         .maxEnum = u8(AW::OctaveDownSine), .bytes = 1},
        {.name = "anchor.mode", .kind = K::Enum8, .law = L::Stepped, .axis = A::Anchor, .group = G::Anchor,
         .anchorBlock = true, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = u8(AM::Ghost),
         .bytes = 1},
        {.name = "anchor.level", .kind = K::F32, .law = L::Linear, .axis = A::Anchor, .group = G::None,
         .anchorBlock = true, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "anchor.punchBoostDb", .kind = K::F32, .law = L::Linear, .axis = A::Anchor, .group = G::None,
         .anchorBlock = true, .lo = AnchorOscillator::kMinPunchBoostDb, .hi = AnchorOscillator::kMaxPunchBoostDb,
         .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "anchor.punchTimeMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::Anchor, .group = G::None,
         .anchorBlock = true, .lo = AnchorOscillator::kMinPunchTimeMs, .hi = AnchorOscillator::kMaxPunchTimeMs,
         .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "anchor.ghostTimeMs", .kind = K::F32, .law = L::Logarithmic, .axis = A::Anchor, .group = G::None,
         .anchorBlock = true, .lo = AnchorOscillator::kMinGhostTimeMs, .hi = AnchorOscillator::kMaxGhostTimeMs,
         .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
        {.name = "anchorFoldFeed", .kind = K::F32, .law = L::EqualPower, .axis = A::Anchor, .group = G::None,
         .anchorBlock = true, .lo = 0.0f, .hi = 1.0f, .hiOpen = false, .logOffset = 0.0f, .maxEnum = 0, .bytes = 4},
    }};
}();

/// Byte offset of each leaf in the version-1 stream: offset[0] = 1, offset[i] = offset[i - 1] + bytes[i - 1].
inline constexpr std::array<std::size_t, kMorphLeafFieldCount> kLeafOffsets = [] {
    std::array<std::size_t, kMorphLeafFieldCount> offsets{};
    std::size_t at = 1;   // byte 0 is the version
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        offsets[i] = at;
        at += kFieldTable[i].bytes;
    }
    return offsets;
}();

template <std::size_t I>
using Leaf = std::integral_constant<std::size_t, I>;

/// Calls f(Leaf<i>{}, s.<leaf i>...) for i = 0..61 in table order (plan S3.3); S... are MorphState or
/// const MorphState. A template visitor keeps every access inlined (straight-line code, plan S10).
template <class F, class... S>
constexpr void forEachMorphLeaf(F&& f, S&... s) {
    f(Leaf<0>{}, s.recipe.depth...);
    f(Leaf<1>{}, s.recipe.body...);
    f(Leaf<2>{}, s.recipe.bodyCurvature...);
    f(Leaf<3>{}, s.recipe.bodyEmphasis...);
    f(Leaf<4>{}, s.recipe.edge...);
    f(Leaf<5>{}, s.recipe.shift...);
    f(Leaf<6>{}, s.excitation.mix...);
    f(Leaf<7>{}, s.excitation.drive...);
    f(Leaf<8>{}, s.excitation.asymmetry...);
    f(Leaf<9>{}, s.foldAmount...);
    f(Leaf<10>{}, s.shape...);
    f(Leaf<11>{}, s.characterAlgorithm...);
    f(Leaf<12>{}, s.characterAmount...);
    f(Leaf<13>{}, s.characterTone...);
    f(Leaf<14>{}, s.dropAmountSt...);
    f(Leaf<15>{}, s.dropTimeMs...);
    f(Leaf<16>{}, s.dropShape...);
    f(Leaf<17>{}, s.dropRebound...);
    f(Leaf<18>{}, s.ampEnv.attackMs...);
    f(Leaf<19>{}, s.ampEnv.decayMs...);
    f(Leaf<20>{}, s.ampEnv.sustain...);
    f(Leaf<21>{}, s.ampEnv.releaseMs...);
    f(Leaf<22>{}, s.ampEnv.attackCurve...);
    f(Leaf<23>{}, s.ampEnv.decayCurve...);
    f(Leaf<24>{}, s.ampEnv.releaseCurve...);
    f(Leaf<25>{}, s.field.mode...);
    f(Leaf<26>{}, s.field.spread...);
    f(Leaf<27>{}, s.field.gravity...);
    f(Leaf<28>{}, s.field.crossoverHz...);
    f(Leaf<29>{}, s.field.curve...);
    f(Leaf<30>{}, s.field.motionDepth...);
    f(Leaf<31>{}, s.field.motionRate...);
    f(Leaf<32>{}, s.field.motionSource...);
    f(Leaf<33>{}, s.field.cycleSync...);
    f(Leaf<34>{}, s.field.cycleNote...);
    f(Leaf<35>{}, s.field.cycleModifier...);
    f(Leaf<36>{}, s.motion.source...);
    f(Leaf<37>{}, s.motion.target...);
    f(Leaf<38>{}, s.motion.amount...);
    f(Leaf<39>{}, s.motion.depth...);
    f(Leaf<40>{}, s.motion.cycleRateHz...);
    f(Leaf<41>{}, s.motion.cycleShape...);
    f(Leaf<42>{}, s.motion.cycleSync...);
    f(Leaf<43>{}, s.motion.cycleNote...);
    f(Leaf<44>{}, s.motion.cycleModifier...);
    f(Leaf<45>{}, s.motion.cyclePhaseDeg...);
    f(Leaf<46>{}, s.motion.cycleRetrigger...);
    f(Leaf<47>{}, s.motion.cycleFadeInMs...);
    f(Leaf<48>{}, s.motion.polarity...);
    f(Leaf<49>{}, s.motion.driftSmoothness...);
    f(Leaf<50>{}, s.motion.envAttackMs...);
    f(Leaf<51>{}, s.motion.envDecayMs...);
    f(Leaf<52>{}, s.mass...);
    f(Leaf<53>{}, s.transientAttackDb...);
    f(Leaf<54>{}, s.transientSustainDb...);
    f(Leaf<55>{}, s.anchor.waveform...);
    f(Leaf<56>{}, s.anchor.mode...);
    f(Leaf<57>{}, s.anchor.level...);
    f(Leaf<58>{}, s.anchor.punchBoostDb...);
    f(Leaf<59>{}, s.anchor.punchTimeMs...);
    f(Leaf<60>{}, s.anchor.ghostTimeMs...);
    f(Leaf<61>{}, s.anchorFoldFeed...);
}

/// float -> F32, bool -> Bool, any one-byte enum -> Enum8.
template <class T>
[[nodiscard]] constexpr MorphState::Kind kindOf() noexcept {
    if constexpr (std::is_same_v<T, float>) {
        return MorphState::Kind::F32;
    } else if constexpr (std::is_same_v<T, bool>) {
        return MorphState::Kind::Bool;
    } else {
        static_assert(std::is_enum_v<T> && sizeof(T) == 1, "a MorphState leaf is float, bool or a one-byte enum");
        return MorphState::Kind::Enum8;
    }
}

/// FR-030: sum of the table's row sizes (the stream size without the version byte).
[[nodiscard]] consteval std::size_t sumOfRowBytes() {
    std::size_t sum = 0;
    for (const auto& row : kFieldTable) {
        sum += row.bytes;
    }
    return sum;
}

/// Compile-time drift detection (plan S3.3, R-4): the visitor walks exactly kMorphLeafFieldCount leaves, in table
/// order, and every leaf's C++ type matches its row's Kind.
[[nodiscard]] consteval bool leavesMatchTable() {
    MorphState s{};
    std::size_t n = 0;
    bool ok = true;
    forEachMorphLeaf(
        [&](auto leaf, auto& v) {
            constexpr std::size_t i = decltype(leaf)::value;
            ok = ok && i == n && kFieldTable[i].kind == kindOf<std::remove_cvref_t<decltype(v)>>();
            ++n;
        },
        s);
    return ok && n == kMorphLeafFieldCount;
}

static_assert(kFieldTable.size() == kMorphLeafFieldCount);   // FR-030
static_assert(leavesMatchTable());
static_assert(kMorphStateBytes == 1 + sumOfRowBytes());       // FR-030
static_assert(kMorphStateBytes == 201);                        // visible edit on any change
static_assert(kLeafOffsets[61] == 197);

// Row-shape sweep (plan S4.2, S3.2): Stepped rows carry a group and are one byte; continuous rows carry no group,
// are F32 and four bytes; the Anchor block is exactly leaves 55 - 61; a log row's logged quantity lo + o is > 0
// (FR-005, FR-032); a closed box has lo <= hi, the open one lo < hi.
static_assert([] {
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        const auto& row = kFieldTable[i];
        const bool stepped = row.law == MorphState::Law::Stepped;
        if (stepped != (row.group != MorphState::SelectionGroup::None)) {
            return false;
        }
        if (stepped != (row.kind != MorphState::Kind::F32)) {
            return false;
        }
        if (row.bytes != (row.kind == MorphState::Kind::F32 ? 4 : 1)) {
            return false;
        }
        if (row.kind == MorphState::Kind::Bool && row.maxEnum != 1) {
            return false;
        }
        if (row.anchorBlock != (i >= 55)) {
            return false;
        }
        if (row.law == MorphState::Law::Logarithmic && !(row.lo + row.logOffset > 0.0f)) {
            return false;
        }
        if (row.kind == MorphState::Kind::F32 && !(row.hiOpen ? row.lo < row.hi : row.lo <= row.hi)) {
            return false;
        }
    }
    return true;
}());

// The layout is defined as LITTLE-ENDIAN and every scalar moves as a raw std::memcpy of its object representation
// (the spectral_state.h pattern); this turns that assumption into a build-time contract.
static_assert(std::endian::native == std::endian::little,
              "kMorphStateBytes layout is little-endian; add byte swapping for this target");

/// FR-005a: field-wise bit equality (floats by std::bit_cast, enums and bools by value). Never memcmp: MorphState
/// has padding (plan S3.6).
[[nodiscard]] inline bool leafBitsEqual(const MorphState& a, const MorphState& b) noexcept {
    bool equal = true;
    forEachMorphLeaf(
        [&](auto, const auto& x, const auto& y) {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::is_same_v<T, float>) {
                equal = equal && std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
            } else {
                equal = equal && x == y;
            }
        },
        a, b);
    return equal;
}

/// Maps a finite float onto a signed integer in IEEE total order with -0 == +0, so the FR-005 box test does not
/// depend on the thread's MXCSR: under DAZ an ordinary float compare reads a denormal as 0 and admits
/// nextafter(0, -inf) into a [0, x] box (the test main and the audio thread both run with FTZ/DAZ).
[[nodiscard]] constexpr std::int64_t orderedFloatKey(float x) noexcept {
    const std::uint32_t u = std::bit_cast<std::uint32_t>(x);
    const auto magnitude = static_cast<std::int64_t>(u & 0x7FFFFFFFu);
    return (u & 0x80000000u) != 0u ? -magnitude : magnitude;
}

}  // namespace detail::morph_state_io

/// FR-005: true iff every float is finite and inside its table box (closed, except motion.cyclePhaseDeg's
/// [0, 360)), every enum holds a declared enumerator, and the field block passes BassStereoField::isValidControls.
/// -0.0f passes [0, x] boxes; denormals are valid finite values. Real-time safe.
[[nodiscard]] inline bool isValidMorphState(const MorphState& s) noexcept {
    namespace io = detail::morph_state_io;
    bool valid = true;
    io::forEachMorphLeaf(
        [&]([[maybe_unused]] auto leaf, const auto& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                constexpr float lo = io::kFieldTable[decltype(leaf)::value].lo;
                constexpr float hi = io::kFieldTable[decltype(leaf)::value].hi;
                constexpr bool hiOpen = io::kFieldTable[decltype(leaf)::value].hiOpen;
                constexpr std::int64_t loKey = io::orderedFloatKey(lo);
                constexpr std::int64_t hiKey = io::orderedFloatKey(hi);
                const std::int64_t key = io::orderedFloatKey(v);
                valid = valid && detail::isFinite(v) && loKey <= key && (key < hiKey || (!hiOpen && key == hiKey));
            } else if constexpr (std::is_enum_v<T>) {
                constexpr std::uint8_t maxEnum = io::kFieldTable[decltype(leaf)::value].maxEnum;
                valid = valid && static_cast<std::uint8_t>(v) <= maxEnum;
            }
            // bool: always valid in memory; a stream byte > 1 is rejected at decode (plan S1 C-13).
        },
        s);
    return valid && BassStereoField::isValidControls(s.field);
}

/// FR-006: write @p s into @p dest as exactly kMorphStateBytes bytes (version byte, then every leaf at
/// kLeafOffsets[i]: floats as their 4-byte bit pattern, enums and bools as one std::uint8_t).
/// @return kMorphStateBytes; 0 -- having written NOTHING -- if @p dest is null, @p capacity is short or @p s fails
///         isValidMorphState. Real-time safe: no allocation, no locks, no exceptions, no I/O.
[[nodiscard]] inline std::size_t serializeMorphState(const MorphState& s, std::byte* dest,
                                                     std::size_t capacity) noexcept {
    if (dest == nullptr || capacity < kMorphStateBytes || !isValidMorphState(s)) {
        return 0;
    }
    namespace io = detail::morph_state_io;

    const auto version = static_cast<std::uint8_t>(kMorphStateFormatVersion);
    std::memcpy(dest, &version, sizeof(version));

    io::forEachMorphLeaf(
        [&](auto leaf, const auto& v) {
            constexpr std::size_t offset = io::kLeafOffsets[decltype(leaf)::value];
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                std::memcpy(dest + offset, &v, sizeof(float));
            } else if constexpr (std::is_same_v<T, bool>) {
                const std::uint8_t byte = v ? std::uint8_t{1} : std::uint8_t{0};
                std::memcpy(dest + offset, &byte, sizeof(byte));
            } else {
                const auto byte = static_cast<std::uint8_t>(v);
                std::memcpy(dest + offset, &byte, sizeof(byte));
            }
        },
        s);
    return kMorphStateBytes;
}

/// FR-006: decode a stream written by serializeMorphState. Decodes into a LOCAL MorphState and copies into @p out
/// only on success, so a rejected stream leaves @p out bitwise untouched.
/// @return false -- @p out unmodified -- if @p src is null, @p size is short, the version byte is not
///         kMorphStateFormatVersion, a bool byte is > 1 (plan S1 C-13) or the decoded state fails isValidMorphState.
///         The round trip is exact because the bytes are stored values. Real-time safe.
[[nodiscard]] inline bool deserializeMorphState(const std::byte* src, std::size_t size, MorphState& out) noexcept {
    if (src == nullptr || size < kMorphStateBytes) {
        return false;
    }
    namespace io = detail::morph_state_io;

    std::uint8_t version = 0;
    std::memcpy(&version, src, sizeof(version));
    if (version != kMorphStateFormatVersion) {
        return false;
    }

    MorphState decoded{};
    bool ok = true;
    io::forEachMorphLeaf(
        [&](auto leaf, auto& v) {
            constexpr std::size_t offset = io::kLeafOffsets[decltype(leaf)::value];
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                std::memcpy(&v, src + offset, sizeof(float));
            } else {
                std::uint8_t byte = 0;
                std::memcpy(&byte, src + offset, sizeof(byte));
                if constexpr (std::is_same_v<T, bool>) {
                    ok = ok && byte <= 1;
                    v = byte != 0;
                } else {
                    v = static_cast<T>(byte);   // every byte is representable in a uint8-based enum
                }
            }
        },
        decoded);

    if (!ok || !isValidMorphState(decoded)) {
        return false;
    }
    out = decoded;
    return true;
}

}  // namespace Krate::DSP
