// =============================================================================
// Proxy <-> node wiring of the Shape tab (fix plan T1.20)
// =============================================================================
// The Shape tab's knobs are bound to per-band "Displayed" proxy parameters;
// NodeSelectionController (built in Controller::didOpen) mirrors them onto the
// selected node's real parameters. These cases open the editor headlessly so
// the controllers are wired exactly as the plugin wires them, then drive the
// parameters the way the host and the UI do (setParamNormalized).
//
// D-063: a type change must also write the restored shadow slots into the
//        node's real kNodeShape params, not only into the proxies.
// D-064: the per-type shadow is per node; node B must not use node A's memory.
// D-067: automating a real node param must move its proxy (and only once).
// D-065: DynamicNodeSelector must show SelectedNode, decoded over four nodes.
// =============================================================================

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "controller/views/dynamic_node_selector.h"
#include "dsp/distortion_types.h"
#include "plugin_ids.h"
#include "state_codec.h"

#include "base/source/updatehandler.h"
#include "vstgui/uidescription/uiattributes.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Disrumpo;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

/// Disrumpo controller that records every setParamNormalized / performEdit call.
class NodeWiringRecordingController : public Controller {
public:
    struct Call {
        ParamID id;
        ParamValue value;
    };

    Steinberg::tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override {
        setCalls.push_back({.id = tag, .value = value});
        return Controller::setParamNormalized(tag, value);
    }
    Steinberg::tresult performEdit(ParamID tag, ParamValue value) override {
        editCalls.push_back({.id = tag, .value = value});
        return Controller::performEdit(tag, value);
    }

    void clearCalls() {
        setCalls.clear();
        editCalls.clear();
    }

    std::vector<Call> setCalls;
    std::vector<Call> editCalls;
};

/// Editor opened headlessly (didOpen builds the NodeSelectionControllers), closed on destruction.
class HeadlessEditor {
public:
    explicit HeadlessEditor(Controller& c) {
        Krate::TestSupport::ensureVstguiInitialized();
        editor_ = new VSTGUI::VST3Editor(&c, "editor",
                                         (std::string(DISRUMPO_RESOURCES_DIR) + "/editor.uidesc").c_str());
        Steinberg::IPlugView* view = editor_;
        opened_ = view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue &&
                  editor_->getFrame() != nullptr;
    }
    ~HeadlessEditor() {
        Steinberg::IPlugView* view = editor_;
        view->removed();
        view->release();
    }
    HeadlessEditor(const HeadlessEditor&) = delete;
    HeadlessEditor& operator=(const HeadlessEditor&) = delete;
    HeadlessEditor(HeadlessEditor&&) = delete;
    HeadlessEditor& operator=(HeadlessEditor&&) = delete;

    [[nodiscard]] bool ready() const { return opened_; }

private:
    VSTGUI::VST3Editor* editor_ = nullptr;
    bool opened_ = false;
};

constexpr ParamID kSelectedNodeId = makeBandParamId(0, BandParamType::kBandSelectedNode);
constexpr ParamID kDisplayedTypeId = makeBandParamId(0, BandParamType::kBandDisplayedType);
constexpr ParamID kDisplayedDriveId = makeBandParamId(0, BandParamType::kBandDisplayedDrive);
constexpr ParamID kDisplayedShape0Id = makeBandParamId(0, BandParamType::kBandDisplayedShape0);
constexpr ParamID kActiveNodesId = makeBandParamId(0, BandParamType::kBandActiveNodes);

constexpr ParamID nodeParam(int node, NodeParamType type) {
    return makeNodeParamId(0, static_cast<uint8_t>(node), type);
}

double typeNorm(DistortionType t) { return listIndexToNorm(static_cast<int>(t), kDistortionTypeCount); }

double nodeNorm(int node) { return listIndexToNorm(node, 4); }

}  // namespace

