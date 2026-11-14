// ==============================================================================
// Layer 2: DSP Processor - Fractal Distortion
// ==============================================================================
// Recursive multi-scale distortion processor with self-similar harmonic structure.
// Implements five modes (Residual, Multiband, Harmonic, Cascade, Feedback) of
// fractal-inspired distortion where each iteration level contributes progressively
// smaller amplitude content, creating complex evolving harmonic structures.
//
// Feature: 114-fractal-distortion
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/sigmoid.h, core/db_utils.h
//   - Layer 1: primitives/waveshaper.h, primitives/biquad.h, primitives/dc_blocker.h,
//              primitives/smoother.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 2 (depends on Layers 0-1)
// - Principle X: DSP Constraints (aliasing accepted as "Digital Destruction" aesthetic)
// - Principle XI: Performance Budget (< 0.5% CPU for 8 iterations at 44.1kHz)
// - Principle XII: Test-First Development
//
// Reference: specs/114-fractal-distortion/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/chebyshev_shaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/processors/crossover_filter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// The Harmonic block path's Clenshaw recurrences and ReciprocalSqrt curve in SSE2
// (harmonicLevelChunk). Not where FMA may fuse the scalar c + 2x * b1 or x * x + 1, which the SSE2
// lanes would then not match.
#if (defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && \
    !defined(__FMA__)
#define KRATE_FRACTAL_CURVE_SSE2 1
#include <emmintrin.h>
#else
#define KRATE_FRACTAL_CURVE_SSE2 0
#endif


namespace Krate::DSP {

#if KRATE_FRACTAL_CURVE_SSE2
namespace detail {
/// @brief Chebyshev::harmonicMix(x[k], weights, numWeights) for eight samples (two SSE2 vectors),
/// the Clenshaw recurrence's state in registers: per lane c + 2x * b1 - b2, then x * b1 - b2,
/// the scalar operations in their order, each IEEE-exact, so every lane is the scalar result
/// (FractalDistortion's Harmonic block path, round 6 CPU). MSVC /fp:fast reorders intrinsic
/// arithmetic too, so the function is compiled precise there and the intrinsics run as written.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
inline void fractalChebyshev8Sse2(const float* weights, int numWeights, const float* x, float* out) noexcept {
    const __m128 two = _mm_set1_ps(2.0f);
    const __m128 x0 = _mm_loadu_ps(x);
    const __m128 x1 = _mm_loadu_ps(x + 4);
    const __m128 twoX0 = _mm_mul_ps(two, x0);
    const __m128 twoX1 = _mm_mul_ps(two, x1);
    __m128 b10 = _mm_setzero_ps();
    __m128 b11 = _mm_setzero_ps();
    __m128 b20 = _mm_setzero_ps();
    __m128 b21 = _mm_setzero_ps();
    for (int h = numWeights; h >= 1; --h) {
        const __m128 c = _mm_set1_ps(weights[h - 1]);
        const __m128 n0 = _mm_sub_ps(_mm_add_ps(c, _mm_mul_ps(twoX0, b10)), b20);
        const __m128 n1 = _mm_sub_ps(_mm_add_ps(c, _mm_mul_ps(twoX1, b11)), b21);
        b20 = b10;
        b21 = b11;
        b10 = n0;
        b11 = n1;
    }
    _mm_storeu_ps(out, _mm_sub_ps(_mm_mul_ps(x0, b10), b20));
    _mm_storeu_ps(out + 4, _mm_sub_ps(_mm_mul_ps(x1, b11), b21));
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif

/// @brief The Harmonic level's ReciprocalSqrt curves and mix for the first @p n (a multiple of 4)
/// samples, four at a time: per lane drive * x + asymmetry, x / sqrt(x * x + 1), flushDenormal,
/// then (o + e) * 0.5, the scalar operations in their order, each IEEE-exact, so every lane is the
/// scalar result (round 5 CPU). MSVC /fp:fast reorders intrinsic arithmetic too, so the function
/// is compiled precise there and the intrinsics run as written.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
inline void fractalRecipSqrtMixSse2(const float* odd, float oddDriveS, float oddAsymS, const float* even,
                                    float evenDriveS, float evenAsymS, float* out, size_t n) noexcept {
    const __m128 oddDrive = _mm_set1_ps(oddDriveS);
    const __m128 oddAsym = _mm_set1_ps(oddAsymS);
    const __m128 evenDrive = _mm_set1_ps(evenDriveS);
    const __m128 evenAsym = _mm_set1_ps(evenAsymS);
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 half = _mm_set1_ps(0.5f);
    const __m128 posThr = _mm_set1_ps(kDenormalThreshold);
    const __m128 negThr = _mm_set1_ps(-kDenormalThreshold);
    const auto curve = [&](__m128 x, __m128 drive, __m128 asym) {
        const __m128 t = _mm_add_ps(_mm_mul_ps(drive, x), asym);
        const __m128 y = _mm_div_ps(t, _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(t, t), one)));
        const __m128 tiny = _mm_and_ps(_mm_cmpgt_ps(y, negThr), _mm_cmplt_ps(y, posThr));
        return _mm_andnot_ps(tiny, y);
    };
    for (size_t k = 0; k + 4 <= n; k += 4) {
        const __m128 o = curve(_mm_loadu_ps(odd + k), oddDrive, oddAsym);
        const __m128 e = curve(_mm_loadu_ps(even + k), evenDrive, evenAsym);
        _mm_storeu_ps(out + k, _mm_mul_ps(_mm_add_ps(o, e), half));
    }
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif
}  // namespace detail
#endif

// =============================================================================
// FractalMode Enumeration (FR-004 to FR-009)
// =============================================================================

/// @brief Processing algorithm modes for FractalDistortion.
///
/// Each mode implements a different approach to recursive distortion:
/// - Residual: Classic residual-based recursion (default)
/// - Multiband: Octave-band splitting with scaled iterations
/// - Harmonic: Odd/even harmonic separation via Chebyshev polynomials
/// - Cascade: Different waveshaper type per iteration level
/// - Feedback: Cross-level feedback for chaotic textures
enum class FractalMode : uint8_t {
    Residual = 0,   ///< Classic residual-based recursion (FR-005)
    Multiband = 1,  ///< Octave-band splitting with scaled iterations (FR-006)
    Harmonic = 2,   ///< Odd/even harmonic separation (FR-007)
    Cascade = 3,    ///< Different waveshaper per level (FR-008)
    Feedback = 4    ///< Cross-level feedback with delay (FR-009)
};

/// @brief Blend modes for combining fractal iteration levels.
///
/// Controls how the per-iteration level outputs are combined into the final
/// wet signal. Each mode produces a distinct harmonic character:
/// - Add: Linear sum (default, preserves fractal decomposition)
/// - Multiply: Ring-modulation style, each level modulates the accumulator
/// - Max: Peak selection, only the loudest level is heard
/// - Screen: Soft union that avoids clipping (audio analog of screen blend)
enum class FractalBlendMode : uint8_t {
    Add = 0,       ///< Linear sum of all levels (default)
    Multiply = 1,  ///< Cascaded ring modulation between levels
    Max = 2,       ///< Peak selection — loudest level wins
    Screen = 3     ///< Soft combination: a + b - a*b (avoids clipping)
};

// =============================================================================
// FractalDistortion Class (FR-001 to FR-050)
// =============================================================================

/// @brief Recursive multi-scale distortion processor with self-similar harmonics.
///
/// Implements fractal-inspired distortion where each iteration level contributes
/// progressively smaller amplitude content, creating complex evolving harmonic
/// structures. Supports five processing modes with configurable iteration depth,
/// scale factor, drive, and mix parameters.
///
/// @par Features
/// - 5 processing modes (Residual, Multiband, Harmonic, Cascade, Feedback)
/// - 1-8 iteration levels with exponential amplitude scaling
/// - Per-level frequency decay (progressive highpass filtering)
/// - Click-free parameter automation via 10ms smoothing
/// - DC blocking after asymmetric saturation
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle X: Aliasing accepted as intentional aesthetic
///
/// @par Usage Example
/// @code
/// FractalDistortion fractal;
/// fractal.prepare(44100.0, 512);
/// fractal.setMode(FractalMode::Residual);
/// fractal.setIterations(4);
/// fractal.setScaleFactor(0.5f);
/// fractal.setDrive(2.0f);
/// fractal.setMix(0.75f);
///
/// float output = fractal.process(inputSample);
/// // Or block processing:
/// fractal.process(buffer, numSamples);
/// @endcode
///
/// @see specs/114-fractal-distortion/spec.md
class FractalDistortion {
public:
    // =========================================================================
    // Constants (FR-011, FR-014, FR-017, FR-020, FR-024, FR-032, FR-042)
    // =========================================================================

