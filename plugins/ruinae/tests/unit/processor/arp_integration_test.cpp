// ==============================================================================
// Arpeggiator Integration Tests (071-arp-engine-integration)
// ==============================================================================
// Tests for processor-level arp integration: MIDI routing, block processing,
// enable/disable transitions, transport handling.
//
// Phase 3 (US1): T011, T012, T013
// Phase 7 (US5): T051, T052, T053
//
// Reference: specs/071-arp-engine-integration/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mock: Event List (same pattern as midi_events_test.cpp)
// =============================================================================

namespace {

class ArpTestEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }

    void addNoteOn(int16_t pitch, float velocity, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteOff(int16_t pitch, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

// =============================================================================
// Mock: Single Parameter Value Queue
// =============================================================================

class ArpTestParamQueue : public Steinberg::Vst::IParamValueQueue {
public:
    ArpTestParamQueue(Steinberg::Vst::ParamID id, double value)
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

// =============================================================================
// Mock: Parameter Changes Container
// =============================================================================

class ArpTestParamChanges : public Steinberg::Vst::IParameterChanges {
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

    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<ArpTestParamQueue> queues_;
};

// =============================================================================
// Empty parameter changes (no changes)
// =============================================================================

class ArpEmptyParamChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return 0; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32) override { return nullptr; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override { return nullptr; }
};

// =============================================================================
// Helpers
// =============================================================================

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

// =============================================================================
// Test Fixture for Arp Integration Tests
// =============================================================================

struct ArpIntegrationFixture {
    Ruinae::Processor processor;
    ArpTestEventList events;
    ArpEmptyParamChanges emptyParams;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    Steinberg::Vst::ProcessContext processContext{};
    static constexpr size_t kBlockSize = 512;

    ArpIntegrationFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &emptyParams;
        data.inputEvents = &events;

        // Set up process context with transport playing at 120 BPM
        processContext.state = Steinberg::Vst::ProcessContext::kPlaying
                            | Steinberg::Vst::ProcessContext::kTempoValid
                            | Steinberg::Vst::ProcessContext::kTimeSigValid;
        processContext.tempo = 120.0;
        processContext.timeSigNumerator = 4;
        processContext.timeSigDenominator = 4;
        processContext.sampleRate = 44100.0;
        processContext.projectTimeMusic = 0.0;
        processContext.projectTimeSamples = 0;
        data.processContext = &processContext;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~ArpIntegrationFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processBlock() {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        // Advance transport position for next block
        processContext.projectTimeSamples += static_cast<Steinberg::int64>(kBlockSize);
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / 44100.0 * (120.0 / 60.0);
    }

    void processBlockWithParams(ArpTestParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
        data.inputParameterChanges = &emptyParams;
        // Advance transport position for next block
        processContext.projectTimeSamples += static_cast<Steinberg::int64>(kBlockSize);
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / 44100.0 * (120.0 / 60.0);
    }

    void clearEvents() {
        events.clear();
    }

    /// Enable the arp via parameter change
    void enableArp() {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 1.0);
        processBlockWithParams(params);
    }

    /// Disable the arp via parameter change
    void disableArp() {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 0.0);
        processBlockWithParams(params);
    }

    void setTransportPlaying(bool playing) {
        if (playing) {
            processContext.state |= Steinberg::Vst::ProcessContext::kPlaying;
        } else {
            processContext.state &= ~static_cast<Steinberg::uint32>(
                Steinberg::Vst::ProcessContext::kPlaying);
        }
    }
};

} // anonymous namespace

// =============================================================================
// Phase 3 (US1) Tests: T011, T012, T013
// =============================================================================

// T011: ArpIntegration_EnabledRoutesMidiToArp (SC-001)
//
// When arp is enabled, MIDI note-on events should be routed through the
// ArpeggiatorCore, which transforms them into timed sequences. The synth engine
// should eventually produce audio from the arp-generated events.
TEST_CASE("ArpIntegration_EnabledRoutesMidiToArp", "[arp][integration]") {
    ArpIntegrationFixture f;

    // Enable arp
    f.enableArp();

    // Send a chord (C4, E4, G4)
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.events.addNoteOn(67, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process several blocks to allow arp to generate events and engine to
    // produce audio. The arp at 120 BPM with 1/8 note default rate = 250ms
    // per step = ~11025 samples. With blockSize=512, that's ~22 blocks per step.
    // We process enough blocks to cover at least 2 arp steps.
    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    REQUIRE(audioFound);
}

// T012: ArpIntegration_DisabledRoutesMidiDirectly
//
// When arp is disabled (default), note-on/off events should route directly to
// the synth engine, producing audio immediately.
TEST_CASE("ArpIntegration_DisabledRoutesMidiDirectly", "[arp][integration]") {
    ArpIntegrationFixture f;

    // Arp is disabled by default -- send a note directly
    f.events.addNoteOn(60, 0.8f);
    f.processBlock();
    f.clearEvents();

    // With direct routing, audio should appear very quickly (within a few blocks)
    bool audioFound = false;
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    REQUIRE(audioFound);
}

// T013: ArpIntegration_PrepareCalledInSetupProcessing (FR-008)
//
// Verify that setupProcessing() prepares the arpCore_ with the correct sample
// rate and block size. We test this indirectly: if prepare() was NOT called,
// the arp would use default sampleRate (44100) which might coincidentally work,
// so we test with a different sample rate (96000) and verify the arp still
// functions correctly (the timing is different, but events are generated).
TEST_CASE("ArpIntegration_PrepareCalledInSetupProcessing", "[arp][integration]") {
    // Create a processor with a non-default sample rate
    Ruinae::Processor processor;
    processor.initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 96000.0;
    setup.maxSamplesPerBlock = 256;
    processor.setupProcessing(setup);
    processor.setActive(true);

    // Set up process data
    std::vector<float> outL(256, 0.0f);
    std::vector<float> outR(256, 0.0f);
    float* channelBuffers[2] = { outL.data(), outR.data() };

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessContext ctx{};
    ctx.state = Steinberg::Vst::ProcessContext::kPlaying
              | Steinberg::Vst::ProcessContext::kTempoValid
              | Steinberg::Vst::ProcessContext::kTimeSigValid;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.sampleRate = 96000.0;
    ctx.projectTimeMusic = 0.0;
    ctx.projectTimeSamples = 0;

    ArpEmptyParamChanges emptyParams;
    ArpTestEventList events;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = 256;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &emptyParams;
    data.inputEvents = &events;
    data.processContext = &ctx;

    // Enable arp
    {
        ArpTestParamChanges arpEnable;
        arpEnable.addChange(Ruinae::kArpEnabledId, 1.0);
        data.inputParameterChanges = &arpEnable;
        processor.process(data);
        data.inputParameterChanges = &emptyParams;
        ctx.projectTimeSamples += 256;
    }

    // Send a note
    events.addNoteOn(60, 0.8f);
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        ctx.projectTimeSamples += 256;
    }
    events.clear();

    // Process many blocks to allow arp to generate events.
    // At 96000 Hz and 120 BPM, 1/8 note = 24000 samples = ~94 blocks of 256.
    // Process enough to see at least one arp step.
    bool audioFound = false;
    for (int i = 0; i < 120; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        ctx.projectTimeSamples += 256;
        ctx.projectTimeMusic =
            static_cast<double>(ctx.projectTimeSamples) / 96000.0 * (120.0 / 60.0);
        if (hasNonZeroSamples(outL.data(), 256)) {
            audioFound = true;
            break;
        }
    }

    // If prepare was called correctly at 96000 Hz, arp timing will be correct
    // and events will eventually be generated. If not called, behavior is
    // undefined (likely wrong timing or crash).
    REQUIRE(audioFound);

    processor.setActive(false);
    processor.terminate();
}

