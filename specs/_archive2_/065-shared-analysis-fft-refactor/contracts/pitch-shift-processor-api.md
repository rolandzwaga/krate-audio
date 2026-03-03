# API Contract: PitchShiftProcessor Shared-Analysis Extensions

**Layer**: 2 (Processors) -- pImpl Wrapper
**File**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
**Namespace**: `Krate::DSP`

## Existing API (Unchanged)

```cpp
class PitchShiftProcessor {
public:
    PitchShiftProcessor() noexcept;
    ~PitchShiftProcessor() noexcept;
    PitchShiftProcessor(PitchShiftProcessor&&) noexcept;
    PitchShiftProcessor& operator=(PitchShiftProcessor&&) noexcept;

    // Delete copy operations (pImpl with unique_ptr)
    PitchShiftProcessor(const PitchShiftProcessor&) = delete;
    PitchShiftProcessor& operator=(const PitchShiftProcessor&) = delete;

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Standard full-pipeline processing (unchanged)
    void process(const float* input, float* output, std::size_t numSamples) noexcept;

    // Mode, pitch, formant, phase reset (all unchanged)
    void setMode(PitchMode mode) noexcept;
    [[nodiscard]] PitchMode getMode() const noexcept;
    void setSemitones(float semitones) noexcept;
    [[nodiscard]] float getSemitones() const noexcept;
    void setCents(float cents) noexcept;
    [[nodiscard]] float getCents() const noexcept;
    [[nodiscard]] float getPitchRatio() const noexcept;
    void setFormantPreserve(bool enabled) noexcept;
    [[nodiscard]] bool getFormantPreserve() const noexcept;
    void setPhaseReset(bool enabled) noexcept;
    [[nodiscard]] bool getPhaseReset() const noexcept;
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;
};
```

## New API (Shared-Analysis Extensions)

### processWithSharedAnalysis

```cpp
/// @brief Process one analysis frame using shared analysis, bypassing internal STFT.
///
/// When mode is PhaseVocoder: delegates to internal PhaseVocoderPitchShifter's
/// processWithSharedAnalysis(). The pitch ratio is passed directly without
/// internal parameter smoothing (the caller is responsible for smoothing).
///
/// When mode is NOT PhaseVocoder (Simple, Granular, PitchSync): no-op.
/// No frame is pushed to the OLA buffer. pullSharedAnalysisOutput() will
/// return 0 for this frame. There is no output buffer parameter in this method.
///
/// @param analysis  Read-only reference to pre-computed analysis spectrum.
/// @param pitchRatio  Pitch ratio for this frame (direct, not smoothed).
///
/// @pre prepare() has been called.
/// @pre Mode is set via setMode() before calling.
void processWithSharedAnalysis(const SpectralBuffer& analysis,
                               float pitchRatio) noexcept;
```

### pullSharedAnalysisOutput

```cpp
/// @brief Pull output samples from the PhaseVocoder OLA buffer after
///        processWithSharedAnalysis() calls.
///
/// @param output      Destination buffer.
/// @param maxSamples  Maximum samples to pull.
/// @return            Samples actually written (may be less if OLA has fewer).
///
/// When mode is NOT PhaseVocoder: returns 0, output untouched.
std::size_t pullSharedAnalysisOutput(float* output,
                                     std::size_t maxSamples) noexcept;
```

### sharedAnalysisSamplesAvailable

```cpp
/// @brief Query available output samples from the PhaseVocoder OLA buffer.
///
/// @return Samples available, or 0 if mode is not PhaseVocoder.
[[nodiscard]] std::size_t sharedAnalysisSamplesAvailable() const noexcept;
```

### getPhaseVocoderFFTSize / getPhaseVocoderHopSize (FR-011)

```cpp
/// @brief Get the PhaseVocoder's FFT size for shared STFT configuration.
/// @return 4096 (compile-time constant).
[[nodiscard]] static constexpr std::size_t getPhaseVocoderFFTSize() noexcept {
    return PhaseVocoderPitchShifter::kFFTSize;
}

/// @brief Get the PhaseVocoder's hop size for shared STFT configuration.
/// @return 1024 (compile-time constant).
[[nodiscard]] static constexpr std::size_t getPhaseVocoderHopSize() noexcept {
    return PhaseVocoderPitchShifter::kHopSize;
}
```

## Impl Struct Additions

```cpp
struct PitchShiftProcessor::Impl {
    // ... existing members unchanged ...

    // New delegation methods for shared analysis
    void processWithSharedAnalysis(const SpectralBuffer& analysis,
                                   float pitchRatio) noexcept {
        if (!prepared) return;
        if (mode != PitchMode::PhaseVocoder) return;
        phaseVocoderShifter.processWithSharedAnalysis(analysis, pitchRatio);
    }

    std::size_t pullSharedAnalysisOutput(float* output,
                                         std::size_t maxSamples) noexcept {
        if (!prepared || mode != PitchMode::PhaseVocoder) return 0;
        return phaseVocoderShifter.pullOutputSamples(output, maxSamples);
    }

    std::size_t sharedAnalysisSamplesAvailable() const noexcept {
        if (!prepared || mode != PitchMode::PhaseVocoder) return 0;
        return phaseVocoderShifter.outputSamplesAvailable();
    }
};
```

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Not prepared | No-op, returns 0 for pull methods |
| Mode != PhaseVocoder | No-op for processWithSharedAnalysis, returns 0 for pull |
| FFT size mismatch | Handled by PhaseVocoderPitchShifter (assert debug, no-op release) |
| Null output pointer in pull | Returns 0 |
