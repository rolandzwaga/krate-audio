# Data Model: Unified Waveshaper Primitive

**Feature**: 052-waveshaper | **Date**: 2026-01-13

## Entity Diagram

```
+---------------------------------------+
|          WaveshapeType                |
|---------------------------------------|
| enum class : uint8_t                  |
|---------------------------------------|
| Tanh           = 0                    |
| Atan           = 1                    |
| Cubic          = 2                    |
| Quintic        = 3                    |
| ReciprocalSqrt = 4                    |
| Erf            = 5                    |
| HardClip       = 6                    |
| Diode          = 7                    |
| Tube           = 8                    |
+---------------------------------------+
           |
           | uses
           v
+---------------------------------------+
|            Waveshaper                 |
|---------------------------------------|
| - type_      : WaveshapeType          |
| - drive_     : float                  |
| - asymmetry_ : float                  |
|---------------------------------------|
| + Waveshaper()                        |
| + setType(type) noexcept              |
| + setDrive(drive) noexcept            |
| + setAsymmetry(bias) noexcept         |
| + getType() const noexcept            |
| + getDrive() const noexcept           |
| + getAsymmetry() const noexcept       |
| + process(x) const noexcept           |
| + processBlock(buffer, n) noexcept    |
|---------------------------------------|
| - applyShape(x) const noexcept        |
+---------------------------------------+
```

## Entity Definitions

### WaveshapeType (Enumeration)

**Purpose**: Selects which waveshaping transfer function to apply.

| Value | Name | Underlying Function | Output Bounds | Harmonic Character |
|-------|------|---------------------|---------------|-------------------|
| 0 | Tanh | `Sigmoid::tanh()` | [-1, 1] | Odd harmonics, warm |
| 1 | Atan | `Sigmoid::atan()` | [-1, 1] | Odd harmonics, bright |
| 2 | Cubic | `Sigmoid::softClipCubic()` | [-1, 1] | Odd, 3rd dominant |
| 3 | Quintic | `Sigmoid::softClipQuintic()` | [-1, 1] | Odd, smooth rolloff |
| 4 | ReciprocalSqrt | `Sigmoid::recipSqrt()` | [-1, 1] | Odd, tanh-like |
| 5 | Erf | `Sigmoid::erfApprox()` | [-1, 1] | Odd, tape-like nulls |
| 6 | HardClip | `Sigmoid::hardClip()` | [-1, 1] | All harmonics, harsh |
| 7 | Diode | `Asymmetric::diode()` | Unbounded | Even + Odd, subtle |
| 8 | Tube | `Asymmetric::tube()` | [-1, 1]* | Even + Odd, warm |

*Note: Tube is mathematically bounded by final tanh() but documented as "unbounded" per spec clarification.

**Validation**: Enum class with uint8_t storage. Values 0-8 are valid.

### Waveshaper (Class)

**Purpose**: Unified waveshaping primitive providing configurable saturation with selectable transfer functions.

#### Members

| Member | Type | Default | Validation | Description |
|--------|------|---------|------------|-------------|
| type_ | WaveshapeType | Tanh | None (enum) | Selected waveshape algorithm |
| drive_ | float | 1.0f | Stored as abs() | Pre-gain multiplier (intensity) |
| asymmetry_ | float | 0.0f | Clamped [-1, 1] | DC bias for even harmonics |

#### State Transitions

This is a stateless primitive. No state transitions between calls.

| Method | Effect on State |
|--------|-----------------|
| setType(type) | Updates type_ immediately |
| setDrive(drive) | Stores abs(drive) to drive_ |
| setAsymmetry(bias) | Stores clamp(bias, -1, 1) to asymmetry_ |
| process(x) | No state change (const) |
| processBlock(buffer, n) | No state change (calls process() internally) |

## Relationships

```
Waveshaper "1" -- "1" WaveshapeType : has selected type
Waveshaper "1" -- "9" Sigmoid/Asymmetric functions : delegates to (via type_)
```

## Processing Formula

For all waveshape types, the processing formula is:

```
transformed = drive_ * x + asymmetry_
output = applyShape(transformed)  // applies selected function
```

**Special cases**:
- If drive_ == 0.0f: return 0.0f immediately (FR-027)

## Memory Layout

```cpp
class Waveshaper {
    WaveshapeType type_;  // 1 byte
    // 3 bytes padding (alignment)
    float drive_;         // 4 bytes
    float asymmetry_;     // 4 bytes
};
// Total: 12 bytes (or 16 with alignment)
```

No dynamic allocations. Stack-friendly. Can be embedded directly in processors.

## Invariants

1. **Type Valid**: type_ is always a valid WaveshapeType value (0-8)
2. **Drive Non-Negative**: drive_ >= 0.0f (stored as absolute value)
3. **Asymmetry Bounded**: -1.0f <= asymmetry_ <= 1.0f
4. **Stateless Processing**: process() is const and has no side effects

## Usage Patterns

### Single Sample Processing
```cpp
Waveshaper shaper;
shaper.setType(WaveshapeType::Tube);
shaper.setDrive(2.0f);
shaper.setAsymmetry(0.2f);

float output = shaper.process(input);
```

### Block Processing
```cpp
Waveshaper shaper;
shaper.setType(WaveshapeType::Tanh);
shaper.setDrive(4.0f);

float buffer[512] = { /* audio data */ };
shaper.processBlock(buffer, 512);  // In-place processing
```

### Integration with DC Blocker
```cpp
Waveshaper shaper;
DCBlocker dcBlocker;

shaper.setType(WaveshapeType::Tube);
shaper.setAsymmetry(0.3f);  // Creates DC offset
dcBlocker.prepare(sampleRate, 10.0f);

// Processing loop
for (size_t i = 0; i < numSamples; ++i) {
    float shaped = shaper.process(buffer[i]);
    buffer[i] = dcBlocker.process(shaped);  // Remove DC
}
```
