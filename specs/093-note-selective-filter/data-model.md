# Data Model: Note-Selective Filter

**Date**: 2026-01-24 | **Spec**: 093-note-selective-filter

## Overview

This document defines the data model for the NoteSelectiveFilter processor, including all entities, relationships, and state management.

---

## Entity: NoDetectionMode

Enum specifying behavior when pitch detection fails or confidence is below threshold.

```cpp
enum class NoDetectionMode : uint8_t {
    Dry = 0,       // Pass dry signal when no pitch detected (default)
    Filtered = 1,  // Apply filter regardless of detection
    LastState = 2  // Maintain previous filtering state
};
```

### Behavior Matrix

| Mode | Valid Pitch + Match | Valid Pitch + No Match | No Valid Pitch |
|------|---------------------|------------------------|----------------|
| Dry | Crossfade to filtered | Crossfade to dry | Crossfade to dry |
| Filtered | Crossfade to filtered | Crossfade to dry | Crossfade to filtered |
| LastState | Crossfade to filtered | Crossfade to dry | Keep previous state |

---

## Entity: NoteSelectiveFilter

Main processor class that implements note-selective filtering.

### Constants

```cpp
// Detection defaults
static constexpr float kDefaultConfidenceThreshold = 0.3f;
static constexpr float kMinConfidenceThreshold = 0.0f;
static constexpr float kMaxConfidenceThreshold = 1.0f;

// Tolerance constraints
static constexpr float kDefaultNoteTolerance = 49.0f;  // cents
static constexpr float kMinNoteTolerance = 1.0f;       // cents
static constexpr float kMaxNoteTolerance = 49.0f;      // cents (prevents overlap)

// Crossfade constraints
static constexpr float kDefaultCrossfadeTimeMs = 5.0f;
static constexpr float kMinCrossfadeTimeMs = 0.5f;
static constexpr float kMaxCrossfadeTimeMs = 50.0f;

// Filter defaults
static constexpr float kDefaultCutoffHz = 1000.0f;
static constexpr float kDefaultResonance = 0.7071f;  // Butterworth Q
static constexpr float kMinCutoffHz = 20.0f;
static constexpr float kMinResonance = 0.1f;
static constexpr float kMaxResonance = 30.0f;
```

### Atomic Parameters (Thread-Safe)

These parameters can be modified from the UI thread while audio is processing.

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| targetNotes_ | atomic<uint16_t> | 0 | 0-4095 | Bitset of enabled notes (bit 0=C, 11=B) |
| noteTolerance_ | atomic<float> | 49.0f | 1-49 | Cents tolerance for note matching |
| crossfadeTimeMs_ | atomic<float> | 5.0f | 0.5-50 | Transition time in milliseconds |
| cutoffHz_ | atomic<float> | 1000.0f | 20-Nyquist | Filter cutoff frequency |
| resonance_ | atomic<float> | 0.7071f | 0.1-30 | Filter Q factor |
| filterType_ | atomic<int> | 0 | 0-7 | SVFMode enum value |
| confidenceThreshold_ | atomic<float> | 0.3f | 0-1 | Pitch detection threshold |
| noDetectionMode_ | atomic<int> | 0 | 0-2 | NoDetectionMode enum value |
| minHz_ | atomic<float> | 50.0f | 50-maxHz | Min detection frequency |
| maxHz_ | atomic<float> | 1000.0f | minHz-1000 | Max detection frequency |

### Non-Atomic State (Audio Thread Only)

These are internal state variables only accessed from the audio thread.

| State | Type | Initial | Description |
|-------|------|---------|-------------|
| sampleRate_ | double | 44100.0 | Current sample rate |
| prepared_ | bool | false | Whether prepare() has been called |
| lastDetectedNote_ | int | -1 | Last valid detected note class |
| lastFilteringState_ | bool | false | For LastState mode tracking |
| samplesSinceNoteUpdate_ | size_t | 0 | Counter for block-rate updates |
| blockUpdateInterval_ | size_t | 512 | Samples between note matching updates |

### Composed Components

