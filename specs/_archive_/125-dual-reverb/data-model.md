# Data Model: Dual Reverb System

**Date**: 2026-03-11 | **Spec**: specs/125-dual-reverb/spec.md

## Entities

### 1. ReverbParams (EXISTING - no changes)

**Location**: `dsp/include/krate/dsp/effects/reverb.h`
**Layer**: 4

Shared parameter structure used by both reverb types. No modifications needed.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| roomSize | float | [0.0, 1.0] | 0.5 | Decay control |
| damping | float | [0.0, 1.0] | 0.5 | HF absorption |
| width | float | [0.0, 1.0] | 1.0 | Stereo decorrelation |
| mix | float | [0.0, 1.0] | 0.3 | Dry/wet blend |
| preDelayMs | float | [0.0, 100.0] | 0.0 | Pre-delay in ms |
| diffusion | float | [0.0, 1.0] | 0.7 | Input diffusion amount |
| freeze | bool | | false | Infinite sustain mode |
| modRate | float | [0.0, 2.0] | 0.5 | LFO rate in Hz |
| modDepth | float | [0.0, 1.0] | 0.0 | LFO depth |

### 2. Reverb (MODIFIED - Dattorro optimization)

**Location**: `dsp/include/krate/dsp/effects/reverb.h`
**Layer**: 4

Optimized in-place. Public API preserved (`prepare`, `reset`, `setParams`, `process`, `processBlock`).

**Internal changes**:

| Change | Current | Optimized |
|--------|---------|-----------|
| LFO | `std::sin(phase_)` / `std::cos(phase_)` per sample | Gordon-Smith phasor (sinState_, cosState_, epsilon_) |
| Parameter smoothing | 9x `smoother.process()` per sample | Block-rate: process once per 16-sample sub-block |
| Filter updates | `setCutoff()` per sample | Block-rate: every 16 samples |
| Diffusion coeff | `setCoefficient()` per sample | Block-rate: every 16 samples |
| Delay buffers | 13 separate allocations | Single contiguous buffer managed by a private `ContiguousDelayBuffer` helper class within Reverb — NOT by modifying the `DelayLine` Layer 1 API. Each section uses power-of-2 sizing for masking efficiency. See research.md R6. |
| Denormal flushing | `flushDenormal()` on tank outputs | Removed (FTZ/DAZ mode assumed) |

**New members**:

| Member | Type | Description |
|--------|------|-------------|
| sinState_ | float | Gordon-Smith phasor sine state |
| cosState_ | float | Gordon-Smith phasor cosine state |
| lfoEpsilon_ | float | Gordon-Smith rotation coefficient |
| contiguousBuffer_ | std::vector<float> | Single allocation for all 13 delay lines |
| totalBufferSize_ | size_t | Total contiguous buffer size (test-verifiable) |

**Removed members**:

| Member | Reason |
|--------|--------|
| lfoPhase_ | Replaced by sinState_/cosState_ |
| lfoPhaseIncrement_ | Replaced by lfoEpsilon_ |

### 3. FDNReverb (NEW)

**Location**: `dsp/include/krate/dsp/effects/fdn_reverb.h`
**Layer**: 4
**Namespace**: `Krate::DSP`

8-channel feedback delay network reverb with SIMD acceleration.

**Public API** (matches Reverb pattern):

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate) noexcept` | Allocate buffers, init filters |
| reset | `void reset() noexcept` | Clear all state |
| setParams | `void setParams(const ReverbParams& params) noexcept` | Update parameters |
| process | `void process(float& left, float& right) noexcept` | Process one stereo sample |
| processBlock | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | Process a block |
| isPrepared | `bool isPrepared() const noexcept` | Query state |

**Internal architecture**:

```
Input (stereo) -> Mono sum -> Pre-delay -> Diffuser -> Feedback Network -> Stereo output
                                              |               |
                                        3-4 Hadamard     Householder
                                         stages        + 8 delay lines
                                                       + 8 damping filters
                                                       + 8 DC blockers
                                                       + 4 modulated LFOs
