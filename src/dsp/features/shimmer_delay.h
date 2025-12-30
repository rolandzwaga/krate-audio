// ==============================================================================
// Layer 4: User Feature - ShimmerDelay
// ==============================================================================
// Pitch-shifted feedback delay creating ethereal, cascading harmonic textures.
// Classic shimmer effect (Strymon BigSky, Eventide Space, Valhalla Shimmer).
//
// Composes:
// - FlexibleFeedbackNetwork (Layer 3): Feedback loop with processor injection
// - PitchShiftProcessor (Layer 2): 2 instances for stereo pitch shifting
// - DiffusionNetwork (Layer 2): Smearing for reverb-like texture
// - OnePoleSmoother (Layer 1): Parameter smoothing
// - ModulationMatrix (Layer 3): Optional external modulation
//
// Feature: 029-shimmer-delay
// Layer: 4 (User Feature)
// Reference: specs/029-shimmer-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/note_value.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/diffusion_network.h"
#include "dsp/processors/pitch_shift_processor.h"
#include "dsp/systems/delay_engine.h"            // For TimeMode enum
#include "dsp/systems/flexible_feedback_network.h"
#include "dsp/systems/i_feedback_processor.h"
#include "dsp/systems/modulation_matrix.h"       // For optional modulation

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// ShimmerFeedbackProcessor - IFeedbackProcessor for shimmer effect
// =============================================================================

/// @brief Feedback path processor that applies pitch shifting and diffusion
///
/// Implements IFeedbackProcessor to be injected into FlexibleFeedbackNetwork.
/// The processor applies:
/// 1. Pitch shifting (stereo)
/// 2. Diffusion network (reverb-like smearing)
/// 3. Shimmer mix blending (pitched vs unpitched ratio)
class ShimmerFeedbackProcessor : public IFeedbackProcessor {
public:
    ShimmerFeedbackProcessor() noexcept = default;
    ~ShimmerFeedbackProcessor() override = default;

    // IFeedbackProcessor interface
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept override {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Prepare pitch shifters
        pitchShifterL_.prepare(sampleRate, maxBlockSize);
        pitchShifterR_.prepare(sampleRate, maxBlockSize);

        // Prepare diffusion network
        diffusion_.prepare(static_cast<float>(sampleRate), maxBlockSize);

        // Allocate buffers
        unpitchedL_.resize(maxBlockSize, 0.0f);
        unpitchedR_.resize(maxBlockSize, 0.0f);
        diffusionOutL_.resize(maxBlockSize, 0.0f);
        diffusionOutR_.resize(maxBlockSize, 0.0f);
    }

    void process(float* left, float* right, std::size_t numSamples) noexcept override {
        if (numSamples == 0) return;

        // Store unpitched signal for shimmer mix blending
        for (std::size_t i = 0; i < numSamples; ++i) {
            unpitchedL_[i] = left[i];
            unpitchedR_[i] = right[i];
        }

        // Apply pitch shifting
        pitchShifterL_.process(left, left, numSamples);
        pitchShifterR_.process(right, right, numSamples);

        // Apply diffusion to pitched signal if enabled
        if (diffusionAmount_ > 0.001f) {
            diffusion_.process(left, right, diffusionOutL_.data(), diffusionOutR_.data(), numSamples);

            // Blend pitched with diffused based on diffusion amount
            for (std::size_t i = 0; i < numSamples; ++i) {
                left[i] = left[i] * (1.0f - diffusionAmount_) + diffusionOutL_[i] * diffusionAmount_;
                right[i] = right[i] * (1.0f - diffusionAmount_) + diffusionOutR_[i] * diffusionAmount_;
            }
        }

        // Apply shimmer mix: blend between unpitched and pitched+diffused
        // At 0% shimmer: output = unpitched (standard delay feedback)
        // At 100% shimmer: output = pitched+diffused (full shimmer)
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i] = unpitchedL_[i] * (1.0f - shimmerMix_) + left[i] * shimmerMix_;
            right[i] = unpitchedR_[i] * (1.0f - shimmerMix_) + right[i] * shimmerMix_;
        }
    }

    void reset() noexcept override {
        pitchShifterL_.reset();
        pitchShifterR_.reset();
        diffusion_.reset();
    }

    [[nodiscard]] std::size_t getLatencySamples() const noexcept override {
        return pitchShifterL_.getLatencySamples();
    }

    // Configuration methods (called from ShimmerDelay)
    void setPitchSemitones(float semitones) noexcept {
        pitchShifterL_.setSemitones(semitones);
        pitchShifterR_.setSemitones(semitones);
    }

    void setPitchCents(float cents) noexcept {
        pitchShifterL_.setCents(cents);
        pitchShifterR_.setCents(cents);
    }

    void setPitchMode(PitchMode mode) noexcept {
        pitchShifterL_.setMode(mode);
        pitchShifterR_.setMode(mode);
    }

    void setShimmerMix(float mix) noexcept {
        shimmerMix_ = std::clamp(mix, 0.0f, 1.0f);
    }

    void setDiffusionAmount(float amount) noexcept {
        diffusionAmount_ = std::clamp(amount, 0.0f, 1.0f);
        diffusion_.setDensity(amount * 100.0f);
    }

    void setDiffusionSize(float size) noexcept {
        diffusion_.setSize(size);
    }

