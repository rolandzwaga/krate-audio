// ==============================================================================
// Layer 2: DSP Processor - Particle / Swarm Oscillator
// ==============================================================================
// Generates complex textural timbres from up to 64 lightweight sine oscillators
// ("particles") with individual frequency scatter, drift, lifetime, and spawn
// behavior. Three spawn modes control temporal pattern: Regular (evenly spaced),
// Random (stochastic), and Burst (manual trigger).
//
// Features:
// - Up to 64 simultaneous sine particles with individual frequency offsets
// - 3 spawn modes: Regular, Random, Burst (manual trigger)
// - 6 grain envelope types (Hann, Trapezoid, Sine, Blackman, Linear, Exponential)
// - Per-particle frequency drift via low-pass filtered random walk (5-20 Hz)
// - 1/sqrt(N) normalization for stable perceived loudness
// - All memory pre-allocated, fully real-time safe
//
// Performance:
// - SoA (Structure-of-Arrays) layout for hot fields enables cache-line
//   utilization and compiler auto-vectorization (SSE/NEON)
// - Gordon-Smith magic circle phasor eliminates sine wavetable lookups
//   (2 muls + 2 adds vs 2 table loads + interpolation per particle)
// - Cold drift data separated from hot processing path
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process: noexcept, no alloc, fixed pools)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
// - Principle IV: SIMD & DSP Optimization (alignas(32), contiguous arrays)
// - Principle IX: Layer 2 (depends on Layer 0 only)
// - Principle XII: Test-First Development
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

