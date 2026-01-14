// ==============================================================================
// Layer 2: DSP Processor - Tape Saturator
// ==============================================================================
// Tape saturation processor with Simple (tanh + pre/de-emphasis) and
// Hysteresis (Jiles-Atherton) models.
//
// Features:
// - Simple model: tanh saturation with pre/de-emphasis filtering (+9dB @ 3kHz)
// - Hysteresis model: Jiles-Atherton magnetic hysteresis with RK2/RK4/NR4/NR8 solvers
// - Expert mode: configurable J-A parameters (a, alpha, c, k, Ms)
// - Parameter smoothing: 5ms via OnePoleSmoother
// - DC blocking: 10Hz via DCBlocker
// - Model crossfade: 10ms equal-power crossfade
// - T-scaling: sample rate independence
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]])
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (Simple < 0.3% CPU, Hysteresis/RK4 < 1.5% CPU)
// - Principle XII: Test-First Development
//
// Reference: specs/062-tape-saturator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations (FR-001, FR-002)
// =============================================================================

/// @brief Saturation model selection.
enum class TapeModel : uint8_t {
    Simple = 0,     ///< tanh saturation + pre/de-emphasis filters
    Hysteresis = 1  ///< Jiles-Atherton magnetic hysteresis model
};

/// @brief Numerical solver for Hysteresis model.
enum class HysteresisSolver : uint8_t {
    RK2 = 0,  ///< Runge-Kutta 2nd order (~2 evals/sample)
    RK4 = 1,  ///< Runge-Kutta 4th order (~4 evals/sample)
    NR4 = 2,  ///< Newton-Raphson 4 iterations/sample
    NR8 = 3   ///< Newton-Raphson 8 iterations/sample
};

// =============================================================================
// TapeSaturator Class
// =============================================================================

