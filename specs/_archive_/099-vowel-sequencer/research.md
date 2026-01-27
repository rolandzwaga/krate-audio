# Research: VowelSequencer with SequencerCore Refactor

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Summary

All technical unknowns have been resolved through codebase analysis. The existing FilterStepSequencer implementation provides a clear template for SequencerCore extraction, and FormantFilter provides the vowel sound generation needed for VowelSequencer.

## Research Tasks Completed

### 1. FilterStepSequencer Timing Logic Analysis

**Question**: What specific timing logic needs to be extracted to SequencerCore?

**Finding**: The FilterStepSequencer contains approximately 160 lines of timing-related code:

| Logic Component | Lines | Location |
|-----------------|-------|----------|
| Direction enum | 6 | Lines 64-70 |
| updateStepDuration() | 16 | Lines 687-702 |
| advanceStep() | 6 | Lines 704-710 |
| calculateNextStep() | 47 | Lines 712-761 |
| applySwingToStep() | 14 | Lines 805-819 |
| calculatePingPongStep() | 18 | Lines 821-838 |
| sync() method | 44 | Lines 531-574 |
| Gate logic in process() | ~30 | Lines 606-670 (partial) |
| **Total** | ~160 | |

**Decision**: Extract all of the above to SequencerCore. The gate logic will be split - timing in SequencerCore, audio crossfade in consumer.

### 2. SequencerCore API Design

**Question**: What should SequencerCore's public interface look like?

**Finding**: Based on FilterStepSequencer usage patterns and VowelSequencer requirements:

```cpp
class SequencerCore {
public:
    // Direction enum (moved from FilterStepSequencer)
    enum class Direction : uint8_t { Forward, Backward, PingPong, Random };

    // Constants
    static constexpr size_t kMaxSteps = 16;
    static constexpr float kMinTempoBPM = 20.0f;
    static constexpr float kMaxTempoBPM = 300.0f;
    static constexpr float kMinSwing = 0.0f;
    static constexpr float kMaxSwing = 1.0f;
    static constexpr float kMinGateLength = 0.0f;
    static constexpr float kMaxGateLength = 1.0f;
    static constexpr float kGateCrossfadeMs = 5.0f;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Configuration
    void setNumSteps(size_t numSteps) noexcept;
    [[nodiscard]] size_t getNumSteps() const noexcept;
    void setTempo(float bpm) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setSwing(float swing) noexcept;
    void setGateLength(float gateLength) noexcept;
    void setDirection(Direction direction) noexcept;
    [[nodiscard]] Direction getDirection() const noexcept;

    // Transport
    void sync(double ppqPosition) noexcept;
    void trigger() noexcept;
    [[nodiscard]] int getCurrentStep() const noexcept;

    // Per-sample processing
    [[nodiscard]] bool tick() noexcept;  // Returns true when step changes
    [[nodiscard]] bool isGateActive() const noexcept;
    [[nodiscard]] float getGateRampValue() noexcept;  // 0-1 for crossfade

private:
    // State extracted from FilterStepSequencer
    // ... (see implementation)
};
```

**Decision**: Use tick() method that returns step change events. Consumers handle their own parameter glides based on step changes.

### 3. Gate Crossfade Behavior Comparison

**Question**: How should VowelSequencer gate behavior differ from FilterStepSequencer?

**Finding**: Spec clarification resolved this:

| Aspect | FilterStepSequencer | VowelSequencer (spec) |
|--------|---------------------|----------------------|
| Gate ON | wet=1.0, dry=0.0 | wet=1.0, dry=0.0 |
| Gate OFF | wet=0.0, dry=1.0 | wet=0.0, dry=1.0 |
| Crossfade | Equal power crossfade | Wet fades, dry at unity |
| Formula | `wet*g + dry*(1-g)` | `wet*g + dry` |

**Decision**: VowelSequencer implements `output = wet * gateRamp + input` (dry always at unity). This differs from FilterStepSequencer's equal-power crossfade but matches the spec clarification.

### 4. FormantFilter Morph Integration

**Question**: How should VowelSequencer use FormantFilter's morph functionality?

**Finding**: FormantFilter provides two modes:
1. `setVowel(Vowel)` - Discrete vowel selection
2. `setVowelMorph(float)` - Continuous morph (0-4 range)

For smooth vowel transitions:
```cpp
// On step change:
float sourcePos = static_cast<float>(static_cast<uint8_t>(previousVowel));
float targetPos = static_cast<float>(static_cast<uint8_t>(currentVowel));
morphRamp_.setTarget(targetPos);

// Per sample:
float morphPos = morphRamp_.process();
formantFilter_.setVowelMorph(morphPos);
```

