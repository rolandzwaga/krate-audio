# Data Model: GranularFilter

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entities

### 1. FilteredGrainState

Per-grain slot filter state. One instance per grain slot (64 total).

```cpp
struct FilteredGrainState {
    SVF filterL;           ///< Left channel SVF filter
    SVF filterR;           ///< Right channel SVF filter
    float cutoffHz;        ///< This grain's randomized cutoff frequency
    bool filterEnabled;    ///< Whether this grain uses filtering (captures state at trigger time)
};
```

**Fields:**

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| filterL | SVF | unprepared | - | Left channel filter instance |
| filterR | SVF | unprepared | - | Right channel filter instance |
| cutoffHz | float | 1000.0f | 20Hz to sampleRate*0.495 | Per-grain cutoff after randomization |
| filterEnabled | bool | true | - | Snapshot of global filterEnabled at grain trigger |

**Relationships:**
- Indexed by grain slot position (0-63)
- Parallel to GrainPool's internal grain array

---

### 2. GranularFilter

Main system component. Composes granular processing with per-grain filtering.

```cpp
class GranularFilter {
public:
    // Constants
    static constexpr float kDefaultMaxDelaySeconds = 2.0f;
    static constexpr float kDefaultSmoothTimeMs = 20.0f;
    static constexpr float kFreezeCrossfadeMs = 50.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMaxRandomizationOctaves = 4.0f;

    // Lifecycle
    void prepare(double sampleRate, float maxDelaySeconds = kDefaultMaxDelaySeconds) noexcept;
    void reset() noexcept;

    // === Granular Parameters (same as GranularEngine) ===
    void setGrainSize(float ms) noexcept;
    void setDensity(float grainsPerSecond) noexcept;
    void setPitch(float semitones) noexcept;
    void setPitchSpray(float amount) noexcept;
    void setPosition(float ms) noexcept;
    void setPositionSpray(float amount) noexcept;
    void setReverseProbability(float probability) noexcept;
    void setPanSpray(float amount) noexcept;
    void setJitter(float amount) noexcept;
    void setEnvelopeType(GrainEnvelopeType type) noexcept;
    void setPitchQuantMode(PitchQuantMode mode) noexcept;
    void setTexture(float amount) noexcept;
    void setFreeze(bool frozen) noexcept;

    // Getters for granular parameters
    [[nodiscard]] PitchQuantMode getPitchQuantMode() const noexcept;
    [[nodiscard]] float getTexture() const noexcept;
    [[nodiscard]] bool isFrozen() const noexcept;
    [[nodiscard]] size_t activeGrainCount() const noexcept;

    // === Filter Parameters (NEW) ===
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterType(SVFMode mode) noexcept;
    void setCutoffRandomization(float octaves) noexcept;

    // Getters for filter parameters
    [[nodiscard]] bool isFilterEnabled() const noexcept;
    [[nodiscard]] float getFilterCutoff() const noexcept;
    [[nodiscard]] float getFilterResonance() const noexcept;
    [[nodiscard]] SVFMode getFilterType() const noexcept;
    [[nodiscard]] float getCutoffRandomization() const noexcept;

    // Processing
    void process(float inputL, float inputR, float& outputL, float& outputR) noexcept;

    // Deterministic seeding
    void seed(uint32_t seedValue) noexcept;

private:
    // Internal helpers
    void triggerNewGrain(float grainSizeMs, float pitchSemitones, float positionMs) noexcept;
    [[nodiscard]] float calculateRandomizedCutoff() noexcept;
    [[nodiscard]] size_t getGrainSlotIndex(const Grain* grain) const noexcept;

    // === Granular Components ===
    DelayLine delayL_;
    DelayLine delayR_;
    GrainPool pool_;
    GrainScheduler scheduler_;
    GrainProcessor processor_;

    // === Filter State (NEW) ===
    std::array<FilteredGrainState, GrainPool::kMaxGrains> filterStates_;

    // === Parameter Smoothers ===
    OnePoleSmoother grainSizeSmoother_;
    OnePoleSmoother pitchSmoother_;
    OnePoleSmoother positionSmoother_;
    OnePoleSmoother gainScaleSmoother_;
    LinearRamp freezeCrossfade_;

    // === RNG ===
    Xorshift32 rng_{54321};

    // === Granular Parameter State ===
    float grainSizeMs_ = 100.0f;
    float density_ = 10.0f;
    float pitchSemitones_ = 0.0f;
    float pitchSpray_ = 0.0f;
    float positionMs_ = 500.0f;
    float positionSpray_ = 0.0f;
    float reverseProbability_ = 0.0f;
    float panSpray_ = 0.0f;
    GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;
    PitchQuantMode pitchQuantMode_ = PitchQuantMode::Off;
    float texture_ = 0.0f;
    bool frozen_ = false;

    // === Filter Parameter State (NEW) ===
    bool filterEnabled_ = true;
    float baseCutoffHz_ = 1000.0f;
    float resonanceQ_ = 0.7071f;  // Butterworth
    SVFMode filterType_ = SVFMode::Lowpass;
    float cutoffRandomizationOctaves_ = 0.0f;

    // === Runtime State ===
    size_t currentSample_ = 0;
    double sampleRate_ = 44100.0;
};
```

---

## Parameter Constraints

### Granular Parameters (inherited from GranularEngine)

