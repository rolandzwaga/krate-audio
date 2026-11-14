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

#include "../../test_support/disrumpo_rig.h"
#include "../../test_support/headless_editor.h"

#include "controller/controller.h"
#include "controller/views/dynamic_node_selector.h"
#include "dsp/distortion_types.h"
#include "dsp/shape_slot_descriptors.h"
#include "plugin_ids.h"
#include "state_codec.h"

#include "base/source/updatehandler.h"
#include "vstgui/uidescription/uiattributes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
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

using Disrumpo::TestSupport::HeadlessEditor;

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

// =============================================================================
// The band header's Bias knob (Band.NodeBias, proxy kBandDisplayedBias) and a type's own
// Bias slot are one control (user ruling 2026-11-10, follow-up 87): since the slot wins
// over kNodeBias in the processor, the knob edits the selected node's Bias slot for Tube,
// Tape, Fuzz, Asymmetric Fuzz, the three wavefolders and Ring Saturation (kNodeBias mirrors
// it), the slot's own knob moves the header knob, and the edits change the sound. A type
// without a Bias slot keeps the node parameter.
// =============================================================================

namespace {

constexpr ParamID kDisplayedBiasId = makeBandParamId(0, BandParamType::kBandDisplayedBias);

/// One band, node 0 alone at drive 5, the controller's node-parameter edits applied, 220 Hz
/// at -12 dBFS for 0.25 s at 48 kHz: the left channel.
std::vector<float> renderWithEdits(DistortionType type, const std::vector<NodeWiringRecordingController::Call>& edits) {
    using namespace Disrumpo::TestSupport;
    constexpr double kSr = 48000.0;
    Rig rig(kSr);
    rig.apply({{kBandCountId, 0.0},
               {makeBandParamId(0, BandParamType::kBandActiveNodes), 0.0},
               {nodeParam(0, NodeParamType::kNodeType), typeNorm(type)},
               {nodeParam(0, NodeParamType::kNodeDrive), 0.5}});
    PList list;
    for (const auto& e : edits) {
        if (isNodeParamId(e.id)) list.emplace_back(e.id, e.value);
    }
    rig.apply(list);
    rig.reactivate();
    const auto n = static_cast<size_t>(kSr * 0.25);
    std::vector<float> in(n);
    for (size_t i = 0; i < n; ++i) {
        in[i] = static_cast<float>(0.25118864315095801 *
                                   std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kSr));
    }
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    return outL;
}

}  // namespace

TEST_CASE("The band header's Bias knob edits the Bias slot of the types that have one",
          "[disrumpo][ui][bias][D-160]") {
    int typesWithSlot = 0;
    for (int t = 0; t < kDistortionTypeCount; ++t) {
        const auto type = static_cast<DistortionType>(t);
        const int slot = biasShapeSlot(type);
        if (slot < 0 && type != DistortionType::SoftClip) continue;  // SoftClip: the no-slot control
        INFO("type " << t << ", Bias slot " << slot);
        NodeWiringRecordingController c;
        REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
        {
            HeadlessEditor ed(c);
            REQUIRE(ed.ready());
            c.setParamNormalized(kSelectedNodeId, nodeNorm(0));
            c.setParamNormalized(nodeParam(0, NodeParamType::kNodeType), typeNorm(type));

            c.clearCalls();
            c.setParamNormalized(kDisplayedBiasId, 0.2);
            const auto editsLow = c.editCalls;
            const double biasNode = c.getParamNormalized(nodeParam(0, NodeParamType::kNodeBias));
            if (slot < 0) {
                std::printf("bias header: type %d (no Bias slot): kNodeBias %.4f\n", t, biasNode);
                CHECK(std::fabs(biasNode - 0.2) < 1e-6);  // the proxy copy runs in float
            } else {
                ++typesWithSlot;
                const auto slotNodeId = makeNodeParamId(0, 0, kProxyIndexToNodeParam[4 + slot]);
                const auto slotProxyId = makeBandParamId(0, kProxyIndexToBandParam[4 + slot]);
                const double slotNode = c.getParamNormalized(slotNodeId);
                const double slotProxy = c.getParamNormalized(slotProxyId);

                c.clearCalls();
                c.setParamNormalized(kDisplayedBiasId, 0.8);
                const auto editsHigh = c.editCalls;
                const auto low = renderWithEdits(type, editsLow);
                const auto high = renderWithEdits(type, editsHigh);
                double diff = 0.0;
                for (size_t i = 0; i < low.size(); ++i) {
                    diff = std::max(diff, std::fabs(static_cast<double>(low[i]) - static_cast<double>(high[i])));
                }

                // The other direction: the slot's knob moves the header knob
                c.setParamNormalized(slotProxyId, 0.65);
                const double header = c.getParamNormalized(kDisplayedBiasId);
                const double slotAfter = c.getParamNormalized(slotNodeId);
                // ... and host automation of the slot too
                c.setParamNormalized(slotNodeId, 0.35);
                const double headerAuto = c.getParamNormalized(kDisplayedBiasId);
                std::printf("bias header: type %d slot %d: header 0.2 -> slot %.4f (proxy %.4f), kNodeBias %.4f; "
                            "render header 0.2 vs 0.8 max diff %.4g; slot knob 0.65 -> header %.4f (slot %.4f); "
                            "slot automated 0.35 -> header %.4f\n",
                            t, slot, slotNode, slotProxy, biasNode, diff, header, slotAfter, headerAuto);
                CHECK(std::fabs(slotNode - 0.2) < 1e-9);
                CHECK(std::fabs(slotProxy - 0.2) < 1e-9);
                CHECK(std::fabs(biasNode - 0.2) < 1e-9);
                CHECK(diff > 1e-3);
                CHECK(std::fabs(header - 0.65) < 1e-9);
                CHECK(std::fabs(slotAfter - 0.65) < 1e-9);
                CHECK(std::fabs(headerAuto - 0.35) < 1e-9);
            }
        }
        c.terminate();
    }
    CHECK(typesWithSlot == 8);
}

