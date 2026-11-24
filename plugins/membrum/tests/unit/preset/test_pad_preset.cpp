// ==============================================================================
// Per-pad preset save/load tests (Phase 6 / T039)
// ==============================================================================
// Verifies:
//   - Pad preset StateProvider produces 460-byte blob (Phase 10)
//     (version int32 + exciterType int32 + bodyModel int32 + 56 float64)
//   - Pad preset LoadProvider applies to selected pad only, other 31 unchanged
//   - Choke group and output bus are NOT modified by pad preset load
//   - Pad preset loaded onto pad 15 matches original pad 1 sound params
//   - Truncated/corrupted pad preset blob fails gracefully
//   - Pad preset subcategory directory structure matches drum types
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "state/state_codec.h"
#include "dsp/default_kit.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "preset/membrum_preset_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"

#include "vst_param_changes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int64 kPadPresetBytes = 484;  // version(4) + exciter(4) + body(4) + 59*float64(472) (wire-coupling)

/// Number of sound params serialized as float64.
constexpr int kPadPresetSoundParamCount = 59;  // wire-coupling: added wireCoupling at index 58

/// Build a pad preset blob manually for testing load.
/// Writes version=kPadBlobVersion, exciterType, bodyModel, and 59 float64 sound values.
MemoryStream* buildPadPresetBlob(int32 exciterType, int32 bodyModel,
                                  double soundParams[kPadPresetSoundParamCount])
{
    auto* stream = new MemoryStream();
    int32 version = 5;  // wire-coupling codec version
    stream->write(&version, sizeof(version), nullptr);
    stream->write(&exciterType, sizeof(exciterType), nullptr);
    stream->write(&bodyModel, sizeof(bodyModel), nullptr);
    for (int i = 0; i < kPadPresetSoundParamCount; ++i)
        stream->write(&soundParams[i], sizeof(soundParams[i]), nullptr);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

} // namespace

// ==============================================================================
// T039: Pad preset StateProvider produces exactly 460 bytes (56 sound slots, Phase 10).
// ==============================================================================

