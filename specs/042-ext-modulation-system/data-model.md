# Data Model: Extended Modulation System

**Feature**: 042-ext-modulation-system | **Date**: 2026-02-08

## Entity Diagram

```
+------------------+       +------------------+       +------------------+
| VoiceModSource   |       | VoiceModDest     |       | VoiceModRoute    |
| (enum, 8 values) |------>| (enum, 9 values) |<------| source           |
|                  |       |                  |       | destination      |
| Env1 = 0         |       | FilterCutoff = 0 |       | amount [-1,+1]   |
| Env2 = 1         |       | FilterRes = 1    |       +------------------+
| Env3 = 2         |       | MorphPos = 2     |               |
| VoiceLFO = 3     |       | DistDrive = 3    |               | up to 16
| GateOutput = 4   |       | TGDepth = 4      |               v
| Velocity = 5     |       | OscAPitch = 5    |       +------------------+
| KeyTrack = 6     |       | OscBPitch = 6    |       | VoiceModRouter   |
| Aftertouch = 7   |       | OscALevel = 7    |       | routes_[16]      |
| NumSources = 8   |       | OscBLevel = 8    |       | offsets_[9]      |
+------------------+       | NumDest = 9      |       | sourceValues_[8] |
                            +------------------+       +------------------+
                                                              |
                                                              | 1 per voice
                                                              v
                            +----------------------------------+
                            | RuinaeVoice                      |
                            | - oscA_, oscB_                   |
                            | - modRouter_                     |
                            | - aftertouch_ : float [0,1]     |
                            | - velocity_ : float [0,1]       |
                            | + setAftertouch(float)           |
                            | + processBlock(float*, size_t)   |
                            +----------------------------------+

+------------------+       +------------------+       +------------------+
| ModSource        |       | ModRouting       |       | ModulationEngine |
| (enum, existing) |------>| source           |------>| routings_[32]    |
|                  |       | destParamId      |       | modOffsets_[128] |
| LFO1, LFO2      |       | amount [-1,+1]   |       | lfo1_, lfo2_     |
| EnvFollower      |       | curve            |       | chaos_           |
| Random           |       | active           |       | envFollower_     |
| Macro1-4         |       +------------------+       | macros_[4]       |
| Chaos            |                                  | amountSmoothers_ |
| SampleHold       |                                  +------------------+
| PitchFollower    |                                         |
| Transient        |                                  Composed into test
+------------------+                                  scaffold
                                                             |
+------------------+                                         v
| Rungler          |<----- registered as -----+  +------------------+
| : ModSource      |   ModulationSource       |  | TestEngineScaffold|
| (Layer 2)        |                          |  | (test-only)       |
| + getCurrentValue() -> runglerCV_ [0,+1]   |  | - engine_         |
| + getSourceRange() -> {0.0, 1.0}           |  | - rungler_        |
+------------------+                          |  | - voices_[]       |
                                              |  +------------------+
```

## Per-Voice Modulation Entities

### VoiceModSource (Extended)

| Value | Name | Range | Description |
|-------|------|-------|-------------|
| 0 | Env1 | [0, 1] | Amplitude envelope |
| 1 | Env2 | [0, 1] | Filter envelope |
| 2 | Env3 | [0, 1] | Modulation envelope |
| 3 | VoiceLFO | [-1, +1] | Per-voice LFO |
| 4 | GateOutput | [0, 1] | TranceGate smoothed value |
| 5 | Velocity | [0, 1] | Note velocity (constant per note) |
| 6 | KeyTrack | [-1, +1.1] | (midiNote - 60) / 60 |
| **7** | **Aftertouch** | **[0, 1]** | **Channel aftertouch (NEW)** |
| 8 | NumSources | -- | Sentinel |

### VoiceModDest (Extended)

| Value | Name | Offset Unit | Valid Range | Description |
|-------|------|-------------|-------------|-------------|
| 0 | FilterCutoff | semitones | -- | Filter cutoff offset |
| 1 | FilterResonance | linear | -- | Filter resonance offset |
| 2 | MorphPosition | linear | [0, 1] | Mix position offset |
| 3 | DistortionDrive | linear | [0, 1] | Distortion drive offset |
| 4 | TranceGateDepth | linear | [0, 1] | Gate depth offset |
| 5 | OscAPitch | semitones | [-48, +48] | OSC A pitch offset |
| 6 | OscBPitch | semitones | [-48, +48] | OSC B pitch offset |
| **7** | **OscALevel** | **linear** | **[0, 1]** | **OSC A amplitude (NEW)** |
| **8** | **OscBLevel** | **linear** | **[0, 1]** | **OSC B amplitude (NEW)** |
| 9 | NumDestinations | -- | -- | Sentinel |

### VoiceModRoute (Unchanged)

```cpp
struct VoiceModRoute {
    VoiceModSource source{VoiceModSource::Env1};
    VoiceModDest destination{VoiceModDest::FilterCutoff};
    float amount{0.0f};  // Bipolar: [-1.0, +1.0], clamped on setRoute()
};
```

