// ==============================================================================
// Unit Tests: MidiNoteDelay (Layer 2 Processor)
// ==============================================================================
// Tests for the MIDI echo post-processor for arpeggiator output.
// Reference: Gradus MIDI Delay Lane plan
// ==============================================================================

#include <krate/dsp/processors/midi_note_delay.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

static BlockContext makeCtx(double sampleRate = 44100.0, size_t blockSize = 512,
                            double tempo = 120.0)
{
    BlockContext ctx{};
    ctx.sampleRate = sampleRate;
    ctx.blockSize = blockSize;
    ctx.tempoBPM = tempo;
    ctx.isPlaying = true;
    return ctx;
}

/// Create a single NoteOn ArpEvent at given sample offset.
static ArpEvent makeNoteOn(uint8_t note, uint8_t velocity, int32_t sampleOffset = 0)
{
    return ArpEvent{ArpEvent::Type::NoteOn, note, velocity, sampleOffset, false};
}

/// Collect all events across multiple blocks, adjusting offsets to absolute positions.
static std::vector<ArpEvent> collectEchoes(MidiNoteDelay& delay, BlockContext& ctx,
                                           size_t numBlocks,
                                           const ArpEvent* initialEvent = nullptr,
                                           size_t currentStep = 0)
{
    std::vector<ArpEvent> allEvents;
    std::array<ArpEvent, 512> output;

    for (size_t b = 0; b < numBlocks; ++b) {
        std::array<ArpEvent, 1> input;
        size_t inputCount = 0;

        // Only provide the initial event in block 0
        if (b == 0 && initialEvent != nullptr) {
            input[0] = *initialEvent;
            inputCount = 1;
        }

        size_t count = delay.process(ctx,
            std::span<const ArpEvent>(input.data(), inputCount),
            inputCount,
            std::span<ArpEvent>(output.data(), output.size()),
            currentStep);

        for (size_t i = 0; i < count; ++i) {
            ArpEvent evt = output[i];
            evt.sampleOffset += static_cast<int32_t>(b * ctx.blockSize);
            allEvents.push_back(evt);
        }
    }
    return allEvents;
}

/// Filter events by type.
static std::vector<ArpEvent> filterByType(const std::vector<ArpEvent>& events,
                                          ArpEvent::Type type)
{
    std::vector<ArpEvent> filtered;
    for (const auto& e : events) {
        if (e.type == type) filtered.push_back(e);
    }
    return filtered;
}

// =============================================================================
// Basic Functionality
// =============================================================================

TEST_CASE("MidiNoteDelay: no echoes when feedback is 0", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    // Default config has feedbackCount = 0
    auto noteOn = makeNoteOn(60, 100, 0);
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(&noteOn, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    // Only the pass-through event
    REQUIRE(count == 1);
    CHECK(output[0].type == ArpEvent::Type::NoteOn);
    CHECK(output[0].note == 60);
    CHECK(output[0].velocity == 100);
    CHECK(delay.pendingCount() == 0);
}

TEST_CASE("MidiNoteDelay: pass-through preserves all input events", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    std::array<ArpEvent, 3> input = {{
        {ArpEvent::Type::NoteOn, 60, 100, 0, false},
        {ArpEvent::Type::NoteOff, 60, 0, 100, false},
        {ArpEvent::Type::NoteOn, 64, 80, 200, false},
    }};
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(input.data(), input.size()), input.size(),
        std::span<ArpEvent>(output.data(), output.size()), 0);

    REQUIRE(count >= 3);
    CHECK(output[0].type == ArpEvent::Type::NoteOn);
    CHECK(output[0].note == 60);
    CHECK(output[1].type == ArpEvent::Type::NoteOff);
    CHECK(output[1].note == 60);
    CHECK(output[2].type == ArpEvent::Type::NoteOn);
    CHECK(output[2].note == 64);
}

TEST_CASE("MidiNoteDelay: NoteOff events do not generate echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    MidiDelayStepConfig config;
    config.active = true;
    config.feedbackCount = 3;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;
    delay.setStepConfig(0, config);

    ArpEvent noteOff{ArpEvent::Type::NoteOff, 60, 0, 0, false};
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(&noteOff, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(count == 1); // Only pass-through
    CHECK(delay.pendingCount() == 0);
}

// =============================================================================
// Echo Timing (Free Mode)
// =============================================================================

TEST_CASE("MidiNoteDelay: single echo at correct time (free mode)", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;  // 100ms = 4410 samples at 44100Hz
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;  // No decay — echo same velocity
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);

    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    // Should have 2 NoteOns: original + 1 echo
    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 0);        // Original
    CHECK(noteOns[1].sampleOffset == 4410);      // Echo at 100ms
    CHECK(noteOns[1].note == 60);
    CHECK(noteOns[1].velocity == 100);           // No decay
}