TEST_CASE("Pad preset: StateProvider produces exactly 460 bytes",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Set selected pad to pad 5 so we know which pad is being serialized
    controller.setParamNormalized(Membrum::kSelectedPadId, 5.0 / 31.0);

    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    int64 pos = 0;
    stream->tell(&pos);

    // Stream should be seeked to beginning (ready to read) -- check size by seeking to end
    stream->seek(0, IBStream::kIBSeekEnd, &pos);
    CHECK(pos == kPadPresetBytes);

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Pad preset: blob format is version + exciterType + bodyModel + 56 float64",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Configure pad 0 with known values
    const auto padExcId = static_cast<ParamID>(
        Membrum::padParamId(0, Membrum::kPadExciterType));
    const auto padBodyId = static_cast<ParamID>(
        Membrum::padParamId(0, Membrum::kPadBodyModel));
    const auto padMatId = static_cast<ParamID>(
        Membrum::padParamId(0, Membrum::kPadMaterial));

    // Set exciter to NoiseBurst (2), body to Plate (1), material to 0.75
    controller.setParamNormalized(padExcId,
        (2.0 + 0.5) / static_cast<double>(Membrum::ExciterType::kCount));
    controller.setParamNormalized(padBodyId,
        (1.0 + 0.5) / static_cast<double>(Membrum::BodyModelType::kCount));
    controller.setParamNormalized(padMatId, 0.75);

    // Select pad 0
    controller.setParamNormalized(Membrum::kSelectedPadId, 0.0);

    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Read back the blob
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    int32 version = 0;
    stream->read(&version, sizeof(version), nullptr);
    CHECK(version == 5);  // wire-coupling codec version

    int32 exciterTypeI32 = -1;
    stream->read(&exciterTypeI32, sizeof(exciterTypeI32), nullptr);
    CHECK(exciterTypeI32 == 2);  // NoiseBurst

    int32 bodyModelI32 = -1;
    stream->read(&bodyModelI32, sizeof(bodyModelI32), nullptr);
    CHECK(bodyModelI32 == 1);  // Plate

    // First sound param (offset 2 = material) should be 0.75
    double materialVal = 0.0;
    stream->read(&materialVal, sizeof(materialVal), nullptr);
    CHECK(materialVal == Approx(0.75).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: LoadProvider applies to selected pad only
// ==============================================================================

TEST_CASE("Pad preset: LoadProvider applies to selected pad only, others unchanged",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Set pad 5 material to a distinctive value
    const auto pad5MatId = static_cast<ParamID>(
        Membrum::padParamId(5, Membrum::kPadMaterial));
    controller.setParamNormalized(pad5MatId, 0.123);

    // Record pad 3's material before load
    const auto pad3MatId = static_cast<ParamID>(
        Membrum::padParamId(3, Membrum::kPadMaterial));
    const double pad3Before = controller.getParamNormalized(pad3MatId);

    // Build a pad preset with material = 0.99
    double soundParams[kPadPresetSoundParamCount] = {};
    soundParams[0] = 0.99;  // material (offset 2 -> index 0 in sound array)
    for (int i = 1; i < kPadPresetSoundParamCount; ++i)
        soundParams[i] = 0.5;  // fill rest with 0.5

    auto* stream = buildPadPresetBlob(0, 0, soundParams);

    // Select pad 5 and load
    controller.setParamNormalized(Membrum::kSelectedPadId, 5.0 / 31.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // Pad 5 material should now be 0.99
    CHECK(controller.getParamNormalized(pad5MatId) == Approx(0.99).margin(1e-6));

    // Pad 3 material should be unchanged
    CHECK(controller.getParamNormalized(pad3MatId) == Approx(pad3Before).margin(1e-9));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Choke group and output bus NOT modified by pad preset load
// ==============================================================================

TEST_CASE("Pad preset: choke group and output bus are NOT modified by load",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Set pad 2 choke group to 5 and output bus to 3
    const auto pad2ChokeId = static_cast<ParamID>(
        Membrum::padParamId(2, Membrum::kPadChokeGroup));
    const auto pad2BusId = static_cast<ParamID>(
        Membrum::padParamId(2, Membrum::kPadOutputBus));
    controller.setParamNormalized(pad2ChokeId, 5.0 / 8.0);
    controller.setParamNormalized(pad2BusId, 3.0 / 15.0);

    const double chokeBefore = controller.getParamNormalized(pad2ChokeId);
    const double busBefore = controller.getParamNormalized(pad2BusId);

    // Build a pad preset blob (choke/bus positions contain arbitrary values)
    double soundParams[kPadPresetSoundParamCount] = {};
    for (int i = 0; i < kPadPresetSoundParamCount; ++i)
        soundParams[i] = 0.5;
    // Positions 28 and 29 in the sound array correspond to offsets 30-31 (choke/bus)
    soundParams[28] = 7.0;   // choke group as float64 in kit format
    soundParams[29] = 12.0;  // output bus as float64 in kit format

    auto* stream = buildPadPresetBlob(0, 0, soundParams);

    // Select pad 2 and load
    controller.setParamNormalized(Membrum::kSelectedPadId, 2.0 / 31.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // Choke group and output bus should be UNCHANGED
    CHECK(controller.getParamNormalized(pad2ChokeId) == Approx(chokeBefore).margin(1e-9));
    CHECK(controller.getParamNormalized(pad2BusId) == Approx(busBefore).margin(1e-9));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Pad preset loaded onto pad 15 matches original pad 1 sound params
// ==============================================================================

TEST_CASE("Pad preset: round-trip from pad 1 to pad 15 matches all sound params",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Configure pad 1 with distinctive values
    const int srcPad = 1;
    const auto excId = static_cast<ParamID>(
        Membrum::padParamId(srcPad, Membrum::kPadExciterType));
    const auto bodyId = static_cast<ParamID>(
        Membrum::padParamId(srcPad, Membrum::kPadBodyModel));

    // Set exciter=FMSine(3), body=Tube(3)
    controller.setParamNormalized(excId,
        (3.0 + 0.5) / static_cast<double>(Membrum::ExciterType::kCount));
    controller.setParamNormalized(bodyId,
        (3.0 + 0.5) / static_cast<double>(Membrum::BodyModelType::kCount));

    // Set various sound params to distinctive values
    const int offsets[] = {
        Membrum::kPadMaterial, Membrum::kPadSize, Membrum::kPadDecay,
        Membrum::kPadStrikePosition, Membrum::kPadLevel,
        Membrum::kPadTSFilterCutoff, Membrum::kPadTSFilterResonance,
        Membrum::kPadModeStretch, Membrum::kPadMorphEnabled,
        Membrum::kPadFMRatio, Membrum::kPadFeedbackAmount,
    };
    const double values[] = {
        0.11, 0.22, 0.33, 0.44, 0.55, 0.66, 0.77, 0.88, 1.0, 0.91, 0.82,
    };
    for (int i = 0; i < 11; ++i)
    {
        const auto pid = static_cast<ParamID>(
            Membrum::padParamId(srcPad, offsets[i]));
        controller.setParamNormalized(pid, values[i]);
    }

    // Save pad preset from pad 1
    controller.setParamNormalized(Membrum::kSelectedPadId, 1.0 / 31.0);
    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Load onto pad 15
    const int dstPad = 15;
    controller.setParamNormalized(Membrum::kSelectedPadId, 15.0 / 31.0);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // Verify pad 15 matches pad 1 for all distinctive values
    for (int i = 0; i < 11; ++i)
    {
        const auto srcId = static_cast<ParamID>(
            Membrum::padParamId(srcPad, offsets[i]));
        const auto dstId = static_cast<ParamID>(
            Membrum::padParamId(dstPad, offsets[i]));
        INFO("Offset " << offsets[i]);
        CHECK(controller.getParamNormalized(dstId) ==
              Approx(controller.getParamNormalized(srcId)).margin(1e-6));
    }

    // Also verify exciter type and body model match
    const auto dstExcId = static_cast<ParamID>(
        Membrum::padParamId(dstPad, Membrum::kPadExciterType));
    const auto dstBodyId = static_cast<ParamID>(
        Membrum::padParamId(dstPad, Membrum::kPadBodyModel));
    CHECK(controller.getParamNormalized(dstExcId) ==
          Approx(controller.getParamNormalized(excId)).margin(1e-6));
    CHECK(controller.getParamNormalized(dstBodyId) ==
          Approx(controller.getParamNormalized(bodyId)).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Truncated/corrupted pad preset blob fails gracefully
// ==============================================================================

TEST_CASE("Pad preset: truncated blob fails gracefully",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Build a truncated blob: only version + exciterType (8 bytes, missing bodyModel + sound params)
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);
    int32 exciterType = 0;
    stream->write(&exciterType, sizeof(exciterType), nullptr);
    // Missing: bodyModel and all 51 float64 values
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    controller.setParamNormalized(Membrum::kSelectedPadId, 0.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK_FALSE(ok);

    // Pad 0 should be unchanged (or at least not corrupted)
    // Note: partial reads may have modified some params, but the function should
    // ideally not apply partial state. We at least verify it didn't crash.

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Pad preset: wrong version fails gracefully",
          "[membrum][preset][pad_preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Build a blob with version=99
    double soundParams[kPadPresetSoundParamCount] = {};
    for (int i = 0; i < kPadPresetSoundParamCount; ++i)
        soundParams[i] = 0.5;

    auto* stream = new MemoryStream();
    int32 version = 99;
    stream->write(&version, sizeof(version), nullptr);
    int32 et = 0;
    stream->write(&et, sizeof(et), nullptr);
    int32 bm = 0;
    stream->write(&bm, sizeof(bm), nullptr);
    for (int i = 0; i < kPadPresetSoundParamCount; ++i)
        stream->write(&soundParams[i], sizeof(soundParams[i]), nullptr);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    controller.setParamNormalized(Membrum::kSelectedPadId, 0.0);
    bool ok = controller.padPresetLoadProvider(stream);
    CHECK_FALSE(ok);

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// T039: Pad preset subcategory directory structure
// ==============================================================================

TEST_CASE("Pad preset: subcategories match drum types",
          "[membrum][preset][pad_preset]")
{
    // Spec 141 Phase 6 (T052): pad subcategories match the controller's
    // preset browser tab list (`Factory`/`User` tabs come from the browser
    // view itself): Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, FX.
    auto cfg = Membrum::padPresetConfig();
    REQUIRE(cfg.subcategoryNames.size() == 8);
    CHECK(cfg.subcategoryNames[0] == "Kick");
    CHECK(cfg.subcategoryNames[1] == "Snare");
    CHECK(cfg.subcategoryNames[2] == "Tom");
    CHECK(cfg.subcategoryNames[3] == "Hat");
    CHECK(cfg.subcategoryNames[4] == "Cymbal");
    CHECK(cfg.subcategoryNames[5] == "Perc");
    CHECK(cfg.subcategoryNames[6] == "Tonal");
    CHECK(cfg.subcategoryNames[7] == "FX");
}

// ==============================================================================
// One apply for pad presets (R-203, Ruinae audit fix plan T1.37)
// ==============================================================================
// A pad-preset browser load used to reach the processor only as performEdits
// from a controller-side copy of State::applyPadPresetSnapshot. That copy
// overwrote the target pad's enabled flag and pan (FR-061 keeps both) and
// never delivered noiseLayerGain (it has no ParamID). These cases pin the
// browser route to State::applyPadPresetSnapshot.
// ==============================================================================

namespace {

constexpr int32  kPpBlockSize  = 256;
constexpr double kPpSampleRate = 48000.0;

// A host's component handler: records every performEdit the controller sends,
// so the test can forward them to the processor as a host would, and counts
// the edit gestures, restartComponent flag words and setDirty calls.
class PpRecordingHandler final : public IComponentHandler, public IComponentHandler2
{
public:
    std::vector<std::pair<ParamID, ParamValue>> edits;
    int beginEdits = 0;
    int endEdits = 0;
    std::vector<int32> restartFlags;
    std::vector<bool> dirtyCalls;

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override
    {
        ++beginEdits;
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        edits.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override
    {
        ++endEdits;
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        restartFlags.push_back(flags);
        return kResultOk;
    }

    // IComponentHandler2
    tresult PLUGIN_API setDirty(TBool state) override
    {
        dirtyCalls.push_back(state != 0);
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString /*name*/) override { return kResultOk; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }

    void resetCounts()
    {
        edits.clear();
        beginEdits = 0;
        endEdits = 0;
        restartFlags.clear();
        dirtyCalls.clear();
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler2::iid))
        {
            *obj = static_cast<IComponentHandler2*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    // Owned by the test: reference counting is a no-op.
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

std::unique_ptr<Membrum::Processor> makePpProcessor(FUnknown* context)
{
    auto proc = std::make_unique<Membrum::Processor>();
    REQUIRE(proc->initialize(context) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kPpBlockSize;
    setup.sampleRate = kPpSampleRate;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);
    return proc;
}

void processPpBlock(Membrum::Processor& proc, IParameterChanges* changes)
{
    std::array<float, kPpBlockSize> outL{};
    std::array<float, kPpBlockSize> outR{};
    std::array<float*, 2> chans{outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans.data();

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kPpBlockSize;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = changes;
    proc.process(data);
}

std::vector<char> ppStreamBytes(MemoryStream& stream)
{
    const auto size = static_cast<std::size_t>(stream.getSize());
    const char* data = stream.getData();
    return std::vector<char>(data, data + size);
}

std::vector<char> ppProcessorStateBytes(Membrum::Processor& proc)
{
    MemoryStream stream;
    REQUIRE(proc.getState(&stream) == kResultOk);
    return ppStreamBytes(stream);
}

Membrum::State::KitSnapshot ppProcessorKit(Membrum::Processor& proc)
{
    const auto bytes = ppProcessorStateBytes(proc);
    MemoryStream stream(const_cast<char*>(bytes.data()),
                        static_cast<TSize>(bytes.size()));
    Membrum::State::KitSnapshot kit;
    REQUIRE(Membrum::State::readKitBlob(&stream, kit) == kResultOk);
    return kit;
}

std::vector<char> ppKitBytes(const Membrum::State::KitSnapshot& kit)
{
    MemoryStream stream;
    REQUIRE(Membrum::State::writeKitBlob(&stream, kit) == kResultOk);
    return ppStreamBytes(stream);
}

std::vector<char> ppPadPresetBytes(const Membrum::State::PadPresetSnapshot& preset)
{
    MemoryStream stream;
    REQUIRE(Membrum::State::writePadPresetBlob(&stream, preset) == kResultOk);
    return ppStreamBytes(stream);
}

constexpr int kPpTargetPad = 3;

// The kit both sides start from: the default kit, with the target pad selected,
// disabled, panned hard left, an explicit body damping (not the -1 sentinel),
// noiseLayerGain 1.0 and non-neutral macros.
Membrum::State::KitSnapshot ppStartKit()
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);
    Membrum::State::KitSnapshot kit;
    for (int i = 0; i < Membrum::kNumPads; ++i)
        kit.pads[static_cast<std::size_t>(i)] =
            Membrum::State::toPadSnapshot(pads[static_cast<std::size_t>(i)]);
    kit.selectedPadIndex = kPpTargetPad;

    auto& target = kit.pads[kPpTargetPad];
    target.sound[51] = 0.0;   // enabled: off
    target.sound[56] = 0.0;   // pan: hard left
    target.sound[42] = 0.4;   // bodyDampingB1: explicit
    target.sound[43] = 0.3;   // bodyDampingB3: explicit
    target.sound[57] = 1.0;   // noiseLayerGain
    target.macros = {0.3, 0.7, 0.6, 0.4, 0.55};
    return kit;
}

// A pad preset from another pad's sound, storing enabled = 1, pan centre,
// the body-damping sentinel and a noiseLayerGain of 3.0.
Membrum::State::PadPresetSnapshot ppPreset()
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);
    auto preset = Membrum::State::toPadPresetSnapshot(pads[1]);
    preset.sound[0]  = 0.25;  // material
    preset.sound[51] = 1.0;   // enabled
    preset.sound[56] = 0.5;   // pan: centre
    preset.sound[42] = -1.0;  // bodyDampingB1: sentinel
    preset.sound[43] = -1.0;  // bodyDampingB3: sentinel
    preset.sound[57] = 3.0;   // noiseLayerGain
    return preset;
}

// A connected Controller + Processor pair, as a host wires them, both loaded
// with ppStartKit() through the host path.
struct PadPresetPair
{
    HostApplication host;
    PpRecordingHandler handler;
    std::unique_ptr<Membrum::Processor> proc;
    Membrum::Controller controller;

    PadPresetPair()
    {
        proc = makePpProcessor(&host);
        REQUIRE(controller.initialize(&host) == kResultOk);
        controller.setComponentHandler(&handler);
        controller.connect(static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get())));
        proc->connect(static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&controller)));

        const auto kit = ppKitBytes(ppStartKit());
        {
            MemoryStream stream(const_cast<char*>(kit.data()),
                                static_cast<TSize>(kit.size()));
            REQUIRE(proc->setState(&stream) == kResultOk);
        }
        {
            MemoryStream stream(const_cast<char*>(kit.data()),
                                static_cast<TSize>(kit.size()));
            REQUIRE(controller.setComponentState(&stream) == kResultOk);
        }
        controller.setParamNormalized(
            Membrum::kSelectedPadId, static_cast<double>(kPpTargetPad) / 31.0);
        forwardEditsAndProcess();
    }

    ~PadPresetPair()
    {
        proc->disconnect(static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&controller)));
        controller.disconnect(static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get())));
        controller.setComponentHandler(nullptr);
        controller.terminate();
        proc->setActive(false);
        proc->terminate();
    }

    PadPresetPair(const PadPresetPair&) = delete;
    PadPresetPair& operator=(const PadPresetPair&) = delete;

    bool browserLoad(const std::vector<char>& bytes)
    {
        MemoryStream stream;
        stream.write(const_cast<char*>(bytes.data()),
                     static_cast<int32>(bytes.size()), nullptr);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return controller.padPresetLoadProvider(&stream);
    }

    // What a host does with the controller's performEdits: hand them to the
    // processor in the next process() call.
    void forwardEditsAndProcess()
    {
        Krate::Test::ParameterChanges changes;
        for (const auto& [id, value] : handler.edits)
            changes.addChange(id, value);
        handler.edits.clear();
        processPpBlock(*proc, &changes);
    }
};

// The state a browser pad load must reach: the start kit with the target pad
// set by the processor-side helper, loaded through setState, one block.
std::vector<char> ppReferenceStateBytes(const Membrum::State::PadPresetSnapshot& preset)
{
    auto refKit = ppStartKit();
    {
        Membrum::PadConfig cfg;
        Membrum::State::applyPadSnapshot(refKit.pads[kPpTargetPad], cfg);
        Membrum::State::applyPadPresetSnapshot(preset, cfg);
        refKit.pads[kPpTargetPad] = Membrum::State::toPadSnapshot(cfg);
    }
    auto reference = makePpProcessor(nullptr);
    {
        const auto bytes = ppKitBytes(refKit);
        MemoryStream stream(const_cast<char*>(bytes.data()),
                            static_cast<TSize>(bytes.size()));
        REQUIRE(reference->setState(&stream) == kResultOk);
    }
    processPpBlock(*reference, nullptr);
    auto state = ppProcessorStateBytes(*reference);
    reference->setActive(false);
    reference->terminate();
    return state;
}

std::size_t ppFirstDifference(const std::vector<char>& a, const std::vector<char>& b)
{
    std::size_t i = 0;
    while (i < std::min(a.size(), b.size()) && a[i] == b[i])
        ++i;
    return i;
}

} // namespace

