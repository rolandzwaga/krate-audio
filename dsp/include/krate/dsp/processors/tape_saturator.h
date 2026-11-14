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
// - Sample rate independence: the J-A equation is integrated along the field path
//
// Hysteresis model (repaired 2026-11-10, specs/disrumpo-audit/follow-ups.md 79 / 82 / 88):
//   x = input * drive + bias,  H = x * kHysteresisFieldScale  (x = 1 is He = 100, ~4.5a at the default a)
//   m = M / Ms_eff with Ms_eff = Saturation * Ms (FR-029), He = H + alpha Ms_eff m, Q = He / a
//   dm/dH = [ dM (1-c) (L(Q) - m) / ((1-c) d k - alpha Ms_eff (L(Q) - m)) + c L'(Q) / a ]
//           / (1 - c alpha Ms_eff L'(Q) / a)
//   L = Langevin, d = sign(dH), dM = 1 when (L(Q) - m) has the sign of d, else 0 (the
//   standard reversible term c dMan/dH and delta-M gating; DAFx 2019 / ChowTape v1 values).
//   The input is piecewise linear in time, so dM/dt = (dm/dH) dH/dt integrated over one
//   sample equals dm/dH integrated over the field step H[n-1] -> H[n]: the solvers step
//   the field (substeps of at most 2 min(a, k)), which makes the model rate-independent.
//   RK2 = midpoint, RK4 = classic, NR4 / NR8 = Newton on the trapezoidal step with the
//   analytic Jacobian (ChowTape's NR solver). A step that would need more than
//   kHysteresisMaxSubsteps substeps runs L-stable substeps instead (exact reversible
//   increment, backward Euler on the gated relaxation), so no input can make it diverge.
//   Output = kHysteresisOutputGain * Saturation * m: bounded by the Langevin (|m| < 1)
//   instead of a clamp, so Saturation 1 is stable.
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


namespace Krate::DSP {

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

    // Jiles-Atherton default parameters (DAFx 2019 / ChowTape v1: c = 1.7e-1; the 1.7
    // this header carried before 2026-11-10 was a typo that flipped the irreversible term)
    static constexpr float kDefaultJA_a = 22.0f;
    static constexpr float kDefaultJA_alpha = 1.6e-11f;
    static constexpr float kDefaultJA_c = 0.17f;
    static constexpr float kDefaultJA_k = 27.0f;
    static constexpr float kDefaultJA_Ms = 350000.0f;

    // Hysteresis model scaling and integration limits (see the file header)
    static constexpr float kHysteresisFieldScale = 100.0f; ///< field per unit of driven input
    static constexpr float kHysteresisOutputGain = 1.0f;   ///< output = gain x Saturation x M / Ms_eff
    static constexpr int kHysteresisMaxSubsteps = 8;       ///< above this, L-stable substeps

    // =========================================================================
    // Lifecycle (FR-003 to FR-006)
    // =========================================================================

