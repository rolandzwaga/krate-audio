# Research: Ducking Delay

**Feature**: 032-ducking-delay
**Date**: 2025-12-26

## Research Summary

This document captures research findings and design decisions for the Ducking Delay feature.

## Key Findings

### 1. DuckingProcessor Already Fully Implemented

**Finding**: DuckingProcessor (Layer 2) at `src/dsp/processors/ducking_processor.h` provides all core ducking functionality:

```cpp
class DuckingProcessor {
public:
    // Key methods
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] float processSample(float main, float sidechain) noexcept;
    void process(const float* main, const float* sidechain, float* output, size_t numSamples) noexcept;

    // Parameters
    void setThreshold(float dB) noexcept;        // -60 to 0 dB
    void setDepth(float dB) noexcept;            // -48 to 0 dB
    void setAttackTime(float ms) noexcept;       // 0.1 to 500 ms
    void setReleaseTime(float ms) noexcept;      // 1 to 5000 ms
    void setHoldTime(float ms) noexcept;         // 0 to 1000 ms
    void setSidechainFilterEnabled(bool) noexcept;
    void setSidechainFilterCutoff(float hz) noexcept;  // 20 to 500 Hz

    // Metering
    [[nodiscard]] float getCurrentGainReduction() const noexcept;
};
```

**Impact**: DuckingDelay Layer 4 feature reuses this directly - no new ducking logic needed.

### 2. Parameter Mapping Considerations

**Finding**: Spec FR-003 uses "duck amount" as percentage (0-100%), but DuckingProcessor uses "depth" in dB (-48 to 0 dB).

**Decision**: Implement mapping at Layer 4:
```cpp
// Convert user-facing percentage to internal dB
// 0% → 0 dB (no attenuation)
// 100% → -48 dB (full attenuation per FR-004)
float percentToDepth(float percent) {
    return -48.0f * (percent / 100.0f);
}
```

**Rationale**: User-facing percentage is more intuitive than dB for "duck amount".

### 3. Target Selection Architecture

**Finding**: Spec requires three ducking targets (FR-010 to FR-013):
1. **Output Only**: Duck delay wet signal before dry/wet mix
2. **Feedback Only**: Duck feedback path but NOT initial delay tap
3. **Both**: Duck both simultaneously

**Analysis of FlexibleFeedbackNetwork**:
- Has processor injection via `setProcessor(IFeedbackProcessor*)`
- Processor is applied to feedback signal AFTER delay read, BEFORE re-entry
- Output of `process()` IS what feeds back on next block

**Decision**: Use FlexibleFeedbackNetwork + external ducking:
- For **Output ducking**: Apply DuckingProcessor AFTER FFN::process(), BEFORE dry/wet mix
- For **Feedback ducking**: Apply DuckingProcessor to FFN output, which becomes next block's feedback
- For **Both**: Apply to both paths

**Key Insight**: Since FFN's output feeds back on the next block, ducking the output naturally ducks the feedback. But for "Feedback Only", we need to:
1. Store unducked output for user to hear
2. Apply ducking to what becomes feedback

**Architecture**:
```
Input ─────┬──────► [FlexibleFeedbackNetwork] ──┬──► (unducked output)
           │                                    │
           │                  ┌─────────────────┘
           │                  │
           │     ┌────────────┴────────────────┐
           │     │                             │
           │     ▼                             ▼
           │  [Store copy      [DuckingProcessor (sidechain: input)]
           │   if Feedback                     │
           │   Only mode]                      ▼
           │                             (ducked output)
           └──► Sidechain ─────────────────────┘
```

For "Feedback Only": Copy FFN output before ducking, return unducked copy to user, ducked version feeds back.

### 4. Delay Time Ranges

**Finding**: DuckingProcessor supports wider ranges than spec:
- Attack: 0.1-500ms (spec: 0.1-100ms)
- Release: 1-5000ms (spec: 10-2000ms)
- Hold: 0-1000ms (spec: 0-500ms)

**Decision**: Clamp to spec ranges in Layer 4 for user-facing parameters:
```cpp
static constexpr float kMinAttackMs = 0.1f;
static constexpr float kMaxAttackMs = 100.0f;   // Spec FR-006

static constexpr float kMinReleaseMs = 10.0f;   // Spec FR-007
static constexpr float kMaxReleaseMs = 2000.0f;

static constexpr float kMinHoldMs = 0.0f;
static constexpr float kMaxHoldMs = 500.0f;     // Spec FR-008
```

### 5. Layer 4 Pattern Reference

**Finding**: ShimmerDelay, ReverseDelay, and FreezeMode all follow similar patterns:
1. Compose FlexibleFeedbackNetwork for delay + feedback
2. Add custom processor or wrapper logic
3. Expose user-facing parameters with smoothing
4. Handle dry/wet mixing externally

**Impact**: DuckingDelay follows established pattern - low risk architecture.

## Alternatives Considered

### Duck Target Implementation

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| Custom delay implementation | Full control | Duplicates FFN logic | Rejected |
| Modify FFN to expose feedback | Clean separation | Invasive change to FFN | Rejected |
| Process output with copy for feedback | No FFN changes | Slight memory overhead | **Selected** |

### Sidechain Source

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| External sidechain input | Pro flexibility | Adds complexity | Rejected (future work) |
| Input as sidechain | Simple, intuitive | Less flexible | **Selected** |

## Dependencies Verified

| Component | Version/Location | Status |
|-----------|------------------|--------|
| DuckingProcessor | src/dsp/processors/ducking_processor.h | Verified, full API documented |
| FlexibleFeedbackNetwork | src/dsp/systems/flexible_feedback_network.h | Verified, process/filter/freeze API present |
| MultimodeFilter | src/dsp/processors/multimode_filter.h | Verified (used by FFN) |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Verified, for parameter smoothing |
| BlockContext | src/dsp/core/block_context.h | Verified, for tempo sync |

## API Signatures Verified

| Dependency | Method | Exact Signature |
|------------|--------|-----------------|
| DuckingProcessor | processSample | `[[nodiscard]] float processSample(float main, float sidechain) noexcept` |
| DuckingProcessor | setThreshold | `void setThreshold(float dB) noexcept` |
| DuckingProcessor | setDepth | `void setDepth(float dB) noexcept` |
| DuckingProcessor | getCurrentGainReduction | `[[nodiscard]] float getCurrentGainReduction() const noexcept` |
| FlexibleFeedbackNetwork | process | `void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx) noexcept` |
| FlexibleFeedbackNetwork | setFilterEnabled | `void setFilterEnabled(bool enabled) noexcept` |
| FlexibleFeedbackNetwork | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` |
| FlexibleFeedbackNetwork | setDelayTimeMs | `void setDelayTimeMs(float ms) noexcept` |
| FlexibleFeedbackNetwork | setFeedbackAmount | `void setFeedbackAmount(float amount) noexcept` |

## Open Questions

All questions resolved through codebase research. No blocking unknowns remain.

## References

- DuckingProcessor implementation: `src/dsp/processors/ducking_processor.h`
- FlexibleFeedbackNetwork implementation: `src/dsp/systems/flexible_feedback_network.h`
- ShimmerDelay reference: `src/dsp/features/shimmer_delay.h`
- FreezeMode reference: `src/dsp/features/freeze_mode.h`
- Spec: `specs/032-ducking-delay/spec.md`
