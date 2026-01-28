# API Contract: MorphEngine

**Feature**: 005-morph-system | **Date**: 2026-01-28

---

## Overview

MorphEngine is the DSP system that computes morph weights and processes audio through weighted distortion blends. It is designed for integration with BandProcessor.

---

## Class: MorphEngine

**Location**: `plugins/disrumpo/src/dsp/morph_engine.h`
**Layer**: Plugin-specific DSP (composition layer)
**Dependencies**: Krate::DSP::OnePoleSmoother, Krate::DSP::equalPowerGains

---

## Lifecycle Methods

### prepare()

```cpp
/// @brief Initialize the morph engine for a given sample rate.
///
/// Pre-allocates all buffers and configures smoothers. Must be called
/// before any processing.
///
/// @param sampleRate Sample rate in Hz (typically 44100, 48000, 96000)
/// @param maxBlockSize Maximum block size for processing (typically 512-2048)
///
/// @pre sampleRate > 0
/// @pre maxBlockSize > 0
/// @post Engine ready for process() calls
/// @post All smoothers configured
/// @post Distortion adapters prepared
void prepare(double sampleRate, size_t maxBlockSize) noexcept;
```

### reset()

```cpp
/// @brief Reset all internal state without reallocation.
///
/// Clears smoother history, resets transition state, but preserves
/// configuration (nodes, mode, smoothing time).
///
/// @post Smoothers reset to snap mode
/// @post Transition zone state cleared
/// @post Processing state cleared
void reset() noexcept;
```

---

## Configuration Methods

### setNodes()

```cpp
/// @brief Set the morph nodes for this engine.
///
/// Copies node configuration. Up to 4 nodes supported.
/// Nodes beyond activeNodeCount are ignored.
///
/// @param nodes Array of up to 4 MorphNode structures
/// @param activeCount Number of active nodes (2-4)
///
/// @pre 2 <= activeCount <= 4
/// @pre nodes contains valid data for indices 0 to activeCount-1
/// @post Internal node array updated
/// @post Distortion types/params configured for each active node
///
/// Thread safety: Safe to call from any thread (atomic update pattern)
void setNodes(const MorphNode* nodes, int activeCount) noexcept;
```

### setMorphPosition()

```cpp
/// @brief Set the morph cursor position.
///
/// Position is smoothed based on configured smoothing time.
/// For automated drivers, use setRawWeights() instead.
///
/// @param x Horizontal position [0, 1]
/// @param y Vertical position [0, 1] (ignored in Linear1D mode)
///
/// @pre 0.0 <= x <= 1.0
/// @pre 0.0 <= y <= 1.0
/// @post Position target set (smoothed over time)
///
/// Thread safety: Safe to call from any thread
void setMorphPosition(float x, float y) noexcept;
```

### setRawWeights()

```cpp
/// @brief Set morph weights directly (for automated drivers).
///
/// Bypasses position-to-weight computation. Weights are smoothed
/// before use. Use for Chaos, Envelope, and other advanced drivers
/// that compute weights directly.
///
/// @param weights Array of 4 weights [0, 1] each
///
/// @pre weights sums to approximately 1.0 (normalized)
/// @post Weight targets set (smoothed over time)
///
/// Thread safety: Safe to call from any thread
void setRawWeights(const float* weights) noexcept;
```

### setMode()

```cpp
/// @brief Set the morph interpolation mode.
///
/// Affects how cursor position maps to node weights.
/// Mode change takes effect immediately.
///
/// @param mode MorphMode enum value
///
/// @post Mode updated
/// @post Weights recomputed on next process() call
void setMode(MorphMode mode) noexcept;
```

### setSmoothingTimeMs()

```cpp
/// @brief Set the morph smoothing time.
///
/// Longer times create slower, smoother transitions.
/// Set to 0 for instant transitions.
///
/// @param ms Smoothing time [0, 500] ms
///
/// @pre 0.0 <= ms <= 500.0
/// @post Smoother coefficient updated
void setSmoothingTimeMs(float ms) noexcept;
```

---

## Processing Methods

### process()

```cpp
/// @brief Process a single stereo sample pair through morphed distortion.
///
/// Computes weights, applies appropriate interpolation strategy
/// (same-family or cross-family), and outputs processed samples.
///
/// @param left Left channel sample (modified in-place)
/// @param right Right channel sample (modified in-place)
///
/// @post Samples processed through weighted distortion blend
///
/// @note Real-time safe: no allocations, no locks
void process(float& left, float& right) noexcept;
```

### processBlock()

