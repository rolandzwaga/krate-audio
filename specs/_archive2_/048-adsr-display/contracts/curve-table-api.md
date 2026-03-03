# API Contract: Curve Table Utility (Layer 0)

**Feature**: 048-adsr-display | **Date**: 2026-02-10

## Overview

Shared utility for generating 256-entry curve lookup tables used by both the ADSREnvelope DSP component (Layer 1) and the ADSRDisplay UI component (for visual curve rendering). Placed in Layer 0 (`dsp/include/krate/dsp/core/`) because it is a pure stateless utility with no dependencies beyond stdlib.

## File

**Path**: `dsp/include/krate/dsp/core/curve_table.h`
**Namespace**: `Krate::DSP`
**Dependencies**: `<array>`, `<cmath>`, `<algorithm>`

## Constants

```cpp
/// Number of entries in each curve lookup table.
inline constexpr size_t kCurveTableSize = 256;

/// Controls the curvature range. With k=3:
///   curve = -1.0 -> exponent = 2^(-3) = 0.125 (very logarithmic)
///   curve =  0.0 -> exponent = 2^(0)  = 1.0   (linear)
///   curve = +1.0 -> exponent = 2^(+3) = 8.0   (very exponential)
inline constexpr float kCurveRangeK = 3.0f;
```

## Functions

### generatePowerCurveTable

```cpp
/// Generate a power curve lookup table.
///
/// For each table entry i (0 to 255):
///   phase = i / 255.0
///   exponent = 2^(curveAmount * kCurveRangeK)
///   table[i] = startLevel + (endLevel - startLevel) * phase^exponent
///
/// Special case: curveAmount == 0.0 produces a linear ramp.
///
/// @param table        Output array (256 entries)
/// @param curveAmount  Curve shape [-1, +1]
/// @param startLevel   Level at phase=0 (default 0.0)
/// @param endLevel     Level at phase=1 (default 1.0)
void generatePowerCurveTable(
    std::array<float, kCurveTableSize>& table,
    float curveAmount,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept;
```

### generateBezierCurveTable

```cpp
/// Generate a cubic Bezier curve lookup table.
///
/// The Bezier curve is defined by 4 control points:
///   P0 = (0, startLevel)
///   P1 = (cp1x, lerp(startLevel, endLevel, cp1y))
///   P2 = (cp2x, lerp(startLevel, endLevel, cp2y))
///   P3 = (1, endLevel)
///
/// The table is indexed by uniform phase (x-coordinate).
/// Implementation: evaluate Bezier at 1024 uniform t values,
/// then resample to 256 uniform x values via linear interpolation.
///
/// @param table        Output array (256 entries)
/// @param cp1x, cp1y  Control point 1 (normalized [0,1] within segment)
/// @param cp2x, cp2y  Control point 2 (normalized [0,1] within segment)
/// @param startLevel   Level at phase=0 (default 0.0)
/// @param endLevel     Level at phase=1 (default 1.0)
void generateBezierCurveTable(
    std::array<float, kCurveTableSize>& table,
    float cp1x, float cp1y,
    float cp2x, float cp2y,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept;
```

### lookupCurveTable

```cpp
/// Linearly interpolate a value from the 256-entry curve table.
///
/// @param table  The lookup table
/// @param phase  Normalized phase [0, 1]
/// @return Interpolated output value
[[nodiscard]] inline float lookupCurveTable(
    const std::array<float, kCurveTableSize>& table,
    float phase) noexcept
{
    float index = phase * 255.0f;
    int i0 = static_cast<int>(index);
    i0 = std::clamp(i0, 0, 254);
    float frac = index - static_cast<float>(i0);
    return table[i0] + frac * (table[i0 + 1] - table[i0]);
}
```

### Conversion Functions

```cpp
/// Convert the discrete EnvCurve enum to a continuous curve amount.
/// Preserves backward compatibility.
[[nodiscard]] inline float envCurveToCurveAmount(EnvCurve curve) noexcept {
    switch (curve) {
        case EnvCurve::Logarithmic:  return -0.7f;
        case EnvCurve::Linear:       return  0.0f;
        case EnvCurve::Exponential:  return +0.7f;
    }
    return 0.0f;
}

/// Derive the simple curve amount from a Bezier curve.
/// Samples the Bezier at phase=0.5 and finds the power curve
/// that matches: output_50 = 0.5^(2^(c * k))
/// Solving: c = log2(log(output_50) / log(0.5)) / k
///
/// Returns 0.0 if the Bezier produces exactly linear output at phase 0.5.
[[nodiscard]] float bezierToSimpleCurve(
    float cp1x, float cp1y,
    float cp2x, float cp2y,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept;

/// Generate Bezier control points that approximate a power curve.
/// Places cp1 at (1/3, powerCurve(1/3)) and cp2 at (2/3, powerCurve(2/3)).
void simpleCurveToBezier(
    float curveAmount,
    float& cp1x, float& cp1y,
    float& cp2x, float& cp2y) noexcept;
```

## Usage Example

```cpp
#include <krate/dsp/core/curve_table.h>

// In ADSREnvelope::setAttackCurve(float amount):
Krate::DSP::generatePowerCurveTable(attackTable_, amount, 0.0f, peakLevel_);

// In ADSREnvelope::processAttack():
float phase = sampleIndex / totalSamples;
output_ = Krate::DSP::lookupCurveTable(attackTable_, phase);

// In Bezier mode:
Krate::DSP::generateBezierCurveTable(attackTable_, cp1x, cp1y, cp2x, cp2y, 0.0f, peakLevel_);
// Same processAttack() code -- table is the abstraction boundary.
```

## Test Requirements

- Power curve with curveAmount=0 produces linear ramp (max error < 1e-6)
- Power curve with curveAmount=+1 produces exponential shape (table[128] < 0.1)
- Power curve with curveAmount=-1 produces logarithmic shape (table[128] > 0.9)
- Bezier with handles at (1/3, 1/3) and (2/3, 2/3) produces near-linear table
- Bezier-to-simple round-trip: generate Bezier from curve amount, convert back, error < 0.05
- lookupCurveTable with phase=0 returns table[0], phase=1 returns table[255]
- lookupCurveTable interpolation is monotonic for monotonic tables
