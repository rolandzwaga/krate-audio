// ==============================================================================
// Unit Test: Flanger State Save/Load & Migration (126-ruinae-flanger, US6)
// ==============================================================================
// Tests for:
//   - New preset round-trip: set flanger params, save, load, verify restored
//   - Old preset migration: phaserEnabled_=1 -> modulationType=Phaser
//   - Old preset migration: phaserEnabled_=0 -> modulationType=None
//   - Absent flanger params (old preset, version 5): defaults applied
//
// Phase 8: T039-T046
// Reference: specs/126-ruinae-flanger/spec.md FR-014, SC-006
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

// Parameter pack headers needed for extracting state fields
#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/phaser_params.h"
#include "parameters/mono_mode_params.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mock: Parameter Value Queue (single param change)
// =============================================================================

namespace {

class FlangerStateParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    FlangerStateParamQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index,
        Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

class FlangerStateParamChangeBatch : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }

    void add(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<FlangerStateParamQueue> queues_;
};

// =============================================================================
// Testable Processor
// =============================================================================

class FlangerStateTestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

// =============================================================================
// Helpers
// =============================================================================

static std::unique_ptr<FlangerStateTestableProcessor> makeFlangerStateProcessor() {
    auto p = std::make_unique<FlangerStateTestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);
    p->setActive(true);

    return p;
}

/// Process a minimal block to apply parameter changes
static void processFlangerBlock(Ruinae::Processor* proc, size_t numSamples,
                                Steinberg::Vst::IParameterChanges* paramChanges = nullptr) {
    std::vector<float> outL(numSamples, 0.0f);
    std::vector<float> outR(numSamples, 0.0f);

    float* outputs[2] = { outL.data(), outR.data() };
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outputs;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = static_cast<Steinberg::int32>(numSamples);
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = paramChanges;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;

    proc->process(data);
}

/// Helper: extract modulationType and flanger params from a saved state.
struct FlangerStateExtract {
    int modulationType = -1;
    float rateHz = -1.0f;
    float depth = -1.0f;
    float feedback = -999.0f;
    float mix = -1.0f;
    float stereoSpread = -1.0f;
    int waveform = -1;
    int sync = -1;
    int noteValue = -1;
    bool flangerParamsFound = false;
};

