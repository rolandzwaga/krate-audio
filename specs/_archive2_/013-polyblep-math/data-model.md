# Data Model: PolyBLEP Math Foundations

**Spec**: 013-polyblep-math | **Date**: 2026-02-03

---

## Entities

### 1. PolyBLEP Correction (polyblep.h)

Pure stateless mathematical functions. No entity state -- these are free functions in `Krate::DSP` namespace.

**Functions**:

| Function | Signature | Description |
|----------|-----------|-------------|
| `polyBlep` | `[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept` | 2-point (2nd-degree) polynomial band-limited step correction |
| `polyBlep4` | `[[nodiscard]] constexpr float polyBlep4(float t, float dt) noexcept` | 4-point (4th-degree) polynomial band-limited step correction |
| `polyBlamp` | `[[nodiscard]] constexpr float polyBlamp(float t, float dt) noexcept` | 2-point (3rd-degree) polynomial band-limited ramp correction |
| `polyBlamp4` | `[[nodiscard]] constexpr float polyBlamp4(float t, float dt) noexcept` | 4-point (5th-degree) polynomial band-limited ramp correction |

**Parameters**:

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `t` | `float` | [0, 1) | Normalized phase position within one oscillator period |
| `dt` | `float` | (0, 0.5) | Normalized phase increment = frequency / sampleRate. Precondition: 0 < dt < 0.5 |

**Return Value**: `float` -- correction value to subtract from naive waveform output.
- Returns exactly `0.0f` when `t` is outside the correction region.
- Returns non-zero when `t` is within the correction region near a discontinuity.

**Correction Regions**:

| Function | Before-wrap region | After-wrap region |
|----------|-------------------|------------------|
| `polyBlep` | [1-dt, 1) | [0, dt) |
| `polyBlep4` | [1-2*dt, 1) | [0, 2*dt) |
| `polyBlamp` | [1-dt, 1) | [0, dt) |
| `polyBlamp4` | [1-2*dt, 1) | [0, 2*dt) |

**Dependencies**: `math_constants.h` (included for potential constant needs; may not be used if only arithmetic is needed).

---

### 2. PhaseAccumulator (phase_utils.h)

Lightweight value-type struct for managing oscillator phase state.

```cpp
struct PhaseAccumulator {
    double phase = 0.0;       // Current phase [0, 1)
    double increment = 0.0;   // Phase advance per sample (frequency / sampleRate)
};
```

**Fields**:

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `phase` | `double` | 0.0 | [0, 1) | Current position within oscillator period |
| `increment` | `double` | 0.0 | [0, 0.5) | Phase advance per sample = freq/sampleRate |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `advance` | `[[nodiscard]] bool advance() noexcept` | Advance phase by increment, wrap if >= 1.0. Returns true if wrap occurred. |
| `reset` | `void reset() noexcept` | Reset phase to 0.0, preserve increment. |
| `setFrequency` | `void setFrequency(float frequency, float sampleRate) noexcept` | Convenience: sets increment = frequency / sampleRate. |

**State Transitions**:

```
[Initial] --advance()--> [phase += increment]
  |                           |
  |                      phase < 1.0? --yes--> return false
  |                           |
  |                      phase >= 1.0? --yes--> phase -= 1.0, return true
  |
  reset() ---> phase = 0.0
```

**Invariants**:
- After `advance()`, `phase` is always in [0, 1) (assuming increment < 1.0)
- After `reset()`, `phase` == 0.0
- `increment` is never modified by `advance()` or `reset()`

---

### 3. Phase Utility Functions (phase_utils.h)

Free functions in `Krate::DSP` namespace.

| Function | Signature | Description |
|----------|-----------|-------------|
| `calculatePhaseIncrement` | `[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept` | Returns frequency / sampleRate. Returns 0.0 if sampleRate == 0. |
| `wrapPhase` | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Wrap phase to [0, 1) via subtraction. Handles negative values. |
| `detectPhaseWrap` | `[[nodiscard]] constexpr bool detectPhaseWrap(double currentPhase, double previousPhase) noexcept` | Returns true if current < previous (wrap occurred). |
| `subsamplePhaseWrapOffset` | `[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept` | Returns fractional sample position [0, 1) where the wrap occurred. Formula: `(increment > 0.0) ? (phase / increment) : 0.0`. |

---

## Relationships

```
polyblep.h                          phase_utils.h
  |                                   |
  +-- polyBlep(t, dt)                 +-- PhaseAccumulator
  +-- polyBlep4(t, dt)                |     .phase
  +-- polyBlamp(t, dt)                |     .increment
  +-- polyBlamp4(t, dt)               |     .advance() -> bool
  |                                   |     .reset()
  |                                   |     .setFrequency(freq, sr)
  |                                   |
  |                                   +-- calculatePhaseIncrement(freq, sr)
  |                                   +-- wrapPhase(phase)
  |                                   +-- detectPhaseWrap(curr, prev)
  |                                   +-- subsamplePhaseWrapOffset(phase, inc)
  |                                   |
  +--- NO dependency between them ----+
```

Both files are Layer 0 (core/). `polyblep.h` depends only on `math_constants.h`. `phase_utils.h` depends only on `<cmath>` (stdlib). Neither depends on the other.

---

## Validation Rules

### polyblep.h

| Rule | Applied To | Description |
|------|-----------|-------------|
| Precondition | All 4 functions | `0 < dt < 0.5` (undefined behavior otherwise) |
| Zero outside region | All 4 functions | Returns 0.0f when t is not in correction region |
| NaN/Inf propagation | All 4 functions | IEEE 754 propagation, no sanitization |
| Continuity (C1) | polyBlep, polyBlamp | 2nd-degree polynomial ensures C1 continuity |
| Continuity (C3) | polyBlep4, polyBlamp4 | 4th-degree polynomial ensures C3 continuity |

### phase_utils.h

| Rule | Applied To | Description |
|------|-----------|-------------|
| Division-by-zero guard | calculatePhaseIncrement | Returns 0.0 when sampleRate == 0 |
| Range [0, 1) | wrapPhase | Output always in [0, 1) for finite inputs |
| Negative handling | wrapPhase | Handles negative inputs by adding 1.0 iteratively |
| Wrap detection | detectPhaseWrap | True when currentPhase < previousPhase |
| Subsample range | subsamplePhaseWrapOffset | Returns value in [0, 1) |
| Double precision | PhaseAccumulator | Both phase and increment are double |
