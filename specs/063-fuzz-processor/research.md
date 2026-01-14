# Research: FuzzProcessor

**Feature**: 063-fuzz-processor | **Date**: 2026-01-14

## Research Questions Resolved

### RQ-001: Germanium Sag Implementation

**Question**: How should the "saggy" characteristic of germanium transistors be implemented?

**Decision**: Envelope follower with 1ms attack, 100ms release that modulates the clipping threshold.

**Rationale**: Real germanium fuzz pedals exhibit "sag" because the transistor's gain is dependent on operating conditions that change with signal level. When the input signal is loud, the power supply voltage drops slightly (capacitor discharge), which reduces the transistor's gain ceiling. This creates a compressed, "saggy" feel where loud signals get more compression.

The envelope follower approach models this by:
1. Tracking the input signal amplitude with asymmetric attack/release
2. Using the envelope value to reduce the clipping threshold for loud signals
3. 1ms attack captures transients quickly
4. 100ms release creates the characteristic "bloom" as gain recovers

**Alternatives Considered**:
1. RMS tracking - rejected because it smooths too much and loses transient response
2. Peak detection - rejected because it's too aggressive for the musical sag effect
3. Physical modeling of power supply - rejected as too complex for Layer 2 processor

**Implementation Notes**:
- Cannot use EnvelopeFollower class (Layer 2 dependency violation)
- Implement inline one-pole envelope tracking
- Attack: `coeff = exp(-1 / (0.001 * sampleRate))`
- Release: `coeff = exp(-1 / (0.100 * sampleRate))`

---

### RQ-002: Octave Fuzz Implementation

**Question**: How should the octave-up effect be implemented?

**Decision**: Self-modulation via `input * abs(input)` before the main fuzz stage.

**Rationale**: Classic octave fuzz pedals (like the Octavia) achieve the octave-up effect through frequency doubling. The self-modulation formula `x * |x|` creates this mathematically:

- For input `sin(wt)`: output = `sin(wt) * |sin(wt)|`
- This produces `sin(wt) * (1 - cos(2wt))/2` after simplification
- The `cos(2wt)` term is at double the frequency (one octave up)

**Alternatives Considered**:
1. Ring modulator with sine oscillator - rejected because it requires a tracking oscillator and phase-locked loop for proper pitch tracking
2. Full-wave rectification - rejected because it produces only octave-up without the fundamental
3. Half-wave rectification - rejected because asymmetric and produces harsh artifacts

**Implementation Notes**:
- Apply before waveshaping stage (FR-053)
- Simple one-line implementation: `input = input * std::abs(input)`
- Results in 2nd harmonic generation (octave up)
- Combine with fuzz for classic "Octavia" sound

---

### RQ-003: Type Transition Strategy

**Question**: How should switching between Germanium and Silicon types be handled?

**Decision**: 5ms equal-power crossfade between type outputs.

**Rationale**: Instant switching would cause audible clicks due to:
1. Different DC offsets from asymmetric vs symmetric saturation
2. Different harmonic content creating phase discontinuities
3. Different gain/compression characteristics

The 5ms crossfade duration is chosen because:
- Long enough to prevent audible clicks (SC-004)
- Short enough to feel instantaneous to the user
- Matches the parameter smoothing time for consistency

Equal-power crossfade (using sin/cos curves) maintains constant perceived loudness during the transition, unlike linear crossfades which dip at the midpoint.

**Alternatives Considered**:
1. Instant switch - rejected because it causes audible clicks
2. Linear crossfade - rejected because it causes a loudness dip at the midpoint
3. Morphing internal parameters - rejected as too complex and not musically meaningful

**Implementation Notes**:
- Use `equalPowerGains()` from crossfade_utils.h
- Use `crossfadeIncrement()` for sample-accurate timing
- Process both types in parallel during crossfade (2x CPU briefly)
- Store previous type to know which outputs to blend

---

### RQ-004: Envelope Follower Timing

**Question**: What attack/release times should be used for Germanium sag?

**Decision**: 1ms attack, 100ms release.

**Rationale**: These values model the electrical behavior of germanium transistor circuits:

- **1ms attack**: Fast enough to respond to transients but not so fast that it causes "pumping". This captures the initial impact when you strike a note hard.

- **100ms release**: Slow enough to create the characteristic "bloom" of germanium fuzz where the sustain gradually increases after the initial attack. This models the recovery time of the power supply capacitors.

The asymmetry (100:1 ratio) is critical - if attack and release were equal, the effect would sound like a simple compressor rather than the distinctive germanium character.

**Alternatives Considered**:
1. 0.1ms attack, 50ms release - rejected because attack too fast causes clicking
2. 5ms attack, 500ms release - rejected because attack too slow loses transient character
3. User-configurable timing - rejected as out of scope for this implementation

---

### RQ-005: Bias Gating Implementation

**Question**: How should the bias parameter create the "dying battery" gating effect?

**Decision**: Bias controls a gate threshold below which signals are attenuated.

**Rationale**: In real Fuzz Face pedals, low battery voltage (dying battery) causes the transistor to operate in a suboptimal bias point. This creates a threshold effect where:
- Signals below the threshold are cut off entirely (gating)
- Signals above the threshold pass through with increased saturation
- The result is a "sputtery" or "gated" character

**Implementation**:
```cpp
// bias=0 -> aggressive gating (dying battery)
// bias=1 -> no gating (fresh battery)
float gateThreshold = (1.0f - bias_) * 0.2f;  // Max threshold ~0.2 (-14dBFS)
float gatingFactor = (gateThreshold > 0.001f)
    ? std::min(1.0f, std::abs(input) / gateThreshold)
    : 1.0f;
output *= gatingFactor;
```

