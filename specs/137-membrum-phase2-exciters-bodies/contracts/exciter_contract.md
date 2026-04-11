# Exciter Backend Contract

**Applies to:** Every alternative type in `Membrum::ExciterBank`'s `std::variant` (`ImpulseExciter`, `MalletExciter`, `NoiseBurstExciter`, `FrictionExciter`, `FMImpulseExciter`, `FeedbackExciter`).

This contract is **structural** — there is no virtual base class per FR-001. Every variant alternative MUST satisfy the public API below so that `std::visit` / `switch` dispatch compiles.

## Required public interface

```cpp
struct Exciter {
    // Lifecycle
    void prepare(double sampleRate, uint32_t voiceId) noexcept;
    void reset() noexcept;

    // Trigger
    void trigger(float velocity) noexcept;   // velocity in [0, 1]
    void release() noexcept;                 // for exciters that have a release phase

    // Per-sample processing
    [[nodiscard]] float process(float bodyFeedback) noexcept;

    // Query
    [[nodiscard]] bool isActive() const noexcept;
};
```

## Behavioral invariants

### Real-time safety (FR-006, FR-072)
- `trigger()`, `release()`, `process()`, `reset()`, and `isActive()` **MUST** be `noexcept`.
- NO heap allocation in any of the above methods. All buffers pre-allocated in `prepare()`.
- NO locks, mutexes, condition variables, or blocking primitives.
- NO exception throwing or catching.
- NO logging, file I/O, or system calls.
- NO dynamic dispatch (virtual calls, `std::function`, type erasure) in the inner loop.

### Output range (SC-007, SC-008)
- `process()` output **MUST** be finite (no NaN, no Inf, no denormal).
- `process()` output **MUST** remain in `[-1.0f, +1.0f]` (peak ≤ 0 dBFS) for any `bodyFeedback` in `[-1.0f, +1.0f]` and any velocity in `[0.0f, 1.0f]`.
- `FeedbackExciter` specifically **MUST** engage its energy limiter to enforce this even when body feedback would otherwise cause Larsen oscillation.

### Velocity response (FR-016, SC-004)
- For any velocity `v ∈ [0, 1]`, `process()` output's spectral centroid measured over the first 10 ms **MUST** vary monotonically with velocity.
- The ratio `spectralCentroid(velocity=1.0) / spectralCentroid(velocity=0.23)` **MUST** be ≥ 2.0 (SC-004 threshold; velocity 30/127 = 0.23 in MIDI-normalized units).
- Velocity also drives amplitude: `peak(velocity=1.0) >> peak(velocity=0.1)`.

### Trigger semantics
- `trigger(velocity)` starts a new exciter event. Previous state may or may not be reset depending on the exciter type (document per exciter).
- `trigger()` with `velocity == 0.0f` is allowed but produces silent output (`process()` returns ~0).
- Calling `trigger()` while already active **MUST** be safe (no crash, no allocation). The exciter retriggers; Phase 2 does not require perfect glitch-free retrigger smoothing, but retriggering **MUST NOT** produce peaks above the pre-retrigger envelope.

### `isActive()` semantics
- Returns `true` after `trigger()` and before the internal envelope has fully decayed.
- Returns `false` when the exciter's contribution is below an effective silence threshold (typically −60 dBFS, documented per exciter).
- The DrumVoice uses `isActive()` + amp envelope `isActive()` as an early-out for silent processing.

### `bodyFeedback` parameter
- Passed to every exciter's `process()` method for uniformity. Exciters that don't use body feedback (Impulse, Mallet, NoiseBurst, Friction, FMImpulse) **MUST** ignore it.
- Only `FeedbackExciter` consumes it: `feedback_amount * filter(saturate(bodyFeedback))`.
- Value range: last sample of the body output (arbitrary float; may exceed `[-1, +1]` pre-soft-clip).

### Sample-rate change
- `prepare(sampleRate, voiceId)` may be called multiple times (e.g., host sample-rate change). Subsequent calls **MUST** reset internal state and recompute sample-rate-dependent coefficients without allocating.

## Per-exciter addenda

