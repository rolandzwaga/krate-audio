# Research: Stochastic Filter Algorithms

**Feature**: 087-stochastic-filter
**Date**: 2026-01-23
**Purpose**: Document algorithm decisions for all four random modulation modes

---

## 1. Walk Mode (Brownian Motion)

### Decision
Use a bounded random walk with configurable step size controlled by change rate.

### Algorithm
```cpp
// Per control-rate update (every 32-64 samples)
float delta = rng.nextFloat() * stepSize;  // nextFloat() returns [-1, 1]
walkValue += delta;
walkValue = std::clamp(walkValue, -1.0f, 1.0f);  // Bound to [-1, 1]

// Step size derived from change rate
// At 1 Hz rate, we want full range traversal in ~1 second
// stepSize = (2.0 / samplesPerSecond) * changeRateHz * updateInterval
```

### Rationale
- Simple and efficient (single random value per update)
- Bounded output prevents runaway values
- Step size controls smoothness vs. activity tradeoff
- Clamping provides natural "reflection" at boundaries

### Alternatives Considered
1. **Ornstein-Uhlenbeck process** (mean-reverting): More complex, not needed for modulation
2. **Unbounded walk with wrapping**: Could cause discontinuities
3. **Gaussian steps**: Unnecessary complexity, uniform works well for audio

---

## 2. Jump Mode (Discrete Random)

### Decision
Generate new random value at specified rate, with smoothing applied.

### Algorithm
```cpp
// Timer-based trigger
samplesUntilNextJump -= updateBlockSize;
if (samplesUntilNextJump <= 0) {
    targetJumpValue = rng.nextFloat();  // [-1, 1]
    samplesUntilNextJump += sampleRate / changeRateHz;
}

// Smoothing handled by OnePoleSmoother applied to modulation output
```

### Rationale
- Clean separation between random generation and smoothing
- Rate directly maps to jumps-per-second (intuitive)
- Smoothing prevents clicks (FR-011)

### Alternatives Considered
1. **Poisson-distributed intervals**: More complex, not needed for typical use
2. **S&H from LFO**: Duplicates existing component, less flexible

---

## 3. Lorenz Mode (Chaotic Attractor)

### Decision
Use discrete-time Lorenz attractor with standard parameters, X-axis output scaled to [-1, 1].

### Algorithm (Euler Integration)
```cpp
// Standard Lorenz parameters (classic "butterfly" attractor)
constexpr float sigma = 10.0f;
constexpr float rho = 28.0f;
constexpr float beta = 8.0f / 3.0f;

// Time step - scaled by change rate
// Base dt chosen for stability at 44.1kHz with 32-sample blocks
float dt = 0.0001f * changeRateHz;

// Euler integration (per control-rate update)
float dx = sigma * (y - x) * dt;
float dy = (x * (rho - z) - y) * dt;
float dz = (x * y - beta * z) * dt;
x += dx;
y += dy;
z += dz;

// Output: X-axis normalized to [-1, 1]
// Lorenz X typically ranges [-20, 20] for standard params
float output = x / 20.0f;
output = std::clamp(output, -1.0f, 1.0f);
```

### Parameter Justification
- **sigma = 10, rho = 28, beta = 8/3**: Classic Lorenz parameters producing chaotic behavior
- **X-axis**: Primary oscillation dimension (spec clarification)
- **Range [-20, 20]**: Empirical bounds for X with standard parameters
- **dt scaling**: Change rate controls attractor "speed"

### Rationale
- Euler integration is sufficient for audio modulation (not scientific simulation)
- Fixed parameters ensure consistent chaotic character
- Bounded output via clamping handles edge cases
- Deterministic given initial state and seed

### Alternatives Considered
1. **Runge-Kutta integration**: More accurate but unnecessary for modulation
2. **Adaptive time step**: Overengineered for this use case
3. **Rossler attractor**: Simpler but less distinctive character
4. **Y or Z axis output**: X chosen per spec clarification

### Initialization
```cpp
// Initial state from seed (deterministic)
void seedLorenz(uint32_t seed) {
    rng.seed(seed);
    x = rng.nextFloat() * 0.1f + 0.1f;  // Small non-zero values
    y = rng.nextFloat() * 0.1f + 0.1f;  // to start on attractor
    z = rng.nextFloat() * 0.1f + 25.0f; // Near center of attractor
}
```

---

## 4. Perlin Mode (Coherent Noise)

### Decision
Implement 1D Perlin noise with 3 octaves using gradient interpolation.

