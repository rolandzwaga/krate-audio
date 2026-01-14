// ==============================================================================
// Layer 3: DSP System - AmpChannel
// ==============================================================================
// Guitar amp channel with multiple gain stages, tone shaping, and optional
// oversampling. Composes TubeStage processors with Baxandall tone stack,
// bright cap filter, and gain staging for complete amp channel modeling.
//
// Feature: 065-amp-channel
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/db_utils.h (dbToGain)
//   - Layer 1: primitives/biquad.h, primitives/dc_blocker.h,
//              primitives/smoother.h, primitives/oversampler.h
//   - Layer 2: processors/tube_stage.h
//   - stdlib: <cstddef>, <algorithm>, <cmath>, <array>, <vector>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 3 (depends only on Layers 0, 1, 2)
// - Principle X: DSP Constraints (oversampling for saturation, DC blocking)
// - Principle XI: Performance Budget (< 1% CPU for Layer 3 system)
// - Principle XII: Test-First Development
//
// Reference: specs/065-amp-channel/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/tube_stage.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Tone stack position relative to distortion stages
enum class ToneStackPosition : uint8_t {
    Pre = 0,   ///< Before preamp stages (EQ drives into distortion)
    Post = 1   ///< After poweramp stage (EQ shapes distorted tone)
};

// =============================================================================
// AmpChannel Class
// =============================================================================

/// @brief Layer 3 System - Guitar amp channel with gain staging and tone shaping
///
/// Models a complete guitar amplifier channel with:
/// - Configurable preamp stages (1-3 TubeStage processors)
/// - Single poweramp stage
/// - Baxandall-style tone stack (bass/mid/treble/presence)
/// - Bright cap filter with gain-dependent attenuation
/// - Optional 2x/4x oversampling for anti-aliasing
///
/// @par Signal Chain (Post tone stack position - default)
/// Input -> [Input Gain] -> [Bright Cap] -> [Preamp Stages] -> [Poweramp] ->
/// [Tone Stack] -> [Master Volume] -> Output
///
/// @par Signal Chain (Pre tone stack position)
/// Input -> [Input Gain] -> [Bright Cap] -> [Tone Stack] -> [Preamp Stages] ->
/// [Poweramp] -> [Master Volume] -> Output
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, value semantics)
/// - Principle IX: Layer 3 (depends only on Layers 0, 1, 2)
/// - Principle X: DSP Constraints (oversampling, DC blocking, smoothing)
/// - Principle XI: Performance Budget (< 1% CPU per instance)
///
/// @par Usage Example
/// @code
/// AmpChannel amp;
/// amp.prepare(44100.0, 512);
/// amp.setPreampGain(12.0f);    // +12dB drive
/// amp.setPreampStages(2);       // 2 preamp stages
/// amp.setBass(0.7f);            // Boost bass
/// amp.setBrightCap(true);       // Enable bright cap
///
/// // Process audio blocks
/// amp.process(buffer, numSamples);
/// @endcode
///
/// @see specs/065-amp-channel/spec.md
class AmpChannel {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum gain in dB for input, preamp, poweramp
    static constexpr float kMinGainDb = -24.0f;

    /// Maximum gain in dB for input, preamp, poweramp
    static constexpr float kMaxGainDb = +24.0f;

    /// Minimum master volume in dB
    static constexpr float kMinMasterDb = -60.0f;

    /// Maximum master volume in dB
    static constexpr float kMaxMasterDb = +6.0f;

    /// Default smoothing time in milliseconds
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// Minimum number of preamp stages
    static constexpr int kMinPreampStages = 1;

    /// Maximum number of preamp stages
    static constexpr int kMaxPreampStages = 3;

    /// Default number of preamp stages (FR-013)
    static constexpr int kDefaultPreampStages = 2;

    // Tone stack frequencies
    static constexpr float kBassFreqHz = 100.0f;
    static constexpr float kMidFreqHz = 800.0f;
    static constexpr float kTrebleFreqHz = 3000.0f;
    static constexpr float kPresenceFreqHz = 5000.0f;

    // Bright cap parameters
    static constexpr float kBrightCapFreqHz = 3000.0f;
    static constexpr float kBrightCapMaxBoostDb = 6.0f;

    // Filter Q values
    static constexpr float kButterworthQ = 0.707f;  ///< Butterworth Q for shelving
    static constexpr float kMidQ = 1.0f;            ///< Q for parametric mid
    static constexpr float kPresenceQ = 0.5f;       ///< Wider Q for presence

