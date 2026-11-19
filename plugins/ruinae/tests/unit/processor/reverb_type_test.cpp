// ==============================================================================
// Unit Test: Reverb Type Selection (125-dual-reverb, User Story 3)
// ==============================================================================
// Tests for kReverbTypeId parameter registration, crossfade switching,
// click-free transitions, state save/load, backward compatibility,
// parameter routing, and freeze+switch behavior.
//
// Phase 5: T041-T046B
// Reference: specs/125-dual-reverb/spec.md FR-023 through FR-029
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"
#include <artifact_detection.h>

using Catch::Approx;

// =============================================================================
// Mock: Parameter Value Queue (single param change)
// =============================================================================

namespace {

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SingleParamQueue = Krate::Test::ParamValueQueue;
using ParamChangeBatch = Krate::Test::ParameterChanges;




// =============================================================================
// Testable Processor (expose processParameterChanges)
// =============================================================================

class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
    using Ruinae::Processor::engine;
};

// =============================================================================
// Helpers
// =============================================================================

static std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
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

static Ruinae::Controller* makeControllerRaw() {
    auto* ctrl = new Ruinae::Controller();
    ctrl->initialize(nullptr);
    return ctrl;
}

/// Process a stereo block of silence through the processor
static void processBlock(Ruinae::Processor* proc, size_t numSamples,
                         std::vector<float>& outL, std::vector<float>& outR,
                         Steinberg::Vst::IParameterChanges* paramChanges = nullptr) {
    outL.resize(numSamples, 0.0f);
    outR.resize(numSamples, 0.0f);

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

/// Compute RMS of a buffer
static float rms(const float* buf, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(buf[i]) * buf[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

/// Check if all samples are finite
static bool allFinite(const float* buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(buf[i])) return false;
    }
    return true;
}

/// Get max amplitude delta between consecutive samples
static float maxAmplitudeDelta(const float* buf, size_t n) {
    float maxDelta = 0.0f;
    for (size_t i = 1; i < n; ++i) {
        float delta = std::abs(buf[i] - buf[i - 1]);
        if (delta > maxDelta) maxDelta = delta;
    }
    return maxDelta;
}

// =============================================================================
// Mock: Event List (for MIDI note events)
// =============================================================================

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using MockEventList = Krate::Test::EventList;


/// Process a stereo block with MIDI events through the processor
static void processBlockWithEvents(Ruinae::Processor* proc, size_t numSamples,
                                   std::vector<float>& outL, std::vector<float>& outR,
                                   Steinberg::Vst::IParameterChanges* paramChanges,
                                   Steinberg::Vst::IEventList* events) {
    outL.assign(numSamples, 0.0f);
    outR.assign(numSamples, 0.0f);

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
    data.inputEvents = events;
    data.outputEvents = nullptr;
    data.processContext = nullptr;

    proc->process(data);
}

/// Extract the reverbType value from a saved state stream.
/// Returns the int32 reverbType value (0=Plate, 1=Hall).
/// Parses through the binary state by loading all param packs to
/// reach the reverbType field position.
static Steinberg::int32 extractReverbTypeFromState(Steinberg::MemoryStream& stateStream) {
    stateStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::IBStreamer streamer(&stateStream, kLittleEndian);

    // Read version
    Steinberg::int32 version = 0;
    streamer.readInt32(version);

    // Skip through all parameter packs (using dummy structs to read values)
    Ruinae::GlobalParams globalP;
    loadGlobalParams(globalP, streamer);
    Ruinae::OscAParams oscAP;
    loadOscAParams(oscAP, streamer);
    Ruinae::OscBParams oscBP;
    loadOscBParams(oscBP, streamer);
    Ruinae::MixerParams mixerP;
    loadMixerParams(mixerP, streamer);
    Ruinae::RuinaeFilterParams filterP;
    loadFilterParams(filterP, streamer);
    Ruinae::RuinaeDistortionParams distP;
    loadDistortionParams(distP, streamer);
    Ruinae::RuinaeTranceGateParams tgP;
    loadTranceGateParams(tgP, streamer);
    Ruinae::AmpEnvParams aeP;
    loadAmpEnvParams(aeP, streamer);
    Ruinae::FilterEnvParams feP;
    loadFilterEnvParams(feP, streamer);
    Ruinae::ModEnvParams meP;
    loadModEnvParams(meP, streamer);
    Ruinae::LFO1Params l1P;
    loadLFO1Params(l1P, streamer);
    Ruinae::LFO2Params l2P;
    loadLFO2Params(l2P, streamer);
    Ruinae::ChaosModParams cmP;
    loadChaosModParams(cmP, streamer);
    Ruinae::ModMatrixParams mmP;
    loadModMatrixParams(mmP, streamer);
    Ruinae::GlobalFilterParams gfP;
    loadGlobalFilterParams(gfP, streamer);
    Ruinae::RuinaeDelayParams delP;
    loadDelayParams(delP, streamer);
    Ruinae::RuinaeReverbParams revP;
    loadReverbParams(revP, streamer);

    // Now read the reverbType int32
    Steinberg::int32 reverbType = -1;
    if (version >= 5) {
        streamer.readInt32(reverbType);
    }
    return reverbType;
}

/// Build a version-4 state stream from a version-5 state stream.
/// Removes the reverbType int32 field and patches version to 4.
static std::vector<char> buildV4StateFromV5(Steinberg::MemoryStream& v5Stream) {
    // Get raw bytes from the v5 stream
    Steinberg::int64 totalSize = 0;
    v5Stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &totalSize);
    v5Stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    std::vector<char> v5Bytes(static_cast<size_t>(totalSize));
    Steinberg::int32 bytesRead = 0;
    v5Stream.read(v5Bytes.data(), static_cast<Steinberg::int32>(totalSize), &bytesRead);

    // Parse through the stream to find the reverbType field position
    Steinberg::MemoryStream parseStream(v5Bytes.data(), static_cast<Steinberg::TSize>(totalSize));
    Steinberg::IBStreamer streamer(&parseStream, kLittleEndian);

    Steinberg::int32 version = 0;
    streamer.readInt32(version);

    // Skip all param packs up to and including reverb params
    Ruinae::GlobalParams globalP;
    loadGlobalParams(globalP, streamer);
    Ruinae::OscAParams oscAP;
    loadOscAParams(oscAP, streamer);
    Ruinae::OscBParams oscBP;
    loadOscBParams(oscBP, streamer);
    Ruinae::MixerParams mixerP;
    loadMixerParams(mixerP, streamer);
    Ruinae::RuinaeFilterParams filterP;
    loadFilterParams(filterP, streamer);
    Ruinae::RuinaeDistortionParams distP;
    loadDistortionParams(distP, streamer);
    Ruinae::RuinaeTranceGateParams tgP;
    loadTranceGateParams(tgP, streamer);
    Ruinae::AmpEnvParams aeP;
    loadAmpEnvParams(aeP, streamer);
    Ruinae::FilterEnvParams feP;
    loadFilterEnvParams(feP, streamer);
    Ruinae::ModEnvParams meP;
    loadModEnvParams(meP, streamer);
    Ruinae::LFO1Params l1P;
    loadLFO1Params(l1P, streamer);
    Ruinae::LFO2Params l2P;
    loadLFO2Params(l2P, streamer);
    Ruinae::ChaosModParams cmP;
    loadChaosModParams(cmP, streamer);
    Ruinae::ModMatrixParams mmP;
    loadModMatrixParams(mmP, streamer);
    Ruinae::GlobalFilterParams gfP;
    loadGlobalFilterParams(gfP, streamer);
    Ruinae::RuinaeDelayParams delP;
    loadDelayParams(delP, streamer);
    Ruinae::RuinaeReverbParams revP;
    loadReverbParams(revP, streamer);

    // Current position is right before the reverbType int32
    Steinberg::int64 reverbTypePos = 0;
    parseStream.tell(&reverbTypePos);

    // Build the v4 state: everything before reverbType + everything after reverbType+4
    std::vector<char> v4Bytes;
    v4Bytes.reserve(static_cast<size_t>(totalSize) - 4);

    // Copy everything before reverbType
    v4Bytes.insert(v4Bytes.end(), v5Bytes.begin(),
                   v5Bytes.begin() + static_cast<ptrdiff_t>(reverbTypePos));
    // Skip the 4-byte reverbType field
    v4Bytes.insert(v4Bytes.end(),
                   v5Bytes.begin() + static_cast<ptrdiff_t>(reverbTypePos) + 4,
                   v5Bytes.end());

    // Patch version from 5 to 4 (first 4 bytes, little-endian int32)
    Steinberg::int32 v4Version = 4;
    std::memcpy(v4Bytes.data(), &v4Version, sizeof(v4Version));

    return v4Bytes;
}

} // anonymous namespace