### Algorithm
```cpp
// Perlin 1D with 3 octaves (spec clarification)
float perlin1D(float t) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int octave = 0; octave < 3; ++octave) {
        value += noise1D(t * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;   // Persistence
        frequency *= 2.0f;   // Lacunarity
    }

    return value / maxValue;  // Normalize to [-1, 1]
}

// Base noise function (gradient noise)
float noise1D(float x) {
    int xi = static_cast<int>(std::floor(x));
    float xf = x - static_cast<float>(xi);

    // Smoothstep interpolation (5th order)
    float u = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);

    // Gradients from permutation table (seeded hash)
    float g0 = gradientAt(xi);
    float g1 = gradientAt(xi + 1);

    // Interpolate between gradients
    return g0 * (1.0f - u) + g1 * u;
}

// Gradient lookup (deterministic from seed)
float gradientAt(int i) {
    // Hash function using seed
    uint32_t hash = (i * 0x9E3779B9u) ^ seed_;
    hash ^= hash >> 16;
    hash *= 0x85EBCA6Bu;
    hash ^= hash >> 13;
    // Convert to [-1, 1]
    return static_cast<float>(hash) / 2147483647.5f - 1.0f;
}
```

### Time Progression
```cpp
// Per control-rate update
perlinTime += changeRateHz * updateIntervalSec;

// Get modulation value
float modulation = perlin1D(perlinTime);
```

### Rationale
- 3 octaves provides good detail without excessive CPU (spec clarification)
- 5th order smoothstep prevents derivative discontinuities
- Hash-based gradients are deterministic and seedable
- Time progression controlled by change rate

### Alternatives Considered
1. **Simplex noise**: More complex, 2D+ optimized (not needed for 1D)
2. **Value noise**: Simpler but less smooth character
3. **More octaves**: Diminishing returns, higher CPU
4. **Pre-computed gradient table**: Slightly faster but more memory

---

## 5. Control-Rate Update Strategy

### Decision
Update modulation values every 32 samples, with per-sample smoothing.

**Why 32 samples (not 64)?**
- At 44.1kHz, 32 samples = 0.73ms update rate (~1378 Hz)
- At 44.1kHz, 64 samples = 1.45ms update rate (~689 Hz)
- 32 samples provides sub-millisecond resolution, better for fast-changing modes like Jump
- CPU overhead difference is negligible (modulation calculation is lightweight)
- Matches typical host automation update rates
- Smoothing between updates masks any quantization artifacts

### Implementation
```cpp
static constexpr size_t kControlRateInterval = 32;  // samples (chosen over 64 for better temporal resolution)

void processBlock(float* buffer, size_t numSamples) {
    size_t remaining = numSamples;
    size_t offset = 0;

    while (remaining > 0) {
        // Check if control-rate update needed
        if (samplesUntilUpdate_ <= 0) {
            updateModulation();  // Update random mode state
            samplesUntilUpdate_ += kControlRateInterval;
        }

        // Process samples until next update
        size_t samplesToProcess = std::min(remaining,
            static_cast<size_t>(samplesUntilUpdate_));

        // Apply modulation with smoothing
        for (size_t i = 0; i < samplesToProcess; ++i) {
            float smoothedCutoff = cutoffSmoother_.process();
            float smoothedResonance = resonanceSmoother_.process();  // resonance (Q)
            filter_.setCutoff(smoothedCutoff);
            filter_.setResonance(smoothedResonance);
            buffer[offset + i] = filter_.process(buffer[offset + i]);
        }

        offset += samplesToProcess;
        remaining -= samplesToProcess;
        samplesUntilUpdate_ -= static_cast<int>(samplesToProcess);
    }
}
```

### Rationale
- 32 samples at 44.1kHz = ~0.7ms update rate (adequate for modulation)
- Per-sample smoothing prevents zipper artifacts
- Block-based processing enables vectorization potential
- SVF handles rapid parameter changes gracefully

---

## 6. Filter Type Transition (Parallel Crossfade)

### Decision
Run two filter instances during type transitions with complementary gain crossfade.

