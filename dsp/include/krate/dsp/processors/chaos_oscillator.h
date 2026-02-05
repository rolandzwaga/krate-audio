// ==============================================================================
// Layer 2: DSP Processor - Chaos Attractor Oscillator
// ==============================================================================
// Audio-rate chaos oscillator implementing 5 attractor types with RK4
// adaptive substepping for numerical stability.
//
// Feature: 026-chaos-attractor-oscillator
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: fast_math.h (fastTanh), db_utils.h (isNaN, isInf, flushDenormal)
//   - Layer 1: dc_blocker.h (DCBlocker)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking, tanh normalization)
// - Principle XI: Performance Budget (< 1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/026-chaos-attractor-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Available chaos attractor models for audio-rate oscillation.
///
/// Each attractor has distinct mathematical character and timbral qualities:
/// - Lorenz: Smooth, flowing, three-lobe butterfly pattern
/// - Rossler: Asymmetric, single spiral, buzzy
/// - Chua: Harsh double-scroll with abrupt transitions
/// - Duffing: Driven nonlinear, harmonically rich
/// - VanDerPol: Relaxation oscillations, pulse-like
///
/// @note Distinct from ChaosModel enum (which includes Henon and excludes Duffing/VanDerPol)
enum class ChaosAttractor : uint8_t {
    Lorenz = 0,    ///< Lorenz attractor (sigma=10, rho=28, beta=8/3)
    Rossler = 1,   ///< Rossler attractor (a=0.2, b=0.2, c=5.7)
    Chua = 2,      ///< Chua circuit (alpha=15.6, beta=28, m0=-1.143, m1=-0.714)
    Duffing = 3,   ///< Duffing oscillator (gamma=0.1, A=0.35, omega=1.4)
    VanDerPol = 4  ///< Van der Pol oscillator (mu=1.0)
};

/// @brief Number of attractor types.
inline constexpr size_t kNumChaosAttractors = 5;

// =============================================================================
// Internal Structures
// =============================================================================

namespace detail {

/// @brief Internal state variables for attractor dynamics.
///
/// For 3D attractors (Lorenz, Rossler, Chua):
///   - x, y, z represent the three state variables
///
/// For 2D oscillators (Duffing, VanDerPol):
///   - x represents position
///   - y represents velocity (v)
///   - z is unused (kept at 0.0f)
struct AttractorState {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/// @brief Per-attractor configuration constants.
///
/// These values are empirically tuned for audio-rate operation and are
/// stored in a constexpr array indexed by ChaosAttractor enum value.
struct AttractorConstants {
    // Numerical integration
    float dtMax;              ///< Maximum stable dt per RK4 substep
    float baseDt;             ///< Base dt for frequency scaling
    float referenceFrequency; ///< Reference frequency for dt scaling

    // Safety
    float safeBound;          ///< State bound for divergence detection

    // Normalization
    float xScale;             ///< Normalization divisor for x axis
    float yScale;             ///< Normalization divisor for y axis
    float zScale;             ///< Normalization divisor for z axis

    // Chaos parameter mapping
    float chaosMin;           ///< Minimum chaos parameter value
    float chaosMax;           ///< Maximum chaos parameter value
    float chaosDefault;       ///< Default chaos parameter value