```

**SoA state arrays** (aligned for SIMD):

| Array | Type | Size | Description |
|-------|------|------|-------------|
| delayOutputs_ | alignas(32) float[8] | 8 | Current delay line outputs |
| filterStates_ | alignas(32) float[8] | 8 | One-pole LP filter states |
| filterCoeffs_ | alignas(32) float[8] | 8 | One-pole LP coefficients. Mapped from `damping` param: damping=0 → cutoff 20kHz, damping=1 → cutoff 200Hz (same formula as Dattorro) |
| dcBlockX_ | alignas(32) float[8] | 8 | DC blocker input state |
| dcBlockY_ | alignas(32) float[8] | 8 | DC blocker output state |
| feedbackGains_ | alignas(32) float[8] | 8 | Per-channel feedback gain (decay) |

**Delay line storage**:

| Member | Type | Description |
|--------|------|-------------|
| delayBuffers_ | std::vector<float> | Contiguous buffer for all 8 delay lines |
| delayLengths_ | size_t[8] | Current delay lengths in samples |
| delayMaxLengths_ | size_t[8] | Maximum delay lengths (for allocation) |
| delayWritePos_ | size_t[8] | Per-channel write positions |

**Diffuser state**:

| Member | Type | Description |
|--------|------|-------------|
| diffuserBuffers_ | std::vector<float> | Contiguous buffer for diffuser delays |
| diffuserSteps_ | size_t | Fixed at `kNumDiffuserSteps = 4` diffusion steps (not runtime-variable) |

**LFO state** (4 modulated channels):

| Member | Type | Description |
|--------|------|-------------|
| lfoSinState_ | float[4] | Gordon-Smith sine states |
| lfoCosState_ | float[4] | Gordon-Smith cosine states |
| lfoEpsilon_ | float | Shared rotation coefficient |
| lfoModChannels_ | size_t[4] | Indices of the 4 longest delay lines |
| lfoMaxExcursion_ | float | Max modulation depth in samples |

**Pre-delay**:

| Member | Type | Description |
|--------|------|-------------|
| preDelay_ | DelayLine | Pre-delay line (reuse Layer 1 primitive) |

**Reference delay lengths at 48kHz**:

| Index | Samples | Time (ms) | Modulated? |
|-------|---------|-----------|------------|
| 0 | 149 | 3.1 | No |
| 1 | 189 | 3.9 | No |
| 2 | 240 | 5.0 | No |
| 3 | 305 | 6.4 | No |
| 4 | 387 | 8.1 | Yes (longest 4) |
| 5 | 492 | 10.3 | Yes |
| 6 | 625 | 13.0 | Yes |
| 7 | 794 | 16.5 | Yes |

### 4. RuinaeReverbParams (MODIFIED)

**Location**: `plugins/ruinae/src/parameters/reverb_params.h`

Add reverb type field:

| New Field | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| reverbType | std::atomic<int32_t> | 0-1 | 0 | 0=Plate, 1=Hall/FDN |

### 5. RuinaeEffectsChain (MODIFIED)

**Location**: `plugins/ruinae/src/engine/ruinae_effects_chain.h`

**New members**:

| Member | Type | Description |
|--------|------|-------------|
| fdnReverb_ | FDNReverb | FDN reverb instance |
| activeReverbType_ | int | Currently active reverb (0=Plate, 1=Hall) |
| incomingReverbType_ | int | Target reverb during crossfade |
| reverbCrossfading_ | bool | Crossfade in progress |
| reverbCrossfadeAlpha_ | float | Crossfade position [0, 1] |
| reverbCrossfadeIncrement_ | float | Per-sample increment |

**New methods**:

| Method | Signature | Description |
|--------|-----------|-------------|
| setReverbType | `void setReverbType(int type) noexcept` | Initiate 30ms equal-power crossfade to the new reverb type. No-op if `type == activeReverbType_` and not already crossfading. |
| setReverbTypeDirect | `void setReverbTypeDirect(int type) noexcept` | Set the active reverb type immediately without a crossfade. Used only during state load (`setState`) to restore the saved type without generating an audible transition. Sets `activeReverbType_ = type` and clears any in-progress crossfade state. |

## State Transitions

### Reverb Type Switching

```
IDLE -> CROSSFADING -> IDLE
 |          |           |
 |    Both reverbs      |
 |    process audio     |
 |    alpha: 0->1       |
 |          |           |
 |    On completion:    |
 |    reset outgoing,   |
 |    swap active type  |
 v          v           v
Only active           Only active
reverb runs           reverb runs
```

### State Serialization (Version 5)

Current state version: 4. Bump to 5. The reverb type parameter is added at the end of reverb params in the save/load order.

**Backward compatibility**: When loading version 4 states, `reverbType` defaults to 0 (Plate).

## Parameter ID Allocation

**New parameter**:

| Parameter | ID | Range |
|-----------|----|-------|
| kReverbTypeId | 1709 | 0=Plate, 1=Hall (StringListParameter) |

This fits within the existing Reverb ID range (1700-1799). Next available ID after kReverbModDepthId (1708).
