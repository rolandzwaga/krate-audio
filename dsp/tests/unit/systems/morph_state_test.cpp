// ==============================================================================
// Layer 3: System Tests - MorphState
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// Holds: FR-001 - FR-007, FR-030 (table), SC-007(a)(b) (finite rows; NaN / Inf rows are T006's),
//        SC-019(a)(c); plan S3, C-1, C-8, C-13.
// Filled by: T002. Engine-only: no render, no FTZ / DAZ (tasks.md rule 9, plan R-7).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/bass_morph_engine.h>

// Peers and name hazards (Waveform, SpectralState): proves the new headers coexist with them (T001).
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/spectral_morph_engine.h>
#include <krate/dsp/primitives/lfo.h>

#include "bass_morph_state_helpers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Qualified calls into the helper namespace (tasks.md rule 5); no using-directive.
namespace BassMorphTest = Krate::DSP::BassMorphTest;

namespace {

using Krate::DSP::AnchorOscillator;
using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassStereoField;
using Krate::DSP::deserializeMorphState;
using Krate::DSP::isValidMorphState;
using Krate::DSP::kMorphLeafFieldCount;
using Krate::DSP::kMorphStateBytes;
using Krate::DSP::MorphState;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;
using Krate::DSP::ProfundumCore;
using Krate::DSP::serializeMorphState;
using Krate::DSP::SpectralShapeRecipe;
namespace io = Krate::DSP::detail::morph_state_io;

using BmKind = MorphState::Kind;
using BmLaw = MorphState::Law;
using BmAxis = MorphState::Axis;
using BmGroup = MorphState::SelectionGroup;

// Directions for std::nextafter. FLT_MAX / lowest instead of +-inf (no infinity under fast-math); identical for
// every finite table bound.
constexpr float kBmUp = std::numeric_limits<float>::max();
constexpr float kBmDown = std::numeric_limits<float>::lowest();

[[nodiscard]] bool bmSame(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// Calls fn(v) on leaf `index` of s (fn is generic; it must handle float, bool and enum leaves).
template <class Fn>
void bmVisitLeaf(MorphState& s, std::size_t index, const Fn& fn) {
    io::forEachMorphLeaf(
        [&](auto leaf, auto& v) {
            if (decltype(leaf)::value == index) {
                fn(v);
            }
        },
        s);
}

void bmSetFloat(MorphState& s, std::size_t index, float x) {
    bmVisitLeaf(s, index, [&]([[maybe_unused]] auto& v) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(v)>, float>) {
            v = x;
        }
    });
}

void bmSetEnumByte(MorphState& s, std::size_t index, std::uint8_t byte) {
    bmVisitLeaf(s, index, [&]([[maybe_unused]] auto& v) {
        using T = std::remove_cvref_t<decltype(v)>;
        if constexpr (std::is_enum_v<T>) {
            v = static_cast<T>(byte);
        }
    });
}

/// Moves leaf `index` to a different valid value (F32: lo, or hi when already at lo; hiOpen: the float below hi;
/// enum: the next declared enumerator; bool: negated).
void bmChangeLeaf(MorphState& s, std::size_t index) {
    io::forEachMorphLeaf(
        [&](auto leaf, auto& v) {
            if (decltype(leaf)::value != index) {
                return;
            }
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
                const float top = row.hiOpen ? std::nextafter(row.hi, 0.0f) : row.hi;
                v = bmSame(v, row.lo) ? top : row.lo;
            } else if constexpr (std::is_same_v<T, bool>) {
                v = !v;
            } else {
                constexpr std::uint32_t count =
                    static_cast<std::uint32_t>(io::kFieldTable[decltype(leaf)::value].maxEnum) + 1u;
                const auto next = static_cast<std::uint32_t>(static_cast<std::uint8_t>(v)) + 1u;
                v = static_cast<T>(static_cast<std::uint8_t>(next % count));
            }
        },
        s);
}

/// The out-of-range finite values of an F32 row: below lo, above hi (the hiOpen row: hi itself).
[[nodiscard]] std::array<float, 2> bmOutOfRange(const MorphState::FieldInfo& row) {
    return {std::nextafter(row.lo, kBmDown), row.hiOpen ? row.hi : std::nextafter(row.hi, kBmUp)};
}

/// One expected row of plan S3.2 (transcribed; offset is the version-1 stream byte offset).
struct BmRow {
    std::string_view name;
    BmKind kind;
    BmLaw law;
    BmAxis axis;
    BmGroup group;
    bool anchorBlock;
    float lo;
    float hi;
    bool hiOpen;
    float logOffset;
    std::uint8_t maxEnum;
    std::uint8_t bytes;
    std::size_t offset;
};

constexpr BmRow bmF(std::string_view name, BmLaw law, BmAxis axis, float lo, float hi, std::size_t offset,
                    bool anchorBlock = false, bool hiOpen = false, float logOffset = 0.0f) {
    return BmRow{.name = name, .kind = BmKind::F32, .law = law, .axis = axis, .group = BmGroup::None,
                 .anchorBlock = anchorBlock, .lo = lo, .hi = hi, .hiOpen = hiOpen, .logOffset = logOffset,
                 .maxEnum = 0, .bytes = 4, .offset = offset};
}

