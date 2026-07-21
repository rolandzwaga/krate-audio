// ==============================================================================
// Live-mode transport Play-edge hygiene (Gradus audit F3)
// ==============================================================================
// Source = Live is a classic arpeggiator: it free-runs off held notes and keeps
// clocking even while the host transport is stopped (processor.cpp forces
// blockCtx.isPlaying = true in Live mode). That means notes are already
// sounding, and a held chord is already latched into the engine, when the user
// presses Play.
//
// The rising transport-play edge used to run `arpCore_.reset()` +
// `midiDelay_.reset()` unconditionally. Neither emits a NoteOff: reset() clears
// currentArpNotes_/pendingNoteOffs_/heldNotes_ outright and midiDelay_.reset()
// discards pending echoes. Every note sounding at that instant was therefore
// orphaned downstream, and the held chord was forgotten so the arp fell silent
// until the keys were physically re-pressed.
//
// The reset is correct for Source = Sequencer (pattern restart; the sequencer is
// transport-gated and emits nothing while stopped), so the fix is to gate it on
// Sequencer mode rather than to remove it.
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

/// Drives a Gradus::Processor block by block, tallying emitted MIDI per pitch.
struct LiveModeHarness {
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

    std::array<int, 128> onCount{};
    std::array<int, 128> offCount{};

    LiveModeHarness()
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

        // Transport STOPPED but tempo valid — the state a user sits in while
        // noodling on the keyboard before pressing Play.
        ctx.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid;
        ctx.tempo = 120.0;
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;
        ctx.sampleRate = kSampleRate;
        ctx.projectTimeMusic = 0.0;
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

    ~LiveModeHarness()
    {
        proc->setActive(false);
        proc->terminate();
    }

    void setPlaying(bool playing)
    {
        if (playing) ctx.state |= ProcessContext::kPlaying;
        else         ctx.state &= ~static_cast<uint32>(ProcessContext::kPlaying);
    }

    void noteOn(int16 pitch)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = 100.0f / 127.0f;
        e.noteOn.noteId = -1;
        inEvents.addEvent(e);
    }

    void noteOff(int16 pitch)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = 0;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        inEvents.addEvent(e);
    }

    /// Run one block. Returns the number of NoteOns emitted in it.
    int runBlock(IParameterChanges* params = nullptr)
    {
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = params ? params : &noParams;
        proc->process(data);
        inEvents.clear();

        int noteOnsThisBlock = 0;
        const int32 count = outEvents.getEventCount();
        for (int32 i = 0; i < count; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            if (e.type == Event::kNoteOnEvent) {
                ++onCount[e.noteOn.pitch];
                ++noteOnsThisBlock;
            } else if (e.type == Event::kNoteOffEvent) {
                ++offCount[e.noteOff.pitch];
            }
        }
        ctx.projectTimeSamples += kBlockSize;
        return noteOnsThisBlock;
    }

    int runBlocks(int n)
    {
        int noteOns = 0;
        for (int i = 0; i < n; ++i) noteOns += runBlock();
        return noteOns;
    }
};

}  // namespace

TEST_CASE("Live mode: transport Play edge does not orphan sounding notes or drop the held chord",
          "[processor][gradus][live-mode][transport][F3]")
{
    LiveModeHarness h;

    // Source = Live is the default (sourceMode 0). Configure a fast, tempo-synced
    // clock so several steps fire within a handful of blocks: tempo sync ON,
    // note value index 4 = 1/32 (0.125 beats = 2756 samples @ 120 BPM / 44.1k).
    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 4.0 / 20.0);

    // Hold a chord while the transport is STOPPED. Live mode free-runs, so the
    // arp starts emitting immediately.
    h.noteOn(60);
    h.noteOn(64);
    h.noteOn(67);
    h.runBlock(&setupParams);
    h.runBlocks(24);

    const int noteOnsBeforeEdge = h.onCount[60] + h.onCount[64] + h.onCount[67];
    INFO("note-ons emitted while stopped: " << noteOnsBeforeEdge);
    REQUIRE(noteOnsBeforeEdge > 0);   // sanity: Live mode really is free-running

    // ---- Press Play: rising transport edge, chord still physically held. ----
    h.setPlaying(true);
    int noteOnsAfterEdge = h.runBlock();
    noteOnsAfterEdge += h.runBlocks(24);

    // The arp must keep running off the still-held chord. Under the bug the
    // unconditional reset() cleared heldNotes_ and the arp fell silent here.
    INFO("note-ons emitted after the Play edge (keys never re-pressed): "
         << noteOnsAfterEdge);
    REQUIRE(noteOnsAfterEdge > 0);

    // ---- Release the chord and drain. Nothing may be left sounding. ----
    h.noteOff(60);
    h.noteOff(64);
    h.noteOff(67);
    h.runBlocks(80);

    for (int pitch = 0; pitch < 128; ++pitch) {
        INFO("pitch=" << pitch << " on=" << h.onCount[pitch]
                      << " off=" << h.offCount[pitch]);
        REQUIRE(h.offCount[pitch] >= h.onCount[pitch]);
    }
}

TEST_CASE("Sequencer mode: transport Play edge still restarts the pattern",
          "[processor][gradus][sequencer][transport][F3]")
{
    // The Play-edge reset is correct for Source = Sequencer -- pressing Play
    // must restart the programmed pattern from step 0. Guard that the F3 fix
    // did not disable it for the mode that wants it.
    LiveModeHarness h;

    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 4.0 / 20.0);
    setupParams.add(kArpSourceModeId, 1.0);            // Sequencer
    setupParams.add(kArpSequencerNoteLaneLengthId, 0.0);  // length 1
    setupParams.add(kArpSequencerNoteLaneStep0Id, 72.0 / 127.0);
    setupParams.add(kArpSequencerNoteLaneRestStep0Id, 0.0);
    h.runBlock(&setupParams);

    // Transport stopped: the sequencer is transport-gated, so it must stay
    // silent no matter how long we run.
    h.runBlocks(24);
    REQUIRE(h.onCount[72] == 0);

    // Press Play -> the pattern starts.
    h.setPlaying(true);
    h.runBlock();
    h.runBlocks(24);
    REQUIRE(h.onCount[72] > 0);

    // Stop the transport and drain. The sequencer is transport-gated, so Stop
    // is what ends the pattern; every programmed note must then be released.
    // (Draining while still playing would always leave the currently-sounding
    // step unmatched, which is correct behaviour rather than a stuck note.)
    h.setPlaying(false);
    h.runBlocks(80);
    for (int pitch = 0; pitch < 128; ++pitch) {
        INFO("pitch=" << pitch << " on=" << h.onCount[pitch]
                      << " off=" << h.offCount[pitch]);
        REQUIRE(h.offCount[pitch] >= h.onCount[pitch]);
    }
}
