// ==============================================================================
// Layer 2: Processor Tests - Pattern Scheduler
// ==============================================================================
// Unit tests for PatternScheduler (spec 069 - Pattern Freeze Mode).
//
// Tests verify:
// - Pattern-based slice triggering
// - Tempo synchronization
// - Step sequencing
// - Callback invocation
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/pattern_scheduler.h>
#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/block_context.h>

#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Helper: Trigger Recorder
// =============================================================================

class TriggerRecorder {
public:
    void recordTrigger(int step) {
        triggers_.push_back(step);
    }

    size_t getTriggerCount() const { return triggers_.size(); }
    int getTriggerAt(size_t index) const { return triggers_.at(index); }
    void clear() { triggers_.clear(); }

    std::vector<int> triggers_;
};

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("PatternScheduler initializes with default pattern", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    REQUIRE(scheduler.getCurrentStep() == 0);
    REQUIRE(scheduler.getSteps() == PatternFreezeConstants::kDefaultEuclideanSteps);
}

TEST_CASE("PatternScheduler reset clears state", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    // Advance some steps
    scheduler.setPattern(EuclideanPattern::generate(4, 8, 0), 8);
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process to advance
    for (int i = 0; i < 10; ++i) {
        scheduler.process(512, ctx);
    }

    scheduler.reset();
    REQUIRE(scheduler.getCurrentStep() == 0);
}

// =============================================================================
// Pattern Configuration Tests
// =============================================================================

TEST_CASE("PatternScheduler accepts pattern bitmask", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    // Set E(3,8) tresillo pattern
    uint32_t pattern = EuclideanPattern::generate(3, 8, 0);
    scheduler.setPattern(pattern, 8);

    REQUIRE(scheduler.getPattern() == pattern);
    REQUIRE(scheduler.getSteps() == 8);
}

TEST_CASE("PatternScheduler setEuclidean generates correct pattern", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    scheduler.setEuclidean(3, 8, 0);

    uint32_t expected = EuclideanPattern::generate(3, 8, 0);
    REQUIRE(scheduler.getPattern() == expected);
    REQUIRE(scheduler.getSteps() == 8);
}

// =============================================================================
// Trigger Callback Tests
// =============================================================================

TEST_CASE("PatternScheduler invokes callback on hits", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    TriggerRecorder recorder;
    scheduler.setTriggerCallback([&recorder](int step) {
        recorder.recordTrigger(step);
    });

    // Set pattern with all hits (all steps trigger)
    scheduler.setPattern((1u << 4) - 1, 4);  // All 4 steps are hits

    // Set tempo and step duration so we advance through all steps
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Calculate samples per step at 120 BPM for 1/4 note steps
    // At 120 BPM, quarter note = 500ms = 22050 samples
    // If we use 1/16 notes: 125ms = 5512 samples per step
    scheduler.setStepDuration(5512);  // Direct sample count

    // Process enough to advance through all 4 steps
    for (int i = 0; i < 5; ++i) {
        scheduler.process(5512, ctx);
    }

    // Should have triggered on each step (4 steps = 4 triggers + 1 for wraparound)
    REQUIRE(recorder.getTriggerCount() >= 4);
}

TEST_CASE("PatternScheduler does not trigger on rests", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    TriggerRecorder recorder;
    scheduler.setTriggerCallback([&recorder](int step) {
        recorder.recordTrigger(step);
    });

    // Set pattern: only step 0 is a hit (binary: 0001)
    scheduler.setPattern(1, 4);
    scheduler.setStepDuration(1000);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process through all steps
    for (int i = 0; i < 5; ++i) {
        scheduler.process(1000, ctx);
    }

    // Count triggers - should be triggered once per cycle (at step 0)
    // Processing 5 * 1000 samples through 4 steps (1000 each) = ~1.25 cycles
    // Expect at least 1 trigger at step 0
    REQUIRE(recorder.getTriggerCount() >= 1);

    // Verify triggers are only at step 0
    for (size_t i = 0; i < recorder.getTriggerCount(); ++i) {
        REQUIRE(recorder.getTriggerAt(i) == 0);
    }
}

// =============================================================================
// Tempo Sync Tests
// =============================================================================

TEST_CASE("PatternScheduler synchronizes to tempo", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    TriggerRecorder recorder;
    scheduler.setTriggerCallback([&recorder](int step) {
        recorder.recordTrigger(step);
    });

    // Set pattern with hits on all 4 steps
    scheduler.setPattern(0xF, 4);

    // At 120 BPM, 1/16 note = 125ms = 5512.5 samples
    // Set tempo-sync mode
    scheduler.setTempoSync(true, NoteValue::Sixteenth, NoteModifier::None);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process one full cycle (4 steps)
    // 4 steps * 5512 samples = 22050 samples
    recorder.clear();
    size_t totalSamples = 0;
    while (totalSamples < 44100) {  // Process 1 second
        scheduler.process(512, ctx);
        totalSamples += 512;
    }

    // At 120 BPM with 1/16 notes, 1 second = 8 steps = 8 triggers
    REQUIRE(recorder.getTriggerCount() >= 7);  // Allow for timing variance
}

