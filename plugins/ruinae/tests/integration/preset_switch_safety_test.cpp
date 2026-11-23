// ==============================================================================
// Integration Test: Preset Switch Safety During Playback
// ==============================================================================
// Verifies that preset switching during active audio playback is crash-proof:
//   - No NaN/Inf in output after preset switch
//   - No crash under rapid preset switching
//   - Oscillator type changes during playback are safe
//   - Arp-to-non-arp transitions are clean
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "drain_preset_transfer.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "vst_param_changes.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

std::unique_ptr<TestableProcessor> makeTestableProcessor() {
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

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SingleParamQueue = Krate::Test::ParamValueQueue;
using ParamChangeBatch = Krate::Test::ParameterChanges;




// Minimal MIDI event list for testing
class TestEventList : public Steinberg::Vst::IEventList {
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

    void addNoteOn(int note, float velocity, int sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = static_cast<Steinberg::int16>(note);
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = note;
        events_.push_back(e);
    }

    void addNoteOff(int note, int sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = static_cast<Steinberg::int16>(note);
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = note;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

std::vector<char> captureState(Ruinae::Processor* proc) {
    Steinberg::MemoryStream stream;
    proc->getState(&stream);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

void loadState(Ruinae::Processor* proc, const std::vector<char>& bytes) {
    Steinberg::MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
        static_cast<Steinberg::int32>(bytes.size()), nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc->setState(&stream);
}

bool hasNanOrInf(const float* buffer, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!Krate::DSP::detail::isFinite(buffer[i])) return true;
    }
    return false;
}

/// Per-pitch NoteOn / NoteOff counts read off the processor's MIDI output.
struct MidiOutTally {
    std::array<int, 128> on{};
    std::array<int, 128> off{};
    int totalOn = 0;

    void collect(TestEventList& out) {
        for (Steinberg::int32 i = 0; i < out.getEventCount(); ++i) {
            Steinberg::Vst::Event e{};
            if (out.getEvent(i, e) != Steinberg::kResultTrue) continue;
            if (e.type == Steinberg::Vst::Event::kNoteOnEvent && e.noteOn.velocity > 0.0f) {
                ++on[static_cast<size_t>(e.noteOn.pitch & 0x7F)];
                ++totalOn;
            } else if (e.type == Steinberg::Vst::Event::kNoteOnEvent) {
                ++off[static_cast<size_t>(e.noteOn.pitch & 0x7F)];
            } else if (e.type == Steinberg::Vst::Event::kNoteOffEvent) {
                ++off[static_cast<size_t>(e.noteOff.pitch & 0x7F)];
            }
        }
        out.clear();
    }

    [[nodiscard]] int unbalancedPitches() const {
        int n = 0;
        for (size_t p = 0; p < 128; ++p) {
            if (on[p] != off[p]) ++n;
        }
        return n;
    }
};

/// Largest |x[i] - x[i-1]| over [a, b).
double maxStep(const std::vector<float>& x, size_t a, size_t b) {
    double m = 0.0;
    b = std::min(b, x.size());
    for (size_t i = std::max<size_t>(a, 1); i < b; ++i) {
        m = std::max(m, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    }
    return m;
}

void processBlock(Ruinae::Processor& proc, TestEventList& events,
                  float* outL, float* outR, Steinberg::int32 numSamples,
                  TestEventList* midiOut = nullptr) {
    Steinberg::Vst::ProcessContext context{};
    context.state = Steinberg::Vst::ProcessContext::kPlaying |
                    Steinberg::Vst::ProcessContext::kTempoValid;
    context.tempo = 120.0;
    context.sampleRate = 44100.0;

    float* channelBuffers[2] = {outL, outR};
    Steinberg::Vst::AudioBusBuffers output{};
    output.numChannels = 2;
    output.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
    data.numSamples = numSamples;
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputEvents = &events;
    data.outputEvents = midiOut;
    data.processContext = &context;

    proc.process(data);
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Preset switch during playback produces finite output",
          "[preset-safety][playback]") {
    auto proc = makeTestableProcessor();

    // Set up a saw oscillator preset
    ParamChangeBatch changes;
    changes.add(Ruinae::kOscATypeId, 0.0); // Saw
    changes.add(Ruinae::kOscALevelId, 1.0);
    proc->processParameterChanges(&changes);

    static constexpr Steinberg::int32 kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Play a note
    events.addNoteOn(60, 0.8f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    // Process a few blocks to get sound flowing
    for (int i = 0; i < 5; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    }

    // Now switch to a different preset (different osc type + filter)
    auto proc2 = makeTestableProcessor();
    ParamChangeBatch changes2;
    changes2.add(Ruinae::kOscATypeId, 3.0 / 9.0); // Sync (3)
    changes2.add(Ruinae::kFilterCutoffId, 0.3);
    proc2->processParameterChanges(&changes2);
    auto presetB = captureState(proc2.get());

    // Load new preset while notes are playing
    loadState(proc.get(), presetB);

    // Process several blocks after the switch — verify no NaN/Inf
    bool anyBadSamples = false;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);

    proc->terminate();
    proc2->terminate();
}

TEST_CASE("Rapid preset switching doesn't crash",
          "[preset-safety][rapid]") {
    auto proc = makeTestableProcessor();

    static constexpr Steinberg::int32 kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Create 5 different preset states
    std::vector<std::vector<char>> presets;
    for (int i = 0; i < 5; ++i) {
        auto tmp = makeTestableProcessor();
        ParamChangeBatch changes;
        changes.add(Ruinae::kOscATypeId, static_cast<double>(i) / 9.0);
        changes.add(Ruinae::kMasterGainId, 0.2 + 0.15 * i);
        tmp->processParameterChanges(&changes);
        presets.push_back(captureState(tmp.get()));
        tmp->terminate();
    }

    // Play a note
    events.addNoteOn(60, 0.8f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    // Rapidly switch between presets (2 presets per process block)
    bool anyBadSamples = false;
    for (int cycle = 0; cycle < 10; ++cycle) {
        loadState(proc.get(), presets[static_cast<size_t>(cycle % 5)]);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);

        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);
    proc->terminate();
}

TEST_CASE("Oscillator type switch during playback",
          "[preset-safety][osc-switch]") {
    auto proc = makeTestableProcessor();

    // Start with Saw oscillator
    ParamChangeBatch sawChanges;
    sawChanges.add(Ruinae::kOscATypeId, 0.0); // Saw = 0
    sawChanges.add(Ruinae::kOscALevelId, 1.0);
    proc->processParameterChanges(&sawChanges);

    static constexpr Steinberg::int32 kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Play a note and process a few blocks
    events.addNoteOn(60, 0.8f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    for (int i = 0; i < 3; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    }
    // The last steady block is the click reference.
    std::vector<float> trace(outL.begin(), outL.end());

    // Create an Additive preset
    auto wtProc = makeTestableProcessor();
    ParamChangeBatch wtChanges;
    wtChanges.add(Ruinae::kOscATypeId, 4.0 / 9.0); // Additive = 4
    wtChanges.add(Ruinae::kOscALevelId, 0.8);
    wtProc->processParameterChanges(&wtChanges);
    auto wtState = captureState(wtProc.get());

    // Switch while note is playing
    loadState(proc.get(), wtState);

    // The switch block: a preset load is a hard cut by design (engine reset), so
    // the bound that holds is a jump of at most 3x the steady block's largest
    // per-sample step across the switch boundary, not "no discontinuity".
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    trace.insert(trace.end(), outL.begin(), outL.end());
    const double steadyStep = maxStep(trace, 1, static_cast<size_t>(kBlockSize));
    const double switchStep = maxStep(trace, static_cast<size_t>(kBlockSize),
                                      static_cast<size_t>(kBlockSize) + 2 * static_cast<size_t>(kBlockSize));
    INFO("steady max|dx| " << steadyStep << ", switch-block max|dx| " << switchStep);
    REQUIRE(steadyStep > 1e-4); // the reference is a real signal
    CHECK(switchStep / steadyStep <= 3.0);

    // Verify clean output after switch
    bool anyBadSamples = false;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);

    proc->terminate();
    wtProc->terminate();
}

TEST_CASE("Arp-to-non-arp preset switch",
          "[preset-safety][arp-switch]") {
    // Create an arp-enabled preset
    auto arpProc = makeTestableProcessor();
    ParamChangeBatch arpChanges;
    arpChanges.add(Ruinae::kArpOperatingModeId, 1.0 / 3.0);
    arpChanges.add(Ruinae::kArpModeId, 0.0); // Up
    arpChanges.add(Ruinae::kArpTempoSyncId, 1.0);
    arpChanges.add(Ruinae::kOscALevelId, 1.0);
    arpChanges.add(Ruinae::kArpGateLengthId, 1.0); // 200 %: an arp note always sounds
    arpChanges.add(Ruinae::kArpMidiOutId, 1.0);
    arpProc->processParameterChanges(&arpChanges);
    auto arpState = captureState(arpProc.get());

    // Create a non-arp preset
    auto noArpProc = makeTestableProcessor();
    ParamChangeBatch noArpChanges;
    noArpChanges.add(Ruinae::kArpOperatingModeId, 0.0);
    noArpChanges.add(Ruinae::kOscALevelId, 0.8);
    noArpChanges.add(Ruinae::kFilterCutoffId, 0.5);
    noArpProc->processParameterChanges(&noArpChanges);
    auto noArpState = captureState(noArpProc.get());

    // Start playing with arp preset
    auto proc = makeTestableProcessor();
    loadState(proc.get(), arpState);

    static constexpr Steinberg::int32 kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    events.addNoteOn(60, 0.8f);
    events.addNoteOn(64, 0.8f);
    events.addNoteOn(67, 0.8f);

    TestEventList midiOut;
    MidiOutTally tally;

    // Process several blocks with arp running
    for (int i = 0; i < 5; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize, &midiOut);
        tally.collect(midiOut);
        events.clear();
    }
    REQUIRE(tally.totalOn > 0); // the arp is really running

    // Switch to non-arp preset, then release the keys
    loadState(proc.get(), noArpState);
    events.addNoteOff(60);
    events.addNoteOff(64);
    events.addNoteOff(67);

    // Default amp release 200 ms = 18 blocks of 512 at 44.1 kHz, plus 10 blocks.
    constexpr int kReleasePlus10Blocks = 18 + 10;
    bool anyBadSamples = false;
    for (int i = 0; i < kReleasePlus10Blocks; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize, &midiOut);
        tally.collect(midiOut);
        events.clear();
        anyBadSamples = anyBadSamples || hasNanOrInf(outL.data(), kBlockSize) ||
                        hasNanOrInf(outR.data(), kBlockSize);
    }
    REQUIRE_FALSE(anyBadSamples);

    // Silence: the next 10 blocks are exactly zero (no stuck arp voice).
    bool silent = true;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize, &midiOut);
        tally.collect(midiOut);
        for (Steinberg::int32 s = 0; s < kBlockSize; ++s) {
            if (outL[static_cast<size_t>(s)] != 0.0f || outR[static_cast<size_t>(s)] != 0.0f) {
                silent = false;
            }
        }
    }
    CHECK(silent);
    // Every MIDI-out NoteOn got its NoteOff: no stranded downstream note.
    CHECK(tally.unbalancedPitches() == 0);

    arpProc->terminate();
    noArpProc->terminate();
    proc->terminate();
}

