# Research: Live Sidechain Mode

**Feature Branch**: `117-live-sidechain-mode`
**Date**: 2026-03-04

## R-001: VST3 Sidechain Bus Registration

**Decision**: Use `addAudioInput()` with `Steinberg::Vst::BusTypes::kAux` and `SpeakerArr::kStereo` to register the sidechain bus.

**Rationale**: The VST3 SDK distinguishes between main and auxiliary input buses. The first `addAudioInput()` call creates the main input bus (bus index 0); the second call creates an auxiliary bus (bus index 1). Since Innexus is an instrument plugin (no main audio input), the sidechain will be bus index 0 as an auxiliary input, or we can register it as a second input if we add a main audio input first. However, instrument plugins typically have zero main audio inputs. The correct approach for an instrument needing a sidechain is to add a single auxiliary audio input.

In `Processor::initialize()`:
```cpp
addAudioInput(STR16("Sidechain"), Steinberg::Vst::SpeakerArr::kStereo,
              Steinberg::Vst::BusTypes::kAux, Steinberg::Vst::BusInfo::kDefaultActive);
```

This must be the ONLY audio input bus (no main input needed since this is an instrument). The bus is defaulted to active so hosts enable routing by default. The bus can be deactivated by the host if not needed.

**Alternatives considered**:
- `BusTypes::kMain`: Incorrect for optional sidechain; main implies required audio pass-through.
- `SpeakerArr::kMono`: Limits host routing flexibility; stereo bus with internal downmix is more compatible.

**VST3 SDK Reference**: `AudioEffect::addAudioInput()` in `public.sdk/source/vst/vstaudioeffect.cpp`. The `BusTypes::kAux` flag identifies this as a sidechain input to the host's routing UI.

## R-002: Accessing Sidechain Audio in process()

**Decision**: Access sidechain audio from `data.inputs[0]` (first and only audio input bus) in `process()`. Downmix stereo to mono with `(L + R) * 0.5f`.

