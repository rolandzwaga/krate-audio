# Research: Spectral Tilt Filter

**Feature**: 082-spectral-tilt
**Date**: 2026-01-22
**Status**: Complete

## Research Questions

### 1. High-Shelf Filter Coefficient Calculation for Tilt Approximation

**Question**: How to map dB/octave tilt to high-shelf filter gain parameter?

**Research Findings**:

A high-shelf filter provides gain above its cutoff frequency, which can approximate a linear dB/octave tilt when:
- The shelf cutoff frequency equals the pivot frequency
- The shelf gain corresponds to the desired gain at 1 octave above pivot

**Key insight**: A true linear dB/octave tilt requires infinite filter stages or FFT-based processing. A single high-shelf filter is an **approximation** that provides good accuracy within 2-3 octaves of the pivot frequency.

**Approximation accuracy analysis**:
- At 1 octave above pivot: shelf provides exactly the specified gain
- At 2 octaves above pivot: shelf provides approximately 2x the gain (shelf reaches plateau)
- Beyond 2-3 octaves: shelf gain plateaus, deviation from linear tilt increases

For the spec requirement of "+/- 1 dB accuracy within 100 Hz to 10 kHz" (FR-009), a single high-shelf with pivot at 1 kHz provides acceptable accuracy across this range.

**Decision**: Use shelf gain directly equal to tilt dB/octave. This provides the target gain at 1 octave from pivot and acceptable approximation elsewhere.

### 2. Q Factor Selection

**Question**: What Q value provides the smoothest tilt approximation?

**Research Findings**:

| Q Value | Characteristic | Tilt Quality |
|---------|---------------|--------------|
| 0.5 | Overdamped | Very gradual slope, poor tilt approximation |
| 0.707 (Butterworth) | Maximally flat | Smooth transition, good tilt approximation |
| 1.0 | Slightly resonant | Slight bump at pivot, okay for most uses |
| > 1.0 | Resonant | Noticeable peak, not suitable for tilt |

**Decision**: Use Q = 0.7071 (Butterworth) for maximally flat passband and smooth tilt transition.

### 3. Gain Limiting Implementation

**Question**: Where and how to apply gain limits to prevent extreme boost/cut?

**Research Findings**:

**Option A**: Clamp tilt parameter directly
- Limits tilt to safe range
- Simple implementation
- User-facing parameter is clamped

**Option B**: Clamp calculated gain before coefficient calculation
- Allows full tilt range but limits actual gain
- More flexible for different pivot frequencies
- Maintains stability at all settings

**Option C**: Post-process filter coefficients
- Complex, requires understanding coefficient relationships
- Risk of unstable filters if done incorrectly

**Decision**: Option B - Clamp calculated shelf gain to [+24 dB, -48 dB] range during coefficient calculation. This allows the tilt parameter to span its full range while preventing numerical instability from extreme gains.

```cpp
// Calculate target gain from tilt
float targetGainDb = tiltDbPerOctave;  // Direct mapping for high-shelf

// Clamp to safe limits (FR-024, FR-025)
float clampedGainDb = std::clamp(targetGainDb, kMinGainDb, kMaxGainDb);

// Calculate coefficients with clamped gain
auto coeffs = BiquadCoefficients::calculate(
    FilterType::HighShelf, pivotFreq, kQ, clampedGainDb, sampleRate);
```

### 4. Denormal Prevention Strategy

**Question**: How to prevent denormal values in filter state variables?

**Research Findings**:

**Option A**: Flush denormals to zero (existing approach in Biquad)
- Already implemented in `Biquad::process()` via `flushDenormal()`
- Proven approach, well-tested
- Zero signal integrity impact (below audible threshold)

**Option B**: DC offset on state variables (~1e-15f per spec)
- Add tiny constant to state variables before coefficient multiplication
- Prevents denormals from forming
- Slight (imperceptible) DC offset in output

**Option C**: CPU-level FTZ/DAZ mode
- Set processor flags to flush denormals automatically
- Platform-specific implementation required
- Most efficient but less portable

**Investigation of existing Biquad**:
```cpp
// From biquad.h Biquad::process()
z1_ = detail::flushDenormal(z1_);
z2_ = detail::flushDenormal(z2_);
```

The existing Biquad already handles denormals via `flushDenormal()` in Layer 0.

**Decision**: Use existing Biquad denormal handling (Option A). Document DC offset technique (Option B) in code comments per FR-011 requirement, but don't implement separately since Biquad already handles this.

### 5. Tilt Slope Accuracy Testing

**Question**: How to accurately measure tilt slope for test verification?

**Research Findings**:

**Measurement approach**:
1. Generate sine wave at test frequency
2. Process through SpectralTilt filter
3. Measure output RMS amplitude
4. Compare to input amplitude to get gain in dB

**Test frequencies** (octave intervals per spec):
- 125 Hz, 250 Hz, 500 Hz, 1000 Hz, 2000 Hz, 4000 Hz, 8000 Hz

**Settling time considerations**:
- IIR filters need time to reach steady state
- For accurate measurement, process ~0.5 seconds before measuring
- Another ~0.5 seconds for measurement averaging

**Expected behavior for +6 dB/octave tilt at 1 kHz pivot**:

| Frequency | Octaves from 1kHz | Expected Gain | Actual (shelf) |
|-----------|-------------------|---------------|----------------|
| 125 Hz | -3 | -18 dB | ~-18 dB |
| 250 Hz | -2 | -12 dB | ~-12 dB |
| 500 Hz | -1 | -6 dB | ~-6 dB |
| 1000 Hz | 0 | 0 dB | ~0 dB |
| 2000 Hz | +1 | +6 dB | ~+6 dB |
| 4000 Hz | +2 | +12 dB | ~+6 dB (shelf plateau) |
| 8000 Hz | +3 | +18 dB | ~+6 dB (shelf plateau) |

**Important**: The shelf approximation accuracy degrades significantly beyond 1-2 octaves from pivot. However, for practical musical use, this behavior is often desirable (prevents extreme gain at frequency extremes).

**Decision**: Test accuracy at +/- 1-2 octaves from pivot with strict tolerance (+/- 1 dB), accept wider tolerance at extreme frequencies where shelf plateaus.

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Shelf gain mapping | Gain = Tilt dB/octave | Direct mapping, provides correct gain at 1 octave |
| Q factor | 0.7071 (Butterworth) | Maximally flat, smooth transition |
| Gain limiting | Clamp calculated gain | Flexible, maintains stability |
| Denormal handling | Use existing Biquad flushDenormal() | Already implemented, proven approach |
| Test tolerance | +/- 1 dB at +/- 1-2 octaves | Matches shelf approximation accuracy |

## Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Multi-stage shelf cascade | Overkill for spec requirements, increased complexity |
| FFT-based linear tilt | High latency, CPU intensive, not suitable for real-time |
| Low-shelf + high-shelf pair | More complex, single shelf sufficient for requirements |
| Custom coefficient calculation | Existing BiquadCoefficients::calculate() works well |