TEST_CASE("MidiNoteDelay: multiple echoes with correct spacing", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;   // 50ms = 2205 samples
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4); // Original + 3 echoes
    CHECK(noteOns[0].sampleOffset == 0);
    CHECK(noteOns[1].sampleOffset == 2205);  // 50ms
    CHECK(noteOns[2].sampleOffset == 4410);  // 100ms
    CHECK(noteOns[3].sampleOffset == 6615);  // 150ms
}

TEST_CASE("MidiNoteDelay: echo from non-zero source offset", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;  // 2205 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 256); // Source at sample 256
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 256);
    CHECK(noteOns[1].sampleOffset == 256 + 2205); // 256 + 50ms
}

// =============================================================================
// Echo Timing (Synced Mode)
// =============================================================================

TEST_CASE("MidiNoteDelay: tempo-synced echo timing", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512, 120.0); // 120 BPM

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Synced;
    config.noteValueIndex = 10;  // 1/8 note = 250ms at 120 BPM = 11025 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 40, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 0);
    CHECK(noteOns[1].sampleOffset == 11025); // 1/8 at 120 BPM
}

TEST_CASE("MidiNoteDelay: dotted note value timing", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512, 120.0);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Synced;
    config.noteValueIndex = 11;  // 1/8D = 375ms at 120 BPM = 16537 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 60, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 0);
    // 1/8 dotted = 0.75 beats = 375ms = 16537.5 → 16537 samples
    CHECK(noteOns[1].sampleOffset == Catch::Approx(16537).margin(1));
}

// =============================================================================
// Velocity Decay
// =============================================================================

TEST_CASE("MidiNoteDelay: velocity decay per echo", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 4;
    config.velocityDecay = 0.5f;  // Each echo = prev * 0.5
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 5); // Original + 4 echoes
    CHECK(noteOns[0].velocity == 100);  // Original
    CHECK(noteOns[1].velocity == 50);   // 100 * 0.5^1
    CHECK(noteOns[2].velocity == 25);   // 100 * 0.5^2
    CHECK(noteOns[3].velocity == 13);   // 100 * 0.5^3 ≈ 12.5 → 13
    CHECK(noteOns[4].velocity == 6);    // 100 * 0.5^4 = 6.25 → 6
}

TEST_CASE("MidiNoteDelay: zero decay means no velocity change", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 80, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    for (const auto& e : noteOns) {
        CHECK(e.velocity == 80);
    }
}

TEST_CASE("MidiNoteDelay: full decay stops echoes early", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 16;
    config.velocityDecay = 1.0f;  // (1 - 1.0)^N = 0 for N >= 1
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    // Only the original — all echoes would have velocity 0
    REQUIRE(noteOns.size() == 1);
}

// =============================================================================
// Pitch Shift
// =============================================================================

TEST_CASE("MidiNoteDelay: pitch shift per echo", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = 7;  // Perfect fifth per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 60);
    CHECK(noteOns[1].note == 67);  // +7
    CHECK(noteOns[2].note == 74);  // +14
    CHECK(noteOns[3].note == 81);  // +21
}

TEST_CASE("MidiNoteDelay: negative pitch shift", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = -12;  // Octave down per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 60);
    CHECK(noteOns[1].note == 48);  // -12
    CHECK(noteOns[2].note == 36);  // -24
    CHECK(noteOns[3].note == 24);  // -36
}

TEST_CASE("MidiNoteDelay: pitch clamped to MIDI range", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = 24;  // +24 per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(100, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 100);
    CHECK(noteOns[1].note == 124); // 100 + 24
    CHECK(noteOns[2].note == 127); // Clamped (100 + 48 = 148 → 127)
    CHECK(noteOns[3].note == 127); // Clamped
}

TEST_CASE("MidiNoteDelay: pitch clamped at bottom", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = -24;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(20, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 20);
    CHECK(noteOns[1].note == 0);   // 20 - 24 → clamped to 0
    CHECK(noteOns[2].note == 0);
    CHECK(noteOns[3].note == 0);
}

