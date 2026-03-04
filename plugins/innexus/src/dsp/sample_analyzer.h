#pragma once

// ==============================================================================
// Innexus - Sample Analyzer
// ==============================================================================
// Loads WAV/AIFF audio files and runs the complete analysis pipeline on a
// background thread: PreProcessingPipeline -> YIN -> dual STFT ->
// PartialTracker -> HarmonicModelBuilder.
//
// The completed SampleAnalysis is transferred to the audio thread via
// std::atomic<SampleAnalysis*> with release/acquire semantics (FR-058).
//
// Constitution Compliance:
// - Principle II: Background thread only, never blocks audio thread (FR-044)
// - Principle III: Modern C++ (std::thread, std::atomic, std::unique_ptr)
// - Principle VI: Cross-platform (dr_wav + std::thread, no platform-specific code)
//
// Reference: spec.md FR-043 to FR-047, FR-058
// ==============================================================================

#include "sample_analysis.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Innexus {

/// @brief Loads audio files and runs harmonic analysis on a background thread.
///
/// Usage:
///   SampleAnalyzer analyzer;
///   analyzer.startAnalysis("path/to/sample.wav");
///   // ... poll isComplete() ...
///   auto result = analyzer.takeResult();
class SampleAnalyzer {
public:
    SampleAnalyzer() = default;
    ~SampleAnalyzer();

    // Non-copyable, non-movable (owns a thread)
    SampleAnalyzer(const SampleAnalyzer&) = delete;
    SampleAnalyzer& operator=(const SampleAnalyzer&) = delete;
    SampleAnalyzer(SampleAnalyzer&&) = delete;
    SampleAnalyzer& operator=(SampleAnalyzer&&) = delete;

    /// @brief Start analysis on a background thread (FR-044).
    /// @param filePath Path to WAV or AIFF audio file
    /// @note Returns immediately. Call isComplete() to check progress.
    /// @note If analysis is already running, it will be cancelled first.
    void startAnalysis(const std::string& filePath);

    /// @brief Check if analysis has completed.
    /// @return true if analysis is done (result available via takeResult())
    [[nodiscard]] bool isComplete() const noexcept;

    /// @brief Take the analysis result. Call only after isComplete() returns true.
    /// @return The analysis result, or nullptr if not yet complete or already taken
    [[nodiscard]] std::unique_ptr<SampleAnalysis> takeResult();

    /// @brief Cancel ongoing analysis without crash.
    void cancel();

private:
    /// @brief Run the full analysis pipeline on the background thread (FR-045).
    /// @param audioData Loaded audio samples (mono, float32)
    /// @param sampleRate Source sample rate in Hz
    /// @param filePath Source file path for state persistence
    void analyzeOnThread(std::vector<float> audioData, float sampleRate,
                         std::string filePath);

    /// @brief Wait for the analysis thread to finish and join it.
    void joinThread();

    std::thread analysisThread_;
    std::atomic<bool> complete_{false};
    std::atomic<bool> cancelled_{false};
    std::unique_ptr<SampleAnalysis> result_;
};

} // namespace Innexus
