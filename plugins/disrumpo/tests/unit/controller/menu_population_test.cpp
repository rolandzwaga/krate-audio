// =============================================================================
// Type-specific shape-slot menus in the live editor (fix plan T1.19)
// =============================================================================
// The TypeParams_* templates put COptionMenus with a custom "menu-items"
// attribute on Band.NodeShape2. BandSubController::verifyView adds those
// entries; VSTGUI's ParameterChangeListener then replaces them with
// stepCount+1 numeric entries whenever the bound parameter has a step count
// (D-135). These cases open the editor headlessly, step band 0's Shape tab and
// its TypeParams switch to each type, and read the menus the user would see.
//
// The headless frame is never attached (CFrame::open(nullptr) fails), so no
// UIViewSwitchContainer builds its template by itself: both switches are
// stepped by hand, which runs the same createView -> verifyView path.
//
// D-168: the SergeFold labels must follow distortion_adapter.cpp's models[]
// order (Serge, Simple, Buchla259, Lockhart), and a freshly chosen SergeFold
// node must run the Serge model. D-157: SineFold / TriangleFold start on their
// pure algorithm (slot 2 = 0).
// =============================================================================

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "dsp/distortion_adapter.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "state_codec.h"

#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Disrumpo;

namespace {

constexpr Steinberg::Vst::ParamID kShape2ProxyId = makeBandParamId(0, BandParamType::kBandDisplayedShape2);
constexpr VSTGUI::CCoord kTabSwitchHeight = 243.0;       // Band<N>MainTab / Band<N>ShapeTab switch
constexpr VSTGUI::CCoord kTypeParamsSwitchHeight = 200.0; // TypeParams_* switch inside BandShapeTab
constexpr int kShapeTabIndex = 1;                         // template-names="Band1MainTab,Band1ShapeTab"

/// First UIViewSwitchContainer of the given height below `container`, in document order.
VSTGUI::UIViewSwitchContainer* findSwitch(VSTGUI::CViewContainer* container, VSTGUI::CCoord height) {
    VSTGUI::UIViewSwitchContainer* found = nullptr;
    container->forEachChild([&found, height](VSTGUI::CView* child) {
        if (found) return;
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            if (sw->getViewSize().getHeight() == height) {
                found = sw;
                return;
            }
        }
        if (auto* sub = child->asViewContainer()) found = findSwitch(sub, height);
    });
    return found;
}

void collectShape2Menus(VSTGUI::CViewContainer* container, std::vector<VSTGUI::COptionMenu*>& out) {
    container->forEachChild([&out](VSTGUI::CView* child) {
        if (auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(child)) {
            if (menu->getTag() == static_cast<int32_t>(kShape2ProxyId)) out.push_back(menu);
        }
        if (auto* sub = child->asViewContainer()) collectShape2Menus(sub, out);
    });
}

std::vector<std::string> entriesOf(VSTGUI::COptionMenu* menu) {
    std::vector<std::string> titles;
    titles.reserve(static_cast<size_t>(menu->getNbEntries()));
    for (int32_t i = 0; i < menu->getNbEntries(); ++i)
        titles.emplace_back(menu->getEntry(i)->getTitle().getString());
    return titles;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& e : v) s += (s.empty() ? "" : ",") + e;
    return s;
}

