// ==============================================================================
// Layer 2: DSP Processors
// stochastic_filter.h - Filter with stochastically varying parameters
// ==============================================================================
// Feature: 087-stochastic-filter
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics)
// - Principle IX: Layer 2 (depends only on Layers 0-1)
// - Principle X: DSP Constraints (control-rate updates, smoothed transitions)
//
// Dependencies:
// - Layer 0: core/random.h (Xorshift32 PRNG)
// - Layer 1: primitives/svf.h (TPT State Variable Filter)
// - Layer 1: primitives/smoother.h (OnePoleSmoother)
// ==============================================================================

#pragma once

#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// RandomMode Enumeration (FR-001)
// =============================================================================

/// @brief Random modulation algorithm selection.
///
/// Four modes provide different characters of randomness:
/// - Walk: Brownian motion, smooth drift
/// - Jump: Discrete random values at specified rate
/// - Lorenz: Chaotic attractor, deterministic but unpredictable
/// - Perlin: Coherent noise, smooth band-limited randomness
enum class RandomMode : uint8_t {
    Walk = 0,   ///< Brownian motion (FR-002)
    Jump,       ///< Discrete random jumps (FR-003)
    Lorenz,     ///< Chaotic attractor (FR-004)
    Perlin      ///< Coherent noise (FR-005)
};

// =============================================================================
// FilterTypeMask Namespace (FR-008)
// =============================================================================

/// @brief Bitmask values for enabling filter types in random selection.
namespace FilterTypeMask {
    constexpr uint8_t Lowpass   = 1 << 0;  ///< 0x01
    constexpr uint8_t Highpass  = 1 << 1;  ///< 0x02
    constexpr uint8_t Bandpass  = 1 << 2;  ///< 0x04
    constexpr uint8_t Notch     = 1 << 3;  ///< 0x08
    constexpr uint8_t Allpass   = 1 << 4;  ///< 0x10
    constexpr uint8_t Peak      = 1 << 5;  ///< 0x20
    constexpr uint8_t LowShelf  = 1 << 6;  ///< 0x40
    constexpr uint8_t HighShelf = 1 << 7;  ///< 0x80
    constexpr uint8_t All       = 0xFF;    ///< All types enabled
}

// =============================================================================
// StochasticFilter Class (FR-014, FR-016)
// =============================================================================