```cpp
/// @brief Process a block of stereo samples.
///
/// More efficient than per-sample process() for batch processing.
/// Weights are computed once per block (sample-accurate if needed).
///
/// @param left Left channel buffer (modified in-place)
/// @param right Right channel buffer (modified in-place)
/// @param numSamples Number of samples per channel
///
/// @pre numSamples <= maxBlockSize from prepare()
/// @post Samples processed through weighted distortion blend
///
/// @note Real-time safe: no allocations, no locks
void processBlock(float* left, float* right, size_t numSamples) noexcept;
```

---

## Query Methods

### getWeights()

```cpp
/// @brief Get the current computed morph weights.
///
/// Returns smoothed weights after position-to-weight computation.
/// Useful for UI visualization and sweep-morph linking.
///
/// @return MorphWeights structure with values for each node
///
/// Thread safety: Safe to call from any thread
[[nodiscard]] MorphWeights getWeights() const noexcept;
```

### isSmoothing()

```cpp
/// @brief Check if smoothing is in progress.
///
/// Returns true if position or weight smoothers are still transitioning.
///
/// @return true if any smoother not yet at target
[[nodiscard]] bool isSmoothing() const noexcept;
```

### isCrossFamily()

```cpp
/// @brief Check if current morph involves cross-family processing.
///
/// Cross-family processing uses parallel distortion instances with
/// equal-power crossfade. Same-family uses parameter interpolation.
///
/// @return true if nodes span multiple distortion families
[[nodiscard]] bool isCrossFamily() const noexcept;
```

### getActiveProcessorCount()

```cpp
/// @brief Get number of active distortion processor instances.
///
/// Used for global processor cap enforcement across bands.
///
/// @return Number of active processors (1-4)
[[nodiscard]] int getActiveProcessorCount() const noexcept;
```

---

## Static Methods

### requestProcessors() (Class Method)

```cpp
/// @brief Request allocation from global processor pool.
///
/// Call before activating cross-family processing to check if
/// processors are available within the global cap (16 max).
///
/// @param count Number of processors requested
/// @return Number of processors granted (may be less than requested)
///
/// Thread safety: Uses atomic counter, safe from any thread
[[nodiscard]] static int requestProcessors(int count) noexcept;
```

### releaseProcessors() (Class Method)

```cpp
/// @brief Release processors back to global pool.
///
/// Call when transitioning from cross-family to same-family processing.
///
/// @param count Number of processors to release
///
/// Thread safety: Uses atomic counter, safe from any thread
static void releaseProcessors(int count) noexcept;
```

---

## Error Handling

| Method | Invalid Input | Behavior |
|--------|---------------|----------|
| setNodes() | activeCount < 2 | Clamp to 2 |
| setNodes() | activeCount > 4 | Clamp to 4 |
| setMorphPosition() | x < 0 or x > 1 | Clamp to [0, 1] |
| setMorphPosition() | y < 0 or y > 1 | Clamp to [0, 1] |
| setSmoothingTimeMs() | ms < 0 | Clamp to 0 |
| setSmoothingTimeMs() | ms > 500 | Clamp to 500 |
| processBlock() | numSamples > maxBlockSize | Process in chunks |

---

## Usage Example

```cpp
// In BandProcessor::prepare()
morphEngine_.prepare(sampleRate, maxBlockSize);

// Configure nodes
MorphNode nodes[4];
nodes[0] = {0, DistortionType::SoftClip, {}, {}, 0.0f, 0.5f};
nodes[1] = {1, DistortionType::Tube, {}, {}, 1.0f, 0.5f};
morphEngine_.setNodes(nodes, 2);
morphEngine_.setMode(MorphMode::Linear1D);
morphEngine_.setSmoothingTimeMs(50.0f);

// In BandProcessor::process()
morphEngine_.setMorphPosition(morphX_, morphY_);
morphEngine_.process(left, right);

// For UI visualization
MorphWeights weights = morphEngine_.getWeights();
// Draw weight indicators...
```

---

## Performance Guarantees

| Operation | Target | Typical |
|-----------|--------|---------|
| Weight computation (4 nodes) | < 100ns | ~70ns |
| Same-family process() | < 500ns | ~300ns |
| Cross-family process() | < 1000ns | ~700ns |
| processBlock() overhead | < 10% | ~5% |

---

## Thread Safety

| Method | Safe from Audio Thread | Safe from UI Thread | Notes |
|--------|------------------------|---------------------|-------|
| prepare() | No | Yes | Call during setup only |
| reset() | Yes | Yes | No-op if already reset |
| setNodes() | Yes | Yes | Atomic update |
| setMorphPosition() | Yes | Yes | Atomic update |
| setMode() | Yes | Yes | Atomic update |
| process() | Yes | No | Audio thread only |
| getWeights() | Yes | Yes | Read-only |