**Rationale**: Since Innexus has no main audio input (it's an instrument), the auxiliary sidechain bus is at input bus index 0. The `data.inputs` array contains active input buses. If the bus is inactive or not connected, `data.numInputs` will be 0 and we skip sidechain processing entirely.

**Critical implementation detail**: Must check `data.numInputs > 0` and `data.inputs[0].numChannels >= 1` before accessing. If the host provides only mono, use single channel directly. If stereo, average both channels.

```cpp
// In process():
const float* sidechainMono = nullptr;
std::array<float, maxBlockSize> sidechainBuffer;
if (inputSource == InputSource::Sidechain && data.numInputs > 0) {
    const auto& scBus = data.inputs[0];
    if (scBus.numChannels >= 2) {
        // Stereo downmix
        for (int s = 0; s < data.numSamples; ++s) {
            sidechainBuffer[s] = (scBus.channelBuffers32[0][s] +
                                  scBus.channelBuffers32[1][s]) * 0.5f;
        }
        sidechainMono = sidechainBuffer.data();
    } else if (scBus.numChannels == 1) {
        sidechainMono = scBus.channelBuffers32[0];
    }
}
```

**Alternatives considered**:
- Using `data.inputs[1]` (second bus): Wrong because instrument has no main input bus.
- Direct pointer without copy: Stereo downmix requires a copy; mono can use direct pointer.

## R-003: setBusArrangements with Sidechain

**Decision**: Update `setBusArrangements()` to accept the sidechain bus. Accept stereo sidechain and stereo output. Also accept the case where host provides mono sidechain.

**Rationale**: The current implementation rejects all input buses (`numIns == 0`). Must change to accept 1 input (the sidechain) with stereo or mono arrangement.

```cpp
tresult Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    // Accept: 0 or 1 input (sidechain optional), 1 stereo output
    if (numOuts != 1 || outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;

    if (numIns == 0)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);

    if (numIns == 1 &&
        (inputs[0] == SpeakerArr::kStereo || inputs[0] == SpeakerArr::kMono))
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);

    return kResultFalse;
}
```

## R-004: Live Analysis Pipeline Architecture

**Decision**: Create a `LiveAnalysisPipeline` class in `plugins/innexus/src/dsp/` that composes the existing pipeline components (PreProcessingPipeline, YinPitchDetector, STFT, PartialTracker, HarmonicModelBuilder) and runs them incrementally on audio-thread sample blocks.

**Rationale**: The existing `SampleAnalyzer` runs the same pipeline components but on a background thread with the entire audio file available. The live pipeline must:
1. Accept small blocks of samples (host buffer size, typically 64-512)
2. Accumulate internally until enough data for analysis (STFT hop size)
3. Run the pipeline stages when triggered
4. Output HarmonicFrame and ResidualFrame incrementally

The pipeline is stateful: STFT accumulates samples, YIN needs a window of recent samples, partial tracker maintains track history. All of these components already handle incremental feeding via their existing APIs (`STFT::pushSamples()`, `STFT::canAnalyze()`).

**Key design decisions**:
- Owns instances of all pipeline components (PreProcessingPipeline, YinPitchDetector, 1 or 2 STFTs, PartialTracker, HarmonicModelBuilder, SpectralCoringEstimator)
- `prepare(sampleRate, latencyMode)` configures window sizes based on mode
- `pushSamples(data, count)` feeds audio into the pipeline
- `hasNewFrame()` returns true when a new analysis frame is available
- `consumeFrame()` returns the most recent HarmonicFrame (and clears the new-frame flag)
- `consumeResidualFrame()` returns the most recent ResidualFrame
- All internal buffers pre-allocated in `prepare()`

**Alternatives considered**:
- Refactoring `SampleAnalyzer` to share code: Too complex; SampleAnalyzer runs on a background thread with file I/O. The shared components are already in the DSP library. The live pipeline is a new composition.
- Running analysis in a separate method called from process(): This IS what we do, just encapsulated in LiveAnalysisPipeline.

## R-005: Spectral Coring Residual Estimation

**Decision**: Create a `SpectralCoringEstimator` class in `dsp/include/krate/dsp/processors/` (Layer 2) that produces ResidualFrame output from STFT spectral data + HarmonicFrame, without requiring original audio subtraction.

**Rationale**: The existing `ResidualAnalyzer` uses time-domain subtraction (resynthesize harmonics, subtract from original, FFT the residual). This requires a full FFT frame of original audio aligned with the harmonic model. For live mode, this introduces one-frame latency (must wait for harmonic resynthesis).

Spectral coring is simpler:
1. Take the STFT magnitude spectrum
2. Zero out bins near harmonic frequencies (identified from the HarmonicFrame partials)
3. Measure energy in remaining bins per frequency band
4. Produce a ResidualFrame with the same format as the existing analyzer

This produces the same ResidualFrame struct, so the existing ResidualSynthesizer works unchanged.

**Algorithm**:
```
For each bin in STFT:
  For each partial in HarmonicFrame:
    if |bin_freq - partial_freq| < coring_bandwidth:
      mark bin as harmonic
  if bin is NOT harmonic:
    add bin energy to appropriate frequency band
```

Coring bandwidth: 1.5x the bin spacing (empirical, covers main lobe of Blackman-Harris window).

**Alternatives considered**:
- Full subtraction (like ResidualAnalyzer): Adds one frame of latency, higher CPU.
- Time-domain noise gating: Less accurate spectral envelope estimation.
- No residual at all in live mode: Loses perceptual quality (breath, texture).

## R-006: Latency Mode Configuration

**Decision**: Two modes reconfigure the STFT window strategy:

| Mode | Short Window | Long Window | YIN Window | Min F0 | Latency |
|------|-------------|-------------|------------|--------|---------|
| Low Latency | 1024 / hop 512 | Disabled | 1024 | ~80-100 Hz | 512/44100 = 11.6ms + processing |
| High Precision | 1024 / hop 512 | 4096 / hop 2048 | 2048 | ~40 Hz | 2048/44100 = 46.4ms + processing |

**Rationale**: Matches the existing dual-STFT architecture. Low-latency mode simply skips the long window, reducing both latency and CPU usage. The short window hop of 512 samples gives ~11.6ms effective latency at 44.1kHz, plus YIN detection time. Total should be well within the 15-25ms target.

High-precision mode uses the full dual-window setup identical to sample analysis (already proven in M1-M2). The long window's 2048-sample hop gives ~46ms latency, within the 50-100ms target.

**Switching behavior**: When switching from low-latency to high-precision while running, the long STFT begins accumulating samples and contributes once it has enough data. When switching from high-precision to low-latency, the long STFT output is simply ignored (no reset needed).

**Alternatives considered**:
- Three modes (ultra-low, balanced, precision): Over-engineering for initial release.
- Adjustable window size slider: Too complex for user-facing control; two modes cover the primary use cases.

## R-007: Input Source Crossfade

**Decision**: Use a 20ms linear crossfade between sample-mode frames and sidechain-mode frames when switching input source. Crossfade operates on the final mixed output (harmonic + residual), not individual partials.

**Rationale**: The two sources produce HarmonicFrame data with potentially different partial layouts, making partial-level blending complex and error-prone. Output-level crossfade is simple, reliable, and covers the perceptual requirement (no audible click).

**Implementation**:
- Store `crossfadeSamplesRemaining_` and `crossfadeLength_` (20ms in samples)
- When source switch detected: capture current output level, set crossfade counter
- Per-sample: blend old source output with new source output using linear ramp

This pattern already exists in the codebase (anti-click voice steal crossfade uses identical logic at lines 326-334 of processor.cpp).

**Alternatives considered**:
- Partial-level morphing: Too complex; partial indices may not correspond between sources.
- Exponential crossfade: Linear is sufficient for 20ms and simpler.
- No crossfade: Would produce audible clicks (violates SC-005).

## R-008: YIN Buffer Management for Live Mode

**Decision**: Maintain a circular buffer of recent sidechain samples for YIN detection. When enough samples accumulate (YIN window size), run detection.

**Rationale**: YIN needs a contiguous window of samples (1024 for low-latency, 2048 for high-precision). Host buffer sizes may be much smaller (32-512 samples). The existing `YinPitchDetector::detect()` takes a pointer + length. We accumulate samples in a ring buffer and pass the latest window-size chunk when ready.

**Implementation**: A simple pre-allocated `std::array` or `std::vector` (allocated in prepare()) used as a ring buffer. When `writeIndex >= yinWindowSize`, run YIN on the most recent `yinWindowSize` samples. This is the same pattern as STFT internal buffering but for YIN.

## R-009: State Versioning

**Decision**: Increment state version from 2 to 3 for M3 parameters. Add input source selector and latency mode to state persistence.

**Rationale**: The existing state uses version 2 (M2 residual parameters). Version 3 adds two new parameters. Backward compatibility is maintained: version 2 states load with default values for the new parameters (InputSource::Sample, LatencyMode::LowLatency).

## R-010: Pre-allocated Buffer Sizes

**Decision**: Maximum block size for sidechain downmix buffer is 8192 samples. This covers all common host buffer sizes with generous headroom.

**Rationale**: Common host buffer sizes range from 32 to 4096 samples. 8192 provides 2x headroom over the largest common size. Pre-allocated in `setActive(true)` to avoid audio-thread allocation (Constitution Principle II).

**Alternatives considered**:
- Dynamic allocation based on host buffer size: Violates real-time constraints if buffer size changes.
- Using `processSetup.maxSamplesPerBlock`: Could use this value from `setupProcessing()`, but a fixed maximum is simpler and avoids edge cases.