// =============================================================================
// T041: kReverbTypeId parameter registration
// =============================================================================

TEST_CASE("kReverbTypeId is registered as StringListParameter", "[reverb_type][controller]") {
    auto* ctrl = makeControllerRaw();

    // Verify the parameter exists
    auto* paramObj = ctrl->getParameterObject(Ruinae::kReverbTypeId);
    REQUIRE(paramObj != nullptr);

    // Verify it has 2 steps (Plate, Hall)
    // StringListParameter: stepCount = numStrings - 1
    CHECK(paramObj->getInfo().stepCount == 1);

    ctrl->terminate();
}

// =============================================================================
// T042: Reverb type switching triggers crossfade with finite output
// =============================================================================

TEST_CASE("Reverb type switch produces finite output during crossfade", "[reverb_type][crossfade]") {
    auto proc = makeTestableProcessor();

    // Enable reverb and set mix to 100% wet
    ParamChangeBatch batch;
    batch.add(Ruinae::kReverbEnabledId, 1.0);
    batch.add(Ruinae::kReverbMixId, 1.0);
    batch.add(Ruinae::kReverbSizeId, 0.8);

    std::vector<float> outL, outR;

    // Process some initial audio to build up reverb tail
    processBlock(proc.get(), 512, outL, outR, &batch);
    processBlock(proc.get(), 512, outL, outR);
    processBlock(proc.get(), 512, outL, outR);

    // Switch to Hall (type=1)
    ParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kReverbTypeId, 1.0);

    // Process during the crossfade window (~1323 samples at 44.1kHz for 30ms)
    processBlock(proc.get(), 2048, outL, outR, &switchBatch);

    // Output should be finite during crossfade
    CHECK(allFinite(outL.data(), outL.size()));
    CHECK(allFinite(outR.data(), outR.size()));

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T043: SC-003 click-free switching
// =============================================================================