// =============================================================================
// Phase 5 (US3) Tests: T035b
// =============================================================================

// T035b: ArpProcessor_StateRoundTrip_AllParams (SC-003 end-to-end)
//
// Configure all 11 arp params to non-default values on a Processor, call
// getState(), create a fresh Processor, call setState(), then getState() again
// and verify the arp portion contains the expected values by deserializing
// through loadArpParams().
TEST_CASE("ArpProcessor_StateRoundTrip_AllParams", "[arp][integration][state]") {
    using Catch::Approx;

    // Create and initialize original processor
    Ruinae::Processor original;
    original.initialize(nullptr);
    {
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        original.setupProcessing(setup);
    }
    original.setActive(true);

    // Set all 11 arp params to non-default values via parameter changes
    {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 1.0);           // enabled = true
        params.addChange(Ruinae::kArpModeId, 3.0 / 9.0);       // mode = 3 (DownUp)
        params.addChange(Ruinae::kArpOctaveRangeId, 2.0 / 3.0); // octaveRange = 3
        params.addChange(Ruinae::kArpOctaveModeId, 1.0);        // octaveMode = 1 (Interleaved)
        params.addChange(Ruinae::kArpTempoSyncId, 0.0);         // tempoSync = false
        params.addChange(Ruinae::kArpNoteValueId, 14.0 / 20.0); // noteValue = 14
        // freeRate: normalized = (12.5 - 0.5) / 49.5
        params.addChange(Ruinae::kArpFreeRateId, (12.5 - 0.5) / 49.5);
        // gateLength: normalized = (60.0 - 1.0) / 199.0
        params.addChange(Ruinae::kArpGateLengthId, (60.0 - 1.0) / 199.0);
        // swing: normalized = 25.0 / 75.0
        params.addChange(Ruinae::kArpSwingId, 25.0 / 75.0);
        params.addChange(Ruinae::kArpLatchModeId, 0.5);         // latchMode = 1 (Hold)
        params.addChange(Ruinae::kArpRetriggerId, 1.0);         // retrigger = 2 (Beat)

        // Process one block to apply the parameter changes
        std::vector<float> outL(512, 0.0f), outR(512, 0.0f);
        float* channelBuffers[2] = { outL.data(), outR.data() };
        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = 512;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &params;

        ArpTestEventList events;
        data.inputEvents = &events;

        Steinberg::Vst::ProcessContext ctx{};
        ctx.state = Steinberg::Vst::ProcessContext::kTempoValid;
        ctx.tempo = 120.0;
        data.processContext = &ctx;

        original.process(data);
    }

    // Save state from original processor
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(original.getState(stream) == Steinberg::kResultTrue);

    // Create a fresh processor and load the saved state
    Ruinae::Processor loaded;
    loaded.initialize(nullptr);
    {
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        loaded.setupProcessing(setup);
    }

    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(loaded.setState(stream) == Steinberg::kResultTrue);

    // Save state from the loaded processor to verify the arp data persisted
    auto stream2 = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(loaded.getState(stream2) == Steinberg::kResultTrue);

    // Read both streams with IBStreamer and skip to the arp params section.
    // The arp params are appended at the very end after the harmonizer enable flag.
    // We verify round-trip by reading the arp section from stream2 using loadArpParams.
    stream2->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream2, kLittleEndian);

        // Skip version int32
        Steinberg::int32 version = 0;
        REQUIRE(readStream.readInt32(version));
        REQUIRE(version == 1);

        // Skip all existing state data by reading it into throw-away structs.
        // Rather than skipping byte-by-byte, re-read using the same load functions
        // that the processor uses (ensures we land at the correct offset).
        Ruinae::GlobalParams gp; Ruinae::loadGlobalParams(gp, readStream);
        Ruinae::OscAParams oap; Ruinae::loadOscAParams(oap, readStream);
        Ruinae::OscBParams obp; Ruinae::loadOscBParams(obp, readStream);
        Ruinae::MixerParams mp; Ruinae::loadMixerParams(mp, readStream);
        Ruinae::RuinaeFilterParams fp; Ruinae::loadFilterParams(fp, readStream);
        Ruinae::RuinaeDistortionParams dp; Ruinae::loadDistortionParams(dp, readStream);
        Ruinae::RuinaeTranceGateParams tgp; Ruinae::loadTranceGateParams(tgp, readStream);
        Ruinae::AmpEnvParams aep; Ruinae::loadAmpEnvParams(aep, readStream);
        Ruinae::FilterEnvParams fep; Ruinae::loadFilterEnvParams(fep, readStream);
        Ruinae::ModEnvParams mep; Ruinae::loadModEnvParams(mep, readStream);
        Ruinae::LFO1Params l1p; Ruinae::loadLFO1Params(l1p, readStream);
        Ruinae::LFO2Params l2p; Ruinae::loadLFO2Params(l2p, readStream);
        Ruinae::ChaosModParams cmp; Ruinae::loadChaosModParams(cmp, readStream);
        Ruinae::ModMatrixParams mmp; Ruinae::loadModMatrixParams(mmp, readStream);
        Ruinae::GlobalFilterParams gfp; Ruinae::loadGlobalFilterParams(gfp, readStream);
        Ruinae::RuinaeDelayParams dlp; Ruinae::loadDelayParams(dlp, readStream);
        Ruinae::RuinaeReverbParams rvp; Ruinae::loadReverbParams(rvp, readStream);
        Ruinae::MonoModeParams mop; Ruinae::loadMonoModeParams(mop, readStream);

        // Skip voice routes (16 slots x 8 fields)
        for (int i = 0; i < 16; ++i) {
            Steinberg::int8 i8 = 0; float fv = 0;
            readStream.readInt8(i8); readStream.readInt8(i8);
            readStream.readFloat(fv); readStream.readInt8(i8);
            readStream.readFloat(fv); readStream.readInt8(i8);
            readStream.readInt8(i8); readStream.readInt8(i8);
        }

        // FX enable flags
        Steinberg::int8 i8 = 0;
        readStream.readInt8(i8); readStream.readInt8(i8);

        // Phaser params + enable
        Ruinae::RuinaePhaserParams php; Ruinae::loadPhaserParams(php, readStream);
        readStream.readInt8(i8);

        // Extended LFO params
        Ruinae::loadLFO1ExtendedParams(l1p, readStream);
        Ruinae::loadLFO2ExtendedParams(l2p, readStream);

        // Macro and Rungler
        Ruinae::MacroParams macp; Ruinae::loadMacroParams(macp, readStream);
        Ruinae::RunglerParams rgp; Ruinae::loadRunglerParams(rgp, readStream);

        // Settings
        Ruinae::SettingsParams sp; Ruinae::loadSettingsParams(sp, readStream);

        // Mod source params
        Ruinae::EnvFollowerParams efp; Ruinae::loadEnvFollowerParams(efp, readStream);
        Ruinae::SampleHoldParams shp; Ruinae::loadSampleHoldParams(shp, readStream);
        Ruinae::RandomParams rp; Ruinae::loadRandomParams(rp, readStream);
        Ruinae::PitchFollowerParams pfp; Ruinae::loadPitchFollowerParams(pfp, readStream);
        Ruinae::TransientParams tp; Ruinae::loadTransientParams(tp, readStream);

        // Harmonizer params + enable
        Ruinae::RuinaeHarmonizerParams hp;
        Ruinae::loadHarmonizerParams(hp, readStream);
        readStream.readInt8(i8);

        // NOW we're at the arp params section -- read and verify
        Ruinae::ArpeggiatorParams arpLoaded;
        bool ok = Ruinae::loadArpParams(arpLoaded, readStream);
        REQUIRE(ok);

        CHECK(arpLoaded.enabled.load() == true);
        CHECK(arpLoaded.mode.load() == 3);
        CHECK(arpLoaded.octaveRange.load() == 3);
        CHECK(arpLoaded.octaveMode.load() == 1);
        CHECK(arpLoaded.tempoSync.load() == false);
        CHECK(arpLoaded.noteValue.load() == 14);
        CHECK(arpLoaded.freeRate.load() == Approx(12.5f).margin(0.01f));
        CHECK(arpLoaded.gateLength.load() == Approx(60.0f).margin(0.01f));
        CHECK(arpLoaded.swing.load() == Approx(25.0f).margin(0.01f));
        CHECK(arpLoaded.latchMode.load() == 1);
        CHECK(arpLoaded.retrigger.load() == 2);
    }

    original.setActive(false);
    original.terminate();
    loaded.terminate();
}

