# Research: Allpass-Saturator Network

**Feature**: 109-allpass-saturator-network | **Date**: 2026-01-26

## Research Questions Resolved

### Q1: Allpass Filter Implementation

**Decision**: Use existing `Biquad` class with `FilterType::Allpass` mode

**Rationale**:
- Biquad already implements allpass coefficients correctly (Bristow-Johnson formulas)
- Well-tested in existing codebase
- Supports configurable frequency and Q
- Q parameter controls bandwidth of phase shift, higher Q = sharper resonance

**Alternatives Considered**:
- Custom Schroeder allpass (like AllpassStage in DiffusionNetwork): Different topology, uses delay line
- First-order allpass: Only 90-degree phase shift, insufficient for resonance

**Implementation Note**:
The Biquad allpass produces 180-degree phase shift at the cutoff frequency with flat magnitude response. When combined with saturation in a feedback loop, this creates strong resonance at the cutoff frequency.

### Q2: Saturation in Feedback Loop

**Decision**: Use existing `Waveshaper` primitive with configurable `WaveshapeType`

**Rationale**:
- All 9 waveshape types already implemented
- No oversampling needed at Layer 1 (handled by Waveshaper design)
- Stateless processing (can be called anywhere in signal chain)
- Drive parameter scales saturation intensity

**Signal Flow**:
```
input -> [+] -> [allpass] -> [waveshaper] -> [soft clip] -> output
          ^                                      |
          |______ feedback * gain _______________|
```

### Q3: Network Topologies

**Decision**: Four topologies as specified

#### SingleAllpass
```
input -> [allpass+saturator] -> output
              |________^
              feedback
```
- Simplest topology, pitched resonance at filter frequency
- Feedback controls sustain, saturation adds harmonics

#### AllpassChain (4 stages)
```
input -> [AP1] -> [AP2] -> [AP3] -> [AP4] -> output
          ^                           |
          |______ feedback ___________|
```
- Frequencies at prime ratios: f, 1.5f, 2.33f, 3.67f
- Creates inharmonic, bell-like timbre
- Single saturation stage at end (before feedback)
- DC blocker in feedback path

#### KarplusStrong
```
input -> [delay] -> [saturator] -> [1-pole LP] -> output
              ^                          |
              |______ feedback __________|
```
- Classic plucked string synthesis
- Delay sets pitch (period = sampleRate / frequency)
- 1-pole lowpass in feedback simulates string damping
- Cutoff derived from decay parameter

#### FeedbackMatrix (4x4 Householder)
```
       [saturator1]     [saturator2]     [saturator3]     [saturator4]
            |                |                |                |
input -> [allpass1]      [allpass2]      [allpass3]      [allpass4] -> output (sum)
            |                |                |                |
            +------ [Householder Matrix] ------+
            |                |                |                |
            v                v                v                v
```
- 4 parallel allpass+saturator channels
- Householder matrix cross-feeds all channels
- Energy-preserving (unitary matrix)
- Creates dense, evolving textures

### Q4: Householder Matrix Construction

**Decision**: Use normalized Householder reflection matrix

**Formula**: H = I - 2vv^T where v = [1,1,1,1]^T normalized

For N=4:
```cpp
// v = [0.5, 0.5, 0.5, 0.5] (normalized [1,1,1,1])
// H[i][j] = (i == j ? 1 : 0) - 2 * v[i] * v[j]
// H[i][j] = (i == j ? -0.5 : 0.5)

constexpr float kHouseholder[4][4] = {
    {-0.5f,  0.5f,  0.5f,  0.5f},
    { 0.5f, -0.5f,  0.5f,  0.5f},
    { 0.5f,  0.5f, -0.5f,  0.5f},
    { 0.5f,  0.5f,  0.5f, -0.5f}
};
```

**Properties**:
- Orthogonal: H^T * H = I
- Energy-preserving: ||H*x|| = ||x||
- Determinant = -1 (reflection)
- Maximally diffusive: each output depends equally on all inputs

### Q5: Parameter Smoothing

**Decision**: 10ms time constant using `OnePoleSmoother`

**Implementation**:
```cpp
// In prepare():
frequencySmoother_.configure(10.0f, sampleRate);  // 10ms
feedbackSmoother_.configure(10.0f, sampleRate);
driveSmoother_.configure(10.0f, sampleRate);

// In process():
float freq = frequencySmoother_.process();
float fb = feedbackSmoother_.process();
float drv = driveSmoother_.process();
```