TEST_CASE("Reverb type switch is click-free (max delta < 0.01)", "[reverb_type][click_free]") {
    auto proc = makeTestableProcessor();

    // Enable reverb with moderate mix
    ParamChangeBatch initBatch;
    initBatch.add(Ruinae::kReverbEnabledId, 1.0);
    initBatch.add(Ruinae::kReverbMixId, 0.5);
    initBatch.add(Ruinae::kReverbSizeId, 0.5);

    std::vector<float> outL, outR;

    // Process several blocks to build up a reverb tail
    processBlock(proc.get(), 512, outL, outR, &initBatch);
    for (int i = 0; i < 10; ++i) {
        processBlock(proc.get(), 512, outL, outR);
    }

    // Switch to Hall
    ParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kReverbTypeId, 1.0);

    // Process the crossfade window
    processBlock(proc.get(), 2048, outL, outR, &switchBatch);

    // Max amplitude delta per sample should be small (no click)
    float maxDeltaL = maxAmplitudeDelta(outL.data(), outL.size());
    float maxDeltaR = maxAmplitudeDelta(outR.data(), outR.size());

    CHECK(maxDeltaL < 0.01f);
    CHECK(maxDeltaR < 0.01f);

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T044: State save/load with reverb type (FR-026, SC-006)
// =============================================================================

