# Data Model: Granular Distortion Processor

**Date**: 2026-01-27 | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

## Entity Definitions

### GranularDistortion (Main Processor Class)

**Purpose**: Layer 2 DSP processor that applies distortion in time-windowed micro-grains.

**Location**: `dsp/include/krate/dsp/processors/granular_distortion.h`

**Layer**: 2 (Processors)

#### Member Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `grainPool_` | `GrainPool` | — | Manages 64-grain allocation with voice stealing |
| `scheduler_` | `GrainScheduler` | — | Controls grain trigger timing |
| `waveshapers_` | `std::array<Waveshaper, 64>` | — | Per-grain distortion processors |
| `grainStates_` | `std::array<GrainState, 64>` | — | Per-grain additional state |
| `buffer_` | `std::array<float, 32768>` | 0.0f | Circular input buffer |
| `envelopeTable_` | `std::array<float, 2048>` | — | Pre-computed Hann envelope |
| `rng_` | `Xorshift32` | seed(12345) | RNG for variation |
| `driveSmoother_` | `OnePoleSmoother` | — | Smooths base drive changes |
| `mixSmoother_` | `OnePoleSmoother` | — | Smooths mix changes |

#### State Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `writePos_` | `size_t` | 0 | Current write position in circular buffer |
| `samplesWritten_` | `size_t` | 0 | Total samples written (for jitter clamping) |
| `currentSample_` | `size_t` | 0 | Global sample counter (for grain age) |
| `sampleRate_` | `double` | 44100.0 | Current sample rate |
| `prepared_` | `bool` | false | Whether prepare() has been called |

#### Parameter Variables

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `grainSizeMs_` | `float` | 50.0f | [5, 100] | Grain window duration in ms |
| `density_` | `float` | 4.0f | [1, 8] | Approx simultaneous grains |
| `baseDrive_` | `float` | 5.0f | [1, 20] | Base distortion intensity |
| `driveVariation_` | `float` | 0.0f | [0, 1] | Per-grain drive randomization |
| `positionJitterMs_` | `float` | 0.0f | [0, 50] | Position jitter in ms |
| `mix_` | `float` | 1.0f | [0, 1] | Dry/wet mix |
| `baseDistortionType_` | `WaveshapeType` | Tanh | enum | Base distortion algorithm |
| `algorithmVariation_` | `bool` | false | — | Enable random algorithm per grain |

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kBufferSize` | `size_t` | 32768 | Circular buffer size (power of 2) |
| `kBufferMask` | `size_t` | 32767 | Bit mask for wraparound |
| `kEnvelopeTableSize` | `size_t` | 2048 | Envelope lookup table size |
| `kSmoothingTimeMs` | `float` | 10.0f | Parameter smoothing time |
| `kMinGrainSizeMs` | `float` | 5.0f | Minimum grain size |
| `kMaxGrainSizeMs` | `float` | 100.0f | Maximum grain size |
| `kMinDensity` | `float` | 1.0f | Minimum density |
| `kMaxDensity` | `float` | 8.0f | Maximum density |
| `kMinDrive` | `float` | 1.0f | Minimum drive |
| `kMaxDrive` | `float` | 20.0f | Maximum drive |
| `kMinPositionJitterMs` | `float` | 0.0f | Minimum position jitter |
| `kMaxPositionJitterMs` | `float` | 50.0f | Maximum position jitter |

---

### GrainState (Internal Struct)

**Purpose**: Additional per-grain state not present in the base Grain struct.

**Location**: Nested inside GranularDistortion class (private)

#### Fields

| Field | Type | Description |
|-------|------|-------------|
| `drive` | `float` | Per-grain drive value (after variation applied) |
| `startBufferPos` | `size_t` | Frozen start position in circular buffer |
| `grainSizeSamples` | `size_t` | Grain duration in samples |
| `waveshaperIndex` | `size_t` | Index into waveshapers_ array |

---

### GrainPool::Grain (Existing - Reused)

**Purpose**: Core grain state (from Layer 1 primitive).

**Location**: `dsp/include/krate/dsp/primitives/grain_pool.h`

#### Fields Used

| Field | Type | Usage |
|-------|------|-------|
| `readPosition` | `float` | NOT USED (we use GrainState.startBufferPos instead) |
| `envelopePhase` | `float` | Progress through envelope [0, 1] |
| `envelopeIncrement` | `float` | Phase advance per sample |
| `active` | `bool` | Is grain currently playing |
| `startSample` | `size_t` | Sample when grain was triggered (for voice stealing) |

---

## Relationships

```
GranularDistortion
    |
    +-- grainPool_: GrainPool (1:1)
    |       |
    |       +-- grains_[64]: Grain (1:64)
    |
    +-- scheduler_: GrainScheduler (1:1)
    |
    +-- waveshapers_[64]: Waveshaper (1:64)
    |       ^
    |       | indexed by grain
    |       v
    +-- grainStates_[64]: GrainState (1:64)
    |
    +-- buffer_[32768]: float (circular)
    |
    +-- envelopeTable_[2048]: float (lookup)
