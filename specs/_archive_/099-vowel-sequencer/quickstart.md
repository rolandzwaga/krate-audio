# Quickstart: VowelSequencer Implementation

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Implementation Checklist

### Phase 1: SequencerCore (Layer 1) - EXTRACT FIRST

```
1. [ ] Create header: dsp/include/krate/dsp/primitives/sequencer_core.h
2. [ ] Write failing tests: dsp/tests/unit/primitives/sequencer_core_tests.cpp
3. [ ] Implement Direction enum
4. [ ] Implement prepare/reset/isPrepared
5. [ ] Implement tick() with Forward direction
6. [ ] Implement Backward direction
7. [ ] Implement PingPong direction
8. [ ] Implement Random direction (no repeat)
9. [ ] Implement swing timing
10. [ ] Implement PPQ sync
11. [ ] Implement gate length with isGateActive/getGateRampValue
12. [ ] Verify all SequencerCore tests pass
13. [ ] Build: cmake --build build/windows-x64-release --config Release --target dsp_tests
14. [ ] Commit: "feat(dsp): add SequencerCore - extract timing logic from FilterStepSequencer"
```

### Phase 2: VowelSequencer (Layer 3) - NEW FEATURE

```
1. [ ] Create header: dsp/include/krate/dsp/systems/vowel_sequencer.h
2. [ ] Write failing tests: dsp/tests/unit/systems/vowel_sequencer_tests.cpp
3. [ ] Implement VowelStep struct
4. [ ] Implement VowelSequencer constructor with default pattern
5. [ ] Implement prepare/reset/isPrepared
6. [ ] Implement step configuration methods
7. [ ] Implement setPreset() with "aeiou", "wow", "yeah"
8. [ ] Implement process() with FormantFilter
9. [ ] Implement morph time with LinearRamp
10. [ ] Implement gate bypass (dry at unity)
11. [ ] Verify all VowelSequencer tests pass
12. [ ] Build and run tests
13. [ ] Commit: "feat(dsp): add VowelSequencer - rhythmic vowel effects with tempo sync"
```

### Phase 3: FilterStepSequencer Refactor - BACKWARD COMPAT

```
1. [ ] Run existing tests to capture baseline (33 tests, 181 assertions)
2. [ ] Add SequencerCore member to FilterStepSequencer
3. [ ] Replace timing logic with SequencerCore delegation
4. [ ] Add using Direction = SequencerCore::Direction for backward compat
5. [ ] Verify all 33 existing tests pass
6. [ ] Commit: "refactor(dsp): FilterStepSequencer uses SequencerCore"
```

### Phase 4: Documentation

```
1. [ ] Update specs/_architecture_/layer-1-primitives.md
2. [ ] Update specs/_architecture_/layer-3-systems.md
3. [ ] Commit: "docs: add SequencerCore and VowelSequencer to architecture docs"
```

## File Creation Order

```
# Phase 1: SequencerCore
dsp/include/krate/dsp/primitives/sequencer_core.h
dsp/tests/unit/primitives/sequencer_core_tests.cpp

# Phase 2: VowelSequencer
dsp/include/krate/dsp/systems/vowel_sequencer.h
dsp/tests/unit/systems/vowel_sequencer_tests.cpp

# Phase 3: FilterStepSequencer (modify existing)
dsp/include/krate/dsp/systems/filter_step_sequencer.h
```

## Build Commands

```bash
# Configure (if not already done)
"/c/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP tests
"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests
./build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run specific test tags
./build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[sequencer_core]"
./build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer]"
./build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"
```

## Key Implementation Patterns

### 1. SequencerCore tick() Pattern

