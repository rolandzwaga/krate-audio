// ==============================================================================
// Layer 4: User Feature - ShimmerDelay
// ==============================================================================
// Pitch-shifted feedback delay creating ethereal, cascading harmonic textures.
// Classic shimmer effect (Strymon BigSky, Eventide Space, Valhalla Shimmer).
//
// Composes:
// - DelayLine (Layer 1): 2 instances for stereo delay buffers
// - LFO (Layer 1): Optional modulation source
// - OnePoleSmoother (Layer 1): 8+ instances for parameter smoothing
// - PitchShiftProcessor (Layer 2): 2 instances for stereo pitch shifting
// - DiffusionNetwork (Layer 2): Smearing for reverb-like texture
// - MultimodeFilter (Layer 2): 2 instances for feedback filtering
// - DynamicsProcessor (Layer 2): Feedback limiting for >100%
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
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/diffusion_network.h"
#include "dsp/processors/dynamics_processor.h"
#include "dsp/processors/multimode_filter.h"
#include "dsp/processors/pitch_shift_processor.h"
#include "dsp/systems/delay_engine.h"       // For TimeMode enum
#include "dsp/systems/modulation_matrix.h"  // For optional modulation

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

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

    // Output (FR-022, FR-025)
    static constexpr float kMinDryWetMix = 0.0f;
    static constexpr float kMaxDryWetMix = 100.0f;
    static constexpr float kDefaultDryWetMix = 50.0f;

    static constexpr float kMinOutputGainDb = -12.0f;
    static constexpr float kMaxOutputGainDb = 12.0f;
    static constexpr float kDefaultOutputGainDb = 0.0f;

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
    void setFilterEnabled(bool enabled) noexcept { filterEnabled_ = enabled; }

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

    /// @brief Set output level
    /// @param dB Output gain [-12, +12]
    void setOutputGainDb(float dB) noexcept;

    /// @brief Get output level
    [[nodiscard]] float getOutputGainDb() const noexcept { return outputGainDb_; }

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

    // Layer 1 primitives - delay lines
    DelayLine delayLineL_;
    DelayLine delayLineR_;

    // Layer 2 processors - pitch shifting (mono, need 2 for stereo)
    PitchShiftProcessor pitchShifterL_;
    PitchShiftProcessor pitchShifterR_;

    // Layer 2 processors - diffusion
    DiffusionNetwork diffusion_;

    // Layer 2 processors - feedback filter
    MultimodeFilter filterL_;
    MultimodeFilter filterR_;

    // Layer 2 processors - feedback limiting
    DynamicsProcessor limiter_;

    // Layer 1 primitives - parameter smoothers
    OnePoleSmoother delaySmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother shimmerMixSmoother_;
    OnePoleSmoother diffusionSmoother_;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;
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
    float outputGainDb_ = kDefaultOutputGainDb;

    // Scratch buffers for processing (heap allocated in prepare())
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
    std::vector<float> pitchBufferL_;
    std::vector<float> pitchBufferR_;
    std::vector<float> diffusionBufferL_;
    std::vector<float> diffusionBufferR_;

    // Previous block's processed feedback (for block-based pitch shifter in feedback loop)
    std::vector<float> prevFeedbackL_;
    std::vector<float> prevFeedbackR_;
    size_t prevFeedbackSize_ = 0;
    size_t prevFeedbackReadPos_ = 0;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline void ShimmerDelay::prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    maxDelayMs_ = std::min(maxDelayMs, kMaxDelayMs);

    // Convert to seconds for DelayLine
    const float maxDelaySeconds = maxDelayMs_ / 1000.0f;

    // Prepare delay lines (Layer 1)
    delayLineL_.prepare(sampleRate, maxDelaySeconds);
    delayLineR_.prepare(sampleRate, maxDelaySeconds);

    // Prepare pitch shifters (Layer 2) - mono processors
    pitchShifterL_.prepare(sampleRate, maxBlockSize);
    pitchShifterR_.prepare(sampleRate, maxBlockSize);
    pitchShifterL_.setMode(pitchMode_);
    pitchShifterR_.setMode(pitchMode_);

    // Prepare diffusion network (Layer 2) - takes float sampleRate
    diffusion_.prepare(static_cast<float>(sampleRate), maxBlockSize);

    // Prepare feedback filters (Layer 2)
    filterL_.prepare(sampleRate, maxBlockSize);
    filterR_.prepare(sampleRate, maxBlockSize);
    filterL_.setType(FilterType::Lowpass);
    filterR_.setType(FilterType::Lowpass);
    filterL_.setCutoff(filterCutoffHz_);
    filterR_.setCutoff(filterCutoffHz_);

    // Prepare limiter (Layer 2) - for feedback > 100%
    limiter_.prepare(sampleRate, maxBlockSize);
    limiter_.setThreshold(kLimiterThresholdDb);
    limiter_.setRatio(kLimiterRatio);
    limiter_.setKneeWidth(kLimiterKneeDb);
    limiter_.setDetectionMode(DynamicsDetectionMode::Peak);

    // Allocate scratch buffers (heap, not stack, for large blocks)
    const size_t bufferSize = std::max(maxBlockSize, kMaxDryBufferSize);
    dryBufferL_.resize(bufferSize);
    dryBufferR_.resize(bufferSize);
    pitchBufferL_.resize(bufferSize);
    pitchBufferR_.resize(bufferSize);
    diffusionBufferL_.resize(bufferSize);
    diffusionBufferR_.resize(bufferSize);
    prevFeedbackL_.resize(bufferSize);
    prevFeedbackR_.resize(bufferSize);

    // Configure smoothers (Layer 1)
    const float sr = static_cast<float>(sampleRate);
    delaySmoother_.configure(kSmoothingTimeMs, sr);
    feedbackSmoother_.configure(kSmoothingTimeMs, sr);
    shimmerMixSmoother_.configure(kSmoothingTimeMs, sr);
    diffusionSmoother_.configure(kSmoothingTimeMs, sr);
    dryWetSmoother_.configure(kSmoothingTimeMs, sr);
    outputGainSmoother_.configure(kSmoothingTimeMs, sr);
    pitchRatioSmoother_.configure(kSmoothingTimeMs, sr);  // FR-009

    // Initialize to defaults
    delaySmoother_.snapTo(delayTimeMs_);
    feedbackSmoother_.snapTo(feedbackAmount_);
    shimmerMixSmoother_.snapTo(shimmerMix_ / 100.0f);
    diffusionSmoother_.snapTo(diffusionAmount_ / 100.0f);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
    pitchRatioSmoother_.snapTo(calculatePitchRatio());  // FR-009

    prepared_ = true;
}

