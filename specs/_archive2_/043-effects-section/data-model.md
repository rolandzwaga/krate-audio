# Data Model: Ruinae Effects Section

**Feature**: 043-effects-section | **Date**: 2026-02-08

## Entities

### E-001: RuinaeDelayType (Enum)

**Location**: `dsp/include/krate/dsp/systems/ruinae_types.h`

```cpp
enum class RuinaeDelayType : uint8_t {
    Digital = 0,    // DigitalDelay (pristine, 80s, lo-fi)
    Tape = 1,       // TapeDelay (motor inertia, heads, wear)
    PingPong = 2,   // PingPongDelay (alternating L/R)
    Granular = 3,   // GranularDelay (grain-based)
    Spectral = 4,   // SpectralDelay (FFT per-bin)
    NumTypes = 5    // Sentinel
};
```

**Validation**: Value must be < NumTypes. Default = Digital.

### E-002: RuinaeEffectsChain (Class)

**Location**: `dsp/include/krate/dsp/systems/ruinae_effects_chain.h`

#### Fields (Member Variables)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sampleRate_` | `double` | `44100.0` | Current sample rate |
| `maxBlockSize_` | `size_t` | `512` | Maximum block size |
| `prepared_` | `bool` | `false` | Whether prepare() has been called |
| `tempoBPM_` | `double` | `120.0` | Current tempo for delay types |
| | | | |
| **Freeze Slot** | | | |
| `freeze_` | `FreezeMode` | -- | Spectral freeze effect instance |
| `freezeEnabled_` | `bool` | `false` | Whether freeze slot is active |
| | | | |
| **Delay Slot** | | | |
| `digitalDelay_` | `DigitalDelay` | -- | Digital delay instance |
| `tapeDelay_` | `TapeDelay` | -- | Tape delay instance |
| `pingPongDelay_` | `PingPongDelay` | -- | Ping-pong delay instance |
| `granularDelay_` | `GranularDelay` | -- | Granular delay instance |
| `spectralDelay_` | `SpectralDelay` | -- | Spectral delay instance |
| | | | |
| **Crossfade State** | | | |
| `activeDelayType_` | `RuinaeDelayType` | `Digital` | Currently active delay type |
| `incomingDelayType_` | `RuinaeDelayType` | `Digital` | Incoming delay during crossfade |
| `crossfading_` | `bool` | `false` | Whether a crossfade is in progress |
| `crossfadeAlpha_` | `float` | `0.0f` | Crossfade position [0, 1] |
| `crossfadeIncrement_` | `float` | `0.0f` | Per-sample alpha increment |
| | | | |
| **Latency Compensation** | | | |
| `targetLatencySamples_` | `size_t` | `0` | Spectral delay's FFT latency |
| `compDelayL_[4]` | `DelayLine[4]` | -- | L compensation delays (Digital, Tape, PingPong, Granular) |
| `compDelayR_[4]` | `DelayLine[4]` | -- | R compensation delays |
| | | | |
| **Reverb Slot** | | | |
| `reverb_` | `Reverb` | -- | Dattorro plate reverb instance |
| | | | |
| **Temporary Buffers** | | | |
| `tempL_` | `std::vector<float>` | -- | Temp buffer L (crossfade mixing + granular normalization) |
| `tempR_` | `std::vector<float>` | -- | Temp buffer R |
| `crossfadeOutL_` | `std::vector<float>` | -- | Outgoing delay output L during crossfade |
| `crossfadeOutR_` | `std::vector<float>` | -- | Outgoing delay output R during crossfade |

#### Relationships

```
RuinaeEffectsChain
  |-- owns 1 FreezeMode (freeze slot)
  |-- owns 5 delay types (Digital, Tape, PingPong, Granular, Spectral)
  |-- owns 1 Reverb (reverb slot)
  |-- owns 4 pairs of DelayLine (latency compensation for non-spectral delays)
  |-- references BlockContext (constructed per-block from tempoBPM_)
```

#### State Transitions

**Crossfade State Machine**:
```
           setDelayType(same)
  [Idle] ----------------------> [Idle] (no-op)
    |
    | setDelayType(new)
    v
  [Crossfading] ---(alpha >= 1.0)---> [Idle]
    |                                    ^
    | setDelayType(X)                    |
    | (fast-track: snap alpha=1,         |
    |  reset outgoing, start new)        |
    +------------------------------------+
```