**Rationale**:
- 10ms provides inaudible transitions for all parameters
- OnePoleSmoother is efficient (single multiply-add per sample)
- Time to 99% target matches spec requirement (SC-004)

### Q6: Feedback Soft Clipping

**Decision**: `tanh(x * 0.5f) * 2.0f` for +/-2.0 soft limit

**Implementation**:
```cpp
[[nodiscard]] inline float softClipFeedback(float x) noexcept {
    return Sigmoid::tanh(x * 0.5f) * 2.0f;
}
```

**Properties**:
- Output bounded to approximately +/-2.0
- Linear region for |x| < 0.5 (preserves dynamics)
- Gradual compression from 0.5 to 4.0
- Hard limit approached asymptotically

**Why +/-2.0 instead of +/-1.0**:
- Allows some natural headroom for resonance peaks
- Still bounded, prevents runaway feedback
- More musical than hard clipping
- DC blocker handles any offset

### Q7: KarplusStrong Decay to Filter Cutoff

**Decision**: Derive lowpass cutoff from decay time and fundamental frequency

**Formula**:
```cpp
// RT60 decay: output drops to -60dB after decayTime seconds
// Each period, the signal passes through the lowpass once
// For 1-pole LP: attenuation per period = exp(-2*pi*f_c/f_s * period)
//
// After N periods (= decayTime * frequency): amplitude = 10^(-3) = 0.001
// So: exp(-2*pi*f_c/f_s * N) = 0.001
// Solving: f_c = -f_s * ln(0.001) / (2*pi * N)
//              = f_s * 6.908 / (2*pi * decayTime * frequency)

float cutoff = sampleRate_ * 1.1f / (decayTime * frequency_);
cutoff = std::clamp(cutoff, frequency_, sampleRate_ * 0.45f);
```

This creates the characteristic "bright attack, dark decay" of plucked strings.

### Q8: Frequency Clamping

**Decision**: Clamp to [20Hz, sampleRate * 0.45]

**Rationale**:
- 20Hz minimum prevents subsonic instability
- 0.45 * sampleRate prevents aliasing (below Nyquist/2)
- Matches existing Biquad frequency clamping

### Q9: Feedback Clamping

**Decision**: User-facing feedback clamped to [0.0, 0.999]

**Rationale**:
- 0.0 = no feedback (input passes through once)
- 0.999 = maximum sustain without numerical instability
- Soft clipping handles transients that exceed limits
- Self-oscillation occurs around feedback > 0.9 (SC-003)

## Component Summary

| Component | Source | Notes |
|-----------|--------|-------|
| Biquad (allpass) | Layer 1 | Configure with FilterType::Allpass |
| Waveshaper | Layer 1 | Saturation with configurable curve |
| DelayLine | Layer 1 | For KarplusStrong topology |
| DCBlocker | Layer 1 | 10Hz cutoff in feedback path |
| OnePoleSmoother | Layer 1 | 10ms parameter smoothing |
| OnePoleLP | Layer 1 | 6dB/oct for KarplusStrong |
| Sigmoid::tanh | Layer 0 | Soft clipping at +/-2.0 |
| flushDenormal | Layer 0 | Prevent CPU spikes |
| isNaN/isInf | Layer 0 | Input validation |

## Performance Considerations

1. **Per-sample operations**:
   - SingleAllpass: ~20 ops (biquad + waveshaper + soft clip + dc block)
   - AllpassChain: ~80 ops (4x biquad + waveshaper + soft clip + dc block)
   - KarplusStrong: ~25 ops (delay read/write + lowpass + waveshaper + soft clip + dc block)
   - FeedbackMatrix: ~100 ops (4x biquad + 4x waveshaper + matrix multiply + soft clip + dc block)

2. **Memory**:
   - SingleAllpass: ~40 bytes state
   - AllpassChain: ~160 bytes state (4 biquads)
   - KarplusStrong: ~8KB (delay buffer at 44.1kHz, 20Hz minimum)
   - FeedbackMatrix: ~200 bytes state (4 biquads + 4 waveshapers)

3. **CPU budget**: SC-005 requires < 0.5% per instance
   - At 44100Hz, 1 second = 44100 samples
   - 0.5% of 1 core-second = 5ms processing time
   - Budget: ~113ns per sample
   - Estimate: ~50-80ns per sample (within budget)
