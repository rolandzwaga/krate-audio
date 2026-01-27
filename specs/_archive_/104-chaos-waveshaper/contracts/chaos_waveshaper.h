// ==============================================================================
// Layer 1: DSP Primitive - Chaos Attractor Waveshaper
// ==============================================================================
// API CONTRACT - Implementation target for ChaosWaveshaper
//
// Feature: 104-chaos-waveshaper
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 0: core/db_utils.h (flushDenormal, isNaN, isInf)
//   - Layer 0: core/sigmoid.h (Sigmoid::tanhVariable)
//   - stdlib: <cstdint>, <cstddef>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/104-chaos-waveshaper/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// ChaosModel Enumeration (FR-005 to FR-008)
// =============================================================================

/// @brief Available chaos attractor models.
///
/// Each model has distinct mathematical character:
/// - Lorenz: Classic 3D continuous attractor with swirling, unpredictable behavior
/// - Rossler: Smoother 3D continuous attractor with spiraling patterns
/// - Chua: Double-scroll circuit attractor with bi-modal jumps
/// - Henon: 2D discrete map with sharp, rhythmic transitions
///
/// @note All models use standard "chaotic regime" parameters from literature.
enum class ChaosModel : uint8_t {
    Lorenz = 0,   ///< Lorenz system (sigma=10, rho=28, beta=8/3)
    Rossler = 1,  ///< Rossler system (a=0.2, b=0.2, c=5.7)
    Chua = 2,     ///< Chua circuit (alpha=15.6, beta=28, m0=-1.143, m1=-0.714)
    Henon = 3     ///< Henon map (a=1.4, b=0.3)
};

// =============================================================================
// ChaosWaveshaper Class
// =============================================================================

/// @brief Time-varying waveshaping using chaos attractor dynamics.
///
/// The attractor's normalized X component modulates the drive of a tanh-based
/// soft-clipper, producing distortion that evolves over time without external
/// modulation. Four chaos models provide different characters.
///
/// @par Features
/// - 4 chaos models: Lorenz, Rossler, Chua, Henon (FR-005 to FR-008)
/// - ChaosAmount parameter for dry/wet mixing (FR-010)
/// - AttractorSpeed for evolution rate control (FR-011)
/// - InputCoupling for signal-reactive behavior (FR-012, FR-025 to FR-027)
/// - Automatic state reset on divergence (FR-018, FR-033)
/// - Sample-rate compensated integration (FR-019)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
/// - Principle XI: Performance Budget (< 0.1% CPU per instance)
///
/// @par Design Rationale
/// - Control-rate attractor updates (every 32 samples) for efficiency
/// - No internal oversampling: Compose with Oversampler for anti-aliasing
/// - No internal DC blocking: Tanh is symmetric; compose with DCBlocker if needed
/// - Stateful processing: process() evolves attractor state
///
/// @par Usage Example
/// @code
/// ChaosWaveshaper shaper;
/// shaper.prepare(44100.0);
/// shaper.setModel(ChaosModel::Lorenz);
/// shaper.setChaosAmount(0.5f);
/// shaper.setAttractorSpeed(1.0f);
/// shaper.setInputCoupling(0.3f);
///
/// // Sample-by-sample
/// float output = shaper.process(input);
///
/// // Block processing
/// shaper.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/104-chaos-waveshaper/spec.md
/// @see Oversampler for anti-aliasing
/// @see DCBlocker for DC offset removal (if needed)
class ChaosWaveshaper {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinChaosAmount = 0.0f;
    static constexpr float kMaxChaosAmount = 1.0f;
    static constexpr float kDefaultChaosAmount = 0.5f;

    static constexpr float kMinAttractorSpeed = 0.01f;
    static constexpr float kMaxAttractorSpeed = 100.0f;
    static constexpr float kDefaultAttractorSpeed = 1.0f;

    static constexpr float kMinInputCoupling = 0.0f;
    static constexpr float kMaxInputCoupling = 1.0f;
    static constexpr float kDefaultInputCoupling = 0.0f;

    static constexpr float kMinDrive = 0.5f;   ///< Minimum waveshaping drive
    static constexpr float kMaxDrive = 4.0f;   ///< Maximum waveshaping drive

    static constexpr size_t kControlRateInterval = 32;  ///< Samples between attractor updates

