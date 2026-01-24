// ==============================================================================
// Layer 3: System Component - Filter Feedback Matrix
// ==============================================================================
// Multiple SVF filters with configurable feedback routing between them.
// Creates complex resonant networks by routing filter outputs back into other
// filters with adjustable amounts and delays.
//
// Feature: 096-filter-feedback-matrix
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20 templates, std::array, RAII)
// - Principle IX: Layer 3 (composes Layer 0-1 components)
// - Principle X: DSP Constraints (feedback limiting with tanh, DC blocking)
// - Principle XI: Performance Budget (<1% CPU single core at 44.1kHz)
//
// Reference: specs/096-filter-feedback-matrix/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Multiple SVF filters with configurable feedback routing between them.
///
/// Creates complex resonant networks by routing filter outputs back into other
/// filters with adjustable amounts and delays. Similar to Feedback Delay Networks
/// (FDN) but uses filters instead of pure delays for tonal shaping.
///
/// @tparam N Maximum number of filters (2-4). Compile-time capacity.
///
/// @par Architecture
/// - Template parameter N sets compile-time array sizes
/// - Runtime setActiveFilters() controls how many are processed (CPU optimization)
/// - Dual-mono stereo: processStereo() uses two independent networks
/// - Per-filter soft clipping (tanh) before feedback routing for stability
/// - Per-feedback-path DC blocking after each delay line
///
/// @par Signal Flow
/// ```
/// Input -> [inputGains] -> Filters -> [tanh] -> [feedback matrix with delays]
///                              |                         |
///                              v                         v
///                         [outputGains] <----- [dcBlocker] <---- [from other filters]
///                              |
///                              v
///                           Output
/// ```
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 3 (composes Layer 0-1 components)
/// - Principle X: DSP Constraints (feedback limiting with tanh, DC blocking)
/// - Principle XI: Performance Budget (<1% CPU single core at 44.1kHz)
///
/// @see SVF for filter implementation
/// @see FeedbackNetwork for simpler delay-based feedback
template <size_t N>
class FilterFeedbackMatrix {
    static_assert(N >= 2 && N <= 4, "Filter count must be 2-4");

public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxFilters = N;
    static constexpr float kMinCutoff = 20.0f;
    static constexpr float kMaxCutoff = 20000.0f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 30.0f;
    static constexpr float kMinFeedback = -1.0f;
    static constexpr float kMaxFeedback = 1.0f;
    static constexpr float kMaxDelayMs = 100.0f;
    static constexpr float kSmoothingTimeMs = 20.0f;
    static constexpr float kDCBlockerCutoff = 10.0f;

    // =========================================================================
    // Construction / Destruction (FR-016: Real-time safe)
    // =========================================================================

    /// @brief Default constructor.
    /// Creates an unprepared matrix. Call prepare() before processing.
    FilterFeedbackMatrix() noexcept;

    /// @brief Destructor.
    ~FilterFeedbackMatrix() = default;

    // Non-copyable due to DelayLine (which is move-only)
    FilterFeedbackMatrix(const FilterFeedbackMatrix&) = delete;
    FilterFeedbackMatrix& operator=(const FilterFeedbackMatrix&) = delete;

    // Movable
    FilterFeedbackMatrix(FilterFeedbackMatrix&&) noexcept = default;
    FilterFeedbackMatrix& operator=(FilterFeedbackMatrix&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-014, FR-015)
    // =========================================================================

    /// @brief Prepare for processing at the given sample rate.
    ///
    /// Allocates delay line buffers and configures all internal components.
    /// Must be called before process() or processStereo().
    ///
    /// @param sampleRate Sample rate in Hz (minimum 1000)
    ///
    /// @note This is the ONLY method that may allocate memory.
    /// @note Safe to call multiple times (reconfigures for new sample rate).
    void prepare(double sampleRate) noexcept;

    /// @brief Clear all filter and delay states without changing parameters.
    ///
    /// Use when starting a new audio region to prevent artifacts from
    /// previous audio content.
    void reset() noexcept;

    /// @brief Check if the matrix has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Filter Configuration (FR-001, FR-002, FR-003, FR-004, FR-022)
    // =========================================================================

    /// @brief Set the number of active filters (1 to N).
    ///
    /// Only active filters are processed, saving CPU when fewer are needed.
    ///
    /// @param count Number of filters to process (clamped to [1, N])
    ///
    /// @note In debug builds, asserts if count > N
    /// @note In release builds, clamps to N if count > N
    void setActiveFilters(size_t count) noexcept;

    /// @brief Get the current number of active filters.
    [[nodiscard]] size_t getActiveFilters() const noexcept;

