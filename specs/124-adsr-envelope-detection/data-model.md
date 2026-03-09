# Data Model: Innexus ADSR Envelope Detection

**Branch**: `124-adsr-envelope-detection` | **Date**: 2026-03-08

## Entities

### 1. DetectedADSR (new struct, analysis output)

**Location**: `plugins/innexus/src/dsp/envelope_detector.h`
**Namespace**: `Innexus`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| attackMs | float | 10.0f | Detected attack time (ms) |
| decayMs | float | 100.0f | Detected decay time (ms) |
| sustainLevel | float | 1.0f | Detected sustain level (0-1, relative to peak) |
| releaseMs | float | 100.0f | Detected release time (ms) |

**Validation**:
- attackMs: clamped to [1.0, 5000.0]
- decayMs: clamped to [1.0, 5000.0]
- sustainLevel: clamped to [0.0, 1.0]
- releaseMs: clamped to [1.0, 5000.0]

### 2. EnvelopeDetector (new class, analysis-time only)

**Location**: `plugins/innexus/src/dsp/envelope_detector.h`
**Namespace**: `Innexus`

| Method | Signature | Description |
|--------|-----------|-------------|
| detect | `DetectedADSR detect(const std::vector<Krate::DSP::HarmonicFrame>& frames, float hopTimeSec) noexcept` | Runs ADSR fitting on amplitude contour |

**Internal state**: None (stateless, pure function wrapped in class for testability).

**Algorithm**:
1. Extract `globalAmplitude` from each frame into a contour vector
2. Find peak index (max amplitude)
3. Attack = peakIndex * hopTimeSec * 1000.0f (ms)
4. From peak, run sliding window with O(1) rolling least-squares:
   - Window size is fixed at `kWindowSize` (chosen value within the valid range 8–20 frames; default 12)
   - The window grows from 0 up to `kWindowSize` frames at signal start (grow-in phase), then slides as a fixed-size window
   - Maintain: n (current window occupancy, 0 to kWindowSize), sum_x, sum_y, sum_xy, sum_x2, mean, M2 (for variance)
   - During grow-in, `n` increments each frame until it reaches `kWindowSize`; thereafter it stays constant and the oldest sample is subtracted when the newest is added
   - Steady-state when |slope| < 0.0005/frame AND variance < 0.002
5. Decay = (steady_state_start - peakIndex) * hopTimeSec * 1000.0f
6. Sustain = mean amplitude in steady region / peak amplitude
7. Release = (totalFrames - 1 - steady_state_end) * hopTimeSec * 1000.0f, or 100ms default if no second inflection is detected.
   `steady_state_end` is the index of the **last** frame in the steady-state region (inclusive, 0-based).

### 3. MemorySlot (extended struct)

**Location**: `dsp/include/krate/dsp/processors/harmonic_snapshot.h`
**Existing fields**: `HarmonicSnapshot snapshot`, `bool occupied`

**New fields**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| adsrAttackMs | float | 10.0f | Stored attack time |
| adsrDecayMs | float | 100.0f | Stored decay time |
| adsrSustainLevel | float | 1.0f | Stored sustain level |
| adsrReleaseMs | float | 100.0f | Stored release time |
| adsrAmount | float | 0.0f | Stored envelope amount |
| adsrTimeScale | float | 1.0f | Stored time scale |
| adsrAttackCurve | float | 0.0f | Stored attack curve amount |
| adsrDecayCurve | float | 0.0f | Stored decay curve amount |
| adsrReleaseCurve | float | 0.0f | Stored release curve amount |

### 4. SampleAnalysis (extended struct)

**Location**: `plugins/innexus/src/dsp/sample_analysis.h`
**Existing fields**: frames, residualFrames, sampleRate, hopTimeSec, etc.

**New field**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| detectedADSR | DetectedADSR | {} | Auto-detected envelope parameters |

### 5. New Parameters (ParameterIds enum extension)

**Location**: `plugins/innexus/src/plugin_ids.h`