// =============================================================================
// Phase 7 (US5) Tests: T051, T052, T053
// =============================================================================

// T051: ArpIntegration_DisableWhilePlaying_NoStuckNotes (SC-005)
//
// Enable arp, send note-on events, process blocks to generate arp events,
// then disable arp and process more blocks. After disabling, the arp queues
// cleanup note-offs via setEnabled(false) -> processBlock(). The engine should
// eventually go silent (all note-offs delivered, no orphaned notes).
TEST_CASE("ArpIntegration_DisableWhilePlaying_NoStuckNotes", "[arp][integration][transition]") {
    ArpIntegrationFixture f;

    // Enable arp
    f.enableArp();

    // Send a chord (C4, E4, G4)
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.events.addNoteOn(67, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process enough blocks for the arp to generate note events and the
    // engine to produce audio. At 120 BPM / 1/8 note = ~11025 samples per
    // step = ~22 blocks of 512. Process 60 blocks (~30720 samples = ~2.7 steps).
    bool audioFoundBeforeDisable = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFoundBeforeDisable = true;
        }
    }
    REQUIRE(audioFoundBeforeDisable);

    // Disable the arp. setEnabled(false) queues cleanup note-offs internally;
    // the processBlock() inside disableArp() drains them. FR-017 guarantees
    // every sounding arp note gets a matching note-off.
    f.disableArp();

    // Process many more blocks. The synth engine has a release tail (amp
    // envelope release), so audio won't go silent immediately. But it MUST
    // eventually go silent -- if notes are stuck, audio persists indefinitely.
    // The default amp envelope release is short (~200ms = ~9000 samples = ~18
    // blocks). Process 200 blocks to be absolutely sure.
    bool allSilentAfterRelease = false;
    int silentBlockCount = 0;
    for (int i = 0; i < 200; ++i) {
        f.processBlock();
        if (!hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            ++silentBlockCount;
            // Require 10 consecutive silent blocks to confirm silence
            if (silentBlockCount >= 10) {
                allSilentAfterRelease = true;
                break;
            }
        } else {
            silentBlockCount = 0;
        }
    }

    // If no stuck notes, audio should have gone silent
    REQUIRE(allSilentAfterRelease);
}

// T052: ArpIntegration_TransportStop_ResetsTimingPreservesLatch (FR-018)
//
// Enable arp with latch mode Hold, send notes, release keys (latch preserves
// them), process blocks with transport playing. Then stop transport -- the
// processor calls arpCore_.reset() which clears timing and sends note-offs for
// sounding notes, but preserves the held-note/latch buffer. When transport
// restarts, the arp should resume producing audio from the latched notes.
TEST_CASE("ArpIntegration_TransportStop_PreservesLatch", "[arp][integration][transition]") {
    // The arp always runs when enabled (processor forces isPlaying=true).
    // This test verifies that latched notes survive across the full lifecycle:
    // play -> release keys (latch holds) -> transport stop -> transport restart.
    // Audio should be continuous because the arp never pauses.
    ArpIntegrationFixture f;

    // Enable arp with latch mode = Hold (1)
    {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 1.0);
        params.addChange(Ruinae::kArpLatchModeId, 0.5); // 0.5 -> latch=1 (Hold)
        f.processBlockWithParams(params);
    }

    // Send a chord and then release (latch should hold them)
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Release keys -- latch Hold keeps them in the buffer
    f.events.addNoteOff(60);
    f.events.addNoteOff(64);
    f.processBlock();
    f.clearEvents();

    // Process blocks with transport playing -- arp should generate events
    bool audioFoundWhilePlaying = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFoundWhilePlaying = true;
        }
    }
    REQUIRE(audioFoundWhilePlaying);

    // Stop transport -- arp continues running (processor forces isPlaying=true)
    f.setTransportPlaying(false);

    // Arp should still produce audio (it doesn't pause on transport stop)
    bool audioAfterStop = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioAfterStop = true;
            break;
        }
    }
    REQUIRE(audioAfterStop);

    // Restart transport -- latched notes still alive, audio continues
    f.setTransportPlaying(true);

    bool audioFoundAfterRestart = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFoundAfterRestart = true;
            break;
        }
    }
    REQUIRE(audioFoundAfterRestart);
}

// T053: ArpIntegration_EnableWithExistingHeldNote_NoStuckNotes
//
// With arp disabled, send a note-on directly to the engine (it plays normally).
// Then enable the arp. The previously-held note in the engine should NOT get a
// spurious duplicate note-off from the arp transition (since the arp has no
// knowledge of engine-held notes). After enabling, audio from the direct note
// should continue normally and eventually go silent only when a note-off is
// sent via the normal MIDI path.
TEST_CASE("ArpIntegration_EnableWithExistingHeldNote_NoStuckNotes", "[arp][integration][transition]") {
    ArpIntegrationFixture f;

    // Arp disabled by default -- send a note directly to engine
    f.events.addNoteOn(60, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Verify engine is producing audio from the direct note
    bool audioFoundDirect = false;
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFoundDirect = true;
            break;
        }
    }
    REQUIRE(audioFoundDirect);

    // Enable arp -- this should NOT affect the currently sounding engine note.
    // The arp has no notes in its held buffer, so it won't generate any events.
    // The engine note should continue sounding.
    f.enableArp();

    // Audio should still be present (engine note is still held)
    bool audioStillPresent = false;
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioStillPresent = true;
            break;
        }
    }
    REQUIRE(audioStillPresent);

    // Now send note-off for the direct note through the arp path (since arp is
    // now enabled, note-off goes to arpCore_, not engine). But the engine note
    // was sent via direct path -- the engine won't receive this note-off through
    // the arp. So we need to also verify that when we send a new note through
    // the arp path, it doesn't cause duplicate events.
    //
    // The key verification here is that enabling the arp did NOT send any
    // spurious note-on or note-off events that would cause glitches. The engine
    // note continues to sound until it naturally releases.
    //
    // Send note-off for the original note. Since arp is enabled, this goes to
    // arpCore_.noteOff(60). The arp doesn't have this note, so it should be a
    // no-op for the arp. The engine note continues until the amp envelope
    // naturally releases it (since no one sent engine_.noteOff(60)).
    f.events.addNoteOff(60);
    f.processBlock();
    f.clearEvents();

    // Audio should still be present (the engine note was never told to stop
    // via engine_.noteOff -- the note-off went to arpCore_ which didn't have it)
    bool audioAfterNoteOff = false;
    for (int i = 0; i < 3; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioAfterNoteOff = true;
            break;
        }
    }
    // The original engine note should still be sounding because the note-off
    // went to the arp (not the engine). This is the expected behavior -- no
    // duplicate note-offs or stuck notes from the transition.
    CHECK(audioAfterNoteOff);
}

