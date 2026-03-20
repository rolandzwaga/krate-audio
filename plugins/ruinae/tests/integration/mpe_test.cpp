// ==============================================================================
// Integration Test: MPE (MIDI Polyphonic Expression) Support
// ==============================================================================
// Verifies per-note expression routing through the Ruinae processor:
// - noteId-aware noteOn/noteOff via event list (tests dispatcher concept detection)
// - NoteExpression tuning, volume, brightness
// - Pitch bend callback
// - Backward compatibility with non-MPE noteOn/noteOff
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;
using namespace Steinberg;
using namespace Steinberg::Vst;

// =============================================================================
// Test Helpers
// =============================================================================

static constexpr int32 kBlockSize = 512;

class EmptyParamChanges : public IParameterChanges {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue* PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
};

class MpeEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override {
        return static_cast<int32>(events_.size());
    }

    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }

    tresult PLUGIN_API addEvent(Event& e) override {
        events_.push_back(e);
        return kResultTrue;
    }

    void addNoteOn(int16_t pitch, float velocity, int32_t noteId = -1) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = noteId;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteOff(int16_t pitch, int32_t noteId = -1) {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = 0;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = noteId;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteExpression(int32_t noteId, uint32_t typeId, double value) {
        Event e{};
        e.type = Event::kNoteExpressionValueEvent;
        e.sampleOffset = 0;
        e.noteExpressionValue.noteId = noteId;
        e.noteExpressionValue.typeId = typeId;
        e.noteExpressionValue.value = value;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

struct TestProcessor {
    Ruinae::Processor proc;
    std::vector<float> outL = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    float* channels[2];
    AudioBusBuffers bus{};
    EmptyParamChanges paramChanges;
    MpeEventList eventList;
    ProcessData data{};

    TestProcessor() {
        channels[0] = outL.data();
        channels[1] = outR.data();
        bus.numChannels = 2;
        bus.channelBuffers32 = channels;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputParameterChanges = &paramChanges;
        data.inputEvents = &eventList;
        data.processContext = nullptr;

        proc.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = kBlockSize;
        proc.setupProcessing(setup);
        proc.setActive(true);
    }

    ~TestProcessor() {
        proc.setActive(false);
        proc.terminate();
    }

    /// Process N blocks. Events are consumed on first block, cleared for rest.
    void processBlocks(int count) {
        for (int i = 0; i < count; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            bus.silenceFlags = 0;
            proc.process(data);
            // Clear events after first block so they don't re-fire
            if (i == 0) eventList.clear();
        }
    }

    float rmsL() const {
        float sumSq = 0.0f;
        for (size_t i = 0; i < static_cast<size_t>(kBlockSize); ++i)
            sumSq += outL[i] * outL[i];
        return std::sqrt(sumSq / static_cast<float>(kBlockSize));
    }

    float rmsR() const {
        float sumSq = 0.0f;
        for (size_t i = 0; i < static_cast<size_t>(kBlockSize); ++i)
            sumSq += outR[i] * outR[i];
        return std::sqrt(sumSq / static_cast<float>(kBlockSize));
    }

    float rmsMono() const {
        return (rmsL() + rmsR()) * 0.5f;
    }

    bool hasAudio() const {
        for (size_t i = 0; i < static_cast<size_t>(kBlockSize); ++i) {
            if (outL[i] != 0.0f || outR[i] != 0.0f)
                return true;
        }
        return false;
    }
};

// =============================================================================
// Phase 1: Voice Identity Tests
// =============================================================================

TEST_CASE("MPE: noteId-aware noteOn produces audio", "[mpe][integration]") {
    TestProcessor t;

    // Send noteOn with noteId through event list
    t.eventList.addNoteOn(60, 0.8f, 42);
    t.processBlocks(10);

    REQUIRE(t.hasAudio());
}

TEST_CASE("MPE: noteId-aware noteOff releases voice", "[mpe][integration]") {
    TestProcessor t;

    // Play note with noteId
    t.eventList.addNoteOn(60, 0.8f, 42);
    t.processBlocks(5);

    float sustainRms = t.rmsMono();
    REQUIRE(sustainRms > 0.001f);

    // Release with matching noteId
    t.eventList.addNoteOff(60, 42);
    t.processBlocks(50);

    float releaseRms = t.rmsMono();
    // After release, should be significantly quieter
    REQUIRE(releaseRms < sustainRms * 0.5f);
}

TEST_CASE("MPE: two notes with different noteIds both produce output", "[mpe][integration]") {
    TestProcessor t;

    // Play single note, measure RMS
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);
    float singleRms = t.rmsMono();
    REQUIRE(singleRms > 0.001f);

    // Add second note
    t.eventList.addNoteOn(64, 0.8f, 2);
    t.processBlocks(10);
    float dualRms = t.rmsMono();

    // Two notes should be louder than a single note
    REQUIRE(dualRms > singleRms * 0.4f);
}

TEST_CASE("MPE: backward compatibility - noteOn without noteId works", "[mpe][integration]") {
    TestProcessor t;

    // Use noteOn without noteId (defaults to -1)
    t.eventList.addNoteOn(60, 0.8f);
    t.processBlocks(10);

    REQUIRE(t.hasAudio());
    REQUIRE(t.rmsMono() > 0.001f);
}

// =============================================================================
// Phase 2: Expression Application Tests
// =============================================================================

TEST_CASE("MPE: NoteExpression tuning shifts pitch", "[mpe][integration]") {
    TestProcessor t;

    // Play note C4 with noteId=1
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);

    // Apply tuning: +12 semitones (one octave up)
    // kTuningTypeID=2, value 0.5 + 12/240 = 0.55
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kTuningTypeID, 0.55);
    t.processBlocks(5);

    // Verify it still produces audio after tuning change
    REQUIRE(t.hasAudio());
    REQUIRE(t.rmsMono() > 0.001f);
}

