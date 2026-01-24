// ==============================================================================
// API Contract: FilterFeedbackMatrix
// ==============================================================================
// This file defines the PUBLIC API contract for the FilterFeedbackMatrix.
// Implementation must match these signatures exactly.
//
// Feature: 096-filter-feedback-matrix
// Layer: 3 (Systems)
// Date: 2026-01-24
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

#include <array>
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
    /// @param ms Delay time in milliseconds (clamped to [0, 100+])
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
    // Implementation details not part of public contract
    // See filter_feedback_matrix.h for actual implementation
};

// Common instantiations
extern template class FilterFeedbackMatrix<2>;
extern template class FilterFeedbackMatrix<3>;
extern template class FilterFeedbackMatrix<4>;

} // namespace DSP
} // namespace Krate
