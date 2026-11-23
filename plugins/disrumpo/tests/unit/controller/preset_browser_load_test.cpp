// =============================================================================
// One preset-load path (fix plan T1.38, R-204)
// =============================================================================
// A preset-browser load used to replay the controller's parser with performEdit
// and never ran Processor::setState. The per-type shape shadows (26 types x 10
// slots per node) are not parameters, so they reached only the controller: the
// processor kept the previous patch's shadows, a node-type change by automation
// then loaded stale slots, and a project save wrote them.
// =============================================================================

#include "../../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "preset/disrumpo_preset_config.h"
#include "processor/processor.h"

#include "preset/preset_info.h"
#include "preset/preset_manager.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace Disrumpo;
using namespace Disrumpo::TestSupport;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// --- Processor stream layout (Processor::writeState, version 13, kMaxBands = 4) ---
// version, 3 globals, band count, 4 x (gain, pan, solo, bypass, mute), 3 crossovers
constexpr size_t kSweepOffset = 4 + 12 + 4 + 4 * 11 + 3 * 4;
// sweep core (15), sweep LFO (12), sweep envelope (13), then the custom curve's point count
constexpr size_t kCurveCountOffset = kSweepOffset + 15 + 12 + 13;
// LFO 1/2 (2 x 13), env follower 13, random 9, chaos 9, S&H 9, pitch 16, transient 12,
// rungler 10, macros 4 x 13, routings 32 x 10
constexpr size_t kModulationBytes = 26 + 13 + 9 + 9 + 9 + 16 + 12 + 10 + 52 + 320;
constexpr size_t kBandHeaderBytes = 16;                  // morph X/Y, mode, count, smoothing, X/Y link
constexpr size_t kNodeSlotsOffset = 1 + 6 * 4;           // type, drive, mix, tone, bias, folds, bits
constexpr size_t kNodeShadowOffset = kNodeSlotsOffset + 10 * 4;
constexpr size_t kNodeBytes = kNodeShadowOffset + static_cast<size_t>(kDistortionTypeCount) * 10 * 4;
constexpr size_t kBandBytes = kBandHeaderBytes + 4 * kNodeBytes;
constexpr size_t kTrailerBytes = 4 + 4 * 4 * 12 + 12;    // node positions block, instance-ID block
constexpr size_t kInstanceIdBytes = 12;                  // marker + int64, the stream's last 12 bytes

size_t morphStart(const std::vector<uint8_t>& s) {
    int32_t points = 0;
    REQUIRE(s.size() > kCurveCountOffset + 4);
    std::memcpy(&points, s.data() + kCurveCountOffset, sizeof points);
    REQUIRE(points >= 0);
    return kCurveCountOffset + 4 + static_cast<size_t>(points) * 8 + kModulationBytes;
}

size_t nodeOffset(const std::vector<uint8_t>& s, int band, int node) {
    const size_t start = morphStart(s);
    REQUIRE(s.size() == start + 4 * kBandBytes + kTrailerBytes);  // the layout above holds
    return start + static_cast<size_t>(band) * kBandBytes + kBandHeaderBytes + static_cast<size_t>(node) * kNodeBytes;
}

float floatAt(const std::vector<uint8_t>& s, size_t off) {
    float v = 0.0f;
    std::memcpy(&v, s.data() + off, sizeof v);
    return v;
}

/// The node's live shape slots in a stream.
std::vector<float> shapeSlots(const std::vector<uint8_t>& s, int band, int node) {
    const size_t at = nodeOffset(s, band, node) + kNodeSlotsOffset;
    std::vector<float> out;
    for (size_t i = 0; i < 10; ++i) out.push_back(floatAt(s, at + i * 4));
    return out;
}

/// The node's stored shadow of `type` in a stream.
std::vector<float> shadowSlots(const std::vector<uint8_t>& s, int band, int node, DistortionType type) {
    const size_t at = nodeOffset(s, band, node) + kNodeShadowOffset + static_cast<size_t>(type) * 40;
    std::vector<float> out;
    for (size_t i = 0; i < 10; ++i) out.push_back(floatAt(s, at + i * 4));
    return out;
}

std::vector<uint8_t> withoutInstanceId(std::vector<uint8_t> s) {
    REQUIRE(s.size() > kInstanceIdBytes);
    s.resize(s.size() - kInstanceIdBytes);
    return s;
}

ParamID nodeId(int band, int node, NodeParamType type) {
    return makeNodeParamId(static_cast<uint8_t>(band), static_cast<uint8_t>(node), type);
}

