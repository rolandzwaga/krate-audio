# Quickstart: MorphEngine DSP System

**Feature**: 005-morph-system | **Date**: 2026-01-28

---

## Overview

The MorphEngine enables smooth blending between 2-4 distortion types within each frequency band. It supports three interpolation modes (1D Linear, 2D Planar, 2D Radial) and handles both same-family parameter interpolation and cross-family parallel processing.

---

## Quick Usage

### Basic Setup

```cpp
#include "morph_engine.h"
#include "morph_node.h"

// In your band processor or plugin processor
Disrumpo::MorphEngine morphEngine_;

// Initialize during prepare()
void prepare(double sampleRate, size_t maxBlockSize) {
    morphEngine_.prepare(sampleRate, maxBlockSize);

    // Configure 2 nodes for simple A-B morphing
    Disrumpo::MorphNode nodes[2];

    // Node A: Soft Clip
    nodes[0].id = 0;
    nodes[0].type = Disrumpo::DistortionType::SoftClip;
    nodes[0].commonParams.drive = 5.0f;
    nodes[0].commonParams.mix = 1.0f;
    nodes[0].posX = 0.0f;
    nodes[0].posY = 0.5f;

    // Node B: Tube
    nodes[1].id = 1;
    nodes[1].type = Disrumpo::DistortionType::Tube;
    nodes[1].commonParams.drive = 7.0f;
    nodes[1].commonParams.mix = 1.0f;
    nodes[1].posX = 1.0f;
    nodes[1].posY = 0.5f;

    morphEngine_.setNodes(nodes, 2);
    morphEngine_.setMode(Disrumpo::MorphMode::Linear1D);
    morphEngine_.setSmoothingTimeMs(50.0f);
}
```

### Processing Audio

```cpp
// In your audio callback
void process(float** inputs, float** outputs, int32 numSamples) {
    // Update morph position from parameter
    float morphX = normalizedMorphParam;  // 0.0 to 1.0
    morphEngine_.setMorphPosition(morphX, 0.5f);

    // Process audio
    float* left = outputs[0];
    float* right = outputs[1];

    // Copy input to output first
    std::copy(inputs[0], inputs[0] + numSamples, left);
    std::copy(inputs[1], inputs[1] + numSamples, right);

    // Process through morph engine
    morphEngine_.processBlock(left, right, numSamples);
}
```

---

## Morph Modes

### 1D Linear Mode

Best for simple A-B or A-B-C-D blending along a single axis.

```cpp
morphEngine_.setMode(Disrumpo::MorphMode::Linear1D);

// Only morphX is used (0.0 = Node A, 1.0 = Node B/D)
morphEngine_.setMorphPosition(0.5f, 0.0f);  // Y is ignored
```

### 2D Planar Mode

Uses inverse distance weighting for natural XY pad interaction.

```cpp
morphEngine_.setMode(Disrumpo::MorphMode::Planar2D);

// Both X and Y affect weights
morphEngine_.setMorphPosition(0.25f, 0.75f);  // Cursor position

// Weights based on distance to each node
// Closer nodes have higher weight
```

### 2D Radial Mode

Angle selects nodes, distance controls blend intensity.

```cpp
morphEngine_.setMode(Disrumpo::MorphMode::Radial2D);

// Convert XY to polar
float angle = atan2f(y - 0.5f, x - 0.5f);  // Radians
float distance = sqrtf((x-0.5f)*(x-0.5f) + (y-0.5f)*(y-0.5f));

// Angle determines which nodes are blended
// Distance determines how much of the selected blend vs neutral
```

---

## 4-Node Configuration

For full XY pad morphing with 4 corners:

```cpp
Disrumpo::MorphNode nodes[4];

// Top-left: Soft Clip
nodes[0] = {0, Disrumpo::DistortionType::SoftClip, {5.0f, 1.0f, 4000.0f}, {}, 0.0f, 0.0f};

// Top-right: Tube
nodes[1] = {1, Disrumpo::DistortionType::Tube, {7.0f, 1.0f, 3000.0f}, {}, 1.0f, 0.0f};

// Bottom-left: Wavefolder
nodes[2] = {2, Disrumpo::DistortionType::SergeFold, {4.0f, 1.0f, 5000.0f}, {}, 0.0f, 1.0f};

// Bottom-right: Bitcrusher
nodes[3] = {3, Disrumpo::DistortionType::Bitcrush, {3.0f, 0.8f, 8000.0f}, {}, 1.0f, 1.0f};

morphEngine_.setNodes(nodes, 4);
morphEngine_.setMode(Disrumpo::MorphMode::Planar2D);
```

