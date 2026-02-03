# Data Model: Phase Accumulator Utilities

**Spec**: 014-phase-accumulation-utils | **Date**: 2026-02-03

---

## Entities

### 1. PhaseAccumulator (phase_utils.h)

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
| `phase` | `double` | 0.0 | [0, 1) | Current position within oscillator period. Direct read access is intentional for waveform generation (e.g., `2.0f * acc.phase - 1.0f`). |
| `increment` | `double` | 0.0 | [0, 1.0) | Phase advance per sample = freq/sampleRate. Precondition: must be < 1.0 (frequency < sampleRate). |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `advance` | `[[nodiscard]] bool advance() noexcept` | Advance phase by increment. Wraps via `phase -= 1.0` when `phase >= 1.0`. Returns true if wrap occurred. |
| `reset` | `void reset() noexcept` | Reset phase to 0.0, preserve increment. |
| `setFrequency` | `void setFrequency(float frequency, float sampleRate) noexcept` | Convenience: sets increment via `calculatePhaseIncrement(frequency, sampleRate)`. |

**State Transitions**:

```
[Initial: phase=0.0, increment=0.0]
    |
    setFrequency(freq, sr)
    |
    v
[Configured: phase=0.0, increment=freq/sr]
    |
    advance() -- per sample
    |
    v
[Running: phase in [0, 1)]
    |
    phase < 1.0? --yes--> return false, continue
    |
    phase >= 1.0? --yes--> phase -= 1.0, return true (wrap!)
    |
    reset()
    |
    v
[Reset: phase=0.0, increment preserved]
```

**Invariants**:
- After `advance()`, `phase` is always in [0, 1) (assuming increment < 1.0)
- After `reset()`, `phase` == 0.0 and `increment` is unchanged
- `increment` is never modified by `advance()` or `reset()`
- After `setFrequency()`, `increment` == `calculatePhaseIncrement(frequency, sampleRate)`
- Default-initialized PhaseAccumulator has `phase = 0.0` and `increment = 0.0` (FR-013)

---

### 2. Phase Utility Functions (phase_utils.h)

Free functions in `Krate::DSP` namespace. All are `[[nodiscard]] constexpr ... noexcept`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `calculatePhaseIncrement` | `[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept` | Returns `static_cast<double>(frequency) / static_cast<double>(sampleRate)`. Returns 0.0 when sampleRate == 0 (division-by-zero guard, FR-002). |
| `wrapPhase` | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Wraps phase to [0, 1) via subtraction (`while (phase >= 1.0) phase -= 1.0`) and addition (`while (phase < 0.0) phase += 1.0`). Handles any finite input (FR-003, FR-004). |
| `detectPhaseWrap` | `[[nodiscard]] constexpr bool detectPhaseWrap(double currentPhase, double previousPhase) noexcept` | Returns `currentPhase < previousPhase` (FR-005). Assumes monotonically increasing phase. |
| `subsamplePhaseWrapOffset` | `[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept` | Returns `phase / increment` when increment > 0, else 0.0 (FR-006). Returns value in [0, 1) when called correctly after a detected wrap (FR-007). |

---

## Relationships

```
phase_utils.h
  |
  +-- calculatePhaseIncrement(freq, sr) --> double
  +-- wrapPhase(phase) --> double
  +-- detectPhaseWrap(curr, prev) --> bool
  +-- subsamplePhaseWrapOffset(phase, inc) --> double
  |
  +-- PhaseAccumulator
        .phase: double
        .increment: double
        .advance() --> bool        [uses internal phase -= 1.0]
        .reset()                   [sets phase = 0.0]
        .setFrequency(freq, sr)    [calls calculatePhaseIncrement]
```

**Internal dependency**: `PhaseAccumulator::setFrequency()` calls `calculatePhaseIncrement()`. No other internal dependencies.

**External consumers** (future, from OSC-ROADMAP):
- Phase 2: PolyBLEP Oscillator -- composes PhaseAccumulator, uses detectPhaseWrap + subsamplePhaseWrapOffset
- Phase 3: Wavetable Oscillator -- composes PhaseAccumulator
- Phase 5: Sync Oscillator -- uses subsamplePhaseWrapOffset for sub-sample sync reset timing
- Phase 8: FM Operator -- composes PhaseAccumulator
- Phase 10-14: Various oscillators -- compose PhaseAccumulator

**Existing code compatibility** (not refactored, but verified compatible):
- `lfo.h` -- uses identical `double phase_` + `double phaseIncrement_` pattern
- `audio_rate_filter_fm.h` -- uses identical pattern
- `frequency_shifter.h` -- uses different quadrature rotation approach (not directly compatible)

---

## Validation Rules

| Rule | Applied To | Description |
|------|-----------|-------------|
| Division-by-zero guard | calculatePhaseIncrement | Returns 0.0 when sampleRate == 0 (FR-002) |
| Range [0, 1) | wrapPhase | Output always in [0, 1) for all finite inputs (FR-004) |
| Negative handling | wrapPhase | Handles negative inputs by adding 1.0 iteratively (FR-003) |
| Wrap detection | detectPhaseWrap | True when currentPhase < previousPhase (FR-005) |
| Zero-increment guard | subsamplePhaseWrapOffset | Returns 0.0 when increment == 0 (FR-006) |
| Subsample range | subsamplePhaseWrapOffset | Returns value in [0, 1) when preconditions met (FR-007) |
| Double precision | PhaseAccumulator | Both phase and increment are double (FR-012) |
| Default initialization | PhaseAccumulator | phase = 0.0, increment = 0.0 (FR-013) |
| constexpr | All standalone functions | Must be constexpr-evaluable at compile time (FR-014) |
| noexcept | All functions and methods | All must be noexcept (FR-014, FR-015) |
| [[nodiscard]] | All functions + advance() | Return values must not be silently discarded (FR-014, FR-015) |
