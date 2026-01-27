# Quickstart: BitwiseMangler

**Feature**: 111-bitwise-mangler
**Date**: 2026-01-27

## Overview

BitwiseMangler is a Layer 1 DSP primitive for bit manipulation distortion. It creates "wild tonal shifts" by treating audio samples as binary data and applying bitwise operations.

## Installation

Include the header in your DSP code:

```cpp
#include <krate/dsp/primitives/bitwise_mangler.h>

using namespace Krate::DSP;
```

## Basic Usage

### Setup and Processing

```cpp
// Create and prepare
BitwiseMangler mangler;
mangler.prepare(44100.0);  // Sample rate

// Configure
mangler.setOperation(BitwiseOperation::XorPattern);
mangler.setPattern(0xAAAAAAAA);  // Alternating bits
mangler.setIntensity(1.0f);      // Full wet

// Single sample processing
float output = mangler.process(input);

// Block processing
mangler.processBlock(buffer, numSamples);
```

### Reset State

```cpp
// Reset between audio sections
mangler.reset();  // Clears previous sample state
```

---

## Operation Modes

### 1. XorPattern - Metallic Distortion

XOR each sample with a configurable bit pattern. Creates harmonically complex distortion with metallic edge.

```cpp
mangler.setOperation(BitwiseOperation::XorPattern);
mangler.setPattern(0xAAAAAAAA);  // Alternating 1010... pattern
mangler.setIntensity(0.5f);
```

**Pattern Examples**:
| Pattern | Effect |
|---------|--------|
| `0x00000000` | Bypass (no change) |
| `0xAAAAAAAA` | Alternating bits (default) |
| `0x55555555` | Inverse alternating |
| `0xFFFFFFFF` | Invert all bits |
| `0x0000FFFF` | Affect lower 16 bits only |

### 2. XorPrevious - Signal-Responsive Distortion

XOR each sample with the previous sample. Creates distortion that responds to signal dynamics.

```cpp
mangler.setOperation(BitwiseOperation::XorPrevious);
mangler.setIntensity(1.0f);
```

**Characteristics**:
- High-frequency content -> more dramatic changes
- Low-frequency content -> subtler changes
- Transients get more distortion than sustained notes
- First sample after reset() XORs with 0

### 3. BitRotate - Pseudo-Pitch Effects

Circular bit rotation creates unusual frequency shifts without true pitch shifting.

```cpp
mangler.setOperation(BitwiseOperation::BitRotate);
mangler.setRotateAmount(4);   // Rotate left by 4 bits
mangler.setIntensity(0.8f);
```

**Parameters**:
- Positive values: Rotate left (multiply-like effect)
- Negative values: Rotate right (divide-like effect)
- Range: [-16, +16]
- 0 = no rotation (passthrough)

### 4. BitShuffle - Deterministic Chaos

Reorders bits within each sample according to a seed-based permutation.

```cpp
mangler.setOperation(BitwiseOperation::BitShuffle);
mangler.setSeed(42);          // Deterministic output
mangler.setIntensity(1.0f);
```

**Characteristics**:
- Same seed = identical output (reproducible)
- Different seeds = different timbres
- Most destructive mode - complete signal transformation
- Good for extreme sound design and glitch effects

### 5. BitAverage - Bit-Level Smoothing

Bitwise AND with previous sample. Preserves only bits common to both samples.

```cpp
mangler.setOperation(BitwiseOperation::BitAverage);
mangler.setIntensity(0.7f);
```

**Characteristics**:
- Adjacent similar samples -> less change
- Adjacent different samples -> tends toward zero
- Creates unique thinning/smoothing effect
- More subtle than other modes

### 6. OverflowWrap - Integer Overflow Artifacts

Wraps values that exceed the 24-bit integer range instead of clipping.

```cpp
// Apply external gain first
for (auto& sample : buffer) {
    sample *= 2.0f;  // Drive signal hot
}

mangler.setOperation(BitwiseOperation::OverflowWrap);
mangler.setIntensity(1.0f);
mangler.processBlock(buffer, numSamples);

// Output may exceed [-1, 1] - apply limiter downstream
```

**Important**:
- No internal gain - you must drive the signal hot
- Output can exceed [-1, 1] after wrap
- Use downstream limiter for safety

---

## Intensity Control

The intensity parameter controls wet/dry blend:

```cpp
mangler.setIntensity(0.0f);   // Bypass (bit-exact passthrough)
mangler.setIntensity(0.5f);   // 50% wet, 50% dry
mangler.setIntensity(1.0f);   // Full wet (default)
```

**Formula**: `output = original * (1 - intensity) + mangled * intensity`

---

## DC Blocking

DC blocking is **enabled by default** to remove DC offset introduced by stateful operations (XorPrevious, BitAverage).

```cpp
// Default behavior: DC blocking ON
mangler.isDCBlockEnabled();   // Returns true

// Disable for "utter destruction" mode (raw DC offset)
mangler.setDCBlockEnabled(false);

// Re-enable
mangler.setDCBlockEnabled(true);
```

**When to disable DC blocking**:
- You want the full, raw bitwise destruction including DC offset
- You have your own DC blocker downstream
- You're creating intentional asymmetric waveforms

---

## Common Use Cases

### Aggressive Digital Distortion

```cpp
BitwiseMangler mangler;
mangler.prepare(sampleRate);
mangler.setOperation(BitwiseOperation::XorPattern);
mangler.setPattern(0xFFFF00FF);
mangler.setIntensity(0.8f);
```

### Dynamic Transient Emphasis

```cpp
BitwiseMangler mangler;
mangler.prepare(sampleRate);
mangler.setOperation(BitwiseOperation::XorPrevious);
mangler.setIntensity(0.6f);
// Transients will be more distorted than sustained sections
```

### Reproducible Glitch Effect

```cpp
BitwiseMangler mangler;
mangler.prepare(sampleRate);
mangler.setOperation(BitwiseOperation::BitShuffle);
mangler.setSeed(12345);
mangler.setIntensity(1.0f);
// Same seed = same output every time
```

### Subtle Bit-Level Texture

```cpp
BitwiseMangler mangler;
mangler.prepare(sampleRate);
mangler.setOperation(BitwiseOperation::BitRotate);
mangler.setRotateAmount(2);
mangler.setIntensity(0.3f);
```

### Integer Overflow Distortion

```cpp
// Pre-boost signal
float gain = 1.5f;
for (auto& sample : buffer) {
    sample *= gain;
}

BitwiseMangler mangler;
mangler.prepare(sampleRate);
mangler.setOperation(BitwiseOperation::OverflowWrap);
mangler.setIntensity(1.0f);
mangler.processBlock(buffer, numSamples);

// Post-limit
for (auto& sample : buffer) {
    sample = std::clamp(sample, -1.0f, 1.0f);
}
```

### Utter Destruction Mode

Disable DC blocking for the most extreme, raw output:

```cpp
BitwiseMangler mangler;
mangler.prepare(sampleRate);
mangler.setOperation(BitwiseOperation::XorPrevious);
mangler.setIntensity(1.0f);
mangler.setDCBlockEnabled(false);  // Raw destruction!
// Warning: Output will have significant DC offset
```

---

## Performance Notes

- **CPU Usage**: < 0.1% per instance at 44100Hz (Layer 1 budget)
- **Latency**: Zero samples
- **Real-Time Safe**: No allocations in process()
- **Thread Safety**: NOT thread-safe - call from audio thread only

---

## Edge Cases

| Input | Behavior |
|-------|----------|
| NaN | Returns 0.0 |
| Inf | Returns 0.0 |
| Denormal | Flushed to 0.0 |
| Intensity 0.0 | Bit-exact passthrough |
| Pattern 0x00000000 | XorPattern bypasses |
| Rotate 0 | BitRotate bypasses |
| Seed 0 | Replaced with default seed |
| DCBlockEnabled=true | DC offset removed (default) |
| DCBlockEnabled=false | Raw output with DC offset |

---

## API Reference

See [data-model.md](data-model.md) for complete class definition.

```cpp
class BitwiseMangler {
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Operation
    void setOperation(BitwiseOperation op) noexcept;
    BitwiseOperation getOperation() const noexcept;

    // Parameters
    void setIntensity(float intensity) noexcept;      // [0, 1]
    void setPattern(uint32_t pattern) noexcept;       // XorPattern
    void setRotateAmount(int bits) noexcept;          // [-16, +16]
    void setSeed(uint32_t seed) noexcept;             // BitShuffle

    // DC Blocking
    void setDCBlockEnabled(bool enabled) noexcept;    // Default: true
    bool isDCBlockEnabled() const noexcept;

    // Processing
    float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
};
```