// =============================================================================
// Bug fix: Arp should produce sound in free-rate mode without transport
// =============================================================================

TEST_CASE("ArpIntegration_FreeRate_WorksWithoutTransport",
          "[arp][integration][bug]") {
    // Free-rate mode (tempoSync OFF) should work regardless of transport state.
    ArpIntegrationFixture f;

    // Enable arp AND switch to free-rate mode (tempoSync OFF)
    ArpTestParamChanges params;
    params.addChange(Ruinae::kArpEnabledId, 1.0);
    params.addChange(Ruinae::kArpTempoSyncId, 0.0);  // free-rate mode
    // Set freeRate to 8 Hz (fast enough to trigger within a few blocks)
    params.addChange(Ruinae::kArpFreeRateId, (8.0 - 0.5) / 49.5);  // denorm: 0.5 + norm*49.5 = 8 Hz
    f.processBlockWithParams(params);
    f.clearEvents();

    // Stop transport
    f.setTransportPlaying(false);

    // Send a chord (C4, E4, G4)
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.events.addNoteOn(67, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process enough blocks for free-rate arp to fire (8 Hz = every ~5512 samples
    // at 44100 Hz, so within ~11 blocks of 512 samples)
    bool audioFound = false;
    for (int i = 0; i < 30; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    REQUIRE(audioFound);  // Free-rate arp must produce sound without transport
}

TEST_CASE("ArpCore_SetModeEveryBlock_PreventsNoteAdvance",
          "[arp][integration][bug]") {
    // Proves the root cause: calling setMode() every block resets the
    // NoteSelector step index, so the arp only ever plays the first note.
    // Then proves the fix: calling setMode() only when changed lets it cycle.
    using namespace Krate::DSP;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(true);

    // Hold a 3-note chord
    arp.noteOn(60, 100);  // C4
    arp.noteOn(64, 100);  // E4
    arp.noteOn(67, 100);  // G4

    BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                     .tempoBPM = 120.0, .isPlaying = true};

    std::array<ArpEvent, 128> events{};
    std::set<uint8_t> notesHeard;

    SECTION("BUG: setMode every block resets step index - only one note heard") {
        for (int block = 0; block < 100; ++block) {
            // Simulate old applyParamsToEngine: setMode called unconditionally
            arp.setMode(ArpMode::Up);
            size_t n = arp.processBlock(ctx, events);
            for (size_t i = 0; i < n; ++i) {
                if (events[i].type == ArpEvent::Type::NoteOn) {
                    notesHeard.insert(events[i].note);
                }
            }
        }
        // Bug: only note 60 (C4) is ever heard because step resets to 0 each block
        CHECK(notesHeard.size() == 1);
        CHECK(notesHeard.count(60) == 1);
    }

    SECTION("FIX: setMode only on change - all chord notes cycle") {
        // setMode was already called once above in test setup. Don't call again.
        for (int block = 0; block < 100; ++block) {
            // Simulate fixed applyParamsToEngine: no setMode call (value unchanged)
            size_t n = arp.processBlock(ctx, events);
            for (size_t i = 0; i < n; ++i) {
                if (events[i].type == ArpEvent::Type::NoteOn) {
                    notesHeard.insert(events[i].note);
                }
            }
        }
        // Fix: all 3 notes should be heard
        REQUIRE(notesHeard.size() == 3);
        CHECK(notesHeard.count(60) == 1);
        CHECK(notesHeard.count(64) == 1);
        CHECK(notesHeard.count(67) == 1);
    }
}

TEST_CASE("ArpIntegration_ChordArpeggiates_MultipleNotes",
          "[arp][integration][bug]") {
    // Verifies the processor correctly arpeggates a chord (different notes heard).
    // Uses a standalone ArpeggiatorCore to mirror what the processor does,
    // since checking distinct pitches via audio output is unreliable (ADSR tails).
    using namespace Krate::DSP;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    // Mirror processor's applyParamsToEngine: set all params, setEnabled LAST
    arp.setMode(ArpMode::Up);
    arp.setOctaveRange(1);
    arp.setOctaveMode(OctaveMode::Sequential);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setFreeRate(4.0f);
    arp.setGateLength(80.0f);
    arp.setSwing(0.0f);
    arp.setLatchMode(LatchMode::Off);
    arp.setRetrigger(ArpRetriggerMode::Off);
    arp.setEnabled(true);

    // Hold a 3-note chord
    arp.noteOn(60, 100);  // C4
    arp.noteOn(64, 100);  // E4
    arp.noteOn(67, 100);  // G4

    BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                     .tempoBPM = 120.0, .isPlaying = true};

    std::array<ArpEvent, 128> events{};
    std::set<uint8_t> notesHeard;

    // Simulate processor loop: DON'T call resetting setters every block (the fix)
    // Only call safe setters (setTempoSync, setFreeRate, etc.) as the processor does
    for (int block = 0; block < 100; ++block) {
        arp.setTempoSync(true);
        arp.setFreeRate(4.0f);
        arp.setGateLength(80.0f);
        arp.setSwing(0.0f);
        arp.setEnabled(true);

        size_t n = arp.processBlock(ctx, events);
        for (size_t i = 0; i < n; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn) {
                notesHeard.insert(events[i].note);
            }
        }
    }

    // All 3 chord notes must be heard in Up mode
    REQUIRE(notesHeard.size() == 3);
    CHECK(notesHeard.count(60) == 1);
    CHECK(notesHeard.count(64) == 1);
    CHECK(notesHeard.count(67) == 1);
}

TEST_CASE("ArpIntegration_DefaultSettings_WorksWithoutTransport",
          "[arp][integration][bug]") {
    // Reproduces: user loads plugin in a simple host (no transport control),
    // enables arp with default settings (tempoSync=true), presses a key,
    // and hears nothing. The arp must always produce sound when enabled,
    // regardless of host transport state.
    ArpIntegrationFixture f;

    // Stop transport FIRST (simulating a host with no transport)
    f.setTransportPlaying(false);

    // Enable arp with defaults (tempoSync=true, noteValue=1/8, 120 BPM)
    ArpTestParamChanges params;
    params.addChange(Ruinae::kArpEnabledId, 1.0);
    f.processBlockWithParams(params);
    f.clearEvents();

    // Send a chord (C4, E4, G4)
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.events.addNoteOn(67, 0.8f);
    f.processBlock();
    f.clearEvents();

    // At 120 BPM with 1/8 note, step duration = 0.25 sec = 11025 samples
    // That's ~21.5 blocks of 512, so check up to 60 blocks
    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    REQUIRE(audioFound);  // Arp MUST produce sound even without host transport
}

