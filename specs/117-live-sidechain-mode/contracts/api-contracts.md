# API Contracts: Live Sidechain Mode

**Feature Branch**: `117-live-sidechain-mode`
**Date**: 2026-03-04

## VST3 Bus Configuration

### Sidechain Audio Input Bus

```cpp
// In Processor::initialize()
addAudioInput(
    STR16("Sidechain"),
    Steinberg::Vst::SpeakerArr::kStereo,
    Steinberg::Vst::BusTypes::kAux,
    Steinberg::Vst::BusInfo::kDefaultActive
);
```

**Host interaction**:
- Host sees this as a sidechain input in its routing UI
- Host may provide stereo, mono, or no audio (bus inactive)
- Plugin handles all three cases gracefully

### Updated Bus Arrangements

```cpp
// Accept: 0 or 1 input, 1 stereo output
tresult Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numOuts != 1 || outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;
    if (numIns == 0)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    if (numIns == 1 && (inputs[0] == SpeakerArr::kStereo || inputs[0] == SpeakerArr::kMono))
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}
```

## Parameter Registration Contracts

### Input Source Selector (kInputSourceId = 500)

```cpp
// In Controller::initialize()
auto* inputSourceParam = new Steinberg::Vst::StringListParameter(
    STR16("Input Source"), kInputSourceId, nullptr,
    Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
inputSourceParam->appendString(STR16("Sample"));
inputSourceParam->appendString(STR16("Sidechain"));
parameters.addParameter(inputSourceParam);
```

### Latency Mode Selector (kLatencyModeId = 501)

```cpp
// In Controller::initialize()
auto* latencyModeParam = new Steinberg::Vst::StringListParameter(
    STR16("Latency Mode"), kLatencyModeId, nullptr,
    Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
latencyModeParam->appendString(STR16("Low Latency"));
latencyModeParam->appendString(STR16("High Precision"));
parameters.addParameter(latencyModeParam);
```

## SpectralCoringEstimator API

**File**: `dsp/include/krate/dsp/processors/spectral_coring_estimator.h`
**Namespace**: `Krate::DSP`

```cpp
class SpectralCoringEstimator {
public:
    SpectralCoringEstimator() = default;
    ~SpectralCoringEstimator() = default;

    // Non-copyable, movable
    SpectralCoringEstimator(const SpectralCoringEstimator&) = delete;
    SpectralCoringEstimator& operator=(const SpectralCoringEstimator&) = delete;
    SpectralCoringEstimator(SpectralCoringEstimator&&) noexcept;
    SpectralCoringEstimator& operator=(SpectralCoringEstimator&&) noexcept;

    /// Configure for the given FFT parameters.
    /// @param fftSize FFT size (must be power of 2)
    /// @param sampleRate Sample rate in Hz
    void prepare(size_t fftSize, float sampleRate);

    /// Reset internal state.
    void reset();

    /// Estimate residual by zeroing harmonic bins in the spectrum.
    /// @param spectrum STFT spectral buffer with magnitude data
    /// @param frame Harmonic frame identifying partial frequencies
    /// @return ResidualFrame with band energies from non-harmonic bins
    [[nodiscard]] ResidualFrame estimateResidual(
        const SpectralBuffer& spectrum,
        const HarmonicFrame& frame) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }
};
```

## LiveAnalysisPipeline API

**File**: `plugins/innexus/src/dsp/live_analysis_pipeline.h`
**Namespace**: `Innexus`

```cpp
class LiveAnalysisPipeline {
public:
    LiveAnalysisPipeline() = default;
    ~LiveAnalysisPipeline() = default;

    // Non-copyable, movable
    LiveAnalysisPipeline(const LiveAnalysisPipeline&) = delete;
    LiveAnalysisPipeline& operator=(const LiveAnalysisPipeline&) = delete;
    LiveAnalysisPipeline(LiveAnalysisPipeline&&) noexcept = default;
    LiveAnalysisPipeline& operator=(LiveAnalysisPipeline&&) noexcept = default;

    /// Allocate all buffers and configure pipeline components.
    /// @param sampleRate Current sample rate
    /// @param mode Latency mode determining window configuration
    void prepare(double sampleRate, LatencyMode mode);

    /// Clear all pipeline state. Call when switching sources or on reset.
    void reset();

    /// Reconfigure for a different latency mode.
    /// @param mode New latency mode
    void setLatencyMode(LatencyMode mode);

    /// Enable or disable spectral coring residual computation.
    /// When disabled, coringEstimator_.estimateResidual() is skipped in pushSamples().
    /// Call from Processor::process() when residual level parameter is 0.0f.
    /// ~10% CPU reduction when disabled.
    void setResidualEnabled(bool enabled) noexcept;

    /// Feed mono sidechain audio samples into the pipeline.
    /// Internally accumulates and triggers analysis when enough data available.
    /// @param data Pointer to mono audio samples
    /// @param count Number of samples
    void pushSamples(const float* data, size_t count);

    /// Check if a new analysis frame is available since last consume.
    [[nodiscard]] bool hasNewFrame() const noexcept;

    /// Get the latest harmonic frame and clear the new-frame flag.
    [[nodiscard]] const Krate::DSP::HarmonicFrame& consumeFrame() noexcept;

    /// Get the latest residual frame.
    [[nodiscard]] const Krate::DSP::ResidualFrame& consumeResidualFrame() noexcept;

    /// Check if pipeline is prepared and ready to accept samples.
    [[nodiscard]] bool isPrepared() const noexcept;
};
```

## Processor Process Flow Contract

### process() flow with sidechain mode:

```
1. processParameterChanges()
2. checkForNewAnalysis()          // existing: check SampleAnalyzer
3. detectSourceSwitch()           // NEW: check if inputSource changed
4. if (sidechain mode active):
     a. downmixSidechainToMono()  // NEW: stereo -> mono
     b. liveAnalysis_.pushSamples()
     c. if liveAnalysis_.hasNewFrame():
          update currentLiveFrame_
          update currentLiveResidualFrame_
5. processEvents()                // MIDI handling
6. for each sample:
     a. select frame source (sample vs live, with crossfade)
     b. generate oscillator bank output
     c. generate residual output
     d. apply crossfade if switching
     e. apply velocity, release, master gain
     f. write to output
```

## State Persistence Contract (Version 3)

### getState() additions (after version 2 data):

```cpp
// M3 parameters
streamer.writeInt32(static_cast<int32>(
    inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0));
streamer.writeInt32(static_cast<int32>(
    latencyMode_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0));
```

### setState() additions (when version >= 3):

```cpp
int32 inputSourceInt = 0;
int32 latencyModeInt = 0;
if (streamer.readInt32(inputSourceInt))
    inputSource_.store(inputSourceInt > 0 ? 1.0f : 0.0f);
if (streamer.readInt32(latencyModeInt))
    latencyMode_.store(latencyModeInt > 0 ? 1.0f : 0.0f);
```

### Backward compatibility:

```cpp
if (version < 3) {
    // Default to sample mode, low latency
    inputSource_.store(0.0f);   // Sample
    latencyMode_.store(0.0f);   // LowLatency
}
```
