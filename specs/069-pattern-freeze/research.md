# Pattern Freeze Mode - Research Findings

**Feature Branch**: `069-pattern-freeze`
**Research Date**: 2026-01-16

## Table of Contents

1. [Euclidean/Bjorklund Algorithm](#1-euclideanbjorklund-algorithm)
2. [Poisson Process for Granular Triggering](#2-poisson-process-for-granular-triggering)
3. [Rolling Circular Buffer](#3-rolling-circular-buffer)
4. [Voice Stealing Strategies](#4-voice-stealing-strategies)
5. [Grain Envelope Shaping](#5-grain-envelope-shaping)
6. [Multi-Voice Pitch Shifting](#6-multi-voice-pitch-shifting)

---

## 1. Euclidean/Bjorklund Algorithm

### Background

The Euclidean rhythm algorithm, popularized by Godfried Toussaint in his 2005 paper "The Euclidean Algorithm Generates Traditional Musical Rhythms," is based on Bjorklund's earlier work on evenly distributing events in a binary sequence. The notation E(k, n) represents distributing k pulses across n steps as evenly as possible.

### The Algorithm

**Toussaint's original insight**: The problem of distributing k pulses among n-k silences is equivalent to Euclid's algorithm for computing the GCD. The Bjorklund algorithm uses this to generate rhythmic patterns that appear in traditional music worldwide.

**Classic patterns**:
- E(3, 8) = `10010010` - Tresillo (Cuban/Afro-Cuban)
- E(5, 8) = `10110110` - Cinquillo
- E(5, 13) = `1001010010100` - Traditional African

### Implementation Approaches

#### 1. Recursive Bjorklund (Complex)

The full recursive algorithm builds patterns by repeatedly combining groups:

```
Input: E(5, 13)
Initial: 1 1 1 1 1 0 0 0 0 0 0 0 0 (5 ones, 8 zeros)
Iteration 1: 10 10 10 10 10 0 0 0 (5 groups of "10", 3 remaining "0")
Iteration 2: 100 100 100 10 10 (3 groups of "100", 2 remaining "10")
Iteration 3: 10010 10010 100 (2 groups of "10010", 1 remaining "100")
Final: 1001010010100
```

**Implementation from GitHub gist** (unohee):
```cpp
void Bjorklund::iter() {
    divisor = lengthOfSeq - pulseAmt;
    remainder.push_back(pulseAmt);
    level = 0;
    do {
        count.push_back(floor(divisor / remainder[level]));
        remainder.push_back(divisor % remainder[level]);
        divisor = remainder[level];
        level++;
    } while (remainder[level] > 1);
    count.push_back(divisor);
    buildSeq(level);
}
```

**Drawbacks**: Requires dynamic allocation, recursive, more complex than necessary for audio.

#### 2. Accumulator Method (Simple, Recommended)

From Paul Batchelor's sndkit, a much simpler stateless approach:

```cpp
bool isHit(int pulses, int steps, int rotation, int position) {
    return (((pulses * (position + rotation)) % steps) + pulses) >= steps;
}
```

**Advantages**:
- No dynamic allocation
- Stateless - can be computed on demand
- Can pre-generate bitmask for O(1) lookup
- Simple to understand and debug

**Pre-generation for efficiency**:
```cpp
uint32_t generatePattern(int pulses, int steps, int rotation) {
    uint32_t pattern = 0;
    for (int i = 0; i < steps; ++i) {
        if (isHit(pulses, steps, rotation, i)) {
            pattern |= (1u << i);
        }
    }
    return pattern;
}
```

### Implementation Decision

**Choice**: Accumulator method with pre-generated bitmask

**Rationale**:
- Real-time safe (no allocation during playback)
- O(1) lookup after pattern generation
- Pattern only regenerates when parameters change
- Max 32 steps fits in uint32_t

### Sources

- [Toussaint's Original Paper (PDF)](https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf)
- [Paul Batchelor's sndkit Implementation](https://paulbatchelor.github.io/sndkit/euclid/)
- [Bjorklund C++ Gist](https://gist.github.com/unohee/d4f32b3222b42de84a5f)
- [SuperCollider Bjorklund Class](https://doc.sccode.org/Classes/Bjorklund.html)

---

## 2. Poisson Process for Granular Triggering

### Background

A Poisson process is a stochastic model where events occur continuously and independently at a constant average rate. The inter-arrival times follow an exponential distribution. This creates irregular, organic timing that sounds more natural than fixed intervals with added jitter.

### Mathematical Foundation

For a Poisson process with rate lambda (events per second):
- Mean inter-arrival time = 1/lambda
- Variance = 1/lambda^2
- Inter-arrival times are exponentially distributed: f(t) = lambda * e^(-lambda*t)

### Generating Exponential Random Variables

**Inverse Transform Sampling**:

If U is uniformly distributed on [0,1], then:
```
T = -ln(U) / lambda
```
generates exponentially distributed T with rate lambda.

### Implementation

```cpp
class PoissonScheduler {
    Xorshift32 rng_;
    double sampleRate_;
    float samplesUntilNext_ = 0.0f;

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
    }

    void setDensity(float grainsPerSecond) {
        // Generate first interval
        samplesUntilNext_ = generateInterval(grainsPerSecond);
    }

    bool process() {
        samplesUntilNext_ -= 1.0f;
        if (samplesUntilNext_ <= 0.0f) {
            // Trigger grain and schedule next
            samplesUntilNext_ += generateInterval(density_);
            return true;
        }
        return false;
    }

private:
    float generateInterval(float lambda) {
        float u = rng_.nextUnipolar();
        u = std::max(u, 1e-7f);  // Avoid log(0)
        float intervalSeconds = -std::log(u) / lambda;
        return intervalSeconds * static_cast<float>(sampleRate_);
    }
};
```

### Comparison with Other Methods

| Method | Regularity | Clumping | Implementation |
|--------|------------|----------|----------------|
| Fixed intervals | Very regular | None | Simple counter |
| Fixed + jitter | Semi-regular | Slight | Counter + random |
| Poisson | Irregular | Natural | Exponential distribution |

### Implementation Decision

**Choice**: Poisson process using exponential distribution

**Rationale**:
- Spec explicitly requires Poisson (FR-039a)
- Creates organic, musical grain clouds
- Simple to implement with existing Xorshift32
- Memory-less property means no state complexity

### Sources

- [C++ std::exponential_distribution](https://en.cppreference.com/w/cpp/numeric/random/exponential_distribution.html)
- [Granular Synthesis Wikipedia](https://en.wikipedia.org/wiki/Granular_synthesis)
- [Csound exprand opcode](https://csound.com/docs/manual/exprand.html)

---

## 3. Rolling Circular Buffer

### Background

A rolling circular buffer continuously records audio, allowing freeze effects to capture whatever was playing at any moment. Unlike a standard delay line that requires input to be "playing through," a rolling buffer always contains recent audio.

### Requirements Analysis

From the spec:
- FR-001: Continuously recording regardless of freeze state
- FR-002: At least 5 seconds of audio
- FR-003: Stereo recording
- FR-004: Continue recording even when frozen
- FR-005: Report fill level

Memory requirement: 5s * 2ch * 4bytes * 192kHz = 7.68 MB max

### Existing DelayLine Analysis

The codebase already has `DelayLine` which provides:
- Power-of-2 buffer sizing (efficient wraparound with bitmask)
- Linear interpolation for fractional reads
- 10 second max capacity at any sample rate
- Real-time safe write/read operations

### Implementation Decision

**Choice**: Wrap two DelayLine instances in a RollingCaptureBuffer class

**Rationale**:
- No need for lock-free complexity (single audio thread)
- DelayLine already handles all buffer management
- Just need to add fill-level tracking and stereo API
- Avoids code duplication

**Design**:
```cpp
class RollingCaptureBuffer {
    DelayLine bufferL_;
    DelayLine bufferR_;
    size_t samplesRecorded_ = 0;
    size_t maxSamples_ = 0;

    void write(float left, float right) noexcept {
        bufferL_.write(left);
        bufferR_.write(right);
        if (samplesRecorded_ < maxSamples_) {
            ++samplesRecorded_;
        }
    }

    std::pair<float, float> read(float delaySamples) const noexcept {
        return {
            bufferL_.readLinear(delaySamples),
            bufferR_.readLinear(delaySamples)
        };
    }

    float getFillLevel() const noexcept {
        return 100.0f * static_cast<float>(samplesRecorded_) /
               static_cast<float>(maxSamples_);
    }
};
```

### Alternative Considered: Lock-Free Ring Buffer

Implementations like TPCircularBuffer use virtual memory mapping to provide contiguous reads across the wrap point. This is unnecessary here because:
- Single producer/consumer (audio thread only)
- DelayLine's interpolated read handles wrap elegantly
- No inter-thread communication needed

### Sources

- [Lock-free ring buffer discussion](https://kmdreko.github.io/posts/20191003/a-simple-lock-free-ring-buffer/)
- [TPCircularBuffer](https://github.com/michaeltyson/TPCircularBuffer)
- [Efficient Circular Buffer Discussion (KVR)](https://www.kvraudio.com/forum/viewtopic.php?t=408611)

---

## 4. Voice Stealing Strategies

### Background

When a synthesizer/sampler runs out of polyphony (all voices are active), it must decide which voice to "steal" for a new note/grain. Different strategies have different musical implications.

### Common Strategies

#### 1. Oldest (Age-Based)
Replace the voice that has been playing longest.
- **Pros**: Simple, deterministic
- **Cons**: May cut off a still-audible sustained note

#### 2. Lowest Amplitude
Replace the quietest voice.
- **Pros**: Minimizes audible impact
- **Cons**: Requires tracking envelope state, may cut sustaining notes

#### 3. Shortest Remaining (Spec Requirement)
Replace the voice closest to completing its envelope.
- **Pros**: Least disruptive; voice is already fading out
- **Cons**: Requires envelope phase tracking

#### 4. Pitch-Based Priority
Never steal lowest/highest notes.
- **Pros**: Preserves bass/melody lines
- **Cons**: Complex, context-dependent

### Existing GrainPool Analysis

The current `GrainPool` uses "oldest" stealing:
```cpp
// From grain_pool.h
for (auto& grain : grains_) {
    const size_t age = currentSample - grain.startSample;
    if (age >= oldestAge) {
        oldestAge = age;
        oldest = &grain;
    }
}
```

### Implementation Decision

**Choice**: New SlicePool with "shortest remaining" strategy

**Rationale**:
- Spec explicitly requires it (FR-087a)
- Works naturally with envelope phase [0,1]
- The grain with highest envelopePhase is closest to completion

**Implementation**:
```cpp
Slice* stealShortestRemaining() {
    Slice* victim = nullptr;
    float highestPhase = -1.0f;

    for (auto& slice : slices_) {
        if (slice.active && slice.envelopePhase > highestPhase) {
            highestPhase = slice.envelopePhase;
            victim = &slice;
        }
    }

    // Apply micro-fade to avoid click
    if (victim) {
        // Mark for quick release
        victim->inRelease = true;
    }

    return victim;
}
```

### Click Prevention on Voice Steal

When stealing a voice, abruptly cutting it causes a click. Solutions:
1. **Micro-fade**: Apply 2-5ms fade-out before reuse
2. **Quick release**: Set envelope to release state
3. **Delayed reuse**: Wait until envelope reaches near-zero

### Sources

- [Voice Stealing Discussion (KVR)](https://www.kvraudio.com/forum/viewtopic.php?t=91557)
- [Voice Allocation - Electronic Music Wiki](https://electronicmusic.fandom.com/wiki/Voice_allocation)
- [JUCE Voice Steal Pops](https://forum.juce.com/t/voice-steal-pops/30923)

---

## 5. Grain Envelope Shaping

### Background

Grain envelopes are essential for click-free playback. They control the amplitude of each grain over its duration, ensuring smooth attack and release to prevent discontinuities.

### Key Parameters (from granularsynthesis.com)

1. **Duration**: 10-50ms optimal range
2. **Attack Time**: Prevents clicks at start
3. **Sustain Time**: How long at peak amplitude
4. **Decay/Release Time**: Prevents clicks at end

### Envelope Types

#### Hann (Raised Cosine)
```
w(n) = 0.5 * (1 - cos(2*pi*n / (N-1)))
```
- Smooth, general purpose
- Symmetric attack/decay
- Good spectral properties

#### Trapezoid (Linear Attack/Decay, Flat Sustain)
- Preserves transients
- Configurable attack/decay ratios
- More "present" sound

#### Sine (Half-Cosine)
```
w(n) = sin(pi * n / (N-1))
```
- Better for pitch shifting
- Smoother than Hann

#### Linear (Triangle)
- Simple, efficient
- Faster transients
- Spec requirement (FR-070)

#### Exponential (RC Curve)
- Punchier attack
- More natural decay
- Spec requirement (FR-070)

### Exponential Envelope Implementation

```cpp
void generateExponential(float* output, size_t size,
                         float attackRatio, float releaseRatio) {
    const size_t attackSamples = size * attackRatio;
    const size_t releaseSamples = size * releaseRatio;
    const size_t sustainStart = attackSamples;
    const size_t sustainEnd = size - releaseSamples;

    // Attack: 1 - e^(-5t) (reaches ~99% in attack time)
    for (size_t i = 0; i < attackSamples; ++i) {
        float t = static_cast<float>(i) / attackSamples;
        output[i] = 1.0f - std::exp(-5.0f * t);
    }

    // Sustain: hold at 1.0
    for (size_t i = sustainStart; i < sustainEnd; ++i) {
        output[i] = 1.0f;
    }

    // Release: e^(-5t)
    for (size_t i = sustainEnd; i < size; ++i) {
        float t = static_cast<float>(i - sustainEnd) / releaseSamples;
        output[i] = std::exp(-5.0f * t);
    }
}
```

### Minimum Envelope Time for Click Prevention

Testing shows that:
- **10ms minimum** attack/release prevents audible clicks in most cases
- Shorter times may work but risk artifacts
- Spec aligns with this: FR-065 (attack 0-500ms), FR-067 (release 0-2000ms)

### Implementation Decision

**Choice**: Extend GrainEnvelope with Linear and Exponential types

**Rationale**:
- Existing infrastructure handles envelope generation and lookup
- Just need to add two new envelope types
- Spec requires both (FR-070)

### Sources

- [Grain Envelope Design](https://www.granularsynthesis.com/hthesis/envelope.html)
- [Window Functions for Audio](https://music.arts.uci.edu/dobrian/maxcookbook/generate-window-function-use-amplitude-envelope)
- [Audio Synthesis - Granular](https://michaelkrzyzaniak.com/AudioSynthesis/2_Audio_Synthesis/11_Granular_Synthesis/)

---

## 6. Multi-Voice Pitch Shifting

### Background

Harmonic Drones requires multiple simultaneous pitch-shifted voices to create pad-like textures. Each voice plays the captured audio at a different pitch interval while applying slow drift modulation.

### Pitch Intervals (Semitones)

| Interval | Semitones | Ratio |
|----------|-----------|-------|
| Unison | 0 | 1.0 |
| Minor Third | 3 | 1.189 |
| Major Third | 4 | 1.260 |
| Perfect Fourth | 5 | 1.335 |
| Perfect Fifth | 7 | 1.498 |
| Octave | 12 | 2.0 |

### Existing PitchShiftProcessor

The codebase has a sophisticated pitch shifter with multiple modes:
- Simple: Zero latency, audible artifacts
- Granular: ~46ms latency, good quality
- PhaseVocoder: ~116ms latency, excellent quality
- PitchSync: Variable latency, pitch-synchronized

For drones, the Granular or Simple mode would work well since:
- Drones are sustained, masking artifacts
- Lower latency is preferable
- Multiple instances needed (4 max)

### Multi-Voice Architecture

```cpp
class DroneVoice {
    PitchShiftProcessor shifter_;
    LFO driftLFO_;
    float baseInterval_;  // Semitones

    void prepare(double sampleRate, size_t maxBlockSize) {
        shifter_.prepare(sampleRate, maxBlockSize);
        shifter_.setMode(PitchMode::Simple);  // Low latency for drones
        driftLFO_.prepare(sampleRate);
        driftLFO_.setWaveform(Waveform::Sine);
    }

    void process(const float* in, float* out, size_t n, float drift) {
        // Apply LFO-modulated pitch
        for (size_t i = 0; i < n; ++i) {
            float mod = driftLFO_.process() * drift * 0.5f;  // +/- 50 cents max
            shifter_.setCents(mod * 100.0f);
        }
        shifter_.setSemitones(baseInterval_);
        shifter_.process(in, out, n);
    }
};
```

### Gain Compensation

With N overlapping voices, output can clip. Apply 1/sqrt(N) scaling:
- 1 voice: 1.0
- 2 voices: 0.707
- 3 voices: 0.577
- 4 voices: 0.5

This maintains approximately constant perceived loudness.

### Drift Modulation

Slow LFO modulation creates movement and prevents static sound:
- Rate: 0.1-2.0 Hz (very slow)
- Depth: 0-100% mapped to pitch cents
- Each voice has independent LFO phase

### Implementation Decision

**Choice**: Reuse PitchShiftProcessor with Simple mode

**Rationale**:
- Proven, tested implementation
- Zero latency in Simple mode ideal for real-time freeze
- 4 instances is reasonable CPU load
- LFO provides drift modulation

### Sources

- [Pitchometry Plugin](https://aegeanmusic.com/pitchometry)
- [Time Stretching Overview](https://blogs.zynaptiq.com/bernsee/time-pitch-overview/)
- [UDO DMNO Synthesizer (8-voice architecture)](https://www.udo-audio.com/dmno)

---

## Summary of Key Decisions

| Component | Decision | Key Reason |
|-----------|----------|------------|
| Euclidean | Accumulator + bitmask | Simple, real-time safe, efficient |
| Granular trigger | Poisson/exponential | Organic timing, spec requirement |
| Capture buffer | Wrap DelayLine | Reuse proven code, no lock-free needed |
| Voice stealing | Shortest remaining | Spec requirement, least disruptive |
| Envelope shapes | Extend GrainEnvelope | Existing infrastructure, add Linear/Exp |
| Drone pitch | Reuse PitchShiftProcessor | Proven, multiple modes, LFO for drift |

---

*Research completed: 2026-01-16*
