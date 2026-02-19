// ==============================================================================
// Layer 3: System Component - Multi-Voice Harmonizer Engine
// ==============================================================================
// Orchestrates shared pitch analysis, per-voice pitch shifting, level/pan
// mixing, and mono-to-stereo constant-power panning. Composes existing
// Layer 0-2 components without introducing new DSP algorithms.
//
// Signal flow: mono input -> [PitchTracker] -> per-voice [DelayLine ->
// PitchShiftProcessor -> Level/Pan] -> stereo sum -> dry/wet mix -> stereo output.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, std::array, C++20)
// - Principle IX: Layer 3 (depends on Layer 0, 1, 2 only)
// - Principle XII: Test-First Development
//
// Reference: specs/064-harmonizer-engine/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/scale_harmonizer.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/pitch_tracker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/processors/pitch_shift_processor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

// =============================================================================
// HarmonyMode Enum (T009)
// =============================================================================

/// @brief Harmony intelligence mode selector.
enum class HarmonyMode : uint8_t {
    Chromatic = 0,  ///< Fixed semitone shift, no pitch tracking or scale awareness
    Scalic = 1,     ///< Diatonic interval in a configured key/scale, with pitch tracking
};

// =============================================================================
// HarmonizerEngine Class (T010-T014)
// =============================================================================

/// @brief Multi-voice harmonizer engine (Layer 3 - Systems).
///
/// Orchestrates shared pitch analysis, per-voice pitch shifting, level/pan
/// mixing, and mono-to-stereo constant-power panning. Composes existing
/// Layer 0-2 components without introducing new DSP algorithms.
///
/// @par Shared-Analysis Architecture (spec 065)
/// In PhaseVocoder mode, the engine runs a single forward FFT analysis per
/// block and shares the resulting spectrum across all active voices via
/// const reference (FR-017, FR-019). Each voice performs only its own
/// phase rotation, synthesis iFFT, and OLA reconstruction. This eliminates
/// 75% of forward FFT computation for 4 voices. Per-voice onset delays
/// are applied post-pitch in PhaseVocoder mode (FR-025). In all other modes
/// (Simple, Granular, PitchSync), the standard per-voice process() path
/// is used unchanged (FR-014).
///
/// @par Real-Time Safety
/// All processing methods are noexcept. Zero heap allocations after prepare().
/// No locks, no I/O, no exceptions in the process path.
///
/// @par Thread Safety
/// Parameter setters are safe to call between process() calls from the same thread.
/// No cross-thread safety is provided -- the host must serialize parameter changes
/// with processing.
class HarmonizerEngine {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr int kMaxVoices = 4;
    static constexpr float kMinLevelDb = -60.0f;       ///< At or below = mute
    static constexpr float kMaxLevelDb = 6.0f;
    static constexpr int kMinInterval = -24;
    static constexpr int kMaxInterval = 24;
    static constexpr float kMinPan = -1.0f;
    static constexpr float kMaxPan = 1.0f;
    static constexpr float kMaxDelayMs = 50.0f;
    static constexpr float kMinDetuneCents = -50.0f;
    static constexpr float kMaxDetuneCents = 50.0f;

    // Smoothing time constants (milliseconds)
    static constexpr float kPitchSmoothTimeMs = 10.0f;
    static constexpr float kLevelSmoothTimeMs = 5.0f;
    static constexpr float kPanSmoothTimeMs = 5.0f;
    static constexpr float kDryWetSmoothTimeMs = 10.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================
    HarmonizerEngine() noexcept = default;

    /// @brief Initialize all internal components and pre-allocate buffers.
    /// @param sampleRate Audio sample rate in Hz (e.g. 44100.0)
    /// @param maxBlockSize Maximum number of samples per process() call
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        const auto sampleRateF = static_cast<float>(sampleRate);

        // Prepare all 4 PitchShiftProcessors
        for (auto& voice : voices_) {
            voice.pitchShifter.prepare(sampleRate, maxBlockSize);
        }

        // Prepare all 4 DelayLines (50ms max)
        for (auto& voice : voices_) {
            voice.delayLine.prepare(sampleRate, kMaxDelayMs / 1000.0f);
        }