    // Tone stack gain range
    static constexpr float kToneMaxBoostDb = 12.0f;  ///< Max +/-12dB for bass/mid/treble
    static constexpr float kPresenceMaxBoostDb = 6.0f;  ///< Max +/-6dB for presence

    // DC blocker cutoff
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor with safe defaults.
    ///
    /// Initializes with:
    /// - All gains at 0 dB (unity)
    /// - 2 preamp stages
    /// - Tone controls at 0.5 (neutral)
    /// - Bright cap disabled
    /// - Oversampling factor 1 (disabled)
    AmpChannel() noexcept = default;

    /// @brief Configure the system for the given sample rate.
    ///
    /// Configures all internal components for processing. Must be called
    /// before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation.
    ///
    /// Clears filter state and snaps smoothers to current targets.
    /// Also applies pending oversampling factor changes (FR-027).
    void reset() noexcept;

    // =========================================================================
    // Gain Staging (FR-004 to FR-008)
    // =========================================================================

    /// @brief Set the input gain in dB.
    ///
    /// Adjusts input level before any processing.
    /// Value is clamped to [-24, +24] dB.
    ///
    /// @param dB Input gain in decibels
    void setInputGain(float dB) noexcept;

    /// @brief Set the preamp gain (drive) in dB.
    ///
    /// Controls saturation intensity of preamp stages.
    /// Value is clamped to [-24, +24] dB.
    ///
    /// @param dB Preamp gain in decibels
    void setPreampGain(float dB) noexcept;

    /// @brief Set the poweramp gain in dB.
    ///
    /// Controls saturation intensity of poweramp stage.
    /// Value is clamped to [-24, +24] dB.
    ///
    /// @param dB Poweramp gain in decibels
    void setPowerampGain(float dB) noexcept;

    /// @brief Set the master volume in dB.
    ///
    /// Final output level control.
    /// Value is clamped to [-60, +6] dB.
    ///
    /// @param dB Master volume in decibels
    void setMasterVolume(float dB) noexcept;

    /// @brief Get the current input gain in dB.
    [[nodiscard]] float getInputGain() const noexcept;

    /// @brief Get the current preamp gain in dB.
    [[nodiscard]] float getPreampGain() const noexcept;

    /// @brief Get the current poweramp gain in dB.
    [[nodiscard]] float getPowerampGain() const noexcept;

    /// @brief Get the current master volume in dB.
    [[nodiscard]] float getMasterVolume() const noexcept;

    // =========================================================================
    // Preamp Configuration (FR-009 to FR-013)
    // =========================================================================

    /// @brief Set the number of active preamp stages.
    ///
    /// Controls how many preamp stages process the signal.
    /// Value is clamped to [1, 3].
    ///
    /// @param count Number of active stages (1-3)
    void setPreampStages(int count) noexcept;

    /// @brief Get the current number of active preamp stages.
    [[nodiscard]] int getPreampStages() const noexcept;

    // =========================================================================
    // Tone Stack (FR-014 to FR-021)
    // =========================================================================

    /// @brief Set the tone stack position.
    ///
    /// @param pos Pre (before distortion) or Post (after distortion)
    void setToneStackPosition(ToneStackPosition pos) noexcept;

    /// @brief Set the bass control.
    ///
    /// Value is clamped to [0, 1] and maps to +/-12dB.
    /// 0.5 = neutral (0dB).
    ///
    /// @param value Bass level [0, 1]
    void setBass(float value) noexcept;

    /// @brief Set the mid control.
    ///
    /// Value is clamped to [0, 1] and maps to +/-12dB.
    /// 0.5 = neutral (0dB).
    ///
    /// @param value Mid level [0, 1]
    void setMid(float value) noexcept;

    /// @brief Set the treble control.
    ///
    /// Value is clamped to [0, 1] and maps to +/-12dB.
    /// 0.5 = neutral (0dB).
    ///
    /// @param value Treble level [0, 1]
    void setTreble(float value) noexcept;

    /// @brief Set the presence control.
    ///
    /// Value is clamped to [0, 1] and maps to +/-6dB.
    /// 0.5 = neutral (0dB).
    ///
    /// @param value Presence level [0, 1]
    void setPresence(float value) noexcept;

    /// @brief Get the current tone stack position.
    [[nodiscard]] ToneStackPosition getToneStackPosition() const noexcept;