constexpr BmRow bmE(std::string_view name, BmKind kind, BmAxis axis, BmGroup group, std::uint8_t maxEnum,
                    std::size_t offset, bool anchorBlock = false) {
    return BmRow{.name = name, .kind = kind, .law = BmLaw::Stepped, .axis = axis, .group = group,
                 .anchorBlock = anchorBlock, .lo = 0.0f, .hi = 0.0f, .hiOpen = false, .logOffset = 0.0f,
                 .maxEnum = maxEnum, .bytes = 1, .offset = offset};
}

// Plan S3.2, rows 0 - 61, transcribed literally (not from the source constants, so a drift in either shows).
constexpr std::array<BmRow, 62> kBmExpectedRows{{
    bmF("recipe.depth", BmLaw::Linear, BmAxis::SineComplex, 0.0f, 1.0f, 1),
    bmF("recipe.body", BmLaw::Linear, BmAxis::SineComplex, 0.0f, 1.0f, 5),
    bmF("recipe.bodyCurvature", BmLaw::Linear, BmAxis::SineComplex, 0.0f, 1.0f, 9),
    bmF("recipe.bodyEmphasis", BmLaw::Linear, BmAxis::SineComplex, -1.0f, 0.0f, 13),
    bmF("recipe.edge", BmLaw::Linear, BmAxis::DarkBright, 0.0f, 1.0f, 17),
    bmF("recipe.shift", BmLaw::Linear, BmAxis::DarkBright, -1.0f, 1.0f, 21),
    bmF("excitation.mix", BmLaw::EqualPower, BmAxis::CleanNonlinear, 0.0f, 1.0f, 25),
    bmF("excitation.drive", BmLaw::Linear, BmAxis::CleanNonlinear, 0.0f, 1.0f, 29),
    bmF("excitation.asymmetry", BmLaw::Linear, BmAxis::CleanNonlinear, 0.0f, 1.0f, 33),
    bmF("foldAmount", BmLaw::Linear, BmAxis::CleanNonlinear, 0.0f, 100.0f, 37),
    bmF("shape", BmLaw::Linear, BmAxis::DarkBright, 0.0f, 1.0f, 41),
    bmE("characterAlgorithm", BmKind::Enum8, BmAxis::CleanNonlinear, BmGroup::Character, 5, 45),
    bmF("characterAmount", BmLaw::Linear, BmAxis::CleanNonlinear, 0.0f, 1.0f, 46),
    bmF("characterTone", BmLaw::Linear, BmAxis::DarkBright, 0.0f, 1.0f, 50),
    bmF("dropAmountSt", BmLaw::Linear, BmAxis::ShortLong, 0.0f, 48.0f, 54),
    bmF("dropTimeMs", BmLaw::Logarithmic, BmAxis::ShortLong, 1.0f, 4000.0f, 58),
    bmF("dropShape", BmLaw::Linear, BmAxis::ShortLong, 0.0f, 1.0f, 62),
    bmF("dropRebound", BmLaw::Linear, BmAxis::ShortLong, 0.0f, 1.0f, 66),
    bmF("ampEnv.attackMs", BmLaw::Logarithmic, BmAxis::ShortLong, 0.1f, 10000.0f, 70),
    bmF("ampEnv.decayMs", BmLaw::Logarithmic, BmAxis::ShortLong, 0.1f, 10000.0f, 74),
    bmF("ampEnv.sustain", BmLaw::Linear, BmAxis::ShortLong, 0.0f, 1.0f, 78),
    bmF("ampEnv.releaseMs", BmLaw::Logarithmic, BmAxis::ShortLong, 0.1f, 10000.0f, 82),
    bmF("ampEnv.attackCurve", BmLaw::Linear, BmAxis::ShortLong, -1.0f, 1.0f, 86),
    bmF("ampEnv.decayCurve", BmLaw::Linear, BmAxis::ShortLong, -1.0f, 1.0f, 90),
    bmF("ampEnv.releaseCurve", BmLaw::Linear, BmAxis::ShortLong, -1.0f, 1.0f, 94),
    bmE("field.mode", BmKind::Enum8, BmAxis::MonoStereo, BmGroup::ImageMode, 2, 98),
    bmF("field.spread", BmLaw::Linear, BmAxis::MonoStereo, 0.0f, 1.0f, 99),
    bmF("field.gravity", BmLaw::Linear, BmAxis::MonoStereo, 0.0f, 1.0f, 103),
    bmF("field.crossoverHz", BmLaw::Logarithmic, BmAxis::MonoStereo, 60.0f, 250.0f, 107),
    bmF("field.curve", BmLaw::Linear, BmAxis::MonoStereo, 0.0f, 1.0f, 111),
    bmF("field.motionDepth", BmLaw::Linear, BmAxis::MonoStereo, 0.0f, 1.0f, 115),
    bmF("field.motionRate", BmLaw::Linear, BmAxis::MonoStereo, 0.0f, 1.0f, 119),
    bmE("field.motionSource", BmKind::Enum8, BmAxis::MonoStereo, BmGroup::FieldMotion, 1, 123),
    bmE("field.cycleSync", BmKind::Bool, BmAxis::MonoStereo, BmGroup::FieldMotion, 1, 124),
    bmE("field.cycleNote", BmKind::Enum8, BmAxis::MonoStereo, BmGroup::FieldMotion, 9, 125),
    bmE("field.cycleModifier", BmKind::Enum8, BmAxis::MonoStereo, BmGroup::FieldMotion, 2, 126),
    bmE("motion.source", BmKind::Enum8, BmAxis::Motion, BmGroup::Motion, 3, 127),
    bmE("motion.target", BmKind::Enum8, BmAxis::Motion, BmGroup::Motion, 4, 128),
    bmF("motion.amount", BmLaw::Linear, BmAxis::Motion, -1.0f, 1.0f, 129),
    bmF("motion.depth", BmLaw::Linear, BmAxis::Motion, 0.0f, 1.0f, 133),
    bmF("motion.cycleRateHz", BmLaw::Logarithmic, BmAxis::Motion, 0.01f, 20.0f, 137),
    bmE("motion.cycleShape", BmKind::Enum8, BmAxis::Motion, BmGroup::Motion, 5, 141),
    bmE("motion.cycleSync", BmKind::Bool, BmAxis::Motion, BmGroup::Motion, 1, 142),
    bmE("motion.cycleNote", BmKind::Enum8, BmAxis::Motion, BmGroup::Motion, 9, 143),
    bmE("motion.cycleModifier", BmKind::Enum8, BmAxis::Motion, BmGroup::Motion, 2, 144),
    bmF("motion.cyclePhaseDeg", BmLaw::Linear, BmAxis::Motion, 0.0f, 360.0f, 145, false, true),
    bmE("motion.cycleRetrigger", BmKind::Bool, BmAxis::Motion, BmGroup::Motion, 1, 149),
    bmF("motion.cycleFadeInMs", BmLaw::Logarithmic, BmAxis::Motion, 0.0f, 5000.0f, 150, false, false, 1.0f),
    bmE("motion.polarity", BmKind::Enum8, BmAxis::Motion, BmGroup::Motion, 1, 154),
    bmF("motion.driftSmoothness", BmLaw::Linear, BmAxis::Motion, 0.0f, 1.0f, 155),
    bmF("motion.envAttackMs", BmLaw::Logarithmic, BmAxis::Motion, 0.1f, 10000.0f, 159),
    bmF("motion.envDecayMs", BmLaw::Logarithmic, BmAxis::Motion, 0.1f, 10000.0f, 163),
    bmF("mass", BmLaw::Linear, BmAxis::CleanNonlinear, 0.0f, 100.0f, 167),
    bmF("transientAttackDb", BmLaw::Linear, BmAxis::ShortLong, -12.0f, 12.0f, 171),
    bmF("transientSustainDb", BmLaw::Linear, BmAxis::ShortLong, -12.0f, 12.0f, 175),
    bmE("anchor.waveform", BmKind::Enum8, BmAxis::Anchor, BmGroup::Anchor, 4, 179, true),
    bmE("anchor.mode", BmKind::Enum8, BmAxis::Anchor, BmGroup::Anchor, 3, 180, true),
    bmF("anchor.level", BmLaw::Linear, BmAxis::Anchor, 0.0f, 1.0f, 181, true),
    bmF("anchor.punchBoostDb", BmLaw::Linear, BmAxis::Anchor, 0.0f, 12.0f, 185, true),
    bmF("anchor.punchTimeMs", BmLaw::Logarithmic, BmAxis::Anchor, 5.0f, 250.0f, 189, true),
    bmF("anchor.ghostTimeMs", BmLaw::Logarithmic, BmAxis::Anchor, 1.0f, 100.0f, 193, true),
    bmF("anchorFoldFeed", BmLaw::EqualPower, BmAxis::Anchor, 0.0f, 1.0f, 197, true),
}};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// FR-001: plain, trivially copyable aggregate; every enum leaf is one byte.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphState_TypeTraits", "[systems][profundum][bass-morph][fr-001]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<MorphState>);
    STATIC_REQUIRE(std::is_aggregate_v<MorphState>);
    STATIC_REQUIRE(!std::is_polymorphic_v<MorphState>);
    STATIC_REQUIRE(sizeof(MorphState::Law) == 1);
    STATIC_REQUIRE(sizeof(MorphState::Axis) == 1);
    STATIC_REQUIRE(sizeof(MorphState::Kind) == 1);
    STATIC_REQUIRE(sizeof(MorphState::SelectionGroup) == 1);
    STATIC_REQUIRE(sizeof(MorphState::MotionConfig::Source) == 1);
    STATIC_REQUIRE(sizeof(MorphState::MotionConfig::Target) == 1);
    STATIC_REQUIRE(sizeof(MorphState::MotionConfig::Polarity) == 1);
    STATIC_REQUIRE(sizeof(BassCharacterStage::Algorithm) == 1);
    STATIC_REQUIRE(sizeof(BassStereoField::ImageMode) == 1);
    STATIC_REQUIRE(sizeof(BassStereoField::MotionSource) == 1);
    STATIC_REQUIRE(sizeof(NoteValue) == 1);
    STATIC_REQUIRE(sizeof(NoteModifier) == 1);
    STATIC_REQUIRE(sizeof(::Krate::DSP::Waveform) == 1);
    STATIC_REQUIRE(sizeof(AnchorOscillator::Waveform) == 1);
    STATIC_REQUIRE(sizeof(AnchorOscillator::Mode) == 1);
}

