# Data Model: Diffusion Network

**Feature**: 015-diffusion-network
**Date**: 2025-12-24

## Entities

### AllpassStage

Internal component - a single Schroeder allpass filter stage.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| delayLine_ | DelayLine | - | Circular buffer for delay |
| baseDelaySamples_ | float | 0-2880 | Base delay time at size=100% |
| delayRatio_ | float | 1.0-4.2 | Ratio multiplier for this stage |
| g_ | float | 0.618 | Allpass coefficient (fixed) |
| prevOutput_ | float | - | Previous output for feedback |
| enableSmoother_ | OnePoleSmoother | - | Fade for density bypass |

**Validation**:
- delayLine_ must be prepared before processing
- delayRatio_ is fixed per stage (set at construction)

### DiffusionNetwork

Main processor - 8 stereo allpass stages with modulation.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| stagesL_ | array<AllpassStage, 8> | - | Left channel stages |
| stagesR_ | array<AllpassStage, 8> | - | Right channel stages |
| lfo_ | LFO | - | Modulation source |
| sizeSmoother_ | OnePoleSmoother | - | Size parameter smoother |
| densitySmoother_ | OnePoleSmoother | - | Density parameter smoother |
| widthSmoother_ | OnePoleSmoother | - | Width parameter smoother |
| modDepthSmoother_ | OnePoleSmoother | - | Mod depth smoother |
| modRateSmoother_ | OnePoleSmoother | - | Mod rate smoother |
| sampleRate_ | float | 44100-192000 | Current sample rate |
| size_ | float | 0-100 | Size in percent |
| density_ | float | 0-100 | Density in percent |
| width_ | float | 0-100 | Stereo width in percent |
| modDepth_ | float | 0-100 | Modulation depth in percent |
| modRate_ | float | 0.1-5.0 | Modulation rate in Hz |

**Validation**:
- All parameters are clamped to their valid ranges
- prepare() must be called before process()

## Constants

```cpp
// Number of stages
static constexpr size_t kNumStages = 8;

// Allpass coefficient (golden ratio inverse)
static constexpr float kAllpassCoeff = 0.618033988749895f;

// Base delay time at size=100% (ms)
static constexpr float kBaseDelayMs = 3.2f;

// Maximum modulation depth (ms)
static constexpr float kMaxModDepthMs = 2.0f;

// Delay ratios per stage (left channel)
static constexpr std::array<float, kNumStages> kDelayRatiosL = {
    1.000f, 1.127f, 1.414f, 1.732f, 2.236f, 2.828f, 3.317f, 4.123f
};

// Stereo decorrelation factor
static constexpr float kStereoOffset = 1.127f;

// Default smoothing time (ms)
static constexpr float kSmoothingMs = 10.0f;
```

## State Transitions

### Lifecycle

```
[Uninitialized] --prepare()--> [Ready] --process()--> [Ready]
                                  |                      |
                                  +-------- reset() -----+
```

### Parameter Updates

All parameter changes are smoothed:
```
setSize(x) --> sizeSmoother_.setTarget(x/100) --> per-sample smoothing
```

## Relationships

```
DiffusionNetwork
├── stagesL_[0..7]: AllpassStage
│   └── delayLine_: DelayLine (Layer 1)
│   └── enableSmoother_: OnePoleSmoother (Layer 1)
├── stagesR_[0..7]: AllpassStage
│   └── delayLine_: DelayLine (Layer 1)
│   └── enableSmoother_: OnePoleSmoother (Layer 1)
├── lfo_: LFO (Layer 1)
├── sizeSmoother_: OnePoleSmoother (Layer 1)
├── densitySmoother_: OnePoleSmoother (Layer 1)
├── widthSmoother_: OnePoleSmoother (Layer 1)
├── modDepthSmoother_: OnePoleSmoother (Layer 1)
└── modRateSmoother_: OnePoleSmoother (Layer 1)
```

**Layer 1 dependencies**: DelayLine, LFO, OnePoleSmoother
**Layer 0 dependencies**: None direct (via Layer 1)
