// ==============================================================================
// Integration Test: Voice Modulation → Filter Cutoff (End-to-End)
// ==============================================================================
// Verifies that per-voice modulation routes (e.g. ENV3 → FilterCutoff) actually
// affect audio output when configured through the processor's voiceRoutes_ and
// processed through the full audio pipeline.
//
// Bug: ENV3 → FilterCutoff route produces no audible change in sound.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "drain_preset_transfer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mock classes (same pattern as processor_audio_test.cpp)
// =============================================================================

namespace {

class MockEventList : public Steinberg::Vst::IEventList {
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

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

class MockParameterChanges : public Steinberg::Vst::IParameterChanges {
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

class MockParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    MockParamValueQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index, Steinberg::int32& sampleOffset,
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

class MockParamChangesWithData : public Steinberg::Vst::IParameterChanges {
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
    std::vector<MockParamValueQueue> queues_;
};

// =============================================================================
// Helper: Send a VoiceModRouteUpdate message to the processor via notify()
// =============================================================================

void sendVoiceModRoute(Ruinae::Processor& proc, int slotIndex,
                       int source, int destination, double amount,
                       int curve = 0, double smoothMs = 0.0,
                       int scale = 2, int bypass = 0, int active = 1)
{
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slotIndex));
    attrs->setInt("source", static_cast<Steinberg::int64>(source));
    attrs->setInt("destination", static_cast<Steinberg::int64>(destination));
    attrs->setFloat("amount", amount);
    attrs->setInt("curve", static_cast<Steinberg::int64>(curve));
    attrs->setFloat("smoothMs", smoothMs);
    attrs->setInt("scale", static_cast<Steinberg::int64>(scale));
    attrs->setInt("bypass", static_cast<Steinberg::int64>(bypass));
    attrs->setInt("active", static_cast<Steinberg::int64>(active));
    proc.notify(msg);
}

// =============================================================================
// Helpers
// =============================================================================

static float computeRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

/// Process multiple blocks, accumulating output into a single buffer.
/// Returns the concatenated left-channel output.
static std::vector<float> processBlocks(
    Ruinae::Processor& processor,
    MockEventList& events,
    Steinberg::Vst::IParameterChanges* paramChanges,
    int numBlocks,
    size_t blockSize = 512)
{
    std::vector<float> accumulated;
    accumulated.reserve(static_cast<size_t>(numBlocks) * blockSize);

    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(blockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = paramChanges;
    data.inputEvents = &events;
    data.processContext = nullptr;

    MockParameterChanges emptyParams;

    for (int b = 0; b < numBlocks; ++b) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        processor.process(data);

        accumulated.insert(accumulated.end(), outL.begin(), outL.end());

        // Clear events after first block (noteOn only needs to be sent once)
        if (b == 0) {
            events.clear();
            data.inputParameterChanges = &emptyParams;
        }
    }

    return accumulated;
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Voice mod route ENV3 -> FilterCutoff affects audio output",
          "[voice-mod][filter][integration]") {
    // =========================================================================
    // Setup: Two identical processors, one with ENV3->FilterCutoff route
    // =========================================================================
    auto initProc = []() {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        proc->setupProcessing(setup);
        proc->setActive(true);
        return proc;
    };

    auto procWithMod = initProc();
    auto procNoMod = initProc();

    // =========================================================================
    // Configure: Set filter cutoff LOW so modulation has room to move it
    // =========================================================================
    // Filter cutoff parameter range: 20-20000 Hz
    // Normalized 0.0 = 20 Hz, 1.0 = 20000 Hz
    // Set cutoff low (~200 Hz) so ENV3 pushing it up is clearly audible
    constexpr double kLowCutoffNorm = 0.1; // ~200 Hz

    // =========================================================================
    // Configure: Add ENV3 → FilterCutoff voice mod route on procWithMod
    // =========================================================================
    // Send ENV3 -> FilterCutoff voice mod route via IMessage (same path as UI)
    sendVoiceModRoute(*procWithMod,
        /*slotIndex=*/0,
        /*source=*/2,        // Env3
        /*destination=*/0,   // FilterCutoff
        /*amount=*/1.0);     // Full positive

    // =========================================================================
    // Process: Set filter cutoff, play a note, process ~200ms of audio
    // =========================================================================
    constexpr int kNumBlocks = 20; // ~232ms at 512 samples/block, 44.1kHz

    // Process with modulation
    std::vector<float> outputWithMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f); // Middle C

        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kLowCutoffNorm);

        outputWithMod = processBlocks(*procWithMod, events, &params, kNumBlocks);
    }

    // Process without modulation (identical except no voice route)
    std::vector<float> outputNoMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f); // Middle C

        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kLowCutoffNorm);

        outputNoMod = processBlocks(*procNoMod, events, &params, kNumBlocks);
    }

    // =========================================================================
    // Verify: Both produce audio
    // =========================================================================
    REQUIRE(hasNonZeroSamples(outputWithMod.data(), outputWithMod.size()));
    REQUIRE(hasNonZeroSamples(outputNoMod.data(), outputNoMod.size()));

    float rmsWithMod = computeRMS(outputWithMod.data(), outputWithMod.size());
    float rmsNoMod = computeRMS(outputNoMod.data(), outputNoMod.size());

    INFO("RMS with ENV3->FilterCutoff mod: " << rmsWithMod);
    INFO("RMS without mod route: " << rmsNoMod);

    // =========================================================================
    // Verify: The modulation route MUST change the output
    // =========================================================================
    // ENV3 at sustain should push the filter cutoff up from ~200 Hz,
    // letting through significantly more harmonics. The RMS of the
    // modulated output should be measurably higher than unmodulated.
    //
    // If this fails, the voice mod route is not being applied.
    // =========================================================================

    // The outputs must be different (modulation has an effect)
    bool outputsDiffer = false;
    for (size_t i = 0; i < outputWithMod.size() && i < outputNoMod.size(); ++i) {
        if (std::abs(outputWithMod[i] - outputNoMod[i]) > 1e-6f) {
            outputsDiffer = true;
            break;
        }
    }

    // PRIMARY ASSERTION: ENV3 -> FilterCutoff route must change the audio
    REQUIRE(outputsDiffer);

    // SECONDARY: Modulated output should have more energy (filter opens up)
    REQUIRE(rmsWithMod > rmsNoMod * 1.1f);
}