TEST_CASE("Voice routes synced after preset snapshot",
          "[preset-safety][voice-routes]") {
    // Create a preset with a voice route configured through notify (UI thread)
    auto proc1 = makeTestableProcessor();
    const auto stateDefault = captureState(proc1.get());

    auto routeMsg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    routeMsg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = routeMsg->getAttributes();
    attrs->setInt("slotIndex", 2);
    attrs->setInt("source", 1);
    attrs->setInt("destination", 0);
    attrs->setFloat("amount", 0.5);
    attrs->setInt("active", 1);
    proc1->notify(routeMsg);

    // Let the audio thread apply the route, then save.
    static constexpr Steinberg::int32 kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;
    processBlock(*proc1, events, outL.data(), outR.data(), kBlockSize);
    auto state1 = captureState(proc1.get());
    // The route is really in the saved bytes (otherwise the round trip is vacuous).
    REQUIRE(state1 != stateDefault);

    // Load into a fresh processor, drain, verify state survives round-trip
    auto proc2 = makeTestableProcessor();
    loadState(proc2.get(), state1);
    drainPresetTransfer(proc2.get());

    // Get state and verify it matches
    auto state2 = captureState(proc2.get());
    REQUIRE(state1.size() == state2.size());
    REQUIRE(state1 == state2);

    proc1->terminate();
    proc2->terminate();
}