// =============================================================================
// Gate Scaling
// =============================================================================

TEST_CASE("MidiNoteDelay: NoteOff emitted after gate duration", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;  // 2205 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    config.gateScaling = 1.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOffs = filterByType(events, ArpEvent::Type::NoteOff);

    // Should have at least 1 NoteOff for the echo
    REQUIRE(noteOffs.size() >= 1);

    // Find the echo's NoteOff (not the original's)
    // Echo NoteOn at sample 2205, gate = 80% of 2205 = 1764
    // Echo NoteOff at 2205 + 1764 = 3969
    bool foundEchoNoteOff = false;
    for (const auto& e : noteOffs) {
        if (e.sampleOffset > 2000) {
            foundEchoNoteOff = true;
            CHECK(e.note == 60);
            CHECK(e.sampleOffset == Catch::Approx(2205 + 1764).margin(2));
        }
    }
    CHECK(foundEchoNoteOff);
}

TEST_CASE("MidiNoteDelay: gate scaling shrinks echo gates", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;  // 4410 samples
    config.feedbackCount = 2;
    config.velocityDecay = 0.0f;
    config.gateScaling = 0.5f;  // Each echo's gate halves
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 40, &noteOn, 0);
    auto noteOffs = filterByType(events, ArpEvent::Type::NoteOff);

    // Base gate = 80% of 4410 = 3528
    // Echo 1 gate = 3528 * 0.5 = 1764 → NoteOff at 4410 + 1764 = 6174
    // Echo 2 gate = 3528 * 0.25 = 882 → NoteOff at 8820 + 882 = 9702
    int echoNoteOffCount = 0;
    for (const auto& e : noteOffs) {
        if (e.sampleOffset > 4000 && e.sampleOffset < 7000) {
            CHECK(e.sampleOffset == Catch::Approx(6174).margin(2));
            ++echoNoteOffCount;
        }
        if (e.sampleOffset > 9000 && e.sampleOffset < 10500) {
            CHECK(e.sampleOffset == Catch::Approx(9702).margin(2));
            ++echoNoteOffCount;
        }
    }
    CHECK(echoNoteOffCount == 2);
}

// =============================================================================
// Per-Step Configuration
// =============================================================================