    /// @brief Default constructor with safe defaults (FR-006).
    TapeSaturator() noexcept = default;

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
        m_ = 0.0f;
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
    /// a and k are clamped to >= 1e-3, alpha and Ms to >= 0, c (the reversible fraction)
    /// to [0, 1].
    void setJAParams(float a, float alpha, float c, float k, float Ms) noexcept {
        ja_a_ = std::max(a, kMinJAScale);
        ja_alpha_ = std::max(alpha, 0.0f);
        ja_c_ = std::clamp(c, 0.0f, 1.0f);
        ja_k_ = std::max(k, kMinJAScale);
        ja_Ms_ = std::max(Ms, 0.0f);
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
                float fadeOut;
                float fadeIn;
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

    /// @brief Langevin function L, its first and second derivatives at Q.
    struct LangevinValues {
        float L;    ///< L(Q) = coth(Q) - 1/Q
        float Lp;   ///< L'(Q) = 1/Q^2 - csch^2(Q)
        float Lpp;  ///< L''(Q) = 2 coth(Q) csch^2(Q) - 2/Q^3
    };

    /// @brief Evaluate L, L', L'' at Q. A series below |Q| = 0.5 (coth - 1/Q cancels in
    /// float there); above it one exp(-2|Q|), which only underflows towards zero.
    [[nodiscard]] static LangevinValues langevinAll(float q) noexcept {
        const float aq = std::abs(q);
        if (aq < 0.5f) {
            const float q2 = q * q;
            // L  = Q/3 - Q^3/45 + 2Q^5/945 - Q^7/4725 + 2Q^9/93555
            // L' = 1/3 - Q^2/15 + 2Q^4/189 - Q^6/675 + 2Q^8/10395
            // L''= -2Q/15 + 8Q^3/189 - 2Q^5/225 + 16Q^7/10395
            const float L = q * (1.0f / 3.0f + q2 * (-1.0f / 45.0f + q2 * (2.0f / 945.0f
                              + q2 * (-1.0f / 4725.0f + q2 * (2.0f / 93555.0f)))));
            const float Lp = 1.0f / 3.0f + q2 * (-1.0f / 15.0f + q2 * (2.0f / 189.0f
                               + q2 * (-1.0f / 675.0f + q2 * (2.0f / 10395.0f))));
            const float Lpp = q * (-2.0f / 15.0f + q2 * (8.0f / 189.0f + q2 * (-2.0f / 225.0f
                                + q2 * (16.0f / 10395.0f))));
            return {.L = L, .Lp = Lp, .Lpp = Lpp};
        }
        const float e = std::exp(-2.0f * aq);            // in (0, e^-1]
        const float inv = 1.0f / (1.0f - e);             // 1 - e >= 0.63
        const float sgn = (q > 0.0f) ? 1.0f : -1.0f;
        const float coth = sgn * (1.0f + e) * inv;
        const float csch2 = 4.0f * e * inv * inv;
        const float invQ = 1.0f / q;
        const float invQ2 = invQ * invQ;
        return {.L = coth - invQ, .Lp = invQ2 - csch2, .Lpp = 2.0f * coth * csch2 - 2.0f * invQ2 * invQ};
    }

    /// @brief Per-sample constants of the J-A slope.
    struct JAStepContext {
        float d;      ///< direction of the field step (+1 / -1)
        float aMs;    ///< alpha * Ms_eff (the mean-field coupling)
        float invA;   ///< 1 / a
        float nc;     ///< 1 - c
        float ncK;    ///< (1 - c) k
        float cInvA;  ///< c / a
    };

    /// @brief Jiles-Atherton slope dm/dH (m = M / Ms_eff) and, for the NR solvers, its
    /// derivative with respect to m.
    /// @param H Field
    /// @param m Normalised magnetisation
    /// @param s Per-sample constants
    /// @param slopePrime If not null, receives d(dm/dH)/dm
    [[nodiscard]] static float jaSlope(float H, float m, const JAStepContext& s,
                                       float* slopePrime = nullptr) noexcept {
        const float q = (H + s.aMs * m) * s.invA;
        const auto lv = langevinAll(q);
        const float u = lv.L - m;                        // (Man - M) / Ms_eff
        const bool gate = (u * s.d) > 0.0f;              // delta-M: Man - M along dH
        // Irreversible: nc u / (nc d k - aMs u); with u d > 0 its denominator is
        // d (nc k - aMs |u|), kept above zero (alpha large enough to flip it is unphysical)
        const float irrDen = std::max(s.ncK - s.aMs * std::abs(u), kMinIrreversibleDenominator);
        const float irr = gate ? s.nc * u * s.d / irrDen : 0.0f;
        const float rev = s.cInvA * lv.Lp;
        const float den = std::max(1.0f - s.aMs * s.cInvA * lv.Lp, kMinSlopeDenominator);
        const float slope = (irr + rev) / den;
        if (slopePrime != nullptr) {
            const float beta = s.aMs * s.invA;           // dQ/dm
            const float du = beta * lv.Lp - 1.0f;
            const float dIrr = gate ? s.nc * du * s.d * s.ncK / (irrDen * irrDen) : 0.0f;
            const float dRev = s.cInvA * lv.Lpp * beta;
            const float dDen = -s.aMs * s.cInvA * lv.Lpp * beta;
            *slopePrime = ((dIrr + dRev) * den - (irr + rev) * dDen) / (den * den);
        }
        return slope;
    }

    /// @brief Process a single sample through the Hysteresis model.
    [[nodiscard]] float processSampleHysteresis(float input, float driveGain,
                                                 float sat, float bias) noexcept {
        // Driven input to field (FR-030: bias offsets the field before the hysteresis)
        const float x = input * driveGain + bias;
        const float H = x * kHysteresisFieldScale;

        if (detail::isFinite(H)) {
            const float dH = H - H_prev_;
            if (dH != 0.0f) {
                const float nc = 1.0f - ja_c_;
                const float invA = 1.0f / ja_a_;
                const JAStepContext s{.d = (dH > 0.0f) ? 1.0f : -1.0f,
                                      .aMs = ja_alpha_ * ja_Ms_ * sat,  // Ms_eff = Saturation * Ms (FR-029)
                                      .invA = invA,
                                      .nc = nc,
                                      .ncK = nc * ja_k_,
                                      .cInvA = ja_c_ * invA};
                const float need = std::abs(dH) / (kHysteresisSubstepScale * std::min(ja_a_, ja_k_));
                const bool stiff = need > static_cast<float>(kHysteresisMaxSubsteps);
                const int n = stiff ? kHysteresisMaxSubsteps
                                    : std::max(1, static_cast<int>(std::ceil(need)));
                const float h = dH / static_cast<float>(n);
                for (int i = 0; i < n; ++i) {
                    const float H0 = H_prev_ + static_cast<float>(i) * h;
                    m_ = stiff ? stepStiff(H0, h, s) : stepSolver(H0, h, s);
                }
                if (!detail::isFinite(m_)) {
                    m_ = 0.0f;
                }
            }
            H_prev_ = H;
        }

        return kHysteresisOutputGain * sat * m_;
    }

    /// @brief One substep H0 -> H0 + h with the selected solver.
    [[nodiscard]] float stepSolver(float H0, float h, const JAStepContext& s) const noexcept {
        switch (solver_) {
            case HysteresisSolver::RK2:
                return solveRK2(H0, h, s);
            case HysteresisSolver::NR4:
                return solveNR(H0, h, s, 4);
            case HysteresisSolver::NR8:
                return solveNR(H0, h, s, 8);
            case HysteresisSolver::RK4:
            default:
                return solveRK4(H0, h, s);
        }
    }

    /// @brief RK2 (midpoint) substep along the field.
    [[nodiscard]] float solveRK2(float H0, float h, const JAStepContext& s) const noexcept {
        const float k1 = h * jaSlope(H0, m_, s);
        const float k2 = h * jaSlope(H0 + 0.5f * h, m_ + 0.5f * k1, s);
        return m_ + k2;
    }

    /// @brief RK4 (classic) substep along the field.
    [[nodiscard]] float solveRK4(float H0, float h, const JAStepContext& s) const noexcept {
        const float halfH = 0.5f * h;
        const float k1 = h * jaSlope(H0, m_, s);
        const float k2 = h * jaSlope(H0 + halfH, m_ + 0.5f * k1, s);
        const float k3 = h * jaSlope(H0 + halfH, m_ + 0.5f * k2, s);
        const float k4 = h * jaSlope(H0 + h, m_ + k3, s);
        return m_ + (k1 + 2.0f * k2 + 2.0f * k3 + k4) * (1.0f / 6.0f);
    }

    /// @brief Newton-Raphson on the trapezoidal substep
    /// m1 - m0 - h/2 (f(H0, m0) + f(H1, m1)) = 0, explicit Euler as the first guess.
    [[nodiscard]] float solveNR(float H0, float h, const JAStepContext& s, int iterations) const noexcept {
        const float H1 = H0 + h;
        const float halfH = 0.5f * h;
        const float f0 = jaSlope(H0, m_, s);
        float m1 = m_ + h * f0;
        for (int i = 0; i < iterations; ++i) {
            float fPrime = 0.0f;
            const float f1 = jaSlope(H1, m1, s, &fPrime);
            const float residual = m1 - m_ - halfH * (f0 + f1);
            const float jacobian = 1.0f - halfH * fPrime;
            m1 -= residual / (std::abs(jacobian) > kMinSlopeDenominator ? jacobian : 1.0f);
        }
        return m1;
    }

    /// @brief L-stable substep for steps too large for the explicit / trapezoidal
    /// solvers: the reversible increment c (L(Q1) - L(Q0)) exactly, backward Euler on
    /// the gated irreversible relaxation towards L(Q1). Never overshoots L(Q1).
    [[nodiscard]] float stepStiff(float H0, float h, const JAStepContext& s) const noexcept {
        const float L0 = langevinAll((H0 + s.aMs * m_) * s.invA).L;
        const auto lv1 = langevinAll((H0 + h + s.aMs * m_) * s.invA);
        const float den = std::max(1.0f - s.aMs * s.cInvA * lv1.Lp, kMinSlopeDenominator);
        const float mRev = m_ + ja_c_ * (lv1.L - L0) / den;
        const float u = lv1.L - mRev;
        if (u * s.d <= 0.0f) {
            return mRev;
        }
        const float irrDen = std::max(s.ncK - s.aMs * std::abs(u), kMinIrreversibleDenominator);
        const float r = std::abs(h) * s.nc / irrDen;
        return (mRev + r * lv1.L) / (1.0f + r);
    }

    static constexpr float kMinJAScale = 1e-3f;                   ///< floor of a and k
    static constexpr float kMinIrreversibleDenominator = 1e-6f;   ///< floor of (1-c) k - alpha Ms_eff |u|
    static constexpr float kMinSlopeDenominator = 0.1f;           ///< floor of 1 - c alpha Ms_eff L' / a
    static constexpr float kHysteresisSubstepScale = 2.0f;        ///< substep <= this x min(a, k)

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Parameters
    TapeModel model_{TapeModel::Simple};
    HysteresisSolver solver_{HysteresisSolver::RK4};
    float driveDb_{0.0f};
    float saturation_{0.5f};
    float bias_{0.0f};
    float mix_{1.0f};

    // Jiles-Atherton parameters
    float ja_a_{kDefaultJA_a};
    float ja_alpha_{kDefaultJA_alpha};
    float ja_c_{kDefaultJA_c};
    float ja_k_{kDefaultJA_k};
    float ja_Ms_{kDefaultJA_Ms};

    // Configuration
    double sampleRate_{0.0};
    bool prepared_{false};

    // Hysteresis state
    float m_{0.0f};       ///< Magnetisation normalised to Ms_eff (M / (Saturation x Ms))
    float H_prev_{0.0f};  ///< Previous magnetic field value

    // Crossfade state
    bool crossfadeActive_{false};
    float crossfadePosition_{0.0f};
    float crossfadeIncrement_{0.0f};
    TapeModel previousModel_{TapeModel::Simple};

    // Components
    Biquad preEmphasis_;
    Biquad deEmphasis_;
    DCBlocker dcBlocker_;
    OnePoleSmoother driveSmoother_;
    OnePoleSmoother saturationSmoother_;
    OnePoleSmoother biasSmoother_;
    OnePoleSmoother mixSmoother_;
};

} // namespace Krate::DSP