ParamID shapeId(int band, int node, int slot) {
    return nodeId(band, node, static_cast<NodeParamType>(static_cast<uint8_t>(NodeParamType::kNodeShape0) + slot));
}

double typeNorm(DistortionType t) { return static_cast<double>(t) / 25.0; }

constexpr DistortionType kShadowType = DistortionType::SineFold;   // never the current type below
constexpr DistortionType kCurrentType = DistortionType::Tube;

/// Preset A (variantB false) or B, written by a processor the way a user builds a patch.
/// On bands 0 and 1, node 0 was set to SineFold, its shape edited, and set back to Tube,
/// so the SineFold shadow differs between A and B while the current type is the same.
std::vector<uint8_t> makePreset(bool variantB) {
    Rig rig(48000.0);
    const auto pick = [variantB](double a, double b) { return variantB ? b : a; };
    rig.apply({{makeGlobalParamId(GlobalParamType::kGlobalInputGain), pick(0.25, 0.75)},
               {nodeId(0, 0, NodeParamType::kNodeDrive), pick(0.2, 0.6)}});
    for (int band = 0; band < 2; ++band) {
        rig.apply({{nodeId(band, 0, NodeParamType::kNodeType), typeNorm(kShadowType)}});
        rig.apply({{shapeId(band, 0, 0), pick(0.125, 0.875)},
                   {shapeId(band, 0, 1), pick(0.25, 0.625)}});
        rig.apply({{nodeId(band, 0, NodeParamType::kNodeType), typeNorm(kCurrentType)}});
        rig.apply({{shapeId(band, 0, 0), pick(0.375, 0.5625)}});
    }
    return getStateBytes(*rig.p);
}

// A host's component handler: records every performEdit, so the test can hand them
// to the processor as a host would.
class RecordingHandler final : public IComponentHandler {
public:
    std::vector<std::pair<ParamID, ParamValue>> edits;

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        edits.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 /*flags*/) override { return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }   // owned by the test
    uint32 PLUGIN_API release() override { return 1; }
};

/// A connected, active Controller + Processor pair, as a host wires them, with the
/// controller's own PresetManager loading .vstpreset files from a scratch directory.
struct BrowserPair {
    HostApplication host;
    RecordingHandler handler;
    std::unique_ptr<Processor> proc = std::make_unique<Processor>();
    std::unique_ptr<Controller> ctrl = std::make_unique<Controller>();
    std::filesystem::path dir;
    int presetCount = 0;