TEST_CASE("Rapid preset switch with voice route updates produces finite output",
          "[preset-safety][voice-routes][playback]") {
    auto proc = makeTestableProcessor();

    static constexpr Steinberg::int32 kBlockSize = 256;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    TestEventList events;

    // Create two presets with different settings
    auto procA = makeTestableProcessor();
    ParamChangeBatch changesA;
    changesA.add(Ruinae::kOscATypeId, 0.0); // Saw
    changesA.add(Ruinae::kOscALevelId, 1.0);
    procA->processParameterChanges(&changesA);
    auto presetA = captureState(procA.get());

    auto procB = makeTestableProcessor();
    ParamChangeBatch changesB;
    changesB.add(Ruinae::kOscATypeId, 3.0 / 9.0); // Sync (3)
    changesB.add(Ruinae::kFilterCutoffId, 0.3);
    procB->processParameterChanges(&changesB);
    auto presetB = captureState(procB.get());

    // Play notes
    events.addNoteOn(60, 0.8f);
    events.addNoteOn(64, 0.7f);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    events.clear();

    // Process a few blocks to get sound flowing
    for (int i = 0; i < 3; ++i) {
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
    }

    // Now: load preset A, send voice route update, load preset B, play more
    loadState(proc.get(), presetA);
    processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);

    // Send voice route update via notify (simulates UI thread)
    auto routeMsg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    routeMsg->setMessageID("VoiceModRouteUpdate");
    auto* attrs = routeMsg->getAttributes();
    attrs->setInt("slotIndex", 2);
    attrs->setInt("source", 1);
    attrs->setInt("destination", 0);
    attrs->setFloat("amount", 0.5);
    attrs->setInt("active", 1);
    proc->notify(routeMsg);

    loadState(proc.get(), presetB);

    // Process several blocks after all the chaos — verify no NaN/Inf
    bool anyBadSamples = false;
    for (int i = 0; i < 10; ++i) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processBlock(*proc, events, outL.data(), outR.data(), kBlockSize);
        if (hasNanOrInf(outL.data(), kBlockSize) ||
            hasNanOrInf(outR.data(), kBlockSize)) {
            anyBadSamples = true;
            break;
        }
    }

    REQUIRE_FALSE(anyBadSamples);

    procA->terminate();
    procB->terminate();
    proc->terminate();
}