/// @brief Layer 2 DSP Processor - Filter with stochastic parameter modulation.
///
/// Composes an SVF filter with multiple random modulation sources for
/// experimental sound design. Supports randomization of cutoff, resonance,
/// and filter type with four distinct random algorithms.
///
/// @par Real-Time Safety (FR-019)
/// All processing methods are noexcept with zero allocations.
/// Random generation uses only the deterministic Xorshift32 PRNG.
///
/// @par Stereo Processing (FR-018)
/// Uses linked modulation - same random sequence for both channels.
/// Create one instance and process both L/R through it.
///
/// @par Usage
/// @code
/// StochasticFilter filter;
/// filter.prepare(44100.0, 512);
/// filter.setMode(RandomMode::Walk);
/// filter.setBaseCutoff(1000.0f);
/// filter.setCutoffOctaveRange(2.0f);
/// filter.processBlock(buffer, numSamples);
/// @endcode
class StochasticFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinChangeRate = 0.01f;   ///< Minimum rate in Hz (FR-010)
    static constexpr float kMaxChangeRate = 100.0f;  ///< Maximum rate in Hz (FR-010)
    static constexpr float kDefaultChangeRate = 1.0f;

    static constexpr float kMinSmoothing = 0.0f;     ///< Minimum smoothing in ms (FR-011)
    static constexpr float kMaxSmoothing = 1000.0f;  ///< Maximum smoothing in ms (FR-011)
    static constexpr float kDefaultSmoothing = 50.0f;

    static constexpr float kMinOctaveRange = 0.0f;   ///< No modulation
    static constexpr float kMaxOctaveRange = 8.0f;   ///< 8 octaves (FR-006)
    static constexpr float kDefaultOctaveRange = 2.0f;

    static constexpr float kMinQRange = 0.0f;
    static constexpr float kMaxQRange = 1.0f;        ///< Normalized (FR-007)
    static constexpr float kDefaultQRange = 0.5f;

    static constexpr size_t kControlRateInterval = 32;  ///< Samples between updates (FR-022)

    // =========================================================================
    // Lifecycle
    // =========================================================================

    StochasticFilter() noexcept = default;
    ~StochasticFilter() = default;

    // Non-copyable (contains filter state)
    StochasticFilter(const StochasticFilter&) = delete;
    StochasticFilter& operator=(const StochasticFilter&) = delete;

    // Movable
    StochasticFilter(StochasticFilter&&) noexcept = default;
    StochasticFilter& operator=(StochasticFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate. (FR-016)
    /// @param sampleRate Audio sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum samples per processBlock() call (unused, for future)
    /// @pre sampleRate >= 1000.0
    /// @note NOT real-time safe (may initialize state)
    void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
        sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

        // Prepare filters
        filterA_.prepare(sampleRate_);
        filterB_.prepare(sampleRate_);

        // Configure filter with base parameters
        filterA_.setMode(baseFilterType_);
        filterA_.setCutoff(baseCutoffHz_);
        filterA_.setResonance(baseResonance_);

        filterB_.setMode(baseFilterType_);
        filterB_.setCutoff(baseCutoffHz_);
        filterB_.setResonance(baseResonance_);

        // Configure smoothers
        const float sampleRateF = static_cast<float>(sampleRate_);
        cutoffSmoother_.configure(smoothingTimeMs_, sampleRateF);
        cutoffSmoother_.snapTo(baseCutoffHz_);

        resonanceSmoother_.configure(smoothingTimeMs_, sampleRateF);
        resonanceSmoother_.snapTo(baseResonance_);

        crossfadeSmoother_.configure(smoothingTimeMs_, sampleRateF);
        crossfadeSmoother_.snapTo(0.0f);

        // Initialize random state
        rng_.seed(seed_);

        // Initialize mode-specific state
        walkValue_ = 0.0f;
        jumpValue_ = 0.0f;
        samplesUntilNextJump_ = 0.0f;
        jumpOccurred_ = false;
        initializeLorenzState();
        perlinTime_ = 0.0f;

        // Reset control rate counter
        samplesUntilUpdate_ = 0;

        // Reset transition state
        currentTypeA_ = baseFilterType_;
        currentTypeB_ = baseFilterType_;
        isTransitioning_ = false;

        prepared_ = true;
    }

    /// @brief Reset all state while preserving configuration. (FR-024, FR-025)
    /// @post Random state restored to saved seed
    /// @post Filter state cleared
    /// @post All configuration preserved
    /// @note Real-time safe
    void reset() noexcept {
        // Reset filters
        filterA_.reset();
        filterB_.reset();

        // Restore random state from saved seed (FR-024)
        rng_.seed(seed_);

        // Reset mode-specific state
        walkValue_ = 0.0f;
        jumpValue_ = 0.0f;
        samplesUntilNextJump_ = 0.0f;
        jumpOccurred_ = false;
        initializeLorenzState();
        perlinTime_ = 0.0f;

        // Reset smoothers to current base values
        cutoffSmoother_.snapTo(baseCutoffHz_);
        resonanceSmoother_.snapTo(baseResonance_);
        crossfadeSmoother_.snapTo(0.0f);

        // Reset control rate counter
        samplesUntilUpdate_ = 0;

        // Reset transition state
        currentTypeA_ = baseFilterType_;
        currentTypeB_ = baseFilterType_;
        isTransitioning_ = false;
    }

    // =========================================================================
    // Processing (FR-016, FR-019)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;
        }

        // Check if control-rate update is needed
        if (samplesUntilUpdate_ <= 0) {
            updateModulation();
            samplesUntilUpdate_ += static_cast<int>(kControlRateInterval);
        }

        // Apply smoothing to get current parameter values
        const float smoothedCutoff = cutoffSmoother_.process();
        const float smoothedResonance = resonanceSmoother_.process();

        // Update filter parameters
        filterA_.setCutoff(smoothedCutoff);
        filterA_.setResonance(smoothedResonance);

        --samplesUntilUpdate_;

        // Process through filter (handle crossfade if transitioning)
        if (!isTransitioning_) {
            return filterA_.process(input);
        }

        // Process through both filters and crossfade
        filterB_.setCutoff(smoothedCutoff);
        filterB_.setResonance(smoothedResonance);

        const float outA = filterA_.process(input);
        const float outB = filterB_.process(input);
        const float mix = crossfadeSmoother_.process();

        // Check if transition complete
        if (crossfadeSmoother_.isComplete()) {
            // Swap: B becomes new A
            std::swap(filterA_, filterB_);
            currentTypeA_ = currentTypeB_;
            crossfadeSmoother_.snapTo(0.0f);
            isTransitioning_ = false;
        }

        return outA * (1.0f - mix) + outB * mix;
    }

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio samples (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocations)
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Mode Selection (FR-001)
    // =========================================================================

    /// @brief Set the random modulation mode.
    /// @param mode Random algorithm (Walk, Jump, Lorenz, Perlin)
    void setMode(RandomMode mode) noexcept {
        mode_ = mode;
    }

    /// @brief Get the current random modulation mode.
    [[nodiscard]] RandomMode getMode() const noexcept {
        return mode_;
    }

    // =========================================================================
    // Randomization Enable (FR-009)
    // =========================================================================

    /// @brief Enable/disable cutoff frequency randomization.
    void setCutoffRandomEnabled(bool enabled) noexcept {
        cutoffRandomEnabled_ = enabled;
    }

    /// @brief Enable/disable resonance (Q) randomization.
    void setResonanceRandomEnabled(bool enabled) noexcept {
        resonanceRandomEnabled_ = enabled;
    }

    /// @brief Enable/disable filter type randomization.
    void setTypeRandomEnabled(bool enabled) noexcept {
        typeRandomEnabled_ = enabled;
    }

    [[nodiscard]] bool isCutoffRandomEnabled() const noexcept {
        return cutoffRandomEnabled_;
    }

    [[nodiscard]] bool isResonanceRandomEnabled() const noexcept {
        return resonanceRandomEnabled_;
    }

    [[nodiscard]] bool isTypeRandomEnabled() const noexcept {
        return typeRandomEnabled_;
    }

    // =========================================================================
    // Base Parameters (FR-013)
    // =========================================================================

    /// @brief Set center cutoff frequency.
    /// @param hz Frequency in Hz (clamped to [1, sampleRate*0.495])
    void setBaseCutoff(float hz) noexcept {
        const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
        baseCutoffHz_ = std::clamp(hz, SVF::kMinCutoff, maxCutoff);
    }

    /// @brief Set center resonance (Q factor).
    /// @param q Q value (clamped to [0.1, 30])
    void setBaseResonance(float q) noexcept {
        baseResonance_ = std::clamp(q, SVF::kMinQ, SVF::kMaxQ);
    }

    /// @brief Set default filter type (used when type randomization disabled).
    /// @param type SVF filter mode
    void setBaseFilterType(SVFMode type) noexcept {
        baseFilterType_ = type;
        if (!isTransitioning_) {
            filterA_.setMode(type);
            currentTypeA_ = type;
        }
    }

    [[nodiscard]] float getBaseCutoff() const noexcept {
        return baseCutoffHz_;
    }

    [[nodiscard]] float getBaseResonance() const noexcept {
        return baseResonance_;
    }

    [[nodiscard]] SVFMode getBaseFilterType() const noexcept {
        return baseFilterType_;
    }

    // =========================================================================
    // Randomization Ranges (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set cutoff modulation range in octaves. (FR-006)
    /// @param octaves Range in +/- octaves from base (0-8, default 2)
    void setCutoffOctaveRange(float octaves) noexcept {
        cutoffOctaveRange_ = std::clamp(octaves, kMinOctaveRange, kMaxOctaveRange);
    }

    /// @brief Set resonance modulation range. (FR-007)
    /// @param range Normalized range 0-1 (maps to Q range)
    void setResonanceRange(float range) noexcept {
        resonanceRange_ = std::clamp(range, kMinQRange, kMaxQRange);
    }

    /// @brief Set which filter types can be randomly selected. (FR-008)
    /// @param typeMask Bitmask of FilterTypeMask values
    void setEnabledFilterTypes(uint8_t typeMask) noexcept {
        // Ensure at least one type is enabled
        enabledTypesMask_ = (typeMask != 0) ? typeMask : FilterTypeMask::Lowpass;
    }

    [[nodiscard]] float getCutoffOctaveRange() const noexcept {
        return cutoffOctaveRange_;
    }

    [[nodiscard]] float getResonanceRange() const noexcept {
        return resonanceRange_;
    }

    [[nodiscard]] uint8_t getEnabledFilterTypes() const noexcept {
        return enabledTypesMask_;
    }

    // =========================================================================
    // Control Parameters (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set modulation change rate. (FR-010)
    /// @param hz Rate in Hz (0.01-100, default 1)
    void setChangeRate(float hz) noexcept {
        changeRateHz_ = std::clamp(hz, kMinChangeRate, kMaxChangeRate);
    }

    /// @brief Set transition smoothing time. (FR-011)
    /// @param ms Time in milliseconds (0-1000, default 50)
    void setSmoothingTime(float ms) noexcept {
        smoothingTimeMs_ = std::clamp(ms, kMinSmoothing, kMaxSmoothing);
        // Reconfigure smoothers if prepared
        if (prepared_) {
            const float sampleRateF = static_cast<float>(sampleRate_);
            cutoffSmoother_.configure(smoothingTimeMs_, sampleRateF);
            resonanceSmoother_.configure(smoothingTimeMs_, sampleRateF);
            crossfadeSmoother_.configure(smoothingTimeMs_, sampleRateF);
        }
    }

    /// @brief Set random seed for reproducibility. (FR-012, FR-023)
    /// @param seed Seed value (non-zero)
    void setSeed(uint32_t seed) noexcept {
        // Store seed (Xorshift32 will use default if 0 is passed)
        seed_ = (seed != 0) ? seed : 1;
        rng_.seed(seed_);

        // Reset mode-specific state for reproducibility
        walkValue_ = 0.0f;
        jumpValue_ = 0.0f;
        samplesUntilNextJump_ = 0.0f;
        initializeLorenzState();
        perlinTime_ = 0.0f;
    }

    [[nodiscard]] float getChangeRate() const noexcept {
        return changeRateHz_;
    }

    [[nodiscard]] float getSmoothingTime() const noexcept {
        return smoothingTimeMs_;
    }

    [[nodiscard]] uint32_t getSeed() const noexcept {
        return seed_;
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get configured sample rate.
    [[nodiscard]] double sampleRate() const noexcept {
        return sampleRate_;
    }

private:
    // =========================================================================
    // Filter Instances (for type crossfade)
    // =========================================================================

    SVF filterA_;
    SVF filterB_;

    // =========================================================================
    // Random Generator
    // =========================================================================

    Xorshift32 rng_{1};
    uint32_t seed_ = 1;

    // =========================================================================
    // Mode State
    // =========================================================================

    RandomMode mode_ = RandomMode::Walk;

    // =========================================================================
    // Walk Mode State
    // =========================================================================

    float walkValue_ = 0.0f;

    // =========================================================================
    // Jump Mode State
    // =========================================================================

    float jumpValue_ = 0.0f;
    float samplesUntilNextJump_ = 0.0f;
    bool jumpOccurred_ = false;  ///< Flag indicating a jump just occurred (for FR-008)

    // =========================================================================
    // Lorenz Mode State
    // =========================================================================

    float lorenzX_ = 0.1f;
    float lorenzY_ = 0.0f;
    float lorenzZ_ = 25.0f;

    // =========================================================================
    // Perlin Mode State
    // =========================================================================

    float perlinTime_ = 0.0f;

    // =========================================================================
    // Parameter Smoothers
    // =========================================================================

    OnePoleSmoother cutoffSmoother_;
    OnePoleSmoother resonanceSmoother_;
    OnePoleSmoother crossfadeSmoother_;

    // =========================================================================
    // Type Transition State
    // =========================================================================

    SVFMode currentTypeA_ = SVFMode::Lowpass;
    SVFMode currentTypeB_ = SVFMode::Lowpass;
    bool isTransitioning_ = false;

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    float baseCutoffHz_ = 1000.0f;
    float baseResonance_ = 0.707f;
    SVFMode baseFilterType_ = SVFMode::Lowpass;
    float cutoffOctaveRange_ = kDefaultOctaveRange;
    float resonanceRange_ = kDefaultQRange;
    uint8_t enabledTypesMask_ = FilterTypeMask::Lowpass |
                                FilterTypeMask::Highpass |
                                FilterTypeMask::Bandpass;  // LP, HP, BP by default
    float changeRateHz_ = kDefaultChangeRate;
    float smoothingTimeMs_ = kDefaultSmoothing;
    bool cutoffRandomEnabled_ = true;
    bool resonanceRandomEnabled_ = false;
    bool typeRandomEnabled_ = false;
    bool prepared_ = false;

    // =========================================================================
    // Control Rate State
    // =========================================================================

    int samplesUntilUpdate_ = 0;

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update modulation values at control rate
    void updateModulation() noexcept {
        // Get modulation value based on current mode
        float modulation = 0.0f;
        switch (mode_) {
            case RandomMode::Walk:
                modulation = calculateWalkValue();
                break;
            case RandomMode::Jump:
                modulation = calculateJumpValue();
                break;
            case RandomMode::Lorenz:
                modulation = calculateLorenzValue();
                break;
            case RandomMode::Perlin:
                modulation = calculatePerlinValue();
                break;
        }

        // Apply modulation to cutoff if enabled
        if (cutoffRandomEnabled_) {
            // Octave-based scaling (research.md section 7)
            const float octaveOffset = modulation * cutoffOctaveRange_;
            const float modulatedCutoff = baseCutoffHz_ * std::pow(2.0f, octaveOffset);

            // Clamp to valid range
            const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
            const float clampedCutoff = std::clamp(modulatedCutoff, SVF::kMinCutoff, maxCutoff);
            cutoffSmoother_.setTarget(clampedCutoff);
        } else {
            cutoffSmoother_.setTarget(baseCutoffHz_);
        }

        // Apply modulation to resonance if enabled (FR-007)
        if (resonanceRandomEnabled_) {
            // Scale modulation by resonanceRange_ (normalized 0-1)
            // Map from [-1, 1] to [baseResonance_ - range, baseResonance_ + range]
            // resonanceRange_ represents how much Q can vary from base
            const float qVariation = modulation * resonanceRange_ * (SVF::kMaxQ - SVF::kMinQ);
            const float modulatedQ = baseResonance_ + qVariation;
            const float clampedQ = std::clamp(modulatedQ, SVF::kMinQ, SVF::kMaxQ);
            resonanceSmoother_.setTarget(clampedQ);
        } else {
            resonanceSmoother_.setTarget(baseResonance_);
        }

        // Handle type randomization if enabled (FR-008)
        // Type changes occur when a jump happens in Jump mode
        if (typeRandomEnabled_ && mode_ == RandomMode::Jump && jumpOccurred_) {
            SVFMode newType = selectRandomType();
            startTypeTransition(newType);
        }
    }

    /// @brief Calculate Walk mode (Brownian motion) value
    /// @return Modulation value in [-1, 1]
    float calculateWalkValue() noexcept {
        // Step size derived from change rate (research.md section 1)
        // At 1 Hz rate, we want full range traversal in ~1 second
        // updateInterval = kControlRateInterval / sampleRate
        const float updateIntervalSec = static_cast<float>(kControlRateInterval) /
                                        static_cast<float>(sampleRate_);
        // stepSize scales with change rate and update interval
        const float stepSize = 2.0f * changeRateHz_ * updateIntervalSec;

        // Random delta in [-stepSize, stepSize]
        const float delta = rng_.nextFloat() * stepSize;

        // Update walk value with clamping
        walkValue_ += delta;
        walkValue_ = std::clamp(walkValue_, -1.0f, 1.0f);

        return walkValue_;
    }

    /// @brief Calculate Jump mode value (discrete random jumps)
    /// @return Modulation value in [-1, 1]
    float calculateJumpValue() noexcept {
        // Timer-based trigger (research.md section 2)
        samplesUntilNextJump_ -= static_cast<float>(kControlRateInterval);

        jumpOccurred_ = false;  // Reset flag each update
        if (samplesUntilNextJump_ <= 0.0f) {
            // Generate new random value
            jumpValue_ = rng_.nextFloat();  // [-1, 1]

            // Reset timer based on change rate
            samplesUntilNextJump_ += static_cast<float>(sampleRate_) / changeRateHz_;

            jumpOccurred_ = true;  // Signal that a jump occurred (for FR-008)
        }

        return jumpValue_;
    }

    /// @brief Calculate Lorenz mode value (chaotic attractor)
    /// @return Modulation value in [-1, 1]
    float calculateLorenzValue() noexcept {
        // Standard Lorenz parameters (research.md section 3)
        constexpr float sigma = 10.0f;
        constexpr float rho = 28.0f;
        constexpr float beta = 8.0f / 3.0f;

        // Time step scaled by change rate
        const float dt = 0.0001f * changeRateHz_;

        // Euler integration
        const float dx = sigma * (lorenzY_ - lorenzX_) * dt;
        const float dy = (lorenzX_ * (rho - lorenzZ_) - lorenzY_) * dt;
        const float dz = (lorenzX_ * lorenzY_ - beta * lorenzZ_) * dt;

        lorenzX_ += dx;
        lorenzY_ += dy;
        lorenzZ_ += dz;

        // Check for NaN/Inf and reset if needed
        if (!std::isfinite(lorenzX_) || !std::isfinite(lorenzY_) ||
            !std::isfinite(lorenzZ_)) {
            initializeLorenzState();
        }

        // Output: X-axis normalized to [-1, 1]
        // Lorenz X typically ranges [-20, 20] for standard params
        const float output = lorenzX_ / 20.0f;
        return std::clamp(output, -1.0f, 1.0f);
    }

    /// @brief Calculate Perlin mode value (coherent noise)
    /// @return Modulation value in [-1, 1]
    float calculatePerlinValue() noexcept {
        // Advance time based on change rate
        const float updateIntervalSec = static_cast<float>(kControlRateInterval) /
                                        static_cast<float>(sampleRate_);
        perlinTime_ += changeRateHz_ * updateIntervalSec;

        return perlin1D(perlinTime_);
    }

    /// @brief Initialize Lorenz attractor state from seed
    void initializeLorenzState() noexcept {
        // Initialize from seed for deterministic behavior
        Xorshift32 initRng{seed_};
        lorenzX_ = initRng.nextFloat() * 0.1f + 0.1f;
        lorenzY_ = initRng.nextFloat() * 0.1f + 0.1f;
        lorenzZ_ = initRng.nextFloat() * 0.1f + 25.0f;
    }

    /// @brief 1D Perlin noise with 3 octaves (research.md section 4)
    /// @param t Time value
    /// @return Noise value in [-1, 1]
    float perlin1D(float t) const noexcept {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        // 3 octaves per spec clarification
        for (int octave = 0; octave < 3; ++octave) {
            value += noise1D(t * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= 0.5f;    // Persistence
            frequency *= 2.0f;    // Lacunarity
        }

        return value / maxValue;  // Normalize to [-1, 1]
    }

    /// @brief Base 1D gradient noise function
    /// @param x Position value
    /// @return Noise value
    float noise1D(float x) const noexcept {
        const int xi = static_cast<int>(std::floor(x));
        const float xf = x - static_cast<float>(xi);

        // 5th order smoothstep interpolation
        const float u = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);

        // Gradients from hash
        const float g0 = gradientAt(xi);
        const float g1 = gradientAt(xi + 1);

        // Interpolate
        return g0 * (1.0f - u) + g1 * u;
    }

    /// @brief Get gradient value at integer position (deterministic from seed)
    /// @param i Integer position
    /// @return Gradient value in [-1, 1]
    float gradientAt(int i) const noexcept {
        // Hash function using seed
        uint32_t hash = static_cast<uint32_t>(i) * 0x9E3779B9u ^ seed_;
        hash ^= hash >> 16;
        hash *= 0x85EBCA6Bu;
        hash ^= hash >> 13;
        // Convert to [-1, 1]
        return static_cast<float>(hash) / 2147483647.5f - 1.0f;
    }

    /// @brief Update filter parameters based on modulation
    void updateFilterParameters() noexcept {
        // This method is integrated into updateModulation() for efficiency
    }

    /// @brief Select a random filter type from enabled mask
    /// @return Selected filter type
    SVFMode selectRandomType() noexcept {
        // Count enabled types
        int enabledCount = 0;
        for (int i = 0; i < 8; ++i) {
            if (enabledTypesMask_ & (1 << i)) {
                ++enabledCount;
            }
        }

        if (enabledCount == 0) {
            return SVFMode::Lowpass;  // Default fallback
        }

        // Select random index
        const int selectedIndex = static_cast<int>(rng_.nextUnipolar() *
                                  static_cast<float>(enabledCount));

        // Find the nth enabled type
        int count = 0;
        for (int i = 0; i < 8; ++i) {
            if (enabledTypesMask_ & (1 << i)) {
                if (count == selectedIndex) {
                    return static_cast<SVFMode>(i);
                }
                ++count;
            }
        }

        return SVFMode::Lowpass;  // Fallback
    }

    /// @brief Start a type transition to a new filter type (FR-008)
    /// @param newType The new filter type to transition to
    void startTypeTransition(SVFMode newType) noexcept {
        if (newType == currentTypeA_ || isTransitioning_) {
            return;  // Already at this type or already transitioning
        }

        // Set up filterB with the new type
        currentTypeB_ = newType;
        filterB_.setMode(newType);

        // Copy current parameters to filterB
        filterB_.setCutoff(cutoffSmoother_.getCurrentValue());
        filterB_.setResonance(resonanceSmoother_.getCurrentValue());

        // Start crossfade
        crossfadeSmoother_.setTarget(1.0f);
        isTransitioning_ = true;
    }
};

} // namespace DSP
} // namespace Krate