**Freeze Slot State**:
```
  [Disabled] --setFreezeEnabled(true)--> [Enabled, Live]
       ^                                      |
       |                                setFreeze(true)
  setFreezeEnabled(false)                     |
       |                                      v
       +--- setFreezeEnabled(false) ---[Enabled, Frozen]
                                              |
                                        setFreeze(false)
                                              |
                                              v
                                       [Enabled, Live]
```

## Processing Flow

### Signal Flow (FR-005)

```
Input (left, right)
    |
    v
[1. Freeze Slot]
    |-- if freezeEnabled_: FreezeMode::process(left, right, n, ctx)
    |-- else: pass-through (no processing)
    |
    v
[2. Delay Slot]
    |-- Process active delay type -> left, right
    |-- if crossfading_: also process incoming delay type -> tempL, tempR
    |   blend: output = active * (1-alpha) + incoming * alpha
    |-- Apply latency compensation (non-spectral types only)
    |
    v
[3. Reverb Slot]
    |-- Reverb::processBlock(left, right, n)
    |
    v
Output (left, right)
```

### Delay Processing Detail

For each delay type, the chain must:
1. Forward common parameters (time, feedback, mix) via type-specific setters
2. Process audio through the type-specific API
3. Apply latency compensation if the type is non-spectral

```
processDelay(type, left, right, numSamples, ctx):
  switch (type):
    Digital:
      digitalDelay_.process(left, right, numSamples, ctx)
      compensateLatency(0, left, right, numSamples)  // index 0
    Tape:
      tapeDelay_.process(left, right, numSamples)     // NO ctx
      compensateLatency(1, left, right, numSamples)
    PingPong:
      pingPongDelay_.process(left, right, numSamples, ctx)
      compensateLatency(2, left, right, numSamples)
    Granular:
      copy left/right to tempL_/tempR_
      granularDelay_.process(tempL_, tempR_, left, right, numSamples, ctx)
      compensateLatency(3, left, right, numSamples)
    Spectral:
      spectralDelay_.process(left, right, numSamples, ctx)
      // NO compensation needed (latency is intrinsic)
```

### Latency Compensation Detail

```
compensateLatency(delayIndex, left, right, numSamples):
  if targetLatencySamples_ == 0: return  // No spectral delay prepared
  for i in 0..numSamples:
    compDelayL_[delayIndex].write(left[i])
    compDelayR_[delayIndex].write(right[i])
    left[i] = compDelayL_[delayIndex].read(targetLatencySamples_)
    right[i] = compDelayR_[delayIndex].read(targetLatencySamples_)
```

## Parameter Forwarding Map

### Common Delay Parameters -> Per-Type API

| Chain Method | Digital | Tape | PingPong | Granular | Spectral |
|-------------|---------|------|----------|----------|----------|
| `setDelayTime(ms)` | `setTime(ms)` | `setMotorSpeed(ms)` | `setDelayTimeMs(ms)` | `setDelayTime(ms)` | `setBaseDelayMs(ms)` |
| `setDelayFeedback(amt)` | `setFeedback(amt)` | `setFeedback(amt)` | `setFeedback(amt)` | `setFeedback(amt)` | `setFeedback(amt)` |
| `setDelayMix(mix)` | `setMix(mix)` | `setMix(mix)` | `setMix(mix)` | `setDryWet(mix)` | `setDryWetMix(mix)` |

### Freeze Parameters

| Chain Method | FreezeMode Method | Unit Conversion |
|-------------|-------------------|-----------------|
| `setFreezeEnabled(bool)` | managed externally (bypass) | none |
| `setFreeze(bool)` | `setFreezeEnabled(bool)` | none |
| `setFreezePitchSemitones(f)` | `setPitchSemitones(f)` | none |
| `setFreezeShimmerMix(f)` | `setShimmerMix(f)` | none (both 0-1) |
| `setFreezeDecay(f)` | `setDecay(f)` | none (both 0-1) |

### Reverb Parameters

| Chain Method | Reverb Method | Notes |
|-------------|---------------|-------|
| `setReverbParams(params)` | `setParams(params)` | Direct forwarding of ReverbParams struct |
