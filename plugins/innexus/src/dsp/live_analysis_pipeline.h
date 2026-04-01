// ==============================================================================
// LiveAnalysisPipeline - Real-Time Analysis Pipeline for Sidechain Audio
// ==============================================================================
// Plugin-local DSP (not in shared library)
// Spec: specs/117-live-sidechain-mode/spec.md
// Covers: FR-003 (continuous analysis), FR-005/FR-006 (latency modes),
//         FR-008/FR-009 (real-time safety, audio thread execution)
//
// Orchestrates the full analysis chain for real-time sidechain audio:
// PreProcessingPipeline -> YinPitchDetector -> STFT -> PartialTracker ->
// MultiPitchDetector (when poly) -> MultiSourceSieve -> HarmonicModelBuilder
// -> SpectralCoringEstimator
//
// Polyphonic Analysis Upgrade:
// - Adaptive mono/poly mode switching based on YIN confidence
// - Multi-pitch detection via harmonic salience + iterative cancellation
// - Multi-source harmonic sieve for peak-to-F0 assignment
// - PolyphonicFrame output for multi-voice resynthesis
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all buffers pre-allocated in prepare())
// - Principle III: Modern C++ (C++20)
// - Principle IX: Plugin-local (depends on shared DSP library components)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "pre_processing_pipeline.h"
#include "dual_stft_config.h"
#include "plugin_ids.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/processors/yin_pitch_detector.h>
#include <krate/dsp/processors/partial_tracker.h>
#include <krate/dsp/processors/multi_pitch_detector.h>
#include <krate/dsp/processors/multi_source_sieve.h>
#include <krate/dsp/processors/spectral_coring_estimator.h>
#include <krate/dsp/processors/subharmonic_validator.h>
#include <krate/dsp/systems/harmonic_model_builder.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/spectral_buffer.h>

#include <array>
#include <cstddef>
#include <vector>

namespace Innexus {

/// @brief Real-time analysis pipeline for sidechain audio with polyphonic support.
///
/// Composes existing DSP components (PreProcessingPipeline, YinPitchDetector,
/// STFT, PartialTracker, HarmonicModelBuilder, SpectralCoringEstimator) plus
/// new polyphonic components (MultiPitchDetector, MultiSourceSieve) into a
/// single pipeline that processes live audio into HarmonicFrame or
/// PolyphonicFrame data.
///
/// Supports three analysis modes:
/// - Mono: Always use YIN monophonic detection (current behavior)
/// - Poly: Always use multi-pitch detection
/// - Auto: Switch based on YIN confidence (default)
class LiveAnalysisPipeline {
public:
    /// YIN confidence threshold for auto mode switching
    static constexpr float kPolyConfidenceThreshold = 0.6f;

    /// Number of frames to crossfade when switching modes
    static constexpr int kModeSwitchCrossfadeFrames = 4;

    LiveAnalysisPipeline() = default;
    ~LiveAnalysisPipeline() = default;

    // Non-copyable, non-movable (owns non-movable YinPitchDetector with pffft resources)
    LiveAnalysisPipeline(const LiveAnalysisPipeline&) = delete;
    LiveAnalysisPipeline& operator=(const LiveAnalysisPipeline&) = delete;
    LiveAnalysisPipeline(LiveAnalysisPipeline&&) = delete;
    LiveAnalysisPipeline& operator=(LiveAnalysisPipeline&&) = delete;

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
    /// When disabled, coringEstimator_.estimateResidual() is skipped.
    /// ~10% CPU reduction when disabled.
    void setResidualEnabled(bool enabled) noexcept { residualEnabled_ = enabled; }

    /// Set the responsiveness parameter on the internal HarmonicModelBuilder.
    /// @param value Blend factor [0.0, 1.0] (0 = slow/stable, 1 = fast/responsive)
    void setResponsiveness(float value) noexcept {
        modelBuilder_.setResponsiveness(value);
    }

    /// Set the analysis mode (Mono/Poly/Auto).
    /// @param mode Analysis mode
    void setAnalysisMode(Krate::DSP::AnalysisMode mode) noexcept {
        analysisMode_ = mode;
    }

    /// Get the current analysis mode.
    [[nodiscard]] Krate::DSP::AnalysisMode getAnalysisMode() const noexcept {
        return analysisMode_;
    }

    /// Check whether the pipeline is currently running in polyphonic mode.
    /// In Auto mode, this depends on the latest YIN confidence.
    [[nodiscard]] bool isPolyphonicActive() const noexcept {
        return lastFrameWasPolyphonic_;
    }

