# Data Model: Parameter Smoother

**Feature**: 005-parameter-smoother
**Date**: 2025-12-22
**Purpose**: Define class structures and relationships for parameter smoothing primitives

---

## Entity Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Iterum::DSP Namespace                        │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐     │
│  │  OnePoleSmoother │  │   LinearRamp    │  │   SlewLimiter   │     │
│  │                 │  │                 │  │                 │     │
│  │ - coefficient_  │  │ - increment_    │  │ - riseRate_     │     │
│  │ - current_      │  │ - current_      │  │ - fallRate_     │     │
│  │ - target_       │  │ - target_       │  │ - current_      │     │
│  │ - timeMs_       │  │ - rampTimeMs_   │  │ - target_       │     │
│  │ - sampleRate_   │  │ - sampleRate_   │  │ - sampleRate_   │     │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘     │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Utility Functions                          │   │
│  │                                                               │   │
│  │  constexpr float calculateOnePolCoefficient(ms, sr)          │   │
│  │  constexpr float calculateLinearIncrement(delta, ms, sr)     │   │
│  │  constexpr float calculateSlewRate(unitsPerMs, sr)           │   │
│  │                                                               │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                      Constants                                │   │
│  │                                                               │   │
│  │  kDefaultSmoothingTimeMs = 5.0f                              │   │
│  │  kCompletionThreshold = 0.0001f                              │   │
│  │  kMinSmoothingTimeMs = 0.1f                                  │   │
│  │  kMaxSmoothingTimeMs = 1000.0f                               │   │
│  │  kDenormalThreshold = 1e-15f                                 │   │
│  │                                                               │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Entity Definitions

### OnePoleSmoother

Exponential smoothing using first-order IIR filter topology. Most common smoother type for audio parameters.

| Field | Type | Description | Constraints |
|-------|------|-------------|-------------|
| `coefficient_` | `float` | Smoothing coefficient (0.0-1.0) | Calculated from timeMs_ and sampleRate_ |
| `current_` | `float` | Current smoothed value | Any float |
| `target_` | `float` | Target value to approach | Any float |
| `timeMs_` | `float` | Time to 99% of target | 0.1 - 1000.0 ms |
| `sampleRate_` | `float` | Sample rate for coefficient calculation | 1.0 - 384000.0 Hz |

**State Transitions**:
- Initial → Smoothing: `setTarget(newValue)` called
- Smoothing → Complete: `|current - target| < kCompletionThreshold`
- Any → Initial: `reset()` called or `snapToTarget()` called

**Invariants**:
- `coefficient_` recalculated whenever `timeMs_` or `sampleRate_` changes
- `current_` snapped to `target_` when within threshold

---

### LinearRamp

Constant-rate parameter changes. Used for delay time to create tape-like pitch effects.

| Field | Type | Description | Constraints |
|-------|------|-------------|-------------|
| `increment_` | `float` | Per-sample increment | Calculated from target delta and rampTimeMs_ |
| `current_` | `float` | Current ramped value | Any float |
| `target_` | `float` | Target value to reach | Any float |
| `rampTimeMs_` | `float` | Time to complete ramp | 0.1 - 10000.0 ms |
| `sampleRate_` | `float` | Sample rate for increment calculation | 1.0 - 384000.0 Hz |

**State Transitions**:
- Idle → Ramping: `setTarget(newValue)` called with different value
- Ramping → Idle: `current_` reaches `target_` exactly
- Ramping → Ramping: `setTarget(newValue)` called (recalculates increment)
- Any → Idle: `snapToTarget()` called

**Invariants**:
- `increment_` recalculated whenever `target_`, `rampTimeMs_`, or `sampleRate_` changes
- When ramping toward target, increment sign matches direction
- Never overshoots target (clamped each sample)

---

### SlewLimiter

Rate-limited parameter changes with separate rise and fall rates.

| Field | Type | Description | Constraints |
|-------|------|-------------|-------------|
| `riseRate_` | `float` | Max positive rate per sample | > 0.0 |
| `fallRate_` | `float` | Max negative rate per sample | > 0.0 |
| `current_` | `float` | Current limited value | Any float |
| `target_` | `float` | Target value to approach | Any float |
| `riseRatePerMs_` | `float` | User-specified rise rate | > 0.0 units/ms |
| `fallRatePerMs_` | `float` | User-specified fall rate | > 0.0 units/ms |
| `sampleRate_` | `float` | Sample rate for rate calculation | 1.0 - 384000.0 Hz |

**State Transitions**:
- Idle → Limiting: `setTarget(newValue)` where `|delta| > rate`
- Limiting → Idle: `current_` within one step of `target_`
- Any → Idle: `snapToTarget()` called

**Invariants**:
- Per-sample rates recalculated when `sampleRate_` changes
- Rising uses `riseRate_`, falling uses `fallRate_`
- Snaps to target when within one rate step

---

## Validation Rules

### OnePoleSmoother

| Rule | Condition | Action |
|------|-----------|--------|
| Time clamping | `timeMs < kMinSmoothingTimeMs` | Clamp to minimum |
| Time clamping | `timeMs > kMaxSmoothingTimeMs` | Clamp to maximum |
| Zero time | `timeMs ≈ 0` | Behave as snap-to-target |
| Sample rate | `sampleRate <= 0` | Use default 44100 Hz |

### LinearRamp

| Rule | Condition | Action |
|------|-----------|--------|
| Zero ramp time | `rampTimeMs ≈ 0` | Snap to target immediately |
| Same target | `newTarget == current` | No-op, remain idle |
| Overshoot prevention | `current` would pass `target` | Clamp to exact `target` |

### SlewLimiter

| Rule | Condition | Action |
|------|-----------|--------|
| Zero rate | `rate <= 0` | Use minimum rate (0.0001) |
| Same target | `newTarget == current` | No-op, remain idle |
| Within rate | `|delta| <= rate` | Snap to target |

---

## Relationships

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Usage Contexts                               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  SmoothedBiquad (Layer 1)                                          │
│       └── Uses: OnePoleSmoother (per coefficient)                  │
│                                                                     │
│  DelayEngine (Layer 3)                                              │
│       └── Uses: LinearRamp (delay time)                            │
│       └── Uses: OnePoleSmoother (feedback, mix)                    │
│                                                                     │
│  FeedbackNetwork (Layer 3)                                          │
│       └── Uses: SlewLimiter (feedback amount)                      │
│       └── Uses: OnePoleSmoother (filter params)                    │
│                                                                     │
│  Any Parameter (Controller)                                         │
│       └── Uses: OnePoleSmoother (most common)                      │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Memory Layout

Each smoother has minimal footprint suitable for per-parameter instantiation:

| Class | Size (bytes) | Cache Lines |
|-------|--------------|-------------|
| OnePoleSmoother | 20 | < 1 |
| LinearRamp | 20 | < 1 |
| SlewLimiter | 28 | < 1 |

All fit within a single cache line (64 bytes), enabling efficient per-parameter smoothing.
