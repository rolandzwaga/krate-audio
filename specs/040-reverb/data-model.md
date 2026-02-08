# Data Model: Dattorro Plate Reverb

**Feature Branch**: `040-reverb` | **Date**: 2026-02-08

## Entities

### ReverbParams (struct)

Parameter structure for the Reverb effect. Passed to `setParams()`.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `roomSize` | `float` | [0.0, 1.0] | 0.5 | Decay coefficient control. Maps: `decay = 0.5 + roomSize * roomSize * 0.45` |
| `damping` | `float` | [0.0, 1.0] | 0.5 | HF absorption. Maps: `cutoffHz = 200.0 * pow(100.0, 1.0 - damping)` |
| `width` | `float` | [0.0, 1.0] | 1.0 | Stereo decorrelation via mid-side encoding |
| `mix` | `float` | [0.0, 1.0] | 0.3 | Dry/wet blend |
| `preDelayMs` | `float` | [0.0, 100.0] | 0.0 | Pre-delay time in milliseconds |
| `diffusion` | `float` | [0.0, 1.0] | 0.7 | Input diffusion coefficient scaling |
| `freeze` | `bool` | true/false | false | Infinite sustain mode |
| `modRate` | `float` | [0.0, 2.0] | 0.5 | Tank modulation LFO rate in Hz |
| `modDepth` | `float` | [0.0, 1.0] | 0.0 | Tank modulation depth (0=off, 1=max 8 samples @ 29761 Hz) |

### Reverb (class)

The main reverb processor. Layer 4 effect in `Krate::DSP` namespace.

**Lifecycle State Machine**:
```
[Constructed] --prepare()--> [Prepared] --process()/processBlock()--> [Processing]
     ^                          |                                         |
     |                     reset()                                   reset()
     |                          |                                         |
     +--- (no deallocation) ----+-----------------------------------------+
```

**Internal Components** (all private members):

| Component | Type | Count | Purpose |
|-----------|------|-------|---------|
| `bandwidthFilter_` | `OnePoleLP` | 1 | Input bandwidth filter (coeff 0.9995) |
| `preDelay_` | `DelayLine` | 1 | Pre-delay line (0-100ms) |
| `inputDiffusion_` | `SchroederAllpass` | 4 | Input diffusion allpass cascade |
| `tankADD1Delay_` | `DelayLine` | 1 | Tank A decay diffusion 1 delay (standalone for output tap access + LFO modulation; allpass math inline) |
| `tankAPreDampDelay_` | `DelayLine` | 1 | Tank A pre-damping delay |
| `tankADamping_` | `OnePoleLP` | 1 | Tank A damping lowpass filter |
| `tankADD2Delay_` | `DelayLine` | 1 | Tank A decay diffusion 2 delay (standalone for output tap access; allpass math inline) |
| `tankAPostDampDelay_` | `DelayLine` | 1 | Tank A post-damping delay |
| `tankADCBlocker_` | `DCBlocker` | 1 | Tank A DC blocker |
| `tankBDD1Delay_` | `DelayLine` | 1 | Tank B decay diffusion 1 delay (standalone for output tap access + LFO modulation; allpass math inline) |
| `tankBPreDampDelay_` | `DelayLine` | 1 | Tank B pre-damping delay |
| `tankBDamping_` | `OnePoleLP` | 1 | Tank B damping lowpass filter |
| `tankBDD2Delay_` | `DelayLine` | 1 | Tank B decay diffusion 2 delay (standalone for output tap access; allpass math inline) |
| `tankBPostDampDelay_` | `DelayLine` | 1 | Tank B post-damping delay |
| `tankBDCBlocker_` | `DCBlocker` | 1 | Tank B DC blocker |

**Parameter Smoothers** (all `OnePoleSmoother`):

| Smoother | Purpose |
|----------|---------|
| `decaySmoother_` | Smooth decay coefficient changes |
| `dampingSmoother_` | Smooth damping LP coefficient changes |
| `mixSmoother_` | Smooth dry/wet mix changes |
| `widthSmoother_` | Smooth stereo width changes |
| `inputGainSmoother_` | Smooth freeze on/off (0.0 or 1.0 target) |
| `preDelayMsSmoother_` | Smooth pre-delay time changes (milliseconds) |
| `diffusion1Smoother_` | Smooth input diffusion 1 coefficient |
| `diffusion2Smoother_` | Smooth input diffusion 2 coefficient |

