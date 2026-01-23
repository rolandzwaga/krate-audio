# Layer 4: Effects (User Features)

This folder contains complete, user-facing audio effects that compose all lower layers into production-ready delay algorithms. **Layer 4 is the top of the architecture** and depends on Layers 0-3.

Each effect represents a distinct delay character or mode that can be selected in the plugin.

## Files

### Classic Delay Types

| File | Purpose |
|------|---------|
| `digital_delay.h` | Clean, pristine digital delay with precise timing and transparent repeats. The reference delay with no coloration. |
| `tape_delay.h` | Vintage tape echo emulation (Roland RE-201, Echoplex style) with motor inertia, wow/flutter, saturation, hiss, and multi-head patterns. |
| `bbd_delay.h` | Bucket-Brigade Device delay emulation with characteristic bandwidth limiting, clock noise, and soft saturation of analog delay chips. |

### Spatial Delay Types

| File | Purpose |
|------|---------|
| `ping_pong_delay.h` | Stereo ping-pong delay that alternates between left and right channels for wide spatial effects. |
| `multi_tap_delay.h` | Multi-tap delay with configurable tap positions, levels, pans, and feedback for complex rhythmic patterns. |

### Creative Delay Types

| File | Purpose |
|------|---------|
| `reverse_delay.h` | Reverse delay that plays back audio in reverse, creating ethereal backwards effects. |
| `granular_delay.h` | Granular delay that breaks audio into grains for texture, pitch shifting, time stretching, and glitch effects. |
| `shimmer_delay.h` | Shimmer delay with pitch shifting in the feedback path for ethereal, pad-like textures (octave up/down effects). |
| `spectral_delay.h` | FFT-based spectral delay with per-frequency-bin delay times for unique frequency-dependent echo effects. |

### Dynamic Delay Types

| File | Purpose |
|------|---------|
| `ducking_delay.h` | Ducking delay that automatically reduces delay volume when input is present, keeping the mix clean during playing. |
| `freeze_mode.h` | Infinite hold/freeze mode that captures and loops a section of audio indefinitely. |
| `pattern_freeze_mode.h` | Pattern-based freeze with rhythmic slice triggering based on Euclidean or custom patterns. |

## Usage

Include files using the `<krate/dsp/effects/...>` path:

```cpp
#include <krate/dsp/effects/tape_delay.h>
#include <krate/dsp/effects/granular_delay.h>
#include <krate/dsp/effects/shimmer_delay.h>
```

## Common Interface

All effects follow a consistent interface:

```cpp
class SomeDelay {
public:
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs);
    void reset();
    bool isPrepared() const;

    // Processing
    void process(float* left, float* right, size_t numSamples);
    void process(float* buffer, size_t numSamples);  // Mono

    // Common Parameters
    void setDelayTime(float ms);    // or setMotorSpeed() for tape
    void setFeedback(float amount);
    void setMix(float wetDry);

    // Effect-specific parameters...
};
```

## Architecture

Effects are the final composition layer:

```
┌─────────────────────────────────────────────────────────┐
│ Layer 4: TapeDelay                                      │
│                                                         │
│  ┌─────────────┐ ┌─────────────┐ ┌───────────────────┐  │
│  │MotorControl │ │ TapManager  │ │CharacterProcessor │  │
│  │ (internal)  │ │ (Layer 3)   │ │ (Layer 3)         │  │
│  └─────────────┘ └─────────────┘ └───────────────────┘  │
│                        │                  │             │
│         ┌──────────────┼──────────────────┤             │
│         ▼              ▼                  ▼             │
│  ┌────────────┐ ┌────────────┐ ┌──────────────────────┐ │
│  │ DelayLine  │ │ Smoother   │ │ SaturationProcessor  │ │
│  │ (Layer 1)  │ │ (Layer 1)  │ │ (Layer 2)            │ │
│  └────────────┘ └────────────┘ └──────────────────────┘ │
│                                          │              │
│                                          ▼              │
│                               ┌──────────────────────┐  │
│                               │ Biquad, LFO, etc.    │  │
│                               │ (Layer 1)            │  │
│                               └──────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Design Principles

- **Complete features** - ready for end-user exposure in the plugin
- **Consistent API** - similar interface across all delay types
- **Parameter mapping** - user-friendly parameter ranges (not raw DSP values)
- **Real-time safe** - no allocations after prepare(), noexcept processing
- **Smooth transitions** - parameter changes and mode switches are click-free