TEST_CASE("MidiNoteDelay: different steps use different configs", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    // Step 0: 2 echoes
    MidiDelayStepConfig config0;
    config0.active = true;
    config0.timeMode = TimeMode::Free;
    config0.delayTimeMs = 50.0f;
    config0.feedbackCount = 2;
    config0.velocityDecay = 0.0f;
    delay.setStepConfig(0, config0);

    // Step 1: no echoes (bypass)
    MidiDelayStepConfig config1;
    config1.feedbackCount = 0;
    delay.setStepConfig(1, config1);

    // Process with step 0
    auto noteOn0 = makeNoteOn(60, 100, 0);
    auto events0 = collectEchoes(delay, ctx, 20, &noteOn0, 0);
    auto noteOns0 = filterByType(events0, ArpEvent::Type::NoteOn);
    CHECK(noteOns0.size() == 3); // Original + 2 echoes

    // Reset and process with step 1
    delay.reset();
    auto noteOn1 = makeNoteOn(64, 100, 0);
    auto events1 = collectEchoes(delay, ctx, 20, &noteOn1, 1);
    auto noteOns1 = filterByType(events1, ArpEvent::Type::NoteOn);
    CHECK(noteOns1.size() == 1); // Only original (no echoes)
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("MidiNoteDelay: reset clears pending echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 500.0f;  // Long delay
    config.feedbackCount = 8;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    // Schedule echoes
    auto noteOn = makeNoteOn(60, 100, 0);
    std::array<ArpEvent, 64> output;
    delay.process(ctx, std::span<const ArpEvent>(&noteOn, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(delay.pendingCount() > 0);

    delay.reset();
    CHECK(delay.pendingCount() == 0);
}

// =============================================================================
// Overflow / Capacity
// =============================================================================

TEST_CASE("MidiNoteDelay: overflow steals oldest echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 1000.0f;  // Long delay to keep echoes pending
    config.feedbackCount = 16;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    // Send many notes to overwhelm the buffer (256 max)
    std::array<ArpEvent, 64> output;
    for (int i = 0; i < 20; ++i) {
        auto noteOn = makeNoteOn(60 + static_cast<uint8_t>(i % 12), 100, 0);
        delay.process(ctx, std::span<const ArpEvent>(&noteOn, 1), 1,
            std::span<ArpEvent>(output.data(), output.size()), 0);
    }

    // Should not exceed max capacity
    CHECK(delay.pendingCount() <= MidiNoteDelay::kMaxPendingEchoes);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("MidiNoteDelay: minimum delay time clamped", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 1.0f;  // Below minimum (10ms)
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 10, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    // 10ms minimum = 441 samples at 44100
    CHECK(noteOns[1].sampleOffset == 441);
}

TEST_CASE("MidiNoteDelay: empty input produces no output initially", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>{}, 0,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(count == 0);
}

TEST_CASE("MidiNoteDelay: max feedback count is 16", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 10.0f;  // 441 samples — short enough to fit in ~20 blocks
    config.feedbackCount = 100;  // Way above max — should clamp to 16
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 40, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    // Original + 16 echoes = 17
    CHECK(noteOns.size() == 17);
}

TEST_CASE("MidiNoteDelay: step config out of range uses clamped step", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.feedbackCount = 1;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(31, config);  // Last valid step

    // Access step 100 (out of range) — should clamp to step 31
    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 100);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    CHECK(noteOns.size() == 2); // Original + 1 echo (from step 31's config)
}

TEST_CASE("MidiNoteDelay: kSkip events pass through without echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    MidiDelayStepConfig config;
    config.active = true;
    config.feedbackCount = 3;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    delay.setStepConfig(0, config);

    ArpEvent skipEvent{ArpEvent::Type::kSkip, 5, 0, 0, false};
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(&skipEvent, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(count == 1);
    CHECK(output[0].type == ArpEvent::Type::kSkip);
    CHECK(delay.pendingCount() == 0);
}

// =============================================================================
// Combined Parameters
// =============================================================================

TEST_CASE("MidiNoteDelay: combined pitch shift + velocity decay", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.3f;
    config.pitchShiftPerRepeat = 12;  // Octave up per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(48, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);

    // Check pitch progression
    CHECK(noteOns[0].note == 48);
    CHECK(noteOns[1].note == 60);  // +12
    CHECK(noteOns[2].note == 72);  // +24
    CHECK(noteOns[3].note == 84);  // +36

    // Check velocity decay: 100 * 0.7^N
    CHECK(noteOns[0].velocity == 100);
    CHECK(noteOns[1].velocity == 70);   // 100 * 0.7
    CHECK(noteOns[2].velocity == 49);   // 100 * 0.49
    CHECK(noteOns[3].velocity == 34);   // 100 * 0.343 ≈ 34
}

// =============================================================================
// Multi-Block Spanning
// =============================================================================

TEST_CASE("MidiNoteDelay: echoes span multiple process blocks", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 256); // Small blocks

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;  // 4410 samples = ~17 blocks of 256
    config.feedbackCount = 2;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 50, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 3);
    CHECK(noteOns[0].sampleOffset == 0);
    CHECK(noteOns[1].sampleOffset == Catch::Approx(4410).margin(1));
    CHECK(noteOns[2].sampleOffset == Catch::Approx(8820).margin(1));
}

// =============================================================================
// Tempo Changes (Synced Mode)
// =============================================================================

TEST_CASE("MidiNoteDelay: different tempo changes synced delay time", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Synced;
    config.noteValueIndex = 13;  // 1/4 note
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    // At 120 BPM: 1/4 = 500ms = 22050 samples
    {
        auto ctx = makeCtx(44100.0, 512, 120.0);
        auto noteOn = makeNoteOn(60, 100, 0);
        auto events = collectEchoes(delay, ctx, 80, &noteOn, 0);
        auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);
        REQUIRE(noteOns.size() == 2);
        CHECK(noteOns[1].sampleOffset == 22050);
    }

    delay.reset();

    // At 60 BPM: 1/4 = 1000ms = 44100 samples
    {
        auto ctx = makeCtx(44100.0, 512, 60.0);
        auto noteOn = makeNoteOn(60, 100, 0);
        auto events = collectEchoes(delay, ctx, 120, &noteOn, 0);
        auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);
        REQUIRE(noteOns.size() == 2);
        CHECK(noteOns[1].sampleOffset == 44100);
    }
}
