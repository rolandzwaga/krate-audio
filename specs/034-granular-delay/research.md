# Granular Delay Research

**Feature Branch**: `034-granular-delay`
**Research Date**: 2025-12-27
**Status**: COMPLETE

## Executive Summary

This document compiles research findings on granular synthesis techniques for implementing a high-quality Granular Delay effect as the final Layer 4 feature. The research covers architecture patterns, efficient algorithms, and best practices from academic papers, commercial implementations, and expert practitioners.

## Key Sources

### Academic & Technical Papers

1. **Ross Bencina - "Implementing Real-Time Granular Synthesis" (2001)**
   - Foundational architecture paper describing Scheduler, SequenceStrategy, Grain components
   - Covers three envelope algorithms: Parabolic, Trapezoidal, Raised Cosine Bell
   - Describes both synchronous and asynchronous grain scheduling
   - Source: [Semantic Scholar](https://www.semanticscholar.org/paper/Implementing-Real-Time-Granular-Synthesis-Bencina/9f6016d992daa927ac8eac56b2427ffeef0060b6)

2. **Curtis Roads - "Microsound" (MIT Press, 2001)**
   - Definitive academic reference on granular synthesis
   - Coined terminology: grain clouds, density, asynchronous granular synthesis
   - Historical context from Gabor (1947) through Xenakis (1971) to Roads (1978)

3. **Barry Truax - Real-Time Granular Synthesis (1986)**
   - First real-time implementation using DMX-1000 Signal Processing Computer
   - Source: [SFU Truax Gran](https://www.sfu.ca/~truax/gran.html)

4. **DSP Concepts - Granular Synthesis Module Documentation**
   - Commercial DSP implementation reference
   - Source: [DSP Concepts](https://documentation.dspconcepts.com/awe-designer/8.D.2.6/granular-synthesis-module)

### Commercial Product References

- **Hologram Microcosm**: Granular looper/glitch pedal with freeze
- **Electro-Harmonix Freeze**: Infinite sustain via granular/spectral freeze
- **Intellijel Rainmaker**: 16-tap delay with per-tap granular pitch shifter
- **SaltyGrain**: VST plugin with delay-line granular and freeze
- **Red Panda Particle**: Granular delay on constrained DSP (FV-1)

---

## 1. Architecture Patterns

### 1.1 Bencina's Modular Architecture

The canonical architecture from Ross Bencina's paper:

```
┌─────────────────────────────────────────────────────────────┐
│                        SCHEDULER                             │
│  - Maintains grain pool (active/free lists)                 │
│  - Triggers grain creation based on SequenceStrategy        │
│  - Manages grain lifecycle (create, process, recycle)       │
├─────────────────────────────────────────────────────────────┤
│                    SEQUENCE STRATEGY                         │
│  - Synchronous: regular intervals (pitch period)            │
│  - Asynchronous: stochastic interonset times (density)      │
├─────────────────────────────────────────────────────────────┤
│                          GRAIN                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │  Envelope   │  │   Source    │  │ Parameters  │         │
│  │ (amplitude) │  │ (audio tap) │  │ (pitch,pan) │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Three Types of Granular Synthesis

1. **Tapped Delay Line Granular** (our target)
   - Uses circular buffer storing real-time input
   - Each grain reads from buffer at different delay time and playback rate
   - Best for "effects" processing of live audio
   - Higher implementation complexity due to variable-rate delay taps

2. **Stored Sample Granular**
   - Reads from pre-loaded wavetable/sample
   - Used for sample manipulation, not real-time effects

3. **Synthetic Grain Granular**
   - Generates grains via oscillators/FM/synthesis
   - Purely synthetic textures, no input audio

**Decision**: Implement Tapped Delay Line Granular for real-time delay effect.

---

## 2. Grain Envelope Algorithms

### 2.1 Efficient Envelope Types

From Bencina's analysis, three envelope algorithms offer similar efficiency:

| Envelope | Shape | Characteristics | Best For |
|----------|-------|-----------------|----------|
| Parabolic | y = 1 - (2t-1)² | Smooth, natural decay | General purpose |
| Trapezoidal | Attack-Sustain-Decay | Preserves content better | Speech, transients |
| Raised Cosine Bell | y = 0.5(1 - cos(2πt)) | Very smooth, Hann window | Seamless overlap |

### 2.2 Computation Methods

**Pre-computed Lookup Table** (recommended):
- Store envelope shape in small buffer (512-2048 samples)
- Index into table based on grain phase
- Interpolate for smooth values between samples
- One-time computation cost at prepare()

**Incremental Computation**:
- Parabolic: `y[n] = y[n-1] + delta; delta += delta2`
- Requires maintaining state per grain
- Slightly more complex but avoids table lookup

**Decision**: Use lookup table approach for simplicity and cache efficiency.

### 2.3 Window Function Considerations

From katjaas pitch shifting research:

| Window | Overlap Behavior | Artifacts |
|--------|------------------|-----------|
| Hann (Raised Cosine) | Amplitude dips at max overlap | Less comb filtering |
| Sine (Half-cosine) | Amplitude peaks at max overlap | Compensates phase loss |

**Insight**: Sine windows are better for granular pitch shifting because they compensate for the statistical energy loss caused by phase cancellation between overlapping grains.

---

## 3. Grain Scheduling

### 3.1 Synchronous Scheduling

- Grains triggered at regular intervals
- Interonset time = 1/frequency (for pitched output)
- Creates periodic, tonal textures
- Lower CPU (predictable grain count)

```cpp
float interonsetTime = 1.0f / frequencyHz;  // seconds
int interonsetSamples = static_cast<int>(interonsetTime * sampleRate);
```

### 3.2 Asynchronous (Stochastic) Scheduling

- Grains triggered at random intervals based on density
- More organic, textural sound
- Can create "cloud" textures

**Interonset Time Calculation (from Bencina)**:
```cpp
// Method 1: Exponential distribution (natural clustering)
float interonset = -log(random01()) / density;

// Method 2: Uniform distribution with jitter
float baseInteronset = 1.0f / density;
float jitter = (random01() - 0.5f) * baseInteronset * jitterAmount;
float interonset = baseInteronset + jitter;
```

### 3.3 Density Parameter

- Measured in grains per second
- Higher density = more overlap = thicker texture = more CPU
- Typical range: 1-100 grains/second
- At very high density (>50 Hz), grains fuse into continuous texture

**CPU Relationship**:
```
Active Grains ≈ Density × Grain Duration
```
Example: 20 grains/sec × 50ms = 1.0 simultaneous grains on average

---

## 4. Pitch Shifting in Granular Delay

### 4.1 Two-Head Delay Approach (Tempophon Style)

From katjaas research - the classic granular pitch shifter:

```
┌─────────────────────────────────────────────────────────────┐
│                    DELAY BUFFER                              │
│  ←──────────────────────────────────────────────────────    │
│     │        │                      │                        │
│     │   Head A                 Head B                        │
│     │   (reading)              (reading)                     │
│     └────┬───────────────────────┬──────────────────────    │
│          ↓                       ↓                          │
│     ┌────────┐              ┌────────┐                      │
│     │ Window │              │ Window │                      │
│     │ (fade) │              │ (fade) │                      │
│     └────┬───┘              └────┬───┘                      │
│          └──────────┬───────────┘                           │
│                     ↓                                        │
│               [ Mix Output ]                                 │
└─────────────────────────────────────────────────────────────┘
```

**Algorithm**:
1. Two read heads positioned at alternating phases
2. Each head reads at variable speed (pitch shift ratio)
3. Heads crossfade at boundaries to avoid clicks
4. Offset between heads = grain size / 2

**Pitch Shift via Playback Rate**:
```cpp
// pitch = 2.0 = octave up (reads buffer twice as fast)
// pitch = 0.5 = octave down (reads buffer half as fast)
readPosition += pitch;  // fractional increment
sample = interpolate(buffer, readPosition);
```

### 4.2 Interpolation for Fractional Positions

| Method | Quality | CPU Cost |
|--------|---------|----------|
| Nearest | Poor (aliasing) | Minimal |
| Linear | Acceptable | Low |
| Cubic (Hermite) | Good | Medium |
| Lagrange/Sinc | Excellent | High |

**Decision**: Use linear interpolation as default, with option for cubic in high-quality mode.

### 4.3 Avoiding Pitch Shifting Artifacts

1. **Grain size matters**: Larger grains = smoother pitch shift, but more latency
2. **Crossfade overlap**: 50% overlap with raised cosine or sine window
3. **Minimum grain size**: ~20ms to preserve low frequencies
4. **Maximum grain size**: ~500ms before obvious looping

---

## 5. Delay Line Integration

### 5.1 Circular Buffer Design

```cpp
class GranularDelayLine {
    std::vector<float> buffer_;
    size_t writePosition_ = 0;
    size_t bufferSize_;  // e.g., 10 seconds at 48kHz = 480000 samples

    void write(float sample) {
        buffer_[writePosition_] = sample;
        writePosition_ = (writePosition_ + 1) % bufferSize_;
    }

    float readInterpolated(float delayInSamples) const {
        float readPos = static_cast<float>(writePosition_) - delayInSamples;
        if (readPos < 0) readPos += bufferSize_;
        // Linear interpolation between samples
        size_t idx0 = static_cast<size_t>(readPos);
        float frac = readPos - idx0;
        return buffer_[idx0] * (1.0f - frac) +
               buffer_[(idx0 + 1) % bufferSize_] * frac;
    }
};
```

### 5.2 Per-Grain Delay Tap

Each grain needs:
- **Position**: Where in the delay buffer to read (0 to max delay)
- **Playback rate**: Speed to traverse buffer (1.0 = normal, 0.5 = half speed)
- **Direction**: Forward or reverse

```cpp
struct GrainState {
    float readPosition;     // Current position in delay buffer
    float playbackRate;     // Samples to advance per output sample
    float envelopePhase;    // 0.0 to 1.0 progress through envelope
    float amplitude;        // Grain volume
    float pan;              // Stereo position
    bool active;            // Is grain currently sounding
};
```

### 5.3 Position Randomization

**Spray/Scatter Control**:
```cpp
// Position in delay buffer with randomization
float basePosition = delayTime * sampleRate;  // User-set delay time
float spray = sprayAmount * maxDelayInSamples;  // Randomization range
float grainPosition = basePosition + (random01() - 0.5f) * spray;
```

---

## 6. Freeze/Hold Implementation

### 6.1 Granular Freeze vs Spectral Freeze

| Aspect | Granular Freeze | Spectral (FFT) Freeze |
|--------|-----------------|----------------------|
| Texture | Preserves "surface texture" | Smooth, glassy |
| Movement | Subtle grain variations | Static spectrum |
| Best for | Organic, evolving holds | Drone, pad sustain |

### 6.2 Implementation

**Freeze = Stop writing, keep reading**:
```cpp
void process(float input) {
    if (!frozen_) {
        delayLine_.write(input);
    }
    // Grains continue reading from frozen buffer
    for (auto& grain : activeGrains_) {
        output += processGrain(grain);
    }
}
```

**Crossfade on freeze toggle**:
- When freezing: Fade out live input over ~50-100ms
- When unfreezing: Crossfade from frozen to live over same duration
- Prevents clicks and allows musical transitions

---

## 7. Parameter Design

### 7.1 Core Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Delay Time | 0-2000ms | Base position in delay buffer |
| Grain Size | 10-500ms | Duration of each grain |
| Density | 1-100 Hz | Grains per second |
| Pitch | -24 to +24 semitones | Playback rate shift |
| Position Spray | 0-100% | Randomize grain start position |
| Pitch Spray | 0-100% | Randomize grain pitch |
| Reverse Probability | 0-100% | Chance of reverse playback |
| Feedback | 0-120% | Granulated output back to input |
| Dry/Wet Mix | 0-100% | Blend with original |
| Freeze | On/Off | Hold buffer contents |

### 7.2 Advanced Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Density Mode | Sync/Async | Regular or stochastic timing |
| Envelope Type | Hann/Sine/Trap | Grain window shape |
| Pan Spray | 0-100% | Randomize stereo position |
| Size Spray | 0-100% | Randomize grain duration |
| Filter Cutoff | 20Hz-20kHz | Per-grain low/high pass |
| Filter Type | LP/HP/BP | Filter mode |

---

## 8. CPU Optimization Strategies

### 8.1 Grain Pool / Voice Stealing

**Pre-allocate grain pool**:
```cpp
static constexpr size_t kMaxGrains = 64;
std::array<GrainState, kMaxGrains> grainPool_;
std::vector<GrainState*> activeGrains_;
std::vector<GrainState*> freeGrains_;
```

**Voice stealing when at capacity**:
1. Steal oldest grain (longest elapsed time)
2. Steal quietest grain (lowest envelope amplitude)
3. Steal grain with shortest remaining time

### 8.2 Lookup Tables

Pre-compute at prepare():
- Envelope shapes (512-2048 samples per shape)
- Pitch ratios for semitone values
- Pan law coefficients

### 8.3 SIMD Considerations

- Process 4 grains simultaneously with SSE
- Batch envelope/interpolation calculations
- Contiguous memory layout for grain state

### 8.4 CPU Budget

From Layer 4 architecture:
- Target: <3% CPU at 44.1kHz stereo
- Similar budget to SpectralDelay (achieved 0.93%)
- 64 max grains should be achievable

---

## 9. Comparison to Existing Components

### 9.1 Reusable from Codebase

| Component | Location | Relevance |
|-----------|----------|-----------|
| DelayLine | primitives/delay_line.h | Base buffer - may need extension for multi-tap |
| OnePoleSmoother | primitives/smoother.h | Parameter smoothing |
| MultimodeFilter | processors/multimode_filter.h | Per-grain or global filtering |
| FlexibleFeedbackNetwork | processors/flexible_feedback_network.h | Feedback path with processing |

### 9.2 New Components Needed

| Component | Layer | Description |
|-----------|-------|-------------|
| GrainPool | 1 | Pre-allocated grain state management |
| GrainScheduler | 2 | Sync/Async timing with density control |
| GrainProcessor | 2 | Individual grain with envelope, pitch, pan |
| GranularEngine | 3 | Combines pool, scheduler, delay buffer |
| GranularDelay | 4 | User-facing feature with full parameter set |

---

## 10. Implementation Recommendations

### 10.1 Phased Approach

**Phase 1: Foundation**
- GrainPool primitive (fixed-size grain state array)
- Envelope lookup tables (Hann, Sine, Trapezoid)
- Basic interpolated delay buffer read

**Phase 2: Core Engine**
- GrainScheduler with density control
- GrainProcessor with pitch shift
- Basic mono granular processing

**Phase 3: Full Feature**
- Stereo processing with pan spray
- Freeze mode with crossfade
- Feedback path integration
- Reverse grain probability

**Phase 4: Polish**
- Per-grain filtering (optional)
- Tempo sync for delay time
- SIMD optimization if needed
- CPU profiling and tuning

### 10.2 Testing Strategy

1. **Unit tests**: Grain lifecycle, envelope accuracy, interpolation
2. **Integration tests**: Multi-grain mixing, freeze transitions
3. **Audio tests**: Pitch accuracy, no clicks, CPU budget
4. **A/B comparison**: Compare to reference plugins (SaltyGrain, Microcosm)

### 10.3 Risk Areas

| Risk | Mitigation |
|------|------------|
| CPU spikes at high density | Voice stealing, density limiting |
| Clicks on grain boundaries | Proper envelope overlap (50%+) |
| Pitch artifacts | Adequate grain size, good interpolation |
| Freeze clicks | Crossfade transitions (50-100ms) |

---

## 11. References

### Papers & Books
- Bencina, R. (2001). "Implementing Real-Time Granular Synthesis"
- Roads, C. (2001). *Microsound*. MIT Press.
- Truax, B. (1986). "Real-time granular synthesis with the DMX-1000"

### Web Resources
- [Sound on Sound: Understanding Granular Delay](https://www.soundonsound.com/techniques/understanding-granular-delay)
- [DSP Labs: Granular Synthesis Implementation](https://lcav.gitbook.io/dsp-labs/granular-synthesis/implementation)
- [Katjaas: Pitch Shifting](https://www.katjaas.nl/pitchshift/pitchshift.html)
- [GranularSynthesis.com: Envelope](https://www.granularsynthesis.com/hthesis/envelope.html)
- [Red Panda: Particle History](https://www.redpandalab.com/blog/particle-history-part-1-creating-a-granular-delay/)

### Commercial References
- Hologram Microcosm (granular looper pedal)
- Intellijel Rainmaker (16-tap granular delay)
- SaltyGrain (VST granular plugin)
- Electro-Harmonix Freeze (infinite sustain)
