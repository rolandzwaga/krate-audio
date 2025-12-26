# Research: Digital Delay Mode

**Feature**: 026-digital-delay
**Date**: 2025-12-26

## Summary

This research phase identified that most required components already exist in the codebase. The Digital Delay feature is primarily a composition task leveraging existing Layer 0-3 components.

## Key Findings

### 1. LFO Waveforms Already Exist

**Decision**: Use existing `Waveform` enum from `dsp/primitives/lfo.h`

**Rationale**: The LFO primitive already provides all 6 required modulation waveforms:
- `Sine` - Smooth, natural pitch variation
- `Triangle` - Linear pitch sweeps (classic chorus)
- `Sawtooth` - Rising pitch ramps (Doppler effect)
- `Square` - Alternating pitch steps (rhythmic)
- `SampleHold` - Random stepped pitch
- `SmoothRandom` - Continuously varying random

**Alternatives Considered**:
- Create new `ModulationWaveform` enum - Rejected: would duplicate existing functionality

### 2. CharacterProcessor DigitalVintage Mode

**Decision**: Leverage existing `CharacterMode::DigitalVintage` for 80s Digital and Lo-Fi eras

**Rationale**: CharacterProcessor already includes:
- BitCrusher for bit depth reduction
- SampleRateReducer for sample rate reduction
- Smooth crossfade between modes

**Era Mapping**:
| Era | CharacterMode | Notes |
|-----|---------------|-------|
| Pristine | Clean | Full bypass for transparency |
| 80s Digital | DigitalVintage | Moderate settings |
| Lo-Fi | DigitalVintage | Aggressive settings |

**Alternatives Considered**:
- Direct BitCrusher/SampleRateReducer use - Rejected: CharacterProcessor provides smoothing and unified interface

### 3. Limiter Implementation

**Decision**: Use existing `DynamicsProcessor` from Layer 2

**Rationale**: DynamicsProcessor provides:
- Peak detection mode for transient response
- Configurable knee (0-24dB)
- Threshold and ratio settings
- Already real-time safe

**Configuration for Digital Delay**:
- Detection: Peak
- Threshold: -0.5dBFS
- Ratio: 100:1 (true limiting)
- Knee: 0/3/6dB based on LimiterCharacter

**Alternatives Considered**:
- Custom limiter implementation - Rejected: DynamicsProcessor is sufficient and tested
- Soft clip instead of limiter - Rejected: doesn't prevent runaway feedback as effectively

### 4. Tempo Sync

**Decision**: Use existing `TimeMode` enum and `BlockContext` integration from DelayEngine

**Rationale**: DelayEngine already supports:
- Free mode (milliseconds)
- Synced mode (note values via BlockContext)
- All note values with dotted/triplet modifiers

**Alternatives Considered**: None - existing implementation is complete

## Components to Create

Only the following new types are needed:

1. **DigitalDelay class** - Main Layer 4 feature class
2. **DigitalEra enum** - Era preset selection (Pristine, EightiesDigital, LoFi)
3. **LimiterCharacter enum** - Limiter knee selection (Soft, Medium, Hard)

## Components to Reuse

| Component | Layer | Purpose |
|-----------|-------|---------|
| DelayEngine | 3 | Core delay with tempo sync |
| FeedbackNetwork | 3 | Feedback path |
| CharacterProcessor | 3 | DigitalVintage mode |
| DynamicsProcessor | 2 | Feedback limiting |
| LFO | 1 | Modulation with Waveform enum |
| OnePoleSmoother | 1 | Parameter smoothing |
| BlockContext | 0 | Tempo/transport info |
| NoteValue | 0 | Note value calculations |
| db_utils | 0 | dB conversions |

## Outstanding Questions

None - all technical decisions resolved.

## Next Steps

Proceed to `/speckit.tasks` for implementation task generation.
