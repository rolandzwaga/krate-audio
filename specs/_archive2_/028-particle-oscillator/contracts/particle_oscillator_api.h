// ==============================================================================
// API Contract: Particle / Swarm Oscillator
// ==============================================================================
// This file defines the public API contract for the ParticleOscillator.
// It is a design artifact -- NOT compiled code.
//
// Location: dsp/include/krate/dsp/processors/particle_oscillator.h
// Layer: 2 (Processors)
// Namespace: Krate::DSP
// Dependencies: Layer 0 (random.h, grain_envelope.h, pitch_utils.h,
//               math_constants.h, db_utils.h)
//
// Reference: specs/028-particle-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/random.h>
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SpawnMode Enumeration (FR-008)
// =============================================================================

/// @brief Temporal pattern for particle creation.
enum class SpawnMode : uint8_t {
    Regular = 0,  ///< Evenly spaced intervals (lifetime / density)
    Random  = 1,  ///< Stochastic (Poisson-like) timing
    Burst   = 2   ///< Manual trigger only via triggerBurst()
};

// =============================================================================
// ParticleOscillator Class
// =============================================================================

/// @brief Particle/swarm oscillator generating textural timbres from many
///        lightweight sine oscillators with individual drift, lifetime,
///        and spawn behavior.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: Layer 0 (random.h, grain_envelope.h, pitch_utils.h,
///                    math_constants.h, db_utils.h)
///
/// @par Memory Model
/// All particle storage and envelope tables are pre-allocated (compile-time
/// arrays). No heap allocation during processing. Total fixed footprint:
/// ~10 KB (particles + envelope tables).
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (computes envelope tables)
/// - All other methods: Real-time safe (noexcept, no allocations)
///
/// @par Usage Example
/// @code
/// ParticleOscillator osc;
/// osc.prepare(44100.0);
/// osc.setFrequency(440.0f);
/// osc.setDensity(16.0f);
/// osc.setFrequencyScatter(3.0f);
/// osc.setLifetime(200.0f);
///
/// float buffer[512];
/// osc.processBlock(buffer, 512);
/// @endcode
class ParticleOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxParticles = 64;        ///< Maximum particle count
    static constexpr size_t kEnvTableSize = 256;       ///< Envelope lookup table size
    static constexpr size_t kNumEnvelopeTypes = 6;     ///< Number of envelope types
    static constexpr float kMinFrequency = 1.0f;       ///< Min center frequency (Hz)
    static constexpr float kMinLifetimeMs = 1.0f;      ///< Min lifetime (ms)
    static constexpr float kMaxLifetimeMs = 10000.0f;  ///< Max lifetime (ms)
    static constexpr float kMaxScatter = 48.0f;        ///< Max scatter (semitones)
    static constexpr float kOutputClamp = 2.0f;        ///< Output safety clamp

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ParticleOscillator() noexcept;

    /// @brief Destructor.
    ~ParticleOscillator() noexcept = default;

    // Non-copyable, movable
    ParticleOscillator(const ParticleOscillator&) = delete;
    ParticleOscillator& operator=(const ParticleOscillator&) = delete;
    ParticleOscillator(ParticleOscillator&&) noexcept = default;
    ParticleOscillator& operator=(ParticleOscillator&&) noexcept = default;

    /// @brief Initialize for processing (FR-001).
    ///
    /// Pre-computes all envelope tables and initializes internal state.
    /// Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @post isPrepared() returns true
    /// @note NOT real-time safe (computes envelope tables)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all particles and internal state (FR-002).
    ///
    /// Clears all active particles and resets spawn timing.
    /// Does not change configuration (frequency, density, etc.)
    /// or sample rate.
    ///
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Frequency Control (FR-004, FR-005)
    // =========================================================================

    /// @brief Set center frequency (FR-004).
    ///
    /// @param centerHz Center frequency in Hz.
    ///        Clamped to [1.0, Nyquist). NaN/Inf sanitized.
    /// @note Real-time safe, noexcept
    void setFrequency(float centerHz) noexcept;

    /// @brief Set frequency scatter (FR-005).
    ///
    /// Controls spread of particle frequencies around center.
    /// Each particle's offset is drawn uniformly from
    /// [-scatter, +scatter] semitones.
    ///
    /// @param semitones Half-range in semitones. Clamped to [0, 48].
    /// @note Real-time safe, noexcept
    void setFrequencyScatter(float semitones) noexcept;

    // =========================================================================
    // Population Control (FR-006, FR-007)
    // =========================================================================

    /// @brief Set target particle density (FR-006).
    ///
    /// @param particles Target active particle count. Clamped to [1, 64].
    ///        When decreasing, excess particles expire naturally.
    /// @note Real-time safe, noexcept
    void setDensity(float particles) noexcept;

    /// @brief Set particle lifetime (FR-007).
    ///
    /// @param ms Duration of each particle in milliseconds.
    ///        Clamped to [1, 10000].
    /// @note Real-time safe, noexcept
    void setLifetime(float ms) noexcept;

    // =========================================================================
    // Spawn Behavior (FR-008, FR-008a)
    // =========================================================================

    /// @brief Set spawn mode (FR-008).
    ///
    /// @param mode Spawn timing pattern (Regular, Random, Burst)
    /// @note Real-time safe, noexcept
    void setSpawnMode(SpawnMode mode) noexcept;

    /// @brief Trigger burst spawn (FR-008a).
    ///
    /// Spawns all particles up to density count simultaneously.
    /// Only has effect when spawn mode is Burst; no-op otherwise.
    ///
    /// @note Real-time safe, noexcept
    void triggerBurst() noexcept;

    // =========================================================================
    // Envelope (FR-012)
    // =========================================================================

    /// @brief Set grain envelope type (FR-012).
    ///
    /// Switches which precomputed envelope table is used.
    /// All tables are precomputed during prepare().
    ///
    /// @param type Envelope shape from GrainEnvelopeType enum
    /// @note Real-time safe (index swap only), noexcept
    void setEnvelopeType(GrainEnvelopeType type) noexcept;

    // =========================================================================
    // Drift (FR-013)
    // =========================================================================

    /// @brief Set frequency drift amount (FR-013).
    ///
    /// @param amount Drift magnitude [0, 1]. 0 = no drift, 1 = maximum.
    ///        Clamped to [0, 1].
    /// @note Real-time safe, noexcept
    void setDriftAmount(float amount) noexcept;

    // =========================================================================
    // Processing (FR-015)
    // =========================================================================

    /// @brief Generate a single output sample (FR-015).
    ///
    /// @return Mono output sample, normalized and sanitized
    /// @note Real-time safe, noexcept
    [[nodiscard]] float process() noexcept;

    /// @brief Generate a block of output samples (FR-015).
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    /// @note Real-time safe, noexcept
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Seeding
    // =========================================================================

    /// @brief Seed the PRNG for deterministic behavior.
    ///
    /// @param seedValue Seed for the internal Xorshift32 generator
    /// @note Useful for testing reproducibility (SC-005)
    void seed(uint32_t seedValue) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if oscillator is prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get current center frequency.
    [[nodiscard]] float getFrequency() const noexcept;

    /// @brief Get current density setting.
    [[nodiscard]] float getDensity() const noexcept;

    /// @brief Get current lifetime setting in ms.
    [[nodiscard]] float getLifetime() const noexcept;

    /// @brief Get current spawn mode.
    [[nodiscard]] SpawnMode getSpawnMode() const noexcept;

    /// @brief Get number of currently active particles.
    [[nodiscard]] size_t activeParticleCount() const noexcept;
};

} // namespace DSP
} // namespace Krate
