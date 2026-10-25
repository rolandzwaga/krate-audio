// ==============================================================================
// Profundum Phase 12 - editor layout, binding, labels, placement and zoom (T027)
// ==============================================================================
// Spec:  specs/profundum-phase12-ui/spec.md  (FR-004, FR-010 - FR-014, FR-020, FR-022, FR-024, FR-030 - FR-036,
//        FR-045, FR-050 - FR-052, FR-070, FR-080, FR-093; SC-002 - SC-005, SC-017, SC-019, SC-031, SC-032)
// Plan:  specs/profundum-phase12-ui/plan.md  (sections 4.12, 5.6, 6.2 "Editor layout and binding")
// Design: D1 main-page.md, D2 advanced-page.md, D6 interaction-automation.md, D7 accessibility-scaling.md; every
//        ruled literal comes from ui_strings_test_table.h, which copies them byte for byte.
// Tasks: specs/profundum-phase12-ui/tasks.md (T027)
//
// Two readings of the editor (tasks.md Deviation 7):
//   - the uidesc XML (scanUidesc, ui_tree_helpers.h) for containers and placement: uidesc-label is a parse-time
//     attribute, so section / page membership is read from the description;
//   - the BUILT tree of a headless VST3Editor (HeadlessEditor) for binding, classes, visibility, geometry, fonts,
//     tooltips and colours. The VST3Editor fills the segments / entries of the second and later controls bound to
//     one ID through a deferred update, so every built-tree case flushes the UpdateHandler first.
//
// "Main region" = the header plus page-main (D1: the header carries voice mode 1, glide 412 and the zoom);
// "Advanced region" = page-advanced. A Main-region control is never inside page-advanced.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"
#include "ui_strings_test_table.h"
#include "ui_tree_helpers.h"

#include "test_helpers/uidesc_reachability.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "processor/spectrum_frame.h"
#include "ui/harmonic_bar_view.h"
#include "ui/macro_ui_map.h"
#include "ui/offset_ring_knob.h"
#include "ui/outline_button.h"       // plugins/shared/src/ui: OutlineBrowserButton (Phase 13 T014)
#include "ui/panel_sub_controller.h"
#include "ui/preset_browser_view.h"  // plugins/shared/src/ui (Phase 13 T013 walker stop)
#include "ui/spread_curve_view.h"

// plugins/shared/src/ui
#include "ui/adsr_display.h"
#include "ui/arc_knob.h"
#include "ui/bipolar_slider.h"

#include "base/source/updatehandler.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/cparamdisplay.h"
#include "vstgui/lib/controls/csegmentbutton.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/viewcreator/viewcreator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using Steinberg::Vst::ParamID;

constexpr std::int32_t kSessionBase = P::UI::kSessionTagBase;  // 9000: every tag at or above it is UI-only
/// Phase 13 T014: the header preset button's tooltip (editor.uidesc, the PresetBrowserButton node); not a D1 row.
constexpr const char* kPresetButtonTooltip = "Browse and load factory and user presets.";

// ==============================================================================
// Shared helpers
// ==============================================================================

[[nodiscard]] std::string readUidesc() {
    std::ifstream in(std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc", std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// The VST3Editor adds the second and later controls of one ID through Parameter::deferUpdate.
void flushDeferredUpdates() { Steinberg::UpdateHandler::instance()->triggerDeferedUpdates(); }

[[nodiscard]] std::vector<int> registeredIds(P::Controller& ctl) {
    std::vector<int> ids;
    const Steinberg::int32 n = ctl.getParameterCount();
    for (Steinberg::int32 i = 0; i < n; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        ids.push_back(static_cast<int>(info.id));
    }
    return ids;
}

[[nodiscard]] std::set<int> visibleIds(P::Controller& ctl) {
    std::set<int> out;
    for (const int id : registeredIds(ctl)) {
        if (std::find(kHiddenPerformanceIds.begin(), kHiddenPerformanceIds.end(), id) == kHiddenPerformanceIds.end()) {
            out.insert(id);
        }
    }
    return out;
}

[[nodiscard]] std::string asciiOf(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

/// The registered list strings of @p id, index 0 .. stepCount (FR-036).
[[nodiscard]] std::vector<std::string> registeredListStrings(P::Controller& ctl, ParamID id) {
    Steinberg::Vst::Parameter* param = ctl.getParameterObject(id);
    REQUIRE(param != nullptr);
    const Steinberg::int32 steps = param->getInfo().stepCount;
    REQUIRE(steps > 0);
    std::vector<std::string> out;
    for (Steinberg::int32 i = 0; i <= steps; ++i) {
        Steinberg::Vst::String128 s{};
        param->toString(param->toNormalized(static_cast<Steinberg::Vst::ParamValue>(i)), s);
        out.push_back(asciiOf(s));
    }
    return out;
}

[[nodiscard]] std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

[[nodiscard]] bool containsNoCase(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

template <std::size_t N>
[[nodiscard]] const char* findTooltip(const std::array<UiTooltip, N>& table, std::int32_t tag) {
    for (const UiTooltip& t : table) {
        if (t.tag == tag) {
            return t.text;
        }
    }
    return nullptr;
}

[[nodiscard]] const char* ruledTooltip(bool advanced, std::int32_t tag) {
    return advanced ? findTooltip(kAdvancedTooltips, tag) : findTooltip(kMainTooltips, tag);
}

// ==============================================================================
// Built-tree helpers
// ==============================================================================

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

[[nodiscard]] std::string uidescLabelOf(const VSTGUI::CView* view) {
    return viewStringAttribute(view, VSTGUI::UIViewCreator::ViewCreator::labelAttrID);
}

[[nodiscard]] std::string tooltipOf(const VSTGUI::CView* view) {
    return viewStringAttribute(view, VSTGUI::kCViewTooltipAttribute);
}

struct BuiltView {
    VSTGUI::CView* view = nullptr;
    VSTGUI::CViewContainer* parent = nullptr;
    int parentIndex = -1;           // index into the walk, -1 for a direct child of the frame
    VSTGUI::CRect window;           // frame coordinates at 100 %
    bool visible = false;           // effective: the view and every ancestor visible
    std::vector<std::string> labels;  // uidesc-labels of the ancestors and the view, outermost first

    [[nodiscard]] bool under(const std::string& label) const {
        return std::find(labels.begin(), labels.end(), label) != labels.end();
    }
    [[nodiscard]] bool advanced() const { return under("page-advanced"); }
    [[nodiscard]] VSTGUI::CControl* control() const { return dynamic_cast<VSTGUI::CControl*>(view); }
    [[nodiscard]] std::int32_t tag() const {
        const VSTGUI::CControl* c = control();
        return c == nullptr ? -1 : c->getTag();
    }
};

void walkBuilt(VSTGUI::CViewContainer* container, int containerIndex, VSTGUI::CPoint offset, bool visible,
               const std::vector<std::string>& labels, std::vector<BuiltView>& out) {
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr) {
            continue;
        }
        BuiltView bv;
        bv.view = view;
        bv.parent = container;
        bv.parentIndex = containerIndex;
        bv.window = view->getViewSize();
        bv.window.offset(offset.x, offset.y);
        bv.visible = visible && view->isVisible();
        bv.labels = labels;
        const std::string own = uidescLabelOf(view);
        if (!own.empty()) {
            bv.labels.push_back(own);
        }
        // Copies, not references into `out`: the recursion appends to it and may reallocate.
        const VSTGUI::CPoint childOffset = bv.window.getTopLeft();
        const bool childVisible = bv.visible;
        const std::vector<std::string> childLabels = bv.labels;
        out.push_back(std::move(bv));
        const int index = static_cast<int>(out.size()) - 1;
        // Phase 13 T013: never descend into the PresetBrowserView overlay (its own tagged buttons, 1-5 / 10-31).
        if (dynamic_cast<Krate::Plugins::PresetBrowserView*>(view) != nullptr) {
            continue;
        }
        if (auto* child = view->asViewContainer()) {
            walkBuilt(child, index, childOffset, childVisible, childLabels, out);
        }
    }
}

[[nodiscard]] std::vector<BuiltView> builtTree(VSTGUI::CFrame* frame) {
    std::vector<BuiltView> out;
    REQUIRE(frame != nullptr);
    walkBuilt(frame, -1, VSTGUI::CPoint(0.0, 0.0), true, {}, out);
    return out;
}

[[nodiscard]] bool isTaggedControl(const BuiltView& bv) { return bv.tag() >= 0; }
[[nodiscard]] bool isBoundControl(const BuiltView& bv) { return bv.tag() >= 0 && bv.tag() < kSessionBase; }
[[nodiscard]] bool isCustomView(const BuiltView& bv) {
    return dynamic_cast<P::UI::HarmonicBarView*>(bv.view) != nullptr ||
           dynamic_cast<P::UI::SpreadCurveView*>(bv.view) != nullptr;
}
/// Exactly CParamDisplay: the P-8 display-only readout (not a CTextLabel, CTextEdit or COptionMenu).
[[nodiscard]] bool isPlainReadout(const VSTGUI::CView* view) {
    return view != nullptr && typeid(*view) == typeid(VSTGUI::CParamDisplay);
}

/// Every string a view shows: tooltip, label / edit text, menu entries, segment names, button title.
[[nodiscard]] std::vector<std::string> textsOf(VSTGUI::CView* view) {
    std::vector<std::string> out;
    if (std::string t = tooltipOf(view); !t.empty()) {
        out.push_back(std::move(t));
    }
    if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(view)) {
        out.push_back(label->getText().getString());
    }
    if (auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(view)) {
        for (std::int32_t i = 0; i < menu->getNbEntries(); ++i) {
            if (const VSTGUI::CMenuItem* item = menu->getEntry(i)) {
                out.push_back(item->getTitle().getString());
            }
        }
    }
    if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(view)) {
        for (const auto& s : segment->getSegments()) {
            out.push_back(s.name.getString());
        }
    }
    if (auto* button = dynamic_cast<VSTGUI::CTextButton*>(view)) {
        out.push_back(button->getTitle().getString());
    }
    return out;
}

