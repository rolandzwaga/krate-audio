// ==============================================================================
// Profundum - macro UI map tests (Phase 12)
// ==============================================================================
// Profundum_MacroUiMap, Profundum_UiContractTables — written by T015 (specs/profundum-phase12-ui/tasks.md).
// Profundum_MacroModulationRing (controller arm) — T023. Profundum_MacroHighlight, the ring knob arm
// (SECTION "KnobsCarryRing") and Profundum_MidiLearnHostLevel — T029, on the built tree of a HeadlessEditor
// (plan §4.7 "Macro highlight", §6.2 "Macros"; SC-009, SC-028 knob arm, SC-029).
//
// Profundum_MacroUiMap (FR-060; plan §4.10): kMacroTargetParamIds element-wise, and highlightMask(m) against the
// sets DERIVED from ProfundumMacroMatrix::kRows, then against the literal sets of the task.
//
// Profundum_UiContractTables (FR-005, FR-007; plan §3.2) is a hidden probe ([.][probe]): it prints the D3 CSV
// (one row per kRows row) and the D4 CSV (one row per kLawTable leaf) that the design docs paste between their
// GENERATED markers. Run: profundum_tests.exe "Profundum_UiContractTables"
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

#include "ui_tree_helpers.h"  // HeadlessEditor, collectViewsOfType (T029)

#include "controller/controller.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "ui/macro_ui_map.h"
#include "ui/offset_ring_knob.h"
#include "ui/panel_sub_controller.h"

// plugins/shared/src/ui: the classes the panel highlights (T029)
#include "ui/adsr_display.h"
#include "ui/arc_knob.h"
#include "ui/bipolar_slider.h"

#include "base/source/updatehandler.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstmidilearn.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/csegmentbutton.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/events.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uidescription.h"

#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace P = ::Profundum;
namespace PU = ::Profundum::UI;
using Krate::DSP::ProfundumMacro;
using Krate::DSP::ProfundumMacroMatrix;
using Krate::DSP::ProfundumMacroTarget;
using Steinberg::Vst::ParamID;

using IdSet = std::set<ParamID>;

constexpr std::array<ProfundumMacro, 4> kAllMacros{ProfundumMacro::Weight, ProfundumMacro::DarkBright,
                                                   ProfundumMacro::TightLoose, ProfundumMacro::CleanViolent};

const char* macroName(ProfundumMacro m) {
    switch (m) {
        case ProfundumMacro::Weight: return "Weight";
        case ProfundumMacro::DarkBright: return "DarkBright";
        case ProfundumMacro::TightLoose: return "TightLoose";
        case ProfundumMacro::CleanViolent: return "CleanViolent";
        default: return "?";
    }
}

const char* targetName(ProfundumMacroTarget t) {
    constexpr std::array<const char*, ProfundumMacroMatrix::kNumTargets> kNames{
        "RecipeDepth",   "RecipeBody",      "AnchorLevel", "RecipeEdge",      "RecipeShift",
        "Shape",         "CharacterTone",   "AmpAttackOct", "AmpDecayOct",    "AmpReleaseOct",
        "TransientAttackDb", "CompressionAmount", "ExcitationMix", "ExcitationDrive", "FoldAmount",
        "CharacterAmount"};
    const auto i = static_cast<std::size_t>(t);
    return i < kNames.size() ? kNames[i] : "?";
}

const char* curveName(Krate::DSP::ModCurve c) {
    switch (c) {
        case Krate::DSP::ModCurve::Linear: return "Linear";
        case Krate::DSP::ModCurve::Exponential: return "Exponential";
        case Krate::DSP::ModCurve::SCurve: return "SCurve";
        default: return "other";
    }
}

const char* lawName(Krate::DSP::MorphState::Law l) {
    using L = Krate::DSP::MorphState::Law;
    switch (l) {
        case L::Linear: return "Linear";
        case L::Logarithmic: return "Logarithmic";
        case L::EqualPower: return "EqualPower";
        case L::Stepped: return "Stepped";
        default: return "?";
    }
}

const char* axisName(Krate::DSP::MorphState::Axis a) {
    using A = Krate::DSP::MorphState::Axis;
    switch (a) {
        case A::SineComplex: return "SineComplex";
        case A::CleanNonlinear: return "CleanNonlinear";
        case A::MonoStereo: return "MonoStereo";
        case A::ShortLong: return "ShortLong";
        case A::DarkBright: return "DarkBright";
        case A::Anchor: return "Anchor";
        case A::Motion: return "Motion";
        default: return "?";
    }
}

/// The IDs of m's kRows rows with amount != 0, CompressionAmount excluded — derived from the table.
IdSet derivedHighlightSet(ProfundumMacro m) {
    IdSet ids;
    for (const auto& row : ProfundumMacroMatrix::kRows) {
        if (row.macro == m && row.amount != 0.0f && row.target != ProfundumMacroTarget::CompressionAmount) {
            ids.insert(PU::kMacroTargetParamIds[static_cast<std::size_t>(row.target)]);
        }
    }
    return ids;
}

IdSet maskToSet(const std::array<bool, P::kNumRoutedIds>& mask) {
    IdSet ids;
    for (std::size_t id = 0; id < mask.size(); ++id) {
        if (mask[id]) {
            ids.insert(static_cast<ParamID>(id));
        }
    }
    return ids;
}