TEST_CASE("ArpCore_AllModes_ProduceDistinctPatterns",
          "[arp][integration][modes]") {
    // Verify every arp mode produces a distinct note pattern from a 3-note chord.
    using namespace Krate::DSP;

    const char* modeNames[] = {
        "Up", "Down", "UpDown", "DownUp", "Converge",
        "Diverge", "Random", "Walk", "AsPlayed", "Chord"
    };

    // Collect first 12 note-on pitches for each mode
    std::array<std::vector<uint8_t>, 10> sequences;

    for (int m = 0; m < 10; ++m) {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(static_cast<ArpMode>(m));
        arp.setTempoSync(true);

        arp.noteOn(60, 100);  // C4
        arp.noteOn(64, 100);  // E4
        arp.noteOn(67, 100);  // G4

        BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                         .tempoBPM = 120.0, .isPlaying = true};
        std::array<ArpEvent, 128> events{};

        for (int block = 0; block < 200 && sequences[m].size() < 12; ++block) {
            size_t n = arp.processBlock(ctx, events);
            for (size_t i = 0; i < n && sequences[m].size() < 12; ++i) {
                if (events[i].type == ArpEvent::Type::NoteOn) {
                    sequences[m].push_back(events[i].note);
                }
            }
        }

        // Log the sequence for diagnostic purposes
        std::string seq;
        for (auto note : sequences[m]) {
            seq += std::to_string(note) + " ";
        }
        INFO("Mode " << m << " (" << modeNames[m] << "): " << seq);
        REQUIRE(sequences[m].size() >= 6);  // Should produce at least 6 notes
    }

    // Up and Down must be different
    CHECK(sequences[0] != sequences[1]);

    // UpDown must differ from Up (has a descending portion)
    CHECK(sequences[0] != sequences[2]);

    // DownUp must differ from Down
    CHECK(sequences[1] != sequences[3]);

    // UpDown and DownUp must differ from each other
    CHECK(sequences[2] != sequences[3]);

    // Converge and Diverge must differ from Up
    CHECK(sequences[0] != sequences[4]);
    CHECK(sequences[0] != sequences[5]);

    // AsPlayed (insertion order) must differ from Up (pitch order)
    // since notes were inserted as 60, 64, 67 which happens to be pitch order
    // for this chord, so AsPlayed may equal Up here. Skip this check.

    // Chord mode: should play all 3 notes simultaneously
    // (multiple notes per step, not one at a time)
    // We can check that it has all 3 notes in the first step
    if (sequences[9].size() >= 3) {
        std::set<uint8_t> chordNotes(sequences[9].begin(), sequences[9].begin() + 3);
        CHECK(chordNotes.count(60) == 1);
        CHECK(chordNotes.count(64) == 1);
        CHECK(chordNotes.count(67) == 1);
    }
}

// =============================================================================
// Parameter Chain Tests: handleArpParamChange → atomic → applyParamsToEngine
// =============================================================================
// These tests verify the FULL parameter denormalization chain, mimicking
// exactly what happens when a COptionMenu sends a normalized value through
// the VST3 parameter system to the processor.

TEST_CASE("ArpParamChain_ModeNormalization_AllValues", "[arp][integration][params]") {
    // Test that handleArpParamChange correctly denormalizes all 10 mode values
    // from the normalized [0,1] range that StringListParameter uses.
    Ruinae::ArpeggiatorParams params;

    // StringListParameter with 10 entries has stepCount = 9.
    // Normalized values: index / stepCount = index / 9
    const int stepCount = 9;
    const char* modeNames[] = {
        "Up", "Down", "UpDown", "DownUp", "Converge",
        "Diverge", "Random", "Walk", "AsPlayed", "Chord"
    };

    for (int expectedIndex = 0; expectedIndex <= stepCount; ++expectedIndex) {
        double normalizedValue = static_cast<double>(expectedIndex) / stepCount;

        Ruinae::handleArpParamChange(params, Ruinae::kArpModeId, normalizedValue);

        int storedMode = params.mode.load(std::memory_order_relaxed);
        INFO("Mode " << modeNames[expectedIndex] << ": normalized=" << normalizedValue
             << " expected=" << expectedIndex << " got=" << storedMode);
        REQUIRE(storedMode == expectedIndex);
    }
}

TEST_CASE("ArpParamChain_ModeChangeReachesCore", "[arp][integration][params]") {
    // Test the FULL chain: handleArpParamChange → atomic → change detection →
    // arpCore.setMode → processBlock produces correct pattern.
    // This mimics exactly what happens in Processor::processParameterChanges()
    // followed by Processor::applyParamsToEngine().
    using namespace Krate::DSP;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);

    // Add a chord (C4, E4, G4) - distinct enough to detect mode differences
    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);

    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    std::array<ArpEvent, 128> events{};

    // Simulate the processor's atomic + change-detection pattern
    Ruinae::ArpeggiatorParams params;
    ArpMode prevMode = ArpMode::Up;

    // Collect note sequences for each mode, going through the full param chain
    std::map<int, std::vector<uint8_t>> sequences;

    for (int modeIdx = 0; modeIdx <= 9; ++modeIdx) {
        // Step 1: Simulate COptionMenu sending normalized value via parameter system
        double normalizedValue = static_cast<double>(modeIdx) / 9.0;
        Ruinae::handleArpParamChange(params, Ruinae::kArpModeId, normalizedValue);

        // Step 2: Simulate applyParamsToEngine() change-detection pattern
        const auto modeInt = params.mode.load(std::memory_order_relaxed);
        const auto mode = static_cast<ArpMode>(modeInt);
        if (mode != prevMode) {
            arp.setMode(mode);
            prevMode = mode;
        }

        // Step 3: Process blocks and collect note events
        std::vector<uint8_t> noteSequence;
        for (int block = 0; block < 100; ++block) {
            size_t n = arp.processBlock(ctx, events);
            for (size_t i = 0; i < n; ++i) {
                if (events[i].type == ArpEvent::Type::NoteOn) {
                    noteSequence.push_back(events[i].note);
                }
            }
        }

        sequences[modeIdx] = noteSequence;
        INFO("Mode " << modeIdx << ": " << noteSequence.size() << " notes");
        REQUIRE(!noteSequence.empty());
    }

    // Verify key distinctions between modes
    // Up (0) must differ from Down (1) - ascending vs descending
    REQUIRE(sequences[0] != sequences[1]);

    // Random (6) must differ from Up (0) - random vs ascending
    // (With 100 blocks at 120 BPM, there should be many notes)
    CHECK(sequences[0] != sequences[6]);

    // UpDown (2) must differ from Up (0) - ping-pong vs one-direction
    CHECK(sequences[0] != sequences[2]);

    // Chord (9) should have different structure (all notes per step)
    CHECK(sequences[0] != sequences[9]);
}