    BrowserPair() {
        std::random_device rd;
        dir = std::filesystem::temp_directory_path() / ("disrumpo_browser_load_" + std::to_string(rd()));
        std::filesystem::create_directories(dir);

        REQUIRE(proc->initialize(&host) == kResultOk);
        REQUIRE(ctrl->initialize(&host) == kResultOk);
        REQUIRE(proc->connect(ctrlConn()) == kResultOk);
        REQUIRE(ctrl->connect(procConn()) == kResultOk);
        ctrl->setComponentHandler(&handler);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = 48000.0;
        setup.maxSamplesPerBlock = 512;
        REQUIRE(proc->setupProcessing(setup) == kResultOk);
        REQUIRE(proc->setActive(true) == kResultOk);
        // The host hands the processor's state to the controller (its instance ID)
        auto state = getStateBytes(*proc);
        auto* stream = new MemoryStream(state.data(), static_cast<TSize>(state.size()));
        REQUIRE(ctrl->setComponentState(stream) == kResultOk);
        stream->release();
    }
    ~BrowserPair() {
        proc->setActive(false);
        ctrl->setComponentHandler(nullptr);
        proc->disconnect(ctrlConn());
        ctrl->disconnect(procConn());
        ctrl->terminate();
        proc->terminate();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    BrowserPair(const BrowserPair&) = delete;
    BrowserPair& operator=(const BrowserPair&) = delete;
    BrowserPair(BrowserPair&&) = delete;
    BrowserPair& operator=(BrowserPair&&) = delete;

    IConnectionPoint* procConn() const { return static_cast<IConnectionPoint*>(static_cast<AudioEffect*>(proc.get())); }
    IConnectionPoint* ctrlConn() const {
        return static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get()));
    }

    /// Writes `component` as a .vstpreset and loads it through the preset browser's manager.
    bool browserLoad(std::vector<uint8_t> component) {
        const auto path = dir / ("preset" + std::to_string(++presetCount) + ".vstpreset");
        {
            auto* file = FileStream::open(path.string().c_str(), "wb");
            REQUIRE(file != nullptr);
            auto* comp = new MemoryStream(component.data(), static_cast<TSize>(component.size()));
            const bool saved = PresetFile::savePreset(file, makeDisrumpoPresetConfig().processorUID, comp);
            comp->release();
            file->release();
            REQUIRE(saved);
        }
        Krate::Plugins::PresetInfo info;
        info.name = "preset" + std::to_string(presetCount);
        info.path = path;
        return ctrl->getPresetManager()->loadPreset(info);
    }

    /// What a host does with the controller's performEdits: hands them to the processor
    /// in the next process() call.
    void forwardEditsAndProcess() {
        PChanges changes;
        for (const auto& [id, value] : handler.edits) changes.addChange(id, value);
        handler.edits.clear();
        process(&changes);
    }

    void process(IParameterChanges* changes) {
        float z[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float* in[2] = {z, z + 1};
        float* out[2] = {z + 2, z + 3};
        AudioBusBuffers ib{};
        ib.numChannels = 2;
        ib.channelBuffers32 = in;
        AudioBusBuffers ob{};
        ob.numChannels = 2;
        ob.channelBuffers32 = out;
        ProcessData d{};
        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = 0;
        d.numInputs = 1;
        d.numOutputs = 1;
        d.inputs = &ib;
        d.outputs = &ob;
        d.inputParameterChanges = changes;
        proc->process(d);
    }

    std::vector<uint8_t> controllerSaveBytes() const {
        auto* stream = ctrl->createComponentStateStream();
        REQUIRE(stream != nullptr);
        Steinberg::int64 size = 0;
        stream->seek(0, IBStream::kIBSeekEnd, &size);
        stream->seek(0, IBStream::kIBSeekSet, nullptr);
        std::vector<uint8_t> out(static_cast<size_t>(size));
        int32 got = 0;
        if (size > 0) stream->read(out.data(), static_cast<int32>(size), &got);
        stream->release();
        return out;
    }
};

/// Every differing byte offset (the first 24), each with the float that starts there in a and b.
std::string differences(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::string out;
    int count = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (a[i] == b[i]) continue;
        if (++count <= 24 && i + 4 <= std::min(a.size(), b.size()))
            out += " @" + std::to_string(i) + " (" + std::to_string(floatAt(a, i)) + " vs " + std::to_string(floatAt(b, i)) + ")";
    }
    return std::to_string(count) + " differing bytes:" + out;
}

/// The stream's fields in order ('b' int8, 'i' int32, 'f' float, 'l' int64), per Processor::writeState.
std::string fieldKinds(const std::vector<uint8_t>& s) {
    std::string k = "ifffi";
    for (int b = 0; b < 4; ++b) k += "ffbbb";
    k += "fff";
    k += "bfffbb" "bfbfbb" "bfff";                             // sweep core, LFO, envelope
    int32_t points = 0;
    std::memcpy(&points, s.data() + kCurveCountOffset, sizeof points);
    k += "i";
    for (int32_t p = 0; p < points; ++p) k += "ff";
    k += "fbfbbbb" "fbfbbbb" "fffb" "ffb" "bff" "bff" "ffff" "fff" "ffbb";
    for (int m = 0; m < 4; ++m) k += "fffb";
    for (int r = 0; r < 32; ++r) k += "bifb";
    for (int b = 0; b < 4; ++b) {
        k += "ffbbfbb";
        for (int n = 0; n < 4; ++n) k += "b" + std::string(6 + 10 + kDistortionTypeCount * 10, 'f');
    }
    k += "i" + std::string(4 * 4 * 3, 'f') + "il";
    return k;
}

/// Field-by-field comparison: integers exact, floats within `tol` (relative above 1). The controller
/// and the processor compute the floats in separately compiled code (fast-math on macOS), so they
/// may differ by an ulp (root CLAUDE.md); a value off by more is a different value.
std::string fieldDifferences(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, double tol) {
    if (a.size() != b.size()) return "sizes differ: " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    const std::string kinds = fieldKinds(a);
    std::string out;
    int count = 0;
    size_t off = 0;
    for (const char kind : kinds) {
        const size_t width = kind == 'b' ? 1 : (kind == 'l' ? 8 : 4);
        REQUIRE(off + width <= a.size());
        bool same = std::memcmp(a.data() + off, b.data() + off, width) == 0;
        if (!same && kind == 'f') {
            const double x = floatAt(a, off);
            const double y = floatAt(b, off);
            same = std::fabs(x - y) <= tol * std::max(1.0, std::fabs(x));
        }
        if (!same && ++count <= 24) {
            out += " @" + std::to_string(off);
            if (kind == 'f') out += " (" + std::to_string(floatAt(a, off)) + " vs " + std::to_string(floatAt(b, off)) + ")";
        }
        off += width;
    }
    if (off != a.size()) return "layout walk ended at " + std::to_string(off) + " of " + std::to_string(a.size());
    return count == 0 ? std::string() : std::to_string(count) + " differing fields:" + out;
}

}  // namespace

