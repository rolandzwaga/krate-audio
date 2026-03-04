# Data Model: Live Sidechain Mode

**Feature Branch**: `117-live-sidechain-mode`
**Date**: 2026-03-04

## Entities

### InputSource (Enum)

Selector for the analysis input source.

| Value | Name | Description |
|-------|------|-------------|
| 0 | Sample | Existing precomputed sample analysis (M1-M2 behavior) |
| 1 | Sidechain | Live analysis from sidechain audio bus |

**Location**: `plugins/innexus/src/plugin_ids.h` (alongside existing enums)
**Persistence**: Saved/restored as int32 in state version 3

### LatencyMode (Enum)

Selector for analysis window configuration.

| Value | Name | Description |
|-------|------|-------------|
| 0 | LowLatency | Short window only, 15-25ms latency, min F0 ~80-100 Hz |
| 1 | HighPrecision | Dual window (short + long), 50-100ms latency, min F0 ~40 Hz |

**Location**: `plugins/innexus/src/plugin_ids.h`
**Persistence**: Saved/restored as int32 in state version 3

### New Parameter IDs

Added to `ParameterIds` enum in `plugins/innexus/src/plugin_ids.h`:

| ID | Name | Range (Normalized) | Range (Plain) | Default | Group |
|----|------|---------------------|---------------|---------|-------|
| 500 | kInputSourceId | 0.0-1.0 | 0 (Sample), 1 (Sidechain) | 0.0 (Sample) | Sidechain (500-599) |
| 501 | kLatencyModeId | 0.0-1.0 | 0 (LowLatency), 1 (HighPrecision) | 0.0 (LowLatency) | Sidechain (500-599) |

**Registration**: Both as `StringListParameter` in controller.

### SpectralCoringEstimator (New Class - Layer 2)

Lightweight residual estimation via spectral coring.

**Location**: `dsp/include/krate/dsp/processors/spectral_coring_estimator.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (processors) - depends on Layer 0 (core) and Layer 1 (primitives)

| Field | Type | Description |
|-------|------|-------------|
| fftSize_ | size_t | FFT size from STFT configuration |
| sampleRate_ | float | Current sample rate |
| numBins_ | size_t | fftSize / 2 + 1 |
| coringBandwidthBins_ | float | 1.5 (main lobe width in bins) |
| prepared_ | bool | Whether prepare() was called |

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(size_t fftSize, float sampleRate)` | Configure for given FFT parameters |
| reset | `void reset()` | Clear internal state |
| estimateResidual | `ResidualFrame estimateResidual(const SpectralBuffer& spectrum, const HarmonicFrame& frame) noexcept` | Compute residual by zeroing harmonic bins |

**Dependencies**: `residual_types.h`, `harmonic_types.h`, `spectral_buffer.h`

### LiveAnalysisPipeline (New Class - Plugin-Local)

Orchestrates the full analysis chain for real-time sidechain audio.

**Location**: `plugins/innexus/src/dsp/live_analysis_pipeline.h` (and `.cpp`)
**Namespace**: `Innexus`