TEST_CASE("Type change writes the restored slots into the node's real shape params", "[disrumpo][ui][D-063]") {
    NodeWiringRecordingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());

        c.setParamNormalized(kSelectedNodeId, nodeNorm(0));
        c.setParamNormalized(nodeParam(0, NodeParamType::kNodeType), typeNorm(DistortionType::Tube));
        REQUIRE(std::fabs(c.getParamNormalized(kDisplayedTypeId) - typeNorm(DistortionType::Tube)) < 1e-6);

        // Turn the slot-0 knob on the Tube page: proxy -> node A's real slot 0.
        c.setParamNormalized(kDisplayedShape0Id, 0.8);
        REQUIRE(std::fabs(c.getParamNormalized(nodeParam(0, NodeParamType::kNodeShape0)) - 0.8) < 1e-6);

        // Pick Fuzz in the type menu: Fuzz's shadow slots (0.5) are restored.
        c.setParamNormalized(kDisplayedTypeId, typeNorm(DistortionType::Fuzz));
        const double proxy = c.getParamNormalized(kDisplayedShape0Id);
        const double real = c.getParamNormalized(nodeParam(0, NodeParamType::kNodeShape0));
        std::printf("T1.20 D-063 after Tube->Fuzz: proxy slot0 %.6f, node A real kNodeShape0 %.6f (want both 0.5)\n",
                    proxy, real);
        CHECK(std::fabs(proxy - 0.5) < 1e-6);
        CHECK(std::fabs(real - proxy) < 1e-9);

        // Every slot, not just slot 0.
        int mismatches = 0;
        for (int s = 0; s < kNumDisplayedProxyParams - 4; ++s) {
            const auto proxyId = makeBandParamId(0, kProxyIndexToBandParam[4 + s]);
            const auto realId = nodeParam(0, kProxyIndexToNodeParam[4 + s]);
            if (std::fabs(c.getParamNormalized(proxyId) - c.getParamNormalized(realId)) > 1e-9) ++mismatches;
        }
        std::printf("T1.20 D-063 slots whose real value differs from the proxy: %d of 10\n", mismatches);
        CHECK(mismatches == 0);
    }
    c.terminate();
}

TEST_CASE("Shadow storage is indexed by the selected node", "[disrumpo][ui][D-064]") {
    NodeWiringRecordingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());

        c.setParamNormalized(kActiveNodesId, listIndexToNorm(1, 4));  // 2 nodes
        c.setParamNormalized(nodeParam(0, NodeParamType::kNodeType), typeNorm(DistortionType::SoftClip));
        c.setParamNormalized(nodeParam(1, NodeParamType::kNodeType), typeNorm(DistortionType::Tube));

        // Node B on Tube: slot 0 = 0.9, then Fuzz and back to Tube.
        c.setParamNormalized(kSelectedNodeId, nodeNorm(1));
        // The open editor's node selectors follow SelectedNode without writing it back (D-065).
        REQUIRE(c.getParamNormalized(kSelectedNodeId) == nodeNorm(1));
        REQUIRE(std::fabs(c.getParamNormalized(kDisplayedTypeId) - typeNorm(DistortionType::Tube)) < 1e-6);
        c.setParamNormalized(kDisplayedShape0Id, 0.9);
        c.setParamNormalized(kDisplayedTypeId, typeNorm(DistortionType::Fuzz));
        c.setParamNormalized(kDisplayedTypeId, typeNorm(DistortionType::Tube));
        const double bProxy = c.getParamNormalized(kDisplayedShape0Id);
        const double bReal = c.getParamNormalized(nodeParam(1, NodeParamType::kNodeShape0));
        std::printf("T1.20 D-064 node B back on Tube: proxy slot0 %.6f, real %.6f (want 0.9)\n", bProxy, bReal);
        CHECK(std::fabs(bProxy - 0.9) < 1e-6);
        CHECK(std::fabs(bReal - 0.9) < 1e-6);

        // Node A has never been Tube: switching it to Tube must restore A's own default (0.5).
        c.setParamNormalized(kSelectedNodeId, nodeNorm(0));
        REQUIRE(std::fabs(c.getParamNormalized(kDisplayedTypeId) - typeNorm(DistortionType::SoftClip)) < 1e-6);
        c.setParamNormalized(kDisplayedTypeId, typeNorm(DistortionType::Tube));
        const double aProxy = c.getParamNormalized(kDisplayedShape0Id);
        std::printf("T1.20 D-064 node A switched to Tube: proxy slot0 %.6f (want A's own 0.5, not B's 0.9)\n",
                    aProxy);
        CHECK(std::fabs(aProxy - 0.5) < 1e-6);
    }
    c.terminate();
}

