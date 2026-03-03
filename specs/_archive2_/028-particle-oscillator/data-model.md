# Data Model: Particle / Swarm Oscillator

**Feature Branch**: `028-particle-oscillator`
**Date**: 2026-02-06

---

## Entities

### SpawnMode (Enumeration)

**Location**: `dsp/include/krate/dsp/processors/particle_oscillator.h`
**Namespace**: `Krate::DSP`

```cpp
enum class SpawnMode : uint8_t {
    Regular = 0,  ///< Evenly spaced particle spawns (interval = lifetime / density)
    Random  = 1,  ///< Stochastic spawn timing (Poisson-like, average rate = density / lifetime)
    Burst   = 2   ///< Manual trigger only via triggerBurst()
};
```

**Validation**: No validation needed; type-safe enum.
**State transitions**: Any mode can transition to any other mode at any time. Existing particles are unaffected.

---

### Particle (Internal struct)

**Location**: `dsp/include/krate/dsp/processors/particle_oscillator.h` (inside `detail` namespace or as private nested struct)
**Namespace**: `Krate::DSP`

```cpp
struct Particle {
    // Sine oscillator state
    float phase = 0.0f;              ///< Phase accumulator [0, 1)
    float phaseIncrement = 0.0f;     ///< Phase advance per sample (freq / sampleRate)
    float baseFrequency = 0.0f;      ///< Assigned frequency at spawn (Hz)

    // Lifetime tracking
    float envelopePhase = 0.0f;      ///< Progress through lifetime [0, 1]
    float envelopeIncrement = 0.0f;  ///< Envelope phase advance per sample

    // Drift state
    float driftState = 0.0f;         ///< Low-pass filtered random walk value [-1, 1]
    float driftRange = 0.0f;         ///< Maximum frequency deviation (Hz) for this particle

    // Status
    bool active = false;             ///< Is particle currently producing sound
};
```

**Fields**:
| Field | Type | Range | Description |
|-------|------|-------|-------------|
| phase | float | [0, 1) | Normalized phase accumulator for sine generation |
| phaseIncrement | float | > 0 | freq / sampleRate, recalculated with drift each sample |
| baseFrequency | float | [1, Nyquist) | Center + scatter offset, assigned at spawn |
| envelopePhase | float | [0, 1] | 0 = start of life, 1 = end of life |
| envelopeIncrement | float | > 0 | 1.0 / lifetimeSamples |
| driftState | float | [-1, 1] | Current value of low-pass filtered noise |
| driftRange | float | >= 0 | Max Hz deviation (proportional to scatter * center) |
| active | bool | true/false | Whether particle is currently producing sound |

**Validation**: All fields are set atomically at spawn time. No external validation needed.

**Lifecycle**:
1. Inactive: `active = false`, all other fields irrelevant
2. Spawn: All fields initialized, `active = true`, `envelopePhase = 0`, `phase = 0`
3. Active: Phase and envelope advance each sample, drift updates frequency
4. Expire: `envelopePhase >= 1.0` triggers deactivation (`active = false`)

---

### ParticleOscillator (Main class)

**Location**: `dsp/include/krate/dsp/processors/particle_oscillator.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (Processors)

#### Constants

```cpp
static constexpr size_t kMaxParticles = 64;       ///< Maximum particle count (FR-006)
static constexpr size_t kEnvTableSize = 256;       ///< Envelope lookup table size (FR-021)
static constexpr size_t kNumEnvelopeTypes = 6;     ///< Number of GrainEnvelopeType values
static constexpr float kMinFrequency = 1.0f;       ///< Minimum center frequency Hz (FR-004)
static constexpr float kMinLifetimeMs = 1.0f;      ///< Minimum lifetime ms (FR-007, edge case)
static constexpr float kMaxLifetimeMs = 10000.0f;  ///< Maximum lifetime ms (FR-007)
static constexpr float kMaxScatter = 48.0f;        ///< Maximum scatter semitones (FR-005)
static constexpr float kOutputClamp = 2.0f;        ///< Output safety clamp (FR-017)
```

#### Configuration State (set via setters)

| Member | Type | Default | Range | Setter |
|--------|------|---------|-------|--------|
| centerFrequency_ | float | 440.0f | [1, Nyquist) | setFrequency() |
| scatter_ | float | 0.0f | [0, 48] semitones | setFrequencyScatter() |
| density_ | float | 1.0f | [1, 64] | setDensity() |
| lifetimeMs_ | float | 100.0f | [1, 10000] ms | setLifetime() |
| spawnMode_ | SpawnMode | Regular | enum | setSpawnMode() |
| driftAmount_ | float | 0.0f | [0, 1] | setDriftAmount() |
| currentEnvType_ | size_t | 0 | [0, 5] | setEnvelopeType() |

#### Derived/Cached State (computed from configuration)

| Member | Type | Computed From | Updated When |
|--------|------|---------------|--------------|
| normFactor_ | float | 1/sqrt(density) | setDensity() |
| lifetimeSamples_ | float | lifetimeMs * sampleRate / 1000 | setLifetime(), prepare() |
| interonsetSamples_ | float | lifetimeSamples / density | setLifetime(), setDensity(), prepare() |
| nyquist_ | float | sampleRate / 2 | prepare() |
| driftFilterCoeff_ | float | exp(-2*pi*10/sampleRate) | prepare() |
| sampleRate_ | double | prepare() arg | prepare() |

#### Processing State (modified each sample)

| Member | Type | Description |
|--------|------|-------------|
| particles_ | std::array<Particle, 64> | Fixed particle pool |
| samplesUntilNextSpawn_ | float | Countdown to next spawn (Regular/Random) |
| rng_ | Xorshift32 | PRNG for scatter, timing, drift noise |
| prepared_ | bool | Whether prepare() has been called |

#### Pre-computed Tables

| Member | Type | Size | Computed In |
|--------|------|------|-------------|
| envelopeTables_ | std::array<std::array<float, 256>, 6> | 6 KB | prepare() |

---

## Relationships

```
ParticleOscillator 1──────*  Particle         (owns fixed pool of up to 64)
ParticleOscillator 1──────1  Xorshift32       (owns PRNG instance)
ParticleOscillator 1──────*  EnvelopeTable    (owns 6 precomputed tables)
ParticleOscillator uses────── SpawnMode        (configuration enum)
ParticleOscillator uses────── GrainEnvelopeType (from core/grain_envelope.h)
ParticleOscillator uses────── GrainEnvelope    (generate/lookup functions)
ParticleOscillator uses────── semitonesToRatio  (from core/pitch_utils.h)
ParticleOscillator uses────── math_constants    (kTwoPi from core/math_constants.h)
ParticleOscillator uses────── detail::isNaN/isInf (from core/db_utils.h)
```

---

## State Machine

### Oscillator Lifecycle

```
[Uninitialized] ──prepare()──> [Ready] ──processBlock()──> [Producing]
                                  ^                            |
                                  └───────reset()──────────────┘

[Uninitialized]: prepared_ = false, processBlock outputs silence
[Ready]: prepared_ = true, particles_ cleared, tables computed
[Producing]: particles spawn/expire, output generated
```

### Particle Lifecycle

```
[Inactive] ──spawn()──> [Active] ──expire()──> [Inactive]
                           |
                        (voice steal)
                           |
                     [Reinitialize]
```

### Spawn Mode Behavior

```
Regular:  |--t--|--t--|--t--|--t--|   (t = lifetime/density)
Random:   |--?--|---?---|--?--|       (avg rate = density/lifetime, random intervals)
Burst:    ||||                        (all at once on triggerBurst())
```