/// The strings a list control shows (segment names or menu entries).
[[nodiscard]] std::vector<std::string> shownListStrings(VSTGUI::CView* view) {
    std::vector<std::string> out;
    if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(view)) {
        for (const auto& s : segment->getSegments()) {
            out.push_back(s.name.getString());
        }
    } else if (auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(view)) {
        for (std::int32_t i = 0; i < menu->getNbEntries(); ++i) {
            if (const VSTGUI::CMenuItem* item = menu->getEntry(i)) {
                out.push_back(item->getTitle().getString());
            }
        }
    }
    return out;
}

// ---- contrast (plan 5.6; D7 4) ----

[[nodiscard]] double linearChannel(std::uint8_t c) {
    const double s = static_cast<double>(c) / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double relativeLuminance(const VSTGUI::CColor& c) {
    return 0.2126 * linearChannel(c.red) + 0.7152 * linearChannel(c.green) + 0.0722 * linearChannel(c.blue);
}

[[nodiscard]] double contrastRatio(const VSTGUI::CColor& a, const VSTGUI::CColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// The background of the nearest opaque ancestor container of tree[index].
[[nodiscard]] std::optional<VSTGUI::CColor> opaqueBackground(const std::vector<BuiltView>& tree, int index) {
    int p = tree[static_cast<std::size_t>(index)].parentIndex;
    while (p >= 0) {
        const BuiltView& a = tree[static_cast<std::size_t>(p)];
        if (const auto* container = a.view->asViewContainer()) {
            VSTGUI::CColor bg = container->getBackgroundColor();
            if (!container->getTransparency() && bg.alpha == 255) {
                return bg;
            }
        }
        p = a.parentIndex;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<VSTGUI::CColor> firstStop(const VSTGUI::CGradient* gradient) {
    if (gradient == nullptr || gradient->getColorStops().empty()) {
        return std::nullopt;
    }
    return gradient->getColorStops().begin()->second;
}

/// (text colour, background colour) pairs of a text-bearing view; empty for a view that draws no text.
[[nodiscard]] std::vector<std::pair<VSTGUI::CColor, VSTGUI::CColor>> textColourPairs(const std::vector<BuiltView>& tree,
                                                                                    int index) {
    std::vector<std::pair<VSTGUI::CColor, VSTGUI::CColor>> out;
    VSTGUI::CView* view = tree[static_cast<std::size_t>(index)].view;
    if (dynamic_cast<VSTGUI::CSegmentButton*>(view) == nullptr && dynamic_cast<VSTGUI::CTextButton*>(view) == nullptr &&
        dynamic_cast<VSTGUI::CParamDisplay*>(view) == nullptr) {
        return out;  // draws no text
    }
    const std::optional<VSTGUI::CColor> ancestor = opaqueBackground(tree, index);
    REQUIRE(ancestor.has_value());  // every text view sits on an opaque container
    if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(view)) {
        out.emplace_back(segment->getTextColor(), firstStop(segment->getGradient()).value_or(*ancestor));
        out.emplace_back(segment->getTextColorHighlighted(),
                         firstStop(segment->getGradientHighlighted()).value_or(*ancestor));
    } else if (auto* button = dynamic_cast<VSTGUI::CTextButton*>(view)) {
        out.emplace_back(button->getTextColor(), firstStop(button->getGradient()).value_or(*ancestor));
        out.emplace_back(button->getTextColorHighlighted(),
                         firstStop(button->getGradientHighlighted()).value_or(*ancestor));
    } else if (auto* display = dynamic_cast<VSTGUI::CParamDisplay*>(view)) {
        out.emplace_back(display->getFontColor(), display->getTransparency() ? *ancestor : display->getBackColor());
    }
    return out;
}

/// The font of a text-bearing view, nullptr for one that draws no text.
[[nodiscard]] const VSTGUI::CFontDesc* fontOf(VSTGUI::CView* view) {
    if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(view)) {
        return segment->getFont();
    }
    if (auto* button = dynamic_cast<VSTGUI::CTextButton*>(view)) {
        return button->getFont();
    }
    if (auto* display = dynamic_cast<VSTGUI::CParamDisplay*>(view)) {
        return display->getFont();
    }
    return nullptr;
}

[[nodiscard]] bool strictlyOverlap(const VSTGUI::CRect& a, const VSTGUI::CRect& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// ---- spectrum frame delivery (the DataExchange consumer, plan 4.6.1) ----

[[nodiscard]] P::ProfundumSpectrumFrame testFrame(std::uint32_t sequence) {
    P::ProfundumSpectrumFrame f = P::Controller::silenceFrame();
    f.sequence = sequence;
    f.flags = 0;
    f.sampleRate = 48000.0f;
    f.f0Hz = 41.2f;
    f.anchorPower = 0.25f;
    f.windowSamples = 9320;
    f.numPartials = 8;
    for (std::size_t i = 0; i < 8u; ++i) {
        const auto n = static_cast<float>(i + 1u);
        f.partialPower[i] = 0.5f / (n * n);
    }
    return f;
}

void deliverFrame(P::Controller& ctl, P::ProfundumSpectrumFrame& frame) {
    Steinberg::Vst::DataExchangeBlock block{};
    block.data = &frame;
    block.size = static_cast<Steinberg::uint32>(sizeof(P::ProfundumSpectrumFrame));
    block.blockID = 0;
    ctl.onDataExchangeBlocksReceived(P::kProfundumFrameUserContextId, 1u, &block, 0);
}

// ---- XML helpers ----

[[nodiscard]] int singleLabelled(const XmlScan& scan, const std::string& label) {
    const std::vector<int> nodes = findLabelled(scan, label);
    INFO("uidesc-label " << label);
    REQUIRE(nodes.size() == 1u);
    return nodes[0];
}

[[nodiscard]] bool hasDescendantBoundTo(const XmlScan& scan, const std::vector<std::pair<int, int>>& bound,
                                        int ancestor, int id) {
    return std::ranges::any_of(bound, [&scan, ancestor, id](const std::pair<int, int>& b) {
        return b.second == id && isDescendantOf(scan, b.first, ancestor);
    });
}

[[nodiscard]] bool hasDescendantWithSessionTag(const XmlScan& scan, int ancestor, int tag) {
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        if (scan.nodes[i].get("session-tag") == std::to_string(tag) && isDescendantOf(scan, static_cast<int>(i), ancestor)) {
            return true;
        }
    }
    return false;
}

/// The nearest enclosing FR-004 container of a node ("section-*", "tab-modulation", "tab-settings"), or "".
[[nodiscard]] std::string fr004ContainerOf(const XmlScan& scan, int node) {
    int p = scan.nodes[static_cast<std::size_t>(node)].parent;
    while (p >= 0) {
        const std::string label = scan.nodes[static_cast<std::size_t>(p)].get("uidesc-label");
        if (label.starts_with("section-") || label == "tab-modulation" || label == "tab-settings") {
            return label;
        }
        p = scan.nodes[static_cast<std::size_t>(p)].parent;
    }
    return {};
}

[[nodiscard]] bool fr004Lists(const std::string& container, int tag) {
    return std::any_of(kFr004Members.begin(), kFr004Members.end(),
                       [&](const SectionMember& m) { return container == m.container && m.tag == tag; });
}

[[nodiscard]] std::string joinLines(const std::vector<std::string>& lines, std::size_t limit = 40) {
    std::string out;
    for (std::size_t i = 0; i < lines.size() && i < limit; ++i) {
        out += lines[i];
        out += '\n';
    }
    if (lines.size() > limit) {
        out += "... (" + std::to_string(lines.size() - limit) + " more)\n";
    }
    return out;
}

}  // namespace

// ==============================================================================
// SC-002 - binding completeness
// ==============================================================================

TEST_CASE("Profundum_UidescBindsEverySurfaceId", "[profundum][ui]") {
    ControllerFixture cf;
    const std::vector<int> all = registeredIds(*cf.controller.get());
    REQUIRE(all.size() == 84u);
    const std::string xml = readUidesc();

    const std::set<int> allow(kHiddenPerformanceIds.begin(), kHiddenPerformanceIds.end());
    const std::vector<int> missing = Krate::Test::unreachableParams(xml, all, allow);
    INFO("unbound IDs: " << missing.size());
    CHECK(missing.empty());

    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    std::vector<VSTGUI::CControl*> controls;
    collectBoundControls(ed.frame(), controls);
    std::set<int> boundTags;
    for (const VSTGUI::CControl* c : controls) {
        if (c->getTag() < kSessionBase) {
            boundTags.insert(c->getTag());
        }
    }
    const std::set<int> expected = visibleIds(*cf.controller.get());
    CHECK(expected.size() == 80u);
    CHECK(boundTags.size() == 80u);
    CHECK(boundTags == expected);
    for (const std::int32_t hidden : kHiddenPerformanceIds) {
        CAPTURE(hidden);
        CHECK_FALSE(boundTags.contains(hidden));
    }
}

// ==============================================================================
// SC-003 - tag integrity
// ==============================================================================

TEST_CASE("Profundum_UidescTagIntegrity", "[profundum][ui]") {
    ControllerFixture cf;
    const std::string xml = readUidesc();
    const std::map<std::string, int> tagMap = Krate::Test::extractControlTagMap(xml);

    CHECK(tagMap.size() == 80u);
    std::set<int> values;
    for (const auto& [name, tag] : tagMap) {
        CAPTURE(name, tag);
        CHECK(tag < kSessionBase);
        CHECK(cf.controller->getParameterObject(static_cast<ParamID>(tag)) != nullptr);
        CHECK(std::find(kHiddenPerformanceIds.begin(), kHiddenPerformanceIds.end(), tag) ==
              kHiddenPerformanceIds.end());
        values.insert(tag);
    }
    CHECK(values.size() == tagMap.size());  // one name per ID
    CHECK(values == visibleIds(*cf.controller.get()));

    for (const std::string& ref : Krate::Test::extractReferencedTagNames(xml)) {
        CAPTURE(ref);
        CHECK(tagMap.count(ref) == 1u);
    }

    for (const TagName& t : kPhase10TagNames) {  // FR-033
        CAPTURE(t.name);
        const auto it = tagMap.find(t.name);
        REQUIRE(it != tagMap.end());
        CHECK(it->second == t.tag);
    }
}

// ==============================================================================
// SC-004 - class rule (FR-032 as amended by P-8; P-5 ring knobs; 417 steps)
// ==============================================================================

TEST_CASE("Profundum_UiClassRule", "[profundum][ui]") {
    ControllerFixture cf;
    P::Controller& ctl = *cf.controller.get();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    const std::vector<BuiltView> tree = builtTree(ed.frame());

    std::vector<std::string> failures;
    std::set<std::pair<bool, int>> writers;   // (advanced, id) bound to a conforming writing control
    std::set<std::pair<bool, int>> readouts;  // (advanced, id) of the display-only CParamDisplays
    int knobs417 = 0;
    std::size_t boundCount = 0;

    for (const BuiltView& bv : tree) {
        if (!isBoundControl(bv)) {
            continue;
        }
        ++boundCount;
        const auto id = static_cast<ParamID>(bv.tag());
        Steinberg::Vst::Parameter* param = ctl.getParameterObject(id);
        if (param == nullptr) {
            failures.push_back("unregistered tag " + std::to_string(id));
            continue;
        }
        const std::string where = std::to_string(id) + (bv.advanced() ? " (advanced) " : " (main) ") +
                                  typeid(*bv.view).name();
        const Steinberg::int32 steps = param->getInfo().stepCount;
        if (dynamic_cast<Steinberg::Vst::StringListParameter*>(param) != nullptr) {
            const bool menu = dynamic_cast<VSTGUI::COptionMenu*>(bv.view) != nullptr;
            const auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(bv.view);
            const bool toggle = (dynamic_cast<VSTGUI::COnOffButton*>(bv.view) != nullptr ||
                                 dynamic_cast<VSTGUI::CCheckBox*>(bv.view) != nullptr) &&
                                steps == 1;
            if (!menu && segment == nullptr && !toggle) {
                failures.push_back("list bound to a non-list class: " + where);
            }
            if (segment != nullptr && segment->getSegments().size() != static_cast<std::size_t>(steps) + 1u) {
                failures.push_back("segment count != list size: " + where);
            }
            if (const auto* m = dynamic_cast<VSTGUI::COptionMenu*>(bv.view);
                m != nullptr && m->getNbEntries() != steps + 1) {
                failures.push_back("menu entries != list size: " + where);
            }
            writers.insert({bv.advanced(), static_cast<int>(id)});
            continue;
        }
        if (isPlainReadout(bv.view)) {  // P-8: display-only, needs a writer of the same ID on the same page
            readouts.insert({bv.advanced(), static_cast<int>(id)});
            continue;
        }
        const bool arc = dynamic_cast<Krate::Plugins::ArcKnob*>(bv.view) != nullptr;
        const bool conforming = arc || dynamic_cast<Krate::Plugins::BipolarSlider*>(bv.view) != nullptr ||
                                dynamic_cast<VSTGUI::CSlider*>(bv.view) != nullptr ||
                                dynamic_cast<Krate::Plugins::ADSRDisplay*>(bv.view) != nullptr;
        if (!conforming) {
            failures.push_back("continuous ID bound to a non-conforming class: " + where);
            continue;
        }
        writers.insert({bv.advanced(), static_cast<int>(id)});

        // P-5: every knob of a macro target (except 501) draws the one-sided ring; no other knob does.
        if (arc) {
            const bool target = id != P::kFoldShapeId &&
                                std::find(P::UI::kMacroTargetParamIds.begin(), P::UI::kMacroTargetParamIds.end(), id) !=
                                    P::UI::kMacroTargetParamIds.end();
            const bool ring = dynamic_cast<P::OffsetRingKnob*>(bv.view) != nullptr;
            if (target != ring) {
                failures.push_back(std::string(target ? "macro target knob is not an OffsetRingKnob: "
                                                      : "non-target knob is an OffsetRingKnob: ") +
                                   where);
            }
        }
        // 417 honours its step count: the VST3Editor binds a stepped parameter in plain step units.
        if (id == P::kNotePitchBendRangeId) {
            ++knobs417;
            const auto* control = bv.control();
            CHECK(steps > 0);
            CHECK(control->getMin() == Catch::Approx(0.0f));
            CHECK(control->getMax() == Catch::Approx(static_cast<float>(steps)));
        }
    }

    for (const auto& r : readouts) {
        CAPTURE(r.first, r.second);
        CHECK(writers.count(r) == 1u);  // a readout without a writer on its page fails (P-8)
    }
    const std::set<std::pair<bool, int>> expectedReadouts{{false, static_cast<int>(P::kMorphPositionId)},
                                                          {false, static_cast<int>(P::kNotePortamentoId)}};
    CHECK(readouts == expectedReadouts);
    CHECK(knobs417 == 1);
    CHECK(boundCount >= 80u);
    INFO(joinLines(failures));
    CHECK(failures.empty());
}

// ==============================================================================
// SC-005 - labels and statements
// ==============================================================================

TEST_CASE("Profundum_UiLabels", "[profundum][ui]") {
    ControllerFixture cf;
    P::Controller& ctl = *cf.controller.get();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    const std::vector<BuiltView> tree = builtTree(ed.frame());

    // ---- 301 and 1000 show the registered strings, and the two sets are disjoint (FR-036, FR-045) ----
    const std::vector<std::string> anchorMode = registeredListStrings(ctl, P::kAnchorModeId);
    const std::vector<std::string> morphMode = registeredListStrings(ctl, P::kMorphAnchorModeId);
    int anchorModeControls = 0;
    int morphModeControls = 0;
    for (const BuiltView& bv : tree) {
        if (bv.tag() == static_cast<std::int32_t>(P::kAnchorModeId)) {
            ++anchorModeControls;
            CHECK(shownListStrings(bv.view) == anchorMode);
        } else if (bv.tag() == static_cast<std::int32_t>(P::kMorphAnchorModeId)) {
            ++morphModeControls;
            CHECK(shownListStrings(bv.view) == morphMode);
        }
    }
    CHECK(anchorModeControls == 1);
    CHECK(morphModeControls == 3);  // Main anchor-group, Advanced sub-anchor and morph-behaviour
    for (const std::string& s : anchorMode) {
        CAPTURE(s);
        CHECK(std::find(morphMode.begin(), morphMode.end(), s) == morphMode.end());
    }

    // ---- FR-012 placement of the two labels (XML) ----
    const std::string xml = readUidesc();
    const XmlScan scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);
    const auto tagMap = Krate::Test::extractControlTagMap(xml);
    const auto bound = boundNodes(scan, tagMap);
    const int anchorGroup = singleLabelled(scan, "anchor-group");
    const int pageMain = singleLabelled(scan, "page-main");
    const int pageAdvanced = singleLabelled(scan, "page-advanced");
    const int subAnchor = singleLabelled(scan, "section-sub-anchor");
    REQUIRE(isDescendantOf(scan, anchorGroup, pageMain));

    bool morphLabelInAnchorGroup = false;
    int anchorModeLabel = -1;
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        const XmlNode& n = scan.nodes[i];
        if (n.get("class") != "CTextLabel") {
            continue;
        }
        if (n.get("title") == kMorphBehaviourText && isDescendantOf(scan, static_cast<int>(i), anchorGroup)) {
            morphLabelInAnchorGroup = true;
        }
        if (n.get("title") == kAnchorModeText && isDescendantOf(scan, static_cast<int>(i), subAnchor)) {
            anchorModeLabel = static_cast<int>(i);
        }
    }
    CHECK(morphLabelInAnchorGroup);
    CHECK(hasDescendantBoundTo(scan, bound, anchorGroup, static_cast<int>(P::kMorphAnchorModeId)));
    for (const auto& [node, id] : bound) {
        if (id == static_cast<int>(P::kAnchorModeId)) {
            CHECK(isDescendantOf(scan, node, pageAdvanced));  // never on Main (FR-012)
            CHECK_FALSE(isDescendantOf(scan, node, pageMain));
        }
    }
    REQUIRE(anchorModeLabel >= 0);
    bool anchorModeLabelled = false;
    for (const auto& [node, id] : bound) {
        if (id != static_cast<int>(P::kAnchorModeId) || !isDescendantOf(scan, node, subAnchor)) {
            continue;
        }
        const XmlNode& label = scan.nodes[static_cast<std::size_t>(anchorModeLabel)];
        const XmlNode& control = scan.nodes[static_cast<std::size_t>(node)];
        const bool sameRow = label.window.top < control.window.bottom && control.window.top < label.window.bottom;
        const double gap = control.window.left - label.window.right;
        anchorModeLabelled = label.parent == control.parent && sameRow && gap >= 0.0 && gap <= 16.0;
    }
    CHECK(anchorModeLabelled);

    // ---- built tree: banned labels (FR-093), forbidden Main words (FR-020), ruled statements, exact tooltips ----
    std::vector<std::string> failures;
    int monoCarriers = 0;
    int wideCarriers = 0;
    int syncCarriers = 0;
    int bipolarCarriers = 0;
    int compressionCarriers = 0;
    for (const BuiltView& bv : tree) {
        const std::vector<std::string> texts = textsOf(bv.view);
        for (const std::string& t : texts) {
            for (const char* banned : kBannedLabels) {
                if (containsNoCase(t, banned)) {
                    failures.push_back(std::string("banned label '") + banned + "' in: " + t);
                }
            }
            if (!bv.advanced()) {
                for (const char* word : kForbiddenMainWords) {
                    if (containsNoCase(t, word)) {
                        failures.push_back(std::string("forbidden Main word '") + word + "' in: " + t);
                    }
                }
            }
        }

        const std::string tooltip = tooltipOf(bv.view);
        const std::int32_t tag = bv.tag();
        if (tag >= static_cast<std::int32_t>(P::kFieldImageModeId) && tag <= static_cast<std::int32_t>(P::kFieldSyncRateId)) {
            ++monoCarriers;
            if (tooltip.find(kMonoGuaranteeText) == std::string::npos) {
                failures.push_back("MONO-GUARANTEE missing on " + std::to_string(tag));
            }
        }
        if (tag >= static_cast<std::int32_t>(P::kCharacterId) && tag <= static_cast<std::int32_t>(P::kCharacterToneId)) {
            ++wideCarriers;
            if (tooltip.find(kWideCaveatText) == std::string::npos) {
                failures.push_back("WIDE caveat missing on " + std::to_string(tag));
            }
        }
        if (tag == static_cast<std::int32_t>(P::kFieldSyncRateId)) {
            ++syncCarriers;
            if (tooltip.find(kFieldSyncFoldingText) == std::string::npos) {
                failures.emplace_back("field-sync folding statement missing on 809");
            }
        }
        if (tag == static_cast<std::int32_t>(P::kMotionPolarityId)) {
            ++bipolarCarriers;
            if (tooltip.find(kBipolarRestText) == std::string::npos) {
                failures.emplace_back("Bipolar-rest statement missing on 911");
            }
        }
        if (tag == static_cast<std::int32_t>(P::kMacroTightLooseId)) {
            ++compressionCarriers;
            if (tooltip.find(kCompressionWord) == std::string::npos) {
                failures.emplace_back("Compression missing on 102");
            }
        }

        // Exact D1 / D2 tooltip per control (the ratified literal, never composed here).
        const char* expected = nullptr;
        if (dynamic_cast<Krate::Plugins::ADSRDisplay*>(bv.view) != nullptr) {
            expected = kAdsrDisplayTooltip;
        } else if (const auto* bars = dynamic_cast<P::UI::HarmonicBarView*>(bv.view)) {
            expected = bars->variant() == P::UI::HarmonicBarView::Variant::Main ? kBarsMainTooltip : kBarsCoreTooltip;
        } else if (dynamic_cast<P::UI::SpreadCurveView*>(bv.view) != nullptr) {
            expected = kSpreadCurveTooltip;
        } else if (tag == P::UI::kPresetBrowserTag) {
            expected = kPresetButtonTooltip;  // Phase 13 T014
        } else if (tag >= 0) {
            expected = ruledTooltip(bv.advanced(), tag);
            if (expected == nullptr) {
                failures.push_back("no ruled tooltip for tag " + std::to_string(tag) +
                                   (bv.advanced() ? " (advanced)" : " (main)"));
                continue;
            }
        } else if (const auto* label = dynamic_cast<VSTGUI::CTextLabel*>(bv.view);
                   label != nullptr && label->getText().getString() == kHeldAtAText) {
            expected = kHeldAtATooltip;
        }
        if (expected != nullptr && tooltip != expected) {
            failures.push_back("tooltip of tag " + std::to_string(tag) + ": '" + tooltip + "' != '" + expected + "'");
        }
    }
    CHECK(monoCarriers == 10);
    CHECK(wideCarriers == 3);
    CHECK(syncCarriers == 1);
    CHECK(bipolarCarriers == 1);
    CHECK(compressionCarriers == 1);
    INFO(joinLines(failures));
    CHECK(failures.empty());
}

