// ==============================================================================
// Layer 2: DSP Processor - Chaos Modulation Source
// ==============================================================================
// Chaotic attractor modulation source using Lorenz, Rossler, Chua, Henon models.
// Outputs normalized attractor X-axis value for modulation routing.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
//
// Reference: specs/008-modulation-system/spec.md (FR-030 to FR-035)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>  // ChaosModel enum

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Chaotic attractor modulation source.
///
/// Implements ModulationSource interface. Evolves a chaotic attractor system
/// and outputs the normalized X-axis value as modulation signal.
///
/// @par Features
/// - 4 attractor models: Lorenz, Rossler, Chua, Henon (FR-031)
/// - Configurable speed [0.05, 20.0] (FR-032)
/// - Audio coupling [0.0, 1.0] (FR-033)
/// - Soft-limit normalization: tanh(x/scale) (FR-034)
/// - Real-time safe (FR-035)
///
/// @par Output Range: [-1.0, +1.0]
class ChaosModSource : public ModulationSource {
public:
    // Speed and coupling constants
    static constexpr float kMinSpeed = 0.05f;
    static constexpr float kMaxSpeed = 20.0f;
    static constexpr float kDefaultSpeed = 1.0f;
    static constexpr float kMinCoupling = 0.0f;
    static constexpr float kMaxCoupling = 1.0f;
    static constexpr float kDefaultCoupling = 0.0f;
    static constexpr size_t kControlRateInterval = 32;

    // Per-model normalization scale constants (FR-034)
    // Formula: output = tanh(state.x / scale)
    static constexpr float kLorenzScale = 20.0f;
    static constexpr float kRosslerScale = 10.0f;
    static constexpr float kChuaScale = 2.0f;
    static constexpr float kHenonScale = 1.5f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    ChaosModSource() noexcept = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process one sample (call at audio rate).
    /// Updates attractor at control rate (every 32 samples).
    void process() noexcept;

    // =========================================================================
    // ModulationSource Interface
    // =========================================================================

    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    void setModel(ChaosModel model) noexcept;
    void setSpeed(float speed) noexcept;
    void setCoupling(float coupling) noexcept;

    /// @brief Set current audio input level for coupling perturbation.
    /// Called by ModulationEngine with the audio envelope value.
    void setInputLevel(float level) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] ChaosModel getModel() const noexcept;
    [[nodiscard]] float getSpeed() const noexcept;
    [[nodiscard]] float getCoupling() const noexcept;

private:
    struct AttractorState {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    void updateAttractor() noexcept;
    void checkAndResetIfDiverged() noexcept;
    void resetModelState() noexcept;

    void updateLorenz() noexcept;
    void updateRossler() noexcept;
    void updateChua() noexcept;
    void updateHenon() noexcept;

    [[nodiscard]] static float chuaDiode(float x) noexcept;

    // State
    AttractorState state_;
    float normalizedOutput_ = 0.0f;
    float inputLevel_ = 0.0f;
    int samplesUntilUpdate_ = 0;

    // Henon interpolation
    float prevHenonX_ = 0.0f;
    float henonPhase_ = 0.0f;

    // Configuration
    ChaosModel model_ = ChaosModel::Lorenz;
    float speed_ = kDefaultSpeed;
    float coupling_ = kDefaultCoupling;
    double sampleRate_ = 44100.0;

    // Per-model parameters
    float baseDt_ = 0.005f;
    float safeBound_ = 50.0f;
    float normalizationScale_ = 20.0f;
    float perturbationScale_ = 0.1f;
};

}  // namespace DSP
}  // namespace Krate
