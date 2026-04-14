// ==============================================================================
// Extreme macro automation hardening test (Phase 6, T093 / SC-008)
// Spec: specs/141-membrum-phase6-ui/spec.md (SC-008 audio-thread allocations,
//       edge case: extreme macro automation)
// ==============================================================================
//
// Drive all five macros of pad 1 at audio-block rate (every 128 samples) for
// the equivalent of 10 seconds of audio. Assert:
//   * Zero allocations on the audio thread during the run
//     (AllocationDetector, matches SC-008)
//   * No click artefacts: peak sample <= 2.0 (headroom against macro-driven
//     parameter deltas) and no NaN / Inf samples in the output.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <allocation_detector.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Simple IParamValueQueue implementation that holds one point: (0, value).
class SinglePointQueue : public IParamValueQueue
{
public:
    SinglePointQueue(ParamID id, ParamValue v) : id_(id), value_(v) {}

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override
    {
        if (index != 0) return kInvalidArgument;
        sampleOffset = 0;
        value = value_;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }

private:
    ParamID    id_;
    ParamValue value_;
};

class MultiChangeQueue : public IParameterChanges
{
public:
    MultiChangeQueue() { queues_.reserve(16); }
    void add(ParamID id, ParamValue v) { queues_.emplace_back(id, v); }
    void clear() { queues_.clear(); }

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getParameterCount() override
    {
        return static_cast<int32>(queues_.size());
    }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<std::size_t>(index)];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override
    {
        return nullptr;
    }

private:
    std::vector<SinglePointQueue> queues_;
};

class EmptyEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return count_; }
    tresult PLUGIN_API getEvent(int32, Event& e) override { e = stored_; return kResultOk; }
    tresult PLUGIN_API addEvent(Event& e) override { stored_ = e; count_ = 1; return kResultOk; }
    void clear() { count_ = 0; }
private:
    Event stored_{};
    int32 count_ = 0;
};

} // namespace