| ID Name | Value | Plain Range | Normalized Range | Default (plain) | Parameter Type |
|---------|-------|-------------|------------------|------------------|----------------|
| kAdsrAttackId | 720 | 1-5000 ms | 0.0-1.0 (log) | 10.0 | RangeParameter |
| kAdsrDecayId | 721 | 1-5000 ms | 0.0-1.0 (log) | 100.0 | RangeParameter |
| kAdsrSustainId | 722 | 0.0-1.0 | 0.0-1.0 | 1.0 | RangeParameter |
| kAdsrReleaseId | 723 | 1-5000 ms | 0.0-1.0 (log) | 100.0 | RangeParameter |
| kAdsrAmountId | 724 | 0.0-1.0 | 0.0-1.0 | 0.0 | RangeParameter |
| kAdsrTimeScaleId | 725 | 0.25-4.0 | 0.0-1.0 | 1.0 | RangeParameter |
| kAdsrAttackCurveId | 726 | -1.0 to +1.0 | 0.0-1.0 | 0.0 | RangeParameter |
| kAdsrDecayCurveId | 727 | -1.0 to +1.0 | 0.0-1.0 | 0.0 | RangeParameter |
| kAdsrReleaseCurveId | 728 | -1.0 to +1.0 | 0.0-1.0 | 0.0 | RangeParameter |

**Note on normalization**: Time parameters (Attack, Decay, Release) use logarithmic mapping for perceptual uniformity:
- `normalizedToPlain: 1.0 * exp(normalized * log(5000.0/1.0))` = 1 to 5000 ms
- This matches ADSRDisplay's internal `normalizedToTimeMs` function

### 6. Processor ADSR State (new fields in Processor class)

**Location**: `plugins/innexus/src/processor/processor.h`

| Field | Type | Description |
|-------|------|-------------|
| adsrAttackMs_ | std::atomic<float> | Attack time parameter (ms) |
| adsrDecayMs_ | std::atomic<float> | Decay time parameter (ms) |
| adsrSustainLevel_ | std::atomic<float> | Sustain level parameter |
| adsrReleaseMs_ | std::atomic<float> | Release time parameter (ms) |
| adsrAmount_ | std::atomic<float> | Envelope amount parameter |
| adsrTimeScale_ | std::atomic<float> | Time scale parameter |
| adsrAttackCurve_ | std::atomic<float> | Attack curve amount |
| adsrDecayCurve_ | std::atomic<float> | Decay curve amount |
| adsrReleaseCurve_ | std::atomic<float> | Release curve amount |
| adsr_ | Krate::DSP::ADSREnvelope | Envelope generator instance |
| adsrAmountSmoother_ | Krate::DSP::OnePoleSmoother | Smooth Amount transitions |

## Relationships

```
SampleAnalyzer::analyzeOnThread()
    |
    v
EnvelopeDetector::detect(frames, hopTimeSec)
    |
    v
DetectedADSR --> stored in SampleAnalysis.detectedADSR
    |
    v
Processor::checkForNewAnalysis()
    reads DetectedADSR, sets parameter atomics
    sends IMessage to Controller to update knob positions
    |
    v
Processor::process()
    reads parameter atomics
    drives ADSREnvelope (gate on note-on/off)
    applies envelope gain to output
    |
    |--- Processor::handleNoteOn()  --> adsr_.gate(true)
    |--- Processor::handleNoteOff() --> adsr_.gate(false)
    |
    v
MemorySlot (capture/recall)
    stores/restores all 9 ADSR parameter values

EvolutionEngine::getInterpolatedFrame()
    reads MemorySlot ADSR fields
    returns interpolated ADSR values alongside harmonic data

Controller
    registers 9 RangeParameters
    creates ADSRDisplay via createCustomView()
    wires ADSRDisplay to parameter IDs
```

## State Transitions

### ADSR Envelope State Machine (from ADSREnvelope class)

```
         gate(true)           attack complete
[Idle] ──────────> [Attack] ──────────────> [Decay]
  ^                   ^                        |
  |                   |                        | decay reaches sustain
  |                   |  gate(true)            v
  |                   +──────────── [Sustain] ───> [Release] ──> [Idle]
  |                   (hard retrigger)              gate(false)
  |                                                     |
  +─────────────────────────────────────────────────────+
                     release complete
```

### Envelope Detection State (analysis-time only)

```
[No Analysis] ──load sample──> [Analyzing] ──complete──> [Detection Complete]
                                                              |
                                                              v
                                                    [ADSR params auto-populated]
```