// ---------------------------------------------------------------------------------------------------------------
// FR-004: member defaults equal each component's own defaults (plan S1 C-8 for the amp envelope).
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphState_Defaults", "[systems][profundum][bass-morph][fr-004]") {
    const MorphState s{};

    const auto& rc = SpectralShapeRecipe::kDefaultControls;
    CHECK(bmSame(s.recipe.depth, rc.depth));
    CHECK(bmSame(s.recipe.body, rc.body));
    CHECK(bmSame(s.recipe.bodyCurvature, rc.bodyCurvature));
    CHECK(bmSame(s.recipe.bodyEmphasis, rc.bodyEmphasis));
    CHECK(bmSame(s.recipe.edge, rc.edge));
    CHECK(bmSame(s.recipe.shift, rc.shift));

    const auto& ex = ProfundumCore::kDefaultExcitation;
    CHECK(bmSame(s.excitation.mix, ex.mix));
    CHECK(bmSame(s.excitation.drive, ex.drive));
    CHECK(bmSame(s.excitation.asymmetry, ex.asymmetry));

    CHECK(bmSame(s.foldAmount, 0.0f));
    CHECK(bmSame(s.shape, 0.0f));
    CHECK(s.characterAlgorithm == BassCharacterStage::Algorithm::Tube);
    CHECK(bmSame(s.characterAmount, 0.0f));
    CHECK(bmSame(s.characterTone, 0.5f));
    CHECK(bmSame(s.dropAmountSt, 0.0f));
    CHECK(bmSame(s.dropTimeMs, 100.0f));
    CHECK(bmSame(s.dropShape, 0.0f));
    CHECK(bmSame(s.dropRebound, 0.0f));

    // plan S1 C-8: ADSREnvelope member defaults (adsr_envelope.h:521-533)
    CHECK(bmSame(s.ampEnv.attackMs, 10.0f));
    CHECK(bmSame(s.ampEnv.decayMs, 50.0f));
    CHECK(bmSame(s.ampEnv.sustain, 0.5f));
    CHECK(bmSame(s.ampEnv.releaseMs, 100.0f));
    CHECK(bmSame(s.ampEnv.attackCurve, 0.7f));
    CHECK(bmSame(s.ampEnv.decayCurve, 0.7f));
    CHECK(bmSame(s.ampEnv.releaseCurve, 0.7f));

    const BassStereoField::Controls fc{};
    CHECK(s.field.mode == fc.mode);
    CHECK(bmSame(s.field.spread, fc.spread));
    CHECK(bmSame(s.field.gravity, fc.gravity));
    CHECK(bmSame(s.field.crossoverHz, fc.crossoverHz));
    CHECK(bmSame(s.field.curve, fc.curve));
    CHECK(bmSame(s.field.motionDepth, fc.motionDepth));
    CHECK(bmSame(s.field.motionRate, fc.motionRate));
    CHECK(s.field.motionSource == fc.motionSource);
    CHECK(s.field.cycleSync == fc.cycleSync);
    CHECK(s.field.cycleNote == fc.cycleNote);
    CHECK(s.field.cycleModifier == fc.cycleModifier);

    CHECK(s.motion.source == MorphState::MotionConfig::Source::Off);
    CHECK(s.motion.target == MorphState::MotionConfig::Target::Mass);
    CHECK(bmSame(s.motion.amount, 0.0f));
    CHECK(bmSame(s.motion.depth, 1.0f));
    CHECK(bmSame(s.motion.cycleRateHz, 1.0f));
    CHECK(s.motion.cycleShape == ::Krate::DSP::Waveform::Sine);
    CHECK(s.motion.cycleSync == false);
    CHECK(s.motion.cycleNote == NoteValue::Quarter);
    CHECK(s.motion.cycleModifier == NoteModifier::None);
    CHECK(bmSame(s.motion.cyclePhaseDeg, 0.0f));
    CHECK(s.motion.cycleRetrigger == true);
    CHECK(bmSame(s.motion.cycleFadeInMs, 0.0f));
    CHECK(s.motion.polarity == MorphState::MotionConfig::Polarity::Bipolar);
    CHECK(bmSame(s.motion.driftSmoothness, 0.5f));
    CHECK(bmSame(s.motion.envAttackMs, 10.0f));
    CHECK(bmSame(s.motion.envDecayMs, 300.0f));

    CHECK(bmSame(s.mass, 50.0f));
    CHECK(bmSame(s.transientAttackDb, 6.0f));
    CHECK(bmSame(s.transientSustainDb, 0.0f));

    const AnchorOscillator::Controls ac{};
    CHECK(s.anchor.waveform == ac.waveform);
    CHECK(s.anchor.mode == ac.mode);
    CHECK(bmSame(s.anchor.level, ac.level));
    CHECK(bmSame(s.anchor.punchBoostDb, ac.punchBoostDb));
    CHECK(bmSame(s.anchor.punchTimeMs, ac.punchTimeMs));
    CHECK(bmSame(s.anchor.ghostTimeMs, ac.ghostTimeMs));
    CHECK(bmSame(s.anchorFoldFeed, 0.0f));
}

