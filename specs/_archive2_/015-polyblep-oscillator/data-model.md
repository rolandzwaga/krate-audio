# Data Model: PolyBLEP Oscillator

**Branch**: `015-polyblep-oscillator` | **Date**: 2026-02-03

## Terminology Glossary

| Term | Context | Meaning |
|------|---------|---------|
| **PolyBLEP** | Algorithm name (prose, specs, docs) | Polynomial Band-Limited Step — the anti-aliasing algorithm. All-caps "BLEP" in prose. |
| **PolyBlep** | Code symbol (class/function names) | PascalCase form used in C++ identifiers: `PolyBlepOscillator`, `polyBlep()`. |
| **phase wrap** | Event (noun) | The event when the oscillator's phase crosses from near-1.0 to near-0.0, starting a new cycle. |
| **phase wrapped** | State (past-tense adjective) | Boolean condition: whether a phase wrap occurred on the most recent `process()` call. |
| **phaseWrapped()** | Method name | The accessor method returning the "phase wrapped" boolean state. |
| **prepare** | Lifecycle method | Initializes oscillator for a sample rate. **NOT real-time safe** (may allocate or compute). |
| **reset** | Lifecycle method | Clears runtime state (phase, integrator). **IS real-time safe** (simple assignments only). |

## Entities

### OscWaveform (Enumeration)

**Location**: `dsp/include/krate/dsp/primitives/polyblep_oscillator.h`
**Namespace**: `Krate::DSP` (file scope, NOT nested in class)
**Underlying type**: `uint8_t`

| Value | Name | Integer | Description |
|-------|------|---------|-------------|
| Sine | `OscWaveform::Sine` | 0 | Pure sine wave, no PolyBLEP |
| Sawtooth | `OscWaveform::Sawtooth` | 1 | PolyBLEP at phase wrap |
| Square | `OscWaveform::Square` | 2 | PolyBLEP at both edges |
| Pulse | `OscWaveform::Pulse` | 3 | PolyBLEP with variable PW |
| Triangle | `OscWaveform::Triangle` | 4 | Integrated PolyBLEP square |

**Relationships**: Used by `PolyBlepOscillator::setWaveform()`. Will be shared by downstream components (SyncOscillator, SubOscillator, UnisonEngine).

**Validation**: Values 0-4 only. Out-of-range values are undefined behavior (debug assertion).

---

### PolyBlepOscillator (Class)

**Location**: `dsp/include/krate/dsp/primitives/polyblep_oscillator.h`
**Layer**: 1 (primitives/)
**Namespace**: `Krate::DSP`
**Dependencies**: Layer 0 only (`core/polyblep.h`, `core/phase_utils.h`, `core/math_constants.h`, `core/db_utils.h`)

#### Member Variables (Internal State)

Listed in cache-friendly order (hot-path data first):

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `phaseAcc_` | `PhaseAccumulator` | `{0.0, 0.0}` | Phase state (phase + increment) |
| `dt_` | `float` | `0.0f` | Cached phase increment as float (for PolyBLEP) |
| `sampleRate_` | `float` | `0.0f` | Current sample rate |
| `frequency_` | `float` | `440.0f` | Base frequency in Hz |
| `pulseWidth_` | `float` | `0.5f` | Pulse width [0.01, 0.99] |
| `integrator_` | `float` | `0.0f` | Leaky integrator state (Triangle) |
| `fmOffset_` | `float` | `0.0f` | FM offset in Hz (reset each sample) |
| `pmOffset_` | `float` | `0.0f` | PM offset in radians (reset each sample) |
| `waveform_` | `OscWaveform` | `Sine` | Active waveform |
| `phaseWrapped_` | `bool` | `false` | Phase wrapped on last process() |

#### State Transitions

```
                 prepare(sr)
    [Uninitialized] ---------> [Ready]
                                  |
                    reset()       |  process() / processBlock()
                   <-----------  [Processing]
                                  |
                   setFrequency() | setWaveform() | setPulseWidth()
                   setFM() | setPM() | resetPhase()
                                  |
                              [Processing] (parameters take effect on next process())
```

**Lifecycle**:
1. Construct (default state, sampleRate = 0)
2. `prepare(sampleRate)` -- initializes for given sample rate, resets all state
3. `setFrequency()`, `setWaveform()`, etc. -- configure parameters
4. `process()` / `processBlock()` -- generate output
5. `reset()` -- clear phase/integrator state, preserve configuration

#### Validation Rules

| Parameter | Range | Clamping | Notes |
|-----------|-------|----------|-------|
| `frequency` | [0, sampleRate/2) | Silent clamp | FR-006 |
| `pulseWidth` | [0.01, 0.99] | Silent clamp | FR-008 |
| `pmOffset` | any float | No clamp, converted by /kTwoPi | FR-020 |
| `fmOffset` | any float | Effective freq clamped to [0, sr/2) | FR-021 |
| `newPhase` (resetPhase) | any double | Wrapped to [0, 1) | FR-019 |

---

## Processing Flow

### Per-Sample Processing (`process()`)

```
1. Compute effective frequency: effectiveFreq = clamp(frequency_ + fmOffset_, 0, sr/2)
2. Compute dt: dt = effectiveFreq / sampleRate_
3. Compute effective phase (with PM): effectivePhase = wrapPhase(phase + pmOffset / kTwoPi)
4. Generate waveform output based on current waveform type:
   - Sine: sin(kTwoPi * effectivePhase)
   - Sawtooth: (2 * effectivePhase - 1) - polyBlep(effectivePhase, dt)
   - Square: (effectivePhase < 0.5 ? 1 : -1) - polyBlep(effectivePhase, dt) + polyBlep(wrap(effectivePhase + 0.5), dt)
   - Pulse: (effectivePhase < pw ? 1 : -1) - polyBlep(effectivePhase, dt) + polyBlep(wrap(effectivePhase + 1.0 - pw), dt)
   - Triangle: leakyIntegrate(squareBLEP) with leak = 1 - 4*effectiveFreq/sr
5. Advance phase: phaseWrapped_ = phaseAcc_.advance() using dt for this sample
6. Reset modulation offsets: fmOffset_ = 0, pmOffset_ = 0
7. Sanitize output (branchless NaN/range check)
8. Return sanitized output
```

**Important ordering note**: The phase is read BEFORE advance for waveform generation, then advanced for the next sample. This means `phase()` after `process()` returns the phase for the NEXT sample, and `phaseWrapped()` indicates whether THIS sample crossed the boundary.

### Block Processing (`processBlock()`)

```
for i in 0..numSamples:
    output[i] = process()
```

Identical to calling `process()` N times (FR-010, SC-008).

---

## Dependency API Contracts

### From `core/polyblep.h`

```cpp
[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept;
// t: normalized phase [0, 1)
// dt: normalized phase increment (frequency / sampleRate), must be < 0.5
// Returns: correction to subtract from naive waveform
```

### From `core/phase_utils.h`

```cpp
struct PhaseAccumulator {
    double phase = 0.0;
    double increment = 0.0;
    [[nodiscard]] bool advance() noexcept;  // Returns true on wrap
    void reset() noexcept;                   // Resets phase to 0
    void setFrequency(float frequency, float sampleRate) noexcept;
};

[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept;
[[nodiscard]] constexpr double wrapPhase(double phase) noexcept;
```

### From `core/math_constants.h`

```cpp
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
```

### From `core/db_utils.h` (detail namespace)

```cpp
namespace detail {
    constexpr bool isNaN(float x) noexcept;
    [[nodiscard]] constexpr bool isInf(float x) noexcept;
}
```
