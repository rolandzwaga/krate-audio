// ==============================================================================
// Layer 2: DSP Processor - WavefolderProcessor
// ==============================================================================
// Full-featured wavefolding processor with multiple models, symmetry control,
// DC blocking, and dry/wet mix.
//
// Feature: 061-wavefolder-processor
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/wavefold_math.h (WavefoldMath::triangleFold)
//   - Layer 1: primitives/wavefolder.h (Wavefolder, WavefoldType)
//   - Layer 1: primitives/dc_blocker.h (DCBlocker)
//   - Layer 1: primitives/smoother.h (OnePoleSmoother)
//   - stdlib: <cstddef>, <algorithm>, <cmath>, <array>, <cstdint>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.5% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/061-wavefolder-processor/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/wavefold_math.h>
#include <krate/dsp/primitives/wavefolder.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Available wavefolder model types (FR-001, FR-002).
///
/// Each model has distinct harmonic characteristics:
/// - Simple: Dense odd harmonics, smooth rolloff (triangle fold)
/// - Serge: FM-like sparse spectrum (sine fold)
/// - Buchla259: Rich timbre from parallel folding stages
/// - Lockhart: Even/odd harmonics with spectral nulls (Lambert-W)
///
/// @note Default: Simple (most general-purpose)
enum class WavefolderModel : uint8_t {
    Simple = 0,    ///< Triangle fold - basic symmetric folding
    Serge = 1,     ///< Sine fold - characteristic Serge wavefolder
    Buchla259 = 2, ///< 5-stage parallel - Buchla 259 style
    Lockhart = 3   ///< Lambert-W based - circuit-derived
};

/// @brief Sub-modes for Buchla259 model (FR-002a).
///
/// - Classic: Fixed authentic thresholds and gains
/// - Custom: User-configurable thresholds and gains
enum class BuchlaMode : uint8_t {
    Classic = 0,   ///< Fixed authentic thresholds/gains
    Custom = 1     ///< User-configurable thresholds/gains
};

// =============================================================================
// WavefolderProcessor Class
// =============================================================================