// ==============================================================================
// T093 / SC-008: extreme macro automation, zero allocations, no NaN, no clicks.
// ==============================================================================
TEST_CASE("Extreme macro automation at block rate produces clean audio, zero allocs (SC-008)",
          "[macro_mapper][realtime][extreme_automation]")
{
    constexpr int32  kBlockSize  = 128;
    constexpr double kSampleRate = 44100.0;
    constexpr int    kPad        = 1;
    // 10 seconds / (128 samples / 44100 Hz) ~= 3445 blocks.
    const int numBlocks =
        static_cast<int>(kSampleRate * 10.0 / static_cast<double>(kBlockSize));

    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    ProcessSetup setup{};
    setup.processMode          = kRealtime;
    setup.symbolicSampleSize   = kSample32;
    setup.maxSamplesPerBlock   = kBlockSize;
    setup.sampleRate           = kSampleRate;
    REQUIRE(processor.setupProcessing(setup) == kResultOk);

    REQUIRE(processor.setActive(true) == kResultOk);

    // Output buffer scaffolding
    std::vector<float> outL(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kBlockSize), 0.0f);
    float* channels[2] = { outL.data(), outR.data() };

    AudioBusBuffers outputBus{};
    outputBus.numChannels      = 2;
    outputBus.channelBuffers32 = channels;
    outputBus.silenceFlags     = 0;

    EmptyEventList events;
    MultiChangeQueue paramChanges;

    ProcessData data{};
    data.processMode         = kRealtime;
    data.symbolicSampleSize  = kSample32;
    data.numSamples          = kBlockSize;
    data.numOutputs          = 1;
    data.outputs             = &outputBus;
    data.numInputs           = 0;
    data.inputs              = nullptr;
    data.inputEvents         = &events;
    data.outputEvents        = nullptr;
    data.inputParameterChanges = &paramChanges;
    data.outputParameterChanges = nullptr;
    data.processContext      = nullptr;

    const ParamID macroIds[5] = {
        static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadMacroTightness)),
        static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadMacroBrightness)),
        static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadMacroBodySize)),
        static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadMacroPunch)),
        static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadMacroComplexity)),
    };

    // Warm-up: run a handful of full-workload blocks (note-on, param changes,
    // recomputeCouplingMatrix) so every lazy allocation (MacroMapper defaults
    // cache, voice pool first-steal allocations, coupling matrix scratch) is
    // already done before allocation tracking starts.
    Event noteOn{};
    noteOn.type = Event::kNoteOnEvent;
    noteOn.sampleOffset = 0;
    noteOn.noteOn.channel = 0;
    noteOn.noteOn.pitch = 37;  // pad 1
    noteOn.noteOn.velocity = 100.0f / 127.0f;
    noteOn.noteOn.noteId = 37;
    events.addEvent(noteOn);

    for (int b = 0; b < 32; ++b)
    {
        paramChanges.clear();
        for (int m = 0; m < 5; ++m) {
            const double v = 0.5 + 0.1 * (b & 1 ? -1 : 1) * (m + 1) / 5.0;
            paramChanges.add(macroIds[m], v);
        }
        // Periodically retrigger so voice pool stealing paths get warm.
        if ((b % 4) == 0 && b > 0) {
            Event rt{};
            rt.type = Event::kNoteOnEvent;
            rt.sampleOffset = 0;
            rt.noteOn.channel = 0;
            rt.noteOn.pitch = 37;
            rt.noteOn.velocity = 80.0f / 127.0f;
            rt.noteOn.noteId = 1000 + b;
            events.addEvent(rt);
        }
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        events.clear();
    }

    paramChanges.clear();

    // Retrigger the note for the measured run so automation has a fresh live
    // voice to modulate.
    Event retrig{};
    retrig.type = Event::kNoteOnEvent;
    retrig.sampleOffset = 0;
    retrig.noteOn.channel = 0;
    retrig.noteOn.pitch = 37;
    retrig.noteOn.velocity = 100.0f / 127.0f;
    retrig.noteOn.noteId = 4200;
    events.addEvent(retrig);

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    double maxAbsSample = 0.0;
    bool sawNaN = false;
    bool sawInf = false;

    for (int b = 0; b < numBlocks; ++b)
    {
        // Rebuild param changes for every block. Use sine modulation across
        // the [0, 1] range so all 5 macros change every block (no early-out).
        paramChanges.clear();
        const double tNorm = static_cast<double>(b) / static_cast<double>(numBlocks);
        for (int m = 0; m < 5; ++m)
        {
            const double phase = 6.2831853 * (tNorm * (m + 1));
            const double v = 0.5 + 0.499 * std::sin(phase);
            paramChanges.add(macroIds[m], v);
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
        events.clear();

        for (int i = 0; i < kBlockSize; ++i)
        {
            // NaN / Inf check via bit manipulation -- avoids std::isnan which
            // may be stripped under -ffast-math. Per project policy we rely
            // on float-bit inspection; MSVC defaults are fine.
            std::uint32_t bits = 0;
            std::memcpy(&bits, &outL[static_cast<std::size_t>(i)], sizeof(bits));
            const std::uint32_t exp  = (bits >> 23) & 0xFFu;
            const std::uint32_t mant = bits & 0x7FFFFFu;
            if (exp == 0xFFu && mant != 0u) sawNaN = true;
            if (exp == 0xFFu && mant == 0u) sawInf = true;

            const double a = std::abs(static_cast<double>(outL[static_cast<std::size_t>(i)]));
            if (a > maxAbsSample) maxAbsSample = a;
        }
    }

    const std::size_t allocs = detector.stopTracking();

    REQUIRE(processor.setActive(false) == kResultOk);
    processor.terminate();

    // SC-008: zero audio-thread allocations across 10 s of block-rate macro
    // automation.
    REQUIRE(allocs == 0);

    // No NaN / Inf -- macro-driven parameter deltas must not destabilise the
    // modal resonator network.
    REQUIRE_FALSE(sawNaN);
    REQUIRE_FALSE(sawInf);

    // No click artefacts: output stays well below the 2.0f sentinel. Real
    // Membrum output at velocity 100 should be under ~1.2 peak even with
    // macros at the extremes; we assert a generous headroom guard.
    REQUIRE(maxAbsSample < 2.0);
}
