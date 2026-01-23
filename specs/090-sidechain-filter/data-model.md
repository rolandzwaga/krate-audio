# Data Model: Sidechain Filter Processor

**Feature**: 090-sidechain-filter
**Date**: 2026-01-23

## Overview

The SidechainFilter processor dynamically controls a filter's cutoff frequency based on a sidechain signal's amplitude envelope. This document describes the state, parameters, and data flow.

---

## Entities

### SidechainFilterState (Enum)

State machine states for hold behavior.

| Value | Name | Description |
|-------|------|-------------|
| 0 | Idle | Sidechain below threshold, filter at resting position |
| 1 | Active | Sidechain above threshold, envelope controlling filter |
| 2 | Holding | Sidechain dropped below threshold, in hold period |

**State Transitions**:
```
Idle -> Active:   envelope_dB > threshold
Active -> Holding: envelope_dB <= threshold AND holdTime > 0
Active -> Idle:   envelope_dB <= threshold AND holdTime == 0
Holding -> Active: envelope_dB > threshold (re-trigger)
Holding -> Idle:  holdTimer expires
```

---

### Direction (Enum)

Envelope-to-cutoff mapping direction.

| Value | Name | Resting Position | Active Behavior |
|-------|------|------------------|-----------------|
| 0 | Up | minCutoff | Louder -> higher cutoff |
| 1 | Down | maxCutoff | Louder -> lower cutoff |

---

### FilterType (Enum)

Filter response type (maps to SVFMode).

| Value | Name | SVFMode Mapping |
|-------|------|-----------------|
| 0 | Lowpass | SVFMode::Lowpass |
| 1 | Bandpass | SVFMode::Bandpass |
| 2 | Highpass | SVFMode::Highpass |

---

## Parameters

### Sidechain Detection

| Parameter | Type | Range | Default | Unit | Description |
|-----------|------|-------|---------|------|-------------|
| attackMs_ | float | [0.1, 500] | 10.0 | ms | Envelope attack time |
| releaseMs_ | float | [1, 5000] | 100.0 | ms | Envelope release time |
| thresholdDb_ | float | [-60, 0] | -30.0 | dB | Trigger threshold |
| sensitivityDb_ | float | [-24, +24] | 0.0 | dB | Sidechain pre-gain |
| sensitivityGain_ | float | derived | 1.0 | linear | Computed from sensitivityDb_ |

### Filter Response

| Parameter | Type | Range | Default | Unit | Description |
|-----------|------|-------|---------|------|-------------|
| direction_ | Direction | Up/Down | Down | enum | Envelope mapping direction |
| filterType_ | FilterType | LP/BP/HP | Lowpass | enum | Filter response type |
| minCutoffHz_ | float | [20, maxCutoff-1] | 200.0 | Hz | Minimum cutoff frequency |
| maxCutoffHz_ | float | [minCutoff+1, Nyquist*0.45] | 2000.0 | Hz | Maximum cutoff frequency |
| resonance_ | float | [0.5, 20.0] | 8.0 | Q | Filter resonance |

### Timing

| Parameter | Type | Range | Default | Unit | Description |
|-----------|------|-------|---------|------|-------------|
| lookaheadMs_ | float | [0, 50] | 0.0 | ms | Audio delay for anticipation |
| lookaheadSamples_ | size_t | derived | 0 | samples | Computed from lookaheadMs_ |
| holdMs_ | float | [0, 1000] | 0.0 | ms | Hold time before release |
| holdSamplesTotal_ | size_t | derived | 0 | samples | Computed from holdMs_ |

### Sidechain Filter

| Parameter | Type | Range | Default | Unit | Description |
|-----------|------|-------|---------|------|-------------|
| sidechainHpEnabled_ | bool | true/false | false | - | Enable sidechain HP filter |
| sidechainHpCutoffHz_ | float | [20, 500] | 80.0 | Hz | Sidechain filter cutoff |

---

## State Variables

### Processing State

| Variable | Type | Initial | Description |
|----------|------|---------|-------------|
| state_ | SidechainFilterState | Idle | Current state machine state |
| holdSamplesRemaining_ | size_t | 0 | Remaining samples in hold |
| activeEnvelope_ | float | 0.0 | Tracked envelope during Active/Holding |

### Monitoring State

| Variable | Type | Initial | Description |
|----------|------|---------|-------------|
| currentCutoff_ | float | 200.0 | Current filter cutoff (for UI) |
| currentEnvelope_ | float | 0.0 | Current envelope value (for UI) |

### Lifecycle State