// ---------------------------------------------------------------------------------------------------------------
// FR-004 / FR-005: the default state is valid.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphState_DefaultIsValid", "[systems][profundum][bass-morph][fr-004][fr-005]") {
    CHECK(isValidMorphState(MorphState{}));
}

// ---------------------------------------------------------------------------------------------------------------
// FR-002 / FR-030 (plan S1 C-1, S3.2): the one field-descriptor table matches the plan row by row, and its field
// rows agree with BassStereoField::isValidControls.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphState_TableMatchesComponentRanges", "[systems][profundum][bass-morph][fr-002][fr-030]") {
    REQUIRE(io::kFieldTable.size() == 62);
    REQUIRE(io::kFieldTable.size() == kMorphLeafFieldCount);

    for (std::size_t i = 0; i < kBmExpectedRows.size(); ++i) {
        const auto& row = io::kFieldTable[i];
        const auto& exp = kBmExpectedRows[i];
        INFO("row " << i << " " << exp.name);
        CHECK(std::string_view(row.name) == exp.name);
        CHECK(row.kind == exp.kind);
        CHECK(row.law == exp.law);
        CHECK(row.axis == exp.axis);
        CHECK(row.group == exp.group);
        CHECK(row.anchorBlock == exp.anchorBlock);
        CHECK(bmSame(row.lo, exp.lo));
        CHECK(bmSame(row.hi, exp.hi));
        CHECK(row.hiOpen == exp.hiOpen);
        CHECK(bmSame(row.logOffset, exp.logOffset));
        CHECK(row.maxEnum == exp.maxEnum);
        CHECK(row.bytes == exp.bytes);
        CHECK(io::kLeafOffsets[i] == exp.offset);
    }

    // The field rows (25 - 35) carry the component's own ranges.
    CHECK(bmSame(io::kFieldTable[28].lo, BassStereoField::kMinCrossoverHz));
    CHECK(bmSame(io::kFieldTable[28].hi, BassStereoField::kMaxCrossoverHz));
    for (std::size_t i : {26u, 27u, 29u, 30u, 31u}) {
        INFO("field row " << i);
        CHECK(bmSame(io::kFieldTable[i].lo, 0.0f));
        CHECK(bmSame(io::kFieldTable[i].hi, 1.0f));
    }

    // Agreement with isValidControls on 10 000 random field blocks (seed 0x8005): floats on
    // [lo - 0.1 span, hi + 0.1 span] (finite only), enums on [0, maxEnum + 1], bools 0 / 1.
    std::mt19937 rng(0x8005u);
    int disagreements = 0;
    int validCount = 0;
    for (int n = 0; n < 10000; ++n) {
        MorphState s{};
        bool expected = true;
        io::forEachMorphLeaf(
            [&](auto leaf, [[maybe_unused]] auto& v) {
                constexpr std::size_t i = decltype(leaf)::value;
                if constexpr (i >= 25 && i <= 35) {
                    using T = std::remove_cvref_t<decltype(v)>;
                    const auto w = static_cast<std::uint32_t>(rng());
                    if constexpr (std::is_same_v<T, float>) {
                        const double lo = static_cast<double>(io::kFieldTable[i].lo);
                        const double span = static_cast<double>(io::kFieldTable[i].hi) - lo;
                        const double x = lo - 0.1 * span
                                         + BassMorphTest::unitFromWord(w) * 1.2 * span;
                        v = static_cast<float>(x);
                        expected = expected && v >= io::kFieldTable[i].lo && v <= io::kFieldTable[i].hi;
                    } else if constexpr (std::is_same_v<T, bool>) {
                        v = (w & 1u) != 0u;
                    } else {
                        constexpr std::uint32_t count = static_cast<std::uint32_t>(io::kFieldTable[i].maxEnum) + 2u;
                        const auto byte = static_cast<std::uint8_t>(w % count);
                        v = static_cast<T>(byte);
                        expected = expected && byte <= io::kFieldTable[i].maxEnum;
                    }
                }
            },
            s);
        const bool actual = BassStereoField::isValidControls(s.field);
        if (actual != expected) {
            ++disagreements;
        }
        if (actual) {
            ++validCount;
        }
    }
    INFO("valid field blocks: " << validCount << " / 10000");
    CHECK(disagreements == 0);
    CHECK(validCount > 0);   // the draw exercises both outcomes
    CHECK(validCount < 10000);
}