**Decision**: Use setVowelMorph() with LinearRamp for smooth transitions. Also apply per-step formantShift via setFormantShift().

### 5. Swing Behavior Verification

**Question**: Does swing apply to step indices regardless of direction?

**Finding**: Spec clarification confirmed: "Swing applies to step indices (even steps long, odd steps short) regardless of direction."

From FilterStepSequencer:
```cpp
inline float FilterStepSequencer::applySwingToStep(int stepIndex, float baseDuration) const noexcept {
    if (swing_ <= 0.0f) return baseDuration;
    bool isOddStep = (stepIndex % 2 == 1);
    if (isOddStep) {
        return baseDuration * (1.0f - swing_);  // Shorter
    } else {
        return baseDuration * (1.0f + swing_);  // Longer
    }
}
```

**Decision**: Copy exact formula to SequencerCore. Step index parity determines swing, not playback position.

### 6. Preset Pattern Design

**Question**: What presets should VowelSequencer provide?

**Finding**: Based on spec:

| Preset | Pattern | numSteps |
|--------|---------|----------|
| "aeiou" | A, E, I, O, U | 5 |
| "wow" | O, A, O | 3 |
| "yeah" | I, E, A | 3 |
| (default) | A, E, I, O, U, O, I, E | 8 |

**Decision**: Implement setPreset(const char* name) returning bool. Unknown presets return false, pattern unchanged.

### 7. Random Direction No-Repeat Algorithm

**Question**: How does FilterStepSequencer prevent immediate repetition in Random mode?

**Finding**: From FilterStepSequencer calculateNextStep():
```cpp
case Direction::Random:
    if (numSteps_ <= 1) {
        next = 0;
    } else {
        // Rejection sampling with xorshift PRNG
        do {
            rngState_ ^= rngState_ << 13;
            rngState_ ^= rngState_ >> 17;
            rngState_ ^= rngState_ << 5;
            next = static_cast<int>(rngState_ % static_cast<uint32_t>(numSteps_));
        } while (next == currentStep_);
    }
    break;
```

**Decision**: Copy exact xorshift PRNG algorithm to SequencerCore for consistency.

### 8. PPQ Sync PingPong Calculation

**Question**: How is PingPong step calculated from PPQ position?

**Finding**: From FilterStepSequencer:
```cpp
inline int FilterStepSequencer::calculatePingPongStep(double stepsIntoPattern) const noexcept {
    if (numSteps_ <= 1) return 0;
    // PingPong cycle length: 2 * (N - 1) for N steps
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

**Decision**: Copy exact algorithm to SequencerCore.

## Alternatives Considered

### Alternative 1: SequencerCore as Template Class

**Considered**: Template SequencerCore on step data type to allow FilterStepSequencer and VowelSequencer to share more code.

**Rejected Because**:
- Adds complexity without significant benefit
- Step data (filter params vs vowel) have very different structures
- Current composition approach is simpler and sufficient

### Alternative 2: Direction Enum at Layer 0

**Considered**: Put Direction enum in core/ since it's used by multiple systems.

**Rejected Because**:
- Direction is sequencer-specific, not general-purpose
- Keeping it in SequencerCore (Layer 1) maintains cohesion
- FilterStepSequencer can still access via include

### Alternative 3: Morph Time in SequencerCore

**Considered**: Add generic morph/glide time handling to SequencerCore.

**Rejected Because**:
- Different consumers have different morph targets (filter params vs vowel position)
- SequencerCore should only handle timing, not parameter interpolation
- Consumers use LinearRamp directly for their specific needs

## Technology Choices

| Choice | Decision | Rationale |
|--------|----------|-----------|
| SequencerCore Location | Layer 1 primitive | Must be usable by multiple Layer 3 systems |
| Tick Return Type | bool (step changed) | Simple, consumers query getCurrentStep() |
| Gate Ramp | External LinearRamp | Consistent with FilterStepSequencer pattern |
| Vowel Morph | FormantFilter.setVowelMorph() | Leverages existing smooth interpolation |
| PRNG | xorshift32 | Fast, adequate randomness, matches existing |

## Dependencies Verified

All dependencies exist and APIs are verified:

- [x] FormantFilter::setVowelMorph(float) - verified in header
- [x] FormantFilter::setFormantShift(float) - verified in header
- [x] LinearRamp::configure/setTarget/process - verified in header
- [x] NoteValue/NoteModifier enums - verified in header
- [x] getBeatsForNote() - verified in header
- [x] Vowel enum - verified in header
- [x] BlockContext::tempoBPM - verified in header

## Outstanding Questions

None. All clarifications resolved in spec.
