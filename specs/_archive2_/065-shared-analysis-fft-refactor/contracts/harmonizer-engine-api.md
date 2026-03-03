# API Contract: HarmonizerEngine Shared-Analysis Integration

**Layer**: 3 (Systems)
**File**: `dsp/include/krate/dsp/systems/harmonizer_engine.h`
**Namespace**: `Krate::DSP`

## Public API (Unchanged -- FR-016)

The HarmonizerEngine public API remains entirely unchanged. No new public methods are introduced. The shared-analysis optimization is an internal implementation detail.

```cpp
class HarmonizerEngine {
public:
    // All existing methods unchanged:
    HarmonizerEngine() = default;
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    void process(const float* input, float* outputL, float* outputR,
                 std::size_t numSamples) noexcept;

    void setHarmonyMode(HarmonyMode mode) noexcept;
    void setNumVoices(int numVoices) noexcept;
    [[nodiscard]] int getNumVoices() const noexcept;
    void setKey(int rootNote) noexcept;
    void setScale(ScaleType scale) noexcept;
    void setPitchShiftMode(PitchMode mode) noexcept;
    void setFormantPreserve(bool enabled) noexcept;
    void setDryLevel(float linearGain) noexcept;
    void setWetLevel(float linearGain) noexcept;
    void setVoiceInterval(int voiceIndex, int interval) noexcept;
    void setVoiceLevel(int voiceIndex, float levelDb) noexcept;
    void setVoicePan(int voiceIndex, float pan) noexcept;
    void setVoiceDelay(int voiceIndex, float delayMs) noexcept;
    void setVoiceDetune(int voiceIndex, float cents) noexcept;
    [[nodiscard]] float getDetectedPitch() const noexcept;
    [[nodiscard]] int getDetectedNote() const noexcept;
    [[nodiscard]] float getPitchConfidence() const noexcept;
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;
};
```

## Internal Changes

### New Private Members

```cpp
private:
    // Shared-analysis resources (PhaseVocoder mode only)
    STFT sharedStft_;                          // Shared forward FFT
    SpectralBuffer sharedAnalysisSpectrum_;     // Shared analysis result
    std::vector<float> pvVoiceScratch_;         // Scratch buffer for PV voice output
```

### Modified prepare()

```cpp
void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
    // ... existing preparation unchanged ...

    // NEW: Prepare shared-analysis resources for PhaseVocoder mode
    constexpr auto fftSize = PitchShiftProcessor::getPhaseVocoderFFTSize();
    constexpr auto hopSize = PitchShiftProcessor::getPhaseVocoderHopSize();
    sharedStft_.prepare(fftSize, hopSize, WindowType::Hann);
    sharedAnalysisSpectrum_.prepare(fftSize);
    pvVoiceScratch_.resize(maxBlockSize, 0.0f);

    prepared_ = true;
}
```

### Modified reset()

```cpp
void reset() noexcept {
    // ... existing resets unchanged ...

    // NEW: Reset shared-analysis resources
    sharedStft_.reset();
    sharedAnalysisSpectrum_.reset();
}
```

### Modified process() -- PhaseVocoder Path