TEST_CASE("Browser load leaves the processor in the same state as a host load", "[disrumpo][presets][state]") {
    const auto stateA = makePreset(false);
    const auto stateB = makePreset(true);
    for (int band = 0; band < 2; ++band) {
        REQUIRE(shadowSlots(stateA, band, 0, kShadowType) != shadowSlots(stateB, band, 0, kShadowType));
        REQUIRE(stateA[nodeOffset(stateA, band, 0)] == static_cast<uint8_t>(kCurrentType));
        REQUIRE(stateB[nodeOffset(stateB, band, 0)] == static_cast<uint8_t>(kCurrentType));
    }

    BrowserPair pair;
    const auto ownId = getStateBytes(*pair.proc);
    REQUIRE(pair.browserLoad(stateA));
    pair.forwardEditsAndProcess();
    REQUIRE(pair.browserLoad(stateB));
    pair.forwardEditsAndProcess();

    Rig reference(48000.0);
    REQUIRE(setStateBytes(*reference.p, stateB));
    reference.apply({});

    // The instance-ID block names the processor, not the patch: each processor keeps its own,
    // so a browser load leaves the controller's link to its processor's display intact
    const auto got = getStateBytes(*pair.proc);
    const auto want = getStateBytes(*reference.p);
    INFO("browser-loaded state " << got.size() << " bytes, host-loaded " << want.size()
                                 << " bytes, " << differences(got, want));
    CHECK(withoutInstanceId(got) == withoutInstanceId(want));
    CHECK(std::vector<uint8_t>(got.end() - static_cast<std::ptrdiff_t>(kInstanceIdBytes), got.end()) ==
          std::vector<uint8_t>(ownId.end() - static_cast<std::ptrdiff_t>(kInstanceIdBytes), ownId.end()));
}

TEST_CASE("Node type automation after a browser load uses the preset's shape shadow", "[disrumpo][presets][state]") {
    const auto stateA = makePreset(false);
    const auto stateB = makePreset(true);

    BrowserPair pair;
    REQUIRE(pair.browserLoad(stateA));
    pair.forwardEditsAndProcess();
    REQUIRE(pair.browserLoad(stateB));
    pair.forwardEditsAndProcess();

    // Automation only: the controller is not involved
    PChanges automation;
    automation.addChange(nodeId(0, 0, NodeParamType::kNodeType), typeNorm(kShadowType));
    automation.addChange(nodeId(1, 0, NodeParamType::kNodeType), typeNorm(kShadowType));
    pair.process(&automation);

    const auto got = getStateBytes(*pair.proc);
    for (int band = 0; band < 2; ++band) {
        INFO("band " << band);
        CHECK(got[nodeOffset(got, band, 0)] == static_cast<uint8_t>(kShadowType));
        CHECK(shapeSlots(got, band, 0) == shadowSlots(stateB, band, 0, kShadowType));
    }
}

TEST_CASE("Browser save after a browser load writes the processor's state", "[disrumpo][presets][state]") {
    const auto stateA = makePreset(false);
    const auto stateB = makePreset(true);

    BrowserPair pair;
    REQUIRE(pair.browserLoad(stateA));
    pair.forwardEditsAndProcess();
    REQUIRE(pair.browserLoad(stateB));
    pair.forwardEditsAndProcess();

    // Every field the processor holds, the per-type shape shadows included, with a tolerance far
    // below any parameter's step for the floats the two sides compute separately
    const auto saved = pair.controllerSaveBytes();
    const auto processor = getStateBytes(*pair.proc);
    const auto mismatches = fieldDifferences(saved, processor, 1e-6);
    INFO("browser save " << saved.size() << " bytes, processor state " << processor.size() << " bytes, "
                         << differences(saved, processor));
    CHECK(mismatches == std::string());
}
