// ==============================================================================
// Layer 2: DSP Processor - Granular Distortion
// ==============================================================================
// Time-windowed granular distortion with per-grain variation.
// Applies distortion in overlapping micro-grains (5-100ms) for evolving,
// textured destruction effects impossible with static waveshaping.
//
// Feature: 113-granular-distortion
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/grain_envelope.h, core/random.h, core/db_utils.h
//   - Layer 1: primitives/grain_pool.h, primitives/waveshaper.h, primitives/smoother.h
//   - Layer 2: processors/grain_scheduler.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 2 (depends on Layers 0-1 and same-layer scheduler)
// - Principle X: DSP Constraints (aliasing accepted as "Digital Destruction" aesthetic)
// - Principle XI: Performance Budget (< 0.5% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/113-granular-distortion/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/grain_pool.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/processors/grain_scheduler.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Granular distortion processor with per-grain variation.
///
/// Applies distortion in time-windowed micro-grains (5-100ms) with optional
/// per-grain drive variation, algorithm variation, and position jitter.
/// Creates evolving, textured "destruction" effects impossible with static
/// waveshaping.
///
/// @par Features
/// - 64 simultaneous grains with voice stealing
/// - 9 distortion algorithms (Tanh, Atan, Cubic, Tube, etc.)
/// - Per-grain drive randomization (0-100%)
/// - Per-grain algorithm randomization
/// - Position jitter for temporal smearing
/// - Click-free parameter automation via 10ms smoothing
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle X: Aliasing accepted as intentional aesthetic
///
/// @par Usage Example
/// @code
/// GranularDistortion gd;
/// gd.prepare(44100.0, 512);
/// gd.setGrainSize(50.0f);       // 50ms grains
/// gd.setGrainDensity(4.0f);     // ~4 overlapping grains
/// gd.setDrive(5.0f);            // Moderate drive
/// gd.setDriveVariation(0.5f);   // 50% drive variation
/// gd.setAlgorithmVariation(true);
/// gd.setMix(0.75f);
///
/// gd.process(buffer, numSamples);
/// @endcode
///
/// @see specs/113-granular-distortion/spec.md
class GranularDistortion {
public:
    // =========================================================================
    // Constants (FR-005, FR-008, FR-014, FR-021, FR-026)
    // =========================================================================

    static constexpr size_t kBufferSize = 32768;        ///< Circular buffer size (power of 2)
    static constexpr size_t kBufferMask = kBufferSize - 1;  ///< Bit mask for wraparound
    static constexpr size_t kEnvelopeTableSize = 2048;  ///< Envelope lookup table size

    static constexpr float kMinGrainSizeMs = 5.0f;
    static constexpr float kMaxGrainSizeMs = 100.0f;
    static constexpr float kMinDensity = 1.0f;
    static constexpr float kMaxDensity = 8.0f;
    static constexpr float kMinDrive = 1.0f;
    static constexpr float kMaxDrive = 20.0f;
    static constexpr float kMinPositionJitterMs = 0.0f;
    static constexpr float kMaxPositionJitterMs = 50.0f;
    static constexpr float kSmoothingTimeMs = 10.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    /// @post Object in unprepared state. Must call prepare() before processing.
    GranularDistortion() noexcept
        : rng_(12345) {
        // Initialize envelope table
        GrainEnvelope::generate(envelopeTable_.data(), kEnvelopeTableSize,
                                GrainEnvelopeType::Hann);
    }

    /// @brief Destructor.
    ~GranularDistortion() = default;

    // Non-copyable (contains non-copyable members)
    GranularDistortion(const GranularDistortion&) = delete;
    GranularDistortion& operator=(const GranularDistortion&) = delete;
    GranularDistortion(GranularDistortion&&) noexcept = default;
    GranularDistortion& operator=(GranularDistortion&&) noexcept = default;

    /// @brief Initialize for given sample rate (FR-001, FR-003).
    ///
    /// Prepares all internal components including grain pool, scheduler,
    /// envelope table, and circular buffer. Must be called before processing.
    /// Supports sample rates from 44100Hz to 192000Hz.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum expected block size (reserved for future use)
    /// @note NOT real-time safe (initializes arrays)
    void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Prepare components
        grainPool_.prepare(sampleRate);
        scheduler_.prepare(sampleRate);

        // Configure smoothers
        driveSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Initialize smoother values
        driveSmoother_.snapTo(baseDrive_);
        mixSmoother_.snapTo(mix_);