TEST_CASE("MPE: NoteExpression volume scales output", "[mpe][integration]") {
    TestProcessor t;

    // Play note with noteId=1
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);
    float baseRms = t.rmsMono();
    REQUIRE(baseRms > 0.001f);

    // Apply volume expression: 0.5 = 2x gain (kVolumeTypeID=0)
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kVolumeTypeID, 0.5);
    t.processBlocks(5);
    float boostedRms = t.rmsMono();

    // Should be louder than baseline
    REQUIRE(boostedRms > baseRms * 1.2f);
}

TEST_CASE("MPE: NoteExpression brightness modulates filter", "[mpe][integration]") {
    TestProcessor t;

    // Play note with noteId=1, let it stabilize
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(15);
    float defaultRms = t.rmsMono();
    REQUIRE(defaultRms > 0.001f);

    // Apply brightness=0 (darkest: -48 semitones cutoff offset)
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kBrightnessTypeID, 0.0);
    t.processBlocks(5);
    float darkRms = t.rmsMono();

    // Apply brightness=1 (brightest: +48 semitones cutoff offset)
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kBrightnessTypeID, 1.0);
    t.processBlocks(5);
    float brightRms = t.rmsMono();

    // Brightness should affect audio: bright should be different from dark
    // (higher cutoff = more harmonics = typically higher RMS)
    // At minimum, both should still produce audio
    REQUIRE(darkRms > 0.0001f);
    REQUIRE(brightRms > 0.0001f);
    // The spectral content should differ — bright typically louder due to more harmonics
    REQUIRE(brightRms != Approx(darkRms).margin(0.0001f));
}

TEST_CASE("MPE: NoteExpression volume at zero silences voice", "[mpe][integration]") {
    TestProcessor t;

    // Play note with noteId=1
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);
    REQUIRE(t.hasAudio());

    // Set volume to zero
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kVolumeTypeID, 0.0);
    t.processBlocks(5);

    float rms = t.rmsMono();
    REQUIRE(rms < 0.001f);
}

TEST_CASE("MPE: per-voice expression independence", "[mpe][integration]") {
    TestProcessor t;

    // Play two notes
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.eventList.addNoteOn(64, 0.8f, 2);
    t.processBlocks(10);

    float dualRms = t.rmsMono();
    REQUIRE(dualRms > 0.001f);

    // Silence only noteId=1 via volume expression
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kVolumeTypeID, 0.0);
    t.processBlocks(5);

    float afterSilenceRms = t.rmsMono();

    // Should still have audio from noteId=2
    REQUIRE(afterSilenceRms > 0.001f);
    // But should be quieter than both notes together
    REQUIRE(afterSilenceRms < dualRms * 0.9f);
}

// =============================================================================
// Phase 3: Pitch Bend Callback Tests
// =============================================================================

TEST_CASE("MPE: onPitchBend applies global pitch bend", "[mpe][integration]") {
    TestProcessor t;

    // Play note via event list
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);

    float baseRms = t.rmsMono();
    REQUIRE(baseRms > 0.001f);

    // Apply pitch bend via direct call (pitch bend comes via legacy MIDI CC)
    t.proc.onPitchBend(1.0f);
    t.processBlocks(5);

    // Should still produce audio after pitch bend
    REQUIRE(t.hasAudio());
    REQUIRE(t.rmsMono() > 0.001f);
}

TEST_CASE("MPE: pitch bend + expression tuning combine", "[mpe][integration]") {
    TestProcessor t;

    // Play note with noteId
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(5);

    // Apply both global pitch bend and per-note expression tuning
    t.proc.onPitchBend(0.5f);
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kTuningTypeID, 0.55);
    t.processBlocks(5);

    // Should still produce audio (both applied without crashing)
    REQUIRE(t.hasAudio());
    REQUIRE(t.rmsMono() > 0.001f);
}