| Component | Type | Purpose |
|-----------|------|---------|
| pitchDetector_ | PitchDetector | Detect input pitch |
| filter_ | SVF | Apply filtering effect |
| crossfadeSmoother_ | OnePoleSmoother | Smooth dry/wet transitions |

---

## State Transitions

### Crossfade State Machine

```
                  +-----------+
                  |   Reset   |
                  | (cf=0.0)  |
                  +-----+-----+
                        |
                        v prepare()
                  +-----+-----+
              +-->|   Idle    |<--+
              |   | (cf=prev) |   |
              |   +-----+-----+   |
              |         |         |
  No match    |         | process()
  or no pitch |         v         | Match
              |   +-----+-----+   | detected
              +---|  Checking |---+
                  | (read cfg)|
                  +-----------+
                        |
            +-----------+-----------+
            |                       |
            v                       v
      +-----+-----+           +-----+-----+
      | Fade Dry  |           | Fade Wet  |
      | (cf->0.0) |           | (cf->1.0) |
      +-----+-----+           +-----+-----+
            |                       |
            +------> Process <------+
                  (apply crossfade)
```

### Note Matching Logic

```
1. At block start (every blockUpdateInterval_ samples):
   a. Get detected frequency and confidence
   b. If confidence >= threshold:
      - Convert frequency to note class
      - Check if note class bit is set in targetNotes_
      - Check if cents deviation is within tolerance
      - Set target: 1.0 if match, 0.0 if no match
   c. If confidence < threshold:
      - Apply NoDetectionMode behavior
   d. Store lastDetectedNote_ and lastFilteringState_

2. Every sample:
   a. Push sample to pitch detector
   b. Process sample through filter (always hot)
   c. Advance crossfade smoother
   d. Output = (1 - crossfade) * dry + crossfade * filtered
```

---

## Data Validation Rules

### Parameter Clamping

```cpp
// Note tolerance: prevent overlapping tolerance zones
tolerance = std::clamp(tolerance, kMinNoteTolerance, kMaxNoteTolerance);

// Crossfade time: reasonable range for click-free transitions
crossfadeMs = std::clamp(crossfadeMs, kMinCrossfadeTimeMs, kMaxCrossfadeTimeMs);

// Cutoff frequency: valid audio range
float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
cutoff = std::clamp(cutoff, kMinCutoffHz, maxCutoff);

// Resonance: SVF stable range
resonance = std::clamp(resonance, kMinResonance, kMaxResonance);

// Note class: valid range 0-11 or clamp
noteClass = std::clamp(noteClass, 0, 11);

// Detection range: constrained by PitchDetector
minHz = std::clamp(minHz, PitchDetector::kMinFrequency, maxHz);
maxHz = std::clamp(maxHz, minHz, PitchDetector::kMaxFrequency);
```

### Tolerance Zone Non-Overlap

Adjacent semitones are 100 cents apart. Maximum tolerance of 49 cents ensures:
- Note at +49 cents from center A is not also within +49 cents of center A#
- Gap of 2 cents between tolerance zones prevents ambiguous matching

---

## Relationships

```
NoteSelectiveFilter
    |
    +-- has-a --> PitchDetector (Layer 1)
    |              |
    |              +-- detects --> frequency, confidence
    |
    +-- has-a --> SVF (Layer 1)
    |              |
    |              +-- applies --> filter effect
    |
    +-- has-a --> OnePoleSmoother (Layer 1)
    |              |
    |              +-- smooths --> crossfade value
    |
    +-- uses --> frequencyToNoteClass() (Layer 0)
    |              |
    |              +-- converts --> frequency to note class
    |
    +-- uses --> frequencyToCentsDeviation() (Layer 0)
                   |
                   +-- calculates --> cents from note center
```

---

## Memory Layout

Approximate size calculation:

| Component | Size |
|-----------|------|
| PitchDetector | ~4KB (buffers) |
| SVF | ~56 bytes |
| OnePoleSmoother | ~24 bytes |
| Atomic parameters (10) | ~80 bytes |
| Non-atomic state | ~48 bytes |
| **Total** | **~4.2 KB** |

Note: PitchDetector dominates due to analysis buffers (256-sample window + autocorrelation buffer).