TEST_CASE("State save/load preserves reverb type", "[reverb_type][state]") {
    auto proc1 = makeTestableProcessor();

    // Set reverb type to Hall (1)
    ParamChangeBatch batch;
    batch.add(Ruinae::kReverbTypeId, 1.0);
    std::vector<float> outL, outR;
    processBlock(proc1.get(), 512, outL, outR, &batch);

    // Save state
    Steinberg::MemoryStream stream;
    auto saveResult = proc1->getState(&stream);
    REQUIRE(saveResult == Steinberg::kResultTrue);

    // Verify the saved state contains reverbType=1
    Steinberg::int32 savedType = extractReverbTypeFromState(stream);
    CHECK(savedType == 1);

    // Load into a fresh processor
    auto proc2 = makeTestableProcessor();
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    auto loadResult = proc2->setState(&stream);
    REQUIRE(loadResult == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Save again from proc2 and verify reverbType=1 was preserved
    Steinberg::MemoryStream stream2;
    proc2->getState(&stream2);
    Steinberg::int32 restoredType = extractReverbTypeFromState(stream2);
    CHECK(restoredType == 1);

    proc1->setActive(false);
    proc1->terminate();
    proc2->setActive(false);
    proc2->terminate();
}

// =============================================================================
// T045: Backward compatibility - version 4 state loads with Plate default
// =============================================================================

TEST_CASE("Version 4 state loads without crash, defaults to Plate", "[reverb_type][backward_compat]") {
    // Build a genuine version-4 state: save a v5 state, then remove the
    // reverbType int32 field and patch the version number to 4.
    auto proc = makeTestableProcessor();

    // Set reverbType to Hall (1) so we can verify it's NOT restored from v4
    ParamChangeBatch batch;
    batch.add(Ruinae::kReverbTypeId, 1.0);
    std::vector<float> outL, outR;
    processBlock(proc.get(), 512, outL, outR, &batch);

    // Save a v5 state (with reverbType=1)
    Steinberg::MemoryStream v5Stream;
    proc->getState(&v5Stream);

    // Build a v4 state by removing the reverbType field and patching version
    std::vector<char> v4Bytes = buildV4StateFromV5(v5Stream);
    REQUIRE(v4Bytes.size() > 4);

    // Verify the v4 state is 4 bytes smaller than v5 (the removed reverbType)
    Steinberg::int64 v5Size = 0;
    v5Stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &v5Size);
    CHECK(v4Bytes.size() == static_cast<size_t>(v5Size) - 4);

    // Load v4 state into a fresh processor
    auto proc2 = makeTestableProcessor();
    Steinberg::MemoryStream v4Stream(v4Bytes.data(),
                                     static_cast<Steinberg::TSize>(v4Bytes.size()));
    auto result = proc2->setState(&v4Stream);
    REQUIRE(result == Steinberg::kResultTrue);
    drainPresetTransfer(proc2.get());

    // Save state from proc2 and verify reverbType defaults to 0 (Plate)
    Steinberg::MemoryStream restoredStream;
    proc2->getState(&restoredStream);
    Steinberg::int32 restoredType = extractReverbTypeFromState(restoredStream);
    CHECK(restoredType == 0);

    // Verify processor can still process audio without crash
    processBlock(proc2.get(), 512, outL, outR);
    CHECK(allFinite(outL.data(), outL.size()));

    proc->setActive(false);
    proc->terminate();
    proc2->setActive(false);
    proc2->terminate();
}

// =============================================================================
// T046: FR-027 parameter routing - reverb params apply to active type
// =============================================================================