| Field | Type | Description |
|-------|------|-------------|
| preProcessing_ | PreProcessingPipeline | DC block, HPF, noise gate, transient suppression |
| yin_ | YinPitchDetector | F0 tracking |
| shortStft_ | STFT | Short-window spectral analysis |
| longStft_ | STFT | Long-window spectral analysis (high-precision only) |
| shortSpectrum_ | SpectralBuffer | Output buffer for short STFT |
| longSpectrum_ | SpectralBuffer | Output buffer for long STFT |
| tracker_ | PartialTracker | Frame-to-frame partial tracking |
| modelBuilder_ | HarmonicModelBuilder | Smoothed harmonic model |
| coringEstimator_ | SpectralCoringEstimator | Residual estimation |
| yinBuffer_ | std::vector<float> | Circular buffer for YIN input |
| yinWriteIndex_ | size_t | Write position in YIN buffer |
| yinBufferFilled_ | bool | Whether buffer has enough samples |
| latencyMode_ | LatencyMode | Current mode (determines window config) |
| shortHopCounter_ | size_t | Frame counter for long-window duty cycling |
| latestFrame_ | HarmonicFrame | Most recent analysis result |
| latestResidualFrame_ | ResidualFrame | Most recent residual result |
| newFrameAvailable_ | bool | Flag: new frame since last check |
| residualEnabled_ | bool | When false, skips coringEstimator_.estimateResidual() — set by setResidualEnabled() |
| sampleRate_ | float | Current sample rate |
| prepared_ | bool | Whether prepare() was called |

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate, LatencyMode mode)` | Allocate all buffers, configure pipeline |
| reset | `void reset()` | Clear all pipeline state |
| setLatencyMode | `void setLatencyMode(LatencyMode mode)` | Reconfigure windows (may reset long STFT) |
| setResidualEnabled | `void setResidualEnabled(bool enabled) noexcept` | Enable/disable spectral coring residual computation (~10% CPU reduction when disabled) |
| pushSamples | `void pushSamples(const float* data, size_t count)` | Feed pre-processed audio into pipeline |
| hasNewFrame | `bool hasNewFrame() const noexcept` | Check if new analysis available |
| consumeFrame | `const HarmonicFrame& consumeFrame() noexcept` | Get latest frame (clears flag) |
| consumeResidualFrame | `const ResidualFrame& consumeResidualFrame() noexcept` | Get latest residual frame |

### Processor Extensions

Added fields to `Innexus::Processor`:

| Field | Type | Description |
|-------|------|-------------|
| inputSource_ | std::atomic<float> | Input source selector (normalized) |
| latencyMode_ | std::atomic<float> | Latency mode selector (normalized) |
| liveAnalysis_ | LiveAnalysisPipeline | Live analysis pipeline instance |
| sidechainBuffer_ | std::array<float, 8192> | Pre-allocated stereo-to-mono downmix buffer |
| sourceCrossfadeSamplesRemaining_ | int | Crossfade counter for source switch |
| sourceCrossfadeLengthSamples_ | int | 20ms in samples |
| sourceCrossfadeOldLevel_ | float | Captured output level at switch point |
| previousInputSource_ | int | Tracks source changes for crossfade trigger |

### State Format (Version 3)

Extends existing version 2 format:

```
[Version 2 data...]  // M1 + M2 parameters + residual frames

// M3 additions (after all version 2 data):
int32  inputSource          // 0 = Sample, 1 = Sidechain
int32  latencyMode          // 0 = LowLatency, 1 = HighPrecision
```

**Backward compatibility**: Version 2 states load with `InputSource::Sample` and `LatencyMode::LowLatency` defaults.

## Relationships

```
Processor
  |-- owns --> LiveAnalysisPipeline
  |              |-- owns --> PreProcessingPipeline
  |              |-- owns --> YinPitchDetector
  |              |-- owns --> STFT (short)
  |              |-- owns --> STFT (long, optional)
  |              |-- owns --> SpectralBuffer (short)
  |              |-- owns --> SpectralBuffer (long)
  |              |-- owns --> PartialTracker
  |              |-- owns --> HarmonicModelBuilder
  |              |-- owns --> SpectralCoringEstimator
  |-- owns --> SampleAnalyzer (existing, unchanged)
  |-- owns --> HarmonicOscillatorBank (existing, unchanged)
  |-- owns --> ResidualSynthesizer (existing, unchanged)
  |-- reads --> SampleAnalysis (existing, for sample mode)
  |-- reads --> HarmonicFrame (from live pipeline OR sample analysis)
  |-- reads --> ResidualFrame (from live pipeline OR sample analysis)
```

## Validation Rules

- InputSource must be 0 or 1
- LatencyMode must be 0 or 1
- Both parameters are discrete (step count = 1)
- SpectralCoringEstimator requires fftSize > 0 and sampleRate > 0 in prepare()
- LiveAnalysisPipeline requires sampleRate > 0 in prepare()
- Sidechain audio must be finite (no NaN/Inf) - checked by PreProcessingPipeline

## State Transitions

### Source Switch State Machine

```
[Sample Active] --user selects Sidechain--> [Crossfading to Sidechain]
    ^                                             |
    |                                      20ms crossfade completes
    |                                             v
    +--user selects Sample-- [Sidechain Active] --user selects Sample--> [Crossfading to Sample]
                                                                              |
                                                                       20ms crossfade completes
                                                                              v
                                                                        [Sample Active]
```

### Live Analysis Pipeline State Machine

```
[Unprepared] --prepare()--> [Ready]
[Ready] --pushSamples()--> [Accumulating]
[Accumulating] --STFT hop complete--> [Analyzing]
[Analyzing] --pipeline complete--> [Frame Available] --consumeFrame()--> [Accumulating]
```