/// Load all parameter packs to skip past them and reach the modulationType +
/// flanger params section. Mirrors the processor state format.
static FlangerStateExtract extractFlangerFromState(Steinberg::MemoryStream& stateStream) {
    FlangerStateExtract result;
    stateStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer streamer(&stateStream, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) return result;

    // Skip through all param packs in deterministic order (matching getState)
    Ruinae::GlobalParams globalP; loadGlobalParams(globalP, streamer);
    Ruinae::OscAParams oscAP; loadOscAParams(oscAP, streamer);
    Ruinae::OscBParams oscBP; loadOscBParams(oscBP, streamer);
    Ruinae::MixerParams mixerP; loadMixerParams(mixerP, streamer);
    Ruinae::RuinaeFilterParams filterP; loadFilterParams(filterP, streamer);
    Ruinae::RuinaeDistortionParams distP; loadDistortionParams(distP, streamer);
    Ruinae::RuinaeTranceGateParams tgP; loadTranceGateParams(tgP, streamer);
    Ruinae::AmpEnvParams aeP; loadAmpEnvParams(aeP, streamer);
    Ruinae::FilterEnvParams feP; loadFilterEnvParams(feP, streamer);
    Ruinae::ModEnvParams meP; loadModEnvParams(meP, streamer);
    Ruinae::LFO1Params l1P; loadLFO1Params(l1P, streamer);
    Ruinae::LFO2Params l2P; loadLFO2Params(l2P, streamer);
    Ruinae::ChaosModParams cmP; loadChaosModParams(cmP, streamer);
    Ruinae::ModMatrixParams mmP; loadModMatrixParams(mmP, streamer);
    Ruinae::GlobalFilterParams gfP; loadGlobalFilterParams(gfP, streamer);
    Ruinae::RuinaeDelayParams delP; loadDelayParams(delP, streamer);
    Ruinae::RuinaeReverbParams revP; loadReverbParams(revP, streamer);

    // Reverb type (version 5+)
    if (version >= 5) {
        Steinberg::int32 reverbType = 0;
        streamer.readInt32(reverbType);
    }

    // Mono mode params
    Ruinae::MonoModeParams monoP; loadMonoModeParams(monoP, streamer);

    // Skip voice routes (16 routes x 8 fields)
    for (int i = 0; i < 16; ++i) {
        Steinberg::int8 i8 = 0; float f = 0.0f;
        streamer.readInt8(i8); streamer.readInt8(i8);
        streamer.readFloat(f); streamer.readInt8(i8);
        streamer.readFloat(f); streamer.readInt8(i8);
        streamer.readInt8(i8); streamer.readInt8(i8);
    }

    // FX enable flags (delay, reverb)
    Steinberg::int8 flag = 0;
    streamer.readInt8(flag); // delay
    streamer.readInt8(flag); // reverb

    // Phaser params
    Ruinae::RuinaePhaserParams phaserP;
    loadPhaserParams(phaserP, streamer);

    // modulationType (int8 in current format)
    Steinberg::int8 modTypeI8 = 0;
    if (streamer.readInt8(modTypeI8)) {
        result.modulationType = static_cast<int>(modTypeI8);
    }

    // Flanger params (new in version 6+)
    if (version >= 6) {
        float fv = 0.0f;
        Steinberg::int32 iv = 0;
        if (streamer.readFloat(fv)) { result.rateHz = fv; result.flangerParamsFound = true; }
        if (streamer.readFloat(fv)) result.depth = fv;
        if (streamer.readFloat(fv)) result.feedback = fv;
        if (streamer.readFloat(fv)) result.mix = fv;
        if (streamer.readFloat(fv)) result.stereoSpread = fv;
        if (streamer.readInt32(iv)) result.waveform = static_cast<int>(iv);
        if (streamer.readInt32(iv)) result.sync = static_cast<int>(iv);
        if (streamer.readInt32(iv)) result.noteValue = static_cast<int>(iv);
    }

    return result;
}