### `ImpulseExciter`
- Velocity → `ImpactExciter::trigger(velocity, hardness=lerp(0.3, 0.8, velocity), mass=0.3, brightness=lerp(0.15, 0.4, velocity), position=0, f0=0)`.
- `isActive()` wraps `core_.isActive()`.
- First-5-ms output dominated by impulse transient; body takes over thereafter.

### `MalletExciter`
- Same `ImpactExciter` backend, different parameter envelope:
  - Contact duration: `lerp(8 ms, 1 ms, velocity)`.
  - Mallet hardness exponent α: `lerp(1.5, 4.0, velocity)`.
  - SVF brightness rising with velocity.
- First-2-ms output has **lower spectral centroid** than `ImpulseExciter` at the same velocity (acceptance scenario US1-2).

### `NoiseBurstExciter`
- Wrapped `NoiseOscillator` + `SVF` + linear decay envelope.
- Velocity → filter cutoff (`lerp(200 Hz, 5000 Hz+, velocity)`), burst duration (`lerp(15 ms, 2 ms, velocity)`), amplitude.
- `isActive()` returns `burstSamplesRemaining_ > 0`.
- First-20-ms output's spectral centroid > 2× Impulse case (acceptance scenario US1-3).

### `FrictionExciter`
- Wrapped `BowExciter` in transient mode. Phase 2 triggers a short envelope on bow pressure (≤ 50 ms) and releases automatically.
- Velocity → `BowExciter::setPressure`, `setSpeed`.
- Output contains characteristic stick-slip signature (non-monotonic envelope) for 20–50 ms (acceptance scenario US1-4).
- Sustained bowing (infinite pressure, steady-state oscillation) is **out of scope** — Phase 4.

### `FMImpulseExciter`
- Carrier + modulator `FMOperator` pair. Default carrier:modulator ratio = 1:1.4 (Chowning bell; configurable via secondary parameter `kExciterFMRatioId`).
- `ampEnv` (≤ 100 ms) gates output amplitude.
- `modIndexEnv` decays **faster** than `ampEnv` — spec 135 mandate.
- First-50-ms output contains clearly inharmonic sidebands (acceptance scenario US1-5).

### `FeedbackExciter`
- `process(bodyFeedback)` computes a soft-limited filtered feedback of the body output.
- Velocity → `feedbackAmount_` only (not the energy limiter threshold).
- **Stability guarantee:** peak ≤ 0 dBFS for ANY body, ANY velocity. Test at all 6 bodies at velocity 127 with `feedbackAmount_ = max`.
- Energy limiter engages via `EnvelopeFollower` measuring body RMS; when RMS > `kEnergyThreshold`, `energyGain` is scaled down proportionally.
- Output contains a self-sustaining component that outlasts the direct body decay (acceptance scenario US1-6).

## Swap semantics (deferred to next trigger — FR-004, FR-005)

- `ExciterBank::setExciterType(type)` sets `pendingType_` only; does NOT switch the active variant alternative immediately.
- On the next `trigger()`:
  1. If `pendingType_ != currentType_`, call `reset()` on the new alternative, emplace it into the variant, update `currentType_`.
  2. Call `trigger(velocity)` on the newly active alternative.
- Between a `setExciterType()` call and the next `trigger()`, any ringing tail from the previous exciter continues to play through `process()` — deferred swap preserves the tail (FR-005 guarantee).

## Test coverage requirements

Every exciter backend MUST have unit tests covering:

1. **Allocation detector test** — `trigger()` and `process()` must show zero heap activity (`allocation_detector.h`).
2. **Velocity spectral centroid** — process at velocity 0.23 and 1.0; assert centroid ratio ≥ 2.0.
3. **Finite-output sanity** — process 1 s of audio; assert no NaN/Inf/denormal; assert peak ≤ 1.0.
4. **Retrigger safety** — trigger twice in quick succession; assert no crash, no NaN.
5. **Reset idempotence** — call `reset()` twice; assert internal state is deterministic.
6. **Sample rate change** — call `prepare(44100.0)` then `prepare(96000.0)` then `trigger()`; assert output is reasonable.