// ==============================================================================
// SC-017 - page switch
// ==============================================================================

namespace {

struct PageSurface {
    std::array<VSTGUI::CViewContainer*, 2> pages{};  // page-main, page-advanced
    std::array<VSTGUI::CViewContainer*, 3> tabs{};   // tab-sections, tab-modulation, tab-settings
    std::vector<VSTGUI::CSegmentButton*> pageStrips;
    std::vector<VSTGUI::CSegmentButton*> tabStrips;
    int pageLabelCount = 0;
};

[[nodiscard]] PageSurface collectPageSurface(VSTGUI::CFrame* frame) {
    PageSurface s;
    constexpr std::array<const char*, 2> kPages{"page-main", "page-advanced"};
    constexpr std::array<const char*, 3> kTabs{"tab-sections", "tab-modulation", "tab-settings"};
    for (const BuiltView& bv : builtTree(frame)) {
        const std::string own = uidescLabelOf(bv.view);
        if (own.starts_with("page-") && own != "page-strip") {
            ++s.pageLabelCount;
        }
        for (std::size_t i = 0; i < kPages.size(); ++i) {
            if (own == kPages[i]) {
                s.pages[i] = bv.view->asViewContainer();
            }
        }
        for (std::size_t i = 0; i < kTabs.size(); ++i) {
            if (own == kTabs[i]) {
                s.tabs[i] = bv.view->asViewContainer();
            }
        }
        if (auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(bv.view)) {
            if (segment->getTag() == P::UI::kPageStripTag) {
                s.pageStrips.push_back(segment);
            } else if (segment->getTag() == P::UI::kTabStripTag) {
                s.tabStrips.push_back(segment);
            }
        }
    }
    return s;
}

template <std::size_t N>
[[nodiscard]] bool onlyVisible(const std::array<VSTGUI::CViewContainer*, N>& containers, std::size_t k) {
    for (std::size_t i = 0; i < N; ++i) {
        if (containers[i] == nullptr || containers[i]->isVisible() != (i == k)) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Profundum_Editor_PageSwitch", "[profundum][ui]") {
    ConnectedPair cp;
    P::Controller& ctl = *cp.cf.controller.get();
    const StateBytes state0 = writeV2Stream(*cp.fx.proc);
    cp.clearRecords();

    {
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        PageSurface s = collectPageSurface(ed.frame());
        CHECK(s.pageLabelCount == 2);  // exactly page-main and page-advanced
        REQUIRE(s.pages[0] != nullptr);
        REQUIRE(s.pages[1] != nullptr);
        CHECK(s.pages[0]->getParentView() == s.pages[1]->getParentView());  // siblings
        for (const auto* tab : s.tabs) {
            REQUIRE(tab != nullptr);
        }
        REQUIRE(s.pageStrips.size() == 1u);
        REQUIRE(s.tabStrips.size() == 1u);
        VSTGUI::CSegmentButton* pageStrip = s.pageStrips[0];
        VSTGUI::CSegmentButton* tabStrip = s.tabStrips[0];
        CHECK(pageStrip->getSegments().size() == 2u);
        CHECK(tabStrip->getSegments().size() == 3u);

        // A fresh controller opens on Main and the first tab.
        CHECK(onlyVisible(s.pages, 0));
        CHECK(onlyVisible(s.tabs, 0));
        CHECK(ctl.activePage() == 0);

        for (const std::uint32_t k : {1u, 0u, 1u}) {
            CAPTURE(k);
            pageStrip->setSelectedSegment(k);
            pageStrip->valueChanged();
            CHECK(ctl.activePage() == static_cast<int>(k));
            CHECK(onlyVisible(s.pages, k));
        }
        for (const std::uint32_t t : {1u, 2u, 0u, 2u}) {
            CAPTURE(t);
            tabStrip->setSelectedSegment(t);
            tabStrip->valueChanged();
            CHECK(ctl.activeTab() == static_cast<int>(t));
            CHECK(onlyVisible(s.tabs, t));
            CHECK(onlyVisible(s.pages, 1u));  // a tab switch keeps the page
        }
    }

    // Close / reopen within the session: Advanced and SETTINGS are still selected.
    {
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.frame() != nullptr);
        const PageSurface s = collectPageSurface(ed.frame());
        REQUIRE(s.pageStrips.size() == 1u);
        REQUIRE(s.tabStrips.size() == 1u);
        CHECK(onlyVisible(s.pages, 1u));
        CHECK(onlyVisible(s.tabs, 2u));
        CHECK(s.pageStrips[0]->getSelectedSegment() == 1u);
        CHECK(s.tabStrips[0]->getSelectedSegment() == 2u);
        s.pageStrips[0]->setSelectedSegment(0u);
        s.pageStrips[0]->valueChanged();
        CHECK(onlyVisible(s.pages, 0u));
    }

    // Session state only: no parameter edit, no message, the state bytes unchanged.
    CHECK(cp.cf.handler.performEdits.empty());
    CHECK(cp.toProcessor.records().empty());
    CHECK(writeV2Stream(*cp.fx.proc) == state0);
}

// ==============================================================================
// SC-019 - scaling, accessibility and placement
// ==============================================================================

TEST_CASE("Profundum_UiLayout", "[profundum][ui]") {
    ControllerFixture cf;
    P::Controller& ctl = *cf.controller.get();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);
    flushDeferredUpdates();
    P::UI::ProfundumPanelSubController* panel = ctl.panelForTest();
    REQUIRE(panel != nullptr);

