# Data Model: Innexus M6 -- Creative Extensions

**Branch**: `120-creative-extensions` | **Date**: 2026-03-05

## Entities

### 1. Pure Harmonic Reference (Cross-Synthesis)

**Location**: Computed at `prepare()` time, stored as member in `Processor`

| Field | Type | Description |
|-------|------|-------------|
| `relativeFreqs[n]` | `float[48]` | = n (integer harmonic ratios) |
| `normalizedAmps[n]` | `float[48]` | = (1/n) / L2_norm, L2-normalized 1/n rolloff |
| `inharmonicDeviation[n]` | `float[48]` | = 0.0 (perfect harmonics) |
| `numPartials` | `int` | = 48 (always full set) |

**Validation**: L2 norm of normalizedAmps must equal 1.0 (+/- 1e-6). All relativeFreqs must be positive integers.

### 2. Stereo Pan Map (HarmonicOscillatorBank extension)

**Location**: New SoA arrays in `HarmonicOscillatorBank`

| Field | Type | Description |
|-------|------|-------------|
| `panPosition_[n]` | `alignas(32) float[48]` | Per-partial pan position [-1, +1], 0 = center |
| `panLeft_[n]` | `alignas(32) float[48]` | Pre-computed left gain = cos(angle) |
| `panRight_[n]` | `alignas(32) float[48]` | Pre-computed right gain = sin(angle) |
| `stereoSpread_` | `float` | Current spread amount [0, 1] |
| `detuneSpread_` | `float` | Current detune amount [0, 1] |
| `detuneMultiplier_[n]` | `alignas(32) float[48]` | Frequency multiplier from detune |

**Validation**: panLeft and panRight must satisfy `panLeft^2 + panRight^2 ~= 1.0` (constant power). detuneMultiplier must be in range [0.96, 1.04] (max ~720 cents = 48*15).

**State transitions**: Pan positions recalculated when `setStereoSpread()` is called (per-frame). Detune multipliers recalculated when `setDetuneSpread()` is called (per-frame).

### 3. Evolution State (EvolutionEngine)

**Location**: `plugins/innexus/src/dsp/evolution_engine.h`

| Field | Type | Description |
|-------|------|-------------|
| `enabled_` | `bool` | Master enable |
| `mode_` | `EvolutionMode` enum | Cycle, PingPong, RandomWalk |
| `speed_` | `float` | Hz [0.01, 10.0] |
| `depth_` | `float` | [0.0, 1.0] |
| `phase_` | `float` | Current position [0.0, 1.0) |
| `direction_` | `int` | +1 or -1 (PingPong mode) |
| `numWaypoints_` | `int` | Count of occupied slots |
| `waypointIndices_[8]` | `int[8]` | Indices of occupied slots |
| `rng_` | `Xorshift32` | For Random Walk mode |
| `inverseSampleRate_` | `float` | 1.0 / sampleRate |

**State transitions**:
- `Cycle`: phase += speed * inverseSampleRate; if phase >= 1.0, phase -= 1.0
- `PingPong`: phase += direction * speed * inverseSampleRate; if phase >= 1.0 or <= 0.0, direction *= -1
- `RandomWalk`: phase += rng.nextFloat() * speed * inverseSampleRate * 0.1; clamp to [0, 1]
  - The `0.1` factor limits the maximum per-sample random step to 10% of the `speed*inverseSampleRate` increment. At speed=10 Hz and SR=44100, the maximum per-sample step is `10/44100*0.1 ≈ 0.0000227`, keeping the walk smooth rather than jumping. Without this factor the walk would be identical in speed to Cycle mode but in a random direction.

**Validation**: phase must be in [0, 1). numWaypoints must be >= 0 and <= 8.

### 4. Harmonic Modulator State (HarmonicModulator)

**Location**: `plugins/innexus/src/dsp/harmonic_modulator.h`