TEST_CASE("PatternScheduler respects tempo changes", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    TriggerRecorder recorder;
    scheduler.setTriggerCallback([&recorder](int step) {
        recorder.recordTrigger(step);
    });

    scheduler.setPattern(0xFF, 8);  // All 8 steps trigger
    scheduler.setTempoSync(true, NoteValue::Eighth, NoteModifier::None);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process at 120 BPM
    recorder.clear();
    for (int i = 0; i < 50; ++i) {
        scheduler.process(512, ctx);
    }
    size_t triggersAt120 = recorder.getTriggerCount();

    // Reset and process at 240 BPM (double tempo)
    scheduler.reset();
    recorder.clear();
    ctx.tempoBPM = 240.0;
    for (int i = 0; i < 50; ++i) {
        scheduler.process(512, ctx);
    }
    size_t triggersAt240 = recorder.getTriggerCount();

    // Double tempo should produce approximately double triggers
    REQUIRE(triggersAt240 >= triggersAt120 * 1.5);  // Allow margin for timing
}

// =============================================================================
// Step Advancement Tests
// =============================================================================

TEST_CASE("PatternScheduler advances through steps", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    scheduler.setPattern(0xFF, 8);
    scheduler.setStepDuration(1000);  // 1000 samples per step

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Start at step 0
    REQUIRE(scheduler.getCurrentStep() == 0);

    // Process one step worth of samples
    scheduler.process(1000, ctx);

    // Should have advanced to step 1
    REQUIRE(scheduler.getCurrentStep() == 1);

    // Process 7 more steps
    for (int i = 0; i < 7; ++i) {
        scheduler.process(1000, ctx);
    }

    // Should have wrapped to step 0
    REQUIRE(scheduler.getCurrentStep() == 0);
}

TEST_CASE("PatternScheduler wraps at pattern end", "[processors][pattern_scheduler][layer2]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    scheduler.setPattern(1, 3);  // 3 steps
    scheduler.setStepDuration(100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process through multiple cycles
    for (int i = 0; i < 100; ++i) {
        scheduler.process(100, ctx);
    }

    // Should be at a valid step (0, 1, or 2)
    int step = scheduler.getCurrentStep();
    REQUIRE(step >= 0);
    REQUIRE(step < 3);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("PatternScheduler handles zero-length blocks", "[processors][pattern_scheduler][layer2][edge]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    scheduler.setPattern(0xFF, 8);
    scheduler.setStepDuration(1000);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    int stepBefore = scheduler.getCurrentStep();

    // Process zero samples
    scheduler.process(0, ctx);

    // Step should not change
    REQUIRE(scheduler.getCurrentStep() == stepBefore);
}

TEST_CASE("PatternScheduler handles empty pattern", "[processors][pattern_scheduler][layer2][edge]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    TriggerRecorder recorder;
    scheduler.setTriggerCallback([&recorder](int step) {
        recorder.recordTrigger(step);
    });

    // Empty pattern (no hits)
    scheduler.setPattern(0, 8);
    scheduler.setStepDuration(100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process through all steps
    for (int i = 0; i < 16; ++i) {
        scheduler.process(100, ctx);
    }

    // No triggers should occur
    REQUIRE(recorder.getTriggerCount() == 0);
}

TEST_CASE("PatternScheduler handles single step pattern", "[processors][pattern_scheduler][layer2][edge]") {
    PatternScheduler scheduler;
    scheduler.prepare(44100.0, 512);

    TriggerRecorder recorder;
    scheduler.setTriggerCallback([&recorder](int step) {
        recorder.recordTrigger(step);
    });

    // Single step pattern (must use minimum 2 steps)
    scheduler.setPattern(1, 2);
    scheduler.setStepDuration(100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    // Process several cycles
    for (int i = 0; i < 10; ++i) {
        scheduler.process(100, ctx);
    }

    // Should have triggered multiple times (at step 0)
    REQUIRE(recorder.getTriggerCount() >= 4);
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("PatternScheduler process is noexcept", "[processors][pattern_scheduler][layer2][realtime]") {
    PatternScheduler scheduler;
    BlockContext ctx;
    static_assert(noexcept(scheduler.process(512, ctx)),
                  "process() must be noexcept");
}
