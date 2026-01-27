# Quickstart: TapeSaturator Processor

**Feature**: 062-tape-saturator
**Layer**: 2 (Processors)
**Location**: `dsp/include/krate/dsp/processors/tape_saturator.h`

## Overview

TapeSaturator is a Layer 2 processor providing tape-style saturation with two models:
- **Simple**: Fast tanh saturation with pre/de-emphasis filters
- **Hysteresis**: Physical Jiles-Atherton magnetic model with configurable solvers

## Quick Usage

```cpp
#include <krate/dsp/processors/tape_saturator.h>

using namespace Krate::DSP;

// Create and prepare
TapeSaturator saturator;
saturator.prepare(44100.0, 512);

// Set parameters
saturator.setModel(TapeModel::Simple);  // or TapeModel::Hysteresis
saturator.setDrive(6.0f);               // +6 dB input gain
saturator.setSaturation(0.7f);          // 70% saturation intensity
saturator.setMix(1.0f);                 // 100% wet

// Process audio
saturator.process(buffer, numSamples);
```

## Models

### Simple Model

Lightweight tanh-based saturation with tape character:

```cpp
saturator.setModel(TapeModel::Simple);
saturator.setDrive(3.0f);       // Light overdrive
saturator.setSaturation(0.5f);  // Moderate saturation
```

**Signal flow**: Input -> Drive -> Pre-emphasis (+9dB @ 3kHz) -> tanh -> De-emphasis (-9dB @ 3kHz) -> DC Block -> Mix

### Hysteresis Model

Physical Jiles-Atherton model with configurable solvers:

```cpp
saturator.setModel(TapeModel::Hysteresis);
saturator.setSolver(HysteresisSolver::RK4);  // Default, balanced
saturator.setDrive(6.0f);
saturator.setSaturation(0.8f);
saturator.setBias(0.2f);  // Slight asymmetry
```

**Solver options**:
| Solver | CPU | Accuracy | Use Case |
|--------|-----|----------|----------|
| RK2 | Low | Medium | Live performance |
| RK4 | Medium | High | Default, mixing |
| NR4 | Higher | High | Quality mixing |
| NR8 | Highest | Highest | Mastering |

## Expert Mode: J-A Parameters

For advanced users who understand the Jiles-Atherton model:

```cpp
// Set custom J-A parameters
saturator.setJAParams(
    22.0f,     // a: Langevin shape
    1.6e-11f,  // alpha: inter-domain coupling
    1.7f,      // c: reversibility
    27.0f,     // k: pinning (coercivity)
    350000.0f  // Ms: saturation magnetization
);

// Read back values
float a = saturator.getJA_a();
```

## Parameter Ranges

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| drive | [-24, +24] dB | 0 | Input gain |
| saturation | [0, 1] | 0.5 | Saturation intensity |
| bias | [-1, +1] | 0 | Asymmetry (even harmonics) |
| mix | [0, 1] | 1.0 | Dry/wet mix |

## Integration in Delay Modes

For TapeDelay (Layer 4):

```cpp
// In TapeDelay, TapeSaturator provides the "Age" character
TapeSaturator ageSaturator;
ageSaturator.prepare(sampleRate, maxBlockSize);

// Modulate saturation based on Age parameter [0,1]
void TapeDelay::setAge(float age) {
    // Age 0: minimal saturation
    // Age 1: heavily saturated "old tape" sound
    ageSaturator.setSaturation(age * 0.8f);
    ageSaturator.setDrive(age * 12.0f);  // Up to +12dB at max age
}
```

## CPU Budget

| Model | Cycles/Sample | CPU% @ 44.1kHz/2.5GHz |
|-------|--------------|----------------------|
| Simple | ~170 | 0.3% |
| Hysteresis RK2 | ~400 | 0.7% |
| Hysteresis RK4 | ~850 | 1.5% |
| Hysteresis NR8 | ~1700 | 3.0% |

## Testing

Run unit tests:
```bash
cmake --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[tape_saturator]"
```

Key test categories:
- `[tape_saturator][foundational]` - Construction, defaults
- `[tape_saturator][functional]` - Signal processing
- `[tape_saturator][performance]` - CPU benchmarks

## Related Components

- **Layer 0**: `core/sigmoid.h` (tanh), `core/db_utils.h`, `core/crossfade_utils.h`
- **Layer 1**: `primitives/biquad.h`, `primitives/dc_blocker.h`, `primitives/smoother.h`
- **Layer 4**: `effects/tape_delay.h` (consumer of this processor)