| Field | Type | Description |
|-------|------|-------------|
| `enabled_` | `bool` | Master enable |
| `waveform_` | `ModulatorWaveform` enum | Sine, Triangle, Square, Saw, RandomSH |
| `rate_` | `float` | Hz [0.01, 20.0] |
| `depth_` | `float` | [0.0, 1.0] |
| `rangeStart_` | `int` | First partial [1, 48] |
| `rangeEnd_` | `int` | Last partial [1, 48] |
| `target_` | `ModulatorTarget` enum | Amplitude, Frequency, Pan |
| `phase_` | `float` | LFO phase [0.0, 1.0) |
| `shValue_` | `float` | Current S&H held value |
| `rng_` | `Xorshift32` | For S&H waveform |
| `inverseSampleRate_` | `float` | 1.0 / sampleRate |

**State transitions**: Phase advances per sample: `phase += rate * inverseSampleRate`. When phase >= 1.0, wraps and updates S&H value.

**Validation**: rangeStart <= rangeEnd. Both in [1, 48]. Phase in [0, 1).

### 5. Harmonic Blender State (HarmonicBlender)

**Location**: `plugins/innexus/src/dsp/harmonic_blender.h`

| Field | Type | Description |
|-------|------|-------------|
| `enabled_` | `bool` | Master enable |
| `slotWeights_[8]` | `float[8]` | Per-slot weight [0, 1] |
| `liveWeight_` | `float` | Live source weight [0, 1] |

**Validation**: Weights normalized before use. Empty slots contribute zero regardless of weight.

### 6. M6 Parameter Atomics (Processor additions)

**Location**: Added to `plugins/innexus/src/processor/processor.h`

| Field | Atomic Type | ID | Default (normalized) |
|-------|-------------|----|-----------------------|
| `timbralBlend_` | `atomic<float>` | 600 | 1.0 |
| `stereoSpread_` | `atomic<float>` | 601 | 0.0 |
| `evolutionEnable_` | `atomic<float>` | 602 | 0.0 |
| `evolutionSpeed_` | `atomic<float>` | 603 | norm(0.1) |
| `evolutionDepth_` | `atomic<float>` | 604 | 0.5 |
| `evolutionMode_` | `atomic<float>` | 605 | 0.0 |
| `mod1Enable_` | `atomic<float>` | 610 | 0.0 |
| `mod1Waveform_` | `atomic<float>` | 611 | 0.0 |
| `mod1Rate_` | `atomic<float>` | 612 | norm(1.0) |
| `mod1Depth_` | `atomic<float>` | 613 | 0.0 |
| `mod1RangeStart_` | `atomic<float>` | 614 | 0.0 |
| `mod1RangeEnd_` | `atomic<float>` | 615 | 1.0 |
| `mod1Target_` | `atomic<float>` | 616 | 0.0 |
| `mod2Enable_` | `atomic<float>` | 620 | 0.0 |
| `mod2Waveform_` | `atomic<float>` | 621 | 0.0 |
| `mod2Rate_` | `atomic<float>` | 622 | norm(1.0) |
| `mod2Depth_` | `atomic<float>` | 623 | 0.0 |
| `mod2RangeStart_` | `atomic<float>` | 624 | 0.0 |
| `mod2RangeEnd_` | `atomic<float>` | 625 | 1.0 |

> **Note on Range Start/End atomics**: `mod1RangeStart_`, `mod1RangeEnd_`, `mod2RangeStart_`, `mod2RangeEnd_` store VST3-normalized values [0.0, 1.0]. In `processParameterChanges()`, denormalize to integer partial index [1, 48]: `rangeStart = 1 + static_cast<int>(roundf(normalizedValue * 47.0f))`. Default 0.0 maps to partial 1; default 1.0 maps to partial 48.
| `mod2Target_` | `atomic<float>` | 626 | 0.0 |
| `detuneSpread_` | `atomic<float>` | 630 | 0.0 |
| `blendEnable_` | `atomic<float>` | 640 | 0.0 |
| `blendSlotWeight1_` through `8_` | `atomic<float>` | 641-648 | 0.0 |
| `blendLiveWeight_` | `atomic<float>` | 649 | 0.0 |

