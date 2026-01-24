# Data Model: TransientAwareFilter

**Feature Branch**: `091-transient-filter`
**Date**: 2026-01-24
**Status**: Complete

## Entity Definitions

### TransientFilterMode (Enum)

Filter response type selection for TransientAwareFilter.

```cpp
enum class TransientFilterMode : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass, emphasizes low frequencies
    Bandpass = 1,  ///< Constant 0 dB peak bandpass
    Highpass = 2   ///< 12 dB/oct highpass, emphasizes high frequencies
};
```

**Validation**: No validation needed (type-safe enum).

**State Transitions**: N/A (static configuration).

### TransientAwareFilter (Class)

Layer 2 DSP processor that detects transients and modulates filter parameters.

#### Configuration Fields

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `sensitivity_` | float | [0.0, 1.0] | 0.5 | Transient detection threshold (0=none, 1=all) |
| `transientAttack_` | float | [0.1, 50] | 1.0 | Response attack time in ms (matches `setTransientAttack()`) |
| `transientDecay_` | float | [1, 1000] | 50.0 | Response decay time in ms (matches `setTransientDecay()`) |
| `idleCutoff_` | float | [20, Nyquist*0.45] | 200.0 | Filter cutoff when no transient |
| `transientCutoff_` | float | [20, Nyquist*0.45] | 4000.0 | Filter cutoff at peak transient |
| `idleResonance_` | float | [0.5, 20.0] | 0.7071 | Q factor when no transient |
| `transientQBoost_` | float | [0.0, 20.0] | 0.0 | Additional Q during transient |
| `filterType_` | TransientFilterMode | enum | Lowpass | Filter response type |

#### Internal State Fields

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `sampleRate_` | double | >= 1000 | Current sample rate |
| `currentCutoff_` | float | [20, Nyquist*0.45] | Current filter cutoff (for monitoring) |
| `currentResonance_` | float | [0.5, 30.0] | Current Q factor (for monitoring) |
| `transientLevel_` | float | [0.0, 1.0] | Current transient detection level |
| `prepared_` | bool | true/false | Whether prepare() has been called |

#### Composed Components

| Field | Type | Description |
|-------|------|-------------|
| `fastEnvelope_` | EnvelopeFollower | 1ms attack/release envelope |
| `slowEnvelope_` | EnvelopeFollower | 50ms attack/release envelope |
| `responseSmoother_` | OnePoleSmoother | Attack/decay smoothing for response |
| `filter_` | SVF | Main audio filter |

#### Validation Rules

1. **Sensitivity**: Clamped to [0.0, 1.0]
2. **Attack Time**: Clamped to [0.1, 50] ms
3. **Decay Time**: Clamped to [1, 1000] ms
4. **Cutoff Frequencies**: Clamped to [20, sampleRate * 0.45] Hz
5. **Resonance**: Clamped to [0.5, 20.0]
6. **Q boost**: Clamped to [0.0, 20.0]
7. **Total Q**: Always clamped to max 30.0 (SVF stability limit)

#### State Transitions

```
UNPREPARED ──prepare()──> PREPARED
     ^                        │
     │                        v
     └───────reset()──────────┘
                              │
                              v
                    [Ready for process()]
```

## Relationships

```
TransientAwareFilter
├── fastEnvelope_ : EnvelopeFollower (composition)
│   └── Configured: attack=1ms, release=1ms
├── slowEnvelope_ : EnvelopeFollower (composition)
│   └── Configured: attack=50ms, release=50ms
├── responseSmoother_ : OnePoleSmoother (composition)
│   └── Configured: attack/decay from parameters
└── filter_ : SVF (composition)
    └── Configured: mode, cutoff, resonance from parameters
```

## Processing Data Flow

```
Audio Input
    │
    ├──────────────────────────────┐
    │                              │
    v                              v
[fastEnvelope_]             [slowEnvelope_]
    │                              │
    v                              v
 fastEnv                       slowEnv
    │                              │
    └──────────┬───────────────────┘
               │
               v
    diff = max(0, fast - slow)
               │
               v
    normalized = diff / max(slow, 1e-6)
               │
               v
    threshold = 1.0 - sensitivity_
               │
               v
    transient = (normalized > threshold) ? normalized : 0
               │
               v
    [responseSmoother_]
               │
               v
    transientLevel_ (smoothed 0-1)
               │
    ┌──────────┴──────────┐
    │                     │
    v                     v
 cutoff              resonance
    │                     │
    v                     v
[filter_.setCutoff()] [filter_.setResonance()]
               │
               v
    [filter_.process(input)]
               │
               v
         Audio Output
```

## Constants

| Constant | Value | From Spec |
|----------|-------|-----------|
| `kFastEnvelopeAttackMs` | 1.0f | FR-005 |
| `kFastEnvelopeReleaseMs` | 1.0f | FR-005 |
| `kSlowEnvelopeAttackMs` | 50.0f | FR-006 |
| `kSlowEnvelopeReleaseMs` | 50.0f | FR-006 |
| `kMinSensitivity` | 0.0f | FR-002 |
| `kMaxSensitivity` | 1.0f | FR-002 |
| `kMinAttackMs` | 0.1f | FR-003 |
| `kMaxAttackMs` | 50.0f | FR-003 |
| `kMinDecayMs` | 1.0f | FR-004 |
| `kMaxDecayMs` | 1000.0f | FR-004 |
| `kMinCutoffHz` | 20.0f | FR-007 |
| `kMinResonance` | 0.5f | FR-011 |
| `kMaxResonance` | 20.0f | FR-011 |
| `kMaxTotalResonance` | 30.0f | FR-013 |
| `kMaxQBoost` | 20.0f | FR-012 |
| `kEpsilon` | 1e-6f | Internal |

## Memory Layout

No dynamic allocation. All state is in-place:

```cpp
class TransientAwareFilter {
    // Composed (stack allocated)
    EnvelopeFollower fastEnvelope_;     // ~48 bytes
    EnvelopeFollower slowEnvelope_;     // ~48 bytes
    OnePoleSmoother responseSmoother_;  // ~20 bytes
    SVF filter_;                        // ~64 bytes

    // Configuration
    double sampleRate_;                 // 8 bytes
    float sensitivity_;                 // 4 bytes
    float transientAttackMs_;           // 4 bytes
    float transientDecayMs_;            // 4 bytes
    float idleCutoff_;                  // 4 bytes
    float transientCutoff_;             // 4 bytes
    float idleResonance_;               // 4 bytes
    float transientQBoost_;             // 4 bytes
    TransientFilterMode filterType_;    // 1 byte

    // State
    float currentCutoff_;               // 4 bytes
    float currentResonance_;            // 4 bytes
    float transientLevel_;              // 4 bytes
    bool prepared_;                     // 1 byte

    // Padding for alignment            // ~6 bytes
};
// Total: ~232 bytes (approximate, depends on EnvelopeFollower/SVF internals)
```

## Thread Safety

**Not thread-safe**. Create separate instances for each audio thread.

**Safe operations from any thread**:
- None (all state is mutable)

**Audio thread only**:
- `process()`
- `processBlock()`

**UI thread only** (during non-processing):
- All setters
- `prepare()`
- `reset()`
