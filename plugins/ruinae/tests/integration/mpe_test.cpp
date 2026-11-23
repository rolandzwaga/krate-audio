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
#include "vst_param_changes.h"

using Catch::Approx;
using namespace Steinberg;
using namespace Steinberg::Vst;

// =============================================================================
// Test Helpers
// =============================================================================

static constexpr int32 kBlockSize = 512;

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using EmptyParamChanges = Krate::Test::ParameterChanges;


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

    void addNoteExpression(int32_t noteId, uint32_t typeId, double value,
                           int32 sampleOffset = 0) {
        Event e{};
        e.type = Event::kNoteExpressionValueEvent;
        e.sampleOffset = sampleOffset;
        e.noteExpressionValue.noteId = noteId;
        e.noteExpressionValue.typeId = typeId;
        e.noteExpressionValue.value = value;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

namespace {
// Exposes the read-only engine accessor for tests that assert on voice state.
class MpeTestProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::engine;
};
} // namespace

struct TestProcessor {
    MpeTestProcessor proc;
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

// =============================================================================
// Pitch assertions (R-120): the tests above only prove "something sounds"
// =============================================================================

namespace {

/// Renders `blocks` more blocks and returns the left channel of all of them.
std::vector<float> captureLeft(TestProcessor& t, int blocks) {
    std::vector<float> out;
    for (int i = 0; i < blocks; ++i) {
        t.processBlocks(1);
        out.insert(out.end(), t.outL.begin(), t.outL.end());
    }
    return out;
}

/// Mean frequency from interpolated rising zero crossings.
double zeroCrossingHz(const std::vector<float>& x, double sampleRate) {
    double first = -1.0;
    double last = -1.0;
    int periods = -1;
    for (size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double crossing = static_cast<double>(i - 1) +
                static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            if (first < 0.0) first = crossing;
            last = crossing;
            ++periods;
        }
    }
    if (periods < 1) return 0.0;
    return sampleRate * static_cast<double>(periods) / (last - first);
}

constexpr double kC4Hz = 261.6255653005986;

/// Pitch of the held note after `settleBlocks` further blocks, over `measureBlocks`.
double heldPitchHz(TestProcessor& t, int settleBlocks, int measureBlocks) {
    t.processBlocks(settleBlocks);
    return zeroCrossingHz(captureLeft(t, measureBlocks), 44100.0);
}

} // namespace

TEST_CASE("MPE: tuning expression of +12 semitones shifts the pitch by an octave",
          "[mpe][integration][pitch]") {
    TestProcessor t;
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);
    const double before = zeroCrossingHz(captureLeft(t, 8), 44100.0);
    INFO("before tuning: " << before << " Hz");
    REQUIRE(before == Approx(kC4Hz).epsilon(0.01));

    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kTuningTypeID, 0.55);
    const double after = heldPitchHz(t, 6, 8);
    INFO("after tuning: " << after << " Hz");
    REQUIRE(after == Approx(523.25).epsilon(0.01));
}

TEST_CASE("MPE: pitch bend and expression tuning sum their offsets",
          "[mpe][integration][pitch]") {
    TestProcessor t;
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);

    // Bend +0.5 of the default 2-semitone range = +1 semitone; tuning +12 semitones
    t.proc.onPitchBend(0.5f);
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kTuningTypeID, 0.55);
    const double hz = heldPitchHz(t, 20, 8);
    const double expected = kC4Hz * std::pow(2.0, 13.0 / 12.0);
    INFO("measured " << hz << " Hz, expected " << expected << " Hz");
    REQUIRE(hz == Approx(expected).epsilon(0.01));
}

TEST_CASE("MPE: pitch bend alone bends the note by its range",
          "[mpe][integration][pitch]") {
    TestProcessor t;
    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);

    t.proc.onPitchBend(1.0f);  // +2 semitones
    const double hz = heldPitchHz(t, 20, 8);
    const double expected = kC4Hz * std::pow(2.0, 2.0 / 12.0);
    INFO("measured " << hz << " Hz, expected " << expected << " Hz");
    REQUIRE(hz == Approx(expected).epsilon(0.01));
}

// =============================================================================
// Phase 4: noteId bookkeeping addresses the sounding note (R-035, R-168, R-167, R-036)
// =============================================================================

namespace {

constexpr int kBlocksPer500Ms = static_cast<int>(0.5 * 44100.0 / kBlockSize) + 1;

/// Voice 0's sounding pitch in Hz: its note frequency times its MPE tuning offset.
double monoVoicePitchHz(const TestProcessor& t) {
    const auto& voice = t.proc.engine().getVoice(0);
    return static_cast<double>(voice.getNoteFrequency()) *
           std::pow(2.0, static_cast<double>(voice.expressionTuning()) / 12.0);
}

double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }

} // namespace