**Tank State Variables**:

| Variable | Type | Description |
|----------|------|-------------|
| `tankAOut_` | `float` | Tank A output (from post-damp delay + DC blocker), fed to Tank B input |
| `tankBOut_` | `float` | Tank B output (from post-damp delay + DC blocker), fed to Tank A input |
| `lfoPhase_` | `float` | LFO phase accumulator [0, 2*pi) |

**Scaled Delay Lengths** (computed in `prepare()`):

| Variable | Description |
|----------|-------------|
| `inputDiffDelays_[4]` | Scaled input diffusion allpass delays |
| `tankADD1Delay_` | Tank A decay diffusion 1 allpass delay (center) |
| `tankAPreDampLen_` | Tank A pre-damping delay length |
| `tankADD2Delay_` | Tank A decay diffusion 2 allpass delay |
| `tankAPostDampLen_` | Tank A post-damping delay length |
| `tankBDD1Delay_` | Tank B decay diffusion 1 allpass delay (center) |
| `tankBPreDampLen_` | Tank B pre-damping delay length |
| `tankBDD2Delay_` | Tank B decay diffusion 2 allpass delay |
| `tankBPostDampLen_` | Tank B post-damping delay length |
| `outputTaps_[14]` | Scaled output tap positions (7 left + 7 right) |
| `maxExcursion_` | Scaled max LFO excursion (8 samples @ 29761 Hz) |

## Relationships

```
Reverb "has" 1 OnePoleLP (bandwidth filter)
Reverb "has" 1 DelayLine (pre-delay)
Reverb "has" 4 SchroederAllpass (input diffusion)
Reverb "has" 2 DelayLine (tank decay diffusion 1, modulated; allpass math inline for output tap access)
Reverb "has" 2 DelayLine (tank pre-damping delays)
Reverb "has" 2 OnePoleLP (tank damping filters)
Reverb "has" 2 DelayLine (tank decay diffusion 2, fixed; allpass math inline for output tap access)
Reverb "has" 2 DelayLine (tank post-damping delays)
Reverb "has" 2 DCBlocker (tank DC blockers)
Reverb "has" 8 OnePoleSmoother (parameter smoothing)
```

## Constants

```cpp
// Reference sample rate from Dattorro paper
static constexpr double kReferenceSampleRate = 29761.0;

// Input diffusion delay lengths at reference rate
static constexpr size_t kInputDiffDelays[4] = {142, 107, 379, 277};

// Tank delay lengths at reference rate
// Tank A: DD1=672, PreDamp=4453, DD2=1800, PostDamp=3720
// Tank B: DD1=908, PreDamp=4217, DD2=2656, PostDamp=3163

// Default coefficients from Dattorro paper
static constexpr float kBandwidthCoeff = 0.9995f;
static constexpr float kInputDiffusion1Default = 0.75f;
static constexpr float kInputDiffusion2Default = 0.625f;
static constexpr float kDecayDiffusion1 = 0.70f;  // negated when used
static constexpr float kDecayDiffusion2 = 0.50f;  // fixed

// LFO excursion at reference rate
static constexpr float kMaxExcursionRef = 8.0f;  // samples peak

// Output gain applied to tap sums
static constexpr float kOutputGain = 0.6f;

// Parameter smoothing time
static constexpr float kSmoothingTimeMs = 10.0f;
```

## Validation Rules

1. All `float` parameters in `ReverbParams` are clamped to their specified ranges in `setParams()`.
2. `freeze` is a boolean -- no clamping needed.
3. `prepare()` must be called before any processing; processing without prepare is a no-op.
4. `reset()` clears all state but does not deallocate; the instance remains prepared.
5. Sample rate must be in range [8000, 192000] Hz.
6. NaN/Inf inputs are replaced with 0.0f before entering the algorithm.
