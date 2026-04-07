// Contract: EnvelopeDetector API
// Location: plugins/innexus/src/dsp/envelope_detector.h
// Namespace: Innexus

#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <vector>

namespace Innexus {

/// @brief Result of ADSR envelope detection from sample analysis.
struct DetectedADSR {
    float attackMs = 10.0f;      ///< Detected attack time (ms), clamped [1, 5000]
    float decayMs = 100.0f;      ///< Detected decay time (ms), clamped [1, 5000]
    float sustainLevel = 1.0f;   ///< Detected sustain level [0, 1] relative to peak
    float releaseMs = 100.0f;    ///< Detected release time (ms), clamped [1, 5000]
};

/// @brief Extracts ADSR envelope parameters from a sequence of HarmonicFrames.
///
/// Algorithm: Peak-finding + O(1) rolling least-squares steady-state detection.
/// - Attack = time from first frame to peak amplitude frame
/// - Decay = time from peak until steady-state detected
/// - Sustain = mean amplitude in steady region / peak amplitude
/// - Release = time from last steady-state frame to end, or 100ms default
///
/// @par Thread Safety: Stateless; safe to call from any thread.
/// @par Real-Time Safety: Uses std::vector internally; NOT for audio thread.
class EnvelopeDetector {
public:
    /// @brief Detect ADSR parameters from harmonic frame amplitude contour.
    /// @param frames The harmonic analysis frames (ordered by time)
    /// @param hopTimeSec Time between consecutive frames (seconds)
    /// @return Detected ADSR parameters with sensible defaults for edge cases
    [[nodiscard]] static DetectedADSR detect(
        const std::vector<Krate::DSP::HarmonicFrame>& frames,
        float hopTimeSec) noexcept;

private:
    /// @brief Sliding window size for steady-state detection.
    ///
    /// FR-002 specifies the valid tuning range as 8–20 frames. This constant is the chosen fixed
    /// value within that range — it is NOT a runtime-adaptive parameter. The window grows from 0 to
    /// kWindowSize during the grow-in phase (tracked via the rolling `n` accumulator), then slides
    /// as a fixed-size window for the remainder of the contour.
    static constexpr int kWindowSize = 12;

    /// @brief Slope threshold for steady-state detection (per frame).
    /// FR-002: |slope| < kSlopeThreshold is one of two required steady-state conditions.
    static constexpr float kSlopeThreshold = 0.0005f;

    /// @brief Variance threshold for steady-state detection.
    /// FR-002: variance < kVarianceThreshold is the second required steady-state condition.
    static constexpr float kVarianceThreshold = 0.002f;

    /// @brief Minimum number of frames for valid detection
    static constexpr int kMinFrames = 4;
};

} // namespace Innexus