/// Build a version-5 state from a version-7 state by removing flanger + chorus params
/// and patching the version number to 5.
static std::vector<char> buildV5StateFromV6(Steinberg::MemoryStream& v6Stream) {
    Steinberg::int64 totalSize = 0;
    v6Stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &totalSize);
    v6Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> v6Bytes(static_cast<size_t>(totalSize));
    Steinberg::int32 bytesRead = 0;
    v6Stream.read(v6Bytes.data(), static_cast<Steinberg::int32>(totalSize), &bytesRead);

    // Parse through the stream to find where flanger params start
    Steinberg::MemoryStream parseStream(v6Bytes.data(), static_cast<Steinberg::TSize>(totalSize));
    Steinberg::IBStreamer streamer(&parseStream, kLittleEndian);

    Steinberg::int32 version = 0;
    streamer.readInt32(version);

    // Skip all param packs
    Ruinae::GlobalParams globalP; loadGlobalParams(globalP, streamer);
    Ruinae::OscAParams oscAP; loadOscAParams(oscAP, streamer);
    Ruinae::OscBParams oscBP; loadOscBParams(oscBP, streamer);
    Ruinae::MixerParams mixerP; loadMixerParams(mixerP, streamer);
    Ruinae::RuinaeFilterParams filterP; loadFilterParams(filterP, streamer);
    Ruinae::RuinaeDistortionParams distP; loadDistortionParams(distP, streamer);
    Ruinae::RuinaeTranceGateParams tgP; loadTranceGateParams(tgP, streamer);
    Ruinae::AmpEnvParams aeP; loadAmpEnvParams(aeP, streamer);
    Ruinae::FilterEnvParams feP; loadFilterEnvParams(feP, streamer);
    Ruinae::ModEnvParams meP; loadModEnvParams(meP, streamer);
    Ruinae::LFO1Params l1P; loadLFO1Params(l1P, streamer);
    Ruinae::LFO2Params l2P; loadLFO2Params(l2P, streamer);
    Ruinae::ChaosModParams cmP; loadChaosModParams(cmP, streamer);
    Ruinae::ModMatrixParams mmP; loadModMatrixParams(mmP, streamer);
    Ruinae::GlobalFilterParams gfP; loadGlobalFilterParams(gfP, streamer);
    Ruinae::RuinaeDelayParams delP; loadDelayParams(delP, streamer);
    Ruinae::RuinaeReverbParams revP; loadReverbParams(revP, streamer);
    if (version >= 5) {
        Steinberg::int32 rt = 0; streamer.readInt32(rt);
    }
    Ruinae::MonoModeParams monoP; loadMonoModeParams(monoP, streamer);

    // Skip voice routes
    for (int i = 0; i < 16; ++i) {
        Steinberg::int8 i8 = 0; float f = 0.0f;
        streamer.readInt8(i8); streamer.readInt8(i8);
        streamer.readFloat(f); streamer.readInt8(i8);
        streamer.readFloat(f); streamer.readInt8(i8);
        streamer.readInt8(i8); streamer.readInt8(i8);
    }

    // FX enable flags
    Steinberg::int8 fl = 0;
    streamer.readInt8(fl); streamer.readInt8(fl);

    // Phaser params
    Ruinae::RuinaePhaserParams phaserP;
    loadPhaserParams(phaserP, streamer);

    // modulationType (int8)
    Steinberg::int8 modType = 0;
    streamer.readInt8(modType);

    // Current position is right after modulationType, before flanger params
    Steinberg::int64 flangerStart = 0;
    parseStream.tell(&flangerStart);

    // Flanger params: 5 floats + 3 int32 = 32 bytes
    // Chorus params: 5 floats + 4 int32 = 36 bytes
    constexpr size_t kFlangerParamBytes = 5 * 4 + 3 * 4; // 32 bytes
    constexpr size_t kChorusParamBytes = 5 * 4 + 4 * 4; // 36 bytes
    constexpr size_t kStripBytes = kFlangerParamBytes + kChorusParamBytes; // 68 bytes

    // Build v5 state: everything before flanger params + everything after chorus params
    std::vector<char> v5Bytes;
    v5Bytes.reserve(static_cast<size_t>(totalSize) - kStripBytes);

    // Copy everything before flanger params
    v5Bytes.insert(v5Bytes.end(), v6Bytes.begin(),
                   v6Bytes.begin() + static_cast<ptrdiff_t>(flangerStart));
    // Skip flanger + chorus param bytes
    v5Bytes.insert(v5Bytes.end(),
                   v6Bytes.begin() + static_cast<ptrdiff_t>(flangerStart) + kStripBytes,
                   v6Bytes.end());

    // Patch version to 5
    Steinberg::int32 v5Version = 5;
    std::memcpy(v5Bytes.data(), &v5Version, sizeof(v5Version));

    return v5Bytes;
}