        // Update scheduler density
        updateSchedulerDensity();

        // Clear buffer
        buffer_.fill(0.0f);

        prepared_ = true;
    }

    /// @brief Clear all internal state without reallocation (FR-002).
    ///
    /// Resets circular buffer, all active grains, and smoothers.
    /// Does not change parameter values or sample rate.
    void reset() noexcept {
        // Clear buffer
        buffer_.fill(0.0f);
        writePos_ = 0;
        samplesWritten_ = 0;
        currentSample_ = 0;

        // Reset grain pool
        grainPool_.reset();

        // Reset scheduler
        scheduler_.reset();

        // Snap smoothers to current targets
        driveSmoother_.snapTo(baseDrive_);
        mixSmoother_.snapTo(mix_);

        // Reset grain states
        for (auto& state : grainStates_) {
            state = GrainState{};
        }

        // Reset instrumentation
        lastTriggeredGrainDrive_ = 0.0f;
        lastTriggeredGrainAlgorithm_ = WaveshapeType::Tanh;
        grainsTriggeredCount_ = 0;
    }

    // =========================================================================
    // Grain Size Control (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set grain window duration (FR-004).
    /// @param ms Grain size in milliseconds, clamped to [5.0, 100.0] (FR-005)
    /// @note Changes apply to newly triggered grains only (FR-006)
    void setGrainSize(float ms) noexcept {
        grainSizeMs_ = std::clamp(ms, kMinGrainSizeMs, kMaxGrainSizeMs);
        updateSchedulerDensity();
    }

    /// @brief Get current grain size in milliseconds.
    [[nodiscard]] float getGrainSize() const noexcept { return grainSizeMs_; }

    // =========================================================================
    // Grain Density Control (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Set grain density / overlap amount (FR-007).
    /// @param density Approximate simultaneous grains, clamped to [1.0, 8.0] (FR-008)
    /// @note Changes are click-free via scheduler smoothing (FR-009)
    void setGrainDensity(float density) noexcept {
        density_ = std::clamp(density, kMinDensity, kMaxDensity);
        updateSchedulerDensity();
    }

    /// @brief Get current grain density.
    [[nodiscard]] float getGrainDensity() const noexcept { return density_; }

    // =========================================================================
    // Distortion Type Control (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set base distortion algorithm (FR-010).
    /// @param type Waveshape type (Tanh, Atan, Cubic, Tube, etc.) (FR-011)
    /// @note Changes apply to newly triggered grains (FR-012)
    void setDistortionType(WaveshapeType type) noexcept {
        baseDistortionType_ = type;
    }

    /// @brief Get current base distortion type.
    [[nodiscard]] WaveshapeType getDistortionType() const noexcept {
        return baseDistortionType_;
    }

    // =========================================================================
    // Drive Control (FR-025, FR-026, FR-027)
    // =========================================================================

    /// @brief Set base drive / distortion intensity (FR-025).
    /// @param drive Drive amount, clamped to [1.0, 20.0] (FR-026)
    /// @note Changes are click-free via 10ms smoothing (FR-027)
    void setDrive(float drive) noexcept {
        baseDrive_ = std::clamp(drive, kMinDrive, kMaxDrive);
        driveSmoother_.setTarget(baseDrive_);
    }

    /// @brief Get current base drive.
    [[nodiscard]] float getDrive() const noexcept { return baseDrive_; }

    // =========================================================================
    // Drive Variation Control (FR-013, FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Set per-grain drive randomization amount (FR-013).
    /// @param amount Variation amount, clamped to [0.0, 1.0] (FR-014)
    void setDriveVariation(float amount) noexcept {
        driveVariation_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Get current drive variation amount.
    [[nodiscard]] float getDriveVariation() const noexcept { return driveVariation_; }

    // =========================================================================
    // Algorithm Variation Control (FR-017, FR-018, FR-019)
    // =========================================================================

    /// @brief Enable/disable per-grain algorithm randomization (FR-017).
    /// @param enabled true = random algorithm per grain (FR-018)
    void setAlgorithmVariation(bool enabled) noexcept {
        algorithmVariation_ = enabled;
    }

    /// @brief Get current algorithm variation state.
    [[nodiscard]] bool getAlgorithmVariation() const noexcept {
        return algorithmVariation_;
    }

    // =========================================================================
    // Position Jitter Control (FR-020, FR-021, FR-022, FR-023, FR-024-NEW)
    // =========================================================================

    /// @brief Set grain start position randomization (FR-020).
    /// @param ms Maximum jitter in milliseconds, clamped to [0.0, 50.0] (FR-021)
    void setPositionJitter(float ms) noexcept {
        positionJitterMs_ = std::clamp(ms, kMinPositionJitterMs, kMaxPositionJitterMs);
    }

    /// @brief Get current position jitter in milliseconds.
    [[nodiscard]] float getPositionJitter() const noexcept { return positionJitterMs_; }

    // =========================================================================
    // Mix Control (FR-028, FR-029, FR-030, FR-031)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-028).
    /// @param mix Mix amount, clamped to [0.0, 1.0] (FR-029)
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get current mix amount.
    [[nodiscard]] float getMix() const noexcept { return mix_; }

    // =========================================================================
    // Processing (FR-032, FR-033, FR-034, FR-035, FR-036)
    // =========================================================================

    /// @brief Process a single sample (FR-032).
    /// @param input Input sample (expected normalized [-1, 1])
    /// @return Processed output sample
    /// @note Real-time safe: noexcept, no allocations (FR-033)
    [[nodiscard]] float process(float input) noexcept {
        // FR-034: Handle invalid input
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // SC-008: Bypass optimization for mix=0 (bit-exact dry signal)
        // When mix target is exactly 0.0, skip all processing and return dry directly.
        // This ensures bit-exact output without waiting for smoother convergence.
        if (mix_ == 0.0f) {
            // Still need to maintain internal state for when mix changes
            buffer_[writePos_] = input;
            writePos_ = (writePos_ + 1) & kBufferMask;
            samplesWritten_ = std::min(samplesWritten_ + 1, kBufferSize);
            if (scheduler_.process()) {
                triggerGrain();
            }
            // Process grains to keep state consistent (but discard output)
            for (Grain* grain : grainPool_.activeGrains()) {
                (void)processGrain(grain);  // Intentionally discard [[nodiscard]] result
            }
            ++currentSample_;
            // Snap smoother to 0 so it's ready when mix changes
            mixSmoother_.snapTo(0.0f);
            return input;  // Bit-exact dry signal
        }

        // Store dry signal
        const float dry = input;

        // Write to circular buffer
        buffer_[writePos_] = input;
        writePos_ = (writePos_ + 1) & kBufferMask;
        samplesWritten_ = std::min(samplesWritten_ + 1, kBufferSize);

        // Check for grain trigger
        if (scheduler_.process()) {
            triggerGrain();
        }

        // Process all active grains
        float wet = 0.0f;
        for (Grain* grain : grainPool_.activeGrains()) {
            wet += processGrain(grain);
        }

        // Advance sample counter
        ++currentSample_;

        // Mix dry/wet (FR-031)
        const float smoothedMix = mixSmoother_.process();
        float output = (1.0f - smoothedMix) * dry + smoothedMix * wet;

        // FR-035: Flush denormals
        output = detail::flushDenormal(output);

        return output;
    }

    /// @brief Process buffer in-place (FR-032).
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr) return;

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get number of currently active grains.
    [[nodiscard]] size_t getActiveGrainCount() const noexcept {
        return grainPool_.activeCount();
    }

    /// @brief Get maximum grain capacity (always 64).
    [[nodiscard]] static constexpr size_t getMaxGrains() noexcept {
        return GrainPool::kMaxGrains;
    }

    /// @brief Seed RNG for reproducible behavior (testing only).
    void seed(uint32_t seedValue) noexcept {
        rng_.seed(seedValue);
        scheduler_.seed(seedValue);
    }

    /// @brief Get the drive value of the most recently triggered grain.
    /// @return Drive value in range [1.0, 20.0], or 0.0 if no grain triggered yet
    /// @note For testing SC-002: allows verification of per-grain drive variation
    [[nodiscard]] float getLastTriggeredGrainDrive() const noexcept {
        return lastTriggeredGrainDrive_;
    }

    /// @brief Get the algorithm type of the most recently triggered grain.
    /// @return WaveshapeType used by the last triggered grain
    /// @note For testing SC-003: allows verification of per-grain algorithm variation
    [[nodiscard]] WaveshapeType getLastTriggeredGrainAlgorithm() const noexcept {
        return lastTriggeredGrainAlgorithm_;
    }

    /// @brief Get count of grains triggered since last reset.
    /// @return Number of grains triggered
    /// @note For testing SC-002/SC-003: allows collecting enough samples
    [[nodiscard]] size_t getGrainsTriggeredCount() const noexcept {
        return grainsTriggeredCount_;
    }