```cpp
[[nodiscard]] bool SequencerCore::tick() noexcept {
    if (!prepared_) return false;

    bool stepChanged = false;

    // Advance sample counter
    ++sampleCounter_;

    // Check for step change
    if (sampleCounter_ >= stepDurationSamples_) {
        sampleCounter_ = 0;
        advanceStep();
        updateStepDuration();  // Recalc with swing for new step
        stepChanged = true;
    }

    // Update gate state
    bool wasGateActive = gateActive_;
    gateActive_ = (sampleCounter_ < gateDurationSamples_);

    // Update gate ramp
    if (gateActive_ != wasGateActive) {
        gateRamp_.setTarget(gateActive_ ? 1.0f : 0.0f);
    }

    return stepChanged;
}
```

### 2. VowelSequencer process() Pattern

```cpp
[[nodiscard]] float VowelSequencer::process(float input) noexcept {
    if (!prepared_) return 0.0f;

    // Tick sequencer
    if (sequencer_.tick()) {
        // Step changed - update targets
        int step = sequencer_.getCurrentStep();
        applyStepParameters(step);
    }

    // Process morph ramp
    float morphPos = morphRamp_.process();
    formantFilter_.setVowelMorph(morphPos);
    formantFilter_.setFormantShift(currentFormantShift_);

    // Apply formant filter
    float wet = formantFilter_.process(input);

    // Apply gate (dry at unity, wet fades)
    float gateValue = gateRamp_.process();
    return wet * gateValue + input;
}
```

### 3. Swing Timing Formula

```cpp
float SequencerCore::applySwingToStep(int stepIndex, float baseDuration) const noexcept {
    if (swing_ <= 0.0f) return baseDuration;

    // Even steps (0, 2, 4, ...) are longer
    // Odd steps (1, 3, 5, ...) are shorter
    bool isOddStep = (stepIndex % 2 == 1);

    if (isOddStep) {
        return baseDuration * (1.0f - swing_);  // Shorter
    } else {
        return baseDuration * (1.0f + swing_);  // Longer
    }
}
```

### 4. Random No-Repeat Pattern

```cpp
int SequencerCore::calculateNextStep() noexcept {
    // ... other cases ...

    case Direction::Random:
        if (numSteps_ <= 1) {
            return 0;
        }
        // Rejection sampling with xorshift32
        int next;
        do {
            rngState_ ^= rngState_ << 13;
            rngState_ ^= rngState_ >> 17;
            rngState_ ^= rngState_ << 5;
            next = static_cast<int>(rngState_ % static_cast<uint32_t>(numSteps_));
        } while (next == currentStep_);
        return next;
}
```

### 5. PingPong Step Calculation

```cpp
int SequencerCore::calculatePingPongStep(double stepsIntoPattern) const noexcept {
    if (numSteps_ <= 1) return 0;

    // PingPong cycle: 0,1,2,3,2,1,0,1,2,3,2,1,...
    // Cycle length = 2 * (N - 1) for N steps
    int cycleLength = 2 * (static_cast<int>(numSteps_) - 1);
    int posInCycle = static_cast<int>(std::fmod(stepsIntoPattern, static_cast<double>(cycleLength)));
    if (posInCycle < 0) posInCycle += cycleLength;

    // First half: ascending, Second half: descending
    if (posInCycle < static_cast<int>(numSteps_)) {
        return posInCycle;
    } else {
        return cycleLength - posInCycle;
    }
}
```

### 6. Preset Loading

```cpp
bool VowelSequencer::setPreset(const char* name) noexcept {
    if (std::strcmp(name, "aeiou") == 0) {
        setNumSteps(5);
        setStepVowel(0, Vowel::A);
        setStepVowel(1, Vowel::E);
        setStepVowel(2, Vowel::I);
        setStepVowel(3, Vowel::O);
        setStepVowel(4, Vowel::U);
        return true;
    }
    if (std::strcmp(name, "wow") == 0) {
        setNumSteps(3);
        setStepVowel(0, Vowel::O);
        setStepVowel(1, Vowel::A);
        setStepVowel(2, Vowel::O);
        return true;
    }
    if (std::strcmp(name, "yeah") == 0) {
        setNumSteps(3);
        setStepVowel(0, Vowel::I);
        setStepVowel(1, Vowel::E);
        setStepVowel(2, Vowel::A);
        return true;
    }
    return false;  // Unknown preset
}
```

