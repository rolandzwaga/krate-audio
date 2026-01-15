# Shimmer Delay Route-Based Architecture Plan

## Problem Statement

The shimmer delay produces audible crackles/clicks when shimmer mix is between 0% and 100%. Root cause: blending two signals with different latencies causes comb filtering artifacts.

- **Unpitched signal**: 0ms processing latency (direct pass-through)
- **Pitched signal**: ~46ms latency (granular pitch shifter internal buffer)

## Evidence

- 0% shimmer (pure unpitched): **0 clicks** - no artifacts
- 25% shimmer: 13 clicks
- 50% shimmer: 15 clicks
- 75% shimmer: 18 clicks
- 100% shimmer (pure pitched): 30 clicks (from granular crossfades in feedback)

## Solution: Route-Based Architecture

Instead of blending pitched/unpitched at the sample level, use parallel processing paths with proper crossfading.

### Current Architecture (Problematic)

```
Input → ShimmerFeedbackProcessor → Output
              │
              ├─ Store unpitched copy
              ├─ Pitch shift
              └─ Blend: output = unpitched*(1-mix) + pitched*mix  ← CAUSES COMB FILTERING
```

### New Architecture (Route-Based)

```
                    ┌─────────────────────────────────────┐
                    │    FlexibleFeedbackNetwork          │
                    │                                     │
Input ──┬──────────►│  Delay Line  ──┬── Path A (bypass) ─┼──┬──► Output
        │           │                │                    │  │
        │           │                └── Path B (process) ─┼──┤
        │           │                        │            │  │
        │           │              ShimmerProcessor       │  │
        │           │              (pitch shift only)     │  │
        │           │                                     │  │
        │           │  Crossfade controlled by            │  │
        │           │  shimmerMix parameter               │  │
        │           └─────────────────────────────────────┘  │
        │                                                    │
        └───────────────── Feedback ◄────────────────────────┘
```

### Key Design Decisions

1. **Crossfading happens at the FlexibleFeedbackNetwork level**, not inside the processor
2. **Both paths have consistent timing** - Path A is just a pass-through at the same "logical time"
3. **Equal-power crossfade** prevents volume dips during transitions
4. **Shimmer mix becomes a routing parameter**, not a blend parameter

## Implementation Plan

### Phase 1: FlexibleFeedbackNetwork Changes

#### 1.1 Add Parallel Path Support

Add new member variables:
```cpp
// Parallel processing for shimmer mix routing
std::vector<float> bypassL_;      // Path A: unprocessed feedback
std::vector<float> bypassR_;
float processorRouteMix_ = 1.0f;  // 0 = bypass, 1 = full processor
OnePoleSmoother routeMixSmoother_;
```

#### 1.2 Modify process() Method

```cpp
void process(...) {
    // ... existing delay loop code ...

    // After delay loop, we have feedbackL_/feedbackR_ buffers

    if (processor_) {
        // Copy feedback to bypass buffer BEFORE processing
        std::copy(feedbackL_.begin(), feedbackL_.begin() + numSamples, bypassL_.begin());
        std::copy(feedbackR_.begin(), feedbackR_.begin() + numSamples, bypassR_.begin());

        // Process through injected processor
        std::copy(feedbackL_.begin(), feedbackL_.begin() + numSamples, processedL_.begin());
        std::copy(feedbackR_.begin(), feedbackR_.begin() + numSamples, processedR_.begin());
        processor_->process(processedL_.data(), processedR_.data(), numSamples);

        // Route-based crossfade (NOT sample-level blend of different-latency signals)
        // Both bypassL_ and processedL_ represent the SAME time point
        // because bypass is just unprocessed, not "original input"
        for (std::size_t i = 0; i < numSamples; ++i) {
            const float routeMix = routeMixSmoother_.process();

            // Equal-power crossfade
            const float bypassGain = std::cos(routeMix * kPi * 0.5f);
            const float processGain = std::sin(routeMix * kPi * 0.5f);

            feedbackL_[i] = bypassL_[i] * bypassGain + processedL_[i] * processGain;
            feedbackR_[i] = bypassR_[i] * bypassGain + processedR_[i] * processGain;
        }
    }

    // ... rest of processing (filter, limiter, output) ...
}
```

