// ==============================================================================
// Gradus: one preset-load path (Ruinae audit fix plan T1.40: R-206, R-207, R-208)
// ==============================================================================
// R-206: the preset browser's load provider ran only in the controller; the
//   processor's setState never saw the preset, and the controller replayed values
//   setState would not hold (operating mode, a ratchet shuffle between the three
//   snap positions, out-of-range ratchet decay / strum time / lane swing, an
//   off-grid lane speed). A browser load now hands the bytes to setState.
// R-207: speed-curve depth reached the engine only through the editor's
//   SpeedCurveTable message, so after a host load with the editor closed the
//   engine ran depth 0.
// R-208: the editor's depth knobs sit in ring/UI lane order (Cond before Ratchet)
//   while the message's lane is the engine's (Ratchet before Cond).
// ==============================================================================

#include "controller/controller.h"
#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"
#include "preset/gradus_preset_config.h"
#include "processor/processor.h"
#include "ui/speed_curve_data.h"

#include <editor_lifecycle_harness.h>
#include <vst_param_changes.h>

#include "preset/preset_info.h"
#include "preset/preset_manager.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

using Catch::Approx;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

constexpr Steinberg::int32 kBlock = 512;

// Records begin/perform/endEdit, as a host's component handler sees them.
class RecordingHandler final : public Steinberg::Vst::IComponentHandler {
public:
    struct Edit {
        ParamID id;
        ParamValue value;
    };
    std::vector<Edit> performed;

