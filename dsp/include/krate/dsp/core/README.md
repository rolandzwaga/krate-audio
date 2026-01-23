# Layer 0: Core Utilities

This folder contains foundational utilities that form the base layer of the KrateDSP library. **Layer 0 has no dependencies on higher layers** - only the C++ standard library.

All components in this layer are designed for real-time safety: no allocations, no locks, no exceptions, and heavy use of `constexpr` and `noexcept`.

## Files

### Audio Context

| File | Purpose |
|------|---------|
| `block_context.h` | Per-block processing context carrying sample rate, tempo, time signature, and transport state. Used by tempo-synced components. |
| `note_value.h` | Enums and utilities for musical note values (`Quarter`, `Eighth`, etc.) and modifiers (`Dotted`, `Triplet`) used in tempo sync. |

### Math Utilities

| File | Purpose |
|------|---------|
| `math_constants.h` | Mathematical constants: `kPi`, `kTwoPi`, `kSqrt2`, etc. |
| `fast_math.h` | Fast approximations for expensive math operations (sin, cos, exp, tanh) optimized for audio. |
| `interpolation.h` | Sample interpolation algorithms: linear, cubic Hermite (Catmull-Rom), and 4-point Lagrange. |
| `db_utils.h` | dB/linear conversions, denormal flushing, and constexpr-safe NaN detection that works with `-ffast-math`. |
| `pitch_utils.h` | Pitch and frequency conversion utilities (MIDI note to Hz, cents, ratios). |
| `random.h` | Fast, deterministic pseudo-random number generation for audio use. |

### DSP Building Blocks

| File | Purpose |
|------|---------|
| `dsp_utils.h` / `.cpp` | General DSP utilities shared across the library. |
| `crossfade_utils.h` | Crossfade curve calculations (linear, equal-power, S-curve) for smooth transitions. |
| `stereo_utils.h` | Stereo processing helpers: pan laws, mid/side encoding/decoding, width control. |
| `window_functions.h` | Window functions for spectral processing: Hann, Hamming, Blackman, Kaiser, etc. |

### Waveshaping Math

| File | Purpose |
|------|---------|
| `sigmoid.h` | Sigmoid function implementations for soft saturation curves. |
| `chebyshev.h` | Chebyshev polynomial evaluation for harmonic waveshaping. |
| `wavefold_math.h` | Mathematical functions for wavefolding distortion. |

### Specialized Utilities

| File | Purpose |
|------|---------|
| `grain_envelope.h` | Envelope shapes for granular synthesis grains (Hann, Gaussian, trapezoid). |
| `euclidean_pattern.h` | Euclidean rhythm pattern generation for rhythmic effects. |
| `pattern_freeze_types.h` | Type definitions for pattern-based freeze modes. |

### Filter Design

| File | Purpose |
|------|---------|
| `filter_design.h` | Filter coefficient calculation utilities and design formulas. |
| `filter_tables.h` | Pre-computed filter coefficient lookup tables for efficiency. |

## Usage

Include files using the `<krate/dsp/core/...>` path:

```cpp
#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/db_utils.h>
```

## Design Principles

- **Zero dependencies** on higher layers (primitives, processors, systems, effects)
- **Constexpr where possible** for compile-time evaluation
- **Value semantics** - no raw pointers or dynamic allocation
- **Noexcept everywhere** for real-time safety guarantees