TEST_CASE("Pad preset load keeps the target pad's enabled and pan",
          "[membrum][presets][pad]")
{
    PadPresetPair pair;
    const auto enabledId = static_cast<ParamID>(
        Membrum::padParamId(kPpTargetPad, Membrum::kPadEnabled));
    const auto panId = static_cast<ParamID>(
        Membrum::padParamId(kPpTargetPad, Membrum::kPadPan));
    REQUIRE(pair.controller.getParamNormalized(enabledId) == 0.0);
    REQUIRE(pair.controller.getParamNormalized(panId) == 0.0);

    REQUIRE(pair.browserLoad(ppPadPresetBytes(ppPreset())));
    pair.forwardEditsAndProcess();

    // The load did land (the sound changed) ...
    const auto materialId = static_cast<ParamID>(
        Membrum::padParamId(kPpTargetPad, Membrum::kPadMaterial));
    CHECK(pair.controller.getParamNormalized(materialId) == Approx(0.25).margin(1e-6));

    // ... but FR-061 keeps the pad's enable state and pan, in the controller
    CHECK(pair.controller.getParamNormalized(enabledId) == 0.0);
    CHECK(pair.controller.getParamNormalized(panId) == 0.0);

    // ... and in the processor.
    const auto kit = ppProcessorKit(*pair.proc);
    CHECK(kit.pads[kPpTargetPad].sound[51] == 0.0);
    CHECK(kit.pads[kPpTargetPad].sound[56] == 0.0);
}

