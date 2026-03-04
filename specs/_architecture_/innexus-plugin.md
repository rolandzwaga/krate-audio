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