inline void ShimmerDelay::reset() noexcept {
    delayLineL_.reset();
    delayLineR_.reset();
    pitchShifterL_.reset();
    pitchShifterR_.reset();
    diffusion_.reset();
    filterL_.reset();
    filterR_.reset();
    limiter_.reset();

    // Clear previous feedback state
    prevFeedbackSize_ = 0;
    prevFeedbackReadPos_ = 0;
    if (!prevFeedbackL_.empty()) {
        std::fill(prevFeedbackL_.begin(), prevFeedbackL_.end(), 0.0f);
        std::fill(prevFeedbackR_.begin(), prevFeedbackR_.end(), 0.0f);
    }

    // Snap smoothers to current targets
    delaySmoother_.snapTo(delayTimeMs_);
    feedbackSmoother_.snapTo(feedbackAmount_);
    shimmerMixSmoother_.snapTo(shimmerMix_ / 100.0f);
    diffusionSmoother_.snapTo(diffusionAmount_ / 100.0f);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
    pitchRatioSmoother_.snapTo(calculatePitchRatio());  // FR-009
}

inline void ShimmerDelay::snapParameters() noexcept {
    delaySmoother_.snapTo(delayTimeMs_);
    feedbackSmoother_.snapTo(feedbackAmount_);
    shimmerMixSmoother_.snapTo(shimmerMix_ / 100.0f);
    diffusionSmoother_.snapTo(diffusionAmount_ / 100.0f);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
    pitchRatioSmoother_.snapTo(calculatePitchRatio());  // FR-009

    // Apply snapped pitch to pitch shifters immediately
    pitchShifterL_.setSemitones(pitchSemitones_);
    pitchShifterL_.setCents(pitchCents_);
    pitchShifterR_.setSemitones(pitchSemitones_);
    pitchShifterR_.setCents(pitchCents_);
}

