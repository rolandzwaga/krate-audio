// ==============================================================================
// Integration Test: MIDI Event Dispatch
// ==============================================================================
// Verifies that MIDI events are dispatched correctly through the Processor:
// - Multiple noteOn events produce audio
// - NoteOff events trigger release
// - Velocity-0 noteOn treated as noteOff
// - Unsupported events are ignored
//
// Reference: specs/045-plugin-shell/spec.md FR-009, FR-010
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"

using Catch::Approx;

// =============================================================================
// Mock classes (same as processor_audio_test)
// =============================================================================

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using TestEventList = Krate::Test::EventList;


// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using TestParameterChanges = Krate::Test::ParameterChanges;


// =============================================================================
// Helpers
// =============================================================================

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

static float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// =============================================================================
// Test Fixture Helper
// =============================================================================

struct ProcessorFixture {
    Ruinae::Processor processor;
    TestEventList events;
    TestParameterChanges params;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 512;

    ProcessorFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &params;
        data.inputEvents = &events;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~ProcessorFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processBlock() {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
    }

    void clearEvents() {
        events.clear();
    }
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Multiple noteOn events produce audio", "[midi][integration]") {
    ProcessorFixture f;

    // Send two notes simultaneously
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process a few more blocks
    bool audioFound = false;
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    REQUIRE(audioFound);
}

TEST_CASE("Velocity-0 noteOn is treated as noteOff", "[midi][integration]") {
    ProcessorFixture f;

    // Start a note
    f.events.addNoteOn(60, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Let it play for a bit
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
    }

    // Send velocity-0 noteOn (should act as noteOff)
    f.events.addNoteOn(60, 0.0f);
    f.processBlock();
    f.clearEvents();

    // Wait for release + effects tails
    float finalPeak = 1.0f;
    for (int i = 0; i < 200; ++i) {
        f.processBlock();
        finalPeak = std::max(findPeak(f.outL.data(), f.kBlockSize),
                             findPeak(f.outR.data(), f.kBlockSize));
        if (finalPeak < 1e-6f) break;
    }

    REQUIRE(finalPeak < 0.01f);
}

TEST_CASE("Unsupported event types are ignored", "[midi][integration]") {
    ProcessorFixture f;

    // Create an unsupported event type
    Steinberg::Vst::Event unsupportedEvent{};
    unsupportedEvent.type = Steinberg::Vst::Event::kDataEvent;
    unsupportedEvent.sampleOffset = 0;
    f.events.addEvent(unsupportedEvent);

    // Should not crash
    f.processBlock();

    // Output should be silence (no notes played)
    float peak = std::max(findPeak(f.outL.data(), f.kBlockSize),
                          findPeak(f.outR.data(), f.kBlockSize));
    REQUIRE(peak < 0.01f);
}

// =============================================================================
// Sample-accurate notes
// =============================================================================
// Ruinae used to apply every note-on and note-off (from the host and from its
// own arpeggiator) at the start of the audio buffer, whatever its sampleOffset,
// so at a 2048-sample buffer a note could start or stop up to 46 ms early. Each
// note must start and stop at its own sample, so a 2048-sample buffer renders
// what 64-sample buffers render.

namespace {

struct TimedNote {
    int64_t at;     ///< absolute sample
    bool on;
    int16_t pitch;
};

/// Render `total` samples in blocks of `blockSize` with the given notes at their
/// absolute positions (each delivered with its sampleOffset in its block). Delay
/// and reverb are off so the dry voice is measured; `arpMidi` switches the
/// arpeggiator to MIDI mode (it then plays the held notes as timed steps).
std::vector<float> renderTimed(size_t blockSize, const std::vector<TimedNote>& notes,
                               int64_t total, bool arpMidi) {
    Ruinae::Processor processor;
    processor.initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(blockSize);
    processor.setupProcessing(setup);
    processor.setActive(static_cast<Steinberg::TBool>(true));

    std::vector<float> outL(blockSize);
    std::vector<float> outR(blockSize);
    std::vector<float> rendered;
    rendered.reserve(static_cast<size_t>(total));
    float* channels[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = channels;
    TestEventList events;
    TestEventList outEvents;
    TestParameterChanges params;
    Steinberg::Vst::ProcessContext ctx{};
    ctx.state = Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kTempoValid
              | Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
    ctx.sampleRate = 44100.0;
    ctx.tempo = 120.0;

    for (int64_t pos = 0; pos < total; pos += static_cast<int64_t>(blockSize)) {
        const auto n = static_cast<Steinberg::int32>(std::min<int64_t>(static_cast<int64_t>(blockSize), total - pos));
        events.clear();
        outEvents.clear();
        params.clear();
        if (pos == 0) {
            params.addChange(Ruinae::kDelayEnabledId, 0.0);
            params.addChange(Ruinae::kReverbEnabledId, 0.0);
            if (arpMidi) params.addChange(Ruinae::kArpOperatingModeId, 1.0 / 3.0);
        }
        for (const auto& note : notes) {
            if (note.at >= pos && note.at < pos + n) {
                const auto offset = static_cast<Steinberg::int32>(note.at - pos);
                if (note.on) events.addNoteOn(note.pitch, 0.8f, offset);
                else events.addNoteOff(note.pitch, offset);
            }
        }
        ctx.projectTimeSamples = pos;
        ctx.projectTimeMusic = static_cast<double>(pos) / 44100.0 * 2.0;
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = n;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputParameterChanges = &params;
        data.inputEvents = &events;
        data.outputEvents = &outEvents;
        data.processContext = &ctx;
        processor.process(data);
        rendered.insert(rendered.end(), outL.begin(), outL.begin() + n);
    }
    processor.setActive(static_cast<Steinberg::TBool>(false));
    processor.terminate();
    return rendered;
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t& worst) {
    float m = 0.0f;
    worst = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const float d = std::abs(a[i] - b[i]);
        if (d > m) { m = d; worst = i; }
    }
    return m;
}

}  // namespace

