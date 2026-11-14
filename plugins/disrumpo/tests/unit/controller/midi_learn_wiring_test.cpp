// =============================================================================
// MIDI learn wiring (fix plan T1.18)
// =============================================================================
// Audit findings D-136 / D-096 / D-058 / D-094: the context-menu MIDI Learn
// armed the shared MidiCCManager, but nothing ever completed the learn (no
// event input bus, no IMidiLearn, a separate processor-side learn keyed on a
// different parameter). "Save Mapping with Preset" moved a mapping into
// storage Controller::getState never wrote, and no mapping change told the
// host to re-query IMidiMapping.
//
// One design now: the processor declares one event input bus so the host
// routes MIDI CC to the plugin; the controller implements IMidiLearn, so the
// host reports live CCs to the UI thread and an armed learn completes there;
// every mapping change calls restartComponent(kMidiCCAssignmentChanged); the
// controller state (version 3) carries the global and the per-preset blob.
// The host turns a mapped CC into a parameter change itself (IMidiMapping):
// the processor sees only the parameter, never the CC.
// =============================================================================

#include "../../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "plugin_ids.h"

#include "midi/midi_cc_manager.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidilearn.h"
#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;

namespace {

std::vector<uint8_t> drain(Steinberg::IBStream* s) {
    Steinberg::int64 size = 0;
    s->seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> out(static_cast<size_t>(size));
    Steinberg::int32 got = 0;
    if (size > 0) s->read(out.data(), static_cast<Steinberg::int32>(size), &got);
    return out;
}

/// A MemoryStream holding a copy of `bytes`, positioned at 0. Caller releases.
Steinberg::MemoryStream* streamOf(std::vector<uint8_t> bytes) {
    auto* s = new Steinberg::MemoryStream();
    Steinberg::int32 written = 0;
    s->write(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &written);
    s->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return s;
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int32_t readI32(const std::vector<uint8_t>& b, size_t off) {
    REQUIRE(off + 4 <= b.size());
    int32_t v = 0;
    std::memcpy(&v, b.data() + off, sizeof v);
    return v;
}

std::vector<uint8_t> controllerState(Controller& c) {
    auto* out = new Steinberg::MemoryStream();
    REQUIRE(c.getState(out) == Steinberg::kResultOk);
    auto bytes = drain(out);
    out->release();
    return bytes;
}

bool loadControllerState(Controller& c, const std::vector<uint8_t>& bytes) {
    auto* in = streamOf(bytes);
    const bool ok = c.setState(in) == Steinberg::kResultOk;
    in->release();
    return ok;
}

/// Stub host component handler: records every restartComponent flag set.
class RecordingHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid)) {
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID /*id*/,
                                              Steinberg::Vst::ParamValue /*value*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        restarts.push_back(flags);
        return Steinberg::kResultOk;
    }

    [[nodiscard]] int ccAssignmentRestarts() const {
        int n = 0;
        for (const auto f : restarts)
            if ((f & Steinberg::Vst::kMidiCCAssignmentChanged) != 0) ++n;
        return n;
    }

    std::vector<Steinberg::int32> restarts;
};

/// The controller's IMidiLearn, or nullptr when it does not implement it.
Steinberg::Vst::IMidiLearn* midiLearnOf(Controller& c) {
    void* obj = nullptr;
    auto* unknown = static_cast<Steinberg::Vst::IEditController*>(&c);
    if (unknown->queryInterface(Steinberg::Vst::IMidiLearn::iid, &obj) != Steinberg::kResultOk) return nullptr;
    auto* learn = static_cast<Steinberg::Vst::IMidiLearn*>(obj);
    learn->release();  // queryInterface added a reference; the test owns the controller
    return learn;
}

constexpr Steinberg::Vst::ParamID kMixId = makeGlobalParamId(GlobalParamType::kGlobalMix);
constexpr Steinberg::Vst::ParamID kLearnActiveId = makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive);
constexpr Steinberg::Vst::ParamID kSweepFreqId = makeSweepParamId(SweepParamType::kSweepFrequency);
constexpr Steinberg::Vst::ParamID kCrossover0Id = makeCrossoverParamId(0);

// The v2 fixture (captured from Controller::getState before T1.18): CC 74 -> Sweep
// Frequency (7-bit), CC 1 -> Crossover 1 (14-bit), both global.
const char* const kV2Fixture = DISRUMPO_FIXTURES_DIR "/disrumpo_controller_state_v2.bin";

}  // namespace

TEST_CASE("Context-menu MIDI Learn completes from a live CC and persists", "[disrumpo][midi][D-136][D-058]") {
    RecordingHandler handler;
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    c.setComponentHandler(&handler);

    c.startMidiLearn(kMixId);
    CHECK(c.getParamNormalized(kLearnActiveId) == 1.0);

    // The host reports the live CC to the UI thread through IMidiLearn.
    auto* learn = midiLearnOf(c);
    std::printf("D-136 controller implements IMidiLearn: %s\n", learn ? "yes" : "no");
    REQUIRE(learn != nullptr);
    CHECK(learn->onLiveMIDIControllerInput(0, 0, 7) == Steinberg::kResultOk);

    Steinberg::Vst::ParamID id = 0;
    const auto resolved = c.getMidiControllerAssignment(0, 0, 7, id);
    std::printf("D-136 after learn: getMidiControllerAssignment(CC 7) = %d id=0x%X (want 0x%X), "
                "restartComponent(kMidiCCAssignmentChanged) x%d, learn flag %.1f\n",
                static_cast<int>(resolved), static_cast<unsigned>(id), static_cast<unsigned>(kMixId),
                handler.ccAssignmentRestarts(), c.getParamNormalized(kLearnActiveId));
    REQUIRE(resolved == Steinberg::kResultTrue);
    CHECK(id == kMixId);
    CHECK(handler.ccAssignmentRestarts() == 1);
    CHECK(c.getParamNormalized(kLearnActiveId) == 0.0);

    // A second live CC after the learn completed changes nothing.
    CHECK(learn->onLiveMIDIControllerInput(0, 0, 11) == Steinberg::kResultOk);
    CHECK(c.getMidiControllerAssignment(0, 0, 11, id) == Steinberg::kResultFalse);
    CHECK(handler.ccAssignmentRestarts() == 1);

    // Scope toggle to per-preset: the host is told, and the mapping persists.
    c.toggleMidiMappingScope(kMixId);
    CHECK(handler.ccAssignmentRestarts() == 2);
    CHECK(c.getMidiControllerAssignment(0, 0, 7, id) == Steinberg::kResultTrue);

    const auto saved = controllerState(c);
    CHECK(readI32(saved, 0) == 3);

    RecordingHandler handler2;
    Controller c2;
    REQUIRE(c2.initialize(nullptr) == Steinberg::kResultOk);
    c2.setComponentHandler(&handler2);
    REQUIRE(loadControllerState(c2, saved));
    id = 0;
    const auto restored = c2.getMidiControllerAssignment(0, 0, 7, id);
    std::printf("D-058 per-preset mapping after getState/setState: %d id=0x%X, restarts on load x%d\n",
                static_cast<int>(restored), static_cast<unsigned>(id), handler2.ccAssignmentRestarts());
    REQUIRE(restored == Steinberg::kResultTrue);
    CHECK(id == kMixId);
    CHECK(handler2.ccAssignmentRestarts() == 1);

    // And it is still per-preset: toggling it back makes it global again, still resolved.
    c2.toggleMidiMappingScope(kMixId);
    CHECK(c2.getMidiControllerAssignment(0, 0, 7, id) == Steinberg::kResultTrue);
    CHECK(id == kMixId);

    c2.terminate();
    c.terminate();
}

TEST_CASE("Per-preset MIDI mapping survives controller getState/setState", "[disrumpo][midi][D-058]") {
    RecordingHandler handler;
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    c.setComponentHandler(&handler);
    REQUIRE(loadControllerState(c, readFileBytes(kV2Fixture)));
    const int restartsAfterLoad = handler.ccAssignmentRestarts();

    c.toggleMidiMappingScope(kSweepFreqId);  // CC 74: global -> per-preset
    const int restartsAfterToggle = handler.ccAssignmentRestarts();

    Controller c2;
    REQUIRE(c2.initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(loadControllerState(c2, controllerState(c)));
    Steinberg::Vst::ParamID id74 = 0;
    Steinberg::Vst::ParamID id1 = 0;
    const auto r74 = c2.getMidiControllerAssignment(0, 0, 74, id74);
    const auto r1 = c2.getMidiControllerAssignment(0, 0, 1, id1);
    std::printf("D-058 v2 load restarts x%d, toggle restarts x%d; reloaded CC 74 (per-preset) = %d id=0x%X, "
                "CC 1 (global) = %d id=0x%X\n",
                restartsAfterLoad, restartsAfterToggle - restartsAfterLoad, static_cast<int>(r74),
                static_cast<unsigned>(id74), static_cast<int>(r1), static_cast<unsigned>(id1));
    CHECK(restartsAfterLoad == 1);
    CHECK(restartsAfterToggle == 2);
    REQUIRE(r74 == Steinberg::kResultTrue);
    CHECK(id74 == kSweepFreqId);
    REQUIRE(r1 == Steinberg::kResultTrue);
    CHECK(id1 == kCrossover0Id);

    c2.terminate();
    c.terminate();
}

TEST_CASE("Clear MIDI Learn removes the mapping and tells the host", "[disrumpo][midi][D-058]") {
    RecordingHandler handler;
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(loadControllerState(c, readFileBytes(kV2Fixture)));
    c.setComponentHandler(&handler);

    c.clearMidiLearn(kSweepFreqId);
    Steinberg::Vst::ParamID id = 0;
    CHECK(c.getMidiControllerAssignment(0, 0, 74, id) == Steinberg::kResultFalse);
    CHECK(c.getMidiControllerAssignment(0, 0, 1, id) == Steinberg::kResultTrue);
    std::printf("D-058 clear: restartComponent(kMidiCCAssignmentChanged) x%d\n", handler.ccAssignmentRestarts());
    CHECK(handler.ccAssignmentRestarts() == 1);
    c.terminate();
}

TEST_CASE("Controller state v2 fixture loads with its global mappings and no per-preset mappings",
          "[disrumpo][midi][state][D-058]") {
    const auto v2 = readFileBytes(kV2Fixture);
    REQUIRE(readI32(v2, 0) == 2);

    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    // A per-preset mapping from before the load must not survive a v2 load (v2 has none).
    REQUIRE(loadControllerState(c, v2));
    c.toggleMidiMappingScope(kSweepFreqId);
    REQUIRE(loadControllerState(c, v2));

    Steinberg::Vst::ParamID id = 0;
    REQUIRE(c.getMidiControllerAssignment(0, 0, 74, id) == Steinberg::kResultTrue);
    CHECK(id == kSweepFreqId);
    REQUIRE(c.getMidiControllerAssignment(0, 0, 1, id) == Steinberg::kResultTrue);
    CHECK(id == kCrossover0Id);
    CHECK(c.getMidiControllerAssignment(0, 0, 7, id) == Steinberg::kResultFalse);

    // Re-saved as v3: the global blob is the fixture's, the per-preset blob is empty.
    const auto v3 = controllerState(c);
    const int32_t globalSize = readI32(v3, 20);
    REQUIRE(globalSize >= 0);
    const size_t presetOffset = 24 + static_cast<size_t>(globalSize);
    std::printf("D-058 v2 fixture (%zu bytes) re-saved: version=%d size=%zu global blob %d bytes (fixture %d), "
                "per-preset count %d\n",
                v2.size(), readI32(v3, 0), v3.size(), globalSize, readI32(v2, 20),
                v3.size() >= presetOffset + 8 ? readI32(v3, presetOffset + 4) : -1);
    REQUIRE(readI32(v3, 0) == 3);
    REQUIRE(globalSize == readI32(v2, 20));
    REQUIRE(v3.size() == v2.size() + 4 + 4);
    CHECK(readI32(v3, presetOffset) == 4);      // per-preset blob size
    CHECK(readI32(v3, presetOffset + 4) == 0);  // per-preset count
    c.terminate();
}

TEST_CASE("Legacy sweep MIDI learn parameters are hidden and not automatable", "[disrumpo][midi][D-094]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const Steinberg::Vst::ParamID legacy[] = {makeSweepParamId(SweepParamType::kSweepMidiLearnActive),
                                              makeSweepParamId(SweepParamType::kSweepMidiCCNumber),
                                              kSweepDetectedCCOutputId};
    for (const auto pid : legacy) {
        auto* param = c.getParameterObject(pid);
        REQUIRE(param != nullptr);  // still registered: the IDs are never reused
        const auto flags = param->getInfo().flags;
        std::printf("D-094 param 0x%X flags 0x%X\n", static_cast<unsigned>(pid), static_cast<unsigned>(flags));
        CHECK((flags & Steinberg::Vst::ParameterInfo::kIsHidden) != 0);
        CHECK((flags & Steinberg::Vst::ParameterInfo::kCanAutomate) == 0);
    }
    c.terminate();
}

TEST_CASE("A learned CC moves its parameter through the host's IMidiMapping", "[disrumpo][midi][D-096]") {
    // VST3 has no CC input event: the host asks IMidiMapping which parameter a CC
    // drives and sends the CC value as an ordinary parameter change. That is the
    // path exercised here, end to end through the processor.
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    c.startMidiLearn(kMixId);
    auto* learn = midiLearnOf(c);
    REQUIRE(learn != nullptr);
    REQUIRE(learn->onLiveMIDIControllerInput(0, 0, 7) == Steinberg::kResultOk);

    Steinberg::Vst::ParamID id = 0;
    REQUIRE(c.getMidiControllerAssignment(0, 0, 7, id) == Steinberg::kResultTrue);

    Rig rig(44100.0);
    const double ccValue = 100.0 / 127.0;
    rig.apply({{kMixId, 0.0}});
    float mixBefore = 0.0f;
    std::memcpy(&mixBefore, getStateBytes(*rig.p).data() + 12, sizeof mixBefore);
    rig.apply({{id, ccValue}});  // host: CC 7 value 100 on the mapped parameter
    float mixAfter = 0.0f;
    std::memcpy(&mixAfter, getStateBytes(*rig.p).data() + 12, sizeof mixAfter);
    std::printf("D-096 CC 7 = 100 via mapping -> param 0x%X: processor mix %.6f -> %.6f (want %.6f)\n",
                static_cast<unsigned>(id), static_cast<double>(mixBefore), static_cast<double>(mixAfter), ccValue);
    CHECK(mixBefore == 0.0f);
    CHECK(static_cast<double>(mixAfter) == Catch::Approx(ccValue).margin(1e-6));
    c.terminate();
}
