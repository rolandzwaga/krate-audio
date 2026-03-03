# Research: Oscillator Sync (Phase 5)

**Feature**: 018-oscillator-sync | **Date**: 2026-02-04

This document resolves all NEEDS CLARIFICATION items and consolidates research findings for the SyncOscillator implementation.

---

## 1. Hard Sync Phase Reset Technique

### Decision: Reset slave phase to master's fractional position scaled by frequency ratio

### Rationale

The correct hard sync formula resets the slave phase to account for the sub-sample timing of the master's wrap. Per Eli Brandt's "Hard Sync Without Aliasing" (CMU, 2001):

- When the master phase wraps, compute the exact fractional sample position of the wrap using `subsamplePhaseWrapOffset(masterPhase, masterIncrement)`.
- The slave's new phase = `masterFractionalPhase * (slaveIncrement / masterIncrement)`.
- This ensures that when master and slave frequencies are identical (1:1 ratio), the slave's natural wrap and the sync reset coincide perfectly, producing a clean pass-through with no audible artifacts.

Resetting to zero (the naive approach) causes the slave waveform to jitter by one sample period at the reset point, producing audible artifacts when master and slave frequencies are nearly equal.

### Alternatives Considered

1. **Reset to zero**: Simpler, but introduces phase jitter and fails the 1:1 ratio clean pass-through test (SC-003). Rejected.
2. **Reset to master's raw phase**: Would be correct only if master and slave increments are equal. Does not generalize to arbitrary frequency ratios. Rejected.

---

## 2. MinBLEP Discontinuity Correction at Sync Reset

### Decision: Use existing MinBlepTable::Residual::addBlep() for hard sync and phase advance step discontinuities

### Rationale

The existing `MinBlepTable::Residual` API is a perfect fit:
- `addBlep(subsampleOffset, amplitude)` stamps a scaled minBLEP correction into the ring buffer at the given sub-sample position.
- `consume()` returns the next correction value to add to the raw output.
- The residual buffer supports overlapping corrections (accumulated additively).

The correction formula at each hard sync reset:
1. Compute `valueBefore` = slave waveform evaluated at current phase (before reset).
2. Compute `valueAfter` = slave waveform evaluated at new phase (after reset).
3. `discontinuity = valueAfter - valueBefore`.
4. Call `residual.addBlep(subsampleOffset, discontinuity)`.

The minBLEP correction applies `correction[i] = amplitude * (table.sample(offset, i) - 1.0)`, which is the difference between the ideal band-limited step and the naive step. This smooths the hard transition over ~16 samples (the table length).

### Alternatives Considered

1. **PolyBLEP-only correction**: The slave oscillator already applies PolyBLEP at its natural discontinuities. However, sync resets create *additional* discontinuities not at the natural wrap points. PolyBLEP cannot correct these because it requires knowing the phase position relative to a wrap point. minBLEP handles arbitrary sub-sample-positioned discontinuities. Rejected as sole approach.
2. **No discontinuity correction**: Would produce audible aliasing at sync reset points. Rejected.

---

## 3. MinBLAMP for Reverse Sync Derivative Discontinuities

### Decision: Add `addBlamp()` method to `MinBlepTable::Residual` by numerically integrating the existing minBLEP table

### Rationale

Reverse sync reverses the slave's traversal direction at each master wrap. The waveform itself is continuous at the reversal point (no step discontinuity), but the first derivative is discontinuous (the slope suddenly inverts). This requires a minBLAMP (band-limited ramp) correction rather than a minBLEP.

**How to compute the minBLAMP table from the minBLEP table:**

The minBLAMP is the integral of the minBLEP. Given the existing minBLEP table `table[i]`, the minBLAMP table is computed as:

```
blampTable[0] = 0.0
blampTable[i] = blampTable[i-1] + (table[i] - 1.0)  // Integrate (table - 1.0)
```

