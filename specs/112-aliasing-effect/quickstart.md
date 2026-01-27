# Quickstart: AliasingEffect

**Feature**: 112-aliasing-effect | **Date**: 2026-01-27

## Overview

AliasingEffect is a Layer 2 DSP processor for creating intentional aliasing artifacts. It downsamples audio without anti-aliasing, causing high frequencies to fold back into the audible spectrum for a digital grunge/lo-fi aesthetic.

## Quick Usage

```cpp
#include <krate/dsp/processors/aliasing_effect.h>

using namespace Krate::DSP;

// Create and prepare
AliasingEffect aliaser;
aliaser.prepare(44100.0, 512);

// Configure
aliaser.setDownsampleFactor(8.0f);      // Heavy aliasing
aliaser.setAliasingBand(2000.0f, 8000.0f);  // Only mid-highs get aliased
aliaser.setFrequencyShift(500.0f);      // Shift before aliasing
aliaser.setMix(0.75f);                  // 75% wet

// Process
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = aliaser.process(input[i]);
}

// Or block processing
aliaser.process(buffer, numSamples);
```

## Key Parameters

| Parameter | Range | Effect |
|-----------|-------|--------|
| Downsample Factor | 2-32 | Higher = more severe aliasing |
| Frequency Shift | -5000 to +5000 Hz | Moves spectrum before aliasing |
| Aliasing Band | 20Hz to Nyquist | Which frequencies get aliased |
| Mix | 0-1 | Dry/wet blend |

## Processing Chain

```
Input -> Band Isolation -> Freq Shift -> Downsample -> Recombine -> Mix
           (24dB/oct)        (SSB)      (no AA filter)
```

## Common Patterns

### Subtle Lo-Fi Character
```cpp
aliaser.setDownsampleFactor(2.0f);
aliaser.setAliasingBand(4000.0f, 16000.0f);
aliaser.setFrequencyShift(0.0f);
aliaser.setMix(0.3f);
```

### Classic Digital Grunge
```cpp
aliaser.setDownsampleFactor(8.0f);
aliaser.setAliasingBand(2000.0f, 10000.0f);
aliaser.setFrequencyShift(200.0f);
aliaser.setMix(0.7f);
```

### Extreme Digital Destruction
```cpp
aliaser.setDownsampleFactor(32.0f);
aliaser.setAliasingBand(500.0f, 15000.0f);
aliaser.setFrequencyShift(-1000.0f);
aliaser.setMix(1.0f);
```

## Stereo Usage

AliasingEffect is mono-only. For stereo, use two instances:

```cpp
AliasingEffect aliaserL, aliaserR;
aliaserL.prepare(sampleRate, blockSize);
aliaserR.prepare(sampleRate, blockSize);

// Independent control possible
aliaserL.setFrequencyShift(+250.0f);
aliaserR.setFrequencyShift(-250.0f);

for (size_t i = 0; i < numSamples; ++i) {
    outL[i] = aliaserL.process(inL[i]);
    outR[i] = aliaserR.process(inR[i]);
}
```

## Implementation Checklist

### Modify Existing Files
- [ ] `dsp/include/krate/dsp/primitives/sample_rate_reducer.h`
  - Change `kMaxReductionFactor` from `8.0f` to `32.0f`
- [ ] `dsp/tests/unit/primitives/sample_rate_reducer_test.cpp`
  - Add tests for extended factor range

### Create New Files
- [ ] `dsp/include/krate/dsp/processors/aliasing_effect.h`
  - Full header-only implementation
- [ ] `dsp/tests/unit/processors/aliasing_effect_test.cpp`
  - Unit tests covering all requirements

### Update Architecture Docs
- [ ] `specs/_architecture_/layer-2-processors.md`
  - Add AliasingEffect documentation
- [ ] `specs/_architecture_/layer-1-primitives.md`
  - Update SampleRateReducer max factor

## Dependencies

```
AliasingEffect (Layer 2)
├── FrequencyShifter (Layer 2) - SSB modulation
├── BiquadCascade<2> (Layer 1) - Band filters (x2)
├── SampleRateReducer (Layer 1) - Sample-and-hold
├── OnePoleSmoother (Layer 1) - Parameter smoothing (x5)
└── db_utils.h (Layer 0) - NaN/Inf checks, denormal flushing
```

## Performance Notes

- **CPU**: < 0.5% per instance at 44100Hz (Layer 2 budget)
- **Memory**: ~2.5KB per instance (mostly FrequencyShifter)
- **Latency**: ~5 samples (from Hilbert transform)

## Testing Priorities

1. **P1**: Basic aliasing (downsample factor creates aliased frequencies)
2. **P1**: Band isolation (frequencies outside band pass through)
3. **P2**: Frequency shift affects aliasing patterns
4. **P2**: Stability under extreme settings
5. **P3**: Click-free parameter automation