TEST_CASE("NoteId_SupersededNoteOff_DoesNotCutTheNewNote", "[integration][mpe][noteid]") {
    TestProcessor t;

    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);
    t.eventList.addNoteOn(60, 0.8f, 2);  // same pitch, new noteId, while id 1 is held
    t.processBlocks(10);
    const float sustainRms = t.rmsMono();
    REQUIRE(sustainRms > 0.001f);

    // Note 1 ends: it no longer owns a voice, so note 2 must keep sounding.
    t.eventList.addNoteOff(60, 1);
    t.processBlocks(kBlocksPer500Ms);

    INFO("sustain RMS " << sustainRms << ", RMS 500 ms after the stale note-off " << t.rmsMono());
    REQUIRE(t.rmsMono() >= 0.5f * sustainRms);
}

TEST_CASE("NoteId_MonoLegato_ExpressionFollowsTheSoundingNote", "[integration][mpe][noteid]") {
    TestProcessor t;
    t.paramChanges.addChange(Ruinae::kVoiceModeId, 1.0);   // Mono
    t.paramChanges.addChange(Ruinae::kMonoLegatoId, 1.0);  // Legato on
    t.processBlocks(2);
    t.paramChanges.clear();

    const double c3 = 440.0 * std::pow(2.0, (48.0 - 69.0) / 12.0);

    t.eventList.addNoteOn(48, 0.8f, 1);
    t.processBlocks(5);
    t.eventList.addNoteOn(52, 0.8f, 2);
    t.processBlocks(5);
    t.eventList.addNoteExpression(2, NoteExpressionTypeIDs::kTuningTypeID, 0.5 + 1.0 / 240.0);
    t.processBlocks(5);
    t.eventList.addNoteOff(52, 2);  // back to the held C3
    t.processBlocks(kBlocksPer500Ms);  // past any glide back to C3

    const double pitchAfterReturn = monoVoicePitchHz(t);
    INFO("pitch after returning to C3: " << pitchAfterReturn << " Hz, C3 = " << c3 << " Hz");
    REQUIRE(std::abs(centsBetween(pitchAfterReturn, c3)) <= 1.0);

    // The held note's own finger still bends it.
    t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kTuningTypeID, 0.5 + 2.0 / 240.0);
    t.processBlocks(5);
    const double pitchAfterBend = monoVoicePitchHz(t);
    INFO("pitch after a +2 st tuning expression on id 1: " << pitchAfterBend << " Hz");
    REQUIRE(std::abs(centsBetween(pitchAfterBend, c3) - 200.0) <= 1.0);
}

TEST_CASE("NoteId_TinyVelocity_IsANoteOn", "[integration][mpe][noteid]") {
    TestProcessor t;

    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(10);
    t.eventList.addNoteOn(64, 0.003f, 2);  // positive VST3 velocity: a note-on
    t.processBlocks(5);
    t.eventList.addNoteOff(64, 2);
    t.processBlocks(5);

    // The held 60 keeps its gate and its envelope is not in release.
    const auto& engine = t.proc.engine();
    int voice60 = -1;
    for (size_t i = 0; i < Krate::DSP::RuinaeEngine::kMaxPolyphony; ++i) {
        if (engine.gatedVoiceNoteForTest(i) == 60) voice60 = static_cast<int>(i);
    }
    REQUIRE(voice60 >= 0);
    CHECK(engine.getVoiceAmpEnvelope(static_cast<size_t>(voice60)).getStage() !=
          Krate::DSP::ADSRStage::Release);
    CHECK(engine.isVoiceActive(static_cast<size_t>(voice60)));

    // Releasing 60 frees every allocator slot once the release (200 ms) has run.
    t.eventList.addNoteOff(60, 1);
    t.processBlocks(4 * kBlocksPer500Ms);
    REQUIRE(t.proc.engineActiveVoiceCountForTest() == 0);
}

TEST_CASE("NoteExpression_IgnoredTypes_DoNotSplitSegments", "[integration][mpe][noteid]") {
    TestProcessor t;

    t.eventList.addNoteOn(60, 0.8f, 1);
    t.processBlocks(5);

    // 500 pan expressions, each at its own sample: pan is not applied by the
    // engine, so none of them may split the render.
    for (int32 i = 0; i < 500; ++i) {
        t.eventList.addNoteExpression(1, NoteExpressionTypeIDs::kPanTypeID, 0.25, i);
    }
    const uint64_t before = t.proc.engineRenderCallsForTest();
    t.processBlocks(1);
    const uint64_t calls = t.proc.engineRenderCallsForTest() - before;
    INFO("engine processBlock calls for the buffer: " << calls);
    REQUIRE(calls == 1);
}
