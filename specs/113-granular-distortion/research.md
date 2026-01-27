# Research: Granular Distortion Processor

**Date**: 2026-01-27 | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

## Research Questions and Findings

### 1. Grain-to-Density Mapping

**Question**: How to map the density parameter [1-8] to GrainScheduler's grains/second?

**Analysis**: The spec says density represents "approximately simultaneous grains." With grain sizes from 5-100ms, we need to calculate grains/second to achieve the desired overlap.

**Formula derivation**:
- If we want `density` grains overlapping simultaneously
- And each grain lasts `grainSizeMs` milliseconds
- Then trigger interval = `grainSizeMs / density` milliseconds
- Therefore grains/second = `density * 1000 / grainSizeMs`

**Example**:
- grainSize = 50ms, density = 4
- grains/second = 4 * 1000 / 50 = 80 grains/second
- Each grain lasts 50ms, with 80/sec triggering, we get ~4 overlapping

**Decision**: Calculate grains/second dynamically: `grainsPerSecond = density * 1000.0f / grainSizeMs`

**Rationale**: This maintains consistent overlap regardless of grain size. The GrainScheduler already handles timing jitter internally.

**Alternatives considered**:
- Fixed grains/second regardless of size - rejected, breaks overlap semantics
- Fixed trigger interval - rejected, same issue

---

### 2. Buffer Size Calculation

**Question**: Exact buffer size needed for 100ms at 192kHz?

**Analysis**:
- Maximum sample rate: 192,000 Hz
- Maximum grain duration: 100ms = 0.1 seconds
- Maximum position jitter: 50ms = 0.05 seconds
- Total lookback needed: 100ms + 50ms = 150ms

**Calculation**:
```cpp
// At 192kHz:
// 150ms * 192000 = 28,800 samples minimum
// Round up to power of 2: 32,768 samples (for efficient modulo)
```

**Decision**: Pre-allocate 32,768 samples (170ms at 192kHz, sufficient for all scenarios)

**Rationale**: Power-of-2 sizing enables efficient bit-masking for circular buffer wraparound. Slight overallocation is acceptable given < 256KB total budget.

**Alternatives considered**:
- Exact 150ms calculation per sample rate - rejected, complicates code
- Smaller buffer with runtime checks - rejected, could cause artifacts

---

### 3. Position Jitter Clamping

**Question**: How to track available buffer history for dynamic clamping?

**Analysis**: The spec requires FR-024-NEW: "Position jitter offsets MUST be clamped dynamically based on available buffer history."

**State tracking needed**:
- `samplesWritten_` counter incremented each sample written
- Available history = min(samplesWritten_, bufferSize_ - 1)
- Effective jitter = min(requestedJitter, availableHistory)

**Decision**: Track write count, clamp jitter per-grain at trigger time

```cpp
// At grain trigger:
const size_t availableHistory = std::min(samplesWritten_, bufferSize_ - 1);
const size_t maxJitterSamples = availableHistory;
const size_t requestedJitterSamples = static_cast<size_t>(positionJitterMs_ * sampleRate_ / 1000.0f);
const size_t effectiveJitterSamples = std::min(requestedJitterSamples, maxJitterSamples);
```

**Rationale**: Simple, O(1), no additional buffer state needed beyond write counter.

**Alternatives considered**:
- Track high-water mark separately - rejected, unnecessary complexity
- Disable jitter entirely when buffer not full - rejected, overly restrictive

---

### 4. Grain Completion Detection

**Question**: Best approach to detect grain completion and release?

**Analysis**: Looking at existing GrainProcessor pattern:
```cpp
// From grain_processor.h
[[nodiscard]] bool isGrainComplete(const Grain& grain) const noexcept {
    return grain.envelopePhase >= 1.0f;
}
```

**Decision**: Use same pattern - check `envelopePhase >= 1.0f` and release grain back to pool

**Processing loop pattern**:
```cpp
for (Grain* grain : grainPool_.activeGrains()) {
    // Process grain...

    // Check for completion
    grain->envelopePhase += grain->envelopeIncrement;
    if (grain->envelopePhase >= 1.0f) {
        grainPool_.releaseGrain(grain);
    }
}
```

**Rationale**: Consistent with existing granular infrastructure. Envelope phase naturally tracks completion.

**Alternatives considered**:
- Separate completion flag - rejected, redundant with phase
- Sample counter per grain - rejected, more state to track

---

### 5. Per-Grain Waveshaper Management

**Question**: How to associate Waveshaper instances with grains?

**Analysis**: FR-043 specifies each grain MUST embed its own Waveshaper instance. The existing Grain struct doesn't have waveshaper fields.

**Options**:
1. Extend Grain struct (ODR risk, modifies shared component)
2. Parallel array of Waveshapers indexed by grain
3. Nested DistortionGrain struct containing Grain + Waveshaper

**Decision**: Use parallel array approach - `std::array<Waveshaper, 64>` alongside GrainPool

**Rationale**:
- Avoids modifying shared Grain struct (ODR prevention)
- Simple indexing: grain index maps directly to waveshaper
- Memory is contiguous, cache-friendly
- Waveshaper is trivially copyable, no complex lifecycle