    // =========================================================================
    // Construction (FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes ChaosWaveshaper with:
    /// - Model: Lorenz
    /// - ChaosAmount: 0.5 (50% wet)
    /// - AttractorSpeed: 1.0 (nominal rate)
    /// - InputCoupling: 0.0 (no coupling)
    ///
    /// @note prepare() must be called before processing.
    ChaosWaveshaper() noexcept = default;

    // Default copy/move (trivially copyable state)
    ChaosWaveshaper(const ChaosWaveshaper&) = default;
    ChaosWaveshaper& operator=(const ChaosWaveshaper&) = default;
    ChaosWaveshaper(ChaosWaveshaper&&) noexcept = default;
    ChaosWaveshaper& operator=(ChaosWaveshaper&&) noexcept = default;
    ~ChaosWaveshaper() = default;

    // =========================================================================
    // Initialization (FR-001, FR-002)
    // =========================================================================

    /// @brief Prepare for processing at given sample rate.
    ///
    /// Initializes attractor state and configures sample-rate-dependent
    /// integration timestep.
    ///
    /// @param sampleRate Sample rate in Hz (typically 44100-192000)
    /// @pre sampleRate >= 1000.0 (clamped internally if lower)
    /// @post Attractor initialized to stable starting conditions
    /// @note NOT real-time safe (initializes state)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset attractor to stable initial conditions.
    ///
    /// Reinitializes attractor state variables per current model.
    /// Configuration (model, parameters) is preserved.
    ///
    /// @post State variables reset to model-specific initial conditions
    /// @post Configuration preserved
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set the chaos attractor model.
    ///
    /// @param model Chaos algorithm to use
    ///
    /// @note Model change takes effect immediately.
    /// @note Consider calling reset() after model change for clean transition.
    void setModel(ChaosModel model) noexcept;

    /// @brief Set the chaos amount (dry/wet mix).
    ///
    /// @param amount Amount in range [0.0, 1.0]
    ///               - 0.0 = bypass (output equals input)
    ///               - 1.0 = full chaos processing
    ///
    /// @note Values outside [0, 1] are clamped.
    void setChaosAmount(float amount) noexcept;

    /// @brief Set the attractor evolution speed.
    ///
    /// @param speed Speed multiplier in range [0.01, 100.0]
    ///              - 0.01 = very slow evolution
    ///              - 1.0 = nominal rate
    ///              - 100.0 = rapid fluctuation
    ///
    /// @note Values outside [0.01, 100] are clamped.
    void setAttractorSpeed(float speed) noexcept;

    /// @brief Set the input coupling amount.
    ///
    /// Determines how much input signal amplitude perturbs the attractor state.
    ///
    /// @param coupling Coupling in range [0.0, 1.0]
    ///                 - 0.0 = no coupling (chaos evolves independently)
    ///                 - 1.0 = full coupling (maximum input influence)
    ///
    /// @note Values outside [0, 1] are clamped.
    void setInputCoupling(float coupling) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current chaos model.
    [[nodiscard]] ChaosModel getModel() const noexcept;

    /// @brief Get the current chaos amount.
    [[nodiscard]] float getChaosAmount() const noexcept;

    /// @brief Get the current attractor speed.
    [[nodiscard]] float getAttractorSpeed() const noexcept;

    /// @brief Get the current input coupling.
    [[nodiscard]] float getInputCoupling() const noexcept;

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get configured sample rate.
    [[nodiscard]] double getSampleRate() const noexcept;

    // =========================================================================
    // Processing (FR-003, FR-004, FR-020 to FR-024, FR-028 to FR-033)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Applies chaos-modulated waveshaping:
    /// output = lerp(input, waveshape(input, chaosModulatedDrive), chaosAmount)
    ///
    /// @param input Input sample (assumed normalized to [-1, 1])
    /// @return Chaos-modulated waveshaped output
    ///
    /// @note Real-time safe: no allocations, O(1) complexity
    /// @note NaN inputs are treated as 0.0 (FR-031)
    /// @note Infinity inputs are clamped to [-1, 1] (FR-032)
    /// @note ChaosAmount=0.0 returns input unchanged (FR-023)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() for each sample sequentially.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note Real-time safe: no allocations
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    // Implementation details omitted from contract
    // See data-model.md for internal structure
};

} // namespace DSP
} // namespace Krate
