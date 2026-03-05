# Innexus Plugin Architecture

[<- Back to Architecture Index](README.md)

**Plugin**: Innexus (Harmonic Analysis/Synthesis) | **Location**: `plugins/innexus/`

---

## Overview

Innexus is a harmonic analysis/resynthesis plugin that analyzes audio samples (or live sidechain input) into harmonic partial data and resynthesizes them via an oscillator bank and residual noise synthesizer. MIDI input controls which pitches are played using the analyzed timbral data.

---

## VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/innexus/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/innexus/src/controller/` | UI state management |
| Entry | `plugins/innexus/src/entry.cpp` | Factory registration |
| IDs | `plugins/innexus/src/plugin_ids.h` | Parameter and component IDs |

---

## Audio Bus Configuration

| Bus | Type | Channels | Purpose |
|-----|------|----------|---------|
| Audio Output | `kMain` | Stereo | Synthesized audio output |
| MIDI Input | Event | N/A | Note on/off, pitch bend |
| Sidechain Input | `kAux` | Stereo | Live audio input for real-time analysis (bus index 0). Hosts route sidechain audio to this bus. The plugin downmixes stereo to mono internally before feeding the analysis pipeline. Registered in `Processor::initialize()` via `addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, BusTypes::kAux, BusInfo::kDefaultActive)`. |

---

## Plugin-Local DSP Components

### LiveAnalysisPipeline
**Path:** [live_analysis_pipeline.h](../../plugins/innexus/src/dsp/live_analysis_pipeline.h) / [live_analysis_pipeline.cpp](../../plugins/innexus/src/dsp/live_analysis_pipeline.cpp) | **Since:** 0.12.0

Real-time analysis pipeline for sidechain audio. Orchestrates the full analysis chain: PreProcessingPipeline, YinPitchDetector, STFT (short and optionally long window), PartialTracker, HarmonicModelBuilder, and SpectralCoringEstimator. Processes live audio incrementally into HarmonicFrame + ResidualFrame data that drives the existing oscillator bank and residual synthesizer.

```cpp
namespace Innexus {

class LiveAnalysisPipeline {
    // Lifecycle
    void prepare(double sampleRate, LatencyMode mode);
    void reset();
    void setLatencyMode(LatencyMode mode);
    void setResidualEnabled(bool enabled) noexcept;
    void setResponsiveness(float value) noexcept;  // M4: forwards to HarmonicModelBuilder

    // Audio feeding (real-time safe, called from audio thread)
    void pushSamples(const float* data, size_t count);

    // Frame retrieval
    [[nodiscard]] bool hasNewFrame() const noexcept;
    [[nodiscard]] const Krate::DSP::HarmonicFrame& consumeFrame() noexcept;
    [[nodiscard]] const Krate::DSP::ResidualFrame& consumeResidualFrame() noexcept;

    // Query
    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace Innexus
```

**When to use:**
- Live sidechain mode: real-time audio-to-HarmonicFrame conversion from external audio input
- Any scenario requiring incremental (streaming) harmonic analysis on the audio thread
- Phase 21 (Multi-Source Blending): one instance per live source

**Instantiability note:** Designed for multiple simultaneous instances. No singletons, no static state. Each instance owns its own pipeline components and buffers. Safe for Phase 21 multi-source blending where multiple live analysis pipelines run in parallel.

**Latency modes:**
- `LowLatency`: Short STFT window only (<= 25ms analysis-to-synthesis latency)
- `HighPrecision`: Dual STFT windows (short + long) for better low-frequency resolution (~50-100ms latency)

**Key constraint:** Real-time safe. All buffers (YIN circular buffer, pre-processing buffer, spectral buffers) are pre-allocated during `prepare()`. No memory allocations, locks, exceptions, or I/O in `pushSamples()` or `runAnalysis()`.

**Dependencies:** Plugin-local (PreProcessingPipeline, dual_stft_config.h, plugin_ids.h), Shared DSP Library (YinPitchDetector, STFT, SpectralBuffer, PartialTracker, HarmonicModelBuilder, SpectralCoringEstimator)

---

### SampleAnalyzer
**Path:** [sample_analyzer.h](../../plugins/innexus/src/dsp/sample_analyzer.h) | **Since:** 0.11.0

Offline analysis of loaded audio samples into HarmonicFrame sequences. Runs on a background thread (not audio thread). Produces the same HarmonicFrame format as LiveAnalysisPipeline but processes entire samples at once rather than incrementally.

---

### PreProcessingPipeline
**Path:** [pre_processing_pipeline.h](../../plugins/innexus/src/dsp/pre_processing_pipeline.h) | **Since:** 0.11.0