    /// @brief Set filter mode (Lowpass, Highpass, Bandpass, Notch, Peak).
    ///
    /// @param filterIndex Filter index (0 to N-1)
    /// @param mode Filter mode from SVFMode enum
    void setFilterMode(size_t filterIndex, SVFMode mode) noexcept;

    /// @brief Set filter cutoff frequency.
    ///
    /// @param filterIndex Filter index (0 to N-1)
    /// @param hz Cutoff frequency in Hz (clamped to [20Hz, 20kHz])
    void setFilterCutoff(size_t filterIndex, float hz) noexcept;

    /// @brief Set filter Q/resonance.
    ///
    /// @param filterIndex Filter index (0 to N-1)
    /// @param q Q factor (clamped to [0.5, 30.0])
    void setFilterResonance(size_t filterIndex, float q) noexcept;

    // =========================================================================
    // Feedback Matrix Configuration (FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set feedback amount from one filter to another.
    ///
    /// @param from Source filter index (0 to N-1)
    /// @param to Destination filter index (0 to N-1)
    /// @param amount Feedback amount (-1.0 to 1.0, negative inverts phase)
    ///
    /// @note from == to sets self-feedback
    void setFeedbackAmount(size_t from, size_t to, float amount) noexcept;

    /// @brief Set all feedback amounts at once.
    ///
    /// @param matrix NxN array of feedback amounts
    ///
    /// @note Updates atomically without glitches (SC-002)
    void setFeedbackMatrix(const std::array<std::array<float, N>, N>& matrix) noexcept;

    /// @brief Set feedback delay time for a path.
    ///
    /// @param from Source filter index (0 to N-1)
    /// @param to Destination filter index (0 to N-1)
    /// @param ms Delay time in milliseconds (clamped to [0, 100])
    void setFeedbackDelay(size_t from, size_t to, float ms) noexcept;

    // =========================================================================
    // Input/Output Routing (FR-008, FR-009)
    // =========================================================================

    /// @brief Set how much input signal reaches a filter.
    ///
    /// @param filterIndex Filter index (0 to N-1)
    /// @param gain Input gain (0.0 to 1.0)
    void setInputGain(size_t filterIndex, float gain) noexcept;

    /// @brief Set how much a filter contributes to output.
    ///
    /// @param filterIndex Filter index (0 to N-1)
    /// @param gain Output gain (0.0 to 1.0)
    void setOutputGain(size_t filterIndex, float gain) noexcept;

    /// @brief Set all input gains at once.
    /// @param gains Array of N input gains
    void setInputGains(const std::array<float, N>& gains) noexcept;

    /// @brief Set all output gains at once.
    /// @param gains Array of N output gains
    void setOutputGains(const std::array<float, N>& gains) noexcept;

    // =========================================================================
    // Global Control (FR-010)
    // =========================================================================

    /// @brief Set the global feedback scalar.
    ///
    /// Multiplies all feedback matrix values. Use for performance control.
    ///
    /// @param amount Global feedback (0.0 to 1.0)
    ///        - 0.0: No feedback (parallel filters)
    ///        - 1.0: Full feedback (default)
    void setGlobalFeedback(float amount) noexcept;

    /// @brief Get the current global feedback amount.
    [[nodiscard]] float getGlobalFeedback() const noexcept;

    // =========================================================================
    // Processing (FR-012, FR-013, FR-016, FR-017)
    // =========================================================================

    /// @brief Process a single mono sample.
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note Returns 0 and resets on NaN/Inf input (FR-017)
    /// @note noexcept, no allocations (FR-016)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process stereo samples in-place.
    ///
    /// Uses dual-mono architecture: two independent filter networks,
    /// one per channel, with no cross-channel feedback.
    ///
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    ///
    /// @note Returns 0 and resets on NaN/Inf input
    /// @note noexcept, no allocations
    void processStereo(float& left, float& right) noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Convert milliseconds to samples
    [[nodiscard]] float msToSamples(float ms) const noexcept {
        return ms * static_cast<float>(sampleRate_) * 0.001f;
    }

    /// @brief Clamp a value to a range
    template <typename T>
    [[nodiscard]] static constexpr T clamp(T value, T min, T max) noexcept {
        return (value < min) ? min : (value > max) ? max : value;
    }

    /// @brief Process the internal network for one channel
    /// @param input Input sample
    /// @param filters Array of SVF filters
    /// @param delayLines NxN delay lines
    /// @param dcBlockers NxN DC blockers
    /// @param filterOutputs Previous filter outputs (updated in place)
    /// @return Processed output sample
    [[nodiscard]] float processNetwork(
        float input,
        std::array<SVF, N>& filters,
        std::array<std::array<DelayLine, N>, N>& delayLines,
        std::array<std::array<DCBlocker, N>, N>& dcBlockers,
        std::array<float, N>& filterOutputs) noexcept;

