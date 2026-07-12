// ==============================================================================
// Perceptual render test: assert the DEFAULT KIT pads sound like what they are.
// ==============================================================================
// Renders representative default-kit pads through the full Membrum Processor and
// asserts spectral signatures within tolerance -- NOT merely that outputs differ.
// This catches the class of regression where a pad still produces sound but the
// timbre is wrong (e.g. a kick losing its sub-bass, a hi-hat losing its
// brightness) which pure "output changed" tests miss.
//
// Assertions encode stable perceptual truths of the current default kit, with
// generous margins so legitimate tuning doesn't trip them but a real timbre
// regression does. (Note: the acoustic-SNARE body balance is deliberately NOT
// asserted here -- that is governed by its own dedicated balance tests.)
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "audio_features.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <cmath>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

class NoteEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 i, Event& e) override {
        if (i < 0 || i >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(i)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultOk; }
    void noteOn(int16 midi) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = 1.0f;
        events_.push_back(e);
    }
private:
    std::vector<Event> events_;
};

// Render a single note through a fresh Processor and return the mono features.
Krate::Test::AudioFeatures renderNote(int16 midi, double seconds) {
    Membrum::Processor proc;
    proc.initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSampleRate;
    proc.setupProcessing(setup);
    proc.setActive(true);

    std::vector<float> outL(kBlock), outR(kBlock);
    float* chans[2] = {outL.data(), outR.data()};
    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = chans;

    NoteEventList events;
    events.noteOn(midi);

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.numSamples = kBlock;

    const auto totalSamples = static_cast<size_t>(seconds * kSampleRate);
    std::vector<float> mono;
    mono.reserve(totalSamples);
    size_t done = 0;
    bool first = true;
    while (done < totalSamples) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputEvents = first ? &events : nullptr;
        proc.process(data);
        for (int i = 0; i < kBlock; ++i) mono.push_back(0.5f * (outL[static_cast<size_t>(i)] + outR[static_cast<size_t>(i)]));
        done += static_cast<size_t>(kBlock);
        first = false;
    }
    proc.setActive(false);
    proc.terminate();
    return Krate::Test::extractAudioFeatures(mono, kSampleRate);
}

}  // namespace

TEST_CASE("Default-kit kick is sub-bass dominant", "[membrum][render][perceptual]") {
    auto f = renderNote(36, 0.7);
    INFO("kick features: " << Krate::Test::formatFeatures(f));
    REQUIRE(f.peakDbfs > -40.0);            // audible
    REQUIRE(f.band[0] > 0.30);              // strong 20-100 Hz energy
    REQUIRE(f.centroidHz < 500.0);         // low spectral centroid
}

TEST_CASE("Default-kit hi-hat is bright", "[membrum][render][perceptual]") {
    auto f = renderNote(42, 0.7);
    INFO("hat features: " << Krate::Test::formatFeatures(f));
    REQUIRE(f.peakDbfs > -40.0);
    REQUIRE(f.centroidHz > 2000.0);        // high spectral centroid
    REQUIRE(f.band[0] < 0.05);             // almost no sub-bass
}

TEST_CASE("Default-kit tom is mid-body dominant", "[membrum][render][perceptual]") {
    auto f = renderNote(48, 0.7);
    INFO("tom features: " << Krate::Test::formatFeatures(f));
    REQUIRE(f.peakDbfs > -40.0);
    REQUIRE(f.band[1] > 0.50);             // strong 100-500 Hz body
}