    // The template root is the designed size.
    REQUIRE(ed.frame()->getNbViews() >= 1u);
    const VSTGUI::CRect rootRect = ed.frame()->getView(0)->getViewSize();
    CHECK(rootRect.getWidth() == kDesignedWidth);
    CHECK(rootRect.getHeight() == kDesignedHeight);

    struct ShowState {
        int page;
        int tab;
    };
    constexpr std::array<ShowState, 4> kStates{
        {{.page = 0, .tab = 0}, {.page = 1, .tab = 0}, {.page = 1, .tab = 1}, {.page = 1, .tab = 2}}};

    std::vector<std::string> failures;
    for (std::size_t zi = 0; zi < kD7ZoomSteps.size(); ++zi) {
        ctl.setZoomStep(static_cast<int>(zi));
        const double zoom = ed.editor()->getZoomFactor();
        REQUIRE(zoom == kD7ZoomSteps[zi]);
        const double frameW = kDesignedWidth * zoom;
        const double frameH = kDesignedHeight * zoom;

        for (const ShowState& state : kStates) {
            ctl.setActivePage(state.page);
            ctl.setActiveTab(state.tab);
            panel->showSessionState();
            const std::vector<BuiltView> tree = builtTree(ed.frame());
            const std::string where =
                " [zoom " + std::to_string(zoom) + ", page " + std::to_string(state.page) + ", tab " +
                std::to_string(state.tab) + "]";

            std::vector<std::size_t> visibleTargets;  // tagged controls and the custom views
            int visibleAdvanced = 0;
            int visibleMainPage = 0;
            for (std::size_t i = 0; i < tree.size(); ++i) {
                const BuiltView& bv = tree[i];
                if (!bv.visible) {
                    continue;
                }
                const std::string name = std::to_string(bv.tag()) + " " + typeid(*bv.view).name() + where;

                // Fonts and contrast of every visible text-bearing view.
                if (const VSTGUI::CFontDesc* font = fontOf(bv.view)) {
                    if (font->getSize() * zoom < kMinFontPx * zoom) {
                        failures.push_back("font " + std::to_string(font->getSize()) + " px: " + name);
                    }
                } else if (dynamic_cast<VSTGUI::CParamDisplay*>(bv.view) != nullptr ||
                           dynamic_cast<VSTGUI::CSegmentButton*>(bv.view) != nullptr) {
                    failures.push_back("text view without a font: " + name);
                }
                for (const auto& [fg, bg] : textColourPairs(tree, static_cast<int>(i))) {
                    const double ratio = contrastRatio(fg, bg);
                    if (ratio < kMinContrastText) {
                        failures.push_back("contrast " + std::to_string(ratio) + ": " + name);
                    }
                }

                if (!isTaggedControl(bv) && !isCustomView(bv)) {
                    continue;
                }
                visibleTargets.push_back(i);
                if (bv.advanced()) {
                    ++visibleAdvanced;
                }
                if (bv.under("page-main")) {
                    ++visibleMainPage;
                }

                const VSTGUI::CRect& r = bv.window;
                if (r.left < 0.0 || r.top < 0.0 || r.right * zoom > frameW || r.bottom * zoom > frameH) {
                    failures.push_back("outside the frame: " + name);
                }
                if (isTaggedControl(bv) && tooltipOf(bv.view).empty()) {
                    failures.push_back("no tooltip: " + name);
                }
                // Hit targets (D7 2): labels and display-only readouts are not targets.
                if (isTaggedControl(bv) && dynamic_cast<VSTGUI::CTextLabel*>(bv.view) == nullptr &&
                    !isPlainReadout(bv.view)) {
                    if (const auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(bv.view)) {
                        const std::size_t n = segment->getSegments().size();
                        const double cellW = n == 0u ? 0.0 : r.getWidth() / static_cast<double>(n);
                        if (cellW * zoom < kMinSegmentCellPx * zoom || r.getHeight() * zoom < kMinSegmentCellPx * zoom) {
                            failures.push_back("segment cell " + std::to_string(cellW) + " x " +
                                               std::to_string(r.getHeight()) + ": " + name);
                        }
                    } else if (r.getWidth() * zoom < kMinHitTargetPx * zoom ||
                               r.getHeight() * zoom < kMinHitTargetPx * zoom) {
                        failures.push_back("hit target " + std::to_string(r.getWidth()) + " x " +
                                           std::to_string(r.getHeight()) + ": " + name);
                    }
                }
            }
            // Non-vacuity: the selected page is the one being measured.
            if (state.page == 0 && (visibleAdvanced != 0 || visibleMainPage == 0)) {
                failures.push_back("page visibility" + where);
            }
            if (state.page == 1 && (visibleMainPage != 0 || visibleAdvanced == 0)) {
                failures.push_back("page visibility" + where);
            }
            for (std::size_t a = 0; a < visibleTargets.size(); ++a) {
                for (std::size_t b = a + 1; b < visibleTargets.size(); ++b) {
                    const BuiltView& va = tree[visibleTargets[a]];
                    const BuiltView& vb = tree[visibleTargets[b]];
                    if (strictlyOverlap(va.window, vb.window)) {
                        failures.push_back("overlap " + std::to_string(va.tag()) + " / " + std::to_string(vb.tag()) +
                                           where);
                    }
                }
            }
        }
    }
    ctl.setZoomStep(0);
    INFO(joinLines(failures));
    CHECK(failures.empty());