### Algorithm
```cpp
// State for type transition
SVF filterA_, filterB_;        // Two filter instances
OnePoleSmoother crossfadeMix_; // 0.0 = filterA, 1.0 = filterB
SVFMode currentTypeA_, currentTypeB_;
bool isTransitioning_ = false;

void setFilterType(SVFMode newType) {
    if (newType != currentTypeA_ && !isTransitioning_) {
        // Start transition
        currentTypeB_ = newType;
        filterB_.setMode(newType);
        crossfadeMix_.setTarget(1.0f);  // Fade to B
        isTransitioning_ = true;
    }
}

float processWithCrossfade(float input) {
    if (!isTransitioning_) {
        return filterA_.process(input);
    }

    // Process through both filters
    float outA = filterA_.process(input);
    float outB = filterB_.process(input);

    // Crossfade
    float mix = crossfadeMix_.process();
    float output = outA * (1.0f - mix) + outB * mix;

    // Check if transition complete
    if (crossfadeMix_.isComplete()) {
        // Swap: B becomes new A
        std::swap(filterA_, filterB_);
        currentTypeA_ = currentTypeB_;
        crossfadeMix_.snapTo(0.0f);
        isTransitioning_ = false;
    }

    return output;
}
```

### Rationale
- Parallel processing ensures continuous audio (no gaps)
- Linear crossfade is simple and effective for filter transitions
- Both filters receive same input to maintain phase coherence
- Smoothing time controls transition duration (FR-011)

---

## 7. Cutoff Modulation Scaling

### Decision
Use octave-based scaling centered on base frequency.

### Algorithm
```cpp
// Base cutoff in Hz (e.g., 1000 Hz)
float baseCutoffHz;

// Modulation value in [-1, 1] from random mode
float modulation;

// Octave range (e.g., 2 octaves)
float octaveRange;

// Calculate effective cutoff
float octaveOffset = modulation * octaveRange;
float effectiveCutoff = baseCutoffHz * std::pow(2.0f, octaveOffset);

// Clamp to valid range
effectiveCutoff = std::clamp(effectiveCutoff,
    SVF::kMinCutoff,
    static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio);
```

### Rationale
- Octave scaling is perceptually linear (musical intervals)
- Bipolar modulation allows both higher and lower frequencies
- Centered on base frequency (0 modulation = base frequency)
- Clamping prevents invalid filter states

---

## 8. Resonance (Q) Modulation Scaling

### Decision
Linear interpolation within Q range, mapped from modulation value.

### Algorithm
```cpp
// Resonance (Q) range - maps to SVF's supported range (0.1-30.0)
float qMin = 0.5f;   // Low resonance (Q)
float qMax = 8.0f;   // High resonance (Q)

// Modulation value in [-1, 1]
float modulation;

// Map [-1, 1] to [0, 1]
float normalized = (modulation + 1.0f) * 0.5f;

// Interpolate within range
float effectiveQ = qMin + normalized * (qMax - qMin);

// Clamp to SVF limits (0.1-30.0)
effectiveQ = std::clamp(effectiveQ, SVF::kMinQ, SVF::kMaxQ);
```

### Rationale
- Linear resonance (Q) scaling is acceptable (Q is already logarithmic in perception)
- Full modulation range maps to full Q range
- Clamping ensures valid filter state

---

## 9. Determinism and Seeding

### Decision
Single seed controls all random state for reproducible behavior.

### Implementation
```cpp
void setSeed(uint32_t seed) {
    seed_ = seed;
    rng_.seed(seed);

    // Reset mode-specific state
    walkValue_ = 0.0f;
    initializeLorenzState(seed);
    perlinTime_ = 0.0f;
    samplesUntilNextJump_ = 0.0f;
}

void reset() {
    // Restore from saved seed
    rng_.seed(seed_);
    walkValue_ = 0.0f;
    initializeLorenzState(seed_);
    perlinTime_ = 0.0f;
    samplesUntilNextJump_ = 0.0f;
    // ... reset filters and smoothers
}
```

### Rationale
- Single seed simplifies API (FR-012)
- reset() restores seed for reproducibility (FR-024)
- Perlin gradient table is deterministic from seed

---

## Summary of Decisions

| Aspect | Decision | Key Rationale |
|--------|----------|---------------|
| Walk | Bounded random walk | Simple, bounded, efficient |
| Jump | Timer-based trigger | Clean separation, intuitive rate |
| Lorenz | Euler integration, X-axis | Standard chaos, adequate accuracy |
| Perlin | 3 octaves, gradient noise | Good detail, CPU efficient |
| Update rate | 32 samples | Balance CPU vs. responsiveness |
| Type transition | Parallel crossfade | Click-free, continuous audio |
| Cutoff scaling | Octave-based | Perceptually linear |
| Q scaling | Linear interpolation | Simple, effective |
| Seeding | Single seed for all | Reproducible, simple API |