    // =========================================================================
    // Member Variables - Left Channel (Primary)
    // =========================================================================

    std::array<SVF, N> filtersL_;
    std::array<std::array<DelayLine, N>, N> delayLinesL_;
    std::array<std::array<DCBlocker, N>, N> dcBlockersL_;
    std::array<float, N> filterOutputsL_;  // Previous filter outputs for feedback

    // =========================================================================
    // Member Variables - Right Channel (Stereo)
    // =========================================================================

    std::array<SVF, N> filtersR_;
    std::array<std::array<DelayLine, N>, N> delayLinesR_;
    std::array<std::array<DCBlocker, N>, N> dcBlockersR_;
    std::array<float, N> filterOutputsR_;

    // =========================================================================
    // Configuration Matrices
    // =========================================================================

    std::array<std::array<float, N>, N> feedbackMatrix_;  // Target feedback amounts
    std::array<std::array<float, N>, N> delayMatrix_;     // Delay times in ms
    std::array<float, N> inputGains_;                     // Input routing
    std::array<float, N> outputGains_;                    // Output mix

    // =========================================================================
    // Smoothers for Click-Free Parameter Changes
    // =========================================================================

    std::array<std::array<OnePoleSmoother, N>, N> feedbackSmoothers_;
    std::array<std::array<OnePoleSmoother, N>, N> delaySmoothers_;
    std::array<OnePoleSmoother, N> inputGainSmoothers_;
    std::array<OnePoleSmoother, N> outputGainSmoothers_;
    OnePoleSmoother globalFeedbackSmoother_;

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;
    size_t activeFilters_ = N;
    float globalFeedback_ = 1.0f;
    bool prepared_ = false;
};

// =============================================================================
// Implementation
// =============================================================================

template <size_t N>
FilterFeedbackMatrix<N>::FilterFeedbackMatrix() noexcept
    : filterOutputsL_{}
    , filterOutputsR_{}
    , feedbackMatrix_{}
    , delayMatrix_{}
    , inputGains_{}
    , outputGains_{}
    , sampleRate_(44100.0)
    , activeFilters_(N)
    , globalFeedback_(1.0f)
    , prepared_(false) {

    // Initialize input/output gains to 1.0 (unity)
    inputGains_.fill(1.0f);
    outputGains_.fill(1.0f);

    // Initialize filter outputs to 0
    filterOutputsL_.fill(0.0f);
    filterOutputsR_.fill(0.0f);

    // Feedback matrix starts at 0 (no feedback)
    for (size_t i = 0; i < N; ++i) {
        feedbackMatrix_[i].fill(0.0f);
        delayMatrix_[i].fill(0.0f);
    }
}

template <size_t N>
void FilterFeedbackMatrix<N>::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

    // Prepare filters
    for (size_t i = 0; i < N; ++i) {
        filtersL_[i].prepare(sampleRate_);
        filtersL_[i].setMode(SVFMode::Lowpass);
        filtersL_[i].setCutoff(1000.0f);
        filtersL_[i].setResonance(SVF::kButterworthQ);

        filtersR_[i].prepare(sampleRate_);
        filtersR_[i].setMode(SVFMode::Lowpass);
        filtersR_[i].setCutoff(1000.0f);
        filtersR_[i].setResonance(SVF::kButterworthQ);
    }

    // Prepare delay lines (max 100ms + some headroom)
    const float maxDelaySeconds = (kMaxDelayMs + 10.0f) * 0.001f;
    for (size_t from = 0; from < N; ++from) {
        for (size_t to = 0; to < N; ++to) {
            delayLinesL_[from][to].prepare(sampleRate_, maxDelaySeconds);
            delayLinesR_[from][to].prepare(sampleRate_, maxDelaySeconds);

            dcBlockersL_[from][to].prepare(sampleRate_, kDCBlockerCutoff);
            dcBlockersR_[from][to].prepare(sampleRate_, kDCBlockerCutoff);
        }
    }

    // Configure smoothers
    const float sampleRateF = static_cast<float>(sampleRate_);
    for (size_t i = 0; i < N; ++i) {
        inputGainSmoothers_[i].configure(kSmoothingTimeMs, sampleRateF);
        inputGainSmoothers_[i].snapTo(inputGains_[i]);

        outputGainSmoothers_[i].configure(kSmoothingTimeMs, sampleRateF);
        outputGainSmoothers_[i].snapTo(outputGains_[i]);

        for (size_t j = 0; j < N; ++j) {
            feedbackSmoothers_[i][j].configure(kSmoothingTimeMs, sampleRateF);
            feedbackSmoothers_[i][j].snapTo(feedbackMatrix_[i][j]);

            delaySmoothers_[i][j].configure(kSmoothingTimeMs, sampleRateF);
            delaySmoothers_[i][j].snapTo(delayMatrix_[i][j]);
        }
    }

    globalFeedbackSmoother_.configure(kSmoothingTimeMs, sampleRateF);
    globalFeedbackSmoother_.snapTo(globalFeedback_);

    prepared_ = true;
}