/// @brief Layer 2 tape saturation processor with Simple and Hysteresis models.
///
/// Provides tape-style saturation with two distinct algorithms:
/// - Simple: tanh saturation with pre/de-emphasis filtering
/// - Hysteresis: Jiles-Atherton magnetic model with configurable solvers
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 1.5% CPU per instance)
///
/// @see specs/062-tape-saturator/spec.md
class TapeSaturator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDriveDb = -24.0f;
    static constexpr float kMaxDriveDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kPreEmphasisFreqHz = 3000.0f;
    static constexpr float kPreEmphasisGainDb = 9.0f;
    static constexpr float kCrossfadeDurationMs = 10.0f;

    // Jiles-Atherton default parameters (DAFx/ChowDSP)
    static constexpr float kDefaultJA_a = 22.0f;
    static constexpr float kDefaultJA_alpha = 1.6e-11f;
    static constexpr float kDefaultJA_c = 1.7f;
    static constexpr float kDefaultJA_k = 27.0f;
    static constexpr float kDefaultJA_Ms = 350000.0f;

    // =========================================================================
    // Lifecycle (FR-003 to FR-006)
    // =========================================================================

    /// @brief Default constructor with safe defaults (FR-006).
    TapeSaturator() noexcept
        : model_(TapeModel::Simple)
        , solver_(HysteresisSolver::RK4)
        , driveDb_(0.0f)
        , saturation_(0.5f)
        , bias_(0.0f)
        , mix_(1.0f)
        , ja_a_(kDefaultJA_a)
        , ja_alpha_(kDefaultJA_alpha)
        , ja_c_(kDefaultJA_c)
        , ja_k_(kDefaultJA_k)
        , ja_Ms_(kDefaultJA_Ms)
        , sampleRate_(0.0)
        , prepared_(false)
        , M_(0.0f)
        , H_prev_(0.0f)
        , TScale_(1.0f)
        , crossfadeActive_(false)
        , crossfadePosition_(0.0f)
        , crossfadeIncrement_(0.0f)
        , previousModel_(TapeModel::Simple) {
    }

    /// @brief Destructor.
    ~TapeSaturator() = default;

    // Default copy/move
    TapeSaturator(const TapeSaturator&) = default;
    TapeSaturator& operator=(const TapeSaturator&) = default;
    TapeSaturator(TapeSaturator&&) noexcept = default;
    TapeSaturator& operator=(TapeSaturator&&) noexcept = default;

    /// @brief Configure for given sample rate and block size (FR-003).
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum expected block size
    void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Configure DC blocker
        dcBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

        // Configure pre-emphasis and de-emphasis filters
        // Pre-emphasis: high shelf boost before saturation
        preEmphasis_.configure(FilterType::HighShelf, kPreEmphasisFreqHz,
                               kButterworthQ, kPreEmphasisGainDb,
                               static_cast<float>(sampleRate));

        // De-emphasis: high shelf cut after saturation (inverse)
        deEmphasis_.configure(FilterType::HighShelf, kPreEmphasisFreqHz,
                              kButterworthQ, -kPreEmphasisGainDb,
                              static_cast<float>(sampleRate));

        // Configure parameter smoothers
        driveSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        saturationSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        biasSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));

        // Snap smoothers to current values
        driveSmoother_.snapTo(dbToGain(driveDb_));
        saturationSmoother_.snapTo(saturation_);
        biasSmoother_.snapTo(bias_);
        mixSmoother_.snapTo(mix_);

        // Calculate T-scaling for sample rate independence
        TScale_ = 44100.0f / static_cast<float>(sampleRate);

        // Calculate crossfade increment
        crossfadeIncrement_ = crossfadeIncrement(kCrossfadeDurationMs, sampleRate);

        prepared_ = true;
    }

    /// @brief Clear all internal state (FR-004).
    void reset() noexcept {
        // Reset filters
        preEmphasis_.reset();
        deEmphasis_.reset();
        dcBlocker_.reset();

        // Snap smoothers to current values (no ramp on next process)
        driveSmoother_.snapTo(dbToGain(driveDb_));
        saturationSmoother_.snapTo(saturation_);
        biasSmoother_.snapTo(bias_);
        mixSmoother_.snapTo(mix_);

        // Reset hysteresis state
        M_ = 0.0f;
        H_prev_ = 0.0f;

        // Reset crossfade state
        crossfadeActive_ = false;
        crossfadePosition_ = 0.0f;
    }

    // =========================================================================
    // Model and Solver Selection (FR-007, FR-008)
    // =========================================================================

    /// @brief Set the saturation model (FR-007).
    /// @param model TapeModel::Simple or TapeModel::Hysteresis
    void setModel(TapeModel model) noexcept {
        if (model != model_ && prepared_) {
            // Trigger crossfade
            previousModel_ = model_;
            crossfadeActive_ = true;
            crossfadePosition_ = 0.0f;
        }
        model_ = model;
    }

    /// @brief Set the numerical solver for Hysteresis model (FR-008).
    /// @param solver Solver type (ignored for Simple model)
    void setSolver(HysteresisSolver solver) noexcept {
        solver_ = solver;
    }

    /// @brief Get current model (FR-013).
    [[nodiscard]] TapeModel getModel() const noexcept {
        return model_;
    }

    /// @brief Get current solver (FR-014).
    [[nodiscard]] HysteresisSolver getSolver() const noexcept {
        return solver_;
    }

    // =========================================================================
    // Parameter Setters (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set input drive in dB (FR-009).
    /// @param dB Drive in decibels, clamped to [-24, +24]
    void setDrive(float dB) noexcept {
        driveDb_ = std::clamp(dB, kMinDriveDb, kMaxDriveDb);
        if (prepared_) {
            driveSmoother_.setTarget(dbToGain(driveDb_));
        }
    }

    /// @brief Set saturation intensity (FR-010).
    /// @param amount Saturation [0, 1] - 0=linear, 1=full saturation
    void setSaturation(float amount) noexcept {
        saturation_ = std::clamp(amount, 0.0f, 1.0f);
        if (prepared_) {
            saturationSmoother_.setTarget(saturation_);
        }
    }

    /// @brief Set tape bias / asymmetry (FR-011).
    /// @param bias Bias [-1, +1] - 0=symmetric, +/-=asymmetric
    void setBias(float bias) noexcept {
        bias_ = std::clamp(bias, -1.0f, 1.0f);
        if (prepared_) {
            biasSmoother_.setTarget(bias_);
        }
    }

    /// @brief Set dry/wet mix (FR-012).
    /// @param mix Mix [0, 1] - 0=bypass, 1=100% wet
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        if (prepared_) {
            mixSmoother_.setTarget(mix_);
        }
    }

    // =========================================================================
    // Parameter Getters (FR-015 to FR-018)
    // =========================================================================

    /// @brief Get current drive in dB (FR-015).
    [[nodiscard]] float getDrive() const noexcept {
        return driveDb_;
    }

    /// @brief Get current saturation amount (FR-016).
    [[nodiscard]] float getSaturation() const noexcept {
        return saturation_;
    }

    /// @brief Get current bias value (FR-017).
    [[nodiscard]] float getBias() const noexcept {
        return bias_;
    }

    /// @brief Get current mix value (FR-018).
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Expert Mode: Jiles-Atherton Parameters (FR-030b, FR-030c)
    // =========================================================================

    /// @brief Set all J-A parameters at once (FR-030b).
    void setJAParams(float a, float alpha, float c, float k, float Ms) noexcept {
        ja_a_ = a;
        ja_alpha_ = alpha;
        ja_c_ = c;
        ja_k_ = k;
        ja_Ms_ = Ms;
    }

    /// @brief Get J-A 'a' parameter (FR-030c).
    [[nodiscard]] float getJA_a() const noexcept { return ja_a_; }

    /// @brief Get J-A 'alpha' parameter (FR-030c).
    [[nodiscard]] float getJA_alpha() const noexcept { return ja_alpha_; }

    /// @brief Get J-A 'c' parameter (FR-030c).
    [[nodiscard]] float getJA_c() const noexcept { return ja_c_; }

    /// @brief Get J-A 'k' parameter (FR-030c).
    [[nodiscard]] float getJA_k() const noexcept { return ja_k_; }

    /// @brief Get J-A 'Ms' parameter (FR-030c).
    [[nodiscard]] float getJA_Ms() const noexcept { return ja_Ms_; }

    // =========================================================================
    // Processing (FR-031 to FR-034)
    // =========================================================================

    /// @brief Process audio buffer in-place (FR-031).
    /// @param buffer Audio buffer to process
    /// @param numSamples Number of samples
    /// @note No memory allocation (FR-032)
    /// @note n=0 handled gracefully (FR-033)
    /// @note mix=0 skips processing (FR-034)
    void process(float* buffer, size_t numSamples) noexcept {
        // FR-033: Handle n=0 gracefully
        if (numSamples == 0) {
            return;
        }

        // FR-005: Return input unchanged if not prepared
        if (!prepared_) {
            return;
        }

        // FR-034: Skip processing entirely when mix=0 (instant bypass, no smoothing)
        // This ensures SC-009: mix=0.0 produces output identical to input
        if (mix_ <= 0.0f) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            const float driveGain = driveSmoother_.process();
            const float sat = saturationSmoother_.process();
            const float currentBias = biasSmoother_.process();
            const float currentMix = mixSmoother_.process();

            const float dryInput = buffer[i];
            float wetOutput = 0.0f;

            // Handle crossfade between models
            if (crossfadeActive_) {
                // Process through both models
                float oldOutput = processSampleSimple(dryInput, driveGain, sat, currentBias);
                float newOutput = oldOutput;

                if (previousModel_ == TapeModel::Simple && model_ == TapeModel::Hysteresis) {
                    newOutput = processSampleHysteresis(dryInput, driveGain, sat, currentBias);
                } else if (previousModel_ == TapeModel::Hysteresis && model_ == TapeModel::Simple) {
                    oldOutput = processSampleHysteresis(dryInput, driveGain, sat, currentBias);
                    newOutput = processSampleSimple(dryInput, driveGain, sat, currentBias);
                }

                // Equal-power crossfade
                float fadeOut, fadeIn;
                equalPowerGains(crossfadePosition_, fadeOut, fadeIn);
                wetOutput = oldOutput * fadeOut + newOutput * fadeIn;

                // Advance crossfade
                crossfadePosition_ += crossfadeIncrement_;
                if (crossfadePosition_ >= 1.0f) {
                    crossfadeActive_ = false;
                    crossfadePosition_ = 0.0f;
                }
            } else {
                // Normal processing - single model
                if (model_ == TapeModel::Simple) {
                    wetOutput = processSampleSimple(dryInput, driveGain, sat, currentBias);
                } else {
                    wetOutput = processSampleHysteresis(dryInput, driveGain, sat, currentBias);
                }
            }

            // DC blocking
            wetOutput = dcBlocker_.process(wetOutput);

            // Apply mix
            buffer[i] = dryInput * (1.0f - currentMix) + wetOutput * currentMix;
        }
    }