// The band header's Bias knob (and its label) shows only while the selected node's type has a
// Bias slot (user ruling 2026-11-10): the other 18 types do not use bias. It follows a type
// change on the selected node and a change of the selected node.
TEST_CASE("The band header's Bias knob shows only for the types that use bias", "[disrumpo][ui][bias]") {
    NodeWiringRecordingController c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    {
        HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        auto* handler = Steinberg::UpdateHandler::instance();
        const auto biasTag = static_cast<int32_t>(kDisplayedBiasId);
        const auto shown = [&]() {
            handler->triggerDeferedUpdates();
            auto* group = Disrumpo::TestSupport::findContainerHoldingTag(ed.frame(), biasTag);
            REQUIRE(group != nullptr);
            return group->isVisible();
        };
        c.setParamNormalized(kActiveNodesId, listIndexToNorm(1, 4));  // 2 nodes
        c.setParamNormalized(kSelectedNodeId, nodeNorm(0));
        int wrong = 0;
        for (int t = 0; t < kDistortionTypeCount; ++t) {
            const auto type = static_cast<DistortionType>(t);
            c.setParamNormalized(nodeParam(0, NodeParamType::kNodeType), typeNorm(type));
            const bool want = biasShapeSlot(type) >= 0;
            const bool got = shown();
            if (got != want) {
                std::printf("header Bias knob: type %d shown %d, want %d\n", t, got ? 1 : 0, want ? 1 : 0);
                ++wrong;
            }
        }
        std::printf("header Bias knob: %d of %d types shown wrongly\n", wrong, kDistortionTypeCount);
        CHECK(wrong == 0);

        // Follows the selected node: node 0 Tube (Bias slot), node 1 SoftClip (none)
        c.setParamNormalized(nodeParam(0, NodeParamType::kNodeType), typeNorm(DistortionType::Tube));
        c.setParamNormalized(nodeParam(1, NodeParamType::kNodeType), typeNorm(DistortionType::SoftClip));
        CHECK(shown());
        c.setParamNormalized(kSelectedNodeId, nodeNorm(1));
        CHECK_FALSE(shown());
        c.setParamNormalized(kSelectedNodeId, nodeNorm(0));
        CHECK(shown());
        // ... and a type change on it from the header's type menu
        c.setParamNormalized(kDisplayedTypeId, typeNorm(DistortionType::HardClip));
        CHECK_FALSE(shown());
        c.setParamNormalized(kDisplayedTypeId, typeNorm(DistortionType::Fuzz));
        CHECK(shown());
    }
    c.terminate();
}
