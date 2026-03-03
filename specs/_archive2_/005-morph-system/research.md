# Research: MorphEngine DSP System

**Feature**: 005-morph-system | **Date**: 2026-01-28

---

## Research Summary

This document consolidates research findings for the MorphEngine DSP implementation.

---

## 1. Inverse Distance Weighting (IDW)

### Decision
Use inverse distance weighting with exponent p=2 (Shepard's method).

### Rationale
- Standard algorithm for spatial interpolation
- p=2 provides smooth blending with distance
- Higher exponents (p=3) weight nearest node too heavily
- Lower exponents (p=1) produce harsh transitions

### Formula
```
w_i = 1 / d_i^p
W = sum(w_i) for all i
weight_i = w_i / W  (normalized)
```

Where `d_i = sqrt((x - node_i.x)^2 + (y - node_i.y)^2)`

### Edge Cases
| Case | Handling |
|------|----------|
| Cursor exactly on node | 100% weight to that node (d < epsilon) |
| All nodes at same position | Equal weights (divide by node count) |
| Weight below threshold (0.001) | Skip node, renormalize remaining |

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| Bilinear interpolation | Only works for 4 nodes in grid arrangement |
| Barycentric coordinates | Requires triangulation, complex for 2-4 nodes |
| RBF (Radial Basis Functions) | Overkill for 4 nodes, harder to tune |

---

## 2. Equal-Power Crossfade

### Decision
Use cosine/sine equal-power crossfade for cross-family transitions.

### Rationale
- Maintains constant perceived loudness during transition
- cos^2(x) + sin^2(x) = 1 guarantees energy conservation
- Industry standard for audio crossfades

### Formula
```cpp
float angle = position * kHalfPi;  // position in [0, 1]
fadeOut = cos(angle);              // 1.0 -> 0.0
fadeIn = sin(angle);               // 0.0 -> 1.0
```

### Existing Implementation
Found in `dsp/include/krate/dsp/core/crossfade_utils.h`:
```cpp
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept {
    fadeOut = std::cos(position * kHalfPi);
    fadeIn = std::sin(position * kHalfPi);
}
```

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| Linear crossfade | Causes 3dB dip at 50% position |
| Logarithmic crossfade | Not mathematically clean, harder to tune |
| Custom curves | Unnecessary complexity for standard case |

---

## 3. Transition Zone Strategy (40-60%)

### Decision
Gradual processor activation in 40-60% weight zone using equal-power crossfade.

### Rationale
- Prevents abrupt processor activation/deactivation
- Zone width (20%) provides sufficient blend region
- Below 40%, second processor is essentially inaudible
- Above 60%, first processor is essentially inaudible
- CPU savings outside zone (only one processor active)

### Implementation
```cpp
bool calculateTransitionGains(float morphPosition, float& gainA, float& gainB) {
    constexpr float kZoneStart = 0.4f;
    constexpr float kZoneEnd = 0.6f;

    if (morphPosition < kZoneStart) {
        gainA = 1.0f; gainB = 0.0f;
        return false;  // Single processor
    }
    if (morphPosition > kZoneEnd) {
        gainA = 0.0f; gainB = 1.0f;
        return false;  // Single processor
    }

    // Zone: equal-power crossfade
    float zonePos = (morphPosition - kZoneStart) / (kZoneEnd - kZoneStart);
    equalPowerGains(zonePos, gainA, gainB);
    return true;  // Both processors needed
}
```

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| No transition zone | Causes artifacts when processor state differs |
| Wider zone (30-70%) | Unnecessary CPU cost |
| Narrower zone (45-55%) | Audible discontinuities possible |

---

## 4. Parameter Smoothing Strategy

### Decision
- Manual control: Smooth position (X/Y) before weight computation
- Automated drivers: Smooth computed weights directly

### Rationale
- Manual control benefits from position smoothing (natural cursor movement)
- Automated drivers may cause discontinuous position jumps (Chaos attractor)
- Smoothing weights directly handles discontinuities better

### Implementation
Use existing `OnePoleSmoother` from `dsp/include/krate/dsp/primitives/smoother.h`:
```cpp
class MorphEngine {
    OnePoleSmoother positionXSmoother_;
    OnePoleSmoother positionYSmoother_;
    std::array<OnePoleSmoother, 4> weightSmootherPool_;  // For automated drivers

    void setSmoothingTimeMs(float ms) {
        // Configure based on current driver type
    }
};
```

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| LinearRamp | Not exponential approach, less natural feel |
| SlewLimiter | Adds complexity, not needed for this use case |
| Custom smoother | OnePoleSmoother already exists and is well-tested |

---

## 5. Family-Based Interpolation Strategy

### Decision
Define DistortionFamily enum with specific interpolation methods per family.

### Rationale
- Same-family morphs can interpolate parameters (more efficient)
- Cross-family morphs require parallel processing
- Family grouping matches algorithm characteristics

### Family Definitions (per spec FR-016)

| Family | Types | Interpolation Method |
|--------|-------|---------------------|
| Saturation | D01-D06 | Sample-level transfer function blend |
| Wavefold | D07-D09 | Parameter interpolation |
| Digital | D12-D14, D18-D19 | Parameter interpolation |
| Rectify | D10-D11 | Parameter interpolation |
| Dynamic | D15, D17 | Parameter interpolation + envelope coupling |
| Hybrid | D16, D26 | Parallel blend with output crossfade |
| Experimental | D20-D25 | Parallel blend with output crossfade |

### Implementation
```cpp
enum class DistortionFamily : uint8_t {
    Saturation = 0,
    Wavefold,
    Digital,
    Rectify,
    Dynamic,
    Hybrid,
    Experimental
};

constexpr DistortionFamily getFamily(DistortionType type) noexcept {
    switch (getCategory(type)) {
        case DistortionCategory::Saturation: return DistortionFamily::Saturation;
        case DistortionCategory::Wavefold:   return DistortionFamily::Wavefold;
        // ... etc
    }
}
```

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| No family concept | Forced parallel processing for all morphs (CPU intensive) |
| Per-type compatibility matrix | 26x26 = 676 entries, unmaintainable |
| Category == Family | Categories don't match interpolation requirements exactly |

---

## 6. Global Processor Cap Strategy

### Decision
Maximum 16 active distortion processor instances across all bands.

### Rationale
- 5 bands x 4 nodes x cross-family = 20 potential processors
- CPU budget cannot support 20 simultaneous distortion instances
- 16 is reasonable cap (3-4 per band average)

### Implementation
```cpp
class MorphEngine {
    static std::atomic<int> globalActiveProcessors_;
    static constexpr int kMaxGlobalProcessors = 16;

    int requestProcessors(int count) {
        int current = globalActiveProcessors_.load();
        int available = kMaxGlobalProcessors - current;
        int granted = std::min(count, available);
        globalActiveProcessors_.fetch_add(granted);
        return granted;
    }
};
```

### Enforcement Strategy
When cap would be exceeded:
1. Raise weight threshold (skip low-weight nodes)
2. Keep highest-weight nodes active
3. Reduce to single processor if necessary

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| No cap (let CPU spike) | Unacceptable for real-time audio |
| Per-band cap | Doesn't account for total load across bands |
| Quality degradation | More complex, cap is simpler |

---

## 7. Radial Mode Algorithm

### Decision
Angle determines which nodes are active, distance from center determines blend intensity.

### Rationale
- Alternative interaction model preferred by some users
- Angle naturally maps to node selection (0-360 degrees / node count)
- Distance provides intensity control (0 = center/neutral, 1 = full effect)

### Algorithm
```cpp
void computeRadialWeights(float angle, float distance,
                          const MorphNode* nodes, int nodeCount,
                          float* weights) {
    // Angle determines primary node (0-360 degrees mapped to nodes)
    float nodeAngleSpan = 360.0f / nodeCount;
    int primaryNode = static_cast<int>(angle / nodeAngleSpan) % nodeCount;
    int secondaryNode = (primaryNode + 1) % nodeCount;

    // Blend between adjacent nodes based on fractional angle
    float blendFactor = std::fmod(angle, nodeAngleSpan) / nodeAngleSpan;

    // Distance scales overall intensity
    // At distance=0, all nodes equal weight
    // At distance=1, full node selection
    float selectionStrength = distance;

    // Compute weights
    for (int i = 0; i < nodeCount; ++i) {
        if (i == primaryNode) {
            weights[i] = selectionStrength * (1.0f - blendFactor) + (1.0f - selectionStrength) / nodeCount;
        } else if (i == secondaryNode) {
            weights[i] = selectionStrength * blendFactor + (1.0f - selectionStrength) / nodeCount;
        } else {
            weights[i] = (1.0f - selectionStrength) / nodeCount;
        }
    }
}
```

### Alternatives Considered
| Alternative | Why Rejected |
|-------------|--------------|
| Circular IDW | Doesn't provide intensity control |
| Polar to Cartesian conversion | Loses radial interaction model |

---

## 8. 1D Linear Mode Algorithm

### Decision
Map morphX to position along A-B-C-D axis, compute weights based on distance to adjacent nodes.

### Rationale
- Simplest morph mode for basic A-B blending
- Extends naturally to 3 or 4 nodes
- MorphY ignored in this mode

### Algorithm
```cpp
void computeLinear1DWeights(float morphX, int nodeCount, float* weights) {
    // morphX in [0, 1] maps to node positions
    // Nodes evenly distributed: 0, 0.33, 0.67, 1.0 for 4 nodes
    float nodeSpacing = 1.0f / (nodeCount - 1);

    // Find adjacent nodes
    int lowerNode = static_cast<int>(morphX / nodeSpacing);
    lowerNode = std::clamp(lowerNode, 0, nodeCount - 2);
    int upperNode = lowerNode + 1;

    // Linear interpolation factor
    float lowerPos = lowerNode * nodeSpacing;
    float factor = (morphX - lowerPos) / nodeSpacing;
    factor = std::clamp(factor, 0.0f, 1.0f);

    // Set weights
    std::fill(weights, weights + nodeCount, 0.0f);
    weights[lowerNode] = 1.0f - factor;
    weights[upperNode] = factor;
}
```

---

## 9. Existing Component Analysis

### OnePoleSmoother Usage
Location: `dsp/include/krate/dsp/primitives/smoother.h`
- `configure(float smoothTimeMs, float sampleRate)` - set smoothing time
- `setTarget(float target)` - set target value
- `process()` - get next smoothed sample
- `snapTo(float value)` - instantly set both current and target
- `isComplete()` - check if reached target

### equalPowerGains Usage
Location: `dsp/include/krate/dsp/core/crossfade_utils.h`
- `equalPowerGains(float position, float& fadeOut, float& fadeIn)` - compute gains
- Position 0.0 = full fadeOut, 0.5 = equal blend, 1.0 = full fadeIn

### BandProcessor Integration Points
Location: `plugins/disrumpo/src/dsp/band_processor.h`
- `process(float& left, float& right)` - single sample processing
- `processBlock(float* left, float* right, size_t numSamples)` - block processing
- Currently uses single DistortionAdapter per band
- Need to extend for multiple adapters (morph parallel processing)

---

## 10. Performance Benchmarks

### Target: Weight Computation < 100ns for 4 nodes

Estimated operations:
- 4 sqrt calls: ~10ns each = 40ns
- 4 divisions: ~5ns each = 20ns
- Normalization: ~10ns
- Total: ~70ns (within budget)

### Target: No CPU Spikes During 20Hz Automation

Requirements:
- Smooth weight changes (no sudden processor activation)
- Transition zone fade prevents state discontinuities
- Pre-allocated processor pool avoids allocation

---

## Summary

All NEEDS CLARIFICATION items from Technical Context have been resolved through research. Key decisions:
1. Use inverse distance weighting (p=2) for weight computation
2. Use existing equalPowerGains for crossfade
3. Implement 40-60% transition zone for cross-family morphing
4. Use OnePoleSmoother for position/weight smoothing
5. Define DistortionFamily enum for interpolation strategy
6. Cap at 16 global processors with threshold raising strategy
7. Implement three morph modes with documented algorithms