## Test Tags

| Tag | Component | Use |
|-----|-----------|-----|
| `[sequencer_core]` | SequencerCore | All timing tests |
| `[sequencer_core][timing]` | SequencerCore | Step duration accuracy |
| `[sequencer_core][direction]` | SequencerCore | Direction modes |
| `[sequencer_core][swing]` | SequencerCore | Swing timing |
| `[sequencer_core][gate]` | SequencerCore | Gate control |
| `[vowel_sequencer]` | VowelSequencer | All vowel tests |
| `[vowel_sequencer][preset]` | VowelSequencer | Preset loading |
| `[vowel_sequencer][morph]` | VowelSequencer | Vowel morphing |
| `[filter_step_sequencer]` | FilterStepSequencer | Existing regression |

## Critical Test Cases

### SC-001: Timing Accuracy (1ms tolerance)

```cpp
TEST_CASE("SequencerCore timing accuracy - SC-001", "[sequencer_core][timing]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setTempo(120.0f);
    core.setNoteValue(NoteValue::Quarter);
    core.setNumSteps(4);

    // At 120 BPM, 1/4 note = 500ms = 22050 samples
    const size_t expectedSamples = 22050;
    const float tolerance = 44.1f;  // 1ms

    // Count samples until step change
    size_t count = 0;
    while (!core.tick() && count < 30000) {
        ++count;
    }

    REQUIRE(count == Approx(expectedSamples).margin(tolerance));
}
```

### SC-004: Swing Ratio (3:1 at 50%)

```cpp
TEST_CASE("SequencerCore swing ratio - SC-004", "[sequencer_core][swing]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setTempo(120.0f);
    core.setNoteValue(NoteValue::Eighth);
    core.setSwing(0.5f);
    core.setNumSteps(4);

    // Measure step 0 (even, longer)
    size_t step0Samples = 0;
    while (!core.tick()) ++step0Samples;

    // Measure step 1 (odd, shorter)
    size_t step1Samples = 0;
    while (!core.tick()) ++step1Samples;

    float ratio = static_cast<float>(step0Samples) / static_cast<float>(step1Samples);
    REQUIRE(ratio >= 2.9f);
    REQUIRE(ratio <= 3.1f);
}
```

### SC-003: Click-Free Transitions

```cpp
TEST_CASE("VowelSequencer no clicks - SC-003", "[vowel_sequencer][morph]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setTempo(300.0f);
    seq.setNoteValue(NoteValue::Sixteenth);

    std::array<float, 10000> buffer;
    std::fill(buffer.begin(), buffer.end(), 0.5f);

    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = seq.process(buffer[i]);
    }

    // Max sample-to-sample difference should be small (no clicks)
    float maxDiff = 0.0f;
    for (size_t i = 1; i < buffer.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(buffer[i] - buffer[i-1]));
    }

    REQUIRE(maxDiff < 0.5f);
}
```

## Common Mistakes to Avoid

1. **Forgetting to call configure() on LinearRamp before setTarget()**
   - Call `ramp.configure(timeMs, sampleRate)` in `prepare()`

2. **Using wrong morph range for FormantFilter**
   - setVowelMorph() uses 0-4 range: A=0, E=1, I=2, O=3, U=4

3. **Incorrect swing application in PingPong**
   - Swing applies to step INDEX, not playback position
   - Step 2 is always "even" regardless of direction

4. **Gate crossfade direction**
   - VowelSequencer: `wet * gate + input` (dry always unity)
   - FilterStepSequencer: `wet * gate + dry * (1-gate)` (crossfade)

5. **Missing Direction backward compat**
   - FilterStepSequencer must keep `using Direction = SequencerCore::Direction;`