    /// @brief Get the current bass level.
    [[nodiscard]] float getBass() const noexcept;

    /// @brief Get the current mid level.
    [[nodiscard]] float getMid() const noexcept;

    /// @brief Get the current treble level.
    [[nodiscard]] float getTreble() const noexcept;

    /// @brief Get the current presence level.
    [[nodiscard]] float getPresence() const noexcept;

    // =========================================================================
    // Character Controls (FR-022 to FR-025)
    // =========================================================================

    /// @brief Enable or disable the bright cap filter.
    ///
    /// When enabled, adds high-frequency boost that decreases
    /// as input gain increases (vintage amp behavior).
    ///
    /// @param enabled true to enable bright cap
    void setBrightCap(bool enabled) noexcept;

    /// @brief Get the current bright cap state.
    [[nodiscard]] bool getBrightCap() const noexcept;

    // =========================================================================
    // Oversampling (FR-026 to FR-030)
    // =========================================================================

    /// @brief Set the oversampling factor.
    ///
    /// Change is deferred until reset() or prepare() is called (FR-027).
    /// Valid values: 1 (disabled), 2, or 4.
    ///
    /// @param factor Oversampling factor (1, 2, or 4)
    void setOversamplingFactor(int factor) noexcept;

    /// @brief Get the current oversampling factor.
    ///
    /// Returns the active factor, not the pending factor.
    [[nodiscard]] int getOversamplingFactor() const noexcept;

    /// @brief Get the processing latency in samples.
    ///
    /// Returns 0 when oversampling is disabled (factor = 1).
    [[nodiscard]] size_t getLatency() const noexcept;

    // =========================================================================
    // Processing (FR-031 to FR-034)
    // =========================================================================

    /// @brief Process a block of audio samples in-place.
    ///
    /// Applies the complete amp channel processing chain.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call (FR-003)
    /// @note n=0 is handled gracefully (FR-032)
    /// @note nullptr is handled gracefully (FR-033)
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // =========================================================================
    // Gain Parameters (stored in dB)
    // =========================================================================

    float inputGainDb_ = 0.0f;
    float preampGainDb_ = 0.0f;
    float powerampGainDb_ = 0.0f;
    float masterVolumeDb_ = 0.0f;

    // =========================================================================
    // Preamp Configuration
    // =========================================================================

    int activePreampStages_ = kDefaultPreampStages;

    // =========================================================================
    // Tone Stack Parameters (stored as normalized 0-1)
    // =========================================================================

    ToneStackPosition toneStackPosition_ = ToneStackPosition::Post;
    float bassValue_ = 0.5f;
    float midValue_ = 0.5f;
    float trebleValue_ = 0.5f;
    float presenceValue_ = 0.5f;

    // =========================================================================
    // Character Controls
    // =========================================================================

    bool brightCapEnabled_ = false;

    // =========================================================================
    // Oversampling
    // =========================================================================

    int currentOversamplingFactor_ = 1;
    int pendingOversamplingFactor_ = 1;

    // =========================================================================
    // DSP Components - Parameter Smoothers
    // =========================================================================

    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother masterVolumeSmoother_;

    // =========================================================================
    // DSP Components - Preamp Stages (fixed array, active count variable)
    // =========================================================================

    std::array<TubeStage, kMaxPreampStages> preampStages_;
    std::array<DCBlocker, kMaxPreampStages> preampDCBlockers_;

    // =========================================================================
    // DSP Components - Poweramp Stage
    // =========================================================================

    TubeStage powerampStage_;
    DCBlocker powerampDCBlocker_;

    // =========================================================================
    // DSP Components - Tone Stack (Baxandall style)
    // =========================================================================

    Biquad bassFilter_;        ///< LowShelf @ 100Hz
    Biquad midFilter_;         ///< Peak @ 800Hz
    Biquad trebleFilter_;      ///< HighShelf @ 3kHz
    Biquad presenceFilter_;    ///< HighShelf @ 5kHz

    // =========================================================================
    // DSP Components - Bright Cap Filter
    // =========================================================================

    Biquad brightCapFilter_;   ///< HighShelf @ 3kHz, gain-dependent

    // =========================================================================
    // DSP Components - Oversamplers
    // =========================================================================

    Oversampler<2, 1> oversampler2x_;
    Oversampler<4, 1> oversampler4x_;

    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Update tone stack filter coefficients.
    void updateToneStack() noexcept;