TEST_CASE("Voice mod route ENV3 -> FilterCutoff with negative amount darkens sound",
          "[voice-mod][filter][integration]") {
    // Same test but with negative amount: ENV3 should push cutoff DOWN
    auto makeProc = []() {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        proc->setupProcessing(setup);
        proc->setActive(true);
        return proc;
    };

    auto procWithMod = makeProc();
    auto procNoMod = makeProc();

    // High cutoff so negative modulation can pull it down
    constexpr double kHighCutoffNorm = 0.9;

    // Negative amount: ENV3 should close the filter
    sendVoiceModRoute(*procWithMod,
        /*slotIndex=*/0,
        /*source=*/2,        // Env3
        /*destination=*/0,   // FilterCutoff
        /*amount=*/-1.0);    // Full negative

    constexpr int kNumBlocks = 20;

    std::vector<float> outputWithMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kHighCutoffNorm);
        outputWithMod = processBlocks(*procWithMod, events, &params, kNumBlocks);
    }

    std::vector<float> outputNoMod;
    {
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        MockParamChangesWithData params;
        params.addChange(Ruinae::kFilterCutoffId, kHighCutoffNorm);
        outputNoMod = processBlocks(*procNoMod, events, &params, kNumBlocks);
    }

    REQUIRE(hasNonZeroSamples(outputWithMod.data(), outputWithMod.size()));
    REQUIRE(hasNonZeroSamples(outputNoMod.data(), outputNoMod.size()));

    float rmsWithMod = computeRMS(outputWithMod.data(), outputWithMod.size());
    float rmsNoMod = computeRMS(outputNoMod.data(), outputNoMod.size());

    INFO("RMS with negative ENV3->FilterCutoff: " << rmsWithMod);
    INFO("RMS without mod: " << rmsNoMod);

    // Outputs must differ
    bool outputsDiffer = false;
    for (size_t i = 0; i < outputWithMod.size() && i < outputNoMod.size(); ++i) {
        if (std::abs(outputWithMod[i] - outputNoMod[i]) > 1e-6f) {
            outputsDiffer = true;
            break;
        }
    }

    REQUIRE(outputsDiffer);

    // Negative modulation should reduce energy (filter closes)
    REQUIRE(rmsWithMod < rmsNoMod * 0.9f);
}
