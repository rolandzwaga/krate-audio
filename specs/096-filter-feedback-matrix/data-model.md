# Data Model: Filter Feedback Matrix

**Feature**: 096-filter-feedback-matrix
**Date**: 2026-01-24

## Overview

This document defines the entities, relationships, and state transitions for the FilterFeedbackMatrix component.

## Entities

### 1. FilterFeedbackMatrix<N>

The main component class. Template parameter `N` specifies maximum filter count (2-4).

**Fields**:

| Field | Type | Description | Constraints |
|-------|------|-------------|-------------|
| filters_ | `std::array<SVF, N>` | SVF filter instances | N = 2-4 |
| delayLines_ | `std::array<std::array<DelayLine, N>, N>` | NxN delay lines for feedback paths | 0-100ms delay (min 1 sample) |
| dcBlockers_ | `std::array<std::array<DCBlocker, N>, N>` | NxN DC blockers (one per path) | 10Hz cutoff |
| feedbackMatrix_ | `std::array<std::array<float, N>, N>` | NxN feedback amounts | [-1.0, 1.0] |
| delayMatrix_ | `std::array<std::array<float, N>, N>` | NxN delay times in ms | [0, 100] |
| inputGains_ | `std::array<float, N>` | Input routing gains | [0.0, 1.0] |
| outputGains_ | `std::array<float, N>` | Output mix gains | [0.0, 1.0] |
| globalFeedback_ | float | Master feedback scalar | [0.0, 1.0] |
| activeFilters_ | size_t | Runtime active filter count | [1, N] |
| sampleRate_ | double | Current sample rate | >= 1000 |
| prepared_ | bool | Lifecycle state | |

**Smoothed Parameters** (using OnePoleSmoother):

| Parameter | Smoothing Time | Purpose |
|-----------|---------------|---------|
| feedbackSmoothers_[N][N] | 20ms | Smooth feedback matrix changes |
| delaySmoothers_[N][N] | 20ms | Smooth delay time changes |
| inputGainSmoothers_[N] | 20ms | Smooth input routing changes |
| outputGainSmoothers_[N] | 20ms | Smooth output mix changes |
| globalFeedbackSmoother_ | 20ms | Smooth global feedback changes |

### 2. FeedbackPath (Internal Concept)

Each element of the NxN matrix represents a feedback path from filter `from` to filter `to`.

**Logical Structure** (not a separate struct, stored in parallel arrays):

| Component | Description |
|-----------|-------------|
| feedbackMatrix_[from][to] | Feedback amount (-1 to +1) |
| delayMatrix_[from][to] | Delay time in ms (0 to 100) |
| delayLines_[from][to] | DelayLine instance |
| dcBlockers_[from][to] | DCBlocker instance |
| feedbackSmoothers_[from][to] | OnePoleSmoother for amount |
| delaySmoothers_[from][to] | OnePoleSmoother for delay |

### 3. Filter Configuration

Each filter (0 to N-1) has:

| Parameter | Type | Range | Default |
|-----------|------|-------|---------|
| mode | SVFMode | Lowpass, Highpass, Bandpass, Notch, Peak | Lowpass |
| cutoff | float | 20Hz - 20kHz | 1000Hz |
| resonance | float | 0.5 - 30.0 | 0.707 (Butterworth) |

## Relationships

```
FilterFeedbackMatrix<N>
├── contains N SVF filters
├── contains N x N DelayLines (feedback path delays)
├── contains N x N DCBlockers (one per feedback path)
├── contains N x N feedback amounts
├── contains N x N delay times
├── contains N input gains (input routing)
└── contains N output gains (output mix)
```

**Signal Flow**:

```
Input
  │
  ├──[inputGain[0]]──> Filter 0 ──[tanh]──┬──[delayLines[0][*]]──┬──[dcBlocker]──> to other filters
  ├──[inputGain[1]]──> Filter 1 ──[tanh]──┼──[delayLines[1][*]]──┤
  ├──[inputGain[2]]──> Filter 2 ──[tanh]──┼──[delayLines[2][*]]──┤
  └──[inputGain[3]]──> Filter 3 ──[tanh]──┴──[delayLines[3][*]]──┘
                              │
                              v
              Sum(filter outputs * outputGain[i])
                              │
                              v
                           Output
```

