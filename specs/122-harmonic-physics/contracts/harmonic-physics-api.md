# API Contract: HarmonicPhysics

**Module**: `plugins/innexus/src/dsp/harmonic_physics.h`
**Namespace**: `Innexus`

## Class: HarmonicPhysics

### Lifecycle

```cpp
/// Prepare for processing. Computes timing constants from hop size.
/// Must be called before processFrame().
/// @param sampleRate Audio sample rate in Hz
/// @param hopSize Analysis hop size in samples
void prepare(double sampleRate, int hopSize) noexcept;

/// Reset all internal state (agent amplitudes, velocities, persistence).
/// Called on note-on, setActive(false), or sample change.
void reset() noexcept;
```

### Processing

```cpp
/// Process one harmonic frame through the physics chain.
/// Order: Coupling -> Warmth -> Dynamics
/// Modifies frame.partials[i].amplitude in-place.
/// Only modifies amplitude; frequency, phase, etc. are unchanged.
/// Safe to call with all-zero frames.
/// @param frame The HarmonicFrame to process (modified in-place)
void processFrame(Krate::DSP::HarmonicFrame& frame) noexcept;
```

### Parameter Setters

```cpp
/// Set warmth amount. 0.0 = bypass, 1.0 = maximum compression.
/// Thread-safe: may be called from processParameterChanges().
void setWarmth(float warmth) noexcept;   // [0.0, 1.0]

/// Set coupling amount. 0.0 = bypass, 1.0 = maximum neighbor sharing.
void setCoupling(float coupling) noexcept; // [0.0, 1.0]

/// Set stability (inertia). 0.0 = bypass, 1.0 = maximum inertia.
void setStability(float stability) noexcept; // [0.0, 1.0]

/// Set entropy (decay). 0.0 = infinite sustain, 1.0 = rapid decay.
void setEntropy(float entropy) noexcept; // [0.0, 1.0]
```

### Guarantees

| Property | Guarantee |
|----------|-----------|
| Bypass | All params at 0.0: output == input (bit-exact) |
| Energy (coupling) | sum(out[i]^2) == sum(in[i]^2) within 0.001% |
| Energy (warmth) | output RMS <= input RMS |
| NaN safety | All-zero input produces all-zero output, no NaN |
| Boundary safety | Index 0 and N-1 handled correctly in coupling |
| Real-time safe | No allocation, no locks, no exceptions |
| Reset safety | After reset(), first processFrame() initializes from input |

## Parameter IDs

```cpp
// In plugin_ids.h, within ParameterIds enum
// Harmonic Physics (700-703) -- Spec A
kWarmthId = 700,       // 0.0-1.0, default 0.0
kCouplingId = 701,     // 0.0-1.0, default 0.0
kStabilityId = 702,    // 0.0-1.0, default 0.0
kEntropyId = 703,      // 0.0-1.0, default 0.0
```

## State Serialization

Version 7 appends 4 floats after v6 data:
```
[v6 data] + warmth(float) + coupling(float) + stability(float) + entropy(float)
```

## Smoother Integration

Four `OnePoleSmoother` instances in `Processor`:
- `warmthSmoother_`, `couplingSmoother_`, `stabilitySmoother_`, `entropySmoother_`

Pattern:
```cpp
// In processParameterChanges():
case kWarmthId:
    warmth_.store(static_cast<float>(value));
    break;

// In process() per-block:
warmthSmoother_.setTarget(warmth_.load(std::memory_order_relaxed));
warmthSmoother_.advanceSamples(numSamples);

// At frame update point:
harmonicPhysics_.setWarmth(warmthSmoother_.getCurrentValue());
harmonicPhysics_.processFrame(morphedFrame_);
```