    static constexpr int kMaxIterations = 8;              ///< Maximum iteration depth
    static constexpr int kNumBands = 4;                   ///< Number of multiband bands
    static constexpr int kMinIterations = 1;              ///< Minimum iterations (FR-011)
    static constexpr float kMinScaleFactor = 0.3f;        ///< Minimum scale factor (FR-014)
    static constexpr float kMaxScaleFactor = 0.9f;        ///< Maximum scale factor (FR-014)
    static constexpr float kMinDrive = 1.0f;              ///< Minimum drive (FR-017)
    static constexpr float kMaxDrive = 20.0f;             ///< Maximum drive (FR-017)
    static constexpr float kMinMix = 0.0f;                ///< Minimum mix (FR-020)
    static constexpr float kMaxMix = 1.0f;                ///< Maximum mix (FR-020)
    static constexpr float kMinFrequencyDecay = 0.0f;     ///< Minimum frequency decay (FR-024)
    static constexpr float kMaxFrequencyDecay = 1.0f;     ///< Maximum frequency decay (FR-024)
    static constexpr float kMinFeedbackAmount = 0.0f;     ///< Minimum feedback (FR-042)
    static constexpr float kMaxFeedbackAmount = 0.5f;     ///< Maximum feedback (FR-042)
    static constexpr float kDefaultCrossoverFrequency = 250.0f;  ///< Default crossover (FR-031)
    static constexpr float kDefaultBandIterationScale = 0.5f;    ///< Default band scale (FR-032)
    static constexpr float kBaseDecayFrequency = 200.0f;  ///< Base frequency for decay (FR-025)
    static constexpr float kSmoothingTimeMs = 10.0f;      ///< Parameter smoothing time (FR-018)

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    /// @post Object in unprepared state. Must call prepare() before processing.
    FractalDistortion() noexcept {
        // Initialize waveshapers array with default Tanh type
        for (auto& ws : waveshapers_) {
            ws.setType(WaveshapeType::Tanh);
            ws.setDrive(1.0f);
            ws.setAsymmetry(0.0f);
        }
        // Initialize level waveshaper types for Cascade mode
        levelWaveshapers_.fill(WaveshapeType::Tanh);
        // Initialize feedback buffer
        feedbackBuffer_.fill(0.0f);
    }

    /// @brief Destructor.
    ~FractalDistortion() = default;

    // Non-copyable and non-movable due to internal state.
    FractalDistortion(const FractalDistortion&) = delete;
    FractalDistortion& operator=(const FractalDistortion&) = delete;
    FractalDistortion(FractalDistortion&&) = delete;
    FractalDistortion& operator=(FractalDistortion&&) = delete;

    /// @brief Initialize for given sample rate (FR-001, FR-003).
    ///
    /// Prepares all internal components including smoothers, DC blocker,
    /// and frequency decay filters. Must be called before processing.
    /// Supports sample rates from 44100Hz to 192000Hz.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to [44100, 192000])
    /// @param maxBlockSize Maximum expected block size (reserved for future use)
    /// @note NOT real-time safe (initializes components)
    void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
        // Clamp sample rate to valid range (FR-003)
        sampleRate_ = std::clamp(sampleRate, 44100.0, 192000.0);

