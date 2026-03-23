# Data Model: Bow Model Exciter (Spec 130)

**Date**: 2026-03-23 | **Branch**: `130-bow-model-exciter`

## Entities

### BowExciter (New - Layer 2 Processor)

**Location**: `dsp/include/krate/dsp/processors/bow_exciter.h`
**Namespace**: `Krate::DSP`

| Field | Type | Description | Default |
|-------|------|-------------|---------|
| `pressure_` | `float` | Bow pressure 0.0-1.0, maps to friction slope | 0.3f |
| `speed_` | `float` | Bow speed 0.0-1.0, scales velocity ceiling | 0.5f |
| `position_` | `float` | Bow position 0.0-1.0, 0=bridge, 1=fingerboard | 0.13f |
| `bowVelocity_` | `float` | Current integrated bow velocity | 0.0f |
| `maxVelocity_` | `float` | Velocity ceiling from MIDI velocity | 0.0f |
| `targetEnergy_` | `float` | Energy target from MIDI velocity/speed at note-on | 0.0f |
| `currentEnergy_` | `float` | Running energy estimate (EMA follower) | 0.0f |
| `energyAlpha_` | `float` | EMA coefficient for energy tracking | 0.0f |
| `hairLpf_` | `OnePoleLP` | Bow hair width LPF at ~8 kHz | -- |
| `rosinLfo_` | `LFO` | Slow drift LFO at 0.7 Hz for friction jitter | -- |
| `noiseState_` | `uint32_t` | LCG RNG state for high-freq noise jitter | -- |
| `noiseHpState_` | `float` | Highpass state for noise jitter at ~200 Hz | 0.0f |
| `noiseHpCoeff_` | `float` | Highpass coefficient for noise at ~200 Hz | 0.0f |
| `sampleRate_` | `double` | Sample rate | 0.0 |
| `prepared_` | `bool` | Whether prepare() has been called | false |
| `active_` | `bool` | Whether the exciter is currently active | false |

**Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(double sampleRate) noexcept` | Initialize internal state, LPF, LFO |
| `reset` | `void reset() noexcept` | Reset all state to initial values |
| `trigger` | `void trigger(float velocity) noexcept` | Set maxVelocity and targetEnergy from MIDI velocity |
| `release` | `void release() noexcept` | Mark exciter for release (velocity ramp-down via ADSR) |
| `process` | `float process(float feedbackVelocity) noexcept` | Compute one sample of excitation force |
| `setPressure` | `void setPressure(float p) noexcept` | Set bow pressure (0.0-1.0) |
| `setSpeed` | `void setSpeed(float s) noexcept` | Set bow speed (0.0-1.0) |
| `setPosition` | `void setPosition(float pos) noexcept` | Set bow position (0.0-1.0) |
| `setEnvelopeValue` | `void setEnvelopeValue(float env) noexcept` | Set ADSR envelope value for acceleration computation |
| `isActive` | `bool isActive() const noexcept` | Whether exciter is producing output |
| `isPrepared` | `bool isPrepared() const noexcept` | Whether prepare() has been called |

**Validation Rules**:
- Pressure clamped to [0.0, 1.0]
- Speed clamped to [0.0, 1.0]
- Position clamped to [0.0, 1.0] with impedance formula using `max(beta*(1-beta)*4.0, 0.1)` to prevent singularities at extremes

**State Transitions**:
```
Unprepared -> Prepared (via prepare())
Prepared -> Active (via trigger())
Active -> Active (sustained bowing, process() called each sample)
Active -> Releasing (via release(), ADSR drives velocity down)
Releasing -> Inactive (velocity reaches 0)
Inactive -> Active (via trigger() on new note)
```

### ImpactExciter (Modified)

**Location**: `dsp/include/krate/dsp/processors/impact_exciter.h`

**Changes**:
- `process()` signature changes from `float process() noexcept` to `float process(float feedbackVelocity) noexcept`
- `processBlock(float*, int)` internal call updated to `process(0.0f)`
- Parameter `feedbackVelocity` is accepted but ignored (documented with `[[maybe_unused]]` or named comment)

### ResidualSynthesizer (Modified)

**Location**: `dsp/include/krate/dsp/processors/residual_synthesizer.h`

**Changes**:
- `process()` signature changes from `float process() noexcept` to `float process(float feedbackVelocity) noexcept`
- Parameter `feedbackVelocity` is accepted but ignored

### ModalResonatorBank (Extended)

**Location**: `dsp/include/krate/dsp/processors/modal_resonator_bank.h`

**New Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `bowedModeFilters_` | `std::array<BowedModeBPF, 8>` | Bandpass velocity taps (Q ~50) |
| `bowedModeSumVelocity_` | `float` | Summed feedback velocity from 8 taps |
| `bowPosition_` | `float` | Bow position for harmonic weighting |
| `bowModeActive_` | `bool` | Whether bowed-mode coupling is active |

**New Methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| `setBowPosition` | `void setBowPosition(float pos) noexcept` | Set bow position, update harmonic weights |
| `setBowModeActive` | `void setBowModeActive(bool active) noexcept` | Enable/disable bowed-mode velocity taps |

**Modified Methods**:
- `processSample()` / `processSampleCore()`: When `bowModeActive_`, apply 8 bandpass filters to output and sum into `bowedModeSumVelocity_`
- `getFeedbackVelocity()`: Return `bowedModeSumVelocity_` when `bowModeActive_`
- `process(float excitation)`: When `bowModeActive_`, weight excitation input per mode using `sin((n+1) * pi * bowPosition)`

**Biquad Bandpass State (per tap)**:

| Field | Type | Description |
|-------|------|-------------|
| `b0, b1, b2` | `float` | Feedforward coefficients |
| `a1, a2` | `float` | Feedback coefficients |
| `z1, z2` | `float` | State variables |

Coefficients computed from: `fc = modeFrequency[k]`, `Q = 50`, standard biquad BPF formula.

### WaveguideString (Modified)

**Location**: `dsp/include/krate/dsp/processors/waveguide_string.h`

**Changes for Bow Junction** (FR-019, FR-021):
- DC blocker relocation: move from after loss filter to after the bow junction output (before signal re-enters delay lines). For non-bow mode, behavior is functionally identical.
- No new fields needed -- the existing `feedbackVelocity_` and split delay topology serve the bow coupling.

### InnexusVoice (Extended)

**Location**: `plugins/innexus/src/processor/innexus_voice.h`

**New Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `bowExciter` | `Krate::DSP::BowExciter` | Bow exciter instance |

### Plugin IDs (Extended)

**Location**: `plugins/innexus/src/plugin_ids.h`

**New Parameter IDs**:

| ID | Name | Value | Type | Range | Default |
|----|------|-------|------|-------|---------|
| `kBowPressureId` | Bow Pressure | TBD | RangeParameter | 0.0-1.0 | 0.3 |
| `kBowSpeedId` | Bow Speed | TBD | RangeParameter | 0.0-1.0 | 0.5 |
| `kBowPositionId` | Bow Position | TBD | RangeParameter | 0.0-1.0 | 0.13 |
| `kBowOversamplingId` | Bow Oversampling | TBD | Parameter (bool) | off/on | off |

**Note**: All `TBD` values are to be determined by inspecting `plugins/innexus/src/plugin_ids.h` for the highest existing ID at the time of T061, then assigning sequential values above it.

## Relationships

```
InnexusVoice
  |-- BowExciter (new, Layer 2)
  |     |-- OnePoleLP (reuse, Layer 1)
  |     |-- LFO (reuse, Layer 1)
  |-- ImpactExciter (modified signature, Layer 2)
  |-- ResidualSynthesizer (modified signature, Layer 2)
  |-- ModalResonatorBank (extended, Layer 2, implements IResonator)
  |     |-- 8x BiquadBP bowed-mode taps (new, internal)
  |-- WaveguideString (modified DC blocker position, Layer 2, implements IResonator)
  |-- ADSREnvelope (existing, drives bow acceleration)
```

## Layer Dependency Diagram

```
Layer 0 (core):    math_utils, dsp_utils
                      |
Layer 1 (prims):   OnePoleLP, LFO, DCBlocker, DelayLine, ADSREnvelope
                      |
Layer 2 (procs):   BowExciter (NEW), ImpactExciter, ResidualSynthesizer,
                   ModalResonatorBank, WaveguideString
                      |
Plugin layer:      InnexusVoice, Processor, Controller
```

No circular dependencies. BowExciter depends only on Layer 0/1 primitives.