**Implementation**:
```cpp
class GranularDistortion {
    // Existing grain pool for allocation/timing
    GrainPool grainPool_;

    // Parallel array of per-grain waveshapers
    std::array<Waveshaper, GrainPool::kMaxGrains> waveshapers_;

    // Additional per-grain state (drive, buffer offset)
    struct GrainState {
        float drive = 1.0f;
        size_t bufferOffset = 0;  // Frozen position in circular buffer
    };
    std::array<GrainState, GrainPool::kMaxGrains> grainStates_;
};
```

**Alternatives considered**:
- Single shared Waveshaper reconfigured per grain - rejected, spec prohibits
- Waveshaper inside Grain struct - rejected, would modify Layer 1 component

---

### 6. Algorithm Variation Selection

**Question**: How to select random waveshape types for algorithmVariation?

**Analysis**: WaveshapeType enum has 9 values (0-8). Need uniform random selection.

**Decision**: Use Xorshift32::nextUnipolar() scaled to enum range

```cpp
// Select random algorithm type
if (algorithmVariation_) {
    const float r = rng_.nextUnipolar();  // [0, 1]
    const int typeIndex = static_cast<int>(r * 9.0f);  // [0, 8]
    waveshaper.setType(static_cast<WaveshapeType>(std::min(typeIndex, 8)));
} else {
    waveshaper.setType(baseDistortionType_);
}
```

**Rationale**: Simple, uniform distribution across all 9 types.

**Alternatives considered**:
- Weighted selection favoring certain types - rejected, spec doesn't require
- Exclude certain types - rejected, spec says "randomly select from available"

---

### 7. Circular Buffer Read Pattern

**Question**: How should grains read from the circular buffer with frozen offset?

**Analysis**: FR-046-NEW specifies grains store start position and read with frozen offset.

**Pattern**:
```cpp
// At grain trigger:
grainState.bufferOffset = writePos_ - jitteredPosition;  // Freeze this position

// During grain processing:
// Position advances based on envelope progress, but RELATIVE to frozen offset
const size_t readPos = (grainState.bufferOffset + sampleIndex) & bufferMask_;
const float sample = buffer_[readPos];
```

**Wait - this needs clarification**: Does "frozen offset" mean:
1. Grain reads from a fixed position throughout its lifetime (static read)?
2. Grain starts at fixed position but advances through buffer (streaming read)?

**Spec says**: "Grain stores start position only and reads from circular buffer with frozen offset"

**Interpretation**: The START position is frozen at trigger time. The grain then reads sequentially from that position forward (or backward for reverse). This is standard granular synthesis behavior.

**Decision**: Store frozen start position, advance read position during grain playback

```cpp
// At trigger:
grainState.startBufferPos = writePos_ - jitteredOffset;

// Each sample:
const float phase = grain->envelopePhase;  // 0 to 1
const size_t grainSampleIndex = static_cast<size_t>(phase * grainSizeSamples);
const size_t readPos = (grainState.startBufferPos + grainSampleIndex) & bufferMask_;
```

**Rationale**: Matches spec language "frozen offset" (the offset is frozen, not the entire read).

---

### 8. NaN/Inf Handling Strategy

**Question**: What specific behavior for FR-034 (handle NaN/Inf inputs)?

**Analysis**: Looking at existing processors (AliasingEffect, BitcrusherProcessor):
```cpp
// From aliasing_effect.h
if (detail::isNaN(input) || detail::isInf(input)) {
    reset();
    return 0.0f;
}
```

**Decision**: Same pattern - reset state and return 0.0f

**Rationale**: Consistent with codebase conventions. Prevents corruption propagation.

---

## Memory Budget Analysis

| Component | Size | Count | Total |
|-----------|------|-------|-------|
| Circular buffer | 4 bytes | 32,768 | 131,072 bytes |
| Waveshaper array | ~12 bytes | 64 | 768 bytes |
| GrainState array | ~12 bytes | 64 | 768 bytes |
| GrainPool (internal) | ~40 bytes | 64 | 2,560 bytes |
| Envelope table | 4 bytes | 2,048 | 8,192 bytes |
| Smoothers (4x) | ~20 bytes | 4 | 80 bytes |
| Other state | — | — | ~200 bytes |
| **Total** | — | — | **~143,640 bytes (~140KB)** |

**Conclusion**: Well under 256KB budget (SC-007-MEM). Approved.

---

## Performance Considerations

### Per-Sample Operations

1. Write to circular buffer: O(1)
2. Check scheduler for trigger: O(1)
3. Process active grains: O(activeGrains)
   - Envelope lookup: O(1)
   - Buffer read: O(1)
   - Waveshaper: O(1)
4. Parameter smoothing: O(1)
5. Mix dry/wet: O(1)

**Worst case**: 64 active grains, each doing buffer read + waveshape + envelope = ~200 ops/sample

**At 44.1kHz**: 200 * 44100 = 8.8M ops/second (well within 0.5% CPU budget)

---

## Key Design Decisions Summary

| Decision | Rationale |
|----------|-----------|
| Buffer size: 32,768 samples | Power-of-2 for efficient masking, covers 150ms at 192kHz |
| Density formula: `density * 1000 / grainSizeMs` | Maintains consistent overlap |
| Parallel waveshaper array | Avoids modifying shared Grain struct |
| Track samplesWritten_ for jitter clamping | Simple O(1) solution |
| Store frozen start position per grain | Standard granular synthesis pattern |
| NaN/Inf: reset() + return 0.0f | Consistent with codebase |