        // Configure smoothers (FR-018, FR-022)
        driveSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate_));
        mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate_));

        // Initialize smoother values
        driveSmoother_.snapTo(drive_);
        mixSmoother_.snapTo(mix_);

        // Configure DC blocker (FR-050)
        dcBlocker_.prepare(sampleRate_, 10.0f);

        // Configure frequency decay filters (FR-025)
        updateDecayFilters();

        // Configure crossover for Multiband mode (FR-030): frequencies first,
        // then prepare, which snaps to them (no glide)
        updateCrossoverFrequencies();
        crossover_.setAllpassCompensation(true);
        crossover_.prepare(sampleRate_);

        // Configure ChebyshevShapers for Harmonic mode (FR-034, FR-035)
        // Odd harmonics: T1, T3, T5, T7 (levels 0.5, 0.3, 0.2, 0.1)
        oddHarmonicShaper_.setHarmonicLevel(1, 0.5f);
        oddHarmonicShaper_.setHarmonicLevel(3, 0.3f);
        oddHarmonicShaper_.setHarmonicLevel(5, 0.2f);
        oddHarmonicShaper_.setHarmonicLevel(7, 0.1f);

        // Even harmonics: T2, T4, T6, T8 (levels 0.5, 0.3, 0.2, 0.1)
        evenHarmonicShaper_.setHarmonicLevel(2, 0.5f);
        evenHarmonicShaper_.setHarmonicLevel(4, 0.3f);
        evenHarmonicShaper_.setHarmonicLevel(6, 0.2f);
        evenHarmonicShaper_.setHarmonicLevel(8, 0.1f);

        prepared_ = true;
    }

    /// @brief Clear all internal state without reallocation (FR-002).
    ///
    /// Resets DC blocker, smoothers, decay filters, and feedback buffer.
    /// Does not change parameter values or sample rate.
    void reset() noexcept {
        // Reset DC blocker
        dcBlocker_.reset();

        // Snap smoothers to current targets
        driveSmoother_.snapTo(drive_);
        mixSmoother_.snapTo(mix_);

        // Reset decay filters
        for (auto& filter : decayFilters_) {
            filter.reset();
        }

        // Reset feedback buffer
        feedbackBuffer_.fill(0.0f);

        // Reset crossover for Multiband mode
        crossover_.reset();
    }

    // =========================================================================
    // Mode Selection (FR-004 to FR-009)
    // =========================================================================

    /// @brief Set the processing algorithm mode (FR-004).
    /// @param mode FractalMode to use
    void setMode(FractalMode mode) noexcept {
        mode_ = mode;
    }

    /// @brief Get the current processing mode.
    [[nodiscard]] FractalMode getMode() const noexcept {
        return mode_;
    }

    // =========================================================================
    // Iteration Control (FR-010 to FR-012)
    // =========================================================================

    /// @brief Set recursion depth (FR-010).
    /// @param iterations Number of iterations, clamped to [1, 8] (FR-011)
    void setIterations(int iterations) noexcept {
        iterations_ = std::clamp(iterations, kMinIterations, kMaxIterations);
    }

    /// @brief Get current iteration count.
    [[nodiscard]] int getIterations() const noexcept {
        return iterations_;
    }

    // =========================================================================
    // Scale Factor (FR-013 to FR-015)
    // =========================================================================

    /// @brief Set amplitude reduction per level (FR-013).
    /// @param scale Scale factor, clamped to [0.3, 0.9] (FR-014)
    void setScaleFactor(float scale) noexcept {
        scaleFactor_ = std::clamp(scale, kMinScaleFactor, kMaxScaleFactor);
    }

    /// @brief Get current scale factor.
    [[nodiscard]] float getScaleFactor() const noexcept {
        return scaleFactor_;
    }

    // =========================================================================
    // Drive Control (FR-016 to FR-018)
    // =========================================================================

    /// @brief Set base distortion intensity (FR-016).
    /// @param drive Drive amount, clamped to [1.0, 20.0] (FR-017)
    /// @note Changes are smoothed over 10ms (FR-018)
    void setDrive(float drive) noexcept {
        drive_ = std::clamp(drive, kMinDrive, kMaxDrive);
        driveSmoother_.setTarget(drive_);
    }

    /// @brief Get current drive value.
    [[nodiscard]] float getDrive() const noexcept {
        return drive_;
    }

    // =========================================================================
    // Mix Control (FR-019 to FR-022)
    // =========================================================================

    /// @brief Set dry/wet balance (FR-019).
    /// @param mix Mix amount, clamped to [0.0, 1.0] (FR-020)
    /// @note Changes are smoothed over 10ms (FR-022)
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, kMinMix, kMaxMix);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get current mix value.
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Frequency Decay (FR-023 to FR-025)
    // =========================================================================

    /// @brief Set high-frequency emphasis at deeper levels (FR-023).
    /// @param decay Frequency decay amount, clamped to [0.0, 1.0] (FR-024)
    void setFrequencyDecay(float decay) noexcept {
        const float newDecay = std::clamp(decay, kMinFrequencyDecay, kMaxFrequencyDecay);
        if (newDecay == frequencyDecay_) return;  // Skip if unchanged
        frequencyDecay_ = newDecay;
        if (prepared_) {
            updateDecayFilters();
        }
    }

    /// @brief Get current frequency decay value.
    [[nodiscard]] float getFrequencyDecay() const noexcept {
        return frequencyDecay_;
    }

    // =========================================================================
    // Multiband Mode (FR-030 to FR-033)
    // =========================================================================

    /// @brief Set base crossover frequency for multiband mode (FR-031).
    /// After prepare() the crossovers glide to the new split over 5 ms.
    /// @param hz Crossover frequency in Hz
    void setCrossoverFrequency(float hz) noexcept {
        crossoverFrequency_ = std::max(20.0f, hz);
        if (prepared_) {
            updateCrossoverFrequencies();
        }
    }

    /// @brief Get current crossover frequency.
    [[nodiscard]] float getCrossoverFrequency() const noexcept {
        return crossoverFrequency_;
    }

    /// @brief Set iteration reduction scale for lower bands (FR-032).
    /// @param scale Band iteration scale, clamped to [0.0, 1.0]
    void setBandIterationScale(float scale) noexcept {
        bandIterationScale_ = std::clamp(scale, 0.0f, 1.0f);
    }

    /// @brief Get current band iteration scale.
    [[nodiscard]] float getBandIterationScale() const noexcept {
        return bandIterationScale_;
    }

    // =========================================================================
    // Harmonic Mode (FR-034 to FR-038)
    // =========================================================================

    /// @brief Set waveshaper curve for odd harmonics (FR-036).
    /// @param type WaveshapeType to use for odd harmonic processing
    void setOddHarmonicCurve(WaveshapeType type) noexcept {
        oddHarmonicCurve_ = type;
    }

    /// @brief Set waveshaper curve for even harmonics (FR-036).
    /// @param type WaveshapeType to use for even harmonic processing
    void setEvenHarmonicCurve(WaveshapeType type) noexcept {
        evenHarmonicCurve_ = type;
    }

    /// @brief Get current odd harmonic curve type.
    [[nodiscard]] WaveshapeType getOddHarmonicCurve() const noexcept {
        return oddHarmonicCurve_;
    }

    /// @brief Get current even harmonic curve type.
    [[nodiscard]] WaveshapeType getEvenHarmonicCurve() const noexcept {
        return evenHarmonicCurve_;
    }

    // =========================================================================
    // Cascade Mode (FR-039 to FR-041)
    // =========================================================================

    /// @brief Set waveshaper type for a specific iteration level (FR-039).
    /// @param level Level index (0 to iterations-1)
    /// @param type WaveshapeType to use at this level
    /// @note Invalid level indices are safely ignored (FR-041)
    void setLevelWaveshaper(int level, WaveshapeType type) noexcept {
        if (level >= 0 && level < kMaxIterations) {
            levelWaveshapers_[static_cast<size_t>(level)] = type;
            waveshapers_[static_cast<size_t>(level)].setType(type);
        }
    }

    /// @brief Get waveshaper type for a specific level.
    /// @param level Level index (0 to kMaxIterations-1)
    /// @return WaveshapeType at level, or Tanh if level is invalid
    [[nodiscard]] WaveshapeType getLevelWaveshaper(int level) const noexcept {
        if (level >= 0 && level < kMaxIterations) {
            return levelWaveshapers_[static_cast<size_t>(level)];
        }
        return WaveshapeType::Tanh;
    }

    // =========================================================================
    // Feedback Mode (FR-042 to FR-045)
    // =========================================================================

    /// @brief Set cross-level feedback amount (FR-042).
    /// @param amount Feedback amount, clamped to [0.0, 0.5]
    void setFeedbackAmount(float amount) noexcept {
        feedbackAmount_ = std::clamp(amount, kMinFeedbackAmount, kMaxFeedbackAmount);
    }

    /// @brief Get current feedback amount.
    [[nodiscard]] float getFeedbackAmount() const noexcept {
        return feedbackAmount_;
    }

    // =========================================================================
    // Blend Mode
    // =========================================================================

    /// @brief Set how iteration levels are combined into the final output.
    /// @param mode FractalBlendMode to use
    void setBlendMode(FractalBlendMode mode) noexcept {
        blendMode_ = mode;
    }

    /// @brief Get current blend mode.
    [[nodiscard]] FractalBlendMode getBlendMode() const noexcept {
        return blendMode_;
    }

    // =========================================================================
    // Curve Type (applies to all modes)
    // =========================================================================

    /// @brief Set the saturation curve type used across all modes.
    ///
    /// - Residual/Feedback/Multiband: replaces the default tanh saturation
    /// - Harmonic: sets both odd and even harmonic waveshaper curves
    /// - Cascade: sets the waveshaper type for all iteration levels
    ///
    /// @param type WaveshapeType to use (default: Tanh)
    void setCurveType(WaveshapeType type) noexcept {
        curveType_ = type;
        curveShaper_.setType(type);
        oddHarmonicCurve_ = type;
        evenHarmonicCurve_ = type;
        for (auto& ws : waveshapers_)
            ws.setType(type);
    }

    /// @brief Get current curve type.
    [[nodiscard]] WaveshapeType getCurveType() const noexcept {
        return curveType_;
    }

    // =========================================================================
    // Processing (FR-046 to FR-050)
    // =========================================================================

    /// @brief Process a single sample (FR-046).
    ///
    /// Applies fractal distortion based on the current mode setting.
    /// All processing is noexcept and allocation-free (FR-048).
    ///
    /// @param input Input sample (expected normalized [-1, 1])
    /// @return Processed output sample
    /// @note Real-time safe: noexcept, no allocations
    [[nodiscard]] float process(float input) noexcept {
        // Handle invalid input (Edge Case: NaN/Inf handling)
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // SC-004: Mix=0.0 produces bit-exact dry signal (FR-021)
        if (mix_ == 0.0f) {
            // Snap smoother to 0 so it's ready when mix changes
            mixSmoother_.snapTo(0.0f);
            return input;
        }

        // Store dry signal for mixing
        const float dry = input;

        // Get smoothed parameter values
        const float smoothedDrive = driveSmoother_.process();
        const float smoothedMix = mixSmoother_.process();

        // Process based on current mode
        float wet = 0.0f;
        switch (mode_) {
            case FractalMode::Residual:
                wet = processResidual(input, smoothedDrive);
                break;
            case FractalMode::Multiband:
                wet = processMultiband(input, smoothedDrive);
                break;
            case FractalMode::Harmonic:
                wet = processHarmonic(input, smoothedDrive);
                break;
            case FractalMode::Cascade:
                wet = processCascade(input, smoothedDrive);
                break;
            case FractalMode::Feedback:
                wet = processFeedback(input, smoothedDrive);
                break;
        }

        // Apply DC blocking (FR-050)
        wet = dcBlocker_.process(wet);

        // Mix dry/wet
        float output = (1.0f - smoothedMix) * dry + smoothedMix * wet;

        // Flush denormals (FR-049)
        output = detail::flushDenormal(output);

        return output;
    }

    /// @brief Process a block of samples in-place (FR-047).
    ///
    /// Equivalent to calling process() for each sample sequentially.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    /// @note Real-time safe: noexcept, no allocations (FR-048)
    void process(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr) return;

        size_t i = 0;
        while (i < numSamples) {
            const size_t len = std::min(kBlockChunk, numSamples - i);
            if (mode_ == FractalMode::Harmonic && mix_ != 0.0f && allFinite(buffer + i, len)) {
                processHarmonicChunk(buffer + i, len);
            } else {
                for (size_t k = 0; k < len; ++k) {
                    buffer[i + k] = process(buffer[i + k]);
                }
            }
            i += len;
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Level Combination
    // =========================================================================

    /// @brief Combine iteration levels according to the current blend mode.
    /// @param levels Array of per-iteration level outputs
    /// @param count Number of active iterations
    /// @return Combined output sample
    float combineLevels(const std::array<float, kMaxIterations>& levels,
                        int count) const noexcept {
        if (count <= 0) return 0.0f;

        switch (blendMode_) {
            case FractalBlendMode::Add:
            default: {
                float sum = 0.0f;
                for (int i = 0; i < count; ++i)
                    sum += levels[static_cast<size_t>(i)];
                return sum;
            }
            case FractalBlendMode::Multiply: {
                // Each level ring-modulates the accumulator
                float result = levels[0];
                for (int i = 1; i < count; ++i)
                    result *= (1.0f + levels[static_cast<size_t>(i)]);
                return result;
            }
            case FractalBlendMode::Max: {
                // Peak selection: loudest level wins (preserves sign)
                float best = levels[0];
                float bestAbs = std::abs(best);
                for (int i = 1; i < count; ++i) {
                    const float a = std::abs(levels[static_cast<size_t>(i)]);
                    if (a > bestAbs) {
                        best = levels[static_cast<size_t>(i)];
                        bestAbs = a;
                    }
                }
                return best;
            }
            case FractalBlendMode::Screen: {
                // Audio screen blend: a + b - a*b
                // Soft union that naturally limits toward ±1
                float result = levels[0];
                for (int i = 1; i < count; ++i)
                    result = result + levels[static_cast<size_t>(i)]
                             - result * levels[static_cast<size_t>(i)];
                return result;
            }
        }
    }

    // =========================================================================
    // Mode-Specific Processing
    // =========================================================================

    /// @brief Process using Residual mode algorithm (FR-027 to FR-029).
    ///
    /// Implements recursive distortion where each level processes the residual
    /// (input minus sum of previous levels) with exponentially decreasing amplitude.
    ///
    /// Algorithm:
    /// - level[0] = tanh(input * drive)
    /// - level[N] = tanh(residual * scaleFactor^N * drive)
    /// - output = sum(all levels)
    [[nodiscard]] float processResidual(float input, float smoothedDrive) noexcept {
        std::array<float, kMaxIterations> levels{};

        // Level 0: saturate(input * drive) (FR-027)
        curveShaper_.setDrive(smoothedDrive);
        levels[0] = curveShaper_.process(input);
        levels[0] = detail::flushDenormal(levels[0]);

        // Apply frequency decay to level 0 if enabled
        if (frequencyDecay_ > 0.0f) {
            levels[0] = decayFilters_[0].process(levels[0]);
        }

        float sum = levels[0];

        // Subsequent levels (FR-028)
        float scalePower = scaleFactor_;
        for (int i = 1; i < iterations_; ++i) {
            // Compute residual: input - sum of previous levels
            const float residual = input - sum;

            // Apply saturation with scaled drive
            curveShaper_.setDrive(scalePower * smoothedDrive);
            levels[static_cast<size_t>(i)] = curveShaper_.process(residual);
            levels[static_cast<size_t>(i)] = detail::flushDenormal(levels[static_cast<size_t>(i)]);

            // Apply frequency decay if enabled
            if (frequencyDecay_ > 0.0f) {
                levels[static_cast<size_t>(i)] = decayFilters_[static_cast<size_t>(i)].process(
                    levels[static_cast<size_t>(i)]);
            }

            sum += levels[static_cast<size_t>(i)];
            scalePower *= scaleFactor_;
        }

        return combineLevels(levels, iterations_);
    }

    /// @brief Process using Multiband mode algorithm (FR-030 to FR-033).
    ///
    /// Splits signal into 4 bands with pseudo-octave spacing using MultibandCrossover
    /// with D'Appolito allpass compensation (the bands sum flat).
    /// Each band is processed with a different iteration count based on
    /// bandIterationScale: high bands get more iterations than low bands.
    ///
    /// Algorithm:
    /// - Split signal into Sub/Low/Mid/High bands
    /// - Band iterations = max(1, round(baseIterations * scale^(numBands-1-i)))
    /// - Process each band with its iteration count
    /// - Sum all bands for output
    [[nodiscard]] float processMultiband(float input, float smoothedDrive) noexcept {
        // Split input into 4 bands (sub, low, mid, high)
        std::array<float, kNumBands> bands{};
        crossover_.process(input, bands);

        // Calculate iterations per band (FR-033)
        // Band 0 = sub (fewest iterations), Band 3 = high (most iterations)
        std::array<int, kNumBands> bandIterations{};
        for (int i = 0; i < kNumBands; ++i) {
            bandIterations[static_cast<size_t>(i)] = calculateBandIterations(i);
        }

        // Process each band with its iteration count
        std::array<float, kNumBands> bandOutputs{};

        for (std::size_t b = 0; b < static_cast<std::size_t>(kNumBands); ++b) {
            bandOutputs[b] = processBandResidual(bands[b], smoothedDrive, bandIterations[b]);
        }

        // Combine bands using blend mode (FR-030)
        std::array<float, kMaxIterations> padded{};
        for (int i = 0; i < kNumBands; ++i)
            padded[static_cast<size_t>(i)] = bandOutputs[static_cast<size_t>(i)];
        return combineLevels(padded, kNumBands);
    }

    /// @brief Process using Harmonic mode algorithm (FR-034 to FR-038).
    ///
    /// Separates odd and even harmonics using Chebyshev polynomial extraction,
    /// then applies different saturation curves to each before recombining.
    ///
    /// Algorithm:
    /// - Extract odd harmonics via ChebyshevShaper (T1, T3, T5, T7)
    /// - Extract even harmonics via ChebyshevShaper (T2, T4, T6, T8)
    /// - Apply oddHarmonicCurve_ to odd component
    /// - Apply evenHarmonicCurve_ to even component
    /// - Recombine with recursive processing at each iteration level
    [[nodiscard]] float processHarmonic(float input, float smoothedDrive) noexcept {
        std::array<float, kMaxIterations> levels{};

        // Level 0: Process input through both harmonic shapers
        const float drivenInput = input * smoothedDrive;

        // Clamp driven input to avoid extreme values
        const float clampedInput = std::clamp(drivenInput, -1.0f, 1.0f);

        // Extract odd and even harmonic components (ChebyshevShaper::process's arithmetic, through
        // the block path's helper)
        float oddComponent = 0.0f;
        float evenComponent = 0.0f;
        chebyshevChunk(oddHarmonicShaper_.getHarmonicLevels(), &clampedInput, &oddComponent, 1);
        chebyshevChunk(evenHarmonicShaper_.getHarmonicLevels(), &clampedInput, &evenComponent, 1);

        // Apply saturation curves based on configured waveshaper types
        oddWaveshaper_.setType(oddHarmonicCurve_);
        oddWaveshaper_.setDrive(1.0f);
        evenWaveshaper_.setType(evenHarmonicCurve_);
        evenWaveshaper_.setDrive(1.0f);

        oddComponent = oddWaveshaper_.process(oddComponent);
        oddComponent = detail::flushDenormal(oddComponent);
        evenComponent = evenWaveshaper_.process(evenComponent);
        evenComponent = detail::flushDenormal(evenComponent);

        // Combine odd and even components for level 0
        levels[0] = (oddComponent + evenComponent) * 0.5f;

        // Apply frequency decay if enabled
        if (frequencyDecay_ > 0.0f) {
            levels[0] = decayFilters_[0].process(levels[0]);
        }

        float sum = levels[0];

        // Subsequent levels: process residual with scaled drive
        float scalePower = scaleFactor_;
        for (int i = 1; i < iterations_; ++i) {
            const float residual = input - sum;
            const float drivenResidual = residual * scalePower * smoothedDrive;
            const float clampedResidual = std::clamp(drivenResidual, -1.0f, 1.0f);

            // Apply harmonic separation at each level
            float oddRes = 0.0f;
            float evenRes = 0.0f;
            chebyshevChunk(oddHarmonicShaper_.getHarmonicLevels(), &clampedResidual, &oddRes, 1);
            chebyshevChunk(evenHarmonicShaper_.getHarmonicLevels(), &clampedResidual, &evenRes, 1);

            oddRes = oddWaveshaper_.process(oddRes);
            oddRes = detail::flushDenormal(oddRes);
            evenRes = evenWaveshaper_.process(evenRes);
            evenRes = detail::flushDenormal(evenRes);

            levels[static_cast<size_t>(i)] = (oddRes + evenRes) * 0.5f;

            if (frequencyDecay_ > 0.0f) {
                levels[static_cast<size_t>(i)] = decayFilters_[static_cast<size_t>(i)].process(
                    levels[static_cast<size_t>(i)]);
            }

            sum += levels[static_cast<size_t>(i)];
            scalePower *= scaleFactor_;
        }

        return combineLevels(levels, iterations_);
    }

    /// Samples per level-major chunk of the block path (processHarmonicChunk).
    static constexpr size_t kBlockChunk = 64;
    /// Samples per sub-chunk of processHarmonicChunk's skewed schedule (a multiple of 4: the SSE2
    /// curve's lanes cover the same samples as in one 64-sample pass).
    static constexpr size_t kSkewChunk = 8;

    /// @brief processHarmonic()'s level for each of @p n <= kSkewChunk clamped inputs: both
    /// Clenshaw recurrences step through the chunk together (the samples are independent, so the
    /// compiler vectorises the steps). Per sample: the operations of ChebyshevShaper::process,
    /// Waveshaper::process and flushDenormal, in their order. The curves stay per sample: a
    /// vectorised x / sqrt(x^2 + 1) under MSVC /fp:fast is not the scalar result (1-3 ulp apart,
    /// which the Max blend's level selection carried to -50 dB of difference, round 5).
    void harmonicLevelChunk(const float* clamped, float* out, size_t n) const noexcept {
        std::array<float, kSkewChunk> odd{};
        std::array<float, kSkewChunk> even{};
#if KRATE_FRACTAL_CURVE_SSE2
        if (n == kSkewChunk) {
            chebyshevChunkSse2(oddHarmonicShaper_.getHarmonicLevels(), clamped, odd.data());
            chebyshevChunkSse2(evenHarmonicShaper_.getHarmonicLevels(), clamped, even.data());
        } else
#endif
        {
            chebyshevChunk(oddHarmonicShaper_.getHarmonicLevels(), clamped, odd.data(), n);
            chebyshevChunk(evenHarmonicShaper_.getHarmonicLevels(), clamped, even.data(), n);
        }
        size_t k = 0;
#if KRATE_FRACTAL_CURVE_SSE2
        // ReciprocalSqrt (the default curve) four samples at a time in SSE2
        // (detail::fractalRecipSqrtMixSse2, every lane the scalar result). Only for drives that keep
        // x * x finite, where recipSqrt's infinity branch is never taken.
        if (oddHarmonicCurve_ == WaveshapeType::ReciprocalSqrt && evenHarmonicCurve_ == WaveshapeType::ReciprocalSqrt &&
            oddWaveshaper_.getDrive() != 0.0f && evenWaveshaper_.getDrive() != 0.0f &&
            boundedForRecipSqrt(odd.data(), oddWaveshaper_, n) && boundedForRecipSqrt(even.data(), evenWaveshaper_, n)) {
            k = n & ~static_cast<size_t>(3);
            detail::fractalRecipSqrtMixSse2(odd.data(), oddWaveshaper_.getDrive(), oddWaveshaper_.getAsymmetry(),
                                            even.data(), evenWaveshaper_.getDrive(), evenWaveshaper_.getAsymmetry(),
                                            out, k);
        }
#endif
        for (; k < n; ++k) {
            const float o = detail::flushDenormal(oddWaveshaper_.process(odd[k]));
            const float e = detail::flushDenormal(evenWaveshaper_.process(even[k]));
            out[k] = (o + e) * 0.5f;
        }
    }

#if KRATE_FRACTAL_CURVE_SSE2
    /// @brief True when drive * x + asymmetry stays below 1e18 in magnitude for every x of
    /// @p x[0, n) (its square, and so recipSqrt's x * x + 1, is then finite).
    [[nodiscard]] static bool boundedForRecipSqrt(const float* x, const Waveshaper& ws, size_t n) noexcept {
        constexpr float kBound = 1.0e18f;
        // Four at a time (a NaN compares false, as in the scalar test), the rest one by one
        const __m128 drive = _mm_set1_ps(ws.getDrive());
        const __m128 asym = _mm_set1_ps(ws.getAsymmetry());
        const __m128 bound = _mm_set1_ps(kBound);
        const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
        size_t k = 0;
        for (; k + 4 <= n; k += 4) {
            const __m128 t = _mm_add_ps(_mm_mul_ps(drive, _mm_loadu_ps(x + k)), asym);
            if (_mm_movemask_ps(_mm_cmplt_ps(_mm_and_ps(t, absMask), bound)) != 0xF) return false;
        }
        for (; k < n; ++k) {
            const float t = ws.getDrive() * x[k] + ws.getAsymmetry();
            if (!(std::abs(t) < kBound)) return false;
        }
        return true;
    }

    /// @brief chebyshevChunk() over a full sub-chunk (kSkewChunk = 8 samples, two SSE2 vectors):
    /// detail::fractalChebyshev8Sse2.
    static void chebyshevChunkSse2(const std::array<float, ChebyshevShaper::kMaxHarmonics>& weights, const float* x,
                                   float* out) noexcept {
        static_assert(kSkewChunk == 8, "two SSE2 vectors per sub-chunk");
        detail::fractalChebyshev8Sse2(weights.data(), ChebyshevShaper::kMaxHarmonics, x, out);
    }
#endif

    /// @brief Chebyshev::harmonicMix(x[k], weights, 8) for k in [0, n): the Clenshaw recurrence
    /// with each step taken for the whole chunk (n <= kSkewChunk). processHarmonic() calls it too
    /// (n = 1), so both paths share this arithmetic. Under clang it runs in source order, unfused:
    /// -ffast-math (the macOS leg) otherwise contracts and reassociates each inlined copy on its
    /// own, and the decay filters carry that ulp-level difference to 5e-3 between the paths.
    static void chebyshevChunk(const std::array<float, ChebyshevShaper::kMaxHarmonics>& weights, const float* x,
                               float* out, size_t n) noexcept {
#ifdef __clang__
#pragma clang fp contract(off) reassociate(off)
#endif
        std::array<float, kSkewChunk> b1{};
        std::array<float, kSkewChunk> b2{};
        for (int h = ChebyshevShaper::kMaxHarmonics; h >= 1; --h) {
            const float c = weights[static_cast<size_t>(h - 1)];
            for (size_t k = 0; k < n; ++k) {
                const float b0 = c + 2.0f * x[k] * b1[k] - b2[k];
                b2[k] = b1[k];
                b1[k] = b0;
            }
        }
        for (size_t k = 0; k < n; ++k) {
            out[k] = x[k] * b1[k] - b2[k];
        }
    }

    /// @brief True when every sample of @p x[0, n) is finite (process() resets on any other).
    [[nodiscard]] static bool allFinite(const float* x, size_t n) noexcept {
        for (size_t k = 0; k < n; ++k) {
            if (!detail::isFinite(x[k])) return false;
        }
        return true;
    }

    /// @brief process() over @p n <= kBlockChunk finite samples in Harmonic mode at a non-zero Mix,
    /// in level-major order: every sample's level 0, then every sample's level 1, ... A sample's
    /// levels form one dependent chain (two Chebyshev Clenshaw recurrences and the residual per
    /// level), so per sample the processor waits on that chain; across samples the chains are
    /// independent apart from the frequency-decay filters, which still run each level's samples in
    /// order. Every sample takes the same operations in the same order as processHarmonic():
    /// the result is the per-sample one.
    ///
    /// The levels run skewed over kSkewChunk-sample sub-chunks: step t takes level i on sub-chunk
    /// t - i, whose inputs (the levels below it on that sub-chunk) the steps before completed. The
    /// decay filters of a step's levels then run interleaved sample by sample, so their recursions
    /// overlap instead of each level's filter making its own serial pass (round 6 CPU). Each
    /// filter still takes its level's samples in order, and each sample its levels in order.
    void processHarmonicChunk(float* buffer, size_t n) noexcept {
        // The smoothers and the DC blocker run on copies in locals (their recursions then stay in
        // registers), written back after the chunk
        std::array<float, kBlockChunk> drive{};
        std::array<float, kBlockChunk> mix{};
        OnePoleSmoother driveSmoother = driveSmoother_;
        OnePoleSmoother mixSmoother = mixSmoother_;
        for (size_t k = 0; k < n; ++k) {
            drive[k] = driveSmoother.process();
            mix[k] = mixSmoother.process();
        }
        driveSmoother_ = driveSmoother;
        mixSmoother_ = mixSmoother;
        oddWaveshaper_.setType(oddHarmonicCurve_);
        oddWaveshaper_.setDrive(1.0f);
        evenWaveshaper_.setType(evenHarmonicCurve_);
        evenWaveshaper_.setDrive(1.0f);
        std::array<std::array<float, kBlockChunk>, kMaxIterations> levels{};
        std::array<float, kBlockChunk> sum{};
        // Level i's residual gain, scaleFactor_^i by the per-sample loop's running product
        std::array<float, kMaxIterations> scalePowers{};
        float scalePower = scaleFactor_;
        for (int i = 1; i < iterations_; ++i) {
            scalePowers[static_cast<size_t>(i)] = scalePower;
            scalePower *= scaleFactor_;
        }
        const bool decay = frequencyDecay_ > 0.0f;
        const auto levelCount = static_cast<size_t>(iterations_);
        const size_t subChunks = (n + kSkewChunk - 1) / kSkewChunk;
        for (size_t t = 0; t + 1 < subChunks + levelCount; ++t) {
            const size_t first = t >= subChunks ? t - subChunks + 1 : 0;  // the step's levels
            const size_t last = std::min(levelCount - 1, t);
            for (size_t i = first; i <= last; ++i) {
                const size_t start = (t - i) * kSkewChunk;
                const size_t len = std::min(kSkewChunk, n - start);
                std::array<float, kSkewChunk> clamped{};
                if (i == 0) {
                    for (size_t k = 0; k < len; ++k) {
                        clamped[k] = std::clamp(buffer[start + k] * drive[start + k], -1.0f, 1.0f);
                    }
                } else {
                    for (size_t k = 0; k < len; ++k) {
                        const float residual = buffer[start + k] - sum[start + k];
                        clamped[k] = std::clamp(residual * scalePowers[i] * drive[start + k], -1.0f, 1.0f);
                    }
                }
                harmonicLevelChunk(clamped.data(), levels[i].data() + start, len);
            }
            if (decay) {
                for (size_t k = 0; k < kSkewChunk; ++k) {
                    for (size_t i = first; i <= last; ++i) {
                        const size_t s = (t - i) * kSkewChunk + k;
                        if (s < n) levels[i][s] = decayFilters_[i].process(levels[i][s]);
                    }
                }
            }
            for (size_t i = first; i <= last; ++i) {
                const size_t start = (t - i) * kSkewChunk;
                const size_t end = std::min(start + kSkewChunk, n);
                if (i == 0) {
                    for (size_t k = start; k < end; ++k) sum[k] = levels[0][k];
                } else {
                    for (size_t k = start; k < end; ++k) sum[k] += levels[i][k];
                }
            }
        }
        DCBlocker dcBlocker = dcBlocker_;
        for (size_t k = 0; k < n; ++k) {
            std::array<float, kMaxIterations> sampleLevels{};
            for (int i = 0; i < iterations_; ++i) {
                sampleLevels[static_cast<size_t>(i)] = levels[static_cast<size_t>(i)][k];
            }
            float wet = combineLevels(sampleLevels, iterations_);
            wet = dcBlocker.process(wet);
            const float output = (1.0f - mix[k]) * buffer[k] + mix[k] * wet;
            buffer[k] = detail::flushDenormal(output);
        }
        dcBlocker_ = dcBlocker;
    }

    /// @brief Process using Cascade mode algorithm (FR-039 to FR-041).
    ///
    /// Uses different waveshaper types at each iteration level for varied
    /// harmonic evolution (e.g., warm->harsh progression).
    ///
    /// Algorithm:
    /// - level[0] = waveshapers_[0].process(input * drive)
    /// - level[N] = waveshapers_[N].process(residual * scaleFactor^N * drive)
    /// - output = sum(all levels)
    [[nodiscard]] float processCascade(float input, float smoothedDrive) noexcept {
        std::array<float, kMaxIterations> levels{};

        // Level 0: Use waveshaper[0] with configured type
        waveshapers_[0].setDrive(smoothedDrive);
        levels[0] = waveshapers_[0].process(input);
        levels[0] = detail::flushDenormal(levels[0]);

        // Apply frequency decay if enabled
        if (frequencyDecay_ > 0.0f) {
            levels[0] = decayFilters_[0].process(levels[0]);
        }

        float sum = levels[0];

        // Subsequent levels with different waveshaper types
        float scalePower = scaleFactor_;
        for (int i = 1; i < iterations_; ++i) {
            const float residual = input - sum;

            // Configure waveshaper for this level
            waveshapers_[static_cast<size_t>(i)].setDrive(scalePower * smoothedDrive);

            // Process through level's waveshaper
            levels[static_cast<size_t>(i)] = waveshapers_[static_cast<size_t>(i)].process(residual);
            levels[static_cast<size_t>(i)] = detail::flushDenormal(levels[static_cast<size_t>(i)]);

            // Apply frequency decay if enabled
            if (frequencyDecay_ > 0.0f) {
                levels[static_cast<size_t>(i)] = decayFilters_[static_cast<size_t>(i)].process(
                    levels[static_cast<size_t>(i)]);
            }

            sum += levels[static_cast<size_t>(i)];
            scalePower *= scaleFactor_;
        }

        return combineLevels(levels, iterations_);
    }

    /// @brief Process using Feedback mode algorithm (FR-042 to FR-045).
    ///
    /// Cross-feeds between iteration levels using previous sample's outputs
    /// stored in feedbackBuffer_. Creates chaotic but bounded textures.
    ///
    /// Algorithm:
    /// - level[0] = tanh(input * drive)
    /// - level[N] = tanh((residual + feedbackAmount * feedbackBuffer_[N-1]) * scaleFactor^N * drive)
    /// - Store current level outputs in feedbackBuffer_ for next sample
    /// - output = sum(all levels)
    [[nodiscard]] float processFeedback(float input, float smoothedDrive) noexcept {
        std::array<float, kMaxIterations> levels{};

        // Level 0: saturate(input * drive) - no feedback on first level
        curveShaper_.setDrive(smoothedDrive);
        levels[0] = curveShaper_.process(input);
        levels[0] = detail::flushDenormal(levels[0]);

        // Apply frequency decay if enabled
        if (frequencyDecay_ > 0.0f) {
            levels[0] = decayFilters_[0].process(levels[0]);
        }

        float sum = levels[0];

        // Subsequent levels with cross-level feedback
        float scalePower = scaleFactor_;
        for (int i = 1; i < iterations_; ++i) {
            // Compute residual
            const float residual = input - sum;

            // Add feedback from previous sample's level[i-1] (FR-043, FR-044)
            const float feedbackContribution = feedbackAmount_ * feedbackBuffer_[static_cast<size_t>(i - 1)];

            // Combine residual with feedback and apply saturation
            const float combinedInput = residual + feedbackContribution;
            curveShaper_.setDrive(scalePower * smoothedDrive);
            levels[static_cast<size_t>(i)] = curveShaper_.process(combinedInput);
            levels[static_cast<size_t>(i)] = detail::flushDenormal(levels[static_cast<size_t>(i)]);

            // Apply frequency decay if enabled
            if (frequencyDecay_ > 0.0f) {
                levels[static_cast<size_t>(i)] = decayFilters_[static_cast<size_t>(i)].process(
                    levels[static_cast<size_t>(i)]);
            }

            sum += levels[static_cast<size_t>(i)];
            scalePower *= scaleFactor_;
        }

        // Store current levels in feedback buffer for next sample (FR-043)
        for (int i = 0; i < iterations_; ++i) {
            feedbackBuffer_[static_cast<size_t>(i)] = levels[static_cast<size_t>(i)];
        }

        // Combine levels using blend mode
        float output = combineLevels(levels, iterations_);

        // Apply soft limiting only when feedback is active to prevent runaway (FR-045)
        // When feedbackAmount=0, this should match Residual mode exactly
        if (feedbackAmount_ > 0.0f) {
            output = Sigmoid::tanh(output);
        }

        return output;
    }

    // =========================================================================
    // Helper Methods
    // =========================================================================

    /// @brief Update frequency decay filter configurations.
    ///
    /// Configures highpass filters for each level based on frequencyDecay parameter.
    /// Level N is highpass-filtered at baseFrequency * (N+1) (FR-025).
    ///
    /// @note Does NOT reset filter state — only updates coefficients.
    /// Resetting state on every parameter change causes audible clicks when
    /// parameters are automated (e.g., morph system, host automation).
    /// Filter state is reset separately via reset() during initialization.
    void updateDecayFilters() noexcept {
        for (int i = 0; i < kMaxIterations; ++i) {
            // Calculate cutoff frequency: baseFrequency * (level + 1) * frequencyDecay
            const float cutoff = kBaseDecayFrequency * static_cast<float>(i + 1) * frequencyDecay_;

            // Configure as highpass with Butterworth Q (FR-025)
            if (cutoff > 0.0f && frequencyDecay_ > 0.0f) {
                decayFilters_[static_cast<size_t>(i)].configure(
                    FilterType::Highpass,
                    cutoff,
                    0.707f,  // Butterworth Q
                    0.0f,    // No gain
                    static_cast<float>(sampleRate_)
                );
            }
            // Note: filter state is NOT reset here. Resetting on every
            // setFrequencyDecay() call causes discontinuities when parameters
            // are automated. State is only reset via reset() or prepare().
        }
    }

    /// @brief Calculate iterations for a multiband band (FR-033).
    /// @param bandIndex Band index (0 = sub/low, 3 = high)
    /// @return Number of iterations for this band
    [[nodiscard]] int calculateBandIterations(int bandIndex) const noexcept {
        // Formula: bandIterations[i] = max(1, round(baseIterations * scale^(numBands - 1 - i)))
        const float scalePower = std::pow(bandIterationScale_,
                                          static_cast<float>(kNumBands - 1 - bandIndex));
        const int result = std::max(1, static_cast<int>(
            std::round(static_cast<float>(iterations_) * scalePower)));
        return result;
    }

    /// @brief Update crossover frequencies based on crossoverFrequency parameter.
    ///
    /// Uses pseudo-octave spacing with ratios 1:4:16:
    /// - Sub-Low crossover: crossoverFrequency / 4
    /// - Low-Mid crossover: crossoverFrequency
    /// - Mid-High crossover: crossoverFrequency * 4
    void updateCrossoverFrequencies() noexcept {
        // Pseudo-octave spacing (FR-030): base/4, base, base*4
        const float subLow = crossoverFrequency_ / 4.0f;
        const float lowMid = crossoverFrequency_;
        const float midHigh = crossoverFrequency_ * 4.0f;

        // Clamp to valid range (monotone, so the three stay ascending)
        const float maxFreq = static_cast<float>(sampleRate_) * 0.45f;
        crossover_.setCrossoverFrequency(0, std::clamp(subLow, 20.0f, maxFreq));
        crossover_.setCrossoverFrequency(1, std::clamp(lowMid, 20.0f, maxFreq));
        crossover_.setCrossoverFrequency(2, std::clamp(midHigh, 20.0f, maxFreq));
    }

    /// @brief Process a single band using Residual algorithm with custom iteration count.
    /// @param input Band input sample
    /// @param smoothedDrive Drive value (already smoothed)
    /// @param numIterations Number of iterations to apply (may differ from iterations_)
    /// @return Processed band output
    [[nodiscard]] float processBandResidual(float input, float smoothedDrive,
                                            int numIterations) noexcept {
        std::array<float, kMaxIterations> levels{};

        // Level 0
        curveShaper_.setDrive(smoothedDrive);
        levels[0] = curveShaper_.process(input);
        levels[0] = detail::flushDenormal(levels[0]);

        float sum = levels[0];

        // Subsequent levels
        float scalePower = scaleFactor_;
        for (int i = 1; i < numIterations && i < kMaxIterations; ++i) {
            const float residual = input - sum;
            curveShaper_.setDrive(scalePower * smoothedDrive);
            levels[static_cast<size_t>(i)] = curveShaper_.process(residual);
            levels[static_cast<size_t>(i)] = detail::flushDenormal(levels[static_cast<size_t>(i)]);
            sum += levels[static_cast<size_t>(i)];
            scalePower *= scaleFactor_;
        }

        return combineLevels(levels, numIterations);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Components
    std::array<Waveshaper, kMaxIterations> waveshapers_;      ///< Per-level waveshapers
    std::array<Biquad, kMaxIterations> decayFilters_;         ///< Per-level highpass for frequencyDecay
    DCBlocker dcBlocker_;                                      ///< Post-processing DC removal
    OnePoleSmoother driveSmoother_;                            ///< Drive parameter smoothing
    OnePoleSmoother mixSmoother_;                              ///< Mix parameter smoothing
    Waveshaper curveShaper_;                                    ///< General saturation curve for Residual/Feedback/Multiband

    // Multiband mode components (FR-030)
    MultibandCrossover<kNumBands> crossover_;                  ///< 4-band splitter, allpass-compensated

    // Harmonic mode components (FR-034, FR-035)
    ChebyshevShaper oddHarmonicShaper_;                        ///< Odd harmonic extraction (T1, T3, T5, T7)
    ChebyshevShaper evenHarmonicShaper_;                       ///< Even harmonic extraction (T2, T4, T6, T8)
    Waveshaper oddWaveshaper_;                                 ///< Saturation curve for odd harmonics
    Waveshaper evenWaveshaper_;                                ///< Saturation curve for even harmonics

    // Feedback mode state (FR-043)
    std::array<float, kMaxIterations> feedbackBuffer_{};       ///< Previous sample's level outputs

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Parameters
    FractalMode mode_ = FractalMode::Residual;
    int iterations_ = 4;
    float scaleFactor_ = 0.5f;
    float drive_ = 2.0f;
    float mix_ = 1.0f;
    float frequencyDecay_ = 0.0f;
    float crossoverFrequency_ = kDefaultCrossoverFrequency;
    float bandIterationScale_ = kDefaultBandIterationScale;
    WaveshapeType curveType_ = WaveshapeType::Tanh;             ///< Saturation curve for all modes
    WaveshapeType oddHarmonicCurve_ = WaveshapeType::Tanh;     ///< FR-037: Default Tanh
    WaveshapeType evenHarmonicCurve_ = WaveshapeType::Tube;    ///< FR-037: Default Tube
    std::array<WaveshapeType, kMaxIterations> levelWaveshapers_{};  ///< Cascade mode per-level types
    float feedbackAmount_ = 0.0f;
    FractalBlendMode blendMode_ = FractalBlendMode::Add;          ///< Level combination mode
};

} // namespace Krate::DSP