private:
    // =========================================================================
    // Simple Model Processing
    // =========================================================================

    /// @brief Process a single sample through the Simple model.
    [[nodiscard]] float processSampleSimple(float input, float driveGain,
                                             float sat, float bias) noexcept {
        // Apply drive gain
        float x = input * driveGain;

        // Add bias (DC offset before saturation)
        x += bias;

        // Pre-emphasis: boost high frequencies before saturation
        x = preEmphasis_.process(x);

        // Saturation: blend between linear and tanh based on saturation parameter
        // saturation=0 -> linear, saturation=1 -> full tanh
        const float linear = x;
        const float saturated = Sigmoid::tanh(x);
        x = linear * (1.0f - sat) + saturated * sat;

        // De-emphasis: cut high frequencies after saturation
        x = deEmphasis_.process(x);

        return x;
    }

    // =========================================================================
    // Hysteresis Model Processing
    // =========================================================================

    /// @brief Langevin function L(x) = coth(x) - 1/x
    /// Uses Taylor series for small x to avoid numerical issues
    [[nodiscard]] float langevin(float x) const noexcept {
        const float absX = std::abs(x);
        if (absX < 0.001f) {
            // Taylor series: L(x) ~ x/3 - x^3/45 + 2x^5/945
            return x / 3.0f;
        }
        // L(x) = coth(x) - 1/x
        const float cothX = 1.0f / std::tanh(x);
        return cothX - 1.0f / x;
    }

    /// @brief Derivative of Langevin function L'(x)
    [[nodiscard]] float langevinDerivative(float x) const noexcept {
        const float absX = std::abs(x);
        if (absX < 0.001f) {
            // Taylor series: L'(x) ~ 1/3 - x^2/15
            return 1.0f / 3.0f;
        }
        // L'(x) = 1/x^2 - csch^2(x)
        const float sinhX = std::sinh(x);
        const float cschSq = 1.0f / (sinhX * sinhX);
        return 1.0f / (x * x) - cschSq;
    }

    /// @brief Jiles-Atherton dM/dH differential equation
    [[nodiscard]] float jaDerivative(float H, float M, float dH) const noexcept {
        // Effective field: He = H + alpha*M
        const float He = H + ja_alpha_ * M;

        // Anhysteretic magnetization: Man = Ms * L(He/a)
        const float Man = ja_Ms_ * langevin(He / ja_a_);

        // Sign of dH for irreversible component direction
        const float delta = (dH >= 0.0f) ? 1.0f : -1.0f;

        // Denominator for dM/dH
        const float denom = 1.0f - ja_c_ * ja_alpha_ * ja_Ms_ * langevinDerivative(He / ja_a_) / ja_a_;

        // Irreversible component
        const float Mirr = (Man - M) / (delta * ja_k_ - ja_alpha_ * (Man - M));

        // Reversible component
        const float Mrev = ja_c_ * (Man - M);

        // dM/dH
        return (Mirr + Mrev) / denom;
    }

    /// @brief Process a single sample through the Hysteresis model.
    [[nodiscard]] float processSampleHysteresis(float input, float driveGain,
                                                 float sat, float bias) noexcept {
        // Apply drive gain and bias
        float x = input * driveGain + bias;

        // Scale input to magnetic field H
        // The scaling factor maps audio signal range to magnetic field range
        const float H = x * 1000.0f;  // Scale factor for reasonable H values

        // Calculate dH with T-scaling for sample rate independence
        const float dH = (H - H_prev_) * TScale_;

        // Solve using selected solver
        float dM = 0.0f;
        switch (solver_) {
            case HysteresisSolver::RK2:
                dM = solveRK2(H, dH);
                break;
            case HysteresisSolver::RK4:
                dM = solveRK4(H, dH);
                break;
            case HysteresisSolver::NR4:
                dM = solveNR(H, dH, 4);
                break;
            case HysteresisSolver::NR8:
                dM = solveNR(H, dH, 8);
                break;
        }

        // Update magnetization state
        M_ += dM;

        // Clamp M to prevent runaway (saturation limit)
        const float Ms_scaled = ja_Ms_ * sat;
        M_ = std::clamp(M_, -Ms_scaled, Ms_scaled);

        // Store previous H
        H_prev_ = H;

        // Output is normalized magnetization
        const float output = M_ / ja_Ms_;

        return output;
    }

    /// @brief RK2 (Heun's method) solver
    [[nodiscard]] float solveRK2(float H, float dH) const noexcept {
        // k1 = f(H, M)
        const float k1 = jaDerivative(H, M_, dH) * dH;

        // k2 = f(H + dH, M + k1)
        const float k2 = jaDerivative(H + dH, M_ + k1, dH) * dH;

        // dM = (k1 + k2) / 2
        return (k1 + k2) * 0.5f;
    }

    /// @brief RK4 (4th order Runge-Kutta) solver
    [[nodiscard]] float solveRK4(float H, float dH) const noexcept {
        const float halfDH = dH * 0.5f;

        // k1 = f(H, M) * dH
        const float k1 = jaDerivative(H, M_, dH) * dH;

        // k2 = f(H + dH/2, M + k1/2) * dH
        const float k2 = jaDerivative(H + halfDH, M_ + k1 * 0.5f, dH) * dH;

        // k3 = f(H + dH/2, M + k2/2) * dH
        const float k3 = jaDerivative(H + halfDH, M_ + k2 * 0.5f, dH) * dH;

        // k4 = f(H + dH, M + k3) * dH
        const float k4 = jaDerivative(H + dH, M_ + k3, dH) * dH;

        // dM = (k1 + 2*k2 + 2*k3 + k4) / 6
        return (k1 + 2.0f * k2 + 2.0f * k3 + k4) / 6.0f;
    }

    /// @brief Newton-Raphson solver with configurable iterations
    [[nodiscard]] float solveNR(float H, float dH, int iterations) const noexcept {
        // Start with explicit Euler estimate
        float M_new = M_ + jaDerivative(H, M_, dH) * dH;

        // Newton-Raphson iterations
        for (int i = 0; i < iterations; ++i) {
            // f(M_new) = M_new - M - dM/dH * dH
            const float dMdH = jaDerivative(H + dH, M_new, dH);
            const float f = M_new - M_ - dMdH * dH;

            // f'(M_new) ~ 1 (simplified, ignoring derivative of dM/dH w.r.t. M)
            // For a more accurate implementation, compute the Jacobian

            // Update
            M_new -= f * 0.5f;  // Damped Newton step

            // Clamp to prevent divergence
            M_new = std::clamp(M_new, -ja_Ms_, ja_Ms_);
        }

        return M_new - M_;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Parameters
    TapeModel model_;
    HysteresisSolver solver_;
    float driveDb_;
    float saturation_;
    float bias_;
    float mix_;

    // Jiles-Atherton parameters
    float ja_a_;
    float ja_alpha_;
    float ja_c_;
    float ja_k_;
    float ja_Ms_;

    // Configuration
    double sampleRate_;
    bool prepared_;

    // Hysteresis state
    float M_;       ///< Current magnetization
    float H_prev_;  ///< Previous magnetic field value
    float TScale_;  ///< Time scaling for sample rate independence

    // Crossfade state
    bool crossfadeActive_;
    float crossfadePosition_;
    float crossfadeIncrement_;
    TapeModel previousModel_;

    // Components
    Biquad preEmphasis_;
    Biquad deEmphasis_;
    DCBlocker dcBlocker_;
    OnePoleSmoother driveSmoother_;
    OnePoleSmoother saturationSmoother_;
    OnePoleSmoother biasSmoother_;
    OnePoleSmoother mixSmoother_;
};

} // namespace DSP
} // namespace Krate
