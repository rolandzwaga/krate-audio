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
// Constitution Compliance:
// - Principle II: Real-Time Safety (process: noexcept, no alloc, fixed pools)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
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
    static constexpr float kOutputClamp = 1.5f;        ///< Output safety clamp (SC-002)
    static constexpr size_t kSineTableSize = 2048;     ///< Sine wavetable size (power of 2)
    static constexpr uint32_t kSineTableMask = kSineTableSize - 1;

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
        , sampleRate_(44100.0)
        , samplesUntilNextSpawn_(0.0f)
        , rng_(12345)
        , prepared_(false) {
        // Initialize all particles as inactive
        for (auto& p : particles_) {
            p.active = false;
        }
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

        // Precompute sine wavetable
        for (size_t i = 0; i < kSineTableSize; ++i) {
            sineTable_[i] = std::sin(kTwoPi * static_cast<float>(i) /
                                     static_cast<float>(kSineTableSize));
        }

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
        for (auto& p : particles_) {
            p.active = false;
        }

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
        for (auto& p : particles_) {
            p.active = false;
        }
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
        if (spawnMode_ != SpawnMode::Burst) {
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

        // Sum all active particles inline for performance
        const bool hasDrift = (driftAmount_ > 0.0f);
        const float* envTable = envelopeTables_[currentEnvType_].data();
        constexpr float kEnvMaxIndex = static_cast<float>(kEnvTableSize - 1);

        float sum = 0.0f;
        for (size_t idx = 0; idx < kMaxParticles; ++idx) {
            Particle& p = particles_[idx];
            if (!p.active) {
                continue;
            }

            // Advance envelope phase
            p.envelopePhase += p.envelopeIncrement;
            if (p.envelopePhase >= 1.0f) {
                p.active = false;
                continue;
            }

            // Apply drift if enabled (update every 8 samples to reduce cost)
            if (hasDrift && p.driftRange > 0.0f) {
                ++p.driftCounter;
                if (p.driftCounter >= 8) {
                    p.driftCounter = 0;
                    float noise = rng_.nextFloat();
                    p.driftState = driftFilterCoeff_ * p.driftState +
                                   driftOneMinusCoeff_ * noise;
                    float deviationHz = p.driftState * driftAmount_ * p.driftRange;
                    float driftedFreq = p.baseFrequency + deviationHz;
                    if (driftedFreq < kMinFrequency) driftedFreq = kMinFrequency;
                    p.phaseIncrement = driftedFreq * inverseSampleRate_;
                }
            }

            // Inline envelope lookup
            float envIdx = p.envelopePhase * kEnvMaxIndex;
            auto ei0 = static_cast<size_t>(envIdx);
            float efrac = envIdx - static_cast<float>(ei0);
            size_t ei1 = ei0 + 1 < kEnvTableSize ? ei0 + 1 : ei0;
            float envValue = envTable[ei0] + efrac * (envTable[ei1] - envTable[ei0]);

            // Sine wavetable lookup (phase is [0, 1))
            float sinIdx = p.phase * static_cast<float>(kSineTableSize);
            auto si0 = static_cast<uint32_t>(sinIdx) & kSineTableMask;
            uint32_t si1 = (si0 + 1) & kSineTableMask;
            float sfrac = sinIdx - static_cast<float>(static_cast<uint32_t>(sinIdx));
            float sinValue = sineTable_[si0] + sfrac * (sineTable_[si1] - sineTable_[si0]);

            sum += sinValue * envValue;

            // Advance phase
            p.phase += p.phaseIncrement;
            if (p.phase >= 1.0f) {
                p.phase -= 1.0f;
            }
        }

        // Normalize by target density (FR-016)
        sum *= normFactor_;

        // Sanitize output (FR-017)
        return sanitizeOutput(sum);
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
    [[nodiscard]] size_t activeParticleCount() const noexcept {
        size_t count = 0;
        for (const auto& p : particles_) {
            if (p.active) {
                ++count;
            }
        }
        return count;
    }

private:
    // =========================================================================
    // Internal Particle Struct (T007)
    // =========================================================================

    struct Particle {
        // Sine oscillator state
        float phase = 0.0f;              ///< Phase accumulator [0, 1)
        float phaseIncrement = 0.0f;     ///< Phase advance per sample
        float baseFrequency = 0.0f;      ///< Assigned frequency at spawn (Hz)

        // Lifetime tracking
        float envelopePhase = 0.0f;      ///< Progress through lifetime [0, 1]
        float envelopeIncrement = 0.0f;  ///< Envelope phase advance per sample

        // Drift state
        float driftState = 0.0f;         ///< Low-pass filtered random walk [-1, 1]
        float driftRange = 0.0f;         ///< Max frequency deviation (Hz)

        // Status
        bool active = false;
        uint8_t driftCounter = 0;        ///< Subsample counter for drift updates
    };

    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Recompute timing values from current configuration.
    void recomputeTimingValues() noexcept {
        lifetimeSamples_ = lifetimeMs_ * static_cast<float>(sampleRate_) / 1000.0f;
        interonsetSamples_ = lifetimeSamples_ / density_;
    }

    /// @brief Spawn a new particle (T031, T060, T061).
    void spawnParticle() noexcept {
        // Find an inactive slot, or steal the oldest
        Particle* target = nullptr;
        float maxEnvPhase = -1.0f;
        Particle* oldest = nullptr;

        for (auto& p : particles_) {
            if (!p.active) {
                target = &p;
                break;
            }
            // Track oldest for voice stealing
            if (p.envelopePhase > maxEnvPhase) {
                maxEnvPhase = p.envelopePhase;
                oldest = &p;
            }
        }

        // Voice stealing if all slots full (T061)
        if (target == nullptr) {
            target = oldest;
        }

        if (target == nullptr) {
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

        // Initialize particle with random starting phase for decorrelation
        target->phase = rng_.nextUnipolar(); // Random phase [0, 1)
        target->baseFrequency = freq;
        target->phaseIncrement = freq * inverseSampleRate_;
        target->envelopePhase = 0.0f;
        target->envelopeIncrement = 1.0f / lifetimeSamples_;

        // Drift state (T105)
        target->driftState = 0.0f;
        if (scatter_ > 0.0f) {
            // Max deviation proportional to scatter range
            float highFreq = centerFrequency_ * semitonesToRatio(scatter_);
            target->driftRange = highFreq - centerFrequency_;
        } else {
            target->driftRange = 0.0f;
        }

        target->active = true;
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
    // Members (T009)
    // =========================================================================

    // Particle pool (FR-019)
    std::array<Particle, kMaxParticles> particles_{};

    // Configuration state (set via setters)
    float centerFrequency_;     ///< Center frequency (Hz)
    float scatter_;             ///< Scatter half-range (semitones)
    float density_;             ///< Target particle count [1, 64]
    float lifetimeMs_;          ///< Particle lifetime (ms)
    SpawnMode spawnMode_;       ///< Current spawn mode
    float driftAmount_;         ///< Drift magnitude [0, 1]
    size_t currentEnvType_;     ///< Active envelope table index [0, 5]

    // Derived state (computed from configuration)
    float normFactor_;          ///< 1/sqrt(density) (FR-016)
    float lifetimeSamples_;     ///< Lifetime in samples
    float interonsetSamples_;   ///< Spawn interval in samples
    float nyquist_;             ///< Nyquist frequency (Hz)
    float driftFilterCoeff_;    ///< One-pole LPF coefficient for drift
    float driftOneMinusCoeff_;  ///< 1.0f - driftFilterCoeff_ (precomputed)
    float inverseSampleRate_;   ///< 1.0f / sampleRate (for fast division)

    // Processing state
    double sampleRate_;         ///< Current sample rate
    float samplesUntilNextSpawn_; ///< Countdown to next spawn
    Xorshift32 rng_;            ///< PRNG (FR-020)
    bool prepared_;             ///< Whether prepare() has been called

    // Precomputed tables
    std::array<std::array<float, kEnvTableSize>, kNumEnvelopeTypes> envelopeTables_{};
    std::array<float, kSineTableSize> sineTable_{};
};

} // namespace DSP
} // namespace Krate