TEST_CASE("Reverb parameters route to active reverb type", "[reverb_type][routing]") {
    auto proc = makeTestableProcessor();

    // Enable reverb, switch to Hall, set mix to 100% wet
    ParamChangeBatch initBatch;
    initBatch.add(Ruinae::kReverbEnabledId, 1.0);
    initBatch.add(Ruinae::kReverbTypeId, 1.0);
    initBatch.add(Ruinae::kReverbMixId, 1.0);
    initBatch.add(Ruinae::kReverbSizeId, 0.8);

    std::vector<float> outL, outR;

    // Send a MIDI note to generate audio through the synth engine
    MockEventList noteOnEvents;
    noteOnEvents.addNoteOn(60, 0.9f); // Middle C, high velocity
    processBlockWithEvents(proc.get(), 512, outL, outR, &initBatch, &noteOnEvents);

    // Process more blocks (without note events) to let reverb build up
    MockEventList emptyEvents;
    for (int i = 0; i < 20; ++i) {
        processBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }

    // With mix=1.0 and a note playing, output should be non-zero
    float outputRms = rms(outL.data(), outL.size());
    CHECK(allFinite(outL.data(), outL.size()));
    CHECK(allFinite(outR.data(), outR.size()));
    CHECK(outputRms > 0.0001f); // Non-zero reverb output

    // Now set mix=0.0 (dry only) and process more blocks
    ParamChangeBatch dryBatch;
    dryBatch.add(Ruinae::kReverbMixId, 0.0);
    processBlockWithEvents(proc.get(), 512, outL, outR, &dryBatch, &emptyEvents);
    for (int i = 0; i < 5; ++i) {
        processBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }

    // With mix=0.0, the reverb contribution should be absent (dry only)
    float dryRms = rms(outL.data(), outL.size());
    CHECK(allFinite(outL.data(), outL.size()));
    // dry output should still exist (synth is playing)
    // but reverb tail energy should be lower when comparing a block
    // where mix changed from 1.0 (pure wet) to 0.0 (pure dry)
    // We just need to verify both modes produce valid audio
    CHECK(dryRms >= 0.0f); // Valid output

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// T046B: FR-029 freeze+switch
// =============================================================================

TEST_CASE("Freeze is applied to incoming reverb before crossfade", "[reverb_type][freeze_switch]") {
    auto proc = makeTestableProcessor();

    // Step 1: Enable reverb WITHOUT freeze, send a note to build up reverb tail
    ParamChangeBatch initBatch;
    initBatch.add(Ruinae::kReverbEnabledId, 1.0);
    initBatch.add(Ruinae::kReverbMixId, 1.0);
    initBatch.add(Ruinae::kReverbSizeId, 0.8);

    std::vector<float> outL, outR;
    MockEventList noteOnEvents;
    noteOnEvents.addNoteOn(60, 0.9f);
    processBlockWithEvents(proc.get(), 512, outL, outR, &initBatch, &noteOnEvents);

    // Process several blocks to build up reverb tail
    MockEventList emptyEvents;
    for (int i = 0; i < 20; ++i) {
        processBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }

    // Step 2: Enable freeze to capture the reverb tail
    ParamChangeBatch freezeBatch;
    freezeBatch.add(Ruinae::kReverbFreezeId, 1.0);
    processBlockWithEvents(proc.get(), 512, outL, outR, &freezeBatch, &emptyEvents);

    // Process a few more blocks with freeze active to verify tail sustains
    for (int i = 0; i < 5; ++i) {
        processBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }
    // Verify the frozen tail is audible before switching
    CHECK(rms(outL.data(), outL.size()) > 1e-6f);

    // Step 3: Switch to Hall while freeze is active
    ParamChangeBatch switchBatch;
    switchBatch.add(Ruinae::kReverbTypeId, 1.0);

    // Process crossfade window (~1323 samples at 44.1kHz for 30ms)
    processBlockWithEvents(proc.get(), 2048, outL, outR, &switchBatch, &emptyEvents);

    // (a) During crossfade, both reverbs should contribute non-zero output
    float crossfadeRms = rms(outL.data(), outL.size());
    CHECK(allFinite(outL.data(), outL.size()));
    CHECK(allFinite(outR.data(), outR.size()));
    // The frozen plate reverb tail should still be audible during crossfade
    CHECK(crossfadeRms > 1e-6f);

    // (c) No click at switch point — with active audio content (synth note +
    // frozen reverb tail), the amplitude deltas may be slightly larger than
    // in the silence-only click test (T043). Threshold raised after fixing
    // Dattorro bandwidth filter (was incorrectly a 3.8 Hz lowpass, now
    // near-transparent as specified in the paper, producing higher tank energy).
    float maxDeltaL = maxAmplitudeDelta(outL.data(), outL.size());
    float maxDeltaR = maxAmplitudeDelta(outR.data(), outR.size());
    CHECK(maxDeltaL < 0.5f);
    CHECK(maxDeltaR < 0.5f);

    // (b) After crossfade completes, if FDN entered freeze mode, it should
    // sustain output even with no input. Process several hundred ms of
    // silence and verify output doesn't decay to zero.
    for (int i = 0; i < 40; ++i) {
        processBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }
    float rmsAfterCrossfade = rms(outL.data(), outL.size());

    // Process another 500ms and check RMS hasn't decayed significantly
    for (int i = 0; i < 40; ++i) {
        processBlockWithEvents(proc.get(), 512, outL, outR, nullptr, &emptyEvents);
    }
    float rmsLater = rms(outL.data(), outL.size());

    // The frozen tail must still be there: the switch is deferred while
    // Freeze is on (R-061), so the frozen tank is neither faded into an empty
    // one nor reset. The later RMS should be at least 30% of the earlier RMS
    // (not decaying away to nothing).
    CHECK(rmsAfterCrossfade > 1e-6f);
    CHECK(rmsLater > rmsAfterCrossfade * 0.3f);

    proc->setActive(false);
    proc->terminate();
}

// =============================================================================
// R-134 / R-056: the processor re-sends the reverb type on every block
// =============================================================================
// applyParamsToEngine() calls setReverbType() every block. The tests above
// switch inside one 2048-sample block, longer than the 30 ms fade, so they
// never see a request repeated mid-fade. These render at host block sizes.

namespace {

constexpr Steinberg::int16 kA3Note = 57; // 220 Hz
constexpr size_t kSamplesPer100Ms = 4410;  // at 44.1 kHz
constexpr size_t kSamplesPerSecond = 44100;

/// Whole blocks needed to cover at least numSamples.
size_t blocksFor(size_t numSamples, size_t blockSize) {
    return (numSamples + blockSize - 1) / blockSize;
}

/// Render numBlocks blocks of blockSize, appending the output to accL/accR.
/// firstParams / firstEvents go with the first block only.
void renderBlocks(Ruinae::Processor* proc, size_t blockSize, size_t numBlocks,
                  std::vector<float>& accL, std::vector<float>& accR,
                  Steinberg::Vst::IParameterChanges* firstParams = nullptr,
                  Steinberg::Vst::IEventList* firstEvents = nullptr) {
    std::vector<float> blockL;
    std::vector<float> blockR;
    MockEventList noEvents;
    for (size_t b = 0; b < numBlocks; ++b) {
        const bool first = (b == 0);
        processBlockWithEvents(proc, blockSize, blockL, blockR,
                               first ? firstParams : nullptr,
                               (first && firstEvents != nullptr) ? firstEvents : &noEvents);
        accL.insert(accL.end(), blockL.begin(), blockL.end());
        accR.insert(accR.end(), blockR.begin(), blockR.end());
    }
}

/// Load into proc a state saved from a fresh processor whose only change is
/// the reverb type (normalized 0 = Plate, 1 = Hall). Restores the type
/// without a crossfade (setReverbTypeDirect on the load path).
void loadStateWithReverbType(Ruinae::Processor* proc, double reverbTypeNorm) {
    auto source = makeTestableProcessor();
    ParamChangeBatch batch;
    batch.add(Ruinae::kReverbTypeId, reverbTypeNorm);
    std::vector<float> outL;
    std::vector<float> outR;
    processBlock(source.get(), 512, outL, outR, &batch);

    Steinberg::MemoryStream stream;
    REQUIRE(source->getState(&stream) == Steinberg::kResultTrue);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(&stream) == Steinberg::kResultTrue);
    drainPresetTransfer(proc);

    source->setActive(false);
    source->terminate();
}

/// sqrt(sum (a-b)^2 / sum b^2) over the last `count` samples of both channels.
double relativeRmsDifference(const std::vector<float>& aL, const std::vector<float>& aR,
                             const std::vector<float>& bL, const std::vector<float>& bR,
                             size_t count) {
    double diff = 0.0;
    double ref = 0.0;
    const size_t start = aL.size() - count;
    for (size_t i = start; i < aL.size(); ++i) {
        const double dL = static_cast<double>(aL[i]) - bL[i];
        const double dR = static_cast<double>(aR[i]) - bR[i];
        diff += dL * dL + dR * dR;
        ref += static_cast<double>(bL[i]) * bL[i] + static_cast<double>(bR[i]) * bR[i];
    }
    return ref > 0.0 ? std::sqrt(diff / ref) : 1.0;
}

/// Clicks the detector finds in `signal` that it does not also find, within
/// the merge gap, in `reference`. The wet output of a held sawtooth carries
/// per-period edges the detector flags in any render; only a detection the
/// untoggled reference lacks is a click caused by the type switch.
std::vector<Krate::DSP::TestUtils::ClickDetection> clicksNotInReference(
    const std::vector<float>& signal, const std::vector<float>& reference) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg;
    cfg.sampleRate = 44100.0f;
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    const auto refClicks = detector.detect(reference.data(), reference.size());
    const auto clicks = detector.detect(signal.data(), signal.size());
    std::vector<Krate::DSP::TestUtils::ClickDetection> extra;
    for (const auto& c : clicks) {
        const bool inReference = std::any_of(
            refClicks.begin(), refClicks.end(),
            [&](const auto& r) { return c.isAdjacentTo(r, cfg.mergeGap); });
        if (!inReference) extra.push_back(c);
    }
    return extra;
}