### VoiceModRouter::computeOffsets (Modified Signature)

```
Previous: computeOffsets(env1, env2, env3, lfo, gate, velocity, keyTrack)
New:      computeOffsets(env1, env2, env3, lfo, gate, velocity, keyTrack, aftertouch)
```

### OscALevel/OscBLevel Application Formula (FR-004)

```
baseLevel = 1.0 (fixed constant, not user-adjustable)
offset = modRouter_.getOffset(VoiceModDest::OscALevel)  // or OscBLevel
effectiveLevel = clamp(baseLevel + offset, 0.0, 1.0)

// Applied before mixing:
oscABuffer_[i] *= effectiveOscALevel;
oscBBuffer_[i] *= effectiveOscBLevel;
```

## Global Modulation Entities

### Global Source Mapping (via ModulationEngine)

| Source | ModSource Enum | How Registered |
|--------|----------------|----------------|
| LFO 1 | ModSource::LFO1 | Built-in (engine owns lfo1_) |
| LFO 2 | ModSource::LFO2 | Built-in (engine owns lfo2_) |
| Chaos Mod | ModSource::Chaos | Built-in (engine owns chaos_) |
| Rungler | N/A (use Macro3) | Via setMacroValue() after Rungler.process() |
| Env Follower | ModSource::EnvFollower | Built-in (engine owns envFollower_) |
| Macro 1 (Pitch Bend) | ModSource::Macro1 | Via setMacroValue(0, normalizedPitchBend) |
| Macro 2 (Mod Wheel) | ModSource::Macro2 | Via setMacroValue(1, normalizedModWheel) |
| Macro 3 (Rungler) | ModSource::Macro3 | Via setMacroValue(2, rungler.getCurrentValue()) |
| Macro 4 (User) | ModSource::Macro4 | Via setMacroValue(3, userValue) |

**Note**: In Phase 6 (Ruinae Engine), dedicated ModSource enum values for PitchBend, ModWheel, and Rungler may be added. The macro approach is a pragmatic interim for the test scaffold.

### Global Destination IDs

| Dest ID | Name | Offset Unit | Forwarded? |
|---------|------|-------------|------------|
| 0 | Global Filter Cutoff | normalized | No (engine-level) |
| 1 | Global Filter Resonance | normalized | No (engine-level) |
| 2 | Master Volume | normalized | No (engine-level) |
| 3 | Effect Mix | normalized | No (engine-level) |
| 4 | All Voice Filter Cutoff | normalized | Yes -> each voice filter cutoff |
| 5 | All Voice Morph Position | normalized | Yes -> each voice mix position |
| 6 | Trance Gate Rate | Hz | Yes -> each voice TranceGate rateHz |

### Global-to-Voice Forwarding Formula (FR-021)

```
Step 1: perVoiceResult = clamp(baseValue + perVoiceOffset, min, max)
Step 2: finalValue = clamp(perVoiceResult + globalOffset, min, max)
```

### Trance Gate Rate Forwarding (FR-020)

```
baseRate = voice.tranceGateParams.rateHz    // Hz
globalRateOffset = engine.getModulationOffset(kTranceGateRateDestId) * 19.9  // scale from [-1,+1] to Hz range
effectiveRate = clamp(baseRate + globalRateOffset, 0.1, 20.0)
```

## State Transitions

### Voice Aftertouch Lifecycle

```
[Voice Inactive] --noteOn()--> [Voice Active, aftertouch = 0.0]
                                    |
                        setAftertouch(value) <-- MIDI channel pressure
                                    |
                        processBlock() reads aftertouch_
                                    |
                    --noteOff()---> [Voice in Release, aftertouch still active]
                                    |
                    --env done----> [Voice Inactive, aftertouch irrelevant]
```

### Rungler ModulationSource State

```
[Unprepared] --prepare()--> [Prepared, runglerCV_ = 0.0]
                                |
                    process() updates runglerCV_ per sample
                                |
                    getCurrentValue() returns runglerCV_ [0, +1]
```

## Validation Rules

| Entity | Field | Rule |
|--------|-------|------|
| VoiceModRoute | amount | Clamped to [-1.0, +1.0] on setRoute() |
| RuinaeVoice | aftertouch_ | Clamped to [0.0, 1.0] on setAftertouch() |
| VoiceModRouter | offsets_ | NaN/Inf replaced with 0.0f after accumulation |
| OscALevel effective | value | Clamped to [0.0, 1.0] (base=1.0 + offset) |
| OscBLevel effective | value | Clamped to [0.0, 1.0] (base=1.0 + offset) |
| Trance Gate Rate | Hz | Clamped to [0.1, 20.0] after global offset |
| Pitch Bend | normalized | Range [-1.0, +1.0] from 14-bit MIDI |
| Mod Wheel | normalized | Range [0.0, 1.0] from CC#1 |