```

**Index mapping**: When a grain is acquired from GrainPool, its index in `grainPool_.grains_` determines which `waveshapers_[i]` and `grainStates_[i]` to use.

---

## State Transitions

### Grain Lifecycle

```
[Inactive] --trigger--> [Active] --envelope complete--> [Released] --pool return--> [Inactive]
     ^                                                                                   |
     |                                                                                   |
     +---------------------------voice stealing (pool full)------------------------------+
```

### Parameter Update Flow

```
setXxx() --> parameter stored --> smoother.setTarget() --> process() reads smoother.process()
```

---

## Validation Rules

### Parameter Validation (applied in setters)

| Parameter | Validation | Action |
|-----------|------------|--------|
| `grainSizeMs` | Clamp to [5, 100] | `std::clamp()` |
| `density` | Clamp to [1, 8] | `std::clamp()` |
| `baseDrive` | Clamp to [1, 20] | `std::clamp()` |
| `driveVariation` | Clamp to [0, 1] | `std::clamp()` |
| `positionJitterMs` | Clamp to [0, 50] | `std::clamp()` |
| `mix` | Clamp to [0, 1] | `std::clamp()` |
| `distortionType` | Valid enum | Implicit (enum class) |

### Per-Grain Drive Calculation (FR-015)

```cpp
// Formula: baseDrive * (1.0 + driveVariation * random[-1,1])
// Then clamp to [1.0, 20.0]
float perGrainDrive = baseDrive * (1.0f + driveVariation * rng_.nextFloat());
perGrainDrive = std::clamp(perGrainDrive, kMinDrive, kMaxDrive);
```

### Position Jitter Clamping (FR-024-NEW)

```cpp
// Clamp jitter to available buffer history
const size_t availableHistory = std::min(samplesWritten_, kBufferSize - 1);
const size_t maxJitterSamples = static_cast<size_t>(kMaxPositionJitterMs * sampleRate_ / 1000.0f);
const size_t requestedJitterSamples = static_cast<size_t>(positionJitterMs_ * sampleRate_ / 1000.0f);
const size_t effectiveJitterSamples = std::min(requestedJitterSamples,
                                                std::min(maxJitterSamples, availableHistory));