        // Prepare PitchTracker
        pitchTracker_.prepare(sampleRate);

        // Configure per-voice smoothers
        for (auto& voice : voices_) {
            voice.levelSmoother.configure(kLevelSmoothTimeMs, sampleRateF);
            voice.panSmoother.configure(kPanSmoothTimeMs, sampleRateF);
            voice.pitchSmoother.configure(kPitchSmoothTimeMs, sampleRateF);
        }

        // Configure global dry/wet smoothers (10ms)
        dryLevelSmoother_.configure(kDryWetSmoothTimeMs, sampleRateF);
        wetLevelSmoother_.configure(kDryWetSmoothTimeMs, sampleRateF);

        // Allocate scratch buffers
        delayScratch_.resize(maxBlockSize, 0.0f);
        voiceScratch_.resize(maxBlockSize, 0.0f);

        // Prepare shared pitch detector (PitchSync mode optimization)
        // Same window size (256) as the per-voice detectors in PitchSyncGranularShifter
        sharedPitchDetector_.prepare(sampleRate, 256);

        // Prepare shared-analysis resources for PhaseVocoder mode (spec 065)
        constexpr auto fftSize = PitchShiftProcessor::getPhaseVocoderFFTSize();
        constexpr auto hopSize = PitchShiftProcessor::getPhaseVocoderHopSize();
        sharedStft_.prepare(fftSize, hopSize, WindowType::Hann);
        sharedAnalysisSpectrum_.prepare(fftSize);
        pvVoiceScratch_.resize(maxBlockSize, 0.0f);