TEST_CASE("Pad preset load applies noiseLayerGain", "[membrum][presets][pad]")
{
    PadPresetPair pair;
    REQUIRE(ppProcessorKit(*pair.proc).pads[kPpTargetPad].sound[57] == 1.0);

    REQUIRE(pair.browserLoad(ppPadPresetBytes(ppPreset())));
    pair.forwardEditsAndProcess();

    const auto kit = ppProcessorKit(*pair.proc);
    CHECK(kit.pads[kPpTargetPad].sound[57] == Approx(3.0).margin(1e-6));
}

TEST_CASE("Browser pad load leaves the processor in the same state as applyPadPresetSnapshot",
          "[membrum][presets][pad][state]")
{
    const auto preset = ppPreset();

    PadPresetPair pair;
    REQUIRE(pair.browserLoad(ppPadPresetBytes(preset)));
    pair.forwardEditsAndProcess();

    const auto got = ppProcessorStateBytes(*pair.proc);
    const auto want = ppReferenceStateBytes(preset);
    INFO("browser-loaded state " << got.size() << " bytes, reference "
         << want.size() << " bytes, first difference at byte "
         << ppFirstDifference(got, want));
    CHECK(got == want);
}

TEST_CASE("Pad browser load sends no parameter edits", "[presets][pad][state]")
{
    const auto preset = ppPreset();

    PadPresetPair pair;
    pair.handler.resetCounts();
    REQUIRE(pair.browserLoad(ppPadPresetBytes(preset)));

    INFO("performEdit " << pair.handler.edits.size() << ", beginEdit "
         << pair.handler.beginEdits << ", endEdit " << pair.handler.endEdits);
    CHECK(pair.handler.edits.empty());
    CHECK(pair.handler.beginEdits == 0);
    CHECK(pair.handler.endEdits == 0);

    const auto& flags = pair.handler.restartFlags;
    const auto valuesRestarts = std::count_if(flags.begin(), flags.end(),
        [](int32 f) { return (f & kParamValuesChanged) != 0; });
    CHECK(valuesRestarts == 1);
    const auto& dirty = pair.handler.dirtyCalls;
    CHECK(std::find(dirty.begin(), dirty.end(), true) != dirty.end());

    // The UI still mirrors the load.
    const auto materialId = static_cast<ParamID>(
        Membrum::padParamId(kPpTargetPad, Membrum::kPadMaterial));
    CHECK(pair.controller.getParamNormalized(materialId) == Approx(0.25).margin(1e-6));

    pair.forwardEditsAndProcess();
    const auto got = ppProcessorStateBytes(*pair.proc);
    const auto want = ppReferenceStateBytes(preset);
    INFO("first state difference at byte " << ppFirstDifference(got, want));
    CHECK(got == want);
}