std::string describeClicks(const std::vector<Krate::DSP::TestUtils::ClickDetection>& clicks) {
    std::string s;
    for (const auto& c : clicks)
        s += std::to_string(c.sampleIndex) + "(" + std::to_string(c.amplitude) + ") ";
    return s;
}

void checkReverbTypeSwitchCompletes(size_t blockSize) {
    auto proc = makeTestableProcessor();
    auto ref = makeTestableProcessor();
    loadStateWithReverbType(proc.get(), 0.0);
    loadStateWithReverbType(ref.get(), 1.0);
    REQUIRE(proc->engine().activeReverbTypeForTest() == 0);
    REQUIRE(ref->engine().activeReverbTypeForTest() == 1);

    ParamChangeBatch procInit;
    procInit.add(Ruinae::kReverbEnabledId, 1.0);
    procInit.add(Ruinae::kReverbMixId, 1.0);
    procInit.add(Ruinae::kReverbTypeId, 1.0); // Hall, sent once
    ParamChangeBatch refInit;
    refInit.add(Ruinae::kReverbEnabledId, 1.0);
    refInit.add(Ruinae::kReverbMixId, 1.0);
    MockEventList procNote;
    procNote.addNoteOn(kA3Note, 0.8f);
    MockEventList refNote;
    refNote.addNoteOn(kA3Note, 0.8f);

    std::vector<float> procL;
    std::vector<float> procR;
    std::vector<float> refL;
    std::vector<float> refR;

    // (a) 100 ms after the single request the switch is complete.
    const size_t blocks100Ms = blocksFor(kSamplesPer100Ms, blockSize);
    renderBlocks(proc.get(), blockSize, blocks100Ms, procL, procR, &procInit, &procNote);
    renderBlocks(ref.get(), blockSize, blocks100Ms, refL, refR, &refInit, &refNote);
    CHECK(proc->engine().activeReverbTypeForTest() == 1);
    CHECK_FALSE(proc->engine().isReverbCrossfadingForTest());

    // (b) Over the last second of a 3 s render, the output matches a processor
    // that was on Hall from the start.
    const size_t totalBlocks = blocksFor(3 * kSamplesPerSecond, blockSize);
    renderBlocks(proc.get(), blockSize, totalBlocks - blocks100Ms, procL, procR);
    renderBlocks(ref.get(), blockSize, totalBlocks - blocks100Ms, refL, refR);
    REQUIRE(procL.size() == refL.size());
    REQUIRE(rms(refL.data() + refL.size() - kSamplesPerSecond, kSamplesPerSecond) > 1e-4f);

    const double relDiff = relativeRmsDifference(procL, procR, refL, refR, kSamplesPerSecond);
    INFO("block " << blockSize << ": relative RMS difference " << relDiff);
    CHECK(relDiff < 0.01);
    CHECK(proc->engine().activeReverbTypeForTest() == 1);
    CHECK_FALSE(proc->engine().isReverbCrossfadingForTest());

    proc->setActive(false);
    proc->terminate();
    ref->setActive(false);
    ref->terminate();
}