/// The registered sound-row ID that carries `leaf` (as leaf or leaf2), or kNoParamId.
ParamID registeredIdOfLeaf(std::size_t leaf) {
    for (const auto& row : P::kSoundRows) {
        if (static_cast<std::size_t>(row.leaf) == leaf ||
            (row.leaf2 != P::kNoLeaf && static_cast<std::size_t>(row.leaf2) == leaf)) {
            return row.id;
        }
    }
    return PU::kNoParamId;
}

}  // namespace

TEST_CASE("Profundum_MacroUiMap", "[profundum][ui]") {
    SECTION("TargetIdsElementWise") {
        const std::array<ParamID, 16> expected{200, 201, 302, 204, 205, 501, 602, 420,
                                               421, 423, 700, PU::kNoParamId, 206, 207, 500, 601};
        REQUIRE(PU::kMacroTargetParamIds.size() == expected.size());
        for (std::size_t t = 0; t < expected.size(); ++t) {
            INFO("target " << t << " (" << targetName(static_cast<ProfundumMacroTarget>(t)) << ")");
            CHECK(PU::kMacroTargetParamIds[t] == expected[t]);
        }
    }

    SECTION("HighlightMaskMatchesRows") {
        const std::array<IdSet, 4> literal{IdSet{200, 201, 302}, IdSet{204, 205, 602}, IdSet{420, 421, 423, 700},
                                           IdSet{206, 207, 500, 601}};
        IdSet unionIds;
        std::size_t sizeSum = 0;
        for (std::size_t mi = 0; mi < kAllMacros.size(); ++mi) {
            const ProfundumMacro m = kAllMacros[mi];
            INFO("macro " << macroName(m));
            const IdSet derived = derivedHighlightSet(m);
            const IdSet fromMask = maskToSet(PU::highlightMask(m));
            CHECK(fromMask == derived);
            CHECK(fromMask == literal[mi]);
            CHECK(derived == literal[mi]);
            CHECK_FALSE(fromMask.contains(PU::kNoParamId));
            sizeSum += fromMask.size();
            unionIds.insert(fromMask.begin(), fromMask.end());
        }
        // Pairwise disjoint <=> the union's size equals the sum of the sizes.
        CHECK(unionIds.size() == sizeSum);
        CHECK(unionIds.size() == 14);
    }

    SECTION("InertShapeRowNotHighlighted") {
        CHECK(ProfundumMacroMatrix::kRows[static_cast<std::size_t>(ProfundumMacroTarget::Shape)].amount == 0.0f);
        CHECK_FALSE(PU::highlightMask(ProfundumMacro::DarkBright)[501]);
        for (const ProfundumMacro m : kAllMacros) {
            INFO("macro " << macroName(m));
            CHECK_FALSE(PU::highlightMask(m)[501]);
        }
    }

    SECTION("PairwiseDisjoint") {
        std::size_t overlaps = 0;
        for (std::size_t a = 0; a < kAllMacros.size(); ++a) {
            const auto ma = PU::highlightMask(kAllMacros[a]);
            for (std::size_t b = a + 1; b < kAllMacros.size(); ++b) {
                const auto mb = PU::highlightMask(kAllMacros[b]);
                for (std::size_t id = 0; id < ma.size(); ++id) {
                    if (ma[id] && mb[id]) {
                        INFO("ID " << id << " highlighted by both " << macroName(kAllMacros[a]) << " and "
                                   << macroName(kAllMacros[b]));
                        CHECK(false);
                        ++overlaps;
                    }
                }
            }
        }
        CHECK(overlaps == 0);
    }
}

TEST_CASE("Profundum_UiContractTables", "[profundum][ui][.][probe]") {
    // ---- D3: one row per kRows row (FR-005) ----
    std::size_t d3Rows = 0;
    std::printf("# D3 GENERATED (Profundum_UiContractTables)\n");
    std::printf("row,macro,target,amount,curve,registered_id,no_control,inert\n");
    for (std::size_t t = 0; t < ProfundumMacroMatrix::kRows.size(); ++t) {
        const auto& row = ProfundumMacroMatrix::kRows[t];
        const ParamID id = PU::kMacroTargetParamIds[t];
        const bool noControl = id == PU::kNoParamId;
        const bool inert = row.amount == 0.0f;
        if (noControl) {
            std::printf("%zu,%s,%s,%.9g,%s,-,yes,%s\n", t, macroName(row.macro), targetName(row.target),
                        static_cast<double>(row.amount), curveName(row.curve), inert ? "yes" : "no");
        } else {
            std::printf("%zu,%s,%s,%.9g,%s,%u,no,%s\n", t, macroName(row.macro), targetName(row.target),
                        static_cast<double>(row.amount), curveName(row.curve), static_cast<unsigned>(id),
                        inert ? "yes" : "no");
        }
        ++d3Rows;
    }

    // ---- D4: one row per kLawTable leaf (FR-007) ----
    std::size_t d4Rows = 0;
    std::printf("# D4 GENERATED (Profundum_UiContractTables)\n");
    std::printf("leaf,name,law,axis,lo,hi,registered_id\n");
    const auto& laws = Krate::DSP::BassMorphEngine::kLawTable;
    for (std::size_t leaf = 0; leaf < laws.size(); ++leaf) {
        const auto& f = laws[leaf];
        const ParamID id = registeredIdOfLeaf(leaf);
        INFO("leaf " << leaf << " (" << f.name << ")");
        REQUIRE(id != PU::kNoParamId);
        REQUIRE(id < static_cast<ParamID>(P::kNumRoutedIds));
        REQUIRE(P::kParamRoutes[id].kind == P::RouteKind::Sound);
        const bool isFloat = f.kind == Krate::DSP::MorphState::Kind::F32;
        const double lo = isFloat ? static_cast<double>(f.lo) : 0.0;
        const double hi = isFloat ? static_cast<double>(f.hi) : static_cast<double>(f.maxEnum);
        std::printf("%zu,%s,%s,%s,%.9g,%.9g%s,%u\n", leaf, f.name, lawName(f.law), axisName(f.axis), lo, hi,
                    f.hiOpen ? " (open)" : "", static_cast<unsigned>(id));
        ++d4Rows;
    }
    std::fflush(stdout);

    REQUIRE(d3Rows == 16);
    REQUIRE(d4Rows == 62);
}

