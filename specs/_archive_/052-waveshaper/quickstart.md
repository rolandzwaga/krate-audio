# Quickstart: Unified Waveshaper Primitive

**Feature**: 052-waveshaper | **Layer**: 1 (Primitives)

## Installation

The Waveshaper is a header-only component. Include it directly:

```cpp
#include <krate/dsp/primitives/waveshaper.h>
```

## Basic Usage

### Simple Waveshaping

```cpp
#include <krate/dsp/primitives/waveshaper.h>

using namespace Krate::DSP;

// Create waveshaper with defaults (Tanh, drive=1.0, asymmetry=0.0)
Waveshaper shaper;

// Process a single sample
float input = 0.5f;
float output = shaper.process(input);  // ~0.462 (tanh(0.5))
```

### Configuring Waveshape Type

```cpp
Waveshaper shaper;

// Choose from 9 waveshape types
shaper.setType(WaveshapeType::Tanh);           // Warm, smooth (default)
shaper.setType(WaveshapeType::Atan);           // Slightly brighter
shaper.setType(WaveshapeType::Cubic);          // 3rd harmonic emphasis
shaper.setType(WaveshapeType::Quintic);        // Smoother than cubic
shaper.setType(WaveshapeType::ReciprocalSqrt); // Fast tanh alternative
shaper.setType(WaveshapeType::Erf);            // Tape-like character
shaper.setType(WaveshapeType::HardClip);       // Harsh, digital
shaper.setType(WaveshapeType::Diode);          // Subtle even harmonics
shaper.setType(WaveshapeType::Tube);           // Warm even harmonics
```

### Controlling Saturation with Drive

```cpp
Waveshaper shaper;
shaper.setType(WaveshapeType::Tanh);

// Low drive - nearly linear
shaper.setDrive(0.1f);
float soft = shaper.process(0.5f);  // ~0.05 (almost linear)

// High drive - aggressive saturation
shaper.setDrive(10.0f);
float hard = shaper.process(0.5f);  // ~1.0 (heavily saturated)

// Unity drive - standard curve
shaper.setDrive(1.0f);
float normal = shaper.process(0.5f);  // ~0.46 (tanh(0.5))
```

### Adding Even Harmonics with Asymmetry

```cpp
Waveshaper shaper;
shaper.setType(WaveshapeType::Tanh);
shaper.setAsymmetry(0.3f);  // Add DC bias before shaping

float output = shaper.process(0.5f);  // tanh(0.5 + 0.3) = tanh(0.8)

// WARNING: Asymmetry introduces DC offset!
// Always DC-block the output when using asymmetry.
```

### Block Processing

```cpp
Waveshaper shaper;
shaper.setType(WaveshapeType::Tube);
shaper.setDrive(2.0f);

float buffer[512] = { /* audio samples */ };
shaper.processBlock(buffer, 512);  // In-place processing
```

## Composition with DCBlocker

When using asymmetry, always follow with DC blocking:

```cpp
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/dc_blocker.h>

Waveshaper shaper;
DCBlocker dcBlocker;

// Configure waveshaper with asymmetry
shaper.setType(WaveshapeType::Tube);
shaper.setDrive(2.0f);
shaper.setAsymmetry(0.2f);  // Even harmonics = DC offset

// Configure DC blocker
dcBlocker.prepare(44100.0, 10.0f);  // 44.1kHz, 10Hz cutoff

// Process audio block
for (size_t i = 0; i < numSamples; ++i) {
    float shaped = shaper.process(buffer[i]);
    buffer[i] = dcBlocker.process(shaped);  // Remove DC
}
```

## Waveshape Type Reference

| Type | Harmonics | Character | Output Bounds |
|------|-----------|-----------|---------------|
| Tanh | Odd only | Warm, smooth | [-1, 1] |
| Atan | Odd only | Slightly bright | [-1, 1] |
| Cubic | Odd (3rd) | Gentle saturation | [-1, 1] |
| Quintic | Odd | Very smooth knee | [-1, 1] |
| ReciprocalSqrt | Odd | Fast, tanh-like | [-1, 1] |
| Erf | Odd | Tape-like | [-1, 1] |
| HardClip | All | Harsh, digital | [-1, 1] |
| Diode | Even + Odd | Subtle warmth | **Unbounded** |
| Tube | Even + Odd | Rich warmth | [-1, 1]* |

*Tube is bounded to [-1, 1] mathematically but documented as potentially exceeding bounds.

## Important Notes

1. **No Oversampling**: Waveshaper is a pure transfer function. For alias-free processing, use it within an Oversampler or at oversampled rate.

2. **No DC Blocking**: When using asymmetry > 0, output will have DC offset. Compose with DCBlocker.

3. **Diode is Unbounded**: The Diode type can produce output exceeding [-1, 1]. Apply limiting if needed.

4. **Drive = 0 Returns 0**: When drive is 0, process() always returns 0 (signal scaled to zero).

5. **Stateless**: No prepare() or reset() needed. Safe to use immediately after construction.

## Performance

- Layer 1 primitive: < 0.1% CPU per instance
- No memory allocation in process path
- Inline-friendly implementation
- Switch-based dispatch (optimized by compiler)

## API Summary

```cpp
class Waveshaper {
public:
    Waveshaper();  // Defaults: Tanh, drive=1.0, asymmetry=0.0

    // Configuration
    void setType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;        // abs() applied
    void setAsymmetry(float bias) noexcept;     // clamped [-1, 1]

    // Query
    WaveshapeType getType() const noexcept;
    float getDrive() const noexcept;
    float getAsymmetry() const noexcept;

    // Processing
    float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
};
```