        prepared_ = true;
    }

    /// @brief Reset all processing state without changing configuration.
    void reset() noexcept {
        // Reset all 4 PitchShiftProcessors
        for (auto& voice : voices_) {
            voice.pitchShifter.reset();
        }

        // Reset all 4 DelayLines
        for (auto& voice : voices_) {
            voice.delayLine.reset();
        }

        // Reset PitchTracker
        pitchTracker_.reset();

        // Reset all smoothers
        for (auto& voice : voices_) {
            voice.levelSmoother.reset();
            voice.panSmoother.reset();
            voice.pitchSmoother.reset();
        }
        dryLevelSmoother_.reset();
        wetLevelSmoother_.reset();

        // Zero scratch buffers
        std::fill(delayScratch_.begin(), delayScratch_.end(), 0.0f);
        std::fill(voiceScratch_.begin(), voiceScratch_.end(), 0.0f);

        // Reset shared pitch detector
        sharedPitchDetector_.reset();

        // Reset shared-analysis resources (spec 065)
        sharedStft_.reset();
        sharedAnalysisSpectrum_.reset();
        std::fill(pvVoiceScratch_.begin(), pvVoiceScratch_.end(), 0.0f);

        // Reset pitch tracking state
        lastDetectedNote_ = -1;
    }

    /// @brief Check whether prepare() has been called successfully.
    /// @return true if the engine is ready to process audio.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Audio Processing
    // =========================================================================

    /// @brief Process one block of audio: mono input to stereo output.
    /// @param input Pointer to mono input samples (numSamples)
    /// @param outputL Pointer to left output channel (numSamples, zeroed + written)
    /// @param outputR Pointer to right output channel (numSamples, zeroed + written)
    /// @param numSamples Number of samples to process (must be <= maxBlockSize)
    void process(const float* input, float* outputL, float* outputR,
                 std::size_t numSamples) noexcept {
        // FR-015: Pre-condition guard -- if not prepared, zero-fill and return
        if (!prepared_) {
            std::fill(outputL, outputL + numSamples, 0.0f);
            std::fill(outputR, outputR + numSamples, 0.0f);
            return;
        }

        // Step 0: Zero output buffers (harmony bus accumulation target)
        std::fill(outputL, outputL + numSamples, 0.0f);
        std::fill(outputR, outputR + numSamples, 0.0f);

        // FR-018: If numVoices==0, skip all voice processing and pitch tracking
        if (numActiveVoices_ > 0) {
            // Step 1: Push input to PitchTracker (Scalic mode only, FR-008/FR-009)
            if (harmonyMode_ == HarmonyMode::Scalic) {
                pitchTracker_.pushBlock(input, numSamples);
                if (pitchTracker_.isPitchValid()) {
                    lastDetectedNote_ = pitchTracker_.getMidiNote();
                }
            }

            if (pitchShiftMode_ == PitchMode::PhaseVocoder) {
                // ====== SHARED-ANALYSIS PATH (spec 065) ======

                // Step 2: Compute pitch parameters for all active voices
                // (same as standard path: pitch smoother target + advancement)
                for (int v = 0; v < numActiveVoices_; ++v) {
                    auto& voice = voices_[static_cast<std::size_t>(v)];
                    if (voice.linearGain == 0.0f) {
                        // Still advance smoothers for muted voices
                        voice.pitchSmoother.setTarget(0.0f);
                        (void)voice.pitchSmoother.process();
                        if (numSamples > 1) {
                            voice.pitchSmoother.advanceSamples(numSamples - 1);
                        }
                        voice.levelSmoother.advanceSamples(numSamples);
                        voice.panSmoother.advanceSamples(numSamples);
                        continue;
                    }

                    float targetSemitones = 0.0f;
                    if (harmonyMode_ == HarmonyMode::Chromatic) {
                        targetSemitones = static_cast<float>(voice.interval) +
                                         voice.detuneCents / 100.0f;
                    } else {
                        if (lastDetectedNote_ >= 0) {
                            auto result = scaleHarmonizer_.calculate(
                                lastDetectedNote_, voice.interval);
                            targetSemitones = static_cast<float>(result.semitones) +
                                              voice.detuneCents / 100.0f;
                        } else {
                            targetSemitones = voice.detuneCents / 100.0f;
                        }
                    }

                    voice.pitchSmoother.setTarget(targetSemitones);
                    float smoothedPitch = voice.pitchSmoother.process();
                    if (numSamples > 1) {
                        voice.pitchSmoother.advanceSamples(numSamples - 1);
                    }

                    // Store the smoothed pitch for use during frame processing.
                    // We set it on the PitchShiftProcessor so the formant
                    // preserve state is consistent.
                    voice.pitchShifter.setSemitones(smoothedPitch);
                }

                // Step 3: Push input to shared STFT (once for all voices)
                sharedStft_.pushSamples(input, numSamples);

                // Step 4: Process all ready analysis frames
                while (sharedStft_.canAnalyze()) {
                    sharedStft_.analyze(sharedAnalysisSpectrum_);

                    // Pass shared spectrum to each active voice
                    for (int v = 0; v < numActiveVoices_; ++v) {
                        auto& voice = voices_[static_cast<std::size_t>(v)];
                        if (voice.linearGain == 0.0f) continue;

                        float pitchRatio = semitonesToRatio(
                            voice.pitchShifter.getSemitones());

                        // FR-025: Unity-pitch bypass at engine level
                        // Matches PhaseVocoderPitchShifter::processUnityPitch()
                        // behavior for SC-002 output equivalence
                        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
                            voice.pitchShifter.synthesizePassthrough(
                                sharedAnalysisSpectrum_);
                        } else {
                            voice.pitchShifter.processWithSharedAnalysis(
                                sharedAnalysisSpectrum_, pitchRatio);
                        }
                    }
                }

                // Step 5: Pull output from each voice and apply level/pan/delay
                for (int v = 0; v < numActiveVoices_; ++v) {
                    auto& voice = voices_[static_cast<std::size_t>(v)];
                    if (voice.linearGain == 0.0f) continue;

                    // Pull OLA output into pvVoiceScratch_
                    std::size_t available =
                        voice.pitchShifter.sharedAnalysisSamplesAvailable();
                    std::size_t toPull = std::min(numSamples, available);

                    // FR-013a/T037: Zero-fill pvVoiceScratch_ first, then
                    // overwrite with pulled samples. Samples not covered by
                    // pullSharedAnalysisOutput remain zero.
                    std::fill(pvVoiceScratch_.begin(),
                              pvVoiceScratch_.begin() +
                                  static_cast<std::ptrdiff_t>(numSamples),
                              0.0f);
                    if (toPull > 0) {
                        voice.pitchShifter.pullSharedAnalysisOutput(
                            pvVoiceScratch_.data(), toPull);
                    }

                    // FR-025: Apply per-voice delay POST-pitch in PV mode
                    if (voice.delayMs > 0.0f) {
                        for (std::size_t s = 0; s < numSamples; ++s) {
                            voice.delayLine.write(pvVoiceScratch_[s]);
                            voiceScratch_[s] = voice.delayLine.readLinear(
                                voice.delaySamples);
                        }
                    } else {
                        std::copy(pvVoiceScratch_.data(),
                                  pvVoiceScratch_.data() +
                                      static_cast<std::ptrdiff_t>(numSamples),
                                  voiceScratch_.data());
                    }

                    // Per-sample accumulation with level and pan smoothing
                    for (std::size_t s = 0; s < numSamples; ++s) {
                        float levelGain = voice.levelSmoother.process();
                        float panVal = voice.panSmoother.process();

                        // Constant-power pan (FR-005)
                        float angle = (panVal + 1.0f) * kPi * 0.25f;
                        float leftGain = std::cos(angle);
                        float rightGain = std::sin(angle);

                        // Quadratic fade-in curve: gentle start, avoids click
                        // when pitch shifter delay line begins producing output
                        float fadeGain = voice.fadeInGain * voice.fadeInGain;
                        float sample = voiceScratch_[s] * levelGain * fadeGain;
                        outputL[s] += sample * leftGain;
                        outputR[s] += sample * rightGain;

                        // Advance voice fade-in (linear ramp, applied quadratically)
                        if (voice.fadeInGain < 1.0f) {
                            voice.fadeInGain += voice.fadeInIncrement;
                            if (voice.fadeInGain >= 1.0f) {
                                voice.fadeInGain = 1.0f;
                                voice.fadeInIncrement = 0.0f;
                            }
                        }
                    }
                }
            } else if (pitchShiftMode_ == PitchMode::PitchSync) {
                // ====== SHARED PITCH DETECTION PATH (PitchSync optimization) ======
                // Run a single PitchDetector on the input and pass results to all voices.
                // Eliminates 75% redundant autocorrelation for 4-voice configurations.

                // Step 2a: Run shared pitch detection ONCE for all voices
                sharedPitchDetector_.pushBlock(input, numSamples);
                const float sharedPeriod = sharedPitchDetector_.getDetectedPeriod();
                const float sharedConfidence = sharedPitchDetector_.getConfidence();

                for (int v = 0; v < numActiveVoices_; ++v) {
                    auto& voice = voices_[static_cast<std::size_t>(v)];

                    // Skip muted voices (optimization)
                    if (voice.linearGain == 0.0f) {
                        continue;
                    }

                    // Step 2b: Compute target semitones (same as standard path)
                    float targetSemitones = 0.0f;
                    if (harmonyMode_ == HarmonyMode::Chromatic) {
                        targetSemitones = static_cast<float>(voice.interval) +
                                         voice.detuneCents / 100.0f;
                    } else {
                        if (lastDetectedNote_ >= 0) {
                            auto result = scaleHarmonizer_.calculate(
                                lastDetectedNote_, voice.interval);
                            targetSemitones = static_cast<float>(result.semitones) +
                                              voice.detuneCents / 100.0f;
                        } else {
                            targetSemitones = voice.detuneCents / 100.0f;
                        }
                    }

                    voice.pitchSmoother.setTarget(targetSemitones);

                    float smoothedPitch = voice.pitchSmoother.process();
                    voice.pitchShifter.setSemitones(smoothedPitch);

                    if (numSamples > 1) {
                        voice.pitchSmoother.advanceSamples(numSamples - 1);
                    }

                    // Step 3: Process delay line (pre-pitch in non-PV modes)
                    if (voice.delayMs > 0.0f) {
                        for (std::size_t s = 0; s < numSamples; ++s) {
                            voice.delayLine.write(input[s]);
                            delayScratch_[s] = voice.delayLine.readLinear(
                                voice.delaySamples);
                        }
                    } else {
                        std::copy(input, input + numSamples, delayScratch_.data());
                    }

                    // Step 4: Process pitch shift with shared pitch detection
                    voice.pitchShifter.processWithSharedPitch(
                        delayScratch_.data(), voiceScratch_.data(), numSamples,
                        sharedPeriod, sharedConfidence);

                    // Step 5: Per-sample accumulation with level and pan smoothing
                    for (std::size_t s = 0; s < numSamples; ++s) {
                        float levelGain = voice.levelSmoother.process();
                        float panVal = voice.panSmoother.process();

                        // Constant-power pan (FR-005)
                        float angle = (panVal + 1.0f) * kPi * 0.25f;
                        float leftGain = std::cos(angle);
                        float rightGain = std::sin(angle);

                        // Quadratic fade-in curve: gentle start, avoids click
                        // when pitch shifter delay line begins producing output
                        float fadeGain = voice.fadeInGain * voice.fadeInGain;
                        float sample = voiceScratch_[s] * levelGain * fadeGain;
                        outputL[s] += sample * leftGain;
                        outputR[s] += sample * rightGain;

                        // Advance voice fade-in (linear ramp, applied quadratically)
                        if (voice.fadeInGain < 1.0f) {
                            voice.fadeInGain += voice.fadeInIncrement;
                            if (voice.fadeInGain >= 1.0f) {
                                voice.fadeInGain = 1.0f;
                                voice.fadeInIncrement = 0.0f;
                            }
                        }
                    }
                }
            } else {
                // ====== STANDARD PER-VOICE PATH (Simple, Granular) ======

                for (int v = 0; v < numActiveVoices_; ++v) {
                    auto& voice = voices_[static_cast<std::size_t>(v)];

                    // Skip muted voices (optimization)
                    if (voice.linearGain == 0.0f) {
                        continue;
                    }

                    // Step 2: Compute target semitones
                    float targetSemitones = 0.0f;
                    if (harmonyMode_ == HarmonyMode::Chromatic) {
                        targetSemitones = static_cast<float>(voice.interval) +
                                         voice.detuneCents / 100.0f;
                    } else {
                        if (lastDetectedNote_ >= 0) {
                            auto result = scaleHarmonizer_.calculate(
                                lastDetectedNote_, voice.interval);
                            targetSemitones = static_cast<float>(result.semitones) +
                                              voice.detuneCents / 100.0f;
                        } else {
                            targetSemitones = voice.detuneCents / 100.0f;
                        }
                    }

                    voice.pitchSmoother.setTarget(targetSemitones);

                    float smoothedPitch = voice.pitchSmoother.process();
                    voice.pitchShifter.setSemitones(smoothedPitch);

                    if (numSamples > 1) {
                        voice.pitchSmoother.advanceSamples(numSamples - 1);
                    }

                    // Step 3: Process delay line (pre-pitch in non-PV modes)
                    if (voice.delayMs > 0.0f) {
                        for (std::size_t s = 0; s < numSamples; ++s) {
                            voice.delayLine.write(input[s]);
                            delayScratch_[s] = voice.delayLine.readLinear(
                                voice.delaySamples);
                        }
                    } else {
                        std::copy(input, input + numSamples, delayScratch_.data());
                    }

                    // Step 4: Process pitch shift
                    voice.pitchShifter.process(delayScratch_.data(),
                                               voiceScratch_.data(), numSamples);

                    // Step 5: Per-sample accumulation with level and pan smoothing
                    for (std::size_t s = 0; s < numSamples; ++s) {
                        float levelGain = voice.levelSmoother.process();
                        float panVal = voice.panSmoother.process();

                        // Constant-power pan (FR-005)
                        float angle = (panVal + 1.0f) * kPi * 0.25f;
                        float leftGain = std::cos(angle);
                        float rightGain = std::sin(angle);

                        // Quadratic fade-in curve: gentle start, avoids click
                        // when pitch shifter delay line begins producing output
                        float fadeGain = voice.fadeInGain * voice.fadeInGain;
                        float sample = voiceScratch_[s] * levelGain * fadeGain;
                        outputL[s] += sample * leftGain;
                        outputR[s] += sample * rightGain;

                        // Advance voice fade-in (linear ramp, applied quadratically)
                        if (voice.fadeInGain < 1.0f) {
                            voice.fadeInGain += voice.fadeInIncrement;
                            if (voice.fadeInGain >= 1.0f) {
                                voice.fadeInGain = 1.0f;
                                voice.fadeInIncrement = 0.0f;
                            }
                        }
                    }
                }
            }
        }

        // Step 6-7: Per-sample dry/wet blend (FR-017 steps 6-7)
        for (std::size_t s = 0; s < numSamples; ++s) {
            float dryGain = dryLevelSmoother_.process();
            float wetGain = wetLevelSmoother_.process();

            outputL[s] = wetGain * outputL[s] + dryGain * input[s];
            outputR[s] = wetGain * outputR[s] + dryGain * input[s];
        }
    }

    // =========================================================================
    // Global Configuration (T027)
    // =========================================================================

    /// @brief Set the harmony mode (Chromatic or Scalic).
    void setHarmonyMode(HarmonyMode mode) noexcept {
        harmonyMode_ = mode;
    }

    /// @brief Set the number of active harmony voices. Clamped to [0, kMaxVoices].
    void setNumVoices(int count) noexcept {
        const int newCount = std::clamp(count, 0, kMaxVoices);
        // Fade in newly activated voices to prevent click.
        // Only when voices are already active (mid-stream addition).
        // Initial enable (0→N) is handled by the effects chain crossfade +
        // applyVoiceFadeIn() called from the effects chain.
        if (newCount > numActiveVoices_ && numActiveVoices_ > 0) {
            static constexpr float kVoiceFadeInMs = 100.0f;
            const float fadeInSamples = kVoiceFadeInMs * static_cast<float>(sampleRate_) / 1000.0f;
            const float increment = (fadeInSamples > 0.0f) ? (1.0f / fadeInSamples) : 1.0f;
            for (int v = numActiveVoices_; v < newCount; ++v) {
                auto& voice = voices_[static_cast<std::size_t>(v)];
                voice.fadeInGain = 0.0f;
                voice.fadeInIncrement = increment;
                // Don't reset pitch shifter — clearing the delay line creates
                // a hard edge when it refills. The fade-in handles the transition.
                voice.levelSmoother.snapToTarget();
                voice.panSmoother.snapToTarget();
                voice.pitchSmoother.snapToTarget();
            }
        }
        numActiveVoices_ = newCount;
    }

    /// @brief Get the current number of active harmony voices.
    [[nodiscard]] int getNumVoices() const noexcept {
        return numActiveVoices_;
    }

    /// @brief Set the root note for Scalic mode.
    void setKey(int rootNote) noexcept {
        scaleHarmonizer_.setKey(rootNote);
    }

    /// @brief Set the scale type for Scalic mode.
    void setScale(ScaleType type) noexcept {
        scaleHarmonizer_.setScale(type);
    }

    /// @brief Set the pitch shifting algorithm for all voices.
    void setPitchShiftMode(PitchMode mode) noexcept {
        if (mode == pitchShiftMode_) return;
        pitchShiftMode_ = mode;
        for (auto& voice : voices_) {
            voice.pitchShifter.setMode(mode);
            voice.pitchShifter.reset();
        }
    }

    /// @brief Enable or disable formant preservation for all voices.
    void setFormantPreserve(bool enable) noexcept {
        formantPreserve_ = enable;
        for (auto& voice : voices_) {
            voice.pitchShifter.setFormantPreserve(enable);
        }
    }

    /// @brief Set the dry signal level in decibels.
    void setDryLevel(float dB) noexcept {
        float gain = dbToGain(dB);
        dryLevelSmoother_.setTarget(gain);
    }

    /// @brief Set the wet (harmony) signal level in decibels.
    void setWetLevel(float dB) noexcept {
        float gain = dbToGain(dB);
        wetLevelSmoother_.setTarget(gain);
    }

    // =========================================================================
    // Per-Voice Configuration (T028)
    // =========================================================================

    /// @brief Set the interval for a specific voice.
    void setVoiceInterval(int voiceIndex, int diatonicSteps) noexcept {
        if (voiceIndex < 0 || voiceIndex >= kMaxVoices) return;
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        voice.interval = std::clamp(diatonicSteps, kMinInterval, kMaxInterval);
    }

    /// @brief Set the output level for a specific voice.
    void setVoiceLevel(int voiceIndex, float dB) noexcept {
        if (voiceIndex < 0 || voiceIndex >= kMaxVoices) return;
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        float clampedDb = std::clamp(dB, kMinLevelDb, kMaxLevelDb);
        voice.levelDb = clampedDb;

        // Mute threshold: at or below -60dB, gain is 0
        if (clampedDb <= kMinLevelDb) {
            voice.linearGain = 0.0f;
        } else {
            voice.linearGain = dbToGain(clampedDb);
        }
        voice.levelSmoother.setTarget(voice.linearGain);
    }

    /// @brief Set the stereo pan position for a specific voice.
    void setVoicePan(int voiceIndex, float pan) noexcept {
        if (voiceIndex < 0 || voiceIndex >= kMaxVoices) return;
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        voice.pan = std::clamp(pan, kMinPan, kMaxPan);
        voice.panSmoother.setTarget(voice.pan);
    }

    /// @brief Set the onset delay for a specific voice.
    void setVoiceDelay(int voiceIndex, float ms) noexcept {
        if (voiceIndex < 0 || voiceIndex >= kMaxVoices) return;
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        voice.delayMs = std::clamp(ms, 0.0f, kMaxDelayMs);
        voice.delaySamples = voice.delayMs * static_cast<float>(sampleRate_) /
                             1000.0f;
    }

    /// @brief Snap all internal smoothers to their current targets.
    /// Call this when transitioning from disabled to enabled to avoid
    /// a fade-in from zero (smoothers don't advance while disabled).
    void snapParameters() noexcept {
        dryLevelSmoother_.snapToTarget();
        wetLevelSmoother_.snapToTarget();
        for (auto& voice : voices_) {
            voice.levelSmoother.snapToTarget();
            voice.panSmoother.snapToTarget();
            voice.pitchSmoother.snapToTarget();
        }
    }

    /// @brief Apply a per-voice fade-in ramp for all active voices.
    /// Called by the effects chain when enabling the harmonizer, to smooth
    /// pitch shifter startup transients during the crossfade transition.
    void applyVoiceFadeIn() noexcept {
        static constexpr float kFadeInMs = 100.0f;
        const float fadeInSamples = kFadeInMs * static_cast<float>(sampleRate_) / 1000.0f;
        const float increment = (fadeInSamples > 0.0f) ? (1.0f / fadeInSamples) : 1.0f;
        for (int v = 0; v < numActiveVoices_; ++v) {
            auto& voice = voices_[static_cast<std::size_t>(v)];
            voice.fadeInGain = 0.0f;
            voice.fadeInIncrement = increment;
        }
    }

    /// @brief Set the micro-detuning for a specific voice.
    void setVoiceDetune(int voiceIndex, float cents) noexcept {
        if (voiceIndex < 0 || voiceIndex >= kMaxVoices) return;
        auto& voice = voices_[static_cast<std::size_t>(voiceIndex)];
        voice.detuneCents = std::clamp(cents, kMinDetuneCents, kMaxDetuneCents);
    }

    // =========================================================================
    // Query Methods (FR-013, FR-012, SC-010)
    // =========================================================================

    /// @brief Get the smoothed detected frequency from the PitchTracker.
    /// @return Frequency in Hz. Returns 0 if no pitch detected or in Chromatic mode.
    [[nodiscard]] float getDetectedPitch() const noexcept {
        return pitchTracker_.getFrequency();
    }

    /// @brief Get the committed MIDI note from the PitchTracker.
    /// @return MIDI note number (0-127). Returns -1 if no note committed.
    [[nodiscard]] int getDetectedNote() const noexcept {
        return pitchTracker_.getMidiNote();
    }

    /// @brief Get the raw confidence value from the PitchTracker.
    /// @return Confidence in [0.0, 1.0]. Higher = more reliable pitch estimate.
    [[nodiscard]] float getPitchConfidence() const noexcept {
        return pitchTracker_.getConfidence();
    }

    /// @brief Get the engine's processing latency in samples.
    /// @return Latency matching the underlying PitchShiftProcessor for the
    ///         configured mode. Returns 0 if not prepared.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        if (!prepared_) return 0;
        return voices_[0].pitchShifter.getLatencySamples();
    }

