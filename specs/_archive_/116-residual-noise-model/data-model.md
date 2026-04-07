# Data Model: Innexus Milestone 2 -- Residual/Noise Model

## Entities

### ResidualFrame (NEW)

**Location**: `dsp/include/krate/dsp/processors/residual_types.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (Processors)

A per-frame representation of the stochastic (non-harmonic) component of the analyzed signal.

| Field | Type | Description | Constraints |
|-------|------|-------------|-------------|
| `bandEnergies` | `std::array<float, kResidualBands>` | Spectral envelope: RMS energy per frequency band | All values >= 0.0 |
| `totalEnergy` | `float` | Overall residual energy: RMS of the residual magnitude spectrum (`sqrt(sum(mag²)/numBins)`) | >= 0.0 |
| `transientFlag` | `bool` | True if a transient was detected in this frame | -- |

**Constants**:
- `kResidualBands = 16` -- number of spectral envelope breakpoints

**Validation**:
- All `bandEnergies` values must be >= 0.0 (clamped from spectral subtraction artifacts)
- `totalEnergy` must be >= 0.0
- Default-constructed ResidualFrame has all zeros and `transientFlag = false`

**Relationships**:
- Produced by `ResidualAnalyzer` during sample analysis (1 per analysis frame)
- Consumed by `ResidualSynthesizer` during real-time playback
- Stored in `SampleAnalysis::residualFrames` alongside `SampleAnalysis::frames` (HarmonicFrame)
- Time-aligned: `residualFrames[i]` corresponds to `frames[i]` for all i

### SampleAnalysis (EXTENDED)

**Location**: `plugins/innexus/src/dsp/sample_analysis.h`
**Namespace**: `Innexus`

Extended to include residual analysis data.

| Field | Type | Description | New? |
|-------|------|-------------|------|
| `frames` | `std::vector<Krate::DSP::HarmonicFrame>` | Time-indexed harmonic frames | Existing |
| `residualFrames` | `std::vector<Krate::DSP::ResidualFrame>` | Time-indexed residual frames | NEW |
| `sampleRate` | `float` | Source sample rate (Hz) | Existing |
| `hopTimeSec` | `float` | Time between frames (seconds) | Existing |
| `totalFrames` | `size_t` | Number of frames | Existing |
| `filePath` | `std::string` | Source file path | Existing |
| `analysisFFTSize` | `size_t` | Short-window FFT size used during analysis | NEW |
| `analysisHopSize` | `size_t` | Short-window hop size used during analysis | NEW |

**Invariants**:
- `residualFrames.size() == frames.size()` (always, after M2 analysis)
- `residualFrames.size() == 0` for M1-only analysis results (backward compatible)
- `totalFrames == frames.size()`

**New method**:
```cpp
[[nodiscard]] const Krate::DSP::ResidualFrame& getResidualFrame(size_t index) const noexcept;
```
Returns the residual frame at `index`, clamped to valid range. Returns a default (silent) frame if `residualFrames` is empty (M1 backward compatibility).

### Parameter IDs (EXTENDED)

**Location**: `plugins/innexus/src/plugin_ids.h`
**Namespace**: `Innexus`

| Parameter | ID | Range (Normalized) | Range (Plain) | Default (Plain) | Default (Normalized) |
|-----------|----|--------------------|---------------|-----------------|----------------------|
| `kHarmonicLevelId` | 400 | 0.0 - 1.0 | 0.0 - 2.0 | 1.0 | 0.5 |
| `kResidualLevelId` | 401 | 0.0 - 1.0 | 0.0 - 2.0 | 1.0 | 0.5 |
| `kResidualBrightnessId` | 402 | 0.0 - 1.0 | -1.0 - +1.0 | 0.0 (neutral) | 0.5 |
| `kTransientEmphasisId` | 403 | 0.0 - 1.0 | 0.0 - 2.0 | 0.0 (no boost) | 0.0 |

**Mapping formulas**:
- Harmonic Level: `plain = normalized * 2.0`; `normalized = plain / 2.0`
- Residual Level: `plain = normalized * 2.0`; `normalized = plain / 2.0`
- Brightness: `plain = normalized * 2.0 - 1.0` (plain range -1.0 to +1.0, dimensionless tilt ratio); `normalized = (plain + 1.0) / 2.0`
- Transient Emphasis: `plain = normalized * 2.0` (plain range 0.0 to 2.0; energy multiplier = `1.0 + plain`); `normalized = plain / 2.0`

### State Persistence Format (EXTENDED)

**Version 2 format** (version 1 = M1, version 2 = M2):

```
[int32]  version = 2
-- M1 parameters (unchanged) --
[float]  releaseTimeMs
[float]  inharmonicityAmount
[float]  masterGain
[float]  bypass
[int32]  pathLen
[bytes]  filePath (pathLen bytes)
-- M2 parameters (new) --
[float]  harmonicLevel       (default 1.0 if missing)
[float]  residualLevel       (default 1.0 if missing)
[float]  residualBrightness  (default 0.0 if missing)
[float]  transientEmphasis   (default 0.0 if missing)
-- M2 residual frames (new) --
[int32]  residualFrameCount
[int32]  analysisFFTSize
[int32]  analysisHopSize
For each frame:
  [float * 16]  bandEnergies
  [float]       totalEnergy
  [int8]        transientFlag
```

**Backward compatibility**:
- Loading version 1 state: new parameters get defaults, residualFrames remains empty
- Loading version 2 state: all data restored, residual playback resumes without re-analysis

## Entity Relationship Diagram

```
+-------------------+     1:N     +------------------+
| SampleAnalysis    |------------>| HarmonicFrame    |
|                   |             | (existing M1)    |
|  sampleRate       |             +------------------+
|  hopTimeSec       |
|  totalFrames      |     1:N     +------------------+
|  filePath         |------------>| ResidualFrame    |
|  analysisFFTSize  |             | (NEW M2)         |
|  analysisHopSize  |             +------------------+
+-------------------+
        |
        | owned by (shared_ptr)
        v
+-------------------+
| Innexus::Processor|
|                   |
| oscillatorBank_   |----> HarmonicOscillatorBank (existing)
| residualSynth_    |----> ResidualSynthesizer (NEW)
| harmonicLevel_    |
| residualLevel_    |
| brightness_       |
| transientEmph_    |
+-------------------+
```

## State Transitions

### Analysis Pipeline (Background Thread)

```
IDLE
  |
  v  [loadSample() called]
ANALYZING_HARMONICS (existing M1 pipeline)
  |
  v  [harmonic analysis complete for current frame]
ANALYZING_RESIDUAL (NEW: subtraction + spectral envelope)
  |
  v  [both complete for current frame, loop to next]
ANALYZING_HARMONICS (next frame)
  ...
  v  [all frames processed]
COMPLETE (result_ published with both harmonic + residual frames)
```

Note: The residual analysis is interleaved with harmonic analysis on a per-frame basis within the existing `analyzeOnThread()` loop, not as a separate pass.

### Synthesis Pipeline (Audio Thread)

```
SILENT (no analysis loaded or no note active)
  |
  v  [analysis loaded + note-on]
PLAYING
  |  per sample:
  |    1. Advance frame counter
  |    2. At frame boundary: load HarmonicFrame + ResidualFrame
  |    3. Generate oscillator bank sample
  |    4. Generate residual synthesizer sample (from OverlapAdd buffer)
  |    5. output = harmonicSample * harmonicLevel + residualSample * residualLevel
  |
  v  [note-off]
RELEASING (existing M1 release envelope applies to combined output)
  |
  v  [release complete]
SILENT
```