TEST_CASE("Automating a node param moves its proxy", "[disrumpo][ui][D-067]") {
    NodeWiringRecordingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());

        c.setParamNormalized(kActiveNodesId, listIndexToNorm(1, 4));  // 2 nodes
        c.setParamNormalized(kSelectedNodeId, nodeNorm(0));

        // Host automation of the selected node's Drive: one call, nothing else.
        c.clearCalls();
        c.setParamNormalized(nodeParam(0, NodeParamType::kNodeDrive), 0.7);
        const double proxy = c.getParamNormalized(kDisplayedDriveId);

        int proxySets = 0;
        int realSets = 0;  // excluding the host's own call (setCalls[0])
        for (size_t i = 0; i < c.setCalls.size(); ++i) {
            if (c.setCalls[i].id == kDisplayedDriveId) ++proxySets;
            if (i > 0 && isNodeParamId(c.setCalls[i].id)) ++realSets;
        }
        int realEdits = 0;
        for (const auto& call : c.editCalls)
            if (isNodeParamId(call.id)) ++realEdits;
        std::printf("T1.20 D-067 automate node A Drive 0.7: proxy %.6f, proxy sets %d, extra real sets %d, "
                    "real performEdits %d, total set calls %zu\n",
                    proxy, proxySets, realSets, realEdits, c.setCalls.size());
        CHECK(std::fabs(proxy - 0.7) < 1e-6);
        CHECK(proxySets == 1);
        CHECK(realSets == 0);
        CHECK(realEdits == 0);

        // A shape slot follows too.
        c.setParamNormalized(nodeParam(0, NodeParamType::kNodeShape3), 0.25);
        const double shape3 = c.getParamNormalized(makeBandParamId(0, BandParamType::kBandDisplayedShape3));
        std::printf("T1.20 D-067 automate node A Shape3 0.25: proxy %.6f\n", shape3);
        CHECK(std::fabs(shape3 - 0.25) < 1e-6);

        // A node that is not selected leaves the proxy alone.
        c.setParamNormalized(nodeParam(1, NodeParamType::kNodeDrive), 0.2);
        const double after = c.getParamNormalized(kDisplayedDriveId);
        std::printf("T1.20 D-067 automate node B Drive 0.2 while A selected: proxy %.6f (want 0.7)\n", after);
        CHECK(std::fabs(after - 0.7) < 1e-6);
    }
    c.terminate();
}

TEST_CASE("DynamicNodeSelector highlights the selected node with 2 nodes", "[disrumpo][ui][D-065]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::ensureVstguiInitialized();

    c.setParamNormalized(kActiveNodesId, listIndexToNorm(1, 4));  // 2 nodes
    c.setParamNormalized(kSelectedNodeId, nodeNorm(1));           // node B

    for (const bool compact : {false, true}) {
        VSTGUI::UIAttributes attributes;
        attributes.setAttribute("band", "0");
        attributes.setAttribute("size", "130, 16");
        if (compact) attributes.setAttribute("auto-hide-single", "true");
        auto* view = c.createCustomView("DynamicNodeSelector", attributes, nullptr, nullptr);
        REQUIRE(view != nullptr);
        auto* selector = dynamic_cast<DynamicNodeSelector*>(view);
        REQUIRE(selector != nullptr);

        const uint32_t atOpen = selector->getSelectedSegment();
        c.setParamNormalized(kSelectedNodeId, nodeNorm(0));
        const uint32_t afterA = selector->getSelectedSegment();
        c.setParamNormalized(kSelectedNodeId, nodeNorm(1));
        const uint32_t afterB = selector->getSelectedSegment();
        std::printf("T1.20 D-065 %s selector, %zu segments: at open %u (want 1), SelectedNode=A -> %u (want 0), "
                    "SelectedNode=B -> %u (want 1)\n",
                    compact ? "compact" : "expanded", selector->getSegments().size(), atOpen, afterA, afterB);
        CHECK(atOpen == 1);
        CHECK(afterA == 0);
        CHECK(afterB == 1);
        view->forget();
    }
    c.terminate();
}

TEST_CASE("Editor open adds and close removes every dependent registration", "[disrumpo][ui][lifecycle]") {
    NodeWiringRecordingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    auto* handler = Steinberg::UpdateHandler::instance();
    REQUIRE(handler != nullptr);

    const size_t baseline = handler->countDependencies();
    size_t whileOpen = 0;
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        whileOpen = handler->countDependencies();
    }
    const size_t afterClose = handler->countDependencies();
    std::printf("T1.20 dependents: baseline %zu, editor open %zu (+%zu), after close %zu\n", baseline, whileOpen,
                whileOpen - baseline, afterClose);
    CHECK(afterClose == baseline);
    c.terminate();
}