Audio conditioning pipeline (DC blocking, normalization, pre-emphasis) shared by both SampleAnalyzer and LiveAnalysisPipeline. Processes audio in-place.

---

## M4 Musical Control Layer (Freeze, Morph, Harmonic Filter, Responsiveness)
**Since:** M4 (118-musical-control-layer)

Processor extension that inserts a creative control pipeline between the analysis output (HarmonicFrame + ResidualFrame) and the oscillator bank / residual synthesizer. Provides four user-facing parameters: Freeze, Morph Position, Harmonic Filter Type, and Responsiveness.

### Signal Chain Position

```
Analysis Output (HarmonicFrame + ResidualFrame)
    |
    v
[Manual Freeze Gate] --- captures State A (frozen snapshot) on engage
    |                     10ms crossfade on disengage
    v
[Confidence-Gated Auto-Freeze] (existing M1 mechanism, bypassed during manual freeze)
    |
    v
[Morph Interpolation] --- lerpHarmonicFrame/lerpResidualFrame between
    |                      frozen State A and live State B
    v
[Harmonic Filter] --- applyHarmonicMask with pre-computed filter mask
    |                  residual passes through unmodified
    v
Oscillator Bank + Residual Synthesizer (existing)
```

### Parameters (IDs 300-303)

| Parameter | ID | Type | Range | Default |
|-----------|-----|------|-------|---------|
| Freeze | `kFreezeId` (300) | Toggle (stepCount=1) | 0/1 | 0 (off) |
| Morph Position | `kMorphPositionId` (301) | RangeParameter | 0.0 - 1.0 | 0.0 |
| Harmonic Filter | `kHarmonicFilterTypeId` (302) | StringListParameter | 5 presets | AllPass |
| Responsiveness | `kResponsivenessId` (303) | RangeParameter | 0.0 - 1.0 | 0.5 |

### Manual Freeze State (Separate from Auto-Freeze)

Manual freeze uses its own member variables (`manualFreezeActive_`, `manualFrozenFrame_`, `manualFrozenResidualFrame_`), entirely independent of the confidence-gated auto-freeze mechanism (`isFrozen_`, `lastGoodFrame_`). When manual freeze is active, it takes priority over auto-freeze. This separation prevents interference between the two mechanisms (manual = creative intent, auto = stability fallback).

On freeze disengage, a 10ms crossfade smooths the transition from frozen to live frames, preventing audible clicks.

### Morph Interpolation Pipeline

When manual freeze is active, the morph position (smoothed via `OnePoleSmoother` at ~5-10ms time constant) controls linear interpolation between the frozen State A and the current live State B using `lerpHarmonicFrame()` and `lerpResidualFrame()` from `harmonic_frame_utils.h` (shared DSP Layer 2). When freeze is off, morph has no effect (live frame passes through unmodified).

### Harmonic Filter Mask Pre-Computation

The filter mask (`std::array<float, kMaxPartials>`) is recomputed via `computeHarmonicMask()` only when the filter type parameter changes, not every frame. The pre-computed mask is applied to the morphed frame via `applyHarmonicMask()` each frame. The residual component passes through unmodified (harmonic filter only affects partials). The oscillator bank's existing ~2ms per-partial amplitude smoothing handles transitions when the filter type changes.

### Responsiveness Forwarding

The Responsiveness parameter is forwarded to `LiveAnalysisPipeline::setResponsiveness()`, which in turn calls `HarmonicModelBuilder::setResponsiveness()`. This controls the dual-timescale blend between fast-tracking and slow-stable partial estimation. Value 0.0 = slow/stable, 1.0 = fast/responsive, 0.5 = default (matches M1/M3 behavior). Only affects live sidechain mode; has no effect in sample playback mode.

### State Persistence (Version 4)

State version bumped from 3 (M3) to 4. Four new fields appended after M3 data in byte order: `int8 freeze`, `float morphPosition`, `int32 filterType`, `float responsiveness`. Loading a v3 state applies M4 defaults (freeze=off, morph=0.0, filter=AllPass, responsiveness=0.5). Backward compatible.

### Key Design Patterns

- **Pre-allocated storage**: All frozen frames, morph intermediates, and filter masks are member variables. Zero heap allocations on the audio thread.
- **Atomic parameter exchange**: All four parameters use `std::atomic<float>` for thread-safe communication from `processParameterChanges()` to `process()`.
- **Frame-rate processing**: Freeze/morph/filter operate per analysis frame (~86 Hz at 44.1kHz/512 hop), not per audio sample. Combined overhead is negligible (<0.1% CPU).
- **Smooth transitions**: Morph position smoothed via OnePoleSmoother; filter changes handled by oscillator bank's amplitude smoothing; freeze disengage uses explicit 10ms crossfade.