---

## Smoothing Control

Control transition speed for different contexts:

```cpp
// Instant for UI snapping
morphEngine_.setSmoothingTimeMs(0.0f);

// Fast for responsive automation
morphEngine_.setSmoothingTimeMs(20.0f);

// Default for smooth manual control
morphEngine_.setSmoothingTimeMs(50.0f);

// Slow for cinematic sweeps
morphEngine_.setSmoothingTimeMs(200.0f);
```

---

## Reading Weights for UI

Display current morph weights in your UI:

```cpp
// Get current weights (after smoothing)
Disrumpo::MorphWeights weights = morphEngine_.getWeights();

// Draw weight indicators
for (int i = 0; i < 4; ++i) {
    float weight = weights.values[i];
    // Draw indicator with opacity = weight
    drawNodeIndicator(nodes[i].posX, nodes[i].posY, weight);
}

// Show active count
int activeCount = weights.activeCount;
// Display "2 of 4 nodes active" etc.
```

---

## Cross-Family Detection

Check if morphing between different distortion families:

```cpp
// Same-family: Soft Clip + Tube (both Saturation)
// Cross-family: Soft Clip + Bitcrush (Saturation + Digital)

if (morphEngine_.isCrossFamily()) {
    // More CPU-intensive parallel processing
    // Consider reducing other effects
}

// Check processor count for global budgeting
int processors = morphEngine_.getActiveProcessorCount();
```

---

## Integration with BandProcessor

The MorphEngine integrates naturally with BandProcessor:

```cpp
class BandProcessor {
    MorphEngine morphEngine_;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        // ... existing prepare code ...
        morphEngine_.prepare(sampleRate, maxBlockSize);
    }

    void updateMorphState(const BandState& state) noexcept {
        morphEngine_.setNodes(state.nodes.data(), state.activeNodeCount);
        morphEngine_.setMode(state.morphMode);
        morphEngine_.setSmoothingTimeMs(state.morphSmoothingMs);
        morphEngine_.setMorphPosition(state.morphX, state.morphY);
    }

    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        // Process through morph engine (replaces single distortion)
        morphEngine_.processBlock(left, right, numSamples);

        // Apply existing output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }
};
```

---

## Parameter ID Patterns

Following CLAUDE.md naming conventions:

```cpp
// Per-band morph parameters
enum BandMorphParamIds {
    kBand1MorphXId,           // Morph X position
    kBand1MorphYId,           // Morph Y position
    kBand1MorphModeId,        // MorphMode enum
    kBand1MorphSmoothingId,   // Smoothing time ms
    kBand1NodeCountId,        // Active node count

    // Per-node parameters (node 0-3 per band)
    kBand1Node0TypeId,        // DistortionType
    kBand1Node0DriveId,       // Common params
    kBand1Node0MixId,
    kBand1Node0ToneId,
    // ... type-specific params as needed
};
```

---

## Performance Tips

1. **Avoid unnecessary node changes**: Only call `setNodes()` when configuration actually changes.

2. **Batch position updates**: Update morph position once per block, not per sample.

3. **Monitor processor count**: Use `getActiveProcessorCount()` to manage global CPU budget.

4. **Use appropriate smoothing**: Faster smoothing = more responsive but potential artifacts.

5. **Prefer same-family morphs**: Saturation-to-Saturation uses less CPU than Saturation-to-Digital.

---

## Common Patterns

### Parameter Automation

```cpp
// Map automation value to morph position
void onParameterChange(ParamID id, float normalized) {
    if (id == kMorphXId) {
        morphEngine_.setMorphPosition(normalized, currentY_);
    }
}
```

### Morph Lock (Freeze)

```cpp
// Freeze current weights
MorphWeights frozen = morphEngine_.getWeights();
// Store and restore later
morphEngine_.setRawWeights(frozen.values.data());
```

### Random Morph

```cpp
// Jump to random position
float randomX = static_cast<float>(rand()) / RAND_MAX;
float randomY = static_cast<float>(rand()) / RAND_MAX;
morphEngine_.setMorphPosition(randomX, randomY);
```