```cpp
void process(const float* input, float* outputL, float* outputR,
             std::size_t numSamples) noexcept {
    // ... existing pre-condition guard and output zeroing unchanged ...

    if (numActiveVoices_ > 0) {
        // ... existing pitch tracking for Scalic mode unchanged ...

        if (pitchShiftMode_ == PitchMode::PhaseVocoder) {
            // ====== SHARED-ANALYSIS PATH ======

            // Step 1: Push input to shared STFT (once for all voices)
            sharedStft_.pushSamples(input, numSamples);

            // Step 2: For each voice, compute pitch parameters
            //         (same as before: pitch smoother, etc.)
            for (int v = 0; v < numActiveVoices_; ++v) {
                auto& voice = voices_[static_cast<std::size_t>(v)];
                if (voice.linearGain == 0.0f) continue;

                // Compute smoothed pitch ratio (same as existing code)
                // ... pitch computation, smoother advancement ...
            }

            // Step 3: Process all ready analysis frames
            while (sharedStft_.canAnalyze()) {
                sharedStft_.analyze(sharedAnalysisSpectrum_);

                // Pass shared spectrum to each active voice
                for (int v = 0; v < numActiveVoices_; ++v) {
                    auto& voice = voices_[static_cast<std::size_t>(v)];
                    if (voice.linearGain == 0.0f) continue;

                    float pitchRatio = semitonesToRatio(
                        voice.pitchSmoother.getCurrentValue());
                    voice.pitchShifter.processWithSharedAnalysis(
                        sharedAnalysisSpectrum_, pitchRatio);
                }
            }

            // Step 4: Pull output from each voice and apply level/pan/delay
            for (int v = 0; v < numActiveVoices_; ++v) {
                auto& voice = voices_[static_cast<std::size_t>(v)];
                if (voice.linearGain == 0.0f) continue;

                // Pull OLA output
                std::size_t available =
                    voice.pitchShifter.sharedAnalysisSamplesAvailable();
                std::size_t toPull = std::min(numSamples, available);

                std::fill(pvVoiceScratch_.begin(),
                          pvVoiceScratch_.begin() + numSamples, 0.0f);
                if (toPull > 0) {
                    voice.pitchShifter.pullSharedAnalysisOutput(
                        pvVoiceScratch_.data(), toPull);
                }

                // Apply per-voice delay (post-pitch, see R-004)
                if (voice.delayMs > 0.0f) {
                    for (std::size_t s = 0; s < numSamples; ++s) {
                        voice.delayLine.write(pvVoiceScratch_[s]);
                        voiceScratch_[s] = voice.delayLine.readLinear(
                            voice.delaySamples);
                    }
                } else {
                    std::copy(pvVoiceScratch_.data(),
                              pvVoiceScratch_.data() + numSamples,
                              voiceScratch_.data());
                }

                // Per-sample accumulation with level and pan smoothing
                for (std::size_t s = 0; s < numSamples; ++s) {
                    float levelGain = voice.levelSmoother.process();
                    float panVal = voice.panSmoother.process();
                    float angle = (panVal + 1.0f) * kPi * 0.25f;
                    float leftGain = std::cos(angle);
                    float rightGain = std::sin(angle);
                    float sample = voiceScratch_[s] * levelGain;
                    outputL[s] += sample * leftGain;
                    outputR[s] += sample * rightGain;
                }
            }
        } else {
            // ====== STANDARD PER-VOICE PATH (unchanged) ======
            for (int v = 0; v < numActiveVoices_; ++v) {
                // ... existing per-voice processing unchanged ...
            }
        }
    }

    // ... existing dry/wet blend unchanged ...
}
```

## Data Flow Invariants

1. `sharedStft_` receives input EXACTLY ONCE per `process()` call (not per voice).
2. `sharedAnalysisSpectrum_` is written by `sharedStft_.analyze()` and then read by all voices. No voice writes to it.
3. Per-voice OLA buffers are independent. Voice N's `processWithSharedAnalysis()` never accesses Voice M's state.
4. Per-voice delays are applied AFTER OLA output in PhaseVocoder mode (delay post-pitch), and BEFORE pitch shifting in all other modes (delay pre-pitch, unchanged behavior).
5. The shared STFT is always prepared regardless of mode, but only driven (pushSamples/analyze) when mode is PhaseVocoder.

## Performance Expectations

| Metric | Pre-Refactor | Post-Refactor | Savings |
|--------|-------------|---------------|---------|
| Forward FFTs per block (4 voices) | 4 | 1 | 75% of forward FFT cost |
| Synthesis iFFTs per block (4 voices) | 4 | 4 | 0% (unchanged) |
| Total PhaseVocoder CPU (SC-001 target) | ~24% | <18% | >25% reduction |