// ==============================================================================
// Profundum_MacroModulationRing (FR-061; SC-028 controller arm; plan §4.6.5, §5.5) — T023.
// The knob arm (SECTION "KnobsCarryRing", built tree) is T029's.
// ==============================================================================

namespace {

using ProfundumPluginTest::ControllerFixture;

constexpr std::array<ParamID, 4> kMacroIds{P::kMacroWeightId, P::kMacroDarkBrightId, P::kMacroTightLooseId,
                                           P::kMacroCleanViolentId};

void setAllMacrosNeutral(P::Controller& ctl) {
    for (const ParamID m : kMacroIds) {
        REQUIRE(ctl.setParamNormalized(m, 0.5) == Steinberg::kResultTrue);
    }
}

/// The test's own re-derivation of the ring of target t (bound to `id`): the row's contribution from kRows and
/// applyModCurve at the controller's current macro value, the base from the field table, the apply rule (amp times
/// in the log domain, everything else additive), then norm(mod) - norm(base).
float expectedRing(P::Controller& ctl, std::size_t t, ParamID id) {
    const auto& row = ProfundumMacroMatrix::kRows[t];
    if (row.target == ProfundumMacroTarget::CompressionAmount || row.amount == 0.0f) {
        return 0.0f;
    }
    const float m = static_cast<float>(ctl.getParamNormalized(kMacroIds[static_cast<std::size_t>(row.macro)]));
    const float u = (m - 0.5f) * 2.0f;
    const float c = row.amount * Krate::DSP::applyModCurve(row.curve, std::fabs(u)) * (u < 0.0f ? -1.0f : 1.0f);
    if (c == 0.0f) {
        return 0.0f;
    }
    REQUIRE(P::kParamRoutes[id].kind == P::RouteKind::Sound);
    const auto& f = Krate::DSP::BassMorphEngine::kLawTable[P::kSoundRows[P::kParamRoutes[id].index].leaf];
    const double base = P::plainFromNormalized(f, ctl.getParamNormalized(id));
    const bool ampTime = row.target == ProfundumMacroTarget::AmpAttackOct ||
                         row.target == ProfundumMacroTarget::AmpDecayOct ||
                         row.target == ProfundumMacroTarget::AmpReleaseOct;
    const double mod = ampTime ? std::clamp(base * std::exp2(static_cast<double>(c)),
                                            static_cast<double>(Krate::DSP::kMinEnvelopeTimeMs),
                                            static_cast<double>(Krate::DSP::kMaxEnvelopeTimeMs))
                               : std::clamp(base + static_cast<double>(c), static_cast<double>(f.lo),
                                            static_cast<double>(f.hi));
    return static_cast<float>(P::normalizedFromPlain(f, mod) - P::normalizedFromPlain(f, base));
}

// ---- T029 built-tree helpers ----

using ProfundumPluginTest::HeadlessEditor;

// The VST3Editor fills a bound control's value / menu through deferred updates (vst3editor.cpp:151-154); calling it
// before the editor exists also creates the UpdateHandler the parameters' dependents register with.
void flushDeferredUpdates() { Steinberg::UpdateHandler::instance()->triggerDeferedUpdates(); }

[[nodiscard]] bool isMacroTargetId(ParamID id) {
    return id != PU::kNoParamId &&
           std::find(PU::kMacroTargetParamIds.begin(), PU::kMacroTargetParamIds.end(), id) !=
               PU::kMacroTargetParamIds.end();
}

/// Every control of the built tree, classed the way the panel highlights it (panel_sub_controller.cpp
/// collectBoundControl): the ADSRDisplay by its control-point colour, an ArcKnob by its guide colour, a
/// BipolarSlider by its track colour; any other class cannot be highlighted.
struct HighlightView {
    enum class Kind : std::uint8_t { Arc, Bipolar, Adsr, Other };
    VSTGUI::CControl* control = nullptr;
    Kind kind = Kind::Other;
    std::int32_t tag = -1;
    VSTGUI::CColor original;  // the colour at editor open (nothing hovered, no gesture open)
};

[[nodiscard]] VSTGUI::CColor highlightColourOf(const HighlightView& v) {
    switch (v.kind) {
        case HighlightView::Kind::Arc:
            if (const auto* knob = dynamic_cast<const Krate::Plugins::ArcKnob*>(v.control)) {
                return knob->getGuideColor();
            }
            break;
        case HighlightView::Kind::Bipolar:
            if (const auto* slider = dynamic_cast<const Krate::Plugins::BipolarSlider*>(v.control)) {
                return slider->getTrackColor();
            }
            break;
        case HighlightView::Kind::Adsr:
            if (const auto* adsr = dynamic_cast<const Krate::Plugins::ADSRDisplay*>(v.control)) {
                return adsr->getControlPointColor();
            }
            break;
        case HighlightView::Kind::Other: break;
    }
    return {};
}

[[nodiscard]] std::vector<HighlightView> collectHighlightViews(VSTGUI::CFrame* frame) {
    std::vector<HighlightView> out;
    for (VSTGUI::CControl* c : ProfundumPluginTest::collectViewsOfType<VSTGUI::CControl>(frame)) {
        HighlightView v;
        v.control = c;
        v.tag = c->getTag();
        if (dynamic_cast<Krate::Plugins::ADSRDisplay*>(c) != nullptr) {
            v.kind = HighlightView::Kind::Adsr;
        } else if (dynamic_cast<Krate::Plugins::ArcKnob*>(c) != nullptr) {
            v.kind = HighlightView::Kind::Arc;
        } else if (dynamic_cast<Krate::Plugins::BipolarSlider*>(c) != nullptr) {
            v.kind = HighlightView::Kind::Bipolar;
        }
        v.original = highlightColourOf(v);
        out.push_back(v);
    }
    return out;
}

[[nodiscard]] bool inSet(const IdSet& ids, std::int32_t tag) {
    return tag >= 0 && ids.contains(static_cast<ParamID>(tag));
}

/// The number of controls whose drawn highlight state disagrees with @p active (the IDs that must show the
/// `highlight` token; every other control shows its original colour). A control bound to an ID of @p universe (the
/// union of the four sets) whose class the panel cannot highlight is a mismatch in every call.
[[nodiscard]] int highlightMismatches(const std::vector<HighlightView>& views, const IdSet& active,
                                      const IdSet& universe, const VSTGUI::CColor& highlight) {
    int bad = 0;
    for (const HighlightView& v : views) {
        bool want = false;
        switch (v.kind) {
            case HighlightView::Kind::Adsr:  // the display stands for the three amp times (FR-060)
                want = active.contains(P::kAmpAttackId) || active.contains(P::kAmpDecayId) ||
                       active.contains(P::kAmpReleaseId);
                break;
            case HighlightView::Kind::Arc:
            case HighlightView::Kind::Bipolar:
                want = inSet(active, v.tag) && inSet(universe, v.tag);
                break;
            case HighlightView::Kind::Other:
                if (inSet(universe, v.tag)) {
                    UNSCOPED_INFO("highlight-set ID " << v.tag << " is bound to a class the panel cannot highlight");
                    ++bad;
                }
                continue;
        }
        const VSTGUI::CColor shown = highlightColourOf(v);
        const VSTGUI::CColor expected = want ? highlight : v.original;
        if (shown != expected) {
            UNSCOPED_INFO("control tag " << v.tag << ": highlight " << (want ? "expected" : "not expected")
                                         << ", the colour disagrees");
            ++bad;
        }
    }
    return bad;
}

[[nodiscard]] IdSet toIdSet(const std::vector<ParamID>& ids) {
    return {ids.begin(), ids.end()};
}

/// A string attribute of a view (the tooltip), "" when it has none (editor_layout_test.cpp viewStringAttribute).
[[nodiscard]] std::string viewStringAttribute(const VSTGUI::CView* view, VSTGUI::CViewAttributeID id) {
    std::uint32_t size = 0;
    if (view == nullptr || !view->getAttributeSize(id, size) || size == 0) {
        return {};
    }
    std::vector<char> buf(size + 1, '\0');
    std::uint32_t outSize = 0;
    if (!view->getAttribute(id, size, buf.data(), outSize)) {
        return {};
    }
    return {buf.data()};
}

/// Every text a built view shows or offers: its tooltip, a label's / text edit's text, a text button's title, the
/// segment names of a segment button and the entries of an option menu.
void collectViewTexts(const VSTGUI::CView* view, std::vector<std::string>& out) {
    if (std::string tip = viewStringAttribute(view, VSTGUI::kCViewTooltipAttribute); !tip.empty()) {
        out.push_back(std::move(tip));
    }
    if (const auto* label = dynamic_cast<const VSTGUI::CTextLabel*>(view)) {
        out.push_back(label->getText().getString());
    }
    if (const auto* button = dynamic_cast<const VSTGUI::CTextButton*>(view)) {
        out.push_back(button->getTitle().getString());
    }
    if (const auto* segment = dynamic_cast<const VSTGUI::CSegmentButton*>(view)) {
        for (const auto& seg : segment->getSegments()) {
            out.push_back(seg.name.getString());
        }
    }
    if (const auto* menu = dynamic_cast<const VSTGUI::COptionMenu*>(view)) {
        for (std::int32_t i = 0; i < menu->getNbEntries(); ++i) {
            if (const VSTGUI::CMenuItem* item = menu->getEntry(i)) {
                out.push_back(item->getTitle().getString());
            }
        }
    }
}

}  // namespace