    // Initial conditions
    AttractorState initialState; ///< Reset state for this attractor
};

/// @brief Constexpr array of per-attractor constants indexed by ChaosAttractor enum.
///
/// NOTE: The baseDt values have been scaled up by 100x from the spec's original
/// values to achieve audible output. The spec's values (0.01, 0.05, 0.02) were
/// empirically too small when divided by sampleRate, resulting in near-zero
/// attractor evolution per sample. The corrected values (1.0, 5.0, 2.0) produce
/// meaningful audio-rate chaotic output with approximate pitch tracking.
inline constexpr AttractorConstants kAttractorConstants[kNumChaosAttractors] = {
    // Lorenz (FR-001)
    {
        .dtMax = 0.001f,
        .baseDt = 1.0f,  // Scaled 100x from spec (0.01) for audible output
        .referenceFrequency = 100.0f,
        .safeBound = 500.0f,
        .xScale = 20.0f,
        .yScale = 20.0f,
        .zScale = 30.0f,
        .chaosMin = 20.0f,
        .chaosMax = 28.0f,
        .chaosDefault = 28.0f,
        .initialState = {1.0f, 1.0f, 1.0f}
    },
    // Rossler (FR-002)
    {
        .dtMax = 0.002f,
        .baseDt = 5.0f,  // Scaled 100x from spec (0.05) for audible output
        .referenceFrequency = 80.0f,
        .safeBound = 300.0f,
        .xScale = 12.0f,
        .yScale = 12.0f,
        .zScale = 20.0f,
        .chaosMin = 4.0f,
        .chaosMax = 8.0f,
        .chaosDefault = 5.7f,
        .initialState = {0.1f, 0.0f, 0.0f}
    },
    // Chua (FR-003)
    {
        .dtMax = 0.0005f,
        .baseDt = 2.0f,  // Scaled 100x from spec (0.02) for audible output
        .referenceFrequency = 120.0f,
        .safeBound = 50.0f,
        .xScale = 2.5f,
        .yScale = 1.5f,
        .zScale = 1.5f,
        .chaosMin = 12.0f,
        .chaosMax = 18.0f,
        .chaosDefault = 15.6f,
        .initialState = {0.7f, 0.0f, 0.0f}
    },
    // Duffing (FR-004)
    {
        .dtMax = 0.001f,
        .baseDt = 1.4f,
        .referenceFrequency = 1.0f,
        .safeBound = 10.0f,
        .xScale = 2.0f,
        .yScale = 2.0f,
        .zScale = 1.0f,  // N/A for 2D, but set to 1.0 to avoid division by zero
        .chaosMin = 0.2f,
        .chaosMax = 0.5f,
        .chaosDefault = 0.35f,
        .initialState = {0.5f, 0.0f, 0.0f}
    },
    // VanDerPol (FR-005)
    {
        .dtMax = 0.001f,
        .baseDt = 1.0f,
        .referenceFrequency = 1.0f,
        .safeBound = 10.0f,
        .xScale = 2.5f,
        .yScale = 3.0f,
        .zScale = 1.0f,  // N/A for 2D
        .chaosMin = 0.5f,
        .chaosMax = 5.0f,
        .chaosDefault = 1.0f,
        .initialState = {0.5f, 0.0f, 0.0f}
    }
};

}  // namespace detail

// =============================================================================
// ChaosOscillator Class
// =============================================================================

/// @brief Audio-rate chaos oscillator implementing 5 attractor types.
///
/// Generates complex, evolving waveforms by numerically integrating
/// chaotic attractor systems at audio rate using RK4 with adaptive
/// substepping for numerical stability.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: Layer 0 (fast_math.h, db_utils.h, math_constants.h),
///                    Layer 1 (dc_blocker.h)
///
/// @par Memory Model
/// All state is pre-allocated. No heap allocation during processing.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (prepares DC blocker)
/// - All other methods: Real-time safe (noexcept, no allocations)
///
/// @par Usage Example
/// @code
/// ChaosOscillator osc;
/// osc.prepare(44100.0);
/// osc.setAttractor(ChaosAttractor::Lorenz);
/// osc.setFrequency(220.0f);
/// osc.setChaos(1.0f);
///
/// // Process single sample
/// float sample = osc.process();
///
/// // Process block
/// float buffer[512];
/// osc.processBlock(buffer, 512, nullptr);
/// @endcode
class ChaosOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSubsteps = 100;
    static constexpr size_t kResetCooldownSamples = 100;
    static constexpr float kMinFrequency = 0.1f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr float kDefaultDCBlockerCutoff = 10.0f;

    // =========================================================================
    // Lifecycle (FR-015, FR-016)
    // =========================================================================

    /// @brief Default constructor.
    ChaosOscillator() noexcept = default;

    /// @brief Destructor.
    ~ChaosOscillator() noexcept = default;

    // Non-copyable (contains DCBlocker state)
    ChaosOscillator(const ChaosOscillator&) = delete;
    ChaosOscillator& operator=(const ChaosOscillator&) = delete;
    ChaosOscillator(ChaosOscillator&&) noexcept = default;
    ChaosOscillator& operator=(ChaosOscillator&&) noexcept = default;