inline void ShimmerDelay::setDelayTimeMs(float ms) noexcept {
    delayTimeMs_ = std::clamp(ms, kMinDelayMs, maxDelayMs_);
    delaySmoother_.setTarget(delayTimeMs_);
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
    pitchShifterL_.setMode(mode);
    pitchShifterR_.setMode(mode);
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
    shimmerMixSmoother_.setTarget(shimmerMix_ / 100.0f);
}

inline void ShimmerDelay::setFeedbackAmount(float amount) noexcept {
    feedbackAmount_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
    feedbackSmoother_.setTarget(feedbackAmount_);
}

inline void ShimmerDelay::setDiffusionAmount(float percent) noexcept {
    diffusionAmount_ = std::clamp(percent, kMinDiffusion, kMaxDiffusion);
    diffusionSmoother_.setTarget(diffusionAmount_ / 100.0f);
    diffusion_.setDensity(diffusionAmount_);
}

inline void ShimmerDelay::setDiffusionSize(float percent) noexcept {
    diffusionSize_ = std::clamp(percent, kMinDiffusion, kMaxDiffusion);
    diffusion_.setSize(diffusionSize_);
}

inline void ShimmerDelay::setFilterCutoff(float hz) noexcept {
    filterCutoffHz_ = std::clamp(hz, kMinFilterCutoff, kMaxFilterCutoff);
    filterL_.setCutoff(filterCutoffHz_);
    filterR_.setCutoff(filterCutoffHz_);
}

inline void ShimmerDelay::setDryWetMix(float percent) noexcept {
    dryWetMix_ = std::clamp(percent, kMinDryWetMix, kMaxDryWetMix);
    dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
}

inline void ShimmerDelay::setOutputGainDb(float dB) noexcept {
    outputGainDb_ = std::clamp(dB, kMinOutputGainDb, kMaxOutputGainDb);
    outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
}

inline float ShimmerDelay::getCurrentDelayMs() const noexcept {
    return delaySmoother_.getCurrentValue();
}