| Variable | Type | Initial | Description |
|----------|------|---------|-------------|
| sampleRate_ | double | 44100.0 | Current sample rate |
| prepared_ | bool | false | Whether prepare() has been called |
| maxCutoffLimit_ | float | 20000.0 | Nyquist-safe max cutoff |

---

## Composed Components

| Component | Type | Purpose |
|-----------|------|---------|
| envFollower_ | EnvelopeFollower | Sidechain amplitude detection |
| filter_ | SVF | Main audio filter |
| lookaheadDelay_ | DelayLine | Audio lookahead buffer |
| sidechainHpFilter_ | Biquad | Sidechain highpass filter |
| cutoffSmoother_ | OnePoleSmoother | Optional cutoff smoothing |

---

## Data Flow

### External Sidechain Mode

```
sidechain_input -> [sidechainHpFilter] -> [sensitivityGain] -> [envFollower]
                                                                     |
                                                                     v
                                                          envelope (linear)
                                                                     |
                                                                     v
                                                          [gainToDb] -> envelope_dB
                                                                     |
                                                                     v
                                                          [state machine] -> effective_envelope
                                                                     |
                                                                     v
                                                          [mapEnvelopeToCutoff] -> cutoff_Hz
                                                                     |
                                                                     v
main_input -> [lookaheadDelay] -> [SVF (cutoff)] -> output
```

### Self-Sidechain Mode with Lookahead

```
input -> [sidechainHpFilter] -> [sensitivityGain] -> [envFollower] -> envelope
   |                                                                      |
   |                                                                      v
   |                                                          [state machine + mapping]
   |                                                                      |
   v                                                                      v
[write to delay] -> [read delayed] -> [SVF (cutoff)] -> output
```

**Key Point**: In self-sidechain mode, the sidechain path sees the UNDELAYED input while the audio path is delayed. This creates anticipatory filter response.

---

## Cutoff Mapping Formula

### Log-Space Interpolation (FR-012)

```
cutoff = exp(lerp(log(minCutoff), log(maxCutoff), t))

where:
  t = envelope            (Direction::Up)
  t = 1 - envelope        (Direction::Down)
  envelope in [0, 1]
```

Equivalent forms:
```
cutoff = minCutoff * pow(maxCutoff / minCutoff, t)
cutoff = minCutoff * exp(t * log(maxCutoff / minCutoff))
```

### Perceptual Linearity

Equal envelope changes produce equal octave changes:
- envelope 0.0 -> 0.5: covers half the frequency range in octaves
- envelope 0.5 -> 1.0: covers the other half

Example with minCutoff=200Hz, maxCutoff=3200Hz (4 octaves):
- envelope 0.0: 200 Hz
- envelope 0.25: 400 Hz (+1 octave)
- envelope 0.5: 800 Hz (+1 octave)
- envelope 0.75: 1600 Hz (+1 octave)
- envelope 1.0: 3200 Hz (+1 octave)

---

## Threshold Comparison (FR-005)

```cpp
// Convert linear envelope to dB
float envelopeDb = gainToDb(envelope);  // Returns -144 if envelope <= 0

// Compare in dB domain
bool aboveThreshold = envelopeDb > thresholdDb_;
```

The threshold parameter is in dB, and comparison happens in dB domain for intuitive behavior matching standard dynamics processors.

---

## Latency

| Condition | Latency |
|-----------|---------|
| lookahead = 0 | 0 samples |
| lookahead > 0 | lookaheadSamples_ samples |

Latency is reported via `getLatency()` and equals the lookahead delay in samples.

---

## Validation Rules

| Parameter | Validation |
|-----------|------------|
| minCutoff | >= 20 Hz, < maxCutoff |
| maxCutoff | > minCutoff, <= sampleRate * 0.45 |
| resonance | [0.5, 20.0] |
| threshold | [-60, 0] dB |
| sensitivity | [-24, +24] dB |
| attack | [0.1, 500] ms |
| release | [1, 5000] ms |
| hold | [0, 1000] ms |
| lookahead | [0, 50] ms |
| sidechainHpCutoff | [20, 500] Hz |

---

## Edge Cases

| Case | Behavior |
|------|----------|
| minCutoff == maxCutoff | Static filter, no sweep |
| holdMs == 0 | Direct release, no hold phase |
| lookaheadMs == 0 | No latency, no audio delay |
| Silent sidechain | Filter at resting position (direction-dependent) |
| NaN/Inf main input | Return 0, reset filter state |
| NaN/Inf sidechain | Treat as silent (envelope = 0) |