// ==============================================================================
// Pad preset SAVE from the controller (MB-087, MB-162, MB-030, MB-043, MB-166,
// MB-139)
// ==============================================================================
// padPresetStateProvider builds the blob from the controller. noiseLayerGain
// (sound[57]) and the body-damping -1 sentinel (sound[42/43]) come from the
// controller's mirror of the loaded state, so a saved pad reloads unchanged.
// ==============================================================================

TEST_CASE("Pad preset saved from the controller reloads to the processor's pad config",
          "[presets][pad][save]")
{
    int pad = -1;
    SECTION("pad 2 (note 38, noiseLayerGain 6.2)") { pad = 2; }
    SECTION("pad 5 (note 41, body-damping B1 sentinel)") { pad = 5; }
    REQUIRE(pad >= 0);

    auto p1 = makePpProcessor(nullptr);
    processPpBlock(*p1, nullptr);
    const auto bytes0 = ppProcessorStateBytes(*p1);

    std::vector<char> saved;
    {
        HostApplication host;
        Membrum::Controller controller;
        REQUIRE(controller.initialize(&host) == kResultOk);
        {
            MemoryStream stream(const_cast<char*>(bytes0.data()),
                                static_cast<TSize>(bytes0.size()));
            REQUIRE(controller.setComponentState(&stream) == kResultOk);
        }
        controller.setParamNormalized(Membrum::kSelectedPadId,
                                      static_cast<double>(pad) / 31.0);
        IBStream* s = controller.padPresetStateProvider();
        REQUIRE(s != nullptr);
        std::array<char, 4096> chunk{};
        int32 bytesRead = 0;
        while (s->read(chunk.data(), static_cast<int32>(chunk.size()), &bytesRead) ==
                   kResultOk &&
               bytesRead > 0)
        {
            saved.insert(saved.end(), chunk.data(), chunk.data() + bytesRead);
        }
        s->release();
        controller.terminate();
    }
    REQUIRE_FALSE(saved.empty());

    auto p2 = makePpProcessor(nullptr);
    {
        auto msg = Steinberg::owned(new HostMessage());
        msg->setMessageID("LoadPadPreset");
        auto* attrs = msg->getAttributes();
        REQUIRE(attrs != nullptr);
        attrs->setBinary("bytes", saved.data(),
                         static_cast<Steinberg::uint32>(saved.size()));
        attrs->setInt("pad", static_cast<Steinberg::int64>(pad));
        REQUIRE(p2->notify(msg) == kResultOk);
    }
    processPpBlock(*p2, nullptr);
    const auto bytes1 = ppProcessorStateBytes(*p2);

    const auto& want = p1->voicePoolForTest().padConfig(pad);
    const auto& got = p2->voicePoolForTest().padConfig(pad);
    INFO("pad " << pad << ": noiseLayerGain " << want.noiseLayerGain << " -> "
         << got.noiseLayerGain << ", bodyDampingB1 " << want.bodyDampingB1 << " -> "
         << got.bodyDampingB1 << ", bodyDampingB3 " << want.bodyDampingB3 << " -> "
         << got.bodyDampingB3 << "; first state difference at byte "
         << ppFirstDifference(bytes1, bytes0));
    const bool identical = (bytes1 == bytes0);
    CHECK(identical);

    p1->setActive(false);
    p1->terminate();
    p2->setActive(false);
    p2->terminate();
}