    Steinberg::tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        performed.push_back({id, value});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

std::unique_ptr<Gradus::Processor> makeReadyProcessor() {
    auto proc = std::make_unique<Gradus::Processor>();
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = kBlock;
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(proc->setActive(static_cast<Steinberg::TBool>(true)) == Steinberg::kResultOk);
    return proc;
}

void shutDown(Gradus::Processor& proc) {
    proc.setActive(static_cast<Steinberg::TBool>(false));
    proc.terminate();
}

/// One block carrying `changes` (no notes, no transport).
void processBlock(Gradus::Processor& proc, Steinberg::Vst::IParameterChanges* changes) {
    std::vector<float> left(static_cast<size_t>(kBlock), 0.0f);
    std::vector<float> right(static_cast<size_t>(kBlock), 0.0f);
    float* channels[2] = {left.data(), right.data()};
    Steinberg::Vst::AudioBusBuffers out{};
    out.numChannels = 2;
    out.channelBuffers32 = channels;
    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlock;
    data.numOutputs = 1;
    data.outputs = &out;
    data.inputParameterChanges = changes;
    proc.process(data);
}

std::vector<char> stateBytes(Gradus::Processor& proc) {
    Steinberg::MemoryStream stream;
    REQUIRE(proc.getState(&stream) == Steinberg::kResultOk);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> bytes(static_cast<size_t>(size));
    Steinberg::int32 read = 0;
    stream.read(bytes.data(), static_cast<Steinberg::int32>(size), &read);
    return bytes;
}

Steinberg::IPtr<Steinberg::MemoryStream> streamOf(const std::vector<char>& bytes) {
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    stream->write(const_cast<char*>(bytes.data()), static_cast<Steinberg::int32>(bytes.size()), nullptr);
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

/// A current-version component state, written field by field as getState does.
template <typename Author>
std::vector<char> authorState(Author author) {
    Gradus::ArpeggiatorParams params;
    author(params);
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer writer(&stream, kLittleEndian);
    writer.writeInt32(Gradus::kCurrentStateVersion);
    Gradus::saveArpParams(params, writer);
    Gradus::saveSequencerNoteLaneParams(params, writer);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> bytes(static_cast<size_t>(size));
    Steinberg::int32 read = 0;
    stream.read(bytes.data(), static_cast<Steinberg::int32>(size), &read);
    return bytes;
}

Gradus::SpeedCurveData curveWith(float peakY, int presetIndex) {
    Gradus::SpeedCurveData curve;
    curve.enabled = true;
    curve.presetIndex = presetIndex;
    Gradus::SpeedCurvePoint mid;
    mid.x = 0.5f;
    mid.y = peakY;
    mid.cpLeftX = 0.4f;
    mid.cpLeftY = peakY;
    mid.cpRightX = 0.6f;
    mid.cpRightY = peakY;
    curve.points.insert(curve.points.begin() + 1, mid);
    return curve;
}

/// Preset A: in-range values, ratchet shuffle near Dotted.
std::vector<char> presetA() {
    return authorState([](Gradus::ArpeggiatorParams& p) {
        p.operatingMode.store(2);
        p.mode.store(3);
        p.ratchetSwing.store(72.0f);
        p.ratchetDecay.store(25.0f);
        p.strumTime.store(10.0f);
        p.ratchetLaneSwing.store(20.0f);
        p.conditionLaneSwing.store(12.0f);
        p.ratchetLaneSpeed.store(2.0f);
        p.conditionLaneSpeed.store(0.5f);
        p.ratchetLaneSpeedCurveDepth.store(0.3f);
        p.conditionLaneSpeedCurveDepth.store(0.35f);
        p.speedCurves[4] = curveWith(0.8f, -1);
    });
}

/// Preset B: what setState must reduce to values a parameter can hold.
std::vector<char> presetB() {
    return authorState([](Gradus::ArpeggiatorParams& p) {
        p.operatingMode.store(0);         // setState forces MIDI
        p.mode.store(5);
        // Continuous values as the preset generator writes them: the controller's
        // normalized edit must give the processor back the same float.
        p.freeRate.store(7.3f);
        p.gateLength.store(63.7f);
        p.swing.store(21.3f);
        p.gateLaneSteps[3].store(1.37f);
        p.slideTime.store(123.4f);
        p.velocityCurveAmount.store(37.7f);
        p.midiDelayGateScaleSteps[2].store(1.23f);
        p.midiDelayLaneSwing.store(17.9f);
        p.seqNoteLaneSwing.store(41.3f);
        p.ratchetSwing.store(58.0f);      // between Even and Triplet
        p.ratchetDecay.store(140.0f);     // above the 0-100 range
        p.strumTime.store(150.0f);        // above the 0-100 range
        p.ratchetLaneSwing.store(90.0f);  // above the 0-75 range
        p.conditionLaneSwing.store(33.0f);
        p.ratchetLaneSpeed.store(1.1f);   // off the speed grid
        p.conditionLaneSpeed.store(3.0f);
        p.midiDelayLaneSpeed.store(1.1f);
        p.seqNoteLaneSpeed.store(1.6f);
        const float depths[8] = {0.1f, 0.2f, 0.3f, 0.4f, 0.55f, 0.65f, 0.75f, 0.85f};
        std::atomic<float>* depthTargets[8] = {
            &p.velocityLaneSpeedCurveDepth, &p.gateLaneSpeedCurveDepth,
            &p.pitchLaneSpeedCurveDepth,    &p.modifierLaneSpeedCurveDepth,
            &p.ratchetLaneSpeedCurveDepth,  &p.conditionLaneSpeedCurveDepth,
            &p.chordLaneSpeedCurveDepth,    &p.inversionLaneSpeedCurveDepth};
        for (size_t i = 0; i < 8; ++i) depthTargets[i]->store(depths[i]);
        p.speedCurves[5] = curveWith(0.2f, -1);
        p.speedCurves[1] = curveWith(0.65f, 3);
    });
}

/// A connected controller + processor pair whose preset manager reads temp directories.
struct BrowserPair {
    RecordingHandler handler;
    Steinberg::Vst::HostApplication hostApp;
    std::unique_ptr<Gradus::Processor> proc;
    Steinberg::IPtr<Gradus::Controller> ctrl;
    std::filesystem::path dir;
    int presetCount = 0;

    BrowserPair() {
        std::random_device rd;
        dir = std::filesystem::temp_directory_path() /
              ("gradus_browser_load_" + std::to_string(rd()));
        std::filesystem::create_directories(dir / "user");
        std::filesystem::create_directories(dir / "factory");

        proc = makeReadyProcessor();
        ctrl = Steinberg::owned(new Gradus::Controller());
        REQUIRE(ctrl->initialize(&hostApp) == Steinberg::kResultOk);
        REQUIRE(ctrl->setComponentHandler(&handler) == Steinberg::kResultOk);
        ctrl->replacePresetManagerForTest(dir / "user", dir / "factory");
        REQUIRE(ctrl->connect(connection()) == Steinberg::kResultOk);
        REQUIRE(proc->connect(ctrl) == Steinberg::kResultOk);
    }
    ~BrowserPair() {
        proc->disconnect(ctrl);
        ctrl->disconnect(connection());
        ctrl->setComponentHandler(nullptr);
        ctrl->terminate();
        shutDown(*proc);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    BrowserPair(const BrowserPair&) = delete;
    BrowserPair& operator=(const BrowserPair&) = delete;
    BrowserPair(BrowserPair&&) = delete;
    BrowserPair& operator=(BrowserPair&&) = delete;

    [[nodiscard]] Steinberg::Vst::IConnectionPoint* connection() const {
        return static_cast<Steinberg::Vst::IConnectionPoint*>(
            static_cast<Steinberg::Vst::AudioEffect*>(proc.get()));
    }

    /// Writes `component` as a .vstpreset and loads it through the browser's manager.
    [[nodiscard]] bool browserLoad(const std::vector<char>& component) {
        const std::filesystem::path path =
            dir / "user" / ("preset" + std::to_string(++presetCount) + ".vstpreset");
        {
            auto file = Steinberg::owned(
                Steinberg::Vst::FileStream::open(path.string().c_str(), "wb"));
            REQUIRE(file != nullptr);
            auto comp = streamOf(component);
            REQUIRE(Steinberg::Vst::PresetFile::savePreset(
                file, Gradus::makeGradusPresetConfig().processorUID, comp));
        }
        Krate::Plugins::PresetInfo info;
        info.name = "preset" + std::to_string(presetCount);
        info.path = path;
        return ctrl->getPresetManager()->loadPreset(info);
    }

    /// One block carrying every edit the controller told the host about, as a host relays them.
    void block() {
        Krate::Test::ParameterChanges edits;
        for (const auto& e : handler.performed) edits.addChange(e.id, e.value);
        handler.performed.clear();
        processBlock(*proc, &edits);
    }
};

}  // namespace

// R-206: one parse, one apply. The processor's state after a browser load (and the
// edits the host relays) is byte for byte the state a host setState leaves.
TEST_CASE("Browser load leaves the processor in the same state as a host load",
          "[gradus][presets][state]") {
    const auto stateA = presetA();
    const auto stateB = presetB();
    REQUIRE(stateA != stateB);

    BrowserPair pair;
    REQUIRE(pair.browserLoad(stateA));
    pair.block();
    REQUIRE(pair.browserLoad(stateB));
    REQUIRE_FALSE(pair.handler.performed.empty());
    pair.block();

    auto hostLoaded = makeReadyProcessor();
    REQUIRE(hostLoaded->setState(streamOf(stateB)) == Steinberg::kResultOk);
    processBlock(*hostLoaded, nullptr);

    const auto browserBytes = stateBytes(*pair.proc);
    const auto hostBytes = stateBytes(*hostLoaded);
    REQUIRE(browserBytes.size() == hostBytes.size());
    size_t differing = 0;
    size_t firstDiff = browserBytes.size();
    for (size_t i = 0; i < browserBytes.size(); ++i) {
        if (browserBytes[i] != hostBytes[i]) {
            ++differing;
            firstDiff = std::min(firstDiff, i);
        }
    }
    INFO(differing << " differing bytes, the first at offset " << firstDiff);
    if (firstDiff + 4 <= browserBytes.size()) {
        // The 4-byte field holding the first difference, read as int32 and float.
        const size_t field = firstDiff & ~size_t{3};
        Steinberg::int32 bi = 0;
        Steinberg::int32 hi = 0;
        float bf = 0.0f;
        float hf = 0.0f;
        std::memcpy(&bi, browserBytes.data() + field, 4);
        std::memcpy(&hi, hostBytes.data() + field, 4);
        std::memcpy(&bf, browserBytes.data() + field, 4);
        std::memcpy(&hf, hostBytes.data() + field, 4);
        INFO("field at " << field << ": browser " << bi << " / " << bf << ", host " << hi << " / " << hf);
        CHECK(browserBytes == hostBytes);
    } else {
        CHECK(browserBytes == hostBytes);
    }

    // The engine runs the loaded speed-curve depths (R-207 through the browser route).
    CHECK(pair.proc->arpCoreForTest().laneSpeedCurveDepth(4) == Approx(0.55f).margin(1e-6));
    CHECK(pair.proc->arpCoreForTest().laneSpeedCurveDepth(5) == Approx(0.65f).margin(1e-6));

    shutDown(*hostLoaded);
}

// R-207: a processor with no controller and no editor runs the depths it loaded.
TEST_CASE("Host load applies speed-curve depth without the editor", "[gradus][state]") {
    const auto state = authorState([](Gradus::ArpeggiatorParams& p) {
        std::atomic<float>* depths[8] = {
            &p.velocityLaneSpeedCurveDepth, &p.gateLaneSpeedCurveDepth,
            &p.pitchLaneSpeedCurveDepth,    &p.modifierLaneSpeedCurveDepth,
            &p.ratchetLaneSpeedCurveDepth,  &p.conditionLaneSpeedCurveDepth,
            &p.chordLaneSpeedCurveDepth,    &p.inversionLaneSpeedCurveDepth};
        for (auto* d : depths) d->store(0.7f);
    });

    auto proc = makeReadyProcessor();
    REQUIRE(proc->setState(streamOf(state)) == Steinberg::kResultOk);
    processBlock(*proc, nullptr);
    for (size_t lane = 0; lane < 8; ++lane) {
        INFO("engine lane " << lane);
        CHECK(proc->arpCoreForTest().laneSpeedCurveDepth(lane) == Approx(0.7f).margin(1e-6));
    }
    shutDown(*proc);
}

// R-208: the SpeedCurveTable message for an engine lane carries that lane's depth,
// even though the editor holds its depth knobs in ring order (Cond before Ratchet).
TEST_CASE("Speed-curve message carries each lane's own depth", "[gradus][controller]") {
    BrowserPair pair;

    // Engine / parameter order: Vel, Gate, Pitch, Mod, Ratchet, Cond, Chord, Inv.
    constexpr std::array<double, 8> kDepths = {0.11, 0.22, 0.33, 0.44, 0.2, 0.9, 0.66, 0.77};
    for (size_t lane = 0; lane < 8; ++lane) {
        pair.ctrl->setParamNormalized(
            static_cast<ParamID>(Gradus::kArpVelocityLaneSpeedCurveDepthId + lane), kDepths[lane]);
    }

    // The editor is open, so its depth knobs exist and show the parameters.
    Krate::TestSupport::ensureVstguiInitialized();
    auto* editor = new VSTGUI::VST3Editor(
        pair.ctrl.get(), "editor", (std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc").c_str());
    Steinberg::IPlugView* view = editor;
    REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);

    const Gradus::SpeedCurveData curve = curveWith(0.7f, -1);
    constexpr size_t kRatchet = 4;
    constexpr size_t kCondition = 5;
    pair.ctrl->sendSpeedCurveTableForTest(kRatchet, curve);
    pair.ctrl->sendSpeedCurveTableForTest(kCondition, curve);
    CHECK(pair.proc->arpCoreForTest().laneSpeedCurveDepth(kRatchet) ==
          Approx(kDepths[kRatchet]).margin(1e-6));
    CHECK(pair.proc->arpCoreForTest().laneSpeedCurveDepth(kCondition) ==
          Approx(kDepths[kCondition]).margin(1e-6));

    view->removed();
    view->release();
}