inline size_t ShimmerDelay::getLatencySamples() const noexcept {
    // Latency comes from pitch shifter (Granular ~46ms, PhaseVocoder ~116ms)
    return pitchShifterL_.getLatencySamples();
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

    // Limit to buffer size (vectors are sized in prepare())
    numSamples = std::min(numSamples, dryBufferL_.size());

    // Store dry signal for mixing
    for (size_t i = 0; i < numSamples; ++i) {
        dryBufferL_[i] = left[i];
        dryBufferR_[i] = right[i];
    }

    // Calculate base delay time (tempo sync or free)
    float baseDelayMs = delayTimeMs_;
    if (timeMode_ == TimeMode::Synced) {
        baseDelayMs = calculateTempoSyncedDelay(ctx);
    }
    delaySmoother_.setTarget(baseDelayMs);

    // FR-009: Apply smoothed pitch ratio at start of block
    // Advance smoother for the full block to get end-of-block value
    float smoothedRatio = pitchRatioSmoother_.getCurrentValue();
    for (size_t i = 0; i < numSamples; ++i) {
        smoothedRatio = pitchRatioSmoother_.process();
    }
    const float smoothedSemitones = 12.0f * std::log2(smoothedRatio);
    pitchShifterL_.setSemitones(smoothedSemitones);
    pitchShifterL_.setCents(0.0f);  // Cents included in smoothed ratio
    pitchShifterR_.setSemitones(smoothedSemitones);
    pitchShifterR_.setCents(0.0f);

    // Sample-by-sample processing for feedback loop
    // The pitch shifter is block-based, so we use the PREVIOUS block's
    // pitch-shifted output as the feedback source (one-block latency in feedback path)
    for (size_t i = 0; i < numSamples; ++i) {
        // Get smoothed parameters
        const float currentDelayMs = delaySmoother_.process();
        const float currentFeedback = feedbackSmoother_.process();
        const float currentShimmerMix = shimmerMixSmoother_.process();
        const float currentDryWet = dryWetSmoother_.process();
        const float currentOutputGain = outputGainSmoother_.process();
        (void)diffusionSmoother_.process();  // Keep smoother advancing

        // Convert delay to samples (no -1 offset; write-after-read pattern)
        const float delaySamples = msToSamples(currentDelayMs);

        // Read delayed samples (read-before-write) - this is the wet signal
        float delayedL = delayLineL_.readLinear(delaySamples);
        float delayedR = delayLineR_.readLinear(delaySamples);

        // Store delay output for pitch processing after this loop
        pitchBufferL_[i] = delayedL;
        pitchBufferR_[i] = delayedR;

        // Get previous block's processed feedback (if available)
        float prevPitchedL = 0.0f;
        float prevPitchedR = 0.0f;
        if (prevFeedbackReadPos_ < prevFeedbackSize_) {
            prevPitchedL = prevFeedbackL_[prevFeedbackReadPos_];
            prevPitchedR = prevFeedbackR_[prevFeedbackReadPos_];
            ++prevFeedbackReadPos_;
        }

        // Shimmer mix: blend between unpitched (delayed) and pitched (previous block)
        // At 0% shimmer: standard delay (no pitch shift)
        // At 100% shimmer: fully pitch-shifted feedback
        float feedbackL = delayedL * (1.0f - currentShimmerMix) + prevPitchedL * currentShimmerMix;
        float feedbackR = delayedR * (1.0f - currentShimmerMix) + prevPitchedR * currentShimmerMix;

        // Apply filter if enabled
        if (filterEnabled_) {
            feedbackL = filterL_.processSample(feedbackL);
            feedbackR = filterR_.processSample(feedbackR);
        }

        // Scale by feedback amount
        feedbackL *= currentFeedback;
        feedbackR *= currentFeedback;

        // Apply soft limiting if feedback > 100% to prevent runaway
        if (currentFeedback > 1.0f) {
            feedbackL = std::tanh(feedbackL);
            feedbackR = std::tanh(feedbackR);
        }

        // Write input + feedback to delay lines
        delayLineL_.write(dryBufferL_[i] + feedbackL);
        delayLineR_.write(dryBufferR_[i] + feedbackR);

        // Mix dry/wet for output
        left[i] = (dryBufferL_[i] * (1.0f - currentDryWet) + delayedL * currentDryWet) * currentOutputGain;
        right[i] = (dryBufferR_[i] * (1.0f - currentDryWet) + delayedR * currentDryWet) * currentOutputGain;
    }

    // Process pitch shifting on this block's delay output
    // This will be used as feedback source in the NEXT block
    pitchShifterL_.process(pitchBufferL_.data(), pitchBufferL_.data(), numSamples);
    pitchShifterR_.process(pitchBufferR_.data(), pitchBufferR_.data(), numSamples);

    // Process diffusion on pitch-shifted signal (if enabled)
    const float currentDiffusion = diffusionSmoother_.getCurrentValue();
    if (currentDiffusion > 0.001f) {
        diffusion_.process(pitchBufferL_.data(), pitchBufferR_.data(),
                           diffusionBufferL_.data(), diffusionBufferR_.data(), numSamples);

        // Blend pitch-shifted with diffused based on diffusion amount
        for (size_t i = 0; i < numSamples; ++i) {
            pitchBufferL_[i] = pitchBufferL_[i] * (1.0f - currentDiffusion) +
                               diffusionBufferL_[i] * currentDiffusion;
            pitchBufferR_[i] = pitchBufferR_[i] * (1.0f - currentDiffusion) +
                               diffusionBufferR_[i] * currentDiffusion;
        }
    }

    // Store for next block's feedback
    for (size_t i = 0; i < numSamples; ++i) {
        prevFeedbackL_[i] = pitchBufferL_[i];
        prevFeedbackR_[i] = pitchBufferR_[i];
    }
    prevFeedbackSize_ = numSamples;
    prevFeedbackReadPos_ = 0;
}

} // namespace DSP
} // namespace Iterum