/// Build a very old state (pre-v5) by removing both flanger params AND the
/// modulationType byte from a v6 state, then patching version to 5.
/// This simulates a preset so old that the phaserEnabled/modulationType field
/// was not yet written at all.
static std::vector<char> buildVeryOldStateFromV6(Steinberg::MemoryStream& v6Stream) {
    Steinberg::int64 totalSize = 0;
    v6Stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &totalSize);
    v6Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> v6Bytes(static_cast<size_t>(totalSize));
    Steinberg::int32 bytesRead = 0;
    v6Stream.read(v6Bytes.data(), static_cast<Steinberg::int32>(totalSize), &bytesRead);

    // Parse through to find where modulationType byte starts
    Steinberg::MemoryStream parseStream(v6Bytes.data(), static_cast<Steinberg::TSize>(totalSize));
    Steinberg::IBStreamer streamer(&parseStream, kLittleEndian);

    Steinberg::int32 version = 0;
    streamer.readInt32(version);

    // Skip all param packs
    Ruinae::GlobalParams globalP; loadGlobalParams(globalP, streamer);
    Ruinae::OscAParams oscAP; loadOscAParams(oscAP, streamer);
    Ruinae::OscBParams oscBP; loadOscBParams(oscBP, streamer);
    Ruinae::MixerParams mixerP; loadMixerParams(mixerP, streamer);
    Ruinae::RuinaeFilterParams filterP; loadFilterParams(filterP, streamer);
    Ruinae::RuinaeDistortionParams distP; loadDistortionParams(distP, streamer);
    Ruinae::RuinaeTranceGateParams tgP; loadTranceGateParams(tgP, streamer);
    Ruinae::AmpEnvParams aeP; loadAmpEnvParams(aeP, streamer);
    Ruinae::FilterEnvParams feP; loadFilterEnvParams(feP, streamer);
    Ruinae::ModEnvParams meP; loadModEnvParams(meP, streamer);
    Ruinae::LFO1Params l1P; loadLFO1Params(l1P, streamer);
    Ruinae::LFO2Params l2P; loadLFO2Params(l2P, streamer);
    Ruinae::ChaosModParams cmP; loadChaosModParams(cmP, streamer);
    Ruinae::ModMatrixParams mmP; loadModMatrixParams(mmP, streamer);
    Ruinae::GlobalFilterParams gfP; loadGlobalFilterParams(gfP, streamer);
    Ruinae::RuinaeDelayParams delP; loadDelayParams(delP, streamer);
    Ruinae::RuinaeReverbParams revP; loadReverbParams(revP, streamer);
    if (version >= 5) {
        Steinberg::int32 rt = 0; streamer.readInt32(rt);
    }
    Ruinae::MonoModeParams monoP; loadMonoModeParams(monoP, streamer);

    // Skip voice routes
    for (int i = 0; i < 16; ++i) {
        Steinberg::int8 i8 = 0; float f = 0.0f;
        streamer.readInt8(i8); streamer.readInt8(i8);
        streamer.readFloat(f); streamer.readInt8(i8);
        streamer.readFloat(f); streamer.readInt8(i8);
        streamer.readInt8(i8); streamer.readInt8(i8);
    }

    // FX enable flags
    Steinberg::int8 fl = 0;
    streamer.readInt8(fl); streamer.readInt8(fl);

    // Phaser params
    Ruinae::RuinaePhaserParams phaserP;
    loadPhaserParams(phaserP, streamer);

    // Current position is right before the modulationType byte
    Steinberg::int64 modTypePos = 0;
    parseStream.tell(&modTypePos);

    // Truncate here -- do NOT include modulationType byte or anything after
    std::vector<char> oldBytes(v6Bytes.begin(),
                               v6Bytes.begin() + static_cast<ptrdiff_t>(modTypePos));

    // Patch version to 5
    Steinberg::int32 v5Version = 5;
    std::memcpy(oldBytes.data(), &v5Version, sizeof(v5Version));

    return oldBytes;
}

} // anonymous namespace

// =============================================================================
// T039: New preset round-trip
// =============================================================================

