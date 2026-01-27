// ==============================================================================
// CONTRACT: Tape Delay Mode - Public Interface
// ==============================================================================
// This file defines the public API contract for the TapeDelay Layer 4 feature.
// Implementation must conform to this interface.
//
// Feature: 024-tape-delay
// Layer: 4 (User Feature)
// Reference: specs/024-tape-delay/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

class TapManager;
class FeedbackNetwork;
class CharacterProcessor;

// =============================================================================
// TapeHead Structure (FR-015 to FR-020)
// =============================================================================

/// @brief Configuration for a single tape playback head
///
/// Represents one of the 3 playback heads (like RE-201 Space Echo).
/// Head timing is relative to Motor Speed via the ratio field.
struct TapeHead {
    float ratio = 1.0f;        ///< Timing ratio (1.0, 1.5, 2.0 typical)
    float levelDb = 0.0f;      ///< Output level [-96, +6] dB (FR-017)
    float pan = 0.0f;          ///< Stereo position [-100, +100] (FR-018)
    bool enabled = true;       ///< Head output enable (FR-016)
};

// =============================================================================
// TapeDelay Class - Public Interface
// =============================================================================

/// @brief Layer 4 User Feature - Classic Tape Delay Emulation
///
/// Emulates vintage tape echo units (Roland RE-201, Echoplex, Watkins Copicat).
/// Composes Layer 3 components: TapManager, FeedbackNetwork, CharacterProcessor.
///
/// @par User Controls
/// - Motor Speed: Delay time with motor inertia (FR-001 to FR-004)
/// - Wear: Wow/flutter depth + hiss level (FR-005 to FR-009)
/// - Saturation: Tape drive amount (FR-010 to FR-014)
/// - Age: EQ rolloff + noise + degradation (FR-021 to FR-025)
/// - Echo Heads: 3 playback heads at fixed ratios (FR-015 to FR-020)
/// - Feedback: Echo repeats with filtering (FR-026 to FR-030)
/// - Mix: Dry/wet balance (FR-031 to FR-033)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// TapeDelay delay;
/// delay.prepare(44100.0, 512, 2000.0f);
/// delay.setMotorSpeed(500.0f);  // 500ms delay
/// delay.setWear(0.3f);          // Moderate wow/flutter
/// delay.setFeedback(0.5f);      // 50% feedback
///
/// // In process callback
/// delay.process(left, right, numSamples);
/// @endcode
class TapeDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumHeads = 3;           ///< Number of playback heads
    static constexpr float kMinDelayMs = 20.0f;      ///< Minimum delay (FR-002)
    static constexpr float kMaxDelayMs = 2000.0f;    ///< Maximum delay (FR-002)
    static constexpr float kHeadRatio1 = 1.0f;       ///< Head 1 timing ratio
    static constexpr float kHeadRatio2 = 1.5f;       ///< Head 2 timing ratio
    static constexpr float kHeadRatio3 = 2.0f;       ///< Head 3 timing ratio

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    TapeDelay() noexcept;
    ~TapeDelay();

    // Non-copyable, movable
    TapeDelay(const TapeDelay&) = delete;
    TapeDelay& operator=(const TapeDelay&) = delete;
    TapeDelay(TapeDelay&&) noexcept;
    TapeDelay& operator=(TapeDelay&&) noexcept;

    // =========================================================================
    // Lifecycle Methods (FR-034 to FR-036)
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    /// @post Ready for process() calls
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept;

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Motor Speed / Delay Time (FR-001 to FR-004)
    // =========================================================================

    /// @brief Set delay time (Motor Speed control)
    /// @param ms Delay time in milliseconds [20, 2000]
    /// @note Changes smoothly with motor inertia (200-500ms transition)
    void setMotorSpeed(float ms) noexcept;

    /// @brief Get current (smoothed) delay time
    [[nodiscard]] float getCurrentDelayMs() const noexcept;

    /// @brief Get target delay time
    [[nodiscard]] float getTargetDelayMs() const noexcept;

    /// @brief Set motor inertia time
    /// @param ms Transition time in milliseconds [100, 1000]
    void setMotorInertia(float ms) noexcept;

    // =========================================================================
    // Wear (Wow/Flutter/Hiss) (FR-005 to FR-009)
    // =========================================================================

    /// @brief Set wear amount
    /// @param amount Wear [0, 1] - controls wow/flutter depth and hiss level
    void setWear(float amount) noexcept;

    /// @brief Get current wear amount
    [[nodiscard]] float getWear() const noexcept;

    // =========================================================================
    // Saturation (FR-010 to FR-014)
    // =========================================================================

    /// @brief Set tape saturation amount
    /// @param amount Saturation [0, 1] - controls tape drive/warmth
    void setSaturation(float amount) noexcept;

    /// @brief Get current saturation amount
    [[nodiscard]] float getSaturation() const noexcept;

    // =========================================================================
    // Age / Degradation (FR-021 to FR-025)
    // =========================================================================

    /// @brief Set age/degradation amount
    /// @param amount Age [0, 1] - controls EQ rolloff, noise, degradation
    void setAge(float amount) noexcept;

    /// @brief Get current age amount
    [[nodiscard]] float getAge() const noexcept;

    // =========================================================================
    // Echo Heads (FR-015 to FR-020)
    // =========================================================================

    /// @brief Set head enabled state
    /// @param headIndex Head index [0, 2]
    /// @param enabled Whether head contributes to output
    void setHeadEnabled(size_t headIndex, bool enabled) noexcept;

    /// @brief Set head output level
    /// @param headIndex Head index [0, 2]
    /// @param levelDb Level in dB [-96, +6]
    void setHeadLevel(size_t headIndex, float levelDb) noexcept;

    /// @brief Set head pan position
    /// @param headIndex Head index [0, 2]
    /// @param pan Pan position [-100, +100]
    void setHeadPan(size_t headIndex, float pan) noexcept;

    /// @brief Get head configuration
    /// @param headIndex Head index [0, 2]
    /// @return Copy of head configuration
    [[nodiscard]] TapeHead getHead(size_t headIndex) const noexcept;

    /// @brief Check if head is enabled
    [[nodiscard]] bool isHeadEnabled(size_t headIndex) const noexcept;

    // =========================================================================
    // Feedback (FR-026 to FR-030)
    // =========================================================================

    /// @brief Set feedback amount
    /// @param amount Feedback [0, 1.2] (>1.0 enables self-oscillation)
    void setFeedback(float amount) noexcept;

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedback() const noexcept;

    // =========================================================================
    // Mix (FR-031)
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param amount Mix [0, 1] (0 = dry, 1 = wet)
    void setMix(float amount) noexcept;

    /// @brief Get current mix amount
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Output Level (FR-032)
    // =========================================================================

    /// @brief Set output level
    /// @param dB Output level in dB [-96, +12]
    void setOutputLevel(float dB) noexcept;

    /// @brief Get current output level
    [[nodiscard]] float getOutputLevel() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free (FR-034, FR-035)
    void process(float* left, float* right, size_t numSamples) noexcept;

    /// @brief Process mono audio in-place
    /// @param buffer Mono buffer (modified in-place)
    /// @param numSamples Number of samples
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get number of active (enabled) heads
    [[nodiscard]] size_t getActiveHeadCount() const noexcept;

    /// @brief Check if currently transitioning (motor inertia active)
    [[nodiscard]] bool isTransitioning() const noexcept;
};

} // namespace DSP
} // namespace Iterum
