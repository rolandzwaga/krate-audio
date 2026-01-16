# Pattern Freeze Mode - Design Plan

**Date**: 2026-01-16
**Status**: Planning

## Problem Statement

The current Freeze mode requires users to activate freeze while audio is playing, otherwise the buffer is empty and nothing is heard. This makes the effect:
- Confusing for new users who enable Freeze before playing audio
- Only practical with DAW automation
- Not "playable" as a standalone effect

## Proposed Solution

Redesign Freeze mode to use **pattern-triggered playback** of captured audio slices, with a **continuously rolling capture buffer** that always has content available.

---

## Core Architecture

### 1. Rolling Capture Buffer

A circular buffer that continuously records input audio, regardless of freeze state.

- **Buffer Size**: 2-5 seconds of audio (configurable or fixed)
- **Always Recording**: Input flows into buffer even when freeze is engaged
- **Slice Extraction**: When a trigger occurs, extract a slice from recent history

```
Input ──────► [Rolling Circular Buffer - last N seconds] ──► Slice on trigger
                        ↑
                   Always recording
```

### 2. Slice Capture

When triggered (by pattern or manual), extract a portion of the rolling buffer.

| Parameter | Range | Description |
|-----------|-------|-------------|
| Slice Length | 10-2000ms | Duration of captured slice |
| Slice Mode | Fixed / Variable | Fixed = constant length, Variable = pattern-controlled |

### 3. Pattern Engine

Generates trigger events that play the captured slice through the processing chain.

#### Pattern Type: Euclidean Rhythms
Distributes N hits evenly across M steps (Bjorklund algorithm).

| Parameter | Range | Description |
|-----------|-------|-------------|
| Steps | 2-32 | Total steps in pattern |
| Hits | 1 to Steps | Number of triggers |
| Rotation | 0 to Steps-1 | Pattern offset |
| Rate | Note value | Pattern tempo (1/1 to 1/32) |

Examples:
- 3 hits / 8 steps = tresillo rhythm
- 5 hits / 8 steps = cinquillo
- 7 hits / 16 steps = West African bell pattern

#### Pattern Type: Granular Scatter
Random/semi-random triggering of grains from the slice.

| Parameter | Range | Description |
|-----------|-------|-------------|
| Density | 1-50 Hz | Triggers per second |
| Position Jitter | 0-100% | Randomize playback position within slice |
| Size Jitter | 0-100% | Randomize grain duration |
| Grain Size | 10-500ms | Base grain duration |

#### Pattern Type: Harmonic Drones
Sustained layered playback with pitch variation.

| Parameter | Range | Description |
|-----------|-------|-------------|
| Voice Count | 1-4 | Number of simultaneous layers |
| Interval | Unison/Oct/5th/etc | Pitch relationship between voices |
| Drift | 0-100% | Slow pitch/amplitude modulation depth |
| Drift Rate | 0.1-2 Hz | LFO speed for drift |

#### Pattern Type: Noise Bursts
Rhythmic filtered noise (not from input audio).

| Parameter | Range | Description |
|-----------|-------|-------------|
| Noise Color | White/Pink/Brown | Noise spectrum |
| Burst Rate | Note value | Rhythm of bursts |
| Filter Type | LP/HP/BP | Filter mode |
| Cutoff | 20-20kHz | Filter frequency |
| Sweep | 0-100% | Filter envelope amount |

### 4. Envelope Shaper

Applied to each triggered slice/grain.

| Parameter | Range | Description |
|-----------|-------|-------------|
| Attack | 0-500ms | Fade-in time |
| Release | 0-2000ms | Fade-out time |
| Shape | Linear/Exp | Envelope curve |

### 5. Processing Chain (Existing)

After pattern triggering, audio flows through existing freeze processors:

```
Pattern Trigger ──► Envelope ──► [Pitch Shift] ──► [Diffusion] ──► [Filter] ──► [Decay] ──► Mix
```

All existing parameters remain:
- Pitch Semitones/Cents + Shimmer Mix
- Diffusion Amount/Size
- Filter Enable/Type/Cutoff
- Decay
- Dry/Wet Mix

---

## Signal Flow Diagram

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                    PATTERN FREEZE                        │
                    │                                                          │