// ---------------------------------------------------------------------------------------------------------------
// FR-005: validation edges per leaf (finite cases only; the NaN / Inf rows are T006's nonfinite TU).
// ---------------------------------------------------------------------------------------------------------------
// FR-005 / FR-032: every log row's logged quantity lo + o is strictly positive.
static_assert([] {
    for (const auto& row : io::kFieldTable) {
        if (row.law == BmLaw::Logarithmic && !(row.lo + row.logOffset > 0.0f)) {
            return false;
        }
    }
    return true;
}());

TEST_CASE("MorphState_Validation", "[systems][profundum][bass-morph][fr-005]") {
    const float denormal = 1e-40f;
    int checkedF32 = 0;
    int checkedEnum = 0;
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        const auto& row = io::kFieldTable[i];
        INFO("leaf " << i << " " << row.name);
        if (row.kind == BmKind::F32) {
            ++checkedF32;
            const auto out = bmOutOfRange(row);
            for (float x : out) {
                MorphState s{};
                bmSetFloat(s, i, x);
                INFO("value " << std::setprecision(9) << x);
                CHECK_FALSE(isValidMorphState(s));
            }
            const float top = row.hiOpen ? std::nextafter(row.hi, 0.0f) : row.hi;
            for (float x : {row.lo, top}) {
                MorphState s{};
                bmSetFloat(s, i, x);
                INFO("value " << std::setprecision(9) << x);
                CHECK(isValidMorphState(s));
            }
            if (bmSame(row.lo, 0.0f)) {
                for (float x : {-0.0f, denormal}) {
                    MorphState s{};
                    bmSetFloat(s, i, x);
                    INFO("value " << std::setprecision(9) << x);
                    CHECK(isValidMorphState(s));
                }
            }
        } else if (row.kind == BmKind::Enum8) {
            ++checkedEnum;
            MorphState bad{};
            bmSetEnumByte(bad, i, static_cast<std::uint8_t>(row.maxEnum + 1));
            CHECK_FALSE(isValidMorphState(bad));
            MorphState good{};
            bmSetEnumByte(good, i, row.maxEnum);
            CHECK(isValidMorphState(good));
        }
    }
    CHECK(checkedF32 == 46);
    CHECK(checkedEnum == 13);
}

