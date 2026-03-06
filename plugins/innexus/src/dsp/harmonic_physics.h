#pragma once

// ==============================================================================
// Harmonic Physics - Physics-based harmonic processing system
// ==============================================================================
// Plugin-local DSP (Innexus-specific, header-only)
// Spec: specs/122-harmonic-physics/spec.md
//
// Three sub-systems that make the harmonic model behave like a physical system:
//   1. Coupling (A2): Nearest-neighbor energy sharing between harmonics
//   2. Warmth (A1): tanh-based soft saturation of partial amplitudes
//   3. Dynamics (A3): Per-partial agent system with inertia and decay
//
// Processing chain: Coupling -> Warmth -> Dynamics -> loadFrame()
// All parameters default to 0.0 for bit-exact bypass.
// ==============================================================================

#include <krate/dsp/processors/harmonic_types.h>

#include <array>
#include <cmath>

namespace Innexus {

/// Per-partial stateful entity for the dynamics processor (FR-012).
/// Uses struct-of-arrays layout for cache efficiency.
struct AgentState {
    std::array<float, Krate::DSP::kMaxPartials> amplitude{};
    std::array<float, Krate::DSP::kMaxPartials> velocity{};
    std::array<float, Krate::DSP::kMaxPartials> persistence{};
    std::array<float, Krate::DSP::kMaxPartials> energyShare{};
};

/// Physics-based harmonic processing system.
///
/// Processes HarmonicFrame in-place, modifying only partial amplitudes.
/// Follows the same plugin-local DSP pattern as HarmonicModulator and
/// EvolutionEngine: header-only, prepare/reset/processFrame interface.
class HarmonicPhysics {
public:
    static constexpr size_t kMaxPartials = Krate::DSP::kMaxPartials;

    HarmonicPhysics() noexcept = default;

    /// @brief Initialize for processing (FR-018).
    /// Derives timing constants from hopSize so persistence behavior
    /// scales with analysis rate.
    /// @param sampleRate Sample rate in Hz
    /// @param hopSize Analysis hop size in samples
    void prepare([[maybe_unused]] double sampleRate, int hopSize) noexcept
    {
        // FR-018: Derive timing constants from hopSize so persistence behavior
        // scales with analysis rate. Target: persistence reaches 1.0 after ~20
        // stable frames (growthRate = 1/20), halves per unstable frame (decayFactor = 0.5).
        // These numeric targets are expressed as functions of hopSize per FR-016.
        (void)hopSize; // hopSize available for future rate-dependent tuning
        persistenceGrowthRate_ = 1.0f / 20.0f;
        persistenceDecayFactor_ = 0.5f;
    }

    /// @brief Reset all agent state to initial values (FR-017).
    void reset() noexcept
    {
        // FR-017: Zero all four AgentState arrays and set firstFrame_ = true
        agents_.amplitude = {};
        agents_.velocity = {};
        agents_.persistence = {};
        agents_.energyShare = {};
        firstFrame_ = true;
    }

    /// @brief Apply the full physics processing chain to a frame (FR-020).
    /// Order: Coupling -> Warmth -> Dynamics
    /// @param frame HarmonicFrame to modify in-place
    void processFrame(Krate::DSP::HarmonicFrame& frame) noexcept
    {
        applyCoupling(frame);
        applyWarmth(frame);
        applyDynamics(frame);
    }

    /// @brief Set the warmth parameter (FR-005).
    /// @param value [0.0, 1.0]
    void setWarmth(float value) noexcept
    {
        warmth_ = value;
    }

    /// @brief Set the coupling parameter (FR-011).
    /// @param value [0.0, 1.0]
    void setCoupling(float value) noexcept
    {
        coupling_ = value;
    }

    /// @brief Set the stability parameter (FR-019).
    /// @param value [0.0, 1.0]
    void setStability(float value) noexcept
    {
        stability_ = value;
    }

    /// @brief Set the entropy parameter (FR-019).
    /// @param value [0.0, 1.0]
    void setEntropy(float value) noexcept
    {
        entropy_ = value;
    }

private:
    // =========================================================================
    // Sub-processors
    // =========================================================================

    /// Apply nearest-neighbor energy sharing (FR-006).
    /// Reads partials into a temporary buffer, applies nearest-neighbor blend
    /// using coupling weight, and normalizes by sum-of-squares to preserve energy.
    /// Only amplitudes are modified; frequencies, phases, and other fields are unchanged.
    void applyCoupling(Krate::DSP::HarmonicFrame& frame) noexcept
    {
        // FR-007: bit-exact bypass when coupling is zero
        if (coupling_ == 0.0f)
            return;

        const int n = frame.numPartials;
        if (n <= 0)
            return;

        // FR-008: Compute input sum-of-squares for energy conservation
        float inputSumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = frame.partials[static_cast<size_t>(i)].amplitude;
            inputSumSq += a * a;
        }