Input ──┬──────────►│  [Rolling Buffer] ──► [Slice Capture]                   │
        │           │         │                    │                          │
        │           │         │              ┌─────▼─────┐                    │
        │           │         │              │  Pattern  │                    │
        │           │         │              │  Engine   │                    │
        │           │         │              │           │                    │
        │           │         │              │ Euclidean │                    │
        │           │         │              │ Granular  │                    │
        │           │         │              │ Drones    │                    │
        │           │         │              │ Noise     │                    │
        │           │         │              └─────┬─────┘                    │
        │           │         │                    │ triggers                 │
        │           │         │              ┌─────▼─────┐                    │
        │           │         │              │ Envelope  │                    │
        │           │         │              │  Shaper   │                    │
        │           │         │              └─────┬─────┘                    │
        │           │         │                    │                          │
        │           │         │              ┌─────▼─────┐                    │
        │           │         │              │  Shimmer  │ (existing)         │
        │           │         │              │ Diffusion │                    │
        │           │         │              │  Filter   │                    │
        │           │         │              │  Decay    │                    │
        │           │         │              └─────┬─────┘                    │
        │           │                              │                          │
        │           └──────────────────────────────┼──────────────────────────┘
        │                                          │
        │              Dry                         │ Wet
        └──────────────────────────┬───────────────┘
                                   │
                                   ▼
                               [Mix] ──► Output
```

---

## Parameter Summary

### New Parameters (Pattern Section)

| Parameter | Type | Range | Default |
|-----------|------|-------|---------|
| Pattern Type | Enum | Euclidean/Granular/Drones/Noise | Euclidean |
| Pattern Rate | Note Value | 1/1 to 1/32 | 1/8 |
| Slice Length | ms | 10-2000 | 200 |
| Slice Mode | Enum | Fixed/Variable | Fixed |
| Attack | ms | 0-500 | 10 |
| Release | ms | 0-2000 | 100 |

### Euclidean-Specific

| Parameter | Range | Default |
|-----------|-------|---------|
| Steps | 2-32 | 8 |
| Hits | 1-Steps | 3 |
| Rotation | 0-Steps | 0 |

### Granular-Specific

| Parameter | Range | Default |
|-----------|-------|---------|
| Density | 1-50 Hz | 10 |
| Position Jitter | 0-100% | 50 |
| Size Jitter | 0-100% | 25 |

### Drone-Specific

| Parameter | Range | Default |
|-----------|-------|---------|
| Voice Count | 1-4 | 2 |
| Interval | Enum | Octave |
| Drift Amount | 0-100% | 30 |
| Drift Rate | 0.1-2 Hz | 0.5 |

### Noise-Specific

| Parameter | Range | Default |
|-----------|-------|---------|
| Noise Color | Enum | Pink |
| Filter Type | Enum | LP |
| Cutoff | 20-20kHz | 2000 |
| Sweep Amount | 0-100% | 50 |

### Existing Parameters (Retained)

- Pitch Semitones (-24 to +24)
- Pitch Cents (-100 to +100)
- Shimmer Mix (0-100%)
- Diffusion Amount (0-100%)
- Diffusion Size (0-100%)
- Filter Enabled (On/Off)
- Filter Type (LP/HP/BP)
- Filter Cutoff (20-20kHz)
- Decay (0-100%)
- Dry/Wet Mix (0-100%)

---

## Backwards Compatibility

**Option A: Replace Freeze Mode**
- Pattern Freeze completely replaces old Freeze mode
- Simpler, but breaks existing presets

**Option B: Add "Legacy" Pattern Type**
- Add a 5th pattern type: "Legacy/Live"
- When selected, behaves like current freeze (mute input, loop buffer)
- Preserves backwards compatibility

**Recommendation**: Option B - add Legacy mode to preserve existing behavior.

---

## Implementation Phases

### Phase 1: Rolling Buffer Infrastructure
- Implement circular capture buffer
- Slice extraction with configurable length
- Basic envelope shaper (attack/release)

### Phase 2: Euclidean Pattern Engine
- Bjorklund algorithm for hit distribution
- Tempo-synced step sequencing
- Trigger → slice playback

### Phase 3: Granular Scatter
- Random trigger generation
- Position/size jitter
- Polyphonic grain playback

### Phase 4: Harmonic Drones
- Multi-voice playback
- Pitch intervals
- LFO drift modulation

### Phase 5: Noise Bursts
- Integrate existing NoiseGenerator
- Envelope-triggered bursts
- Filter sweep automation

### Phase 6: UI Integration
- Pattern type selector
- Context-sensitive parameter display
- Visual feedback (step indicators, grain visualization)

---

## Open Questions

1. **Buffer Size**: Fixed 5 seconds, or user-configurable?
2. **Polyphony**: How many simultaneous grains/slices for Granular mode?
3. **Crossfade**: How to handle slice boundaries to avoid clicks?
4. **Legacy Mode**: Include as 5th pattern type, or separate toggle?
5. **Visual Feedback**: Show pattern steps? Grain activity? Buffer fill level?

---

## DSP Layer Placement

- **RollingBuffer**: Layer 1 (primitive) - simple circular buffer
- **PatternGenerator**: Layer 1 (primitive) - generates trigger events
- **EnvelopeShaper**: Layer 1 (primitive) - applies AR envelope
- **PatternFreezeMode**: Layer 4 (effect) - composes all components