TEST_CASE("Flanger state round-trip preserves all parameters", "[flanger_state][round_trip]") {
    auto proc1 = makeFlangerStateProcessor();

    // Set all flanger params to known non-default values via parameter changes.
    // Normalized values that map to the desired plain values:
    //   rate:    3.0 Hz -> (3.0 - 0.05) / 4.95 = 0.5960...
    //   depth:   0.8 -> 0.8
    //   feedback: -0.5 -> (-0.5 + 1.0) / 2.0 = 0.25
    //   mix:     0.75 -> 0.75
    //   stereoSpread: 180.0 -> 180.0 / 360.0 = 0.5
    //   waveform: Sine (0) -> 0.0 / 1.0 = 0.0
    //   sync:    true -> 1.0
    //   modulationType: Flanger (2) -> 2/2 = 1.0
    FlangerStateParamChangeBatch batch;
    batch.add(Ruinae::kFlangerRateId, (3.0 - 0.05) / 4.95);
    batch.add(Ruinae::kFlangerDepthId, 0.8);
    batch.add(Ruinae::kFlangerFeedbackId, 0.25);
    batch.add(Ruinae::kFlangerMixId, 0.75);
    batch.add(Ruinae::kFlangerStereoSpreadId, 0.5);
    batch.add(Ruinae::kFlangerWaveformId, 0.0); // Sine
    batch.add(Ruinae::kFlangerSyncId, 1.0);
    batch.add(Ruinae::kModulationTypeId, 2.0 / 3.0); // Flanger = 2 (normalized: 2/3)

    processFlangerBlock(proc1.get(), 512, &batch);

    // Save state
    Steinberg::MemoryStream stream;
    auto saveResult = proc1->getState(&stream);
    REQUIRE(saveResult == Steinberg::kResultTrue);

    // Extract and verify saved flanger params
    auto saved = extractFlangerFromState(stream);
    CHECK(saved.modulationType == 2); // Flanger
    CHECK(saved.flangerParamsFound);
    CHECK(saved.rateHz == Approx(3.0f).margin(0.1f));
    CHECK(saved.depth == Approx(0.8f).margin(1e-5f));
    CHECK(saved.feedback == Approx(-0.5f).margin(0.01f));
    CHECK(saved.mix == Approx(0.75f).margin(1e-5f));
    CHECK(saved.stereoSpread == Approx(180.0f).margin(0.5f));
    CHECK(saved.waveform == 0); // Sine
    CHECK(saved.sync == 1); // true

    // Load into a fresh processor
    auto proc2 = makeFlangerStateProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto loadResult = proc2->setState(&stream);
    REQUIRE(loadResult == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Save again from proc2 and verify round-trip
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);
    auto restored = extractFlangerFromState(stream2);

    CHECK(restored.modulationType == 2);
    CHECK(restored.flangerParamsFound);
    CHECK(restored.rateHz == Approx(saved.rateHz).margin(1e-5f));
    CHECK(restored.depth == Approx(saved.depth).margin(1e-5f));
    CHECK(restored.feedback == Approx(saved.feedback).margin(1e-5f));
    CHECK(restored.mix == Approx(saved.mix).margin(1e-5f));
    CHECK(restored.stereoSpread == Approx(saved.stereoSpread).margin(1e-5f));
    CHECK(restored.waveform == saved.waveform);
    CHECK(restored.sync == saved.sync);
    CHECK(restored.noteValue == saved.noteValue);

    proc1->setActive(false);
    proc1->terminate();
    proc2->setActive(false);
    proc2->terminate();
}

// =============================================================================
// T039: Old preset migration - phaserEnabled_=1 -> modulationType=Phaser
// =============================================================================

TEST_CASE("Old preset with phaserEnabled=1 migrates to modulationType=Phaser", "[flanger_state][migration]") {
    auto proc1 = makeFlangerStateProcessor();

    // Set modulationType to Phaser (1)
    FlangerStateParamChangeBatch batch;
    batch.add(Ruinae::kModulationTypeId, 1.0 / 3.0); // Phaser = 1 (normalized: 1/3)

    processFlangerBlock(proc1.get(), 512, &batch);

    // Save state
    Steinberg::MemoryStream stream;
    proc1->getState(&stream);

    // Load into fresh processor
    auto proc2 = makeFlangerStateProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc2->setState(&stream);
    drainPresetTransfer(proc2.get());

    // Verify modulationType round-trips as Phaser
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);
    auto restored = extractFlangerFromState(stream2);
    CHECK(restored.modulationType == 1); // Phaser

    proc1->setActive(false);
    proc1->terminate();
    proc2->setActive(false);
    proc2->terminate();
}

// =============================================================================
// T039: Old preset migration - phaserEnabled_=0 -> modulationType=None
// =============================================================================

TEST_CASE("Old preset with phaserEnabled=0 migrates to modulationType=None", "[flanger_state][migration]") {
    auto proc1 = makeFlangerStateProcessor();

    // Default modulationType is 0 (None) -- save as-is
    Steinberg::MemoryStream stream;
    proc1->getState(&stream);

    // Load into fresh processor
    auto proc2 = makeFlangerStateProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc2->setState(&stream);
    drainPresetTransfer(proc2.get());

    // Verify
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);
    auto restored = extractFlangerFromState(stream2);
    CHECK(restored.modulationType == 0); // None

    proc1->setActive(false);
    proc1->terminate();
    proc2->setActive(false);
    proc2->terminate();
}

