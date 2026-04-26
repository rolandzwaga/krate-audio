// Measurement-only test: render each default-kit pad type at full velocity and
// report the peak output level in dBFS. Tagged [.measure] so it is excluded
// from the default test run; invoke explicitly via the executable filter.
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 48000.0;
constexpr int kRenderBlocks = 376;  // ~2 seconds @ 48 kHz / 256

class NoteEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultOk; }
    void noteOn(int16 midi, float velocity)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = velocity;
        e.noteOn.channel = 0;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

ProcessSetup makeSetup()
{
    ProcessSetup s{};
    s.processMode = kRealtime;
    s.symbolicSampleSize = kSample32;
    s.maxSamplesPerBlock = kBlockSize;
    s.sampleRate = kSampleRate;
    return s;
}

struct PeakResult {
    float peakL;
    float peakR;
    float peakLinear;
    float peakDbfs;
    double rms;
};

PeakResult measurePad(int16 midi, float velocity)
{
    Membrum::Processor processor;
    NoteEventList events;
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    float* outChans[2] = { outL.data(), outR.data() };
    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;
    outBus.silenceFlags = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlockSize;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &events;
    data.outputEvents = nullptr;
    data.processContext = nullptr;

    processor.initialize(nullptr);
    auto setup = makeSetup();
    processor.setupProcessing(setup);
    processor.setActive(true);

    events.noteOn(midi, velocity);

    float peakL = 0.0f, peakR = 0.0f;
    double sumSq = 0.0;
    size_t totalSamples = 0;

    for (int b = 0; b < kRenderBlocks; ++b) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        events.clear();  // only first block carries the noteOn
        for (int s = 0; s < kBlockSize; ++s) {
            const float l = outL[s];
            const float r = outR[s];
            peakL = std::max(peakL, std::abs(l));
            peakR = std::max(peakR, std::abs(r));
            sumSq += static_cast<double>(l) * l + static_cast<double>(r) * r;
            totalSamples += 2;
        }
    }

    processor.setActive(false);
    processor.terminate();

    PeakResult r{};
    r.peakL = peakL;
    r.peakR = peakR;
    r.peakLinear = std::max(peakL, peakR);
    r.peakDbfs = (r.peakLinear > 0.0f)
        ? 20.0f * std::log10(r.peakLinear)
        : -std::numeric_limits<float>::infinity();
    r.rms = std::sqrt(sumSq / std::max<size_t>(1, totalSamples));
    return r;
}

void report(const char* name, int16 midi, const PeakResult& r)
{
    const double rmsDb = (r.rms > 0.0)
        ? 20.0 * std::log10(r.rms)
        : -std::numeric_limits<double>::infinity();
    std::printf("[level] %-18s MIDI=%2d  peak=%7.4f (%+6.2f dBFS)  RMS=%.4f (%+6.2f dB)  L=%.4f R=%.4f\n",
                name, static_cast<int>(midi),
                static_cast<double>(r.peakLinear), static_cast<double>(r.peakDbfs),
                r.rms, rmsDb,
                static_cast<double>(r.peakL), static_cast<double>(r.peakR));
    std::fflush(stdout);
}

} // namespace

TEST_CASE("Default kit output levels: peak dBFS per pad-type at vel=1.0",
          "[.measure][level]")
{
    std::printf("\n=== Default-kit peak levels @ velocity 1.0, 2 s render, 48 kHz ===\n");
    std::fflush(stdout);
    report("Kick (BD)",    36, measurePad(36, 1.0f));
    report("Snare",        38, measurePad(38, 1.0f));
    report("Floor Tom Lo", 41, measurePad(41, 1.0f));
    report("Floor Tom Hi", 43, measurePad(43, 1.0f));
    report("Low Tom",      45, measurePad(45, 1.0f));
    report("Low-Mid Tom",  47, measurePad(47, 1.0f));
    report("Hi-Mid Tom",   48, measurePad(48, 1.0f));
    report("High Tom",     50, measurePad(50, 1.0f));
    report("Closed Hat",   42, measurePad(42, 1.0f));
    report("Open Hat",     46, measurePad(46, 1.0f));
    report("Crash",        49, measurePad(49, 1.0f));
    report("Ride",         51, measurePad(51, 1.0f));

    // No assertion -- this is purely diagnostic. Dummy CHECK so Catch2
    // counts the case as run.
    CHECK(true);
}

TEST_CASE("Default kit output levels: peak dBFS per pad-type at vel=0.5",
          "[.measure][level]")
{
    std::printf("\n=== Default-kit peak levels @ velocity 0.5, 2 s render, 48 kHz ===\n");
    std::fflush(stdout);
    report("Kick (BD)",    36, measurePad(36, 0.5f));
    report("Snare",        38, measurePad(38, 0.5f));
    report("Floor Tom Lo", 41, measurePad(41, 0.5f));
    report("High Tom",     50, measurePad(50, 0.5f));
    report("Closed Hat",   42, measurePad(42, 0.5f));
    CHECK(true);
}
