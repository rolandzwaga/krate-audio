# Data Model: Delay Line DSP Primitive

**Feature**: 002-delay-line
**Date**: 2025-12-22

## Entities

### DelayLine

The primary class providing circular buffer delay functionality.

| Attribute | Type | Description |
|-----------|------|-------------|
| buffer_ | std::vector<float> | Circular buffer storing audio samples |
| mask_ | size_t | Bitmask for power-of-2 wraparound (bufferSize - 1) |
| writeIndex_ | size_t | Current write position in buffer |
| allpassState_ | float | Previous output for allpass interpolation |
| sampleRate_ | double | Current sample rate (for time-to-samples conversion) |
| maxDelaySamples_ | size_t | Maximum delay in samples |

### State Transitions

```
┌─────────────────────────────────────────────────────────────┐
│                    DelayLine Lifecycle                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────┐    prepare()    ┌───────────┐                │
│  │  Created │ ───────────────>│  Ready    │                │
│  └──────────┘                 └───────────┘                │
│       │                            │ │                      │
│       │                   write()  │ │ read*()              │
│       │                   ┌────────┘ └────────┐             │
│       │                   ▼                   ▼             │
│       │              ┌─────────────────────────┐            │
│       │              │      Processing         │            │
│       │              │ (can call write/read)   │            │
│       │              └─────────────────────────┘            │
│       │                        │                            │
│       │               reset()  │                            │
│       │                        ▼                            │
│       │              ┌─────────────────────────┐            │
│       │              │   Ready (buffer zeroed) │            │
│       │              └─────────────────────────┘            │
│       │                        │                            │
│       │              prepare() │ (re-configure)             │
│       │                        ▼                            │
│       │              ┌─────────────────────────┐            │
│       │              │   Ready (resized)       │            │
│       └──────────────│                         │            │
│                      └─────────────────────────┘            │
└─────────────────────────────────────────────────────────────┘
```

### Validation Rules

| Rule | Condition | Action |
|------|-----------|--------|
| VR-001 | delaySamples < 0 | Clamp to 0 |
| VR-002 | delaySamples > maxDelaySamples_ | Clamp to maxDelaySamples_ |
| VR-003 | delaySamples is NaN | Return 0.0f |
| VR-004 | delaySamples is Infinity | Clamp to maxDelaySamples_ |
| VR-005 | prepare() called with maxDelaySeconds <= 0 | Use minimum 1 sample |
| VR-006 | prepare() called with sampleRate <= 0 | Use default 44100 Hz |

### Memory Layout

```
buffer_ (power-of-2 aligned):
┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│  0  │  1  │  2  │ ... │ W-2 │ W-1 │ W   │ ... │
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
                          │     │     │
                          │     │     └── writeIndex_ (current)
                          │     └── Most recent sample
                          └── Older samples

Read at delay D:
  readIndex = (writeIndex_ - D) & mask_
```

## Relationships

```
┌─────────────────────────────────────────────────────────────┐
│                    Layer Dependencies                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Layer 2+ (Processors/Systems/Features)                      │
│       │                                                      │
│       │ uses                                                 │
│       ▼                                                      │
│  ┌──────────────┐                                           │
│  │  DelayLine   │  Layer 1: DSP Primitive                   │
│  └──────────────┘                                           │
│       │                                                      │
│       │ uses                                                 │
│       ▼                                                      │
│  ┌──────────────┐                                           │
│  │ std::vector  │  Standard Library                         │
│  │ std::fill    │                                           │
│  └──────────────┘                                           │
│                                                              │
│  No Layer 0 dependencies (self-contained)                    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## Usage Patterns

### Simple Fixed Delay

```cpp
DelayLine delay;
delay.prepare(44100.0, 1.0f);  // 1 second max

// In audio callback:
delay.write(inputSample);
float output = delay.read(22050);  // 0.5 second delay
```

### Modulated Delay (Chorus/Flanger)

```cpp
DelayLine delay;
delay.prepare(44100.0, 0.050f);  // 50ms max for chorus

// In audio callback:
float lfoValue = lfo.process();  // 0 to 1
float delaySamples = 200.0f + lfoValue * 400.0f;  // 4.5ms - 13.6ms

delay.write(inputSample);
float output = delay.readLinear(delaySamples);  // Smooth modulation
```

### Feedback Delay

```cpp
DelayLine delay;
delay.prepare(44100.0, 2.0f);  // 2 second max

float feedback = 0.7f;
float delayTime = 44100.0f;  // 1 second

// In audio callback:
float delayedSample = delay.readAllpass(delayTime);  // Unity gain
float inputWithFeedback = inputSample + feedback * delayedSample;
delay.write(inputWithFeedback);
float output = delayedSample;
```
