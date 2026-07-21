// ==============================================================================
// Audition silence reporting + tempo validity (Gradus audit F18, F19)
// ==============================================================================
// F18 -- process() cleared the output buffers, rendered the audition voice only
// when auditionEnabled_, but then set silenceFlags from auditionVoice_.isActive()
// unconditionally. AuditionVoice::active_ only clears from inside processBlock,
// so switching audition off mid-envelope froze the voice "active" forever and
// the plugin reported a non-silent buffer that was in fact all zeros.
//
// F19 -- blockCtx.tempoBPM was taken straight from processContext->tempo, and
// the `tempo > 0 ? tempo : 120` fallback only ran inside the !transportPlaying
// branch. A host reporting kPlaying with tempo 0 therefore left tempoBPM at 0,
// and synced step math divides 60.0 / tempoBPM.
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

#include "vst_param_changes.h"
#include "vst_event_list.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Gradus;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;

struct Harness {
    std::unique_ptr<Gradus::Processor> proc;

    std::vector<float> outL{static_cast<size_t>(kBlockSize), 0.0f};
    std::vector<float> outR{static_cast<size_t>(kBlockSize), 0.0f};
    float* channels[2]{};
    AudioBusBuffers outputBus{};

    Krate::Test::EventList inEvents;
    Krate::Test::EventList outEvents;
    Krate::Test::ParameterChanges noParams;

    ProcessContext ctx{};
    ProcessData data{};

    Harness()
    {
        proc = std::make_unique<Gradus::Processor>();
        proc->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);

        outL.assign(static_cast<size_t>(kBlockSize), 0.0f);
        outR.assign(static_cast<size_t>(kBlockSize), 0.0f);
        channels[0] = outL.data();
        channels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;

        ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid
                  | ProcessContext::kTimeSigValid;
        ctx.tempo = 120.0;
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;
        ctx.sampleRate = kSampleRate;
        ctx.projectTimeSamples = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &inEvents;
        data.outputEvents = &outEvents;
        data.processContext = &ctx;
    }

    ~Harness()
    {
        proc->setActive(false);
        proc->terminate();
    }

    void noteOn(int16 pitch)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = 100.0f / 127.0f;
        e.noteOn.noteId = -1;
        inEvents.addEvent(e);
    }

    /// Run one block; returns the number of MIDI events emitted.
    int runBlock(IParameterChanges* params = nullptr)
    {
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        outputBus.silenceFlags = 0;
        data.inputParameterChanges = params ? params : &noParams;
        proc->process(data);
        inEvents.clear();
        ctx.projectTimeSamples += kBlockSize;
        return static_cast<int>(outEvents.getEventCount());
    }

    [[nodiscard]] bool buffersAreSilent() const
    {
        for (int i = 0; i < kBlockSize; ++i) {
            if (outL[static_cast<size_t>(i)] != 0.0f) return false;
            if (outR[static_cast<size_t>(i)] != 0.0f) return false;
        }
        return true;
    }
};

}  // namespace

TEST_CASE("silenceFlags reports silent after audition is disabled mid-note",
          "[processor][gradus][audition][F18]")
{
    Harness h;

    Krate::Test::ParameterChanges enableAudition;
    enableAudition.add(kArpTempoSyncId, 1.0);
    enableAudition.add(kArpNoteValueId, 16.0 / 20.0);  // 1/2 note -- long envelope
    enableAudition.add(kAuditionEnabledId, 1.0);
    enableAudition.add(kAuditionDecayId, 1.0);          // slowest decay

    h.noteOn(60);
    h.runBlock(&enableAudition);
    h.runBlock();

    // Sanity: with audition on and a note sounding, the buffer is non-silent.
    REQUIRE(h.outputBus.silenceFlags == 0);
    REQUIRE_FALSE(h.buffersAreSilent());

    // Switch audition off mid-envelope. The voice stops being rendered, so the
    // buffers are all zeros from here on.
    Krate::Test::ParameterChanges disableAudition;
    disableAudition.add(kAuditionEnabledId, 0.0);
    h.runBlock(&disableAudition);
    h.runBlock();

    INFO("buffers silent: " << h.buffersAreSilent()
         << ", silenceFlags: " << h.outputBus.silenceFlags);
    REQUIRE(h.buffersAreSilent());
    REQUIRE(h.outputBus.silenceFlags == 0x3);
}

TEST_CASE("kPlaying with tempo 0 still yields sane synced step timing",
          "[processor][gradus][transport][F19]")
{
    // A host reporting kPlaying with tempo == 0 must not produce a degenerate
    // step duration. Compare event traffic against the same run at a valid
    // tempo: with the fallback applied the two must be in the same ballpark;
    // without it the step duration collapses and the arp machine-guns.
    auto runWithTempo = [](double tempo) {
        Harness h;
        h.ctx.tempo = tempo;

        Krate::Test::ParameterChanges setupParams;
        setupParams.add(kArpTempoSyncId, 1.0);
        setupParams.add(kArpNoteValueId, 7.0 / 20.0);  // 1/16

        h.noteOn(60);
        int events = h.runBlock(&setupParams);
        for (int b = 0; b < 20; ++b) events += h.runBlock();
        return events;
    };

    const int atValidTempo = runWithTempo(120.0);
    const int atZeroTempo  = runWithTempo(0.0);

    INFO("events at 120 BPM: " << atValidTempo
         << ", events at tempo 0: " << atZeroTempo);
    REQUIRE(atValidTempo > 0);
    // The 120 BPM fallback must apply, so the two runs emit the same traffic.
    REQUIRE(atZeroTempo == atValidTempo);
}
