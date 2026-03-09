// ==============================================================================
// ADSRDisplay UI Wiring Tests (Spec 124: T045, T046)
// ==============================================================================
// Tests that Controller::createCustomView() returns a non-null ADSRDisplay for
// the "ADSRDisplay" custom-view-name. Also tests that playback state atomics
// exist on the Processor and are updated during note playback.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "ui/adsr_display.h"

#include "vstgui/uidescription/uiattributes.h"

using Catch::Approx;

// ==============================================================================
// T045: Controller::createCustomView() returns non-null for ADSRDisplay
// ==============================================================================

TEST_CASE("Controller::createCustomView returns non-null ADSRDisplay",
          "[adsr][ui][wiring]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // Create UIAttributes with a size (required by createCustomView)
    VSTGUI::UIAttributes attrs;
    attrs.setAttribute("size", "300, 150");

    auto* view = controller.createCustomView(
        "ADSRDisplay", attrs, nullptr, nullptr);

    REQUIRE(view != nullptr);

    // Verify it is actually an ADSRDisplay by dynamic_cast
    auto* adsrDisplay = dynamic_cast<Krate::Plugins::ADSRDisplay*>(view);
    REQUIRE(adsrDisplay != nullptr);

    // Clean up -- VSTGUI views must be manually released when not owned by parent
    view->forget();

    controller.terminate();
}

TEST_CASE("ADSRDisplay receives correct base param IDs after wiring",
          "[adsr][ui][wiring]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    VSTGUI::UIAttributes attrs;
    attrs.setAttribute("size", "300, 150");

    auto* view = controller.createCustomView(
        "ADSRDisplay", attrs, nullptr, nullptr);
    REQUIRE(view != nullptr);

    auto* adsrDisplay = dynamic_cast<Krate::Plugins::ADSRDisplay*>(view);
    REQUIRE(adsrDisplay != nullptr);

    // T045: Verify ADSR base param IDs are set to 720 (A=720, D=721, S=722, R=723)
    REQUIRE(adsrDisplay->getAttackParamId() == Innexus::kAdsrAttackId);
    REQUIRE(adsrDisplay->getDecayParamId() == Innexus::kAdsrDecayId);
    REQUIRE(adsrDisplay->getSustainParamId() == Innexus::kAdsrSustainId);
    REQUIRE(adsrDisplay->getReleaseParamId() == Innexus::kAdsrReleaseId);

    // T045: Verify curve base param IDs are set to 726 (AC=726, DC=727, RC=728)
    REQUIRE(adsrDisplay->getAttackCurveParamId() == Innexus::kAdsrAttackCurveId);
    REQUIRE(adsrDisplay->getDecayCurveParamId() == Innexus::kAdsrDecayCurveId);
    REQUIRE(adsrDisplay->getReleaseCurveParamId() == Innexus::kAdsrReleaseCurveId);

    view->forget();
    controller.terminate();
}

TEST_CASE("Controller::createCustomView returns null for unknown view name",
          "[adsr][ui][wiring]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    VSTGUI::UIAttributes attrs;
    attrs.setAttribute("size", "300, 150");

    auto* view = controller.createCustomView(
        "NonExistentView", attrs, nullptr, nullptr);

    REQUIRE(view == nullptr);

    controller.terminate();
}

// ==============================================================================
// T046: Playback state atomics exist on Processor and are updated
// ==============================================================================

TEST_CASE("Processor has ADSR playback state atomics with correct defaults",
          "[adsr][ui][playback-state]")
{
    Innexus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultOk);

    // Verify default values of playback state atomics
    REQUIRE(processor.getAdsrEnvelopeOutput() == Approx(0.0f));
    REQUIRE(processor.getAdsrStage() == 0);
    REQUIRE(processor.getAdsrActive() == false);

    processor.terminate();
}

// ==============================================================================
// Helpers for T046 playback state test
// ==============================================================================

#include "dsp/sample_analysis.h"
#include <krate/dsp/processors/harmonic_types.h>
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include <vector>
#include <cmath>

namespace {

static Innexus::SampleAnalysis* makePlaybackTestAnalysis()
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;