    /// @brief Update bright cap filter based on current input gain.
    void updateBrightCap() noexcept;

    /// @brief Configure oversampler with current/pending factor.
    void configureOversampler() noexcept;

    /// @brief Process through preamp stages.
    void processPreampStages(float* buffer, size_t numSamples) noexcept;

    /// @brief Process through tone stack filters.
    void processToneStack(float* buffer, size_t numSamples) noexcept;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void AmpChannel::prepare(double sampleRate, size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Configure smoothers (5ms default)
    const float sr = static_cast<float>(sampleRate);
    inputGainSmoother_.configure(kDefaultSmoothingMs, sr);
    masterVolumeSmoother_.configure(kDefaultSmoothingMs, sr);

    // Initialize smoother targets with current parameter values
    inputGainSmoother_.setTarget(dbToGain(inputGainDb_));
    inputGainSmoother_.snapToTarget();
    masterVolumeSmoother_.setTarget(dbToGain(masterVolumeDb_));
    masterVolumeSmoother_.snapToTarget();

    // Prepare all preamp stages
    for (auto& stage : preampStages_) {
        stage.prepare(sampleRate, maxBlockSize);
    }
    for (auto& blocker : preampDCBlockers_) {
        blocker.prepare(sampleRate, kDCBlockerCutoffHz);
    }

    // Configure preamp stage 1 with preamp gain
    preampStages_[0].setInputGain(preampGainDb_);
    preampStages_[0].setOutputGain(0.0f);
    preampStages_[0].setBias(0.0f);
    preampStages_[0].setSaturationAmount(1.0f);

    // Configure preamp stages 2 and 3 with slight bias variations
    preampStages_[1].setInputGain(0.0f);
    preampStages_[1].setOutputGain(0.0f);
    preampStages_[1].setBias(0.1f);
    preampStages_[1].setSaturationAmount(1.0f);

    preampStages_[2].setInputGain(0.0f);
    preampStages_[2].setOutputGain(0.0f);
    preampStages_[2].setBias(0.05f);
    preampStages_[2].setSaturationAmount(1.0f);

    // Prepare poweramp stage
    powerampStage_.prepare(sampleRate, maxBlockSize);
    powerampStage_.setInputGain(powerampGainDb_);
    powerampStage_.setOutputGain(0.0f);
    powerampStage_.setBias(0.0f);
    powerampStage_.setSaturationAmount(1.0f);

    powerampDCBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

    // Configure tone stack
    updateToneStack();

    // Configure bright cap
    updateBrightCap();

    // Configure oversampler
    configureOversampler();

    reset();
}

inline void AmpChannel::reset() noexcept {
    // Apply pending oversampling factor change (FR-027)
    if (pendingOversamplingFactor_ != currentOversamplingFactor_) {
        currentOversamplingFactor_ = pendingOversamplingFactor_;
        configureOversampler();
    }

    // Snap smoothers to current targets
    inputGainSmoother_.snapToTarget();
    masterVolumeSmoother_.snapToTarget();

    // Reset all preamp stages and DC blockers
    for (auto& stage : preampStages_) {
        stage.reset();
    }
    for (auto& blocker : preampDCBlockers_) {
        blocker.reset();
    }

    // Reset poweramp
    powerampStage_.reset();
    powerampDCBlocker_.reset();

    // Reset tone stack filters
    bassFilter_.reset();
    midFilter_.reset();
    trebleFilter_.reset();
    presenceFilter_.reset();

    // Reset bright cap
    brightCapFilter_.reset();

    // Reset oversamplers
    oversampler2x_.reset();
    oversampler4x_.reset();
}

// =========================================================================
// Gain Staging Implementation
// =========================================================================

inline void AmpChannel::setInputGain(float dB) noexcept {
    inputGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
    inputGainSmoother_.setTarget(dbToGain(inputGainDb_));

    // Update bright cap as it depends on input gain
    if (brightCapEnabled_) {
        updateBrightCap();
    }
}

inline void AmpChannel::setPreampGain(float dB) noexcept {
    preampGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
    // Update first preamp stage with new gain
    preampStages_[0].setInputGain(preampGainDb_);
}

inline void AmpChannel::setPowerampGain(float dB) noexcept {
    powerampGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
    powerampStage_.setInputGain(powerampGainDb_);
}

inline void AmpChannel::setMasterVolume(float dB) noexcept {
    masterVolumeDb_ = std::clamp(dB, kMinMasterDb, kMaxMasterDb);
    masterVolumeSmoother_.setTarget(dbToGain(masterVolumeDb_));
}

inline float AmpChannel::getInputGain() const noexcept {
    return inputGainDb_;
}

inline float AmpChannel::getPreampGain() const noexcept {
    return preampGainDb_;
}

inline float AmpChannel::getPowerampGain() const noexcept {
    return powerampGainDb_;
}

inline float AmpChannel::getMasterVolume() const noexcept {
    return masterVolumeDb_;
}

// =========================================================================
// Preamp Configuration Implementation
// =========================================================================

inline void AmpChannel::setPreampStages(int count) noexcept {
    activePreampStages_ = std::clamp(count, kMinPreampStages, kMaxPreampStages);
}

inline int AmpChannel::getPreampStages() const noexcept {
    return activePreampStages_;
}

// =========================================================================
// Tone Stack Implementation
// =========================================================================

inline void AmpChannel::setToneStackPosition(ToneStackPosition pos) noexcept {
    toneStackPosition_ = pos;
}

inline void AmpChannel::setBass(float value) noexcept {
    bassValue_ = std::clamp(value, 0.0f, 1.0f);
    updateToneStack();
}

inline void AmpChannel::setMid(float value) noexcept {
    midValue_ = std::clamp(value, 0.0f, 1.0f);
    updateToneStack();
}

inline void AmpChannel::setTreble(float value) noexcept {
    trebleValue_ = std::clamp(value, 0.0f, 1.0f);
    updateToneStack();
}

inline void AmpChannel::setPresence(float value) noexcept {
    presenceValue_ = std::clamp(value, 0.0f, 1.0f);
    updateToneStack();
}

inline ToneStackPosition AmpChannel::getToneStackPosition() const noexcept {
    return toneStackPosition_;
}

inline float AmpChannel::getBass() const noexcept {
    return bassValue_;
}

inline float AmpChannel::getMid() const noexcept {
    return midValue_;
}

inline float AmpChannel::getTreble() const noexcept {
    return trebleValue_;
}

inline float AmpChannel::getPresence() const noexcept {
    return presenceValue_;
}

// =========================================================================
// Character Controls Implementation
// =========================================================================

inline void AmpChannel::setBrightCap(bool enabled) noexcept {
    brightCapEnabled_ = enabled;
    if (enabled) {
        updateBrightCap();
    }
}

inline bool AmpChannel::getBrightCap() const noexcept {
    return brightCapEnabled_;
}

// =========================================================================
// Oversampling Implementation
// =========================================================================

inline void AmpChannel::setOversamplingFactor(int factor) noexcept {
    // Only accept valid factors
    if (factor == 1 || factor == 2 || factor == 4) {
        pendingOversamplingFactor_ = factor;
    }
}

inline int AmpChannel::getOversamplingFactor() const noexcept {
    return currentOversamplingFactor_;
}

inline size_t AmpChannel::getLatency() const noexcept {
    if (currentOversamplingFactor_ == 2) {
        return oversampler2x_.getLatency();
    } else if (currentOversamplingFactor_ == 4) {
        return oversampler4x_.getLatency();
    }
    return 0;  // No oversampling = no latency
}

// =========================================================================
// Private Method Implementation
// =========================================================================

inline void AmpChannel::updateToneStack() noexcept {
    const float sr = static_cast<float>(sampleRate_);

    // Map [0, 1] -> [-12, +12] dB (or [-6, +6] for presence)
    const float bassDb = (bassValue_ - 0.5f) * 2.0f * kToneMaxBoostDb;
    const float midDb = (midValue_ - 0.5f) * 2.0f * kToneMaxBoostDb;
    const float trebleDb = (trebleValue_ - 0.5f) * 2.0f * kToneMaxBoostDb;
    const float presenceDb = (presenceValue_ - 0.5f) * 2.0f * kPresenceMaxBoostDb;

    // Configure filters
    bassFilter_.configure(FilterType::LowShelf, kBassFreqHz, kButterworthQ, bassDb, sr);
    midFilter_.configure(FilterType::Peak, kMidFreqHz, kMidQ, midDb, sr);
    trebleFilter_.configure(FilterType::HighShelf, kTrebleFreqHz, kButterworthQ, trebleDb, sr);
    presenceFilter_.configure(FilterType::HighShelf, kPresenceFreqHz, kPresenceQ, presenceDb, sr);
}

inline void AmpChannel::updateBrightCap() noexcept {
    // Calculate gain-dependent boost (FR-023, FR-024, FR-025)
    // At -24dB input: +6dB boost (max boost at low gain)
    // At +12dB input: 0dB boost (no boost at high gain)
    // Linear interpolation between these specific thresholds

    constexpr float kBrightCapMinGainDb = -24.0f;  // Full boost threshold
    constexpr float kBrightCapMaxGainDb = +12.0f;  // Zero boost threshold
    constexpr float kBrightCapRange = kBrightCapMaxGainDb - kBrightCapMinGainDb;  // 36dB

    // Normalize: 0.0 at -24dB, 1.0 at +12dB (clamped outside this range)
    const float normalizedGain = (inputGainDb_ - kBrightCapMinGainDb) / kBrightCapRange;
    const float clampedNorm = std::clamp(normalizedGain, 0.0f, 1.0f);

    // Boost decreases as gain increases: 6dB at norm=0 (-24dB), 0dB at norm=1 (+12dB)
    const float boostDb = kBrightCapMaxBoostDb * (1.0f - clampedNorm);

    brightCapFilter_.configure(
        FilterType::HighShelf,
        kBrightCapFreqHz,
        kButterworthQ,
        boostDb,
        static_cast<float>(sampleRate_)
    );
}

inline void AmpChannel::configureOversampler() noexcept {
    if (currentOversamplingFactor_ == 2) {
        oversampler2x_.prepare(sampleRate_, maxBlockSize_,
            OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    } else if (currentOversamplingFactor_ == 4) {
        oversampler4x_.prepare(sampleRate_, maxBlockSize_,
            OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    }
    // Factor 1 needs no configuration (bypass)
}

inline void AmpChannel::processPreampStages(float* buffer, size_t numSamples) noexcept {
    // Process through active preamp stages (FR-009, FR-010)
    for (int i = 0; i < activePreampStages_; ++i) {
        preampStages_[i].process(buffer, numSamples);
        preampDCBlockers_[i].processBlock(buffer, numSamples);  // FR-012
    }
}

inline void AmpChannel::processToneStack(float* buffer, size_t numSamples) noexcept {
    // Apply Baxandall-style tone stack (FR-019, FR-020, FR-021)
    bassFilter_.processBlock(buffer, numSamples);
    midFilter_.processBlock(buffer, numSamples);
    trebleFilter_.processBlock(buffer, numSamples);
    presenceFilter_.processBlock(buffer, numSamples);
}

inline void AmpChannel::process(float* buffer, size_t numSamples) noexcept {
    // FR-032, FR-033: Handle edge cases
    if (numSamples == 0 || buffer == nullptr) {
        return;
    }

    // Apply input gain with smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= inputGainSmoother_.process();
    }

    // Apply bright cap (if enabled, before distortion)
    if (brightCapEnabled_) {
        brightCapFilter_.processBlock(buffer, numSamples);
    }

    // Tone stack in Pre position (before distortion)
    if (toneStackPosition_ == ToneStackPosition::Pre) {
        processToneStack(buffer, numSamples);
    }

    // Process through preamp and poweramp with oversampling
    if (currentOversamplingFactor_ == 1) {
        // No oversampling - process directly (FR-030)
        processPreampStages(buffer, numSamples);
        powerampStage_.process(buffer, numSamples);
        powerampDCBlocker_.processBlock(buffer, numSamples);  // FR-012
    } else if (currentOversamplingFactor_ == 2) {
        oversampler2x_.process(buffer, numSamples,
            [this](float* os, size_t n) {
                processPreampStages(os, n);
                powerampStage_.process(os, n);
                powerampDCBlocker_.processBlock(os, n);
            });
    } else {  // 4x
        oversampler4x_.process(buffer, numSamples,
            [this](float* os, size_t n) {
                processPreampStages(os, n);
                powerampStage_.process(os, n);
                powerampDCBlocker_.processBlock(os, n);
            });
    }

    // Tone stack in Post position (after distortion, default)
    if (toneStackPosition_ == ToneStackPosition::Post) {
        processToneStack(buffer, numSamples);
    }

    // Apply master volume with smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= masterVolumeSmoother_.process();
    }
}

} // namespace DSP
} // namespace Krate