// Suppress MSVC C4324: structure was padded due to alignment specifier
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

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
/// Hot particle fields use SoA (Structure-of-Arrays) layout with 32-byte
/// alignment for SIMD-friendly access. Cold drift data is in a separate
/// AoS struct. A compact active list avoids scanning inactive slots.
/// Total fixed footprint: ~12 KB (SoA arrays + cold data + tables).
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
    static constexpr float kOutputClamp = 1.5f;        ///< Output safety clamp (SC-002)

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor (T024).
    ParticleOscillator() noexcept
        : centerFrequency_(440.0f)
        , scatter_(0.0f)
        , density_(1.0f)
        , lifetimeMs_(100.0f)
        , spawnMode_(SpawnMode::Regular)
        , driftAmount_(0.0f)
        , currentEnvType_(0)
        , normFactor_(1.0f)
        , lifetimeSamples_(0.0f)
        , interonsetSamples_(0.0f)
        , nyquist_(22050.0f)
        , driftFilterCoeff_(0.0f)
        , driftOneMinusCoeff_(1.0f)
        , inverseSampleRate_(1.0f / 44100.0f)
        , activeCount_(0)
        , sampleRate_(44100.0)
        , samplesUntilNextSpawn_(0.0f)
        , rng_(12345)
        , prepared_(false) {
    }

    /// @brief Destructor.
    ~ParticleOscillator() noexcept = default;

    // Non-copyable, movable
    ParticleOscillator(const ParticleOscillator&) = delete;
    ParticleOscillator& operator=(const ParticleOscillator&) = delete;
    ParticleOscillator(ParticleOscillator&&) noexcept = default;
    ParticleOscillator& operator=(ParticleOscillator&&) noexcept = default;

    /// @brief Initialize for processing (FR-001, T025).
    ///
    /// Pre-computes all envelope tables and initializes internal state.
    /// Must be called before any processing.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @post isPrepared() returns true
    /// @note NOT real-time safe (computes envelope tables)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        nyquist_ = static_cast<float>(sampleRate / 2.0);
        inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate);

        // Precompute all 6 envelope tables (FR-021)
        GrainEnvelope::generate(envelopeTables_[0].data(), kEnvTableSize,
                                GrainEnvelopeType::Hann);
        GrainEnvelope::generate(envelopeTables_[1].data(), kEnvTableSize,
                                GrainEnvelopeType::Trapezoid);
        GrainEnvelope::generate(envelopeTables_[2].data(), kEnvTableSize,
                                GrainEnvelopeType::Sine);
        GrainEnvelope::generate(envelopeTables_[3].data(), kEnvTableSize,
                                GrainEnvelopeType::Blackman);
        GrainEnvelope::generate(envelopeTables_[4].data(), kEnvTableSize,
                                GrainEnvelopeType::Linear);
        GrainEnvelope::generate(envelopeTables_[5].data(), kEnvTableSize,
                                GrainEnvelopeType::Exponential);

        // Compute drift filter coefficient: one-pole LPF at ~10 Hz (FR-014)
        driftFilterCoeff_ = static_cast<float>(
            std::exp(-2.0 * static_cast<double>(kPi) * 10.0 / sampleRate));
        driftOneMinusCoeff_ = 1.0f - driftFilterCoeff_;

        // Recompute derived timing values
        recomputeTimingValues();

        // Clear all particles
        activeCount_ = 0;
        slotActive_.fill(0);

        // Reset spawn counter
        samplesUntilNextSpawn_ = 0.0f;

        prepared_ = true;
    }

    /// @brief Reset all particles and internal state (FR-002, T026).
    ///
    /// Clears all active particles and resets spawn timing.
    /// Does not change configuration or sample rate.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        activeCount_ = 0;
        slotActive_.fill(0);
        samplesUntilNextSpawn_ = 0.0f;
    }

    // =========================================================================
    // Frequency Control (FR-004, FR-005)
    // =========================================================================

    /// @brief Set center frequency (FR-004, T028).
    ///
    /// @param centerHz Center frequency in Hz.
    ///        Clamped to [1.0, Nyquist). NaN/Inf sanitized to 440.
    /// @note Real-time safe, noexcept
    void setFrequency(float centerHz) noexcept {
        if (detail::isNaN(centerHz) || detail::isInf(centerHz)) {
            centerHz = 440.0f;
        }
        centerFrequency_ = std::clamp(centerHz, kMinFrequency, nyquist_ - 1.0f);
    }

    /// @brief Set frequency scatter (FR-005, T058).
    ///
    /// Controls spread of particle frequencies around center.
    /// Each particle's offset is drawn uniformly from
    /// [-scatter, +scatter] semitones.
    ///
    /// @param semitones Half-range in semitones. Clamped to [0, 48].
    /// @note Real-time safe, noexcept
    void setFrequencyScatter(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) {
            semitones = 0.0f;
        }
        scatter_ = std::clamp(semitones, 0.0f, kMaxScatter);
    }

    // =========================================================================
    // Population Control (FR-006, FR-007)
    // =========================================================================

    /// @brief Set target particle density (FR-006, T029).
    ///
    /// @param particles Target active particle count. Clamped to [1, 64].
    /// @note Real-time safe, noexcept
    void setDensity(float particles) noexcept {
        if (detail::isNaN(particles) || detail::isInf(particles)) {
            particles = 1.0f;
        }
        density_ = std::clamp(particles, 1.0f, static_cast<float>(kMaxParticles));
        normFactor_ = 1.0f / std::sqrt(density_);

        if (prepared_) {
            recomputeTimingValues();
        }
    }

    /// @brief Set particle lifetime (FR-007, T030).
    ///
    /// @param ms Duration of each particle in milliseconds.
    ///        Clamped to [1, 10000].
    /// @note Real-time safe, noexcept
    void setLifetime(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) {
            ms = 100.0f;
        }
        lifetimeMs_ = std::clamp(ms, kMinLifetimeMs, kMaxLifetimeMs);

        if (prepared_) {
            recomputeTimingValues();
        }
    }

    // =========================================================================
    // Spawn Behavior (FR-008, FR-008a)
    // =========================================================================

    /// @brief Set spawn mode (FR-008, T081).
    ///
    /// @param mode Spawn timing pattern (Regular, Random, Burst)
    /// @note Real-time safe, noexcept
    void setSpawnMode(SpawnMode mode) noexcept {
        spawnMode_ = mode;
        samplesUntilNextSpawn_ = 0.0f;
    }

    /// @brief Trigger burst spawn (FR-008a, T084).
    ///
    /// Spawns all particles up to density count simultaneously.
    /// Only has effect when spawn mode is Burst; no-op otherwise.
    ///
    /// @note Real-time safe, noexcept
    void triggerBurst() noexcept {
        if (spawnMode_ != SpawnMode::Burst) {
            return;
        }

        auto count = static_cast<size_t>(density_);
        for (size_t i = 0; i < count; ++i) {
            spawnParticle();
        }
    }

    // =========================================================================
    // Envelope (FR-012)
    // =========================================================================

    /// @brief Set grain envelope type (FR-012, T103).
    ///
    /// Switches which precomputed envelope table is used.
    /// All tables are precomputed during prepare().
    ///
    /// @param type Envelope shape from GrainEnvelopeType enum
    /// @note Real-time safe (index swap only), noexcept
    void setEnvelopeType(GrainEnvelopeType type) noexcept {
        auto idx = static_cast<size_t>(type);
        if (idx < kNumEnvelopeTypes) {
            currentEnvType_ = idx;
        }
    }

    // =========================================================================
    // Drift (FR-013)
    // =========================================================================

    /// @brief Set frequency drift amount (FR-013, T102).
    ///
    /// @param amount Drift magnitude [0, 1]. 0 = no drift, 1 = maximum.
    ///        Clamped to [0, 1].
    /// @note Real-time safe, noexcept
    void setDriftAmount(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) {
            amount = 0.0f;
        }
        driftAmount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Processing (FR-015)
    // =========================================================================

    /// @brief Generate a single output sample (FR-015, T034).
    ///
    /// @return Mono output sample, normalized and sanitized
    /// @note Real-time safe, noexcept
    [[nodiscard]] float process() noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // Spawn logic based on current mode
        handleSpawn();

        // Hot loop — direct iteration over all slots, SoA layout
        const float* envTable = envelopeTables_[currentEnvType_].data();
        constexpr float kEnvMaxIndex = static_cast<float>(kEnvTableSize - 1);
        const bool hasDrift = (driftAmount_ > 0.0f);

        float sum = 0.0f;

        for (size_t idx = 0; idx < kMaxParticles; ++idx) {
            if (slotActive_[idx] == 0) {
                continue;
            }

            // Advance envelope phase
            float envPh = envelopePhase_[idx] + envelopeIncrement_[idx];
            if (envPh >= 1.0f) {
                slotActive_[idx] = 0;
                --activeCount_;
                continue;
            }
            envelopePhase_[idx] = envPh;

            // Drift (cold path, every 8 samples)
            if (hasDrift && particleCold_[idx].driftRange > 0.0f) {
                ++particleCold_[idx].driftCounter;
                if (particleCold_[idx].driftCounter >= 8) {
                    particleCold_[idx].driftCounter = 0;
                    float noise = rng_.nextFloat();
                    particleCold_[idx].driftState =
                        driftFilterCoeff_ * particleCold_[idx].driftState +
                        driftOneMinusCoeff_ * noise;
                    float deviationHz = particleCold_[idx].driftState *
                                        driftAmount_ * particleCold_[idx].driftRange;
                    float driftedFreq = particleCold_[idx].baseFrequency + deviationHz;
                    if (driftedFreq < kMinFrequency) driftedFreq = kMinFrequency;
                    epsilon_[idx] = 2.0f * std::sin(kPi * driftedFreq * inverseSampleRate_);
                }
            }

            // Envelope table lookup (nearest-neighbor, 256 entries is smooth enough)
            auto envIndex = static_cast<size_t>(envPh * kEnvMaxIndex);
            float envValue = envTable[envIndex];

            // Magic circle (Gordon-Smith) phasor — replaces sine table lookup
            float s = sinState_[idx];
            float c = cosState_[idx];
            sum += s * envValue;

            // Advance phasor rotation (amplitude-stable, det = 1)
            float eps = epsilon_[idx];
            s += eps * c;
            c -= eps * s;
            sinState_[idx] = s;
            cosState_[idx] = c;
        }

        // Normalize by target density (FR-016) and sanitize (FR-017)
        return sanitizeOutput(sum * normFactor_);
    }

    /// @brief Generate a block of output samples (FR-015, T035).
    ///
    /// @param output Destination buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    /// @note Real-time safe, noexcept
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) {
            return;
        }

        if (!prepared_) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = 0.0f;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Seeding (T038)
    // =========================================================================

    /// @brief Seed the PRNG for deterministic behavior.
    ///
    /// @param seedValue Seed for the internal Xorshift32 generator
    void seed(uint32_t seedValue) noexcept {
        rng_.seed(seedValue);
    }

    // =========================================================================
    // Query (T036, T037)
    // =========================================================================

    /// @brief Check if oscillator is prepared (T027).
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get current center frequency.
    [[nodiscard]] float getFrequency() const noexcept {
        return centerFrequency_;
    }

    /// @brief Get current density setting.
    [[nodiscard]] float getDensity() const noexcept {
        return density_;
    }

    /// @brief Get current lifetime setting in ms.
    [[nodiscard]] float getLifetime() const noexcept {
        return lifetimeMs_;
    }

    /// @brief Get current spawn mode.
    [[nodiscard]] SpawnMode getSpawnMode() const noexcept {
        return spawnMode_;
    }

    /// @brief Get number of currently active particles (T036).
    /// O(1) — returns the compact active list size.
    [[nodiscard]] size_t activeParticleCount() const noexcept {
        return activeCount_;
    }

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    /// @brief Cold particle data — accessed infrequently (drift updates only).
    struct ParticleCold {
        float baseFrequency = 0.0f;   ///< Assigned frequency at spawn (Hz)
        float driftState = 0.0f;      ///< Low-pass filtered random walk [-1, 1]
        float driftRange = 0.0f;      ///< Max frequency deviation (Hz)
        uint8_t driftCounter = 0;     ///< Subsample counter for drift updates
    };

    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Handle per-sample spawn scheduling.
    void handleSpawn() noexcept {
        if (spawnMode_ == SpawnMode::Burst) {
            return;
        }
        samplesUntilNextSpawn_ -= 1.0f;
        if (samplesUntilNextSpawn_ <= 0.0f) {
            spawnParticle();
            if (spawnMode_ == SpawnMode::Regular) {
                samplesUntilNextSpawn_ = interonsetSamples_;
            } else {
                // Random mode: exponential distribution for Poisson process
                float u = rng_.nextUnipolar();
                u = std::max(u, 1e-6f);
                samplesUntilNextSpawn_ = interonsetSamples_ * (-std::log(u));
            }
        }
    }

    /// @brief Recompute timing values from current configuration.
    void recomputeTimingValues() noexcept {
        lifetimeSamples_ = lifetimeMs_ * static_cast<float>(sampleRate_) / 1000.0f;
        interonsetSamples_ = lifetimeSamples_ / density_;
    }

    /// @brief Spawn a new particle (T031, T060, T061).
    void spawnParticle() noexcept {
        // Find an inactive slot, or steal the oldest
        size_t target = kMaxParticles; // sentinel
        float maxEnvPhase = -1.0f;
        size_t oldestIdx = kMaxParticles;

        for (size_t i = 0; i < kMaxParticles; ++i) {
            if (slotActive_[i] == 0) {
                target = i;
                break;
            }
        }

        // Voice stealing if all slots full (T061)
        if (target == kMaxParticles) {
            // Find the oldest active particle (highest envelope phase)
            for (size_t i = 0; i < activeCount_; ++i) {
                const auto idx = static_cast<size_t>(activeIndices_[i]);
                if (envelopePhase_[idx] > maxEnvPhase) {
                    maxEnvPhase = envelopePhase_[idx];
                    oldestIdx = idx;
                }
            }
            target = oldestIdx;
        }

        if (target >= kMaxParticles) {
            return; // Should not happen with kMaxParticles > 0
        }

        // Compute scattered frequency (T060)
        float freq = centerFrequency_;
        if (scatter_ > 0.0f) {
            // Uniform offset in [-scatter_, +scatter_] semitones
            float offset = rng_.nextFloat() * scatter_; // nextFloat() returns [-1, 1]
            float ratio = semitonesToRatio(offset);
            freq = centerFrequency_ * ratio;
            freq = std::clamp(freq, kMinFrequency, nyquist_ - 1.0f);
        }

        // Write hot fields (SoA) — magic circle phasor init
        float initialPhase = rng_.nextUnipolar(); // Random phase [0, 1)
        sinState_[target] = std::sin(kTwoPi * initialPhase);
        cosState_[target] = std::cos(kTwoPi * initialPhase);
        epsilon_[target] = 2.0f * std::sin(kPi * freq * inverseSampleRate_);
        envelopePhase_[target] = 0.0f;
        envelopeIncrement_[target] = 1.0f / lifetimeSamples_;

        // Write cold fields
        particleCold_[target].baseFrequency = freq;
        particleCold_[target].driftState = 0.0f;
        particleCold_[target].driftCounter = 0;
        if (scatter_ > 0.0f) {
            float highFreq = centerFrequency_ * semitonesToRatio(scatter_);
            particleCold_[target].driftRange = highFreq - centerFrequency_;
        } else {
            particleCold_[target].driftRange = 0.0f;
        }

        // Add to active list (only if newly activated)
        if (slotActive_[target] == 0) {
            slotActive_[target] = 1;
            activeIndices_[activeCount_] = static_cast<uint8_t>(target);
            ++activeCount_;
        }
        // If voice-stealing (slot was already active), it stays in the list
    }

    /// @brief Sanitize output: replace NaN/Inf with 0, clamp to safe range (T033).
    /// Uses hard clamp at [-kOutputClamp, +kOutputClamp] (FR-017).
    [[nodiscard]] static float sanitizeOutput(float x) noexcept {
        if (detail::isNaN(x) || detail::isInf(x)) {
            return 0.0f;
        }
        return std::clamp(x, -kOutputClamp, kOutputClamp);
    }

    // =========================================================================
    // Members
    // =========================================================================

    // --- Hot particle data: SoA layout, 32-byte aligned (Principle IV) ---
    // Accessed every sample for every active particle.
    // Magic circle (Gordon-Smith) phasor: sin/cos state + epsilon coefficient
    alignas(32) std::array<float, kMaxParticles> sinState_{};
    alignas(32) std::array<float, kMaxParticles> cosState_{};
    alignas(32) std::array<float, kMaxParticles> epsilon_{};
    alignas(32) std::array<float, kMaxParticles> envelopePhase_{};
    alignas(32) std::array<float, kMaxParticles> envelopeIncrement_{};

    // --- Cold particle data: AoS, accessed only during drift updates ---
    std::array<ParticleCold, kMaxParticles> particleCold_{};

    // --- Compact active list ---
    std::array<uint8_t, kMaxParticles> activeIndices_{};  ///< Indices of active slots
    size_t activeCount_;                                   ///< Number of active particles
    std::array<uint8_t, kMaxParticles> slotActive_{};     ///< Per-slot active flag

    // --- Configuration state (set via setters) ---
    float centerFrequency_;     ///< Center frequency (Hz)
    float scatter_;             ///< Scatter half-range (semitones)
    float density_;             ///< Target particle count [1, 64]
    float lifetimeMs_;          ///< Particle lifetime (ms)
    SpawnMode spawnMode_;       ///< Current spawn mode
    float driftAmount_;         ///< Drift magnitude [0, 1]
    size_t currentEnvType_;     ///< Active envelope table index [0, 5]

    // --- Derived state (computed from configuration) ---
    float normFactor_;          ///< 1/sqrt(density) (FR-016)
    float lifetimeSamples_;     ///< Lifetime in samples
    float interonsetSamples_;   ///< Spawn interval in samples
    float nyquist_;             ///< Nyquist frequency (Hz)
    float driftFilterCoeff_;    ///< One-pole LPF coefficient for drift
    float driftOneMinusCoeff_;  ///< 1.0f - driftFilterCoeff_ (precomputed)
    float inverseSampleRate_;   ///< 1.0f / sampleRate (for fast division)

    // --- Processing state ---
    double sampleRate_;         ///< Current sample rate
    float samplesUntilNextSpawn_; ///< Countdown to next spawn
    Xorshift32 rng_;            ///< PRNG (FR-020)
    bool prepared_;             ///< Whether prepare() has been called

    // --- Precomputed tables ---
    std::array<std::array<float, kEnvTableSize>, kNumEnvelopeTypes> envelopeTables_{};
};

} // namespace DSP
} // namespace Krate

#ifdef _MSC_VER
#pragma warning(pop)
#endif