/// @brief Layer 2 DSP processor for full-featured wavefolding.
///
/// Provides configurable wavefolding with four distinct models, each with
/// unique harmonic characteristics. Includes parameter smoothing, symmetry
/// control for even harmonics, DC blocking, and dry/wet mix.
///
/// @par Signal Chain (FR-025)
/// Input -> [Symmetry DC Offset] -> [Wavefolder (model)] -> [DC Blocker] -> [Mix Blend] -> Output
///
/// @par Features
/// - Four wavefolder models: Simple (triangle), Serge (sine), Buchla259 (5-stage), Lockhart (Lambert-W)
/// - Fold amount control [0.1, 10.0] for intensity
/// - Symmetry control [-1, +1] for even/odd harmonic balance
/// - DC blocking after folding (10Hz cutoff)
/// - Dry/wet mix for parallel processing
/// - Parameter smoothing (5ms) to prevent clicks
/// - No internal oversampling (handled externally per user preference)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 0.5% CPU per instance)
///
/// @par Usage Example
/// @code
/// WavefolderProcessor folder;
/// folder.prepare(44100.0, 512);
/// folder.setModel(WavefolderModel::Serge);
/// folder.setFoldAmount(3.14159f);  // Characteristic Serge tone
/// folder.setSymmetry(0.0f);        // Symmetric folding
/// folder.setMix(1.0f);             // 100% wet
///
/// // Process audio
/// folder.process(buffer, numSamples);
/// @endcode
///
/// @see specs/061-wavefolder-processor/spec.md
class WavefolderProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum fold amount to prevent degeneracy (FR-009)
    static constexpr float kMinFoldAmount = 0.1f;

    /// Maximum fold amount (FR-009)
    static constexpr float kMaxFoldAmount = 10.0f;

    /// Default smoothing time in milliseconds (FR-029)
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// DC blocker cutoff frequency in Hz (FR-035)
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    /// Number of stages in Buchla259 model (FR-021)
    static constexpr size_t kBuchlaStages = 5;

    // =========================================================================
    // Lifecycle (FR-003, FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Default constructor with safe defaults (FR-006).
    ///
    /// Initializes with:
    /// - Model: Simple
    /// - foldAmount: 1.0
    /// - symmetry: 0.0 (centered)
    /// - mix: 1.0 (100% wet)
    /// - buchlaMode: Classic
    WavefolderProcessor() noexcept = default;

    /// @brief Configure the processor for the given sample rate (FR-003).
    ///
    /// Configures internal components (Wavefolder, DCBlocker, smoothers)
    /// for the specified sample rate. Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples (unused, for future use)
    void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Configure wavefolder for current model
        updateWavefolderType();

        // Configure DC blocker
        dcBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

        // Configure smoothers with 5ms smoothing time
        const float sr = static_cast<float>(sampleRate);
        foldAmountSmoother_.configure(kDefaultSmoothingMs, sr);
        symmetrySmoother_.configure(kDefaultSmoothingMs, sr);
        mixSmoother_.configure(kDefaultSmoothingMs, sr);

        // Initialize smoother targets with current parameter values
        foldAmountSmoother_.setTarget(foldAmount_);
        symmetrySmoother_.setTarget(symmetry_);
        mixSmoother_.setTarget(mix_);

        // Snap to initial values
        foldAmountSmoother_.snapToTarget();
        symmetrySmoother_.snapToTarget();
        mixSmoother_.snapToTarget();

        prepared_ = true;
    }

    /// @brief Reset all internal state without reallocation (FR-004).
    ///
    /// Clears DC blocker state and snaps smoothers to current target values.
    /// Call when starting a new audio stream or after discontinuity.
    void reset() noexcept {
        // Snap smoothers to current targets (no ramp on next process) (FR-033)
        foldAmountSmoother_.snapToTarget();
        symmetrySmoother_.snapToTarget();
        mixSmoother_.snapToTarget();

        // Reset DC blocker state
        dcBlocker_.reset();
    }

    // =========================================================================
    // Model Selection (FR-007, FR-014, FR-023, FR-023a)
    // =========================================================================

    /// @brief Set the wavefolder model (FR-007).
    ///
    /// @param model WavefolderModel to use
    /// @note Change is immediate (FR-032) - no smoothing
    void setModel(WavefolderModel model) noexcept {
        model_ = model;
        updateWavefolderType();
    }

    /// @brief Get the current wavefolder model (FR-014).
    [[nodiscard]] WavefolderModel getModel() const noexcept {
        return model_;
    }

    /// @brief Set the Buchla259 sub-mode (FR-023).
    ///
    /// @param mode BuchlaMode (Classic or Custom)
    /// @note Only affects processing when model == Buchla259
    void setBuchlaMode(BuchlaMode mode) noexcept {
        buchlaMode_ = mode;
    }

    /// @brief Get the current Buchla259 sub-mode (FR-023a).
    [[nodiscard]] BuchlaMode getBuchlaMode() const noexcept {
        return buchlaMode_;
    }

    // =========================================================================
    // Buchla259 Custom Configuration (FR-022b, FR-022c)
    // =========================================================================

    /// @brief Set custom thresholds for Buchla259 Custom mode (FR-022b).
    ///
    /// @param thresholds Array of 5 threshold values for parallel stages
    /// @note Only affects processing when buchlaMode == Custom
    void setBuchlaThresholds(const std::array<float, kBuchlaStages>& thresholds) noexcept {
        buchlaThresholds_ = thresholds;
    }

    /// @brief Set custom gains for Buchla259 Custom mode (FR-022c).
    ///
    /// @param gains Array of 5 gain values for parallel stages
    /// @note Only affects processing when buchlaMode == Custom
    void setBuchlaGains(const std::array<float, kBuchlaStages>& gains) noexcept {
        buchlaGains_ = gains;
    }

    // =========================================================================
    // Parameter Setters (FR-008, FR-009, FR-010, FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set the fold amount (intensity) (FR-008).
    ///
    /// Controls how aggressively the signal is folded.
    /// Value is clamped to [0.1, 10.0] (FR-009).
    ///
    /// @param amount Fold amount
    void setFoldAmount(float amount) noexcept {
        foldAmount_ = std::clamp(amount, kMinFoldAmount, kMaxFoldAmount);
        foldAmountSmoother_.setTarget(foldAmount_);
    }

    /// @brief Set the symmetry (asymmetric folding amount) (FR-010).
    ///
    /// Controls even harmonic content.
    /// - 0.0: Symmetric folding (odd harmonics only)
    /// - +/-1.0: Maximum asymmetry (even harmonics added)
    /// Value is clamped to [-1.0, +1.0] (FR-011).
    ///
    /// @param symmetry Symmetry value [-1.0, +1.0]
    void setSymmetry(float symmetry) noexcept {
        symmetry_ = std::clamp(symmetry, -1.0f, 1.0f);
        symmetrySmoother_.setTarget(symmetry_);
    }

    /// @brief Set the dry/wet mix (FR-012).
    ///
    /// - 0.0: Full bypass (output equals input)
    /// - 1.0: 100% folded signal
    /// Value is clamped to [0.0, 1.0] (FR-013).
    ///
    /// @param mix Mix amount [0.0, 1.0]
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    // =========================================================================
    // Parameter Getters (FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Get the current fold amount (FR-015).
    [[nodiscard]] float getFoldAmount() const noexcept {
        return foldAmount_;
    }

    /// @brief Get the current symmetry value (FR-016).
    [[nodiscard]] float getSymmetry() const noexcept {
        return symmetry_;
    }

    /// @brief Get the current mix value (FR-017).
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Processing (FR-024, FR-025, FR-026, FR-027, FR-028)
    // =========================================================================

    /// @brief Process a block of audio samples in-place (FR-024).
    ///
    /// Applies the wavefolder effect with the current parameter settings.
    /// Signal chain (FR-025): symmetry offset -> wavefolder -> DC blocker -> mix blend
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call (FR-026)
    /// @note n=0 is handled gracefully (FR-027)
    /// @note mix=0 produces exact input (FR-028)
    /// @note If prepare() not called, returns input unchanged (FR-005)
    void process(float* buffer, size_t numSamples) noexcept {
        // FR-027: Handle n=0 gracefully
        if (numSamples == 0 || buffer == nullptr) {
            return;
        }

        // FR-005: Return input unchanged if not prepared
        if (!prepared_) {
            return;
        }

        // Process sample-by-sample for parameter smoothing
        for (size_t i = 0; i < numSamples; ++i) {
            // Advance smoothers
            const float foldAmt = foldAmountSmoother_.process();
            const float sym = symmetrySmoother_.process();
            const float mixAmt = mixSmoother_.process();

            // FR-028: Full bypass when mix is essentially 0
            // Skip wavefolder AND DC blocker - output equals input exactly
            if (mixAmt < 0.0001f) {
                continue;  // Output equals input
            }

            // Store dry sample for blend
            const float dry = buffer[i];

            // FR-025: Apply symmetry as DC offset before wavefolding
            // Scale symmetry by 1/foldAmount for consistent effect across fold intensities
            float wet = dry + sym * (1.0f / foldAmt);

            // Apply selected wavefolder model
            switch (model_) {
                case WavefolderModel::Simple:
                case WavefolderModel::Serge:
                case WavefolderModel::Lockhart:
                    // Use Layer 1 Wavefolder primitive (FR-037)
                    wavefolder_.setFoldAmount(foldAmt);
                    wet = wavefolder_.process(wet);
                    break;

                case WavefolderModel::Buchla259:
                    // Use custom 5-stage parallel implementation (FR-021)
                    wet = applyBuchla259(wet, foldAmt);
                    break;
            }

            // FR-034: Apply DC blocking after wavefolding
            wet = dcBlocker_.process(wet);

            // Apply dry/wet mix blend
            buffer[i] = dry * (1.0f - mixAmt) + wet * mixAmt;
        }
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update the Wavefolder primitive's type based on current model.
    void updateWavefolderType() noexcept {
        switch (model_) {
            case WavefolderModel::Simple:
                wavefolder_.setType(WavefoldType::Triangle);  // FR-018
                break;
            case WavefolderModel::Serge:
                wavefolder_.setType(WavefoldType::Sine);      // FR-019
                break;
            case WavefolderModel::Lockhart:
                wavefolder_.setType(WavefoldType::Lockhart);  // FR-020
                break;
            case WavefolderModel::Buchla259:
                // Buchla259 uses custom implementation, not Wavefolder primitive
                break;
        }
    }

    /// @brief Apply Buchla259 5-stage parallel folding (FR-021).
    ///
    /// Implements the characteristic Buchla 259 wavefolder architecture
    /// with 5 parallel folding stages, each with different threshold and gain.
    ///
    /// @param input Input sample (with symmetry offset already applied)
    /// @param foldAmount Current smoothed fold amount
    /// @return Folded output (sum of all stages, normalized)
    [[nodiscard]] float applyBuchla259(float input, float foldAmount) const noexcept {
        // Select thresholds and gains based on mode (FR-022)
        const std::array<float, kBuchlaStages>* thresholds;
        const std::array<float, kBuchlaStages>* gains;

        if (buchlaMode_ == BuchlaMode::Custom) {
            // FR-022b, FR-022c: Use custom values
            thresholds = &buchlaThresholds_;
            gains = &buchlaGains_;
        } else {
            // FR-022a: Use fixed Classic values
            thresholds = &kClassicBuchlaThresholds;
            gains = &kClassicBuchlaGains;
        }

        // Sum output from all stages
        float output = 0.0f;
        float gainSum = 0.0f;

        for (size_t i = 0; i < kBuchlaStages; ++i) {
            // Scale threshold by 1/foldAmount per FR-022a
            const float scaledThreshold = (*thresholds)[i] / foldAmount;
            const float stageGain = (*gains)[i];

            // Apply triangle fold at this threshold
            const float stageFolded = WavefoldMath::triangleFold(input, scaledThreshold);

            // Accumulate weighted output
            output += stageFolded * stageGain;
            gainSum += stageGain;
        }

        // Normalize by gain sum for consistent output level
        if (gainSum > 0.0f) {
            output /= gainSum;
        }

        return output;
    }

    // =========================================================================
    // Constants for Buchla259 Classic Mode (FR-022a)
    // =========================================================================

    /// Classic thresholds: {0.2, 0.4, 0.6, 0.8, 1.0}
    static constexpr std::array<float, kBuchlaStages> kClassicBuchlaThresholds = {
        0.2f, 0.4f, 0.6f, 0.8f, 1.0f
    };

    /// Classic gains: {1.0, 0.8, 0.6, 0.4, 0.2}
    static constexpr std::array<float, kBuchlaStages> kClassicBuchlaGains = {
        1.0f, 0.8f, 0.6f, 0.4f, 0.2f
    };

    // =========================================================================
    // Parameters (stored in user units)
    // =========================================================================

    WavefolderModel model_ = WavefolderModel::Simple;
    BuchlaMode buchlaMode_ = BuchlaMode::Classic;
    float foldAmount_ = 1.0f;       ///< Fold intensity [0.1, 10.0]
    float symmetry_ = 0.0f;         ///< Asymmetry [-1.0, +1.0]
    float mix_ = 1.0f;              ///< Dry/wet [0.0, 1.0]

    // =========================================================================
    // Buchla259 Custom Configuration
    // =========================================================================

    std::array<float, kBuchlaStages> buchlaThresholds_ = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    std::array<float, kBuchlaStages> buchlaGains_ = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};

    // =========================================================================
    // Parameter Smoothers (FR-029, FR-030, FR-031, FR-039)
    // =========================================================================

    OnePoleSmoother foldAmountSmoother_;  ///< Smoother for fold amount
    OnePoleSmoother symmetrySmoother_;    ///< Smoother for symmetry
    OnePoleSmoother mixSmoother_;         ///< Smoother for mix

    // =========================================================================
    // DSP Components (FR-037, FR-038)
    // =========================================================================

    Wavefolder wavefolder_;   ///< For Simple, Serge, Lockhart models
    DCBlocker dcBlocker_;     ///< DC offset removal after folding

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;  ///< Current sample rate
    bool prepared_ = false;         ///< Whether prepare() has been called
};

} // namespace DSP
} // namespace Krate