**Verification**: Per SC-009, bias=0.2 should attenuate signals below -20dBFS by at least 6dB compared to bias=1.0.

---

## Component Selection

### Waveshaper Selection

| Type | Component | Harmonics | Character |
|------|-----------|-----------|-----------|
| Germanium | Waveshaper (WaveshapeType::Tube) | Even + Odd | Warm, asymmetric, soft knee |
| Silicon | Waveshaper (WaveshapeType::Tanh) | Odd only | Bright, symmetric, harder knee |

**Note**: Per FR-018 and FR-021, Germanium uses `Asymmetric::tube()` (via Waveshaper) and Silicon uses `Sigmoid::tanh()` (via Waveshaper with drive scaling).

### Filter Selection

| Purpose | Component | Configuration |
|---------|-----------|---------------|
| Tone | Biquad (FilterType::Lowpass) | Q=0.7071 (Butterworth), freq=400-8000Hz |
| DC Block | DCBlocker | cutoff=10Hz |

### Smoother Selection

| Parameter | Component | Time | Notes |
|-----------|-----------|------|-------|
| Fuzz | OnePoleSmoother | 5ms | Gain smoothing |
| Volume | OnePoleSmoother | 5ms | Output gain smoothing |
| Bias | OnePoleSmoother | 5ms | Gating threshold smoothing |
| Tone | OnePoleSmoother | 5ms | Filter cutoff smoothing |

---

## Algorithm Details

### Germanium Signal Path

```
1. input = input * |input|  (if octave-up enabled)
2. sagEnvelope = envelope_follow(|input|, 1ms attack, 100ms release)
3. saggedInput = input * (1.0 - sagEnvelope * 0.3)  // Reduce gain for loud signals
4. biasedInput = saggedInput + bias_offset
5. saturated = Asymmetric::tube(biasedInput * fuzz_drive)
6. gated = apply_bias_gating(saturated)
7. dcBlocked = dcBlocker.process(gated)
8. toned = toneFilter.process(dcBlocked)
9. output = toned * volume_gain
```

### Silicon Signal Path

```
1. input = input * |input|  (if octave-up enabled)
2. biasedInput = input + bias_offset
3. saturated = Sigmoid::tanh(biasedInput * fuzz_drive * 2.0)  // Higher drive for harder clip
4. gated = apply_bias_gating(saturated)
5. dcBlocked = dcBlocker.process(gated)
6. toned = toneFilter.process(dcBlocked)
7. output = toned * volume_gain
```

### Tone Frequency Mapping

Per FR-027 and FR-028:
```cpp
float cutoffHz = kToneMinHz + tone_ * (kToneMaxHz - kToneMinHz);
// tone=0.0 -> 400Hz (dark)
// tone=0.5 -> 4200Hz (neutral)
// tone=1.0 -> 8000Hz (bright)
```

---

## Performance Considerations

### CPU Budget Analysis

Per SC-005: < 0.5% CPU @ 44.1kHz/2.5GHz baseline (512 samples)

| Operation | Est. Cycles/Sample | Notes |
|-----------|-------------------|-------|
| Envelope follower | 5 | Simple one-pole |
| Waveshaper | 15 | Tube/Tanh (via Waveshaper) |
| DC blocker | 5 | First-order |
| Tone filter | 10 | Biquad |
| Smoothers (4x) | 20 | 4 OnePoleSmoothers |
| Volume gain | 2 | Multiply |
| **Total** | ~57 | |

At 2.5GHz and 44.1kHz:
- Cycles per sample available: 2,500,000,000 / 44,100 = ~56,700
- 0.5% budget: ~284 cycles/sample
- Estimated usage: ~57 cycles/sample = ~0.1% CPU

**Conclusion**: Well within budget. Even during type crossfade (2x processing), still under 0.3%.

### Memory Requirements

| Component | Size | Notes |
|-----------|------|-------|
| Smoothers (4x) | ~80 bytes | 5 floats each |
| Waveshapers (2x) | ~16 bytes | Type + drive + asymmetry |
| Biquad | ~32 bytes | Coefficients + state |
| DCBlocker | ~24 bytes | State + config |
| Parameters | ~20 bytes | 4 floats + bool |
| Crossfade state | ~16 bytes | Position, increment, flags |
| **Total** | ~188 bytes | |

---

## Test Strategy

### Harmonic Content Verification (SC-001 to SC-003)

Use FFT to analyze harmonic content:
1. Feed 440Hz sine wave at -6dBFS
2. Capture 4096 samples of output
3. Perform FFT
4. Measure harmonic amplitudes at 880Hz (2nd), 1320Hz (3rd), 1760Hz (4th), 2200Hz (5th)

**Germanium Criteria** (SC-002):
- 2nd harmonic > -40dBFS (measurable even content)
- 4th harmonic present

**Silicon Criteria** (SC-003):
- 3rd harmonic > 5th harmonic > 7th harmonic
- 2nd and 4th harmonics < -60dBFS (minimal even content)

### Gating Behavior (SC-009)

1. Set bias=0.2, fuzz=0.5
2. Feed -20dBFS sine wave
3. Measure output level
4. Set bias=1.0
5. Feed same -20dBFS sine wave
6. Measure output level
7. Verify difference > 6dB

### Tone Response (SC-010)

1. Feed white noise
2. Measure output spectrum at tone=0.0 and tone=1.0
3. Verify > 12dB difference at 4kHz