    // ---- the colour table is D7's (the contrast above reads these values from the built views) ----
    const std::string xml = readUidesc();
    for (const ColourToken& token : kD7Colours) {
        CAPTURE(token.name);
        char rgba[16] = {};
        std::snprintf(rgba, sizeof(rgba), "#%02X%02X%02Xff", token.r, token.g, token.b);
        const std::string expected = std::string("<color name=\"") + token.name + "\" rgba=\"" + rgba + "\"/>";
        CHECK(xml.find(expected) != std::string::npos);
    }

    // ---- FR-022 / FR-050 (XML) ----
    const XmlScan scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);
    const auto bound = boundNodes(scan, Krate::Test::extractControlTagMap(xml));
    const int pageAdvanced = singleLabelled(scan, "page-advanced");
    int morphControls = 0;
    for (const auto& [node, id] : bound) {
        if (id != static_cast<int>(P::kMorphPositionId)) {
            continue;
        }
        ++morphControls;
        int ancestor = scan.nodes[static_cast<std::size_t>(node)].parent;
        while (ancestor >= 0 && scan.nodes[static_cast<std::size_t>(ancestor)].get("uidesc-label") != "morph-anchor") {
            ancestor = scan.nodes[static_cast<std::size_t>(ancestor)].parent;
        }
        REQUIRE(ancestor >= 0);  // every 105-bound control sits in morph-anchor
        CHECK(hasDescendantBoundTo(scan, bound, ancestor, static_cast<int>(P::kMorphAnchorModeId)));
    }
    CHECK(morphControls == 2);  // the CSlider and its readout
    bool mainBindsMotionSource = false;
    for (const auto& [node, id] : bound) {
        if (isDescendantOf(scan, node, pageAdvanced)) {
            continue;
        }
        if (id == static_cast<int>(P::kMotionSourceId)) {
            mainBindsMotionSource = true;
        }
        CAPTURE(id);
        CHECK_FALSE((id >= static_cast<int>(P::kMotionTargetId) && id <= static_cast<int>(P::kMotionEnvDecayId)));
    }
    CHECK(mainBindsMotionSource);

    // ---- Phase 13 T014 (FR-030, FR-059; SC-013 layout arm): the header preset button ----
    SECTION("header holds exactly one OutlineBrowserButton at (452, 24) 96 x 24, every ratified rect kept") {
        // The nine ratified header children, in uidesc order, read from editor.uidesc:125-133 before the insert.
        const std::array<VSTGUI::CRect, 9> kRatifiedHeader{{
            VSTGUI::CRect(16.0, 16.0, 156.0, 40.0),   // PROFUNDUM
            VSTGUI::CRect(170.0, 4.0, 290.0, 20.0),   // VOICE
            VSTGUI::CRect(170.0, 24.0, 290.0, 48.0),  // VoiceMode
            VSTGUI::CRect(310.0, 4.0, 436.0, 20.0),   // GLIDE
            VSTGUI::CRect(310.0, 22.0, 340.0, 52.0),  // NotePortamento knob
            VSTGUI::CRect(346.0, 26.0, 436.0, 46.0),  // NotePortamento readout (ends at x 436)
            VSTGUI::CRect(560.0, 24.0, 760.0, 48.0),  // page strip (starts at x 560)
            VSTGUI::CRect(780.0, 4.0, 948.0, 20.0),   // ZOOM
            VSTGUI::CRect(780.0, 24.0, 948.0, 48.0),  // zoom-header
        }};
        const VSTGUI::CRect kButtonRect(452.0, 24.0, 548.0, 48.0);

        VSTGUI::CControl* buttonAtStep0 = nullptr;
        for (std::size_t zi = 0; zi < kD7ZoomSteps.size(); ++zi) {
            ctl.setZoomStep(static_cast<int>(zi));
            const double zoom = ed.editor()->getZoomFactor();
            REQUIRE(zoom == kD7ZoomSteps[zi]);
            CAPTURE(zoom);
            const std::vector<BuiltView> tree = builtTree(ed.frame());

            int header = -1;
            for (std::size_t i = 0; i < tree.size(); ++i) {
                if (tree[i].view->asViewContainer() != nullptr && uidescLabelOf(tree[i].view) == "header") {
                    REQUIRE(header == -1);  // one header
                    header = static_cast<int>(i);
                }
            }
            REQUIRE(header >= 0);
            std::vector<std::size_t> children;
            for (std::size_t i = 0; i < tree.size(); ++i) {
                if (tree[i].parentIndex == header) {
                    children.push_back(i);
                }
            }
            // The nine ratified children plus the button: no preset-name display, no previous / next control.
            REQUIRE(children.size() == kRatifiedHeader.size() + 1u);

            std::vector<std::size_t> buttons;
            for (const std::size_t i : children) {
                if (dynamic_cast<Krate::Plugins::OutlineBrowserButton*>(tree[i].view) != nullptr) {
                    buttons.push_back(i);
                }
            }
            REQUIRE(buttons.size() == 1u);
            const BuiltView& button = tree[buttons.front()];
            CHECK(buttons.front() == children.back());  // the last header child
            CHECK(button.visible);
            CHECK(button.window == kButtonRect);
            // Scaled by the step: the frame draws the 100 % rect through the zoom transform.
            const VSTGUI::CRect scaled(button.window.left * zoom, button.window.top * zoom,
                                       button.window.right * zoom, button.window.bottom * zoom);
            CHECK(scaled == VSTGUI::CRect(452.0 * zoom, 24.0 * zoom, 548.0 * zoom, 48.0 * zoom));
            CHECK(scaled.getWidth() == Catch::Approx(96.0 * zoom));
            CHECK(scaled.getHeight() == Catch::Approx(24.0 * zoom));
            CHECK(button.window.left >= 436.0);   // clear of the Glide readout
            CHECK(button.window.right <= 560.0);  // clear of the page strip
            CHECK(button.tag() == P::UI::kPresetBrowserTag);
            CHECK(button.control()->getListener() == static_cast<VSTGUI::IControlListener*>(panel));
            CHECK_FALSE(tooltipOf(button.view).empty());

            for (std::size_t c = 0; c < kRatifiedHeader.size(); ++c) {
                const BuiltView& child = tree[children[c]];
                const std::string what = std::to_string(c) + " " + typeid(*child.view).name();
                CAPTURE(what);
                CHECK(child.view->getViewSize() == kRatifiedHeader[c]);
                CHECK_FALSE(strictlyOverlap(child.window, button.window));
                for (const std::string& text : textsOf(child.view)) {
                    CAPTURE(text);
                    CHECK_FALSE(containsNoCase(text, "preset"));
                    CHECK_FALSE(containsNoCase(text, "previous"));
                    CHECK_FALSE(containsNoCase(text, "next"));
                }
                if (dynamic_cast<VSTGUI::CParamDisplay*>(child.view) != nullptr &&
                    dynamic_cast<VSTGUI::CTextLabel*>(child.view) == nullptr) {
                    CHECK(child.tag() == static_cast<std::int32_t>(P::kNotePortamentoId));  // the Glide readout only
                }
            }
            if (zi == 0u) {
                buttonAtStep0 = button.control();
            }
        }
        ctl.setZoomStep(0);

        // The session tag list: 14 tags, 9400 last.
        CHECK(P::UI::kSessionTags.size() == 14u);
        CHECK(P::UI::kPresetBrowserTag == 9400);
        CHECK(P::UI::kSessionTags.back() == P::UI::kPresetBrowserTag);
        CHECK(std::find(P::UI::kSessionTags.begin(), P::UI::kSessionTags.end(), 9400) != P::UI::kSessionTags.end());

        // A click (the button's one valueChanged per press) opens the overlay.
        REQUIRE(buttonAtStep0 != nullptr);
        const Krate::Plugins::PresetBrowserView* browser = ctl.presetBrowserViewForTest();
        REQUIRE(browser != nullptr);
        CHECK_FALSE(browser->isOpen());
        buttonAtStep0->setValueNormalized(1.0f);
        panel->valueChanged(buttonAtStep0);
        CHECK(browser->isOpen());
    }
}