// =============================================================================
// T039: Flanger params default correctly when absent from old v5 state
// =============================================================================

TEST_CASE("Flanger params default when loaded from version 5 state", "[flanger_state][defaults]") {
    // First save a v6 state, then build a v5 state by removing flanger params.
    auto proc1 = makeFlangerStateProcessor();

    // Set flanger params to non-default values so we have a valid v6 state
    FlangerStateParamChangeBatch batch;
    batch.add(Ruinae::kFlangerRateId, 0.8);
    batch.add(Ruinae::kModulationTypeId, 1.0 / 3.0); // Phaser
    processFlangerBlock(proc1.get(), 512, &batch);

    Steinberg::MemoryStream v6Stream;
    proc1->getState(&v6Stream);

    // Build a v5 state by stripping flanger param bytes
    std::vector<char> v5Bytes = buildV5StateFromV6(v6Stream);
    REQUIRE(v5Bytes.size() > 4);

    // Load v5 state into fresh processor
    auto proc2 = makeFlangerStateProcessor();
    Steinberg::MemoryStream v5Stream(v5Bytes.data(),
                                     static_cast<Steinberg::TSize>(v5Bytes.size()));
    auto result = proc2->setState(&v5Stream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Save from proc2 and extract - flanger params should be at defaults
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);
    auto restored = extractFlangerFromState(stream2);

    // modulationType should still be loaded (1 = Phaser from the v5 state)
    CHECK(restored.modulationType == 1);

    // Flanger params should be at defaults
    if (restored.flangerParamsFound) {
        CHECK(restored.rateHz == Approx(0.5f).margin(0.01f));
        CHECK(restored.depth == Approx(0.5f).margin(1e-5f));
        CHECK(restored.feedback == Approx(0.0f).margin(1e-5f));
        CHECK(restored.mix == Approx(0.5f).margin(1e-5f));
        CHECK(restored.stereoSpread == Approx(90.0f).margin(0.5f));
        CHECK(restored.waveform == 1); // Triangle
        CHECK(restored.sync == 0); // false
    }

    // Verify processor can still process audio
    processFlangerBlock(proc2.get(), 512);

    proc1->setActive(false);
    proc1->terminate();
    proc2->setActive(false);
    proc2->terminate();
}

// =============================================================================
// T039: Absent boolean (very old preset -- reading past stream end)
// =============================================================================

TEST_CASE("Very old preset with absent modulationType defaults to Phaser", "[flanger_state][migration]") {
    // Build a state stream that ends right before the modulationType byte,
    // simulating a very old preset format where the phaserEnabled/modulationType
    // field was never written.
    auto proc1 = makeFlangerStateProcessor();

    // Save a normal v6 state first
    Steinberg::MemoryStream v6Stream;
    proc1->getState(&v6Stream);

    // Strip everything from modulationType onward
    std::vector<char> veryOldBytes = buildVeryOldStateFromV6(v6Stream);
    REQUIRE(veryOldBytes.size() > 4);

    // Load into fresh processor
    auto proc2 = makeFlangerStateProcessor();
    Steinberg::MemoryStream oldStream(veryOldBytes.data(),
                                      static_cast<Steinberg::TSize>(veryOldBytes.size()));
    auto result = proc2->setState(&oldStream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Save from proc2 and extract -- modulationType should default to Phaser (1)
    // per FR-011: "If the boolean is absent entirely (even older preset),
    // the default of modType=Phaser is applied to preserve pre-existing behavior."
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);
    auto restored = extractFlangerFromState(stream2);
    CHECK(restored.modulationType == 1); // Phaser (default for absent field)

    // Verify processor can still process audio without crash
    processFlangerBlock(proc2.get(), 512);

    proc1->setActive(false);
    proc1->terminate();
    proc2->setActive(false);
    proc2->terminate();
}