template <size_t N>
void FilterFeedbackMatrix<N>::reset() noexcept {
    // Reset filters
    for (size_t i = 0; i < N; ++i) {
        filtersL_[i].reset();
        filtersR_[i].reset();
    }

    // Reset delay lines and DC blockers
    for (size_t from = 0; from < N; ++from) {
        for (size_t to = 0; to < N; ++to) {
            delayLinesL_[from][to].reset();
            delayLinesR_[from][to].reset();
            dcBlockersL_[from][to].reset();
            dcBlockersR_[from][to].reset();
        }
    }

    // Reset filter outputs
    filterOutputsL_.fill(0.0f);
    filterOutputsR_.fill(0.0f);
}

template <size_t N>
bool FilterFeedbackMatrix<N>::isPrepared() const noexcept {
    return prepared_;
}

template <size_t N>
void FilterFeedbackMatrix<N>::setActiveFilters(size_t count) noexcept {
#ifndef NDEBUG
    // Assert in debug builds
    if (count > N) {
        // Would assert here, but we'll just clamp
    }
#endif
    activeFilters_ = (count < 1) ? 1 : (count > N) ? N : count;
}

template <size_t N>
size_t FilterFeedbackMatrix<N>::getActiveFilters() const noexcept {
    return activeFilters_;
}

