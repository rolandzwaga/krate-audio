# Data Model: Innexus Milestone 1 -- Core Playable Instrument

**Date**: 2026-03-03 | **Branch**: `115-innexus-m1-core-instrument`

## Entity Definitions

### F0Estimate

**Location**: `dsp/include/krate/dsp/processors/harmonic_types.h`
**Layer**: 2 (Processors)

The output of the YIN pitch detector for one analysis frame.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| frequency | float | >= 0.0 Hz (0 = unvoiced) | Estimated fundamental frequency |
| confidence | float | [0.0, 1.0] | Detection confidence |
| voiced | bool | -- | confidence > threshold |

**Validation Rules**:
- frequency must be 0 when voiced is false
- confidence must be in [0.0, 1.0]

---

### Partial

**Location**: `dsp/include/krate/dsp/processors/harmonic_types.h`
**Layer**: 2 (Processors)

A single tracked harmonic component within a HarmonicFrame.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| harmonicIndex | int | [0, 48] (0 = unassigned) | 1-based harmonic number |
| frequency | float | > 0.0 Hz | Measured frequency (actual, not idealized) |
| amplitude | float | >= 0.0 | Linear amplitude |
| phase | float | [-pi, pi] | Phase in radians |
| relativeFrequency | float | > 0.0 | frequency / F0 ratio |
| inharmonicDeviation | float | any | relativeFrequency - harmonicIndex |
| stability | float | [0.0, 1.0] | Tracking confidence |
| age | int | >= 0 | Frames since track birth |

**Validation Rules**:
- relativeFrequency = frequency / f0 (computed, not stored independently)
- inharmonicDeviation = relativeFrequency - harmonicIndex
- stability decays when partial is unmatched, resets on re-match
- age increments each frame; resets to 0 on birth

---

### HarmonicFrame

**Location**: `dsp/include/krate/dsp/processors/harmonic_types.h`
**Layer**: 2 (Processors)

A snapshot of the harmonic analysis at one point in time. The fundamental data unit flowing from analysis to synthesis.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| f0 | float | >= 0.0 Hz | Fundamental frequency |
| f0Confidence | float | [0.0, 1.0] | From YIN detector |
| partials | std::array<Partial, 48> | fixed size 48 | Active partials |
| numPartials | int | [0, 48] | Active partial count |
| spectralCentroid | float | >= 0.0 Hz | Amplitude-weighted mean frequency |
| brightness | float | >= 0.0 | Perceptual brightness descriptor |
| noisiness | float | [0.0, 1.0] | Ratio of residual to harmonic energy |
| globalAmplitude | float | >= 0.0 | Smoothed RMS of source |

**Relationships**:
- Contains up to 48 Partial instances
- Produced by HarmonicModelBuilder from PartialTracker output + F0Estimate
- Consumed by HarmonicOscillatorBank for synthesis
- Stored in sequence within SampleAnalysis

---

### SampleAnalysis

**Location**: `plugins/innexus/src/dsp/sample_analysis.h`
**Layer**: Plugin-local

The stored result of analyzing a loaded audio file. Immutable after publication.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| frames | std::vector<HarmonicFrame> | non-empty after analysis | Time-indexed sequence |
| sampleRate | float | > 0 | Source sample rate |
| hopTimeSec | float | > 0 | Time between frames |
| totalFrames | size_t | > 0 | Number of frames |
| filePath | std::string | valid path | For state persistence |

**Lifecycle**:
1. Allocated and populated by SampleAnalyzer on background thread
2. Published to audio thread via std::atomic<SampleAnalysis*> with release semantics
3. Audio thread reads with acquire semantics
4. Immutable after publication -- never modified
5. Deleted when replaced by a new analysis (deferred deletion, not on audio thread)

**State Transitions**:
```
[Not Loaded] --load file--> [Analyzing] --complete--> [Ready]
[Ready] --load new file--> [Analyzing] --complete--> [Ready]
[Analyzing] --cancel--> [Not Loaded]
```

---

## Component Relationships

```
PreProcessingPipeline
  |-- DCBlocker2 (reuse)
  |-- Biquad (reuse, HPF at 30 Hz)
  |-- EnvelopeFollower x2 (reuse, for transient detection)
  +-- NoiseGate (simple threshold, inline)

YinPitchDetector
  |-- FFT (reuse, for difference function acceleration)
  +-- produces: F0Estimate

STFT (reuse, 2 instances)
  |-- Long window (4096, BlackmanHarris)
  |-- Short window (1024, BlackmanHarris)
  +-- produces: SpectralBuffer

PartialTracker
  |-- consumes: SpectralBuffer, F0Estimate
  |-- uses: spectral_utils (bin/freq conversion, parabolic interpolation)
  +-- produces: tracked Partial array

HarmonicModelBuilder
  |-- consumes: Partial array, F0Estimate
  |-- uses: OnePoleSmoother (for dual-timescale blending)
  +-- produces: HarmonicFrame

SampleAnalyzer (plugin-local)
  |-- uses: PreProcessingPipeline, YinPitchDetector, STFT x2, PartialTracker, HarmonicModelBuilder
  |-- uses: dr_wav (file loading)
  +-- produces: SampleAnalysis

HarmonicOscillatorBank
  |-- consumes: HarmonicFrame
  |-- 48 MCF oscillators (SoA layout)
  +-- produces: audio samples

Processor (plugin-local)
  |-- owns: HarmonicOscillatorBank
  |-- reads: SampleAnalysis (via atomic pointer)
  |-- handles: MIDI note-on/off, velocity, pitch bend
  +-- produces: stereo audio output
```

## Parameter Model

| Parameter ID | Name | Range | Default | VST Normalized | Description |
|-------------|------|-------|---------|----------------|-------------|
| 0 | Bypass | 0/1 | 0 | 0.0/1.0 | Bypass toggle |
| 1 | Master Gain | 0-1 | 1.0 | 0.0-1.0 | Output level |
| 200 | Release Time | 20-5000ms | 100ms | normalized | Note-off release. Range minimum 20ms equals the anti-click floor (FR-057); any user value below 20ms would be clamped to 20ms, so 20ms is the effective minimum. |
| 201 | Inharmonicity | 0-100% | 100% | 0.0-1.0 | Harmonic vs source ratio |