| Parameter | Range | Default | Units |
|-----------|-------|---------|-------|
| grainSize | 10-500 | 100 | ms |
| density | 1-100 | 10 | grains/sec |
| pitch | -24 to +24 | 0 | semitones |
| pitchSpray | 0-1 | 0 | normalized |
| position | 0-2000 | 500 | ms |
| positionSpray | 0-1 | 0 | normalized |
| reverseProbability | 0-1 | 0 | probability |
| panSpray | 0-1 | 0 | normalized |
| jitter | 0-1 | 0.5 | normalized |
| texture | 0-1 | 0 | normalized |

### Filter Parameters (NEW)

| Parameter | Range | Default | Units | Clamping |
|-----------|-------|---------|-------|----------|
| filterEnabled | bool | true | - | N/A |
| baseCutoffHz | 20 to sampleRate*0.495 | 1000 | Hz | Hard clamp |
| resonanceQ | 0.5-20 | 0.7071 | Q | Hard clamp |
| filterType | LP/HP/BP/Notch | LP | enum | N/A |
| cutoffRandomizationOctaves | 0-4 | 0 | octaves | Hard clamp |

---

## State Transitions

### Grain Lifecycle with Filter

```
INACTIVE ──[acquireGrain]──> ACTIVE
    │                           │
    │                           ├── Filter L/R reset()
    │                           ├── Calculate randomized cutoff
    │                           ├── Configure filter cutoff/Q/type
    │                           └── Capture filterEnabled snapshot
    │
    │                    ┌──────┴──────┐
    │                    │  PROCESSING │
    │                    │             │
    │                    │ For each sample:
    │                    │ 1. Read delay buffer
    │                    │ 2. Apply pitch (playback rate)
    │                    │ 3. Apply envelope
    │                    │ 4. Apply filter (if enabled)
    │                    │ 5. Apply pan
    │                    │ 6. Sum to output
    │                    │ 7. Advance grain state
    │                    │             │
    │                    └──────┬──────┘
    │                           │
    │     [envelope complete]   │
    │                           v
INACTIVE <──[releaseGrain]── COMPLETE
```

### Filter Configuration Update

```
setFilterCutoff(hz)
    │
    ├── Clamp hz to [20, sampleRate*0.495]
    ├── Store baseCutoffHz_
    └── Note: Does NOT update active grains
        (they keep their randomized cutoff)

setFilterResonance(q)
    │
    ├── Clamp q to [0.5, 20]
    ├── Store resonanceQ_
    └── Update all active grain filters immediately
        (Q is global, not per-grain)

setFilterType(mode)
    │
    ├── Store filterType_
    └── Update all active grain filters immediately
        (type is global, not per-grain)
```

---

## Validation Rules

### Cutoff Clamping

```cpp
float clampCutoff(float hz) const noexcept {
    const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
    return std::clamp(hz, kMinCutoffHz, maxCutoff);
}
```

### Q Clamping

```cpp
float clampQ(float q) const noexcept {
    return std::clamp(q, kMinQ, kMaxQ);  // 0.5 to 20
}
```

### Randomization Octave Clamping

```cpp
float clampRandomization(float octaves) const noexcept {
    return std::clamp(octaves, 0.0f, kMaxRandomizationOctaves);  // 0 to 4
}
```

### Cutoff Randomization Formula

```cpp
float calculateRandomizedCutoff() noexcept {
    if (cutoffRandomizationOctaves_ <= 0.0f) {
        return clampCutoff(baseCutoffHz_);
    }

    // Bipolar random: [-1, 1]
    const float randomOffset = rng_.nextFloat();

    // Scale to octaves: [-octaves, +octaves]
    const float octaveOffset = randomOffset * cutoffRandomizationOctaves_;

    // Calculate cutoff: base * 2^offset
    const float cutoff = baseCutoffHz_ * std::exp2(octaveOffset);

    return clampCutoff(cutoff);
}
```

---

## Memory Layout

### FilteredGrainState Array

```
Index:  0     1     2    ...   63
      +-----+-----+-----+     +-----+
      | SVF | SVF | SVF | ... | SVF |  <- filterL
      | SVF | SVF | SVF | ... | SVF |  <- filterR
      | Hz  | Hz  | Hz  | ... | Hz  |  <- cutoffHz
      | en  | en  | en  | ... | en  |  <- filterEnabled
      +-----+-----+-----+     +-----+

Total: 64 * ~168 bytes = ~10.75 KB
```

### Parallel Indexing

```
GrainPool::grains_[i]  <--->  filterStates_[i]

Both arrays have 64 elements, indexed identically.
When grain pointer is obtained, derive index from:
  index = grain - &pool_.grains_[0]  // Requires access to pool internal array
```

---

## Invariants

1. **Filter array size matches grain pool**: `filterStates_.size() == GrainPool::kMaxGrains`
2. **Q always within spec range**: `0.5 <= resonanceQ_ <= 20.0`
3. **Cutoff always valid**: `20 Hz <= cutoff <= sampleRate * 0.495`
4. **Randomization non-negative**: `0 <= cutoffRandomizationOctaves_ <= 4`
5. **Filter state reset on grain acquire**: No state leakage between grain lifetimes
6. **Filter type/Q global**: All active grains share same type and Q value
7. **Filter cutoff per-grain**: Each grain has independently randomized cutoff
