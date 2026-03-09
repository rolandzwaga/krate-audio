#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Innexus {

/// @brief Result of ADSR envelope detection from sample analysis.
struct DetectedADSR {
    float attackMs = 10.0f;      ///< Detected attack time (ms), clamped [1, 5000]
    float decayMs = 100.0f;      ///< Detected decay time (ms), clamped [1, 5000]
    float sustainLevel = 1.0f;   ///< Detected sustain level [0, 1] relative to peak
    float releaseMs = 100.0f;    ///< Detected release time (ms), clamped [1, 5000]

    /// Frame indices defining the sustain loop region.
    /// During sustain, frame playback loops [sustainStartFrame, sustainEndFrame).
    /// If sustainStartFrame >= sustainEndFrame, no valid loop region exists.
    int sustainStartFrame = 0;   ///< First frame of the sustain (steady-state) region
    int sustainEndFrame = 0;     ///< One-past-last frame of the sustain region (= release start)
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
        float hopTimeSec) noexcept
    {
        const auto numFrames = static_cast<int>(frames.size());

        // Edge case: empty or too-short frame list -> return defaults
        if (numFrames < kMinFrames || hopTimeSec <= 0.0f)
            return DetectedADSR{};

        // --- Step 1: Extract amplitude contour ---
        std::vector<float> contour(static_cast<size_t>(numFrames));
        for (int i = 0; i < numFrames; ++i)
            contour[static_cast<size_t>(i)] = frames[static_cast<size_t>(i)].globalAmplitude;

        // --- Step 2: Find peak index ---
        int peakIdx = 0;
        float peakAmp = contour[0];
        for (int i = 1; i < numFrames; ++i)
        {
            if (contour[static_cast<size_t>(i)] > peakAmp)
            {
                peakAmp = contour[static_cast<size_t>(i)];
                peakIdx = i;
            }
        }

        // Guard: if peak amplitude is zero/negligible, return defaults
        if (peakAmp < 1e-8f)
            return DetectedADSR{};

        // --- Step 3: Compute Attack ---
        float attackMs = static_cast<float>(peakIdx) * hopTimeSec * 1000.0f;

        // --- Step 4: O(1) rolling least-squares steady-state detection ---
        // Start scanning from peak+1 onward. We maintain running sums to compute
        // slope and variance in O(1) per frame.
        //
        // Rolling window accumulates: n, sum_x, sum_y, sum_xy, sum_x2
        // Plus Welford online variance: mean, M2
        //
        // Slope = (n*sum_xy - sum_x*sum_y) / (n*sum_x2 - sum_x*sum_x)
        // Variance = M2 / n  (population variance)

        int steadyStateStart = -1; // first frame where steady-state is detected
        int steadyStateEnd = -1;   // last frame in steady-state region

        {
            int n = 0;
            float sum_x = 0.0f;
            float sum_y = 0.0f;
            float sum_xy = 0.0f;
            float sum_x2 = 0.0f;

            // Welford online variance accumulators
            float w_mean = 0.0f;
            float w_M2 = 0.0f;

            // Circular buffer to track oldest values for removal
            std::vector<float> windowBuf(static_cast<size_t>(kWindowSize), 0.0f);
            std::vector<float> windowX(static_cast<size_t>(kWindowSize), 0.0f);
            int bufIdx = 0;

            bool inSteadyState = false;

            for (int i = peakIdx + 1; i < numFrames; ++i)
            {
                float x = static_cast<float>(i - peakIdx); // local x coordinate
                float y = contour[static_cast<size_t>(i)];

                if (n < kWindowSize)
                {
                    // Grow-in phase: add new sample
                    n++;
                    sum_x += x;
                    sum_y += y;
                    sum_xy += x * y;
                    sum_x2 += x * x;

                    // Welford update
                    float delta = y - w_mean;
                    w_mean += delta / static_cast<float>(n);
                    float delta2 = y - w_mean;
                    w_M2 += delta * delta2;

                    windowBuf[static_cast<size_t>(bufIdx)] = y;
                    windowX[static_cast<size_t>(bufIdx)] = x;
                    bufIdx = (bufIdx + 1) % kWindowSize;
                }
                else
                {
                    // Sliding phase: remove oldest, add newest
                    float oldY = windowBuf[static_cast<size_t>(bufIdx)];
                    float oldX = windowX[static_cast<size_t>(bufIdx)];

                    // Remove oldest from sums
                    sum_x -= oldX;
                    sum_y -= oldY;
                    sum_xy -= oldX * oldY;
                    sum_x2 -= oldX * oldX;

                    // Remove oldest from Welford (reverse update)
                    float delta_old = oldY - w_mean;
                    w_mean = (w_mean * static_cast<float>(n) - oldY)
                             / static_cast<float>(n - 1);
                    float delta_old2 = oldY - w_mean;
                    w_M2 -= delta_old * delta_old2;

                    // Add new
                    sum_x += x;
                    sum_y += y;
                    sum_xy += x * y;
                    sum_x2 += x * x;

                    // Welford add new
                    float delta_new = y - w_mean;
                    w_mean += delta_new / static_cast<float>(n);
                    float delta_new2 = y - w_mean;
                    w_M2 += delta_new * delta_new2;

                    windowBuf[static_cast<size_t>(bufIdx)] = y;
                    windowX[static_cast<size_t>(bufIdx)] = x;
                    bufIdx = (bufIdx + 1) % kWindowSize;
                }

                // Need at least kWindowSize frames to check steady-state
                if (n < kWindowSize)
                    continue;

                // Compute slope: (n*sum_xy - sum_x*sum_y) / (n*sum_x2 - sum_x*sum_x)
                float nf = static_cast<float>(n);
                float denom = nf * sum_x2 - sum_x * sum_x;
                float slope = 0.0f;
                if (std::abs(denom) > 1e-12f)
                    slope = (nf * sum_xy - sum_x * sum_y) / denom;

                // Compute population variance
                float variance = (n > 1) ? (w_M2 / nf) : 0.0f;

                // Check steady-state conditions
                bool isSteady = (std::abs(slope) < kSlopeThreshold) &&
                                (variance < kVarianceThreshold);

                if (isSteady)
                {
                    if (!inSteadyState)
                    {
                        // Steady state just started.
                        // The start is the beginning of the current window.
                        steadyStateStart = i - kWindowSize + 1;
                        inSteadyState = true;
                    }
                    steadyStateEnd = i; // extend end
                }
                else
                {
                    // If we were in steady state but just left, that's the end.
                    // But we keep scanning in case there's another steady region.
                    if (inSteadyState)
                    {
                        // Keep the first steady-state region found
                        break;
                    }
                }
            }
        }

        // --- Step 5: Compute Decay ---
        float decayMs = 100.0f; // default
        if (steadyStateStart > peakIdx)
        {
            decayMs = static_cast<float>(steadyStateStart - peakIdx) * hopTimeSec * 1000.0f;
        }

        // --- Step 6: Compute Sustain ---
        float sustainLevel = 1.0f; // default
        if (steadyStateStart >= 0 && steadyStateEnd >= steadyStateStart)
        {
            // Mean amplitude in steady-state region / peak amplitude
            float sum = 0.0f;
            int count = 0;
            for (int i = steadyStateStart; i <= steadyStateEnd; ++i)
            {
                sum += contour[static_cast<size_t>(i)];
                count++;
            }
            if (count > 0 && peakAmp > 1e-8f)
                sustainLevel = (sum / static_cast<float>(count)) / peakAmp;
        }

        // --- Step 7: Compute Release ---
        float releaseMs = 100.0f; // default if no steady state found
        if (steadyStateEnd >= 0 && steadyStateEnd < numFrames - 1)
        {
            releaseMs = static_cast<float>(numFrames - 1 - steadyStateEnd)
                        * hopTimeSec * 1000.0f;
        }

        // --- Step 8: Clamp to valid ranges ---
        attackMs = std::clamp(attackMs, 1.0f, 5000.0f);
        decayMs = std::clamp(decayMs, 1.0f, 5000.0f);
        sustainLevel = std::clamp(sustainLevel, 0.0f, 1.0f);
        releaseMs = std::clamp(releaseMs, 1.0f, 5000.0f);

        // --- Step 9: Compute sustain loop frame indices ---
        // sustainStartFrame = where steady-state begins (after decay)
        // sustainEndFrame = where release begins (one-past sustain region)
        int susStart = 0;
        int susEnd = 0;
        if (steadyStateStart >= 0 && steadyStateEnd >= steadyStateStart)
        {
            susStart = steadyStateStart;
            // Release starts right after steady-state ends
            susEnd = steadyStateEnd + 1;
        }

        return DetectedADSR{attackMs, decayMs, sustainLevel, releaseMs,
                            susStart, susEnd};
    }

private:
    /// @brief Sliding window size for steady-state detection.
    ///
    /// FR-002 specifies the valid tuning range as 8-20 frames. This constant is the chosen fixed
    /// value within that range -- it is NOT a runtime-adaptive parameter. The window grows from 0 to
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