    for (int f = 0; f < 50; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = 440.0f;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = 0.5f;

        for (int p = 0; p < 4; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = 440.0f * static_cast<float>(p + 1);
            partial.amplitude = 0.5f / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 10;
            partial.phase = 0.0f;
        }
        analysis->frames.push_back(frame);
    }
    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_sample.wav";
    return analysis;
}

// Simple EventList for MIDI events
class T046EventList : public Steinberg::Vst::IEventList
{
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
    { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override
    { return static_cast<Steinberg::int32>(events_.size()); }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
        Steinberg::Vst::Event& e) override
    {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override
    {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }

    void addNoteOn(int16_t pitch, float velocity, Steinberg::int32 sampleOffset = 0)
    {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = pitch;
        e.noteOn.tuning = 0.0f;
        e.noteOn.length = 0;
        events_.push_back(e);
    }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

// Simple parameter change queue for setting parameters
class T046ParamChanges : public Steinberg::Vst::IParameterChanges
{
public:
    class ParamQueue : public Steinberg::Vst::IParamValueQueue
    {
    public:
        ParamQueue(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue val)
            : id_(id), value_(val) {}

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }

        Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
        Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
        Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
            Steinberg::int32& sampleOffset, Steinberg::Vst::ParamValue& value) override
        {
            if (index != 0) return Steinberg::kResultFalse;
            sampleOffset = 0;
            value = value_;
            return Steinberg::kResultTrue;
        }
        Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32,
            Steinberg::Vst::ParamValue, Steinberg::int32&) override
        { return Steinberg::kResultFalse; }

    private:
        Steinberg::Vst::ParamID id_;
        Steinberg::Vst::ParamValue value_;
    };

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
    { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override
    { return static_cast<Steinberg::int32>(queues_.size()); }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override
    {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override
    { return nullptr; }

    void addParam(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value)
    { queues_.emplace_back(id, value); }

private:
    std::vector<ParamQueue> queues_;
};

} // anonymous namespace

// ==============================================================================
// T046: Playback state atomics are updated during note playback
// ==============================================================================

TEST_CASE("Processor ADSR playback state atomics update during note playback",
          "[adsr][ui][playback-state]")
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    Innexus::Processor proc;
    proc.initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 128;
    setup.sampleRate = 44100.0;
    proc.setupProcessing(setup);
    proc.setActive(true);

    // Inject analysis so oscillator has something to play
    auto* analysis = makePlaybackTestAnalysis();
    proc.testInjectAnalysis(analysis);

    // Set Amount=1.0 so ADSR is active
    T046ParamChanges params;
    params.addParam(Innexus::kAdsrAmountId, 1.0);

    // Send note-on
    T046EventList events;
    events.addNoteOn(60, 1.0f);

    // Process a block with note-on + Amount=1.0
    constexpr int32 blockSize = 128;
    std::vector<float> outL(blockSize, 0.0f);
    std::vector<float> outR(blockSize, 0.0f);
    float* outBuffers[2] = {outL.data(), outR.data()};

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outBuffers;

    ProcessData data{};
    data.numSamples = blockSize;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputEvents = &events;
    data.inputParameterChanges = &params;

    proc.process(data);

    // Process several more blocks to let the envelope progress past attack
    for (int b = 0; b < 20; ++b)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        ProcessData data2{};
        data2.numSamples = blockSize;
        data2.numOutputs = 1;
        data2.outputs = &outBus;
        proc.process(data2);
    }

    // Now verify that the playback state atomics were updated:
    // 1. adsrEnvelopeOutput should be > 0 (envelope is outputting gain > 0)
    REQUIRE(proc.getAdsrEnvelopeOutput() > 0.0f);

    // 2. adsrActive should be true (note is playing, envelope is active)
    REQUIRE(proc.getAdsrActive() == true);

    // 3. adsrStage should reflect a non-idle stage (attack=1, decay=2, sustain=3)
    //    After 20 blocks of 128 samples at 44100 Hz with default attack=10ms,
    //    the envelope should be past attack and in decay or sustain.
    //    Stage 0 = Idle, so it should be > 0.
    REQUIRE(proc.getAdsrStage() > 0);

    proc.setActive(false);
    proc.terminate();
}
