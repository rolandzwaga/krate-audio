# API Contract: BandProcessor Oversampling Extensions

**Location**: `plugins/disrumpo/src/dsp/band_processor.h` (modifications to existing class)
**Namespace**: `Disrumpo`

## Overview

Extensions to the existing `BandProcessor` class to support intelligent oversampling with morph-weighted factor computation and smooth crossfade transitions.

## New Public API

### `requestOversampleFactor()`

```cpp
/// @brief Request a new oversampling factor with smooth transition.
///
/// If the requested factor differs from the current factor, initiates an
/// 8ms equal-power crossfade transition. If a transition is already in
/// progress, aborts it and starts a new transition from the current state.
///
/// Per spec FR-010: 8ms crossfade with dual-path processing
/// Per spec FR-017: Only triggers if factor actually changes (hysteresis)
///
/// @param factor Target oversampling factor (1, 2, 4, or 8)
void requestOversampleFactor(int factor) noexcept;
```

### `recalculateOversampleFactor()`

```cpp
/// @brief Recalculate oversampling factor from morph state.
///
/// Call after morph position changes, morph node type changes, or
/// global limit changes. Computes the morph-weighted factor and
/// requests a transition if the result differs from the current factor.
///
/// Per spec FR-003, FR-004, FR-017
void recalculateOversampleFactor() noexcept;
```

### `isOversampleTransitioning()`

```cpp
/// @brief Check if an oversampling crossfade transition is in progress.
/// @return true if crossfade is active
[[nodiscard]] bool isOversampleTransitioning() const noexcept;
```

## Modified Public API

### `setMaxOversampleFactor()` (MODIFIED)

```cpp
/// @brief Set the maximum oversampling factor (global limit).
///
/// MODIFIED: Now triggers recalculation and potential crossfade transition
/// when the clamped factor changes.
///
/// @param factor Maximum factor (1, 2, 4, or 8)
void setMaxOversampleFactor(int factor) noexcept;
```

### `setDistortionType()` (MODIFIED)

```cpp
/// @brief Set the distortion type for this band.
///
/// MODIFIED: Now triggers oversampling factor recalculation through
/// recalculateOversampleFactor() instead of directly setting the factor.
///
/// @param type The distortion type from DistortionType enum
void setDistortionType(DistortionType type) noexcept;
```

### `setMorphPosition()` (MODIFIED)

```cpp
/// @brief Set morph cursor position.
///
/// MODIFIED: After setting morph position, triggers oversampling factor
/// recalculation. Only initiates crossfade if computed factor changes.
///
/// @param x X position [0, 1]
/// @param y Y position [0, 1]
void setMorphPosition(float x, float y) noexcept;
```

### `setMorphNodes()` (MODIFIED)

```cpp
/// @brief Set morph nodes for this band.
///
/// MODIFIED: After setting nodes, triggers oversampling factor
/// recalculation since node types may have changed.
///
/// @param nodes Array of morph nodes
/// @param activeCount Number of active nodes (2-4)
void setMorphNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept;
```

### `processBlock()` (MODIFIED)

```cpp
/// @brief Process stereo buffer in-place with oversampling.
///
/// MODIFIED: Added crossfade path. When crossfadeActive_ is true,
/// runs both old and new oversampling paths and blends per-sample
/// using equal-power gains. Advances crossfade progress per sample.
///
/// @param left Left channel buffer
/// @param right Right channel buffer
/// @param numSamples Number of samples per channel
void processBlock(float* left, float* right, size_t numSamples) noexcept;
```

## New Private Members

```cpp
// Crossfade state
int targetOversampleFactor_ = kDefaultOversampleFactor;
int crossfadeOldFactor_ = kDefaultOversampleFactor;
float crossfadeProgress_ = 0.0f;
float crossfadeIncrement_ = 0.0f;
bool crossfadeActive_ = false;

// Pre-allocated crossfade buffers
std::array<float, kMaxBlockSize> crossfadeOldLeft_{};
std::array<float, kMaxBlockSize> crossfadeOldRight_{};
```

## New Private Methods

### `processBlockWithCrossfade()`