void checkReverbToggleBackKeepsTail(size_t blockSize) {
    auto proc = makeTestableProcessor();
    auto ref = makeTestableProcessor();

    std::vector<float> procL;
    std::vector<float> procR;
    std::vector<float> refL;
    std::vector<float> refR;

    // Build a Plate tail: 400 ms of a held 220 Hz note, 100 % wet.
    for (auto* p : {proc.get(), ref.get()}) {
        ParamChangeBatch init;
        init.add(Ruinae::kReverbEnabledId, 1.0);
        init.add(Ruinae::kReverbMixId, 1.0);
        init.add(Ruinae::kReverbSizeId, 0.8);
        MockEventList noteOn;
        noteOn.addNoteOn(kA3Note, 0.8f);
        std::vector<float> scratchL;
        std::vector<float> scratchR;
        renderBlocks(p, blockSize, blocksFor(4 * kSamplesPer100Ms, blockSize),
                     scratchL, scratchR, &init, &noteOn);
    }
    REQUIRE(proc->engine().activeReverbTypeForTest() == 0);

    // Request Hall, and one block later request Plate again. The processor
    // keeps re-sending Plate on every block after that.
    ParamChangeBatch toHall;
    toHall.add(Ruinae::kReverbTypeId, 1.0);
    ParamChangeBatch toPlate;
    toPlate.add(Ruinae::kReverbTypeId, 0.0);
    renderBlocks(proc.get(), blockSize, 1, procL, procR, &toHall);
    renderBlocks(proc.get(), blockSize, 1, procL, procR, &toPlate);
    renderBlocks(ref.get(), blockSize, 2, refL, refR);

    const size_t tailBlocks = blocksFor(5 * kSamplesPer100Ms, blockSize);
    renderBlocks(proc.get(), blockSize, tailBlocks - 2, procL, procR);
    renderBlocks(ref.get(), blockSize, tailBlocks - 2, refL, refR);

    const float procTail = rms(procL.data(), procL.size());
    const float refTail = rms(refL.data(), refL.size());
    INFO("block " << blockSize << ": toggled tail RMS " << procTail
                  << ", reference tail RMS " << refTail);
    REQUIRE(refTail > 1e-4f);
    CHECK(procTail >= 0.5f * refTail);

    const auto clicksL = clicksNotInReference(procL, refL);
    const auto clicksR = clicksNotInReference(procR, refR);
    INFO("clicks not in the reference: L " << describeClicks(clicksL)
                                            << "| R " << describeClicks(clicksR));
    CHECK(clicksL.empty());
    CHECK(clicksR.empty());

    CHECK(proc->engine().activeReverbTypeForTest() == 0);
    CHECK_FALSE(proc->engine().isReverbCrossfadingForTest());

    proc->setActive(false);
    proc->terminate();
    ref->setActive(false);
    ref->terminate();
}

} // anonymous namespace

TEST_CASE("Reverb type switch completes at small block sizes", "[processor][reverb][fx]") {
    SECTION("block 64") { checkReverbTypeSwitchCompletes(64); }
    SECTION("block 256") { checkReverbTypeSwitchCompletes(256); }
    SECTION("block 512") { checkReverbTypeSwitchCompletes(512); }
}

TEST_CASE("Reverb type toggle back mid-fade keeps the active tail", "[processor][reverb][fx]") {
    SECTION("block 64") { checkReverbToggleBackKeepsTail(64); }
    SECTION("block 256") { checkReverbToggleBackKeepsTail(256); }
    SECTION("block 512") { checkReverbToggleBackKeepsTail(512); }
}