    /// Feed mono sidechain audio samples into the pipeline.
    /// Internally accumulates and triggers analysis when enough data available.
    /// @param data Pointer to mono audio samples
    /// @param count Number of samples
    void pushSamples(const float* data, size_t count);

    /// Check if a new analysis frame is available since last consume.
    [[nodiscard]] bool hasNewFrame() const noexcept { return newFrameAvailable_; }

    /// Get the latest harmonic frame and clear the new-frame flag.
    /// In mono mode, this is the full monophonic frame.
    /// In poly mode, this returns the first (strongest) source's frame.
    [[nodiscard]] const Krate::DSP::HarmonicFrame& consumeFrame() noexcept
    {
        newFrameAvailable_ = false;
        return latestFrame_;
    }

    /// Get the latest polyphonic frame.
    /// Only meaningful when isPolyphonicActive() returns true.
    [[nodiscard]] const Krate::DSP::PolyphonicFrame& consumePolyphonicFrame() noexcept
    {
        return latestPolyFrame_;
    }

    /// Get the latest residual frame.
    [[nodiscard]] const Krate::DSP::ResidualFrame& consumeResidualFrame() noexcept
    {
        return latestResidualFrame_;
    }

    /// Check if pipeline is prepared and ready to accept samples.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    /// Run the analysis chain: YIN, PartialTracker, ModelBuilder, Coring
    void runAnalysis();

    /// Run the polyphonic analysis path
    void runPolyphonicAnalysis(const Krate::DSP::F0Estimate& yinF0, float inputRms);

    /// Run the monophonic analysis path (original behavior)
    void runMonophonicAnalysis(const Krate::DSP::F0Estimate& f0, float inputRms);

    // =========================================================================
    // Pipeline components
    // =========================================================================
    PreProcessingPipeline preProcessing_;
    Krate::DSP::YinPitchDetector yin_{kShortWindowConfig.fftSize}; // default 1024 window
    Krate::DSP::STFT shortStft_;
    Krate::DSP::STFT longStft_;
    Krate::DSP::SpectralBuffer shortSpectrum_;
    Krate::DSP::SpectralBuffer longSpectrum_;
    Krate::DSP::SubharmonicValidator subharmonicValidator_;
    Krate::DSP::PartialTracker tracker_;
    Krate::DSP::HarmonicModelBuilder modelBuilder_;
    Krate::DSP::SpectralCoringEstimator coringEstimator_;

    // Polyphonic analysis components
    Krate::DSP::MultiPitchDetector multiPitchDetector_;
    Krate::DSP::MultiSourceSieve multiSourceSieve_;

    // =========================================================================
    // YIN circular buffer
    // =========================================================================
    std::vector<float> yinBuffer_;
    std::vector<float> yinContiguousBuffer_; // Pre-allocated buffer for unwrapping circular YIN data (FR-008)
    size_t yinWriteIndex_ = 0;
    size_t yinWindowSize_ = 0;
    bool yinBufferFilled_ = false;

    // =========================================================================
    // Pre-processing buffer (for in-place processing)
    // =========================================================================
    static constexpr size_t kMaxPreProcBlockSize = 8192;
    std::array<float, kMaxPreProcBlockSize> preProcBuffer_{};

    // =========================================================================
    // Output state
    // =========================================================================
    Krate::DSP::HarmonicFrame latestFrame_{};
    Krate::DSP::PolyphonicFrame latestPolyFrame_{};
    Krate::DSP::ResidualFrame latestResidualFrame_{};
    bool newFrameAvailable_ = false;

    // =========================================================================
    // Configuration
    // =========================================================================
    LatencyMode latencyMode_ = LatencyMode::LowLatency;
    Krate::DSP::AnalysisMode analysisMode_ = Krate::DSP::AnalysisMode::Auto;
    bool residualEnabled_ = true;
    float sampleRate_ = 44100.0f;
    bool prepared_ = false;
    bool longStftActive_ = false;

    // =========================================================================
    // Mode switching state
    // =========================================================================
    bool lastFrameWasPolyphonic_ = false;
    int modeSwitchCrossfadeRemaining_ = 0; ///< Frames remaining in mode crossfade
    Krate::DSP::HarmonicFrame previousModeFrame_{}; ///< Frame from the old mode for crossfading
};

} // namespace Innexus