#### 1.3 Add Route Mix API

```cpp
/// @brief Set the routing mix between bypass and processor
/// @param mix 0 = full bypass, 100 = full processor
void setProcessorRouteMix(float mix) noexcept {
    processorRouteMix_ = std::clamp(mix / 100.0f, 0.0f, 1.0f);
    routeMixSmoother_.setTarget(processorRouteMix_);
}
```

### Phase 2: ShimmerFeedbackProcessor Changes

#### 2.1 Remove Internal Blending

The processor should ONLY do pitch shifting + diffusion, no shimmer mix blending:

```cpp
void process(float* left, float* right, std::size_t numSamples) noexcept override {
    if (numSamples == 0) return;

    // Apply pitch shifting (always, no bypass)
    pitchShifterL_.process(left, left, numSamples);
    pitchShifterR_.process(right, right, numSamples);

    // Apply diffusion if enabled
    if (diffusionAmount_ > 0.001f) {
        diffusion_.process(left, right, diffusionOutL_.data(), diffusionOutR_.data(), numSamples);
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i] = left[i] * (1.0f - diffusionAmount_) + diffusionOutL_[i] * diffusionAmount_;
            right[i] = right[i] * (1.0f - diffusionAmount_) + diffusionOutR_[i] * diffusionAmount_;
        }
    }

    // NO shimmer mix blending here - that's now handled by FlexibleFeedbackNetwork
}
```

#### 2.2 Remove Unused Members

Remove from ShimmerFeedbackProcessor:
- `unpitchedL_`, `unpitchedR_` buffers
- `shimmerMix_`, `shimmerMixSmoother_`
- `latencyCompL_`, `latencyCompR_` delay lines
- `setShimmerMix()`, `snapShimmerMix()` methods

### Phase 3: ShimmerDelay Changes

#### 3.1 Update Shimmer Mix Routing

Change `setShimmerMix()` to route through the feedback network:

```cpp
void ShimmerDelay::setShimmerMix(float percent) noexcept {
    shimmerMix_ = std::clamp(percent, kMinShimmerMix, kMaxShimmerMix);
    // Route through feedback network instead of processor
    feedbackNetwork_.setProcessorRouteMix(shimmerMix_);
}
```

#### 3.2 Remove Duplicate Smoothing

The shimmer mix smoothing now happens in FlexibleFeedbackNetwork, so remove:
- `shimmerMixSmoother_` from ShimmerDelay
- References in `snapParameters()`, `reset()`, `prepare()`

### Phase 4: Testing

1. **Run existing shimmer delay tests** - Verify no regressions
2. **Run artifact detection test** - Should now pass with ≤2 clicks at all shimmer levels
3. **Run pluginval** - Verify plugin still validates

## Files to Modify

| File | Changes |
|------|---------|
| `flexible_feedback_network.h` | Add parallel path support, route mix API |
| `shimmer_delay.h` | Remove internal blending from ShimmerFeedbackProcessor, update ShimmerDelay |
| `shimmer_delay_test.cpp` | Update tests if API changes, verify artifact test passes |

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Breaking existing shimmer behavior | Run full test suite before/after |
| Introducing new artifacts | Test at all shimmer mix levels (0%, 25%, 50%, 75%, 100%) |
| Performance regression | Profile before/after - extra buffer copy is minimal |

## Success Criteria

1. Artifact test passes: ≤2 clicks at all shimmer mix levels (0%, 25%, 50%, 75%, 100%)
2. All existing shimmer delay tests pass
3. Pluginval validation passes
4. Audible quality: Clean shimmer sound at all mix levels

## References

- [Valhalla Shimmer: How it's made](https://valhalladsp.com/2010/05/11/enolanois-shimmer-sound-how-it-is-made/)
- [Shimmer: Modulation and Decorrelation](https://valhalladsp.com/2010/05/12/shimmer-modulation-auto-correlation-and-decorrelation/)
- [Low Latency Pitch Shifting](https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html)