### 7. M6 Smoothers (Processor additions)

| Smoother | Smoothing Time | Purpose |
|----------|---------------|---------|
| `timbralBlendSmoother_` | 5ms | FR-005 |
| `stereoSpreadSmoother_` | 10ms | FR-011 |
| `evolutionSpeedSmoother_` | 5ms | FR-023 |
| `evolutionDepthSmoother_` | 5ms | FR-023 |
| `mod1RateSmoother_` | 5ms | FR-033 |
| `mod1DepthSmoother_` | 5ms | FR-033 |
| `mod2RateSmoother_` | 5ms | FR-033 |
| `mod2DepthSmoother_` | 5ms | FR-033 |
| `detuneSpreadSmoother_` | 5ms | FR-033 |
| `blendWeightSmoother_[9]` | 5ms | FR-041 (8 slots + 1 live) |

## Relationships

```
Processor
  |-- owns --> HarmonicOscillatorBank (extended with stereo + detune)
  |-- owns --> EvolutionEngine (plugin-local)
  |-- owns --> HarmonicModulator x2 (plugin-local)
  |-- owns --> HarmonicBlender (plugin-local)
  |-- owns --> MemorySlot[8] (existing, reused as evolution waypoints + blend sources)
  |-- owns --> Pure harmonic reference snapshot (pre-computed)
  |-- owns --> 31 new parameter atomics + 19 smoothers
  |
  Pipeline per frame:
  |
  MemorySlot[8] + live frame
    |-- input to --> HarmonicBlender (if enabled, FR-040)
    |-- OR input to --> Cross-Synthesis blend (timbralBlend, FR-001-002)
    |-- OR input to --> EvolutionEngine (if enabled and blend off, FR-022)
    |
    v
  Harmonic Filter (existing, FR-049 step 3)
    |
    v
  HarmonicModulator 1 & 2 (amplitude/freq/pan, FR-049 step 4)
    |
    v
  HarmonicOscillatorBank.processStereo() (with stereo spread + detune, FR-049 step 5)
    |
    v
  Stereo output + mono residual center-panned
```

## State Persistence Layout (v6)

```
[version: int32 = 6]
[bypass: float]
[masterGain: float]
[releaseTimeMs: float (normalized)]
[inharmonicityAmount: float (normalized)]
[harmonicLevel: float (normalized)]
[residualLevel: float (normalized)]
[residualBrightness: float (normalized)]
[transientEmphasis: float (normalized)]
[inputSource: float (normalized)]
[latencyMode: float (normalized)]
[loadedFilePath: int32 length + chars]
[freeze: float]
[morphPosition: float]
[harmonicFilterType: float]
[responsiveness: float]
[memorySlot: float]
[memorySlots: 8 x (bool occupied + HarmonicSnapshot blob)]
--- v6 additions below ---
[timbralBlend: float]
[stereoSpread: float]
[evolutionEnable: float]
[evolutionSpeed: float]
[evolutionDepth: float]
[evolutionMode: float]
[mod1Enable: float]
[mod1Waveform: float]
[mod1Rate: float]
[mod1Depth: float]
[mod1RangeStart: float]
[mod1RangeEnd: float]
[mod1Target: float]
[mod2Enable: float]
[mod2Waveform: float]
[mod2Rate: float]
[mod2Depth: float]
[mod2RangeStart: float]
[mod2RangeEnd: float]
[mod2Target: float]
[detuneSpread: float]
[blendEnable: float]
[blendSlotWeight1..8: 8 x float]
[blendLiveWeight: float]
```

When loading v5 state, all v6 fields default to their initial values (timbralBlend=1.0, all others=0.0 or as specified in parameter table).