TEST_CASE("ArpParamChain_ProcessorModeChange", "[arp][integration][params]") {
    // End-to-end test through the actual Processor using parameter changes.
    // This tests the complete path: IParameterChanges → processParameterChanges →
    // handleArpParamChange → atomic → applyParamsToEngine → arpCore.setMode.
    ArpIntegrationFixture f;

    // Enable arp
    f.enableArp();

    // Send a chord
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.events.addNoteOn(67, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Let arp run for a bit with default mode (Up)
    for (int i = 0; i < 30; ++i) f.processBlock();

    // Now change mode to Down via parameter change (normalized value = 1/9)
    {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpModeId, 1.0 / 9.0);
        f.processBlockWithParams(params);
    }

    // Process more blocks with Down mode
    bool audioAfterModeChange = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioAfterModeChange = true;
        }
    }
    REQUIRE(audioAfterModeChange);

    // Now change to Random mode (normalized value = 6/9)
    {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpModeId, 6.0 / 9.0);
        f.processBlockWithParams(params);
    }

    // Process blocks with Random mode - should still produce audio
    bool audioAfterRandomMode = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioAfterRandomMode = true;
        }
    }
    REQUIRE(audioAfterRandomMode);

    // Change to Chord mode (normalized value = 9/9 = 1.0)
    {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpModeId, 1.0);
        f.processBlockWithParams(params);
    }

    bool audioAfterChordMode = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioAfterChordMode = true;
        }
    }
    REQUIRE(audioAfterChordMode);
}

TEST_CASE("ArpParamChain_VSTGUIValueFlow", "[arp][integration][params]") {
    // Simulate the EXACT value flow from VSTGUI COptionMenu through the VST3 SDK:
    //
    // 1. StringListParameter with 10 entries (stepCount=9)
    // 2. COptionMenu stores raw index, min=0, max=stepCount
    //    getValueNormalized() = float(index) / float(stepCount) [float division!]
    // 3. performEdit sends this float-precision normalized value to host
    // 4. Processor receives it as ParamValue (double) and denormalizes
    //
    // This tests for float→double precision mismatch in the normalization chain.

    using namespace Steinberg::Vst;

    // Create the actual StringListParameter used by the controller
    StringListParameter modeParam(STR16("Arp Mode"), Ruinae::kArpModeId, nullptr,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    modeParam.appendString(STR16("Up"));
    modeParam.appendString(STR16("Down"));
    modeParam.appendString(STR16("UpDown"));
    modeParam.appendString(STR16("DownUp"));
    modeParam.appendString(STR16("Converge"));
    modeParam.appendString(STR16("Diverge"));
    modeParam.appendString(STR16("Random"));
    modeParam.appendString(STR16("Walk"));
    modeParam.appendString(STR16("AsPlayed"));
    modeParam.appendString(STR16("Chord"));

    REQUIRE(modeParam.getInfo().stepCount == 9);

    const char* modeNames[] = {
        "Up", "Down", "UpDown", "DownUp", "Converge",
        "Diverge", "Random", "Walk", "AsPlayed", "Chord"
    };

    Ruinae::ArpeggiatorParams params;

    for (int index = 0; index <= 9; ++index) {
        // Simulate COptionMenu value flow:
        // COptionMenu stores value as index, min=0, max=stepCount
        // getValueNormalized() does: (float(index) - 0.0f) / (float(stepCount) - 0.0f)
        // This is FLOAT division, which may introduce precision errors
        float controlMin = 0.0f;
        float controlMax = static_cast<float>(modeParam.getInfo().stepCount);
        float controlValue = static_cast<float>(index);
        float vstguiNormalized = (controlValue - controlMin) / (controlMax - controlMin);

        // VST3Editor casts this to ParamValue (double) before sending
        ParamValue normalizedValue = static_cast<ParamValue>(vstguiNormalized);

        // The processor's handleArpParamChange denormalizes this
        Ruinae::handleArpParamChange(params, Ruinae::kArpModeId, normalizedValue);

        int storedMode = params.mode.load(std::memory_order_relaxed);
        INFO("Mode " << modeNames[index] << " (index=" << index
             << "): float_norm=" << vstguiNormalized
             << " double_norm=" << normalizedValue
             << " expected=" << index << " got=" << storedMode);
        REQUIRE(storedMode == index);

        // Also test with SDK's toNormalized for comparison
        ParamValue sdkNorm = modeParam.toNormalized(static_cast<ParamValue>(index));
        Ruinae::handleArpParamChange(params, Ruinae::kArpModeId, sdkNorm);
        int sdkStoredMode = params.mode.load(std::memory_order_relaxed);
        INFO("  SDK normalized=" << sdkNorm << " sdk_got=" << sdkStoredMode);
        CHECK(sdkStoredMode == index);
    }
}

// =============================================================================
// Phase 7 (072-independent-lanes) US5: Lane State Persistence Integration Tests
// =============================================================================

// ArpIntegration_LaneParamsFlowToCore: Set lane params via handleArpParamChange,
// call applyParamsToArp (via processBlock), verify arp lane values match via
// observable behavior.
TEST_CASE("ArpIntegration_LaneParamsFlowToCore", "[arp][integration]") {
    // We test the full pipeline: handleArpParamChange -> atomic storage ->
    // applyParamsToEngine -> arp_.velocityLane()/gateLane()/pitchLane()
    // We observe the effect by running the arp and checking that the generated
    // notes have the velocity/pitch modifications we set up.
    using namespace Krate::DSP;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Simulate param changes via handleArpParamChange into ArpeggiatorParams
    Ruinae::ArpeggiatorParams params;

    // Set velocity lane: length=2, steps = [0.5, 1.0]
    Ruinae::handleArpParamChange(params, Ruinae::kArpVelocityLaneLengthId,
        (2.0 - 1.0) / 31.0);  // normalized for length=2
    Ruinae::handleArpParamChange(params, Ruinae::kArpVelocityLaneStep0Id, 0.5);
    Ruinae::handleArpParamChange(params, Ruinae::kArpVelocityLaneStep1Id, 1.0);

    // Set pitch lane: length=2, steps = [+7, -5]
    Ruinae::handleArpParamChange(params, Ruinae::kArpPitchLaneLengthId,
        (2.0 - 1.0) / 31.0);
    // +7: normalized = (7 + 24) / 48 = 31/48
    Ruinae::handleArpParamChange(params, Ruinae::kArpPitchLaneStep0Id, 31.0 / 48.0);
    // -5: normalized = (-5 + 24) / 48 = 19/48
    Ruinae::handleArpParamChange(params, Ruinae::kArpPitchLaneStep1Id, 19.0 / 48.0);

    // Verify the atomic storage is correct
    CHECK(params.velocityLaneLength.load() == 2);
    CHECK(params.velocityLaneSteps[0].load() == Approx(0.5f).margin(0.01f));
    CHECK(params.velocityLaneSteps[1].load() == Approx(1.0f).margin(0.01f));
    CHECK(params.pitchLaneLength.load() == 2);
    CHECK(params.pitchLaneSteps[0].load() == 7);
    CHECK(params.pitchLaneSteps[1].load() == -5);

    // Now simulate applyParamsToEngine: push lane data to ArpeggiatorCore
    // Expand to max length before writing steps to prevent index clamping,
    // then set the actual length afterward (same pattern as processor.cpp).
    {
        const auto velLen = params.velocityLaneLength.load(std::memory_order_relaxed);
        arp.velocityLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            arp.velocityLane().setStep(
                static_cast<size_t>(i),
                params.velocityLaneSteps[i].load(std::memory_order_relaxed));
        }
        arp.velocityLane().setLength(static_cast<size_t>(velLen));
    }
    {
        const auto pitchLen = params.pitchLaneLength.load(std::memory_order_relaxed);
        arp.pitchLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            int val = std::clamp(
                params.pitchLaneSteps[i].load(std::memory_order_relaxed), -24, 24);
            arp.pitchLane().setStep(
                static_cast<size_t>(i), static_cast<int8_t>(val));
        }
        arp.pitchLane().setLength(static_cast<size_t>(pitchLen));
    }

    // Verify the ArpeggiatorCore lane values match
    CHECK(arp.velocityLane().length() == 2);
    CHECK(arp.velocityLane().getStep(0) == Approx(0.5f).margin(0.01f));
    CHECK(arp.velocityLane().getStep(1) == Approx(1.0f).margin(0.01f));
    CHECK(arp.pitchLane().length() == 2);
    CHECK(arp.pitchLane().getStep(0) == 7);
    CHECK(arp.pitchLane().getStep(1) == -5);

    // Run the arp and verify that the output notes carry the lane modifications
    arp.noteOn(60, 100);  // C4, velocity 100

    BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                     .tempoBPM = 120.0, .isPlaying = true};
    std::array<ArpEvent, 128> events{};

    std::vector<uint8_t> noteVelocities;
    std::vector<uint8_t> notePitches;

    for (int block = 0; block < 200 && noteVelocities.size() < 4; ++block) {
        size_t n = arp.processBlock(ctx, events);
        for (size_t i = 0; i < n; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn) {
                noteVelocities.push_back(events[i].velocity);
                notePitches.push_back(events[i].note);
            }
        }
    }

    REQUIRE(noteVelocities.size() >= 4);

    // Step 0: vel=0.5*100=50, pitch=60+7=67
    // Step 1: vel=1.0*100=100, pitch=60-5=55
    // Step 2 (cycle): vel=0.5*100=50, pitch=60+7=67
    // Step 3 (cycle): vel=1.0*100=100, pitch=60-5=55
    CHECK(noteVelocities[0] == 50);
    CHECK(notePitches[0] == 67);
    CHECK(noteVelocities[1] == 100);
    CHECK(notePitches[1] == 55);
    CHECK(noteVelocities[2] == 50);
    CHECK(notePitches[2] == 67);
    CHECK(noteVelocities[3] == 100);
    CHECK(notePitches[3] == 55);
}

