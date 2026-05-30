// ==============================================================================
// Controller Preset-Save Delegation Tests
// ==============================================================================
// Regression guard for the rank-10 consolidation: Controller::createComponentStateStream()
// must delegate to the host's IComponent::getState() rather than hand re-serializing
// parameters in the controller.
//
// The old hand-written re-serializer drifted from Processor::getState() -- notably it
// wrote a block of hardcoded placeholder values for the Freeze mode instead of the live
// freeze parameters, so freeze settings were silently lost when saving a preset. By
// delegating to the processor, the saved stream is guaranteed to match the processor
// state byte-for-byte, which this test asserts.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"  // IComponentHandler
#include "pluginterfaces/vst/ivstaudioprocessor.h"  // ProcessSetup
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>
#include <vector>

namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Iterum;

// -----------------------------------------------------------------------------
// Minimal host bridge: acts as the controller's IComponentHandler and answers a
// queryInterface for IComponent by delegating getState() to a real Processor,
// mirroring how a VST3 host connects the two components.
// -----------------------------------------------------------------------------
class ProcessorBackedHostBridge : public IComponentHandler, public IComponent {
public:
    explicit ProcessorBackedHostBridge(Processor& processor) : processor_(processor) {}

    // ---- FUnknown ----
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IPluginBase::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IComponent::iid, IComponent)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    // ---- IComponentHandler ----
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

    // ---- IPluginBase ----
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    // ---- IComponent ----
    tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType, BusDirection) override { return 0; }
    tresult PLUGIN_API getBusInfo(MediaType, BusDirection, int32, BusInfo&) override {
        return kResultFalse;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override {
        return kResultFalse;
    }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override {
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool) override { return kResultOk; }
    tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState(IBStream* state) override {
        return processor_.getState(state);
    }

private:
    Processor& processor_;
};

std::vector<char> readAll(MemoryStream& stream) {
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    std::vector<char> bytes(static_cast<size_t>(stream.getSize()));
    if (!bytes.empty()) {
        int32 numRead = 0;
        stream.read(bytes.data(), static_cast<int32>(bytes.size()), &numRead);
        bytes.resize(static_cast<size_t>(numRead));
    }
    return bytes;
}

TEST_CASE("Controller preset save delegates to host component getState",
          "[controller][preset][state]") {
    Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    SECTION("Without a component handler there is no re-serialization fallback") {
        REQUIRE(controller.createComponentStateStream() == nullptr);
    }

    SECTION("Saved stream matches Processor::getState byte-for-byte") {
        Processor processor;
        REQUIRE(processor.initialize(nullptr) == kResultOk);

        ProcessSetup setup;
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 512;
        setup.sampleRate = 44100.0;
        processor.setupProcessing(setup);

        ProcessorBackedHostBridge bridge(processor);
        REQUIRE(controller.setComponentHandler(&bridge) == kResultOk);

        MemoryStream* controllerStream = controller.createComponentStateStream();
        REQUIRE(controllerStream != nullptr);

        MemoryStream processorStream;
        REQUIRE(processor.getState(&processorStream) == kResultOk);

        std::vector<char> fromController = readAll(*controllerStream);
        std::vector<char> fromProcessor = readAll(processorStream);

        // Delegation guarantee: the preset stream IS the processor state. A
        // regression to hand re-serialization (e.g. the old hardcoded Freeze
        // placeholder block) would change the size and/or bytes here.
        REQUIRE(fromController.size() == fromProcessor.size());
        REQUIRE(fromController == fromProcessor);

        controllerStream->release();
    }
}

} // namespace