// ==============================================================================
// SC-031 - placement
// ==============================================================================

TEST_CASE("Profundum_UiPlacement", "[profundum][ui]") {
    const std::string xml = readUidesc();
    const XmlScan scan = scanUidesc(xml);
    REQUIRE(scan.wellFormed);
    const auto bound = boundNodes(scan, Krate::Test::extractControlTagMap(xml));
    const int editorTemplate = findTemplate(scan, "editor");
    REQUIRE(editorTemplate >= 0);
    const int pageMain = singleLabelled(scan, "page-main");
    const int pageAdvanced = singleLabelled(scan, "page-advanced");

    // (a) the Main region binds exactly the FR-011 set; none of 205, 102, 301.
    std::set<int> mainIds;
    std::set<int> advancedIds;
    for (const auto& [node, id] : bound) {
        (isDescendantOf(scan, node, pageAdvanced) ? advancedIds : mainIds).insert(id);
    }
    CHECK(mainIds == std::set<int>(kFr011MainIds.begin(), kFr011MainIds.end()));
    for (const int advancedOnly : {static_cast<int>(P::kCoreShiftId), static_cast<int>(P::kMacroTightLooseId),
                                   static_cast<int>(P::kAnchorModeId)}) {
        CAPTURE(advancedOnly);
        CHECK_FALSE(mainIds.contains(advancedOnly));
        CHECK(advancedIds.count(advancedOnly) == 1u);  // (b) bound on Advanced
    }

    // (b) every FR-004 member is inside its container, and every Advanced control is in a container that lists it.
    for (const SectionMember& m : kFr004Members) {
        CAPTURE(m.container, m.tag);
        const int container = singleLabelled(scan, m.container);
        CHECK(isDescendantOf(scan, container, pageAdvanced));
        if (m.tag < kSessionBase) {
            CHECK(hasDescendantBoundTo(scan, bound, container, m.tag));
        } else {
            CHECK(hasDescendantWithSessionTag(scan, container, m.tag));
        }
    }
    const int tabSections = singleLabelled(scan, "tab-sections");
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        const std::string label = scan.nodes[i].get("uidesc-label");
        if (label.starts_with("section-")) {
            CAPTURE(label);
            CHECK(isDescendantOf(scan, static_cast<int>(i), tabSections));
        }
    }
    for (const auto& [node, id] : bound) {
        if (!isDescendantOf(scan, node, pageAdvanced)) {
            continue;
        }
        const std::string container = fr004ContainerOf(scan, node);
        CAPTURE(id, container);
        CHECK(fr004Lists(container, id));
    }
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        const std::string tagText = scan.nodes[i].get("session-tag");
        if (tagText.empty() || !isDescendantOf(scan, static_cast<int>(i), pageAdvanced)) {
            continue;
        }
        const int tag = std::stoi(tagText);
        if (tag == P::UI::kTabStripTag) {
            continue;  // the tab strip is a child of page-advanced itself (D2 1.1)
        }
        const std::string container = fr004ContainerOf(scan, static_cast<int>(i));
        CAPTURE(tag, container);
        CHECK(fr004Lists(container, tag));
    }

    // (c) the tabs are inside Advanced; the page containers are exactly page-main and page-advanced, siblings.
    for (const char* tab : {"tab-sections", "tab-modulation", "tab-settings"}) {
        CAPTURE(tab);
        CHECK(scan.nodes[static_cast<std::size_t>(singleLabelled(scan, tab))].parent == pageAdvanced);
    }
    std::set<std::string> pageLabels;
    for (const XmlNode& n : scan.nodes) {
        const std::string label = n.get("uidesc-label");
        if (label.starts_with("page-") && label != "page-strip") {
            pageLabels.insert(label);
        }
        CHECK(n.get("class") != "UIViewSwitchContainer");  // FR-030
    }
    CHECK(pageLabels == std::set<std::string>{"page-main", "page-advanced"});
    CHECK(scan.nodes[static_cast<std::size_t>(pageMain)].parent == editorTemplate);
    CHECK(scan.nodes[static_cast<std::size_t>(pageAdvanced)].parent == editorTemplate);

    // (d) 800 and the SPREAD mini-spectrum are in Stereo & Output; the mini-spectrum is not on Main.
    const int stereo = singleLabelled(scan, "section-stereo-output");
    int imageModeControls = 0;
    for (const auto& [node, id] : bound) {
        if (id == static_cast<int>(P::kFieldImageModeId)) {
            ++imageModeControls;
            CHECK(isDescendantOf(scan, node, stereo));
        }
    }
    CHECK(imageModeControls == 1);
    int spreadViews = 0;
    int barViewsMain = 0;
    int barViewsCore = 0;
    const int massGroup = singleLabelled(scan, "mass-group");
    const int coreSection = singleLabelled(scan, "section-core-spectrum");
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        const XmlNode& n = scan.nodes[i];
        if (n.get("custom-view-name") == "SpreadCurveView") {
            ++spreadViews;
            CHECK(isDescendantOf(scan, static_cast<int>(i), stereo));
            CHECK_FALSE(isDescendantOf(scan, static_cast<int>(i), pageMain));
        }
        if (n.get("custom-view-name") == "HarmonicBarView") {
            if (n.get("bar-variant") == "main") {
                ++barViewsMain;
                CHECK(isDescendantOf(scan, static_cast<int>(i), massGroup));
            } else {
                ++barViewsCore;
                CHECK(n.get("bar-variant") == "core");
                CHECK(isDescendantOf(scan, static_cast<int>(i), coreSection));
            }
        }
    }
    CHECK(spreadViews == 1);
    CHECK(barViewsMain == 1);
    CHECK(barViewsCore == 1);
    // D2 2.11 / D7 7: the SETTINGS tab states the no-run-loop note.
    const int settings = singleLabelled(scan, "tab-settings");
    bool runLoopNote = false;
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        runLoopNote = runLoopNote || (scan.nodes[i].get("class") == "CTextLabel" &&
                                      scan.nodes[i].get("title") == kNoRunLoopNote &&
                                      isDescendantOf(scan, static_cast<int>(i), settings));
    }
    CHECK(runLoopNote);

    // (e) and (f): the built tree.
    ControllerFixture cf;
    P::Controller& ctl = *cf.controller.get();
    HeadlessEditor ed(cf.controller.get());
    REQUIRE(ed.frame() != nullptr);

    // (e) both HarmonicBarViews draw the one cached frame: identical heights after one tick.
    const auto bars = collectViewsOfType<P::UI::HarmonicBarView>(ed.frame());
    REQUIRE(bars.size() == 2u);
    P::ProfundumSpectrumFrame frame = testFrame(4242u);
    deliverFrame(ctl, frame);
    REQUIRE(ctl.cachedFrameForTest().sequence == 4242u);
    bars[0]->onTickForTest(0.033f);
    bars[1]->onTickForTest(0.033f);
    CHECK(bars[0]->barHeightsForTest() == bars[1]->barHeightsForTest());
    CHECK(bars[0]->anchorHeightForTest() == bars[1]->anchorHeightForTest());
    CHECK(bars[0]->barHeightsForTest()[0] > 0.0f);  // non-vacuity: the frame was drawn

    // (f) the FR-009 drift guard: every tagged control is a registered ID or a D6 UI-only tag (plus the Phase 13
    // preset button, T014); every one of them exists.
    std::set<std::int32_t> expectedSessionTags(kD6UiOnlyTags.begin(), kD6UiOnlyTags.end());
    expectedSessionTags.insert(P::UI::kPresetBrowserTag);
    std::set<std::int32_t> sessionTagsSeen;
    for (const BuiltView& bv : builtTree(ed.frame())) {
        const std::int32_t tag = bv.tag();
        if (tag < 0) {
            continue;
        }
        CAPTURE(tag);
        if (tag < kSessionBase) {
            CHECK(ctl.getParameterObject(static_cast<ParamID>(tag)) != nullptr);
        } else {
            CHECK(expectedSessionTags.contains(tag));
            sessionTagsSeen.insert(tag);
        }
    }
    CHECK(sessionTagsSeen == expectedSessionTags);
}