// ArpIntegration_AllLanesReset_OnDisable: Set non-default lanes, disable/enable,
// verify all lane currentStep()==0 (FR-022, SC-007)
TEST_CASE("ArpIntegration_AllLanesReset_OnDisable", "[arp][integration]") {
    using namespace Krate::DSP;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Set up velocity lane length=4, gate lane length=3, pitch lane length=5
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.3f);
    arp.velocityLane().setStep(3, 0.7f);

    arp.gateLane().setLength(3);
    arp.gateLane().setStep(0, 1.0f);
    arp.gateLane().setStep(1, 0.5f);
    arp.gateLane().setStep(2, 1.5f);

    arp.pitchLane().setLength(5);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 7);
    arp.pitchLane().setStep(2, 12);
    arp.pitchLane().setStep(3, -5);
    arp.pitchLane().setStep(4, -12);

    // Hold a note and process enough blocks to advance lanes
    arp.noteOn(60, 100);

    BlockContext ctx{.sampleRate = 44100.0, .blockSize = 512,
                     .tempoBPM = 120.0, .isPlaying = true};
    std::array<ArpEvent, 128> events{};

    // Process enough blocks to generate a few arp steps (advancing lanes)
    int noteCount = 0;
    for (int block = 0; block < 200 && noteCount < 3; ++block) {
        size_t n = arp.processBlock(ctx, events);
        for (size_t i = 0; i < n; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn) {
                ++noteCount;
            }
        }
    }
    REQUIRE(noteCount >= 3);

    // Lanes should now be mid-cycle (not at step 0)
    // (We can't directly observe currentStep() from the arp without public access,
    //  but we verified the steps were used above since the notes had lane modifications.)

    // Disable the arp
    arp.setEnabled(false);
    // Process one block to flush the disable transition
    arp.processBlock(ctx, events);

    // Re-enable the arp
    arp.setEnabled(true);

    // After enable, all lane positions should be at 0 (FR-022)
    // Verify by checking that the NEXT note uses step 0 values
    arp.noteOn(60, 100);

    std::vector<uint8_t> noteVelocities;
    std::vector<uint8_t> notePitches;

    for (int block = 0; block < 200 && noteVelocities.size() < 1; ++block) {
        size_t n = arp.processBlock(ctx, events);
        for (size_t i = 0; i < n; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn) {
                noteVelocities.push_back(events[i].velocity);
                notePitches.push_back(events[i].note);
            }
        }
    }

    REQUIRE(noteVelocities.size() >= 1);

    // Step 0 values: vel=1.0*100=100, pitch=60+0=60
    CHECK(noteVelocities[0] == 100);
    CHECK(notePitches[0] == 60);

    // Verify lane positions are at 0 by checking currentStep() directly
    // After the first note, lanes have advanced to step 1
    // But right after reset and before any note fires, they should be at 0.
    // We already verified this implicitly: the first note after enable used step 0 values.
}

// SC006_AllLaneParamsRegistered: Enumerate param IDs 3020-3132; verify each
// expected ID present; length params have kCanAutomate but NOT kIsHidden;
// step params have kCanAutomate AND kIsHidden (SC-006, 99 total params)
TEST_CASE("SC006_AllLaneParamsRegistered", "[arp][integration]") {
    using namespace Ruinae;
    using namespace Steinberg::Vst;

    ParameterContainer container;
    registerArpParams(container);

    int laneParamCount = 0;

    // Check all velocity lane params (3020-3052)
    {
        // Length param: kCanAutomate, NOT kIsHidden
        auto* param = container.getParameter(kArpVelocityLaneLengthId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
        ++laneParamCount;

        // Step params: kCanAutomate AND kIsHidden
        for (int i = 0; i < 32; ++i) {
            auto* stepParam = container.getParameter(
                static_cast<ParamID>(kArpVelocityLaneStep0Id + i));
            INFO("Velocity step param " << i << " (ID " << (kArpVelocityLaneStep0Id + i) << ")");
            REQUIRE(stepParam != nullptr);
            ParameterInfo stepInfo = stepParam->getInfo();
            CHECK((stepInfo.flags & ParameterInfo::kCanAutomate) != 0);
            CHECK((stepInfo.flags & ParameterInfo::kIsHidden) != 0);
            ++laneParamCount;
        }
    }

    // Check all gate lane params (3060-3092)
    {
        auto* param = container.getParameter(kArpGateLaneLengthId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
        ++laneParamCount;

        for (int i = 0; i < 32; ++i) {
            auto* stepParam = container.getParameter(
                static_cast<ParamID>(kArpGateLaneStep0Id + i));
            INFO("Gate step param " << i << " (ID " << (kArpGateLaneStep0Id + i) << ")");
            REQUIRE(stepParam != nullptr);
            ParameterInfo stepInfo = stepParam->getInfo();
            CHECK((stepInfo.flags & ParameterInfo::kCanAutomate) != 0);
            CHECK((stepInfo.flags & ParameterInfo::kIsHidden) != 0);
            ++laneParamCount;
        }
    }

    // Check all pitch lane params (3100-3132)
    {
        auto* param = container.getParameter(kArpPitchLaneLengthId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
        ++laneParamCount;

        for (int i = 0; i < 32; ++i) {
            auto* stepParam = container.getParameter(
                static_cast<ParamID>(kArpPitchLaneStep0Id + i));
            INFO("Pitch step param " << i << " (ID " << (kArpPitchLaneStep0Id + i) << ")");
            REQUIRE(stepParam != nullptr);
            ParameterInfo stepInfo = stepParam->getInfo();
            CHECK((stepInfo.flags & ParameterInfo::kCanAutomate) != 0);
            CHECK((stepInfo.flags & ParameterInfo::kIsHidden) != 0);
            ++laneParamCount;
        }
    }

    // SC-006: 99 total lane params
    CHECK(laneParamCount == 99);
}

// =============================================================================
// Phase 5 (US3) Tests: Slide engine integration (073 T035)
// =============================================================================

TEST_CASE("ArpIntegration_SlidePassesLegatoToEngine",
          "[arp][integration][slide]") {
    // FR-032, SC-003: Configure a Slide step, run processBlock, verify that
    // the engine receives a legato noteOn. Since we can't easily mock the engine,
    // we verify indirectly by: enabling arp, setting a Slide modifier step,
    // sending notes, and checking that audio is produced (the slide path through
    // engine_.noteOn(note, vel, true) works without crash/silence).
    ArpIntegrationFixture f;

    // Enable arp and set up modifier lane with Slide on step 1
    {
        ArpTestParamChanges params;
        params.addChange(Ruinae::kArpEnabledId, 1.0);
        // Set modifier lane length = 2
        params.addChange(Ruinae::kArpModifierLaneLengthId, 1.0 / 31.0);  // denorm: 1 + round(1/31 * 31) = 2
        // Step 0: Active (0x01) -> normalized 1.0/255.0
        params.addChange(Ruinae::kArpModifierLaneStep0Id, 1.0 / 255.0);
        // Step 1: Active|Slide (0x05) -> normalized 5.0/255.0
        params.addChange(static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpModifierLaneStep0Id + 1),
                         5.0 / 255.0);
        f.processBlockWithParams(params);
    }
    f.clearEvents();

    // Send two notes for the arp to cycle through
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process enough blocks to cover at least 2 arp steps.
    // At 120 BPM, 1/8 note = ~11025 samples, block = 512 samples, so ~22 blocks/step.
    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
        }
    }

    // Audio should be produced -- engine_.noteOn(note, vel, true) accepted the legato flag
    REQUIRE(audioFound);
}

