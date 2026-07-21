// ==============================================================================
// Ring edit routing / geometry seeding (Gradus audit F11 + F12)
// ==============================================================================
// Gradus indexes its arp lanes in TWO different orders and they disagree at
// indices 3/4/5:
//
//   getArpLane / getArpLaneStepBaseParamId order:
//       0 Vel  1 Gate  2 Pitch  3 Ratchet   4 Modifier   5 Condition  6 Chord  7 Inv
//   ring / UI order (SubZone -> subZoneToLaneIndex, ringDataBridge_.setLane,
//   RingRenderer::isBarTypeLane, kDepthParamIds):
//       0 Vel  1 Gate  2 Pitch  3 Modifier  4 Condition  5 Ratchet    6 Chord  7 Inv
//
// The ring renderer hands its callbacks a UI-order lane index. Feeding that
// straight into getArpLaneStepBaseParamId() cross-wired every edit made on the
// three inner-ring lanes: editing Modifier wrote the Ratchet param, Condition
// wrote Modifier, and Ratchet -- which is a *drag* (bar-type) lane -- pushed
// continuous normalized values into the discrete Condition lane.
//
// This test drives the real wired callback: it opens the editor headlessly (so
// the controller's actual verifyView wiring runs), finds the RingRenderer in the
// view tree, and clicks each ring lane, asserting the ParamID the controller
// edits. A mapping-table unit test would not catch a regression in the wiring
// itself, which is where the bug lived.
// ==============================================================================

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "ui/ring_renderer.h"
#include "ui/ring_geometry.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace Gradus;

namespace {

struct EditRecord {
    enum class Action { Begin, Perform, End };
    Action action;
    Steinberg::Vst::ParamID id;
    Steinberg::Vst::ParamValue value;
};

class MockComponentHandler : public Steinberg::Vst::IComponentHandler {
public:
    std::vector<EditRecord> records;

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
        records.push_back({EditRecord::Action::Begin, id, 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(
        Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue v) override {
        records.push_back({EditRecord::Action::Perform, id, v});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
        records.push_back({EditRecord::Action::End, id, 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API queryInterface(
        const Steinberg::TUID, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount_; }
    Steinberg::uint32 PLUGIN_API release() override { return --refCount_; }

private:
    Steinberg::uint32 refCount_{1};
};

/// Depth-first search for the RingRenderer built from the .uidesc.
RingRenderer* findRingRenderer(VSTGUI::CViewContainer* container)
{
    if (!container) return nullptr;
    for (uint32_t i = 0; i < container->getNbViews(); ++i) {
        VSTGUI::CView* child = container->getView(i);
        if (auto* renderer = dynamic_cast<RingRenderer*>(child)) return renderer;
        if (auto* sub = dynamic_cast<VSTGUI::CViewContainer*>(child)) {
            if (auto* found = findRingRenderer(sub)) return found;
        }
    }
    return nullptr;
}

/// Click the centre of `step` in the given ring sub-zone, at mid-radius.
void clickLane(RingRenderer* renderer, int ringIndex, SubZone zone,
               int stepIndex, int stepCount)
{
    const auto& geo = renderer->geometry();
    const float radius = geo.normalizedValueToRadius(ringIndex, zone, 0.5f);
    const float angle = RingGeometry::stepArc(stepIndex, stepCount).centerAngle;

    VSTGUI::CPoint where{
        renderer->getViewSize().left + static_cast<double>(
            geo.centerX() + radius * std::cos(angle)),
        renderer->getViewSize().top + static_cast<double>(
            geo.centerY() + radius * std::sin(angle))};

    renderer->onMouseDown(where, VSTGUI::kLButton);
}

/// ParamID of the first Perform record, or 0 if the click routed nowhere.
Steinberg::Vst::ParamID firstPerformId(const MockComponentHandler& handler)
{
    for (const auto& r : handler.records) {
        if (r.action == EditRecord::Action::Perform) return r.id;
    }
    return 0;
}

}  // namespace

TEST_CASE("Ring edit routes the UI lane index to the matching step param",
          "[gradus][controller][ui][ring][F11]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    MockComponentHandler handler;
    REQUIRE(controller.setComponentHandler(&handler) == Steinberg::kResultOk);

    Krate::TestSupport::ensureVstguiInitialized();

    auto* editor = new VSTGUI::VST3Editor(
        &controller, "editor",
        (std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc").c_str());

    Steinberg::IPlugView* view = editor;
    REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType())
            == Steinberg::kResultTrue);
    REQUIRE(editor->getFrame() != nullptr);

    RingRenderer* renderer = findRingRenderer(editor->getFrame());
    REQUIRE(renderer != nullptr);

    // Each ring lane must edit ITS OWN param. Ring 2 holds Modifier/Condition,
    // ring 3 holds Ratchet/Chord/Inversion (ring_geometry.h SubZone enum).
    struct Case {
        const char*                 name;
        int                         ringIndex;
        SubZone                     zone;
        Steinberg::Vst::ParamID     expectedBase;
    };
    const Case cases[] = {
        {"Modifier (UI lane 3)",  2, SubZone::kModifier,  kArpModifierLaneStep0Id},
        {"Condition (UI lane 4)", 2, SubZone::kCondition, kArpConditionLaneStep0Id},
        {"Ratchet (UI lane 5)",   3, SubZone::kRatchet,   kArpRatchetLaneStep0Id},
    };

    for (const auto& c : cases) {
        handler.records.clear();
        clickLane(renderer, c.ringIndex, c.zone, /*stepIndex=*/0, /*stepCount=*/16);

        const auto edited = firstPerformId(handler);
        INFO(c.name << ": edited ParamID " << edited
             << ", expected " << c.expectedBase);
        REQUIRE(edited == c.expectedBase);
    }

    view->removed();
    view->release();
    controller.terminate();
}

TEST_CASE("Ring geometry init seeds step counts in UI lane order",
          "[gradus][controller][ui][ring][F12]")
{
    // constructArpLanes seeds RingGeometry::setLaneStepCount(i, ...) while
    // hitTest reads laneStepCounts_[i] in UI order, so the seeding must use the
    // lane drawn at ring index i -- not getArpLane(i). Give Modifier and Ratchet
    // different lengths and check the geometry agrees with the ring order.
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // Lane length params are normalized over 1..32 steps. Pick clearly distinct
    // lengths for the two lanes that the getArpLane order would swap.
    constexpr double kLen8  = (8.0 - 1.0) / 31.0;
    constexpr double kLen24 = (24.0 - 1.0) / 31.0;
    REQUIRE(controller.setParamNormalized(kArpModifierLaneLengthId, kLen8)
            == Steinberg::kResultOk);
    REQUIRE(controller.setParamNormalized(kArpRatchetLaneLengthId, kLen24)
            == Steinberg::kResultOk);

    Krate::TestSupport::ensureVstguiInitialized();

    auto* editor = new VSTGUI::VST3Editor(
        &controller, "editor",
        (std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc").c_str());

    Steinberg::IPlugView* view = editor;
    REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType())
            == Steinberg::kResultTrue);
    REQUIRE(editor->getFrame() != nullptr);

    RingRenderer* renderer = findRingRenderer(editor->getFrame());
    REQUIRE(renderer != nullptr);

    const auto& geo = renderer->geometry();
    INFO("ring slot 3 (Modifier) step count = " << geo.laneStepCount(3));
    REQUIRE(geo.laneStepCount(3) == 8);
    INFO("ring slot 5 (Ratchet) step count = " << geo.laneStepCount(5));
    REQUIRE(geo.laneStepCount(5) == 24);

    view->removed();
    view->release();
    controller.terminate();
}
