#pragma once

// ==============================================================================
// Innexus - Sample Analysis Result
// ==============================================================================
// Stores the result of analyzing a loaded audio file: a time-indexed sequence
// of HarmonicFrames representing the evolving harmonic content of the sample.
//
// Ownership: allocated and populated by the background analysis thread, then
// published to the audio thread via std::atomic<SampleAnalysis*> (release on
// write, acquire on read). Immutable after publication -- the audio thread
// reads it; the background thread never writes to it again.
//
// Constitution Compliance:
// - Principle II: Immutable after publication (no audio-thread allocations)
// - Principle III: Modern C++ (RAII, noexcept)
//
// Reference: spec.md FR-046
// ==============================================================================

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <string>
#include <vector>

namespace Innexus {

/// @brief Stores the complete result of harmonic analysis on a loaded audio file.
///
/// Contains a time-indexed sequence of HarmonicFrames, one per analysis hop.
/// This object is immutable after publication via atomic pointer swap (FR-058).
struct SampleAnalysis {
    std::vector<Krate::DSP::HarmonicFrame> frames; ///< Time-indexed harmonic frames
    std::vector<Krate::DSP::ResidualFrame> residualFrames; ///< Time-indexed residual frames (M2)
    float sampleRate = 0.0f;                         ///< Source sample rate (Hz)
    float hopTimeSec = 0.0f;                         ///< Time between frames (seconds)
    size_t totalFrames = 0;                          ///< Number of frames
    std::string filePath;                            ///< Source file path (for state persistence)
    size_t analysisFFTSize = 0;                      ///< Short-window FFT size used during analysis (M2)
    size_t analysisHopSize = 0;                      ///< Short-window hop size used during analysis (M2)

    /// @brief Get a frame by index, clamped to valid range.
    /// @param index Frame index
    /// @return Reference to the frame (last frame if index >= totalFrames)
    [[nodiscard]] const Krate::DSP::HarmonicFrame& getFrame(size_t index) const noexcept
    {
        if (frames.empty()) {
            static const Krate::DSP::HarmonicFrame kEmpty{};
            return kEmpty;
        }
        if (index >= totalFrames) {
            return frames[totalFrames - 1];
        }
        return frames[index];
    }

    /// @brief Get a residual frame by index, clamped to valid range.
    /// Returns a default (silent) frame if residualFrames is empty (M1 backward compat).
    [[nodiscard]] const Krate::DSP::ResidualFrame& getResidualFrame(size_t index) const noexcept
    {
        if (residualFrames.empty()) {
            static const Krate::DSP::ResidualFrame kEmpty{};
            return kEmpty;
        }
        if (index >= residualFrames.size()) {
            return residualFrames[residualFrames.size() - 1];
        }
        return residualFrames[index];
    }

    /// @brief Get the number of frames in the analysis.
    [[nodiscard]] size_t frameCount() const noexcept { return totalFrames; }
};

} // namespace Innexus