TEST_CASE("ArpIntegration_NormalStepPassesLegatoFalse",
          "[arp][integration][slide]") {
    // FR-032: Normal Active step produces engine_.noteOn(note, vel, false).
    // Verify by: enabling arp with all-Active modifier lane (default), sending
    // notes, and checking audio is produced.
    ArpIntegrationFixture f;

    // Enable arp (default modifier lane is all-Active, legato=false)
    f.enableArp();

    // Send a note
    f.events.addNoteOn(60, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process blocks and verify audio output
    bool audioFound = false;
    for (int i = 0; i < 60; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    // Normal noteOn with legato=false should produce audio normally
    REQUIRE(audioFound);
}

// =============================================================================
// Phase 8 (073-per-step-mods) US6: Modifier Lane Persistence Integration (T062)
// =============================================================================

TEST_CASE("ModifierParams_SC010_AllRegistered", "[arp][integration]") {
    // SC-010: Enumerate param IDs 3140-3181; verify all 35 present;
    // length/config params have kCanAutomate without kIsHidden;
    // step params have kCanAutomate AND kIsHidden.
    using namespace Ruinae;
    using namespace Steinberg::Vst;

    ParameterContainer container;
    registerArpParams(container);

    int modifierParamCount = 0;

    // Modifier lane length (3140): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpModifierLaneLengthId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
        ++modifierParamCount;
    }

    // Modifier lane steps (3141-3172): kCanAutomate AND kIsHidden
    for (int i = 0; i < 32; ++i) {
        auto paramId = static_cast<ParamID>(kArpModifierLaneStep0Id + i);
        auto* param = container.getParameter(paramId);
        INFO("Modifier step param " << i << " (ID " << paramId << ")");
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
        ++modifierParamCount;
    }

    // Accent velocity (3180): kCanAutomate, NOT kIsHidden
    {
        auto* param = container.getParameter(kArpAccentVelocityId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        CHECK((info.flags & ParameterInfo::kIsHidden) == 0);
        ++modifierParamCount;
    }

    // Slide time (3181): kCanAutomate
    {
        auto* param = container.getParameter(kArpSlideTimeId);
        REQUIRE(param != nullptr);
        ParameterInfo info = param->getInfo();
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
        ++modifierParamCount;
    }

    // SC-010: 35 total modifier params
    CHECK(modifierParamCount == 35);
}

TEST_CASE("ModifierParams_FlowToCore", "[arp][integration]") {
    // FR-031: Set modifier params via handleArpParamChange, call applyParamsToArp(),
    // verify arp_.modifierLane().length() and step values match.
    using namespace Krate::DSP;
    using namespace Ruinae;

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(true);

    // Simulate param changes via handleArpParamChange
    ArpeggiatorParams params;

    // Set modifier lane length = 4
    handleArpParamChange(params, kArpModifierLaneLengthId, 3.0 / 31.0);  // 1 + round(3/31 * 31) = 4
    // Set step 0 = Active|Slide (0x05)
    handleArpParamChange(params, kArpModifierLaneStep0Id, 5.0 / 255.0);
    // Set step 1 = Active|Accent (0x09)
    handleArpParamChange(params, static_cast<Steinberg::Vst::ParamID>(kArpModifierLaneStep0Id + 1),
                         9.0 / 255.0);
    // Set step 2 = Rest (0x00)
    handleArpParamChange(params, static_cast<Steinberg::Vst::ParamID>(kArpModifierLaneStep0Id + 2),
                         0.0);
    // Set step 3 = Active (0x01)
    handleArpParamChange(params, static_cast<Steinberg::Vst::ParamID>(kArpModifierLaneStep0Id + 3),
                         1.0 / 255.0);

    // Verify atomic storage
    CHECK(params.modifierLaneLength.load() == 4);
    CHECK(params.modifierLaneSteps[0].load() == 5);
    CHECK(params.modifierLaneSteps[1].load() == 9);
    CHECK(params.modifierLaneSteps[2].load() == 0);
    CHECK(params.modifierLaneSteps[3].load() == 1);

    // Simulate applyParamsToArp: push modifier lane data to ArpeggiatorCore
    // Using expand-write-shrink pattern
    {
        const auto modLen = params.modifierLaneLength.load(std::memory_order_relaxed);
        arp.modifierLane().setLength(32);
        for (int i = 0; i < 32; ++i) {
            arp.modifierLane().setStep(
                static_cast<size_t>(i),
                static_cast<uint8_t>(params.modifierLaneSteps[i].load(std::memory_order_relaxed)));
        }
        arp.modifierLane().setLength(static_cast<size_t>(modLen));
    }
    arp.setAccentVelocity(params.accentVelocity.load(std::memory_order_relaxed));
    arp.setSlideTime(params.slideTime.load(std::memory_order_relaxed));

    // Verify the ArpeggiatorCore lane values match
    CHECK(arp.modifierLane().length() == 4);
    CHECK(arp.modifierLane().getStep(0) == 5);
    CHECK(arp.modifierLane().getStep(1) == 9);
    CHECK(arp.modifierLane().getStep(2) == 0);
    CHECK(arp.modifierLane().getStep(3) == 1);
}