private:
    // =========================================================================
    // Internal Struct
    // =========================================================================

    /// Per-grain additional state not in base Grain struct
    struct GrainState {
        float drive = 1.0f;             ///< Per-grain drive (after variation)
        size_t startBufferPos = 0;      ///< Frozen start position in buffer
        size_t grainSizeSamples = 0;    ///< Duration in samples
    };

    // =========================================================================
    // Helper Methods
    // =========================================================================

    /// Convert milliseconds to sample count
    [[nodiscard]] size_t msToSamples(float ms) const noexcept {
        return static_cast<size_t>(ms * static_cast<float>(sampleRate_) / 1000.0f);
    }

    /// Get grain index from pointer (for indexing parallel arrays)
    /// Note: This method is not const because activeGrains() is non-const
    [[nodiscard]] size_t getGrainIndex(const Grain* grain) noexcept {
        // grainPool_ exposes grains via activeGrains(), need to find index
        // The grains are stored in grainPool_'s internal array
        // We can calculate the index by processing order
        size_t index = 0;
        for (const Grain* g : grainPool_.activeGrains()) {
            if (g == grain) return index;
            ++index;
        }
        return 0;  // Fallback
    }

    /// Update scheduler density based on grain size and density parameter
    void updateSchedulerDensity() noexcept {
        // grainsPerSecond = density * 1000 / grainSizeMs
        const float grainsPerSecond = density_ * 1000.0f / grainSizeMs_;
        scheduler_.setDensity(grainsPerSecond);
    }

    /// Calculate per-grain drive with variation
    [[nodiscard]] float calculateGrainDrive(float smoothedDrive) noexcept {
        if (driveVariation_ <= 0.0f) {
            return smoothedDrive;
        }

        // Formula: baseDrive * (1 + variation * random[-1,1])
        const float variation = driveVariation_ * rng_.nextFloat();
        float grainDrive = smoothedDrive * (1.0f + variation);

        // Clamp to valid range (FR-015)
        return std::clamp(grainDrive, kMinDrive, kMaxDrive);
    }

    /// Select algorithm for grain (random if variation enabled)
    [[nodiscard]] WaveshapeType selectGrainAlgorithm() noexcept {
        if (!algorithmVariation_) {
            return baseDistortionType_;
        }

        // Random selection from 9 algorithms
        const int typeIndex = static_cast<int>(rng_.nextUnipolar() * 9.0f);
        return static_cast<WaveshapeType>(std::min(typeIndex, 8));
    }

    /// Calculate effective jitter clamped to available buffer history (FR-024-NEW)
    [[nodiscard]] size_t calculateEffectiveJitter() const noexcept {
        const size_t availableHistory = std::min(samplesWritten_, kBufferSize - 1);
        const size_t requestedJitter = msToSamples(positionJitterMs_);
        return std::min(requestedJitter, availableHistory);
    }

    /// Trigger a new grain
    void triggerGrain() noexcept {
        // Acquire grain (may steal oldest)
        Grain* grain = grainPool_.acquireGrain(currentSample_);
        if (grain == nullptr) return;

        // Find available grain state slot using grain's index in the pool
        // Since GrainPool uses a fixed array, we can find the index
        size_t grainIndex = 0;
        {
            // Find this grain's position in the pool by checking active grains
            // This is a bit awkward, but necessary since GrainPool doesn't expose indices
            size_t idx = 0;
            for (const Grain* g : grainPool_.activeGrains()) {
                if (g == grain) {
                    grainIndex = idx;
                    break;
                }
                ++idx;
            }
        }

        // Ensure we use a consistent index (mod 64 to stay in bounds)
        grainIndex = grainIndex % GrainPool::kMaxGrains;

        // Calculate per-grain drive
        const float smoothedDrive = driveSmoother_.process();
        const float grainDrive = calculateGrainDrive(smoothedDrive);

        // Record instrumentation for SC-002 testing
        lastTriggeredGrainDrive_ = grainDrive;
        ++grainsTriggeredCount_;

        // Configure waveshaper for this grain
        Waveshaper& ws = waveshapers_[grainIndex];
        ws.setDrive(grainDrive);
        ws.setAsymmetry(0.0f);  // Always symmetric
        const WaveshapeType grainAlgorithm = selectGrainAlgorithm();
        ws.setType(grainAlgorithm);

        // Record instrumentation for SC-003 testing
        lastTriggeredGrainAlgorithm_ = grainAlgorithm;

        // Calculate position jitter
        size_t jitterOffset = 0;
        if (positionJitterMs_ > 0.0f) {
            const size_t maxJitter = calculateEffectiveJitter();
            if (maxJitter > 0) {
                // Random offset in range [-maxJitter, +maxJitter]
                // but clamped to not go negative when applied
                const float jitterRandom = rng_.nextFloat();  // [-1, 1]
                const auto absJitter = static_cast<size_t>(
                    std::abs(jitterRandom) * static_cast<float>(maxJitter));
                jitterOffset = std::min(absJitter, samplesWritten_);
            }
        }

        // Store grain state
        GrainState& state = grainStates_[grainIndex];
        state.drive = grainDrive;
        // Start position: current write position minus 1, minus jitter
        const size_t basePos = (writePos_ > 0) ? writePos_ - 1 : kBufferSize - 1;
        state.startBufferPos = (basePos + kBufferSize - jitterOffset) & kBufferMask;
        state.grainSizeSamples = msToSamples(grainSizeMs_);

        // Initialize envelope
        grain->envelopePhase = 0.0f;
        grain->envelopeIncrement = (state.grainSizeSamples > 0)
            ? 1.0f / static_cast<float>(state.grainSizeSamples)
            : 1.0f;
    }

    /// Process a single grain and return its contribution
    [[nodiscard]] float processGrain(Grain* grain) noexcept {
        // Find grain index
        size_t grainIndex = 0;
        {
            size_t idx = 0;
            for (const Grain* g : grainPool_.activeGrains()) {
                if (g == grain) {
                    grainIndex = idx;
                    break;
                }
                ++idx;
            }
        }
        grainIndex = grainIndex % GrainPool::kMaxGrains;

        const GrainState& state = grainStates_[grainIndex];
        const Waveshaper& ws = waveshapers_[grainIndex];

        // Get envelope value
        const float envelope = GrainEnvelope::lookup(
            envelopeTable_.data(), kEnvelopeTableSize, grain->envelopePhase);

        // Calculate read position (frozen start + progress)
        const size_t progressSamples = static_cast<size_t>(
            grain->envelopePhase * static_cast<float>(state.grainSizeSamples));
        const size_t readPos = (state.startBufferPos + progressSamples) & kBufferMask;

        // Read from buffer
        const float bufferSample = buffer_[readPos];

        // Apply waveshaper
        const float distorted = ws.process(bufferSample);

        // Apply envelope
        float output = distorted * envelope;

        // Flush denormals (FR-035)
        output = detail::flushDenormal(output);

        // Advance envelope phase
        grain->envelopePhase += grain->envelopeIncrement;

        // Release if complete
        if (grain->envelopePhase >= 1.0f) {
            grainPool_.releaseGrain(grain);
        }

        return output;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Components
    GrainPool grainPool_;
    GrainScheduler scheduler_;
    std::array<Waveshaper, GrainPool::kMaxGrains> waveshapers_;
    std::array<GrainState, GrainPool::kMaxGrains> grainStates_{};

    // Circular buffer
    std::array<float, kBufferSize> buffer_{};

    // Envelope table
    std::array<float, kEnvelopeTableSize> envelopeTable_{};

    // RNG
    Xorshift32 rng_;

    // Smoothers
    OnePoleSmoother driveSmoother_;
    OnePoleSmoother mixSmoother_;

    // State
    size_t writePos_ = 0;
    size_t samplesWritten_ = 0;
    size_t currentSample_ = 0;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Instrumentation (for testing SC-002, SC-003)
    float lastTriggeredGrainDrive_ = 0.0f;
    WaveshapeType lastTriggeredGrainAlgorithm_ = WaveshapeType::Tanh;
    size_t grainsTriggeredCount_ = 0;

    // Parameters
    float grainSizeMs_ = 50.0f;
    float density_ = 4.0f;
    float baseDrive_ = 5.0f;
    float driveVariation_ = 0.0f;
    float positionJitterMs_ = 0.0f;
    float mix_ = 1.0f;
    WaveshapeType baseDistortionType_ = WaveshapeType::Tanh;
    bool algorithmVariation_ = false;
};

}  // namespace DSP
}  // namespace Krate