**Feedback Routing Detail**:

For each filter `to`, its input is:
```
filterInput[to] = inputGain[to] * audioInput
                + sum(
                    feedbackMatrix[from][to] * globalFeedback
                    * dcBlocker[from][to].process(
                        delayLine[from][to].readLinear(delayMs * sampleRate / 1000)
                      )
                  ) for all from in [0, activeFilters)
```

The delayed signal comes from the previous sample's `tanh(filter[from].output)`.

## State Machine

### Lifecycle States

```
                    ┌─────────────┐
                    │ Unprepared  │
                    └──────┬──────┘
                           │ prepare(sampleRate)
                           v
                    ┌─────────────┐
              ┌─────│  Prepared   │─────┐
              │     └──────┬──────┘     │
              │            │            │
     reset()  │            │ process()  │ prepare(newRate)
              │            v            │
              │     ┌─────────────┐     │
              └────>│ Processing  │<────┘
                    └─────────────┘
```

### State Transitions

| From | To | Trigger | Actions |
|------|----|---------|---------|
| Unprepared | Prepared | prepare(sr) | Init all components, snap smoothers |
| Prepared | Processing | process() | Process audio |
| Processing | Prepared | reset() | Clear all state |
| Any | Prepared | prepare(newSr) | Reinitialize for new sample rate |

## Validation Rules

### FR-001: Filter Count
- Template parameter N: 2, 3, or 4
- Runtime activeFilters_: 1 <= activeFilters_ <= N
- setActiveFilters(count) asserts count <= N in debug, clamps in release

### FR-002-004: Filter Parameters
- Cutoff: Clamped to [20Hz, sampleRate * 0.495]
- Resonance: Clamped to [0.5, 30.0]
- Mode: Enum SVFMode (Lowpass, Highpass, Bandpass, Notch, Peak)

### FR-005-006: Feedback Matrix
- Feedback amounts: [-1.0, 1.0] (negative for phase inversion)
- Self-feedback (diagonal): Allowed

### FR-007: Feedback Delay
- Delay times: [0, 100] ms (user-facing range)
- Minimum actual delay: 1 sample (ensures causality, 0ms clamped internally to 1 sample)

### FR-008-009: Input/Output Routing
- Input gains: [0.0, 1.0]
- Output gains: [0.0, 1.0]

### FR-010: Global Feedback
- Range: [0.0, 1.0]
- Multiplies all matrix values

### FR-011: Stability Limiting
- Apply tanh() to each filter output before feedback routing
- Bounds output to approximately [-1, 1]

### FR-017: NaN/Inf Handling
- On NaN/Inf input: Return 0, reset all filter and delay states

## Memory Layout

For N=4 (maximum case):

| Component | Size | Count | Total Bytes |
|-----------|------|-------|-------------|
| SVF | ~64 bytes | 4 | 256 |
| DelayLine | ~variable* | 16 | ~640KB** |
| DCBlocker | ~32 bytes | 16 | 512 |
| OnePoleSmoother | ~20 bytes | 33 | 660 |
| float arrays | 4 bytes | 40 | 160 |
| **Total** | | | ~641KB |

*DelayLine size depends on max delay and sample rate
**At 192kHz, 100ms = 19,200 samples * 4 bytes = 76.8KB per delay line

For stereo (dual-mono): Double the above for the second channel.

## Constants

```cpp
static constexpr size_t kMinFilters = 2;
static constexpr size_t kMaxFilters = 4;
static constexpr float kMinCutoff = 20.0f;
static constexpr float kMaxCutoff = 20000.0f;
static constexpr float kMinQ = 0.5f;
static constexpr float kMaxQ = 30.0f;
static constexpr float kMinFeedback = -1.0f;
static constexpr float kMaxFeedback = 1.0f;
static constexpr float kMinDelay = 0.0f;
static constexpr float kMaxDelayMs = 100.0f;
static constexpr float kSmoothingTimeMs = 20.0f;
static constexpr float kDCBlockerCutoff = 10.0f;
```
