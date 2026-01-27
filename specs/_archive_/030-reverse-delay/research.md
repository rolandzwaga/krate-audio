# Research: Reverse Delay Mode

**Feature**: 030-reverse-delay
**Date**: 2025-12-26

## Research Questions

### Q1: Double-Buffer vs Single-Buffer Architecture

**Question**: How should ReverseBuffer manage capture and playback simultaneously?

**Decision**: Double-buffer (ping-pong) architecture

**Rationale**:
- Single buffer requires complex index management for simultaneous read/write
- Double buffer provides clean separation: one captures, one plays
- Crossfade between buffers eliminates clicks at swap points
- Standard technique used in granular synthesis and time-stretching

**Alternatives Considered**:
1. Single circular buffer with reverse read pointer - complex, prone to glitches
2. Triple buffer (read, write, crossfade) - more memory, no significant benefit

### Q2: Crossfade Implementation

**Question**: How to implement smooth transitions between chunks?

**Decision**: Equal-power crossfade using sine/cosine curves

**Rationale**:
- Linear crossfade causes 3dB dip at center (50% point)
- Equal-power maintains constant perceived loudness
- Formula: `gain_out = cos(pos * PI/2)`, `gain_in = sin(pos * PI/2)`
- At crossfade midpoint: both gains = ~0.707, sum of squares = 1

**Alternatives Considered**:
1. Linear crossfade - simpler but audible level dip
2. Cubic crossfade - not constant power
3. S-curve crossfade - works but sine/cosine is standard

### Q3: FlexibleFeedbackNetwork Integration

**Question**: Can we use FlexibleFeedbackNetwork for feedback management?

**Decision**: Yes - use FlexibleFeedbackNetwork with injected ReverseFeedbackProcessor

**Rationale**:
- FlexibleFeedbackNetwork supports IFeedbackProcessor injection via setProcessor()
- ReverseFeedbackProcessor implements IFeedbackProcessor, wrapping stereo ReverseBuffer pair
- Follows established ShimmerDelay pattern for consistency and reusability
- FlexibleFeedbackNetwork handles feedback loop, filtering, and limiting - no need to duplicate
- Delay line set to minimum (reverse buffer provides timing)

**Implementation**:
```cpp
// In ReverseDelay::prepare()
feedbackNetwork_.setProcessor(&reverseProcessor_);
feedbackNetwork_.setDelayTimeMs(kMinDelayMs);  // minimal delay
feedbackNetwork_.setProcessorMix(100.0f);       // 100% processed (always reverse)
```

**Alternatives Rejected**:
1. Direct component composition - duplicates FlexibleFeedbackNetwork logic, violates FR-015
2. Modify FlexibleFeedbackNetwork - invasive, breaks other users
3. New FlexibleReverseFeedbackNetwork class - unnecessary abstraction

### Q4: Playback Mode State Machine

**Question**: Where should playback mode decisions be made?

**Decision**: Store mode in ReverseDelay, query at each chunk boundary

**Rationale**:
- Mode changes should take effect at next chunk (FR-014)
- ReverseBuffer handles mechanics, ReverseDelay handles policy
- Random mode needs RNG state which is better at feature level

**Implementation**:
```cpp
enum class PlaybackMode : uint8_t {
    FullReverse,  // Every chunk reversed
    Alternating,  // Forward, reverse, forward, reverse...
    Random        // 50/50 random per chunk
};

// At chunk boundary:
bool shouldReverse = false;
switch (mode_) {
    case PlaybackMode::FullReverse:
        shouldReverse = true;
        break;
    case PlaybackMode::Alternating:
        shouldReverse = (chunkIndex_ % 2 == 0);
        break;
    case PlaybackMode::Random:
        shouldReverse = (rng_() % 2 == 0);
        break;
}
```

### Q5: Chunk Size Parameter Smoothing

**Question**: How to handle chunk size changes during playback?

**Decision**: Apply new chunk size at next chunk boundary only

**Rationale**:
- Changing chunk size mid-chunk would cause discontinuity
- Smoother controls the transition but actual size change waits for boundary
- Target vs current value distinction similar to pitch smoother in ShimmerDelay

**Implementation**:
```cpp
// Set target (user action)
void setChunkSizeMs(float ms) {
    targetChunkMs_ = clamp(ms, kMinChunkMs, kMaxChunkMs);
    chunkSizeSmoother_.setTarget(targetChunkMs_);
}

// At chunk boundary
void onChunkBoundary() {
    // Update active chunk size from smoothed value
    activeChunkMs_ = chunkSizeSmoother_.getCurrentValue();
    activeChunkSamples_ = msToSamples(activeChunkMs_);
}
```

## Component Dependencies

```
ReverseDelay (Layer 4)
├── FlexibleFeedbackNetwork (Layer 3) - feedback loop, filter, limiter
│   └── ReverseFeedbackProcessor (Layer 2) - injected via setProcessor()
│       ├── ReverseBuffer (Layer 1) - left channel
│       └── ReverseBuffer (Layer 1) - right channel
├── OnePoleSmoother (Layer 1) × N (parameters)
├── BlockContext (Layer 0) - tempo sync
└── db_utils.h (Layer 0) - gain conversion
```

## Performance Considerations

1. **Memory**: Double buffers at max chunk size (2000ms) at 192kHz = ~768KB per channel
   - Total stereo: ~1.5MB - acceptable for delay plugin

2. **CPU**: Primary operations per sample:
   - Buffer write: O(1)
   - Reverse read: O(1) (index arithmetic)
   - Crossfade: 2 multiplies + 1 add when active
   - Feedback: 1 multiply per sample

3. **Latency**: Equal to chunk size (capture before playback)
   - Must report via getLatencySamples()
   - Host PDC compensates

## References

- Roadmap section 4.7: Reverse Delay Mode
- FlexibleFeedbackNetwork implementation (feedback patterns)
- ShimmerDelay implementation (Layer 4 feature pattern)
- DelayLine implementation (buffer patterns)
