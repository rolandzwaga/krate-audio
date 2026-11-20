// ==============================================================================
// Retrigger Beat follows the host's bar line; a MIDI Delay echo follows its own note's step
// ==============================================================================
// Retrigger Beat: the processor never filled BlockContext::transportPositionSamples, so it stayed 0, and the arp's bar
// detection found a bar line at sample 0 of every block: the pattern restarted every block (2026-11).
// Echo step: the processor read the DELAY lane's position once, after the arp had generated the whole block; the arp
// advances the lane when it fires a step, so every note of the block was echoed with the NEXT step's settings
// (2026-11).
//
// The processor is heap-allocated: it holds the full arp engine.

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "vst_event_list.h"
#include "vst_param_changes.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32 kBlockSize = 512;

struct NoteOnAt {
    int64_t time;  ///< absolute sample
    int16 pitch;
};

/// A Gradus processor in Live mode at 120 BPM, 4/4, transport playing with the musical position advancing.
struct Harness {
    std::unique_ptr<Gradus::Processor> proc = std::make_unique<Gradus::Processor>();
    std::vector<float> outL = std::vector<float>(kBlockSize, 0.0f);
    std::vector<float> outR = std::vector<float>(kBlockSize, 0.0f);
    float* channels[2]{};
    AudioBusBuffers outputBus{};
    Krate::Test::EventList inEvents;
    Krate::Test::EventList outEvents;
    Krate::Test::ParameterChanges params;
    ProcessContext ctx{};
    ProcessData data{};
    int64_t position = 0;
    std::vector<NoteOnAt> noteOns;

    Harness() {
        proc->initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);
        channels[0] = outL.data();
        channels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;
        ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid |
                    ProcessContext::kProjectTimeMusicValid;
        ctx.tempo = 120.0;
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;
        ctx.sampleRate = kSampleRate;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputEvents = &inEvents;
        data.outputEvents = &outEvents;
        data.inputParameterChanges = &params;
        data.processContext = &ctx;
    }

    ~Harness() {
        proc->setActive(false);
        proc->terminate();
    }

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    void noteOn(int16 pitch) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = 100.0f / 127.0f;
        e.noteOn.noteId = -1;
        inEvents.addEvent(e);
    }

    void run(double seconds) {
        const auto blocks = static_cast<int>(seconds * kSampleRate / kBlockSize);
        for (int b = 0; b < blocks; ++b) {
            ctx.projectTimeSamples = position;
            ctx.projectTimeMusic = static_cast<double>(position) / kSampleRate * ctx.tempo / 60.0;
            outEvents.clear();
            proc->process(data);
            inEvents.clear();
            params.clear();
            for (int32 i = 0; i < outEvents.getEventCount(); ++i) {
                Event e{};
                if (outEvents.getEvent(i, e) == kResultOk && e.type == Event::kNoteOnEvent) {
                    noteOns.push_back({position + e.sampleOffset, e.noteOn.pitch});
                }
            }
            position += kBlockSize;
        }
    }
};

[[nodiscard]] bool hasNoteOnNear(const std::vector<NoteOnAt>& notes, int64_t time, int64_t tolerance) {
    return std::any_of(notes.begin(), notes.end(),
                       [&](const NoteOnAt& n) { return std::llabs(n.time - time) <= tolerance; });
}

}  // namespace

TEST_CASE("Retrigger Beat restarts the pattern on the bar line, not every block", "[processor][gradus][retrigger]") {
    Harness h;
    h.params.addChange(Gradus::kArpRetriggerId, 1.0);  // Off, Note, Beat
    h.noteOn(60);
    h.noteOn(64);
    h.noteOn(67);
    h.run(2.0);  // one bar at 120 BPM is 2 s

    std::set<int16> pitches;
    for (const auto& n : h.noteOns) pitches.insert(n.pitch);
    INFO("note-ons " << h.noteOns.size() << ", distinct pitches " << pitches.size());
    CHECK(pitches.size() == 3u);           // the pattern walks the held chord
    CHECK(h.noteOns.size() >= 6u);         // and keeps stepping through the bar
}

TEST_CASE("A MIDI Delay echo uses the delay step of the note it echoes", "[processor][gradus][midi-delay]") {
    Harness h;
    constexpr double kEchoMs = 100.0;
    h.params.addChange(Gradus::kArpMidiDelayLaneLengthId, 1.0 / 31.0);  // 2 steps
    h.params.addChange(Gradus::kArpMidiDelayActiveStep0Id, 1.0);         // step 0 echoes
    h.params.addChange(Gradus::kArpMidiDelayActiveStep0Id + 1, 0.0);     // step 1 does not
    h.params.addChange(Gradus::kArpMidiDelayTimeModeStep0Id, 0.0);       // Free
    h.params.addChange(Gradus::kArpMidiDelayTimeStep0Id, (kEchoMs - 10.0) / 1990.0);
    h.params.addChange(Gradus::kArpMidiDelayFeedbackStep0Id, 1.0 / 16.0);  // one echo
    h.noteOn(60);
    h.run(3.0);

    // The arp notes are the note-ons that are not themselves an echo (100 ms after another note-on)
    const auto echoTol = static_cast<int64_t>(0.002 * kSampleRate);
    const auto echoLag = static_cast<int64_t>(kEchoMs * 0.001 * kSampleRate);
    std::vector<NoteOnAt> arp;
    for (const auto& n : h.noteOns) {
        if (!hasNoteOnNear(h.noteOns, n.time - echoLag, echoTol)) arp.push_back(n);
    }
    REQUIRE(arp.size() >= 6u);
    for (size_t k = 0; k + 1 < arp.size(); ++k) {
        INFO("arp note " << k << " at sample " << arp[k].time);
        CHECK(hasNoteOnNear(h.noteOns, arp[k].time + echoLag, echoTol) == (k % 2 == 0));
    }
}