/// Editor open headlessly with band 0's Shape tab showing `type`, closed on destruction.
class ShapeTabEditor {
public:
    ShapeTabEditor(Controller& c, DistortionType type) {
        // Node 0 (the selected node) gets the type; the displayed-type proxy mirrors it.
        const double typeNorm = listIndexToNorm(static_cast<int>(type), kDistortionTypeCount);
        c.setParamNormalized(makeBandParamId(0, BandParamType::kBandTabView), 1.0);
        c.setParamNormalized(makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm);
        c.setParamNormalized(makeBandParamId(0, BandParamType::kBandDisplayedType), typeNorm);

        Krate::TestSupport::ensureVstguiInitialized();
        editor_ = new VSTGUI::VST3Editor(&c, "editor",
                                         (std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc").c_str());
        Steinberg::IPlugView* view = editor_;
        opened_ = view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue &&
                  editor_->getFrame() != nullptr;
        if (!opened_) return;

        auto* tabSwitch = findSwitch(editor_->getFrame(), kTabSwitchHeight);  // band 1 strip comes first
        if (tabSwitch == nullptr) return;
        tabSwitch->setCurrentViewIndex(kShapeTabIndex);
        typeSwitch_ = findSwitch(tabSwitch, kTypeParamsSwitchHeight);
        if (typeSwitch_ != nullptr) typeSwitch_->setCurrentViewIndex(static_cast<int32_t>(type));
    }
    ~ShapeTabEditor() {
        Steinberg::IPlugView* view = editor_;
        view->removed();
        view->release();
    }
    ShapeTabEditor(const ShapeTabEditor&) = delete;
    ShapeTabEditor& operator=(const ShapeTabEditor&) = delete;
    ShapeTabEditor(ShapeTabEditor&&) = delete;
    ShapeTabEditor& operator=(ShapeTabEditor&&) = delete;

    [[nodiscard]] bool ready() const { return opened_ && typeSwitch_ != nullptr; }

    /// Slot-2 menus of the TypeParams template on screen, bound to band 0's proxy.
    [[nodiscard]] std::vector<VSTGUI::COptionMenu*> shape2Menus() const {
        std::vector<VSTGUI::COptionMenu*> menus;
        if (typeSwitch_ != nullptr) collectShape2Menus(typeSwitch_, menus);
        return menus;
    }

private:
    VSTGUI::VST3Editor* editor_ = nullptr;
    VSTGUI::UIViewSwitchContainer* typeSwitch_ = nullptr;
    bool opened_ = false;
};

struct MenuExpectation {
    DistortionType type;
    const char* name;
    std::vector<std::string> entries;
};

}  // namespace

TEST_CASE("Type-specific COptionMenus keep their uidesc entries", "[disrumpo][ui][D-135][D-168]") {
    // uidesc menu-items of every slot-2 COptionMenu; SergeFold in the adapter's models[] order.
    const std::vector<MenuExpectation> expectations = {
        {.type = DistortionType::Tube, .name = "Tube", .entries = {"12AX7", "EL84", "6L6", "300B"}},
        {.type = DistortionType::AsymmetricFuzz, .name = "AsymFuzz", .entries = {"Germanium", "Silicon"}},
        {.type = DistortionType::SergeFold,
         .name = "SergeFold",
         .entries = {"Serge", "Simple", "Buchla", "Lockhart"}},
        {.type = DistortionType::Bitcrush, .name = "Bitcrush", .entries = {"Truncate", "Dither"}},
        {.type = DistortionType::SampleReduce, .name = "SampleReduce", .entries = {"ZOH", "Linear"}},
    };

    for (const auto& exp : expectations) {
        Controller c;
        REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
        {
            ShapeTabEditor ed(c, exp.type);
            REQUIRE(ed.ready());
            const auto menus = ed.shape2Menus();
            std::printf("T1.19 %s: %zu slot-2 menu(s)\n", exp.name, menus.size());
            REQUIRE(menus.size() == 1);
            auto* menu = menus.front();
            const auto entries = entriesOf(menu);
            std::printf("T1.19 %s slot-2 menu: %zu entries [%s]\n", exp.name, entries.size(), joined(entries).c_str());
            CHECK(entries == exp.entries);

            // Pick entry 1 the way the popup does; the proxy must land on list index 1.
            menu->beginEdit();
            menu->setValue(1.0f);
            menu->valueChanged();
            menu->endEdit();
            const double norm = c.getParamNormalized(kShape2ProxyId);
            const double want = listIndexToNorm(1, static_cast<int>(exp.entries.size()));
            std::printf("T1.19 %s entry 1 -> proxy %.6f (want %.6f)\n", exp.name, norm, want);
            CHECK(std::fabs(norm - want) < 1e-6);
        }
        c.terminate();
    }
}

TEST_CASE("Only the five list types put a COptionMenu on shape slot 2", "[disrumpo][ui][D-135]") {
    std::vector<int> typesWithMenu;
    for (int t = 0; t < kDistortionTypeCount; ++t) {
        Controller c;
        REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
        {
            ShapeTabEditor ed(c, static_cast<DistortionType>(t));
            REQUIRE(ed.ready());
            const auto menus = ed.shape2Menus();
            if (!menus.empty()) {
                typesWithMenu.push_back(t);
                std::printf("T1.19 survey type %d: %zu slot-2 menu(s), %d entries [%s]\n", t, menus.size(),
                            menus.front()->getNbEntries(), joined(entriesOf(menus.front())).c_str());
            }
        }
        c.terminate();
    }
    const std::vector<int> expected = {static_cast<int>(DistortionType::Tube),
                                       static_cast<int>(DistortionType::AsymmetricFuzz),
                                       static_cast<int>(DistortionType::SergeFold),
                                       static_cast<int>(DistortionType::Bitcrush),
                                       static_cast<int>(DistortionType::SampleReduce)};
    CHECK(typesWithMenu == expected);
}

TEST_CASE("Fresh fold nodes start on their named algorithm", "[disrumpo][ui][D-168][D-157]") {
    const ShapeShadowStorage shadow;

    DistortionParams serge;
    Processor::mapShapeSlotsToParams(DistortionType::SergeFold,
                                     shadow.typeSlots[static_cast<int>(DistortionType::SergeFold)], serge);
    DistortionParams sine;
    Processor::mapShapeSlotsToParams(DistortionType::SineFold,
                                     shadow.typeSlots[static_cast<int>(DistortionType::SineFold)], sine);
    DistortionParams tri;
    Processor::mapShapeSlotsToParams(DistortionType::TriangleFold,
                                     shadow.typeSlots[static_cast<int>(DistortionType::TriangleFold)], tri);
    std::printf("T1.19 defaults: SergeFold foldModel %d shape %.3f | SineFold shape %.3f | TriangleFold angle %.3f\n",
                serge.foldModel, static_cast<double>(serge.shape), static_cast<double>(sine.shape),
                static_cast<double>(tri.angle));

    CHECK(serge.foldModel == 0);  // models[0] = WavefolderModel::Serge (distortion_adapter.cpp)
    CHECK(serge.shape == 0.0f);   // pure fold, no triangle crossfade
    CHECK(sine.shape == 0.0f);    // pure sine fold
    CHECK(tri.angle == 0.0f);     // pure triangle fold
}