    /// @brief Configure the oscillator for processing.
    ///
    /// Prepares the DC blocker and loads default attractor constants.
    /// Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz (minimum 1000)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(sampleRate, 1000.0);
        dcBlocker_.prepare(sampleRate_, kDefaultDCBlockerCutoff);
        updateConstants();
        resetState();
        prepared_ = true;
    }

    /// @brief Reset the oscillator state.
    ///
    /// Reinitializes attractor state to initial conditions.
    /// DC blocker state is also reset.
    void reset() noexcept {
        resetState();
        dcBlocker_.reset();
    }

    // =========================================================================
    // Parameter Setters (FR-017 to FR-021)
    // =========================================================================

    /// @brief Set the attractor type.
    ///
    /// Changes the chaos attractor model. Also resets state to the new
    /// attractor's initial conditions and updates all derived constants.
    ///
    /// @param type Attractor type to use
    void setAttractor(ChaosAttractor type) noexcept {
        if (type != attractor_) {
            attractor_ = type;
            updateConstants();
            resetState();
        }
    }

    /// @brief Set the target frequency.
    ///
    /// Adjusts the integration timestep to achieve approximate pitch tracking.
    /// Due to the chaotic nature, actual perceived pitch will vary.
    ///
    /// @param hz Target frequency in Hz (clamped to [0.1, 20000])
    void setFrequency(float hz) noexcept {
        frequency_ = std::clamp(hz, kMinFrequency, kMaxFrequency);
        updateDt();
    }

    /// @brief Set the chaos amount (normalized).
    ///
    /// Maps [0, 1] to the per-attractor chaos parameter range.
    /// Higher values produce more chaotic behavior.
    ///
    /// @param amount Chaos amount [0, 1]
    void setChaos(float amount) noexcept {
        chaosNormalized_ = std::clamp(amount, 0.0f, 1.0f);
        updateChaosParameter();
    }

    /// @brief Set the external coupling amount.
    ///
    /// Controls how much external input affects the attractor dynamics.
    /// Applied as additive forcing to the x-derivative.
    ///
    /// @param amount Coupling strength [0, 1]
    void setCoupling(float amount) noexcept {
        coupling_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Set the output axis.
    ///
    /// Selects which state variable to output (x, y, or z).
    ///
    /// @param axis Output axis (0=x, 1=y, 2=z, clamped)
    void setOutput(size_t axis) noexcept {
        outputAxis_ = std::min(axis, static_cast<size_t>(2));
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current attractor type.
    [[nodiscard]] ChaosAttractor getAttractor() const noexcept {
        return attractor_;
    }

    /// @brief Get the target frequency.
    [[nodiscard]] float getFrequency() const noexcept {
        return frequency_;
    }

    /// @brief Get the normalized chaos amount.
    [[nodiscard]] float getChaos() const noexcept {
        return chaosNormalized_;
    }

    /// @brief Get the coupling amount.
    [[nodiscard]] float getCoupling() const noexcept {
        return coupling_;
    }

    /// @brief Get the output axis.
    [[nodiscard]] size_t getOutput() const noexcept {
        return outputAxis_;
    }

    /// @brief Check if the oscillator is prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Processing (FR-022, FR-023)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Integrates the attractor state by one audio sample period,
    /// applies normalization and DC blocking.
    ///
    /// @param externalInput External input for coupling (default 0)
    /// @return Output sample in [-1, +1] range
    [[nodiscard]] float process(float externalInput = 0.0f) noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // FR-014: Sanitize external input
        float sanitizedInput = sanitizeInput(externalInput);

        // FR-006: Integrate one step with adaptive substepping
        integrateOneStep(sanitizedInput);

        // FR-011, FR-012, FR-013: Check for divergence and reset if needed
        if (checkDivergence()) {
            if (resetCooldown_ == 0) {
                resetState();
                resetCooldown_ = kResetCooldownSamples;
            }
        }

        // Decrement cooldown
        if (resetCooldown_ > 0) {
            --resetCooldown_;
        }

        // FR-010: Select output axis
        float axisValue = getAxisValue();

        // FR-008: Normalize output
        float normalized = normalizeOutput(axisValue);

        // FR-009: Apply DC blocking
        return dcBlocker_.process(normalized);
    }

    /// @brief Process a block of samples.
    ///
    /// @param output Output buffer (must be at least numSamples long)
    /// @param numSamples Number of samples to process
    /// @param extInput External input buffer (optional, can be nullptr)
    void processBlock(float* output, size_t numSamples,
                      const float* extInput = nullptr) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            float ext = (extInput != nullptr) ? extInput[i] : 0.0f;
            output[i] = process(ext);
        }
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update all constants from the current attractor type.
    void updateConstants() noexcept {
        const auto& constants = detail::kAttractorConstants[static_cast<size_t>(attractor_)];
        dtMax_ = constants.dtMax;
        safeBound_ = constants.safeBound;
        xScale_ = constants.xScale;
        yScale_ = constants.yScale;
        zScale_ = constants.zScale;
        updateDt();
        updateChaosParameter();
    }

    /// @brief Update the integration timestep from frequency.
    void updateDt() noexcept {
        const auto& constants = detail::kAttractorConstants[static_cast<size_t>(attractor_)];
        // FR-007: dt = baseDt * (targetFreq / refFreq) / sampleRate
        dt_ = constants.baseDt * (frequency_ / constants.referenceFrequency)
              / static_cast<float>(sampleRate_);
    }

    /// @brief Update the chaos parameter from normalized value.
    void updateChaosParameter() noexcept {
        const auto& constants = detail::kAttractorConstants[static_cast<size_t>(attractor_)];
        // FR-019: Map [0, 1] to [chaosMin, chaosMax]
        chaosParameter_ = constants.chaosMin +
                          chaosNormalized_ * (constants.chaosMax - constants.chaosMin);
    }

    /// @brief Reset attractor state to initial conditions.
    void resetState() noexcept {
        const auto& constants = detail::kAttractorConstants[static_cast<size_t>(attractor_)];
        state_ = constants.initialState;
        duffingPhase_ = 0.0f;
        resetCooldown_ = 0;
    }

    /// @brief Integrate the attractor by one audio sample.
    ///
    /// Uses adaptive substepping to maintain numerical stability.
    ///
    /// @param externalInput Sanitized external input for coupling
    void integrateOneStep(float externalInput) noexcept {
        // FR-006: Adaptive substepping
        int numSubsteps = static_cast<int>(std::ceil(dt_ / dtMax_));
        numSubsteps = std::clamp(numSubsteps, 1, static_cast<int>(kMaxSubsteps));

        float dtSubstep = dt_ / static_cast<float>(numSubsteps);
        float couplingForce = coupling_ * externalInput;

        for (int i = 0; i < numSubsteps; ++i) {
            rk4Step(dtSubstep, couplingForce);
        }
    }

    /// @brief Perform a single RK4 integration step.
    ///
    /// @param dt Timestep for this substep
    /// @param couplingForce Pre-computed coupling force (coupling * extInput)
    void rk4Step(float dt, float couplingForce) noexcept {
        // Standard RK4
        auto k1 = computeDerivatives(state_, couplingForce);

        detail::AttractorState s2;
        s2.x = state_.x + dt * k1.x * 0.5f;
        s2.y = state_.y + dt * k1.y * 0.5f;
        s2.z = state_.z + dt * k1.z * 0.5f;
        auto k2 = computeDerivatives(s2, couplingForce);

        detail::AttractorState s3;
        s3.x = state_.x + dt * k2.x * 0.5f;
        s3.y = state_.y + dt * k2.y * 0.5f;
        s3.z = state_.z + dt * k2.z * 0.5f;
        auto k3 = computeDerivatives(s3, couplingForce);

        detail::AttractorState s4;
        s4.x = state_.x + dt * k3.x;
        s4.y = state_.y + dt * k3.y;
        s4.z = state_.z + dt * k3.z;
        auto k4 = computeDerivatives(s4, couplingForce);

        // Update state
        state_.x += dt * (k1.x + 2.0f * k2.x + 2.0f * k3.x + k4.x) / 6.0f;
        state_.y += dt * (k1.y + 2.0f * k2.y + 2.0f * k3.y + k4.y) / 6.0f;
        state_.z += dt * (k1.z + 2.0f * k2.z + 2.0f * k3.z + k4.z) / 6.0f;

        // Flush denormals
        state_.x = detail::flushDenormal(state_.x);
        state_.y = detail::flushDenormal(state_.y);
        state_.z = detail::flushDenormal(state_.z);

        // FR-004: Advance Duffing phase in attractor time
        if (attractor_ == ChaosAttractor::Duffing) {
            constexpr float omega = 1.4f;
            duffingPhase_ += omega * dt;
            // Wrap phase to prevent overflow
            if (duffingPhase_ > kTwoPi * 1000.0f) {
                duffingPhase_ = std::fmod(duffingPhase_, kTwoPi);
            }
        }
    }

    /// @brief Compute derivatives for the current attractor type.
    ///
    /// Dispatches to the appropriate derivative computation based on attractor_.
    ///
    /// @param s Current state
    /// @param couplingForce Coupling force to add to dx/dt
    /// @return Derivatives (dx/dt, dy/dt, dz/dt)
    [[nodiscard]] detail::AttractorState computeDerivatives(
        const detail::AttractorState& s, float couplingForce) const noexcept {
        switch (attractor_) {
            case ChaosAttractor::Lorenz:
                return computeLorenzDerivatives(s, couplingForce);
            case ChaosAttractor::Rossler:
                return computeRosslerDerivatives(s, couplingForce);
            case ChaosAttractor::Chua:
                return computeChuaDerivatives(s, couplingForce);
            case ChaosAttractor::Duffing:
                return computeDuffingDerivatives(s, couplingForce);
            case ChaosAttractor::VanDerPol:
                return computeVanDerPolDerivatives(s, couplingForce);
            default:
                return computeLorenzDerivatives(s, couplingForce);
        }
    }

    /// @brief Compute Lorenz attractor derivatives (FR-001).
    ///
    /// dx/dt = sigma * (y - x)
    /// dy/dt = x * (rho - z) - y
    /// dz/dt = x * y - beta * z
    [[nodiscard]] detail::AttractorState computeLorenzDerivatives(
        const detail::AttractorState& s, float couplingForce) const noexcept {
        constexpr float sigma = 10.0f;
        constexpr float beta = 8.0f / 3.0f;
        float rho = chaosParameter_;  // Maps to [20, 28]

        detail::AttractorState d;
        d.x = sigma * (s.y - s.x) + couplingForce;
        d.y = s.x * (rho - s.z) - s.y;
        d.z = s.x * s.y - beta * s.z;
        return d;
    }

    /// @brief Compute Rossler attractor derivatives (FR-002).
    ///
    /// dx/dt = -y - z
    /// dy/dt = x + a * y
    /// dz/dt = b + z * (x - c)
    [[nodiscard]] detail::AttractorState computeRosslerDerivatives(
        const detail::AttractorState& s, float couplingForce) const noexcept {
        constexpr float a = 0.2f;
        constexpr float b = 0.2f;
        float c = chaosParameter_;  // Maps to [4, 8]

        detail::AttractorState d;
        d.x = -s.y - s.z + couplingForce;
        d.y = s.x + a * s.y;
        d.z = b + s.z * (s.x - c);
        return d;
    }

    /// @brief Compute Chua circuit derivatives (FR-003).
    ///
    /// dx/dt = alpha * (y - x - h(x))
    /// dy/dt = x - y + z
    /// dz/dt = -beta * y
    [[nodiscard]] detail::AttractorState computeChuaDerivatives(
        const detail::AttractorState& s, float couplingForce) const noexcept {
        float alpha = chaosParameter_;  // Maps to [12, 18]
        constexpr float beta = 28.0f;

        detail::AttractorState d;
        d.x = alpha * (s.y - s.x - chuaDiode(s.x)) + couplingForce;
        d.y = s.x - s.y + s.z;
        d.z = -beta * s.y;
        return d;
    }

    /// @brief Compute Duffing oscillator derivatives (FR-004).
    ///
    /// dx/dt = v (stored in y)
    /// dv/dt = x - x^3 - gamma * v + A * cos(omega * phase)
    [[nodiscard]] detail::AttractorState computeDuffingDerivatives(
        const detail::AttractorState& s, float couplingForce) const noexcept {
        constexpr float gamma = 0.1f;
        constexpr float omega = 1.4f;
        float A = chaosParameter_;  // Maps to [0.2, 0.5]

        float v = s.y;  // y stores velocity
        float x3 = s.x * s.x * s.x;
        float driving = A * std::cos(omega * duffingPhase_);

        detail::AttractorState d;
        d.x = v + couplingForce;
        d.y = s.x - x3 - gamma * v + driving;
        d.z = 0.0f;  // Unused for 2D system
        return d;
    }

    /// @brief Compute Van der Pol oscillator derivatives (FR-005).
    ///
    /// dx/dt = v (stored in y)
    /// dv/dt = mu * (1 - x^2) * v - x
    [[nodiscard]] detail::AttractorState computeVanDerPolDerivatives(
        const detail::AttractorState& s, float couplingForce) const noexcept {
        float mu = chaosParameter_;  // Maps to [0.5, 5]

        float v = s.y;
        float x2 = s.x * s.x;

        detail::AttractorState d;
        d.x = v + couplingForce;
        d.y = mu * (1.0f - x2) * v - s.x;
        d.z = 0.0f;  // Unused for 2D system
        return d;
    }

    /// @brief Chua diode piecewise-linear nonlinearity (FR-003).
    ///
    /// h(x) = m1*x + 0.5*(m0-m1)*(|x+1| - |x-1|)
    [[nodiscard]] static float chuaDiode(float x) noexcept {
        constexpr float m0 = -1.143f;
        constexpr float m1 = -0.714f;
        return m1 * x + 0.5f * (m0 - m1) * (std::abs(x + 1.0f) - std::abs(x - 1.0f));
    }

    /// @brief Check if attractor state has diverged (FR-011).
    [[nodiscard]] bool checkDivergence() const noexcept {
        // Check bounds
        if (std::abs(state_.x) > safeBound_ ||
            std::abs(state_.y) > safeBound_ ||
            std::abs(state_.z) > safeBound_) {
            return true;
        }
        // Check for NaN/Inf
        if (detail::isNaN(state_.x) || detail::isNaN(state_.y) || detail::isNaN(state_.z) ||
            detail::isInf(state_.x) || detail::isInf(state_.y) || detail::isInf(state_.z)) {
            return true;
        }
        return false;
    }

    /// @brief Sanitize external input (FR-014).
    ///
    /// Replaces NaN with 0.
    [[nodiscard]] float sanitizeInput(float input) const noexcept {
        return detail::isNaN(input) ? 0.0f : input;
    }

    /// @brief Get the value from the selected output axis (FR-010).
    [[nodiscard]] float getAxisValue() const noexcept {
        switch (outputAxis_) {
            case 0: return state_.x;
            case 1: return state_.y;
            case 2: return state_.z;
            default: return state_.x;
        }
    }

    /// @brief Normalize output using tanh soft-limiting (FR-008).
    [[nodiscard]] float normalizeOutput(float value) const noexcept {
        float scale;
        switch (outputAxis_) {
            case 0: scale = xScale_; break;
            case 1: scale = yScale_; break;
            case 2: scale = zScale_; break;
            default: scale = xScale_; break;
        }
        return FastMath::fastTanh(value / scale);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    ChaosAttractor attractor_ = ChaosAttractor::Lorenz;
    float frequency_ = 220.0f;
    float chaosNormalized_ = 1.0f;  // [0, 1]
    float coupling_ = 0.0f;
    size_t outputAxis_ = 0;

    // Computed parameters (from configuration)
    float chaosParameter_ = 28.0f;  // Actual parameter value (e.g., rho)
    float dt_ = 0.0f;               // Integration timestep per sample
    float dtMax_ = 0.001f;          // Maximum stable substep dt
    float safeBound_ = 500.0f;      // Divergence threshold
    float xScale_ = 20.0f;          // Output normalization
    float yScale_ = 20.0f;
    float zScale_ = 30.0f;

    // State
    detail::AttractorState state_;
    float duffingPhase_ = 0.0f;     // Duffing driving term phase
    size_t resetCooldown_ = 0;      // Samples until next reset allowed
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // DC Blocker (FR-009)
    DCBlocker dcBlocker_;
};

}  // namespace DSP
}  // namespace Krate
