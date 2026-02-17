# Data Model: Identity Phase Locking for PhaseVocoderPitchShifter

**Feature**: 061-phase-locking | **Date**: 2026-02-17

## Entities

### 1. PhaseVocoderPitchShifter (Modified Entity)

**Location**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
**Type**: Existing class, modified in-place

#### New Constants

| Name | Type | Value | Description |
|------|------|-------|-------------|
| `kMaxBins` | `static constexpr std::size_t` | `4097` | Maximum FFT bins (8192/2+1, supports max FFT size) |
| `kMaxPeaks` | `static constexpr std::size_t` | `512` | Maximum detectable peaks per frame |

#### New Member Variables

| Name | Type | Default | Size | Description |
|------|------|---------|------|-------------|
| `isPeak_` | `std::array<bool, kMaxBins>` | `{}` (all false) | 4097 bytes | Per-analysis-bin peak flag |
| `peakIndices_` | `std::array<uint16_t, kMaxPeaks>` | `{}` (all 0) | 1024 bytes | Analysis-domain peak bin indices |
| `numPeaks_` | `std::size_t` | `0` | 8 bytes | Count of detected peaks in current frame |
| `regionPeak_` | `std::array<uint16_t, kMaxBins>` | `{}` (all 0) | 8194 bytes | Region-peak assignment: maps each analysis bin to its controlling peak |
| `phaseLockingEnabled_` | `bool` | `true` | 1 byte | Phase locking toggle (enabled by default) |
| `wasLocked_` | `bool` | `false` | 1 byte | Previous frame's locking state (for toggle-to-basic re-init detection) |

**Total new memory**: ~13.3 KB per PhaseVocoderPitchShifter instance

> **Authoritative reference**: This member variables table is the canonical definition. `spec.md` and `plan.md` reference this table; if values differ between documents, this data-model.md definition takes precedence.

#### New Public Methods

| Method | Signature | Description | Thread Safety |
|--------|-----------|-------------|---------------|
| `setPhaseLocking` | `void setPhaseLocking(bool enabled) noexcept` | Enable/disable identity phase locking at runtime | NOT thread-safe. `phaseLockingEnabled_` is a plain `bool`. Do not call concurrently with `processFrame()` from another thread (data race). Safe to call from the audio thread itself or from a control thread when the audio thread is not in `processFrame()`. |
| `getPhaseLocking` | `[[nodiscard]] bool getPhaseLocking() const noexcept` | Query current phase locking state | Read-only; safe to call from any thread if no concurrent write is in progress. |

#### Modified Methods

| Method | Modification |
|--------|-------------|
| `processFrame(float pitchRatio)` | Add 3-stage phase locking algorithm; split synthesis loop into two passes |
| `reset()` | Add clearing of phase locking state arrays |

### 2. Peak (Conceptual Entity -- Not a Struct)

A spectral peak is not represented as a standalone struct. Instead, peak data is stored in parallel arrays within PhaseVocoderPitchShifter:

| Data | Storage | Index |
|------|---------|-------|
| Is bin k a peak? | `isPeak_[k]` | Analysis bin index k |
| k-th peak's bin index | `peakIndices_[i]` | Peak ordinal i (0 to numPeaks_-1) |
| Peak count | `numPeaks_` | Scalar |

**Validation rules**:
- Peak detection uses strict inequality: `magnitude[k] > magnitude[k-1] AND magnitude[k] > magnitude[k+1]`
- Bins 0 and numBins-1 are excluded from peak detection (boundary bins)
- Peak count is capped at `kMaxPeaks` (512)
- Typical count: 20-100 peaks for 4096-pt FFT on music signals

### 3. Region of Influence (Conceptual Entity -- Not a Struct)

Each bin is assigned to its nearest peak. Stored as `regionPeak_[k]` which contains the bin index of the controlling peak for analysis bin `k`.

**Validation rules**:
- 100% bin coverage: every bin from 0 to numBins-1 MUST be assigned
- Boundary placement: midpoint between adjacent peaks, rounding toward lower-frequency peak when equidistant
- Division formula: `midpoint = (peakIndices_[i] + peakIndices_[i+1]) / 2` (integer division truncates, which rounds toward lower)

### 4. Phase Locking State Machine

```
                    setPhaseLocking(true)
            +----------------------------------+
            |                                  |
            v                                  |
    +----------------+     setPhaseLocking(false)     +----------------+
    |    LOCKED       | ---------------------------> |    BASIC         |
    | (default state) |                              | (pre-mod path)  |
    |                 |     setPhaseLocking(true)     |                 |
    |                 | <--------------------------- |                 |
    +----------------+                               +----------------+
            |                                                |
            | wasLocked_ = true                              | wasLocked_ = false
            | phaseLockingEnabled_ = true                    | phaseLockingEnabled_ = false
            |                                                |
            +-- On frame: peak detect,                       +-- On frame: basic per-bin
                region assign,                                   phase accumulation
                two-pass synthesis                               (identical to pre-mod)
                (peaks then non-peaks)

    Transition LOCKED -> BASIC:
      wasLocked_ = true, phaseLockingEnabled_ = false
      => Re-init: synthPhase_[k] = prevPhase_[k] for all k
      => One-frame artifact acceptable

    Transition BASIC -> LOCKED:
      wasLocked_ = false, phaseLockingEnabled_ = true
      => No special handling needed
      => Rotation angle derived fresh from current frame
```

