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
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// The block path's Tanh grains in SSE2 (runGrains). Not where FMA may fuse the scalar curve's
// products and sums, which the SSE2 lanes would then not match.
#if (defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && \
    !defined(__FMA__)
#define KRATE_GRANULAR_TANH_SSE2 1
#include <emmintrin.h>
#else
#define KRATE_GRANULAR_TANH_SSE2 0
#endif

namespace Krate::DSP {

#if KRATE_GRANULAR_TANH_SSE2
namespace detail {
/// @brief FastMath::fastTanh over @p x[0, n) in place, four at a time in SSE2: the Pade (5,4)
/// ratio x (945 + 105 x^2 + x^4) / (945 + 420 x^2 + 15 x^4), each an IEEE-exact operation in
/// the scalar order, and +1 / -1 from +3.5 / -3.5 on (infinities included), selected per lane;
/// the remainder through fastTanh itself. MSVC /fp:fast rewrites intrinsic arithmetic too
/// (measured: 945 moved last and the division turned into a reciprocal product, 1 ulp off the
/// scalar curve on a third of the inputs), so the function is compiled precise there: the
/// intrinsics then run as written, and the scalar curve's /fp:fast code is this order.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
inline void granularFastTanhSse2(float* x, size_t n) noexcept {
    const __m128 c945 = _mm_set1_ps(945.0f);
    const __m128 c105 = _mm_set1_ps(105.0f);
    const __m128 c420 = _mm_set1_ps(420.0f);
    const __m128 c15 = _mm_set1_ps(15.0f);
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 minusOne = _mm_set1_ps(-1.0f);
    const __m128 hiEdge = _mm_set1_ps(3.5f);
    const __m128 loEdge = _mm_set1_ps(-3.5f);
    size_t k = 0;
    for (; k + 4 <= n; k += 4) {
        const __m128 v = _mm_loadu_ps(x + k);
        const __m128 x2 = _mm_mul_ps(v, v);
        const __m128 x4 = _mm_mul_ps(x2, x2);
        const __m128 num = _mm_add_ps(_mm_add_ps(c945, _mm_mul_ps(c105, x2)), x4);
        const __m128 den = _mm_add_ps(_mm_add_ps(c945, _mm_mul_ps(c420, x2)), _mm_mul_ps(c15, x4));
        const __m128 pade = _mm_div_ps(_mm_mul_ps(v, num), den);
        const __m128 hi = _mm_cmpge_ps(v, hiEdge);
        const __m128 lo = _mm_cmple_ps(v, loEdge);
        const __m128 inner = _mm_andnot_ps(_mm_or_ps(hi, lo), pade);
        _mm_storeu_ps(x + k, _mm_or_ps(inner, _mm_or_ps(_mm_and_ps(hi, one), _mm_and_ps(lo, minusOne))));
    }
    for (; k < n; ++k) x[k] = FastMath::fastTanh(x[k]);
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif
}  // namespace detail
#endif

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
        : rng_(kDefaultSeed) {
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

        // Restart the random sequences (seed(), default kDefaultSeed): the output does not
        // depend on how often prepare() or reset() ran before
        rng_.seed(seedValue_);
        scheduler_.seed(seedValue_);

        // Prepare components
        grainPool_.prepare(sampleRate);
        activeSlots_ = 0;
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
        // Restart the random sequences (seed(), default kDefaultSeed)
        rng_.seed(seedValue_);
        scheduler_.seed(seedValue_);

        // Clear buffer
        buffer_.fill(0.0f);
        writePos_ = 0;
        samplesWritten_ = 0;
        currentSample_ = 0;

        // Reset grain pool
        grainPool_.reset();
        activeSlots_ = 0;

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
        debugLastProcessedGrainCount_ = 0;
        debugLastActiveGrainCount_ = 0;
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
    // Grain Position Control
    // =========================================================================

    /// @brief Set grain read position as lookback into the circular buffer.
    /// @param normalized Position in [0, 1] where 0 = most recent audio, 1 = oldest in buffer
    void setPosition(float normalized) noexcept {
        position_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    /// @brief Get current grain position.
    [[nodiscard]] float getPosition() const noexcept { return position_; }

    // =========================================================================
    // Curve Control
    // =========================================================================

    /// @brief Set distortion curve (power reshaping of waveshaper output).
    /// @param normalized Curve in [0, 1] where 0.5 = linear, <0.5 = expand, >0.5 = compress
    void setCurve(float normalized) noexcept {
        curve_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    /// @brief Get current curve value.
    [[nodiscard]] float getCurve() const noexcept { return curve_; }

    // =========================================================================
    // Envelope Type Control
    // =========================================================================

    /// @brief Set grain envelope type.
    /// @param type Envelope shape (Hann, Trapezoid, Sine, Blackman)
    void setEnvelopeType(GrainEnvelopeType type) noexcept {
        if (type != envelopeType_) {
            envelopeType_ = type;
            GrainEnvelope::generate(envelopeTable_.data(), kEnvelopeTableSize, type);
        }
    }

    /// @brief Get current envelope type.
    [[nodiscard]] GrainEnvelopeType getEnvelopeType() const noexcept { return envelopeType_; }

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
            debugLastProcessedGrainCount_ = 0;
            debugLastActiveGrainCount_ = grainPool_.activeCount();
            // Iterate a snapshot: a grain released inside processGrain must not shift the list
            for (uint64_t pending = activeSlots_; pending != 0; pending &= pending - 1) {
                // Intentionally discard [[nodiscard]] result
                (void)processGrain(grainPool_.grainAt(static_cast<size_t>(std::countr_zero(pending))));
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
        debugLastProcessedGrainCount_ = 0;
        debugLastActiveGrainCount_ = grainPool_.activeCount();
        // The active grains in slot order (GrainPool::activeGrains' order), from a snapshot of the
        // slot mask: a grain released inside processGrain does not change which grains this sample runs
        for (uint64_t pending = activeSlots_; pending != 0; pending &= pending - 1) {
            wet += processGrain(grainPool_.grainAt(static_cast<size_t>(std::countr_zero(pending))));
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
    ///
    /// The same result as process(float) per sample. Chunks of finite samples at a non-zero Mix
    /// run grain by grain (processChunk); the debug grain records describe process(float) calls
    /// only and are left as the last of those set them.
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr) return;

        size_t i = 0;
        while (i < numSamples) {
            const size_t len = std::min(kBlockChunk, numSamples - i);
            if (mix_ != 0.0f && allFinite(buffer + i, len)) {
                processChunk(buffer + i, len);
            } else {
                for (size_t k = 0; k < len; ++k) buffer[i + k] = process(buffer[i + k]);
            }
            i += len;
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
        seedValue_ = seedValue;
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

    /// One processGrain call of the most recent process(float) call (testing only).
    struct GrainReadRecord {
        size_t slot = 0;             ///< Grain's stable pool slot
        size_t startBufferPos = 0;   ///< Start position read from the grain's state
        size_t readPos = 0;          ///< Buffer position read this sample
        float envelopePhase = 0.0f;  ///< Phase before this sample's advance (0 = first sample)
    };

    /// @brief Grains processed by the most recent process(float) call, in call order.
    /// @note For testing D-074: each grain's read position must advance by 1 per sample
    [[nodiscard]] std::span<const GrainReadRecord> debugGrainReadPositions() const noexcept {
        return {debugGrainReads_.data(), debugLastProcessedGrainCount_};
    }

    /// @brief Number of processGrain calls in the most recent process(float) call.
    [[nodiscard]] size_t debugLastProcessedGrainCount() const noexcept {
        return debugLastProcessedGrainCount_;
    }

    /// @brief Active grains when the most recent process(float) call entered its grain loop.
    [[nodiscard]] size_t debugLastActiveGrainCount() const noexcept {
        return debugLastActiveGrainCount_;
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
        size_t progressSamples = 0;     ///< Samples read so far (exact, no float drift)
    };

    // =========================================================================
    // Helper Methods
    // =========================================================================

    /// Convert milliseconds to sample count
    [[nodiscard]] size_t msToSamples(float ms) const noexcept {
        return static_cast<size_t>(ms * static_cast<float>(sampleRate_) / 1000.0f);
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

        // Per-grain state is indexed by the grain's pool slot, stable for its whole life
        const size_t grainIndex = grainPool_.indexOf(grain);
        activeSlots_ |= uint64_t{1} << grainIndex;  // a stolen grain's slot is set already

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
        // Start position: current write position minus lookback, minus jitter
        // position_ [0,1] maps to lookback into available buffer history
        const size_t availableHistory = std::min(samplesWritten_, kBufferSize - 1);
        const size_t positionLookback = static_cast<size_t>(position_ * static_cast<float>(availableHistory));
        const size_t basePos = (writePos_ + kBufferSize - 1 - positionLookback) & kBufferMask;
        state.startBufferPos = (basePos + kBufferSize - jitterOffset) & kBufferMask;
        state.grainSizeSamples = msToSamples(grainSizeMs_);
        state.progressSamples = 0;

        // Initialize envelope
        grain->envelopePhase = 0.0f;
        grain->envelopeIncrement = (state.grainSizeSamples > 0)
            ? 1.0f / static_cast<float>(state.grainSizeSamples)
            : 1.0f;
    }

    /// Samples per grain-major chunk of the block path (processChunk).
    static constexpr size_t kBlockChunk = 64;

    /// True when every sample of @p x[0, n) is finite (process(float) resets on any other).
    [[nodiscard]] static bool allFinite(const float* x, size_t n) noexcept {
        for (size_t k = 0; k < n; ++k) {
            if (!detail::isFinite(x[k])) return false;
        }
        return true;
    }

    /// @brief process(float) over @p n <= kBlockChunk finite samples at a non-zero Mix, grain by
    /// grain (CPU, Disrumpo round 5). The chunk splits at the samples where the scheduler triggers
    /// a grain, which there acquires (or steals) a pool slot and starts. Between triggers each
    /// active grain runs its samples in one loop (runGrains), the grains in ascending slot order,
    /// so every sample sums its grains in process(float)'s order. The chunk's input reaches the
    /// circular buffer after the grains have run: a grain's read of a position the chunk writes at
    /// or before that sample takes the chunk's input, any other read the buffer as it was.
    void processChunk(float* x, size_t n) noexcept {
        // The scheduler and the mix smoother run on copies in locals (their recursions then stay in
        // registers), written back after their loops (round 6 CPU)
        std::array<bool, kBlockChunk> trigger{};
        GrainScheduler scheduler = scheduler_;
        for (size_t k = 0; k < n; ++k) trigger[k] = scheduler.process();
        scheduler_ = scheduler;
        std::array<float, kBlockChunk> wet{};
        const size_t writePos0 = writePos_;
        const size_t written0 = samplesWritten_;
        const size_t sample0 = currentSample_;
        size_t segmentStart = 0;
        for (size_t k = 0; k <= n; ++k) {
            if (k < n && !trigger[k]) continue;
            runGrains(x, wet.data(), writePos0, segmentStart, k);
            if (k == n) break;
            // Sample k's state at its trigger: its input written, the sample counter not yet advanced
            writePos_ = (writePos0 + k + 1) & kBufferMask;
            samplesWritten_ = std::min(written0 + k + 1, kBufferSize);
            currentSample_ = sample0 + k;
            triggerGrain();
            segmentStart = k;
        }
        for (size_t k = 0; k < n; ++k) buffer_[(writePos0 + k) & kBufferMask] = x[k];
        writePos_ = (writePos0 + n) & kBufferMask;
        samplesWritten_ = std::min(written0 + n, kBufferSize);
        currentSample_ = sample0 + n;
        OnePoleSmoother mixSmoother = mixSmoother_;
        for (size_t k = 0; k < n; ++k) {
            const float smoothedMix = mixSmoother.process();
            x[k] = detail::flushDenormal((1.0f - smoothedMix) * x[k] + smoothedMix * wet[k]);
        }
        mixSmoother_ = mixSmoother;
    }

    /// @brief Every active grain over the chunk samples [@p begin, @p end), added to @p wet: per
    /// sample processGrain()'s operations in their order, without its debug record. @p in is the
    /// chunk's input, which the buffer receives from @p writePos0 on after the chunk.
    void runGrains(const float* in, float* wet, size_t writePos0, size_t begin, size_t end) noexcept {
        if (begin >= end) return;
        // Each grain's running state (envelope phase, read progress) and its waveshaper run on
        // copies in locals, written back when the grain stops: a store to wet[] then forces no reload
        // of them, and the phase recursion stays in a register (round 6 CPU)
        const float* table = envelopeTable_.data();
        const bool reshape = curve_ != 0.5f;
        for (uint64_t pending = activeSlots_; pending != 0; pending &= pending - 1) {
            const auto grainIndex = static_cast<size_t>(std::countr_zero(pending));
            Grain* grain = grainPool_.grainAt(grainIndex);
            GrainState& state = grainStates_[grainIndex];
            const Waveshaper ws = waveshapers_[grainIndex];
            const size_t startPos = state.startBufferPos;
            size_t progress = state.progressSamples;
            float phase = grain->envelopePhase;
            const float increment = grain->envelopeIncrement;
            bool released = false;
#if KRATE_GRANULAR_TANH_SSE2
            if (!reshape && ws.getType() == WaveshapeType::Tanh && ws.getDrive() != 0.0f) {
                // Tanh grains (the default curve) in spans: per sample the envelope, the read and
                // Waveshaper::process's drive * x + asymmetry, then the curve four samples at a
                // time (detail::granularFastTanhSse2), then the envelope product and the flush;
                // the same values per sample, added to wet[] in the same grain order (round 6 CPU)
                const float drive = ws.getDrive();
                const float asymmetry = ws.getAsymmetry();
                constexpr size_t kSpan = 16;
                std::array<float, kSpan> shaped{};
                std::array<float, kSpan> envelope{};
                size_t k = begin;
                while (k < end && !released) {
                    const size_t limit = std::min(kSpan, end - k);
                    size_t m = 0;
                    while (m < limit) {
                        envelope[m] = GrainEnvelope::lookup(table, kEnvelopeTableSize, phase);
                        const size_t readPos = (startPos + progress) & kBufferMask;
                        ++progress;
                        const size_t writer = (readPos - writePos0) & kBufferMask;
                        const float bufferSample = writer <= k + m ? in[writer] : buffer_[readPos];
                        shaped[m] = drive * bufferSample + asymmetry;
                        ++m;
                        phase += increment;
                        if (phase >= 1.0f) {
                            released = true;
                            break;
                        }
                    }
                    detail::granularFastTanhSse2(shaped.data(), m);
                    for (size_t j = 0; j < m; ++j) {
                        wet[k + j] += detail::flushDenormal(shaped[j] * envelope[j]);
                    }
                    k += m;
                }
            } else
#endif
            {
                for (size_t k = begin; k < end; ++k) {
                    const float envelope = GrainEnvelope::lookup(table, kEnvelopeTableSize, phase);
                    const size_t readPos = (startPos + progress) & kBufferMask;
                    ++progress;
                    // The chunk sample that writes readPos; it holds this sample's buffer value
                    // when written at or before sample k
                    const size_t writer = (readPos - writePos0) & kBufferMask;
                    const float bufferSample = writer <= k ? in[writer] : buffer_[readPos];
                    float distorted = ws.process(bufferSample);
                    if (reshape) {
                        const float exponent = std::pow(10.0f, (curve_ - 0.5f) * 2.0f);
                        const float sign = (distorted >= 0.0f) ? 1.0f : -1.0f;
                        distorted = sign * std::pow(std::abs(distorted), exponent);
                    }
                    wet[k] += detail::flushDenormal(distorted * envelope);
                    phase += increment;
                    if (phase >= 1.0f) {
                        released = true;
                        break;
                    }
                }
            }
            state.progressSamples = progress;
            grain->envelopePhase = phase;
            if (released) {
                grainPool_.releaseGrain(grain);
                activeSlots_ &= ~(uint64_t{1} << grainIndex);
            }
        }
    }

    /// Process a single grain and return its contribution
    [[nodiscard]] float processGrain(Grain* grain) noexcept {
        const size_t grainIndex = grainPool_.indexOf(grain);
        GrainState& state = grainStates_[grainIndex];
        const Waveshaper& ws = waveshapers_[grainIndex];

        // Get envelope value
        const float envelope = GrainEnvelope::lookup(
            envelopeTable_.data(), kEnvelopeTableSize, grain->envelopePhase);

        // Calculate read position (frozen start + progress). An integer count, not
        // envelopePhase * size: the float product floors to k-1 on some samples.
        const size_t readPos = (state.startBufferPos + state.progressSamples) & kBufferMask;
        ++state.progressSamples;

        if (debugLastProcessedGrainCount_ < GrainPool::kMaxGrains) {
            debugGrainReads_[debugLastProcessedGrainCount_++] = GrainReadRecord{
                .slot = grainIndex,
                .startBufferPos = state.startBufferPos,
                .readPos = readPos,
                .envelopePhase = grain->envelopePhase};
        }

        // Read from buffer
        const float bufferSample = buffer_[readPos];

        // Apply waveshaper
        float distorted = ws.process(bufferSample);

        // Apply curve reshaping: maps [0,1] → exponent [0.2, 5.0]
        // 0.5 = exponent 1.0 (linear), <0.5 = expand (softer), >0.5 = compress (harder)
        if (curve_ != 0.5f) {
            const float exponent = std::pow(10.0f, (curve_ - 0.5f) * 2.0f);  // [0.1, 10] range
            const float sign = (distorted >= 0.0f) ? 1.0f : -1.0f;
            distorted = sign * std::pow(std::abs(distorted), exponent);
        }

        // Apply envelope
        float output = distorted * envelope;

        // Flush denormals (FR-035)
        output = detail::flushDenormal(output);

        // Advance envelope phase
        grain->envelopePhase += grain->envelopeIncrement;

        // Release if complete
        if (grain->envelopePhase >= 1.0f) {
            grainPool_.releaseGrain(grain);
            activeSlots_ &= ~(uint64_t{1} << grainIndex);
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
    /// Bit i set while pool slot i holds an active grain (GrainPool::kMaxGrains == 64): the
    /// per-sample grain loop walks it instead of rebuilding GrainPool::activeGrains() from 64 slots
    uint64_t activeSlots_ = 0;
    static_assert(GrainPool::kMaxGrains == 64, "activeSlots_ holds one bit per pool slot");

    // Circular buffer
    std::array<float, kBufferSize> buffer_{};

    // Envelope table
    std::array<float, kEnvelopeTableSize> envelopeTable_{};

    // RNG (and the scheduler's), restarted at seedValue_ by prepare() and reset()
    static constexpr uint32_t kDefaultSeed = 12345;
    uint32_t seedValue_ = kDefaultSeed;
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
    std::array<GrainReadRecord, GrainPool::kMaxGrains> debugGrainReads_{};
    size_t debugLastProcessedGrainCount_ = 0;
    size_t debugLastActiveGrainCount_ = 0;

    // Parameters
    float grainSizeMs_ = 50.0f;
    float density_ = 4.0f;
    float baseDrive_ = 5.0f;
    float driveVariation_ = 0.0f;
    float positionJitterMs_ = 0.0f;
    float position_ = 0.0f;
    float curve_ = 0.5f;
    float mix_ = 1.0f;
    WaveshapeType baseDistortionType_ = WaveshapeType::Tanh;
    GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;
    bool algorithmVariation_ = false;
};

} // namespace Krate::DSP

