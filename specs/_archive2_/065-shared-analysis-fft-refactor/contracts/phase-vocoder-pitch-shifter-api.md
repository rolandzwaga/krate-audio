# API Contract: PhaseVocoderPitchShifter Shared-Analysis Extensions

**Layer**: 2 (Processors)
**File**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
**Namespace**: `Krate::DSP`

## Existing API (Unchanged)

```cpp
class PhaseVocoderPitchShifter {
public:
    static constexpr std::size_t kFFTSize = 4096;
    static constexpr std::size_t kHopSize = 1024;
    static constexpr std::size_t kMaxBins = kFFTSize / 2 + 1;  // 2049
    static constexpr std::size_t kMaxPeaks = kMaxBins / 2;

    PhaseVocoderPitchShifter() = default;

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Standard full-pipeline processing (unchanged)
    void process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept;

    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    // Formant, phase locking, phase reset (all unchanged)
    void setFormantPreserve(bool enabled) noexcept;
    [[nodiscard]] bool getFormantPreserve() const noexcept;
    void setPhaseLocking(bool enabled) noexcept;
    [[nodiscard]] bool getPhaseLocking() const noexcept;
    void setPhaseReset(bool enabled) noexcept;
    [[nodiscard]] bool getPhaseReset() const noexcept;

    // ... other existing accessors unchanged ...
};
```

## New API (Shared-Analysis Extensions)

### processWithSharedAnalysis

```cpp
/// @brief Process one analysis frame using an externally provided spectrum.
///
/// Performs synthesis-only processing: phase rotation, optional phase locking,
/// optional transient detection, optional formant preservation, synthesis iFFT,
/// and overlap-add. Bypasses internal STFT analysis entirely.
///
/// @param analysis  Read-only reference to the pre-computed analysis spectrum.
///                  Must have numBins() == kFFTSize / 2 + 1 (2049).
///                  The caller MUST NOT modify this spectrum during or after
///                  the call. The reference is only valid for the duration
///                  of this call (FR-024).
/// @param pitchRatio  Pitch ratio for this frame (e.g., 1.0594 for +1 semitone).
///                    Clamped to [0.25, 4.0].
///
/// @pre  prepare() has been called.
/// @pre  analysis.numBins() == kFFTSize / 2 + 1 (= 2049 for kFFTSize = 4096).
/// @post One synthesis frame has been added to the internal OLA buffer.
///       Use outputSamplesAvailable() and pullOutputSamples() to retrieve output.
///
/// In degenerate conditions (unprepared, FFT size mismatch), the method is a
/// no-op: no frame is pushed to the OLA buffer. pullOutputSamples() will
/// return 0 for this frame. The caller is responsible for zero-filling any
/// output samples not covered by pullOutputSamples() (see FR-013a).
/// There is no output buffer parameter in this method.
///
/// @note This method MUST NOT apply unity-pitch bypass internally. The caller
///       (HarmonizerEngine) is responsible for detecting unity pitch and
///       routing accordingly (FR-025).
void processWithSharedAnalysis(const SpectralBuffer& analysis,
                               float pitchRatio) noexcept;
```

### pullOutputSamples

```cpp
/// @brief Pull processed samples from the internal OLA buffer.
///
/// @param output      Destination buffer. Must have room for at least
///                    maxSamples floats.
/// @param maxSamples  Maximum number of samples to pull.
/// @return            Number of samples actually written to output.
///                    May be less than maxSamples if fewer are available.
///
/// @pre  prepare() has been called.
/// @post Up to maxSamples are copied from OLA buffer to output.
///       OLA buffer advances accordingly.
std::size_t pullOutputSamples(float* output, std::size_t maxSamples) noexcept;
```

### outputSamplesAvailable

```cpp
/// @brief Query how many samples are available in the OLA buffer.
///
/// @return Number of samples that can be pulled via pullOutputSamples().
[[nodiscard]] std::size_t outputSamplesAvailable() const noexcept;
```

## Modified Internal Method

### processFrame (private)

```cpp
// BEFORE (current signature):
void processFrame(float pitchRatio) noexcept;

// AFTER (refactored signature):
void processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis,
                  float pitchRatio) noexcept;
```

**Change**: Reads from `analysis` parameter instead of `analysisSpectrum_` member. Writes to `synthesis` parameter instead of `synthesisSpectrum_` member. All per-voice state (`prevPhase_`, `synthPhase_`, `magnitude_`, `frequency_`, phase locking arrays, transient detector, formant preserver) continues to use member variables.

**Backward compatibility**: The existing `process()` method calls `processFrame(analysisSpectrum_, synthesisSpectrum_, pitchRatio)` using its own internal members.

## Error Handling

| Condition | Debug | Release |
|-----------|-------|---------|
| `analysis.numBins() != kFFTSize / 2 + 1` (2049) | Assert fires | No-op return (no OLA write) |
| `!prepared (prepare() not called)` | No-op return | No-op return (no OLA write) |
| `pitchRatio outside [0.25, 4.0]` | Clamped | Clamped |
| `output == nullptr in pullOutputSamples` | No-op, returns 0 | No-op, returns 0 |

Note: `kMaxBins` is sized for the maximum supported FFT (8192), giving `kMaxBins = 4097`. The validation check uses the runtime-configured value `kFFTSize / 2 + 1` (= 2049), NOT `kMaxBins`. Using `kMaxBins` in the check would incorrectly accept spectra from a larger FFT.

## Usage Example

```cpp
// Engine-level shared analysis flow (per block)
STFT sharedStft;
SpectralBuffer sharedSpectrum;
PhaseVocoderPitchShifter voices[4];

// In process():
sharedStft.pushSamples(input, numSamples);
while (sharedStft.canAnalyze()) {
    sharedStft.analyze(sharedSpectrum);
    for (auto& voice : voices) {
        voice.processWithSharedAnalysis(sharedSpectrum, voicePitchRatio);
    }
}

// Pull output from each voice
for (auto& voice : voices) {
    std::size_t pulled = voice.pullOutputSamples(output, numSamples);
    // Zero-fill remaining: already handled by caller
}
```