private:
    // =========================================================================
    // Internal Voice Structure
    // =========================================================================
    struct Voice {
        PitchShiftProcessor pitchShifter;   // L2: per-voice pitch shifting
        DelayLine           delayLine;       // L1: per-voice onset delay
        OnePoleSmoother     levelSmoother;   // L1: smooths gain changes (5ms)
        OnePoleSmoother     panSmoother;     // L1: smooths pan changes (5ms)
        OnePoleSmoother     pitchSmoother;   // L1: smooths semitone shift changes (10ms)

        // Configuration (set by public API, read in process)
        int   interval     = 0;       // diatonic steps (Scalic) or raw semitones (Chromatic)
        float levelDb      = 0.0f;    // output level in dB [-60, +6]
        float pan          = 0.0f;    // stereo position [-1.0, +1.0]
        float delayMs      = 0.0f;    // onset delay [0, 50] ms
        float detuneCents  = 0.0f;    // micro-detuning [-50, +50] cents

        // Computed (derived from configuration + pitch tracking)
        float targetSemitones = 0.0f; // total semitone shift (interval + detune)
        float linearGain      = 1.0f; // dbToGain(levelDb), 0 if muted
        float delaySamples    = 0.0f; // delayMs * sampleRate / 1000

        // Fade-in on activation (prevents click when numVoices increases)
        float fadeInGain      = 1.0f; // 0→1 ramp over ~5ms
        float fadeInIncrement = 0.0f; // per-sample increment (0 = not fading)
    };

    // =========================================================================
    // Members
    // =========================================================================

    // Shared analysis components
    PitchTracker     pitchTracker_;      // Shared, Scalic mode only
    ScaleHarmonizer  scaleHarmonizer_;   // Shared, Scalic mode only

    // Voices (always 4 allocated, only numActiveVoices_ used)
    std::array<Voice, kMaxVoices> voices_;

    // Global configuration
    HarmonyMode harmonyMode_    = HarmonyMode::Chromatic;
    int         numActiveVoices_ = 0;
    PitchMode   pitchShiftMode_ = PitchMode::Simple;
    bool        formantPreserve_ = false;

    // Global level smoothers (independent, FR-007)
    OnePoleSmoother dryLevelSmoother_;
    OnePoleSmoother wetLevelSmoother_;

    // Scratch buffers (pre-allocated in prepare())
    std::vector<float> delayScratch_;   // Delayed input per voice
    std::vector<float> voiceScratch_;   // Pitch-shifted voice output

    // Shared pitch detection (PitchSync mode optimization)
    // Runs a single PitchDetector on the mono input and passes results to all
    // PitchSync voices, eliminating 75% redundant autocorrelation for 4 voices.
    PitchDetector sharedPitchDetector_;

    // Shared-analysis resources (PhaseVocoder mode only, spec 065)
    // All three are pre-allocated in prepare() and never resized in process().
    STFT sharedStft_;                          ///< Shared forward FFT; runs once per block instead of per-voice (FR-017)
    SpectralBuffer sharedAnalysisSpectrum_;     ///< Shared analysis result; passed as const ref to all voices (FR-019)
    std::vector<float> pvVoiceScratch_;         ///< Per-voice OLA output scratch buffer; sized to maxBlockSize

    // State
    double      sampleRate_       = 44100.0;
    std::size_t maxBlockSize_     = 0;
    bool        prepared_         = false;
    int         lastDetectedNote_ = -1; // Last valid MIDI note from PitchTracker
};

} // namespace Krate::DSP