// ==============================================================================
// SC-032 - zoom control
// ==============================================================================

TEST_CASE("Profundum_UiZoom", "[profundum][ui]") {
    ConnectedPair cp;
    P::Controller& ctl = *cp.cf.controller.get();
    const StateBytes state0 = writeV2Stream(*cp.fx.proc);
    REQUIRE(ctl.getParameterCount() == 84);
    CHECK(ctl.getParameterObject(static_cast<ParamID>(P::UI::kZoomTag)) == nullptr);  // bound to no registered ID
    cp.clearRecords();

    {
        HeadlessEditor ed(cp.cf.controller.get());
        REQUIRE(ed.editor() != nullptr);
        REQUIRE(ed.frame() != nullptr);

        // One designed size, no other size offered.
        VSTGUI::CPoint minSize;
        VSTGUI::CPoint maxSize;
        ed.editor()->getEditorSizeConstrains(minSize, maxSize);
        CHECK(minSize == VSTGUI::CPoint(kDesignedWidth, kDesignedHeight));
        CHECK(maxSize == VSTGUI::CPoint(kDesignedWidth, kDesignedHeight));

        // The two zoom controls (header and SETTINGS, ruled 2026-10-23) carry the one session tag.
        std::vector<VSTGUI::CSegmentButton*> zooms;
        int inHeader = 0;
        int inSettings = 0;
        for (const BuiltView& bv : builtTree(ed.frame())) {
            if (bv.tag() != P::UI::kZoomTag) {
                continue;
            }
            auto* segment = dynamic_cast<VSTGUI::CSegmentButton*>(bv.view);
            REQUIRE(segment != nullptr);
            CHECK(segment->getSegments().size() == kD7ZoomSteps.size());
            zooms.push_back(segment);
            if (bv.under("tab-settings")) {
                ++inSettings;
            } else if (!bv.under("page-main") && !bv.under("page-advanced")) {
                ++inHeader;
            }
        }
        REQUIRE(zooms.size() == 2u);
        CHECK(inHeader == 1);
        CHECK(inSettings == 1);

        for (VSTGUI::CSegmentButton* zoomControl : zooms) {
            for (std::size_t i : {3u, 1u, 2u, 0u}) {
                CAPTURE(i);
                zoomControl->setSelectedSegment(static_cast<std::uint32_t>(i));
                zoomControl->valueChanged();
                CHECK(ed.editor()->getZoomFactor() == kD7ZoomSteps[i]);
                CHECK(ctl.zoomStep() == static_cast<int>(i));
                for (const VSTGUI::CSegmentButton* other : zooms) {
                    CHECK(other->getSelectedSegment() == static_cast<std::uint32_t>(i));  // both show the one step
                }
            }
        }
    }

    CHECK(ctl.getParameterCount() == 84);
    CHECK(cp.cf.handler.performEdits.empty());
    CHECK(cp.toProcessor.records().empty());
    CHECK(writeV2Stream(*cp.fx.proc) == state0);
}