Wait -- more precisely, the minBLAMP correction is the integral of the minBLEP residual. The minBLEP residual is `table.sample(offset, i) - 1.0`. The minBLAMP residual is the running sum of the minBLEP residual, which converges to zero (the step function's total area above and below the settled value sums to zero for minimum-phase filters).

**Implementation approach**: Rather than precomputing a separate minBLAMP table, we implement `addBlamp()` in the Residual by computing the integrated correction on the fly from the minBLEP table. For each sample `i` in the correction window:

```
blepResidual[i] = table.sample(offset, i) - 1.0
blampCorrection[i] = sum(blepResidual[0..i])  // Running sum
buffer[(readIdx + i) % len] += amplitude * blampCorrection[i]
```

However, this requires O(N^2) operations per addBlamp call (N = table length = 16). Since N is small and sync events are at most one per sample, this is acceptable.

**Alternative: Precompute minBLAMP table** alongside minBLEP during `prepare()`. This gives O(N) addBlamp at the cost of storing an additional table. With N=16 and oversamplingFactor=64, the additional storage is 16*64 = 1024 floats = 4KB. This is the preferred approach for real-time efficiency.

### Scaling the minBLAMP correction

The minBLAMP correction amplitude is the change in the waveform's derivative at the reversal point:
- Before reversal: derivative = `dWaveform/dt * slaveIncrement` (positive direction)
- After reversal: derivative = `dWaveform/dt * (-slaveIncrement)` (reversed)
- Derivative discontinuity = `2 * dWaveform/dt * slaveIncrement`

For a sawtooth wave: `dWaveform/dt = 2.0` (slope of 2t-1), so the derivative discontinuity = `4.0 * slaveIncrement`.

The general formula: evaluate the slope at the current phase for the active waveform, then multiply by 2 (because direction reversal doubles the slope change) and by slaveIncrement (because the minBLAMP is in sample units, not phase units).

### Alternatives Considered

1. **PolyBLAMP only**: The existing `polyBlamp4()` in `core/polyblep.h` could handle derivative discontinuities. However, like PolyBLEP, it requires knowing the position relative to a natural discontinuity. Sync reversals happen at arbitrary positions, not at natural discontinuity points. The polyBLAMP functions are designed for phase positions near 0 or 1. Rejected.
2. **Skip correction for reverse sync**: Reverse sync produces continuous waveforms with only derivative discontinuities. The aliasing from derivative discontinuities is typically 6 dB/octave less severe than step discontinuities. However, for high-quality output, correction is still needed. Rejected.
3. **Approximate with minBLEP**: Could approximate the ramp correction with a scaled/differentiated step. This loses precision. Rejected.

### Final Decision: Precompute minBLAMP table during `prepare()`

Store the minBLAMP table as an additional vector in `MinBlepTable`. Add `sampleBlamp(subsampleOffset, index)` method and `addBlamp(subsampleOffset, amplitude)` to `Residual`.

---

## 4. Phase Advance Sync Mode

### Decision: Implement as interpolated hard sync (lerp between current phase and synced phase)

### Rationale

Phase advance sync at syncAmount = 1.0 is mathematically equivalent to hard sync. The formula:

```
phaseAdvance = syncAmount * (syncedPhase - currentSlavePhase)
newSlavePhase = currentSlavePhase + phaseAdvance
```

At syncAmount = 0.0: `newSlavePhase = currentSlavePhase` (no change, free-running).
At syncAmount = 1.0: `newSlavePhase = syncedPhase` (full reset, equivalent to hard sync).

The discontinuity at the phase advance point is computed the same way as hard sync:
```
valueBefore = waveform(currentSlavePhase)
valueAfter = waveform(newSlavePhase)
discontinuity = valueAfter - valueBefore
```

For small syncAmount, the phase advance is small and the discontinuity is proportionally small, requiring proportionally less minBLEP correction.

### Alternatives Considered

1. **Separate phase advance calculation**: A different formula that accumulates phase offsets over multiple cycles. More complex and harder to verify equivalence with hard sync at 1.0. Rejected in favor of the simpler interpolation approach.

---

## 5. Sync Amount Crossfading

### Decision: Linear interpolation between free-running and synced behavior

### Rationale

For Hard sync mode with syncAmount between 0.0 and 1.0:
```
effectivePhase = lerp(currentSlavePhase, syncedPhase, syncAmount)
```

This directly controls the size of the phase discontinuity. At syncAmount = 0.0, no phase change occurs. At syncAmount = 1.0, full reset. The discontinuity amplitude scales linearly with syncAmount.

For Reverse sync mode with syncAmount between 0.0 and 1.0:
```
effectiveIncrement = lerp(forwardIncrement, reversedIncrement, syncAmount)
```

At syncAmount = 0.0, the slave continues forward. At syncAmount = 1.0, full reversal.

---

## 6. Master Oscillator Implementation

### Decision: Use PhaseAccumulator directly (not PolyBlepOscillator)

### Rationale

The master oscillator's sole purpose is to provide timing for sync events via phase wrap detection. Its waveform output is never used. Using a full `PolyBlepOscillator` for the master would waste computation on waveform generation and PolyBLEP correction that are discarded.

The `PhaseAccumulator` struct from `core/phase_utils.h` provides exactly what is needed:
- `advance()` returns `true` when phase wraps
- `phase` member gives the current phase for sub-sample offset computation
- `setFrequency(hz, sampleRate)` sets the increment

This saves approximately 20-40 cycles/sample compared to using a full oscillator.

### Alternatives Considered

1. **PolyBlepOscillator as master**: Simpler composition but wastes ~30 cycles/sample on unused waveform computation. With a 100-150 cycle/sample budget, this is significant. Rejected.
2. **External phase signal**: Accepting an external phase as input for the master. More flexible but adds API complexity and deviates from the self-contained processor pattern used by other Layer 2 components. Rejected.

---

## 7. Slave Waveform Evaluation for Discontinuity Computation

### Decision: Inline waveform evaluation function that mirrors PolyBlepOscillator's naive waveform (without PolyBLEP correction)

### Rationale

To compute the discontinuity amplitude at a sync reset, we need to evaluate the slave waveform at the current phase and at the new phase. We need the *naive* (uncorrected) waveform value because:
1. The PolyBLEP correction is position-dependent and would be incorrect for arbitrary phase evaluation.
2. The discontinuity is in the naive waveform; the minBLEP corrects this discontinuity.

The waveform evaluation function:
```cpp
float evaluateWaveform(OscWaveform wf, float phase, float pulseWidth) {
    switch (wf) {
        case Sine:      return sin(2*pi*phase);
        case Sawtooth:  return 2*phase - 1;
        case Square:    return phase < 0.5 ? 1 : -1;
        case Pulse:     return phase < pulseWidth ? 1 : -1;
        case Triangle:  return phase < 0.5 ? (4*phase - 1) : (3 - 4*phase);
    }
}
```

Note: For Triangle waveform, the naive evaluation is straightforward (piecewise linear). The PolyBlepOscillator uses a leaky integrator for triangle, but for discontinuity computation we use the analytical formula.

### Alternatives Considered

1. **Use PolyBlepOscillator::process() to evaluate**: Would include PolyBLEP corrections that distort the discontinuity measurement. Also would advance the oscillator state. Rejected.
2. **Store previous output value**: Track the last output sample and use it as `valueBefore`. This works for hard sync but does not work for phase advance (where we need to evaluate at an arbitrary phase). Rejected for generality.

---

## 8. Waveform Derivative Evaluation for MinBLAMP

### Decision: Inline derivative evaluation function for reverse sync

### Rationale

For minBLAMP correction amplitude in reverse sync mode, we need the waveform's derivative at the reversal point:

```cpp
float evaluateWaveformDerivative(OscWaveform wf, float phase, float pulseWidth) {
    switch (wf) {
        case Sine:      return 2*pi*cos(2*pi*phase);
        case Sawtooth:  return 2.0;  // Constant slope
        case Square:    return 0.0;  // Flat segments (derivative = 0 except at edges)
        case Pulse:     return 0.0;  // Same as square
        case Triangle:  return phase < 0.5 ? 4.0 : -4.0;
    }
}
```

The derivative discontinuity at reversal = `2 * derivative * slaveIncrement` (because direction flips from +increment to -increment, a total change of 2*increment in the rate of phase change).

For Square and Pulse waveforms, the derivative is zero on the flat segments, so the minBLAMP correction amplitude is zero (no derivative discontinuity on flat segments). However, if the reversal happens to coincide with a step edge, there would be a step discontinuity that requires minBLEP correction instead. This edge case is handled by checking for step discontinuities alongside derivative discontinuities.

---

## 9. Performance Budget Analysis

### Decision: Target 100-150 cycles/sample with the following breakdown

### Budget Breakdown

| Operation | Estimated Cycles |
|-----------|-----------------|
| Master phase advance + wrap detection | ~5 |
| Sub-sample offset computation | ~5 |
| Slave PolyBlepOscillator::process() | ~40-60 |
| Sync event processing (amortized) | ~10-20 |
| MinBLEP residual.consume() | ~5 |
| Waveform evaluation (for discontinuity) | ~10-15 |
| residual.addBlep() (amortized) | ~15-20 |
| Output sanitization | ~5 |
| **Total** | **~95-130** |

The sync event processing (mode-specific logic, waveform evaluation, addBlep/addBlamp) happens at most once per sample (at the master's frequency rate). At lower master frequencies, sync events are less frequent, reducing the amortized cost.

The budget is achievable within the 100-150 cycle target.

---

## 10. MinBlepTable Extension for MinBLAMP

### Decision: Extend MinBlepTable with a precomputed minBLAMP table and add sampleBlamp() + Residual::addBlamp()

### Implementation Plan

1. **In `MinBlepTable::prepare()`**: After computing the minBLEP table, compute the minBLAMP table by numerically integrating the minBLEP residual:
   ```
   blampTable_[0] = table_[0] - 1.0  (first residual value)
   blampTable_[i] = blampTable_[i-1] + (table_[i] - 1.0)
   ```
   Then normalize so the total converges to 0.0 (the ramp correction is transient).

2. **Add `sampleBlamp(float subsampleOffset, size_t index)`**: Same lookup/interpolation logic as `sample()` but reading from `blampTable_`.

3. **Add `Residual::addBlamp(float subsampleOffset, float amplitude)`**: Same ring buffer stamping logic as `addBlep()` but using `sampleBlamp()` instead of `sample()`.

### Storage Impact

Additional table: `length_ * oversamplingFactor_` floats = 16 * 64 = 1024 floats = 4 KB. Negligible.

### Backward Compatibility

Adding new methods does not break existing API. The `addBlep()` and `consume()` methods remain unchanged. The minBLAMP table is simply additional data computed during `prepare()`.

---

## 11. Existing PolyBlepOscillator Integration Patterns

### Key API Details Verified from Source

From `primitives/polyblep_oscillator.h` (read and verified):

| Method | Signature | Notes |
|--------|-----------|-------|
| `prepare` | `void prepare(double sampleRate) noexcept` | Resets all state |
| `reset` | `void reset() noexcept` | Resets phase/state, preserves config |
| `setFrequency` | `void setFrequency(float hz) noexcept` | Clamps to [0, Nyquist) |
| `setWaveform` | `void setWaveform(OscWaveform waveform) noexcept` | Clears integrator for Triangle |
| `setPulseWidth` | `void setPulseWidth(float width) noexcept` | Clamps to [0.01, 0.99] |
| `process` | `[[nodiscard]] float process() noexcept` | Returns one sample |
| `processBlock` | `void processBlock(float* output, size_t numSamples) noexcept` | Block processing |
| `phase` | `[[nodiscard]] double phase() const noexcept` | Current phase [0, 1) |
| `phaseWrapped` | `[[nodiscard]] bool phaseWrapped() const noexcept` | Last process() wrapped |
| `resetPhase` | `void resetPhase(double newPhase = 0.0) noexcept` | Force phase position |

**Critical integration detail**: The SyncOscillator will NOT use PolyBlepOscillator::process() and then manually reset phase. Instead, it will:
1. Use the slave oscillator's `process()` to generate one sample (this handles the slave's natural PolyBLEP corrections).
2. After generating the sample, check if the master wrapped.
3. If the master wrapped, compute the discontinuity and add minBLEP correction to the residual.
4. Set the slave's phase for the NEXT sample using `resetPhase()`.
5. Add `residual.consume()` to the output.

This ordering ensures the slave's natural PolyBLEP and the sync minBLEP are both applied correctly.

**Revised approach**: Actually, the sync reset needs to happen BEFORE the slave generates its sample for correct sub-sample timing. The sequence should be:
1. Advance the master phase. Check for wrap.
2. If master wrapped: compute sub-sample offset, evaluate slave waveform at current phase (valueBefore), compute new slave phase, evaluate at new phase (valueAfter), add minBLEP correction, reset slave phase.
3. Generate slave output via `process()` (this now starts from the reset phase).
4. Add `residual.consume()` to the output.

Wait -- there is a subtlety. The `process()` method of PolyBlepOscillator reads the current phase, generates a sample, and THEN advances the phase. So:
1. Advance master phase. Check for wrap.
2. If master wrapped: compute everything, reset slave phase via `resetPhase()`.
3. Call slave `process()` which reads the reset phase, generates the sample, and advances.
4. Add `residual.consume()`.

This is the correct ordering.

---

## 12. Output Ordering: Process Pipeline

### Final Decision: Per-sample processing pipeline

```
1. Compute masterIncrement from masterFrequency / sampleRate
2. masterPhase += masterIncrement
3. masterWrapped = (masterPhase >= 1.0)
4. if masterWrapped:
     a. masterPhase -= 1.0
     b. subsampleOffset = masterPhase / masterIncrement
     c. [mode-specific sync processing]  // sets slave phase, adds BLEP/BLAMP
5. slaveSample = slave_.process()  // reads current phase, generates sample, advances
6. output = slaveSample + residual_.consume()
7. sanitize(output)
```

---

## Sources

- [Eli Brandt, "Hard Sync Without Aliasing", ICMC 2001](http://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf)
- [KVR Audio: Triangle OSC with BLAMP, minBLEP](https://www.kvraudio.com/forum/viewtopic.php?t=287993)
- [KVR Audio: About BLEP/minBLEP, polyBLEP and bandlimited ramps](https://www.kvraudio.com/forum/viewtopic.php?t=248390)
- [ExperimentalScene: MinBLEPs article](https://www.experimentalscene.com/articles/minbleps.php)
- [RS-MET GitHub: BLITs/BLEPs/BLAMPs implementation](https://github.com/RobinSchmidt/RS-MET/issues/273)
- [Esqueda, Valimaki, Bilbao: "Rounding Corners with BLAMP", DAFx-16, 2016](https://www.dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf)
- [Metafunction: All About Digital Oscillators Part 2](https://www.metafunction.co.uk/post/all-about-digital-oscillators-part-2-blits-bleps)