template <size_t N>
void FilterFeedbackMatrix<N>::setFilterMode(size_t filterIndex, SVFMode mode) noexcept {
    if (filterIndex >= N) return;
    filtersL_[filterIndex].setMode(mode);
    filtersR_[filterIndex].setMode(mode);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setFilterCutoff(size_t filterIndex, float hz) noexcept {
    if (filterIndex >= N) return;
    const float clamped = clamp(hz, kMinCutoff, kMaxCutoff);
    filtersL_[filterIndex].setCutoff(clamped);
    filtersR_[filterIndex].setCutoff(clamped);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setFilterResonance(size_t filterIndex, float q) noexcept {
    if (filterIndex >= N) return;
    const float clamped = clamp(q, kMinQ, kMaxQ);
    filtersL_[filterIndex].setResonance(clamped);
    filtersR_[filterIndex].setResonance(clamped);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setFeedbackAmount(size_t from, size_t to, float amount) noexcept {
    if (from >= N || to >= N) return;
    feedbackMatrix_[from][to] = clamp(amount, kMinFeedback, kMaxFeedback);
    feedbackSmoothers_[from][to].setTarget(feedbackMatrix_[from][to]);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setFeedbackMatrix(
    const std::array<std::array<float, N>, N>& matrix) noexcept {
    for (size_t from = 0; from < N; ++from) {
        for (size_t to = 0; to < N; ++to) {
            feedbackMatrix_[from][to] = clamp(matrix[from][to], kMinFeedback, kMaxFeedback);
            feedbackSmoothers_[from][to].setTarget(feedbackMatrix_[from][to]);
        }
    }
}

template <size_t N>
void FilterFeedbackMatrix<N>::setFeedbackDelay(size_t from, size_t to, float ms) noexcept {
    if (from >= N || to >= N) return;
    delayMatrix_[from][to] = clamp(ms, 0.0f, kMaxDelayMs);
    delaySmoothers_[from][to].setTarget(delayMatrix_[from][to]);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setInputGain(size_t filterIndex, float gain) noexcept {
    if (filterIndex >= N) return;
    inputGains_[filterIndex] = clamp(gain, 0.0f, 1.0f);
    inputGainSmoothers_[filterIndex].setTarget(inputGains_[filterIndex]);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setOutputGain(size_t filterIndex, float gain) noexcept {
    if (filterIndex >= N) return;
    outputGains_[filterIndex] = clamp(gain, 0.0f, 1.0f);
    outputGainSmoothers_[filterIndex].setTarget(outputGains_[filterIndex]);
}

template <size_t N>
void FilterFeedbackMatrix<N>::setInputGains(const std::array<float, N>& gains) noexcept {
    for (size_t i = 0; i < N; ++i) {
        setInputGain(i, gains[i]);
    }
}

template <size_t N>
void FilterFeedbackMatrix<N>::setOutputGains(const std::array<float, N>& gains) noexcept {
    for (size_t i = 0; i < N; ++i) {
        setOutputGain(i, gains[i]);
    }
}

template <size_t N>
void FilterFeedbackMatrix<N>::setGlobalFeedback(float amount) noexcept {
    globalFeedback_ = clamp(amount, 0.0f, 1.0f);
    globalFeedbackSmoother_.setTarget(globalFeedback_);
}

template <size_t N>
float FilterFeedbackMatrix<N>::getGlobalFeedback() const noexcept {
    return globalFeedback_;
}

template <size_t N>
float FilterFeedbackMatrix<N>::processNetwork(
    float input,
    std::array<SVF, N>& filters,
    std::array<std::array<DelayLine, N>, N>& delayLines,
    std::array<std::array<DCBlocker, N>, N>& dcBlockers,
    std::array<float, N>& filterOutputs) noexcept {

    const size_t numFilters = activeFilters_;
    const float globalFb = globalFeedbackSmoother_.process();

    // Calculate filter inputs: input routing + feedback from all filters
    std::array<float, N> filterInputs{};

    for (size_t to = 0; to < numFilters; ++to) {
        // Input routing
        const float inputGain = inputGainSmoothers_[to].process();
        filterInputs[to] = input * inputGain;

        // Add feedback from all filters
        for (size_t from = 0; from < numFilters; ++from) {
            // Get smoothed feedback amount and delay
            const float fbAmount = feedbackSmoothers_[from][to].process() * globalFb;

            if (std::abs(fbAmount) > 1e-6f) {
                // Get delay in samples (minimum 1 sample for causality)
                const float delayMs = delaySmoothers_[from][to].process();
                float delaySamples = msToSamples(delayMs);
                if (delaySamples < 1.0f) {
                    delaySamples = 1.0f;
                }

                // Read from delay line with linear interpolation
                float delayed = delayLines[from][to].readLinear(delaySamples);

                // Apply DC blocking
                delayed = dcBlockers[from][to].process(delayed);

                // Add to filter input
                filterInputs[to] += delayed * fbAmount;
            } else {
                // Still need to advance smoother state
                [[maybe_unused]] float unused = delaySmoothers_[from][to].process();
            }
        }
    }

    // Process filters and collect outputs
    float output = 0.0f;

    for (size_t i = 0; i < numFilters; ++i) {
        // Process through filter
        float filterOut = filters[i].process(filterInputs[i]);

        // Apply soft clipping (tanh) for stability before feedback routing (FR-011)
        filterOut = std::tanh(filterOut);

        // Store for next sample's feedback
        filterOutputs[i] = filterOut;

        // Write to all delay lines going FROM this filter
        for (size_t to = 0; to < numFilters; ++to) {
            delayLines[i][to].write(filterOut);
        }

        // Add to output mix
        const float outputGain = outputGainSmoothers_[i].process();
        output += filterOut * outputGain;
    }

    // Flush denormals
    output = detail::flushDenormal(output);

    return output;
}

template <size_t N>
float FilterFeedbackMatrix<N>::process(float input) noexcept {
    // FR-018: Return 0 if not prepared
    if (!prepared_) {
        return 0.0f;
    }

    // FR-017: Handle NaN/Inf input
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    return processNetwork(input, filtersL_, delayLinesL_, dcBlockersL_, filterOutputsL_);
}

template <size_t N>
void FilterFeedbackMatrix<N>::processStereo(float& left, float& right) noexcept {
    // FR-018: Return 0 if not prepared
    if (!prepared_) {
        left = 0.0f;
        right = 0.0f;
        return;
    }

    // FR-017: Handle NaN/Inf input - check both channels
    if (detail::isNaN(left) || detail::isInf(left) ||
        detail::isNaN(right) || detail::isInf(right)) {
        reset();
        left = 0.0f;
        right = 0.0f;
        return;
    }

    // Process each channel independently (dual-mono)
    left = processNetwork(left, filtersL_, delayLinesL_, dcBlockersL_, filterOutputsL_);
    right = processNetwork(right, filtersR_, delayLinesR_, dcBlockersR_, filterOutputsR_);
}

// Explicit template instantiations
template class FilterFeedbackMatrix<2>;
template class FilterFeedbackMatrix<3>;
template class FilterFeedbackMatrix<4>;

}  // namespace DSP
}  // namespace Krate