```

---

## Processing Algorithm

### Per-Sample Processing (in process())

```cpp
float process(float input) noexcept {
    // 1. Handle invalid input
    if (isNaN(input) || isInf(input)) {
        reset();
        return 0.0f;
    }

    // 2. Store dry signal
    const float dry = input;

    // 3. Write to circular buffer
    buffer_[writePos_] = input;
    writePos_ = (writePos_ + 1) & kBufferMask;
    samplesWritten_ = std::min(samplesWritten_ + 1, kBufferSize);

    // 4. Check for grain trigger
    if (scheduler_.process()) {
        triggerGrain();
    }

    // 5. Process all active grains
    float wet = 0.0f;
    for (Grain* grain : grainPool_.activeGrains()) {
        wet += processGrain(grain);
    }

    // 6. Advance sample counter
    ++currentSample_;

    // 7. Mix dry/wet
    const float smoothedMix = mixSmoother_.process();
    return (1.0f - smoothedMix) * dry + smoothedMix * wet;
}
```

### triggerGrain() Algorithm

```cpp
void triggerGrain() noexcept {
    // 1. Acquire grain (may steal oldest)
    Grain* grain = grainPool_.acquireGrain(currentSample_);
    if (!grain) return;

    // 2. Find grain index
    const size_t grainIndex = getGrainIndex(grain);

    // 3. Calculate per-grain drive
    const float smoothedDrive = driveSmoother_.process();
    float grainDrive = smoothedDrive;
    if (driveVariation_ > 0.0f) {
        grainDrive = smoothedDrive * (1.0f + driveVariation_ * rng_.nextFloat());
        grainDrive = std::clamp(grainDrive, kMinDrive, kMaxDrive);
    }

    // 4. Configure waveshaper
    Waveshaper& ws = waveshapers_[grainIndex];
    ws.setDrive(grainDrive);
    ws.setAsymmetry(0.0f);  // Always symmetric
    if (algorithmVariation_) {
        const int typeIndex = static_cast<int>(rng_.nextUnipolar() * 9.0f);
        ws.setType(static_cast<WaveshapeType>(std::min(typeIndex, 8)));
    } else {
        ws.setType(baseDistortionType_);
    }

    // 5. Calculate position jitter
    size_t jitterOffset = 0;
    if (positionJitterMs_ > 0.0f) {
        const size_t maxJitter = calculateEffectiveJitter();
        const float jitterRandom = rng_.nextFloat();  // [-1, 1]
        jitterOffset = static_cast<size_t>(jitterRandom * static_cast<float>(maxJitter));
    }

    // 6. Store grain state
    GrainState& state = grainStates_[grainIndex];
    state.drive = grainDrive;
    state.startBufferPos = (writePos_ - 1 + kBufferSize - jitterOffset) & kBufferMask;
    state.grainSizeSamples = static_cast<size_t>(grainSizeMs_ * sampleRate_ / 1000.0f);

    // 7. Initialize envelope
    grain->envelopePhase = 0.0f;
    grain->envelopeIncrement = (state.grainSizeSamples > 0)
        ? 1.0f / static_cast<float>(state.grainSizeSamples)
        : 1.0f;
}
```

### processGrain() Algorithm

```cpp
float processGrain(Grain* grain) noexcept {
    const size_t grainIndex = getGrainIndex(grain);
    const GrainState& state = grainStates_[grainIndex];
    Waveshaper& ws = waveshapers_[grainIndex];

    // 1. Get envelope value
    const float envelope = GrainEnvelope::lookup(
        envelopeTable_.data(), kEnvelopeTableSize, grain->envelopePhase);

    // 2. Calculate read position (frozen start + progress)
    const size_t progressSamples = static_cast<size_t>(
        grain->envelopePhase * static_cast<float>(state.grainSizeSamples));
    const size_t readPos = (state.startBufferPos + progressSamples) & kBufferMask;

    // 3. Read from buffer
    const float bufferSample = buffer_[readPos];

    // 4. Apply waveshaper
    const float distorted = ws.process(bufferSample);

    // 5. Apply envelope
    const float output = distorted * envelope;

    // 6. Advance envelope phase
    grain->envelopePhase += grain->envelopeIncrement;

    // 7. Release if complete
    if (grain->envelopePhase >= 1.0f) {
        grainPool_.releaseGrain(grain);
    }

    return output;
}
```

---

## Memory Layout

```
GranularDistortion instance (~143KB total)
|
+-- buffer_[32768]         : 131,072 bytes (128KB)
+-- waveshapers_[64]       :     768 bytes (~12B each)
+-- grainStates_[64]       :     768 bytes (~12B each)
+-- grainPool_             :   2,560 bytes (internal grains_)
+-- envelopeTable_[2048]   :   8,192 bytes (8KB)
+-- scheduler_             :      48 bytes
+-- smoothers_ (2x)        :      80 bytes
+-- rng_                   :       4 bytes
+-- scalars                :      ~50 bytes
```