## Relationships

```
PhaseVocoderPitchShifter
    |
    |-- has many --> Peak (via isPeak_[], peakIndices_[], numPeaks_)
    |                  |
    |                  +-- owns --> Region of Influence (via regionPeak_[])
    |                                 |
    |                                 +-- covers --> All analysis bins [0, numBins-1]
    |
    |-- uses --> magnitude_[] (existing, analysis domain, input to peak detection)
    |-- uses --> prevPhase_[] (existing, analysis phases, input to rotation angle)
    |-- uses --> synthPhase_[] (existing, modified for peak bin accumulation + non-peak locked phase storage)
    |-- uses --> frequency_[] (existing, instantaneous frequency for peak bins)
    |-- uses --> wrapPhase() (existing, Layer 1 spectral_utils.h)
    |
    |-- toggles via --> phaseLockingEnabled_ (bool)
    |-- tracks transition via --> wasLocked_ (bool)
```

## Algorithm Flow (per frame)

```
processFrame(pitchRatio):

  Step 1: Analysis (EXISTING - unchanged)
  ├── Extract magnitude_[k] from analysisSpectrum_
  ├── Compute phase difference, deviation, frequency_[k]
  └── Store prevPhase_[k]

  Step 1b: Formant envelope extraction (EXISTING - unchanged, if enabled)

  Step 1c: Phase Locking Setup (NEW)
  ├── IF phaseLockingEnabled_:
  │   ├── Peak Detection: scan magnitude_[1..numBins-2] for local maxima
  │   │   └── Populate isPeak_[], peakIndices_[], numPeaks_
  │   └── Region Assignment: assign each bin to nearest peak
  │       └── Populate regionPeak_[]
  │
  └── Toggle-to-basic check:
      └── IF wasLocked_ && !phaseLockingEnabled_:
          └── Re-init: synthPhase_[k] = prevPhase_[k] for all k
      └── wasLocked_ = phaseLockingEnabled_

  Step 2: Synthesis (MODIFIED)
  ├── Reset synthesisSpectrum_
  │
  ├── IF phaseLockingEnabled_ && numPeaks_ > 0:
  │   ├── Pass 1 (peak bins):
  │   │   └── For each synthesis bin k:
  │   │       ├── Compute srcBin = k / pitchRatio
  │   │       ├── srcBinRounded = round(srcBin)
  │   │       ├── IF isPeak_[srcBinRounded]:
  │   │       │   ├── Interpolate magnitude
  │   │       │   ├── synthPhase_[k] += freq; synthPhase_[k] = wrapPhase(synthPhase_[k])
  │   │       │   ├── Set Cartesian output
  │   │       │   └── Store shiftedMagnitude_[k]
  │   │       └── ELSE: skip (processed in Pass 2)
  │   │
  │   └── Pass 2 (non-peak bins):
  │       └── For each synthesis bin k:
  │           ├── Compute srcBin = k / pitchRatio
  │           ├── srcBinRounded = round(srcBin)
  │           ├── IF NOT isPeak_[srcBinRounded]:
  │           │   ├── Interpolate magnitude
  │           │   ├── analysisPeak = regionPeak_[srcBinRounded]
  │           │   ├── synthPeakBin = round(analysisPeak * pitchRatio)
  │           │   ├── rotationAngle = synthPhase_[synthPeakBin] - prevPhase_[analysisPeak]
  │           │   ├── analysisPhaseAtSrc = interpolate prevPhase_[] at srcBin
  │           │   ├── phaseForOutput = analysisPhaseAtSrc + rotationAngle
  │           │   ├── synthPhase_[k] = phaseForOutput (for formant step compatibility)
  │           │   ├── Set Cartesian output
  │           │   └── Store shiftedMagnitude_[k]
  │           └── ELSE: skip (already processed in Pass 1)
  │
  └── ELSE (basic path - when disabled or no peaks):
      └── For each synthesis bin k:
          ├── Interpolate magnitude
          ├── synthPhase_[k] += freq; synthPhase_[k] = wrapPhase(synthPhase_[k])
          ├── Set Cartesian output
          └── Store shiftedMagnitude_[k]

  Step 3: Formant preservation (EXISTING - unchanged)
  └── Uses synthPhase_[k] for all bins (correct for both locked and basic paths)
```