private:
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;

    // Pitch shifters (stereo)
    PitchShiftProcessor pitchShifterL_;
    PitchShiftProcessor pitchShifterR_;

    // Diffusion network
    DiffusionNetwork diffusion_;

    // Parameters
    float shimmerMix_ = 1.0f;       // 0-1 (0 = unpitched, 1 = fully pitched)
    float diffusionAmount_ = 0.5f;  // 0-1

    // Scratch buffers
    std::vector<float> unpitchedL_;
    std::vector<float> unpitchedR_;
    std::vector<float> diffusionOutL_;
    std::vector<float> diffusionOutR_;
};

// =============================================================================
// ShimmerDelay Class
// =============================================================================

/// @brief Layer 4 User Feature - Shimmer Delay
///
/// Creates pitch-shifted feedback delay for ethereal, cascading harmonic textures.
/// The signature "shimmer" sound comes from pitch shifting in the feedback path -
/// each delay repeat is shifted further, creating infinite harmonic cascades.
///
/// @par Signal Flow
/// ```
/// Input ──┬──────────────────────────────────────────┬──> Mix ──> Output
///         │                                          │
///         v                                          │
///    ┌─────────┐                                     │
///    │  Delay  │<────────────────────────────────────┤
///    │  Line   │                                     │
///    └────┬────┘                                     │
///         │                                          │
///         v (feedback path)                          │
///    ┌─────────┐  ┌───────────┐  ┌────────┐  ┌─────┐│
///    │  Pitch  │─>│ Diffusion │─>│ Filter │─>│Limit├┘
///    │ Shifter │  │  Network  │  │        │  │     │
///    └─────────┘  └───────────┘  └────────┘  └─────┘
///         ^                                     │
///         └─────────── shimmerMix blend ────────┘
/// ```
///
/// @par User Controls
/// - Delay Time: 10-5000ms with tempo sync option (FR-001 to FR-006)
/// - Pitch: ±24 semitones + ±100 cents fine tuning (FR-007 to FR-010)
/// - Shimmer Mix: 0-100% blend of pitched/unpitched feedback (FR-011 to FR-012)
/// - Feedback: 0-120% with limiting for stability (FR-013 to FR-015)
/// - Diffusion: 0-100% amount and size (FR-016 to FR-019)
/// - Filter: Optional lowpass in feedback path (FR-020 to FR-021)
/// - Dry/Wet Mix: 0-100% blend (FR-022 to FR-024)
/// - Output Level: -12 to +12dB (FR-025)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// ShimmerDelay shimmer;
/// shimmer.prepare(44100.0, 512, 5000.0f);
/// shimmer.setPitchSemitones(12.0f);   // Octave up
/// shimmer.setShimmerMix(100.0f);      // Full shimmer
/// shimmer.setFeedbackAmount(0.6f);    // 60% feedback
/// shimmer.setDiffusionAmount(70.0f);  // Lush diffusion
/// shimmer.setDryWetMix(50.0f);        // 50/50 mix
/// shimmer.snapParameters();
///
/// // In process callback
/// shimmer.process(left, right, numSamples, ctx);
/// @endcode
class ShimmerDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Delay time limits (FR-001)
    static constexpr float kMinDelayMs = 10.0f;
    static constexpr float kMaxDelayMs = 5000.0f;
    static constexpr float kDefaultDelayMs = 500.0f;

    // Pitch limits (FR-007, FR-008)
    static constexpr float kMinPitchSemitones = -24.0f;
    static constexpr float kMaxPitchSemitones = 24.0f;
    static constexpr float kDefaultPitchSemitones = 12.0f;  // Octave up

    static constexpr float kMinPitchCents = -100.0f;
    static constexpr float kMaxPitchCents = 100.0f;
    static constexpr float kDefaultPitchCents = 0.0f;

    // Shimmer mix (FR-011)
    static constexpr float kMinShimmerMix = 0.0f;
    static constexpr float kMaxShimmerMix = 100.0f;
    static constexpr float kDefaultShimmerMix = 100.0f;  // Full shimmer

    // Feedback (FR-013)
    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  // 120% for self-oscillation
    static constexpr float kDefaultFeedback = 0.5f;

    // Diffusion (FR-016, FR-017)
    static constexpr float kMinDiffusion = 0.0f;
    static constexpr float kMaxDiffusion = 100.0f;
    static constexpr float kDefaultDiffusionAmount = 50.0f;
    static constexpr float kDefaultDiffusionSize = 50.0f;

    // Filter (FR-020, FR-021)
    static constexpr float kMinFilterCutoff = 20.0f;
    static constexpr float kMaxFilterCutoff = 20000.0f;
    static constexpr float kDefaultFilterCutoff = 4000.0f;

    // Output (FR-022)
    static constexpr float kMinDryWetMix = 0.0f;
    static constexpr float kMaxDryWetMix = 100.0f;
    static constexpr float kDefaultDryWetMix = 50.0f;

    // Internal
    static constexpr float kSmoothingTimeMs = 20.0f;
    static constexpr size_t kMaxDryBufferSize = 65536;  // Supports ~1.5s blocks at 44.1kHz

    // Limiter constants (for feedback > 100%)
    static constexpr float kLimiterThresholdDb = -0.5f;
    static constexpr float kLimiterRatio = 100.0f;
    static constexpr float kLimiterKneeDb = 6.0f;

    // Modulation destination IDs (FR-023)
    // Use these with ModulationMatrix::registerDestination()
    static constexpr uint8_t kModDestDelayTime = 0;
    static constexpr uint8_t kModDestPitch = 1;
    static constexpr uint8_t kModDestShimmerMix = 2;
    static constexpr uint8_t kModDestFeedback = 3;
    static constexpr uint8_t kModDestDiffusion = 4;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    ShimmerDelay() noexcept = default;
    ~ShimmerDelay() = default;

    // Non-copyable, movable
    ShimmerDelay(const ShimmerDelay&) = delete;
    ShimmerDelay& operator=(const ShimmerDelay&) = delete;
    ShimmerDelay(ShimmerDelay&&) noexcept = default;
    ShimmerDelay& operator=(ShimmerDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
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

    /// @brief Snap all smoothers to current targets (for initialization)
    /// @note Call after setting multiple parameters for tests or preset loads
    void snapParameters() noexcept;

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Delay Configuration (FR-001 to FR-006)
    // =========================================================================

    /// @brief Set delay time in milliseconds
    /// @param ms Delay time in milliseconds [10, 5000]
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Get current delay time
    [[nodiscard]] float getDelayTimeMs() const noexcept { return delayTimeMs_; }

    /// @brief Set time mode (free or synced)
    /// @param mode TimeMode::Free or TimeMode::Synced
    void setTimeMode(TimeMode mode) noexcept { timeMode_ = mode; }

    /// @brief Get current time mode
    [[nodiscard]] TimeMode getTimeMode() const noexcept { return timeMode_; }

    /// @brief Set note value for tempo sync (FR-004)
    /// @param note Note value (quarter, eighth, etc.)
    /// @param modifier Note modifier (none, dotted, triplet)
    void setNoteValue(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Get current note value
    [[nodiscard]] NoteValue getNoteValue() const noexcept { return noteValue_; }

    // =========================================================================
    // Pitch Configuration (FR-007 to FR-010)
    // =========================================================================

    /// @brief Set pitch shift in semitones
    /// @param semitones Pitch shift [-24, +24]
    void setPitchSemitones(float semitones) noexcept;

    /// @brief Get pitch shift in semitones
    [[nodiscard]] float getPitchSemitones() const noexcept { return pitchSemitones_; }

    /// @brief Set fine pitch adjustment in cents
    /// @param cents Fine adjustment [-100, +100]
    void setPitchCents(float cents) noexcept;

    /// @brief Get fine pitch adjustment
    [[nodiscard]] float getPitchCents() const noexcept { return pitchCents_; }

    /// @brief Set pitch quality mode
    /// @param mode PitchMode::Simple, Granular, or PhaseVocoder
    void setPitchMode(PitchMode mode) noexcept;

    /// @brief Get current pitch mode
    [[nodiscard]] PitchMode getPitchMode() const noexcept { return pitchMode_; }

    /// @brief Get target pitch ratio (from semitones + cents)
    [[nodiscard]] float getPitchRatio() const noexcept;

    /// @brief Get current smoothed pitch ratio (FR-009)
    /// @note Returns the actual interpolated value being applied, not the target
    [[nodiscard]] float getSmoothedPitchRatio() const noexcept;

    // =========================================================================
    // Shimmer Configuration (FR-011 to FR-015)
    // =========================================================================

    /// @brief Set shimmer mix (% of feedback that is pitch-shifted)
    /// @param percent Shimmer mix [0, 100] (0 = standard delay, 100 = full shimmer)
    void setShimmerMix(float percent) noexcept;

    /// @brief Get shimmer mix
    [[nodiscard]] float getShimmerMix() const noexcept { return shimmerMix_; }

    /// @brief Set feedback amount
    /// @param amount Feedback [0, 1.2] (>1.0 enables self-oscillation with limiting)
    void setFeedbackAmount(float amount) noexcept;

    /// @brief Get feedback amount
    [[nodiscard]] float getFeedbackAmount() const noexcept { return feedbackAmount_; }

    // =========================================================================
    // Diffusion Configuration (FR-016 to FR-019)
    // =========================================================================

    /// @brief Set diffusion amount
    /// @param percent Diffusion [0, 100] (0 = no smearing, 100 = maximum diffusion)
    void setDiffusionAmount(float percent) noexcept;

    /// @brief Get diffusion amount
    [[nodiscard]] float getDiffusionAmount() const noexcept { return diffusionAmount_; }

    /// @brief Set diffusion size (time scaling)
    /// @param percent Size [0, 100]
    void setDiffusionSize(float percent) noexcept;

    /// @brief Get diffusion size
    [[nodiscard]] float getDiffusionSize() const noexcept { return diffusionSize_; }

    // =========================================================================
    // Filter Configuration (FR-020, FR-021)
    // =========================================================================

    /// @brief Enable/disable feedback filter
    /// @param enabled true to enable lowpass filter in feedback path
    void setFilterEnabled(bool enabled) noexcept {
        filterEnabled_ = enabled;
        feedbackNetwork_.setFilterEnabled(enabled);
    }

    /// @brief Get filter enabled state
    [[nodiscard]] bool isFilterEnabled() const noexcept { return filterEnabled_; }

    /// @brief Set filter cutoff frequency
    /// @param hz Cutoff in Hz [20, 20000]
    void setFilterCutoff(float hz) noexcept;

    /// @brief Get filter cutoff
    [[nodiscard]] float getFilterCutoff() const noexcept { return filterCutoffHz_; }

    // =========================================================================
    // Output Configuration (FR-022 to FR-025)
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param percent Mix [0, 100] (0 = dry, 100 = wet)
    void setDryWetMix(float percent) noexcept;

    /// @brief Get dry/wet mix
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    // =========================================================================
    // Modulation (FR-026 to FR-028)
    // =========================================================================

    /// @brief Connect external modulation matrix
    /// @param matrix Pointer to ModulationMatrix (nullptr to disconnect)
    void connectModulationMatrix(ModulationMatrix* matrix) noexcept {
        modulationMatrix_ = matrix;
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get current effective delay time (after tempo sync calculation)
    [[nodiscard]] float getCurrentDelayMs() const noexcept;

    /// @brief Get processing latency in samples (from pitch shifter)
    [[nodiscard]] size_t getLatencySamples() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @param ctx Block context with tempo/transport info
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate tempo-synced delay time
    [[nodiscard]] float calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept;

    /// @brief Convert milliseconds to samples
    [[nodiscard]] float msToSamples(float ms) const noexcept;

    /// @brief Calculate pitch ratio from semitones and cents (FR-009)
    [[nodiscard]] float calculatePitchRatio() const noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    float maxDelayMs_ = kMaxDelayMs;
    bool prepared_ = false;

    // Layer 3 - Flexible feedback network (FR-018)
    FlexibleFeedbackNetwork feedbackNetwork_;

    // Shimmer processor (injected into feedback network)
    ShimmerFeedbackProcessor shimmerProcessor_;

    // Layer 1 primitives - parameter smoothers
    OnePoleSmoother delaySmoother_;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother pitchRatioSmoother_;  // FR-009: Smooth pitch changes

    // Layer 3 - optional modulation matrix
    ModulationMatrix* modulationMatrix_ = nullptr;

    // Parameters - delay
    float delayTimeMs_ = kDefaultDelayMs;
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;

    // Parameters - pitch
    float pitchSemitones_ = kDefaultPitchSemitones;
    float pitchCents_ = kDefaultPitchCents;
    PitchMode pitchMode_ = PitchMode::Granular;  // Default per FR-008

    // Parameters - shimmer
    float shimmerMix_ = kDefaultShimmerMix;
    float feedbackAmount_ = kDefaultFeedback;

    // Parameters - diffusion
    float diffusionAmount_ = kDefaultDiffusionAmount;
    float diffusionSize_ = kDefaultDiffusionSize;

    // Parameters - filter
    bool filterEnabled_ = false;
    float filterCutoffHz_ = kDefaultFilterCutoff;

    // Parameters - output
    float dryWetMix_ = kDefaultDryWetMix;

    // Scratch buffers for dry signal storage
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline void ShimmerDelay::prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    maxDelayMs_ = std::min(maxDelayMs, kMaxDelayMs);

    // Prepare the shimmer processor (will also be prepared by feedbackNetwork_)
    shimmerProcessor_.setPitchMode(pitchMode_);

    // Prepare flexible feedback network (FR-018)
    feedbackNetwork_.prepare(sampleRate, maxBlockSize);
    feedbackNetwork_.setProcessor(&shimmerProcessor_);
    feedbackNetwork_.setProcessorMix(100.0f);  // Full processor effect
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    feedbackNetwork_.setFilterEnabled(filterEnabled_);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    feedbackNetwork_.setFilterType(FilterType::Lowpass);

    // Allocate scratch buffers for dry signal storage
    const size_t bufferSize = std::max(maxBlockSize, kMaxDryBufferSize);
    dryBufferL_.resize(bufferSize);
    dryBufferR_.resize(bufferSize);

    // Configure smoothers (Layer 1)
    const float sr = static_cast<float>(sampleRate);
    delaySmoother_.configure(kSmoothingTimeMs, sr);
    dryWetSmoother_.configure(kSmoothingTimeMs, sr);
    pitchRatioSmoother_.configure(kSmoothingTimeMs, sr);  // FR-009

    // Initialize smoothers to defaults
    delaySmoother_.snapTo(delayTimeMs_);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    pitchRatioSmoother_.snapTo(calculatePitchRatio());  // FR-009

    // Initialize shimmer processor parameters
    shimmerProcessor_.setShimmerMix(shimmerMix_ / 100.0f);
    shimmerProcessor_.setDiffusionAmount(diffusionAmount_ / 100.0f);
    shimmerProcessor_.setDiffusionSize(diffusionSize_);

    // Snap feedback network parameters
    feedbackNetwork_.snapParameters();

    prepared_ = true;
}

inline void ShimmerDelay::reset() noexcept {
    // Reset feedback network (includes delay lines, filter, limiter)
    feedbackNetwork_.reset();

    // Reset shimmer processor (includes pitch shifters, diffusion)
    shimmerProcessor_.reset();

    // Snap local smoothers to current targets
    delaySmoother_.snapTo(delayTimeMs_);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    pitchRatioSmoother_.snapTo(calculatePitchRatio());  // FR-009

    // Snap feedback network parameters
    feedbackNetwork_.snapParameters();
}

inline void ShimmerDelay::snapParameters() noexcept {
    // Snap local smoothers
    delaySmoother_.snapTo(delayTimeMs_);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    pitchRatioSmoother_.snapTo(calculatePitchRatio());  // FR-009

    // Apply snapped pitch to shimmer processor immediately
    shimmerProcessor_.setPitchSemitones(pitchSemitones_);
    shimmerProcessor_.setPitchCents(pitchCents_);

    // Update feedback network parameters
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    feedbackNetwork_.setFilterEnabled(filterEnabled_);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    feedbackNetwork_.snapParameters();

    // Update shimmer processor parameters
    shimmerProcessor_.setShimmerMix(shimmerMix_ / 100.0f);
    shimmerProcessor_.setDiffusionAmount(diffusionAmount_ / 100.0f);
    shimmerProcessor_.setDiffusionSize(diffusionSize_);
}

inline void ShimmerDelay::setDelayTimeMs(float ms) noexcept {
    delayTimeMs_ = std::clamp(ms, kMinDelayMs, maxDelayMs_);
    delaySmoother_.setTarget(delayTimeMs_);
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
}

inline void ShimmerDelay::setNoteValue(NoteValue note, NoteModifier modifier) noexcept {
    noteValue_ = note;
    noteModifier_ = modifier;
}

inline void ShimmerDelay::setPitchSemitones(float semitones) noexcept {
    pitchSemitones_ = std::clamp(semitones, kMinPitchSemitones, kMaxPitchSemitones);
    // FR-009: Set smoother target instead of applying directly
    pitchRatioSmoother_.setTarget(calculatePitchRatio());
}

inline void ShimmerDelay::setPitchCents(float cents) noexcept {
    pitchCents_ = std::clamp(cents, kMinPitchCents, kMaxPitchCents);
    // FR-009: Set smoother target instead of applying directly
    pitchRatioSmoother_.setTarget(calculatePitchRatio());
}

inline void ShimmerDelay::setPitchMode(PitchMode mode) noexcept {
    pitchMode_ = mode;
    shimmerProcessor_.setPitchMode(mode);
}

inline float ShimmerDelay::getPitchRatio() const noexcept {
    // Return the TARGET ratio (what user set), not the smoothed value
    return calculatePitchRatio();
}

inline float ShimmerDelay::getSmoothedPitchRatio() const noexcept {
    // Return the current interpolated value being applied (FR-009)
    return pitchRatioSmoother_.getCurrentValue();
}

inline void ShimmerDelay::setShimmerMix(float percent) noexcept {
    shimmerMix_ = std::clamp(percent, kMinShimmerMix, kMaxShimmerMix);
    shimmerProcessor_.setShimmerMix(shimmerMix_ / 100.0f);
}

inline void ShimmerDelay::setFeedbackAmount(float amount) noexcept {
    feedbackAmount_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
}

inline void ShimmerDelay::setDiffusionAmount(float percent) noexcept {
    diffusionAmount_ = std::clamp(percent, kMinDiffusion, kMaxDiffusion);
    shimmerProcessor_.setDiffusionAmount(diffusionAmount_ / 100.0f);
}

inline void ShimmerDelay::setDiffusionSize(float percent) noexcept {
    diffusionSize_ = std::clamp(percent, kMinDiffusion, kMaxDiffusion);
    shimmerProcessor_.setDiffusionSize(diffusionSize_);
}

inline void ShimmerDelay::setFilterCutoff(float hz) noexcept {
    filterCutoffHz_ = std::clamp(hz, kMinFilterCutoff, kMaxFilterCutoff);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
}

inline void ShimmerDelay::setDryWetMix(float percent) noexcept {
    dryWetMix_ = std::clamp(percent, kMinDryWetMix, kMaxDryWetMix);
    dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
}

inline float ShimmerDelay::getCurrentDelayMs() const noexcept {
    return delaySmoother_.getCurrentValue();
}

inline size_t ShimmerDelay::getLatencySamples() const noexcept {
    // Latency comes from feedback network (includes pitch shifter latency)
    return feedbackNetwork_.getLatencySamples();
}

inline float ShimmerDelay::calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept {
    const size_t delaySamples = ctx.tempoToSamples(noteValue_, noteModifier_);
    float delayMs = static_cast<float>(delaySamples * 1000.0 / ctx.sampleRate);
    return std::clamp(delayMs, kMinDelayMs, maxDelayMs_);
}

inline float ShimmerDelay::msToSamples(float ms) const noexcept {
    return static_cast<float>(ms * sampleRate_ / 1000.0);
}

inline float ShimmerDelay::calculatePitchRatio() const noexcept {
    // Convert semitones + cents to ratio: ratio = 2^((semitones + cents/100) / 12)
    const float totalSemitones = pitchSemitones_ + pitchCents_ / 100.0f;
    return std::pow(2.0f, totalSemitones / 12.0f);
}

inline void ShimmerDelay::process(float* left, float* right, size_t numSamples,
                                   const BlockContext& ctx) noexcept {
    if (!prepared_ || numSamples == 0) return;

    // Calculate base delay time (tempo sync or free)
    float baseDelayMs = delayTimeMs_;
    if (timeMode_ == TimeMode::Synced) {
        baseDelayMs = calculateTempoSyncedDelay(ctx);
        feedbackNetwork_.setDelayTimeMs(baseDelayMs);
    }
    delaySmoother_.setTarget(baseDelayMs);

    // Process in chunks of maxBlockSize_ to handle large buffers
    size_t samplesProcessed = 0;
    while (samplesProcessed < numSamples) {
        const size_t chunkSize = std::min(maxBlockSize_, numSamples - samplesProcessed);
        float* chunkLeft = left + samplesProcessed;
        float* chunkRight = right + samplesProcessed;

        // Store dry signal for mixing
        for (size_t i = 0; i < chunkSize; ++i) {
            dryBufferL_[i] = chunkLeft[i];
            dryBufferR_[i] = chunkRight[i];
        }

        // FR-009: Apply smoothed pitch ratio
        float smoothedRatio = pitchRatioSmoother_.getCurrentValue();
        for (size_t i = 0; i < chunkSize; ++i) {
            smoothedRatio = pitchRatioSmoother_.process();
        }
        const float smoothedSemitones = 12.0f * std::log2(smoothedRatio);
        shimmerProcessor_.setPitchSemitones(smoothedSemitones);
        shimmerProcessor_.setPitchCents(0.0f);

        // Process through feedback network
        feedbackNetwork_.process(chunkLeft, chunkRight, chunkSize, ctx);

        // Mix dry/wet for output with smoothed parameters
        for (size_t i = 0; i < chunkSize; ++i) {
            const float currentDryWet = dryWetSmoother_.process();

            chunkLeft[i] = dryBufferL_[i] * (1.0f - currentDryWet) + chunkLeft[i] * currentDryWet;
            chunkRight[i] = dryBufferR_[i] * (1.0f - currentDryWet) + chunkRight[i] * currentDryWet;
        }

        samplesProcessed += chunkSize;
    }
}

} // namespace DSP
} // namespace Iterum