TEST_CASE("Notes start and stop at their sample offset inside a buffer", "[midi][integration][block_size]") {
    constexpr int64_t kTotal = int64_t{3} * 2048;

    SECTION("note-on at offset k of a 2048-sample buffer starts at sample k") {
        for (int64_t k : {int64_t{0}, int64_t{1}, int64_t{1000}, int64_t{2047}}) {
            const std::vector<TimedNote> notes{{.at = k, .on = true, .pitch = 60}};
            const auto large = renderTimed(2048, notes, kTotal, false);
            const auto small = renderTimed(64, notes, kTotal, false);
            // Silent before the note, sounding within its first 64 samples.
            float before = 0.0f;
            for (int64_t i = 0; i < k; ++i) before = std::max(before, std::abs(large[static_cast<size_t>(i)]));
            float after = 0.0f;
            for (int64_t i = k; i < k + 64; ++i) after = std::max(after, std::abs(large[static_cast<size_t>(i)]));
            size_t worst = 0;
            const float diff = maxAbsDiff(large, small, worst);
            // The two buffer sizes render the same voice from the same sample, on
            // the same 64-sample modulation grid, so they differ only by float
            // noise: 1e-5 (-100 dB). A note quantised to its buffer start moves
            // the whole note by up to 2047 samples (a difference of order 1).
            INFO("k = " << k << ": max |out| before k " << before << ", in [k, k+64) " << after
                 << ", max |2048 - 64| " << diff << " at sample " << worst);
            CHECK(before == 0.0f);
            CHECK(after > 1e-4f);
            CHECK(diff <= 1e-5f);
        }
    }

    SECTION("note-off at offset k of a 2048-sample buffer stops at sample k") {
        for (int64_t k : {int64_t{0}, int64_t{1}, int64_t{1000}, int64_t{2047}}) {
            const std::vector<TimedNote> notes{{.at = 0, .on = true, .pitch = 60}, {.at = 2048 + k, .on = false, .pitch = 60}};
            const auto large = renderTimed(2048, notes, kTotal, false);
            const auto small = renderTimed(64, notes, kTotal, false);
            size_t worst = 0;
            const float diff = maxAbsDiff(large, small, worst);
            INFO("note-off at " << 2048 + k << ": max |2048 - 64| " << diff << " at sample " << worst);
            CHECK(diff <= 1e-5f);
        }
    }

    SECTION("arpeggiator notes start at their step's sample") {
        const std::vector<TimedNote> notes{
            {.at = 0, .on = true, .pitch = 60}, {.at = 0, .on = true, .pitch = 63}, {.at = 0, .on = true, .pitch = 67}};
        constexpr int64_t kArpTotal = int64_t{22} * 2048;  // ~1 s: several arp steps
        const auto large = renderTimed(2048, notes, kArpTotal, true);
        const auto small = renderTimed(64, notes, kArpTotal, true);
        REQUIRE(findPeak(small.data(), small.size()) > 1e-3f);  // not vacuous: the arp plays
        size_t worst = 0;
        const float diff = maxAbsDiff(large, small, worst);
        INFO("arp: max |2048 - 64| " << diff << " at sample " << worst);
        CHECK(diff <= 1e-5f);
    }
}

// The timed-note queue holds kMaxTimedEvents (1024) events per buffer. When a
// buffer carries more, the overflow used to be applied at once while the queued
// events waited for their offsets, so a note-off past the limit could run before
// its own queued note-on, which then played for ever. A full queue must keep the
// events' order (it may give up their timing for that buffer).
TEST_CASE("A buffer with more note events than the timed queue holds leaves no note stuck",
          "[midi][integration][block_size]") {
    // 700 overlapping notes in one 2048-sample buffer, 1400 events: note i starts
    // at sample 2i and stops at 2i + 3, so the queue fills between a note-on and
    // its note-off. Every note is released; the default amp release is short, so
    // two seconds later every voice must have finished and the output be silent.
    std::vector<TimedNote> notes;
    for (int i = 0; i < 700; ++i) {
        const auto pitch = static_cast<int16_t>(36 + (i % 48));
        notes.push_back({.at = int64_t{2} * i, .on = true, .pitch = pitch});
        notes.push_back({.at = int64_t{2} * i + 3, .on = false, .pitch = pitch});
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const TimedNote& a, const TimedNote& b) { return a.at < b.at; });
    constexpr int64_t kTotal = int64_t{2048} + int64_t{2} * 44100;
    const auto out = renderTimed(2048, notes, kTotal, false);
    REQUIRE(findPeak(out.data(), 2048) > 1e-3f);  // not vacuous: the notes played

    // Last 4096 samples, ~1.9 s after the final note-off: a released voice has
    // decayed to nothing (the voice is freed below its silence threshold), while
    // a stuck note sustains at full level (order 0.1).
    const float tail = findPeak(out.data() + kTotal - 4096, 4096);
    INFO("peak of the last 4096 samples " << tail);
    CHECK(tail < 1e-5f);
}