TEST_CASE("Profundum_MacroModulationRing", "[profundum][ui]") {
    SECTION("ControllerValue") {
        ControllerFixture cf;
        auto& ctl = *cf.controller;
        constexpr std::array<double, 5> kValues{0.0, 0.25, 0.5, 0.75, 1.0};
        std::size_t nonZeroRings = 0;
        std::size_t negativeRings = 0;
        for (std::size_t mi = 0; mi < kMacroIds.size(); ++mi) {
            for (const double v : kValues) {
                setAllMacrosNeutral(ctl);
                REQUIRE(ctl.setParamNormalized(kMacroIds[mi], v) == Steinberg::kResultTrue);
                for (std::size_t t = 0; t < PU::kMacroTargetParamIds.size(); ++t) {
                    const ParamID id = PU::kMacroTargetParamIds[t];
                    if (id == PU::kNoParamId) {
                        continue;
                    }
                    INFO("macro " << kMacroIds[mi] << " at " << v << ", target " << t << " ("
                                  << targetName(static_cast<ProfundumMacroTarget>(t)) << "), ID " << id);
                    const double before = ctl.getParamNormalized(id);
                    const float got = ctl.macroRingRange(id);
                    const float want = expectedRing(ctl, t, id);
                    CHECK(std::fabs(static_cast<double>(got) - static_cast<double>(want)) <= 1e-6);
                    CHECK(got >= -1.0f);
                    CHECK(got <= 1.0f);
                    if (v == 0.5) {
                        CHECK(got == 0.0f);
                    }
                    // A macro moves only its own rows (FR-052 ownership).
                    if (ProfundumMacroMatrix::kRows[t].macro != kAllMacros[mi]) {
                        CHECK(got == 0.0f);
                    }
                    CHECK(ctl.macroRingRange(id) == got);         // two evaluations are identical
                    CHECK(ctl.getParamNormalized(id) == before);  // the target's value is never written (FR-005)
                    if (got != 0.0f) {
                        ++nonZeroRings;
                    }
                    if (got < 0.0f) {
                        ++negativeRings;
                    }
                }
                // 501 (Shape, amount 0) and IDs outside the map carry no ring.
                CHECK(ctl.macroRingRange(P::kFoldShapeId) == 0.0f);
                for (const ParamID other : {ParamID{0}, ParamID{99}, ParamID{100}, ParamID{104}, ParamID{105},
                                            ParamID{300}, ParamID{1001}, ParamID{1003}, ParamID{9000},
                                            PU::kNoParamId}) {
                    INFO("ID " << other);
                    CHECK(ctl.macroRingRange(other) == 0.0f);
                }
            }
        }
        // Teeth: the defaults leave room to move, so the sweep saw rings on both sides.
        CHECK(nonZeroRings > 0u);
        CHECK(negativeRings > 0u);

        // Motion is not drawn: setting 900 (Motion Source) to Cycle changes no ring.
        setAllMacrosNeutral(ctl);
        REQUIRE(ctl.setParamNormalized(P::kMacroTightLooseId, 1.0) == Steinberg::kResultTrue);
        REQUIRE(ctl.setParamNormalized(P::kMacroCleanViolentId, 0.25) == Steinberg::kResultTrue);
        std::array<float, PU::kNumMacroTargets> withOff{};
        for (std::size_t t = 0; t < withOff.size(); ++t) {
            withOff[t] = ctl.macroRingRange(PU::kMacroTargetParamIds[t]);
        }
        // Motion Source labels: Off, Env, Cycle, Drift (motion_params.h:37) -> Cycle = 2 / 3.
        REQUIRE(ctl.setParamNormalized(P::kMotionSourceId, 2.0 / 3.0) == Steinberg::kResultTrue);
        for (std::size_t t = 0; t < withOff.size(); ++t) {
            INFO("target " << t);
            CHECK(ctl.macroRingRange(PU::kMacroTargetParamIds[t]) == withOff[t]);
        }
    }

    SECTION("HandCheckAmpAttack") {  // plan §5.5: unclamped log-law amp time, delta = c ln 2 / ln(tMax / tMin)
        ControllerFixture cf;
        auto& ctl = *cf.controller;
        const auto& f =
            Krate::DSP::BassMorphEngine::kLawTable[P::kSoundRows[P::kParamRoutes[P::kAmpAttackId].index].leaf];
        REQUIRE(ctl.setParamNormalized(P::kAmpAttackId, P::normalizedFromPlain(f, 100.0)) == Steinberg::kResultTrue);
        REQUIRE(std::fabs(P::plainFromNormalized(f, ctl.getParamNormalized(P::kAmpAttackId)) - 100.0) <= 1e-9);
        setAllMacrosNeutral(ctl);
        REQUIRE(ctl.setParamNormalized(P::kMacroTightLooseId, 1.0) == Steinberg::kResultTrue);
        const double want = 5.0 * std::numbers::ln2 / std::log(10000.0 / 0.1);
        CHECK(std::fabs(static_cast<double>(ctl.macroRingRange(P::kAmpAttackId)) - want) <= 1e-6);
        // Tight (0) mirrors it: 100 ms / 32 = 3.125 ms, still inside [0.1, 10000].
        REQUIRE(ctl.setParamNormalized(P::kMacroTightLooseId, 0.0) == Steinberg::kResultTrue);
        CHECK(std::fabs(static_cast<double>(ctl.macroRingRange(P::kAmpAttackId)) + want) <= 1e-6);
    }

    SECTION("RefreshFeedsRegisteredKnob") {  // refreshMacroRings (P-5): setOffset on each registered knob
        ControllerFixture cf;
        auto& ctl = *cf.controller;
        auto knob = VSTGUI::makeOwned<P::OffsetRingKnob>(VSTGUI::CRect(0, 0, 40, 40), nullptr,
                                                         static_cast<int32_t>(P::kAmpDecayId));
        setAllMacrosNeutral(ctl);
        ctl.registerRingKnob(P::kAmpDecayId, knob.get());
        CHECK(knob->getOffset() == 0.0f);  // neutral: no ring
        const float valueBefore = knob->getValueNormalized();

        REQUIRE(ctl.setParamNormalized(P::kMacroTightLooseId, 0.75) == Steinberg::kResultTrue);
        CHECK(knob->getOffset() == ctl.macroRingRange(P::kAmpDecayId));
        CHECK(knob->getOffset() != 0.0f);

        // A change of the target's own value refreshes too.
        REQUIRE(ctl.setParamNormalized(P::kAmpDecayId, 0.2) == Steinberg::kResultTrue);
        CHECK(knob->getOffset() == ctl.macroRingRange(P::kAmpDecayId));
        CHECK(knob->getValueNormalized() == valueBefore);  // the ring never moves the knob

        // Cleared (the editor closed): later changes leave the knob alone.
        ctl.clearRingKnobs();
        const float frozen = knob->getOffset();
        REQUIRE(ctl.setParamNormalized(P::kMacroTightLooseId, 0.0) == Steinberg::kResultTrue);
        CHECK(knob->getOffset() == frozen);
        CHECK(ctl.macroRingRange(P::kAmpDecayId) != frozen);
    }

    // SC-028 knob arm (T029; FR-061, P-5): every OffsetRingKnob of the BUILT tree carries its target's ring.
    SECTION("KnobsCarryRing") {
        ControllerFixture cf;
        auto& ctl = *cf.controller;
        flushDeferredUpdates();
        HeadlessEditor ed(cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        flushDeferredUpdates();
        REQUIRE(ctl.panelForTest() != nullptr);

        const auto ringKnobs = ProfundumPluginTest::collectViewsOfType<P::OffsetRingKnob>(ed.frame());
        REQUIRE_FALSE(ringKnobs.empty());
        std::map<ParamID, int> knobsPerId;
        std::vector<float> valuesBefore;
        for (const P::OffsetRingKnob* k : ringKnobs) {
            ++knobsPerId[static_cast<ParamID>(k->getTag())];
            valuesBefore.push_back(k->getValueNormalized());
        }
        // Teeth for a target drawn more than once (Fold Amount 500 sits on Main and Advanced): every copy is fed.
        CHECK(knobsPerId[P::kFoldAmountId] >= 2);

        // The ArcKnobs that are not OffsetRingKnobs (501 among them) never carry a ring.
        std::vector<Krate::Plugins::ArcKnob*> plainKnobs;
        bool sawShape = false;
        for (Krate::Plugins::ArcKnob* k : ProfundumPluginTest::collectViewsOfType<Krate::Plugins::ArcKnob>(ed.frame())) {
            if (dynamic_cast<P::OffsetRingKnob*>(k) == nullptr) {
                plainKnobs.push_back(k);
                sawShape = sawShape || k->getTag() == static_cast<std::int32_t>(P::kFoldShapeId);
            }
        }
        CHECK(sawShape);

        constexpr std::array<double, 5> kValues{0.0, 0.25, 0.5, 0.75, 1.0};  // the T023 sweep
        std::size_t positive = 0;
        std::size_t negative = 0;
        for (unsigned int kMacroId : kMacroIds) {
            for (const double v : kValues) {
                setAllMacrosNeutral(ctl);
                REQUIRE(ctl.setParamNormalized(kMacroId, v) == Steinberg::kResultTrue);
                flushDeferredUpdates();
                for (std::size_t i = 0; i < ringKnobs.size(); ++i) {
                    const P::OffsetRingKnob* knob = ringKnobs[i];
                    const auto id = static_cast<ParamID>(knob->getTag());
                    INFO("macro " << kMacroId << " at " << v << ", knob " << i << " bound to ID " << id);
                    const float got = knob->getOffset();
                    const float want = ctl.macroRingRange(id);
                    CHECK(std::fabs(static_cast<double>(got) - static_cast<double>(want)) <= 1e-6);
                    if (!isMacroTargetId(id) || id == P::kFoldShapeId) {
                        CHECK(got == 0.0f);
                    }
                    if (v == 0.5) {
                        CHECK(got == 0.0f);  // neutral: exactly no ring
                    }
                    const float value = knob->getValueNormalized();
                    CHECK(value == valuesBefore[i]);  // the ring never moves the knob
                    const auto span = knob->ringSpan();
                    if (got == 0.0f) {
                        CHECK_FALSE(span.has_value());
                        continue;
                    }
                    if (!span.has_value()) {
                        FAIL("a non-zero ring offset with no ring span");
                        continue;
                    }
                    CHECK(span->low <= span->high);
                    if (got > 0.0f) {  // one-sided (P-5): from the value upwards
                        CHECK(std::fabs(span->low - value) <= 1e-6f);
                        CHECK(std::fabs(span->high - std::min(1.0f, value + got)) <= 1e-6f);
                        ++positive;
                    } else {  // from the value downwards
                        CHECK(std::fabs(span->high - value) <= 1e-6f);
                        CHECK(std::fabs(span->low - std::max(0.0f, value + got)) <= 1e-6f);
                        ++negative;
                    }
                }
                for (const Krate::Plugins::ArcKnob* k : plainKnobs) {
                    INFO("plain ArcKnob bound to " << k->getTag());
                    CHECK(k->getModulationRange() == 0.0f);
                }
            }
        }
        CHECK(positive > 0u);  // non-vacuity: rings on both sides of the value were drawn
        CHECK(negative > 0u);
    }
}

// ==============================================================================
// Profundum_MacroHighlight (FR-060; SC-009; plan §4.7 "Macro highlight") — T029, built tree.
// Hover is a synthetic MouseEnter / MouseExit dispatched on the macro control (CView::dispatchEvent hands it to the
// panel's viewOnEvent, cview.cpp:666-676). The expected sets are derived here from kRows (derivedHighlightSet).
// ==============================================================================

TEST_CASE("Profundum_MacroHighlight", "[profundum][ui]") {
    ControllerFixture cf;
    auto& ctl = *cf.controller;
    flushDeferredUpdates();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    P::UI::ProfundumPanelSubController* panel = ctl.panelForTest();
    REQUIRE(panel != nullptr);

    VSTGUI::CColor highlight;
    REQUIRE(ed.editor()->getUIDescription() != nullptr);
    REQUIRE(ed.editor()->getUIDescription()->getColor("highlight", highlight));

    std::array<IdSet, 4> sets;
    IdSet universe;
    for (std::size_t mi = 0; mi < kAllMacros.size(); ++mi) {
        sets[mi] = derivedHighlightSet(kAllMacros[mi]);
        REQUIRE_FALSE(sets[mi].empty());
        universe.insert(sets[mi].begin(), sets[mi].end());
    }
    CHECK_FALSE(sets[1].contains(P::kFoldShapeId));  // Dark <-> Bright's set excludes 501 (amount 0)

    const std::vector<HighlightView> views = collectHighlightViews(ed.frame());
    const IdSet none;

    // Non-vacuity: every ID of every set has a highlightable control, and none starts in the highlight colour.
    for (const ParamID id : universe) {
        INFO("ID " << id);
        std::size_t drawable = 0;
        for (const HighlightView& v : views) {
            const bool adsrStandsFor =
                v.kind == HighlightView::Kind::Adsr &&
                (id == P::kAmpAttackId || id == P::kAmpDecayId || id == P::kAmpReleaseId);
            const bool bound = (v.kind == HighlightView::Kind::Arc || v.kind == HighlightView::Kind::Bipolar) &&
                               std::cmp_equal(v.tag, id);
            if (adsrStandsFor || bound) {
                ++drawable;
                CHECK(v.original != highlight);
            }
        }
        CHECK(drawable > 0u);
    }
    CHECK(highlightMismatches(views, none, universe, highlight) == 0);
    CHECK(panel->highlightedIdsForTest().empty());

    // The macro controls 100 - 103 of the tree.
    std::array<std::vector<VSTGUI::CControl*>, 4> macroControls;
    for (const HighlightView& v : views) {
        for (std::size_t mi = 0; mi < kMacroIds.size(); ++mi) {
            if (std::cmp_equal(v.tag, kMacroIds[mi])) {
                macroControls[mi].push_back(v.control);
            }
        }
    }

    SECTION("HoverShowsSet") {
        for (std::size_t mi = 0; mi < kMacroIds.size(); ++mi) {
            INFO("macro " << macroName(kAllMacros[mi]));
            REQUIRE_FALSE(macroControls[mi].empty());
            for (VSTGUI::CControl* c : macroControls[mi]) {
                VSTGUI::MouseEnterEvent enter;
                c->dispatchEvent(enter);
                CHECK(highlightMismatches(views, sets[mi], universe, highlight) == 0);
                CHECK(toIdSet(panel->highlightedIdsForTest()) == sets[mi]);
                CHECK_FALSE(toIdSet(panel->highlightedIdsForTest()).contains(P::kFoldShapeId));

                VSTGUI::MouseExitEvent exit;
                c->dispatchEvent(exit);
                CHECK(highlightMismatches(views, none, universe, highlight) == 0);
                CHECK(panel->highlightedIdsForTest().empty());
            }
        }
    }

    SECTION("GestureWithoutHover") {  // plan 4.6.4: beginEdit / endEdit on 100 - 103 hold the highlight
        for (std::size_t mi = 0; mi < kMacroIds.size(); ++mi) {
            INFO("macro " << macroName(kAllMacros[mi]));
            REQUIRE(ctl.beginEdit(kMacroIds[mi]) == Steinberg::kResultOk);
            CHECK(highlightMismatches(views, sets[mi], universe, highlight) == 0);
            CHECK(toIdSet(panel->highlightedIdsForTest()) == sets[mi]);
            REQUIRE(ctl.endEdit(kMacroIds[mi]) == Steinberg::kResultOk);
            CHECK(highlightMismatches(views, none, universe, highlight) == 0);
            CHECK(panel->highlightedIdsForTest().empty());
        }
    }

    SECTION("ExitMidGestureKeepsHighlight") {
        for (std::size_t mi = 0; mi < kMacroIds.size(); ++mi) {
            INFO("macro " << macroName(kAllMacros[mi]));
            REQUIRE_FALSE(macroControls[mi].empty());
            VSTGUI::CControl* c = macroControls[mi].front();
            VSTGUI::MouseEnterEvent enter;
            c->dispatchEvent(enter);
            REQUIRE(ctl.beginEdit(kMacroIds[mi]) == Steinberg::kResultOk);
            VSTGUI::MouseExitEvent exit;
            c->dispatchEvent(exit);
            CHECK(highlightMismatches(views, sets[mi], universe, highlight) == 0);  // the open gesture keeps it
            CHECK(toIdSet(panel->highlightedIdsForTest()) == sets[mi]);
            REQUIRE(ctl.endEdit(kMacroIds[mi]) == Steinberg::kResultOk);
            CHECK(highlightMismatches(views, none, universe, highlight) == 0);
            CHECK(panel->highlightedIdsForTest().empty());
        }
    }

    SECTION("KeyboardOrWheelEdit") {  // begin / perform / end through the control, no hover
        for (std::size_t mi = 0; mi < kMacroIds.size(); ++mi) {
            INFO("macro " << macroName(kAllMacros[mi]));
            REQUIRE_FALSE(macroControls[mi].empty());
            VSTGUI::CControl* c = macroControls[mi].front();
            c->beginEdit();
            c->setValueNormalized(0.75f);
            c->valueChanged();
            CHECK(highlightMismatches(views, sets[mi], universe, highlight) == 0);
            CHECK(toIdSet(panel->highlightedIdsForTest()) == sets[mi]);
            c->endEdit();
            CHECK(highlightMismatches(views, none, universe, highlight) == 0);
            CHECK(panel->highlightedIdsForTest().empty());
        }
    }

    SECTION("HoverAndGestureUnion") {  // two active macros show the union of their (disjoint) sets
        REQUIRE_FALSE(macroControls[0].empty());
        VSTGUI::MouseEnterEvent enter;
        macroControls[0].front()->dispatchEvent(enter);
        REQUIRE(ctl.beginEdit(kMacroIds[2]) == Steinberg::kResultOk);
        IdSet both = sets[0];
        both.insert(sets[2].begin(), sets[2].end());
        CHECK(highlightMismatches(views, both, universe, highlight) == 0);
        CHECK(toIdSet(panel->highlightedIdsForTest()) == both);
        VSTGUI::MouseExitEvent exit;
        macroControls[0].front()->dispatchEvent(exit);
        CHECK(toIdSet(panel->highlightedIdsForTest()) == sets[2]);
        REQUIRE(ctl.endEdit(kMacroIds[2]) == Steinberg::kResultOk);
        CHECK(highlightMismatches(views, none, universe, highlight) == 0);
        CHECK(panel->highlightedIdsForTest().empty());
    }
}

// ==============================================================================
// Profundum_MidiLearnHostLevel (FR-098; SC-029) — T029. MIDI learn is the host's: 100 - 103 and 105 are
// automatable and writable, the controller does not implement IMidiLearn, and the UI offers no "Learn".
// The Phase 11 IMidiMapping cases (midi_mapping_test.cpp) are left unedited.
// ==============================================================================

TEST_CASE("Profundum_MidiLearnHostLevel", "[profundum][ui]") {
    ControllerFixture cf;
    auto& ctl = *cf.controller;

    const IdSet wanted{P::kMacroWeightId, P::kMacroDarkBrightId, P::kMacroTightLooseId, P::kMacroCleanViolentId,
                       P::kMorphPositionId};
    IdSet seen;
    const Steinberg::int32 count = ctl.getParameterCount();
    REQUIRE(count > 0);
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (!wanted.contains(info.id)) {
            continue;
        }
        INFO("ID " << info.id);
        seen.insert(info.id);
        CHECK((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly) == 0);
    }
    CHECK(seen == wanted);

    void* obj = nullptr;
    CHECK(ctl.queryInterface(Steinberg::Vst::IMidiLearn::iid, &obj) == Steinberg::kNoInterface);
    CHECK(obj == nullptr);
    // Non-vacuity: the same query path does find an implemented interface.
    obj = nullptr;
    REQUIRE(ctl.queryInterface(Steinberg::Vst::IMidiMapping::iid, &obj) == Steinberg::kResultOk);
    REQUIRE(obj != nullptr);
    static_cast<Steinberg::Vst::IMidiMapping*>(obj)->release();  // queryInterface added a reference

    flushDeferredUpdates();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    std::vector<std::string> texts;
    for (const VSTGUI::CView* view : ProfundumPluginTest::collectViewsOfType<VSTGUI::CView>(ed.frame())) {
        collectViewTexts(view, texts);
    }
    REQUIRE(texts.size() > 10u);  // non-vacuity: the walk read the tree's tooltips and labels
    std::size_t learn = 0;
    for (const std::string& t : texts) {
        if (t.find("Learn") != std::string::npos) {
            UNSCOPED_INFO("built-tree text offers MIDI learn: \"" << t << "\"");
            ++learn;
        }
    }
    CHECK(learn == 0u);
}