```cpp
/// @brief Process a block during an active oversampling crossfade.
///
/// Runs both old factor and new factor paths in parallel, then blends
/// per-sample using equal-power gains from crossfade_utils.h.
///
/// @param left Left channel buffer (output is blended result)
/// @param right Right channel buffer (output is blended result)
/// @param numSamples Number of samples per channel
void processBlockWithCrossfade(float* left, float* right, size_t numSamples) noexcept;
```

### `processWithFactor()`

```cpp
/// @brief Process a block through a specific oversampling factor path.
///
/// Routes to the correct oversampler (or direct path for 1x) based on
/// the specified factor.
///
/// @param left Left channel input/output buffer
/// @param right Right channel input/output buffer
/// @param numSamples Number of samples
/// @param factor Oversampling factor to use (1, 2, 4, or 8)
void processWithFactor(float* left, float* right, size_t numSamples, int factor) noexcept;
```

### `startCrossfade()`

```cpp
/// @brief Initiate or restart an oversampling crossfade transition.
///
/// Sets up crossfade state for an 8ms equal-power transition from
/// the current factor to the target factor.
///
/// @param newFactor Target oversampling factor
void startCrossfade(int newFactor) noexcept;
```

## Crossfade Processing Flow

```
processBlock(left, right, numSamples):
  if (band is bypassed):
    // FR-012: Pass through bit-transparent
    return

  if (crossfadeActive_):
    processBlockWithCrossfade(left, right, numSamples)
  else:
    processWithFactor(left, right, numSamples, currentOversampleFactor_)


processBlockWithCrossfade(left, right, numSamples):
  // 1. Copy input to old-path buffers
  copy(left -> crossfadeOldLeft_, numSamples)
  copy(right -> crossfadeOldRight_, numSamples)

  // 2. Process old path (writes to crossfadeOldLeft_/Right_)
  processWithFactor(crossfadeOldLeft_, crossfadeOldRight_, numSamples, crossfadeOldFactor_)

  // 3. Process new path (writes to left/right in-place)
  processWithFactor(left, right, numSamples, currentOversampleFactor_)

  // 4. Blend per-sample with equal-power crossfade
  for i in [0, numSamples):
    crossfadeProgress_ += crossfadeIncrement_
    crossfadeProgress_ = min(crossfadeProgress_, 1.0)

    auto [fadeOut, fadeIn] = equalPowerGains(crossfadeProgress_)
    left[i]  = crossfadeOldLeft_[i]  * fadeOut + left[i]  * fadeIn
    right[i] = crossfadeOldRight_[i] * fadeOut + right[i] * fadeIn

    if (crossfadeProgress_ >= 1.0):
      crossfadeActive_ = false
      break  // Remaining samples processed without crossfade

  // 5. Process remaining samples (after crossfade completes mid-block)
  if (!crossfadeActive_ && i < numSamples):
    processWithFactor(left + i, right + i, numSamples - i, currentOversampleFactor_)
```

**Abort-and-Restart Behavior**: If `requestOversampleFactor()` is called while `crossfadeActive_` is true (mid-transition), the next `processBlock()` call will reset `crossfadeProgress_` to 0.0 and set `crossfadeOldFactor_` to the current (partially faded-in) factor. The crossfade loop then starts fresh toward the new target. Abort checking happens at the START of `processBlock()`, not mid-loop, because `requestOversampleFactor()` is called during parameter handling at the beginning of `process()` before audio processing begins.

**Note on `equalPowerGains()`**: `equalPowerGains()` from `crossfade_utils.h` (Layer 0) implements `cos(π/2 * t)` and `sin(π/2 * t)` gains, ensuring `oldGain² + newGain² = 1` per FR-011. The caller MUST clamp `crossfadeProgress_` to [0.0, 1.0] before calling, as the function does NOT clamp internally.

## Thread Safety

- `requestOversampleFactor()`: Called from parameter handling (beginning of `process()`)
- `recalculateOversampleFactor()`: Called from parameter handling (beginning of `process()`)
- `processBlock()`: Called from audio thread only
- All crossfade state is modified and read exclusively on the audio thread
- No atomics or locks needed for crossfade state (single-threaded access)