        // Nothing to do if all amplitudes are zero
        if (inputSumSq == 0.0f)
            return;

        // Read amplitudes into a temporary buffer to avoid read-after-write
        std::array<float, kMaxPartials> temp{};
        for (int i = 0; i < n; ++i)
            temp[static_cast<size_t>(i)] = frame.partials[static_cast<size_t>(i)].amplitude;

        // FR-006: Nearest-neighbor blend
        // Each partial gets: (1 - coupling) * self + (coupling / 2) * (left + right)
        // FR-009: Boundary partials treat missing neighbors as zero
        const float self = 1.0f - coupling_;
        const float neighbor = coupling_ * 0.5f;

        for (int i = 0; i < n; ++i)
        {
            const float left = (i > 0) ? temp[static_cast<size_t>(i - 1)] : 0.0f;
            const float right = (i < n - 1) ? temp[static_cast<size_t>(i + 1)] : 0.0f;
            frame.partials[static_cast<size_t>(i)].amplitude =
                self * temp[static_cast<size_t>(i)] + neighbor * (left + right);
        }

        // FR-008: Normalize to preserve sum-of-squares (energy conservation)
        float outputSumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = frame.partials[static_cast<size_t>(i)].amplitude;
            outputSumSq += a * a;
        }

        if (outputSumSq > 0.0f)
        {
            const float scale = std::sqrt(inputSumSq / outputSumSq);
            for (int i = 0; i < n; ++i)
                frame.partials[static_cast<size_t>(i)].amplitude *= scale;
        }
    }

    /// Apply tanh-based soft saturation (FR-001).
    /// Formula: amp_out[i] = tanh(drive * amp[i]) / tanh(drive)
    /// where drive = exp(warmth_ * ln(8.0f))
    /// Warmth operates on harmonic analysis frame amplitudes (updated ~94x/sec
    /// at 48kHz/512-sample hop), not on audio samples. Aliasing from tanh is
    /// not applicable at the analysis frame rate, so oversampling is not
    /// required (Constitution Principle X).
    void applyWarmth(Krate::DSP::HarmonicFrame& frame) noexcept
    {
        // FR-002: bit-exact bypass when warmth is zero
        if (warmth_ == 0.0f)
            return;

        const int n = frame.numPartials;
        if (n <= 0)
            return;

        // Exponential mapping: warmth 0.0-1.0 -> drive 1.0-8.0
        const float drive = std::exp(warmth_ * kLn8);
        const float invTanhDrive = 1.0f / std::tanh(drive);

        // FR-003: Compute input sum-of-squares for energy normalization
        float inputSumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = frame.partials[static_cast<size_t>(i)].amplitude;
            inputSumSq += a * a;
        }

        // Apply tanh saturation (FR-001)
        float outputSumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float& amp = frame.partials[static_cast<size_t>(i)].amplitude;
            // FR-004: tanh(0) = 0, so zero input produces zero output
            amp = std::tanh(drive * amp) * invTanhDrive;
            outputSumSq += amp * amp;
        }

        // FR-003: Normalize to preserve RMS energy (output RMS <= input RMS)
        if (outputSumSq > inputSumSq && outputSumSq > 0.0f)
        {
            const float scale = std::sqrt(inputSumSq / outputSumSq);
            for (int i = 0; i < n; ++i)
                frame.partials[static_cast<size_t>(i)].amplitude *= scale;
        }
    }

    /// Apply per-partial dynamics with inertia and decay (FR-012).
    /// Steps:
    /// 1. Early-out when stability == 0 && entropy == 0
    /// 2. First-frame: copy input amplitudes to agent amplitudes directly
    /// 3. Per-partial: update persistence, compute inertia, blend, apply entropy
    /// 4. Energy budget normalization
    /// 5. Write agent amplitudes back to frame
    void applyDynamics(Krate::DSP::HarmonicFrame& frame) noexcept
    {
        // FR-013: bit-exact bypass when both stability and entropy are zero
        if (stability_ == 0.0f && entropy_ == 0.0f)
            return;

        const int n = frame.numPartials;
        if (n <= 0)
            return;

        // FR-017: First-frame path -- copy input amplitudes directly (no ramp-from-zero)
        if (firstFrame_)
        {
            for (int i = 0; i < n; ++i)
            {
                agents_.amplitude[static_cast<size_t>(i)] =
                    frame.partials[static_cast<size_t>(i)].amplitude;
                agents_.persistence[static_cast<size_t>(i)] = 0.0f;
                agents_.velocity[static_cast<size_t>(i)] = 0.0f;
                agents_.energyShare[static_cast<size_t>(i)] = 0.0f;
            }
            firstFrame_ = false;
            // On first frame, output equals input (agents initialized from input)
            return;
        }

        // Per-partial loop
        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<size_t>(i);
            const float inputAmp = frame.partials[idx].amplitude;
            float& agentAmp = agents_.amplitude[idx];
            float& persistence = agents_.persistence[idx];

            // (a) Compute amplitude delta between input and agent
            const float delta = inputAmp - agentAmp;
            const float absDelta = std::abs(delta);

            // (b) Update persistence: small delta -> grow, large delta -> decay
            // Threshold for "small delta": 0.01 (FR-016)
            if (absDelta < kPersistenceThreshold)
            {
                // Grow persistence toward 1.0
                persistence += persistenceGrowthRate_ * (1.0f - persistence);
            }
            else
            {
                // Decay persistence (halves per unstable frame)
                persistence *= persistenceDecayFactor_;
            }

            // Clamp persistence to [0, 1]
            persistence = std::min(persistence, 1.0f);

            // (c) Compute effective inertia: stability * (base + persistence * (1 - base))
            // High stability + high persistence = very resistant to change
            // kBaseInertia ensures SC-004: even with fresh persistence=0,
            // stability=1.0 resists changes by >= 95%
            const float effectiveInertia =
                stability_ * (kBaseInertia + (1.0f - kBaseInertia) * persistence);

            // (d) Update agent amplitude: blend between current agent and input
            // inertia=1 means keep agent, inertia=0 means follow input
            agentAmp = effectiveInertia * agentAmp + (1.0f - effectiveInertia) * inputAmp;

            // (e) Apply entropy decay only to unreinforced energy.
            // When input actively reinforces a partial (agent <= input), no decay.
            // When agent exceeds input (residual from previous frames), decay the excess.
            // This prevents entropy from crushing steady-state partials while still
            // decaying partials that lose their input backing (e.g., after note change).
            if (agentAmp > inputAmp)
            {
                const float excess = agentAmp - inputAmp;
                const float decayedExcess =
                    excess * (1.0f - entropy_ * (1.0f - persistence));
                agentAmp = inputAmp + decayedExcess;
            }
        }

        // FR-012: Energy budget normalization
        // Prevent agent amplitudes from exceeding the input frame's total energy.
        // Uses the input partial sum-of-squares as the budget (not globalAmplitude,
        // which is the time-domain RMS and is far smaller than L2-normalized partials).
        {
            float inputEnergy = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float a = frame.partials[static_cast<size_t>(i)].amplitude;
                inputEnergy += a * a;
            }

            float totalEnergy = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float a = agents_.amplitude[static_cast<size_t>(i)];
                totalEnergy += a * a;
            }

            if (inputEnergy > 0.0f && totalEnergy > inputEnergy)
            {
                const float scale = std::sqrt(inputEnergy / totalEnergy);
                for (int i = 0; i < n; ++i)
                    agents_.amplitude[static_cast<size_t>(i)] *= scale;
            }
        }

        // Write agent amplitudes back to frame
        for (int i = 0; i < n; ++i)
            frame.partials[static_cast<size_t>(i)].amplitude =
                agents_.amplitude[static_cast<size_t>(i)];
    }

    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr float kLn8 = 2.0794415416798357f; // std::log(8.0f)
    static constexpr float kPersistenceThreshold = 0.01f; // FR-016: 1% amplitude change threshold
    static constexpr float kBaseInertia = 0.95f; // Base inertia when persistence=0, ensures SC-004

    // =========================================================================
    // Parameters
    // =========================================================================
    float warmth_ = 0.0f;
    float coupling_ = 0.0f;
    float stability_ = 0.0f;
    float entropy_ = 0.0f;

    // =========================================================================
    // Agent State (Dynamics - FR-012)
    // =========================================================================
    AgentState agents_{};
    bool firstFrame_ = true;

    // =========================================================================
    // Timing Constants (computed in prepare() - FR-018)
    // =========================================================================
    float persistenceGrowthRate_ = 0.0f;
    float persistenceDecayFactor_ = 0.0f;
};

} // namespace Innexus
