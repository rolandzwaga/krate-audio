# Layered DSP Architecture

This project uses a strict 5-layer architecture for DSP code. Each layer has specific responsibilities and dependency rules.

---

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                   │
│                                                             │
│  Complete delay modes (Digital, Tape, Granular, etc.)       │
│  Full effect implementations ready for the plugin           │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                 │
│                                                             │
│  Composed processors (modulated delay, filtered feedback)   │
│  Modulation systems (LFO routing, envelope followers)       │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                   │
│                                                             │
│  Filters (biquad, SVF, comb)                                │
│  Saturators (tanh, waveshaper, tube)                        │
│  Diffusers (allpass chains, Schroeder)                      │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
│                                                             │
│  Delay lines (basic, interpolated, multi-tap)               │
│  Oscillators (sine, triangle, noise)                        │
│  Envelopes (ADSR, follower)                                 │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                  │
│                                                             │
│  Math functions (dB conversion, interpolation)              │
│  Buffer helpers (copy, mix, gain)                           │
│  Constants and type definitions                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Dependency Rules

### The Cardinal Rule

**Each layer can ONLY depend on layers below it.**

```cpp
// Layer 2 processor - ALLOWED
#include <krate/dsp/core/math_utils.h>      // Layer 0 - OK
#include <krate/dsp/primitives/delay_line.h> // Layer 1 - OK

// Layer 2 processor - FORBIDDEN
#include <krate/dsp/systems/mod_delay.h>     // Layer 3 - NO!
#include <krate/dsp/effects/tape_mode.h>     // Layer 4 - NO!
```

### No Circular Dependencies

If Layer 1 component A depends on Layer 1 component B, then B cannot depend on A.

### Layer 0 Extraction Rule

If a utility is used by 2+ Layer 1 primitives, extract it to Layer 0.

```cpp
// BAD: Duplicate code in two Layer 1 primitives
// primitives/delay_line.h
inline float linearInterp(float a, float b, float t) { return a + t * (b - a); }

// primitives/oscillator.h
inline float lerp(float a, float b, float t) { return a + t * (b - a); }

// GOOD: Extract to Layer 0
// core/interpolation.h
inline float linearInterpolate(float a, float b, float t) { return a + t * (b - a); }
```

---

## File Organization

```
dsp/
├── include/krate/dsp/
│   ├── core/                     # Layer 0
│   │   ├── math_utils.h          # dB conversion, clamp, etc.
│   │   ├── interpolation.h       # Linear, cubic, Lagrange
│   │   ├── buffer_utils.h        # Copy, mix, apply gain
│   │   └── constants.h           # Pi, sample rate limits, etc.
│   │
│   ├── primitives/               # Layer 1
│   │   ├── delay_line.h          # Basic circular buffer delay
│   │   ├── interpolated_delay.h  # With fractional delay
│   │   ├── lfo.h                 # Low-frequency oscillator
│   │   ├── envelope_follower.h   # RMS/peak detection
│   │   └── one_pole.h            # Simple lowpass/highpass
│   │
│   ├── processors/               # Layer 2
│   │   ├── biquad_filter.h       # 2nd order IIR filter
│   │   ├── svf_filter.h          # State variable filter
│   │   ├── saturator.h           # Soft clipping
│   │   ├── allpass.h             # Allpass filter
│   │   └── dc_blocker.h          # DC removal filter
│   │
│   ├── systems/                  # Layer 3
│   │   ├── modulated_delay.h     # Delay + LFO modulation
│   │   ├── filtered_feedback.h   # Feedback path with filter
│   │   ├── diffusion_network.h   # Multiple allpass stages
│   │   └── stereo_processor.h    # L/R processing framework
│   │
│   └── effects/                  # Layer 4
│       ├── digital_delay.h       # Complete digital delay mode
│       ├── tape_delay.h          # Complete tape delay mode
│       ├── granular_delay.h      # Complete granular mode
│       └── ...                   # Other delay modes
│
└── tests/                        # Mirror structure
    ├── core/
    ├── primitives/
    ├── processors/
    ├── systems/
    └── effects/
```

---

## Include Patterns

### From DSP Code

```cpp
// Use angle brackets with full path
#include <krate/dsp/core/math_utils.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/processors/biquad_filter.h>
```

### From Plugin Code

```cpp
// Plugin code includes DSP headers the same way
#include <krate/dsp/effects/digital_delay.h>

// Plugin-specific headers use quotes
#include "processor/processor.h"
```

---

## Layer Responsibilities

### Layer 0: Core Utilities

- **No DSP state** - Pure functions only
- **No sample rate** - Math operations only
- **Dependencies** - Standard library only
- **Examples**: `dBToLinear()`, `linearInterpolate()`, `clamp()`

### Layer 1: DSP Primitives

- **Single responsibility** - One DSP operation
- **Stateful** - Has internal buffers/state
- **Sample rate aware** - Needs `prepare(sampleRate)`
- **Examples**: `DelayLine`, `LFO`, `EnvelopeFollower`

### Layer 2: DSP Processors

- **Composed primitives** - Uses Layer 1 components
- **Standard interfaces** - `prepare()`, `process()`, `reset()`
- **Configurable** - Multiple modes/parameters
- **Examples**: `BiquadFilter`, `Saturator`, `AllpassChain`

### Layer 3: System Components

- **Multiple processors** - Combines Layer 2 components
- **Signal routing** - Feedback, parallel, series
- **Modulation** - LFO/envelope routing built-in
- **Examples**: `ModulatedDelay`, `FilteredFeedback`, `DiffusionNetwork`

### Layer 4: User Features

- **Complete effects** - Ready for plugin use
- **All parameters** - Full control interface
- **Preset-ready** - All state serializable
- **Examples**: `DigitalDelay`, `TapeDelay`, `GranularDelay`

---

## ODR Prevention

**One Definition Rule** - Two classes with the same name in the same namespace cause undefined behavior.

### Before Creating Any New Class

```bash
# Search for existing class with same name
grep -r "class ClassName" dsp/ plugins/

# Check the architecture document
cat ARCHITECTURE.md | grep ClassName
```

### Symptoms of ODR Violation

- Garbage values in member variables
- Methods that "do nothing"
- Mysterious test failures
- Works in one file, fails in another

### Prevention

1. Use unique, descriptive class names
2. Check existing code before adding new classes
3. Use nested namespaces if needed: `Krate::DSP::Delay::Line`