// ---------------------------------------------------------------------------------------------------------------
// SC-019(a)(c): leaf arity and FR-003 exclusions.
// Adding a member fails here until the binding, the table and the serializer change together.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphState_LeafArity", "[systems][profundum][bass-morph][sc-019]") {
    MorphState s{};
    // (a) Structured bindings with the exact member count: a new member breaks the build here.
    {
        [[maybe_unused]] auto& [m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19] =
            s;
    }
    {
        [[maybe_unused]] auto& [e1, e2, e3, e4, e5, e6, e7] = s.ampEnv;
    }
    {
        [[maybe_unused]] auto& [c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15, c16] = s.motion;
    }
    {
        [[maybe_unused]] auto& [r1, r2, r3, r4, r5, r6] = s.recipe;
    }
    {
        [[maybe_unused]] auto& [x1, x2, x3] = s.excitation;
    }
    {
        [[maybe_unused]] auto& [f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11] = s.field;
    }
    {
        [[maybe_unused]] auto& [a1, a2, a3, a4, a5, a6] = s.anchor;
    }
    // 19 top-level members = 13 scalar leaves + 6 aggregates.
    STATIC_REQUIRE(std::size_t{13 + 7 + 16 + 6 + 3 + 11 + 6} == kMorphLeafFieldCount);

    // (c) FR-003: no excluded field exists.
    const std::array<std::string_view, 14> excluded{
        "outputtrim", "glide",      "legato",    "priority",          "poly",        "voice",       "partial",
        "guard",      "seed",       "transientduration", "compression", "wideoffset", "memory",     "retriggerphase"};
    for (const auto& row : io::kFieldTable) {
        std::string lower(row.name);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        for (const auto word : excluded) {
            INFO(row.name << " vs " << word);
            CHECK(lower.find(word) == std::string::npos);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// SC-007(a): 10 000 random valid states round-trip bit-exactly; the size is exactly kMorphStateBytes.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("BassMorph_Serialisation_RoundTrip", "[systems][profundum][bass-morph][sc-007]") {
    std::mt19937 rng(0x8007u);
    int sizeFailures = 0;
    int decodeFailures = 0;
    int mismatches = 0;
    int versionFailures = 0;
    for (int n = 0; n < 10000; ++n) {
        const MorphState s = BassMorphTest::randomValidState(rng);
        std::array<std::byte, 256> buf{};
        if (serializeMorphState(s, buf.data(), buf.size()) != 201) {
            ++sizeFailures;
            continue;
        }
        if (buf[0] != std::byte{1}) {
            ++versionFailures;
        }
        MorphState out{};
        if (!deserializeMorphState(buf.data(), 201, out)) {
            ++decodeFailures;
            continue;
        }
        if (!BassMorphTest::bitEqual(out, s)) {
            ++mismatches;
        }
        MorphState out256{};
        if (!deserializeMorphState(buf.data(), 256, out256)) {
            ++decodeFailures;
            continue;
        }
        if (!BassMorphTest::bitEqual(out256, s)) {
            ++mismatches;
        }
    }
    CHECK(kMorphStateBytes == 201);
    CHECK(sizeFailures == 0);
    CHECK(versionFailures == 0);
    CHECK(decodeFailures == 0);
    CHECK(mismatches == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// SC-007(b), finite rows (plan S1 C-13): every rejection writes nothing / leaves `out` bitwise unchanged.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("BassMorph_Serialisation_Rejects", "[systems][profundum][bass-morph][sc-007]") {
    constexpr auto kFill = std::byte{0xA5};
    const MorphState valid = BassMorphTest::showcasePair().first;
    const MorphState preset = BassMorphTest::showcasePair().second;
    REQUIRE_FALSE(BassMorphTest::bitEqual(valid, preset));

    auto untouched = [&](const std::array<std::byte, 256>& b) {
        return std::all_of(b.begin(), b.end(), [&](std::byte x) { return x == kFill; });
    };

    // ---- serialise ----
    CHECK(serializeMorphState(valid, nullptr, 256) == 0);
    for (std::size_t cap = 0; cap <= 200; ++cap) {
        std::array<std::byte, 256> buf{};
        buf.fill(kFill);
        INFO("capacity " << cap);
        CHECK(serializeMorphState(valid, buf.data(), cap) == 0);
        CHECK(untouched(buf));
    }
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        const auto& row = io::kFieldTable[i];
        INFO("leaf " << i << " " << row.name);
        if (row.kind == BmKind::F32) {
            for (float x : bmOutOfRange(row)) {
                MorphState bad = valid;
                bmSetFloat(bad, i, x);
                std::array<std::byte, 256> buf{};
                buf.fill(kFill);
                CHECK(serializeMorphState(bad, buf.data(), buf.size()) == 0);
                CHECK(untouched(buf));
            }
        } else if (row.kind == BmKind::Enum8) {
            MorphState bad = valid;
            bmSetEnumByte(bad, i, static_cast<std::uint8_t>(row.maxEnum + 1));
            std::array<std::byte, 256> buf{};
            buf.fill(kFill);
            CHECK(serializeMorphState(bad, buf.data(), buf.size()) == 0);
            CHECK(untouched(buf));
        }
    }

    // ---- deserialise ----
    const auto stream = BassMorphTest::serializedBytes(valid);
    auto rejects = [&](const std::byte* src, std::size_t size) {
        MorphState out = preset;
        const bool ok = deserializeMorphState(src, size, out);
        return !ok && BassMorphTest::bitEqual(out, preset);
    };
    {
        MorphState out = preset;
        CHECK(deserializeMorphState(stream.data(), stream.size(), out));   // the base stream is accepted
        CHECK(BassMorphTest::bitEqual(out, valid));
    }
    CHECK(rejects(nullptr, kMorphStateBytes));
    for (std::size_t size = 0; size <= 200; ++size) {
        INFO("size " << size);
        CHECK(rejects(stream.data(), size));
    }
    for (std::uint8_t version : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{255}}) {
        auto bytes = stream;
        bytes[0] = static_cast<std::byte>(version);
        INFO("version " << static_cast<int>(version));
        CHECK(rejects(bytes.data(), bytes.size()));
    }
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        const auto& row = io::kFieldTable[i];
        const std::size_t offset = io::kLeafOffsets[i];
        INFO("leaf " << i << " " << row.name << " at offset " << offset);
        if (row.kind == BmKind::F32) {
            for (float x : bmOutOfRange(row)) {
                auto bytes = stream;
                std::memcpy(bytes.data() + offset, &x, sizeof(float));
                INFO("value " << std::setprecision(9) << x);
                CHECK(rejects(bytes.data(), bytes.size()));
            }
        } else if (row.kind == BmKind::Enum8) {
            for (std::uint8_t b : {static_cast<std::uint8_t>(row.maxEnum + 1), std::uint8_t{255}}) {
                auto bytes = stream;
                bytes[offset] = static_cast<std::byte>(b);
                INFO("byte " << static_cast<int>(b));
                CHECK(rejects(bytes.data(), bytes.size()));
            }
        } else {
            // plan S1 C-13: a bool byte other than 0 / 1 is rejected.
            for (std::uint8_t b : {std::uint8_t{2}, std::uint8_t{255}}) {
                auto bytes = stream;
                bytes[offset] = static_cast<std::byte>(b);
                INFO("byte " << static_cast<int>(b));
                CHECK(rejects(bytes.data(), bytes.size()));
            }
        }
    }
    // A stream truncated to 200 bytes.
    {
        std::array<std::byte, 200> truncated{};
        std::memcpy(truncated.data(), stream.data(), truncated.size());
        CHECK(rejects(truncated.data(), truncated.size()));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Plan S3.6 / FR-005a: the serialised-bytes bitEqual and the field-wise leafBitsEqual agree.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphStateHelpers_BitEqualAgreesWithLeafBits", "[systems][profundum][bass-morph]") {
    std::mt19937 rng(0x8036u);
    int disagreements = 0;
    int equalPairs = 0;
    for (int n = 0; n < 10000; ++n) {
        const auto [a, b] = BassMorphTest::randomValidPair(rng, 0.5f);
        const bool viaBytes = BassMorphTest::bitEqual(a, b);
        const bool viaLeaves = io::leafBitsEqual(a, b);
        if (viaBytes != viaLeaves) {
            ++disagreements;
        }
        if (viaLeaves) {
            ++equalPairs;
        }
        // An identical copy agrees too.
        if (!BassMorphTest::bitEqual(a, a) || !io::leafBitsEqual(a, a)) {
            ++disagreements;
        }
    }
    INFO("random pairs bitwise equal: " << equalPairs);
    CHECK(disagreements == 0);

    // Per leaf: a pair that differs in that leaf only.
    const MorphState base = BassMorphTest::randomValidState(rng);
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        INFO("leaf " << i << " " << io::kFieldTable[i].name);
        MorphState changed = base;
        bmChangeLeaf(changed, i);
        const auto diff = BassMorphTest::differingLeaves(base, changed);
        REQUIRE(diff.size() == 1);
        CHECK(diff[0] == i);
        CHECK_FALSE(BassMorphTest::bitEqual(base, changed));
        CHECK_FALSE(io::leafBitsEqual(base, changed));
    }

    // +0.0f vs -0.0f in one F32 leaf whose box holds both: both definitions say "not equal".
    int signedZeroRows = 0;
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        const auto& row = io::kFieldTable[i];
        if (row.kind != BmKind::F32 || row.lo > 0.0f || row.hi < 0.0f) {
            continue;
        }
        ++signedZeroRows;
        INFO("leaf " << i << " " << row.name);
        MorphState pos{};
        bmSetFloat(pos, i, 0.0f);
        MorphState neg = pos;
        bmSetFloat(neg, i, -0.0f);
        CHECK_FALSE(BassMorphTest::bitEqual(pos, neg));
        CHECK_FALSE(io::leafBitsEqual(pos, neg));
    }
    CHECK(signedZeroRows > 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Plan S5.1: the random generator only produces valid states and exercises zeros, box edges and every enumerator.
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("MorphStateHelpers_RandomValidStateIsValid", "[systems][profundum][bass-morph]") {
    std::mt19937 rng(0x8099u);
    constexpr int kStates = 10000;
    int invalid = 0;
    std::array<int, 5> governingZero{};
    std::array<int, kMorphLeafFieldCount> hitLo{};
    std::array<int, kMorphLeafFieldCount> hitHi{};
    std::array<std::array<bool, 256>, kMorphLeafFieldCount> seen{};
    for (int n = 0; n < kStates; ++n) {
        const MorphState s = BassMorphTest::randomValidState(rng);
        if (!isValidMorphState(s)) {
            ++invalid;
        }
        const std::array<float, 5> governing{s.characterAmount, s.field.spread, s.field.motionDepth, s.motion.amount,
                                             s.anchor.level};
        for (std::size_t g = 0; g < governing.size(); ++g) {
            if (governing[g] == 0.0f) {
                ++governingZero[g];
            }
        }
        io::forEachMorphLeaf(
            [&](auto leaf, const auto& v) {
                constexpr std::size_t i = decltype(leaf)::value;
                using T = std::remove_cvref_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>) {
                    if (v == io::kFieldTable[i].lo) {
                        ++hitLo[i];
                    }
                    if (v == io::kFieldTable[i].hi) {
                        ++hitHi[i];
                    }
                } else {
                    seen[i][static_cast<std::uint8_t>(v)] = true;
                }
            },
            s);
    }
    CHECK(invalid == 0);

    const std::array<const char*, 5> governingNames{"characterAmount", "field.spread", "field.motionDepth",
                                                    "motion.amount", "anchor.level"};
    for (std::size_t g = 0; g < governingZero.size(); ++g) {
        INFO(governingNames[g] << " exactly 0 in " << governingZero[g] << " of " << kStates);
        CHECK(governingZero[g] >= 1000);
        CHECK(governingZero[g] <= 2000);
    }
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        const auto& row = io::kFieldTable[i];
        INFO("leaf " << i << " " << row.name << " lo hits " << hitLo[i] << " hi hits " << hitHi[i]);
        if (row.kind == BmKind::F32) {
            CHECK(hitLo[i] >= 1);
            if (!row.hiOpen) {
                CHECK(hitHi[i] >= 1);
            } else {
                CHECK(hitHi[i] == 0);   // [0, 360): 360 is never produced
            }
        } else {
            for (std::size_t e = 0; e <= row.maxEnum; ++e) {
                INFO("value " << e);
                CHECK(seen[i][e]);
            }
        }
    }
}
